// Members of CButton, CVScrollBar and CDropdown (Dirk\dialog.cpp; layouts
// in re/ui_classes.md and controls.cpp). All thiscall. "element" below is
// the object's CScreenElement part, reached through its vbptr at +4
// (this + 4 + vbtable[1]); the original computes it with a null check of
// the object first (`this ? element : 0`), kept here.
//
// CButton (this = the button unless noted; fields from its start):
// CButton::IsPressed (0x0041a990): () -> AL = pressed (+0x49).
// CButton::Click (0x0041a9b0): a toggle button (+0x48) flips checked
//   (+0x4a); the click functor (+0x38) slot 0 with the element; with a
//   command id (+0x44) a message posted (element slot 31): type 0x40 in
//   byte 0, the id at +0xc (the other 15 bytes are an uninitialized local
//   in the original; ours zero them). EAX = PostMessage's, or 0 (no id).
// CButton::SetChecked (0x0041aa20): (bool) ret 4: a toggle button whose
//   checked byte differs from the argument's low byte: Click. EAX: Click's,
//   or AL = the byte last loaded (toggle or checked; the rest is the
//   caller's in the original, zero in ours).
// CButton::KillFocus (0x0041aa40; this = the element part, button + 0x50):
//   if IsPressed: pressed = 0, the state functor (+0x40) with the element,
//   element Invalidate (slot 7); then CScreenElement::KillFocus 0x415bd0 on
//   the element. EAX = its.
// CButton::OnLButtonDown (0x0041aaa0; this = button + 0x50): (message by
//   value) ret 0x14: CControl::OnLButtonDown 0x418380 (button + 0x3c) with
//   the message; disabled (+0x0c): AL = 0; else pressed = 1, the functor at
//   +0x3c with the element, Invalidate, AL = 1.
// CButton::OnLButtonUp (0x0041ab10; this = button + 0x50): (message) ret
//   0x14: disabled: AL = 0; pressed: Click, pressed = 0, the state functor
//   (+0x40), Invalidate; AL = 1.
// CButton::OnMouseLeave (0x0041ab70; this = button + 0x50): (message) ret
//   0x14: CControl::OnMouseLeave 0x4184e0 (button + 0x3c) with it; pressed:
//   pressed = 0 (no click), the state functor, Invalidate; AL = 1.
//
// CVScrollBar (this = the scroll bar; position +0x2e4 in [0, range +0x2e0 -
// page +0x2e8], the scroll functor +0xb4 gets (element, position)):
// CVScrollBar::ScrollBy (0x0041e290): (delta) ret 4: unless delta > 0 with
//   the position already at the end: position += delta, clamped to the end
//   and then to 0 (each step stored), the functor, Invalidate. EAX =
//   Invalidate's, or delta.
// CVScrollBar::SetPosition (0x0041e330): (position) ret 4: if it changes:
//   stored, the functor, Invalidate. EAX: Invalidate's, or the caller's
//   (every caller overwrites it).
// CVScrollBar::SetRange (0x0041e380): (position, range, page) ret 0xc: if
//   any differs: page, range, position stored, the functor, Invalidate.
//   EAX = Invalidate's, or range.
// CVScrollBar::GetPosition (0x0041e400): () -> the position.
//
// CDropdown (this = the dropdown; its group part +0x38 (vtable: AddChild,
// RemoveChild), the drop list +0x1c8 (vbptr +0x1cc), open +0x660, the list
// height +0x664):
// CDropdown::OnArrowClicked (0x00422590): (button) ret 4, the argument
//   unused: the element rect; closed: the list's element gets {left - 4,
//   bottom + 2, right - 10, bottom + height + 8} (SetRect 0x4156e0), is
//   added to the group, open = 1, g_openDropdown = this; open: the list is
//   removed from the group, open = 0. EAX = AddChild's / RemoveChild's.
// CDropdown::OnFocusChanged (0x00422770; this = the element, dropdown +
//   0x66c): (bool) ret 4: an open list of a dropdown other than
//   g_openDropdown is closed (OnArrowClicked with the element); then
//   CScreenElement::vf25 0x415c90 (`xor al, al`) on the element. AL = 0
//   (the rest of EAX is the vbtable address in the original; callers use
//   AL only).
// CDropdown::Draw (0x004227d0; this = dropdown + 0x66c): (surface, a, b)
//   ret 0xc: the same close, then CScreenGroup::Draw 0x417480 of the group
//   (this - 0x5b4). EAX = Draw's.

#include "game/globals.h"
#include "game/ui/screen_element_layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// CButton's own fields (from the object's start; its CControl part first).
struct ButtonFields {
    std::uint32_t vtable;           // 0x00
    std::uint32_t vbptr;            // 0x04
    std::uint32_t unk08;
    std::uint8_t disabled;          // 0x0c (CControl)
    std::uint8_t control0d[0x38 - 0x0d];
    std::uint32_t click_functor;    // 0x38 the listener notified on a click
    std::uint32_t press_functor;    // 0x3c called when the button is pressed
    std::uint32_t state_functor;    // 0x40 called when the pressed state ends
    std::uint32_t command_id;       // 0x44 posted as a type-0x40 message
    std::uint8_t toggle;            // 0x48 a toggle button
    std::uint8_t pressed;           // 0x49
    std::uint8_t checked;           // 0x4a
    std::uint8_t pad4b;
};
static_assert(offsetof(ButtonFields, disabled) == 0x0c);
static_assert(offsetof(ButtonFields, press_functor) == 0x3c);
static_assert(offsetof(ButtonFields, command_id) == 0x44);
static_assert(offsetof(ButtonFields, pressed) == 0x49);
static_assert(sizeof(ButtonFields) == 0x4c);

