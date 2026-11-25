// Watcom value vectors of plain elements: resize and insertAt (layout in
// vector_base.h). The elements are copied as raw bytes; the library's empty
// error hook 0x59f200 calls are left out.
//
// resize(n) ret 4 (WCBareVectorBase<T>::resize): keep = min(filled, n)
//   (signed); n != 0: a block of n * size bytes from operator new (null:
//   return 0, nothing changed) and the first `keep` elements copied into it
//   (each destination null-checked); n == 0: no block. The old block is
//   freed; data, filled = keep, capacity = n. The WCValOrderedVector
//   version also clamps length to n (unsigned). EAX = 1.
//     0x00499790 WCBareVectorBase<GameChunkDescription> (0x120 bytes)
//     0x00499fb0 WCValOrderedVector<GameChunkDescription> (clamps length)
// insertAt(index, const T* value) ret 8 (WCValOrderedVector<T>): an index
//   past the length becomes the length, a negative one 0. A full vector
//   (length == capacity) grows by its step (+0x18; 0: return 0) through the
//   resize above (with the length clamp; operator new failing: return 0).
//   Then at the end: the value copied to element `index` (the address
//   null-checked); else the last element is copied one up (the destination
//   null-checked), elements length-2 down to index one up each, and the
//   value copied to `index`. length and filled grow by one. EAX = 1.
//     0x00428d10 4-byte elements (pointers, ids; shared by 31 callers)
//     0x00499980 GameChunkDescription (0x120 bytes)

#include "watcom/vector_base.h"

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace {

constexpr std::uint32_t kChunkDescriptionSize = 0x120;

void CopyElement(std::uint32_t to, std::uint32_t from, std::uint32_t size) {
    std::memmove(reinterpret_cast<void*>(to), reinterpret_cast<const void*>(from), size);
}

// The resize part (also the growth step of insertAt). False when operator
// new returned null.
bool Reallocate(VectorBase* self, std::uint32_t n, std::uint32_t size, bool clampLength) {
    const std::int32_t keep = self->filled < static_cast<std::int32_t>(n) ? self->filled : static_cast<std::int32_t>(n);
    std::uint32_t block = 0;
    if (n != 0) {
        block = reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(n * size));
        if (block == 0) {
            return false;
        }
        for (std::int32_t i = 0; i < keep; ++i) {
            const std::uint32_t to = block + static_cast<std::uint32_t>(i) * size;
            if (to != 0) {
                CopyElement(to, self->data + static_cast<std::uint32_t>(i) * size, size);
            }
        }
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(self->data));
    self->data = block;
    self->filled = keep;
    self->capacity = n;
    if (clampLength && self->length > n) {
        self->length = n;
    }
    return true;
}

std::uint32_t InsertAt(VectorBase* self, std::int32_t index, std::uint32_t value, std::uint32_t size) {
    if (index > static_cast<std::int32_t>(self->length)) {
        index = static_cast<std::int32_t>(self->length);
    } else if (index < 0) {
        index = 0;
    }
    if (self->length == self->capacity) {
        if (self->grow == 0) {
            return 0;
        }
        if (!Reallocate(self, self->capacity + self->grow, size, true)) {
            return 0;
        }
    }
    const auto at = static_cast<std::uint32_t>(index);
    if (at == self->length) {
        const std::uint32_t to = self->data + at * size;
        if (to != 0) {
            CopyElement(to, value, size);
        }
    } else {
        const std::uint32_t last = self->data + self->length * size;
        if (last != 0) {
            CopyElement(last, last - size, size);
        }
        for (std::int32_t k = static_cast<std::int32_t>(self->length) - 2; k >= index; --k) {
            const std::uint32_t from = self->data + static_cast<std::uint32_t>(k) * size;
            CopyElement(from + size, from, size);
        }
        CopyElement(self->data + at * size, value, size);
    }
    ++self->length;
    ++self->filled;
    return 1;
}

}  // namespace

using ResizeFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::uint32_t n);
using InsertAtFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::int32_t index, std::uint32_t value);

