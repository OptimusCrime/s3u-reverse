#pragma once

// Test bench of CLogicThread::ExecuteCommands (0x535990): the differential
// tests of every command group (execute_commands*.cpp) run the whole
// function, the original through its trampoline and ours, on two
// identical world-sized buffers.
//
// Each buffer holds the world (CGameData) and, after it, at the same
// offsets on both sides: the CLogicThread, the two MessageQueues and their
// rings. So stubbed callees with ECX = the thread record the same relative
// ECX on both sides. The queues are driven by the game's own Pop
// (0x430980) and Push (0x4309c0), with a real critical section and event
// shared by both sides; the network -> logic queue holds the case's
// messages, the logic -> network queue collects what the function pushes
// (compared). The queue's wait (vtable slot 3) is FakeWait: it records its
// arguments and can throw a Win32::CTimeout* (the game's throw info
// 0x674f38) to test the catch; the thrown object is a fake whose deleting
// destructor records the call.
//
// A test: build the case's messages with MessageBuilder, set up the world
// through bench.worlds.Both(...), install callee stubs in bench.stubs, then
// bench.Run(ctx, i, messages). Run compares EAX, the stub calls, the
// DebugLog calls, the waits, the deleted timeouts, the thread, the queues
// and rings, and (every `checkpoint` cases, and in Finish) the whole world.

#include "game/debug_log.h"
#include "game/logic/commands/execute_commands.h"
#include "game/logic/logic_thread_layout.h"
#include "game/vtables.h"
#include "game/world.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "game/enums/commands.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace s3re::game {

using ExecuteCommandsFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);
extern ExecuteCommandsFn Original_CLogicThread_ExecuteCommands;
std::uint32_t __fastcall CLogicThread_ExecuteCommands(CLogicThread* self, void*);

namespace commands::bench {

// Layout of each side's buffer (offsets from the world).
inline constexpr std::size_t kThreadOffset = (sizeof(CGameData) + 0xfff) & ~static_cast<std::size_t>(0xfff);
inline constexpr std::size_t kThreadBytes = 0x1000;
inline constexpr std::size_t kFromNetOffset = kThreadOffset + kThreadBytes;
inline constexpr std::size_t kToNetOffset = kFromNetOffset + 0x40;
inline constexpr std::size_t kRingDwords = 1024;
inline constexpr std::size_t kFromRingOffset = kFromNetOffset + 0x100;
inline constexpr std::size_t kToRingOffset = kFromRingOffset + kRingDwords * 4;
inline constexpr std::size_t kBufferBytes = kToRingOffset + kRingDwords * 4;

// One call of the queue's wait.
struct WaitCall {
    std::uint32_t queue;  // relative to the world
    std::uint32_t timeout;
    std::uint32_t flag;
    bool operator==(const WaitCall& o) const { return queue == o.queue && timeout == o.timeout && flag == o.flag; }
};

// State shared by the fakes (tests run on one thread, before the game starts).
struct FakeState {
    CGameData* world = nullptr;
    std::vector<WaitCall> waits;
    int throwAtWait = -1;          // index of the wait that throws a CTimeout (-1: none)
    std::uint32_t deletedTimeouts = 0;
    std::uint32_t deleteFlags = 0;
};
inline FakeState& Fakes() {
    static FakeState state;
    return state;
}

// The thrown "Win32::CTimeout": only its vtable (slot 0 = the deleting destructor) is used.
inline void __fastcall FakeTimeoutDelete(void* object, void*, std::uint32_t flags) {
    (void)object;
    ++Fakes().deletedTimeouts;
    Fakes().deleteFlags = flags;
}
inline void* const kFakeTimeoutVtable[2] = {reinterpret_cast<void*>(&FakeTimeoutDelete), nullptr};
struct FakeTimeout {
    void* const* vtable = kFakeTimeoutVtable;
};

inline std::uint32_t __fastcall FakeWait(MessageQueue* queue, void*, std::uint32_t timeout, std::uint32_t flag) {
    FakeState& state = Fakes();
    state.waits.push_back({static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(queue) -
                                                      reinterpret_cast<std::uintptr_t>(state.world)),
                           timeout, flag});
    if (static_cast<int>(state.waits.size()) - 1 == state.throwAtWait) {
        static FakeTimeout object;
        static FakeTimeout* thrown;
        thrown = &object;
        constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
        constexpr std::uintptr_t kTimeoutPointerThrowInfo = throw_info::kCTimeoutPointer;
        using ThrowFn = void(__stdcall*)(void* object, const void* info);
        reinterpret_cast<ThrowFn>(kCxxThrowException)(&thrown, reinterpret_cast<const void*>(kTimeoutPointerThrowInfo));
    }
    return 1;
}
inline void* const kFakeQueueVtable[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void*>(&FakeWait)};

