// Dirk::CMapFile, the chunk container of maps and savegames
// (Dirk\SaveGames.cpp; format in re/file_formats.md, layouts in map_file.h).
// All functions are thiscall (ECX = the CMapFile, or its stream at +8).
//
// CMapFileStream::HasError (0x004a5a60): () -> bool: error (+0x10) != 0.
// Dirk::CMapFile::SetMemoryOnly (0x004a60c0): () memory_only (+4) = 1.
// Dirk::CMapFile::GetError (0x004a60d0): () -> 0 for a memory-only file,
//   else the stream's error code (a tail jump to the folded getter
//   0x49f4d0 with ECX = the stream).
// Dirk::CMapFile::GetChecksum (0x004a6a30): () -> checksum (+0x130).
// Dirk::CMapFile::GetChunk (0x004a60f0): (type, subIndex) ret 8 -> the
//   decoded chunk (header included) or 0. A cached chunk gets its
//   reference count raised. Otherwise (unless memory-only) the chunks are
//   scanned from offset 8: read the 8-byte header, seek back over it, and
//   either read the whole chunk (operator new; "lpChunk" out of memory) or
//   seek past it; until an error, a short header read or the key. A short
//   chunk read frees the buffer and returns 0. A read chunk is decoded
//   (DecodeChunkData) and cached with reference count 1.
// Dirk::CMapFile::ReleaseChunk (0x004a6310): (type, subIndex) ret 8: lowers
//   the reference count; at 0 frees the buffer and removes the key from
//   both dictionaries. EAX: 0 if the key isn't cached, the address of the
//   count while it is above 0, else the last remove's result.
// Dirk::CMapFile::AddChunk (0x004a63f0): (type, subIndex, data) ret 12:
//   copies the chunk (size = data's header size, header included) into a
//   new buffer ("newchunk" out of memory). A cached key gets the new
//   buffer (the old one is freed) and its count raised; a new key is
//   inserted with count 1 (WCValSkipListDict<uint,int>::insert 0x4a6aa0 and
//   <uint,void*>::insert 0x4a6ba0). EAX: the address of the buffer slot, or
//   the second insert's result.
//
// The chunk cache is two Watcom skip-list dictionaries (map_file.h,
// SkipListDict); their code (find 0x60bab0, insert 0x60bbe0, remove
// 0x60be20) is called at its S3.EXE address, exactly as the original's
// inlined find-or-insert does it (key into the dictionary's key slot,
// find, else insert, value at node - 4). Inserting draws the node's level
// from the CRT rand() (WCSkipNonTempBase::base_random_level 0x60bca0).

#include "game/fileformats/map_file.h"

#include "game/debug_log.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <windows.h>

#include <cstring>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::CMapFileStream;
using fileformats::SkipListDict;

namespace fileformats {

void DecodeChunkData(std::uint8_t* data, std::uint32_t size, std::uint8_t key) {
    // 0x4a6263..0x4a629b. The counter's sign bit (reached after about 10.9
    // million bytes) is ORed into the key as 0xff.
    std::uint32_t counter = 0x628;
    for (std::uint32_t i = sizeof(ChunkHeader); i < size; ++i) {
        const std::uint8_t plain = static_cast<std::uint8_t>(data[i] ^ key);
        data[i] = plain;
        const std::uint8_t carry = (counter & 0x80000000u) != 0 ? 0xff : 0;
        key = static_cast<std::uint8_t>((key << 1 ^ plain) | carry);
        counter += 0xc5;
    }
}

}  // namespace fileformats

namespace {

// The CMapFileStream members GetChunk calls (at their S3.EXE addresses).
using StreamReadFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size);
using StreamSeekFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, std::int32_t offset,
                                                std::uint32_t origin);
using StreamHasErrorFn = bool(__fastcall*)(CMapFileStream* self, void* edx);
constexpr std::uintptr_t kStreamRead = 0x004a57b0;
constexpr std::uintptr_t kStreamSeek = 0x004a5930;
constexpr std::uintptr_t kStreamHasError = 0x004a5a60;

std::int32_t StreamRead(CMapFileStream* stream, void* buffer, std::uint32_t size) {
    return reinterpret_cast<StreamReadFn>(kStreamRead)(stream, nullptr, buffer, size);
}

void StreamSeek(CMapFileStream* stream, std::int32_t offset, std::uint32_t origin) {
    reinterpret_cast<StreamSeekFn>(kStreamSeek)(stream, nullptr, offset, origin);
}

bool StreamHasError(CMapFileStream* stream) {
    return reinterpret_cast<StreamHasErrorFn>(kStreamHasError)(stream, nullptr);
}

// The skip-list dictionary code (WCSkipNonTempBase).
using DictFindFn = std::uint8_t*(__fastcall*)(SkipListDict* self, void* edx, const std::uint32_t* key);
using DictRemoveFn = std::uint32_t(__fastcall*)(SkipListDict* self, void* edx, const std::uint32_t* key);
using DictInsertPairFn = std::uint32_t(__fastcall*)(SkipListDict* self, void* edx, const std::uint32_t* key,
                                                    const std::uint32_t* value);