struct ScrollBarFields {
    std::uint32_t vtable;           // 0x00
    std::uint32_t vbptr;            // 0x04
    std::uint8_t unk08[0xb4 - 0x08];
    std::uint32_t scroll_functor;   // 0xb4 slot 0 gets (element, position)
    std::uint8_t unkb8[0x2e0 - 0xb8];
    std::int32_t range;             // 0x2e0
    std::int32_t position;          // 0x2e4
    std::int32_t page;              // 0x2e8 the visible part
};
static_assert(offsetof(ScrollBarFields, scroll_functor) == 0xb4);
static_assert(offsetof(ScrollBarFields, range) == 0x2e0);
static_assert(offsetof(ScrollBarFields, page) == 0x2e8);

struct DropdownFields {
    std::uint32_t vtable;           // 0x000
    std::uint32_t vbptr;            // 0x004
    std::uint8_t unk008[0x38 - 0x08];
    std::uint32_t group_vtable;     // 0x038 the CScreenGroup part (AddChild, RemoveChild)
    std::uint8_t unk03c[0x1c8 - 0x3c];
    std::uint32_t list;             // 0x1c8 the drop list (a CListBox)
    std::uint32_t list_vbptr;       // 0x1cc
    std::uint8_t unk1d0[0x660 - 0x1d0];
    std::uint8_t open;              // 0x660 the list is shown
    std::uint8_t pad661[3];
    std::int32_t list_height;       // 0x664
};
static_assert(offsetof(DropdownFields, group_vtable) == 0x38);
static_assert(offsetof(DropdownFields, list_vbptr) == 0x1cc);
static_assert(offsetof(DropdownFields, open) == 0x660);
static_assert(offsetof(DropdownFields, list_height) == 0x664);

struct Message {
    std::uint32_t words[5];
};
#pragma pack(pop)

constexpr std::uintptr_t kElementRect = 0x004156d0;        // CScreenElement::Rect (ours)
constexpr std::uintptr_t kElementSetRect = 0x004156e0;     // CScreenElement::SetRect (ours)
constexpr std::uintptr_t kElementKillFocus = 0x00415bd0;   // CScreenElement::KillFocus (ours)
constexpr std::uintptr_t kElementSlot25 = 0x00415c90;      // CScreenElement::vf25 (`xor al, al`)
constexpr std::uintptr_t kGroupDraw = 0x00417480;          // CScreenGroup::Draw (ours)
constexpr std::uintptr_t kControlOnLButtonDown = 0x00418380;  // CControl::OnLButtonDown (ours)
constexpr std::uintptr_t kControlOnMouseLeave = 0x004184e0;   // CControl::OnMouseLeave (ours)
constexpr std::uintptr_t kButtonIsPressed = 0x0041a990;    // CButton::IsPressed (ours)
constexpr std::uintptr_t kButtonClick = 0x0041a9b0;        // CButton::Click (ours)
constexpr std::uintptr_t kDropdownOnArrowClicked = 0x00422590;  // CDropdown::OnArrowClicked (ours)

constexpr std::uint8_t kCommandMessage = 0x40;  // message type of a button's command
constexpr std::uint32_t kButtonElement = 0x50;  // the button's element part
constexpr std::uint32_t kButtonControl = 0x3c;  // its CControl handler's `this`
constexpr std::uint32_t kDropdownElement = 0x66c;
constexpr std::uint32_t kDropdownGroup = 0x38;
constexpr std::uint32_t kDropdownGroupThis = 0xb8;  // CScreenGroup::Draw's `this` (the group part + 0x80)
constexpr LONG kListLeftOverhang = 4;
constexpr LONG kListGap = 2;
constexpr LONG kListRightInset = 10;
constexpr LONG kListExtra = 8;

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);
using RectFn = const RECT*(__fastcall*)(std::uintptr_t self, void* edx);
using SetRectFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, const RECT* rect);

