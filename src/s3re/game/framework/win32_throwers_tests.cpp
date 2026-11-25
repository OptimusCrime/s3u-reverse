// Differential tests of win32_throwers.cpp (session 58, wip/framework-errors).
//
// Every case runs the original (Original_* trampoline) and ours on identical
// inputs: object buffers with guard bytes, message/location strings with
// their own buffers and count bytes (0, shared, frozen), random codes
// (often Win32 HRESULTs and small error numbers). FormatMessageA, LocalFree
// and mciGetErrorStringA are replaced (their import slots) by fakes that log
// every call (arguments by value, texts by index) and hand out texts drawn
// from an Rng seeded the same for both runs. Compared: EAX, the object bytes,
// the strings by content and sharing, the count bytes of buffers that
// survive, the fake calls, and for the throwers the thrown object and throw
// info. The Describe results' allocator byte is not compared (see the
// header of win32_throwers.cpp).

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using msvc6::String;

using HResultCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t code,
                                         const String* location, std::uint32_t mostDerived);
using CodeCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t code,
                                      std::uint32_t mostDerived);
using MessageCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
using ExceptionDescribeFn = String*(__fastcall*)(void* self, void* edx, String* result);
using ExceptionDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThrowLocationFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line, std::uint32_t code,
                                          const String* location);
using ThrowCodeFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line, std::uint32_t code);
using ThrowPlaceFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);

void* __fastcall Win32_CHResult_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                          const String* location, std::uint32_t mostDerived);
String* __fastcall Win32_CHResult_Describe(void* self, void* edx, String* result);
std::uint32_t __fastcall Win32_CHResult_Destroy(void* self, void* edx);
void __fastcall Win32_ThrowWin32Error(const char* text, const char* file, std::int32_t line, std::uint32_t error,
                                      const String* location);
void __fastcall Win32_ThrowHResult(const char* text, const char* file, std::int32_t line, std::uint32_t hr,
                                   const String* location);
void* __fastcall Win32_CTimeout_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void __fastcall Win32_ThrowTimeout(const char* text, const char* file, std::int32_t line);
void* __fastcall Win32_CMMException_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                              std::uint32_t mostDerived);
String* __fastcall Win32_CMMException_Describe(void* self, void* edx, String* result);
void __fastcall Win32_ThrowMMError(const char* text, const char* file, std::int32_t line, std::uint32_t code);
void* __fastcall Win32_CMCIException_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                               std::uint32_t mostDerived);
String* __fastcall Win32_CMCIException_Describe(void* self, void* edx, String* result);
void __fastcall Win32_ThrowMCIError(const char* text, const char* file, std::int32_t line, std::uint32_t code);
std::uint32_t __fastcall Win32_CException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CRunTimeError_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_COutOfMemory_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CProgrammingError_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CAssertmentFailed_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CTimeout_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CMMException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CStructuredException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectInputException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectDrawException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectPlayException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Lobby_CDataError_Destroy(void* self, void* edx);

extern HResultCtorFn Original_Win32_CHResult_Construct;
extern ExceptionDescribeFn Original_Win32_CHResult_Describe;
extern ExceptionDestroyFn Original_Win32_CHResult_Destroy;
extern ThrowLocationFn Original_Win32_ThrowWin32Error;
extern ThrowLocationFn Original_Win32_ThrowHResult;
extern MessageCtorFn Original_Win32_CTimeout_Construct;
extern ThrowPlaceFn Original_Win32_ThrowTimeout;
extern CodeCtorFn Original_Win32_CMMException_Construct;
extern ExceptionDescribeFn Original_Win32_CMMException_Describe;
extern ThrowCodeFn Original_Win32_ThrowMMError;
extern CodeCtorFn Original_Win32_CMCIException_Construct;
extern ExceptionDescribeFn Original_Win32_CMCIException_Describe;
extern ThrowCodeFn Original_Win32_ThrowMCIError;
extern ExceptionDestroyFn Original_Win32_CException_Destroy;
extern ExceptionDestroyFn Original_Win32_CRunTimeError_Destroy;
extern ExceptionDestroyFn Original_Win32_COutOfMemory_Destroy;
extern ExceptionDestroyFn Original_Win32_CProgrammingError_Destroy;
extern ExceptionDestroyFn Original_Win32_CAssertmentFailed_Destroy;
extern ExceptionDestroyFn Original_Win32_CTimeout_Destroy;
extern ExceptionDestroyFn Original_Win32_CMMException_Destroy;
extern ExceptionDestroyFn Original_Win32_CStructuredException_Destroy;
extern ExceptionDestroyFn Original_DirectX_CDirectInputException_Destroy;
extern ExceptionDestroyFn Original_DirectX_CDirectDrawException_Destroy;
extern ExceptionDestroyFn Original_DirectX_CDirectPlayException_Destroy;
extern ExceptionDestroyFn Original_Lobby_CDataError_Destroy;

