# Logic thread (CLogicThread, Volker\LogicInit.cpp, LogicDraw.cpp, GameDataSave.cpp): findings of the naming pass

Findings of the naming pass of session 50 (2026-11-24) over
0x509980..0x54cbd0. Read from the code, not yet confirmed by
reimplemented functions. Function names are in `re/functions.csv` (notes
"Named (session 50, ...)").


74 `classified` game/watcom rows in this range. 61 renames are proposed in `logic.tsv` (48 high, 13 medium).
13 rows were left alone because they already have a sensible name: CLogicThread::SaveGameToSlot 0x50ac40,
CGameData::SpawnSettlersOnWater 0x50dc10, CGameData::PrepareLoadedTerrain 0x519540,
CLogicThread::PrepareDrawData 0x51f110, CLogicThread::ProcessEvents 0x524fa0, the CLogicThread
ctor/sdd/dtor 0x53c5e0/0x53c7a0/0x53c7d0, OnStart 0x53c9e0, OnExit 0x53dab0, the WCValSortedVector
<CGamePlayerStatus> dtor/sdd 0x541690/0x5416f0, and CLandscapeSurface::CLandscapeSurface 0x54bd30.
The names checked out against the code, so they can move to `identified`.

Texts were read from `GFX/Siedler3_0N.f8007e01f.dat` (text table at header +0x34: dword magic, dword size,
word count, word languages, then offsets `[lang*count + index]`; language 0 German, 1 English).

## Lifecycle of the logic thread

**Before it runs**, the app (0x405630, Console.cpp) calls `CLogicThread::Setup` 0x53c970. It wires up the
thread: the net queues (+0x3c4/+0x3c8), the draw-data hand-over (shared slot pointer +0x3cc, draw event +0x3d4,
draw-request event +0x3d8), the lobby objects (+0x66c lobby, +0x670 lobby client), +0x3c0, +0x3a8 and a byte
at +0x3b8.

**OnStart** (0x53c9e0) runs these steps in order:
1. Builds `CGameData` and loads the game from a savegame (LoadSaveGame), from a random-map chunk
   ("Erstelle Zufallswelt", GenerateRandomMap) or from a map (LoadMap + InitAfterMapLoad).
   InitAfterMapLoad calls InitTileShadesAndMapEdge 0x519e80 (shading, water border, "sectors done") and
   RebuildFogOfWar 0x518e70.
2. Sets up the players ("LOGIC: Reading Player", lobby ids "LOGIC: Slot %i LobbyId %i Name %s") and creates
   CComputerPlayer objects.
3. Handles the NGO objectives ("*** NGO enabled ***").
4. Starts the 64 ms timer.
5. Shows the start messages (`wstring_from_wcs` + `ShowMessage`).
6. Calls `StartMusic(local race, or -1)`.
7. With g_worldUpdateDisabled it reveals the map: the minimap slices and UpdateAllBorderFlags.
8. Calls `SendGameStatusToLobby(1)`.
9. Logs "CLogicThread: Initialisierung abgeschlossen".

**Step** (0x53db30) runs on every step:
- `ProcessEvents` (UI events from the draw thread: keys, clicks, chat input, menus, save/load slots).
- `UpdateLobby` 0x541170 (lobby client poll, `PollLobbyStatus`, `PollLobbyChat`).
- `ExecuteCommands` and the 0x20/0x21 messages.
- Game-end timeout messages ("Spiel endet in %u min").

On a tick it runs these, with `PrepareDrawData` before each phase while g_stepCountdown == 0:
1. UpdateEnvironment
2. UpdateEconomy
3. UpdateSettlers
4. CComputerPlayer::Step for the 20 slots
5. `UpdateTutorial` 0x53fbb0, only when g_campaignMission == 99
6. CheckGameEnd
7. `AgeMessages` 0x51e680
8. A final `PrepareDrawData`

**OnExit** (0x53dab0) does the following:
- Clears the waitable list +0x3e0.
- Calls `StopMusic`.
- Stops the timer +0x3bc.
- Calls `Win32::CSemaphore::Release(1)` on the global semaphore 0x7a800c, probably to tell the main thread the
  logic thread has finished.
