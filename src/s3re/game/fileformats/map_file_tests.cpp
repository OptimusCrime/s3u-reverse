// Tests of the CMapFile container (map_file.cpp): the original against
// ours on random containers in a temporary folder, and on every map and
// savegame of the installation (read only: the files are opened by the
// original CMapFile::Open in read mode, GENERIC_READ and FILE_SHARE_READ).

#include "game/fileformats/map_file.h"

#include "game/globals.h"
#include "log.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::CMapFileStream;
using fileformats::SkipListDict;

bool __fastcall CMapFileStream_HasError(CMapFileStream* self, void* edx);
void __fastcall CMapFile_SetMemoryOnly(CMapFile* self, void* edx);
std::uint32_t __fastcall CMapFile_GetError(CMapFile* self, void* edx);
std::uint32_t __fastcall CMapFile_GetChecksum(CMapFile* self, void* edx);
std::uint8_t* __fastcall CMapFile_GetChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
std::uint32_t __fastcall CMapFile_ReleaseChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
std::uint32_t __fastcall CMapFile_AddChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                           const std::uint8_t* data);

using StreamHasErrorHookFn = bool(__fastcall*)(CMapFileStream* self, void* edx);
using MapFileVoidFn = void(__fastcall*)(CMapFile* self, void* edx);
using MapFileGetterFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx);
using GetChunkFn = std::uint8_t*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
using ReleaseChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type,
                                                  std::uint32_t subIndex);
using AddChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                              const std::uint8_t* data);
extern StreamHasErrorHookFn Original_CMapFileStream_HasError;
extern MapFileVoidFn Original_CMapFile_SetMemoryOnly;
extern MapFileGetterFn Original_CMapFile_GetError;
extern MapFileGetterFn Original_CMapFile_GetChecksum;
extern GetChunkFn Original_CMapFile_GetChunk;
extern ReleaseChunkFn Original_CMapFile_ReleaseChunk;
extern AddChunkFn Original_CMapFile_AddChunk;

namespace {

// ---------------------------------------------------------------------------
// Original CMapFile members the tests use to build and drop objects.
// ---------------------------------------------------------------------------

using ConstructFn = CMapFile*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t rethrow);
using OpenFn = void(__fastcall*)(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode);
using CloseFn = void(__fastcall*)(CMapFile* self, void* edx);
using DeletingDestructorFn = void*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t flags);
const auto kConstruct = reinterpret_cast<ConstructFn>(0x004a5a70);
const auto kOpen = reinterpret_cast<OpenFn>(0x004a5c80);
const auto kClose = reinterpret_cast<CloseFn>(0x004a5f40);
const auto kDeletingDestructor = reinterpret_cast<DeletingDestructorFn>(0x004a5b20);

// _getptd (0x62293a): the CRT's per-thread data; rand() keeps its state
// at +0x14.
using GetPtdFn = std::uint8_t*(__cdecl*)();
const auto kGetPtd = reinterpret_cast<GetPtdFn>(0x0062293a);
constexpr std::uint32_t kPtdRandState = 0x14;

CMapFile* NewMapFile() {
    auto* file = static_cast<CMapFile*>(msvc6::GameOperatorNew(sizeof(CMapFile)));
    std::memset(file, 0, sizeof(CMapFile));  // the dictionaries' value slots are never initialized
    return kConstruct(file, nullptr, 0);
}

CMapFile* OpenMapFile(const std::string& path, std::uint32_t mode) {
    CMapFile* file = NewMapFile();
    msvc6::String name;
    msvc6::String_FromChars(&name, nullptr, path.c_str(), static_cast<std::uint32_t>(path.size()));
    kOpen(file, nullptr, &name, mode);
    msvc6::String_Destroy(&name, nullptr);
    return file;
}

void DeleteMapFile(CMapFile* file) {
    kClose(file, nullptr);
    kDeletingDestructor(file, nullptr, 1);
}

// The random state skip-list inserts consume: two globals and the CRT's
// rand() state. Set before the original and before ours, compared after.
struct RandomState {
    std::uint32_t bits;
    std::int32_t bitsLeft;
    std::uint32_t crt;

