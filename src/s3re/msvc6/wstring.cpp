// std::wstring (MSVC 6.0, 16-bit characters) members the UI's edit fields
// use, and allocator<char>::deallocate. Layout as std::string (string.h):
// allocator byte, ptr, len, res; the reference count is the byte right
// before ptr, and the block starts 2 bytes before ptr.
//
// WString_Tidy (0x004286d0; std::wstring::_Tidy(bool built)) ret 4: when
//   built and there is a buffer: a shared one (count not 0 and not frozen
//   0xff) loses a reference, else the block (ptr - 2) is freed; then ptr =
//   len = res = 0. void in the library; EAX is left over (AL = the flag or
//   the new count, or operator delete's), so ours returns nothing.
// WString_CompareChars (0x00429980; compare(pos, n0, const wchar_t* s,
//   count)) ret 0x10: std::_Xran 0x61e646 when pos > len; n = min(len - pos,
//   n0), the first min(n, count) characters compared as unsigned 16-bit
//   values (-1 / 1 at the first difference), then the lengths: n < count
//   -1, n > count 1, else 0. A string without buffer reads from address 0
//   (as the original; only reached with len 0, so nothing is read).
// Allocator_Deallocate (0x00486920; allocator<char>::deallocate(p, n))
//   ret 8: operator delete(p); EAX = its.

#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uint8_t kFrozen = 0xFF;

#pragma pack(push, 1)
struct WString {
    std::uint8_t allocator;
    std::uint8_t pad[3];
    std::uint16_t* ptr;
    std::uint32_t len;
    std::uint32_t res;
};
#pragma pack(pop)
static_assert(sizeof(WString) == 16);

std::uint8_t& RefCount(std::uint16_t* ptr) {
    return reinterpret_cast<std::uint8_t*>(ptr)[-1];
}

void GameXran() {
    reinterpret_cast<void(__cdecl*)()>(0x0061e646)();
}

}  // namespace

using WStringTidyFn = void(__fastcall*)(WString* self, void* edx, std::uint32_t built);
using WStringCompareCharsFn = std::int32_t(__fastcall*)(const WString* self, void* edx, std::uint32_t pos,
                                                        std::uint32_t n0, const std::uint16_t* s,
                                                        std::uint32_t count);
using DeallocateFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* p, std::uint32_t n);

// Purpose: Releases a wide string's buffer (its destructor and reassignment
// code): drops one reference of a shared buffer, frees an own one.
void __fastcall WString_Tidy(WString* self, void* /*edx*/, std::uint32_t built) {
    if (static_cast<std::uint8_t>(built) != 0 && self->ptr != nullptr) {
        const std::uint8_t count = RefCount(self->ptr);
        if (count != 0 && count != kFrozen) {
            RefCount(self->ptr) = static_cast<std::uint8_t>(count - 1);
        } else {
            GameOperatorDelete(reinterpret_cast<std::uint8_t*>(self->ptr) - 2);
        }
    }
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
}

// Purpose: Compares part of a wide string with wide characters (the edit
// field and the game launch dialog's chat).
std::int32_t __fastcall WString_CompareChars(const WString* self, void* /*edx*/, std::uint32_t pos, std::uint32_t n0,
                                             const std::uint16_t* s, std::uint32_t count) {
    if (self->len < pos) {
        GameXran();
    }
    std::uint32_t n = self->len - pos;
    if (n0 < n) {
        n = n0;
    }
    const std::uint32_t common = n < count ? n : count;
    const std::uint16_t* chars = self->ptr != nullptr ? self->ptr + pos : nullptr;
    for (std::uint32_t i = 0; i < common; ++i) {
        if (chars[i] != s[i]) {
            return chars[i] < s[i] ? -1 : 1;
        }
    }
    if (n < count) {
        return -1;
    }
    return n != count ? 1 : 0;
}

// Purpose: allocator<char>::deallocate: frees a block (the game setup
// dialog's text buffers).
std::uint32_t __fastcall Allocator_Deallocate(void* /*self*/, void* /*edx*/, void* p, std::uint32_t /*n*/) {
    return GameOperatorDelete(p);
}

S3RE_HOOK(0x004286d0, WString_Tidy, WStringTidyFn, Original_WString_Tidy, WString_Tidy);
S3RE_HOOK(0x00429980, WString_CompareChars, WStringCompareCharsFn, Original_WString_CompareChars,
          WString_CompareChars);
S3RE_HOOK(0x00486920, Allocator_Deallocate, DeallocateFn, Original_Allocator_Deallocate, Allocator_Deallocate);

// ---------------------------------------------------------------------------
// Tests: operator delete replaced by a recording stub (the freed address,
// relative to the case's buffer, and EAX fixed by the call); wide strings
// in a buffer with random counts (0, 1, 2, 0xfe, 0xff), the flag 0/1 with
// random upper bytes; compare on random texts sharing prefixes.
// ---------------------------------------------------------------------------

namespace {

std::vector<std::uint32_t>* g_deletes = nullptr;
std::uintptr_t g_base = 0;

std::uint32_t __cdecl RecordDelete(void* block) {
    g_deletes->push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block) - g_base));
    return 0xde1e7e00u + static_cast<std::uint32_t>(g_deletes->size());
}

