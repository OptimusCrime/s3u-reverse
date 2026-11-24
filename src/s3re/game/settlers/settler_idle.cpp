// Taking a settler off its job (UpdateSettlers callees, package B).
//
// CGameData::ResetSettlerToIdle (0x0050cf50): (settler) -> EAX = the
// settler's direction dword (+0x8; callers use AL at most). thiscall
// (ECX = world), ret 4. 14 callers (walking, building removal,
// ExecuteCommands, ...). It undoes whatever the settler's job (+0xe) had
// reserved, in this order (each step is one block of the function below):
//   1. waiting units leave their player's idle list for their type;
//   2. pioneers and geologists free the tile they meant to work on;
//   3. idle civilians leave their area's list (area 0 is logged);
//   4. a carrier fetching a tool gives back the tool pile's unit;
//   5. a worker gives back its place in its building (soldier counts,
//      digger and builder slot bits, the worker field);
//   6./7. diggers and builders give back their site slot;
//   8. diggers and builders free their target tile;
//   9. jobs 307/308 (0x133/0x134) give back one or two units of the pile;
//   10. carriers on the way to a pile give back its unit;
//   11. jobs 76..79 (0x4c..0x4f) drop their good and release their pile;
//   12./13. carriers on the way to a building release the building's input
//      pile and drop what they carry;
//   14. the settler is put back on its tile (PlaceSettlerNear) if needed;
//   15. a tile without walk region is logged;
//   16. the job becomes SeekOwnArea (172, 0xac), with a valid direction.

