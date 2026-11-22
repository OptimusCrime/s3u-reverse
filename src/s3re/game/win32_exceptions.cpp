// The framework's exception classes with a message (Win32\Classes.cpp,
// RTTI names) and the lobby's "data error" thrower (Dirk\SoundData.cpp
// range). All constructors are thiscall with a hidden last argument
// `mostDerived` (MSVC's flag for classes with virtual bases: only the most
// derived constructor sets the virtual-base pointer at +4).
//
// Layout of Win32::CException (0x18 bytes): +0 vtable, +4 virtual-base
// pointer, +8 the message as a std::string (allocator byte, ptr, len, res).
//
// Win32::CException::CException (0x00611a20): (const string& msg,
//   mostDerived) ret 8: if mostDerived, +4 = 0x65d898; CObject() (vtable
//   0x65d67c at +0); the message string: allocator byte = the low byte of
//   mostDerived (the original passes an uninitialized allocator that shares
//   that stack slot), then an inlined assign(msg) (shares msg's buffer when
//   possible, exactly as string::assign(msg, 0, npos)); +0 = 0x65d890.
//   EAX = this.
// Win32::CRunTimeError::CRunTimeError (0x00614c60): (msg, mostDerived)
//   ret 8: if mostDerived, +4 = 0x65d884; CException(msg, 0); +0 =
//   0x65d744. EAX = this.
// Lobby::priv::CDataError::CDataError (0x006003b0): (msg, mostDerived)
//   ret 8: if mostDerived, +4 = 0x65d4c0; CRunTimeError(msg, 0); +0 =
//   0x65d4b8. EAX = this.
// Lobby_ThrowDataError (0x005ffcc0, our name): fastcall (ECX = const
//   string& text, EDX = const string& file, stack: line), never returns.
//   Builds text + ", file " + file + ", line " + _itoa(line, 10) once and
//   destroys the four temporaries (the leftover of a removed log call), then
//   builds it again, creates a CDataError (0x18 bytes from operator new,
//   most derived) with it and throws the pointer (ThrowInfo 0x68bfd0); a
//   null pointer when operator new fails. 102 callers (lobby/sound data
//   checks).
//
// Known difference: the original's exception frame destroys the four
// temporaries of the second message when the exception unwinds its frame;
// ours has no frame, so they leak (four small strings, and the message
// buffer they share keeps one extra reference). It only happens on the way
// to a fatal error report. Same convention as DirectX_ThrowIfDDFailed.

#include "game/framework_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CObject_Construct(void* self, void* edx);

namespace {

using msvc6::String;

constexpr std::uintptr_t kItoa = 0x00621380;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;

using framework::CException;

CException* AsException(void* self) {
    return static_cast<CException*>(self);
}

std::uint32_t Npos() {
    return g_stringNpos;
}

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

}  // namespace

using ExceptionCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
using ThrowDataErrorFn = void(__fastcall*)(const String* text, const String* file, std::int32_t line);

void* __fastcall Win32_CException_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void* __fastcall Win32_CRunTimeError_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void* __fastcall Lobby_CDataError_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void __fastcall Lobby_ThrowDataError(const String* text, const String* file, std::int32_t line);

S3RE_HOOK(0x00611a20, Win32_CException_Construct, ExceptionCtorFn, Original_Win32_CException_Construct,
          Win32_CException_Construct);

// Purpose: Constructor of the framework's base exception with a message
// (the text shown in the game's error report).
void* __fastcall Win32_CException_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCException;
    }
    Win32_CObject_Construct(self, nullptr);
    const auto allocator = static_cast<std::uint8_t>(mostDerived);
    msvc6::String_FromSubstr(&AsException(self)->message, nullptr, msg, 0, Npos(), &allocator);
    AsException(self)->vtable = vtables::kCException;
    return self;
}

S3RE_HOOK(0x00614c60, Win32_CRunTimeError_Construct, ExceptionCtorFn, Original_Win32_CRunTimeError_Construct,
          Win32_CRunTimeError_Construct);

// Purpose: Constructor of the framework's run-time error (an exception with
// a message, the base of the lobby's data errors).
void* __fastcall Win32_CRunTimeError_Construct(void* self, void* /*edx*/, const String* msg,
                                               std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCRunTimeError;
    }
    Win32_CException_Construct(self, nullptr, msg, 0);
    AsException(self)->vtable = vtables::kCRunTimeError;
    return self;
}

S3RE_HOOK(0x006003b0, Lobby_CDataError_Construct, ExceptionCtorFn, Original_Lobby_CDataError_Construct,
          Lobby_CDataError_Construct);

