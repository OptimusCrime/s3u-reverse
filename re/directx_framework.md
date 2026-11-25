# DirectX framework (DirectX\*.cpp): findings of the naming pass

Findings of the naming pass of session 50 (2026-11-24) over the DirectX
application framework (Gamebase.cpp, Dxbase.cpp, dinput.cpp: part 1) and
the DirectDraw/DirectSound wrappers (part 2). Read from the code, not yet
confirmed by reimplemented functions. Function names are in
`re/functions.csv` (notes "Named (session 50, ...)"). Network code
(netsession.cpp, Dplay.cpp) was left out.

## dx1: DirectX application framework (0x5b50ad..0x5ba380)

Covers DirectX/Gamebase.cpp, DirectX/Dxbase.cpp, DirectX/dinput.cpp and the
Watcom containers instantiated there. 88 proposals in `dx1.tsv` (64 high,
24 medium). The 52 classified rows left out already carry correct
RTTI-derived constructor/destructor names (checked: each one stores its own
vtable or calls the matching destructor). Network classes (CConnectionShortcut)
were not touched.

### Class structure

```
Win32::CThread (Execute 0x60e2d0)
  DirectX::CGameThread            vtable 0x65ca04, 11 slots; +0x28 active (applied), +0x29 pause-when-inactive,
  |                               +0x2c request flags, +0x30 requested active, +0x34 lpSignalGuard (critical section),
  |                               +0x38 lpSigActive (CEvent), +0x3c exit code, +0x40 iCoInitOpen
  |   CLogicThread, CNetThreadBase (CNetThread, CNetBounceThread), Menus::CNetThread
  |   DirectX::CGameDrawThread    14 slots; +0x44 lpDrawer, +0x48 HWND, +0x4c frame counter, +0x50 full-redraw flag;
  |                               global 0x7e8bec = the draw thread
  |       CDrawThread, Menus::CDrawThread
  Win32::CApplication -> Win32::CWindowedApp -> DirectX::CApplication (+0x1c active flag)
      -> DirectX::CGameApp (+0x20 CWin32WaitableBag<CGameThread>: list of game threads at +0x24, count +0x30)
      -> DirectX::CGameAppDraw (+0x3c lpMainWindow, +0x40 owns-main-window flag)
      -> CConsoleThread
Win32::CWindow -> CMainWindow -> DirectX::CDirectXMainWindow -> DirectX::CGameMainWindow -> CSiedlerMainWindow
      (global 0x7e8be8 = the CGameMainWindow)
DirectX::CDirectDraw -> DirectX::CGameDrawer -> CDrawer / Menus::priv::CDrawer
      CGameDrawer: +0xc HWND, +0x10 WCPtrSList<CSurface>, +0x28 WCPtrSList<CPalette>,
      +0x40 WCValOrderedVector<CDirectDrawDevice> (0x30-byte records: GUID, description, name),
      +0x5c WCValOrderedVector<CDisplayMode> (108-byte DDSURFACEDESC), +0x78 primary surface, +0x7c flag;
      global 0x7e8bf0 = the drawer; 0x7e8bf8..0x7e8c04 = primary surface rectangle
DirectX::CDirectInputErrorHandler -> DirectX::CDirectInputDevice (+ CWin32Waitable at +4; +0x1c IDirectInputDevice*)
      -> DirectX::CDIMouse (+0x20 lpEvent CEvent whose handle is also the waitable handle)
```

The application classes use virtual inheritance: the CThread part is a
virtual base (+0x28 in CApplication, +0x40 in CGameApp, +0x48 in
CGameAppDraw, +0xd8 in CConsoleThread). The `vfNN_subXX_thunk` rows are the
vtordisp thunks into it; the vbase deleting destructors are the
`scalar_deleting_destructor` bodies proposed for 0x5b9100, 0x5b8760, 0x5b8830.

### Slot meanings

Thread vtable (Win32::CThread and everything below it; also the CThread
vbase vtable of the application classes):

