# Graphics data files (CGraphicsData, Dirk\graphicsdata.cpp)

Findings of the naming pass of session 50 (2026-11-24), read from the code
and checked against the game's files in `GFX\` (headers of all 45 files)
and `SND\`. Not yet reimplemented: treat details as "read from the code"
until a verified loader confirms them. Function names are in
`re/functions.csv` (notes "Named (session 50, ...)").


Scope: Dirk\graphicsdata.cpp (0x5f48e0..0x5fdb25) plus the Win32 mapping/detach helpers it
calls and a light pass over CSoundData (Dirk\SoundData.cpp). Names are in `gfx3.tsv`
(114 rows: 84 high, 30 medium). Layouts were checked against the RGB565 files in
`Settlers3Ultimate/GFX` (read only).

## Files

`GFX\siedler3_NN.<pixel format>.dat`. NN is a group number 00..61 (70 slots, groups that don't exist
stay empty). The pixel format is the R, G, B masks of the DDPIXELFORMAT in lowercase hex, written one
after the other (`_itoa(mask, 16)`): `f8007e01f` = RGB565, `7c003e01f` = RGB555. Both sets ship.
Open picks the set that matches the surface (`CGraphicsData::Open` 0x5f6fd0, strings "siedler3_",
".dat", "siedler3_??.", "00".."61" at 0x7a2d98..0x7a2e6c). Folder search: `.\Gfx`, `Gfx`, `..` and a
base path argument (string 0x7a7c80), via `FindDirectoryContainingFile` 0x5f6a10 (SetCurrentDirectory +
FindFirstFileA, throws Win32::CFileNotFound).

Each file is memory-mapped whole (CFile::Open GENERIC_READ, CFileMapping::Create PAGE_READONLY, or
PAGE_WRITECOPY plus FILE_MAP_COPY when the files will be "detached" from a CD:
`DetachFromMedium` 0x5fa7c0 -> Win32 `DetachMemory` 0x61ab60, which turns every copy-on-write page into
a private copy). There's no decompression step: the game draws straight from the mapped view.

Note: `Siedler3_18.f8007e01f.dat` is not a GFX file at all, it starts with "MZ" (a PE executable).
Open would load it as group 18. Its first dword isn't 0x1404, so it passes the only header check.

### File header (0x54 bytes, confirmed in all 45 GFX files)

| off | value | meaning |
|---|---|---|
| +0x00 | 0x00041304 | magic/version; Open rejects 0x1404 ("Falsche Gfx-Datei %i") |
| +0x04 | 0x0c, 0, 0x54 | (header sizes?) |
| +0x10 | 32 bytes | DDPIXELFORMAT (dwSize 0x20, DDPF_RGB 0x40, 16 bpp, R/G/B masks, alpha 0) |
| +0x30 | dword | file size (matches every file) |
| +0x34..+0x50 | 8 dwords | file offsets of the 8 tables below |

Open sets, per group g, `table[g] = view + header[k]` (view at CGraphicsData+0x264):

| header | magic | CGraphicsData | contents | accessor |
|---|---|---|---|---|
| +0x34 | 0x1904 | +0x37c | **texts**: dword size, word textsPerLanguage, word languages (8), then offsets `[lang*n + i]` to C strings; groups 07/08/09/99 | TextAddress 0x5faaa0 (was VariantFrameAddress) |
| +0x38 | 0x2412 | +0x6c4 | **landscape textures** (group 00, 235 of 128x128) | LandscapeTexturePointer 0x5face0 |
| +0x3c | 0x11306 | +0x5ac | **GUI images** (groups 02, 03, 14/24/34/44 per race, 48, 60, 61) | GuiImagePointer 0x5fac70 (was FramePointer), GuiImagePointerAt 0x5faca0 |
| +0x40 | 0x0106 | +0x494 | **"Animation"** = image lists of normal 16-bit sprites | AnimationPointer 0x5faaf0 ("GFX: Animation not found") |
| +0x44 | 0x3112 | +0x7dc | **ColorIndexedAnimation** = image lists of 8-bit palette-index sprites (player-colour layer) | ColorIndexedAnimationPointer 0x5fab50 |
| +0x48 | 0x5982 | +0x8f4 | **ShadowAnimation** = image lists of shadow masks | ShadowAnimationPointer 0x5fabb0 |
| +0x4c | 0x21702 | +0xb24 | **sequences** (groups 15/25/35/45): composed frames | SequenceFrameCount 0x5fad20, GetSequenceFrame 0x5fad50 |
| +0x50 | 0x2607 | +0xa0c | **palettes** (group 01: 20 x 0x1000 bytes = 8 brightness levels x 256 x 16-bit) | PalettePointer 0x5fac10 (`entry + (b>>5)*0x200`) |

Generic table header: dword magic, word size in bytes, word count, then `count` dwords of offsets
(relative to the file start). The text table has a dword size instead. The palette table has an extra dword
(0x100) before its offsets, and its reader starts at +0xc. Ids are `group << 16 | index`.

Group usage seen in the files: 00 landscape, 01 landscape objects (lists + shadows + palettes),
02/03 GUI, 07/08/09/99 texts, 10/20/30/40 buildings per race (60 lists each, all three layers), 11/21/31/41
settlers per race, 12..13/22..23/... more objects, 14/24/34/44 GUI per race, 15/25/35/45 sequences per
race. `+0xc3c` = number of languages (first non-zero text table), `+0xc40` = current language
(SetLanguage 0x5faa60). `+0x34[70]` (unk034) = file sizes (CFile::GetSize, stored at `views - 0x230`),
used by detach. `+0x04` = DDPIXELFORMAT (GetPixelFormat 0x5fa730).

### Image list (entries of the 0x106/0x3112/0x5982 tables)

`02 14 00 00 08 00 00 NN`: dword 0x1402, word 8, byte 0, byte count NN at +7, then NN dword offsets
relative to the list. `GfxDword::ListImage` 0x5fc5a0 (no check) and `ListImageChecked` 0x5fcce0
(falls back to image 0) select a frame.

### Image formats (GfxDword = one pointer to an image; 0x5fc540 is its constructor)

* **Sprite (16-bit, colour-indexed, shadow)**: 12-byte header `dword 0x0c, word w, word h, short x,
  short y`, then a count byte n at +0xc and n extra bytes, then the RLE rows at `align2(+0xd+n)`.
  Accessors: PointAt4 (size), +8 offset, SpriteExtraCount/Bytes, SpriteRleData(16).
* **GUI image**: 8-byte header `w, h, x, y` (words), count byte at +8, extra bytes, rows at
  `align2(+9+n)`. PointAt0 = size, PointAt4 = offset (DrawGuiImage 0x5f64c0).
* **Landscape texture**: `w, h`, count byte at +4, extra bytes from +5 (read one by one by
  `TextureExtraByte` in landscape.cpp), rows at `align2(+5+n)`.
* **RLE rows** (checked in the files): a row is a sequence of runs `u8 count, u8 skip | 0x80 on the
  row's last run`, followed by `count` pixels: 2 bytes each (RGB565/555) for sprites/GUI/textures,
  1 byte each (palette index) for colour-indexed sprites, none for shadows (the run only marks covered
  pixels). The skip is the number of transparent pixels before the run (row 0 of siedler3_10 list 0
  image 0: `04 87` = 4 pixels after 7 transparent ones, last run; width 15). Skipping rows:
  Rle16SkipRows 0x5fb130, ColorIndexedSkipRows 0x5fc810, ShadowSkipRows 0x5fd040.
  The "skip before the run" reading fits the widths but isn't proven by the blitter code (not read
  run by run).

