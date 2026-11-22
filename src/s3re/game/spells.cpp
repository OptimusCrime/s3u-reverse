// Spells ("god events", Volker\GodlyEvents.cpp): the dispatcher and the
// small spells. Each spell checks HasManaForSpell (0x54b240) and, when it
// had an effect, pays with PayForSpell (0x54b2b0); see godly_events.cpp.
// The ring table world+0x110a588 (MapOffset) gives the area: the first N
// offsets around (x, y), N from a table of int32 at 0x65a1f0 (127, 200,
// 100, 200, 300 in the image; re-read every step of a loop), tiles checked
// signed against the map size. Spell numbers: the Spell enum (enums.h).
//
// CGameData::ExecuteSpell (0x005480b0): (player, x, y, a4, spell), the
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
// CGameData::SpellRevealSpot (0x00549530): (player, x, y) spell 5: for the
//   local player, the first free reveal point (active == 0, of 50) gets (x,
//   y), countdown 250 (0xfa) and active 1, the high-water mark raised (signed).
//   Paid also for other players and when all 50 are taken. ret 0xc.
// CGameData::SpellRevealMap (0x005495d0): (player) spell 31: for the local
//   player: if g_revealMap is not 1 yet: g_revealMap = 1, player +0x124 =
//   1, +0x128 = g_gameTick, DebugLog("xxx GE done: optionviewall SET");
//   else PushUiFeedback(54, 0x36). Always paid. ret 4.
// CGameData::SpellBurnTrees (0x00549650): (player, x, y) spell 6: trees
//   0x44..0x51 among the first 127 offsets catch fire (object 0xf3); paid
//   if any did. ret 0xc.
// CGameData::SpellAddFish (0x00549700): (player, x, y) spell 7: on water
//   tiles (terrain & 0xf0 == 0) among the first 200 offsets the resource
//   byte += 1 + (Random() & 0xffff) % 4 (byte arithmetic), at most 0xf
//   (results >= 0x10 become 0xf); paid if any tile was on the map and
//   water. ret 0xc.
// CGameData::SpellHalveResources (0x005497f0): (player, x, y) spell 8: the
//   resource amount (low nibble) of the first 100 offsets' tiles is halved
//   ((r >> 1) & 7 | r & 0xf0); paid if any tile was on the map. ret 0xc.
// CGameData::SpellGrowTrees (0x005498a0): (player, x, y) spell 9: growing
//   trees among the first 200 offsets advance three stages within their
//   kind (0x55..0x5d up to 0x5d, 0x5e..0x66 up to 0x66, 0x67..0x6f up to
//   0x6f, one after the other on the same tile); paid if any grew. ret 0xc.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
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

// The area sizes (index into g_spellAreaSizes, 0x65a1f0: 127, 200, 100, 200, ...).
constexpr U kBurnTreesArea = 0;
constexpr U kAddFishArea = 1;
constexpr U kHalveResourcesArea = 2;
constexpr U kGrowTreesArea = 3;

constexpr std::int32_t kRevealPoints = 50;
constexpr std::int32_t kRevealSpotTicks = 250;  // 0xfa
constexpr U kFeedbackMapAlreadyRevealed = 54;   // PushUiFeedback 0x36 (probably "not possible")

// Tile::object values (hex, see environment.cpp): trees 0x44..0x51 catch
// fire (object 0xf3); the three kinds of young trees grow in 0x55..0x5d,
// 0x5e..0x66 and 0x67..0x6f.
constexpr std::uint8_t kFirstTree = 0x44;
constexpr std::uint8_t kLastBurningTree = 0x51;
constexpr std::uint8_t kFireStart = 0xf3;
constexpr std::int32_t kGrowStages = 3;

// Tile::resources: fish in the low nibble, up to 15.
constexpr std::uint8_t kMaxFish = 15;
constexpr U kFishVariants = 4;  // AddFish adds 1..4

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

using Fn0 = U(__fastcall*)(CGameData*, void*);
using Fn1 = U(__fastcall*)(CGameData*, void*, U);
using Fn2 = U(__fastcall*)(CGameData*, void*, U, U);
using Fn3 = U(__fastcall*)(CGameData*, void*, U, U, U);
using Fn4 = U(__fastcall*)(CGameData*, void*, U, U, U, U);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

void DebugLog(const GameText& format) {
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLogFn)(format.c_str());
}

bool HasMana(CGameData* world, U player, U spell) {
    return (Callee<Fn2>(kHasManaForSpell)(world, nullptr, player, spell) & 0xff) != 0;
}

