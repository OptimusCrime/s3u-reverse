// The map effects shown for a cast spell.
//
// GameWorld::SpawnSpellEffects (0x005474e0): (a1, x, y, a4, spell), a1 and
//   a4 unused. Runs when a timed event of type 1 (the Effect table at
//   world+0x36700, see UpdateEnvironment) comes due. x and y are clamped to
//   0..size-1 (signed). Then, per spell (jump table 0x548044 / index bytes
//   0x54808c), map effects are built with the make functions 0x543f70 (a
//   stationary effect: screen x, screen y, kind), 0x543930 (moving: a0,
//   screen x, screen y, x, y, graphics) and 0x543e80 (a0, screen x, screen
//   y, x, y, graphics, 0xff) and added to the effect table (0x5445e0). Each
//   make function gets g_gameTick + delay as its EDX (the effect's start
//   tick). Screen positions are TileScreenX (0x519610) / TileScreenY
//   (0x519640) of a tile; for spells 7 and 8 some effects take the screen x
//   of (x, y) but the screen y of an offset tile, as in the original. The
//   effects per spell are in kSpells below (read off the push sequences).
//   Spells 4, 6, 0x12..0x17 and 0x1a..0x1d show nothing. ret 0x14; EAX not
//   used (the caller overwrites it).
//   The records' padding bytes come from the make functions' stack
//   temporaries, in the original as in ours (a known difference without
//   effect; the determinism trace and the emulator spec leave them out).
// thiscall (ECX = world).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::MapEffect;
using layout::MapEffectTable;
using U = std::uint32_t;

constexpr std::uintptr_t kMake3 = 0x00543f70;
constexpr std::uintptr_t kMake6 = 0x00543930;
constexpr std::uintptr_t kMake7 = 0x00543e80;
constexpr std::uintptr_t kAdd = 0x005445e0;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kGameTick = 0x007dfd48;

using Make3Fn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U);
using Make6Fn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U, U, U, U);
using Make7Fn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U, U, U, U, U);
using AddFn = U(__fastcall*)(MapEffectTable* self, void* edx, const MapEffect* effect);
using ScreenFn = U(__fastcall*)(GameWorld* world, void* edx, U x, U y);

enum class Make : std::uint8_t { Stationary, Moving, Moving7 };

// One effect: make function, start delay, first argument (moving kinds),
// tile offsets for the screen x, the screen y and the tile arguments, and
// the kind (stationary) or graphics id (moving).
struct Step {
    Make make;
    std::uint8_t delay;
    std::uint8_t a0;
    std::int8_t sx_dx, sx_dy, sy_dx, sy_dy, dx, dy;
    U value;
};

constexpr Step Stationary(std::uint8_t delay, std::int8_t sy_dx, std::int8_t sy_dy, U kind) {
    return {Make::Stationary, delay, 0, 0, 0, sy_dx, sy_dy, 0, 0, kind};
}
constexpr Step Moving(std::uint8_t delay, std::uint8_t a0, std::int8_t dx, std::int8_t dy, U graphics) {
    return {Make::Moving, delay, a0, dx, dy, dx, dy, dx, dy, graphics};
}
// Screen x of (x, y), screen y and tile of (x + dx, y + dy).
constexpr Step MovingAtY(std::uint8_t delay, std::uint8_t a0, std::int8_t dx, std::int8_t dy, U graphics) {
    return {Make::Moving, delay, a0, 0, 0, dx, dy, dx, dy, graphics};
}
constexpr Step Moving7(std::uint8_t delay, std::uint8_t a0, U graphics) {
    return {Make::Moving7, delay, a0, 0, 0, 0, 0, 0, 0, graphics};
}

constexpr Step kSpell0[] = {Stationary(0, 0, 0, 0x5e), Stationary(5, 0, 0, 0x4b), Moving(0, 0x24, 0, 0, 0xd003e)};
constexpr Step kSpell3[] = {Moving(0, 0x24, 0, 0, 0xd003c), Stationary(0, 0, 0, 0x61)};
constexpr Step kSpell5[] = {Moving7(0x46, 0x1e, 0xd003d), Stationary(0x50, 0, 0, 0x50)};
constexpr Step kSpell7[] = {
    Stationary(0, 0, 0, 0x4f),   Stationary(3, -3, 1, 0x4f),  Stationary(10, 1, -1, 0x4f),
    Stationary(15, 5, 3, 0x4f),  Stationary(20, 4, -5, 0x4f), Stationary(6, -2, -4, 0x4f),
    Moving(0, 0xc, 0, 0, 0xd0036),  Moving(3, 0xc, -3, 1, 0xd0036),  Moving(10, 0xc, 1, -1, 0xd0036),
    Moving(15, 0xc, 5, 3, 0xd0036), Moving(20, 0xc, 4, -5, 0xd0036), Moving(6, 0xc, -2, -4, 0xd0036)};
