// The military AI's battle-spell casters (computer players A2, session 53;
// Rainer\CompMapWorkerAgent.cpp; re/computer_players.md). AI::CastBattleSpells
// (0x5953b0) calls one of them per mage of a computer player, with the number
// of enemy soldiers near the mage (0x58a2c0) and a minimum. Each picks a
// target and casts through AI::SendCastSpell (0x590990, thiscall on
// g_aiCommandSender), with the battle-spell slot k of the player's race
// (0 .. 2; AI::InitBattleSpellSlots 0x590870 maps it to the spell).
//
// All are stdcall (player, mage, enemies, minimum), ret 0x10. Callers ignore
// EAX (CastBattleSpells reloads it right after each call, 0x595778), so ours
// return nothing.
//
// The "settler near the mage" search (the same code in seven casters): the
// first 500 map offsets of g_aiMapWorld (offsets[i].dx, .dy) added to the
// mage's position; both coordinates must pass AI::IsOnMap (0x572090, on
// g_aiWorld's map size); the tile's settler (0 = none) must belong to the
// player (own) or to anybody else (enemy) and be of the wanted types. The
// first such settler is the target: the spell is cast at its position.
// Soldiers are the types 5, 8, 15, 30..35 (swordsmen, bowmen, pikemen of the
// three levels), bowmen the types 8, 30, 33.
//
//   0x005957c0 AI::CastRomanSpell10: not in campaign mission 67, enemies >
//              minimum: an enemy soldier, slot 0.
//   0x00595910 AI::CastRomanSpell11: not in campaign mission 67, enemies >= 5
//              (minimum unused): an enemy soldier, slot 1.
//   0x00595a60 AI::CastEgyptianSpell12: enemies > minimum: an own soldier,
//              slot 0.
//   0x00595bb0 AI::CastEgyptianSpell13: enemies >= 4 (minimum unused): a
//              random map position, slot 1. Two CGameData::Random calls on
//              g_aiMapWorld, each (value & 0xffff) % map size (signed): the
//              first is y, the second x.
//   0x00595c10 AI::CastEgyptianSpell15: enemies > minimum: an enemy bowman,
//              slot 2.
//   0x00595d30 AI::CastAsianSpell16: enemies > minimum: an own soldier,
//              slot 0.
//   0x00595e80 AI::CastAsianSpell17: enemies > minimum: an own soldier,
//              slot 1.
//   0x00595fd0 AI::CastAsianSpell14: enemies > minimum and the summed health
//              (Settler::health) of the members of the player's unit groups
//              1..9 (player_lists; lengths <= 0 count nothing) is at most a
//              third (signed, rounded toward zero) of their summed start
//              health (g_settlerTypeStartHealth by type, not range-checked):
//              slot 2 at the player's start position (Player::start_x/_y).
//   0x005960d0 AI::CastAmazonSpell24: enemies >= 5 (minimum unused): an
//              enemy soldier, slot 0.
//   0x00596210 AI::CastAmazonSpell25: enemies > minimum: an enemy bowman,
//              slot 1.
//
// Spell meanings (re/computer_players_naming.md): 10 enemy soldiers deal half
// damage, 11 enemy units change sides, 12 own soldiers double damage, 13
// teleports enemies, 14 gathers own units, 15 enemy bowmen become pioneers,
// 16 own soldiers' damage * 3/4, 17 the strong hit.

#include "game/ai/ai_layout.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kSendCastSpell = 0x00590990;  // AI::SendCastSpell (thiscall, 5 args)
constexpr std::uintptr_t kIsOnMap = 0x00572090;        // AI::IsOnMap (fastcall)
constexpr std::uintptr_t kRandom = 0x004feb70;         // CGameData::Random (thiscall)

