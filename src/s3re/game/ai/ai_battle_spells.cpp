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
// The driver and the slot table:
//
//   0x005953b0 AI::CastBattleSpells(player) thiscall (on g_aiGroupOrders,
//              passed on as the casters' unused ECX), ret 4: walks the
//              player's mages (Player::settler_lists[Mage], linked through
//              Settler::next). Per mage, by its own race (Settler::race):
//              bit k set if the player's mana (Player::mana) is at least
//              (unsigned) Player::spell_mana[battle_spells[race][k]] (slots
//              0, 1 for Romans and Amazons, 0..2 for Egyptians and Asians).
//              With any bit, the enemy soldiers within 20 of the mage
//              (0x58a2c0 on g_aiEnemySearch), then the race's casters in a
//              fixed order: a caster whose bit is set gets a
//              CGameData::Random draw and runs (player, mage, enemies,
//              minimum 0) if the draw's low word is above 25000; otherwise
//              the next one is tried. Romans: bit 2 -> 11, bit 1 -> 10.
//              Egyptians: 2 -> 13, 4 -> 15, 1 -> 12. Asians: 4 -> 14, 2 ->
//              17, 1 -> 16. Amazons: 2 -> 24, 1 -> 25, an original bug
//              (kept): the slot-1 mana check starts the caster of slot 0
//              and the other way round. EAX: callers ignore it (RunMilitary
//              0x58f990 passes it on to T_ATTACK_AGENT::Run, whose result
//              RunAgents ignores). A mage of a race above 3 reads its bits
//              from the original's stack (the return address, an argument,
//              ...); at most that leads to a call of the side-effect-free
//              enemy count and no cast, so ours skips such mages.
//   0x00590870 AI::InitBattleSpellSlots() thiscall (on g_aiCommandSender):
//              per race and battle spell the index of the spell in the
//              race's row of g_raceSpells (0x658d64, 8 per race): byte 0, 1
//              Roman spells 10, 11; 3, 4, 5 Egyptian 12, 13, 15; 6, 7, 8
//              Asian 16, 17, 14; 9, 10 Amazon 24, 25. A spell not in the
//              row leaves its byte alone (so do bytes 2 and 11). Callers
//              (RunMilitary's reset, AI::LoadState) ignore EAX.
//
// Spell meanings (re/computer_players_naming.md): 10 enemy soldiers deal half
// damage, 11 enemy units change sides, 12 own soldiers double damage, 13
// teleports enemies, 14 gathers own units, 15 enemy bowmen become pioneers,
// 16 own soldiers' damage * 3/4, 17 the strong hit.

#include "game/ai/ai_layout.h"
#include "game/enums/races.h"
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
constexpr std::uintptr_t kCountEnemySoldiersNear = 0x0058a2c0;  // AI::CountEnemySoldiersNear (thiscall, 4 args)

// The casters, called through their S3.EXE addresses by CastBattleSpells.
constexpr std::uintptr_t kRomanSpell10 = 0x005957c0;
constexpr std::uintptr_t kRomanSpell11 = 0x00595910;
constexpr std::uintptr_t kEgyptianSpell12 = 0x00595a60;
constexpr std::uintptr_t kEgyptianSpell13 = 0x00595bb0;
constexpr std::uintptr_t kEgyptianSpell15 = 0x00595c10;
constexpr std::uintptr_t kAsianSpell16 = 0x00595d30;
constexpr std::uintptr_t kAsianSpell17 = 0x00595e80;
constexpr std::uintptr_t kAsianSpell14 = 0x00595fd0;
constexpr std::uintptr_t kAmazonSpell24 = 0x005960d0;
constexpr std::uintptr_t kAmazonSpell25 = 0x00596210;

constexpr std::int32_t kMissionWithoutRomanSpells = 67;  // campaign mission in which the Romans cast nothing
constexpr std::size_t kSearchOffsets = 500;              // map offsets tried around the mage
constexpr std::int32_t kManyEnemies = 5;                 // 0x595910, 0x5960d0: enemies needed
constexpr std::int32_t kTeleportEnemies = 4;             // 0x595bb0: enemies needed
constexpr std::int32_t kHealthShareDivisor = 3;          // 0x595fd0: cast at a third of the health
constexpr std::size_t kFirstGroup = 1;                   // 0x595fd0 sums the groups 1..9

