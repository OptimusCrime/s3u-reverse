# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**More reimplementation coverage, verification may lag (decided
2026-11-09).** The user wants the number of reimplemented functions to
grow faster; verification can follow later or when a caller needs it.
Convention: a reimplementation without evidence uses `S3RE_HOOK_UNVERIFIED`,
inventory status `reimplemented`, hook `unverified`; it is only installed
with `s3re_launcher --unverified`, so default runs and whole-game checks
stay on verified code (CLAUDE.md, "Unverified reimplementations"). Cheap
evidence (an emulator spec) is still written whenever it is quick.

Evaluation (2026-11-09; numbers from `frontier.py`, the inventory and the
disassembly; "READY" = not done, every direct callee done, all game code
0x401000..0x61e4ef, strict view without --library-done):

1. **MSVC6 std::string (STL, 63 functions, ~5,000 instructions)** - the
   single biggest blocker: every game file uses it (~2,400 destructor and
   ~2,000 constructor call sites). Before batch 21, READY was 602; with
   the 13 core members of batch 21 done it is 1,029. All of the STL done
   would give about 1,080 READY game functions (vs 672 before, game
   category only). Needed anyway (game objects embed the layout; the
   standalone build needs it). Easy to verify (emulator, fake heap).
   **Done: core in batch 21, the rest in batch 22 (39 functions incl. game
   classes with string members and logic_error/out_of_range). Only the
   throwing functions remain (bitset::_Xran x3, _Doraise x2).**
2. **Trivial shared stubs that Detours can't hook.** `NoOp` 0x59f200 (a
   lone `ret`, 9 vtable slots) alone blocks 92 NEAR functions; the
   `AdjustorThunk`/`JmpThunk` stubs (0x429d90, 0x429db0, ...) 25 each.
   Too short for a 5-byte detour, and nothing to reimplement. Proposal
   (user decision): let frontier.py count byte-proven trivial functions
   (lone `ret N`, a single `jmp`, `add ecx,N; jmp`) as done, recorded as
   such in the inventory (status `identified`, note "trivial: proven by its
   bytes"), not `verified`.
3. **Compiler/CRT library code: treat as done by mapping, not by hooks.**
   4,286 compiler stubs are identified (1,966 std::string static
   initializers, 2,271 atexit registrations, thunks): they are
   byte-pattern instances of `T global(args)`, and a standalone build
   recreates them as plain global definitions. CRT (506): use the real
   one (frontier already counts it as done). Not worth hooks.
4. **Simulation frontier (0x4c0000..0x560000)** - 100 READY, 124 NEAR.
   Best: 0x504420 (144 ins, 7 callers), 0x542990 (122, 6 callers),
   0x51b7f0/0x51d000 (unblock the area set-up 0x51a580), the AI helpers
   0x551970/0x5519b0 (4 each), the 13 MapEffect make functions. Blocker
   0x50cf50 (919 ins, settler removal?) unblocks 5 (0x51b640, 0x504f70,
   0x50e980, 0x4dbb10, 0x4dee20). Strongest checks (whole-game replay).
5. **File formats: savegames, Picture/GfxData.** Savegame loader 0x508fa0
   (677 ins, called by CLogicThread::OnStart) needs Dirk/SaveGames.cpp's
   chunk readers 0x4a60f0/0x4a6310 (unit 0x4a55e0..0x4a77c0, 30 SaveGame
   functions / 7,200 ins), which need Win32 file wrappers 0x60bab0/0x60bbe0/
   0x60be20 and probably a decompressor 0x4a57b0/0x4a5930/0x4a5a60 (not
   analysed), plus std::string 0x413d70. Picture (194 functions, 25,400
   ins, 79 READY; includes zlib/libpng at 0x5b2fb3..0x5b50ad) and GfxData
   (67, 7,700 ins, 29 READY) are the graphics loaders: self-contained,
   verifiable by comparing decoded bytes, needed for the macOS port.
6. **Win32/DirectX framework classes** - `Win32::CObject::~CObject`
   0x610330 (13 ins) unblocks 24, `Win32::ThrowWin32Error` 0x610de0 21,
   `CAssertmentFailed` 0x612a20 14; the UI accessor block 0x429d70..
   0x42a180 (4-13 ins each, 17-25 callers each) waits on CScreenElement
   virtuals 0x415xxx-0x418xxx. Many functions per effort, but mostly
   outside the simulation; good "coverage" batches.
7. **Watcom containers (545 classified `WC*` functions)** - template
   instantiations of a few class templates; one C++ template each would
   cover many functions, but each instantiation needs its own hook and
   test. Later, once a caller subsystem (AI, lobby) is tackled.

Next steps (for 2026-11-10, in this order):
1. (Batch 26 done 2026-11-09: exception chain incl. the data-error
   thrower 0x5ffcc0, CSurface release/destructors, the MapEffects table
   (find slot, cell, link, Add and 8 spawn functions), AI and framework
   helpers; 45 in all, see the session log.) Next coverage batch, concrete:
   - Win32::CAssertmentFailed 0x612a20 (180 ins) -> 0x6119b0 (48 callers,
     14 unblocks); Win32::ThrowError 0x610960 (-> 0x601280);
     Win32::ThrowWin32Error 0x610de0 (-> 0x617c80, 0x616d50).
   - Lobby: 0x603270 (-> CLMGeneric 0x601e80, 24 callers), 0x602850 (->
     0x602e20, 22 callers), 0x5ec550 (-> CAddress 0x5ecd50, 16 callers),
     0x601170 (-> 0x600e40).
   - Tooltip opener 0x418550 (needs 0x41c3e0, 0x41ca60, ThrowError, GDI
     probes SelectObject / GetTextExtentPoint32A), then 0x4183c0 and
     0x429f10 (25 callers).
   - App 0x40a580 (vector<string> resize, 23 callers): the new elements'
     allocator byte is read from the caller's stack ([esp+8] beyond the
     argument) - can't be reproduced; decide (known difference, emulator
     ignore) before doing it.
   - Simulation: the remaining MapEffects callers (0x544630, 0x5449f0,
     0x545150, 0x5453f0, 0x545730, 0x5458d0, 0x544390, 0x545c80; check
     their other callees), 0x51d2b0 (-> area set-up 0x51a580), 0x553fe0
     (-> 0x553dc0), 0x523540.
