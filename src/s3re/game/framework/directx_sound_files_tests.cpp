// Tests of directx_sound_files.cpp (CSoundFile, CMemorySoundFile, the
// DirectSound exception, CAppNotActiveException) and of CSoundData's two
// getters (sound_data.cpp).
//
// Each case picks one function and runs the original on one object and
// ours on a copy (random bytes with guard bytes, from the game's heap;
// strings built alike in both). Compared: EAX (relative to the object where
// it is an address), the object bytes, strings by content (length,
// capacity, text, count byte, allocator byte where it is reproducible,
// whether the argument's buffer is shared) and the count bytes of the
// strings' old buffers, the bytes Read and GetFormat write.

#include "game/framework/directx_sound_layout.h"
#include "game/sound/sound_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using msvc6::String;

void* __fastcall DirectX_CSoundFile_Construct(void* self, void* edx, const String* name);
std::uint32_t __fastcall DirectX_CSoundFile_Destroy(void* self, void* edx);
void* __fastcall DirectX_CMemorySoundFile_Construct(void* self, void* edx, const void* format, const void* data,
                                                    std::uint32_t bytes, std::uint32_t mode);
void __fastcall DirectX_CMemorySoundFile_Rewind(void* self, void* edx);
std::uint32_t __fastcall DirectX_CMemorySoundFile_GetFormat(void* self, void* edx, void* format,
                                                            std::uint32_t* bytes);
void* __fastcall DirectX_CMemorySoundFile_Read(void* self, void* edx, std::uint32_t count, void* dest);
void* __fastcall DirectX_CDirectSoundException_Construct(void* self, void* edx, std::int32_t hr, const char* where,
                                                         std::uint32_t mostDerived);
String* __fastcall DirectX_CDirectSoundException_Describe(void* self, void* edx, String* result);
std::uint32_t __fastcall DirectX_CDirectSoundException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CAppNotActiveException_Destroy(void* self, void* edx);
void* __fastcall DirectX_CAppNotActiveException_Construct(void* self, void* edx, const String* msg,
                                                          std::uint32_t mostDerived);
std::uint32_t __fastcall CSoundData_IsEnabled(const void* self, void* edx);
std::uint32_t __fastcall CSoundData_GetSetNumber(const void* self, void* edx);

using SoundFileConstructFn = decltype(&DirectX_CSoundFile_Construct);
using SoundFileDestroyFn = decltype(&DirectX_CSoundFile_Destroy);
using MemorySoundFileConstructFn = decltype(&DirectX_CMemorySoundFile_Construct);
using SoundFileRewindFn = decltype(&DirectX_CMemorySoundFile_Rewind);
using SoundFileGetFormatFn = decltype(&DirectX_CMemorySoundFile_GetFormat);
using SoundFileReadFn = decltype(&DirectX_CMemorySoundFile_Read);
using DSExceptionConstructFn = decltype(&DirectX_CDirectSoundException_Construct);
using DSExceptionDescribeFn = decltype(&DirectX_CDirectSoundException_Describe);
using ExceptionDestroyFn = decltype(&DirectX_CDirectSoundException_Destroy);
using AppNotActiveConstructFn = decltype(&DirectX_CAppNotActiveException_Construct);
using SndGetterFn = decltype(&CSoundData_IsEnabled);

extern SoundFileConstructFn Original_DirectX_CSoundFile_Construct;
extern SoundFileDestroyFn Original_DirectX_CSoundFile_Destroy;
extern MemorySoundFileConstructFn Original_DirectX_CMemorySoundFile_Construct;
extern SoundFileRewindFn Original_DirectX_CMemorySoundFile_Rewind;
extern SoundFileGetFormatFn Original_DirectX_CMemorySoundFile_GetFormat;
extern SoundFileReadFn Original_DirectX_CMemorySoundFile_Read;
extern DSExceptionConstructFn Original_DirectX_CDirectSoundException_Construct;
extern DSExceptionDescribeFn Original_DirectX_CDirectSoundException_Describe;
extern ExceptionDestroyFn Original_DirectX_CDirectSoundException_Destroy;
extern ExceptionDestroyFn Original_DirectX_CAppNotActiveException_Destroy;
extern AppNotActiveConstructFn Original_DirectX_CAppNotActiveException_Construct;
extern SndGetterFn Original_CSoundData_IsEnabled;
extern SndGetterFn Original_CSoundData_GetSetNumber;

