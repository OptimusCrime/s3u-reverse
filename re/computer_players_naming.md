# Computer players: findings of the naming pass (session 50)

Structural findings of the naming pass of 2026-11-24 over the AI range
0x54cbd0..0x5a0000, by four parallel reviews (one per address range).
Read from the code, not yet confirmed by reimplemented functions; merge
each item into `re/computer_players.md`, `src/s3re/game/ai/ai_layout.h`
or `re/structs.md` when the code it describes is reimplemented (TODO.md
"Computer players"). Function names are in `re/functions.csv` (notes
start with "Named (session 50, ...)"). Where the parts disagree, the
code decides.

## Notes: naming of 0x54cbd0..0x565000 (CComputerPlayer unit, Rainer\Cevaland.cpp)

Names proposed in `ai1.tsv` (65 rows). Skipped: 0x550900 (already
`CComputerPlayer::CComputerPlayer`, correct), 0x551637 (false function start
inside DeactivateNote, no name), 0x54cbd0 (a landscape-drawing routine of
Volker\landscape.cpp: locks a DirectDraw surface (0x5bf050, DDSURFACEDESC
0x6c), uses g_screenWidth/Height, DebugLog "Division durch 0 bei %u %u";
called by 0x4b26c0 on `[ebx+0x5f6cc]`; not analysed far enough to name).

### 24 static initializers (not AI code)

24 of the 68 "AI_sub_/Render_sub_/Unknown_sub_" rows are compiler static
initializers: `std::string G = std::string L + "\\General" | "\\Index Server"
| "\\Patches"`, where L is the file's static "Software\BlueByte\Siedler3\1.0"
string. Every AI .cpp includes the same header, so each object unit has the
triple. Proposed `StaticInit_<target>` like the existing rows; their category
should be `compiler`, not `game`. They are run by an initializer-table stub
16 bytes before each (`call init; jmp StaticDtorReg_<target>`, e.g. 0x54e8b0),
which is not a known function start, so asm_tools attributes the call to the
preceding StaticDtorReg row.

### CEvaLand ("pEvalMap") structure

- **`ComputerPlayer +0x774` is the `CEvaLand*`**, not "settlement_lists":
  CComputerPlayer::Init 0x550a30 stores AI::CEvaLand::Acquire 0x553d30 there,
  and 0x550f80 passes it as ECX to 0x5547a0 / 0x554980. Rename the field
  (e.g. `eval_map`) and its comment in ai_layout.h.
- Strings: `"pEvalMap"` (0x7312e8) is the out-of-memory text of Acquire
  (ThrowOutOfMemory line 0x46), `"map_part_list_ptr"` (0x731314) that of the
  grid allocation in Init 0x553ea0. So the original names are probably
  "EvalMap" (the object) and "map parts" (the 8x8-tile grid cells).
- **`g_aiUnk7e3134` = the current CEvaLand** (set to `this` at the end of
  Init 0x553ea0, cleared by the destructor 0x553fe0 and by
  CComputerPlayer::InitAiGlobals 0x551b80); CCentrumCheckAgent::Run 0x560e00
  uses it as ECX for ReevaluateAroundTile. Suggest `g_aiEvalMap`.