### Sequences (0x21702 entries)

`dword frameCount`, then 24-byte frame records:
`Point16 offset; dword bodyId; dword colorIndexedId; dword shadowId; word bodyAndColorFrame;
word shadowFrame; dword sound (low 24 bits sound id, high byte a threshold, -1 none)`.
Example siedler3_15 sequence 0: 12 frames, body (11:0xce), torso (11:0xc1), shadow (11:0xce), frame
11, sound -1. `SequenceFrame::SequenceFrame` 0x5fd800 resolves a record into a 0x14-byte object
(offset, body image, colour-indexed image, shadow image, sound), with getters 0x5fd980..0x5fd9f0.

## Drawing

All draw functions are GfxDword methods taking a 0x6c-byte surface descriptor (a DDSURFACEDESC,
pitch +0x10) by pointer, copying it by value to the blitter, plus a position, a clip rect and more:

* `DrawSprite` 0x5fadb0 (pos, clip, brightness 0..255, visible height 16.16): brightness picks two
  span tables (`SelectBrightnessSpans` 0x5fb4c0): `0x7a2bd8[b>>3]`, 32 shade spans 0x5f48e0..0x5f59e0
  (level i copies pixels scaled to about i/32 with shift-and-mask sums of the pixel-format half masks;
  0 writes black, 31 copies), and `0x7a2c58[b>>5]`, 8 masked spans 0x5fb470..0x5fb180 (copy where a
  16-entry dither mask is set). The masked spans draw up to 8 "dissolve" rows at the top edge of a
  partly visible sprite (construction progress): `Rle16VerticalClip` 0x5fb050. Blitters: ClipLeft
  0x5fb4f0, ClipBoth 0x5fb850, ClipRight 0x5fbdf0, Shaded 0x5fc160, FullBright 0x5fc350 (b >= 248).
