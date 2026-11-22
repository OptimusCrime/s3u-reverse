// CScreenElement, the base class of the menu and in-game UI controls
// (Dirk\dialog.cpp; RTTI name), its message plumbing, the dialog globals
// next to it, and the first members of a derived group class that reach it
// through a virtual base.
//
// CScreenElement layout (vtable 0x655a68; all thiscall):
//   +0x00 vtable
//   +0x04 dword, 0x1111 after construction: the background fill colour
//         (vf11, FillBackground)
//   +0x08 RECT: left, top, right, bottom (screen coordinates)
//   +0x18 parent: a container object; its own vbtable at +4 leads to its
//         CScreenElement-like virtual base, whose vtable is used
//   +0x1c byte: has the focus (set by vf03 SetFocus, cleared by vf04)
//   +0x1d byte: the mouse is inside (TrackMouse)
//   +0x1e byte: needs redrawing (set by construction and vf07, cleared after
//         drawing)
//   +0x1f byte: set by vf06; when set, vf11 does not fill the background
//         (probably "transparent")
// Virtual slots used here (byte offsets): 0x04 parent: remove child, 0x0c
// parent: child got the focus, 0x1c parent: invalidate, 0x20 accepts message
// (pointer to it), 0x24 handle message, 0x28 / 0x2c draw, 0x30..0x58
// per-message-type handlers (5-dword message by value; 0x54 without it),
// 0x5c / 0x60 mouse entered / left, 0x64 focus changed (bool), 0x68 parent
// changed (bool), 0x6c enable (bool), 0x70 moved (rect, delta), 0x74
// resized (rect, size delta), 0x78 a query (probably "can be enabled").
//
// Messages are 20-byte records passed by value: byte +0 type (1, 2, 3, 0x20,
// 0x30, 0x40: mouse messages, see WantsMessage), words +8 / +0xa x / y.
//
// The group class (our name; constructed by 0x416c70 and others) has a
// vbtable at this-0x7c (from the subobject these methods get) leading to its
// CScreenElement virtual base, a focus child at this-0x58 and a byte at
// this-0x78. The 0x429xxx functions are this-adjusting entry points of
// derived classes that forward to them.

#include "game/framework_layout.h"
#include "game/raw.h"
#include "game/screen_element_layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "watcom/ptr_vector.h"

#include <windows.h>
#include <ddraw.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace s3re::game {

using ui::ControlOf;
using ui::CControl;
using ui::ElementOf;
using ui::GroupOf;
using ui::CScreenElement;
using ui::CScreenGroup;

namespace {

constexpr std::uint32_t kScreenElementVtable = 0x00655a68;

// CGroupControl: the "this" its control part's methods get is 0x7c bytes
// before the group part's "this" (it is where the group part starts).
constexpr std::ptrdiff_t kGroupControlControl = 0x7c;
// CScreen: its group part's "this" is 0x18 bytes before its own.
constexpr std::ptrdiff_t kScreenGroup = 0x18;

void* Offset(void* p, std::ptrdiff_t delta) {
    return static_cast<std::uint8_t*>(p) + delta;
}

// The virtual base reached through a vbtable pointer field (the vbtable's
// second entry is the base's offset from the field).
void* VirtualBase(const std::int32_t* const& vbptr) {
    auto* at = reinterpret_cast<std::uint8_t*>(const_cast<const std::int32_t**>(&vbptr));
    return at + vbptr[1];
}

// The parent's CScreenElement part (the parent is a CScreenGroup
// (sub)object).
void* ParentBase(void* parent) {
    return VirtualBase(static_cast<CScreenGroup*>(parent)->vbptr);
}

// A group's CScreenElement virtual base.
void* GroupBase(void* group) {
    return VirtualBase(GroupOf(group)->vbptr);
}

// The control part of a CGroupControl (given its group part's "this").
CControl* GroupControlOf(void* group) {
    return ControlOf(Offset(group, -kGroupControlControl));
}

template <typename R, typename... A>
R CallVirtual(void* self, std::size_t slot, A... args) {
    using Fn = R(__fastcall*)(void*, void*, A...);
    auto* vtable = *reinterpret_cast<std::uint8_t**>(self);
    return (*reinterpret_cast<Fn*>(vtable + slot))(self, nullptr, args...);
}

}  // namespace

// A 20-byte input message, passed by value.
struct Message {
    std::uint8_t type;       // +0x00 (1, 2, 3, 0x20, 0x30, 0x40: mouse messages, see WantsMessage)
    std::uint8_t unk01[7];
    std::uint16_t x;         // +0x08
    std::uint16_t y;         // +0x0a
    std::uint32_t unk0c[2];
};
static_assert(sizeof(Message) == 20);
static_assert(offsetof(Message, x) == 8 && offsetof(Message, y) == 0xa);

using CtorFn = void*(__fastcall*)(void* self, void* edx);
using CtorRectFn = void*(__fastcall*)(void* self, void* edx, const RECT* rect);
using DtorFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DeletingDtorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using RectFn = RECT*(__fastcall*)(void* self, void* edx);
using SetRectFn = std::uint32_t(__fastcall*)(void* self, void* edx, const RECT* rect);
using MoveByFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t dx, std::int32_t dy);
using Arg1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using NoArgFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ContainsPointFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using MessageFn = std::uint32_t(__fastcall*)(void* self, void* edx, Message message);
using MessagePtrFn = std::uint32_t(__fastcall*)(void* self, void* edx, const Message* message);
using Draw2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t surface, const RECT* clip);
using Draw3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t surface, const RECT* rect, std::uint32_t c);
using IntersectFn = RECT*(__fastcall*)(void* self, void* edx, RECT* out, const RECT* a, const RECT* b);
using GlobalFn = std::uint32_t(__cdecl*)();
using CopyGlobalsFn = void*(__stdcall*)(void* out);
using VoidArg1Fn = void(__fastcall*)(void* self, void* edx, std::uint32_t a);
using TableFn = std::uint32_t(__stdcall*)(std::uint32_t index);
using MovedFn = std::uint32_t(__fastcall*)(void* self, void* edx, const RECT* rect, const std::int32_t* delta);
using Draw3PtrFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t surface, const RECT* a,
                                              const RECT* b);
using VoidNoArgFn = void(__fastcall*)(void* self, void* edx);

