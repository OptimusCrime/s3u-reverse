// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4ee0c1..0x4f0816 of
// the original (settler_jobs.h): the sawmill's work cycle (0x31..0x34), the
// gold and iron smelters (0x3a..0x4b), the miners (0x52..0x63, 0xd6..0xe1),
// jobs 0x186..0x18f (NoOp_ret4), a settler leaving its house (0x64), dying
// settlers (0x36, 0xaf), the toolsmith (0x6b, 0x74, 0x7d..0x82) and the war
// machine workshops (0x128..0x139, "Katapultproduktion").
//
// Most work cycles follow the same steps, one job per step (the settler's
// counter, set by SetSettlerAnimation, makes each last an animation):
//   start  - wait until the building wants work (request flag bit 0) and its
//            input piles have goods and the output pile has room, reserve
//            one unit of each input (PileIncAmountLeaving), stand on the
//            work tile (Tile::settler);
//   consume - take the inputs (TakeGoodFromPile, SubtractPlayerCounter);
//   work    - animations;
//   produce - add the product to the output pile (PileIncAmountComing,
//            AddGoodsToPile, AddStatistic, AddPlayerCounter), then either
//            start the next round at once or leave the work tile.
// "Wait" sets the counter to 10 (the shared code at 0x4f8b7c) and keeps the
// job.
//
// Building fields used (layout::Building): pile_ring is the output pile,
// piles[0..2] the inputs; war machine workshops (BuildingWaterBytes): the
// three input amounts wanted +0x1a..+0x1c, state +0x1f (0x10 + k: a
// catapult-like unit is done, 0x20 + k: a ship-like order is done) and
// +0x20 the units still to be put in; +0x19 counts workers starting
// (also in the other families' start jobs, 0x4f8f44).
//
// Every callee is called through its S3.EXE address.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"
#include "test/callee_stub.h"