constexpr Step kSpell8[] = {
    Stationary(0, 0, 0, 0x50),          Moving(0, 0xc, 0, 0, 0xd0037),     Moving(3, 0xc, -3, 1, 0xd0037),
    Moving(10, 0xc, 1, -1, 0xd0037),    Moving(15, 0xc, 5, 3, 0xd0037),    MovingAtY(20, 0xc, 4, -5, 0xd0037),
    MovingAtY(6, 0xc, -2, -4, 0xd0037)};
constexpr Step kSpell10[] = {Moving(0, 0x28, 0, 0, 0xd0033), Stationary(0, 0, 0, 0x4b)};
constexpr Step kSpell12[] = {Moving(0, 0x28, 0, 0, 0xd0032), Stationary(0, 0, 0, 0x4b)};
constexpr Step kSpell24[] = {Moving(0, 0x28, 0, 0, 0xd0036), Stationary(0, 0, 0, 0x4b)};
constexpr Step kKind4b[] = {Stationary(0, 0, 0, 0x4b)};
constexpr Step kKind4bLate[] = {Stationary(5, 0, 0, 0x4b)};
constexpr Step kSpell31[] = {Stationary(5, 0, 0, 0x4b), Moving7(10, 0x1e, 0xd003d)};

struct Steps {
    const Step* steps;
    std::uint32_t count;
};

template <std::size_t N>
constexpr Steps Of(const Step (&steps)[N]) {
    return {steps, N};
}

constexpr Steps kNone = {nullptr, 0};

// Per spell 0..0x1f.
constexpr Steps kSpells[32] = {
    Of(kSpell0),  Of(kSpell0),  Of(kSpell0),  Of(kSpell3),      // 0..3
    kNone,        Of(kSpell5),  kNone,        Of(kSpell7),      // 4..7
    Of(kSpell8),  Of(kSpell3),  Of(kSpell10), Of(kKind4b),      // 8..0xb
    Of(kSpell12), Of(kKind4b),  Of(kKind4bLate), Of(kKind4b),   // 0xc..0xf
    Of(kSpell10), Of(kSpell12), kNone,        kNone,            // 0x10..0x13
    kNone,        kNone,        kNone,        kNone,            // 0x14..0x17
    Of(kSpell24), Of(kSpell24), kNone,        kNone,            // 0x18..0x1b
    kNone,        kNone,        Of(kSpell3),  Of(kSpell31),     // 0x1c..0x1f
};

}  // namespace

using SpawnSpellFn = void(__fastcall*)(GameWorld* world, void* edx, U a1, std::int32_t x, std::int32_t y, U a4,
                                       U spell);

// Purpose: Shows the visual effects of a spell that was cast at a spot (flashes, sparkles, rising
// lights and the like, different per spell).
void __fastcall GameWorld_SpawnSpellEffects(GameWorld* world, void*, U, std::int32_t x, std::int32_t y, U, U spell) {
    const auto size = static_cast<std::int32_t>(world->map_size);
    if (x < 0) {
        x = 0;
    }
    if (x >= size) {
        x = size - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= size) {
        y = size - 1;
    }
    if (spell >= 32) {
        return;
    }
    const auto screenX = reinterpret_cast<ScreenFn>(kTileScreenX);
    const auto screenY = reinterpret_cast<ScreenFn>(kTileScreenY);
    const auto add = reinterpret_cast<AddFn>(kAdd);
    MapEffectTable* table = &world->map_effects;
    const Steps& steps = kSpells[spell];
    for (std::uint32_t i = 0; i < steps.count; ++i) {
        const Step& s = steps.steps[i];
        const U sy = screenY(world, nullptr, static_cast<U>(x + s.sy_dx), static_cast<U>(y + s.sy_dy));
        const U sx = screenX(world, nullptr, static_cast<U>(x + s.sx_dx), static_cast<U>(y + s.sx_dy));
        void* const tick = reinterpret_cast<void*>(static_cast<std::uintptr_t>(*reinterpret_cast<const U*>(kGameTick) + s.delay));
        MapEffect effect;
        const U tx = static_cast<U>(x + s.dx);
        const U ty = static_cast<U>(y + s.dy);
        switch (s.make) {
        case Make::Stationary:
            reinterpret_cast<Make3Fn>(kMake3)(&effect, tick, sx, sy, s.value);
            break;
        case Make::Moving:
            reinterpret_cast<Make6Fn>(kMake6)(&effect, tick, s.a0, sx, sy, tx, ty, s.value);
            break;
        case Make::Moving7:
            reinterpret_cast<Make7Fn>(kMake7)(&effect, tick, s.a0, sx, sy, tx, ty, s.value, 0xff);
            break;
        }
        add(table, nullptr, &effect);
    }
}

