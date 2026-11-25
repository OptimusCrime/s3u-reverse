// The graphics data files (Dirk\graphicsdata.cpp): closing them and the
// removable-drive check of the "detach" option. Formats and the load call
// tree: re/graphics_formats.md.
//
// CGraphicsData::Close (0x5fa690): thiscall, ret. Only if group 0 has a
//   mapped view: for every one of the 70 groups with a view, UnmapView
//   (0x61aa40, thiscall on the group's Win32::CFileMapping, the view) and
//   the mapping's virtual slot 1 (+4, probably its close); then (for every
//   group, mapped or not) the view and the eight table pointers are zeroed.
//   Always: languageCount and language = 0. So with group 0 not loaded
//   nothing is unmapped (kept). The mapping objects stay (the destructor
//   deletes them).
// IsPathOnRemovableDrive (0x5fa750): thiscall (path (std::string*),
//   otherwise), ret 8; this is unused. GetDriveTypeA (import 0x6550a4) of
//   the root made of the path's first three characters (each read from the
//   empty-string buffer 0x6553a0 when the string has no buffer; no length
//   check); DRIVE_REMOVABLE (2) or DRIVE_CDROM (5) give AL = 1, anything
//   else AL = the low byte of `otherwise`; the rest of EAX is the drive
//   type's (0 for every type Windows returns).

#include "game/globals.h"
#include "game/graphics/gfx_layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

namespace {

using gfx::CGraphicsData;

constexpr std::uintptr_t kUnmapView = 0x0061aa40;
// Win32::CFileMapping's virtual slot 1.
constexpr std::uint32_t kMappingCloseSlot = 1;

constexpr std::uint32_t kDriveRemovable = 2;  // DRIVE_REMOVABLE
constexpr std::uint32_t kDriveCdrom = 5;      // DRIVE_CDROM

using UnmapViewFn = void(__fastcall*)(void* mapping, void* edx, std::uint32_t view);
using VirtualFn = void(__fastcall*)(void* self, void* edx);

}  // namespace

void __fastcall CGraphicsData_Close(void* self, void* edx);
std::uint32_t __fastcall IsPathOnRemovableDrive(void* self, void* edx, const msvc6::String* path,
                                                std::uint32_t otherwise);

using CloseFn = void(__fastcall*)(void* self, void* edx);
using RemovableFn = std::uint32_t(__fastcall*)(void* self, void* edx, const msvc6::String* path,
                                               std::uint32_t otherwise);

S3RE_HOOK(0x005fa690, CGraphicsData_Close, CloseFn, Original_CGraphicsData_Close, CGraphicsData_Close);

// Purpose: unmaps the graphics files (when the drawing surfaces are
// released or the game ends) and forgets every table.
void __fastcall CGraphicsData_Close(void* self, void* /*edx*/) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    if (gfx->views[0] != 0) {
        for (std::size_t group = 0; group < gfx::kGroups; ++group) {
            if (gfx->views[group] != 0) {
                void* const mapping = reinterpret_cast<void*>(gfx->mappings[group]);
                reinterpret_cast<UnmapViewFn>(kUnmapView)(mapping, nullptr, gfx->views[group]);
                // Read again: the mapping pointer after the call, as the original.
                void* const again = reinterpret_cast<void*>(gfx->mappings[group]);
                const auto* const vtable = *static_cast<const std::uint32_t* const*>(again);
                reinterpret_cast<VirtualFn>(vtable[kMappingCloseSlot])(again, nullptr);
            }
            gfx->views[group] = 0;
            gfx->textTables[group] = nullptr;
            gfx->animationTables[group] = nullptr;
            gfx->colorIndexedTables[group] = nullptr;
            gfx->shadowTables[group] = nullptr;
            gfx->paletteTables[group] = nullptr;
            gfx->guiImageTables[group] = nullptr;
            gfx->landscapeTextureTables[group] = nullptr;
            gfx->sequenceTables[group] = nullptr;
        }
    }
    gfx->languageCount = 0;
    gfx->language = 0;
}

S3RE_HOOK(0x005fa750, IsPathOnRemovableDrive, RemovableFn, Original_IsPathOnRemovableDrive, IsPathOnRemovableDrive);

