// Small members of the UI controls (Dirk\dialog.cpp; layouts in
// re/ui_classes.md). All thiscall; "this" is the CScreenElement part (the
// virtual base) unless noted, so the controls' own fields sit before it.
//
// CControl::Draw (0x004187d0): (surface, rect A, rect B) ret 0xc:
//   CScreenElement::DrawRect 0x415af0 on the virtual base (this - 0x38 +
//   vbtable[1]); EAX = its.
// CControl::SetEnabled (0x00418810; CControl's own vtable slot 0, this = the
//   control): (bool) ret 4: disabled (+0xc) = !enabled. EAX: AL = the new
//   byte, the rest is the caller's (both callers overwrite EAX); ours
//   returns the byte zero-extended.
// CControl::SetTooltipTextId (0x00418330; this = the control): (text id)
//   ret 4: id 0: the tooltip text (+0x10) = "" (0x7a7868); otherwise
//   Dialog::GraphicsData 0x4168c0 on the virtual base (this + 4 +
//   vbtable[1]; it takes no arguments, the original pushes TextAddress's
//   (id, -1) before calling it), CGraphicsData::TextAddress 0x5faaa0(id, -1)
//   on its result, and that
//   text assigned (std::string assign_cstr_or_empty 0x413d70). EAX = the
//   assign's.
// CScreen::HasQueuedMessages (0x00417f50; this = the screen): EAX = (the
//   message count +0x84 != 0).
// CEditControl::OnLButtonDown (0x00419b30): (message by value, 20 bytes)
//   ret 0x14: disabled (this-0x64 != 0): the base handler on the virtual
//   base (0x415c80; `xor al, al`), else CControl's handler 0x418380 (this -
//   0x34) with the message. EAX = the handler's. (The second branch,
//   0x419b5f, is listed as a function start of its own: not a real one.)
// CVScrollBar::OnMoved (0x0041e9e0): (rect*, delta) ret 8:
//   CScreenGroup::OnMoved 0x417ab0 (this - 0x23c), then the inner rect
//   (this-0x24) = rect with top + 12 and bottom - 12 (the arrow buttons).
//   EAX = the new top.
// CDropdown::SetFlag1F (0x00422490): (flag) ret 4: CScreenGroup::SetFlag1F
//   0x417be0 (this - 0x5b4), then flag1f (+0x1f) of the edit field's
//   CScreenElement (its vbptr at this-0x668) cleared, so the field stays
//   opaque. EAX = SetFlag1F's.
// CListView::GetHeaderHeight (0x00424590; this = the list view): the header
//   height (+0x180) when headers are shown (+0x4f2), else 0.
// CListView::CColumn (0x20 bytes) and CLine (0x14 bytes): copy constructors
//   (0x004275a0, 0x004277a0) and assignments (0x00427600, 0x00427800; a
//   self-assignment does nothing) copy the fields and take the owned
//   pointer (+0x1c header text / +0x10 row group) from the source, which is
//   cleared (auto_ptr style). CLine copies its flag as a byte. EAX = this.
//   CColumn::operator== (0x00427830): AL = the indexes (+0) are equal; the
//   rest of EAX is this index's. CColumn::SetPosition (0x00427650): (x, y,
//   height) ret 0xc: x (+0x14) = x; with a header text (+0x1c):
//   CScreenElement::SetRect 0x4156e0 on it with {x, y, x + width (+8),
//   y + height}; EAX = SetRect's, or this without a header.
// CStaticImage (0x0041d7a0), CStaticFrame (0x0041d960) and
//   CStaticColorIndexedFrame (0x0041db90) constructors: (rect, a[, b[, c]])
//   ret 8 / 0xc / 0x10: CStaticControl(rect) 0x41c3c0, the class vtable
//   (0x6561e0 / 0x656264 / 0x6562e8) and the values at +0x20, +0x24,
//   +0x28. EAX = this.
// CVScrollBar::OnResized (0x0041e940), CDropdown::OnResized (0x004224c0),
//   CUpDown::OnResized (0x00422d90): (rect*, size delta) ret 8: place the
//   member controls with CScreenElement::SetRect 0x4156e0 (each one's
//   element through its vbptr, the up-down's edit field directly), the rect
//   re-read after each call. Scroll bar: up arrow {l, t, r, t + 12} (vbptr
//   this-0x238), down arrow {l, b - 12, r, b} (this-0x1b8), then the track
//   (this-0x24) = {l, t + 12, r, b - 12}; EAX = t + 12. Dropdown: the edit
//   field {l, t, r - 14, b} (this-0x5b0), the button {r - 12, t, r, b}
//   (this-0x520), the drop list {l - 4, b + 2, r - 10, b + 40} (this-0x4a0).
//   Up-down: the field {l, t, r - 12, b} (this-0x16c), up {r - 12, t, r,
//   (t + b) / 2} (this-0x124), down {r - 12, (t + b) / 2, r, b} (this-0xa4;
//   the halves rounded toward zero). EAX = the last SetRect's.
// CEditControl::~CEditControl (0x00421f50, this = the element, +0x70): the
//   text (a 16-bit string at this-0x24: buffer this-0x20, its count byte
//   just before it, the block 2 bytes before) released: count 0 or 0xff:
//   operator delete, else the count decremented; buffer/length/capacity =
//   0; then ~CControl 0x4180a0 (this - 0x34). EAX = its.
// UI_MoveWords (0x00418cf0): fastcall (ECX = to, EDX = from, stack: count)
//   ret 4: memmove of 16-bit characters (backwards when the ranges overlap
//   with from < to). EAX = to.

