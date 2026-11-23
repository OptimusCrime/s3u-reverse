// Spells on goods ("god events", Volker\GodlyEvents.cpp, package S2):
// converting, creating and moving goods (spells 18..23 and 26..29). Called
// by CGameData::ExecuteSpell (0x5480b0, spells.cpp) through their
// addresses. HasManaForSpell (0x54b240) / PayForSpell (0x54b2b0) as for the
// other spells; areas from the ring table world+0x110a588 (the first
// g_spellRingCounts[spell] + 1 offsets, or g_spellRingCounts[spell] for ConvertGoods);
// g_spellMaxObjects[spell] is the most units moved.
//
// CGameData::SpellConvertGoods (0x00549bd0): (player, x, y) spells 18, 19,
//   20, 27 ("EventConvertGoods"), one per race (players[player].race 0..3;
//   other races: assert "bloedkopf", GodlyEvents.cpp line 1110, which
//   throws). DebugLog(max). Over the area (signed index, < area): on each
//   map tile (IsOnMap) with a pile (no building, amount > 0) of the race's
//   g_convertGoodsFrom good (Amazons also gems 28): up to 9 times while
//   units are left and fewer than max were converted: StealGoodFromPile;
//   if that worked, DropGoodNear(g_convertGoodsTo[race], 1, tile) and
//   SubtractPlayerCounter(owner of the pile's tile, good, 1), else
//   DebugLog("cannot steal good"). Then DebugLog(count) and two effects at
//   the tile (Make_543f70 0x5f, Make_543930 kind 12 0xd0031), both
//   started at g_gameTick + rand() % 8 (the C runtime's rand 0x61f802, not
//   the simulation RNG). Paid if any pile matched. ret 0xc.
//   Original bug (kept): an area tile off the map doesn't advance the loop
//   (0x549cd2 jumps past the increment), so casting it within the ring's
//   radius of the map edge loops forever.
// CGameData::SpellGodlyGoods (0x005499b0): (player, x, y) spells 21, 22,
//   23, 26 ("EventGodlyGoods"), one per race (assert line 1041 otherwise).
//   One Random() is thrown away; then 4 times: k = the first of the race's
//   8 cumulative chances g_godlyGoodsChance above Random() & 0xffff; amount
//   = (Random() & 0xffff) % ((max + 1) / 2) + max / 2 (max =
//   g_godlyGoodsMaxAmount, signed int arithmetic), at most 8;
//   DropGoodNear(g_godlyGoods[k], amount, x, y); two effects (Make_543930
//   kind 12 0xd0030, Make_543f70 0x62) at (x + rand() % 4 - 2, y + rand()
//   % 4 - 2), started at g_gameTick + rand() % 8 (rand calls in that
//   order: delay, x, y). Always paid. ret 0xc.
// CGameData::SpellGoodsBan (0x0054aa40): (player, x, y, settler) spell 28
//   ("EventGoodsBan"): DebugLog(max) first; over the area around the
//   settler's position, every pile (no building, amount > 0) of any owner
//   gives up to 9 units (while fewer than max moved): StealGoodFromPile,
//   DropGoodNear(good, 1, x, y), SubtractPlayerCounter(owner of the pile's
//   tile, good, 1), effect kind 9 at (x, y); failure: DebugLog("cannot
//   steal good (GoodsBan)"). Then DebugLog("ported: %u"). Paid if any pile
//   was found. ret 0x10.
// CGameData::SpellGoodsSummon (0x0054acd0): (player, x, y, settler) spell
//   29 ("EventGoodsSummon"): the reverse: piles on the caster's own tiles
//   (tile owner == player) around (x, y) are moved to the settler's
//   position (re-read per unit); the effects are at (x, y). Same logs, no
//   DebugLog before. ret 0x10.
// thiscall (ECX = world). EAX is not used by the callers.

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/goods.h"
#include "game/enums/spells.h"
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

