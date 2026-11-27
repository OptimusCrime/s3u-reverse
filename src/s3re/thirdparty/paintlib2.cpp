// More of PaintLib (paintlib.cpp has the bitmap classes and CDataSource):
// the PNG decoder's own class, the exception's constructor and the file
// source (libs-r7). Member functions are __thiscall (written as __fastcall
// with the dummy EDX).
//
// CTextException::CTextException (0x005a4230): (code, text) ret 8: vtable,
//   SetErrorMode(0) (sic: PaintLib's), +8 = a heap copy of the text
//   (strlen + 1 bytes, no null check), +4 = code. EAX = this.
// CPNGDecoder (vtable 0x65c0fc), derived from CPicDecoder:
//   CPNGDecoder (0x005a45c0): CPicDecoder's constructor, then the vtable.
//   ~CPNGDecoder (0x005a4600): the vtable, then ~CPicDecoder (a tail jump;
//     EAX is ~CPicDecoder's).
//   scalar_deleting_destructor (0x005a45e0): ~CPNGDecoder, then operator
//     delete if bit 0 of the flags is set. EAX = this.
// CFileSource (vtable 0x65c1a4), derived from CDataSource: +0x18 the file
//   handle, +0x1c the file mapping.
//   CFileSource (0x005a9b90): CDataSource's constructor, the vtable, both
//     handles 0.
//   ~CFileSource (0x005a9bd0): the vtable; with a file open, Close inlined;
//     then ~CDataSource.
//   scalar_deleting_destructor (0x005a9bb0): as CPNGDecoder's.
//   Close (0x005a9de0, vtable slot 2): UnmapViewOfFile(+8, the read
//     cursor, not +4: after reading, the view is not unmapped; kept),
//     CDataSource::Release, CloseHandle(mapping), CloseHandle(file), both
//     handles 0. EAX = 0.
//   Open (0x005a9c60, slot 3): (path) ret 4: CreateFileA(path, GENERIC_READ,
//     FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
//     FILE_FLAG_SEQUENTIAL_SCAN, 0) into +0x18; failing: 3 for
//     ERROR_PATH_NOT_FOUND, 5 for ERROR_ACCESS_DENIED / SHARING_VIOLATION,
//     else 4. Then GetFileSize, CreateFileMappingA(PAGE_READONLY) into +0x1c
//     and MapViewOfFile(FILE_MAP_READ): either failing returns 5 (the
//     handles stay open; kept). Else CDataSource::Attach(path, view, size),
//     0. If Attach throws, the view and handles are released and the
//     exception passed on.

#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::thirdparty {

namespace {

#pragma pack(push, 1)
struct TextException {
    std::uint32_t vtable;  // 0x00
    std::int32_t code;     // 0x04
    char* text;            // 0x08
};

struct FileSource {
    std::uint32_t vtable;  // 0x00
    std::uint8_t* data;    // 0x04 (CDataSource)
    std::uint8_t* cursor;  // 0x08
    char* name;            // 0x0c
    std::uint32_t size;    // 0x10
    std::uint32_t unk14;   // 0x14
    void* file;            // 0x18
    void* mapping;         // 0x1c
};
#pragma pack(pop)
static_assert(offsetof(FileSource, file) == 0x18);
static_assert(sizeof(FileSource) == 0x20);

}  // namespace

using ThisFn = void*(__fastcall*)(void* self, void* edx);
using DeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using ResultFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ExceptionConstructFn = void*(__fastcall*)(void* self, void* edx, std::int32_t code, const char* text);
using OpenFn = std::uint32_t(__fastcall*)(void* self, void* edx, const char* path);

void* __fastcall CTextException_Construct(void* self, void* edx, std::int32_t code, const char* text);
void* __fastcall CPNGDecoder_Construct(void* self, void* edx);
std::uint32_t __fastcall CPNGDecoder_Destroy(void* self, void* edx);
void* __fastcall CPNGDecoder_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void* __fastcall CFileSource_Construct(void* self, void* edx);
std::uint32_t __fastcall CFileSource_Destroy(void* self, void* edx);
void* __fastcall CFileSource_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall CFileSource_Close(void* self, void* edx);
std::uint32_t __fastcall CFileSource_Open(void* self, void* edx, const char* path);

