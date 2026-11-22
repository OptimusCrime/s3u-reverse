// Spells ("god events", Volker\GodlyEvents.cpp): the dispatcher and the
// small spells. Each spell checks HasManaForSpell (0x54b240) and, when it
// had an effect, pays with PayForSpell (0x54b2b0); see godly_events.cpp.
// The ring table world+0x110a588 (MapOffset) gives the area: the first N
// offsets around (x, y), N from a table of int32 at 0x65a1f0 (127, 200,
// 100, 200, 300 in the image; re-read every step of a loop), tiles checked
// signed against the map size.
//
// GameWorld::ExecuteSpell (0x005480b0): (player, x, y, a4, spell), the
//   timed event of type 2 (UpdateEnvironment). x, y clamped to the map
//   (signed); then by spell (jump table 0x5482bc): 0..3 0x548340(player, x,
//   y, spell); 4 0x548fb0; 5 RevealSpot; 6 BurnTrees; 7 AddFish; 8
//   HalveResources; 9 GrowTrees (player, x, y); 10, 12, 16, 17, 24, 25
//   0x549f40(player, x, y, spell); 11 0x54a370; 13 0x54a7c0 and 14 0x54a540
//   (player, x, y, a4); 15 0x54a1e0; 18..20, 27 0x549bd0 ("xxx about to
//   execute EventConvert..."); 21..23, 26 0x5499b0 ("EventGodlyG..."); 28
//   0x54aa40 and 29 0x54acd0 (player, x, y, a4; "EventGoodsB/S..."); 30
//   0x5492a0 ("EventTreeCut..."); 31 RevealMap(player) ("EventReveal...");
//   the DebugLog comes before the call. Other spells: nothing. ret 0x14.
//   EAX not used (the caller overwrites it).
// GameWorld::SpellRevealSpot (0x00549530): (player, x, y) spell 5: for the
//   local player, the first free reveal point (active == 0, of 50) gets (x,
//   y), countdown 0xfa and active 1, the high-water mark raised (signed).
//   Paid also for other players and when all 50 are taken. ret 0xc.
// GameWorld::SpellRevealMap (0x005495d0): (player) spell 31: for the local
//   player: if g_revealMap is not 1 yet: g_revealMap = 1, player +0x124 =
//   1, +0x128 = g_gameTick, DebugLog("xxx GE done: optionviewall SET");
//   else PushUiFeedback(0x36). Always paid. ret 4.
// GameWorld::SpellBurnTrees (0x00549650): (player, x, y) spell 6: trees
//   0x44..0x51 among the first 127 offsets catch fire (object 0xf3); paid
//   if any did. ret 0xc.
// GameWorld::SpellAddFish (0x00549700): (player, x, y) spell 7: on water
//   tiles (terrain & 0xf0 == 0) among the first 200 offsets the resource
//   byte += 1 + (Random() & 0xffff) % 4 (byte arithmetic), at most 0xf
//   (results >= 0x10 become 0xf); paid if any tile was on the map and
//   water. ret 0xc.
// GameWorld::SpellHalveResources (0x005497f0): (player, x, y) spell 8: the
//   resource amount (low nibble) of the first 100 offsets' tiles is halved
//   ((r >> 1) & 7 | r & 0xf0); paid if any tile was on the map. ret 0xc.
// GameWorld::SpellGrowTrees (0x005498a0): (player, x, y) spell 9: growing
//   trees among the first 200 offsets advance three stages within their
//   kind (0x55..0x5d up to 0x5d, 0x5e..0x66 up to 0x66, 0x67..0x6f up to
//   0x6f, one after the other on the same tile); paid if any grew. ret 0xc.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::MapOffset;
using layout::Tile;
using U = std::uint32_t;

constexpr std::uintptr_t kHasManaForSpell = 0x0054b240;
constexpr std::uintptr_t kPayForSpell = 0x0054b2b0;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kPushUiFeedback = 0x004febe0;
constexpr std::uintptr_t kDebugLogFn = 0x00428720;
constexpr std::uintptr_t kGameTick = 0x007dfd48;
constexpr std::uintptr_t kRevealMap = 0x007dfd84;     // int32 g_revealMap
constexpr std::uintptr_t kSpellAreas = 0x0065a1f0;    // int32[]: offsets per spell area
constexpr std::uintptr_t kRevealMapText = 0x007310c0;  // "xxx GE done: optionviewall SET"

