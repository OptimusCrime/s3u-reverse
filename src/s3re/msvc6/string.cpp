// std::string (MSVC 6.0) member functions. See string.h for the layout.
//
// The code follows the MSVC 6.0 <xstring> (Dinkumware) implementation, which
// the disassembly matches function by function; the comments name the
// pieces of it that the compiler inlined. Constants:
//   max_size() = 0xFFFFFFFD (npos - 2), _MIN_SIZE = 31, _FROZEN = 0xFF.
// Game functions used (cdecl): operator new 0x61efb9, operator delete
// 0x61e4ef, memmove 0x61e860, std::_Xlen 0x61e4fa, std::_Xran 0x61e646.
// std::string::npos is read from its static member at 0x655390 (0xFFFFFFFF),
// as the original does; c_str() of a string without buffer is the static
// empty string at 0x6553a0, whose "count byte" (0x65539f) assign() reads and
// can even increment.

#include "msvc6/string.h"

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstring>
#include <string>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uint32_t kMaxSize = 0xFFFFFFFDu;
constexpr std::uint32_t kMinSize = 31;
constexpr std::uint8_t kFrozen = 0xFF;

void* GameNew(std::int32_t size) {
    using Fn = void*(__cdecl*)(std::int32_t);
    return reinterpret_cast<Fn>(0x0061efb9)(size);
}

void GameDelete(void* block) {
    using Fn = void(__cdecl*)(void*);
    reinterpret_cast<Fn>(0x0061e4ef)(block);
}

void GameMemmove(void* to, const void* from, std::uint32_t count) {
    using Fn = void*(__cdecl*)(void*, const void*, std::uint32_t);
    reinterpret_cast<Fn>(0x0061e860)(to, from, count);
}

// Throw the game's length_error / out_of_range (they don't return in the
// game; the code after them is what the original does as well).
void GameXlen() {
    reinterpret_cast<void(__cdecl*)()>(0x0061e4fa)();
}

void GameXran() {
    reinterpret_cast<void(__cdecl*)()>(0x0061e646)();
}

std::uint32_t Npos() {
    return game::g_stringNpos;
}

char* NullString() {
    return game::g_stringNullBuffer;
}

std::uint8_t& RefCount(char* ptr) {
    return reinterpret_cast<std::uint8_t*>(ptr)[-1];
}

// c_str()
char* CStr(const String* s) {
    return s->ptr != nullptr ? s->ptr : NullString();
}

// The inlined "drop one reference": free an unshared or frozen buffer,
// otherwise decrement its count.
void Release(char* ptr) {
    const std::uint8_t count = RefCount(ptr);
    if (count == 0 || count == kFrozen) {
        GameDelete(ptr - 1);
    } else {
        RefCount(ptr) = static_cast<std::uint8_t>(count - 1);
    }
}

bool IsShared(const String* s) {
    if (s->ptr == nullptr) {
        return false;
    }
    const std::uint8_t count = RefCount(s->ptr);
    return count != 0 && count != kFrozen;
}

void CopyChars(char* to, const char* from, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        to[i] = from[i];
    }
}

std::uint32_t Length(const char* s) {
    std::uint32_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

void Clear(String* s) {
    s->ptr = nullptr;
    s->len = 0;
    s->res = 0;
}

// erase(p0, m)
void Erase(String* self, std::uint32_t p0, std::uint32_t m) {
    if (self->len < p0) {
        GameXran();
    }
    String_Split(self, nullptr);
    if (self->len - p0 < m) {
        m = self->len - p0;
    }
    if (m > 0) {
        GameMemmove(self->ptr + p0, self->ptr + p0 + m, self->len - p0 - m);
        const std::uint32_t n = self->len - m;
        if (String_Grow(self, nullptr, n, 0)) {
            String_Eos(self, nullptr, n);
        }
    }
}

}  // namespace

namespace detail {
std::uint32_t Npos() { return msvc6::Npos(); }
char* CStr(const String* s) { return msvc6::CStr(s); }
void Release(char* ptr) { msvc6::Release(ptr); }
void Erase(String* self, std::uint32_t p0, std::uint32_t m) { msvc6::Erase(self, p0, m); }
void Clear(String* s) { msvc6::Clear(s); }
std::uint32_t Length(const char* s) { return msvc6::Length(s); }
std::uint32_t TidyFreeReturningEax(String* s) {
    std::uint32_t eax = 0;
    if (char* ptr = s->ptr; ptr != nullptr) {
        const std::uint8_t count = RefCount(ptr);
        if (count == 0 || count == kFrozen) {
            using Fn = std::uint32_t(__cdecl*)(void*);
            eax = reinterpret_cast<Fn>(0x0061e4ef)(ptr - 1);
        } else {
            RefCount(ptr) = static_cast<std::uint8_t>(count - 1);
            eax = (reinterpret_cast<std::uintptr_t>(ptr) & 0xFFFFFF00u) | RefCount(ptr);
        }
    }
    Clear(s);
    return eax;
}
}  // namespace detail

// ---------------------------------------------------------------------------
// _Eos (0x00401f60)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401f60, String_Eos, StringEosFn, Original_String_Eos, String_Eos);

// Purpose: Part of the C++ standard string the game was built with: sets a string's length and writes the
// terminating zero. Game data structures contain such strings, so we need the exact same
// behaviour.
void __fastcall String_Eos(String* self, void* /*edx*/, std::uint32_t n) {
    self->len = n;
    self->ptr[n] = '\0';
}

// ---------------------------------------------------------------------------
// _Tidy (0x00402170), _Tidy(true) (0x00401d20), max_size (0x00402260)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00402170, String_Tidy, StringTidyFn, Original_String_Tidy, String_Tidy);

// Purpose: Standard string helper: forgets a string's text, releasing its buffer
// first if asked to (used by destructors and assignments everywhere).
void __fastcall String_Tidy(String* self, void* /*edx*/, std::uint32_t built) {
    if ((built & 0xFF) != 0 && self->ptr != nullptr) {
        Release(self->ptr);
    }
    Clear(self);
}

S3RE_HOOK(0x00401d20, String_TidyFree, StringTidyFreeFn, Original_String_TidyFree,
          String_TidyFree);

// Purpose: Standard string helper: releases a string's buffer and empties it
// (an out-of-line copy of _Tidy(true)).
void __fastcall String_TidyFree(String* self, void* /*edx*/) {
    if (self->ptr != nullptr) {
        Release(self->ptr);
    }
    Clear(self);
}

S3RE_HOOK(0x00402260, String_MaxSize, StringMaxSizeFn, Original_String_MaxSize, String_MaxSize);

// Purpose: Standard string helper: the longest possible string length.
std::uint32_t __fastcall String_MaxSize(const String* /*self*/, void* /*edx*/) {
    return kMaxSize;
}