namespace s3re::game::settler_jobs {

namespace {

using G = Good;

constexpr std::uint16_t kWaitTicks = 10;           // the shared "wait" 0x4f8b7c
constexpr std::uint8_t kOutputPileFull = 7;        // more than 7 units: no room
constexpr std::uint8_t kRequestWork = 0x01;        // Building::request_flags bit 0
constexpr std::uint8_t kDeadOwner = 0xff;

// ----------------------------------------------------------------- helpers

// Adds to a 32-bit counter with wrap-around (as the original's add).
void AddWrapping(std::int32_t& counter, std::uint32_t amount) {
    counter = static_cast<std::int32_t>(static_cast<std::uint32_t>(counter) + amount);
}

// The worker's building, re-read from the settler like the original does
// before every use.
Building& WorkBuilding(SettlerContext& ctx) { return BuildingAt(ctx.world, ctx.settler.building); }
std::uint32_t Owner(const SettlerContext& ctx) { return ctx.settler.owner; }
Tile& OwnTile(SettlerContext& ctx) { return TileAt(ctx.world, ctx.settler.x, ctx.settler.y); }

std::uint32_t HasGoods(SettlerContext& ctx, std::uint32_t slot) {
    // BuildingPileHasGoods returns a bool in AL.
    return Call<kBuildingPileHasGoods>(ctx.world, ctx.settler.building, slot) & 0xff;
}

// Sets the next job, then starts its animation (in this order, as the original).
JobResult Animate(SettlerContext& ctx, Job next, std::uint32_t animation) {
    SetJob(ctx.settler, next);
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
    return JobResult::Next;
}

// 0x4f8b7c (and the inline copies): try again in ten ticks.
JobResult Wait(SettlerContext& ctx) {
    ctx.settler.counter = kWaitTicks;
    return JobResult::Next;
}

// Leaves the work tile and goes on with `next` at once (counter = EDX = 0,
// jump to 0x4f91fb).
JobResult LeaveWorkTile(SettlerContext& ctx, Job next) {
    OwnTile(ctx).settler = 0;
    SetJob(ctx.settler, next);
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4f8f44 + 0x4ecf12 (shared with other families): a worker starts its
// first round: counts itself into building byte +0x19, leaves the tile,
// counter 0.
JobResult StartFirstRound(SettlerContext& ctx, Job next) {
    SetJob(ctx.settler, next);
    ++WorkBuilding(ctx).kind_bytes[0];
    OwnTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// Takes one unit of an input pile and books it off the player's goods.
// `good` < 0: the pile's own goods type, read after the take.
void ConsumeInput(SettlerContext& ctx, std::uint32_t slot, std::int32_t good) {
    Call<kTakeGoodFromPile>(ctx.world, WorkBuilding(ctx).piles[slot]);
    const std::uint32_t goods =
        good >= 0 ? static_cast<std::uint32_t>(good) : PileAt(ctx.world, WorkBuilding(ctx).piles[slot]).good;
    Call<kSubtractPlayerCounter>(ctx.world, Owner(ctx), goods, 1);
}

// Puts one unit of `good` on the output pile and counts it.
void ProduceToOutput(SettlerContext& ctx, Good good) {
    Call<kPileIncAmountComing>(ctx.world, WorkBuilding(ctx).pile_ring);
    Call<kAddGoodsToPile>(ctx.world, WorkBuilding(ctx).pile_ring);
    Call<kAddStatistic>(ctx.world, Owner(ctx), Value(good), 1);
    Call<kAddPlayerCounter>(ctx.world, Owner(ctx), Value(good), 1);
}

bool OutputHasRoom(SettlerContext& ctx) {
    return PileAt(ctx.world, WorkBuilding(ctx).pile_ring).amount <= kOutputPileFull;
}

// After producing: another round if the inputs (one or two) are there, the
// output pile has room and the building still wants work.
bool CanWorkAgain(SettlerContext& ctx, bool twoInputs) {
    if (HasGoods(ctx, 1) == 0) {
        return false;
    }
    if (twoInputs && HasGoods(ctx, 2) == 0) {
        return false;
    }
    if (!OutputHasRoom(ctx)) {
        return false;
    }
    return (WorkBuilding(ctx).request_flags & kRequestWork) != 0;
}

// Reserves one unit of the first (and second) input.
void ReserveInputs(SettlerContext& ctx, bool twoInputs) {
    Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[0]);
    if (twoInputs) {
        Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[1]);
    }
}

// The start of a two-input round (0x4ee252 smelters, 0x4ee6cb): wait until
// the building wants work, both inputs have goods and the output has room.
JobResult StartTwoInputRound(SettlerContext& ctx, Job next, std::uint32_t animation) {
    if ((WorkBuilding(ctx).request_flags & kRequestWork) == 0) {
        return Wait(ctx);
    }
    if (HasGoods(ctx, 1) == 0 || HasGoods(ctx, 2) == 0 || !OutputHasRoom(ctx)) {
        return Wait(ctx);
    }
    ReserveInputs(ctx, true);
    OwnTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    return Animate(ctx, next, animation);
}

// ----------------------------------------------------------------- sawmill

// 0x4ee0c1 (jobs 0x31, 0x34): the sawmiller takes a log.
JobResult SawmillTakeLog(SettlerContext& ctx) {
    ConsumeInput(ctx, 0, Value(G::Log));
    return Animate(ctx, Job::Job50, 24);
}

// 0x4ee114 (job 0x32): a board is done.
JobResult SawmillBoardDone(SettlerContext& ctx) {
    ProduceToOutput(ctx, G::Board);
    if (CanWorkAgain(ctx, false)) {
        ReserveInputs(ctx, false);
        return Animate(ctx, Job::Job52, 26);
    }
    return Animate(ctx, Job::Job51, 25);
}

// ----------------------------------------------------------------- smelters

// 0x4ee35a / 0x4ee7d3 (jobs 0x3a, 0x40 / 0x44, 0x4a): take the ore (second input).
JobResult SmelterTakeOre(SettlerContext& ctx, Job next, std::uint32_t animation) {
    ConsumeInput(ctx, 1, -1);
    return Animate(ctx, next, animation);
}

// 0x4ee3de / 0x4ee857 (jobs 0x3b / 0x45): take the coal and show the
// furnace (MapEffects 0x544630 at the building; variant 1 gold, 0 iron).
JobResult SmelterTakeCoal(SettlerContext& ctx, Job next, std::uint32_t animation, std::uint32_t variant) {
    const std::uint32_t building = ctx.settler.building;
    const std::uint32_t x = BuildingAt(ctx.world, building).x;
    const std::uint32_t y = BuildingAt(ctx.world, building).y;
    ConsumeInput(ctx, 0, -1);
    Animate(ctx, next, animation);
    const std::uint32_t screenY = Call<kTileScreenY>(ctx.world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(ctx.world, x, y);
    const std::uint32_t tick = g_gameTick;
    Call<kSpawn544630>(&ctx.world->map_effects, tick, BuildingAt(ctx.world, building).race, variant, building, screenX,
                       screenY, x, y);
    return JobResult::Next;
}

// 0x4ee4ff (job 0x3e): a gold bar is done; the player's points tally grows
// by 1 for race 1, else by 2.
JobResult GoldBarDone(SettlerContext& ctx) {
    const std::uint32_t owner = Owner(ctx);
    AddWrapping(g_playerTallies[owner].points, PlayerAt(ctx.world, owner).race == 1 ? 1u : 2u);
    ProduceToOutput(ctx, G::Gold);
    if (CanWorkAgain(ctx, true)) {
        ReserveInputs(ctx, true);
        return Animate(ctx, Job::Job64, 79);
    }
    return Animate(ctx, Job::Job63, 78);
}

// 0x4ee978 (job 0x48): an iron bar is done.
JobResult IronBarDone(SettlerContext& ctx) {
    ProduceToOutput(ctx, G::Iron);
    if (CanWorkAgain(ctx, true)) {
        ReserveInputs(ctx, true);
        return Animate(ctx, Job::Job74, 88);
    }
    return Animate(ctx, Job::Job73, 87);
}

// ----------------------------------------------------------------- miners

// 0x4eeb71, 0x4eec32, 0x4eecf3, 0x4eedb4, 0x4eee78: a mine's product is out.
JobResult MineProductDone(SettlerContext& ctx, Good good, Job next, std::uint32_t animation) {
    ProduceToOutput(ctx, good);
    return Animate(ctx, next, animation);
}

// ----------------------------------------------------------------- dying settlers

// The tally word at `byteOffset` from player `owner`'s PlayerTally (the
// original indexes S3.EXE 0x7acfcc / 0x7acfdc by owner * 17 dwords; -8 is
// the previous record's last-but-one dword, for player 0 the 8 bytes before
// the array).
std::int32_t& TallyWord(std::uint32_t owner, std::int32_t byteOffset) {
    auto* const base = reinterpret_cast<std::uint8_t*>(&g_playerTallies[0]);
    return *reinterpret_cast<std::int32_t*>(base + owner * sizeof(PlayerTally) + byteOffset);
}

// 0x4eef09 (job 0x64): counted (tally 0x7acfcc), job cleared, run again.
JobResult LeaveHouse(SettlerContext& ctx) {
    AddWrapping(TallyWord(Owner(ctx), -8), 1);
    Call<kClearSettlerJob>(ctx.world, ctx.number);
    return JobResult::Again;
}

// Frees the settler record and its tile.
void RemoveSettler(SettlerContext& ctx) {
    ctx.settler.owner = kDeadOwner;
    Tile& tile = OwnTile(ctx);
    if (tile.settler == ctx.number) {
        tile.settler = 0;
    }
}

// 0x4eef33 (job 0x36): a settler dies: counted per player (soldier-like
// classes 1..9 in unk12c[0], others in unk12c[1]); a thief drops the goods
// it carries (byte +0x15).
JobResult SettlerDies(SettlerContext& ctx) {
    const std::uint32_t owner = Owner(ctx);
    if (ctx.typeClass >= 1 && ctx.typeClass <= 9) {
        AddWrapping(PlayerAt(ctx.world, owner).unk12c[0], 1);
    } else {
        AddWrapping(PlayerAt(ctx.world, owner).unk12c[1], 1);
    }
    if (TypeOf(ctx.settler) == SettlerType::Thief && ctx.settler.slot != 0) {
        Call<kDropGoodNear>(ctx.world, ctx.settler.slot, 1, ctx.settler.x, ctx.settler.y);
        Call<kSubtractPlayerCounter>(ctx.world, ctx.settler.owner, ctx.settler.slot, 1);
    }
    RemoveSettler(ctx);
    return JobResult::Next;
}

// 0x4ef005 (job 0xaf): a soldier of a building dies; the best soldier of its
// garrison list (Building::worker, linked by next; class 3 before 1 before
// 2, the first of a class wins; settler 0 if the list is empty) takes its
// place and guards the building (job 0xae).
JobResult GarrisonSoldierDies(SettlerContext& ctx) {
    RemoveSettler(ctx);
    std::uint32_t best = 0;
    std::uint32_t bestRank = 0;
    for (std::uint32_t s = BuildingAt(ctx.world, ctx.settler.building).worker; s != 0; s = SettlerAt(ctx.world, s).next) {
        const std::uint32_t type = SettlerAt(ctx.world, s).art;
        const std::int32_t typeClass = (&g_settlerTypeClass[0])[type];
        std::uint32_t rank = 0;
        if (typeClass == 3) {
            rank = 3;
        } else if (typeClass == 1) {
            rank = 2;
        } else if (typeClass == 2) {
            rank = 1;
        } else {
            GameDebugLog(texts::kTypeNumber, type, s);
            continue;
        }
        if (bestRank < rank) {
            best = s;
            bestRank = rank;
        }
    }
    Settler& guard = SettlerAt(ctx.world, best);
    if ((&g_settlerTypeClass[0])[guard.art] == 2) {
        Call<kLeaveBuildingSlot>(ctx.world, best);
    }
    guard.x = ctx.settler.x;
    guard.y = ctx.settler.y;
    TileAt(ctx.world, guard.x, guard.y).settler = static_cast<std::uint16_t>(best);
    SetJob(guard, Job::GuardBuilding);
    guard.counter = 0;
    return JobResult::Next;
}

// ----------------------------------------------------------------- toolsmith

// The seven tools, in the order the toolsmith looks at them, and the tile
// (from the door) of the pile each goes to.
struct ToolSlot {
    Good good;
    std::int32_t dx;
    std::int32_t dy;
};
constexpr ToolSlot kTools[7] = {{G::Hammer, 0, 0},  {G::Shovel, -1, 0},     {G::Axe, 1, 0},     {G::Pickaxe, -1, -1},
                                {G::Saw, 1, 1},     {G::FishingRod, 0, -1}, {G::Scythe, 0, 1}};

std::uint32_t DoorOffset(SettlerContext& ctx) {
    const Building& building = WorkBuilding(ctx);
    return TileOffset(building.door_x, building.door_y);
}
Tile& DoorTileOffsetBy(SettlerContext& ctx, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(ctx.world, DoorOffset(ctx) + static_cast<std::uint32_t>(dx) * kTileRowBytes +
                                       static_cast<std::uint32_t>(dy) * kTileBytes);
}

// The units on the seven tool piles (the door tile and its six neighbours).
std::uint32_t ToolPileUnits(SettlerContext& ctx) {
    std::uint32_t units = 0;
    for (const ToolSlot& tool : kTools) {
        units += PileAt(ctx.world, DoorTileOffsetBy(ctx, tool.dx, tool.dy).object_number).amount;
    }
    return units;
}

// Area of the door tile: settings1[good] = tools ordered, settings2[good] =
// the share wanted, settings3[good] = tools made (per share round).
Area& DoorArea(SettlerContext& ctx) { return AreaAt(ctx.world, DoorTileOffsetBy(ctx, 0, 0).area); }

bool AnyToolWanted(const Area& area) {
    for (const ToolSlot& tool : kTools) {
        if (area.settings1[Value(tool.good)] != 0) {
            return true;
        }
    }
    for (const ToolSlot& tool : kTools) {
        if (area.settings2[Value(tool.good)] != 0) {
            return true;
        }
    }
    return false;
}

// 0x4ef170 (job 0x74): the toolsmith starts a tool if the building wants
// work, coal and iron are there, the tool piles hold at most 7 units and the
// area wants any tool.
JobResult ToolsmithStart(SettlerContext& ctx) {
    if ((WorkBuilding(ctx).request_flags & kRequestWork) == 0) {
        return Wait(ctx);
    }
    if (HasGoods(ctx, 1) == 0 || HasGoods(ctx, 2) == 0) {
        return Wait(ctx);
    }
    if (ToolPileUnits(ctx) > kOutputPileFull || !AnyToolWanted(DoorArea(ctx))) {
        return Wait(ctx);
    }
    ReserveInputs(ctx, true);
    OwnTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    return Animate(ctx, Job::Job125, 125);
}

// 0x4ef41b (jobs 0x7d, 0x82): take the coal.
JobResult ToolsmithTakeCoal(SettlerContext& ctx) {
    ConsumeInput(ctx, 0, Value(G::Coal));
    return Animate(ctx, Job::Job126, 126);
}

// 0x4ef486 (job 0x7f): take the iron and show the forge (MapEffects 0x5449f0).
JobResult ToolsmithTakeIron(SettlerContext& ctx) {
    const std::uint32_t building = ctx.settler.building;
    const std::uint32_t x = BuildingAt(ctx.world, building).x;
    const std::uint32_t y = BuildingAt(ctx.world, building).y;
    ConsumeInput(ctx, 1, Value(G::Iron));
    Animate(ctx, Job::Job128, 128);
    const std::uint32_t screenY = Call<kTileScreenY>(ctx.world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(ctx.world, x, y);
    const std::uint32_t tick = g_gameTick;
    Call<kSpawn5449f0>(&ctx.world->map_effects, tick, BuildingAt(ctx.world, building).race, 0, building, screenX, screenY,
                       x, y);
    return JobResult::Next;
}

// Which tool the share rule picks: per tool made / share (x256, rounded),
// unwanted tools (share 0) last; the first tool whose value is not above
// any later one's (0x4efcb1..0x4eff98, a chain of unsigned compares).
std::uint32_t ToolByShare(const Area& area) {
    std::uint32_t value[7];
    for (std::uint32_t i = 0; i < 7; ++i) {
        const std::uint32_t good = Value(kTools[i].good);
        const std::uint8_t share = area.settings2[good];
        value[i] = share == 0 ? 0xffffffffu
                              : static_cast<std::uint32_t>(static_cast<std::int32_t>(area.settings3[good] * 256 + 128) /
                                                           static_cast<std::int32_t>(share));
    }
    for (std::uint32_t i = 0; i < 6; ++i) {
        bool lowest = true;
        for (std::uint32_t j = i + 1; j < 7; ++j) {
            lowest = lowest && value[i] <= value[j];
        }
        if (lowest) {
            return i;
        }
    }
    return 6;
}

// 0x4ef53d (job 0x80): the tool is done. An ordered tool (settings1) first,
// else by the shares; it goes to its own pile next to the door. Then the
// next tool at once (job 0x82) if possible, else job 0x81.
JobResult ToolsmithToolDone(SettlerContext& ctx) {
    const std::uint32_t areaNumber = DoorTileOffsetBy(ctx, 0, 0).area;
    Area& area = AreaAt(ctx.world, areaNumber);
    std::uint32_t pick = 7;
    for (std::uint32_t i = 0; i < 7; ++i) {
        std::uint8_t& ordered = area.settings1[Value(kTools[i].good)];
        if (ordered != 0) {
            --ordered;
            pick = i;
            break;
        }
    }
    if (pick == 7) {
        pick = ToolByShare(area);
        std::uint8_t& made = area.settings3[Value(kTools[pick].good)];
        if (made == 0xff) {
            Call<kClearAreaGoodsSettings1D2>(ctx.world, areaNumber);
        }
        ++made;
    }
    const ToolSlot& tool = kTools[pick];
    Call<kPileIncAmountComing>(ctx.world, DoorTileOffsetBy(ctx, tool.dx, tool.dy).object_number);
    Call<kAddGoodsToPile>(ctx.world, DoorTileOffsetBy(ctx, tool.dx, tool.dy).object_number);
    Call<kAddStatistic>(ctx.world, Owner(ctx), Value(tool.good), 1);
    Call<kAddPlayerCounter>(ctx.world, Owner(ctx), Value(tool.good), 1);
    // 0x4efa5c: another tool at once?
    if (HasGoods(ctx, 1) == 0 || HasGoods(ctx, 2) == 0) {
        return Animate(ctx, Job::Job129, 129);
    }
    Building& building = WorkBuilding(ctx);
    if ((building.request_flags & kRequestWork) == 0 || ToolPileUnits(ctx) > kOutputPileFull ||
        !AnyToolWanted(DoorArea(ctx))) {
        return Animate(ctx, Job::Job129, 129);
    }
    Call<kPileIncAmountLeaving>(ctx.world, building.piles[0]);
    Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[1]);
    return Animate(ctx, Job::Job130, 130);
}

// ----------------------------------------------------------------- war machine workshops

// Per workshop (catapult, ballista, cannon; k = 0, 1, 2): the third input
// and the jobs.
struct Workshop {
    Good third;
    Job work;    // 0x129 / 0x12b / 0x12d: waits for inputs
    Job check;   // 0x137 / 0x138 / 0x139: after putting a unit in
    std::uint32_t notify;  // NotifyLocalPlayer kind when a unit is done
};
constexpr Workshop kWorkshops[3] = {{G::Stone, Job::Job297, Job::CatapultProduction, 13},
                                    {G::Log, Job::Job299, Job::Job312, 14},
                                    {G::Gunpowder, Job::Job301, Job::Job313, 15}};
constexpr std::uint8_t kUnitsLeftMax = 0xfa;
constexpr std::uint8_t kStateUnitDone = 0x10;   // + k
constexpr std::uint8_t kStateOrderDone = 0x20;  // + k
constexpr std::uint32_t kNotifyOrderDone = 23;
constexpr std::uint8_t kOrderTilesStep = 6;     // Building::unk36 += 6

// 0x4f0077 / 0x4f035c / 0x4f05c0 (jobs 0x129, 0x12b, 0x12d): put one unit of
// the first input that has goods and is still wanted into the machine.
JobResult WorkshopPutInput(SettlerContext& ctx, const Workshop& shop) {
    const std::uint32_t number = ctx.settler.building;
    Building& building = BuildingAt(ctx.world, number);
    const std::uint8_t left = building.kind_bytes[7];
    if (left == 0 || left > kUnitsLeftMax) {
        return Wait(ctx);
    }
    const Good goods[3] = {G::Board, G::Iron, shop.third};
    std::uint32_t slot = 3;
    for (std::uint32_t k = 0; k < 3; ++k) {
        if (Call<kBuildingPileHasGoods>(ctx.world, number, k + 1) & 0xff) {
            if (building.water.wanted[k] != 0) {
                slot = k;
                break;
            }
        }
    }
    if (slot == 3) {
        return Wait(ctx);
    }
    Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[slot]);
    Call<kTakeGoodFromPile>(ctx.world, building.piles[slot]);
    Call<kSubtractPlayerCounter>(ctx.world, Owner(ctx), Value(goods[slot]), 1);
    --building.water.wanted[slot];
    building.kind_bytes[7] = static_cast<std::uint8_t>(building.kind_bytes[7] - 1);
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, 275);
    SetJob(ctx.settler, shop.check);  // after the animation, as the original
    return JobResult::Next;
}

// 0x4f0229 / 0x4f04cc / 0x4f0730 (jobs 0x137..0x139): once all units are in
// and the state says what was built: a war machine (a seven-tile unit at the
// centre of the extra tiles) or an order (Building::unk36 += 6); the owner
// is told. Then back to putting in (counter 0).
JobResult WorkshopCheck(SettlerContext& ctx, std::uint32_t k) {
    const Workshop& shop = kWorkshops[k];
    const std::uint32_t number = ctx.settler.building;
    Building& building = BuildingAt(ctx.world, number);
    if (building.kind_bytes[7] == 0 && building.water.state != 0) {
        const std::uint8_t state = building.water.state;
        if (state == kStateUnitDone + k) {
            Call<kReleaseBuildingExtraTiles>(ctx.world, number);
            const std::uint32_t y = building.y + static_cast<std::uint32_t>(g_extraTilesCentre[k][1]);
            const std::uint32_t x = building.x + static_cast<std::uint32_t>(g_extraTilesCentre[k][0]);
            Call<kCreateSevenTileUnit>(ctx.world, Value(SettlerType::Catapult), Owner(ctx), x, y);
            if (k == 0) {
                GameDebugLog(texts::kCatapultProduction, Owner(ctx));
                building.water.state = 0;
                Call<kNotifyLocalPlayer>(ctx.world, Owner(ctx), shop.notify, ctx.settler.x, ctx.settler.y);
            } else {
                Call<kNotifyLocalPlayer>(ctx.world, Owner(ctx), shop.notify, ctx.settler.x, ctx.settler.y);
                building.water.state = 0;
            }
        } else if (state == kStateOrderDone + k) {
            Call<kReleaseBuildingExtraTiles>(ctx.world, number);
            building.unk36 = static_cast<std::uint8_t>(building.unk36 + kOrderTilesStep);
            Call<kNotifyLocalPlayer>(ctx.world, Owner(ctx), kNotifyOrderDone, ctx.settler.x, ctx.settler.y);
            building.water.state = 0;
        }
    }
    SetJob(ctx.settler, shop.work);
    ctx.settler.counter = 0;
    return JobResult::Next;
}

}  // namespace

