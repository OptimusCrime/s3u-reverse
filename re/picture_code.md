# The "Picture" range 0x5a0000..0x5b50ad: credits, PaintLib, libpng, zlib, ProtectCD

Findings of the naming pass of session 50 (2026-11-24). The survey called
this range "Picture" and the source-file scan put it under
`Credits\cpp_exo_text__diverses.h`; it actually holds the credits text
scroller (game code), and third-party code (category `thirdparty`):
PaintLib, libpng 1.0.2 and zlib 1.1.3, used only for the online lobby's
`Banner\Banner.png`, plus the loader of the copy protection embedded in
S3.EXE. None of it loads the game's own graphics (that is CGraphicsData,
`re/graphics_formats.md`).

## 0x5a0000..0x5a8f20: credits text scroller, PaintLib, libpng 1.0.2

The survey's "Picture" subsystem here is three unrelated things that sit in one
object unit (unit 72, `Credits\cpp_exo_text__diverses.h` range):

1. **0x5a0930..0x5a4220: the credits text scroller (game code, Credits).**
   The credits screen (`Menus::priv::CCreditsDialog`, Dirk/netdialogs.cpp) renders
   `DATA.DAT` from the game folder. Despite its name, DATA.DAT is a 27,897-byte
   **HTML file** ("Demotext", author Volker, written with Netscape 4.05), not a data
   archive. A small HTML-like interpreter lays it out with GDI fonts and a
   `cpp::exo::text::_t_Scroller` scrolls it up the DirectDraw surface through a GDI DC.
   Tags: `<BODY> </BODY> <BR> <P> <B> <I> <U> <SUB> <H1>-<H3>`, colours
   `<RED> <GREEN> <BLUE> <YELLOW> <ORANGE> <WHITE> <BLACK>`, fonts
   `<HANDSCHRIFT>` (Arial) / `<DRUCKSCHRIFT>` (Times New Roman), alignment
   `<MITTE> <LINKS-MITTE> <CENTER> </CENTER> <LEFT=n>`, speed `<MAX-n-FRAMES>`
   (sets the scroll timer interval: 10 -> 100 ms, 15 -> 66, 20 -> 50, 30 -> 33,
   40 -> 25, 60 -> 16, 80 -> 12, MAX-IMAL -> 0), entities `&lt; &auml; &ouml;
   &uuml; &Auml; &Ouml; &Uuml; &nbsp;`.
   Classes (names ours unless noted):
   - `Credits::CreditsView` (dialog member at dialog-0xe8): +0 CSurface*, +4 CreditsText
     (bool loaded-flag +0, TextLayout +4, `_t_Scroller*` +0x64), +0x6c height, +0x70 width.
     ctor 0x5a3fa0, Init 0x5a3fc0 (loads "data.dat"), Draw 0x5a4160 (per frame).
   - `Credits::TextLayout` (0x60 bytes, ctor 0x5a1e80): piece list head/tail/line start,
     x +0xc, y +0x10 (starts at 480), line height +0x18, current style +0x1c,
     default style +0x34, line ascent +0x4c, in-body +0x50, interval +0x54, left margin
     +0x58, centering +0x5c, no-merge +0x5d.
   - `Credits::TextStueck` (0x34 bytes; **real name** from the string "~TextStueck"):
     next +0, text +4, x +8, y +0xc, w +0x10, h +0x14, ascent +0x18, TextStyle +0x1c.
   - `Credits::TextStyle` (0x18 bytes, ctor 0x5a0960 already verified as
     `Credits::TextStyle_5a0960`): face +0, flags +4 (1 bold, 2 italic, 4 underline),
     height +8, COLORREF +0xc, HFONT holder +0x10, next +0x14.
   - `Credits::FontCache`: a singly linked list of TextStyle nodes at global
     0x7e8630 (init 0x5a0930, destroy 0x5a1a60 with DeleteObject).
   - RTTI classes: `cmy::impuls::Taktbar` ("clockable": one virtual, the tick) and
     `cpp::exo::text::_t_Scroller` (derived; offset double +8, step double +0x10 =
     -1.0, `cmy::impuls::Timer` +0x18 with interval +0x1c).
   The real namespace of the non-RTTI classes is probably `cpp::exo::text` too
   (source file cpp_exo_text.cpp, the RTTI class `cpp::exo::text::_t_Scroller`).
   I kept the existing `Credits::` prefix of session 50 for consistency; renaming the
   whole group to `cpp::exo::text::` later would be justified.
   Category: game code (Credits). Not simulation; deterministic only in layout.