void Pay(CGameData* world, U player, U spell) {
    Callee<Fn2>(kPayForSpell)(world, nullptr, player, spell);
}

std::int32_t AreaSize(U index) {
    return g_spellAreaSizes[index];
}

// Calls visit(tile) for the map tiles among the first AreaSize(area)
// offsets around (x, y).
template <typename Visit>
void ForArea(CGameData* world, U area, std::int32_t x, std::int32_t y, Visit visit) {
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

using SpellFn = U(__fastcall*)(CGameData* world, void* edx, U player, std::int32_t x, std::int32_t y);
using RevealMapFn = U(__fastcall*)(CGameData* world, void* edx, U player);
using ExecuteFn = U(__fastcall*)(CGameData* world, void* edx, U player, std::int32_t x, std::int32_t y, U a4, U spell);

// Purpose: Spell 5: lets the casting (local) player see a spot of the map for a while.
U __fastcall CGameData_SpellRevealSpot(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::RevealSpot);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    if (player == static_cast<U>(world->local_player)) {
        std::int32_t i = 0;
        while (world->reveal_points[i].active != 0) {
            if (++i >= kRevealPoints) {
                Pay(world, player, kSpell);  // all taken: paid all the same
                return 0;
            }
        }
        layout::RevealPoint& point = world->reveal_points[i];
        point.x = x;
        point.y = y;
        point.countdown = kRevealSpotTicks;
        point.active = 1;
        if (i > world->reveal_point_high_water) {
            world->reveal_point_high_water = i;
        }
    }
    Pay(world, player, kSpell);
    return 0;
}

// Purpose: Spell 31: shows the local player the whole map.
U __fastcall CGameData_SpellRevealMap(CGameData* world, void*, U player) {
    constexpr U kSpell = Value(Spell::RevealMap);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    if (player == static_cast<U>(world->local_player)) {
        std::int32_t& reveal = g_revealMap;
        if (reveal != 1) {
            reveal = 1;
            layout::Player& p = world->players[player];
            p.map_revealed = 1;
            p.map_revealed_tick = static_cast<std::int32_t>(g_gameTick.get());
            DebugLog(texts::kRevealMapDone);
            Pay(world, player, kSpell);
            return 0;
        }
        Callee<Fn1>(kPushUiFeedback)(world, nullptr, kFeedbackMapAlreadyRevealed);
    }
    Pay(world, player, kSpell);
    return 0;
}