S3RE_HOOK(0x005a4230, CTextException_Construct, ExceptionConstructFn, Original_CTextException_Construct,
          CTextException_Construct);
S3RE_HOOK(0x005a45c0, CPNGDecoder_Construct, ThisFn, Original_CPNGDecoder_Construct, CPNGDecoder_Construct);
S3RE_HOOK(0x005a4600, CPNGDecoder_Destroy, ResultFn, Original_CPNGDecoder_Destroy, CPNGDecoder_Destroy);
S3RE_HOOK(0x005a45e0, CPNGDecoder_DeletingDestructor, DeletingFn, Original_CPNGDecoder_DeletingDestructor,
          CPNGDecoder_DeletingDestructor);
S3RE_HOOK(0x005a9b90, CFileSource_Construct, ThisFn, Original_CFileSource_Construct, CFileSource_Construct);
S3RE_HOOK(0x005a9bd0, CFileSource_Destroy, ResultFn, Original_CFileSource_Destroy, CFileSource_Destroy);
S3RE_HOOK(0x005a9bb0, CFileSource_DeletingDestructor, DeletingFn, Original_CFileSource_DeletingDestructor,
          CFileSource_DeletingDestructor);
S3RE_HOOK(0x005a9de0, CFileSource_Close, ResultFn, Original_CFileSource_Close, CFileSource_Close);
S3RE_HOOK(0x005a9c60, CFileSource_Open, OpenFn, Original_CFileSource_Open, CFileSource_Open);

namespace {

constexpr std::uint32_t kDeleteSelf = 1;
constexpr std::uintptr_t kPicDecoderConstruct = 0x005a4980;  // CPicDecoder::CPicDecoder (ours)
constexpr std::uintptr_t kPicDecoderDestroy = 0x005a49d0;    // CPicDecoder::~CPicDecoder (ours)
constexpr std::uintptr_t kDataSourceConstruct = 0x005b03f0;  // CDataSource::CDataSource (ours)
constexpr std::uintptr_t kDataSourceDestroy = 0x005b0460;    // CDataSource::~CDataSource (ours)
constexpr std::uintptr_t kDataSourceAttach = 0x005b04a0;     // CDataSource::Attach (ours)
constexpr std::uintptr_t kDataSourceRelease = 0x005b0500;    // CDataSource::Release (ours)
constexpr std::uintptr_t kFileSourceDestroy = 0x005a9bd0;
constexpr std::uintptr_t kPngDecoderDestroy = 0x005a4600;

constexpr std::uint32_t kGenericRead = 0x80000000;
constexpr std::uint32_t kFileShareRead = 1;
constexpr std::uint32_t kOpenExisting = 3;
constexpr std::uint32_t kNormalSequential = 0x08000080;  // FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL
constexpr std::uint32_t kPageReadonly = 2;
constexpr std::uint32_t kFileMapRead = 4;
const auto kInvalidHandle = reinterpret_cast<void*>(~std::uintptr_t{0});  // INVALID_HANDLE_VALUE
constexpr std::uint32_t kErrorPathNotFound = 3;
constexpr std::uint32_t kErrorAccessDenied = 5;
constexpr std::uint32_t kErrorSharingViolation = 32;

// CFileSource::Open's results.
constexpr std::uint32_t kOpenOk = 0;
constexpr std::uint32_t kOpenPathNotFound = 3;
constexpr std::uint32_t kOpenFailed = 4;
constexpr std::uint32_t kOpenNoAccess = 5;  // also a mapping failure

using AttachFn = std::uint8_t*(__fastcall*)(void* self, void* edx, const char* name, std::uint8_t* data,
                                            std::uint32_t size);

void* CallThis(std::uintptr_t function, void* self) {
    return reinterpret_cast<ThisFn>(function)(self, nullptr);
}

// The handles and the view released (Close's body, inlined in the
// destructor).
void ReleaseFile(FileSource* source) {
    game::g_importUnmapViewOfFile.get()(source->cursor);
    CallThis(kDataSourceRelease, source);
    game::g_importCloseHandle.get()(source->mapping);
    game::g_importCloseHandle.get()(source->file);
    source->mapping = nullptr;
    source->file = nullptr;
}

}  // namespace

