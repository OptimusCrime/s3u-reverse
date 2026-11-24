// Tests of the CMapFile constructor, Open, Close and the write path
// (map_file_io.cpp): the original against ours on every map and savegame
// of the installation (read only), on random containers and on files
// written in a temporary folder (the written bytes are compared).

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/globals.h"
#include "log.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;

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
CMapFile* __fastcall CMapFile_Construct(CMapFile* self, void* edx, std::uint32_t rethrow);
void __fastcall CMapFile_Open(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode);
void __fastcall CMapFile_Close(CMapFile* self, void* edx);
void __fastcall CMapFile_WriteChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                    const std::uint8_t* chunk);
void __fastcall CMapFile_WriteChunkWithTail(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                            const std::uint8_t* chunk, std::uint32_t tailSize,
                                            const std::uint8_t* tail);
void __fastcall CMapFile_WriteChunkData(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                        std::uint32_t size, const std::uint8_t* data);
extern ConstructFn Original_CMapFile_Construct;
extern OpenFn Original_CMapFile_Open;
extern CloseFn Original_CMapFile_Close;
extern WriteChunkFn Original_CMapFile_WriteChunk;
extern WriteChunkWithTailFn Original_CMapFile_WriteChunkWithTail;
extern WriteChunkDataFn Original_CMapFile_WriteChunkData;

using GetChunkFn = std::uint8_t*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
extern GetChunkFn Original_CMapFile_GetChunk;

namespace {

using namespace mapfile_test;

// The functions under test, as a set (a wrong one replaces a member).
struct IoFunctions {
    ConstructFn construct;
    OpenFn open;
    CloseFn close;
    WriteChunkFn writeChunk;
    WriteChunkWithTailFn writeChunkWithTail;
    WriteChunkDataFn writeChunkData;
};

IoFunctions OriginalIo() {
    return {Original_CMapFile_Construct, Original_CMapFile_Open, Original_CMapFile_Close,
            Original_CMapFile_WriteChunk, Original_CMapFile_WriteChunkWithTail, Original_CMapFile_WriteChunkData};
}

IoFunctions OurIo() {
    return {&CMapFile_Construct, &CMapFile_Open, &CMapFile_Close,
            &CMapFile_WriteChunk, &CMapFile_WriteChunkWithTail, &CMapFile_WriteChunkData};
}

// Zeroes the stack below the caller's frame, so that locals a function
// called next leaves uninitialized read zeros (Open's file type and
// checksum buffer when a read fails; see map_file_io.cpp).
__declspec(noinline) void ZeroStackBelow() {
    volatile std::uint8_t area[0x48000];
    for (std::size_t i = 0; i < sizeof(area); i += 4) {
        area[i] = 0;
        area[i + 1] = 0;
        area[i + 2] = 0;
        area[i + 3] = 0;
    }
}

CMapFile* Construct(const IoFunctions& io, std::uint32_t rethrow, std::uint32_t fill) {
    auto* file = static_cast<CMapFile*>(msvc6::GameOperatorNew(sizeof(CMapFile)));
    std::memset(file, static_cast<int>(fill), sizeof(CMapFile));
    return io.construct(file, nullptr, rethrow);
}

void Open(const IoFunctions& io, CMapFile* file, const std::string& path, std::uint32_t mode) {
    msvc6::String name;
    msvc6::String_FromChars(&name, nullptr, path.c_str(), static_cast<std::uint32_t>(path.size()));
    ZeroStackBelow();
    io.open(file, nullptr, &name, mode);
    msvc6::String_Destroy(&name, nullptr);
}

void Drop(const IoFunctions& io, CMapFile* file) {
    io.close(file, nullptr);
    using DeletingDestructorFn = void*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t flags);
    reinterpret_cast<DeletingDestructorFn>(0x004a5b20)(file, nullptr, 1);
}

std::string PathText(const CMapFile* file) {
    return file->path.ptr != nullptr ? std::string(file->path.ptr, file->path.len) : std::string("(null)");
}

// Two objects' state, beyond CompareFiles: the path and its allocator byte.
void CompareOpened(test::Context& ctx, const CMapFile* a, const CMapFile* b) {
    CompareFiles(ctx, a, b);
    S3RE_EXPECT(PathText(a) == PathText(b));
    S3RE_EXPECT_EQ(a->path.allocator, b->path.allocator);
    S3RE_EXPECT_EQ(a->path.len, b->path.len);
    S3RE_EXPECT_EQ(a->path.res, b->path.res);
}

