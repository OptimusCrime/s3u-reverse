// Tests of win32_mapping.cpp (Win32::CFile, Win32::CFileMapping) and
// win32_detach.cpp (Win32::DetachMemory).
//
// Scripted: the imports are replaced by test::FakeWin32 (results drawn
// from a seeded Rng, every call logged); the original runs on one object
// and ours on a copy, with fakes from the same seed; compared: EAX, the
// object, the out-parameters, the logs and the thrown exception (type,
// CHResult code). Every operation runs on random object states and random
// arguments, failures included (CFileNotFound, ThrowWin32Error).
//
// Real files: a temporary file written by the test is opened, sized,
// read, mapped (PAGE_READONLY and PAGE_WRITECOPY), unmapped, closed and
// destroyed by the original and by ours; a missing file and a folder give
// CFileNotFound and a CHResult. DetachMemory runs on two copy-on-write
// views of the same file (original on one, ours on the other); the file
// is then rewritten: a detached view keeps the old bytes, a third view
// that wasn't detached shows the new ones.

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall Win32_CFile_Open(void* self, void* edx, const char* name, std::uint32_t access,
                                          std::uint32_t share, void* security, std::uint32_t disposition,
                                          std::uint32_t flags, void* templateFile);
std::uint32_t __fastcall Win32_CFile_Read(void* self, void* edx, void* buffer, std::uint32_t size,
                                          std::uint32_t* done, void* overlapped);
std::uint32_t __fastcall Win32_CFile_Write(void* self, void* edx, const void* buffer, std::uint32_t size,
                                           std::uint32_t* done, void* overlapped);
std::uint32_t __fastcall Win32_CFile_Seek(void* self, void* edx, std::int32_t low, std::int32_t* high,
                                          std::uint32_t method);
std::uint32_t __fastcall Win32_CFile_GetSize(void* self, void* edx, std::uint32_t* high);
void* __fastcall Win32_CFileMapping_Construct(void* self, void* edx);
std::uint32_t __fastcall Win32_CFileMapping_Destroy(void* self, void* edx);
void* __fastcall Win32_CFileMapping_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall Win32_CFileMapping_Close(void* self, void* edx);
std::uint32_t __fastcall Win32_CFileMapping_Create(void* self, void* edx, void* file, void* security,
                                                   std::uint32_t protect, std::uint32_t sizeHigh,
                                                   std::uint32_t sizeLow, const char* name);
std::uint32_t __fastcall Win32_CFileMapping_MapView(void* self, void* edx, std::uint32_t access,
                                                    std::uint32_t offsetHigh, std::uint32_t offsetLow,
                                                    std::uint32_t size);
std::uint32_t __fastcall Win32_CFileMapping_UnmapView(void* self, void* edx, std::uint32_t view);
std::uint32_t __fastcall Win32_DetachMemory(std::uint32_t start, std::uint32_t size);

using FileOpenFn = decltype(&Win32_CFile_Open);
using FileReadFn = decltype(&Win32_CFile_Read);
using FileWriteFn = decltype(&Win32_CFile_Write);
using FileSeekFn = decltype(&Win32_CFile_Seek);
using FileGetSizeFn = decltype(&Win32_CFile_GetSize);
using MappingConstructFn = decltype(&Win32_CFileMapping_Construct);
using MappingDestroyFn = decltype(&Win32_CFileMapping_Destroy);
using MappingDeletingFn = decltype(&Win32_CFileMapping_DeletingDestroy);
using MappingCloseFn = decltype(&Win32_CFileMapping_Close);
using MappingCreateFn = decltype(&Win32_CFileMapping_Create);
using MappingMapViewFn = decltype(&Win32_CFileMapping_MapView);
using MappingUnmapViewFn = decltype(&Win32_CFileMapping_UnmapView);
using DetachMemoryFn = decltype(&Win32_DetachMemory);