#include "game/texts.h"
#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// The parts of the controls these members use, each anchored where the
// member's `this` points (the field offsets in the comments are from it).
struct ListColumn {  // CListView::CColumn
    std::uint32_t index;          // 0x00
    std::uint32_t type;           // 0x04 cell type
    std::uint32_t width;          // 0x08
    std::uint32_t unk0c;
    std::uint32_t tooltip_text;   // 0x10 text id
    std::uint32_t x;              // 0x14
    std::uint32_t click_listener; // 0x18
    std::uint32_t header;         // 0x1c the header CStaticText (owned)
};
static_assert(sizeof(ListColumn) == 0x20);
struct ListLine {  // CListView::CLine
    std::uint32_t index;   // 0x00
    std::uint32_t height;  // 0x04
    std::uint32_t y;       // 0x08
    std::uint8_t flag;     // 0x0c
    std::uint8_t pad0d[3];
    std::uint32_t row;     // 0x10 the row CScreenGroup (owned)
};
static_assert(sizeof(ListLine) == 0x14);
struct ListViewHeader {  // CListView, from the object's start
    std::uint8_t unk000[0x180];
    std::uint32_t header_height;  // 0x180
    std::uint8_t unk184[0x4f2 - 0x184];
    std::uint8_t headers_shown;   // 0x4f2
};
static_assert(offsetof(ListViewHeader, headers_shown) == 0x4f2);
struct ScreenQueue {  // CScreen, from the object's start
    std::uint8_t unk00[0x84];
    std::uint32_t message_count;  // 0x84
};
struct StaticFrameFields {  // CStaticImage / CStaticFrame / CStaticColorIndexedFrame
    std::uint8_t control[0x20];   // CStaticControl
    std::uint32_t value20;        // image / file
    std::uint32_t value24;        // frame
    std::uint32_t value28;
};
#pragma pack(pop)

// The virtual-base adjustments: `this` + delta + vbtable[1], where the
// vbtable pointer sits at `this` + delta (the class's vbptr).
std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t delta) {
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self + delta);
    return self + delta + vbtable[1];
}

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

struct Message {
    std::uint32_t words[5];
};
using MessageHandlerFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

constexpr std::uintptr_t kDrawRect = 0x00415af0;              // CScreenElement::DrawRect (ours)
constexpr std::uintptr_t kGraphicsData = 0x004168c0;          // Dialog::GraphicsData (ours)
constexpr std::uintptr_t kTextAddress = 0x005faaa0;           // CGraphicsData::TextAddress (ours)
constexpr std::uintptr_t kAssignText = 0x00413d70;            // std::string::assign_cstr_or_empty (ours)
constexpr std::uintptr_t kElementOnLButtonDown = 0x00415c80;  // CScreenElement::vf12 (`xor al, al`)
constexpr std::uintptr_t kControlOnLButtonDown = 0x00418380;  // CControl's handler
constexpr std::uintptr_t kGroupOnMoved = 0x00417ab0;          // CScreenGroup::OnMoved (ours)
constexpr std::uintptr_t kGroupSetFlag1F = 0x00417be0;        // CScreenGroup::SetFlag1F (ours)
constexpr std::uintptr_t kSetRect = 0x004156e0;               // CScreenElement::SetRect (ours)
constexpr std::uintptr_t kStaticControl = 0x0041c3c0;         // CStaticControl::CStaticControl (ours)
constexpr std::uintptr_t kControlDestroy = 0x004180a0;        // CControl::~CControl (ours)
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;        // the game's operator delete

constexpr std::uint32_t kScrollArrowHeight = 12;  // the scroll bar's arrow buttons
constexpr LONG kDropButtonWidth = 12;     // the dropdown's and up-down's buttons
constexpr LONG kDropFieldGap = 14;        // the dropdown's field ends 14 before the right edge
constexpr LONG kDropListOverhang = 4;     // the drop list reaches 4 left of the field
constexpr LONG kDropListRightInset = 10;
constexpr LONG kDropListTop = 2;          // below the field
constexpr LONG kDropListBottom = 40;

using SetRectFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, const RECT* rect);

// SetRect on the element of the member whose vbptr is at self + delta.
std::uint32_t PlaceMember(std::uintptr_t self, std::int32_t delta, LONG left, LONG top, LONG right, LONG bottom) {
    const RECT rect{left, top, right, bottom};
    return reinterpret_cast<SetRectFn>(kSetRect)(VirtualBase(self, delta), nullptr, &rect);
}

}  // namespace

// Purpose: Draws a control (its element part's DrawRect).
std::uint32_t __fastcall CControl_Draw(std::uintptr_t self, void*, std::uint32_t surface, std::uint32_t a,
                                       std::uint32_t b) {
    return reinterpret_cast<Thiscall3>(kDrawRect)(VirtualBase(self, -0x38), nullptr, surface, a, b);
}

// Purpose: Enables or disables a control (buttons, edit fields).
std::uint32_t __fastcall CControl_SetEnabled(ui::CControl* self, void*, std::uint32_t enabled) {
    self->disabled = static_cast<std::uint8_t>(enabled) == 0 ? 1 : 0;
    return self->disabled;  // (only AL is defined in the original; callers overwrite EAX)
}

// Purpose: Sets the text id of a control's tooltip (0: none).
std::uint32_t __fastcall CControl_SetTooltipTextId(ui::CControl* self, void*, std::uint32_t textId) {
    const auto control = reinterpret_cast<std::uintptr_t>(self);
    const auto tooltipText = reinterpret_cast<std::uintptr_t>(&self->tooltip_text);
    if (textId == 0) {
        return reinterpret_cast<Thiscall1>(kAssignText)(tooltipText, nullptr, static_cast<std::uint32_t>(texts::kEmpty.address()));
    }
    // GraphicsData takes no arguments (`mov eax, [g]; ret`); the original
    // pushes TextAddress's two arguments before calling it.
    const std::uint32_t graphics = reinterpret_cast<Thiscall0>(kGraphicsData)(VirtualBase(control, 4), nullptr);
    const std::uint32_t text = reinterpret_cast<Thiscall2>(kTextAddress)(graphics, nullptr, textId, 0xFFFFFFFFu);
    return reinterpret_cast<Thiscall1>(kAssignText)(tooltipText, nullptr, text);
}

// Purpose: Whether a screen has UI messages waiting.
std::uint32_t __fastcall CScreen_HasQueuedMessages(const ScreenQueue* self, void*) {
    return self->message_count != 0 ? 1 : 0;
}

// Purpose: Left click on an edit field: a disabled one ignores it, an
// enabled one handles it as a control (focus, tooltip).
std::uint32_t __fastcall CEditControl_OnLButtonDown(std::uintptr_t self, void*, Message message) {
    if (Field<std::uint8_t>(self, -0x64) != 0) {
        return reinterpret_cast<MessageHandlerFn>(kElementOnLButtonDown)(VirtualBase(self, -0x6c), nullptr, message);
    }
    return reinterpret_cast<MessageHandlerFn>(kControlOnLButtonDown)(self - 0x34, nullptr, message);
}

