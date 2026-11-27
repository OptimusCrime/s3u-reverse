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
//   and (wip/watcom-r6) 0x005df310 (the same bytes) and 0x00414770 (11
//   callers; its ~string has the buffer split inlined too); 0x004583d0
//   destroys such a vector at +4 of its object.
//
// WCValOrderedVector<String>::insertAt (0x004148a0; 18 callers):
//   (index, const String* value) ret 8. The index is clamped to 0..length.
//   A full vector (length == capacity) grows by its step (+0x18; 0: EAX 0)
//   through StringVector_Resize 0x40a580 (0: EAX 0), then length is clamped
//   to the new capacity. At the end: the new element is copy-constructed
//   from value (allocator byte = the low byte of the clamped index, whose
//   stack slot it shares; _Tidy(false) 0x402170, assign(value, 0, npos)),
//   unless its address is null. Otherwise the last element is
//   copy-constructed into the slot after it (allocator byte = the low byte
//   of that slot's address, which the compiler kept in the same stack slot;
//   skipped if the address is null), elements length-2..index are assigned
//   one up (assign 0x401d60), and value is assigned to `index` (inlined
//   operator=). length and filled grow by one. EAX = 1.
// WCValOrderedVector<String> copy constructor (0x00409c40): (const& from)
//   ret 4: CopyVector (vector_base.h) with vtable 0x655440 and each string
//   copy-constructed: allocator byte = the low byte of its index (the loop
//   counter's stack slot), cleared, assign(from[i], 0, npos) 0x401d60.
// WCValOrderedVector<String>::removeAt (0x00429750): (index) ret 4: no
//   bounds check; elements index+1..length-1 are assigned one down (the
//   inlined operator=, length re-read each time), the last one destroyed
//   (~string 0x401b60), length and filled shrink by one. EAX = the new
//   filled.

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
std::uint32_t __fastcall StringVectorDestructor_5df310(VectorBase* self, void*) {
    return DestroyStringVector(self);
}
std::uint32_t __fastcall StringVectorDestructor_414770(VectorBase* self, void*) {
    return DestroyStringVector(self);
}
// (0x4583d0 destroys the vector at +4 of its object: an object's member
// destructor that is only this.)
std::uint32_t __fastcall StringVectorDestructor_4583d0(VectorBase* self, void*) {
    return DestroyStringVector(reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(self) + 4));
}
// (0x459550 is the same for the menus' dictionary pairs <key, vector of
// strings>: the pair's destructor, the value at +4.)
std::uint32_t __fastcall StringVectorDestructor_459550(VectorBase* self, void*) {
    return DestroyStringVector(reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(self) + 4));
}

using StringInsertAtFn = std::uint32_t(__fastcall*)(PtrVector* self, void* edx, std::int32_t index,
                                                    const String* value);
using StringRemoveAtFn = std::uint32_t(__fastcall*)(PtrVector* self, void* edx, std::int32_t index);

namespace {

constexpr std::uintptr_t kStringVectorResize = 0x0040a580;  // StringVector_Resize (ours)
constexpr std::uintptr_t kStringDestroy = 0x00401b60;       // ~string (ours)

#pragma pack(push, 1)
struct StringVectorGrow {  // the growth step after the PtrVector fields
    PtrVector vector;
    std::uint32_t grow;  // +0x18
};
#pragma pack(pop)

// The copy constructor as the vector code inlines it.
void CopyConstruct(String* to, std::uint8_t allocator, const String* from) {
    to->allocator = allocator;
    msvc6::String_Tidy(to, nullptr, 0);
    msvc6::String_AssignSubstr(to, nullptr, from, 0, 0xFFFFFFFFu);  // (a literal npos here)
}

}  // namespace