extern FileOpenFn Original_Win32_CFile_Open;
extern FileReadFn Original_Win32_CFile_Read;
extern FileWriteFn Original_Win32_CFile_Write;
extern FileSeekFn Original_Win32_CFile_Seek;
extern FileGetSizeFn Original_Win32_CFile_GetSize;
extern MappingConstructFn Original_Win32_CFileMapping_Construct;
extern MappingDestroyFn Original_Win32_CFileMapping_Destroy;
extern MappingDeletingFn Original_Win32_CFileMapping_DeletingDestroy;
extern MappingCloseFn Original_Win32_CFileMapping_Close;
extern MappingCreateFn Original_Win32_CFileMapping_Create;
extern MappingMapViewFn Original_Win32_CFileMapping_MapView;
extern MappingUnmapViewFn Original_Win32_CFileMapping_UnmapView;
extern DetachMemoryFn Original_Win32_DetachMemory;

namespace {

using framework::CFileMapping;
using framework::CWin32Handle;
using test::CaughtException;
using test::FakeWin32;
using test::FakeWin32Config;

constexpr std::uintptr_t kCFileConstruct = 0x006195c0;
using CFileConstructFn = void*(__fastcall*)(void* self, void* edx);

struct Functions {
    FileOpenFn open;
    FileReadFn read;
    FileWriteFn write;
    FileSeekFn seek;
    FileGetSizeFn getSize;
    MappingConstructFn construct;
    MappingDestroyFn destroy;
    MappingDeletingFn deleting;
    MappingCloseFn close;
    MappingCreateFn create;
    MappingMapViewFn mapView;
    MappingUnmapViewFn unmapView;
    DetachMemoryFn detach;
};

Functions Originals() {
    return {Original_Win32_CFile_Open,
            Original_Win32_CFile_Read,
            Original_Win32_CFile_Write,
            Original_Win32_CFile_Seek,
            Original_Win32_CFile_GetSize,
            Original_Win32_CFileMapping_Construct,
            Original_Win32_CFileMapping_Destroy,
            Original_Win32_CFileMapping_DeletingDestroy,
            Original_Win32_CFileMapping_Close,
            Original_Win32_CFileMapping_Create,
            Original_Win32_CFileMapping_MapView,
            Original_Win32_CFileMapping_UnmapView,
            Original_Win32_DetachMemory};
}

Functions Ours() {
    return {&Win32_CFile_Open,
            &Win32_CFile_Read,
            &Win32_CFile_Write,
            &Win32_CFile_Seek,
            &Win32_CFile_GetSize,
            &Win32_CFileMapping_Construct,
            &Win32_CFileMapping_Destroy,
            &Win32_CFileMapping_DeletingDestroy,
            &Win32_CFileMapping_Close,
            &Win32_CFileMapping_Create,
            &Win32_CFileMapping_MapView,
            &Win32_CFileMapping_UnmapView,
            &Win32_DetachMemory};
}

enum class Op { Open, Read, Write, Seek, GetSize, Create, MapView, UnmapView, Close, Detach };

// One call: plain data only (the __try frame must not hold C++ objects).
struct Call {
    Op op;
    void* self;
    const char* name;
    std::uint32_t a, b, c, d, e;
    void* p;
    void* q;
};

std::uint32_t Dispatch(const Functions* f, const Call* call) {
    switch (call->op) {
    case Op::Open:
        return f->open(call->self, nullptr, call->name, call->a, call->b, call->p, call->c, call->d, call->q);
    case Op::Read:
        return f->read(call->self, nullptr, call->p, call->a, static_cast<std::uint32_t*>(call->q),
                       reinterpret_cast<void*>(call->b));
    case Op::Write:
        return f->write(call->self, nullptr, call->p, call->a, static_cast<std::uint32_t*>(call->q),
                        reinterpret_cast<void*>(call->b));
    case Op::Seek:
        return f->seek(call->self, nullptr, static_cast<std::int32_t>(call->a), static_cast<std::int32_t*>(call->q),
                       call->b);
    case Op::GetSize: return f->getSize(call->self, nullptr, static_cast<std::uint32_t*>(call->q));
    case Op::Create:
        return f->create(call->self, nullptr, call->p, call->q, call->a, call->b, call->c, call->name);
    case Op::MapView: return f->mapView(call->self, nullptr, call->a, call->b, call->c, call->d);
    case Op::UnmapView: return f->unmapView(call->self, nullptr, call->a);
    case Op::Close: return f->close(call->self, nullptr);
    case Op::Detach: return f->detach(call->a, call->b);
    }
    return 0;
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const Functions* f, const Call* call) {
    Outcome out;
    __try {
        out.eax = Dispatch(f, call);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

// ---------------------------------------------------------------------------
// A fake CFile for Close: slot 0 records its calls.
// ---------------------------------------------------------------------------

std::vector<std::uint32_t>* g_fileLog = nullptr;

void* __fastcall FakeFileDelete(void* self, void* /*edx*/, std::uint32_t flags) {
    if (g_fileLog != nullptr) {
        g_fileLog->push_back(0xf11e);
        g_fileLog->push_back(flags);
    }
    (void)self;
    return reinterpret_cast<void*>(0x5e1f);  // the same for both fake files
}

const std::uint32_t kFakeFileVtable[2] = {reinterpret_cast<std::uint32_t>(&FakeFileDelete), 0};

struct FakeFile {
    const std::uint32_t* vtable;
    std::uint32_t handle;
    std::uint32_t owned;
};

// ---------------------------------------------------------------------------
// Scripted operations
// ---------------------------------------------------------------------------

// The comparable state of a mapping: everything but the view list's data
// pointer, plus its entries (both lists are allocated separately).
struct MappingState {
    std::uint8_t bytes[sizeof(CFileMapping)];
    std::vector<std::uint32_t> entries;
};

MappingState StateOf(const CFileMapping* mapping, std::uint32_t entryCount) {
    MappingState state{};
    std::memcpy(state.bytes, mapping, sizeof(*mapping));
    std::memset(state.bytes + offsetof(CFileMapping, views) + offsetof(framework::WCPtrOrderedVector, data), 0, 4);
    if (mapping->views.data != 0) {
        const auto* data = reinterpret_cast<const std::uint32_t*>(mapping->views.data);
        state.entries.assign(data, data + entryCount);
    }
    return state;
}

bool SameMapping(test::Context& ctx, const CFileMapping* a, const CFileMapping* b) {
    // The entries in use (the rest of the block is uninitialized).
    std::uint32_t count = a->views.length > 0 ? static_cast<std::uint32_t>(a->views.length) : 0;
    count = count < a->views.capacity ? count : a->views.capacity;
    count = count < 64 ? count : 64;
    const MappingState sa = StateOf(a, count);
    const MappingState sb = StateOf(b, count);
    bool same = S3RE_EXPECT_MEM_EQ(sa.bytes, sb.bytes, sizeof(sa.bytes));
    same &= S3RE_EXPECT((a->views.data == 0) == (b->views.data == 0));
    same &= S3RE_EXPECT(sa.entries == sb.entries);
    return same;
}

bool SameOutcome(test::Context& ctx, const Outcome& a, const Outcome& b, const FakeWin32& fa, const FakeWin32& fb) {
    bool same = S3RE_EXPECT_EQ(a.eax, b.eax);
    same &= S3RE_EXPECT(a.caught == b.caught);
    if (!S3RE_EXPECT(fa.log_ == fb.log_)) {
        same = false;
        for (std::size_t i = 0; i < fa.log_.size() || i < fb.log_.size(); ++i) {
            ctx.Fail(__FILE__, __LINE__, "  log[%u]: %#x %#x", static_cast<unsigned>(i),
                     i < fa.log_.size() ? fa.log_[i] : 0xdead, i < fb.log_.size() ? fb.log_[i] : 0xdead);
        }
    }
    return same;
}

constexpr std::uint32_t kAllFileImports = test::kFakeCreateFile | test::kFakeGetLastError | test::kFakeReadFile |
                                          test::kFakeWriteFile | test::kFakeSetFilePointer |
                                          test::kFakeGetFileSize;
constexpr std::uint32_t kAllMappingImports = test::kFakeCreateFileMapping | test::kFakeMapView |
                                             test::kFakeUnmapView | test::kFakeCloseHandle |
                                             test::kFakeGetLastError;

// One scripted CFile operation on two copies of a random CFile.
bool CompareFileOp(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    CWin32Handle a{};
    rng.Fill(&a, sizeof(a));
    a.vtable = vtables::kCFile;
    CWin32Handle b = a;
    static const char* const kNames[] = {"C:\\Games\\S3\\Gfx\\siedler3_00.f8007e01f.dat", "x", "", nullptr,
                                         "..\\Snd\\Siedler3_00.dat"};
    std::uint32_t outA[2] = {};
    rng.Fill(outA, sizeof(outA));
    std::uint32_t outB[2] = {outA[0], outA[1]};
    Call ca{};
    ca.op = static_cast<Op>(rng.Below(5));
    ca.self = &a;
    ca.name = kNames[rng.Below(5)];
    ca.a = rng.Next32();
    ca.b = rng.Next32();
    ca.c = rng.Next32();
    ca.d = rng.Next32();
    ca.p = reinterpret_cast<void*>(rng.Next32());
    const bool withOut = rng.Chance(1, 2);
    Call cb = ca;
    cb.self = &b;
    ca.q = withOut ? static_cast<void*>(outA) : nullptr;
    cb.q = withOut ? static_cast<void*>(outB) : nullptr;
    if (ca.op == Op::Open) {
        cb.q = ca.q = reinterpret_cast<void*>(rng.Next32());  // the template handle, passed on
    }
    FakeWin32Config config;
    config.failChance = rng.Below(5);
    const std::uint64_t seed = rng.Next64();
    FakeWin32 fa(seed, kAllFileImports, config);
    FakeWin32 fb(seed, kAllFileImports, config);
    const Functions originals = Originals();
    fa.Install();
    const Outcome oa = Run(&originals, &ca);
    fa.Uninstall();
    fb.Install();
    const Outcome ob = Run(&ours, &cb);
    fb.Uninstall();
    bool same = SameOutcome(ctx, oa, ob, fa, fb);
    same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  file op %d", static_cast<int>(ca.op));
    }
    if (withOut && ca.op != Op::Open) {
        same &= S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA));
    }
    return same;
}

