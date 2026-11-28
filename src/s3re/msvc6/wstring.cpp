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
// WString_CopyConstruct (0x00534d30; wstring(const wstring& x) ret 4, EAX =
//   this; was std::wstring::wstring_copy; wip/logic-r4): the allocator byte
//   from x, ptr = len = res = 0, then the inlined assign(x, 0, npos) (its
//   self-assignment branch can't run for a constructor).
// WString_ConcatWcsLeft (0x005418a0; operator+(const wchar_t*, const
//   wstring&), cdecl with the result first, EAX = result; was
//   std::wstring::concat_wcs_left; wip/logic-r4): a local wstring (allocator
//   byte = the text pointer's low byte, the compiler's empty allocator
//   argument) gets the text (wcslen 0x61fe1b, _Grow(n, true), UI_CopyWords
//   0x418fb0, _Eos) and the inlined append(right): _Xlen when the new length
//   reaches npos, _Grow(len + n, false), the words, length and terminator;
//   then the result is constructed from it (allocator byte copied,
//   assign(local, 0, -1) 0x428400) and the local destroyed (inlined).
// WString_SubstrConstruct (0x00541410; wstring(const wstring& x, pos, n,
//   const allocator&) ret 0x10, EAX = this; was std::wstring::wstring_substr;
//   wip/logic-r4): the allocator byte from the allocator argument, ptr =
//   len = res = 0, then the inlined assign(x, pos, n) (its own _Xran check
//   first; the self-assignment branch can't run for a constructor).
//   ShowTutorialText cuts the lines with it.
// WString_Erase (0x00428730; erase(pos, n) ret 8, EAX = this; wip/logic-r4):
//   std::_Xran when pos > len; then an inlined _Split that differs from
//   0x428af0: a shared buffer loses a reference, the string is emptied,
//   and with _Grow(wcslen(text), true) (wcslen through the CRT's 0x61fe1b)
//   the text is copied back (UI_CopyWords 0x418fb0, _Eos 0x428880); n is
//   clamped to len - pos; for n > 0 the tail moves down (word copy,
//   backwards if the ranges overlap that way, which they can't), _Xlen
//   above 0x7ffffffd, and the inlined _Grow(len - n, false): a shared
//   buffer is emptied for 0 (one reference less, _Tidy(false) 0x4286d0) or
//   copied (_Copy), an own one truncated for 0 or grown (_Copy) when its
//   capacity is below the new length; then len and the terminating zero.

// WString_FromWcs (0x0053ba60; wstring(const wchar_t* s, const allocator&)
//   ret 8, EAX = this; wip/logic-r8): the allocator byte from the argument,
//   ptr = len = res = 0, wcslen 0x61fe1b (_Xlen above 0x7ffffffd), then the
//   inlined _Grow(n, true) of the empty string: _Tidy(true), for n > 0
//   _Copy(n), the words, length and terminator.
// WString_ConcatWcs (0x00534e50; operator+(const wstring&, const wchar_t*),
//   cdecl with the result first, EAX = result; wip/logic-r8): a local copy
//   of the left side (its allocator byte, assign(left, 0, npos)), the
//   inlined append(text) (_Xlen when the new length reaches 2^32 - 1,
//   _Grow(len + n, false), the words), the result assigned from the local
//   (0x428400) and the local freed (inlined).
// WString_AssignCopy (0x00523a40; operator=(const wstring&) ret 4, EAX =
//   this; wip/logic-r8): the inlined assign(x, 0, npos) as in 0x428400,
//   except that the copying branch calls _Grow(n, true) 0x4288a0.

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

S3RE_HOOK(0x00534d30, WString_CopyConstruct, WStringCopyConstructFn, Original_WString_CopyConstruct,
          WString_CopyConstruct);

// Purpose: Standard wide string: the copy constructor (shares the other's
// buffer where it can).
WString* __fastcall WString_CopyConstruct(WString* self, void* /*edx*/, const WString* x) {
    self->allocator = x->allocator;
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
    reinterpret_cast<WStringAssignSubstrFn>(0x00428400)(self, nullptr, x, 0, game::g_wstringNpos);
    return self;
}

S3RE_HOOK(0x005418a0, WString_ConcatWcsLeft, WStringConcatWcsLeftFn, Original_WString_ConcatWcsLeft,
          WString_ConcatWcsLeft);