S3RE_HOOK(0x005474e0, GameWorld_SpawnSpellEffects, SpawnSpellFn, Original_GameWorld_SpawnSpellEffects,
          GameWorld_SpawnSpellEffects);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(GameWorld);

// Bytes of the effect table to compare: everything but the records'
// padding (stack leftovers of the make functions' temporaries).
bool SameEffects(test::Context& ctx, test::WorldPair& worlds) {
    const std::size_t base = offsetof(GameWorld, map_effects);
    for (std::size_t slot = 0; slot < 5000; ++slot) {
        for (std::size_t pad : {std::size_t{0x2a}, std::size_t{0x2b}, std::size_t{0x3b}, std::size_t{0x3e},
                                std::size_t{0x3f}}) {
            worlds.originalBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
            worlds.oursBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
        }
    }
    return worlds.Checkpoint(ctx);
}

void SetUpEffectTable(test::Context& ctx, test::WorldPair& worlds) {
    worlds.Both([&](GameWorld* w) {
        std::memset(&w->map_effects, 0, sizeof(w->map_effects));
        w->map_effects.blocks_x = 30;
        w->map_effects.blocks_y = 30;
    });
    const auto used = static_cast<std::int32_t>(ctx.rng().Below(60));
    worlds.Both([&](GameWorld* w) { w->map_effects.slots_used = used; });
    for (std::int32_t slot = 1; slot <= used; ++slot) {
        const U start = ctx.rng().Chance(1, 4) ? 0 : 1 + ctx.rng().Below(1000);
        worlds.Both([&](GameWorld* w) { w->map_effects.records[slot].start_tick = start; });
    }
}

void Compare(test::Context& ctx, SpawnSpellFn candidate, int cases) {
    test::WorldPair worlds(ctx, kWorld);
    const U savedTick = *reinterpret_cast<U*>(kGameTick);
    for (int i = 0; i < cases; ++i) {
        const U size = 32 + ctx.rng().Below(353);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        SetUpEffectTable(ctx, worlds);
        *reinterpret_cast<U*>(kGameTick) = ctx.rng().Next32();
        const auto x = static_cast<std::int32_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(size + 4));
        const auto y = static_cast<std::int32_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(size + 4));
        const U spell = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(34);
        const U a1 = ctx.rng().Next32();
        const U a4 = ctx.rng().Next32();
        Original_GameWorld_SpawnSpellEffects(worlds.original(), nullptr, a1, x, y, a4, spell);
        candidate(worlds.ours(), nullptr, a1, x, y, a4, spell);
        if (!SameEffects(ctx, worlds)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%d, %d) spell %u", i, x, y, spell);
            break;
        }
    }
    *reinterpret_cast<U*>(kGameTick) = savedTick;
}

// Wrong: spell 8's last effect uses the screen x of its own tile.
void __fastcall WrongSpawnSpellEffects(GameWorld* world, void*, U a1, std::int32_t x, std::int32_t y, U a4, U spell) {
    if (spell == 8) {
        GameWorld_SpawnSpellEffects(world, nullptr, a1, x, y, a4, 7);
        return;
    }
    GameWorld_SpawnSpellEffects(world, nullptr, a1, x, y, a4, spell);
}

}  // namespace

S3RE_TEST(GameWorld_SpawnSpellEffects_MatchesOriginal) {
    Compare(ctx, &GameWorld_SpawnSpellEffects, 400);
}

S3RE_TEST(GameWorld_SpawnSpellEffects_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongSpawnSpellEffects, 400);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