// The handlers, by address.
constexpr std::uintptr_t kSpellArea = 0x00548340;      // spells 0..3
constexpr std::uintptr_t kSpell4 = 0x00548fb0;
constexpr std::uintptr_t kRevealSpot = 0x00549530;
constexpr std::uintptr_t kBurnTrees = 0x00549650;
constexpr std::uintptr_t kAddFish = 0x00549700;
constexpr std::uintptr_t kHalveResources = 0x005497f0;
constexpr std::uintptr_t kGrowTrees = 0x005498a0;
constexpr std::uintptr_t kSpell549f40 = 0x00549f40;
constexpr std::uintptr_t kSpell11 = 0x0054a370;
constexpr std::uintptr_t kSpell13 = 0x0054a7c0;
constexpr std::uintptr_t kSpell14 = 0x0054a540;
constexpr std::uintptr_t kSpell15 = 0x0054a1e0;
constexpr std::uintptr_t kConvert = 0x00549bd0;
constexpr std::uintptr_t kGodly = 0x005499b0;
constexpr std::uintptr_t kGoodsB = 0x0054aa40;
constexpr std::uintptr_t kGoodsS = 0x0054acd0;
constexpr std::uintptr_t kTreeCut = 0x005492a0;
constexpr std::uintptr_t kRevealMapSpell = 0x005495d0;
constexpr std::uintptr_t kConvertText = 0x00730f90;
constexpr std::uintptr_t kGodlyText = 0x00730f68;
constexpr std::uintptr_t kGoodsBText = 0x00730fe0;
constexpr std::uintptr_t kGoodsSText = 0x00730fb8;
constexpr std::uintptr_t kTreeCutText = 0x00731004;
constexpr std::uintptr_t kRevealText = 0x00731028;

using Fn0 = U(__fastcall*)(GameWorld*, void*);
using Fn1 = U(__fastcall*)(GameWorld*, void*, U);
using Fn2 = U(__fastcall*)(GameWorld*, void*, U, U);
using Fn3 = U(__fastcall*)(GameWorld*, void*, U, U, U);
using Fn4 = U(__fastcall*)(GameWorld*, void*, U, U, U, U);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

void DebugLog(std::uintptr_t format) {
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLogFn)(reinterpret_cast<const char*>(format));
}

bool HasMana(GameWorld* world, U player, U spell) {
    return (Callee<Fn2>(kHasManaForSpell)(world, nullptr, player, spell) & 0xff) != 0;
}

void Pay(GameWorld* world, U player, U spell) {
    Callee<Fn2>(kPayForSpell)(world, nullptr, player, spell);
}

std::int32_t AreaSize(U index) {
    return *reinterpret_cast<const volatile std::int32_t*>(kSpellAreas + index * 4);
}

// Calls visit(tile) for the map tiles among the first AreaSize(area)
// offsets around (x, y).
template <typename Visit>
void ForArea(GameWorld* world, U area, std::int32_t x, std::int32_t y, Visit visit) {
    for (std::int32_t i = 0; i < AreaSize(area); ++i) {
        const MapOffset& o = world->offsets[i];
        const std::int32_t tx = o.dx + x;
        const std::int32_t ty = o.dy + y;
        const auto size = static_cast<std::int32_t>(world->map_size);
        if (tx >= 0 && tx < size && ty >= 0 && ty < size) {
            visit(world->tiles[tx][ty]);
        }
    }
}

}  // namespace

using SpellFn = U(__fastcall*)(GameWorld* world, void* edx, U player, std::int32_t x, std::int32_t y);
using RevealMapFn = U(__fastcall*)(GameWorld* world, void* edx, U player);
using ExecuteFn = U(__fastcall*)(GameWorld* world, void* edx, U player, std::int32_t x, std::int32_t y, U a4, U spell);

// Purpose: Spell 5: lets the casting (local) player see a spot of the map for a while.
U __fastcall GameWorld_SpellRevealSpot(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    if (!HasMana(world, player, 5)) {
        return 0;
    }
    if (player == static_cast<U>(world->local_player)) {
        std::int32_t i = 0;
        while (world->reveal_points[i].active != 0) {
            if (++i >= 50) {
                Pay(world, player, 5);
                return 0;
            }
        }
        layout::RevealPoint& point = world->reveal_points[i];
        point.x = x;
        point.y = y;
        point.countdown = 0xfa;
        point.active = 1;
        if (i > world->reveal_point_high_water) {
            world->reveal_point_high_water = i;
        }
    }
    Pay(world, player, 5);
    return 0;
}

