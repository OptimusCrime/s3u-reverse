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

#include "game/debug_log.h"
#include "game/layout.h"
#include "game/raw.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

using layout::MapEffect;
using layout::MapEffectTable;

using DefaultEffectFn = void*(__fastcall*)(MapEffect* self, void* edx);
using EffectFn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                    std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                    std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                    std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15,
                                    std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                                    std::uint32_t a20);

// Purpose: Sets up an empty map effect record (probably a visual effect on the map) when the effect
// table is created.
void* __fastcall MapEffect_DefaultConstruct(MapEffect* self, void*) {
    self->start_tick = 0;
    self->start_x = 0;
    self->start_y = 0;
    self->end_x = 0;
    self->end_y = 0;
    self->unk18 = 0;
    self->unk1c = 0;
    self->unk20 = 0;
    self->unk24 = 0;
    self->previous = 0;
    self->next = 0;
    self->duration = 0;
    self->unk06 = 0;
    self->arc = 0;
    self->unk32 = 0;
    self->source_id = 0;
    self->unk2c = 0xffffffffu;
    self->unk34 = 0xffffffffu;
    self->unk38 = 0xff;
    self->unk39 = 0;
    self->source_kind = 0;
    return self;
}

// Purpose: Fills in a map effect record (type, position, graphics, timing; exact meaning of the fields
// unknown) before it is added to the effect table.
void* __fastcall MapEffect_Construct(MapEffect* self, void*, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                     std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                     std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                     std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15,
                                     std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                                     std::uint32_t a20) {
    self->start_tick = a1;
    self->duration = static_cast<std::int16_t>(a2);
    self->unk06 = static_cast<std::uint16_t>(a3);
    self->source_kind = static_cast<std::uint8_t>(a4);
    self->source_id = static_cast<std::uint16_t>(a5);
    self->start_x = static_cast<std::int32_t>(a6);
    self->start_y = static_cast<std::int32_t>(a7);
    self->end_x = static_cast<std::int32_t>(a8);
    self->end_y = static_cast<std::int32_t>(a9);
    self->arc = static_cast<std::int16_t>(a10);
    self->unk18 = a11;
    self->unk1c = a12;
    self->unk20 = a13;
    self->unk24 = a14;
    self->unk2c = a15;
    self->unk30 = static_cast<std::uint16_t>(a16);
    self->unk32 = static_cast<std::uint16_t>(a17);
    self->unk38 = static_cast<std::uint8_t>(a18);
    self->unk39 = static_cast<std::uint8_t>(a19);
    self->unk34 = a20;
    self->list = 0;
    self->previous = 0;
    self->next = 0;
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
using SetLinksFn = std::uint32_t(__fastcall*)(MapEffect* self, void* edx, std::uint32_t list, std::uint32_t previous,
                                              std::uint32_t next);

// Purpose: Links a map effect record into one of the effect table's lists
// (probably one list per kind or state of effect), or updates its links.
std::uint32_t __fastcall MapEffect_SetLinks(MapEffect* self, void*, std::uint32_t list, std::uint32_t previous,
                                            std::uint32_t next) {
    self->list = list;
    if (previous != 0xffffffffu) {
        self->previous = previous;
    }
    if (next != 0xffffffffu) {
        self->next = next;
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
using Make3Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t);
using Make6Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make7Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make8Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make9Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make10Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make11Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make12Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using Make13Fn = void*(__fastcall*)(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);

namespace {

// The record bytes the constructor never writes (MapEffect::pad*).
constexpr std::size_t kPaddingBytes[] = {offsetof(MapEffect, pad2a), offsetof(MapEffect, pad2a) + 1,
                                         offsetof(MapEffect, pad3b), offsetof(MapEffect, pad3e),
                                         offsetof(MapEffect, pad3e) + 1};

void* MakeEffect(MapEffect* self, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                 std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9,
                 std::uint32_t a10, std::uint32_t a11, std::uint32_t a12, std::uint32_t a13, std::uint32_t a14,
                 std::uint32_t a15, std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19,
                 std::uint32_t a20) {
    MapEffect temporary;  // padding left uninitialized, as in the original
    MapEffect_Construct(&temporary, nullptr, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                        a17, a18, a19, a20);
    std::memcpy(self, &temporary, sizeof(MapEffect));
    return self;
}

}  // namespace

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543930(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543930, MapEffect_Make_543930, Make6Fn, Original_MapEffect_Make_543930, MapEffect_Make_543930);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_5439a0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, a6, 0u, 0xffffffffu);
}

