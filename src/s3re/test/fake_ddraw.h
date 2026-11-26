#pragma once

// Scripted DirectDraw for differential tests of the DirectDraw wrappers
// and the drawer code (directx_draw.cpp, the CGameDrawer/CDrawer members):
// fake COM objects (one IDirectDraw2, surfaces, palettes) whose every
// method logs its call and returns a result drawn from the script's Rng,
// fake game objects with a logging vtable (the wrappers' own virtual calls:
// CDirectDraw's slots, a surface owner's slot 3, deleting destructors), and
// fakes of the imports the code calls (LoadLibraryA, GetProcAddress for
// DirectDrawEnumerateA, CoCreateInstance, SetRect, SendMessageA, Sleep).
// Like FakeDSound: a test runs the original under one FakeDDraw and ours
// under another made from the same seed and compares the logs.
//
// Logged pointers: fake objects by id, addresses inside the registered
// arena relative to it (0x10000 + offset), S3.EXE's image as they are,
// others (stack temporaries) as kOtherPointer. Out parameters the methods
// write (created surfaces, QueryInterface, GetAttachedSurface) get the next
// fake surface of the pool. Surface descriptions are logged by the fields
// their dwFlags name (the primary surfaces' Creates leave the others
// uninitialized), filled descriptions get random contents from the script.

#include "test/difftest.h"

#include <cstdint>
#include <vector>

namespace s3re::test {

inline constexpr std::size_t kFakeDDrawSurfaces = 64;
inline constexpr std::size_t kFakeDDrawPalettes = 4;
inline constexpr std::size_t kFakeDDrawGameObjects = 8;
inline constexpr std::uint32_t kOtherPointer = 0xdddd0001;

struct FakeDDrawObject {
    void* const* vtable;
    std::uint32_t id;
};

// A fake game object: a vtable of 16 logging thiscall slots (slot 0 takes
// one argument, the deleting destructor's flags; slots 1..15 take
// `slotArgs[slot]` arguments, default 0) and room for fields.
struct FakeDDrawGameObject {
    void* const* vtable;
    std::uint8_t fields[0xfc];
};

struct FakeDDrawConfig {
    // Chance (out of 16) that a COM call fails (or an import returns null).
    std::uint32_t failChance = 2;
    // Chance (out of 16) that QueryInterface answers E_NOINTERFACE.
    std::uint32_t noInterfaceChance = 4;
};

class FakeDDraw {
public:
    FakeDDraw(std::uint64_t seed, const FakeDDrawConfig& config = {}) : rng_(seed), config_(config) {}

    // Patches the imports and makes this the active script.
    void Install();
    void Uninstall();

    // Pointers inside [base, base + size) are logged relative to base.
    void SetArena(const void* base, std::size_t size) {
        arena_ = static_cast<const std::uint8_t*>(base);
        arenaSize_ = size;
    }

    static FakeDDrawObject* DirectDraw();
    static FakeDDrawObject* Surface(std::size_t i);
    static FakeDDrawObject* Palette(std::size_t i);
    static FakeDDrawGameObject* Game(std::size_t i);
    // The vtable of the fake game objects (to give an object under test).
    static void* const* GameVtable();
    // The number of stack arguments of game slot k (1..15; default 0).
    static void SetGameSlotArgs(std::uint32_t slot, std::uint32_t count);
    // Resets the fake objects (call before each case).
    static void ResetObjects();

    std::vector<std::uint32_t>& log() { return log_; }

    // State (read by fake_ddraw.cpp).
    Rng rng_;
    FakeDDrawConfig config_;
    std::vector<std::uint32_t> log_;
    std::size_t nextSurface_ = 0;
    const std::uint8_t* arena_ = nullptr;
    std::size_t arenaSize_ = 0;
};

// The active script.
FakeDDraw& ActiveDDraw();

// A pointer as the log shows it.
std::uint32_t DDrawLogPointer(const void* p);

}  // namespace s3re::test
