# Win32 framework (Win32\*.cpp): findings of the naming pass

Findings of the naming pass of session 50 (2026-11-24) over
Synchronize.cpp and Classes.cpp (part 1) and cdaudio.cpp, Registry.cpp,
Internet.cpp, Mapping.cpp, Detach.cpp (part 2). Read from the code, not
yet confirmed by reimplemented functions. Function names are in
`re/functions.csv` (notes "Named (session 50, ...)").

## Win32 framework 0x60c060..0x615740: naming notes

Sources: Win32\Synchronize.cpp + synchronize.ipp (0x60c060..0x60dcd0),
Win32\Classes.cpp (0x60df50..~0x615510), then cdaudio.cpp (0x615520 and
later, although source_files.py still puts 0x615520 in Classes.cpp).
Evidence came from build/s3.asm (with the import names resolved from the
PE import table), REA/Ghidra decompilation and RTTI vtables (re/classes.json).

### Class structure

```
Win32::CObject (vtable 0x65d67c; slot 0 deleting dtor)
  Win32::CWin32Handle (0x65d654): +4 HANDLE, +8 owned; slot 1 Close
  Win32::CWaitable (0x65d3c8, abstract) -> Win32::CWin32Waitable (0x65d660)
      +0 vtable, +4 CWin32Handle subobject (handle at +8), +0x10 lpBag
      (CWin32WaitableBag<CWin32Waitable>*, 0x1c bytes, created lazily with
      just this object in it, by slots 1/2)
      slots: 0 Wait(timeout, throwOnTimeout)          0x60c060
             1 WaitForDispatchingMessages(queue, timeout, wakeMask) 0x60c2c0
             2 MsgWaitFor(timeout, wakeMask) -> bool message arrived 0x60c3e0
    Win32::CEvent / CSemaphore / CMutex: Create = CreateEventA/
      CreateSemaphoreA/CreateMutexA. CEvent::Set is 0x42cf80 (SetEvent,
      today "Win32::CEvent::Set", outside this range).
    Win32::CTimer (0x65d6e4): +0x14 timer resolution ms (timeBeginPeriod
      when no waitable timers), +0x18 timeSetEvent id, +0x1c fallback
      CEvent* (signalled by the TimeProc 0x60d640), +0x20..+0x2c
      CreateWaitableTimerA / OpenWaitableTimerA / SetWaitableTimer /
      CancelWaitableTimer from GetProcAddress("Kernel32").
      Create 0x60d040, Start 0x60d0a0, StartPeriodic 0x60d3a0, Stop 0x60d3e0.
    Win32::CThread (0x65d710): +0x14 thread id, +0x18 state (1 after
      construction), +0x1c/+0x20 constructor args, +0x24 parent CThread*
      (the Start argument).
      slots: 0-2 as CWin32Waitable, 3 IsMessagedThread (0 / 1 in
             CMessagedThread), 4 OnStart (base 0x610370: installs the SE
             translator), 5 Main (base returns 0), 6 OnExit (base: empty,
             folded with DebugLog), 7 Execute 0x60e2d0, 8
             HandleUnhandledException(kind, exception) 0x60e5e0.
             DirectX::CGameThread adds 9 Step, 10 OnEvent.
      Start(parent) 0x60e060 = _beginthreadex(stack 0x1000, ThreadProc
      0x60ec20 -> DebugLog, slot 7 Execute, DebugLog), CREATE_SUSPENDED,
      then Set handle + ResumeThread. SetPriority 0x60e9d0. operator==
      0x60ec00 compares thread ids.
      Win32::CMessagedThread (virtual base CThread, plus CMessageQueue
        member/base at +0x34 in its own layout; +4 "started" CEvent*):
        ctor 0x60f290, dtor 0x60f340, deleting dtor 0x610210,
        Start(parent) 0x60f390 (starts, then waits up to GetTimeout(1) =
        30 s, INFINITE with -debug, for OnStart to signal that the thread's
        message queue exists), OnStart 0x60f430, Main 0x60f470 (=
        CMessageQueue::Run), PostThreadMessage 0x60f480, IsMessagedThread
        0x466200 (returns 1; outside range, rename suggested).
      Win32::CApplication (0x65d78c; virtual base CThread at +0x10; +8 =
        HINSTANCE, used by CWindow::GetWindowClass/CreateEx; g_app 0x7e9168
        is the singleton, "Only one instance of CApplication allowed").
        slot 0 "Start" asserts "Applications are created by running them.
        This one is running. So what?"; ctor 0x60ec50 also wraps
        GetCurrentThread()/GetCurrentThreadId() as its own thread;
        HandleUnhandledException 0x60f010 (slot 8) terminates the process;
        DetachFromMedium 0x60edb0.
        Win32::CWindowedApp (0x65d830; CApplication + CMessagedThread,
          CMessageQueue at +0x4c): slot 0 Start (same assert), slot 1
          OnException(kind, exc) 0x60f8b0 (rethrows); OnStart 0x60f880;
          OnThreadMessage 0x60f940 (msg 0x600 -> OnException);
          ctor 0x60f6b0, dtor 0x60f7c0, deleting dtor 0x6102b0.
          -> DirectX::CApplication -> CGameApp -> CGameAppDraw -> CConsoleThread
  Win32::CMessageQueue (0x65d6f4): +4 HWND filter for GetMessage (0 = all;
      the existing "owner" field comment in framework_layout.h is probably
      wrong: it is the hwnd argument of GetMessageA).
      slots: 0 deleting dtor (0x5b9e20, "DirectX_sub_5b9e20"), 1 OnIdle
      (base asserts "A nearly pure virtual function"), 2
      OnThreadMessage(msg, wParam, lParam) (0x41abd0, today
      "Win32::CMessageQueue::vf02").
      ProcessMessage(int* quitCode) 0x60d740, Run 0x60d6b0, RunWithIdle 0x60d6d0.
  Win32::CWindowBase (0x65d854): +4 HWND. PostMessage 0x60f9b0, Destroy
      0x60fbf0, Show 0x60fe40.
    Win32::CWindow (0x65d85c): slots 1 Create(title) 0x60fea0, 2
      GetWindowClass(WNDCLASS*) 0x60ffa0 (class "CWindow"), 3
      WindowProc(hwnd, msg, wParam, lParam) 0x610020 (DefWindowProcA).
      CreateEx(CREATESTRUCT*) 0x60ff00 (non-virtual). Static window
      procedure at 0x610040 (no inventory row: inside the Ghidra function
      0x610020): finds the CWindow through GetWindowLongA(offset
      0x7e9170), attaches 0x7e9174 (the window being created) on the first
      message, forwards to slot 3.
      Win32::CMainWindow (0x65d870): 3 WindowProc (WM_DESTROY -> 4),
        4 OnDestroy = PostQuitMessage(0).
  Win32::CCDAudio (0x65d944, global instance 0x7a7a00): Open 0x615520.
```

