# UpdatePhase2 (0x4E05E0): what it is and how it is built

Companion of `re/update_settlers.md` (packages, risks, CSV files). Made
2026-11-10 from `build/s3.asm` and the jump tables in `S3.EXE`.

## Purpose

**The time-sliced economy update.** Each call does one of 18 phases,
counted by the world dword +0x110A57C (0..17, then back to 0):

| phase | what it does |
|---|---|
| 0 | `CountDownTimers` 0x4FEE00 (notification timers); `RecountPlayerPopulation` 0x5104C0 (per player: civilians, carriers, residence capacity 10/30/100 for building types 0x1A/0x1B/0x1C); clears three arrays of 0x24 words in every area record (area +0x52, +0x9A, +0xE2); copies presence map B (+0xDF2848) to A (+0xDE7448) and clears B, for every 16x16-tile block |
| 1..16 | settlers number = phase, phase+16, ... (one sixteenth per call): military and special types (5 8 F 16 17 1E..25 27 28 29 2B 2C, table 0x4E492C/0x4E4934 on type - 5) mark their player's byte in presence map B at (x/16, y/16); then buildings number = phase, phase+16, ...: construction sites and finished buildings (below); then `SendCarriersToPile` 0x5052C0 when a pile's +1 and +2 differ |
| 17 | for every area: `FetchGoodsToTradeBuilding(area, word, good)` for goods 1..0x23 whose word at area +0xE4 + 2*good is non-zero, then the area's goods balancing (BuildingInputWord24/2A, GoodsDistance 0x4FF320, SetSettlerTarget, PileIncAmountLeaving/Coming, area counters +0x278) |

So presence map A always holds the military presence of the last full
cycle (read by IsOtherPlayerNear), and every building and settler is
visited once per 16 calls.

**Name proposal: `GameWorld::UpdateEconomy`** (medium): goods requests,
worker requests, population counts and goods distribution, time-sliced.
The presence-map part is military, so `UpdateEconomyAndPresence` would be
more exact but clumsy. The current name "UpdatePhase2" only says where it
is called.

## Buildings (phases 1..16)

For a used building (+0 != 0xFF):

- **Construction site** (+0x21 buildup != 0; gated by the request flags
  +0x18 and the player word +0x28): while levelling (buildup 1), for
  each digger slot i below the count +0x1B whose bit is clear in +0x16,
  `RequestSettlerForBuilding(building, 1)` and set the bit on success;
  while building (buildup 2) the same with type 2, the bits in +0x17 and
  the count +0x1C; material requests with `AddBuildingGoodsRequest`
  0x4DDF30. So argument 2 of 0x4DACD0 is the settler type: type 1 =
  digger, type 2 = builder (this also settles the guesses for building
  +0x16/+0x17/+0x1B/+0x1C in re/structs.md: digger and builder slot
  bits and counts).
- **Finished building**: jump table 0x4E495C on the building type - 2
  (0x37 entries). Per type: `GoodsRequestPriority` 0x4FF380 +
  `AddBuildingGoodsRequest` for the inputs, `RequestSettlerForBuilding`
  for the worker, `TakeBuildingExtraTiles` 0x4DD6F0 for the water
  buildings 0x1F/0x2F/0x30/0x32, `CreateSettler` + `SetSettlerAnimation`
  for the residences 0x1A/0x1B/0x1C and types 0x29/0x2B/0x2D/0x37,
  `SendNearestSettlerToPile`/`SendNearestSettlerToBuilding` with
  SetAreaBuildingSlotA/B for 0x21/0x22 (storage/trade?), the military
  types 0xF/0x10/0x12 (475 instructions: SendNearestWorkerToBuilding,
  LeaveBuildingSlot, ClearSettlerJob, 0x50E200, NotifyLocalPlayer), and
  no work for types 2, 3, 5, 6, 0xE, 0x18, 0x19, 0x1E, 0x23, 0x24.