// A pair of constructed mappings (original and ours) with random lists.
struct MappingPair {
    CFileMapping* a;
    CFileMapping* b;
    FakeFile fileA;
    FakeFile fileB;
};

// Sequences on one pair: construct, then random operations, then the
// destructor or the scalar deleting destructor.
bool CompareMappingSequence(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    auto* a = static_cast<CFileMapping*>(msvc6::GameOperatorNew(sizeof(CFileMapping)));
    auto* b = static_cast<CFileMapping*>(msvc6::GameOperatorNew(sizeof(CFileMapping)));
    rng.Fill(a, sizeof(*a));
    std::memcpy(b, a, sizeof(*a));
    Original_Win32_CFileMapping_Construct(a, nullptr);
    ours.construct(b, nullptr);
    bool same = SameMapping(ctx, a, b);
    if (rng.Chance(1, 4)) {
        a->views.grow = b->views.grow = rng.Below(3);
    }
    FakeFile fileA{kFakeFileVtable, rng.Next32(), rng.Below(2)};
    FakeFile fileB = fileA;
    std::vector<std::uint32_t> views;
    for (int i = 0; i < 20; ++i) {
        views.push_back((rng.Next32() & ~0xfffu) | 0x1000);
    }
    FakeWin32Config config;
    config.failChance = rng.Below(3);
    config.views = &views;
    const Functions originals = Originals();
    std::vector<std::uint32_t> fileLogA;
    std::vector<std::uint32_t> fileLogB;
    const int steps = 1 + static_cast<int>(rng.Below(30));
    for (int step = 0; step < steps && same; ++step) {
        Call ca{};
        const std::uint32_t kind = rng.Below(10);
        if (kind == 0) {
            ca.op = Op::Create;
            ca.p = rng.Chance(1, 4) ? nullptr : &fileA;
            ca.q = reinterpret_cast<void*>(rng.Next32());
            ca.a = rng.Next32();
            ca.b = rng.Next32();
            ca.c = rng.Next32();
            ca.name = rng.Chance(1, 2) ? nullptr : "S3Map";
        } else if (kind == 1) {
            ca.op = Op::Close;
            if (a->file == 0 && rng.Chance(1, 2)) {
                a->file = reinterpret_cast<std::uint32_t>(&fileA);
                b->file = reinterpret_cast<std::uint32_t>(&fileB);
            }
        } else if (kind < 6) {
            ca.op = Op::MapView;
            ca.a = rng.Next32();
            ca.b = rng.Next32();
            ca.c = rng.Next32();
            ca.d = rng.Next32();
        } else {
            ca.op = Op::UnmapView;
            ca.a = rng.Chance(3, 4) ? views[rng.Below(static_cast<std::uint32_t>(views.size()))] : rng.Next32();
        }
        Call cb = ca;
        ca.self = a;
        cb.self = b;
        if (ca.op == Op::Create && ca.p != nullptr) {
            cb.p = &fileB;
        }
        const std::uint64_t seed = rng.Next64();
        FakeWin32 fa(seed, kAllMappingImports, config);
        FakeWin32 fb(seed, kAllMappingImports, config);
        fa.Install();
        g_fileLog = &fileLogA;
        const Outcome oa = Run(&originals, &ca);
        fa.Uninstall();
        fb.Install();
        g_fileLog = &fileLogB;
        const Outcome ob = Run(&ours, &cb);
        fb.Uninstall();
        g_fileLog = nullptr;
        // The file pointers differ (one fake file each): compare the rest.
        const bool filesMatch = (a->file == 0) == (b->file == 0);
        same &= S3RE_EXPECT(filesMatch);
        const std::uint32_t savedA = a->file;
        const std::uint32_t savedB = b->file;
        a->file = b->file = 0;
        same &= SameMapping(ctx, a, b);
        a->file = savedA;
        b->file = savedB;
        same &= SameOutcome(ctx, oa, ob, fa, fb);
        same &= S3RE_EXPECT(fileLogA == fileLogB);
        same &= S3RE_EXPECT_MEM_EQ(&fileA, &fileB, sizeof(fileA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  step %d (op %d)", step, static_cast<int>(ca.op));
        }
    }
    // The end of the mapping: destructor (Close included) or scalar deleting
    // destructor without freeing (the test frees both blocks itself).
    const std::uint64_t seed = rng.Next64();
    FakeWin32 fa(seed, kAllMappingImports, config);
    FakeWin32 fb(seed, kAllMappingImports, config);
    const bool deleting = rng.Chance(1, 2);
    if (a->file == 0 && rng.Chance(1, 2)) {
        a->file = reinterpret_cast<std::uint32_t>(&fileA);
        b->file = reinterpret_cast<std::uint32_t>(&fileB);
    }
    const std::uint32_t dataA = a->views.data;
    const std::uint32_t dataB = b->views.data;
    fa.Install();
    g_fileLog = &fileLogA;
    const std::uint32_t ra = deleting ? reinterpret_cast<std::uint32_t>(Original_Win32_CFileMapping_DeletingDestroy(
                                            a, nullptr, 0))
                                      : 0;
    if (!deleting) {
        Original_Win32_CFileMapping_Destroy(a, nullptr);
    }
    fa.Uninstall();
    fb.Install();
    g_fileLog = &fileLogB;
    const std::uint32_t rb = deleting ? reinterpret_cast<std::uint32_t>(ours.deleting(b, nullptr, 0)) : 0;
    if (!deleting) {
        ours.destroy(b, nullptr);
    }
    fb.Uninstall();
    g_fileLog = nullptr;
    same &= S3RE_EXPECT_EQ(ra == reinterpret_cast<std::uint32_t>(a), rb == reinterpret_cast<std::uint32_t>(b));
    same &= S3RE_EXPECT(fa.log_ == fb.log_);
    same &= S3RE_EXPECT(fileLogA == fileLogB);
    same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(*a));  // the data pointers are 0 now
    (void)dataA;
    (void)dataB;
    msvc6::GameOperatorDelete(a);
    msvc6::GameOperatorDelete(b);
    return same;
}