Non-RTTI helpers (class names invented, medium):
`Win32::CCriticalSection` (ctor/dtor/Enter/Leave 0x60d650..0x60d680,
`this` is the CRITICAL_SECTION) and the scoped lock `Win32::CLock`
(0x60da00/0x60da20, 119/126 callers).

### Thread / wait / timer model

- A thread object is started with `CThread::Start(parent)`; the thread
  procedure 0x60ec20 calls slot 7 `Execute`: OnStart, Main, OnExit, with
  C++ and structured exceptions caught and passed to slot 8
  `HandleUnhandledException`. The base `OnStart` installs an SE translator
  (0x610380) that converts Windows exceptions into
  `Win32::CStructuredException`.
- `HandleUnhandledException` forwards the exception as thread message
  0x600 (wParam = kind, lParam = exception object) to the parent thread
  if that is a `CMessagedThread`, else to the application thread; the
  `CWindowedApp` receives it in `OnThreadMessage` and rethrows it in
  `OnException`, where `CApplication::HandleUnhandledException`
  terminates the process. Without either: MessageBox "Error!" + exit(-1).
- Waiting: `CWin32Waitable::Wait` = WaitForSingleObject; the wait sets
  (`CWin32WaitableBag<T>`, a Watcom WCPtrSList at +4) are instances of a
  template from synchronize.ipp: 0x463100 (plain WaitForMultipleObjects,
  used by CLogicThread::Step), 0x60da30 (MsgWaitForMultipleObjects loop
  pumping a CMessageQueue) and 0x60dcd0 (one MsgWaitForMultipleObjects).
  All throw `CTimeout` with "CWin32Waitable::WaitForXYZ" on a timeout.