#include "game/debug_log.h"
#include "game/enums/settler_types.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/enums/job_ids.h"
#include "game/enums/directions.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall CGameData_PileDecAmountLeaving(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_PileDecAmountComing(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_PileNeighbourBuildingByteE(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_MakePileEmpty(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_SetPileGood(CGameData* world, void* edx, std::uint32_t pile, std::uint32_t good);
std::uint32_t __fastcall CGameData_DropGoodNear(CGameData* world, void* edx, std::uint32_t good, std::uint32_t amount,
                                                std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_SubtractPlayerCounter(CGameData* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);
std::uint32_t __fastcall CGameData_PlaceSettlerNear(CGameData* world, void* edx, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y);

namespace {

using layout::Building;
using layout::Pile;
using layout::Settler;
using layout::Tile;

using Job = SettlerJob;

// Tile flag 0x02: probably "a settler is on its way to work here": set by
// UpdateSettlers, cleared here for diggers, builders, pioneers and
// geologists that give up their target.
constexpr std::uint8_t kTileClaimed = layout::kTileFlag02;

// A trade building's ring pile (PileKind::TradeRing) counts the units
// coming in the high nibble of its `leaving` byte (as UpdateEconomy reads it).
constexpr std::uint8_t kLeavingHighNibbleMinusOne = 0xf0;  // + 0xf0 = - 0x10 in a byte

// The jobs in which a settler holds a place in its building (step 5).
constexpr Job kWorkJobs[] = {Job::WalkToWork,
                             Job::ArriveAtWork,
                             Job::RecruitAtBarracks,
                             Job::CarrierWalkToTool,
                             Job::CarrierTakeTool,
                             Job::WoodcutterWalkToTree,
                             Job::StonecutterWalkToStone,
                             Job::WoodcutterAtTree,
                             Job::WoodcutterCut,
                             Job::StonecutterAtStone,
                             Job::StonecutterCut,
                             Job::WoodcutterWalkHome,
                             Job::StonecutterWalkHome,
                             Job::ForesterWalkToSpot,
                             Job::ForesterAtSpot,
                             Job::ForesterPlant,
                             Job::ForesterWalkHome,
                             Job::FarmerWalkToSow,
                             Job::FarmerSow,
                             Job::FarmerSown,
                             Job::FarmerWalkHome,
                             Job::FarmerAtField,
                             Job::FarmerHarvest,
                             Job::FarmerWalkHomeLoaded,
                             Job::FarmerWalkToHarvest,
                             Job::FisherWalkHome,
                             Job::FisherWalkToWater,
                             Job::WaterworkerWalkToWater,
                             Job::WaterworkerWalkHome,
                             Job::RiceFarmerWalkToPlant,
                             Job::RiceFarmerPlant,
                             Job::RiceFarmerPlanted,
                             Job::RiceFarmerWalkHome,
                             Job::RiceFarmerAtField,
                             Job::RiceFarmerHarvest,
                             Job::RiceFarmerWalkHomeLoaded,
                             Job::RiceFarmerWalkToHarvest,
                             Job::WinegrowerWalkToPlant,
                             Job::WinegrowerPlant,
                             Job::WinegrowerPlanted,
                             Job::WinegrowerWalkHome,
                             Job::WinegrowerAtField,
                             Job::WinegrowerHarvest,
                             Job::WinegrowerWalkHomeLoaded,
                             Job::WinegrowerWalkToHarvest,
                             Job::BeekeeperWalkToHiveSpot,
                             Job::BeekeeperWalkHome};

bool IsOneOf(Job job, std::initializer_list<Job> jobs) {
    for (const Job candidate : jobs) {
        if (candidate == job) {
            return true;
        }
    }
    return false;
}

bool IsWorkJob(Job job) {
    for (const Job candidate : kWorkJobs) {
        if (candidate == job) {
            return true;
        }
    }
    return false;
}

// The good a carrier has in its hands is kept in byte +0x15 (the `slot`
// byte of other jobs).
std::uint8_t& CarriedGood(Settler& settler) {
    return settler.slot;
}

// Clears the highest set bit of `bits` (the original's chain of tests);
// false if none was set.
bool ClearHighestBit(std::uint8_t& bits) {
    for (std::uint32_t bit = 0x80; bit != 0; bit >>= 1) {
        if ((bits & bit) != 0) {
            bits = static_cast<std::uint8_t>(bits - bit);
            return true;
        }
    }
    return false;
}

void Unclaim(Tile& tile) {
    tile.flags = static_cast<std::uint8_t>(tile.flags & ~kTileClaimed);
}

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

Tile& TargetTile(CGameData* world, const Settler& settler) {
    return TileAt(world, settler.target_x, settler.target_y);
}

// Removes the settler from a doubly linked settler list whose head is `head`.
void Unlink(CGameData* world, Settler& settler, std::uint16_t& head) {
    if (settler.prev != 0) {
        world->settlers[settler.prev].next = settler.next;
    } else {
        head = settler.next;
    }
    if (settler.next != 0) {
        world->settlers[settler.next].prev = settler.prev;
    }
}

// Step 5: the worker gives back its place in building `settler.building`.
void LeaveWorkplace(CGameData* world, Settler& settler) {
    Building& building = world->buildings[settler.building];
    const SettlerType type = TypeOf(settler);
    const Job job = settler.job;
    if (type == SettlerType::Carrier && IsOneOf(job, {Job::WalkToWork, Job::ArriveAtWork, Job::RecruitAtBarracks})) {
        // A carrier on its way to become a recruit: saved_counter is the
        // barracks' input slot + 1. Three separate tests, as in the original.
        if (settler.saved_counter == 1) {
            --building.recruiting.recruits[0];
        }
        if (settler.saved_counter == 2) {
            --building.recruiting.recruits[1];
        }
        if (settler.saved_counter == 3) {
            --building.recruiting.recruits[2];
        }
        return;
    }
    switch (ClassOf(type)) {
    case SettlerClass::Swordsman: --building.military.class1_coming; return;
    case SettlerClass::Bowman: --building.military.class2_coming; return;
    case SettlerClass::Pikeman: --building.military.class3_coming; return;
    case SettlerClass::Civilian: break;
    default: return;  // other units hold no place
    }
    // A carrier fetching a tool already counts as the worker it becomes.
    const bool fetchingTool = job == Job::CarrierWalkToTool || job == Job::CarrierTakeTool;
    if (type == SettlerType::Digger || (fetchingTool && settler.tool_type == Value(SettlerType::Digger))) {
        ClearHighestBit(building.digger_slots);
    } else if (type == SettlerType::Builder || (fetchingTool && settler.tool_type == Value(SettlerType::Builder))) {
        ClearHighestBit(building.builder_slots);
    } else {
        building.worker = 0;
        building.production.worker_inside = 0;
    }
}

// Step 11: jobs 76..79 drop their good and release the pile in word +0x22.
// `savedGoods` is the original's frame dword for the pile's goods byte.
void ReleasePileOfJobs76To79(CGameData* world, Settler& settler, std::uint32_t& savedGoods) {
    if (CarriedGood(settler) != Value(Good::Nothing)) {
        CGameData_DropGoodNear(world, nullptr, CarriedGood(settler), 1, settler.x, settler.y);
        CGameData_SubtractPlayerCounter(world, nullptr, settler.owner, CarriedGood(settler), 1);
    }
    const std::uint32_t pileNumber = settler.building;  // word +0x22 holds a pile in these jobs
    Pile& pile = world->piles[pileNumber];
    if (KindOf(pile) == PileKind::TradeRing) {
        pile.leaving = static_cast<std::uint8_t>(pile.leaving + kLeavingHighNibbleMinusOne);
        return;
    }
    CGameData_PileDecAmountComing(world, nullptr, pileNumber);
    if (pile.amount != 0 || pile.coming != 0) {
        return;
    }
    // The pile is empty and nothing comes: it is freed, then gets the goods
    // type of its building's pile again (unless that has none).
    const std::uint32_t buildingGood = CGameData_PileNeighbourBuildingByteE(world, nullptr, pileNumber);
    savedGoods = (savedGoods & 0xffffff00u) | (buildingGood & 0xff);
    CGameData_MakePileEmpty(world, nullptr, pileNumber);
    if ((buildingGood & 0xff) != Value(Good::Empty)) {
        CGameData_SetPileGood(world, nullptr, pileNumber, savedGoods);
    }
}

// Step 12: the building's input pile for `good` expects one unit less.
void CancelDelivery(CGameData* world, const Building& building, std::uint32_t good) {
    if (building.buildup != 0) {
        // A construction site: boards go to pile 0, stones to pile 1.
        if (good == Value(Good::Board)) {
            CGameData_PileDecAmountComing(world, nullptr, building.piles[0]);
        } else if (good == Value(Good::Stone)) {
            CGameData_PileDecAmountComing(world, nullptr, building.piles[1]);
        }
        return;
    }
    for (const std::uint16_t pile : building.piles) {
        if (world->piles[pile].good == good) {
            CGameData_PileDecAmountComing(world, nullptr, pile);
            return;
        }
    }
}

void LogTile(const Tile& tile, std::uint32_t owner) {
    GameDebugLog(texts::kTileOwnerDetails, owner, static_cast<std::uint32_t>(tile.owner),
                 static_cast<std::uint32_t>(tile.object), static_cast<std::uint32_t>(tile.area),
                 static_cast<std::uint32_t>(tile.terrain));
}

}  // namespace

using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);

