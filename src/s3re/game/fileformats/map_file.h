#pragma once

// Dirk::CMapFile (Dirk\SaveGames.cpp): the container of maps (.map, file
// type 10) and savegames (.sav/.mps, type 11). Format and call trees:
// re/file_formats.md.
//
//   +0 checksum   over the dwords from offset 8 to the end
//   +4 file type
//   +8 chunks:    [key = subIndex << 16 | type][size incl. this header][data]
//                 the data XORed with a rolling key (DecodeChunkData)
//
// The layouts below mirror the original objects (packed, offsets asserted).

#include "game/framework/framework_layout.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::fileformats {

#pragma pack(push, 1)

// A WCValSkipListDict<unsigned int, V> (Watcom container library, the code
// of WCSkipNonTempBase at 0x60b9f0..0x60be60). Nodes are allocated as
// [key][value][forward pointers...] and pointed at their forward array, so
// a node's key is at node - 8 and its value at node - 4. The dictionary
// object ends with a key/value pair that the inlined find/insert code of
// the callers uses as the lookup argument.
struct SkipListDict {
    std::uint32_t vtable;            // +0x00
    std::uint32_t unk04;             // +0x04
    std::uint32_t header[20];        // +0x08 forward pointers of the header
    std::uint32_t allow_duplicates;  // +0x58
    std::uint32_t allocate;          // +0x5c allocator function or 0 (operator new)
    std::uint32_t deallocate;        // +0x60
    std::uint32_t max_levels;        // +0x64 1..20
    std::uint32_t probability_mask;  // +0x68 1 or 3
    std::int32_t top_level;          // +0x6c
    std::uint32_t header_ptr;        // +0x70 points at header
    std::uint32_t count;             // +0x74
    std::uint32_t key_slot;          // +0x78 the key/value pair passed to find and insert
    std::uint32_t value_slot;        // +0x7c
};
static_assert(offsetof(SkipListDict, header) == 0x08);
static_assert(offsetof(SkipListDict, allow_duplicates) == 0x58);
static_assert(offsetof(SkipListDict, top_level) == 0x6c);
static_assert(offsetof(SkipListDict, header_ptr) == 0x70);
static_assert(offsetof(SkipListDict, key_slot) == 0x78);
static_assert(offsetof(SkipListDict, value_slot) == 0x7c);
static_assert(sizeof(SkipListDict) == 0x80);

// The file stream of CMapFile (CMapFile + 8; code 0x4a55e0..0x4a5a70).
struct CMapFileStream {
    std::uint8_t rethrow;          // +0x00 constructor argument: rethrow caught CHResult errors
    std::uint8_t pad01[3];         // +0x01
    framework::CWin32Handle file;  // +0x04 Win32::CFile (vtable 0x65d9b8)
    std::uint32_t error;           // +0x10 0, or the error code of the caught exception
};
static_assert(offsetof(CMapFileStream, file) == 0x04);
static_assert(offsetof(CMapFileStream, error) == 0x10);
static_assert(sizeof(CMapFileStream) == 0x14);

// Dirk::CMapFile (operator new 0x134 bytes; vtable 0x658770).
struct CMapFile {
    std::uint32_t vtable;        // +0x000
    std::uint8_t memory_only;    // +0x004 GetChunk never reads the file (SetMemoryOnly)
    std::uint8_t open;           // +0x005 set by Open, cleared by Close
    std::uint8_t pad06[2];       // +0x006
    CMapFileStream stream;       // +0x008
    msvc6::String path;          // +0x01c
    std::uint32_t mode;          // +0x02c kOpenRead / kOpenWrite / kOpenNoChecksum bits
    SkipListDict chunks;         // +0x030 key -> chunk buffer (header included)
    SkipListDict references;     // +0x0b0 key -> reference count
    std::uint32_t checksum;      // +0x130 stored checksum (read mode) or the one written
};
static_assert(offsetof(CMapFile, memory_only) == 0x04);
static_assert(offsetof(CMapFile, stream) == 0x08);
static_assert(offsetof(CMapFile, path) == 0x1c);
static_assert(offsetof(CMapFile, mode) == 0x2c);
static_assert(offsetof(CMapFile, chunks) == 0x30);
static_assert(offsetof(CMapFile, references) == 0xb0);
static_assert(offsetof(CMapFile, checksum) == 0x130);
static_assert(sizeof(CMapFile) == 0x134);

// The 8-byte header in front of every chunk.
struct ChunkHeader {
    std::uint32_t key;   // subIndex << 16 | type
    std::uint32_t size;  // whole chunk, header included
};
static_assert(sizeof(ChunkHeader) == 8);

#pragma pack(pop)

// Open modes (CMapFile::Open, CMapFileStream::Open).
inline constexpr std::uint32_t kOpenRead = 1;
inline constexpr std::uint32_t kOpenWrite = 2;
inline constexpr std::uint32_t kOpenNoChecksum = 4;

// File types (CMapFile::Open).
inline constexpr std::uint32_t kFileTypeMap = 10;
inline constexpr std::uint32_t kFileTypeSaveGame = 11;

inline constexpr std::uint32_t ChunkKey(std::uint32_t type, std::uint32_t subIndex) {
    return (subIndex & 0xffff) << 16 | (type & 0xffff);
}

// The rolling XOR of the chunk data (GetChunk 0x4a6263..0x4a629b): `key`
// starts at the chunk type's low byte and takes each decoded byte in.
void DecodeChunkData(std::uint8_t* data, std::uint32_t size, std::uint8_t key);

// The file checksum step (Open 0x4a5e96..0x4a5ead): rotate left by one,
// with the XOR applied before the carried-in bit is ORed in.
inline constexpr std::uint32_t ChecksumStep(std::uint32_t checksum, std::uint32_t word) {
    return ((checksum << 1) ^ word) | (checksum >> 31);
}

}  // namespace s3re::game::fileformats