JobResult RunMetalJobs(SettlerContext& ctx) {
    switch (static_cast<Job>(ctx.job)) {
    // Sawmill (type Sawmiller).
    case Job::Job49:
    case Job::Job52: return SawmillTakeLog(ctx);
    case Job::Job50: return SawmillBoardDone(ctx);
    case Job::Job51: return LeaveWorkTile(ctx, Job::Job26);  // 0x4ee21f
    // Gold smelter.
    case Job::Job65: return StartTwoInputRound(ctx, Job::Job58, 73);  // 0x4ee252
    case Job::Job58:
    case Job::Job64: return SmelterTakeOre(ctx, Job::Job59, 74);
    case Job::Job59: return SmelterTakeCoal(ctx, Job::Job60, 75, 1);
    case Job::Job60: return Animate(ctx, Job::Job61, 76);  // 0x4ee4cf
    case Job::Job61: return Animate(ctx, Job::Job62, 77);  // 0x4ee4e7
    case Job::Job62: return GoldBarDone(ctx);
    case Job::Job63: return LeaveWorkTile(ctx, Job::Job65);  // 0x4ee698
    // Iron smelter.
    case Job::Job75: return StartTwoInputRound(ctx, Job::Job68, 82);  // 0x4ee6cb
    case Job::Job68:
    case Job::Job74: return SmelterTakeOre(ctx, Job::Job69, 83);
    case Job::Job69: return SmelterTakeCoal(ctx, Job::Job70, 84, 0);
    case Job::Job70: return Animate(ctx, Job::Job71, 85);  // 0x4ee948
    case Job::Job71: return Animate(ctx, Job::Job72, 86);  // 0x4ee960
    case Job::Job72: return IronBarDone(ctx);
    case Job::Job73: return LeaveWorkTile(ctx, Job::Job75);  // 0x4eeaca
    // Miners: entering, eating, digging (miner_jobs.cpp), then the products.
    case Job::MinerEnterCoalMine:
    case Job::MinerEnterGoldMine:
    case Job::MinerEnterIronMine:
    case Job::MinerEnterGemMine:
    case Job::MinerEnterSulfurMine:  // 0x4eeafd
        Call<kMinerJobEnter>(ctx.world, ctx.number);
        return JobResult::Next;
    case Job::Job85:
    case Job::Job86:
    case Job::Job87:
    case Job::Job216:
    case Job::Job217:  // 0x4eeb0a
        Call<kMinerJobFood>(ctx.world, ctx.number);
        return JobResult::Next;
    case Job::Job88:
    case Job::Job92:
    case Job::Job96:
    case Job::Job218:
    case Job::Job222:  // 0x4eeb31
        Call<kMinerJobDig>(ctx.world, ctx.number);
        return JobResult::Next;
    case Job::Job89:
    case Job::Job91: return LeaveWorkTile(ctx, Job::Job85);  // 0x4eeb3e
    case Job::Job90: return MineProductDone(ctx, G::Coal, Job::Job91, 96);
    case Job::Job93:
    case Job::Job95: return LeaveWorkTile(ctx, Job::Job86);  // 0x4eebff
    case Job::Job94: return MineProductDone(ctx, G::GoldOre, Job::Job95, 99);
    case Job::Job97:
    case Job::Job99: return LeaveWorkTile(ctx, Job::Job87);  // 0x4eecc0
    case Job::Job98: return MineProductDone(ctx, G::IronOre, Job::Job99, 102);
    case Job::Job219:
    case Job::Job221: return LeaveWorkTile(ctx, Job::Job216);  // 0x4eed81
    case Job::Job220: return MineProductDone(ctx, G::Gems, Job::Job221, 218);
    case Job::Job223:
    case Job::Job225: return LeaveWorkTile(ctx, Job::Job217);  // 0x4eee45
    case Job::Job224: return MineProductDone(ctx, G::Sulfur, Job::Job225, 221);
    // 0x4eeb17, 0x4eeb24: jobs 0x186..0x18f call an empty function.
    case Job::Job390:
    case Job::Job391:
    case Job::Job392:
    case Job::Job393:
    case Job::Job394:
    case Job::Job395:
    case Job::Job396:
    case Job::Job397:
    case Job::Job398:
    case Job::Job399: Call<kNoOpRet4>(ctx.world, ctx.number); return JobResult::Next;
    case Job::LeaveHouse: return LeaveHouse(ctx);
    case Job::Job54: return SettlerDies(ctx);
    case Job::Job175: return GarrisonSoldierDies(ctx);
    // Toolsmith.
    case Job::Job107: return StartFirstRound(ctx, Job::Job116);  // 0x4ef162
    case Job::Job116: return ToolsmithStart(ctx);
    case Job::Job125:
    case Job::Job130: return ToolsmithTakeCoal(ctx);
    case Job::Job126: return Animate(ctx, Job::Job127, 127);  // 0x4ef46e
    case Job::Job127: return ToolsmithTakeIron(ctx);
    case Job::Job128: return ToolsmithToolDone(ctx);
    case Job::Job129: return LeaveWorkTile(ctx, Job::Job116);  // 0x4f0036
    // War machine workshops.
    case Job::Job296: return StartFirstRound(ctx, Job::Job297);  // 0x4f0069
    case Job::Job297: return WorkshopPutInput(ctx, kWorkshops[0]);
    case Job::CatapultProduction: return WorkshopCheck(ctx, 0);
    case Job::Job298: return StartFirstRound(ctx, Job::Job299);  // 0x4f034e
    case Job::Job299: return WorkshopPutInput(ctx, kWorkshops[1]);
    case Job::Job312: return WorkshopCheck(ctx, 1);
    case Job::Job300: return StartFirstRound(ctx, Job::Job301);  // 0x4f05b2
    case Job::Job301: return WorkshopPutInput(ctx, kWorkshops[2]);
    case Job::Job313: return WorkshopCheck(ctx, 2);
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

}  // namespace s3re::game::settler_jobs

// ---------------------------------------------------------------------------
// Tests: every metal job against the original UpdateSettlers (harness in
// settler_jobs_test.h), with the work buildings, piles, door-tile areas,
// workshop states and garrison lists set up so that every branch is reached.
// ---------------------------------------------------------------------------

namespace s3re::game {

namespace {

using settler_jobs::Building;
using settler_jobs::Job;
using settler_jobs::testing::CaseSetup;
using settler_jobs::testing::FamilyTest;

constexpr Job kMetalJobs[] = {
    Job::Job49, Job::Job50, Job::Job51, Job::Job52, Job::Job54, Job::Job58, Job::Job59, Job::Job60,
    Job::Job61, Job::Job62, Job::Job63, Job::Job64, Job::Job65, Job::Job68, Job::Job69, Job::Job70,
    Job::Job71, Job::Job72, Job::Job73, Job::Job74, Job::Job75, Job::MinerEnterCoalMine,
    Job::MinerEnterGoldMine, Job::MinerEnterIronMine, Job::Job85, Job::Job86, Job::Job87, Job::Job88,
    Job::Job89, Job::Job90, Job::Job91, Job::Job92, Job::Job93, Job::Job94, Job::Job95, Job::Job96,
    Job::Job97, Job::Job98, Job::Job99, Job::LeaveHouse, Job::Job107, Job::Job116, Job::Job125, Job::Job126,
    Job::Job127, Job::Job128, Job::Job129, Job::Job130, Job::Job175, Job::MinerEnterGemMine,
    Job::MinerEnterSulfurMine, Job::Job216, Job::Job217, Job::Job218, Job::Job219, Job::Job220, Job::Job221,
    Job::Job222, Job::Job223, Job::Job224, Job::Job225, Job::Job296, Job::Job297, Job::Job298, Job::Job299,
    Job::Job300, Job::Job301, Job::CatapultProduction, Job::Job312, Job::Job313, Job::Job390, Job::Job391,
    Job::Job392, Job::Job393, Job::Job394, Job::Job395, Job::Job396, Job::Job397, Job::Job398, Job::Job399};
static_assert(sizeof(kMetalJobs) / sizeof(kMetalJobs[0]) == 80);

constexpr SettlerType kMetalTypes[] = {SettlerType::Sawmiller, SettlerType::Smelter,  SettlerType::Miner,
                                       SettlerType::Smith,     SettlerType::Thief,    SettlerType::Swordsman1,
                                       SettlerType::Bowman1,   SettlerType::Pikeman1, SettlerType::Carrier};
constexpr SettlerType kGarrisonTypes[] = {SettlerType::Swordsman1, SettlerType::Bowman2, SettlerType::Pikeman3,
                                          SettlerType::Swordsman3, SettlerType::Bowman1, SettlerType::Pikeman1};
constexpr std::uint8_t kToolGoods[] = {13, 12, 14, 15, 16, 17, 32};

void CustomizeMetal(CaseSetup& setup, std::uint32_t n) {
    auto& rng = setup.rng();
    std::uint32_t b = 0;
    setup.Both([&](CGameData* w) { b = w->settlers[n].building; });
    const auto flags = static_cast<std::uint8_t>(rng.Chance(7, 10) ? rng.Next32() | 1 : rng.Next32() & 0xfe);
    const auto ringAmount = static_cast<std::uint8_t>(rng.Below(10));
    // War machine workshops: units left (+0x20), state (+0x1f), wanted inputs.
    static constexpr std::uint8_t kLeft[] = {0, 0, 1, 2, 5, 0xfa, 0xfb, 0xff};
    static constexpr std::uint8_t kStates[] = {0, 0x10, 0x11, 0x12, 0x20, 0x21, 0x22, 0x13, 0x30};
    const std::uint8_t left = kLeft[rng.Below(8)];
    const std::uint8_t state = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Next32()) : kStates[rng.Below(9)];
    std::uint8_t wanted[3];
    for (std::uint8_t& w : wanted) {
        w = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(3));
    }
    // Garrison list: increasing settler numbers (no cycles), last next 0.
    std::uint16_t chain[5] = {};
    const std::uint32_t length = rng.Below(5);
    std::uint32_t member = 100 + rng.Below(10);
    for (std::uint32_t k = 0; k < length; ++k) {
        chain[k] = static_cast<std::uint16_t>(member);
        member += 1 + rng.Below(15);
    }
    std::uint8_t chainTypes[5];
    for (std::uint8_t& t : chainTypes) {
        t = rng.Chance(4, 5) ? Value(kGarrisonTypes[rng.Below(6)]) : static_cast<std::uint8_t>(rng.Below(48));
    }
    std::uint16_t doorX = 0;
    std::uint16_t doorY = 0;
    setup.Both([&](CGameData* w) {
        Building& r = w->buildings[b];
        r.request_flags = flags;
        r.kind_bytes[7] = left;
        r.kind_bytes[6] = state;
        for (int k = 0; k < 3; ++k) {
            r.kind_bytes[1 + k] = wanted[k];
        }
        r.worker = length != 0 ? chain[0] : 0;
        for (std::uint32_t k = 0; k < length; ++k) {
            w->settlers[chain[k]].next = k + 1 < length ? chain[k + 1] : 0;
            w->settlers[chain[k]].art = chainTypes[k];
        }
        w->piles[r.pile_ring].amount = ringAmount;
        doorX = r.door_x;
        doorY = r.door_y;
    });
    // The tool piles around the door: mostly few units.
    static constexpr std::int32_t kOffsets[7][2] = {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}, {1, 1}, {0, -1}, {0, 1}};
    const bool fewUnits = rng.Chance(2, 3);
    for (const auto& offset : kOffsets) {
        const auto pile = static_cast<std::uint16_t>(1 + rng.Below(settler_jobs::testing::kPiles));
        const auto amount = static_cast<std::uint8_t>(fewUnits ? rng.Below(2) : rng.Below(9));
        setup.Both([&](CGameData* w) {
            w->tiles[doorX + offset[0]][doorY + offset[1]].object_number = pile;
            w->piles[pile].amount = amount;
        });
    }
    // The door tile's area: tool orders, shares and counts.
    std::uint16_t area = 0;
    setup.Both([&](CGameData* w) { area = w->tiles[doorX][doorY].area; });
    const bool orders = rng.Chance(1, 3);
    const bool shares = rng.Chance(3, 4);
    for (const std::uint8_t good : kToolGoods) {
        const auto ordered = static_cast<std::uint8_t>(orders && rng.Chance(1, 3) ? 1 + rng.Below(3) : 0);
        const auto share = static_cast<std::uint8_t>(shares && rng.Chance(2, 3) ? 1 + rng.Below(4) : 0);
        const auto made = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(8));
        setup.Both([&](CGameData* w) {
            layout::Area& a = w->areas[area];
            a.settings1[good] = ordered;
            a.settings2[good] = share;
            a.settings3[good] = made;
        });
    }
}