// Purpose: Inserts a copy of a string into a string vector at a position
// (the console's history, the menus' text lists), growing it when full.
std::uint32_t __fastcall StringVector_InsertAt(PtrVector* self, void* /*edx*/, std::int32_t index, const String* value) {
    if (index > static_cast<std::int32_t>(self->length)) {
        index = static_cast<std::int32_t>(self->length);
    } else if (index < 0) {
        index = 0;
    }
    if (self->length == self->capacity) {
        const std::uint32_t grow = reinterpret_cast<StringVectorGrow*>(self)->grow;
        if (grow == 0) {
            return 0;
        }
        const std::uint32_t capacity = self->capacity + grow;
        if (reinterpret_cast<ResizeFn>(kStringVectorResize)(self, nullptr, capacity) == 0) {
            return 0;
        }
        if (self->length > capacity) {
            self->length = capacity;
        }
    }
    const auto at = static_cast<std::uint32_t>(index);
    String* const entries = Entries(self);
    if (at == self->length) {
        String* const slot = reinterpret_cast<String*>(reinterpret_cast<std::uintptr_t>(entries) + 16u * at);
        if (slot != nullptr) {
            CopyConstruct(slot, static_cast<std::uint8_t>(index), value);  // (index's stack slot)
        }
    } else {
        String* const last = reinterpret_cast<String*>(reinterpret_cast<std::uintptr_t>(entries) + 16u * self->length);
        if (last != nullptr) {
            // (the allocator byte: the low byte of the slot's own address)
            last->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(last));
            msvc6::detail::Clear(last);
            msvc6::String_AssignSubstr(last, nullptr, last - 1, 0, 0xFFFFFFFFu);
        }
        for (std::int32_t k = static_cast<std::int32_t>(self->length) - 2; k >= index; --k) {
            msvc6::String_AssignSubstr(&Entries(self)[k + 1], nullptr, &Entries(self)[k], 0, msvc6::detail::Npos());
        }
        msvc6::String_AssignSubstr(&Entries(self)[at], nullptr, value, 0, msvc6::detail::Npos());
    }
    ++self->length;
    ++self->filled;
    return 1;
}

// Purpose: Removes the string at a position from a string vector (the
// later ones move down).
std::uint32_t __fastcall StringVector_RemoveAt(PtrVector* self, void* /*edx*/, std::int32_t index) {
    for (std::int32_t i = index; i < static_cast<std::int32_t>(self->length - 1); ++i) {
        msvc6::String_AssignSubstr(&Entries(self)[i], nullptr, &Entries(self)[i + 1], 0, msvc6::detail::Npos());
    }
    reinterpret_cast<void(__fastcall*)(String*, void*)>(kStringDestroy)(&Entries(self)[self->length - 1], nullptr);
    --self->length;
    return static_cast<std::uint32_t>(--self->filled);
}

using StringVectorCopyFn = VectorBase*(__fastcall*)(VectorBase* self, void* edx, const VectorBase* from);

// Purpose: Copy constructor of a vector of strings (the console's command
// list, the lobby's chat lines).
namespace {
VectorBase* CopyStrings(VectorBase* self, const VectorBase* from) {
    return CopyVector(self, from, game::vtables::kWCValOrderedVector_String, sizeof(String),
                      [](std::int32_t index, std::uint32_t to, std::uint32_t source) {
                          auto* string = reinterpret_cast<String*>(to);
                          string->allocator = static_cast<std::uint8_t>(index);  // (the loop counter's slot)
                          msvc6::detail::Clear(string);
                          msvc6::String_AssignSubstr(string, nullptr, reinterpret_cast<const String*>(source), 0,
                                                     0xFFFFFFFFu);
                      });
}
}  // namespace

VectorBase* __fastcall StringVector_CopyConstruct(VectorBase* self, void* /*edx*/, const VectorBase* from) {
    return CopyStrings(self, from);
}

#pragma pack(push, 1)
struct KeyedStringVector {  // WCSkipListDictKeyVal<unsigned int, WCValOrderedVector<String>>
    std::uint32_t key;
    VectorBase value;
};
#pragma pack(pop)
static_assert(sizeof(KeyedStringVector) == 0x20);

using KeyedCopyFn = KeyedStringVector*(__fastcall*)(KeyedStringVector* self, void* edx, const std::uint32_t* key,
                                                    const VectorBase* value);

// Purpose: Constructor of the menus' dictionary pairs of a key and a vector
// of strings (copies both; the vector as the copy constructor above, whose
// loop counter again gives each string's allocator byte).
KeyedStringVector* __fastcall KeyedStringVector_Construct(KeyedStringVector* self, void* /*edx*/,
                                                          const std::uint32_t* key, const VectorBase* value) {
    self->key = *key;
    CopyStrings(&self->value, value);
    return self;
}

S3RE_HOOK(0x004582b0, KeyedStringVector_Construct, KeyedCopyFn, Original_KeyedStringVector_Construct,
          KeyedStringVector_Construct);
S3RE_HOOK(0x00409c40, StringVector_CopyConstruct, StringVectorCopyFn, Original_StringVector_CopyConstruct,
          StringVector_CopyConstruct);
S3RE_HOOK(0x004148a0, StringVector_InsertAt, StringInsertAtFn, Original_StringVector_InsertAt,
          StringVector_InsertAt);
