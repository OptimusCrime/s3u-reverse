// Differential tests of win32_synchronize.cpp (session 58,
// wip/framework-errors). Each case runs the original (Original_*
// trampoline) and ours on the same random object (a 0x40-byte buffer with
// guard bytes; for PostThreadMessage a virtual-base table pointing at a
// CThread part inside it) with every Windows function they call replaced
// (its import slot) by a fake that logs the call and returns a result
// drawn from an Rng seeded the same for both runs (failures and
// GetLastError codes of 0 included). A thrown game exception is caught
// with SEH: its type, throw info, code (+0x18) and message text are
// compared, then it is deleted. Also compared: EAX, the object bytes, the
// fake calls.

#include "game/globals.h"
#include "game/vtables.h"
#include "msvc6/catch_match.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using MemberArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using Member2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Member3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);
using Member4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d);

std::uint32_t __fastcall Win32_CEvent_Set(void* self, void* edx);
std::uint32_t __fastcall Win32_CEvent_Reset(void* self, void* edx);
std::uint32_t __fastcall Win32_CEvent_Pulse(void* self, void* edx);
std::uint32_t __fastcall Win32_CSemaphore_Release(void* self, void* edx, std::uint32_t count);
std::uint32_t __fastcall Win32_CEvent_Create(void* self, void* edx, std::uint32_t security, std::uint32_t manualReset,
                                             std::uint32_t initialState, std::uint32_t name);
std::uint32_t __fastcall Win32_CSemaphore_Create(void* self, void* edx, std::uint32_t security, std::uint32_t initial,
                                                 std::uint32_t maximum, std::uint32_t name);
std::uint32_t __fastcall Win32_CMutex_Create(void* self, void* edx, std::uint32_t security,
                                             std::uint32_t initialOwner, std::uint32_t name);
std::uint32_t __fastcall Win32_CWin32Waitable_Wait(void* self, void* edx, std::uint32_t timeout,
                                                   std::uint32_t throwOnTimeout);
std::uint32_t __fastcall Win32_CThread_SetPriority(void* self, void* edx, std::uint32_t priority);
std::uint32_t __fastcall Win32_CMessagedThread_PostThreadMessage(void* self, void* edx, std::uint32_t message,
                                                                 std::uint32_t wParam, std::uint32_t lParam);
std::uint32_t __fastcall Win32_CWindowBase_PostMessage(void* self, void* edx, std::uint32_t message,
                                                       std::uint32_t wParam, std::uint32_t lParam);
std::uint32_t __fastcall Win32_CWindowBase_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CThread_Start(void* self, void* edx, std::uint32_t parameter);
std::uint32_t __fastcall Win32_CWindowBase_Show(void* self, void* edx);
std::uint32_t __cdecl Win32_HasPendingMessage();
void* __fastcall Win32_CCriticalSection_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameThread_OnExit(void* self, void* edx);
using HasPendingMessageFn = std::uint32_t(__cdecl*)();
using ConstructFn = void*(__fastcall*)(void* self, void* edx);

extern MemberFn Original_Win32_CEvent_Set;
extern MemberFn Original_Win32_CEvent_Reset;
extern MemberFn Original_Win32_CEvent_Pulse;
extern MemberArgFn Original_Win32_CSemaphore_Release;
extern Member4Fn Original_Win32_CEvent_Create;
extern Member4Fn Original_Win32_CSemaphore_Create;
extern Member3Fn Original_Win32_CMutex_Create;
extern Member2Fn Original_Win32_CWin32Waitable_Wait;
extern MemberArgFn Original_Win32_CThread_SetPriority;
extern Member3Fn Original_Win32_CMessagedThread_PostThreadMessage;
extern Member3Fn Original_Win32_CWindowBase_PostMessage;
extern MemberFn Original_Win32_CWindowBase_Destroy;
extern MemberArgFn Original_Win32_CThread_Start;
extern MemberFn Original_Win32_CWindowBase_Show;
extern HasPendingMessageFn Original_Win32_HasPendingMessage;
extern ConstructFn Original_Win32_CCriticalSection_Construct;
extern MemberFn Original_DirectX_CGameThread_OnExit;