using layout::MapEffect;
using layout::MapEffectTable;
using layout::MapOffset;
using layout::Pile;
using layout::Settler;
using layout::Tile;
using U = std::uint32_t;

constexpr std::uintptr_t kHasManaForSpell = 0x0054b240;
constexpr std::uintptr_t kPayForSpell = 0x0054b2b0;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::uintptr_t kStealGoodFromPile = 0x00505c20;
constexpr std::uintptr_t kDropGoodNear = 0x005046b0;
constexpr std::uintptr_t kSubtractPlayerCounter = 0x004ffb40;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kMakeMoving = 0x00543930;      // MapEffect::Make_543930
constexpr std::uintptr_t kMakeStationary = 0x00543f70;  // MapEffect::Make_543f70
constexpr std::uintptr_t kAddEffect = 0x005445e0;       // MapEffects::Add
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;
constexpr std::uintptr_t kCrtRand = 0x0061f802;  // the C runtime's rand(), not CGameData::Random
constexpr std::uintptr_t kCrtSrand = 0x0061f7f5;

using Fn0 = U(__fastcall*)(CGameData*, void*);
using Fn1 = U(__fastcall*)(CGameData*, void*, U);
using Fn2 = U(__fastcall*)(CGameData*, void*, U, U);
using Fn3 = U(__fastcall*)(CGameData*, void*, U, U, U);
using Fn4 = U(__fastcall*)(CGameData*, void*, U, U, U, U);
using MakeMovingFn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U, U, U, U);
using MakeStationaryFn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U);
using AddFn = U(__fastcall*)(MapEffectTable* self, void* edx, const MapEffect* effect);
using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);
using RandFn = int(__cdecl*)();
using SrandFn = void(__cdecl*)(unsigned);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

constexpr U kEffectKindSettler = 9;
constexpr U kEffectKindChange = 12;
constexpr U kEffectSettlerGraphics = 0xd0031;  // raw-ok: graphics (group << 16 | index), not an offset
constexpr U kEffectChangeGraphics = 0xd0030;   // raw-ok: graphics, not an offset
constexpr U kConvertSparkle = 0x5f;  // Make_543f70 graphics of ConvertGoods
constexpr U kGodlySparkle = 0x62;    // Make_543f70 graphics of GodlyGoods
constexpr int kEffectDelays = 8;     // rand() % 8 ticks
constexpr int kScatter = 4;          // GodlyGoods effects at x + rand() % 4 - 2
constexpr int kScatterShift = 2;

constexpr U kUnitsPerPile = 9;       // a pile gives at most 9 units (k <= 8)
constexpr U kGodlyDrops = 4;
constexpr std::int32_t kGodlyKinds = 8;
constexpr std::int32_t kGodlyMaxAmount = 8;
constexpr std::int32_t kConvertAssertLine = 0x456;  // 1110
constexpr std::int32_t kGodlyAssertLine = 0x411;    // 1041

bool HasMana(CGameData* world, U player, U spell) {
    return (Callee<Fn2>(kHasManaForSpell)(world, nullptr, player, spell) & 0xff) != 0;
}

void Pay(CGameData* world, U player, U spell) {
    Callee<Fn2>(kPayForSpell)(world, nullptr, player, spell);
}

U Random(CGameData* world) {
    return Callee<Fn0>(kRandom)(world, nullptr);
}

int CrtRand() {
    return Callee<RandFn>(kCrtRand)();
}

void* Tick(U tick) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(tick));
}

void AddEffect(CGameData* world, const MapEffect& effect) {
    Callee<AddFn>(kAddEffect)(&world->map_effects, nullptr, &effect);
}

// A moving map effect (Make_543930) at the screen position of (x, y);
// TileScreenY is called first, as in the original.
void SpawnMovingEffect(CGameData* world, U kind, U x, U y, U graphics, U tick) {
    const U sy = Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
    const U sx = Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
    MapEffect effect;
    Callee<MakeMovingFn>(kMakeMoving)(&effect, Tick(tick), kind, sx, sy, x, y, graphics);
    AddEffect(world, effect);
}

