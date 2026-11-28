// MessageQueue: the ring buffer of dwords between the logic thread and the
// network thread (logic_thread_layout.h; re/commands_lockstep.md). Both ends
// lock the queue's critical section (+0x2c, Win32::CCriticalSection).
//
// MessageQueue::Pop (0x00430980): thiscall (): under the lock the read index
//   moves on by one (wrapping with capacity - 1) and the count drops by one;
//   after the lock is left, EAX = buffer + the old read index (the address of
//   the dword taken; the caller reads it, so a message's dwords are popped
//   one by one).
// MessageQueue::Push (0x004309c0): thiscall (const dword* dwords, count,
//   bool signal) ret 0Ch: under the lock: when count + the queued dwords
//   exceed the capacity (signed), the queue overflows: `throw new
//   CEventQueueOverflow(string(""), 1)` (operator new 0x18 bytes, the
//   constructor 0x430410, _CxxThrowException with the throw info 0x66ed88;
//   a null object is thrown as null), and the lock stays entered. Otherwise
//   the dwords go into the ring (write index wrapping), the count grows, the
//   peak (+0x30) is the larger of it and the count (signed). With `signal`
//   (its low byte) the inlined Win32::CEvent::Set of the event (+0x28, its
//   handle at +8): SetEvent, and on failure VERIFY's
//   Win32::ThrowWin32Error("SetEvent (hHandle)", "Win32\synchronize.ipp",
//   271, GetLastError(), location). Then the lock is left. EAX is not used
//   by the callers.
//
// Strings of the throws: the overflow's text and VERIFY's location are ""
// (0x7a7868) built through temporaries, exactly as the original (no
// allocation; the bytes are only on the stack). The allocator bytes: the
// overflow's three strings get the low byte of the new object's address
// (the compiler reused the stack slot of the `signal` argument for it), the
// location strings the `signal` byte itself.

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/logic/message_queue.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using msvc6::String;

constexpr std::uintptr_t kEnter = 0x0060d670;             // Win32::CCriticalSection::Enter
constexpr std::uintptr_t kLeave = 0x0060d680;             // Win32::CCriticalSection::Leave
constexpr std::uintptr_t kOverflowConstruct = 0x00430410;  // CEventQueueOverflow::CEventQueueOverflow
constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;

constexpr std::uint32_t kOverflowBytes = 0x18;
constexpr std::uint32_t kOverflowFlag = 1;         // the constructor's second argument
constexpr std::uint32_t kLineSetEvent = 271;        // Win32\synchronize.ipp
constexpr std::uint32_t kStringNpos = 0xFFFFFFFFu;  // the literal -1 of the temporaries

using SectionFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using OverflowConstructFn = void*(__fastcall*)(void* self, void* edx, const String* text, std::uint32_t flag);
using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* location);
using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);

void Enter(const MessageQueue* queue) {
    reinterpret_cast<SectionFn>(kEnter)(queue->lock, nullptr);
}

void Leave(const MessageQueue* queue) {
    reinterpret_cast<SectionFn>(kLeave)(queue->lock, nullptr);
}

// The empty string of the compiler: allocator byte, _Tidy(false).
void MakeEmpty(String* s, std::uint8_t allocator) {
    s->allocator = allocator;
    msvc6::String_Tidy(s, nullptr, 0);
}

// The queue is full: throw new CEventQueueOverflow(string(""), 1).
[[noreturn]] void ThrowOverflow() {
    void* object = msvc6::GameOperatorNew(kOverflowBytes);
    if (object != nullptr) {
        const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(object));
        String text;
        MakeEmpty(&text, allocator);
        String fromText;
        MakeEmpty(&fromText, allocator);
        const char* empty = texts::kEmpty.c_str();
        msvc6::String_AssignChars(&fromText, nullptr, empty, static_cast<std::uint32_t>(std::strlen(empty)));
        String copy;
        MakeEmpty(&copy, allocator);
        msvc6::String_AssignSubstr(&copy, nullptr, &fromText, 0, kStringNpos);
        msvc6::String_AssignSubstr(&text, nullptr, &copy, 0, msvc6::detail::Npos());
        msvc6::String_Tidy(&copy, nullptr, 1);
        msvc6::String_Tidy(&fromText, nullptr, 1);
        reinterpret_cast<OverflowConstructFn>(kOverflowConstruct)(object, nullptr, &text, kOverflowFlag);
    }
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&object, throw_info::kCEventQueueOverflowPointer);
    __assume(0);
}