// ---------------------------------------------------------------------------
// _Copy (0x00402270; continues at 0x004022f0, which is not a function of its
// own but the code after the try block)
//
// Allocation is `try { new(ns + 2) } catch (...) { ns = n; new(n + 2) }`.
// The game's operator new returns null instead of throwing, so the catch
// branch never runs; ours has no catch.
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00402270, String_Copy, StringCopyFn, Original_String_Copy, String_Copy);

// Purpose: Standard string helper: moves a string's text into a new buffer of
// its own, big enough for n characters (rounded up to 31).
void __fastcall String_Copy(String* self, void* /*edx*/, std::uint32_t n) {
    std::uint32_t capacity = n | kMinSize;
    if (capacity > kMaxSize) {
        capacity = n;
    }
    std::int32_t bytes = static_cast<std::int32_t>(capacity + 2);
    if (bytes < 0) {
        bytes = 0;  // the allocator's "negative count" check
    }
    char* block = static_cast<char*>(GameNew(bytes));
    if (self->len > 0) {
        CopyChars(block + 1, self->ptr, self->len > capacity ? capacity : self->len);
    }
    const std::uint32_t oldLength = self->len;
    String_TidyFree(self, nullptr);  // _Tidy(true), inlined in the original
    self->ptr = block + 1;
    RefCount(self->ptr) = 0;
    self->res = capacity;
    String_Eos(self, nullptr, oldLength > capacity ? capacity : oldLength);
}

// ---------------------------------------------------------------------------
// _Grow (0x00401f80)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401f80, String_Grow, StringGrowFn, Original_String_Grow, String_Grow);

// Purpose: Standard string helper: before a string is overwritten, makes sure
// it has an unshared buffer with room for n characters (or empties it for 0).
bool __fastcall String_Grow(String* self, void* /*edx*/, std::uint32_t n, std::uint32_t trim) {
    const bool trimming = (trim & 0xFF) != 0;
    if (n > kMaxSize) {
        GameXlen();
    }
    if (IsShared(self)) {
        if (n == 0) {
            --RefCount(self->ptr);
            Clear(self);  // _Tidy()
            return false;
        }
        String_Copy(self, nullptr, n);
        return true;
    }
    if (n == 0) {
        if (trimming) {
            String_TidyFree(self, nullptr);
        } else if (self->ptr != nullptr) {
            String_Eos(self, nullptr, 0);
        }
        return false;
    }
    if (trimming && (kMinSize < self->res || self->res < n)) {
        String_TidyFree(self, nullptr);
        String_Copy(self, nullptr, n);
    } else if (!trimming && self->res < n) {
        String_Copy(self, nullptr, n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// assign(const char*, n) (0x004021c0), _Split (0x004020a0)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x004021c0, String_AssignChars, StringAssignCharsFn, Original_String_AssignChars,
          String_AssignChars);

// Purpose: Standard string: sets a string to the first n characters at s.
String* __fastcall String_AssignChars(String* self, void* /*edx*/, const char* s, std::uint32_t n) {
    if (String_Grow(self, nullptr, n, 1)) {
        CopyChars(self->ptr, s, n);
        String_Eos(self, nullptr, n);
    }
    return self;
}

S3RE_HOOK(0x004020a0, String_Split, StringSplitFn, Original_String_Split, String_Split);

// Purpose: Standard string helper: a string that shares its buffer with copies
// gets its own copy of the text before it is changed (copy on write).
void __fastcall String_Split(String* self, void* /*edx*/) {
    if (IsShared(self)) {
        char* shared = self->ptr;
        String_TidyFree(self, nullptr);
        String_AssignChars(self, nullptr, shared, Length(shared));
    }
}

// ---------------------------------------------------------------------------
// assign(const string&, pos, n) (0x00401d60)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401d60, String_AssignSubstr, StringAssignSubstrFn, Original_String_AssignSubstr,
          String_AssignSubstr);

// Purpose: Standard string: sets a string to (part of) another string; a full
// copy shares the other string's buffer instead of copying it.
String* __fastcall String_AssignSubstr(String* self, void* /*edx*/, const String* x, std::uint32_t pos,
                                       std::uint32_t n) {
    if (x->len < pos) {
        GameXran();
    }
    std::uint32_t count = x->len - pos;
    if (n < count) {
        count = n;
    }
    if (self == x) {
        Erase(self, pos + count, Npos());
        Erase(self, 0, pos);
    } else if (count > 0 && count == x->len && RefCount(CStr(x)) < kFrozen - 1) {
        String_TidyFree(self, nullptr);
        self->ptr = CStr(x);
        self->len = x->len;
        self->res = x->res;
        ++RefCount(self->ptr);
    } else if (String_Grow(self, nullptr, count, 1)) {
        CopyChars(self->ptr, CStr(x) + pos, count);
        String_Eos(self, nullptr, count);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Destructor (0x00401b60)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401b60, String_Destroy, StringDestroyFn, Original_String_Destroy, String_Destroy);

// Purpose: Standard string destructor (the compiled form: erase() first, then
// release the buffer). Called at the end of every scope that has a string.
void __fastcall String_Destroy(String* self, void* /*edx*/) {
    Erase(self, 0, Npos());
    String_TidyFree(self, nullptr);
}

// ---------------------------------------------------------------------------
// Constructors (0x00401a10, 0x00407880)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401a10, String_FromChars, StringFromCharsFn, Original_String_FromChars,
          String_FromChars);

// Purpose: Standard string constructor from C text (at most n characters); used
// by nearly 2,000 static initializers that set up the game's global strings.
// The compiled code builds it through two temporaries: string(s), then
// string(that, 0, n), then a copy of that.
String* __fastcall String_FromChars(String* self, void* /*edx*/, const char* s, std::uint32_t n) {
    // The allocator byte is copied from the low byte of the first argument
    // (the original's allocator parameter shares that stack slot).
    self->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(s));
    Clear(self);
    if (s != nullptr) {
        String text{};
        String_AssignChars(&text, nullptr, s, Length(s));
        String part{};
        String_AssignSubstr(&part, nullptr, &text, 0, n);
        String_AssignSubstr(self, nullptr, &part, 0, Npos());
        if (part.ptr != nullptr) {
            Release(part.ptr);
        }
        if (text.ptr != nullptr) {
            Release(text.ptr);
        }
    }
    return self;
}

S3RE_HOOK(0x00407880, String_Default, StringDefaultFn, Original_String_Default, String_Default);

// Purpose: Standard string constructor: an empty string.
String* __fastcall String_Default(String* self, void* /*edx*/, const std::uint8_t* allocator) {
    self->allocator = *allocator;
    Clear(self);
    return self;
}