S3RE_HOOK(0x00429750, StringVector_RemoveAt, StringRemoveAtFn, Original_StringVector_RemoveAt,
          StringVector_RemoveAt);

S3RE_HOOK(0x00409d50, StringVectorDestructor_409d50, StringVectorDestructorFn,
          Original_StringVectorDestructor_409d50, StringVectorDestructor_409d50);
S3RE_HOOK(0x0044c980, StringVectorDestructor_44c980, StringVectorDestructorFn,
          Original_StringVectorDestructor_44c980, StringVectorDestructor_44c980);
S3RE_HOOK(0x0040a280, StringVectorDestructor_40a280, StringVectorDestructorFn,
          Original_StringVectorDestructor_40a280, StringVectorDestructor_40a280);
S3RE_HOOK(0x0044cf50, StringVectorDestructor_44cf50, StringVectorDestructorFn,
          Original_StringVectorDestructor_44cf50, StringVectorDestructor_44cf50);
S3RE_HOOK(0x005df310, StringVectorDestructor_5df310, StringVectorDestructorFn,
          Original_StringVectorDestructor_5df310, StringVectorDestructor_5df310);
S3RE_HOOK(0x00414770, StringVectorDestructor_414770, StringVectorDestructorFn,
          Original_StringVectorDestructor_414770, StringVectorDestructor_414770);
S3RE_HOOK(0x004583d0, StringVectorDestructor_4583d0, StringVectorDestructorFn,
          Original_StringVectorDestructor_4583d0, StringVectorDestructor_4583d0);
S3RE_HOOK(0x00459550, StringVectorDestructor_459550, StringVectorDestructorFn,
          Original_StringVectorDestructor_459550, StringVectorDestructor_459550);

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

// insertAt / removeAt on consistent vectors (filled = length = count,
// capacity count..count+2, grow 0..2), an outside value string (sometimes
// sharing an element's buffer, sometimes an element itself). Compared: EAX
// and the vectors (allocator bytes of the shift path's new slot aside: they
// are the slot's address).
void BuildWithRoom(PtrVector& vector, std::uint32_t& grow, std::uint32_t count,
                   const std::uint32_t* lengths, const std::uint8_t* counts, const int* shareWith, const char* text,
                   std::uint32_t extra, std::uint32_t growStep) {
    Build(vector, count, lengths, counts, shareWith, text, static_cast<std::int32_t>(count));
    if (extra != 0) {
        auto* block = static_cast<std::uint32_t*>(GameNew((count + extra) * 16));
        if (count != 0) {
            std::memcpy(block, vector.data, count * 16);
        }
        GameDelete(vector.data);
        vector.data = block;
        vector.capacity = count + extra;
    }
    grow = growStep;
}

void CompareInsertRemove(test::Context& ctx, StringInsertAtFn insert, StringRemoveAtFn remove, int cases) {
    char text[48];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(6) + (ctx.rng().Chance(1, 2) ? 1 : 0);
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
        const std::uint32_t extra = ctx.rng().Below(3);
        const std::uint32_t growStep = ctx.rng().Below(3);
        const bool removing = count != 0 && ctx.rng().Chance(1, 3);
        const std::int32_t index = removing ? static_cast<std::int32_t>(ctx.rng().Below(count))
                                            : ctx.rng().Range(-2, static_cast<std::int32_t>(count) + 2);
        const int valueKind = static_cast<int>(ctx.rng().Below(3));  // 0 own, 1 sharing, 2 an element
        const std::uint32_t valueOf = count == 0 ? 0 : ctx.rng().Below(count);
        StringVectorGrow a{}, b{};
        BuildWithRoom(a.vector, a.grow, count, lengths, counts, shareWith, text, extra, growStep);
        BuildWithRoom(b.vector, b.grow, count, lengths, counts, shareWith, text, extra, growStep);
        String valueA{}, valueB{};
        msvc6::String_AssignChars(&valueA, nullptr, text, 7);
        msvc6::String_AssignChars(&valueB, nullptr, text, 7);
        const String* va = &valueA;
        const String* vb = &valueB;
        if (count != 0 && valueKind == 1) {
            msvc6::String_AssignSubstr(&valueA, nullptr, &Entries(&a.vector)[valueOf], 0, 0xFFFFFFFFu);
            msvc6::String_AssignSubstr(&valueB, nullptr, &Entries(&b.vector)[valueOf], 0, 0xFFFFFFFFu);
        } else if (count != 0 && valueKind == 2 && extra != 0) {
            va = &Entries(&a.vector)[valueOf];  // (only without growth: a grown block moves the element)
            vb = &Entries(&b.vector)[valueOf];
        }
        std::uint32_t ra, rb;
        if (removing) {
            ra = Original_StringVector_RemoveAt(&a.vector, nullptr, index);
            rb = remove(&b.vector, nullptr, index);
        } else {
            ra = Original_StringVector_InsertAt(&a.vector, nullptr, index, va);
            rb = insert(&b.vector, nullptr, index, vb);
            // The shift path's new slot: allocator byte = its address.
            if (ra == 1 && rb == 1 && index < static_cast<std::int32_t>(a.vector.length) - 1) {
                Entries(&a.vector)[a.vector.length - 1].allocator = 0;
                Entries(&b.vector)[b.vector.length - 1].allocator = 0;
            }
        }
        const std::uint32_t growA = a.grow, growB = b.grow;
        bool same = S3RE_EXPECT_EQ(ra, rb) && S3RE_EXPECT_EQ(growA, growB) && SameVectors(ctx, a.vector, b.vector);
        same = S3RE_EXPECT_EQ(valueA.len, valueB.len) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %s, count %u, extra %u, grow %u, index %d, value %d", i,
                     removing ? "remove" : "insert", count, extra, growStep, index, valueKind);
            return;
        }
    }
}

