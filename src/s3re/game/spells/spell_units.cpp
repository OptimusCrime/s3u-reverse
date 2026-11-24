// Spells on settlers ("god events", Volker\GodlyEvents.cpp, package S2):
// the spells 10..17, 24 and 25 that mark, convert, move or change soldiers
// and other units, and their two helpers. Called by CGameData::ExecuteSpell
// (0x5480b0, spells.cpp) through their addresses. Each spell checks
// HasManaForSpell (0x54b240) and pays with PayForSpell (0x54b2b0) when it
// affected anything. The area is the first g_spellRingCounts[spell] offsets of
// the ring table world+0x110a588 around a point; at most
// g_spellMaxObjects[spell] settlers are affected. The visual effect per
// settler is a map effect (Make_543930 kind 9 or 12 + MapEffects::Add)
// at the tile's screen position (TileScreenX/Y), started at g_gameTick
// (EDX of the make function).
//
// CGameData::IsSpellTarget (0x0054af60): (settler) -> EAX 1 if the
//   settler's type is a soldier (5, 8, 15, 30..35), geologist (22), thief
//   (23), mage (36) or pioneer (40) and its job is one of 0 (new), 0x35
//   (walking), 0x37 (idle), 0x8e, 0xb0, 0xd4/0xd5 (geologist work),
//   0x12e/0x12f (pioneer work); else 0. ret 4.
// CGameData::ReleaseSpellTarget (0x0054b000): (settler) lets go of what the
//   settler was doing before a spell changes it: geologists and pioneers
//   (types 22, 40) with a work target (+0x1c != 0xffff) unclaim their tile
//   (flag 2: the target tile for job 0x35, their own tile for jobs
//   0xd4/0xd5/0x12e/0x12f); jobs 0, 0x37, 0x8e, 0xd4, 0xd5, 0x12e, 0x12f get
//   counter 1; geologists and pioneers get +0x1c = 0xffff (dword) and +0x20
//   = 0xffff; a walking or idle unit (jobs 0x35, 0x37, 0x8e, 0xb0) of the
//   IsSpellTarget types leaves its player's idle list (prev/next +0x12/
//   +0x10, list head players[owner].settler_lists[type]). EAX = world +
//   settler * 64 (unused). ret 4.
// CGameData::SpellMarkSoldiers (0x00549f40): (player, x, y, spell) spells
//   10, 12, 16, 17, 24, 25: DebugLog("GOD: EventChangeSettler ..."); per
//   spell (jump table 0x54a1ac/0x54a1c8) a settler flag bit, enemy or own
//   team and a duration index: 10 bit 1 enemies, 12 bit 2 own, 17 bit 4 own,
//   16 bit 8 own, 24 bit 0x80 enemies, 25 bit 0x10 own (soldier.cpp: 1
//   halves the damage, 2 doubles it, 4 the strong hit, 8 three quarters).
//   Soldiers (class 1..9) on the area tiles (signed bounds checks) of the
//   chosen side get the bit, and a timed event (AllocTable36700Record type
//   3: settler, mask ~bit) takes it back after g_soldierSpellTicks[index]
//   ticks. Other spells: never passed (only caller ExecuteSpell). ret 0x10.
// CGameData::SpellConvertSettlers (0x0054a370): (player, x, y) spell 11:
//   spell targets of other teams around (x, y) are released, change owner
//   to the player and race to the player's race, lose their job
//   (ClearSettlerJob). In campaign mission 67, every converted Amazon
//   (race 3) adds (count + 1) & 0xfff to the player's +0x134. ret 0xc.
// CGameData::SpellTeleportEnemies (0x0054a7c0): (player, x, y, settler)
//   spell 13: spell targets around the given settler whose team differs
//   from that settler's owner's are moved to (x, y) (PlaceSettlerNear;
//   effects at the old and the new place, the second 5 ticks later). ret 0x10.
// CGameData::SpellGatherUnits (0x0054a540): (player, x, y, settler) spell
//   14: spell targets around (x, y) of the same team as the given
//   settler's owner are moved to that settler (as spell 13). ret 0x10.
// CGameData::SpellDisarmBowmen (0x0054a1e0): (player, x, y) spell 15:
//   enemy bowmen (types 8, 30, 33) that are spell targets around (x, y)
//   become type 40 (pioneers, effect kind 12). ret 0xc.
// Spells 11, 13, 14, 15 loop over offsets 0..area inclusive (unsigned) and
// check the map bounds unsigned; spell 10's group loops 0..area-1, signed.
// thiscall (ECX = world). The handlers' EAX is not used (ExecuteSpell's
// caller overwrites it).

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/spells.h"
#include "game/enums/job_ids.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
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
using layout::Settler;
using layout::Tile;
using U = std::uint32_t;