// The inlined Win32::CEvent::Set: VERIFY(SetEvent(hHandle)).
void SetEventVerified(void* event, std::uint8_t signal) {
    const auto* handle = reinterpret_cast<const framework::CWin32Handle*>(static_cast<std::uint8_t*>(event) + 4);
    if (g_importSetEvent.get()(reinterpret_cast<void*>(handle->handle)) != 0) {
        return;
    }
    String location;
    MakeEmpty(&location, signal);
    String fromText;
    MakeEmpty(&fromText, signal);
    const char* empty = texts::kEmpty.c_str();
    msvc6::String_AssignChars(&fromText, nullptr, empty, static_cast<std::uint32_t>(std::strlen(empty)));
    String copy;
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, kStringNpos, &signal);
    msvc6::String_AssignSubstr(&location, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
    const std::uint32_t error = g_importGetLastError.get()();
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(texts::kVerifySetEvent.c_str(),
                                                          texts::kSynchronizeIpp.c_str(), kLineSetEvent, error,
                                                          &location);
    // Not reached: ThrowWin32Error throws (the original destroys the
    // location after it).
}

}  // namespace

using PopFn = std::uint32_t*(__fastcall*)(MessageQueue* self, void* edx);
using PushFn = void(__fastcall*)(MessageQueue* self, void* edx, const std::uint32_t* dwords, std::int32_t count,
                                 std::uint32_t signal);

S3RE_HOOK(0x00430980, MessageQueue_Pop, PopFn, Original_MessageQueue_Pop, MessageQueue_Pop);
// Push: no hook here; the command recorder's S3RE_HOOK_ALWAYS on 0x4309c0
// (tooling/replay/command_log.cpp) calls MessageQueue_Push outside original
// mode.

// Purpose: takes the next dword out of a logic/network message queue (the
// caller reads it through the returned address).
std::uint32_t* __fastcall MessageQueue_Pop(MessageQueue* self, void* /*edx*/) {
    Enter(self);
    const std::uint32_t index = self->read;
    self->read = (self->capacity - 1) & (index + 1);
    --self->count;
    Leave(self);
    return self->buffer + index;
}

// Purpose: appends a message (a few dwords) to a logic/network message
// queue and, if asked, wakes the thread waiting for it.
void __fastcall MessageQueue_Push(MessageQueue* self, void* /*edx*/, const std::uint32_t* dwords, std::int32_t count,
                                  std::uint32_t signal) {
    Enter(self);
    if (self->count + count > static_cast<std::int32_t>(self->capacity)) {
        ThrowOverflow();
    }
    for (std::int32_t i = 0; i < count; ++i) {
        self->buffer[self->write] = dwords[i];
        self->write = (self->capacity - 1) & (self->write + 1);
    }
    self->count += count;
    if (self->peak > self->count) {
        // (the peak stays)
    } else {
        self->peak = self->count;
    }
    const auto signalByte = static_cast<std::uint8_t>(signal);
    if (signalByte != 0) {
        SetEventVerified(self->event, signalByte);
    }
    Leave(self);
}

// ---------------------------------------------------------------------------
// Tests: two identical queues (random indexes, counts and peaks, a ring of
// 2^k dwords with random contents), each with its own real critical section
// and an event object whose handle is a real event; random sequences of
// pushes (never overflowing; the overflow and the SetEvent failure throw
// and are covered by the emulator specs) and pops. Compared: the queue
// fields, the ring, the popped addresses (relative to the ring) and whether
// the event got signalled.
// ---------------------------------------------------------------------------

namespace {

struct QueueSide {
    MessageQueue queue{};
    std::vector<std::uint32_t> ring;
    CRITICAL_SECTION section{};
    std::uint8_t event[12] = {};  // CEvent: vtable, then the CWin32Handle at +4 (handle at +8)
    HANDLE handle = nullptr;

    QueueSide() {
        InitializeCriticalSection(&section);
        handle = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }
    ~QueueSide() {
        DeleteCriticalSection(&section);
        CloseHandle(handle);
    }
    QueueSide(const QueueSide&) = delete;
    QueueSide& operator=(const QueueSide&) = delete;

