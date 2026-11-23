# UpdateSettlers and its callees: work map

Analysis for the batches that replace `CGameData::UpdateSettlers` (0x4E9670),
`CGameData::UpdateEconomy` (0x4E05E0, see `re/update_economy.md`) and their
callees. Made 2026-11-10 from `build/s3.asm`, the jump tables read from
`S3.EXE`, `re/functions.csv` and `tools/world_fields.py`; no game runs, no
decompiler output. Names below are proposals (ours, can be wrong), not
applied to the inventory.

Data files next to this one:

- `re/update_settlers_callees.csv`: one row per undone function in the two
  call trees (76 rows): size, status, package, undone callees, all callers,
  x87, jump tables, globals, world fields per record (lower bounds from
  world_fields.py; for the MapEffects functions ECX is the member at world
  +0x14D2BC8, so their "effects" offsets are not world fields),
  difficulty, proposed name, confidence and evidence.
- `re/update_settlers_jobs.csv`: one row per job value 0..0x194 that has a
  handler in UpdateSettlers (385 rows; 20 more jobs go straight to the loop
  end): handler address, size, jobs sharing the handler, the jobs it sets
  next, the settler types whose work chain reaches it, undone and done
  callees, DebugLog strings, number of "run again" jumps.

How the forest was computed: direct calls in the function ranges of
`re/ghidra_functions.tsv` + `re/extra_functions.tsv`, done = `verified` or
`reimplemented` or CRT/DebugLog or a done trivial forwarder (frontier.py's
rule). Calls into `StaticInit_*`/`StaticDtorReg_*` were dropped: they come
from static-initializer stubs that sit inside a Ghidra range after the real
function end (UpdateEconomy's range contains the stub `call 0x4E4A50; jmp
0x4E4A70` at 0x4E4A40, so a naive walk adds 270 bogus nodes through the
"\General"/"\Index Server"/"\Patches" initializers).

## 1. Numbers

| | instructions | undone direct callees | undone transitive |
|---|---|---|---|
| UpdateSettlers 0x4E9670 | 16,626 code (+~900 lines of jump-table data dumpbin decodes as code) | 25 | 70 |
| UpdateEconomy 0x4E05E0 | 4,797 code (to `ret` at 0x4E492B; tables and the init stub follow) | 6 | 10 |
| Both trees together | | | 76 functions, 18,628 instructions |

The direct undone callees of UpdateSettlers: 0x4D97B0, 0x4DBC50, 0x4F9A00,
0x4F9B80, 0x4FA220, 0x4FAA70, 0x4FEC10, 0x500860, 0x5046B0, 0x505820,
0x505C20, 0x50CB20, 0x50CCB0, 0x50CF50, 0x50DD20, 0x50E140, 0x50E320,
0x50E9F0, 0x510800, 0x519670, 0x519A90, 0x51B640, 0x51BA00, 0x544630,
0x5449F0. Of UpdateEconomy: 0x4DACD0, 0x4FEC10, 0x5052C0, 0x505820,
0x50E200, 0x5104C0. Shared by both roots: 0x4FEC10, 0x505820 (and below
them 0x5061B0, 0x49F510). (LOGBOOK said 27/8; the difference is the two
compiler stubs above and counting conventions.)

Big subtrees (exclusive sizes):

| subtree | functions | instructions |
|---|---|---|
| Walking 0x500860 + 0x503940 | 2 | 3,119 |
| Pathfinder 0x4A1EE0 (Dirk\Pathing.cpp, 22 functions) + 0x4FA6D0, 0x4FA880 | 24 | 3,860 |
| Soldier 0x50E9F0 + 0x545150 + 2 config getters | 4 | 2,020 |
| Building destruction (0x4D97B0, 0x4D9250, 0x4DE430 and helpers) | 13 | 2,362 |
| BuildingDone 0x4DBC50 | 1 | 1,440 |
| Settler reset 0x50CF50 + 0x5046B0 + 0x5061B0 | 3 | 1,213 |
| UpdateEconomy callees | 4 | 1,028 |
| Miner helpers 0x4F9A00, 0x4F9B80, 0x4FA220, 0x4FAA70 | 4 | 890 |
| Projectile effects 0x545B90 + 3 (x87) | 4 | 688 |
| Territory 0x519A90, 0x51B640, 0x504F70, 0x50E980 | 4 | 517 |

**One call cycle (recursion):** 0x519A90 RecalculateTerritory ->
0x4D97B0 DestroyBuildingAt -> 0x4D9250 CrushBuilding -> 0x50E3B0
ReleaseSiteWorkers -> 0x519A90. These four must be in one package.

## 2. Work packages for the parallel round

Each package owns its functions and writes only its own new source files
(names below are suggestions; follow whatever layout the struct refactor
ends with). A replacement may call a callee that another package owns
through its original address (`Original_*` or a plain function pointer to
the S3.EXE address): Detours redirects it once the owner hooks it, so no
package has to wait for another. Bottom-up order inside a package is still
preferred so that each replacement's tests run against verified callees.

### Package A: walking and pathfinding (26 functions, 6,979 instructions)

Files: `game/pathing/pathing.cpp` (Dirk\Pathing.cpp; keep it separate, it is
another programmer's source file), `game/settlers/settler_walk.cpp`.

Order:
1. Leaves: 0x503940 AddTileTraffic, 0x4FA6D0 IsWalkingJob, 0x4FA880
   IsWalkingOrIdleJob, pathing leaves 0x4A23E0, 0x4A3870, 0x4A3940,
   0x4A3A40, 0x4A3D30, 0x4A4860, 0x4A49D0, 0x4A4B00.
2. 0x4A36A0 (8 callers in the tree), 0x4A48F0.
3. 0x4A2EC0, 0x4A2F70, 0x4A33D0, 0x4A2660, 0x4A4050, 0x4A3E30.
4. 0x4A2C70, 0x4A2E20, 0x4A3170, 0x4A3AB0, 0x4A22F0.
5. 0x4A1EE0 FindPath (sets and clears the global 0x7A9D98).
6. 0x500860 WalkSettler (3,097 instructions, 3 jump tables; calls B's
   0x50CF50 and 0x545B90 through their addresses).
   Done in session 45 (settler_walk.cpp; every callee through its
   address).

Heaviest package, but self-contained (no other package calls into it).

### Package B: settler state, piles, soldiers, mines, map effects (28 functions, 6,201 instructions)

Files: `game/settlers/settler_idle.cpp` (0x50CF50), `game/economy/pile_goods_moves.cpp`
(0x5046B0, 0x5061B0, 0x505820, 0x505C20), `game/world/notifications.cpp`
(0x4FEC10), `game/settlers/soldier.cpp` (0x50E9F0), `game/settlers/miner_jobs.cpp`
(0x4F9A00, 0x4F9B80, 0x4FA220, 0x4FAA70), `game/settlers/settler_spawn.cpp`
(0x50CB20, 0x50CCB0, 0x50DD20, 0x50E140, 0x50E320), and the MapEffects
spawns in a new `game/environment/map_effect_spawns.cpp` (0x544630, 0x5449F0,
0x545150, 0x545B90, 0x5453F0, 0x545730, 0x5458D0, 0x545C80, 0x543FD0,
0x545E40) so that `map_effects.cpp` stays untouched.

Order (the first wave is what A and C call most):
1. 0x5061B0 SetPileGood, 0x5046B0 DropGoodNear, 0x4FEC10 NotifyLocalPlayer
   (0x49F510 is 4 bytes, not hookable: call it, or read the config field
   the same way), 0x50E320, 0x50CCB0, 0x50CB20, 0x50DD20, 0x50E140,
   0x4FAA70, 0x4F9A00, 0x543FD0, 0x545E40, the x87 leaves 0x5453F0,
   0x545730, 0x5458D0, 0x545150, then 0x544630, 0x5449F0.
2. 0x505820 TakeGoodFromPile, 0x545B90, 0x545C80, 0x414030 (7 bytes,
   hookable but trivial).
3. 0x50CF50 ResetSettlerToIdle (14 callers: A, C, ExecuteCommands,
   0x4DEE20, 0x4DEEC0), 0x505C20, 0x4F9B80, 0x4FA220.
4. 0x50E9F0 UpdateSoldier (calls C's 0x519A90 and 0x51B640).

### Package C: buildings, territory, terrain, UpdateEconomy callees (22 functions, 5,448 instructions)

Files: `game/buildings/building_done.cpp` (0x4DBC50), `game/buildings/building_crush.cpp`
(0x4D97B0, 0x4D9250, 0x4DE430, 0x504A00, 0x504B70, 0x504E20, 0x50E3B0,
0x51AAE0, 0x4DBB10, 0x4DBB90), `game/map/territory.cpp` (0x519A90, 0x51B640,
0x504F70, 0x50E980), `game/map/terrain_updates.cpp` (0x519670, 0x51BA00,
0x510800), `game/economy/economy.cpp` (UpdateEconomy's 0x4DACD0, 0x5052C0,
0x5104C0, 0x50E200).

Order:
1. Leaves: 0x519670 UpdateShadeAround, 0x51BA00, 0x510800, 0x5104C0,
   0x50E200, 0x51AAE0, 0x4DBC50 BuildingDone (big but has no undone
   callee).
2. 0x4DBB10, 0x50E980, 0x504F70, 0x504A00, 0x504B70, 0x504E20 (call B's
   0x50CF50/0x505820), 0x5052C0 (calls B's 0x5061B0), 0x4DACD0 (calls
   B's 0x4FEC10).
3. 0x4DBB90, 0x51B640, 0x4DE430.
4. The cycle, as one step: 0x50E3B0, 0x4D9250, 0x4D97B0, 0x519A90 (hook
   all four in the same batch; tests must cover the recursion: a
   territory change that destroys a construction site with diggers).

### Shared callees and their owners

| callee | owner | called from other packages |
|---|---|---|
| 0x50CF50 ResetSettlerToIdle | B | A (0x500860), C (0x4DBB10, 0x4DE430, 0x504A00, 0x504B70, 0x504E20, 0x504F70, 0x50E3B0, 0x50E980) |
| 0x505820 TakeGoodFromPile | B | C (0x504B70, 0x504E20), UpdateEconomy |
| 0x5046B0 DropGoodNear | B | C (0x4DE430) |
| 0x5061B0 SetPileGood | B | C (0x5052C0) |
| 0x4FEC10 NotifyLocalPlayer | B | C (0x4DACD0), UpdateEconomy, UpdateEnvironment |
| 0x545B90 SpawnProjectile | B | A (0x500860) |
| 0x519A90 RecalculateTerritory | C | B (0x50E9F0) |
| 0x51B640 ClearTileOwner | C | B (0x50E9F0) |

No function in A is called by B or C. Functions with callers outside both
trees (more whole-game evidence, more care): 0x5046B0 and 0x505C20 (AI
0x5499B0..0x54ACD0), 0x4FAA70 (0x5540F0, 0x599A30), 0x4DBC50 (map loader
0x4FD540, 0x506CF0), 0x519A90 and 0x50CB20 (0x506CF0), 0x4D9250
(UpdateEnvironment, ExecuteCommands), 0x50CF50 (ExecuteCommands),
0x4FEC10 (UpdateEnvironment), 0x50CCB0 and 0x50DD20 (map loader).

## 3. Structure of UpdateSettlers (0x4E9670)

### Main loop

```
for n = 1 .. world.settlerHwm (+0x12A5E5C), re-read every iteration:
    s = settlers[n]                     // 0x40 bytes at +0x12A5E60
    if s.owner == 0xFF: next
    if s.counter (+0xC) != 0: s.counter--, next        // wait timer
    art = s.art (+0x1); cls = SettlerClass[art] (S3.EXE 0x658F64)
    if 1 <= cls <= 9 and (s.flags +0x14 & 0x80): s.counter = 0, next   // 0x4F91FB
    if s.job (+0xE) > 0x194: next
    switch (s.job) via table 0x4F9270 (405 dwords)
    loop end 0x4F9202: if again ([esp+50h]) != 0: clear it, n--   // run n again
```

- **"Run again" mechanism.** 22 handlers jump to 0x4F9214 (`again = 0;
  n--`), so the same settler is dispatched again in the same tick after
  a job change. WalkSettler's EAX is stored in `again` (0x4EAD2F); that
  is the only handler that sets it. (Correction, session 45: the writes
  at 0x4F23E6 (job 0x9E), 0x4F5AFA (job 0x11C) and 0x4F2E00 are scratch
  writes to frame slots, not to `again`; the reimplementation's tests
  confirm it.) In C++: each job handler returns `Next` or `Again`, and
  the loop decrements n on `Again`. The `again` flag is reset before the
  repeat, so there is no endless loop unless a handler keeps asking.
- **Job 0 (0x4E971C)**: new settler: counter 0, direction RandomBelow6;
  class != 0 -> job 0x37, linked into the player's per-type list
  (player +0x20 words, +0x11943FC); class 0 (civilians) -> job 1,
  linked into the area's per-type list (area +0x12A, +0xE14712).
- **Job 1 (0x4EA855, 301 ins)**: idle civilian; DebugLog "nr. %u art %u
  owner %u groundowner %u" (settler on foreign ground) and
  ResetSettlerToIdle. **Job 0x37 (0x4E98BE, 698 ins)**: idle soldier or
  special unit: wanders (Random, CanSettlerEnterTile, SetSettlerTarget),
  UpdateSoldier.
- **Walking**: 45 jobs share handler 0x4EAD27 (`again = WalkSettler(n)`);
  jobs 0x35 and 0xB0 call WalkSettler too. The 47 jobs are exactly
  IsWalkingJob's list (0x4FA6D0): 2 3 6 A C F 11 1C 1D 2A 2B 2D 30 35 4C
  4E 68 90 93 98 9C AB B0 B2 B5 BB BC E5 E8 ED F1 F6 F9 FE 102 133 135
  140 141 147 148 149 14A 165 168 16A 16D.
- **No-op jobs** (straight to the loop end): 0x39 0x43 0x89 0x106 0x10D
  0x114 0x11B 0x154 0x162 0x172..0x17B 0x17D. Jobs 0x186..0x18F only call
  `NoOp_ret4` 0x582530.
- **Job 4 (0x4EAD38, 444 ins incl. sub-cases)**: arriving at the
  workplace. Second jump table 0x4F98C4 on the settler type (0..0x2F):
  most types do MoveSettlerToBuildingTileA + SetSettlerAnimation and start
  their work chain (table below); types 0x14 0x16 0x17 0x24 0x25 0x27..0x2C
  go to 0x4F91FB. Type 0xA (miner) uses a third table 0x4F9984 (byte index
  0x4F999C) on the building type - 7: types 7, 8, 9, 0x25, 0x1D/0x38 start
  jobs 0x52, 0x53, 0x54, 0xD6, 0xD7; all other types -> loop end.
- **Job 0x21 (0x4ED58E)**: a fourth table 0x4F99D0 on the state byte +0x17
  (incremented each visit, states 1..9), each state one animation, then
  UnblockTile and job 0x2A.
- **Job 5 (digger, 0x4EB43B, 330 ins)** can reach 0x4F922F: DebugLog
  "Digger trouble %u %u" followed by `jmp $` (an endless loop on purpose,
  an assert). Replacement: same behaviour or log + abort; not testable.

### Work chains (job 4 table)

Start job set for each settler type by job 4, with the jobs reachable by
direct job writes (approximate: chains that pass through walking jobs or
SetSettlerTarget are cut there; see `chain_started_by_settler_types` in
the jobs CSV). The instruction counts are those of the handlers.

| settler type(s) | start job | jobs of the chain (direct writes) | ins |
|---|---|---|---|
| 0x0 (carrier?) | 0x8A | 0x8A | 222 |
| 0x1 (digger) | 0x5 | 0x5 (see above; 0x50E3B0 logs "digger!") | 330 |
| 0x2 | 0xE | 0xE | 75 |
| 0x3 | 0x13 | 0x13 0x18 0x1E | 205 |
| 0x4 | 0x14 | 0x14 0x19 0x1F | 202 |
| 0x5 0x20 0x23 (class 1) | 0x10 | 0x10, via shared handler 0x4F8B7C also 0x17E..0x185 | 260 |
| 0x6 | 0x15 | 0x15 0x1A 0x31..0x34 | 156 |
| 0x7 | 0x16 | 0x16 0x1B 0x2C | 180 |
| 0x8 0x1E 0x21 (class 2) | 0x6A | 0x6A 0x8C | 50 |
| 0x9 | 0x38 / 0x42 | 0x38..0x41 / 0x42..0x4B (two products; MapEffects 0x544630 at 0x3B/0x45) | 589 |
| 0xA (miner) | 0x52..0x54, 0xD6, 0xD7 | then 0x55..0x60, 0xD8..0xE1 via 0x4F9A00/0x4F9B80/0x4FA220 | |
| 0xB | 0x6B, 0x6C, 0x128, 0x12A, 0x12C, 0x190 | 0x6B 0x74 0x7D..0x82; 0x6C 0x75 0x83..0x88; catapult production 0x128/0x129/0x137 ("Katapultproduktion owner %u"), 0x12A/0x12B/0x138, 0x12C/0x12D/0x139; gong production 0x190..0x192 ("Gongproduktion owner %u") | 2,309 |
| 0xC | 0x71 | 0x71 0x7A 0x9E..0xA2 (MapEffects Spawn_544dc0) | 313 |
| 0xD | 0x72 | 0x72 0x7B 0xA3..0xA8 (Spawn_5449f0) | 253 |
| 0xE | 0x6F | 0x6F 0x78 0xC3..0xC7 | 162 |
| 0xF 0x1F 0x22 (class 3) | 0x8B | 0x8B 0x8D | 102 |
| 0x10 | 0x70 | 0x70 0x79 0x8F 0x95 (TileSlope, RestoreGrassAt at 0x91) | 276 |
| 0x11 | 0x6D | 0x6D 0x76 0xB1 | 158 |
| 0x12 | 0x73 | 0x73 0x7C 0xBA | 144 |
| 0x13 | 0x13A | 0x13A..0x13F | 147 |
| 0x15 | 0x150 | 0x150 0x14D..0x14F | 273 |
| 0x18 | 0x11A | 0x11A 0x123 0x11C..0x11F 0x124..0x127 | 405 |
| 0x19 | 0x105 | 0x105 0x120 0x107..0x10B | 215 |
| 0x1A | 0x10C | 0x10C 0x121 0x10E..0x112 | 215 |
| 0x1B | 0x113 | 0x113 0x122 0x115..0x119 | 215 |
| 0x1C | 0x6E, 0x131 | 0x6E 0x77 0xC8..0xCD; 0x131 0x132 0xCE..0xD3 (SpawnSettlersAround at 0xD2) | 427 |
| 0x1D | 0xF3 | 0xF3 0xF4 0xF5 0xFB | 359 |
| 0x26 | 0xE2 | 0xE2 0xE3 0xE4 0xEA | 220 |
| 0x2D | 0x161 | 0x161 0x163 0x164 0x169 | 284 |
| 0x2E | 0x17C | 0x17C | 2 |
| 0x2F | 0x153 | 0x153 0x155..0x160 | 506 |

Most production chains are built from the same five handler shapes (the
original probably used a macro or copy-paste): take input
(BuildingPileHasGoods + PileIncAmountLeaving, ~47..123 ins), consume
(TakeGoodFromPile + SubtractPlayerCounter + animation, 23..35 ins), work
animation (6..9 ins), produce (PileIncAmountComing + AddGoodsToPile +
AddStatistic + AddPlayerCounter, 40..110 ins), wait (11 ins). Keep them as
separate per-job code paths in our version (the constants differ per job),
but they can share small helpers.

Jobs not reached from job 4 (the other 187 rows, 6,802 instructions):
idle/new (0, 1, 0x37), walking, soldiers and military buildings (0x8E,
0x8C, 0x10, 0x6A, 0x8B, 0x69, 0xAC..0xAF: EnterBuildingSlot,
RecalculateTerritory, UpdateSoldier, CreateSettler at 0xAE), construction
(digger 5/7/8/9: UpdateShadeAround, WearPath; builder 0x12: BuildingDone),
carriers (0x4D, 0x4F..0x51, 0x36, 0xA9, 0xAA, 0x64, 0x194: DropGoodNear,
AddGoodsToPile), ships (0x133..0x14C; 0x14C logs "Schiff lädt ab", ship
unloads), the territory-changing job 0x12E (SetOwnerOfFreeTile,
UpdateBorderFlagsAround, ClearTileOwner), the thief 0x193 ("Dieb[%u
(%u,%u)]: StealGoodFromPile fail!", "... SpyTakeGood -> no pile there?")
and the blocking/unblocking jobs 0x1E..0x2F.

### Locals shared between handlers

`esi` = world, `edi` = settler record, `ebp` = settler number, and the
stack slots [esp+48h] = type, [esp+4Ch] = class, [esp+50h] = again,
[esp+54h] = type byte, [esp+2Ch] = n. Handlers use [esp+14h..40h] as
scratch. `ebx` is reloaded from [esp+20h] at every loop start
(0x4E9696) although nothing writes [esp+20h] before the first iteration;
handlers write it as scratch. A quick check found no handler reading ebx
before writing it, but the batch must confirm that no value leaks from
one settler's handler into the next one through ebx/[esp+20h].

### Proposed C++ split (hooked as one unit)

```
// settler_update.cpp: S3RE_HOOK on 0x4E9670 only
enum class JobResult { Next, Again };
struct SettlerContext { CGameData& world; uint32_t number; Settler& settler;
                        uint8_t type; int32_t settlerClass; };
void CGameData::UpdateSettlers();          // the loop above
JobResult RunSettlerJob(SettlerContext&);  // switch over 0..0x194, same table
```

One file per family, each a set of `static JobResult Job_XXX(SettlerContext&)`
functions named after the job number until a better name is known:
`settler_jobs_idle.cpp` (0, 1, 0x37, 0x8E), `settler_jobs_work_start.cpp`
(job 4 and its type/building tables), `settler_jobs_construction.cpp`
(5, 7, 8, 9, 0x12), `settler_jobs_carrier.cpp`, `settler_jobs_soldier.cpp`
(0x10, 0x69, 0x6A, 0x8B..0x8D, 0xAC..0xB0), `settler_jobs_production.cpp`
(the type chains in the table above), `settler_jobs_miner.cpp`,
`settler_jobs_vehicles.cpp` (0x128..0x139, 0x190..0x192),
`settler_jobs_ships.cpp` (0x133..0x14F), `settler_jobs_special.cpp`
(0x12E, 0x150..0x171, 0x17C..0x194, 0x1E..0x2F). Walking stays one call
(`Again` iff WalkSettler returns non-zero).

The original cannot be called for single jobs, so the function is hooked
only when every handler exists. The family files can be written in
parallel once the dispatcher and `SettlerContext` exist. Testing: an
in-process differential test per job (a world with a few settlers in job
J, settlerHwm small, run Original_UpdateSettlers and ours, compare the
whole world) plus one emulator spec with a preset per job value for
coverage (295 handlers; the counter word must be 0 for the handler to
run).

## 4. Name proposals

All in the callee CSV (column `proposed_name`, with evidence). The ones
with clear evidence:

| address | proposal | confidence | evidence (short) |
|---|---|---|---|
| 0x4E9670 | keep `CGameData::UpdateSettlers` | high | per-settler job loop above |
| 0x500860 | `CGameData::WalkSettler` | high | handler of all 47 walking jobs; pathfinder; "suche Ziel bei %u %u" |
| 0x503940 | `CGameData::AddTileTraffic` | high | tile +0x12 += 1, WearPath at 12 |
| 0x4FA6D0 | `CGameData::IsWalkingJob` | high | its list = the walking jobs |
| 0x4FA880 | `CGameData::IsWalkingOrIdleJob` | high | same + jobs 1, 0x37 |
| 0x4DBC50 | `CGameData::BuildingDone` | high | "BuildingDone: Storage.ProductionPile ..." |
| 0x4D9250 | `CGameData::CrushBuilding` | high | "Start crushing nr. %u art %u" |
| 0x4D97B0 | `CGameData::DestroyBuildingAt` | high | "DestroyBuilding: Can't find building in range" |
| 0x505C20 | `CGameData::StealGoodFromPile` | high | its own DebugLog name |
| 0x4FEC10 | `CGameData::NotifyLocalPlayer` | high | re/world_layout.md notifications |
| 0x50CCB0 | `CGameData::EnterBuildingSlot` | high | mirror of LeaveBuildingSlot |
| 0x519670 | `CGameData::UpdateShadeAround` | high | tile +5 for 7 tiles |
| 0x4FAA70 | `CGameData::TileSlope` | high | max height difference of opposite neighbours |
| 0x5104C0 | `CGameData::RecountPlayerPopulation` | high | residences 0x1A/0x1B/0x1C give 10/30/100 |
| 0x50CF50 | `CGameData::ResetSettlerToIdle` | medium | "JobNothing/StepAside ..."; 14 callers |
| 0x50E9F0 | `CGameData::UpdateSoldier` | medium | classes 1..3 only; "Player %u wins tower from %u" |
| 0x519A90 | `CGameData::RecalculateTerritory` | medium | territory scratch, military buildings |
| 0x505820 | `CGameData::TakeGoodFromPile` | medium | pile amount -1 after each "take input" |
| 0x5061B0 | `CGameData::SetPileGood` | medium | pile +0 = good, relinks per-good list |
| 0x4DE430 | `CGameData::RemoveBuildingStorage` | medium | "%u Waren hierher unterwegs" |
| 0x50E3B0 | `CGameData::ReleaseSiteWorkers` | medium | "digger!", "builder!" |
| 0x504F70 | `CGameData::LosePileOnTile` | medium | "Pile %u Lost", "illegal pile in changing land" |
| 0x50DD20 | `CGameData::CreateSevenTileUnit` | medium | main settler + six type-0x2A parts; catapult/gong production |
| 0x4F9A00 / 0x4F9B80 / 0x4FA220 | `CGameData::MinerJobEnter/Food/Dig` | medium/low/medium | mine building types, tile resources +0x11 |
| 0x4A1EE0 | `Pathing::FindPath` | medium | entry of the Dirk\Pathing.cpp tree |
| 0x7DFDB0 (global) | `g_campaignMission` | high | CrushBuilding compares it with 70 next to "Campaign 70: ..." |

Rename `SaveGame_sub_*` rows: they are simulation code, the survey's
subsystem guess came from their position.

## 5. Risks

- **x87.** Only in four MapEffects projectile helpers of package B:
  0x545150, 0x5453F0, 0x5458D0 (fild, fmul by 0.05/0.05/0.1, `_ftol`) and
  0x545730 (fild of dx*dx+dy*dy, fsqrt, fmul 0.04, `_ftol`). UpdateSettlers
  and UpdateEconomy have none (the f-instructions dumpbin shows in them are
  jump-table data). Per the LOGBOOK proposal: `double`, same operation
  order, truncation for `_ftol`; emulator cases where the product lands
  next to an integer (e.g. distance 20 * 0.05); confirm the control word
  0x27F once at runtime.
- **Uninitialised stack.** UpdateSettlers' ebx/[esp+20h] (above).
  0x544630 fills a 0x138-byte table on the stack field by field: check
  that every entry it reads was written. The MapEffect Make calls take up
  to 13 stack arguments: compare the whole records written.
- **Globals outside the world.** `g_gameTick` 0x7DFD48 (WalkSettler,
  UpdateSoldier, CrushBuilding); 0x7DFDB0 campaign mission (CrushBuilding,
  MinerJobDig, UpdateSoldier); the config object 0x7A7AF0 (registry
  settings: +0x189 via 0x414030, +0x64 via 0x49F510): machine-dependent,
  emulator specs must preset it; 0x7A9D98 = the pathfinder's world pointer,
  set by 0x4A1EE0 and read by the whole tree (keep writing it while any
  original pathing function can run); .data constants never written by
  code: 0x730EEC = 256 (UpdateSoldier divisor), 0x730EF0 = 11 and
  0x730F00 = 10 (BuildingDone), 0x659200 (WalkSettler), 0x659664.. (hit
  points per military type), 0x658F64 (class), 0x6A8060 (animations),
  0x659DD0 (notification radii). Read them at their addresses, don't copy
  the values.
- **Graphics data in the simulation.** 0x50E320 (like SetSettlerAnimation)
  calls CGraphicsData_FrameCount 0x5FAD20 on [0x7A8010]: the frame count of an
  animation becomes the settler's wait counter. Emulator specs need the
  same preset as the SetSettlerAnimation spec.
- **UI and screen.** NotifyLocalPlayer depends on the local player
  (+0x2D2D4) and a UI setting; the work jobs call TileScreenX/Y to place
  MapEffects. These records are visual, but they live in the world and are
  hashed unless excluded (see determinism.cpp); no draw-thread calls were
  found in the trees.
- **Recursion** 0x519A90 -> 0x4D97B0 -> 0x4D9250 -> 0x50E3B0 -> 0x519A90
  (package C).
- **Endless loop** after "Digger trouble" (job 5).
- **Struct layout.** Nearly every field of the settler (+0x0..+0x3C),
  building (+0x0..+0x38), pile (14 bytes) and tile (24 bytes) records is
  used, plus area +0x8/+0xA/+0x12A lists/+0x23E..+0x240, player
  +0x0..+0xD0 counters, notifications, offset table, presence maps,
  territory scratch, recent positions. Wait for the refactor's typed
  layout; the CSV lists the fields per function.
- **Return values.** EAX is used by the callers of 0x500860 (the again
  flag), 0x50E9F0 (5 sites), 0x4DACD0 (25 sites), 0x4FAA70, 0x505C20,
  0x510800; 0x50CF50 and 0x505820 have PARTIAL sites (AL only): keep EAX
  as the original leaves it.
- **Emulator coverage.** 0x500860 (3 jump tables), 0x50E9F0 and 0x4DBC50
  are large; expect many presets. UpdateSettlers itself needs one per job.