void* __fastcall CScreenElement_Construct(void* self, void* edx);
void* __fastcall CScreenElement_ConstructRect(void* self, void* edx, const RECT* rect);
std::uint32_t __fastcall CScreenElement_Destroy(void* self, void* edx);
void* __fastcall CScreenElement_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
RECT* __fastcall CScreenElement_Rect(void* self, void* edx);
std::uint32_t __fastcall CScreenElement_SetRect(void* self, void* edx, const RECT* rect);
std::uint32_t __fastcall CScreenElement_MoveBy(void* self, void* edx, std::int32_t dx, std::int32_t dy);
std::uint32_t __fastcall CScreenElement_SetParent(void* self, void* edx, std::uint32_t parent);
std::uint32_t __fastcall CScreenElement_ContainsPoint(void* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CScreenElement_NeedsRedraw(void* self, void* edx);
std::uint32_t __fastcall CScreenElement_IsQuery78Zero(void* self, void* edx);
std::uint32_t __fastcall CScreenElement_OnMessage(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenElement_WantsMessage(void* self, void* edx, const Message* message);
std::uint32_t __fastcall CScreenElement_DispatchMessage(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenElement_DrawClipped(void* self, void* edx, std::uint32_t surface, const RECT* clip);
std::uint32_t __fastcall CScreenElement_DrawRect(void* self, void* edx, std::uint32_t surface, const RECT* rect,
                                                 std::uint32_t unused);
RECT* __fastcall CScreenElement_IntersectRects(void* self, void* edx, RECT* out, const RECT* a, const RECT* b);
std::uint32_t __fastcall CScreenElement_SetFocus(void* self, void* edx, std::uint32_t unused);
std::uint32_t __fastcall CScreenElement_KillFocus(void* self, void* edx);
std::uint32_t __fastcall CScreenElement_Invalidate(void* self, void* edx);
std::uint32_t __fastcall CScreenElement_TrackMouse(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenElement_Enable(void* self, void* edx, std::uint32_t enable);
std::uint32_t __fastcall CScreenElement_ParentQuery78(void* self, void* edx);
void __fastcall CScreenElement_SetFlag1F(void* self, void* edx, std::uint32_t unused);
std::uint32_t __cdecl Dialog_Global7a823c();
void* __stdcall Dialog_CopyGlobals7a8020(void* out);
std::uint32_t __stdcall Dialog_Table7a809c(std::uint32_t index);
std::uint32_t __stdcall Dialog_Table7a8064(std::uint32_t index);
std::uint32_t __cdecl Dialog_Global7a8238();
std::uint32_t __cdecl Dialog_Global7a8240();
std::uint32_t __fastcall UiGroup_WantsMessage(void* self, void* edx, const Message* message);
std::uint32_t __fastcall UiGroup_SetFocus(void* self, void* edx, std::uint32_t child);
std::uint32_t __fastcall UiGroup_KillFocus(void* self, void* edx);
std::uint32_t __fastcall UiGroup_SetFocus_429d70(void* self, void* edx, std::uint32_t child);
std::uint32_t __fastcall UiGroup_WantsMessage_429df0(void* self, void* edx, const Message* message);
std::uint32_t __fastcall UiGroup_Enable_429fd0(void* self, void* edx, std::uint32_t enable);
std::uint32_t __fastcall CScreenGroup_SetFlag1F(void* self, void* edx, std::uint32_t argument);
std::uint32_t __fastcall CScreenGroup_OnMoved(void* self, void* edx, const RECT* rect, const std::int32_t* delta);
std::uint32_t __fastcall CScreenGroup_Draw(void* self, void* edx, std::uint32_t surface, const RECT* rectA,
                                           const RECT* rectB);
std::uint32_t __fastcall CScreenGroup_HandleMessage(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenGroup_OnMouseMove(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenGroup_SetFlag1F_429dd0(void* self, void* edx, std::uint32_t argument);
std::uint32_t __fastcall CScreenGroup_OnMoved_42a030(void* self, void* edx, const RECT* rect,
                                                     const std::int32_t* delta);
std::uint32_t __fastcall CScreenGroup_Draw_42a180(void* self, void* edx, std::uint32_t surface, const RECT* rectA,
                                                  const RECT* rectB);
std::uint32_t __fastcall CScreenGroup_HandleMessage_429e10(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreen_HandleMessage(void* self, void* edx, Message message);
void __fastcall CControl_DeleteTooltip(void* control, void* edx);
std::uint32_t __fastcall CControl_OnMessage10(void* self, void* edx, Message message);
std::uint32_t __fastcall CControl_OnMouseMove(void* self, void* edx, Message message);
std::uint32_t __fastcall CControl_OnMouseEnter(void* self, void* edx, Message message);
std::uint32_t __fastcall CControl_OnMouseLeave(void* self, void* edx, Message message);
std::uint32_t __fastcall CGroupControl_OnMessage10(void* self, void* edx, Message message);
std::uint32_t __fastcall CGroupControl_OnMouseMove(void* self, void* edx, Message message);
std::uint32_t __fastcall CGroupControl_OnMouseEnter(void* self, void* edx, Message message);
std::uint32_t __fastcall CGroupControl_OnMouseLeave(void* self, void* edx, Message message);
std::uint32_t __fastcall CScreenGroup_OnEnable(void* self, void* edx, std::uint32_t enable);
std::uint32_t __fastcall CControl_OnEnable(void* self, void* edx, std::uint32_t enable);
std::uint32_t __fastcall CGroupControl_OnEnable(void* self, void* edx, std::uint32_t enable);
std::uint32_t __fastcall CScreenElement_FillBackground(void* self, void* edx, void* surface, const RECT* rect);
std::uint32_t __fastcall CScreenGroup_FillBackground(void* self, void* edx, void* surface, const RECT* rect);
std::uint32_t __fastcall CScreenGroup_FillBackground_429e50(void* self, void* edx, void* surface, const RECT* rect);
std::uint32_t __fastcall DirectX_CSurface_DDBlt(void* self, void* edx, const RECT* dst, void* src,
                                                const RECT* srcRect, std::uint32_t flags, void* fx);

// ---------------------------------------------------------------------------
// Construction and destruction
// ---------------------------------------------------------------------------

namespace {

void InitScreenElement(void* self) {
    CScreenElement* element = ElementOf(self);
    element->background = 0x1111;
    element->parent = 0;
    element->has_focus = 0;
    element->mouse_inside = 0;
    element->needs_redraw = 1;
    element->vtable = kScreenElementVtable;
}

}  // namespace

S3RE_HOOK(0x00415610, CScreenElement_Construct, CtorFn, Original_CScreenElement_Construct, CScreenElement_Construct);

// Purpose: Constructor of a UI element with an empty rectangle.
void* __fastcall CScreenElement_Construct(void* self, void* /*edx*/) {
    InitScreenElement(self);
    ElementOf(self)->flag1f = 0;
    std::memset(&ElementOf(self)->rect, 0, sizeof(RECT));
    return self;
}

S3RE_HOOK(0x00415660, CScreenElement_ConstructRect, CtorRectFn, Original_CScreenElement_ConstructRect,
          CScreenElement_ConstructRect);

// Purpose: Constructor of a UI element at a given screen rectangle. Byte
// +0x1f is left as it was (the original doesn't set it here).
void* __fastcall CScreenElement_ConstructRect(void* self, void* /*edx*/, const RECT* rect) {
    ElementOf(self)->background = 0x1111;
    std::memcpy(&ElementOf(self)->rect, rect, sizeof(RECT));
    InitScreenElement(self);
    return self;
}

S3RE_HOOK(0x004156b0, CScreenElement_Destroy, DtorFn, Original_CScreenElement_Destroy, CScreenElement_Destroy);

// Purpose: Destructor of a UI element: removes it from its parent.
std::uint32_t __fastcall CScreenElement_Destroy(void* self, void* /*edx*/) {
    ElementOf(self)->vtable = kScreenElementVtable;
    void* parent = reinterpret_cast<void*>(ElementOf(self)->parent);
    if (parent == nullptr) {
        return reinterpret_cast<std::uint32_t>(self);
    }
    return CallVirtual<std::uint32_t>(parent, 0x04, self);
}

S3RE_HOOK(0x00415640, CScreenElement_DeletingDestroy, DeletingDtorFn, Original_CScreenElement_DeletingDestroy,
          CScreenElement_DeletingDestroy);

// Purpose: The "delete this element" entry of the vtable: destroys it and
// frees it if asked to.
void* __fastcall CScreenElement_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CScreenElement_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(self);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Rectangle
// ---------------------------------------------------------------------------

S3RE_HOOK(0x004156d0, CScreenElement_Rect, RectFn, Original_CScreenElement_Rect, CScreenElement_Rect);

// Purpose: The element's screen rectangle.
RECT* __fastcall CScreenElement_Rect(void* self, void* /*edx*/) {
    return &ElementOf(self)->rect;
}

S3RE_HOOK(0x004156e0, CScreenElement_SetRect, SetRectFn, Original_CScreenElement_SetRect, CScreenElement_SetRect);

// Purpose: Moves and/or resizes an element and tells it (virtual "moved"
// and "resized" handlers, with the change). EAX: the "resized" handler's
// result, or 0.
std::uint32_t __fastcall CScreenElement_SetRect(void* self, void* /*edx*/, const RECT* rect) {
    RECT& own = ElementOf(self)->rect;
    // Everything is read before the copy, as in the original (rect may
    // point at the element's own rectangle).
    const std::int32_t newLeft = rect->left;
    const std::int32_t newTop = rect->top;
    std::int32_t move[2] = {newLeft - own.left, newTop - own.top};
    std::int32_t size[2] = {(rect->right - own.right) - newLeft + own.left,
                            (rect->bottom - own.bottom) - rect->top + own.top};
    own = *rect;
    if (move[0] != 0 || move[1] != 0) {
        CallVirtual<std::uint32_t>(self, 0x70, rect, static_cast<void*>(move));
    }
    if (size[0] != 0 || size[1] != 0) {
        return CallVirtual<std::uint32_t>(self, 0x74, rect, static_cast<void*>(size));
    }
    return 0;
}

S3RE_HOOK(0x00415780, CScreenElement_MoveBy, MoveByFn, Original_CScreenElement_MoveBy, CScreenElement_MoveBy);

// Purpose: Moves an element by an offset.
std::uint32_t __fastcall CScreenElement_MoveBy(void* self, void* /*edx*/, std::int32_t dx, std::int32_t dy) {
    const RECT& own = ElementOf(self)->rect;
    const RECT moved{own.left + dx, own.top + dy, own.right + dx, own.bottom + dy};
    return CScreenElement_SetRect(self, nullptr, &moved);
}

S3RE_HOOK(0x00415800, CScreenElement_ContainsPoint, ContainsPointFn, Original_CScreenElement_ContainsPoint,
          CScreenElement_ContainsPoint);

// Purpose: Whether a screen point (16-bit coordinates) lies inside the
// element.
std::uint32_t __fastcall CScreenElement_ContainsPoint(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const POINT point{static_cast<LONG>(x & 0xffff), static_cast<LONG>(y & 0xffff)};
    return PtInRect(&ElementOf(self)->rect, point) != 0 ? 1 : 0;
}

S3RE_HOOK(0x00415b20, CScreenElement_IntersectRects, IntersectFn, Original_CScreenElement_IntersectRects,
          CScreenElement_IntersectRects);

// Purpose: The overlap of two rectangles (a helper that ignores the
// element it is called on).
RECT* __fastcall CScreenElement_IntersectRects(void* /*self*/, void* /*edx*/, RECT* out, const RECT* a,
                                               const RECT* b) {
    RECT overlap;
    IntersectRect(&overlap, a, b);
    *out = overlap;
    return out;
}

// ---------------------------------------------------------------------------
// Parent, focus, redraw
// ---------------------------------------------------------------------------

S3RE_HOOK(0x004157d0, CScreenElement_SetParent, Arg1Fn, Original_CScreenElement_SetParent,
          CScreenElement_SetParent);

// Purpose: Puts the element into a container (or takes it out with null)
// and tells it.
std::uint32_t __fastcall CScreenElement_SetParent(void* self, void* /*edx*/, std::uint32_t parent) {
    if (ElementOf(self)->parent == parent) {
        return parent;
    }
    ElementOf(self)->parent = parent;
    return CallVirtual<std::uint32_t>(self, 0x68, static_cast<std::uint32_t>(parent != 0 ? 1 : 0));
}

S3RE_HOOK(0x00415830, CScreenElement_NeedsRedraw, NoArgFn, Original_CScreenElement_NeedsRedraw,
          CScreenElement_NeedsRedraw);

// Purpose: Whether the element must be drawn again. Only AL is the result.
std::uint32_t __fastcall CScreenElement_NeedsRedraw(void* self, void* /*edx*/) {
    return ElementOf(self)->needs_redraw;
}

S3RE_HOOK(0x00415840, CScreenElement_IsQuery78Zero, NoArgFn, Original_CScreenElement_IsQuery78Zero,
          CScreenElement_IsQuery78Zero);

// Purpose: The negation of the element's virtual query 0x78 (probably "is
// it disabled").
std::uint32_t __fastcall CScreenElement_IsQuery78Zero(void* self, void* /*edx*/) {
    return CallVirtual<std::uint32_t>(self, 0x78) == 0 ? 1 : 0;
}

S3RE_HOOK(0x00415ba0, CScreenElement_SetFocus, Arg1Fn, Original_CScreenElement_SetFocus, CScreenElement_SetFocus);

// Purpose: Gives the element the keyboard focus: tells the parent, then the
// element itself.
std::uint32_t __fastcall CScreenElement_SetFocus(void* self, void* /*edx*/, std::uint32_t /*unused*/) {
    ElementOf(self)->has_focus = 1;
    if (void* parent = reinterpret_cast<void*>(ElementOf(self)->parent); parent != nullptr) {
        CallVirtual<std::uint32_t>(ParentBase(parent), 0x0c, self);
    }
    return CallVirtual<std::uint32_t>(self, 0x64, 1u);
}

S3RE_HOOK(0x00415bd0, CScreenElement_KillFocus, NoArgFn, Original_CScreenElement_KillFocus,
          CScreenElement_KillFocus);

// Purpose: Takes the focus away from the element (and tells it, if it had
// it). EAX is not used by callers.
std::uint32_t __fastcall CScreenElement_KillFocus(void* self, void* /*edx*/) {
    std::uint32_t result = 0;
    if (ElementOf(self)->has_focus != 0) {
        result = CallVirtual<std::uint32_t>(self, 0x64, 0u);
    }
    ElementOf(self)->has_focus = 0;
    return result;
}

S3RE_HOOK(0x00415bf0, CScreenElement_Invalidate, NoArgFn, Original_CScreenElement_Invalidate,
          CScreenElement_Invalidate);

// Purpose: Marks the element for redrawing and passes that on to its
// parent.
std::uint32_t __fastcall CScreenElement_Invalidate(void* self, void* /*edx*/) {
    ElementOf(self)->needs_redraw = 1;
    void* parent = reinterpret_cast<void*>(ElementOf(self)->parent);
    if (parent == nullptr) {
        return 0;
    }
    return CallVirtual<std::uint32_t>(ParentBase(parent), 0x1c);
}

S3RE_HOOK(0x00415ca0, CScreenElement_Enable, Arg1Fn, Original_CScreenElement_Enable, CScreenElement_Enable);

// Purpose: Probably enables (if its query 0x78 allows it) or disables the
// element.
std::uint32_t __fastcall CScreenElement_Enable(void* self, void* /*edx*/, std::uint32_t enable) {
    if (static_cast<std::uint8_t>(enable) == 0) {
        return CallVirtual<std::uint32_t>(self, 0x6c, 0u);
    }
    const std::uint32_t allowed = CallVirtual<std::uint32_t>(self, 0x78);
    if (allowed == 0) {
        return allowed;
    }
    return CallVirtual<std::uint32_t>(self, 0x6c, 1u);
}

S3RE_HOOK(0x004160d0, CScreenElement_ParentQuery78, NoArgFn, Original_CScreenElement_ParentQuery78,
          CScreenElement_ParentQuery78);

// Purpose: Asks the parent the same query 0x78 (0 without a parent).
std::uint32_t __fastcall CScreenElement_ParentQuery78(void* self, void* /*edx*/) {
    void* parent = reinterpret_cast<void*>(ElementOf(self)->parent);
    if (parent == nullptr) {
        return 0;
    }
    return CallVirtual<std::uint32_t>(ParentBase(parent), 0x78);
}

S3RE_HOOK(0x00416c60, CScreenElement_SetFlag1F, VoidArg1Fn, Original_CScreenElement_SetFlag1F,
          CScreenElement_SetFlag1F);

// Purpose: Sets byte +0x1f of the element (meaning unknown; the argument is
// ignored).
void __fastcall CScreenElement_SetFlag1F(void* self, void* /*edx*/, std::uint32_t /*unused*/) {
    ElementOf(self)->flag1f = 1;
}

// ---------------------------------------------------------------------------
// Messages and drawing
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00415850, CScreenElement_OnMessage, MessageFn, Original_CScreenElement_OnMessage,
          CScreenElement_OnMessage);

// Purpose: Entry point for an input message: the element takes it if its
// "accepts" check agrees, then handles it.
std::uint32_t __fastcall CScreenElement_OnMessage(void* self, void* /*edx*/, Message message) {
    ElementOf(self)->has_focus = 1;
    const std::uint32_t accepted = CallVirtual<std::uint32_t>(self, 0x20, static_cast<const void*>(&message));
    if (static_cast<std::uint8_t>(accepted) == 0) {
        return accepted;
    }
    return CallVirtual<std::uint32_t>(self, 0x24, message);
}

S3RE_HOOK(0x00415890, CScreenElement_WantsMessage, MessagePtrFn, Original_CScreenElement_WantsMessage,
          CScreenElement_WantsMessage);

// Purpose: Whether a message concerns this element: always for the types
// 1, 2, 3, 0x20, 0x30 and 0x40, otherwise if its position lies inside.
// Only AL is the result.
std::uint32_t __fastcall CScreenElement_WantsMessage(void* self, void* /*edx*/, const Message* message) {
    switch (message->type) {
        case 1: case 2: case 3: case 0x30: case 0x40: case 0x20:
            return 1;
        default:
            return CScreenElement_ContainsPoint(self, nullptr, message->x, message->y);
    }
}

S3RE_HOOK(0x004158d0, CScreenElement_DispatchMessage, MessageFn, Original_CScreenElement_DispatchMessage,
          CScreenElement_DispatchMessage);

// Purpose: Passes a message to the element's handler for its type (one
// virtual function per type); 0 for types it has no handler for.
std::uint32_t __fastcall CScreenElement_DispatchMessage(void* self, void* /*edx*/, Message message) {
    std::size_t slot = 0;
    switch (message.type) {
        case 0x01: slot = 0x44; break;
        case 0x02: slot = 0x48; break;
        case 0x03: slot = 0x4c; break;
        case 0x10: slot = 0x30; break;
        case 0x12: slot = 0x34; break;
        case 0x13: slot = 0x38; break;
        case 0x15: slot = 0x3c; break;
        case 0x16: slot = 0x40; break;
        case 0x20: slot = 0x50; break;
        case 0x30: return CallVirtual<std::uint32_t>(self, 0x54);
        case 0x40: slot = 0x58; break;
        default:
            // xor al, al on EAX = this.
            return reinterpret_cast<std::uint32_t>(self) & ~0xffu;
    }
    return CallVirtual<std::uint32_t>(self, slot, message);
}

S3RE_HOOK(0x00415c10, CScreenElement_TrackMouse, MessageFn, Original_CScreenElement_TrackMouse,
          CScreenElement_TrackMouse);

// Purpose: Notices the mouse entering or leaving the element (from a
// message's position) and calls the matching handler once.
std::uint32_t __fastcall CScreenElement_TrackMouse(void* self, void* /*edx*/, Message message) {
    const std::uint32_t x = message.x;
    const std::uint32_t y = message.y;
    const bool inside = static_cast<std::uint8_t>(CScreenElement_ContainsPoint(self, nullptr, x, y)) != 0;
    std::uint8_t& mouseInside = ElementOf(self)->mouse_inside;
    if (inside) {
        if (mouseInside != 0) {
            return mouseInside;
        }
        mouseInside = 1;
        return CallVirtual<std::uint32_t>(self, 0x5c, message);
    }
    if (mouseInside != 1) {
        return mouseInside;
    }
    mouseInside = 0;
    return CallVirtual<std::uint32_t>(self, 0x60, message);
}

S3RE_HOOK(0x00415aa0, CScreenElement_DrawClipped, Draw2Fn, Original_CScreenElement_DrawClipped,
          CScreenElement_DrawClipped);

// Purpose: Draws the part of the element inside a clip rectangle (if any),
// then marks it as drawn. EAX: the draw handler's result, or IsRectEmpty's.
std::uint32_t __fastcall CScreenElement_DrawClipped(void* self, void* /*edx*/, std::uint32_t surface,
                                                    const RECT* clip) {
    RECT visible;
    IntersectRect(&visible, clip, CScreenElement_Rect(self, nullptr));
    std::uint32_t result = static_cast<std::uint32_t>(IsRectEmpty(&visible));
    if (result == 0) {
        result = CallVirtual<std::uint32_t>(self, 0x28, surface, static_cast<const void*>(&visible),
                                            static_cast<const void*>(&visible));
    }
    ElementOf(self)->needs_redraw = 0;
    return result;
}

S3RE_HOOK(0x00415af0, CScreenElement_DrawRect, Draw3Fn, Original_CScreenElement_DrawRect, CScreenElement_DrawRect);

// Purpose: Draws the element into a rectangle unless it is empty, then
// marks it as drawn.
std::uint32_t __fastcall CScreenElement_DrawRect(void* self, void* /*edx*/, std::uint32_t surface,
                                                 const RECT* rect, std::uint32_t /*unused*/) {
    std::uint32_t result = static_cast<std::uint32_t>(IsRectEmpty(rect));
    if (result == 0) {
        result = CallVirtual<std::uint32_t>(self, 0x2c, surface, rect);
    }
    ElementOf(self)->needs_redraw = 0;
    return result;
}

// ---------------------------------------------------------------------------
// Dialog globals (0x7a8020..0x7a8240; probably fonts and colours of the
// menu screens, set up by the dialog initialization 0x4168d0)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00416870, Dialog_Global7a823c, GlobalFn, Original_Dialog_Global7a823c, Dialog_Global7a823c);

// Purpose: Returns a dialog-system global (0x7a823c; probably a font or
// palette handle).
std::uint32_t __cdecl Dialog_Global7a823c() {
    return RawAtAddress<std::uint32_t>(0x007a823c);
}

S3RE_HOOK(0x004168c0, Dialog_Global7a8238, GlobalFn, Original_Dialog_Global7a8238, Dialog_Global7a8238);

// Purpose: Returns a dialog-system global (0x7a8238).
std::uint32_t __cdecl Dialog_Global7a8238() {
    return RawAtAddress<std::uint32_t>(0x007a8238);
}

S3RE_HOOK(0x00416c50, Dialog_Global7a8240, GlobalFn, Original_Dialog_Global7a8240, Dialog_Global7a8240);

// Purpose: Returns a dialog-system global (0x7a8240; a GDI object that
// 0x416760 deletes).
std::uint32_t __cdecl Dialog_Global7a8240() {
    return RawAtAddress<std::uint32_t>(0x007a8240);
}

S3RE_HOOK(0x00416880, Dialog_CopyGlobals7a8020, CopyGlobalsFn, Original_Dialog_CopyGlobals7a8020,
          Dialog_CopyGlobals7a8020);

// Purpose: Copies a 32-byte block of dialog settings (at 0x7a8020) out.
void* __stdcall Dialog_CopyGlobals7a8020(void* out) {
    auto* to = static_cast<std::uint32_t*>(out);
    for (int i = 0; i < 8; ++i) {
        to[i] = RawAtAddress<std::uint32_t>(0x007a8020 + 4 * i);
    }
    return out;
}

S3RE_HOOK(0x004168a0, Dialog_Table7a809c, TableFn, Original_Dialog_Table7a809c, Dialog_Table7a809c);

// Purpose: An entry of a dialog-system table (0x7a809c; probably fonts or
// colours by index).
std::uint32_t __stdcall Dialog_Table7a809c(std::uint32_t index) {
    return RawAtAddress<std::uint32_t>(0x007a809c + index * 4);
}

S3RE_HOOK(0x004168b0, Dialog_Table7a8064, TableFn, Original_Dialog_Table7a8064, Dialog_Table7a8064);

// Purpose: An entry of another dialog-system table (0x7a8064, 14 entries).
std::uint32_t __stdcall Dialog_Table7a8064(std::uint32_t index) {
    return RawAtAddress<std::uint32_t>(0x007a8064 + index * 4);
}

// ---------------------------------------------------------------------------
// Group class (virtual base CScreenElement through the vbtable at this-0x7c)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00417100, UiGroup_WantsMessage, MessagePtrFn, Original_UiGroup_WantsMessage, UiGroup_WantsMessage);

// Purpose: Whether a message concerns a group: if it concerns its element
// part, or unless its byte -0x78 (probably "only inside") is set. AL only.
std::uint32_t __fastcall UiGroup_WantsMessage(void* self, void* /*edx*/, const Message* message) {
    void* base = GroupBase(self);
    if (static_cast<std::uint8_t>(CScreenElement_WantsMessage(base, nullptr, message)) != 0) {
        return 1;
    }
    return GroupOf(self)->only_inside != 0 ? 0 : 1;
}

S3RE_HOOK(0x00417650, UiGroup_SetFocus, Arg1Fn, Original_UiGroup_SetFocus, UiGroup_SetFocus);

// Purpose: Gives a group the focus and records which child has it,
// telling the previous child (virtual 0x10).
std::uint32_t __fastcall UiGroup_SetFocus(void* self, void* /*edx*/, std::uint32_t child) {
    const std::uint32_t result = CScreenElement_SetFocus(GroupBase(self), nullptr, child);
    void* old = reinterpret_cast<void*>(GroupOf(self)->focus);
    if (reinterpret_cast<std::uint32_t>(old) == child) {
        return result;
    }
    GroupOf(self)->focus = child;
    if (old == nullptr) {
        return result;
    }
    return CallVirtual<std::uint32_t>(old, 0x10);
}

S3RE_HOOK(0x00417680, UiGroup_KillFocus, NoArgFn, Original_UiGroup_KillFocus, UiGroup_KillFocus);

// Purpose: Takes the focus from a group and from its focused child. Only AL
// of EAX is defined.
std::uint32_t __fastcall UiGroup_KillFocus(void* self, void* /*edx*/) {
    void* old = reinterpret_cast<void*>(GroupOf(self)->focus);
    GroupOf(self)->focus = 0;
    if (old != nullptr) {
        CallVirtual<std::uint32_t>(old, 0x10);
    }
    return CScreenElement_KillFocus(GroupBase(self), nullptr);
}

S3RE_HOOK(0x00429d70, UiGroup_SetFocus_429d70, Arg1Fn, Original_UiGroup_SetFocus_429d70, UiGroup_SetFocus_429d70);

// Purpose: A derived class's copy of the group's SetFocus (same object
// pointer).
std::uint32_t __fastcall UiGroup_SetFocus_429d70(void* self, void* /*edx*/, std::uint32_t child) {
    return UiGroup_SetFocus(self, nullptr, child);
}

S3RE_HOOK(0x00429df0, UiGroup_WantsMessage_429df0, MessagePtrFn, Original_UiGroup_WantsMessage_429df0,
          UiGroup_WantsMessage_429df0);

// Purpose: A derived class's copy of the group's WantsMessage.
std::uint32_t __fastcall UiGroup_WantsMessage_429df0(void* self, void* /*edx*/, const Message* message) {
    return UiGroup_WantsMessage(self, nullptr, message);
}

S3RE_HOOK(0x00429fd0, UiGroup_Enable_429fd0, Arg1Fn, Original_UiGroup_Enable_429fd0, UiGroup_Enable_429fd0);

// Purpose: A derived class's Enable, forwarded to its CScreenElement part
// (vbtable at this-0xb4: the vbptr of the control part whose "this" is
// this-0x7c, as in CGroupControl).
std::uint32_t __fastcall UiGroup_Enable_429fd0(void* self, void* /*edx*/, std::uint32_t enable) {
    return CScreenElement_Enable(VirtualBase(GroupControlOf(self)->vbptr), nullptr, enable);
}

// CStaticControl (RTTI name; vtable 0x6560d8): a CScreenElement with its
// own vtable (probably static text or pictures in the menus).

constexpr std::uint32_t kCStaticControlVtable = 0x006560d8;
void* __fastcall CStaticControl_Construct(void* self, void* edx, const RECT* rect);

S3RE_HOOK(0x0041c3c0, CStaticControl_Construct, CtorRectFn, Original_CStaticControl_Construct,
          CStaticControl_Construct);

// Purpose: Constructor of a static control (probably a text or picture
// element of a menu) at a screen rectangle.
void* __fastcall CStaticControl_Construct(void* self, void* /*edx*/, const RECT* rect) {
    CScreenElement_ConstructRect(self, nullptr, rect);
    ElementOf(self)->vtable = kCStaticControlVtable;
    return self;
}

// ---------------------------------------------------------------------------
// CScreenGroup: the container of child elements (the "group class" above;
// RTTI name CScreenGroup). Its children are a Watcom pointer vector at
// this-0x74 (watcom/ptr_vector.h; length at this-0x60), its CScreenElement
// base is reached through the vbtable at this-0x7c, a child that has
// captured the input is at this-0x50 (our reading of 0x417130) and the
// focused child at this-0x58. Every access to a child goes through the
// vector's operator[] bounds check, inlined by the compiler; ours use
// PtrVector_At, which behaves the same (an index can only be out of range
// when a child's virtual call changed the vector).
// ---------------------------------------------------------------------------

namespace {

watcom::PtrVector* Children(void* group) {
    return &GroupOf(group)->children;
}

void* Child(void* group, std::uint32_t index) {
    return reinterpret_cast<void*>(watcom::PtrVector_At(Children(group), static_cast<std::int32_t>(index)));
}

// The loops re-read the length (this-0x60) before every iteration.
std::uint32_t ChildCount(void* group) {
    return GroupOf(group)->children.length;
}

}  // namespace

S3RE_HOOK(0x00417be0, CScreenGroup_SetFlag1F, Arg1Fn, Original_CScreenGroup_SetFlag1F, CScreenGroup_SetFlag1F);

// Purpose: Sets the group's flag +0x1f (vf06, meaning unknown) and passes
// the call on to every child. EAX: the last child's result, or 0.
std::uint32_t __fastcall CScreenGroup_SetFlag1F(void* self, void* /*edx*/, std::uint32_t argument) {
    CScreenElement_SetFlag1F(GroupBase(self), nullptr, argument);
    std::uint32_t result = 0;
    for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
        result = CallVirtual<std::uint32_t>(Child(self, i), 0x18, argument);
    }
    return result;
}

S3RE_HOOK(0x00417ab0, CScreenGroup_OnMoved, MovedFn, Original_CScreenGroup_OnMoved, CScreenGroup_OnMoved);

// Purpose: The "moved" handler of a group: moves every child by the same
// offset. The base's handler (0x45c380) is empty. EAX: the last child's
// MoveBy result, or 0.
std::uint32_t __fastcall CScreenGroup_OnMoved(void* self, void* /*edx*/, const RECT* /*rect*/,
                                              const std::int32_t* delta) {
    std::uint32_t result = 0;
    for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
        result = CScreenElement_MoveBy(Child(self, i), nullptr, delta[0], delta[1]);
    }
    return result;
}

S3RE_HOOK(0x00417480, CScreenGroup_Draw, Draw3PtrFn, Original_CScreenGroup_Draw, CScreenGroup_Draw);

// Purpose: Draws a group: its own background, then every child clipped to
// the area being drawn (the first or second rectangle, by byte -0x78), then
// clears the group's "needs redraw" byte. EAX: the child count.
std::uint32_t __fastcall CScreenGroup_Draw(void* self, void* /*edx*/, std::uint32_t surface, const RECT* rectA,
                                           const RECT* rectB) {
    CScreenElement_DrawRect(GroupBase(self), nullptr, surface, rectA, reinterpret_cast<std::uint32_t>(rectB));
    for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
        RECT visible;
        const RECT* area = GroupOf(self)->only_inside != 0 ? rectA : rectB;
        IntersectRect(&visible, area, CScreenElement_Rect(Child(self, i), nullptr));
        CallVirtual<std::uint32_t>(Child(self, i), 0x28, surface, static_cast<const void*>(&visible), rectB);
    }
    ElementOf(GroupBase(self))->needs_redraw = 0;
    return ChildCount(self);
}

S3RE_HOOK(0x00417130, CScreenGroup_HandleMessage, MessageFn, Original_CScreenGroup_HandleMessage,
          CScreenGroup_HandleMessage);

// Purpose: Message handling of a group: a child that captured the input
// gets the message first; otherwise the group's own handlers, then the
// focused child (types 1..3, probably keys), all children (0x30, 0x16) or
// the first child that accepts it (other types). AL: handled.
//
// Known difference: when the capturing child declines the message, the
// original returns an uninitialized stack byte in AL; ours returns 0 (not
// handled).
std::uint32_t __fastcall CScreenGroup_HandleMessage(void* self, void* /*edx*/, Message message) {
    if (void* captured = reinterpret_cast<void*>(GroupOf(self)->captured); captured != nullptr) {
        const std::uint32_t accepted = CallVirtual<std::uint32_t>(captured, 0x20, static_cast<const void*>(&message));
        if (static_cast<std::uint8_t>(accepted) == 0) {
            return 0;
        }
        // (re-read after the call, as in the original)
        return CallVirtual<std::uint32_t>(reinterpret_cast<void*>(GroupOf(self)->captured), 0x24, message);
    }
    std::uint8_t handled =
        static_cast<std::uint8_t>(CScreenElement_DispatchMessage(GroupBase(self), nullptr, message));
    if (handled != 0) {
        return handled;
    }
    switch (message.type) {
        case 1: case 2: case 3:
            if (void* focus = reinterpret_cast<void*>(GroupOf(self)->focus); focus != nullptr) {
                handled = static_cast<std::uint8_t>(CallVirtual<std::uint32_t>(focus, 0x24, message));
            }
            return handled;
        case 0x30:
        case 0x16:
            for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
                CallVirtual<std::uint32_t>(Child(self, i), 0x24, message);
            }
            return handled;
        default:
            for (std::uint32_t i = 0; i < ChildCount(self) && handled == 0; ++i) {
                const std::uint32_t accepts =
                    CallVirtual<std::uint32_t>(Child(self, i), 0x20, static_cast<const void*>(&message));
                if (static_cast<std::uint8_t>(accepts) != 0) {
                    handled = static_cast<std::uint8_t>(CallVirtual<std::uint32_t>(Child(self, i), 0x24, message));
                }
            }
            return handled;
    }
}

S3RE_HOOK(0x004176b0, CScreenGroup_OnMouseMove, MessageFn, Original_CScreenGroup_OnMouseMove,
          CScreenGroup_OnMouseMove);

// Purpose: Mouse movement over a group: tracks entering/leaving for the
// group and each child. AL is 0 (not handled); the rest of EAX is the last
// TrackMouse result, or 0.
std::uint32_t __fastcall CScreenGroup_OnMouseMove(void* self, void* /*edx*/, Message message) {
    CScreenElement_TrackMouse(GroupBase(self), nullptr, message);
    std::uint32_t result = 0;
    for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
        result = CScreenElement_TrackMouse(Child(self, i), nullptr, message);
    }
    return result & ~0xffu;
}

// The same members in the vtables of derived classes (CGroupControl,
// CDialog, the menu dialogs; CScreen for 0x417f60).

S3RE_HOOK(0x00429dd0, CScreenGroup_SetFlag1F_429dd0, Arg1Fn, Original_CScreenGroup_SetFlag1F_429dd0,
          CScreenGroup_SetFlag1F_429dd0);

// Purpose: A derived class's copy of the group's SetFlag1F.
std::uint32_t __fastcall CScreenGroup_SetFlag1F_429dd0(void* self, void* /*edx*/, std::uint32_t argument) {
    return CScreenGroup_SetFlag1F(self, nullptr, argument);
}

S3RE_HOOK(0x0042a030, CScreenGroup_OnMoved_42a030, MovedFn, Original_CScreenGroup_OnMoved_42a030,
          CScreenGroup_OnMoved_42a030);

// Purpose: A derived class's copy of the group's "moved" handler.
std::uint32_t __fastcall CScreenGroup_OnMoved_42a030(void* self, void* /*edx*/, const RECT* rect,
                                                     const std::int32_t* delta) {
    return CScreenGroup_OnMoved(self, nullptr, rect, delta);
}

S3RE_HOOK(0x0042a180, CScreenGroup_Draw_42a180, Draw3PtrFn, Original_CScreenGroup_Draw_42a180,
          CScreenGroup_Draw_42a180);

// Purpose: A derived class's copy of the group's Draw.
std::uint32_t __fastcall CScreenGroup_Draw_42a180(void* self, void* /*edx*/, std::uint32_t surface, const RECT* rectA,
                                                  const RECT* rectB) {
    return CScreenGroup_Draw(self, nullptr, surface, rectA, rectB);
}

S3RE_HOOK(0x00429e10, CScreenGroup_HandleMessage_429e10, MessageFn, Original_CScreenGroup_HandleMessage_429e10,
          CScreenGroup_HandleMessage_429e10);

// Purpose: A derived class's copy of the group's message handling.
std::uint32_t __fastcall CScreenGroup_HandleMessage_429e10(void* self, void* /*edx*/, Message message) {
    return CScreenGroup_HandleMessage(self, nullptr, message);
}

S3RE_HOOK(0x00417f60, CScreen_HandleMessage, MessageFn, Original_CScreen_HandleMessage, CScreen_HandleMessage);

// Purpose: CScreen's message handling: the group's, on its group part at
// this-0x18.
std::uint32_t __fastcall CScreen_HandleMessage(void* self, void* /*edx*/, Message message) {
    return CScreenGroup_HandleMessage(Offset(self, -kScreenGroup), nullptr, message);
}

// ---------------------------------------------------------------------------
// CControl: a UI element with a tooltip (RTTI name; buttons, edit fields and
// the dialogs derive from it). These members get the address of its
// CScreenElement part ("this" below = control + 0x3c); the base is found
// through the vbtable at this-0x38. Fields (offsets from the control):
//   +0x08 an object told when the tooltip time is up (0x4183c0, not done)
//   +0x10 std::string: the tooltip text
//   +0x20 the tooltip object (deleted through its vtable slot 0)
//   +0x24 timeGetTime() of the last mouse movement
//   +0x28, +0x2c the mouse position then (16-bit message coordinates)
//   +0x30, +0x34 where the mouse was when the tooltip was opened
// The base's handler they end with (0x415c80, CScreenElement::vf12) is `xor
// al,al; ret 14h`: it does nothing and returns AL = 0, so these return
// whatever EAX held with AL cleared (callers only use AL).
// ---------------------------------------------------------------------------

namespace {

std::uint32_t BaseHandlerResult(std::uint32_t eax) {
    return eax & ~0xffu;
}

void* ControlBase(void* self) {
    return VirtualBase(ControlOf(self)->vbptr);
}

// EAX left over in the control's handlers: its vbtable pointer.
std::uint32_t ControlVbptr(void* self) {
    return reinterpret_cast<std::uint32_t>(ControlOf(self)->vbptr);
}

using TimeGetTimeFn = DWORD(WINAPI*)();
constexpr std::uintptr_t kTimeGetTimeImport = 0x00655358;  // S3.EXE's import of timeGetTime (_INMM.dll)

std::int32_t AbsoluteDifference(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t d = a - b;
    const std::uint32_t sign = static_cast<std::uint32_t>(static_cast<std::int32_t>(d) >> 31);
    return static_cast<std::int32_t>((d ^ sign) - sign);  // cdq; xor; sub (so -2^31 stays negative)
}

}  // namespace

S3RE_HOOK(0x004187b0, CControl_DeleteTooltip, VoidNoArgFn, Original_CControl_DeleteTooltip, CControl_DeleteTooltip);

// Purpose: Closes a control's tooltip, if one is open. ECX is the control
// itself here.
void __fastcall CControl_DeleteTooltip(void* control, void* /*edx*/) {
    auto* part = static_cast<CControl*>(control);
    void* tooltip = reinterpret_cast<void*>(part->tooltip);
    if (tooltip == nullptr) {
        return;
    }
    CallVirtual<std::uint32_t>(tooltip, 0x00, 1u);  // scalar deleting destructor
    part->tooltip = 0;
}

S3RE_HOOK(0x00418380, CControl_OnMessage10, MessageFn, Original_CControl_OnMessage10, CControl_OnMessage10);

// Purpose: A control's handler for message type 0x10 (probably a mouse
// click): gives it the focus (vf03 with 0).
std::uint32_t __fastcall CControl_OnMessage10(void* self, void* /*edx*/, Message /*message*/) {
    CallVirtual<std::uint32_t>(ControlBase(self), 0x0c, 0u);
    return BaseHandlerResult(ControlVbptr(self));
}

S3RE_HOOK(0x00418430, CControl_OnMouseMove, MessageFn, Original_CControl_OnMouseMove, CControl_OnMouseMove);

// Purpose: A control's mouse-movement handler (message type 0x16): notes the
// time and position (for the tooltip delay) and closes the tooltip once the
// mouse has moved more than 50 pixels from where it opened. Only AL (0) is
// defined in EAX.
std::uint32_t __fastcall CControl_OnMouseMove(void* self, void* /*edx*/, Message message) {
    CControl* control = ControlOf(self);
    control->last_move_time = (*reinterpret_cast<TimeGetTimeFn*>(kTimeGetTimeImport))();
    const std::uint32_t x = message.x;
    const std::uint32_t y = message.y;
    control->mouse_x = x;
    control->mouse_y = y;
    if (control->tooltip != 0) {
        if (AbsoluteDifference(control->mouse_x, control->tooltip_x) > 50 ||
            AbsoluteDifference(y, control->tooltip_y) > 50) {
            CControl_DeleteTooltip(control, nullptr);
        }
    }
    return 0;
}

S3RE_HOOK(0x004184b0, CControl_OnMouseEnter, MessageFn, Original_CControl_OnMouseEnter, CControl_OnMouseEnter);

// Purpose: A control's "mouse entered" handler: nothing beyond the base's
// (empty) one.
std::uint32_t __fastcall CControl_OnMouseEnter(void* self, void* /*edx*/, Message /*message*/) {
    (void)ControlBase(self);
    return BaseHandlerResult(reinterpret_cast<std::uint32_t>(self));
}

S3RE_HOOK(0x004184e0, CControl_OnMouseLeave, MessageFn, Original_CControl_OnMouseLeave, CControl_OnMouseLeave);

// Purpose: A control's "mouse left" handler: closes its tooltip.
std::uint32_t __fastcall CControl_OnMouseLeave(void* self, void* /*edx*/, Message /*message*/) {
    CControl_DeleteTooltip(ControlOf(self), nullptr);
    (void)ControlBase(self);
    return BaseHandlerResult(ControlVbptr(self));
}

// CGroupControl (a control that is also a group; its group part is "this"
// here, its control part's CScreenElement address this-0x7c).

S3RE_HOOK(0x00429e80, CGroupControl_OnMessage10, MessageFn, Original_CGroupControl_OnMessage10,
          CGroupControl_OnMessage10);

// Purpose: CGroupControl's handler for message type 0x10: the control's.
std::uint32_t __fastcall CGroupControl_OnMessage10(void* self, void* /*edx*/, Message message) {
    return CControl_OnMessage10(Offset(self, -kGroupControlControl), nullptr, message);
}

S3RE_HOOK(0x00429ec0, CGroupControl_OnMouseMove, MessageFn, Original_CGroupControl_OnMouseMove,
          CGroupControl_OnMouseMove);

// Purpose: CGroupControl's mouse-movement handler: the control's (tooltip),
// then the group's (children).
std::uint32_t __fastcall CGroupControl_OnMouseMove(void* self, void* /*edx*/, Message message) {
    CControl_OnMouseMove(Offset(self, -kGroupControlControl), nullptr, message);
    return CScreenGroup_OnMouseMove(self, nullptr, message);
}

S3RE_HOOK(0x00429f50, CGroupControl_OnMouseEnter, MessageFn, Original_CGroupControl_OnMouseEnter,
          CGroupControl_OnMouseEnter);

// Purpose: CGroupControl's "mouse entered" handler: the control's.
std::uint32_t __fastcall CGroupControl_OnMouseEnter(void* self, void* /*edx*/, Message message) {
    return CControl_OnMouseEnter(Offset(self, -kGroupControlControl), nullptr, message);
}

S3RE_HOOK(0x00429f90, CGroupControl_OnMouseLeave, MessageFn, Original_CGroupControl_OnMouseLeave,
          CGroupControl_OnMouseLeave);

// Purpose: CGroupControl's "mouse left" handler: the control's (closes the
// tooltip).
std::uint32_t __fastcall CGroupControl_OnMouseLeave(void* self, void* /*edx*/, Message message) {
    return CControl_OnMouseLeave(Offset(self, -kGroupControlControl), nullptr, message);
}

// ---------------------------------------------------------------------------
// Batch 25: the "enabled changed" notification (vtable slot 0x6c, vf27) of
// groups and controls, and the background fill (slot 0x2c, vf11)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x004177f0, CScreenGroup_OnEnable, Arg1Fn, Original_CScreenGroup_OnEnable, CScreenGroup_OnEnable);

// Purpose: Tells every child of a group that the group was enabled or
// disabled (vf27 of each child). The base's handler (0x582530) is empty.
// EAX: the vbtable pointer at this-0x7c (left over from that base call).
std::uint32_t __fastcall CScreenGroup_OnEnable(void* self, void* /*edx*/, std::uint32_t enable) {
    for (std::uint32_t i = 0; i < ChildCount(self); ++i) {
        CallVirtual<std::uint32_t>(Child(self, i), 0x6c, enable);
    }
    return reinterpret_cast<std::uint32_t>(GroupOf(self)->vbptr);
}

S3RE_HOOK(0x00418520, CControl_OnEnable, Arg1Fn, Original_CControl_OnEnable, CControl_OnEnable);

// Purpose: A control being disabled closes its tooltip (only the low byte
// of the argument counts). EAX: the vbtable pointer at this-0x38.
std::uint32_t __fastcall CControl_OnEnable(void* self, void* /*edx*/, std::uint32_t enable) {
    if (static_cast<std::uint8_t>(enable) == 0) {
        CControl_DeleteTooltip(ControlOf(self), nullptr);
    }
    return ControlVbptr(self);
}

S3RE_HOOK(0x0042a000, CGroupControl_OnEnable, Arg1Fn, Original_CGroupControl_OnEnable, CGroupControl_OnEnable);

// Purpose: CGroupControl's "enabled changed" handler: the control's
// (tooltip), then the group's (children).
std::uint32_t __fastcall CGroupControl_OnEnable(void* self, void* /*edx*/, std::uint32_t enable) {
    CControl_OnEnable(Offset(self, -kGroupControlControl), nullptr, enable);
    return CScreenGroup_OnEnable(self, nullptr, enable);
}

using FillFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* surface, const RECT* rect);

