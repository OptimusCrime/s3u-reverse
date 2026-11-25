#pragma once

// Test helpers of the graphics drawing tests (gfx_rle.cpp, gfx_draw.cpp):
// synthetic RLE sprites and every sprite image of the game's own GFX files
// (GFX\siedler3_NN.f8007e01f.dat, the RGB565 set, mapped read-only; the
// game folder is S3.EXE's).

#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace s3re::game::gfx_test {

// A synthetic RLE sprite: rows of runs within width w.
inline std::vector<std::uint8_t> MakeRle(test::Rng& rng, std::uint32_t width, std::uint32_t height, std::uint32_t pixelBytes) {
    std::vector<std::uint8_t> data;
    for (std::uint32_t row = 0; row < height; ++row) {
        std::uint32_t x = 0;
        do {
            const std::uint32_t skip = rng.Below(width - x + 1) & 0x7f;
            x += skip;
            const std::uint32_t count = x < width ? rng.Below(width - x + 1) : 0;
            x += count;
            const bool last = x >= width || rng.Chance(1, 4);
            data.push_back(static_cast<std::uint8_t>(count));
            data.push_back(static_cast<std::uint8_t>(skip | (last ? 0x80 : 0)));
            for (std::uint32_t i = 0; i < count * pixelBytes; ++i) {
                data.push_back(static_cast<std::uint8_t>(rng.Next32()));
            }
            if (last) {
                break;
            }
        } while (true);
    }
    return data;
}

// --- the GFX files -----------------------------------------------------------

struct MappedFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const std::uint8_t* view = nullptr;
    std::uint32_t size = 0;
};

// Every image of the three sprite-list tables (header +0x40, +0x44, +0x48)
// and of the GUI image table (+0x3c, offsets straight to images).
struct GfxSprites {
    std::vector<MappedFile> files;
    std::vector<std::uint32_t> images[4];  // 16-bit, colour-indexed, shadow, GUI
};

inline std::string GfxDirectory() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    return dir + "GFX\\";
}

inline constexpr std::uint32_t kGfxMagic = 0x00041304;  // raw-ok: the file header magic, not an offset
inline constexpr std::uint32_t kSpriteHeaderSize = 0x0c;

inline void CollectList(const MappedFile& file, std::uint32_t tableOffset, std::vector<std::uint32_t>& out) {
    if (tableOffset == 0 || tableOffset + 8 > file.size) {
        return;
    }
    const std::uint8_t* table = file.view + tableOffset;
    const std::uint16_t count = *reinterpret_cast<const std::uint16_t*>(table + 6);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t listOffset = reinterpret_cast<const std::uint32_t*>(table + 8)[i];
        if (listOffset == 0 || listOffset + 8 > file.size) {
            continue;
        }
        const std::uint8_t* list = file.view + listOffset;
        for (std::uint32_t k = 0; k < list[7]; ++k) {
            const std::uint32_t image = listOffset + reinterpret_cast<const std::uint32_t*>(list + 8)[k];
            if (image + kSpriteHeaderSize < file.size &&
                *reinterpret_cast<const std::uint32_t*>(file.view + image) == kSpriteHeaderSize) {
                out.push_back(reinterpret_cast<std::uint32_t>(file.view + image));
            }
        }
    }
}

inline void CollectGui(const MappedFile& file, std::uint32_t tableOffset, std::vector<std::uint32_t>& out) {
    if (tableOffset == 0 || tableOffset + 8 > file.size) {
        return;
    }
    const std::uint8_t* table = file.view + tableOffset;
    const std::uint16_t count = *reinterpret_cast<const std::uint16_t*>(table + 6);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t image = reinterpret_cast<const std::uint32_t*>(table + 8)[i];
        if (image != 0 && image + 9 < file.size) {
            out.push_back(reinterpret_cast<std::uint32_t>(file.view + image));
        }
    }
}

inline const GfxSprites& LoadGfxSprites() {
    static GfxSprites sprites;
    static bool loaded = false;
    if (loaded) {
        return sprites;
    }
    loaded = true;
    const std::string dir = GfxDirectory();
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA((dir + "siedler3_??.f8007e01f.dat").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return sprites;
    }
    do {
        MappedFile file;
        file.file = CreateFileA((dir + found.cFileName).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (file.file == INVALID_HANDLE_VALUE) {
            continue;
        }
        file.size = GetFileSize(file.file, nullptr);
        file.mapping = CreateFileMappingA(file.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        file.view = file.mapping ? static_cast<const std::uint8_t*>(MapViewOfFile(file.mapping, FILE_MAP_READ, 0, 0, 0))
                                 : nullptr;
        if (file.view == nullptr || file.size < 0x54 ||
            *reinterpret_cast<const std::uint32_t*>(file.view) != kGfxMagic) {
            continue;  // e.g. siedler3_18, which is an executable
        }
        const auto* header = reinterpret_cast<const std::uint32_t*>(file.view);
        CollectList(file, header[0x40 / 4], sprites.images[0]);
        CollectList(file, header[0x44 / 4], sprites.images[1]);
        CollectGui(file, header[0x3c / 4], sprites.images[3]);
        CollectList(file, header[0x48 / 4], sprites.images[2]);
        sprites.files.push_back(file);
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return sprites;
}


}  // namespace s3re::game::gfx_test
