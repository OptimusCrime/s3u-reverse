# Whole-game checks from savegames

Results of `tools/check_save.py` (see CLAUDE.md, "Starting from a
savegame"). The savegames are the user's own late games in
`Settlers3Ultimate\Save\` (single player, November 2023); they are not in
the repository. Map size 704 for slots 1 and 2.

## Determinism from a savegame (original code twice)

`python tools/check_save.py "slot N.sav" --ticks 500` (2026-11-09):

| Save | Start tick | Ticks | Result |
|---|---|---|---|
| slot 1.sav | 366,754 | 500 | identical |
| slot 2.sav | 386,061 | 500 | identical |
| slot 3.sav | 243,319 | 500 | identical |

Only after one fix to the trace: at first the "rest" hash differed between
two original runs of slot 1 from the first hashed tick on (RNG, header and
map identical). The world dumps differed in 2 to 6 bytes, all in the
alignment padding (+0x2A..+0x2B, +0x3B, +0x3E..+0x3F) of the 0x4C-byte
MapEffect records at world+0x14D2BC8. No constructor writes those bytes and
no MapEffect code reads them; they are copied from uninitialized stack
memory when a record is added (0x5445E0). Since trace v3 the rest hash
leaves them out (src/s3re/trace/determinism.cpp, kMapEffectPadding;
tools/diff_world_dumps.py knows them too).

## Original vs our build

`python tools/check_save.py "slot N.sav" --ticks 2000 --against-original`
(2026-11-09; replacements as of main f3f4a64 = batch 27, 448 installed,
verified ones only; later batches not included):

| Save | Ticks | Result | Replacements called |
|---|---|---|---|
| slot 1.sav | 366,755..368,754 | identical | 201 of 448 |
| slot 2.sav | 386,062..388,061 | identical | 180 of 448 |
| slot 3.sav | 243,320..245,319 | identical | 199 of 448 |

208 replacements were called in at least one of the three runs.

### New whole-game evidence

Replacements called in the savegame runs whose inventory notes say "Not
called in the whole-game check" (the recorded early game,
scripted-seed127.bin). Calls per run (slot 1 / slot 2 / slot 3), from
`python tools/savegame_calls.py`. To be merged into the inventory notes
(not done here, because another agent edits re/functions.csv in
parallel):

| Address | Function | Calls |
|---|---|---|
| 0x004c7b00 | Render::PackColour | 0 / 1459269 / 0 |
| 0x004fefd0 | GameWorld::AllocTable36700Record | 116 / 28 / 24 |
| 0x00505fd0 | GameWorld::PileNeighbourBuildingByteE | 1 / 0 / 0 |
| 0x00506080 | GameWorld::MakePileEmpty | 1 / 0 / 0 |
| 0x0050dff0 | GameWorld::RemoveSettlerFromLists | 13 / 0 / 0 |
| 0x00543a10 | MapEffect::Make_543a10 | 998 / 0 / 0 |
| 0x00543aa0 | MapEffect::Make_543aa0 | 499 / 0 / 0 |
| 0x00543b40 | MapEffect::Make_543b40 | 34 / 35 / 17 |
| 0x00543bc0 | MapEffect::Make_543bc0 | 162 / 139 / 67 |
| 0x00543d60 | MapEffect::Make_543d60 | 35 / 40 / 19 |
| 0x00543df0 | MapEffect::Make_543df0 | 64 / 52 / 25 |
| 0x00544210 | MapEffect::PositionAt | 600 / 0 / 35512 |
| 0x00544290 | MapEffect::SetLinks | 4929 / 480 / 259 |
| 0x005445e0 | MapEffects::Add | 1792 / 266 / 128 |
| 0x00544dc0 | MapEffects::Spawn_544dc0 | 58 / 14 / 12 |
| 0x00545120 | MapEffects::Spawn_545120 | 3 / 2 / 2 |
| 0x00572050 | AI::ClampToMap | 87 / 27 / 73 |

Suggested note text: "Whole-game check from savegames (slot 1-3.sav,
2,000 ticks each) identical to the original, N calls."

The full per-function counts are in
`build/x86/bin/Debug/save-check/slot_N-calls.tsv` after a run (not
committed).
