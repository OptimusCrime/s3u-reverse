# C runtime: what the game depends on

User decision 2026-11-26 (CLAUDE.md "Library and compiler code"): the
statically linked MSVC 6 C runtime is not rewritten wholesale. CRT
functions whose behaviour the game (or one of our replacements) depends on
are verified like game code; the rest get inventory status `platform`
("provided by the platform library": understood, never reimplemented or
verified; the standalone build uses the platform's C library instead).
`inventory.py check` allows `platform` only for category `crt`, with hook
`none` and no `verified_by`; `inventory.py stats` and `tools/progress.py`
(grey, `#7d8590`) count it apart from verified.

## The split (2026-11-27)

`python tools/crt_dependencies.py report` (rule in its docstring, call graph
from `build/s3.asm`); `apply` writes the status. Of the 506 crt rows:

- **206 candidates** (keep their status, to be verified): 115 referenced
  directly from non-crt code (game, Watcom, STL, third-party, compiler
  stubs, or functions without a row), plus the crt functions those reach
  through crt-internal calls that don't touch the OS (the internals of
  formatting, conversion, string and math code), plus named rows.
- **300 platform**: everything else: startup and exit (WinMainCRTStartup,
  `_exit`, `_amsg_exit`), heap internals (HeapAlloc/VirtualAlloc
  wrappers, `_nh_malloc`), locks (critical sections), low-level file and
  console I/O (`_read`/`_write`/`_lseek`/`_open`, `_fclose_lk`,
  `_freebuf`), locale and code pages (GetLocaleInfo, LCMapString,
  GetStringType, CompareString), environment, the SEH runtime
  (`_global_unwind2`, `_local_unwind2`, `_CallSettingFrame`) and the
  iostream internals (vtable slots of streambuf, filebuf, strstreambuf
  that game code doesn't call directly).
- Trivial functions (tools/trivial.py) stay `identified` (done by their
  bytes).

## Depends-on list, by group (why each stays a candidate)

| Group | Functions | Why |
|---|---|---|
| Float to int | `_ftol` 0x61fe98 | every float-to-int conversion in the simulation (CLAUDE.md "Floating point") |
| Allocation | operator new 0x61efb9, operator delete 0x61e4ef, `_malloc` 0x621784 | our replacements call the game's operator new/delete where the original does (CLAUDE.md workflow, step 4) |
| Memory and strings | memmove 0x61e860, `_memset`, `_memchr`, `_strlen`, `_strcmp`, `_strncmp`, `_strncpy`, `_strchr`, `_strrchr`, `_mbsnbicoll` | called from game, Watcom and STL code; results feed names, file paths, savegames |
| Formatting and conversion | `_itoa` 0x621380, the sprintf family and its internals (`_cfltcvt`, `_fassign`, `_fload_withFB`, `_frnd`, `__add_12`; still unnamed CRT_sub_ rows) | text the game writes (savegame names, statistics, UI); float formatting |
| 64-bit arithmetic | `_alldiv`, `_allmul`, `_aulldiv`, `_aullrem`, `_allshl` | compiler helpers called from game code |
| Random numbers | `rand` 0x61f802 (and its seed) | 28 callers; not the simulation RNG (CGameData::Random), but game code calls it (what for is still to check) |
| Time | `___timet_from_ft` and the time functions game code calls (GetLocalTime wrapper 0x61f6a7) | called from game code (dates, probably savegame times); not read by the simulation as far as known |
| Startup registration | atexit 0x61e812 and the AtexitReg/StaticDtorReg stubs | already verified with the compiler stubs |
| Exceptions and stack | `_CxxThrowException`, `_EH_prolog`, `exception::~exception`, `_chkstk`, `setjmp3`/`longjmp` (libpng) | called directly; their effect on control flow is part of the caller's behaviour |
| iostream entry points | the IOS_sub_ rows game code calls directly, and their crt-internal callees (31 rows) | game code writes files and logs through the old iostream library |
| Other direct calls | the remaining unnamed CRT_sub_ rows referenced from game/library code (55) | not looked at yet: kept to be safe |

**Left to do** (backlog `docs/todo/evidence.md`): name the unnamed
candidates, verify the list (some, e.g. the file-search wrappers around
FindFirstFile or the thread start around CreateThread, may turn out to be
platform after all), and verify the remaining candidates.