// The critical section and event of the queues (not part of the compared bytes).
struct SharedQueueObjects {
    CRITICAL_SECTION lock;
    struct {
        std::uint32_t pad[2];
        HANDLE handle;  // +8, signalled by Push
    } event;
    SharedQueueObjects() {
        InitializeCriticalSection(&lock);
        event.pad[0] = event.pad[1] = 0;
        event.handle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }
};
inline SharedQueueObjects& Shared() {
    static SharedQueueObjects objects;
    return objects;
}

// Builds the dwords of the network -> logic messages of a case.
class MessageBuilder {
public:
    // A Command message: {1, id, size, 0} then the payload bytes (padded to dwords).
    MessageBuilder& Command(CommandId id, std::initializer_list<std::uint8_t> payload) {
        return Raw(Value(NetMessage::Command), Value(id), payload);
    }
    // Any message: {type, byte1, size, 0, payload...}. The size byte counts
    // the header and the payload rounded up to whole dwords.
    MessageBuilder& Raw(std::uint8_t type, std::uint8_t byte1, std::initializer_list<std::uint8_t> payload,
                        std::uint8_t byte3 = 0) {
        std::vector<std::uint8_t> bytes = {type, byte1, 0, byte3};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        while (bytes.size() % 4 != 0) {
            bytes.push_back(0);
        }
        bytes[2] = static_cast<std::uint8_t>(bytes.size());
        return Bytes(bytes);
    }
    // Bytes as they are (byte 2 must hold the size the original pops by).
    MessageBuilder& Bytes(const std::vector<std::uint8_t>& bytes) {
        for (std::size_t i = 0; i < bytes.size(); i += 4) {
            std::uint32_t dword = 0;
            for (std::size_t k = 0; k < 4 && i + k < bytes.size(); ++k) {
                dword |= static_cast<std::uint32_t>(bytes[i + k]) << (8 * k);
            }
            dwords_.push_back(dword);
        }
        return *this;
    }
    // The StepGrant message that ends the loop: {2, 0, 8, 0, step}.
    MessageBuilder& Grant(std::uint32_t step) {
        dwords_.push_back(Value(NetMessage::StepGrant) | 8u << 16);
        dwords_.push_back(step);
        return *this;
    }
    const std::vector<std::uint32_t>& dwords() const { return dwords_; }

private:
    std::vector<std::uint32_t> dwords_;
};

struct Outcome {
    std::uint32_t eax = 0;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
    std::vector<WaitCall> waits;
    std::uint32_t deletedTimeouts = 0;
    std::uint32_t deleteFlags = 0;
    std::string sideLog;  // Bench::afterSide
};

class Bench {
public:
    explicit Bench(test::Context& ctx) : worlds(ctx, kBufferBytes) { Shared(); }

    test::WorldPair worlds;
    test::CalleeStubs stubs;
    // S3.EXE globals the commands change (address, size): saved before the
    // original runs, restored before ours runs, and compared afterwards.
    std::vector<std::pair<std::uintptr_t, std::size_t>> globalRegions;
    // Called before each side runs (with that side's world), e.g. to clear a
    // test's own call log; and after each side, to collect that log into a
    // string that is compared.
    std::function<void(CGameData*)> beforeSide;
    std::function<std::string(CGameData*)> afterSide;

    CLogicThread* Thread(CGameData* world) {
        return reinterpret_cast<CLogicThread*>(reinterpret_cast<std::uint8_t*>(world) + kThreadOffset);  // raw-ok: the bench's objects after the world
    }
    MessageQueue* Queue(CGameData* world, std::size_t offset) {
        return reinterpret_cast<MessageQueue*>(reinterpret_cast<std::uint8_t*>(world) + offset);  // raw-ok: the bench's objects after the world
    }

    // Sets the thread's step (+0x3b0) on both sides.
    void SetStep(std::uint32_t step) {
        worlds.Both([&](CGameData* w) { Thread(w)->step = step; });
    }

