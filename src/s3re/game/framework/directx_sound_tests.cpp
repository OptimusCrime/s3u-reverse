// Tests of directx_sound.cpp (the DirectSound wrapper classes).
//
// Each case picks one function and runs the original on one arena and ours
// on another (the same random bytes, 16-byte aligned, from the game's
// heap), under two FakeDSound scripts with the same seed: the wrapper
// object at the arena's start points at fake COM objects and a fake game
// vtable, g_directSound at a fake game object. Compared: EAX (arena
// addresses made relative), the arena (unless the object was deleted),
// g_directSound, the call logs and the thrown exception (vtable,
// ThrowInfo, the HRESULT at +0x18).

#include "game/framework/directx_sound.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/fake_dsound.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using test::CaughtException;
using test::FakeDSound;

struct Functions {
    DSErrorTextFn errorText;
    CheckDSResultFn check;
    DSThisFn soundConstruct;
    DSValueFn soundDestroy;
    DSDeletingFn soundDeleting;
    DSConnectFn connect;
    DSValueFn disconnect;
    DSDuplicateFn duplicate;
    DSValueFn release;
    DSValueFn bufferDestroy;
    DSDeletingFn bufferDeleting;
    DSDeletingFn primaryDeleting;
    DSCopyFn copyConstruct;
    DSCopyFn assign;
    DSCheckResultFn checkResult;
    DSSetFn setFlags;
    DSCreateFn create;
    DSValueFn getStatus;
    DSValueFn restoreAll;
    DSValueFn restore;
    DSGetVolumeFn getVolume;
    DSThisFn primaryConstruct;
    DSPrimaryCreateFn primaryCreate;
    DSThisFn secondaryConstruct;
    DSSecondaryCreateFn secondaryCreate;
    DSCopyFn effectBufferConstruct;
};

Functions Originals() {
    return {Original_DirectX_DSErrorText,
            Original_DirectX_CheckDSResult,
            Original_DirectX_CDirectSound_Construct,
            Original_DirectX_CDirectSound_Destroy,
            Original_DirectX_CDirectSound_DeletingDestroy,
            Original_DirectX_CDirectSound_Connect,
            Original_DirectX_CDirectSound_Disconnect,
            Original_DirectX_CDirectSound_DSDuplicateSoundBuffer,
            Original_DirectX_CDirectSoundBuffer_Release,
            Original_DirectX_CDirectSoundBuffer_Destroy,
            Original_DirectX_CDirectSoundBuffer_DeletingDestroy,
            Original_DirectX_CPrimaryBuffer_DeletingDestroy,
            Original_DirectX_CDirectSoundBuffer_CopyConstruct,
            Original_DirectX_CDirectSoundBuffer_Assign,
            Original_DirectX_CDirectSoundBuffer_CheckResult,
            Original_DirectX_CDirectSoundBuffer_SetCreateFlags,
            Original_DirectX_CDirectSoundBuffer_DSCreateSoundBuffer,
            Original_DirectX_CDirectSoundBuffer_DSGetStatus,
            Original_DirectX_CDirectSoundBuffer_RestoreAll,
            Original_DirectX_CDirectSoundBuffer_DSRestore,
            Original_DirectX_CDirectSoundBuffer_DSGetVolume,
            Original_DirectX_CPrimaryBuffer_Construct,
            Original_DirectX_CPrimaryBuffer_Create,
            Original_DirectX_CSecondaryBuffer_Construct,
            Original_DirectX_CSecondaryBuffer_Create,
            Original_DirectX_CSoundEffectBuffer_Construct};
}