constexpr std::uintptr_t kHasManaForSpell = 0x0054b240;
constexpr std::uintptr_t kPayForSpell = 0x0054b2b0;
constexpr std::uintptr_t kAllocTimedEvent = 0x004fefd0;  // CGameData::AllocTable36700Record
constexpr std::uintptr_t kClearSettlerJob = 0x00510440;
constexpr std::uintptr_t kPlaceSettlerNear = 0x0050ce80;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kMakeEffect = 0x00543930;  // MapEffect::Make_543930
constexpr std::uintptr_t kAddEffect = 0x005445e0;   // MapEffects::Add
constexpr std::uintptr_t kIsSpellTarget = 0x0054af60;
constexpr std::uintptr_t kReleaseSpellTarget = 0x0054b000;

using Fn1 = U(__fastcall*)(CGameData*, void*, U);
using Fn2 = U(__fastcall*)(CGameData*, void*, U, U);
using Fn3 = U(__fastcall*)(CGameData*, void*, U, U, U);
using Fn10 = U(__fastcall*)(CGameData*, void*, U, U, U, U, U, U, U, U, U, U);
using MakeFn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U, U, U, U);
using AddFn = U(__fastcall*)(MapEffectTable* self, void* edx, const MapEffect* effect);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

// Timed event type 3 (UpdateEnvironment): settler flags &= mask when due.
constexpr U kTimedEventSettlerFlags = 3;
// Map effects (kind, graphics) of the spells.
constexpr U kEffectKindSettler = 9;
constexpr U kEffectKindChange = 12;
constexpr U kEffectSettlerGraphics = 0xd0031;  // raw-ok: graphics (group << 16 | index), not an offset
constexpr U kEffectChangeGraphics = 0xd0030;   // raw-ok: graphics, not an offset
constexpr U kArrivalDelay = 5;  // the effect at the new place starts 5 ticks later

constexpr std::int32_t kCampaignConvertMission = 67;  // counts converted Amazons
constexpr U kConvertCountMask = 0xfff;

// Tile::flags bit 2: a geologist's or pioneer's claimed tile.
constexpr std::uint8_t kTileClaimed = layout::kTileFlag02;
// Settler::saved_animation of geologists and pioneers: no work target.
constexpr std::uint32_t kNoWorkTarget = 0xffff;

constexpr std::int32_t kSoldierClassFirst = 1;
constexpr std::int32_t kSoldierClassLast = 9;

bool HasMana(CGameData* world, U player, U spell) {
    return (Callee<Fn2>(kHasManaForSpell)(world, nullptr, player, spell) & 0xff) != 0;
}

void Pay(CGameData* world, U player, U spell) {
    Callee<Fn2>(kPayForSpell)(world, nullptr, player, spell);
}

Tile& TileAt(CGameData* world, U x, U y) {
    return world->tiles[x][y];
}

std::int32_t TeamOf(CGameData* world, U player) {
    return world->players[player].team;
}

// One map effect (Make_543930 + Add) at the screen position of (x, y);
// TileScreenY is called first, as in the original.
void SpawnEffect(CGameData* world, U kind, U x, U y, U graphics, U tick) {
    const U sy = Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
    const U sx = Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
    MapEffect effect;
    Callee<MakeFn>(kMakeEffect)(&effect, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tick)), kind, sx, sy, x,
                                y, graphics);
    Callee<AddFn>(kAddEffect)(&world->map_effects, nullptr, &effect);
}