// DetachMemory on a scripted walk over a private buffer.
bool CompareDetachScripted(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    static void* const area = VirtualAlloc(nullptr, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const std::uint32_t low = reinterpret_cast<std::uint32_t>(area);
    FakeWin32Config config;
    config.regionLow = low;
    config.regionHigh = low + 0x20000;
    config.failChance = rng.Below(3);
    Call call{};
    call.op = Op::Detach;
    call.a = low + rng.Below(0x10000);
    call.b = rng.Chance(1, 8) ? 0 : rng.Below(0x10000);
    const std::uint32_t pageSize = rng.Chance(1, 2) ? 0 : 0x1000 << rng.Below(2);
    const std::uint32_t savedPageSize = g_detachPageSize.get();
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t imports = test::kFakeGetSystemInfo | test::kFakeVirtualQuery | test::kFakeVirtualProtect |
                                  test::kFakeGetLastError;
    FakeWin32 fa(seed, imports, config);
    FakeWin32 fb(seed, imports, config);
    const Functions originals = Originals();
    g_detachPageSize.get() = pageSize;
    fa.Install();
    const Outcome oa = Run(&originals, &call);
    fa.Uninstall();
    const std::uint32_t pageA = g_detachPageSize.get();
    g_detachPageSize.get() = pageSize;
    fb.Install();
    const Outcome ob = Run(&ours, &call);
    fb.Uninstall();
    const std::uint32_t pageB = g_detachPageSize.get();
    g_detachPageSize.get() = savedPageSize;
    bool same = SameOutcome(ctx, oa, ob, fa, fb);
    same &= S3RE_EXPECT_EQ(pageA, pageB);
    return same;
}

void CompareScripted(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        bool same;
        switch (i % 4) {
        case 0:
        case 1: same = CompareFileOp(ctx, ours); break;
        case 2: same = CompareMappingSequence(ctx, ours); break;
        default: same = CompareDetachScripted(ctx, ours); break;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Real files
// ---------------------------------------------------------------------------

std::string TempPath(const char* name) {
    char folder[MAX_PATH];
    GetTempPathA(MAX_PATH, folder);
    return std::string(folder) + name;
}

void WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD done = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr);
    CloseHandle(file);
}

// The real file chain of the graphics loader on one side.
struct RealRun {
    Outcome open;
    std::uint32_t size = 0;
    std::uint32_t high = 0;
    Outcome sizeHigh[2];
    Outcome read;
    std::uint32_t readDone = 0;
    std::vector<std::uint8_t> readBytes;
    std::uint32_t seek = 0;
    std::uint32_t view = 0;
    std::vector<std::uint8_t> viewBytes;
    std::uint32_t unmap = 0;
    std::uint32_t listLength = 0;
};

RealRun RunReal(const Functions& f, const std::string& path, std::uint32_t protect, std::uint32_t access,
                std::uint32_t length) {
    RealRun run;
    void* const file = msvc6::GameOperatorNew(0xc);
    reinterpret_cast<CFileConstructFn>(kCFileConstruct)(file, nullptr);
    Call open{};
    open.op = Op::Open;
    open.self = file;
    open.name = path.c_str();
    open.a = GENERIC_READ;
    open.b = FILE_SHARE_READ;
    open.c = OPEN_EXISTING;
    open.d = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
    run.open = Run(&f, &open);
    if (run.open.caught.threw) {
        msvc6::GameOperatorDelete(file);
        return run;
    }
    run.open.eax = run.open.eax != 0;  // handles differ
    run.size = f.getSize(file, nullptr, nullptr);
    // With a high dword: the original's inverted check throws when the
    // last error is 0 (and passes otherwise).
    Call size{};
    size.op = Op::GetSize;
    size.self = file;
    size.q = &run.high;
    SetLastError(0);
    run.sizeHigh[0] = Run(&f, &size);
    SetLastError(ERROR_ALREADY_EXISTS);
    run.sizeHigh[1] = Run(&f, &size);
    run.readBytes.resize(64);
    Call read{};
    read.op = Op::Read;
    read.self = file;
    read.p = run.readBytes.data();
    read.a = 64;
    read.q = &run.readDone;
    run.read = Run(&f, &read);
    run.seek = f.seek(file, nullptr, 3, nullptr, FILE_BEGIN);
    auto* mapping = static_cast<CFileMapping*>(msvc6::GameOperatorNew(sizeof(CFileMapping)));
    f.construct(mapping, nullptr);
    f.create(mapping, nullptr, file, nullptr, protect, 0, 0, nullptr);
    run.view = f.mapView(mapping, nullptr, access, 0, 0, 0);
    run.viewBytes.assign(reinterpret_cast<const std::uint8_t*>(run.view),
                         reinterpret_cast<const std::uint8_t*>(run.view) + length);
    run.listLength = static_cast<std::uint32_t>(mapping->views.length);
    run.unmap = f.unmapView(mapping, nullptr, run.view);
    run.view = run.view != 0;
    f.deleting(mapping, nullptr, 1);  // closes the mapping, deletes the file
    return run;
}

bool SameReal(test::Context& ctx, const RealRun& a, const RealRun& b) {
    bool same = S3RE_EXPECT_EQ(a.open.eax, b.open.eax);
    same &= S3RE_EXPECT(a.open.caught == b.open.caught);
    same &= S3RE_EXPECT_EQ(a.size, b.size);
    same &= S3RE_EXPECT_EQ(a.high, b.high);
    for (int i = 0; i < 2; ++i) {
        same &= S3RE_EXPECT_EQ(a.sizeHigh[i].eax, b.sizeHigh[i].eax);
        same &= S3RE_EXPECT(a.sizeHigh[i].caught == b.sizeHigh[i].caught);
    }
    same &= S3RE_EXPECT_EQ(a.read.eax, b.read.eax);
    same &= S3RE_EXPECT_EQ(a.readDone, b.readDone);
    same &= S3RE_EXPECT(a.readBytes == b.readBytes);
    same &= S3RE_EXPECT_EQ(a.seek, b.seek);
    same &= S3RE_EXPECT_EQ(a.view, b.view);
    same &= S3RE_EXPECT(a.viewBytes == b.viewBytes);
    same &= S3RE_EXPECT_EQ(a.unmap, b.unmap);
    same &= S3RE_EXPECT_EQ(a.listLength, b.listLength);
    return same;
}

void CompareRealFiles(test::Context& ctx, const Functions& ours) {
    const std::string path = TempPath("s3re_mapping_test.dat");
    std::vector<std::uint8_t> bytes(0x5321);
    ctx.rng().Fill(bytes.data(), bytes.size());
    WriteWholeFile(path, bytes);
    const Functions originals = Originals();
    struct Mode {
        std::uint32_t protect;
        std::uint32_t access;
    };
    const Mode modes[] = {{PAGE_READONLY, FILE_MAP_READ}, {PAGE_WRITECOPY, FILE_MAP_COPY}};
    for (const Mode& mode : modes) {
        const RealRun a = RunReal(originals, path, mode.protect, mode.access, 0x5321);
        const RealRun b = RunReal(ours, path, mode.protect, mode.access, 0x5321);
        if (!SameReal(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  protect %#x", mode.protect);
        }
        S3RE_EXPECT(a.viewBytes == bytes);
        S3RE_EXPECT(a.sizeHigh[0].caught.threw && !a.sizeHigh[1].caught.threw);
    }
    // A missing file (CFileNotFound) and a folder (access denied: CHResult).
    const std::string missing = TempPath("s3re_no_such_folder\\none.dat");
    char folder[MAX_PATH];
    GetTempPathA(MAX_PATH, folder);
    for (const char* name : {missing.c_str(), static_cast<const char*>(folder)}) {
        const RealRun a = RunReal(originals, name, PAGE_READONLY, FILE_MAP_READ, 0);
        const RealRun b = RunReal(ours, name, PAGE_READONLY, FILE_MAP_READ, 0);
        S3RE_EXPECT(a.open.caught.threw);
        SameReal(ctx, a, b);
    }
    DeleteFileA(path.c_str());
}

// DetachMemory on real copy-on-write views.
void CompareRealDetach(test::Context& ctx, const Functions& ours) {
    const std::string path = TempPath("s3re_detach_test.dat");
    std::vector<std::uint8_t> bytes(0x6000);
    ctx.rng().Fill(bytes.data(), bytes.size());
    WriteWholeFile(path, bytes);
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_WRITECOPY, 0, 0, nullptr);
    auto* viewA = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 0));
    auto* viewB = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 0));
    auto* viewC = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 0));
    const std::uint32_t savedPageSize = g_detachPageSize.get();
    g_detachPageSize.get() = 0;
    const std::uint32_t ra = Original_Win32_DetachMemory(reinterpret_cast<std::uint32_t>(viewA), 0x6000);
    const std::uint32_t pageA = g_detachPageSize.get();
    g_detachPageSize.get() = 0;
    const std::uint32_t rb = ours.detach(reinterpret_cast<std::uint32_t>(viewB), 0x6000);
    const std::uint32_t pageB = g_detachPageSize.get();
    g_detachPageSize.get() = savedPageSize;
    S3RE_EXPECT_EQ(ra, rb);
    S3RE_EXPECT_EQ(pageA, pageB);
    MEMORY_BASIC_INFORMATION infoA{};
    MEMORY_BASIC_INFORMATION infoB{};
    VirtualQuery(viewA, &infoA, sizeof(infoA));
    VirtualQuery(viewB, &infoB, sizeof(infoB));
    S3RE_EXPECT_EQ(infoA.Protect, infoB.Protect);
    S3RE_EXPECT_EQ(infoA.RegionSize, infoB.RegionSize);
    // Rewrite the file: the detached views keep their copies.
    std::vector<std::uint8_t> other(bytes.size(), 0x5a);
    DWORD done = 0;
    SetFilePointer(file, 0, nullptr, FILE_BEGIN);
    WriteFile(file, other.data(), static_cast<DWORD>(other.size()), &done, nullptr);
    FlushFileBuffers(file);
    S3RE_EXPECT(std::memcmp(viewA, bytes.data(), bytes.size()) == 0);
    S3RE_EXPECT(std::memcmp(viewB, bytes.data(), bytes.size()) == 0);
    S3RE_EXPECT(std::memcmp(viewC, other.data(), other.size()) == 0);  // the control
    UnmapViewOfFile(viewA);
    UnmapViewOfFile(viewB);
    UnmapViewOfFile(viewC);
    CloseHandle(mapping);
    CloseHandle(file);
    DeleteFileA(path.c_str());
}