- `Win32::GetTimeout(i)` 0x60df50: framework timeouts 5 s / 30 s, both
  INFINITE when the game runs with `-debug` (flag 0x7e916c, set at 0x405f2f).
- CTimer prefers kernel waitable timers (looked up dynamically); without
  them it uses timeSetEvent with an internal CEvent signalled from the
  callback 0x60d640.

### Exceptions

Hierarchy (virtual base CObject, +4 vbase pointer, +8 message string):
CException -> CProgrammingError -> CAssertmentFailed; CException ->
CRunTimeError -> CFileNotFound, CHResult, CMCIException, CMMException,
COutOfMemory, CStructuredException, CTimeout. Slot 1 of every one is
`Describe(string* result)` (texts quoted in the TSV). CStructuredException:
+0x18 code, +0x1c EXCEPTION_RECORD (0x50), +0x6c CONTEXT (0x2cc), 0x338
bytes. Copy constructors (throw-info, no direct callers): 0x6103c0
CStructuredException, 0x610510 CRunTimeError, 0x610690 CException.
Throw helpers, all fastcall (ECX expression, EDX file, stack line[, code]):
ThrowAssertmentFailed 0x6119b0, ThrowOutOfMemory 0x610960, ThrowWin32Error
0x610de0, ThrowTimeout 0x610ba0, ThrowHResult 0x611050, ThrowMMError
0x6112a0, ThrowMCIError 0x6114f0.

### Corrections of existing names (not in the TSV)

- 0x00463100 `Win32::CWin32Waitable::WaitForXYZ` (identified, passthrough
  in fast.cpp): `this` is a `CWin32WaitableBag<CWin32Waitable>` (list at
  +4), not a CWin32Waitable; better
  `Win32::CWin32WaitableBag_Win32_CWin32Waitable::WaitFor` (the string
  "CWin32Waitable::WaitForXYZ" is only the CTimeout text shared by all
  bag waits). Rename touches the hook code, so maybe TODO.md.
- 0x00466200 `Menu_sub_466200` (mov eax,1; ret): CMessagedThread slot 3,
  i.e. `Win32::CMessagedThread::IsMessagedThread` (also folded with any
  other `return 1` slot; check its other xrefs).
- 0x0042cf80 `Win32::CEvent::Set`: `Win32::CEvent::Set` (SetEvent(+8),
  ThrowWin32Error); folded into Draw.cpp's range.
- 0x00618250 `CTextException::vf01` is called as "get handle" on
  CWin32Handle subobjects (0x60d08b, 0x60dad6) – a folded `mov eax,[ecx+4]`
  style getter; its current name is misleading.
- 0x0041abd0 `Win32::CMessageQueue::vf02` -> `OnThreadMessage`;
  0x005b9e20 `DirectX_sub_5b9e20` is CMessageQueue slot 0 (deleting dtor).
- framework_layout.h `CMessageQueue::owner` (+4) is the HWND filter passed
  to GetMessageA (CMessageQueue constructor argument), not an owner thread.
  `CThread::unk14` = thread id, `unk24` = parent thread.
- 0x0060f010 lacks the `Win32::` namespace (in the TSV).
- 0x00615520/0x00615601 are cdaudio.cpp code (strings, callees in
  0x615740.. use "E:\Siedler3\Win32\cdaudio.cpp"); the Classes.cpp range
  end in re/source_files.csv is a little too far.
- Missing starts: 0x60ec20 (CThread thread procedure), 0x610380 (SE
  translator), 0x610040 (static window procedure), 0x6101a0 (body of the
  CApplication deleting destructor) are inside other Ghidra functions.

