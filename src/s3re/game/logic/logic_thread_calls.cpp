// Small CLogicThread members that mostly call others (Volker\LogicInit.cpp,
// LogicDraw.cpp; wip/logic-r8).
//
// CLogicThread::TakeDrawRequest (0x0051f0b0): () thiscall: asks the
//   draw-request event (+0x3d8) whether it is signalled (vtable slot 0,
//   Wait(0 ms, no throw)), resets it (Win32::CEvent::Reset 0x523110) and
//   returns the wait's AL, zero-extended (PrepareDrawData tests EAX).
// CLogicThread::UpdateLobby (0x00541170): () thiscall, every step: when
//   there is a lobby object (+0x66c) and it is connected (0x5dd3a0, AL),
//   the lobby client (+0x670) is polled (CLobbyManager::Update(1) 0x4532f0),
//   then PollLobbyStatus 0x540980 and PollLobbyChat 0x540f40. EAX not used.
// CLogicThread::OnExit (0x0053dab0): () thiscall, vtable slot 6, EAX not
//   used (Win32::CThread::Execute keeps Main's): the wait set's list (+0x3e0)
//   clearAndDestroy (0x60b560), StopMusic 0x53f5c0, the timer's Stop
//   0x60d3e0, the graphics-data semaphore Release(1) 0x53e830,
//   DirectX::CGameThread::OnExit 0x5b6470; the draw-data buffer (+0x3d0)
//   destroyed (0x405a50, probably ~DrawData) and freed, +0x3d0 = 0; each
//   computer player deleted (vtable slot 0, the deleting destructor, with
//   1) and its slot cleared.

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kEventReset = 0x00523110;        // Win32::CEvent::Reset
constexpr std::uintptr_t kLobbyConnected = 0x005dd3a0;    // Lobby_sub_5dd3a0 (AL)
constexpr std::uintptr_t kLobbyUpdate = 0x004532f0;       // Menus::priv::CLobbyManager::Update(bool)
constexpr std::uintptr_t kPollLobbyStatus = 0x00540980;   // CLogicThread::PollLobbyStatus
constexpr std::uintptr_t kPollLobbyChat = 0x00540f40;     // CLogicThread::PollLobbyChat
constexpr std::uintptr_t kClearAndDestroy = 0x0060b560;   // WCIsvSListBase::clearAndDestroy
constexpr std::uintptr_t kStopMusic = 0x0053f5c0;         // CLogicThread::StopMusic
constexpr std::uintptr_t kTimerStop = 0x0060d3e0;         // Win32::CTimer::Stop
constexpr std::uintptr_t kSemaphoreRelease = 0x0053e830;  // Win32::CSemaphore::Release(count)
constexpr std::uintptr_t kGameThreadOnExit = 0x005b6470;  // DirectX::CGameThread::OnExit
constexpr std::uintptr_t kDrawDataDestroy = 0x00405a50;   // App_sub_405a50 (probably ~DrawData)
constexpr std::uint32_t kPollOnce = 1;                    // CLobbyManager::Update's argument
constexpr std::uint32_t kReleaseOne = 1;
constexpr std::uint32_t kDeleteObject = 1;                // the deleting destructor's flag
constexpr std::size_t kWaitSetList = 4;                   // wait_set + 4 = CLogicThread+0x3e0

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using Member2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);

std::uint32_t Call(void* self, std::uintptr_t address) {
    return reinterpret_cast<MemberFn>(address)(self, nullptr);
}

std::uint32_t Call(void* self, std::uintptr_t address, std::uint32_t value) {
    return reinterpret_cast<Member1Fn>(address)(self, nullptr, value);
}

// Slot `slot` of an object's vtable.
template <typename Fn>
Fn Virtual(void* object, std::size_t slot) {
    return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[slot]);
}

}  // namespace

using TakeDrawRequestFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);
using ThreadVoidFn = void(__fastcall*)(CLogicThread* self, void* edx);
std::uint32_t __fastcall CLogicThread_TakeDrawRequest(CLogicThread* self, void* edx);
void __fastcall CLogicThread_UpdateLobby(CLogicThread* self, void* edx);
void __fastcall CLogicThread_OnExit(CLogicThread* self, void* edx);

S3RE_HOOK(0x0051f0b0, CLogicThread_TakeDrawRequest, TakeDrawRequestFn, Original_CLogicThread_TakeDrawRequest,
          CLogicThread_TakeDrawRequest);
S3RE_HOOK(0x00541170, CLogicThread_UpdateLobby, ThreadVoidFn, Original_CLogicThread_UpdateLobby,
          CLogicThread_UpdateLobby);
S3RE_HOOK(0x0053dab0, CLogicThread_OnExit, ThreadVoidFn, Original_CLogicThread_OnExit, CLogicThread_OnExit);

