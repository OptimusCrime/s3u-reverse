# Function survey (2026-11-09)

A mechanical classification of every known function in `S3.EXE` that had no
row in `re/functions.csv` yet. Nothing here is reimplemented or verified;
it says *what kind of code* each function is (compiler stub, library, which
subsystem/source file) and names it where the evidence supports a name.

- `re/function_survey.csv`: one row per surveyed function
  (`address,name,category,confidence,subsystem,source_file,evidence`).
- `re/obj_units.csv`: the object-file units found from the static
  initializer blocks (see below).
- `tools/survey_functions.py` regenerates both (`--stats` only prints counts;
  `--inventory PATH` treats another functions.csv as the existing inventory).
  `tools/survey_lib.py` extracts per-function features from `build/s3.asm`
  and `S3.EXE` (calls, imports through the IAT, string literals, vtable
  stores) and caches them in `build/survey_cache.pickle`.
- `tools/merge_survey.py` adds the survey rows to `re/functions.csv`
  (never changes existing rows; see its docstring).

## Coverage

| | rows | share of 9,836 known functions |
|---|---|---|
| inventory before | 1,685 | 17.1% |
| survey rows | 8,151 | |
| after merge | 9,836 | 100% (99.1% without the 90 `Unknown` rows) |

After a merge, `inventory.py stats` reports "Understood" (identified or
better) 4,584 / 9,836 = 46.6% (from ~2%), mostly because the 4,286 compiler
stubs become `identified`.

By category: compiler 4,312, game 3,047, crt 463, watcom 278, stl 51.
By confidence: high 4,981, medium 2,375, low 795.

| Subsystem | rows | high | medium | low |
|---|---|---|---|---|
| Compiler (static init stubs, thunks) | 4312 | 4285 | 27 | 0 |
| Lobby (online lobby, protocol) | 639 | 63 | 359 | 217 |
| UI (dialogs, controls, net dialogs) | 475 | 22 | 412 | 41 |
| AI (Rainer\\ computer players) | 450 | 2 | 291 | 157 |
| CRT (C runtime after 0x61e4ef) | 394 | 391 | 3 | 0 |
| Logic (simulation, Volker\\) | 330 | 3 | 246 | 81 |
| Net (in-game network, DirectPlay) | 247 | 22 | 174 | 51 |
| Picture (CAnyBmp/CPNGDecoder/libpng, zlib) | 194 | 0 | 183 | 11 |
| Win32 (framework: threads, sync, registry, FTP, mapping) | 156 | 39 | 92 | 25 |
| App (CConsoleThread, init, globals) | 128 | 6 | 112 | 10 |
| Menu | 105 | 1 | 82 | 22 |
| Render (DirectDraw, drawing) | 101 | 15 | 69 | 17 |
| Unknown (game code, no subsystem evidence) | 90 | 0 | 0 | 90 |
| GameSetup (GameParams.cpp) | 74 | 1 | 43 | 30 |
| iostream (old MSVCIRT, static) | 69 | 69 | 0 | 0 |
| GfxData (graphicsdata.cpp) | 68 | 5 | 53 | 10 |
| Sound | 67 | 14 | 52 | 1 |
| DirectX (CGameApp framework) | 49 | 1 | 42 | 6 |
| STL (std::string members, _Xlen/_Xran) | 46 | 8 | 37 | 1 |
| Watcom (helpers called only by WC* code) | 40 | 0 | 35 | 5 |
| Pathing | 37 | 7 | 28 | 2 |
| SaveGame | 30 | 6 | 20 | 4 |
| Credits / Window / Input / Text | 50 | 21 | 15 | 14 |

`Logic` and `AI` rows carry a detail when one CGameData region dominates the
function's field accesses, e.g. `Logic.piles`, `AI.settlers`
(`tools/world_fields.py` region table).

## Naming scheme

