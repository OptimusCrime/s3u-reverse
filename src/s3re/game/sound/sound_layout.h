#pragma once

// CSoundData (Dirk\SoundData.cpp): the game's sound effects. It is the
// DirectSound object (it derives from DirectX::CDirectSound) and maps the
// two SND files (SND\Siedler3_00.dat, siedler3_01.dat) into memory; the
// effects are created from the mapped samples on first use. File format:
// re/sound_formats.md; code: sound_data.cpp.

#include "game/fileformats/map_file.h"
#include "game/framework/framework_layout.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::sound {

#pragma pack(push, 1)

inline constexpr std::size_t kFiles = 2;  // SND groups 00 and 01

// The header of an SND file (0x1c bytes, checked in both files).
struct SndFileHeader {
    std::uint32_t magic;       // +0x00 0x00011544
    std::uint32_t unk04;       // +0x04 2
    std::uint32_t unk08;       // +0x08 0
    std::uint32_t headerSize;  // +0x0c 0x1c
    std::uint32_t fileSize;    // +0x10
    std::int32_t unk14;        // +0x14 -1 in file 00, 1 in file 01 (CSoundData +0x148)
    std::uint32_t table;       // +0x18 file offset of the sound table (0x1c)
};
static_assert(sizeof(SndFileHeader) == 0x1c);

// The sound table: like the GFX tables (re/graphics_formats.md), a dword
// magic, a word size in bytes, a word count, then `count` file offsets of
// the sounds.
struct SndTable {
    std::uint32_t magic;     // +0x00 0x00011f74
    std::uint16_t size;      // +0x04 8 + 4 * count
    std::uint16_t count;     // +0x06
    std::uint32_t sounds[1];  // +0x08 file offsets of the sounds (count of them)
};
static_assert(offsetof(SndTable, sounds) == 8);

// A sound: its variants (GetEffect picks one, or a random one).
struct SndSound {
    std::uint32_t count;        // +0x00
    std::uint32_t variants[1];  // +0x04 file offsets of the samples
};

// A sample: its size (this field included), a WAVEFORMATEX, the PCM data.
struct SndSample {
    std::uint32_t size;          // +0x00 bytes from here to the end of the data
    std::uint8_t format[0x12];   // +0x04 WAVEFORMATEX (cbSize 0)
    std::uint8_t data[1];        // +0x16 size - 0x16 bytes
};
static_assert(offsetof(SndSample, data) == 0x16);

// CSoundData (operator new 0x14c bytes; vtable 0x65d3ec: 0 scalar deleting
// destructor, 1 CDirectSound's restore-if-active 0x5c1dc0, 2 ReleaseBuffers
// 0x5fde20, 3 Restore 0x5fdef0).
struct CSoundData {
    framework::CDirectSound base;          // +0x000 vtable, IDirectSound*
    std::uint32_t format;                  // +0x008 Open's 4th argument: primary format 1, 2 or 4
    std::uint8_t enabled;                  // +0x00c set when the primary buffer plays (IsEnabled 0x5ff4d0)
    std::uint8_t pad0d[3];                 // +0x00d
    msvc6::String folder;                  // +0x010 the SND folder (with a trailing '\')
    std::uint32_t fileSizes[kFiles];       // +0x020
    std::uint32_t mappings[kFiles];        // +0x028 Win32::CFileMapping*
    std::uint32_t views[kFiles];           // +0x030 the mapped files
    std::uint32_t tables[kFiles];          // +0x038 view + header.table
    fileformats::SkipListDict effects;     // +0x040 key -> DirectX::CSoundEffect* (key: id << 24 | variant)
    fileformats::SkipListDict lastUse;     // +0x0c0 key -> timeGetTime of its last GetEffect
    void* primary;                         // +0x140 DirectX::CPrimaryBuffer*
    std::uint32_t createdTime;             // +0x144 timeGetTime at construction
    std::int32_t unk148;                   // +0x148 header +0x14 of the first file where it isn't -1 (getter 0x5ff4e0)
};
static_assert(offsetof(CSoundData, format) == 0x08);
static_assert(offsetof(CSoundData, enabled) == 0x0c);
static_assert(offsetof(CSoundData, folder) == 0x10);
static_assert(offsetof(CSoundData, fileSizes) == 0x20);
static_assert(offsetof(CSoundData, mappings) == 0x28);
static_assert(offsetof(CSoundData, views) == 0x30);
static_assert(offsetof(CSoundData, tables) == 0x38);
static_assert(offsetof(CSoundData, effects) == 0x40);
static_assert(offsetof(CSoundData, lastUse) == 0xc0);
static_assert(offsetof(CSoundData, primary) == 0x140);
static_assert(offsetof(CSoundData, unk148) == 0x148);
static_assert(sizeof(CSoundData) == 0x14c);

#pragma pack(pop)

}  // namespace s3re::game::sound