// The element part of an object whose vbptr is at +4 (0 for a null object,
// as the original checks).
std::uintptr_t ElementOf(std::uintptr_t object) {
    if (object == 0) {
        return 0;
    }
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(object + 4);
    return object + 4 + vbtable[1];
}
// The same without the null check (where the original has none).
std::uintptr_t ElementAt(std::uintptr_t object) {
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(object + 4);
    return object + 4 + vbtable[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

std::uint32_t Invalidate(std::uintptr_t element) {
    return Slot<Thiscall0>(element, ui::element_slot::kInvalidate)(element, nullptr);
}

// A functor's slot 0 with the element (and an argument).
void Notify(std::uint32_t functor, std::uintptr_t element) {
    Slot<Thiscall1>(functor, 0)(functor, nullptr, static_cast<std::uint32_t>(element));
}
void Notify(std::uint32_t functor, std::uintptr_t element, std::uint32_t value) {
    Slot<Thiscall2>(functor, 0)(functor, nullptr, static_cast<std::uint32_t>(element), value);
}

ButtonFields& ButtonOf(std::uintptr_t element) {
    return *reinterpret_cast<ButtonFields*>(element - kButtonElement);
}

// 0x41aa4f..0x41aa7f (and the same in OnLButtonUp / OnMouseLeave): a
// pressed button is released: pressed = 0, the state functor, Invalidate.
std::uint32_t Release(ButtonFields& button) {
    button.pressed = 0;
    const auto object = reinterpret_cast<std::uintptr_t>(&button);
    if (button.state_functor != 0) {
        Notify(button.state_functor, ElementOf(object));
    }
    return Invalidate(ElementAt(object));
}

// 0x42277a..0x4227a5: closes the open list of a dropdown that is not the
// global open one.
void CloseIfNotCurrent(std::uintptr_t element) {
    const std::uintptr_t dropdown = element - kDropdownElement;
    if (reinterpret_cast<const DropdownFields*>(dropdown)->open != 0 && g_openDropdown.get() != dropdown) {
        reinterpret_cast<Thiscall1>(kDropdownOnArrowClicked)(dropdown, nullptr,
                                                            static_cast<std::uint32_t>(ElementOf(dropdown)));
    }
}

}  // namespace

// Purpose: Whether a button is being held down.
std::uint32_t __fastcall CButton_IsPressed(const ButtonFields* self, void*) {
    return self->pressed;
}

// Purpose: A button was clicked: toggles a toggle button, tells its click
// listener and posts its command to the screen.
std::uint32_t __fastcall CButton_Click(ButtonFields* self, void*) {
    if (self->toggle != 0) {
        self->checked = self->checked == 0 ? 1 : 0;
    }
    const auto object = reinterpret_cast<std::uintptr_t>(self);
    if (self->click_functor != 0) {
        Notify(self->click_functor, ElementOf(object));
    }
    if (self->command_id == 0) {
        return 0;
    }
    Message message{};  // (only the type byte and the id are set in the original)
    message.words[0] = kCommandMessage;
    message.words[3] = self->command_id;
    const std::uintptr_t element = ElementAt(object);
    return Slot<MessageFn>(element, ui::element_slot::kPostMessage)(element, nullptr, message);
}

// Purpose: Sets a toggle button's checked state (clicking it if it changes).
std::uint32_t __fastcall CButton_SetChecked(ButtonFields* self, void*, std::uint32_t checked) {
    if (self->toggle == 0) {
        return self->toggle;
    }
    if (self->checked == static_cast<std::uint8_t>(checked)) {
        return self->checked;
    }
    return reinterpret_cast<Thiscall0>(kButtonClick)(reinterpret_cast<std::uintptr_t>(self), nullptr);
}

// Purpose: A button loses the focus: a held button is released without a
// click.
std::uint32_t __fastcall CButton_KillFocus(std::uintptr_t self, void*) {
    ButtonFields& button = ButtonOf(self);
    const auto object = reinterpret_cast<std::uintptr_t>(&button);
    if (static_cast<std::uint8_t>(reinterpret_cast<Thiscall0>(kButtonIsPressed)(object, nullptr)) != 0) {
        Release(button);
    }
    return reinterpret_cast<Thiscall0>(kElementKillFocus)(ElementAt(object), nullptr);
}

// Purpose: The mouse button goes down on a button: it is pressed (unless
// disabled).
std::uint32_t __fastcall CButton_OnLButtonDown(std::uintptr_t self, void*, Message message) {
    ButtonFields& button = ButtonOf(self);
    reinterpret_cast<MessageFn>(kControlOnLButtonDown)(self - kButtonElement + kButtonControl, nullptr, message);
    if (button.disabled != 0) {
        return 0;
    }
    button.pressed = 1;
    const auto object = reinterpret_cast<std::uintptr_t>(&button);
    if (button.press_functor != 0) {
        Notify(button.press_functor, ElementOf(object));
    }
    Invalidate(ElementAt(object));
    return 1;
}

// Purpose: The mouse button is released over a button: a pressed button is
// clicked and released.
std::uint32_t __fastcall CButton_OnLButtonUp(std::uintptr_t self, void*, Message) {
    ButtonFields& button = ButtonOf(self);
    if (button.disabled != 0) {
        return 0;
    }
    if (button.pressed != 0) {
        reinterpret_cast<Thiscall0>(kButtonClick)(reinterpret_cast<std::uintptr_t>(&button), nullptr);
        Release(button);
    }
    return 1;
}

// Purpose: The mouse leaves a button: a pressed button is released without
// a click.
std::uint32_t __fastcall CButton_OnMouseLeave(std::uintptr_t self, void*, Message message) {
    ButtonFields& button = ButtonOf(self);
    reinterpret_cast<MessageFn>(kControlOnMouseLeave)(self - kButtonElement + kButtonControl, nullptr, message);
    if (button.pressed != 0) {
        Release(button);
    }
    return 1;
}

namespace {

// The scroll functor with the new position, then Invalidate.
std::uint32_t ScrollChanged(ScrollBarFields* bar, std::int32_t position) {
    const auto object = reinterpret_cast<std::uintptr_t>(bar);
    if (bar->scroll_functor != 0) {
        Notify(bar->scroll_functor, ElementOf(object), static_cast<std::uint32_t>(position));
    }
    return Invalidate(ElementAt(object));
}

}  // namespace

// Purpose: Scrolls a scroll bar by some steps, kept inside its range.
std::uint32_t __fastcall CVScrollBar_ScrollBy(ScrollBarFields* self, void*, std::int32_t delta) {
    if (delta > 0 && self->position >= self->range - self->page) {
        return static_cast<std::uint32_t>(delta);
    }
    self->position += delta;
    const std::int32_t end = self->range - self->page;
    std::int32_t position = self->position < end ? self->position : end;
    self->position = position;
    if (position <= 0) {
        position = 0;
    }
    self->position = position;
    return ScrollChanged(self, position);
}

// Purpose: Moves a scroll bar to a position (no clamping).
std::uint32_t __fastcall CVScrollBar_SetPosition(ScrollBarFields* self, void*, std::int32_t position) {
    if (self->position == position) {
        return static_cast<std::uint32_t>(position);  // (EAX is the caller's in the original; callers overwrite it)
    }
    self->position = position;
    return ScrollChanged(self, position);
}

// Purpose: Sets a scroll bar's position, range and page size (after the
// list's contents changed).
std::uint32_t __fastcall CVScrollBar_SetRange(ScrollBarFields* self, void*, std::int32_t position, std::int32_t range,
                                              std::int32_t page) {
    if (self->range == range && self->position == position && self->page == page) {
        return static_cast<std::uint32_t>(range);
    }
    self->page = page;
    self->range = range;
    self->position = position;
    return ScrollChanged(self, position);
}

// Purpose: A scroll bar's position.
std::uint32_t __fastcall CVScrollBar_GetPosition(const ScrollBarFields* self, void*) {
    return static_cast<std::uint32_t>(self->position);
}

// Purpose: The dropdown's arrow was clicked: opens the list below the field
// or closes it.
std::uint32_t __fastcall CDropdown_OnArrowClicked(DropdownFields* self, void*, std::uint32_t) {
    const auto dropdown = reinterpret_cast<std::uintptr_t>(self);
    const RECT field = *reinterpret_cast<RectFn>(kElementRect)(ElementAt(dropdown), nullptr);
    const std::uintptr_t group = dropdown + kDropdownGroup;
    const std::uintptr_t listObject = dropdown + offsetof(DropdownFields, list);
    if (self->open == 0) {
        const RECT list{field.left - kListLeftOverhang, field.bottom + kListGap, field.right - kListRightInset,
                        self->list_height + field.bottom + kListExtra};
        reinterpret_cast<SetRectFn>(kElementSetRect)(ElementAt(listObject), nullptr, &list);
        const std::uint32_t result = Slot<Thiscall1>(group, ui::group_slot::kAddChild)(
            group, nullptr, static_cast<std::uint32_t>(ElementOf(listObject)));
        self->open = 1;
        g_openDropdown.get() = static_cast<std::uint32_t>(dropdown);
        return result;
    }
    const std::uint32_t result = Slot<Thiscall1>(group, ui::group_slot::kRemoveChild)(
        group, nullptr, static_cast<std::uint32_t>(ElementOf(listObject)));
    self->open = 0;
    return result;
}

// Purpose: A dropdown gains or loses the focus: an open list of another
// dropdown is closed first.
std::uint32_t __fastcall CDropdown_OnFocusChanged(std::uintptr_t self, void*, std::uint32_t focused) {
    CloseIfNotCurrent(self);
    return reinterpret_cast<Thiscall1>(kElementSlot25)(ElementAt(self - kDropdownElement), nullptr, focused);
}

// Purpose: Draws a dropdown (closing a stale open list first).
std::uint32_t __fastcall CDropdown_Draw(std::uintptr_t self, void*, std::uint32_t surface, std::uint32_t a,
                                        std::uint32_t b) {
    CloseIfNotCurrent(self);
    return reinterpret_cast<Thiscall3>(kGroupDraw)(self - kDropdownElement + kDropdownGroupThis, nullptr, surface, a,
                                                   b);
}


using ButtonGetFn = std::uint32_t(__fastcall*)(const ButtonFields* self, void* edx);
using ButtonClickFn = std::uint32_t(__fastcall*)(ButtonFields* self, void* edx);
using ButtonSetFn = std::uint32_t(__fastcall*)(ButtonFields* self, void* edx, std::uint32_t value);
using ElementFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using ElementMessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);
using ScrollByFn = std::uint32_t(__fastcall*)(ScrollBarFields* self, void* edx, std::int32_t value);
using ScrollRangeFn = std::uint32_t(__fastcall*)(ScrollBarFields* self, void* edx, std::int32_t position,
                                                 std::int32_t range, std::int32_t page);