    static RandomState Save() {
        std::uint32_t crt = 0;
        std::memcpy(&crt, kGetPtd() + kPtdRandState, 4);
        return {g_skipListRandomBits, g_skipListRandomBitsLeft, crt};
    }
    void Restore() const {
        g_skipListRandomBits = bits;
        g_skipListRandomBitsLeft = bitsLeft;
        std::memcpy(kGetPtd() + kPtdRandState, &crt, 4);
    }
    bool operator==(const RandomState& other) const {
        return bits == other.bits && bitsLeft == other.bitsLeft && crt == other.crt;
    }
};

std::uint32_t AddressOf(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

const std::uint8_t* PointerOf(std::uint32_t value) {
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value));
}

// One dictionary entry, as seen from level 0.
struct Entry {
    std::uint32_t key;
    std::uint32_t value;
    std::uint32_t valueAddress;
    std::uint32_t node;
};

std::vector<Entry> Entries(const SkipListDict& dict) {
    std::vector<Entry> entries;
    std::uint32_t node = PointerOf(dict.header_ptr) ? *reinterpret_cast<const std::uint32_t*>(PointerOf(dict.header_ptr))
                                                    : 0;
    while (node != 0 && entries.size() < 100000) {
        const auto* fields = reinterpret_cast<const std::uint32_t*>(PointerOf(node));
        entries.push_back({fields[-2], fields[-1], node - 4, node});
        node = fields[0];
    }
    return entries;
}

// The keys linked on `level` (node levels come from the random state).
std::vector<std::uint32_t> KeysOnLevel(const SkipListDict& dict, std::int32_t level) {
    std::vector<std::uint32_t> keys;
    std::uint32_t node = reinterpret_cast<const std::uint32_t*>(PointerOf(dict.header_ptr))[level];
    while (node != 0 && keys.size() < 100000) {
        const auto* fields = reinterpret_cast<const std::uint32_t*>(PointerOf(node));
        keys.push_back(fields[-2]);
        node = fields[level];
    }
    return keys;
}

// A chunk buffer's bytes (its size is in its own header).
std::vector<std::uint8_t> ChunkBytes(const std::uint8_t* buffer) {
    if (buffer == nullptr) {
        return {};
    }
    const std::uint32_t size = reinterpret_cast<const ChunkHeader*>(buffer)->size;
    return std::vector<std::uint8_t>(buffer, buffer + size);
}

// A return value that may point into the file's dictionaries, as something
// comparable between two objects: small values as they are, value
// addresses as (dictionary, key).
std::uint64_t DescribeResult(const CMapFile* file, std::uint32_t eax) {
    if (eax <= 1) {
        return eax;
    }
    for (const Entry& entry : Entries(file->references)) {
        if (entry.valueAddress == eax) {
            return 0x100000000ull | entry.key;
        }
    }
    for (const Entry& entry : Entries(file->chunks)) {
        if (entry.valueAddress == eax) {
            return 0x200000000ull | entry.key;
        }
    }
    return 0xdead00000000ull;  // a pointer into neither: shows up as a mismatch
}

std::uint32_t FilePosition(const CMapFile* file) {
    return SetFilePointer(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(file->stream.file.handle)), 0,
                          nullptr, FILE_CURRENT);
}