    // Runs one case: `original` on side A, `candidate` on side B, with the
    // network -> logic queue holding `messages` (end them with Grant, or
    // set throwAtWait). Returns false (and reports) on a difference.
    bool Run(test::Context& ctx, int caseIndex, const MessageBuilder& messages, int throwAtWait = -1,
             ExecuteCommandsFn candidate = &CLogicThread_ExecuteCommands, int checkpoint = 16) {
        worlds.Both([&](CGameData* w) { Prepare(w, messages); });
        const std::vector<std::vector<std::uint8_t>> before = SaveGlobals();
        Outcome expected = RunSide(worlds.original(), Original_CLogicThread_ExecuteCommands, throwAtWait);
        const std::vector<std::vector<std::uint8_t>> globalsA = SaveGlobals();
        RestoreGlobals(before);
        Outcome actual = RunSide(worlds.ours(), candidate, throwAtWait);
        bool same = true;
        same &= Report(ctx, caseIndex, "globals", globalsA == SaveGlobals());
        same &= Report(ctx, caseIndex, "test log", expected.sideLog == actual.sideLog);
        if (expected.sideLog != actual.sideLog) {
            ctx.Fail(__FILE__, __LINE__, "  original: %s", expected.sideLog.c_str());
            ctx.Fail(__FILE__, __LINE__, "  ours:     %s", actual.sideLog.c_str());
        }
        same &= Report(ctx, caseIndex, "EAX", expected.eax == actual.eax);
        same &= Report(ctx, caseIndex, "stub calls", expected.calls == actual.calls);
        same &= Report(ctx, caseIndex, "DebugLog calls", expected.logs == actual.logs);
        same &= Report(ctx, caseIndex, "waits", expected.waits == actual.waits);
        same &= Report(ctx, caseIndex, "deleted timeouts",
                       expected.deletedTimeouts == actual.deletedTimeouts &&
                           expected.deleteFlags == actual.deleteFlags);
        if (!same) {
            ReportCalls(ctx, expected, actual);
            return false;
        }
        if (!SameFrameObjects(ctx, caseIndex)) {
            return false;
        }
        if (checkpoint > 0 && caseIndex % checkpoint == checkpoint - 1) {
            return Finish(ctx, caseIndex);
        }
        return true;
    }

