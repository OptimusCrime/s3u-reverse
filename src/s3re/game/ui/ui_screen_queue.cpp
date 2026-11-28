// The message queue of a CScreen and the modal stack of a CScreenGroup
// (Dirk\dialog.cpp, wip/dialog-r8). All thiscall; layouts in
// re/ui_classes.md. The queues are Watcom intrusive lists: get(0)
// (WCIsvSListBase 0x60b500) unlinks and returns the first link, the list's
// own vtable slot 1 deletes it.
//
// CScreen::ProcessQueuedMessage (0x00417df0; this = the screen): if
//   HasQueuedMessages 0x417f50: the first message (20 bytes at link + 4)
//   of the queue at +0x7c taken out (the link deleted) and handed to
//   CScreenElement::OnMessage 0x415850 of the element part (vbptr +4). A
//   null link (impossible after HasQueuedMessages) would hand over an
//   uninitialized message. EAX = OnMessage's, else HasQueuedMessages'.
// CScreen::OnMessage (0x00417e70; slot 2 of the element part, this - 0x98
//   = the screen): (message by value) ret 0x14: first every queued
//   message (as above; queue at this - 0x1c; a null link hands over the
//   argument), then the argument itself, each through OnMessage 0x415850
//   of the element (vbptr this - 0x94). EAX = the last OnMessage's.
// CScreenGroup::PopModal (0x00417a30; this = the group): (child) ret 4:
//   restores what PushModal 0x417920 saved, each from the front of its
//   list: the captured child (+0x30 = the link's value, or 0 when the list
//   at +0x34 is empty); from the bool list (+0x64): when set, the group's
//   own slot 1 (RemoveChild) with `child`; from the list at +0x4c: the
//   saved focus element, whose slot 3 (SetFocus) gets 0. EAX = the last
//   call's (SetFocus's; the delete's when the saved focus is null; 0 when
//   the list was empty).

#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kHasQueuedMessages = 0x00417f50;  // CScreen::HasQueuedMessages (ours)
constexpr std::uintptr_t kListGet = 0x0060b500;            // WCIsvSListBase::get(index)
constexpr std::uintptr_t kElementOnMessage = 0x00415850;   // CScreenElement::OnMessage (ours)

constexpr std::uint32_t kListDeleteLink = 0x04;  // the list's own vtable slot 1

namespace off {
constexpr std::int32_t kScreenQueue = 0x7c;              // from the screen
constexpr std::int32_t kElementToScreen = -0x98;         // CScreen's element part -> the screen
constexpr std::int32_t kGroupCaptured = 0x30;
constexpr std::int32_t kGroupSavedCaptured = 0x34;
constexpr std::int32_t kGroupSavedFocus = 0x4c;
constexpr std::int32_t kGroupSavedAdded = 0x64;          // bools: the child was added by PushModal
}  // namespace off

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: Watcom list links and fields before a virtual base
    return *reinterpret_cast<T*>(self + offset);
}

std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t delta) {
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self + delta);
    return self + delta + vbtable[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);

struct Message {  // 20 bytes, passed by value
    std::uint32_t words[5];
};
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

std::uint32_t TakeFirst(std::uintptr_t list) {
    return reinterpret_cast<Thiscall1>(kListGet)(list, nullptr, 0);
}

std::uint32_t DeleteLink(std::uintptr_t list, std::uint32_t link) {
    return Slot<Thiscall1>(list, kListDeleteLink)(list, nullptr, link);
}

std::uint8_t HasQueued(std::uintptr_t screen) {
    using HasQueuedFn = std::uint8_t(__fastcall*)(std::uintptr_t self, void* edx);
    return reinterpret_cast<HasQueuedFn>(kHasQueuedMessages)(screen, nullptr);
}

// Takes the first queued message out into `message`; false (message
// untouched) when the queue had no link.
bool TakeMessage(std::uintptr_t queue, Message& message) {
    const std::uint32_t link = TakeFirst(queue);
    if (link == 0) {
        return false;
    }
    std::memcpy(&message, reinterpret_cast<const void*>(link + 4), sizeof(message));
    DeleteLink(queue, link);
    return true;
}

std::uint32_t Dispatch(std::uintptr_t element, const Message& message) {
    return reinterpret_cast<MessageFn>(kElementOnMessage)(element, nullptr, message);
}

}  // namespace

using ProcessFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using OnMessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);
using PopModalFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t child);

