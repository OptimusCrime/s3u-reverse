# CLogicThread::ExecuteCommands (0x535990): analysis and plan

Written 2026-11-23 (session 46, parallel worktree). How commands reach the
simulation: "Commands and lockstep" in `CLAUDE.md`; the recorder:
`src/s3re/tooling/replay/command_log.h`. Enums: `NetMessage` and `CommandId` in
`src/s3re/game/enums.h` (evidence in `re/enums.md`).

## Status (session 47): reimplemented and verified

ExecuteCommands is reimplemented as one hook (`S3RE_HOOK`, verified):

| File | Part |
|---|---|
| `execute_commands.cpp` | frame (wait/pop, dispatch, step grant, step message, the CTimeout catch), 0x01..0x05, 0x07, 0x08, 0x11, 0x14..0x19, 0x1b, 0x1c, 0xf1, 0xf5, 0xf6 |
| `execute_commands_units.cpp` | MoveUnits 0x09 / 0x0d / 0x0e / 0x0f |
| `execute_commands_trade.cpp` | SetTradeRoute 0x12, SetWorkshopProduction 0x13 |
| `execute_commands_areas.cpp` | 0x0a, 0x0b, 0x0c, 0x10 |
| `execute_commands_net.cpp` | 0x06, 0xf2, 0xf4, 0xf7..0xff, network messages 3, 4, 5, 6, 8, 0x22 |
| `execute_commands.h` | `Run`, `FrameLocals` (locals that persist across messages), callee addresses |
| `execute_commands_test.h` | the test bench (both sides run the whole function) |

**Evidence.** In-process (`--test "ExecuteCommands"`, 10 tests): per part a
`..._MatchesOriginal` test (3000..4000 cases of 1..4 messages plus a grant;
the whole original through the trampoline against ours on two world-sized
buffers; the thread, both MessageQueues and their rings sit after the world
at the same offsets; the game's own Pop/Push run; simulation callees are
recorded stubs, UI/lobby/save callees recorders of their string contents;
the queue's wait sometimes throws a real `Win32::CTimeout*` through the
game's `_CxxThrowException`) and a `..._ComparisonDetectsWrongImplementation`
test. Emulator: one spec `CLogicThread_ExecuteCommands` (logicthread; the
queues in the fake thread's area, Pop/Push run, the critical section
wrappers probed, Push's SetEvent call patched per case;
`S3RE_EC_ONLY=c9,n3,...` restricts the generated ids for work on one part).
Exempt: the CTimeout catch (0x53b823.., exception only), the dead `jmp`
0x53b81e, MoveUnits' dead clamp 0x536faa, nine refcount branches of
inlined std::string code in 0x06 / 0xf7 (dead).

**Known differences (not reproducible, documented at the code):** bytes 3
and 5..7 of the 0x21 replies are uninitialized stack in the original (ours
0; the bench masks them); the garrison sizes 0x0c uses for a building that
is not a tower or castle come from an earlier 0x0c in the same call or the
stack (ours 0, `FrameLocals`); SetSettlerGroup with a count beyond the
message reads the original's stack (ours 0); MoveUnits' ship-tile list
grows on the original's stack without limit (ours stops at 1024).

**What the commands do** (beyond the table above):
- 0x0a ChangeTransportOrder (good, op, player, area): op 1/2 transport flag
  on/off (Area +0x21a), Area +0x1f6 = the good's place in the order: op 3
  top, 4 up, 5 down, 6 to the race's last place (26 Roman, 27 Egyptian, 29
  Asian/Amazon).
- 0x0b ChangeToolSettings (kind, value, player, area): kinds 1..3 the shares
  +0x23e..+0x240 in steps of 5 (sum <= 95), 4..6 / 7..9 the percentages
  +0x242..+0x244 up / down (0..100), 10 sets +0x241.
- 0x0c ChangeGarrison (building, player, op): wanted swordsmen / bowmen /
  pikemen (Building +0x1a / +0x1c / +0x20); sizes small tower 1 (bowmen 2),
  large 3 (3), castle 6 (4); op 1 spread, 2..4 one more, 5 one of the first
  class present, 6..8 one less. A settler of another type in the garrison
  chain continues the count from settler 0's next (quirk).
- 0x10 ConvertCarriers (kind, player, x, y, count): up to 3000 offsets around
  the tile; kind 1 geologist (22), 2 thief (23), 3 pioneer (40), 4 pioneers
  back to carriers. The local player's new units become settler group 0.