| Name | Meaning |
|---|---|
| `StaticInit_<global>` | dynamic initializer of a global (mostly `std::string g = "..."`, the text is in evidence) |
| `StaticDtorReg_<global>` | `atexit` registration of the global's destructor stub (the stub `mov ecx,g; jmp dtor` sits in the same Ghidra function) |
| `StaticDtor_<global>`, `StaticInitExpr_<addr>`, `StaticGuardReg_<addr>`, `AtexitReg_<addr>` | other initializer/destructor stubs |
| `VtordispThunk_`, `AdjustorThunk_`, `JmpThunk_`, `ImportThunk_<addr>` | compiler/linker thunks |
| `<Class>::<Class>` | constructor shape (vtable of Class stored after the first call); `_<addr>` suffix for more than one |
| `<Class>::ctor_or_dtor_<addr>` | vtable stored before any call: a destructor or a constructor with inlined bases |
| `<Class>::<Method>` from a name string | DirectX/Win32 wrappers pass their own `"Class::Method"` to the error handler; only used when exactly one function references the string |
| `std::string::sub_<addr>` | calls only std::string internals and handles the refcount byte |
| `<WCClass>::sub_<addr>` | called only from that Watcom container's code |
| `CRT_sub_`, `IOS_sub_`, `Zlib_sub_<addr>` | library code by address; Ghidra Function ID names are used where they exist |
| `<Subsystem>_sub_<addr>` | game code whose subsystem is known but not its purpose |

Names are unique against the inventory; collisions get `_<addr>`.

## Method (layers, first that applies wins)

1. **Compiler stubs** by exact instruction shape (`parse_stub`). The
   1,966 `push -1; push text; mov ecx,g; call 0x401a10; ret` stubs, 2,271
   `push stub; call 0x61e812; pop ecx; ret` registrations (`0x61e812` =
   `_atexit`), thunks.
2. **Library by address.** `>= 0x61e4ef`: C runtime. `0x61c4f0..0x61e4ef`:
   old iostreams (RTTI classes `ios`, `streambuf`, `filebuf`; code not
   16-byte aligned, unlike game code). Non-aligned code at
   `0x5b2fb3..0x5b50ad` inside the picture library: zlib (category `game`
   with subsystem `Picture.zlib`, since inventory.py has no third-party
   category).
3. **std::string code**: hand-checked members (`_Tidy` 0x402170 with 1,025
   callers, `assign_cstr_n` 0x4021c0, `assign_substr` 0x401d60, `_Copy`,
   `max_size`, `string_default`) and then a fixpoint: functions calling only
   string internals and testing the 0xFF refcount byte.
4. **Watcom helpers**: every caller is Watcom code.
5. **Vtable writers** (re/classes.json vtables stored by `mov [reg], VT`).
6. **Name strings** (`"Class::Method"`, see above).
7. **Position**: inside a `re/source_files.csv` range → that file; in the
   simulation gap → Logic; between two files of the same subsystem (or both
   Volker\\/Rainer\\) → that subsystem (low).
8. **Object-file units** (below) give a file when only one named range
   overlaps the unit.
9. **`__FILE__` references**: a function that pushes an
   `E:\Siedler3\...\X.cpp` path (assert macro) was compiled in X.cpp
   (MSVC6 has no cross-.cpp inlining), so source file and subsystem become
   high-confidence. 243 surveyed functions do this. (re/source_files.md says
   no code references the paths; many functions do push them.)
10. **Call graph votes** for functions without position evidence (callers
    count double; two thirds majority).
11. **World fields, imports, strings** add evidence and the subsystem detail;
    imports decide the subsystem only when nothing else does.

Confidence: high = exact stub/library shape, Ghidra FID name, own name
string, `__FILE__` path, or hand-checked; medium = inside a named file's
range / constructor shape / call-graph rule; low = gap position or call-graph
votes only.

## Notable findings

- **Over half of the "functions" are compiler stubs.** 4,312 of the 8,151
  surveyed functions are static initializer/atexit stubs and thunks.
