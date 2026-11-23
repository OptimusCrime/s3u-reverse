// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f0816..0x4f335a of
// the original (settler_jobs.h): the weapon smith (jobs 0x6c, 0x75,
// 0x83..0x88), the pig farm and the donkey farm (0x6e, 0x77, 0xc8..0xcd;
// 0x131, 0x132, 0xce..0xd3), the butcher (0x6f, 0x78, 0xc3..0xc7), the
// farmer (0x70, 0x79, 0x8f..0x9d), the miller (0x71, 0x7a, 0x9e..0xa2), the
// baker (0x72, 0x7b, 0xa3..0xa8), the fisher (0x6d, 0x76, 0xb1..0xb9) and
// the waterworker (0x73, 0x7c, 0xba..0xc2).
//
// Most workshops follow one cycle: a start job (0x6c..0x73, 0x131) counts
// the worker in at its building (kind byte +0x19) and goes to the "take
// inputs" job; that one waits (counter 10) unless the building wants to
// work (request flag bit 0), its input piles hold goods and its output pile
// has room, reserves the inputs (PileIncAmountLeaving), puts the worker on
// its tile and starts the first animation; then one job per animation:
// consume each input (TakeGoodFromPile, SubtractPlayerCounter), produce
// (PileIncAmountComing, AddGoodsToPile, AddStatistic, AddPlayerCounter), and
// either reserve the next inputs at once or leave the tile and start over.
// Farmer, fisher and waterworker instead search the offset table around
// their work spot (Building::work_x/y) for a field, a fishing spot or a
// well, walk there, work, and walk back.
//
// The animation ids passed to SetSettlerAnimation are given in decimal.
//
// Frame slots (FrameSlots): the searches of the farmer (0x79), fisher (0x76,
// 0xb3) and waterworker (0x7c) leave their last candidate (x, y) in
// [esp+20h] / [esp+14h]; ours mirror them into ctx.frame. No handler here
// reads a slot left by another settler (the leaks2 entries of this range
// are loop-carried values written in the search's first iteration).

#include "game/settlers/jobs/settler_jobs.h"
#include "game/settlers/jobs/settler_jobs_test.h"
#include "game/enums/settler_types.h"
#include "game/enums/goods.h"
#include "game/enums/job_ids.h"
#include "game/enums/terrain.h"

#include <vector>

