// Watcom pointer vector: resize and the operator[] index check (layout and
// behaviour in ptr_vector.h).

#include "watcom/ptr_vector.h"

#include "hooks.h"
#include "test/difftest.h"

#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace {

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

void GameDelete(void* block) {
    reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(block);
}

// The common part of both resize instantiations.
bool Reallocate(PtrVector* self, std::uint32_t n) {
    const std::int32_t keep = self->filled < static_cast<std::int32_t>(n) ? self->filled : static_cast<std::int32_t>(n);
    std::uint32_t* block = nullptr;
    if (n != 0) {
        block = static_cast<std::uint32_t*>(GameNew(n * 4));
        if (block == nullptr) {
            return false;  // (the original calls the empty error hook 0x59f200 here)
        }
        for (std::int32_t i = 0; i < keep; ++i) {
            // The original checks each destination for null (placement new).
            auto* to = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(block) + 4u * i);
            if (to != nullptr) {
                *to = self->data[i];
            }
        }
    }
    GameDelete(self->data);
    self->filled = keep;
    self->data = block;
    self->capacity = n;
    return true;
}

void LengthChanged(PtrVector* self) {
    using Fn = std::uint32_t(__fastcall*)(PtrVector*, void*, std::uint32_t);
    const auto* vtable = reinterpret_cast<const Fn*>(self->vtable);
    vtable[1](self, nullptr, 0);
}

}  // namespace

S3RE_HOOK(0x00429a00, PtrVector_Resize, ResizeFn, Original_PtrVector_Resize, PtrVector_Resize);

// Purpose: Changes the number of entries a pointer vector has room for
// (keeping the ones that fit); the screen groups use it to grow their child
// lists.
std::uint32_t __fastcall PtrVector_Resize(PtrVector* self, void* /*edx*/, std::uint32_t n) {
    return Reallocate(self, n) ? 1 : 0;
}

S3RE_HOOK(0x00437fe0, PtrVector_ResizeClampLength, ResizeFn, Original_PtrVector_ResizeClampLength,
          PtrVector_ResizeClampLength);

// Purpose: The same resize for another vector type, which also shortens the
// used length to the new size.
std::uint32_t __fastcall PtrVector_ResizeClampLength(PtrVector* self, void* /*edx*/, std::uint32_t n) {
    if (!Reallocate(self, n)) {
        return 0;
    }
    if (self->length > n) {
        self->length = n;
    }
    return 1;
}

S3RE_HOOK(0x00428ee0, PtrVector_Index, IndexFn, Original_PtrVector_Index, PtrVector_Index);

// Purpose: The bounds check of a vector access: an index outside the
// vector is replaced by the nearest valid one, and an empty vector first gets
// one entry (the library's error handlers do nothing in this build).
std::uint32_t __fastcall PtrVector_Index(PtrVector* self, void* /*edx*/, std::int32_t index) {
    const std::uint32_t length = self->length;
    const std::int32_t last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return static_cast<std::uint32_t>(index);
    }
    if (length != 0) {
        return index < 0 ? 0 : static_cast<std::uint32_t>(last);
    }
    if (self->capacity == 0) {
        if (!Reallocate(self, 1)) {
            return 0;
        }
        if (self->length > 1) {
            self->length = 1;
        }
    }
    LengthChanged(self);
    self->length = 1;
    return 0;
}

std::uint32_t PtrVector_At(PtrVector* self, std::int32_t index) {
    const std::uint32_t i = PtrVector_Index(self, nullptr, index);
    return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(self->data) + 4u * i);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each case builds the same vector twice, each with its own data block from
// the game's operator new (the functions free it), runs the original on one
// and ours on the other, and compares the return values, the fields, the
// defined entries of the data blocks and the vtable calls. The vtable is a
// recording stub.

namespace {

struct Recorder {
    std::vector<std::uint32_t> calls;  // (this offset in the case buffer, argument) per call
    std::uint8_t* base = nullptr;
};
Recorder* g_recorder = nullptr;

std::uint32_t __fastcall RecordLengthChanged(PtrVector* self, void*, std::uint32_t argument) {
    g_recorder->calls.push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uint8_t*>(self) - g_recorder->base));
    g_recorder->calls.push_back(argument);
    return 0x5eed;
}

std::uint32_t __fastcall UndeclaredSlot(PtrVector*, void*) {
    g_recorder->calls.push_back(0xbadu);
    return 0;
}

const void* const kFakeVtable[2] = {reinterpret_cast<const void*>(&UndeclaredSlot),
                                    reinterpret_cast<const void*>(&RecordLengthChanged)};

struct Setup {
    PtrVector vector;
    std::uint32_t entries[8];  // initial contents of the data block
    std::uint32_t blockEntries;
    bool nullData;
    std::int32_t argument;
};

std::int32_t SmallOrAnything(test::Context& ctx, std::int32_t low, std::int32_t high) {
    return ctx.rng().Chance(4, 5) ? ctx.rng().Range(low, high) : static_cast<std::int32_t>(ctx.rng().Next32());
}