// ---------------------------------------------------------------------------
// operator+(const string&, const char*) (0x00402380), and the helper that
// builds left + right or a copy of left (0x00401c70)
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00402380, String_PlusChars, StringPlusCharsFn, Original_String_PlusChars,
          String_PlusChars);

// Purpose: Standard string concatenation (string + C text), e.g. registry key
// paths built from a base path and a value name.
String* __cdecl String_PlusChars(String* result, const String* left, const char* right) {
    String sum{};
    sum.allocator = left->allocator;
    String_AssignSubstr(&sum, nullptr, left, 0, Npos());
    // append(right)
    const std::uint32_t count = Length(right);
    if (Npos() - sum.len <= count) {
        GameXlen();
    }
    if (count > 0) {
        const std::uint32_t total = sum.len + count;
        if (String_Grow(&sum, nullptr, total, 0)) {
            CopyChars(sum.ptr + sum.len, right, count);
            String_Eos(&sum, nullptr, total);
        }
    }
    result->allocator = sum.allocator;
    Clear(result);
    String_AssignSubstr(result, nullptr, &sum, 0, Npos());
    if (sum.ptr != nullptr) {
        Release(sum.ptr);
    }
    return result;
}

S3RE_HOOK(0x00401c70, String_ConcatChars, StringConcatCharsFn, Original_String_ConcatChars,
          String_ConcatChars);

// Purpose: Builds a string as base + suffix (or just the base when there is no
// suffix); the game's static initializers use it for registry paths such as
// "Software\BlueByte\Siedler3\1.0" + a key name.
String* __fastcall String_ConcatChars(String* self, const String* left, const char* right) {
    if (right != nullptr) {
        String sum{};
        String_PlusChars(&sum, left, right);
        self->allocator = sum.allocator;
        Clear(self);
        String_AssignSubstr(self, nullptr, &sum, 0, Npos());
        if (sum.ptr != nullptr) {
            Release(sum.ptr);
        }
    } else {
        self->allocator = 0;  // the low byte of `right`
        Clear(self);
        String_AssignSubstr(self, nullptr, left, 0, 0xFFFFFFFFu);
    }
    return self;
}

S3RE_HOOK(0x00437480, String_ConcatChars2, StringConcatCharsFn, Original_String_ConcatChars2,
          String_ConcatChars2);

// Purpose: The same helper as String_ConcatChars (base + optional suffix),
// compiled a second time into another source file (used by the framework's
// error messages and UI code).
String* __fastcall String_ConcatChars2(String* self, const String* left, const char* right) {
    return String_ConcatChars(self, left, right);
}

// ---------------------------------------------------------------------------
// Batch 22: the remaining out-of-line members. Most of them are the compiled
// form of a member with assign()/_Grow()/erase() inlined; ours call the
// members above instead, which do the same steps in the same order.
// ---------------------------------------------------------------------------

namespace {

// The text the game assigns instead of a null C string (a zero byte in
// S3.EXE's data, texts::kEmpty); read at run time, like the original.
const char* EmptyText() {
    return game::texts::kEmpty;
}

// self = a copy of the temporary, which is then dropped (the pattern every
// operator+ and helper below ends with).
void CopyAndDrop(String* self, String* temporary) {
    self->allocator = temporary->allocator;
    Clear(self);
    String_AssignSubstr(self, nullptr, temporary, 0, Npos());
    if (temporary->ptr != nullptr) {
        Release(temporary->ptr);
    }
}

// append(x) (as append(x, 0, npos) inlined into the operator+ functions).
void AppendString(String* self, const String* x) {
    std::uint32_t count = Npos();
    if (x->len < count) {
        count = x->len;
    }
    if (Npos() - self->len <= count) {
        GameXlen();
    }
    if (count > 0) {
        const std::uint32_t total = self->len + count;
        if (String_Grow(self, nullptr, total, 0)) {
            CopyChars(self->ptr + self->len, CStr(x), count);
            String_Eos(self, nullptr, total);
        }
    }
}

}  // namespace

S3RE_HOOK(0x004078a0, String_FromSubstr, StringFromSubstrFn, Original_String_FromSubstr,
          String_FromSubstr);

// Purpose: Standard string constructor from part of another string
// (string(x, pos, n)).
String* __fastcall String_FromSubstr(String* self, void* /*edx*/, const String* x, std::uint32_t pos,
                                     std::uint32_t n, const std::uint8_t* allocator) {
    self->allocator = *allocator;
    Clear(self);
    return String_AssignSubstr(self, nullptr, x, pos, n);
}

S3RE_HOOK(0x00408cc0, String_FromSubstrDefault, StringFromSubstrDefaultFn, Original_String_FromSubstrDefault,
          String_FromSubstrDefault);

// Purpose: Standard string constructor from a part of another string, with
// the default allocator (used to return strings by value, e.g. the text of
// an assertion message).
String* __fastcall String_FromSubstrDefault(String* self, void* /*edx*/, const String* x, std::uint32_t pos,
                                            std::uint32_t n) {
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(x));
    return String_FromSubstr(self, nullptr, x, pos, n, &allocator);
}

S3RE_HOOK(0x00407a50, String_FromText, StringFromTextFn, Original_String_FromText, String_FromText);

// Purpose: Standard string constructor from C text (string(s)), used wherever
// the game turns a literal or a buffer into a string.
String* __fastcall String_FromText(String* self, void* /*edx*/, const char* s,
                                   const std::uint8_t* allocator) {
    self->allocator = *allocator;
    Clear(self);
    return String_AssignChars(self, nullptr, s, Length(s));
}

S3RE_HOOK(0x0041f430, String_AssignRepeat, StringAssignRepeatFn, Original_String_AssignRepeat,
          String_AssignRepeat);

// Purpose: Standard string: sets a string to n copies of one character.
String* __fastcall String_AssignRepeat(String* self, void* /*edx*/, std::uint32_t n, std::uint32_t c) {
    if (n == Npos()) {
        GameXlen();
    }
    if (String_Grow(self, nullptr, n, 1)) {
        for (std::uint32_t i = 0; i < n; ++i) {
            self->ptr[i] = static_cast<char>(c);
        }
        String_Eos(self, nullptr, n);
    }
    return self;
}

S3RE_HOOK(0x005f3bf0, String_FromRepeat, StringFromRepeatFn, Original_String_FromRepeat,
          String_FromRepeat);

// Purpose: Standard string constructor: n copies of one character.
String* __fastcall String_FromRepeat(String* self, void* /*edx*/, std::uint32_t n, std::uint32_t c,
                                     const std::uint8_t* allocator) {
    self->allocator = *allocator;
    Clear(self);
    return String_AssignRepeat(self, nullptr, n, c);
}

S3RE_HOOK(0x00407b10, String_Erase, StringEraseFn, Original_String_Erase, String_Erase);