namespace {

struct Functions {
    MemberFn set, reset, pulse, destroy;
    MemberArgFn release, setPriority;
    Member4Fn createEvent, createSemaphore;
    Member3Fn createMutex, postThreadMessage, postMessage;
    Member2Fn wait;
    // wip/menus-r5
    MemberArgFn threadStart;
    MemberFn show;
    HasPendingMessageFn hasPendingMessage;
    ConstructFn criticalSection;
    MemberFn gameThreadOnExit;
};

Functions Originals() {
    return {Original_Win32_CEvent_Set,         Original_Win32_CEvent_Reset,
            Original_Win32_CEvent_Pulse,       Original_Win32_CWindowBase_Destroy,
            Original_Win32_CSemaphore_Release, Original_Win32_CThread_SetPriority,
            Original_Win32_CEvent_Create,      Original_Win32_CSemaphore_Create,
            Original_Win32_CMutex_Create,      Original_Win32_CMessagedThread_PostThreadMessage,
            Original_Win32_CWindowBase_PostMessage, Original_Win32_CWin32Waitable_Wait,
            Original_Win32_CThread_Start, Original_Win32_CWindowBase_Show, Original_Win32_HasPendingMessage,
            Original_Win32_CCriticalSection_Construct, Original_DirectX_CGameThread_OnExit};
}

Functions Ours() {
    return {&Win32_CEvent_Set,         &Win32_CEvent_Reset,         &Win32_CEvent_Pulse,
            &Win32_CWindowBase_Destroy, &Win32_CSemaphore_Release, &Win32_CThread_SetPriority,
            &Win32_CEvent_Create,      &Win32_CSemaphore_Create,    &Win32_CMutex_Create,
            &Win32_CMessagedThread_PostThreadMessage, &Win32_CWindowBase_PostMessage, &Win32_CWin32Waitable_Wait,
            &Win32_CThread_Start, &Win32_CWindowBase_Show, &Win32_HasPendingMessage,
            &Win32_CCriticalSection_Construct, &DirectX_CGameThread_OnExit};
}

// ---------------------------------------------------------------- the fakes

struct Fake {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t Result() {
        // Mostly success, a failure in about one call in three.
        if (rng.Chance(1, 3)) {
            return 0;
        }
        return rng.Chance(1, 2) ? 1 : rng.Next32();
    }
};
Fake* g_fake = nullptr;

int __stdcall FakeHandleCall(void* handle) {
    g_fake->log.insert(g_fake->log.end(), {0x11u, reinterpret_cast<std::uint32_t>(handle)});
    return static_cast<int>(g_fake->Result());
}
int __stdcall FakeReleaseSemaphore(void* handle, std::int32_t count, std::int32_t* previous) {
    g_fake->log.insert(g_fake->log.end(), {0x12u, reinterpret_cast<std::uint32_t>(handle),
                                           static_cast<std::uint32_t>(count), previous != nullptr ? 1u : 0u});
    return static_cast<int>(g_fake->Result());
}
void* __stdcall FakeCreateEvent(void* security, std::uint32_t manual, std::uint32_t initial, const char* name) {
    g_fake->log.insert(g_fake->log.end(), {0x13u, reinterpret_cast<std::uint32_t>(security), manual, initial,
                                           reinterpret_cast<std::uint32_t>(name)});
    return reinterpret_cast<void*>(g_fake->Result());
}
void* __stdcall FakeCreateSemaphore(void* security, std::int32_t initial, std::int32_t maximum, const char* name) {
    g_fake->log.insert(g_fake->log.end(), {0x14u, reinterpret_cast<std::uint32_t>(security),
                                           static_cast<std::uint32_t>(initial), static_cast<std::uint32_t>(maximum),
                                           reinterpret_cast<std::uint32_t>(name)});
    return reinterpret_cast<void*>(g_fake->Result());
}
void* __stdcall FakeCreateMutex(void* security, std::uint32_t owner, const char* name) {
    g_fake->log.insert(g_fake->log.end(), {0x15u, reinterpret_cast<std::uint32_t>(security), owner,
                                           reinterpret_cast<std::uint32_t>(name)});
    return reinterpret_cast<void*>(g_fake->Result());
}
int __stdcall FakeSetThreadPriority(void* thread, std::int32_t priority) {
    g_fake->log.insert(g_fake->log.end(),
                       {0x16u, reinterpret_cast<std::uint32_t>(thread), static_cast<std::uint32_t>(priority)});
    return static_cast<int>(g_fake->Result());
}
int __stdcall FakePostThreadMessage(std::uint32_t thread, std::uint32_t message, std::uint32_t wParam,
                                    std::uint32_t lParam) {
    g_fake->log.insert(g_fake->log.end(), {0x17u, thread, message, wParam, lParam});
    return static_cast<int>(g_fake->Result());
}
int __stdcall FakePostMessage(void* window, std::uint32_t message, std::uint32_t wParam, std::uint32_t lParam) {
    g_fake->log.insert(g_fake->log.end(), {0x18u, reinterpret_cast<std::uint32_t>(window), message, wParam, lParam});
    return static_cast<int>(g_fake->Result());
}
std::uint32_t __stdcall FakeWaitForSingleObject(void* handle, std::uint32_t milliseconds) {
    g_fake->log.insert(g_fake->log.end(), {0x19u, reinterpret_cast<std::uint32_t>(handle), milliseconds});
    static const std::uint32_t kResults[] = {0, 0, 0x102, 0x102, 0x80, 0xFFFFFFFF, 1};
    return g_fake->rng.Chance(1, 8) ? g_fake->rng.Next32() : kResults[g_fake->rng.Below(7)];
}
std::uint32_t __stdcall FakeGetLastError() {
    g_fake->log.push_back(0x1Au);
    static const std::uint32_t kCodes[] = {0, 0, 5, 6, 87, 1400, 0x12345};
    return g_fake->rng.Chance(1, 8) ? g_fake->rng.Next32() : kCodes[g_fake->rng.Below(7)];
}

// wip/menus-r5: the thread start's CRT call and the window / message /
// COM imports. Pointers to the object are logged relative to it (each run
// has its own object), stack pointers by presence.
std::uintptr_t g_self = 0;
std::uint32_t RelativeToSelf(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - g_self);
}
std::uint32_t __cdecl FakeBeginThreadEx(void* security, std::uint32_t stack, std::uintptr_t start, void* argument,
                                        std::uint32_t flags, std::uint32_t* threadId) {
    g_fake->log.insert(g_fake->log.end(), {0x20u, reinterpret_cast<std::uint32_t>(security), stack,
                                           static_cast<std::uint32_t>(start), RelativeToSelf(argument), flags,
                                           RelativeToSelf(threadId)});
    return g_fake->Result();
}
std::uint32_t __stdcall FakeResumeThread(void* thread) {
    g_fake->log.insert(g_fake->log.end(), {0x21u, reinterpret_cast<std::uint32_t>(thread)});
    return g_fake->Result();
}
int __stdcall FakeShowWindow(void* window, std::int32_t show) {
    g_fake->log.insert(g_fake->log.end(), {0x22u, reinterpret_cast<std::uint32_t>(window), static_cast<std::uint32_t>(show)});
    return static_cast<int>(g_fake->Result());
}
int __stdcall FakePeekMessage(void* msg, void* window, std::uint32_t first, std::uint32_t last, std::uint32_t remove) {
    g_fake->log.insert(g_fake->log.end(), {0x23u, msg != nullptr ? 1u : 0u, reinterpret_cast<std::uint32_t>(window),
                                           first, last, remove});
    return static_cast<int>(g_fake->Result());
}
void __stdcall FakeInitializeCriticalSection(void* section) {
    g_fake->log.insert(g_fake->log.end(), {0x24u, RelativeToSelf(section)});
}
void __stdcall FakeCoUninitialize() {
    g_fake->log.push_back(0x25u);
}