Setup MakeSetup(test::Context& ctx, bool forIndex) {
    Setup s{};
    s.vector.vtable = reinterpret_cast<std::uint32_t>(kFakeVtable);
    s.vector.unknown04 = ctx.rng().Next32();
    s.blockEntries = 1 + ctx.rng().Below(6);
    s.vector.capacity = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(8);
    // Never more filled entries than the block holds (the copy reads them).
    s.vector.filled = ctx.rng().Range(-2, static_cast<std::int32_t>(s.blockEntries));
    s.vector.length = ctx.rng().Chance(1, 3) ? 0 : static_cast<std::uint32_t>(SmallOrAnything(ctx, 0, 6));
    s.nullData = ctx.rng().Chance(1, 8);
    for (std::uint32_t& e : s.entries) {
        e = ctx.rng().Next32();
    }
    s.argument = forIndex ? SmallOrAnything(ctx, -3, 8) : ctx.rng().Range(0, 6);
    if (s.nullData && s.vector.filled > 0) {
        s.vector.filled = 0;
    }
    return s;
}

struct Run {
    std::uint32_t result;
    PtrVector vector;
    std::uint32_t* initialBlock;
    std::vector<std::uint32_t> calls;
};

template <typename Call>
Run RunOnce(const Setup& s, Call call) {
    Run r{};
    r.vector = s.vector;
    r.initialBlock = nullptr;
    if (!s.nullData) {
        r.initialBlock = static_cast<std::uint32_t*>(GameNew(s.blockEntries * 4));
        std::memcpy(r.initialBlock, s.entries, s.blockEntries * 4);
    }
    r.vector.data = r.initialBlock;
    Recorder recorder;
    recorder.base = reinterpret_cast<std::uint8_t*>(&r.vector);
    g_recorder = &recorder;
    r.result = call(&r.vector, s.argument);
    g_recorder = nullptr;
    r.calls = recorder.calls;
    return r;
}

// Compares, then frees whatever block each vector holds now.
bool CompareRuns(test::Context& ctx, Run& a, Run& b) {
    bool same = S3RE_EXPECT_EQ(a.result, b.result);
    same &= S3RE_EXPECT(a.calls == b.calls);
    same &= S3RE_EXPECT_EQ(a.vector.vtable, b.vector.vtable);
    same &= S3RE_EXPECT_EQ(a.vector.unknown04, b.vector.unknown04);
    same &= S3RE_EXPECT_EQ(a.vector.capacity, b.vector.capacity);
    same &= S3RE_EXPECT_EQ(a.vector.filled, b.vector.filled);
    same &= S3RE_EXPECT_EQ(a.vector.length, b.vector.length);
    const bool aMoved = a.vector.data != a.initialBlock;
    const bool bMoved = b.vector.data != b.initialBlock;
    same &= S3RE_EXPECT_EQ(aMoved, bMoved);
    if (aMoved && bMoved) {
        same &= S3RE_EXPECT_EQ(a.vector.data == nullptr, b.vector.data == nullptr);
        if (a.vector.data != nullptr && b.vector.data != nullptr && a.vector.filled > 0) {
            same &= S3RE_EXPECT_MEM_EQ(a.vector.data, b.vector.data, 4u * static_cast<std::uint32_t>(a.vector.filled));
        }
    }
    GameDelete(a.vector.data);
    GameDelete(b.vector.data);
    return same;
}

using Candidates = std::uint32_t (*)(int which, PtrVector* v, std::int32_t argument);

std::uint32_t CallOriginal(int which, PtrVector* v, std::int32_t argument) {
    switch (which) {
        case 0: return Original_PtrVector_Resize(v, nullptr, static_cast<std::uint32_t>(argument));
        case 1: return Original_PtrVector_ResizeClampLength(v, nullptr, static_cast<std::uint32_t>(argument));
        default: return Original_PtrVector_Index(v, nullptr, argument);
    }
}

std::uint32_t CallOurs(int which, PtrVector* v, std::int32_t argument) {
    switch (which) {
        case 0: return PtrVector_Resize(v, nullptr, static_cast<std::uint32_t>(argument));
        case 1: return PtrVector_ResizeClampLength(v, nullptr, static_cast<std::uint32_t>(argument));
        default: return PtrVector_Index(v, nullptr, argument);
    }
}

// Wrong: resize forgets to clamp the length.
std::uint32_t CallWrong(int which, PtrVector* v, std::int32_t argument) {
    if (which == 1) {
        return PtrVector_Resize(v, nullptr, static_cast<std::uint32_t>(argument));
    }
    return CallOurs(which, v, argument);
}

void ComparePtrVector(test::Context& ctx, Candidates candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const int which = static_cast<int>(ctx.rng().Below(3));
        const Setup s = MakeSetup(ctx, which == 2);
        Run a = RunOnce(s, [&](PtrVector* v, std::int32_t arg) { return CallOriginal(which, v, arg); });
        Run b = RunOnce(s, [&](PtrVector* v, std::int32_t arg) { return candidate(which, v, arg); });
        if (!CompareRuns(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, function %d, argument %d", i, which, s.argument);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(PtrVector_MatchesOriginal) {
    ComparePtrVector(ctx, &CallOurs, 30000);
}

S3RE_TEST(PtrVector_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePtrVector(probe, &CallWrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