- 0x12 SetTradeRoute (player, kind, building, ...): finished harbour /
  marketplace, no owner check; kind 2 trade mode (Trade +0x01), 3 clear the
  route, 4..6 route points, 7 end point (harbour: the first tile in the
  water area of its landing), 8..11 orders of trade-menu line (table
  0x659414): 100 / 0 / + / -.
- 0x13 SetWorkshopProduction (player, value, building): war machine
  workshops (owner checked) start (state = race + 0x10/0x20) or stop
  (ReleaseBuildingExtraTiles); a shipyard (no owner check) builds ship kind
  0x30 / 0x40 or stops.
- 0x09/0x0d/0x0e/0x0f MoveUnits (player, group, x, y): mode 0 move, 1
  waypoint (path_cache), 2 patrol (path_cache = own position, tile, 0xfffe,
  0xfffe), 3 attack-move (job 176, +0x3c = 0). A 0x09 with a group of only
  catapults (type 41) searches enemy towers/castles, buildings (Amazon
  "GongAttack") or catapults/ships within distance 5: mode 5 (path_cache
  0xfffd, g_raceAttackValue659db0[race]); else mode 4 (attack-move, +0x3c =
  40) unless an enemy fighter is within 6. Each unit takes the next
  MapOffset tile reachable for it (walk region; ships: sea area, ferries 9
  apart).
- 0x06 DestroyBuilding: a finished big temple sets Player +0x08 bit 0;
  CrushBuilding; a failure shows text 0x8018e to the local player.
- 0xf4 SyncCheck: seven checksums and a stamp per player in g_syncChecksums
  (0x7deb58); a mismatch logs "Desync x!", dumps (0x4ff6d0) and counts up
  g_simulationPaused, which stops the simulation.
- 0xfb is a desync check of the RNG history (renamed SyncRandomCheck).
- 0xf7..0xfa: multiplayer save (start 0x50ac40, commit 0x50af70, rollback
  0x50b3c0) with acks/failures in CLogicThread +0x22c/+0x228.
- 0xfe/0xff: an active player becomes 2 (dropped); without objectives a
  CComputerPlayer takes over (CLogicThread +0x350).
- Messages 3/5 kill a player found by its network id (Player +0x1c), 4 ends
  a film (posts 0x12 to g_app+0xc), 6 shows chat, 8 a checksum failure,
  0x22 lag warnings (words above 5000).

## Shape

One function of about 24 KB (0x535990..0x53b839; disassembly 5,900
lines, Ghidra pseudocode 3,150 lines), thiscall on the CLogicThread, no
arguments, a 0xb38-byte frame with an SEH frame (std::string temporaries).
Called by CLogicThread::Step before each tick.

```
step = this+0x3b0, budget = this+0x3b4
if step != 0:
  loop:
    timeout = 0x40d0e0(5)              // App: wait time per class (single / network, 0x7e916c)
    this->fromNet(+0x3c8)->vtable[3](timeout, 1)   // wait for a message
    pop the message (dword 0; byte 2 = size in bytes; size/4 dwords) into a stack buffer
    switch (byte 0)                    // NetMessage, jump table 0x53b850 / index 0x53b874
      1 Command: switch (byte 1)       // CommandId, jump table 0x53b898 / index 0x53b934
      2 StepGrant: budget = dword 1; leave the loop
      3 PlayerLost, 4 FilmEnd, 5 KillPlayer, 6 InGameChat, 8 ChecksumFailure, 0x22 LagReport
      other: DebugLog "Ungültige Netznachricht: Type: %u"
if step < budget (after a grant) or step == 0: step++, this+0x3b0 = step (and a flag local)
push the 0x20 step message {0x20, 0, 8, 0, step} on this+0x3c4
```

The command payload bytes are read big-endian in pairs (`b[i] * 256 +
b[i+1]`): x, y, building and settler numbers are 16-bit, players and kinds
8-bit. Many handlers end with the same UI refresh: if the command's player
is the local player (world +0x2d2d4) and the open menu (+0x2d284, +0x2d290
/ +0x2d2a0 / +0x2d298) shows what changed, `refresh_ui` (+0x2d27c) = 1; for
building commands, if the building is the selected one (global
0x7dfd6c) and menu 4 is open.

## Command ids (type-1 messages, byte 1)

Handler = address of the jump table target. "Recordings" = how often the
id occurs in `recordings/*.s3rec` (fun8 / random768 / scripted).