* `DrawColorIndexed` 0x5fc5f0: 8-bit RLE, palette word per pixel (palette from PalettePointer, by
  brightness level); blitters 0x5fcb30/0x5fc9a0/0x5fc860.
* `DrawShadow` 0x5fcdc0: shadow runs drawn by the span function in 0x7e8e20, chosen by
  `SetShadowSpan` 0x5fcd30 (via `CGraphicsData::SetShadowMethod` 0x5faa70): method 0 = halve the
  destination (0x5fd0c0, masks 0x7bef/0x3def), method 1 = black checkerboard (0x5fd080).
* `DrawGuiImage` 0x5f64c0: GUI images with the 16-bit blitters.
* Pixel-format globals (set at the end of Open): 0x7e8dd8.. = 0x7bef, 0x39e7, 0x18e3, 0x861, 0x20
  and their doubled 32-bit forms (565), or 0x3def, 0x1ce7, 0xc63, 0x421, 0 (555): the masks of
  `x>>1`, `x>>2`, `x>>3`, `x>>4`, `x>>5` per channel.

Entry points the draw code uses: AnimationPointer / ColorIndexedAnimationPointer /
ShadowAnimationPointer + ListImage(Checked) + DrawSprite / DrawColorIndexed / DrawShadow (buildings
0x4a9960, 0x4b26c0, settlers 0x4c85a0, 0x4d3050, 0x4d8400); GetSequenceFrame (settlers and
animated buildings); GuiImagePointer(At) + DrawGuiImage (menus, CStaticImage, panel 0x4b4d00);
LandscapeTexturePointer + Texture* (landscape.cpp 0x54bec0); TextAddress (UI texts, 294 callers);
PalettePointer (CStaticColorIndexedFrame, drawers).

## Load call tree

```
CDrawer::CDrawer 0x42c820 / Menus::CDrawThread::vf04 -> CGraphicsData::CGraphicsData 0x5f67c0, CSoundData::CSoundData 0x5fdb60
CDrawer::vf12 0x42e430 / Menus CDrawer::vf11 0x497ef0:
  CGraphicsData::Open 0x5f6fd0 (pixel format, base path 0x7a7c80, detach mode from g_config, group-set mode [0x7a7ff8])
    FindDirectoryContainingFile 0x5f6a10 ("siedler3_??.<fmt>.dat")
    IsPathOnRemovableDrive 0x5fa750 (detach mode 3), DebugLog "GFX: Preparing DAT files for detach"
    per group: CFile::CFile 0x6195c0, CFile::Open 0x619610, CFile::GetSize 0x61a080,
               CFileMapping::Create 0x61a520, CFileMapping::MapView 0x61a7d0, header check, 8 tables
    pixel-format masks, SetShadowSpan 0x5fcd30
  CGraphicsData::GetPixelFormat, LanguageCount, SetLanguage, SetShadowMethod, DetachFromMedium
  CSoundData::Open 0x5fe760 (+ chunks), CSoundData::DetachFromMedium 0x5ff230
CDrawer::vf05 / destructor -> CGraphicsData::Close 0x5fa690 (UnmapView 0x61aa40)
```