S3RE_HOOK(0x00415b60, CScreenElement_FillBackground, FillFn, Original_CScreenElement_FillBackground,
          CScreenElement_FillBackground);

// Purpose: Fills an element's area on a surface with its background colour
// (+0x04, 0x1111 after construction), unless its byte +0x1f is set
// (probably "transparent"). EAX: DDBlt's 0, else the byte in AL (the rest
// of EAX is the caller's; callers only pass it on).
std::uint32_t __fastcall CScreenElement_FillBackground(void* self, void* /*edx*/, void* surface, const RECT* rect) {
    const std::uint8_t transparent = ElementOf(self)->flag1f;
    if (transparent != 0) {
        return transparent;
    }
    static_assert(sizeof(DDBLTFX) == 0x64 && offsetof(DDBLTFX, dwFillColor) == 0x50);  // as the original builds it
    DDBLTFX fx = {};  // the original leaves all but these two fields uninitialized
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = ElementOf(self)->background;
    return DirectX_CSurface_DDBlt(surface, nullptr, rect, nullptr, nullptr, DDBLT_COLORFILL, &fx);
}

S3RE_HOOK(0x00417460, CScreenGroup_FillBackground, FillFn, Original_CScreenGroup_FillBackground,
          CScreenGroup_FillBackground);