| id | name | handler | what it does | callees | recordings |
|----|------|---------|--------------|---------|------------|
| 0x01 | PlaceBuilding | 0x535aa4 | unless campaign mission 0x42: the player needs a finished tower/castle (0x0f/0x10/0x12) with a garrison; CheckBuildingSite(x, y, type, player, 1); if local and selection mode 1 with params: NextSearchGeneration | CheckBuildingSite, NextSearchGeneration (verified) | 94 / 18 / 14 |
| 0x02 | ScrollCamera | 0x535bea | camera_x/y += two signed 32-bit values, ClampCamera, redraw = 0 (UI state in the world) | ClampCamera (verified) | - |
| 0x03 | SetSettlerGroup | 0x535cb0 | player_lists[player][group] = up to n settler words, length (0 clears) | - | 18 / - / - |
| 0x04 | SetBuildingWorking | 0x536fbf | owner check; request_flags bit 0 = value; off: site -> ClearBuildingFlag02/04/10/08 + RecallSiteCarriers, finished -> RecallBuildingCarriers; UI refresh | **RecallSiteCarriers, RecallBuildingCarriers (new)**, ClearBuildingFlag* (verified) | 126 / - / - |
| 0x05 | SetWorkArea | 0x5370b4 | for building types 2, 0x1e, 0x0e, 0x19, 0x24, 0x18, 5, 3, 0x33 within distance 60 and the same tile area: work tile (+0x30/+0x32, or +0x0e/+0x14 of a site); clears the worker's +0x10; overlay (DrawWorkAreaOverlay) when selected | TileDistance, DrawWorkAreaOverlay (verified) | 34 / - / - |
| 0x06 | DestroyBuilding | 0x53729c | owner check; a finished big temple (0x29, 0x2b, 0x2d, 0x37) sets bit 0 of Player +0x8; CrushBuilding; if it failed for the local player: a text (CGraphicsData::VariantFrameAddress 0x8018e, sprintf, std::string, 0x60b840, ShowMessage 0x51e440); clears the selection | CrushBuilding (verified), UI text helpers (not ours) | - |
| 0x07 | ChangeGoodsPriority | 0x5375c7 | area settings1[good] +-1 (0..20) | - | - |
| 0x08 | SetGoodsSetting | 0x53767d | area settings2[good] = value; ClearAreaGoodsSettings1D2 | verified | - |
| 0x09, 0x0d..0x0f | MoveUnits (modes 0..3) | 0x536045 | the selected units of a group: attack target search (tiles with flag 0x20 / enemy military buildings, "GongAttack"), then formation targets per settler (SetSettlerTarget) around the tile; the longest handler (~0x536045..0x536f98) | TileDistance, SetSettlerTarget, NeighbourX/Y (verified) | 18 + 990 (0x0f) / 92 / 78 |
| 0x0a | ChangeTransportOrder | 0x53919d | area settings5 (+0x21a) flags and settings4 (+0x1f6) order list: up/down/top/bottom/reset by race | - | - |
| 0x0b | ChangeToolSettings | 0x5395b5 | area bytes +0x23e..+0x244: three shares (steps of 5, sum <= 95), two percentages (<= 100), +0x241 | - | 524 / 4 / 20 |
| 0x0c | ChangeGarrison | 0x539a7f | tower 0x0f / 0x10 / castle 0x12: counts its garrison by class, changes the wanted soldiers per class (+0x1a/+0x1c/+0x20) | - | 4 / - / 4 |
| 0x10 | ConvertCarriers | 0x539f30 | up to n idle carriers (jobs 1, 2, 0x37, 0x35) near a tile become type 0x16/0x17/0x28 (or back to 0): ResetSettlerToIdle, type, flags, job 0; local player: new settler group 0, UpdateGroupMenu / RemoveSettlerFromGroups | ResetSettlerToIdle, UpdateGroupMenu, RemoveSettlerFromGroups (verified) | 9 / - / - |
| 0x11 | SetAreaCondition | 0x537728 | area condition_enabled[kind] = value; ResetAreaConditionCounters | **ResetAreaConditionCounters (new)** | 76 / 16 / 8 |
| 0x12 | SetTradeRoute | 0x5377cf | harbour 0x21 / marketplace 0x22: route points 1..3 and end (Trade +0x2..+0x10; checks of water body / tile area), clear, orders per good (Trade +0x12.., table 0x659414); DrawBuildingRouteOverlay when selected | NeighbourX/Y, DrawBuildingRouteOverlay (verified) | - |
| 0x13 | SetWorkshopProduction | 0x53858a | water buildings 0x1f/0x2f/0x30/0x32 and shipyard 0x20: production state (+0x1f, by race) or stop (ReleaseBuildingExtraTiles) | ReleaseBuildingExtraTiles (verified) | - |
| 0x14 | SetShipyardDock | 0x538e45 | finished idle shipyard of the player: BlockBuildingLine, PlaceDockLine(b, x, y) | BlockBuildingLine (verified), **PlaceDockLine (new)** | - |
| 0x15 | CastSpell | 0x53888e | a priest (type 0x24) of the player with enough mana: within range (table 0x658de4): AllocTable36700Record twice ("Godly Event ... happened at") | TileDistance, AllocTable36700Record (verified) | - |
| 0x16 | UnloadShip | 0x538bd3 | ferry (type 0x2c) on water with passengers: first shore tile in 6 directions (8 steps); passengers placed there (PlaceSettlerNear) | NeighbourX/Y, PlaceSettlerNear (verified) | - |
| 0x17 | SetHarbourDock | 0x538f19 | as 0x14 for a harbour (0x21) | as 0x14 | - |
| 0x18 | PromoteSoldiers | 0x538fed | kind 1..3 (+0x80: set mana_total to the cost first): if mana_total >= cost and the level rule allows: level+1, mana_total -= cost, SetPromotionCost | **SetPromotionCost (new)** | - |
| 0x19 | LoadShip | 0x538a4f | a ship of the player with < 6 aboard: nearby water buildings (0x1f/0x2f/0x30/0x32) hand over their units (+0x36) | IsOnMap (verified) | - |
| 0x1b | HoldPosition | 0x535d63 | soldiers (class 1..9, jobs 0x37/0x35/0xb0) of a group get flag 0x20 and their position as home (+0x24..+0x2a) | - | - |
| 0x1c | SetStorehouseGoods | 0x535e8e | storehouse (type 1) of the player: +0x0e = value; ChangePileGood on its six neighbour piles | **ChangePileGood (new)** | - |
| 0xf1 | RevealMap | 0x53b34e | global 0x7dfd84 = 1 | - | - |
| 0xf2 | ClearPlayerFlag | 0x53a1af | this+0x235[player] = 0; 0x5411b0 (UI) | 0x5411b0 (not ours) | - |
| 0xf4 | SyncCheck | 0x53a1ce | per player checksum words vs the local buffer 0x7deb58; on a mismatch "Desync ..." messages (World, Economy, Trade, Pile, Settler, Buildings) and dumps (0x4ff6d0) | UI/debug only | - |
| 0xf5 | PrioritizeSite | 0x53a608 | owner check; SetBuildingFlag02/04/10/08, RecallCarriersOfOtherSites; UI refresh | **RecallCarriersOfOtherSites (new)**, SetBuildingFlag* (verified) | - |
| 0xf6 | UnprioritizeSite | 0x53a690 | owner check; ClearBuildingFlag02/04/10/08; if the player's request count (+0x24) != 0: RecallSiteCarriers | **RecallSiteCarriers (new)** | - |
| 0xf7 | SaveRequest | 0x53a735 | multiplayer save (0x50ac40), answers with 0x21 0xf9 on failure, texts | savegame code (not ours) | - |
| 0xf8 | SaveDone | 0x53a97e | counts acks; all: SaveGameCommit (0x50af70) | not ours | - |
| 0xf9 | SaveFailed | 0x53aaa1 | SaveGameRollBack (0x50b3c0), text | not ours | - |
| 0xfa | SaveAllowed | 0x53abb3 | tick limit (0x3a9 ticks after this+0x224): the local player pushes 0x21 0xf7, text | not ours | - |
| 0xfb | SyncRandomCheck (was CheckVersion) | 0x53aca5 | another player's word differs from table 0x7dfb58: text, this+0x675 = 1 (probably a version/data check) | UI text | 20 / 6 / 12 |
| 0xfc | PlayerLost | 0x53ad95 | text for another player (0x4351e0, Lobby 0x5db260), 0x540500; without objectives: CrushBuilding on the player's military buildings | CrushBuilding (verified), 0x540500 | - |
| 0xfd | PlayerWon | 0x53aef0 | 0x540500 | not ours | - |
| 0xfe | DropPlayer | 0x53aefe | an active player (Player +0 == 1) becomes 2; without objectives a CComputerPlayer (new 0x780, 0x550900) at this+0x350[player]; text; 0x5411b0 | not ours | - |
| 0xff | PlayerLeft | 0x53b145 | as 0xfe | not ours | - |

