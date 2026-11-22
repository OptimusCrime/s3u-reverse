# Coding style for the reimplementation

Decided 2026-11-22 (session 45). Applies to all new code and, step by
step, to existing code (the readability pass). `CLAUDE.md` points here.

Our code has two jobs: behave **exactly** like the original (proven by the
differential tests, the emulator specs and the whole-game replays) and be
**readable C++** that a person can follow without the disassembly. The
first job always wins; within it, write the code a human would write.

## 1. Behaviour is fixed, the shape is free

The tests compare results, memory and side effects, not instructions. So
control flow, expressions, helper functions and local variables can be
rewritten freely. What must stay exactly as the original has it:

- **Order of side effects**, above all calls of `CGameData::Random`
  (one call more or less changes the whole game), calls of other game
  functions, and writes that a later read in the same function sees.
- **Integer widths, signedness and wrap-around.** If the original keeps a
  counter in a byte, ours does too (`std::uint8_t`, wrapping); a signed
  compare stays signed. Use the field's own type from `layout.h`; cast
  explicitly where the original truncates.
- **Reproduced quirks**: reads past a table, uninitialized values that we
  replicate, the exact rounding of x87 code (`double`, original operation
  order, `_ftol` = truncation). Each gets a short comment saying it is
  deliberate.
- **Calls of other replaced functions through their S3.EXE address**
  (so tests can stub them and the counting thunk sees them).

After a restructuring, rerun the function's in-process tests and emulator
spec; a batch also reruns a fast replay against the original.

## 2. Traceability to the original

Readable code must still lead back to the binary:

- Every replaced function keeps its `// Purpose:` comment (what the game
  uses it for) and its original address, in the file header or right
  above it (`CGameData::ResetSettlerToIdle (0x50cf50)`).
- Internal helpers we split off a large function say which part of the
  original they cover (`// 0x4e0ec6..0x4e1648: military buildings`).
- Struct fields keep their offset (`static_assert` plus the offset in the
  comment); globals live in `game/globals.h` / `tables.h` / `texts.h`
  with their address.
- A non-obvious restructuring (loop instead of the original's unrolled
  code, a merged branch) gets a one-line comment with the original
  address range.

## 3. Named constants instead of magic numbers

