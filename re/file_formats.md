# Map and savegame file formats

Findings of the naming pass of session 50 (2026-11-24), read from the code
(dumpbin disassembly, Ghidra) and checked against the game's own files: a
parser written from this recomputed the stored checksums of
`Map\MULTI\384-2-Lakes.map` and `Save\slot 1.sav` and decoded sensible
chunks. Not yet reimplemented: treat the details as "read from the code"
until a verified loader confirms them. Function names are in
`re/functions.csv` (notes start with "Named (session 50, ...)").


Sources: Ghidra decompilations of 0x4a55e0..0x4a6d40, 0x4fd540, 0x508fa0,
0x509980, the Win32/Mapping.cpp file class, plus a read-only parse of
`Map/MULTI/384-2-Lakes.map` and `Save/slot 1.sav`. My parser recomputes both
stored checksums correctly and decodes the chunks into plausible data, which
confirms the container, checksum and XOR scheme below.

## Container (Dirk::CMapFile, Dirk/SaveGames.cpp)

Maps (`.map`, file type 10) and savegames (`.sav`/`.mps`, type 11) use the same container:

```
+0  uint32 checksum    over bytes 8..end, in dwords: c = ((c << 1) ^ w) | (c >> 31)
                       (rotate left by one, with the XOR applied before the carried-in bit is ORed in)
+4  uint32 file type   10 = map, 11 = savegame ("LOAD: Invalid filetype %i %i" otherwise)
+8  chunks, back to back:
      uint32 key       = subIndex << 16 | type
      uint32 size      whole chunk including this 8-byte header
      byte   data[size-8]   obfuscated
```

- **There is no compression.** The bytes after each chunk header are XORed with
  a rolling key. Reading (GetChunk 0x4a60f0): `k = type & 0xff; for each byte b:
  b ^= k; k = (k << 1 ^ b_plain) | (counter < 0 ? 0xff : 0)`, where
  `counter` starts at 0x628 and grows by 0xc5 per byte. The sign bit only
  matters after about 10.9 million bytes in one chunk. Writing (WriteChunk 0x4a6540)
  is the inverse. WriteChunkData 0x4a66d0 starts its counter at 0x18a
  instead (it counts from the payload, not the header); that difference
  only shows up in chunks over 10 MB, which never occur.
- The checksum is checked on open unless mode bit 4 is set. It is written on Close in write mode, by seeking back to offset 0.
- The map file has extra chunks with nonsense keys (type 977/sub 2704,
  1682/1084, 758/3413, 1033/2194; 1.4 to 2.8 KB), and it ends with a 0/0 chunk of size 8.
  The game never asks for them. They are probably decoys or editor data (not analysed).
- CMapFile object (0x134 bytes): +0 vtable 0x658770; +4 memory-only flag;
  +5 open flag; +8 CMapFileStream (byte, Win32::CFile at +0xc, error code
  +0x18); +0x1c std::string path; +0x2c open mode; +0x30
  WCValSkipListDict<uint,void*> key -> chunk buffer (key slot at +0xa8); +0xb0
  WCValSkipListDict<uint,int> key -> reference count (key slot +0x128); +0x130 checksum.
  Chunks are cached and reference-counted: GetChunk/ReleaseChunk pairs. AddChunk puts a
  memory-only chunk into the cache, which is how random maps work.
- Open modes: 1 = read (OPEN_EXISTING, share read), 2 = write
  (CREATE_ALWAYS; the game only ever writes type 11), 4 = skip the checksum check.

## Map chunks (CGameData::LoadMap 0x4fd540 and the menus)

All multi-byte fields are little-endian. LoadMap assembles them byte by byte.

