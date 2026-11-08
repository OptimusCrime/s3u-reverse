# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Replacing simulation functions, bottom up, gated by the emulator tests.**

Per function: reimplement; in-process differential test (`s3re_launcher
--test`); emulator spec in `tools/emutest/specs.py` with
`python tools/emutest/run.py --require-coverage` (0 mismatches, all basic
blocks of the original covered). The whole-game replay
(`check_replay.py --against-original`) is a milestone check now.

Status: all 157 replacements pass the emulator suite with full block
coverage (2,000 cases each, fewer for the slow ones: the flood fills 150,
ShiftStatistics 60, the map-wide loops and table scans 150-800);
`--self-test` catches 11/11 wrong pairings. Batches 9-14 passed the
whole-game check on 2026-11-08 (call counts in the inventory); batches
15-19 haven't had one yet. Specs can set S3.EXE globals per case
(`Spec.globals`), and take ECX = a fake CLogicThread ("logicthread"
convention) and pointer arguments into scratch buffers (`scratch(n)`).

Next steps:
1. Continue with leaf functions (list below), each with an emulator spec.
2. Shadow mode (compare against the original during normal play), a faster
   game clock and savegames, for real and late-game data.
3. Optional: symbolic equivalence proofs (angr/Z3) for loop-free functions.

Done (verified: differential tests incl. mutation tests + whole-game
replay identical to the original):
- search/statistics: TileDistance, GameWorld::NextSearchGeneration,
  InitStatisticsKernel, ShiftStatistics, AddStatistic
- directions: NeighbourX/Y, Previous/NextDirection, DirectionFromDelta
- players: GameWorld::AddPlayerCounter, SubtractPlayerCounter,
  ClearPlayerListLengths
- settlers: GameWorld::CanSettlerEnterTile, SettlerGroup
- map: GameWorld::IsOnMap, IsInsideMapBorder, TileScreenX, TileScreenY

Batch 8 (2026-11-07, 20 functions; in-process + emulator tests, full
coverage; whole-game check passed, 7 of the 20 called):
- UI state (ui_state.cpp): IsUiSelectionMode, IsUiField298,
  IsUiSelectionParam1/2, IsUiSelectionMode4Field298Is2, IsGameOutcome1,
  ScreenCenterTileX/Y, PushUiFeedback
- free ids (id_allocators.cpp): FindFreeId256/4096/8192
- map areas (map_areas.cpp): AssignTileToArea, RemoveTileFromArea,
  SetTileAreaMarked, SetAreaBuildingSlotA/B
- map marks (map_marks.cpp): Set/ClearDiagonalTileByteB,
  ClearTileByte11Slice

Batch 9 (2026-11-08, 15 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet, so no call counts):
- building/settler tiles (building_tiles.cpp): RemoveBuildingFromTiles,
  LeaveBuildingSlot, MoveSettlerToBuildingTileA/B
- goods piles (goods_piles.cpp): BuildingPileHasGoods, RecountPlayerGoods,
  PileNeighbourBuildingByteE
- tables/rings/timers (world_tables.cpp): AllocBuildingExtra,
  AllocTable36700Record, QueueEvent, QueueMarkedTile, CountDownTimers,
  AddToRecentPositions
- players (player_queries.cpp): FindPlayerByField0C,
  BuildingTypeAllowedForRace