constexpr std::uint32_t kEnemySearchDistance = 20;  // 0x5955b8: enemy soldiers counted within 20 of the mage
constexpr std::uint16_t kCastDrawAbove = 25000;     // 0x5955ee: a cast needs a draw above it (about 24 %)
constexpr std::int32_t kNoMinimum = 0;              // CastBattleSpells passes minimum 0

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
void CastAtSettlerNear(std::uint32_t player, std::uint32_t mage, Side side, bool (*wanted)(std::uint8_t),
                       Slot slot) {
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

namespace {

// A caster CastBattleSpells may start, with the mana bit that allows it.
struct CastOption {
    std::uint32_t bit;
    std::uintptr_t caster;
};

// Per race, in the order tried (0x5955dc..0x595773).
constexpr CastOption kRomanOptions[] = {{2, kRomanSpell11}, {1, kRomanSpell10}};
constexpr CastOption kEgyptianOptions[] = {{2, kEgyptianSpell13}, {4, kEgyptianSpell15}, {1, kEgyptianSpell12}};
constexpr CastOption kAsianOptions[] = {{4, kAsianSpell14}, {2, kAsianSpell17}, {1, kAsianSpell16}};
// Original bug, kept: the slot-1 bit starts the slot-0 caster and the other
// way round (0x59572e..0x595773).
constexpr CastOption kAmazonOptions[2] = {{2, kAmazonSpell24}, {1, kAmazonSpell25}};

struct RaceOptions {
    const CastOption* options;
    std::size_t count;
    std::size_t slots;  // battle-spell slots whose mana is checked
};

constexpr RaceOptions kRaceOptions[kRaces] = {
    {kRomanOptions, 2, 2}, {kEgyptianOptions, 3, 3}, {kAsianOptions, 3, 3}, {kAmazonOptions, 2, 2}};

// Bit k: the player's mana pays the spell of battle-spell slot k
// (0x59543b..0x595596).
std::uint32_t AffordableSlots(const layout::Player& owner, std::uint8_t race, std::size_t slots) {
    const AiCommandSender& sender = g_aiCommandSender;
    const auto mana = static_cast<std::uint32_t>(owner.mana);
    std::uint32_t bits = 0;
    for (std::size_t k = 0; k < slots; ++k) {
        // (the spell number is not range-checked)
        const auto cost = static_cast<std::uint32_t>((&owner.spell_mana[0])[sender.battle_spells[race][k]]);
        if (mana >= cost) {
            bits |= 1u << k;
        }
    }
    return bits;
}

using EnemyCountFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                               std::uint32_t distance, std::uint32_t player);

// One mage's turn (0x5953e7..0x595773).
void CastWithMage(void* self, std::uint32_t player, std::uint32_t mage) {
    const CGameData* world = g_aiMapWorld;
    const layout::Settler& caster = world->settlers[mage];
    const std::uint32_t x = caster.x, y = caster.y;
    const std::uint8_t race = caster.race;
    if (race >= kRaces) {
        return;  // (the original reads stack leftovers here; see the file header)
    }
    const RaceOptions& raceOptions = kRaceOptions[race];
    const std::uint32_t bits = AffordableSlots((&world->players[0])[player], race, raceOptions.slots);
    if (bits == 0) {
        return;
    }
    const std::int32_t enemies = reinterpret_cast<EnemyCountFn>(kCountEnemySoldiersNear)(
        &g_aiEnemySearch.get(), nullptr, x, y, kEnemySearchDistance, player);
    const auto random = reinterpret_cast<RandomFn>(kRandom);
    for (std::size_t i = 0; i < raceOptions.count; ++i) {
        const CastOption& option = raceOptions.options[i];
        if ((bits & option.bit) == 0) {
            continue;
        }
        if (static_cast<std::uint16_t>(random(g_aiMapWorld, nullptr)) > kCastDrawAbove) {
            // The casters ignore ECX; the original passes its own `this`.
            using CasterThisFn = void(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t mage,
                                                   std::int32_t enemies, std::int32_t minimum);
            reinterpret_cast<CasterThisFn>(option.caster)(self, nullptr, player, mage, enemies, kNoMinimum);
            return;
        }
    }
}

// Which spell each byte of AiCommandSender::battle_spells looks up.
struct SlotSpell {
    std::size_t race;
    std::size_t slot;
    std::uint32_t spell;
};

constexpr SlotSpell kSlotSpells[] = {
    {0, 0, 10}, {0, 1, 11},              // Romans
    {1, 0, 12}, {1, 1, 13}, {1, 2, 15},  // Egyptians
    {2, 0, 16}, {2, 1, 17}, {2, 2, 14},  // Asians
    {3, 0, 24}, {3, 1, 25},              // Amazons
};

constexpr std::size_t kSpellsPerRace = 8;

}  // namespace