// Purpose: Standard wide string: text + string as a new string (the chat
// prompt in front of the input).
WString* __cdecl WString_ConcatWcsLeft(WString* result, const std::uint16_t* text, const WString* right) {
    using WordsFn = void(__fastcall*)(std::uint16_t*, const std::uint16_t*, std::uint32_t);
    const auto copyWords = reinterpret_cast<WordsFn>(0x00418fb0);
    WString local{};
    local.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(text));
    using WcslenFn = std::uint32_t(__cdecl*)(const std::uint16_t*);
    const std::uint32_t textLength = reinterpret_cast<WcslenFn>(0x0061fe1b)(text);
    if (CallGrow(&local, textLength, 1)) {
        copyWords(local.ptr, text, textLength);
        CallEos(&local, textLength);
    }
    const std::uint32_t n = right->len;  // (min(len, npos) with the literal -1)
    if (0xFFFFFFFFu - local.len <= n) {
        GameXlen();
    }
    if (n > 0) {
        const std::uint32_t length = local.len + n;
        if (CallGrow(&local, length, 0)) {
            copyWords(local.ptr + local.len, CStr(right), n);
            local.len = length;
            local.ptr[length] = 0;
        }
    }
    result->allocator = local.allocator;
    result->ptr = nullptr;
    result->len = 0;
    result->res = 0;
    reinterpret_cast<WStringAssignSubstrFn>(0x00428400)(result, nullptr, &local, 0, 0xFFFFFFFFu);
    if (local.ptr != nullptr) {
        if (IsShared(local.ptr)) {
            --RefCount(local.ptr);
        } else {
            GameOperatorDelete(BlockOf(local.ptr));
        }
    }
    return result;
}

S3RE_HOOK(0x00541410, WString_SubstrConstruct, WStringSubstrConstructFn, Original_WString_SubstrConstruct,
          WString_SubstrConstruct);

// Purpose: Standard wide string: constructs a string from part of another
// (the tutorial text's lines).
WString* __fastcall WString_SubstrConstruct(WString* self, void* /*edx*/, const WString* x, std::uint32_t pos,
                                            std::uint32_t n, const std::uint8_t* allocator) {
    self->allocator = *allocator;
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
    if (x->len < pos) {
        GameXran();
    }
    reinterpret_cast<WStringAssignSubstrFn>(0x00428400)(self, nullptr, x, pos, n);
    return self;
}

S3RE_HOOK(0x00428730, WString_Erase, WStringEraseFn, Original_WString_Erase, WString_Erase);

