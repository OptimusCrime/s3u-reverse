// The old (pre-standard) MSVC 6.0 iostream's ios locking, as compiled into
// the game (MSVCIRT's <ios.h> inline functions; out-of-line copies in
// Dirk\ConsoleInit.cpp's code, called by the console setup 0x410da0 and
// Win32::CStructuredException::Describe 0x614870 around their flag changes).
//
// The ios fields used (MSVCIRT's class ios): +0x24 x_flags, +0x34 LockFlg
// (negative: the stream locks), +0x38 x_lock (a CRITICAL_SECTION).
//
// ios::lock (0x00411490): cdecl (ios*): LockFlg < 0: _mtlock(&x_lock)
//   (0x61d1b5, the CRT's EnterCriticalSection wrapper). EAX: the ios
//   pointer, or what _mtlock left (EnterCriticalSection's EAX); the callers
//   don't read it.
// ios::unlock (0x004114b0): the same with _mtunlock (0x61d1c0,
//   LeaveCriticalSection).
// ios::setf(long f, long mask) (0x004114d0): thiscall ret 8: lock, x_flags
//   = (x_flags & ~mask) | (f & mask), unlock (LockFlg read again for it);
//   EAX = the old flags.

#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

#pragma pack(push, 1)
struct Ios {
    std::uint8_t unk00[0x24];
    std::uint32_t x_flags;      // 0x24
    std::uint8_t unk28[0x0c];
    std::int32_t lock_flag;     // 0x34 LockFlg: < 0 when the stream locks
    std::uint8_t x_lock[0x18];  // 0x38 CRITICAL_SECTION
};
#pragma pack(pop)
static_assert(offsetof(Ios, x_flags) == 0x24);
static_assert(offsetof(Ios, lock_flag) == 0x34);
static_assert(offsetof(Ios, x_lock) == 0x38);

constexpr std::uintptr_t kMtLock = 0x0061d1b5;    // _mtlock (CRT)
constexpr std::uintptr_t kMtUnlock = 0x0061d1c0;  // _mtunlock (CRT)

using MtLockFn = std::uint32_t(__cdecl*)(void* criticalSection);

}  // namespace

using IosLockFn = std::uint32_t(__cdecl*)(Ios* stream);
using IosSetfFn = std::uint32_t(__fastcall*)(Ios* self, void* edx, std::uint32_t flags, std::uint32_t mask);

// Purpose: Old iostream: locks a stream's critical section (if the stream
// locks at all) before its flags are changed.
std::uint32_t __cdecl Ios_Lock(Ios* stream) {
    if (stream->lock_flag < 0) {
        return reinterpret_cast<MtLockFn>(kMtLock)(stream->x_lock);
    }
    return reinterpret_cast<std::uint32_t>(stream);
}

// Purpose: Old iostream: unlocks a stream again.
std::uint32_t __cdecl Ios_Unlock(Ios* stream) {
    if (stream->lock_flag < 0) {
        return reinterpret_cast<MtLockFn>(kMtUnlock)(stream->x_lock);
    }
    return reinterpret_cast<std::uint32_t>(stream);
}

// Purpose: Old iostream: sets the format flags selected by a mask (e.g. hex
// output for the crash report), returning the previous ones.
std::uint32_t __fastcall Ios_Setf(Ios* self, void*, std::uint32_t flags, std::uint32_t mask) {
    if (self->lock_flag < 0) {
        reinterpret_cast<MtLockFn>(kMtLock)(self->x_lock);
    }
    const std::uint32_t old = self->x_flags;
    self->x_flags = (old & ~mask) | (flags & mask);
    if (self->lock_flag < 0) {
        reinterpret_cast<MtLockFn>(kMtUnlock)(self->x_lock);
    }
    return old;
}