// Purpose: Standard string: removes n characters starting at pos.
String* __fastcall String_Erase(String* self, void* /*edx*/, std::uint32_t pos, std::uint32_t n) {
    Erase(self, pos, n);
    return self;
}

S3RE_HOOK(0x00409a90, String_AssignText, StringAssignTextFn, Original_String_AssignText,
          String_AssignText);

// Purpose: Standard string: assigns C text (string = "...").
String* __fastcall String_AssignText(String* self, void* /*edx*/, const char* s) {
    return String_AssignChars(self, nullptr, s, Length(s));
}

S3RE_HOOK(0x00413d70, String_AssignTextOrEmpty, StringAssignTextOrEmptyFn,
          Original_String_AssignTextOrEmpty, String_AssignTextOrEmpty);

// Purpose: Assigns C text that may be null (then the empty text); probably a
// game helper for optional texts. The text goes through a temporary string,
// as in the original.
String* __fastcall String_AssignTextOrEmpty(String* self, void* /*edx*/, const char* s) {
    if (s != nullptr) {
        String text{};
        text.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(s));
        String_AssignChars(&text, nullptr, s, Length(s));
        String_AssignSubstr(self, nullptr, &text, 0, Npos());
        if (text.ptr != nullptr) {
            Release(text.ptr);
        }
    } else {
        String_AssignChars(self, nullptr, EmptyText(), Length(EmptyText()));
    }
    return self;
}

S3RE_HOOK(0x00414490, String_Freeze, StringFreezeFn, Original_String_Freeze, String_Freeze);

// Purpose: Standard string helper: before the game gets a writable pointer
// into a string, gives it an unshared buffer and marks the buffer as never
// to be shared again (count 0xFF).
void __fastcall String_Freeze(String* self, void* /*edx*/) {
    if (IsShared(self)) {
        String_Grow(self, nullptr, self->len, 0);
    }
    if (self->ptr != nullptr) {
        RefCount(self->ptr) = kFrozen;
    }
}

S3RE_HOOK(0x004142d0, String_At, StringAtFn, Original_String_At, String_At);

// Purpose: Standard string: writable access to one character (s[pos]); a
// position past the end gives the static empty string.
char* __fastcall String_At(String* self, void* /*edx*/, std::uint32_t pos) {
    if (self->len < pos || self->ptr == nullptr) {
        return NullString();
    }
    String_Freeze(self, nullptr);
    // A shared empty string loses its buffer in _Freeze; the original then
    // returns null + pos as well.
    return reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self->ptr) + pos);
}

S3RE_HOOK(0x00414370, String_IndexConst, StringIndexConstFn, Original_String_IndexConst, String_IndexConst);

// Purpose: Standard string: read access to one character (s[pos] on a const
// string); without a buffer the static empty string, whatever pos is.
const char* __fastcall String_IndexConst(const String* self, void* /*edx*/, std::uint32_t pos) {
    if (self->ptr == nullptr) {
        return NullString();
    }
    return reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(self->ptr) + pos);
}

S3RE_HOOK(0x0042f920, String_AppendSubstr, StringAppendSubstrFn, Original_String_AppendSubstr,
          String_AppendSubstr);

// Purpose: Standard string: append part of another string
// (append(x, pos, n)), e.g. when the console and the menus build texts.
// The original has _Grow inlined; it behaves as String_Grow(total, false).
String* __fastcall String_AppendSubstr(String* self, void* /*edx*/, const String* x, std::uint32_t pos,
                                       std::uint32_t n) {
    if (x->len < pos) {
        GameXran();
    }
    std::uint32_t count = x->len - pos;
    if (n < count) {
        count = n;
    }
    if (Npos() - self->len <= count) {
        GameXlen();
    }
    if (count > 0) {
        const std::uint32_t total = self->len + count;
        if (String_Grow(self, nullptr, total, 0)) {
            CopyChars(self->ptr + self->len, CStr(x) + pos, count);
            String_Eos(self, nullptr, total);
        }
    }
    return self;
}

S3RE_HOOK(0x00409e60, String_PlusString, StringPlusStringFn, Original_String_PlusString,
          String_PlusString);

// Purpose: Standard string concatenation (string + string), e.g. paths and
// messages built from pieces.
String* __cdecl String_PlusString(String* result, const String* left, const String* right) {
    String sum{};
    sum.allocator = left->allocator;
    String_AssignSubstr(&sum, nullptr, left, 0, Npos());
    AppendString(&sum, right);
    CopyAndDrop(result, &sum);
    return result;
}

S3RE_HOOK(0x00409f90, String_CharsPlus, StringCharsPlusFn, Original_String_CharsPlus,
          String_CharsPlus);

// Purpose: Standard string concatenation (C text + string).
String* __cdecl String_CharsPlus(String* result, const char* left, const String* right) {
    String sum{};
    // The allocator byte is the low byte of `left` (its stack slot).
    sum.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(left));
    String_AssignChars(&sum, nullptr, left, Length(left));
    AppendString(&sum, right);
    CopyAndDrop(result, &sum);
    return result;
}

S3RE_HOOK(0x00421aa0, String_PlusChar, StringPlusCharFn, Original_String_PlusChar, String_PlusChar);

// Purpose: Standard string concatenation (string + one character).
String* __cdecl String_PlusChar(String* result, const String* left, std::uint32_t c) {
    String sum{};
    sum.allocator = left->allocator;
    String_AssignSubstr(&sum, nullptr, left, 0, Npos());
    // append(1, c); this one compares against a literal npos.
    if (0xFFFFFFFFu - sum.len <= 1) {
        GameXlen();
    }
    const std::uint32_t total = sum.len + 1;
    if (String_Grow(&sum, nullptr, total, 0)) {
        sum.ptr[sum.len] = static_cast<char>(c);
        String_Eos(&sum, nullptr, total);
    }
    CopyAndDrop(result, &sum);
    return result;
}

S3RE_HOOK(0x004077f0, String_ConcatString, StringConcatStringFn, Original_String_ConcatString,
          String_ConcatString);

// Purpose: Builds a string as left + right (both strings); a helper the
// compiler generated for expressions like `string s = a + b;`.
String* __fastcall String_ConcatString(String* self, const String* left, const String* right) {
    String sum{};
    String_PlusString(&sum, left, right);
    CopyAndDrop(self, &sum);
    return self;
}

namespace {

String* ConcatCharsLeft(String* self, const char* left, const String* right) {
    if (left != nullptr) {
        String sum{};
        String_CharsPlus(&sum, left, right);
        CopyAndDrop(self, &sum);
    } else {
        self->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(right));
        Clear(self);
        String_AssignSubstr(self, nullptr, right, 0, 0xFFFFFFFFu);
    }
    return self;
}

}  // namespace