// Purpose: A group's background fill: its CScreenElement part's.
std::uint32_t __fastcall CScreenGroup_FillBackground(void* self, void* /*edx*/, void* surface, const RECT* rect) {
    return CScreenElement_FillBackground(GroupBase(self), nullptr, surface, rect);
}

S3RE_HOOK(0x00429e50, CScreenGroup_FillBackground_429e50, FillFn, Original_CScreenGroup_FillBackground_429e50,
          CScreenGroup_FillBackground_429e50);

// Purpose: A derived class's copy of the group's background fill.
std::uint32_t __fastcall CScreenGroup_FillBackground_429e50(void* self, void* /*edx*/, void* surface,
                                                            const RECT* rect) {
    return CScreenGroup_FillBackground(self, nullptr, surface, rect);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The objects live in one buffer: each case snapshots it, runs the
// original, saves the result, restores the snapshot and runs ours at the
// same addresses, so object pointers compare directly. Virtual calls go to a
// fake vtable of recording stubs (arguments that point into the stack are
// recorded by their contents, since the two stack frames differ); each stub
// returns 0, 1 or a random value from the case's sequence.

namespace {

// Stack argument count per vtable slot (offset / 4); -1 = not used here.
constexpr int kSlotArgs[32] = {
    1, 1, -1, 1, 0, -1, 1, 0,     // 0x00 (delete), 0x04, 0x0c, 0x10, 0x18, 0x1c
    1, 5, 3, 2, 5, 5, 5, 5,       // 0x20 .. 0x3c
    5, 5, 5, 5, 5, 0, 5, 5,       // 0x40 .. 0x5c (0x54 without arguments)
    5, 1, 1, 1, 2, 2, 0, -1,      // 0x60 .. 0x78
};

// Arguments that point into the stack (or may: the rectangle of 0x70 / 0x74
// is MoveBy's local): (slot, argument, bytes).
struct StackPointer {
    int slot;
    int argument;
    int bytes;
};
constexpr StackPointer kStackPointers[] = {{0x20 / 4, 0, 20}, {0x28 / 4, 1, 16}, {0x28 / 4, 2, 16},
                                           {0x70 / 4, 0, 16}, {0x70 / 4, 1, 8},
                                           {0x74 / 4, 0, 16}, {0x74 / 4, 1, 8}};

struct ProbeLog {
    std::vector<std::uint32_t> words;  // per call: slot, this, arguments (or contents)
    std::uint64_t seed = 0;
    std::uint32_t calls = 0;
    bool undeclared = false;
    std::uint32_t* groupLength = nullptr;  // the group's child count, changed by child calls
};
ProbeLog* g_probeLog = nullptr;

// The value the probe call number `index` of a case returns (as ProbeValue).
std::uint32_t ProbeValueAt(std::uint64_t seed, std::uint32_t index) {
    test::Rng rng(seed * 0x9e3779b97f4a7c15ull + index);
    const std::uint32_t choice = rng.Below(4);
    return choice == 0 ? 0 : choice == 1 ? 1 : rng.Next32();
}

// Child calls sometimes change the group's child count, to reach the bounds
// handling of the groups' loops: 0, 0x80000001 (every index out of range;
// data[0x80000000] wraps around to data[0]) or 0..2; a huge count is always
// set back to 0, which ends the loop.
void MaybeChangeChildCount(int slot) {
    ProbeLog& log = *g_probeLog;
    if (log.groupLength == nullptr) {
        return;
    }
    switch (slot * 4) {
        case 0x18: case 0x20: case 0x24: case 0x28: case 0x5c: case 0x60: case 0x6c: case 0x70: case 0x74: break;
        default: return;
    }
    test::Rng rng(log.seed * 0xc2b2ae3d27d4eb4full + log.calls);
    const std::uint32_t choice = rng.Below(8);
    if (*log.groupLength >= 0x80000000u || choice == 0) {
        *log.groupLength = 0;
    } else if (choice == 1 && slot * 4 != 0x5c && slot * 4 != 0x60) {
        // (not from TrackMouse's calls: later children may not call at all)
        *log.groupLength = 0x80000001u;
    } else if (choice == 2) {
        *log.groupLength = rng.Below(3);
    }
}

std::uint32_t ProbeValue() {
    test::Rng rng(g_probeLog->seed * 0x9e3779b97f4a7c15ull + g_probeLog->calls);
    const std::uint32_t choice = rng.Below(4);
    return choice == 0 ? 0 : choice == 1 ? 1 : rng.Next32();
}

std::uint32_t LogCall(int slot, void* self, const std::uint32_t* args, int count) {
    ProbeLog& log = *g_probeLog;
    log.words.push_back(0xca110000u | static_cast<std::uint32_t>(slot * 4));
    log.words.push_back(reinterpret_cast<std::uint32_t>(self));
    for (int i = 0; i < count; ++i) {
        int bytes = 0;
        for (const StackPointer& p : kStackPointers) {
            if (p.slot == slot && p.argument == i) {
                bytes = p.bytes;
            }
        }
        if (bytes == 0) {
            log.words.push_back(args[i]);
        } else {
            const auto* contents = reinterpret_cast<const std::uint32_t*>(args[i]);
            for (int w = 0; w < bytes / 4; ++w) {
                log.words.push_back(contents[w]);
            }
        }
    }
    const std::uint32_t value = ProbeValue();
    MaybeChangeChildCount(slot);
    ++log.calls;
    log.words.push_back(value);
    return value;
}

template <int K, int N>
struct Stub;
template <int K>
struct Stub<K, 0> {
    static std::uint32_t __fastcall Call(void* self, void*) { return LogCall(K, self, nullptr, 0); }
};
template <int K>
struct Stub<K, 1> {
    static std::uint32_t __fastcall Call(void* self, void*, std::uint32_t a) {
        const std::uint32_t args[] = {a};
        return LogCall(K, self, args, 1);
    }
};
template <int K>
struct Stub<K, 2> {
    static std::uint32_t __fastcall Call(void* self, void*, std::uint32_t a, std::uint32_t b) {
        const std::uint32_t args[] = {a, b};
        return LogCall(K, self, args, 2);
    }
};
template <int K>
struct Stub<K, 3> {
    static std::uint32_t __fastcall Call(void* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const std::uint32_t args[] = {a, b, c};
        return LogCall(K, self, args, 3);
    }
};
template <int K>
struct Stub<K, 5> {
    static std::uint32_t __fastcall Call(void* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                         std::uint32_t d, std::uint32_t e) {
        const std::uint32_t args[] = {a, b, c, d, e};
        return LogCall(K, self, args, 5);
    }
};

std::uint32_t __fastcall UndeclaredStub(void*, void*) {
    g_probeLog->undeclared = true;
    return 0;
}

template <int K>
void* StubFor() {
    if constexpr (kSlotArgs[K] < 0) {
        return reinterpret_cast<void*>(&UndeclaredStub);
    } else {
        return reinterpret_cast<void*>(&Stub<K, kSlotArgs[K]>::Call);
    }
}

template <int... K>
std::vector<void*> MakeVtable(std::integer_sequence<int, K...>) {
    return {StubFor<K>()...};
}

const std::vector<void*>& FakeVtable() {
    static const std::vector<void*> table = MakeVtable(std::make_integer_sequence<int, 32>{});
    return table;
}

// Buffer layout (offsets): element 0x100; parent 0x200 (vbtable at +4, base
// at 0x224); group "this" 0x300 (vbptr 0x284 -> base = element), focus
// child slot 0x2a8, child object 0x380; the 0x429fd0 subobject 0x400 (vbptr
// 0x34c -> element). Shared inputs (rectangles, message) are in `inputs`.
constexpr std::size_t kBuffer = 0xa00;
constexpr std::size_t kElement = 0x100;
constexpr std::size_t kParent = 0x200;
constexpr std::size_t kGroup = 0x300;
constexpr std::size_t kChild = 0x380;
constexpr std::size_t kEnableThis = 0x400;
// Where the vbtable pointers lie in the buffer, and the vbtables leading
// from them to the element.
constexpr std::size_t kFreeRect = 0x40;  // a free spot for an output RECT
constexpr std::size_t kGroupVbptr = kGroup - ui::kGroupThis + offsetof(CScreenGroup, vbptr);
constexpr std::size_t kEnableVbptr = kEnableThis - kGroupControlControl - ui::kControlThis + offsetof(CControl, vbptr);
static_assert(kGroupVbptr == 0x284 && kEnableVbptr == 0x34c);
const std::int32_t kParentVbtable[2] = {0, 0x20};  // the parent's element part at kParent + 0x24
const std::int32_t kGroupVbtable[2] = {0, static_cast<std::int32_t>(kElement) - static_cast<std::int32_t>(kGroupVbptr)};
const std::int32_t kEnableVbtable[2] = {0, static_cast<std::int32_t>(kElement) - static_cast<std::int32_t>(kEnableVbptr)};
// Batch 24 additions: the group's children vector at kGroup-0x74 (data at
// kChildData: four children at kChildren + 0x40 * k), the captured child at
// kGroup-0x50; a CControl's CScreenElement part at kControl (vbtable at
// -0x38 -> element) and the CGroupControl one at kGroup-0x7c (vbtable at
// kGroup-0xb4).
constexpr std::size_t kChildren = 0x800;
constexpr std::size_t kChildData = 0x980;
constexpr std::size_t kControl = 0x600;
constexpr std::size_t kGroupControl = kGroup - kGroupControlControl;
constexpr std::size_t kControlVbptrDistance = ui::kControlThis - offsetof(CControl, vbptr);  // 0x38
const std::int32_t kControlVbtable[2] = {
    0, static_cast<std::int32_t>(kElement) - static_cast<std::int32_t>(kControl - kControlVbptrDistance)};
const std::int32_t kGroupControlVbtable[2] = {
    0, static_cast<std::int32_t>(kElement) - static_cast<std::int32_t>(kGroupControl - kControlVbptrDistance)};

struct Functions {
    CtorFn construct;
    CtorRectFn constructRect;
    DtorFn destroy;
    DeletingDtorFn deletingDestroy;
    RectFn rect;
    SetRectFn setRect;
    MoveByFn moveBy;
    Arg1Fn setParent;
    ContainsPointFn containsPoint;
    NoArgFn needsRedraw;
    NoArgFn isQuery78Zero;
    MessageFn onMessage;
    MessagePtrFn wantsMessage;
    MessageFn dispatch;
    Draw2Fn drawClipped;
    Draw3Fn drawRect;
    IntersectFn intersect;
    Arg1Fn setFocus;
    NoArgFn killFocus;
    NoArgFn invalidate;
    MessageFn trackMouse;
    Arg1Fn enable;
    NoArgFn parentQuery78;
    VoidArg1Fn setFlag1F;
    GlobalFn global823c;
    CopyGlobalsFn copyGlobals;
    TableFn table809c;
    TableFn table8064;
    GlobalFn global8238;
    GlobalFn global8240;
    MessagePtrFn groupWants;
    Arg1Fn groupSetFocus;
    NoArgFn groupKillFocus;
    Arg1Fn groupSetFocus429d70;
    MessagePtrFn groupWants429df0;
    Arg1Fn groupEnable429fd0;
    Arg1Fn screenGroupSetFlag1F;
    MovedFn screenGroupOnMoved;
    Draw3PtrFn screenGroupDraw;
    MessageFn screenGroupHandleMessage;
    MessageFn screenGroupOnMouseMove;
    Arg1Fn setFlag1F429dd0;
    MovedFn onMoved42a030;
    Draw3PtrFn draw42a180;
    MessageFn handleMessage429e10;
    MessageFn screenHandleMessage;
    VoidNoArgFn controlDeleteTooltip;
    MessageFn controlOnMessage10;
    MessageFn controlOnMouseMove;
    MessageFn controlOnMouseEnter;
    MessageFn controlOnMouseLeave;
    MessageFn groupControlOnMessage10;
    MessageFn groupControlOnMouseMove;
    MessageFn groupControlOnMouseEnter;
    MessageFn groupControlOnMouseLeave;
    Arg1Fn screenGroupOnEnable;
    Arg1Fn controlOnEnable;
    Arg1Fn groupControlOnEnable;
};
constexpr int kOperations = 58;

Functions OriginalFunctions() {
    return {Original_CScreenElement_Construct,     Original_CScreenElement_ConstructRect,
            Original_CScreenElement_Destroy,       Original_CScreenElement_DeletingDestroy,
            Original_CScreenElement_Rect,          Original_CScreenElement_SetRect,
            Original_CScreenElement_MoveBy,        Original_CScreenElement_SetParent,
            Original_CScreenElement_ContainsPoint, Original_CScreenElement_NeedsRedraw,
            Original_CScreenElement_IsQuery78Zero, Original_CScreenElement_OnMessage,
            Original_CScreenElement_WantsMessage,  Original_CScreenElement_DispatchMessage,
            Original_CScreenElement_DrawClipped,   Original_CScreenElement_DrawRect,
            Original_CScreenElement_IntersectRects, Original_CScreenElement_SetFocus,
            Original_CScreenElement_KillFocus,     Original_CScreenElement_Invalidate,
            Original_CScreenElement_TrackMouse,    Original_CScreenElement_Enable,
            Original_CScreenElement_ParentQuery78, Original_CScreenElement_SetFlag1F,
            Original_Dialog_Global7a823c,          Original_Dialog_CopyGlobals7a8020,
            Original_Dialog_Table7a809c,           Original_Dialog_Table7a8064,
            Original_Dialog_Global7a8238,          Original_Dialog_Global7a8240,
            Original_UiGroup_WantsMessage,         Original_UiGroup_SetFocus,
            Original_UiGroup_KillFocus,            Original_UiGroup_SetFocus_429d70,
            Original_UiGroup_WantsMessage_429df0,  Original_UiGroup_Enable_429fd0,
            Original_CScreenGroup_SetFlag1F,       Original_CScreenGroup_OnMoved,
            Original_CScreenGroup_Draw,            Original_CScreenGroup_HandleMessage,
            Original_CScreenGroup_OnMouseMove,     Original_CScreenGroup_SetFlag1F_429dd0,
            Original_CScreenGroup_OnMoved_42a030,  Original_CScreenGroup_Draw_42a180,
            Original_CScreenGroup_HandleMessage_429e10, Original_CScreen_HandleMessage,
            Original_CControl_DeleteTooltip,       Original_CControl_OnMessage10,
            Original_CControl_OnMouseMove,         Original_CControl_OnMouseEnter,
            Original_CControl_OnMouseLeave,        Original_CGroupControl_OnMessage10,
            Original_CGroupControl_OnMouseMove,    Original_CGroupControl_OnMouseEnter,
            Original_CGroupControl_OnMouseLeave,   Original_CScreenGroup_OnEnable,
            Original_CControl_OnEnable,            Original_CGroupControl_OnEnable};
}

Functions OurFunctions() {
    return {&CScreenElement_Construct,     &CScreenElement_ConstructRect, &CScreenElement_Destroy,
            &CScreenElement_DeletingDestroy, &CScreenElement_Rect,        &CScreenElement_SetRect,
            &CScreenElement_MoveBy,        &CScreenElement_SetParent,     &CScreenElement_ContainsPoint,
            &CScreenElement_NeedsRedraw,   &CScreenElement_IsQuery78Zero, &CScreenElement_OnMessage,
            &CScreenElement_WantsMessage,  &CScreenElement_DispatchMessage, &CScreenElement_DrawClipped,
            &CScreenElement_DrawRect,      &CScreenElement_IntersectRects, &CScreenElement_SetFocus,
            &CScreenElement_KillFocus,     &CScreenElement_Invalidate,    &CScreenElement_TrackMouse,
            &CScreenElement_Enable,        &CScreenElement_ParentQuery78, &CScreenElement_SetFlag1F,
            &Dialog_Global7a823c,          &Dialog_CopyGlobals7a8020,     &Dialog_Table7a809c,
            &Dialog_Table7a8064,           &Dialog_Global7a8238,          &Dialog_Global7a8240,
            &UiGroup_WantsMessage,         &UiGroup_SetFocus,             &UiGroup_KillFocus,
            &UiGroup_SetFocus_429d70,      &UiGroup_WantsMessage_429df0,  &UiGroup_Enable_429fd0,
            &CScreenGroup_SetFlag1F,       &CScreenGroup_OnMoved,         &CScreenGroup_Draw,
            &CScreenGroup_HandleMessage,   &CScreenGroup_OnMouseMove,     &CScreenGroup_SetFlag1F_429dd0,
            &CScreenGroup_OnMoved_42a030,  &CScreenGroup_Draw_42a180,     &CScreenGroup_HandleMessage_429e10,
            &CScreen_HandleMessage,        &CControl_DeleteTooltip,       &CControl_OnMessage10,
            &CControl_OnMouseMove,         &CControl_OnMouseEnter,        &CControl_OnMouseLeave,
            &CGroupControl_OnMessage10,    &CGroupControl_OnMouseMove,    &CGroupControl_OnMouseEnter,
            &CGroupControl_OnMouseLeave,   &CScreenGroup_OnEnable,        &CControl_OnEnable,
            &CGroupControl_OnEnable};
}

struct Inputs {
    RECT rects[2];
    Message message;
    std::uint32_t a, b;
    std::int32_t delta[2];
};

// HandleMessage's AL when the captured child declines the message is an
// uninitialized stack byte in the original (see CScreenGroup_HandleMessage):
// not compared then.
constexpr std::uint32_t kUndefinedResult = 0xdeadbeefu;

std::uint32_t HandleMessageResult(MessageFn fn, void* self, void* group, Inputs& in) {
    const bool captured = GroupOf(group)->captured != 0;
    const bool declines = captured && (ProbeValueAt(g_probeLog->seed, g_probeLog->calls) & 0xff) == 0;
    const std::uint32_t result = fn(self, nullptr, in.message) & 0xff;
    return declines ? kUndefinedResult : result;
}

// Runs operation `op`; returns EAX (masked where only AL is defined).
std::uint32_t RunOperation(const Functions& f, int op, std::uint8_t* buffer, Inputs& in) {
    void* element = buffer + kElement;
    void* group = buffer + kGroup;
    const auto ptr = [](const void* p) { return reinterpret_cast<std::uint32_t>(p); };
    switch (op) {
        case 0: return ptr(f.construct(element, nullptr));
        case 1: return ptr(f.constructRect(element, nullptr, &in.rects[0]));
        case 2: return f.destroy(element, nullptr);
        case 3: return ptr(f.deletingDestroy(element, nullptr, in.a & ~1u));
        case 4: return ptr(f.rect(element, nullptr));
        case 5: return f.setRect(element, nullptr, (in.b & 3) == 0 ? &ElementOf(element)->rect : &in.rects[0]);
        case 6: return f.moveBy(element, nullptr, static_cast<std::int32_t>(in.a), static_cast<std::int32_t>(in.b));
        case 7: return f.setParent(element, nullptr, (in.b & 1) ? ptr(buffer + kParent) : in.a);
        case 8: return f.containsPoint(element, nullptr, in.a, in.b);
        case 9: return f.needsRedraw(element, nullptr) & 0xff;
        case 10: return f.isQuery78Zero(element, nullptr);
        case 11: return f.onMessage(element, nullptr, in.message);
        case 12: return f.wantsMessage(element, nullptr, &in.message) & 0xff;
        case 13: return f.dispatch(element, nullptr, in.message);
        case 14: return f.drawClipped(element, nullptr, in.a, &in.rects[1]);
        case 15: return f.drawRect(element, nullptr, in.a, &in.rects[1], in.b);
        case 16: return ptr(f.intersect(element, nullptr, reinterpret_cast<RECT*>(buffer + kFreeRect), &in.rects[0], &in.rects[1]));
        case 17: return f.setFocus(element, nullptr, in.a);
        case 18: return f.killFocus(element, nullptr) & 0xff;
        case 19: return f.invalidate(element, nullptr);
        case 20: return f.trackMouse(element, nullptr, in.message);
        case 21: return f.enable(element, nullptr, in.a);
        case 22: return f.parentQuery78(element, nullptr);
        case 23: f.setFlag1F(element, nullptr, in.a); return 0;
        case 24: return f.global823c();
        case 25: return ptr(f.copyGlobals(buffer + 0x20)) - ptr(buffer);
        case 26: return f.table809c(in.a % 14);
        case 27: return f.table8064(in.a % 14);
        case 28: return f.global8238();
        case 29: return f.global8240();
        case 30: return f.groupWants(group, nullptr, &in.message) & 0xff;
        case 31: return f.groupSetFocus(group, nullptr, (in.b & 1) ? ptr(buffer + kChild) : GroupOf(group)->focus);
        case 32: return f.groupKillFocus(group, nullptr) & 0xff;
        case 33: return f.groupSetFocus429d70(group, nullptr, (in.b & 1) ? ptr(buffer + kChild) : in.a);
        case 34: return f.groupWants429df0(group, nullptr, &in.message) & 0xff;
        case 35: return f.groupEnable429fd0(buffer + kEnableThis, nullptr, in.a);
        case 36: return f.screenGroupSetFlag1F(group, nullptr, in.a);
        case 37: return f.screenGroupOnMoved(group, nullptr, &in.rects[0], in.delta);
        case 38: return f.screenGroupDraw(group, nullptr, in.a, &in.rects[0], &in.rects[1]);
        case 39: return HandleMessageResult(f.screenGroupHandleMessage, group, group, in);
        case 40: return f.screenGroupOnMouseMove(group, nullptr, in.message);
        case 41: return f.setFlag1F429dd0(group, nullptr, in.a);
        case 42: return f.onMoved42a030(group, nullptr, &in.rects[0], in.delta);
        case 43: return f.draw42a180(group, nullptr, in.a, &in.rects[0], &in.rects[1]);
        case 44: return HandleMessageResult(f.handleMessage429e10, group, group, in);
        case 45: return HandleMessageResult(f.screenHandleMessage, buffer + kGroup + kScreenGroup, group, in);
        case 46: f.controlDeleteTooltip(ControlOf(buffer + kControl), nullptr); return 0;
        case 47: return f.controlOnMessage10(buffer + kControl, nullptr, in.message);
        case 48: return f.controlOnMouseMove(buffer + kControl, nullptr, in.message) & 0xff;
        case 49: return f.controlOnMouseEnter(buffer + kControl, nullptr, in.message);
        case 50: return f.controlOnMouseLeave(buffer + kControl, nullptr, in.message);
        case 51: return f.groupControlOnMessage10(group, nullptr, in.message);
        case 52: return f.groupControlOnMouseMove(group, nullptr, in.message) & 0xff;
        case 53: return f.groupControlOnMouseEnter(group, nullptr, in.message);
        case 54: return f.groupControlOnMouseLeave(group, nullptr, in.message);
        case 55: return f.screenGroupOnEnable(group, nullptr, in.a);
        case 56: return f.controlOnEnable(buffer + kControl, nullptr, in.a);
        default: return f.groupControlOnEnable(group, nullptr, in.a);
    }
}

void SetUpBuffer(test::Context& ctx, std::uint8_t* buffer) {
    ctx.rng().Fill(buffer, kBuffer);
    // Small rectangle coordinates, so points and rectangles overlap.
    const auto randomRect = [&](RECT& r) {
        for (LONG* coordinate : {&r.left, &r.top, &r.right, &r.bottom}) {
            *coordinate = ctx.rng().Range(-8, 40);
        }
    };
    CScreenElement* element = ElementOf(buffer + kElement);
    randomRect(element->rect);
    void* vtable = const_cast<void**>(FakeVtable().data());
    const auto vtableWord = reinterpret_cast<std::uint32_t>(vtable);
    element->vtable = vtableWord;
    // The fake parent: a CScreenGroup (vtable, vbtable pointer), element
    // part at +0x24.
    auto* const parent = reinterpret_cast<CScreenGroup*>(buffer + kParent);
    parent->vtable = vtableWord;
    parent->vbptr = kParentVbtable;
    ElementOf(buffer + kParent + 0x24)->vtable = vtableWord;
    ElementOf(buffer + kChild)->vtable = vtableWord;
    element->parent = ctx.rng().Chance(1, 3) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kParent);
    element->has_focus = static_cast<std::uint8_t>(ctx.rng().Below(2));
    element->mouse_inside = static_cast<std::uint8_t>(ctx.rng().Below(3));
    CScreenGroup* group = GroupOf(buffer + kGroup);
    group->vbptr = kGroupVbtable;
    group->focus = ctx.rng().Chance(1, 3) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kChild);
    group->only_inside = static_cast<std::uint8_t>(ctx.rng().Below(2));
    GroupControlOf(buffer + kEnableThis)->vbptr = kEnableVbtable;
    // The group's children (capacity never 0 here, so the bounds handling
    // never allocates; the emulator spec covers that).
    const std::uint32_t count = std::array<std::uint32_t, 7>{0, 1, 2, 3, 4, 4, 0x80000001u}[ctx.rng().Below(7)];
    group->children.vtable = vtableWord;
    group->children.data = reinterpret_cast<std::uint32_t*>(buffer + kChildData);
    group->children.capacity = 1 + ctx.rng().Below(8);
    group->children.filled = ctx.rng().Range(-1, 4);
    group->children.length = count;
    group->captured =
        ctx.rng().Chance(3, 5) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kChildren + 0x40 * ctx.rng().Below(4));
    group->only_inside = static_cast<std::uint8_t>(ctx.rng().Below(2));
    for (std::size_t k = 0; k < 4; ++k) {
        std::uint8_t* childBytes = buffer + kChildren + 0x40 * k;
        group->children.data[k] = reinterpret_cast<std::uint32_t>(childBytes);
        CScreenElement* child = ElementOf(childBytes);
        child->vtable = vtableWord;
        child->parent = ctx.rng().Chance(1, 2) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kParent);
        child->has_focus = static_cast<std::uint8_t>(ctx.rng().Below(2));
        if (count == 0x80000001u && k == 0) {
            // The mouse arrives at the first child (TrackMouse calls 0x5c),
            // so a huge vector is always emptied by a child call.
            child->mouse_inside = 0;
            child->rect = RECT{0, 0, 0x10000, 0x10000};
        } else {
            child->mouse_inside = static_cast<std::uint8_t>(ctx.rng().Below(3));
            randomRect(child->rect);
        }
    }
    // The CControl parts.
    for (const auto& [control, vbtable] : {std::pair{kControl, kControlVbtable}, std::pair{kGroupControl, kGroupControlVbtable}}) {
        CControl* part = ControlOf(buffer + control);
        part->vbptr = vbtable;
        part->tooltip = ctx.rng().Chance(1, 3) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kChild);
        for (std::uint32_t* field : {&part->tooltip_x, &part->tooltip_y}) {
            *field = static_cast<std::uint32_t>(ctx.rng().Chance(9, 10) ? ctx.rng().Range(-40, 120)
                                                                        : static_cast<std::int32_t>(ctx.rng().Next32()));
        }
    }
}

