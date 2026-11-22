# Recording a whole game

We want one long game played by a person from start to finish, recorded
so that we can replay it later (fast, unattended) against the original
game. That exercises code that short test games never reach: spells,
ships, towers being captured, shooting soldiers, the late building types.

## Before you start

Build once (from the repository root, see the README):

```
cmake --preset x86
cmake --build --preset debug
```

Close other games and make sure no other check is running
(`python tools/game_lock.py status` should say "not locked"; the script
below waits for the lock otherwise).

## Start the game

From the repository root:

```
python tools/play_game.py
```

That starts, without menus:

- a **random map of size 768** (the largest the map dialog offers),
- **you against 2 computer players**, each in a party of its own,
- **high start goods** (the free-game dialog's "goods" setting),
- your race chosen at random by the game (add `--race roman`, `egyptian`,
  `asian` or `amazon` to pick one),
- a random seed (printed, and part of the file name),
- the **original game code** (`--original`): our reimplementations are not
  used, so a bug in them can't spoil a long game,
- normal game speed.

Options: `--size 640`, `--computers 3`, `--goods medium`, `--race asian`,
`--seed 1234`, `--name mygame`. `python tools/play_game.py --help` lists
them.

There is **no difficulty setting** for computer players in The Settlers 3
(re/game_setup.md); they always play the same way. The amount of ore,
coal, stone and trees on a random map can't be chosen either: it comes
from the map generator, the size and the seed. "Goods" is the amount of
building material and goods you and the computers start with.

The same game can be started by hand:

```
build\x86\bin\Debug\s3re_launcher.exe --original --random-map 768,0 --goods high ^
    --computers 2 --seed 1234 --trace-determinism ^
    --record-commands recordings\fullgame.s3rec
```

## While playing

- Play normally, as long as you like (hours are fine). Every command is
  written to the recording file the moment it happens, so nothing is lost
  if the game crashes or the window is closed.
- **Saving** is fine: in single player a save is not a command, so it
  doesn't touch the recording, and a replay never saves (found by reading
  the code, not yet tried in a real game).
- **Don't press F12.** It is the game's fast-forward; it is not recorded,
  so a replay would go wrong from that point (the log warns if it
  happened).
- **Don't load a savegame** and don't start a second game from the menu
  in the same session: the recording belongs to the game it started with.
  If you want to play another game, quit and run the script again.

## Stopping

When the game is won or lost, or whenever you want to stop: quit through
the game's menu, or close the window. That ends the recording. There is no
special hotkey; nothing else is needed.


## What to send back

The script prints where the files went: the `recordings\` folder of the
repository.

- `fullgame-<date>-<time>-seed<N>.s3rec`: the recording (the important
  one). It is a text file and shows up in `git status` as a new file:
  that is intended, it gets committed.
- `...s3rec.tsv`: the determinism trace of your game (the reference; not
  committed, it can be regenerated).
- `...log`: s3re.log of the session (not committed).

Tell us the file name, roughly how long you played, and anything unusual
(a crash, F12 pressed by accident).

## What we do with it

```
python tools/command_log.py list NAME
python tools/check_replay.py --recording NAME --against-original --ticks N
```

(`NAME` is the file name in `recordings\`, with or without `.s3rec`.) The
recording stores the seed, the random map's parameters, the start goods,
your race and the number of computer players, so
`s3re_launcher --replay-commands FILE` starts exactly the same game and
replays your commands into it. Then the recording gets a line in
`recordings/README.md`.

## The file format

Recordings are text files (format version 4, since 2026-11-22; the exact
rules are in `src/common/recording_format.h`):

```
s3re-recording 4
# The Settlers 3 command recording (s3re). Format: src/common/recording_format.h
seed 4321
settings 1
map Map\MULTI\384-2-Lakes.map
setup flags=1 size=768 symmetry=0 field0c=0 map-seed=4321 goods=3 race=0 computers=2
# step  message bytes (hex, memory order, 4 bytes per group)
1 21fb0c00 000079b3 00000000  # cmd fb
16 210b0c00 01010100 01fbf807  # cmd 0b
```

- Line 1 is the magic and the version. `#` starts a comment, anywhere.
- `seed`: the game seed. `settings`: bit 0 = random races include the
  Amazons. `map`: the `-map` argument (`map` alone = none). `setup`: the
  game setup of a `--random-map` game (flags bit 0 = random map, map size,
  symmetry, field0c, map seed, start goods 0..3, your race 0..3 or 255 =
  random, number of computer opponents). All numbers decimal.
- Then one line per command: the step (the number of step messages before
  it, about one per tick), then the command message exactly as the game
  pushed it, as groups of 4 bytes in memory order (first byte 21 = command
  message, second byte = command id). Bytes 9..11 of 12-byte commands are
  often uninitialized memory of the game; they are kept as recorded.
  `# cmd XX` repeats the command id for reading.
- The recorder writes and flushes each line the moment the command
  happens.

Older recordings (binary versions 1 to 3, `.bin`) are still read
everywhere; `python tools/command_log.py convert OLD.bin NEW.s3rec`
converts one (`convert` of a `.s3rec` file rewrites it in normal form).
