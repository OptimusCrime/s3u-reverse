// The framework's assertion and out-of-memory errors (Win32\Classes.cpp,
// RTTI names): Win32::CProgrammingError, CAssertmentFailed (with its text
// function) and COutOfMemory, and the two functions that throw them.
//
// Object layouts (the CException part is described in win32_exceptions.cpp:
// +0 vtable, +4 virtual-base pointer, +8 message string):
//   CProgrammingError (0x18 bytes): a CException.
//   CAssertmentFailed (0x24 bytes): a CProgrammingError with +0x18 the
//     failed expression (C text), +0x1c the source file (C text), +0x20 the
//     line.
//   COutOfMemory (0x18 bytes): a CRunTimeError.
//
// Win32::CProgrammingError::CProgrammingError (0x00614eb0): (const string&
//   msg, mostDerived) ret 8: if mostDerived, +4 = 0x65d924; CException(msg,
//   0); +0 = 0x65d91c. EAX = this.
// Win32::CAssertmentFailed::Describe (0x00612e80, vtable slot 1; our name):
//   (string* result) ret 4: result = "Assertment failed: " + expression +
//   ", file " + file + ", line " + _ultoa(line) + "\n", built from nine
//   temporaries in the original's order, then copied into result with the
//   default-allocator constructor 0x408cc0. EAX = result.
// Win32::CAssertmentFailed::CAssertmentFailed (0x00612a20): (expression,
//   file, line, mostDerived) ret 10h: if mostDerived, +4 = 0x65d8d4; the
//   message = string(string("" (0x7a7868))) (all three temporaries take the
//   low byte of mostDerived as allocator byte); CProgrammingError(message,
//   0); the message is erased and freed; +0x18/+0x1c/+0x20 = the arguments;
//   +0 = 0x65d8cc; then Describe() into a temporary that is split, erased
//   and freed again (the leftover of a removed log call). EAX = this.
// Win32::ThrowAssertmentFailed (0x006119b0, our name): fastcall (ECX =
//   expression, EDX = file, stack: line), never returns: new 0x24 bytes,
//   CAssertmentFailed(expression, file, line, 1) (null if new fails), thrown
//   with ThrowInfo 0x68edf0. 48 callers (the framework's assert macro).
// Win32::COutOfMemory::COutOfMemory (0x006124a0): (const string& msg,
//   mostDerived) ret 8: if mostDerived, +4 = 0x65d8c0; CRunTimeError(msg,
//   0); +0 = 0x65d8b8; then builds "Not enough memory at " + message + "."
//   and frees it again (removed log call). EAX = this.
// Win32::ThrowOutOfMemory (0x00610960, was ThrowError): fastcall (ECX = C
//   text or null, EDX = file, stack: line), never returns: message = text
//   + ", file " + file + ", line " + _itoa(line); new 0x18 bytes,
//   COutOfMemory(message, 1) (null if new fails), thrown with ThrowInfo
//   0x682f58. Called where operator new returned null.
//
// Allocator bytes: the original gives most temporaries an allocator byte
// from uninitialized stack memory. Those temporaries are all freed before
// the function returns, so ours use 0. The one that survives is Describe's
// result, whose allocator byte is the low byte of the address of the
// original's last temporary (0x408cc0 takes it from its argument's stack
// slot). Ours is the low byte of our own temporary's address, so this byte
// differs; the allocator of MSVC 6.0's string is an empty class that is
// never read, so it has no effect (see CLAUDE.md, "Allocator bytes").
//
// Known difference: like Lobby_ThrowDataError, the throwers have no
// exception frame, so the message of ThrowOutOfMemory leaks when the
// exception unwinds (the original's frame frees it), and neither frees its
// object if the constructor itself throws. Both only happen on the way to a
// fatal error report.

#include "game/framework/framework_layout.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace s3re::game {

// win32_exceptions.cpp
void* __fastcall Win32_CException_Construct(void* self, void* edx, const msvc6::String* msg,
                                            std::uint32_t mostDerived);
void* __fastcall Win32_CRunTimeError_Construct(void* self, void* edx, const msvc6::String* msg,
                                               std::uint32_t mostDerived);