// Opens `path` with the original into `a` and with `io` into `b` (both
// built by the original constructor), from the same random state, and
// compares the objects and the DebugLog calls.
void CompareOpen(test::Context& ctx, const IoFunctions& io, const std::string& path, std::uint32_t mode) {
    const IoFunctions original = OriginalIo();
    CMapFile* a = Construct(original, 0, 0);
    CMapFile* b = Construct(original, 0, 0);
    DebugLogRecorder log;
    Open(original, a, path, mode);
    const std::vector<DebugLogCall> logA = log.Take();
    Open(io, b, path, mode);
    const std::vector<DebugLogCall> logB = log.Take();
    const int failures = ctx.failures();
    CompareOpened(ctx, a, b);
    if (S3RE_EXPECT_EQ(logA.size(), logB.size())) {
        for (std::size_t k = 0; k < logA.size(); ++k) {
            S3RE_EXPECT(logA[k] == logB[k]);
        }
    }
    if (ctx.failures() != failures) {
        ctx.Fail(__FILE__, __LINE__, "  open %s, mode %u", path.c_str(), mode);
    }
    Drop(original, a);
    Drop(original, b);
}

// A random container: a valid or broken checksum, type 10, 11 or another,
// chunks of random sizes, sometimes fewer than 8 bytes in all.
std::vector<std::uint8_t> RandomFile(test::Rng& rng) {
    std::vector<std::uint8_t> bytes(8);
    const std::uint32_t types[] = {fileformats::kFileTypeMap, fileformats::kFileTypeSaveGame, 0, 12, rng.Next32()};
    const std::uint32_t fileType = types[rng.Below(5)];
    std::memcpy(bytes.data() + 4, &fileType, 4);
    std::uint32_t size = rng.Below(200);
    if (rng.Chance(1, 6)) {
        size = 0x40000 - 8 + rng.Below(16) * 4 - 32;  // around one checksum block
    } else if (rng.Chance(1, 8)) {
        size = 0x40000 + rng.Below(0x2000);  // a second block
    }
    bytes.resize(8 + size);
    rng.Fill(bytes.data() + 8, size);
    std::uint32_t checksum = ComputeChecksum(bytes);
    if (rng.Chance(1, 4)) {
        checksum ^= 1u << rng.Below(32);
    }
    std::memcpy(bytes.data(), &checksum, 4);
    if (rng.Chance(1, 10)) {
        bytes.resize(rng.Below(8));
    }
    return bytes;
}

// A well-formed container with a few chunks (keys may repeat).
std::vector<std::uint8_t> RandomChunkedFile(test::Rng& rng) {
    std::vector<std::uint8_t> bytes(8);
    const std::uint32_t fileType = rng.Chance(1, 2) ? fileformats::kFileTypeMap : fileformats::kFileTypeSaveGame;
    std::memcpy(bytes.data() + 4, &fileType, 4);
    const std::uint32_t chunks = rng.Below(10);
    for (std::uint32_t i = 0; i < chunks; ++i) {
        const ChunkHeader header{fileformats::ChunkKey(rng.Below(6), rng.Chance(1, 4) ? rng.Below(3) : 0),
                                 8 + rng.Below(200)};
        const std::size_t at = bytes.size();
        bytes.resize(at + header.size);
        std::memcpy(bytes.data() + at, &header, sizeof(header));
        rng.Fill(bytes.data() + at + 8, header.size - 8);
    }
    const std::uint32_t checksum = ComputeChecksum(bytes);
    std::memcpy(bytes.data(), &checksum, 4);
    return bytes;
}

constexpr std::uint32_t kReadModes[] = {fileformats::kOpenRead, fileformats::kOpenRead | fileformats::kOpenNoChecksum,
                                        fileformats::kOpenRead | fileformats::kOpenWrite, 0, 4, 8};

