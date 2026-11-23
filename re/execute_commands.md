# CLogicThread::ExecuteCommands (0x535990): analysis and plan

Written 2026-11-23 (session 46, parallel worktree). How commands reach the
simulation: "Commands and lockstep" in `CLAUDE.md`; the recorder:
`src/s3re/replay/command_log.h`. Enums: `NetMessage` and `CommandId` in
`src/s3re/game/enums.h` (evidence in `re/enums.md`).

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
| 0x06 | DestroyBuilding | 0x53729c | owner check; a finished big temple (0x29, 0x2b, 0x2d, 0x37) sets bit 0 of Player +0x8; CrushBuilding; if it failed for the local player: a text (GfxData::FrameOffset 0x8018e, sprintf, std::string, 0x60b840, ShowMessage 0x51e440); clears the selection | CrushBuilding (verified), UI text helpers (not ours) | - |
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
| 0xfb | CheckVersion | 0x53aca5 | another player's word differs from table 0x7dfb58: text, this+0x675 = 1 (probably a version/data check) | UI text | 20 / 6 / 12 |
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
RemoveSettlerFromGroups, UpdateGroupMenu, GfxData::FrameOffset, the
std::string functions, Win32::ThrowOutOfMemory, MessageQueue::Push/Pop
(identified, passthrough/none).

**Done in session 46 (this package, `src/s3re/game/command_helpers.cpp`,
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

## Order for the rest (bottom-up)

ExecuteCommands is hooked as one unit, so it can only be switched once all
of it is written. Proposed split of our version (one file
`src/s3re/game/execute_commands.cpp`, plus `execute_commands_units.cpp`
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