2. (Item 2 done 2026-11-09: tools/trivial.py.) Then the UI
   accessor block and Win32::CObject et al. as coverage batches, with
   `S3RE_HOOK_UNVERIFIED` where a spec is not quick.
3. (Done 2026-11-09 for batches 15-21: identical.) Whole-game check
   (`python tools/check_replay.py --recording <file> --against-original
   --ticks 3000`, then `tools/record_call_counts.py`). Batch 21 runs during start-up (all
   1,966 string initializers go through String_FromChars). Record the
   call counts in the inventory.
4. Simulation frontier (item 4) and 0x50cf50.
5. Savegame loader chain (item 5), then map/GFX loaders.
6. Older items: tile base 0x67444 + determinism.cpp fixes
   (`re/world_layout.md`), adopt `re/proposed_structs.h` gradually.

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

Batch 20 (2026-11-09, 12 functions; in-process + emulator tests, full
coverage; whole-game check NOT run yet):
- area_piles.cpp: PileDecAmountLeaving (0x505ac0; counterpart of
  PileIncAmountLeaving: a fully reserved pile is listed again),
  MakePileEmpty (0x506080; goods byte 0xff, onto the area's empty-pile
  list +0x8)
- counting.cpp: CountAreaSettlersOfClass0 (0x50e750), CountAreaSettlersOfType
  (0x50e820): settlers of the area's player standing on their own tile in
  the area
- settler_records.cpp: IsSettlerJob01Or37 (0x4fa6b0), JobAfter01Or37
  (0x4faa40) (jobs 1 -> 2, 0x37 -> 0x35, probably walking)
- building_goods.cpp: GoodsRequestPriority (0x4ff380, 64000 - 8000 amount
  - 4000 coming)