S3RE_HOOK(0x00411490, Ios_Lock, IosLockFn, Original_Ios_Lock, Ios_Lock);
S3RE_HOOK(0x004114b0, Ios_Unlock, IosLockFn, Original_Ios_Unlock, Ios_Unlock);
S3RE_HOOK(0x004114d0, Ios_Setf, IosSetfFn, Original_Ios_Setf, Ios_Setf);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// _mtlock / _mtunlock replaced by stubs that record (which, argument
// relative to the stream) and return a value fixed by the call count.
std::vector<std::uint32_t>* g_lockLog = nullptr;
std::uintptr_t g_stream = 0;

template <std::uint32_t Which>
std::uint32_t __cdecl LockStub(void* criticalSection) {
    g_lockLog->push_back(Which);
    g_lockLog->push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(criticalSection) - g_stream));
    return 0x51a70000u + Which * 0x100 + static_cast<std::uint32_t>(g_lockLog->size());
}

struct IosFunctions {
    IosLockFn lock;
    IosLockFn unlock;
    IosSetfFn setf;
};

struct IosRun {
    std::uint32_t eax;
    std::vector<std::uint32_t> log;
};

bool CompareIos(test::Context& ctx, const IosFunctions& ours, int cases) {
    const IosFunctions original{Original_Ios_Lock, Original_Ios_Unlock, Original_Ios_Setf};
    test::PatchJump(kMtLock, reinterpret_cast<const void*>(&LockStub<1>));
    test::PatchJump(kMtUnlock, reinterpret_cast<const void*>(&LockStub<2>));
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        alignas(4) std::uint8_t a[sizeof(Ios)], b[sizeof(Ios)];
        ctx.rng().Fill(a, sizeof(a));
        auto* ia = reinterpret_cast<Ios*>(a);
        if (ctx.rng().Chance(1, 2)) {
            ia->lock_flag = ctx.rng().Chance(1, 2) ? -1 : ctx.rng().Range(-5, 5);
        }
        std::memcpy(b, a, sizeof(a));
        const std::uint32_t flags = ctx.rng().Next32(), mask = ctx.rng().Next32();
        const int op = static_cast<int>(ctx.rng().Below(3));
        auto run = [&](const IosFunctions& f, std::uint8_t* buffer) {
            IosRun r{};
            g_lockLog = &r.log;
            g_stream = reinterpret_cast<std::uintptr_t>(buffer);
            Ios* stream = reinterpret_cast<Ios*>(buffer);
            r.eax = op == 0 ? f.lock(stream) : op == 1 ? f.unlock(stream) : f.setf(stream, nullptr, flags, mask);
            // A returned pointer to the stream, relative to it.
            if (r.eax == static_cast<std::uint32_t>(g_stream)) {
                r.eax = 0xabcd0000u;
            }
            g_lockLog = nullptr;
            return r;
        };
        const IosRun ra = run(original, a);
        const IosRun rb = run(ours, b);
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT(ra.log == rb.log) ||
            !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            ok = false;
        }
    }
    test::UnpatchJump(kMtUnlock);
    test::UnpatchJump(kMtLock);
    return ok;
}

// Wrong: the flags outside the mask are dropped.
std::uint32_t __fastcall WrongSetf(Ios* self, void* edx, std::uint32_t flags, std::uint32_t mask) {
    const std::uint32_t old = Ios_Setf(self, edx, flags, mask);
    self->x_flags &= mask;
    return old;
}

}  // namespace

S3RE_TEST(OldIostream_MatchesOriginal) {
    CompareIos(ctx, {&Ios_Lock, &Ios_Unlock, &Ios_Setf}, 3000);
}

S3RE_TEST(OldIostream_ComparisonDetectsWrongImplementation) {
    test::Context quiet("wrong", 3, true);
    S3RE_EXPECT(!CompareIos(quiet, {&Ios_Lock, &Ios_Unlock, &WrongSetf}, 3000));
    test::Context quiet2("wrong", 4, true);
    S3RE_EXPECT(!CompareIos(quiet2, {&Ios_Unlock, &Ios_Unlock, &Ios_Setf}, 3000));
}

}  // namespace s3re::msvc6