| building type(s) | handler | ins | callees |
|---|---|---|---|
| 0xF 0x10 0x12 | 0x4E0EC6 | 475 | SendNearestWorkerToBuilding, NotifyLocalPlayer, LeaveBuildingSlot, 0x50E200, ClearSettlerJob, IsOnMap |
| 0xC | 0x4E1649 | 246 | 0x4DACD0, AddBuildingGoodsRequest, GoodsRequestPriority, 0x5052C0 |
| 0x13 | 0x4E2599 | 211 | 0x4DACD0, AddBuildingGoodsRequest, GoodsRequestPriority, CountAreaSettlersOfClass0/OfType |
| 0xD | 0x4E19B1 | 143 | as 0xC |
| 0x21 / 0x22 | 0x4E38AE / 0x4E36D4 | 139 / 130 | SendNearestSettlerToBuilding / ToPile, SetAreaBuildingSlotA/B |
| 0x20 | 0x4E3AB8 | 123 | 0x4DACD0, TakeBuildingExtraTiles, AddBuildingGoodsRequest, GoodsRequestPriority |
| 0x1F 0x2F 0x30 0x32 | 0x4E2B3F.. | 117..120 | TakeBuildingExtraTiles, AddBuildingGoodsRequest, GoodsRequestPriority (0x32 has a DebugLog) |
| 0x35 / 0x34 / 0x33 | 0x4E3DD0 / 0x4E3CC6 / 0x4E3C89 | 113 / 77 / 16 | 0x4DACD0 (+ requests, 0x5052C0) |
| 0x2E / 0x36 | 0x4E334B / 0x4E34BA | 97 / 93 | requests + TakeGoodFromPile 0x505820, SubtractPlayerCounter |
| 7 8 9 0x25 0x1D 0x38 (mines) | 0x4E1BA9.. | 28..84 | AddBuildingGoodsRequest, GoodsRequestPriority |
| 0x1A 0x1B 0x1C (residences) | 0x4E211A.. | 44 each | CreateSettler, SetSettlerAnimation |
| 0x29 0x2B 0x2D 0x37 | 0x4E3619 | 49 | CreateSettler, SetSettlerAnimation |
| others with requests | | 27..55 | GoodsRequestPriority (+ AddBuildingGoodsRequest) |

## Callees still undone (all in package C, except the shared ones)

| address | proposal | ins | notes |
|---|---|---|---|
| 0x4DACD0 | `GameWorld::RequestSettlerForBuilding` (building, type) -> EAX | 589 | 25 call sites, EAX read at all; writes settler type/job/building and the area lists +0x12A; calls NotifyLocalPlayer |
| 0x5052C0 | `GameWorld::SendCarriersToPile` (pile) | 250 | also UpdateEnvironment; calls SetPileGood 0x5061B0 (package B) |
| 0x5104C0 | `GameWorld::RecountPlayerPopulation` | 124 | phase 0 only |
| 0x50E200 | `GameWorld::SetSoldierAnimationInBuilding2` | 65 | military buildings |
| 0x4FEC10 | `GameWorld::NotifyLocalPlayer` | 159 | package B |
| 0x505820 | `GameWorld::TakeGoodFromPile` | 52 | package B |

## Proposed C++ split (hooked as one unit, after its callees)

```
void GameWorld::UpdateEconomy() {          // 0x4E05E0
    uint32_t phase = economyPhase;          // +0x110A57C
    if (phase == 0) StartEconomyCycle();    // timers, population, area counters, presence swap
    if (1 <= phase && phase <= 16) {
        MarkMilitaryPresence(phase);        // settlers phase, phase+16, ...
        for (b = phase; b <= buildingHwm; b += 16) UpdateBuildingEconomy(b);
    }
    if (phase == 17) for (area ...) DistributeAreaGoods(area);
    economyPhase = phase + 1 == 18 ? 0 : phase + 1;
}
```

`UpdateBuildingEconomy` splits into `UpdateConstructionSite` and one
function per building-type handler (or per group with the same body).
Note the loop bounds: settlers up to the high-water mark +0x12A5E5C, but
only when phase <= 16 and phase <= settlerHwm; buildings while
number <= +0x1499E60; local `ebx = 100` is re-set per building
(0x4E078D).

Risks specific to this function: a 0x5C8-byte stack frame (check for
locals read before they are written per building type); the presence
maps and the notification timers are also written by UI-side code
(re/world_layout.md); everything else as in `re/update_settlers.md`.
