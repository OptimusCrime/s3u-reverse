#pragma once

// Internal header of CGameData::UpdateSettlers (0x004e9670,
// update_settlers.cpp): the per-settler job handlers live in the
// settler_jobs_*.cpp files, one file per range of the original's code (the
// compiler laid the switch cases out in source order, which mostly groups
// them by profession):
//
//   settler_jobs_units.cpp     0x4e971c..0x4eb43b  new, idle, soldiers, walking, job 4 (work start)
//   settler_jobs_building.cpp  0x4eb43b..0x4ee0c1  diggers, builders, carriers, wood, stone, forester
//   settler_jobs_metal.cpp     0x4ee0c1..0x4f0816  sawmill tail, smelters, miners, smiths, war machines
//   settler_jobs_food.cpp      0x4f0816..0x4f335a  weapon smith, pig farm, butcher, farmer, miller, baker,
//                                                  fisher, waterworker
//   settler_jobs_drinks.cpp    0x4f335a..0x4f5f6b  pioneer land, rice farmer, winegrower, distillers,
//                                                  brewer, powder maker, charcoal burner
//   settler_jobs_special.cpp   0x4f5f6b..0x4f91fb  shipyard, ships, healer, thief, geologist, beekeeper,
//                                                  mead maker, alchemist, gongs
//
// The loop (update_settlers.cpp) dispatches each settler to the file that
// holds its job's handler (kJobFamily); each file switches on the job.
//
// Original register / stack use, for reading the disassembly: ESI = world,
// EDI = settler number << 6 (the record is [edi + 0x12a5e60]), EBP = the
// settler number (also [esp+2Ch]), [esp+48h] = its type, [esp+4Ch] = its
// class, [esp+54h] = the type byte, [esp+50h] = the "run again" flag.
// At a handler's entry EDX = 0, ECX = the job, EAX = (number << 6) with the
// owner in AL, EBX = the frame slot [esp+20h] (see FrameSlots).

#include "game/debug_log.h"
#include "game/enums/job_ids.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"

#include <cstdint>