// Purpose: Spell 31: shows the local player the whole map.
U __fastcall GameWorld_SpellRevealMap(GameWorld* world, void*, U player) {
    if (!HasMana(world, player, 0x1f)) {
        return 0;
    }
    if (player == static_cast<U>(world->local_player)) {
        auto& reveal = *reinterpret_cast<volatile std::int32_t*>(kRevealMap);
        if (reveal != 1) {
            reveal = 1;
            layout::Player& p = world->players[player];
            p.map_revealed = 1;
            p.map_revealed_tick = *reinterpret_cast<const volatile std::int32_t*>(kGameTick);
            DebugLog(kRevealMapText);
            Pay(world, player, 0x1f);
            return 0;
        }
        Callee<Fn1>(kPushUiFeedback)(world, nullptr, 0x36);
    }
    Pay(world, player, 0x1f);
    return 0;
}

// Purpose: Spell 6: sets the trees around a spot on fire.
U __fastcall GameWorld_SpellBurnTrees(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    if (!HasMana(world, player, 6)) {
        return 0;
    }
    bool done = false;
    ForArea(world, 0, x, y, [&](Tile& t) {
        if (t.object >= 0x44 && t.object <= 0x51) {
            t.object = 0xf3;
            done = true;
        }
    });
    if (done) {
        Pay(world, player, 6);
    }
    return 0;
}

// Purpose: Spell 7: fills the waters around a spot with fish.
U __fastcall GameWorld_SpellAddFish(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    if (!HasMana(world, player, 7)) {
        return 0;
    }
    bool done = false;
    ForArea(world, 1, x, y, [&](Tile& t) {
        if ((t.terrain & 0xf0) != 0) {
            return;
        }
        done = true;
        const U r = Callee<Fn0>(kRandom)(world, nullptr) & 0xffff;
        const auto fish = static_cast<std::uint8_t>(t.resources + static_cast<std::uint8_t>(r % 4 + 1));
        t.resources = fish;
        if (fish >= 0x10) {
            t.resources = 0xf;
        }
    });
    if (done) {
        Pay(world, player, 7);
    }
    return 0;
}

// Purpose: Spell 8: halves the resources (ore, fish) around a spot, probably to harm an enemy.
U __fastcall GameWorld_SpellHalveResources(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    if (!HasMana(world, player, 8)) {
        return 0;
    }
    bool done = false;
    ForArea(world, 2, x, y, [&](Tile& t) {
        done = true;
        const std::uint8_t r = t.resources;
        t.resources = static_cast<std::uint8_t>(((r >> 1) & 7) | (r & 0xf0));
    });
    if (done) {
        Pay(world, player, 8);
    }
    return 0;
}

// Purpose: Spell 9: makes the young trees around a spot grow faster.
U __fastcall GameWorld_SpellGrowTrees(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    if (!HasMana(world, player, 9)) {
        return 0;
    }
    bool done = false;
    ForArea(world, 3, x, y, [&](Tile& t) {
        static constexpr std::uint8_t kKinds[3][2] = {{0x55, 0x5d}, {0x5e, 0x66}, {0x67, 0x6f}};
        for (const auto& kind : kKinds) {
            const std::uint8_t o = t.object;
            if (o >= kind[0] && o <= kind[1]) {
                done = true;
                const std::int32_t grown = o + 3;
                t.object = static_cast<std::uint8_t>(grown > kind[1] ? kind[1] : grown);
            }
        }
    });
    if (done) {
        Pay(world, player, 9);
    }
    return 0;
}