S3RE_HOOK(0x00408e50, String_ConcatCharsLeft, StringConcatCharsLeftFn,
          Original_String_ConcatCharsLeft, String_ConcatCharsLeft);

// Purpose: Builds a string as text + right, or as a copy of right when there
// is no text (a compiler-generated helper like String_ConcatChars).
String* __fastcall String_ConcatCharsLeft(String* self, const char* left, const String* right) {
    return ConcatCharsLeft(self, left, right);
}

S3RE_HOOK(0x0043d8e0, String_ConcatCharsLeft2, StringConcatCharsLeftFn,
          Original_String_ConcatCharsLeft2, String_ConcatCharsLeft2);

// Purpose: The same helper as String_ConcatCharsLeft, compiled a second time
// into another source file.
String* __fastcall String_ConcatCharsLeft2(String* self, const char* left, const String* right) {
    return ConcatCharsLeft(self, left, right);
}

S3RE_HOOK(0x00420cb0, String_ConcatChar, StringConcatCharFn, Original_String_ConcatChar,
          String_ConcatChar);

// Purpose: Builds a string as left + one character (compiler-generated
// helper).
String* __fastcall String_ConcatChar(String* self, const String* left, std::uint32_t c) {
    String sum{};
    String_PlusChar(&sum, left, c);
    CopyAndDrop(self, &sum);
    return self;
}

// ---------------------------------------------------------------------------
// compare(pos, n0, const char* s, count) (0x0040a140)
// ---------------------------------------------------------------------------

using StringCompareCharsFn = std::int32_t(__fastcall*)(const String* self, void* edx, std::uint32_t pos,
                                                       std::uint32_t n0, const char* s, std::uint32_t count);
std::int32_t __fastcall String_CompareChars(const String* self, void* edx, std::uint32_t pos, std::uint32_t n0,
                                            const char* s, std::uint32_t count);

S3RE_HOOK(0x0040a140, String_CompareChars, StringCompareCharsFn, Original_String_CompareChars,
          String_CompareChars);

// Purpose: Standard string comparison of a part of the string (n0
// characters from pos) with `count` characters: -1, 0 or 1, comparing bytes
// unsigned, a shorter equal prefix first. Used e.g. to test for an empty
// tooltip text.
std::int32_t __fastcall String_CompareChars(const String* self, void* /*edx*/, std::uint32_t pos, std::uint32_t n0,
                                            const char* s, std::uint32_t count) {
    if (self->len < pos) {
        GameXran();
    }
    std::uint32_t n = self->len - pos;
    if (n0 < n) {
        n = n0;
    }
    const std::uint32_t compared = n < count ? n : count;
    const auto* p = reinterpret_cast<const unsigned char*>(self->ptr == nullptr ? nullptr : self->ptr + pos);
    const auto* q = reinterpret_cast<const unsigned char*>(s);
    for (std::uint32_t i = 0; i < compared; ++i) {
        if (p[i] != q[i]) {
            return p[i] < q[i] ? -1 : 1;
        }
    }
    if (n < count) {
        return -1;
    }
    return n != count ? 1 : 0;
}

// compare(const char* s) (0x0040a0e0): compare(0, npos, s, strlen(s)).
using StringCompareTextFn = std::int32_t(__fastcall*)(const String* self, void* edx, const char* s);
std::int32_t __fastcall String_CompareText(const String* self, void* edx, const char* s);

S3RE_HOOK(0x0040a0e0, String_CompareText, StringCompareTextFn, Original_String_CompareText, String_CompareText);

// Purpose: Standard string comparison with C text (-1, 0 or 1); the menus
// and the console use it to look up names.
std::int32_t __fastcall String_CompareText(const String* self, void* /*edx*/, const char* s) {
    return String_CompareChars(self, nullptr, 0, 0xFFFFFFFFu, s, Length(s));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Runs `candidate` and the original _Eos on identical random strings and
// compares the resulting objects and buffers byte for byte.
void CompareEos(test::Context& ctx, StringEosFn candidate, int iterations) {
    constexpr std::uint32_t kGuard = 8;  // catch writes past the buffer
    for (int i = 0; i < iterations; ++i) {
        const std::uint32_t capacity = ctx.rng().Below(64);
        const std::uint32_t n = ctx.rng().Below(capacity + 1);
        const std::uint32_t size = capacity + 2 + kGuard;  // count + chars + zero + guard

        std::vector<unsigned char> bufferA(size);
        ctx.rng().Fill(bufferA.data(), size);
        std::vector<unsigned char> bufferB = bufferA;

        String a{};
        a.allocator = static_cast<std::uint8_t>(ctx.rng().Next32());
        a.ptr = reinterpret_cast<char*>(bufferA.data() + 1);
        a.len = ctx.rng().Below(capacity + 1);
        a.res = capacity;
        String b = a;
        b.ptr = reinterpret_cast<char*>(bufferB.data() + 1);

        Original_String_Eos(&a, nullptr, n);
        candidate(&b, nullptr, n);

        bool same = true;
        same &= S3RE_EXPECT_EQ(a.allocator, b.allocator);
        same &= S3RE_EXPECT_EQ(a.len, b.len);
        same &= S3RE_EXPECT_EQ(a.res, b.res);
        same &= S3RE_EXPECT(a.ptr == reinterpret_cast<char*>(bufferA.data() + 1));
        same &= S3RE_EXPECT(b.ptr == reinterpret_cast<char*>(bufferB.data() + 1));
        same &= S3RE_EXPECT_MEM_EQ(bufferA.data(), bufferB.data(), size);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  inputs: capacity=%u n=%u (iteration %d)",
                     capacity, n, i);
            return;
        }
    }
}

// A deliberately wrong _Eos (forgets the terminating zero when n is 0).
void __fastcall WrongEos(String* self, void* /*edx*/, std::uint32_t n) {
    self->len = n;
    if (n != 0) {
        self->ptr[n] = '\0';
    }
}

// The string operations of one side of the random-operations test.
struct StringOps {
    StringTidyFn tidy;
    StringTidyFreeFn tidyFree;
    StringCopyFn copy;
    StringGrowFn grow;
    StringSplitFn split;
    StringAssignCharsFn assignChars;
    StringAssignSubstrFn assignSubstr;
    StringDestroyFn destroy;
    StringFromCharsFn fromChars;
    StringDefaultFn makeDefault;
    StringPlusCharsFn plusChars;
    StringConcatCharsFn concatChars;
    // Batch 22
    StringFromSubstrFn fromSubstr;
    StringFromTextFn fromText;
    StringFromRepeatFn fromRepeat;
    StringEraseFn erase;
    StringAssignTextFn assignText;
    StringAssignRepeatFn assignRepeat;
    StringAssignTextOrEmptyFn assignTextOrEmpty;
    StringAtFn at;
    StringFreezeFn freeze;
    StringPlusStringFn plusString;
    StringCharsPlusFn charsPlus;
    StringPlusCharFn plusChar;
    StringConcatStringFn concatString;
    StringConcatCharsLeftFn concatCharsLeft;
    StringConcatCharsLeftFn concatCharsLeft2;
    StringConcatCharFn concatChar;
    StringConcatCharsFn concatChars2;
    StringFromSubstrDefaultFn fromSubstrDefault;
    // Session 56
    StringIndexConstFn indexConst;
    StringAppendSubstrFn appendSubstr;
};