All other ids (0x1a, 0x1d..0xf0, 0xf3) go to the shared end (0x53b814)
and do nothing.

## Callees and their status

Verified before this session: CheckBuildingSite, NextSearchGeneration,
ClampCamera, ClearBuildingFlag02/04/08/10, SetBuildingFlag02/04/08/10,
CrushBuilding, BlockBuildingLine, ReleaseBuildingExtraTiles,
TileDistance, NeighbourX/Y, SetSettlerTarget, PlaceSettlerNear,
ResetSettlerToIdle, IsOnMap, AllocTable36700Record,
ClearAreaGoodsSettings1D2, DrawBuildingRouteOverlay, DrawWorkAreaOverlay,
RemoveSettlerFromGroups, UpdateGroupMenu, CGraphicsData::VariantFrameAddress, the
std::string functions, Win32::ThrowOutOfMemory, MessageQueue::Push/Pop
(identified, passthrough/none).

**Done in session 46 (this package, `src/s3re/game/logic/commands/command_helpers.cpp`,
all verified: in-process tests + emulator specs with full coverage):**
RecallSiteCarriers 0x4dee20, RecallBuildingCarriers 0x4deec0,
RecallCarriersOfOtherSites 0x4df8e0, ChangePileGood 0x4dfb40,
SetPromotionCost 0x508450, PlaceDockLine 0x4db710, OpenDockLineTile
0x4dbab0, ResetAreaConditionCounters 0x51bc20.