// ---------------------------------------------------------------------------
// Wrong implementations
// ---------------------------------------------------------------------------

// Wrong: GetSize with a high dword uses the intended check instead of the
// original's inverted one.
std::uint32_t __fastcall WrongGetSize(void* self, void* edx, std::uint32_t* high) {
    if (high == nullptr) {
        return Win32_CFile_GetSize(self, edx, high);
    }
    return g_importGetFileSize.get()(reinterpret_cast<void*>(static_cast<CWin32Handle*>(self)->handle), high);
}

// Wrong: UnmapView doesn't shrink the list.
std::uint32_t __fastcall WrongUnmapView(void* /*self*/, void* /*edx*/, std::uint32_t view) {
    g_importUnmapViewOfFile.get()(reinterpret_cast<const void*>(view));
    return 0;
}

// Wrong: DetachMemory skips copy-on-write regions.
std::uint32_t __fastcall WrongDetach(std::uint32_t start, std::uint32_t size) {
    MEMORY_BASIC_INFORMATION region;
    if (size != 0 && g_importVirtualQuery.get()(reinterpret_cast<const void*>(start), &region, sizeof(region)) ==
                         sizeof(region) &&
        region.Protect == PAGE_WRITECOPY) {
        return 0;
    }
    return Win32_DetachMemory(start, size);
}

}  // namespace

S3RE_TEST(Win32Mapping_MatchOriginal) {
    const Functions ours = Ours();
    CompareScripted(ctx, ours, 4000);
    CompareRealFiles(ctx, ours);
    CompareRealDetach(ctx, ours);
}

S3RE_TEST(Win32Mapping_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.getSize = &WrongGetSize;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe, wrong, 800);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ours();
    wrong.unmapView = &WrongUnmapView;
    test::Context probe2(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe2, wrong, 800);
    S3RE_EXPECT(probe2.failures() > 0);
    wrong = Ours();
    wrong.detach = &WrongDetach;
    test::Context probe3(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe3, wrong, 800);
    S3RE_EXPECT(probe3.failures() > 0);
}

}  // namespace s3re::game