bool IsSpellTargetType(SettlerType type) {
    using T = SettlerType;
    switch (type) {
    case T::Swordsman1: case T::Bowman1: case T::Pikeman1: case T::Swordsman3: case T::Bowman2:
    case T::Pikeman2: case T::Pikeman3: case T::Bowman3: case T::Swordsman2: case T::Geologist:
    case T::Pioneer: case T::Thief: case T::Mage:
        return true;
    default:
        return false;
    }
}

bool HasWorkTarget(SettlerType type) {
    return type == SettlerType::Pioneer || type == SettlerType::Geologist;
}

bool IsGeologistWork(SettlerJob job) {
    return job == SettlerJob::GeologistWork || job == SettlerJob::GeologistDone;
}

bool IsPioneerWork(SettlerJob job) {
    return job == SettlerJob::PioneerWork || job == SettlerJob::PioneerDone;
}

// The jobs of units standing or walking around (not inside a building).
bool IsIdleUnitJob(SettlerJob job) {
    return job == SettlerJob::UnitIdle || job == SettlerJob::UnitWalk ||
           job == SettlerJob::UnitWalkCountdown || job == SettlerJob::UnitFighting;
}

}  // namespace

using SettlerFn = U(__fastcall*)(CGameData* world, void* edx, U settler);
using Spell3Fn = U(__fastcall*)(CGameData* world, void* edx, U player, U x, U y);
using Spell4Fn = U(__fastcall*)(CGameData* world, void* edx, U player, U x, U y, U a4);

// Purpose: Tells whether a spell can change a settler: soldiers and special units that are standing,
// walking or at their own work outside buildings.
U __fastcall CGameData_IsSpellTarget(CGameData* world, void*, U settler) {
    const Settler& s = world->settlers[settler];
    if (!IsSpellTargetType(TypeOf(s))) {
        return 0;
    }
    const SettlerJob job = s.job;
    return job == SettlerJob::GeologistWork || job == SettlerJob::GeologistDone ||
                   job == SettlerJob::PioneerWork || job == SettlerJob::PioneerDone ||
                   job == SettlerJob::New || job == SettlerJob::UnitIdle || job == SettlerJob::UnitWalk ||
                   job == SettlerJob::UnitFighting || job == SettlerJob::UnitWalkCountdown
               ? 1
               : 0;
}

// Purpose: Makes a settler drop what it was doing (claimed tiles, its place in the idle list) before a
// spell takes it over or changes it.
U __fastcall CGameData_ReleaseSpellTarget(CGameData* world, void*, U settler) {
    Settler& s = world->settlers[settler];
    const auto result = static_cast<U>(reinterpret_cast<std::uintptr_t>(world) + settler * sizeof(Settler));
    // 0x54b00f: a geologist's or pioneer's claimed tile.
    if (HasWorkTarget(TypeOf(s)) && s.saved_animation != kNoWorkTarget) {
        if (s.job == SettlerJob::UnitWalk) {
            TileAt(world, s.target_x, s.target_y).flags &= static_cast<std::uint8_t>(~kTileClaimed);
        }
        if (IsPioneerWork(s.job)) {
            TileAt(world, s.x, s.y).flags &= static_cast<std::uint8_t>(~kTileClaimed);
        }
        if (IsGeologistWork(s.job)) {
            TileAt(world, s.x, s.y).flags &= static_cast<std::uint8_t>(~kTileClaimed);
        }
    }
    // 0x54b0f7: the job continues at once.
    const SettlerJob job = s.job;
    if (IsGeologistWork(job) || IsPioneerWork(job) || job == SettlerJob::New || job == SettlerJob::UnitIdle ||
        job == SettlerJob::UnitFighting) {
        s.counter = 1;
    }
    if (HasWorkTarget(TypeOf(s))) {
        s.saved_animation = kNoWorkTarget;
        s.saved_counter = 0xffff;
    }
    // 0x54b15e: out of the player's list of units of its type.
    if (IsIdleUnitJob(s.job) && IsSpellTargetType(TypeOf(s))) {
        if (s.prev != 0) {
            world->settlers[s.prev].next = s.next;
        } else {
            world->players[s.owner].settler_lists[s.art] = s.next;
        }
        if (s.next != 0) {
            world->settlers[s.next].prev = s.prev;
        }
    }
    return result;
}