// Purpose: Moves a vertical scroll bar: its group moves, and the track
// between the arrow buttons follows.
std::uint32_t __fastcall CVScrollBar_OnMoved(std::uintptr_t self, void*, const RECT* rect, std::uint32_t delta) {
    reinterpret_cast<Thiscall2>(kGroupOnMoved)(self - 0x23c, nullptr, reinterpret_cast<std::uint32_t>(rect), delta);
    RECT& track = Field<RECT>(self, -0x24);
    const RECT moved = *rect;
    track.left = moved.left;
    track.top = moved.top + kScrollArrowHeight;
    track.right = moved.right;
    track.bottom = moved.bottom - kScrollArrowHeight;
    return static_cast<std::uint32_t>(track.top);
}

// Purpose: Sets a dropdown's "transparent" flag (its group), keeping its
// edit field opaque.
std::uint32_t __fastcall CDropdown_SetFlag1F(std::uintptr_t self, void*, std::uint32_t flag) {
    const std::uint32_t result = reinterpret_cast<Thiscall1>(kGroupSetFlag1F)(self - 0x5b4, nullptr, flag);
    reinterpret_cast<ui::CScreenElement*>(VirtualBase(self, -0x668))->flag1f = 0;
    return result;
}

// Purpose: The height of a list view's header row (0 when hidden).
std::uint32_t __fastcall CListView_GetHeaderHeight(const ListViewHeader* self, void*) {
    return self->headers_shown != 0 ? self->header_height : 0;
}

// Purpose: Copy constructors and assignments of the list view's columns and
// lines (they take over the source's header text / row group).
ListColumn* __fastcall CColumn_CopyConstruct(ListColumn* self, void*, ListColumn* other) {
    *self = *other;
    other->header = 0;
    return self;
}
ListColumn* __fastcall CColumn_Assign(ListColumn* self, void*, ListColumn* other) {
    if (self != other) {
        *self = *other;
        other->header = 0;
    }
    return self;
}
ListLine* __fastcall CLine_CopyConstruct(ListLine* self, void*, ListLine* other) {
    self->index = other->index;
    self->height = other->height;
    self->y = other->y;
    self->flag = other->flag;  // (the padding after it is not copied)
    self->row = other->row;
    other->row = 0;
    return self;
}
ListLine* __fastcall CLine_Assign(ListLine* self, void* edx, ListLine* other) {
    if (self != other) {
        CLine_CopyConstruct(self, edx, other);
    }
    return self;
}

// Purpose: Whether two list view columns are the same column (by index).
std::uint32_t __fastcall CColumn_Equal(const ListColumn* self, void*, const ListColumn* other) {
    return (self->index & 0xFFFFFF00u) | (self->index == other->index ? 1u : 0u);
}

// Purpose: Places a list view column (and its header text) at x.
std::uint32_t __fastcall CColumn_SetPosition(ListColumn* self, void*, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t height) {
    self->x = x;
    if (self->header == 0) {
        return reinterpret_cast<std::uint32_t>(self);
    }
    RECT rect;
    rect.left = static_cast<LONG>(x);
    rect.top = static_cast<LONG>(y);
    rect.right = static_cast<LONG>(self->width + x);
    rect.bottom = static_cast<LONG>(y + height);
    return reinterpret_cast<Thiscall1>(kSetRect)(self->header, nullptr, reinterpret_cast<std::uint32_t>(&rect));
}

// Purpose: Constructors of the static picture controls (a GUI image, a GFX
// frame, a colour-indexed frame).
StaticFrameFields* __fastcall CStaticImage_Construct(StaticFrameFields* self, void*, std::uint32_t rect,
                                                     std::uint32_t image) {
    reinterpret_cast<Thiscall1>(kStaticControl)(reinterpret_cast<std::uintptr_t>(self), nullptr, rect);
    *reinterpret_cast<std::uint32_t*>(self->control) = vtables::kCStaticImage;
    self->value20 = image;
    return self;
}
StaticFrameFields* __fastcall CStaticFrame_Construct(StaticFrameFields* self, void*, std::uint32_t rect,
                                                     std::uint32_t file, std::uint32_t frame) {
    reinterpret_cast<Thiscall1>(kStaticControl)(reinterpret_cast<std::uintptr_t>(self), nullptr, rect);
    self->value20 = file;
    self->value24 = frame;
    *reinterpret_cast<std::uint32_t*>(self->control) = vtables::kCStaticFrame;
    return self;
}
StaticFrameFields* __fastcall CStaticColorIndexedFrame_Construct(StaticFrameFields* self, void*, std::uint32_t rect,
                                                                 std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    reinterpret_cast<Thiscall1>(kStaticControl)(reinterpret_cast<std::uintptr_t>(self), nullptr, rect);
    self->value28 = c;
    self->value20 = a;
    self->value24 = b;
    *reinterpret_cast<std::uint32_t*>(self->control) = vtables::kCStaticColorIndexedFrame;
    return self;
}

// Purpose: A vertical scroll bar was resized: places its arrow buttons at
// the ends and the track between them.
std::uint32_t __fastcall CVScrollBar_OnResized(std::uintptr_t self, void*, const RECT* rect, std::uint32_t) {
    const LONG arrow = static_cast<LONG>(kScrollArrowHeight);
    PlaceMember(self, -0x238, rect->left, rect->top, rect->right, rect->top + arrow);
    PlaceMember(self, -0x1b8, rect->left, rect->bottom - arrow, rect->right, rect->bottom);
    RECT& track = Field<RECT>(self, -0x24);
    const RECT now = *rect;
    track.left = now.left;
    track.top = now.top + arrow;
    track.right = now.right;
    track.bottom = now.bottom - arrow;
    return static_cast<std::uint32_t>(track.top);
}