| slot | meaning | evidence |
|---|---|---|
| 0-2 | CWin32Waitable (0 = Wait) | |
| 3 | returns 0 (CThread) / 1 (messaged threads, 0x466200); purpose unknown | |
| 4 | OnStart | Execute; CGameThread 0x5b6160, CGameApp 0x5b5510, CApplication 0x5b8d40, CWindowedApp 0x60f880, CGameDrawThread 0x5b65d0, CGameAppDraw 0x5b5a30 |
| 5 | Main | CGameThread::Main 0x5b63d0, CConsoleThread::Main 0x404860 |
| 6 | OnExit | CGameThread 0x5b6470, CGameDrawThread 0x5b6670, CGameApp 0x5b5530, CApplication = CoUninitialize |
| 7 | Execute | 0x60e2d0 |
| 8 | HandleException (CApplication::HandleUnhandledException 0x60f010; apps 0x40a7f0) | |
| 9 | Step (pure in CGameThread) | Main |
| 10 | OnActivate(bool) (was "OnEvent") | Main calls it when SetActive set flag bit 1 |
| 11 | CGameDrawThread: StartWithWindow(HWND) | |
| 12 | CGameDrawThread: create the drawer (pure; CDrawThread::CreateDrawer 0x42c6a0) | OnStart, assert "lpDrawer" |
| 13 | CGameDrawThread: draw one frame (bool full redraw) (pure; Menus::CDrawThread::Update) | Step |

Application primary vtable (CWindowedApp -> CConsoleThread):
0, 1 = CWindowedApp (not analysed), 2 = SetActive(bool), 3 = StopThreads,
4 = Initialize (called by CGameApp::OnStart), 5 = Shutdown (called by
CGameApp::OnExit), 6 = get/create main window (bool* ownsWindow) (pure in
CGameAppDraw; CConsoleThread::vf06 0x405b00 returns +0x54 and sets the flag
to 0), 7 = get the draw thread (pure; CConsoleThread::vf07 0x405b10 picks
+0x94 or +0x8c by mode +0x44).

Window vtable: 0 deleting dtor, 1 Create(title), 2 CWindow::vf02, 3
WindowProc, 4 CMainWindow::vf04, 5 OnActivateApp, 6 OnClose, 7
OnEraseBackground, 8 OnPaint, 9 OnSetCursor, 10 OnSysCommand
(CSiedlerMainWindow continues to slot 30).

CGameDrawer vtable: 0 deleting dtor, 1 Create(GUID*), 2 Destroy, 3
CDirectDraw::vf03 (checks GetApplication()->IsActive()), 4 RestoreSurfaces,
5 ReleaseSurfaces, 6 ReleasePalettes, 7 Initialize(HWND), 8 Shutdown, 9
CreateOnDevice(device), 10 set up the display (pure; CDrawer 0x42ca70 sets
the mode from globals 0x730f0c..), 11 (pure; CDrawer 0x42ca90 forwards to
slot 12).

Exceptions: slot 1 of Win32::CException and its subclasses builds the
description string (CDirectInputException, CAppNotActiveException).

### Thread and event model

- WinMain runs `CConsoleThread::Execute` on the application object (the UI
  thread). CGameApp::OnStart -> CApplication::OnStart (message queue,
  COM) -> Initialize. CGameAppDraw::Initialize gets the main window
  (slot 6) and the draw thread (slot 7), creates/shows the window, starts
  the draw thread with the HWND (slot 11 -> CGameThread::Start ->
  CThread::Start with the app thread as parent: `_beginthreadex`
  suspended + ResumeThread) and adds it to the app's thread list
  (AddThread). CConsoleThread also starts and adds its other game threads
  (0x405450/0x405630 call CGameThread::Start + AddThread).
- Each game thread runs CGameThread::Main: under the guard lock it checks
  the request flags (+0x2c): bit 0 RequestQuit -> return 0; bit 2
  RequestStop(code) -> return code; bit 1 (SetActive) -> slot 10
  OnActivate(requested state). Otherwise, if the thread is inactive and
  set to pause when inactive (+0x29), it waits on lpSigActive with
  GetTimeout(1) = 1 s, else calls Step (slot 9).
- Activation: WM_ACTIVATEAPP -> CDirectXMainWindow::OnActivateApp ->
  app SetActive(bool) (CGameApp::SetActive) -> every thread's
  CGameThread::SetActive (flag + set/reset lpSigActive) and the app's own
  flag +0x1c, which CDirectDraw/CDirectSound check (IsActive) and which
  CAppNotActiveException reports.
- Shutdown: WM_CLOSE -> CGameMainWindow::OnClose -> app StopThreads
  (RequestQuit on all, wait up to GetTimeout(0) = 25 s each, delete) ->
  DefWindowProc(WM_CLOSE). OnExit -> Shutdown (stop threads again,
  destroy the window if owned) -> CoUninitialize.
- GetTimeout table 0x79e6f0: {25000, INFINITE}, {1000, 1000}; the second
  column is used when the dword 0x7e916c is non-zero (probably a debug
  switch; not checked who sets it).
