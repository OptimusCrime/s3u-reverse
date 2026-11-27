#pragma once

// The draw-data buffer (DrawData, 0xc7720 bytes, filled by
// CLogicThread::PrepareDrawData 0x51f110; constructor in
// logic/draw_data.cpp) as the drawing code reads it: the fields the
// drawer functions use, and the 130 x 130 grid of 48-byte cells at +0x500
// (the visible part of the map, [column][row]). Only the fields our code
// reads are named; meanings without evidence are left as unkXXX with how
// they are used.

#include "msvc6/wstring.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

// One visible map cell (48 bytes).
struct DrawCell {
    std::uint8_t lift;            // +0x00 drawn this many pixels higher (the terrain height)
    std::uint8_t unk01;
    std::uint8_t unk02;           // +0x02 compared with 0x50, 2..7 and 7 by QueueLandscapeObject (probably the terrain)
    std::uint8_t object;          // +0x03 the landscape object (tile byte 7)
    std::uint8_t owner;           // +0x04 player (0xff: none)
    std::uint8_t direction;       // +0x05 low 6 bits: the sprites' parameter / 4
    std::uint8_t unk06;
    std::uint8_t marker;          // +0x07 1..0x20: a marker drawn on the cell (QueueLandscapeObject's tail)
    std::uint16_t seen_object;    // +0x08 (0xf800 bits 0x1800: an object seen under the fog, low byte)
    std::uint16_t animation;      // +0x0a low 7 bits: the frame + 1 (0 = nothing), bits 7..14: the object type
    std::uint8_t unk0c;           // +0x0c low nibble: a level 0..15 (QueueLandscapeObject's water animation)
    std::uint8_t unk0d[0x30 - 0x0d];
};
static_assert(sizeof(DrawCell) == 0x30);

constexpr std::uint32_t kDrawCellRows = 130;

// The message texts of the draw data (filled by PrepareDrawData 0x51f110
// from the logic thread; drawn by CDrawer::RenderMessageTexts 0x4d82b0).
struct DrawMessageHeader {
    msvc6::WString text;          // +0x00 the text (16-bit characters)
    std::uint32_t unk10;          // +0x10 set by PrepareDrawData
    std::uint8_t shown;           // +0x14 drawn when set
    std::uint8_t unk15[3];
};
static_assert(sizeof(DrawMessageHeader) == 0x18);

struct DrawMessageLine {
    msvc6::WString text;          // +0x00
    std::uint32_t unk10;          // +0x10 the text is drawn only when not 0
    std::uint32_t unk14;          // +0x14
    std::uint8_t shown;           // +0x18 the line's rect is cleared (and its text drawn) when set
    std::uint8_t unk19[3];
};
static_assert(sizeof(DrawMessageLine) == 0x1c);
constexpr std::size_t kDrawMessageLines = 20;

struct DrawDataView {
    std::uint8_t unk000[0x24];
    std::int32_t origin_column;   // +0x024 map column of cell column 0
    std::int32_t origin_row;      // +0x028 map row of cell row 0
    std::uint8_t unk02c[0x48 - 0x2c];
    std::uint32_t clock;          // +0x048 the animation clock (cycling animations are phased by it)
    std::uint8_t unk04c[0x5c - 0x4c];
    std::int32_t sound_level;     // +0x05c -1: no settler sounds; else a settler's work sound plays when its
                                  //        high byte is <= this (CDrawer::QueueSettler 0x4c85a0)
    std::int32_t unk060;          // +0x060 a UI mode (1 and 4 draw markers in QueueLandscapeObject)
    std::int32_t unk064;          // +0x064 in mode 1: a group 1..4 of the selection sprites
    std::int32_t unk068;          // +0x068 in mode 1: an item of that group
    std::uint8_t unk06c[0x74 - 0x6c];
    std::uint32_t unk074;         // +0x074 in mode 4: 2..13 draw the marker
    std::uint8_t unk078[0x94 - 0x78];
    std::int32_t unk094;          // +0x094 a variant 0..3 of some selection sprites (probably the race)
    std::int32_t local_player;    // +0x098 the player whose settlers get hit areas (CDrawer::QueueSettler)
    std::uint8_t unk09c[0xa4 - 0x9c];
    std::int32_t unk0a4;          // +0x0a4 in mode 4: one of 11 values draws the marker
    std::uint8_t unk0a8[0x264 - 0xa8];
    std::int32_t selected_column; // +0x264 the cell (column, row) the selection sprites are drawn at
    std::int32_t selected_row;    // +0x268
    std::uint8_t unk26c[0x294 - 0x26c];
    std::int32_t player_team[20]; // +0x294 per player: a thief of another team is drawn as a carrier
                                  //        (CDrawer::QueueSettler; indexed by any owner byte)
    std::uint8_t unk2e4[0x500 - 0x2e4];
    DrawCell cells[kDrawCellRows][kDrawCellRows];  // +0x500 [column][row]
    std::uint8_t unkc65c0[0xc66c0 - 0xc65c0];
    DrawMessageHeader message_header;     // +0xc66c0 the line above the messages
    DrawMessageLine messages[kDrawMessageLines];  // +0xc66d8 (CDrawer::RenderMessageTexts 0x4d82b0)
};
static_assert(offsetof(DrawDataView, origin_column) == 0x24);
static_assert(offsetof(DrawDataView, clock) == 0x48);
static_assert(offsetof(DrawDataView, sound_level) == 0x5c);
static_assert(offsetof(DrawDataView, unk060) == 0x60);
static_assert(offsetof(DrawDataView, local_player) == 0x98);
static_assert(offsetof(DrawDataView, player_team) == 0x294);
static_assert(offsetof(DrawDataView, unk074) == 0x74);
static_assert(offsetof(DrawDataView, unk094) == 0x94);
static_assert(offsetof(DrawDataView, unk0a4) == 0xa4);
static_assert(offsetof(DrawDataView, selected_column) == 0x264);
static_assert(offsetof(DrawDataView, cells) == 0x500);
static_assert(offsetof(DrawDataView, message_header) == 0xc66c0);
static_assert(offsetof(DrawDataView, messages) == 0xc66d8);

#pragma pack(pop)

}  // namespace s3re::game