// Purpose: A dropdown was resized: places its edit field, its button and
// its (closed) drop list.
std::uint32_t __fastcall CDropdown_OnResized(std::uintptr_t self, void*, const RECT* rect, std::uint32_t) {
    PlaceMember(self, -0x5b0, rect->left, rect->top, rect->right - kDropFieldGap, rect->bottom);
    PlaceMember(self, -0x520, rect->right - kDropButtonWidth, rect->top, rect->right, rect->bottom);
    return PlaceMember(self, -0x4a0, rect->left - kDropListOverhang, rect->bottom + kDropListTop,
                       rect->right - kDropListRightInset, rect->bottom + kDropListBottom);
}

// Purpose: An up-down control was resized: its number field on the left,
// the up and down buttons stacked on the right.
std::uint32_t __fastcall CUpDown_OnResized(std::uintptr_t self, void*, const RECT* rect, std::uint32_t) {
    {
        const RECT field{rect->left, rect->top, rect->right - kDropButtonWidth, rect->bottom};
        reinterpret_cast<SetRectFn>(kSetRect)(self - 0x16c, nullptr, &field);  // (the field's element itself)
    }
    const LONG upMiddle = (rect->top + rect->bottom) / 2;
    PlaceMember(self, -0x124, rect->right - kDropButtonWidth, rect->top, rect->right, upMiddle);
    const LONG downMiddle = (rect->top + rect->bottom) / 2;
    return PlaceMember(self, -0xa4, rect->right - kDropButtonWidth, downMiddle, rect->right, rect->bottom);
}

// Purpose: Destroys an edit field: releases its text, then the control.
std::uint32_t __fastcall CEditControl_Destroy(std::uintptr_t self, void*) {
    auto*& text = Field<std::uint8_t*>(self, -0x20);
    if (text != nullptr) {
        std::uint8_t& count = text[-1];
        if (count != 0 && count != 0xff) {
            --count;  // still shared
        } else {
            reinterpret_cast<void(__cdecl*)(void*)>(kOperatorDelete)(text - 2);
        }
    }
    text = nullptr;
    Field<std::uint32_t>(self, -0x1c) = 0;
    Field<std::uint32_t>(self, -0x18) = 0;
    return reinterpret_cast<Thiscall0>(kControlDestroy)(self - 0x34, nullptr);
}

// Purpose: Moves 16-bit characters inside a wide-character buffer (the edit
// field's text when inserting or deleting).
std::uint16_t* __fastcall UI_MoveWords(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count) {
    if (from < to && to < from + count) {
        for (std::uint32_t i = count; i > 0; --i) {
            to[i - 1] = from[i - 1];
        }
        return to;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        to[i] = from[i];
    }
    return to;
}

using ControlDrawFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, std::uint32_t, std::uint32_t, std::uint32_t);
using SetEnabledFn = std::uint32_t(__fastcall*)(ui::CControl*, void*, std::uint32_t);
using HasQueuedFn = std::uint32_t(__fastcall*)(const ScreenQueue*, void*);
using OnLButtonDownFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, Message);
using OnMovedFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, const RECT*, std::uint32_t);
using SetFlagFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, std::uint32_t);
using HeaderHeightFn = std::uint32_t(__fastcall*)(const ListViewHeader*, void*);
using ColumnCopyFn = ListColumn*(__fastcall*)(ListColumn*, void*, ListColumn*);
using LineCopyFn = ListLine*(__fastcall*)(ListLine*, void*, ListLine*);
using ColumnEqualFn = std::uint32_t(__fastcall*)(const ListColumn*, void*, const ListColumn*);
using SetPositionFn = std::uint32_t(__fastcall*)(ListColumn*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
using StaticImageFn = StaticFrameFields*(__fastcall*)(StaticFrameFields*, void*, std::uint32_t, std::uint32_t);
using StaticFrameFn = StaticFrameFields*(__fastcall*)(StaticFrameFields*, void*, std::uint32_t, std::uint32_t,
                                                      std::uint32_t);
using StaticColorFrameFn = StaticFrameFields*(__fastcall*)(StaticFrameFields*, void*, std::uint32_t, std::uint32_t,
                                                           std::uint32_t, std::uint32_t);
using MoveWordsFn = std::uint16_t*(__fastcall*)(std::uint16_t*, const std::uint16_t*, std::uint32_t);
using OnResizedFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, const RECT*, std::uint32_t);
using DestroyFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*);

S3RE_HOOK(0x004187d0, CControl_Draw, ControlDrawFn, Original_CControl_Draw, CControl_Draw);
S3RE_HOOK(0x00418810, CControl_SetEnabled, SetEnabledFn, Original_CControl_SetEnabled, CControl_SetEnabled);
S3RE_HOOK(0x00418330, CControl_SetTooltipTextId, SetEnabledFn, Original_CControl_SetTooltipTextId,
          CControl_SetTooltipTextId);
S3RE_HOOK(0x00417f50, CScreen_HasQueuedMessages, HasQueuedFn, Original_CScreen_HasQueuedMessages,
          CScreen_HasQueuedMessages);
S3RE_HOOK(0x00419b30, CEditControl_OnLButtonDown, OnLButtonDownFn, Original_CEditControl_OnLButtonDown,
          CEditControl_OnLButtonDown);
S3RE_HOOK(0x0041e9e0, CVScrollBar_OnMoved, OnMovedFn, Original_CVScrollBar_OnMoved, CVScrollBar_OnMoved);
S3RE_HOOK(0x00422490, CDropdown_SetFlag1F, SetFlagFn, Original_CDropdown_SetFlag1F, CDropdown_SetFlag1F);
S3RE_HOOK(0x00424590, CListView_GetHeaderHeight, HeaderHeightFn, Original_CListView_GetHeaderHeight,
          CListView_GetHeaderHeight);
S3RE_HOOK(0x004275a0, CColumn_CopyConstruct, ColumnCopyFn, Original_CColumn_CopyConstruct, CColumn_CopyConstruct);
S3RE_HOOK(0x00427600, CColumn_Assign, ColumnCopyFn, Original_CColumn_Assign, CColumn_Assign);
S3RE_HOOK(0x004277a0, CLine_CopyConstruct, LineCopyFn, Original_CLine_CopyConstruct, CLine_CopyConstruct);
S3RE_HOOK(0x00427800, CLine_Assign, LineCopyFn, Original_CLine_Assign, CLine_Assign);
S3RE_HOOK(0x00427830, CColumn_Equal, ColumnEqualFn, Original_CColumn_Equal, CColumn_Equal);
S3RE_HOOK(0x00427650, CColumn_SetPosition, SetPositionFn, Original_CColumn_SetPosition, CColumn_SetPosition);
S3RE_HOOK(0x0041d7a0, CStaticImage_Construct, StaticImageFn, Original_CStaticImage_Construct,
          CStaticImage_Construct);