// Purpose: Whether the draw thread asked for new draw data (the request
// is taken: the event is reset).
std::uint32_t __fastcall CLogicThread_TakeDrawRequest(CLogicThread* self, void* /*edx*/) {
    void* event = self->draw_request_event;
    const auto signalled = static_cast<std::uint8_t>(Virtual<Member2Fn>(event, 0)(event, nullptr, 0, 0));
    Call(self->draw_request_event, kEventReset);
    return signalled;
}

// Purpose: Polls the lobby (multiplayer over the Blue Byte lobby) once per
// step when it is connected.
void __fastcall CLogicThread_UpdateLobby(CLogicThread* self, void* /*edx*/) {
    if (self->lobby == nullptr || static_cast<std::uint8_t>(Call(self->lobby, kLobbyConnected)) == 0) {
        return;
    }
    Call(self->lobby_client, kLobbyUpdate, kPollOnce);
    Call(self, kPollLobbyStatus);
    Call(self, kPollLobbyChat);
}

// Purpose: The logic thread's shutdown: stops the music and the tick
// timer, releases the graphics lock, frees the draw data and deletes the
// computer players.
void __fastcall CLogicThread_OnExit(CLogicThread* self, void* /*edx*/) {
    Call(self->wait_set + kWaitSetList, kClearAndDestroy);
    Call(self, kStopMusic);
    Call(self->timer, kTimerStop);
    Call(g_graphicsDataLock.get(), kSemaphoreRelease, kReleaseOne);
    Call(self, kGameThreadOnExit);
    if (void* data = self->draw_data; data != nullptr) {
        Call(data, kDrawDataDestroy);
        msvc6::GameOperatorDelete(data);
    }
    self->draw_data = nullptr;
    for (std::uint32_t& player : self->computer_players) {
        if (player != 0) {
            void* object = reinterpret_cast<void*>(player);
            Virtual<Member1Fn>(object, 0)(object, nullptr, kDeleteObject);
        }
        player = 0;
    }
}

// ---------------------------------------------------------------------------
// Tests: a thread image (random bytes) at the start of a buffer per side,
// its objects (events, timer, lobby, draw data, computer players) in the
// buffer behind it; every callee a recording stub (ECX relative to the
// buffer), the virtual calls a fake vtable that records, operator delete a
// recording fake. Compared: EAX, the thread, the calls and records.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kObjects = 0x1000;  // offset of the objects in the buffer
constexpr std::size_t kCallBufferBytes = 0x2000;

struct CallLog {
    std::vector<std::uint32_t> events;
    std::uintptr_t base = 0;
    std::uint32_t seed = 0;
};
CallLog* g_callLog = nullptr;

std::uint32_t Rel(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - g_callLog->base);
}

std::uint32_t __fastcall FakeVirtual0(void* self, void*, std::uint32_t a, std::uint32_t b) {
    g_callLog->events.insert(g_callLog->events.end(), {0x7e00u, Rel(self), a, b});
    return g_callLog->seed ^ Rel(self);
}
std::uint32_t __fastcall FakeDeletingDestructor(void* self, void*, std::uint32_t flag) {
    g_callLog->events.insert(g_callLog->events.end(), {0x7d00u, Rel(self), flag});
    return 0;
}
void* const g_waitVtable[1] = {reinterpret_cast<void*>(&FakeVirtual0)};
void* const g_playerVtable[1] = {reinterpret_cast<void*>(&FakeDeletingDestructor)};

std::uint32_t __cdecl FakeDelete(void* block) {
    g_callLog->events.insert(g_callLog->events.end(), {0xde1e7eu, Rel(block)});
    return 0;
}

enum class Which { TakeDrawRequest, UpdateLobby, OnExit };

struct CallRun {
    std::uint32_t eax = 0;
    std::vector<std::uint8_t> thread;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> events;
};

