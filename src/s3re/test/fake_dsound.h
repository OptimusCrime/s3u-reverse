#pragma once

// Scripted DirectSound for differential tests of the sound code
// (directx_sound.cpp, the sound loader): fake COM objects (IDirectSound,
// IDirectSoundBuffer), fake game objects with a logging vtable (the
// wrappers' own virtual calls) and fake LoadLibraryA / GetProcAddress /
// DirectSoundCreate. Every call is logged (an id, the arguments that
// matter; stack addresses only as "given or not") and answered from the
// script's Rng: a test runs the original with one FakeDSound and ours with
// another made from the same seed and compares the logs.
//
// The fake objects are static (the same addresses in both runs), so
// objects that store interface pointers compare byte for byte. A
// successful CreateSoundBuffer / DuplicateSoundBuffer hands out the next
// fake buffer of the pool (the pool position is part of the script).

#include "test/difftest.h"

#include <cstdint>
#include <vector>

namespace s3re::test {

inline constexpr std::size_t kFakeSoundBuffers = 512;
inline constexpr std::size_t kFakeGameObjects = 4;

// A fake COM object: vtable first, like every interface.
struct FakeComObject {
    void* const* vtable;
    std::uint32_t id;
};

// A fake game object with a logging vtable; its layout starts like
// DirectX::CDirectSound (+4 the interface), so it can stand in for
// g_directSound.
struct FakeGameObject {
    void* const* vtable;
    void* sound;
    std::uint32_t id;
    std::uint8_t padding[0x40];
};

struct FakeDSoundConfig {
    // Chance (out of 16) that a COM call fails (or LoadLibraryA /
    // GetProcAddress return null).
    std::uint32_t failChance = 2;
    // The failure codes to choose from (0: the standard set).
    const std::vector<std::int32_t>* failures = nullptr;
    // A failed Lock still writes its out values (the first part: the lock
    // area with 0 bytes, no second part), so that code that goes on after a
    // DSERR_BUFFERLOST (CheckResult doesn't throw then) copies nothing
    // instead of using its uninitialized locals.
    bool lockAlwaysWrites = false;
};

class FakeDSound {
public:
    FakeDSound(std::uint64_t seed, const FakeDSoundConfig& config = {}) : rng_(seed), config_(config) {}

    // Patches LoadLibraryA and GetProcAddress and makes this the active
    // script (COM calls of the fake objects go to the active one).
    void Install();
    void Uninstall();

    // The fake IDirectSound.
    static FakeComObject* Sound();
    // Fake IDirectSoundBuffer number i.
    static FakeComObject* Buffer(std::size_t i);
    // Fake game object number i (vtable slots log and return a scripted
    // value; slot 0 takes one stack argument, the others none).
    static FakeGameObject* Game(std::size_t i);
    // Resets the fake objects' fields (call before each case).
    static void ResetObjects();

    std::vector<std::uint32_t>& log() { return log_; }

    Rng rng_;
    FakeDSoundConfig config_;
    std::vector<std::uint32_t> log_;
    std::size_t nextBuffer_ = 0;
};

// The active script (for the fake functions).
FakeDSound& ActiveDSound();

// A pointer for the log: fake objects by id, others as given.
std::uint32_t DSoundLogPointer(const void* p);

}  // namespace s3re::test