namespace {

constexpr std::size_t kGuard = 8;
constexpr std::size_t kObject = 0x2c;  // the largest: CHResult

struct Functions {
    HResultCtorFn hresultConstruct;
    ExceptionDescribeFn hresultDescribe;
    ExceptionDestroyFn hresultDestroy;
    ThrowLocationFn throwWin32Error;
    ThrowLocationFn throwHResult;
    MessageCtorFn timeoutConstruct;
    ThrowPlaceFn throwTimeout;
    CodeCtorFn mmConstruct;
    ExceptionDescribeFn mmDescribe;
    ThrowCodeFn throwMMError;
    CodeCtorFn mciConstruct;
    ExceptionDescribeFn mciDescribe;
    ThrowCodeFn throwMCIError;
    ExceptionDestroyFn messageDestroy[12];
};

Functions Originals() {
    return {Original_Win32_CHResult_Construct,
            Original_Win32_CHResult_Describe,
            Original_Win32_CHResult_Destroy,
            Original_Win32_ThrowWin32Error,
            Original_Win32_ThrowHResult,
            Original_Win32_CTimeout_Construct,
            Original_Win32_ThrowTimeout,
            Original_Win32_CMMException_Construct,
            Original_Win32_CMMException_Describe,
            Original_Win32_ThrowMMError,
            Original_Win32_CMCIException_Construct,
            Original_Win32_CMCIException_Describe,
            Original_Win32_ThrowMCIError,
            {Original_Win32_CException_Destroy, Original_Win32_CRunTimeError_Destroy,
             Original_Win32_COutOfMemory_Destroy, Original_Win32_CProgrammingError_Destroy,
             Original_Win32_CAssertmentFailed_Destroy, Original_Win32_CTimeout_Destroy,
             Original_Win32_CMMException_Destroy, Original_Win32_CStructuredException_Destroy,
             Original_DirectX_CDirectInputException_Destroy, Original_DirectX_CDirectDrawException_Destroy,
             Original_DirectX_CDirectPlayException_Destroy, Original_Lobby_CDataError_Destroy}};
}

Functions Ours() {
    return {&Win32_CHResult_Construct,
            &Win32_CHResult_Describe,
            &Win32_CHResult_Destroy,
            &Win32_ThrowWin32Error,
            &Win32_ThrowHResult,
            &Win32_CTimeout_Construct,
            &Win32_ThrowTimeout,
            &Win32_CMMException_Construct,
            &Win32_CMMException_Describe,
            &Win32_ThrowMMError,
            &Win32_CMCIException_Construct,
            &Win32_CMCIException_Describe,
            &Win32_ThrowMCIError,
            {&Win32_CException_Destroy, &Win32_CRunTimeError_Destroy, &Win32_COutOfMemory_Destroy,
             &Win32_CProgrammingError_Destroy, &Win32_CAssertmentFailed_Destroy, &Win32_CTimeout_Destroy,
             &Win32_CMMException_Destroy, &Win32_CStructuredException_Destroy, &DirectX_CDirectInputException_Destroy,
             &DirectX_CDirectDrawException_Destroy, &DirectX_CDirectPlayException_Destroy,
             &Lobby_CDataError_Destroy}};
}

// ---------------------------------------------------------------- the fakes

struct FakeSystem {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::vector<std::vector<char>> texts;  // handed out by FormatMessageA
};
FakeSystem* g_fake = nullptr;

void RandomChars(test::Rng& rng, char* out, std::uint32_t length) {
    for (std::uint32_t k = 0; k < length; ++k) {
        out[k] = static_cast<char>(1 + rng.Below(255));
    }
    out[length] = 0;
}

std::uint32_t __stdcall FakeFormatMessage(std::uint32_t flags, const void* source, std::uint32_t id,
                                          std::uint32_t language, char* buffer, std::uint32_t size, void* arguments) {
    FakeSystem& fake = *g_fake;
    fake.log.insert(fake.log.end(), {0xF0u, flags, reinterpret_cast<std::uint32_t>(source), id, language, size,
                                     reinterpret_cast<std::uint32_t>(arguments)});
    // A system text, mostly ending in CR LF; sometimes 0 or 1 characters.
    const std::uint32_t length = fake.rng.Chance(1, 6) ? fake.rng.Below(2) : fake.rng.Below(40);
    std::vector<char> text(length + 3);
    RandomChars(fake.rng, text.data(), length);
    if (length >= 2 && fake.rng.Chance(3, 4)) {
        text[length - 2] = '\r';
        text[length - 1] = '\n';
    }
    fake.texts.push_back(text);
    // The pointer is always written (also on failure, see the header of
    // win32_throwers.cpp).
    *reinterpret_cast<char**>(buffer) = fake.texts.back().data();
    return fake.rng.Chance(1, 4) ? 0 : length;
}

void* __stdcall FakeLocalFree(void* memory) {
    FakeSystem& fake = *g_fake;
    std::uint32_t index = 0xFFFF;
    for (std::size_t k = 0; k < fake.texts.size(); ++k) {
        if (fake.texts[k].data() == memory) {
            index = static_cast<std::uint32_t>(k);
        }
    }
    fake.log.insert(fake.log.end(), {0xF1u, index});
    return nullptr;
}

int __stdcall FakeMciGetErrorString(std::uint32_t error, char* text, std::uint32_t size) {
    FakeSystem& fake = *g_fake;
    // The buffer as the function prepared it (its start and its end).
    std::uint32_t start = 0, end = 0;
    std::memcpy(&start, text, 4);
    std::memcpy(&end, text + size - 4, 4);
    fake.log.insert(fake.log.end(), {0xF2u, error, size, start, end});
    if (fake.rng.Chance(3, 4)) {
        RandomChars(fake.rng, text, fake.rng.Below(50));
    }
    return static_cast<int>(fake.rng.Below(2));
}

class InstalledFakes {
public:
    explicit InstalledFakes(FakeSystem* fake) {
        g_fake = fake;
        const auto format = reinterpret_cast<std::uintptr_t>(&FakeFormatMessage);
        const auto localFree = reinterpret_cast<std::uintptr_t>(&FakeLocalFree);
        const auto mci = reinterpret_cast<std::uintptr_t>(&FakeMciGetErrorString);
        test::PatchBytes(g_importFormatMessageA.address(), &format, 4);
        test::PatchBytes(g_importLocalFree.address(), &localFree, 4);
        test::PatchBytes(g_importMciGetErrorStringA.address(), &mci, 4);
    }
    ~InstalledFakes() {
        test::UnpatchBytes(g_importMciGetErrorStringA.address());
        test::UnpatchBytes(g_importLocalFree.address());
        test::UnpatchBytes(g_importFormatMessageA.address());
        g_fake = nullptr;
    }
    InstalledFakes(const InstalledFakes&) = delete;
    InstalledFakes& operator=(const InstalledFakes&) = delete;
};

// ---------------------------------------------------------------- inputs

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
    RandomChars(rng, spec.text, spec.length);
    static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
    spec.count = kCounts[rng.Below(sizeof(kCounts))];
    spec.none = rng.Chance(1, 10);
    spec.allocator = static_cast<std::uint8_t>(rng.Next32());
    return spec;
}