using CastBattleSpellsFn = void(__fastcall*)(void* self, void* edx, std::uint32_t player);
using InitSlotsFn = void(__fastcall*)(AiCommandSender* self, void* edx);

void __fastcall AI_CastBattleSpells(void* self, void* edx, std::uint32_t player);
void __fastcall AI_InitBattleSpellSlots(AiCommandSender* self, void* edx);

S3RE_HOOK(0x005953b0, AI_CastBattleSpells, CastBattleSpellsFn, Original_AI_CastBattleSpells, AI_CastBattleSpells);
S3RE_HOOK(0x00590870, AI_InitBattleSpellSlots, InitSlotsFn, Original_AI_InitBattleSpellSlots,
          AI_InitBattleSpellSlots);

// Purpose: Lets every mage of a computer player cast one of its race's battle
// spells that the player can pay for, now and then (each try about one in
// four), when enemies are near.
void __fastcall AI_CastBattleSpells(void* self, void* /*edx*/, std::uint32_t player) {
    const CGameData* world = g_aiMapWorld;
    std::uint32_t mage = (&world->players[0])[player].settler_lists[Value(SettlerType::Mage)];
    while (mage != 0) {
        CastWithMage(self, player, mage);
        world = g_aiMapWorld;
        mage = world->settlers[mage].next;
    }
}

// Purpose: Finds, per race, where its battle spells stand in the race's spell
// list (the index the CastSpell command takes); run when the military AI
// starts or loads its state.
void __fastcall AI_InitBattleSpellSlots(AiCommandSender* self, void* /*edx*/) {
    for (const SlotSpell& entry : kSlotSpells) {
        for (std::size_t i = 0; i < kSpellsPerRace; ++i) {
            if (g_raceSpells[entry.race][i] == entry.spell) {
                self->battle_spells[entry.race][entry.slot] = static_cast<std::uint8_t>(i);
                break;
            }
        }
    }
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
                rng.Chance(1, 8) ? -static_cast<std::int32_t>(rng.Below(3))
                                 : static_cast<std::int32_t>(rng.Below(6));
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
        g_campaignMission =
            rng.Chance(1, 4) ? kMissionWithoutRomanSpells : static_cast<std::int32_t>(rng.Below(80));
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
    const auto draw = [&] {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(random(g_aiMapWorld, nullptr) & 0xffff) % size);
    };
    const std::uint32_t x = draw();
    const std::uint32_t y = draw();
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

// --- CastBattleSpells and InitBattleSpellSlots -----------------------------
//
// CastBattleSpells: the ten casters and Random are stubbed (the log holds
// every caster call with its ECX and arguments and every Random call, in
// order); the enemy count runs (verified) on soldier lists of the other
// players. Mages of races 0..3 (sometimes higher), mana near the spells'
// costs, battle-spell slots 0..9. InitBattleSpellSlots: entries of
// g_raceSpells are patched for the case (other spells, duplicates, missing
// spells).

namespace {

constexpr std::uintptr_t kCasterAddresses[kCasters] = {
    kRomanSpell10, kRomanSpell11, kEgyptianSpell12, kEgyptianSpell13, kEgyptianSpell15,
    kAsianSpell16, kAsianSpell17, kAsianSpell14,    kAmazonSpell24,   kAmazonSpell25};

void SetUpBattle(test::Rng& rng, CGameData* world, std::uint32_t player) {
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        layout::Player& record = world->players[p];
        record.team = static_cast<std::int32_t>(rng.Below(3));
        for (std::uint16_t& head : record.settler_lists) {
            head = 0;
        }
        // one soldier list per player: 0..3 settlers near the mages
        const std::uint32_t first = 200 + p * 4;
        const std::uint32_t length = rng.Below(4);
        record.settler_lists[Value(SettlerType::Swordsman1)] = static_cast<std::uint16_t>(length ? first : 0);
        for (std::uint32_t k = 0; k < length; ++k) {
            layout::Settler& soldier = world->settlers[first + k];
            soldier.x = static_cast<std::uint16_t>(rng.Below(60));
            soldier.y = static_cast<std::uint16_t>(rng.Below(60));
            soldier.next = static_cast<std::uint16_t>(k + 1 < length ? first + k + 1 : 0);
        }
    }
    layout::Player& owner = world->players[player];
    owner.mana = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(200));
    for (std::int32_t& cost : owner.spell_mana) {
        cost = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(200));
    }
    owner.promotion_cost = static_cast<std::int32_t>(rng.Below(200));  // (read as a cost by slot 8)
    owner.soldier_levels[0] = static_cast<std::int32_t>(rng.Below(200));  // (slot 9)
    const std::uint32_t mages = rng.Chance(1, 8) ? 0 : 1 + rng.Below(5);
    std::uint16_t* link = &owner.settler_lists[Value(SettlerType::Mage)];
    for (std::uint32_t k = 0; k < mages; ++k) {
        const auto id = static_cast<std::uint16_t>(1 + k * 19 + rng.Below(19));
        *link = id;
        layout::Settler& mage = world->settlers[id];
        mage.race = static_cast<std::uint8_t>(rng.Chance(1, 10) ? 4 + rng.Below(252) : rng.Below(4));
        mage.x = static_cast<std::uint16_t>(rng.Below(60));
        mage.y = static_cast<std::uint16_t>(rng.Below(60));
        link = &mage.next;
    }
    *link = 0;
    AiCommandSender& sender = g_aiCommandSender.get();
    for (auto& race : sender.battle_spells) {
        for (std::uint8_t& slot : race) {
            slot = static_cast<std::uint8_t>(rng.Chance(1, 10) ? 8 + rng.Below(2) : rng.Below(8));
        }
    }
}