void SpawnStationaryEffect(CGameData* world, U x, U y, U graphics, U tick) {
    const U sy = Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
    const U sx = Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
    MapEffect effect;
    Callee<MakeStationaryFn>(kMakeStationary)(&effect, Tick(tick), sx, sy, graphics);
    AddEffect(world, effect);
}

// The race's spell of a per-race group, or asserts like the original
// (Win32::ThrowAssertmentFailed throws a C++ exception; it never returns).
bool SpellOfRace(std::int32_t race, const U (&spells)[4], std::int32_t assertLine, U& spell) {
    if (race < 0 || race > 3) {  // the original compares unsigned (ja)
        Callee<AssertFn>(kThrowAssertmentFailed)(texts::kGodlyEventsAssert.c_str(), texts::kGodlyEventsFile.c_str(),
                                                 assertLine);
        return false;
    }
    spell = spells[race];
    return true;
}

// The pile on a tile that a goods spell may take from: not a building's,
// with units. 0 if none.
U PileOn(CGameData* world, U x, U y) {
    const Tile& tile = world->tiles[x][y];
    const U pile = tile.object_number;
    if (pile == 0 || (tile.flags & layout::kTileHasBuilding) != 0) {
        return 0;
    }
    return world->piles[pile].amount != 0 ? pile : 0;
}

// Takes one unit from the pile (StealGoodFromPile); true if that worked,
// with the unit's good (read before the call) in `good`.
bool StealUnit(CGameData* world, U pile, U& good) {
    good = world->piles[pile].good;
    return (Callee<Fn1>(kStealGoodFromPile)(world, nullptr, pile) & 0xff) != 0;
}

// The owner of the pile's tile loses the unit from its tallies.
void SubtractFromOwner(CGameData* world, U pile, U good) {
    const Pile& p = world->piles[pile];
    const U owner = world->tiles[p.x][p.y].owner;
    Callee<Fn3>(kSubtractPlayerCounter)(world, nullptr, owner, good, 1);
}

}  // namespace

using Spell3Fn = U(__fastcall*)(CGameData* world, void* edx, U player, U x, U y);
using Spell4Fn = U(__fastcall*)(CGameData* world, void* edx, U player, U x, U y, U a4);

// Purpose: Spells 18, 19, 20 and 27 (one per race): turns the goods of one kind around a spot into a
// more valuable kind (e.g. Roman iron into gold).
U __fastcall CGameData_SpellConvertGoods(CGameData* world, void*, U player, U x, U y) {
    static constexpr U kSpells[4] = {Value(Spell::ConvertGoodsRoman), Value(Spell::ConvertGoodsEgyptian),
                                     Value(Spell::ConvertGoodsAsian), Value(Spell::ConvertGoodsAmazon)};
    const std::int32_t race = world->players[player].race;
    U spell = 0;
    if (!SpellOfRace(race, kSpells, kConvertAssertLine, spell)) {
        return 0;
    }
    if (!HasMana(world, player, spell)) {
        return 0;
    }
    GameDebugLog(texts::kConvertGoodsMax, g_spellMaxObjects[spell]);
    bool done = false;
    U count = 0;
    std::int32_t i = 0;
    while (i < g_spellRingCounts[spell]) {
        if (count >= static_cast<U>(g_spellMaxObjects[spell])) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const U tx = o.dx + x;
        const U ty = o.dy + y;
        if (Callee<Fn2>(kIsOnMap)(world, nullptr, tx, ty) == 0) {
            // Original bug (0x549cd2): i is not advanced, so this repeats
            // forever; kept for identical behaviour.
            continue;
        }
        const U pile = PileOn(world, tx, ty);
        const U kind = pile != 0 ? world->piles[pile].good : 0;
        const bool convertible =
            pile != 0 && (kind == static_cast<U>(g_convertGoodsFrom[race]) ||
                          (static_cast<Race>(race) == Race::Amazon && kind == Value(Good::Gems)));
        if (convertible) {
            done = true;
            for (U k = 0; count < static_cast<U>(g_spellMaxObjects[spell]); ) {
                if (world->piles[pile].amount == 0 || k >= kUnitsPerPile) {
                    break;
                }
                ++k;
                U good = 0;
                if (StealUnit(world, pile, good)) {
                    Callee<Fn4>(kDropGoodNear)(world, nullptr, static_cast<U>(g_convertGoodsTo[race]), 1, tx, ty);
                    SubtractFromOwner(world, pile, good);
                    ++count;
                } else {
                    GameDebugLog(texts::kCannotStealGood);
                }
            }
            GameDebugLog(texts::kGoodStolenAndAdded, count);
            const U tick = g_gameTick + static_cast<U>(CrtRand() % kEffectDelays);
            SpawnStationaryEffect(world, tx, ty, kConvertSparkle, tick);
            SpawnMovingEffect(world, kEffectKindChange, tx, ty, kEffectSettlerGraphics, tick);
        }
        ++i;
    }
    if (done) {
        Pay(world, player, spell);
    }
    return 0;
}