Group-set mode (4th argument of Open, values 0..4): before loading, it empties some group names:
probably group 60 for modes 0/1/3, groups 40..49 unless mode is 1, 3 or 4, and group 61 when a
g_config check (0x413be0) fails. The indexes come from the stack offsets and are not certain. It's
probably the installed content (e.g. the 4th race of the add-on). Detach mode (3rd argument):
2 = always, 3 = when on a removable drive.

## Corrections of existing names

Done in session 50: the three CGraphicsData renames below and their gfx_layout.h fields
(TextTable, textTables, language, languageCount, guiImageTables, SequenceTable, sequenceTables).
The unknowns in the fourth item are not renamed yet.

* 0x5faaa0 `CGraphicsData::VariantFrameAddress` (verified) -> `TextAddress`: the "variants" are the
  8 languages and the "frames" are text strings (checked in siedler3_08: same text id in German,
  English, Italian, French, Polish, Spanish, Korean, Japanese). Rename `FrameTable` in gfx_layout.h
  (framesPerVariant -> textsPerLanguage, unk0a -> languages), `frameTables` -> `textTables`,
  `currentVariant` -> `language`, `unkc3c` -> `languageCount`.
* 0x5fac70 `CGraphicsData::FramePointer` (verified) -> `GuiImagePointer`; `frameTables5ac` ->
  `guiImageTables`.
* 0x5fad20 `FrameCount` -> `SequenceFrameCount` (medium; optional); `spriteTables` -> `sequenceTables`
  (its entries are sequences, not sprites).
* gfx_layout.h unknowns: `unk034` = file sizes, `unk004` = DDPIXELFORMAT, `unk6c4` = landscape
  texture tables, `unk7dc` = colour-indexed list tables, `unk8f4` = shadow list tables, `unka0c` =
  palette tables.
* `GfxDword` (verified constructor 0x5fc540) is a reference to one image or image list. A better class
  name would be something like `GfxImage`; the tsv keeps `GfxDword::` so it matches the current name.
* 0x5fe948 `DirectX::CDirectSound::DSSetCooperativeLevel` (identified) and 0x5fe9d8
  `DirectX::CPrimaryBuffer::DSSetFormat` aren't functions: they're catch-continuation addresses inside
  CSoundData::Open 0x5fe760 (`mov eax, <addr>; ret` funclet returns right before them); same for
  0x5feb05.
* Missing function starts (not in the inventory; Ghidra merged or missed them): shade spans 0x5f48e0,
  0x5f4920, 0x5f4a00, 0x5f4c00, 0x5f5070, 0x5f59e0 (table 0x7a2bd8 entries 0, 1, 3, 7, 15, 31);
  masked spans 0x5fb470, 0x5fb410, 0x5fb3a0, 0x5fb340, 0x5fb2d0, 0x5fb260, 0x5fb180 (table 0x7a2c58
  entries 0..5 and 7); shadow span 0x5fd0c0 (method 0). Suggested names: `ShadeSpan16_LevelNN`,
  `MaskedSpan16_LevelN`, `ShadowSpan_Halve`.
* Lobby_sub_5f4830 may run into 0x5f48e0 (its end wasn't checked).

## Not done

SND file format (`SND\Siedler3_00.dat` header `44 15 01 00 02 00 ...`) not analysed. CSoundData vf02,
0x5ff4e0, 0x5ff9f0, 0x5fff00 (Lobby callers) skipped. The blitters' inner loops weren't read in
detail (their names come from the branch structure of the callers).
