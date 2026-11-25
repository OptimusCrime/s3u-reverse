# Map and savegame file formats

Findings of the naming pass of session 50 (2026-11-24), read from the code
(dumpbin disassembly, Ghidra) and checked against the game's own files: a
parser written from this recomputed the stored checksums of
`Map\MULTI\384-2-Lakes.map` and `Save\slot 1.sav` and decoded sensible
chunks. The container layer (stream, chunk scan, decoding, chunk cache) is
reimplemented and verified since session 51 (see "Verified container
(session 51)" below); the chunk contents are still "read from the code"
until verified loaders confirm them. Function names are in
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

## Verified container (session 51)

`src/s3re/game/fileformats/` (map_file.cpp, map_file_stream.cpp) replaces
CMapFileStream (0x4a55e0..0x4a5a60) and CMapFile's GetChunk, ReleaseChunk,
AddChunk, SetMemoryOnly, GetError and GetChecksum. The in-process test
`MapFile_AllGameFiles` opens every file under `Settlers3Ultimate\Map\`
(178, recursively) and `Save\` (3) read-only with the original Open and reads
every distinct chunk key twice with the original and our GetChunk:

- All 181 files have the file type 10 or 11 and a valid checksum (our
  ChecksumStep over the dwords from offset 8; trailing bytes that don't fill
  a dword are not checksummed). 4,651 distinct chunks, 395 MB decoded, none
  cut short by the end of a file; original, ours and our own decoding of the
  raw bytes (`DecodeChunkData`) agree byte for byte.
- GetChunk finds the **first** chunk with the key. The decoding key starts at
  the low byte of the type *argument* (the same as the stored key's, since
  the keys match). A chunk header with a size below 8 would make the scan
  step backwards or loop forever (size 0); no file of the game has one.
- CMapFileStream (CMapFile + 8, 0x14 bytes): +0 a "rethrow" flag (every
  caller passes 0 to the CMapFile constructor), +4 Win32::CFile (0xc bytes),
  +0x10 the error code. Each member wraps its CFile call in
  `catch (Win32::CHResult*)` (Open first `catch (Win32::CFileNotFound*)`,
  error 2). Read/Write return -1 once an error is pending and don't touch
  the file then; Seek and GetSize call the file even with an error pending;
  GetSize returns -1 whenever an error is pending afterwards; Seek after a
  caught error returns 0x4a59a8 (the catch continuation, EAX after the CRT's
  _JumpToContinuation 0x61eb95).
- The chunk cache's skip-list inserts draw node levels from the CRT rand()
  (through g_skipListRandomBits 0x7e8e24 / g_skipListRandomBitsLeft
  0x7a4030), so loading a file consumes CRT random numbers; the simulation
  RNG is not involved.
- CMapFile::Open (0x4a5c80) and the rest of the container are replaced
  since session 52, see below.

## Verified open, close and write path (session 52)

`src/s3re/game/fileformats/map_file_io.cpp` replaces the constructor
(0x4a5a70), Open (0x4a5c80), Close (0x4a5f40) and the writers WriteChunk
(0x4a6540), WriteChunkWithTail (0x4a6600) and WriteChunkData (0x4a66d0).
In-process tests (`map_file_io_tests.cpp`) compare them with the original on
every file under `Map\` and `Save\` (read only), on random containers and on
savegames written into a temporary folder (the written bytes are compared,
then reopened with the checksum check); `MapFile_AllGameFiles` now opens
through our Open.

- Constructor: the path's allocator byte is the low byte of the `rethrow`
  argument (its stack slot); `mode` (+0x2c) is left as it was.
- Open: `path = *arg` is std::string assign(x, 0, npos) (inlined; ours calls
  0x401d60). `open` = 1 and `mode` are set before anything is read. Read
  mode (bit 0) opens the stream with the caller's mode, write mode (bit 1
  without bit 0) with mode 3 and writes a placeholder checksum (the field's
  value) and type 11. A bad file type does not end Open (see above). The
  checksum pass reads 0x40000 bytes each time and checksums the bytes it
  expected from GetSize.
  **Known difference:** when a read fails (a file that cannot be opened,
  an I/O error) the original uses uninitialized stack memory for the file
  type and the checksum buffer; ours uses 0 and zeros. A failed open is
  reported either way, only the error code (E_INVALIDARG or the stream's)
  can differ. The tests zero the stack below their frame, so the original
  reads zeros as well, and then compare.
- Close: frees the cached chunk buffers, clears both dictionaries
  (0x60bd80), and only in mode 2 exactly (not 3) recomputes the checksum
  from offset 8 and writes it at offset 0; it does not keep that checksum
  (+0x130 = 0 afterwards).
- WriteChunk / WriteChunkWithTail ignore the sub index (the caller filled
  in the header); WithTail seeks to the end before allocating, WriteChunk
  after copying. WriteChunkData writes the 8-byte header itself (key,
  size + 8), then the payload encoded from its first byte with the counter
  at 0x18a, while GetChunk decodes payload byte j with the counter at
  0x628 + j * 0xc5. Only the counter's sign bit matters (it is ORed into
  the key as 0xff), and both reach it after about 10.9 million bytes, at
  slightly different bytes; so a WriteChunkData chunk over ~10.9 MB would
  not read back correctly (none is that big). `MapFileWrite_HugeChunks`
  writes 11.2 MB chunks with all
  three writers and compares the files.

Session 55 added the destructor `~CMapFile` (0x4a5b50, same file): it
closes the file if it is still open (so a savegame left open gets its
checksum), clears both dictionaries and destroys the path and the stream's
file handle. A memory-only file (AddChunk, random maps) is never open, so
its cached chunk buffers are not freed by the destructor (the dictionary
nodes are): unless someone calls Close first, they leak, in the original
as well.

## Map chunks (CGameData::LoadMap 0x4fd540 and the menus)

All multi-byte fields are little-endian. LoadMap assembles them byte by byte.

| type | content |
|---|---|
| 1 | map info: +8 kind (0 = map file, 2 = random map), +0xc number of players/start positions, +0x10 start goods (random_map_start.cpp) |
| 2 | per map slot a 45-byte record: +0 dword (0xff in the multiplayer maps; probably the race, "chunk 2 race"), +4 start x, +8 start y (384-2-Lakes: (203, 304) and (121, 73)); read by the menus, not by LoadMap. The rest of a record was zero in the maps looked at |
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
| 0 | desc | header 0x120: +8 version (g_versionNumber, 0xa0), +0xc g_buildInfo (2 dwords), +0x14 mission-1, +0x18 the local player's race (Player +4), +0x1c time(), +0x20 map name (GameSettings::MapName 0x436a60, strcpy). LoadSaveGame only reads the version (gates: <0x82 no 0x18/0x19; <0x86 no 0x1c; <0x89 clears player+8; <0x96 no 0x1d; <0x99 no 0x1f, timeout 60) |
| 0x14, 0x1a, 0x1b, 0x1e | - | game setup, written by GameSettings::SaveSetup 0x433490 (read by GameSettings::LoadSaveGame 0x432b00, not analysed); see "Verified savegame writer" |
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
| 0x15..0x17 | - | computer players: AI::SaveGlobals 0x551220 once per computer player at CLogicThread+0x350, each time the same three global tables (0x7e37d0 0x20f8 bytes, 0x7e5ef8 0x1ad8, 0x7e58c8 0x51c); AI load 0x596380 |
| 0x18 | GCVictoryConditions | 0x77 bytes, world+0x15394b1 |
| 0x19 | GCPlayerColors | g_playerColourIndex 0x7ace64: 20 indexes copied into the buffer, but the header's size is 40 *with* the header, so the file holds 16 |
| 0x1c | GCProdStat | 0x2d000 bytes at world+0x38 ("ProdStatArray", statistics) |
| 0x1d | GCNewGameObjective | +0x1195dcc, +0x1195dd0 |
| 0x1f | GCNewGameObjectiveTimeout | world+0x2d248 |

After loading: UnblockCastleTiles. Every economy sector that no tile references (tile +0x14) gets
id byte 0xff ("LOAD: Dropped eco %i"). Then world+0x2d278 (minimap_ready) = 1.

## Verified savegame loader (session 52)

`CLogicThread::LoadSaveGame` (0x508fa0) is replaced
(`src/s3re/game/logic/load_save_game.cpp`; chunk layouts as packed structs in
`src/s3re/game/fileformats/savegame_chunks.h`). The table above is confirmed
by the in-process tests (every savegame under `Save\`, loaded by the
original and ours into two worlds, and synthetic savegames for every
version gate), with these details:

- Order: chunk 0 (only +8 version is read; the chunk is **never released**,
  it stays in the cache), 1, 2/0, 2/1, 3/row for rows 0..map_size-1 (the
  map size from chunk 1, re-read each row), 4, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, DebugLog "Savefile Version : %d", then the versioned chunks
  (0x18 + 0x19 from 0x82, 0x1c from 0x86, 0x1d from 0x96, 0x1f from 0x99;
  older: colour indexes 0..19 = i, ProdStat zeroed, objectives zeroed,
  timeout 60), Player +8 cleared for every player below 0x89,
  UnblockCastleTiles, the eco drop, minimap_ready = 1. Every other chunk is
  released right after its copy.
- Chunk 1 ("general"): camera_drawn_x/y are not read; both are set to
  10,000,000 (forces a redraw). +0x2c (minimap_cycle) is not read; it is set
  to 63. The RNG state is three words (+0x1c..+0x21). +0x30 goes to
  g_lastDrawnTick (0x7dfd4c), +0x34 to g_gameTick. The player info table
  g_playerInfo (0x7acfa8, 20 x 0x44 bytes): +0 and +4 from the record's
  first two dwords, the name (`std::string` at +8) assigned from the
  0x100 characters (assign_cstr_or_empty 0x413d70, up to the first zero),
  +0x18..+0x40 from the record's +0x108..+0x130.
  +0x1848 -> g_strengthDivisor (active players), +0x184c -> g_gameOutcome,
  +0x1850 + 1 -> g_campaignMission.
- Chunk 2/1 ("events"): 3000 dwords are copied into the event ring like
  into the pending-event ring, but the event records are 16 bytes: only
  750 of the 1000 events (12,000 of 16,000 bytes) come from the file.
- Table chunks (7 settlers 0x40, 8 buildings 0x3a, 9 piles 0xe, 13 trades
  0x5a, 14 timer 0x14 bytes): +8 the high-water mark, then (mark + 1)
  records from index 0. Chunk 4 copies (area_high_water + 1) area records.
- **Chunk 0x19 of the game's own savegames is 40 bytes** (16 colour
  indexes), but the loader copies 40 data bytes (20 indexes): colours
  16..19 come from the 8 heap bytes after the chunk buffer (the slack of
  its 48-byte heap block, contents left from earlier allocations). Ours
  reads the same bytes; the in-process test leaves those four indexes out
  for the game's files (the two runs' heaps differ) and compares all 20 on
  synthetic savegames. The other chunks of the three savegames are at
  least as long as the loader reads.
- g_victoryProtectActive (0x7deb54) = 1 when the loaded victory conditions
  have protect_players (+0x44) set (only on the >= 0x82 path).
- Eco drop: tiles 0..map_size-1 in both directions mark their area (tile
  +0x14) when 0 < area < 4096; every unmarked sector of the 4096 gets
  player byte 0xff, with "LOAD: Dropped eco %i" when it was not 0xff
  already.
- In the three savegames of the installation (version 0xa0, 704 x 704, 732
  chunks) every chunk the loader asks for exists.

## Verified savegame writer (session 55)

`CLogicThread::SaveGame` (0x509980, `const std::string* name` ret 4, a bool
in AL: false when the file had an error before or after Close) is replaced
(`src/s3re/game/logic/save_game.cpp`), with `GameSettings::MapName`
(0x436a60, `src/s3re/game/ui/game_settings.cpp`). It creates
g_gameFolder + "Save" and + "Save\Multi" (_mkdir), opens g_gameFolder +
"Save\" + name in mode 2 (a CMapFile on its stack) and writes, in this
order: 0, the setup (0x14, 0x1a, 0x1b, 0x1e), 1, 2/0, 2/1, 3/row for
rows 0..map_size-1 (WriteChunkData, the map size re-read per row), 4, 6,
7, 8, 9, 10, 11 (WriteChunkData), 12, 13, 14, 15, 16, 0x15..0x17 per
computer player, 0x18, 0x19, 0x1c, 0x1d, 0x1f. Each chunk is built in a
game-heap buffer (operator new, "out of memory" with the chunk's label
and the GameDataSave.cpp line otherwise) and freed after its write; the
tables (7, 8, 9, 13, 14), the events and the sectors go out as a header
buffer plus the world records as WriteChunkWithTail's tail.

- **g_gameFolder** (0x7a7c80, a std::string: the folder prefix of the
  game's files) is where the paths come from; the tests point it at
  `%TEMP%\s3re-savegame-test\` (no hook needed), so they never write
  under Settlers3Ultimate.
- **Bytes nobody writes** (so the file holds heap or stack leftovers;
  two saves of the same world differ there): chunk 0 after the map name's
  zero; chunk 1 +0x22..+0x23 (after the RNG state) and each player name's
  bytes after its zero; from GameSettings::SaveSetup's stack buffers
  chunk 0x14 byte +0xb of every saved player record and the records past
  the setup count, and chunk 0x1a +0x22..+0x23. The tests mask exactly
  these; measured on the 3 savegames, 14 maps and 12 random worlds, the
  masked bytes of chunks 0, 1 and 0x14 differed between the two runs (0x1a
  never did). The time (chunk 0 +0x1c) is checked to lie within each call.
- Chunk 2/1 holds the whole event ring (16,000 bytes; the loader reads
  12,000). Chunk 0x19 holds 16 colours (see the table). The map name and
  the player names are copied with strcpy (no bound: a name of 256+
  characters runs into the next fields, overwritten afterwards; a map name
  of 256+ past the 0x120-byte buffer).
- Chunk 0x14 (SaveSetup): +8 the player-setup count (GameSettings +0x6c,
  the count of the vector at +0x58), then 20 records of 0x18 bytes: dword
  0, 7 bytes from the setup's getters (0x49f4d0, the index, 0x49f590,
  0x49f350, 0x49f510, 0x49f580, 0), one unwritten byte, three dwords
  (GamePlayerSetup +0xa8, +0xac, +0xb0). 0x1a (0x24 bytes): GameSettings
  +0x78 (the local player id, also logged "CFG: Saving local player id
  %i"), +0xb0..+0xbc, the logic thread's tick (CLogicThread +0x70), the
  RNG's first word. 0x1b (0x10): +0xc8, +0x74 (start goods). 0x1e (0xa):
  bytes +0xcc (random races include the Amazons), +0xcd.
- Evidence: `CLogicThread_SaveGame_AllSaveGames` (the 3 savegames, loaded
  with LoadSaveGame), `_Maps` (every 12th map, 14, loaded with LoadMap),
  `_RandomWorlds_MatchOriginal` (12 random worlds, names of every length):
  the original and ours write two files that are byte-identical chunk by
  chunk outside the masked bytes (same keys, sizes, order, valid
  checksums), same result, DebugLog calls and S3.EXE globals (CRT heap and
  time() cache left out), the world untouched; round trip: our file loaded
  with LoadSaveGame over a copy gives the world and the globals back (map
  and random worlds after one save/load, which normalizes the fields the
  loader sets). Emulator spec `CLogicThread_SaveGame` (the file members
  probed, chunk buffers recorded byte for byte, out-of-memory paths of
  chunks 0..10; the rest is COVERED_ELSEWHERE because the emulator's heap
  can't hold chunk 10's 578 KB buffer).

## Verified LoadMap callees (session 53)

Replaced and verified (in-process tests on worlds built like a new game's:
the original CGameData constructor 0x4fc990, then GenerateRandomMap; whole
world, DebugLog calls and S3.EXE globals compared; emulator specs):

- `CGameData::PrepareLoadedTerrain` 0x519540 (map/load_map.cpp): every
  water variant 0..15 becomes 0, then the water gets depth bands from the
  shore: 0 along the shore (terrain 0x30, through the marker 0xff), 1..6
  band by band, 7 further out.
- `CGameData::SpawnSettlersOnWater` 0x50dc10 (settlers/settler_spawn.cpp):
  the map's ships (settler types 0x2b/0x2c of chunk 7) on water tiles (area
  >= 0x8000) with no settler within distance 9; the candidate tile is not
  bounds-checked.
- `CGameData::PlaceTown` 0x506cf0 and `CGameData::AddPlayerTowns` 0x5074b0
  (map/player_towns.cpp): the start building is a **small tower** (type
  15), garrisoned by a Swordsman1 (job 23); start settlers and goods by
  `GameSettings::StartGoods` 0x412300 (+0x74: 1 low, 0 or 2 medium, else
  high; tables in player_towns.cpp); a computer player gets 10 more
  Swordsman1. Player +0x18 is the "town placed" flag. AddPlayerTowns uses a
  given start position (offset entries 0..9999, IsFreeSmallTowerSite) or up
  to 1,000,000 random ones (IsGoodStartPosition), and on symmetric random
  maps (world +0x2d274) places the mirror players of teams 1..3 (Player
  +0xc, FindPlayerByField0C) at (y, x), (size - y - 1, size - x - 1) and
  (size - x - 1, size - y - 1).
- The colour helpers: 0x4351e0 is `GameSettings::PlayerSetup(index)`,
  element access of the player-setup vector at GameSettings +0x58 (0xcc-byte
  records; grows an empty vector to one record, clamps a bad index; Watcom
  vector code with an SEH frame, UI/settings side, not reimplemented), and
  0x49fec0 is `GamePlayerSetup::Colour` (+0xb4), verified. LoadMap copies
  `PlayerSetup(i)->Colour()` for i = 0..19 into g_playerColourIndex
  0x7ace64 (0x4fe550..0x4fe576), right after chunk 0x40.

## Verified map loader (session 54)

`CGameData::LoadMap` (0x4fd540) is replaced (`src/s3re/game/map/load_map.cpp`,
one helper per chunk). Evidence: `CGameData_LoadMap_AllMaps` loads every
map under `Map\` (166 files of type 10; read only) with the original and
ours into two worlds built like a new game's (zeroed, original
constructor), 2..4 players taking the map slots in random orders with the
maps' start positions; variants in memory cover the rest (listed tower
garrisons, catapults and ships, pile records without a kind, missing
chunks, chunk 0x40, victory conditions, campaign missions 49..71). Whole
world, S3.EXE globals (CRT heap state left out), DebugLog calls, CRT rand
state and the CMapFile caches identical. Details confirmed:

- Order: chunk 6 ("Loading map...", "Worldsize: %u"; records y-major:
  record (y * size + x) is tile (x, y)); with chunk 6 also chunk 8's first
  pass ("Deleting Building-Bits...") and then "Setting all sectors for
  loaded Map", PrepareLoadedTerrain, BuildRegionsAndAreas. Without chunk 6
  none of these run (the map size stays). Chunk 8 again ("Loading
  buildings...", "Amount of buildings: %u", per record "Gebäude Typ %u
  x,y %u %u mapowner %u gameowner %u" with the slot and its table entry),
  then chunk 7 ("Loading settlers...", "Amount of settlers: %u"); without
  chunk 8 AddPlayerTowns runs and chunk 7 is not read. Then 9 ("Loading
  piles...", "Amount of piles: %u"), 10, 0x40, the colours, search marks
  cleared, UnblockCastleTiles, the mission fix-ups.
- Landscape tiles: +4 height, +6 terrain, +7 object and +0xe seen_object,
  +8 owner (slot 0xff: 0xff; an untaken slot (table entry 0xff): 0xff and
  search_mark +0xb = 1; else the entry's low byte), +0xa flags (| 0x40
  when owned), +0x11 resources; +0, +2, +9, +0x10, +0x12, +0x14 zeroed.
  Objects 0x6f..0x72 on terrain 0..7 become 0x0d..0x10 (seen_object keeps
  the old value).
- "Deleting Building-Bits": for each building record whose slot's table
  entry is 0xff: tile object = 0, then the layout of (race of player
  *number slot*, Player +4; type) from its first 0x80000000: footprint
  pairs lose flags 0x08 and 0x01, the pairs after 0x80000001 (skipping
  0x80000002 markers) lose 0x08.
- Buildings of taken slots: AddBuildingSite(x, y, player, type), buildup
  0, BuildingDone. Towers (15, 16, 18), record byte 6: 0 = one Swordsman1
  as the garrison; 1 = counts in nibbles: byte 7 high/low Swordsman1/2,
  byte 9 high Swordsman3 (job 23, chained into the garrison, counter
  +0x1a), byte 9 low Bowman1, byte 8 high/low Bowman2/3 (job 140,
  EnterBuildingSlot, counter +0x1c), byte 11 high/low Pikeman1/2, byte 10
  high Pikeman3 (job 141, chained, counter +0x20); byte 10 low is unused;
  other values: no garrison. The soldiers are created at the door and
  removed from their tile.
- Settlers of taken slots: type 41 CreateSevenTileUnit, 43/44
  SpawnSettlersOnWater(1, ...), **both with the map slot as the player**;
  others CreateSettler(x, y, type, player), counter +0xc = 0 (also on
  record 0 when it fails), and Player +0xcc of the settler's owner + 1 for
  classes 0 and 10..29.
- Piles: skipped on tiles with search_mark; kind != 0: CreatePile(x, y,
  goods, amount, kind); kind 0 and amount != 0: the tile's pile is
  unlinked from its area's list (area +8 when it was the head), gets the
  goods and amount, kind 0, and becomes the head of the area's list for
  its goods. No game map has such a record (78 records with amount and
  kind 0 are skipped).
- Victory conditions: 0x77 bytes to world +0x15394b1, g_victoryProtectActive
  = 1 when +0x44 is set. Chunk 0x40 sets g_worldUpdateDisabled, its
  absence clears it.
- Colours: `GameSettings::PlayerSetup(i)->Colour()` for 20 players
  (PlayerSetup stays the original).
- Missions: 49 halves every resource amount, but only for y 0..249; 50
  creates four ferries of player 0 at (60, 510), (70, 520), (80, 530),
  (90, 540) and on board 8 pioneers each in the first three (Player +0xcc
  counted) and 8 Swordsman1 in the last (created at (60, 530), job 326);
  53 sets player 0's mana +0x8c to 30, 61 to 60; 59 sets the first tree
  (object 0x44..0x51) among offset entries 0..20 around (313, 64), (351,
  113), (392, 152), (535, 277) on fire (0xf3) and mana 60; 70 makes 17
  towers at fixed positions Amazon (race 3) when the building there has
  the expected type ("xxx race changed #%u - BuildingType %u"; no building:
  "... NOT changed ...").
- In the 166 maps: 100 have only chunk 6 (AddPlayerTowns), 34 have
  buildings; building and settler slots 0..3; one landscape tile has slot
  191 (an index past the game's 20-entry table: it reads OnStart's stack
  there; the test's table is 256 entries of 0xff).

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
       GameSettings::PlayerSetup 0x4351e0 / GamePlayerSetup::Colour 0x49fec0 (player colours), UnblockCastleTiles
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
