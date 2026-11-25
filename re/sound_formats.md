# Sound data files (CSoundData, Dirk\SoundData.cpp)

Session 56 (wip/sound). The loader is reimplemented and verified
(`src/s3re/game/sound/sound_data.cpp`, layout `sound_layout.h`), so are the
DirectSound wrapper classes it uses (`src/s3re/game/framework/directx_sound.cpp`).
**Verified** below means: read from the code and checked in both shipped
files (`Settlers3Ultimate/SND`, read only) by a script and by the in-process
tests that run the original and our Open on them. **Code only** means read
from the disassembly, not yet exercised by a test. **Guess** is marked.

## Files (verified)

`SND\Siedler3_00.dat` (11,927,271 bytes) and `SND\siedler3_01.dat` (159,936
bytes). The names are built as "siedler3_" + "00" / "01" + ".dat" (the
"00"/"01" literals are the GFX group names, re/graphics_formats.md). Open
searches the folder of `siedler3_00.dat` in `.`, base + `Snd`, `.\Snd`,
base, base + `..` (FindDirectoryContainingFile 0x5fdfc0, the same search as
the graphics loader's) and memory-maps both files whole (read-only, or
copy-on-write when the files are to be detached from a CD, as the GFX
files). There is no decompression and no reading: the game plays samples
straight from the mapped view.

## Header (0x1c bytes; verified in both files)

| off | 00.dat | 01.dat | meaning |
|---|---|---|---|
| +0x00 | 0x00011544 | 0x00011544 | magic (not checked by Open) |
| +0x04 | 2 | 2 | unknown (probably a version) |
| +0x08 | 0 | 0 | unknown |
| +0x0c | 0x1c | 0x1c | header size (guess: not read by the code) |
| +0x10 | 0x00b5fee7 | 0x000270c0 | file size (equals the real size) |
| +0x14 | -1 | 1 | Open stores the first value that isn't -1 in CSoundData +0x148 (getter 0x5ff4e0, called from the lobby code; meaning unknown, guess: a sound set or language number) |
| +0x18 | 0x1c | 0x1c | file offset of the sound table; Open stores view + this in CSoundData +0x38 |

## Sound table (verified)

The same shape as the GFX tables: dword magic 0x00011f74, word size in bytes
(8 + 4 * count), word count, then `count` dword file offsets, one per
sound. 00.dat: 119 sounds (size 484, so the table ends at 0x200, where the
first sound starts); 01.dat: 1 sound.

## Sound (verified)

`dword n`, then `n` dword file offsets of its variants (samples). 00.dat:
82 sounds with 1 variant, 15 with 2, 9 with 3, 5 with 4, 3 with 5, 3 with 6,
1 with 7, 1 with 9 (208 samples); 01.dat: one sound with 2 variants.

## Sample (verified)

`dword size` (bytes from the size field to the end of the data, the field
included), a WAVEFORMATEX (0x12 bytes, cbSize 0), then `size - 0x16` bytes
of PCM data. Every sample of 00.dat is mono 16-bit 22,050 Hz PCM; the two of
01.dat are mono 16-bit 44,100 Hz. Largest: 385,684 bytes. The file is
packed without gaps: table, then per sound its header and its samples, in
table order, up to the file size.

## Use (code only; GetEffect 0x5ff4f0, not reimplemented yet)

A sound id is `file << 16 | index`. GetEffect(id, variant) returns the
DirectX::CSoundEffect of that sample, created on first use from the mapped
sample (new CMemorySoundFile(format = sample + 4, data = sample + 0x16,
bytes = size - 0x16, CSoundData +8), CSoundEffect::Load); variant -1 picks
`rand() % n` (the CRT rand). Effects are cached in the dictionary at
CSoundData +0x40 under the key `id << 24 | variant`, which keeps only the
low 8 bits of the index and drops the file number (so 0:0 and 1:0 would
share an entry; guess: harmless because the game's ids don't collide), and
the time of the last use goes into the dictionary at +0xc0, then 0x5ff700
(probably purges effects unused for a while) runs.

## The primary buffer (verified with a scripted DirectSound)

Open creates the DirectSound object (dsound.dll's DirectSoundCreate),
asks for the priority cooperative level (DSERR_ALLOCATED: falls back to the
normal level and leaves the card's format), creates the primary buffer and
sets its format from Open's last argument (`g_primaryFormats` 0x7a2fb8):
1 and 2 = PCM stereo 16-bit 22,050 Hz, 4 = PCM stereo 16-bit 11,025 Hz, any
other value asserts "Invalid soundformat"; DSERR_BADFORMAT only logs. The
primary buffer then plays looping (keeps the card running) and the sound is
enabled (+0xc). A missing or busy card (DSERR_NODRIVER, DSERR_ALLOCATED,
E_FAIL from DirectSoundCreate) only logs "DSOUND: Cannot create DSound
object" and leaves the sound off; the files are mapped anyway.

## CSoundData layout

See `src/s3re/game/sound/sound_layout.h` (0x14c bytes, vtable 0x65d3ec; it
derives from DirectX::CDirectSound, whose constructor registers it in
g_directSound 0x7e8c0c).

## Call tree

```
CDrawer::CDrawer 0x42c820 / Menus::CDrawThread::OnStart 0x4932e0 -> CSoundData::CSoundData 0x5fdb60
CDrawer::InitSurfaces / Menus CDrawer::CreateSurfaces:
  CSoundData::Open 0x5fe760 (window, base path, detach mode, sound format)
    DirectX::CDirectSound::Connect 0x5c1d20, IDirectSound::SetCooperativeLevel
    new CPrimaryBuffer 0x5c2180, Create 0x5c21c0, SetFormat, Play (looping)
    FindDirectoryContainingFile 0x5fdfc0, IsPathOnRemovableDrive 0x5fa750
    per file: CFile, CFile::Open, GetSize, CFileMapping::Create, MapView
  CSoundData::DetachFromMedium 0x5ff230
drawing code (settlers 0x4c85a0, 0x4d3050, ...): IsEnabled 0x5ff4d0, GetEffect 0x5ff4f0, CSoundEffect::Play 0x5c2fc0
options: SetVolume 0x5ff9f0, GetVolume 0x5ffa30
CDirectSound::Disconnect 0x5c1da0 -> CSoundData::ReleaseBuffers 0x5fde20 (vtable slot 2)
lost buffers: CDirectSoundBuffer::CheckResult -> RestoreAll -> CDirectSound slot 1 0x5c1dc0 -> CSoundData::Restore 0x5fdef0
destructor 0x5fdc90 -> Close 0x5ff1c0
```

## Quirks kept

* CDirectSound::Connect, CDirectSoundBuffer::DSCreateSoundBuffer and
  DSGetStatus pass a local that starts as a leftover (ECX's stack slot, the
  description argument's slot) to the COM call; if the call succeeded
  without writing it, that value would be used. Ours start with the same
  values.
* CDirectSoundBuffer::operator= doesn't release the old buffer.
* CSoundData::ReleaseBuffers releases the primary buffer without checking
  its interface (a second call crashes); Close deletes it afterwards.
* CheckDSResult treats every result other than DS_OK as an error, success
  codes included.

## Known differences

* The folder string's allocator byte (constructor) and
  FindDirectoryContainingFile's result allocator byte come from
  uninitialized stack bytes of the original (never read).
* Open's local strings and folder vector leak when a call throws (the
  original's frame frees them; only on the way to a fatal error).
* Open's and Restore's catch blocks are SEH filters: a code the original
  catches and rethrows (`throw;`) is not caught at all, the same for every
  caller.