- `CEvaLand::lists[20]` (+0x000) is one `std::list<Area>` per player (MSVC6
  list: +4 head node, +8 size). Node 0x744 bytes = next, prev, 0x73c-byte
  value. **`AiPlayerData` (ComputerPlayer +0x778) is a node of this list**,
  so its offsets include the 8-byte node header; the value (the "area
  record") starts at +8 with the economy area id word. A better struct name:
  `AiAreaData` / `CEvaLand::Area` (one per economy area owned by the
  player, created by AddArea 0x555260 and removed by RemoveArea 0x558e80 as
  areas change owner).
- `CEvaLand +0xf0`: 20 dwords, per player "area list changed" flag (set by
  RemoveArea at +0xf0 + 4 * player; cleared by Init). 0x550f80 reads and
  clears it with the player index in EAX, i.e. **+0xf0 + 4 * p** (the
  `lea edx,[eax+eax*2]` there is only for the list). re/computer_players.md
  step 4 says "+0xf0 + 12 * p": wrong.
- `+0x14c` = 64 (tiles per cell, the divisor of the percentages),
  `+0x150 / +0x154` = the cell cursor of UpdateNextCells 0x5547a0.
- The 20 lists are built by the CRT eh-vector constructor with ctor
  0x553e70 and dtor 0x5591e0 (`std::list<Area>::~list`, calls ~Area
  0x556080 per node); 0x559240 is the per-player bitset constructor of the
  cells. 0x553e70, 0x5591e0 and 0x559240 are function starts not in the
  Ghidra list (asm_tools counts 0x5591e0..0x559260 as part of 0x559170).

#### Grid cell (AiGridCell, 0xc4 bytes) from EvaluateCell 0x5540f0

| Off | Meaning |
|---|---|
| +0x00 word | flags: init 0b1010; any land tile: bit 3 cleared, bit 0 set; any water tile: bit 1 cleared, bit 2 set (so bit 1 = all land, bit 3 = all water) |
| +0x02 | tiles of terrain class 0x60 (capped 250) |
| +0x03 | stones: sum of AI::StoneAmountAt / 2 (capped 250) |
| +0x04 | trees (IsTreeAt count, capped 250) |
| +0x05 | mountain resource kind 0x20, amount sum / 2 |
| +0x06 | kind 0x10 |
| +0x07 | kind 0x30 |
| +0x08 | kind 0x50 |
| +0x09 | kind 0x40 |
| +0x0a | water tiles' resource amount (kind 0, probably fish) / 2 |
| +0x0b | % of grass (class 0x10) tiles with TileSlope < 7 (free, flat) |
| +0x0c | % of grass tiles with slope >= 7 |
| +0x0d | % of terrain 0x15 tiles without tile flags 1/8 and slope < 7 |
| +0x0e | % of grass tiles (class 0x10) |
| +0x24 | `bitset<57>[20]`: per player, the building types standing in this cell (set/cleared by ProcessPendingEvents 0x554980 events 2/5/6; read by HasBuildingTypeNear 0x558f00) |

The "class 0x10 / 0x20 / 0x60" are `Tile::terrain & 0xf0` (0 water,
0x10 grass/land, 0x20 mountain). Existing names `value03`, `value04`,
`values05` in ai_layout.h map to stones, trees, resources.

#### Area record (value of a list node; offsets below are node offsets, as AiPlayerData uses)

- node+0x08 word: economy area id. node+0x24 word: construction sites in the
  area (AddArea: CountSitesInArea 0x59dbb0; ProcessPendingEvents event 2: +1).
- node+0x2e0 / 0x2e4 / 0x2e8 = candidate_values[7 / 9 / 11] (coal mine, iron
  mine, iron smelter) set to 400 / 500 / 500 by AddArea when
  CheckBasicEconomy fails: the AI waits with mining until the basic economy
  stands.
- node+0x344 building_counts[type]: also +1 on event 2 and -1 on event 6
  ("RAINER: Durcheinander bei ABRISS!" when it hits 0 while another house
  of the type is still in the cell).
- node+0x3f4..+0x4d0: a pointer per building type to one of the record's
  site trees (several types share one tree, e.g. +0x714, +0x704, +0x6f4,
  +0x6e4 are used for several types). Site trees at node+0x4d4..+0x734,
  stride 0x10 (MSVC6 `_Tree`: +0 comparator byte, +1 allocator byte, +4
  head, +8 _Multi (1), +0xc size).
- Site trees are multisets of 6-byte values keyed (ordered) by their first
  byte (probably a rating; then x, y words - G), node 0x18 bytes, shared
  `_Nil` 0x7e0570 and `_Nilrefs` 0x7e0574 (globals to add).
  T_MAP_WORKER_AGENT::MakeBuildOrders 0x57ce90 takes candidates out with
  SiteTree::erase 0x5599b0.

#### AI event ring (world+0x2f990), event types as ProcessPendingEvents reads them

1 -> 0x573890 (a); 2 site placed (a = building id: owner byte +0, x +2,
y +4, type +6 of 0x58e1c0's record); 3 remembers a position (a = x, b = y)
for a following 6; 4 economy area a changed owner (area owner byte at
world+0xe145e8 + a*0x2e0; adds/removes/refills the area record); 5
building a changes owner (old owner b); 6 building of type a of player b
removed at the remembered position; 7 building a finished (SetWorkArea for
types 2/3, DestroyBuilding for 7, 8, 9, 29, 37 - meaning of that still
open); 8 re-evaluate around (a, b). Only "human" player kind 2
(world+0x11943dc + p*0x148 == 2, probably "computer player") records are
updated. Worth adding to re/structs.md "Pending event".

### Census and AI globals (savegame chunks)

**Done 2026-11-24** (ai_layout.h, globals.h, re/globals.csv): `g_aiGroupOrders`
is 0x20f8 bytes with the fields below, `g_aiArmyState` 0x51c
(`group_values` -> `attack_delays`), the census is `g_aiCensus` 0x7e5ef8
(`settlers_counted`, `group_target_buildings`, `group_defending`),
`g_aiUnk7e3134` -> `g_aiLandEvaluation`, ComputerPlayer +0x774 ->
`land_evaluation`, `AiList50Owner` merged into ComputerPlayer; renamed the
DoomNote functions, `AI::CEvaLand::*` (ResetGrid, SumAllCells, GridCell,
FreeGrid, GridCellOfTile, GridCellWrapped, GridSumAround, ResetGlobals),
`AI::CellCentreOwner`, `AI::TileOwner`; re/computer_players.md corrected.

- The census `AiAgentData` lives at **0x7e5ef8** (savegame chunk 0x16, size
  0x1ad8 = sizeof(AiAgentData)). Therefore **`AiArmyState` (g_aiArmyState
  0x7e58c8) is only 0x51c bytes** (chunk 0x17): its fields from +0x630 on
  (`use_player_population`, `group_words` +0x634, `group_bytes` +0x7c4)
  are the census's byte +0, `words004` and `bytes194`. Fix ai_layout.h /
  globals.csv. 0x58f990 calls CountSettlers 0x55b880 with ECX = 0x7e5ef8
  (0x5907c0), which sets census byte +0 to 0 when a count starts and to 1
  (with +2 = 1) when the last slice is done. So the economy agent's
  "use_player_population" byte 0x7e5ef8 (0x567910) is really "the census
  settler count is complete" (then it takes the per-player value instead
  of counting the area).
- **`g_aiGroupOrders` (0x7e37d0) is 0x20f8 bytes** (chunk 0x15), not 0x898;
  0x7e37d0 + 0x20f8 = 0x7e58c8.
- AI::LoadGlobals 0x551210 / 0x596380 also copy `g_aiCatapultVectors[i].begin`
  into `.end` (0x7e3690 <- 0x7e368c and 0x7e7a28 <- 0x7e7a24 per 16 bytes:
  two arrays of 20 vectors, emptied) and set 0x7e7cc8 = 0x7e7cc9 = 1.
- Census fields from AI::Census::CountSettlers 0x55b880:
  `soldiers[p][9]` order is Bowman1, Bowman2, Bowman3, Pikeman1..3,
  Swordsman1..3 (types 8, 30, 33, 15, 31, 34, 5, 32, 35); so the
  "kinds 2/5/8 double" of ComputeTeamStrength are the level-3 soldiers.
  `dwords2b4` geologists (22), `dwords304` thieves (23), `dwords354`
  pioneers (40), `dwords3a4` all other settlers, `dwords3f4` all settlers
  per player. `g_aiCatapultVectors[p]` (0x7e7a24) = per player vector of the
  settler numbers of its catapults (type 41); rename to e.g.
  `g_aiCatapults`. Counting runs in 8 slices of the settler high-water
  mark (0x12a5e5c; state 0x7e06e0, position 0x7e0578), maps under 50
  settlers in one go.

### Corrections of other names

Done in session 50: `AI::SiteTree::begin` and `BLACK_BOARD_AGENT::IsBusy`.

- `AI::ListFirst` 0x559260 (verified): it is `std::_Tree::begin()`
  (head->left) as used on the site trees by ~Area 0x556080, AddArea,
  FillAreaSiteLists and RefillAreaSiteList (39 + 21 calls); probably
  `AI::SiteTree::begin`.
- `AI::ClearGlobals7e0568` 0x553e00: clears g_aiEvaLandRefCount and
  g_aiEvaLand: `AI::CEvaLand::ResetGlobals`.
- `AI::EvaLand::*` and `AI::CEvaLand_*` rows (0x554070, 0x5546c0, 0x554700,
  0x554040, 0x554770): unify to `AI::CEvaLand::` (ResetGrid, SumAllCells,
  GridCell, FreeGrid, GridCellOfTile).
- `AI::BlockCentreByte8_554850`: tile byte +8 is `Tile::owner`, so
  "CellCentreOwner".
- `BLACK_BOARD_AGENT::vf05` (0x55ca80, `xor eax,eax; ret`) is slot 5 =
  "busy" (WorkOnNote skips while non-zero): `BLACK_BOARD_AGENT::IsBusy`.
- 0x551210 / 0x551220 survey names said "probably loading/saving the AI
  state" (G); confirmed: chunk ids 0x15..0x17 through Dirk\SaveGames.cpp.
- `T_ATTACK_AGENT::Run` calls 0x58f990 (currently AI_sub_58f990, "census /
  military" in the doc) with the catch text "Marcus: FOE ATTACK EXCEPTION!"
  (Marcus: probably another programmer); 0x58f990 is the attack agent's
  main work, not only the census.

## AI naming round 2 (0x565000..0x58b520): structural findings

Proposals: ai2.tsv (117 rows: 96 high, 21 medium). Skipped (4): 0x567870
T_ECONOMY_AGENT::vf11 (slot 11, never called through a vtable; calls the
base NoOp_ret4 and clears +0x14), 0x56cd90 / 0x57ce90 (already named
MakeBuildOrders, fine), 0x56faa0 (name already right).

### Corrections of existing names (including verified ones)

Done in session 50: items 1 (`BLACK_BOARD_NOTE::ExecuteAtHandler`, `agent_slot::kExecute`)
and 3 (`std::multimap::end_580220` / `empty_580230`, category stl like their neighbours).

1. **0x582510 `BLACK_BOARD_NOTE::CancelAtHandler` is not a cancel.** Slot 10
   is "carry the request out": T_DO_BUILD_HOUSE_AGENT::WorkOnNote 0x5655c0
   calls its own slot 10 once the house request reported success (state 2);
   slot 10 0x5656e0 -> HOUSE_REQUEST child -> 0x582510 -> HOUSE_REQUEST_AGENT
   slot 10 0x5789b0 -> ground child -> 0x582510 -> HOUSE_GROUND_REQUEST_AGENT
   slot 10 0x576610 -> 0x576430, which sends the PlaceBuilding command.
   Suggest `BLACK_BOARD_NOTE::ExecuteAtHandler`, `agent_slot::kExecute`
   (ai_layout.h comment "G: a note it took is withdrawn" is wrong), and the
   slot table in re/computer_players.md / re/structs.md line 618.
2. **0x56fac0 `BLACK_BOARD_AGENT::BLACK_BOARD_AGENT` is the destructor**
   (only caller: the derived agents' deleting destructor 0x56fad0, before
   operator delete). -> `~BLACK_BOARD_AGENT`.
3. **0x580220 `AI::List::begin_580220` (verified) is `std::map::end()`**, and
   0x580230 `AI::List::empty_580230` is `std::map::empty()`: the container at
   AiPlayerData value +0x68c is an MSVC6 `_Tree` (+4 _Head, +0xc _Size; end()
   = _Head, begin() would be _Left(_Head)). MakeBuildOrders 0x57ce90 does
   `it = end(); it--; *it; erase(it)` (0x580270 postfix --, 0x580260 *,
   0x580240 erase): it takes the highest-key (best) entry. Suggest
   `std::multimap::end_580220` / `empty_580230` (or map; multi-ness unknown).
4. `g_aiUnk7e3134` (re/globals.csv) is the **CEvaLand object**
   (AiLandEvaluation): 0x5868a0 reads its grid width/height at +0x140/+0x144,
   and `lists[20]` at +0 (12 bytes each: +4 head, +8 size) are the per-player
   std::lists of AiPlayerData nodes (FindNode 0x573be0, COMP_* functions,
   0x572420). Suggest `g_aiLandEvaluation`. ComputerPlayer::settlement_lists
   (+0x774) is probably the same pointer.
5. **AiPlayerData offsets are list-node offsets.** 0x580290 (`*it` = node+8)
   is called with ECX = &g_aiPlayerData, so g_aiPlayerData is a std::list
   iterator / node pointer: +0 next, +4 prev, value from +8. The "area word
   at +8" is the value's first field. The existing struct keeps node-relative
   offsets, which is fine, but the comment should say so.
6. 0x5654d0 etc.: the notes "T_DO_BUILD_HOUSE_AGENT's slot 3 turns one into a
   HOUSE_REQUEST_NOTE below it" are right; HOUSE_REQUEST_AGENT's slot 3 makes
   the ground request (Init 0x575d60, found now: ai_layout.h says "its init is
   not found yet") and the local ware request, both below the house request.

### The request chain (T_DO_BUILD_HOUSE -> HOUSE_REQUEST -> ground + ware)

- Request agents' slots: 2 GetNoteTypes (writes its own note type, AX = 1),
  3 TakeNote, 4 WorkOnNote, 9 NoteReported, 10 Execute. Base / non-request
  agents use 0x57cdc0 (AX = 0) for 2, 3, 4, 10; slot 7 is 0x57cdd0 (returns
  1, "wants to run") in the active agents and 0x55ca80 (0) in the request
  agents; slot 8 is Run.
- T_DO_BUILD_HOUSE_NOTE state +0x24: 2 done, 4 no site found (give up), 8
  site found but something else failed (retry); +0x26 retries (max 5). On
  giving up, the type's AiPlayerData::type_blocked byte is cleared in the
  area's node(s) (so type_blocked = "a build order for this type is
  running").
- HOUSE_REQUEST_NOTE: +0x24 bits 1 ground found, 2 ware/tool OK; +0x26 number
  of child reports; +0x28 site position (x | y << 16).
- HOUSE_GROUND_REQUEST_NOTE (0x4c bytes): +0x1c type, +0x1e/+0x20 centre,
  +0x28 found, +0x2c ring iterator (current x/y at +0x2c/+0x2e; 0x597070
  constructs it, 0x5970f0 bounds, 0x5970c0 centre, 0x597110 step), +0x3c
  ring, +0x3e side 0..5, +0x40 step, +0x42 max radius (the request's 4th
  value), +0x44 skip-check flag, +0x48 military site.
- Site rules (0x5760d0): own tile; mines need >= g_aiPlayerSetup+4 resources
  of their kind on rings 0..2 (resource kinds: coal 0x10, iron 0x20, gold
  0x30, gem 0x40, sulfur 0x50); fields (vineyard, farm, rice farm) keep their
  work radius from the request's place (the distance of map offset 150,
  300 + 5, 400 + 3) and every site keeps away from the player's finished
  fields' work points (offset 150 / 300 / 400 distance + 4) (V, session 54); table
  0x731a88 = minimum distance to a building of the same type (woodcutter 4,
  stonecutter 4, sawmill/forester/mines 8, fields/fishery/apiary 24);
  final value from CheckBuildingSite 0x4d99e0 (lower is better, < 3 taken
  at once by 0x575df0).
- Table 0x732804 (4 bytes per building type): tool goods + worker settler
  type (woodcutter axe/3, stonecutter pickaxe?15/4, sawmill saw 16/6, mines
  pickaxe 15/10, smiths hammer 13/11, fishery rod 17/17, farm 32/16 ...).

### g_aiPlayerSetup (0x7e2eb4, 0x180 bytes per setup) fields seen

+0x00 ground-search positions per WorkOnNote turn; +0x04 minimum mine
resources; +0xb2 COMPUTER_PLAYER_AGENT small-tower period (x14 + player);
+0xc4 soldier-promotion period (x14 steps); +0x148 T_SAFETY_AGENT period,
+0x14a cells checked per run, +0x14c..+0x164 threat weights per building
kind, +0x166 / +0x168 Random thresholds (percent). Probably the difficulty
/ personality table.

### Other fields

- AiPlayerData (node offsets): +0x10 short_of_wood and +0x14 short_of_stone
  confirmed by the original names COMP_EmergencyPowersActWood/Stone; +0x18
  force-production flag (COMP_ForceProduction); +0x1c soldier-share stage
  0..3 (0x56a840); +0x24 word decremented when a building of the node is
  finished (0x573890; G: open build orders); best[57+15] at +0x1d6 is the
  GLOBAL_EXPAND_EMPIRE_AGENT trigger; +0x68c a std::(multi)map of candidate
  sites used by T_MAP_WORKER_AGENT::MakeBuildOrders.
- ComputerPlayer +0x768: soldier promotions done (max 3, 0x56a0f0); +0x764
  steps is reset by each promotion.
- Player::active == 2 marks a computer player (0x573890).
- CEvaLand grid cell +0x24 + player*8: std::bitset<64> of the building types
  the player has in the cell (0x5723a0 sets, 0x5868a0 reads).
- Building production bytes: +0x1a digs and +0x30/+0x32 look like a 32-bit
  dig success history for mines (0x573810, G).
- EconomyAgent: +0x38 the residential stage float, +0x3c wanted residents.
- Unit-group building table at 0x7e3bc0 (per player 30 words, 10 groups x 3)
  and a building bitset at 0x7e77dc (std::bitset<4000>?), both
  CompMapWorkerAgent globals.

### Original bug

0x58af60 starts its best distance at 0 and keeps a building only when
`distance < best` (jge at 0x58b116), so it always returns 0 (its caller
0x58f990 never gets a building from it). A reimplementation must keep that.

### Agents in this range (what each does)

- T_DO_BUILD_HOUSE / HOUSE_REQUEST / HOUSE_GROUND_REQUEST / LOCAL_WARE_REQUEST:
  the build-order pipeline above (find a site by a spiral search, check the
  worker's tool, then place the building with command 1).
- T_ECONOMY_AGENT (kind 8): build-order priorities per building type; plus
  every player+14 runs: demolish a residence when there are >10 free beds,
  promote soldiers (3 times max), adjust the area's three shares (command
  11), and the wood/stone emergency mode (pause sites, prioritize the
  stonecutter).
- GLOBAL_EXPAND_EMPIRE_AGENT (kind 3): when a small tower was taken for
  building, plans build orders around the next garrisoned military building
  toward resources (strings 'GLOBAL_EXPAND GOLD!/EISEN!/KOHLE!...').
- COMPUTER_PLAYER_AGENT (kind 5): periodically asks for a small tower
  (priority 20000).
- T_MAP_WORKER_AGENT (kind 6): every 14 runs turns the taken candidates
  best[57+type] into build orders (MakeBuildOrders).
- T_SAFETY_AGENT (kind 9): scans the land grid for own valuable buildings
  without military protection and asks for a tower there.

## ai3: 0x58b520..0x596650 (first half of Rainer\CompMapWorkerAgent.cpp): notes

78 rows named (44 high, 34 medium, 0 skipped). Names in `ai3.tsv`.

### What this range is

Apart from the static initializers and the command senders, 0x58f990..0x596540 is
the computer players' **military code**. Its entry is `AI::RunMilitary` 0x58f990,
called only by `T_ATTACK_AGENT::vf08` 0x55ec10 (fastcall world, player =
g_aiPlayerNumber; stack: g_aiPlayer->message_queue, reset byte 0x7e3410). The
military state is not in the agent but in globals that are shared by every
computer player and indexed by player * 10 + group (groups 1..9; group 0 is
the scratch group the senders use):

| Global | Size | What |
|---|---|---|
| 0x7e37d0 (`g_aiGroupOrders`) | **0x20f8** (savegame chunk 21) | the "army" object (thiscall `this` of 0x591c40..0x5953b0) |
| 0x7e58c8 (`g_aiArmyState`) | **0x51c** (chunk 23) | per player and group dwords |
| 0x7e5de4 | 4+ | thiscall object of 0x58a2c0 / 0x58a610 / 0x58aa70...; dword +0 = MapOffset search limit (0x595050 sets 5000 or 500) |
| 0x7e5de8 | 0x18+ | "command sender": bytes 0..11 battle-spell slots by race * 3 + k (0x590870), +0xc the 12-byte command buffer of 0x590990..0x590c80 |
| 0x7e5ef8 (AiAgentData, census) | 0x1ad8 (chunk 22) | census, plus group fields (below) |
| 0x7e3688 | 20 x 16 | per player std::vector of {hunter settler, target settler} (8-byte elements) |
| 0x7e7a20 | 20 x 16 | per player std::vector<u32> of its catapults |

Saved / loaded by `AI::SaveMilitaryState` 0x596330 / `AI::LoadMilitaryState`
0x596380 (wrappers 0x551220 / 0x551210, so those two can be named
`CComputerPlayer::SaveMilitaryState` / `LoadMilitaryState`-style wrappers).

#### Army object 0x7e37d0 (current `ai::AiGroupOrders`, declared 0x898 bytes: too small)

- +0x000 u16 [20][10]: group home area (area id of the own military building
  nearest to the group's first member, set by AssignSoldier 0x592820).
  Groups with the same home area are paired; every second one is "spare"
  (0x591fb0 / 0x592100 / 0x5923e0) and may attack.
- +0x190: cleared per player and group by Reset (meaning open).
- +0x320 u8 [20][10] (`group_flags`): **steal flag**: 1 = the group goes for
  a weakly guarded enemy military building ("Start Steal", 0x594080);
  0x590c80 then uses attack-move.
- +0x3e8 6 bytes [20][10] (`group_targets`): x, y (cleared), +4 target settler
  (catapult, 0x593180).
- +0x898 / +0x8e8: per player dwords (0x898 = 0x7e4068: the gather building
  of a pending attack, "Start Gather"; set by 0x594080).
- +0x988 i32 [20][10]: mages per group (0x591c40, 0x5926e0).
- +0xca8 bitset of 32000 settlers (4000 bytes): settlers that have orders
  (group members, hunters, busy geologists). Its code calls the bitset
  `_Xran` 0x596570 on an index >= 32000 (std::bitset<32000>).
- +0x1d10 i32 [20]: highest group in use per player.
- +0x1d60 i32 [20]: group size per player (7 after Reset, +5 when all groups
  are full, up to 100; -5 when a group is disbanded).
- +0x1db0 / +0x1db2 i16 [20][10] (4 bytes each): gather position of a group.
- +0x20d0 / +0x20d2 i16 [10] (4 bytes each): per-group offsets added to the
  gather position (Reset writes +-18, +-9, +-27 into +0x20d4..+0x20f6).

#### g_aiArmyState 0x7e58c8 is only 0x51c bytes

`AiArmyState` in ai_layout.h runs to +0x7c4+200 and so overlaps the census.
The savegame chunk 23 is exactly 0x51c bytes (0x147 dwords in the loader), and
0x7e5de4 / 0x7e5de8 are separate objects. The fields placed past 0x51c belong
to `AiAgentData` at 0x7e5ef8:

- `AiArmyState::use_player_population` (+0x630) = `AiAgentData::unk000` (+0).
- `AiArmyState::group_words` (+0x634) = `AiAgentData::words004` (+4):
  **the group's target building** (0 = none; 0x594080, 0x594a10, 0x592100).
- `AiArmyState::group_bytes` (+0x7c4) = `AiAgentData::bytes194` (+0x194):
  **the group is defending** ("Verteidigung", 0x594a10; skipped as attackers).
- `AiArmyState::group_values` (+0x194) = **attack-delay countdowns**
  (0x594000 resets the group when one runs out: "RESET wegen abgelaufenem
  Attackdelay"; started with 70 - r%17 for a catapult hunt, 900 - r%225 for a
  steal, 2200 - r%550 for an attack).
- AiArmyState per_player0a0 (0x7e5968) is set to 15 together with the gather
  building (0x594080); 0x7e59b8 / 0x7e5a08 (per_player0f0 / per_player140) are
  the step counters of HuntEnemyThieves / HuntEnemyMages (> 420 / > 550);
  0x7e79d0 (`g_aiPerPlayer7e79d0`) the counter of AttackEnemyCatapults (> 50);
  0x7e5d7c the every-7th counter of SendGeologists.
- AiAgentData census: dwords2b4 = geologists, dwords304 = thieves, dwords3a4 a
  population figure (0x592e60 wants dwords3a4/64 + 1 of each); soldiers[p][9]
  at +0x444 summed per kind in 0x594080 (>= 15 soldiers before attacking).

`AI::ResetUnitGroup` 0x5930f0's inventory note ("clears ... g_aiArmyState
+0x194 dwords, +0x634 words, +0x7c4 bytes") is therefore: attack delay, census
target building, census defending flag (plus g_aiGroupOrders +0x320/+0x3e8).

### Corrections to re/computer_players.md

- "The AI never writes the world directly" is wrong. The military code writes
  `player_lists` / `player_list_lengths` of the computer players directly
  (AssignSoldier 0x592820, AssignMage 0x5926e0, SendHunter 0x5935b0,
  DisbandWeakGroups 0x592bf0, MoveLastGroupInto 0x592240), and
  ProcessWorldEvents 0x590da0 consumes the world's event ring (advances
  world+0x366f8). Both are simulation state, so the replacements must keep
  them exact (and the trace hashes them).
- The command sender table: 0x58e7d0 always sends "up" (value 1); 0x58e8b0
  always value 0; 0x58eb40 is ChangeToolSettings kind 10 only; 0x58e510 sends
  only for a construction site with request bit 0 while the player's request
  counter (Player +0x24) is below 10; 0x58e220 moves the tile into the
  building's area first. The 0x590990..0x590c80 senders are thiscall on
  0x7e5de8 and push on the global queue 0x7e37c8 (set by RunMilitary), the
  0x58e220..0x58ebc0 ones are fastcall with pointer arguments and push on
  g_aiPlayer +0x760.
- Message layouts (payload after the 4-byte header): see the evidence column;
  all match the ExecuteCommands handlers.

### Behaviour worth knowing

- **Amazon spell quirk (probably an original bug):** CastBattleSpells 0x5953b0
  computes mana bits per spell (bit 1 = k0, bit 2 = k1), but for the Amazons
  (race 3) bit 2 leads to 0x5960d0, which casts k=0, and bit 1 to 0x596210,
  which casts k=1. For the other races the bits match the casters.
- Campaign special cases in this code: mission 56 (0x38) final assault by
  player 1 after tick 81000 (0x594d40); missions 70 (0x46, player 2) and 72
  (0x48, player 1) force StartAttack's attack branch; mission 67 (0x43)
  disables the Roman spells 0x5957c0 / 0x595910.
- 0x58e390: for building types outside its jump table (type - 2 > 0x22), the
  radius stays the value of the second argument, i.e. a pointer (huge), so the
  check is always true for those types (quirk, only matters if called with such
  a type).
- 0x591c10 starts with bytes 56 8B F1 (push esi; mov esi, ecx); the dumpbin
  listing desyncs there (shows a lone F1).

### Names for callees outside this range (other agents' range; for the merge)

- 0x58a2c0: `AI::CountEnemySoldiersNear(x, y, distance, player)`: enemy (other
  team) settlers of the nine soldier types within distance (high).
- 0x58a610: `AI::CountEnemyBowmenOnOwnLandNear(x, y, distance, player)`: enemy
  bowmen (types 8, 30, 33) standing on tiles owned by player (Tile +8) within
  distance (high). Both are thiscall on 0x7e5de4.
- 0x596540 / 0x58f910: destructor / constructor of the 16-byte vector objects
  of 0x7e3688 / 0x7e7a20 (not in the inventory as separate rows, inside other
  Ghidra functions).
- 0x551210 / 0x551220: wrappers calling LoadMilitaryState / SaveMilitaryState.
- 0x7e3410 (byte): RunMilitary's reset request, cleared by T_ATTACK_AGENT::vf08
  after the call; 0x7e7cc8 / 0x7e7cc9: set by LoadMilitaryState (0x7e7cc9 makes
  the next reset a no-op and is cleared by it); 0x7e37c8: the message queue of
  the current military step.
- Table 0x6590e4: per settler type the maximum health (compared with Settler +3
  in 0x595fd0).

## ai4 notes: 0x596330..0x5a0000 (structural findings)

### Original function names from DebugLog strings

- 0x599a30 = `CountResourceInOwnersLand` (string 0x733070 "RAINER - DEFAULT IN
  SWITCH CountResourceInOwnersLand", its switch default).
- 0x599f50 = `CountPlayersResourcesAround` (string 0x7330a8).
- 0x572fc0 (not in this range) prints "FORCEPRODUCTION AN / AUS player: %u,
  ecosector %u" (0x7325f0 / 0x732620): the AI's "force production" switch
  per area ("ecosector" = economy area); 0x572fa0 reads that flag (node
  +0x18 of the node found by 0x573be0). Suggested names:
  `AI::SetForceProduction` / `AI::IsForceProduction`.

### AI state in savegames

- 0x596380 `AI::LoadState` / 0x596330 `AI::SaveState` (wrappers 0x551210 /
  0x551220 should become e.g. `CComputerPlayer::LoadAiState` /
  `SaveAiState`; 0x551210 is "Logic_sub_551210", 0x551220
  "SaveGame_sub_551220"). Chunks: 0x15 = g_aiGroupOrders 0x7e37d0 (0x20f8
  bytes), 0x16 = 0x7e5ef8 (0x1ad8 bytes, the AiAgentData/census object
  that AI::ResetAgentData resets), 0x17 = g_aiArmyState 0x7e58c8 (0x51c).
  This answers the open question in re/computer_players.md ("probably
  loading/saving the AI state, G"): confirmed, R.
- So SaveGame_sub_4a66d0 = write chunk (id, 0, size, data),
  SaveGame_sub_4a60f0 = find chunk (id, 0) -> pointer (data at +8) or 0,
  SaveGame_sub_4a6310 = release chunk (id, 0). Worth naming in the
  SaveGames batch.
- On load the AI also empties 20 vectors at 0x7e3688 (stride 0x10, 8-byte
  elements; begin +4 = 0x7e368c, end +8 = 0x7e3690) and g_aiCatapultVectors
  0x7e7a24, and zeroes 0x7e79d0[20] and 0x7e40b8[20] (only when loading a
  saved state). Bytes 0x7e7cc8 / 0x7e7cc9 are set to 1 (0x7e7cc9 is set to
  0 before the fresh-game initialisation: probably an "AI state valid"
  flag pair). 0x596540 (inside the listing of 0x596380, not an inventory
  row?) is the destructor of those 8-byte-element vectors (pushed to
  atexit by 0x58f8c0..0x58f970).
- 0x5935b0's vector growth uses std::_Construct 0x596650 (8-byte element).

### Land evaluation grid cell (AiGridCell, 0xc4 bytes, one per 8x8 tiles)

Per-goods bytes and sums, from CountPlayersResourcesAround 0x599f50,
DiscountExploitedResources 0x59bc20, FindFisheryWorkArea 0x59b780,
FindWaterworksWorkArea 0x59ba90, IsFarmLand 0x59a720 (all R):

| Offset | Meaning |
|---|---|
| +0x00 | flags (EvaluateCell 0x5540f0, V): 1 has land, 2 no water tiles, 4 has water, 8 no land tiles (starts as 2 / 8); bit 8 tested by 0x59c200 |
| +0x02 | stream tiles (terrain class 0x60; goods 26, water for the waterworks), at most 250 |
| +0x03 | stone present (value03, summed into +0x10) |
| +0x04 | trees present (value04, summed into +0x0f) |
| +0x05 | iron (summed into +0x12) |
| +0x06 | coal (+0x14) |
| +0x07 | gold (+0x16) |
| +0x08 | sulfur (+0x18) |
| +0x09 | gems (+0x1a) |
| +0x0a | fish (+0x1c) |
| +0x0b | grain land (percent +0x1e) |
| +0x0c | wine land (percent +0x1f) |
| +0x0d | rice land (percent +0x20; IsFarmLand reads +0x0d itself for race 2) |
| +0x0f | trees around (sum) |
| +0x10 | stones around (word) |
| +0x12..+0x1c | iron, coal, gold, sulfur, gems, fish around (words) |

So `resources[6]` (was `values05`) in ai_layout.h are iron, coal, gold,
sulfur, gems, fish and `resources_around[6]` (was `sums12`) their 3x3 sums
(field names since session 54; how EvaluateCell computes each byte:
ai_evaland_cells.cpp). 576 in the percent formula = 9 cells x 64
tiles (the 3x3 neighbourhood).

### Tile terrain classes used by the AI (CountResourceInOwnersLand)

- terrain class 0x10 (high nibble): grain land when flags & 9 == 0 and
  CGameData::TileSlope <= 7; wine land when slope >= 7 (vineyards on
  slopes). Probably grass/meadow. terrain == 0x15: rice land (flat).
- terrain class 0x60: counts as "water" for goods 26 (waterworks); maybe
  river terrain (G). Not yet in game/enums/terrain.h.
- Resource kinds (Tile::resources high nibble): 0x10 coal, 0x20 iron,
  0x30 gold, 0x40 gems, 0x50 sulfur (R, from the goods mapping).

### Table 0x732804 (per building type, 4 bytes)

{u16 tool goods, u8 worker settler type, u8 0}: e.g. woodcutter 2 ->
axe 14, settler 3; stonecutter 3 -> pickaxe 15, 4; sawmill 4 -> saw 16, 6;
mines -> pickaxe, 10; smiths 12/13 -> hammer 13, 11; farm 24 -> scythe 32,
16; fishery 25 -> fishing rod 17, 17; others (no tool) have tool 0 and
their worker type. Used by AI::OrderMissingTools 0x59bf70. Suggested
global name `g_aiBuildingToolAndWorker` (tables.h / globals.csv).

### Other structure

- `AI::WrappedPos` (our name, 0x597070..0x597300): {s16 x, s16 y, u16
  width, u16 height, dword +8, dword +0xc} with wrap-around stepping;
  direction order 0 x+1, 1 x+1 y+1, 2 y+1, 3 x-1, 4 x-1 y-1, 5 y-1 (also
  used by AI::AreaInDirection 0x599760). Users: MakeBuildOrders 0x56cd90,
  0x571c40, 0x571fa0, 0x5763a0, HOUSE_REQUEST_AGENT::vf03.
- The work-area finders are dispatched by 0x573890 (switch on building
  type, table 0x573bb8: 24 farm -> 0x59ac50, 25 fishery -> 0x59b780, 14
  vineyard -> 0x59b430, 36 rice farm -> 0x59b0a0, 30 waterworks ->
  0x59ba90; then SetWorkArea 0x58e220). CCentrumCheckAgent's run 0x560e00
  handles woodcutters / stonecutters (0x59a260..0x59a8d0) and waterworks.
  So 0x573890 is probably "AI::PlaceWorkArea" / "SetupNewBuilding" and
  0x58e390 a check "an order of this building type is already planned at
  (x, y)" (called with type 24 / 36 / 14 / 2 / 3).
- 0x573be0 (unnamed, compGlobs range): finds the AiPlayerData node of
  (player, area) in g_aiUnk7e3134 (CEvaLand) lists[player] (node +8 =
  area). g_aiUnk7e3134 is the CEvaLand pointer used everywhere here
  (AI::CEvaLand_GridCell is called on it): suggest renaming the global to
  `g_aiEvaLandCurrent` or similar after checking g_aiEvaLand 0x7e056c.
- AiPlayerData node +0x348: a log threshold (wood wanted): foresters are
  switched on while logs / 5 < it (0x59c0f0).
- MSVC6 std::_Tree instances: this file's map<u8, pos, greater<u8>> uses
  nil node 0x7e80a8 / refcount 0x7e80ac; Cevaland's multimap
  (0x55a970 ctor, 0x559270 insert, 0x5593a0 erase, 0x559170 dtor) uses
  0x7e0570 / 0x7e0574. Their value is {u8 score; u32 position (x, y
  words)} at node +0xc / +0xe; AI::Pair5 0x59c5a0 (dword, byte) may be a
  related pair constructor. _Tree layout: +0 comparator, +4 head, +8
  _Multi, +0xc size.
- Bug: AI::FewUnownedTilesNear 0x59c2c0 uses cell coordinates as tile
  coordinates and compares a count of at most 9 against 19, so it always
  returns 1 (behaviour to keep in a reimplementation).
- DiscountExploitedResources 0x59bc20 only discounts when the work point
  shares x or y with the building position (probably meant "work area not
  moved"; for mines always true).

### Object-file layout

The "Software\BlueByte\Siedler3\1.0" header-string initializer blocks
start new header inclusions at 0x596670, 0x5973e0, 0x59d0c0, 0x59dcf0,
0x59e800, 0x59f2d0, 0x59ff20; the survey's single unit 71
(0x599510..0x5a0960) therefore covers several object files. 0x59e6f0..
0x59e7d0 (debug output, used only by Credits code) and 0x59f210..0x59f2b0
(the cmy::impuls timer) are Credits code, not AI: the AI code ends at
0x59e0a0 (last AI function 0x59dcc0, then the initializer block
0x59dcf0..0x59e6b0). re/computer_players.md's range end 0x59fcd0 and the
source_files estimate (CompMapWorkerAgent to 0x59e7d0) should end before
0x59e6f0.

### Categories

The 18 `StaticInit_7e....` rows (0x596760, 0x5968c0, 0x596a20, 0x5974d0,
0x597630, 0x597790, 0x59d1b0, 0x59d310, 0x59d470, 0x59dde0, 0x59df40,
0x59e0a0, 0x59e8f0, 0x59ea50, 0x59ebb0, 0x59f3c0, 0x59f520, 0x59f680)
should move to category `compiler`, status identified, like their
StaticDtorReg neighbours. The std::_Tree rows and 0x596650 belong in
`stl`. The same 105-line "\General" / "\Index Server" / "\Patches"
initializers exist all over the binary (e.g. 0x401100, 0x401260,
0x4013c0, 0x4025b0... named Unknown_sub_*), a mechanical rename
candidate for the survey tool.