- The draw thread's Step increments a frame counter and calls slot 13 with
  a full-redraw flag that is set at start, on activation and by
  RequestFullRedraw (0x5b6730).

### How input is read (dinput.cpp / Dxbase.cpp)

- CDirectInputDevice wraps an IDirectInputDevice (+0x1c); its DI* methods
  (already named: DIAcquire, DIUnacquire, DIGetDeviceData,
  DISetCooperativeLevel, DISetDataFormat, DISetEventNotification,
  DISetProperty) call the COM method and pass the HRESULT to
  CDirectInputErrorHandler::CheckResult (throws CDirectInputException on
  failure, logs positive codes; ErrorCodeToString maps codes to names).
- CDIMouse::Initialize (called from CConsoleThread::vf04, the console's
  Initialize override): Create(GUID_SysMouse), SetDataFormat(c_dfDIMouse),
  SetEventNotification(its own CEvent). The event's handle is also the
  device's waitable handle, so the console thread can wait on the mouse
  together with its other waitables.
- The console loop 0x404889 polls with HasBufferedData (GetDeviceData
  with DIGDD_PEEK, count INFINITE) and reads with ReadBufferedData
  (buffered DIDEVICEOBJECTDATA records of 16 bytes). The keyboard is not
  read through DirectInput here (no other CDirectInputDevice subclass).

### Corrections of existing names (for the inventory)

- 0x5b6520 `DirectX::CGameThread::OnEvent` -> OnActivate (in dx1.tsv). The
  inventory note of the verified-free row 0x5b63d0 `CGameThread::Main`
  should then read "bit1 activation -> slot 10 (OnActivate(+0x30))", "bit2
  RequestStop: returns +0x3c (exit code)" and mention the wait on
  lpSigActive while inactive. CLAUDE.md "Runtime structure" says "slot 10
  OnEvent"; same correction.
- 0x5b6790 `GuidNames_Construct_5b6790` (verified; renamed 2026-11-24 to
  `DirectX::CDirectDrawDevice::CDirectDrawDevice_guid`, ours
  `CDirectDrawDevice_Construct`) is
  `DirectX::CDirectDrawDevice::CDirectDrawDevice(const GUID*, const char*
  description, const char* name)`, the constructor used by
  CGameDrawer::EnumDevicesCallback (0x30-byte record; default constructor
  0x5b6740, copy constructor 0x5b8560, operator= 0x5b69e0). Suggest
  renaming it to `DirectX::CDirectDrawDevice::CDirectDrawDevice_guid` (or
  similar) in the verified-renames pass.
- 0x5b53d0 was `Win32::CWin32WaitableBag_DirectX_CGameThread::ctor_or_dtor`:
  it is ~CGameApp (the bag is only a member).
- 0x5b7fd0/0x5b7fe0/0x5b8030 named as WCIsvSListBase constructors are
  destructors (store the base vtable and jump to ~WCIsvListBase).
- 0x5b7ec0/0x5b7f40 (`WCBareVectorBase_...::ctor_or_dtor`) are the
  WCVectorBase<..> constructors (they end on vtable 0x65cb88/0x65cb9c).
- 0x5b8fb0 `ImportThunk_5b8fb0` (identified, `jmp [CoUninitialize]`) is
  also DirectX::CApplication::OnExit (thread slot 6 through thunk 0x5b90c0
  and called by CGameApp::OnExit): the compiler folded the one-call body
  into the import thunk. Could be noted in its row.
- 0x618250 `CTextException::vf01` is used as a "get handle" getter (window
  HWND before ShowWindow-related calls, CEvent handle in CDIMouse): a
  folded getter, the name is misleading (out of range, not proposed).