void CompareDict(test::Context& ctx, const SkipListDict& a, const SkipListDict& b, bool chunkValues) {
    S3RE_EXPECT_EQ(a.vtable, b.vtable);
    S3RE_EXPECT_EQ(a.allow_duplicates, b.allow_duplicates);
    S3RE_EXPECT_EQ(a.allocate, b.allocate);
    S3RE_EXPECT_EQ(a.deallocate, b.deallocate);
    S3RE_EXPECT_EQ(a.max_levels, b.max_levels);
    S3RE_EXPECT_EQ(a.probability_mask, b.probability_mask);
    S3RE_EXPECT_EQ(a.top_level, b.top_level);
    S3RE_EXPECT_EQ(a.header_ptr - AddressOf(&a), b.header_ptr - AddressOf(&b));
    S3RE_EXPECT_EQ(a.count, b.count);
    S3RE_EXPECT_EQ(a.key_slot, b.key_slot);
    S3RE_EXPECT_EQ(a.value_slot, b.value_slot);
    if (a.top_level != b.top_level || a.top_level < 0 || a.top_level >= 20) {
        return;
    }
    for (std::int32_t level = 0; level <= a.top_level; ++level) {
        S3RE_EXPECT(KeysOnLevel(a, level) == KeysOnLevel(b, level));
    }
    const std::vector<Entry> ea = Entries(a);
    const std::vector<Entry> eb = Entries(b);
    if (!S3RE_EXPECT_EQ(ea.size(), eb.size())) {
        return;
    }
    for (std::size_t i = 0; i < ea.size(); ++i) {
        S3RE_EXPECT_EQ(ea[i].key, eb[i].key);
        if (chunkValues) {
            S3RE_EXPECT(ChunkBytes(PointerOf(ea[i].value)) == ChunkBytes(PointerOf(eb[i].value)));
        } else {
            S3RE_EXPECT_EQ(ea[i].value, eb[i].value);
        }
    }
}

void CompareFiles(test::Context& ctx, const CMapFile* a, const CMapFile* b) {
    S3RE_EXPECT_EQ(a->vtable, b->vtable);
    S3RE_EXPECT_EQ(a->memory_only, b->memory_only);
    S3RE_EXPECT_EQ(a->open, b->open);
    S3RE_EXPECT_EQ(a->stream.rethrow, b->stream.rethrow);
    S3RE_EXPECT_EQ(a->stream.file.vtable, b->stream.file.vtable);
    S3RE_EXPECT_EQ(a->stream.file.owned, b->stream.file.owned);
    S3RE_EXPECT_EQ(a->stream.file.handle != 0, b->stream.file.handle != 0);
    S3RE_EXPECT_EQ(a->stream.error, b->stream.error);
    if (a->stream.file.handle != 0 && b->stream.file.handle != 0) {
        S3RE_EXPECT_EQ(FilePosition(a), FilePosition(b));
    }
    S3RE_EXPECT_EQ(a->mode, b->mode);
    S3RE_EXPECT_EQ(a->checksum, b->checksum);
    CompareDict(ctx, a->references, b->references, false);
    CompareDict(ctx, a->chunks, b->chunks, true);
}

// ---------------------------------------------------------------------------
// The functions under test, as a set (a wrong one replaces a member).
// ---------------------------------------------------------------------------

struct ChunkFunctions {
    GetChunkFn getChunk;
    ReleaseChunkFn releaseChunk;
    AddChunkFn addChunk;
    MapFileVoidFn setMemoryOnly;
};

ChunkFunctions OriginalChunkFunctions() {
    return {Original_CMapFile_GetChunk, Original_CMapFile_ReleaseChunk, Original_CMapFile_AddChunk,
            Original_CMapFile_SetMemoryOnly};
}

ChunkFunctions OurChunkFunctions() {
    return {&CMapFile_GetChunk, &CMapFile_ReleaseChunk, &CMapFile_AddChunk, &CMapFile_SetMemoryOnly};
}

// ---------------------------------------------------------------------------
// Random containers
// ---------------------------------------------------------------------------

std::string TempFolder() {
    char buffer[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buffer);
    std::string folder = std::string(buffer) + "s3re-mapfile-test";
    CreateDirectoryA(folder.c_str(), nullptr);
    return folder;
}

void WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(handle);
}

std::uint32_t RandomType(test::Rng& rng) {
    return rng.Below(5);
}

std::uint32_t RandomSub(test::Rng& rng) {
    return rng.Chance(1, 4) ? rng.Below(3) : 0;
}