// Purpose: Spells 21, 22, 23 and 26 (one per race): the gods drop four random heaps of goods at a spot.
U __fastcall CGameData_SpellGodlyGoods(CGameData* world, void*, U player, U x, U y) {
    static constexpr U kSpells[4] = {Value(Spell::GodlyGoodsRoman), Value(Spell::GodlyGoodsEgyptian),
                                     Value(Spell::GodlyGoodsAsian), Value(Spell::GodlyGoodsAmazon)};
    const std::int32_t race = world->players[player].race;
    U spell = 0;
    if (!SpellOfRace(race, kSpells, kGodlyAssertLine, spell)) {
        return 0;
    }
    if (!HasMana(world, player, spell)) {
        return 0;
    }
    Random(world);  // 0x549a33: the first number is thrown away
    for (U n = 0; n < kGodlyDrops; ++n) {
        const auto chance = static_cast<std::int32_t>(Random(world) & 0xffff);
        std::int32_t k = 0;
        while (k < kGodlyKinds && chance >= g_godlyGoodsChance[race * kGodlyKinds + k]) {
            ++k;
        }
        // k stays below 8: the last chance of every race is 65536.
        const std::int32_t good = g_godlyGoods[race * kGodlyKinds + k];
        const std::int32_t most = g_godlyGoodsMaxAmount[race * kGodlyKinds + k];
        const auto roll = static_cast<std::int32_t>(Random(world) & 0xffff);
        std::int32_t amount = roll % ((most + 1) / 2) + most / 2;
        if (amount > kGodlyMaxAmount) {
            amount = kGodlyMaxAmount;
        }
        Callee<Fn4>(kDropGoodNear)(world, nullptr, static_cast<U>(good), static_cast<U>(amount), x, y);
        const int delay = CrtRand() % kEffectDelays;
        const U ex = x + static_cast<U>(CrtRand() % kScatter - kScatterShift);
        const U ey = y + static_cast<U>(CrtRand() % kScatter - kScatterShift);
        const U tick = g_gameTick + static_cast<U>(delay);
        SpawnMovingEffect(world, kEffectKindChange, ex, ey, kEffectChangeGraphics, tick);
        SpawnStationaryEffect(world, ex, ey, kGodlySparkle, tick);
    }
    Pay(world, player, spell);
    return 0;
}