Batch 10 (2026-11-08, 5 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- map_neighbourhood.cpp: IsNeighbourhoodTerrain (0x51b450),
  AreOffsetTilesClear (0x51b3c0)
- world_tables.cpp: QueueMarkedTileTerrain5To7 (0x51ce50),
  QueueMarkedTileNoOwner (0x51d3d0)
- ui_state.cpp: ClampCamera (0x51b4d0)

Batch 11 (2026-11-08, 2 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- world_tables.cpp: ReplaceTileWord16AndQueue (0x51c560),
  ReplaceTileByte10AndQueue (0x51c820): steps of the flood fills
  0x51c6c0 / 0x51c8a0, which use the tile ring at world+0xdfdc48 as their
  queue (good next candidates; they only call these steps).

Batch 12 (2026-11-08, 18 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet). First callers of replaced
functions; our versions call our callees directly (game/replacements.h):
- flood_fills.cpp: FloodFillTileWord16 (0x51c6c0), FloodFillTileByte10
  (0x51c8a0), FloodMarkAreaTerrain5To7 (0x51cee0), FloodSetByte10MarkedTiles
  (0x51d450), MoveTileToAreaAndQueue (0x4e6430)
- map_terrain.cpp: ReplaceTerrainF3 (0x51a400), SpreadTerrainRandomly
  (0x519330), UpdateBorderFlagsAround (0x51a460, territory border bit 0x40
  of tile byte +9)
- settler_queries.cpp: SetSettlerTarget (0x503610, 45 call sites),
  CountListSettlersNear (0x50e8f0), FindNearbySettlerType29 (0x50cdb0,
  returns the last tile's settler, an original quirk)
- game_end.cpp (CLogicThread, game-end check helpers):
  CountBuildingsOfTypes15To18 (0x514a00), CountTilesOwnedBy (0x514a90),
  CountTilesOwnedByEither (0x514ae0), SumAreaGoods10PerPlayer (0x514b40)
- ui_state.cpp: CenterCameraOnTile (0x51b560), CLogicThread::PushUiFeedback
  (0x53f560); random.cpp: RandomBelow6 (0x503880)

Batch 13 (2026-11-08, 11 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- map_generation.cpp: ScatterTileByte11 (0x51ad40), ScatterTerrainNear
  (0x51aed0), ScatterObjects (0x51b010), ScatterObjectGroups (0x51b1a0):
  random scattering used by GameWorld::Create (map generation; may not
  run for maps loaded from files, so whole-game evidence may be absent)
- building_goods.cpp: BuildingInputWord2A/24 (0x4df150/0x4df220) and
  their setters (0x4df2f0/0x4df3c0): per-input-slot counters of buildings
  (input goods per type in the table at 0x6a6824)
- godly_events.cpp: HasManaForSpell (0x54b240); area_piles.cpp:
  AddTilePileToArea (0x5051b0); player_presence.cpp: IsOtherPlayerNear
  (0x510650, 16x16-tile presence blocks at world+0xde7448)
- Replacements can call the game's DebugLog with the original format
  string (game/debug_log.h), so the diagnostics stay. (Correction, batch
  18: until then those calls never reached s3re.log, because the logging
  passthrough only accepted call sites in S3.EXE; now sites in s3re.dll
  are logged too, as "s3re.dll+RVA (replacement)", with the game callers.)
- Found: tile +0xa bit 0 is "blocked" (AreOffsetTilesClear checks it);
  tile word +0x2 is a pile number when the building flag 0x20 of +0xa is
  clear; areas keep one pile list per goods type (heads at area +0xa +
  type*2), so 0x514b40 sums goods type 10 only (renamed
  SumAreaGoods10PerPlayer).

Batch 14 (2026-11-08, 9 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- flood_fills.cpp: FloodMoveArea (0x4e62c0), MergeTileWord16Regions
  (0x51c5e0), MergeTileByte10Regions (0x51ca00; returns the neighbour
  count, unlike the word version)
- building_goods.cpp: AddBuildingGoodsRequest (0x4ddf30, 49 calls from
  UpdatePhase2), RemoveBuildingFromAreaRequests (0x4def80). Input word 2A
  is the link of the area's per-goods request list (heads at area +0x52 +
  goods*2), word 24 the requested amount.
- settler_queries.cpp: PlaceSettlerNear (0x50ce80, called by
  ExecuteCommands); map_terrain.cpp: GrowTerrain (0x519470),
  UpdateAllBorderFlags (0x51a420, called by CLogicThread::Step);
  ui_state.cpp: CenterCameraOnLocalPlayer (0x51b6b0)
Next candidates (all callees verified): 0x51c0f0 (assigns word-16 region
ids around a tile, 19 offsets), 0x4ddbd0 / 0x4ddd70 (send the nearest
settler of a player list to a pile / building), 0x51c1f0, 0x51caf0,
0x50dff0, 0x505650, 0x5058c0, 0x507f60.

Batch 15 (2026-11-08, 4 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- flood_fills.cpp: AssignWord16RegionsAround (0x51c0f0)
- settler_queries.cpp: SendNearestSettlerToPile (0x4ddbd0),
  SendNearestSettlerToBuilding (0x4ddd70) (player lists of waiting
  settlers at player record +0x6e / +0x76, settlers linked +0x10/+0x12),
  RemoveSettlerFromLists (0x50dff0; also edits ten global lists in
  S3.EXE's data at 0x7ad620)
Batch 16 (2026-11-08, 1 function; whole-game check NOT run yet):
area_piles.cpp: AddGoodsToPile (0x505650, 33 call sites; EAX reproduced
step by step). Next similar ones: 0x5058c0 (probably taking goods from a
pile) and 0x507f60, both DebugLog users near it.

Batch 17 (2026-11-08, 9 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- area_piles.cpp: PileIncAmountLeaving (0x5058c0; pile byte +0x2 is
  "leaving", +0x4 "art"/kind, names from its DebugLog text)
- player_queries.cpp: LogPlayerLandResources (0x507f60; DebugLog only)
- flood_fills.cpp: SplitTileWord16Region (0x51c1f0), SplitTileByte10Region
  (0x51caf0) (a tile leaves its region; a walk around it finds cut-off
  parts, which get new ids), SplitWord16RegionsAround (0x51c080)
- settler_queries.cpp: SendNearestWorkerToBuilding (0x4db500, 27 calls
  from UpdatePhase2; worker lists at player record +0x20 + list*2)
- ui_overlays.cpp: CLogicThread::DrawOverlayLine (0x523560),
  DrawWorkAreaOverlay (0x523840; four rings by the offset table's +0x8
  value, ring number in tile +0xc)
- landscape_colours.cpp: Landscape::FindOrAddColour (0x54c940; class of
  ECX not identified, a 255-colour palette)
- DebugLog calls are now compared: the emulator records format and
  arguments of every call (tools/emutest/emu.py), in-process tests can use
  game/debug_log.h's DebugLogRecorder.
Next candidates (all callees verified): 0x5236b0 (calls DrawOverlayLine,
128 instructions), 0x4de0a0 (TileDistance, SetSettlerTarget,
PileIncAmountLeaving; 255 instructions), 0x51b6f0 (blocks a tile; needs
0x4e5d50, 330 instructions, first).

Batch 18 (2026-11-08, 12 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- area_split.cpp (new): the area life cycle. GameWorld::InitArea
  (0x4e6510; defaults per race), AllocArea (0x4e64c0), MoveAreaContents
  (0x4e54f0), MergeAreaContents (0x4e5a20), SplitAreaAtTile (0x4e5d50;
  a border walk around the removed tile finds cut-off parts), JoinAreasAtTile
  (0x4e6150), BlockTile (0x51b6f0), UnblockTile (0x51b770),
  SetOwnerOfFreeTile (0x51b5d0)
- world_tables.cpp: QueuePendingEvent (0x4e8ba0; the 12-byte ring that
  QueueEvent checks, type 4 deduplicated)
- settler_queries.cpp: FetchGoodsToTradeBuilding (0x4de0a0; probably
  market/port: best pile of a goods type in the area, nearest carrier of
  the area's list +0x12a, one of the building's six piles as destination)
- ui_overlays.cpp: CLogicThread::DrawBuildingRouteOverlay (0x5236b0;
  waypoints in the building extra record, probably a trade route)
- Area record layout written up in area_split.cpp's header (lists at +0x8,
  +0xa, +0x52, +0x9a, +0xe2, +0x12a; settings arrays).
- Tooling: DebugLog calls from s3re.dll now reach s3re.log.
Next candidates: 0x4dda10 (144 instructions) and 0x4dfa60 (76), both
calling only verified functions (UnblockTile / BlockTile); then 0x4dbb10
(needs 0x50cf50), 0x4dd6f0 (needs 0x50c9e0), 0x51b640 (needs 0x50e980 and
0x504f70), 0x4e6cd0 (needs 0x4e6e10). Many of the remaining direct callers are the
huge Update* functions, so leaves with unclear meaning are getting more
common.

Batch 19 (2026-11-09, 12 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- area_split.cpp: GameWorld::TakeBuildingExtraTiles (0x4dd6f0; blocks the
  seven tiles next to building types 0x1f/0x2f/0x30/0x32 at position +
  offset from S3.EXE 0x659624, or launches a shipyard's ship: CreateSettler
  type 0x2b/0x2c, DebugLog "Schiffsproduktion"), ReleaseBuildingExtraTiles
  (0x4dda10, the counterpart), UnblockType12BuildingTiles (0x4dfa60, per
  race), ClearAreaGoodsSettings1D2 (0x4ff3c0)
- settler_records.cpp (new): CreateSettler (0x50c9e0, 37 call sites),
  SettlerStartHealth (0x510470; table 0x6590e4 is the start health per
  settler type, 250 for ships), ClearSettlerJob (0x510440),
  RestoreSettlerDirection (0x50e380); settler field layout in its header
- area_piles.cpp: PileIncAmountComing / PileDecAmountComing (0x505bc0 /
  0x505bf0, pile byte +0x3), RemovePileFromTile (0x504650)
- flood_fills.cpp: FloodAssignMarkedTiles (0x4e6cd0; area set-up flood)
Next candidates: 0x4ffb90 (7-byte list at world+0x1195dcd), 0x4ff380
(pure arithmetic, 64000 - 8000a - 4000b), 0x4fa6b0/0x4faa40 (type ids
1/55), then callers whose callees are now all verified (check with
`asm_tools.py callers` on the batch-19 functions, e.g. 0x51a580 the area
set-up, which needs 0x51d5b0/0x51d450/0x5194e0/0x51d570/0x51cee0/
0x51c040/0x51d2b0 checked). Blocked: 0x51b640, 0x504f70, 0x50e980 and
0x4dbb10 all need 0x50cf50 (920 instructions, settler removal?);
0x4e6e10 is a static initializer (std::string "S3Qota1.DAT"), not
simulation code.

How to find the next ones: leaf functions (no calls) in
0x4d0000..0x560000, listed by a small script over the dumpbin disassembly
(function starts from re/ghidra_functions.tsv + re/extra_functions.tsv;
count instructions, skip any with `call` or `fs:`). About 110 remain.
Not yet looked at: the draw-list inserts 0x4d7c90..0x4d8240 (called
only by the building drawing function 0x4a9960; ECX is probably not the
world). Skipped so far because their meaning is unclear (do them once understood):
0x4fa6b0/0x4faa40 (type ids 1/55), 0x4ffb90 (7-byte list at
world+0x1195dcd), 0x505bc0/0x505bf0 (byte counters in 14-byte records at
world+0x123885f), 0x510440/0x50e380/0x510470 (settler record fields),
0x504650, 0x4ff380, 0x4ff3c0, 0x523540 (reads an uninitialized stack
byte), 0x5437b0 and 0x51e650 (objects other than the world, class
unknown), 0x53f530 (CLogicThread, +0x84 unknown).

Next steps:
0. Run the whole-game check for batches 9, 10 and 11 (record call counts).
1. Continue with leaf functions; then move up to their callers (e.g. the
   area flood fill 0x51a580 uses FindFreeId256; 0x4e6150/0x4e62c0/
   0x4e6430/0x4e6cd0 use AssignTileToArea/RemoveTileFromArea).
2. Settle the tile record layout. Evidence so far for records starting at
   0x67444 (not 0x67440 as in world.h): the tile area word is indexed as
   (x*0x300+y+0x44d9)*24 = 0x67458 in 130 places, i.e. field +0x14 of a
   record at 0x67444; 0x67444 is the most used absolute offset (215
   uses); the y-1 neighbour accesses (0x6742c..0x67436) are fields
   +0x0..+0xa of the record before. New (batch 9): the dword at
   0x67440 is the high-water mark of the 20-byte table at 0x36700
   (AllocTable36700Record), so it is not part of the tiles; batch 9's
   files already address tiles from 0x67444. Changing world.h affects
   determinism.cpp's per-tile hashing; do it as its own step.
3. (done 2026-11-08) Replays are self-contained: seed and map in the
   recording.

## Roadmap: larger areas to tackle (decided 2026-11-08)

Ranked by value and how easy they are to verify:

1. **File formats: map loading first, then GFX and SND.** Needed anyway
   for the standalone build and the macOS port. Easy to verify: run the
   original loader and ours on the same file and compare the decoded bytes
   (no game state or timing involved). Lead: the byte-decoding loop at file
   offset 0xFE72 (keystream multiplier 0xFED42531) is probably the
   decryption for map or data files. Map loading also shows how the world
   object is filled at game start (helps settle the tile layout and world
   fields). Start: find where `.map` files are opened (string search,
   file-API imports) and follow it down.
2. **Keep climbing the simulation from the leaves**, alongside item 1.
   Strongest check (whole-game replay, per-tick hashes). Next up: the
   callers of replaced functions, e.g. the area flood fill 0x51a580 and the
   tile-area functions 0x4e6150/0x4e62c0/0x4e6430/0x4e6cd0.
3. **Watcom container library (248 `WC*` classes).** Generic containers
   with well-defined behaviour, used by all game code; understanding it
   makes the code built on it (e.g. the AI agents) much easier to read.
   Mainly a way to read other code, not a goal in itself.
4. **Rendering (DirectDraw path).** The main obstacle for Phase 2 (macOS)
   and big. Harder to verify (screenshot/framebuffer comparison instead of
   byte-exact state). After the file formats, since drawing depends on the
   decoded graphics.
5. **UI and menus (`CScreenElement`, `Menus::*`).** Easy to map via RTTI,
   but hardest to verify automatically and least important for
   correctness. Last, apart from the UI state that lives in the world.

Skip: MFC and the C runtime (standard libraries; use the real ones).

Suggested next session: whole-game check for batches 9-10, then start on
map-file loading.

Planned, not started: **reorganize our source tree after the original
files** (`re/source_files.md`). First plan the full layout: author folders
(`Dirk/`, `Volker/`, `Rainer/`) or subsystem folders with the original
file names, what to do with code in unnamed files, and a `source_file`
column in `re/functions.csv` filled from `tools/source_files.py`.

## Open questions and loose ends

- Original source files: refine the code ranges (they run from the first
  to the last voting function; unnamed files sit in the gaps and maybe
  inside ranges, e.g. `WinMain` lands in LobbyManager.cpp). Does the
  alphabetical link order of the named files predict the names of unnamed
  ones? What is `Rainer\Cevaland.cpp` ("land evaluation"?)?

- The 128-slot position list at world+0x1195dd4 is changed per logic step
  (g_recentPositionsCursor, 0x7ace60), not per tick. Does it affect the
  simulation, and is it in the determinism hash? (found in batch 9)
- Where does the game seed (`g_gameSettings+0x88`) originally come from?
  It's copied from the settings struct at `[0x7e2eb0]+0x364` by `0x567910`.
  Not traced further. Recording the seed is enough for replays.
- `CLogicThread::Step` stores the RNG state every 256 ticks into
  `g_syncRandomHistory` (`0x7dfb58`) and sends it via `0x4309c0`. This is
  probably the multiplayer desync check, more evidence for lockstep.
- `CLogicThread::Step` is called about 240 times a second, but the tick
  advances only about 15 times a second; the step decides when to tick.
  Some work runs per step, not per tick (`0x524fa0`, `0x51f110`), so its
  side effects depend on timing. Known volatile world fields: `+0x2d2e4`
  to `+0x2d2eb` and `+0x2d301` (skipped by the trace hash). Check whether
  they can ever influence the simulation.
- Tile byte 15 is a "visited" stamp shared by the simulation's searches
  (GameWorld::NextSearchGeneration) and the UI overlays. Safe as long as
  every search bumps the generation before marking; only spot-checked
  (0x4fd540 compares with 0 but runs only in OnStart).
- Byte 3 of command messages varies between runs for the same command
  (seen 3, 4, 0xa, 0xcc); possibly uninitialized like bytes 9..11.
- Bytes 9..11 of 12-byte command messages are uninitialized stack memory
  in many commands (vary between runs). Replay feeds the recorded bytes, and
  no effect on the simulation has been seen, but confirm per command id
  when decoding them.
- `ProcessEvents` (`0x524fa0`) does extra work when the game's `-record`
  flag (`0x7a800a`) is set (mouse positions scaled by `0x7acf84`, pushed
  somewhere). Not analyzed; we don't use `-record`.
- The `-record` file header value is the last ExecuteStep + 5; `-play`
  sends a type 4 message when reaching it (probably "end of movie").
- Determinism has only been tested on one 2-player map without input.
  Test other maps, computer opponents, and longer runs.

- `GameWorld::UpdatePhase2` (`0x4e05e0`): purpose unknown.
- `GameWorld::UpdateSettlers` (`0x4e9670`): the name is provisional.
- `0x550f80` is called on 20 objects at `CLogicThread+0x350` each step.
  Per-player objects (AI?) are likely; `CComputerPlayer` exists in the RTTI.
- `0x51f110(0)` is called between the world update phases when
  `g_stepCountdown` is 0. Unknown.
- Where is the `GameWorld` object allocated, and how big is it exactly?
- Constructors aren't named. `re/classes.json` lists, per vtable, the
  functions that store it (`writers`); separate constructors from
  destructors and inlined code.
- 84 virtual functions are shared by unrelated classes (linker folding); only
  `_purecall`, `DebugLog` and the empty `NoOp_retN` ones are named.
- The exact boundary between game code and the C runtime in `.text` is
  unknown (somewhere around `0x61e4ef`).
- `CThreadData`, `CTrace` and `CTextException` are categorized as game
  classes by default; they might be library code.
- REA can't create functions in Ghidra, so the 973 functions in
  `re/extra_functions.tsv` can't be decompiled. Use `dumpbin` (see
  `CLAUDE.md`).
- The user hasn't yet played a game with the hooks active (currently
  `_Eos` and `GameWorld::Random` replaced) as an integration check.
- **Replays need the game seed they were recorded with**, which recordings
  now store (fixed 2026-11-08, see session log). `recordings/scripted-seed127.bin` was
  recorded with game seed 1234 (check_replay's default; "127" is the
  synthetic-input seed). Replaying it with `--seed 127` gives a different
  game (player races 1/2 instead of 2/3: OnStart draws a random race with
  GameWorld::Random() % 4) into which the recorded commands are fed, and
  the original game crashes after ~800-2,300 ticks: a recorded command
  builds building type 0x24 (an Asian-only building) for the Egyptian
  player, and drawing it looks up a shadow sprite that doesn't exist
  (0x4a9960 -> 0x5fabb0 "GFX: ShadowAnimation not found 0:0", whose bounds
  check only calls DebugLog, then a garbage pointer is dereferenced in
  0x5fcdc0 at 0x5fcdfb). Fixed: recordings carry seed and map now.

## Session log

### 2026-11-09 (26): batch 19

- Batch 19 (12 functions, list under "Current focus"): in-process 239/239
  (19 new incl. 7 mutation tests); emulator specs 12/12 with full
  coverage; full emulator suite 157/157, self-test 11/11.
- Found: SettlerStartHealth's EAX upper half differs by path (race 3:
  type * 4, i.e. 0; else settler << 6); the emulator caught my first
  version.
- Test pitfall: building positions are words, and the original adds the
  extra-tile offset to the zero-extended word, so a position that would
  be negative (cy < dy) reads tiles far off the map: tests keep cy >= 8.
- Test pitfall: ReleaseBuildingExtraTiles does not check for an unset ship
  position (0xffff) the way TakeBuildingExtraTiles does; tests only use
  set ones for it.
- Test pitfall: a crashing in-process test leaks its WorldPair (no
  destructors after the SEH catch), and later tests with big worlds then
  crash too (address space); look at the first CRASH only.
- Several splits in one call need connected byte-10 regions (the
  extra-tile tests use one region over the whole map); the release tests
  block the tiles with the original BlockTile first, so the states are the
  game's own.

### 2026-11-08 (25): batch 18

- Batch 18 (12 functions, list under "Current focus"): in-process 220
  tests all pass (24 new incl. 12 mutation tests); emulator specs 12/12
  with full coverage; full emulator suite 145/145 (0 mismatches, full
  coverage), self-test 11/11.
- Tooling: DebugLog from our DLL is logged ("DebugLog from s3re.dll+RVA
  (replacement)", plus the game callers each time); the batch-13 note is
  corrected.
- Test pitfall: MergeAreaContents leaves the merged area's list heads as
  they were, so joining a second, disconnected piece of the same area
  links its lists twice and a later list walk loops for ever (in the
  original too). Area tests for joins use connected stripe areas
  (SetUpAreaMap stripes / area_map(stripes=True)). Likewise, every list of
  an area that a function walks must be set up (random lists cycle).
- Test pitfall: piles must lie on map tiles whose area record is inside
  the test world (PileIncAmountLeaving unlinks from the tile's area).

### 2026-11-08 (24): batch 17

- Batch 17 (9 functions, list under "Current focus"): in-process 196/196
  (18 new incl. 7 mutation tests); emulator specs with full coverage.
- New check: DebugLog arguments are compared (emulator: hook on 0x428720
  counts the format's conversions; in-process: DebugLogRecorder).
- Found: DebugLog calls made from our DLL are captured but never written
  to s3re.log, because LogMessage only logs call sites inside S3.EXE's
  code (the batch-13 claim that replacements keep the diagnostics is
  wrong in practice; fix by also accepting sites in s3re.dll).
- Test pitfall: region splits free an id when a tile had no neighbour in
  its region; with random ids shared by separate stretches, the freed id
  is handed out again while in use and the flood fill loops for ever
  (in the original too). Tests for chains of splits label each connected
  stretch with its own id, as in the game.
- The emulator calls the spec's argument generators in order, so they can
  share state (line_args in specs.py correlates end points).

### 2026-11-08 (23): batch 16, full emulator suite

- Batch 16: AddGoodsToPile (0x505650).
- Full emulator suite: 124/124, 0 mismatches, full coverage; self-test
  11/11. It first failed because a file-local helper was named NeighbourX
  like a replacement: the emulator finds our functions by name in
  s3re.map, so helper names must not collide with replacement names.

### 2026-11-08 (22): whole-game check for batches 12-14, batch 15

- Whole-game check (scripted-seed127.bin, 3,000 ticks): REPLAY OK with
  all 119 replacements, 51 called; counts in the inventory (e.g.
  RandomBelow6 128,668, AddTilePileToArea 6,371, SetSettlerTarget 3,342,
  AddBuildingGoodsRequest 250, UpdateAllBorderFlags 1). The map
  generation scatterers were not called (the map comes from a file).
- Call counts only count calls that go through the hook, i.e. from
  original code. Callees that our replacements call directly (e.g.
  IsInsideMapBorder from UpdateBorderFlagsAround, the flood-fill steps)
  now show fewer or no calls; their earlier evidence still stands.
- Batch 15 (4 functions, list under "Current focus"): in-process 177/177,
  emulator 4/4 full coverage.
- Test pitfall: region merges look one tile beyond the tiles they fill;
  near the map edge the original floods off the map (real maps have a
  border), so test tiles must stay 3 tiles inside.

### 2026-11-08 (21): batch 14 (9 functions)

- Region merges, the area flood fill, goods-request list add/remove,
  settler placement, terrain growth, the all-tiles border pass, camera on
  the local player. In-process 171/171 (11 new incl. 2 mutation tests);
  emulator 9/9 with full coverage.
- MergeTileByte10Regions returns the neighbour count in EAX, its word-16
  twin returns 0 (different stack slot loaded last); the in-process test
  caught my first version.

### 2026-11-08 (20): batch 13 (11 functions)

- Map generation scatterers, building input slots, god-event mana check,
  pile-to-area link, player presence query (list under "Current focus").
  In-process 160/160 (15 new incl. 4 mutation tests); emulator specs 11/11
  with full coverage.
- First ScatterObjects version blocked tile +0xe instead of +0xa (the old
  0x67440-based offset); the in-process test caught it, after the test
  landscape was made patchy enough for placements to happen (uniform
  8x8 terrain patches, few objects and blocked tiles).

### 2026-11-08 (19): whole-game check for batches 9-11, batch 12 (18 functions)

- Whole-game check (scripted-seed127.bin, 3,000 ticks, original vs ours):
  REPLAY OK, 40 of 81 replacements called. Call counts written into the
  inventory notes of batches 9-11 (12 of their 22 functions were never
  called; e.g. QueueMarkedTileTerrain5To7 486,138 calls,
  ReplaceTileWord16AndQueue 900,054, QueueMarkedTileNoOwner 314,910).
- Batch 12: 18 functions, mostly callers of already replaced ones (flood
  fills over the tile ring, terrain spreading, border flags, settler
  targets, game-end counts). In-process 145/145 (23 new tests incl. 6
  mutation tests); emulator 18/18 full coverage; self-test 11/11.
- emutest: "logicthread" convention (fake CLogicThread at world+0x1f80000
  whose +0x3a0 is the world) and `scratch(n)` pointer arguments
  (world+0x1f90000 + n*0x1000), both inside the lazily random, compared
  world area.
- Pitfalls: `near` is a Windows macro (don't use it as a name). The
  flood fills loop for ever when the new value equals the old one; the
  first in-process test set-up could produce that (start tile not `old`)
  and hung the test run. Big emulator flood cases (101x101 map, needed to
  cover the ring wrap) take ~14 s each; keep them rare.

### 2026-11-08 (18): batch 11 (2 leaf functions, short session)

- Replaced 0x51c560 and 0x51c820 (conditional tile-field replace + push
  onto the tile ring; flood-fill steps). In-process tests incl. a mutation
  test, emulator specs with 4/4 blocks covered. No game run this session.
- Next: whole-game check for batches 9-11, then the flood fills
  0x51c6c0/0x51c8a0 themselves, or map-file loading.

### 2026-11-08 (17): original source files

- Checked for debug info: none (no debug directory, no PDB or map file).
- Found 57 `E:\Siedler3\...` source paths and mapped them to code ranges
  through the strings stored after each path (`tools/source_files.py`,
  `re/source_files.csv`). About 88% of string references agree with one
  monotone file order. Known functions land in the expected files (e.g.
  ExecuteCommands in LogicHandleNet.cpp, CGameThread::Main in
  Gamebase.cpp).
- Wrote `re/source_files.md`: method, limits, link order, translations of
  the folder/file names and of the German debug texts.
- Pitfalls found on the way: count only string literals (globals are
  shared and pulled LogicInit.cpp over half the logic); one vote per
  function; Latin-1 strings, but don't glue a preceding 0xFFFFFFFF global
  onto a string.
- Reorganizing our sources after the original layout is postponed until
  the full layout is planned.

### 2026-11-08 (16): batch 10 (5 leaf functions, short session)

- Replaced IsNeighbourhoodTerrain, AreOffsetTilesClear,
  QueueMarkedTileTerrain5To7, QueueMarkedTileNoOwner, ClampCamera (all
  thiscall on the world). In-process tests pass (9 new incl. 3 mutation
  tests); emulator specs 0 mismatches, all blocks covered (2,000 cases
  each). Full emulator suite and whole-game check not rerun.
- Found that 0x514a00/0x514a90/0x514ae0/0x514b40 take CLogicThread in ECX;
  left for when the emulator supports that (see Current focus).

### 2026-11-08 (15): batch 9 (15 leaf functions)

- Replaced 15 world functions (list under "Current focus"). New files:
  game/building_tiles.cpp, goods_piles.cpp, world_tables.cpp,
  player_queries.cpp. In-process tests 111/111 (19 new incl. 4 mutation
  tests); emulator suite 74/74 with full block coverage; self-test 9/9.
- Record layouts learned: settlers are 0x40-byte records from 0x12a5e60
  (+0x4/+0x6 position, +0x8 state, +0x14 flags, +0x22 building); buildings
  have two tiles (+0x2/+0x4 "A", +0xa/+0xc "B") and three pile slots
  (+0x10..+0x14); goods piles are 14-byte records at 0x123885c (count at
  0x1238858; +0 goods type, +1 amount, +4 kind, +6/+8 position); tile
  +0x0 settler word, +0x2 building word, +0x8 owner, +0xa flags (0x20 =
  building). The player counters at 0x11944b8 are goods counts per type
  (RecountPlayerGoods rebuilds them at start).
- New global g_recentPositionsCursor (0x7ace60): advanced per logic
  *step*, not per tick, and it decides which slot AddToRecentPositions
  uses. If the list influences the simulation, that is timing-dependent;
  check whether world+0x1195dd4 is in the determinism hash and whether it
  matters (open question).
- First QueueEvent version was wrong (the duplicate scan stops at the
  first match); the in-process test caught it.
- Next: whole-game check (`check_replay.py --against-original`) to get
  call counts for batch 9 (at the next milestone), then continue.

### 2026-11-08 (14): progress treemap

- Added `tools/progress.py`: a treemap of all 9,836 known functions, sized
  by code bytes (distance to the next function or funclet start) and
  coloured by inventory status, grouped by category and class (unidentified
  functions by 64 KB address range). Writes `docs/progress.svg` (in the
  README) and `docs/progress.html` (stats, hover details, history per
  inventory commit). Standard library only.
- Then made `docs/progress.html` a static, hand-written page that draws the
  treemap in the browser (hover, click to zoom, history chart) from
  `docs/progress-data.js`. The script only writes the data (a `.js` file
  rather than JSON so the page also works from `file://`) and the SVG.
- Current numbers: 60 functions verified, 5,232 of 2,258,245 bytes (0.23%).
- The workflow step 7 in CLAUDE.md now includes regenerating it.

### 2026-11-08 (13): self-contained recordings

- Recording format version 2 (`src/common/recording_format.h`, shared by
  launcher and DLL): header with game seed, settings (bit 0: random races
  include the Amazons) and the -map argument. The DLL writes it when
  recording (seed from the SeedRandom hook, `game::LastSeed`) and, when
  replaying, stops the game if seed, setting or map differ. The launcher
  takes seed and map from the recording; a conflicting --seed/-map or a
  version 1 recording without --seed is refused (checked by hand: all
  three refused).
- `tools/command_log.py`: reads both versions, `upgrade` converts version
  1. `recordings/scripted-seed127.bin` upgraded (seed 1234; the version 1
  original is kept as `scripted-seed127.v1.bin`).
- `check_replay.py` passes --seed/--map only when recording (or when given
  explicitly).
- Checked with the game: a fresh 300-tick record/replay round trip
  (header written and applied, mutation detected) REPLAY OK; the upgraded
  scripted recording, replayed without --seed, original vs our code:
  traces identical over 1,000 ticks (compared by hand, see next point).
- Loose end: when the game's clean shutdown hangs ("did not finish within
  15 s"), the logic thread keeps stepping and the trace reopens as a second
  file; check_replay.py then reports "run failed (2 new trace files)"
  although the run was fine. Make run_game take the first new trace.

### 2026-11-07 (12): batch 8 (20 leaf functions)

- Replaced 20 functions (UI state queries, screen-centre tile, free-id
  search, tile areas, diagonal/slice tile marks; list under "Current
  focus"). New files: game/ui_state.cpp, id_allocators.cpp,
  map_areas.cpp, map_marks.cpp, globals.h. In-process tests 92/92 pass
  (27 new incl. 4 mutation tests); emulator suite 59/59 with full block
  coverage; self-test 9/9.
- emutest: `Spec.globals` sets S3.EXE globals per case (for
  0x730f18/0x730f1c/0x730f64; added to re/globals.csv).
- Tile layout: evidence that tile records start at 0x67444 (see next
  steps); world.h not changed yet.
- Whole-game check: first attempts crashed in the reference run (original
  code). Cause: wrong seed for the recording (see open questions), not
  our code. Found with two new diagnostics: a crash logger
  (src/s3re/crash_log.cpp: vectored exception handler, logs registers,
  memory and callers of access violations in game code) and a DebugLog
  passthrough (src/s3re/debug_log.cpp: the game's 688 debug messages,
  silent in the release build, now go to s3re.log, 5 per call site). The
  seed hook also logs the "random race includes Amazons" setting.
- With the right seed (1234): `check_replay.py --recording
  recordings/scripted-seed127.bin --against-original --ticks 3000` REPLAY
  OK with all 59 replacements; 30 of 59 called. Of batch 8: AssignTileToArea
  (8,179 calls), RemoveTileFromArea (2,092), SetTileAreaMarked (81,023),
  FindFreeId256/4096/8192 (4/7/83), ClearTileByte11Slice (448); the other
  13 (UI queries, building slots, diagonals) were not called.

### 2026-11-07 (11): emulator test harness

- tools/emutest: Unicorn-based differential harness (emu.py: PE loading,
  lazy random world, write undo, block coverage; run.py: comparison and
  report; specs.py: one spec per replacement). Linker map s3re.map added
  to the build for symbol lookup. Packages pinned in tools/requirements.txt.
- All 39 replacements match with full block coverage; the first run found
  uncovered branches in 5 functions (fixed with targeted presets).

### 2026-11-07 (10): batches 5-7, call coverage, testing strategy

- Batches 5-7 (tile neighbourhood checks, counting functions, building
  request flags): all differential tests pass (68/68).
- Added call counting to replacement hooks. The coverage run showed that
  16 of 39 replacements were never called in the scripted 3,000-tick game
  (e.g. NextSearchGeneration, all Count* functions, the SetBuildingFlag*
  functions, TileScreenX/Y), so the earlier "whole-game check passed"
  notes were wrong for them; the inventory notes now give call counts.
- Decided with the user: the whole-game replay is too slow and too
  coverage-limited as a per-batch gate. Next: an emulator-based
  differential harness (Unicorn) that runs the original bytes from S3.EXE
  and ours from s3re.dll, measures branch coverage of the original and
  checks calling-convention details; later symbolic equivalence proofs
  (angr/Z3) for loop-free functions, a shadow mode for real game data, a
  faster game clock and savegames for late-game states.

### 2026-11-07 (9): more leaf functions

- 14 more functions replaced in three batches (directions, player counters,
  settler helpers, map coordinates); each batch passed all tests and the
  whole-game check against the original. Added test/world_buffer.h.
- Some callers only overwrite AL after a call, so for those functions the
  exact EAX of the original is reproduced and tested.
- Don't rebuild while a game run is going: the link fails because the DLL
  is in use (and a rebuild between the two runs of --against-original
  would mix builds).

### 2026-11-07 (8): first simulation functions replaced

- Reimplemented and verified 5 world functions (src/s3re/game/
  world_search.cpp, statistics.cpp): 10 new differential tests incl.
  mutation tests, all 19 tests pass. Found that the statistics history
  (20 players x 36 categories x 256 samples) sits at world+0x38.
- Added original mode (`s3re_launcher --original`, `S3RE_HOOK_ALWAYS` for
  hooks that are also tooling) and `check_replay.py --against-original`.
  A scripted 3,000-tick game replayed identically with the original code
  and with ours.

### 2026-11-07 (7): scripted player input, UI state in the world

- `tools/synthetic_input.py`: random play through SendInput (verified with
  screenshots: 640x480 full screen; the build menu opens, building-site
  markers are detected in a screenshot and clicked). It produces real
  player commands.
- Replays with player input keep the RNG identical, but the world hashes
  differed. Dump diffs (`--dump-tick` now takes several ticks and dumps
  the whole hashed world; `tools/diff_world_dumps.py`) found only UI state
  inside the world: camera, menu mode/sub-pages, UI refresh flag, cursor
  mode, search generation, overlay marks (tile bytes 15/16), the tile
  redraw bit (byte 13 bit 7), the footprint check's scratch grids, a UI
  feedback list, the statistics chart. All excluded from the hashes with
  code evidence (trace format v4).
- `check_replay.py`: `--synthetic-input SEED`, `--dump-every N`; the
  mutation check now removes a player command when there is one.

### 2026-11-07 (6): commands, lockstep, record/replay

- The game's `-record` writes `Siedler3.evt` (`CEventFile`): 8-byte header
  (magic `0x47832443`, last step + 5), then 20-byte announce packets. Even
  without human input it holds about 50 commands per 1,000 ticks (computer
  player, sync checks). `-play` adds the file's commands to the live ones,
  so they run twice and the replay diverges (from tick 32).
- Single player runs a loopback network thread, `CNetBounceThread`.
  Traced the full command path: logic pushes 0x21 commands and 0x20 step
  messages on one queue; the bounce thread returns them in order; the
  executor `CLogicThread::ExecuteCommands` (`0x535990`) runs them.
- Built our own recorder/replayer on `MessageQueue::Push` (`0x4309c0`):
  `src/s3re/replay/command_log.cpp`, launcher `--record-commands`,
  `--replay-commands`, `--log-commands`; `tools/command_log.py`,
  `tools/check_replay.py`. Result: replay identical over 1,000 ticks
  (50/50 live commands matched); removing one command diverges from tick 43.
- `--exit-after-ticks` now shuts down through WM_CLOSE (needed so `-record`
  files are completed); about 1 s to exit.
- Pass-through hook on `CNetThread::OnNetAnnounce` (multiplayer only; it's
  never called in single player).

### 2026-11-07 (5): unattended runs, determinism confirmed

- The game has built-in command-line options (German help text at
  `0x6960e4`): `-map <file>` starts a map directly, `-record` / `-play`
  record and replay a "movie" (`Siedler3.evt`), `-demo` skips menus (but
  crashes with `-map`), `-debug`, `-netmon`, `-version`, `-help`.
- Added `--exit-after-ticks`, `--timeout` and `--dump-tick` to the
  launcher, and `tools/check_determinism.py` (unattended runs + compare).
- Diffing header dumps found only timing-dependent scratch fields; after
  skipping them, runs are identical (1,000 and 2,000 tick runs).

### 2026-11-07 (4): first determinism runs, RNG seeding

- First real traces: 384x384 map, about 15 ticks/s, the RNG changes every
  tick. Two runs without input differ from tick 1 (different seed).
- Disassembled all of `S3.EXE` with dumpbin to grep for the RNG state
  offsets (`0x2d2dc..0x2d2e0`). Found `GameWorld::SeedRandom` (`0x4feb40`),
  its two callers in `CLogicThread::OnStart`, the savegame-load copy of the
  RNG state (`0x509xxx`), and the sync check in `CLogicThread::Step`.
- Reimplemented `GameWorld::SeedRandom` (verified, 100,000 cases); added
  `--seed` to the launcher and step-count diagnostics to `s3re.log`.

### 2026-11-07 (3): determinism trace

- Pass-through hook on `CLogicThread::Step` (`src/s3re/game/logic_thread.cpp`)
  feeds `src/s3re/trace/determinism.cpp`. After each step that changes them,
  it logs the tick and RNG state; every 32 ticks it logs hashes of the world
  header (`0..0x67440`), the map tiles (rows of `mapSize` tiles) and the
  rest (up to 8 MB after the tile array, clamped to committed memory).
- Added `tools/compare_traces.py` (checked on synthetic traces).
- Map tile layout from `UpdateEnvironment`: tile (x, y) at world
  `+0x67440 + (x*0x300 + y)*24` (record start 0x67440 or 0x67444, not
  settled).

### 2026-11-07 (2): game loop and RNG

- Traced `WinMain` → `CConsoleThread` (the application object) →
  `Win32::CThread::Execute` → the OnStart/Main/OnExit slots. The main thread
  is a UI/message loop; the simulation runs in `CLogicThread`.
- `DirectX::CGameThread::Main` is the shared game-thread loop; slot 9
  (`Step`) is pure virtual there. `CLogicThread::Step` (`0x53db30`, missed by
  Ghidra, disassembled with dumpbin) is the simulation step. It calls three
  world updates and a per-object update on 20 objects.
- Found the simulation RNG `GameWorld::Random` (`0x4feb70`) and the tick
  counter `g_gameTick` (`0x7dfd48`). Reimplemented the RNG and verified it
  against the original over 1,000,000 calls (with a mutation test).
- Added `re/globals.csv` for global variables, `src/s3re/game/world.h` for
  world offsets, and a "Runtime structure" section in `CLAUDE.md`.
- Inventory: 1,495 rows, 15 understood, 2 verified.

### 2026-11-07 (1): project setup, tooling, class structure

- Set up the repo (gitignore excludes the game install; LF line endings via
  `.gitattributes`), GitHub remote `origin`, branch `main`.
- Confirmed REA works (Ghidra 12.1.4 backend). Ghidra's analysis of `S3.EXE`
  takes about 2–3 minutes per session.
- Analyzed the binaries: `S3.EXE` and `S3_multi.EXE` differ in 7 bytes (GOG
  serial-number patches). MSVC 6.0, C++, fixed base `0x400000`.
- Chose the approach: incremental replacement via an injected DLL, with
  differential testing (see `CLAUDE.md`).
- Built `s3re_launcher.exe` + `s3re.dll` (Detours). Verified in a real run:
  the `WinMain` pass-through hook works and the game exits cleanly.
- Built the function inventory (`re/functions.csv`, `tools/inventory.py`)
  and the differential test harness (`s3re_launcher --test`). First verified
  function: `std::string::_Eos` (`0x401f60`), with 100,000 random cases.
- Recovered the class structure from RTTI (`tools/rtti.py`): 586 classes,
  1,376 virtual functions, 210 deleting destructors, 649 thunks; 973
  functions Ghidra missed. The inventory went from 5 to 1,486 named
  functions out of 9,836 known.