String Build(const StringSpec& spec) {
    String s{};
    s.allocator = spec.allocator;
    if (spec.none) {
        return s;
    }
    auto* block = static_cast<char*>(msvc6::GameOperatorNew(spec.length + 2));
    block[0] = static_cast<char>(spec.count);
    std::memcpy(block + 1, spec.text, spec.length + 1);
    s.ptr = block + 1;
    s.len = spec.length;
    s.res = spec.length;
    return s;
}

std::uint32_t RandomCode(test::Rng& rng) {
    static const std::uint32_t kCodes[] = {0, 1, 2, 5, 0x57, 0x80070002, 0x80070005, 0x8007000e, 0x80004005,
                                           0x88780078, 0x00070005, 0x1fff0000, 0x80060005, 0x106, 0x113, 0x101};
    if (rng.Chance(1, 4)) {
        return rng.Next32();
    }
    if (rng.Chance(1, 4)) {
        return (rng.Next32() & 0xE000FFFF) | 0x70000;  // other Win32 HRESULTs
    }
    return kCodes[rng.Below(sizeof(kCodes) / 4)];
}

// Compares two strings by content; `shareA`/`shareB`: argument buffers the
// result may share.
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

// The count byte of an argument buffer that survives (shared: 1..0xfd).
std::uint32_t SurvivingCount(const char* buffer, std::uint8_t count) {
    if (buffer == nullptr || count == 0 || count >= 0xfe) {
        return 0xffff;
    }
    return static_cast<std::uint8_t>(buffer[-1]);
}

