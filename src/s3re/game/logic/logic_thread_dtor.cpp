// CLogicThread's destructors (Volker\LogicInit.cpp). Like every game
// thread (win32_thread_destructors.cpp) the class has its CWin32Handle
// virtual base at +4, so ECX ("self") is the object + 4; offsets in the
// comments are relative to the full object (logic_thread_layout.h).
//
// CLogicThread::~CLogicThread (0x0053c7d0): vtables 0x65a14c / 0x65a140;
//   the timer (+0x3bc; its +4 part's slot 0 with 1, a deleting destructor)
//   and the world (+0x3a0, its slot 0 with 1) if set; the 20 message lines
//   (+0x414, 0x1c bytes each) through the CRT's `eh vector destructor
//   iterator` 0x61f18a with their destructor 0x409a50; the chat input's
//   std::wstring (+0x3f8) inlined: a shared buffer (refcount byte before
//   the characters not 0 or 0xff) loses a reference, otherwise it is freed
//   (game operator delete of the characters - 2), then pointer, length and
//   capacity 0; the waitable bag at +0x3dc (Win32::CWin32WaitableBag,
//   vtables 0x65733c and 0x65732c on its list at +0x3e0): while the list
//   has entries, get(0) 0x60b500, the list's slot 1 (destroy the link) and
//   the waitable's deleting destructor (its +4 part, slot 0 with 1);
//   clearAndDestroy 0x60b560; the list base classes (vtable 0x657430: if
//   it still has entries, NoOp 0x59f200 and clearAndDestroy; vtable
//   0x657424, ~WCIsvListBase 0x60b5a0), ~CObject 0x610330 on the bag; the
//   16 std::strings at +0x250 through the same iterator (~string
//   0x401b60); then ~CGameThread 0x5b60a0.
//   When a step throws, the original's unwinding destroys the remaining
//   members; ours doesn't model that (no destructor here throws in the
//   game; the tests and specs stub the callees).
// CLogicThread::scalar_deleting_destructor (0x0053c7a0): (flags) ret 4:
//   ~CLogicThread, with bit 0 operator delete(self - 4); EAX = self - 4.

#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kVectorDestructorIterator = 0x0061f18a;  // CRT `eh vector destructor iterator` (stdcall)
constexpr std::uintptr_t kMessageLineDestroy = 0x00409a50;
constexpr std::uintptr_t kStringDestroy = 0x00401b60;              // std::string::~string
constexpr std::uintptr_t kListGet = 0x0060b500;                    // WCIsvSListBase::get(index)
constexpr std::uintptr_t kListClearAndDestroy = 0x0060b560;
constexpr std::uintptr_t kNoOp = 0x0059f200;
constexpr std::uintptr_t kIsvListBaseDestroy = 0x0060b5a0;
constexpr std::uintptr_t kObjectDestroy = 0x00610330;
constexpr std::uintptr_t kGameThreadDestroy = 0x005b60a0;
constexpr std::uintptr_t kLogicThreadDestroy = 0x0053c7d0;
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;

constexpr std::uint32_t kMessageLines = 20;
constexpr std::uint32_t kMessageLineBytes = 0x1c;
constexpr std::uint32_t kStatusStrings = 16;
constexpr std::uint32_t kStringBytes = 0x10;
constexpr std::uint32_t kListSlotDestroyLink = 1;

// Offsets relative to self (= the object + 4).
constexpr std::uint32_t kWorld = 0x39c;
constexpr std::uint32_t kTimer = 0x3b8;
constexpr std::uint32_t kWaitableBag = 0x3d8;
constexpr std::uint32_t kChatText = 0x3f4;     // the std::wstring (allocator byte, then ptr, length, capacity)
constexpr std::uint32_t kMessages = 0x410;
constexpr std::uint32_t kStatusTexts = 0x24c;

std::uint8_t* At(void* self, std::uint32_t offset) {
    return static_cast<std::uint8_t*>(self) + offset;
}
std::uint32_t& Dword(void* self, std::uint32_t offset) {
    return *reinterpret_cast<std::uint32_t*>(At(self, offset));
}

