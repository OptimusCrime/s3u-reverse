// Dirk::CMapFile: constructing, opening and closing a map or savegame
// file, and writing chunks (Dirk\SaveGames.cpp; format in
// re/file_formats.md, layouts in map_file.h). All thiscall (ECX = the
// CMapFile); the stream members (map_file_stream.cpp), the std::string
// assign and the Watcom skip-list code are called at their S3.EXE
// addresses.
//
// Dirk::CMapFile::CMapFile (0x004a5a70): (rethrow) ret 4 -> this. Clears
//   memory_only and open, constructs the stream (0x4a55e0) with `rethrow`,
//   an empty path whose allocator byte is the low byte of `rethrow` (the
//   argument's stack slot, docs/reimplementation_notes.md), the two
//   dictionaries (WCSkipNonTempBase 0x60b9f0 with 3, 8, 0, then their
//   vtables), checksum 0, the vtable.
// Dirk::CMapFile::~CMapFile (0x004a5b50): () ret 0. Sets the vtable,
//   closes the file if it is open (Close), destroys the reference and the
//   chunk dictionary (each: its WCSkipListBase vtable, the inlined call
//   0x59f200 when it still has entries, which is a bare `ret`, clear
//   0x60bd80, the WCExcept vtable), the path (the string destructor
//   inlined: erase(), then _Tidy(true)) and the stream's file
//   (Win32::CWin32Handle::~CWin32Handle 0x60beb0, the original).
// Dirk::CMapFile::Open (0x004a5c80): (const std::string* path, mode) ret 8.
//   path = *path (assign 0x401d60 with 0, npos; the original inlines it),
//   open = 1, mode = mode. Read mode (bit 0): opens the stream with the
//   mode, reads the stored checksum into +0x130 and the file type; a type
//   other than 10 or 11 logs "LOAD: Invalid filetype %i %i" (type, 11),
//   closes the stream (which clears the error) and sets the stream error
//   to E_INVALIDARG (the folded setter 0x4276a0), but Open goes on. Unless
//   bit 2 is set, it then checksums the file from offset 8 in reads of
//   0x40000 bytes (each read asks for 0x40000; the bytes it expected from
//   GetSize are checksummed), seeks back to 0, and on a mismatch closes
//   the stream and sets E_INVALIDARG. Write mode (bit 1, without bit 0):
//   opens the stream with mode 3 and writes the checksum field and the
//   type 11 (savegame) as placeholders.
// Dirk::CMapFile::Close (0x004a5f40): () ret 0. Frees every cached chunk
//   buffer (a walk of the chunk dictionary's level 0, the original through
//   an inlined WCValSkipListDictIter), clears both dictionaries (0x60bd80),
//   in mode 2 (exactly) recomputes the checksum over the file from offset
//   8 (as Open does) and writes it at offset 0, closes the stream and
//   clears memory_only, open and the checksum field.
// Dirk::CMapFile::WriteChunk (0x004a6540): (type, sub, chunk) ret 12.
//   Copies the chunk (size from its header, header included as the caller
//   filled it in), encodes bytes 8.. (EncodeChunkData, key = the type's
//   low byte), appends it at the end of the file, frees the copy.
// Dirk::CMapFile::WriteChunkWithTail (0x004a6600): (type, sub, chunk,
//   tailSize, tail) ret 20. Seeks to the end first, then builds the chunk
//   from the first size - tailSize bytes of `chunk` and `tailSize` bytes of
//   `tail` (size from the chunk's header), encodes and writes it.
// Dirk::CMapFile::WriteChunkData (0x004a66d0): (type, sub, size, data)
//   ret 16. Seeks to the end, writes the key and size + 8 (the header),
//   then a copy of the `size` data bytes encoded from the first byte with
//   the counter at 0x18a.
// None of the callers uses EAX (asm_tools eax-use), except the
// constructor's.
//
// Known, behaviour-irrelevant differences (docs/reimplementation_notes.md):
// when a read fails in Open (a file that could not be opened, or an I/O
// error during the checksum pass), the original uses whatever its 256 KB
// stack frame held: the file type then comes from an uninitialized local
// (ours: 0, so the invalid-type branch runs; the original runs it unless
// the leftover happens to be 10 or 11, and logs the leftover) and the
// checksum takes in stale buffer bytes (ours: zeros). Either way the file
// is reported as not opened (an error code is set); only which code can
// differ. The in-process tests zero the stack below their frame first, so
// that the original reads zeros too, and then compare everything.