struct Slot {
    std::uintptr_t address;
    std::uintptr_t fake;
};

class InstalledFakes {
public:
    explicit InstalledFakes(Fake* fake) {
        g_fake = fake;
        const Slot slots[] = {
            {g_importSetEvent.address(), reinterpret_cast<std::uintptr_t>(&FakeHandleCall)},
            {g_importResetEvent.address(), reinterpret_cast<std::uintptr_t>(&FakeHandleCall)},
            {g_importPulseEvent.address(), reinterpret_cast<std::uintptr_t>(&FakeHandleCall)},
            {g_importDestroyWindow.address(), reinterpret_cast<std::uintptr_t>(&FakeHandleCall)},
            {g_importReleaseSemaphore.address(), reinterpret_cast<std::uintptr_t>(&FakeReleaseSemaphore)},
            {g_importCreateEventA.address(), reinterpret_cast<std::uintptr_t>(&FakeCreateEvent)},
            {g_importCreateSemaphoreA.address(), reinterpret_cast<std::uintptr_t>(&FakeCreateSemaphore)},
            {g_importCreateMutexA.address(), reinterpret_cast<std::uintptr_t>(&FakeCreateMutex)},
            {g_importSetThreadPriority.address(), reinterpret_cast<std::uintptr_t>(&FakeSetThreadPriority)},
            {g_importPostThreadMessageA.address(), reinterpret_cast<std::uintptr_t>(&FakePostThreadMessage)},
            {g_importPostMessageA.address(), reinterpret_cast<std::uintptr_t>(&FakePostMessage)},
            {g_importWaitForSingleObject.address(), reinterpret_cast<std::uintptr_t>(&FakeWaitForSingleObject)},
            {g_importGetLastError.address(), reinterpret_cast<std::uintptr_t>(&FakeGetLastError)},
            {g_importResumeThread.address(), reinterpret_cast<std::uintptr_t>(&FakeResumeThread)},
            {g_importShowWindow.address(), reinterpret_cast<std::uintptr_t>(&FakeShowWindow)},
            {g_importPeekMessageA.address(), reinterpret_cast<std::uintptr_t>(&FakePeekMessage)},
            {g_importInitializeCriticalSection.address(),
             reinterpret_cast<std::uintptr_t>(&FakeInitializeCriticalSection)},
            {g_importCoUninitialize.address(), reinterpret_cast<std::uintptr_t>(&FakeCoUninitialize)},
        };
        test::PatchJump(0x0062279a, reinterpret_cast<const void*>(&FakeBeginThreadEx));  // _beginthreadex
        jumped_ = true;
        for (const Slot& slot : slots) {
            test::PatchBytes(slot.address, &slot.fake, 4);
            patched_.push_back(slot.address);
        }
    }
    ~InstalledFakes() {
        if (jumped_) {
            test::UnpatchJump(0x0062279a);
        }
        for (auto it = patched_.rbegin(); it != patched_.rend(); ++it) {
            test::UnpatchBytes(*it);
        }
        g_fake = nullptr;
    }
    InstalledFakes(const InstalledFakes&) = delete;
    InstalledFakes& operator=(const InstalledFakes&) = delete;

private:
    std::vector<std::uintptr_t> patched_;
    bool jumped_ = false;
};

