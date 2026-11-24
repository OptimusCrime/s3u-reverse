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
- **Detours and tiny functions:** a 4-byte body such as `mov eax,[ecx+0Ch];
  ret` can be hooked when nop/int 3 padding follows it (Detours uses the
  padding; 0x601270).
