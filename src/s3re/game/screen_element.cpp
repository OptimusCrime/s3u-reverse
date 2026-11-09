// CScreenElement, the base class of the menu and in-game UI controls
// (Dirk\dialog.cpp; RTTI name), its message plumbing, the dialog globals
// next to it, and the first members of a derived group class that reach it
// through a virtual base.
//
// CScreenElement layout (vtable 0x655a68; all thiscall):
//   +0x00 vtable
//   +0x04 dword, 0x1111 after construction (meaning unknown)
//   +0x08 RECT: left, top, right, bottom (screen coordinates)
//   +0x18 parent: a container object; its own vbtable at +4 leads to its
//         CScreenElement-like virtual base, whose vtable is used
//   +0x1c byte: has the focus (set by vf03 SetFocus, cleared by vf04)
//   +0x1d byte: the mouse is inside (TrackMouse)
//   +0x1e byte: needs redrawing (set by construction and vf07, cleared after
//         drawing)
//   +0x1f byte: set by vf06 (meaning unknown)
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

#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::game {

namespace {

template <typename T>
T& At(void* base, std::ptrdiff_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

template <typename T>
T& AtAddress(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

constexpr std::uint32_t kScreenElementVtable = 0x00655a68;

// The virtual base reached through the vbtable pointer at `vbptr` (its
// second entry is the base's offset from the vbtable pointer).
void* VirtualBase(void* object, std::ptrdiff_t vbptr) {
    auto* at = static_cast<std::uint8_t*>(object) + vbptr;
    const auto* vbtable = *reinterpret_cast<std::int32_t* const*>(at);
    return at + vbtable[1];
}

// The parent's CScreenElement part (vbtable at parent +4).
void* ParentBase(void* parent) {
    return VirtualBase(parent, 4);
}

template <typename R, typename... A>
R CallVirtual(void* self, std::size_t slot, A... args) {
    using Fn = R(__fastcall*)(void*, void*, A...);
    auto* vtable = *reinterpret_cast<std::uint8_t**>(self);
    return (*reinterpret_cast<Fn*>(vtable + slot))(self, nullptr, args...);
}

}  // namespace

struct Message {
    std::uint32_t d[5];
};

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

// ---------------------------------------------------------------------------
// Construction and destruction
// ---------------------------------------------------------------------------

namespace {

void InitScreenElement(void* self) {
    At<std::uint32_t>(self, 0x4) = 0x1111;
    At<std::uint32_t>(self, 0x18) = 0;
    At<std::uint8_t>(self, 0x1c) = 0;
    At<std::uint8_t>(self, 0x1d) = 0;
    At<std::uint8_t>(self, 0x1e) = 1;
    At<std::uint32_t>(self, 0) = kScreenElementVtable;
}

}  // namespace

S3RE_HOOK(0x00415610, CScreenElement_Construct, CtorFn, Original_CScreenElement_Construct, CScreenElement_Construct);

// Purpose: Constructor of a UI element with an empty rectangle.
void* __fastcall CScreenElement_Construct(void* self, void* /*edx*/) {
    InitScreenElement(self);
    At<std::uint8_t>(self, 0x1f) = 0;
    std::memset(static_cast<std::uint8_t*>(self) + 8, 0, 16);
    return self;
}

S3RE_HOOK(0x00415660, CScreenElement_ConstructRect, CtorRectFn, Original_CScreenElement_ConstructRect,
          CScreenElement_ConstructRect);

// Purpose: Constructor of a UI element at a given screen rectangle. Byte
// +0x1f is left as it was (the original doesn't set it here).
void* __fastcall CScreenElement_ConstructRect(void* self, void* /*edx*/, const RECT* rect) {
    At<std::uint32_t>(self, 0x4) = 0x1111;
    std::memcpy(static_cast<std::uint8_t*>(self) + 8, rect, 16);
    InitScreenElement(self);
    return self;
}

S3RE_HOOK(0x004156b0, CScreenElement_Destroy, DtorFn, Original_CScreenElement_Destroy, CScreenElement_Destroy);

// Purpose: Destructor of a UI element: removes it from its parent.
std::uint32_t __fastcall CScreenElement_Destroy(void* self, void* /*edx*/) {
    At<std::uint32_t>(self, 0) = kScreenElementVtable;
    void* parent = At<void*>(self, 0x18);
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
    return &At<RECT>(self, 8);
}

S3RE_HOOK(0x004156e0, CScreenElement_SetRect, SetRectFn, Original_CScreenElement_SetRect, CScreenElement_SetRect);

// Purpose: Moves and/or resizes an element and tells it (virtual "moved"
// and "resized" handlers, with the change). EAX: the "resized" handler's
// result, or 0.
std::uint32_t __fastcall CScreenElement_SetRect(void* self, void* /*edx*/, const RECT* rect) {
    RECT& own = At<RECT>(self, 8);
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
    const RECT& own = At<RECT>(self, 8);
    const RECT moved{own.left + dx, own.top + dy, own.right + dx, own.bottom + dy};
    return CScreenElement_SetRect(self, nullptr, &moved);
}

S3RE_HOOK(0x00415800, CScreenElement_ContainsPoint, ContainsPointFn, Original_CScreenElement_ContainsPoint,
          CScreenElement_ContainsPoint);

// Purpose: Whether a screen point (16-bit coordinates) lies inside the
// element.
std::uint32_t __fastcall CScreenElement_ContainsPoint(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const POINT point{static_cast<LONG>(x & 0xffff), static_cast<LONG>(y & 0xffff)};
    return PtInRect(&At<RECT>(self, 8), point) != 0 ? 1 : 0;
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
    if (At<std::uint32_t>(self, 0x18) == parent) {
        return parent;
    }
    At<std::uint32_t>(self, 0x18) = parent;
    return CallVirtual<std::uint32_t>(self, 0x68, static_cast<std::uint32_t>(parent != 0 ? 1 : 0));
}

S3RE_HOOK(0x00415830, CScreenElement_NeedsRedraw, NoArgFn, Original_CScreenElement_NeedsRedraw,
          CScreenElement_NeedsRedraw);

// Purpose: Whether the element must be drawn again. Only AL is the result.
std::uint32_t __fastcall CScreenElement_NeedsRedraw(void* self, void* /*edx*/) {
    return At<std::uint8_t>(self, 0x1e);
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
    At<std::uint8_t>(self, 0x1c) = 1;
    if (void* parent = At<void*>(self, 0x18); parent != nullptr) {
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
    if (At<std::uint8_t>(self, 0x1c) != 0) {
        result = CallVirtual<std::uint32_t>(self, 0x64, 0u);
    }
    At<std::uint8_t>(self, 0x1c) = 0;
    return result;
}

S3RE_HOOK(0x00415bf0, CScreenElement_Invalidate, NoArgFn, Original_CScreenElement_Invalidate,
          CScreenElement_Invalidate);

// Purpose: Marks the element for redrawing and passes that on to its
// parent.
std::uint32_t __fastcall CScreenElement_Invalidate(void* self, void* /*edx*/) {
    At<std::uint8_t>(self, 0x1e) = 1;
    void* parent = At<void*>(self, 0x18);
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
    void* parent = At<void*>(self, 0x18);
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
    At<std::uint8_t>(self, 0x1f) = 1;
}

// ---------------------------------------------------------------------------
// Messages and drawing
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00415850, CScreenElement_OnMessage, MessageFn, Original_CScreenElement_OnMessage,
          CScreenElement_OnMessage);

// Purpose: Entry point for an input message: the element takes it if its
// "accepts" check agrees, then handles it.
std::uint32_t __fastcall CScreenElement_OnMessage(void* self, void* /*edx*/, Message message) {
    At<std::uint8_t>(self, 0x1c) = 1;
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
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(message);
    switch (bytes[0]) {
        case 1: case 2: case 3: case 0x30: case 0x40: case 0x20:
            return 1;
        default:
            return CScreenElement_ContainsPoint(self, nullptr, *reinterpret_cast<const std::uint16_t*>(bytes + 8),
                                                *reinterpret_cast<const std::uint16_t*>(bytes + 0xa));
    }
}

S3RE_HOOK(0x004158d0, CScreenElement_DispatchMessage, MessageFn, Original_CScreenElement_DispatchMessage,
          CScreenElement_DispatchMessage);

// Purpose: Passes a message to the element's handler for its type (one
// virtual function per type); 0 for types it has no handler for.
std::uint32_t __fastcall CScreenElement_DispatchMessage(void* self, void* /*edx*/, Message message) {
    std::size_t slot = 0;
    switch (static_cast<std::uint8_t>(message.d[0])) {
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
    const std::uint32_t x = message.d[2] & 0xffff;
    const std::uint32_t y = message.d[2] >> 16;
    const bool inside = static_cast<std::uint8_t>(CScreenElement_ContainsPoint(self, nullptr, x, y)) != 0;
    std::uint8_t& mouseInside = At<std::uint8_t>(self, 0x1d);
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
    At<std::uint8_t>(self, 0x1e) = 0;
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
    At<std::uint8_t>(self, 0x1e) = 0;
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
    return AtAddress<std::uint32_t>(0x007a823c);
}

S3RE_HOOK(0x004168c0, Dialog_Global7a8238, GlobalFn, Original_Dialog_Global7a8238, Dialog_Global7a8238);

// Purpose: Returns a dialog-system global (0x7a8238).
std::uint32_t __cdecl Dialog_Global7a8238() {
    return AtAddress<std::uint32_t>(0x007a8238);
}

S3RE_HOOK(0x00416c50, Dialog_Global7a8240, GlobalFn, Original_Dialog_Global7a8240, Dialog_Global7a8240);

// Purpose: Returns a dialog-system global (0x7a8240; a GDI object that
// 0x416760 deletes).
std::uint32_t __cdecl Dialog_Global7a8240() {
    return AtAddress<std::uint32_t>(0x007a8240);
}

S3RE_HOOK(0x00416880, Dialog_CopyGlobals7a8020, CopyGlobalsFn, Original_Dialog_CopyGlobals7a8020,
          Dialog_CopyGlobals7a8020);

// Purpose: Copies a 32-byte block of dialog settings (at 0x7a8020) out.
void* __stdcall Dialog_CopyGlobals7a8020(void* out) {
    auto* to = static_cast<std::uint32_t*>(out);
    for (int i = 0; i < 8; ++i) {
        to[i] = AtAddress<std::uint32_t>(0x007a8020 + 4 * i);
    }
    return out;
}

S3RE_HOOK(0x004168a0, Dialog_Table7a809c, TableFn, Original_Dialog_Table7a809c, Dialog_Table7a809c);

// Purpose: An entry of a dialog-system table (0x7a809c; probably fonts or
// colours by index).
std::uint32_t __stdcall Dialog_Table7a809c(std::uint32_t index) {
    return AtAddress<std::uint32_t>(0x007a809c + index * 4);
}

S3RE_HOOK(0x004168b0, Dialog_Table7a8064, TableFn, Original_Dialog_Table7a8064, Dialog_Table7a8064);

// Purpose: An entry of another dialog-system table (0x7a8064, 14 entries).
std::uint32_t __stdcall Dialog_Table7a8064(std::uint32_t index) {
    return AtAddress<std::uint32_t>(0x007a8064 + index * 4);
}

// ---------------------------------------------------------------------------
// Group class (virtual base CScreenElement through the vbtable at this-0x7c)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00417100, UiGroup_WantsMessage, MessagePtrFn, Original_UiGroup_WantsMessage, UiGroup_WantsMessage);

// Purpose: Whether a message concerns a group: if it concerns its element
// part, or unless its byte -0x78 (probably "only inside") is set. AL only.
std::uint32_t __fastcall UiGroup_WantsMessage(void* self, void* /*edx*/, const Message* message) {
    void* base = VirtualBase(self, -0x7c);
    if (static_cast<std::uint8_t>(CScreenElement_WantsMessage(base, nullptr, message)) != 0) {
        return 1;
    }
    return At<std::uint8_t>(self, -0x78) != 0 ? 0 : 1;
}

S3RE_HOOK(0x00417650, UiGroup_SetFocus, Arg1Fn, Original_UiGroup_SetFocus, UiGroup_SetFocus);

// Purpose: Gives a group the focus and records which child has it,
// telling the previous child (virtual 0x10).
std::uint32_t __fastcall UiGroup_SetFocus(void* self, void* /*edx*/, std::uint32_t child) {
    const std::uint32_t result = CScreenElement_SetFocus(VirtualBase(self, -0x7c), nullptr, child);
    void* old = At<void*>(self, -0x58);
    if (reinterpret_cast<std::uint32_t>(old) == child) {
        return result;
    }
    At<std::uint32_t>(self, -0x58) = child;
    if (old == nullptr) {
        return result;
    }
    return CallVirtual<std::uint32_t>(old, 0x10);
}

S3RE_HOOK(0x00417680, UiGroup_KillFocus, NoArgFn, Original_UiGroup_KillFocus, UiGroup_KillFocus);

// Purpose: Takes the focus from a group and from its focused child. Only AL
// of EAX is defined.
std::uint32_t __fastcall UiGroup_KillFocus(void* self, void* /*edx*/) {
    void* old = At<void*>(self, -0x58);
    At<std::uint32_t>(self, -0x58) = 0;
    if (old != nullptr) {
        CallVirtual<std::uint32_t>(old, 0x10);
    }
    return CScreenElement_KillFocus(VirtualBase(self, -0x7c), nullptr);
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
// (vbtable at this-0xb4).
std::uint32_t __fastcall UiGroup_Enable_429fd0(void* self, void* /*edx*/, std::uint32_t enable) {
    return CScreenElement_Enable(VirtualBase(self, -0xb4), nullptr, enable);
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
    -1, 1, -1, 1, 0, -1, -1, 0,   // 0x04, 0x0c, 0x10, 0x1c
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
};
ProbeLog* g_probeLog = nullptr;

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
constexpr std::size_t kBuffer = 0x500;
constexpr std::size_t kElement = 0x100;
constexpr std::size_t kParent = 0x200;
constexpr std::size_t kGroup = 0x300;
constexpr std::size_t kChild = 0x380;
constexpr std::size_t kEnableThis = 0x400;
const std::int32_t kParentVbtable[2] = {0, 0x20};
const std::int32_t kGroupVbtable[2] = {0, static_cast<std::int32_t>(kElement) - 0x284};
const std::int32_t kEnableVbtable[2] = {0, static_cast<std::int32_t>(kElement) - 0x34c};

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
};
constexpr int kOperations = 36;

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
            Original_UiGroup_WantsMessage_429df0,  Original_UiGroup_Enable_429fd0};
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
            &UiGroup_SetFocus_429d70,      &UiGroup_WantsMessage_429df0,  &UiGroup_Enable_429fd0};
}

