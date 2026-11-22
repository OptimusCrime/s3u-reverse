# Original source files

`S3.EXE` has **no debug symbols**: the PE header has no debug directory, the
symbol table is stripped, and the install has no `.pdb` or linker `.map`
file (the `.map` files in `Map/` are game maps). So there are no original
function names, line numbers or positions in source files.

It does contain **57 original source paths**, all under `E:\Siedler3\`, for
example `E:\Siedler3\Volker\LogicHandleNet.cpp`. These are leftovers of
assert-style macros (`__FILE__`); no code references them. Their *positions*
still tell us where the original source files are in the binary.

## How the code is mapped to files

The MSVC linker puts each `.obj` file's initialized data (in this build the
string literals and globals both sit in `.data`) in one block, and its code
in one block, in the same order. So:

1. The strings stored right after a path belong to that file. (Data
   *before* the path fits much worse: 33 files end up empty.)
2. Each game function that references such strings votes for that file.
   Only references to string literals count. Globals are used across files:
   a global in LogicInit.cpp's block is referenced from all over the logic.
3. The file order is the same in data and code, so each voting function is
   assigned to a file such that file indices never decrease with the code
   address and as many votes as possible agree (dynamic programming). Each
   function has one vote in total, so a function that prints 100 messages
   doesn't outvote its neighbours.

`python tools/source_files.py scan` does this and writes
`re/source_files.csv` (file, path string address, first and last assigned
function, counts). About 88% of the string references agree with the
monotone assignment. Other commands:

- `python tools/source_files.py which ADDR...`: which file an address is in
  (or which named files it lies between).
- `python tools/source_files.py strings [FILTER]`: the strings stored after
  each path, which are clues to what each file does.

Checks against functions we already know:

| Function | Address | File |
|---|---|---|
| `CConsoleThread::OnStart` | `0x40b3f0` | Dirk/ConsoleInit.cpp |
| `CNetBounceThread::Step` | `0x46f6f0` | Dirk/Net.cpp |
| `CLogicThread::ExecuteCommands` | `0x535990` | Volker/LogicHandleNet.cpp |
| `CLogicThread::Step` | `0x53db30` | Volker/LogicInit.cpp |
| `DirectX::CGameThread::Main` | `0x5b63d0` | DirectX/Gamebase.cpp |
| `Win32::CThread::Execute` | `0x60e2d0` | Win32/Classes.cpp |

### Limits

- **Only files that used the assert macro left a path.** The rest are
  unnamed and sit in the gaps between ranges. The biggest gap is
  `0x4c85a0`–`0x509980` (between DrawBuilding.cpp and GameDataSave.cpp).
  It holds most of the simulation: `UpdateEconomy`, `UpdateEnvironment`,
  `UpdateSettlers` and `CGameData::Random`.
- **A range runs from the first to the last function that voted for the
  file.** The real file can start earlier and end later, and unnamed files
  can sit inside a range. Example: `WinMain` (`0x45a120`) lands inside the
  LobbyManager.cpp range, but it is more likely in an unnamed file (a
  `main.cpp` or similar).
- **Inline functions from headers copy their strings into every `.obj`
  that uses them.** Example: `Win32\synchronize.ipp` asserts appear in
  Draw.cpp's block, and the function Ghidra starts at `0x4309c0`
  (`MessageQueue::Push`, probably eventqueue.cpp) votes for GameParams.cpp.
  Ghidra's function boundaries can also merge several functions.
- Two files get no functions: Dirk/MenuPrivTools.cpp and
  Credits/cmy_owners.h. Ranges with only one or two functions (e.g.
  LogicHandleNet.cpp, dinput.cpp) are weak evidence.

### Link order (a hint for unnamed files)

The paths appear in two groups, which are also the order of the code:

1. `Dirk\` (alphabetical, ignoring case: Console, ConsoleInit, DGlobals,
   dialog, Draw, …, Text), then `Volker\` (DrawBuilding, GameDataSave,
   LogicDraw, LogicHandleNet, LogicInit, then GodlyEvents and landscape),
   then `Rainer\` (alphabetical), then `Credits\`.
2. `DirectX\`, more `Dirk\` (lobbyclient, Lobby, graphicsdata, SoundData,
   protocol), `Win32\`. These look like separately built libraries that
   are linked after the game's own objects.

Since the first group is mostly alphabetical, an unnamed file between two
named ones probably has a name that sorts between them. For example, the
simulation code between Volker's DrawBuilding.cpp and GameDataSave.cpp
would be in files whose names sort between "Dr" and "Ga". This is a
hypothesis, not verified.

## Translations of the names

The folder names are the programmers' first names. Their full names and
jobs come from the game's own credits text in `DATA.DAT` (search it for
`PROGRAMMING`; it uses the same markup as the Credits code, e.g.
`<CENTER>`, `<DRUCKSCHRIFT>` = "block letters").

Most file names are English. The developers were German, so the German
parts, abbreviations and German debug texts are translated here.

### Folders

| Folder | Meaning |
|---|---|
| `Siedler3` | German for "Settlers 3" (*Siedler* = settler) |
| `Volker` | **Volker Wertich**, credited with Game Design and Main Program: game logic (simulation) |
| `Dirk` | **Dirk Ringe**, credited with Main Program: application shell, UI, menus, network and lobby |
| `Rainer` | **Rainer Foetzki**, credited with AI: computer players. The message `Marcus: FOE ATTACK EXCEPTION!` in a Rainer file is probably **Marcus Pukropski** (credited with Rainer under "AI and Video Player" in Quest of the Amazons, and with the Level Editor) |
| `Credits` | Credits screen with a small HTML-like text formatter; probably by **Volker Nitsch**, credited as "Credits Scroller" |
| `DirectX`, `Win32` | English: framework around DirectX and the Windows API |

### Files

| File | Meaning / contents (from its strings) |
|---|---|
| Dirk/Console.cpp | `CConsoleThread`, the application thread; command-line help texts |
| Dirk/ConsoleInit.cpp | Its initialization (version, registry settings) |
| Dirk/DGlobals.cpp | "Dirk globals": the `Dirk::Globals` namespace; data file names (`S3CD1.DAT`, …) |
| Dirk/dialog.cpp | UI dialogs and fonts |
| Dirk/Draw.cpp, DrawInitSurfaces.cpp | Drawing thread, DirectDraw surfaces |
| Dirk/eventqueue.cpp | Event/message queue (`CEventQueueOverflow`) |
| Dirk/GameParams.cpp | Game setup, map selection, player config |
| Dirk/LobbyDialogs.cpp, LobbyManager.cpp | Online lobby dialogs and manager |
| Dirk/mainwindow.cpp | Main window, mouse cursor, display changes |
| Dirk/MenuNetThread.cpp, MenuPrivTools.cpp, MenuShared.cpp | Network thread used by the menus, menu helpers ("Priv" = private), shared menu data |
| Dirk/Net.cpp, netdialogs.cpp, nethelp.cpp | In-game network thread; network/campaign/briefing dialogs; helpers including `CEventFile` (`-record`/`-play`) |
| Dirk/Pathing.cpp | Path finding ("Leftwall/Rightwall": wall-following), `CMapFile` |
| Dirk/SaveGames.cpp, Text.cpp | Save game chunks; text rendering with GDI fonts |
| Dirk/lobbyclient.cpp, Lobby.cpp, protocol.cpp | Online lobby client and its message protocol |
| Dirk/graphicsdata.cpp, SoundData.cpp | Loading the `GFX`/`SND` `.dat` files |
| Volker/DrawBuilding.cpp | Drawing buildings and settlers ("Figur" = figure) |
| Volker/GameDataSave.cpp | Saving the game state, consistency checks |
| Volker/LogicDraw.cpp | Logic side of drawing/input (its strings mention god events and network messages) |
| Volker/LogicHandleNet.cpp | Logic handling of network commands, desync checks, multiplayer saves |
| Volker/LogicInit.cpp | Logic thread initialization, map and player setup, random seed |
| Volker/GodlyEvents.cpp | Divine events (priest spells): tree curse, stealing goods, … |
| Volker/landscape.cpp | Landscape drawing (palettes) |
| Rainer/Cevaland.cpp | Unclear. Possibly "C Eva Land" = land **eva**luation (`pEvalMap` is stored just before it, and it handles house requests); a guess |
| Rainer/Comp*.cpp | "Comp" = *Computerspieler*, computer player. CompGExpand = global expansion (toward resources); CompHouseReq = house requests; CompDoBuildHouseReq = carrying out a build request; CompMapWorkerAgent = map worker agent (patrols, towers); compGlobs = computer-player globals (the AI agent classes) |
| Credits/cpp_exo_text.cpp | Text formatter for the credits (HTML entities like `&auml;`) |
| Credits/cpp_exo_text__diverses.h | *diverses* = German for "miscellaneous" |
| Credits/cmy_owners.h, cmy_scanning.cpp | "cmy" is unknown (maybe initials); scanning = text scanner |
| DirectX/Gamebase.cpp, Dxbase.cpp | `CGameApp`/`CGameThread` base classes; DirectX error names |
| DirectX/Ddraw.cpp, dinput.cpp, Dsound.cpp, Dplay.cpp, netsession.cpp | Wrappers for DirectDraw, DirectInput, DirectSound, DirectPlay; network sessions |
| Win32/Classes.cpp | `CThread`, `CApplication`, exception handling |
| Win32/Synchronize.cpp, synchronize.ipp | Events, semaphores, mutexes, timers (`.ipp` = inline implementation) |
| Win32/cdaudio.cpp, Registry.cpp, Internet.cpp | CD audio, registry, FTP via WinINet |
| Win32/Mapping.cpp | File mapping (`CFileMapping`), not game maps |
| Win32/Detach.cpp | Copying data off the CD ("detach") and the crash stack dump |

### German words in the debug texts

| German | English |
|---|---|
| Siedler | settler |
| Figur | figure (a settler on the map) |
| Art (der Figur) | kind, type (of figure) |
| Gebäude, Geb. / Haus | building / house |
| Gebäudebauphase | building construction phase |
| Abriss | demolition |
| Besitzer | owner |
| Stufe | level, stage |
| Wert / random wert | value / random value |
| Zufallswelt | random world (random map) |
| Turm / Truppe / Patrouille | tower / troop / patrol |
| Werkzeug | tool |
| Eisen, Kohle, Gold, Edelsteine, Schwefel, Steine | iron, coal, gold, gems, sulphur, stones |
| Bäume, Fische, Wasser, Weinberg, Farmland | trees, fish, water, vineyard, farmland |
| Ungültig | invalid |
| Nachricht / Netznachricht / Logiknachricht | message / network message / logic message |
| Initialisierung abgeschlossen | initialization finished |
| nicht installiert / nicht unterstützt / nicht definiert | not installed / not supported / not defined |
| gehört nicht dem Spieler | does not belong to the player |
| Spiel endet in %u min | game ends in %u min |

Selected messages:

| String | Translation |
|---|---|
| `-cdaudioXY : Spielt den Audiotrack XY ab (Achtung: kein Looping und immer 2-stellig angeben)` | plays audio track XY (note: no looping, always give 2 digits) |
| `-debug : Setzt die wichtigsten Timeouts auf INFINITE` | sets the most important timeouts to INFINITE |
| `-netmon : Schreibt ziemlich viel Netztraffic in trace.lis` | writes quite a lot of network traffic to trace.lis |
| `-demo : Überspringt die Menüs und versteckt den Mauszeiger` | skips the menus and hides the mouse pointer |
| `-detach : Kopiert alle Dateien ins Swapfile unabhängig vom Ursprungsmedium (default: nur von CD…` | copies all files to the swap file whatever the source medium (default: only from CD…) |
| `-nopatch : In der Registry eingetragene Patches werden nicht ausgeführt` | patches registered in the registry are not run |
| `Illegale Animation bei Figur, Anim Nr. %u` | illegal animation for figure, animation no. %u |
| `extrem verdächtiger todocount` | extremely suspicious todocount |
| `Siedler steht auf sector 0` | settler stands on sector 0 |
| `TROUBLE ????   Soviele sind übrig: %u` | trouble: this many are left: %u |
| `Neuer Computer %u bei step %u` | new computer player %u at step %u |
| `Erstelle Zufallswelt %i %i %i` | creating random world %i %i %i |
| `xxx loaded map: optionviewall nixda` | *nixda* = slang for "nope, nothing there" |
| `Error ! zu viele Farben` / `Division durch 0` | too many colours / division by 0 |
| `RAINER: Durcheinander bei ABRISS!` | mix-up during demolition |
| `RAINER: Hier ist noch ein Haus dieses Types!` | there is still a house of this type here |
| `RAINER: Haus das Besitzer wechseln soll gibt es nicht mehr!` | the house that should change owner no longer exists |
| `Kein ChildNote gefunden!` | no child note found |
| `GLOBAL_EXPANDkeine brauchbare Resource gefunden` | no usable resource found |
| `KEIN PASSENDES WERKZEUG VORHANDEN` | no suitable tool available |
| `ooo RAINER Baue Castle !!!` | build castle |
| `UnoccupiedTowers - schickt Truppe zum Turm` / `nächster Turm gefunden!` | sends troop to the tower / nearest tower found |
| `abgebrochen - gehört mir` | cancelled: it belongs to me |
| `Falscher EventFileMode` | wrong EventFileMode |
| `Das solltest du schon schaffen` / `was soll das?` | "you should be able to manage that" / "what's this supposed to be?" (developer jokes) |
| `mehr als Triplebuffering nicht unterstützt` | more than triple buffering not supported |
| `Wenn man Ctrl-C beim Assemblerlauf drückt, bekommt man halt ungültige DAT-Dateien` | if you press Ctrl-C during the assembler run, you get invalid DAT files, of course |
| `Ungültige Länge der Nachricht (kein Platz für Header)` | invalid message length (no room for the header) |
| `LOB: Nun folgt ein Dump der Nachricht` | a dump of the message follows |
| `file klemmt(fehlt?)` / `Formatter bufferüberlauf` / `Sring zu lang` | file stuck (missing?) / formatter buffer overflow / string too long |
| `<LINKS-MITTE>`, `<MITTE>` | credits markup: left-centre, centre |
| `Thread gestartet` / `Thread beendet` | thread started / thread ended |

The German texts are Latin-1 (Windows-1252) encoded, e.g. `0xE4` = ä,
`0xFC` = ü.
