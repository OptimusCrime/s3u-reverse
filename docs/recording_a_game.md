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
    --record-commands build\x86\bin\Debug\recordings\fullgame.bin
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

The script prints where the files went (`build\x86\bin\Debug\recordings\`):

- `fullgame-<date>-<time>-seed<N>.bin`: the recording (the important one),
- `...bin.tsv`: the determinism trace of your game (the reference),
- `...log`: s3re.log of the session.

Keep all three; they stay in the build folder (not in git). Tell us the
file name, roughly how long you played, and anything unusual (a crash,
F12 pressed by accident).

## What we do with it

```
python tools/command_log.py list build\x86\bin\Debug\recordings\NAME.bin
python tools/check_replay.py --recording build\x86\bin\Debug\recordings\NAME.bin ^
    --against-original --ticks N
```

The recording (format version 3, `src/common/recording_format.h`) stores
the seed, the random map's parameters, the start goods, your race and the
number of computer players, so `s3re_launcher --replay-commands FILE`
starts exactly the same game and replays your commands into it.