// ---------------------------------------------------------------- throwing

struct Thrown {
    bool threw = false;
    std::uint32_t info = 0;
    void* object = nullptr;
};

int ThrowFilter(EXCEPTION_POINTERS* info, Thrown* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    out->object = *reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    out->info = static_cast<std::uint32_t>(record->ExceptionInformation[2]);
    return EXCEPTION_EXECUTE_HANDLER;
}

struct ThrowCall {
    int kind;  // 0 ThrowWin32Error, 1 ThrowHResult, 2 ThrowTimeout, 3 ThrowMMError, 4 ThrowMCIError
    const char* text;
    const char* file;
    std::int32_t line;
    std::uint32_t code;
    const String* location;
};

Thrown Guarded(const Functions& f, const ThrowCall& call) {
    Thrown out;
    __try {
        switch (call.kind) {
            case 0: f.throwWin32Error(call.text, call.file, call.line, call.code, call.location); break;
            case 1: f.throwHResult(call.text, call.file, call.line, call.code, call.location); break;
            case 2: f.throwTimeout(call.text, call.file, call.line); break;
            case 3: f.throwMMError(call.text, call.file, call.line, call.code); break;
            default: f.throwMCIError(call.text, call.file, call.line, call.code); break;
        }
    } __except (ThrowFilter(GetExceptionInformation(), &out)) {
    }
    return out;
}

// ---------------------------------------------------------------- one case

enum Op : std::uint32_t {
    kHResultConstruct,
    kHResultDescribe,
    kHResultDestroy,
    kTimeoutConstruct,
    kMMConstruct,
    kMMDescribe,
    kMCIConstruct,
    kMCIDescribe,
    kMessageDestroy,
    kThrow,
    kOpCount,
};

using framework::CException;
using framework::CHResult;

String& MessageOf(std::uint8_t* object) {
    return reinterpret_cast<CException*>(object)->message;
}
String& LocationOf(std::uint8_t* object) {
    return reinterpret_cast<CHResult*>(object)->location;
}