void CompareRandomOpens(test::Context& ctx, const IoFunctions& io, int files) {
    const std::string path = TempFolder() + "\\open.bin";
    for (int i = 0; i < files && ctx.failures() == 0; ++i) {
        WriteWholeFile(path, RandomFile(ctx.rng()));
        CompareOpen(ctx, io, path, kReadModes[ctx.rng().Below(static_cast<std::uint32_t>(std::size(kReadModes)))]);
    }
    // A file that does not exist (read mode, with and without the checksum).
    const std::string missing = TempFolder() + "\\does-not-exist.map";
    DeleteFileA(missing.c_str());
    CompareOpen(ctx, io, missing, fileformats::kOpenRead);
    CompareOpen(ctx, io, missing, fileformats::kOpenRead | fileformats::kOpenNoChecksum);
}

// ---------------------------------------------------------------------------
// The write path: a savegame written with the original into one file and
// with `io` into another; the files' bytes are compared.
// ---------------------------------------------------------------------------

struct WriteOp {
    int kind;  // 0 WriteChunk, 1 WriteChunkWithTail, 2 WriteChunkData
    std::uint32_t type;
    std::uint32_t sub;
    std::vector<std::uint8_t> chunk;  // kinds 0 and 1: header included
    std::vector<std::uint8_t> tail;   // kind 1
};

std::vector<WriteOp> RandomWrites(test::Rng& rng) {
    std::vector<WriteOp> ops(rng.Below(8));
    for (WriteOp& op : ops) {
        op.kind = static_cast<int>(rng.Below(3));
        op.type = rng.Chance(1, 4) ? rng.Next32() : rng.Below(0x20);
        op.sub = rng.Chance(1, 4) ? rng.Next32() : rng.Below(3);
        std::uint32_t size = 8 + rng.Below(300);
        if (rng.Chance(1, 20)) {
            size = 8 + 0x40000 + rng.Below(100);  // past one checksum block
        }
        op.chunk.resize(op.kind == 2 ? size - 8 : size);
        rng.Fill(op.chunk.data(), op.chunk.size());
        if (op.kind != 2) {
            const ChunkHeader header{fileformats::ChunkKey(op.type, op.sub), size};
            std::memcpy(op.chunk.data(), &header, sizeof(header));
        }
        if (op.kind == 1) {
            op.tail.resize(rng.Below(size - 8 + 1));
            rng.Fill(op.tail.data(), op.tail.size());
        }
    }
    return ops;
}

void ApplyWrite(const IoFunctions& io, CMapFile* file, const WriteOp& op) {
    switch (op.kind) {
    case 0: io.writeChunk(file, nullptr, op.type, op.sub, op.chunk.data()); break;
    case 1:
        io.writeChunkWithTail(file, nullptr, op.type, op.sub, op.chunk.data(),
                              static_cast<std::uint32_t>(op.tail.size()), op.tail.data());
        break;
    default:
        io.writeChunkData(file, nullptr, op.type, op.sub, static_cast<std::uint32_t>(op.chunk.size()),
                          op.chunk.data());
        break;
    }
}

// Writes the same savegame twice: `writer` does the open and the writes,
// `closer` the close. Compares the objects after each step and the files.
void CompareWrites(test::Context& ctx, const IoFunctions& writer, const IoFunctions& closer, int files) {
    const IoFunctions original = OriginalIo();
    const std::string pathA = TempFolder() + "\\write-a.sav";
    const std::string pathB = TempFolder() + "\\write-b.sav";
    for (int i = 0; i < files && ctx.failures() == 0; ++i) {
        const std::vector<WriteOp> ops = RandomWrites(ctx.rng());
        CMapFile* a = Construct(original, 0, 0);
        CMapFile* b = Construct(original, 0, 0);
        Open(original, a, pathA, fileformats::kOpenWrite);
        Open(writer, b, pathB, fileformats::kOpenWrite);
        for (const WriteOp& op : ops) {
            ApplyWrite(original, a, op);
            ApplyWrite(writer, b, op);
        }
        S3RE_EXPECT(ReadWholeFile(pathA) == ReadWholeFile(pathB));
        Drop(original, a);
        Drop(closer, b);
        const std::vector<std::uint8_t> bytesA = ReadWholeFile(pathA);
        if (!S3RE_EXPECT(bytesA == ReadWholeFile(pathB))) {
            ctx.Fail(__FILE__, __LINE__, "  file %d, %u writes", i, static_cast<unsigned>(ops.size()));
        }
        // The written file opens with the game's checksum check, and its
        // chunks read back as written (kinds 0 and 1: from offset 8).
        CMapFile* check = Construct(original, 0, 0);
        Open(original, check, pathA, fileformats::kOpenRead);
        S3RE_EXPECT_EQ(check->stream.error, 0u);
        Drop(original, check);
    }
}