- Runs the base OnExit.
- Deletes the draw-data buffer +0x3d0, through 0x405a50, which is probably `DrawData::~DrawData`.
- Deletes the 20 computer players.

`FinishGame` 0x53e720 (the original name, from its DebugLog) computes the final scores and ranking in the
0x44-byte player table at 0x7acfa8 (`StaticInit_7acfa8` = 0x546950). It then posts 0x801 to the app.
ProcessEvents calls it from a button in menu 0xb.

## Draw-data producer (logic side of the double buffer)

- `PrepareDrawData` 0x51f110 (ret 4, bool force) does the following:
  1. Without `force`, it works only when `TakeDrawRequest` 0x51f0b0 finds the draw thread's request event
     (+0x3d8) signalled. That function tests the event and resets it with `Win32::CEvent::Reset` 0x523110.
  2. On first use it allocates the 0xc7720-byte buffer (`DrawData::DrawData` 0x523480) at this+0x3d0.
  3. It fills the buffer from the world:
     - tiles and objects
     - the 20 message lines, copied with `MessageLine::Assign` 0x523330
     - up to 100 map-effect draw records from `MapEffects::CollectVisible` 0x544390
     - +0x5c = ticks since the last fill
  4. It increments world+0x2d301.
  5. `PublishDrawData` 0x51f0e0 swaps the buffer into the shared slot (*(this+0x3cc)) with InterlockedExchange
     and sets the draw event +0x3d4 through 0x42cf80 = `Win32::CEvent::Set`.
- On the draw side, `CDrawThread::FetchDrawData` 0x42c780 waits on its event and exchanges its own buffer
  with the same slot.

## Message lines and chat (CLogicThread fields)

- The message ring has 20 lines of 0x1c bytes at this+0x414. Each line holds:
  - a wstring
  - +0x10 lifetime: 800 ticks, 1,000,000 for kinds >= 250 (tutorial/system), 0 for kinds < 20
  - +0x14 kind
  - +0x18 dirty
- The ring's count is at +0x644 and its current index at +0x648. Functions: `ShowMessage` 0x51e440,
  `ClearMessages` 0x51e650, `AgeMessages` 0x51e680.
- Chat input: the wstring at +0x3f8, the active flag +0x408, the prompt length +0x40c, dirty +0x410.
  Functions: `ChatInputKey` 0x51e160, `ChatInputChar` 0x51e2e0. The prompt is built with
  `std::wstring::concat_wcs` 0x534e50.
- MSVC6 wstring helpers found here: `wstring_from_wcs` 0x53ba60, `wstring_copy` 0x534d30,
  `assign_copy` 0x523a40, `wstring_substr` 0x541410, `concat_wcs` 0x534e50, `concat_wcs_left` 0x5418a0.
  0x61fe1b (CRT) is wcslen.

## Tutorial (new finding)

- world+0x2d308 is the **tutorial step** and world+0x2d304 is the **tutorial wait countdown**. Both are
  saved in savegame chunk 1 at +0x1854/+0x1858.
- The step table is at 0x72faf0: 16-byte entries {step, next step, text kind 253/254, text id 0x70090..}.
- The texts are the "boot camp" lessons, e.g. 0x70090 "Greetings chosen! Welcome to the Settlers III boot
  camp ...".
- `UpdateTutorial` 0x53fbb0 checks the current step's goal.
- `AdvanceTutorialStep` 0x53f950 moves on, restarts the wait (`StartTutorialWait` 0x515af0 = 100000) and
  shows the text (`ShowTutorialText` 0x540400, which wraps lines at 75 chars).
- A click in ProcessEvents (0x532fd5) sets the countdown to 1, so `TutorialWaitDone` 0x515b00 ends the wait.
- g_campaignMission 99 = the tutorial.

Suggested field names:
- world `+0x2d304 tutorial_wait`, `+0x2d308 tutorial_step` (re/world_layout.md now says "UI?/guess").

## Lobby / ladder (online play)

