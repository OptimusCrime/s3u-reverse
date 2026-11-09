// Map effect records (our name): 0x4c-byte records of the member object at
// world+0x14d2bc8 (Volker\LogicInit.cpp; 5000 records, then 10000 dwords
// and a few counters). Its constructor (0x5442c0) default-constructs each
// record; UpdateEnvironment and a settler update (0x50e9f0) build records
// with one of 13 small "make" functions (0x543930..0x543f70, each passing
// fixed values such as a type 9 and a graphics id 0xd0031 plus the tick
// counter) and add them with 0x5445e0, probably visual effects placed on
// the map (smoke, splashes, spell effects). Field meanings are unknown; the
// layout below is what the two constructors write.
//
// MapEffect::MapEffect (0x00543830): () default constructor: dwords +0x0,
//   +0x8..+0x24, +0x44, +0x48 = 0; words +0x4, +0x6, +0x28, +0x32, +0x3c
//   = 0; dwords +0x2c, +0x34 = -1; byte +0x38 = 0xff; bytes +0x39, +0x3a
//   = 0. +0x30, +0x40 and the padding bytes are left alone. EAX = this
//   (ECX/EDX are left 0/-1, not used by callers). ret.
// MapEffect::MapEffect (0x00543880): (20 arguments) constructor: a1 dword
//   +0x0, a2 word +0x4, a3 word +0x6, a4 byte +0x3a, a5 word +0x3c, a6..a9
//   dwords +0x8..+0x14, a10 word +0x28, a11..a14 dwords +0x18..+0x24, a15
//   dword +0x2c, a16 word +0x30, a17 word +0x32, a18 byte +0x38, a19 byte
//   +0x39, a20 dword +0x34; dwords +0x40..+0x48 = 0. The make functions
//   pass their caller's EDX (uninitialized) as a1. EAX = this. ret 0x50.
// thiscall (ECX = the record).

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::size_t kEffectBytes = 0x4c;

template <typename T>
void Put(void* self, std::size_t offset, T value) {
    std::memcpy(static_cast<std::uint8_t*>(self) + offset, &value, sizeof(T));
}

}  // namespace

using DefaultEffectFn = void*(__fastcall*)(void* self, void* edx);
using EffectFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                    std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                    std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                    std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15,
                                    std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                                    std::uint32_t a20);

// Purpose: Sets up an empty map effect record (probably a visual effect on the map) when the effect
// table is created.
void* __fastcall MapEffect_DefaultConstruct(void* self, void*) {
    for (const std::size_t offset : {0x0u, 0x8u, 0xcu, 0x10u, 0x14u, 0x18u, 0x1cu, 0x20u, 0x24u, 0x44u, 0x48u}) {
        Put<std::uint32_t>(self, offset, 0);
    }
    for (const std::size_t offset : {0x4u, 0x6u, 0x28u, 0x32u, 0x3cu}) {
        Put<std::uint16_t>(self, offset, 0);
    }
    Put<std::uint32_t>(self, 0x2c, 0xffffffffu);
    Put<std::uint32_t>(self, 0x34, 0xffffffffu);
    Put<std::uint8_t>(self, 0x38, 0xff);
    Put<std::uint8_t>(self, 0x39, 0);
    Put<std::uint8_t>(self, 0x3a, 0);
    return self;
}

// Purpose: Fills in a map effect record (type, position, graphics, timing; exact meaning of the fields
// unknown) before it is added to the effect table.
void* __fastcall MapEffect_Construct(void* self, void*, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                     std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                     std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                     std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15,
                                     std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                                     std::uint32_t a20) {
    Put<std::uint32_t>(self, 0x0, a1);
    Put<std::uint16_t>(self, 0x4, static_cast<std::uint16_t>(a2));
    Put<std::uint16_t>(self, 0x6, static_cast<std::uint16_t>(a3));
    Put<std::uint8_t>(self, 0x3a, static_cast<std::uint8_t>(a4));
    Put<std::uint16_t>(self, 0x3c, static_cast<std::uint16_t>(a5));
    Put<std::uint32_t>(self, 0x8, a6);
    Put<std::uint32_t>(self, 0xc, a7);
    Put<std::uint32_t>(self, 0x10, a8);
    Put<std::uint32_t>(self, 0x14, a9);
    Put<std::uint16_t>(self, 0x28, static_cast<std::uint16_t>(a10));
    Put<std::uint32_t>(self, 0x18, a11);
    Put<std::uint32_t>(self, 0x1c, a12);
    Put<std::uint32_t>(self, 0x20, a13);
    Put<std::uint32_t>(self, 0x24, a14);
    Put<std::uint32_t>(self, 0x2c, a15);
    Put<std::uint16_t>(self, 0x30, static_cast<std::uint16_t>(a16));
    Put<std::uint16_t>(self, 0x32, static_cast<std::uint16_t>(a17));
    Put<std::uint8_t>(self, 0x38, static_cast<std::uint8_t>(a18));
    Put<std::uint8_t>(self, 0x39, static_cast<std::uint8_t>(a19));
    Put<std::uint32_t>(self, 0x34, a20);
    Put<std::uint32_t>(self, 0x40, 0);
    Put<std::uint32_t>(self, 0x44, 0);
    Put<std::uint32_t>(self, 0x48, 0);
    return self;
}

S3RE_HOOK(0x00543830, MapEffect_DefaultConstruct, DefaultEffectFn, Original_MapEffect_DefaultConstruct,
          MapEffect_DefaultConstruct);