// Purpose: Resizes the map file's vector of chunk descriptions (its bare
// part / the ordered vector, which also shortens its length).
std::uint32_t __fastcall ChunkDescriptionBareResize(VectorBase* self, void*, std::uint32_t n) {
    return Reallocate(self, n, kChunkDescriptionSize, false) ? 1 : 0;
}
std::uint32_t __fastcall ChunkDescriptionResize(VectorBase* self, void*, std::uint32_t n) {
    return Reallocate(self, n, kChunkDescriptionSize, true) ? 1 : 0;
}

// Purpose: Inserts a value into a Watcom vector at a position (the UI's
// child and font lists, id lists of the menus and the network; the map
// file's chunk descriptions), growing it by its step when full.
std::uint32_t __fastcall WordVectorInsertAt(VectorBase* self, void*, std::int32_t index, std::uint32_t value) {
    return InsertAt(self, index, value, 4);
}
std::uint32_t __fastcall ChunkDescriptionInsertAt(VectorBase* self, void*, std::int32_t index, std::uint32_t value) {
    return InsertAt(self, index, value, kChunkDescriptionSize);
}

S3RE_HOOK(0x00499790, ChunkDescriptionBareResize, ResizeFn, Original_ChunkDescriptionBareResize,
          ChunkDescriptionBareResize);
S3RE_HOOK(0x00499fb0, ChunkDescriptionResize, ResizeFn, Original_ChunkDescriptionResize, ChunkDescriptionResize);
S3RE_HOOK(0x00428d10, WordVectorInsertAt, InsertAtFn, Original_WordVectorInsertAt, WordVectorInsertAt);
S3RE_HOOK(0x00499980, ChunkDescriptionInsertAt, InsertAtFn, Original_ChunkDescriptionInsertAt,
          ChunkDescriptionInsertAt);

// ---------------------------------------------------------------------------
// Tests: the same random vector twice (own blocks from the game's heap with
// the same contents; capacity 0..6, filled -1..capacity, length 0..capacity,
// grow 0..3, sometimes no block), resized or inserted into by the original
// and by ours; compared: EAX, the fields (data only as null or not) and the
// first max(filled, length) elements of the blocks.
// ---------------------------------------------------------------------------

namespace {

struct Setup {
    VectorBase vector;
    std::vector<std::uint8_t> contents;
    bool block;
};

Setup MakeSetup(test::Rng& rng, std::uint32_t size) {
    Setup s{};
    rng.Fill(&s.vector, sizeof(s.vector));
    s.vector.capacity = rng.Below(7);
    s.vector.filled = rng.Range(-1, static_cast<std::int32_t>(s.vector.capacity));
    s.vector.length = rng.Below(s.vector.capacity + 1);
    s.vector.grow = rng.Chance(1, 5) ? 0 : rng.Below(4);
    s.block = s.vector.capacity != 0 || rng.Chance(1, 2);
    s.contents.resize((s.vector.capacity + 1) * size);
    rng.Fill(s.contents.data(), s.contents.size());
    return s;
}

VectorBase Instantiate(const Setup& s) {
    VectorBase v = s.vector;
    v.data = 0;
    if (s.block) {
        v.data = reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(static_cast<std::uint32_t>(s.contents.size())));
        std::memcpy(reinterpret_cast<void*>(v.data), s.contents.data(), s.contents.size());
    }
    return v;
}

// Compares the vectors and the elements marked in `defined` (a new block's
// elements past the copied ones are heap leftovers, different on each side).
bool SameVectors(test::Context& ctx, VectorBase& a, VectorBase& b, std::uint32_t size,
                 const std::vector<bool>& defined) {
    bool same = S3RE_EXPECT_EQ(a.data == 0, b.data == 0);
    for (std::uint32_t k = 0; same && a.data != 0 && k < defined.size() && k < a.capacity; ++k) {
        if (defined[k]) {
            same = S3RE_EXPECT_MEM_EQ(reinterpret_cast<void*>(a.data + k * size),
                                      reinterpret_cast<void*>(b.data + k * size), size);
        }
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(a.data));
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(b.data));
    a.data = b.data = 0;
    return S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a)) && same;
}