// ---------------------------------------------------------------- one case

struct Caught {
    bool threw = false;
    std::uint32_t vtable = 0;
    std::uint32_t info = 0;
    std::uint32_t code = 0;
    std::string message;
    bool operator==(const Caught& o) const {
        return threw == o.threw && vtable == o.vtable && info == o.info && code == o.code && message == o.message;
    }
};

int CatchFilter(const EXCEPTION_POINTERS* info, Caught* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    out->info = static_cast<std::uint32_t>(record->ExceptionInformation[2]);
    void* const object = *reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    if (object != nullptr) {
        const auto* bytes = static_cast<const std::uint8_t*>(object);
        std::memcpy(&out->vtable, bytes, 4);
        std::memcpy(&out->code, bytes + 0x18, 4);  // CHResult's HRESULT (CTimeout: past the object)
        const auto* message = reinterpret_cast<const msvc6::String*>(bytes + 8);
        if (message->ptr != nullptr) {
            out->message.assign(message->ptr, message->len);
        }
        if (out->vtable == vtables::kCTimeout) {
            out->code = 0;  // a CTimeout is 0x18 bytes: no code
        }
    }
    msvc6::DeleteCaughtException(object);
    return EXCEPTION_EXECUTE_HANDLER;
}

constexpr std::size_t kGuard = 8;
constexpr std::size_t kObject = 0x40;

struct Outcome {
    std::uint32_t eax = 0;
    Caught caught;
    std::vector<std::uint32_t> log;
    std::uint8_t bytes[kObject + 2 * kGuard];
};

struct Args {
    std::uint32_t op;
    std::uint32_t a, b, c, d;
};

std::uint32_t Call(const Functions& f, void* self, const Args& args) {
    switch (args.op) {
        case 0: return f.set(self, nullptr);
        case 1: return f.reset(self, nullptr);
        case 2: return f.pulse(self, nullptr);
        case 3: return f.release(self, nullptr, args.a);
        case 4: return f.createEvent(self, nullptr, args.a, args.b, args.c, args.d);
        case 5: return f.createSemaphore(self, nullptr, args.a, args.b, args.c, args.d);
        case 6: return f.createMutex(self, nullptr, args.a, args.b, args.c);
        case 7: return f.wait(self, nullptr, args.a, args.b);
        case 8: return f.setPriority(self, nullptr, args.a);
        case 9: return f.postThreadMessage(self, nullptr, args.a, args.b, args.c);
        case 10: return f.postMessage(self, nullptr, args.a, args.b, args.c);
        case 12: return f.threadStart(self, nullptr, args.a);
        case 13: return f.show(self, nullptr);
        case 14: return f.hasPendingMessage();
        case 15: return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(f.criticalSection(self, nullptr)) -
                                                   reinterpret_cast<std::uintptr_t>(self));
        case 16: return f.gameThreadOnExit(self, nullptr);
        default: return f.destroy(self, nullptr);
    }
}

