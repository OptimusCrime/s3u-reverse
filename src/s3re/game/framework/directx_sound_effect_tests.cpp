// Tests of directx_sound_effect.cpp (DirectX::CSoundEffect, the restore
// entries, DuplicateInto, the duplicates' dictionary code).
//
// A case makes an effect, loads a synthetic sample through a
// CMemorySoundFile (modes 1, 2, 4; 8- and 16-bit formats) and runs a random
// sequence on it: Play, PurgeStoppedDuplicates, Restore, the master's
// CSoundEffectBuffer::Restore and CDirectSound slot 1 (application active
// or not), Unload + Load of a new file, DuplicateInto, operator[], the
// iterator's value, a node made and destroyed directly; finally the
// destructor. The sequence runs twice under scripted DirectSound
// (FakeDSound, same seed): once entirely as original code (every hooked
// function of the sound classes jumps to its trampoline, so the originals
// call each other) and once as ours. The fake clock, the duplicate counter
// (0x7a0c00) and the skip lists' random level bits start alike. Compared
// per step: EAX (keys, the purge result; pointers as dumps), a dump of the
// effect (vtables, buffers by fake interface, flags and effect, the file's
// fields, format, sizes, last purge, the dictionary's count, level,
// key/value pair and every node's key and buffer) and of the other objects
// touched; at the end the DirectSound log (sample hashes included), the
// counter and the exception that ended the sequence.

#include "game/framework/directx_sound.h"
#include "game/framework/directx_sound_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_dsound.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::game {

using EffectThisFn = void*(__fastcall*)(void* self, void* edx);
using EffectValueFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using EffectLoadFn = void(__fastcall*)(void* self, void* edx, void* file, std::uint32_t unused);
using EffectPlayFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t volume, std::int32_t pan,
                                               std::uint32_t flags);
using EffectDuplicateFn = void*(__fastcall*)(void* self, void* edx, void* dest);
using NewNodeFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* pair, std::uint32_t level);
using DestroyNodeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t node, std::uint32_t level);
using DictIndexFn = void*(__fastcall*)(void* self, void* edx, const std::int32_t* key);
using IterValueFn = void*(__fastcall*)(void* self, void* edx, void* result);
using MemorySoundFileConstructFn = void*(__fastcall*)(void* self, void* edx, const void* format, const void* data,
                                                      std::uint32_t bytes, std::uint32_t mode);

void* __fastcall DirectX_CSoundEffect_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffect_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffect_Unload(void* self, void* edx);
void __fastcall DirectX_CSoundEffect_Load(void* self, void* edx, void* file, std::uint32_t unused);
std::uint32_t __fastcall DirectX_CSoundEffect_Restore(void* self, void* edx);
std::int32_t __fastcall DirectX_CSoundEffect_Play(void* self, void* edx, std::int32_t volume, std::int32_t pan,
                                                  std::uint32_t flags);