- **Object-file boundaries.** 100 game `.obj` files include a common header
  that defines ~20 `static std::string` globals (`S3Qota1.DAT`,
  `Software\BlueByte\Siedler3\1.0`, `SingleNumber`, `Playername`, `NoFlip`,
  ... each appears exactly 100 times). MSVC6 emits a file's dynamic
  initializers at the **end** of its code, so each block of stubs ends an
  object file. `re/obj_units.csv` lists the 73 units (blocks closer than
  0x400 bytes are merged, so a few units hold 2-3 small files: those with
  82-124 stubs). Evidence that the boundaries are real: units start exactly
  at known first-of-file functions, e.g. `GameWorld::UpdatePhase2`
  (0x4e05e0), `GameWorld::UpdateEnvironment` (0x4e7810),
  `GameWorld::UpdateSettlers` (0x4e9670), `CGameData::CGameData`
  (0x4fc990), `CLogicThread::ExecuteCommands` (0x535990),
  `CLogicThread::CLogicThread` (0x53c5e0), `CComputerPlayer::CComputerPlayer`
  (0x550900), and many class constructors in Dirk\\ files. `WinMain`
  (0x45a120) sits right after a block, alone with its own block, so it has
  its own source file (as source_files.md suspected).
- **The simulation gap has about 15 unnamed source files** (units 31-42,
  45-47): one each for UpdatePhase2, UpdateEnvironment, UpdateSettlers, the
  CGameData constructor/init, map generation (0x516530..), and others.
  Unit 1 (0x401000..0x401a10) is almost only initializers (3 other functions); unit 2
  (0x401a10..0x4043d0) is the std::string instantiation.
- **No header block from 0x5a3fa0 to 0x5f6430**: the picture library,
  DirectX\\ wrappers, lobbyclient.cpp and Lobby.cpp don't include the
  header (separately built libraries, consistent with the link order in
  source_files.md). Win32\\ files after 0x5f6430 don't either.
- **The DirectX wrappers name themselves**: 49 functions named from their
  own `"CDirectDraw::DDCreateSurface"`-style strings, e.g. the whole
  `CDirectInputDevice`, `CDirectPlay::DP*`, `CDirectSound*::DS*`,
  `CSurface::DDLock/DDBlt/DDFlip` sets.
- **Error reporting hubs**: `Win32::ThrowError` (0x610960, 102 callers) and
  `Win32::ThrowWin32Error` (0x610de0, 94 callers) build
  `"<text>, file <f>, line <n>"` and throw; 0x5ffcc0 in SoundData.cpp has the
  same shape (102 callers).
- `DebugLog` (0x428720) is also the function the linker folded all empty
  functions into: a guarded local static at 0x61c4c0 registers it with
  `atexit` as an empty destructor.
- The Credits\\ "diverses.h" range is a picture library (`CAnyBmp`,
  `CPNGDecoder`, `CBmp`, `CFileSource`, libpng error strings) with zlib.

## Open questions / limits

- Source ranges are loose: DirectDraw wrappers sit inside the
  netsession.cpp range, DirectPlay wrappers inside Ddraw.cpp's and
  DirectSound wrappers inside Dplay.cpp's. Either the ranges are shifted
  there or the wrappers live in other files. Rows note "(position says X,
  the class says Y)"; source_file is left as the range says.
- `obj units` only exist for files including the header; the libraries
  after 0x5a3fa0 are one big unit and get no unit note.
- Constructor vs. destructor from vtable-store order is a heuristic.
- World-region details come from displacement matching and can pick up
  non-world objects with large offsets (rare above 0x2d000).
- Function bounds are "until the next start"; Ghidra mis-splits (e.g.
  `std::string::_Copy_tail` 0x4022f0) are named as they are.
- Possible follow-up: give each unit an `objNNN` name in
  `re/source_files.csv` and re-run the voting per unit instead of per
  monotone range.

## Merging

```
python tools/merge_survey.py            # rewrites re/functions.csv
python tools/inventory.py check
python tools/progress.py
```
Options: `--min-confidence medium`, `--skip-unknown`, `--dry-run`,
`--output PATH`. Tested on a copy: round-trip of the unchanged inventory is
byte-identical, and the merged copy passes `inventory.py check` (9,836 rows,
0 errors). Re-run `tools/survey_functions.py` first if the inventory changed
(the merge skips addresses that already have a row either way).