void SetUpInputs(test::Context& ctx, Inputs& in) {
    for (RECT& r : in.rects) {
        r = {ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40)};
    }
    ctx.rng().Fill(&in.message, sizeof(in.message));
    static const std::uint8_t kTypes[] = {1, 2, 3, 0x10, 0x12, 0x13, 0x15, 0x16, 0x20, 0x30, 0x40, 0, 5, 0x41, 0xff};
    in.message.type = kTypes[ctx.rng().Below(sizeof(kTypes))];
    in.message.x = static_cast<std::uint16_t>(ctx.rng().Range(-8, 40));
    in.message.y = static_cast<std::uint16_t>(ctx.rng().Range(-8, 40));
    in.a = ctx.rng().Chance(1, 2) ? ctx.rng().Below(3) : ctx.rng().Next32();
    in.b = ctx.rng().Chance(1, 2) ? static_cast<std::uint32_t>(ctx.rng().Range(-4, 4)) : ctx.rng().Next32();
    // Mostly a non-zero move (MoveBy then calls the child's 0x70 handler).
    in.delta[0] = ctx.rng().Chance(1, 2) ? ctx.rng().Range(1, 5) : ctx.rng().Range(-5, -1);  // never 0: a huge child count must end
    in.delta[1] = ctx.rng().Range(-3, 3);
}