std::uint32_t __fastcall DirectX_CSoundEffect_PurgeStoppedDuplicates(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffectBuffer_Restore(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSound_RestoreIfActive(void* self, void* edx);
void* __fastcall DirectX_CSoundEffectBuffer_DuplicateInto(void* self, void* edx, void* dest);
std::uint32_t __fastcall EffectBufferDict_NewNode(void* self, void* edx, const void* pair, std::uint32_t level);
std::uint32_t __fastcall EffectBufferDict_DestroyNode(void* self, void* edx, std::uint32_t node,
                                                      std::uint32_t level);
void* __fastcall EffectBufferDict_Index(void* self, void* edx, const std::int32_t* key);
void* __fastcall EffectBufferDictIter_Value(void* self, void* edx, void* result);
void* __fastcall DirectX_CMemorySoundFile_Construct(void* self, void* edx, const void* format, const void* data,
                                                    std::uint32_t bytes, std::uint32_t mode);

namespace {

using framework::CSoundEffect;
using framework::CSoundEffectBuffer;
using framework::EffectBufferDict;
using framework::SkipListIter;
using test::CaughtException;
using test::FakeDSound;

constexpr std::uintptr_t kBufferDestroy = 0x005c1fe0;  // ~CDirectSoundBuffer

struct Functions {
    EffectThisFn construct;
    EffectValueFn destroy;
    EffectValueFn unload;
    EffectLoadFn load;
    EffectValueFn restore;
    EffectPlayFn play;
    EffectValueFn purge;
    EffectValueFn bufferRestore;
    EffectValueFn soundRestore;
    EffectDuplicateFn duplicateInto;
    NewNodeFn newNode;
    DestroyNodeFn destroyNode;
    DictIndexFn index;
    IterValueFn iterValue;
    MemorySoundFileConstructFn fileConstruct;
};

Functions Ours() {
    return {&DirectX_CSoundEffect_Construct,
            &DirectX_CSoundEffect_Destroy,
            &DirectX_CSoundEffect_Unload,
            &DirectX_CSoundEffect_Load,
            &DirectX_CSoundEffect_Restore,
            &DirectX_CSoundEffect_Play,
            &DirectX_CSoundEffect_PurgeStoppedDuplicates,
            &DirectX_CSoundEffectBuffer_Restore,
            &DirectX_CDirectSound_RestoreIfActive,
            &DirectX_CSoundEffectBuffer_DuplicateInto,
            &EffectBufferDict_NewNode,
            &EffectBufferDict_DestroyNode,
            &EffectBufferDict_Index,
            &EffectBufferDictIter_Value,
            &DirectX_CMemorySoundFile_Construct};
}

// ---------------------------------------------------------------------------
// The all-original run: every hooked function on these paths jumps to its
// trampoline, so the originals call each other.
// ---------------------------------------------------------------------------

const char* const kOriginalChain[] = {
    "DirectX_CSoundEffect_Construct",
    "DirectX_CSoundEffect_Destroy",
    "DirectX_CSoundEffect_Unload",
    "DirectX_CSoundEffect_Load",
    "DirectX_CSoundEffect_Restore",
    "DirectX_CSoundEffect_Play",
    "DirectX_CSoundEffect_PurgeStoppedDuplicates",
    "DirectX_CSoundEffectBuffer_Restore",
    "DirectX_CDirectSound_RestoreIfActive",
    "DirectX_CSoundEffectBuffer_DuplicateInto",
    "EffectBufferDict_NewNode",
    "EffectBufferDict_DestroyNode",
    "EffectBufferDict_Index",
    "EffectBufferDictIter_Value",
    "DirectX_CMemorySoundFile_Construct",
    "SkipListDestructor_5c33e0",
    "SkipListIterConstructor_5c34f0",
    "SkipDictKeyEqual_5c33a0",
    "SkipDictKeyLess_5c33c0",
    "DirectX_CSoundFile_Construct",
    "DirectX_CSoundFile_Destroy",
    "DirectX_CMemorySoundFile_Rewind",
    "DirectX_CMemorySoundFile_GetFormat",
    "DirectX_CMemorySoundFile_Read",
    "ScalarDeletingDestructor_5c2780",
    "DirectX_CAppNotActiveException_Construct",
    "DirectX_CDirectSoundException_Construct",
    "DirectX_CDirectSoundException_Describe",
};
constexpr std::size_t kChainLength = sizeof(kOriginalChain) / sizeof(kOriginalChain[0]);

// The functions the steps call directly, as the chain's trampolines.
Functions Originals() {
    const void* trampolines[kChainLength] = {};
    for (std::size_t i = 0; i < kChainLength; ++i) {
        std::uintptr_t address = 0;
        hooks::FindHook(kOriginalChain[i], &address, &trampolines[i]);
    }
    Functions f{};
    std::memcpy(&f, trampolines, sizeof(f));  // the first entries are in Functions' order
    static_assert(sizeof(Functions) == 15 * sizeof(void*));
    return f;
}

class AllOriginal {
public:
    AllOriginal() {
        for (const char* name : kOriginalChain) {
            std::uintptr_t address = 0;
            const void* original = nullptr;
            if (!hooks::FindHook(name, &address, &original) || original == nullptr ||
                reinterpret_cast<std::uintptr_t>(original) == address) {
                missing_ = true;
                continue;
            }
            jumps_.push_back(std::make_unique<test::ScopedJump>(address, original));
        }
    }
    bool missing() const { return missing_; }

private:
    std::vector<std::unique_ptr<test::ScopedJump>> jumps_;
    bool missing_ = false;
};

// ---------------------------------------------------------------------------
// The fake clock and application
// ---------------------------------------------------------------------------

struct Clock {
    std::uint32_t now = 0;
    test::Rng rng{0};
};
Clock* g_clock = nullptr;

std::uint32_t __stdcall FakeTimeGetTime() {
    static const std::uint32_t kSteps[] = {0, 1, 2499, 2500, 1000, 5000, 0xfffff000u};
    g_clock->now += kSteps[g_clock->rng.Below(7)];
    return g_clock->now;
}

std::uint8_t g_fakeApplication[0x40];
constexpr std::size_t kActiveFlag = 0x1c;  // DirectX::CApplication::IsActive's byte

// ---------------------------------------------------------------------------
// The sequence
// ---------------------------------------------------------------------------

enum class Kind {
    Play,
    Purge,
    Restore,
    BufferRestore,
    SoundRestore,
    Reload,
    DuplicateInto,
    Index,
    IterValue,
    Node,
};

struct Step {
    Kind kind;
    std::int32_t volume;
    std::int32_t pan;
    std::uint32_t flags;
    std::uint8_t active;
    std::uint32_t pick;  // which key / node
    std::uint32_t level;
    std::uint32_t mode;  // Reload: the file's mode
};

struct Sample {
    framework::WaveFormat format;
    std::uint32_t bytes;
    std::uint32_t mode;
};

struct Case {
    std::uint64_t seed;
    std::uint32_t failChance;
    std::uint32_t startTime;
    std::uint32_t counter;
    std::uint8_t fill;
    std::uint8_t active;  // the application's active flag while loading
    Sample sample;
    std::vector<Step> steps;
};

std::vector<std::uint8_t> g_sampleData;

// What the cases reached (checked at the end, so that the comparison
// doesn't pass on sequences that never get far).
struct Coverage {
    std::uint32_t steps = 0;
    std::uint32_t thrown = 0;
    std::uint32_t duplicatePlays = 0;  // Play on a duplicate (key != 0)
    std::uint32_t idlePurges = 0;      // PurgeStoppedDuplicates true
    std::uint32_t destroyed = 0;
};
Coverage g_coverage;

struct Run {
    std::vector<std::uint32_t> dumps;
    std::vector<std::uint32_t> log;
    CaughtException caught;
    std::uint32_t counter = 0;
    std::uint32_t stepsDone = 0;
};

void DumpBuffer(std::vector<std::uint32_t>& out, const CSoundEffectBuffer* buffer, const void* effect) {
    out.push_back(buffer->base.vtable);
    out.push_back(test::DSoundLogPointer(buffer->base.buffer));
    out.push_back(buffer->base.createFlags);
    out.push_back(buffer->effect == effect ? 1u : buffer->effect == nullptr ? 0u : 2u);
}

void DumpFile(std::vector<std::uint32_t>& out, const void* object) {
    const auto* file = static_cast<const framework::CMemorySoundFile*>(object);
    out.push_back(file->base.vtable);
    out.push_back(file->base.name.len);
    std::uint32_t words[5] = {};
    std::memcpy(words, &file->format, sizeof(file->format));
    out.insert(out.end(), words, words + 5);
    out.push_back(file->data == g_sampleData.data() ? 1u : 0u);
    out.push_back(file->bytes);
    out.push_back(file->position);
    out.push_back(file->mode);
}

std::uint32_t DictWord(const EffectBufferDict* dict, std::uint32_t offset) {
    return *reinterpret_cast<const std::uint32_t*>(dict->base + offset);
}

constexpr std::uint32_t kTopLevel = 0x6c;
constexpr std::uint32_t kHeaderPointer = 0x70;

void DumpEffect(std::vector<std::uint32_t>& out, const CSoundEffect* effect) {
    out.push_back(0xeffec700u);
    out.push_back(effect->vtable);
    DumpBuffer(out, &effect->master, effect);
    out.push_back(effect->file != nullptr ? 1u : 0u);
    if (effect->file != nullptr) {
        DumpFile(out, effect->file);
    }
    std::uint32_t words[5] = {};
    std::memcpy(words, &effect->format, sizeof(effect->format));
    out.insert(out.end(), words, words + 5);
    out.push_back(effect->bytes);
    out.push_back(effect->lastPurge);
    const EffectBufferDict* dict = &effect->duplicates;
    out.push_back(DictWord(dict, 0));
    out.push_back(DictWord(dict, framework::kDictCount));
    out.push_back(DictWord(dict, kTopLevel));
    out.push_back(static_cast<std::uint32_t>(dict->keySlot));
    DumpBuffer(out, &dict->valueSlot, effect);
    for (std::uint32_t node = *reinterpret_cast<const std::uint32_t*>(DictWord(dict, kHeaderPointer)); node != 0;
         node = *reinterpret_cast<const std::uint32_t*>(node)) {
        out.push_back(*reinterpret_cast<const std::uint32_t*>(node - 0x14));
        DumpBuffer(out, reinterpret_cast<const CSoundEffectBuffer*>(node - 0x10), effect);
    }
}

// The n-th node of the dictionary (0 if fewer).
std::uint32_t NthNode(const CSoundEffect* effect, std::uint32_t n) {
    std::uint32_t node = *reinterpret_cast<const std::uint32_t*>(DictWord(&effect->duplicates, kHeaderPointer));
    for (; node != 0 && n > 0; --n) {
        node = *reinterpret_cast<const std::uint32_t*>(node);
    }
    return node;
}

struct Guarded {
    const Functions* f;
    CSoundEffect* effect;
    const Step* step;
    const Case* c;
    std::vector<std::uint32_t>* dumps;
};

void* NewFile(const Functions* f, const Case* c, std::uint32_t mode) {
    void* file = msvc6::GameOperatorNew(sizeof(framework::CMemorySoundFile));
    std::memset(file, c->fill, sizeof(framework::CMemorySoundFile));
    f->fileConstruct(file, nullptr, &c->sample.format, g_sampleData.data(), c->sample.bytes, mode);
    return file;
}

std::uint32_t RunStep(const Guarded* g) {
    const Functions* f = g->f;
    CSoundEffect* const effect = g->effect;
    const Step& step = *g->step;
    std::vector<std::uint32_t>& out = *g->dumps;
    g_fakeApplication[kActiveFlag] = step.active;
    switch (step.kind) {
        case Kind::Play: return static_cast<std::uint32_t>(f->play(effect, nullptr, step.volume, step.pan, step.flags));
        case Kind::Purge: return f->purge(effect, nullptr) & 0xFF;
        case Kind::Restore: f->restore(effect, nullptr); return 0;
        case Kind::BufferRestore: f->bufferRestore(&effect->master, nullptr); return 0;
        case Kind::SoundRestore: f->soundRestore(FakeDSound::Game(1), nullptr); return 0;
        case Kind::Reload:
            f->unload(effect, nullptr);
            f->load(effect, nullptr, NewFile(f, g->c, step.mode), 1);
            return 0;
        case Kind::DuplicateInto: {
            CSoundEffectBuffer dest{};
            dest.base.vtable = vtables::kCSoundEffectBuffer;
            dest.base.buffer = FakeDSound::Buffer(step.pick % test::kFakeSoundBuffers);
            dest.base.createFlags = step.flags;
            dest.effect = reinterpret_cast<void*>(step.pick);
            const void* const r = f->duplicateInto(&effect->master, nullptr, &dest);
            DumpBuffer(out, &dest, effect);
            return r == effect ? 1 : 0;
        }
        case Kind::Index: {
            // Existing keys only: a new key would insert a duplicate without
            // interface, which the next Play or purge calls in both versions
            // (a crash; the emulator spec covers the insert).
            const std::uint32_t node = NthNode(effect, step.pick % 4);
            if (node == 0) {
                return 2;
            }
            const std::int32_t key = *reinterpret_cast<const std::int32_t*>(node - 0x14);
            auto* const value = static_cast<CSoundEffectBuffer*>(f->index(&effect->duplicates, nullptr, &key));
            if (value != nullptr) {
                DumpBuffer(out, value, effect);
            }
            return value != nullptr ? 1 : 0;
        }
        case Kind::IterValue: {
            SkipListIter iter{vtables::kEffectBufferDictIter, 0, &effect->duplicates, NthNode(effect, step.pick % 4),
                              0};
            if (step.pick % 5 == 0) {
                iter.node = 0;
            }
            CSoundEffectBuffer copy{};
            const void* const r = f->iterValue(&iter, nullptr, &copy);
            DumpBuffer(out, &copy, effect);
            using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
            reinterpret_cast<ThisFn>(kBufferDestroy)(&copy, nullptr);  // drops the copy's reference
            return r == &copy ? 1 : 0;
        }
        case Kind::Node: {
            struct {
                std::int32_t key;
                CSoundEffectBuffer value;
            } pair{static_cast<std::int32_t>(step.pick), effect->master};
            const std::uint32_t node = f->newNode(&effect->duplicates, nullptr, &pair, step.level);
            if (node != 0) {
                out.push_back(*reinterpret_cast<const std::uint32_t*>(node - 0x14));
                DumpBuffer(out, reinterpret_cast<const CSoundEffectBuffer*>(node - 0x10), effect);
            }
            f->destroyNode(&effect->duplicates, nullptr, node, step.level);
            return node != 0 ? 1 : 0;
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

struct GuardedLoad {
    const Functions* f;
    CSoundEffect* effect;
    const Case* c;
};

bool GuardedConstructLoad(const GuardedLoad* g, CaughtException* caught) {
    __try {
        g->f->construct(g->effect, nullptr);
        g->f->load(g->effect, nullptr, NewFile(g->f, g->c, g->c->sample.mode), 1);
        return true;
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
    return false;
}

bool GuardedDestroy(const Functions* f, CSoundEffect* effect, CaughtException* caught) {
    __try {
        f->destroy(effect, nullptr);
        return true;
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
    return false;
}

Run RunCase(const Functions& f, const Case& c, bool original) {
    Run run;
    Clock clock;
    clock.now = c.startTime;
    clock.rng = test::Rng(c.seed);
    g_clock = &clock;
    const auto fakeTime = reinterpret_cast<std::uintptr_t>(&FakeTimeGetTime);
    test::PatchBytes(g_importTimeGetTime.address(), &fakeTime, 4);
    g_soundDuplicateCounter = c.counter;
    g_skipListRandomBits = 0x5a5a5a5a;
    g_skipListRandomBitsLeft = 0;
    test::FakeDSoundConfig config;
    config.failChance = c.failChance;
    config.lockAlwaysWrites = true;
    FakeDSound fake(c.seed ^ 0x5eed, config);
    FakeDSound::ResetObjects();
    void* const savedSound = g_directSound.get();
    std::uint8_t* const savedApp = g_app.get();
    g_directSound = FakeDSound::Game(0);
    g_app = g_fakeApplication;
    g_fakeApplication[kActiveFlag] = c.active;
    fake.Install();
    {
        std::unique_ptr<AllOriginal> jumps = original ? std::make_unique<AllOriginal>() : nullptr;
        auto* effect = static_cast<CSoundEffect*>(msvc6::GameOperatorNew(sizeof(CSoundEffect)));
        std::memset(effect, c.fill, sizeof(CSoundEffect));
        GuardedLoad load{&f, effect, &c};
        bool alive = GuardedConstructLoad(&load, &run.caught);
        if (alive) {
            DumpEffect(run.dumps, effect);
        }
        for (const Step& step : c.steps) {
            if (!alive) {
                break;
            }
            Guarded g{&f, effect, &step, &c, &run.dumps};
            std::uint32_t eax = 0;
            alive = GuardedStep(&g, &eax, &run.caught);
            if (alive) {
                run.dumps.push_back(eax);
                DumpEffect(run.dumps, effect);
                ++run.stepsDone;
                if (!original) {
                    ++g_coverage.steps;
                    g_coverage.duplicatePlays += step.kind == Kind::Play && eax != 0 ? 1 : 0;
                    g_coverage.idlePurges += step.kind == Kind::Purge && eax != 0 ? 1 : 0;
                }
            }
        }
        // The destructor, when the effect can take it (a master without
        // interface, left by a failed reload, would crash both in Release).
        if (alive && effect->master.base.buffer != nullptr) {
            if (GuardedDestroy(&f, effect, &run.caught)) {
                run.dumps.push_back(0xde57u);
                msvc6::GameOperatorDelete(effect);
                g_coverage.destroyed += original ? 0 : 1;
            }
        }
    }
    fake.Uninstall();
    g_coverage.thrown += !original && run.caught.threw ? 1 : 0;
    g_directSound = savedSound;
    g_app = savedApp;
    test::UnpatchBytes(g_importTimeGetTime.address());
    g_clock = nullptr;
    run.log = fake.log_;
    run.counter = g_soundDuplicateCounter;
    return run;
}

Case MakeCase(test::Rng& rng) {
    Case c;
    c.seed = rng.Next64();
    c.failChance = rng.Chance(1, 3) ? 0 : rng.Below(3);
    c.startTime = rng.Chance(1, 2) ? 0 : rng.Next32();
    c.counter = rng.Chance(1, 2) ? rng.Below(100) : rng.Next32();
    c.fill = static_cast<std::uint8_t>(rng.Next32());
    c.active = rng.Chance(3, 4) ? 1 : 0;
    framework::WaveFormat& format = c.sample.format;
    format.formatTag = 1;
    format.channels = 1;
    static const std::uint32_t kRates[] = {22050, 44100, 11025, 22049};
    format.samplesPerSec = kRates[rng.Below(4)];
    format.bitsPerSample = rng.Chance(3, 4) ? 16 : 8;
    format.blockAlign = static_cast<std::uint16_t>(format.bitsPerSample / 8);
    format.avgBytesPerSec = format.samplesPerSec * format.blockAlign;
    format.extraSize = 0;
    c.sample.bytes = 4 * (1 + rng.Below(1024));
    static const std::uint32_t kModes[] = {1, 2, 4};
    c.sample.mode = kModes[rng.Below(3)];
    const std::uint32_t count = 1 + rng.Below(24);
    for (std::uint32_t i = 0; i < count; ++i) {
        Step step{};
        const std::uint32_t r = rng.Below(100);
        step.kind = r < 40   ? Kind::Play
                    : r < 55 ? Kind::Purge
                    : r < 61 ? Kind::Restore
                    : r < 67 ? Kind::BufferRestore
                    : r < 71 ? Kind::SoundRestore
                    : r < 75 ? Kind::Reload
                    : r < 81 ? Kind::DuplicateInto
                    : r < 88 ? Kind::Index
                    : r < 94 ? Kind::IterValue
                             : Kind::Node;
        step.volume = -static_cast<std::int32_t>(rng.Below(10001));
        step.pan = rng.Range(-10000, 10000);
        step.flags = rng.Below(2);
        static const std::uint8_t kActive[] = {1, 1, 1, 0, 2};
        step.active = kActive[rng.Below(5)];
        step.pick = rng.Chance(1, 2) ? rng.Below(16) : rng.Next32();
        step.level = rng.Below(8);
        step.mode = kModes[rng.Below(3)];
        c.steps.push_back(step);
    }
    return c;
}

bool FirstDifference(test::Context& ctx, const char* what, const std::vector<std::uint32_t>& a,
                     const std::vector<std::uint32_t>& b) {
    for (std::size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const std::uint32_t va = i < a.size() ? a[i] : 0xdead;
        const std::uint32_t vb = i < b.size() ? b[i] : 0xdead;
        if (va != vb) {
            ctx.Fail(__FILE__, __LINE__, "  first %s difference at %u: %#x %#x", what, static_cast<unsigned>(i), va,
                     vb);
            return true;
        }
    }
    return false;
}

bool CompareCase(test::Context& ctx, const Functions& originals, const Functions& ours, int index) {
    const Case c = MakeCase(ctx.rng());
    g_sampleData.resize(c.sample.bytes + 8);
    ctx.rng().Fill(g_sampleData.data(), g_sampleData.size());
    const Run a = RunCase(originals, c, true);
    const Run b = RunCase(ours, c, false);
    bool same = S3RE_EXPECT_EQ(a.stepsDone, b.stepsDone);
    same &= S3RE_EXPECT(a.caught == b.caught);
    same &= S3RE_EXPECT_EQ(a.counter, b.counter);
    same &= S3RE_EXPECT(a.dumps == b.dumps);
    same &= S3RE_EXPECT(a.log == b.log);
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  case %d: %u steps (%u done), fail chance %u, mode %u", index,
                 static_cast<unsigned>(c.steps.size()), a.stepsDone, c.failChance, c.sample.mode);
        FirstDifference(ctx, "dump", a.dumps, b.dumps);
        FirstDifference(ctx, "log", a.log, b.log);
    }
    return same;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    {
        AllOriginal probe;
        if (probe.missing()) {
            ctx.Fail(__FILE__, __LINE__, "  a hook of the original chain is missing");
            return;
        }
    }
    const Functions originals = Originals();
    for (int i = 0; i < cases; ++i) {
        if (!CompareCase(ctx, originals, ours, i)) {
            return;
        }
    }
}

// Wrong: the duplicate counter counts twice per new duplicate.
std::int32_t __fastcall WrongPlay(void* self, void* edx, std::int32_t volume, std::int32_t pan, std::uint32_t flags) {
    const std::uint32_t before = g_soundDuplicateCounter;
    const std::int32_t key = DirectX_CSoundEffect_Play(self, edx, volume, pan, flags);
    if (g_soundDuplicateCounter != before) {
        g_soundDuplicateCounter = g_soundDuplicateCounter + 1;
    }
    return key;
}

// Wrong: the purge interval is 2,499 ms.
std::uint32_t __fastcall WrongPurge(void* self, void* edx) {
    static_cast<CSoundEffect*>(self)->lastPurge -= 1;
    return DirectX_CSoundEffect_PurgeStoppedDuplicates(self, edx);
}

}  // namespace

S3RE_TEST(SoundEffect_MatchOriginal) {
    g_coverage = Coverage{};
    Compare(ctx, Ours(), 600);
    // Seed 0 reaches 3342 steps, 387 exceptions, 669 plays on duplicates,
    // 207 idle purges, 213 destructions.
    S3RE_EXPECT(g_coverage.steps > 1000);
    S3RE_EXPECT(g_coverage.thrown > 50);
    S3RE_EXPECT(g_coverage.duplicatePlays > 100);
    S3RE_EXPECT(g_coverage.idlePurges > 30);
    S3RE_EXPECT(g_coverage.destroyed > 50);
}

S3RE_TEST(SoundEffect_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.play = &WrongPlay;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 200);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ours();
    wrong.purge = &WrongPurge;
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2, wrong, 200);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
