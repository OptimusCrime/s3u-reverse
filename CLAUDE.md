# Settlers 3 Reverse

## Goal

Reverse engineer and reimplement **The Settlers 3 – Ultimate Collection**
(Blue Byte, 1998; GOG release).

1. **Phase 1 (current):** reverse the original code and build a reimplementation
   that runs on Windows and behaves exactly like the original game.
2. **Phase 2 (later):** port the reimplementation to macOS.

Correctness is the top priority. Every reimplemented piece must be checked
against the original game's behavior, not just "looks right".

## Ground rules

- The original game install lives in `Settlers3Ultimate/` and is gitignored.
  Never commit original game files (executables, DLLs, GFX, sounds, maps, music).
- The reimplementation loads game data from the user's own installation at runtime.
- Do not commit raw decompiler output. Commit hand-written, cleaned-up
  reimplementations with meaningful names.
- Line endings are normalized to LF via `.gitattributes` (repo is used on
  both Windows and macOS).

## Tools

- **REA MCP server** (`mcp__rea__*` tools) plus the `reverse-engineer-anything`
  skill: static analysis (Ghidra 12.1.4 backend), decompilation, xrefs,
  annotations, and runtime tools (Frida, GDB, rizin).
- The `rea` CLI is not on PATH; the MCP server runs without it.

## Facts about the original binaries

- **Main executables:** `S3.EXE` and `S3_multi.EXE` (3,842,048 bytes each). They
  are the same build and differ in only 7 bytes, both GOG patches:
  - At file offset `0xFE72`, `S3_multi.EXE` has `xor dl, cl` inside a
    byte-decoding loop (keystream multiplier `0xFED42531`). `S3.EXE` replaces it
    with `nop nop`, so that decoding step is disabled.
  - At file offset `0x295A1D`, `S3_multi.EXE` reads the registry value
    `SerialNumber`. `S3.EXE` reads `SingleNumber` instead, a renamed value that
    doesn't exist, so it skips the serial number lookup.
  - Most likely interpretation: `S3_multi.EXE` keeps the original serial/CD-key
    handling that multiplayer needs, and `S3.EXE` has it neutralized for
    single player. Treat `S3.EXE` as the reference binary.
- **Compiler:** MSVC 6.0 linker, PE timestamp `0x38E4B1B0` (2000-03-31). 32-bit x86.
- **Fixed load address:** relocations are stripped and there's no ASLR, so it
  always loads at `0x400000` and Ghidra addresses equal runtime addresses.
- **Known addresses:** entry point (CRT startup) at `0x621bc6`; `WinMain` at
  `0x45a120`.
- **Language:** C++ (586 RTTI class names, e.g. `Win32::CApplication`,
  `DirectX::CGameApp`, `CScreenElement`). Ships with `MSVCRT.DLL` and `MSVCIRT.DLL`.
- **Sections:** `.text` ~2.4 MB of code, `.rdata` ~0.25 MB, `.data` ~1.4 MB.
  Image base `0x400000`.
- **Size:** Ghidra's default auto-analysis finds 20,897 functions and 3,476
  defined strings. The function count includes C runtime/library code and
  small compiler-generated helpers, so the game's own code is smaller.
- **Imports:** DirectDraw (`ddraw`), DirectInput, DirectSound, DirectPlay
  (multiplayer; `directplay.cmd` enables it), WinSock, WinINet, plus
  `ProtectCD.dll` (copy protection reference) and `_INMM.dll` (GOG CD audio
  emulation).
- **Other binaries:** `S3Editor/S3Editor.exe` (map editor), `SETUPS3.EXE`
  (config tool), `FileConvert.exe`, `BGLWINR.DLL`, `NEWTOOLR.DLL`, `PATCHW32.DLL`.
- **Game data:** `GFX/`, `SND/`, `MUSIC/`, `Map/`, `Save/`, `DATA.DAT`.

## Approach (decided)

**Incremental replacement inside the running original game**, the approach
used by OpenRCT2 and re3:

1. A 32-bit DLL of our own (`s3re.dll`) is loaded into the original `S3.EXE`
   by `s3re_launcher.exe` using Microsoft Detours
   (`DetourCreateProcessWithDllEx`). Nothing is added to the game folder.
   A proxy `ddraw.dll` won't work: `S3.EXE` loads DirectX dynamically, and its
   static imports are only system DLLs plus GOG's `_INMM.dll`.
2. Original functions are redirected (hooked) one at a time to our C++
   reimplementations. The game stays playable at every step.
3. Every replacement is verified:
   - **Function level:** differential tests call the original and our
     version with the same inputs and compare outputs and side effects.
   - **System level:** if the simulation is deterministic (likely, since
     lockstep multiplayer depends on it; still to be confirmed), record inputs,
     replay them in the original and in the hooked build, and compare
     game-state hashes every tick.
4. Work bottom-up: leaf functions and file formats first, then subsystems.
5. When nothing original remains, build a standalone executable, then replace
   DirectX with a cross-platform layer (e.g. SDL) for macOS.

Never mark a function as done without verification evidence.

Rejected alternatives: a from-scratch rewrite can't prove equivalence. Matching
decompilation (byte-identical recompile with MSVC 6) is too slow for this much
C++ code.

## Build environment

- **Language:** C++. Our classes mirror the original's where practical.
- **Toolchain:** Visual Studio 2026 Community (v18) at
  `C:\Program Files\Microsoft Visual Studio\18\Community`, with MSVC toolsets
  14.16 through 14.51 and the x86 compiler installed.
- CMake and Ninja come bundled with VS (not on PATH):
  - `...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - `...\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- The hook DLL must be built **32-bit (x86)** to load into `S3.EXE`.
- **Build:** `cmake --preset x86`, then `cmake --build --preset debug`.
  Output goes to `build/x86/bin/Debug/` (`s3re_launcher.exe`, `s3re.dll`).
  Run the launcher to start the game. It writes `s3re.log` next to the DLL.
- **Code layout:**
  - `src/launcher/`: checks the SHA-256 of `S3.EXE` and starts it with the DLL.
  - `src/s3re/`: the DLL. Hooks are registered with `S3RE_HOOK`
    (`hooks.h`); one file per reimplemented function or class, under
    `src/s3re/game/`.
- The user is new to Visual Studio. Drive builds from the command line, and
  explain any IDE steps the user has to take.
- The original game runs fine on the dev machine (Windows 10), so runtime
  comparison against the original is possible.