2. **0x5a4230..0x5a4d50: PaintLib (third-party library, Ulrich von Zadow, ~1998,
   version 1.x).** RTTI classes `CBmp`, `CAnyBmp`, `CPicDecoder`, `CPNGDecoder`,
   `CTextException`, `CDataSource`, `CFileSource` and the PaintLib trace globals
   (0x7e8bb8 trace level, 0x7e8bbc trace file name; strings "--- Decoding file %s. ---",
   "Decoder error: %s", "Opening %s failed", "End of file reached while decoding.").
   Method names are PaintLib's (MakeBmpFromFile, MakeBmp, DoDecode, raiseError,
   CalcDestBPP, Create, initLocals, internalCreate, freeMembers, initLineArray,
   GetBytesPerLine, GetMemUsed). Only the PNG decoder is linked in.
   Suggested category: a library category (like stl/mfc/watcom), e.g. `paintlib`.

3. **0x5a4de0..0x5a8f20 (and on into the other agent's range): libpng 1.0.2**
   (version strings "1.0.2" at 0x733584/0x7335d4), with **zlib 1.1.3** after it
   ("1.1.3" passed to inflateInit_; crc32 = 0x5aa130, inflateEnd 0x5aa2c0, inflateInit_
   0x5aa480). Function names are the real libpng names, identified by strings,
   transformation flag bits (PNG_BGR 1 ... PNG_FILLER 0x8000) and the call order of
   png_do_read_transformations. Unused libpng functions were dropped by the linker;
   png_do_chop is inlined into png_do_read_transformations.
   Suggested category: library (`libpng` / `zlib`), not game.

### Who uses it (entry points from the game)

- Credits: `Menus::priv::CCreditsDialog::CCreditsDialog` 0x4923d0 -> CreditsView ctor
  0x5a3fa0 + Init 0x5a3fc0; draw 0x492790 -> CreditsView::Draw 0x5a4160; dialog
  destructor 0x492620 -> 0x5a4220, ~TextStueck 0x5a19a0.
- PaintLib/libpng: **only** `Lobby_sub_4489d0` (Dirk/LobbyDialogs.cpp, called from
  `CLobbyDialog::CLobbyDialog` 0x43e800 via 0x448960): `CAnyBmp` + `CPNGDecoder`
  `MakeBmpFromFile("...Banner\Banner.png")`, then copies the bitmap into a DirectDraw
  surface (DDLock/Unlock). That is the online-lobby advertising banner; no PNG file
  exists in the GOG install (none under Settlers3Ultimate/), so in practice this code
  never decodes anything offline. Nothing in GFX/ is read by this range; the GFX
  loaders are CGraphicsData (0x5f0000..).

### Function starts missing from the inventory (not in ghidra/extra lists)

- 0x5a0920: static-initializer stub for the font cache (call 0x5a0930; jmp
  StaticDtorReg_7e8630 0x5a0940); 0x5a0950: its destructor stub (ecx = 0x7e8630,
  jmp 0x5a1a60).
- 0x5a1a60: `Credits::FontCache::~FontCache` (for each node: DeleteObject(*font) via
  import 0x65505c, delete the holder, delete the node).
- 0x5a42c0: `CTextException::CTextException(const CTextException&)` (copy ctor, uses
  slots 1/2 GetCode / operator const char*); 0x618250 and 0x451100 (CTextException
  vf01/vf02) are folded getters `mov eax,[ecx+4]` / `[ecx+8]`, i.e. GetCode and
  `operator const char*`.
- 0x5a4610: PaintLib's PNG read callback (png_get_io_ptr -> CDataSource, ReadNBytes
  inlined: "End of file reached while decoding." -> CTextException(11)), set by
  png_set_read_fn in DoDecode.