### Skipped

- 0x0060eff0 `Win32_sub_60eff0`: `mov ecx,[ecx+8]; jmp 0x61b6e0` (Detach.cpp
  routine on the HINSTANCE walking the PE sections > 2 MB); purpose of
  0x61b6e0 not clear enough.
- 0x00614c40 `Win32_sub_614c40`: destroys an ostrstream member at +8, no
  callers (probably an unwind helper of CStructuredException::Describe).
- Existing RTTI constructor/destructor names in the range were checked
  where touched and left as they are.

## Win32 framework 0x615740..0x61e4ef: naming notes (w32b)

Scope: the 83 rows with category game/watcom and status `classified` in
0x615740..0x61e4ef. Proposed: 57 (45 high, 12 medium) in `w32b.tsv`.
Skipped: 26 rows whose current names are already right (RTTI
constructors, destructors and scalar deleting destructors, the
WCValSkipListDict / WCValOrderedVector library rows, the session-50 names
CFileMapping::Close/MapView/UnmapView, CInternetConnection::FTPConnect,
CFTPSession::GetFile, Win32::DetachMemory). Those may be set to
`identified` as they are. The iostream rows (crt) after 0x61c4f0 were not
in scope.

### Win32::CCDAudio (Win32/cdaudio.cpp; vtable 0x65d944, 1 slot)

Global instance at **0x7a7a00** (not in re/globals.csv yet).

| offset | field |
|---|---|
| +0 | vtable |
| +4 | HINSTANCE (Init arg 1) |
| +8 | MCI device id (0 = closed) |
| +0xC | HWND of the hidden callback window |
| +0x10 | HWND to forward MM_MCINOTIFY to (Init arg 2) |
| +0x14 | last MM_MCINOTIFY wParam (notify code) |

- `0x6154c0` ctor (zeroes), `0x615500` ~CCDAudio (Close if open),
  **`0x615520` (Win32_sub_615520, out of range) = `CCDAudio::Init(hInstance,
  notifyHwnd)`**: CreateCallbackWindow, Open, SetTimeFormatTMSF; on an
  exception it closes again. Its caller is `CConsoleThread::vf04` (0x404dcf).
  `0x615601` is a false function start inside Init.
- Threading: the public `Play(from, to)` (0x615ea0) and `Stop()` (0x615f70)
  only **PostMessage** 0x600 / 0x601 to the hidden window; its window proc
  (**0x615bd0, missing from the inventory**) runs `DoPlay` / `DoStop` on
  the thread that created the window. Message 0x602 returns
  `GetTrackCount()` (for SendMessage). WM_CREATE inserts HWND -> CCDAudio*
  into the static `WCValSkipListDict<HWND, CCDAudio*>` at **0x7e9180**
  (lookup key scratch at 0x7e91f8, ctor stub `StaticInit_7e9180`, atexit
  stub 0x615bb0); WM_DESTROY removes it; MM_MCINOTIFY (0x3b9) stores wParam
  at +0x14 and forwards the message to +0x10.
- Time format TMSF, so `from`/`to` are track numbers; `DoPlay` plays from
  track `from` to the end of track `to` (MCI_TO = to+1 when to < track
  count, else to the disc end), with MCI_NOTIFY so the end of a track is
  reported (looping is up to the receiver of MM_MCINOTIFY).
- Callers of Play: credits dialog (track 10 or 12), `Logic_sub_53f5f0`
  (music choice from the table at 0x730b60, indexed by 3 * [+0x660] +
  [+0x658]), Stop from the credits dialog and `Logic_sub_53f5c0`. The
  console help text lists `-cdaudioXY` ("Spielt den Audiotrack XY ab").
  The registry value `CDAudio` (General key) is read by the config reader.
- Errors in Open/Close go to `0x6114f0` (Win32_sub_6114f0, Classes.cpp,
  "error check" + MCI error code): probably the MCI error throw
  (ThrowMciError); out of scope, not renamed.

### Win32::CRegistryKey (Win32/Registry.cpp; vtable 0x65d98c)

