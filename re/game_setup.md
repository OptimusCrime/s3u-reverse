# How a game is set up (players, computer players, goods, random maps)

Findings of session 45 (2026-11-22), from `build/s3.asm` and Ghidra
(REA). Used by `src/s3re/tooling/random_map_start.cpp` (`s3re_launcher
--random-map/--goods/--computers/--race`) and `docs/recording_a_game.md`.

## GameSettings::NewGame 0x431ab0

thiscall on the global `GameSettings` 0x7a7a18 (`game_settings_layout.h`),
13 stack arguments, `ret 0x34`. Callers: the free-game dialog's result
0x488300, the `-map` start in `Menus::CDrawThread::Update` 0x493880
(return address 0x493a56), and two lobby/network places (0x4753c0,
0x479a30).

| arg | stored at | meaning | free-game dialog (0x488300) | `-map` start |
|----:|-----------|---------|-----------------------------|--------------|
| 1 | +0x29 | single player: CLogicThread::ProcessEvents allows F12 fast-forward (+882 steps, 0x525cd2) only when 0 (0x4318d0) | `this+0x31b7 == 0` | 0 |
| 2 | +0x2a | flag (checkbox 0x11f0 or 0x1284) | +0x31c4 | 0 |
| 3 | +0x2b | flag (checkbox 0x1284) | +0x31c5 | 0 |
| 4 | (loop) | **players per party**: Watcom `WCValVector<int>` (+8 data, +0x14 count) | +0x31e8 (grid column 1) | from chunk 3 of the map file (0x4a67c0) |
| 5 | (loop) | **computer players per party** (same type) | +0x3204 (grid column 2) | the same as arg 4, minus one (you) in party 0 |
| 6 | +0x84 | the `Dirk::CMapFile` | +0x31b0 | the `-map` file |
| 7 | +0x2c | map name (std::string) | | |
| 8 | | chunk-3 setup index, -1 = none | +0x31bc or -1 | 0 |
| 9 | +0x74 | **start goods**: 0 = the map's own (chunk 1 +0x10), 1 low, 2 medium, 3 high | combo +0x400 | 0 |
| 10 | +0xc8 | read by CNetThread::OnStart, CNetSession | combo +0xad0 | 0 |
| 11 | +0xcc | random races include the Amazons | checkbox +0x1318 | DGlobals+0x158 |
| 12 | +0xcd | "NGO" (goods objective) enabled -> world +0x1195dcc | checkbox +0x13ac | 0 |
| 13 | +0xd0 | NGO time-out in minutes (DebugLog "*** NGO TimeOut: %u min") | +0x31e4 | 60 |

NewGame also stores `rand()` as the game seed (+0x88) and a GUID (+0xb0).

**Players.** For each party p and each of its `players[p]` players NewGame
fills one record (`GamePlayerSetup::Set` 0x49f120): type 0 (human) for the
first `players[p] - computers[p]` players of the party, type 2 (computer)
for the rest (unless chunk 3 of the map says otherwise). Race, start
position and two more values come from chunk 2 of the map file (0x2d
bytes per start position: +8 race, +0xc, +0x10); a map without chunk 2
(every random map) gives race 0xff and start -1. CLogicThread::OnStart
(0x53c9e0) then copies each record into the world's player table (stride
0x148 at +0x11943dc: type 1 human / 2 computer / 0 none), draws race 0xff
with `CGameData::Random` (`% 3`, or `& 3` if Amazons are included), and
creates a `CComputerPlayer` (0x550900, 0x780 bytes) for every computer
player.

**Start goods** (+0x74) are read by the town placement 0x506cf0 ("placing
town: %u at: %u,%u") through the getter 0x412300: 1 = low, 3 = high,
everything else (0, 2) = medium.

**Computer-player difficulty: there is none.** The player records have no
such field, `CComputerPlayer`'s constructor takes (world, queue, player,
first) only, and NewGame has no argument for it. Settlers 3 computer
players have one strength.

**Teams.** Players of one party are allies. The free-game dialog's grid
gives each party a player count and a computer count.

## Random maps (map dialog 0x481e90)

The dialog builds a memory-only `Dirk::CMapFile` with chunk 5 (size,
second combo `field0c`, symmetry, seed) and chunk 1 (kind 2, start
positions min(((size - 32)^2 >> 14) + 1, 20), +0x10 = 0, so the start
goods are the dialog's goods setting). `CLogicThread::OnStart` passes
seed, size and symmetry of chunk 5 to `CGameData::GenerateRandomMap`
0x5166d0. **`field0c` is not read by the generator or the simulation**,
only by GameSettings 0x436f80 for `CNetSession::vf03` and the game-info
screen (0x48e180). So the landscape's own resources (ore, stone, trees)
can't be set: they come from the generator, size and seed.

## The -map start and our --random-map

The `-map` start reads chunk 3 (setup 0) of the given map file into the
two party vectors, then calls NewGame. `random_map_start.cpp` replaces the
map file by a random one (as the dialog builds it), and optionally the
start goods (arg 9), the party vectors (`--computers N`: N+1 parties of
one player each; you in party 0) and, through a pass-through on
`GamePlayerSetup::Set` during that call, the human player's race
(`--race`). The `-map` file (default `Map\MULTI\384-2-Lakes.map`) is only
used for what the menu thread reads besides (chunk 0x40).

## Other single-player keys that matter for recordings

- **F12** (virtual key 0x7b, ProcessEvents jump table 0x534cbc case 0x2d):
  `g_stepCountdown` 0x7dfd2c += 882 when GameSettings+0x29 is 0: the
  game's fast-forward. It is a UI event, not a command, so a command
  recording doesn't contain it; it also makes the simulation skip work.
  The recorder logs a warning when it happens.
- **Saving** in single player happens in `CLogicThread::ProcessEvents`
  (0x50ac40, 8 call sites), not as a command: a replay never saves. It
  sets only UI fields (+0x2d298 message, +0x2d27c refresh), which the
  determinism trace skips. Multiplayer saves are commands ("Starting
  multiplayersave", ExecuteCommands).