#include "game/fileformats/map_file.h"

#include "game/debug_log.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <windows.h>

#include <cstring>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::CMapFileStream;
using fileformats::SkipListDict;

namespace fileformats {

void EncodeChunkData(std::uint8_t* data, std::uint32_t begin, std::uint32_t end, std::uint8_t key,
                     std::uint32_t counter) {
    // 0x4a659b..0x4a65c4: the inverse of DecodeChunkData.
    for (std::uint32_t i = begin; i < end; ++i) {
        const std::uint8_t plain = data[i];
        data[i] = static_cast<std::uint8_t>(plain ^ key);
        const std::uint8_t carry = (counter & 0x80000000u) != 0 ? 0xff : 0;
        key = static_cast<std::uint8_t>((key << 1 ^ plain) | carry);
        counter += 0xc5;
    }
}

}  // namespace fileformats

namespace {

// The members called at their S3.EXE addresses.
using StreamConstructFn = CMapFileStream*(__fastcall*)(CMapFileStream* self, void* edx, std::uint32_t rethrow);
using StreamOpenFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, const msvc6::String* path,
                                                std::uint32_t mode);
using StreamVoidFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx);
using StreamReadFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size);
using StreamWriteFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, const void* buffer,
                                                std::uint32_t size);
using StreamSeekFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, std::int32_t offset,
                                                std::uint32_t origin);
using StringAssignSubstrFn = msvc6::String*(__fastcall*)(msvc6::String* self, void* edx, const msvc6::String* x,
                                                         std::uint32_t pos, std::uint32_t n);
using DictConstructFn = SkipListDict*(__fastcall*)(SkipListDict* self, void* edx, std::uint32_t maxLevels,
                                                   std::uint32_t probability, std::uint32_t allowDuplicates);
using DictClearFn = void(__fastcall*)(SkipListDict* self, void* edx);
using HandleDestructFn = void(__fastcall*)(framework::CWin32Handle* self, void* edx);
using MapFileCloseFn = void(__fastcall*)(CMapFile* self, void* edx);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);

constexpr std::uintptr_t kStreamConstruct = 0x004a55e0;
constexpr std::uintptr_t kStreamOpen = 0x004a5600;
constexpr std::uintptr_t kStreamClose = 0x004a5710;
constexpr std::uintptr_t kStreamRead = 0x004a57b0;
constexpr std::uintptr_t kStreamWrite = 0x004a5870;
constexpr std::uintptr_t kStreamSeek = 0x004a5930;
constexpr std::uintptr_t kStreamGetSize = 0x004a59d0;
constexpr std::uintptr_t kStringAssignSubstr = 0x00401d60;
constexpr std::uintptr_t kDictConstruct = 0x0060b9f0;
constexpr std::uintptr_t kDictClear = 0x0060bd80;
constexpr std::uintptr_t kHandleDestruct = 0x0060beb0;  // Win32::CWin32Handle::~CWin32Handle
constexpr std::uintptr_t kClose = 0x004a5f40;           // Dirk::CMapFile::Close
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // never returns

constexpr std::int32_t kWriteChunkLine = 494;          // 0x1ee
constexpr std::int32_t kWriteChunkWithTailLine = 529;  // 0x211
constexpr std::int32_t kWriteChunkDataLine = 562;      // 0x232

constexpr std::uint32_t kInvalidArgument = 0x80070057;  // E_INVALIDARG
constexpr std::uint32_t kStreamModeReadWrite = 3;
constexpr std::uint32_t kChecksumStart = 8;  // after the checksum and the file type
constexpr std::uint32_t kChecksumBlock = 256 * 1024;  // 0x40000 bytes per read
constexpr std::uint32_t kDictMaxLevels = 3;
constexpr std::uint32_t kDictProbability = 8;
constexpr std::uint32_t kEncodeCounter = 0x628;      // as GetChunk's decoding
constexpr std::uint32_t kDataEncodeCounter = 0x18a;  // WriteChunkData: counted from the payload