Fields: +0 vtable, +4 HKEY. Every method throws through
`Win32::ThrowWin32Error` (line numbers in the evidence) except where noted.

| address | name | API / behaviour |
|---|---|---|
| 0x616430 | CRegistryKey() (verified) | hkey = 0 |
| 0x616460 | CRegistryKey_copy | reopen other.hkey with KEY_ALL_ACCESS |
| 0x616490 | ~CRegistryKey | Close |
| 0x6164a0 | Create(parent, sub, class, options, sam, sa, pDisp) | RegCreateKeyExA |
| 0x6166e0 | DeleteSubKey(name) | RegDeleteKeyA |
| 0x616900 | Open(parent, sub, sam) | RegOpenKeyExA |
| 0x616b20 | Close() | RegCloseKey |
| 0x616d50 | QueryInfo(10 pointers) | RegQueryInfoKeyA |
| 0x616f90 | EnumKey(index, ...) -> bool | RegEnumKeyExA, false at end |
| 0x6171f0 | GetSubKeyCount | |
| 0x617220 | GetSubKey(index) -> CRegistryKey | |
| 0x617320 | GetValueCount | |
| 0x617350 | GetValueInfo(index) -> CRegistryValueInfo | |
| 0x6175c0 | EnumValue(index, ...) -> bool | RegEnumValueA |
| 0x617810 | QueryValue(name, pType, data, pcb) | RegQueryValueExA |
| 0x617a40 | QueryString(const string&) -> string | |
| 0x617c40 | QueryDword(const string&) -> DWORD | |
| 0x617c80 | SetValue(name, type, data, cb) | RegSetValueExA |
| 0x617eb0 | SetString(const string&, string, bool expand) | REG_SZ / REG_EXPAND_SZ |
| 0x618000 | SetDword(const string&, DWORD) | REG_DWORD |
| 0x618030 | ValueExists(const char*) -> bool | no throw for ERROR_FILE_NOT_FOUND |
| 0x618260 | operator_assign | Close + reopen |

`Win32::CRegistryValueInfo` (vtable 0x65d984): constructed from (std::string
name, dword); the dword is probably the value type or data (not checked).
`0x617350` was named `CRegistryValueInfo::Unknown_617350` because it stores
that vtable (inlined construction of the result), but it is a CRegistryKey
member (ECX = the key).

### Registry settings the game reads and writes

The key paths are std::string globals built at startup (ConsoleInit TU,
initializers 0x40bb40/0x40bca0): base `Software\BlueByte\Siedler3\1.0`
(0x7a7fc8) + `\General` (0x7a7fb8), + `\Index Server` (0x7a7fa8),
+ `\Patches` (0x7a7f98, by order; a second copy of the same strings exists
for another TU at 0x7a7838/28/18/08). Value names are std::string globals
too (0x7a7e08..0x7a7f88 in ConsoleInit; 0x7a7748..0x7a77f8 elsewhere).

**HKLM\Software\BlueByte\Siedler3\1.0\General**, read by `App_sub_40d700`
(ConsoleInit, opens with KEY_ALL_ACCESS, `ValueExists` then
`QueryDword`/`QueryString` for each; probably fills g_config 0x7a7af0),
written by `App_sub_40f4a0` (Create + Set):