bool CompareResize(test::Context& ctx, ResizeFn original, ResizeFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const Setup s = MakeSetup(ctx.rng(), kChunkDescriptionSize);
        const std::uint32_t n = ctx.rng().Below(8);
        VectorBase a = Instantiate(s), b = Instantiate(s);
        const std::uint32_t ra = original(&a, nullptr, n);
        const std::uint32_t rb = candidate(&b, nullptr, n);
        // The copied elements (all of the old block when nothing changed).
        std::vector<bool> defined(8);
        for (std::uint32_t k = 0; k < 8; ++k) {
            defined[k] = ra == 0 ? k <= s.vector.capacity : static_cast<std::int32_t>(k) < a.filled;
        }
        if (!S3RE_EXPECT_EQ(ra, rb) || !SameVectors(ctx, a, b, kChunkDescriptionSize, defined)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: n %u", i, n);
            return false;
        }
    }
    return true;
}

bool CompareInsert(test::Context& ctx, InsertAtFn original, InsertAtFn candidate, std::uint32_t size, int cases) {
    for (int i = 0; i < cases; ++i) {
        const Setup s = MakeSetup(ctx.rng(), size);
        const std::int32_t index = ctx.rng().Range(-2, 8);
        std::vector<std::uint8_t> value(size);
        ctx.rng().Fill(value.data(), value.size());
        VectorBase a = Instantiate(s), b = Instantiate(s);
        const auto pointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value.data()));
        const std::uint32_t ra = original(&a, nullptr, index, pointer);
        const std::uint32_t rb = candidate(&b, nullptr, index, pointer);
        // Defined elements: the old block's (or after a reallocation the
        // copied ones), then moved one up from the insertion point on.
        std::vector<bool> defined(s.vector.capacity + 8);
        const bool reallocated = a.capacity != s.vector.capacity;
        for (std::size_t k = 0; k < defined.size(); ++k) {
            defined[k] = reallocated ? static_cast<std::int32_t>(k) < s.vector.filled : k <= s.vector.capacity;
        }
        if (ra != 0) {
            std::int32_t at = index > static_cast<std::int32_t>(s.vector.length)
                                  ? static_cast<std::int32_t>(s.vector.length) : index < 0 ? 0 : index;
            for (std::int32_t k = static_cast<std::int32_t>(s.vector.length); k > at; --k) {
                defined[k] = defined[k - 1];
            }
            defined[at] = true;
        }
        if (!S3RE_EXPECT_EQ(ra, rb) || !SameVectors(ctx, a, b, size, defined)) {
            ctx.Fail(__FILE__, __LINE__, "  size 0x%x case %d: index %d", size, i, index);
            return false;
        }
    }
    return true;
}

// Wrong: a negative index is used as the length.
std::uint32_t __fastcall WrongInsertAt(VectorBase* self, void*, std::int32_t index, std::uint32_t value) {
    return InsertAt(self, index < 0 ? static_cast<std::int32_t>(self->length) : index, value, 4);
}
// Wrong: the bare resize clamps the length too.
std::uint32_t __fastcall WrongBareResize(VectorBase* self, void* edx, std::uint32_t n) {
    return ChunkDescriptionResize(self, edx, n);
}

}  // namespace

S3RE_TEST(WatcomVectorInsertResize_MatchOriginal) {
    CompareResize(ctx, Original_ChunkDescriptionBareResize, &ChunkDescriptionBareResize, 1000) &&
        CompareResize(ctx, Original_ChunkDescriptionResize, &ChunkDescriptionResize, 1000) &&
        CompareInsert(ctx, Original_WordVectorInsertAt, &WordVectorInsertAt, 4, 2000) &&
        CompareInsert(ctx, Original_ChunkDescriptionInsertAt, &ChunkDescriptionInsertAt, kChunkDescriptionSize, 1000);
}

S3RE_TEST(WatcomVectorInsertResize_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareInsert(probe, Original_WordVectorInsertAt, &WrongInsertAt, 4, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareResize(probe, Original_ChunkDescriptionBareResize, &WrongBareResize, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
