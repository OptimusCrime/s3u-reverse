// Tests of CMapFileStream (map_file_stream.cpp): the original against ours
// on real files in a temporary folder, including the error paths (missing
// and locked files, reads of write-only files, negative seeks, calls after
// Close), which reach the catch blocks, and streams that rethrow.

#include "game/fileformats/map_file.h"

#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::CMapFileStream;

CMapFileStream* __fastcall CMapFileStream_Construct(CMapFileStream* self, void* edx, std::uint32_t rethrow);
std::uint32_t __fastcall CMapFileStream_Open(CMapFileStream* self, void* edx, const msvc6::String* path,
                                             std::uint32_t mode);
std::uint32_t __fastcall CMapFileStream_Close(CMapFileStream* self, void* edx);
std::int32_t __fastcall CMapFileStream_Read(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size);
std::int32_t __fastcall CMapFileStream_Write(CMapFileStream* self, void* edx, const void* buffer,
                                             std::uint32_t size);
std::uint32_t __fastcall CMapFileStream_Seek(CMapFileStream* self, void* edx, std::int32_t offset,
                                             std::uint32_t origin);
std::uint32_t __fastcall CMapFileStream_GetSize(CMapFileStream* self, void* edx);

using StreamConstructFn = CMapFileStream*(__fastcall*)(CMapFileStream* self, void* edx, std::uint32_t rethrow);
using StreamOpenFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, const msvc6::String* path,
                                                std::uint32_t mode);
using StreamCloseFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx);
using StreamReadHookFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size);
using StreamWriteFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, const void* buffer,
                                                std::uint32_t size);
using StreamSeekHookFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, std::int32_t offset,
                                                    std::uint32_t origin);
using StreamGetSizeFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx);
extern StreamConstructFn Original_CMapFileStream_Construct;
extern StreamOpenFn Original_CMapFileStream_Open;
extern StreamCloseFn Original_CMapFileStream_Close;
extern StreamReadHookFn Original_CMapFileStream_Read;
extern StreamWriteFn Original_CMapFileStream_Write;
extern StreamSeekHookFn Original_CMapFileStream_Seek;
extern StreamGetSizeFn Original_CMapFileStream_GetSize;