| type | content |
|---|---|
| 1 | map info: +8 kind (0 = map file, 2 = random map), +0xc number of players/start positions, +0x10 start goods (random_map_start.cpp) |
| 2 | 98 bytes in 384-2-Lakes: player races? (NewGame uses "chunk 2 race"; not analysed here) |
| 3/sub | team setup: name text, then from +0x29 two bytes per player (party, type; type 2 = computer). Read by CMapFile::ReadPartySetup 0x4a67c0 |
| 4 | 131,084 bytes = 12 + 128*128*8: probably the preview picture (not read by LoadMap) |
| 5 | random map parameters (size, field0c, symmetry, seed), only in memory |
| 6 | landscape: +8 uint32 size, then size*size records of 6 bytes: height, terrain, object, owner slot, flags, resources. They go to tile +4, +6, +7 (also +0xe seen_object), +8 (owner mapped through slotToPlayer; flags gets 0x40 if owned; an unmapped slot gives owner 0xff and search_mark 1), +0xa, +0x11. Objects 0x6f..0x72 on terrain < 8 become +0x9e. The inner loop steps 0x4800 bytes, one row of the tile array |
| 7 | settlers: +8 count, 6-byte records: slot, type, x(16), y(16); type 0x29 -> CreateSevenTileUnit, 0x2b/0x2c -> SpawnSettlersOnWater 0x50dc10 (ships), else CreateSettler |
| 8 | buildings: +8 count, 12-byte records: slot, type, x, y, then soldier counts in nibbles (towers 0xf/0x10/0x12 get their garrison). First pass: "Deleting Building-Bits" for slots without a player. Without this chunk: AddPlayerTowns 0x5074b0 |
| 9 | piles: +8 count, 8-byte records: x, y, good, amount, flag |
| 10 | victory conditions, 0x77 bytes -> world+0x15394b1 (sets 0x7deb54 if byte +0x44) |
| 0x40 | presence only: g_worldUpdateDisabled (0x7deb55) = 1 |

After the chunks, LoadMap calls UnblockCastleTiles and then the mission fix-ups
(g_campaignMission 0x31, 0x32, 0x35, 0x3b/0x3d, 0x46: terrain tweaks, extra units,
race changes "xxx race changed").

## Savegame chunks (CLogicThread::SaveGame 0x509980 / LoadSaveGame 0x508fa0)

The original names come from SaveGame's out-of-memory labels (`Win32::ThrowOutOfMemory(label, file, line)`).

| type | label | content (world = CGameData) |
|---|---|---|
| 0 | - | header 0x120: +8 version (0xa0 in slot 1), 0x7a8000, 0x7a8004, mission-1, local player's +4, time(), +0x20 map name (string from 0x436a60). LoadSaveGame only reads the version (gates: <0x82 no 0x18/0x19; <0x86 no 0x1c; <0x89 clears player+8; <0x96 no 0x1d; <0x99 no 0x1f, timeout 60) |
| 0x14, 0x1a, 0x1b, 0x1e | - | game setup, written by GameSetup 0x433490 (read by GameSettings::LoadSaveGame 0x432b00, not analysed) |
| 1 | general | +8 world+0x2d254, +0xc +0x2d258, +0x10/+0x14 camera_x/y (+0x2d25c/+0x2d260), +0x18 map size, +0x1c..+0x20 RNG state (3 words at +0x2d2dc), +0x24/+0x28 +0x110a57c/+0x110a580 (+0x2c +0x110a584, ignored on load: forced to 0x3f), +0x30 0x7dfd4c, +0x34 g_gameTick, +0x38 20 player-info records of 0x134 bytes (2 dwords, name chars 0x100, 11 dwords) -> 0x7acfa8 table, +0x1848 active player count (0x7acfa4), +0x184c g_gameOutcome, +0x1850 mission-1, +0x1854/+0x1858 world +0x2d304/+0x2d308, +0x185c world+0xde7444 |
| 2/0, 2/1 | events | two dwords + 3000 dwords each: pending_events (+0x2f990, counts +0x32870/4) and events (+0x32878, counts +0x366f8/c) |
| 3/row | - | one chunk per tile row (sub = row): size*24 raw tile bytes (WriteChunkData) |
| 4 | sectors | walk_region_high_water +0xe114d8, +0xe115dc (sector count), +0xe115e0, +0xe125e4, 0x40 + 0x400 + 0x800 dwords, then (count+1) * 0x2e0-byte economy sectors (areas, +0xe145e8) |
| 6 | player | local_player +0x2d2d4, player_races (20 dwords), players (20 * 0x148) |
| 7 | settler | count +0x12a5e5c, (count+1) * 0x40 |
| 8 | buildings | count +0x1499e60, (count+1) * 0x3a |
| 9 | piles | count +0x1238858, (count+1) * 0xe |
| 10 | diggertab | footprint_scratch (2 * 0x3c1 at +0x1222ed4, +0x1223295) and site_cache (0x8ceb0 at +0x1196024) |
| 11 | - | minimap (0x15200 bytes at +0x1223656) |
| 12 | activesettlers | player_lists (10000 dwords) + player_list_lengths (200 dwords) |
| 13 | trade | trade_high_water +0x110a578, (n+1) * 0x5a trades |
| 14 | timer | effects_high_water +0x67440, (n+1) * 0x14 records at +0x36700 |
| 15 | messages | notifications (0x12c0 bytes at +0x2d320) + 0x13b0 bytes at +0x2e5e0 |
| 16 | warmap | presence_a/presence_b (2 * 0xb400 at +0xde7448/+0xdf2848) |
| 0x15..0x17 | - | computer players (0x551220 per active AI at CLogicThread+0x350; AI load 0x596380) |
| 0x18 | GCVictoryConditions | 0x77 bytes, world+0x15394b1 |
| 0x19 | GCPlayerColors | 40 bytes, g_playerColourIndex 0x7ace64 |
| 0x1c | GCProdStat | 0x2d000 bytes at world+0x38 ("ProdStatArray", statistics) |
| 0x1d | GCNewGameObjective | +0x1195dcc, +0x1195dd0 |
| 0x1f | GCNewGameObjectiveTimeout | world+0x2d248 |