using ScrollGetFn = std::uint32_t(__fastcall*)(const ScrollBarFields* self, void* edx);
using ArrowFn = std::uint32_t(__fastcall*)(DropdownFields* self, void* edx, std::uint32_t button);
using ElementArgFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using ElementDrawFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t surface, std::uint32_t a,
                                                 std::uint32_t b);

S3RE_HOOK(0x0041a990, CButton_IsPressed, ButtonGetFn, Original_CButton_IsPressed, CButton_IsPressed);
S3RE_HOOK(0x0041a9b0, CButton_Click, ButtonClickFn, Original_CButton_Click, CButton_Click);
S3RE_HOOK(0x0041aa20, CButton_SetChecked, ButtonSetFn, Original_CButton_SetChecked, CButton_SetChecked);
S3RE_HOOK(0x0041aa40, CButton_KillFocus, ElementFn, Original_CButton_KillFocus, CButton_KillFocus);
S3RE_HOOK(0x0041aaa0, CButton_OnLButtonDown, ElementMessageFn, Original_CButton_OnLButtonDown, CButton_OnLButtonDown);
S3RE_HOOK(0x0041ab10, CButton_OnLButtonUp, ElementMessageFn, Original_CButton_OnLButtonUp, CButton_OnLButtonUp);
S3RE_HOOK(0x0041ab70, CButton_OnMouseLeave, ElementMessageFn, Original_CButton_OnMouseLeave, CButton_OnMouseLeave);
S3RE_HOOK(0x0041e290, CVScrollBar_ScrollBy, ScrollByFn, Original_CVScrollBar_ScrollBy, CVScrollBar_ScrollBy);
S3RE_HOOK(0x0041e330, CVScrollBar_SetPosition, ScrollByFn, Original_CVScrollBar_SetPosition, CVScrollBar_SetPosition);
S3RE_HOOK(0x0041e380, CVScrollBar_SetRange, ScrollRangeFn, Original_CVScrollBar_SetRange, CVScrollBar_SetRange);
S3RE_HOOK(0x0041e400, CVScrollBar_GetPosition, ScrollGetFn, Original_CVScrollBar_GetPosition, CVScrollBar_GetPosition);
S3RE_HOOK(0x00422590, CDropdown_OnArrowClicked, ArrowFn, Original_CDropdown_OnArrowClicked, CDropdown_OnArrowClicked);
S3RE_HOOK(0x00422770, CDropdown_OnFocusChanged, ElementArgFn, Original_CDropdown_OnFocusChanged,
          CDropdown_OnFocusChanged);