S3RE_HOOK(0x0041d960, CStaticFrame_Construct, StaticFrameFn, Original_CStaticFrame_Construct,
          CStaticFrame_Construct);
S3RE_HOOK(0x0041db90, CStaticColorIndexedFrame_Construct, StaticColorFrameFn,
          Original_CStaticColorIndexedFrame_Construct, CStaticColorIndexedFrame_Construct);
S3RE_HOOK(0x00418cf0, UI_MoveWords, MoveWordsFn, Original_UI_MoveWords, UI_MoveWords);
S3RE_HOOK(0x0041e940, CVScrollBar_OnResized, OnResizedFn, Original_CVScrollBar_OnResized, CVScrollBar_OnResized);
S3RE_HOOK(0x004224c0, CDropdown_OnResized, OnResizedFn, Original_CDropdown_OnResized, CDropdown_OnResized);
S3RE_HOOK(0x00422d90, CUpDown_OnResized, OnResizedFn, Original_CUpDown_OnResized, CUpDown_OnResized);
S3RE_HOOK(0x00421f50, CEditControl_Destroy, DestroyFn, Original_CEditControl_Destroy, CEditControl_Destroy);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each case runs the original and ours on a random 0x1800-byte buffer; `this` lies 0x800 bytes in, vbtable pointers point at small tables
// inside the buffer, and the callees are recording stubs (returning a value
// fixed by the call; pointer arguments' first 16 bytes are recorded too).
// Both run on the same buffer, set up alike before each. Compared: the
// buffer, EAX and the stub calls.

namespace {

constexpr std::size_t kBytes = 0x1800;
constexpr std::size_t kThis = 0x800;
constexpr std::size_t kTable = 0x1700;  // the vbtables

struct CallLog {
    std::uintptr_t callee;
    std::uint32_t self;
    std::uint32_t args[4];
    std::uint8_t pointee[16];
    bool operator==(const CallLog& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

std::uintptr_t g_base = 0;
std::vector<CallLog>* g_log = nullptr;

std::uint32_t Normalize(std::uint32_t value) {
    return value - g_base < kBytes ? 0xbeef0000u + (value - static_cast<std::uint32_t>(g_base)) : value;
}

template <std::size_t>
using Dword = std::uint32_t;

template <std::uintptr_t Callee, std::size_t... I>
std::uint32_t __fastcall RecordingStub(std::uint32_t self, void*, Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    CallLog entry{Callee, Normalize(self), {}, {}};
    for (std::size_t i = 0; i < sizeof...(I) && i < 4; ++i) {
        entry.args[i] = Normalize(values[i]);
    }
    if (sizeof...(I) > 0 && values[0] - g_base < kBytes - 16) {
        std::memcpy(entry.pointee, reinterpret_cast<const void*>(values[0]), 16);
    }
    if constexpr (Callee == kSetRect) {
        // The rect is a local of the caller: its contents, not its address.
        std::memcpy(entry.pointee, reinterpret_cast<const void*>(values[0]), 16);
        entry.args[0] = 0;
    }
    g_log->push_back(entry);
    return static_cast<std::uint32_t>(Callee * 7 + g_log->size()) ^ Normalize(self);
}

// A stub that takes a 20-byte message by value (5 dwords).
template <std::uintptr_t Callee>
std::uint32_t __fastcall MessageStub(std::uint32_t self, void*, Message message) {
    CallLog entry{Callee, Normalize(self), {message.words[0], message.words[1], message.words[2], message.words[3]},
                  {}};
    std::memcpy(entry.pointee, &message.words[4], 4);
    g_log->push_back(entry);
    return static_cast<std::uint32_t>(Callee) ^ Normalize(self);
}

// operator delete (cdecl): recorded, nothing freed (the blocks are in the
// test buffer).
void __cdecl DeleteStub(void* block) {
    g_log->push_back(CallLog{kOperatorDelete, Normalize(reinterpret_cast<std::uint32_t>(block)), {}, {}});
}

void InstallStubs() {
    test::PatchJump(kDrawRect, reinterpret_cast<const void*>(&RecordingStub<kDrawRect, 0, 1, 2>));
    test::PatchJump(kGraphicsData, reinterpret_cast<const void*>(&RecordingStub<kGraphicsData>));
    test::PatchJump(kTextAddress, reinterpret_cast<const void*>(&RecordingStub<kTextAddress, 0, 1>));
    test::PatchJump(kAssignText, reinterpret_cast<const void*>(&RecordingStub<kAssignText, 0>));
    test::PatchJump(kElementOnLButtonDown, reinterpret_cast<const void*>(&MessageStub<kElementOnLButtonDown>));
    test::PatchJump(kControlOnLButtonDown, reinterpret_cast<const void*>(&MessageStub<kControlOnLButtonDown>));
    test::PatchJump(kGroupOnMoved, reinterpret_cast<const void*>(&RecordingStub<kGroupOnMoved, 0, 1>));
    test::PatchJump(kGroupSetFlag1F, reinterpret_cast<const void*>(&RecordingStub<kGroupSetFlag1F, 0>));
    test::PatchJump(kSetRect, reinterpret_cast<const void*>(&RecordingStub<kSetRect, 0>));
    test::PatchJump(kStaticControl, reinterpret_cast<const void*>(&RecordingStub<kStaticControl, 0>));
    test::PatchJump(kControlDestroy, reinterpret_cast<const void*>(&RecordingStub<kControlDestroy>));
    test::PatchJump(kOperatorDelete, reinterpret_cast<const void*>(&DeleteStub));
}

void RemoveStubs() {
    for (std::uintptr_t callee : {kOperatorDelete, kControlDestroy, kStaticControl, kSetRect, kGroupSetFlag1F, kGroupOnMoved, kControlOnLButtonDown,
                                  kElementOnLButtonDown, kAssignText, kTextAddress, kGraphicsData, kDrawRect}) {
        test::UnpatchJump(callee);
    }
}

// Writes a pointer to `target` (an offset in the buffer) at `offset`.
void Link(std::uint8_t* buffer, std::size_t offset, std::size_t target) {
    const auto p = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + target));
    std::memcpy(buffer + offset, &p, 4);
}

// Sets up a vbtable for the vbptr at this + delta: the table at kTable +
// slot, its entry [1] a small displacement (the virtual base stays in the
// buffer).
void VbTable(test::Rng& rng, std::uint8_t* buffer, std::int32_t delta, std::size_t slot) {
    Link(buffer, kThis + delta, kTable + slot);
    const std::int32_t displacement = -delta + rng.Range(-0x40, 0x40);
    std::memcpy(buffer + kTable + slot + 4, &displacement, 4);
}

struct Run {
    std::uint32_t eax;
    std::vector<CallLog> calls;
};

// Runs `call(base)` with the stubs installed (pointers into the buffer in
// EAX and the calls recorded as offsets).
template <typename Call>
Run RunOn(std::uint8_t* buffer, Call call) {
    Run run{};
    g_base = reinterpret_cast<std::uintptr_t>(buffer);
    g_log = &run.calls;
    run.eax = Normalize(call(reinterpret_cast<std::uintptr_t>(buffer)));
    g_log = nullptr;
    return run;
}

// One member's comparison: setup(rng, buffer) prepares a buffer, then
// `call(fn, base, arguments)` runs it.
template <typename Setup, typename CallA, typename CallB>
bool CompareMember(test::Context& ctx, const char* name, Setup setup, CallA callOriginal, CallB callOurs, int cases) {
    // Both versions run on the same buffer (set up alike before each), so
    // pointers into it need no translation.
    static std::uint8_t buffer[kBytes], afterOriginal[kBytes];
    InstallStubs();
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint64_t argumentSeed = ctx.rng().Next64();
        test::Rng fillA(seed), fillB(seed);
        setup(fillA, buffer);
        const Run ra = RunOn(buffer, [&](std::uintptr_t base) { return callOriginal(base, argumentSeed); });
        std::memcpy(afterOriginal, buffer, kBytes);
        setup(fillB, buffer);
        const Run rb = RunOn(buffer, [&](std::uintptr_t base) { return callOurs(base, argumentSeed); });
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT(ra.calls == rb.calls) ||
            !S3RE_EXPECT_MEM_EQ(afterOriginal, buffer, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
            ok = false;
        }
    }
    RemoveStubs();
    return ok;
}

