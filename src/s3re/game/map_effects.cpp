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


// The 13 make functions (0x543930..0x543f70; our names MapEffect_Make_<address>)
// are thiscall on the destination record, with 3 to 13 stack arguments (ret
// 4 * count). Each pushes a fixed mix of its arguments, constants and the
// caller's EDX (which the original code passes along as it is: a value left
// in the register by the caller) as the 20 constructor arguments, builds the
// record in a 0x4c-byte stack temporary with 0x543880 and copies all 19
// dwords into ECX (rep movs). EAX = ECX. The argument mapping per function
// (a<i> = stack argument i, edx = the caller's EDX) is written out below; it
// was read off the push sequences by a script and checked by the tests.
//
// The copy includes the five padding bytes the constructor never writes
// (+0x2a, +0x2b, +0x3b, +0x3e, +0x3f), so the original leaves whatever was
// on its stack there. Ours does the same with its own temporary; the values
// differ between the two (stack garbage), and the tests ignore exactly these
// bytes.
using Make3Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t);
using Make6Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make7Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make8Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make9Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make10Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make11Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make12Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make13Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);

namespace {

constexpr std::size_t kPaddingBytes[] = {0x2a, 0x2b, 0x3b, 0x3e, 0x3f};

void* MakeEffect(void* self, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                 std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9,
                 std::uint32_t a10, std::uint32_t a11, std::uint32_t a12, std::uint32_t a13, std::uint32_t a14,
                 std::uint32_t a15, std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                 std::uint32_t a20) {
    std::uint8_t temporary[kEffectBytes];  // padding left uninitialized, as in the original
    MapEffect_Construct(temporary, nullptr, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                        a17, a18, a19, a20);
    std::memcpy(self, temporary, kEffectBytes);
    return self;
}

}  // namespace

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543930(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543930, MapEffect_Make_543930, Make6Fn, Original_MapEffect_Make_543930, MapEffect_Make_543930);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_5439a0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, a6, 0u, 0xffffffffu);
}

S3RE_HOOK(0x005439a0, MapEffect_Make_5439a0, Make7Fn, Original_MapEffect_Make_5439a0, MapEffect_Make_5439a0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543a10(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a9, 0u, 0u, a1, a2, a3, a4, 0u, a5, a6, a5, a6,
                      a7, a8, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543a10, MapEffect_Make_543a10, Make10Fn, Original_MapEffect_Make_543a10, MapEffect_Make_543a10);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543aa0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10,
                                       std::uint32_t a11, std::uint32_t a12) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a12, 0u, 0u, a1, a2, a3, a4, a5, a6, a7, a8, a9,
                      a10, a11, a12, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543aa0, MapEffect_Make_543aa0, Make13Fn, Original_MapEffect_Make_543aa0, MapEffect_Make_543aa0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543b40(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7) {
    return MakeEffect(self, a1, a2, a2, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, 0u, a2, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543b40, MapEffect_Make_543b40, Make8Fn, Original_MapEffect_Make_543b40, MapEffect_Make_543b40);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543bc0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, a1, a2, a9, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, a8, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543bc0, MapEffect_Make_543bc0, Make10Fn, Original_MapEffect_Make_543bc0, MapEffect_Make_543bc0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543c40(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10,
                                       std::uint32_t a11) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a11, 0u, 0u, a1, a2, a3, a4, a5, a6, a7, a8, a9,
                      a10, 0u, a11, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543c40, MapEffect_Make_543c40, Make12Fn, Original_MapEffect_Make_543c40, MapEffect_Make_543c40);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543ce0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8) {
    return MakeEffect(self, a1, a2, a8, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, 0u, a8, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543ce0, MapEffect_Make_543ce0, Make9Fn, Original_MapEffect_Make_543ce0, MapEffect_Make_543ce0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543d60(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, a1, a2, a3, reinterpret_cast<std::uint32_t>(edx), a0, a4, a5, a4, a5, 0u, a6, a7, a6, a7,
                      a8, 0u, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543d60, MapEffect_Make_543d60, Make10Fn, Original_MapEffect_Make_543d60, MapEffect_Make_543d60);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543df0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10) {
    return MakeEffect(self, a1, a2, a3, reinterpret_cast<std::uint32_t>(edx), a0, a4, a5, a4, a5, 0u, a6, a7, a6, a7,
                      a8, a9, a10, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543df0, MapEffect_Make_543df0, Make11Fn, Original_MapEffect_Make_543df0, MapEffect_Make_543df0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543e80(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, 0xffu, a6, 0xffffffffu);
}

S3RE_HOOK(0x00543e80, MapEffect_Make_543e80, Make7Fn, Original_MapEffect_Make_543e80, MapEffect_Make_543e80);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543ef0(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a1, 0u, 0u, a2, a3, a2, a3, 0u, a4, a5, a4, a5,
                      a6, 0u, a7, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543ef0, MapEffect_Make_543ef0, Make8Fn, Original_MapEffect_Make_543ef0, MapEffect_Make_543ef0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543f70(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), 0xffffffffu, 1u, 0u, 0u, a0, a1, a0, a1, 0u, 0u, 0u,
                      0u, 0u, 0xffffffffu, 0u, 0u, 0xffu, 0u, a2);
}

S3RE_HOOK(0x00543f70, MapEffect_Make_543f70, Make3Fn, Original_MapEffect_Make_543f70, MapEffect_Make_543f70);

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

namespace {

struct MakeFunction {
    void* original;
    void* ours;
    int arguments;
};

const MakeFunction* MakeFunctions() {
    static const MakeFunction table[] = {
    {reinterpret_cast<void*>(Original_MapEffect_Make_543930), reinterpret_cast<void*>(&MapEffect_Make_543930), 6},
    {reinterpret_cast<void*>(Original_MapEffect_Make_5439a0), reinterpret_cast<void*>(&MapEffect_Make_5439a0), 7},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543a10), reinterpret_cast<void*>(&MapEffect_Make_543a10), 10},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543aa0), reinterpret_cast<void*>(&MapEffect_Make_543aa0), 13},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543b40), reinterpret_cast<void*>(&MapEffect_Make_543b40), 8},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543bc0), reinterpret_cast<void*>(&MapEffect_Make_543bc0), 10},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543c40), reinterpret_cast<void*>(&MapEffect_Make_543c40), 12},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543ce0), reinterpret_cast<void*>(&MapEffect_Make_543ce0), 9},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543d60), reinterpret_cast<void*>(&MapEffect_Make_543d60), 10},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543df0), reinterpret_cast<void*>(&MapEffect_Make_543df0), 11},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543e80), reinterpret_cast<void*>(&MapEffect_Make_543e80), 7},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543ef0), reinterpret_cast<void*>(&MapEffect_Make_543ef0), 8},
    {reinterpret_cast<void*>(Original_MapEffect_Make_543f70), reinterpret_cast<void*>(&MapEffect_Make_543f70), 3}};
    return table;
}
constexpr int kMakeFunctions = 13;

