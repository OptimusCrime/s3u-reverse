// Watcom value vector of std::string (WCValVector<String>): resize. Same
// layout as the pointer vector (ptr_vector.h), with 16-byte MSVC 6.0
// strings as entries.
//
// StringVector_Resize (0x0040a580; Dirk\Console.cpp's unit, 23 callers):
//   (n) ret 4. keep = min(filled, n) (signed). n == 0: no new block.
//   Otherwise a block of n * 16 bytes from operator new; if that is null,
//   the library's empty error hook 0x59f200 is called and 0 returned with
//   nothing changed. The first `keep` strings are copy-constructed into the
//   new block (each null-checked, placement new): allocator byte = the low
//   byte of n (the compiler let the empty allocator object share the stack
//   slot of the argument), then assign(old, 0, npos), so the copies share
//   the old buffers. Then every old string (0..filled-1) is destroyed
//   inline (split if shared, erase, release), the old block is freed, and
//   filled = keep, data = the new block, capacity = n. `length` (+0x14) is
//   left alone. EAX = 1.
//
// Destructors of vectors of strings (session 56; thiscall, no arguments):
// vtable = WCBareVectorBase<String> 0x655454; if data: the empty error
// hook 0x59f200, each of the `filled` strings destroyed inline (as
// ~string 0x401b60 does: erase, release the buffer; filled and data re-read
// per element), operator delete(data); vtable = WCExcept 0x655438. EAX =
// operator delete's, or 0 without a block. Four copies of the same code:
//   0x00409d50 WCValOrderedVector<String>  0x0044c980 WCValSortedVector<String>
//   0x0040a280, 0x0044cf50 (WCBareVectorBase<String> bodies, callers' classes not named)

#include "watcom/ptr_vector.h"
#include "watcom/vector_base.h"

#include "game/vtables.h"

#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstring>

namespace s3re::watcom {

namespace {

using msvc6::String;

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

void GameDelete(void* block) {
    reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(block);
}

String* Entries(PtrVector* self) {
    return reinterpret_cast<String*>(self->data);
}

}  // namespace

std::uint32_t __fastcall StringVector_Resize(PtrVector* self, void* edx, std::uint32_t n);

S3RE_HOOK(0x0040a580, StringVector_Resize, ResizeFn, Original_StringVector_Resize, StringVector_Resize);

// Purpose: Changes the number of strings a string vector has room for,
// keeping the ones that fit (used by the console/menu code for lists of
// texts).
std::uint32_t __fastcall StringVector_Resize(PtrVector* self, void* /*edx*/, std::uint32_t n) {
    const std::int32_t keep = self->filled < static_cast<std::int32_t>(n) ? self->filled : static_cast<std::int32_t>(n);
    String* block = nullptr;
    if (n != 0) {
        block = static_cast<String*>(GameNew(n << 4));
        if (block == nullptr) {
            return 0;  // (the original calls the empty error hook 0x59f200 here)
        }
        for (std::int32_t i = 0; i < keep; ++i) {
            auto* to = reinterpret_cast<String*>(reinterpret_cast<std::uintptr_t>(block) + 16u * i);
            if (to != nullptr) {
                to->allocator = static_cast<std::uint8_t>(n);
                msvc6::detail::Clear(to);
                msvc6::String_AssignSubstr(to, nullptr, &Entries(self)[i], 0, 0xFFFFFFFFu);
            }
        }
    }
    for (std::int32_t i = 0; i < self->filled; ++i) {
        msvc6::String_Destroy(&Entries(self)[i], nullptr);
    }
    GameDelete(self->data);
    self->filled = keep;
    self->data = reinterpret_cast<std::uint32_t*>(block);
    self->capacity = n;
    return 1;
}

using StringVectorDestructorFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx);

namespace {

std::uint32_t DestroyStringVector(VectorBase* self) {
    return DestroyVector(self, game::vtables::kWCBareVectorBase_String, game::vtables::kWCExcept, sizeof(String),
                         [](std::uint32_t string) { msvc6::String_Destroy(reinterpret_cast<String*>(string), nullptr); });
}

}  // namespace

// Purpose: Destructors of the console's and menus' vectors of strings:
// destroy each string, then free the block.
std::uint32_t __fastcall StringVectorDestructor_409d50(VectorBase* self, void*) {
    return DestroyStringVector(self);
}
std::uint32_t __fastcall StringVectorDestructor_44c980(VectorBase* self, void*) {
    return DestroyStringVector(self);
}
std::uint32_t __fastcall StringVectorDestructor_40a280(VectorBase* self, void*) {
    return DestroyStringVector(self);
}
std::uint32_t __fastcall StringVectorDestructor_44cf50(VectorBase* self, void*) {
    return DestroyStringVector(self);
}

S3RE_HOOK(0x00409d50, StringVectorDestructor_409d50, StringVectorDestructorFn,
          Original_StringVectorDestructor_409d50, StringVectorDestructor_409d50);
S3RE_HOOK(0x0044c980, StringVectorDestructor_44c980, StringVectorDestructorFn,
          Original_StringVectorDestructor_44c980, StringVectorDestructor_44c980);