void RandomBuffer(test::Rng& rng, std::uint8_t* buffer) {
    rng.Fill(buffer, kBytes);
}

// The members' comparisons; `wrong` replaces ours (for the harness check).
struct Members {
    ControlDrawFn draw = &CControl_Draw;
    SetEnabledFn setEnabled = &CControl_SetEnabled;
    SetEnabledFn setTooltip = &CControl_SetTooltipTextId;
    HasQueuedFn hasQueued = &CScreen_HasQueuedMessages;
    OnLButtonDownFn onLButtonDown = &CEditControl_OnLButtonDown;
    OnMovedFn onMoved = &CVScrollBar_OnMoved;
    SetFlagFn setFlag = &CDropdown_SetFlag1F;
    HeaderHeightFn headerHeight = &CListView_GetHeaderHeight;
    ColumnCopyFn columnCopy = &CColumn_CopyConstruct;
    ColumnCopyFn columnAssign = &CColumn_Assign;
    LineCopyFn lineCopy = &CLine_CopyConstruct;
    LineCopyFn lineAssign = &CLine_Assign;
    ColumnEqualFn columnEqual = &CColumn_Equal;
    SetPositionFn setPosition = &CColumn_SetPosition;
    StaticImageFn staticImage = &CStaticImage_Construct;
    StaticFrameFn staticFrame = &CStaticFrame_Construct;
    StaticColorFrameFn staticColorFrame = &CStaticColorIndexedFrame_Construct;
    MoveWordsFn moveWords = &UI_MoveWords;
    OnResizedFn scrollResized = &CVScrollBar_OnResized;
    OnResizedFn dropdownResized = &CDropdown_OnResized;
    OnResizedFn upDownResized = &CUpDown_OnResized;
    DestroyFn editDestroy = &CEditControl_Destroy;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    auto at = [](std::uintptr_t base, std::size_t offset) { return base + offset; };
    auto arg = [](std::uint64_t seed, int k) { return static_cast<std::uint32_t>(test::Rng(seed + k).Next32()); };
    auto small = [](std::uint64_t seed, int k) { return test::Rng(seed + k).Below(4); };