| value | type | read | written | meaning (from the name) |
|---|---|---|---|---|
| Playername | string | yes | yes | player name |
| ClanFlag | dword | yes | yes | clan/flag choice (online) |
| NoAlpha | dword | yes | | graphics option |
| NoFlip | dword | yes | | DirectDraw: no page flipping |
| SystemBackBuffer | dword | yes | | back buffer in system memory |
| WaitVBlank | dword | yes | yes | wait for vertical blank |
| GDIMouse | dword | yes | yes | GDI mouse cursor |
| Resolution | dword | yes | yes | screen resolution |
| FogSpeed | dword | yes | | |
| ScrollMode | dword | yes | | |
| ScrollSpeed | dword | yes | | |
| BuildHelp | dword | yes | | building help |
| SoundFormat | dword | yes | | |
| CDAudio | dword | yes (twice) | | CD music on/off or mode |
| Intro | dword | yes | yes | play the intro |
| VideoFormat | dword | yes | | |
| MessageLevel | dword | yes | | |
| GameZoneSelection | dword | yes | yes | online server choice |
| FirstLogon | dword | yes | yes | online |
| Font | string | yes | yes | lobby/chat font |
| FontSize | dword | yes | yes | |
| BannerId, BannerSubId | dword | yes | yes | online banner |
| Mission, Plus, Gold | dword | yes | | installed add-ons (Mission CD, Plus, Gold edition) |
| Tips&Tricks | dword | yes | | |
| Newbie | dword | yes | yes | |
| Language | dword | yes | | |
| Campaigns, CampaignsPlus | (exists only) | ValueExists in App_sub_410370 | | |
| SingleNumber | string | App_sub_40fb40 (QueryString) | App_sub_40ff70 (SetString) | the CD key / serial; **this is the GOG patch**: S3_multi.EXE has "SerialNumber" here (string 0x695a1c / file offset 0x295a1c area), S3.EXE reads the nonexistent "SingleNumber" |

**HKCR\CLSID\{0e9f8400-d9a5-11cf-8f4d00804880b189}**, value `Seed`
(dword): read by App_sub_40fb40, App_sub_410370, App_sub_410910, written by
App_sub_40ff70. A fake COM class key used as a hidden store, most likely
copy-protection / serial related (it is next to SingleNumber). Not analysed
further.

**HKLM\...\1.0\Index Server**: opened by `App_sub_40d700` at 0x40f3bd,
then GetValueCount + GetValueInfo(i) loop: a list of online index servers
(default string "siedler3svr.bluebyte.de:3344" nearby).