S3RE_HOOK(0x004227d0, CDropdown_Draw, ElementDrawFn, Original_CDropdown_Draw, CDropdown_Draw);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The object under test sits in a buffer with its vbtable, its element
// part and fake vtables (element: Invalidate, PostMessage; group: AddChild,
// RemoveChild; functors: slot 0) whose entries are recording stubs; the
// direct callees SetRect, KillFocus, CScreenGroup::Draw and the CControl
// handlers are stubbed too. CScreenElement::Rect, vf25 and our IsPressed /
// Click / OnArrowClicked run for real. Both versions run on the same
// buffer, set up alike before each.

namespace {

constexpr std::size_t kBytes = 0x3000;
constexpr std::size_t kObject = 0x400;
constexpr std::size_t kElementTable = 0x2000;
constexpr std::size_t kGroupTable = 0x2100;
constexpr std::size_t kFunctor1Table = 0x2140;
constexpr std::size_t kFunctor2Table = 0x2180;
constexpr std::size_t kFunctors = 0x2200;  // functor objects
constexpr std::size_t kVbtables = 0x2400;
constexpr std::size_t kElements = 0x2600;  // the list's element etc.

std::uintptr_t g_base = 0;
std::vector<std::uint32_t>* g_log = nullptr;
std::uint32_t g_stubCounter = 0;

std::uint32_t Normalize(std::uint32_t value) {
    const std::uint32_t base = static_cast<std::uint32_t>(g_base);
    return value - base < kBytes ? value - base + 0xb0000000u : value;
}
void Log(std::initializer_list<std::uint32_t> values) {
    for (std::uint32_t v : values) {
        g_log->push_back(Normalize(v));
    }
}
std::uint32_t Result(std::uint32_t id) {
    return 0x5e5e0000u + id * 0x100 + ++g_stubCounter;
}

std::uint32_t __fastcall InvalidateStub(std::uint32_t self, void*) {
    Log({1, self});
    return Result(1);
}
std::uint32_t __fastcall PostMessageStub(std::uint32_t self, void*, Message message) {
    // Only the type byte and the id are defined in the original's message.
    Log({2, self, message.words[0] & 0xff, message.words[3]});
    return Result(2);
}
std::uint32_t __fastcall AddChildStub(std::uint32_t self, void*, std::uint32_t child) {
    Log({3, self, child});
    return Result(3);
}
std::uint32_t __fastcall RemoveChildStub(std::uint32_t self, void*, std::uint32_t child) {
    Log({4, self, child});
    return Result(4);
}
std::uint32_t __fastcall Functor1Stub(std::uint32_t self, void*, std::uint32_t a) {
    Log({5, self, a});
    return Result(5);
}
std::uint32_t __fastcall Functor2Stub(std::uint32_t self, void*, std::uint32_t a, std::uint32_t b) {
    Log({6, self, a, b});
    return Result(6);
}
std::uint32_t __fastcall SetRectStub(std::uint32_t self, void*, const RECT* rect) {
    Log({7, self, static_cast<std::uint32_t>(rect->left), static_cast<std::uint32_t>(rect->top),
         static_cast<std::uint32_t>(rect->right), static_cast<std::uint32_t>(rect->bottom)});
    return Result(7);
}
std::uint32_t __fastcall KillFocusStub(std::uint32_t self, void*) {
    Log({8, self});
    return Result(8);
}
std::uint32_t __fastcall GroupDrawStub(std::uint32_t self, void*, std::uint32_t surface, std::uint32_t a,
                                       std::uint32_t b) {
    Log({9, self, surface, a, b});
    return Result(9);
}
template <std::uint32_t Id>
std::uint32_t __fastcall ControlMessageStub(std::uint32_t self, void*, Message message) {
    Log({Id, self, message.words[0], message.words[1], message.words[2], message.words[3], message.words[4]});
    return Result(Id);
}

struct StubPatch {
    std::uintptr_t address;
    const void* stub;
};
const StubPatch kStubs[] = {
    {kElementSetRect, reinterpret_cast<const void*>(&SetRectStub)},
    {kElementKillFocus, reinterpret_cast<const void*>(&KillFocusStub)},
    {kGroupDraw, reinterpret_cast<const void*>(&GroupDrawStub)},
    {kControlOnLButtonDown, reinterpret_cast<const void*>(&ControlMessageStub<10>)},
    {kControlOnMouseLeave, reinterpret_cast<const void*>(&ControlMessageStub<11>)},
};

void Put(std::uint8_t* buffer, std::size_t offset, std::uint32_t value) {
    std::memcpy(buffer + offset, &value, 4);
}
void Link(std::uint8_t* buffer, std::size_t offset, std::size_t target) {
    Put(buffer, offset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + target)));
}
// The vbptr at `vbptr` -> a vbtable (slot) placing the element at `element`,
// whose vtable is the fake element vtable.
void Virtual(std::uint8_t* buffer, std::size_t vbptr, std::size_t slot, std::size_t element) {
    Link(buffer, vbptr, kVbtables + slot);
    Put(buffer, kVbtables + slot + 4, static_cast<std::uint32_t>(element - vbptr));
    Link(buffer, element, kElementTable);
}