// CControl_OnMouseMove reads the time through S3.EXE's import of
// timeGetTime (0x655358); the tests point that entry at a fake clock while
// they run, so both versions see the same time.
DWORD g_fakeTime = 0;
DWORD WINAPI FakeTimeGetTime() {
    return g_fakeTime;
}

class FakeClock {
public:
    FakeClock() {
        VirtualProtect(Entry(), sizeof(void*), PAGE_READWRITE, &protection_);
        saved_ = *Entry();
        *Entry() = reinterpret_cast<void*>(&FakeTimeGetTime);
    }
    ~FakeClock() {
        *Entry() = saved_;
        DWORD ignored = 0;
        VirtualProtect(Entry(), sizeof(void*), protection_, &ignored);
    }

private:
    static void** Entry() { return reinterpret_cast<void**>(0x00655358); }
    DWORD protection_ = 0;
    void* saved_ = nullptr;
};

void CompareScreenElement(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original = OriginalFunctions();
    const FakeClock clock;
    std::vector<std::uint8_t> buffer(kBuffer), snapshot(kBuffer), afterOriginal(kBuffer);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOperations));
        SetUpBuffer(ctx, buffer.data());
        Inputs in{};
        SetUpInputs(ctx, in);
        snapshot = buffer;
        const Inputs inputSnapshot = in;
        ProbeLog logA, logB;
        logA.seed = logB.seed = ctx.rng().Next32();
        logA.groupLength = logB.groupLength = &GroupOf(buffer.data() + kGroup)->children.length;
        g_fakeTime = ctx.rng().Next32();
        g_probeLog = &logA;
        const std::uint32_t ra = RunOperation(original, op, buffer.data(), in);
        afterOriginal = buffer;
        const Inputs inputsA = in;
        buffer = snapshot;
        in = inputSnapshot;
        g_probeLog = &logB;
        const std::uint32_t rb = RunOperation(candidate, op, buffer.data(), in);
        g_probeLog = nullptr;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(afterOriginal.data(), buffer.data(), kBuffer);
        same &= S3RE_EXPECT_MEM_EQ(&inputsA, &in, sizeof(in));
        same &= S3RE_EXPECT(logA.words == logB.words);
        same &= S3RE_EXPECT(!logA.undeclared && !logB.undeclared);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d (%u vs %u probe words)", i, op,
                     static_cast<unsigned>(logA.words.size()), static_cast<unsigned>(logB.words.size()));
            return;
        }
    }
}