// Purpose: Constructor of the "data error" exception (thrown when a data
// file or a lobby message is malformed).
void* __fastcall Lobby_CDataError_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCDataError;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    AsException(self)->vtable = vtables::kCDataError;
    return self;
}

namespace {

// text + ", file " + file + ", line " + line into four temporaries (t[3] is
// the whole message), with the same calls as the original.
void BuildMessage(String t[4], const String* text, const String* file, std::int32_t line) {
    char digits[16];
    msvc6::String_ConcatChars(&t[0], text, texts::kCommaFile.c_str());
    msvc6::String_ConcatString(&t[1], &t[0], file);
    msvc6::String_ConcatChars(&t[2], &t[1], texts::kCommaLine.c_str());
    const char* number = reinterpret_cast<char*(__cdecl*)(std::int32_t, char*, std::int32_t)>(kItoa)(line, digits, 10);
    msvc6::String_ConcatChars(&t[3], &t[2], number);
}

}  // namespace

S3RE_HOOK(0x005ffcc0, Lobby_ThrowDataError, ThrowDataErrorFn, Original_Lobby_ThrowDataError, Lobby_ThrowDataError);

// Purpose: Throws the lobby's data error with a message naming the source
// file and line (an assertion for malformed data or messages).
void __fastcall Lobby_ThrowDataError(const String* text, const String* file, std::int32_t line) {
    String first[4];
    BuildMessage(first, text, file, line);
    for (const int k : {3, 2, 1, 0}) {
        msvc6::String_Erase(&first[k], nullptr, 0, Npos());
        msvc6::String_Tidy(&first[k], nullptr, 1);
    }
    void* error = GameNew(0x18);
    void* thrown = nullptr;
    String message[4];  // left alive: the original's unwinding frees them, ours has no frame
    if (error != nullptr) {
        BuildMessage(message, text, file, line);
        thrown = Lobby_CDataError_Construct(error, nullptr, &message[3], 1);
    }
    using ThrowFn = void(__stdcall*)(void* object, const void* info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&thrown, reinterpret_cast<const void*>(throw_info::kCDataError));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Constructors: each side gets its own copy of a random message string (a
// fresh heap buffer with a random reference count, or no buffer) and an
// object buffer with identical random contents. Compared: EAX, the object
// bytes (the message pointer as "the message's buffer" or the text of a new
// buffer), and the count byte of every buffer involved.
//
// ThrowDataError: called under SEH; compared: that both throw, the throw
// info, and the thrown object (vtable, virtual-base pointer, message text,
// length and capacity).

namespace {

struct CtorFunctions {
    ExceptionCtorFn exception;
    ExceptionCtorFn runTimeError;
    ExceptionCtorFn dataError;
};

String MakeMessage(std::uint32_t length, std::uint8_t count, const char* text, bool none) {
    String s{};
    s.allocator = static_cast<std::uint8_t>(length * 7);
    if (none) {
        return s;
    }
    auto* block = static_cast<char*>(GameNew(length + 2));
    block[0] = static_cast<char>(count);
    std::memcpy(block + 1, text, length);
    block[1 + length] = 0;
    s.ptr = block + 1;
    s.len = length;
    s.res = length;
    return s;
}

void CompareCtors(test::Context& ctx, const CtorFunctions& candidate, int cases) {
    const CtorFunctions original{Original_Win32_CException_Construct, Original_Win32_CRunTimeError_Construct,
                                 Original_Lobby_CDataError_Construct};
    constexpr std::size_t kGuard = 8, kObject = 0x18;
    std::uint8_t a[kGuard * 2 + kObject], b[sizeof(a)];
    char text[64];
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        const std::uint32_t length = ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(60);
        for (std::uint32_t k = 0; k < length; ++k) {
            text[k] = static_cast<char>(1 + ctx.rng().Below(255));
        }
        static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
        const std::uint8_t count = kCounts[ctx.rng().Below(sizeof(kCounts))];
        const bool none = ctx.rng().Chance(1, 10);
        String msgA = MakeMessage(length, count, text, none);
        String msgB = MakeMessage(length, count, text, none);
        const std::uint32_t mostDerived = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        ExceptionCtorFn fa = op == 0 ? original.exception : op == 1 ? original.runTimeError : original.dataError;
        ExceptionCtorFn fb = op == 0 ? candidate.exception : op == 1 ? candidate.runTimeError : candidate.dataError;
        void* ra = fa(a + kGuard, nullptr, &msgA, mostDerived);
        void* rb = fb(b + kGuard, nullptr, &msgB, mostDerived);
        bool same = S3RE_EXPECT(ra == a + kGuard && rb == b + kGuard);
        auto& sa = AsException(a + kGuard)->message;
        auto& sb = AsException(b + kGuard)->message;
        const bool sharedA = sa.ptr != nullptr && sa.ptr == msgA.ptr;
        const bool sharedB = sb.ptr != nullptr && sb.ptr == msgB.ptr;
        same &= S3RE_EXPECT_EQ(sharedA, sharedB);
        same &= S3RE_EXPECT_EQ(sa.ptr == nullptr, sb.ptr == nullptr);
        if (same && sa.ptr != nullptr) {
            same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(sa.ptr[-1]), static_cast<std::uint8_t>(sb.ptr[-1]));
            same &= S3RE_EXPECT_MEM_EQ(sa.ptr, sb.ptr, sa.len + 1);
        }
        if (same && msgA.ptr != nullptr) {
            same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(msgA.ptr[-1]), static_cast<std::uint8_t>(msgB.ptr[-1]));
        }
        sa.ptr = sb.ptr = nullptr;
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, length %u, count %u", i, op, length, count);
            return;
        }
    }
}

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