constexpr std::int32_t kMissionWithoutRomanSpells = 67;  // campaign mission in which the Romans cast nothing
constexpr std::size_t kSearchOffsets = 500;              // map offsets tried around the mage
constexpr std::int32_t kManyEnemies = 5;                 // 0x595910, 0x5960d0: enemies needed
constexpr std::int32_t kTeleportEnemies = 4;             // 0x595bb0: enemies needed
constexpr std::int32_t kHealthShareDivisor = 3;          // 0x595fd0: cast at a third of the health
constexpr std::size_t kFirstGroup = 1;                   // 0x595fd0 sums the groups 1..9

enum class Slot : std::uint32_t { First = 0, Second = 1, Third = 2 };
enum class Side { Own, Enemy };

using SendCastSpellFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                                   std::uint32_t mage, std::uint32_t slot, std::uint32_t x,
                                                   std::uint32_t y);
using IsOnMapFn = bool(__fastcall*)(std::int32_t v);
using RandomFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

bool IsSoldier(std::uint8_t type) {
    switch (static_cast<SettlerType>(type)) {
    case SettlerType::Swordsman1: case SettlerType::Bowman1: case SettlerType::Pikeman1:
    case SettlerType::Bowman2: case SettlerType::Pikeman2: case SettlerType::Swordsman2:
    case SettlerType::Bowman3: case SettlerType::Pikeman3: case SettlerType::Swordsman3:
        return true;
    default:
        return false;
    }
}

bool IsBowman(std::uint8_t type) {
    const auto kind = static_cast<SettlerType>(type);
    return kind == SettlerType::Bowman1 || kind == SettlerType::Bowman2 || kind == SettlerType::Bowman3;
}

void CastAt(std::uint32_t player, std::uint32_t mage, Slot slot, std::uint32_t x, std::uint32_t y) {
    reinterpret_cast<SendCastSpellFn>(kSendCastSpell)(&g_aiCommandSender.get(), nullptr, player, mage, Value(slot),
                                                      x, y);
}

// The search shared by seven casters: the first settler of the wanted side
// and types among the first 500 map offsets around the mage; 0 if none.
std::uint32_t FindSettlerNear(std::uint32_t player, std::uint32_t mage, Side side, bool (*wanted)(std::uint8_t)) {
    const auto isOnMap = reinterpret_cast<IsOnMapFn>(kIsOnMap);
    const CGameData* world = g_aiMapWorld;
    const layout::Settler& caster = world->settlers[mage];  // (not range-checked)
    const std::int32_t mageX = caster.x, mageY = caster.y;
    for (std::size_t i = 0; i < kSearchOffsets; ++i) {
        world = g_aiMapWorld;
        const std::int32_t x = world->offsets[i].dx + mageX;
        const std::int32_t y = world->offsets[i].dy + mageY;
        if (!isOnMap(x) || !isOnMap(y)) {
            continue;
        }
        world = g_aiMapWorld;
        const std::uint32_t id = (&world->tiles[0][0])[x * kMapRowTiles + y].settler;
        if (id == 0) {
            continue;
        }
        const layout::Settler& settler = world->settlers[id];
        if ((settler.owner == player) != (side == Side::Own)) {
            continue;
        }
        if (wanted(settler.art)) {
            return id;
        }
    }
    return 0;
}

// Casts the slot's spell at the first wanted settler near the mage.
void CastAtSettlerNear(std::uint32_t player, std::uint32_t mage, Side side, bool (*wanted)(std::uint8_t), Slot slot) {
    const std::uint32_t target = FindSettlerNear(player, mage, side, wanted);
    if (target == 0) {
        return;
    }
    const layout::Settler& settler = g_aiMapWorld.get()->settlers[target];
    CastAt(player, mage, slot, settler.x, settler.y);
}

}  // namespace

using CasterFn = void(__stdcall*)(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                  std::int32_t minimum);

void __stdcall AI_CastRomanSpell10(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum);
void __stdcall AI_CastRomanSpell11(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum);
void __stdcall AI_CastEgyptianSpell12(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t minimum);
void __stdcall AI_CastEgyptianSpell13(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t minimum);
void __stdcall AI_CastEgyptianSpell15(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t minimum);
void __stdcall AI_CastAsianSpell16(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum);
void __stdcall AI_CastAsianSpell17(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum);
void __stdcall AI_CastAsianSpell14(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum);
void __stdcall AI_CastAmazonSpell24(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t minimum);
void __stdcall AI_CastAmazonSpell25(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t minimum);