Functions Ours() {
    return {&DirectX_DSErrorText,
            &DirectX_CheckDSResult,
            &DirectX_CDirectSound_Construct,
            &DirectX_CDirectSound_Destroy,
            &DirectX_CDirectSound_DeletingDestroy,
            &DirectX_CDirectSound_Connect,
            &DirectX_CDirectSound_Disconnect,
            &DirectX_CDirectSound_DSDuplicateSoundBuffer,
            &DirectX_CDirectSoundBuffer_Release,
            &DirectX_CDirectSoundBuffer_Destroy,
            &DirectX_CDirectSoundBuffer_DeletingDestroy,
            &DirectX_CPrimaryBuffer_DeletingDestroy,
            &DirectX_CDirectSoundBuffer_CopyConstruct,
            &DirectX_CDirectSoundBuffer_Assign,
            &DirectX_CDirectSoundBuffer_CheckResult,
            &DirectX_CDirectSoundBuffer_SetCreateFlags,
            &DirectX_CDirectSoundBuffer_DSCreateSoundBuffer,
            &DirectX_CDirectSoundBuffer_DSGetStatus,
            &DirectX_CDirectSoundBuffer_RestoreAll,
            &DirectX_CDirectSoundBuffer_DSRestore,
            &DirectX_CDirectSoundBuffer_DSGetVolume,
            &DirectX_CPrimaryBuffer_Construct,
            &DirectX_CPrimaryBuffer_Create,
            &DirectX_CSecondaryBuffer_Construct,
            &DirectX_CSecondaryBuffer_Create,
            &DirectX_CSoundEffectBuffer_Construct};
}

enum Op : std::uint32_t {
    kErrorText,
    kCheck,
    kSoundConstruct,
    kSoundDestroy,
    kSoundDeleting,
    kConnect,
    kDisconnect,
    kDuplicate,
    kRelease,
    kBufferDestroy,
    kBufferDeleting,
    kPrimaryDeleting,
    kCopyConstruct,
    kAssign,
    kCheckResult,
    kSetFlags,
    kCreate,
    kGetStatus,
    kRestoreAll,
    kRestore,
    kGetVolume,
    kPrimaryConstruct,
    kPrimaryCreate,
    kSecondaryConstruct,
    kSecondaryCreate,
    kEffectBufferConstruct,
    kOpCount,
};

// Arena layout.
constexpr std::size_t kArenaBytes = 0x200;
constexpr std::size_t kOther = 0x40;   // a second buffer object (copy, assign)
constexpr std::size_t kDesc = 0x80;    // a DSBUFFERDESC
constexpr std::size_t kOut = 0xa0;     // out slots
// A WAVEFORMATEX, shared by both runs (the fake logs its address).
std::uint8_t g_format[32];

struct Call {
    Op op;
    std::uint8_t* arena;
    std::int32_t hr;
    std::uint32_t value;
    const void* pointer;
    bool selfAssign;
};