S3RE_HOOK(0x005439a0, MapEffect_Make_5439a0, Make7Fn, Original_MapEffect_Make_5439a0, MapEffect_Make_5439a0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543a10(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a9, 0u, 0u, a1, a2, a3, a4, 0u, a5, a6, a5, a6,
                      a7, a8, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543a10, MapEffect_Make_543a10, Make10Fn, Original_MapEffect_Make_543a10, MapEffect_Make_543a10);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543aa0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10,
                                       std::uint32_t a11, std::uint32_t a12) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a12, 0u, 0u, a1, a2, a3, a4, a5, a6, a7, a8, a9,
                      a10, a11, a12, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543aa0, MapEffect_Make_543aa0, Make13Fn, Original_MapEffect_Make_543aa0, MapEffect_Make_543aa0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543b40(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7) {
    return MakeEffect(self, a1, a2, a2, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, 0u, a2, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543b40, MapEffect_Make_543b40, Make8Fn, Original_MapEffect_Make_543b40, MapEffect_Make_543b40);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543bc0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, a1, a2, a9, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, a8, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543bc0, MapEffect_Make_543bc0, Make10Fn, Original_MapEffect_Make_543bc0, MapEffect_Make_543bc0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543c40(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10,
                                       std::uint32_t a11) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a11, 0u, 0u, a1, a2, a3, a4, a5, a6, a7, a8, a9,
                      a10, 0u, a11, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543c40, MapEffect_Make_543c40, Make12Fn, Original_MapEffect_Make_543c40, MapEffect_Make_543c40);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543ce0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8) {
    return MakeEffect(self, a1, a2, a8, reinterpret_cast<std::uint32_t>(edx), a0, a3, a4, a3, a4, 0u, a5, a6, a5, a6,
                      a7, 0u, a8, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543ce0, MapEffect_Make_543ce0, Make9Fn, Original_MapEffect_Make_543ce0, MapEffect_Make_543ce0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543d60(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9) {
    return MakeEffect(self, a1, a2, a3, reinterpret_cast<std::uint32_t>(edx), a0, a4, a5, a4, a5, 0u, a6, a7, a6, a7,
                      a8, 0u, a9, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543d60, MapEffect_Make_543d60, Make10Fn, Original_MapEffect_Make_543d60, MapEffect_Make_543d60);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543df0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10) {
    return MakeEffect(self, a1, a2, a3, reinterpret_cast<std::uint32_t>(edx), a0, a4, a5, a4, a5, 0u, a6, a7, a6, a7,
                      a8, a9, a10, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543df0, MapEffect_Make_543df0, Make11Fn, Original_MapEffect_Make_543df0, MapEffect_Make_543df0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543e80(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a0, 0u, 0u, a1, a2, a1, a2, 0u, a3, a4, a3, a4,
                      a5, 0u, a0, 0xffu, a6, 0xffffffffu);
}

S3RE_HOOK(0x00543e80, MapEffect_Make_543e80, Make7Fn, Original_MapEffect_Make_543e80, MapEffect_Make_543e80);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543ef0(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                       std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                       std::uint32_t a7) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), a0, a1, 0u, 0u, a2, a3, a2, a3, 0u, a4, a5, a4, a5,
                      a6, 0u, a7, 0xffu, 0u, 0xffffffffu);
}

S3RE_HOOK(0x00543ef0, MapEffect_Make_543ef0, Make8Fn, Original_MapEffect_Make_543ef0, MapEffect_Make_543ef0);

// Purpose: Builds a map effect record of one kind (probably one kind of
// visual effect; meaning of the fixed values unknown) in place.
void* __fastcall MapEffect_Make_543f70(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2) {
    return MakeEffect(self, reinterpret_cast<std::uint32_t>(edx), 0xffffffffu, 1u, 0u, 0u, a0, a1, a0, a1, 0u, 0u, 0u,
                      0u, 0u, 0xffffffffu, 0u, 0u, 0xffu, 0u, a2);
}

S3RE_HOOK(0x00543f70, MapEffect_Make_543f70, Make3Fn, Original_MapEffect_Make_543f70, MapEffect_Make_543f70);

// Batch 25.
//
// MapEffect_PositionAt (0x00544210): (tick, int* x, int* y) thiscall on a
//   record, ret 0xc. Linear interpolation from (+0x8, +0xc) at the start
//   tick +0x0 to (+0x10, +0x14) over the duration word +0x4 (signed):
//   *x = x0 + (x1 - x0) * (tick - start) / duration, the same for *y (32-bit
//   products, signed division). If the word +0x28 (signed, probably an arc
//   height) is not 0, *y -= arc * min(tick - start, duration - (tick -
//   start)) * 2 / duration: a hop that peaks halfway. The fields are read
//   again after each store, as in the original. EAX not used by callers.
// MapEffects_SetMapSize (0x00544320): (a, b) thiscall on the effect table
//   (world+0x14d2bc8), ret 8; called once at game start with (map size,
//   200). +0x668b4 = 24a + 2b and +0x668b8 = 9a + 2b (probably the map's
//   size in pixels plus a margin), +0x668a4 / +0x668a8 = (each - 1) / 256 +
//   1 (blocks of 256 pixels; signed division), +0x668b0 = the product of
//   the two block counts / 16 (signed). EAX = that last value.

using PositionAtFn = void(__fastcall*)(MapEffect* self, void* edx, std::int32_t tick, std::int32_t* x, std::int32_t* y);
using SetMapSizeFn = std::int32_t(__fastcall*)(MapEffectTable* self, void* edx, std::int32_t a, std::int32_t b);

void __fastcall MapEffect_PositionAt(MapEffect* self, void* edx, std::int32_t tick, std::int32_t* x, std::int32_t* y);
std::int32_t __fastcall MapEffects_SetMapSize(MapEffectTable* self, void* edx, std::int32_t a, std::int32_t b);

namespace {

// (a * b) / c with the 32-bit wrap-around of imul and a signed idiv.
std::int32_t MulDiv32(std::int32_t a, std::int32_t b, std::int32_t c) {
    const auto product = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
    return product / c;
}

}  // namespace

S3RE_HOOK(0x00544210, MapEffect_PositionAt, PositionAtFn, Original_MapEffect_PositionAt, MapEffect_PositionAt);