// Returns how many casts the original made per caster (to check the inputs reach them).
std::vector<int> CompareCastBattleSpells(test::Context& ctx, CastBattleSpellsFn candidate, int cases) {
    std::vector<int> casts(kCasters);
    CGameData* savedMapWorld = g_aiMapWorld;
    const AiCommandSender savedSender = g_aiCommandSender;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiMapWorld = world;

    std::uint32_t randoms[16] = {};
    int randomCall = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kRandom>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return randoms[randomCall++ & 15];
    });
    stubs.Install<4, kRomanSpell10>();
    stubs.Install<4, kRomanSpell11>();
    stubs.Install<4, kEgyptianSpell12>();
    stubs.Install<4, kEgyptianSpell13>();
    stubs.Install<4, kEgyptianSpell15>();
    stubs.Install<4, kAsianSpell16>();
    stubs.Install<4, kAsianSpell17>();
    stubs.Install<4, kAsianSpell14>();
    stubs.Install<4, kAmazonSpell24>();
    stubs.Install<4, kAmazonSpell25>();

    void* self = &g_aiGroupOrders.get();
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t player = rng.Below(layout::kPlayers);
        SetUpBattle(rng, world, player);
        for (std::uint32_t& value : randoms) {  // often right at the threshold
            value = rng.Chance(1, 2) ? (rng.Next32() & 0xffff0000u) + 24999 + rng.Below(3) : rng.Next32();
        }
        randomCall = 0;
        Original_AI_CastBattleSpells(self, nullptr, player);
        const std::vector<test::StubCall> callsA = stubs.Take();
        randomCall = 0;
        candidate(self, nullptr, player);
        const std::vector<test::StubCall> callsB = stubs.Take();
        for (const test::StubCall& call : callsA) {
            for (int op = 0; op < kCasters; ++op) {
                casts[op] += call.callee == kCasterAddresses[op] ? 1 : 0;
            }
        }
        bool same = S3RE_EXPECT_EQ(callsA.size(), callsB.size());
        for (std::size_t k = 0; same && k < callsA.size(); ++k) {
            same &= S3RE_EXPECT(callsA[k] == callsB[k]);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %u", i, player);
        }
    }
    g_aiMapWorld = savedMapWorld;
    g_aiCommandSender.get() = savedSender;
    return casts;
}