// Purpose: Spell 6: sets the trees around a spot on fire.
U __fastcall CGameData_SpellBurnTrees(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::BurnTrees);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    ForArea(world, kBurnTreesArea, x, y, [&](Tile& t) {
        if (t.object >= kFirstTree && t.object <= kLastBurningTree) {
            t.object = kFireStart;
            done = true;
        }
    });
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 7: fills the waters around a spot with fish.
U __fastcall CGameData_SpellAddFish(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::AddFish);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    ForArea(world, kAddFishArea, x, y, [&](Tile& t) {
        if ((t.terrain & kTerrainClassMask) != kTerrainClassWater) {
            return;
        }
        done = true;
        const U r = Callee<Fn0>(kRandom)(world, nullptr) & 0xffff;
        // Byte arithmetic: the store comes first, then the cap.
        const auto fish = static_cast<std::uint8_t>(t.resources + static_cast<std::uint8_t>(r % kFishVariants + 1));
        t.resources = fish;
        if (fish > kMaxFish) {
            t.resources = kMaxFish;
        }
    });
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 8: halves the resources (ore, fish) around a spot, probably to harm an enemy.
U __fastcall CGameData_SpellHalveResources(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::HalveResources);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    ForArea(world, kHalveResourcesArea, x, y, [&](Tile& t) {
        done = true;
        // The amount (low nibble) halves; the kind (high nibble) stays.
        const std::uint8_t r = t.resources;
        t.resources = static_cast<std::uint8_t>(((r >> 1) & 7) | (r & 0xf0));
    });
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 9: makes the young trees around a spot grow faster.
U __fastcall CGameData_SpellGrowTrees(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::GrowTrees);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    ForArea(world, kGrowTreesArea, x, y, [&](Tile& t) {
        // The three kinds of young trees (first and last stage); one kind
        // after the other on the same tile, as in the original.
        static constexpr std::uint8_t kKinds[3][2] = {{0x55, 0x5d}, {0x5e, 0x66}, {0x67, 0x6f}};
        for (const auto& kind : kKinds) {
            const std::uint8_t o = t.object;
            if (o >= kind[0] && o <= kind[1]) {
                done = true;
                const std::int32_t grown = o + kGrowStages;
                t.object = static_cast<std::uint8_t>(grown > kind[1] ? kind[1] : grown);
            }
        }
    });
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Carries out a spell when its cast time has come: picks the spell's effect by its number.
U __fastcall CGameData_ExecuteSpell(CGameData* world, void*, U player, std::int32_t x, std::int32_t y, U a4, U spell) {
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
    // The spell is a dword (same width as Spell); values past 31 do nothing.
    switch (static_cast<Spell>(spell)) {
    case Spell::Spell0: case Spell::Spell1: case Spell::Spell2: case Spell::Spell3:
        return Callee<Fn4>(kSpellArea)(world, nullptr, player, ux, uy, spell);
    case Spell::Spell4:
        return Callee<Fn3>(kSpell4)(world, nullptr, player, ux, uy);
    case Spell::RevealSpot:
        return Callee<Fn3>(kRevealSpot)(world, nullptr, player, ux, uy);
    case Spell::BurnTrees:
        return Callee<Fn3>(kBurnTrees)(world, nullptr, player, ux, uy);
    case Spell::AddFish:
        return Callee<Fn3>(kAddFish)(world, nullptr, player, ux, uy);
    case Spell::HalveResources:
        return Callee<Fn3>(kHalveResources)(world, nullptr, player, ux, uy);
    case Spell::GrowTrees:
        return Callee<Fn3>(kGrowTrees)(world, nullptr, player, ux, uy);
    case Spell::Spell10: case Spell::Spell12: case Spell::Spell16: case Spell::Spell17: case Spell::Spell24:
    case Spell::Spell25:
        return Callee<Fn4>(kSpell549f40)(world, nullptr, player, ux, uy, spell);
    case Spell::Spell11:
        return Callee<Fn3>(kSpell11)(world, nullptr, player, ux, uy);
    case Spell::Spell13:
        return Callee<Fn4>(kSpell13)(world, nullptr, player, ux, uy, a4);
    case Spell::Spell14:
        return Callee<Fn4>(kSpell14)(world, nullptr, player, ux, uy, a4);
    case Spell::Spell15:
        return Callee<Fn3>(kSpell15)(world, nullptr, player, ux, uy);
    case Spell::ConvertGoodsRoman: case Spell::ConvertGoodsEgyptian: case Spell::ConvertGoodsAsian:
    case Spell::ConvertGoodsAmazon:
        DebugLog(texts::kExecuteConvertGoods);
        return Callee<Fn3>(kConvert)(world, nullptr, player, ux, uy);
    case Spell::GodlyGoodsRoman: case Spell::GodlyGoodsEgyptian: case Spell::GodlyGoodsAsian:
    case Spell::GodlyGoodsAmazon:
        DebugLog(texts::kExecuteGodlyGoods);
        return Callee<Fn3>(kGodly)(world, nullptr, player, ux, uy);
    case Spell::GoodsBan:
        DebugLog(texts::kExecuteGoodsBan);
        return Callee<Fn4>(kGoodsB)(world, nullptr, player, ux, uy, a4);
    case Spell::GoodsSummon:
        DebugLog(texts::kExecuteGoodsSummon);
        return Callee<Fn4>(kGoodsS)(world, nullptr, player, ux, uy, a4);
    case Spell::TreeCurse:
        DebugLog(texts::kExecuteTreeCurse);
        return Callee<Fn3>(kTreeCut)(world, nullptr, player, ux, uy);
    case Spell::RevealMap:
        DebugLog(texts::kExecuteRevealMap);
        return Callee<Fn1>(kRevealMapSpell)(world, nullptr, player);
    default:
        return 0;
    }
}

S3RE_HOOK(0x005480b0, CGameData_ExecuteSpell, ExecuteFn, Original_CGameData_ExecuteSpell, CGameData_ExecuteSpell);
S3RE_HOOK(0x00549530, CGameData_SpellRevealSpot, SpellFn, Original_CGameData_SpellRevealSpot,
          CGameData_SpellRevealSpot);
S3RE_HOOK(0x005495d0, CGameData_SpellRevealMap, RevealMapFn, Original_CGameData_SpellRevealMap,
          CGameData_SpellRevealMap);
S3RE_HOOK(0x00549650, CGameData_SpellBurnTrees, SpellFn, Original_CGameData_SpellBurnTrees,
          CGameData_SpellBurnTrees);
S3RE_HOOK(0x00549700, CGameData_SpellAddFish, SpellFn, Original_CGameData_SpellAddFish, CGameData_SpellAddFish);
S3RE_HOOK(0x005497f0, CGameData_SpellHalveResources, SpellFn, Original_CGameData_SpellHalveResources,
          CGameData_SpellHalveResources);
