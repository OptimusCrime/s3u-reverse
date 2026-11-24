#pragma once

// Object layouts of the CScreenElement family of UI classes (Dirk\dialog.cpp;
// screen_element.cpp, ui_static_text.cpp), named after their RTTI classes.
// Only the fields our replacements use are named; unkXX = unknown. See
// screen_element.cpp for what each field does.
//
// CScreenGroup and CControl derive virtually from CScreenElement (RTTI), so
// their objects start with their own vtable and a vbtable pointer, and the
// CScreenElement part (the virtual base) sits behind their own fields; the
// structs below are those non-virtual parts. Their methods get the address
// of the CScreenElement part as "this" (GroupOf / ControlOf go back).

#include "msvc6/string.h"
#include "watcom/ptr_vector.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace s3re::game::ui {

#pragma pack(push, 1)

// CScreenElement (vtable 0x655a68, 32 slots), 0x20 bytes. Constructor
// 0x415610.
struct CScreenElement {
    std::uint32_t vtable;       // 0x00
    std::uint32_t background;   // 0x04 fill colour (0x1111 after construction)
    RECT rect;                  // 0x08 screen coordinates
    std::uint32_t parent;       // 0x18 the container: a CScreenGroup (sub)object
    std::uint8_t has_focus;     // 0x1c
    std::uint8_t mouse_inside;  // 0x1d
    std::uint8_t needs_redraw;  // 0x1e
    std::uint8_t flag1f;        // 0x1f set by vf06; probably "transparent" (no background fill)
};
static_assert(sizeof(CScreenElement) == 0x20);
static_assert(offsetof(CScreenElement, rect) == 0x08);
static_assert(offsetof(CScreenElement, parent) == 0x18);
static_assert(offsetof(CScreenElement, flag1f) == 0x1f);

// CStaticText (vtable 0x65615c): a CStaticControl (a CScreenElement with
// its own vtable) with a text, 0x44 bytes.
struct CStaticText {
    CScreenElement element;     // 0x00
    std::uint8_t unk20;         // 0x20 set to 0
    std::uint8_t unk21[7];
    msvc6::String text;         // 0x28
    std::uint8_t unk38;         // 0x38 set to 0
    std::uint8_t unk39[3];
    std::uint32_t font;         // 0x3c 0x24 when 0 is passed
    std::uint32_t style;        // 0x40 alignment or style (5 by default; the tooltip sets 0xc)
};
static_assert(offsetof(CStaticText, text) == 0x28);
static_assert(offsetof(CStaticText, unk38) == 0x38);
static_assert(offsetof(CStaticText, font) == 0x3c);
static_assert(sizeof(CStaticText) == 0x44);

// CScreenGroup (the container of child elements; RTTI: virtual base
// CScreenElement at +0x80 in a plain CScreenGroup), its non-virtual part.
// Constructor around 0x416ca0: vtable 0x655b70 (2 slots), the element's
// 0x655aec, vtordisp at +0x7c.
struct CScreenGroup {
    std::uint32_t vtable;          // 0x00 (this-0x80)
    const std::int32_t* vbptr;     // 0x04 (this-0x7c) vbtable leading to the CScreenElement virtual base
    std::uint8_t only_inside;      // 0x08 (this-0x78) 1 after construction; probably "only messages
                                   //      inside"; also picks Draw's clip
    std::uint8_t unk09[3];
    watcom::PtrVector children;    // 0x0c (this-0x74) length at this-0x60
    std::uint32_t unk24;           // 0x24 (this-0x5c) 5 after construction
    std::uint32_t focus;           // 0x28 (this-0x58) focused child
    std::uint32_t unk2c;           // 0x2c (this-0x54)
    std::uint32_t captured;        // 0x30 (this-0x50) child that captured the input
    std::uint8_t unk34[0x18];      // 0x34 a Watcom container (vtable 0x655b8c)
    std::uint8_t unk4c[0x18];      // 0x4c a Watcom container (vtable 0x655b8c)
    std::uint8_t unk64[0x18];      // 0x64 a Watcom container (vtable 0x655b7c)
    std::int32_t vtordisp;         // 0x7c (this-0x4) the virtual base's displacement correction
};
static_assert(offsetof(CScreenGroup, vbptr) == 0x80 - 0x7c);
static_assert(offsetof(CScreenGroup, only_inside) == 0x80 - 0x78);
static_assert(offsetof(CScreenGroup, children) == 0x80 - 0x74);
static_assert(offsetof(CScreenGroup, children) + offsetof(watcom::PtrVector, length) == 0x80 - 0x60);
static_assert(offsetof(CScreenGroup, focus) == 0x80 - 0x58);
static_assert(offsetof(CScreenGroup, captured) == 0x80 - 0x50);
static_assert(offsetof(CScreenGroup, vtordisp) == 0x7c);
static_assert(sizeof(CScreenGroup) == 0x80);

