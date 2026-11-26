// Win32::CCriticalSection and Win32::CLock (Win32\Synchronize.cpp /
// synchronize.ipp; class names invented, re/win32_framework.md): the
// framework's critical section (the object is the CRITICAL_SECTION
// itself) and its scoped lock.
//
// CCriticalSection::Enter (0x0060d670) / Leave (0x0060d680): thiscall ():
//   EnterCriticalSection / LeaveCriticalSection(this) through S3.EXE's
//   imports; EAX is what the call leaves (they return void).
// CLock::CLock (0x0060da00): thiscall (CCriticalSection* section) ret 4:
//   +0 = section, section->Enter (0x60d670); EAX = this.
// CLock::~CLock (0x0060da20): thiscall (): (+0)->Leave (0x60d680; mov ecx,
//   [ecx]; jmp); EAX = Leave's.

#include "game/globals.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kEnter = 0x0060d670;
constexpr std::uintptr_t kLeave = 0x0060d680;

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);

struct CLock {
    void* section;  // +0x00 the locked CCriticalSection
};
static_assert(sizeof(CLock) == 4);

}  // namespace

std::uint32_t __fastcall Win32_CCriticalSection_Enter(void* self, void* edx);
std::uint32_t __fastcall Win32_CCriticalSection_Leave(void* self, void* edx);
void* __fastcall Win32_CLock_Construct(void* self, void* edx, void* section);
std::uint32_t __fastcall Win32_CLock_Destroy(void* self, void* edx);
using SectionFn = decltype(&Win32_CCriticalSection_Enter);
using LockConstructFn = decltype(&Win32_CLock_Construct);

S3RE_HOOK(0x0060d670, Win32_CCriticalSection_Enter, SectionFn, Original_Win32_CCriticalSection_Enter,
          Win32_CCriticalSection_Enter);
S3RE_HOOK(0x0060d680, Win32_CCriticalSection_Leave, SectionFn, Original_Win32_CCriticalSection_Leave,
          Win32_CCriticalSection_Leave);
S3RE_HOOK(0x0060da00, Win32_CLock_Construct, LockConstructFn, Original_Win32_CLock_Construct, Win32_CLock_Construct);
S3RE_HOOK(0x0060da20, Win32_CLock_Destroy, SectionFn, Original_Win32_CLock_Destroy, Win32_CLock_Destroy);

// Purpose: enters a critical section (the message queues and the other
// thread-shared objects lock with it).
std::uint32_t __fastcall Win32_CCriticalSection_Enter(void* self, void* /*edx*/) {
    return g_importEnterCriticalSection.get()(self);
}

// Purpose: leaves a critical section.
std::uint32_t __fastcall Win32_CCriticalSection_Leave(void* self, void* /*edx*/) {
    return g_importLeaveCriticalSection.get()(self);
}

// Purpose: a scoped lock: remembers the section and enters it.
void* __fastcall Win32_CLock_Construct(void* self, void* /*edx*/, void* section) {
    static_cast<CLock*>(self)->section = section;
    reinterpret_cast<MemberFn>(kEnter)(section, nullptr);
    return self;
}

// Purpose: the scoped lock's end: leaves its section.
std::uint32_t __fastcall Win32_CLock_Destroy(void* self, void* /*edx*/) {
    return reinterpret_cast<MemberFn>(kLeave)(static_cast<CLock*>(self)->section, nullptr);
}

// ---------------------------------------------------------------------------
// Tests: the two imports point at fakes that log the section and return a
// value made from it; the original and ours run on copies of a lock.
// ---------------------------------------------------------------------------