struct Inputs {
    RECT rects[2];
    Message message;
    std::uint32_t a, b;
};

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
        case 5: return f.setRect(element, nullptr, (in.b & 3) == 0 ? &At<RECT>(element, 8) : &in.rects[0]);
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
        case 16: return ptr(f.intersect(element, nullptr, &At<RECT>(buffer, 0x40), &in.rects[0], &in.rects[1]));
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
        case 31: return f.groupSetFocus(group, nullptr, (in.b & 1) ? ptr(buffer + kChild) : At<std::uint32_t>(buffer, 0x2a8));
        case 32: return f.groupKillFocus(group, nullptr) & 0xff;
        case 33: return f.groupSetFocus429d70(group, nullptr, (in.b & 1) ? ptr(buffer + kChild) : in.a);
        case 34: return f.groupWants429df0(group, nullptr, &in.message) & 0xff;
        default: return f.groupEnable429fd0(buffer + kEnableThis, nullptr, in.a);
    }
}

void SetUpBuffer(test::Context& ctx, std::uint8_t* buffer) {
    ctx.rng().Fill(buffer, kBuffer);
    // Small rectangle coordinates, so points and rectangles overlap.
    for (std::size_t offset = kElement + 8; offset < kElement + 0x18; offset += 4) {
        At<std::int32_t>(buffer, offset) = ctx.rng().Range(-8, 40);
    }
    void* vtable = const_cast<void**>(FakeVtable().data());
    At<void*>(buffer, kElement) = vtable;
    At<void*>(buffer, kParent) = vtable;
    At<const void*>(buffer, kParent + 4) = kParentVbtable;
    At<void*>(buffer, kParent + 0x24) = vtable;
    At<void*>(buffer, kChild) = vtable;
    At<std::uint32_t>(buffer, kElement + 0x18) = ctx.rng().Chance(1, 3) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kParent);
    At<std::uint8_t>(buffer, kElement + 0x1c) = static_cast<std::uint8_t>(ctx.rng().Below(2));
    At<std::uint8_t>(buffer, kElement + 0x1d) = static_cast<std::uint8_t>(ctx.rng().Below(3));
    At<const void*>(buffer, kGroup - 0x7c) = kGroupVbtable;
    At<std::uint32_t>(buffer, kGroup - 0x58) = ctx.rng().Chance(1, 3) ? 0 : reinterpret_cast<std::uint32_t>(buffer + kChild);
    At<std::uint8_t>(buffer, kGroup - 0x78) = static_cast<std::uint8_t>(ctx.rng().Below(2));
    At<const void*>(buffer, kEnableThis - 0xb4) = kEnableVbtable;
}