    void Reset(const MessageQueue& image, const std::vector<std::uint32_t>& contents) {
        ring = contents;
        queue = image;
        queue.buffer = ring.data();
        queue.lock = &section;
        const auto value = reinterpret_cast<std::uint32_t>(handle);
        std::memcpy(event + 8, &value, 4);
        queue.event = event;
        ResetEvent(handle);
    }
    bool Signalled() const { return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0; }
};

// The original Push: the trampoline of the recorder's hook.
PushFn OriginalPush() {
    std::uintptr_t address = 0;
    const void* original = nullptr;
    if (!hooks::FindHook("MessageQueue_Push", &address, &original)) {
        return reinterpret_cast<PushFn>(0x004309c0);
    }
    return reinterpret_cast<PushFn>(const_cast<void*>(original));
}

void CompareQueues(test::Context& ctx, PopFn pop, PushFn push, int sequences) {
    const PushFn originalPush = OriginalPush();
    QueueSide a, b;
    for (int sequence = 0; sequence < sequences; ++sequence) {
        const std::uint32_t capacity = 1u << (2 + ctx.rng().Below(6));
        MessageQueue image{};
        ctx.rng().Fill(&image, sizeof(image));
        image.capacity = capacity;
        image.read = ctx.rng().Below(capacity);
        image.count = static_cast<std::int32_t>(ctx.rng().Below(capacity + 1));
        image.write = (image.read + static_cast<std::uint32_t>(image.count)) & (capacity - 1);
        if (ctx.rng().Chance(1, 4)) {
            image.write = ctx.rng().Below(capacity);  // inconsistent indexes are copied all the same
        }
        image.peak = ctx.rng().Chance(1, 4) ? -1 - static_cast<std::int32_t>(ctx.rng().Below(4))
                                            : static_cast<std::int32_t>(ctx.rng().Below(capacity + 4));
        std::vector<std::uint32_t> contents(capacity);
        for (auto& word : contents) {
            word = ctx.rng().Next32();
        }
        a.Reset(image, contents);
        b.Reset(image, contents);
        for (int step = 0; step < 30; ++step) {
            std::uint32_t poppedA = 0, poppedB = 0;
            std::uint32_t words[16];
            ctx.rng().Fill(words, sizeof(words));
            const std::int32_t room = static_cast<std::int32_t>(capacity) - a.queue.count;
            const bool doPush = ctx.rng().Chance(1, 2) && room >= 0;
            std::int32_t count = 0;
            std::uint32_t signal = 0;
            if (doPush) {
                count = ctx.rng().Chance(1, 8) ? -static_cast<std::int32_t>(ctx.rng().Below(3))
                                               : static_cast<std::int32_t>(ctx.rng().Below(
                                                     static_cast<std::uint32_t>(room < 16 ? room : 16) + 1));
                signal = (ctx.rng().Next32() & 0xFFFFFF00u) | (ctx.rng().Chance(1, 2) ? ctx.rng().Below(256) : 0);
                originalPush(&a.queue, nullptr, words, count, signal);
                push(&b.queue, nullptr, words, count, signal);
            } else {
                poppedA = static_cast<std::uint32_t>(Original_MessageQueue_Pop(&a.queue, nullptr) - a.ring.data());
                poppedB = static_cast<std::uint32_t>(pop(&b.queue, nullptr) - b.ring.data());
            }
            bool same = S3RE_EXPECT_EQ(poppedA, poppedB);
            same &= S3RE_EXPECT_EQ(a.queue.read, b.queue.read);
            same &= S3RE_EXPECT_EQ(a.queue.write, b.queue.write);
            same &= S3RE_EXPECT_EQ(a.queue.count, b.queue.count);
            same &= S3RE_EXPECT_EQ(a.queue.peak, b.queue.peak);
            same &= S3RE_EXPECT_EQ(a.queue.capacity, b.queue.capacity);
            same &= S3RE_EXPECT_MEM_EQ(a.queue.unk04, b.queue.unk04, sizeof(a.queue.unk04));
            same &= S3RE_EXPECT(a.ring == b.ring);
            same &= S3RE_EXPECT_EQ(a.Signalled(), b.Signalled());
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d, step %d: %s count %d signal 0x%x", sequence, step,
                         doPush ? "push" : "pop", count, signal);
                return;
            }
            ResetEvent(a.handle);
            ResetEvent(b.handle);
        }
    }
}

// Wrong: the peak is compared unsigned.
void __fastcall WrongPush(MessageQueue* self, void* edx, const std::uint32_t* dwords, std::int32_t count,
                          std::uint32_t signal) {
    const std::int32_t peak = self->peak;
    MessageQueue_Push(self, edx, dwords, count, signal);
    if (static_cast<std::uint32_t>(peak) > static_cast<std::uint32_t>(self->count)) {
        self->peak = peak;
    }
}

// Wrong: the read index does not wrap.
std::uint32_t* __fastcall WrongPop(MessageQueue* self, void* edx) {
    std::uint32_t* result = MessageQueue_Pop(self, edx);
    if (self->read == 0) {
        self->read = self->capacity;
    }
    return result;
}

}  // namespace

S3RE_TEST(MessageQueue_MatchOriginal) {
    std::uintptr_t address = 0;
    const void* original = nullptr;
    S3RE_EXPECT(hooks::FindHook("MessageQueue_Push", &address, &original) && address == 0x004309c0);
    CompareQueues(ctx, &MessageQueue_Pop, &MessageQueue_Push, 400);
}

S3RE_TEST(MessageQueue_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareQueues(probe, &MessageQueue_Pop, &WrongPush, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareQueues(probe, &WrongPop, &MessageQueue_Push, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