// A CastBattleSpells with its own option tables and draw threshold, for the
// wrong-implementation checks.
void CastBattleSpellsWith(void* self, std::uint32_t player, const CastOption (&amazon)[2],
                          std::uint16_t drawAbove) {
    const CGameData* world = g_aiMapWorld;
    for (std::uint32_t mage = world->players[player].settler_lists[Value(SettlerType::Mage)]; mage != 0;
         mage = world->settlers[mage].next) {
        const layout::Settler& caster = world->settlers[mage];
        if (caster.race >= kRaces) {
            continue;
        }
        const RaceOptions raceOptions = caster.race == Value(Race::Amazon) ? RaceOptions{amazon, 2, 2}
                                                                           : kRaceOptions[caster.race];
        const std::uint32_t bits = AffordableSlots(world->players[player], caster.race, raceOptions.slots);
        if (bits == 0) {
            continue;
        }
        const std::int32_t enemies = reinterpret_cast<EnemyCountFn>(kCountEnemySoldiersNear)(
            &g_aiEnemySearch.get(), nullptr, caster.x, caster.y, kEnemySearchDistance, player);
        const auto random = reinterpret_cast<RandomFn>(kRandom);
        for (std::size_t k = 0; k < raceOptions.count; ++k) {
            const CastOption& option = raceOptions.options[k];
            if ((bits & option.bit) != 0 &&
                static_cast<std::uint16_t>(random(g_aiMapWorld, nullptr)) > drawAbove) {
                using CasterThisFn = void(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::int32_t,
                                                       std::int32_t);
                reinterpret_cast<CasterThisFn>(option.caster)(self, nullptr, player, mage, enemies, kNoMinimum);
                break;
            }
        }
    }
}

void __fastcall WrongCastBattleSpellsAmazons(void* self, void* /*edx*/, std::uint32_t player) {
    // Wrong: "fixes" the Amazons' pairing.
    constexpr CastOption kFixed[2] = {{2, kAmazonSpell25}, {1, kAmazonSpell24}};
    CastBattleSpellsWith(self, player, kFixed, kCastDrawAbove);
}

void __fastcall WrongCastBattleSpellsDraw(void* self, void* /*edx*/, std::uint32_t player) {
    // Wrong: a draw of exactly 25000 casts too.
    CastBattleSpellsWith(self, player, kAmazonOptions, kCastDrawAbove - 1);
}

// g_raceSpells, patched per case.
std::uintptr_t RaceSpellsAddress() { return reinterpret_cast<std::uintptr_t>(&g_raceSpells[0][0]); }

void CompareInitSlots(test::Context& ctx, InitSlotsFn candidate, int cases) {
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        std::vector<std::uintptr_t> patched;
        const std::uint32_t changes = rng.Below(8);
        for (std::uint32_t k = 0; k < changes; ++k) {
            const std::uint32_t entry = rng.Below(static_cast<std::uint32_t>(kRaces * kSpellsPerRace));
            const std::uintptr_t address = RaceSpellsAddress() + 4 * entry;
            bool already = false;
            for (const std::uintptr_t a : patched) {
                already |= a == address;
            }
            if (already) {
                continue;
            }
            const std::uint32_t spell = rng.Chance(1, 2) ? 10 + rng.Below(16) : rng.Below(40);
            test::PatchBytes(address, &spell, sizeof(spell));
            patched.push_back(address);
        }
        AiCommandSender a{}, b{};
        rng.Fill(&a, sizeof(a));
        b = a;
        Original_AI_InitBattleSpellSlots(&a, nullptr);
        candidate(&b, nullptr);
        for (auto it = patched.rbegin(); it != patched.rend(); ++it) {
            test::UnpatchBytes(*it);
        }
        if (!S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%u table entries changed)", i, changes);
        }
    }
}

void __fastcall WrongInitSlots(AiCommandSender* self, void* /*edx*/) {
    // Wrong: takes the last match.
    for (const SlotSpell& entry : kSlotSpells) {
        for (std::size_t i = 0; i < kSpellsPerRace; ++i) {
            if (g_raceSpells[entry.race][i] == entry.spell) {
                self->battle_spells[entry.race][entry.slot] = static_cast<std::uint8_t>(i);
            }
        }
    }
}

}  // namespace

S3RE_TEST(AiCastBattleSpells_MatchesOriginal) {
    const std::vector<int> casts = CompareCastBattleSpells(ctx, &AI_CastBattleSpells, 4000);
    for (int op = 0; op < kCasters; ++op) {
        if (!S3RE_EXPECT(casts[op] >= 40)) {
            ctx.Fail(__FILE__, __LINE__, "  caster %d started only %d times", op, casts[op]);
        }
    }
}

S3RE_TEST(AiCastBattleSpells_ComparisonDetectsWrongImplementation) {
    for (CastBattleSpellsFn wrong : {&WrongCastBattleSpellsAmazons, &WrongCastBattleSpellsDraw}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareCastBattleSpells(quiet, wrong, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

S3RE_TEST(AiInitBattleSpellSlots_MatchesOriginal) {
    CompareInitSlots(ctx, &AI_InitBattleSpellSlots, 400);
}

S3RE_TEST(AiInitBattleSpellSlots_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareInitSlots(quiet, &WrongInitSlots, 400);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