StringOps OriginalOps() {
    return {Original_String_Tidy,        Original_String_TidyFree,  Original_String_Copy,
            Original_String_Grow,        Original_String_Split,     Original_String_AssignChars,
            Original_String_AssignSubstr, Original_String_Destroy,  Original_String_FromChars,
            Original_String_Default,     Original_String_PlusChars, Original_String_ConcatChars,
            Original_String_FromSubstr,  Original_String_FromText,  Original_String_FromRepeat,
            Original_String_Erase,       Original_String_AssignText, Original_String_AssignRepeat,
            Original_String_AssignTextOrEmpty, Original_String_At,  Original_String_Freeze,
            Original_String_PlusString,  Original_String_CharsPlus, Original_String_PlusChar,
            Original_String_ConcatString, Original_String_ConcatCharsLeft,
            Original_String_ConcatCharsLeft2, Original_String_ConcatChar, Original_String_ConcatChars2,
            Original_String_FromSubstrDefault, Original_String_IndexConst, Original_String_AppendSubstr};
}

StringOps OurOps() {
    return {&String_Tidy,        &String_TidyFree,  &String_Copy,        &String_Grow,
            &String_Split,       &String_AssignChars, &String_AssignSubstr, &String_Destroy,
            &String_FromChars,   &String_Default,   &String_PlusChars,   &String_ConcatChars,
            &String_FromSubstr,  &String_FromText,  &String_FromRepeat,  &String_Erase,
            &String_AssignText,  &String_AssignRepeat, &String_AssignTextOrEmpty, &String_At,
            &String_Freeze,      &String_PlusString, &String_CharsPlus,  &String_PlusChar,
            &String_ConcatString, &String_ConcatCharsLeft, &String_ConcatCharsLeft2,
            &String_ConcatChar, &String_ConcatChars2, &String_FromSubstrDefault, &String_IndexConst,
            &String_AppendSubstr};
}

// A deliberately wrong assign(string, pos, n): never shares the buffer.
String* __fastcall WrongAssignSubstr(String* self, void* /*edx*/, const String* x, std::uint32_t pos,
                                     std::uint32_t n) {
    std::uint32_t count = x->len - pos;
    if (n < count) {
        count = n;
    }
    if (self == x) {
        return String_AssignSubstr(self, nullptr, x, pos, n);
    }
    if (String_Grow(self, nullptr, count, 1)) {
        CopyChars(self->ptr, CStr(x) + pos, count);
        String_Eos(self, nullptr, count);
    }
    return self;
}

constexpr int kSlots = 6;

// Compares slot i of both sides: fields, text, count byte, and which other
// slots share its buffer.
bool SameStrings(test::Context& ctx, const String* a, const String* b) {
    bool same = true;
    for (int i = 0; i < kSlots && same; ++i) {
        same &= S3RE_EXPECT_EQ(a[i].allocator, b[i].allocator);
        same &= S3RE_EXPECT_EQ(a[i].ptr == nullptr, b[i].ptr == nullptr);
        same &= S3RE_EXPECT_EQ(a[i].len, b[i].len);
        same &= S3RE_EXPECT_EQ(a[i].res, b[i].res);
        if (!same || a[i].ptr == nullptr) {
            continue;
        }
        same &= S3RE_EXPECT_EQ(RefCount(a[i].ptr), RefCount(b[i].ptr));
        same &= S3RE_EXPECT_MEM_EQ(a[i].ptr, b[i].ptr, a[i].len + 1);
        for (int j = 0; j < i; ++j) {
            same &= S3RE_EXPECT_EQ(a[i].ptr == a[j].ptr, b[i].ptr == b[j].ptr);
        }
    }
    return same;
}

