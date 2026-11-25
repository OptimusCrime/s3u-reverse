// The draw-data buffer (DrawData, 0xc7720 bytes; Volker\LogicDraw.cpp) that
// CLogicThread::PrepareDrawData (0x51f110) allocates for its +0x3d0 and fills
// for the draw thread.
//
// DrawData::DrawData (0x00523480): () thiscall, EAX = this. Constructs the
//   members that need it, in this order: 16 std::strings at +0xc65c0 and 20
//   message lines (0x1c bytes, a std::string first) at +0xc66d8, both
//   through the CRT's `eh vector constructor iterator` (0x61ff7f) with the
//   element constructor 0x523540; the std::string at +0xc66c0 inlined; 100
//   effect draw records (LogicRecord24::LogicRecord24 0x5437b0) at +0xc690c;
//   then +0x2ec = 0. Everything else keeps the heap block's bytes.
//   Known difference: the allocator byte of the string at +0xc66c0 comes
//   from an uninitialized local of the original's frame (the string lives on
//   in the object); ours writes 0 (docs/reimplementation_notes.md, case b).
//   The vector iterator only adds exception unwinding (the element
//   constructor can't throw), so ours calls the element constructor in a
//   loop.
// std::string::string_empty (0x00523540): () thiscall, EAX = this: an empty
//   string whose allocator byte is the high byte of `this` (the compiler's
//   `push ecx` spill slot doubles as the empty allocator: [esp+3] is ECX's
//   top byte), ptr/len/res = 0. Element constructor of DrawData's string
//   arrays, also used by the lobby, the CD prompt, ProcessEvents and
//   CGraphicsData::Open.

#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uint32_t kStatusTexts = 16;
constexpr std::uint32_t kMessageLines = 20;
constexpr std::uint32_t kEffectRecords = 100;
constexpr std::uint32_t kEffectRecordBytes = 0x24;

#pragma pack(push, 1)
struct DrawMessageLine {             // 0x1c bytes
    msvc6::String text;              // +0x00
    std::uint8_t unk010[0x1c - 0x10];
};

struct DrawData {
    std::uint8_t unk000[0x2ec];
    std::uint32_t unk2ec;                             // +0x2ec cleared by the constructor
    std::uint8_t unk2f0[0xc65c0 - 0x2f0];
    msvc6::String texts[kStatusTexts];                // +0xc65c0
    msvc6::String text_c66c0;                         // +0xc66c0
    std::uint8_t unk_c66d0[0xc66d8 - 0xc66d0];
    DrawMessageLine lines[kMessageLines];             // +0xc66d8
    std::uint8_t unk_c6908[0xc690c - 0xc6908];
    std::uint8_t effect_records[kEffectRecords][kEffectRecordBytes];  // +0xc690c LogicRecord24
    std::uint8_t unk_c771c[0xc7720 - 0xc771c];
};
#pragma pack(pop)
static_assert(sizeof(DrawMessageLine) == 0x1c);
static_assert(offsetof(DrawData, unk2ec) == 0x2ec);
static_assert(offsetof(DrawData, texts) == 0xc65c0);
static_assert(offsetof(DrawData, text_c66c0) == 0xc66c0);
static_assert(offsetof(DrawData, lines) == 0xc66d8);
static_assert(offsetof(DrawData, effect_records) == 0xc690c);
static_assert(sizeof(DrawData) == 0xc7720);

using StringEmptyFn = msvc6::String*(__fastcall*)(msvc6::String* self, void* edx);
using RecordConstructFn = void*(__fastcall*)(void* self, void* edx);
constexpr std::uintptr_t kStringEmpty = 0x00523540;     // std::string::string_empty (ours)
constexpr std::uintptr_t kRecordConstruct = 0x005437b0;  // LogicRecord24::LogicRecord24 (verified)

void ConstructEmpty(msvc6::String* s) {
    reinterpret_cast<StringEmptyFn>(kStringEmpty)(s, nullptr);
}

}  // namespace

using DrawDataConstructFn = DrawData*(__fastcall*)(DrawData* self, void* edx);
DrawData* __fastcall DrawData_Construct(DrawData* self, void* edx);
msvc6::String* __fastcall String_EmptyHighByteAllocator(msvc6::String* self, void* edx);

S3RE_HOOK(0x00523480, DrawData_Construct, DrawDataConstructFn, Original_DrawData_Construct, DrawData_Construct);
S3RE_HOOK(0x00523540, String_EmptyHighByteAllocator, StringEmptyFn, Original_String_EmptyHighByteAllocator,
          String_EmptyHighByteAllocator);

