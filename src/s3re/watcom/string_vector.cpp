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

#include "watcom/ptr_vector.h"

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

}  // namespace

S3RE_TEST(StringVector_Resize_MatchesOriginal) {
    CompareResize(ctx, &StringVector_Resize, 5000);
}

S3RE_TEST(StringVector_Resize_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareResize(probe, &WrongResize, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