// Purpose: Takes a settler off its current job (e.g. when its building is destroyed or the player
// changes something): reservations of piles and building slots are given back, carried goods are
// dropped, and the settler becomes idle.
std::uint32_t __fastcall CGameData_ResetSettlerToIdle(CGameData* world, void*, std::uint32_t settlerNumber) {
    Settler& settler = world->settlers[settlerNumber];
    // The original keeps one dword of its frame for the goods byte of
    // step 11; its upper bytes are ECX's (the world pointer) at entry.
    std::uint32_t savedGoods = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));

    // 1. Units waiting in their player's idle list (head per settler type;
    // for owner 0xff past the players, as the original's arithmetic does).
    if (IsOneOf(settler.job, {Job::UnitIdle, Job::UnitWalk, Job::UnitWalkCountdown, Job::UnitFighting})) {
        Unlink(world, settler, world->players[settler.owner].settler_lists[settler.art]);
    }
    // 2. Pioneers and geologists.
    if (TypeOf(settler) == SettlerType::Pioneer || TypeOf(settler) == SettlerType::Geologist) {
        if (settler.job == Job::UnitWalk || settler.job == Job::UnitWalkCountdown) {
            Unclaim(TargetTile(world, settler));
        }
        if (IsOneOf(settler.job,
                    {Job::GeologistWork, Job::GeologistDone, Job::PioneerWork, Job::PioneerDone})) {
            Unclaim(TileAt(world, settler.x, settler.y));
        }
    }
    // 3. Idle civilians are listed in their area (by settler type).
    if (settler.job == Job::Nothing || settler.job == Job::StepAside) {
        const std::uint32_t area = TileAt(world, settler.x, settler.y).area;
        if (area == 0) {
            GameDebugLog(texts::kStepAsideOnSectorZero, static_cast<std::uint32_t>(settler.art),
                         static_cast<std::uint32_t>(settler.x), static_cast<std::uint32_t>(settler.y));
            const Tile& tile = TileAt(world, settler.x, settler.y);
            const std::uint32_t owner = settler.owner;
            // The original pushes the owner between all arguments here.
            GameDebugLog(texts::kTileOwnerDetails, owner, static_cast<std::uint32_t>(tile.owner), owner,
                         static_cast<std::uint32_t>(tile.object), owner, static_cast<std::uint32_t>(tile.area), owner,
                         static_cast<std::uint32_t>(tile.terrain));
        }
        Unlink(world, settler, world->areas[area].settlers_by_type[settler.art]);
    }
    // 4.
    if (settler.job == Job::CarrierWalkToTool || settler.job == Job::CarrierTakeTool) {
        CGameData_PileDecAmountLeaving(world, nullptr, TargetTile(world, settler).object_number);
    }
    // 5.
    if (IsWorkJob(settler.job)) {
        LeaveWorkplace(world, settler);
    }
    // 6. Diggers (jobs 5..9).
    if (IsOneOf(settler.job, {Job::DiggerDig, Job::DiggerWalkToSpot, Job::DiggerLevel, Job::DiggerLower,
                              Job::DiggerRaise})) {
        Building& site = world->buildings[settler.building];
        if (ClearHighestBit(site.digger_slots)) {
            --site.site.workers_at_work;
        }
    }
    // 7. Builders (jobs 14, 15, 17, 18).
    if (IsOneOf(settler.job,
                {Job::BuilderStart, Job::BuilderWalkToWait, Job::BuilderWalkToSpot, Job::BuilderBuild})) {
        Building& site = world->buildings[settler.building];
        if (ClearHighestBit(site.builder_slots)) {
            --site.site.workers_at_work;
        }
    }
    // 8.
    if (IsOneOf(settler.job, {Job::DiggerWalkToSpot, Job::DiggerLevel, Job::DiggerRaise, Job::DiggerLower,
                              Job::BuilderWalkToSpot, Job::BuilderBuild})) {
        Tile& target = TargetTile(world, settler);
        if ((target.flags & kTileClaimed) != 0) {
            Unclaim(target);
        }
    }
    // 9. One unit, or two with second_unit, of the target pile were taken.
    if (settler.job == Job::DonkeyWalkToPile || settler.job == Job::DonkeyLoad) {
        --world->piles[TargetTile(world, settler).object_number].leaving;
        if (settler.second_unit != 0) {
            --world->piles[TargetTile(world, settler).object_number].leaving;
        }
    }
    // 10.
    if (IsOneOf(settler.job, {Job::CarrierWalkToPile, Job::CarrierAtPile, Job::CarrierPickUp,
                              Job::CarrierWalkToSourcePile, Job::CarrierAtSourcePile})) {
        CGameData_PileDecAmountLeaving(world, nullptr, TargetTile(world, settler).object_number);
    }
    // 11.
    if (IsOneOf(settler.job, {Job::CarrierWalkToSourcePile, Job::CarrierAtSourcePile,
                              Job::CarrierWalkToTargetPile, Job::CarrierAtTargetPile})) {
        ReleasePileOfJobs76To79(world, settler, savedGoods);
    }
    // 12. Before the pick-up the good is the ordered one (+0x1a), after it
    // the carried one (+0x15).
    if (IsOneOf(settler.job,
                {Job::CarrierWalkToPile, Job::CarrierAtPile, Job::CarrierPickUp, Job::CarrierLeavePile,
                 Job::CarrierWalkToBuilding, Job::CarrierAtBuilding, Job::CarrierDeliver})) {
        const bool pickedUp =
            !IsOneOf(settler.job, {Job::CarrierWalkToPile, Job::CarrierAtPile, Job::CarrierPickUp});
        const std::uint32_t good = pickedUp ? CarriedGood(settler) : settler.good;
        CancelDelivery(world, world->buildings[settler.building], good);
    }
    // 13.
    if (IsOneOf(settler.job, {Job::CarrierLeavePile, Job::CarrierWalkToBuilding, Job::CarrierAtBuilding,
                              Job::CarrierDeliver})) {
        CGameData_DropGoodNear(world, nullptr, CarriedGood(settler), 1, settler.x, settler.y);
        CGameData_SubtractPlayerCounter(world, nullptr, settler.owner, CarriedGood(settler), 1);
        CarriedGood(settler) = Value(Good::Nothing);
    }
    // 14.
    if (TileAt(world, settler.x, settler.y).settler != settlerNumber) {
        CGameData_PlaceSettlerNear(world, nullptr, settlerNumber, settler.x, settler.y);
    }
    // 15.
    if (TileAt(world, settler.x, settler.y).walk_region == 0) {
        GameDebugLog(texts::kSettlerOnSectorZero, static_cast<std::uint32_t>(settler.art),
                     static_cast<std::uint32_t>(settler.x), static_cast<std::uint32_t>(settler.y),
                     static_cast<std::uint32_t>(settler.job));
        LogTile(TileAt(world, settler.x, settler.y), settler.owner);
    }
    // 16.
    settler.job = Job::SeekOwnArea;
    settler.slot = 0;
    settler.building = 0;
    settler.counter = 0;
    if (settler.direction >= kDirectionCount) {
        settler.direction = CGameData_RandomBelow6(world, nullptr);
    }
    return settler.direction;
}