S3RE_HOOK(0x005957c0, AI_CastRomanSpell10, CasterFn, Original_AI_CastRomanSpell10, AI_CastRomanSpell10);
S3RE_HOOK(0x00595910, AI_CastRomanSpell11, CasterFn, Original_AI_CastRomanSpell11, AI_CastRomanSpell11);
S3RE_HOOK(0x00595a60, AI_CastEgyptianSpell12, CasterFn, Original_AI_CastEgyptianSpell12, AI_CastEgyptianSpell12);
S3RE_HOOK(0x00595bb0, AI_CastEgyptianSpell13, CasterFn, Original_AI_CastEgyptianSpell13, AI_CastEgyptianSpell13);
S3RE_HOOK(0x00595c10, AI_CastEgyptianSpell15, CasterFn, Original_AI_CastEgyptianSpell15, AI_CastEgyptianSpell15);
S3RE_HOOK(0x00595d30, AI_CastAsianSpell16, CasterFn, Original_AI_CastAsianSpell16, AI_CastAsianSpell16);
S3RE_HOOK(0x00595e80, AI_CastAsianSpell17, CasterFn, Original_AI_CastAsianSpell17, AI_CastAsianSpell17);
S3RE_HOOK(0x00595fd0, AI_CastAsianSpell14, CasterFn, Original_AI_CastAsianSpell14, AI_CastAsianSpell14);
S3RE_HOOK(0x005960d0, AI_CastAmazonSpell24, CasterFn, Original_AI_CastAmazonSpell24, AI_CastAmazonSpell24);
S3RE_HOOK(0x00596210, AI_CastAmazonSpell25, CasterFn, Original_AI_CastAmazonSpell25, AI_CastAmazonSpell25);

// Purpose: A Roman computer player's mage weakens nearby enemy soldiers (they
// deal half damage) when enough enemies are close.
void __stdcall AI_CastRomanSpell10(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum) {
    if (g_campaignMission == kMissionWithoutRomanSpells || enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::First);
}

// Purpose: A Roman computer player's mage turns nearby enemy units to its
// side when at least five enemies are close.
void __stdcall AI_CastRomanSpell11(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t /*minimum*/) {
    if (g_campaignMission == kMissionWithoutRomanSpells || enemies < kManyEnemies) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::Second);
}

// Purpose: An Egyptian computer player's mage doubles the damage of its own
// soldiers near it when enough enemies are close.
void __stdcall AI_CastEgyptianSpell12(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t minimum) {
    if (enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Own, &IsSoldier, Slot::First);
}

// Purpose: An Egyptian computer player's mage casts its teleport spell at a
// random map position when at least four enemies are close (probably meant
// to scatter them).
void __stdcall AI_CastEgyptianSpell13(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t /*minimum*/) {
    if (enemies < kTeleportEnemies) {
        return;
    }
    const auto random = reinterpret_cast<RandomFn>(kRandom);
    // Each draw: (Random & 0xffff) % map size, signed (0x595bbd..0x595be6).
    const auto draw = [&] {
        const auto value = static_cast<std::int32_t>(random(g_aiMapWorld, nullptr) & 0xffff);
        return static_cast<std::uint32_t>(value % static_cast<std::int32_t>(g_aiMapWorld.get()->map_size));
    };
    const std::uint32_t y = draw();  // the first draw is y
    const std::uint32_t x = draw();
    CastAt(player, mage, Slot::Second, x, y);
}

// Purpose: An Egyptian computer player's mage turns a nearby enemy bowman
// into a pioneer when enough enemies are close.
void __stdcall AI_CastEgyptianSpell15(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                      std::int32_t minimum) {
    if (enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Enemy, &IsBowman, Slot::Third);
}

// Purpose: An Asian computer player's mage strengthens its own soldiers near
// it when enough enemies are close.
void __stdcall AI_CastAsianSpell16(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum) {
    if (enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Own, &IsSoldier, Slot::First);
}