// Wrong: the mouse-left handler is called even when the mouse was not inside.
std::uint32_t __fastcall WrongTrackMouse(void* self, void* edx, Message message) {
    if (ElementOf(self)->mouse_inside == 0) {
        if (CScreenElement_ContainsPoint(self, nullptr, message.x, message.y) == 0) {
            return CallVirtual<std::uint32_t>(self, 0x60, message);
        }
    }
    return CScreenElement_TrackMouse(self, edx, message);
}

// Wrong: the group draws its children clipped to the first rectangle only.
std::uint32_t __fastcall WrongGroupDraw(void* self, void* edx, std::uint32_t surface, const RECT* rectA,
                                        const RECT* rectB) {
    const std::uint8_t saved = GroupOf(self)->only_inside;
    GroupOf(self)->only_inside = 1;
    const std::uint32_t result = CScreenGroup_Draw(self, edx, surface, rectA, rectB);
    GroupOf(self)->only_inside = saved;
    return result;
}

}  // namespace

S3RE_TEST(ScreenElement_MatchOriginal) {
    CompareScreenElement(ctx, OurFunctions(), 100000);
}

S3RE_TEST(ScreenElement_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = OurFunctions();
    wrong.trackMouse = &WrongTrackMouse;
    CompareScreenElement(probe, wrong, 5000);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Functions wrongDraw = OurFunctions();
    wrongDraw.screenGroupDraw = &WrongGroupDraw;
    CompareScreenElement(probe2, wrongDraw, 5000);
    S3RE_EXPECT(probe2.failures() > 0);
}

