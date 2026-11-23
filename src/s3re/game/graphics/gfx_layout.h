#pragma once

// Layout of the loaded graphics (class CGraphicsData, Dirk\graphicsdata.cpp)
// and of the per-group tables it points into. The game holds one instance
// (pointer at 0x7a8010, created by CDrawer's constructor 0x42c820 and by
// 0x493300); the simulation reads it for animation lengths.
//
// Evidence: constructor 0x5f67c0 (operator new 0xc44; CObject base, vtable
// 0x65d3e4, a std::string at +0x24, 70 Win32::CFileMapping objects at
// +0x14c, +0x264.. zeroed, +0xc3c/+0xc40 zeroed), destructor 0x5f68b0 and
// Close 0x5fa690 (for every group with a mapped view: UnmapView(view)
// 0x61aa40 and the mapping's slot 1; then the view and the eight table
// arrays +0x37c..+0xb24 are zeroed). Every per-group array has 70 entries
// (0x46, the constructor's and Close's loop count; 0x118 bytes apart).
// The original never checks the group number, so code indexes the arrays
// with any group (the same address arithmetic as the original).

#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::gfx {

static_assert(sizeof(void*) == 4, "the layouts below assume a 32-bit build");

constexpr std::size_t kGroups = 70;

#pragma pack(push, 1)

// A group's frame-offset table (CGraphicsData::frameTables; read by
// CGraphicsData::VariantFrameAddress 0x5faaa0): frames per variant, then one
// offset (from the group's view) per frame, variant by variant.
struct FrameTable {
    std::uint8_t unk00[8];
    std::uint16_t framesPerVariant;  // +0x08
    std::uint16_t unk0a;             // +0x0a
    std::uint32_t offsets[1];        // +0x0c variable length: [framesPerVariant * variant + frame]
};
static_assert(offsetof(FrameTable, framesPerVariant) == 0x08);
static_assert(offsetof(FrameTable, offsets) == 0x0c);

// A table of per-frame offsets (from the group's view) with a count:
// CGraphicsData::animationTables (CGraphicsData::AnimationPointer 0x5faaf0
// checks the count) and ::frameTables5ac (CGraphicsData::FramePointer
// 0x5fac70, no check).
struct OffsetTable {
    std::uint8_t unk00[6];
    std::uint16_t count;        // +0x06 (the animation table's number of entries)
    std::uint32_t offsets[1];   // +0x08 variable length
};
static_assert(offsetof(OffsetTable, count) == 0x06);
static_assert(offsetof(OffsetTable, offsets) == 0x08);

// A group's sprite table (CGraphicsData::spriteTables; CGraphicsData::FrameCount
// 0x5fad20): per sprite (animation) the address of its header, relative to
// the group's view (the view is added); the header's first word is the
// number of frames.
struct SpriteTable {
    std::uint32_t unk00[2];
    std::uint32_t sprites[1];  // +0x08 variable length
};
static_assert(offsetof(SpriteTable, sprites) == 0x08);

// CGraphicsData (RTTI; base Win32::CObject), 0xc44 bytes.
struct CGraphicsData {
    std::uint32_t vtable;                        // +0x000 0x65d3e4 (slot 0: scalar deleting destructor 0x5f6890)
    std::uint8_t unk004[0x24 - 0x04];
    msvc6::String name;                          // +0x024 probably the GFX folder or file name
    std::uint32_t unk034[kGroups];               // +0x034 not cleared by Close
    std::uint32_t mappings[kGroups];             // +0x14c Win32::CFileMapping* per group (new 0x2c, ctor 0x61a3a0)
    std::uint32_t views[kGroups];                // +0x264 the group's mapped file view (0: not loaded); every
                                                 //        table offset is relative to it
    const FrameTable* frameTables[kGroups];      // +0x37c
    const OffsetTable* animationTables[kGroups]; // +0x494
    const OffsetTable* frameTables5ac[kGroups];  // +0x5ac frame tables read by FramePointer
    std::uint32_t unk6c4[kGroups];               // +0x6c4 a table per group (zeroed by Close)
    std::uint32_t unk7dc[kGroups];               // +0x7dc a table per group (zeroed by Close)
    std::uint32_t unk8f4[kGroups];               // +0x8f4 a table per group (zeroed by Close)
    std::uint32_t unka0c[kGroups];               // +0xa0c a table per group (zeroed by Close)
    const SpriteTable* spriteTables[kGroups];    // +0xb24
    std::uint32_t unkc3c;                        // +0xc3c zeroed by the constructor and Close
    std::uint32_t currentVariant;                // +0xc40 the variant VariantFrameAddress uses for -1
};
static_assert(offsetof(CGraphicsData, name) == 0x024);
static_assert(offsetof(CGraphicsData, unk034) == 0x034);
static_assert(offsetof(CGraphicsData, mappings) == 0x14c);
static_assert(offsetof(CGraphicsData, views) == 0x264);
static_assert(offsetof(CGraphicsData, frameTables) == 0x37c);
static_assert(offsetof(CGraphicsData, animationTables) == 0x494);
static_assert(offsetof(CGraphicsData, frameTables5ac) == 0x5ac);
static_assert(offsetof(CGraphicsData, unk6c4) == 0x6c4);
static_assert(offsetof(CGraphicsData, unk7dc) == 0x7dc);
static_assert(offsetof(CGraphicsData, unk8f4) == 0x8f4);
static_assert(offsetof(CGraphicsData, unka0c) == 0xa0c);
static_assert(offsetof(CGraphicsData, spriteTables) == 0xb24);
static_assert(offsetof(CGraphicsData, unkc3c) == 0xc3c);
static_assert(offsetof(CGraphicsData, currentVariant) == 0xc40);
static_assert(sizeof(CGraphicsData) == 0xc44);

#pragma pack(pop)

}  // namespace s3re::game::gfx