// A container: checksum (correct or random), file type, chunks with keys
// from a small set (repeats included), sometimes a last chunk that is cut
// short, sometimes trailing bytes shorter than a header.
std::vector<std::uint8_t> RandomContainer(test::Rng& rng, bool& checksumValid) {
    std::vector<std::uint8_t> bytes(8);
    const std::uint32_t fileType = rng.Chance(1, 2) ? fileformats::kFileTypeMap : fileformats::kFileTypeSaveGame;
    std::memcpy(bytes.data() + 4, &fileType, 4);
    const std::uint32_t chunks = rng.Below(12);
    for (std::uint32_t i = 0; i < chunks; ++i) {
        ChunkHeader header{fileformats::ChunkKey(RandomType(rng), RandomSub(rng)), 8 + rng.Below(200)};
        const std::size_t at = bytes.size();
        bytes.resize(at + header.size);
        std::memcpy(bytes.data() + at, &header, sizeof(header));
        rng.Fill(bytes.data() + at + sizeof(header), header.size - sizeof(header));
    }
    if (rng.Chance(1, 4)) {
        ChunkHeader header{fileformats::ChunkKey(RandomType(rng), RandomSub(rng)), 16 + rng.Below(200)};
        const std::size_t at = bytes.size();
        bytes.resize(at + sizeof(header) + rng.Below(header.size - 8));
        std::memcpy(bytes.data() + at, &header, sizeof(header));
    } else if (rng.Chance(1, 4)) {
        bytes.resize(bytes.size() + 1 + rng.Below(7), 0x5a);
    }
    // The checksum covers whole dwords from offset 8.
    std::uint32_t checksum = 0;
    for (std::size_t at = 8; at + 4 <= bytes.size(); at += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, bytes.data() + at, 4);
        checksum = fileformats::ChecksumStep(checksum, word);
    }
    checksumValid = (bytes.size() - 8) % 4 == 0 && rng.Chance(3, 4);
    if (!checksumValid) {
        checksum = rng.Next32();
    }
    std::memcpy(bytes.data(), &checksum, 4);
    return bytes;
}

std::vector<std::uint8_t> RandomChunkData(test::Rng& rng) {
    ChunkHeader header{rng.Next32(), 8 + rng.Below(120)};
    std::vector<std::uint8_t> data(header.size);
    std::memcpy(data.data(), &header, sizeof(header));
    rng.Fill(data.data() + sizeof(header), header.size - sizeof(header));
    return data;
}

// Runs one random operation on `a` with the original and on `b` with
// `ours`, from the same random state, and compares the results.
void CompareOperation(test::Context& ctx, CMapFile* a, CMapFile* b, const ChunkFunctions& ours) {
    test::Rng& rng = ctx.rng();
    const ChunkFunctions original = OriginalChunkFunctions();
    // Arguments with junk in the upper half sometimes (only 16 bits count;
    // the decoding key is the type's low byte).
    std::uint32_t type = RandomType(rng);
    std::uint32_t sub = RandomSub(rng);
    if (rng.Chance(1, 8)) {
        type |= rng.Next32() & 0xffff0000u;
    }
    if (rng.Chance(1, 8)) {
        sub |= rng.Next32() & 0xffff0000u;
    }
    const RandomState before = RandomState::Save();
    std::uint64_t resultA = 0;
    std::uint64_t resultB = 0;
    const std::uint32_t op = rng.Below(20);
    if (op < 10) {
        const std::uint8_t* chunkA = original.getChunk(a, nullptr, type, sub);
        const RandomState afterA = RandomState::Save();
        before.Restore();
        const std::uint8_t* chunkB = ours.getChunk(b, nullptr, type, sub);
        S3RE_EXPECT(afterA == RandomState::Save());
        S3RE_EXPECT(ChunkBytes(chunkA) == ChunkBytes(chunkB));
        resultA = chunkA != nullptr;
        resultB = chunkB != nullptr;
    } else if (op < 17) {
        const std::uint32_t eaxA = original.releaseChunk(a, nullptr, type, sub);
        resultA = DescribeResult(a, eaxA);
        const RandomState afterA = RandomState::Save();
        before.Restore();
        const std::uint32_t eaxB = ours.releaseChunk(b, nullptr, type, sub);
        resultB = DescribeResult(b, eaxB);
        S3RE_EXPECT(afterA == RandomState::Save());
    } else if (op < 19) {
        const std::vector<std::uint8_t> data = RandomChunkData(rng);
        const std::uint32_t eaxA = original.addChunk(a, nullptr, type, sub, data.data());
        resultA = DescribeResult(a, eaxA);
        const RandomState afterA = RandomState::Save();
        before.Restore();
        const std::uint32_t eaxB = ours.addChunk(b, nullptr, type, sub, data.data());
        resultB = DescribeResult(b, eaxB);
        S3RE_EXPECT(afterA == RandomState::Save());
    } else {
        original.setMemoryOnly(a, nullptr);
        ours.setMemoryOnly(b, nullptr);
    }
    S3RE_EXPECT_EQ(resultA, resultB);
    CompareFiles(ctx, a, b);
}