// Purpose: Spells 10, 12, 16, 17, 24 and 25: for a while, strengthens the caster's soldiers or weakens
// the enemy's around a spot (a settler flag that the fight code reads).
U __fastcall CGameData_SpellMarkSoldiers(CGameData* world, void*, U player, std::int32_t x, std::int32_t y,
                                         U spell) {
    GameDebugLog(texts::kChangeSettler, player, x, y, spell);
    struct Mark {
        std::uint8_t bit;
        bool ownTeam;
        U ticksIndex;  // into g_soldierSpellTicks
    };
    Mark mark{};
    switch (static_cast<Spell>(spell)) {
    case Spell::Spell10: mark = {layout::kSettlerHalfDamage, false, 0}; break;
    case Spell::Spell12: mark = {layout::kSettlerDoubleDamage, true, 1}; break;
    case Spell::Spell17: mark = {layout::kSettlerStrongHit, true, 2}; break;
    case Spell::Spell16: mark = {layout::kSettlerThreeQuarters, true, 3}; break;
    case Spell::Spell24: mark = {layout::kSettlerSpell24, false, 4}; break;
    case Spell::Spell25: mark = {layout::kSettlerSpotted, true, 5}; break;
    default:
        // 0x54a00a: never reached (ExecuteSpell passes only these six);
        // the original would go on with an uninitialized duration index.
        return 0;
    }
    if (!HasMana(world, player, spell)) {
        return 0;
    }
    bool done = false;
    std::int32_t count = 0;
    for (std::int32_t i = 0; i < g_spellRingCounts[spell]; ++i) {
        if (count >= g_spellMaxObjects[spell]) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const std::int32_t tx = o.dx + x;
        const std::int32_t ty = o.dy + y;
        const auto size = static_cast<std::int32_t>(world->map_size);
        if (tx < 0 || tx >= size || ty < 0 || ty >= size) {
            continue;
        }
        const U number = world->tiles[tx][ty].settler;
        if (number == 0) {
            continue;
        }
        Settler& s = world->settlers[number];
        const std::int32_t typeClass = g_settlerTypeClass[s.art];
        if (typeClass < kSoldierClassFirst || typeClass > kSoldierClassLast) {
            continue;
        }
        const bool sameTeam = TeamOf(world, s.owner) == TeamOf(world, player);
        if (sameTeam != mark.ownTeam) {
            continue;
        }
        s.flags |= mark.bit;
        done = true;
        const auto ticks = static_cast<U>(g_soldierSpellTicks[mark.ticksIndex]);
        Callee<Fn10>(kAllocTimedEvent)(world, nullptr, kTimedEventSettlerFlags, ticks, number, ~U{mark.bit}, 0, 0, 0,
                                       0, 0, 0);
        ++count;
    }
    if (done) {
        Pay(world, player, spell);
    }
    return 0;
}

// Purpose: Spell 11 (Roman): converts enemy soldiers and units around a spot to the caster's side.
U __fastcall CGameData_SpellConvertSettlers(CGameData* world, void*, U player, U x, U y) {
    constexpr U kSpell = Value(Spell::Spell11);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    std::int32_t count = 0;
    // Offsets 0..area inclusive, as the original (unsigned jbe at 0x54a511).
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= g_spellMaxObjects[kSpell]) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const U tx = o.dx + x;
        const U ty = o.dy + y;
        if (tx >= world->map_size || ty >= world->map_size) {
            continue;
        }
        const U number = world->tiles[tx][ty].settler;
        if (number == 0 || Callee<Fn1>(kIsSpellTarget)(world, nullptr, number) == 0) {
            continue;
        }
        Settler& s = world->settlers[number];
        layout::Player& caster = world->players[player];
        if (caster.team == TeamOf(world, s.owner)) {
            continue;
        }
        Callee<Fn1>(kReleaseSpellTarget)(world, nullptr, number);
        s.owner = static_cast<std::uint8_t>(player);
        if (g_campaignMission == kCampaignConvertMission && RaceOf(s) == Race::Amazon) {
            // mission_state (+0x134, read by CheckGameEnd 0x511c50), 32-bit wrapping add.
            const U added = (static_cast<U>(count) + 1) & kConvertCountMask;
            caster.mission_state = static_cast<std::int32_t>(static_cast<U>(caster.mission_state) + added);
        }
        s.race = static_cast<std::uint8_t>(caster.race);
        Callee<Fn1>(kClearSettlerJob)(world, nullptr, number);
        done = true;
        SpawnEffect(world, kEffectKindSettler, tx, ty, kEffectSettlerGraphics, g_gameTick);
        ++count;
    }
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