S3RE_HOOK(0x0040a280, StringVectorDestructor_40a280, StringVectorDestructorFn,
          Original_StringVectorDestructor_40a280, StringVectorDestructor_40a280);
S3RE_HOOK(0x0044cf50, StringVectorDestructor_44cf50, StringVectorDestructorFn,
          Original_StringVectorDestructor_44cf50, StringVectorDestructor_44cf50);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The same vector twice (its own heap block and string buffers per side,
// random texts, counts and sometimes shared buffers), resized by the
// original and by ours to the same n. Compared: EAX, the fields, and every
// kept string (allocator byte, length, capacity, count byte, text, and which
// strings share a buffer).

namespace {

void Build(PtrVector& vector, std::uint32_t count, const std::uint32_t* lengths, const std::uint8_t* counts,
           const int* shareWith, const char* text, std::int32_t filled) {
    vector = {};
    vector.vtable = 0x12345678;
    vector.capacity = count;
    vector.filled = filled;
    vector.length = count;
    vector.data = count != 0 ? static_cast<std::uint32_t*>(GameNew(count * 16)) : nullptr;
    for (std::uint32_t i = 0; i < count; ++i) {
        String& s = Entries(&vector)[i];
        s.allocator = static_cast<std::uint8_t>(i * 13);
        if (shareWith[i] >= 0 && Entries(&vector)[shareWith[i]].ptr != nullptr &&
            static_cast<std::uint8_t>(Entries(&vector)[shareWith[i]].ptr[-1]) < 0xfd) {
            s = Entries(&vector)[shareWith[i]];
            s.allocator = static_cast<std::uint8_t>(i * 13);
            s.ptr[-1] = static_cast<char>(s.ptr[-1] + 1);
            continue;
        }
        if (lengths[i] == 0xFFFFFFFFu) {
            s.ptr = nullptr;
            s.len = s.res = 0;
            continue;
        }
        auto* buffer = static_cast<char*>(GameNew(lengths[i] + 2));
        buffer[0] = static_cast<char>(counts[i]);
        std::memcpy(buffer + 1, text, lengths[i]);
        buffer[1 + lengths[i]] = 0;
        s.ptr = buffer + 1;
        s.len = s.res = lengths[i];
    }
}

bool SameVectors(test::Context& ctx, PtrVector& a, PtrVector& b) {
    bool same = S3RE_EXPECT_EQ(a.capacity, b.capacity);
    same &= S3RE_EXPECT_EQ(a.filled, b.filled);
    same &= S3RE_EXPECT_EQ(a.length, b.length);
    same &= S3RE_EXPECT_EQ(a.vtable, b.vtable);
    same &= S3RE_EXPECT_EQ(a.data == nullptr, b.data == nullptr);
    for (std::int32_t i = 0; same && i < a.filled; ++i) {
        const String& sa = Entries(&a)[i];
        const String& sb = Entries(&b)[i];
        same &= S3RE_EXPECT_EQ(sa.allocator, sb.allocator);
        same &= S3RE_EXPECT_EQ(sa.len, sb.len);
        same &= S3RE_EXPECT_EQ(sa.res, sb.res);
        same &= S3RE_EXPECT_EQ(sa.ptr == nullptr, sb.ptr == nullptr);
        if (same && sa.ptr != nullptr) {
            same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(sa.ptr[-1]), static_cast<std::uint8_t>(sb.ptr[-1]));
            same &= S3RE_EXPECT_MEM_EQ(sa.ptr, sb.ptr, sa.len + 1);
            for (std::int32_t j = 0; j < i; ++j) {
                same &= S3RE_EXPECT_EQ(sa.ptr == Entries(&a)[j].ptr, sb.ptr == Entries(&b)[j].ptr);
            }
        }
    }
    return same;
}

void CompareResize(test::Context& ctx, ResizeFn candidate, int cases) {
    char text[48];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(7);
        std::uint32_t lengths[8];
        std::uint8_t counts[8];
        int shareWith[8];
        static constexpr std::uint8_t kCounts[] = {0, 0, 0, 1, 2, 0xfd, 0xff};
        for (std::uint32_t k = 0; k < count; ++k) {
            lengths[k] = ctx.rng().Chance(1, 8) ? 0xFFFFFFFFu : ctx.rng().Below(40);
            counts[k] = kCounts[ctx.rng().Below(sizeof(kCounts))];
            shareWith[k] = k > 0 && ctx.rng().Chance(1, 4) ? static_cast<int>(ctx.rng().Below(k)) : -1;
        }
        for (char& c : text) {
            c = static_cast<char>(1 + ctx.rng().Below(255));
        }
        // filled is at most the capacity (the entries past it don't exist).
        const auto filled = static_cast<std::int32_t>(count == 0 ? 0 : ctx.rng().Below(count + 1));
        PtrVector a{}, b{};
        Build(a, count, lengths, counts, shareWith, text, filled);
        Build(b, count, lengths, counts, shareWith, text, filled);
        const std::uint32_t n = ctx.rng().Chance(1, 4) ? 0x100 + ctx.rng().Below(3) : ctx.rng().Below(9);
        const std::uint32_t ra = Original_StringVector_Resize(&a, nullptr, n);
        const std::uint32_t rb = candidate(&b, nullptr, n);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= SameVectors(ctx, a, b);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, count %u, filled %d, n %u", i, count, filled, n);
            return;
        }
        Original_StringVector_Resize(&a, nullptr, 0);
        Original_StringVector_Resize(&b, nullptr, 0);
    }
}