void CompareChunkOperations(test::Context& ctx, const ChunkFunctions& ours, int files) {
    const std::string folder = TempFolder();
    for (int n = 0; n < files && ctx.failures() == 0; ++n) {
        bool checksumValid = false;
        const std::vector<std::uint8_t> bytes = RandomContainer(ctx.rng(), checksumValid);
        const std::string path = folder + "\\random.map";
        WriteWholeFile(path, bytes);
        const std::uint32_t mode = checksumValid && ctx.rng().Chance(1, 2)
                                       ? fileformats::kOpenRead
                                       : fileformats::kOpenRead | fileformats::kOpenNoChecksum;
        CMapFile* a = OpenMapFile(path, mode);
        CMapFile* b = OpenMapFile(path, mode);
        CompareFiles(ctx, a, b);
        const int operations = 1 + static_cast<int>(ctx.rng().Below(40));
        for (int i = 0; i < operations && ctx.failures() == 0; ++i) {
            CompareOperation(ctx, a, b, ours);
        }
        DeleteMapFile(a);
        DeleteMapFile(b);
        DeleteFileA(path.c_str());
    }
}

// Deliberately wrong: decodes with the subindex's low byte as the key.
std::uint8_t* __fastcall WrongGetChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex) {
    std::uint8_t* chunk = CMapFile_GetChunk(self, edx, type, subIndex);
    if (chunk != nullptr && reinterpret_cast<ChunkHeader*>(chunk)->size > 8 && (subIndex & 0xff) != (type & 0xff)) {
        chunk[8] ^= static_cast<std::uint8_t>((type ^ subIndex) & 0xff);
    }
    return chunk;
}