namespace {

struct StreamFunctions {
    StreamConstructFn construct;
    StreamOpenFn open;
    StreamCloseFn close;
    StreamReadHookFn read;
    StreamWriteFn write;
    StreamSeekHookFn seek;
    StreamGetSizeFn getSize;
};

StreamFunctions OriginalStream() {
    return {Original_CMapFileStream_Construct, Original_CMapFileStream_Open,  Original_CMapFileStream_Close,
            Original_CMapFileStream_Read,      Original_CMapFileStream_Write, Original_CMapFileStream_Seek,
            Original_CMapFileStream_GetSize};
}

StreamFunctions OurStream() {
    return {&CMapFileStream_Construct, &CMapFileStream_Open,  &CMapFileStream_Close,  &CMapFileStream_Read,
            &CMapFileStream_Write,     &CMapFileStream_Seek, &CMapFileStream_GetSize};
}

// One call's outcome: EAX, and whether an exception left the function
// (streams constructed with rethrow = 1).
struct Outcome {
    std::uint32_t eax = 0;
    bool escaped = false;
};

int EscapeFilter(DWORD code) {
    return code == 0xe06d7363 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

enum class Op { Open, Close, Read, Write, Seek, GetSize };

struct Call {
    Op op;
    const msvc6::String* path;
    std::uint32_t mode;
    void* buffer;
    std::uint32_t size;
    std::int32_t offset;
    std::uint32_t origin;
};

// Plain function pointers and PODs only: __try needs a frame without C++
// objects to unwind. The exception that escapes is leaked (test only).
Outcome Run(const StreamFunctions& f, CMapFileStream* stream, const Call& call) {
    Outcome outcome;
    __try {
        switch (call.op) {
        case Op::Open: outcome.eax = f.open(stream, nullptr, call.path, call.mode); break;
        case Op::Close: outcome.eax = f.close(stream, nullptr); break;
        case Op::Read: outcome.eax = static_cast<std::uint32_t>(f.read(stream, nullptr, call.buffer, call.size)); break;
        case Op::Write:
            outcome.eax = static_cast<std::uint32_t>(f.write(stream, nullptr, call.buffer, call.size));
            break;
        case Op::Seek: outcome.eax = f.seek(stream, nullptr, call.offset, call.origin); break;
        case Op::GetSize: outcome.eax = f.getSize(stream, nullptr); break;
        }
    } __except (EscapeFilter(GetExceptionCode())) {
        outcome.escaped = true;
    }
    return outcome;
}

std::string TempFolder() {
    char buffer[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buffer);
    std::string folder = std::string(buffer) + "s3re-mapfile-stream-test";
    CreateDirectoryA(folder.c_str(), nullptr);
    return folder;
}

void WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    if (!bytes.empty()) {
        WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }
    CloseHandle(handle);
}

std::vector<std::uint8_t> ReadWholeFile(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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

bool FileExists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::uint32_t Position(const CMapFileStream& stream) {
    return SetFilePointer(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(stream.file.handle)), 0, nullptr,
                          FILE_CURRENT);
}

void CompareStreams(test::Context& ctx, const CMapFileStream& a, const CMapFileStream& b) {
    S3RE_EXPECT_EQ(a.rethrow, b.rethrow);
    S3RE_EXPECT_EQ(a.file.vtable, b.file.vtable);
    S3RE_EXPECT_EQ(a.file.owned, b.file.owned);
    S3RE_EXPECT_EQ(a.file.handle != 0, b.file.handle != 0);
    S3RE_EXPECT_EQ(a.error, b.error);
    if (a.file.handle != 0 && b.file.handle != 0) {
        S3RE_EXPECT_EQ(Position(a), Position(b));
    }
}

// The paths of one round: A's and B's own file (the same text for reads),
// a missing file, and a file the test holds open without sharing.
struct Files {
    std::string a, b, missingA, missingB, lockedA, lockedB;
};

// How often the rounds reached the catch blocks (for the coverage check).
struct Seen {
    int caught = 0;        // an operation that set the error (a caught CHResult)
    int fileNotFound = 0;  // Open's CFileNotFound clause
    int escaped = 0;       // rethrown out of the stream
};

void CompareStreamRound(test::Context& ctx, const StreamFunctions& ours, const Files& files, Seen& seen) {
    test::Rng& rng = ctx.rng();
    const StreamFunctions original = OriginalStream();
    std::vector<std::uint8_t> content(rng.Below(600));
    rng.Fill(content.data(), content.size());
    WriteWholeFile(files.a, content);
    WriteWholeFile(files.b, content);
    WriteWholeFile(files.lockedA, content);
    WriteWholeFile(files.lockedB, content);
    DeleteFileA(files.missingA.c_str());
    DeleteFileA(files.missingB.c_str());
    HANDLE lockA = CreateFileA(files.lockedA.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE lockB = CreateFileA(files.lockedB.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    const std::uint32_t rethrow = rng.Chance(1, 4) ? 1 : 0;
    CMapFileStream a;
    CMapFileStream b;
    rng.Fill(&a, sizeof(a));
    std::memcpy(&b, &a, sizeof(b));
    S3RE_EXPECT_EQ(original.construct(&a, nullptr, rethrow), &a);
    S3RE_EXPECT_EQ(ours.construct(&b, nullptr, rethrow), &b);
    S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));

    // Open: own file (existing), a missing one, a locked one, an empty
    // name; modes with read and/or write, or neither.
    constexpr std::uint32_t kModes[] = {1, 1, 1, 5, 2, 3, 0, 6};
    const std::uint32_t mode = kModes[rng.Below(8)];
    const std::uint32_t which = rng.Below(10);
    const std::string pathA = which < 7 ? files.a : which == 7 ? files.missingA : which == 8 ? files.lockedA : "";
    const std::string pathB = which < 7 ? files.b : which == 7 ? files.missingB : which == 8 ? files.lockedB : "";
    msvc6::String nameA;
    msvc6::String nameB;
    msvc6::String_FromChars(&nameA, nullptr, pathA.c_str(), static_cast<std::uint32_t>(pathA.size()));
    msvc6::String_FromChars(&nameB, nullptr, pathB.c_str(), static_cast<std::uint32_t>(pathB.size()));
    if (rng.Chance(1, 10)) {
        a.error = b.error = rng.Next32() | 1;  // an error pending before Open
    }
    Call call{Op::Open, &nameA, mode, nullptr, 0, 0, 0};
    const Outcome openA = Run(original, &a, call);
    call.path = &nameB;
    const Outcome openB = Run(ours, &b, call);
    seen.fileNotFound += a.error == 2 ? 1 : 0;
    seen.escaped += openA.escaped ? 1 : 0;
    S3RE_EXPECT_EQ(openA.eax, openB.eax);
    S3RE_EXPECT_EQ(openA.escaped, openB.escaped);
    CompareStreams(ctx, a, b);
    S3RE_EXPECT_EQ(FileExists(pathA), FileExists(pathB));

    const int operations = static_cast<int>(rng.Below(30));
    for (int i = 0; i < operations && ctx.failures() == 0; ++i) {
        std::uint8_t bufferA[512];
        std::uint8_t bufferB[512];
        rng.Fill(bufferA, sizeof(bufferA));
        std::memcpy(bufferB, bufferA, sizeof(bufferB));
        Call op{};
        op.size = rng.Below(sizeof(bufferA) + 1);
        op.buffer = bufferA;
        switch (rng.Below(14)) {
        case 0: case 1: case 2: case 3: op.op = Op::Read; break;
        case 4: case 5: op.op = Op::Write; break;
        case 6: case 7: case 8: case 9:
            op.op = Op::Seek;
            op.offset = rng.Range(-300, 700);
            op.origin = rng.Chance(1, 10) ? rng.Below(5) : rng.Below(3);
            break;
        case 10: case 11: op.op = Op::GetSize; break;
        case 12:
            op.op = Op::Close;
            break;
        default:
            if (rng.Chance(1, 2)) {
                a.error = b.error = rng.Chance(1, 2) ? 0 : rng.Next32();
            }
            continue;
        }
        const std::uint32_t errorBefore = a.error;
        const Outcome resultA = Run(original, &a, op);
        op.buffer = bufferB;
        const Outcome resultB = Run(ours, &b, op);
        seen.escaped += resultA.escaped ? 1 : 0;
        seen.caught += errorBefore == 0 && a.error != 0 ? 1 : 0;
        S3RE_EXPECT_EQ(resultA.eax, resultB.eax);
        S3RE_EXPECT_EQ(resultA.escaped, resultB.escaped);
        S3RE_EXPECT_MEM_EQ(bufferA, bufferB, sizeof(bufferA));
        CompareStreams(ctx, a, b);
    }
    // Close both (the original's Close), then compare what was written.
    a.error = b.error = 0;
    Run(original, &a, Call{Op::Close, nullptr, 0, nullptr, 0, 0, 0});
    Run(original, &b, Call{Op::Close, nullptr, 0, nullptr, 0, 0, 0});
    S3RE_EXPECT(ReadWholeFile(files.a) == ReadWholeFile(files.b));
    msvc6::String_Destroy(&nameA, nullptr);
    msvc6::String_Destroy(&nameB, nullptr);
    CloseHandle(lockA);
    CloseHandle(lockB);
}

Seen CompareStreamRounds(test::Context& ctx, const StreamFunctions& ours, int rounds) {
    Seen seen;
    const std::string folder = TempFolder();
    const Files files{folder + "\\a.sav",         folder + "\\b.sav",        folder + "\\missing-a.sav",
                      folder + "\\missing-b.sav", folder + "\\locked-a.sav", folder + "\\locked-b.sav"};
    for (int round = 0; round < rounds && ctx.failures() == 0; ++round) {
        CompareStreamRound(ctx, ours, files, seen);
    }
    DeleteFileA(files.a.c_str());
    DeleteFileA(files.b.c_str());
    DeleteFileA(files.lockedA.c_str());
    DeleteFileA(files.lockedB.c_str());
    DeleteFileA(files.missingA.c_str());
    DeleteFileA(files.missingB.c_str());
    return seen;
}

// Deliberately wrong: Seek returns 0 after a caught error.
std::uint32_t __fastcall WrongSeek(CMapFileStream* self, void* edx, std::int32_t offset, std::uint32_t origin) {
    const std::uint32_t errorBefore = self->error;
    const std::uint32_t position = CMapFileStream_Seek(self, edx, offset, origin);
    return self->error != errorBefore ? 0 : position;
}

// Deliberately wrong: Read reports the bytes read even with an error.
std::int32_t __fastcall WrongRead(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size) {
    const std::int32_t result = CMapFileStream_Read(self, edx, buffer, size);
    return result == -1 ? 0 : result;
}

}  // namespace

S3RE_TEST(MapFileStream_MatchOriginal) {
    const Seen seen = CompareStreamRounds(ctx, OurStream(), 1500);
    S3RE_EXPECT(seen.caught > 100);
    S3RE_EXPECT(seen.fileNotFound > 10);
    S3RE_EXPECT(seen.escaped > 10);
}

S3RE_TEST(MapFileStream_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        StreamFunctions wrong = OurStream();
        if (k == 0) {
            wrong.seek = &WrongSeek;
        } else {
            wrong.read = &WrongRead;
        }
        CompareStreamRounds(probe, wrong, 300);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