// Purpose: Where a moving map effect is at a given tick (probably flying
// objects such as stones or spells, with a parabolic hop).
// The outputs may alias the record (the tests check that): every field is
// read again after a store, as in the original (MSVC does no type-based
// alias analysis, so the later field reads see the int32 stores).
void __fastcall MapEffect_PositionAt(MapEffect* self, void* /*edx*/, std::int32_t tick, std::int32_t* x, std::int32_t* y) {
    const auto elapsed = [&]() {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(tick) - self->start_tick);
    };
    const std::int32_t x0 = self->start_x;
    *x = static_cast<std::int32_t>(static_cast<std::uint32_t>(MulDiv32(
                                       static_cast<std::int32_t>(static_cast<std::uint32_t>(self->end_x) - static_cast<std::uint32_t>(x0)),
                                       elapsed(), self->duration)) + static_cast<std::uint32_t>(x0));
    const std::int32_t y0 = self->start_y;
    std::int32_t yy = static_cast<std::int32_t>(static_cast<std::uint32_t>(MulDiv32(
                                                    static_cast<std::int32_t>(static_cast<std::uint32_t>(self->end_y) - static_cast<std::uint32_t>(y0)),
                                                    elapsed(), self->duration)) + static_cast<std::uint32_t>(y0));
    *y = yy;
    const std::int16_t arc = self->arc;
    if (arc != 0) {
        const std::int32_t duration = self->duration;
        const std::uint32_t start = self->start_tick;
        std::int32_t a = static_cast<std::int32_t>(static_cast<std::uint32_t>(tick) - start);
        const std::int32_t b = static_cast<std::int32_t>(static_cast<std::uint32_t>(duration) - static_cast<std::uint32_t>(tick) + start);
        if (a >= b) {
            a = b;
        }
        const std::uint32_t product = static_cast<std::uint32_t>(arc) * static_cast<std::uint32_t>(a) * 2u;
        yy = static_cast<std::int32_t>(static_cast<std::uint32_t>(yy) - static_cast<std::uint32_t>(static_cast<std::int32_t>(product) / duration));
        *y = yy;
    }
}

S3RE_HOOK(0x00544320, MapEffects_SetMapSize, SetMapSizeFn, Original_MapEffects_SetMapSize, MapEffects_SetMapSize);

// Purpose: Sets up the map effect table's grid of 256-pixel blocks for the
// map size at the start of a game (probably to find the effects to draw).
std::int32_t __fastcall MapEffects_SetMapSize(MapEffectTable* self, void* /*edx*/, std::int32_t a, std::int32_t b) {
    const auto ua = static_cast<std::uint32_t>(a), ub = static_cast<std::uint32_t>(b);
    const auto width = static_cast<std::int32_t>(2 * (ub + 12 * ua));
    const auto height = static_cast<std::int32_t>(9 * ua + 2 * ub);
    self->width = width;
    self->height = height;
    const std::int32_t blocksX = static_cast<std::int32_t>(static_cast<std::uint32_t>(width) - 1) / 256 + 1;
    self->blocks_x = blocksX;
    const std::int32_t blocksY = static_cast<std::int32_t>(static_cast<std::uint32_t>(height) - 1) / 256 + 1;
    self->blocks_y = blocksY;
    const std::int32_t cells = static_cast<std::int32_t>(static_cast<std::uint32_t>(blocksY) * static_cast<std::uint32_t>(blocksX)) / 16;
    self->block_count_16 = cells;
    return cells;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Each case: two identical random buffers with the record in the middle
// (guard bytes before and after), the original constructs one, ours the
// other; EAX (relative to the buffer) and every byte must match.
constexpr std::size_t kGuard = 64;
constexpr std::size_t kBufferBytes = kGuard + sizeof(MapEffect) + kGuard;

MapEffect* RecordIn(std::uint8_t* buffer) {
    return reinterpret_cast<MapEffect*>(buffer + kGuard);
}

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
        const auto [expected, actual] = call(RecordIn(original), RecordIn(ours), args);
        if (!S3RE_EXPECT(expected == RecordIn(original)) || !S3RE_EXPECT(actual == RecordIn(ours)) ||
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

Results CallConstruct(EffectFn candidate, MapEffect* original, MapEffect* ours, const std::uint32_t* a) {
    return {Original_MapEffect_Construct(original, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                         a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]),
            candidate(ours, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12],
                      a[13], a[14], a[15], a[16], a[17], a[18], a[19])};
}

