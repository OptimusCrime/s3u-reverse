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

// A group's text table (CGraphicsData::textTables; read by
// CGraphicsData::TextAddress 0x5faaa0; groups 07/08/09/99): texts per
// language, the language count, then one offset (from the group's view) to
// a C string per text, language by language (8 languages, see
// re/graphics_formats.md; was "FrameTable" with frames per variant).
struct TextTable {
    std::uint8_t unk00[8];
    std::uint16_t textsPerLanguage;  // +0x08
    std::uint16_t languages;         // +0x0a (8)
    std::uint32_t offsets[1];        // +0x0c variable length: [textsPerLanguage * language + text]
};
static_assert(offsetof(TextTable, textsPerLanguage) == 0x08);
static_assert(offsetof(TextTable, languages) == 0x0a);
static_assert(offsetof(TextTable, offsets) == 0x0c);

// A table of per-frame offsets (from the group's view) with a count:
// CGraphicsData::animationTables (CGraphicsData::AnimationPointer 0x5faaf0
// checks the count), colorIndexedTables and shadowTables (the same check),
// guiImageTables and landscapeTextureTables (no check).
struct OffsetTable {
    std::uint8_t unk00[6];
    std::uint16_t count;        // +0x06 (the animation table's number of entries)
    std::uint32_t offsets[1];   // +0x08 variable length
};
static_assert(offsetof(OffsetTable, count) == 0x06);
static_assert(offsetof(OffsetTable, offsets) == 0x08);

// A group's palette table (CGraphicsData::paletteTables; group 01; read by
// CGraphicsData::PalettePointer 0x5fac10): like OffsetTable with one more
// dword (0x100) before the offsets. Each palette is 8 brightness levels of
// 256 16-bit colours (0x200 bytes per level).
struct PaletteTable {
    std::uint8_t unk00[6];
    std::uint16_t count;        // +0x06 number of palettes (20 in siedler3_01)
    std::uint32_t unk08;        // +0x08 0x100 in the files
    std::uint32_t offsets[1];   // +0x0c variable length
};
static_assert(offsetof(PaletteTable, count) == 0x06);
static_assert(offsetof(PaletteTable, offsets) == 0x0c);

// A group's sequence table (CGraphicsData::sequenceTables;
// CGraphicsData::SequenceFrameCount 0x5fad20): per sequence (animation) the address of its header, relative to
// the group's view (the view is added); the header's first word is the
// number of frames.
struct SequenceTable {
    std::uint32_t unk00[2];
    std::uint32_t sequences[1];  // +0x08 variable length
};
static_assert(offsetof(SequenceTable, sequences) == 0x08);

// DirectDraw's DDSURFACEDESC as the drawing code uses it (passed by
// pointer to the Draw* functions and by value to the blitters).
struct SurfaceDesc {
    std::uint8_t unk00[0x10];
    std::int32_t pitch;       // +0x10 lPitch, bytes per row
    std::uint8_t unk14[0x10];
    std::uint32_t surface;    // +0x24 lpSurface
    std::uint8_t unk28[0x6c - 0x28];
};
static_assert(offsetof(SurfaceDesc, pitch) == 0x10);
static_assert(offsetof(SurfaceDesc, surface) == 0x24);
static_assert(sizeof(SurfaceDesc) == 0x6c);

// A clip rectangle (left/top inclusive, right/bottom exclusive).
struct ClipRect {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

// CGraphicsData (RTTI; base Win32::CObject), 0xc44 bytes.
struct CGraphicsData {
    std::uint32_t vtable;                        // +0x000 0x65d3e4 (slot 0: scalar deleting destructor 0x5f6890)
    std::uint8_t pixelFormat[0x20];              // +0x004 the DDPIXELFORMAT the files were opened for (GetPixelFormat 0x5fa730)
    msvc6::String name;                          // +0x024 probably the GFX folder or file name
    std::uint32_t fileSizes[kGroups];            // +0x034 size of each group's file (CFile::GetSize); not cleared by Close
    std::uint32_t mappings[kGroups];             // +0x14c Win32::CFileMapping* per group (new 0x2c, ctor 0x61a3a0)
    std::uint32_t views[kGroups];                // +0x264 the group's mapped file view (0: not loaded); every
                                                 //        table offset is relative to it
    const TextTable* textTables[kGroups];        // +0x37c texts (8 languages)
    const OffsetTable* animationTables[kGroups]; // +0x494
    const OffsetTable* guiImageTables[kGroups];  // +0x5ac GUI image tables (GUI groups only), read by GuiImagePointer
    const OffsetTable* landscapeTextureTables[kGroups];  // +0x6c4 landscape textures (group 00; LandscapeTexturePointer)
    const OffsetTable* colorIndexedTables[kGroups];  // +0x7dc image lists of 8-bit player-colour sprites
    const OffsetTable* shadowTables[kGroups];    // +0x8f4 image lists of shadow masks
    const PaletteTable* paletteTables[kGroups];  // +0xa0c palettes (group 01; PalettePointer)
    const SequenceTable* sequenceTables[kGroups];  // +0xb24
    std::uint32_t languageCount;                 // +0xc3c zeroed by the constructor and Close
    std::uint32_t language;                      // +0xc40 the current language (TextAddress uses it for -1)
};
static_assert(offsetof(CGraphicsData, name) == 0x024);
static_assert(offsetof(CGraphicsData, pixelFormat) == 0x004);
static_assert(offsetof(CGraphicsData, fileSizes) == 0x034);
static_assert(offsetof(CGraphicsData, mappings) == 0x14c);
static_assert(offsetof(CGraphicsData, views) == 0x264);
static_assert(offsetof(CGraphicsData, textTables) == 0x37c);
static_assert(offsetof(CGraphicsData, animationTables) == 0x494);
static_assert(offsetof(CGraphicsData, guiImageTables) == 0x5ac);
static_assert(offsetof(CGraphicsData, landscapeTextureTables) == 0x6c4);
static_assert(offsetof(CGraphicsData, colorIndexedTables) == 0x7dc);
static_assert(offsetof(CGraphicsData, shadowTables) == 0x8f4);
static_assert(offsetof(CGraphicsData, paletteTables) == 0xa0c);
static_assert(offsetof(CGraphicsData, sequenceTables) == 0xb24);
static_assert(offsetof(CGraphicsData, languageCount) == 0xc3c);
static_assert(offsetof(CGraphicsData, language) == 0xc40);
static_assert(sizeof(CGraphicsData) == 0xc44);

#pragma pack(pop)

}  // namespace s3re::game::gfx