namespace {

// Spells 13 and 14 (0x54a8b8..0x54a9ef / 0x54a635..0x54a76c): moves the
// settler `number` to (toX, toY) read by `destination` after the first
// effect, with an effect where it leaves and one where it arrives.
template <typename Destination>
void MoveSpellTarget(CGameData* world, U number, Destination destination) {
    Callee<Fn1>(kReleaseSpellTarget)(world, nullptr, number);
    Settler& s = world->settlers[number];
    TileAt(world, s.x, s.y).settler = 0;
    SpawnEffect(world, kEffectKindSettler, s.x, s.y, kEffectSettlerGraphics, g_gameTick);
    U toX = 0;
    U toY = 0;
    destination(toX, toY);
    Callee<Fn3>(kPlaceSettlerNear)(world, nullptr, number, toX, toY);
    TileAt(world, s.x, s.y).settler = static_cast<std::uint16_t>(number);
    SpawnEffect(world, kEffectKindSettler, s.x, s.y, kEffectSettlerGraphics, g_gameTick + kArrivalDelay);
    Callee<Fn1>(kClearSettlerJob)(world, nullptr, number);
}

}  // namespace

// Purpose: Spell 13 (Egyptian): pulls the enemy units standing around one of the caster's settlers
// over to a chosen spot.
U __fastcall CGameData_SpellTeleportEnemies(CGameData* world, void*, U player, U x, U y, U centreSettler) {
    constexpr U kSpell = Value(Spell::Spell13);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    std::int32_t count = 0;
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= g_spellMaxObjects[kSpell]) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const Settler& centre = world->settlers[centreSettler];  // re-read every step, as the original
        const U tx = centre.x + o.dx;
        const U ty = centre.y + o.dy;
        if (tx >= world->map_size || ty >= world->map_size) {
            continue;
        }
        const U number = world->tiles[tx][ty].settler;
        if (number == 0 || Callee<Fn1>(kIsSpellTarget)(world, nullptr, number) == 0) {
            continue;
        }
        if (TeamOf(world, centre.owner) == TeamOf(world, world->settlers[number].owner)) {
            continue;
        }
        MoveSpellTarget(world, number, [&](U& toX, U& toY) {
            toX = x;
            toY = y;
        });
        done = true;
        ++count;
    }
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 14 (Asian): gathers the own team's units around a spot at one of the caster's
// settlers.
U __fastcall CGameData_SpellGatherUnits(CGameData* world, void*, U player, U x, U y, U centreSettler) {
    constexpr U kSpell = Value(Spell::Spell14);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    std::int32_t count = 0;
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= g_spellMaxObjects[kSpell]) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const U tx = o.dx + x;
        const U ty = o.dy + y;
        if (tx >= world->map_size || ty >= world->map_size) {
            continue;
        }
        const U number = world->tiles[tx][ty].settler;
        if (number == 0 || Callee<Fn1>(kIsSpellTarget)(world, nullptr, number) == 0) {
            continue;
        }
        const Settler& centre = world->settlers[centreSettler];
        if (TeamOf(world, centre.owner) != TeamOf(world, world->settlers[number].owner)) {
            continue;
        }
        // The destination is read after the first effect (0x54a6c2).
        MoveSpellTarget(world, number, [&](U& toX, U& toY) {
            toX = centre.x;
            toY = centre.y;
        });
        done = true;
        ++count;
    }
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