namespace {

std::vector<std::uint32_t>* g_sectionLog = nullptr;
std::uint32_t g_sectionBase = 0;

std::uint32_t __stdcall FakeEnter(void* section) {
    const std::uint32_t relative = reinterpret_cast<std::uint32_t>(section) - g_sectionBase;
    g_sectionLog->insert(g_sectionLog->end(), {0xe7, relative});
    return relative * 7u + 1;
}

std::uint32_t __stdcall FakeLeave(void* section) {
    const std::uint32_t relative = reinterpret_cast<std::uint32_t>(section) - g_sectionBase;
    g_sectionLog->insert(g_sectionLog->end(), {0x1e, relative});
    return relative * 5u + 3;
}

struct LockFunctions {
    SectionFn enter, leave;
    LockConstructFn construct;
    SectionFn destroy;
};

std::vector<std::uint32_t> RunLocks(const LockFunctions& f, std::uint32_t op, std::uint8_t* area) {
    std::vector<std::uint32_t> log;
    g_sectionLog = &log;
    g_sectionBase = reinterpret_cast<std::uint32_t>(area);
    const void* const enter = reinterpret_cast<const void*>(&FakeEnter);
    const void* const leave = reinterpret_cast<const void*>(&FakeLeave);
    test::PatchBytes(g_importEnterCriticalSection.address(), &enter, sizeof(enter));
    test::PatchBytes(g_importLeaveCriticalSection.address(), &leave, sizeof(leave));
    void* const section = area + 16;
    void* const lock = area;
    std::uint32_t eax = 0;
    switch (op) {
    case 0: eax = f.enter(section, nullptr); break;
    case 1: eax = f.leave(section, nullptr); break;
    case 2: eax = reinterpret_cast<std::uint32_t>(f.construct(lock, nullptr, section)) - g_sectionBase; break;
    default: eax = f.destroy(lock, nullptr); break;
    }
    test::UnpatchBytes(g_importLeaveCriticalSection.address());
    test::UnpatchBytes(g_importEnterCriticalSection.address());
    log.push_back(eax);
    std::uint32_t words[8];
    std::memcpy(words, area, sizeof(words));
    log.insert(log.end(), words, words + 8);
    g_sectionLog = nullptr;
    return log;
}

void CompareLocks(test::Context& ctx, const LockFunctions& candidate, int cases) {
    const LockFunctions original{Original_Win32_CCriticalSection_Enter, Original_Win32_CCriticalSection_Leave,
                                 Original_Win32_CLock_Construct, Original_Win32_CLock_Destroy};
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        std::uint8_t a[32];
        rng.Fill(a, sizeof(a));
        if (rng.Chance(1, 2)) {
            const std::uint32_t inside = reinterpret_cast<std::uint32_t>(a) + 16;  // the lock points at the section
            std::memcpy(a, &inside, 4);
        }
        std::uint8_t b[32];
        std::memcpy(b, a, sizeof(a));
        const std::uint32_t op = rng.Below(4);
        // The lock's pointer is used relative to its own area by both.
        const std::uint32_t pointer = *reinterpret_cast<std::uint32_t*>(a);
        const std::uint32_t moved = pointer - reinterpret_cast<std::uint32_t>(a) + reinterpret_cast<std::uint32_t>(b);
        std::memcpy(b, &moved, 4);
        std::vector<std::uint32_t> la = RunLocks(original, op, a);
        std::vector<std::uint32_t> lb = RunLocks(candidate, op, b);
        // The lock's section pointer (word 0 of the area, after the call's
        // two values and EAX) differs by the areas' distance.
        la[3] -= reinterpret_cast<std::uint32_t>(a);
        lb[3] -= reinterpret_cast<std::uint32_t>(b);
        if (!S3RE_EXPECT(la == lb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: op %u", n, op);
            break;
        }
    }
}

// Wrong: the scoped lock leaves instead of entering.
void* __fastcall WrongLockConstruct(void* self, void* /*edx*/, void* section) {
    static_cast<CLock*>(self)->section = section;
    reinterpret_cast<MemberFn>(kLeave)(section, nullptr);
    return self;
}

}  // namespace

S3RE_TEST(Win32Locks_MatchOriginal) {
    CompareLocks(ctx,
                 {&Win32_CCriticalSection_Enter, &Win32_CCriticalSection_Leave, &Win32_CLock_Construct,
                  &Win32_CLock_Destroy},
                 400);
}

S3RE_TEST(Win32Locks_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLocks(probe,
                 {&Win32_CCriticalSection_Enter, &Win32_CCriticalSection_Leave, &WrongLockConstruct,
                  &Win32_CLock_Destroy},
                 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