// Purpose: Handles one message the screen's elements posted (one per call
// from the UI loop).
std::uint32_t __fastcall CScreen_ProcessQueuedMessage(std::uintptr_t self, void*) {
    using HasQueuedWordFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
    const std::uint32_t queued = reinterpret_cast<HasQueuedWordFn>(kHasQueuedMessages)(self, nullptr);
    if (static_cast<std::uint8_t>(queued) == 0) {
        return queued;
    }
    Message message;  // uninitialized when the queue has no link (as the original; can't happen)
    TakeMessage(self + off::kScreenQueue, message);
    return Dispatch(VirtualBase(self, 4), message);
}

// Purpose: A screen's message handler: first the messages its elements
// posted, then the new one.
std::uint32_t __fastcall CScreen_OnMessage(std::uintptr_t self, void*, Message message) {
    const std::uintptr_t screen = self + off::kElementToScreen;
    std::uint32_t result = 0;
    while (HasQueued(screen) != 0) {
        Message queued = message;  // a null link hands over the argument
        TakeMessage(self + (off::kScreenQueue + off::kElementToScreen), queued);
        result = Dispatch(VirtualBase(screen, 4), queued);
    }
    result = Dispatch(VirtualBase(screen, 4), message);
    return result;
}

// Purpose: Ends a modal child (dialog): restores the input capture and
// focus saved by PushModal and removes the child if PushModal added it.
std::uint32_t __fastcall CScreenGroup_PopModal(std::uintptr_t self, void*, std::uint32_t child) {
    const std::uintptr_t savedCaptured = self + off::kGroupSavedCaptured;
    std::uint32_t captured = TakeFirst(savedCaptured);
    if (captured != 0) {
        const std::uint32_t link = captured;
        captured = Field<std::uint32_t>(link, 4);
        DeleteLink(savedCaptured, link);
    }
    Field<std::uint32_t>(self, off::kGroupCaptured) = captured;

    const std::uintptr_t savedAdded = self + off::kGroupSavedAdded;
    if (const std::uint32_t link = TakeFirst(savedAdded); link != 0) {
        const std::uint8_t added = Field<std::uint8_t>(link, 4);
        DeleteLink(savedAdded, link);
        if (added != 0) {
            Slot<Thiscall1>(self, ui::group_slot::kRemoveChild)(self, nullptr, child);
        }
    }

    const std::uintptr_t savedFocus = self + off::kGroupSavedFocus;
    const std::uint32_t link = TakeFirst(savedFocus);
    if (link == 0) {
        return 0;
    }
    const std::uint32_t focus = Field<std::uint32_t>(link, 4);
    const std::uint32_t deleted = DeleteLink(savedFocus, link);
    if (focus == 0) {
        return deleted;
    }
    return Slot<Thiscall1>(focus, ui::element_slot::kSetFocus)(focus, nullptr, 0);
}

S3RE_HOOK(0x00417df0, CScreen_ProcessQueuedMessage, ProcessFn, Original_CScreen_ProcessQueuedMessage,
          CScreen_ProcessQueuedMessage);
S3RE_HOOK(0x00417e70, CScreen_OnMessage, OnMessageFn, Original_CScreen_OnMessage, CScreen_OnMessage);
S3RE_HOOK(0x00417a30, CScreenGroup_PopModal, PopModalFn, Original_CScreenGroup_PopModal, CScreenGroup_PopModal);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

constexpr std::size_t kLinks = 0x2c00;
int g_queued = 0;  // HasQueuedMessages answers true this many more times

// get(0): a link in the buffer (or 0 now and then, except for the screen
// queue after HasQueuedMessages); HasQueuedMessages: g_queued; the rest a hash.
std::uint32_t Effect(const kit::Call& call, const std::uint32_t*) {
    std::uint32_t hash = call.callee * 2654435761u + static_cast<std::uint32_t>(g_queued);
    for (std::uint32_t i = 0; i < call.count; ++i) {
        hash = (hash ^ call.args[i]) * 16777619u;
    }
    hash ^= call.self * 7;
    if (call.callee == kHasQueuedMessages) {
        return (hash & 0xffffff00u) | (g_queued-- > 0 ? 1u : 0u);
    }
    if (call.callee == kListGet) {
        return hash % 4 == 0 ? 0 : static_cast<std::uint32_t>(kit::Current().base + kLinks + 0x20 * (hash % 8));
    }
    return hash ^ (hash >> 11);
}

// Screen-queue variant: get(0) never 0 (the uninitialized message).
std::uint32_t QueueEffect(const kit::Call& call, const std::uint32_t* raw) {
    if (call.callee == kListGet) {
        return static_cast<std::uint32_t>(kit::Current().base + kLinks + 0x20 * ((call.self + g_queued) % 8));
    }
    return Effect(call, raw);
}

