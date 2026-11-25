#pragma once

// Layouts of the DirectSound effect classes (DirectX\Dsound.cpp; names from
// RTTI): the sound file wrappers DirectX::CSoundFile / CMemorySoundFile, the
// effect DirectX::CSoundEffect with its dictionary of duplicate buffers,
// and DirectX::CDirectSoundException. Code: directx_sound_files.cpp,
// directx_sound_effect.cpp; the buffer classes: framework_layout.h.

#include "game/fileformats/map_file.h"
#include "game/framework/framework_layout.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::framework {

#pragma pack(push, 1)

// WAVEFORMATEX as the game copies it (0x12 bytes, cbSize included).
struct WaveFormat {
    std::uint16_t formatTag;       // +0x00 1 = PCM
    std::uint16_t channels;        // +0x02
    std::uint32_t samplesPerSec;   // +0x04
    std::uint32_t avgBytesPerSec;  // +0x08
    std::uint16_t blockAlign;      // +0x0c
    std::uint16_t bitsPerSample;   // +0x0e
    std::uint16_t extraSize;       // +0x10 cbSize
};
static_assert(sizeof(WaveFormat) == 0x12);

// DirectX::CSoundFile (vtable 0x65cf08: 0 scalar deleting destructor,
// 1..4 pure: Open, Close, GetFormat, Read): an abstract sound source with
// a name.
struct CSoundFile {
    std::uint32_t vtable;  // +0x00
    msvc6::String name;    // +0x04
};
static_assert(sizeof(CSoundFile) == 0x14);

// DirectX::CMemorySoundFile (0x38 bytes, vtable 0x65cf20: 0 scalar deleting
// destructor 0x5c2780, 1 Open and 2 Close 0x5c27b0 (rewind), 3 GetFormat
// 0x5c27c0, 4 Read 0x5c2800): a sample in memory (the mapped SND file).
struct CMemorySoundFile {
    CSoundFile base;          // +0x00 name "memory mapped file"
    WaveFormat format;        // +0x14 as converted by the constructor
    std::uint8_t pad26[2];    // +0x26
    const std::uint8_t* data;  // +0x28 the PCM data
    std::uint32_t bytes;      // +0x2c the size Read delivers in total
    std::uint32_t position;   // +0x30 bytes of `data` already read
    std::uint32_t mode;       // +0x34 1 copy, 2 16 -> 8 bit, 4 16 -> 8 bit and half the rate
};
static_assert(offsetof(CMemorySoundFile, format) == 0x14);
static_assert(offsetof(CMemorySoundFile, data) == 0x28);
static_assert(offsetof(CMemorySoundFile, mode) == 0x34);
static_assert(sizeof(CMemorySoundFile) == 0x38);

// WCValSkipListDict<int, DirectX::CSoundEffectBuffer> (vtable 0x65cf54):
// the skip-list base (SkipListDict without its last two fields) and a
// key/value pair (key, CSoundEffectBuffer) that the find/insert callers use
// as the lookup argument. Nodes: [key][CSoundEffectBuffer][forward
// pointers], pointed at their forward array (key at node - 0x14, value at
// node - 0x10).
struct EffectBufferDict {
    std::uint8_t base[0x78];       // +0x00 the WCSkipNonTempBase fields (fileformats::SkipListDict)
    std::int32_t keySlot;          // +0x78
    CSoundEffectBuffer valueSlot;  // +0x7c
};
static_assert(sizeof(EffectBufferDict) == 0x8c);
inline constexpr std::uint32_t kDictCount = 0x74;  // offset of the node count in the base

// DirectX::CSoundEffect (0xc0 bytes, vtable 0x65cf4c: 0 Unload 0x5c2ce0):
// one sample in a secondary buffer, plus duplicates of it for playing it
// several times at once.
struct CSoundEffect {
    std::uint32_t vtable;         // +0x00
    EffectBufferDict duplicates;  // +0x04 key -> duplicate buffer (keys from g_soundDuplicateCounter)
    CSoundEffectBuffer master;    // +0x90 the buffer the sample is loaded into
    void* file;                   // +0xa0 DirectX::CSoundFile* (owned; not set by the constructor)
    WaveFormat format;            // +0xa4 from the file
    std::uint8_t padb6[2];        // +0xb6
    std::uint32_t bytes;          // +0xb8 the sample's size in the buffer
    std::uint32_t lastPurge;      // +0xbc timeGetTime of the last duplicate purge
};
static_assert(offsetof(CSoundEffect, duplicates) == 0x04);
static_assert(offsetof(CSoundEffect, master) == 0x90);
static_assert(offsetof(CSoundEffect, file) == 0xa0);
static_assert(offsetof(CSoundEffect, format) == 0xa4);
static_assert(offsetof(CSoundEffect, bytes) == 0xb8);
static_assert(offsetof(CSoundEffect, lastPurge) == 0xbc);
static_assert(sizeof(CSoundEffect) == 0xc0);

// DirectX::CDirectSoundException (0x20 bytes, vtable 0x65cea4: 0 scalar
// deleting destructor, 1 Describe; virtual base Win32::CRunTimeError via
// the vbtable 0x65ceac).
struct CDirectSoundException {
    CException base;      // +0x00
    std::int32_t result;  // +0x18 the HRESULT
    const char* where;    // +0x1c the call's name ("CDirectSoundBuffer::DSPlay", ...)
};
static_assert(offsetof(CDirectSoundException, result) == 0x18);
static_assert(sizeof(CDirectSoundException) == 0x20);

// A Watcom skip-list iterator (WCValSkipListDictIter, 0x14 bytes).
struct SkipListIter {
    std::uint32_t vtable;
    std::uint32_t unk04;
    void* dict;
    std::uint32_t node;   // +0x0c the current node (its forward array)
    std::uint32_t atEnd;  // +0x10
};
static_assert(sizeof(SkipListIter) == 0x14);

#pragma pack(pop)

}  // namespace s3re::game::framework