After loading: UnblockCastleTiles. Every economy sector that no tile references (tile +0x14) gets
id byte 0xff ("LOAD: Dropped eco %i"). Then world+0x2d278 (minimap_ready) = 1.

## Load call trees

```
CLogicThread::OnStart 0x53c9e0
  savegame: CLogicThread::LoadSaveGame 0x508fa0 (CMapFile from GameSettings 0x7a7a18 / 0x412340)
     CMapFile::GetChunk 0x4a60f0 / ReleaseChunk 0x4a6310   (chunks 0..0x1f)
       CMapFileStream::Seek/Read/HasError -> Win32::CFile::Seek/Read (SetFilePointer/ReadFile)
       WCSkipNonTempBase::base_find/base_insert/base_remove (chunk cache)
     std::string::assign_cstr_or_empty 0x413d70 (player names)
     CGameData::UnblockCastleTiles 0x4dfa60
  map: [random: GenerateRandomMap 0x5166d0 (chunk 5)]
     CGameData::LoadMap 0x4fd540 (CMapFile*, slotToPlayer)
       GetChunk/ReleaseChunk (6, 8, 7, 9, 10, 0x40)
       PrepareLoadedTerrain 0x519540 -> ReplaceTerrainNextTo/ReplaceTerrainType
       BuildRegionsAndAreas 0x51a580
       AddBuildingSite 0x4dd280, BuildingDone 0x4dbc50, CreateSettler 0x50c9e0, EnterBuildingSlot 0x50ccb0
       CreateSevenTileUnit 0x50dd20, SpawnSettlersOnWater 0x50dc10, CreatePile 0x504420
       [no chunk 8] AddPlayerTowns 0x5074b0 -> PlaceTown 0x506cf0, IsGoodStartPosition, IsFreeSmallTowerSite
       0x4351e0/0x49fec0 (player colours), UnblockCastleTiles
     CGameData::InitAfterMapLoad 0x4fd020 -> InitPlayerInfo 0x4fd0d0, BuildMinimap, ...
  ChooseNewGameObjectives 0x4ffbc0
File opening happens in the menus: GameSettings::LoadSaveGame 0x432b00 -> CMapFile::Open 0x4a5c80 (mode 1)
  -> CMapFileStream::Open -> Win32::CFile::Open 0x619610 (CreateFileA; CFileNotFound for missing files)
```

## Corrections of existing names and notes