template <typename... Args>
std::uint32_t CallThis(std::uintptr_t address, std::uint32_t self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

std::uint32_t AddressOf(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

// Virtual slot `slot` of the object at `address`.
template <typename... Args>
std::uint32_t CallVirtual(std::uint32_t address, std::uint32_t slot, Args... args) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(static_cast<std::uintptr_t>(address));
    return CallThis(vtable[slot], address, args...);
}

void DestroyArray(void* first, std::uint32_t size, std::uint32_t count, std::uintptr_t destructor) {
    using Fn = void(__stdcall*)(void*, std::uint32_t, std::uint32_t, std::uintptr_t);
    reinterpret_cast<Fn>(kVectorDestructorIterator)(first, size, count, destructor);
}

void GameDelete(std::uint32_t address) {
    using Fn = void(__cdecl*)(std::uint32_t);
    reinterpret_cast<Fn>(kOperatorDelete)(address);
}

// The chat input's std::wstring, destroyed as the original inlines it.
void DestroyChatText(void* self) {
    std::uint8_t* const chars = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(Dword(self, kChatText + 4)));
    if (chars != nullptr) {
        const std::uint8_t references = chars[-1];
        if (references != 0 && references != 0xff) {
            chars[-1] = static_cast<std::uint8_t>(references - 1);
        } else {
            GameDelete(AddressOf(chars - 2));
        }
    }
    Dword(self, kChatText + 4) = 0;
    Dword(self, kChatText + 8) = 0;
    Dword(self, kChatText + 12) = 0;
}

// The waitable bag at +0x3dc and its list at +0x3e0.
void DestroyWaitableBag(void* self) {
    const std::uint32_t bag = AddressOf(At(self, kWaitableBag));
    const std::uint32_t list = bag + 4;
    Dword(self, kWaitableBag) = vtables::kCWin32WaitableBag;
    Dword(self, kWaitableBag + 4) = vtables::kWaitablePtrSList;
    while (Dword(self, kWaitableBag + 4 + 8) != 0) {
        const std::uint32_t link = CallThis(kListGet, list, 0);
        if (link == 0) {
            continue;
        }
        const std::uint32_t waitable = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(link + 4));
        CallVirtual(list, kListSlotDestroyLink, link);
        if (waitable != 0) {
            CallVirtual(waitable + 4, 0, 1);
        }
    }
    CallThis(kListClearAndDestroy, list);
    const std::uint32_t base = bag != 0 ? list : 0;  // (the compiler's null-checked adjustment)
    *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(base)) = vtables::kWaitableValListBase;
    if (*reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(base + 8)) != 0) {
        CallThis(kNoOp, base);
        CallThis(kListClearAndDestroy, base);
    }
    *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(base)) = vtables::kWaitableIsvSListBase;
    CallThis(kIsvListBaseDestroy, base);
    CallThis(kObjectDestroy, bag);
}

}  // namespace

using DestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DeletingDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t flags);

// Purpose: Destroys the logic thread when the game is left: the timer, the
// world, the message lines, the chat input, the list of objects it waits
// for and the status texts, then the game thread.
std::uint32_t __fastcall CLogicThread_Destroy(void* self, void*) {
    Dword(self, static_cast<std::uint32_t>(-4)) = vtables::kCLogicThread;
    Dword(self, 0) = vtables::kCLogicThread_sub4;
    if (Dword(self, kTimer) != 0) {
        CallVirtual(Dword(self, kTimer) + 4, 0, 1);
    }
    if (Dword(self, kWorld) != 0) {
        CallVirtual(Dword(self, kWorld), 0, 1);
    }
    DestroyArray(At(self, kMessages), kMessageLineBytes, kMessageLines, kMessageLineDestroy);
    DestroyChatText(self);
    DestroyWaitableBag(self);
    DestroyArray(At(self, kStatusTexts), kStringBytes, kStatusStrings, kStringDestroy);
    return CallThis(kGameThreadDestroy, AddressOf(self));
}