- Values that mean something get a name: enums for categories
  (`SettlerJob`, `SettlerType`, `SettlerClass`, `BuildingType`, `Good`,
  `Race`, ...), named flag constants for bits (`layout::kTileBlocked`,
  `layout::kPathWaiting`, FindPath's `kPathWait`/`kPathSwap`),
  `constexpr` constants for limits and tunables (`kMaxPlayers = 20`).
  Write `TypeOf(building) == BuildingType::Woodcutter`, not
  `building.art == 2`.
- Enums are `enum class Name : std::uint8_t` (or the field's width), so a
  struct field can use the enum type without changing the layout
  (`static_assert(sizeof(...))` stays). Shared enums live in
  `src/s3re/game/enums.h` (one place; each enumerator with its value).
- Names come from evidence: the game's own strings (German DebugLog
  texts, data files, the manual), RTTI, or what the code does. A value
  whose meaning isn't known yet is still named, as a placeholder with its
  number (`SettlerJob::Job309`), so it can be found and renamed later.
- Don't name a value that is just arithmetic (`x + 1`, `count / 2`).
- **Enum or constant?** A *category* (one of several kinds a field can
  hold: job, type, good, race, class) is an `enum class`. *Bits* of a flag
  byte are `constexpr` flag constants (`kTileClaimed`, `kPathWait`), since
  they are combined and masked. *Limits and tunables* (a pile holds 8, a
  carrier is sent at 5, 18 phases) are `constexpr` constants. Constants
  used by one file stay in that file's anonymous namespace; move them to a
  header (`enums.h`, `layout.h`, `pathing.h`) once a second file needs
  them.
- **Placeholders** are the category's word plus the decimal value:
  `SettlerJob::Job309`, `SettlerType::Type20`, `Good::Good31`,
  `SettlerClass::Class41`. When a placeholder's meaning becomes clear,
  rename it everywhere and update `re/enums.md`.
- Each enum has the field's width as its underlying type; where the same
  category sits in fields of different widths, the enum takes the widest
  (`Race` is int32 for Player::race, the uint8 fields widen exactly).
  Never narrow a wider value into an enum: switch on the integer with
  `case Value(Good::Coal):` instead (UpdateEconomy's `ConsumersOf` gets
  goods numbers above 255).
- `Value(e)` (enums.h) gives the underlying integer, for table indexes,
  arguments of original functions and comparisons with dword arguments
  (`job == Value(SettlerJob::UnitIdle)` when the original compares all
  32 bits).
- Evidence and confidence for every enumerator: `re/enums.md`.

### Typed fields (decided 2026-11-22, pilot)

The record fields in `layout.h` stay plain integers for now; converted
code reads and writes them through the accessors of
`game/typed_fields.h`: `JobOf(settler)`, `SetJob(settler, job)`,
`TypeOf(settler)`, `TypeOf(building)`, `GoodOf(pile)`, `RaceOf(...)`,
`ClassOf(type or settler)`. Changing the field types now would have
touched about 30 files at once (188 uses of `.job`, 328 of `.art`), and
every `layout.h` change makes `run.py --changed` select every spec. Once
all files that use a field are converted, the field takes the enum type
(same size, the `static_assert`s keep holding) and its accessors go.
Fields that are indexes (`settler_lists[settler.art]`) or that are passed
to original functions stay raw (`settler.art`, or `Value(type)`).

## 4. Decimal by default, hex where it carries meaning

- **Decimal** for counts, amounts, coordinates, distances, timers, ticks,
  percentages, enum values, array sizes, loop bounds.
- **Hex** for addresses (`0x50cf50`), struct offsets (`+0x1a`, as in the
  disassembly and `re/structs.md`), and bit masks that aren't named yet
  (`0x80` shows the bit, `128` doesn't). Prefer named flags over masks.
- Where a comment refers to a value the disassembly shows in hex, give
  both when it helps (`job 309 (0x135)`).

## 5. Ordinary modern C++ (within the constraints)

- Small functions with descriptive names; early returns instead of deep
  nesting; `for` loops over ranges instead of index juggling; local
  variables named for what they hold (`settler`, `building`, `pile`),
  not `v1`/`eax`.
- References to records (`Settler& settler = world->settlers[id];`)
  instead of repeated indexing.
- `const` wherever a value doesn't change; `constexpr` for compile-time
  constants; no macros except the hook/test registration macros.
- Calling conventions stay as the original needs them (`__fastcall` with
  the dummy EDX for `__thiscall`, see `hooks.h`); wrap the convention at
  the boundary and write the body in plain C++.
- No reliance on DLL startup code (the emulator doesn't run it): no
  globals with dynamic initializers in code the emulator runs.
- Formatting: 4 spaces, braces on the same line, ~110 columns (as the
  existing code).

## 6. Comments

- Explain *why* and *what the game means*, not what the C++ already says.
- Keep the evidence for a name or a quirk next to it, short; longer
  analysis goes to `re/*.md`.
- Unknown meaning: say so (`// unknown, set by 0x4e0ec6 only`).

## Example (session 45 pilot)

`UpdateEconomy`'s per-building handler (update_economy.cpp, jump table
0x4e495c), before:

```cpp
switch (b.art) {
    case 0x04: Production(w, n, 6, {{3, 8, 0, 0x12}}); break;
    case 0x07: Production(w, n, 10, {{8, 2, 6, 0x28}, {0x18, 2, 6, 0x24}, {4, 4, 4, 0x1e}}); break;
    case 0x0c: UpdateTileDropBuilding(w, n, 4, 8, true); break;
    case 0x22: UpdateTradeBuilding(w, n, true); break;   // "port-like"
    case 0x2c: UpdateOffering(w, n, 0x15, false); break;
```

after:

```cpp
switch (TypeOf(building)) {
case B::Sawmill: Production(world, n, S::Sawmiller, {{G::Log, kFull, 0, 18}}); break;
// Mines: three foods, each asked for until 2 or 4 units are there.
case B::CoalMine:
    Production(world, n, S::Miner, {{G::Fish, 2, 6, 40}, {G::Meat, 2, 6, 36}, {G::Bread, 4, 4, 30}});
    break;
case B::Toolsmith: UpdateSmith(world, n, 4, 8, true); break;
case B::Marketplace: UpdateTradeBuilding(world, n, true); break;
case B::SmallTemple: UpdateOffering(world, n, G::Wine, false); break;
```

Naming the values also checked them: the worker and input goods of every
building type had to fit, and two earlier guesses turned out wrong (0x22
is the marketplace, not a port; the "water buildings" are the war machine
workshops), see `re/enums.md`.

## Lessons from the pilot (2026-11-22)

- **Evaluation order.** C++ leaves the order of function arguments open
  (MSVC evaluates right to left). `TileAt(world, NeighbourXOf(..),
  NeighbourYOf(..))` would call NeighbourY first; when the callees are
  probed or counted, compute such values into locals first, in the
  original's order.
- **Call wrappers convert every argument to a dword**
  (`decltype(static_cast<std::uint32_t>(args))...`), so passing a `uint8_t`
  field or an enum's `Value()` never leaves garbage in the upper bytes.
- **Reads may be reordered, writes and calls may not.** Hoisting a field
  read is fine when nothing in between writes it; a value the original
  re-reads after a call (request flags after AddBuildingGoodsRequest, the
  site position per footprint tile) is re-read in ours too, with a comment.
- **Test inputs keep their random draws.** Converting a test's value
  tables to enum names is safe as long as the values and the number and
  order of `rng` calls stay the same (`sizeof` of a uint8 enum array is
  still its element count).
- **Splitting a big function** into helpers per original block (named
  after what the block does, with the block's step number or address)
  made ResetSettlerToIdle and UpdateEconomy readable without changing a
  single call; long `switch`es over job values read well with
  `using Job = SettlerJob;` and short aliases (`B::`, `S::`, `G::`) inside
  the function.
- **Not every hex value becomes decimal:** bytes whose nibbles mean
  something (production states 0x30..0x43, terrain classes 0x20), masks and
  marker words (0xffff, 0x80000000 list ends) stay hex.

## Checks

- `python tools/inventory.py check` runs the typed-access lint (no raw
  offsets or data addresses outside the globals headers).
- Planned: a lint for hex literals in comparisons and for magic numbers
  in job/type comparisons once `enums.h` covers them.