// Purpose: Spell 15 (Egyptian): turns the enemy bowmen around a spot into unarmed pioneers.
U __fastcall CGameData_SpellDisarmBowmen(CGameData* world, void*, U player, U x, U y) {
    constexpr U kSpell = Value(Spell::Spell15);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool done = false;
    std::int32_t count = 0;
    for (U i = 0; i <= static_cast<U>(g_spellRingCounts[kSpell]); ++i) {
        if (count >= g_spellMaxObjects[kSpell]) {
            break;
        }
        const MapOffset& o = world->offsets[i];
        const U tx = o.dx + x;
        const U ty = o.dy + y;
        if (tx >= world->map_size || ty >= world->map_size) {
            continue;
        }
        const U number = world->tiles[tx][ty].settler;
        if (number == 0 || Callee<Fn1>(kIsSpellTarget)(world, nullptr, number) == 0) {
            continue;
        }
        Settler& s = world->settlers[number];
        const SettlerType type = TypeOf(s);
        if (type != SettlerType::Bowman1 && type != SettlerType::Bowman2 && type != SettlerType::Bowman3) {
            continue;
        }
        if (TeamOf(world, player) == TeamOf(world, s.owner)) {
            continue;
        }
        Callee<Fn1>(kReleaseSpellTarget)(world, nullptr, number);
        s.art = Value(SettlerType::Pioneer);
        Callee<Fn1>(kClearSettlerJob)(world, nullptr, number);
        ++count;
        done = true;
        SpawnEffect(world, kEffectKindChange, tx, ty, kEffectChangeGraphics, g_gameTick);
    }
    if (done) {
        Pay(world, player, kSpell);
    }
    return 0;
}

using MarkSoldiersFn = U(__fastcall*)(CGameData* world, void* edx, U player, std::int32_t x, std::int32_t y, U spell);

S3RE_HOOK(0x0054af60, CGameData_IsSpellTarget, SettlerFn, Original_CGameData_IsSpellTarget, CGameData_IsSpellTarget);
S3RE_HOOK(0x0054b000, CGameData_ReleaseSpellTarget, SettlerFn, Original_CGameData_ReleaseSpellTarget,
          CGameData_ReleaseSpellTarget);
S3RE_HOOK(0x00549f40, CGameData_SpellMarkSoldiers, MarkSoldiersFn, Original_CGameData_SpellMarkSoldiers,
          CGameData_SpellMarkSoldiers);
S3RE_HOOK(0x0054a370, CGameData_SpellConvertSettlers, Spell3Fn, Original_CGameData_SpellConvertSettlers,
          CGameData_SpellConvertSettlers);
S3RE_HOOK(0x0054a7c0, CGameData_SpellTeleportEnemies, Spell4Fn, Original_CGameData_SpellTeleportEnemies,
          CGameData_SpellTeleportEnemies);
S3RE_HOOK(0x0054a540, CGameData_SpellGatherUnits, Spell4Fn, Original_CGameData_SpellGatherUnits,
          CGameData_SpellGatherUnits);
S3RE_HOOK(0x0054a1e0, CGameData_SpellDisarmBowmen, Spell3Fn, Original_CGameData_SpellDisarmBowmen,
          CGameData_SpellDisarmBowmen);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(CGameData);
constexpr U kTestSettlers = 200;

constexpr std::uint8_t kTestTypes[] = {5, 8, 15, 30, 31, 32, 33, 34, 35, 22, 23, 36, 40, 40, 22, 0, 1, 0x2b, 0x13};
constexpr std::uint16_t kTestJobs[] = {0, 0x35, 0x37, 0x8e, 0xb0, 0xd4, 0xd5, 0x12e, 0x12f, 0x37, 0x35, 3, 0x10, 0xad};

