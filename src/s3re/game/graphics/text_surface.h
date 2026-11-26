#pragma once

// Dirk::CTextSurface (Dirk\Text.cpp, text_surface.cpp): the 1-bit GDI
// surface the panel texts are drawn into, and its functions.

#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

struct TextRect {
    std::int32_t left, top, right, bottom;
};
static_assert(sizeof(TextRect) == 16);

struct CTextSurface {
    void* const* vtable;            // +0x00 (0x6587f0)
    std::uint8_t fonts[0x78];       // +0x04 WCValSkipListDict<int, HFONT>: font id -> HFONT
    std::int32_t font_key;          // +0x7c the key/value pair looked up in `fonts`: the font id
    std::uint32_t font_value;       // +0x80 ... and its HFONT (insert's default)
    std::uint32_t bitmap;           // +0x84 HBITMAP of the DIB section (0x4a78f0 creates it)
    std::uint8_t* bits;             // +0x88 its pixels, 1 bit per pixel, rows of `pitch` bytes
    std::uint32_t pitch;            // +0x8c bytes per row
    std::uint8_t busy;              // +0x90 set while drawing or locked
};
#pragma pack(pop)
static_assert(offsetof(CTextSurface, fonts) == 0x04);
static_assert(offsetof(CTextSurface, font_key) == 0x7c);
static_assert(offsetof(CTextSurface, bitmap) == 0x84);
static_assert(offsetof(CTextSurface, bits) == 0x88);
static_assert(offsetof(CTextSurface, pitch) == 0x8c);
static_assert(offsetof(CTextSurface, busy) == 0x90);


std::uint32_t __fastcall CTextSurface_Lock(void* self, void* edx);
void __fastcall CTextSurface_Unlock(void* self, void* edx);
TextRect* __fastcall CTextSurface_DrawText(void* self, void* edx, TextRect* result, std::int32_t font,
                                           const TextRect* rect, std::uint32_t flags, const msvc6::String* text);
TextRect* __cdecl CTextSurface_DrawTextF(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                         std::uint32_t flags, const char* format, ...);

}  // namespace s3re::game
