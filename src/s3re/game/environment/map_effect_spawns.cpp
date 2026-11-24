// Map effects made by settler jobs and soldiers (UpdateSettlers callees,
// package B). The effect table and the make functions are in
// map_effects.cpp; this file adds the remaining spawn functions, the record
// reset and the removal of effects.
//
// MapEffect::Reset (0x00543fd0): thiscall on a record: like the default
//   constructor, but also +0x30 = 0, +0x39 = 0xff and the list links
//   +0x40..+0x48 = 0. EAX = 0 (callers ignore it). ret.
// MapEffects::Unlink (0x00545e40): (slot): takes the record out of its
//   cell list: previous record's next (SetLinks(list, -1, next)) or the
//   cell head = next; next record's previous (SetLinks(list, previous,
//   -1)); then SetLinks(0, 0, 0) on the record. EAX = 0. ret 4.
// MapEffects::RemoveMatching (0x00545c80): (kind, id, x, y): removes
//   (Unlink + Reset) every effect whose byte +0x3a equals `kind` (full
//   dword compare) and whose word +0x3c, sign-extended, equals `id`: only
//   in the cell list of (x, y) (CellOfPosition), or, if x or y is -1, in
//   all records 0..slots used (record 0 included). Caller CrushBuilding
//   0x4d9250. EAX not used. ret 0x10.
// The spawn functions are thiscall on the table (a1 = the start tick, then
// positions in pixels and four values passed through; see the code for the
// exact make-function arguments, read off the push sequences):
//   0x545150 (SpawnArrow; UpdateSoldier), 0x5453f0 (SpawnProjectile1):
//     a flight from (a2, a3) to (a6, a7) in three stages, with a direction
//     graphics id per octant of (dx, dy); 0x5453f0 adds start/end effects.
//   0x5458d0 (SpawnProjectile0), 0x545730 (SpawnProjectile2): other flight
//     shapes (one stage for 0x545730) with start/end effects.
//   The flight time n = _ftol(sqrt(dx*dx + dy*dy) * K) with K a double in
//   S3.EXE's data (0x65a1c0, 0x65a1c8, 0x65a1d0), and an arc height from
//   (dx*dx + 4*dy*dy) / 12000 (or / 1500). The x87 code runs at the
//   thread's default control word (53-bit precision, round to nearest),
//   so plain double arithmetic in the same order gives the same results;
//   _ftol truncates (via a 64-bit integer: NaN and huge values give 0 in
//   EAX). EAX: the flight time plus the tick offset (callers use it).
//   0x545b90 (SpawnProjectile; WalkSettler): (a1, kind, b3..b10) calls
//     0x5458d0 / 0x5453f0 / 0x545730 / Spawn_545860 for kind 0..3 with
//     (a1, b3..b10) and returns their EAX; 0 for other kinds.
//   0x544630, 0x5449f0: effects with offsets from small tables the
//     original builds on its stack, indexed by a3 + 2 * a2 (0..7) or
//     a3 + 4 * a2 (0..15); other indices read other stack data and are not
//     reproduced (callers pass a race 0..3 and a small variant).

#include "game/layout.h"
#include "game/raw.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using layout::MapEffect;
using layout::MapEffectTable;

// map_effects.cpp
std::uint32_t __fastcall MapEffect_SetLinks(MapEffect* self, void* edx, std::uint32_t list, std::uint32_t previous,
                                            std::uint32_t next);
std::int32_t __fastcall MapEffects_CellOfPosition(MapEffectTable* self, void* edx, std::int32_t x, std::int32_t y);
std::uint32_t __fastcall MapEffects_Add(MapEffectTable* self, void* edx, const MapEffect* effect);
std::uint32_t __fastcall MapEffects_Spawn_545860(MapEffectTable* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                 std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                 std::uint32_t a6, std::uint32_t a7, std::uint32_t a8,
                                                 std::uint32_t a9);