S3RE_HOOK(0x0050cf50, CGameData_ResetSettlerToIdle, SettlerFn, Original_CGameData_ResetSettlerToIdle,
          CGameData_ResetSettlerToIdle);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 1024;
constexpr std::uint32_t kTestBuildings = 256;
constexpr std::uint32_t kTestPiles = 1024;
constexpr std::uint32_t kTestMapSize = 384;
constexpr std::uint32_t kOffsetEntries = 10000;

// The game's offset table (DropGoodNear, PlaceSettlerNear walk it).
void FillOffsetSpiral(test::WorldPair& worlds) {
    std::uint32_t k = 0;
    for (std::int32_t ring = 0; k < kOffsetEntries; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring && k < kOffsetEntries; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring && k < kOffsetEntries; ++dy) {
                if (TileDistance(0, 0, static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy)) ==
                    static_cast<std::uint32_t>(ring)) {
                    worlds.Both([&](CGameData* w) {
                        w->offsets[k].dx = dx;
                        w->offsets[k].dy = dy;
                    });
                    ++k;
                }
            }
        }
    }
}

// Jobs that reach every step (and some others).
constexpr Job kTestJobs[] = {Job::UnitIdle,
                             Job::UnitWalk,
                             Job::UnitWalkCountdown,
                             Job::UnitFighting,
                             Job::GeologistWork,
                             Job::GeologistDone,
                             Job::PioneerWork,
                             Job::PioneerDone,
                             Job::Nothing,
                             Job::StepAside,
                             Job::CarrierWalkToTool,
                             Job::CarrierTakeTool,
                             Job::WalkToWork,
                             Job::ArriveAtWork,
                             Job::RecruitAtBarracks,
                             Job::WoodcutterWalkToTree,
                             Job::WoodcutterWalkHome,
                             Job::RiceFarmerWalkToPlant,
                             Job::BeekeeperWalkHome,
                             Job::DiggerDig,
                             Job::DiggerWalkToSpot,
                             Job::DiggerLevel,
                             Job::DiggerLower,
                             Job::DiggerRaise,
                             Job::BuilderStart,
                             Job::BuilderWalkToWait,
                             Job::BuilderWalkToSpot,
                             Job::BuilderBuild,
                             Job::DonkeyWalkToPile,
                             Job::DonkeyLoad,
                             Job::CarrierWalkToPile,
                             Job::CarrierAtPile,
                             Job::CarrierPickUp,
                             Job::CarrierWalkToSourcePile,
                             Job::CarrierAtSourcePile,
                             Job::CarrierWalkToTargetPile,
                             Job::CarrierAtTargetPile,
                             Job::CarrierLeavePile,
                             Job::CarrierWalkToBuilding,
                             Job::CarrierAtBuilding,
                             Job::CarrierDeliver,
                             Job::SeekOwnArea,
                             Job::WinegrowerEnterLoaded,
                             Job::MinerEatCoalMine};