    // CControl::Draw: vbptr at this-0x38.
    CompareMember(
        ctx, "CControl::Draw", [](test::Rng& rng, std::uint8_t* buf) { RandomBuffer(rng, buf); VbTable(rng, buf, -0x38, 0); },
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CControl_Draw(at(base, kThis), nullptr, arg(s, 0), arg(s, 1), arg(s, 2));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return ours.draw(at(base, kThis), nullptr, arg(s, 0), arg(s, 1), arg(s, 2));
        },
        cases);
    CompareMember(
        ctx, "CControl::SetEnabled", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CControl_SetEnabled(reinterpret_cast<ui::CControl*>(at(base, kThis)), nullptr,
                                                small(s, 0) == 0 ? 0 : arg(s, 1)) & 0xFF;
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return ours.setEnabled(reinterpret_cast<ui::CControl*>(at(base, kThis)), nullptr,
                                   small(s, 0) == 0 ? 0 : arg(s, 1)) & 0xFF;
        },
        cases);
    // SetTooltipTextId: this = the control; vbptr at +4.
    CompareMember(
        ctx, "CControl::SetTooltipTextId",
        [](test::Rng& rng, std::uint8_t* buf) { RandomBuffer(rng, buf); VbTable(rng, buf, 4, 0x10); },
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CControl_SetTooltipTextId(reinterpret_cast<ui::CControl*>(at(base, kThis)), nullptr,
                                                      small(s, 0) == 0 ? 0 : arg(s, 1));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return ours.setTooltip(reinterpret_cast<ui::CControl*>(at(base, kThis)), nullptr,
                                   small(s, 0) == 0 ? 0 : arg(s, 1));
        },
        cases);
    CompareMember(
        ctx, "CScreen::HasQueuedMessages",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            if (rng.Chance(1, 2)) {
                std::memset(buf + kThis + 0x84, 0, 4);
            }
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return Original_CScreen_HasQueuedMessages(reinterpret_cast<const ScreenQueue*>(at(base, kThis)), nullptr);
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return ours.hasQueued(reinterpret_cast<const ScreenQueue*>(at(base, kThis)), nullptr);
        },
        cases);
    // CEditControl::OnLButtonDown: the disabled byte at this-0x64, vbptr at this-0x6c.
    CompareMember(
        ctx, "CEditControl::OnLButtonDown",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            VbTable(rng, buf, -0x6c, 0x20);
            buf[kThis - 0x64] = rng.Chance(1, 2) ? 0 : static_cast<std::uint8_t>(1 + rng.Below(255));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            const Message m{{arg(s, 0), arg(s, 1), arg(s, 2), arg(s, 3), arg(s, 4)}};
            return Original_CEditControl_OnLButtonDown(at(base, kThis), nullptr, m);
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            const Message m{{arg(s, 0), arg(s, 1), arg(s, 2), arg(s, 3), arg(s, 4)}};
            return ours.onLButtonDown(at(base, kThis), nullptr, m);
        },
        cases);
    // CVScrollBar::OnMoved: the rect in the buffer (offset 0x100).
    CompareMember(
        ctx, "CVScrollBar::OnMoved", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CVScrollBar_OnMoved(at(base, kThis), nullptr, reinterpret_cast<const RECT*>(at(base, 0x100)),
                                                arg(s, 0));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return ours.onMoved(at(base, kThis), nullptr, reinterpret_cast<const RECT*>(at(base, 0x100)), arg(s, 0));
        },
        cases);
    // CDropdown::SetFlag1F: vbptr at this-0x668 (this lies further in).
    CompareMember(
        ctx, "CDropdown::SetFlag1F",
        [](test::Rng& rng, std::uint8_t* buf) { RandomBuffer(rng, buf); VbTable(rng, buf, -0x668, 0x30); },
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CDropdown_SetFlag1F(at(base, kThis), nullptr, arg(s, 0));
        },
        [&](std::uintptr_t base, std::uint64_t s) { return ours.setFlag(at(base, kThis), nullptr, arg(s, 0)); },
        cases);
    CompareMember(
        ctx, "CListView::GetHeaderHeight",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            if (rng.Chance(1, 2)) {
                buf[0x100 + 0x4f2] = 0;
            }
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return Original_CListView_GetHeaderHeight(reinterpret_cast<const ListViewHeader*>(at(base, 0x100)), nullptr);
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return ours.headerHeight(reinterpret_cast<const ListViewHeader*>(at(base, 0x100)), nullptr);
        },
        cases);
    // The column/line copies: source at 0x100, destination 0x200 (or the
    // source itself for an assignment, sometimes).
    auto columnCall = [&](ColumnCopyFn fn, std::uintptr_t base, std::uint64_t s) {
        const std::size_t to = small(s, 0) == 0 ? 0x100 : 0x200;
        return reinterpret_cast<std::uint32_t>(fn(reinterpret_cast<ListColumn*>(at(base, to)), nullptr,
                                                  reinterpret_cast<ListColumn*>(at(base, 0x100))));
    };
    auto lineCall = [&](LineCopyFn fn, std::uintptr_t base, std::uint64_t s) {
        const std::size_t to = small(s, 0) == 0 ? 0x100 : 0x200;
        return reinterpret_cast<std::uint32_t>(
            fn(reinterpret_cast<ListLine*>(at(base, to)), nullptr, reinterpret_cast<ListLine*>(at(base, 0x100))));
    };
    CompareMember(
        ctx, "CColumn copy", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) { return columnCall(Original_CColumn_CopyConstruct, base, s | 1); },
        [&](std::uintptr_t base, std::uint64_t s) { return columnCall(ours.columnCopy, base, s | 1); }, cases);
    CompareMember(
        ctx, "CColumn assign", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) { return columnCall(Original_CColumn_Assign, base, s); },
        [&](std::uintptr_t base, std::uint64_t s) { return columnCall(ours.columnAssign, base, s); }, cases);
    CompareMember(
        ctx, "CLine copy", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) { return lineCall(Original_CLine_CopyConstruct, base, s | 1); },
        [&](std::uintptr_t base, std::uint64_t s) { return lineCall(ours.lineCopy, base, s | 1); }, cases);
    CompareMember(
        ctx, "CLine assign", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) { return lineCall(Original_CLine_Assign, base, s); },
        [&](std::uintptr_t base, std::uint64_t s) { return lineCall(ours.lineAssign, base, s); }, cases);
    CompareMember(
        ctx, "CColumn::operator==",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            if (rng.Chance(1, 2)) {
                std::memcpy(buf + 0x200, buf + 0x100, 4);
            }
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return Original_CColumn_Equal(reinterpret_cast<const ListColumn*>(at(base, 0x100)), nullptr,
                                          reinterpret_cast<const ListColumn*>(at(base, 0x200)));
        },
        [&](std::uintptr_t base, std::uint64_t) {
            return ours.columnEqual(reinterpret_cast<const ListColumn*>(at(base, 0x100)), nullptr,
                                    reinterpret_cast<const ListColumn*>(at(base, 0x200)));
        },
        cases);
    CompareMember(
        ctx, "CColumn::SetPosition",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            if (rng.Chance(1, 3)) {
                std::memset(buf + 0x100 + 0x1c, 0, 4);
            }
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return Original_CColumn_SetPosition(reinterpret_cast<ListColumn*>(at(base, 0x100)), nullptr, arg(s, 0),
                                                arg(s, 1), arg(s, 2));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return ours.setPosition(reinterpret_cast<ListColumn*>(at(base, 0x100)), nullptr, arg(s, 0), arg(s, 1),
                                    arg(s, 2));
        },
        cases);
    // The static controls: the rect argument points into the buffer.
    CompareMember(
        ctx, "CStaticImage", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(Original_CStaticImage_Construct(
                reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100), arg(s, 0)));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(
                ours.staticImage(reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100), arg(s, 0)));
        },
        cases);
    CompareMember(
        ctx, "CStaticFrame", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(Original_CStaticFrame_Construct(
                reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100), arg(s, 0), arg(s, 1)));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(ours.staticFrame(
                reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100), arg(s, 0), arg(s, 1)));
        },
        cases);
    CompareMember(
        ctx, "CStaticColorIndexedFrame", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(Original_CStaticColorIndexedFrame_Construct(
                reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100), arg(s, 0), arg(s, 1),
                arg(s, 2)));
        },
        [&](std::uintptr_t base, std::uint64_t s) {
            return reinterpret_cast<std::uint32_t>(
                ours.staticColorFrame(reinterpret_cast<StaticFrameFields*>(at(base, kThis)), nullptr, at(base, 0x100),
                                      arg(s, 0), arg(s, 1), arg(s, 2)));
        },
        cases);
    // UI_MoveWords: overlapping and separate ranges inside the buffer.
    auto moveCall = [&](MoveWordsFn fn, std::uintptr_t base, std::uint64_t s) {
        test::Rng r(s);
        const std::size_t from = 0x100 + 2 * r.Below(64);
        const std::size_t to = r.Chance(1, 3) ? 0x400 : from + 2 * static_cast<std::size_t>(r.Range(-20, 20));
        const std::uint32_t count = r.Below(40);
        return reinterpret_cast<std::uint32_t>(fn(reinterpret_cast<std::uint16_t*>(at(base, to)),
                                                  reinterpret_cast<const std::uint16_t*>(at(base, from)), count));
    };
    CompareMember(
        ctx, "UI_MoveWords", RandomBuffer,
        [&](std::uintptr_t base, std::uint64_t s) { return moveCall(Original_UI_MoveWords, base, s); },
        [&](std::uintptr_t base, std::uint64_t s) { return moveCall(ours.moveWords, base, s); }, cases);

    // The OnResized handlers: the rect at 0x100 (its coordinates sometimes
    // small, so that the halves round both ways), vbptrs near `this`.
    struct Resized {
        const char* name;
        OnResizedFn original;
        OnResizedFn ours;
        std::int32_t deltas[3];  // 0 = none
    };
    const Resized resized[] = {
        {"CVScrollBar::OnResized", Original_CVScrollBar_OnResized, ours.scrollResized, {-0x238, -0x1b8, 0}},
        {"CDropdown::OnResized", Original_CDropdown_OnResized, ours.dropdownResized, {-0x5b0, -0x520, -0x4a0}},
        {"CUpDown::OnResized", Original_CUpDown_OnResized, ours.upDownResized, {-0x124, -0xa4, 0}},
    };
    for (const Resized& r : resized) {
        CompareMember(
            ctx, r.name,
            [&r](test::Rng& rng, std::uint8_t* buf) {
                RandomBuffer(rng, buf);
                for (std::size_t k = 0; k < 3 && r.deltas[k] != 0; ++k) {
                    VbTable(rng, buf, r.deltas[k], 0x40 + 0x10 * k);
                }
                if (rng.Chance(1, 2)) {
                    for (int k = 0; k < 4; ++k) {
                        const std::int32_t v = rng.Range(-9, 9);
                        std::memcpy(buf + 0x100 + 4 * k, &v, 4);
                    }
                }
            },
            [&](std::uintptr_t base, std::uint64_t s) {
                return r.original(at(base, kThis), nullptr, reinterpret_cast<const RECT*>(at(base, 0x100)), arg(s, 0));
            },
            [&](std::uintptr_t base, std::uint64_t s) {
                return r.ours(at(base, kThis), nullptr, reinterpret_cast<const RECT*>(at(base, 0x100)), arg(s, 0));
            },
            cases);
    }
    // ~CEditControl: the text buffer in the buffer (or none), its count
    // byte 0, 0xff or shared.
    CompareMember(
        ctx, "CEditControl::~CEditControl",
        [](test::Rng& rng, std::uint8_t* buf) {
            RandomBuffer(rng, buf);
            const std::uint32_t text =
                rng.Chance(1, 4) ? 0 : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buf + 0x302));
            std::memcpy(buf + kThis - 0x20, &text, 4);
            const std::uint8_t counts[] = {0, 0xff, 1, 2, 0xfe};
            buf[0x301] = counts[rng.Below(5)];
        },
        [&](std::uintptr_t base, std::uint64_t) { return Original_CEditControl_Destroy(at(base, kThis), nullptr); },
        [&](std::uintptr_t base, std::uint64_t) { return ours.editDestroy(at(base, kThis), nullptr); }, cases);
}

