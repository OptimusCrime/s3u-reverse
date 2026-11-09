#pragma once

// Object layouts of the CScreenElement family of UI classes (Dirk\dialog.cpp;
// screen_element.cpp, ui_static_text.cpp). Only the fields our replacements
// use are named; unkXX = unknown. See screen_element.cpp for what each field
// does and how the subobjects of the derived classes are reached.

#include "msvc6/string.h"
#include "watcom/ptr_vector.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace s3re::game::ui {

#pragma pack(push, 1)

// CScreenElement (vtable 0x655a68), 0x20 bytes.
struct ScreenElement {
    std::uint32_t vtable;       // 0x00
    std::uint32_t background;   // 0x04 fill colour (0x1111 after construction)
    RECT rect;                  // 0x08 screen coordinates
    std::uint32_t parent;       // 0x18 container (its vbtable at +4 leads to its CScreenElement part)
    std::uint8_t has_focus;     // 0x1c
    std::uint8_t mouse_inside;  // 0x1d
    std::uint8_t needs_redraw;  // 0x1e
    std::uint8_t flag1f;        // 0x1f set by vf06; probably "transparent" (no background fill)
};
static_assert(sizeof(ScreenElement) == 0x20);
static_assert(offsetof(ScreenElement, rect) == 0x08);
static_assert(offsetof(ScreenElement, parent) == 0x18);
static_assert(offsetof(ScreenElement, flag1f) == 0x1f);

// CStaticText (vtable 0x65615c): a CStaticControl (a CScreenElement with
// its own vtable) with a text, 0x44 bytes.
struct StaticText {
    ScreenElement element;      // 0x00
    std::uint8_t unk20;         // 0x20 set to 0
    std::uint8_t unk21[7];
    msvc6::String text;         // 0x28
    std::uint8_t unk38;         // 0x38 set to 0
    std::uint8_t unk39[3];
    std::uint32_t font;         // 0x3c 0x24 when 0 is passed
    std::uint32_t style;        // 0x40 alignment or style (5 by default; the tooltip sets 0xc)
};
static_assert(offsetof(StaticText, text) == 0x28);
static_assert(offsetof(StaticText, unk38) == 0x38);
static_assert(offsetof(StaticText, font) == 0x3c);
static_assert(sizeof(StaticText) == 0x44);

// The group part of CScreenGroup (the container of child elements) as its
// methods see it: they get a pointer 0x7c bytes past the start of this
// struct (GroupOf), so the fields are at this-0x7c .. this-0x50.
struct ScreenGroupPart {
    const std::int32_t* vbptr;     // 0x00 (this-0x7c) vbtable leading to the CScreenElement virtual base
    std::uint8_t only_inside;      // 0x04 (this-0x78) probably "only messages inside"; also picks Draw's clip
    std::uint8_t unk05[3];
    watcom::PtrVector children;    // 0x08 (this-0x74) length at this-0x60
    std::uint32_t unk20;           // 0x20 (this-0x5c)
    std::uint32_t focus;           // 0x24 (this-0x58) focused child
    std::uint32_t unk28;           // 0x28 (this-0x54)
    std::uint32_t captured;        // 0x2c (this-0x50) child that captured the input
    std::uint8_t unk30[0x7c - 0x30];
};
static_assert(offsetof(ScreenGroupPart, only_inside) == 0x7c - 0x78);
static_assert(offsetof(ScreenGroupPart, children) == 0x7c - 0x74);
static_assert(offsetof(ScreenGroupPart, children) + offsetof(watcom::PtrVector, length) == 0x7c - 0x60);
static_assert(offsetof(ScreenGroupPart, focus) == 0x7c - 0x58);
static_assert(offsetof(ScreenGroupPart, captured) == 0x7c - 0x50);
static_assert(sizeof(ScreenGroupPart) == 0x7c);

// CControl (a UI element with a tooltip). Its methods get the address of
// the CScreenElement part, 0x3c bytes past the start of this struct
// (ControlOf); CControl_DeleteTooltip gets the control itself.
struct ControlPart {
    std::uint32_t unk00;
    const std::int32_t* vbptr;     // 0x04 (this-0x38) vbtable leading to the CScreenElement virtual base
    std::uint32_t tooltip_owner;   // 0x08 an object told when the tooltip time is up (0x4183c0)
    std::uint32_t unk0c;
    msvc6::String tooltip_text;    // 0x10
    std::uint32_t tooltip;         // 0x20 (this-0x1c) the tooltip object
    std::uint32_t last_move_time;  // 0x24 (this-0x18) timeGetTime() of the last mouse movement
    std::uint32_t mouse_x;         // 0x28 (this-0x14)
    std::uint32_t mouse_y;         // 0x2c (this-0x10)
    std::uint32_t tooltip_x;       // 0x30 (this-0xc) where the mouse was when the tooltip opened
    std::uint32_t tooltip_y;       // 0x34 (this-0x8)
    std::uint32_t unk38;
};
static_assert(offsetof(ControlPart, vbptr) == 0x3c - 0x38);
static_assert(offsetof(ControlPart, tooltip_text) == 0x10);
static_assert(offsetof(ControlPart, tooltip) == 0x3c - 0x1c);
static_assert(offsetof(ControlPart, last_move_time) == 0x3c - 0x18);
static_assert(offsetof(ControlPart, tooltip_x) == 0x3c - 0xc);
static_assert(sizeof(ControlPart) == 0x3c);

#pragma pack(pop)

// Byte distance from the start of these parts to the pointer the methods
// get ("this").
constexpr std::ptrdiff_t kGroupThis = sizeof(ScreenGroupPart);  // 0x7c
constexpr std::ptrdiff_t kControlThis = sizeof(ControlPart);    // 0x3c

inline ScreenElement* ElementOf(void* self) {
    return static_cast<ScreenElement*>(self);
}

inline ScreenGroupPart* GroupOf(void* self) {
    return reinterpret_cast<ScreenGroupPart*>(static_cast<std::uint8_t*>(self) - kGroupThis);
}

inline ControlPart* ControlOf(void* self) {
    return reinterpret_cast<ControlPart*>(static_cast<std::uint8_t*>(self) - kControlThis);
}

}  // namespace s3re::game::ui