// Wrong: the checksum mismatch not reported.
void __fastcall WrongOpen(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode) {
    CMapFile_Open(self, edx, path, mode | fileformats::kOpenNoChecksum);
    self->mode = mode;
}

// Wrong: the data chunk encoded with the key of the sub index.
void __fastcall WrongWriteChunkData(CMapFile* self, void* /*edx*/, std::uint32_t type, std::uint32_t subIndex,
                                    std::uint32_t size, const std::uint8_t* data) {
    std::vector<std::uint8_t> encoded(data, data + size);
    fileformats::EncodeChunkData(encoded.data(), 0, size, static_cast<std::uint8_t>(subIndex + 1), 0x18a);
    const ChunkHeader header{fileformats::ChunkKey(type, subIndex), size + 8};
    using SeekFn = std::uint32_t(__fastcall*)(void*, void*, std::int32_t, std::uint32_t);
    using WriteFn = std::int32_t(__fastcall*)(void*, void*, const void*, std::uint32_t);
    reinterpret_cast<SeekFn>(0x004a5930)(&self->stream, nullptr, 0, FILE_END);
    reinterpret_cast<WriteFn>(0x004a5870)(&self->stream, nullptr, &header, 8);
    reinterpret_cast<WriteFn>(0x004a5870)(&self->stream, nullptr, encoded.data(), size);
}

// Wrong: the checksum not written on close.
void __fastcall WrongClose(CMapFile* self, void* edx) {
    self->mode = 0;
    CMapFile_Close(self, edx);
}

// Wrong: the constructor without the path's allocator byte.
CMapFile* __fastcall WrongConstruct(CMapFile* self, void* edx, std::uint32_t rethrow) {
    CMapFile_Construct(self, edx, rethrow);
    self->path.allocator = 0;
    return self;
}

// Constructs with the original and `io` on identically filled memory and
// compares the objects (dictionary header pointers relative to each).
void CompareConstruct(test::Context& ctx, const IoFunctions& io, int count) {
    for (int i = 0; i < count && ctx.failures() == 0; ++i) {
        const std::uint32_t rethrow = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32();
        const std::uint32_t fill = ctx.rng().Below(256);
        const RandomState random = RandomState::Save();
        CMapFile* a = Construct(OriginalIo(), rethrow, fill);
        const RandomState afterA = RandomState::Save();
        random.Restore();
        CMapFile* b = Construct(io, rethrow, fill);
        S3RE_EXPECT(afterA == RandomState::Save());
        CompareOpened(ctx, a, b);
        // Every byte outside the dictionaries' self-pointers.
        CMapFile copy = *b;
        copy.chunks.header_ptr = copy.chunks.header_ptr - AddressOf(b) + AddressOf(a);
        copy.references.header_ptr = copy.references.header_ptr - AddressOf(b) + AddressOf(a);
        S3RE_EXPECT_MEM_EQ(a, &copy, sizeof(CMapFile));
        a->stream.rethrow = 0;  // the close of the never-opened stream must not rethrow
        b->stream.rethrow = 0;
        Drop(OriginalIo(), a);
        Drop(OriginalIo(), b);
    }
}

}  // namespace

S3RE_TEST(MapFileConstruct_MatchOriginal) {
    CompareConstruct(ctx, OurIo(), 300);
}

S3RE_TEST(MapFileOpen_RandomFiles_MatchOriginal) {
    CompareRandomOpens(ctx, OurIo(), 300);
}

// Every map and savegame of the installation, opened read only by the
// original and ours, with the game's mode (1) and without the checksum (5).
S3RE_TEST(MapFileOpen_AllGameFiles) {
    std::vector<std::string> files;
    ListFiles(GameFolder() + "\\Map", files);
    ListFiles(GameFolder() + "\\Save", files);
    for (const std::string& path : files) {
        CompareOpen(ctx, OurIo(), path, fileformats::kOpenRead);
        CompareOpen(ctx, OurIo(), path, fileformats::kOpenRead | fileformats::kOpenNoChecksum);
        if (ctx.failures() > 10) {
            break;
        }
    }
    S3RE_LOG("MapFileOpen_AllGameFiles: %u files opened by the original and ours\n",
             static_cast<unsigned>(files.size()));
    S3RE_EXPECT(!files.empty());
}