std::int32_t StreamRead(CMapFileStream* stream, void* buffer, std::uint32_t size) {
    return reinterpret_cast<StreamReadFn>(kStreamRead)(stream, nullptr, buffer, size);
}

void StreamWrite(CMapFileStream* stream, const void* buffer, std::uint32_t size) {
    reinterpret_cast<StreamWriteFn>(kStreamWrite)(stream, nullptr, buffer, size);
}

void StreamSeek(CMapFileStream* stream, std::int32_t offset, std::uint32_t origin) {
    reinterpret_cast<StreamSeekFn>(kStreamSeek)(stream, nullptr, offset, origin);
}

void StreamClose(CMapFileStream* stream) {
    reinterpret_cast<StreamVoidFn>(kStreamClose)(stream, nullptr);
}

std::int32_t StreamGetSize(CMapFileStream* stream) {
    return static_cast<std::int32_t>(reinterpret_cast<StreamVoidFn>(kStreamGetSize)(stream, nullptr));
}

// The folded setter 0x4276a0 (`mov [ecx+10h], arg`) on the stream.
void SetStreamError(CMapFileStream* stream, std::uint32_t error) {
    stream->error = error;
}

// 0x4a5e4c..0x4a5ebe (Open) and 0x4a6003..0x4a6073 (Close): the checksum
// of the file from offset 8, read in blocks of 0x40000 bytes; seeks back
// to the start afterwards. `buffer` holds kChecksumBlock bytes.
std::uint32_t ChecksumFile(CMapFileStream* stream, std::uint32_t* buffer) {
    const std::int32_t size = StreamGetSize(stream);
    StreamSeek(stream, static_cast<std::int32_t>(kChecksumStart), FILE_BEGIN);
    std::uint32_t checksum = 0;
    for (std::int32_t position = kChecksumStart; position < size;) {
        std::uint32_t block = static_cast<std::uint32_t>(size - position);
        if (block >= kChecksumBlock) {
            block = kChecksumBlock;
        }
        StreamRead(stream, buffer, kChecksumBlock);  // always a whole block
        position += static_cast<std::int32_t>(block);
        for (std::uint32_t i = 0; i < block / 4; ++i) {
            checksum = fileformats::ChecksumStep(checksum, buffer[i]);
        }
    }
    StreamSeek(stream, 0, FILE_BEGIN);
    return checksum;
}

// A buffer for one chunk being written ("lpTemp").
std::uint8_t* NewTempBuffer(std::uint32_t size, std::int32_t line) {
    auto* buffer = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(size));
    if (buffer == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kTempName.c_str(),
                                                                texts::kSaveGamesFile.c_str(), line);
    }
    return buffer;
}

}  // namespace

// Purpose: sets up an empty map or savegame file object (the menus and the
// savegame code create one before opening a file).
CMapFile* __fastcall CMapFile_Construct(CMapFile* self, void* /*edx*/, std::uint32_t rethrow) {
    self->memory_only = 0;
    self->open = 0;
    reinterpret_cast<StreamConstructFn>(kStreamConstruct)(&self->stream, nullptr, rethrow);
    self->path.allocator = static_cast<std::uint8_t>(rethrow);  // the argument's stack slot
    self->path.ptr = nullptr;
    self->path.len = 0;
    self->path.res = 0;
    reinterpret_cast<DictConstructFn>(kDictConstruct)(&self->chunks, nullptr, kDictMaxLevels, kDictProbability, 0);
    self->chunks.vtable = vtables::kChunkDict;
    reinterpret_cast<DictConstructFn>(kDictConstruct)(&self->references, nullptr, kDictMaxLevels, kDictProbability,
                                                      0);
    self->references.vtable = vtables::kReferenceDict;
    self->checksum = 0;
    self->vtable = vtables::kCMapFile;
    return self;
}

namespace {

// The destruction of one dictionary (0x4a5b8d..0x4a5bb7, 0x4a5bbd..0x4a5be4).
void DestroyDict(SkipListDict* dict, std::uint32_t baseVtable) {
    dict->vtable = baseVtable;
    // With entries left the original calls 0x59f200, a bare `ret` (an
    // inlined member that compiled to nothing); there is nothing to do.
    reinterpret_cast<DictClearFn>(kDictClear)(dict, nullptr);
    dict->vtable = vtables::kWCExcept;
}

}  // namespace

