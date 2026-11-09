// std::logic_error and std::out_of_range (MSVC 6.0), as compiled into S3.EXE.
//
// Layout (0x1c bytes): the C runtime's `exception` (+0 vtable, +4 what text,
// +8 "free the text" flag), then the message as a std::string at +0xc.
// Vtables: logic_error 0x65a548, out_of_range 0x65a538, exception 0x65de1c.
// C runtime functions used (thiscall): exception::exception(const exception&)
// 0x621600, exception::~exception 0x62164a.
//
// Not reimplemented: the functions that throw (std::bitset's out-of-range
// error 0x559550 and its copies 0x58b520 / 0x596570, and the _Doraise
// virtuals 0x5597a0 / 0x559830); they never return.

#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uint32_t kLogicErrorVtable = 0x0065a548;
constexpr std::uint32_t kOutOfRangeVtable = 0x0065a538;
constexpr std::uint32_t kMessage = 0x0c;

String* Message(void* self) {
    return reinterpret_cast<String*>(static_cast<std::uint8_t*>(self) + kMessage);
}

const String* Message(const void* self) {
    return reinterpret_cast<const String*>(static_cast<const std::uint8_t*>(self) + kMessage);
}

void SetVtable(void* self, std::uint32_t vtable) {
    *static_cast<std::uint32_t*>(self) = vtable;
}

void ExceptionCopy(void* self, const void* x) {
    using Fn = void*(__fastcall*)(void*, void*, const void*);  // thiscall
    reinterpret_cast<Fn>(0x00621600)(self, nullptr, x);
}

void ExceptionDestroy(void* self) {
    using Fn = void(__fastcall*)(void*, void*);  // thiscall
    reinterpret_cast<Fn>(0x0062164a)(self, nullptr);
}

// The copy constructors: exception's copy, then the message (shared when
// possible), then the class's own vtable.
void* CopyError(void* self, const void* x, std::uint32_t vtable) {
    ExceptionCopy(self, x);
    String* message = Message(self);
    message->allocator = Message(x)->allocator;
    detail::Clear(message);
    String_AssignSubstr(message, nullptr, Message(x), 0, detail::Npos());
    SetVtable(self, vtable);
    return self;
}

}  // namespace

using WhatFn = const char*(__fastcall*)(const void* self, void* edx);
using DestroyFn = void(__fastcall*)(void* self, void* edx);
using DeletingDestructorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using CopyFn = void*(__fastcall*)(void* self, void* edx, const void* x);

const char* __fastcall LogicError_What(const void* self, void* edx);
void __fastcall LogicError_Destroy(void* self, void* edx);
void* __fastcall LogicError_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void* __fastcall LogicError_Copy(void* self, void* edx, const void* x);
void* __fastcall OutOfRange_Copy(void* self, void* edx, const void* x);

S3RE_HOOK(0x00559630, LogicError_What, WhatFn, Original_LogicError_What, LogicError_What);

// Purpose: The error message of a standard library error (what()), shown
// when the game reports one.
const char* __fastcall LogicError_What(const void* self, void* /*edx*/) {
    return detail::CStr(Message(self));
}

S3RE_HOOK(0x005597e0, LogicError_Destroy, DestroyFn, Original_LogicError_Destroy, LogicError_Destroy);

// Purpose: Destructor of std::logic_error (and the errors derived from it):
// releases the message, then runs exception's destructor.
void __fastcall LogicError_Destroy(void* self, void* /*edx*/) {
    SetVtable(self, kLogicErrorVtable);
    String* message = Message(self);
    if (message->ptr != nullptr) {
        detail::Release(message->ptr);
    }
    message->ptr = nullptr;
    message->len = 0;
    message->res = 0;
    ExceptionDestroy(self);
}

S3RE_HOOK(0x005597c0, LogicError_DeletingDestructor, DeletingDestructorFn,
          Original_LogicError_DeletingDestructor, LogicError_DeletingDestructor);

// Purpose: The compiler's deleting destructor of std::logic_error: destroys
// the object and frees it when bit 0 of flags is set.
void* __fastcall LogicError_DeletingDestructor(void* self, void* /*edx*/, std::uint32_t flags) {
    LogicError_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(self);
    }
    return self;
}

S3RE_HOOK(0x00559640, LogicError_Copy, CopyFn, Original_LogicError_Copy, LogicError_Copy);

// Purpose: Copy constructor of std::logic_error (used when an error is
// thrown or caught by value).
void* __fastcall LogicError_Copy(void* self, void* /*edx*/, const void* x) {
    return CopyError(self, x, kLogicErrorVtable);
}

S3RE_HOOK(0x00559850, OutOfRange_Copy, CopyFn, Original_OutOfRange_Copy, OutOfRange_Copy);