namespace s3re::game::settler_jobs {

using layout::Area;
using layout::Building;
using layout::Pile;
using layout::Player;
using layout::Settler;
using layout::Tile;
using Job = SettlerJob;

// What the loop does after a handler.
enum class JobResult {
    Next,   // on to the next settler (0x4f9202)
    Again,  // run the same settler again this step (0x4f9214, or the again flag [esp+50h] set)
};

// Stack slots of the original's frame that keep their value from one
// settler's handler to the next one's (the frame is shared by the whole
// loop). Only the slots that some handler reads before writing it are kept
// here (scratchpad analysis of every read not preceded by a write since the
// loop head); a handler that writes one of them updates it here. EBX is
// reloaded from [esp+20h] at the end of every iteration, so the EBX a
// handler starts with is slot20. On the first settler of a call the
// original's slots hold whatever the caller's stack held (not
// reproducible); ours start at 0.
struct FrameSlots {
    std::uint32_t slot14 = 0;
    std::uint32_t slot20 = 0;  // = EBX at every handler's entry
    std::uint32_t slot30 = 0;
    std::uint32_t slot44 = 0;
    std::uint32_t slot58 = 0;
    std::uint32_t slot5c = 0;
};

// One settler's turn.
struct SettlerContext {
    CGameData* world;
    std::uint32_t number;    // EBP / [esp+2Ch]
    Settler& settler;        // [edi + 0x12a5e60]
    std::uint32_t job;       // the job the dispatch used (ECX at entry)
    std::uint32_t type;      // [esp+48h]: the settler type read at the loop head
    std::int32_t typeClass;  // [esp+4Ch]: g_settlerTypeClass[type]
    FrameSlots& frame;
};

// The job handler families (update_settlers.cpp dispatches by kJobFamily).
JobResult RunUnitJobs(SettlerContext& ctx);      // settler_jobs_units.cpp
JobResult RunBuildingJobs(SettlerContext& ctx);  // settler_jobs_building.cpp
JobResult RunMetalJobs(SettlerContext& ctx);     // settler_jobs_metal.cpp
JobResult RunFoodJobs(SettlerContext& ctx);      // settler_jobs_food.cpp
JobResult RunDrinkJobs(SettlerContext& ctx);     // settler_jobs_drinks.cpp
JobResult RunSpecialJobs(SettlerContext& ctx);   // settler_jobs_special.cpp

// ---------------------------------------------------------------------------
// Callees, called through their S3.EXE addresses (in the game Detours sends
// the call to our replacement, which is counted; tests stub them). All are
// thiscall on the world unless noted.
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kDestroyBuildingAt = 0x004d97b0;            // (x, y)
constexpr std::uintptr_t kBuildingDone = 0x004dbc50;                 // (building)
constexpr std::uintptr_t kReleaseBuildingExtraTiles = 0x004dda10;    // (building)
constexpr std::uintptr_t kClearBuildingFlag02 = 0x004df490;          // (building)
constexpr std::uintptr_t kClearBuildingFlag04 = 0x004df510;          // (building)
constexpr std::uintptr_t kClearBuildingFlag08 = 0x004df590;          // (building)
constexpr std::uintptr_t kClearBuildingFlag10 = 0x004df610;          // (building)
constexpr std::uintptr_t kBuildingPileHasGoods = 0x004df9b0;         // (building, slot)
constexpr std::uintptr_t kQueuePendingEvent = 0x004e8ba0;            // 3 args
constexpr std::uintptr_t kMinerJobEnter = 0x004f9a00;                // (settler)
constexpr std::uintptr_t kMinerJobFood = 0x004f9b80;                 // (settler)
constexpr std::uintptr_t kMinerJobDig = 0x004fa220;                  // (settler)
constexpr std::uintptr_t kTileSlope = 0x004faa70;                    // (x, y)
constexpr std::uintptr_t kRandom = 0x004feb70;                       // () -> 0..0x7fff
constexpr std::uintptr_t kNotifyLocalPlayer = 0x004fec10;            // 4 args
constexpr std::uintptr_t kAllocTable36700Record = 0x004fefd0;        // 10 args
constexpr std::uintptr_t kTileDistance = 0x004ff320;                 // stdcall (x1, y1, x2, y2), ECX = world
constexpr std::uintptr_t kClearAreaGoodsSettings1D2 = 0x004ff3c0;    // 1 arg
constexpr std::uintptr_t kAddStatistic = 0x004ff510;                 // 3 args
constexpr std::uintptr_t kAddPlayerCounter = 0x004ffb00;             // 3 args
constexpr std::uintptr_t kSubtractPlayerCounter = 0x004ffb40;        // 3 args
constexpr std::uintptr_t kWalkSettler = 0x00500860;                  // (settler) -> run again
constexpr std::uintptr_t kSetSettlerTarget = 0x00503610;             // (settler, x, y)
constexpr std::uintptr_t kNeighbourX = 0x00503700;                   // stdcall (x, direction)
constexpr std::uintptr_t kNeighbourY = 0x00503730;                   // stdcall (y, direction)
constexpr std::uintptr_t kCanSettlerEnterTile = 0x00503760;          // (x, y, owner, type)
constexpr std::uintptr_t kRandomBelow6 = 0x00503880;                 // ()
constexpr std::uintptr_t kPreviousDirection = 0x005038a0;            // stdcall (direction)
constexpr std::uintptr_t kNextDirection = 0x005038c0;                // stdcall (direction)
constexpr std::uintptr_t kDropGoodNear = 0x005046b0;                 // 4 args
constexpr std::uintptr_t kAddGoodsToPile = 0x00505650;               // (pile)
constexpr std::uintptr_t kTakeGoodFromPile = 0x00505820;             // (pile)
constexpr std::uintptr_t kPileIncAmountLeaving = 0x005058c0;         // (pile)
constexpr std::uintptr_t kPileIncAmountComing = 0x00505bc0;          // (pile)
constexpr std::uintptr_t kStealGoodFromPile = 0x00505c20;            // (settler)
constexpr std::uintptr_t kCreateSettler = 0x0050c9e0;                // 4 args -> settler
constexpr std::uintptr_t kSpawnSettlersAround = 0x0050cb20;          // 5 args
constexpr std::uintptr_t kLeaveBuildingSlot = 0x0050cc10;            // (settler)
constexpr std::uintptr_t kEnterBuildingSlot = 0x0050ccb0;            // (settler)
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;           // (settler)
constexpr std::uintptr_t kCreateSevenTileUnit = 0x0050dd20;          // 4 args
constexpr std::uintptr_t kMoveSettlerToBuildingTileA = 0x0050de90;   // (settler)
constexpr std::uintptr_t kMoveSettlerToBuildingTileB = 0x0050df40;   // (settler)
constexpr std::uintptr_t kSetSoldierAnimationInBuilding = 0x0050e140;  // (settler)
constexpr std::uintptr_t kSetSettlerAnimation = 0x0050e2c0;          // (settler, animation)
constexpr std::uintptr_t kSetSettlerSavedAnimation = 0x0050e320;     // 2 args
constexpr std::uintptr_t kRestoreSettlerAnimation = 0x0050e380;      // (settler)
constexpr std::uintptr_t kCountAreaSettlersOfClass0 = 0x0050e750;    // 1 arg
constexpr std::uintptr_t kCountAreaSettlersOfType = 0x0050e820;      // 2 args
constexpr std::uintptr_t kUpdateSoldier = 0x0050e9f0;                // (settler)
constexpr std::uintptr_t kClearSettlerJob = 0x00510440;              // (settler)
constexpr std::uintptr_t kSettlerStartHealth = 0x00510470;           // (type)
constexpr std::uintptr_t kAddPileClaim = 0x00510770;                 // 2 args
constexpr std::uintptr_t kCountOtherPileClaims = 0x00510800;         // 2 args
constexpr std::uintptr_t kIsOnMap = 0x005194e0;                      // (x, y)
constexpr std::uintptr_t kIsFreeOfCatapultsNearby = 0x005199e0;  // (x, y)
constexpr std::uintptr_t kTileScreenX = 0x00519610;                  // (x, y)
constexpr std::uintptr_t kTileScreenY = 0x00519640;                  // (x, y)
constexpr std::uintptr_t kUpdateShadeAround = 0x00519670;            // (x, y)
constexpr std::uintptr_t kRecalculateTerritory = 0x00519a90;         // 2 args
constexpr std::uintptr_t kUpdateBorderFlagsAround = 0x0051a460;      // (x, y)
constexpr std::uintptr_t kSetOwnerOfFreeTile = 0x0051b5d0;           // (x, y, owner)
constexpr std::uintptr_t kClearTileOwner = 0x0051b640;               // (x, y)
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;                    // (x, y)
constexpr std::uintptr_t kUnblockTile = 0x0051b770;                  // (x, y)
constexpr std::uintptr_t kWearPath = 0x0051b7f0;                     // (x, y)
constexpr std::uintptr_t kMakeFieldTile = 0x0051ba00;            // (x, y)
// MapEffects members, thiscall on world->map_effects.
constexpr std::uintptr_t kSpawn544630 = 0x00544630;                  // 8 args
constexpr std::uintptr_t kSpawn5449f0 = 0x005449f0;                  // 8 args
constexpr std::uintptr_t kSpawn544dc0 = 0x00544dc0;                  // 7 args
constexpr std::uintptr_t kSpawn545120 = 0x00545120;                  // 5 args
constexpr std::uintptr_t kNoOpRet4 = 0x00582530;                     // NoOp_ret4, thiscall (1 arg)

// thiscall on the world (or another object) with dword arguments; every
// argument is converted to a dword first (no stale upper bytes).
template <std::uintptr_t Address, typename Self, typename... Args>
std::uint32_t Call(Self* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(Self*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// stdcall helpers (NeighbourX/Y, Next/PreviousDirection, TileDistance).
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallStd(Args... args) {
    using Fn = std::uint32_t(__stdcall*)(decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(static_cast<std::uint32_t>(args)...);
}

// Record access by number (not range-checked, as in the original).
inline Settler& SettlerAt(CGameData* world, std::uint32_t n) { return *(world->settlers + n); }
inline Building& BuildingAt(CGameData* world, std::uint32_t n) { return *(world->buildings + n); }
inline Pile& PileAt(CGameData* world, std::uint32_t n) { return *(world->piles + n); }
inline Player& PlayerAt(CGameData* world, std::uint32_t n) { return *(world->players + n); }
inline Area& AreaAt(CGameData* world, std::uint32_t n) { return *(world->areas + n); }
inline Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

}  // namespace s3re::game::settler_jobs