constexpr SettlerType kTestTypes[] = {
    SettlerType::Carrier,   SettlerType::Digger,   SettlerType::Builder,     SettlerType::Geologist,
    SettlerType::Pioneer,   SettlerType::Pikeman1, SettlerType::Swordsman2,  SettlerType::Bowman1,
    SettlerType::Bowman2,   SettlerType::Swordsman1, SettlerType::Miner,
    static_cast<SettlerType>(kSettlerTypeCount)};  // one past the last type: the tables are read past their end

bool CarriesPileOfJobs76To79(Job job) {
    return IsOneOf(job, {Job::CarrierWalkToSourcePile, Job::CarrierAtSourcePile, Job::CarrierWalkToTargetPile,
                         Job::CarrierAtTargetPile});
}

void SetUpCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settler) {
    auto& rng = ctx.rng();
    const Job job = kTestJobs[rng.Below(sizeof(kTestJobs) / sizeof(kTestJobs[0]))];
    const SettlerType type = kTestTypes[rng.Below(sizeof(kTestTypes))];
    const auto owner = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint32_t x = 10 + rng.Below(360), y = 10 + rng.Below(360);
    const std::uint32_t tx = x + rng.Below(5) - 2, ty = y + rng.Below(5) - 2;
    const std::uint32_t building = 1 + rng.Below(kTestBuildings);
    const std::uint32_t area = rng.Chance(1, 8) ? 0 : 1 + rng.Below(256);
    auto link = [&]() { return static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(kTestSettlers)); };
    const std::uint16_t next = link(), prev = link();
    const auto direction = rng.Chance(1, 4) ? rng.Next32() : rng.Below(kDirectionCount);
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(35));
    const auto good = static_cast<std::uint8_t>(rng.Below(kGoodCount));
    const auto saved = static_cast<std::uint16_t>(rng.Below(5));
    const auto secondUnit = static_cast<std::uint8_t>(rng.Below(2));
    const auto toolType = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint32_t pileWord = CarriesPileOfJobs76To79(job) ? 1 + rng.Below(kTestPiles) : building;
    const bool onTile = rng.Chance(3, 4);
    const auto region = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0 : 1 + rng.Below(5));
    const auto targetPile = static_cast<std::uint16_t>(1 + rng.Below(kTestPiles));
    worlds.Both([&](CGameData* w) {
        Settler& s = w->settlers[settler];
        s.job = job;
        s.art = Value(type);
        s.owner = owner;
        s.x = static_cast<std::uint16_t>(x);
        s.y = static_cast<std::uint16_t>(y);
        s.target_x = static_cast<std::uint16_t>(tx);
        s.target_y = static_cast<std::uint16_t>(ty);
        s.building = static_cast<std::uint16_t>(pileWord);
        s.next = next;
        s.prev = prev;
        s.direction = direction;
        s.slot = slot;
        s.good = good;
        s.saved_counter = saved;
        s.second_unit = secondUnit;
        s.tool_type = toolType;
        Tile& tile = w->tiles[x][y];
        tile.area = static_cast<std::uint16_t>(area);
        tile.settler = static_cast<std::uint16_t>(onTile ? settler : 1 + (settler + 7) % kTestSettlers);
        tile.walk_region = region;
        w->tiles[tx][ty].object_number = targetPile;
    });
    // The building's piles: goods types around the settler's.
    for (std::uint32_t k = 0; k < 3; ++k) {
        const auto pile = static_cast<std::uint16_t>(1 + rng.Below(kTestPiles));
        const auto pileGood =
            static_cast<std::uint8_t>(rng.Chance(1, 3) ? (rng.Chance(1, 2) ? slot : good) : rng.Below(kGoodCount));
        const auto buildup = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(3));
        worlds.Both([&](CGameData* w) {
            w->buildings[building].piles[k] = pile;
            w->piles[pile].good = pileGood;
            w->buildings[building].buildup = buildup;
        });
    }
    if (CarriesPileOfJobs76To79(job)) {
        const auto kind = static_cast<std::uint8_t>(rng.Chance(1, 4) ? Value(PileKind::TradeRing) : rng.Next32());
        const auto amount = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(5));
        const auto coming = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 1 : rng.Below(5));
        const auto px = static_cast<std::uint16_t>(10 + rng.Below(360));
        const auto py = static_cast<std::uint16_t>(10 + rng.Below(360));
        worlds.Both([&](CGameData* w) {
            Pile& p = w->piles[pileWord];
            p.art = kind;
            p.amount = amount;
            p.coming = coming;
            p.x = px;
            p.y = py;
            w->tiles[px][py].area = static_cast<std::uint16_t>(area);
        });
    }
}