- Outside this range, suggested from this analysis: 0x60e060
  Win32::CThread::Start(CThread* parent) (_beginthreadex suspended with
  proc 0x60ec20, parent stored at +0x24, ResumeThread); 0x60f880
  Win32::CWindowedApp::OnStart; 0x463b50 WCValListBase base_new_link (list
  link allocator, +0x10 user allocator); 0x7e8c08 StopThreads reentrancy
  flag; 0x7e916c debug/infinite-timeouts flag; CConsoleThread::vf04/vf05/
  vf06/vf07 = Initialize/Shutdown/GetMainWindow/GetDrawThread overrides;
  0x60d650/0x60d660/0x60d670/0x60d680 = critical section
  ctor/dtor/Enter/Leave (CGameThread lpSignalGuard); 0x42cf80/0x523110 =
  CEvent Set/Reset (folded copies in Draw.cpp / LogicDraw.cpp);
  0x5b7810 (missed function start inside 0x5b7670's range) = the
  EnumDisplayModes callback (stdcall 2 args, appends a CDisplayMode).
- Folded one-liners proposed with generic names: 0x5b6c20 `ReturnThis`,
  0x5b7160 `SetMemberByte7CToOne` (also called on GameSettings).

### Watcom container naming used

base_destroy_link (list slot 1), base_equivalent (slot 2 of WCPtrSList /
WCValOrderedVector), base_find (WCValOrderedVector slot 3), base_init_upto
(WCBareVectorBase slot 1), resize. These follow the Watcom library's
internal method style but are medium: the exact Watcom names were not
checked against its headers.

### Scratch files

My helper files in the shared scratchpad root (vt.py, ann.py, ann2.py,
dx.asm, dxn.asm, imports.txt, todo.txt) were written there before I saw
that other agents use it; names/make_dx1.py regenerates dx1.tsv.

## DirectX wrappers: surfaces and sound effects (Ddraw.cpp / Dsound.cpp ranges)

Proposals: `dx2.tsv` (52 rows: 37 inside 0x5bf400..0x5c1140 and
0x5c2d20..0x5c4930, plus 15 misnamed neighbours just outside the ranges that
the model depends on, each marked "Neighbour" in its evidence).
41 high, 11 medium.

### The source-file ranges are shifted

The `__FILE__`-based ranges are off by about one file here:

- 0x5bf400..0x5bfd60 really is Ddraw.cpp (surfaces). The earlier
  "netsession.cpp" rows 0x5bead0..0x5bf3e0 are Ddraw.cpp too (CDirectDraw,
  CSimpleSurface, CSurface): their `Net_sub_*` names are wrong (fixed for the
  surface ones in the TSV).
- 0x5bfdb0..0x5c1140 is DirectPlay (CDirectPlayException, CDirectPlay,
  CPlayer: Dplay.cpp). Skipped as out of scope (see below).
- 0x5c15d0..0x5c2d20 ("Dplay.cpp") is the DirectSound wrapper (Dsound.cpp:
  CDirectSoundException, CDirectSound, CDirectSoundBuffer, CSoundFile,
  CSoundEffectBuffer, CSoundEffect); its `Net_sub_*` names are wrong.
- 0x5c2d20..0x5c3740 ("Dsound.cpp") is the rest of the sound-effect code
  and the Watcom dictionary instances.
- 0x5c3740..0x5c4140 is **not sound code**: CPU detection and a benchmark
  that picks the fastest surface-copy routine (an unnamed file). Named as
  free functions `Cpu_*` / `SurfaceCopy_*` because the class/file is unknown.
- 0x5c43c0.. is lobby code (LobbyClient::priv::CHelperThread and
  CLobbyServer containers).

### Surface classes (RTTI) and fields

```
DirectX::CSimpleSurface (0x14, vtable 0x65cda0)
  +0x04  IDirectDrawSurface* (QueryInterface'd to IDirectDrawSurface3 when
         available; DDCreateSurface 0x5bec80 returns the version: 1 or 3)
  +0x08  the interface Blt/BltFast/Lock/Unlock/GetSurfaceDesc work on:
         = +0x04 for normal surfaces (0x5bf2a0), the attached back buffer
         for the flip chain
  +0x0c  owner: the CDirectDraw (in practice the CDrawer/CGameDrawer), set
         by SetDirectDraw 0x5bf2b0; its slot 3 restores lost surfaces
  +0x10  interface version (1 or 3), masks the DDLOCK flags in Lock 0x5bf050
DirectX::CSurface : CSimpleSurface (vtable 0x65cda8, 3 slots)
  slot 0 scalar deleting destructor, 1 Release (0x5bf2c0), 2 RestoreIfLost
  +0x14  extra DDSCAPS bits (SetCaps 0x5bf3d0), OR-ed in by every Create
  +0x18  DDPIXELFORMAT, 32 bytes (SetPixelFormat 0x5bf3e0); dwSize 0 = none
  +0x38  RECT 0,0,width,height (DDGetSurfaceDesc 0x5bf310)
  +0x48  zeroed by the constructor (unknown)
DirectX::COffscreenSurface : CSurface (0x50, vtable 0x65ce08)
  +0x4c  1 after Create, 0 after a restore (probably "contents valid")
  Create(width, height) 0x5bfaf0: DDSCAPS_OFFSCREENPLAIN
  -> CLandscapeSurface, CStaticSurface's surface, the window back buffer
DirectX::COverlaySurface : CSurface (vtable 0x65ce18)
  Create 0x5bfbc0 (OVERLAY|VIDEOMEMORY), DDUpdateOverlay 0x5bfc70, Hide 0x5bfc50
  -> Dirk::CMouseSurface (the hardware mouse-cursor overlay)
DirectX::CPrimarySurface : virtual CSurface (vbptr at +0, CSurface at +4; 0x50)
  Create 0x5bf800: DDSCAPS_PRIMARYSURFACE
DirectX::CFlipSurface : virtual CSurface (vbptr +0, backBufferCount +4,
  wrapper +8, back-buffer CSimpleSurface +0xc, CSurface at +0x10)
  GetBackBuffer 0x5bf670, DDFlip 0x5bf740, Flip 0x5bf710 (loop until DD_OK)
DirectX::CPrimaryFlipSurface : CPrimarySurface, CFlipSurface (0x60;
  CFlipSurface part at +4, CSurface vbase at +0x14)
  Create 0x5bf9b0: PRIMARY|FLIP|COMPLEX, 1 back buffer, GetAttachedSurface
```

Virtual slots of the CSurface family (vtable in the CSurface part):
0 = scalar deleting destructor, 1 = `CSurface::Release` 0x5bf2c0 (all),
2 = `RestoreIfLost` (CSurface 0x5bf390, COffscreenSurface override
0x5bfb80 that also clears +0x4c; CLandscapeSurface has its own slot 2
0x54cbc0). Slot 0 for COffscreenSurface and COverlaySurface is one folded
function 0x5bfad0.

`DirectX::CDirectDraw` (vtable 0x65cd80): slot 1 Connect 0x5bec00
(CoCreateInstance CLSID_DirectDraw / IID_IDirectDraw2, Initialize),
2 Disconnect 0x5bec50, 3 RestoreSurfaces 0x5bee30 (throws
CAppNotActiveException when the app is not active, else slot 4),
4 (CGameDrawer::vf04 0x5b72e0: RestoreIfLost on each surface of the
WCPtrSList<CSurface> at drawer+0x10), 5/6 no-ops in CDirectDraw.
CGameDrawer keeps a WCPtrSList<CPalette> at +0x28; vf06 0x5b7390 releases
each palette's IDirectDrawPalette (CPalette::Release 0x5bfd60, CPalette has
no RTTI vtable).