// Purpose: An Asian computer player's mage gives its own soldiers near it the
// strong hit when enough enemies are close.
void __stdcall AI_CastAsianSpell17(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum) {
    if (enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Own, &IsSoldier, Slot::Second);
}

// Purpose: An Asian computer player's mage gathers its units at the start
// position when enough enemies are close and its unit groups have lost two
// thirds of their health.
void __stdcall AI_CastAsianSpell14(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                   std::int32_t minimum) {
    const CGameData* world = g_aiMapWorld;
    std::uint32_t startHealth = 0;  // (both sums wrap like the original's dwords)
    std::uint32_t health = 0;
    for (std::size_t group = kFirstGroup; group < kUnitGroups; ++group) {
        // (player not range-checked: the rows run on through the world)
        const std::int32_t length = (&world->player_list_lengths[0][0])[player * kUnitGroups + group];
        const std::uint16_t* members = &world->player_lists[0][0][0] + (player * kUnitGroups + group) * 100;
        for (std::int32_t i = 0; i < length; ++i) {
            const layout::Settler& settler = world->settlers[members[i]];
            startHealth += static_cast<std::uint32_t>((&g_settlerTypeStartHealth[0])[settler.art]);
            health += settler.health;
        }
    }
    if (enemies <= minimum ||
        static_cast<std::int32_t>(health) > static_cast<std::int32_t>(startHealth) / kHealthShareDivisor) {
        return;
    }
    const layout::Player& owner = (&world->players[0])[player];
    CastAt(player, mage, Slot::Third, static_cast<std::uint32_t>(owner.start_x),
           static_cast<std::uint32_t>(owner.start_y));
}

// Purpose: An Amazon computer player's mage casts its first battle spell at a
// nearby enemy soldier when at least five enemies are close.
void __stdcall AI_CastAmazonSpell24(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t /*minimum*/) {
    if (enemies < kManyEnemies) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::First);
}

