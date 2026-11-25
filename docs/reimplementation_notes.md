# Reimplementation notes

Decided special cases for writing replacements. Moved out of `CLAUDE.md`
on 2026-11-24.

- **Allocator bytes of MSVC 6.0 strings** (decided 2026-11-09, batch
  27). Byte +0 of a `std::string` holds its empty allocator object; it is
  never read. The compiler fills it from wherever the allocator argument
  happens to live. (a) A stack slot it shares with an argument (the low
  byte of `mostDerived`, of a pointer argument, of a count `n`): that is
  reproducible, so replicate it exactly (0x40a580, 0x408cc0, 0x41c3e0,
  0x611a20). (b) An uninitialized local of the original's own frame: not
  reproducible. If that string is freed before the function returns, use 0.
  If it survives (e.g. a returned string, 0x612e80), the byte is a known,
  behaviour-irrelevant difference: say so at the function and in its
  inventory row, leave the byte out of the in-process comparison, and
  exclude exactly that byte with `Spec.ignore_bytes` plus a self-test
  pairing.
- **Padding of map effect records** (2026-11-25). The `MapEffect::Make_*`
  functions build a record in an uninitialized 0x4c-byte stack temporary
  and copy all of it, so the five bytes the constructor never writes
  (`pad2a`, `pad3b`, `pad3e`) are stack leftovers, in the original and in
  ours alike. They differ between the original and ours, and also between
  our Debug and Release builds (other frame sizes and inlining leave other
  bytes on the stack): a test can pass in Debug by luck and fail in
  Release. Any in-process test whose cases can add map effects (directly
  or through a callee such as `SpawnArrow`) compares the world with
  `WorldPair::CheckpointIgnoringEffectPadding` (or calls
  `ClearEffectPadding` first); emulator specs list the bytes in
  `ignore_bytes` (`EFFECT_PADDING`). Found by the UpdateSoldier test
  (tower arrows), which failed only in Release.
- **Detours and tiny functions:** a 4-byte body such as `mov eax,[ecx+0Ch];
  ret` can be hooked when nop/int 3 padding follows it (Detours uses the
  padding; 0x601270).

## Floating point: x87 code as `double` (decided 2026-11-24)

The original was compiled for the x87 FPU; our build uses SSE2. Their
results are identical when (a) x87 runs at 53-bit precision, round to
nearest, (b) the operations happen in the same order with the same operand
widths, and (c) float-to-int conversion truncates like the CRT's `_ftol`
0x61fe98. Windows starts threads with x87 at 53 bits / round to nearest
(control word 0x27F); the game never executes `fldcw` itself (every
`fldcw` is in the CRT: `_ftol` switches to truncation and back, and the
math library), and plain DirectDraw doesn't change it (only Direct3D
device creation does).

Rules for a replacement:
- Compute in `double` (`/fp:precise`, no FMA contraction), in the
  original's order of operations; where the original stores a `float`
  (`fstp dword`), round through a `float` variable at the same point.
- Convert to integers by truncation toward zero, as `_ftol` does (a
  `static_cast<int>` of the double, or a helper named after `_ftol`).
- CRT transcendental functions (sin, atan, pow, ...) would need the
  original CRT's results; none are known in the simulation so far: stop
  and ask if one shows up.
- The emulator spec includes inputs near rounding edges (values whose
  product or quotient lands on .5 or on an integer boundary).
- Say in the function's comment that it is x87 code done in `double`.

Still to do once: confirm the control word at runtime (log
`_controlfp(0, 0)` from a pass-through on `CLogicThread::Step` in the game
thread and in the logic thread); backlog item in
docs/todo/known-differences.md. If it is not 0x27F-equivalent, or a
whole-game check diverges in x87 code, that function gets inline x87
assembly.

Done this way so far: AI WantedForPopulation, Army::GroupFightingShareAbove.
