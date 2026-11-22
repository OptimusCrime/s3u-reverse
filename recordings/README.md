# Command recordings

Recorded games for whole-game checks: every command of a game (player
input, computer players, sync checks) with the step it ran in. Replaying
one starts exactly the same game (seed, map, random-map setup are in the
file) and feeds it the recorded commands, so the original code and our
reimplementations can be compared tick by tick:

```
python tools/check_replay.py --recording scripted-seed127 --against-original --ticks 3000
python tools/command_log.py list scripted-seed127
```

(`--recording` takes a path or a name in this folder.) The files are text
(format version 4, `.s3rec`, described in `src/common/recording_format.h`
and `docs/recording_a_game.md`). `tools/play_game.py` writes new recordings
here; `tools/command_log.py convert IN OUT` turns an old binary recording
(`.bin`, versions 2 and 3) into this format. Determinism traces
(`NAME.s3rec.tsv`) and session logs (`NAME.log`) may sit next to a
recording locally; they are regenerable and not committed (`.gitignore`).

The whole-game evidence in `re/whole_game_calls.csv` names a recording as
source `replay:NAME` (the file name without `.s3rec`), so keep the names.

| Recording | Seed | Map / setup | Length | Commands | How it was made | Used for |
|---|---|---|---|---|---|---|
| `scripted-seed127` | 1234 | `Map\MULTI\384-2-Lakes.map` (its own player setup) | 3,000 ticks (last command at step 2,980) | 136 | 2026-11-08 (file date), recorded by `check_replay.py` with random mouse input (`--synthetic-input 127`: the name's "seed127" is the input seed, not the game seed); upgraded to version 2 on 2026-11-08, converted to version 4 on 2026-11-22 | the standard whole-game check (`replay:scripted-seed127`), 3,000 ticks against the original |
| `fun8-seed2024-input31` | 2024 | `Map\MULTI\448-8-Fun.map` (8 players: you and 7 computers) | 5,000 ticks (last command at step 4,979) | 1,913 (7 from the scripted player) | 2026-11-10, recorded by `check_replay.py` on that map with game seed 2024 and random mouse input (`--synthetic-input 31`), 5,000 ticks; converted to version 4 on 2026-11-22 | second whole-game check (`replay:fun8-seed2024-input31`): many computer players |
| `random768-seed4321-input7` | 4321 | random map 768, symmetry 0, map seed 4321, high goods, you Roman vs 2 computers (`-map Map\MULTI\384-2-Lakes.map` supplies the player slots) | 1,500 ticks (last command at step 1,486) | 136 | 2026-11-22 (session 44), `check_replay.py --random-map 768,0 --goods high --computers 2 --race roman --seed 4321 --synthetic-input 7 --ticks 1500`; converted to version 4 the same day | test of the random-map setup in recordings (`setup` line); a short random-map whole-game check |

The step of a command is the number of step messages before it, about one
per tick (a game starts at step 0, tick 0).