// A functor field: none, or one of the functor objects.
void Functor(test::Rng& rng, std::uint8_t* buffer, std::size_t field, std::size_t table) {
    if (rng.Chance(1, 4)) {
        Put(buffer, field, 0);
        return;
    }
    const std::size_t functor = kFunctors + rng.Below(8) * 0x10;
    Link(buffer, field, functor);
    Link(buffer, functor, table);
}

void SetUpCommon(test::Rng& rng, std::uint8_t* buffer) {
    rng.Fill(buffer, kBytes);
    Put(buffer, kElementTable + ui::element_slot::kInvalidate, reinterpret_cast<std::uint32_t>(&InvalidateStub));
    Put(buffer, kElementTable + ui::element_slot::kPostMessage, reinterpret_cast<std::uint32_t>(&PostMessageStub));
    Put(buffer, kGroupTable + ui::group_slot::kAddChild, reinterpret_cast<std::uint32_t>(&AddChildStub));
    Put(buffer, kGroupTable + ui::group_slot::kRemoveChild, reinterpret_cast<std::uint32_t>(&RemoveChildStub));
    Put(buffer, kFunctor1Table, reinterpret_cast<std::uint32_t>(&Functor1Stub));
    Put(buffer, kFunctor2Table, reinterpret_cast<std::uint32_t>(&Functor2Stub));
}

std::uint8_t SmallByte(test::Rng& rng) {
    return rng.Chance(1, 2) ? 0 : static_cast<std::uint8_t>(rng.Chance(1, 2) ? 1 : rng.Range(2, 255));
}

void SetUpButton(test::Rng& rng, std::uint8_t* buffer) {
    SetUpCommon(rng, buffer);
    // The element part where the class puts it (+0x50), sometimes elsewhere.
    Virtual(buffer, kObject + 4, 0, kObject + (rng.Chance(3, 4) ? kButtonElement : 0x100));
    buffer[kObject + offsetof(ButtonFields, disabled)] = SmallByte(rng);
    buffer[kObject + offsetof(ButtonFields, toggle)] = SmallByte(rng);
    buffer[kObject + offsetof(ButtonFields, pressed)] = SmallByte(rng);
    buffer[kObject + offsetof(ButtonFields, checked)] = SmallByte(rng);
    Functor(rng, buffer, kObject + offsetof(ButtonFields, click_functor), kFunctor1Table);
    Functor(rng, buffer, kObject + offsetof(ButtonFields, press_functor), kFunctor1Table);
    Functor(rng, buffer, kObject + offsetof(ButtonFields, state_functor), kFunctor1Table);
    Put(buffer, kObject + offsetof(ButtonFields, command_id), rng.Chance(1, 3) ? 0 : rng.Next32());
}

void SetUpScrollBar(test::Rng& rng, std::uint8_t* buffer) {
    SetUpCommon(rng, buffer);
    Virtual(buffer, kObject + 4, 0, kObject + 0x300);
    Functor(rng, buffer, kObject + offsetof(ScrollBarFields, scroll_functor), kFunctor2Table);
    auto* bar = reinterpret_cast<ScrollBarFields*>(buffer + kObject);
    bar->range = rng.Range(0, 40);
    bar->page = rng.Range(0, 12);
    bar->position = rng.Range(-3, 40);
    if (rng.Chance(1, 10)) {
        bar->range = static_cast<std::int32_t>(rng.Next32());
        bar->position = static_cast<std::int32_t>(rng.Next32());
    }
}

void SetUpDropdown(test::Rng& rng, std::uint8_t* buffer) {
    SetUpCommon(rng, buffer);
    Virtual(buffer, kObject + 4, 0, kObject + kDropdownElement);  // where the class puts it
    Link(buffer, kObject + offsetof(DropdownFields, group_vtable), kGroupTable);
    Virtual(buffer, kObject + offsetof(DropdownFields, list_vbptr), 0x10, kElements);
    buffer[kObject + offsetof(DropdownFields, open)] = SmallByte(rng);
    Put(buffer, kObject + offsetof(DropdownFields, list_height), static_cast<std::uint32_t>(rng.Range(-5, 300)));
}

struct Run {
    std::uint32_t eax;
    std::uint32_t openDropdown;
    std::vector<std::uint32_t> calls;
};

using Setup = void (*)(test::Rng& rng, std::uint8_t* buffer);

template <typename CallA, typename CallB>
bool CompareMember(test::Context& ctx, const char* name, Setup setup, std::uint32_t mask, CallA callOriginal,
                   CallB callOurs, int cases) {
    static std::uint8_t buffer[kBytes], afterOriginal[kBytes];
    for (const StubPatch& patch : kStubs) {
        test::PatchJump(patch.address, patch.stub);
    }
    const std::uint32_t savedOpen = g_openDropdown.get();
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint64_t argumentSeed = ctx.rng().Next64();
        const std::uint32_t openPick = ctx.rng().Below(3);
        auto run = [&](auto call) {
            test::Rng fill(seed);
            setup(fill, buffer);
            g_base = reinterpret_cast<std::uintptr_t>(buffer);
            g_openDropdown.get() = openPick == 0 ? static_cast<std::uint32_t>(g_base + kObject)
                                                 : (openPick == 1 ? 0u : 0x12345678u);
            Run r{};
            g_log = &r.calls;
            g_stubCounter = 0;
            // Byte results: mask first. EAX's upper bytes are the caller's in
            // the original and may make it look like a pointer into the
            // buffer, which Normalize would rebase (AL included); this was
            // the "wrong AL" of IsPressed / SetChecked / OnFocusChanged.
            const std::uint32_t eax = call(g_base, argumentSeed);
            r.eax = mask == 0xff ? eax & mask : Normalize(eax) & mask;
            r.openDropdown = Normalize(g_openDropdown.get());
            g_log = nullptr;
            return r;
        };
        const Run ra = run(callOriginal);
        std::memcpy(afterOriginal, buffer, kBytes);
        const Run rb = run(callOurs);
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT_EQ(ra.openDropdown, rb.openDropdown) ||
            !S3RE_EXPECT(ra.calls == rb.calls) || !S3RE_EXPECT_MEM_EQ(afterOriginal, buffer, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
            ok = false;
        }
    }
    g_openDropdown.get() = savedOpen;
    for (auto it = std::rbegin(kStubs); it != std::rend(kStubs); ++it) {
        test::UnpatchJump(it->address);
    }
    return ok;
}

