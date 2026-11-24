#pragma once

// Helpers of the CMapFile tests (map_file_tests.cpp, load_save_game_tests.cpp,
// map_file_open_tests.cpp): building and dropping CMapFile objects with the
// original members, the random state skip-list inserts consume, comparing
// two objects (dictionaries, cached chunks, stream state), and reading the
// game's own files (read only).

#include "game/fileformats/map_file.h"
#include "game/globals.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace s3re::game::mapfile_test {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::CMapFileStream;
using fileformats::SkipListDict;

// ---------------------------------------------------------------------------
// Original CMapFile members the tests use to build and drop objects.
// ---------------------------------------------------------------------------

using ConstructFn = CMapFile*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t rethrow);
using OpenFn = void(__fastcall*)(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode);
using CloseFn = void(__fastcall*)(CMapFile* self, void* edx);
using DeletingDestructorFn = void*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t flags);
inline const auto kConstruct = reinterpret_cast<ConstructFn>(0x004a5a70);
inline const auto kOpen = reinterpret_cast<OpenFn>(0x004a5c80);
inline const auto kClose = reinterpret_cast<CloseFn>(0x004a5f40);
inline const auto kDeletingDestructor = reinterpret_cast<DeletingDestructorFn>(0x004a5b20);

// _getptd (0x62293a): the CRT's per-thread data; rand() keeps its state
// at +0x14.
using GetPtdFn = std::uint8_t*(__cdecl*)();
inline const auto kGetPtd = reinterpret_cast<GetPtdFn>(0x0062293a);
inline constexpr std::uint32_t kPtdRandState = 0x14;

inline CMapFile* NewMapFile() {
    auto* file = static_cast<CMapFile*>(msvc6::GameOperatorNew(sizeof(CMapFile)));
    std::memset(file, 0, sizeof(CMapFile));  // the dictionaries' value slots are never initialized
    return kConstruct(file, nullptr, 0);
}

inline CMapFile* OpenMapFile(const std::string& path, std::uint32_t mode) {
    CMapFile* file = NewMapFile();
    msvc6::String name;
    msvc6::String_FromChars(&name, nullptr, path.c_str(), static_cast<std::uint32_t>(path.size()));
    kOpen(file, nullptr, &name, mode);
    msvc6::String_Destroy(&name, nullptr);
    return file;
}

inline void DeleteMapFile(CMapFile* file) {
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

inline std::uint32_t AddressOf(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

inline const std::uint8_t* PointerOf(std::uint32_t value) {
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value));
}

// One dictionary entry, as seen from level 0.
struct Entry {
    std::uint32_t key;
    std::uint32_t value;
    std::uint32_t valueAddress;
    std::uint32_t node;
};

inline std::vector<Entry> Entries(const SkipListDict& dict) {
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
inline std::vector<std::uint32_t> KeysOnLevel(const SkipListDict& dict, std::int32_t level) {
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
inline std::vector<std::uint8_t> ChunkBytes(const std::uint8_t* buffer) {
    if (buffer == nullptr) {
        return {};
    }
    const std::uint32_t size = reinterpret_cast<const ChunkHeader*>(buffer)->size;
    return std::vector<std::uint8_t>(buffer, buffer + size);
}

// A return value that may point into the file's dictionaries, as something
// comparable between two objects: small values as they are, value
// addresses as (dictionary, key).
inline std::uint64_t DescribeResult(const CMapFile* file, std::uint32_t eax) {
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

inline std::uint32_t FilePosition(const CMapFile* file) {
    return SetFilePointer(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(file->stream.file.handle)), 0,
                          nullptr, FILE_CURRENT);
}

inline void CompareDict(test::Context& ctx, const SkipListDict& a, const SkipListDict& b, bool chunkValues) {
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

inline void CompareFiles(test::Context& ctx, const CMapFile* a, const CMapFile* b) {
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
// Files
// ---------------------------------------------------------------------------

inline std::string TempFolder() {
    char buffer[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buffer);
    std::string folder = std::string(buffer) + "s3re-mapfile-test";
    CreateDirectoryA(folder.c_str(), nullptr);
    return folder;
}

inline void WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(handle);
}

inline std::string GameFolder() {
    char buffer[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
}

inline void ListFiles(const std::string& folder, std::vector<std::string>& files) {
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

inline std::vector<std::uint8_t> ReadWholeFile(const std::string& path) {
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

inline std::vector<ParsedChunk> ParseContainer(const std::vector<std::uint8_t>& file) {
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

inline std::uint32_t StoredWord(const std::vector<std::uint8_t>& file, std::size_t at) {
    std::uint32_t word = 0;
    if (at + 4 <= file.size()) {
        std::memcpy(&word, file.data() + at, 4);
    }
    return word;
}

inline std::uint32_t ComputeChecksum(const std::vector<std::uint8_t>& file) {
    std::uint32_t checksum = 0;
    for (std::size_t at = 8; at + 4 <= file.size(); at += 4) {
        checksum = fileformats::ChecksumStep(checksum, StoredWord(file, at));
    }
    return checksum;
}


}  // namespace s3re::game::mapfile_test