constexpr std::uintptr_t kDictFind = 0x0060bab0;
constexpr std::uintptr_t kDictInsert = 0x0060bbe0;  // insert(&key/value pair) -> node or 0
constexpr std::uintptr_t kDictRemove = 0x0060be20;
constexpr std::uintptr_t kReferenceDictInsert = 0x004a6aa0;  // WCValSkipListDict<uint,int>::insert(&key, &value)
constexpr std::uintptr_t kChunkDictInsert = 0x004a6ba0;      // WCValSkipListDict<uint,void*>::insert(&key, &value)

// The node's value sits right before its forward pointers.
constexpr std::uint32_t kNodeValueOffset = 4;

// Puts the key into the dictionary's key slot and finds its node.
std::uint8_t* FindNode(SkipListDict& dict, std::uint32_t key) {
    dict.key_slot = key;
    return reinterpret_cast<DictFindFn>(kDictFind)(&dict, nullptr, &dict.key_slot);
}

// The original's inlined `dict[key]`: find, else insert the key slot's
// pair; returns the address of the node's value. Insert fails only when
// operator new does (the original then uses a null pointer).
std::uint32_t* ValueOf(SkipListDict& dict, std::uint32_t key) {
    std::uint8_t* node = FindNode(dict, key);
    if (node == nullptr) {
        node = reinterpret_cast<DictFindFn>(kDictInsert)(&dict, nullptr, &dict.key_slot);
        if (node == nullptr) {
            return nullptr;
        }
    }
    return reinterpret_cast<std::uint32_t*>(node - kNodeValueOffset);
}

std::uint32_t RemoveKey(SkipListDict& dict, std::uint32_t key) {
    dict.key_slot = key;
    return reinterpret_cast<DictRemoveFn>(kDictRemove)(&dict, nullptr, &dict.key_slot);
}

std::uint32_t BufferValue(const void* buffer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
}

std::uint8_t* BufferOf(std::uint32_t value) {
    return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(value));
}

using ThrowOutOfMemoryFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // never returns
constexpr std::int32_t kGetChunkLine = 395;  // 0x18b
constexpr std::int32_t kAddChunkLine = 465;  // 0x1d1

// 0x4a619d..0x4a6251: scans the file's chunks for `key` and reads the
// matching chunk (header included); null if it isn't there or the read
// came up short.
std::uint8_t* ReadChunkFromFile(CMapFileStream* stream, std::uint32_t key) {
    constexpr std::int32_t kFirstChunk = 8;  // after the checksum and the file type
    StreamSeek(stream, kFirstChunk, FILE_BEGIN);
    std::uint8_t* buffer = nullptr;
    ChunkHeader header;
    std::int32_t headerBytes = 0;
    do {
        headerBytes = StreamRead(stream, &header.key, 4);
        headerBytes += StreamRead(stream, &header.size, 4);
        StreamSeek(stream, -headerBytes, FILE_CURRENT);
        if (headerBytes != sizeof(ChunkHeader)) {
            continue;  // the loop condition ends the scan
        }
        if (header.key != key) {
            StreamSeek(stream, static_cast<std::int32_t>(header.size), FILE_CURRENT);
            continue;
        }
        buffer = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(header.size));
        if (buffer == nullptr) {
            reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kChunkName.c_str(),
                                                                    texts::kSaveGamesFile.c_str(), kGetChunkLine);
        }
        if (static_cast<std::uint32_t>(StreamRead(stream, buffer, header.size)) != header.size) {
            msvc6::GameOperatorDelete(buffer);
            buffer = nullptr;
        }
    } while (!StreamHasError(stream) && headerBytes == sizeof(ChunkHeader) && header.key != key);
    if (buffer != nullptr) {
        fileformats::DecodeChunkData(buffer, header.size, static_cast<std::uint8_t>(key));
    }
    return buffer;
}

}  // namespace

// Purpose: tells whether a read, write or seek of a map or savegame file
// failed (the chunk scan stops then).
bool __fastcall CMapFileStream_HasError(CMapFileStream* self, void* /*edx*/) {
    return self->error != 0;
}

// Purpose: marks a map file as living in memory only (the random map
// dialog builds its chunks with AddChunk), so chunk lookups never read a
// file.
void __fastcall CMapFile_SetMemoryOnly(CMapFile* self, void* /*edx*/) {
    self->memory_only = 1;
}

// Purpose: the error code of the last file operation (0 when it worked);
// the menus and the loaders check it after opening a map or savegame.
std::uint32_t __fastcall CMapFile_GetError(CMapFile* self, void* /*edx*/) {
    if (self->memory_only != 0) {
        return 0;
    }
    return self->stream.error;
}

// Purpose: the checksum stored in the file, probably compared between the
// players of a network game to make sure everyone has the same map.
std::uint32_t __fastcall CMapFile_GetChecksum(CMapFile* self, void* /*edx*/) {
    return self->checksum;
}

