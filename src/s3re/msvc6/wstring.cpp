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
// WString_Copy (0x004289e0; _Copy(n), ret 4; the code after its try block
//   is the separate Ghidra function 0x00428a62): capacity n | 31 (n itself
//   if that exceeds max_size 0x7ffffffd); operator new((capacity + 2) * 2),
//   the count clamped at 0 while signed and the doubling wrapping; copies
//   min(len, capacity) characters to block + 2; releases the old buffer
//   (shared: one reference less, else operator delete(ptr - 2)); ptr =
//   block + 2, count byte 0, res = capacity, len = min(old len, capacity),
//   terminating zero. The catch handler retries operator new with n + 2,
//   but the game's operator new returns null instead of throwing, so it
//   never runs (ours has none). Byte 0 of the block stays unwritten.
// WString_Grow (0x004288a0; _Grow(n, trim) ret 8, bool in AL): as
//   std::string::_Grow 0x401f80: std::_Xlen 0x61e4fa above 0x7ffffffd; a
//   shared buffer loses a reference and the string is emptied (n 0, false)
//   or gets its own copy (_Copy(n), true); n 0 frees (trim) or truncates the
//   string (false); with trim a capacity above 31 or below n is freed and
//   reallocated, without trim only a capacity below n.
// WString_Split (0x00428af0; _Split()): a shared buffer loses a reference;
//   the string is emptied and gets a copy of the shared text up to its first
//   zero character (wcslen 0x61fe1b, not the length): _Tidy(true) and, for a
//   non-empty text, _Copy(length). The inlined _Grow's shared-buffer branch
//   re-checks the pointer just set to null (dead).
// WString_AssignSubstr (0x00428400; assign(const wstring& x, pos, n) ret
//   0xc, EAX = this): std::_Xran 0x61e646 when pos > x.len; count =
//   min(x.len - pos, n). x == this: erase(pos + count, npos) then erase(0,
//   pos) (inlined; _Split 0x428af0, the word move 0x418cf0, _Grow(.., 0),
//   _Eos). Else a full copy (count > 0 and == x.len) of a buffer whose count
//   byte is below 0xfe is shared (x without buffer: the static empty string
//   0x655dc8, whose count byte 0x655dc7 is then incremented); otherwise the
//   inlined _Grow(count, true) and a copy of the characters.

#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"

#include "game/tables.h"

#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uint8_t kFrozen = 0xFF;
constexpr std::uint8_t kMaxShared = 0xFE;        // a buffer with this count or more is copied, not shared
constexpr std::uint32_t kMaxSize = 0x7FFFFFFDu;  // max_size() (0x004289d0)
constexpr std::uint32_t kMinSize = 31;

std::uint8_t& RefCount(std::uint16_t* ptr) {
    return reinterpret_cast<std::uint8_t*>(ptr)[-1];
}

void GameXran() {
    reinterpret_cast<void(__cdecl*)()>(0x0061e646)();
}

void GameXlen() {
    reinterpret_cast<void(__cdecl*)()>(0x0061e4fa)();
}

bool IsShared(std::uint16_t* ptr) {
    if (ptr == nullptr) {
        return false;
    }
    const std::uint8_t count = RefCount(ptr);
    return count != 0 && count != kFrozen;
}

// c_str(): the static empty string for a string without buffer.
std::uint16_t* CStr(const WString* s) {
    return s->ptr != nullptr ? s->ptr : game::g_wstringNullBuffer.get();
}

void* BlockOf(std::uint16_t* ptr) {
    return ptr - 1;  // the block starts 2 bytes before the characters
}

void CopyWords(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        to[i] = from[i];
    }
}

// The inlined _Tidy(true): one reference less of a shared buffer, else free
// it; then empty.
void ReleaseAndClear(WString* s) {
    if (s->ptr != nullptr) {
        if (IsShared(s->ptr)) {
            --RefCount(s->ptr);
        } else {
            GameOperatorDelete(BlockOf(s->ptr));
        }
    }
    s->ptr = nullptr;
    s->len = 0;
    s->res = 0;
}