**HKLM\...\1.0\Patches** (+ per-patch sub keys): the patch/update code
`App_sub_407c30`, `App_sub_407f2a`, `App_sub_408f10` (Console.cpp area)
and the FTP patch thread `Menu_sub_465520`/`Menu_sub_465e2d`: values
`InPatch`, `Convert`, `KeyName`, `FileName`, `Version`, `Installed`,
`PatchId` ("PATCH: File installed in registry with k...", "Siedler3 Patch
Info"); `DeleteSubKey` at 0x409135 removes a patch entry. The other string
globals there (update.exe, s3.exe, fileconvert.exe, gfx) are the files the
patcher handles.

### Internet (Win32/Internet.cpp): WinINet, loaded dynamically

- `Win32::priv::CWininetLoader` (empty base at +4 of every
  CInternetHandle): its constructor 0x6182c0 loads wininet.dll once
  (flags 0x7e9218 tried / 0x7e9219 failed) and resolves 6 functions into
  0x7e9204..0x7e921c. Each API is called through a small stub
  (0x6183f0..0x6184d0) that returns FALSE/NULL with
  ERROR_PROC_NOT_FOUND (0x7e) when the pointer is null. **Bug in the
  original:** after `GetProcAddress("InternetCloseHandle")` it tests the
  address of the stub 0x6183f0 (always nonzero) instead of the result, so
  a missing InternetCloseHandle is not reported (harmless: the stub checks
  the pointer itself).
- `Win32::CInternetHandle` (vtable 0x65d994): +4 HINTERNET, +8 bool owns,
  +0xC last status (slot 1 `OnStatusCallback(status, info, len)`).
  `Attach` installs the static callback **0x618a30 (missing from the
  inventory)**, which calls `((CInternetHandle*)dwContext)->vf1(...)`.
  `Close` calls InternetCloseHandle only if owned; `operator_assign`
  transfers ownership.
- `Win32::CInternetConnection` (vtable 0x65d9a0): `Open` = InternetOpenA;
  `FTPConnect` = InternetConnectA (INTERNET_SERVICE_FTP), returns a
  `CFTPSession`.
- `Win32::CFTPSession` (vtable 0x65d9ac): +0x10 bytes received (summed in
  its `OnStatusCallback` on INTERNET_STATUS_RESPONSE_RECEIVED);
  `GetFile` = FtpGetFileA. Slot 0 of both derived classes is the folded
  scalar deleting destructor 0x618a70.
- Users: the FTP patch download in `Menu_sub_465520` (0x464a8e, 0x464e6e
  construct CInternetConnection; 0x46593d Open; 0x4659be/0x465a0f
  FTPConnect; 0x465d3d GetFile; "FTP: File received"), probably the
  thread class `Menus::priv::CFTPThread`. Network/online code, low
  priority for us.

### Win32/Mapping.cpp

Nothing new beyond session 50. `CTextException::vf01` 0x618250
(`mov eax,[ecx+4]; ret`) is a linker-folded getter shared by 132 call
sites (e.g. the CFile handle in CFileMapping::Create); proposed
`Folded_GetMember4` (like `NoOp_retN`).

### Win32/Detach.cpp: running from CD

- `Win32::DetachModule(HMODULE, flags)` 0x61b190, called by
  `Win32_sub_60edb0` (Classes.cpp; ECX+8 = module) from
  `CConsoleThread::OnStart` (0x40ba0d) with flags 0x1F1F if a config
  value (0x49d8f0 on g_config) equals 2, else 0x1F09.
  Flag bits 0..4 select the drive type the exe runs from (removable, fixed,
  remote/UNC, CD-ROM, RAM disk; 0x09 = removable + CD-ROM), 0x1000 the PE
  header, 0x100/0x200/0x400/0x800/0x1000 the section kinds of
  `GetSectionDetachFlag`. For each selected region `DetachMemory`
  (0x61ab60) copies the pages into private memory (VirtualQuery,
  VirtualProtect), so the image no longer pages in from the CD, and the CD
  can be swapped (music CD) without crashing. `CGraphicsData` and
  `CSoundData::DetachFromMedium` call DetachMemory for their data too.
- `Win32::ChecksumModuleCode` 0x61b6e0 (+ `ChecksumBlock` 0x61b6a0): a
  64-bit checksum of the large code section, called through
  `Win32_sub_60eff0` (ECX+8 = module) in `CConsoleThread::OnStart`
  (0x40b417) and stored in **g_buildInfo 0x7a8000** (64 bit). It is
  compared at 0x4436ac and 0x44b19d (network/lobby code), so it is most
  likely an exe identity check between multiplayer peers. The globals.csv
  note for g_buildInfo could say "checksum of the code section".

### Exception report (0x61b990..0x61c4b8)

This is Bruce Dawson's public `ExceptionHandler.cpp` ("Release mode
debugging", Game Developer Magazine 1998), with his function names:
RecordExceptionInfo (entry, called by `Win32::CStructuredException`'s
constructor 0x614610 at 0x6147ac), hprintf, RecordModuleList,
ShowModuleInfo, PrintTime, RecordSystemInformation,
GetExceptionDescription, GetFilePart. It writes the game's
`errorlog.txt` (the crash reports CLAUDE.md mentions). These functions
have no assert path, so `re/source_files.csv` puts them in
Win32/Detach.cpp; they are probably a separate file (source_file is an
estimate; nothing to fix by hand). Namespace `Win32::priv::` for the
helpers is a guess (in Dawson's code they are file-static).

### Corrections of existing names (no verified rows affected)

- `Win32::CRegistryValueInfo::Unknown_617350` is a CRegistryKey method
  (`GetValueInfo`), not a CRegistryValueInfo one.
- `Win32::CRegistryKey::ctor_or_dtor_616460` is the copy constructor.
- `CTextException::vf01` 0x618250 is a folded getter, not specific to
  CTextException.
- `Win32_sub_615520` (out of range, Classes.cpp) = `Win32::CCDAudio::Init`;
  `Win32_sub_615601` is a false start inside it.
- Missing function starts: 0x615bd0 (CD audio window proc), 0x615bb0
  (atexit stub of the dictionary), 0x618a30 (WinINet status callback
  thunk).
- Verified rows in the range (CRegistryValueInfo ctor 0x6161e0,
  CRegistryKey ctor 0x616430, CFile ctor 0x6195c0): names are fine.

