# Computer players (AI)

Analysis of the computer players' code (session 47, batch 1), from the
dumpbin disassembly (`build/s3.asm`), the RTTI (`re/classes.json`) and the
source-file ranges (`re/source_files.md`). Confidence: **V** verified (a
reimplemented and tested function shows it), **R** read from the code,
**G** guess.

## Where the code is

- Range `0x54cbd0..0x59fcd0` (after Volker\landscape.cpp, before the
  Credits code; correction from the session 50 naming pass: the AI code
  really ends at 0x59e0a0, the last AI function is 0x59dcc0, then a static
  initializer block 0x59dcf0..0x59e6b0; 0x59e6f0.. is Credits code):
  502 game functions, ~50k instructions (plus ~1,270
  compiler-generated static-initializer stubs for the many static
  `std::string`s each AI source file includes). After batch 1: 74 verified
  (1.6k instructions), 428 open (48k instructions).
- Source files (`Rainer\`, Rainer Foetzki): an unnamed file with
  CComputerPlayer (`0x550900..0x553d30`, object unit 57), `Cevaland.cpp`
  (land evaluation "CEvaLand", `0x553d30..0x55ec10`), CompDoBuildHouseReq,
  CompGExpand, compGlobs (the agent factory 0x56f6d0), CompHouseReq,
  CompMapWorkerAgent (`0x58b520..0x59e7d0`, the largest).
- Entry points from outside: the constructor `CComputerPlayer::CComputerPlayer`
  0x550900 (from `CLogicThread::OnStart` 0x53c9e0 and, for a dropped
  player, `CLogicThread::ExecuteCommands` 0x535990), the per-step entry
  **0x550f80** (from `CLogicThread::Step` 0x53db30, on the 20 pointers at
  CLogicThread+0x350; a null pointer = a human or empty slot), 0x551210 /
  0x551220 (wrappers of 0x596380 / 0x596330, called by OnStart and the
savegame code 0x509980: probably loading/saving the AI state, G), and
  0x551b80 (sets the AI globals at game start).

## Classes (RTTI)

Two hierarchies, both without data in the base class (R; layouts in
`src/s3re/game/ai/ai_layout.h`):

| Class | vtable | Role |
|---|---|---|
| `CComputerPlayer` : `Win32::CObject` | 0x65a530 (1 slot) | one per computer player, 0x780 bytes |
| `BLACK_BOARD_AGENT` | 0x65a828 (13 slots) | base of the agents ("knowledge sources") |
| `BLACK_BOARD_NOTE` | 0x65a56c (4 slots) | base of the notes (requests on the blackboard) |

**Agents**: one of each kind per computer player, in
`ComputerPlayer::agents[11]` (+0x730), made by the factory
`AI::CreateAgent` 0x56f6d0 (fastcall kind; allocates, stores the vtable,
calls the constructor; V for the constructors). The word at +4 is the
kind (`AI::SetWord4`). Slot 8 (+0x20) is each agent's "run" (R):

| Kind | Class | vtable | Constructor | Slot 8 (run) | Subtree |
|---|---|---|---|---|---|
| 0 | `LOCAL_WARE_REQUEST_AGENT` | 0x65a7f0 | 0x57ab50 (V) | 0x59f200 (NoOp) | |
| 1 | `HOUSE_GROUND_REQUEST_AGENT` | 0x65a7b8 | 0x5764f0 (V) | NoOp | |
| 2 | `HOUSE_REQUEST_AGENT` | 0x65a780 | 0x5787a0 (V) | NoOp | |
| 4 | `T_DO_BUILD_HOUSE_AGENT` | 0x65a710 | 0x5654b0 (V) | NoOp | |
| 3 | `GLOBAL_EXPAND_EMPIRE_AGENT` | 0x65a748 | 0x56cd00 (V) | 0x56cd20 | 29 fns, 1.4k ins |
| 5 | `COMPUTER_PLAYER_AGENT` | 0x65a6d8 | 0x584680 (V) | 0x5846a0 | 2 fns |
| 6 | `T_MAP_WORKER_AGENT` | 0x65a6a0 | 0x57cda0 (V) | 0x57cde0 | 56 fns, 7.3k ins |
| 7 | `T_ATTACK_AGENT` | 0x65a668 | 0x55ebe0 (V) | 0x55ec10 | 77 fns, 10.2k ins |
| 8 | `T_ECONOMY_AGENT` | 0x65a630 | 0x567850 (V) | 0x567890 | 55 fns, 4.2k ins |
| 9 | `T_SAFETY_AGENT` | 0x65a5f8 | 0x586830 (V) | 0x586850 | 16 fns, 0.7k ins |
| 10 | `CCentrumCheckAgent` | 0x65a5c0 | 0x560dd0 (V) | 0x560e00 | 48 fns, 4.3k ins |

The request agents (kinds 0, 1, 2, 4) instead have their own slots 2, 3,
4, 9, 10: slot 2 lists the note types the agent handles (0x576500 writes
one), slot 3 takes a new note (0x5654d0, 0x5787c0, 0x576510: they set
note flag 2 and note +0x18 to their kind), slots 9/10 are the note
callbacks 0x582480 / 0x582510 call. The slots the CComputerPlayer calls
(V, package A3; `agent_slot` in ai_layout.h):

| Slot | Called by | Arguments | Use |
|---|---|---|---|
| 0 | DeleteNote 0x551330, DeleteAgent 0x551490 | (1) | scalar deleting destructor (notes too) |
| 2 | AddAgent 0x551400 | (u16* types) | the note types it handles; AX = count |
| 3 | DispatchNote 0x551510 | (note) | take a new note of its type |
| 4 | WorkOnNote 0x5516e0 | (note) | work on an active note (skipped while slot 5 is non-zero) |
| 5 | WorkOnNote | () | busy |
| 7, 8 | RunAgents 0x551590 | () | slot 8 (its own work) when slot 7 says so |
| 9 | ReportToRequester 0x582480 | (parent note, note, result, value) | a note the agent made reports back (result 0 / 100 at the callers) |
| 10 | ExecuteAtHandler 0x582510 (was CancelAtHandler) | (note) | carry out a note the agent took (T_DO_BUILD_HOUSE_AGENT's WorkOnNote calls it once the house request succeeded; the chain ends in the PlaceBuilding sender 0x576430) |

Kinds 1, 2, 4 from the factory's jump table 0x56fa68 (session 48; enum
`ai::AgentKind`). Each request agent handles the notes of the type with
its number (`ai::NoteType`: 0 local ware request, 1 house ground
request, 2 house request, 4 "do build house"): GLOBAL_EXPAND_EMPIRE
(0x56cd90) and T_MAP_WORKER (0x57ce90) make T_DO_BUILD_HOUSE_NOTEs
(building type, x, y, value; e.g. a small tower); T_DO_BUILD_HOUSE_AGENT's
slot 3 turns one into a HOUSE_REQUEST_NOTE below it; HOUSE_REQUEST_AGENT's
slot 3 makes a HOUSE_GROUND_REQUEST_NOTE (0x4c bytes) and a
LOCAL_WARE_REQUEST_NOTE below that (G from the vtables they store).

**Notes**: `BLACK_BOARD_NOTE` and `HOUSE_REQUEST_NOTE` (0x65a558),
`HOUSE_GROUND_REQUEST_NOTE` (0x65a598), `LOCAL_WARE_REQUEST_NOTE`
(0x65a5ac), `T_DO_BUILD_HOUSE_NOTE` (0x65a584). Up to 300 per computer
player, by id in `ComputerPlayer::notes` (+0x280). Common layout (V,
`BlackBoardNote`): +4 flags (1 dispatched, 4 active), +6 type (indexes
`ComputerPlayer::handlers` +0x266, the agent kind that handles it), +8 own
id, +0x10 parent, +0x12 next sibling, +0x14 first child (a tree of
requests, e.g. a house request with a ground request and a build order
below it), +0x16 / +0x18 agent kinds for the callbacks.

`CComputerPlayer` keeps four id lists (V): root notes (+0x004, 100),
pending notes (+0x0ce, 50; new notes, handed to their handler by
0x5514c0 on the next step), active notes (+0x134, 100; worked on round
robin with the cursor +0x1fe) and the notes to delete ("doomed", +0x200,
50; `CComputerPlayer::DoomNote` 0x551910, e.g. through `DoomNoteOf`
0x551760 from the agents' slot 3;
deleted by 0x551790 after each active note's turn).

**The note life cycle** (V, package A3, `ai_computer_player.cpp`):

1. An agent makes a note (a derived note's constructor) and calls
   `AddNoteUnder` 0x551230 (note, parent note or none, itself) ->
   `AddNote` 0x551270: the first free id of 300 (all taken: nothing
   happens), below the parent as its first child or a root note, the
   requester (+0x16) is the agent's kind, flags 0, and the id goes on the
   pending list.
2. Next step, `UpdateBlackboard` 0x5517d0 (from 0x550f80): `RunAgents`
   0x551590 (each agent's slot 8 if its slot 7 says so), then
   `DispatchPendingNotes` 0x5514c0 -> `DispatchNote` 0x551510: the
   handler of the note's type (`handlers[type]`, filled by `AddAgent`
   0x551400 from each agent's slot 2, first come first served) gets it
   through slot 3 (flag 1 dispatched); a type nobody handles is deleted at
   once. Then `WorkOnNextActiveNote` 0x551670 -> `WorkOnNote` 0x5516e0:
   one active note per step gets a turn at its handler (slot 4, unless
   slot 5 says busy), then `DeleteDoomedNotes` 0x551790.
3. Notes report to their requester (`ReportToRequester` 0x582480, slot 9)
   and are carried out by their handler (`ExecuteAtHandler` 0x582510, slot
   10; was CancelAtHandler, renamed in session 50) by the agents' code.
4. `DeleteNote` 0x551330 deletes a note with its whole subtree (unlink,
   children first, off the pending and active lists, slot 0).
   `Shutdown` 0x550f30 (from the destructor 0x5509d0) deletes every root
   note's tree and every agent (`DeleteAgent` 0x551490) and releases the
   shared CEvaLand.

`StartBestBuilding` 0x551a70 (0x550f80 step 7): while the current area
(node key) has fewer than 4 of the player's construction sites (0x59dbb0),
the building type with the highest priority (`best[type].score`, first
of equals, the search stops at 65000) is handed on with 0x58ecb0 (moves
`best[type]` to `best[57 + type]`); when AiPlayerData +0x14 is set only a
stonecutter or small tower, when +0x10 is set only a woodcutter, sawmill,
forester or small tower (G: short of stone / wood).

## The per-step entry 0x550f80

`CLogicThread::Step` calls it once per computer player and logic step
(R). With `p` = the player index (+0x76c), it staggers its periodic work
by `p` so that the computer players don't all do the heavy work in the
same step:

1. `++steps` (+0x764). Every 15 steps (`timer274`): lower the 57 per-type
   countdowns (`AiPlayerData::candidate_values`, +0x2d2) of every node of
   the player's node list by one (stops at 0).
2. `0x5547a0` (land evaluation update, Cevaland) and `0x554980` (696
   instructions, subtree 12k: a large Cevaland step); if 0x554980 returns
   non-zero, stop here.
3. If the player has no nodes (the count of `CEvaLand::lists[p]`, through
   the CEvaLand pointer `land_evaluation` +0x774, is 0): only
   `agents[7]` slot 8 runs (probably T_ATTACK_AGENT: a player without land
   can only fight).
4. Pick the current node (`data` +0x778 / `area` +0x77c): the next
   node of the list, round robin (or the first one after the "area list
   changed" flag at CEvaLand +0xf0 + 4 * p, set by RemoveArea 0x558e80,
   which it clears; corrected in the session 50 naming pass, the
   `lea edx,[eax+eax*2]` there is only for the list). The nodes
   (`AiPlayerData`) are one per economy area of the player (R: word +8 is
   the area id; StartBestBuilding passes it to 0x59dbb0, which compares
   it with the tile area (`Tile::area`, +0x14) of the player's buildings'
   door tiles through AI::BuildingArea 0x599610).
5. `AI::SetCurrentPlayer` (globals g_aiPlayer, g_aiPlayerNumber,
   g_aiPlayerSetup, g_aiPlayerData).
6. Every 71 steps (`timer278`): `0x59bc20`, `0x59c0f0` (CompMapWorkerAgent
   area) and return.
7. Otherwise: `UpdateBlackboard` 0x5517d0 (RunAgents, DispatchPendingNotes,
   WorkOnNextActiveNote; see the note life cycle above) and
   `StartBestBuilding` 0x551a70.
8. Every 841 steps (`timer27c`): `0x59bf70`, `0x5724e0`, `0x5720c0`.
9. `CComputerPlayer::RaiseBuildPriorities` (V) and `++data->+0x20`.

The agents' run functions (slot 8) are called from elsewhere in this tree
(e.g. 0x550f80 step 3, the factory's callers); mapping who runs which
agent when is the next analysis step.

**Build priorities** (V): `AiPlayerData::best[type]` (+0x26, 6 bytes per
building type: priority, x, y) is filled by `AI::RecordBestCandidate`
0x58ec50 (type, priority, x, y): it keeps the best place per type and
resets the type's countdown to `g_aiCandidateDefaults[type]` (0x7329d0).
Agents call it with fixed priorities (e.g. 0x5699d0 / 0x569a00 /
0x569b00: types 35, 37, 39 with priority 20000 under conditions on agent
fields). RaiseBuildPriorities then lets each wanted type grow by
`g_aiBuildPrioritySteps` up to `g_aiBuildPriorityCaps`.

**Census** (V, `AiAgentData` / `AI::Census`, 0x1ad8 bytes, filled by
0x58f990): finished buildings per player and type
(`AI::Census::CountBuildings` 0x55be00) and a military strength per
player's team (`ComputeTeamStrength` 0x55bea0: 8 per soldier, kinds 2/5/8
double, 16 per small/large tower, 32 per castle; summed over the team).

## The economy agent (session 48, A2)

`T_ECONOMY_AGENT` (kind 8) run 0x567890: copies the current node's key
(area) and the player's race into the agent, takes a snapshot of the area
(0x567910: population = settlers of class 0 or a per-player global when
the byte 0x7e5ef8 is set; carriers; logs, boards, stones, coal, iron,
sulfur in stock; the node's buildings per type from
`AiPlayerData::building_counts`), then 0x56a1a0 runs one condition per
building type, skipping a type while its countdown runs or its
`type_blocked` byte is set; every player + 14 runs it also runs 0x56a000,
0x56a0f0, 0x56a840, 0x56aa60 (V for the conditions, R for the rest).

The conditions (0x568250..0x569d20, `ai_economy_conditions.cpp`, V) ask
for a building type with `AI::RecordBestCandidate` (priority, position
-1/-1). They encode the economy's build order: a sawmill per three
woodcutters, foresters by the woodcutter surplus, farms per three mines,
fisheries per two mines, mills / pig farms by the grain left over,
bakeries two per mill, slaughterhouses per two pig farms, waterworks per
two water users, a toolsmith once coal and iron are there, barracks per
three weaponsmiths, the race's drink building, small and large temple by
population, and gold mines and weaponsmiths by a "wanted for the
population" curve: population stages 0 / 40 / 100 / 500 / 2000 settlers
(`g_aiPopulationStages`), a wanted count per stage start
(`g_aiWantedGoldMines` 0, 2, 3, 5, 7; `g_aiWantedWeaponsmiths` 0..4) and
per-stage float slopes computed by static initializers; the priority
falls as have * 100 / wanted rises past 10 / 30 / 50 / 70 / 100.
Session 51 (V): the coal mine 0x568bc0, iron mine 0x568d80 and iron
smelter 0x568f40 conditions take the higher (16-bit) of two priorities:
mines per the population's wanted count (the gold mine tables serve all
mines; smelters use the weaponsmith tables) and the stock per its users'
demand ((2 * users - mines) * 20 - stock; coal users: iron and gold
smelters, tool- and weaponsmiths, distilleries, powder makers; iron users:
the smiths; one smelter per two smiths). They store the last ratio in
+0x2c and the result in +0x30 (`last_priority`, was forester_priority);
the iron mine stores the iron (goods 11) in stock, the smelter the iron
ore (goods 7), in +0x4a (`ore`). Still open in this family: 0x567e00
(woodcutter), 0x568420 (stonecutter; both with two float curves per race
and stage, 0x7e1300..0x7e137c) and 0x568890 (residences, Random).
The snapshot 0x567910 is done too (V, session 51, `ai_economy_snapshot.cpp`):
the "per-player global" population is the census's settler figure
(`g_aiCensus.dwords3f4[player]`, low word) once `settlers_counted` is set,
else `AI::AreaPopulation` 0x59dcc0 (CountAreaSettlersOfClass0); the goods
are logs, boards, stones, coal, iron (goods 11) and sulfur; a race above 3
leaves the race-specific counts as they were.

## Map queries (session 48, A2)

Small queries of CompMapWorkerAgent / CompHouseReq (`ai_map_queries.cpp`,
V): the building, area, walk region (searching up to 16 tiles in +x),
stones (0x7f - object), tree and resource nibbles of a tile against the
AI's map size; `AI::TerritoryRadius` (the distance of the 3500th / 4000th
/ 5000th nearest map offset for small tower / large tower / castle);
`AI::SiteKindOf` (2 storehouse, 3 military, 4 mine, 5 field, 1 other);
`AI::TakeBestCandidate` (moves best[type] to best[57 + type]); counts of
construction sites and of settlers of a type in an area; and
`AI::TeamHasActivePlayer`.

Session 51 (V): `AI::WrappedPos` 0x597070..0x597300 (`ai_wrapped_pos.cpp`):
a position on a width x height grid that wraps at the edges (Step in six
directions, torus Distance, WrapCoords; Advance moves without wrapping and
reports leaving the grid), the cursor of the AI's site searches.
`AI::CountNodeBuildingsByType` 0x572420 refills a node's
`building_counts` (the node: the player's CEvaLand list node whose word +8,
`AiPlayerData::area`, is the area). `AI::Army::Reset` 0x591e00
(`ai_army.cpp`) resets g_aiGroupOrders (also 20 x 10 bytes at +0x1c48,
meaning open) and empties the per-player hunter vectors
(`g_aiHunterVectors` 0x7e368c); the gather offsets of groups 1..9 are
(0,0), (9,18), (-9,-18), (-18,0), (18,0), (-9,18), (27,18), (-27,-18),
(18,-27).

Session 52 (V): `ai_building_queries.cpp`: `AI::CountSitesOfType` 0x59db30
(original quirk: `*area == door area || *area != 0`, so a non-zero area
counts every site of the type and area 0 only those in area 0),
`AI::FirstBuildingInArea` / `FirstMilitaryBuildingInArea` 0x58b410 /
0x58b380, `AI::MineSuccessPercent` 0x573810 (finds in the mine's 32-bit dig
history work_y:work_x * 100 / digs); `ai_land_queries.cpp`:
`AI::ResourceAmountOfKindAt` 0x571f60, `AI::ResourceAmountNear` 0x571fa0
(the tile and two hexagonal rings, WrappedPos), `AI::IsFarmLand` 0x59a720
(the Asian rice test reads the raw cell byte +0xd), `AI::NextSparseMapOffset`
0x58a250; `ai_army.cpp`: `AI::Army::GroupFightingShareAbove` 0x593040 (job
142 is UnitFighting; was GroupJob142ShareAbove) and `AI::Army::PruneHunters`
0x5934a0 (original quirk: after an erase the entry moved into the slot is
skipped).

Batch 3 (`ai_site_queries.cpp`, V): `AI::HasSiteNoteInArea` (a build
order of this type or a ground request of another type already planned
in the area; note subclass fields +0x1c type, +0x1e/+0x20 position),
`AI::HasBuildingNear` / `CountBuildingsNear` (TileDistance),
`AI::NextGarrisonedMilitaryBuilding` (round robin), `AI::CheckBasicEconomy`
(AiPlayerData +0xc: sawmill, woodcutter, small tower plus the race's
stonecutter / forester), and two CompMapWorkerAgent globals:
`g_aiArmyState` 0x7e58c8 (reset by 0x593f50) and `g_aiGroupOrders`
0x7e37d0 (set up by 0x591e00), with per player and unit group arrays
(index player * 10 + group, the world's 10 settler lists per player)
cleared by `AI::ResetUnitGroup` 0x5930f0. What the arrays mean is still
a guess (G: the computer players' unit groups and armies).

Session 54 (V, `ai_army_groups.cpp`): the unit-group bookkeeping.
Soldiers go into the first group 1..9 below the group size
(`AssignSoldier` 0x592820; when all nine are full the size grows by 5, at
most 100); mages into the group with the fewest mages (`AssignMage`
0x5926e0). A group's first soldier sets its gather position: the group's
gather offset plus the nearest (TileDistance) of the player's buildings,
one per owned economy area (its first military building, else its first
building), whose area becomes the group's home area. `CountSpareGroups`
0x591fb0 / `CountSpareSoldiers` 0x592100 pair groups by home area: per
pass the first free group is picked and every second later group of the
same area becomes spare. `DisbandWeakGroups` 0x592bf0 dissolves groups
of at most a quarter of the group size (without a target building) and
any group without a swordsman or pikeman; `MoveLastGroupInto` 0x592240
then moves the highest group into the hole (target building, defending
flag, target, steal flag, list; NOT the home area or gather position,
which the slot keeps) and shrinks the group size by 5 (not below 7).
`ReturnGroupHome` 0x592620 logs "Center - player ..." through
OutputDebugStringA and sends the group to its gather position. Dead code:
AssignSoldier's block 0x592aeb (NearestFreeInteriorMilitaryBuilding
0x58aa70) is guarded by `&highest_group == 0`, so it never runs.

Session 54 (V, `ai_army_orders.cpp`, `ai_economy_actions.cpp`): orders.
`NearestFreeInteriorMilitaryBuilding` 0x58aa70 skips buildings named by a
group's AiGroupTarget x or y word (so those two words are probably
building ids, not coordinates; to confirm) and buildings at the border
(original quirk: two of its four border probes use the searched point's x).
`WeakestFrontierMilitaryBuilding` 0x58a7e0 takes the territory radius from
the map offset table (distance of entry 3500 / 4000 / 5000 for small tower /
large tower / castle) and picks the frontier building with the fewest
soldiers in its garrison chain. Mission 56's scripted assault 0x594d40
sends player 1's groups at player 0's weakest frontier building (the
inventory said player 1's). `CheckGroupTargets` 0x594a10 ends defences
without enemies (within 135), attacks on buildings that became the
player's own, steals against more than a tenth of the group and attacks
where all attacking groups' members times a float ratio are below the
enemies within 60 (x87 compare; NaN resets). Thieves (0x594e70, cursor
AiGroupOrders +0x8e8 `thief_cursors`) and geologists (0x595050, every 7th
call, counter AiArmyState +0x4b4 `geologist_timers`; nearest mountain by
the sparse offset search from index 5000 or 500 in g_aiEnemySearch, else a
random point within 127) go to points without enemy soldiers within 18;
of the up to five retries, a point found by the fifth is dropped (quirk).
The economy agent tears down a residence when more than 10 beds are free
(0x56a000: large above 100, medium above 30, else small) and promotes
soldiers (0x56a0f0, kinds 0x81..0x83) after 14 x AiPlayerSetup +0xc4 steps,
at most 3 times (ComputerPlayer +0x768).

Session 55 (V, `ai_army_orders.cpp`): the group "targets" at AiGroupOrders
+0x3e8 are two patrol buildings and a prey settler (AiGroupTarget
`patrol0` / `patrol1` / `prey`; AI::RunMilitary 0x58f990 logs "Patrouille
point 0 / 1" and sends the group to those buildings' positions). The three
interior searches 0x58aa70 / 0x58acd0 / 0x58af60 are one body copied with
small differences (InteriorSearch); 0x58af60 never finds anything
(original bug). g_aiGarrisonedBuildings 0x7e77dc (std::bitset<4000>) marks
military buildings with a garrison or one on the way;
FirstUnassignedMilitaryBuilding 0x58ae90 returns the first tower / castle
without that bit that no group patrols.

Session 55 (V, `ai_army_groups.cpp`, `ai_army_attack.cpp`): the periodic
army work. `FindSpareGroup` 0x5923e0 picks a spare group for an attack
(quirk: TileDistance gets the building's x twice); `RebuildAssignedSettlers`
0x591c40 rebuilds the settler bitset from groups and hunters and recounts
mages; `AssignIdleUnits` 0x593cd0 puts the soldier and mage chains into
groups. `TickAttackDelays` 0x594000 resets groups whose delay runs out.
`AttackEnemyCatapults` 0x593180 sends the nearest group (>= 2 members, not
defending) after each enemy catapult in the player's land (the per-player
vectors at 0x7e7a24 are the catapults counted by the census:
g_aiCatapultVectors, was g_aiAgentVectors); the group's AiGroupTarget prey
is the catapult, the delay 70 - r % 17. `HuntEnemyThieves` /
`HuntEnemyMages` 0x593ad0 / 0x593bd0 call `SendHunter` 0x5935b0 for every
enemy thief / mage in the player's land: a free hunter of the player's
hunter vector, else a soldier taken from the nearest group. Original bug:
the soldier search tests the type of the group's last member eleven times
instead of walking the members, so only a last member that is a
swordsman, pikeman or bowman is taken. `RecruitGeologistsAndThieves`
0x592e60 orders ConvertCarriers (want = other settlers / 64 + 1) in every
area of a player with a residence while its census count is below that
(the count is not re-checked per area, so every area gets an order). The
census fields +0x2b4..+0x3f4 are now named (geologists, thieves, pioneers,
other_settlers, all_settlers).

Session 55 (V, `ai_safety_agent.cpp`): the T_SAFETY_AGENT (kind 9) runs
every AiPlayerSetup +0x148 turns and then checks +0x14a grid cells: a cell
whose centre tile is the player's gets a 16-bit danger sum over the 5 x 5
cells around it from the player's building-type bitsets (AiGridCell
+0x24): setup weights +0x14c..+0x164 for gold smelter, gold mine,
storehouse, weaponsmith, toolsmith, coal mine, iron mine, iron smelter,
harbour and the race's war workshop, small and large temple (and the
Asians' rice farm), -250 per large tower and -350 per castle cell. At 100
or more, unless a Random % 100 falls below +0x166, it asks for a large
tower (or, above 200 and a Random % 100 below +0x168, a castle) at the
cell centre with priority 30000 (RecordBestCandidate), when fewer than 3
of that type stand within 48 (64) and no site note of it is there.
SafetyAgent in ai_layout.h.

Session 55 (V, `ai_army_attack.cpp`): `StartAttack` 0x594080 (from
RunMilitary with ratio = Player::soldier_strength / 256). In mission 56
from tick 81000 the scripted assault; otherwise nothing in the first 31
calls (AiArmyState +0x190), while a gather building is pending, below 15
census soldiers, or while a group attacks. The enemy is the playing
player of another team with towers / castles / barracks whose start
position shares the player's walk region and whose team strength is the
lowest (a computer enemy without an active teammate only on a Random draw
below 200). Attack when soldiers x ratio x 0.75 exceed the enemy's, the
enemy's team strength x 2.2 is below the own x ratio, or the soldiers
exceed 4x the enemy's (missions 70 / 72 have exceptions for players 2 /
1): the target is the enemy's weakest frontier building, dropped when more
than 1.5 x (soldiers x ratio, as float) enemy soldiers are within 60; an
enemy without towers but with barracks loses its first barracks instead
when the player has enough spare groups. Up to 50 spare groups get the
target (delay 2200 - r % 550) and go there, or first to the gather
building (nearest interior military building, countdown 15). Otherwise a
"steal": when soldiers minus own towers exceed 10, the first enemy
military building with a spare, ready group in the same walk region within
120 tiles whose tenth is at least the defenders within 60 (delay 900 - r %
225, steal flag 1).

## How decisions reach the simulation

Most decisions don't write the world directly (the world is read through
`g_aiWorld` 0x7e3408 and `g_aiMapWorld` 0x7e7b60). Two exceptions, found
in the session 50 naming pass (re/computer_players_naming.md): the
military code writes the computer players' `player_lists` /
`player_list_lengths` itself (AssignSoldier 0x592820, AssignMage 0x5926e0,
SendHunter 0x5935b0, DisbandWeakGroups 0x592bf0, MoveLastGroupInto
0x592240), and ProcessWorldEvents 0x590da0 consumes the world's event ring
(advances world +0x366f8). Both are simulation state, so replacements must
keep them exact. The other decisions become
**commands**: 0x21 messages pushed with `MessageQueue::Push` 0x4309c0 on
the logic->net queue (`ComputerPlayer::message_queue` +0x760, or the
global 0x7e37c8), each guarded by `0x4318a0` (a flag at game setup
0x7a7a18 +0x7c, probably "this machine runs the computer players"). So
they are recorded and replayed like human commands (CLAUDE.md "Commands
and lockstep"; ids in `re/execute_commands.md`, enum `CommandId`):

| Sender | Command | Callers |
|---|---|---|
| 0x58e6a0 | 1 PlaceBuilding | 0x58e200 |
| 0x58eac0 | 4 SetBuildingWorking | 0x573560, 0x573670, 0x59c0f0 |
| 0x58e220 | 5 SetWorkArea | 0x554980, 0x560e00, 0x573890 |
| 0x58e5c0 | 6 DestroyBuilding | 0x554980, 0x560e00, 0x56a000, 0x573890 |
| 0x58e7d0 | 7 ChangeGoodsPriority | 0x573760, 0x59bf70 |
| 0x58e8b0 | 8 SetGoodsSetting | 0x58e930 |
| 0x58ebc0 | 10 ChangeTransportOrder | 0x550a30 (initial settings) |
| 0x58e740, 0x58eb40 | 11 ChangeToolSettings | 0x56a840, 0x550a30 |
| 0x58e630 | 12 ChangeGarrison | 0x573890 |
| 0x58ea30 | 17 SetAreaCondition | 0x550a30, 0x5724e0, 0x572a30, 0x572fc0 |
| 0x58e850 | 24 PromoteSoldiers | 0x56a0f0 |
| 0x58e510 | 245 PrioritizeSite | 0x573560 |
| 0x590a30 | 3 SetSettlerGroup + 9 MoveUnits | 0x5935b0, 0x594e70, 0x595050 |
| 0x590af0 | 3 + 14 MoveUnitsMode2 | 0x595050 |
| 0x590bb0 | 12 ChangeGarrison | 0x58f990, 0x590da0 |
| 0x590c10 | 16 ConvertCarriers | 0x592e60 |
| 0x590c80 | 15 MoveUnitsMode3 / 9 MoveUnits | 0x58f990, 0x590da0, 0x592620, 0x593180, 0x594080, 0x594d40 |
| 0x590990 | 21 CastSpell | ten functions 0x5957c0..0x596210 (one per spell kind, probably) |

**Session 52 (V, package A4, `ai_commands.cpp`):** all senders above except
0x58e510 are replaced and verified. `0x4318a0` is `GameSettings::IsHost`
(byte +0x7c, set by NewGame and LoadSaveGame, cleared by the network
RemoteSetup 0x4322e0 and the reset 0x433810: G, "this machine set up the
game"). The economy senders 0x58e220..0x58ebc0 build their message in an
uninitialized stack buffer: header byte 3 and some payload bytes (listed in
ai_commands.cpp) are stack leftovers; ours writes 0 there, and
ExecuteCommands' handlers never read those bytes, **except** for
`AI::SendPrioritizeSite` 0x58e510: PrioritizeSite (0xf5) reads its player
from message byte 6, which that sender never writes, and does nothing unless
it equals the site's owner. So whether a computer player's site
prioritization works depends on the original's stack contents (open; not
replaced; observe at runtime). The military senders 0x590990..0x590c80 build
in `g_aiCommandSender` (0x7e5de8) +0xc, so unwritten bytes keep the previous
command's values (deterministic, reproduced). 0x58e6a0 always sends the
building type's high byte as 0 (`shr cl, 8` on a byte). SendMoveGroup
attack-moves (MoveUnitsMode3) when the group has no target settler and has
the steal flag, no target building or defends; a defending group stays while
enemy bowmen are within 30 of its first member (0x58a610 on
`g_aiEnemySearch` 0x7e5de4).

The building placement check the AI uses is the simulation's own
`CGameData::CheckBuildingSite` 0x4d99e0 (verified; called from 0x575df0
and 0x5760d0 on `ComputerPlayer::world`; the mode argument there is still
to be read).

## Battle spells (session 53)

`AI::CastBattleSpells` 0x5953b0 (from RunMilitary 0x58f990, when a per-player
counter at 0x7e5918 has passed 75; it is then reset) walks the player's mages
(settler list 36). Per mage, by
the mage's own race, bit k is set when `Player::mana` pays (unsigned >=)
`Player::spell_mana[battle_spells[race][k]]` (slots from
`AI::InitBattleSpellSlots` 0x590870: the index of the race's battle spell
in `g_raceSpells`). With any bit, it counts the enemy soldiers within 20 of
the mage (`AI::CountEnemySoldiersNear` 0x58a2c0: players not of the
player's team, the nine soldier lists, TileDistance) and tries the race's
casters in a fixed order; a caster whose bit is set gets one
`CGameData::Random` draw and runs if the draw's low word is above 25000
(about 24 %), else the next one is tried. Order: Romans 11, 10; Egyptians
13, 15, 12; Asians 14, 17, 16; Amazons 24 (on the slot-1 bit), 25 (on the
slot-0 bit), the crossed pairing being an original bug (kept). The casters
(0x5957c0..0x596210, stdcall (player, mage, enemies, minimum 0)): the
Romans cast nothing in campaign mission 67; 11, 24 need at least 5
enemies, 13 at least 4, the others more than the minimum (0, i.e. any
enemy). Targets: the first settler of the wanted kind among the first 500
map offsets around the mage: enemy soldiers (10, 11, 24), own soldiers
(12, 16, 17), enemy bowmen (types 8, 30, 33: 15, 25); 13 casts at a
random map position (two draws, y first), 14 at the player's start
position when the members of its unit groups 1..9 have at most a third of
their start health. A mage of a race above 3 makes the original read its
mana bits from stack leftovers; that leads at most to a side-effect-free
enemy count, never a cast (ours skips such mages).

## Randomness and determinism

The AI uses only the simulation's RNG `CGameData::Random` 0x4feb70 (13
call sites: 0x568890, the T_DO_BUILD_HOUSE_NOTE code 0x56cd90, 0x5868a0,
0x58f990, 0x590da0, 0x593180, 0x594080, 0x594d40, 0x594f70, 0x595050,
0x5952d0, 0x5953b0, 0x595bb0). No call of the CRT's `rand` (R: the AI
range calls only operator new/delete, memmove, atexit, sprintf, the EH
helpers, two lock helpers and `__ftol`). So computer players are part
of the deterministic simulation, and their commands are in every
recording; the determinism trace hashes the world, not the AI objects
(which live on the heap), so an AI difference shows up only once it
changes a command.

## Data the AI reads from the world

Through `g_aiWorld` / `g_aiMapWorld` (both the CGameData; R): map size
(+0x2d2d0), tiles (owner, flags, walk region, area, resources), player
records (active, team, start position), building records (owner, type,
buildup, position, settings), pile and area records (`AI::AreaGoodsAmount`
0x59dc10, `AI::CountBuildings` 0x59dac0, `AI::BuildingArea` 0x599610),
settlers (to collect soldiers for attacks, 0x594080 family). A full list
needs `tools/world_fields.py` over the AI range (TODO).

## Plan for the next rounds (bottom-up)

Frontier after batch 1 (`python tools/frontier.py --range 54cbd0 59fcd0`):
215 READY, 72 NEAR. Suggested packages:

1. **A2: remaining small helpers** (READY, mostly < 70 instructions):
   the `0x5693c0..0x569c90` family (T_ECONOMY_AGENT conditions calling
   RecordBestCandidate), `0x5995c0..0x5999e0` / `0x599510..0x599870`
   (map/tile queries of CompMapWorkerAgent), `0x58ed30`, `0x58ed70`,
   `0x58ecb0`, `0x59dbb0`, `0x59dc70`, `0x5930f0`, `0x593140`, `0x593f50`,
   `0x5720c0`, `0x571d00..0x572420`, `0x597110..0x597260`.
2. **A3: CComputerPlayer core**: done (session 48): the note tree, the
   agents, the destructor, StartBestBuilding and the note callbacks (18
   functions, see the note life cycle).
3. **A4: command senders** `0x58e220..0x58ec33`, `0x590990..0x590d86`
   (need `MessageQueue::Push` and `0x4318a0`; Push is a pass-through hook
   of the recorder, so the emulator must probe it as a callee).
   `0x590990` alone unblocks 10 functions.
4. **A5: Cevaland** (land evaluation): `0x5540f0`, `0x5547a0`,
   `0x5548b0`, `0x5540a0` done (session 54, see "The land evaluation
   grid" below); open: `0x553e10`/`0x553ea0`, the lists `0x5593a0`..`0x55a440`
   (SEH, Watcom-style containers), then `0x554980`, `0x555260`,
   `0x556080`, `0x556630`/`0x5579d0` (1,361 instructions each).
5. **A6: agents' run functions**, smallest first: T_SAFETY (0.7k),
   GLOBAL_EXPAND_EMPIRE (1.4k), T_ECONOMY and CCentrumCheck (4.2k each),
   T_MAP_WORKER (7.3k), T_ATTACK (10.2k), the census/military 0x58f990
   (10.1k), then 0x550f80 itself and the constructor 0x550900 / 0x550a30.
6. Whole-game evidence comes with every recording (computer players run
   in all of them); `--skip-hooks` can isolate an AI replacement when a
   replay diverges.

## The land evaluation grid (session 54, V)

One `AiGridCell` (0xc4 bytes) per 8 x 8 tiles; fields named in
ai_layout.h, how each byte is computed in `ai_evaland_cells.cpp`.

- `EvaluateCell` 0x5540f0 tallies the cell's tiles by terrain class:
  water tiles give the flags (has water / no water) and the fish (kind 0
  resources), grass tiles the stones, trees and grain / wine / rice land
  percentages (TileSlope < 7 grain, >= 7 wine; swampy grass 0x15 rice),
  mountain tiles the five mine resources, stream tiles (class 0x60) the
  waterworks' water. Amounts are stored halved, but a sum above 250 as 250
  (so 250 -> 125, 251 -> 250; quirk kept).
- `UpdateNextCells` 0x5547a0 (every computer player step): the cursor
  visits up to 64 cells and re-evaluates up to 4 owned ones (and their 3 x
  3 sums, GridSumAround 0x554530). `ReevaluateAroundTile` 0x5548b0 does
  the 3 x 3 cells around a tile.
- `DiscountExploitedResources` 0x59bc20 (every 71 steps): for the
  player's finished woodcutters, stonecutters, mines and fisheries the
  3 x 3 sum of their resource is divided by 8 in the building's cell and by
  4, 2, 1 in the square rings 1..3 of cells around it; the 4th ring's
  shift count 3 - 4 wraps to 31 on x86, so those sums are **cleared**
  (quirk kept). Only cells whose centre tile the player owns; only when
  the work point is on the building's row or column.
- `CountResourceInOwnersLand` 0x599a30 / `CountPlayersResourcesAround`
  0x599f50 count a resource on the player's tiles of a cell / of the 3 x 3
  cells whose presence byte is set. Slope exactly 7 counts as grain, rice
  and wine land; fish (goods 8) is not handled (DebugLog per owned tile).
- The per-player building-type bitsets at +0x24 are filled at the start by
  `MarkBuildingTypesInLandGrid` 0x5723a0 (once, guarded by
  g_aiMarkBuildingTypesPending 0x7312ac).

The ground request's site search (`ai_ground_request.cpp`, V): CheckSite
0x5760d0 rates a tile (0 = impossible, else CheckBuildingSite's earth
class; mines need g_aiPlayerSetup->min_mine_resources nearby, fields keep
their work radius from the request's place, every site keeps away from
the player's fields' work areas and from its buildings of the same type,
g_aiSameTypeDistance 0x731a88). CheckAndImproveSite 0x575df0 then moves a
mine to the richest spot among the first 100 offsets and other buildings
(not the small tower) to the best-rated of the first 150 offsets.
SetForestersWorking 0x59c0f0: foresters work while the area's logs / 5 <
its woodcutters (AiPlayerData::building_counts[2]).

Open questions: who makes the
AiPlayerData nodes (one per economy area, see step 4 of 0x550f80) and
when; what
0x4318a0's flag is in multiplayer (G: only the host runs the AI).