    // Compares the whole worlds.
    bool Finish(test::Context& ctx, int caseIndex = -1) {
        if (!worlds.Same(ctx, 0, sizeof(CGameData))) {
            ctx.Fail(__FILE__, __LINE__, "  world differs (by case %d)", caseIndex);
            return false;
        }
        return true;
    }

private:
    void Prepare(CGameData* world, const MessageBuilder& messages) {
        CLogicThread* thread = Thread(world);
        thread->world = world;
        thread->fromNet = Queue(world, kFromNetOffset);
        thread->toNet = Queue(world, kToNetOffset);
        auto* fromRing = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(world) + kFromRingOffset);  // raw-ok: the bench's objects after the world
        auto* toRing = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(world) + kToRingOffset);  // raw-ok: the bench's objects after the world
        std::memset(fromRing, 0, kRingDwords * 4);
        std::memset(toRing, 0, kRingDwords * 4);
        const std::vector<std::uint32_t>& dwords = messages.dwords();
        std::memcpy(fromRing, dwords.data(), dwords.size() * 4);
        InitQueue(thread->fromNet, fromRing, static_cast<std::uint32_t>(dwords.size()));
        InitQueue(thread->toNet, toRing, 0);
    }

    static void InitQueue(MessageQueue* queue, std::uint32_t* ring, std::uint32_t count) {
        std::memset(queue, 0, sizeof(MessageQueue));
        queue->vtable = kFakeQueueVtable;
        queue->buffer = ring;
        queue->capacity = static_cast<std::uint32_t>(kRingDwords);
        queue->read = 0;
        queue->write = count;
        queue->count = static_cast<std::int32_t>(count);
        queue->event = &Shared().event;
        queue->lock = &Shared().lock;
        queue->peak = 0;
    }

    std::vector<std::vector<std::uint8_t>> SaveGlobals() const {
        std::vector<std::vector<std::uint8_t>> saved;
        for (const auto& region : globalRegions) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(region.first);
            saved.emplace_back(bytes, bytes + region.second);
        }
        return saved;
    }
    void RestoreGlobals(const std::vector<std::vector<std::uint8_t>>& saved) const {
        for (std::size_t i = 0; i < saved.size(); ++i) {
            std::memcpy(reinterpret_cast<void*>(globalRegions[i].first), saved[i].data(), saved[i].size());
        }
    }

    // The 0x21 replies ExecuteCommands pushes for the network ({0x21, id, 8,
    // ?, player, ?, ?, ?}: commands 0xf7, 0xfa, network messages 3 and 5)
    // leave bytes 3 and 5..7 as the original's uninitialized stack
    // ([ebp-0x424]); they are zeroed on both sides before the rings are
    // compared.
    static void MaskReplyBytes(CGameData* world) {
        auto* ring = reinterpret_cast<std::uint8_t*>(world) + kToRingOffset;  // raw-ok: the bench's objects after the world
        const auto* queue =
            reinterpret_cast<const MessageQueue*>(reinterpret_cast<std::uint8_t*>(world) + kToNetOffset);  // raw-ok: the bench's objects after the world
        std::uint32_t at = 0;
        while (at < queue->write && at < kRingDwords) {
            std::uint8_t* message = ring + at * 4;
            const std::uint32_t dwords = message[2] >> 2;
            if (message[0] == Value(NetMessage::CommandRequest) && message[2] == 8) {
                message[3] = 0;
                message[5] = message[6] = message[7] = 0;
            }
            at += dwords == 0 ? 1 : dwords;
        }
    }

    Outcome RunSide(CGameData* world, ExecuteCommandsFn function, int throwAtWait) {
        FakeState& state = Fakes();
        state.world = world;
        state.waits.clear();
        state.throwAtWait = throwAtWait;
        state.deletedTimeouts = 0;
        state.deleteFlags = 0;
        stubs.SetWorld(world);
        if (beforeSide) {
            beforeSide(world);
        }
        Outcome out;
        {
            DebugLogRecorder recorder;
            out.eax = function(Thread(world), nullptr);
            out.logs = recorder.Take();
        }
        out.calls = stubs.Take();
        out.waits = state.waits;
        out.deletedTimeouts = state.deletedTimeouts;
        out.deleteFlags = state.deleteFlags;
        state.throwAtWait = -1;
        if (afterSide) {
            out.sideLog = afterSide(world);
        }
        MaskReplyBytes(world);
        return out;
    }

    bool Report(test::Context& ctx, int caseIndex, const char* what, bool same) {
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %s differ", caseIndex, what);
        }
        return same;
    }

    static void ReportCalls(test::Context& ctx, const Outcome& a, const Outcome& b) {
        ctx.Fail(__FILE__, __LINE__, "  EAX %u / %u; stub calls %zu / %zu; logs %zu / %zu; waits %zu / %zu",
                 a.eax, b.eax, a.calls.size(), b.calls.size(), a.logs.size(), b.logs.size(), a.waits.size(),
                 b.waits.size());
        const std::size_t n = a.calls.size() > b.calls.size() ? a.calls.size() : b.calls.size();
        for (std::size_t i = 0; i < n && i < 12; ++i) {
            const test::StubCall* x = i < a.calls.size() ? &a.calls[i] : nullptr;
            const test::StubCall* y = i < b.calls.size() ? &b.calls[i] : nullptr;
            ctx.Fail(__FILE__, __LINE__, "    call %zu: %08x(%x, %x, %x, %x) / %08x(%x, %x, %x, %x)", i,
                     x ? static_cast<unsigned>(x->callee) : 0u, x ? x->args[0] : 0u, x ? x->args[1] : 0u,
                     x ? x->args[2] : 0u, x ? x->args[3] : 0u, y ? static_cast<unsigned>(y->callee) : 0u,
                     y ? y->args[0] : 0u, y ? y->args[1] : 0u, y ? y->args[2] : 0u, y ? y->args[3] : 0u);
        }
    }

    // The thread (without its pointers to the world and the queues), the
    // queues (without the ring pointer) and both rings.
    bool SameFrameObjects(test::Context& ctx, int caseIndex) {
        bool same = worlds.Same(ctx, kThreadOffset, offsetof(CLogicThread, world)) &&
                    worlds.Same(ctx, kThreadOffset + offsetof(CLogicThread, unk3a4),
                                offsetof(CLogicThread, toNet) - offsetof(CLogicThread, unk3a4)) &&
                    worlds.Same(ctx, kThreadOffset + offsetof(CLogicThread, draw_slot),
                                kThreadBytes - offsetof(CLogicThread, draw_slot));
        for (const std::size_t queue : {kFromNetOffset, kToNetOffset}) {
            same = same && worlds.Same(ctx, queue + offsetof(MessageQueue, capacity),
                                       sizeof(MessageQueue) - offsetof(MessageQueue, capacity));
        }
        same = same && worlds.Same(ctx, kFromRingOffset, 2 * kRingDwords * 4);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: thread, queues or rings differ", caseIndex);
        }
        return same;
    }
};

}  // namespace commands::bench

}  // namespace s3re::game