// Purpose: The logic thread's deleting destructor (delete through the
// vtable).
std::uint32_t __fastcall CLogicThread_DeletingDestroy(void* self, void*, std::uint32_t flags) {
    CallThis(kLogicThreadDestroy, AddressOf(self));
    const std::uint32_t object = AddressOf(self) - 4;
    if ((flags & 1) != 0) {
        GameDelete(object);
    }
    return object;
}

S3RE_HOOK(0x0053c7d0, CLogicThread_Destroy, DestroyFn, Original_CLogicThread_Destroy, CLogicThread_Destroy);
S3RE_HOOK(0x0053c7a0, CLogicThread_DeletingDestroy, DeletingDestroyFn, Original_CLogicThread_DeletingDestroy,
          CLogicThread_DeletingDestroy);


// ---------------------------------------------------------------------------
// Tests: random thread images in one static buffer (both runs use the same
// addresses), with fake objects after the thread: the timer, the world and
// two waitables have a recording vtable; the vector destructor iterator,
// operator delete and ~CGameThread (for the deleting destructor:
// ~CLogicThread) are recording fakes; get(0) is a stub that hands out up
// to three links in the buffer and then empties the list; the list's slot
// 1 (WCValListBase::base_destroy_link 0x5b7ff0, reached through the game's
// vtable) and the other list and object calls are recording stubs.
// Compared: the image, the calls in order, EAX of the deleting destructor.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kImageBytes = 0x740;
constexpr std::uint32_t kFakeObjects = 0x680;  // 4 fake objects of 0x20 bytes after the thread
constexpr std::uint32_t kLinks = 0x700;        // 2 links of 8 bytes
constexpr std::uint32_t kChatChars = 0x722;

alignas(16) std::uint8_t g_image[kImageBytes];

struct Recorder {
    std::vector<std::uint32_t> log;
    std::uint32_t links = 0;
};
Recorder* g_recorder = nullptr;

void Log(std::initializer_list<std::uint32_t> values) {
    g_recorder->log.insert(g_recorder->log.end(), values);
}
void __stdcall FakeIterator(void* first, std::uint32_t size, std::uint32_t count, std::uint32_t destructor) {
    Log({0x17e4u, AddressOf(first), size, count, destructor});
}
void __cdecl FakeDelete(std::uint32_t address) {
    Log({0xde1u, address});
}
std::uint32_t __fastcall FakeBase(void* self, void*) {
    Log({0xba5eu, AddressOf(self)});
    return 0x77;
}
std::uint32_t __fastcall FakeSlot(void* self, void*, std::uint32_t argument) {
    Log({0x510u, AddressOf(self), argument});
    return 0;
}
void* g_fakeVtable[4] = {reinterpret_cast<void*>(&FakeSlot), reinterpret_cast<void*>(&FakeSlot),
                         reinterpret_cast<void*>(&FakeSlot), reinterpret_cast<void*>(&FakeSlot)};

struct Case {
    std::vector<std::uint8_t> start;
    std::uint32_t pointers;
    std::uint32_t links;
    std::uint32_t flags;
    bool chat;
};

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint8_t> image;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
};

void Put(std::uint32_t at, std::uint32_t value) {
    std::memcpy(g_image + at, &value, 4);
}