namespace {

using msvc6::String;

constexpr std::uintptr_t kItoa = 0x00621380;
constexpr std::uintptr_t kUltoa = 0x006213aa;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
// Sizes of the exception objects these throw helpers allocate.
constexpr std::uint32_t kAssertmentFailedBytes = 0x24;
constexpr std::uint32_t kOutOfMemoryBytes = 0x18;
constexpr std::int32_t kDecimal = 10;  // _itoa's radix

using framework::CAssertmentFailed;
using framework::CException;

CException* AsException(void* self) {
    return static_cast<CException*>(self);
}

CAssertmentFailed* AsAssertment(void* self) {
    return static_cast<CAssertmentFailed*>(self);
}

[[noreturn]] void Throw(void* object, std::uintptr_t info) {
    using ThrowFn = void(__stdcall*)(void* object, const void* info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&object, reinterpret_cast<const void*>(info));
    __assume(0);
}

// `target` = string(text) as the original inlines it into an existing,
// empty string: a constructed temporary, a copy of it, assign, free both.
void AssignThroughTemporaries(String* target, const char* text, const std::uint8_t* allocator) {
    String fromText;
    String copy;
    msvc6::String_FromText(&fromText, nullptr, text, allocator);
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu, allocator);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// The inlined "erase(0, npos), then free" of a string that goes out of scope.
void EraseAndFree(String* s) {
    msvc6::detail::Erase(s, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

// The same with the out-of-line erase() (0x407b10).
void CallEraseAndFree(String* s) {
    msvc6::String_Erase(s, nullptr, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

}  // namespace

using CtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
using DescribeFn = String*(__fastcall*)(void* self, void* edx, String* result);
using AssertCtorFn = void*(__fastcall*)(void* self, void* edx, const char* expression, const char* file,
                                        std::uint32_t line, std::uint32_t mostDerived);
using ThrowFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);

void* __fastcall Win32_CProgrammingError_Construct(void* self, void* edx, const String* msg,
                                                   std::uint32_t mostDerived);
String* __fastcall Win32_CAssertmentFailed_Describe(void* self, void* edx, String* result);
void* __fastcall Win32_CAssertmentFailed_Construct(void* self, void* edx, const char* expression,
                                                   const char* file, std::uint32_t line, std::uint32_t mostDerived);
void __fastcall Win32_ThrowAssertmentFailed(const char* expression, const char* file, std::int32_t line);
void* __fastcall Win32_COutOfMemory_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void __fastcall Win32_ThrowOutOfMemory(const char* text, const char* file, std::int32_t line);

S3RE_HOOK(0x00614eb0, Win32_CProgrammingError_Construct, CtorFn, Original_Win32_CProgrammingError_Construct,
          Win32_CProgrammingError_Construct);

// Purpose: Constructor of the framework's "programming error" exception
// (the base of failed assertions).
void* __fastcall Win32_CProgrammingError_Construct(void* self, void* /*edx*/, const String* msg,
                                                   std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCProgrammingError;
    }
    Win32_CException_Construct(self, nullptr, msg, 0);
    AsException(self)->vtable = vtables::kCProgrammingError;
    return self;
}

S3RE_HOOK(0x00612e80, Win32_CAssertmentFailed_Describe, DescribeFn, Original_Win32_CAssertmentFailed_Describe,
          Win32_CAssertmentFailed_Describe);

// Purpose: The text of a failed assertion for the error report:
// "Assertment failed: <expression>, file <file>, line <line>" and a newline.
String* __fastcall Win32_CAssertmentFailed_Describe(void* self, void* /*edx*/, String* result) {
    const std::uint8_t allocator = 0;  // the original's are uninitialized (freed temporaries)
    String newline;
    newline.allocator = allocator;
    msvc6::String_Tidy(&newline, nullptr, 0);
    AssignThroughTemporaries(&newline, texts::kNewline.c_str(), &allocator);
    String line;
    msvc6::String_Default(&line, nullptr, &allocator);
    AssignThroughTemporaries(&line, texts::kCommaLine.c_str(), &allocator);
    String head;
    msvc6::String_Default(&head, nullptr, &allocator);
    AssignThroughTemporaries(&head, texts::kAssertmentFailed.c_str(), &allocator);

    String withExpression, withFileText, withFile, withLineText, withNumber, whole;
    msvc6::String_ConcatChars2(&withExpression, &head, AsAssertment(self)->expression);
    msvc6::String_ConcatChars(&withFileText, &withExpression, texts::kCommaFile.c_str());
    msvc6::String_ConcatChars2(&withFile, &withFileText, AsAssertment(self)->file);
    msvc6::String_ConcatString(&withLineText, &withFile, &line);
    char digits[16];
    const char* number = reinterpret_cast<char*(__cdecl*)(std::uint32_t, char*, std::int32_t)>(kUltoa)(
        AsAssertment(self)->line, digits, kDecimal);
    msvc6::String_ConcatChars(&withNumber, &withLineText, number);
    msvc6::String_ConcatString(&whole, &withNumber, &newline);
    msvc6::String_FromSubstrDefault(result, nullptr, &whole, 0, 0xFFFFFFFFu);
    String* const destroyed[] = {&whole, &withNumber, &withLineText, &withFile, &withFileText,
                                 &withExpression, &head, &line, &newline};
    for (String* s : destroyed) {
        msvc6::String_Destroy(s, nullptr);
    }
    return result;
}

S3RE_HOOK(0x00612a20, Win32_CAssertmentFailed_Construct, AssertCtorFn, Original_Win32_CAssertmentFailed_Construct,
          Win32_CAssertmentFailed_Construct);

// Purpose: Constructor of the exception thrown by a failed assertion in the
// framework: keeps the expression, source file and line for the report.
void* __fastcall Win32_CAssertmentFailed_Construct(void* self, void* /*edx*/, const char* expression,
                                                   const char* file, std::uint32_t line, std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCAssertmentFailed;
    }
    const auto allocator = static_cast<std::uint8_t>(mostDerived);
    String message;
    message.allocator = allocator;
    msvc6::String_Tidy(&message, nullptr, 0);
    {
        String text;
        text.allocator = allocator;
        msvc6::String_Tidy(&text, nullptr, 0);
        const char* empty = texts::kEmpty.c_str();
        msvc6::String_AssignChars(&text, nullptr, empty, msvc6::detail::Length(empty));
        String copy;
        copy.allocator = allocator;
        msvc6::String_Tidy(&copy, nullptr, 0);
        msvc6::String_AssignSubstr(&copy, nullptr, &text, 0, 0xFFFFFFFFu);
        msvc6::String_AssignSubstr(&message, nullptr, &copy, 0, msvc6::detail::Npos());
        msvc6::String_Tidy(&copy, nullptr, 1);
        msvc6::String_Tidy(&text, nullptr, 1);
    }
    Win32_CProgrammingError_Construct(self, nullptr, &message, 0);
    EraseAndFree(&message);
    CAssertmentFailed* const failed = AsAssertment(self);
    failed->expression = expression;
    failed->file = file;
    failed->line = line;
    failed->base.vtable = vtables::kCAssertmentFailed;
    String description;
    Win32_CAssertmentFailed_Describe(self, nullptr, &description);
    msvc6::String_Split(&description, nullptr);
    EraseAndFree(&description);
    return self;
}

S3RE_HOOK(0x006119b0, Win32_ThrowAssertmentFailed, ThrowFn, Original_Win32_ThrowAssertmentFailed,
          Win32_ThrowAssertmentFailed);

// Purpose: The framework's assert: throws CAssertmentFailed with the failed
// expression, source file and line (ends in the game's error report).
void __fastcall Win32_ThrowAssertmentFailed(const char* expression, const char* file, std::int32_t line) {
    void* error = msvc6::GameOperatorNew(kAssertmentFailedBytes);
    void* thrown = nullptr;
    if (error != nullptr) {
        thrown = Win32_CAssertmentFailed_Construct(error, nullptr, expression, file, static_cast<std::uint32_t>(line),
                                                   1);
    }
    Throw(thrown, throw_info::kCAssertmentFailed);
}

S3RE_HOOK(0x006124a0, Win32_COutOfMemory_Construct, CtorFn, Original_Win32_COutOfMemory_Construct,
          Win32_COutOfMemory_Construct);

// Purpose: Constructor of the framework's out-of-memory exception (its
// message names where the allocation failed).
void* __fastcall Win32_COutOfMemory_Construct(void* self, void* /*edx*/, const String* msg,
                                              std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCOutOfMemory;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    AsException(self)->vtable = vtables::kCOutOfMemory;
    const std::uint8_t allocator = 0;  // the original's are uninitialized (freed temporaries)
    String fullStop;
    fullStop.allocator = allocator;
    msvc6::String_Tidy(&fullStop, nullptr, 0);
    AssignThroughTemporaries(&fullStop, texts::kFullStop.c_str(), &allocator);
    String head;
    head.allocator = allocator;
    msvc6::String_Tidy(&head, nullptr, 0);
    AssignThroughTemporaries(&head, texts::kNotEnoughMemoryAt.c_str(), &allocator);
    String withMessage, whole;
    msvc6::String_ConcatString(&withMessage, &head, &AsException(self)->message);
    msvc6::String_ConcatString(&whole, &withMessage, &fullStop);
    CallEraseAndFree(&whole);
    CallEraseAndFree(&withMessage);
    msvc6::String_Split(&head, nullptr);
    EraseAndFree(&head);
    CallEraseAndFree(&fullStop);
    return self;
}

S3RE_HOOK(0x00610960, Win32_ThrowOutOfMemory, ThrowFn, Original_Win32_ThrowOutOfMemory, Win32_ThrowOutOfMemory);

// Purpose: Throws the out-of-memory exception after operator new returned
// null, with the place (text, source file, line) in its message.
void __fastcall Win32_ThrowOutOfMemory(const char* text, const char* file, std::int32_t line) {
    const std::uint8_t allocator = 0;  // the original's is uninitialized
    String message;  // left alive: the original's unwinding frees it, ours has no frame
    message.allocator = allocator;
    msvc6::String_Tidy(&message, nullptr, 0);
    if (text != nullptr) {
        AssignThroughTemporaries(&message, text, &allocator);
    }
    String withFileText, withFile, withLineText, whole;
    msvc6::String_ConcatChars(&withFileText, &message, texts::kCommaFile.c_str());
    msvc6::String_ConcatChars2(&withFile, &withFileText, file);
    msvc6::String_ConcatChars(&withLineText, &withFile, texts::kCommaLine.c_str());
    char digits[16];
    const char* number =
        reinterpret_cast<char*(__cdecl*)(std::int32_t, char*, std::int32_t)>(kItoa)(line, digits, kDecimal);
    msvc6::String_ConcatChars(&whole, &withLineText, number);
    msvc6::String_AssignSubstr(&message, nullptr, &whole, 0, msvc6::detail::Npos());
    CallEraseAndFree(&whole);
    CallEraseAndFree(&withLineText);
    CallEraseAndFree(&withFile);
    CallEraseAndFree(&withFileText);
    void* error = msvc6::GameOperatorNew(kOutOfMemoryBytes);
    void* thrown = nullptr;
    if (error != nullptr) {
        thrown = Win32_COutOfMemory_Construct(error, nullptr, &message, 1);
    }
    Throw(thrown, throw_info::kCOutOfMemory);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Constructors: identical random object buffers (with guard bytes) and
// copies of a random message string; compared: EAX, the object bytes (the
// message as "shares the argument's buffer" or its text) and the count
// bytes of the buffers involved. CAssertmentFailed gets random C texts or
// null for the expression and file.
//
// Describe: the result string's length, capacity, count byte and text (not
// its allocator byte, see the header).
//
// Throwers: called under SEH; compared: that both throw, the throw info and
// the thrown object (vtable, virtual-base pointer, fields, message text).

namespace {

String MakeString(std::uint32_t length, std::uint8_t count, const char* text, bool none) {
    String s{};
    s.allocator = static_cast<std::uint8_t>(length * 5);
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

void RandomText(test::Context& ctx, char* text, std::uint32_t length) {
    for (std::uint32_t k = 0; k < length; ++k) {
        text[k] = static_cast<char>(1 + ctx.rng().Below(255));
    }
    text[length] = 0;
}

bool SameMessage(test::Context& ctx, const String& a, const String& b, const String* argA, const String* argB,
                 bool compareCount = true) {
    bool same = S3RE_EXPECT_EQ(a.ptr == nullptr, b.ptr == nullptr);
    same &= S3RE_EXPECT_EQ(argA != nullptr && a.ptr != nullptr && a.ptr == argA->ptr,
                           argB != nullptr && b.ptr != nullptr && b.ptr == argB->ptr);
    same &= S3RE_EXPECT_EQ(a.len, b.len);
    same &= S3RE_EXPECT_EQ(a.res, b.res);
    same &= S3RE_EXPECT_EQ(a.allocator, b.allocator);
    if (same && a.ptr != nullptr && compareCount) {
        same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(a.ptr[-1]), static_cast<std::uint8_t>(b.ptr[-1]));
    }
    if (same && a.ptr != nullptr) {
        same &= S3RE_EXPECT_MEM_EQ(a.ptr, b.ptr, a.len + 1);
    }
    return same;
}

struct Candidate {
    CtorFn programmingError;
    CtorFn outOfMemory;
    AssertCtorFn assertment;
    DescribeFn describe;
};

void CompareObjects(test::Context& ctx, const Candidate& candidate, int cases) {
    constexpr std::size_t kGuard = 8, kObject = 0x24;
    std::uint8_t a[kGuard * 2 + kObject], b[sizeof(a)];
    char text[64], expression[48], file[48];
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(4));
        const std::uint32_t length = ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(60);
        RandomText(ctx, text, length);
        static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
        const std::uint8_t count = kCounts[ctx.rng().Below(sizeof(kCounts))];
        const bool none = ctx.rng().Chance(1, 10);
        String msgA = MakeString(length, count, text, none);
        String msgB = MakeString(length, count, text, none);
        RandomText(ctx, expression, ctx.rng().Below(40));
        RandomText(ctx, file, ctx.rng().Below(40));
        const char* expressionArg = ctx.rng().Chance(1, 6) ? nullptr : expression;
        const char* fileArg = ctx.rng().Chance(1, 6) ? nullptr : file;
        const std::uint32_t line = ctx.rng().Chance(1, 2) ? ctx.rng().Below(3000) : ctx.rng().Next32();
        const std::uint32_t mostDerived = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        void* ra = nullptr;
        void* rb = nullptr;
        bool same = true;
        if (op == 0) {
            ra = Original_Win32_CProgrammingError_Construct(a + kGuard, nullptr, &msgA, mostDerived);
            rb = candidate.programmingError(b + kGuard, nullptr, &msgB, mostDerived);
        } else if (op == 1) {
            ra = Original_Win32_COutOfMemory_Construct(a + kGuard, nullptr, &msgA, mostDerived);
            rb = candidate.outOfMemory(b + kGuard, nullptr, &msgB, mostDerived);
        } else if (op == 2) {
            ra = Original_Win32_CAssertmentFailed_Construct(a + kGuard, nullptr, expressionArg, fileArg, line,
                                                            mostDerived);
            rb = candidate.assertment(b + kGuard, nullptr, expressionArg, fileArg, line, mostDerived);
        } else {
            AsAssertment(a + kGuard)->expression = AsAssertment(b + kGuard)->expression = expressionArg;
            AsAssertment(a + kGuard)->file = AsAssertment(b + kGuard)->file = fileArg;
            AsAssertment(a + kGuard)->line = AsAssertment(b + kGuard)->line = line;
            String resultA, resultB;
            std::memset(&resultA, 0xcc, sizeof(resultA));
            std::memset(&resultB, 0xcc, sizeof(resultB));
            const String* pa = Original_Win32_CAssertmentFailed_Describe(a + kGuard, nullptr, &resultA);
            const String* pb = candidate.describe(b + kGuard, nullptr, &resultB);
            same &= S3RE_EXPECT(pa == &resultA && pb == &resultB);
            resultA.allocator = resultB.allocator = 0;  // differs by design (see the header)
            same &= SameMessage(ctx, resultA, resultB, nullptr, nullptr);
            ra = a + kGuard;
            rb = b + kGuard;
        }
        same &= S3RE_EXPECT(ra == a + kGuard && rb == b + kGuard);
        if (op != 3) {
            auto& sa = AsException(a + kGuard)->message;
            auto& sb = AsException(b + kGuard)->message;
            same &= SameMessage(ctx, sa, sb, op == 2 ? nullptr : &msgA, op == 2 ? nullptr : &msgB);
            sa.ptr = sb.ptr = nullptr;
            if (same && msgA.ptr != nullptr) {
                same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(msgA.ptr[-1]),
                                       static_cast<std::uint8_t>(msgB.ptr[-1]));
            }
        }
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

Thrown Guarded(ThrowFn fn, const char* text, const char* file, std::int32_t line) {
    Thrown out;
    __try {
        fn(text, file, line);
    } __except (ThrowFilter(GetExceptionInformation(), &out)) {
    }
    return out;
}

void CompareThrowers(test::Context& ctx, ThrowFn assertment, ThrowFn outOfMemory, int cases) {
    char text[48], file[48];
    for (int i = 0; i < cases; ++i) {
        const bool isAssert = ctx.rng().Chance(1, 2);
        RandomText(ctx, text, ctx.rng().Below(40));
        RandomText(ctx, file, ctx.rng().Below(40));
        const char* textArg = ctx.rng().Chance(1, 6) ? nullptr : text;
        const char* fileArg = ctx.rng().Chance(1, 6) ? nullptr : file;
        const auto line = static_cast<std::int32_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(5000) : ctx.rng().Next32());
        const Thrown ta = Guarded(isAssert ? Original_Win32_ThrowAssertmentFailed : Original_Win32_ThrowOutOfMemory,
                                  textArg, fileArg, line);
        const Thrown tb = Guarded(isAssert ? assertment : outOfMemory, textArg, fileArg, line);
        bool same = S3RE_EXPECT(ta.threw && tb.threw);
        same &= S3RE_EXPECT_EQ(ta.info, tb.info);
        same &= S3RE_EXPECT(ta.object != nullptr && tb.object != nullptr);
        if (same) {
            same &= S3RE_EXPECT_MEM_EQ(ta.object, tb.object, 9);  // vtable, vbase pointer, allocator
            if (isAssert) {
                same &= S3RE_EXPECT_MEM_EQ(&AsAssertment(ta.object)->expression, &AsAssertment(tb.object)->expression,
                                           12);  // expression, file, line
            }
            // Not the count byte: ThrowOutOfMemory's message shares the
            // buffer, and the original frees its reference while unwinding,
            // ours never (the known difference in the header).
            same &= SameMessage(ctx, AsException(ta.object)->message, AsException(tb.object)->message, nullptr, nullptr,
                                /*compareCount=*/isAssert);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, assert %d, line %d", i, isAssert ? 1 : 0, line);
            return;
        }
    }
}

String* __fastcall WrongDescribe(void* self, void* edx, String* result) {
    Win32_CAssertmentFailed_Describe(self, edx, result);
    msvc6::String_Erase(result, nullptr, result->len > 0 ? result->len - 1 : 0, 1);  // wrong: no newline
    return result;
}

void* __fastcall WrongOutOfMemory(void* self, void* edx, const String* msg, std::uint32_t mostDerived) {
    void* r = Win32_COutOfMemory_Construct(self, edx, msg, mostDerived);
    AsException(self)->vbase = vbtables::kCOutOfMemory;  // wrong: set even when not most derived
    return r;
}

void __fastcall WrongThrowOutOfMemory(const char* text, const char* file, std::int32_t line) {
    Win32_ThrowOutOfMemory(text, file, line - 1);  // wrong: the line number is off by one
}

}  // namespace

S3RE_TEST(Win32Errors_MatchOriginal) {
    CompareObjects(ctx,
                   {&Win32_CProgrammingError_Construct, &Win32_COutOfMemory_Construct,
                    &Win32_CAssertmentFailed_Construct, &Win32_CAssertmentFailed_Describe},
                   20000);
    CompareThrowers(ctx, &Win32_ThrowAssertmentFailed, &Win32_ThrowOutOfMemory, 500);
}

S3RE_TEST(Win32Errors_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareObjects(probe,
                       {&Win32_CProgrammingError_Construct, &WrongOutOfMemory, &Win32_CAssertmentFailed_Construct,
                        &WrongDescribe},
                       2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareThrowers(probe, &Win32_ThrowAssertmentFailed, &WrongThrowOutOfMemory, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