// Purpose: Copy constructor of std::out_of_range (the "invalid position"
// error).
void* __fastcall OutOfRange_Copy(void* self, void* /*edx*/, const void* x) {
    return CopyError(self, x, kOutOfRangeVtable);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kErrorBytes = 0x1c;
constexpr std::uint32_t kGuard = 8;

struct ErrorOps {
    WhatFn what;
    DestroyFn destroy;
    DeletingDestructorFn deleting;
    CopyFn copyLogic, copyRange;
};

// A deliberately wrong copy: copies the message instead of sharing it.
void* __fastcall WrongCopy(void* self, void* /*edx*/, const void* x) {
    ExceptionCopy(self, x);
    String* message = Message(self);
    message->allocator = Message(x)->allocator;
    detail::Clear(message);
    String_AssignChars(message, nullptr, detail::CStr(Message(x)), Message(x)->len);
    SetVtable(self, kLogicErrorVtable);
    return self;
}

// Builds the same error object (exception part with a static what text, so
// nothing is allocated for it, and a message string) in two buffers.
void MakeError(test::Context& ctx, std::uint8_t* a, std::uint8_t* b, const char* text) {
    ctx.rng().Fill(a, kErrorBytes + kGuard);
    std::memcpy(b, a, kErrorBytes + kGuard);
    const char* what = "test";
    std::uint32_t doFree = 0;
    for (std::uint8_t* p : {a, b}) {
        std::memcpy(p + 4, &what, 4);
        std::memcpy(p + 8, &doFree, 4);
        String* m = Message(p);
        *m = String{};
        m->allocator = a[kMessage];
        if (text != nullptr) {
            String_AssignChars(m, nullptr, text, detail::Length(text));
        }
    }
}

bool SameError(test::Context& ctx, const std::uint8_t* a, const std::uint8_t* b) {
    bool same = S3RE_EXPECT_MEM_EQ(a, b, kMessage);
    same &= S3RE_EXPECT_MEM_EQ(a + kErrorBytes, b + kErrorBytes, kGuard);
    const String* x = Message(a);
    const String* y = Message(b);
    same &= S3RE_EXPECT_EQ(x->allocator, y->allocator);
    same &= S3RE_EXPECT_EQ(x->ptr == nullptr, y->ptr == nullptr);
    same &= S3RE_EXPECT_EQ(x->len, y->len);
    same &= S3RE_EXPECT_EQ(x->res, y->res);
    if (same && x->ptr != nullptr) {
        same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(x->ptr[-1]), static_cast<std::uint8_t>(y->ptr[-1]));
        same &= S3RE_EXPECT_MEM_EQ(x->ptr, y->ptr, x->len + 1);
    }
    return same;
}

void CompareErrors(test::Context& ctx, const ErrorOps& candidate, int iterations) {
    const ErrorOps original{Original_LogicError_What, Original_LogicError_Destroy,
                            Original_LogicError_DeletingDestructor, Original_LogicError_Copy,
                            Original_OutOfRange_Copy};
    const char* texts[] = {nullptr, "", "invalid string position", "string too long",
                           "a somewhat longer message that needs more than 31 characters"};
    for (int i = 0; i < iterations; ++i) {
        std::uint8_t a[kErrorBytes + kGuard], b[kErrorBytes + kGuard];
        std::uint8_t a2[kErrorBytes + kGuard], b2[kErrorBytes + kGuard];
        MakeError(ctx, a, b, texts[ctx.rng().Below(5)]);
        const int op = static_cast<int>(ctx.rng().Below(4));
        bool same = true;
        if (op == 0) {
            const char* wa = original.what(a, nullptr);
            const char* wb = candidate.what(b, nullptr);
            same &= S3RE_EXPECT(wa == (Message(a)->ptr ? Message(a)->ptr : reinterpret_cast<const char*>(0x006553a0)));
            same &= S3RE_EXPECT(wb == (Message(b)->ptr ? Message(b)->ptr : reinterpret_cast<const char*>(0x006553a0)));
        } else if (op == 1) {
            // Give the message a second reference first, so that the
            // destructor only decrements and the result stays visible.
            String keepA{}, keepB{};
            String_AssignSubstr(&keepA, nullptr, Message(a), 0, 0xFFFFFFFFu);
            String_AssignSubstr(&keepB, nullptr, Message(b), 0, 0xFFFFFFFFu);
            original.destroy(a, nullptr);
            candidate.destroy(b, nullptr);
            same &= SameError(ctx, a, b);
            same &= S3RE_EXPECT_EQ(keepA.ptr ? static_cast<std::uint8_t>(keepA.ptr[-1]) : 0,
                                   keepB.ptr ? static_cast<std::uint8_t>(keepB.ptr[-1]) : 0);
            String_TidyFree(&keepA, nullptr);
            String_TidyFree(&keepB, nullptr);
            continue;
        } else if (op == 2) {
            void* ra = original.deleting(a, nullptr, 0);  // never 1: the buffers are on the stack
            void* rb = candidate.deleting(b, nullptr, 0);
            same &= S3RE_EXPECT(ra == a && rb == b);
            same &= SameError(ctx, a, b);
            continue;
        } else {
            ctx.rng().Fill(a2, kErrorBytes + kGuard);
            std::memcpy(b2, a2, kErrorBytes + kGuard);
            const bool range = ctx.rng().Chance(1, 2);
            void* ra = (range ? original.copyRange : original.copyLogic)(a2, nullptr, a);
            void* rb = (range ? candidate.copyRange : candidate.copyLogic)(b2, nullptr, b);
            same &= S3RE_EXPECT(ra == a2 && rb == b2);
            same &= SameError(ctx, a2, b2);
            same &= SameError(ctx, a, b);
            String_TidyFree(Message(a2), nullptr);
            String_TidyFree(Message(b2), nullptr);
        }
        same &= SameError(ctx, a, b);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  iteration %d: op %d", i, op);
            return;
        }
        String_TidyFree(Message(a), nullptr);
        String_TidyFree(Message(b), nullptr);
    }
}

}  // namespace

S3RE_TEST(LogicError_MatchesOriginal) {
    CompareErrors(ctx, {&LogicError_What, &LogicError_Destroy, &LogicError_DeletingDestructor,
                        &LogicError_Copy, &OutOfRange_Copy},
                  20000);
}

S3RE_TEST(LogicError_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareErrors(probe, {&LogicError_What, &LogicError_Destroy, &LogicError_DeletingDestructor,
                          &WrongCopy, &WrongCopy},
                  2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::msvc6