namespace {

struct Functions {
    SoundFileConstructFn fileConstruct;
    SoundFileDestroyFn fileDestroy;
    MemorySoundFileConstructFn memoryConstruct;
    SoundFileRewindFn rewind;
    SoundFileGetFormatFn getFormat;
    SoundFileReadFn read;
    DSExceptionConstructFn exceptionConstruct;
    DSExceptionDescribeFn describe;
    ExceptionDestroyFn exceptionDestroy;
    ExceptionDestroyFn notActiveDestroy;
    AppNotActiveConstructFn notActiveConstruct;
    SndGetterFn isEnabled;
    SndGetterFn setNumber;
};

Functions Originals() {
    return {Original_DirectX_CSoundFile_Construct,         Original_DirectX_CSoundFile_Destroy,
            Original_DirectX_CMemorySoundFile_Construct,   Original_DirectX_CMemorySoundFile_Rewind,
            Original_DirectX_CMemorySoundFile_GetFormat,   Original_DirectX_CMemorySoundFile_Read,
            Original_DirectX_CDirectSoundException_Construct, Original_DirectX_CDirectSoundException_Describe,
            Original_DirectX_CDirectSoundException_Destroy, Original_DirectX_CAppNotActiveException_Destroy,
            Original_DirectX_CAppNotActiveException_Construct, Original_CSoundData_IsEnabled,
            Original_CSoundData_GetSetNumber};
}

Functions Ours() {
    return {&DirectX_CSoundFile_Construct,         &DirectX_CSoundFile_Destroy,
            &DirectX_CMemorySoundFile_Construct,   &DirectX_CMemorySoundFile_Rewind,
            &DirectX_CMemorySoundFile_GetFormat,   &DirectX_CMemorySoundFile_Read,
            &DirectX_CDirectSoundException_Construct, &DirectX_CDirectSoundException_Describe,
            &DirectX_CDirectSoundException_Destroy, &DirectX_CAppNotActiveException_Destroy,
            &DirectX_CAppNotActiveException_Construct, &CSoundData_IsEnabled,
            &CSoundData_GetSetNumber};
}

enum Op : std::uint32_t {
    kFileConstruct,
    kFileDestroy,
    kMemoryConstruct,
    kRewind,
    kGetFormat,
    kRead,
    kExceptionConstruct,
    kDescribe,
    kExceptionDestroy,
    kNotActiveDestroy,
    kNotActiveConstruct,
    kIsEnabled,
    kSetNumber,
    kOpCount,
};

constexpr std::size_t kGuard = 8;
constexpr std::size_t kObject = 0x150;  // CSoundData's size and more
constexpr std::size_t kData = 0x800;
constexpr std::size_t kOut = 0x200;

// A string whose buffer has the given count byte (or no buffer).
String MakeString(std::uint32_t length, std::uint8_t count, const char* text, bool none, std::uint8_t allocator) {
    String s{};
    s.allocator = allocator;
    if (none) {
        return s;
    }
    auto* block = static_cast<char*>(msvc6::GameOperatorNew(length + 2));
    block[0] = static_cast<char>(count);
    std::memcpy(block + 1, text, length);
    block[1 + length] = 0;
    s.ptr = block + 1;
    s.len = length;
    s.res = length;
    return s;
}

struct StringSpec {
    std::uint32_t length;
    std::uint8_t count;
    bool none;
    std::uint8_t allocator;
    char text[64];
};

StringSpec RandomString(test::Rng& rng) {
    StringSpec spec{};
    spec.length = rng.Chance(1, 8) ? 0 : rng.Below(60);
    for (std::uint32_t k = 0; k < spec.length; ++k) {
        spec.text[k] = static_cast<char>(1 + rng.Below(255));
    }
    static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
    spec.count = kCounts[rng.Below(sizeof(kCounts))];
    spec.none = rng.Chance(1, 10);
    spec.allocator = static_cast<std::uint8_t>(rng.Next32());
    return spec;
}

String Build(const StringSpec& spec) {
    return MakeString(spec.length, spec.count, spec.text, spec.none, spec.allocator);
}

// Compares two strings by content; `shareA`/`shareB`: the buffers of the
// arguments (whether the result shares them).
bool SameString(test::Context& ctx, const String& a, const String& b, bool allocator, const char* shareA = nullptr,
                const char* shareB = nullptr) {
    bool same = S3RE_EXPECT_EQ(a.ptr == nullptr, b.ptr == nullptr);
    same &= S3RE_EXPECT_EQ(shareA != nullptr && a.ptr == shareA, shareB != nullptr && b.ptr == shareB);
    same &= S3RE_EXPECT_EQ(a.len, b.len);
    same &= S3RE_EXPECT_EQ(a.res, b.res);
    if (allocator) {
        same &= S3RE_EXPECT_EQ(a.allocator, b.allocator);
    }
    if (same && a.ptr != nullptr) {
        same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(a.ptr[-1]), static_cast<std::uint8_t>(b.ptr[-1]));
        same &= S3RE_EXPECT_MEM_EQ(a.ptr, b.ptr, a.len + 1);
    }
    return same;
}

