# Third-party notices

This project is licensed under the GNU GPL v3 (see `LICENSE`). It uses or
reimplements the following third-party software. The original game
(The Settlers III, Blue Byte, 1998) is not included; it is loaded from the
user's own installation at runtime.

## Microsoft Detours

Not included in this repository: `cmake/Detours.cmake` downloads it at
configure time and it is linked into the launcher and `s3re.dll`.

- <https://github.com/microsoft/Detours>
- Copyright (c) Microsoft Corporation.
- MIT License.

## libpng 1.0.2, zlib 1.1.3 and PaintLib

`S3.EXE` statically links these libraries. `src/s3re/thirdparty/`
reimplements the functions the game contains, written by hand to behave
exactly like the versions in the binary (including their bugs). The
reimplementations follow the structure and names of the original
libraries, so they are credited here.

- **libpng** 1.0.2: Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42,
  Inc.; Copyright (c) 1996, 1997 Andreas Dilger; Copyright (c) 1998
  Glenn Randers-Pehrson. Distributed under the libpng license.
  <http://www.libpng.org/pub/png/libpng.html>
- **zlib** 1.1.3: Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler.
  Distributed under the zlib license. <https://zlib.net/>
- **PaintLib**: Copyright (c) Ulrich von Zadow. Distributed under the
  PaintLib license. <http://www.paintlib.de/>

The libpng and zlib licenses permit use and modification provided that
the origin is not misrepresented and altered versions are plainly marked
as such: the code in `src/s3re/thirdparty/` is an altered,
independent reimplementation and is not the original software.