// Purpose: destroys a map or savegame file object (closing the file if it
// is still open); every CMapFile ends here.
void __fastcall CMapFile_Destruct(CMapFile* self, void* /*edx*/) {
    self->vtable = vtables::kCMapFile;
    if (self->open != 0) {
        // Through its S3.EXE address (our Close, hooked there).
        reinterpret_cast<MapFileCloseFn>(kClose)(self, nullptr);
    }
    DestroyDict(&self->references, vtables::kReferenceSkipListBase);
    DestroyDict(&self->chunks, vtables::kChunkSkipListBase);
    msvc6::String_Destroy(&self->path, nullptr);
    reinterpret_cast<HandleDestructFn>(kHandleDestruct)(&self->stream.file, nullptr);
}

// Purpose: opens a map or savegame for reading (checking its file type and
// checksum) or creates a savegame for writing.
void __fastcall CMapFile_Open(CMapFile* self, void* /*edx*/, const msvc6::String* path, std::uint32_t mode) {
    reinterpret_cast<StringAssignSubstrFn>(kStringAssignSubstr)(&self->path, nullptr, path, 0, msvc6::detail::Npos());
    self->open = 1;
    self->mode = mode;
    CMapFileStream* stream = &self->stream;
    if ((mode & fileformats::kOpenRead) != 0) {
        reinterpret_cast<StreamOpenFn>(kStreamOpen)(stream, nullptr, path, mode);
        StreamRead(stream, &self->checksum, 4);
        std::uint32_t fileType = 0;  // the original's is uninitialized (see the file header)
        StreamRead(stream, &fileType, 4);
        if (fileType != fileformats::kFileTypeMap && fileType != fileformats::kFileTypeSaveGame) {
            GameDebugLog(texts::kInvalidFileType, fileType, fileformats::kFileTypeSaveGame);
            StreamClose(stream);
            SetStreamError(stream, kInvalidArgument);
        }
        if ((mode & fileformats::kOpenNoChecksum) != 0) {
            return;
        }
        static_assert(kChecksumBlock % 4 == 0);
        std::uint32_t buffer[kChecksumBlock / 4] = {};  // the original's holds stack leftovers
        if (ChecksumFile(stream, buffer) != self->checksum) {
            StreamClose(stream);
            SetStreamError(stream, kInvalidArgument);
        }
        return;
    }
    if ((mode & fileformats::kOpenWrite) != 0) {
        reinterpret_cast<StreamOpenFn>(kStreamOpen)(stream, nullptr, path, kStreamModeReadWrite);
        const std::uint32_t fileType = fileformats::kFileTypeSaveGame;
        StreamWrite(stream, &self->checksum, 4);
        StreamWrite(stream, &fileType, 4);
    }
}

// Purpose: closes a map or savegame file: drops the cached chunks and, for
// a savegame being written, stores its checksum.
void __fastcall CMapFile_Close(CMapFile* self, void* /*edx*/) {
    // 0x4a5f94..0x4a5fc9: free every cached chunk buffer (node value).
    const auto* node = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(self->chunks.header_ptr));
    for (std::uint32_t next = node[0]; next != 0;) {
        const auto* fields = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(next));
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(fields[-1])));
        next = fields[0];
    }
    reinterpret_cast<DictClearFn>(kDictClear)(&self->chunks, nullptr);
    reinterpret_cast<DictClearFn>(kDictClear)(&self->references, nullptr);
    if (self->mode == fileformats::kOpenWrite) {
        std::uint32_t buffer[kChecksumBlock / 4] = {};
        const std::uint32_t checksum = ChecksumFile(&self->stream, buffer);
        StreamWrite(&self->stream, &checksum, 4);
    }
    StreamClose(&self->stream);
    self->memory_only = 0;
    self->checksum = 0;
    self->open = 0;
}

