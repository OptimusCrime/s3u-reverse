#include "test/fake_win32.h"

#include "game/vtables.h"
#include "msvc6/catch_match.h"

namespace s3re::test {

namespace {

using namespace s3re::game;

FakeWin32* g_active = nullptr;

enum CallId : std::uint32_t {
    kCallCreateFile = 1,
    kCallGetLastError,
    kCallReadFile,
    kCallWriteFile,
    kCallSetFilePointer,
    kCallGetFileSize,
    kCallCreateFileMapping,
    kCallMapView,
    kCallUnmapView,
    kCallCloseHandle,
    kCallGetSystemInfo,
    kCallVirtualQuery,
    kCallVirtualProtect,
    kCallGetCurrentDirectory,
    kCallSetCurrentDirectory,
    kCallFindFirstFile,
    kCallFindClose,
    kCallGetDriveType,
};

// Strings are logged by content and out-parameters by whether they are
// given (their buffers differ between the runs).
std::uint32_t Hash(const char* text) {
    if (text == nullptr) {
        return 0xffffffff;
    }
    std::uint32_t hash = 2166136261u;
    for (; *text != 0; ++text) {
        hash = (hash ^ static_cast<std::uint8_t>(*text)) * 16777619u;
    }
    return hash;
}

FakeWin32& Active() {
    return *g_active;
}

void Log(std::initializer_list<std::uint32_t> values) {
    Active().log_.insert(Active().log_.end(), values);
}

std::uint32_t U(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

bool Fails() {
    return Active().rng_.Below(8) < Active().config_.failChance;
}

// A result that is never -1.
std::uint32_t Value() {
    const std::uint32_t v = Active().rng_.Next32();
    return v == 0xffffffff ? 0 : v;
}

void* __stdcall FakeCreateFile(const char* name, std::uint32_t access, std::uint32_t share, void* security,
                               std::uint32_t disposition, std::uint32_t flags, void* templateFile) {
    Log({kCallCreateFile, Hash(name), access, share, U(security), disposition, flags, U(templateFile)});
    return reinterpret_cast<void*>(Fails() ? 0xffffffff : Value());
}

std::uint32_t __stdcall FakeGetLastError() {
    Log({kCallGetLastError});
    static const std::uint32_t kErrors[] = {0, 2, 3, 5, 87};
    Rng& rng = Active().rng_;
    return rng.Chance(1, 6) ? rng.Next32() : kErrors[rng.Below(5)];
}

int __stdcall FakeReadFile(void* file, void* buffer, std::uint32_t size, std::uint32_t* done, void* overlapped) {
    Log({kCallReadFile, U(file), U(buffer), size, done != nullptr, U(overlapped)});
    if (done != nullptr) {
        *done = Active().rng_.Next32();
    }
    return Fails() ? 0 : static_cast<int>(Value() | 1);
}

int __stdcall FakeWriteFile(void* file, const void* buffer, std::uint32_t size, std::uint32_t* done,
                            void* overlapped) {
    Log({kCallWriteFile, U(file), U(buffer), size, done != nullptr, U(overlapped)});
    if (done != nullptr) {
        *done = Active().rng_.Next32();
    }
    return Fails() ? 0 : static_cast<int>(Value() | 1);
}

std::uint32_t __stdcall FakeSetFilePointer(void* file, std::int32_t low, std::int32_t* high, std::uint32_t method) {
    Log({kCallSetFilePointer, U(file), static_cast<std::uint32_t>(low), high != nullptr,
         high != nullptr ? static_cast<std::uint32_t>(*high) : 0, method});
    if (high != nullptr) {
        *high = static_cast<std::int32_t>(Active().rng_.Next32());
    }
    return Fails() ? 0xffffffff : Value();
}

std::uint32_t __stdcall FakeGetFileSize(void* file, std::uint32_t* high) {
    Log({kCallGetFileSize, U(file), high != nullptr});
    if (high != nullptr) {
        *high = Active().rng_.Next32();
    }
    return Fails() ? 0xffffffff : Value();
}

void* __stdcall FakeCreateFileMapping(void* file, void* security, std::uint32_t protect, std::uint32_t sizeHigh,
                                      std::uint32_t sizeLow, const char* name) {
    Log({kCallCreateFileMapping, U(file), U(security), protect, sizeHigh, sizeLow, Hash(name)});
    return reinterpret_cast<void*>(Fails() ? 0 : Value() | 1);
}

void* __stdcall FakeMapView(void* mapping, std::uint32_t access, std::uint32_t offsetHigh, std::uint32_t offsetLow,
                            std::uint32_t size) {
    Log({kCallMapView, U(mapping), access, offsetHigh, offsetLow, size});
    FakeWin32& fake = Active();
    if (Fails()) {
        return nullptr;
    }
    if (fake.config_.views != nullptr && !fake.config_.views->empty()) {
        const std::uint32_t view = (*fake.config_.views)[fake.nextView_ % fake.config_.views->size()];
        ++fake.nextView_;
        return reinterpret_cast<void*>(view);
    }
    return reinterpret_cast<void*>(Value() | 1);
}

int __stdcall FakeUnmapView(const void* view) {
    Log({kCallUnmapView, U(view)});
    return static_cast<int>(Active().rng_.Next32());
}

int __stdcall FakeCloseHandle(void* handle) {
    Log({kCallCloseHandle, U(handle)});
    return static_cast<int>(Active().rng_.Next32());
}

void __stdcall FakeGetSystemInfo(void* info) {
    Log({kCallGetSystemInfo});
    SYSTEM_INFO* const system = static_cast<SYSTEM_INFO*>(info);
    Active().rng_.Fill(system, sizeof(*system));
    static const std::uint32_t kPageSizes[] = {0x1000, 0x2000, 0x800};
    system->dwPageSize = kPageSizes[Active().rng_.Below(3)];
}

std::uint32_t __stdcall FakeVirtualQuery(const void* address, void* info, std::uint32_t size) {
    Log({kCallVirtualQuery, U(address), size});
    FakeWin32& fake = Active();
    Rng& rng = fake.rng_;
    MEMORY_BASIC_INFORMATION* const region = static_cast<MEMORY_BASIC_INFORMATION*>(info);
    rng.Fill(region, sizeof(*region));
    // A committed region around the address, inside the configured area.
    const std::uint32_t page = U(address) & ~0xfffu;
    std::uint32_t base = page - rng.Below(3) * 0x1000;
    if (base < fake.config_.regionLow) {
        base = fake.config_.regionLow;
    }
    const std::uint32_t maxSize = fake.config_.regionHigh - base;
    region->BaseAddress = reinterpret_cast<void*>(base);
    // The region always reaches past the queried page (the walk advances).
    const std::uint32_t below = page - base;
    const std::uint32_t pagesAfter = (maxSize - below) / 0x1000;
    region->RegionSize = rng.Chance(1, 4) ? maxSize + rng.Below(0x3000)
                                          : below + (rng.Below(pagesAfter > 0 ? pagesAfter : 1) + 1) * 0x1000;
    region->State = MEM_COMMIT;
    static const std::uint32_t kProtects[] = {PAGE_READONLY,         PAGE_READWRITE,         PAGE_WRITECOPY,
                                              PAGE_EXECUTE_WRITECOPY, PAGE_NOACCESS,          PAGE_GUARD | PAGE_READWRITE,
                                              PAGE_EXECUTE_READ,      PAGE_EXECUTE_READWRITE, PAGE_READONLY | PAGE_NOCACHE};
    region->Protect = kProtects[rng.Below(sizeof(kProtects) / sizeof(kProtects[0]))];
    fake.lastProtect_ = region->Protect;
    if (rng.Below(16) < fake.config_.failChance) {
        return rng.Below(28);
    }
    return sizeof(MEMORY_BASIC_INFORMATION);
}

int __stdcall FakeVirtualProtect(void* address, std::uint32_t size, std::uint32_t protect, std::uint32_t* old) {
    Log({kCallVirtualProtect, U(address), size, protect});
    FakeWin32& fake = Active();
    *old = fake.rng_.Chance(1, 8) ? fake.rng_.Next32() : fake.lastProtect_;
    return fake.rng_.Below(16) < fake.config_.failChance ? 0 : 1;
}

std::uint32_t __stdcall FakeGetCurrentDirectory(std::uint32_t size, char* buffer) {
    Log({kCallGetCurrentDirectory, size});
    if (Fails()) {
        return 0;
    }
    const char* const text = Active().config_.currentDirectory;
    std::size_t i = 0;
    for (; i + 1 < size && text[i] != 0; ++i) {
        buffer[i] = text[i];
    }
    if (size != 0) {
        buffer[i] = 0;
    }
    return static_cast<std::uint32_t>(std::strlen(text));
}

int __stdcall FakeSetCurrentDirectory(const char* path) {
    Log({kCallSetCurrentDirectory, Hash(path)});
    return Fails() ? 0 : 1;
}

void* __stdcall FakeFindFirstFile(const char* name, void* data) {
    Log({kCallFindFirstFile, Hash(name)});
    Active().rng_.Fill(data, sizeof(WIN32_FIND_DATAA));
    return reinterpret_cast<void*>(Fails() ? 0xffffffff : Value());
}

int __stdcall FakeFindClose(void* find) {
    Log({kCallFindClose, U(find)});
    return Fails() ? 0 : 1;
}

std::uint32_t __stdcall FakeGetDriveType(const char* root) {
    Log({kCallGetDriveType, Hash(root)});
    return Active().config_.driveType;
}

struct Slot {
    std::uint32_t bit;
    std::uintptr_t address;
    const void* fake;
};

const Slot kSlots[] = {
    {kFakeCreateFile, g_importCreateFileA.address(), reinterpret_cast<const void*>(&FakeCreateFile)},
    {kFakeGetLastError, g_importGetLastError.address(), reinterpret_cast<const void*>(&FakeGetLastError)},
    {kFakeReadFile, g_importReadFile.address(), reinterpret_cast<const void*>(&FakeReadFile)},
    {kFakeWriteFile, g_importWriteFile.address(), reinterpret_cast<const void*>(&FakeWriteFile)},
    {kFakeSetFilePointer, g_importSetFilePointer.address(), reinterpret_cast<const void*>(&FakeSetFilePointer)},
    {kFakeGetFileSize, g_importGetFileSize.address(), reinterpret_cast<const void*>(&FakeGetFileSize)},
    {kFakeCreateFileMapping, g_importCreateFileMappingA.address(),
     reinterpret_cast<const void*>(&FakeCreateFileMapping)},
    {kFakeMapView, g_importMapViewOfFile.address(), reinterpret_cast<const void*>(&FakeMapView)},
    {kFakeUnmapView, g_importUnmapViewOfFile.address(), reinterpret_cast<const void*>(&FakeUnmapView)},
    {kFakeCloseHandle, g_importCloseHandle.address(), reinterpret_cast<const void*>(&FakeCloseHandle)},
    {kFakeGetSystemInfo, g_importGetSystemInfo.address(), reinterpret_cast<const void*>(&FakeGetSystemInfo)},
    {kFakeVirtualQuery, g_importVirtualQuery.address(), reinterpret_cast<const void*>(&FakeVirtualQuery)},
    {kFakeVirtualProtect, g_importVirtualProtect.address(), reinterpret_cast<const void*>(&FakeVirtualProtect)},
    {kFakeGetCurrentDirectory, g_importGetCurrentDirectoryA.address(),
     reinterpret_cast<const void*>(&FakeGetCurrentDirectory)},
    {kFakeSetCurrentDirectory, g_importSetCurrentDirectoryA.address(),
     reinterpret_cast<const void*>(&FakeSetCurrentDirectory)},
    {kFakeFindFirstFile, g_importFindFirstFileA.address(), reinterpret_cast<const void*>(&FakeFindFirstFile)},
    {kFakeFindClose, g_importFindClose.address(), reinterpret_cast<const void*>(&FakeFindClose)},
    {kFakeGetDriveType, g_importGetDriveTypeA.address(), reinterpret_cast<const void*>(&FakeGetDriveType)},
};

}  // namespace

void FakeWin32::Install() {
    g_active = this;
    for (const Slot& slot : kSlots) {
        if ((imports_ & slot.bit) != 0) {
            PatchBytes(slot.address, &slot.fake, sizeof(slot.fake));
        }
    }
}

void FakeWin32::Uninstall() {
    for (const Slot& slot : kSlots) {
        if ((imports_ & slot.bit) != 0) {
            UnpatchBytes(slot.address);
        }
    }
    g_active = nullptr;
}

int CatchGameException(const EXCEPTION_POINTERS* info, CaughtException* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    out->throwInfo = static_cast<std::uint32_t>(record->ExceptionInformation[2]);
    const auto* slot = reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    void* const object = slot != nullptr ? *slot : nullptr;
    out->nullObject = object == nullptr;
    out->vtable = object != nullptr ? *static_cast<const std::uint32_t*>(object) : 0;
    const msvc6::CatchMatch match =
        msvc6::MatchPointerCatch(record, type_descriptor::kCHResultPointer, ".PAVCHResult@Win32@@");
    if (match.matched && match.object != nullptr) {
        out->isHResult = true;
        using GetCodeFn = std::uint32_t(__fastcall*)(void* self, void* edx);
        out->code = reinterpret_cast<GetCodeFn>(0x00613900)(match.object, nullptr);  // CHResult code (+0x18)
    }
    msvc6::DeleteCaughtException(object);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace s3re::test