void Compare(test::Context& ctx, SettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    FillOffsetSpiral(worlds);
    worlds.Both([&](CGameData* w) {
        w->map_size = kTestMapSize;
        // Pile records with coordinates on the map, linked within the test
        // range; buildings piles on the map too.
        for (std::uint32_t p = 0; p < 32000; ++p) {
            Pile& pile = w->piles[p];
            pile.x = static_cast<std::uint16_t>(pile.x % kTestMapSize);
            pile.y = static_cast<std::uint16_t>(pile.y % kTestMapSize);
            pile.prev = static_cast<std::uint16_t>(pile.prev % kTestPiles);
            pile.next = static_cast<std::uint16_t>(pile.next % kTestPiles);
            if (p > kTestPiles) {
                pile.good = Value(Good::Board);  // CreatePile finds free records below
            }
        }
        for (std::uint32_t s = 0; s < 32000; ++s) {
            Settler& r = w->settlers[s];
            r.x = static_cast<std::uint16_t>(r.x % kTestMapSize);
            r.y = static_cast<std::uint16_t>(r.y % kTestMapSize);
            r.target_x = static_cast<std::uint16_t>(r.target_x % kTestMapSize);
            r.target_y = static_cast<std::uint16_t>(r.target_y % kTestMapSize);
            r.building = static_cast<std::uint16_t>(r.building % kTestBuildings);
        }
        for (auto& row : w->tiles) {
            for (Tile& tile : row) {
                tile.object_number = static_cast<std::uint16_t>(tile.object_number % kTestPiles);
                tile.area = static_cast<std::uint16_t>(tile.area % 512);
            }
        }
    });
    DebugLogRecorder logs;
    std::size_t logged = 0;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers);
        SetUpCase(ctx, worlds, settler);
        const std::uint32_t expected = Original_CGameData_ResetSettlerToIdle(worlds.original(), nullptr, settler);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        logged += expectedLogs.size();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): settler %u", i, settler);
            return;
        }
    }
    S3RE_EXPECT(logged > 0);
}

// Wrong: the pikemen (class 3) give back the bowmen's count instead of their own.
std::uint32_t __fastcall WrongResetSettlerToIdle(CGameData* world, void* edx, std::uint32_t settlerNumber) {
    const Settler& settler = world->settlers[settlerNumber];
    if (IsWorkJob(settler.job) && TypeOf(settler) != SettlerType::Carrier &&
        ClassOf(settler) == SettlerClass::Pikeman) {
        Building& building = world->buildings[settler.building];
        ++building.military.class3_coming;
        --building.military.class2_coming;
    }
    return CGameData_ResetSettlerToIdle(world, edx, settlerNumber);
}

}  // namespace

S3RE_TEST(CGameData_ResetSettlerToIdle_MatchesOriginal) {
    Compare(ctx, &CGameData_ResetSettlerToIdle, 6000);
}
S3RE_TEST(CGameData_ResetSettlerToIdle_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongResetSettlerToIdle, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