// The background fill: a fake CSurface (+8 a fake COM surface whose Blt,
// slot 0x14, records its arguments and the two DDBLTFX fields that count)
// in a buffer with an element and a group reaching it through its vbtable.
namespace {

std::vector<std::uint32_t>* g_fillLog = nullptr;
std::uint8_t* g_fillBase = nullptr;

std::int32_t __stdcall RecordingBlt(void* surface, const RECT* dst, void* src, const RECT* srcRect,
                                    std::uint32_t flags, void* fx) {
    const auto rel = [](const void* p) {
        return p == nullptr ? 0xffffffffu : static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(p) - g_fillBase);
    };
    const auto* const blt = static_cast<const DDBLTFX*>(fx);
    g_fillLog->insert(g_fillLog->end(), {rel(surface), rel(dst), rel(src), rel(srcRect), flags,
                                         blt->dwSize, blt->dwFillColor});
    return 0;
}

void* g_fillComVtable[8] = {};

void CompareFill(test::Context& ctx, FillFn candidate, FillFn original, int which, int cases) {
    for (void*& slot : g_fillComVtable) {
        slot = reinterpret_cast<void*>(&RecordingBlt);
    }
    static const std::int32_t kVbtable[2] = {0, 0x100 - 0x184};  // group 0x200: vbptr 0x184 -> element 0x100
    static_assert(0x200 - ui::kGroupThis + offsetof(CScreenGroup, vbptr) == 0x184);
    std::vector<std::uint8_t> buffer(0x400);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(buffer.data(), buffer.size());
        std::uint8_t* b = buffer.data();
        ElementOf(b + 0x100)->flag1f = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint8_t>(ctx.rng().Next32());
        GroupOf(b + 0x200)->vbptr = kVbtable;
        // The fake CSurface at 0x300: its DirectDraw surface (+8) at 0x340,
        // whose first dword is its COM vtable.
        reinterpret_cast<framework::CSimpleSurface*>(b + 0x300)->surface = b + 0x340;
        reinterpret_cast<framework::ComObject*>(b + 0x340)->vtable = g_fillComVtable;
        void* self = which == 0 ? b + 0x100 : b + 0x200;
        const RECT* rect = ctx.rng().Chance(1, 4) ? nullptr : reinterpret_cast<const RECT*>(b + 0x380);
        const std::vector<std::uint8_t> snapshot = buffer;
        std::vector<std::uint32_t> logA, logB;
        g_fillBase = b;
        g_fillLog = &logA;
        const std::uint32_t ra = original(self, nullptr, b + 0x300, rect);
        const std::vector<std::uint8_t> afterA = buffer;
        buffer = snapshot;
        g_fillLog = &logB;
        const std::uint32_t rb = candidate(self, nullptr, b + 0x300, rect);
        g_fillLog = nullptr;
        bool same = S3RE_EXPECT_EQ(ra & 0xff, rb & 0xff);
        same &= S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(afterA.data(), buffer.data(), buffer.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, function %d", i, which);
            return;
        }
    }
}

// Wrong: fills even a "transparent" element.
std::uint32_t __fastcall WrongFill(void* self, void* edx, void* surface, const RECT* rect) {
    const std::uint8_t saved = ElementOf(self)->flag1f;
    ElementOf(self)->flag1f = 0;
    const std::uint32_t result = CScreenElement_FillBackground(self, edx, surface, rect);
    ElementOf(self)->flag1f = saved;
    return result;
}

}  // namespace

S3RE_TEST(ScreenElement_FillBackground_MatchesOriginal) {
    CompareFill(ctx, &CScreenElement_FillBackground, Original_CScreenElement_FillBackground, 0, 3000);
    CompareFill(ctx, &CScreenGroup_FillBackground, Original_CScreenGroup_FillBackground, 1, 3000);
    CompareFill(ctx, &CScreenGroup_FillBackground_429e50, Original_CScreenGroup_FillBackground_429e50, 1, 3000);
}

S3RE_TEST(ScreenElement_FillBackground_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFill(probe, &WrongFill, Original_CScreenElement_FillBackground, 0, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CStaticControl_Construct_MatchesOriginal) {
    std::uint8_t a[0x40], b[0x40];
    for (int i = 0; i < 2000; ++i) {
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(b));
        RECT rect;
        ctx.rng().Fill(&rect, sizeof(rect));
        const void* ra = Original_CStaticControl_Construct(a + 8, nullptr, &rect);
        const void* rb = CStaticControl_Construct(b + 8, nullptr, &rect);
        if (!S3RE_EXPECT(ra == a + 8 && rb == b + 8) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

}  // namespace s3re::game
