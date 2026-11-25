// Tests of sound_effects.cpp (CSoundData::GetEffect and PurgeEffects).
//
// Two CSoundData objects are opened by the original Open on the real SND
// folder (read only) under the same scripted DirectSound (FakeDSound, which
// also takes the samples the effects copy into their buffers and logs their
// hashes). Then a scripted sequence runs on each: the original GetEffect /
// PurgeEffects on one object, ours on the other, with the same fake clock
// (timeGetTime), the same CRT rand seed (variant -1) and the same counter
// of CSoundEffect::Play (0x7a0c00); between the calls the original
// CSoundEffect::Play plays some of the returned effects (it makes the
// duplicate buffers that PurgeStoppedDuplicates looks at). Ids: valid
// sounds of both files (and the colliding keys of the two files), variants
// -1 or in range. Compared: which call's effect each call returned, the
// effects dictionary (keys in order, values as call numbers) and the
// last-use dictionary, the DirectSound log (buffer creation, sample
// hashes, status, duplicates), DebugLog and the exception (a failing
// DirectSound call ends the sequence through GetEffect's catch (...)).

#include "game/debug_log.h"
#include "game/framework/directx_sound.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/sound/sound_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_dsound.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

void* __fastcall CSoundData_GetEffect(void* self, void* edx, std::uint32_t id, std::uint32_t variant);
void __fastcall CSoundData_PurgeEffects(void* self, void* edx, std::uint32_t id, std::uint32_t variant,
                                        std::uint32_t keep);
void* __fastcall CSoundData_Construct(void* self, void* edx);
void* __fastcall CSoundData_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void __fastcall CSoundData_Open(void* self, void* edx, std::uint32_t window, const msvc6::String* basePath,
                                std::uint32_t detachMode, std::uint32_t format);

using SndGetEffectFn = decltype(&CSoundData_GetEffect);
using SndPurgeEffectsFn = decltype(&CSoundData_PurgeEffects);
using SndConstructFn = decltype(&CSoundData_Construct);
using SndDeletingFn = decltype(&CSoundData_DeletingDestroy);
using SndOpenFn = decltype(&CSoundData_Open);

extern SndGetEffectFn Original_CSoundData_GetEffect;
extern SndPurgeEffectsFn Original_CSoundData_PurgeEffects;
extern SndConstructFn Original_CSoundData_Construct;
extern SndDeletingFn Original_CSoundData_DeletingDestroy;
extern SndOpenFn Original_CSoundData_Open;

namespace {

using sound::CSoundData;
using test::CaughtException;
using test::FakeDSound;

constexpr std::uintptr_t kCrtSrand = 0x0061f7f5;
constexpr std::uintptr_t kEffectPlay = 0x005c2fc0;  // DirectX::CSoundEffect::Play (volume, pan, flags)

struct Functions {
    SndGetEffectFn getEffect;
    SndPurgeEffectsFn purge;
};

// --------------------------------------------------------------------------
// The fake clock
// --------------------------------------------------------------------------

struct Clock {
    std::uint32_t now = 0;
    test::Rng rng{0};
};
Clock* g_clock = nullptr;

std::uint32_t __stdcall FakeTimeGetTime() {
    static const std::uint32_t kSteps[] = {0, 0, 1, 2500, 1000, 60001, 300000, 100000};
    g_clock->now += kSteps[g_clock->rng.Below(8)];
    return g_clock->now;
}

// --------------------------------------------------------------------------
// The sequence
// --------------------------------------------------------------------------

enum class StepKind { GetEffect, Purge, Play };

struct Step {
    StepKind kind;
    std::uint32_t id;
    std::uint32_t variant;
    std::uint32_t keep;  // Purge: a key; Play: the call whose effect plays
    std::int32_t volume;
    std::int32_t pan;
    std::uint32_t flags;
};

// An effect's identity: the fake DirectSound buffer its own buffer got (the
// k-th buffer the script handed out), read while it is alive (+0x94: the
// CSoundEffectBuffer at +0x90, its interface at +4).
std::uint32_t EffectId(const void* effect) {
    if (effect == nullptr) {
        return 0xffffffff;
    }
    return test::DSoundLogPointer(*reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(effect) + 0x94));
}

struct Run {
    std::vector<void*> results;  // per GetEffect step
    std::vector<std::uint32_t> ids;  // their EffectId at the time
    std::vector<std::uint32_t> log;
    CaughtException caught;
};

struct Guarded {
    const Functions* f;
    CSoundData* sound;
    const Step* step;
    void* effect;  // Play
};