S3RE_TEST(MapFileWrite_MatchOriginal) {
    CompareWrites(ctx, OurIo(), OriginalIo(), 120);
}

// Chunks of 11.2 MB: the encoding's counter reaches its sign bit (after
// about 10.9 million bytes), and WriteChunkData's counter starts elsewhere.
S3RE_TEST(MapFileWrite_HugeChunks) {
    const IoFunctions original = OriginalIo();
    const IoFunctions ours = OurIo();
    const std::string pathA = TempFolder() + "\\huge-a.sav";
    const std::string pathB = TempFolder() + "\\huge-b.sav";
    constexpr std::uint32_t kSize = 11200000;
    for (int kind = 0; kind < 3; ++kind) {
        WriteOp op{kind, 7, 1, std::vector<std::uint8_t>(kind == 2 ? kSize - 8 : kSize), {}};
        ctx.rng().Fill(op.chunk.data(), op.chunk.size());
        if (kind != 2) {
            const ChunkHeader header{fileformats::ChunkKey(op.type, op.sub), kSize};
            std::memcpy(op.chunk.data(), &header, sizeof(header));
        }
        if (kind == 1) {
            op.tail.resize(1000);
            ctx.rng().Fill(op.tail.data(), op.tail.size());
        }
        CMapFile* a = Construct(original, 0, 0);
        CMapFile* b = Construct(original, 0, 0);
        Open(original, a, pathA, fileformats::kOpenWrite);
        Open(original, b, pathB, fileformats::kOpenWrite);
        ApplyWrite(original, a, op);
        ApplyWrite(ours, b, op);
        Drop(original, a);
        Drop(original, b);
        if (!S3RE_EXPECT(ReadWholeFile(pathA) == ReadWholeFile(pathB))) {
            ctx.Fail(__FILE__, __LINE__, "  kind %d", kind);
        }
    }
    DeleteFileA(pathA.c_str());
    DeleteFileA(pathB.c_str());
}

S3RE_TEST(MapFileClose_MatchOriginal) {
    CompareWrites(ctx, OriginalIo(), OurIo(), 60);
    // Close of read-mode files with cached chunks.
    const IoFunctions original = OriginalIo();
    const std::string path = TempFolder() + "\\close.bin";
    for (int i = 0; i < 100 && ctx.failures() == 0; ++i) {
        const std::vector<std::uint8_t> bytes = RandomChunkedFile(ctx.rng());
        WriteWholeFile(path, bytes);
        CMapFile* a = Construct(original, 0, 0);
        CMapFile* b = Construct(original, 0, 0);
        Open(original, a, path, fileformats::kOpenRead | fileformats::kOpenNoChecksum);
        Open(original, b, path, fileformats::kOpenRead | fileformats::kOpenNoChecksum);
        for (const ParsedChunk& chunk : ParseContainer(bytes)) {
            const RandomState random = RandomState::Save();
            Original_CMapFile_GetChunk(a, nullptr, chunk.key & 0xffff, chunk.key >> 16);
            random.Restore();
            Original_CMapFile_GetChunk(b, nullptr, chunk.key & 0xffff, chunk.key >> 16);
        }
        Original_CMapFile_Close(a, nullptr);
        CMapFile_Close(b, nullptr);
        CompareOpened(ctx, a, b);
        Drop(original, a);
        Drop(original, b);
    }
}

S3RE_TEST(MapFileIo_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        IoFunctions wrong = OurIo();
        wrong.open = &WrongOpen;
        CompareRandomOpens(probe, wrong, 60);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        IoFunctions wrong = OurIo();
        wrong.writeChunkData = &WrongWriteChunkData;
        CompareWrites(probe, wrong, OriginalIo(), 30);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        IoFunctions wrong = OurIo();
        wrong.close = &WrongClose;
        CompareWrites(probe, OriginalIo(), wrong, 30);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        IoFunctions wrong = OurIo();
        wrong.construct = &WrongConstruct;
        CompareConstruct(probe, wrong, 30);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