bool CompareOnce(test::Context& ctx, const Functions& ours, int index) {
    test::Rng& rng = ctx.rng();
    const Op op = static_cast<Op>(rng.Below(kOpCount));
    const Functions originals = Originals();
    std::uint8_t initial[kObject + 2 * kGuard];
    rng.Fill(initial, sizeof(initial));
    std::uint8_t a[sizeof(initial)], b[sizeof(initial)];
    std::memcpy(a, initial, sizeof(a));
    std::memcpy(b, initial, sizeof(b));
    std::uint8_t* const objectA = a + kGuard;
    std::uint8_t* const objectB = b + kGuard;
    const std::uint64_t fakeSeed = rng.Next64();
    FakeSystem fakeA, fakeB;
    fakeA.rng = test::Rng(fakeSeed);
    fakeB.rng = test::Rng(fakeSeed);
    const StringSpec message = RandomString(rng);
    const StringSpec location = RandomString(rng);
    const std::uint32_t code = RandomCode(rng);
    const std::uint32_t mostDerived = rng.Chance(1, 3) ? 0 : (rng.Chance(1, 2) ? 1 : rng.Next32());
    String msgA = Build(message), msgB = Build(message);
    String locA = Build(location), locB = Build(location);
    bool same = true;
    // The object's strings compared by content, then the rest bytewise.
    auto compareObject = [&](bool hasLocation, const char* shareA, const char* shareB, const char* shareLocA,
                             const char* shareLocB) {
        same &= SameString(ctx, MessageOf(objectA), MessageOf(objectB), true, shareA, shareB);
        MessageOf(objectA).ptr = MessageOf(objectB).ptr = nullptr;
        if (hasLocation) {
            same &= SameString(ctx, LocationOf(objectA), LocationOf(objectB), true, shareLocA, shareLocB);
            LocationOf(objectA).ptr = LocationOf(objectB).ptr = nullptr;
        }
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
    };
    auto compareResults = [&](String& ra, String& rb) {
        same &= SameString(ctx, ra, rb, /*allocator=*/false);
    };
    auto prepareObject = [&](bool hasLocation) {
        std::memcpy(objectA + 8, &msgA, sizeof(String));
        std::memcpy(objectB + 8, &msgB, sizeof(String));
        if (hasLocation) {
            std::memcpy(objectA + 0x1c, &locA, sizeof(String));
            std::memcpy(objectB + 0x1c, &locB, sizeof(String));
        }
        std::memcpy(objectA + 0x18, &code, 4);
        std::memcpy(objectB + 0x18, &code, 4);
    };
    switch (op) {
        case kHResultConstruct:
        case kTimeoutConstruct:
        case kMMConstruct:
        case kMCIConstruct: {
            void* ra = nullptr;
            void* rb = nullptr;
            {
                InstalledFakes installed(&fakeA);
                if (op == kHResultConstruct) {
                    ra = originals.hresultConstruct(objectA, nullptr, &msgA, code, &locA, mostDerived);
                } else if (op == kTimeoutConstruct) {
                    ra = originals.timeoutConstruct(objectA, nullptr, &msgA, mostDerived);
                } else if (op == kMMConstruct) {
                    ra = originals.mmConstruct(objectA, nullptr, &msgA, code, mostDerived);
                } else {
                    ra = originals.mciConstruct(objectA, nullptr, &msgA, code, mostDerived);
                }
            }
            {
                InstalledFakes installed(&fakeB);
                if (op == kHResultConstruct) {
                    rb = ours.hresultConstruct(objectB, nullptr, &msgB, code, &locB, mostDerived);
                } else if (op == kTimeoutConstruct) {
                    rb = ours.timeoutConstruct(objectB, nullptr, &msgB, mostDerived);
                } else if (op == kMMConstruct) {
                    rb = ours.mmConstruct(objectB, nullptr, &msgB, code, mostDerived);
                } else {
                    rb = ours.mciConstruct(objectB, nullptr, &msgB, code, mostDerived);
                }
            }
            same &= S3RE_EXPECT(ra == objectA && rb == objectB);
            same &= SameString(ctx, msgA, msgB, true);
            same &= SameString(ctx, locA, locB, true);
            compareObject(op == kHResultConstruct, msgA.ptr, msgB.ptr, locA.ptr, locB.ptr);
            break;
        }
        case kHResultDescribe:
        case kMMDescribe:
        case kMCIDescribe: {
            prepareObject(op == kHResultDescribe);
            String resultA, resultB;
            std::memset(&resultA, 0xcc, sizeof(resultA));
            std::memset(&resultB, 0xcc, sizeof(resultB));
            const ExceptionDescribeFn fa = op == kHResultDescribe ? originals.hresultDescribe
                                           : op == kMMDescribe    ? originals.mmDescribe
                                                                  : originals.mciDescribe;
            const ExceptionDescribeFn fb = op == kHResultDescribe ? ours.hresultDescribe
                                           : op == kMMDescribe    ? ours.mmDescribe
                                                                  : ours.mciDescribe;
            String* pa = nullptr;
            String* pb = nullptr;
            {
                InstalledFakes installed(&fakeA);
                pa = fa(objectA, nullptr, &resultA);
            }
            {
                InstalledFakes installed(&fakeB);
                pb = fb(objectB, nullptr, &resultB);
            }
            same &= S3RE_EXPECT(pa == &resultA && pb == &resultB);
            compareResults(resultA, resultB);
            compareObject(op == kHResultDescribe, msgA.ptr, msgB.ptr, locA.ptr, locB.ptr);
            break;
        }
        case kHResultDestroy:
        case kMessageDestroy: {
            const bool hresult = op == kHResultDestroy;
            prepareObject(hresult);
            const std::uint32_t which = rng.Below(12);
            const ExceptionDestroyFn fa = hresult ? originals.hresultDestroy : originals.messageDestroy[which];
            const ExceptionDestroyFn fb = hresult ? ours.hresultDestroy : ours.messageDestroy[which];
            fa(objectA, nullptr);
            fb(objectB, nullptr);
            same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
            same &= S3RE_EXPECT_EQ(SurvivingCount(msgA.ptr, message.count), SurvivingCount(msgB.ptr, message.count));
            if (hresult) {
                same &= S3RE_EXPECT_EQ(SurvivingCount(locA.ptr, location.count),
                                       SurvivingCount(locB.ptr, location.count));
            }
            break;
        }
        default: {
            char text[48], file[48];
            RandomChars(rng, text, rng.Below(40));
            RandomChars(rng, file, rng.Below(40));
            ThrowCall call{};
            call.kind = static_cast<int>(rng.Below(5));
            call.text = rng.Chance(1, 6) ? nullptr : text;
            call.file = rng.Chance(1, 8) ? nullptr : file;
            call.line = static_cast<std::int32_t>(rng.Chance(1, 2) ? rng.Below(5000) : rng.Next32());
            call.code = code;
            ThrowCall callA = call, callB = call;
            callA.location = &locA;
            callB.location = &locB;
            Thrown ta, tb;
            {
                InstalledFakes installed(&fakeA);
                ta = Guarded(originals, callA);
            }
            {
                InstalledFakes installed(&fakeB);
                tb = Guarded(ours, callB);
            }
            same &= S3RE_EXPECT(ta.threw && tb.threw);
            same &= S3RE_EXPECT_EQ(ta.info, tb.info);
            same &= S3RE_EXPECT(ta.object != nullptr && tb.object != nullptr);
            if (same) {
                auto* oa = static_cast<std::uint8_t*>(ta.object);
                auto* ob = static_cast<std::uint8_t*>(tb.object);
                same &= S3RE_EXPECT_MEM_EQ(oa, ob, 9);  // vtable, virtual-base pointer, allocator
                // Not the message's count byte: the original's unwinding frees
                // its reference of the shared buffer, ours never (the known
                // difference in the header).
                const String& ma = MessageOf(oa);
                const String& mb = MessageOf(ob);
                same &= S3RE_EXPECT_EQ(ma.len, mb.len);
                same &= S3RE_EXPECT_EQ(ma.res, mb.res);
                same &= S3RE_EXPECT_EQ(ma.ptr == nullptr, mb.ptr == nullptr);
                if (same && ma.ptr != nullptr) {
                    same &= S3RE_EXPECT_MEM_EQ(ma.ptr, mb.ptr, ma.len + 1);
                }
                if (call.kind != 2) {
                    same &= S3RE_EXPECT_MEM_EQ(oa + 0x18, ob + 0x18, 4);  // the code
                }
                if (call.kind <= 1) {
                    same &= SameString(ctx, LocationOf(oa), LocationOf(ob), true, locA.ptr, locB.ptr);
                    same &= SameString(ctx, locA, locB, true);
                }
            }
            break;
        }
    }
    same &= S3RE_EXPECT(fakeA.log == fakeB.log);
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  case %d, op %u, code %08x, message %u/%u, mostDerived %08x", index, op, code,
                 message.length, message.count, mostDerived);
    }
    return same;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        if (!CompareOnce(ctx, ours, i)) {
            return;
        }
    }
}