struct DeleteStub {
    explicit DeleteStub(std::vector<std::uint32_t>& log, const void* base) {
        g_deletes = &log;
        g_base = reinterpret_cast<std::uintptr_t>(base);
        test::PatchJump(kOperatorDelete, reinterpret_cast<const void*>(&RecordDelete));
    }
    ~DeleteStub() {
        test::UnpatchJump(kOperatorDelete);
        g_deletes = nullptr;
    }
};

void CompareTidy(test::Context& ctx, WStringTidyFn candidate, int cases) {
    static constexpr std::uint8_t kCounts[] = {0, 1, 2, 0xfe, 0xff};
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[64];
        ctx.rng().Fill(initial, sizeof(initial));
        initial[16 + 1] = kCounts[ctx.rng().Below(sizeof(kCounts))];  // the count byte before ptr (block at +16)
        const bool withBuffer = ctx.rng().Chance(4, 5);
        const std::uint32_t built = ctx.rng().Below(2) | (ctx.rng().Next32() & 0xFFFFFF00u);
        std::uint8_t a[64], b[64];
        std::vector<std::uint32_t> deletesA, deletesB;
        auto run = [&](WStringTidyFn fn, std::uint8_t* buffer, std::vector<std::uint32_t>& deletes) {
            std::memcpy(buffer, initial, sizeof(initial));
            auto* str = reinterpret_cast<WString*>(buffer);
            str->ptr = withBuffer ? reinterpret_cast<std::uint16_t*>(buffer + 18) : nullptr;
            DeleteStub stub(deletes, buffer);
            fn(str, nullptr, built);
        };
        run(Original_WString_Tidy, a, deletesA);
        run(candidate, b, deletesB);
        if (!S3RE_EXPECT(deletesA == deletesB) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: built 0x%x, buffer %d", i, built, withBuffer);
            return;
        }
    }
}

void CompareCompare(test::Context& ctx, WStringCompareCharsFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint16_t text[40], other[44];
        for (auto* chars : {text, other}) {
            for (int k = 0; k < 40; ++k) {
                chars[k] = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 'a' + ctx.rng().Below(3));
            }
        }
        if (ctx.rng().Chance(1, 2)) {
            std::memcpy(other, text, sizeof(text));
        }
        WString str{};
        const std::uint32_t len = ctx.rng().Below(39);
        str.ptr = len == 0 && ctx.rng().Chance(1, 2) ? nullptr : text;
        str.len = str.ptr == nullptr ? 0 : len;
        const std::uint32_t pos = ctx.rng().Below(str.len + 1);
        const std::uint32_t n0 = ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Below(40);
        const std::uint32_t skip = ctx.rng().Below(4);
        const std::uint32_t count = ctx.rng().Below(40);
        const std::int32_t ra = Original_WString_CompareChars(&str, nullptr, pos, n0, other + skip, count);
        const std::int32_t rb = candidate(&str, nullptr, pos, n0, other + skip, count);
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: len %u pos %u n0 %u count %u", i, str.len, pos, n0, count);
            return;
        }
    }
}

void CompareDeallocate(test::Context& ctx, DeallocateFn candidate, int cases) {
    std::uint8_t buffer[16];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t offset = ctx.rng().Below(16);
        const std::uint32_t n = ctx.rng().Next32();
        std::vector<std::uint32_t> deletesA, deletesB;
        std::uint32_t ra, rb;
        {
            DeleteStub stub(deletesA, buffer);
            ra = Original_Allocator_Deallocate(buffer, nullptr, buffer + offset, n);
        }
        {
            DeleteStub stub(deletesB, buffer);
            rb = candidate(buffer, nullptr, buffer + offset, n);
        }
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT(deletesA == deletesB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: a frozen buffer only loses a reference.
void __fastcall WrongTidy(WString* self, void* /*edx*/, std::uint32_t built) {
    if (static_cast<std::uint8_t>(built) != 0 && self->ptr != nullptr) {
        if (RefCount(self->ptr) != 0) {
            --RefCount(self->ptr);
        } else {
            GameOperatorDelete(reinterpret_cast<std::uint8_t*>(self->ptr) - 2);
        }
    }
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
}

// Wrong: compares the characters signed.
std::int32_t __fastcall WrongCompare(const WString* self, void* edx, std::uint32_t pos, std::uint32_t n0,
                                     const std::uint16_t* s, std::uint32_t count) {
    std::uint32_t n = self->len - pos;
    n = n0 < n ? n0 : n;
    const std::uint32_t common = n < count ? n : count;
    for (std::uint32_t i = 0; i < common; ++i) {
        const auto l = static_cast<std::int16_t>(self->ptr[pos + i]);
        const auto r = static_cast<std::int16_t>(s[i]);
        if (l != r) {
            return l < r ? -1 : 1;
        }
    }
    return WString_CompareChars(self, edx, pos, n0, s, count);
}

}  // namespace

S3RE_TEST(WString_Members_MatchOriginal) {
    CompareTidy(ctx, &WString_Tidy, 2000);
    CompareCompare(ctx, &WString_CompareChars, 5000);
    CompareDeallocate(ctx, &Allocator_Deallocate, 100);
}

S3RE_TEST(WString_Members_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareTidy(probe, &WrongTidy, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareCompare(probe, &WrongCompare, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::msvc6