std::uint32_t __fastcall WrongResize(PtrVector* self, void* edx, std::uint32_t n) {
    const std::uint32_t r = StringVector_Resize(self, edx, n);
    for (std::int32_t i = 0; i < self->filled; ++i) {
        Entries(self)[i].allocator = 0;  // wrong: allocator byte not the low byte of n
    }
    return r;
}

// The destructors on the same kind of vectors (the PtrVector layout is the
// first 0x18 bytes of VectorBase). One more string outside the vector
// shares the buffer of a random element, so the destructor's release of a
// shared buffer is visible: compared are EAX (without a block), the
// vector's bytes (data aside) and that string's count byte and text.
void CompareDestructor(test::Context& ctx, StringVectorDestructorFn original, StringVectorDestructorFn candidate,
                       int cases) {
    char text[48];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(7);
        std::uint32_t lengths[8];
        std::uint8_t counts[8];
        int shareWith[8];
        static constexpr std::uint8_t kCounts[] = {0, 0, 0, 1, 2, 0xfd};
        for (std::uint32_t k = 0; k < count; ++k) {
            lengths[k] = ctx.rng().Chance(1, 8) ? 0xFFFFFFFFu : ctx.rng().Below(40);
            counts[k] = kCounts[ctx.rng().Below(sizeof(kCounts))];
            shareWith[k] = k > 0 && ctx.rng().Chance(1, 4) ? static_cast<int>(ctx.rng().Below(k)) : -1;
        }
        for (char& c : text) {
            c = static_cast<char>(1 + ctx.rng().Below(255));
        }
        const auto filled = static_cast<std::int32_t>(count == 0 ? 0 : ctx.rng().Below(count + 1));
        const std::uint32_t outside = count == 0 ? 0 : ctx.rng().Below(count);
        VectorBase va{}, vb{};
        PtrVector* pa = reinterpret_cast<PtrVector*>(&va);
        PtrVector* pb = reinterpret_cast<PtrVector*>(&vb);
        Build(*pa, count, lengths, counts, shareWith, text, filled);
        Build(*pb, count, lengths, counts, shareWith, text, filled);
        va.grow = vb.grow = 0x5eed;
        // The outside string: shares element `outside`'s buffer (if it has one).
        String outA{}, outB{};
        if (count != 0 && Entries(pa)[outside].ptr != nullptr &&
            static_cast<std::uint8_t>(Entries(pa)[outside].ptr[-1]) < 0xfd) {
            outA = Entries(pa)[outside];
            outB = Entries(pb)[outside];
            ++outA.ptr[-1];
            ++outB.ptr[-1];
        }
        const bool block = va.data != 0;
        const std::uint32_t ra = original(&va, nullptr);
        const std::uint32_t rb = candidate(&vb, nullptr);
        va.data = vb.data = 0;
        bool same = S3RE_EXPECT_MEM_EQ(&va, &vb, sizeof(va));
        if (!block) {
            same = S3RE_EXPECT_EQ(ra, rb) && same;
        }
        same = S3RE_EXPECT_EQ(outA.ptr == nullptr, outB.ptr == nullptr) && same;
        if (same && outA.ptr != nullptr) {
            same = S3RE_EXPECT_EQ(static_cast<std::uint8_t>(outA.ptr[-1]), static_cast<std::uint8_t>(outB.ptr[-1])) &&
                   S3RE_EXPECT_MEM_EQ(outA.ptr, outB.ptr, outA.len + 1);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, count %u, filled %d", i, count, filled);
            return;
        }
        if (outA.ptr != nullptr) {
            msvc6::String_Destroy(&outA, nullptr);
            msvc6::String_Destroy(&outB, nullptr);
        }
    }
}

// Wrong: skips the last element.
std::uint32_t __fastcall WrongDestructor(VectorBase* self, void* edx) {
    --self->filled;
    const std::uint32_t r = StringVectorDestructor_409d50(self, edx);
    ++self->filled;
    return r;
}

}  // namespace

S3RE_TEST(StringVectorDestructors_MatchOriginal) {
    CompareDestructor(ctx, Original_StringVectorDestructor_409d50, &StringVectorDestructor_409d50, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_44c980, &StringVectorDestructor_44c980, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_40a280, &StringVectorDestructor_40a280, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_44cf50, &StringVectorDestructor_44cf50, 1000);
}

S3RE_TEST(StringVectorDestructors_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestructor(probe, Original_StringVectorDestructor_409d50, &WrongDestructor, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(StringVector_Resize_MatchesOriginal) {
    CompareResize(ctx, &StringVector_Resize, 5000);
}

S3RE_TEST(StringVector_Resize_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareResize(probe, &WrongResize, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