// Wrong: a16 and a17 swapped.
void* __fastcall WrongMapEffectConstruct(MapEffect* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
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
    CompareEffect(ctx, 1000, [](MapEffect* original, MapEffect* ours, const std::uint32_t*) {
        return Results{Original_MapEffect_DefaultConstruct(original, nullptr), MapEffect_DefaultConstruct(ours, nullptr)};
    });
}
S3RE_TEST(MapEffect_Construct_MatchesOriginal) {
    CompareEffect(ctx, 10000, [](MapEffect* original, MapEffect* ours, const std::uint32_t* a) {
        return CallConstruct(&MapEffect_Construct, original, ours, a);
    });
}
namespace {

void CompareSetLinks(test::Context& ctx, SetLinksFn candidate, int cases) {
    CompareEffect(ctx, cases, [&](MapEffect* original, MapEffect* ours, const std::uint32_t* a) {
        const std::uint32_t previous = ctx.rng().Chance(1, 2) ? 0xffffffffu : a[1];
        const std::uint32_t next = ctx.rng().Chance(1, 2) ? 0xffffffffu : a[2];
        const std::uint32_t ra = Original_MapEffect_SetLinks(original, nullptr, a[0], previous, next);
        const std::uint32_t rb = candidate(ours, nullptr, a[0], previous, next);
        S3RE_EXPECT_EQ(ra, rb);
        return Results{original, ours};
    });
}

// Wrong: -1 is stored as a previous link instead of keeping the old one.
std::uint32_t __fastcall WrongSetLinks(MapEffect* self, void* edx, std::uint32_t list, std::uint32_t previous,
                                       std::uint32_t next) {
    self->previous = previous;
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
    CompareEffect(probe, 100, [](MapEffect* original, MapEffect* ours, const std::uint32_t* a) {
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

void* InvokeMake(void* fn, int arguments, MapEffect* self, void* edx, const std::uint32_t* a) {
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
        void* const expected = InvokeMake(f.original, f.arguments, RecordIn(original), edx, args);
        void* const actual = InvokeMake(candidate, f.arguments, RecordIn(ours), edx, args);
        for (const std::size_t offset : kPaddingBytes) {
            ours[kGuard + offset] = original[kGuard + offset];
        }
        if (!S3RE_EXPECT(expected == RecordIn(original)) || !S3RE_EXPECT(actual == RecordIn(ours)) ||
            !ctx.ExpectMemEq(original, ours, sizeof(original), "original record", "our record", __FILE__, __LINE__)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%d arguments)", i, f.arguments);
            return;
        }
    }
}

// Wrong: correct except byte +0x3c, which follows a padding byte (shows that
// ignoring the padding doesn't hide its neighbours).
void* __fastcall WrongMake543930(MapEffect* self, void* edx, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    MapEffect_Make_543930(self, edx, a0, a1, a2, a3, a4, a5);
    self->source_id ^= 1;  // the low bit of byte +0x3c
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

// Batch 25: PositionAt and SetMapSize on random records / tables; the
// outputs may alias the record (stores followed by reloads).
namespace {

void CompareEffectMath(test::Context& ctx, PositionAtFn positionAt, SetMapSizeFn setMapSize, int cases) {
    // Table buffers (sizeof(MapEffectTable) plus guard bytes); the record
    // tests use their first 0x60 bytes (record 0 plus 0x14 guard bytes).
    static std::uint8_t a[0x66900], b[0x66900];
    static_assert(sizeof(a) >= sizeof(MapEffectTable) + 4);
    auto* const tableA = reinterpret_cast<MapEffectTable*>(a);
    auto* const tableB = reinterpret_cast<MapEffectTable*>(b);
    // The counters from slots_used to the end of the table, plus 4 guard bytes.
    constexpr std::size_t kCounters = offsetof(MapEffectTable, slots_used);
    constexpr std::size_t kCounterBytes = sizeof(MapEffectTable) - kCounters + 4;
    constexpr std::size_t kRecordBytes = 0x60;
    auto* const recordA = &tableA->records[0];
    auto* const recordB = &tableB->records[0];
    for (int i = 0; i < cases; ++i) {
        const bool table = ctx.rng().Chance(1, 4);
        if (table) {
            const std::int32_t sa = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32()) : ctx.rng().Range(-10, 1100);
            const std::int32_t sb = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32()) : ctx.rng().Range(-10, 300);
            ctx.rng().Fill(a + kCounters, kCounterBytes);
            std::memcpy(b + kCounters, a + kCounters, kCounterBytes);
            const std::int32_t ra = Original_MapEffects_SetMapSize(tableA, nullptr, sa, sb);
            const std::int32_t rb = setMapSize(tableB, nullptr, sa, sb);
            if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT_MEM_EQ(a + kCounters, b + kCounters, kCounterBytes)) {
                ctx.Fail(__FILE__, __LINE__, "  case %d: SetMapSize(%d, %d)", i, sa, sb);
                return;
            }
            continue;
        }
        ctx.rng().Fill(a, kRecordBytes);
        const std::int32_t start = ctx.rng().Range(-5, 1000);
        recordA->start_tick = static_cast<std::uint32_t>(start);
        recordA->duration = static_cast<std::int16_t>(ctx.rng().Chance(1, 10) ? -ctx.rng().Range(1, 50) : ctx.rng().Range(1, 200));
        for (std::int32_t* field : {&recordA->start_x, &recordA->start_y, &recordA->end_x, &recordA->end_y}) {
            *field = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32()) : ctx.rng().Range(-500, 20000);
        }
        recordA->arc = static_cast<std::int16_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Range(-100, 300));
        const std::int32_t tick = start + ctx.rng().Range(-10, 220);
        std::memcpy(b, a, kRecordBytes);
        // Outputs: separate slots, or aliasing record fields.
        static const std::size_t kSlots[] = {offsetof(MapEffect, list),    offsetof(MapEffect, previous),
                                             offsetof(MapEffect, start_tick), offsetof(MapEffect, start_x),
                                             offsetof(MapEffect, start_y), offsetof(MapEffect, end_x),
                                             offsetof(MapEffect, end_y),   offsetof(MapEffect, arc)};
        const std::size_t sx = kSlots[ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Below(8)];
        const std::size_t sy = kSlots[ctx.rng().Chance(3, 4) ? 1 : ctx.rng().Below(8)];
        Original_MapEffect_PositionAt(recordA, nullptr, tick, reinterpret_cast<std::int32_t*>(a + sx), reinterpret_cast<std::int32_t*>(a + sy));
        positionAt(recordB, nullptr, tick, reinterpret_cast<std::int32_t*>(b + sx), reinterpret_cast<std::int32_t*>(b + sy));
        if (!S3RE_EXPECT_MEM_EQ(a, b, kRecordBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: PositionAt(%d) slots %u %u", i, tick,
                     static_cast<unsigned>(sx), static_cast<unsigned>(sy));
            return;
        }
    }
}

// Wrong: the hop is one pixel too high.
void __fastcall WrongPositionAt(MapEffect* self, void* edx, std::int32_t tick, std::int32_t* x, std::int32_t* y) {
    MapEffect_PositionAt(self, edx, tick, x, y);
    if (self->arc != 0) {
        *y -= 1;
    }
}

}  // namespace

S3RE_TEST(MapEffect_Math_MatchesOriginal) {
    CompareEffectMath(ctx, &MapEffect_PositionAt, &MapEffects_SetMapSize, 50000);
}

