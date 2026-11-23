# Computer players (AI)

Analysis of the computer players' code (session 47, batch 1), from the
dumpbin disassembly (`build/s3.asm`), the RTTI (`re/classes.json`) and the
source-file ranges (`re/source_files.md`). Confidence: **V** verified (a
reimplemented and tested function shows it), **R** read from the code,
**G** guess.

## Where the code is

- Range `0x54cbd0..0x59fcd0` (after Volker\landscape.cpp, before the
  Credits code): 502 game functions, ~50k instructions (plus ~1,270
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
`src/s3re/game/ai_layout.h`):

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
| 0 | `LOCAL_WARE_REQUEST_AGENT` | 0x65a7f0 | 0x57ab50 | 0x59f200 (NoOp) | |
| 1, 2, 4 | `HOUSE_REQUEST_AGENT`, `HOUSE_GROUND_REQUEST_AGENT`, `T_DO_BUILD_HOUSE_AGENT` (which is which: to confirm in 0x56f6d0's jump table) | 0x65a780 / 0x65a7b8 / 0x65a710 | | NoOp | |
| 3 | `GLOBAL_EXPAND_EMPIRE_AGENT` | 0x65a748 | 0x56cd00 (V) | 0x56cd20 | 29 fns, 1.4k ins |
| 5 | `COMPUTER_PLAYER_AGENT` | 0x65a6d8 | 0x584680 (V) | 0x5846a0 | 2 fns |
| 6 | `T_MAP_WORKER_AGENT` | 0x65a6a0 | 0x57cda0 (V) | 0x57cde0 | 56 fns, 7.3k ins |
| 7 | `T_ATTACK_AGENT` | 0x65a668 | 0x55ebe0 (V) | 0x55ec10 | 77 fns, 10.2k ins |
| 8 | `T_ECONOMY_AGENT` | 0x65a630 | 0x567850 (V) | 0x567890 | 55 fns, 4.2k ins |
| 9 | `T_SAFETY_AGENT` | 0x65a5f8 | 0x586830 (V) | 0x586850 | 16 fns, 0.7k ins |
| 10 | `CCentrumCheckAgent` | 0x65a5c0 | 0x560dd0 (V) | 0x560e00 | 48 fns, 4.3k ins |

The request agents (kinds 0, 1, 2, 4) instead have their own slots 2, 3,
4, 9, 10: slot 3 handles a new note (0x5654d0, 0x5787c0, 0x576510),
slots 9/10 are the note callbacks 0x582480 / 0x582510 call (G: "child
note finished" / "note removed").

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
0x5514c0 on the next step), active notes (+0x134, 100) and a fourth list
(+0x200, 50, `AI::List32Add`; meaning unknown).

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
3. If the player has no nodes (`settlement_lists[p].count` == 0): only
   `agents[7]` slot 8 runs (probably T_ATTACK_AGENT: a player without land
   can only fight).
4. Pick the current node (`data` +0x778 / `data_key` +0x77c): the next
   node of the list, round robin (or the first one after a reset flag at
   +0xf0 + 12 * p of the node lists, which it clears). The nodes (`AiPlayerData`) are probably one per settlement
   / economy area (G; word +8 is its key, passed to 0x59dbb0 with the
   player).
5. `AI::SetCurrentPlayer` (globals g_aiPlayer, g_aiPlayerNumber,
   g_aiPlayerSetup, g_aiPlayerData).
6. Every 71 steps (`timer278`): `0x59bc20`, `0x59c0f0` (CompMapWorkerAgent
   area) and return.
7. Otherwise: `0x5517d0` (notes: 0x551590, 0x5514c0 hands pending notes
   to their handler agent's slot 3, 0x551670), `0x551a70` (when fewer
   than 4 of something in the area (0x59dbb0): picks the highest build
   priority and may call 0x58ecb0).
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
falls as have * 100 / wanted rises past 10 / 30 / 50 / 70 / 100. Still
open in this family: 0x567e00, 0x568420, 0x568890 (Random), 0x568bc0,
0x568d80, 0x568f40 (larger conditions with float curves per race) and
0x567910 itself.

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

## How decisions reach the simulation

The AI never writes the world directly as far as seen (R: the writes of
the verified functions go to AI objects only; the world is read through
`g_aiWorld` 0x7e3408 and `g_aiMapWorld` 0x7e7b60). Decisions become
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

The building placement check the AI uses is the simulation's own
`CGameData::CheckBuildingSite` 0x4d99e0 (verified; called from 0x575df0
and 0x5760d0 on `ComputerPlayer::world`; the mode argument there is still
to be read).

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
2. **A3: CComputerPlayer core**: the note tree (`0x551330` DeleteNote,
   `0x551270`, `0x551400`, `0x551490`, `0x551510` dispatch, `0x5514c0`,
   `0x551590`, `0x5516e0`, `0x551670`), destructor `0x5509d0` /
   `0x550f30` (needs Cevaland `0x553dc0`, verified), `0x551a70`. They make
   virtual calls (agents' slots 3, 9, 10, notes' destructors): use
   `Spec.virtuals` / FAKE_VTABLE.
3. **A4: command senders** `0x58e220..0x58ec33`, `0x590990..0x590d86`
   (need `MessageQueue::Push` and `0x4318a0`; Push is a pass-through hook
   of the recorder, so the emulator must probe it as a callee).
   `0x590990` alone unblocks 10 functions.
4. **A5: Cevaland** (land evaluation): `0x5540f0`, `0x5547a0`,
   `0x5548b0`, `0x553e10`/`0x553ea0`, the lists `0x5593a0`..`0x55a440`
   (SEH, Watcom-style containers), then `0x554980`, `0x555260`,
   `0x556080`, `0x556630`/`0x5579d0` (1,361 instructions each).
5. **A6: agents' run functions**, smallest first: T_SAFETY (0.7k),
   GLOBAL_EXPAND_EMPIRE (1.4k), T_ECONOMY and CCentrumCheck (4.2k each),
   T_MAP_WORKER (7.3k), T_ATTACK (10.2k), the census/military 0x58f990
   (10.1k), then 0x550f80 itself and the constructor 0x550900 / 0x550a30.
6. Whole-game evidence comes with every recording (computer players run
   in all of them); `--skip-hooks` can isolate an AI replacement when a
   replay diverges.

Open questions: kinds 1, 2, 4 of the factory; the meaning of list200
(+0x200) and of the nodes (`AiPlayerData`, per settlement?); what
0x4318a0's flag is in multiplayer (G: only the host runs the AI).