// Purpose: An Amazon computer player's mage casts its second battle spell at
// a nearby enemy bowman when enough enemies are close.
void __stdcall AI_CastAmazonSpell25(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t minimum) {
    if (enemies <= minimum) {
        return;
    }
    CastAtSettlerNear(player, mage, Side::Enemy, &IsBowman, Slot::Second);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// SendCastSpell and Random are stubbed (both versions call them through their
// S3.EXE addresses): the stub logs every cast (ECX and the five arguments),
// Random returns values from the case's list. A small map (size 40..80) with
// settlers of all sides and types around the mage, small map offsets, unit
// groups of mixed health, and the campaign mission often 67.

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::uint32_t kTestSettlers = 400;
constexpr std::int32_t kOffsetRange = 15;
constexpr int kCasters = 10;

struct Casters {
    CasterFn f[kCasters];
};

Casters OriginalCasters() {
    return {{Original_AI_CastRomanSpell10, Original_AI_CastRomanSpell11, Original_AI_CastEgyptianSpell12,
             Original_AI_CastEgyptianSpell13, Original_AI_CastEgyptianSpell15, Original_AI_CastAsianSpell16,
             Original_AI_CastAsianSpell17, Original_AI_CastAsianSpell14, Original_AI_CastAmazonSpell24,
             Original_AI_CastAmazonSpell25}};
}

Casters OurCasters() {
    return {{&AI_CastRomanSpell10, &AI_CastRomanSpell11, &AI_CastEgyptianSpell12, &AI_CastEgyptianSpell13,
             &AI_CastEgyptianSpell15, &AI_CastAsianSpell16, &AI_CastAsianSpell17, &AI_CastAsianSpell14,
             &AI_CastAmazonSpell24, &AI_CastAmazonSpell25}};
}

// Soldiers, bowmen and a few others, so every type test has hits and misses.
constexpr std::uint8_t kTestTypes[] = {5, 8, 15, 30, 31, 32, 33, 34, 35, 0, 3, 36, 4, 9, 29, 255};

void SetUpWorld(test::Rng& rng, CGameData* world) {
    world->map_size = 40 + rng.Below(41);
    for (std::size_t i = 0; i < kSearchOffsets + 10; ++i) {
        world->offsets[i].dx = rng.Range(-kOffsetRange, kOffsetRange);
        world->offsets[i].dy = rng.Range(-kOffsetRange, kOffsetRange);
    }
    const std::uint32_t density = 2 + rng.Below(30);  // settlers per 100 tiles
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].settler =
                static_cast<std::uint16_t>(rng.Below(100) < density ? 1 + rng.Below(kTestSettlers - 1) : 0);
        }
    }
    for (std::uint32_t id = 0; id < kTestSettlers; ++id) {
        layout::Settler& settler = world->settlers[id];
        settler.owner = static_cast<std::uint8_t>(rng.Chance(1, 10) ? 0xff : rng.Below(4));
        settler.art = kTestTypes[rng.Below(sizeof(kTestTypes))];
        settler.health = static_cast<std::uint8_t>(rng.Next32());
        settler.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    for (std::uint32_t player = 0; player < 4; ++player) {
        world->players[player].start_x = static_cast<std::int32_t>(rng.Next32());
        world->players[player].start_y = static_cast<std::int32_t>(rng.Next32());
        // health of most members (start health 100..150): often near a third
        const std::uint32_t healthy = rng.Chance(1, 2) ? 25 + rng.Below(30) : rng.Below(256);
        for (std::size_t group = 0; group < kUnitGroups; ++group) {
            world->player_list_lengths[player][group] =
                rng.Chance(1, 8) ? -static_cast<std::int32_t>(rng.Below(3)) : static_cast<std::int32_t>(rng.Below(6));
            for (std::uint16_t& member : world->player_lists[player][group]) {  // mostly the player's soldiers
                member = static_cast<std::uint16_t>(rng.Chance(9, 10) ? player * 100 + rng.Below(100)
                                                                       : rng.Below(kTestSettlers));
            }
        }
        for (std::uint32_t id = player * 100; id < player * 100 + 100; ++id) {
            layout::Settler& member = world->settlers[id];
            member.health = static_cast<std::uint8_t>(rng.Chance(7, 8) ? healthy : rng.Below(256));
            member.art = rng.Chance(9, 10) ? kTestTypes[rng.Below(9)] : member.art;
        }
    }
}

// Returns how many cases cast per caster (to check that the inputs reach the casts).
std::vector<int> CompareCasters(test::Context& ctx, const Casters& candidate, int cases) {
    std::vector<int> casts(kCasters);
    const Casters original = OriginalCasters();
    CGameData* savedWorld = g_aiWorld;
    CGameData* savedMapWorld = g_aiMapWorld;
    const std::int32_t savedMission = g_campaignMission;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    g_aiMapWorld = world;

    std::uint32_t randoms[2] = {};
    int randomCall = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<5, kSendCastSpell>();
    stubs.Install<0, kRandom>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return randoms[randomCall++ & 1];
    });

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 40 == 0) {
            SetUpWorld(rng, world);
        }
        const int op = static_cast<int>(rng.Below(kCasters));
        const std::uint32_t player = rng.Below(4);
        const std::uint32_t mage = rng.Below(kTestSettlers);
        world->settlers[mage].x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        world->settlers[mage].y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        const std::int32_t enemies = rng.Range(-1, 9);
        const std::int32_t minimum = rng.Range(-1, 6);
        g_campaignMission = rng.Chance(1, 4) ? kMissionWithoutRomanSpells : static_cast<std::int32_t>(rng.Below(80));
        randoms[0] = rng.Next32();
        randoms[1] = rng.Next32();

        randomCall = 0;
        original.f[op](player, mage, enemies, minimum);
        const std::vector<test::StubCall> callsA = stubs.Take();
        randomCall = 0;
        candidate.f[op](player, mage, enemies, minimum);
        const std::vector<test::StubCall> callsB = stubs.Take();
        casts[op] += callsA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT_EQ(callsA.size(), callsB.size());
        for (std::size_t k = 0; same && k < callsA.size(); ++k) {
            same &= S3RE_EXPECT(callsA[k] == callsB[k]);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, caster %d, player %u, mage %u, enemies %d, minimum %d", i, op,
                     player, mage, enemies, minimum);
        }
    }
    g_aiWorld = savedWorld;
    g_aiMapWorld = savedMapWorld;
    g_campaignMission = savedMission;
    return casts;
}