std::uint32_t Arg(std::uint64_t seed, int k) {
    return test::Rng(seed + k).Next32();
}
Message ArgMessage(std::uint64_t seed) {
    Message m;
    for (int k = 0; k < 5; ++k) {
        m.words[k] = Arg(seed, 10 + k);
    }
    return m;
}
std::int32_t SmallArg(std::uint64_t seed, int k) {
    test::Rng r(seed + k);
    return r.Chance(1, 8) ? static_cast<std::int32_t>(r.Next32()) : r.Range(-6, 45);
}

struct Members {
    ButtonGetFn isPressed = &CButton_IsPressed;
    ButtonClickFn click = &CButton_Click;
    ButtonSetFn setChecked = &CButton_SetChecked;
    ElementFn killFocus = &CButton_KillFocus;
    ElementMessageFn onLButtonDown = &CButton_OnLButtonDown;
    ElementMessageFn onLButtonUp = &CButton_OnLButtonUp;
    ElementMessageFn onMouseLeave = &CButton_OnMouseLeave;
    ScrollByFn scrollBy = &CVScrollBar_ScrollBy;
    ScrollByFn setPosition = &CVScrollBar_SetPosition;
    ScrollRangeFn setRange = &CVScrollBar_SetRange;
    ScrollGetFn getPosition = &CVScrollBar_GetPosition;
    ArrowFn onArrowClicked = &CDropdown_OnArrowClicked;
    ElementArgFn onFocusChanged = &CDropdown_OnFocusChanged;
    ElementDrawFn draw = &CDropdown_Draw;
};

Members OriginalMembers() {
    return {Original_CButton_IsPressed,      Original_CButton_Click,          Original_CButton_SetChecked,
            Original_CButton_KillFocus,      Original_CButton_OnLButtonDown,  Original_CButton_OnLButtonUp,
            Original_CButton_OnMouseLeave,   Original_CVScrollBar_ScrollBy,   Original_CVScrollBar_SetPosition,
            Original_CVScrollBar_SetRange,   Original_CVScrollBar_GetPosition, Original_CDropdown_OnArrowClicked,
            Original_CDropdown_OnFocusChanged, Original_CDropdown_Draw};
}