// The call under SEH (in a function of its own: no objects to unwind).
std::uint32_t Guarded(const Functions& f, void* self, const Args& args, Caught* caught) {
    std::uint32_t eax = 0;
    __try {
        eax = Call(f, self, args);
    } __except (CatchFilter(GetExceptionInformation(), caught)) {
    }
    return eax;
}

Outcome RunOne(const Functions& f, const std::uint8_t* initial, std::uint32_t vbaseOffset, std::uint64_t seed,
               const Args& args) {
    Outcome out;
    std::memcpy(out.bytes, initial, sizeof(out.bytes));
    std::uint8_t* const self = out.bytes + kGuard;
    // The virtual-base table of a CMessagedThread: entry 1 = the CThread's
    // offset (the table lives outside the object; its address is restored
    // before the comparison).
    const std::int32_t vbtable[2] = {0, static_cast<std::int32_t>(vbaseOffset)};
    if (args.op == 9) {
        const auto* table = vbtable;
        std::memcpy(self, &table, 4);
    }
    Fake fake;
    fake.rng = test::Rng(seed);
    g_self = reinterpret_cast<std::uintptr_t>(self);
    {
        InstalledFakes installed(&fake);
        out.eax = Guarded(f, self, args, &out.caught);
    }
    if (args.op == 9) {
        std::memcpy(self, initial + kGuard, 4);
    }
    out.log = fake.log;
    return out;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    const Functions originals = Originals();
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        std::uint8_t initial[kObject + 2 * kGuard];
        rng.Fill(initial, sizeof(initial));
        Args args{};
        args.op = rng.Below(17);
        args.a = rng.Chance(1, 2) ? rng.Below(8) : rng.Next32();
        args.b = rng.Chance(1, 2) ? rng.Below(3) : rng.Next32();
        args.c = rng.Next32();
        args.d = rng.Chance(1, 3) ? 0 : rng.Next32();
        const std::uint32_t vbaseOffset = 4 * rng.Below(8);
        const std::uint64_t seed = rng.Next64();
        const Outcome a = RunOne(originals, initial, vbaseOffset, seed, args);
        const Outcome b = RunOne(ours, initial, vbaseOffset, seed, args);
        // A throw leaves EAX undefined.
        const bool same = S3RE_EXPECT_EQ(a.caught.threw ? 0 : a.eax, b.caught.threw ? 0 : b.eax) &&
                          S3RE_EXPECT(a.caught == b.caught) && S3RE_EXPECT(a.log == b.log) &&
                          S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: op %u, a %08x, b %08x, threw %d/%d, message '%s' / '%s'", i,
                     args.op, args.a, args.b, a.caught.threw ? 1 : 0, b.caught.threw ? 1 : 0,
                     a.caught.message.c_str(), b.caught.message.c_str());
            return;
        }
    }
}

// Wrong: a timeout without throwOnTimeout returns 0 in all of EAX.
std::uint32_t __fastcall WrongWait(void* self, void* edx, std::uint32_t timeout, std::uint32_t throwOnTimeout) {
    const std::uint32_t eax = Win32_CWin32Waitable_Wait(self, edx, timeout, throwOnTimeout);
    return eax == 0x100 ? 0 : eax;
}

// Wrong: PostThreadMessage throws without asking GetLastError.
std::uint32_t __fastcall WrongPostThreadMessage(void* self, void* edx, std::uint32_t message, std::uint32_t wParam,
                                                std::uint32_t lParam) {
    return Win32_CWindowBase_PostMessage(self, edx, message, wParam, lParam);
}

}  // namespace

S3RE_TEST(Win32Synchronize_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(Win32Synchronize_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.wait = &WrongWait;
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    wrong = Ours();
    wrong.postThreadMessage = &WrongPostThreadMessage;
    {
        test::Context probe(ctx.name(), 2, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