void __stdcall WrongAmazonSpell24(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                  std::int32_t minimum) {
    // Wrong: compares with the minimum instead of 5.
    if (enemies > minimum) {
        CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::First);
    }
}

void __stdcall WrongRomanSpell10(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                 std::int32_t minimum) {
    // Wrong: ignores the campaign mission.
    if (enemies > minimum) {
        CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::First);
    }
}

void __stdcall WrongEgyptianSpell15(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t minimum) {
    // Wrong: any enemy soldier instead of a bowman.
    if (enemies > minimum) {
        CastAtSettlerNear(player, mage, Side::Enemy, &IsSoldier, Slot::Third);
    }
}

void __stdcall WrongEgyptianSpell13(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                    std::int32_t /*minimum*/) {
    // Wrong: the first draw is x.
    if (enemies < kTeleportEnemies) {
        return;
    }
    const auto random = reinterpret_cast<RandomFn>(kRandom);
    const auto size = static_cast<std::int32_t>(g_aiMapWorld.get()->map_size);
    const auto x = static_cast<std::uint32_t>(static_cast<std::int32_t>(random(g_aiMapWorld, nullptr) & 0xffff) % size);
    const auto y = static_cast<std::uint32_t>(static_cast<std::int32_t>(random(g_aiMapWorld, nullptr) & 0xffff) % size);
    CastAt(player, mage, Slot::Second, x, y);
}

void __stdcall WrongAsianSpell14(std::uint32_t player, std::uint32_t mage, std::int32_t enemies,
                                 std::int32_t minimum) {
    // Wrong: also counts group 0.
    const CGameData* world = g_aiMapWorld;
    std::uint32_t startHealth = 0, health = 0;
    for (std::size_t group = 0; group < kUnitGroups; ++group) {
        for (std::int32_t i = 0; i < world->player_list_lengths[player][group]; ++i) {
            const layout::Settler& settler = world->settlers[world->player_lists[player][group][i]];
            startHealth += static_cast<std::uint32_t>((&g_settlerTypeStartHealth[0])[settler.art]);
            health += settler.health;
        }
    }
    if (enemies > minimum && static_cast<std::int32_t>(health) <= static_cast<std::int32_t>(startHealth) / 3) {
        CastAt(player, mage, Slot::Third, static_cast<std::uint32_t>(world->players[player].start_x),
               static_cast<std::uint32_t>(world->players[player].start_y));
    }
}

}  // namespace

S3RE_TEST(AiBattleSpellCasters_MatchOriginal) {
    const std::vector<int> casts = CompareCasters(ctx, OurCasters(), 12000);
    for (int op = 0; op < kCasters; ++op) {
        if (!S3RE_EXPECT(casts[op] >= 40)) {
            ctx.Fail(__FILE__, __LINE__, "  caster %d cast only %d times", op, casts[op]);
        }
    }
}

S3RE_TEST(AiBattleSpellCasters_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](int op, CasterFn wrong) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        Casters casters = OurCasters();
        casters.f[op] = wrong;
        CompareCasters(quiet, casters, 3000);
        if (!S3RE_EXPECT(quiet.failures() > 0)) {
            ctx.Fail(__FILE__, __LINE__, "  wrong caster %d not detected", op);
        }
    };
    probe(0, &WrongRomanSpell10);
    probe(3, &WrongEgyptianSpell13);
    probe(4, &WrongEgyptianSpell15);
    probe(7, &WrongAsianSpell14);
    probe(8, &WrongAmazonSpell24);
}

}  // namespace s3re::game