// CScreen (a whole screen or menu page; RTTI: base CScreenGroup at +0,
// virtual base CScreenElement at +0x98 in a plain CScreen), its non-virtual
// part. Its group part's "this" (+0x80 of the CScreenGroup) is 0x18 bytes
// before its own.
struct CScreen {
    CScreenGroup group;          // 0x00
    std::uint8_t unk80[0x18];    // 0x80
};
static_assert(sizeof(CScreen) == 0x98);

// CControl (a UI element with a tooltip; RTTI: virtual base CScreenElement
// at +0x3c), its non-virtual part. Constructor around 0x418000: vtable
// 0x655d28 (1 slot), the element's 0x655ca4, vtordisp at +0x38.
// CControl_DeleteTooltip gets the control itself.
struct CControl {
    std::uint32_t vtable;          // 0x00 (this-0x3c)
    const std::int32_t* vbptr;     // 0x04 (this-0x38) vbtable leading to the CScreenElement virtual base
    std::uint32_t timer_functor;   // 0x08 a functor called on every timer tick (CControl::OnTimer 0x4183c0), 0 = none
    std::uint8_t disabled;         // 0x0c = !enabled (CControl::SetEnabled 0x418810)
    std::uint8_t pad0d[3];
    msvc6::String tooltip_text;    // 0x10
    std::uint32_t tooltip;         // 0x20 (this-0x1c) the tooltip object
    std::uint32_t last_move_time;  // 0x24 (this-0x18) timeGetTime() of the last mouse movement
    std::uint32_t mouse_x;         // 0x28 (this-0x14)
    std::uint32_t mouse_y;         // 0x2c (this-0x10)
    std::uint32_t tooltip_x;       // 0x30 (this-0xc) where the mouse was when the tooltip opened
    std::uint32_t tooltip_y;       // 0x34 (this-0x8)
    std::int32_t vtordisp;         // 0x38 (this-0x4) the virtual base's displacement correction
};
static_assert(offsetof(CControl, vbptr) == 0x3c - 0x38);
static_assert(offsetof(CControl, timer_functor) == 0x08);
static_assert(offsetof(CControl, disabled) == 0x0c);
static_assert(offsetof(CControl, tooltip_text) == 0x10);
static_assert(offsetof(CControl, tooltip) == 0x3c - 0x1c);
static_assert(offsetof(CControl, last_move_time) == 0x3c - 0x18);
static_assert(offsetof(CControl, tooltip_x) == 0x3c - 0xc);
static_assert(sizeof(CControl) == 0x3c);

#pragma pack(pop)

// Byte distance from the start of these parts to the pointer the methods
// get ("this", the CScreenElement part).
constexpr std::ptrdiff_t kGroupThis = sizeof(CScreenGroup);  // 0x80
constexpr std::ptrdiff_t kControlThis = sizeof(CControl);    // 0x3c

inline CScreenElement* ElementOf(void* self) {
    return static_cast<CScreenElement*>(self);
}

inline CScreenGroup* GroupOf(void* self) {
    return reinterpret_cast<CScreenGroup*>(static_cast<std::uint8_t*>(self) - kGroupThis);
}

inline CControl* ControlOf(void* self) {
    return reinterpret_cast<CControl*>(static_cast<std::uint8_t*>(self) - kControlThis);
}

}  // namespace s3re::game::ui
