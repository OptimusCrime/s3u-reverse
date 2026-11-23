# CGameData::UpdateEnvironment (0x4E7810) and its call tree

Package D of session 45 (2026-11-22). UpdateEnvironment is the first
world update of every logic step (CLogicThread::Step 0x53DB30). 1,345
instructions, no x87, no jump table of its own. Mapped with
`tools/asm_tools.py` (call tree from `build/s3.asm`; the static-initializer
stubs that follow some functions in the disassembly are not callees) and
Ghidra pseudocode.

## What it does, in order

1. Returns at once if `g_worldUpdateDisabled` (0x7DEB55) is set.
2. **Piles**: every 64th pile (start `g_gameTick & 0x3F`, up to the pile
   high-water mark): a building pile (+4 == 0xFE) of an area whose
   settings5 for its good is 1 and whose amount < its limit byte gets
   DispatchPileCarrier (0x5052C0, verified).
3. **Timed events**: the Effect table (world+0x36700, 0x14-byte records,
   high-water +0x67440): countdown -1; at 0 by type:
   1 SpawnSpellEffects (0x5474E0), 2 ExecuteSpell (0x5480B0, the
   GodlyEvents dispatcher) + 0x4E8BA0(8, x, y), 3 settler +0x14 &= mask,
   4 building +0x18 = (b & mask) | bits, 5 building damage (hit points
   +0x38; destroyed: NotifyLocalPlayer 9 + CrushBuilding), 6 catapult hit
   ("Katapult getroffen", the 7-tile unit 0x29: unlinks it and clears its
   seven tiles; MapEffects 0x544F80), 7 ship hit ("Schiff getroffen" /
   "Schiff sinkt": ships 0x2B/0x2C, the cargo subtracted from the owner's tallies (0x4FFB40),
   passengers freed, MapEffects 0x545070); then type = 0.