### Display / flip / lock model

`CDrawer::InitSurfaces` (0x42e430) builds the screen. Drawer fields: +0x5f6c0 the
CPrimaryFlipSurface (fullscreen), +0x5f6c4 the primary surface (CPrimarySurface,
or the flip surface's CPrimarySurface part), +0x5f6c8 the surface the game
draws into, +0x5f6e4 = 1 in flip mode, +0x5f6f8 the selected copy routine.

- **Windowed / non-flip modes** (two branches decided by the config object
  0x7a7af0): `new CPrimarySurface(1)`, SetDirectDraw(drawer), Create; then a
  `COffscreenSurface` of the window size (globals 0x730f0c/0x730f10) with
  SetCaps(0x800 = DDSCAPS_SYSTEMMEMORY in one branch) as the draw target.
  Presenting = copying the back buffer to the primary with the routine at
  +0x5f6f8 (call at 0x42ce2e: `routine(ecx = back buffer, edx = primary, 1,
  rect, rect)`).
- **Fullscreen flip mode**: `new CPrimaryFlipSurface(1, 1)`,
  SetDirectDraw, `CPrimaryFlipSurface::Create` (primary + 1 attached back
  buffer: +4 = front, +8 = back), the draw target is
  `CFlipSurface::GetBackBuffer()` (a CSimpleSurface whose +4 and +8 are the
  back buffer). Presenting = `CFlipSurface::Flip(0)` (0x42ce4f), which loops
  `IDirectDrawSurface::Flip(NULL, 0)` until DD_OK.
- **Choosing the copy routine** (0x42eb36 and 0x42f288):
  `SurfaceCopy_SelectFastest(src, dst)` runs `Cpu_Detect` and times five
  routines with `SurfaceCopy_MeasureTime` (QueryPerformanceCounter, up to 50
  runs): rep movs (0x5c3a40 -> LockedCopy + 0x5c3a60), FPU fild/fistp
  (0x5c3d40 -> 0x5c3d60), MMX movq (0x5c3ee0 -> 0x5c3f00, only with MMX),
  `CSurface::DDBlt` with SRCCOPY (0x5c4070) and `CSurface::DDBltFast` (0x5c40f0).
  The fastest (smallest average time) wins; BltFast is the default. All five
  are `fastcall(src, dst, rectCount, rects, rects)` with RECTs 16 bytes apart.
  The small wrappers 0x5c3a40, 0x5c3a60, 0x5c3d40, 0x5c3ee0, 0x5c4070,
  0x5c40f0 are not in the function lists yet (only reached through the table
  in 0x5c3740); candidates for `re/extra_functions.tsv` (kind `manual`).
- **Locking for drawing**: `CSurface::Lock(rect, desc, flags)` 0x5bf050
  (19 callers in the UI/drawing code, e.g. CDrawer::InitSurfaces 0x42e80d, the static
  images) fills a DDSURFACEDESC, defaults to DDLOCK_WAIT, masks the flags by
  interface version and retries `DDLock` (IDirectDrawSurface::Lock +0x64 on
  +8) while WASSTILLDRAWING. Unlock is done inline by the callers
  (IDirectDrawSurface::Unlock +0x80, error name "CSurface::DDUnlock", retried
  once after SURFACELOST).
- **Lost surfaces**: every wrapper call routes its HRESULT through
  `CSurface::CheckResult` (verified, 0x5bf2e0): DDERR_SURFACELOST -> owner
  slot 3 `CDirectDraw::RestoreSurfaces` -> (app active) slot 4 ->
  `RestoreIfLost` on every registered surface -> `DDRestore` (0x5bf400:
  IDirectDrawSurface::Restore +0x6c; DDERR_WRONGMODE / NOEXCLUSIVEMODE ->
  DebugLog "DDRAW: WrongMode oder NoExclusiveMode" and a thrown
  CAppNotActiveException). Callers retry the failed call once after a
  SURFACELOST result. COffscreenSurface clears +0x4c after a restore, so its
  owner knows the contents must be redrawn.

### Sound effects

```
DirectX::CDirectSoundBuffer (vtable 0x65cecc): +4 error handler (also the
  CDirectSoundErrorHandler base), +8 IDirectSoundBuffer? (see note below)
  slot 3 = restore; CheckResult 0x5c2000 calls it on DSERR_BUFFERLOST
DirectX::CSoundEffectBuffer : CSecondaryBuffer (0x10, vtable 0x65cf38)
  +0x04 IDirectSoundBuffer*, +0x0c owner CSoundEffect*
  slot 3 Restore 0x5c28e0 -> CSoundEffect::Restore (or CAppNotActiveException)
DirectX::CSoundEffect (0xc0, vtable 0x65cf4c; ctor 0x5c2aa0, dtor 0x5c2b00)
  +0x04  WCValSkipListDict<int, CSoundEffectBuffer> (0x8c bytes; temp KeyVal
         at dict+0x74/+0x78 = effect+0x78/+0x7c): duplicates by id
  +0x90  master CSoundEffectBuffer
  +0xa0  CSoundFile* (vf1 open/rewind, vf2 close, vf3 format+size, vf4 read)
  +0xa4  wave format (filled by CSoundFile vf3)
  +0xb8  data size in bytes
  +0xbc  timeGetTime of the last duplicate purge
```

Interface offsets used (checked against dsound.h's method order):
IDirectSoundBuffer +0x2c Lock, +0x30 Play, +0x3c SetVolume, +0x40 SetPan,
+0x4c Unlock. The IDirectSoundBuffer* is at +4 of every CDirectSoundBuffer
(`[edi+4]` in Play/Restore/Load). RTTI puts the CDirectSoundErrorHandler base
"@+0x4" too: it is an empty base (no vtable, no data) that MSVC places at the
same offset, which is why CheckResult 0x5c2000 passes `this+4` to the error
function 0x5c1c40. So in `framework_layout.h` `CDirectSoundBuffer::unk04` is
the IDirectSoundBuffer*; +8 is still unknown (zeroed by the constructor;
0x5c2050 probably sets it, e.g. buffer flags).

How a sound is played: the game code (e.g. ambient sounds 0x4c7730, the
building/settler sound code 0x4b26c0) asks CSoundData (drawer+0x5f6bc,
0x5ff4f0(soundId, -1)) for a `CSoundEffect*` and calls
`CSoundEffect::Play(volume, pan, flags)` 0x5c2fc0:

1. master buffer idle (CDirectSoundBuffer::DSGetStatus == 0): DSSetPan,
   DSSetVolume, DSPlay(0, 0, flags) on it; result id -1;
2. otherwise look for an idle duplicate in the dictionary (iterating with
   `value()` 0x5c3640); if none, take a new id from the counter 0x7a0c00,
   insert a buffer under it (`operator[]` 0x5c3600 / base_insert) and fill it
   with `CSoundEffectBuffer::DuplicateInto` 0x5c2a60
   (IDirectSound::DuplicateSoundBuffer through CDirectSound at 0x7e8c0c);
   then SetPan/SetVolume/Play on that duplicate;
3. `PurgeStoppedDuplicates` 0x5c3210 (at most every 2.5 s) removes the
   duplicates that have stopped.

Volume is in DirectSound hundredths of a dB (the ambient code computes it
with a floor of -2500/-2300), pan -10000..10000. Loading:
`CSoundEffect::Load` 0x5c2ba0 reads the whole sample from a CSoundFile
(CMemorySoundFile for data held in memory) into a temp buffer, creates the
master buffer and copies the data in with Lock/Unlock. Lost buffers are
refilled by `CSoundEffect::Restore` 0x5c2d20 (reads the file again and
refills the master and every duplicate), triggered by DSERR_BUFFERLOST via
CheckResult -> CSoundEffectBuffer::Restore, and by CSoundData::Restore.

Watcom dictionary instance `WCValSkipListDict<int, CSoundEffectBuffer>`
(vtable 0x65cf54): slot 1 base_equivalent 0x5c33a0, 2 base_less_than
0x5c33c0, 3 base_new_node 0x5c3510, 4 base_destroy_node 0x5c35c0, 5 0x499270
(shared). The unsigned-int dictionary's slot 4 (`UI_sub_4a8f10`) is the
same base_destroy_node shape and could get that name too.

### CPU detection globals (new, for re/globals.csv)

- 0x7e8c10 CPUID signature (family/model/stepping, eax & 0x3fff)
- 0x7e8c14 / 0x7e8c18 / 0x7e8c1c: is Pentium / Pentium Pro / Pentium II
- 0x7e8c20 has MMX
- 0x7a12b0 pointer to the CPU name string ("Intel Pentium", "AMD K6", ...)
- 0x7a12ac cleared when cpuid faults (initial value not checked)
- 0x7e8c0c the CDirectSound object used for DuplicateSoundBuffer
- 0x7a0c00 next sound-effect duplicate id (initial value 1)

### Corrections of existing names (incl. non-classified rows)

- 0x5bf740 `DirectX::CSurface::DDFlip` (identified): `this` is a
  CFlipSurface (it goes through the vbptr), proposed `CFlipSurface::DDFlip`.
- 0x5bf9b0 `DirectX::CSurface::DDGetAttachedSurface`: the whole function is
  `CPrimaryFlipSurface::Create`; the string is only the error name of its
  GetAttachedSurface call.
- 0x5c2d20 / 0x5c2fc0 were named as WCValSkipListDictIter constructors and
  0x5c3210 as a WCBareVectorBase<int> constructor: they are
  CSoundEffect::Restore / Play / PurgeStoppedDuplicates (the survey picked up
  the vtable stores of local iterators/vectors).
- 0x5c3640 `CSoundEffectBuffer::CSoundEffectBuffer_5c3640` is the dictionary
  iterator's `value()`.
- `Sound_sub_5c3740..5c4140`: CPU detection and surface-copy benchmark,
  not sound.
- `Net_sub_*` rows 0x5bf050, 0x5bf1e0, 0x5bf2a0, 0x5bf2b0, 0x5bf310,
  0x5bf3d0, 0x5bf3e0 (surfaces) and 0x5c2000, 0x5c2a60 (sound) are not
  network code (proposed in the TSV). Other `Net_sub_*` rows in 0x5c1c10..
  0x5c2220 are DirectSound wrapper helpers too (not analysed here):
  0x5c1c40 is probably ThrowIfDSFailed, 0x5c21a0 is the shared slot 0
  (scalar deleting destructor) of CPrimaryBuffer/CSecondaryBuffer/
  CSoundEffectBuffer, 0x5c2050/0x5c2220 set up and create the secondary
  buffer, 0x5c2120 copies a buffer object.
- Verified rows: no wrong names found. Field comments to refine in
  `game/framework/framework_layout.h` (CSimpleSurface): +0x04 is the surface
  interface (IDirectDrawSurface3 when QueryInterface succeeds), +0x08 the
  draw/Blt interface (= +0x04, or the back buffer of a flip chain), +0x0c the
  CDirectDraw owner, +0x10 the interface version (1 or 3). CSurface: +0x14
  caps, +0x18 DDPIXELFORMAT (32 bytes), +0x38 RECT.

### Skipped rows in the two ranges

- DirectPlay / network (out of scope, 24 rows): CDirectPlayException
  0x5bfdb0/0x5bfff0/0x5c0010/0x5c0110, CDirectPlay 0x5c0700/0x5c0720/0x5c0740,
  `Render_sub_` 0x5c0430, 0x5c0770, 0x5c07d0, 0x5c0830, 0x5c0870, 0x5c0af0
  (these are DirectPlay, not render: 0x5c0830 CoCreateInstance of DirectPlay
  into +8, 0x5c0870 releases +8, 0x5c0af0 a QueryInterface-based create,
  0x5c0770 DPConnect + 0x5c0830), CPlayer 0x5c0b50..0x5c0d20, 0x5c0ee0..
  0x5c0f70, WCPtrSortedVector<CPlayer> 0x5c0f80/0x5c0fa0.
- Names already right (10): 0x5bf400 DDRestore, 0x5bf7d0, 0x5bfab0,
  0x5bfba0, 0x5bfc70, 0x5c33e0, 0x5c3450, 0x5c34b0, 0x5c34d0, 0x5c34f0.
- Lobby (out of scope, 8): 0x5c44f0, 0x5c4510 (CHelperThread thunks, no
  direct callers), 0x5c4520 (a CHelperThread loop with timeGetTime and
  "LOB: Sehr lange 500 Millisekunden %i"), 0x5c45e3 (not a function start:
  the continuation of 0x5c4520 after its catch block), 0x5c46b0, 0x5c46e0
  (a CLobbyServer-like pair-of-strings constructor), 0x5c4820, 0x5c4830.


## Verified wrappers (wip/ddraw-render, 2026-11-26)

All of Ddraw.cpp's CDirectDraw / surface / CPalette members are ours and
verified (src/s3re/game/framework/directx_draw.cpp; in-process tests
against a scripted DirectDraw, test/fake_ddraw.cpp; emulator specs
`DirectX_C*` in specs/framework.py). What reading them settled:

- Error names confirm the methods: RestoreIfLost's IsLost call reports
  "CSurface::IsLost"; 0x5bf1a0 is DDGetBltStatus (DD_OK,
  DDERR_WASSTILLDRAWING and DDERR_NOBLTHW pass, others go through
  CheckResult); the flip surface's Flip error is "CSurface::DDFlip".
- CDirectDraw: +8 is "owns the IDirectDraw2" (1 when constructed without
  another CDirectDraw; Disconnect releases only then). Disconnect calls
  slots 5 and 6 with 0 (release surfaces / palettes) first.
- DDCreateSurface returns the interface version: 3 when QueryInterface for
  IDirectDrawSurface3 succeeds, 1 on E_NOINTERFACE (the plain interface is
  kept with an AddRef); the created interface is always released. Its out
  locals start as its own stack slots (the `desc` argument slot, the pushed
  ECX), which only matters for a DirectDraw that returns success without
  writing them.
- Lock(rect, desc, flags): flags 0 become DDLOCK_WAIT, then masked to 0x33
  (versions 1 and 2) or 0x833 (version 3); other versions keep all bits.
- DDRestore: DDERR_WRONGMODE / DDERR_NOEXCLUSIVEMODE -> DebugLog "DDRAW:
  WrongMode oder NoExclusiveMode" and CAppNotActiveException; any other
  failure -> CDirectDrawException("CSurface::DDRestore").
- CFlipSurface (+4 back-buffer count, +8 an object the destructor deletes
  but nothing in Ddraw.cpp sets, +0xc the back-buffer CSimpleSurface);
  GetBackBuffer builds it once as CSimpleSurface(owner, +8, +8, version)
  of the CSurface base. CPrimaryFlipSurface::Create asks for the attached
  surface with DDSCAPS_BACKBUFFER for one back buffer and with no caps for
  two; more asserts "mehr als Triplebuffering nicht unterstuetzt".
- The primary surfaces' Creates fill only the DDSURFACEDESC fields they
  name (the original's stack local is otherwise uninitialized; ours zero:
  a known difference DirectDraw ignores). COffscreenSurface /
  COverlaySurface::Create zero the whole description.
- SetPixelFormat leaves EAX as its caller had it; DDGetSurfaceDesc returns
  SetRect's result; DDUpdateOverlay with a destination returns the
  destination interface's Release count.
- The intrusive list code these classes use (WCIsvSListBase::base_next,
  get, clearAndDestroy, the iterator's +=, WCPtrListBase::base_new_link
  0x463b50) is verified too (src/s3re/watcom/isv_slist.cpp).