bool MetalStubReturn(std::uintptr_t callee, std::uint32_t h, const std::uint32_t* args, CGameData* world,
                     std::uint32_t& eax) {
    switch (callee) {
    case settler_jobs::kBuildingPileHasGoods:
        eax = (h & 0xff) < 205 ? 1 : 0;
        return true;
    case settler_jobs::kClearSettlerJob: {
        // As the real one: job, counter and building cleared; the job here is
        // a no-op one (job 0 belongs to another family).
        layout::Settler& s = world->settlers[args[0] & 0xffff];
        s.job = Value(Job::Job57);
        s.counter = 0;
        s.building = 0;
        eax = 0;
        return true;
    }
    default: return false;
    }
}

FamilyTest MetalFamily() {
    FamilyTest family{kMetalJobs, sizeof(kMetalJobs) / sizeof(kMetalJobs[0]), kMetalTypes,
                      sizeof(kMetalTypes) / sizeof(kMetalTypes[0])};
    family.customize = &CustomizeMetal;
    family.stubReturn = &MetalStubReturn;
    family.minCompared = 3500;  // nearly every case is compared (all its jobs are here)
    return family;
}

// Wrong: a gold bar of race 1 counts 2 points (the original 1).
std::uint32_t __fastcall WrongUpdateSettlersGoldPoints(CGameData* world, void* edx) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const layout::Settler& s = world->settlers[n];
        if (s.owner < 20 && s.counter == 0 && JobOf(s) == Job::Job62 && world->players[s.owner].race == 1) {
            ++g_playerTallies[s.owner].points;
        }
    }
    return CGameData_UpdateSettlers(world, edx);
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Metal_MatchesOriginal) {
    settler_jobs::testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, MetalFamily(), 4000);
}

S3RE_TEST(CGameData_UpdateSettlers_Metal_ComparisonDetectsWrongGoldPoints) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FamilyTest family = MetalFamily();
    family.minCompared = 1;  // only a real difference may fail it
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongUpdateSettlersGoldPoints, family, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
