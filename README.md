# Settlers 3 Reverse

A reverse-engineering project to reimplement *The Settlers 3 – Ultimate Collection*
(Blue Byte, 1998).

**Goals**

1. Reimplement the game so it runs on Windows exactly like the original.
2. Later, port it to macOS.

This repository contains no original game files. To run it, you need your own
copy of the game (for example the GOG release).

## How it works

`s3re_launcher.exe` starts the original `S3.EXE` and loads `s3re.dll` into
it. The DLL redirects original game functions to their reimplementations, one
function at a time, so the game stays playable throughout. Your game
installation is never modified.

## Building (Windows)

Requirements: Visual Studio 2026 with the "Desktop development with C++"
workload. CMake comes bundled with it.

Open a "Developer PowerShell for VS" (or any terminal with CMake on the PATH)
in the repository root and run:

```
cmake --preset x86
cmake --build --preset debug
```

The first configure downloads [Microsoft Detours](https://github.com/microsoft/Detours).

## Running

```
build\x86\bin\Debug\s3re_launcher.exe [--game-dir <path to the game folder>]
```

The game folder defaults to `Settlers3Ultimate/` in the repository root. You
can override it with `--game-dir` or the `S3RE_GAME_DIR` environment variable.
Only the GOG `S3.EXE` build is supported; the launcher checks its SHA-256
before starting. The DLL writes `s3re.log` next to itself.