- CLogicThread `+0x235[20]` (structs.md: "per player, cleared by 0xf2/0xfe/0xff") is **"player still connected
  to the lobby"**. `CheckLobbyConnections` 0x5411b0 shows 0x90147 "All players lost the connection to the
  lobby! Game not valid for ladder." when none is left.
- `PollLobbyStatus` 0x540980 pushes the 0xf2 command itself for the local player on status 6 ("Lobby
  connection lost!"). So command 0xf2 ClearPlayerFlag could be renamed **LobbyConnectionLost**
  (re/enums.md, re/execute_commands.md).
- GameSetup +0x2a (0x435aa0) = ladder ("Liga") game.
- CLogicThread +0x674 = game status already sent; +0x675 = desync detected (already known).

## Music

- CLogicThread fields:
  - +0x654 stopped
  - +0x658 track index
  - +0x65c fixed playlist (-1 = cycle)
  - +0x660 current playlist (race 0..3)
- g_config+0x5e = music enabled (0x412810 is that getter); 0x412820 is another config bool (random music?).
- 0x7a7c18 = installed CD version (3 mission CD "AMZ", 4 Gold CD1, 5 Gold CD2, else original).
- The per-version track tables are at 0x730ac0/0x730af0/0x730b30/0x730b60, with the playlist lengths at
  0x730b90/0x730ba0/0x730bb0/0x730bc0.

## Map effects and draw records

- MapEffect fields +0x18/+0x1c/+0x20/+0x24 are **start tile x/y and end tile x/y** (`MapEffect::TileAt`
  0x5441c0). In layout.h they are still unk18..unk24.
- +0x2c is the graphic id (-1 = none). +0x06 is the animation period, +0x30/+0x32 are the first frame and the
  frame count (frame = unk30 + ((tick - start) % unk06) * unk32 / unk06). +0x38/+0x39/+0x34 are copied into
  the draw record; +0x39 non-zero overrides the fog brightness.
- **`LogicRecord24` (0x5437b0, verified) is the map-effect draw record** in DrawData+0xc690c (100 of them),
  filled by `MapEffect::MakeDrawRecord` 0x544100 / `LogicRecord24_args` 0x5437e0. Suggested rename for the
  verified row and its struct: `EffectDrawRecord` (not done here because the row is verified).
- `MapEffects::MapEffects` 0x5442c0 is the table constructor, called from CGameData::CGameData.

## Corrections of existing names (rows outside the list or verified; not in the TSV)

- 0x42cf80 `Render_sub_42cf80` = **Win32::CEvent::Set** (SetEvent on +8, Win32 error throw).
- 0x42c780 `UI_sub_42c780` in functions.csv is CDrawThread::FetchDrawData. The rename from the naming pass
  is apparently not in this checkout yet.
- 0x405a50 `App_sub_405a50` is probably **DrawData::~DrawData** (OnExit calls it on +0x3d0 before
  operator delete).
- 0x4532f0 `WCSortedVectorBase_LobbyClient_CGroup::WCSortedVectorBase_LobbyClient_CGroup` is probably
  misnamed: UpdateLobby calls it on the lobby client object this+0x670 with argument 1 every step (a poll or
  update method, not a constructor).
- 0x44e980 `LobbyClient::CChatMessage::CChatMessage_44e980` is probably
  `WCValOrderedVector<CChatMessage>::resize`, called as resize(1) from the operator[] at 0x5415b0.
- 0x454170 `WCBareVectorBase_LobbyClient_CChatMessage::WCBareVectorBase_LobbyClient_CChatMessage` returns
  the lobby client's pending chat vector into a local (PollLobbyChat), so it is a lobby-client getter rather
  than a constructor (medium).
- 0x5c3740 returns the landscape blit function given to `CLandscapeSurface::SetBlitFunction`. InitSurfaces
  raises the thread priority around it, so it probably benchmarks the blit routines.
- 0x523540 is a plain `std::string` default constructor (no allocator argument), shared by lobby code
  (CLobbyManager ctor, 0x497040, CGraphicsData::Open).
- The names `CLogicThread::ProcessEvents` / `PrepareDrawData` are fine.
- execute_commands.md's "0x53ba60 (message from a wide string)" is just the wstring-from-wcs constructor.