// Purpose: Makes an empty std::string (the element constructor of string
// arrays in several classes).
msvc6::String* __fastcall String_EmptyHighByteAllocator(msvc6::String* self, void* /*edx*/) {
    self->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(self) >> 24);
    self->ptr = nullptr;
    self->len = 0;
    self->res = 0;
    return self;
}

// Purpose: Prepares a new draw-data buffer, which the logic thread fills
// with what the draw thread shows next (texts, messages, effects).
DrawData* __fastcall DrawData_Construct(DrawData* self, void* /*edx*/) {
    for (msvc6::String& text : self->texts) {
        ConstructEmpty(&text);
    }
    self->text_c66c0.allocator = 0;  // (an uninitialized local in the original)
    self->text_c66c0.ptr = nullptr;
    self->text_c66c0.len = 0;
    self->text_c66c0.res = 0;
    for (DrawMessageLine& line : self->lines) {
        ConstructEmpty(&line.text);
    }
    for (auto& record : self->effect_records) {
        reinterpret_cast<RecordConstructFn>(kRecordConstruct)(record, nullptr);
    }
    self->unk2ec = 0;
    return self;
}

// For the emulator's self-test (and the wrong-implementation check below):
// leaves the text pointer of the string at +0xc66c0 alone, next to the
// allocator byte the comparisons leave out.
DrawData* __fastcall WrongDrawDataConstruct(DrawData* self, void* edx) {
    char* const ptr = self->text_c66c0.ptr;
    DrawData_Construct(self, edx);
    self->text_c66c0.ptr = ptr;
    return self;
}

// ---------------------------------------------------------------------------
// Tests: the original and ours run on the same block (a game-heap block of
// DrawData's size, refilled with the same random bytes before each run, so
// the string arrays' allocator bytes, which come from their addresses,
// match); the whole object must be the same except the allocator byte at
// +0xc66c0. String_EmptyHighByteAllocator on strings at random places in a
// buffer.
// ---------------------------------------------------------------------------

namespace {

void CompareDrawData(test::Context& ctx, DrawDataConstructFn candidate, int cases) {
    auto* block = static_cast<DrawData*>(msvc6::GameOperatorNew(sizeof(DrawData)));
    std::vector<std::uint8_t> expected(sizeof(DrawData));
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        test::Rng fill(seed);
        fill.Fill(block, sizeof(DrawData));
        const DrawData* ra = Original_DrawData_Construct(block, nullptr);
        std::memcpy(expected.data(), block, sizeof(DrawData));
        test::Rng refill(seed);
        refill.Fill(block, sizeof(DrawData));
        const DrawData* rb = candidate(block, nullptr);
        expected[offsetof(DrawData, text_c66c0)] = block->text_c66c0.allocator;  // the known difference
        if (!S3RE_EXPECT(ra == block && rb == block) ||
            !S3RE_EXPECT_MEM_EQ(expected.data(), reinterpret_cast<const std::uint8_t*>(block), sizeof(DrawData))) {
            break;
        }
    }
    msvc6::GameOperatorDelete(block);
}

void CompareStringEmpty(test::Context& ctx, StringEmptyFn candidate, int cases) {
    std::vector<std::uint8_t> a(256), b(256);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        std::memcpy(b.data(), a.data(), a.size());
        const std::uint32_t at = ctx.rng().Below(240);
        auto* sa = reinterpret_cast<msvc6::String*>(a.data() + at);
        auto* sb = reinterpret_cast<msvc6::String*>(b.data() + at);
        msvc6::String* ra = Original_String_EmptyHighByteAllocator(sa, nullptr);
        msvc6::String* rb = candidate(sb, nullptr);
        // The allocator byte depends on the address: the high byte of each.
        const auto high = [](const void* p) { return static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(p) >> 24); };
        S3RE_EXPECT_EQ(a[at], high(sa));
        S3RE_EXPECT_EQ(b[at], high(sb));
        a[at] = b[at] = 0;
        if (!S3RE_EXPECT(ra == sa && rb == sb) || !S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
            break;
        }
    }
}

// Wrong: the allocator byte from the low byte of the address.
msvc6::String* __fastcall WrongStringEmpty(msvc6::String* self, void* edx) {
    String_EmptyHighByteAllocator(self, edx);
    self->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(self));
    return self;
}

}  // namespace

S3RE_TEST(DrawData_Construct_MatchesOriginal) {
    CompareDrawData(ctx, &DrawData_Construct, 20);
}
S3RE_TEST(DrawData_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDrawData(probe, &WrongDrawDataConstruct, 3);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(String_EmptyHighByteAllocator_MatchesOriginal) {
    CompareStringEmpty(ctx, &String_EmptyHighByteAllocator, 2000);
}
S3RE_TEST(String_EmptyHighByteAllocator_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStringEmpty(probe, &WrongStringEmpty, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