// A map with settlers of the interesting types and jobs on its tiles,
// players of 4 teams, a ring table of 1001 small offsets and an empty
// effect table. Then copied to the other world.
void SetUpUnitWorld(test::Context& ctx, test::WorldPair& worlds) {
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
            t.settler = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 1 + rng.Below(kTestSettlers) : 0);
            t.flags = static_cast<std::uint8_t>(rng.Next32());
            t.walk_region = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 1 + rng.Below(5) : 0);
        }
    }
    for (U n = 0; n <= kTestSettlers; ++n) {
        Settler& s = w->settlers[n];
        s.owner = static_cast<std::uint8_t>(rng.Below(8));
        s.art = kTestTypes[rng.Below(sizeof(kTestTypes))];
        s.race = static_cast<std::uint8_t>(rng.Below(5));
        s.x = static_cast<std::uint16_t>(rng.Below(size));
        s.y = static_cast<std::uint16_t>(rng.Below(size));
        s.job = static_cast<SettlerJob>(kTestJobs[rng.Below(sizeof(kTestJobs) / sizeof(kTestJobs[0]))]);
        s.next = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : rng.Below(kTestSettlers + 1));
        s.prev = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : rng.Below(kTestSettlers + 1));
        s.saved_animation = rng.Chance(1, 3) ? kNoWorkTarget : rng.Next32();
        s.target_x = static_cast<std::uint16_t>(rng.Below(size));
        s.target_y = static_cast<std::uint16_t>(rng.Below(size));
    }
    w->local_player = static_cast<std::int32_t>(rng.Below(4));
    for (U p = 0; p < layout::kPlayers; ++p) {
        layout::Player& player = w->players[p];
        player.team = static_cast<std::int32_t>(rng.Below(4));
        player.race = static_cast<std::int32_t>(rng.Below(4));
        player.mana = static_cast<std::int32_t>(rng.Below(100));
        for (std::int32_t& m : player.spell_mana) {
            m = static_cast<std::int32_t>(rng.Below(60));
        }
        player.promotion_cost = static_cast<std::int32_t>(rng.Below(60));  // read for spells not in the race's list
    }
    std::memset(w->effects, 0, sizeof(w->effects));
    std::memset(&w->map_effects, 0, sizeof(w->map_effects));
    w->map_effects.blocks_x = 30;
    w->map_effects.blocks_y = 30;
    w->map_effects.slots_used = static_cast<std::int32_t>(rng.Below(40));
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kWorld);
}

// The map effects' padding comes from the make function's stack
// temporaries (stack leftovers, not reproducible): cleared before comparing.
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

U Coordinate(test::Context& ctx, U size) {
    return ctx.rng().Chance(1, 10) ? static_cast<U>(ctx.rng().Range(-20, static_cast<std::int32_t>(size) + 20))
                                   : ctx.rng().Below(size);
}

// Runs original and candidate on both worlds (cases per world set-up: 20).
// call(fn, world, player, x, y, a4, spell) runs one of them.
template <typename Call>
void CompareSpells(test::Context& ctx, int cases, Call call) {
    const std::int32_t savedMission = g_campaignMission;
    test::WorldPair worlds(ctx, kWorld);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        if (i % 20 == 0) {
            SetUpUnitWorld(ctx, worlds);
        }
        const U size = worlds.original()->map_size;
        const U player = ctx.rng().Below(8);
        const U x = Coordinate(ctx, size);
        const U y = Coordinate(ctx, size);
        const U a4 = ctx.rng().Below(kTestSettlers + 1);
        const U pick = ctx.rng().Next32();
        g_campaignMission = ctx.rng().Chance(1, 3) ? kCampaignConvertMission : 0;
        call(true, worlds.original(), player, x, y, a4, pick);
        const auto expected = logs.Take();
        call(false, worlds.ours(), player, x, y, a4, pick);
        if (!S3RE_EXPECT(expected == logs.Take()) || !SameWorlds(ctx, worlds)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u at (%d, %d), a4 %u, pick %u", i, player,
                     static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), a4, pick);
            break;
        }
    }
    g_campaignMission = savedMission;
}

constexpr U kMarkSpells[] = {10, 12, 16, 17, 24, 25};

void CompareMarkSoldiers(test::Context& ctx, MarkSoldiersFn candidate, int cases) {
    CompareSpells(ctx, cases, [&](bool original, CGameData* w, U player, U x, U y, U, U pick) {
        const U spell = kMarkSpells[pick % 6];
        (original ? Original_CGameData_SpellMarkSoldiers : candidate)(w, nullptr, player, static_cast<std::int32_t>(x),
                                                                       static_cast<std::int32_t>(y), spell);
    });
}

void CompareSpell3(test::Context& ctx, Spell3Fn originalFn, Spell3Fn candidate, int cases) {
    CompareSpells(ctx, cases, [&](bool original, CGameData* w, U player, U x, U y, U, U) {
        (original ? originalFn : candidate)(w, nullptr, player, x, y);
    });
}

