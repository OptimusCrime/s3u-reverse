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

#include "hooks.h"
#include "test/difftest.h"

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
    return *reinterpret_cast<const volatile std::uint32_t*>(0x00655390);
}

char* NullString() {
    return reinterpret_cast<char*>(0x006553a0);
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
};

StringOps OriginalOps() {
    return {Original_String_Tidy,        Original_String_TidyFree,  Original_String_Copy,
            Original_String_Grow,        Original_String_Split,     Original_String_AssignChars,
            Original_String_AssignSubstr, Original_String_Destroy,  Original_String_FromChars,
            Original_String_Default,     Original_String_PlusChars, Original_String_ConcatChars};
}

StringOps OurOps() {
    return {&String_Tidy,        &String_TidyFree,  &String_Copy,        &String_Grow,
            &String_Split,       &String_AssignChars, &String_AssignSubstr, &String_Destroy,
            &String_FromChars,   &String_Default,   &String_PlusChars,   &String_ConcatChars};
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
            const int op = static_cast<int>(ctx.rng().Below(12));
            const int i = static_cast<int>(ctx.rng().Below(kSlots));
            const int j = static_cast<int>(ctx.rng().Below(kSlots));
            const std::string& text = texts[ctx.rng().Below(static_cast<std::uint32_t>(texts.size()))];
            const std::uint32_t n = ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Below(100);
            const std::uint32_t flag = ctx.rng().Below(2) | (ctx.rng().Next32() & 0xFF00u);
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
                case 10: {
                    const char* right = ctx.rng().Chance(1, 3) ? nullptr : text.c_str();
                    original.concatChars(&a[i], &a[j], right);
                    candidate.concatChars(&b[i], &b[j], right);
                    break;
                }
                default:
                    original.tidyFree(&a[i], nullptr);
                    candidate.tidyFree(&b[i], nullptr);
                    break;
            }
            if (!S3RE_EXPECT_EQ(resultA & 0xFF, resultB & 0xFF) || !SameStrings(ctx, a, b)) {
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