// Purpose: appends a chunk the caller built (header included) to a
// savegame being written.
void __fastcall CMapFile_WriteChunk(CMapFile* self, void* /*edx*/, std::uint32_t type, std::uint32_t /*subIndex*/,
                                    const std::uint8_t* chunk) {
    const std::uint32_t size = reinterpret_cast<const ChunkHeader*>(chunk)->size;
    std::uint8_t* buffer = NewTempBuffer(size, kWriteChunkLine);
    std::memcpy(buffer, chunk, size);
    fileformats::EncodeChunkData(buffer, sizeof(ChunkHeader), size, static_cast<std::uint8_t>(type), kEncodeCounter);
    StreamSeek(&self->stream, 0, FILE_END);
    StreamWrite(&self->stream, buffer, size);
    msvc6::GameOperatorDelete(buffer);
}

// Purpose: appends a chunk made of a header part and a separate tail
// (probably for chunks whose last part lives elsewhere) to a savegame.
void __fastcall CMapFile_WriteChunkWithTail(CMapFile* self, void* /*edx*/, std::uint32_t type,
                                            std::uint32_t /*subIndex*/, const std::uint8_t* chunk,
                                            std::uint32_t tailSize, const std::uint8_t* tail) {
    const std::uint32_t size = reinterpret_cast<const ChunkHeader*>(chunk)->size;
    StreamSeek(&self->stream, 0, FILE_END);
    std::uint8_t* buffer = NewTempBuffer(size, kWriteChunkWithTailLine);
    const std::uint32_t head = size - tailSize;
    std::memcpy(buffer, chunk, head);
    std::memcpy(buffer + head, tail, tailSize);
    fileformats::EncodeChunkData(buffer, sizeof(ChunkHeader), size, static_cast<std::uint8_t>(type), kEncodeCounter);
    StreamWrite(&self->stream, buffer, size);
    msvc6::GameOperatorDelete(buffer);
}

// Purpose: appends raw data as a chunk to a savegame (the tile rows and
// the computer players' state are saved this way).
void __fastcall CMapFile_WriteChunkData(CMapFile* self, void* /*edx*/, std::uint32_t type, std::uint32_t subIndex,
                                        std::uint32_t size, const std::uint8_t* data) {
    const ChunkHeader header{fileformats::ChunkKey(type, subIndex), size + sizeof(ChunkHeader)};
    StreamSeek(&self->stream, 0, FILE_END);
    StreamWrite(&self->stream, &header.key, 4);
    StreamWrite(&self->stream, &header.size, 4);
    std::uint8_t* buffer = NewTempBuffer(size, kWriteChunkDataLine);
    std::memcpy(buffer, data, size);
    fileformats::EncodeChunkData(buffer, 0, size, static_cast<std::uint8_t>(type), kDataEncodeCounter);
    StreamWrite(&self->stream, buffer, size);
    msvc6::GameOperatorDelete(buffer);
}

using ConstructFn = CMapFile*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t rethrow);
using OpenFn = void(__fastcall*)(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode);
using CloseFn = void(__fastcall*)(CMapFile* self, void* edx);
using WriteChunkFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                       const std::uint8_t* chunk);
using WriteChunkWithTailFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                               const std::uint8_t* chunk, std::uint32_t tailSize,
                                               const std::uint8_t* tail);
using WriteChunkDataFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                           std::uint32_t size, const std::uint8_t* data);

S3RE_HOOK(0x004a5a70, CMapFile_Construct, ConstructFn, Original_CMapFile_Construct, CMapFile_Construct);
S3RE_HOOK(0x004a5c80, CMapFile_Open, OpenFn, Original_CMapFile_Open, CMapFile_Open);
S3RE_HOOK(0x004a5b50, CMapFile_Destruct, CloseFn, Original_CMapFile_Destruct, CMapFile_Destruct);
S3RE_HOOK(0x004a5f40, CMapFile_Close, CloseFn, Original_CMapFile_Close, CMapFile_Close);
S3RE_HOOK(0x004a6540, CMapFile_WriteChunk, WriteChunkFn, Original_CMapFile_WriteChunk, CMapFile_WriteChunk);
S3RE_HOOK(0x004a6600, CMapFile_WriteChunkWithTail, WriteChunkWithTailFn, Original_CMapFile_WriteChunkWithTail,
          CMapFile_WriteChunkWithTail);
S3RE_HOOK(0x004a66d0, CMapFile_WriteChunkData, WriteChunkDataFn, Original_CMapFile_WriteChunkData,
          CMapFile_WriteChunkData);

}  // namespace s3re::game