// ---------------------------------------------------------------- wrong ones

// Wrong: the system text keeps its CR LF (the length is not trimmed).
String* __fastcall WrongHResultDescribe(void* self, void* edx, String* result) {
    Win32_CHResult_Describe(self, edx, result);
    if (result->len > 20) {
        msvc6::String_Erase(result, nullptr, 3, 1);
    }
    return result;
}

// Wrong: the Windows error number is passed on without making it an HRESULT.
void __fastcall WrongThrowWin32Error(const char* text, const char* file, std::int32_t line, std::uint32_t error,
                                     const String* location) {
    Win32_ThrowHResult(text, file, line, error, location);
}

// Wrong: mostDerived is ignored.
void* __fastcall WrongMCIConstruct(void* self, void* edx, const String* msg, std::uint32_t code,
                                   std::uint32_t /*mostDerived*/) {
    return Win32_CMCIException_Construct(self, edx, msg, code, 1);
}

}  // namespace

S3RE_TEST(Win32Throwers_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(Win32Throwers_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.hresultDescribe = &WrongHResultDescribe;
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    wrong = Ours();
    wrong.throwWin32Error = &WrongThrowWin32Error;
    {
        test::Context probe(ctx.name(), 2, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    wrong = Ours();
    wrong.mciConstruct = &WrongMCIConstruct;
    {
        test::Context probe(ctx.name(), 3, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