// Purpose: Constructs PaintLib's exception (an error code and a copy of the
// message).
void* __fastcall CTextException_Construct(void* self, void*, std::int32_t code, const char* text) {
    auto* exception = static_cast<TextException*>(self);
    exception->vtable = game::vtables::kCTextException;
    game::g_importSetErrorMode.get()(0);
    const std::size_t size = std::strlen(text) + 1;
    exception->text = static_cast<char*>(msvc6::GameOperatorNew(size));
    std::memcpy(exception->text, text, size);
    exception->code = code;
    return self;
}

// Purpose: Constructs the PNG decoder object (the lobby's banner).
void* __fastcall CPNGDecoder_Construct(void* self, void*) {
    CallThis(kPicDecoderConstruct, self);
    *static_cast<std::uint32_t*>(self) = game::vtables::kCPNGDecoder;
    return self;
}

// Purpose: The PNG decoder's destructor.
std::uint32_t __fastcall CPNGDecoder_Destroy(void* self, void*) {
    *static_cast<std::uint32_t*>(self) = game::vtables::kCPNGDecoder;
    return reinterpret_cast<ResultFn>(kPicDecoderDestroy)(self, nullptr);
}

// Purpose: Destroys the PNG decoder and frees it if asked to.
void* __fastcall CPNGDecoder_DeletingDestructor(void* self, void*, std::uint32_t flags) {
    reinterpret_cast<ResultFn>(kPngDecoderDestroy)(self, nullptr);
    if ((flags & kDeleteSelf) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

// Purpose: Constructs the decoder's file source (no file open yet).
void* __fastcall CFileSource_Construct(void* self, void*) {
    CallThis(kDataSourceConstruct, self);
    auto* source = static_cast<FileSource*>(self);
    source->vtable = game::vtables::kCFileSource;
    source->file = nullptr;
    source->mapping = nullptr;
    return self;
}

// Purpose: The file source's destructor: closes an open file.
std::uint32_t __fastcall CFileSource_Destroy(void* self, void*) {
    auto* source = static_cast<FileSource*>(self);
    source->vtable = game::vtables::kCFileSource;
    if (source->file != nullptr) {
        ReleaseFile(source);
    }
    return reinterpret_cast<ResultFn>(kDataSourceDestroy)(self, nullptr);
}

// Purpose: Destroys the file source and frees it if asked to.
void* __fastcall CFileSource_DeletingDestructor(void* self, void*, std::uint32_t flags) {
    reinterpret_cast<ResultFn>(kFileSourceDestroy)(self, nullptr);
    if ((flags & kDeleteSelf) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

// Purpose: Closes the mapped file (unmaps it, detaches the data, closes
// the handles).
std::uint32_t __fastcall CFileSource_Close(void* self, void*) {
    ReleaseFile(static_cast<FileSource*>(self));
    return 0;
}

// Purpose: Opens a file for the decoder by mapping all of it into memory.
std::uint32_t __fastcall CFileSource_Open(void* self, void*, const char* path) {
    auto* source = static_cast<FileSource*>(self);
    source->file = game::g_importCreateFileA.get()(path, kGenericRead, kFileShareRead, nullptr, kOpenExisting,
                                                   kNormalSequential, nullptr);
    if (source->file == kInvalidHandle) {
        switch (game::g_importGetLastError.get()()) {
            case kErrorPathNotFound: return kOpenPathNotFound;
            case kErrorAccessDenied:
            case kErrorSharingViolation: return kOpenNoAccess;
            default: return kOpenFailed;
        }
    }
    const std::uint32_t size = game::g_importGetFileSize.get()(source->file, nullptr);
    source->mapping = game::g_importCreateFileMappingA.get()(source->file, nullptr, kPageReadonly, 0, 0, nullptr);
    if (source->mapping == nullptr) {
        return kOpenNoAccess;
    }
    void* const view = game::g_importMapViewOfFile.get()(source->mapping, kFileMapRead, 0, 0, 0);
    if (view == nullptr) {
        return kOpenNoAccess;
    }
    try {
        reinterpret_cast<AttachFn>(kDataSourceAttach)(self, nullptr, path, static_cast<std::uint8_t*>(view), size);
    } catch (...) {
        game::g_importUnmapViewOfFile.get()(view);
        if (source->mapping != nullptr) {
            game::g_importCloseHandle.get()(source->mapping);
        }
        if (source->file != nullptr) {
            game::g_importCloseHandle.get()(source->file);
        }
        throw;
    }
    return kOpenOk;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Objects are random 0x40-byte buffers. The callees (CPicDecoder's and
// CDataSource's members) are recording stubs; the file functions' imports
// are test::FakeWin32 (the same scripted results for both runs), and
// SetErrorMode's import slot points at a logger. Heap blocks (the
// exception's text) are compared by contents and freed. Compared: the
// buffers, EAX, the stub calls and the import logs.

namespace {

std::vector<std::uint32_t>* g_errorModeLog = nullptr;

std::uint32_t __stdcall FakeSetErrorMode(std::uint32_t mode) {
    g_errorModeLog->push_back(mode);
    return 0x8001;
}

constexpr std::uint32_t kAllFileImports = test::kFakeCreateFile | test::kFakeGetLastError | test::kFakeGetFileSize |
                                          test::kFakeCreateFileMapping | test::kFakeMapView | test::kFakeUnmapView |
                                          test::kFakeCloseHandle;

struct Functions {
    ExceptionConstructFn exceptionConstruct;
    ThisFn decoderConstruct;
    ResultFn decoderDestroy;
    DeletingFn decoderDeleting;
    ThisFn sourceConstruct;
    ResultFn sourceDestroy;
    DeletingFn sourceDeleting;
    ResultFn sourceClose;
    OpenFn sourceOpen;
};

const Functions kOriginals{Original_CTextException_Construct, Original_CPNGDecoder_Construct,
                           Original_CPNGDecoder_Destroy,      Original_CPNGDecoder_DeletingDestructor,
                           Original_CFileSource_Construct,    Original_CFileSource_Destroy,
                           Original_CFileSource_DeletingDestructor, Original_CFileSource_Close,
                           Original_CFileSource_Open};
const Functions kOurs{&CTextException_Construct, &CPNGDecoder_Construct, &CPNGDecoder_Destroy,
                      &CPNGDecoder_DeletingDestructor, &CFileSource_Construct, &CFileSource_Destroy,
                      &CFileSource_DeletingDestructor, &CFileSource_Close, &CFileSource_Open};

struct Run {
    std::uint32_t eax = 0;
    std::uint8_t object[0x40] = {};
    std::string text;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> imports;
    std::vector<std::uint32_t> errorModes;
};

Run RunOne(const Functions& f, int op, const std::uint8_t* initial, std::int32_t code, const char* text,
           std::uint32_t flags, std::uint64_t seed) {
    Run run;
    std::memcpy(run.object, initial, sizeof(run.object));
    test::FakeWin32Config config;
    test::FakeWin32 fake(seed, kAllFileImports, config);
    g_errorModeLog = &run.errorModes;
    game::SetErrorModeImport fakeMode = &FakeSetErrorMode;
    test::PatchBytes(game::g_importSetErrorMode.address(), &fakeMode, sizeof(fakeMode));
    fake.Install();
    {
        test::CalleeStubs stubs;
        const auto self = [](game::CGameData*, std::uint32_t self, const std::uint32_t*) { return self; };
        stubs.Install<0, kPicDecoderConstruct>(self);
        stubs.Install<0, kPicDecoderDestroy>(self);
        stubs.Install<0, kDataSourceConstruct>(self);
        stubs.Install<0, kDataSourceDestroy>(self);
        stubs.Install<0, kDataSourceRelease>([](game::CGameData*, std::uint32_t, const std::uint32_t*) { return 0u; });
        stubs.Install<3, kDataSourceAttach>(
            [](game::CGameData*, std::uint32_t, const std::uint32_t* args) { return args[1]; });
        stubs.SetWorld(reinterpret_cast<game::CGameData*>(run.object));
        void* const object = run.object;
        const auto rel = [&](std::uint32_t eax) { return eax - reinterpret_cast<std::uint32_t>(object); };
        switch (op) {
            case 0: run.eax = rel(reinterpret_cast<std::uint32_t>(f.exceptionConstruct(object, nullptr, code, text))); break;
            case 1: run.eax = rel(reinterpret_cast<std::uint32_t>(f.decoderConstruct(object, nullptr))); break;
            case 2: run.eax = f.decoderDestroy(object, nullptr); break;
            case 3: run.eax = rel(reinterpret_cast<std::uint32_t>(f.decoderDeleting(object, nullptr, flags & ~1u))); break;
            case 4: run.eax = rel(reinterpret_cast<std::uint32_t>(f.sourceConstruct(object, nullptr))); break;
            case 5: f.sourceDestroy(object, nullptr); break;
            case 6: run.eax = rel(reinterpret_cast<std::uint32_t>(f.sourceDeleting(object, nullptr, flags & ~1u))); break;
            case 7: run.eax = f.sourceClose(object, nullptr); break;
            default: run.eax = f.sourceOpen(object, nullptr, text); break;
        }
        run.calls = stubs.Take();
    }
    fake.Uninstall();
    test::UnpatchBytes(game::g_importSetErrorMode.address());
    g_errorModeLog = nullptr;
    run.imports = fake.log();
    if (op == 0) {  // the heap copy of the text, compared by contents
        auto* exception = reinterpret_cast<TextException*>(run.object);
        run.text = exception->text;
        msvc6::GameOperatorDelete(exception->text);
        exception->text = nullptr;
    }
    return run;
}

bool Compare(test::Context& ctx, const Functions& candidate, int cases) {
    static const char* const kTexts[] = {"", "x", "Decoder error", "C:\\Games\\S3\\Banner\\Banner.png"};
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(9));
        std::uint8_t initial[0x40];
        ctx.rng().Fill(initial, sizeof(initial));
        auto* source = reinterpret_cast<FileSource*>(initial);
        if (ctx.rng().Chance(1, 2)) {
            source->file = nullptr;
        }
        const std::int32_t code = static_cast<std::int32_t>(ctx.rng().Next32());
        const char* text = kTexts[ctx.rng().Below(4)];
        const std::uint32_t flags = ctx.rng().Next32();
        const std::uint64_t seed = ctx.rng().Next64();
        const Run a = RunOne(kOriginals, op, initial, code, text, flags, seed);
        const Run b = RunOne(candidate, op, initial, code, text, flags, seed);
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT_MEM_EQ(a.object, b.object, sizeof(a.object)) ||
            !S3RE_EXPECT(a.text == b.text) || !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT(a.imports == b.imports) ||
            !S3RE_EXPECT(a.errorModes == b.errorModes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return false;
        }
    }
    return true;
}

// Wrong: Open reports a missing path like any other error.
std::uint32_t __fastcall WrongOpen(void* self, void* edx, const char* path) {
    const std::uint32_t result = CFileSource_Open(self, edx, path);
    return result == kOpenPathNotFound ? kOpenFailed : result;
}

}  // namespace

S3RE_TEST(Paintlib2_MatchOriginal) {
    Compare(ctx, kOurs, 4000);
}

S3RE_TEST(Paintlib2_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.sourceOpen = &WrongOpen;
    Compare(probe, wrong, 4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