// The count byte of a string buffer that survives (shared: 1..0xfd).
std::uint32_t SurvivingCount(const char* buffer, std::uint8_t count) {
    if (buffer == nullptr || count == 0 || count >= 0xfe) {
        return 0xffff;
    }
    return static_cast<std::uint8_t>(buffer[-1]);
}

// Two name slots 256 bytes apart: the same low address byte, which the
// copy constructor takes as allocator byte.
alignas(256) std::uint8_t g_names[512];

std::uint32_t RandomHr(test::Rng& rng) {
    static const std::uint32_t kCodes[] = {0, 1, 0x88780096, 0x88780078, 0x8878000a, 0x80004005, 0x88780064,
                                           0x80070057, 0x8878001e, 0x88780032, 0x8878008c};
    return rng.Chance(1, 5) ? rng.Next32() : kCodes[rng.Below(sizeof(kCodes) / 4)];
}

framework::WaveFormat RandomFormat(test::Rng& rng) {
    framework::WaveFormat format{};
    rng.Fill(&format, sizeof(format));
    static const std::uint16_t kBits[] = {16, 16, 8, 24, 0};
    format.bitsPerSample = rng.Chance(1, 6) ? static_cast<std::uint16_t>(rng.Next32()) : kBits[rng.Below(5)];
    static const std::uint32_t kRates[] = {22050, 22049, 44100, 11025, 22051, 8000};
    if (rng.Chance(4, 5)) {
        format.samplesPerSec = kRates[rng.Below(6)];
    }
    return format;
}