void* InvokeMake(void* fn, int arguments, void* self, void* edx, const std::uint32_t* a) {
    switch (arguments) {
        case 3: return reinterpret_cast<Make3Fn>(fn)(self, edx, a[0], a[1], a[2]);
        case 6: return reinterpret_cast<Make6Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7: return reinterpret_cast<Make7Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 8: return reinterpret_cast<Make8Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        case 9: return reinterpret_cast<Make9Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
        case 10: return reinterpret_cast<Make10Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        case 11: return reinterpret_cast<Make11Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
        case 12: return reinterpret_cast<Make12Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
        case 13: return reinterpret_cast<Make13Fn>(fn)(self, edx, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
        default: return nullptr;
    }
}

// Like CompareEffect, but the five padding bytes of the record are copied
// from the original's result before comparing (they hold stack garbage in
// both, see the make functions).
void CompareMake(test::Context& ctx, const MakeFunction& f, void* candidate, int cases) {
    std::uint8_t original[kBufferBytes];
    std::uint8_t ours[kBufferBytes];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(original, sizeof(original));
        std::memcpy(ours, original, sizeof(ours));
        std::uint32_t args[13];
        for (std::uint32_t& a : args) {
            a = ctx.rng().Next32();
        }
        void* const edx = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ctx.rng().Next32()));
        void* const expected = InvokeMake(f.original, f.arguments, original + kGuard, edx, args);
        void* const actual = InvokeMake(candidate, f.arguments, ours + kGuard, edx, args);
        for (const std::size_t offset : kPaddingBytes) {
            ours[kGuard + offset] = original[kGuard + offset];
        }
        if (!S3RE_EXPECT(expected == original + kGuard) || !S3RE_EXPECT(actual == ours + kGuard) ||
            !ctx.ExpectMemEq(original, ours, sizeof(original), "original record", "our record", __FILE__, __LINE__)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%d arguments)", i, f.arguments);
            return;
        }
    }
}

// Wrong: correct except byte +0x3c, which follows a padding byte (shows that
// ignoring the padding doesn't hide its neighbours).
void* __fastcall WrongMake543930(void* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    MapEffect_Make_543930(self, edx, a0, a1, a2, a3, a4, a5);
    static_cast<std::uint8_t*>(self)[0x3c] ^= 1;
    return self;
}

}  // namespace

S3RE_TEST(MapEffect_Make_MatchOriginal) {
    for (int k = 0; k < kMakeFunctions; ++k) {
        CompareMake(ctx, MakeFunctions()[k], MakeFunctions()[k].ours, 3000);
    }
}

S3RE_TEST(MapEffect_Make_ComparisonDetectsWrongImplementation) {
    // A neighbouring byte of the padding is wrong.
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMake(probe, MakeFunctions()[0], reinterpret_cast<void*>(&WrongMake543930), 100);
    S3RE_EXPECT(probe.failures() > 0);
    // Another make function with the same argument count (0x543d60 against 0x543bc0, both 10).
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    CompareMake(probe2, MakeFunctions()[5], MakeFunctions()[8].ours, 100);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