Outcome Run(const Case& c, bool deleting, DestroyFn destroy, DeletingDestroyFn deletingDestroy) {
    Outcome out;
    std::memcpy(g_image, c.start.data(), kImageBytes);
    const std::uint32_t base = AddressOf(g_image);
    const std::uint32_t table = AddressOf(g_fakeVtable);
    for (std::uint32_t k = 0; k < 4; ++k) {
        Put(kFakeObjects + 0x20 * k, table);
        Put(kFakeObjects + 0x20 * k + 4, table);
    }
    Put(4 + kTimer, (c.pointers & 1) != 0 ? base + kFakeObjects : 0);
    Put(4 + kWorld, (c.pointers & 2) != 0 ? base + kFakeObjects + 0x20 : 0);
    Put(4 + kChatText + 4, c.chat ? base + kChatChars : 0);
    Recorder recorder;
    recorder.links = c.links;
    g_recorder = &recorder;
    test::PatchJump(kOperatorDelete, reinterpret_cast<const void*>(&FakeDelete));
    test::PatchJump(kVectorDestructorIterator, reinterpret_cast<const void*>(&FakeIterator));
    test::PatchJump(deleting ? kLogicThreadDestroy : kGameThreadDestroy, reinterpret_cast<const void*>(&FakeBase));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(nullptr);
        stubs.Install<1, kListGet>([base](CGameData*, std::uint32_t self, const std::uint32_t*) -> std::uint32_t {
            Recorder& r = *g_recorder;
            if (r.links == 0) {
                std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(self + 8)), 0, 4);  // empty now
                return 0;
            }
            --r.links;
            const std::uint32_t slot = r.links & 1;
            const std::uint32_t waitable = r.links == 2 ? 0 : base + kFakeObjects + 0x40 + 0x20 * slot;
            Put(kLinks + 8 * slot + 4, waitable);
            return base + kLinks + 8 * slot;
        });
        stubs.Install<0, kListClearAndDestroy>();
        stubs.Install<0, kNoOp>();
        stubs.Install<0, kIsvListBaseDestroy>();
        stubs.Install<0, kObjectDestroy>();
        stubs.Install<1, 0x005b7ff0>();  // WCValListBase::base_destroy_link (the list's slot 1)
        void* const self = g_image + 4;
        out.eax = deleting ? deletingDestroy(self, nullptr, c.flags) : destroy(self, nullptr);
        out.calls = stubs.Take();
    }
    test::UnpatchJump(deleting ? kLogicThreadDestroy : kGameThreadDestroy);
    test::UnpatchJump(kVectorDestructorIterator);
    test::UnpatchJump(kOperatorDelete);
    out.image.assign(g_image, g_image + kImageBytes);
    out.log = recorder.log;
    g_recorder = nullptr;
    return out;
}

void CompareLogicThreadDestructors(test::Context& ctx, bool deleting, DestroyFn destroy,
                                   DeletingDestroyFn deletingDestroy, int cases) {
    for (int i = 0; i < cases; ++i) {
        Case c;
        c.start.resize(kImageBytes);
        ctx.rng().Fill(c.start.data(), kImageBytes);
        c.pointers = ctx.rng().Below(4);
        c.links = ctx.rng().Below(4);
        c.flags = ctx.rng().Below(4);
        c.chat = ctx.rng().Chance(2, 3);
        c.start[kChatChars - 1] = static_cast<std::uint8_t>(
            ctx.rng().Chance(1, 3) ? 0 : (ctx.rng().Chance(1, 4) ? 0xff : ctx.rng().Below(5)));
        // The list's entry count (+8 of the list at the object + 0x3e0).
        const std::uint32_t entries = c.links != 0 || ctx.rng().Chance(1, 2) ? 1 : 0;
        std::memcpy(c.start.data() + 4 + kWaitableBag + 4 + 8, &entries, 4);
        const Outcome expected = Run(c, deleting, Original_CLogicThread_Destroy, Original_CLogicThread_DeletingDestroy);
        const Outcome actual = Run(c, deleting, destroy, deletingDestroy);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT(expected.image == actual.image);
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%s, pointers %u, links %u)", i, deleting ? "deleting" : "dtor",
                     c.pointers, c.links);
            return;
        }
    }
}

// Wrong: the chat text's capacity is not cleared.
std::uint32_t __fastcall WrongDestroy(void* self, void* edx) {
    const std::uint32_t capacity = Dword(self, kChatText + 12);
    const std::uint32_t eax = CLogicThread_Destroy(self, edx);
    Dword(self, kChatText + 12) = capacity;
    return eax;
}

}  // namespace

S3RE_TEST(CLogicThread_Destructors_MatchOriginal) {
    CompareLogicThreadDestructors(ctx, false, &CLogicThread_Destroy, &CLogicThread_DeletingDestroy, 500);
    CompareLogicThreadDestructors(ctx, true, &CLogicThread_Destroy, &CLogicThread_DeletingDestroy, 200);
}

S3RE_TEST(CLogicThread_Destructors_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLogicThreadDestructors(probe, false, &WrongDestroy, &CLogicThread_DeletingDestroy, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