S3RE_TEST(MapEffect_Math_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEffectMath(probe, &WrongPositionAt, &MapEffects_SetMapSize, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// Batch 26: adding effects to the table and the functions that create them
// ---------------------------------------------------------------------------
//
// The effect table (ECX; world+0x14d2bc8): records 1..4999 at this + slot *
// 0x4c (slot 0 is never used; a record is free while its dword +0 is 0),
// list heads at +0x5cc60 + cell * 4 (one list per 256x256-pixel block of
// the map, cell = by * blocksX + bx + 1), the number of slots in use so far
// at +0x668a0, blocks per row at +0x668a4 (see MapEffects_SetMapSize).
//
// MapEffects_FindFreeSlot (0x00545ee0): () the first slot 1..count whose
//   record's dword +0 is 0; otherwise count + 1 if that is below 5000
//   (count is increased), else DebugLog "EFFECT: Effect overflow" and 0.
//   EAX = the slot.
// MapEffects_CellOfPosition (0x00545eb0): (x, y) ret 8: (y / 256) *
//   blocksX + x / 256 + 1 (signed divisions rounding towards 0; 32-bit
//   product). EAX = the cell.
// MapEffects_LinkIntoCell (0x00545df0): (cell, slot) ret 8: SetLinks(cell,
//   0, head) on the record (head = the cell's list head), then, if the
//   record's next (+0x48) is not 0, SetLinks(cell, slot, -1) on that next
//   record; head = slot. EAX = 0 when there was no next record, else -1
//   (SetLinks' result).
// MapEffects_Add (0x005445e0): (const MapEffect* effect) ret 4: slot =
//   FindFreeSlot(); if not 0: copies the 0x4c bytes into the slot, then
//   LinkIntoCell(CellOfPosition(effect+0x10, effect+0x14), slot) (the
//   effect's end position, read from the source). EAX = 0 without a slot,
//   else LinkIntoCell's result.
//
// The "spawn" functions below (our names; thiscall on the table, all in
// Volker\LogicInit.cpp's range) build records with the make functions in a
// 0x4c-byte stack temporary and add them; a<i> are the stack arguments,
// "edx" is what each passes as the make function's EDX (the record's start
// tick). EAX = the last Add's result unless noted.
//   0x005450d0 (5 args): Make_543ef0(edx a1; 0x23, 0xa, a2, a3, a4, a5,
//              0x30001, 2).
//   0x00545120 (5 args, a4/a5 unused): Make_543f70(edx a1; a2, a3, 0x18).
//   0x00544f20 (5 args): Make_543930(edx a1; 0x10, a2, a3, a4, a5 + 5,
//              0xd0026), then Make_543f70(edx a1; a2, a3, 0x5d).
//   0x00545070 (5 args): the same with 0x63 instead of 0x5d.
//   0x00544dc0 (7 args): with k = a2 (0..3; the original indexes stack
//              arrays with it, other values read its stack):
//              Make_543ce0(edx 0; a3, a1 + T0[k], T1[k], a4, a5, a6, a7,
//              G[k], 0xc), T0 = S3.EXE 0x65a19c, T1 = 0x65a1ac, G =
//              {0xd000f, 0x170016, 0x210015, 0x2b0014}.
//   0x00545860 (9 args, a4..a9 unused): Make_543f70(edx a1 + 0x14 * n;
//              a2, a3, 0x70) for n = 0, 1, 2. EAX = 0x3c.
//   0x00544e70 (7 args): if the low byte of a7 is 3: Make_5439a0(edx a1;
//              0x16, a2, a3, a4, a5, 0x2a0012, a6), then Make_543f70(edx
//              a1; a2, a3, 0x71); otherwise 0xc0012 and 0x23.
//   0x00544f80 (5 args): Make_543930(edx a1 + d; 9, a2 + dx, a3 + dy, a4,
//              a5, 0xd0031) for (d, dx, dy) = (0, 10, -5), (7, -2, 20),
//              (1, 0, 0), (5, 20, 5), then Make_543f70(edx a1; a2, a3,
//              0x5d).
// The records' five padding bytes come from the make functions' stack
// temporaries (see the make functions), in the original as in ours.

using FindFreeSlotFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx);
using CellOfPositionFn = std::int32_t(__fastcall*)(MapEffectTable* self, void* edx, std::int32_t x, std::int32_t y);
using LinkIntoCellFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t cell, std::uint32_t slot);
using AddEffectFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, const MapEffect* effect);
using Spawn5Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                            std::uint32_t a4, std::uint32_t a5);
using Spawn7Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                            std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7);
using Spawn9Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                            std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                            std::uint32_t a8, std::uint32_t a9);

std::uint32_t __fastcall MapEffects_FindFreeSlot(MapEffectTable* self, void* edx);
std::int32_t __fastcall MapEffects_CellOfPosition(MapEffectTable* self, void* edx, std::int32_t x, std::int32_t y);
std::uint32_t __fastcall MapEffects_LinkIntoCell(MapEffectTable* self, void* edx, std::uint32_t cell, std::uint32_t slot);
std::uint32_t __fastcall MapEffects_Add(MapEffectTable* self, void* edx, const MapEffect* effect);