std::uint32_t __fastcall WrongInsertAt(PtrVector* self, void* edx, std::int32_t index, const String* value) {
    return StringVector_InsertAt(self, edx, index < 0 ? static_cast<std::int32_t>(self->length) : index, value);
}

// The copy constructor on Build's vectors (sometimes capacity <= 0 or a
// failing size); compared: EAX, the new vector's fields (data only as null
// or not) and strings, and the source (its buffers' counts).
void CompareCopy(test::Context& ctx, StringVectorCopyFn candidate, int cases,
                 StringVectorCopyFn original = nullptr) {
    if (original == nullptr) {
        original = Original_StringVector_CopyConstruct;
    }
    char text[48];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(7);
        std::uint32_t lengths[8];
        std::uint8_t counts[8];
        int shareWith[8];
        static constexpr std::uint8_t kCounts[] = {0, 0, 0, 1, 2, 0xfd, 0xfe, 0xff};
        for (std::uint32_t k = 0; k < count; ++k) {
            lengths[k] = ctx.rng().Chance(1, 8) ? 0xFFFFFFFFu : ctx.rng().Below(40);
            counts[k] = kCounts[ctx.rng().Below(sizeof(kCounts))];
            shareWith[k] = k > 0 && ctx.rng().Chance(1, 4) ? static_cast<int>(ctx.rng().Below(k)) : -1;
        }
        for (char& c : text) {
            c = static_cast<char>(1 + ctx.rng().Below(255));
        }
        const auto filled = static_cast<std::int32_t>(count == 0 ? 0 : ctx.rng().Below(count + 1));
        StringVectorGrow sa{}, sb{};
        Build(sa.vector, count, lengths, counts, shareWith, text, filled);
        Build(sb.vector, count, lengths, counts, shareWith, text, filled);
        sa.vector.unknown04 = sb.vector.unknown04 = ctx.rng().Next32();
        sa.grow = sb.grow = ctx.rng().Next32();
        if (ctx.rng().Chance(1, 8)) {
            sa.vector.capacity = sb.vector.capacity = 0x80000000u + ctx.rng().Below(4);  // not positive
        }
        VectorBase da{}, db{};
        ctx.rng().Fill(&da, sizeof(da));
        std::memcpy(&db, &da, sizeof(db));
        VectorBase* ra = original(&da, nullptr, reinterpret_cast<VectorBase*>(&sa));
        VectorBase* rb = candidate(&db, nullptr, reinterpret_cast<VectorBase*>(&sb));
        bool same = S3RE_EXPECT(ra == &da && rb == &db) && SameVectors(ctx, sa.vector, sb.vector);
        if (same && da.data == 0) {  // (no strings to compare)
            same = S3RE_EXPECT(db.data == 0) && S3RE_EXPECT_MEM_EQ(&da, &db, sizeof(da));
        } else if (same) {
            same = SameVectors(ctx, *reinterpret_cast<PtrVector*>(&da), *reinterpret_cast<PtrVector*>(&db)) &&
                   S3RE_EXPECT_EQ(da.grow, db.grow) && S3RE_EXPECT_EQ(da.unk04, db.unk04);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: count %u, filled %d", i, count, filled);
            return;
        }
    }
}

