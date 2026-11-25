#include "test/fake_dsound.h"

#include "game/globals.h"
#include "test/callee_stub.h"

#include <windows.h>

#include <cstring>

namespace s3re::test {

namespace {

using namespace s3re::game;

FakeDSound* g_active = nullptr;

constexpr std::uint32_t kLogTag = 0xd5000000;
constexpr std::uint32_t kSoundId = 0x100;
constexpr std::uint32_t kBufferIdBase = 0x200;
constexpr std::uint32_t kGameIdBase = 0x300;
// Lock hands out parts of this area (the sound code copies samples into
// it).
constexpr std::uint32_t kLockArea = 8u << 20;

FakeComObject g_sound{nullptr, kSoundId};
FakeComObject g_buffers[kFakeSoundBuffers];
FakeGameObject g_games[kFakeGameObjects];

std::uint8_t* LockArea() {
    static std::uint8_t* const area =
        static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kLockArea, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    return area;
}

FakeDSound& Active() {
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

std::uint32_t Given(const void* p) {
    return p != nullptr ? 1 : 0;
}

bool Fails() {
    return Active().rng_.Below(16) < Active().config_.failChance;
}

std::int32_t Result() {
    FakeDSound& fake = Active();
    if (Fails()) {
        static const std::vector<std::int32_t> kStandard = {
            static_cast<std::int32_t>(0x88780096),  // DSERR_BUFFERLOST
            static_cast<std::int32_t>(0x88780078),  // DSERR_NODRIVER
            static_cast<std::int32_t>(0x8878000a),  // DSERR_ALLOCATED
            static_cast<std::int32_t>(0x80004005),  // E_FAIL
            static_cast<std::int32_t>(0x88780064),  // DSERR_BADFORMAT
            static_cast<std::int32_t>(0x80070057),  // DSERR_INVALIDPARAM
            static_cast<std::int32_t>(0x8007000e),  // E_OUTOFMEMORY
            1,                                      // a success code other than DS_OK
        };
        const std::vector<std::int32_t>& codes = fake.config_.failures != nullptr ? *fake.config_.failures : kStandard;
        return codes[fake.rng_.Below(static_cast<std::uint32_t>(codes.size()))];
    }
    return 0;
}

std::uint32_t ObjectId(const FakeComObject* self) {
    return self->id;
}

void* NextBuffer() {
    FakeDSound& fake = Active();
    FakeComObject* const buffer = FakeDSound::Buffer(fake.nextBuffer_ % kFakeSoundBuffers);
    ++fake.nextBuffer_;
    return buffer;
}

// ---- IUnknown
std::int32_t __stdcall QueryInterface(FakeComObject* self, const void* iid, void** out) {
    Log({kLogTag | ObjectId(self) << 8 | 0, Given(iid), Given(out)});
    return Result();
}
std::uint32_t __stdcall AddRef(FakeComObject* self) {
    Log({kLogTag | ObjectId(self) << 8 | 1});
    return Active().rng_.Below(4);
}
std::uint32_t __stdcall Release(FakeComObject* self) {
    Log({kLogTag | ObjectId(self) << 8 | 2});
    return Active().rng_.Below(4);
}

// ---- IDirectSound
std::int32_t __stdcall CreateSoundBuffer(FakeComObject* self, const std::uint32_t* desc, void** out, void* outer) {
    Log({kLogTag | ObjectId(self) << 8 | 3, Given(out), DSoundLogPointer(outer)});
    if (desc != nullptr) {
        // The format by content (it may sit in a heap block).
        Log({desc[0], desc[1], desc[2], desc[3], desc[4] != 0 ? 1u : 0u});
        if (desc[4] != 0) {
            Log({Hash(reinterpret_cast<const void*>(desc[4]), 18)});  // the WAVEFORMATEX
        }
    }
    const std::int32_t hr = Result();
    if (hr == 0 && out != nullptr) {
        *out = NextBuffer();
    }
    return hr;
}
std::int32_t __stdcall SoundGetCaps(FakeComObject* self, void* caps) {
    Log({kLogTag | ObjectId(self) << 8 | 4, Given(caps)});
    return Result();
}
std::int32_t __stdcall DuplicateSoundBuffer(FakeComObject* self, void* original, void** out) {
    Log({kLogTag | ObjectId(self) << 8 | 5, DSoundLogPointer(original), Given(out)});
    const std::int32_t hr = Result();
    if (hr == 0 && out != nullptr) {
        *out = NextBuffer();
    }
    return hr;
}
std::int32_t __stdcall SetCooperativeLevel(FakeComObject* self, std::uint32_t window, std::uint32_t level) {
    Log({kLogTag | ObjectId(self) << 8 | 6, window, level});
    return Result();
}
std::int32_t __stdcall SoundNoArgs(FakeComObject* self) {
    Log({kLogTag | ObjectId(self) << 8 | 7});
    return Result();
}
std::int32_t __stdcall SoundOneArg(FakeComObject* self, std::uint32_t a) {
    Log({kLogTag | ObjectId(self) << 8 | 8, a});
    return Result();
}

// ---- IDirectSoundBuffer (slots 3..20)
std::int32_t __stdcall BufferGetCaps(FakeComObject* self, void* caps) {
    Log({kLogTag | ObjectId(self) << 8 | 3, Given(caps)});
    return Result();
}
std::int32_t __stdcall GetCurrentPosition(FakeComObject* self, std::uint32_t* play, std::uint32_t* write) {
    Log({kLogTag | ObjectId(self) << 8 | 4, Given(play), Given(write)});
    const std::int32_t hr = Result();
    if (play != nullptr) {
        *play = Active().rng_.Below(0x10000);
    }
    if (write != nullptr) {
        *write = Active().rng_.Below(0x10000);
    }
    return hr;
}
std::int32_t __stdcall GetFormat(FakeComObject* self, void* format, std::uint32_t size, std::uint32_t* written) {
    Log({kLogTag | ObjectId(self) << 8 | 5, Given(format), size, Given(written)});
    return Result();
}
// GetVolume, GetPan, GetFrequency, GetStatus: one out value.
template <std::uint32_t Slot>
std::int32_t __stdcall GetValue(FakeComObject* self, std::int32_t* value) {
    Log({kLogTag | ObjectId(self) << 8 | Slot, Given(value)});
    const std::int32_t hr = Result();
    if (value != nullptr) {
        Rng& rng = Active().rng_;
        switch (Slot) {
            case 6: *value = -static_cast<std::int32_t>(rng.Below(10001)); break;  // volume
            case 7: *value = rng.Range(-10000, 10000); break;                        // pan
            case 9: *value = static_cast<std::int32_t>(rng.Below(16)); break;        // status bits
            default: *value = static_cast<std::int32_t>(rng.Next32()); break;
        }
    }
    return hr;
}
std::int32_t __stdcall BufferInitialize(FakeComObject* self, void* sound, void* desc) {
    Log({kLogTag | ObjectId(self) << 8 | 10, DSoundLogPointer(sound), Given(desc)});
    return Result();
}
std::int32_t __stdcall Lock(FakeComObject* self, std::uint32_t offset, std::uint32_t bytes, void** part1,
                            std::uint32_t* size1, void** part2, std::uint32_t* size2, std::uint32_t flags) {
    Log({kLogTag | ObjectId(self) << 8 | 11, offset, bytes, Given(part1), Given(size1), Given(part2), Given(size2), flags});
    const std::int32_t hr = Result();
    if (hr == 0) {
        const std::uint32_t first = bytes < kLockArea ? bytes : kLockArea;
        if (part1 != nullptr) {
            *part1 = LockArea();
        }
        if (size1 != nullptr) {
            *size1 = first;
        }
        if (part2 != nullptr) {
            *part2 = nullptr;
        }
        if (size2 != nullptr) {
            *size2 = 0;
        }
    }
    return hr;
}
std::int32_t __stdcall Play(FakeComObject* self, std::uint32_t reserved1, std::uint32_t priority, std::uint32_t flags) {
    Log({kLogTag | ObjectId(self) << 8 | 12, reserved1, priority, flags});
    return Result();
}
std::int32_t __stdcall SetFormat(FakeComObject* self, const void* format) {
    Log({kLogTag | ObjectId(self) << 8 | 14, DSoundLogPointer(format), format != nullptr ? Hash(format, 16) : 0});
    return Result();
}
// SetCurrentPosition, SetVolume, SetPan, SetFrequency: one value.
template <std::uint32_t Slot>
std::int32_t __stdcall SetValue(FakeComObject* self, std::uint32_t value) {
    Log({kLogTag | ObjectId(self) << 8 | Slot, value});
    return Result();
}
std::int32_t __stdcall Stop(FakeComObject* self) {
    Log({kLogTag | ObjectId(self) << 8 | 18});
    return Result();
}
std::int32_t __stdcall Unlock(FakeComObject* self, void* part1, std::uint32_t size1, void* part2, std::uint32_t size2) {
    // The data written into the locked part.
    const std::uint32_t contents = part1 == LockArea() && size1 <= kLockArea ? Hash(part1, size1) : 0;
    Log({kLogTag | ObjectId(self) << 8 | 19, part1 == LockArea() ? 1u : DSoundLogPointer(part1), size1, contents,
         DSoundLogPointer(part2), size2});
    return Result();
}
std::int32_t __stdcall BufferRestore(FakeComObject* self) {
    Log({kLogTag | ObjectId(self) << 8 | 20});
    return Result();
}

void* const kSoundVtable[] = {
    reinterpret_cast<void*>(&QueryInterface),     reinterpret_cast<void*>(&AddRef),
    reinterpret_cast<void*>(&Release),            reinterpret_cast<void*>(&CreateSoundBuffer),
    reinterpret_cast<void*>(&SoundGetCaps),       reinterpret_cast<void*>(&DuplicateSoundBuffer),
    reinterpret_cast<void*>(&SetCooperativeLevel), reinterpret_cast<void*>(&SoundNoArgs),
    reinterpret_cast<void*>(&SoundOneArg),        reinterpret_cast<void*>(&SoundOneArg),
    reinterpret_cast<void*>(&SoundOneArg),
};

void* const kBufferVtable[] = {
    reinterpret_cast<void*>(&QueryInterface),   reinterpret_cast<void*>(&AddRef),
    reinterpret_cast<void*>(&Release),          reinterpret_cast<void*>(&BufferGetCaps),
    reinterpret_cast<void*>(&GetCurrentPosition), reinterpret_cast<void*>(&GetFormat),
    reinterpret_cast<void*>(&GetValue<6>),      reinterpret_cast<void*>(&GetValue<7>),
    reinterpret_cast<void*>(&GetValue<8>),      reinterpret_cast<void*>(&GetValue<9>),
    reinterpret_cast<void*>(&BufferInitialize), reinterpret_cast<void*>(&Lock),
    reinterpret_cast<void*>(&Play),             reinterpret_cast<void*>(&SetValue<13>),
    reinterpret_cast<void*>(&SetFormat),        reinterpret_cast<void*>(&SetValue<15>),
    reinterpret_cast<void*>(&SetValue<16>),     reinterpret_cast<void*>(&SetValue<17>),
    reinterpret_cast<void*>(&Stop),             reinterpret_cast<void*>(&Unlock),
    reinterpret_cast<void*>(&BufferRestore),
};

// ---- game objects: thiscall slots
void* __fastcall GameDelete(FakeGameObject* self, void* /*edx*/, std::uint32_t flags) {
    Log({kLogTag | self->id << 8 | 0, flags});
    return self;
}
template <std::uint32_t Slot>
std::uint32_t __fastcall GameSlot(FakeGameObject* self, void* /*edx*/) {
    Log({kLogTag | self->id << 8 | Slot});
    return Active().rng_.Next32();
}

void* const kGameVtable[] = {
    reinterpret_cast<void*>(&GameDelete),  reinterpret_cast<void*>(&GameSlot<1>), reinterpret_cast<void*>(&GameSlot<2>),
    reinterpret_cast<void*>(&GameSlot<3>), reinterpret_cast<void*>(&GameSlot<4>), reinterpret_cast<void*>(&GameSlot<5>),
    reinterpret_cast<void*>(&GameSlot<6>), reinterpret_cast<void*>(&GameSlot<7>),
};

// ---- imports
std::int32_t __stdcall FakeDirectSoundCreate(const void* device, void** out, void* outer) {
    Log({kLogTag | 0xf3, DSoundLogPointer(device), Given(out), DSoundLogPointer(outer)});
    const std::int32_t hr = Result();
    if (hr == 0 && out != nullptr) {
        *out = &g_sound;
    }
    return hr;
}

constexpr std::uint32_t kFakeModule = 0x7ffe0000;

void* __stdcall FakeLoadLibraryA(const char* name) {
    Log({kLogTag | 0xf1, HashText(name)});
    return Fails() ? nullptr : reinterpret_cast<void*>(kFakeModule);
}

void* __stdcall FakeGetProcAddress(void* module, const char* name) {
    Log({kLogTag | 0xf2, reinterpret_cast<std::uint32_t>(module), HashText(name)});
    return Fails() ? nullptr : reinterpret_cast<void*>(&FakeDirectSoundCreate);
}

}  // namespace

FakeDSound& ActiveDSound() {
    return *g_active;
}

std::uint32_t DSoundLogPointer(const void* p) {
    if (p == &g_sound) {
        return kSoundId;
    }
    for (std::size_t i = 0; i < kFakeSoundBuffers; ++i) {
        if (p == &g_buffers[i]) {
            return g_buffers[i].id;
        }
    }
    for (std::size_t i = 0; i < kFakeGameObjects; ++i) {
        if (p == &g_games[i]) {
            return g_games[i].id;
        }
    }
    return reinterpret_cast<std::uint32_t>(p);
}

FakeComObject* FakeDSound::Sound() {
    return &g_sound;
}

FakeComObject* FakeDSound::Buffer(std::size_t i) {
    return &g_buffers[i];
}

FakeGameObject* FakeDSound::Game(std::size_t i) {
    return &g_games[i];
}

void FakeDSound::ResetObjects() {
    g_sound.vtable = kSoundVtable;
    g_sound.id = kSoundId;
    for (std::size_t i = 0; i < kFakeSoundBuffers; ++i) {
        g_buffers[i].vtable = kBufferVtable;
        g_buffers[i].id = kBufferIdBase + static_cast<std::uint32_t>(i);
    }
    for (std::size_t i = 0; i < kFakeGameObjects; ++i) {
        std::memset(&g_games[i], 0, sizeof(g_games[i]));
        g_games[i].vtable = kGameVtable;
        g_games[i].sound = &g_sound;
        g_games[i].id = kGameIdBase + static_cast<std::uint32_t>(i);
    }
}

void FakeDSound::Install() {
    g_active = this;
    const void* const load = reinterpret_cast<const void*>(&FakeLoadLibraryA);
    const void* const proc = reinterpret_cast<const void*>(&FakeGetProcAddress);
    PatchBytes(g_importLoadLibraryA.address(), &load, sizeof(load));
    PatchBytes(g_importGetProcAddress.address(), &proc, sizeof(proc));
}

void FakeDSound::Uninstall() {
    UnpatchBytes(g_importGetProcAddress.address());
    UnpatchBytes(g_importLoadLibraryA.address());
    g_active = nullptr;
}

}  // namespace s3re::test