std::uint32_t RunStep(const Guarded* g) {
    switch (g->step->kind) {
        case StepKind::GetEffect:
            return reinterpret_cast<std::uint32_t>(g->f->getEffect(g->sound, nullptr, g->step->id, g->step->variant));
        case StepKind::Purge: g->f->purge(g->sound, nullptr, g->step->id, g->step->variant, g->step->keep); return 0;
        case StepKind::Play: {
            using PlayFn = void(__fastcall*)(void* self, void* edx, std::int32_t volume, std::int32_t pan,
                                             std::uint32_t flags);
            reinterpret_cast<PlayFn>(kEffectPlay)(g->effect, nullptr, g->step->volume, g->step->pan, g->step->flags);
            return 0;
        }
    }
    return 0;
}

bool GuardedStep(const Guarded* g, std::uint32_t* eax, CaughtException* caught) {
    __try {
        *eax = RunStep(g);
        return true;
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
    return false;
}

// Valid sound ids: file 0 indexes 0..118, file 1 index 0; variants -1 or
// below the sound's count (read from the mapped file).
std::uint32_t VariantCount(const CSoundData* sound, std::uint32_t id) {
    const std::uint32_t file = id >> 16;
    const auto* table = reinterpret_cast<const sound::SndTable*>(sound->tables[file]);
    return *reinterpret_cast<const std::uint32_t*>(table->sounds[id & 0xffff] + sound->views[file]);
}

std::vector<Step> MakeSteps(test::Rng& rng, const CSoundData* sound, int count) {
    std::vector<Step> steps;
    std::vector<std::uint32_t> keys;
    int gets = 0;
    for (int i = 0; i < count; ++i) {
        Step step{};
        const std::uint32_t r = rng.Below(10);
        // A small pool of ids so effects get reused (and 1:0 collides with 0:0).
        static const std::uint32_t kIds[] = {0, 1, 2, 5, 7, 0x10000, 40, 118, 3, 9};
        step.id = rng.Chance(1, 2) ? kIds[rng.Below(10)] : rng.Below(119);
        const std::uint32_t n = VariantCount(sound, step.id);
        step.variant = rng.Chance(1, 2) ? 0xffffffff : rng.Below(n);
        if (r < 6 || gets == 0) {
            step.kind = StepKind::GetEffect;
            ++gets;
            keys.push_back(step.id << 24 | (step.variant == 0xffffffff ? 0 : step.variant));
        } else if (r < 8) {
            step.kind = StepKind::Play;
            step.keep = rng.Below(static_cast<std::uint32_t>(gets));
            step.volume = -static_cast<std::int32_t>(rng.Below(3000));
            step.pan = rng.Range(-10000, 10000);
            step.flags = rng.Below(2);
        } else {
            step.kind = StepKind::Purge;
            if (rng.Chance(1, 3)) {
                step.id = 0xffffffff;
                step.variant = 0xffffffff;
            }
            step.keep = rng.Chance(1, 2) && !keys.empty() ? keys[rng.Below(static_cast<std::uint32_t>(keys.size()))]
                                                          : rng.Next32();
        }
        steps.push_back(step);
    }
    return steps;
}

// The dictionaries' contents: keys in order, the effects by EffectId, the
// last-use values.
std::vector<std::uint32_t> Contents(const CSoundData* sound) {
    std::vector<std::uint32_t> out;
    for (const fileformats::SkipListDict* dict : {&sound->effects, &sound->lastUse}) {
        out.push_back(dict->count);
        for (std::uint32_t node = *reinterpret_cast<const std::uint32_t*>(dict->header_ptr); node != 0;
             node = *reinterpret_cast<const std::uint32_t*>(node)) {
            const std::uint32_t key = *reinterpret_cast<const std::uint32_t*>(node - 8);
            const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(node - 4);
            out.push_back(key);
            if (dict == &sound->effects) {
                out.push_back(EffectId(reinterpret_cast<void*>(value)));
            } else {
                out.push_back(value);
            }
        }
    }
    return out;
}

// Opens a CSoundData with the original Open (quiet DirectSound).
CSoundData* OpenSound(std::uint64_t seed) {
    auto* sound = static_cast<CSoundData*>(msvc6::GameOperatorNew(sizeof(CSoundData)));
    Original_CSoundData_Construct(sound, nullptr);
    FakeDSound fake(seed, {0});
    FakeDSound::ResetObjects();
    sound->base.vtable = reinterpret_cast<std::uint32_t>(FakeDSound::Game(1)->vtable);
    msvc6::String basePath{};
    fake.Install();
    Original_CSoundData_Open(sound, nullptr, 0x1234, &basePath, 0, 2);
    fake.Uninstall();
    return sound;
}

void CloseSound(CSoundData* sound) {
    FakeDSound quiet(1, {0});
    quiet.Install();
    sound->base.vtable = vtables::kCSoundData;
    g_directSound = sound;
    Original_CSoundData_DeletingDestroy(sound, nullptr, 1);
    quiet.Uninstall();
}

// The cached effect with this EffectId, or null.
void* CachedEffect(const CSoundData* sound, std::uint32_t id) {
    if (id == 0xffffffff) {
        return nullptr;
    }
    for (std::uint32_t node = *reinterpret_cast<const std::uint32_t*>(sound->effects.header_ptr); node != 0;
         node = *reinterpret_cast<const std::uint32_t*>(node)) {
        void* const effect = *reinterpret_cast<void* const*>(node - 4);
        if (effect != nullptr && EffectId(effect) == id) {
            return effect;
        }
    }
    return nullptr;
}

// Whether every effect a Purge(id, variant) reads exists.
bool VariantsLoaded(const CSoundData* sound, const Step& step) {
    const std::uint32_t n = VariantCount(sound, step.id);
    for (std::uint32_t v = 0; v < n; ++v) {
        if (step.variant != 0xffffffff && step.variant != v) {
            continue;
        }
        const std::uint32_t key = step.id << 24 | v;
        bool found = false;
        for (std::uint32_t node = *reinterpret_cast<const std::uint32_t*>(sound->effects.header_ptr); node != 0;
             node = *reinterpret_cast<const std::uint32_t*>(node)) {
            if (*reinterpret_cast<const std::uint32_t*>(node - 8) == key &&
                *reinterpret_cast<const std::uint32_t*>(node - 4) != 0) {
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

Run RunSequence(const Functions& f, CSoundData* sound, const std::vector<Step>& steps, std::uint64_t seed,
                std::uint32_t failChance, std::uint32_t startTime) {
    Run run;
    Clock clock;
    clock.now = startTime;
    clock.rng = test::Rng(seed);
    g_clock = &clock;
    const auto fakeTime = reinterpret_cast<std::uintptr_t>(&FakeTimeGetTime);
    test::PatchBytes(g_importTimeGetTime.address(), &fakeTime, 4);
    reinterpret_cast<void(__cdecl*)(unsigned)>(kCrtSrand)(static_cast<unsigned>(seed));
    g_soundDuplicateCounter = 100;
    g_skipListRandomBits = 0x5a5a5a5a;
    g_skipListRandomBitsLeft = 0;
    // Failures without DSERR_BUFFERLOST: a lost effect buffer restores
    // itself only while the game's application object is active (0x5c28e0),
    // which doesn't exist in test mode.
    static const std::vector<std::int32_t> kFailures = {
        static_cast<std::int32_t>(0x88780078), static_cast<std::int32_t>(0x80004005),
        static_cast<std::int32_t>(0x8007000e), 1};
    test::FakeDSoundConfig config;
    config.failChance = failChance;
    config.failures = &kFailures;
    FakeDSound fake(seed ^ 0x5eed, config);
    g_directSound = sound;
    std::vector<DebugLogCall> logs;
    fake.Install();
    SetDebugLogCapture(&logs);
    for (const Step& step : steps) {
        Guarded g{&f, sound, &step, nullptr};
        if (step.kind == StepKind::Play) {
            // Only effects still in the cache (a purge deletes them; found by
            // identity, as a freed block may hold a new effect).
            g.effect = step.keep < run.ids.size() ? CachedEffect(sound, run.ids[step.keep]) : nullptr;
            if (g.effect == nullptr) {
                continue;
            }
        }
        if (step.kind == StepKind::Purge && step.id != 0xffffffff && !VariantsLoaded(sound, step)) {
            continue;  // the original reads an effect that was never made (a crash in both)
        }
        // DirectSound fails only inside GetEffect (a failed duplicate in
        // Play leaves a buffer without interface behind, which both
        // versions would then use).
        fake.config_.failChance = step.kind == StepKind::GetEffect ? failChance : 0;
        std::uint32_t eax = 0;
        if (!GuardedStep(&g, &eax, &run.caught)) {
            break;
        }
        if (step.kind == StepKind::GetEffect) {
            run.results.push_back(reinterpret_cast<void*>(eax));
            run.ids.push_back(EffectId(reinterpret_cast<void*>(eax)));
        }
    }
    SetDebugLogCapture(nullptr);
    fake.Uninstall();
    test::UnpatchBytes(g_importTimeGetTime.address());
    g_clock = nullptr;
    run.log = fake.log_;
    for (const DebugLogCall& call : logs) {
        run.log.push_back(0xdeb00000u);
        run.log.push_back(call.format);
        run.log.push_back(call.count > 0 ? call.args[0] : 0);
    }
    run.log.push_back(sound->createdTime - startTime);
    return run;
}

bool CompareOnce(test::Context& ctx, const Functions& ours, int length) {
    test::Rng& rng = ctx.rng();
    void* const savedSound = g_directSound.get();
    const std::uint32_t savedCounter = g_soundDuplicateCounter;
    const std::uint64_t openSeed = rng.Next64();
    CSoundData* const a = OpenSound(openSeed);
    CSoundData* const b = OpenSound(openSeed);
    const std::vector<Step> steps = MakeSteps(rng, a, length);
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t failChance = rng.Chance(1, 3) ? 1 : 0;
    const std::uint32_t startTime = rng.Chance(1, 2) ? rng.Next32() : a->createdTime + rng.Below(120000);
    a->enabled = b->enabled = rng.Chance(1, 10) ? 0 : 1;
    b->createdTime = a->createdTime;
    const Functions originals{Original_CSoundData_GetEffect, Original_CSoundData_PurgeEffects};
    const Run ra = RunSequence(originals, a, steps, seed, failChance, startTime);
    const Run rb = RunSequence(ours, b, steps, seed, failChance, startTime);
    bool same = S3RE_EXPECT(ra.caught == rb.caught);
    same &= S3RE_EXPECT(ra.ids == rb.ids);
    same &= S3RE_EXPECT(ra.log == rb.log);
    same &= S3RE_EXPECT(Contents(a) == Contents(b));
    same &= S3RE_EXPECT_EQ(a->createdTime, b->createdTime);
    if (!same) {
        for (std::size_t i = 0; i < ra.log.size() && i < rb.log.size(); ++i) {
            if (ra.log[i] != rb.log[i]) {
                ctx.Fail(__FILE__, __LINE__, "  first log difference at %u: %#x %#x", static_cast<unsigned>(i), ra.log[i],
                         rb.log[i]);
                break;
            }
        }
        for (std::size_t i = 0; i < steps.size(); ++i) {
            ctx.Fail(__FILE__, __LINE__, "  step %u: kind %d id %#x variant %#x keep %#x", static_cast<unsigned>(i),
                     static_cast<int>(steps[i].kind), steps[i].id, steps[i].variant, steps[i].keep);
        }
        ctx.Fail(__FILE__, __LINE__, "  steps %u, fail chance %u, results %u/%u, log %u/%u",
                 static_cast<unsigned>(steps.size()), failChance, static_cast<unsigned>(ra.results.size()),
                 static_cast<unsigned>(rb.results.size()), static_cast<unsigned>(ra.log.size()),
                 static_cast<unsigned>(rb.log.size()));
    }
    CloseSound(a);
    CloseSound(b);
    g_directSound = savedSound;
    g_soundDuplicateCounter = savedCounter;
    return same;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        if (!CompareOnce(ctx, ours, 1 + static_cast<int>(ctx.rng().Below(40)))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: purges when the last use was more than 300 s ago (the probably
// intended rule instead of the original's `!=`).
void __fastcall WrongPurge(void* self, void* edx, std::uint32_t id, std::uint32_t variant, std::uint32_t keep) {
    CSoundData* const sound = static_cast<CSoundData*>(self);
    if (id == 0xffffffff) {
        // Make every last use look recent: nothing qualifies under "> 300 s".
        for (std::uint32_t node = sound->lastUse.header_ptr != 0
                                      ? *reinterpret_cast<const std::uint32_t*>(sound->lastUse.header_ptr)
                                      : 0;
             node != 0; node = *reinterpret_cast<const std::uint32_t*>(node)) {
            *reinterpret_cast<std::uint32_t*>(node - 4) = 0xffffffff;
        }
    }
    CSoundData_PurgeEffects(self, edx, id, variant, keep);
}

// Wrong: the key keeps the file number.
void* __fastcall WrongGetEffect(void* self, void* edx, std::uint32_t id, std::uint32_t variant) {
    if ((id >> 16) != 0 && static_cast<CSoundData*>(self)->enabled != 0) {
        return nullptr;
    }
    return CSoundData_GetEffect(self, edx, id, variant);
}

}  // namespace

S3RE_TEST(SoundEffects_MatchOriginal) {
    Compare(ctx, {&CSoundData_GetEffect, &CSoundData_PurgeEffects}, 50);
}

S3RE_TEST(SoundEffects_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, {&CSoundData_GetEffect, &WrongPurge}, 40);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2, {&WrongGetEffect, &CSoundData_PurgeEffects}, 40);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