namespace {

// Spells 28 and 29 (0x54ab44..0x54ac6f / 0x54adb3..0x54aefc): moves up to 9
// units of the pile, each dropped where `drop` says (read per unit), with an
// effect at (x, y). Stops at the spell's limit.
template <typename Drop>
void MovePileUnits(CGameData* world, U pile, U spell, U& count, U x, U y, Drop drop) {
    if (count >= static_cast<U>(g_spellMaxObjects[spell])) {
        return;
    }
    U k = 0;
    do {
        if (world->piles[pile].amount == 0 || k >= kUnitsPerPile) {
            return;
        }
        ++k;
        U good = 0;
        if (StealUnit(world, pile, good)) {
            U toX = 0;
            U toY = 0;
            drop(toX, toY);
            Callee<Fn4>(kDropGoodNear)(world, nullptr, good, 1, toX, toY);
            SubtractFromOwner(world, pile, good);
            ++count;
            SpawnMovingEffect(world, kEffectKindSettler, x, y, kEffectSettlerGraphics, g_gameTick);
        } else {
            GameDebugLog(texts::kCannotStealGoodBan);
        }
    } while (count < static_cast<U>(g_spellMaxObjects[spell]));
}

}  // namespace

// Purpose: Spell 28 (Amazon): carries off the goods lying around a (probably enemy) settler to a spot.
U __fastcall CGameData_SpellGoodsBan(CGameData* world, void*, U player, U x, U y, U settler) {
    constexpr U kSpell = Value(Spell::GoodsBan);
    GameDebugLog(texts::kGoodsBanMax, g_spellMaxObjects[kSpell]);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    U count = 0;
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= static_cast<U>(g_spellMaxObjects[kSpell])) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const Settler& centre = world->settlers[settler];
        const U tx = centre.x + o.dx;
        const U ty = centre.y + o.dy;
        if (Callee<Fn2>(kIsOnMap)(world, nullptr, tx, ty) == 0) {
            continue;
        }
        const U pile = PileOn(world, tx, ty);
        if (pile == 0) {
            continue;
        }
        done = true;
        MovePileUnits(world, pile, kSpell, count, x, y, [&](U& toX, U& toY) {
            toX = x;
            toY = y;
        });
    }
    GameDebugLog(texts::kGoodsPorted, count);
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 29 (Amazon): fetches the caster's goods lying around a spot to one of its settlers.
U __fastcall CGameData_SpellGoodsSummon(CGameData* world, void*, U player, U x, U y, U settler) {
    constexpr U kSpell = Value(Spell::GoodsSummon);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    U count = 0;
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= static_cast<U>(g_spellMaxObjects[kSpell])) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const U tx = o.dx + x;
        const U ty = o.dy + y;
        if (Callee<Fn2>(kIsOnMap)(world, nullptr, tx, ty) == 0) {
            continue;
        }
        const U pile = PileOn(world, tx, ty);
        if (pile == 0 || world->tiles[tx][ty].owner != player) {
            continue;
        }
        done = true;
        MovePileUnits(world, pile, kSpell, count, x, y, [&](U& toX, U& toY) {
            const Settler& target = world->settlers[settler];  // re-read per unit (0x54ae31)
            toX = target.x;
            toY = target.y;
        });
    }
    GameDebugLog(texts::kGoodsPorted, count);
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

S3RE_HOOK(0x00549bd0, CGameData_SpellConvertGoods, Spell3Fn, Original_CGameData_SpellConvertGoods,
          CGameData_SpellConvertGoods);
S3RE_HOOK(0x005499b0, CGameData_SpellGodlyGoods, Spell3Fn, Original_CGameData_SpellGodlyGoods,
          CGameData_SpellGodlyGoods);
S3RE_HOOK(0x0054aa40, CGameData_SpellGoodsBan, Spell4Fn, Original_CGameData_SpellGoodsBan, CGameData_SpellGoodsBan);
S3RE_HOOK(0x0054acd0, CGameData_SpellGoodsSummon, Spell4Fn, Original_CGameData_SpellGoodsSummon,
          CGameData_SpellGoodsSummon);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(CGameData);
constexpr U kTestPiles = 100;
constexpr U kTestSettlers = 50;
// Area offsets reach 1 + 200 / 30 = 7 tiles: ConvertGoods is cast at least
// that far from the edge (it would hang otherwise, as the original).
constexpr U kRingReach = 8;

constexpr std::uint8_t kTestGoods[] = {11, 8, 2, 10, 28, 1, 3, 24, 0xff};

void SetUpGoodsWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    CGameData* w = worlds.original();
    const U size = 40 + rng.Below(60);
    w->map_size = size;
    for (U n = 0; n < 1001; ++n) {
        const auto d = static_cast<std::int32_t>(1 + n / 30);
        w->offsets[n].dx = rng.Range(-d, d);
        w->offsets[n].dy = rng.Range(-d, d);
    }
    for (U x = 0; x < size; ++x) {
        for (U y = 0; y < size; ++y) {
            Tile& t = w->tiles[x][y];
            t.object_number = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 1 + rng.Below(kTestPiles) : 0);
            t.flags = static_cast<std::uint8_t>(rng.Chance(1, 5) ? layout::kTileHasBuilding : rng.Below(0x20));
            t.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(8));
        }
    }
    for (U n = 0; n <= kTestPiles; ++n) {
        Pile& p = w->piles[n];
        p.good = kTestGoods[rng.Below(sizeof(kTestGoods))];
        p.amount = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0 : 1 + rng.Below(8));
        p.x = static_cast<std::uint16_t>(rng.Below(size));
        p.y = static_cast<std::uint16_t>(rng.Below(size));
    }
    for (U n = 0; n <= kTestSettlers; ++n) {
        w->settlers[n].x = static_cast<std::uint16_t>(rng.Below(size));
        w->settlers[n].y = static_cast<std::uint16_t>(rng.Below(size));
    }
    for (U p = 0; p < layout::kPlayers; ++p) {
        layout::Player& player = w->players[p];
        player.race = static_cast<std::int32_t>(rng.Below(4));
        player.mana = static_cast<std::int32_t>(rng.Below(100));
        for (std::int32_t& m : player.spell_mana) {
            m = static_cast<std::int32_t>(rng.Below(60));
        }
        player.promotion_cost = static_cast<std::int32_t>(rng.Below(60));
    }
    std::memset(&w->map_effects, 0, sizeof(w->map_effects));
    w->map_effects.blocks_x = 30;
    w->map_effects.blocks_y = 30;
    w->map_effects.slots_used = static_cast<std::int32_t>(rng.Below(40));
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kWorld);
}

bool SameWorlds(test::Context& ctx, test::WorldPair& worlds) {
    const std::size_t base = offsetof(CGameData, map_effects);
    for (std::size_t slot = 0; slot < 5000; ++slot) {
        for (std::size_t pad : {0x2a, 0x2b, 0x3b, 0x3e, 0x3f}) {
            worlds.originalBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
            worlds.oursBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
        }
    }
    return worlds.Checkpoint(ctx);
}

// StealGoodFromPile and DropGoodNear (verified, but they need consistent
// carriers and piles) are stubs: a steal fails for some piles (AL = 0 with
// a non-zero EAX) and takes one unit otherwise.
void InstallGoodsStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kStealGoodFromPile>([](CGameData* w, U, const U* args) -> U {
        Pile& p = w->piles[args[0]];
        if ((p.amount + p.x + p.y) % 4 == 0) {
            return 0x100;
        }
        --p.amount;
        return 1;
    });
    stubs.Install<4, kDropGoodNear>();
}