// Purpose: Standard wide string: removes n characters at pos (the chat
// input's backspace and the message lines).
WString* __fastcall WString_Erase(WString* self, void* /*edx*/, std::uint32_t pos, std::uint32_t n) {
    if (self->len < pos) {
        GameXran();
    }
    std::uint16_t* const shared = self->ptr;
    if (IsShared(shared)) {
        --RefCount(shared);
        self->ptr = nullptr;
        self->len = 0;
        self->res = 0;
        using WcslenFn = std::uint32_t(__cdecl*)(const std::uint16_t*);
        const std::uint32_t length = reinterpret_cast<WcslenFn>(0x0061fe1b)(shared);
        if (CallGrow(self, length, 1)) {
            using CopyWordsFn = void(__fastcall*)(std::uint16_t*, const std::uint16_t*, std::uint32_t);
            reinterpret_cast<CopyWordsFn>(0x00418fb0)(self->ptr, shared, length);
            CallEos(self, length);
        }
    }
    const std::uint32_t rest = self->len - pos;
    if (rest < n) {
        n = rest;
    }
    if (n == 0) {
        return self;
    }
    std::uint32_t tail = rest - n;
    const std::uint16_t* from = self->ptr + pos + n;
    std::uint16_t* to = self->ptr + pos;
    if (from < to && to < from + tail) {
        // (backwards; the source lies above the target, so never taken)
        from += tail;
        to += tail;
        while (tail-- > 0) {
            *--to = *--from;
        }
    } else {
        while (tail-- > 0) {
            *to++ = *from++;
        }
    }
    const std::uint32_t length = self->len - n;
    if (length > kMaxSize) {
        GameXlen();
    }
    if (IsShared(self->ptr)) {
        if (length == 0) {
            --RefCount(self->ptr);
            CallTidy(self, 0);
            return self;
        }
        CallCopy(self, length);
    } else if (length == 0) {
        if (self->ptr != nullptr) {
            self->len = 0;
            self->ptr[0] = 0;
        }
        return self;
    } else if (self->res < length) {
        CallCopy(self, length);
    }
    self->len = length;
    self->ptr[length] = 0;
    return self;
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

namespace {

// wcslen through the CRT's 0x61fe1b, as the original calls it.
std::uint32_t GameWcslen(const std::uint16_t* text) {
    using WcslenFn = std::uint32_t(__cdecl*)(const std::uint16_t*);
    return reinterpret_cast<WcslenFn>(0x0061fe1b)(text);
}

}  // namespace

S3RE_HOOK(0x0053ba60, WString_FromWcs, WStringFromWcsFn, Original_WString_FromWcs, WString_FromWcs);

// Purpose: Standard wide string: constructs a string from a zero-terminated
// wide text (the logic thread's chat and tutorial texts).
WString* __fastcall WString_FromWcs(WString* self, void* /*edx*/, const std::uint16_t* text,
                                    const std::uint8_t* allocator) {
    self->allocator = *allocator;
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
    const std::uint32_t length = GameWcslen(text);
    if (length > kMaxSize) {
        GameXlen();
    }
    // The inlined _Grow(length, true) of the still empty string (its
    // shared-buffer branch 0x53baa5..0x53bac6 can't run): _Tidy(true), and
    // for a text a new buffer.
    CallTidy(self, 1);
    if (length == 0) {
        return self;
    }
    CallCopy(self, length);
    CopyWords(self->ptr, text, length);
    self->len = length;
    self->ptr[length] = 0;
    return self;
}

S3RE_HOOK(0x00534e50, WString_ConcatWcs, WStringConcatWcsFn, Original_WString_ConcatWcs, WString_ConcatWcs);

// Purpose: Standard wide string: string + text as a new string.
WString* __cdecl WString_ConcatWcs(WString* result, const WString* left, const std::uint16_t* text) {
    WString local{};
    local.allocator = left->allocator;
    reinterpret_cast<WStringAssignSubstrFn>(0x00428400)(&local, nullptr, left, 0, game::g_wstringNpos);
    // The inlined append(text).
    const std::uint32_t n = GameWcslen(text);
    if (0xFFFFFFFFu - local.len <= n) {
        GameXlen();
    }
    if (n > 0) {
        const std::uint32_t length = local.len + n;
        if (CallGrow(&local, length, 0)) {
            CopyWords(local.ptr + local.len, text, n);
            local.len = length;
            local.ptr[length] = 0;
        }
    }
    result->allocator = local.allocator;
    result->ptr = nullptr;
    result->len = 0;
    result->res = 0;
    reinterpret_cast<WStringAssignSubstrFn>(0x00428400)(result, nullptr, &local, 0, game::g_wstringNpos);
    if (local.ptr != nullptr) {
        if (IsShared(local.ptr)) {
            --RefCount(local.ptr);
        } else {
            GameOperatorDelete(BlockOf(local.ptr));
        }
    }
    return result;
}

S3RE_HOOK(0x00523a40, WString_AssignCopy, WStringAssignCopyFn, Original_WString_AssignCopy, WString_AssignCopy);

// Purpose: Standard wide string: assignment from another string (shares
// its buffer where it can).
WString* __fastcall WString_AssignCopy(WString* self, void* /*edx*/, const WString* x) {
    // The inlined assign(x, 0, npos).
    std::uint32_t count = x->len;
    if (game::g_wstringNpos < count) {
        count = game::g_wstringNpos;
    }
    if (self == x) {
        EraseWords(self, count, game::g_wstringNpos, true);
        EraseWords(self, 0, 0, false);
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
    if (CallGrow(self, count, 1)) {
        CopyWords(self->ptr, CStr(x), count);
        self->len = count;
        self->ptr[count] = 0;
    }
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
    WStringEraseFn erase;
    WStringCopyConstructFn copyConstruct = nullptr;
    WStringConcatWcsLeftFn concatLeft = nullptr;
    WStringSubstrConstructFn substrConstruct = nullptr;
    WStringFromWcsFn fromWcs = nullptr;
    WStringConcatWcsFn concatWcs = nullptr;
    WStringAssignCopyFn assignCopy = nullptr;
};

// How many operations the random sequence picks from: the ones with a
// function on both sides.
int OperationCount(const WStringOps& ops) {
    return ops.assignCopy != nullptr        ? 16
           : ops.concatWcs != nullptr       ? 15
           : ops.fromWcs != nullptr         ? 14
           : ops.substrConstruct != nullptr ? 13
           : ops.concatLeft != nullptr      ? 12
           : ops.copyConstruct != nullptr   ? 11
           : ops.erase != nullptr           ? 10
                                            : 8;
}

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
                              Original_WString_Split, Original_WString_AssignSubstr, Original_WString_Erase,
                              Original_WString_CopyConstruct, Original_WString_ConcatWcsLeft,
                              Original_WString_SubstrConstruct, Original_WString_FromWcs,
                              Original_WString_ConcatWcs, Original_WString_AssignCopy};
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
        for (int k = 0; k < kWSlots; ++k) {
            a[k].allocator = b[k].allocator = static_cast<std::uint8_t>(ctx.rng().Below(256));
        }
        for (int step = 0; step < 40; ++step) {
            const int op = static_cast<int>(ctx.rng().Below(OperationCount(candidate)));
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
                case 15:
                    // Slot j assigned to slot i (also onto itself).
                    resultA = original.assignCopy(&a[i], nullptr, &a[j]) == &a[i];
                    resultB = candidate.assignCopy(&b[i], nullptr, &b[j]) == &b[i];
                    break;
                case 14: {
                    // string + text constructed over slot i (its old buffer
                    // leaks; the left side another slot).
                    if (i != j) {
                        const std::uint16_t* text = source.ptr != nullptr ? source.ptr : &sources[0].words[1];
                        resultA = original.concatWcs(&a[i], &a[j], text) == &a[i];
                        resultB = candidate.concatWcs(&b[i], &b[j], text) == &b[i];
                    }
                    break;
                }
                case 13: {
                    // A text constructed over slot i (its old buffer leaks).
                    const std::uint16_t* text = source.ptr != nullptr ? source.ptr : &sources[0].words[1];
                    const std::uint8_t allocator = static_cast<std::uint8_t>(flag >> 8);
                    resultA = original.fromWcs(&a[i], nullptr, text, &allocator) == &a[i];
                    resultB = candidate.fromWcs(&b[i], nullptr, text, &allocator) == &b[i];
                    break;
                }
                case 12: {
                    // A part of slot j constructed over slot i (its old
                    // buffer leaks).
                    if (i != j) {
                        const std::uint32_t pos = ctx.rng().Below(a[j].len + 1);
                        const std::uint8_t allocator = static_cast<std::uint8_t>(flag >> 8);
                        resultA = original.substrConstruct(&a[i], nullptr, &a[j], pos, n, &allocator) == &a[i];
                        resultB = candidate.substrConstruct(&b[i], nullptr, &b[j], pos, n, &allocator) == &b[i];
                    }
                    break;
                }
                case 11: {
                    // text + string constructed over slot i (its old buffer
                    // leaks; the right side another slot).
                    if (i != j) {
                        const std::uint16_t* text = source.ptr != nullptr ? source.ptr : &sources[0].words[1];
                        resultA = original.concatLeft(&a[i], text, &a[j]) == &a[i];
                        resultB = candidate.concatLeft(&b[i], text, &b[j]) == &b[i];
                    }
                    break;
                }
                case 10:
                    // A copy constructed over slot i (its old buffer leaks;
                    // not onto itself).
                    if (i != j) {
                        resultA = original.copyConstruct(&a[i], nullptr, &a[j]) == &a[i];
                        resultB = candidate.copyConstruct(&b[i], nullptr, &b[j]) == &b[i];
                    }
                    break;
                case 8:
                case 9: {
                    const std::uint32_t pos = ctx.rng().Below(a[i].len + 1);
                    const std::uint32_t count = ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Below(a[i].len + 3);
                    resultA = original.erase(&a[i], nullptr, pos, count) == &a[i];
                    resultB = candidate.erase(&b[i], nullptr, pos, count) == &b[i];
                    break;
                }
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
    CompareWStringOperations(ctx, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split, &WString_AssignSubstr,
                                   &WString_Erase, &WString_CopyConstruct, &WString_ConcatWcsLeft,
                                   &WString_SubstrConstruct, &WString_FromWcs, &WString_ConcatWcs,
                                   &WString_AssignCopy},
                             400);
}