// Wrong implementations.
std::uint32_t __fastcall WrongOnMoved(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta) {
    const std::uint32_t result = CVScrollBar_OnMoved(self, edx, rect, delta);
    ++Field<RECT>(self, -0x24).bottom;  // one pixel more
    return result;
}
std::uint16_t* __fastcall WrongMoveWords(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {  // always forwards
        to[i] = from[i];
    }
    return to;
}
std::uint32_t __fastcall WrongUpDownResized(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta) {
    RECT shifted = *rect;  // a negative odd sum's half rounded down instead of toward zero
    if (((shifted.top + shifted.bottom) & 1) != 0 && shifted.top + shifted.bottom < 0) {
        ++shifted.bottom;
    }
    return CUpDown_OnResized(self, edx, &shifted, delta);
}
ListLine* __fastcall WrongLineCopy(ListLine* self, void*, ListLine* other) {
    std::memcpy(self, other, sizeof(*self));  // copies the padding too
    other->row = 0;
    return self;
}

}  // namespace

S3RE_TEST(UiControlMembers_MatchOriginal) {
    CompareAll(ctx, Members{}, 300);
}

S3RE_TEST(UiControlMembers_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 4; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.onMoved = &WrongOnMoved;
        } else if (k == 3) {
            wrong.upDownResized = &WrongUpDownResized;
        } else if (k == 1) {
            wrong.moveWords = &WrongMoveWords;
        } else {
            wrong.lineCopy = &WrongLineCopy;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