// Applies the same random sequence of string operations to two sets of
// strings, one through the original functions and one through `candidate`,
// on the game's real heap, and compares the strings after every operation.
// (With the hooks installed, the original functions' internal calls of other
// string functions land in ours; the emulator tests compare the originals
// on their own.)
void CompareStringOperations(test::Context& ctx, const StringOps& candidate, int sequences) {
    const StringOps original = OriginalOps();
    std::vector<std::string> texts;
    for (int i = 0; i < 24; ++i) {
        std::string text(ctx.rng().Below(i < 4 ? 4 : 90), 'x');
        for (char& c : text) {
            c = static_cast<char>(1 + ctx.rng().Below(255));
        }
        texts.push_back(text);
    }
    for (int sequence = 0; sequence < sequences; ++sequence) {
        String a[kSlots] = {};
        String b[kSlots] = {};
        for (int step = 0; step < 40; ++step) {
            const int op = static_cast<int>(ctx.rng().Below(32));
            const int i = static_cast<int>(ctx.rng().Below(kSlots));
            const int j = static_cast<int>(ctx.rng().Below(kSlots));
            const std::string& text = texts[ctx.rng().Below(static_cast<std::uint32_t>(texts.size()))];
            const std::uint32_t n = ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Below(100);
            const std::uint32_t flag = ctx.rng().Below(2) | (ctx.rng().Next32() & 0xFF00u);
            // A character for the repeat/append operations: never a zero, as
            // an embedded zero makes a later _Split shorten the string under
            // a running erase(), which crashes the original too.
            const std::uint32_t repeated = (ctx.rng().Next32() & 0xFFFFFF00u) | (1 + ctx.rng().Below(255));
            std::uint32_t resultA = 0;
            std::uint32_t resultB = 0;
            switch (op) {
                case 0:
                    original.fromChars(&a[i], nullptr, text.c_str(), n);
                    candidate.fromChars(&b[i], nullptr, text.c_str(), n);
                    break;
                case 1: {
                    const std::uint8_t allocator = static_cast<std::uint8_t>(n);
                    original.makeDefault(&a[i], nullptr, &allocator);
                    candidate.makeDefault(&b[i], nullptr, &allocator);
                    break;
                }
                case 2: {
                    const std::uint32_t count = n > text.size() ? static_cast<std::uint32_t>(text.size()) : n;
                    original.assignChars(&a[i], nullptr, text.c_str(), count);
                    candidate.assignChars(&b[i], nullptr, text.c_str(), count);
                    break;
                }
                case 3: {
                    const std::uint32_t pos = ctx.rng().Below(a[j].len + 1);
                    original.assignSubstr(&a[i], nullptr, &a[j], pos, n);
                    candidate.assignSubstr(&b[i], nullptr, &b[j], pos, n);
                    break;
                }
                case 4:
                    original.destroy(&a[i], nullptr);
                    candidate.destroy(&b[i], nullptr);
                    break;
                case 5:
                    original.tidy(&a[i], nullptr, flag);
                    candidate.tidy(&b[i], nullptr, flag);
                    break;
                case 6:
                    resultA = original.grow(&a[i], nullptr, n & 0x7F, flag);
                    resultB = candidate.grow(&b[i], nullptr, n & 0x7F, flag);
                    break;
                case 7:
                    original.copy(&a[i], nullptr, n & 0x7F);
                    candidate.copy(&b[i], nullptr, n & 0x7F);
                    break;
                case 8:
                    original.split(&a[i], nullptr);
                    candidate.split(&b[i], nullptr);
                    break;
                case 9:
                    original.plusChars(&a[i], &a[j], text.c_str());
                    candidate.plusChars(&b[i], &b[j], text.c_str());
                    break;
                case 10:
                case 28: {
                    const char* right = ctx.rng().Chance(1, 3) ? nullptr : text.c_str();
                    (op == 10 ? original.concatChars : original.concatChars2)(&a[i], &a[j], right);
                    (op == 10 ? candidate.concatChars : candidate.concatChars2)(&b[i], &b[j], right);
                    break;
                }
                case 11:
                    original.tidyFree(&a[i], nullptr);
                    candidate.tidyFree(&b[i], nullptr);
                    break;
                case 12: {
                    // Constructing a slot from itself empties it first, so
                    // only position 0 is valid then (else std::_Xran throws).
                    const std::uint32_t pos = i == j ? 0 : ctx.rng().Below(a[j].len + 1);
                    const std::uint8_t allocator = static_cast<std::uint8_t>(n >> 3);
                    // A constructor overwrites the slot without releasing it.
                    original.fromSubstr(&a[i], nullptr, &a[j], pos, n, &allocator);
                    candidate.fromSubstr(&b[i], nullptr, &b[j], pos, n, &allocator);
                    break;
                }
                case 29: {
                    // As case 12, with the allocator byte taken from the
                    // address of the source (differs between the sides).
                    const std::uint32_t pos = i == j ? 0 : ctx.rng().Below(a[j].len + 1);
                    const auto lowA = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(&a[j]));
                    const auto lowB = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(&b[j]));
                    original.fromSubstrDefault(&a[i], nullptr, &a[j], pos, n);
                    candidate.fromSubstrDefault(&b[i], nullptr, &b[j], pos, n);
                    resultA = a[i].allocator == lowA;
                    resultB = b[i].allocator == lowB;
                    a[i].allocator = b[i].allocator = 0;
                    break;
                }
                case 30: {
                    // append(a[j], pos, n): pos within the source (else std::_Xran throws).
                    const std::uint32_t pos = ctx.rng().Below(a[j].len + 1);
                    original.appendSubstr(&a[i], nullptr, &a[j], pos, n);
                    candidate.appendSubstr(&b[i], nullptr, &b[j], pos, n);
                    break;
                }
                case 31: {
                    // operator[] const: the same position in the respective
                    // buffer, or the static empty string.
                    const std::uint32_t pos = ctx.rng().Below(a[i].len + 3);
                    const char* pa = original.indexConst(&a[i], nullptr, pos);
                    const char* pb = candidate.indexConst(&b[i], nullptr, pos);
                    resultA = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pa) -
                                                         reinterpret_cast<std::uintptr_t>(a[i].ptr));
                    resultB = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pb) -
                                                         reinterpret_cast<std::uintptr_t>(b[i].ptr));
                    if (pa == NullString() || pb == NullString()) {
                        resultA = pa == NullString();
                        resultB = pb == NullString();
                    }
                    break;
                }
                case 13: {
                    const std::uint8_t allocator = static_cast<std::uint8_t>(n >> 3);
                    original.fromText(&a[i], nullptr, text.c_str(), &allocator);
                    candidate.fromText(&b[i], nullptr, text.c_str(), &allocator);
                    break;
                }
                case 14: {
                    const std::uint8_t allocator = static_cast<std::uint8_t>(n >> 3);
                    original.fromRepeat(&a[i], nullptr, n & 0x7F, repeated, &allocator);
                    candidate.fromRepeat(&b[i], nullptr, n & 0x7F, repeated, &allocator);
                    break;
                }
                case 15: {
                    const std::uint32_t pos = ctx.rng().Below(a[i].len + 1);
                    original.erase(&a[i], nullptr, pos, n);
                    candidate.erase(&b[i], nullptr, pos, n);
                    break;
                }
                case 16:
                    original.assignText(&a[i], nullptr, text.c_str());
                    candidate.assignText(&b[i], nullptr, text.c_str());
                    break;
                case 17:
                    original.assignRepeat(&a[i], nullptr, n & 0x7F, repeated);
                    candidate.assignRepeat(&b[i], nullptr, n & 0x7F, repeated);
                    break;
                case 18: {
                    const char* s = ctx.rng().Chance(1, 3) ? nullptr : text.c_str();
                    original.assignTextOrEmpty(&a[i], nullptr, s);
                    candidate.assignTextOrEmpty(&b[i], nullptr, s);
                    break;
                }
                case 19: {
                    const std::uint32_t pos = ctx.rng().Below(a[i].len + 3);
                    char* pa = original.at(&a[i], nullptr, pos);
                    char* pb = candidate.at(&b[i], nullptr, pos);
                    // Same position in the respective buffer, or the same
                    // static empty string.
                    resultA = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pa) -
                                                         reinterpret_cast<std::uintptr_t>(a[i].ptr));
                    resultB = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pb) -
                                                         reinterpret_cast<std::uintptr_t>(b[i].ptr));
                    if (pa == NullString() || pb == NullString()) {
                        resultA = pa == NullString();
                        resultB = pb == NullString();
                    }
                    break;
                }
                case 20:
                    original.freeze(&a[i], nullptr);
                    candidate.freeze(&b[i], nullptr);
                    break;
                case 21: {
                    const int k = static_cast<int>(ctx.rng().Below(kSlots));
                    original.plusString(&a[i], &a[j], &a[k]);
                    candidate.plusString(&b[i], &b[j], &b[k]);
                    break;
                }
                case 22:
                    original.charsPlus(&a[i], text.c_str(), &a[j]);
                    candidate.charsPlus(&b[i], text.c_str(), &b[j]);
                    break;
                case 23:
                    original.plusChar(&a[i], &a[j], repeated);
                    candidate.plusChar(&b[i], &b[j], repeated);
                    break;
                case 24: {
                    const int k = static_cast<int>(ctx.rng().Below(kSlots));
                    original.concatString(&a[i], &a[j], &a[k]);
                    candidate.concatString(&b[i], &b[j], &b[k]);
                    break;
                }
                case 25:
                case 26: {
                    const char* left = ctx.rng().Chance(1, 3) ? nullptr : text.c_str();
                    const StringConcatCharsLeftFn fa = op == 25 ? original.concatCharsLeft : original.concatCharsLeft2;
                    const StringConcatCharsLeftFn fb = op == 25 ? candidate.concatCharsLeft : candidate.concatCharsLeft2;
                    fa(&a[i], left, &a[j]);
                    fb(&b[i], left, &b[j]);
                    if (left == nullptr) {
                        // Without text the allocator byte is the low byte of
                        // `right`'s address, which differs between the sides.
                        resultA = a[i].allocator == static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(&a[j]));
                        resultB = b[i].allocator == static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(&b[j]));
                        a[i].allocator = b[i].allocator = 0;
                    }
                    break;
                }
                default:
                    original.concatChar(&a[i], &a[j], repeated);
                    candidate.concatChar(&b[i], &b[j], repeated);
                    break;
            }
            const std::uint32_t mask = op == 19 || op == 31 ? 0xFFFFFFFFu : 0xFFu;
            if (!S3RE_EXPECT_EQ(resultA & mask, resultB & mask) || !SameStrings(ctx, a, b)) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d step %d: op %d i=%d j=%d n=0x%x",
                         sequence, step, op, i, j, n);
                return;
            }
        }
        // Release everything. Constructors overwrite slots without releasing
        // what was there, so some buffers leak; both sides leak the same.
        for (int i = 0; i < kSlots; ++i) {
            original.tidyFree(&a[i], nullptr);
            candidate.tidyFree(&b[i], nullptr);
        }
    }
}

}  // namespace