// Purpose: gives the loaders one chunk of a map or savegame (terrain,
// settlers, buildings, ...), decoded, from the cache or from the file.
std::uint8_t* __fastcall CMapFile_GetChunk(CMapFile* self, void* /*edx*/, std::uint32_t type,
                                           std::uint32_t subIndex) {
    const std::uint32_t key = fileformats::ChunkKey(type, subIndex);
    if (FindNode(self->references, key) != nullptr) {
        // 0x4a6132: cached: one more reference.
        std::uint32_t* count = ValueOf(self->references, key);
        ++*count;
        return BufferOf(*ValueOf(self->chunks, key));
    }
    if (self->memory_only != 0) {
        return nullptr;
    }
    // The decoding key is the low byte of the type argument (0x4a625f).
    std::uint8_t* buffer = ReadChunkFromFile(&self->stream, key);
    if (buffer == nullptr) {
        return nullptr;
    }
    *ValueOf(self->chunks, key) = BufferValue(buffer);
    *ValueOf(self->references, key) = 1;
    return buffer;
}

// Purpose: the loaders hand a chunk back when they are done with it; the
// last release frees it.
std::uint32_t __fastcall CMapFile_ReleaseChunk(CMapFile* self, void* /*edx*/, std::uint32_t type,
                                               std::uint32_t subIndex) {
    const std::uint32_t key = fileformats::ChunkKey(type, subIndex);
    if (FindNode(self->references, key) == nullptr) {
        return 0;
    }
    std::uint32_t* count = ValueOf(self->references, key);
    --*count;
    count = ValueOf(self->references, key);
    if (*count != 0) {
        return BufferValue(count);
    }
    msvc6::GameOperatorDelete(BufferOf(*ValueOf(self->chunks, key)));
    RemoveKey(self->chunks, key);
    return RemoveKey(self->references, key);
}

// Purpose: puts a chunk that exists only in memory into the cache (the
// random map dialog adds its parameters this way).
std::uint32_t __fastcall CMapFile_AddChunk(CMapFile* self, void* /*edx*/, std::uint32_t type,
                                           std::uint32_t subIndex, const std::uint8_t* data) {
    const std::uint32_t size = reinterpret_cast<const ChunkHeader*>(data)->size;
    std::uint32_t key = fileformats::ChunkKey(type, subIndex);
    auto* buffer = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(size));
    if (buffer == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kNewChunkName.c_str(),
                                                                texts::kSaveGamesFile.c_str(), kAddChunkLine);
    }
    std::memcpy(buffer, data, size);
    if (FindNode(self->references, key) == nullptr) {
        // 0x4a6509: a new key, with one reference.
        const std::uint32_t one = 1;
        reinterpret_cast<DictInsertPairFn>(kReferenceDictInsert)(&self->references, nullptr, &key, &one);
        const std::uint32_t value = BufferValue(buffer);
        return reinterpret_cast<DictInsertPairFn>(kChunkDictInsert)(&self->chunks, nullptr, &key, &value);
    }
    // 0x4a6472: replace the cached buffer and add a reference.
    msvc6::GameOperatorDelete(BufferOf(*ValueOf(self->chunks, key)));
    ++*ValueOf(self->references, key);
    std::uint32_t* slot = ValueOf(self->chunks, key);
    *slot = BufferValue(buffer);
    return BufferValue(slot);
}

using StreamHasErrorHookFn = bool(__fastcall*)(CMapFileStream* self, void* edx);
using MapFileVoidFn = void(__fastcall*)(CMapFile* self, void* edx);
using MapFileGetterFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx);
using GetChunkFn = std::uint8_t*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
using ReleaseChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type,
                                                  std::uint32_t subIndex);
using AddChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                              const std::uint8_t* data);

S3RE_HOOK(0x004a5a60, CMapFileStream_HasError, StreamHasErrorHookFn, Original_CMapFileStream_HasError,
          CMapFileStream_HasError);
S3RE_HOOK(0x004a60c0, CMapFile_SetMemoryOnly, MapFileVoidFn, Original_CMapFile_SetMemoryOnly, CMapFile_SetMemoryOnly);
S3RE_HOOK(0x004a60d0, CMapFile_GetError, MapFileGetterFn, Original_CMapFile_GetError, CMapFile_GetError);
S3RE_HOOK(0x004a6a30, CMapFile_GetChecksum, MapFileGetterFn, Original_CMapFile_GetChecksum, CMapFile_GetChecksum);
S3RE_HOOK(0x004a60f0, CMapFile_GetChunk, GetChunkFn, Original_CMapFile_GetChunk, CMapFile_GetChunk);
S3RE_HOOK(0x004a6310, CMapFile_ReleaseChunk, ReleaseChunkFn, Original_CMapFile_ReleaseChunk, CMapFile_ReleaseChunk);
S3RE_HOOK(0x004a63f0, CMapFile_AddChunk, AddChunkFn, Original_CMapFile_AddChunk, CMapFile_AddChunk);

}  // namespace s3re::game