- objectives.cpp (new): IsNgoObjectiveChosen (0x4ffb90; seven "new game
  objective" ids at world+0x1195dcd)
- map_generation.cpp: ReplaceTerrainNextTo (0x519270, GameWorld::Create);
  map_terrain.cpp: TileShade (0x519720, brightness 0..127 from the
  neighbours' heights)
- map_effects.cpp (new): MapEffect::MapEffect default (0x543830) and
  20-argument (0x543880) constructors of the 0x4c-byte records of the
  world member at +0x14d2bc8
Next candidates (READY now): the 13 MapEffect "make" functions
0x543930..0x543f70 (fixed arguments + 0x543880; they pass the caller's
EDX as argument 1), 0x519670 (calls TileShade 7 times), 0x504420 (144
instructions, 7 callers), 0x51b7f0, 0x51d000 (unblocks 0x51d2b0 and so
0x51a580).

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

(The older step list that stood here is folded into "Next steps" at the
top of this section.)

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

### 2026-11-09 (34): batch 26 (45 functions: exceptions, surfaces, MapEffects, helpers)

- Batch 26, all verified (none unverified):
  - win32_exceptions.cpp (new): Win32::CException 0x611a20 (message
    string by an inlined assign; allocator byte = low byte of the hidden
    mostDerived argument), CRunTimeError 0x614c60, Lobby CDataError
    0x6003b0, Lobby_ThrowDataError 0x5ffcc0 (text + ", file " + file +
    ", line " + itoa(line), built twice, thrown as new CDataError; 102
    callers).
  - directx_surface.cpp: CSurface::Release (vf01 0x5bf2c0), ~CSurface
    0x5bf240 and its deleting destructor 0x5bf220, ~CLandscapeSurface
    0x54be70 and 0x54be40.
  - map_effects.cpp: the effect table's FindFreeSlot 0x545ee0 (DebugLog
    "EFFECT: Effect overflow" at 5000), CellOfPosition 0x545eb0 (256-pixel
    blocks), LinkIntoCell 0x545df0, Add 0x5445e0, and eight spawn functions
    (0x5450d0, 0x545120, 0x544f20, 0x545070, 0x544f80, 0x544dc0, 0x544e70,
    0x545860) that build records with the make functions and add them.
  - ai_small_helpers.cpp (new): 0x551780, 0x551760 (passes the caller's
    EDX upper half), EvaLand grid reset 0x554070 and cell of tile
    0x554770, TileCentre 0x554880, TileByte8 0x599570, BlockCentreByte8
    0x554850, BuildingIfUsed 0x58e1c0. ui_state.cpp:
    CLogicThread::ClearPendingUiRefresh 0x53f530.
  - framework_helpers.cpp (new): 14 small constructors/clean-ups
    (CPlayerIndex, CMemoryBlock, CFile, CThread, CWin32Handle::Close
    (CloseHandle), CPlayer::Clear, lobby/net initializers that store the
    top byte of ECX as an allocator byte, ...) and the destructor
    NetHelp_Destroy_49ec80 (26 callers).
- Function start 0x545f30 (static initializer entry Ghidra merged into
  0x545ee0) added to re/extra_functions.tsv (kind manual) and the
  inventory.
- Emulator harness: CloseHandle API probe; `Spec.new_fails_size` (only
  allocations of one size fail: reaches the "exception object is null"
  path after the strings were built). 2 new self-test pairings (API probe
  vs delete; effect-table ignore_bytes).
- Known differences: Lobby_ThrowDataError leaks the four temporaries of the
  thrown message (the original's unwinding frees them; fatal-error path
  only); AI_TileByte8 returns 0 in the upper half of EAX off the map (the
  original keeps the caller's; callers use AX); the CSurface destructors'
  EAX is whatever ~CObject leaves (unused).
- Tests: in-process 321/321; emulator: 45 new specs (full suite 405
  functions, 0 mismatches, 0 incomplete coverage), every
  live block covered (dead inlined blocks of 0x611a20 and 0x49ec80 exempt
  with reasons); self-test 20/20.
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK, 188 of 404 replacements called; 21 of the 45 new
  ones (AI_BlockCentreByte8 166,380, MapEffects_CellOfPosition 136,011,
  AI_TileByte8 59,231, CWin32Handle_Close 367, ...). Not called: the
  exception constructors except CRunTimeError, ThrowDataError, MapEffects
  Add/FindFreeSlot/LinkIntoCell and all spawn functions, and a few lobby
  helpers. Counts in the inventory notes.
- Frontier (all game code): READY 1,255 -> 1,305, NEAR 1,111 -> 1,057;
  simulation range READY 252 -> 253, NEAR 77 -> 60.

### 2026-11-09 (33): batch 25 (33 functions: DirectDraw, UI, AI, simulation)

- Ghidra missed the function start 0x582540 (a static-initializer table
  entry right after NoOp_ret4 0x582530). `re/extra_functions.tsv` now has
  rows of kind `manual` (added by hand with evidence; `tools/rtti.py scan`
  keeps them); with it, trivial.py recognises 0x582530 and the vf27
  members below became READY.
- Batch 25, all verified (none unverified):
  - directx_surface.cpp (new): DirectX::ThrowIfDDFailed 0x405d10 (throws
    CDirectDrawException for hr < 0), ThrowIfDPFailed 0x5bb890 (DirectPlay,
    hr != 0), CSurface::CheckResult 0x5bf2e0 (42 callers; lost surface ->
    owner's restore), DDBlt 0x5bf0c0, DDBltFast 0x5bf130.
  - screen_element.cpp: the "enabled changed" vf27 of CScreenGroup 0x4177f0,
    CControl 0x418520 (closes the tooltip), CGroupControl 0x42a000 (25
    trivial thunks forward to them), and the background fill vf11
    0x415b60 (+0x04 is the fill colour, +0x1f "transparent") with the
    group's 0x417460 and 0x429e50.
  - msvc6/string.cpp: compare(pos, n, const char*, count) 0x40a140.
  - ai_helpers.cpp: CountBuildings 0x59dac0 (a non-zero area counts every
    building, sic), AreaAlongRow 0x5996c0, List32Add 0x551910,
    GridCellWrapped 0x554730, GridSumAround 0x554530, ResetAgentData
    0x55b770, ClearGlobals7e0568 0x553e00, ListFirst 0x559260.
  - graphics_helpers.cpp: GfxData::FrameOffset 0x5faaa0 (78 callers),
    Render::PutPixel16 0x4c7ac0, Render::Entry204 0x4c7ab0.
  - misc_helpers.cpp (new): GameParams::CopyRectB0 0x437400,
    Watcom::CopyByte 0x4584f0, Lobby::EqualField4 0x606ac0,
    Picture::FreeIfBoth 0x5aa0a0 (CRT free).
  - logic_helpers.cpp (new): GameWorld::WearPath 0x51b7f0 (traffic 12 on
    grass -> worn path 0x1c; neighbours' grass variants smoothed to 0x10),
    QueueMarkedTileWithRoom 0x51d000, SetSettlerAnimation 0x50e2c0
    (table 0x6a8060[kind * 4 + race], frame count from GfxData),
    CLogicThread::RemoveSettlerFromGroups 0x5428e0; map_effects.cpp:
    MapEffect::PositionAt 0x544210 (linear move with a parabolic hop),
    MapEffects::SetMapSize 0x544320.
- Emulator harness: COM probes (`Spec.virtuals` options "com": ECX not
  recorded, "returns": EAX from a function, e.g. HRESULTs), partial
  pointer contents ((offset, size) pairs, for DDBLTFX), `Spec.callees`
  (probe S3.EXE functions that can't run in the emulator: exception
  constructors, CRT free), _CxxThrowException as a thrower (compared with
  the thrown pointer and throw info), Sleep probe. Self-test pairings for
  the COM probe (DDBlt vs DDBltFast) and the callee probe/throw (DD vs DP
  check). In-process: game C++ exceptions are caught with SEH in the test.
- Known differences: ThrowIfDD/DPFailed have no exception frame (if the
  exception's constructor itself throws, the block leaks); FillBackground
  zeroes the unused DDBLTFX fields (the original passes stack garbage);
  AreaAlongRow off the map returns 0xffff with a zero upper half (the
  original keeps the caller's; callers use AX).
- Tests: in-process 307/307; emulator: 33 new specs, 0 mismatches, every
  live block covered (dead inlined-check blocks of 0x4177f0 and an empty
  copy loop in 0x55b770 exempt with reasons); full suite 360 functions,
  0 mismatches, 0 with incomplete coverage;
  self-test 18/18.
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK, 169 of 359 replacements called; 23 of the 33 new
  ones (Render_Entry204 18.0M, CheckResult 272,664, QueueMarkedTileWithRoom
  160,422, ThrowIfDDFailed 45,356, GridSumAround 9,123, CountBuildings
  8,850, WearPath 4,536, SetSettlerAnimation 3,212, AreaAlongRow 531,
  DDBlt 102, DDBltFast 101, ...). Not called: CScreenGroup::OnEnable,
  FillBackground 0x415b60 / 0x429e50, RemoveSettlerFromGroups,
  MapEffect::PositionAt, PutPixel16, CopyRectB0, Watcom::CopyByte,
  Lobby::EqualField4, Picture::FreeIfBoth. Counts in the inventory notes.
- Frontier (all game code): READY 1,232 -> 1,255, NEAR 1,194 -> 1,111;
  trivial functions done 390 -> 433; simulation range READY 250 -> 252,
  NEAR 91 -> 77.

### 2026-11-09 (32): batch 24 (41 functions: MapEffect, UI groups, misc)

- Batch 24, all verified (none unverified):
  - map_effects.cpp: the 13 MapEffect make functions 0x543930..0x543f70
    (MapEffect_Make_<address>; argument mapping read off the push
    sequences by a script). They build the record in a stack temporary
    and copy all 0x4c bytes, so the padding bytes +0x2a/+0x2b/+0x3b/
    +0x3e/+0x3f are stack leftovers in both versions (not compared).
  - watcom/ptr_vector.cpp (new): the Watcom pointer vector used for
    CScreenGroup's children: resize 0x429a00, resize + clamp 0x437fe0,
    operator[] bounds check 0x428ee0 (layout in ptr_vector.h).
  - screen_element.cpp: CScreenGroup (= the "UiGroup" class) SetFlag1F
    0x417be0, OnMoved 0x417ab0, Draw 0x417480, HandleMessage 0x417130,
    OnMouseMove 0x4176b0, the derived copies 0x429dd0 / 0x42a030 / 0x42a180
    / 0x429e10, CScreen 0x417f60; CControl (tooltip control) DeleteTooltip
    0x4187b0, OnMessage10 0x418380, OnMouseMove 0x418430 (timeGetTime
    through the import 0x655358), OnMouseEnter 0x4184b0, OnMouseLeave
    0x4184e0; CGroupControl 0x429e80 / 0x429ec0 / 0x429f50 / 0x429f90;
    CStaticControl ctor 0x41c3c0.
  - win32_classes.cpp: DirectX::CSurface ctor 0x5bf200. ai_helpers.cpp:
    AI_BuildingArea 0x599610, AI_FreeTileArea 0x58b480 (second AI world
    pointer 0x7e7b60). area_piles.cpp: GameWorld::CreatePile 0x504420.
    ui_state.cpp: CLogicThread::UpdateGroupMenu 0x542990 (menu +0x2d298
    for the ten settler groups at 0x7ad620).
- Known difference: CScreenGroup::HandleMessage returns an uninitialized
  stack byte in AL when the captured child declines a message; ours
  returns 0 (not compared on that path, documented at the function).
- Emulator harness: `Spec.ignore_bytes` (with a self-test pairing that
  differs only next to the padding: 0x5439a0 vs ours 0x543e80),
  `Spec.virtual_effects` (probed virtual calls can change memory; used to
  shorten or blow up the group's child count so the inlined bounds checks
  run), `Spec.new_fails` (operator new returns null), timeGetTime probe.
  Dead inlined-bounds-check blocks are exempt with reasons EMPTY/NEG/LEN0
  at the specs. In-process: the screen element harness got 19 more
  operations, child calls that change the child count, and a fake clock
  in S3.EXE's timeGetTime import.
- Tests: in-process 285/285; emulator: 41 new specs, 0 mismatches, every
  live block covered; full suite 327 functions, 0 mismatches, 0 with
  incomplete coverage; self-test 16/16.
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK, 147 of 326 replacements called; 16 of the 41 new
  ones (AI_BuildingArea 12,324, PtrVector_Index 785, CreatePile 86,
  CControl_OnMouseMove 39, PtrVector_Resize 18, DeleteTooltip 14, CSurface
  5, ...). Not called: the 13 MapEffect make functions, UpdateGroupMenu,
  CScreenGroup HandleMessage/OnMoved/SetFlag1F (+ copies), CControl
  OnMessage10/Enter/Leave, CGroupControl OnMessage10/Leave,
  PtrVector_ResizeClampLength. Counts in the inventory notes.
- Frontier (all game code): READY 1,231 -> 1,232, NEAR 1,414 -> 1,194;
  trivial functions done 177 -> 390 (forwarders to the group members);
  simulation range READY 261 -> 250, NEAR 78 -> 91.

### 2026-11-09 (31): batch 23 (64 functions: framework, AI, UI)

- Batch 23, all verified (none unverified):
  - game/win32_classes.cpp (10): Win32::CObject ctor 0x610320 / dtor
    0x610330 / deleting dtor 0x60bfc0, CWin32Handle 0x60be60, CWaitable
    0x60bf50, CWindowBase 0x60f960, CMessageQueue 0x60d690,
    DirectX::CSimpleSurface 0x5befb0 / 0x5befe0, CDirectSoundBuffer
    0x5c1f70.
  - game/ai_helpers.cpp (12, Rainer): AI::SetCurrentPlayer 0x551a30 (the
    AI context globals, now in re/globals.csv: g_aiWorld 0x7e3408,
    g_aiPlayer 0x7e3130, g_aiPlayerData 0x7e2eb0, ...), WordListInsert /
    Remove 0x551970 / 0x5519b0, IsOnMap 0x572090, ClampToMap 0x572050,
    AreaGoodsAmount 0x59dc10, GetBuilding 0x58e170, RecordBestCandidate
    0x58ec50, the CEvaLand grid cell / free 0x554700 / 0x554040,
    CopySixBytes 0x55a420, SetWord4 0x55ca60.
  - game/graphics_helpers.cpp (5): Render::PackColour 0x4c7b00,
    GfxData::SpriteWord 0x5fad20, GfxData::SetDword 0x5fc540, Point16
    0x5fdab0, LogicRecord24 0x5437b0; map_effects.cpp: MapEffect::SetLinks
    0x544290 (the effect records are in doubly linked lists).
  - game/screen_element.cpp (36): CScreenElement (Dirk\dialog.cpp) layout
    and 24 members (constructors, destructor, rect, SetRect/MoveBy,
    SetParent, focus, Invalidate, message entry/WantsMessage/dispatch
    jump table/TrackMouse, drawing), six dialog-global getters
    0x416870..0x416c50, and the group class reached through a virtual base
    (vbptr at this-0x7c): WantsMessage 0x417100, SetFocus 0x417650,
    KillFocus 0x417680, plus the forwarders 0x429d70, 0x429df0, 0x429fd0.
    4-byte functions (0x4156d0, 0x415830) hook fine (Detours uses the
    padding after them).
- Emulator harness: `fastcall` convention (args[0] = ECX, args[1] = EDX);
  virtual-call probes (emu.FAKE_VTABLE: each slot records slot, ECX and
  arguments; stack-pointer arguments by contents; return values 0 / 1 /
  random per call; slots declared per spec in `Spec.virtuals`, undeclared
  ones are spec errors); every import of S3.EXE and s3re.dll is bound to
  an API probe, IntersectRect / IsRectEmpty / PtInRect implemented, others
  are spec errors. The in-process UI test uses a fake vtable of recording
  stubs the same way and runs original and ours on the same buffer
  (snapshot/restore), so object pointers compare directly.
- Tests: in-process 276/276 (10 new: Win32Classes_*, AiHelpers_*,
  GraphicsHelpers_*, MapEffect_SetLinks_*, ScreenElement_*); emulator 64
  new specs, 0 mismatches, every block covered; full suite 286 functions,
  0 mismatches, 0 incomplete; self-test 15/15 (new: a wrong virtual slot,
  a wrong API call).
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK, 136 of 285 replacements called; 46 of the 64 new
  ones (e.g. GfxData_SetDword 774M, Point16 116M, AI_GetBuilding 159k,
  AI_GridCell 115k, AI_AreaGoodsAmount 27k, AI_SetCurrentPlayer 9k, the
  UI ones at start-up). Not called: AI_ClampToMap, Render_PackColour,
  MapEffect_SetLinks, Win32_CObject_DeletingDestroy, CSimpleSurface(4),
  ContainsPoint, DeletingDestroy, IntersectRects, IsQuery78Zero,
  KillFocus, MoveBy, SetFlag1F, SetFocus, WantsMessage, two dialog
  getters, CopyGlobals, UiGroup_WantsMessage_429df0. Counts in the
  inventory notes.
- Frontier (all game code): READY 1,144 -> 1,231, NEAR 1,550 -> 1,414;
  trivial functions done 66 -> 177 (forwarders whose targets are now
  done); simulation range READY 261, NEAR 78.
- Not done: the 13 MapEffect make functions (0x543930..): they copy a
  stack temporary with uninitialized padding into the result, so the
  bytes differ between runs; needs an "ignore these bytes" spec option.

### 2026-11-09 (30): trivial functions counted as done; whole-game check; batch 22

- Trivial functions (user decision, item 2 of the evaluation): new
  `tools/trivial.py` finds bodies that are provably trivial by their bytes
  (rule in CLAUDE.md: `ret [N]`, constant return, `jmp`, this-adjusting
  and vtordisp thunks). 707 in game code (leaf 7, constant 17, forward
  683); `apply` marked 705 rows `identified` with a "Trivial, proven by
  its bytes (...)" note (DebugLog and max_size were already done).
  `frontier.py` counts them as done (forwarders once their target is;
  `--no-trivial`): all game code READY 1,029 -> 1,123, NEAR 1,609 ->
  1,542; simulation range READY 251 -> 253, NEAR 95 -> 93. Only 66 of the
  707 are done today, because most forwarders (635 vtordisp thunks) jump
  to unverified UI virtuals (e.g. 0x429d90 -> 0x417680, 0x429db0 ->
  0x418800 `mov al,1; ret`, itself trivial).

- Whole-game check for batches 15-21 (`check_replay.py --recording
  recordings/scripted-seed127.bin --against-original --ticks 3000`, run
  unattended): REPLAY OK, traces identical to the original over 3,000
  ticks; 79 of the 182 replacements were called. New
  `tools/record_call_counts.py` wrote the counts from s3re.log into the
  notes of the 64 rows that still said "not run yet" or had no
  whole-game note: 37 called (all 12 std::string core members, e.g.
  _Tidy 2,034, assign_substr 48,394, plus 25 simulation functions such as
  CreateSettler, AddGoodsToPile, BlockTile/UnblockTile, TileShade), 27 not
  called (e.g. the flood fills / region splits, SendNearestSettlerTo*,
  FetchGoodsToTradeBuilding, the overlays, String_TidyFree, MaxSize).

- Batch 22 (39 functions; all verified, none unverified):
  - msvc6/string.cpp, 16 std::string members: string(x, pos, n) 0x4078a0,
    string(const char*) 0x407a50, string(n, c) 0x5f3bf0, erase(pos, n)
    0x407b10 (373 call sites), operator=(const char*) 0x409a90, assign(n,
    c) 0x41f430, assign-or-empty helper 0x413d70 (null -> the text at
    0x7a7868), _Freeze 0x414490, operator[] 0x4142d0, operator+ (string,
    string) 0x409e60 / (const char*, string) 0x409f90 / (string, char)
    0x421aa0, and the __fastcall concat helpers 0x4077f0, 0x408e50,
    0x43d8e0 (a twin), 0x420cb0.
  - game/string_members.cpp (new), 18 game-class functions the survey had
    filed as std::string::sub_*: nine destructors that only destroy a string
    member (0x411980, 0x441740, 0x450ae0, 0x455170, 0x4581a0, 0x459440,
    0x45f060, 0x499470, 0x49a070; several only reached from unwind
    funclets) plus 0x5e1b10 (two strings), setters 0x412130/0x4181f0,
    operator= 0x4499e0 / 0x5be000 (session description, 80-byte
    DPSESSIONDESC2 + name) / 0x5ed590 (lobby record), constructors
    0x5bb0b0 (session description from a DirectPlay record), 0x5b6790
    (GUID + two names) and Win32::CRegistryValueInfo 0x6161e0 (name passed
    by value). The old 0x617350 "constructor" was renamed Unknown_617350.
  - msvc6/exceptions.cpp (new), std::logic_error what 0x559630,
    ~logic_error 0x5597e0, its deleting destructor 0x5597c0, copy
    constructors of logic_error 0x559640 and out_of_range 0x559850. Not
    reimplemented (they throw, never return): bitset::_Xran 0x559550 and
    its copies 0x58b520/0x596570, the _Doraise virtuals 0x5597a0/0x559830
    (named, identified).
  - Tests: in-process 266/266 (4 new: StringMembers_*, LogicError_*; the
    String_Operations test got 16 more operations); emulator: 39 new specs,
    0 mismatches, full coverage of live blocks (dead blocks listed at each
    spec with a reason code DROP/XLEN/CLEARED/UNSHARED/DESTROY/NPOS; two
    globals presets reach the copy paths: a non-empty text at 0x7a7868 and
    a full count on the static empty string); self-test 13/13 (new: a
    destructor against the one for another offset). `run.py
    --all-uncovered` lists every uncovered block. Full emulator suite: 222 functions,
    0 mismatches, 0 with incomplete coverage (globals presets must always
    write their value: a global keeps what an earlier case wrote).
  - Found: an embedded zero (assign(n, '\0')) makes the original's
    erase() crash when the string is shared: _Split re-copies by strlen,
    so the length shrinks under the running erase (memmove with a negative
    count). Faithful in ours; the in-process test avoids zero characters.
  - Whole-game check with batch 22 (same recording, 3,000 ticks): REPLAY
    OK, 90 of 221 replacements called. Batch 22 called: FromSubstr 221,
    FromText 215, Erase 212, Freeze 192, ConcatString 330, LobbyRecord_Assign
    16, ConcatCharsLeft 8, ConcatChar 4, AssignText 2, AssignTextOrEmpty 2,
    GuidNames 2, FromRepeat 1; the other 27 not called (all destructors,
    setters, exceptions, operator+ variants, At, AssignRepeat). Counts are in
    the inventory notes.
  - Frontier (all game code): READY 1,123 -> 1,144, NEAR 1,542 -> 1,550.

### 2026-11-09 (29): evaluation, unverified convention, batch 21 (std::string)

- Evaluated where reimplementation pays off (ranked list under "Current
  focus"). Top: the MSVC6 std::string core, which blocked hundreds of
  functions.
- Convention for reimplementations without evidence: `S3RE_HOOK_UNVERIFIED`
  (HookKind::Unverified, installed only with `s3re_launcher --unverified`
  / S3RE_UNVERIFIED), status `reimplemented`, hook `unverified`.
  `inventory.py check` now enforces verified<->replaced and
  reimplemented<->unverified; `stats` prints a "Reimpl." line;
  `frontier.py` counts reimplemented as done (`--verified-only` for the
  strict view). No function uses it yet: batch 21 is fully verified.
- Emulator harness (tools/emutest): fake heap (operator new/delete as a
  bump allocator in the world area, calls compared), std::_Xlen/_Xran end
  the run as a throw (compared; stack/registers then not compared), FS
  through a GDT so SEH prologues run, `Spec.edx` (fastcall),
  `Spec.cover_until` (code past a false function start) and
  `Spec.exempt_blocks` (dead or catch-only blocks, reason at the spec).
- Batch 21 (13 functions, msvc6/string.cpp): std::string _Tidy 0x402170,
  _Tidy(true) 0x401d20, max_size 0x402260, _Copy 0x402270 (+ its
  continuation 0x4022f0), _Grow 0x401f80, _Split 0x4020a0 (was misnamed
  assign_cstr), assign(const char*, n) 0x4021c0, assign(string, pos, n)
  0x401d60, ~string 0x401b60 (erase() then _Tidy(true)), the (const
  char*, n) constructor 0x401a10, the default constructor 0x407880,
  operator+(string, const char*) 0x402380 (was Unknown_sub_402380), and
  the fastcall concat helper 0x401c70 (ECX = this, EDX = left).
  Exception paths: ours has no unwind frames, so temporaries leak if
  _Xlen/_Xran throw (fatal-error paths only).
- Tests: in-process String_Operations_MatchOriginal (random operation
  sequences on six strings, real heap, compares fields, text, count bytes
  and buffer sharing) + its mutation test; all 262 in-process tests
  pass (2 of them new; the suite counts tests, not functions). Emulator: 14 String_* specs (incl. _Eos,
  which had none), 0 mismatches, every live block covered (dead and
  catch-only blocks exempted with the reason at each spec: 15 blocks; one
  _Xlen needs a 4 GB string); self-test 12/12 (new pairing: _Tidy(true)
  against ~string). Full emulator suite after the harness changes: 183
  functions, 0 mismatches, 0 with incomplete coverage.
- Found: the 1,966 static string initializers run through our
  String_FromChars at start-up (call counts in --test mode), so these
  replacements are exercised before WinMain.

### 2026-11-09 (28): function survey merged

- A survey classified every remaining function (`tools/survey_functions.py`,
  `tools/survey_lib.py`, method and findings in `re/function_survey.md`),
  merged with `tools/merge_survey.py` (never changes existing rows):
  `re/functions.csv` now has all 9,836 known functions (was 1,685, 17%).
  Understood 46.6% (mostly 4,286 compiler stubs: 1,966 `std::string`
  static initializers, 2,271 `atexit` registrations, thunks); 3,771 rows
  are only `classified` (subsystem by source file, call graph, imports,
  vtables, world fields), 90 have subsystem Unknown. Confidence is in the
  notes (high/medium/low); treat low/medium names as provisional.
- Findings: the static-initializer blocks mark the end of each .obj
  (`re/obj_units.csv`, 73 units; boundaries match UpdatePhase2,
  UpdateSettlers, CGameData::CGameData, ExecuteCommands, ...); 243
  functions push an `E:\Siedler3\...cpp` path, so `re/source_files.md`'s
  claim that no code references the paths is wrong (to correct); zlib and
  libpng are linked in (0x5b2fb3..0x5b50ad, subsystem Picture.zlib,
  category game for lack of a third-party category); the source-file ranges
  look shifted around the DirectX wrappers (open question, noted per row).
- The survey files themselves landed in the batch-20 commit 1f5e72c (that
  agent's `git add -A` picked them up while the survey was finishing); they
  were complete, so no history rewrite.

### 2026-11-09 (27): batch 20

- Earlier today, separate commits: fb4ec1b added `tools/frontier.py` (ranked
  READY/NEAR/BLOCKERS lists of functions whose callees are verified);
  31417fc mapped the world object (it is class `CGameData`, 0x153A128
  bytes, vtable at +0): `re/world_layout.md`, `re/structs.md`,
  `re/proposed_structs.h`, `tools/world_fields.py`.
- Part A: 0x401a10 is the MSVC6 `std::string` constructor from (const
  char*, n) (strlen, _Grow, two assigns); 1,966 static-initializer stubs
  call it (`push -1; push text; mov ecx, global; call; ret`). 0x401b60 is
  the `std::string` destructor (inlined erase + _Tidy; 2,358 call sites in
  end-of-scope sequences). Both `stl`, identified. `tools/frontier.py` now
  hides initializer stubs by shape (1,968 in all game code, 749 in the
  default range), lists identified library functions in a LIBRARY section
  and has `--library-done`. CLAUDE.md: entries for frontier.py and the
  world-layout files (and the world object is no longer called
  non-polymorphic).
- Batch 20 (12 functions, list under "Current focus"): in-process
  260/260 (21 new incl. 9 mutation tests); emulator specs 12/12 with full coverage; full emulator suite
  169/169, self-test 11/11.
- Found: area +0x8 is the list of empty piles (goods 0xff); the NGO bytes
  at world+0x1195dcd; the MapEffect record layout (structs.md).
- Skipped: the 13 MapEffect make functions (mechanical, next batch);
  0x5437b0/0x51e650/0x53f530/0x523540 (still unclear objects/fields);
  0x50cf50 (too large for a batch).

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