void CompareSpell4(test::Context& ctx, Spell4Fn originalFn, Spell4Fn candidate, int cases) {
    CompareSpells(ctx, cases, [&](bool original, CGameData* w, U player, U x, U y, U a4, U) {
        (original ? originalFn : candidate)(w, nullptr, player, x, y, a4);
    });
}

// Wrong: spell 11 converts settlers of the own team too.
U __fastcall WrongSpellConvertSettlers(CGameData* world, void*, U player, U x, U y) {
    const std::int32_t team = world->players[player].team;
    for (U p = 0; p < layout::kPlayers; ++p) {
        if (p != player && world->players[p].team == team) {
            world->players[p].team = team + 100;
        }
    }
    CGameData_SpellConvertSettlers(world, nullptr, player, x, y);
    for (U p = 0; p < layout::kPlayers; ++p) {
        if (world->players[p].team == team + 100) {
            world->players[p].team = team;
        }
    }
    return 0;
}

// Wrong: spell 16 marks with spell 17's bit.
U __fastcall WrongSpellMarkSoldiers(CGameData* world, void*, U player, std::int32_t x, std::int32_t y, U spell) {
    return CGameData_SpellMarkSoldiers(world, nullptr, player, x, y, spell == 16 ? 17 : spell);
}

}  // namespace

S3RE_TEST(CGameData_IsSpellTarget_MatchesOriginal) {
    test::WorldPair worlds(ctx, kWorld);
    for (int i = 0; i < 4000; ++i) {
        if (i % 1000 == 0) {
            SetUpUnitWorld(ctx, worlds);
        }
        const U settler = ctx.rng().Below(kTestSettlers + 1);
        const U expected = Original_CGameData_IsSpellTarget(worlds.original(), nullptr, settler);
        const U actual = CGameData_IsSpellTarget(worlds.ours(), nullptr, settler);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler %u", i, settler);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

S3RE_TEST(CGameData_ReleaseSpellTarget_MatchesOriginal) {
    test::WorldPair worlds(ctx, kWorld);
    for (int i = 0; i < 2000; ++i) {
        if (i % 200 == 0) {
            SetUpUnitWorld(ctx, worlds);
        }
        const U settler = ctx.rng().Below(kTestSettlers + 1);
        const U expected = Original_CGameData_ReleaseSpellTarget(worlds.original(), nullptr, settler) -
                           static_cast<U>(reinterpret_cast<std::uintptr_t>(worlds.original()));
        const U actual = CGameData_ReleaseSpellTarget(worlds.ours(), nullptr, settler) -
                         static_cast<U>(reinterpret_cast<std::uintptr_t>(worlds.ours()));
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 20 == 19 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler %u", i, settler);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

S3RE_TEST(CGameData_SpellMarkSoldiers_MatchesOriginal) {
    CompareMarkSoldiers(ctx, &CGameData_SpellMarkSoldiers, 200);
}
S3RE_TEST(CGameData_SpellMarkSoldiers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMarkSoldiers(probe, &WrongSpellMarkSoldiers, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellConvertSettlers_MatchesOriginal) {
    CompareSpell3(ctx, Original_CGameData_SpellConvertSettlers, &CGameData_SpellConvertSettlers, 200);
}
S3RE_TEST(CGameData_SpellConvertSettlers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpell3(probe, Original_CGameData_SpellConvertSettlers, &WrongSpellConvertSettlers, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellTeleportEnemies_MatchesOriginal) {
    CompareSpell4(ctx, Original_CGameData_SpellTeleportEnemies, &CGameData_SpellTeleportEnemies, 200);
}
S3RE_TEST(CGameData_SpellGatherUnits_MatchesOriginal) {
    CompareSpell4(ctx, Original_CGameData_SpellGatherUnits, &CGameData_SpellGatherUnits, 200);
}
S3RE_TEST(CGameData_SpellDisarmBowmen_MatchesOriginal) {
    CompareSpell3(ctx, Original_CGameData_SpellDisarmBowmen, &CGameData_SpellDisarmBowmen, 200);
}

}  // namespace s3re::game