// Purpose: Carries out a spell when its cast time has come: picks the spell's effect by its number.
U __fastcall GameWorld_ExecuteSpell(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y, U a4, U spell) {
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
    const U ux = static_cast<U>(x);
    const U uy = static_cast<U>(y);
    switch (spell) {
    case 0: case 1: case 2: case 3:
        return Callee<Fn4>(kSpellArea)(world, nullptr, player, ux, uy, spell);
    case 4:
        return Callee<Fn3>(kSpell4)(world, nullptr, player, ux, uy);
    case 5:
        return Callee<Fn3>(kRevealSpot)(world, nullptr, player, ux, uy);
    case 6:
        return Callee<Fn3>(kBurnTrees)(world, nullptr, player, ux, uy);
    case 7:
        return Callee<Fn3>(kAddFish)(world, nullptr, player, ux, uy);
    case 8:
        return Callee<Fn3>(kHalveResources)(world, nullptr, player, ux, uy);
    case 9:
        return Callee<Fn3>(kGrowTrees)(world, nullptr, player, ux, uy);
    case 10: case 12: case 16: case 17: case 24: case 25:
        return Callee<Fn4>(kSpell549f40)(world, nullptr, player, ux, uy, spell);
    case 11:
        return Callee<Fn3>(kSpell11)(world, nullptr, player, ux, uy);
    case 13:
        return Callee<Fn4>(kSpell13)(world, nullptr, player, ux, uy, a4);
    case 14:
        return Callee<Fn4>(kSpell14)(world, nullptr, player, ux, uy, a4);
    case 15:
        return Callee<Fn3>(kSpell15)(world, nullptr, player, ux, uy);
    case 18: case 19: case 20: case 27:
        DebugLog(kConvertText);
        return Callee<Fn3>(kConvert)(world, nullptr, player, ux, uy);
    case 21: case 22: case 23: case 26:
        DebugLog(kGodlyText);
        return Callee<Fn3>(kGodly)(world, nullptr, player, ux, uy);
    case 28:
        DebugLog(kGoodsBText);
        return Callee<Fn4>(kGoodsB)(world, nullptr, player, ux, uy, a4);
    case 29:
        DebugLog(kGoodsSText);
        return Callee<Fn4>(kGoodsS)(world, nullptr, player, ux, uy, a4);
    case 30:
        DebugLog(kTreeCutText);
        return Callee<Fn3>(kTreeCut)(world, nullptr, player, ux, uy);
    case 31:
        DebugLog(kRevealText);
        return Callee<Fn1>(kRevealMapSpell)(world, nullptr, player);
    default:
        return 0;
    }
}

S3RE_HOOK(0x005480b0, GameWorld_ExecuteSpell, ExecuteFn, Original_GameWorld_ExecuteSpell, GameWorld_ExecuteSpell);
S3RE_HOOK(0x00549530, GameWorld_SpellRevealSpot, SpellFn, Original_GameWorld_SpellRevealSpot,
          GameWorld_SpellRevealSpot);
S3RE_HOOK(0x005495d0, GameWorld_SpellRevealMap, RevealMapFn, Original_GameWorld_SpellRevealMap,
          GameWorld_SpellRevealMap);
S3RE_HOOK(0x00549650, GameWorld_SpellBurnTrees, SpellFn, Original_GameWorld_SpellBurnTrees,
          GameWorld_SpellBurnTrees);
S3RE_HOOK(0x00549700, GameWorld_SpellAddFish, SpellFn, Original_GameWorld_SpellAddFish, GameWorld_SpellAddFish);
S3RE_HOOK(0x005497f0, GameWorld_SpellHalveResources, SpellFn, Original_GameWorld_SpellHalveResources,
          GameWorld_SpellHalveResources);
S3RE_HOOK(0x005498a0, GameWorld_SpellGrowTrees, SpellFn, Original_GameWorld_SpellGrowTrees,
          GameWorld_SpellGrowTrees);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(GameWorld);