Not ours (UI, lobby, savegame, CRT), called through their addresses when
ExecuteCommands is reimplemented: 0x40d0e0 (wait time table), 0x411fc0
(std::string c_str), 0x4351e0 + 0x5db260 (player name from the game
setup / lobby), 0x4ff6d0 (desync dump file), 0x50ac40 / 0x50af70 /
0x50b3c0 (multiplayer save, commit, rollback), 0x51e440 (show a message),
0x53ba50 (returns the app object), 0x53ba60 (message from a wide string),
0x540500 (game over for a player), 0x5411b0, 0x550900 (CComputerPlayer
constructor), 0x60b840 (std::string helper), 0x60f480 (post a window
message), 0x61f27e (sprintf), operator new/delete, memmove.

## Order for the rest (bottom-up; done in session 47)

ExecuteCommands is hooked as one unit, so it can only be switched once all
of it is written. Proposed split of our version (one file
`src/s3re/game/logic/commands/execute_commands.cpp`, plus `execute_commands_units.cpp`
for MoveUnits):

1. The frame: wait/pop loop, NetMessage dispatch, step grant and the 0x20
   step message; the non-command messages 3, 4, 5, 6, 8, 0x22 (UI texts
   through the original helpers).
2. Simple world commands: 0x02, 0x03, 0x04, 0x05, 0x07, 0x08, 0x0a, 0x0b,
   0x0c, 0x11, 0x14, 0x17, 0x18, 0x1b, 0x1c, 0xf1, 0xf5, 0xf6.
3. Larger world commands: 0x01, 0x06 (with its text), 0x10, 0x12, 0x13,
   0x15, 0x16, 0x19.
4. MoveUnits (0x09/0x0d/0x0e/0x0f, ~0x536045..0x536f98): attack target
   search and formation placement.
5. Multiplayer / UI commands: 0xf2, 0xf4, 0xf7..0xff (texts, saves,
   computer takeover). Single player sends 0xfb (start), 0xfc/0xfd (game
   end) and possibly 0xfe/0xff; the save commands only in multiplayer.

Testing: in-process, a fake CLogicThread whose +0x3c8 queue is a real
`MessageQueue` pre-filled with messages (or a fake object with the
vtable slot 3 wait and the Pop at 0x430980 stubbed), +0x3c4 a recording
queue, a WorldPair for the world; compare world, the pushed messages and
the stubbed UI calls. Emulator: convention "logicthread" with a fake queue
object (vtable at emu.FAKE_VTABLE, slot 0xc) and the Pop/Push functions
probed or run on a preset ring buffer; one spec per command group (presets
choose the message bytes). The std::string temporaries of the text paths
(0x06, 0xf7..0xff) need the allocator-byte rule of CLAUDE.md.

Whole-game evidence will come from the recordings (ids above) and the
savegame checks (CLogicThread::Step runs ExecuteCommands every tick).