4. **Soldier strength**: per area the amounts on the pile lists of goods
   10 and 28 (+0x1E, +0x42) are summed per player; each player's
   `soldier_strength` (+0x80, read by UpdateSoldier's damage) =
   0x600 / (g_strengthDivisor + 4) + 4 * goods10 (race 1: 2 * (goods10 +
   4 * goods28)), damped above 0x100/0x140/0x180/0x1C0, halved; with
   objectives_enabled set, * 50 / 100.
5. **Reveal points**: countdowns of world+0x14D28A8 (fog_of_war.cpp).
6. **Fog of war cycle** (56 steps, counter +0x110A580) unless 0x7DFD30
   (fast-forwarding) - done: ClearSightSlice, MarkBuildingSight,
   MarkRevealPointsSight, MarkSettlerSight, UpdateFogSlice (+SeenObjectAt).
7. **Minimap**: unless 0x7DFD30, counter +0x110A584 (0..63) and
   0x4FB520(counter) (recomputes an interleaved 1/64 of the minimap,
   world+0x1223656, with the pixel function 0x4FB720).
8. **Landscape pass**: size*size/2000 tiles per step, cursor +0x2D2F4/
   +0x2D2F8 stepped by +0x2D2FC (wrapping): terrain 0x16 fields with no
   growing objects around become grass; object byte life cycles (trees
   0x44..0x53 seeding with Random, growth stages 0x55..0x6E, dead trees,
   0xB8..0xE7 stages, 0xF3..0xFD burning/spreading with Random and the
   ring table, 0x548380(0x20, x, y) for 0xF4), DecayTraffic (0x503980),
   fish (resources of kind 0, amount 1..15) moving to a random water
   neighbour (RandomBelow6, NeighbourX/Y 0x503700/0x503730), random
   removal of 0xE8..0xF2.
9. Every 220 ticks 0x4FF470; then MapEffects::ExpireSome (0x545D40) with
   g_gameTick.

## Undone callees (session start) and status

| Address | Instr. | Name (ours) | Notes | Status |
|---|---|---|---|---|
| 0x518F20 | 76 | MarkRevealPointsSight | fog | batch 1 |
| 0x519FA0 | 131 | MarkBuildingSight | fog, jump table on type | batch 1 |
| 0x51A150 | 124 | MarkSettlerSight | fog, jump table on type | batch 1 |
| 0x519010 | 175 | UpdateFogSlice | fog | batch 1 |
| 0x51D5F0 | 101 | SeenObjectAt | fog | batch 1 |
| 0x503980 | 45 | DecayTraffic | | batch 1 |
| 0x545D40 | 56 | MapEffects::ExpireSome | | batch 1 |
| 0x5440C0 | 25 | MapEffect::IsExpired | | batch 1 |
| 0x54B2B0 | 37 | PayForSpell | GodlyEvents | batch 1 |
| 0x5474E0 | 1,056 | SpawnSpellEffects | jump table on spell, make+Add | batch 1 |
| 0x4FB520 | 95 | UpdateMinimapSlice | interleaved 8x8 order | batch 3 |
| 0x4FB720 | 618 | MinimapPixel | UI globals 0x730F58/5C/60, pixel format 0x7DFBA0.. | batch 3 |
| 0x5480B0 | 262 | ExecuteSpell (dispatcher) | 18 spell handlers | batch 4 |
| 0x548340..0x54B000 | ~3,000 | spell handlers + helpers | Volker\GodlyEvents.cpp: 548340 (spells 0..3: 548380 area effect, needs 548760/548A10/548BB0/548D50), 548FB0, 549530 (reveal), 549650, 549700, 5497F0, 5498A0, 549F40, 54A370, 54A7C0, 54A540, 54A1E0 (+54AF60, 54B000), 5495D0 (EventReveal), 5492A0 (EventTreeCut), 54AA40 (EventGoodsB), 54ACD0 (EventGoodsS), 549BD0 (EventConvert), 5499B0 (EventGodlyG.., x87) | S2 (session 46): spells 10..29 and helpers 54AF60/54B000 |

UpdateEnvironment itself: batch 2 (verified, whole-game identical).

0x61FF50 (called by 0x548380) is in the CRT range.

Session 46, package S1 (spells_land.cpp): spells 0..3 SpellTransformLand
0x548340 with TransformTerrainAround 0x548380 (also UpdateEnvironment's
burning trees, kind 32), SpreadTerrainTransform 0x548760,
IsTransformableTerrain 0x548a10, NextTransformTerrain 0x548bb0,
CanTerrainsBorder 0x548d50; spell 4 SpellCreateStones 0x548fb0; spell 30
SpellTreeCurse 0x5492a0. All verified (in-process tests + emulator specs
with full coverage). 0x61ff50 is the CRT's `_chkstk` (stack probe; ours
gets the compiler's own). SpellCreateStones and SpellTreeCurse use the
CRT's rand() 0x61f802 (per-thread state, not the simulation RNG) for the
effects' start delay only.

Batch 4 (spells.cpp): spells 5 SpellRevealSpot 0x549530, 6 SpellBurnTrees
0x549650, 7 SpellAddFish 0x549700, 8 SpellHalveResources 0x5497F0, 9
SpellGrowTrees 0x5498A0, 31 SpellRevealMap 0x5495D0. The other handlers
are listed in TODO.md ("Spells left of UpdateEnvironment's call tree").

Package S2 (session 46, spell_units.cpp / spell_goods.cpp): 0x54AF60
IsSpellTarget, 0x54B000 ReleaseSpellTarget, 0x549F40 SpellMarkSoldiers
(10, 12, 16, 17, 24, 25: settler flag bits 1/2/8/4/0x80/0x10 for 375 ticks,
188 for 24, via timed event type 3), 0x54A370 SpellConvertSettlers (11),
0x54A7C0 SpellTeleportEnemies (13), 0x54A540 SpellGatherUnits (14),
0x54A1E0 SpellDisarmBowmen (15: bowmen become pioneers), 0x549BD0
SpellConvertGoods (18..20, 27), 0x5499B0 SpellGodlyGoods (21..23, 26),
0x54AA40 SpellGoodsBan (28), 0x54ACD0 SpellGoodsSummon (29). Findings:
0x5499B0 has no x87 code (its effects use the C runtime's rand(),
0x61F802, for delays and scatter, as does 0x549BD0); 0x549BD0 never
advances its loop on an area tile off the map, so it hangs when cast near
the map edge (reproduced); spells 13/28 work around a settler given as the
fourth argument (ExecuteSpell's a4), spells 14/29 move to that settler.

Spells are only cast by players and computer players; none of the
recorded games or savegames casts one (all spell call counts 0, session
45), so the spells have test evidence only.