S3RE_HOOK(0x00543880, MapEffect_Construct, EffectFn, Original_MapEffect_Construct, MapEffect_Construct);

// MapEffect::SetLinks (0x00544290): (list, previous, next) thiscall, ret
// 0xc. The records are kept in doubly linked lists (heads at table +0x5cc60
// + list * 4, pushed by 0x545df0 and unlinked by 0x545e40): +0x40 = list;
// +0x44 = previous and +0x48 = next unless the argument is -1 (keep). EAX =
// next (the last argument loaded).
using SetLinksFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t list, std::uint32_t previous,
                                              std::uint32_t next);

// Purpose: Links a map effect record into one of the effect table's lists
// (probably one list per kind or state of effect), or updates its links.
std::uint32_t __fastcall MapEffect_SetLinks(void* self, void*, std::uint32_t list, std::uint32_t previous,
                                            std::uint32_t next) {
    Put<std::uint32_t>(self, 0x40, list);
    if (previous != 0xffffffffu) {
        Put<std::uint32_t>(self, 0x44, previous);
    }
    if (next != 0xffffffffu) {
        Put<std::uint32_t>(self, 0x48, next);
    }
    return next;
}

S3RE_HOOK(0x00544290, MapEffect_SetLinks, SetLinksFn, Original_MapEffect_SetLinks, MapEffect_SetLinks);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Each case: two identical random buffers with the record in the middle
// (guard bytes before and after), the original constructs one, ours the
// other; EAX (relative to the buffer) and every byte must match.
constexpr std::size_t kGuard = 64;
constexpr std::size_t kBufferBytes = kGuard + kEffectBytes + kGuard;

template <typename Call>
void CompareEffect(test::Context& ctx, int cases, Call call) {
    std::uint8_t original[kBufferBytes];
    std::uint8_t ours[kBufferBytes];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(original, sizeof(original));
        std::memcpy(ours, original, sizeof(ours));
        std::uint32_t args[20];
        for (std::uint32_t& a : args) {
            a = ctx.rng().Next32();
        }
        const auto [expected, actual] = call(original + kGuard, ours + kGuard, args);
        if (!S3RE_EXPECT(expected == original + kGuard) || !S3RE_EXPECT(actual == ours + kGuard) ||
            !ctx.ExpectMemEq(original, ours, sizeof(original), "original record", "our record", __FILE__, __LINE__)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

struct Results {
    void* expected;
    void* actual;
};

Results CallConstruct(EffectFn candidate, std::uint8_t* original, std::uint8_t* ours, const std::uint32_t* a) {
    return {Original_MapEffect_Construct(original, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                         a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]),
            candidate(ours, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12],
                      a[13], a[14], a[15], a[16], a[17], a[18], a[19])};
}

// Wrong: a16 and a17 swapped.
void* __fastcall WrongMapEffectConstruct(void* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                         std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                         std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                         std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15,
                                         std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                                         std::uint32_t a20) {
    return MapEffect_Construct(self, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a17, a16,
                               a18, a19, a20);
}

}  // namespace

S3RE_TEST(MapEffect_DefaultConstruct_MatchesOriginal) {
    CompareEffect(ctx, 1000, [](std::uint8_t* original, std::uint8_t* ours, const std::uint32_t*) {
        return Results{Original_MapEffect_DefaultConstruct(original, nullptr), MapEffect_DefaultConstruct(ours, nullptr)};
    });
}
S3RE_TEST(MapEffect_Construct_MatchesOriginal) {
    CompareEffect(ctx, 10000, [](std::uint8_t* original, std::uint8_t* ours, const std::uint32_t* a) {
        return CallConstruct(&MapEffect_Construct, original, ours, a);
    });
}
namespace {

void CompareSetLinks(test::Context& ctx, SetLinksFn candidate, int cases) {
    CompareEffect(ctx, cases, [&](std::uint8_t* original, std::uint8_t* ours, const std::uint32_t* a) {
        const std::uint32_t previous = ctx.rng().Chance(1, 2) ? 0xffffffffu : a[1];
        const std::uint32_t next = ctx.rng().Chance(1, 2) ? 0xffffffffu : a[2];
        const std::uint32_t ra = Original_MapEffect_SetLinks(original, nullptr, a[0], previous, next);
        const std::uint32_t rb = candidate(ours, nullptr, a[0], previous, next);
        S3RE_EXPECT_EQ(ra, rb);
        return Results{original, ours};
    });
}

// Wrong: -1 is stored as a previous link instead of keeping the old one.
std::uint32_t __fastcall WrongSetLinks(void* self, void* edx, std::uint32_t list, std::uint32_t previous,
                                       std::uint32_t next) {
    Put<std::uint32_t>(self, 0x44, previous);
    return MapEffect_SetLinks(self, edx, list, previous, next);
}

}  // namespace

S3RE_TEST(MapEffect_SetLinks_MatchesOriginal) {
    CompareSetLinks(ctx, &MapEffect_SetLinks, 20000);
}

S3RE_TEST(MapEffect_SetLinks_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetLinks(probe, &WrongSetLinks, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(MapEffect_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEffect(probe, 100, [](std::uint8_t* original, std::uint8_t* ours, const std::uint32_t* a) {
        return CallConstruct(&WrongMapEffectConstruct, original, ours, a);
    });
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