// A map with a ring table of 300 small offsets, interesting objects,
// terrain and resources, and players with enough mana now and then.
void SetUpSpellWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    GameWorld* w = worlds.original();
    const U size = 32 + rng.Below(97);
    w->map_size = size;
    for (U n = 0; n < 320; ++n) {
        const auto d = static_cast<std::int32_t>(1 + n / 12);
        w->offsets[n].dx = rng.Range(-d, d);
        w->offsets[n].dy = rng.Range(-d, d);
    }
    for (U x = 0; x < size; ++x) {
        for (U y = 0; y < size; ++y) {
            Tile& t = w->tiles[x][y];
            t.object = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0x40 + rng.Below(0x34) : rng.Next32());
            t.terrain = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(16) : rng.Next32());
            t.resources = static_cast<std::uint8_t>(rng.Next32());
        }
    }
    w->local_player = static_cast<std::int32_t>(rng.Below(4));
    for (U p = 0; p < layout::kPlayers; ++p) {
        layout::Player& player = w->players[p];
        player.race = static_cast<std::int32_t>(rng.Below(4));
        player.mana = static_cast<std::int32_t>(rng.Below(100));
        for (U slot = 0; slot < 9; ++slot) {
            reinterpret_cast<std::int32_t*>(player.spell_mana)[slot] = static_cast<std::int32_t>(rng.Below(60));
        }
    }
    w->reveal_point_high_water = rng.Range(-1, 49);
    for (auto& point : w->reveal_points) {
        point.active = static_cast<std::uint8_t>(rng.Chance(4, 5) ? 1 : 0);
    }
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kWorld);
}

U Player(test::Context& ctx) {
    return ctx.rng().Below(4);
}

std::int32_t Coordinate(test::Context& ctx, U size) {
    return ctx.rng().Chance(1, 10) ? ctx.rng().Range(-20, static_cast<std::int32_t>(size) + 20)
                                   : static_cast<std::int32_t>(ctx.rng().Below(size));
}

void CompareSpell(test::Context& ctx, SpellFn original, SpellFn candidate, int cases) {
    test::WorldPair worlds(ctx, kWorld);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        if (i % 50 == 0) {
            SetUpSpellWorld(ctx, worlds);
        }
        const U player = Player(ctx);
        const std::int32_t x = Coordinate(ctx, worlds.original()->map_size);
        const std::int32_t y = Coordinate(ctx, worlds.original()->map_size);
        original(worlds.original(), nullptr, player, x, y);
        const std::vector<DebugLogCall> expected = logs.Take();
        candidate(worlds.ours(), nullptr, player, x, y);
        const std::vector<DebugLogCall> actual = logs.Take();
        if (!S3RE_EXPECT(expected == actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u at (%d, %d)", i, player, x, y);
            return;
        }
    }
}

void CompareRevealMap(test::Context& ctx, RevealMapFn candidate, int cases) {
    const std::int32_t savedReveal = *reinterpret_cast<std::int32_t*>(kRevealMap);
    test::WorldPair worlds(ctx, kWorld);
    test::CalleeStubs stubs;
    stubs.Install<1, kPushUiFeedback>();
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        if (i % 50 == 0) {
            SetUpSpellWorld(ctx, worlds);
        }
        const U player = Player(ctx);
        const std::int32_t reveal = ctx.rng().Chance(1, 2) ? 1 : static_cast<std::int32_t>(ctx.rng().Below(3)) - 1;
        *reinterpret_cast<std::int32_t*>(kRevealMap) = reveal;
        stubs.SetWorld(worlds.original());
        Original_GameWorld_SpellRevealMap(worlds.original(), nullptr, player);
        const std::int32_t expectedReveal = *reinterpret_cast<std::int32_t*>(kRevealMap);
        const auto expectedCalls = stubs.Take();
        const auto expectedLogs = logs.Take();
        *reinterpret_cast<std::int32_t*>(kRevealMap) = reveal;
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, player);
        const std::int32_t actualReveal = *reinterpret_cast<std::int32_t*>(kRevealMap);
        if (!S3RE_EXPECT_EQ(expectedReveal, actualReveal) || !S3RE_EXPECT(expectedCalls == stubs.Take()) ||
            !S3RE_EXPECT(expectedLogs == logs.Take()) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, reveal %d", i, player, reveal);
            break;
        }
    }
    *reinterpret_cast<std::int32_t*>(kRevealMap) = savedReveal;
}

void InstallHandlerStubs(test::CalleeStubs& stubs) {
    stubs.Install<4, kSpellArea>();
    stubs.Install<3, kSpell4>();
    stubs.Install<3, kRevealSpot>();
    stubs.Install<3, kBurnTrees>();
    stubs.Install<3, kAddFish>();
    stubs.Install<3, kHalveResources>();
    stubs.Install<3, kGrowTrees>();
    stubs.Install<4, kSpell549f40>();
    stubs.Install<3, kSpell11>();
    stubs.Install<4, kSpell13>();
    stubs.Install<4, kSpell14>();
    stubs.Install<3, kSpell15>();
    stubs.Install<3, kConvert>();
    stubs.Install<3, kGodly>();
    stubs.Install<4, kGoodsB>();
    stubs.Install<4, kGoodsS>();
    stubs.Install<3, kTreeCut>();
    stubs.Install<1, kRevealMapSpell>();
}