// Calls of the other members through their S3.EXE addresses (ours in the
// game).
void CallTidy(WString* s, std::uint32_t built) {
    reinterpret_cast<void(__fastcall*)(WString*, void*, std::uint32_t)>(0x004286d0)(s, nullptr, built);
}
void CallCopy(WString* s, std::uint32_t n) {
    reinterpret_cast<WStringCopyFn>(0x004289e0)(s, nullptr, n);
}
bool CallGrow(WString* s, std::uint32_t n, std::uint32_t trim) {
    return reinterpret_cast<WStringGrowFn>(0x004288a0)(s, nullptr, n, trim);
}
void CallSplit(WString* s) {
    reinterpret_cast<WStringSplitFn>(0x00428af0)(s, nullptr);
}
void CallEos(WString* s, std::uint32_t n) {
    reinterpret_cast<void(__fastcall*)(WString*, void*, std::uint32_t)>(0x00428880)(s, nullptr, n);
}
// UI_MoveWords 0x418cf0 (fastcall: to, from; count on the stack).
void CallMoveWords(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count) {
    using MoveWordsFn = void(__fastcall*)(std::uint16_t*, const std::uint16_t*, std::uint32_t);
    reinterpret_cast<MoveWordsFn>(0x00418cf0)(to, from, count);
}