void* __fastcall MapEffect_Make_543930(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543a10(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543aa0(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543b40(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543bc0(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543c40(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543d60(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543df0(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t, std::uint32_t);
void* __fastcall MapEffect_Make_543f70(MapEffect* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t);

namespace {

using U = std::uint32_t;

// MapEffect_SetLinks: -1 keeps a link as it is.
constexpr U kKeepLink = 0xffffffffu;
// RemoveMatching: x or y -1 means "anywhere" (all records are searched).
constexpr U kAnyPosition = 0xffffffffu;

// A graphics id: GFX file << 16 | frame.
constexpr U Graphics(U file, U frame) {
    return file << 16 | frame;
}

// The graphics of the effects made here.
constexpr U kArrowGraphics = Graphics(1, 100);       // 0x10064, + 0..5 by direction (0x545150)
constexpr U kProjectile1Graphics = Graphics(22, 8);  // 0x160008, + 0..5 by direction (0x5453f0)
constexpr U kProjectile0Graphics = Graphics(12, 8);  // 0xc0008 (0x5458d0)
constexpr U kProjectile2Graphics = Graphics(32, 8);  // 0x200008 (0x545730)
constexpr U kImpactGraphics = Graphics(13, 49);      // 0xd0031, where a projectile lands
constexpr U kSparkGraphics = Graphics(13, 42);       // 0xd002a, sparks / chips of a worker's tool
constexpr U kToolGraphics = Graphics(13, 43);        // 0xd002b, tool effect of 0x5449f0, variants 0 and 1
static_assert(kArrowGraphics == 0x10064 && kProjectile1Graphics == 0x160008 && kProjectile0Graphics == 0xc0008 &&
              kProjectile2Graphics == 0x200008 && kImpactGraphics == 0xd0031 && kSparkGraphics == 0xd002a &&
              kToolGraphics == 0xd002b);

// Arc height = (dx*dx + 4*dy*dy) / divisor: a flat arc for arrows and most
// projectiles, a high one for 0x5458d0.
constexpr std::int32_t kFlatArcDivisor = 12000;
constexpr std::int32_t kHighArcDivisor = 1500;

void* Edx(U value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

// Signed division by 256 rounding towards zero (cdq; and edx, 0xff; add;
// sar 8) of a 32-bit product that wraps like the original's.
U Div256(U product) {
    return static_cast<U>(static_cast<std::int32_t>(product) / 256);
}

// |v| as the original's cdq/xor/sub (INT_MIN stays INT_MIN).
U Abs(U v) {
    return static_cast<std::int32_t>(v) < 0 ? 0u - v : v;
}

// The CRT's _ftol (0x61fe98): truncation through a 64-bit integer; EAX is
// the low dword. NaN and values outside the int64 range give the "integer
// indefinite" 0x8000000000000000, i.e. 0 in EAX.
U Ftol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<U>(static_cast<std::int64_t>(value));
}

// _ftol(sqrt((double)(int32)squares) * factor), factor read from S3.EXE's data.
U FlightTime(U squares, const double& factorInData) {
    const double factor = factorInData;
    return Ftol(std::sqrt(static_cast<double>(static_cast<std::int32_t>(squares))) * factor);
}

// The shared geometry of the flights from (a2, a3) to (a6, a7).
struct Flight {
    U dx, dy;
    U squares;   // dx*dx + dy*dy
    U arc;       // (dx*dx + 4*dy*dy) / divisor (signed)
    U time;      // FlightTime
};

Flight MakeFlight(U a2, U a3, U a6, U a7, std::int32_t divisor, const double& factor) {
    Flight f{};
    f.dx = a6 - a2;
    f.dy = a7 - a3;
    f.squares = f.dx * f.dx + f.dy * f.dy;
    f.arc = static_cast<U>(static_cast<std::int32_t>(f.dx * f.dx + 4 * (f.dy * f.dy)) / divisor);
    f.time = FlightTime(f.squares, factor);
    return f;
}

// The graphics id of a three-stage flight by direction: `base` + 0..5
// (kArrowGraphics.. for arrows, kProjectile1Graphics.. for 0x5453f0).
U DirectionGraphics(const Flight& f, U base) {
    const U adx = Abs(f.dx), twice = Abs(f.dy) + Abs(f.dy);
    const bool steep = static_cast<std::int32_t>(adx) <= static_cast<std::int32_t>(twice);
    const bool down = static_cast<std::int32_t>(f.dy) > 0;
    if (static_cast<std::int32_t>(f.dx) >= 0) {
        return !steep ? base + 4 : down ? base + 5 : base + 3;
    }
    return !steep ? base + 1 : down ? base + 0 : base + 2;
}

// The three stages of an arrow-like flight (0x545150, 0x5453f0): the first
// quarter of the way, the middle half along the arc, the last quarter (in
// 256ths: 64, 128, 192). EAX = the last Add's.
U ThreeStages(MapEffectTable* self, U tick, const Flight& f, U g, U a2, U a3, U a4, U a5, U a6, U a7, U a8, U a9) {
    const U p1x = Div256(f.dx * 64) + a2, p2x = Div256(f.dx * 192) + a2;
    const U lift = Div256(f.arc * 64);
    const U p1y = Div256(f.dy * 64) - lift + a3, p2y = Div256(f.dy * 192) - lift + a3;
    const U t1 = Div256(f.time * 64);
    MapEffect effect;
    MapEffect_Make_543a10(&effect, Edx(tick), t1, a2, a3, p1x, p1y, a4, a5, g, 0, 1);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543aa0(&effect, Edx(tick + t1), Div256(f.time * 128), p1x, p1y, p2x, p2y, Div256(f.arc * 196), a4,
                          a5, a8, a9, g, 1, 1);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543a10(&effect, Edx(tick + Div256(f.time * 192)), Div256(f.time * 50), p2x, p2y, a6, a7, a8, a9, g,
                          2, 1);
    return MapEffects_Add(self, nullptr, &effect);
}

// The start, end and impact effects after a flight.
void EndEffects(MapEffectTable* self, U tick, U time, U startKind, U endKind, U a2, U a3, U a6, U a7, U a8, U a9) {
    MapEffect effect;
    MapEffect_Make_543f70(&effect, Edx(tick), a2, a3, startKind);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543f70(&effect, Edx(tick + time), a6, a7, endKind);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543930(&effect, Edx(tick + time), 9, a6, a7, a8, a9 + 3, kImpactGraphics);
    MapEffects_Add(self, nullptr, &effect);
}


}  // namespace

using ResetFn = std::uint32_t(__fastcall*)(MapEffect* self, void* edx);
using UnlinkFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t slot);
using RemoveFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t kind, std::uint32_t id,
                                            std::uint32_t x, std::uint32_t y);
using Spawn8Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, U, U, U, U, U, U, U, U);
using Spawn9Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, U, U, U, U, U, U, U, U, U);
using Spawn10Fn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, U, U, U, U, U, U, U, U, U, U);

// Purpose: Empties a map effect record when its effect is removed (all fields back to "unused").
std::uint32_t __fastcall MapEffect_Reset(MapEffect* self, void*) {
    self->start_tick = 0;
    self->duration = 0;
    self->unk06 = 0;
    self->source_kind = 0;
    self->source_id = 0;
    self->start_x = 0;
    self->start_y = 0;
    self->end_x = 0;
    self->end_y = 0;
    self->arc = 0;
    self->unk18 = 0;
    self->unk1c = 0;
    self->unk20 = 0;
    self->unk24 = 0;
    self->unk2c = 0xffffffffu;
    self->unk30 = 0;
    self->unk32 = 0;
    self->unk38 = 0xff;
    self->unk39 = 0xff;
    self->unk34 = 0xffffffffu;
    self->list = 0;
    self->previous = 0;
    self->next = 0;
    return 0;
}

// Purpose: Takes a map effect out of the list of its map block (before the record is freed).
std::uint32_t __fastcall MapEffects_Unlink(MapEffectTable* self, void*, std::uint32_t slot) {
    MapEffect& record = self->records[slot];
    const U previous = record.previous;
    const U next = record.next;
    const U list = record.list;
    if (previous != 0) {
        MapEffect_SetLinks(&self->records[previous], nullptr, list, kKeepLink, next);
    } else {
        self->cell_heads[list] = next;
    }
    if (record.next != 0) {
        // The original re-reads the record's links here.
        MapEffect_SetLinks(&self->records[record.next], nullptr, list, record.previous, kKeepLink);
    }
    return MapEffect_SetLinks(&record, nullptr, 0, 0, 0);
}

// Purpose: Removes the map effects that belong to something that goes away (matched by kind and id,
// probably a building being crushed), looking only near its position when one is given.
std::uint32_t __fastcall MapEffects_RemoveMatching(MapEffectTable* self, void*, std::uint32_t kind, std::uint32_t id,
                                                   std::uint32_t x, std::uint32_t y) {
    auto matches = [&](const MapEffect& r) {
        return r.source_kind == kind && static_cast<U>(static_cast<std::int32_t>(static_cast<std::int16_t>(r.source_id))) == id;
    };
    if (x != kAnyPosition && y != kAnyPosition) {
        const std::int32_t cell =
            MapEffects_CellOfPosition(self, nullptr, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
        U slot = self->cell_heads[cell];
        while (slot != 0) {
            MapEffect& record = self->records[slot];
            const U next = record.next;
            if (matches(record)) {
                MapEffects_Unlink(self, nullptr, slot);
                MapEffect_Reset(&record, nullptr);
            }
            slot = next;
        }
        return 0;
    }
    for (std::int32_t slot = 0; slot <= self->slots_used; ++slot) {
        MapEffect& record = self->records[slot];
        if (matches(record)) {
            MapEffects_Unlink(self, nullptr, static_cast<U>(slot));
            MapEffect_Reset(&record, nullptr);
        }
    }
    return static_cast<U>(self->slots_used);
}

// Purpose: Shows an arrow flying from a bowman to its target (three flight stages, the graphics
// chosen by direction); returns how long the flight takes plus 10 ticks.
std::uint32_t __fastcall MapEffects_SpawnArrow(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6, U a7,
                                               U a8, U a9) {
    const U tick = a1 + 10;
    const Flight f = MakeFlight(a2, a3, a6, a7, kFlatArcDivisor, g_flightFactor05);
    return ThreeStages(self, tick, f, DirectionGraphics(f, kArrowGraphics), a2, a3, a4, a5, a6, a7, a8, a9);
}

// Purpose: Shows a thrown object (kProjectile1Graphics + direction) flying in three stages, with
// an effect where it starts and where it lands; returns the flight time plus 3 ticks.
std::uint32_t __fastcall MapEffects_SpawnProjectile1(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6,
                                                     U a7, U a8, U a9) {
    const U tick = a1 + 3;
    const Flight f = MakeFlight(a2, a3, a6, a7, kFlatArcDivisor, g_flightFactor05);
    ThreeStages(self, tick, f, DirectionGraphics(f, kProjectile1Graphics), a2, a3, a4, a5, a6, a7, a8, a9);
    EndEffects(self, tick, f.time, 85, 89, a2, a3, a6, a7, a8, a9);  // kinds 0x55, 0x59
    return f.time + 3;
}

// Purpose: Shows a thrown object (kProjectile0Graphics) on a high arc in three stages, with start
// and landing effects; returns the flight time plus 2 ticks.
std::uint32_t __fastcall MapEffects_SpawnProjectile0(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6,
                                                     U a7, U a8, U a9) {
    const U tick = a1 + 2;
    const Flight f = MakeFlight(a2, a3, a6, a7, kHighArcDivisor, g_flightFactor0);
    const U p1x = Div256(f.dx * 64) + a2, p2x = Div256(f.dx * 192) + a2;
    const U lift = Div256(f.arc * 160);
    const U p1y = Div256(f.dy * 64) - lift + a3, p2y = Div256(f.dy * 192) - lift + a3;
    const U t1 = Div256(f.time * 64);
    MapEffect effect;
    MapEffect_Make_543a10(&effect, Edx(tick), t1, a2, a3, p1x, p1y, a4, a5, kProjectile0Graphics, 0, 12);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543c40(&effect, Edx(tick + t1), Div256(f.time * 128), p1x, p1y, p2x, p2y, Div256(f.arc * 80), a4,
                          a5, a8, a9, kProjectile0Graphics, 12);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543a10(&effect, Edx(tick + Div256(f.time * 192)), t1, p2x, p2y, a6, a7, a8, a9,
                          kProjectile0Graphics, 0, 12);
    MapEffects_Add(self, nullptr, &effect);
    EndEffects(self, tick, f.time, 84, 88, a2, a3, a6, a7, a8, a9);  // kinds 0x54, 0x58
    return f.time + 2;
}

// Purpose: Shows a thrown object (kProjectile2Graphics) in one arc, with start and landing effects;
// returns the flight time plus 8 ticks.
std::uint32_t __fastcall MapEffects_SpawnProjectile2(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6,
                                                     U a7, U a8, U a9) {
    const U tick = a1 + 8;
    const Flight f = MakeFlight(a2, a3, a6, a7, kFlatArcDivisor, g_flightFactor2);
    MapEffect effect;
    MapEffect_Make_543c40(&effect, Edx(tick), f.time, a2, a3, a6, a7, f.arc, a4, a5, a8, a9, kProjectile2Graphics, 12);
    MapEffects_Add(self, nullptr, &effect);
    EndEffects(self, tick, f.time, 86, 90, a2, a3, a6, a7, a8, a9);  // kinds 0x56, 0x5a
    return f.time + 8;
}

// Purpose: Shows something a settler throws or shoots while walking (kind 0..3 picks the flight
// shape); returns how long it takes.
std::uint32_t __fastcall MapEffects_SpawnProjectile(MapEffectTable* self, void*, U a1, U kind, U b3, U b4, U b5, U b6,
                                                    U b7, U b8, U b9, U b10) {
    switch (kind) {
    case 0: return MapEffects_SpawnProjectile0(self, nullptr, a1, b3, b4, b5, b6, b7, b8, b9, b10);
    case 1: return MapEffects_SpawnProjectile1(self, nullptr, a1, b3, b4, b5, b6, b7, b8, b9, b10);
    case 2: return MapEffects_SpawnProjectile2(self, nullptr, a1, b3, b4, b5, b6, b7, b8, b9, b10);
    case 3: return MapEffects_Spawn_545860(self, nullptr, a1, b3, b4, b5, b6, b7, b8, b9, b10);
    default: return 0;
    }
}

namespace {

// The stack tables of 0x544630, indexed by a3 + 2 * a2 (0..7; a2 the race,
// a3 the variant). Times in ticks, offsets in pixels.
constexpr U kWorkGraphics[8] = {Graphics(13, 37), Graphics(13, 36), Graphics(23, 37), Graphics(23, 36),
                                Graphics(33, 39), Graphics(33, 38), Graphics(43, 40), Graphics(43, 39)};
static_assert(kWorkGraphics[0] == 0xd0025 && kWorkGraphics[3] == 0x170024 && kWorkGraphics[5] == 0x210026 &&
              kWorkGraphics[7] == 0x2b0027);
constexpr U kWorkFrames[8] = {25, 25, 25, 25, 25, 25, 25, 25};
constexpr U kWorkStart[8] = {50, 50, 70, 50, 50, 50, 50, 50};
constexpr U kWorkKinds[8] = {94, 88, 90, 110, 120, 102, 94, 88};
constexpr U kWorkSparkDelay[8] = {35, 38, 41, 25, 25, 36, 35, 38};
// Where the sparks fly, relative to the worker (x, y); added as wrapping
// 32-bit values like the original's.
constexpr std::int32_t kWorkOffsets[8][2] = {{-38, -70}, {-38, -72}, {-26, -39}, {-27, -42},
                                             {-31, -65}, {-41, -41}, {-24, -82}, {-32, -82}};

// The stack tables of 0x5449f0, indexed by a3 + 4 * a2 (0..15; a2 the race,
// a3 the variant 0..3, of which only 0..2 have values for races 1..3).
constexpr std::int32_t kToolOffsets[16][2] = {{-37, -14}, {-33, -14}, {-15, -39}, {-17, -37},
                                              {-27, -7},  {-34, -4},  {34, -17},  {0, 0},
                                              {-29, 2},   {-34, -1},  {-10, -20}, {0, 0},
                                              {-43, -17}, {-43, -13}, {36, -66},  {0, 0}};
constexpr U kToolDelay[16] = {45, 50, 120, 630, 45, 40, 120, 0, 45, 40, 120, 0, 45, 50, 120, 0};
constexpr U kToolStart[16] = {98, 46, 200, 80, 95, 15, 248, 0, 54, 88, 219, 0, 98, 46, 200, 0};

// The spark effect of both functions: 6 ticks appearing, a moving middle
// part, 6 ticks fading (frames 0, 6, 30 of kSparkGraphics).
constexpr U kSparkFadeTicks = 6;

U AddOffset(U base, std::int32_t offset) {
    return base + static_cast<U>(offset);
}

}  // namespace

// Purpose: Shows a worker's tool animation with sparks or chips flying off (graphics by race), e.g. a
// stonecutter or smith at work; the variant a3 picks one of two motions.
std::uint32_t __fastcall MapEffects_Spawn_544630(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6, U a7,
                                                 U a8) {
    const U i = a3 + 2 * a2;
    const U frames = kWorkFrames[i], start = kWorkStart[i], graphics = kWorkGraphics[i];
    MapEffect effect;
    MapEffect_Make_543b40(&effect, nullptr, a4, a1 + start, frames, a5, a6, a7, a8, graphics);
    MapEffects_Add(self, nullptr, &effect);
    const U second = a1 + start + frames;
    MapEffect_Make_543bc0(&effect, nullptr, a4, second, kWorkKinds[i], a5, a6, a7, a8, graphics, frames - 1, 1);
    MapEffects_Add(self, nullptr, &effect);
    const U x = AddOffset(a5, kWorkOffsets[i][0]), y = AddOffset(a6, kWorkOffsets[i][1]);
    const U delay = kWorkSparkDelay[i];
    const U spark = start - delay + a1;
    MapEffect_Make_543bc0(&effect, nullptr, a4, spark, kSparkFadeTicks, x, y, a7, a8, kSparkGraphics, 0, 6);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543df0(&effect, nullptr, a4, spark + kSparkFadeTicks, delay + frames - 2 * kSparkFadeTicks, 48, x,
                          y, a7, a8, kSparkGraphics, 6, 24);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543bc0(&effect, nullptr, a4, second - kSparkFadeTicks, kSparkFadeTicks, x, y, a7, a8,
                          kSparkGraphics, 30, 6);
    return MapEffects_Add(self, nullptr, &effect);
}

// Purpose: Shows a worker's tool effect (kToolGraphics, or sparks kSparkGraphics for variants 2
// and 3) at an offset that depends on the race and the variant.
std::uint32_t __fastcall MapEffects_Spawn_5449f0(MapEffectTable* self, void*, U a1, U a2, U a3, U a4, U a5, U a6, U a7,
                                                 U a8) {
    const U i = a3 + 4 * a2;
    const U x = AddOffset(a5, kToolOffsets[i][0]), y = AddOffset(a6, kToolOffsets[i][1]);
    MapEffect effect;
    if (static_cast<std::int32_t>(a3) < 2) {
        MapEffect_Make_543d60(&effect, nullptr, a4, a1 + kToolStart[i], kToolDelay[i], 80, x, y, a7, a8, kToolGraphics,
                              40);
        return MapEffects_Add(self, nullptr, &effect);
    }
    const U start = a1 + kToolStart[i];
    MapEffect_Make_543bc0(&effect, nullptr, a4, start, kSparkFadeTicks, x, y, a7, a8, kSparkGraphics, 0, 6);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543df0(&effect, nullptr, a4, start + kSparkFadeTicks, kToolDelay[i] - 2 * kSparkFadeTicks, 72, x,
                          y, a7, a8, kSparkGraphics, 6, 24);
    MapEffects_Add(self, nullptr, &effect);
    MapEffect_Make_543bc0(&effect, nullptr, a4, kToolStart[i] + kToolDelay[i] + a1 - kSparkFadeTicks, kSparkFadeTicks,
                          x, y, a7, a8, kSparkGraphics, 30, 6);
    return MapEffects_Add(self, nullptr, &effect);
}

S3RE_HOOK(0x00543fd0, MapEffect_Reset, ResetFn, Original_MapEffect_Reset, MapEffect_Reset);
S3RE_HOOK(0x00545e40, MapEffects_Unlink, UnlinkFn, Original_MapEffects_Unlink, MapEffects_Unlink);
S3RE_HOOK(0x00545c80, MapEffects_RemoveMatching, RemoveFn, Original_MapEffects_RemoveMatching,
          MapEffects_RemoveMatching);
S3RE_HOOK(0x00545150, MapEffects_SpawnArrow, Spawn9Fn, Original_MapEffects_SpawnArrow, MapEffects_SpawnArrow);
S3RE_HOOK(0x005453f0, MapEffects_SpawnProjectile1, Spawn9Fn, Original_MapEffects_SpawnProjectile1,
          MapEffects_SpawnProjectile1);
S3RE_HOOK(0x005458d0, MapEffects_SpawnProjectile0, Spawn9Fn, Original_MapEffects_SpawnProjectile0,
          MapEffects_SpawnProjectile0);
S3RE_HOOK(0x00545730, MapEffects_SpawnProjectile2, Spawn9Fn, Original_MapEffects_SpawnProjectile2,
          MapEffects_SpawnProjectile2);
S3RE_HOOK(0x00545b90, MapEffects_SpawnProjectile, Spawn10Fn, Original_MapEffects_SpawnProjectile,
          MapEffects_SpawnProjectile);
S3RE_HOOK(0x00544630, MapEffects_Spawn_544630, Spawn8Fn, Original_MapEffects_Spawn_544630, MapEffects_Spawn_544630);
S3RE_HOOK(0x005449f0, MapEffects_Spawn_5449f0, Spawn8Fn, Original_MapEffects_Spawn_5449f0, MapEffects_Spawn_5449f0);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Two effect tables with identical random contents, made consistent per
// case as in map_effects.cpp's tests; the records' padding bytes (stack
// leftovers of the make functions) are not compared.
constexpr std::size_t kTableBytes = sizeof(MapEffectTable) + 4;
constexpr std::size_t kTestCells = 1400;
constexpr std::size_t kSlots = sizeof(MapEffectTable::records) / sizeof(MapEffect);
constexpr std::size_t kPaddingBytes[] = {offsetof(MapEffect, pad2a), offsetof(MapEffect, pad2a) + 1,
                                         offsetof(MapEffect, pad3b), offsetof(MapEffect, pad3e),
                                         offsetof(MapEffect, pad3e) + 1};

struct Functions {
    ResetFn reset;
    UnlinkFn unlink;
    RemoveFn remove;
    Spawn9Fn arrow, projectile1, projectile0, projectile2;
    Spawn10Fn projectile;
    Spawn8Fn spawn544630, spawn5449f0;
};

Functions Originals() {
    return {Original_MapEffect_Reset,          Original_MapEffects_Unlink,        Original_MapEffects_RemoveMatching,
            Original_MapEffects_SpawnArrow,    Original_MapEffects_SpawnProjectile1,
            Original_MapEffects_SpawnProjectile0, Original_MapEffects_SpawnProjectile2,
            Original_MapEffects_SpawnProjectile, Original_MapEffects_Spawn_544630, Original_MapEffects_Spawn_5449f0};
}

Functions Ours() {
    return {&MapEffect_Reset,          &MapEffects_Unlink,        &MapEffects_RemoveMatching,
            &MapEffects_SpawnArrow,    &MapEffects_SpawnProjectile1, &MapEffects_SpawnProjectile0,
            &MapEffects_SpawnProjectile2, &MapEffects_SpawnProjectile, &MapEffects_Spawn_544630,
            &MapEffects_Spawn_5449f0};
}

constexpr int kOps = 10;

U Run(const Functions& f, int op, MapEffectTable* table, const U* a) {
    switch (op) {
    case 0: return f.reset(&table->records[a[0] % kSlots], nullptr);
    case 1: return f.unlink(table, nullptr, 1 + a[0] % (static_cast<U>(table->slots_used) + 12));
    case 2: return f.remove(table, nullptr, a[0], a[1], a[2], a[3]);
    case 3: return f.arrow(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 4: return f.projectile1(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 5: return f.projectile0(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 6: return f.projectile2(table, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 7: return f.projectile(table, nullptr, a[0], a[9] % 5, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 8: return f.spawn544630(table, nullptr, a[0], a[1] % 4, a[2] % 2, a[3], a[4], a[5], a[6], a[7]);
    default: return f.spawn5449f0(table, nullptr, a[0], a[1] % 4, a[2] % 4, a[3], a[4], a[5], a[6], a[7]);
    }
}

void Compare(test::Context& ctx, const Functions& candidate, int cases, int onlyOp = -1) {
    const Functions original = Originals();
    std::vector<std::uint8_t> a(kTableBytes), b(kTableBytes);
    ctx.rng().Fill(a.data(), a.size());
    auto* const table = reinterpret_cast<MapEffectTable*>(a.data());
    for (int i = 0; i < cases; ++i) {
        const int op = onlyOp >= 0 ? onlyOp : static_cast<int>(ctx.rng().Below(kOps));
        const U used = ctx.rng().Below(60);
        table->slots_used = static_cast<std::int32_t>(used);
        table->blocks_x = static_cast<std::int32_t>(1 + ctx.rng().Below(40));
        const U last = used + 12;
        // Records with consistent links: each cell list a chain of slots.
        for (U cell = 0; cell < kTestCells; ++cell) {
            table->cell_heads[cell] = 0;
        }
        for (U slot = 0; slot <= last; ++slot) {
            MapEffect& r = table->records[slot];
            const bool free = slot == 0 || ctx.rng().Chance(1, 6);
            r.start_tick = free ? 0 : 1 + ctx.rng().Below(0xffff);
            r.source_kind = static_cast<std::uint8_t>(ctx.rng().Below(3));
            r.source_id = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 0xfffe + ctx.rng().Below(2) : ctx.rng().Below(3));
            r.list = r.previous = r.next = 0;
            if (slot != 0 && !free) {
                const U cell = ctx.rng().Below(8);
                const U head = table->cell_heads[cell];
                r.list = cell;
                r.next = head;
                if (head != 0) {
                    table->records[head].previous = slot;
                }
                table->cell_heads[cell] = slot;
            }
        }
        U args[10];
        for (U& arg : args) {
            arg = ctx.rng().Below(7000);
        }
        args[0] = ctx.rng().Next32();  // the start tick
        if (op == 2) {
            args[0] = ctx.rng().Below(3);
            args[1] = ctx.rng().Chance(1, 4) ? 0xfffffffeu + ctx.rng().Below(2) : ctx.rng().Below(3);
            args[2] = ctx.rng().Chance(1, 3) ? 0xffffffffu : ctx.rng().Below(256 * 3);
            args[3] = ctx.rng().Chance(1, 3) ? 0xffffffffu : ctx.rng().Below(256 * 2);
            table->blocks_x = 3;
        }
        if (op >= 3 && op <= 7) {
            // The arc lifts the middle points by up to (dx*dx + 4*dy*dy) /
            // 48000 pixels: keep y large enough that every position stays
            // in a cell of the table (as on a real map).
            table->blocks_x = static_cast<std::int32_t>(1 + ctx.rng().Below(10));
            args[2] = 6000 + ctx.rng().Below(7000);
            args[6] = 6000 + ctx.rng().Below(7000);
        }
        // SpawnProjectile0's arc is (dx*dx + 4*dy*dy) / 2400 pixels high:
        // short flights only (it is called for nearby targets).
        const bool highArc = op == 5 || (op == 7 && args[9] % 5 == 0);
        if (highArc) {
            args[5] = args[1] + ctx.rng().Below(3000) - 1500;
            args[6] = args[2] + ctx.rng().Below(3000) - 1500;
        }
        if (op >= 3 && op <= 7 && ctx.rng().Chance(1, 4)) {
            // Distances whose flight time lands on or next to an integer:
            // dx = 20k (times 0.05 = k), or a 3-4-5 triangle.
            const U k = 1 + ctx.rng().Below(highArc ? 15 : 60);
            args[5] = args[1] + 20 * k;
            args[6] = args[2] + (ctx.rng().Chance(1, 2) ? 0 : 1);
            if (ctx.rng().Chance(1, 2)) {
                args[5] = args[1] - 30 * k;
                args[6] = args[2] + 40 * k;
            }
        }
        if (op >= 3 && op <= 7 && !highArc && ctx.rng().Chance(1, 15)) {
            // Long distances: dx*dx + dy*dy wraps to a negative int32 (sqrt
            // gives NaN, _ftol 0) or not. One block per row keeps the cells
            // of these positions inside the table.
            table->blocks_x = 1;
            args[5] = args[1] + 40000 + ctx.rng().Below(60000);
            args[2] = 50000 + ctx.rng().Below(7000);
            args[6] = 50000 + ctx.rng().Below(7000);
        }
        b = a;
        const U ra = Run(original, op, table, args);
        const U rb = Run(candidate, op, reinterpret_cast<MapEffectTable*>(b.data()), args);
        if (op >= 3) {
            for (std::size_t slot = 1; slot < kSlots; ++slot) {
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

// Wrong: rounds the flight time to nearest instead of truncating.
std::uint32_t __fastcall WrongSpawnProjectile2(MapEffectTable* self, void* edx, U a1, U a2, U a3, U a4, U a5, U a6,
                                               U a7, U a8, U a9) {
    const Flight f = MakeFlight(a2, a3, a6, a7, 12000, g_flightFactor2);
    const double exact = std::sqrt(static_cast<double>(static_cast<std::int32_t>(f.squares))) *
                         g_flightFactor2;
    if (exact - std::floor(exact) >= 0.5) {
        return MapEffects_SpawnProjectile2(self, edx, a1 + 1, a2, a3, a4, a5, a6, a7, a8, a9);
    }
    return MapEffects_SpawnProjectile2(self, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

// Wrong: the last record of the cell list is not unlinked from its previous one.
std::uint32_t __fastcall WrongUnlink(MapEffectTable* self, void* edx, std::uint32_t slot) {
    if (self->records[slot].next == 0 && self->records[slot].previous != 0) {
        MapEffect_SetLinks(&self->records[slot], nullptr, 0, 0, 0);
        return 0;
    }
    return MapEffects_Unlink(self, edx, slot);
}

}  // namespace

S3RE_TEST(MapEffects_PackageB_MatchOriginal) {
    Compare(ctx, Ours(), 4000);
}

S3RE_TEST(MapEffects_PackageB_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Functions wrong = Ours();
        wrong.projectile2 = &WrongSpawnProjectile2;
        Compare(probe, wrong, 400, 6);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Functions wrong = Ours();
        wrong.unlink = &WrongUnlink;
        Compare(probe, wrong, 400, 1);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