void CompareExecute(test::Context& ctx, ExecuteFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, map_size) + 4);
    test::CalleeStubs stubs;
    InstallHandlerStubs(stubs);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        const U size = 1 + ctx.rng().Below(768);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        const U player = ctx.rng().Next32();
        const std::int32_t x = ctx.rng().Range(-50, static_cast<std::int32_t>(size) + 50);
        const std::int32_t y = ctx.rng().Range(-50, static_cast<std::int32_t>(size) + 50);
        const U a4 = ctx.rng().Next32();
        const U spell = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(34);
        stubs.SetWorld(worlds.original());
        Original_GameWorld_ExecuteSpell(worlds.original(), nullptr, player, x, y, a4, spell);
        const auto expectedCalls = stubs.Take();
        const auto expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, player, x, y, a4, spell);
        if (!S3RE_EXPECT(expectedCalls == stubs.Take()) || !S3RE_EXPECT(expectedLogs == logs.Take())) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: spell %u at (%d, %d), size %u", i, spell, x, y, size);
            return;
        }
    }
}

// Wrong: the tree growth of one kind stops one stage early.
U __fastcall WrongSpellGrowTrees(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y) {
    std::vector<std::uint8_t> before;
    for (U tx = 0; tx < world->map_size; ++tx) {
        for (U ty = 0; ty < world->map_size; ++ty) {
            before.push_back(world->tiles[tx][ty].object);
        }
    }
    GameWorld_SpellGrowTrees(world, nullptr, player, x, y);
    std::size_t n = 0;
    for (U tx = 0; tx < world->map_size; ++tx) {
        for (U ty = 0; ty < world->map_size; ++ty, ++n) {
            std::uint8_t& o = world->tiles[tx][ty].object;
            if (o == 0x66 && before[n] != 0x66) {
                o = 0x65;  // capped one stage early
            }
        }
    }
    return 0;
}

// Wrong: spells 13 and 14 are swapped.
U __fastcall WrongExecuteSpell(GameWorld* world, void*, U player, std::int32_t x, std::int32_t y, U a4, U spell) {
    const U swapped = spell == 13 ? 14 : spell == 14 ? 13 : spell;
    return GameWorld_ExecuteSpell(world, nullptr, player, x, y, a4, swapped);
}

}  // namespace

S3RE_TEST(GameWorld_SpellRevealSpot_MatchesOriginal) {
    CompareSpell(ctx, Original_GameWorld_SpellRevealSpot, &GameWorld_SpellRevealSpot, 200);
}
S3RE_TEST(GameWorld_SpellBurnTrees_MatchesOriginal) {
    CompareSpell(ctx, Original_GameWorld_SpellBurnTrees, &GameWorld_SpellBurnTrees, 200);
}
S3RE_TEST(GameWorld_SpellAddFish_MatchesOriginal) {
    CompareSpell(ctx, Original_GameWorld_SpellAddFish, &GameWorld_SpellAddFish, 200);
}
S3RE_TEST(GameWorld_SpellHalveResources_MatchesOriginal) {
    CompareSpell(ctx, Original_GameWorld_SpellHalveResources, &GameWorld_SpellHalveResources, 200);
}
S3RE_TEST(GameWorld_SpellGrowTrees_MatchesOriginal) {
    CompareSpell(ctx, Original_GameWorld_SpellGrowTrees, &GameWorld_SpellGrowTrees, 200);
}
S3RE_TEST(GameWorld_SpellGrowTrees_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpell(probe, Original_GameWorld_SpellGrowTrees, &WrongSpellGrowTrees, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SpellRevealMap_MatchesOriginal) {
    CompareRevealMap(ctx, &GameWorld_SpellRevealMap, 200);
}
S3RE_TEST(GameWorld_ExecuteSpell_MatchesOriginal) {
    CompareExecute(ctx, &GameWorld_ExecuteSpell, 20000);
}
S3RE_TEST(GameWorld_ExecuteSpell_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExecute(probe, &WrongExecuteSpell, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