namespace {

void CompareCompareChars(test::Context& ctx, StringCompareCharsFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        char text[40], other[44];
        const std::uint32_t len = ctx.rng().Below(sizeof(text) - 1);
        const std::uint32_t otherLen = ctx.rng().Below(40);
        for (char& c : text) {
            c = static_cast<char>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 'a' + ctx.rng().Below(3));
        }
        for (char& c : other) {
            c = static_cast<char>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 'a' + ctx.rng().Below(3));
        }
        if (ctx.rng().Chance(1, 2)) {
            std::memcpy(other, text, sizeof(text));  // equal prefixes
        }
        String str{};
        str.allocator = static_cast<std::uint8_t>(ctx.rng().Next32());
        str.ptr = len == 0 && ctx.rng().Chance(1, 2) ? nullptr : text;
        str.len = str.ptr == nullptr ? 0 : len;
        str.res = str.len;
        const std::uint32_t pos = ctx.rng().Below(str.len + 1);
        const std::uint32_t n0 = ctx.rng().Chance(1, 4) ? 0xffffffffu : ctx.rng().Below(40);
        const std::uint32_t pos2 = ctx.rng().Below(4);
        const std::int32_t ra = Original_String_CompareChars(&str, nullptr, pos, n0, other + pos2, otherLen - (otherLen > pos2 ? pos2 : 0));
        const std::int32_t rb = candidate(&str, nullptr, pos, n0, other + pos2, otherLen - (otherLen > pos2 ? pos2 : 0));
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: len %u pos %u n0 %u", i, str.len, pos, n0);
            return;
        }
    }
}

// Wrong: compares bytes as signed chars.
std::int32_t __fastcall WrongCompareChars(const String* self, void* edx, std::uint32_t pos, std::uint32_t n0,
                                          const char* s, std::uint32_t count) {
    const std::int32_t result = String_CompareChars(self, edx, pos, n0, s, count);
    std::uint32_t n = self->len - pos;
    n = n0 < n ? n0 : n;
    for (std::uint32_t i = 0; i < (n < count ? n : count); ++i) {
        if (self->ptr[pos + i] != s[i]) {
            return self->ptr[pos + i] < s[i] ? -1 : 1;
        }
    }
    return result;
}

}  // namespace

namespace {

void CompareCompareText(test::Context& ctx, StringCompareTextFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        char text[40], other[40];
        const std::uint32_t len = ctx.rng().Below(sizeof(text) - 1);
        const std::uint32_t otherLen = ctx.rng().Below(sizeof(other) - 1);
        for (char& c : text) {
            c = static_cast<char>(ctx.rng().Chance(1, 8) ? 1 + ctx.rng().Below(255) : 'a' + ctx.rng().Below(3));
        }
        for (char& c : other) {
            c = static_cast<char>(ctx.rng().Chance(1, 8) ? 1 + ctx.rng().Below(255) : 'a' + ctx.rng().Below(3));
        }
        if (ctx.rng().Chance(1, 2)) {
            std::memcpy(other, text, sizeof(text));  // equal prefixes
        }
        other[otherLen] = 0;
        String str{};
        str.ptr = len == 0 && ctx.rng().Chance(1, 2) ? nullptr : text;
        str.len = str.ptr == nullptr ? 0 : len;
        str.res = str.len;
        const std::int32_t ra = Original_String_CompareText(&str, nullptr, other);
        const std::int32_t rb = candidate(&str, nullptr, other);
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: len %u other %u", i, str.len, otherLen);
            return;
        }
    }
}

// Wrong: a longer string with an equal prefix compares as smaller.
std::int32_t __fastcall WrongCompareText(const String* self, void* edx, const char* s) {
    const std::int32_t r = String_CompareText(self, edx, s);
    return r == 1 && self->len > Length(s) ? -1 : r;
}

}  // namespace

S3RE_TEST(String_CompareText_MatchesOriginal) {
    CompareCompareText(ctx, &String_CompareText, 50000);
}

S3RE_TEST(String_CompareText_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCompareText(probe, &WrongCompareText, 50000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(String_CompareChars_MatchesOriginal) {
    CompareCompareChars(ctx, &String_CompareChars, 50000);
}

S3RE_TEST(String_CompareChars_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCompareChars(probe, &WrongCompareChars, 50000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(String_Eos_MatchesOriginal) {
    CompareEos(ctx, &String_Eos, 100000);
}

// Guards against a comparison that can't fail: the same check must catch a
// subtly wrong implementation.
S3RE_TEST(String_Eos_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEos(probe, &WrongEos, 100000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(String_Operations_MatchOriginal) {
    CompareStringOperations(ctx, OurOps(), 3000);
    S3RE_EXPECT_EQ(Original_String_MaxSize(nullptr, nullptr), String_MaxSize(nullptr, nullptr));
}

S3RE_TEST(String_Operations_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    StringOps wrong = OurOps();
    wrong.assignSubstr = &WrongAssignSubstr;
    CompareStringOperations(probe, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::msvc6