bool CompareAll(test::Context& ctx, const Members& ours, int cases) {
    const Members o = OriginalMembers();
    auto button = [](std::uintptr_t base) { return reinterpret_cast<ButtonFields*>(base + kObject); };
    auto bar = [](std::uintptr_t base) { return reinterpret_cast<ScrollBarFields*>(base + kObject); };
    auto dropdown = [](std::uintptr_t base) { return reinterpret_cast<DropdownFields*>(base + kObject); };
    auto element = [](std::uintptr_t base) { return base + kObject + kButtonElement; };
    auto dropElement = [](std::uintptr_t base) { return base + kObject + kDropdownElement; };
    constexpr std::uint32_t kAll = 0xffffffff, kAl = 0xff;
    bool ok = true;
    // Byte results are compared in AL (the rest of EAX is the caller's in
    // the original); SetPosition's EAX is not compared at all when nothing
    // changes (the caller's; every caller overwrites it), see below.
    ok = ok && CompareMember(ctx, "CButton::IsPressed", &SetUpButton, kAl,
                             [&](std::uintptr_t b, std::uint64_t) { return o.isPressed(button(b), nullptr); },
                             [&](std::uintptr_t b, std::uint64_t) { return ours.isPressed(button(b), nullptr); }, cases);
    ok = ok && CompareMember(ctx, "CButton::Click", &SetUpButton, kAll,
                             [&](std::uintptr_t b, std::uint64_t) { return o.click(button(b), nullptr); },
                             [&](std::uintptr_t b, std::uint64_t) { return ours.click(button(b), nullptr); }, cases);
    ok = ok && CompareMember(
                   ctx, "CButton::SetChecked", &SetUpButton, kAl,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.setChecked(button(b), nullptr, Arg(s, 0)); },
                   [&](std::uintptr_t b, std::uint64_t s) { return ours.setChecked(button(b), nullptr, Arg(s, 0)); },
                   cases);
    ok = ok && CompareMember(ctx, "CButton::KillFocus", &SetUpButton, kAll,
                             [&](std::uintptr_t b, std::uint64_t) { return o.killFocus(element(b), nullptr); },
                             [&](std::uintptr_t b, std::uint64_t) { return ours.killFocus(element(b), nullptr); },
                             cases);
    ok = ok && CompareMember(
                   ctx, "CButton::OnLButtonDown", &SetUpButton, kAl,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.onLButtonDown(element(b), nullptr, ArgMessage(s)); },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.onLButtonDown(element(b), nullptr, ArgMessage(s));
                   },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CButton::OnLButtonUp", &SetUpButton, kAl,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.onLButtonUp(element(b), nullptr, ArgMessage(s)); },
                   [&](std::uintptr_t b, std::uint64_t s) { return ours.onLButtonUp(element(b), nullptr, ArgMessage(s)); },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CButton::OnMouseLeave", &SetUpButton, kAl,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.onMouseLeave(element(b), nullptr, ArgMessage(s)); },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.onMouseLeave(element(b), nullptr, ArgMessage(s));
                   },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CVScrollBar::ScrollBy", &SetUpScrollBar, kAll,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.scrollBy(bar(b), nullptr, SmallArg(s, 0)); },
                   [&](std::uintptr_t b, std::uint64_t s) { return ours.scrollBy(bar(b), nullptr, SmallArg(s, 0)); },
                   cases);
    // SetPosition: the position sometimes unchanged (EAX not compared then).
    auto position = [&](std::uintptr_t b, std::uint64_t s) {
        return test::Rng(s).Chance(1, 4) ? bar(b)->position : SmallArg(s, 0);
    };
    ok = ok && CompareMember(
                   ctx, "CVScrollBar::SetPosition", &SetUpScrollBar, kAll,
                   [&](std::uintptr_t b, std::uint64_t s) {
                       const std::int32_t p = position(b, s);
                       const bool same = p == bar(b)->position;
                       const std::uint32_t eax = o.setPosition(bar(b), nullptr, p);
                       return same ? 0u : eax;
                   },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       const std::int32_t p = position(b, s);
                       const bool same = p == bar(b)->position;
                       const std::uint32_t eax = ours.setPosition(bar(b), nullptr, p);
                       return same ? 0u : eax;
                   },
                   cases);
    // SetRange: sometimes all three unchanged.
    auto range = [&](std::uintptr_t b, std::uint64_t s, int k) {
        const bool same = test::Rng(s + 100).Chance(1, 3);
        const ScrollBarFields& f = *bar(b);
        return same ? (k == 0 ? f.position : k == 1 ? f.range : f.page) : SmallArg(s, k);
    };
    ok = ok && CompareMember(
                   ctx, "CVScrollBar::SetRange", &SetUpScrollBar, kAll,
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return o.setRange(bar(b), nullptr, range(b, s, 0), range(b, s, 1), range(b, s, 2));
                   },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.setRange(bar(b), nullptr, range(b, s, 0), range(b, s, 1), range(b, s, 2));
                   },
                   cases);
    ok = ok && CompareMember(ctx, "CVScrollBar::GetPosition", &SetUpScrollBar, kAll,
                             [&](std::uintptr_t b, std::uint64_t) { return o.getPosition(bar(b), nullptr); },
                             [&](std::uintptr_t b, std::uint64_t) { return ours.getPosition(bar(b), nullptr); },
                             cases);
    ok = ok && CompareMember(
                   ctx, "CDropdown::OnArrowClicked", &SetUpDropdown, kAll,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.onArrowClicked(dropdown(b), nullptr, Arg(s, 0)); },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.onArrowClicked(dropdown(b), nullptr, Arg(s, 0));
                   },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CDropdown::OnFocusChanged", &SetUpDropdown, kAl,
                   [&](std::uintptr_t b, std::uint64_t s) { return o.onFocusChanged(dropElement(b), nullptr, Arg(s, 0)); },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.onFocusChanged(dropElement(b), nullptr, Arg(s, 0));
                   },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CDropdown::Draw", &SetUpDropdown, kAll,
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return o.draw(dropElement(b), nullptr, Arg(s, 0), Arg(s, 1), Arg(s, 2));
                   },
                   [&](std::uintptr_t b, std::uint64_t s) {
                       return ours.draw(dropElement(b), nullptr, Arg(s, 0), Arg(s, 1), Arg(s, 2));
                   },
                   cases);
    return ok;
}

// Wrong versions for the harness check.
std::uint32_t __fastcall WrongClick(ButtonFields* self, void* edx) {
    self->toggle = 0;  // never toggles
    return CButton_Click(self, edx);
}
std::uint32_t __fastcall WrongOnLButtonUp(std::uintptr_t self, void*, Message) {
    ButtonFields& button = ButtonOf(self);
    if (button.disabled != 0) {
        return 0;
    }
    if (button.pressed != 0) {
        Release(button);  // releases without clicking
    }
    return 1;
}
std::uint32_t __fastcall WrongScrollBy(ScrollBarFields* self, void* edx, std::int32_t delta) {
    return CVScrollBar_ScrollBy(self, edx, delta > 0 ? delta + 1 : delta);
}
std::uint32_t __fastcall WrongArrow(DropdownFields* self, void* edx, std::uint32_t button) {
    self->list_height += 1;
    const std::uint32_t result = CDropdown_OnArrowClicked(self, edx, button);
    self->list_height -= 1;
    return result;
}

}  // namespace

S3RE_TEST(UiButtonScroll_MatchOriginal) {
    CompareAll(ctx, Members{}, 1500);
}

S3RE_TEST(UiButtonScroll_ComparisonDetectsWrongImplementation) {
    const struct {
        const char* name;
        void (*patch)(Members&);
    } kWrong[] = {
        {"CButton::Click", [](Members& m) { m.click = &WrongClick; }},
        {"CButton::OnLButtonUp", [](Members& m) { m.onLButtonUp = &WrongOnLButtonUp; }},
        {"CVScrollBar::ScrollBy", [](Members& m) { m.scrollBy = &WrongScrollBy; }},
        {"CDropdown::OnArrowClicked", [](Members& m) { m.onArrowClicked = &WrongArrow; }},
    };
    for (const auto& wrong : kWrong) {
        Members candidate;
        wrong.patch(candidate);
        test::Context quiet("wrong", 13, true);
        if (!S3RE_EXPECT(!CompareAll(quiet, candidate, 1500))) {
            ctx.Fail(__FILE__, __LINE__, "  the wrong %s was not caught", wrong.name);
        }
    }
}

}  // namespace s3re::game
