# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Core gameplay first, no network (user decision 2026-11-09, after
batch 27).** Skip network code entirely for now: CNetThread, `Lobby::*`,
`LobbyClient::*`, DirectPlay, the CLM* messages. Single player against
computer players is easier to test and online play may not matter. The
user may provide a late-game save file later. Order:

1. **Building-site chain** - done in batch 28: CheckBuildingSite 0x4d99e0,
   PlanSiteLevelling 0x4da440 (was "EvaluateBuildingFootprint"),
   AddBuildingSite 0x4dd280, plus IsFreeCastleSite 0x508420 and
   IsGoodStartPosition 0x5081a0 (start positions).
2. **GameWorld::Create 0x5166d0** - done in batch 29 (the random map
   generator), with ExpandTerrain 0x519440, MarkTerrainEdge 0x51a2e0 and
   BuildRegionsAndAreas 0x51a580. Create only runs for random maps, so it
   has whole-game evidence since session 40 (`--random-map`,
   tools/check_random_map.py).
2b. **Refactor batch - done 2026-11-10:** typed world structs
   (`src/s3re/game/layout.h`), all replacement files converted, no raw
   offsets left (`tools/typed_access_lint.py` in `inventory.py check`).
   **Next: step 3, as the parallel round of 3 packages planned in
   `re/update_settlers.md`.** Started 2026-11-10 (session 43, cut to one
   hour by the user): package C batch 1 = 3 functions (UpdateShadeAround
   0x519670, SetTerrain16Patch 0x51ba00, CountOtherPileClaims 0x510800).
   **Package B (session 44): 27 of 28 done and verified; left:
   UpdateSoldier 0x50e9f0 (analysis notes in the session 44 entry).**
   **Package C is complete (2026-11-10, session 44, parallel worktree):
   all 22 functions verified** (batches 2-6; session entry below). Left of
   the round: whatever packages A and B have not finished (see their
   entries), the round-end checks (full emulator suite, whole-game replays
   with call counts: none of the 19 new package C functions has
   whole-game evidence yet), and the renames in TODO.md.
3. **The 27 undone callees of GameWorld::UpdateSettlers 0x4e9670** (17,525
   instructions), small ones first. Decide the x87 question first (below).
   READY after batch 29 (instructions): 0x50e320 (28), 0x519670 (63),
   0x50e140 (65), 0x50ccb0 (69), 0x50cb20 (72), 0x50dd20 (111), 0x51ba00
   (158), 0x5449f0 (207, MapEffects tables), 0x544630 (209, MapEffects
   tables), 0x5046b0 (221). NEAR: 0x505820 (52), 0x4d97b0 (126), 0x4fec10
   (159). Big blockers: 0x500860 (3,097, walking), 0x50e9f0 (1,791),
   0x4dbc50 (1,440), 0x50cf50 (919).
4. **GameWorld::UpdatePhase2 0x4e05e0** (4,919 instructions, 8 undone
   callees).
5. **UpdateSettlers itself**, split internally (e.g. per settler job) but
   hooked as one unit.

x87 floating point (proposal, batch 28, not decided yet): the simulation
range 0x4c0000..0x560000 has very little real x87 code: 4 `fsqrt`, 4 calls
of `_ftol` 0x61fe98, a few `fild`/`fmul`/`fstp` (the ~231 `fidiv [esp]`
and the `fisttp` lines dumpbin shows are data decoded as code, e.g. jump
tables). The game never sets the FPU control word itself: every `fldcw`
is in the CRT (`_ftol` switches to truncation and back; the math library).
Windows starts threads at 53-bit precision, round to nearest (0x27F), and
plain DirectDraw doesn't change it (only Direct3D device creation does).
So x87 results equal IEEE double arithmetic, which our SSE2 build
produces with `double` (`/fp:precise`, no FMA contraction) as long as the
operations happen in the same order with the same operand widths
(`float` stores rounded where the original stores a `float`) and `_ftol`
is reproduced as truncation toward zero. Proposal: write such code with
`double`, keep the original's operation order, add emulator specs (the
emulator runs real x87 code) with values near rounding edges, and confirm
the control word once at runtime (log `_controlfp(0, 0)` from a hook on
CLogicThread::Step in the game thread). Fall back to inline `__asm` x87
only if a difference shows up. CRT transcendental functions (sin, atan,
pow) would need the original CRT code; none were found in the simulation
range so far.

Earlier (still valid as background):

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
- (Savegame checks, 2026-11-09 (37).) Merge the new whole-game evidence
  from `re/savegame_checks.md` into the inventory notes; from now on run
  `python tools/check_save.py "slot N.sav" --ticks 2000
  --against-original` for the three saves at milestones, next to the
  recorded-game check (late-game code paths, later UpdateSettlers).
0. (New order, see the top of this section.) Batch 29 (step 2, Create)
   is done; next is the refactor batch (2b at the top), then step 3, the
   READY UpdateSettlers callees listed at the top. Optional tooling: start a random-map game unattended, for
   whole-game evidence of Create. Skip every network
   item below (Lobby 0x602580/0x600f10/0x602850/0x5ec550 etc.). Still
   NEAR in the building area: 0x4dbab0 (needs 0x51b640), 0x4dbb10/
   0x4dee20/0x4deec0/0x504a00 (all need 0x50cf50, 919 ins, probably
   settler removal), 0x4d97b0 (needs 0x4d9250, 349 ins). The start
   placement 0x5074b0 (AddPlayerTowns, 902 ins) now has its two site
   checks done.