// Purpose: whether the graphics folder is on a removable drive or CD (the
// "detach" option then copies the mapped files into memory, so the CD can
// be taken out).
std::uint32_t __fastcall IsPathOnRemovableDrive(void* /*self*/, void* /*edx*/, const msvc6::String* path,
                                                std::uint32_t otherwise) {
    const char* const text = path->ptr;
    const char* const empty = &g_stringNullBuffer[0];
    char root[4];
    root[0] = text != nullptr ? text[0] : empty[0];
    root[1] = text != nullptr ? text[1] : empty[0];
    root[2] = text != nullptr ? text[2] : empty[0];
    root[3] = 0;
    const std::uint32_t type = g_importGetDriveTypeA.get()(root);
    const std::uint32_t result = type == kDriveRemovable || type == kDriveCdrom ? 1 : (otherwise & 0xff);
    return (type & 0xffffff00u) | result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Close: two copies of a CGraphicsData whose groups are mapped or not
// (group 0 too), with fake mapping objects (UnmapView's own bookkeeping
// sees an empty view list; UnmapViewOfFile fails harmlessly on the fake
// views) whose virtual slot 1 records its calls; the objects, the calls
// and the order are compared. IsPathOnRemovableDrive: random paths (real
// drive letters, odd strings, null buffers), first with the real
// GetDriveTypeA, then with a fake import returning every drive type, the
// root strings it saw compared.

namespace {

struct FakeMapping {
    const std::uint32_t* vtable;
    std::uint8_t unk04[0x24 - 0x04];
    std::uint32_t viewCount;  // +0x24 UnmapView's list count: 0 = nothing to remove
    std::uint8_t unk28[0x2c - 0x28];
};
static_assert(offsetof(FakeMapping, viewCount) == 0x24);

std::vector<std::uint32_t>* g_closeLog = nullptr;

void __fastcall RecordClose(void* self, void* /*edx*/) {
    if (g_closeLog != nullptr) {
        g_closeLog->push_back(reinterpret_cast<std::uint32_t>(self));
    }
}

const std::uint32_t kFakeMappingVtable[2] = {0, reinterpret_cast<std::uint32_t>(&RecordClose)};

std::uint32_t FakeViewArea() {
    static void* const area = VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return reinterpret_cast<std::uint32_t>(area);
}

bool CompareOneClose(test::Context& ctx, CloseFn ours) {
    static CGraphicsData a;
    static CGraphicsData b;
    static FakeMapping mappings[gfx::kGroups];
    test::Rng& rng = ctx.rng();
    rng.Fill(&a, sizeof(a));
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        mappings[g] = FakeMapping{};
        mappings[g].vtable = kFakeMappingVtable;
        a.mappings[g] = reinterpret_cast<std::uint32_t>(&mappings[g]);
        const bool mapped = g == 0 ? rng.Chance(3, 4) : rng.Chance(1, 2);
        // Fake views inside private memory, which UnmapViewOfFile rejects
        // (an address inside a real view or image would unmap it).
        a.views[g] = mapped ? FakeViewArea() + rng.Below(0x1000) + 1 : 0;
    }
    std::memcpy(&b, &a, sizeof(a));
    std::vector<std::uint32_t> logA;
    std::vector<std::uint32_t> logB;
    g_closeLog = &logA;
    Original_CGraphicsData_Close(&a, nullptr);
    g_closeLog = &logB;
    ours(&b, nullptr);
    g_closeLog = nullptr;
    bool same = S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
    same &= S3RE_EXPECT(logA == logB);
    return same;
}

std::uint32_t g_fakeDriveType = 0;
std::vector<std::string>* g_driveLog = nullptr;

std::uint32_t __stdcall FakeGetDriveType(const char* root) {
    if (g_driveLog != nullptr) {
        g_driveLog->push_back(root);
    }
    return g_fakeDriveType;
}

bool CompareOneRemovable(test::Context& ctx, RemovableFn ours, bool fake) {
    test::Rng& rng = ctx.rng();
    static const char* const kPaths[] = {"C:\\Games\\S3", "c:", "D:\\", "Z:\\gfx", "\\\\server\\share", "",
                                         "A:", "X", ".\\Gfx", "..\\"};
    char buffer[16];
    msvc6::String path{};
    if (rng.Chance(1, 6)) {
        path.ptr = nullptr;
    } else if (rng.Chance(1, 4)) {
        rng.Fill(buffer, sizeof(buffer));
        buffer[rng.Below(4)] = 0;
        path.ptr = buffer;
    } else {
        const char* const chosen = kPaths[rng.Below(sizeof(kPaths) / sizeof(kPaths[0]))];
        std::memcpy(buffer, chosen, std::strlen(chosen) + 1);
        path.ptr = buffer;
    }
    const std::uint32_t otherwise = rng.Next32();
    g_fakeDriveType = rng.Chance(1, 8) ? rng.Next32() : rng.Below(7);
    if (fake) {
        const GetDriveTypeAImport fakeImport = &FakeGetDriveType;
        test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importGetDriveTypeA.get()), &fakeImport,
                         sizeof(fakeImport));
    }
    std::vector<std::string> rootsA;
    std::vector<std::string> rootsB;
    g_driveLog = &rootsA;
    const std::uint32_t ra = Original_IsPathOnRemovableDrive(nullptr, nullptr, &path, otherwise);
    g_driveLog = &rootsB;
    const std::uint32_t rb = ours(nullptr, nullptr, &path, otherwise);
    g_driveLog = nullptr;
    if (fake) {
        test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importGetDriveTypeA.get()));
    }
    bool same = S3RE_EXPECT_EQ(ra, rb);
    same &= S3RE_EXPECT(rootsA == rootsB);
    return same;
}

// Wrong: CD drives don't count.
std::uint32_t __fastcall WrongRemovable(void* self, void* edx, const msvc6::String* path, std::uint32_t otherwise) {
    const std::uint32_t result = IsPathOnRemovableDrive(self, edx, path, otherwise);
    return g_fakeDriveType == kDriveCdrom ? (otherwise & 0xff) : result;
}

// Wrong: unmaps even when group 0 isn't loaded.
void __fastcall WrongClose(void* self, void* edx) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    const std::uint32_t saved = gfx->views[0];
    if (saved == 0) {
        gfx->views[0] = 1;
        CGraphicsData_Close(self, edx);
        return;
    }
    CGraphicsData_Close(self, edx);
}

void CompareFiles(test::Context& ctx, CloseFn close, RemovableFn removable, int cases) {
    for (int i = 0; i < cases; ++i) {
        bool same;
        if (i % 3 == 0) {
            same = CompareOneClose(ctx, close);
        } else {
            same = CompareOneRemovable(ctx, removable, i % 3 == 2);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
}

}  // namespace

S3RE_TEST(GfxFiles_MatchOriginal) {
    CompareFiles(ctx, CGraphicsData_Close, IsPathOnRemovableDrive, 3000);
}

S3RE_TEST(GfxFiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFiles(probe, CGraphicsData_Close, WrongRemovable, 600);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 1, /*quiet=*/true);
    CompareFiles(probe2, WrongClose, IsPathOnRemovableDrive, 600);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