S3RE_TEST(WString_Operations_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split, &WrongAssignSubstr,
                                     nullptr},
                             50);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: erase(pos, n) without clamping n to the rest (removes up to the end anyway, but the
// length is reduced by the unclamped count).
WString* __fastcall WrongErase(WString* self, void* edx, std::uint32_t pos, std::uint32_t n) {
    if (n != 0xFFFFFFFFu && n > self->len - pos && self->len - pos > 0) {
        return WString_Erase(self, edx, pos, self->len - pos - 1);
    }
    return WString_Erase(self, edx, pos, n);
}

}  // namespace

namespace {

// Wrong: the allocator byte is not copied.
WString* __fastcall WrongCopyConstruct(WString* self, void* edx, const WString* x) {
    const std::uint8_t allocator = self->allocator;
    WString_CopyConstruct(self, edx, x);
    self->allocator = allocator;
    return self;
}

}  // namespace

namespace {

// Wrong: the right side's last character is left out.
WString* __cdecl WrongConcatWcsLeft(WString* result, const std::uint16_t* text, const WString* right) {
    WString_ConcatWcsLeft(result, text, right);
    if (right->len > 0 && result->len > 0) {
        result->len = result->len - 1;
    }
    return result;
}

}  // namespace

namespace {

// Wrong: the allocator byte is not taken from the argument.
WString* __fastcall WrongSubstrConstruct(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                         std::uint32_t n, const std::uint8_t* allocator) {
    const std::uint8_t mine = 0;
    return WString_SubstrConstruct(self, edx, x, pos, n, *allocator == 0 ? allocator : &mine);
}

}  // namespace