// The inlined erase(p0, m) of assign's self-assignment (0x428433..0x428490
// with the _Xran check, `checked`, and 0x428490..0x4284d7 without).
void EraseWords(WString* self, std::uint32_t p0, std::uint32_t m, bool checked) {
    if (checked && self->len < p0) {
        GameXran();
    }
    CallSplit(self);
    const std::uint32_t rest = self->len - p0;
    if (rest < m) {
        m = rest;
    }
    if (m == 0) {
        return;
    }
    CallMoveWords(self->ptr + p0, self->ptr + p0 + m, rest - m);
    const std::uint32_t n = self->len - m;
    if (CallGrow(self, n, 0)) {
        CallEos(self, n);
    }
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

S3RE_HOOK(0x004289e0, WString_Copy, WStringCopyFn, Original_WString_Copy, WString_Copy);

// Purpose: Standard wide-string helper: moves the text into a new buffer of
// its own with room for n characters (rounded up to 31).
void __fastcall WString_Copy(WString* self, void* /*edx*/, std::uint32_t n) {
    std::uint32_t capacity = n | kMinSize;
    if (capacity > kMaxSize) {
        capacity = n;
    }
    std::int32_t words = static_cast<std::int32_t>(capacity + 2);
    if (words < 0) {
        words = 0;  // the allocator's "negative count" check
    }
    auto* block = static_cast<std::uint16_t*>(GameOperatorNew(static_cast<std::uint32_t>(words) * 2));
    const std::uint32_t oldLength = self->len;
    if (oldLength > 0) {
        CopyWords(block + 1, self->ptr, oldLength > capacity ? capacity : oldLength);
    }
    ReleaseAndClear(self);
    self->ptr = block + 1;
    RefCount(self->ptr) = 0;
    self->res = capacity;
    const std::uint32_t length = oldLength > capacity ? capacity : oldLength;
    self->len = length;
    self->ptr[length] = 0;
}

S3RE_HOOK(0x004288a0, WString_Grow, WStringGrowFn, Original_WString_Grow, WString_Grow);

// Purpose: Standard wide-string helper: before a wide string is overwritten,
// makes sure it has an unshared buffer with room for n characters (or
// empties it for 0).
bool __fastcall WString_Grow(WString* self, void* /*edx*/, std::uint32_t n, std::uint32_t trim) {
    if (n > kMaxSize) {
        GameXlen();
    }
    if (IsShared(self->ptr)) {
        if (n == 0) {
            --RefCount(self->ptr);
            self->ptr = nullptr;
            self->len = 0;
            self->res = 0;
            return false;
        }
        CallCopy(self, n);
        return true;
    }
    const bool trimming = static_cast<std::uint8_t>(trim) != 0;
    if (n == 0) {
        if (trimming) {
            ReleaseAndClear(self);
        } else if (self->ptr != nullptr) {
            self->len = 0;
            self->ptr[0] = 0;
        }
        return false;
    }
    if (trimming) {
        if (self->res > kMinSize || self->res < n) {
            ReleaseAndClear(self);
            CallCopy(self, n);
        }
    } else if (self->res < n) {
        CallCopy(self, n);
    }
    return true;
}

S3RE_HOOK(0x00428af0, WString_Split, WStringSplitFn, Original_WString_Split, WString_Split);

// Purpose: Standard wide-string helper: a wide string that shares its
// buffer with copies gets its own copy of the text before it is changed.
void __fastcall WString_Split(WString* self, void* /*edx*/) {
    if (!IsShared(self->ptr)) {
        return;
    }
    const std::uint16_t* shared = self->ptr;
    --RefCount(self->ptr);
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
    std::uint32_t length = 0;  // wcslen (0x61fe1b)
    while (shared[length] != 0) {
        ++length;
    }
    if (length > kMaxSize) {
        GameXlen();
    }
    CallTidy(self, 1);
    if (length == 0) {
        return;
    }
    CallCopy(self, length);
    CopyWords(self->ptr, shared, length);
    self->len = length;
    self->ptr[length] = 0;
}

S3RE_HOOK(0x00428400, WString_AssignSubstr, WStringAssignSubstrFn, Original_WString_AssignSubstr,
          WString_AssignSubstr);

// Purpose: Standard wide string: sets a wide string to (part of) another
// one; a full copy shares the other's buffer instead of copying it. The
// game setup copies the players' 16-bit names with it.
WString* __fastcall WString_AssignSubstr(WString* self, void* /*edx*/, const WString* x, std::uint32_t pos,
                                         std::uint32_t n) {
    if (x->len < pos) {
        GameXran();
    }
    std::uint32_t count = x->len - pos;
    if (n < count) {
        count = n;
    }
    if (self == x) {
        EraseWords(self, pos + count, game::g_wstringNpos, true);
        EraseWords(self, 0, pos, false);
        return self;
    }
    if (count > 0 && count == x->len && RefCount(CStr(x)) < kMaxShared) {
        ReleaseAndClear(self);
        self->ptr = CStr(x);
        self->len = x->len;
        self->res = x->res;
        ++RefCount(self->ptr);
        return self;
    }
    // The inlined _Grow(count, true) (0x428558..0x4285c7).
    if (count > kMaxSize) {
        GameXlen();
    }
    if (IsShared(self->ptr)) {
        if (count == 0) {
            --RefCount(self->ptr);
            CallTidy(self, 0);
            return self;
        }
        CallCopy(self, count);
    } else if (count == 0) {
        CallTidy(self, 1);
        return self;
    } else if (self->res > kMinSize || self->res < count) {
        CallTidy(self, 1);
        CallCopy(self, count);
    }
    CopyWords(self->ptr, CStr(x) + pos, count);
    self->len = count;
    self->ptr[count] = 0;
    return self;
}

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

// The wide-string operations of one side of the random-operations test.
struct WStringOps {
    WStringTidyFn tidy;
    WStringCopyFn copy;
    WStringGrowFn grow;
    WStringSplitFn split;
    WStringAssignSubstrFn assignSubstr;
};

// A deliberately wrong assign(x, pos, n): never shares the buffer.
WString* __fastcall WrongAssignSubstr(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                      std::uint32_t n) {
    if (self == x) {
        return WString_AssignSubstr(self, edx, x, pos, n);
    }
    std::uint32_t count = x->len - pos;
    count = n < count ? n : count;
    if (WString_Grow(self, nullptr, count, 1)) {
        CopyWords(self->ptr, CStr(x) + pos, count);
        self->len = count;
        self->ptr[count] = 0;
    }
    return self;
}

constexpr int kWSlots = 6;
constexpr int kSources = 12;

// A frozen source text (count byte 0xff, so it is copied, never shared or
// freed): words [0] = block start with the count in its high byte, then
// the characters and the zero.
struct Source {
    std::vector<std::uint16_t> words;
    WString string{};
};

bool SameWStrings(test::Context& ctx, const WString* a, const WString* b) {
    bool same = true;
    for (int i = 0; i < kWSlots && same; ++i) {
        same &= S3RE_EXPECT_EQ(a[i].allocator, b[i].allocator);
        same &= S3RE_EXPECT_EQ(a[i].ptr == nullptr, b[i].ptr == nullptr);
        same &= S3RE_EXPECT_EQ(a[i].len, b[i].len);
        same &= S3RE_EXPECT_EQ(a[i].res, b[i].res);
        if (!same || a[i].ptr == nullptr) {
            continue;
        }
        same &= S3RE_EXPECT_EQ(RefCount(a[i].ptr), RefCount(b[i].ptr));
        same &= S3RE_EXPECT_MEM_EQ(a[i].ptr, b[i].ptr, (a[i].len + 1) * 2);
        for (int j = 0; j < i; ++j) {
            same &= S3RE_EXPECT_EQ(a[i].ptr == a[j].ptr, b[i].ptr == b[j].ptr);
        }
    }
    return same;
}

// The same random sequence of operations on two sets of wide strings, one
// through the original functions and one through `candidate`, on the game's
// real heap; the strings are compared after every operation. (With the hooks
// installed, the originals' internal calls land in ours; the emulator specs
// compare the originals on their own.)
void CompareWStringOperations(test::Context& ctx, const WStringOps& candidate, int sequences) {
    const WStringOps original{Original_WString_Tidy, Original_WString_Copy, Original_WString_Grow,
                              Original_WString_Split, Original_WString_AssignSubstr};
    std::vector<Source> sources(kSources);
    for (int k = 0; k < kSources; ++k) {
        const std::uint32_t length = ctx.rng().Below(k < 3 ? 3 : 80);
        Source& source = sources[k];
        source.words.assign(length + 2, 0);
        source.words[0] = 0xff00;  // count byte (frozen) right before the characters
        for (std::uint32_t c = 0; c < length; ++c) {
            source.words[1 + c] = static_cast<std::uint16_t>(1 + ctx.rng().Below(0xfffe));
        }
        source.string.ptr = length == 0 && ctx.rng().Chance(1, 2) ? nullptr : &source.words[1];
        source.string.len = source.string.ptr == nullptr ? 0 : length;
        source.string.res = source.string.len;
    }
    for (int sequence = 0; sequence < sequences; ++sequence) {
        WString a[kWSlots] = {};
        WString b[kWSlots] = {};
        for (int step = 0; step < 40; ++step) {
            const int op = static_cast<int>(ctx.rng().Below(8));
            const int i = static_cast<int>(ctx.rng().Below(kWSlots));
            const int j = static_cast<int>(ctx.rng().Below(kWSlots));
            const WString& source = sources[ctx.rng().Below(kSources)].string;
            const std::uint32_t n = ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Below(90);
            const std::uint32_t flag = ctx.rng().Below(2) | (ctx.rng().Next32() & 0xFF00u);
            std::uint32_t resultA = 0;
            std::uint32_t resultB = 0;
            switch (op) {
                case 0: {
                    const std::uint32_t pos = ctx.rng().Below(source.len + 1);
                    resultA = original.assignSubstr(&a[i], nullptr, &source, pos, n) == &a[i];
                    resultB = candidate.assignSubstr(&b[i], nullptr, &source, pos, n) == &b[i];
                    break;
                }
                case 1:
                case 2: {
                    const std::uint32_t pos = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(a[j].len + 1);
                    resultA = original.assignSubstr(&a[i], nullptr, &a[j], pos, n) == &a[i];
                    resultB = candidate.assignSubstr(&b[i], nullptr, &b[j], pos, n) == &b[i];
                    break;
                }
                case 3:
                    resultA = original.grow(&a[i], nullptr, n & 0x7F, flag);
                    resultB = candidate.grow(&b[i], nullptr, n & 0x7F, flag);
                    break;
                case 4:
                    original.copy(&a[i], nullptr, n & 0x7F);
                    candidate.copy(&b[i], nullptr, n & 0x7F);
                    break;
                case 5:
                    original.split(&a[i], nullptr);
                    candidate.split(&b[i], nullptr);
                    break;
                case 6:
                    original.tidy(&a[i], nullptr, 1);
                    candidate.tidy(&b[i], nullptr, 1);
                    break;
                case 7:
                    // Extra references nobody holds (only leaks): reaches the
                    // shared-buffer branches and the "count 0xfe: copy" rule.
                    if (a[i].ptr != nullptr && RefCount(a[i].ptr) == 0) {
                        const std::uint8_t count = ctx.rng().Chance(1, 2) ? 1 : 0xfd;
                        RefCount(a[i].ptr) = count;
                        RefCount(b[i].ptr) = count;
                    }
                    break;
            }
            if (!S3RE_EXPECT_EQ(resultA, resultB) || !SameWStrings(ctx, a, b)) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d, step %d, op %d, slots %d/%d, n %u", sequence, step, op,
                         i, j, n);
                return;
            }
        }
        for (int i = 0; i < kWSlots; ++i) {
            Original_WString_Tidy(&a[i], nullptr, 1);
            WString_Tidy(&b[i], nullptr, 1);
        }
    }
}

}  // namespace

S3RE_TEST(WString_Members_MatchOriginal) {
    CompareTidy(ctx, &WString_Tidy, 2000);
    CompareCompare(ctx, &WString_CompareChars, 5000);
    CompareDeallocate(ctx, &Allocator_Deallocate, 100);
}

S3RE_TEST(WString_Operations_MatchOriginal) {
    CompareWStringOperations(ctx, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split, &WString_AssignSubstr},
                             400);
}

S3RE_TEST(WString_Operations_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split, &WrongAssignSubstr},
                             50);
    S3RE_EXPECT(probe.failures() > 0);
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