std::uint32_t Dispatch(const Functions* f, const Call* c) {
    void* const self = c->arena;
    void* const other = c->selfAssign ? self : c->arena + kOther;
    const char* const where = static_cast<const char*>(c->pointer);
    switch (c->op) {
        case kErrorText: return reinterpret_cast<std::uint32_t>(f->errorText(c->hr));
        case kCheck: return f->check(self, nullptr, c->hr, where);
        case kSoundConstruct: return reinterpret_cast<std::uint32_t>(f->soundConstruct(self, nullptr));
        case kSoundDestroy: return f->soundDestroy(self, nullptr);
        case kSoundDeleting: return reinterpret_cast<std::uint32_t>(f->soundDeleting(self, nullptr, c->value));
        case kConnect: return f->connect(self, nullptr, c->pointer);
        case kDisconnect: return f->disconnect(self, nullptr);
        case kDuplicate:
            return f->duplicate(self, nullptr, const_cast<void*>(c->pointer), reinterpret_cast<void**>(c->arena + kOut));
        case kRelease: return f->release(self, nullptr);
        case kBufferDestroy: return f->bufferDestroy(self, nullptr);
        case kBufferDeleting: return reinterpret_cast<std::uint32_t>(f->bufferDeleting(self, nullptr, c->value));
        case kPrimaryDeleting: return reinterpret_cast<std::uint32_t>(f->primaryDeleting(self, nullptr, c->value));
        case kCopyConstruct: return reinterpret_cast<std::uint32_t>(f->copyConstruct(self, nullptr, other));
        case kAssign: return reinterpret_cast<std::uint32_t>(f->assign(self, nullptr, other));
        case kCheckResult: return static_cast<std::uint32_t>(f->checkResult(self, nullptr, c->hr, where));
        case kSetFlags: return f->setFlags(self, nullptr, c->value);
        case kCreate: return reinterpret_cast<std::uint32_t>(f->create(self, nullptr, c->arena + kDesc));
        case kGetStatus: return f->getStatus(self, nullptr);
        case kRestoreAll: return f->restoreAll(self, nullptr);
        case kRestore: return f->restore(self, nullptr);
        case kGetVolume:
            return static_cast<std::uint32_t>(
                f->getVolume(self, nullptr, reinterpret_cast<std::int32_t*>(c->arena + kOut)));
        case kPrimaryConstruct: return reinterpret_cast<std::uint32_t>(f->primaryConstruct(self, nullptr));
        case kPrimaryCreate: return reinterpret_cast<std::uint32_t>(f->primaryCreate(self, nullptr));
        case kSecondaryConstruct: return reinterpret_cast<std::uint32_t>(f->secondaryConstruct(self, nullptr));
        case kSecondaryCreate:
            return reinterpret_cast<std::uint32_t>(f->secondaryCreate(self, nullptr, g_format, c->value));
        case kEffectBufferConstruct:
            return reinterpret_cast<std::uint32_t>(f->effectBufferConstruct(self, nullptr, c->pointer));
        default: return 0;
    }
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const Functions* f, const Call* c) {
    Outcome out;
    __try {
        out.eax = Dispatch(f, c);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

// An arena address as an offset (0x10000 + offset), others as given.
std::uint32_t Relative(std::uint32_t value, const std::uint8_t* arena) {
    const auto base = reinterpret_cast<std::uint32_t>(arena);
    return value >= base && value < base + kArenaBytes ? 0x10000 + (value - base) : value;
}

std::int32_t RandomHr(test::Rng& rng) {
    static const std::uint32_t kCodes[] = {0,          0,          1,          0x88780096, 0x88780078,
                                           0x8878000a, 0x80004005, 0x88780064, 0x80070057, 0x8878001e};
    return rng.Chance(1, 6) ? static_cast<std::int32_t>(rng.Next32())
                            : static_cast<std::int32_t>(kCodes[rng.Below(sizeof(kCodes) / 4)]);
}

void* FakeOrNull(test::Rng& rng) {
    if (rng.Chance(1, 6)) {
        return nullptr;
    }
    return FakeDSound::Buffer(rng.Below(test::kFakeSoundBuffers));
}

bool CompareOnce(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    const Op op = static_cast<Op>(rng.Below(kOpCount));
    FakeDSound::ResetObjects();
    // Two arenas with the same bytes.
    auto* a = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kArenaBytes));
    auto* b = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kArenaBytes));
    std::vector<std::uint8_t> bytes(kArenaBytes);
    rng.Fill(bytes.data(), bytes.size());
    // The objects: vtable = a fake game vtable (their own virtual calls are
    // logged), +4 a fake interface or null, +8 flags.
    auto fill = [&](std::uint8_t* object, void* iface) {
        std::memcpy(object, &FakeDSound::Game(1)->vtable, 4);
        std::memcpy(object + 4, &iface, 4);
    };
    const bool soundObject = op == kSoundDestroy || op == kSoundDeleting || op == kConnect || op == kDisconnect ||
                             op == kDuplicate || op == kCheck;
    // Null interfaces only where the original checks (or doesn't use) them.
    const bool nullAllowed = op == kSoundDestroy || op == kSoundDeleting || op == kBufferDestroy ||
                             op == kBufferDeleting || op == kPrimaryDeleting || op == kConnect ||
                             op == kSoundConstruct || op == kPrimaryConstruct || op == kSecondaryConstruct ||
                             op == kEffectBufferConstruct || op == kCopyConstruct || op == kCheck ||
                             op == kErrorText || op == kSetFlags || op == kRestoreAll;
    void* const iface = soundObject ? FakeDSound::Sound() : FakeDSound::Buffer(rng.Below(test::kFakeSoundBuffers));
    fill(bytes.data(), nullAllowed && rng.Chance(1, 5) ? nullptr : iface);
    fill(bytes.data() + kOther, FakeOrNull(rng));
    // The buffer description and the format.
    const std::uint32_t desc[5] = {0x14, rng.Chance(1, 2) ? 0u : rng.Next32(), rng.Below(0x10000), 0,
                                   rng.Chance(1, 2) ? 0u : reinterpret_cast<std::uint32_t>(g_format)};
    std::memcpy(bytes.data() + kDesc, desc, sizeof(desc));
    rng.Fill(g_format, sizeof(g_format));
    std::memcpy(a, bytes.data(), kArenaBytes);
    std::memcpy(b, bytes.data(), kArenaBytes);

    static const char* const kWhere[] = {"CDirectSound::Test", "x"};
    Call ca{op, a, RandomHr(rng), rng.Chance(1, 2) ? rng.Below(2) : rng.Next32(), nullptr, rng.Chance(1, 8)};
    switch (op) {
        case kConnect: ca.pointer = rng.Chance(1, 2) ? nullptr : kWhere[1]; break;
        case kDuplicate: ca.pointer = FakeOrNull(rng); break;
        case kEffectBufferConstruct: ca.pointer = reinterpret_cast<void*>(rng.Next32()); break;
        default: ca.pointer = kWhere[rng.Below(2)]; break;
    }
    Call cb = ca;
    cb.arena = b;

    const std::uint64_t seed = rng.Next64();
    test::FakeDSoundConfig config;
    config.failChance = rng.Below(5);
    FakeDSound fa(seed, config);
    FakeDSound fb(seed, config);
    void* const savedSound = g_directSound.get();
    const Functions originals = Originals();

    FakeDSound::ResetObjects();
    g_directSound = FakeDSound::Game(0);
    fa.Install();
    const Outcome oa = Run(&originals, &ca);
    fa.Uninstall();
    const std::uint32_t soundA = Relative(reinterpret_cast<std::uint32_t>(g_directSound.get()), a);

    FakeDSound::ResetObjects();
    g_directSound = FakeDSound::Game(0);
    fb.Install();
    const Outcome ob = Run(&ours, &cb);
    fb.Uninstall();
    const std::uint32_t soundB = Relative(reinterpret_cast<std::uint32_t>(g_directSound.get()), b);
    g_directSound = savedSound;

    bool same = S3RE_EXPECT(oa.caught == ob.caught);
    if (!oa.caught.threw) {
        same &= S3RE_EXPECT_EQ(Relative(oa.eax, a), Relative(ob.eax, b));
    }
    same &= S3RE_EXPECT_EQ(soundA, soundB);
    same &= S3RE_EXPECT(fa.log_ == fb.log_);
    const bool deleted =
        (op == kSoundDeleting || op == kBufferDeleting || op == kPrimaryDeleting) && (ca.value & 1) != 0;
    if (!deleted) {
        same &= S3RE_EXPECT_MEM_EQ(a, b, kArenaBytes);
        msvc6::GameOperatorDelete(a);
        msvc6::GameOperatorDelete(b);
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  op %u, hr %#x, value %#x, fail chance %u", static_cast<unsigned>(op),
                 static_cast<unsigned>(ca.hr), ca.value, config.failChance);
        for (std::size_t i = 0; i < fa.log_.size() || i < fb.log_.size(); ++i) {
            ctx.Fail(__FILE__, __LINE__, "  log[%u]: %#x %#x", static_cast<unsigned>(i),
                     i < fa.log_.size() ? fa.log_[i] : 0xdead, i < fb.log_.size() ? fb.log_[i] : 0xdead);
        }
    }
    return same;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        if (!CompareOnce(ctx, ours)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: a lost buffer is not restored.
std::int32_t __fastcall WrongCheckResult(void* self, void* edx, std::int32_t hr, const char* where) {
    if (hr == static_cast<std::int32_t>(0x88780096)) {
        return hr;
    }
    return DirectX_CDirectSoundBuffer_CheckResult(self, edx, hr, where);
}

// Wrong: the status bit tested is DSBSTATUS_LOOPING (4).
std::uint32_t __fastcall WrongGetStatus(void* self, void* edx) {
    DirectX_CDirectSoundBuffer_DSGetStatus(self, edx);
    return 0;
}

}  // namespace

S3RE_TEST(DirectXSound_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(DirectXSound_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.checkResult = &WrongCheckResult;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ours();
    wrong.getStatus = &WrongGetStatus;
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2, wrong, 3000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