S3RE_TEST(WString_SubstrConstruct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split,
                                     &WString_AssignSubstr, &WString_Erase, &WString_CopyConstruct,
                                     &WString_ConcatWcsLeft, &WrongSubstrConstruct},
                             100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(WString_ConcatWcsLeft_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split,
                                     &WString_AssignSubstr, &WString_Erase, &WString_CopyConstruct,
                                     &WrongConcatWcsLeft},
                             100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(WString_CopyConstruct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split,
                                     &WString_AssignSubstr, &WString_Erase, &WrongCopyConstruct},
                             100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(WString_Erase_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWStringOperations(probe, {&WString_Tidy, &WString_Copy, &WString_Grow, &WString_Split,
                                     &WString_AssignSubstr, &WrongErase},
                             100);
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

namespace {

// Wrong: the allocator byte is not taken from the argument.
WString* __fastcall WrongFromWcs(WString* self, void* edx, const std::uint16_t* text, const std::uint8_t* allocator) {
    const std::uint8_t mine = 0;
    return WString_FromWcs(self, edx, text, *allocator == 0 ? allocator : &mine);
}

// Wrong: the text's last character is left out.
WString* __cdecl WrongConcatWcs(WString* result, const WString* left, const std::uint16_t* text) {
    WString_ConcatWcs(result, left, text);
    if (text[0] != 0 && result->len > 0) {
        result->len = result->len - 1;
    }
    return result;
}

// Wrong: never shares the buffer.
WString* __fastcall WrongAssignCopy(WString* self, void* edx, const WString* x) {
    if (self == x) {
        return WString_AssignCopy(self, edx, x);
    }
    if (WString_Grow(self, nullptr, x->len, 1)) {
        CopyWords(self->ptr, CStr(x), x->len);
        self->len = x->len;
        self->ptr[x->len] = 0;
    }
    return self;
}

}  // namespace

S3RE_TEST(WString_LogicMembers_ComparisonDetectsWrongImplementation) {
    const WStringOps right{&WString_Tidy,           &WString_Copy,          &WString_Grow,
                           &WString_Split,          &WString_AssignSubstr,  &WString_Erase,
                           &WString_CopyConstruct,  &WString_ConcatWcsLeft, &WString_SubstrConstruct,
                           &WString_FromWcs,        &WString_ConcatWcs,     &WString_AssignCopy};
    WStringOps wrongs[3] = {right, right, right};
    wrongs[0].fromWcs = &WrongFromWcs;
    wrongs[1].concatWcs = &WrongConcatWcs;
    wrongs[2].assignCopy = &WrongAssignCopy;
    for (const WStringOps& wrong : wrongs) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareWStringOperations(probe, wrong, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::msvc6