1. (Batch 27 done 2026-11-09: assertion/out-of-memory exceptions and
   throwers, Lobby::CMemoryBlock/CLMGeneric, CStaticText, the string
   vector resize 0x40a580 (allocator byte = low byte of n, reproduced),
   0x51d2b0, CEvaLand release, 17 small framework/container/graphics
   members; 44 in all, see the session log.) Next batch, concrete:
   - Simulation: the area set-up 0x51a580 (380 ins, READY now; called by
     GameWorld::Create and 0x4fd540, so whole-game evidence is likely).
   - MapEffects callers: 0x545730, 0x545150, 0x5453f0, 0x5458d0 use x87
     (fild/fsqrt/fmul + _ftol 0x61fe98): decide first how to replicate x87
     results exactly (probably __asm blocks; our build uses SSE2, and the
     game's FPU control word may be single precision). 0x544630 and
     0x5449f0 have no FP but build ~10 local tables indexed by arg2 +
     2*arg1 (check for uninitialized entries).
   - Tooltip opener 0x418550 (callees now done except GDI/COM: needs API
     probes SelectObject 0x655030 / GetTextExtentPoint32A 0x655038 and the
     surface GetDC/ReleaseDC COM probes), then 0x4183c0 and 0x429f10 (25
     callers, 25 unblocks).
   - Win32::ThrowWin32Error 0x610de0 -> CHResult 0x613220 -> its text
     function 0x6134d0 (FormatMessageA / LocalFree probes needed).
   - Lobby: 0x602580 (42 callers) and 0x600f10 (40 callers) are READY;
     0x602850, 0x5ec550.
   - Small READY ones with many callers (frontier sorted by callers):
     0x428d10, 0x5da200, 0x463ab0, 0x4148a0, 0x5da3b0, 0x414770, 0x60b5f0.
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
- Tick pacing is understood (2026-11-10, see CLAUDE.md "Tick pacing and
  `--fast`"): a 64 ms CTimer, Step ticks only when it fired. Per-step work
  (`0x524fa0`, `0x51f110` PrepareDrawData) still runs a timing-dependent
  number of times; volatile/UI world fields known so far: `+0x2d2c8`,
  `+0x2d2e4..+0x2d2eb`, `+0x2d301` (skipped by the trace hash). Normal
  speed and `--fast` gave identical traces on the recording and one save;
  other saves/longer games may still find more such fields. `+0x2d2d8`
  (incremented by Step before every ExecuteCommands attempt, also when the
  network allowance stops it) was identical in those runs but is a
  candidate.
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

### 2026-11-10 (44): package B of the UpdateSettlers round (parallel worktree)

27 of package B's 28 functions verified (in-process diff + mutation
tests, emulator specs with full coverage), in four pushed batches:

- Batch 1: SetPileGood 0x5061b0, DropGoodNear 0x5046b0
  (pile_goods_moves.cpp), NotifyLocalPlayer 0x4fec10 (notifications.cpp),
  SpawnSettlersAround 0x50cb20, EnterBuildingSlot 0x50ccb0,
  CreateSevenTileUnit 0x50dd20, SetSoldierAnimationInBuilding 0x50e140,
  SetSettlerSavedAnimation 0x50e320 (settler_spawn.cpp), TileSlope
  0x4faa70 (miner_jobs.cpp), and the linker-folded getters
  GetMemberDword64 0x49f510 / GetMemberByte189 0x414030
  (folded_getters.cpp; both hookable thanks to nop padding; "Config::"
  names were wrong, the code serves several classes).
- Batch 2: map_effect_spawns.cpp (map_effects.cpp untouched): Reset
  0x543fd0, Unlink 0x545e40, RemoveMatching 0x545c80, SpawnArrow
  0x545150, SpawnProjectile0/1/2 0x5458d0/0x5453f0/0x545730,
  SpawnProjectile 0x545b90, Spawn_544630/5449f0 (stack tables);
  MinerJobEnter 0x4f9a00.
- Batch 3: ResetSettlerToIdle 0x50cf50 (settler_idle.cpp),
  TakeGoodFromPile 0x505820, StealGoodFromPile 0x505c20.
- Batch 4: MinerJobFood 0x4f9b80, MinerJobDig 0x4fa220.

**x87 decided and done:** fild/fsqrt/fmul/_ftol reproduced with `double`
in the original order and `_ftol` as truncation through int64 (NaN and
out-of-range give 0 in EAX). In-process tests (real control word) include
flight times landing exactly on integers and wrapped negative squares
(NaN); all identical. **Harness change:** `emu.py` now sets the x87
control word 0x27f before every emulated call (Unicorn starts with 0:
24-bit precision, exceptions unmasked); self-test 21/21, the MapEffect
specs clean; the full suite has not been run with it (left to the round's
end).

Notes: `build/annotate.py`-style stack annotation (track esp through
push/pop/calls) made the long push sequences reliable; it is not
committed (scratch tool). MinerJobDig leaves job/animation as
uninitialized stack for non-mine building types (not reproduced, only
mines call it). 0x544630/0x5449f0 index stack tables with race and
variant; other indices read stack garbage (not reproduced). New globals:
g_campaignMission 0x7dfdb0, g_playerTallies 0x7acfd4 (per player 0x44
bytes; meals, ore found, points; re/globals.csv). specs.py's
animation_table() reads 0x600 entries now.

**Left:** UpdateSoldier 0x50e9f0 (1,791 instructions, 5 call sites use
EAX). Callees all verified except C's 0x519a90 (call through its
address) and 0x51b640 (C, done in C batch 3). Structure from the call
list: IsOtherPlayerNear twice (0x50eaab, 0x50ff0a), attack animations
(SetSettlerAnimation x8) with TileScreenX/Y and SpawnArrow (bowmen,
g_gameTick 0x7dfd48), damage with the divisor 0x730eec (256) at
0x50f248, deaths (QueueEvent, RemoveSettlerFromLists x3,
ClearSettlerJob, DebugLogs 0x6a67fc/0x6a67dc), tower capture
(QueuePendingEvent, CreateSettler, ClearTileOwner/SetOwnerOfFreeTile
x2, RecalculateTerritory 0x519a90), notifications (NotifyLocalPlayer
x5), campaign check (g_campaignMission + GetMemberByte189 at 0x50f36f)
and movement (TileDistance, NeighbourX/Y, SetSettlerTarget) at the end.

### 2026-11-10 (44): package C complete (19 functions, parallel worktree)

All of package C of the UpdateSettlers round (re/update_settlers.md) is
reimplemented and verified, in 5 pushed batches (in-process differential
and mutation tests, emulator specs with full coverage or justified exempt
blocks; no game window runs, whole-game checks are left for the round's
end):

- Batch 2: RecountPlayerPopulation 0x5104c0, SetTowerSoldierAnimation
  0x50e200, ClearBuildingFootprint 0x51aae0 (economy.cpp,
  building_crush.cpp). Player +0xc4..+0xd8 named (population,
  population_limit(+_base), carriers, surplus_carriers(+_marked));
  Building +0x07 is the race. Both still have their old names as union
  aliases in layout.h because package B's settler_spawn.cpp uses them
  (TODO.md).
- Batch 3: ResetCivilianOnTile 0x50e980, LosePileOnTile 0x504f70,
  ClearTileOwner 0x51b640 (territory.cpp); BlockTileAndMoveSettler
  0x4dbb10, BlockBuildingLine 0x4dbb90 (the line ends at terrain 0x30,
  not a flag), DropInputPile 0x504a00, DropStoragePile 0x504b70,
  DropOutputPile 0x504e20 (building_crush.cpp).
