#include "test/fake_ddraw.h"

#include "game/globals.h"
#include "test/callee_stub.h"

#include <windows.h>

#include <cstring>
#include <utility>

namespace s3re::test {

namespace {

using namespace s3re::game;

FakeDDraw* g_active = nullptr;

constexpr std::uint32_t kLogTag = 0xdd000000;
constexpr std::uint32_t kDirectDrawId = 0x100;
constexpr std::uint32_t kSurfaceIdBase = 0x200;
constexpr std::uint32_t kPaletteIdBase = 0x300;
constexpr std::uint32_t kGameIdBase = 0x380;
constexpr std::uint32_t kImportTag = 0xdf000000;
constexpr std::uint32_t kFakeModule = 0x7ffd0000;
// Lock hands out this buffer as the surface memory.
std::uint8_t g_lockBuffer[0x1000];

FakeDDrawObject g_directDraw{nullptr, kDirectDrawId};
FakeDDrawObject g_surfaces[kFakeDDrawSurfaces];
FakeDDrawObject g_palettes[kFakeDDrawPalettes];
FakeDDrawGameObject g_games[kFakeDDrawGameObjects];
std::uint32_t g_gameSlotArgs[16] = {};

FakeDDraw& Active() {
    return *g_active;
}

void Log(std::initializer_list<std::uint32_t> values) {
    Active().log_.insert(Active().log_.end(), values);
}

std::uint32_t Hash(const void* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

std::uint32_t HashText(const char* text) {
    return text == nullptr ? 0xffffffff : Hash(text, std::strlen(text));
}

bool Fails() {
    return Active().rng_.Below(16) < Active().config_.failChance;
}

std::int32_t Result() {
    if (!Fails()) {
        return 0;
    }
    static const std::uint32_t kCodes[] = {
        0x887601c2,  // DDERR_SURFACELOST
        0x8876021c,  // DDERR_WASSTILLDRAWING
        0x8876023f,  // DDERR_NOBLTHW
        0x8876024b,  // DDERR_WRONGMODE
        0x887600e1,  // DDERR_NOEXCLUSIVEMODE
        0x80004005,  // E_FAIL
        0x80070057,  // DDERR_INVALIDPARAMS
        0x80004002,  // E_NOINTERFACE
        1,           // a success code other than DD_OK
    };
    return static_cast<std::int32_t>(kCodes[Active().rng_.Below(sizeof(kCodes) / sizeof(kCodes[0]))]);
}

void* NextSurface() {
    FakeDDraw& fake = Active();
    FakeDDrawObject* const surface = &g_surfaces[fake.nextSurface_ % kFakeDDrawSurfaces];
    ++fake.nextSurface_;
    return surface;
}

std::uint32_t GameId(const FakeDDrawGameObject* self);

std::uint32_t Value(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

// ---- method tables: which arguments are values (not pointers)
enum Kind : std::uint32_t { kDirectDraw, kSurface, kPalette };

std::uint32_t ValueMask(Kind kind, std::uint32_t slot) {
    if (kind == kDirectDraw) {
        switch (slot) {
            case 8: return 0b1;      // EnumDisplayModes(flags, ...)
            case 20: return 0b11;    // SetCooperativeLevel(window, flags)
            case 21: return 0b11111;  // SetDisplayMode(width, height, bpp, refresh, flags)
            case 22: return 0b11;    // WaitForVerticalBlank(flags, event)
            default: return 0;
        }
    }
    if (kind == kSurface) {
        switch (slot) {
            case 5: return 0b01000;   // Blt(dst, src, srcRect, flags, fx)
            case 7: return 0b10011;   // BltFast(x, y, src, srcRect, flags)
            case 11: return 0b10;     // Flip(target, flags)
            case 13: return 0b1;      // GetBltStatus(flags)
            case 16: return 0b1;      // GetColorKey(flags, key)
            case 18: return 0b1;      // GetFlipStatus(flags)
            case 25: return 0b1100;   // Lock(rect, desc, flags, event)
            case 29: return 0b1;      // SetColorKey(flags, key)
            case 30: return 0b11;     // SetOverlayPosition(x, y)
            case 33: return 0b01000;  // UpdateOverlay(srcRect, dst, dstRect, flags, fx)
            default: return 0;
        }
    }
    return slot == 4 || slot == 6 ? 0b0111 : 0;  // palette Get/SetEntries(flags, start, count, entries)
}

// A surface description by the fields its flags name.
void LogDesc(const std::uint32_t* desc) {
    if (desc == nullptr) {
        Log({0});
        return;
    }
    const std::uint32_t flags = desc[1];
    Log({desc[0], flags, (flags & 0x2) != 0 ? desc[2] : 0, (flags & 0x4) != 0 ? desc[3] : 0,
         (flags & 0x20) != 0 ? desc[5] : 0, (flags & 0x1) != 0 ? desc[0x68 / 4] : 0});
    if ((flags & 0x1000) != 0) {
        Log({Hash(desc + 0x48 / 4, 32)});
    }
}

std::int32_t Handle(FakeDDrawObject* self, Kind kind, std::uint32_t slot, const std::uint32_t* args,
                    std::uint32_t count) {
    FakeDDraw& fake = Active();
    Log({kLogTag | self->id << 8 | slot});
    // QueryInterface(iid, out).
    if (slot == 0) {
        Log({args[0] != 0 ? Hash(reinterpret_cast<const void*>(args[0]), 16) : 0, DDrawLogPointer(reinterpret_cast<void*>(args[1]))});
        if (fake.rng_.Below(16) < fake.config_.noInterfaceChance) {
            return static_cast<std::int32_t>(0x80004002);  // E_NOINTERFACE
        }
        const std::int32_t hr = Result();
        if (hr >= 0 && args[1] != 0) {
            *reinterpret_cast<void**>(args[1]) = NextSurface();
        }
        return hr;
    }
    if (slot == 1 || slot == 2) {  // AddRef, Release
        return static_cast<std::int32_t>(fake.rng_.Below(4));
    }
    if (kind == kDirectDraw && slot == 6) {  // CreateSurface(desc, out, outer)
        LogDesc(reinterpret_cast<const std::uint32_t*>(args[0]));
        Log({DDrawLogPointer(reinterpret_cast<void*>(args[1])), args[2]});
        const std::int32_t hr = Result();
        if (hr >= 0 && args[1] != 0) {
            *reinterpret_cast<void**>(args[1]) = NextSurface();
        }
        return hr;
    }
    if ((kind == kDirectDraw && slot == 12) || (kind == kSurface && (slot == 22 || slot == 25))) {
        // GetDisplayMode(desc), GetSurfaceDesc(desc), Lock(rect, desc, flags, event): the
        // description by content, filled on success.
        const std::size_t descArg = slot == 25 ? 1 : 0;
        auto* const desc = reinterpret_cast<std::uint32_t*>(args[descArg]);
        Log({desc != nullptr ? Hash(desc, 0x6c) : 0});
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != descArg) {
                Log({(ValueMask(kind, slot) >> i & 1) != 0 ? args[i] : DDrawLogPointer(reinterpret_cast<void*>(args[i]))});
            }
        }
        const std::int32_t hr = Result();
        if (hr >= 0 && desc != nullptr) {
            Rng& rng = fake.rng_;
            desc[2] = rng.Chance(1, 2) ? 480 + 120 * rng.Below(3) : rng.Below(2000);  // dwHeight
            desc[3] = rng.Chance(1, 2) ? 640 + 160 * rng.Below(3) : rng.Below(2000);  // dwWidth
            desc[4] = rng.Below(4096);                                                // lPitch
            desc[6] = rng.Chance(1, 2) ? 0 : rng.Below(100);                          // refresh rate
            desc[0x24 / 4] = Value(g_lockBuffer);                                     // lpSurface
            desc[0x54 / 4] = rng.Chance(2, 3) ? 16 : 8 * rng.Below(5);                // bit count
        }
        return hr;
    }
    if (kind == kSurface && slot == 12) {  // GetAttachedSurface(caps*, out)
        Log({args[0] != 0 ? *reinterpret_cast<const std::uint32_t*>(args[0]) : 0xffffffff,
             DDrawLogPointer(reinterpret_cast<void*>(args[1]))});
        const std::int32_t hr = Result();
        if (hr >= 0 && args[1] != 0) {
            *reinterpret_cast<void**>(args[1]) = NextSurface();
        }
        return hr;
    }
    if (kind == kDirectDraw && slot == 11) {  // GetCaps(driver, hel): their dwSize
        Log({args[0] != 0 ? *reinterpret_cast<const std::uint32_t*>(args[0]) : 0,
             args[1] != 0 ? *reinterpret_cast<const std::uint32_t*>(args[1]) : 0,
             args[0] != 0 ? Hash(reinterpret_cast<const void*>(args[0]), 0x16c) : 0,
             args[1] != 0 ? Hash(reinterpret_cast<const void*>(args[1]), 0x16c) : 0});
        return Result();
    }
    const std::uint32_t values = ValueMask(kind, slot);
    for (std::uint32_t i = 0; i < count; ++i) {
        Log({(values >> i & 1) != 0 ? args[i] : DDrawLogPointer(reinterpret_cast<void*>(args[i]))});
    }
    return Result();
}

Kind KindOf(const FakeDDrawObject* self) {
    if (self->id == kDirectDrawId) {
        return kDirectDraw;
    }
    return self->id >= kPaletteIdBase ? kPalette : kSurface;
}

template <std::uint32_t Slot>
std::int32_t __stdcall Com0(FakeDDrawObject* self) {
    return Handle(self, KindOf(self), Slot, nullptr, 0);
}
template <std::uint32_t Slot>
std::int32_t __stdcall Com1(FakeDDrawObject* self, std::uint32_t a) {
    const std::uint32_t args[] = {a};
    return Handle(self, KindOf(self), Slot, args, 1);
}
template <std::uint32_t Slot>
std::int32_t __stdcall Com2(FakeDDrawObject* self, std::uint32_t a, std::uint32_t b) {
    const std::uint32_t args[] = {a, b};
    return Handle(self, KindOf(self), Slot, args, 2);
}
template <std::uint32_t Slot>
std::int32_t __stdcall Com3(FakeDDrawObject* self, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const std::uint32_t args[] = {a, b, c};
    return Handle(self, KindOf(self), Slot, args, 3);
}
template <std::uint32_t Slot>
std::int32_t __stdcall Com4(FakeDDrawObject* self, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                            std::uint32_t d) {
    const std::uint32_t args[] = {a, b, c, d};
    return Handle(self, KindOf(self), Slot, args, 4);
}
template <std::uint32_t Slot>
std::int32_t __stdcall Com5(FakeDDrawObject* self, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                            std::uint32_t d, std::uint32_t e) {
    const std::uint32_t args[] = {a, b, c, d, e};
    return Handle(self, KindOf(self), Slot, args, 5);
}

#define S3RE_FAKE_SLOT(n, slot) reinterpret_cast<void*>(&Com##n<slot>)

void* const kDirectDrawVtable[] = {
    S3RE_FAKE_SLOT(2, 0),  S3RE_FAKE_SLOT(0, 1),  S3RE_FAKE_SLOT(0, 2),  S3RE_FAKE_SLOT(0, 3),
    S3RE_FAKE_SLOT(3, 4),  S3RE_FAKE_SLOT(4, 5),  S3RE_FAKE_SLOT(3, 6),  S3RE_FAKE_SLOT(2, 7),
    S3RE_FAKE_SLOT(4, 8),  S3RE_FAKE_SLOT(4, 9),  S3RE_FAKE_SLOT(0, 10), S3RE_FAKE_SLOT(2, 11),
    S3RE_FAKE_SLOT(1, 12), S3RE_FAKE_SLOT(2, 13), S3RE_FAKE_SLOT(1, 14), S3RE_FAKE_SLOT(1, 15),
    S3RE_FAKE_SLOT(1, 16), S3RE_FAKE_SLOT(1, 17), S3RE_FAKE_SLOT(1, 18), S3RE_FAKE_SLOT(0, 19),
    S3RE_FAKE_SLOT(2, 20), S3RE_FAKE_SLOT(5, 21), S3RE_FAKE_SLOT(2, 22), S3RE_FAKE_SLOT(3, 23),
};

void* const kSurfaceVtable[] = {
    S3RE_FAKE_SLOT(2, 0),  S3RE_FAKE_SLOT(0, 1),  S3RE_FAKE_SLOT(0, 2),  S3RE_FAKE_SLOT(1, 3),
    S3RE_FAKE_SLOT(1, 4),  S3RE_FAKE_SLOT(5, 5),  S3RE_FAKE_SLOT(3, 6),  S3RE_FAKE_SLOT(5, 7),
    S3RE_FAKE_SLOT(2, 8),  S3RE_FAKE_SLOT(2, 9),  S3RE_FAKE_SLOT(3, 10), S3RE_FAKE_SLOT(2, 11),
    S3RE_FAKE_SLOT(2, 12), S3RE_FAKE_SLOT(1, 13), S3RE_FAKE_SLOT(1, 14), S3RE_FAKE_SLOT(1, 15),
    S3RE_FAKE_SLOT(2, 16), S3RE_FAKE_SLOT(1, 17), S3RE_FAKE_SLOT(1, 18), S3RE_FAKE_SLOT(2, 19),
    S3RE_FAKE_SLOT(1, 20), S3RE_FAKE_SLOT(1, 21), S3RE_FAKE_SLOT(1, 22), S3RE_FAKE_SLOT(2, 23),
    S3RE_FAKE_SLOT(0, 24), S3RE_FAKE_SLOT(4, 25), S3RE_FAKE_SLOT(1, 26), S3RE_FAKE_SLOT(0, 27),
    S3RE_FAKE_SLOT(1, 28), S3RE_FAKE_SLOT(2, 29), S3RE_FAKE_SLOT(2, 30), S3RE_FAKE_SLOT(1, 31),
    S3RE_FAKE_SLOT(1, 32), S3RE_FAKE_SLOT(5, 33), S3RE_FAKE_SLOT(1, 34), S3RE_FAKE_SLOT(2, 35),
    S3RE_FAKE_SLOT(1, 36), S3RE_FAKE_SLOT(1, 37), S3RE_FAKE_SLOT(1, 38), S3RE_FAKE_SLOT(2, 39),
};

void* const kPaletteVtable[] = {
    S3RE_FAKE_SLOT(2, 0), S3RE_FAKE_SLOT(0, 1), S3RE_FAKE_SLOT(0, 2), S3RE_FAKE_SLOT(1, 3),
    S3RE_FAKE_SLOT(4, 4), S3RE_FAKE_SLOT(3, 5), S3RE_FAKE_SLOT(4, 6),
};

#undef S3RE_FAKE_SLOT

// ---- game objects: thiscall slots
// A game object's id: the fake ones by number, objects under test (which
// borrow the fake vtable) as DDrawLogPointer shows them.
std::uint32_t GameId(const FakeDDrawGameObject* self) {
    for (std::size_t i = 0; i < kFakeDDrawGameObjects; ++i) {
        if (self == &g_games[i]) {
            return kGameIdBase + static_cast<std::uint32_t>(i);
        }
    }
    return DDrawLogPointer(self);
}

void* __fastcall GameDelete(FakeDDrawGameObject* self, void* /*edx*/, std::uint32_t flags) {
    Log({kLogTag | 0xff, GameId(self), 0, flags});
    return self;
}

// Slot k with up to four stack arguments (the count is g_gameSlotArgs[k];
// the callee pops them, so each count needs its own function).
template <std::uint32_t Slot>
std::uint32_t GameResult(FakeDDrawGameObject* self, const std::uint32_t* args, std::uint32_t count) {
    Log({kLogTag | 0xff, GameId(self), Slot});
    for (std::uint32_t i = 0; i < count; ++i) {
        Log({DDrawLogPointer(reinterpret_cast<void*>(args[i]))});
    }
    return Active().rng_.Next32();
}
template <std::uint32_t Slot>
std::uint32_t __fastcall Game0(FakeDDrawGameObject* self, void*) {
    return GameResult<Slot>(self, nullptr, 0);
}
template <std::uint32_t Slot>
std::uint32_t __fastcall Game1(FakeDDrawGameObject* self, void*, std::uint32_t a) {
    const std::uint32_t args[] = {a};
    return GameResult<Slot>(self, args, 1);
}
template <std::uint32_t Slot>
std::uint32_t __fastcall Game2(FakeDDrawGameObject* self, void*, std::uint32_t a, std::uint32_t b) {
    const std::uint32_t args[] = {a, b};
    return GameResult<Slot>(self, args, 2);
}

// The vtable used: rebuilt by ResetObjects from g_gameSlotArgs.
void* g_gameVtable[16];

template <std::uint32_t Slot>
void* GameSlot(std::uint32_t count) {
    switch (count) {
        case 1: return reinterpret_cast<void*>(&Game1<Slot>);
        case 2: return reinterpret_cast<void*>(&Game2<Slot>);
        default: return reinterpret_cast<void*>(&Game0<Slot>);
    }
}

template <std::uint32_t... Slots>
void FillGameVtable(std::integer_sequence<std::uint32_t, Slots...>) {
    ((g_gameVtable[Slots + 1] = GameSlot<Slots + 1>(g_gameSlotArgs[Slots + 1])), ...);
}

// ---- imports
std::int32_t __stdcall FakeDirectDrawEnumerate(void* callback, void* context) {
    Log({kImportTag | 0x10, DDrawLogPointer(callback), DDrawLogPointer(context)});
    return Result();
}

void* __stdcall FakeLoadLibraryA(const char* name) {
    Log({kImportTag | 0x11, HashText(name)});
    return Fails() ? nullptr : reinterpret_cast<void*>(kFakeModule);
}

void* __stdcall FakeGetProcAddress(void* module, const char* name) {
    Log({kImportTag | 0x12, Value(module), HashText(name)});
    return Fails() ? nullptr : reinterpret_cast<void*>(&FakeDirectDrawEnumerate);
}

std::int32_t __stdcall FakeCoCreateInstance(const void* clsid, void* outer, std::uint32_t context, const void* iid,
                                            void** out) {
    Log({kImportTag | 0x13, clsid != nullptr ? Hash(clsid, 16) : 0, DDrawLogPointer(outer), context,
         iid != nullptr ? Hash(iid, 16) : 0, DDrawLogPointer(out)});
    const std::int32_t hr = Result();
    if (out != nullptr) {
        *out = hr >= 0 ? static_cast<void*>(&g_directDraw) : nullptr;
    }
    return hr;
}

int __stdcall FakeSetRect(void* rect, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
    Log({kImportTag | 0x14, DDrawLogPointer(rect), static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
         static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(bottom)});
    if (rect != nullptr) {
        const std::int32_t values[4] = {left, top, right, bottom};
        std::memcpy(rect, values, sizeof(values));
    }
    return static_cast<int>(Active().rng_.Below(2));
}

std::int32_t __stdcall FakeSendMessageA(void* window, std::uint32_t message, std::uint32_t wParam,
                                        std::uint32_t lParam) {
    Log({kImportTag | 0x15, Value(window), message, wParam, lParam});
    return static_cast<std::int32_t>(Active().rng_.Next32());
}

void __stdcall FakeSleep(std::uint32_t milliseconds) {
    Log({kImportTag | 0x16, milliseconds});
}

template <typename T>
void PatchImport(const GameGlobal<T>& slot, const void* fake) {
    PatchBytes(slot.address(), &fake, sizeof(fake));
}

}  // namespace

FakeDDraw& ActiveDDraw() {
    return *g_active;
}

std::uint32_t DDrawLogPointer(const void* p) {
    if (p == nullptr) {
        return 0;
    }
    if (p == &g_directDraw) {
        return kDirectDrawId;
    }
    for (const FakeDDrawObject& surface : g_surfaces) {
        if (p == &surface) {
            return surface.id;
        }
    }
    for (const FakeDDrawObject& palette : g_palettes) {
        if (p == &palette) {
            return palette.id;
        }
    }
    for (const FakeDDrawGameObject& game : g_games) {
        if (p == &game) {
            return GameId(&game);
        }
    }
    const auto* const bytes = static_cast<const std::uint8_t*>(p);
    if (g_active != nullptr && g_active->arena_ != nullptr && bytes >= g_active->arena_ &&
        bytes < g_active->arena_ + g_active->arenaSize_) {
        return 0x10000 + static_cast<std::uint32_t>(bytes - g_active->arena_);
    }
    const std::uint32_t value = Value(p);
    if (value >= 0x400000 && value < 0x800000) {
        return value;  // S3.EXE's image: strings, GUIDs, callbacks
    }
    if (value < 0x10000) {
        return value;  // not a pointer
    }
    return kOtherPointer;
}

FakeDDrawObject* FakeDDraw::DirectDraw() {
    return &g_directDraw;
}

FakeDDrawObject* FakeDDraw::Surface(std::size_t i) {
    return &g_surfaces[i];
}

FakeDDrawObject* FakeDDraw::Palette(std::size_t i) {
    return &g_palettes[i];
}

FakeDDrawGameObject* FakeDDraw::Game(std::size_t i) {
    return &g_games[i];
}

void* const* FakeDDraw::GameVtable() {
    return g_gameVtable;
}

void FakeDDraw::SetGameSlotArgs(std::uint32_t slot, std::uint32_t count) {
    g_gameSlotArgs[slot] = count;
}

void FakeDDraw::ResetObjects() {
    g_directDraw.vtable = kDirectDrawVtable;
    g_directDraw.id = kDirectDrawId;
    for (std::size_t i = 0; i < kFakeDDrawSurfaces; ++i) {
        g_surfaces[i].vtable = kSurfaceVtable;
        g_surfaces[i].id = kSurfaceIdBase + static_cast<std::uint32_t>(i);
    }
    for (std::size_t i = 0; i < kFakeDDrawPalettes; ++i) {
        g_palettes[i].vtable = kPaletteVtable;
        g_palettes[i].id = kPaletteIdBase + static_cast<std::uint32_t>(i);
    }
    g_gameVtable[0] = reinterpret_cast<void*>(&GameDelete);
    FillGameVtable(std::make_integer_sequence<std::uint32_t, 15>{});
    for (FakeDDrawGameObject& game : g_games) {
        std::memset(&game, 0, sizeof(game));
        game.vtable = g_gameVtable;
    }
}

void FakeDDraw::Install() {
    g_active = this;
    PatchImport(g_importLoadLibraryA, reinterpret_cast<const void*>(&FakeLoadLibraryA));
    PatchImport(g_importGetProcAddress, reinterpret_cast<const void*>(&FakeGetProcAddress));
    PatchImport(g_importCoCreateInstance, reinterpret_cast<const void*>(&FakeCoCreateInstance));
    PatchImport(g_importSetRect, reinterpret_cast<const void*>(&FakeSetRect));
    PatchImport(g_importSendMessageA, reinterpret_cast<const void*>(&FakeSendMessageA));
    PatchImport(g_importSleep, reinterpret_cast<const void*>(&FakeSleep));
}

void FakeDDraw::Uninstall() {
    UnpatchBytes(g_importSleep.address());
    UnpatchBytes(g_importSendMessageA.address());
    UnpatchBytes(g_importSetRect.address());
    UnpatchBytes(g_importCoCreateInstance.address());
    UnpatchBytes(g_importGetProcAddress.address());
    UnpatchBytes(g_importLoadLibraryA.address());
    g_active = nullptr;
}

}  // namespace s3re::test