namespace s3re::game::settler_jobs {

namespace {

constexpr std::uint16_t kWaitForInputs = 10;   // counter while the building can't work
constexpr std::uint8_t kPileFull = 7;          // a pile holds at most 8 units; more than 7 = full
constexpr std::uint8_t kRequestWork = 0x01;    // Building::request_flags bit 0: the building may work
constexpr std::uint8_t kTileNotFree = layout::kTileBlocked | layout::kTileBuildingFootprint;
constexpr std::uint32_t kSearchSteps = 50;     // offset table entries tried per search
constexpr std::uint32_t kMaxSlope = 7;         // TileSlope below this: flat enough for a field

// Terrain classes (Tile::terrain & 0xf0) the searches look for.
constexpr std::uint8_t kTerrainClassGrass = 0x10;   // fields are sown here
constexpr std::uint8_t kTerrainClassShore = 0x30;   // the fisher stands here (kTerrainShore)
constexpr std::uint8_t kTerrainClass60 = 0x60;      // the waterworker draws water here (meaning unknown)

// Tile::object values of a field.
constexpr std::uint8_t kFieldSown = 0x55;
constexpr std::uint8_t kFieldRipeFirst = 0x5a;
constexpr std::uint8_t kFieldRipeLast = 0x5c;
constexpr std::uint8_t kFieldHarvested = 0x5d;

// The weapon smith's output: per weapon an area setting triple (settings1:
// ordered units, settings2: share, settings3: made so far), indexed by the
// goods number.
constexpr std::uint32_t kWeapons[3] = {Value(Good::Sword), Value(Good::Bow), Value(Good::Spear)};

// ------------------------------------------------------------------ helpers

Building& BuildingOf(const SettlerContext& ctx) {
    return BuildingAt(ctx.world, ctx.settler.building);
}

std::uint32_t OwnerOf(const SettlerContext& ctx) {
    return ctx.settler.owner;
}

// The tile the settler stands on.
Tile& SettlerTile(const SettlerContext& ctx) {
    return TileAt(ctx.world, ctx.settler.x, ctx.settler.y);
}

void Animate(const SettlerContext& ctx, std::uint32_t animation) {
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
}

// The job first, then the animation (the original's order).
JobResult Switch(SettlerContext& ctx, Job job, std::uint32_t animation) {
    SetJob(ctx.settler, job);
    Animate(ctx, animation);
    return JobResult::Next;
}

// 0x4f8b7c (shared with other ranges): wait before trying again.
JobResult WaitForInputs(SettlerContext& ctx) {
    ctx.settler.counter = kWaitForInputs;
    return JobResult::Next;
}

// The building may work (request flag bit 0).
bool WantsWork(const Building& building) {
    return (building.request_flags & kRequestWork) != 0;
}

bool OutputFull(CGameData* world, const Building& building) {
    return PileAt(world, building.pile_ring).amount > kPileFull;
}

bool HasInput(const SettlerContext& ctx, std::uint32_t slot) {
    // The original tests AL only.
    return static_cast<std::uint8_t>(Call<kBuildingPileHasGoods>(ctx.world, ctx.settler.building, slot)) != 0;
}

void ReserveInput(const SettlerContext& ctx, std::uint32_t slot) {
    const std::uint32_t pile = BuildingOf(ctx).piles[slot];
    Call<kPileIncAmountLeaving>(ctx.world, pile);
}

// Take one unit of input `slot` and count it off the player's goods.
void Consume(const SettlerContext& ctx, std::uint32_t slot, Good good) {
    const std::uint32_t pile = BuildingOf(ctx).piles[slot];
    Call<kTakeGoodFromPile>(ctx.world, pile);
    Call<kSubtractPlayerCounter>(ctx.world, OwnerOf(ctx), Value(good), 1);
}

// Count a produced unit (statistics and the player's goods).
void CountProduced(const SettlerContext& ctx, std::uint32_t good) {
    Call<kAddStatistic>(ctx.world, OwnerOf(ctx), good, 1);
    Call<kAddPlayerCounter>(ctx.world, OwnerOf(ctx), good, 1);
}

// Put one unit of `good` on the output pile.
void Produce(const SettlerContext& ctx, Good good) {
    Call<kPileIncAmountComing>(ctx.world, BuildingOf(ctx).pile_ring);
    Call<kAddGoodsToPile>(ctx.world, BuildingOf(ctx).pile_ring);
    CountProduced(ctx, Value(good));
}

// The worker occupies (n) or leaves (0) its tile.
void OccupyTile(const SettlerContext& ctx) {
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
}

// A cycle ended: leave the tile and start over with `job` (counter 0; e.g.
// 0x4f108b, 0x4f1425).
JobResult Restart(SettlerContext& ctx, Job job) {
    SettlerTile(ctx).settler = 0;
    SetJob(ctx.settler, job);
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4f73bb (special range): leave the tile, counter 0, run again with `job`.
JobResult RestartNow(SettlerContext& ctx, Job job) {
    SetJob(ctx.settler, job);
    SettlerTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Again;
}

// The start jobs: count the worker in at its building (kind byte +0x19),
// leave the tile, counter 0. 0x4f8f44 -> 0x4ecf12 (special and building
// ranges); with `clearNext` 0x4ece3b (building range), which also clears
// Settler::next (the search position of farmer, fisher, waterworker).
JobResult StartWork(SettlerContext& ctx, Job job, bool clearNext) {
    SetJob(ctx.settler, job);
    ++BuildingOf(ctx).kind_bytes[0];
    SettlerTile(ctx).settler = 0;
    if (clearNext) {
        ctx.settler.next = 0;
    }
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4f4328 (drinks range): counter 0, walk to the building's door.
JobResult WalkHome(SettlerContext& ctx, Job job) {
    SetJob(ctx.settler, job);
    ctx.settler.counter = 0;
    const Building& building = BuildingOf(ctx);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, building.door_x, building.door_y);
    return JobResult::Next;
}

// Spawns the work effect (MapEffects_Spawn_5449f0) at the building's
// position: TileScreenY, then TileScreenX, as the original.
// The coordinates are read before the handler's other calls (the original
// keeps them in [esp+48h] / [esp+4Ch]); the race at the spawn.
void SpawnWorkEffect(const SettlerContext& ctx, std::uint32_t buildingNumber, std::uint32_t x, std::uint32_t y,
                     std::uint32_t variant) {
    const Building& building = BuildingAt(ctx.world, buildingNumber);
    const std::uint32_t screenY = Call<kTileScreenY>(ctx.world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(ctx.world, x, y);
    Call<kSpawn5449f0>(&ctx.world->map_effects, g_gameTick.get(), building.race, variant, buildingNumber, screenX,
                       screenY, x, y);
}

// ------------------------------------------------------------ weapon smith

// The three weapon piles (at the door, right and left of it) hold at most
// 7 units together.
bool WeaponPilesHaveRoom(CGameData* world, const Building& building) {
    const std::uint32_t x = building.door_x;
    const std::uint32_t y = building.door_y;
    const std::uint32_t units = std::uint32_t{PileAt(world, TileAt(world, x, y).object_number).amount} +
                                PileAt(world, TileAt(world, x + 1, y).object_number).amount +
                                PileAt(world, TileAt(world, x - 1, y).object_number).amount;
    return units <= kPileFull;
}

// The door's area has a weapon ordered (settings1) or a share set
// (settings2).
bool AreaWantsWeapons(CGameData* world, const Building& building) {
    const Area& area = AreaAt(world, TileAt(world, building.door_x, building.door_y).area);
    bool wanted = false;
    for (const std::uint32_t weapon : kWeapons) {
        wanted = wanted || area.settings1[weapon] != 0 || area.settings2[weapon] != 0;
    }
    return wanted;
}

// Job 0x75 (0x4f0816): take coal and iron, if the three weapon piles at the
// door have room and the area has weapons ordered or a share set.
JobResult WeaponSmithTakeInputs(SettlerContext& ctx) {
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    if (!HasInput(ctx, 1) || !HasInput(ctx, 2)) {
        return WaitForInputs(ctx);
    }
    if (!WeaponPilesHaveRoom(ctx.world, BuildingOf(ctx)) || !AreaWantsWeapons(ctx.world, BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    ReserveInput(ctx, 0);
    ReserveInput(ctx, 1);
    OccupyTile(ctx);
    return Switch(ctx, Job::Job131, 131);
}

// Job 0x86 (0x4f0b1f): a weapon is done. Ordered weapons first (sword, bow,
// spear: area settings1, counted down); otherwise the weapon whose made
// count (settings3) is lowest relative to its share (settings2). The
// weapon goes on its pile at the door (sword at the door, bow left of it,
// spear right of it). Then the next inputs, as job 0x75.
JobResult WeaponSmithFinish(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    const Building& b = BuildingOf(ctx);
    const std::uint32_t doorX = b.door_x;
    const std::uint32_t doorY = b.door_y;
    const std::uint32_t areaNumber = TileAt(world, doorX, doorY).area;
    Area& area = AreaAt(world, areaNumber);
    std::uint32_t choice = 3;
    for (std::uint32_t k = 0; k < 3 && choice == 3; ++k) {
        if (area.settings1[kWeapons[k]] != 0) {
            --area.settings1[kWeapons[k]];
            choice = k;
        }
    }
    if (choice == 3) {
        // 0x4f0f03: (made * 256 + 128) / share, -1 without a share;
        // compared unsigned, ties go to the earlier weapon.
        std::uint32_t ratio[3];
        for (std::uint32_t k = 0; k < 3; ++k) {
            const std::int32_t share = area.settings2[kWeapons[k]];
            ratio[k] = share == 0 ? 0xffffffffu
                                  : static_cast<std::uint32_t>(
                                        (static_cast<std::int32_t>(area.settings3[kWeapons[k]]) * 256 + 128) / share);
        }
        choice = ratio[0] <= ratio[1] && ratio[0] <= ratio[2] ? 0 : ratio[1] <= ratio[2] ? 1 : 2;
        if (area.settings3[kWeapons[choice]] == 0xff) {
            Call<kClearAreaGoodsSettings1D2>(world, areaNumber);
        }
        ++area.settings3[kWeapons[choice]];
    }
    // Sword: the door tile; bow: x - 1; spear: x + 1 (0x4f0b88, 0x4f0c32,
    // 0x4f0cdc). The pile is read again after PileIncAmountComing.
    const auto pileTile = [&]() -> Tile& {
        const Building& building = BuildingOf(ctx);
        const std::uint32_t x = building.door_x + (choice == 1 ? 0xffffffffu : choice == 2 ? 1u : 0u);
        return TileAt(world, x, building.door_y);
    };
    Call<kPileIncAmountComing>(world, pileTile().object_number);
    Call<kAddGoodsToPile>(world, pileTile().object_number);
    CountProduced(ctx, kWeapons[choice]);
    // 0x4f0d92: the next inputs.
    if (HasInput(ctx, 1) && HasInput(ctx, 2) && WantsWork(BuildingOf(ctx)) &&
        WeaponPilesHaveRoom(world, BuildingOf(ctx)) && AreaWantsWeapons(world, BuildingOf(ctx))) {
        ReserveInput(ctx, 0);
        ReserveInput(ctx, 1);
        return Switch(ctx, Job::Job136, 136);
    }
    return Switch(ctx, Job::Job135, 135);  // 0x4f0ee4, 0x4f106c
}

JobResult RunWeaponSmith(SettlerContext& ctx, Job job) {
    switch (job) {
    case Job::Job108: return StartWork(ctx, Job::Job117, false);  // 0x6c (0x4f10be)
    case Job::Job117: return WeaponSmithTakeInputs(ctx);
    case Job::Job131:  // 0x83 and 0x88 (0x4f09f7): coal
    case Job::Job136:
        Consume(ctx, 0, Good::Coal);
        return Switch(ctx, Job::Job132, 132);
    case Job::Job132: return Switch(ctx, Job::Job133, 133);  // 0x84 (0x4f0a4d)
    case Job::Job133: {  // 0x85 (0x4f0a68): iron, and the anvil effect
        const std::uint32_t building = ctx.settler.building;
        const std::uint32_t x = BuildingAt(ctx.world, building).x;
        const std::uint32_t y = BuildingAt(ctx.world, building).y;
        Consume(ctx, 1, Good::Iron);
        Switch(ctx, Job::Job134, 134);
        SpawnWorkEffect(ctx, building, x, y, 1);
        return JobResult::Next;
    }
    case Job::Job134: return WeaponSmithFinish(ctx);
    case Job::Job135: return Restart(ctx, Job::Job117);  // 0x87 (0x4f108b)
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ------------------------------------------- pig farm (0x77) and donkey farm (0x132)

// Jobs 0x77 / 0x132 (0x4f10e8, 0x4f1466): feed (grain and water) while
// fewer than `animals` animals are in the pen (kind byte +0x1d), or take
// one out once it is full. Only the pig farm (`pileOutput`) checks its
// output pile; the donkeys walk out.
JobResult FarmTakeInputs(SettlerContext& ctx, bool pileOutput, std::uint8_t animals, Job feedJob,
                         std::uint32_t feedAnimation, Job takeOutJob, std::uint32_t takeOutAnimation) {
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    if (HasInput(ctx, 1) && HasInput(ctx, 2) && !(pileOutput && OutputFull(ctx.world, BuildingOf(ctx))) &&
        BuildingOf(ctx).kind_bytes[4] < animals) {
        ReserveInput(ctx, 0);
        ReserveInput(ctx, 1);
        OccupyTile(ctx);
        return Switch(ctx, feedJob, feedAnimation);
    }
    if (BuildingOf(ctx).kind_bytes[4] != animals) {
        return WaitForInputs(ctx);
    }
    OccupyTile(ctx);
    return Switch(ctx, takeOutJob, takeOutAnimation);
}

// Jobs 0xca / 0xd0 (0x4f130b, 0x4f166b): fed; with chance the pen gets an
// animal more (Random above `threshold`).
JobResult FarmFed(SettlerContext& ctx, Job job, std::uint16_t threshold) {
    SettlerTile(ctx).settler = 0;
    SetJob(ctx.settler, job);
    ctx.settler.counter = 0;
    const auto roll = static_cast<std::uint16_t>(Call<kRandom>(ctx.world));
    if (roll > threshold) {
        ++BuildingOf(ctx).kind_bytes[4];
    }
    return JobResult::Next;
}

JobResult TakeOutAnimal(SettlerContext& ctx, Job job, std::uint32_t animation) {
    --BuildingOf(ctx).kind_bytes[4];
    return Switch(ctx, job, animation);
}

JobResult RunAnimalFarms(SettlerContext& ctx, Job job) {
    switch (job) {
    // Pig farm.
    case Job::Job110: return StartWork(ctx, Job::Job119, false);  // 0x6e (0x4f10da)
    case Job::Job119: return FarmTakeInputs(ctx, true, 5, Job::Job200, 181, Job::Job203, 184);
    case Job::Job200: Consume(ctx, 0, Good::Grain); return Switch(ctx, Job::Job201, 182);  // 0xc8 (0x4f125f)
    case Job::Job201: Consume(ctx, 1, Good::Water); return Switch(ctx, Job::Job202, 183);  // 0xc9 (0x4f12b5)
    case Job::Job202: return FarmFed(ctx, Job::Job119, 15000);
    case Job::Job203: return TakeOutAnimal(ctx, Job::Job204, 185);  // 0xcb (0x4f1352)
    case Job::Job204: Produce(ctx, Good::Pig); return Switch(ctx, Job::Job205, 186);  // 0xcc (0x4f1394)
    case Job::Job205: return Restart(ctx, Job::Job119);  // 0xcd (0x4f1425)
    // Donkey farm: the animal walks out (SpawnSettlersAround, type donkey).
    case Job::Job305: return StartWork(ctx, Job::Job306, false);  // 0x131 (0x4f1458)
    case Job::Job306: return FarmTakeInputs(ctx, false, 3, Job::Job206, 206, Job::Job209, 209);
    case Job::Job206: Consume(ctx, 0, Good::Grain); return Switch(ctx, Job::Job207, 207);  // 0xce (0x4f15bf)
    case Job::Job207: Consume(ctx, 1, Good::Water); return Switch(ctx, Job::Job208, 208);  // 0xcf (0x4f1615)
    case Job::Job208: return FarmFed(ctx, Job::Job306, 30000);
    case Job::Job209: return TakeOutAnimal(ctx, Job::Job210, 210);  // 0xd1 (0x4f16df)
    case Job::Job210: {  // 0xd2 (0x4f1721)
        const Building& building = BuildingOf(ctx);
        Call<kSpawnSettlersAround>(ctx.world, 1, Value(SettlerType::Donkey), OwnerOf(ctx), building.door_x,
                                   building.door_y);
        return Switch(ctx, Job::Job211, 211);
    }
    case Job::Job211: return Restart(ctx, Job::Job306);  // 0xd3 (0x4f177a)
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ------------------------------------------------ butcher, miller, baker

// "Take inputs" of the butcher (0x78, 0x4f17bb; one input) and the baker
// (0x7b, 0x4f2742; two inputs); the miller (0x7a, 0x4f22e0) is the
// butcher's shape.
JobResult TakeInputs(SettlerContext& ctx, std::uint32_t inputs, Job job, std::uint32_t animation) {
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    if (!HasInput(ctx, 1) || (inputs == 2 && !HasInput(ctx, 2))) {
        return WaitForInputs(ctx);
    }
    if (OutputFull(ctx.world, BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    ReserveInput(ctx, 0);
    if (inputs == 2) {
        ReserveInput(ctx, 1);
    }
    OccupyTile(ctx);
    return Switch(ctx, job, animation);
}

// Produce, then reserve the next inputs at once (`again`) if the building
// still can work, else leave (`done`): butcher 0xc5 (0x4f18f5), miller 0xa0
// (0x4f24da), baker 0xa6 (0x4f2975).
JobResult ProduceAndContinue(SettlerContext& ctx, Good good, std::uint32_t inputs, Job again,
                             std::uint32_t againAnimation, Job done, std::uint32_t doneAnimation) {
    Produce(ctx, good);
    if (HasInput(ctx, 1) && (inputs == 1 || HasInput(ctx, 2)) && !OutputFull(ctx.world, BuildingOf(ctx)) &&
        WantsWork(BuildingOf(ctx))) {
        ReserveInput(ctx, 0);
        if (inputs == 2) {
            ReserveInput(ctx, 1);
        }
        return Switch(ctx, again, againAnimation);
    }
    return Switch(ctx, done, doneAnimation);
}

// Jobs 0x9e / 0xa2 (0x4f23a9, 0x4f261f, identical): the miller grinds:
// grain, the mill's sails (MapEffects_Spawn_544dc0) and two sound / effect
// records (AllocTable36700Record) whose values come per race from the
// table 0x658adc.
JobResult MillerGrind(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    const std::uint32_t number = ctx.settler.building;
    const Building& building = BuildingAt(world, number);
    const std::uint32_t x = building.x;
    const std::uint32_t y = building.y;
    Consume(ctx, 0, Good::Grain);
    Switch(ctx, Job::Job159, 162);
    const std::uint32_t screenY = Call<kTileScreenY>(world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(world, x, y);
    Call<kSpawn544dc0>(&world->map_effects, g_gameTick.get(), building.race, number, screenX, screenY, x, y);
    const std::int32_t* const table = g_table658adc.get();
    const std::uint32_t first = static_cast<std::uint32_t>(table[building.race]);
    Call<kAllocTable36700Record>(world, 4, first, number, 2, 0xff, 0, 0, 0, 0, 0);
    // Reads past the first four entries into the second table (race + 4), as the original.
    const std::uint32_t second =
        static_cast<std::uint32_t>(table[building.race + 4] + table[building.race] - 1);
    Call<kAllocTable36700Record>(world, 4, second, number, 0, 0xfffffffdu, 0, 0, 0, 0, 0);
    return JobResult::Next;
}

JobResult RunMills(SettlerContext& ctx, Job job) {
    switch (job) {
    // Butcher.
    case Job::Job111: return StartWork(ctx, Job::Job120, false);  // 0x6f (0x4f17ad)
    case Job::Job120: return TakeInputs(ctx, 1, Job::Job195, 175);
    case Job::Job195:  // 0xc3 and 0xc7 (0x4f1884)
    case Job::Job199: Consume(ctx, 0, Good::Pig); return Switch(ctx, Job::Job196, 176);
    case Job::Job196: return Switch(ctx, Job::Job197, 177);  // 0xc4 (0x4f18da)
    case Job::Job197: return ProduceAndContinue(ctx, Good::Meat, 1, Job::Job199, 179, Job::Job198, 178);
    case Job::Job198: return Restart(ctx, Job::Job120);  // 0xc6 (0x4f1a06)
    // Miller.
    case Job::Job113: return StartWork(ctx, Job::Job122, false);  // 0x71 (0x4f22d2)
    case Job::Job122: return TakeInputs(ctx, 1, Job::Job158, 161);
    case Job::Job158:
    case Job::Job162: return MillerGrind(ctx);
    case Job::Job159: return Switch(ctx, Job::Job160, 163);  // 0x9f (0x4f24bf)
    case Job::Job160: return ProduceAndContinue(ctx, Good::Flour, 1, Job::Job162, 165, Job::Job161, 164);
    case Job::Job161: return Restart(ctx, Job::Job122);  // 0xa1 (0x4f25ec)
    // Baker.
    case Job::Job114: return StartWork(ctx, Job::Job123, false);  // 0x72 (0x4f2734)
    case Job::Job123: return TakeInputs(ctx, 2, Job::Job163, 166);
    case Job::Job163:  // 0xa3 and 0xa8 (0x4f284d): water
    case Job::Job168: Consume(ctx, 1, Good::Water); return Switch(ctx, Job::Job164, 167);
    case Job::Job164: {  // 0xa4 (0x4f28a3): flour, and the oven effect
        const std::uint32_t building = ctx.settler.building;
        const std::uint32_t x = BuildingAt(ctx.world, building).x;
        const std::uint32_t y = BuildingAt(ctx.world, building).y;
        Consume(ctx, 0, Good::Flour);
        Switch(ctx, Job::Job165, 168);
        SpawnWorkEffect(ctx, building, x, y, 2);
        return JobResult::Next;
    }
    case Job::Job165: return Switch(ctx, Job::Job166, 169);  // 0xa5 (0x4f295a)
    case Job::Job166: return ProduceAndContinue(ctx, Good::Bread, 2, Job::Job168, 171, Job::Job167, 170);
    case Job::Job167: return Restart(ctx, Job::Job123);  // 0xa7 (0x4f2acc)
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ------------------------------------------------------------------ farmer

// A free, flat grass tile with its six neighbours free (no landscape
// object, no pile or building, not blocked): a field can be sown here (the
// checks of 0x4f1bfa..0x4f1d06 and 0x4f1eba..0x4f1fef). TileSlope runs
// only if everything else passes.
bool CanSow(CGameData* world, std::uint32_t x, std::uint32_t y) {
    const std::int32_t around[7][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}};
    for (const auto& d : around) {
        const Tile& tile = TileAt(world, x + d[0], y + d[1]);
        if (tile.object != 0 || tile.object_number != 0) {
            return false;
        }
    }
    for (const auto& d : around) {
        if ((TileAt(world, x + d[0], y + d[1]).flags & kTileNotFree) != 0) {
            return false;
        }
    }
    if ((TileAt(world, x, y).terrain & kTerrainClassMask) != kTerrainClassGrass) {
        return false;
    }
    return Call<kTileSlope>(world, x, y) < kMaxSlope;
}

bool SameAreaAsDoor(CGameData* world, const Building& building, std::uint32_t x, std::uint32_t y) {
    return TileAt(world, x, y).area == TileAt(world, building.door_x, building.door_y).area;
}

// The search position: Settler::next walks through the offset table around
// the work spot, kSearchSteps entries per try; it advances by the entries
// tried (at least one) and wraps at `wrap`.
void AdvanceSearch(SettlerContext& ctx, std::uint32_t tried, std::uint16_t wrap) {
    ctx.settler.next = static_cast<std::uint16_t>(ctx.settler.next + tried);
    if (tried == 0) {
        ctx.settler.next = static_cast<std::uint16_t>(ctx.settler.next + 1);
    }
    if (ctx.settler.next >= wrap) {
        ctx.settler.next = 0;
    }
}

const layout::MapOffset& SearchOffset(CGameData* world, const Settler& settler, std::uint32_t k) {
    return *(world->offsets + (std::uint32_t{settler.next} + k));
}

// Job 0x79 (0x4f1a47): look for a ripe field to harvest (while the output
// pile has room) or a tile to sow, then walk there.
JobResult FarmerSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    enum class Found { Nothing, Sow, Harvest };
    Found found = Found::Nothing;
    std::uint32_t k = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (; k < kSearchSteps; ++k) {
        const Building& building = BuildingOf(ctx);
        const layout::MapOffset& offset = SearchOffset(world, ctx.settler, k);
        x = building.work_x + static_cast<std::uint32_t>(offset.dx);
        y = building.work_y + static_cast<std::uint32_t>(offset.dy);
        ctx.frame.slot20 = x;
        ctx.frame.slot14 = y;
        const std::uint32_t last = world->map_size - 1;
        if (x < 1 || x >= last || y < 1 || y >= last) {
            continue;
        }
        if (!OutputFull(world, building)) {
            const std::uint8_t object = TileAt(world, x, y).object;
            if (object >= kFieldRipeFirst && object <= kFieldRipeLast && SameAreaAsDoor(world, building, x, y)) {
                found = Found::Harvest;
                break;
            }
        }
        if (!SameAreaAsDoor(world, building, x, y)) {
            continue;
        }
        if (CanSow(world, x, y)) {
            found = Found::Sow;
            break;
        }
    }
    AdvanceSearch(ctx, k, 300);
    if (found == Found::Nothing) {
        return WaitForInputs(ctx);
    }
    ctx.settler.counter = 1;
    ctx.settler.next = 0;
    if (Call<kCanSettlerEnterTile>(world, x, y, ctx.settler.owner, ctx.settler.art) == 0) {
        return JobResult::Next;
    }
    OccupyTile(ctx);
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    return found == Found::Harvest ? Switch(ctx, Job::Job149, 160) : Switch(ctx, Job::Job143, 155);
}

// Job 0x91 (0x4f1e93): sow the field here (seven terrain patches), or walk
// home if the tile no longer fits.
JobResult FarmerSow(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    if (!CanSow(world, ctx.settler.x, ctx.settler.y)) {
        return WalkHome(ctx, Job::Job147);
    }
    const std::int32_t patches[7][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}};
    for (const auto& d : patches) {
        const std::uint32_t x = ctx.settler.x + d[0];
        const std::uint32_t y = ctx.settler.y + d[1];
        Call<kMakeFieldTile>(world, x, y);
    }
    return Switch(ctx, Job::Job146, 156);
}

// Job 0x97 (0x4f218a): harvest in two steps (Settler +0x17 counts them).
JobResult FarmerHarvest(SettlerContext& ctx) {
    const auto step = static_cast<std::uint8_t>(ctx.settler.food + 1);
    ctx.settler.food = step;
    if (step == 1) {
        Animate(ctx, 159);
        ctx.settler.slot = 2;
        return JobResult::Next;
    }
    if (step == 2) {
        SettlerTile(ctx).object = kFieldHarvested;
        return WalkHome(ctx, Job::Job152);
    }
    return JobResult::Next;
}

// Go into the building (MoveSettlerToBuildingTileA): 0x4f221a, 0x4f2eda,
// 0x4f3294.
JobResult GoIn(SettlerContext& ctx, Job job, std::uint32_t animation) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    return Switch(ctx, job, animation);
}

// Back at the building: drop the grain if it carries one (Settler::slot),
// else go in (0x4f21f5 fisher shape: 0x99 / 0x9b).
JobResult ComeHome(SettlerContext& ctx, Job deliver, std::uint32_t deliverAnimation, Job goIn,
                   std::uint32_t goInAnimation) {
    if (ctx.settler.slot != 0) {
        return Switch(ctx, deliver, deliverAnimation);
    }
    return GoIn(ctx, goIn, goInAnimation);
}

JobResult RunFarmer(SettlerContext& ctx, Job job) {
    switch (job) {
    case Job::Job112: return StartWork(ctx, Job::Job121, true);  // 0x70 (0x4f1a39)
    case Job::Job121: return FarmerSearch(ctx);
    case Job::Job143:  // 0x8f (0x4f1e6d): walk out to sow
        Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
        ctx.settler.slot = 1;
        ctx.settler.counter = 0;
        SetJob(ctx.settler, Job::Job144);
        return JobResult::Again;
    case Job::Job145: return FarmerSow(ctx);
    case Job::Job146:  // 0x92 (0x4f20d5): sown
        SettlerTile(ctx).object = kFieldSown;
        return WalkHome(ctx, Job::Job147);
    case Job::Job148:  // 0x94 and 0x9d (0x4f2108)
    case Job::Job157: return RestartNow(ctx, Job::Job121);
    case Job::Job149:  // 0x95 (0x4f2116): walk out to harvest
        Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
        ctx.settler.slot = 0;
        ctx.settler.counter = 0;
        SetJob(ctx.settler, Job::Job156);
        return JobResult::Again;
    case Job::Job150: {  // 0x96 (0x4f213c): at the field, still ripe?
        const std::uint8_t object = SettlerTile(ctx).object;
        if (object < kFieldRipeFirst || object > kFieldRipeLast) {
            return WalkHome(ctx, Job::Job147);
        }
        Switch(ctx, Job::Job151, 158);
        ctx.settler.food = 0;
        return JobResult::Next;
    }
    case Job::Job151: return FarmerHarvest(ctx);
    case Job::Job153: return ComeHome(ctx, Job::Job154, 172, Job::Job157, 121);  // 0x99 (0x4f21f5)
    case Job::Job155: return GoIn(ctx, Job::Job157, 121);                         // 0x9b (0x4f221a)
    case Job::Job154:  // 0x9a (0x4f223a): the grain onto the pile
        Produce(ctx, Good::Grain);
        Switch(ctx, Job::Job155, 173);
        ctx.settler.slot = 0;
        return JobResult::Next;
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ------------------------------------------------------------------ fisher

// A tile next to water: one of its six neighbours has terrain class 0.
bool NextToWater(CGameData* world, std::uint32_t x, std::uint32_t y) {
    const std::int32_t around[6][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
    for (const auto& d : around) {
        if ((TileAt(world, x + d[0], y + d[1]).terrain & kTerrainClassMask) == kTerrainClassWater) {
            return true;
        }
    }
    return false;
}

// Job 0x76 (0x4f2b0d): look for a shore tile next to water in the
// building's area, then walk there.
JobResult FisherSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    if (OutputFull(world, BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    bool found = false;
    std::uint32_t k = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (; k < kSearchSteps; ++k) {
        const Building& building = BuildingOf(ctx);
        const layout::MapOffset& offset = SearchOffset(world, ctx.settler, k);
        x = building.work_x + static_cast<std::uint32_t>(offset.dx);
        y = building.work_y + static_cast<std::uint32_t>(offset.dy);
        ctx.frame.slot20 = x;
        ctx.frame.slot14 = y;
        if (Call<kIsOnMap>(world, x, y) == 0) {
            continue;
        }
        if ((TileAt(world, x, y).terrain & kTerrainClassMask) != kTerrainClassShore ||
            !SameAreaAsDoor(world, BuildingOf(ctx), x, y)) {
            continue;
        }
        if (NextToWater(world, x, y)) {
            found = true;
            break;
        }
    }
    AdvanceSearch(ctx, k, 2000);
    if (!found) {
        return WaitForInputs(ctx);
    }
    OccupyTile(ctx);
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    return Switch(ctx, Job::Job177, 191);
}

// Job 0xb3 (0x4f2d88): at the shore: cast towards a random water
// neighbour (turning until one is water). A catch (Random & 7 below the
// tile's resources, which it lowers) is remembered in Settler::slot (8 =
// fish); the animation is the direction + 192 (catch) or + 198. Without
// water around: walk home (job 0xb5).
JobResult FisherCast(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    if (!NextToWater(world, ctx.settler.x, ctx.settler.y)) {
        return WalkHome(ctx, Job::Job181);
    }
    std::uint32_t direction = Call<kRandomBelow6>(world);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (;;) {
        x = CallStd<kNeighbourX>(ctx.settler.x, direction);
        ctx.frame.slot20 = x;
        y = CallStd<kNeighbourY>(ctx.settler.y, direction);
        ctx.frame.slot14 = y;
        if ((TileAt(world, x, y).terrain & kTerrainClassMask) == kTerrainClassWater) {
            break;
        }
        direction = CallStd<kNextDirection>(direction);
    }
    std::uint8_t& fish = TileAt(world, x, y).resources;
    const auto roll = static_cast<std::uint16_t>(Call<kRandom>(world) & 7);
    std::uint32_t animation = 0;
    if (roll < fish) {
        ctx.settler.slot = Value(Good::Fish);
        --fish;
        animation = direction + 192;
    } else {
        ctx.settler.slot = 0;
        animation = direction + 198;
    }
    Animate(ctx, animation);
    SetJob(ctx.settler, Job::Job180);
    return JobResult::Next;
}

JobResult RunFisher(SettlerContext& ctx, Job job) {
    switch (job) {
    case Job::Job109: return StartWork(ctx, Job::Job118, true);  // 0x6d (0x4f10cc)
    case Job::Job118: return FisherSearch(ctx);
    case Job::Job177:  // 0xb1 (0x4f2d69): walk out
        Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
        ctx.settler.counter = 0;
        SetJob(ctx.settler, Job::Job178);
        return JobResult::Again;
    case Job::Job179: return FisherCast(ctx);
    case Job::Job180: return WalkHome(ctx, Job::Job181);  // 0xb4 (0x4f2de1)
    case Job::Job182: return ComeHome(ctx, Job::Job183, 204, Job::Job185, 118);  // 0xb6 (0x4f2eb5)
    case Job::Job184: return GoIn(ctx, Job::Job185, 118);                         // 0xb8 (0x4f2eda)
    case Job::Job183:  // 0xb7 (0x4f2efa): the fish onto the pile
        Produce(ctx, Good::Fish);
        Switch(ctx, Job::Job184, 205);
        ctx.settler.slot = 0;
        return JobResult::Next;
    case Job::Job185: return RestartNow(ctx, Job::Job118);  // 0xb9 (0x4f2f92)
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ------------------------------------------------------------- waterworker

// Job 0x7c (0x4f2fa0): look for a tile of terrain class 0x60 in the
// building's area, then walk there.
JobResult WaterworkerSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    if (!WantsWork(BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    if (OutputFull(world, BuildingOf(ctx))) {
        return WaitForInputs(ctx);
    }
    bool found = false;
    std::uint32_t k = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (; k < kSearchSteps; ++k) {
        const Building& building = BuildingOf(ctx);
        const layout::MapOffset& offset = SearchOffset(world, ctx.settler, k);
        x = building.work_x + static_cast<std::uint32_t>(offset.dx);
        y = building.work_y + static_cast<std::uint32_t>(offset.dy);
        ctx.frame.slot20 = x;
        ctx.frame.slot14 = y;
        const std::uint32_t last = world->map_size - 1;
        if (x < 1 || x >= last || y < 1 || y >= last) {
            continue;
        }
        if ((TileAt(world, x, y).terrain & kTerrainClassMask) == kTerrainClass60 &&
            SameAreaAsDoor(world, building, x, y)) {
            found = true;
            break;
        }
    }
    AdvanceSearch(ctx, k, 800);
    if (!found) {
        return WaitForInputs(ctx);
    }
    OccupyTile(ctx);
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    return Switch(ctx, Job::Job186, 187);
}

JobResult RunWaterworker(SettlerContext& ctx, Job job) {
    switch (job) {
    case Job::Job115: return StartWork(ctx, Job::Job124, true);  // 0x73 (0x4f2aff)
    case Job::Job124: return WaterworkerSearch(ctx);
    case Job::Job186:  // 0xba (0x4f31c5): walk out
        Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
        ctx.settler.counter = 0;
        SetJob(ctx.settler, Job::Job187);
        return JobResult::Again;
    case Job::Job189:  // 0xbd (0x4f31e4): draw water, if still there
        if ((SettlerTile(ctx).terrain & kTerrainClassMask) != kTerrainClass60) {
            return WalkHome(ctx, Job::Job188);
        }
        ctx.settler.slot = Value(Good::Water);
        Animate(ctx, 188);
        SetJob(ctx.settler, Job::Job190);
        return JobResult::Next;
    case Job::Job190: return WalkHome(ctx, Job::Job188);  // 0xbe (0x4f3261)
    case Job::Job191: return ComeHome(ctx, Job::Job192, 189, Job::Job194, 124);  // 0xbf (0x4f326f)
    case Job::Job193: return GoIn(ctx, Job::Job194, 124);                         // 0xc1 (0x4f3294)
    case Job::Job192:  // 0xc0 (0x4f32b4): the water onto the pile
        Produce(ctx, Good::Water);
        Switch(ctx, Job::Job193, 190);
        ctx.settler.slot = 0;
        return JobResult::Next;
    case Job::Job194: return RestartNow(ctx, Job::Job124);  // 0xc2 (0x4f334c)
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

}  // namespace

// Purpose: The food chain's workers (and the weapon smith, whose code sits
// among them): one step of their current job.
JobResult RunFoodJobs(SettlerContext& ctx) {
    const auto job = static_cast<Job>(ctx.job);
    switch (job) {
    case Job::Job108: case Job::Job117: case Job::Job131: case Job::Job132: case Job::Job133: case Job::Job134:
    case Job::Job135: case Job::Job136:
        return RunWeaponSmith(ctx, job);
    case Job::Job110: case Job::Job119: case Job::Job200: case Job::Job201: case Job::Job202: case Job::Job203:
    case Job::Job204: case Job::Job205: case Job::Job305: case Job::Job306: case Job::Job206: case Job::Job207:
    case Job::Job208: case Job::Job209: case Job::Job210: case Job::Job211:
        return RunAnimalFarms(ctx, job);
    case Job::Job111: case Job::Job120: case Job::Job195: case Job::Job196: case Job::Job197: case Job::Job198:
    case Job::Job199: case Job::Job113: case Job::Job122: case Job::Job158: case Job::Job159: case Job::Job160:
    case Job::Job161: case Job::Job162: case Job::Job114: case Job::Job123: case Job::Job163: case Job::Job164:
    case Job::Job165: case Job::Job166: case Job::Job167: case Job::Job168:
        return RunMills(ctx, job);
    case Job::Job112: case Job::Job121: case Job::Job143: case Job::Job145: case Job::Job146: case Job::Job148:
    case Job::Job149: case Job::Job150: case Job::Job151: case Job::Job153: case Job::Job154: case Job::Job155:
    case Job::Job157:
        return RunFarmer(ctx, job);
    case Job::Job109: case Job::Job118: case Job::Job177: case Job::Job179: case Job::Job180: case Job::Job182:
    case Job::Job183: case Job::Job184: case Job::Job185:
        return RunFisher(ctx, job);
    case Job::Job115: case Job::Job124: case Job::Job186: case Job::Job189: case Job::Job190: case Job::Job191:
    case Job::Job192: case Job::Job193: case Job::Job194:
        return RunWaterworker(ctx, job);
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

}  // namespace s3re::game::settler_jobs

// ---------------------------------------------------------------------------
// Tests (settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace s3re::game {

namespace {

using settler_jobs::testing::CaseSetup;
using settler_jobs::testing::FamilyTest;
using settler_jobs::Building;
using settler_jobs::Settler;
using settler_jobs::Tile;
using Job = SettlerJob;

constexpr Job kFoodJobs[] = {
    Job::Job108, Job::Job117, Job::Job131, Job::Job132, Job::Job133, Job::Job134, Job::Job135, Job::Job136,
    Job::Job110, Job::Job119, Job::Job200, Job::Job201, Job::Job202, Job::Job203, Job::Job204, Job::Job205,
    Job::Job305, Job::Job306, Job::Job206, Job::Job207, Job::Job208, Job::Job209, Job::Job210, Job::Job211,
    Job::Job111, Job::Job120, Job::Job195, Job::Job196, Job::Job197, Job::Job198, Job::Job199, Job::Job113,
    Job::Job122, Job::Job158, Job::Job159, Job::Job160, Job::Job161, Job::Job162, Job::Job114, Job::Job123,
    Job::Job163, Job::Job164, Job::Job165, Job::Job166, Job::Job167, Job::Job168, Job::Job112, Job::Job121,
    Job::Job143, Job::Job145, Job::Job146, Job::Job148, Job::Job149, Job::Job150, Job::Job151, Job::Job153,
    Job::Job154, Job::Job155, Job::Job157, Job::Job109, Job::Job118, Job::Job177, Job::Job179, Job::Job180,
    Job::Job182, Job::Job183, Job::Job184, Job::Job185, Job::Job115, Job::Job124, Job::Job186, Job::Job189,
    Job::Job190, Job::Job191, Job::Job192, Job::Job193, Job::Job194};
static_assert(sizeof(kFoodJobs) / sizeof(kFoodJobs[0]) == 77);

constexpr SettlerType kFoodTypes[] = {SettlerType::Smith,  SettlerType::PigFarmer, SettlerType::Butcher,
                                      SettlerType::Farmer, SettlerType::Miller,    SettlerType::Baker,
                                      SettlerType::Fisher, SettlerType::Waterworker};

// Terrain and landscape values the handlers look for, with some others.
constexpr std::uint8_t kTerrains[] = {0x10, 0x10, 0x10, 0x30, 0x30, 0x00, 0x05, 0x60, 0x60, 0x20, 0x40};
constexpr std::uint8_t kObjects[] = {0x55, 0x5a, 0x5b, 0x5c, 0x5d, 0x59, 0x01, 0x80};

// A patch of tiles around (cx, cy): mostly free grass, shore, water and
// class 0x60 tiles in the area `area`, fields here and there.
void FoodPatch(CaseSetup& setup, std::uint32_t cx, std::uint32_t cy, std::int32_t radius, std::uint16_t area) {
    auto& rng = setup.rng();
    for (std::int32_t dx = -radius; dx <= radius; ++dx) {
        for (std::int32_t dy = -radius; dy <= radius; ++dy) {
            const std::uint8_t terrain = kTerrains[rng.Below(sizeof(kTerrains))];
            const auto object = static_cast<std::uint8_t>(rng.Chance(7, 10) ? 0 : kObjects[rng.Below(sizeof(kObjects))]);
            const auto number = static_cast<std::uint16_t>(rng.Chance(17, 20) ? 0 : 1 + rng.Below(400));
            const auto flags = static_cast<std::uint8_t>(rng.Chance(17, 20) ? rng.Next32() & 0xf6 : rng.Next32());
            const auto tileArea = static_cast<std::uint16_t>(rng.Chance(17, 20) ? area : rng.Below(64));
            const auto resources = static_cast<std::uint8_t>(rng.Below(8));
            const std::uint32_t x = cx + static_cast<std::uint32_t>(dx);
            const std::uint32_t y = cy + static_cast<std::uint32_t>(dy);
            setup.Both([&](CGameData* w) {
                Tile& t = w->tiles[x][y];
                t.terrain = terrain;
                t.object = object;
                t.object_number = number;
                t.flags = flags;
                t.area = tileArea;
                t.resources = resources;
            });
        }
    }
}

void FoodCustomize(CaseSetup& setup, std::uint32_t n) {
    auto& rng = setup.rng();
    const std::uint16_t building = static_cast<std::uint16_t>(1 + rng.Below(settler_jobs::testing::kBuildings));
    const auto requests = static_cast<std::uint8_t>(rng.Chance(4, 5) ? rng.Next32() | 1 : rng.Next32() & 0xfe);
    const auto animals = static_cast<std::uint8_t>(rng.Below(7));
    const auto doorX = static_cast<std::uint16_t>(40 + rng.Below(300));
    const auto doorY = static_cast<std::uint16_t>(40 + rng.Below(300));
    const auto workX = static_cast<std::uint16_t>(doorX + rng.Range(-3, 3));
    const auto workY = static_cast<std::uint16_t>(doorY + rng.Range(-3, 3));
    const auto area = static_cast<std::uint16_t>(1 + rng.Below(60));
    const auto next = static_cast<std::uint16_t>(rng.Chance(9, 10) ? rng.Below(40) : rng.Below(2100));
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(30));
    const auto food = static_cast<std::uint8_t>(rng.Below(4));
    const auto x = static_cast<std::uint16_t>(workX + rng.Range(-4, 4));
    const auto y = static_cast<std::uint16_t>(workY + rng.Range(-4, 4));
    std::uint16_t piles[4];
    std::uint8_t amounts[4];
    for (int k = 0; k < 4; ++k) {
        piles[k] = static_cast<std::uint16_t>(1 + rng.Below(400));
        amounts[k] = static_cast<std::uint8_t>(rng.Chance(3, 4) ? rng.Below(9) : rng.Below(256));
    }
    std::uint8_t settings[3][3];
    for (auto& row : settings) {
        for (auto& v : row) {
            v = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Chance(1, 8) ? 0xff : rng.Below(10));
        }
    }
    setup.Both([&](CGameData* w) {
        Settler& s = settler_jobs::SettlerAt(w, n);
        s.building = building;
        s.next = next;
        s.slot = slot;
        s.food = food;
        s.x = x;
        s.y = y;
        Building& b = settler_jobs::BuildingAt(w, building);
        b.request_flags = requests;
        b.kind_bytes[4] = animals;
        b.door_x = doorX;
        b.door_y = doorY;
        b.work_x = workX;
        b.work_y = workY;
        b.pile_ring = piles[0];
        settler_jobs::PileAt(w, piles[0]).amount = amounts[0];
        // The three weapon piles at the door.
        for (int k = 1; k < 4; ++k) {
            const std::uint32_t tx = doorX + static_cast<std::uint32_t>(k == 1 ? 0 : k == 2 ? 1 : -1);
            w->tiles[tx][doorY].object_number = piles[k];
            settler_jobs::PileAt(w, piles[k]).amount = static_cast<std::uint8_t>(amounts[k] & 3);
        }
        layout::Area& a = settler_jobs::AreaAt(w, area);
        for (int k = 0; k < 3; ++k) {
            a.settings1[18 + k] = settings[0][k];
            a.settings2[18 + k] = settings[1][k];
            a.settings3[18 + k] = settings[2][k];
        }
    });
    FoodPatch(setup, workX, workY, 7, area);
    FoodPatch(setup, x, y, 2, area);
    // FoodPatch may have put another pile number on the door tiles.
    setup.Both([&](CGameData* w) {
        w->tiles[doorX][doorY].area = area;
        for (int k = 1; k < 4; ++k) {
            const std::uint32_t tx = doorX + static_cast<std::uint32_t>(k == 1 ? 0 : k == 2 ? 1 : -1);
            w->tiles[tx][doorY].object_number = piles[k];
        }
    });
    // The offset table: small steps around the work spot.
    for (std::uint32_t k = 0; k < 100; ++k) {
        const std::int32_t dx = rng.Range(-6, 6);
        const std::int32_t dy = rng.Range(-6, 6);
        setup.Both([&](CGameData* w) {
            w->offsets[k].dx = dx;
            w->offsets[k].dy = dy;
        });
    }
}

// More "goods there" / "may enter" answers, so the work cycles get further.
bool FoodStubReturn(std::uintptr_t callee, std::uint32_t h, const std::uint32_t*, CGameData*, std::uint32_t& eax) {
    if (callee == settler_jobs::kBuildingPileHasGoods || callee == settler_jobs::kCanSettlerEnterTile) {
        eax = (h & 7) != 0 ? 1 : 0;
        return true;
    }
    return false;
}

FamilyTest FoodFamily() {
    FamilyTest family{kFoodJobs, sizeof(kFoodJobs) / sizeof(kFoodJobs[0])};
    family.types = kFoodTypes;
    family.typeCount = sizeof(kFoodTypes) / sizeof(kFoodTypes[0]);
    family.customize = &FoodCustomize;
    family.stubReturn = &FoodStubReturn;
    family.minCompared = 1000;
    return family;
}

// Wrong: the farmer's search starts one offset further.
std::uint32_t __fastcall WrongFarmerSearch(CGameData* world, void* edx) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        if (world->settlers[n].job == Value(Job::Job121)) {
            ++world->settlers[n].next;
        }
    }
    return CGameData_UpdateSettlers(world, edx);
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Food_MatchesOriginal) {
    settler_jobs::testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, FoodFamily(), 4000);
}

S3RE_TEST(CGameData_UpdateSettlers_Food_ComparisonDetectsWrongSearch) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FamilyTest family = FoodFamily();
    family.minCompared = 0;
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongFarmerSearch, family, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