struct Members {
    ProcessFn process = &CScreen_ProcessQueuedMessage;
    OnMessageFn onMessage = &CScreen_OnMessage;
    PopModalFn popModal = &CScreenGroup_PopModal;
};

Message RandomMessage(test::Rng& r) {
    Message m{};
    for (auto& w : m.words) {
        w = r.Next32();
    }
    return m;
}

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kHasQueuedMessages, 0>().This<kListGet, 1>().This<kElementOnMessage, 5>();
    // The queue's and the link's vtables are fake ones; the screen's element
    // behind its vbptr (+4).
    auto screen = [](test::Rng& r, std::uint8_t* b) {
        kit::VbPtr(b, kThis + 4, 0x100, 0);
        kit::FakeObject(b, kThis + off::kScreenQueue);
        g_queued = static_cast<int>(r.Below(4));
    };
    kit::Compare(
        ctx, "ProcessQueuedMessage", cases, screen,
        [](std::uintptr_t base, test::Rng&) { return Original_CScreen_ProcessQueuedMessage(base + kThis, nullptr); },
        [&](std::uintptr_t base, test::Rng&) { return ours.process(base + kThis, nullptr); }, 0xffffffffu,
        &QueueEffect);
    // OnMessage: `this` = the element part; the screen 0x98 before it.
    auto element = [](test::Rng& r, std::uint8_t* b) {
        kit::VbPtr(b, kThis - 0x98 + 4, 0x100, 0);
        kit::FakeObject(b, kThis - 0x98 + off::kScreenQueue);
        g_queued = static_cast<int>(r.Below(4));
    };
    kit::Compare(
        ctx, "ScreenOnMessage", cases, element,
        [](std::uintptr_t base, test::Rng& r) {
            return Original_CScreen_OnMessage(base + kThis, nullptr, RandomMessage(r));
        },
        [&](std::uintptr_t base, test::Rng& r) { return ours.onMessage(base + kThis, nullptr, RandomMessage(r)); },
        0xffffffffu, &Effect);
    // PopModal: the group and its three lists with fake vtables; the links'
    // values random (sometimes 0); a saved focus element at 0x2400.
    auto group = [](test::Rng& r, std::uint8_t* b) {
        kit::FakeObject(b, kThis);
        for (std::int32_t list : {off::kGroupSavedCaptured, off::kGroupSavedFocus, off::kGroupSavedAdded}) {
            kit::FakeObject(b, kThis + list);
        }
        // The values: 0 or a focus element (at 0x2400 or 0x2501, so the
        // bool list's byte is 0 or 1).
        kit::FakeObject(b, 0x2400);
        kit::FakeObject(b, 0x2501);
        for (int k = 0; k < 8; ++k) {
            const std::size_t value = kLinks + 0x20 * k + 4;
            switch (r.Below(3)) {
            case 0: kit::Put(b, value, 0); break;
            case 1: kit::Link(b, value, 0x2400); break;
            default: kit::Link(b, value, 0x2501); break;
            }
        }
    };
    kit::Compare(
        ctx, "PopModal", cases, group,
        [](std::uintptr_t base, test::Rng& r) { return Original_CScreenGroup_PopModal(base + kThis, nullptr, r.Next32()); },
        [&](std::uintptr_t base, test::Rng& r) { return ours.popModal(base + kThis, nullptr, r.Next32()); },
        0xffffffffu, &Effect);
}

std::uint32_t __fastcall WrongOnMessage(std::uintptr_t self, void* edx, Message message) {
    const std::uintptr_t screen = self + off::kElementToScreen;
    if (HasQueued(screen) != 0) {  // only one queued message
        Message queued = message;
        TakeMessage(self + (off::kScreenQueue + off::kElementToScreen), queued);
        Dispatch(VirtualBase(screen, 4), queued);
    }
    (void)edx;
    return Dispatch(VirtualBase(screen, 4), message);
}
std::uint32_t __fastcall WrongPopModal(std::uintptr_t self, void* edx, std::uint32_t child) {
    const std::uint32_t result = CScreenGroup_PopModal(self, edx, child);
    Field<std::uint32_t>(self, off::kGroupCaptured) = 0;  // the capture not restored
    return result;
}

}  // namespace

S3RE_TEST(UiScreenQueue_MatchOriginal) {
    CompareAll(ctx, Members{}, 500);
}

S3RE_TEST(UiScreenQueue_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.onMessage = &WrongOnMessage;
        } else {
            wrong.popModal = &WrongPopModal;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