Thrown GuardedThrowDataError(ThrowDataErrorFn fn, const String* text, const String* file, std::int32_t line) {
    Thrown out;
    __try {
        fn(text, file, line);
    } __except (ThrowFilter(GetExceptionInformation(), &out)) {
    }
    return out;
}

void CompareThrow(test::Context& ctx, ThrowDataErrorFn candidate, int cases) {
    char text[64], file[64];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t textLength = ctx.rng().Below(40);
        const std::uint32_t fileLength = ctx.rng().Below(40);
        for (std::uint32_t k = 0; k < textLength; ++k) {
            text[k] = static_cast<char>('A' + ctx.rng().Below(26));
        }
        for (std::uint32_t k = 0; k < fileLength; ++k) {
            file[k] = static_cast<char>('a' + ctx.rng().Below(26));
        }
        const bool noText = ctx.rng().Chance(1, 8);
        const String textA = MakeMessage(textLength, 0, text, noText);
        const String textB = MakeMessage(textLength, 0, text, noText);
        const String fileA = MakeMessage(fileLength, 0, file, false);
        const String fileB = MakeMessage(fileLength, 0, file, false);
        const std::int32_t line =
            static_cast<std::int32_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(5000) : ctx.rng().Next32());
        const Thrown ta = GuardedThrowDataError(Original_Lobby_ThrowDataError, &textA, &fileA, line);
        const Thrown tb = GuardedThrowDataError(candidate, &textB, &fileB, line);
        bool same = S3RE_EXPECT(ta.threw && tb.threw);
        same &= S3RE_EXPECT_EQ(ta.info, tb.info);
        same &= S3RE_EXPECT(ta.object != nullptr && tb.object != nullptr);
        if (same) {
            same &= S3RE_EXPECT_MEM_EQ(ta.object, tb.object, 9);  // vtable, vbase pointer, allocator
            const auto& ma = AsException(ta.object)->message;
            const auto& mb = AsException(tb.object)->message;
            same &= S3RE_EXPECT_EQ(ma.len, mb.len);
            same &= S3RE_EXPECT_EQ(ma.res, mb.res);
            same &= S3RE_EXPECT(ma.ptr != nullptr && mb.ptr != nullptr);
            if (same) {
                same &= S3RE_EXPECT_MEM_EQ(ma.ptr, mb.ptr, ma.len + 1);
            }
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, line %d", i, line);
            return;
        }
    }
}

void* __fastcall WrongRunTimeError(void* self, void* edx, const String* msg, std::uint32_t mostDerived) {
    void* r = Win32_CRunTimeError_Construct(self, edx, msg, mostDerived);
    AsException(self)->vbase = vbtables::kCRunTimeError;  // wrong: set even when not most derived
    return r;
}

void __fastcall WrongThrowDataError(const String* text, const String* file, std::int32_t line) {
    Lobby_ThrowDataError(text, file, line + 1);  // wrong: the line number is off by one
}

}  // namespace

S3RE_TEST(Win32Exceptions_MatchOriginal) {
    CompareCtors(ctx, {&Win32_CException_Construct, &Win32_CRunTimeError_Construct, &Lobby_CDataError_Construct},
                 20000);
    CompareThrow(ctx, &Lobby_ThrowDataError, 500);
}

S3RE_TEST(Win32Exceptions_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareCtors(probe, {&Win32_CException_Construct, &WrongRunTimeError, &Lobby_CDataError_Construct}, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareThrow(probe, &WrongThrowDataError, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