- Batch 4: RequestSettlerForBuilding 0x4dacd0, DispatchPileCarrier
  0x5052c0 (economy.cpp). The original passes SetPileGood's good with
  garbage upper bytes (only CL loaded); SetPileGood uses the low byte.
- Batch 5: BuildingDone 0x4dbc50 (building_done.cpp). New global
  0x7acfe0 (int32[20][17], temples add 8; re/globals.csv). For races above
  3 the soldier tile of a military building is (number, number): the
  original reads its overwritten argument slot.
- Batch 6: RemoveBuildingGoods 0x4de430 (was the proposal
  "RemoveBuildingStorage") and the cycle ReleaseSiteWorkers 0x50e3b0,
  CrushBuilding 0x4d9250, DestroyBuildingAt 0x4d97b0,
  RecalculateTerritory 0x519a90. The recursion test
  GameWorld_DestructionCycle_MatchesOriginal crushes a garrisoned tower;
  the recalculation without it hands its land to the neighbouring tower's
  player and destroys a construction site there (DestroyBuildingAt ->
  CrushBuilding -> ReleaseSiteWorkers resets its diggers); the original
  side runs all four as original code (test::ScopedJump points each hooked
  address at its trampoline), ours runs our four. Known difference:
  CrushBuilding on a free record returns whether an uninitialized stack
  slot of the original is 0; ours returns 1.