void SetUpInputs(test::Context& ctx, Inputs& in) {
    for (RECT& r : in.rects) {
        r = {ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40), ctx.rng().Range(-8, 40)};
    }
    ctx.rng().Fill(&in.message, sizeof(in.message));
    static const std::uint8_t kTypes[] = {1, 2, 3, 0x10, 0x12, 0x13, 0x15, 0x16, 0x20, 0x30, 0x40, 0, 5, 0x41, 0xff};
    reinterpret_cast<std::uint8_t*>(&in.message)[0] = kTypes[ctx.rng().Below(sizeof(kTypes))];
    reinterpret_cast<std::uint16_t*>(&in.message)[4] = static_cast<std::uint16_t>(ctx.rng().Range(-8, 40));
    reinterpret_cast<std::uint16_t*>(&in.message)[5] = static_cast<std::uint16_t>(ctx.rng().Range(-8, 40));
    in.a = ctx.rng().Chance(1, 2) ? ctx.rng().Below(3) : ctx.rng().Next32();
    in.b = ctx.rng().Chance(1, 2) ? static_cast<std::uint32_t>(ctx.rng().Range(-4, 4)) : ctx.rng().Next32();
}

void CompareScreenElement(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original = OriginalFunctions();
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
    if (At<std::uint8_t>(self, 0x1d) == 0) {
        const std::uint32_t x = message.d[2] & 0xffff;
        const std::uint32_t y = message.d[2] >> 16;
        if (CScreenElement_ContainsPoint(self, nullptr, x, y) == 0) {
            return CallVirtual<std::uint32_t>(self, 0x60, message);
        }
    }
    return CScreenElement_TrackMouse(self, edx, message);
}

}  // namespace

S3RE_TEST(ScreenElement_MatchOriginal) {
    CompareScreenElement(ctx, OurFunctions(), 50000);
}

S3RE_TEST(ScreenElement_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = OurFunctions();
    wrong.trackMouse = &WrongTrackMouse;
    CompareScreenElement(probe, wrong, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