- 0x5a4970: PaintLib's PNG error callback (`raiseError(2, msg)`); the warning callback
  is the folded NoOp 0x59f200.

### Corrections of existing names / docs

- `CBmp::CBmp_5a4d10` -> `CBmp::~CBmp` (in the TSV).
- `Credits::TextStyle_5a0960` (verified) -> should become `Credits::TextStyle::TextStyle`
  (default face "Times New Roman" 0x69fff0, height 12, colour 0xff = red). Not in the
  TSV (not a target row).
- `png_free` 0x5aa0a0 (verified; was `Picture::FreeIfBoth`) is libpng's
  `png_free(png_ptr, ptr)`. Also there: 0x5a9e20 png_error, 0x5a9fe0 png_set_error_fn,
  0x5aa000 png_create_struct, 0x5aa050 png_destroy_struct, 0x5aa060 png_malloc,
  0x5a9990 png_get_IHDR, 0x5a9a80 png_get_PLTE, 0x5a9af0 png_set_read_fn,
  0x5a9ac0 png_read_data, 0x5b01c0 png_read_start_row, 0x5a93d0
  png_build_gamma_table, 0x5a92b0 png_do_dither, 0x5a8f20 png_do_expand,
  0x5a9b90.. CFileSource (for the other agent).
- LOGBOOK / survey claim "zlib/libpng at 0x5b2fb3..0x5b50ad": libpng actually starts
  at 0x5a4de0 and zlib by 0x5aa130; whatever is at 0x5b2fb3.. should be rechecked.
- The four already-correct rows CAnyBmp::scalar_deleting_destructor 0x5a43c0,
  CAnyBmp::~CAnyBmp 0x5a43e0, CPNGDecoder::scalar_deleting_destructor 0x5a45e0,
  CPNGDecoder::~CPNGDecoder 0x5a4600 keep their names (checked; can go to identified).
- CRT_sub_61f27e is sprintf (format strings "Decoder error: %s" into a buffer);
  0x621543/0x6214d6/0x621459 = fopen/fputs(or fprintf)/fclose in the trace code.

## 0x5a8f20..0x5b50ad: libpng 1.0.2, zlib 1.1.3, CDataSource/CFileSource, ProtectCD stub

Proposals: `gfx2.tsv` (116 rows: 112 high, 4 medium). Not renamed, because the
current names are already right: CFileSource::CFileSource (0x5a9b90),
CFileSource::scalar_deleting_destructor (0x5a9bb0), CFileSource::~CFileSource
(0x5a9bd0), CDataSource::CDataSource (0x5b03f0).

### What the code is

The range is not game picture code. It has three unrelated parts:

1. **libpng 1.0.2** (0x5a8f20..0x5b01c0 here; it starts earlier, in the other
   agent's range) and **zlib 1.1.3** (inflate only: 0x5aa130 crc32,
   0x5aa270..0x5aa4a0 inflate.c, 0x5b0520..0x5b2be0 infblock/infcodes/
   inftrees/infutil/inffast, 0x5b13f0 adler32, 0x5b1520/0x5b1540
   zcalloc/zcfree). Versions from .rdata: `" inflate 1.1.3 Copyright 1995-1998
   Mark Adler "` (0x65c660), the zlib version check string "1.1.3" (used by
   inflateInit2_ 0x5aa310), and the libpng version "1.0.2" (twice near
   0x733584, next to "Incompatible libpng version in application and
   library"). It's a read-only build: no deflate and no png write code
   (removed by the linker). The libpng chunk handlers carry libpng 1.0.2's
   own message strings, including the copy-paste "Missing IHDR before sBIT"
   in png_handle_cHRM.
   - pngerror.c 0x5a9e20..0x5a9fe0, pngmem.c 0x5aa000..0x5aa0f0, pngrio.c
     0x5a9ac0..0x5a9b50, pngget.c 0x5a9990/0x5a9a80, pngrutil.c
     0x5aa8d0..0x5b01c0, pngset.c 0x5b1550..0x5b1ae0 (alphabetical, as in the
     source), pngrtran.c tail 0x5a8f20..0x5a93d0 (png_do_expand,
     png_do_dither, png_build_gamma_table).
   - Link order mixes them: crc32 + inflate.c sit between pngmem.c and
     pngrutil.c, and the game's CDataSource sits between pngrutil.c and
     infblock.c. So the source-file range "Credits\cpp_exo_text__diverses.h
     0x5a0d20..0x5b50ad" in re/source_files.csv doesn't apply to these
     rows. Their source_file should be empty, like the CRT's.
   - **Category:** these rows should get a third-party library category.
     inventory.py has none: add `libpng` and `zlib` (or one
     `thirdparty`) and let frontier.py list them with the
     stl/mfc/watcom library functions. zlib rows are named `zlib::name`
     (crc32, adler32, inflate*, huft_build, zcalloc, zcfree); libpng rows keep
     their real C names (`png_*`).
   - Calling convention: all of it is plain cdecl C (no `this`).

2. **CDataSource / CFileSource** (game classes from RTTI, Credits-area code
   at 0x5a9b90..0x5a9de0 and 0x5b03f0..0x5b0500). This is the input source of
   the picture decoder (CPicDecoder::vf01 0x5a4a00 builds a CFileSource).
   - CDataSource (vtable 0x65c608, 3 slots): +4 data base, +8 read cursor,
     +0xc name copy (operator new), +0x10 size. Slot 0 scalar deleting
     destructor, slot 1 Attach(name, data, size), slot 2 Release.
   - CFileSource (vtable 0x65c1a4, 4 slots; +0x18 file handle, +0x1c mapping
     handle): slot 2 Close, slot 3 Open(path). Open memory-maps the whole
     file read-only. Return codes: 0 ok, 3 path not found, 4 other
     CreateFile error, 5 access/sharing violation or mapping failure.
   - Probably the decoder's read callback reads from the cursor at +8
     (png_default_read_data 0x5a9b50 has no direct callers; check
     CPNGDecoder::vf03 0x5a4680, which calls png_set_read_fn).

3. **0x5b2f70 ProtectCD_RunEmbeddedCheck**: the CD copy-protection
   loader, not zlib. The survey took its non-aligned "function starts" for
   zlib. The function is filled with anti-disassembly junk: `jmp $+N` over
   garbage bytes, `push eax; push ebx; call next; pop ax; pop bx; pop eax;
   pop ebx; xchg ebx,eax`, `cmp edi,edi; xchg esi,edi; jne`, and
   timeGetTime/GetModuleHandleA("KERNEL32.DLL", "USER32.DLL", "SHELL32.DLL",
   "GDI32.DLL") loops. Its real body (followed with capstone, recursive
   descent, in the scratchpad `rd.py`):
   - GetVersionExA. The global 0x7e8bc0 = (dwPlatformId == 2), i.e. Windows NT.
   - GetTempPathA, then GetTempFileNameA(prefix "swp"), then CreateFileA, then
     WriteFile of **an embedded PE image at 0x735b58, size [0x79db58] =
     0x68000**. That image is **ProtectCD.dll** (export directory name
     "ProtectCD.dll", PE timestamp 0x37c40a14 = 1999-08-25, MFC 4.2 static,
     ~70 exports with scrambled names like `_Z8799@8`, `_F73AS@0`).
   - LoadLibraryA on the temp file, then call `hModule + [0x79db68]` (0x49c0 =
     the RVA of export `_Z8799@8`) with (arg1, arg2 ^ 0xa42198fa).
   - FreeLibrary, then DeleteFileA (in a retry loop).
   - Returns the cookie at 0x735b1c if the DLL returned 0xffcc8c89, else 0.
     The cookie is (wDayOfWeek+1)*253 from GetSystemTime, made the first
     time (initially -1).
   - Signature `int __stdcall ProtectCD_RunEmbeddedCheck(const char*, int)`
     (ret 8). The only caller is Menus::CDrawThread::PromptForCD at 0x497548: arg1 = [0x7a980c]
     or "" (0x6553a0), arg2 = 0x95. It logs "CHECK.2: %i %i" and stores
     (result != 0).
   - The 27 rows 0x5b2fb3..0x5b50ad (26 `Zlib_sub_*` plus
     `Picture_sub_5b3070`) are the `call next` targets (each starts with
     `pop ax`). They are false function starts. I named them
     `ProtectCD_RunEmbeddedCheck_junk_<addr>` only because the format needs
     a name. Better: delete these rows, and keep them out of the function
     list (an exclusion in import_ghidra_functions.py, or kind `false` in
     ghidra_functions.tsv) so the next survey or Ghidra import doesn't bring
     them back.
   - The GOG build still contains and runs this stub. _INMM.dll / the GOG
     patches probably make the check pass; not checked. Relevant for
     the static `ProtectCD.dll` import mentioned in CLAUDE.md: the DLL is
     also embedded and dropped as a temp file.

### Image formats handled

PNG only: everything libpng 1.0.2 reads (IHDR, PLTE, IDAT, IEND, gAMA,
sBIT, cHRM, sRGB, tRNS, bKGD, hIST, pHYs, oFFs, pCAL, tIME, tEXt, zTXt,
interlacing, all 5 filters). The transforms present in this half are
png_do_expand, png_do_dither (RGB to a palette through a 5:5:5 lookup, so
the decoder produces 8-bit paletted output) and gamma tables. No other
format code is in 0x5a8f20..0x5b50ad.

### Entry points from the game and files read

- The only users of the decoder: `Lobby_sub_4489d0` (Dirk/LobbyDialogs.cpp)
  constructs a CPNGDecoder (0x5a45c0) on the stack and calls
  CPicDecoder::vf01 (0x5a4a00: open file and decode) with the path
  `<string at 0x7a7c80> + "Banner\Banner.png"` (0x69a4d0) and 0x20.
  `Menu_sub_465520` (= Menus::priv::CFTPThread vf05, MenuPrivTools.cpp)
  uses "Banner.png" (0x69c0c0), probably to download it by FTP.
- So the PNG/zlib code decodes the **online lobby's advertising banner**,
  a file downloaded at runtime. It is not in the GOG install (no `*.png`
  under Settlers3Ultimate/) and the credits don't use it. It's network/lobby
  only, so it doesn't matter for the simulation work.

### Corrections of existing names

- 0x5aa0a0 `Picture::FreeIfBoth` (verified) is libpng's `png_free(png_ptr,
  ptr)`. Done (session 50): renamed to `png_free`, category `thirdparty`.
- All `Zlib_sub_*` rows: not zlib (see above). The actual zlib code is in
  rows that were named `Picture_sub_*`.
- `CDataSource::ctor_or_dtor_5b0460` is the destructor `~CDataSource`, and
  `CDataSource::vf00` is its scalar deleting destructor.
- For the other agent's range, as context (from the call graph, not
  re-checked): 0x5a4ec0 png_reset_crc, 0x5a4ee0 png_calculate_crc, 0x5a5030
  png_create_read_struct, 0x5a4f30 png_create_info_struct (calls
  png_create_struct), 0x5a5180 png_read_info, 0x5a58f0 png_read_end (both
  dispatch to every png_handle_*), 0x5a5440 png_read_row (calls
  png_read_start_row, png_read_filter_row, png_combine_row,
  png_do_read_interlace, png_memcpy_check, png_read_finish_row), 0x5a5b90
  png_read_destroy (only caller of inflateEnd), 0x5a6220
  png_init_read_transformations, 0x5a6c30 png_do_read_transformations.