bool CompareOnce(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    const Op op = static_cast<Op>(rng.Below(kOpCount));
    const Functions originals = Originals();
    std::vector<std::uint8_t> initial(kObject + 2 * kGuard);
    rng.Fill(initial.data(), initial.size());
    auto* a = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(static_cast<std::uint32_t>(initial.size())));
    auto* b = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(static_cast<std::uint32_t>(initial.size())));
    std::uint8_t* const objectA = a + kGuard;
    std::uint8_t* const objectB = b + kGuard;
    bool same = true;
    // Compares the object bytes outside [skipFrom, skipTo).
    auto compareBytes = [&](std::size_t skipFrom, std::size_t skipTo) {
        same &= S3RE_EXPECT_MEM_EQ(a, b, kGuard + skipFrom);
        same &= S3RE_EXPECT_MEM_EQ(objectA + skipTo, objectB + skipTo, kObject + kGuard - skipTo);
    };
    auto setup = [&]() {
        std::memcpy(a, initial.data(), initial.size());
        std::memcpy(b, initial.data(), initial.size());
    };
    setup();
    switch (op) {
        case kFileConstruct: {
            const StringSpec spec = RandomString(rng);
            const std::size_t slot = 4 * rng.Below(60);
            auto* nameA = reinterpret_cast<String*>(g_names + slot);
            auto* nameB = reinterpret_cast<String*>(g_names + 256 + slot);
            *nameA = Build(spec);
            *nameB = Build(spec);
            const auto ra = reinterpret_cast<std::uintptr_t>(originals.fileConstruct(objectA, nullptr, nameA));
            const auto rb = reinterpret_cast<std::uintptr_t>(ours.fileConstruct(objectB, nullptr, nameB));
            same &= S3RE_EXPECT_EQ(ra - reinterpret_cast<std::uintptr_t>(objectA),
                                   rb - reinterpret_cast<std::uintptr_t>(objectB));
            compareBytes(4, 0x14);
            same &= SameString(ctx, *reinterpret_cast<String*>(objectA + 4), *reinterpret_cast<String*>(objectB + 4),
                               true, nameA->ptr, nameB->ptr);
            same &= SameString(ctx, *nameA, *nameB, true);
            break;
        }
        case kFileDestroy:
        case kExceptionDestroy:
        case kNotActiveDestroy: {
            const std::size_t at = op == kFileDestroy ? 4 : 8;
            const StringSpec spec = RandomString(rng);
            String sa = Build(spec);
            String sb = Build(spec);
            std::memcpy(objectA + at, &sa, sizeof(sa));
            std::memcpy(objectB + at, &sb, sizeof(sb));
            const ExceptionDestroyFn fa = op == kFileDestroy       ? originals.fileDestroy
                                          : op == kExceptionDestroy ? originals.exceptionDestroy
                                                                    : originals.notActiveDestroy;
            const ExceptionDestroyFn fb = op == kFileDestroy       ? ours.fileDestroy
                                          : op == kExceptionDestroy ? ours.exceptionDestroy
                                                                    : ours.notActiveDestroy;
            fa(objectA, nullptr);
            fb(objectB, nullptr);
            compareBytes(0, 0);
            same &= S3RE_EXPECT_EQ(SurvivingCount(sa.ptr, spec.count), SurvivingCount(sb.ptr, spec.count));
            break;
        }
        case kMemoryConstruct: {
            framework::WaveFormat format = RandomFormat(rng);
            const std::uint32_t data = rng.Next32();
            const std::uint32_t bytes = rng.Chance(1, 2) ? rng.Below(400000) : rng.Next32();
            static const std::uint32_t kModes[] = {1, 2, 4, 4, 2, 0, 3};
            const std::uint32_t mode = rng.Chance(1, 8) ? rng.Next32() : kModes[rng.Below(7)];
            // The format at the same low address byte (its stack slot gives
            // the temporaries' allocator bytes).
            const std::size_t slot = 4 * rng.Below(55);
            std::memcpy(g_names + slot, &format, sizeof(format));
            std::memcpy(g_names + 256 + slot, &format, sizeof(format));
            const auto ra = reinterpret_cast<std::uintptr_t>(originals.memoryConstruct(
                objectA, nullptr, g_names + slot, reinterpret_cast<void*>(data), bytes, mode));
            const auto rb = reinterpret_cast<std::uintptr_t>(ours.memoryConstruct(
                objectB, nullptr, g_names + 256 + slot, reinterpret_cast<void*>(data), bytes, mode));
            same &= S3RE_EXPECT_EQ(ra - reinterpret_cast<std::uintptr_t>(objectA),
                                   rb - reinterpret_cast<std::uintptr_t>(objectB));
            compareBytes(4, 0x14);
            // The name's allocator byte is a known difference (the header).
            same &= SameString(ctx, *reinterpret_cast<String*>(objectA + 4), *reinterpret_cast<String*>(objectB + 4),
                               false);
            same &= S3RE_EXPECT_MEM_EQ(g_names + slot, g_names + 256 + slot, sizeof(format));
            break;
        }
        case kRewind:
        case kGetFormat:
        case kRead: {
            std::vector<std::uint8_t> data(kData);
            rng.Fill(data.data(), data.size());
            static const std::uint32_t kModes[] = {1, 2, 4, 1, 2, 4, 0, 3};
            const std::uint32_t mode = kModes[rng.Below(8)];
            const std::uint32_t step = mode == 1 ? 1 : mode;
            const std::uint32_t count = rng.Chance(1, 8) ? 0 : rng.Below(kOut - 2 * kGuard);
            const std::uint32_t position = rng.Below(static_cast<std::uint32_t>(kData - count * step - 2));
            auto put = [&](std::uint8_t* object) {
                auto* file = reinterpret_cast<framework::CMemorySoundFile*>(object);
                file->data = data.data();
                file->position = position;
                file->mode = mode;
            };
            put(objectA);
            put(objectB);
            std::uint8_t outA[kOut], outB[kOut];
            rng.Fill(outA, sizeof(outA));
            std::memcpy(outB, outA, sizeof(outB));
            if (op == kRewind) {
                originals.rewind(objectA, nullptr);
                ours.rewind(objectB, nullptr);
            } else if (op == kGetFormat) {
                auto* bytesA = reinterpret_cast<std::uint32_t*>(outA + 0x40);
                auto* bytesB = reinterpret_cast<std::uint32_t*>(outB + 0x40);
                const std::uint32_t ra = originals.getFormat(objectA, nullptr, outA + kGuard, bytesA);
                const std::uint32_t rb = ours.getFormat(objectB, nullptr, outB + kGuard, bytesB);
                same &= S3RE_EXPECT_EQ(ra & 0xFFFF, rb & 0xFFFF);  // the high word: the object's address
            } else {
                const auto ra = reinterpret_cast<std::uintptr_t>(originals.read(objectA, nullptr, count, outA + kGuard));
                const auto rb = reinterpret_cast<std::uintptr_t>(ours.read(objectB, nullptr, count, outB + kGuard));
                same &= S3RE_EXPECT_EQ(ra - reinterpret_cast<std::uintptr_t>(objectA),
                                       rb - reinterpret_cast<std::uintptr_t>(objectB));
            }
            same &= S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA));
            // The data pointer is the same in both; compare everything.
            compareBytes(0, 0);
            break;
        }
        case kExceptionConstruct:
        case kNotActiveConstruct: {
            const std::uint32_t mostDerived = rng.Chance(1, 3) ? 0 : rng.Next32();
            const StringSpec spec = RandomString(rng);
            String msgA = Build(spec);
            String msgB = Build(spec);
            static const char* const kWhere[] = {"CDirectSoundBuffer::DSPlay", "", "x"};
            const char* const where = rng.Chance(1, 8) ? nullptr : kWhere[rng.Below(3)];
            const std::uint32_t hr = RandomHr(rng);
            std::uintptr_t ra, rb;
            if (op == kExceptionConstruct) {
                ra = reinterpret_cast<std::uintptr_t>(originals.exceptionConstruct(
                    objectA, nullptr, static_cast<std::int32_t>(hr), where, mostDerived));
                rb = reinterpret_cast<std::uintptr_t>(
                    ours.exceptionConstruct(objectB, nullptr, static_cast<std::int32_t>(hr), where, mostDerived));
            } else {
                ra = reinterpret_cast<std::uintptr_t>(
                    originals.notActiveConstruct(objectA, nullptr, &msgA, mostDerived));
                rb = reinterpret_cast<std::uintptr_t>(ours.notActiveConstruct(objectB, nullptr, &msgB, mostDerived));
            }
            same &= S3RE_EXPECT_EQ(ra - reinterpret_cast<std::uintptr_t>(objectA),
                                   rb - reinterpret_cast<std::uintptr_t>(objectB));
            compareBytes(8, 0x18);
            same &= SameString(ctx, *reinterpret_cast<String*>(objectA + 8), *reinterpret_cast<String*>(objectB + 8),
                               true, msgA.ptr, msgB.ptr);
            same &= SameString(ctx, msgA, msgB, true);
            break;
        }
        case kDescribe: {
            auto* exceptionA = reinterpret_cast<framework::CDirectSoundException*>(objectA);
            auto* exceptionB = reinterpret_cast<framework::CDirectSoundException*>(objectB);
            static const char* const kWhere[] = {"CDirectSoundBuffer::DSPlay", "", "CDirectSound::Connect.3"};
            exceptionA->result = exceptionB->result = static_cast<std::int32_t>(RandomHr(rng));
            exceptionA->where = exceptionB->where = rng.Chance(1, 8) ? nullptr : kWhere[rng.Below(3)];
            String resultA, resultB;
            rng.Fill(&resultA, sizeof(resultA));
            resultB = resultA;
            const auto ra = reinterpret_cast<std::uintptr_t>(originals.describe(objectA, nullptr, &resultA));
            const auto rb = reinterpret_cast<std::uintptr_t>(ours.describe(objectB, nullptr, &resultB));
            same &= S3RE_EXPECT_EQ(ra - reinterpret_cast<std::uintptr_t>(&resultA),
                                   rb - reinterpret_cast<std::uintptr_t>(&resultB));
            same &= SameString(ctx, resultA, resultB, false);  // the allocator byte: see the header
            compareBytes(0, 0);
            break;
        }
        case kIsEnabled:
        case kSetNumber: {
            const SndGetterFn fa = op == kIsEnabled ? originals.isEnabled : originals.setNumber;
            const SndGetterFn fb = op == kIsEnabled ? ours.isEnabled : ours.setNumber;
            const std::uint32_t mask = op == kIsEnabled ? 0xFF : 0xFFFFFFFF;  // AL only (the callers')
            same &= S3RE_EXPECT_EQ(fa(objectA, nullptr) & mask, fb(objectB, nullptr) & mask);
            compareBytes(0, 0);
            break;
        }
        default: break;
    }
    msvc6::GameOperatorDelete(a);
    msvc6::GameOperatorDelete(b);
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  op %u", static_cast<unsigned>(op));
    }
    return same;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        if (!CompareOnce(ctx, ours)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: mode 2 reads the low byte of each sample.
void* __fastcall WrongRead(void* self, void* edx, std::uint32_t count, void* dest) {
    auto* file = static_cast<framework::CMemorySoundFile*>(self);
    if (file->mode == 2) {
        file->data = file->data - 1;
        void* const result = DirectX_CMemorySoundFile_Read(self, edx, count, dest);
        file->data = file->data + 1;
        return result;
    }
    return DirectX_CMemorySoundFile_Read(self, edx, count, dest);
}

// Wrong: mode 4 also for 8-bit samples.
void* __fastcall WrongMemoryConstruct(void* self, void* edx, const void* format, const void* data,
                                      std::uint32_t bytes, std::uint32_t mode) {
    DirectX_CMemorySoundFile_Construct(self, edx, format, data, bytes, mode);
    auto* file = static_cast<framework::CMemorySoundFile*>(self);
    if (mode == 4 && file->mode == 1) {
        file->mode = 4;
    }
    return self;
}

// For the emulator's self-test (specs/self_test.py): wrong next to the
// ignored allocator bytes (the padding byte after the name's / result's
// allocator byte).
void* __fastcall WrongMemorySoundFileName(void* self, void* edx, const void* format, const void* data,
                                          std::uint32_t bytes, std::uint32_t mode) {
    DirectX_CMemorySoundFile_Construct(self, edx, format, data, bytes, mode);
    static_cast<std::uint8_t*>(self)[5] ^= 1;
    return self;
}
String* __fastcall WrongDSExceptionDescribe(void* self, void* edx, String* result) {
    DirectX_CDirectSoundException_Describe(self, edx, result);
    reinterpret_cast<std::uint8_t*>(result)[1] ^= 1;
    return result;
}

}  // namespace

S3RE_TEST(SoundFiles_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(SoundFiles_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.read = &WrongRead;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
    S3RE_EXPECT(&WrongMemorySoundFileName != nullptr && &WrongDSExceptionDescribe != nullptr);
    wrong = Ours();
    wrong.memoryConstruct = &WrongMemoryConstruct;
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2, wrong, 3000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