// Wrong: the copies' allocator bytes 0.
// 0x4582b0 on the same vectors: the pair built around the destination
// (its key from a fixed word; a wrong key copy returns null).
constexpr std::uint32_t kPairKey = 0x6b657931;
template <KeyedCopyFn* Fn>
VectorBase* __fastcall KeyedCopyOn(VectorBase* to, void*, const VectorBase* from) {
    KeyedStringVector pair;
    pair.key = ~kPairKey;
    pair.value = *to;
    KeyedStringVector* result = (*Fn)(&pair, nullptr, &kPairKey, from);
    *to = pair.value;
    return result == &pair && pair.key == kPairKey ? to : nullptr;
}
KeyedCopyFn g_ourKeyedCopy = &KeyedStringVector_Construct;
// Wrong: the key not copied.
KeyedStringVector* __fastcall WrongKeyedCopy(KeyedStringVector* self, void*, const std::uint32_t*, const VectorBase* value) {
    StringVector_CopyConstruct(&self->value, nullptr, value);
    return self;
}
KeyedCopyFn g_wrongKeyedCopy = &WrongKeyedCopy;

VectorBase* __fastcall WrongCopy(VectorBase* self, void* edx, const VectorBase* from) {
    StringVector_CopyConstruct(self, edx, from);
    for (std::int32_t i = 0; self->data != 0 && i < self->filled; ++i) {
        Entries(reinterpret_cast<PtrVector*>(self))[i].allocator = 0;
    }
    return self;
}

// Wrong: skips the last element.
std::uint32_t __fastcall WrongDestructor(VectorBase* self, void* edx) {
    --self->filled;
    const std::uint32_t r = StringVectorDestructor_409d50(self, edx);
    ++self->filled;
    return r;
}

}  // namespace

S3RE_TEST(StringVector_InsertRemove_MatchOriginal) {
    CompareInsertRemove(ctx, &StringVector_InsertAt, &StringVector_RemoveAt, 3000);
}

S3RE_TEST(StringVector_InsertRemove_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInsertRemove(probe, &WrongInsertAt, &StringVector_RemoveAt, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(StringVector_CopyConstruct_MatchesOriginal) {
    CompareCopy(ctx, &StringVector_CopyConstruct, 2000);
}

S3RE_TEST(KeyedStringVector_Construct_MatchesOriginal) {
    CompareCopy(ctx, &KeyedCopyOn<&g_ourKeyedCopy>, 1000, &KeyedCopyOn<&Original_KeyedStringVector_Construct>);
}

S3RE_TEST(KeyedStringVector_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCopy(probe, &KeyedCopyOn<&g_wrongKeyedCopy>, 20, &KeyedCopyOn<&Original_KeyedStringVector_Construct>);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(StringVector_CopyConstruct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCopy(probe, &WrongCopy, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {
// 0x4583d0 gets the object whose +4 is the vector.
VectorBase* OwnerOf(VectorBase* vector) {
    return reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(vector) - 4);
}
std::uint32_t __fastcall Original4583d0OnVector(VectorBase* vector, void*) {
    return Original_StringVectorDestructor_4583d0(OwnerOf(vector), nullptr);
}
std::uint32_t __fastcall Ours4583d0OnVector(VectorBase* vector, void*) {
    return StringVectorDestructor_4583d0(OwnerOf(vector), nullptr);
}
std::uint32_t __fastcall Original459550OnVector(VectorBase* vector, void*) {
    return Original_StringVectorDestructor_459550(OwnerOf(vector), nullptr);
}
std::uint32_t __fastcall Ours459550OnVector(VectorBase* vector, void*) {
    return StringVectorDestructor_459550(OwnerOf(vector), nullptr);
}
}  // namespace

S3RE_TEST(StringVectorDestructors_MatchOriginal) {
    CompareDestructor(ctx, Original_StringVectorDestructor_409d50, &StringVectorDestructor_409d50, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_44c980, &StringVectorDestructor_44c980, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_40a280, &StringVectorDestructor_40a280, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_44cf50, &StringVectorDestructor_44cf50, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_5df310, &StringVectorDestructor_5df310, 1000);
    CompareDestructor(ctx, Original_StringVectorDestructor_414770, &StringVectorDestructor_414770, 1000);
    CompareDestructor(ctx, &Original4583d0OnVector, &Ours4583d0OnVector, 1000);
    CompareDestructor(ctx, &Original459550OnVector, &Ours459550OnVector, 1000);
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