- Test tooling: `src/s3re/test/callee_stub.h`: CalleeStubs (temporary
  recording stubs over a callee's first five bytes, 1..5 stack arguments,
  optional side effects; the plan from session 43 for package B's callees)
  and ScopedJump. Replacements call such callees (package B's, the
  flooding BlockTile/UnblockTile/SplitAreaAtTile/SetOwnerOfFreeTile, the
  cycle functions) through their S3.EXE addresses, so that tests can stub
  them; in the game Detours sends the call to the replacement as usual (and
  the call is counted). Emulator specs probe the same callees.
- Lessons: random test worlds make list walks loop (request lists, area
  lists, garrison chains) and "remaining amount" fields drop 2^31 units;
  set those up explicitly. A stopped background test run leaves its
  S3.EXE running; kill it by its launcher's PID (only your own).

### 2026-11-10 (43): housekeeping; package C batch 1 (3 functions); game files restored

Session cut to one hour by the user (only the smallest ready package C
functions).

- **Reference traces regenerated (trace v5)** from replays with the
  original code (`--original --fast`, via `check_replay.py
  --against-original`): `recordings/scripted-seed127.bin.tsv` (3,000
  ticks; it had none) and `fun8-seed2024-input31.bin.tsv` (5,000 ticks;
  was v3). Plain replay checks against them: identical, mutation
  detected (good), both. Ours vs original: identical on both (203 and 206
  replacements called; 3,000 and 5,000 ticks).
- **Full emulator suite** on c561f41 (default 8 jobs): 458 specs, 0 mismatches, only GameWorld_Create incomplete (338/415, known); recorded as the last full clean pass. (The DLL was rebuilt while its last spec ran; the later run below repeats it.).
- **Worktrees:** removed the 10 agent worktrees whose branches were fully
  merged into origin/main and had no uncommitted changes, and their
  branches. Kept `agent-a06bfddbf49ac7135` (branch not merged by hash;
  TODO.md). **Accident:** `git worktree remove -f -f` followed each
  worktree's `Settlers3Ultimate` junction and deleted the main checkout's
  game files. Restored by extracting `Settlers 3 Ultimate.zip` from the
  GOG install folder (`C:\Program Files (x86)\GOG Galaxy\Games\Settlers 3
  Ultimate`; the copy was made from it on 2026-11-07): S3.EXE SHA-256
  matches, the three savegames equal the GOG folder's, and the cached
  original traces (whose keys include the input files' hashes) were hit
  for the recording and for `slot 1.sav`, so the inputs are the same as
  before. Lost: only game-written files (errorlog.txt, the lock queue
  directory). Rule added to CLAUDE.md (delete the junction first).
- **Package C batch 1** (re/update_settlers.md), 3 functions, all
  verified (in-process differential + mutation tests, emulator specs with
  full coverage, whole-game):
  - `GameWorld::UpdateShadeAround` 0x519670 (was Logic_sub_519670):
    after a tile's height changes (diggers levelling a site), the shade of
    it and its six neighbours is recomputed. terrain_updates.cpp.
  - `GameWorld::SetTerrain16Patch` 0x51ba00 (was Logic_sub_51ba00; the
    CSV's "RestoreGrassAt" was wrong): a grass tile surrounded by grass
    becomes terrain 0x16 (probably a field or bed made by the workers of
    settler types 0x10/0x26/0x1d, jobs 0x91/0xe6/0xf7), the grass
    variants around it plain grass; stops at the first other neighbour.
    terrain_updates.cpp; re/structs.md terrain row.
  - `GameWorld::CountOtherPileClaims` 0x510800 (was Logic_sub_510800):
    the 128-entry list at world+0x1195dd4 ("recent positions") really
    holds pile claims {pile, settler}: its only writer,
    AddToRecentPositions, is called by job 0x152 as (settler, pile); this
    counts other settlers' claims on a pile. economy_helpers.cpp; rename
    of the old names in TODO.md; re/world_layout.md updated.
  - Whole-game (`--fast`, cached original): scripted-seed127.bin 3,000
    ticks identical (UpdateShadeAround 2,858 calls, SetTerrain16Patch 0,
    CountOtherPileClaims 0); `slot 1.sav` 2,000 ticks identical
    (SetTerrain16Patch 738, others 0); fun8-seed2024-input31.bin 5,000
    ticks identical (UpdateShadeAround 15,026, SetTerrain16Patch 5,
    CountOtherPileClaims 0). CountOtherPileClaims has test evidence only
    (job 0x152 never ran in these games).
  - In-process: full `--test` 363/363 passed. Emulator `--changed`: selected all 461 specs (CMakeLists.txt changed) and ran the self-test: 0 mismatches, only GameWorld_Create incomplete (known), self-test 21/21 caught; the 3 new specs cover all blocks (1/1, 61/61, 6/6).
- **Analysed, not done (for the next package C batch):**
  - 0x50e200 (proposal `GameWorld::SetTowerSoldierAnimation`): animation
    0x176, by soldier type 0x20/0x23/8/0x1e/0x21/0xf/0x1f/0x22 ->
    0x177..0x17e, +9 for building type 0x10, +0x12 for 0x12, +0x1b /
    +0x36 for building byte +0x7 == 1 / 2, then SetSettlerAnimation (EAX =
    its). Its in-process test needs a fake GfxData object for animation
    ids 0x176..0x1d0 (logic_helpers.cpp's CompareSetSettlerAnimation only
    covers kinds < 40); dropped from this batch for time.
  - 0x5104c0 RecountPlayerPopulation: per player p, int32s at player
    +0xc4..+0xd4: +0xc4 = civilians (class 0 or 10..29), +0xc8 = +0xcc
    (base) + 10/30/100 per finished residence 0x1a/0x1b/0x1c, +0xd0 =
    carriers (type 0), +0xd4 = carriers - carriers * ((+0xc8 << 16) /
    +0xc4) >> 16 when +0xc4 > +0xc8 (unsigned), else 0. Name the fields
    in layout.h (Player::unk0c4) when it is done.
  - 0x50e980, 0x51b640, 0x504f70, 0x504a00, 0x504b70, 0x504e20, 0x4dbb10,
    0x4dbb90, 0x51aae0 read (ghidra + asm); they call package B's
    ResetSettlerToIdle 0x50cf50 (and TakeGoodFromPile 0x505820) on
    settlers found by scans up to the settler high-water mark. Plan for
    their tests: stub 0x50cf50/0x505820 during in-process tests (patch a
    `jmp` to a recording stub, restore afterwards) and probe them in the
    emulator (`Spec.callees`), so both versions' calls are compared
    without running B's code on random worlds. Facts found: settler +0x22
    holds the pile number for carrier jobs 0x4c..0x4f; 0x51b640's EAX is
    not used (asm_tools' "flow" hit continues into SetOwnerOfFreeTile);
    0x51aae0 clears a building's footprint (layout table 0x6aa170 by type
    + race * 0x39, entries 0x960 bytes, markers 0x80000000..3, footprint
    bit 0x08, search mark 0x71 -> 0, UnblockTile on the first list) and,
    for a site still in buildup 1, the levelling bit 0x04 of its 31x31
    site-cache grid (proposal `GameWorld::ClearBuildingFootprint`).
  - REA: Ghidra on Windows doesn't allow renames (`set_addresses_names`:
    "Windows Ghidra P0 does not admit database mutation"), so names can't
    be re-applied in REA sessions on this machine.

### 2026-11-10 (42): refactor batch 2b, typed world structs

- `re/proposed_structs.h` moved into the build as
  `src/s3re/game/layout.h`: `GameWorld` derives from
  `layout::GameWorldFields` (record types in namespace `layout`), every
  offset `static_assert`ed; tiles from 0x67444 (settled earlier, now used
  everywhere: the old 0x67440-based files map.cpp, map_neighbourhood.cpp,
  settlers.cpp, world_search.cpp now use the real Tile fields, same
  absolute addresses). `TileOffset`/`TileAtOffset` keep the original's
  32-bit tile byte-offset arithmetic where it steps past the map.
  `game/raw.h` (`RawAt`, `RawAtAddress`, `OffsetIn`) is the only raw
  helper; `WorldPair::Both`/`SameField`/`OffsetOf` for typed tests.
- Six parallel worktree workers converted ~60 files (groups: map/tiles,
  settlers/buildings, areas/players, piles/tables/effects, AI/UI,
  framework). Removed: ~37 per-file `At`-style helpers, all `WorldField` /
  `world_offset::` uses (world.h now just includes layout.h), ~440 world
  offset constants. New object layouts: `framework_layout.h` (Win32,
  DirectX, exceptions, lobby, GfxData), `logic_thread_layout.h`
  (CLogicThread +0x74, +0x84, world +0x3a0, queues +0x3c4/+0x3c8; one view
  shared by ui_state, ui_overlays, game_end, determinism), `ai_layout.h`,
  `screen_element_layout.h`, `settler_groups.h` (S3.EXE groups at
  0x7ad620). `MapEffect` got real fields (padding explicit);
  `ui_2d290[5]` split (ui_special_menu 0x2d298, ...).
- No code/layout conflicts found. Notes: building +0xe is read both as a
  byte and a word (kept uint16 pile_ring); AI_TileByte8 reads the owner,
  not terrain; `Settler::direction` also holds an animation id (rename in
  TODO.md). world_create.cpp's local `NextDirection` clashed with the
  hooked one in the emulator's symbol lookup (renamed NextHexDirection).
- Determinism trace v5: regions follow layout.h (header up to 0x67444, map
  rows of mapSize tiles, rest up to the object end 0x153a128), first header
  skip from 0x2d038 (was 0x2d02c), named per-tile skips;
  tools/diff_world_dumps.py in sync; keeps session 41's v4 skip of
  +0x2d2c8. Older traces (and cached original reference traces) aren't
  comparable: S3RE_ORIGINAL_TOOLING_VERSION bumped to 2.
- Rule in CLAUDE.md ("Typed access, no raw offsets"); postponed items
  (unk fields to name, remaining RawAt objects, MessageQueue) in TODO.md.
- After rebasing onto `--fast` (2090710): +0x2d2c8 named
  `notification_blink` in layout.h, trace v5 keeps its skip; rechecked on
  the final code with `--fast`: recorded game 3000 ticks vs original
  identical (202 called), `slot 1.sav` 2000 ticks identical (200 called),
  in-process 357/357.
- Verification before the rebase (normal speed): build clean, no warnings; in-process 357/357; emulator
  (see below); whole game: recorded game 3000 ticks against the original
  REPLAY OK (203 of 457 replacements called), `slot 1.sav` 2000 ticks
  against the original identical (201 called); inventory, trivial checks
  pass. No inventory status changes.
- Emulator (parallel runner from the emulator agent's worktree, `--jobs
  18`, our DLL): 458 functions, 0 mismatches; the only incomplete coverage
  is GameWorld_Create 338/415 (the known gap, TODO.md). `--self-test` 21 of
  21 caught. Still unverified by this refactor: nothing beyond what was
  unverified before (the 77 emulator blocks of Create).

### 2026-11-10 (41): parallel emulator runs, --changed, fair game lock (tooling, parallel worktree)

- **`run.py --jobs N`** (default cores-2 = 18 here, capped by free memory;
  a worker peaks at ~380 MB): specs run in spawn worker processes, output
  in specs.py order, same summary and exit code. Slowest first (times in
  build/emutest_times.json); specs slower than ~total/jobs/2 run as case
  ranges whose merged result equals a whole run (checked on 10 specs incl.
  self-test pairings, 2/3/7 parts). Full suite: 1128 s on the first run
  (no times yet), 1003 s with times, 856 s with splitting, all on a
  machine shared with another agent's emulator suite; serial is over an
  hour (GameWorld_Create alone 840-1000 s). All three parallel outputs
  identical (458 specs, 0 mismatches, only GameWorld_Create incomplete
  338/415 as known); the `--jobs 1` run matched on its first 70 specs and
  was stopped for time (TODO). Self-test parallel = serial (21/21).
- **Cross-spec state:** `Spec.globals` wrote S3.EXE globals with
  mem_write, outside the per-call undo, so they leaked into later specs
  (the 0x65539f case of session 35). `Emulator.reset` before every spec
  restores both images, zeroes the stack and forgets world bytes and
  probes. Nothing else carried over (writes are undone per call).
- **Bug found:** at HEAD the full suite crashed with "NextDirection: 2
  matching symbols" (world_create.cpp has a NextDirection helper in an
  anonymous namespace). find_our_function now prefers the non-anonymous
  match.
- **`run.py --changed [--since REF] [--dry-run]`** (tools/emutest/changed.py):
  selection from MSBuild's CL.read tlog (headers per object) and the DLL
  reference closure (calls, operand addresses and data pointers, symbols
  from s3re.map); specs.py entries by fingerprint of the entry plus the
  helpers it uses. Checked: a comment in map_directions.cpp selects 13
  specs (its 5 plus 8 callers in area_split/settler code); world.h
  selects 215; a changed helper/entry/comprehension selects exactly its
  1/1/8 specs; a comment change in specs.py selects 0; harness changes
  select all. Full runs with 0 mismatches are recorded in
  build/emutest_last_full.json (incl. the incomplete-coverage list).
- **Release build:** not usable as is (identical-function folding drops
  names from s3re.map); TODO.
- **Game lock:** an S3.EXE whose parent is `s3re_launcher --test` doesn't
  count (checked with a live --test run). FIFO queue of ticket files in
  Settlers3Ultimate/s3re-game.queue/, 3 s polling, dead-pid cleanup,
  max_wait, `status` with holder and queue; tools/test_game_lock.py
  (3 waiters in arrival order, a dead-pid ticket and lock, a timeout).

### 2026-11-10 (41): `--fast`, cached original traces, first-call ticks (tooling, parallel worktree)

- **Pacing found:** CLogicThread::OnStart starts the CTimer at
  CLogicThread+0x3bc with 64 ms (`Win32::CTimer::StartPeriodic` 0x60d3a0 ->
  `Start` 0x60d0a0: SetWaitableTimer, or timeSetEvent); Step waits on the
  wait set +0x3dc (`CWin32Waitable::WaitForXYZ` 0x463100, 500 ms, throws on
  timeout) and ticks only if the timer object was signalled. The bounce
  thread answers at once (its step+5 allowance doesn't pace). The game's
  own fast-forward (`g_stepCountdown` 0x7dfd2c += 882 in ProcessEvents,
  0x525cd2) sets 0x7dfd30, which makes UpdateEnvironment and others skip
  work, so it's not usable. The draw thread doesn't throttle the logic.
- **`--fast`** (`src/s3re/tooling/fast.cpp`, passthrough on 0x463100, only
  for the call from Step): poll, report the timer if nothing is signalled.
  check_replay/check_save/check_determinism use it by default
  (`--no-fast`); recording with `--synthetic-input` stays at normal speed.
- **Evidence (all original unless said):** fast twice, 1,000 ticks:
  identical. Recording scripted-seed127.bin, 3,000 ticks, normal vs fast:
  identical (trace v3). "slot 1.sav", 2,000 ticks, normal vs fast: header
  hash differed at a few ticks; header dumps showed only +0x2d2c8, the
  notification blink bits PrepareDrawData writes and reads only for the
  draw data (UI, timing-dependent) -> skipped, trace v4; rerun normal vs
  fast: identical. Ours vs original, both fast: save 2,000 ticks
  identical (200 of 457 replacements called), recording 3,000 ticks
  identical.
- **Speed:** recording 3,000 ticks 198 s normal vs 9-16 s fast per run
  (about 66 s vs 3-5 s per 1,000 ticks; the game loop itself runs ~450
  ticks/s, the rest is loading and shutdown). Save 2,000 ticks: 139 s vs
  20-25 s.
- **Reference cache** (`tools/trace_cache.py`, `build/trace_cache/`): the
  original's trace is reused (key: S3.EXE + input hashes, launcher
  arguments incl. --fast, trace version, `S3RE_ORIGINAL_TOOLING_VERSION`
  from `src/s3re/tooling/version.h`). `--no-cache`, `--refresh-cache`,
  `--verify-cache`. Second check_replay run: "CACHED", one run instead of
  two (saves ~16 s fast, ~200 s at normal speed).
- **First-call ticks:** the counting thunk also stores g_gameTick of the
  first call; s3re.log "first-call <name> <tick>", `tools/first_calls.py`.
  Recording (3,000 ticks): of the called replacements, 15 were first
  called after tick 500, 10-11 after 1,000, 6 after 2,000 (last at 2,828).
  Save (2,000 ticks from 366,754): 9 after 500, 6 after 1,000, 0 after
  2,000 (last ~1,900). Proposal: keep 3,000 ticks for the recording (cheap
  now) and 2,000 for saves; longer games still add functions late, so a
  longer recording (10,000+ ticks, now ~1 minute) would find more.
- Recordings' own reference traces (`FILE.tsv`, used without
  --against-original) are v3 and must be regenerated before comparing with
  v4 traces.

### 2026-11-10 (40): random-map games, --skip-hooks, a new 8-player recording (tooling, parallel worktree)

- **Random-map start without menus:** `s3re_launcher --random-map
  SIZE[,SYMMETRY[,FIELD0C]]`. How the game does it: the map dialog
  (0x47fea0 / 0x481e90) keeps a Dirk::CMapFile (0x134 bytes, ctor
  0x4a5a70), makes it memory-only (0x4a60c0: +4 = 1, so the chunk lookup
  0x4a60f0 never reads a file) and adds chunk 5 (word 5, word 0, 0x18,
  size = (combo + 6) * 64, a second dialog value, symmetry, seed = atoi of
  the seed field) and chunk 1 (1, 0, 0x20, kind 2, start positions
  min(((size-32)^2 >> 14) + 1, 20), zeros) with CMapFile::AddChunk
  0x4a63f0 (thiscall type, sub-index, data). The free-game dialog then
  calls GameSettings::NewGame 0x431ab0 (13 args, map file = arg 6);
  CLogicThread::OnStart finds chunk 5 (0x53cdfe) and calls Create(word
  seed, size, symmetry). Our tooling (src/s3re/tooling/random_map_start.cpp):
  a pass-through on NewGame, only at the `-map` call site (return address
  0x493a56), swaps in such a map file; the player setup stays the `-map`
  file's (default 384-2-Lakes: one idle human, one computer). With
  `--seed N` the map seed is N too (Create seeds through SeedRandom).
  The `-map` start can't use a random map file directly: 0x4a67c0 reads
  chunk 3, which a random map lacks. 0x431ab0 renamed GameSettings::NewGame
  (inventory row, hook passthrough).
- `--skip-hooks NAMES` (S3RE_SKIP_HOOKS, hooks.cpp): leaves single
  replacements out, for bisecting a divergence or for counting our callees
  of an original function.
- `tools/check_random_map.py` (twice original, or `--against-original`;
  `--skip-hooks`; records call counts as source `random:SIZE,SYM seed N`;
  whole_game.py accepts `random:` sources).
- Results, seed 1234: original twice identical for 384,0 and 448,3 (1,000
  ticks). Original vs ours identical for 384,0, 448,3, 512,1, 640,2
  (1,500 ticks each; 193-194 replacements called, Create 1 call each).
  With `--skip-hooks GameWorld_Create` (original Create calling our
  helpers), 448,3, 300 ticks: identical; ExpandTerrain 29 calls,
  MarkTerrainEdge 4, BuildRegionsAndAreas 1. 14 functions got their first
  whole-game evidence from random maps: Create, ExpandTerrain,
  MarkTerrainEdge, GrowTerrain, ReplaceTerrainF3, the four Scatter*
  functions, Set/ClearDiagonalTileByteB, IsNeighbourhoodByte9Clear,
  IsGoodStartPosition, FindPlayerByField0C.
- New recording (main checkout, build/x86/bin/Debug/recordings/, not
  committed): `fun8-seed2024-input31.bin` (+ `.tsv` trace): map
  Map\MULTI\448-8-Fun.map (8 players: 1 human, 7 computers), game seed
  2024, synthetic input seed 31, 5,000 ticks, 1,913 commands (7 from the
  scripted player). Replay with ours identical to the recording (205
  replacements called); a replay with the original code (`--original`) is
  identical to the recording trace too (5,000 ticks, 156 hashed ticks), so
  original = ours. Call counts recorded as `replay:fun8-seed2024-input31.bin`.
  It brought no new whole-game function (all 205 were already seen; only 7
  commands came from the scripted player, the rest from computers).
- Whole-game evidence: 258 -> 272 of 458 verified functions seen (all 14
  new ones from random maps); tests only 200 -> 186.
- Suggested inventory note text (not written, to avoid conflicts with the
  struct refactor): GameWorld::Create 0x5166d0: "Whole-game: random maps
  384,0 / 448,3 / 512,1 / 640,2 (seed 1234, 1,500 ticks) identical to the
  original, 1 call each (re/whole_game_calls.csv)." ExpandTerrain
  0x519440 / MarkTerrainEdge 0x51a2e0: "Whole-game: random map 448,3 with
  the original Create (--skip-hooks GameWorld_Create, 300 ticks)
  identical, 29 / 4 calls."
- Why checks are slow (asked by the user): every game run is real time
  (about 15 ticks/s, 1,000 ticks a minute) plus about 20 s start/stop; a
  comparison needs 2 runs, a new recording 3-4. Only one game can run at a
  time, and the lock is not FIFO, so parallel agents queue behind each
  other's checks; `--test` runs (no lock, about 10 minutes for the full
  suite) also block, because game_lock waits for any S3.EXE. Emulator
  suites with ~20 workers compete for the CPU. This session waited about
  45 minutes for the lock. Ideas in TODO.md.

### 2026-11-09 (39): batch 29 (random map generator GameWorld::Create, 4 functions)

- Emulator suite rerun in full first (the open check from batch 28, on a
  copy of the batch-28 DLL): 454 functions, 0 mismatches, 1 with
  incomplete coverage: PlanSiteLevelling missed its "no earth to move"
  return (0x4da90a; its random flat windows almost never sat at exactly
  the planned height). site_window can now take the height; with that
  preset 95/95 blocks.
- Batch 29, all verified:
  - map_generation.cpp: GameWorld::ExpandTerrain 0x519440 (one growth step
    of a terrain into another via the temporary 0xf0) and
    GameWorld::MarkTerrainEdge 0x51a2e0 (edge tiles of a terrain -> 0xf3).
  - map_regions.cpp (new): GameWorld::BuildRegionsAndAreas 0x51a580 (the
    "area set-up"): walk regions, flag 0x80 around water objects
    0x0d..0x10, economy areas per owner, water bodies, wide regions.
    Called by Create and by the map loader 0x4fd540.
  - world_create.cpp (new): GameWorld::Create 0x5166d0 = the random map
    generator ("Erstelle Zufallswelt"; (seed, mapSize, symmetry); only for
    random maps, file maps use 0x4fd540). Phases as helpers: ResetTiles,
    SeedCoarseHeights (every 64th tile), RefineHeights (midpoint steps
    32..1, border 0), LowerHeights, LogBorderErrors, RaiseRidges (ridge
    walkers, two walks per peak, a 4-state machine) + MarkRidgeHeights +
    SmoothRidgeSlopes (or SmoothSlopes with symmetry), ClassifyTerrain,
    CarveStreams (streams 0x60..0x63, joining), RemoveStrayStreamTiles,
    ShapeTerrain (fixed terrain passes), PlaceObjects (94 + 8 + 5
    scatter calls, as tables), ScatterFish, ApplySymmetry (bit 1 point
    mirror, bit 0 diagonal), FinishTiles, BuildRegionsAndAreas. SeedRandom
    is called through the game's address so --seed still applies.
  - Pitfall found while debugging: 0x6bc60 is tile (1,1)+4, not (1,0)+4
    (0x4818 = one x and one y step); the first version mixed that up in
    the ridge walker and smoothing.
- Tests: in-process 9 new tests. Create: the whole 22 MB world, EAX, the
  DebugLog calls and S3.EXE's writable sections compared, on a zeroed
  world with the real offset table (built with 0x516530) for sizes
  128..768 and all symmetry bits, odd sizes 0..200, and a world full of
  garbage (sizes 128..384); no uninitialized bytes involved (identical
  everywhere, including garbage bytes Create doesn't write: tile +0xc,
  +0xd, +0x13, +0x17). Mutations (one height bit, one extra DebugLog)
  caught. Full in-process run after pulling the save tooling: 357/357.
- Emulator: ExpandTerrain, MarkTerrainEdge, BuildRegionsAndAreas all
  blocks covered, 0 mismatches. Create: 10 cases on 33..96-tile maps, 0
  mismatches, 338/415 blocks; the rest (inner height bands, the ridge
  walks, the 255 clamp, border errors) needs bigger maps (the in-process
  tests run those sizes).
- Whole-game: scripted-seed127.bin, 3,000 ticks, against the original:
  REPLAY OK (203 of 457 replacements called; BuildRegionsAndAreas 1 call
  from the map loader; Create, ExpandTerrain, MarkTerrainEdge 0, as
  expected for a file map). check_save.py "slot 1.sav" 2,000 ticks
  against the original: SAVE CHECK OK (201 called; Create 0).
- Save-check evidence of re/savegame_checks.md (17 functions) merged into
  the inventory notes.
- Tooling: a crashing in-process test now logs the faulting module+offset
  and the accessed address.

### 2026-11-09 (38): whole-game evidence on the progress page (tooling, parallel worktree)

- New `re/whole_game_calls.csv` (tools/whole_game.py): call counts per
  function and whole-game check source (`replay:scripted-seed127.bin`,
  `save:slot N.sav`), backfilled from the inventory notes and
  re/savegame_checks.md (including the 17 functions only the savegames
  called, and 0 rows for the others). GameWorld::SeedRandom
  (S3RE_HOOK_ALWAYS, not counted) has a hand-made row (runs at every
  game start).
- Of 454 verified: 253 identical in a whole-game check, 201 tests only,
  0 never measured. 34 of the tests-only ones are reachable from seen
  replacements through our own code (calls from our code bypass the
  counting thunk), e.g. PlanSiteLevelling via CheckBuildingSite, the
  area split/merge helpers via BlockTile/UnblockTile, std::string
  _Tidy_free: they may have run uncounted. Unconfirmed; a way to confirm
  would be counting calls inside our own code too.
- record_call_counts.py and check_save.py (identical runs only) write the
  CSV; savegame_calls.py --record imports old *-calls.tsv files.
  progress.py/progress.html: three verified colours, counts, a sortable
  and filterable list of verified functions with their counts, a history
  series for whole-game bytes. inventory.py stats shows the split, check
  validates the CSV (`whole_game.py fix-names` after renames).

### 2026-11-09 (37): whole-game checks from savegames (tooling, parallel worktree)

- `s3re_launcher --load-save FILE` starts the game directly from a
  savegame with the game's own code. No command-line switch exists, so
  the launcher passes a copy of the save as `-map` (menu state 0x14), and a
  pass-through hook on `Menus::CDrawThread::Update` (0x493880) calls
  `GameSettings::LoadSaveGame` (0x432b00, what the load dialog 0x47a860
  calls) and then does what `OnDialogResult` (0x494990) does for result 10
  (state 4, 0x496c90(1)). `CLogicThread::OnStart` then runs the loader
  0x508fa0 instead of Create/SeedRandom. `--exit-after-ticks` counts from
  the save's start tick when a save is loaded.
- `tools/check_save.py SAVE --ticks N [--against-original]`, savegame
  hash/backup safety net (the user's saves were never changed),
  `tools/savegame_calls.py` (new whole-game evidence), `tools/game_lock.py`
  (the shared game-run lock, now used by check_replay.py,
  check_determinism.py and check_save.py).
- Worktree pitfall: starting S3.EXE through a junction path loses the
  `DWM8And16BitMitigation` compatibility layer (registered per path), and
  DirectDraw fails (`DDERR_UNSUPPORTED`). The launcher now resolves the
  game directory to its real path.
- Determinism from a save (original twice, 500 ticks): first the rest hash
  differed; the cause is the alignment padding of the MapEffect records
  (uninitialized stack copies, never read). Trace v3 skips it. Then slot
  1, 2 and 3 (ticks 366,754 / 386,061 / 243,319) are all identical.
- Original vs ours, 2,000 ticks per save: identical for all three, with
  batch 27 and again with batch 28. 208 replacements called; 17 of them
  had no whole-game evidence before (MapEffect make/add/links functions,
  RemoveSettlerFromLists, MakePileEmpty, Render::PackColour, ...). Of
  batch 28 only CheckBuildingSite ran (2,690 calls in slot 3, identical);
  the other four building-site functions were not called.
  Details and the list: `re/savegame_checks.md`. Not yet merged into the
  inventory notes (next step).

### 2026-11-09 (36): new direction (core gameplay, no network); batch 28 (building sites, 5 functions)

- User decision recorded at the top of "Current focus": skip network
  code, focus on core gameplay in the order building sites, Create,
  UpdateSettlers callees, UpdatePhase2, UpdateSettlers. x87 proposal there.
- Emulator suite rerun in full first (batch 27 left 2 functions reported
  with incomplete coverage from a stale run): 449 functions, 0 mismatches, again 2 with incomplete coverage (String_FromSubstrDefault 0x408dde, CStaticText_Construct 0x41c4c6). Real but harmless: both paths need the static empty string's count byte (S3.EXE 0x65539f) below 0xfe, and in a full run an earlier spec's `null_string_count` preset leaves it at 0xfe+ (globals keep what earlier cases wrote). Fixed by giving both specs `globals=null_string_count`; alone, after the String_ group and in the C* group they are fully covered.
- Batch 28, all verified, src/s3re/game/building_sites.cpp (new):
  - GameWorld::CheckBuildingSite 0x4d99e0: (x, y, type, player, mode):
    whether a building fits and how much earth it needs (size class
    1..10, the preview marks); mode 1 also places the site (levelling
    slot, footprint flags, stakes, planned heights); mode + 2 for
    unowned land (start castles). Race groups: ground 0x10 (races 0/3),
    +0x40 (race 1), +0x90 (race 2); mines only need mountain under their
    tile; 0x20/0x21 need water within ring 20.
  - GameWorld::PlanSiteLevelling 0x4da440 (was EvaluateBuildingFootprint):
    the 31x31 levelling plan (slopes 5/7 by direction) and its earth amount.
  - GameWorld::AddBuildingSite 0x4dd280: the construction-site record
    (materials, door, two empty piles, work centre per type).
  - GameWorld::IsFreeCastleSite 0x508420 and IsGoodStartPosition
    0x5081a0 (random start positions: land/resource counts with demands
    that drop after 30000/60000/100000 attempts).
  - Building layout tables in S3.EXE at 0x6aa170 (2400 bytes per race*57
    + type; marker lists 0x80000000..0x80000005) documented in
    re/structs.md, with new tile flag 0x04 (being levelled), objects
    0x70/0x71 (site centre/stakes), building +0x07 race, +0x19/+0x1a
    materials, +0x1b, +0x1c, +0x22 levelling slot, +0x28, +0x30 earth,
    +0x32 player countdown (+0x84), player +0x84, the levelling jobs at
    world+0x1196024 and the scratch grids.
- Tests: in-process 10 new tests (match + mutation for each), on
  realistic worlds (smooth heights, mountains, water, four players'
  land with areas, sparse flags; evolving: placed sites change later
  cases). Full in-process run: 347/347. Emulator: 5 new specs,
  0 mismatches, every live block covered (CheckBuildingSite: 8 dead
  blocks of the size-class chain only reachable with amount 0, and the
  empty-border path, which no layout has, exempt). Self-test 21/21.
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK (run twice). Calls: CheckBuildingSite 2,745,
  AddBuildingSite 2, IsFreeCastleSite 2; PlanSiteLevelling counted 0
  (only called from our CheckBuildingSite, which calls ours directly);
  IsGoodStartPosition 0 (this map has fixed start positions).
- Game runs now use the lock directory
  Settlers3Ultimate/s3re-game.lock (another agent runs games in
  parallel in a worktree): mkdir it (retry every 30 s), owner.txt with
  "batch-agent" and a time, no S3.EXE running; remove it after the run;
  stale after 2 hours with no S3.EXE.

### 2026-11-09 (35): batch 27 (44 functions: assertions, lobby buffers, strings, small helpers)

- Batch 27, all verified (none unverified):
  - win32_errors.cpp (new): Win32::CProgrammingError 0x614eb0,
    CAssertmentFailed 0x612a20 and its text function (vf01, our name
    Describe) 0x612e80, the assert thrower 0x6119b0 (48 callers),
    COutOfMemory 0x6124a0 and the out-of-memory thrower 0x610960 (was
    "ThrowError"; it is called where operator new returned null).
  - msvc6/string.cpp: the twin of ConcatChars 0x437480, the
    default-allocator substring constructor 0x408cc0 (allocator byte = low
    byte of the source's address), compare(const char*) 0x40a0e0.
  - lobby_memory_block.cpp (new): Lobby::CMemoryBlock Size 0x601270 (a
    4-byte body: Detours hooks it through the nop padding), Reserve
    0x601280 (+8 keeps the unrounded size, sic), Append 0x601170,
    AppendBytes 0x6011c0, At 0x600ee0, Equal 0x600ea0, copy constructor
    0x600e40, destructor 0x600e70; CLMGeneric SetHeader 0x603270,
    constructor 0x601e80 (24 callers), Equal 0x6027d0, destructor 0x602190.
    The catch(...) blocks that log and rethrow are SEH filters with the
    same DebugLog.
  - ui_static_text.cpp (new): CStaticText constructor 0x41c3e0 and
    SetStyle 0x41ca60 (both needed by the tooltip opener 0x418550).
  - watcom/string_vector.cpp (new): WCValVector<String>::resize 0x40a580
    (23 callers). Decided: the "allocator byte from the caller's stack"
    was a misreading; it is the low byte of the argument n (same stack
    slot), so it is reproduced exactly.
  - flood_fills.cpp: FloodSetWord16RoomyTiles 0x51d2b0 (word +0x16 over
    marked tiles with room, step 0x51d000); this makes the area set-up
    0x51a580 READY.
  - ai_evaland.cpp (new): the shared land evaluation object's destructor
    0x553fe0 and Release 0x553dc0 (refcount 0x7e0568, instance 0x7e056c).
  - framework_small.cpp (new): Watcom intrusive list destructor 0x60b5a0
    (30 callers) and insert 0x60b430, int vector constructors 0x4376b0 /
    0x437730 and destructors 0x414590 / 0x458590, Lobby_ScaleBy4 0x44c450,
    CWin32Handle::Set 0x60bf00, CMemoryBlock::operator= 0x600e80,
    CRegistryKey 0x616430, CEvent 0x60c510, 0x5da3a0, wide-string helpers
    0x418fb0 / 0x428880, GfxData frame/animation pointers 0x5fac70 /
    0x5faaf0, CLMMessageBlock copy constructor 0x601470.
- Rule (CLAUDE.md, "Allocator bytes of MSVC 6.0 strings"): an allocator
  byte that shares a stack slot with an argument is reproduced; one from an
  uninitialized local is 0 when the string is freed before returning, and a
  documented known difference (ignore_bytes + self-test pairing) when it
  survives (only 0x612e80's result so far).
- Emulator harness: `Spec.max_instructions` (the whole-map roomy flood
  needs more than 20M instructions in our debug build). New self-test
  pairing for the ignored allocator byte.
- Known differences: the throwers have no exception frame (the
  out-of-memory message string leaks, as with ThrowDataError); Describe's
  result allocator byte; Reserve/AppendBytes log during the first SEH pass
  instead of in the handler.
- Tests: in-process 337/337; emulator: 44 new specs, 0 mismatches, every
  live block covered (dead inlined-string blocks and catch handlers exempt
  with reasons). Full suite 446 functions, 0 mismatches; it reported 2
  with incomplete coverage, from a run that started before the last
  exempt/cover_until fixes of this batch; every batch-27 spec group
  (Lobby_, String_, Win32_, CStaticText, StringVector, AI_, Flood, Gfx,
  WCI, UI_) was rerun afterwards with full coverage, but the full suite
  was not rerun - do that first next session. Self-test 21/21.
- Whole-game check (scripted-seed127.bin, 3,000 ticks, against the
  original): REPLAY OK, 205 of 448 replacements called; 21 of the 44 new
  ones (GfxData_AnimationPointer 13.6M, WCIntVector_Construct 2,547,
  CWin32Handle::Set 218, FloodSetWord16RoomyTiles 73, String_CompareText
  22, ...). Not called: the assertion/out-of-memory classes and throwers,
  most CMemoryBlock/CLMGeneric members (lobby), CStaticText::SetStyle,
  CEvaLand destructor, and a few helpers. Counts in the inventory notes.
- Frontier (all game code): READY 1,305 -> 1,318, NEAR 1,057 -> 1,074;
  simulation range READY 253 -> 253, NEAR 60 -> 59.

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