S3RE_HOOK(0x005498a0, CGameData_SpellGrowTrees, SpellFn, Original_CGameData_SpellGrowTrees,
          CGameData_SpellGrowTrees);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(CGameData);

// A map with a ring table of 300 small offsets, interesting objects,
// terrain and resources, and players with enough mana now and then.
void SetUpSpellWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    CGameData* w = worlds.original();
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
    const std::int32_t savedReveal = g_revealMap;
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
        g_revealMap = reveal;
        stubs.SetWorld(worlds.original());
        Original_CGameData_SpellRevealMap(worlds.original(), nullptr, player);
        const std::int32_t expectedReveal = g_revealMap;
        const auto expectedCalls = stubs.Take();
        const auto expectedLogs = logs.Take();
        g_revealMap = reveal;
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, player);
        const std::int32_t actualReveal = g_revealMap;
        if (!S3RE_EXPECT_EQ(expectedReveal, actualReveal) || !S3RE_EXPECT(expectedCalls == stubs.Take()) ||
            !S3RE_EXPECT(expectedLogs == logs.Take()) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, reveal %d", i, player, reveal);
            break;
        }
    }
    g_revealMap = savedReveal;
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
    test::WorldPair worlds(ctx, offsetof(CGameData, map_size) + 4);
    test::CalleeStubs stubs;
    InstallHandlerStubs(stubs);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        const U size = 1 + ctx.rng().Below(768);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const U player = ctx.rng().Next32();
        const std::int32_t x = ctx.rng().Range(-50, static_cast<std::int32_t>(size) + 50);
        const std::int32_t y = ctx.rng().Range(-50, static_cast<std::int32_t>(size) + 50);
        const U a4 = ctx.rng().Next32();
        const U spell = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(34);
        stubs.SetWorld(worlds.original());
        Original_CGameData_ExecuteSpell(worlds.original(), nullptr, player, x, y, a4, spell);
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
U __fastcall WrongSpellGrowTrees(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    std::vector<std::uint8_t> before;
    for (U tx = 0; tx < world->map_size; ++tx) {
        for (U ty = 0; ty < world->map_size; ++ty) {
            before.push_back(world->tiles[tx][ty].object);
        }
    }
    CGameData_SpellGrowTrees(world, nullptr, player, x, y);
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
U __fastcall WrongExecuteSpell(CGameData* world, void*, U player, std::int32_t x, std::int32_t y, U a4, U spell) {
    constexpr U k13 = Value(Spell::Spell13);
    constexpr U k14 = Value(Spell::Spell14);
    const U swapped = spell == k13 ? k14 : spell == k14 ? k13 : spell;
    return CGameData_ExecuteSpell(world, nullptr, player, x, y, a4, swapped);
}

}  // namespace

S3RE_TEST(CGameData_SpellRevealSpot_MatchesOriginal) {
    CompareSpell(ctx, Original_CGameData_SpellRevealSpot, &CGameData_SpellRevealSpot, 200);
}
S3RE_TEST(CGameData_SpellBurnTrees_MatchesOriginal) {
    CompareSpell(ctx, Original_CGameData_SpellBurnTrees, &CGameData_SpellBurnTrees, 200);
}
S3RE_TEST(CGameData_SpellAddFish_MatchesOriginal) {
    CompareSpell(ctx, Original_CGameData_SpellAddFish, &CGameData_SpellAddFish, 200);
}
S3RE_TEST(CGameData_SpellHalveResources_MatchesOriginal) {
    CompareSpell(ctx, Original_CGameData_SpellHalveResources, &CGameData_SpellHalveResources, 200);
}
S3RE_TEST(CGameData_SpellGrowTrees_MatchesOriginal) {
    CompareSpell(ctx, Original_CGameData_SpellGrowTrees, &CGameData_SpellGrowTrees, 200);
}
S3RE_TEST(CGameData_SpellGrowTrees_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpell(probe, Original_CGameData_SpellGrowTrees, &WrongSpellGrowTrees, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellRevealMap_MatchesOriginal) {
    CompareRevealMap(ctx, &CGameData_SpellRevealMap, 200);
}
S3RE_TEST(CGameData_ExecuteSpell_MatchesOriginal) {
    CompareExecute(ctx, &CGameData_ExecuteSpell, 20000);
}
S3RE_TEST(CGameData_ExecuteSpell_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExecute(probe, &WrongExecuteSpell, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