template <typename Call>
void CompareGoodsSpells(test::Context& ctx, int cases, bool inner, Call call) {
    test::WorldPair worlds(ctx, kWorld);
    test::CalleeStubs stubs;
    InstallGoodsStubs(stubs);
    DebugLogRecorder logs;
    const U savedTick = g_gameTick;
    for (int i = 0; i < cases; ++i) {
        if (i % 20 == 0) {
            SetUpGoodsWorld(ctx, worlds);
        }
        const U size = worlds.original()->map_size;
        const U player = ctx.rng().Below(8);
        U x = ctx.rng().Below(size);
        U y = ctx.rng().Below(size);
        if (inner) {
            x = kRingReach + ctx.rng().Below(size - 2 * kRingReach);
            y = kRingReach + ctx.rng().Below(size - 2 * kRingReach);
        } else if (ctx.rng().Chance(1, 10)) {
            x = static_cast<U>(ctx.rng().Range(-20, static_cast<std::int32_t>(size) + 20));
        }
        const U a4 = ctx.rng().Below(kTestSettlers + 1);
        const U seed = ctx.rng().Next32();
        g_gameTick = ctx.rng().Next32();
        Callee<SrandFn>(kCrtSrand)(seed);
        stubs.SetWorld(worlds.original());
        call(true, worlds.original(), player, x, y, a4);
        const auto expectedCalls = stubs.Take();
        const auto expectedLogs = logs.Take();
        const int expectedRand = CrtRand();
        Callee<SrandFn>(kCrtSrand)(seed);
        stubs.SetWorld(worlds.ours());
        call(false, worlds.ours(), player, x, y, a4);
        const int actualRand = CrtRand();
        if (!S3RE_EXPECT(expectedCalls == stubs.Take()) || !S3RE_EXPECT(expectedLogs == logs.Take()) ||
            !S3RE_EXPECT_EQ(expectedRand, actualRand) || !SameWorlds(ctx, worlds)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u at (%d, %d), settler %u", i, player,
                     static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), a4);
            break;
        }
    }
    g_gameTick = savedTick;
}

void CompareSpell3(test::Context& ctx, Spell3Fn originalFn, Spell3Fn candidate, int cases, bool inner) {
    CompareGoodsSpells(ctx, cases, inner, [&](bool original, CGameData* w, U player, U x, U y, U) {
        (original ? originalFn : candidate)(w, nullptr, player, x, y);
    });
}

void CompareSpell4(test::Context& ctx, Spell4Fn originalFn, Spell4Fn candidate, int cases) {
    CompareGoodsSpells(ctx, cases, false, [&](bool original, CGameData* w, U player, U x, U y, U a4) {
        (original ? originalFn : candidate)(w, nullptr, player, x, y, a4);
    });
}

// Wrong: Egyptian GodlyGoods drops one unit less.
U __fastcall WrongSpellGodlyGoods(CGameData* world, void*, U player, U x, U y) {
    const bool egyptian = world->players[player].race == Value(Race::Egyptian);
    if (egyptian) {
        world->players[player].race = Value(Race::Roman);
    }
    CGameData_SpellGodlyGoods(world, nullptr, player, x, y);
    if (egyptian) {
        world->players[player].race = Value(Race::Egyptian);
    }
    return 0;
}

// Wrong: GoodsSummon takes the goods of every owner.
U __fastcall WrongSpellGoodsSummon(CGameData* world, void*, U player, U x, U y, U settler) {
    return CGameData_SpellGoodsBan(world, nullptr, player, x, y, settler);
}

}  // namespace

S3RE_TEST(CGameData_SpellConvertGoods_MatchesOriginal) {
    CompareSpell3(ctx, Original_CGameData_SpellConvertGoods, &CGameData_SpellConvertGoods, 300, true);
}
S3RE_TEST(CGameData_SpellGodlyGoods_MatchesOriginal) {
    CompareSpell3(ctx, Original_CGameData_SpellGodlyGoods, &CGameData_SpellGodlyGoods, 300, false);
}
S3RE_TEST(CGameData_SpellGodlyGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpell3(probe, Original_CGameData_SpellGodlyGoods, &WrongSpellGodlyGoods, 300, false);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellGoodsBan_MatchesOriginal) {
    CompareSpell4(ctx, Original_CGameData_SpellGoodsBan, &CGameData_SpellGoodsBan, 300);
}
S3RE_TEST(CGameData_SpellGoodsSummon_MatchesOriginal) {
    CompareSpell4(ctx, Original_CGameData_SpellGoodsSummon, &CGameData_SpellGoodsSummon, 300);
}
S3RE_TEST(CGameData_SpellGoodsSummon_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpell4(probe, Original_CGameData_SpellGoodsSummon, &WrongSpellGoodsSummon, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