// Deliberately wrong: never frees, only counts down.
std::uint32_t __fastcall WrongReleaseChunk(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex) {
    const std::uint32_t key = fileformats::ChunkKey(type, subIndex);
    for (const Entry& entry : Entries(self->references)) {
        if (entry.key == key && entry.value > 1) {
            return CMapFile_ReleaseChunk(self, edx, type, subIndex);
        }
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Small members
// ---------------------------------------------------------------------------

S3RE_TEST(MapFileSmall_MatchOriginal) {
    for (int i = 0; i < 20000; ++i) {
        CMapFile a;
        ctx.rng().Fill(&a, sizeof(a));
        if (ctx.rng().Chance(1, 3)) {
            a.memory_only = 0;
        }
        if (ctx.rng().Chance(1, 3)) {
            a.stream.error = 0;
        }
        CMapFile b = a;
        S3RE_EXPECT_EQ(Original_CMapFileStream_HasError(&a.stream, nullptr),
                       CMapFileStream_HasError(&b.stream, nullptr));
        S3RE_EXPECT_EQ(Original_CMapFile_GetError(&a, nullptr), CMapFile_GetError(&b, nullptr));
        S3RE_EXPECT_EQ(Original_CMapFile_GetChecksum(&a, nullptr), CMapFile_GetChecksum(&b, nullptr));
        Original_CMapFile_SetMemoryOnly(&a, nullptr);
        CMapFile_SetMemoryOnly(&b, nullptr);
        S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (ctx.failures() != 0) {
            break;
        }
    }
}

S3RE_TEST(MapFileSmall_ComparisonDetectsWrongImplementation) {
    // GetError without the memory-only check.
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    for (int i = 0; i < 200; ++i) {
        CMapFile a;
        probe.rng().Fill(&a, sizeof(a));
        const std::uint32_t wrong = a.stream.error;
        probe.ExpectEq(Original_CMapFile_GetError(&a, nullptr), wrong, "original", "wrong", __FILE__, __LINE__);
    }
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// GetChunk, ReleaseChunk, AddChunk on random containers
// ---------------------------------------------------------------------------

S3RE_TEST(MapFileChunks_MatchOriginal) {
    CompareChunkOperations(ctx, OurChunkFunctions(), 400);
}

S3RE_TEST(MapFileChunks_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        ChunkFunctions wrong = OurChunkFunctions();
        if (k == 0) {
            wrong.getChunk = &WrongGetChunk;
        } else {
            wrong.releaseChunk = &WrongReleaseChunk;
        }
        CompareChunkOperations(probe, wrong, 60);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

// ---------------------------------------------------------------------------
// Every map and savegame of the installation
// ---------------------------------------------------------------------------

namespace {

std::string GameFolder() {
    char buffer[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
}

void ListFiles(const std::string& folder, std::vector<std::string>& files) {
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA((folder + "\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::string name = found.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        const std::string path = folder + "\\" + name;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            ListFiles(path, files);
        } else {
            files.push_back(path);
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

std::vector<std::uint8_t> ReadWholeFile(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return bytes;
    }
    bytes.resize(GetFileSize(handle, nullptr));
    DWORD read = 0;
    if (!bytes.empty()) {
        ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    }
    bytes.resize(read);
    CloseHandle(handle);
    return bytes;
}

// Our own reading of the container from the raw bytes: the chunks a
// GetChunk scan would find (first match per key), decoded.
struct ParsedChunk {
    std::uint32_t key;
    std::vector<std::uint8_t> bytes;  // empty: cut short by the end of the file
};

std::vector<ParsedChunk> ParseContainer(const std::vector<std::uint8_t>& file) {
    std::vector<ParsedChunk> chunks;
    std::size_t at = 8;
    while (at + sizeof(ChunkHeader) <= file.size()) {
        ChunkHeader header;
        std::memcpy(&header, file.data() + at, sizeof(header));
        bool seen = false;
        for (const ParsedChunk& chunk : chunks) {
            seen = seen || chunk.key == header.key;
        }
        if (!seen) {
            ParsedChunk chunk{header.key, {}};
            if (header.size <= file.size() - at) {
                chunk.bytes.assign(file.begin() + at, file.begin() + at + header.size);
                fileformats::DecodeChunkData(chunk.bytes.data(), header.size,
                                             static_cast<std::uint8_t>(header.key));
            }
            chunks.push_back(std::move(chunk));
        }
        if (header.size < sizeof(ChunkHeader)) {
            break;  // the game would loop here; no file of the game has one
        }
        at += header.size;
    }
    return chunks;
}

std::uint32_t StoredWord(const std::vector<std::uint8_t>& file, std::size_t at) {
    std::uint32_t word = 0;
    if (at + 4 <= file.size()) {
        std::memcpy(&word, file.data() + at, 4);
    }
    return word;
}

std::uint32_t ComputeChecksum(const std::vector<std::uint8_t>& file) {
    std::uint32_t checksum = 0;
    for (std::size_t at = 8; at + 4 <= file.size(); at += 4) {
        checksum = fileformats::ChecksumStep(checksum, StoredWord(file, at));
    }
    return checksum;
}

}  // namespace

S3RE_TEST(MapFile_AllGameFiles) {
    const std::string game = GameFolder();
    std::vector<std::string> files;
    ListFiles(game + "\\Map", files);
    const std::size_t mapFiles = files.size();
    ListFiles(game + "\\Save", files);
    std::uint32_t accepted = 0;
    std::uint32_t rejected = 0;
    std::uint32_t chunkCount = 0;
    std::uint32_t truncated = 0;
    std::uint64_t chunkBytes = 0;
    for (const std::string& path : files) {
        const std::vector<std::uint8_t> raw = ReadWholeFile(path);
        const std::uint32_t fileType = StoredWord(raw, 4);
        const bool shouldOpen = raw.size() >= 8 && (fileType == fileformats::kFileTypeMap ||
                                                    fileType == fileformats::kFileTypeSaveGame) &&
                                ComputeChecksum(raw) == StoredWord(raw, 0);
        // Read mode with the checksum check, as the game opens them.
        CMapFile* a = OpenMapFile(path, fileformats::kOpenRead);
        CMapFile* b = OpenMapFile(path, fileformats::kOpenRead);
        const bool opened = Original_CMapFile_GetError(a, nullptr) == 0;
        if (!S3RE_EXPECT_EQ(opened, shouldOpen)) {
            ctx.Fail(__FILE__, __LINE__, "open result differs from our parse: %s", path.c_str());
        }
        S3RE_EXPECT_EQ(CMapFile_GetChecksum(a, nullptr), StoredWord(raw, 0));
        if (!opened) {
            ++rejected;
        } else {
            ++accepted;
            const std::vector<ParsedChunk> parsed = ParseContainer(raw);
            // Every chunk twice (the second from the cache), then released twice.
            for (int pass = 0; pass < 2; ++pass) {
                for (const ParsedChunk& chunk : parsed) {
                    const std::uint32_t type = chunk.key & 0xffff;
                    const std::uint32_t sub = chunk.key >> 16;
                    const RandomState before = RandomState::Save();
                    const std::uint8_t* chunkA = Original_CMapFile_GetChunk(a, nullptr, type, sub);
                    const RandomState afterA = RandomState::Save();
                    before.Restore();
                    const std::uint8_t* chunkB = CMapFile_GetChunk(b, nullptr, type, sub);
                    S3RE_EXPECT(afterA == RandomState::Save());
                    const std::vector<std::uint8_t> bytesA = ChunkBytes(chunkA);
                    S3RE_EXPECT(bytesA == ChunkBytes(chunkB));
                    if (!S3RE_EXPECT(bytesA == chunk.bytes)) {
                        ctx.Fail(__FILE__, __LINE__, "chunk %08x of %s differs from our parse", chunk.key,
                                 path.c_str());
                    }
                    if (pass == 0) {
                        ++chunkCount;
                        chunkBytes += bytesA.size();
                        truncated += chunk.bytes.empty() ? 1 : 0;
                    }
                }
                CompareFiles(ctx, a, b);
            }
            for (int pass = 0; pass < 2; ++pass) {
                for (const ParsedChunk& chunk : parsed) {
                    const std::uint32_t eaxA = Original_CMapFile_ReleaseChunk(a, nullptr, chunk.key & 0xffff,
                                                                              chunk.key >> 16);
                    const std::uint64_t resultA = DescribeResult(a, eaxA);
                    const std::uint32_t eaxB = CMapFile_ReleaseChunk(b, nullptr, chunk.key & 0xffff, chunk.key >> 16);
                    S3RE_EXPECT_EQ(resultA, DescribeResult(b, eaxB));
                }
                CompareFiles(ctx, a, b);
            }
            // A key no file has (type 0xffff).
            S3RE_EXPECT(Original_CMapFile_GetChunk(a, nullptr, 0xffff, 0) == nullptr);
            S3RE_EXPECT(CMapFile_GetChunk(b, nullptr, 0xffff, 0) == nullptr);
            CompareFiles(ctx, a, b);
        }
        DeleteMapFile(a);
        DeleteMapFile(b);
        if (ctx.failures() > 20) {
            break;
        }
    }
    S3RE_LOG("MapFile_AllGameFiles: %u files (%u under Map, %u under Save): %u opened, %u rejected; "
                "%u chunks (%u cut short), %llu bytes\n",
                static_cast<unsigned>(files.size()), static_cast<unsigned>(mapFiles),
                static_cast<unsigned>(files.size() - mapFiles), accepted, rejected, chunkCount, truncated,
                static_cast<unsigned long long>(chunkBytes));
    S3RE_EXPECT(accepted > 0);
}

}  // namespace s3re::game