- **0x4a5f40 `WCValSkipListDictIter_unsigned_int_void::WCValSkipListDictIter_unsigned_int_void`**
  is `Dirk::CMapFile::Close`. It only builds such an iterator as a local, which is why the survey took it for its constructor.
- **0x4a55e0..0x4a5c80 `Pathing_sub_*`**: not pathing. They are the file stream of
  CMapFile and CMapFile::Open. `source_files.py which 0x4a55e0` says Dirk/Pathing.cpp, but
  the code belongs with Dirk/SaveGames.cpp (CMapFile). The range boundary in
  re/source_files.csv is probably off there.
- **LOGBOOK roadmap item 5** ("Win32 file wrappers 0x60bab0/0x60bbe0/0x60be20 and
  probably a decompressor 0x4a57b0/0x4a5930/0x4a5a60") is wrong on both counts.
  0x60bab0/0x60bbe0/0x60be20 are the Watcom skip list (WCSkipNonTempBase find/insert/remove)
  behind CMapFile's chunk cache. 0x4a57b0/0x4a5930/0x4a5a60 are the stream's Read/Seek/HasError.
  There is no compression, only the XOR obfuscation. The real Win32 file wrapper is Win32::CFile in Mapping.cpp (0x619610..0x61a080).
- **g_strengthDivisor (0x7acfa4)** is the number of active players. InitPlayerInfo 0x4fd0d0 counts
  them, and chunk 1 +0x1848 saves the value. So UpdateEnvironment's 0x600 / (value + 4) means
  "soldier strength scaled by player count". Rename, e.g. `g_activePlayerCount`.
- **g_playerTallies (0x7acfd4, 0x44-byte records)** is +0x2c into a 20-entry, 0x44-byte
  player-info table at **0x7acfa8**: +0 dword, +4 dword, +8 std::string name,
  +0x18 player+0xc, +0x1c player index, +0x20..+0x40 counters (the tallies are
  rec+0x2c..+0x40). Savegames store it as 0x134-byte records in chunk 1 (name as 0x100
  chars). Re-base the global to 0x7acfa8 with the real record layout.
- **Original subsystem names from the save labels**: `effects` / `AllocTable36700Record`
  (+0x36700, 0x14-byte records) is the **"timer"** table (fits CountDownTimers 0x4fee00).
  presence_a/b is the **"warmap"**. player_lists is **"activesettlers"**. site_cache +
  footprint_scratch is **"diggertab"**. footprint_scratch is saved, so it is not scratch
  (digger = levelling builders). pending_events/events are **"events"**. The areas block is
  **"sectors"** (economy sectors, "eco"). Chunk 1 is "general". notifications + 0x2e5e0 are "messages".
- 0x618250 `Folded_GetMember4` is a linker-folded `mov eax,[ecx+4]; ret`. CFileMapping::Create
  uses it as the CWin32Handle handle getter. 0x49f4d0 `Net_sub_49f4d0` is the folded
  `mov eax,[ecx+0x10]; ret` that CMapFile::GetError uses as the stream's error getter.
- 0x4a6b00/0x4a6b20/0x4a6d40 are WC skip-list virtuals shared by every `<unsigned int,*>`
  instantiation (also the DirectSound effect dictionaries), not SaveGames code.
- 0x60bca0 (skip-list levels) uses the CRT `rand()`. It doesn't touch the simulation RNG,
  but chunk-cache inserts do consume CRT rand values (relevant only if anything
  else in the game depends on CRT rand).
- CLogicThread::LoadSaveGame/SaveGame (0x508fa0/0x509980) are CLogicThread methods (world =
  this+0x3a0), although they sit in Volker/GameDataSave.cpp.
- Not proposed (already named fine or out of scope): the CMapFile/WC destructors, the CTimer,
  CSemaphore and CMutex constructors, CWin32Waitable vf01/vf02 (wait-for-many helpers via
  0x60da30/0x60dcd0, not analysed), FindNearest_a/_b, and the Lobby Watcom vector helpers
  0x60b0c0..0x60b2e0 (network, skipped by project policy).