CallRun RunCall(Which which, const std::vector<std::uint8_t>& image, std::uint32_t seed, std::uint8_t* buffer,
                TakeDrawRequestFn take, ThreadVoidFn lobby, ThreadVoidFn exit) {
    std::memset(buffer, 0, kCallBufferBytes);
    auto* thread = reinterpret_cast<CLogicThread*>(buffer);
    std::memcpy(thread, image.data(), image.size());
    std::uint8_t* objects = buffer + kObjects;
    test::Rng rng(seed);
    auto object = [&](std::size_t offset, void* const* vtable) {
        void* p = objects + offset;
        *static_cast<void* const**>(p) = vtable;
        return p;
    };
    thread->draw_request_event = object(0x00, g_waitVtable);
    thread->lobby = rng.Chance(1, 4) ? nullptr : object(0x10, g_waitVtable);
    thread->lobby_client = object(0x20, g_waitVtable);
    thread->timer = object(0x30, g_waitVtable);
    thread->draw_data = rng.Chance(1, 4) ? nullptr : object(0x40, g_waitVtable);
    for (std::uint32_t p = 0; p < 20; ++p) {
        thread->computer_players[p] =
            rng.Chance(1, 2) ? 0
                             : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object(0x100 + 0x10 * p,
                                                                                                  g_playerVtable)));
    }
    void* const savedLock = g_graphicsDataLock;
    g_graphicsDataLock = object(0x50, g_waitVtable);
    CallLog log;
    log.base = reinterpret_cast<std::uintptr_t>(buffer);
    log.seed = seed;
    g_callLog = &log;
    CallRun run;
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(buffer));
        stubs.Install<0, kEventReset>();
        const std::uint32_t connected = rng.Next32();
        stubs.Install<0, kLobbyConnected>([connected](CGameData*, std::uint32_t, const std::uint32_t*) { return connected; });
        stubs.Install<1, kLobbyUpdate>();
        stubs.Install<0, kPollLobbyStatus>();
        stubs.Install<0, kPollLobbyChat>();
        stubs.Install<0, kClearAndDestroy>();
        stubs.Install<0, kStopMusic>();
        stubs.Install<0, kTimerStop>();
        stubs.Install<1, kSemaphoreRelease>();
        stubs.Install<0, kGameThreadOnExit>();
        stubs.Install<0, kDrawDataDestroy>();
        test::ScopedJump remove{0x0061e4ef, reinterpret_cast<const void*>(&FakeDelete)};
        switch (which) {
            case Which::TakeDrawRequest:
                run.eax = take(thread, nullptr);
                break;
            case Which::UpdateLobby:
                lobby(thread, nullptr);
                break;
            case Which::OnExit:
                exit(thread, nullptr);
                break;
        }
        run.calls = stubs.Take();
    }
    g_graphicsDataLock = savedLock;
    run.thread.assign(buffer, buffer + image.size());
    run.events = std::move(log.events);
    // Pointers into the buffer as offsets.
    auto* t = reinterpret_cast<CLogicThread*>(run.thread.data());
    for (void** p : {&t->draw_request_event, &t->lobby, &t->lobby_client, &t->timer, &t->draw_data}) {
        if (*p != nullptr) {
            *p = reinterpret_cast<void*>(Rel(*p));
        }
    }
    for (std::uint32_t& player : t->computer_players) {
        if (player != 0) {
            player = Rel(reinterpret_cast<void*>(player));
        }
    }
    g_callLog = nullptr;
    return run;
}

void CompareCalls(test::Context& ctx, Which which, TakeDrawRequestFn take, ThreadVoidFn lobby, ThreadVoidFn exit,
                  int cases) {
    std::vector<std::uint8_t> a(kCallBufferBytes), b(kCallBufferBytes);
    std::vector<std::uint8_t> image(sizeof(CLogicThread));
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(image.data(), image.size());
        const std::uint32_t seed = ctx.rng().Next32();
        const CallRun expected = RunCall(which, image, seed, a.data(), Original_CLogicThread_TakeDrawRequest,
                                         Original_CLogicThread_UpdateLobby, Original_CLogicThread_OnExit);
        const CallRun actual = RunCall(which, image, seed, b.data(), take, lobby, exit);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT(expected.thread == actual.thread);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.events == actual.events);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (function %d)", i, static_cast<int>(which));
            return;
        }
    }
}

// Wrong: the draw-data pointer is left behind.
void __fastcall WrongOnExit(CLogicThread* self, void* edx) {
    void* const data = self->draw_data;
    CLogicThread_OnExit(self, edx);
    self->draw_data = data;
}

// Wrong: the full EAX of the wait.
std::uint32_t __fastcall WrongTakeDrawRequest(CLogicThread* self, void* edx) {
    void* event = self->draw_request_event;
    const std::uint32_t signalled = Virtual<Member2Fn>(event, 0)(event, nullptr, 0, 0);
    Call(self->draw_request_event, kEventReset);
    (void)edx;
    return signalled;
}

}  // namespace

S3RE_TEST(CLogicThread_Calls_MatchOriginal) {
    CompareCalls(ctx, Which::TakeDrawRequest, &CLogicThread_TakeDrawRequest, &CLogicThread_UpdateLobby,
                 &CLogicThread_OnExit, 500);
    CompareCalls(ctx, Which::UpdateLobby, &CLogicThread_TakeDrawRequest, &CLogicThread_UpdateLobby,
                 &CLogicThread_OnExit, 500);
    CompareCalls(ctx, Which::OnExit, &CLogicThread_TakeDrawRequest, &CLogicThread_UpdateLobby, &CLogicThread_OnExit,
                 500);
}

S3RE_TEST(CLogicThread_Calls_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareCalls(probe, Which::TakeDrawRequest, &WrongTakeDrawRequest, &CLogicThread_UpdateLobby,
                     &CLogicThread_OnExit, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareCalls(probe, Which::OnExit, &CLogicThread_TakeDrawRequest, &CLogicThread_UpdateLobby, &WrongOnExit,
                     200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