namespace {

constexpr std::size_t kEffectSlots = sizeof(MapEffectTable::records) / sizeof(MapEffect);  // 5000

void* Edx(std::uint32_t value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

}  // namespace

S3RE_HOOK(0x00545ee0, MapEffects_FindFreeSlot, FindFreeSlotFn, Original_MapEffects_FindFreeSlot,
          MapEffects_FindFreeSlot);

// Purpose: Finds a free record in the map effect table (reusing freed ones
// first); 0 when all 4999 are taken.
std::uint32_t __fastcall MapEffects_FindFreeSlot(MapEffectTable* self, void* /*edx*/) {
    const std::int32_t used = self->slots_used;
    std::int32_t slot = 1;
    for (; slot <= used; ++slot) {
        if (self->records[slot].start_tick == 0) {
            return static_cast<std::uint32_t>(slot);
        }
    }
    slot = used + 1;
    if (slot < static_cast<std::int32_t>(kEffectSlots)) {
        self->slots_used = slot;
        return static_cast<std::uint32_t>(slot);
    }
    GameDebugLog(texts::kEffectOverflow);
    return 0;
}

S3RE_HOOK(0x00545eb0, MapEffects_CellOfPosition, CellOfPositionFn, Original_MapEffects_CellOfPosition,
          MapEffects_CellOfPosition);

// Purpose: The 256x256-pixel block of the map that a position (in pixels)
// falls into; each block keeps a list of the effects in it (probably so the
// drawing only visits the effects on screen).
std::int32_t __fastcall MapEffects_CellOfPosition(MapEffectTable* self, void* /*edx*/, std::int32_t x, std::int32_t y) {
    const std::uint32_t row = static_cast<std::uint32_t>(y / 256) * static_cast<std::uint32_t>(self->blocks_x);
    return static_cast<std::int32_t>(row + static_cast<std::uint32_t>(x / 256) + 1);
}

S3RE_HOOK(0x00545df0, MapEffects_LinkIntoCell, LinkIntoCellFn, Original_MapEffects_LinkIntoCell,
          MapEffects_LinkIntoCell);

// Purpose: Puts an effect record at the front of its block's list.
// Slots and cells are not range-checked (as in the original): indexing with
// any 32-bit value gives the original's 32-bit address arithmetic.
std::uint32_t __fastcall MapEffects_LinkIntoCell(MapEffectTable* self, void* /*edx*/, std::uint32_t cell, std::uint32_t slot) {
    MapEffect* record = &self->records[slot];
    MapEffect_SetLinks(record, nullptr, cell, 0, self->cell_heads[cell]);
    std::uint32_t result = record->next;
    if (result != 0) {
        result = MapEffect_SetLinks(&self->records[result], nullptr, cell, slot, 0xffffffffu);
    }
    self->cell_heads[cell] = slot;
    return result;
}

S3RE_HOOK(0x005445e0, MapEffects_Add, AddEffectFn, Original_MapEffects_Add, MapEffects_Add);

// Purpose: Adds a map effect (probably a visual effect such as smoke or a
// spell) to the table and to the list of its map block; dropped when the
// table is full.
std::uint32_t __fastcall MapEffects_Add(MapEffectTable* self, void* /*edx*/, const MapEffect* effect) {
    const std::uint32_t slot = MapEffects_FindFreeSlot(self, nullptr);
    if (slot == 0) {
        return 0;
    }
    std::memcpy(&self->records[slot], effect, sizeof(MapEffect));
    const std::int32_t cell = MapEffects_CellOfPosition(self, nullptr, effect->end_x, effect->end_y);
    return MapEffects_LinkIntoCell(self, nullptr, static_cast<std::uint32_t>(cell), slot);
}

std::uint32_t __fastcall MapEffects_Spawn_5450d0(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
std::uint32_t __fastcall MapEffects_Spawn_545120(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
std::uint32_t __fastcall MapEffects_Spawn_544f20(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
std::uint32_t __fastcall MapEffects_Spawn_545070(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
std::uint32_t __fastcall MapEffects_Spawn_544f80(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
std::uint32_t __fastcall MapEffects_Spawn_544dc0(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7);
std::uint32_t __fastcall MapEffects_Spawn_544e70(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7);
std::uint32_t __fastcall MapEffects_Spawn_545860(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7, std::uint32_t a8,
                                                 std::uint32_t a9);

S3RE_HOOK(0x005450d0, MapEffects_Spawn_5450d0, Spawn5Fn, Original_MapEffects_Spawn_5450d0, MapEffects_Spawn_5450d0);

// Purpose: Adds one map effect of a fixed kind (type 0x23, graphics
// 0x30001; meaning unknown).
std::uint32_t __fastcall MapEffects_Spawn_5450d0(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    MapEffect effect;
    MapEffect_Make_543ef0(&effect, Edx(a1), 0x23, 0xa, a2, a3, a4, a5, 0x30001, 2);
    return MapEffects_Add(self, nullptr, &effect);
}

S3RE_HOOK(0x00545120, MapEffects_Spawn_545120, Spawn5Fn, Original_MapEffects_Spawn_545120, MapEffects_Spawn_545120);

// Purpose: Adds one stationary map effect of kind 0x18 at a position.
std::uint32_t __fastcall MapEffects_Spawn_545120(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t, std::uint32_t) {
    MapEffect effect;
    MapEffect_Make_543f70(&effect, Edx(a1), a2, a3, 0x18);
    return MapEffects_Add(self, nullptr, &effect);
}

namespace {

std::uint32_t SpawnPair(MapEffectTable* self, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                        std::uint32_t a5, std::uint32_t kind) {
    MapEffect effect;
    MapEffect_Make_543930(&effect, Edx(a1), 0x10, a2, a3, a4, a5 + 5, 0xd0026);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543f70(&effect, Edx(a1), a2, a3, kind);
    return MapEffects_Add(self, nullptr, &effect);
}

}  // namespace

S3RE_HOOK(0x00544f20, MapEffects_Spawn_544f20, Spawn5Fn, Original_MapEffects_Spawn_544f20, MapEffects_Spawn_544f20);

// Purpose: Adds a moving effect (graphics 0xd0026) plus a stationary one of
// kind 0x5d at the start (probably a projectile and its launch effect).
std::uint32_t __fastcall MapEffects_Spawn_544f20(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    return SpawnPair(self, a1, a2, a3, a4, a5, 0x5d);
}

S3RE_HOOK(0x00545070, MapEffects_Spawn_545070, Spawn5Fn, Original_MapEffects_Spawn_545070, MapEffects_Spawn_545070);

// Purpose: The same pair of effects as 0x544f20 with kind 0x63 for the
// stationary one.
std::uint32_t __fastcall MapEffects_Spawn_545070(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    return SpawnPair(self, a1, a2, a3, a4, a5, 0x63);
}

S3RE_HOOK(0x00544f80, MapEffects_Spawn_544f80, Spawn5Fn, Original_MapEffects_Spawn_544f80, MapEffects_Spawn_544f80);

// Purpose: Adds four staggered effects (graphics 0xd0031) around a position
// plus a stationary one of kind 0x5d (probably a burst of sparks or smoke).
std::uint32_t __fastcall MapEffects_Spawn_544f80(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5) {
    static constexpr std::uint32_t kSteps[4][3] = {
        {0, 10, 0xfffffffbu}, {7, 0xfffffffeu, 20}, {1, 0, 0}, {5, 20, 5}};
    MapEffect effect;
    for (const auto& step : kSteps) {
        MapEffect_Make_543930(&effect, Edx(a1 + step[0]), 9, a2 + step[1], a3 + step[2], a4, a5, 0xd0031);
        MapEffects_Add(self, nullptr, &effect);
    }
    MapEffect_Make_543f70(&effect, Edx(a1), a2, a3, 0x5d);
    return MapEffects_Add(self, nullptr, &effect);
}

S3RE_HOOK(0x00544dc0, MapEffects_Spawn_544dc0, Spawn7Fn, Original_MapEffects_Spawn_544dc0, MapEffects_Spawn_544dc0);

// Purpose: Adds one effect whose graphics and offsets depend on a variant
// 0..3 (probably a direction or a race).
std::uint32_t __fastcall MapEffects_Spawn_544dc0(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7) {
    static constexpr std::uint32_t kGraphics[4] = {0xd000f, 0x170016, 0x210015, 0x2b0014};
    const std::uint32_t* offsets0 = g_spawnOffsetsX;
    const std::uint32_t* offsets1 = g_spawnOffsetsY;
    MapEffect effect;
    MapEffect_Make_543ce0(&effect, nullptr, a3, offsets0[a2] + a1, offsets1[a2], a4, a5, a6, a7, kGraphics[a2], 0xc);
    return MapEffects_Add(self, nullptr, &effect);
}

S3RE_HOOK(0x00544e70, MapEffects_Spawn_544e70, Spawn7Fn, Original_MapEffects_Spawn_544e70, MapEffects_Spawn_544e70);

// Purpose: Adds a moving effect of kind 0x16 and a stationary one; variant
// 3 uses other graphics (0x2a0012, kind 0x71) than the rest (0xc0012, 0x23).
std::uint32_t __fastcall MapEffects_Spawn_544e70(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7) {
    const bool three = static_cast<std::uint8_t>(a7) == 3;
    MapEffect effect;
    MapEffect_Make_5439a0(&effect, Edx(a1), 0x16, a2, a3, a4, a5, three ? 0x2a0012u : 0xc0012u, a6);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543f70(&effect, Edx(a1), a2, a3, three ? 0x71u : 0x23u);
    return MapEffects_Add(self, nullptr, &effect);
}

S3RE_HOOK(0x00545860, MapEffects_Spawn_545860, Spawn9Fn, Original_MapEffects_Spawn_545860, MapEffects_Spawn_545860);

// Purpose: Adds three stationary effects of kind 0x70 at one position,
// starting 0x14 ticks apart (probably a repeating animation). Returns 0x3c
// (probably the total duration).
std::uint32_t __fastcall MapEffects_Spawn_545860(MapEffectTable* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t, std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t, std::uint32_t) {
    MapEffect effect;
    for (std::uint32_t n = 0; n < 3; ++n) {
        MapEffect_Make_543f70(&effect, Edx(a1 + 0x14 * n), a2, a3, 0x70);
        MapEffects_Add(self, nullptr, &effect);
    }
    return 0x3c;
}

namespace {

// Tests: two effect tables (0x668c0 bytes) with identical random contents,
// made consistent per case: a slot count (mostly small, sometimes near the
// 4999 limit), blocks per row 1..40, valid list heads and next links, some
// free records. Positions and coordinates stay small so every cell is a
// real list head. The records' padding bytes (written by the make
// functions from their stack temporaries) are not compared for the spawn
// functions; a mutation test checks that a neighbouring byte still is.

constexpr std::size_t kTableBytes = sizeof(MapEffectTable) + 4;  // plus 4 guard bytes
constexpr std::size_t kTestCells = 1400;

struct AddFunctions {
    FindFreeSlotFn findFreeSlot;
    CellOfPositionFn cellOfPosition;
    LinkIntoCellFn linkIntoCell;
    AddEffectFn add;
    Spawn5Fn spawn5450d0;
    Spawn5Fn spawn545120;
    Spawn5Fn spawn544f20;
    Spawn5Fn spawn545070;
    Spawn5Fn spawn544f80;
    Spawn7Fn spawn544dc0;
    Spawn7Fn spawn544e70;
    Spawn9Fn spawn545860;
};

AddFunctions OriginalAdd() {
    return {Original_MapEffects_FindFreeSlot, Original_MapEffects_CellOfPosition, Original_MapEffects_LinkIntoCell,
            Original_MapEffects_Add,          Original_MapEffects_Spawn_5450d0,  Original_MapEffects_Spawn_545120,
            Original_MapEffects_Spawn_544f20, Original_MapEffects_Spawn_545070,  Original_MapEffects_Spawn_544f80,
            Original_MapEffects_Spawn_544dc0, Original_MapEffects_Spawn_544e70,  Original_MapEffects_Spawn_545860};
}

AddFunctions OurAdd() {
    return {&MapEffects_FindFreeSlot, &MapEffects_CellOfPosition, &MapEffects_LinkIntoCell,
            &MapEffects_Add,          &MapEffects_Spawn_5450d0,  &MapEffects_Spawn_545120,
            &MapEffects_Spawn_544f20, &MapEffects_Spawn_545070,  &MapEffects_Spawn_544f80,
            &MapEffects_Spawn_544dc0, &MapEffects_Spawn_544e70,  &MapEffects_Spawn_545860};
}

std::uint32_t RunAdd(const AddFunctions& f, int op, MapEffectTable* table, const std::uint32_t* a,
                     const MapEffect* effect) {
    switch (op) {
        case 0: return f.findFreeSlot(table, nullptr);
        case 1: return static_cast<std::uint32_t>(f.cellOfPosition(table, nullptr, static_cast<std::int32_t>(a[0]),
                                                                   static_cast<std::int32_t>(a[1])));
        case 2: return f.linkIntoCell(table, nullptr, a[0] % kTestCells, 1 + a[1] % 4999);
        case 3: return f.add(table, nullptr, effect);
        case 4: return f.spawn5450d0(table, nullptr, a[0], a[1], a[2], a[3], a[4]);
        case 5: return f.spawn545120(table, nullptr, a[0], a[1], a[2], a[3], a[4]);
        case 6: return f.spawn544f20(table, nullptr, a[0], a[1], a[2], a[3], a[4]);
        case 7: return f.spawn545070(table, nullptr, a[0], a[1], a[2], a[3], a[4]);
        case 8: return f.spawn544f80(table, nullptr, a[0], a[1], a[2], a[3], a[4]);
        case 9: return f.spawn544dc0(table, nullptr, a[0], a[1] & 3, a[2], a[3], a[4], a[5], a[6]);
        case 10: return f.spawn544e70(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        default: return f.spawn545860(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    }
}

void CompareAdd(test::Context& ctx, const AddFunctions& candidate, int cases) {
    const AddFunctions original = OriginalAdd();
    std::vector<std::uint8_t> a(kTableBytes), b(kTableBytes);
    ctx.rng().Fill(a.data(), a.size());
    auto* const table = reinterpret_cast<MapEffectTable*>(a.data());
    MapEffect effect;
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(12));
        const bool full = ctx.rng().Chance(1, 12);
        const std::uint32_t used = full ? 4985 + ctx.rng().Below(15) : ctx.rng().Below(60);
        table->slots_used = static_cast<std::int32_t>(used);
        table->blocks_x = static_cast<std::int32_t>(1 + ctx.rng().Below(40));
        const std::uint32_t last = std::min<std::uint32_t>(used + 12, 4999);
        for (std::uint32_t slot = 1; slot <= last; ++slot) {
            MapEffect& record = table->records[slot];
            const bool free = !full && ctx.rng().Chance(1, 6);
            record.start_tick = free ? 0 : 1 + ctx.rng().Below(0xffff);
            record.next = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(last + 1);
        }
        if (full && ctx.rng().Chance(1, 2)) {
            table->records[1 + ctx.rng().Below(used)].start_tick = 0;
        }
        for (std::uint32_t cell = 0; cell < kTestCells; ++cell) {
            const std::uint32_t head = ctx.rng().Chance(1, 20) ? 0xffffffffu : ctx.rng().Below(last + 1);
            table->cell_heads[cell] = head;
        }
        std::uint32_t args[9];
        for (auto& arg : args) {
            arg = ctx.rng().Below(7000);
        }
        args[0] = ctx.rng().Next32();  // the start tick
        if (op == 1) {
            args[0] = ctx.rng().Next32();
            args[1] = ctx.rng().Next32();
        }
        ctx.rng().Fill(&effect, sizeof(effect));
        effect.end_x = static_cast<std::int32_t>(ctx.rng().Below(7000));
        effect.end_y = static_cast<std::int32_t>(ctx.rng().Below(7000));
        effect.next = ctx.rng().Below(last + 1);
        if (op == 10 && ctx.rng().Chance(1, 2)) {
            args[6] = (ctx.rng().Next32() & ~0xffu) | 3;
        }
        b = a;
        const std::uint32_t ra = RunAdd(original, op, table, args, &effect);
        const std::uint32_t rb = RunAdd(candidate, op, reinterpret_cast<MapEffectTable*>(b.data()), args, &effect);
        if (op >= 4) {
            for (std::uint32_t slot = 1; slot < kEffectSlots; ++slot) {
                for (const std::size_t padding : kPaddingBytes) {
                    const std::size_t at = OffsetIn(table, &table->records[slot]) + padding;
                    b[at] = a[at];
                }
            }
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, used %u", i, op, used);
            return;
        }
    }
}

// Wrong: the variant-3 graphics of 0x544e70 differ only in the bytes
// +0x34..+0x37 next to the padding (+0x3b is padding, +0x38 is not).
std::uint32_t __fastcall WrongSpawn544e70(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                                          std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7) {
    return MapEffects_Spawn_544e70(self, edx, a1, a2, a3, a4, a5, a6 ^ 1, a7);
}

std::uint32_t __fastcall WrongFindFreeSlot(MapEffectTable* self, void* edx) {
    // Wrong: allows the 5000th slot.
    const std::int32_t used = self->slots_used;
    if (used == 4999) {
        bool any = false;
        for (std::uint32_t slot = 1; slot <= 4999 && !any; ++slot) {
            any = self->records[slot].start_tick == 0;
        }
        if (!any) {
            self->slots_used = 5000;
            return 5000;
        }
    }
    return MapEffects_FindFreeSlot(self, edx);
}

}  // namespace

S3RE_TEST(MapEffects_Add_MatchOriginal) {
    CompareAdd(ctx, OurAdd(), 3000);
}

S3RE_TEST(MapEffects_Add_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        AddFunctions wrong = OurAdd();
        wrong.spawn544e70 = &WrongSpawn544e70;
        CompareAdd(probe, wrong, 400);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        AddFunctions wrong = OurAdd();
        wrong.findFreeSlot = &WrongFindFreeSlot;
        CompareAdd(probe, wrong, 1500);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
