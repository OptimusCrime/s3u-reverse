# Readability and code style

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Readability pass (docs/coding_style.md)
- **Readability leftovers (session 51):** the large test sections of the
  AI/UI/framework files got no deeper pass (implementation code is done);
  `Menus::CDrawThread::OnMessage10` 0x4941c0 keeps its name (it serves
  vtable slots 12 and 20; find a name that fits both).
- **Unnamed objects and terrain (session 51):** the map generator's
  decoration objects 0x01..0x43 (world_create.cpp's tables stay data until
  then) and terrain class 0x90 (`kTerrainClass90`) have no names yet;
  `Spell::GrowTrees` grows crops, not trees (rename it). Job 369
  `BeekeeperEndRound` has no setter in S3.EXE (medium confidence).
- **Typed fields:** `Settler::job` is a `SettlerJob` since session 51.
  Left: `Settler::art`, `Building::art`, `Pile::good` and the race fields:
  once every user is converted, give the field its enum type in
  `layout.h` and drop its accessor from `game/typed_fields.h`.
- **BuildingTypeAllowedForRace** (player_queries.cpp) does not fit the
  building numbering of game/enums/buildings.h; find what its argument really is (the
  type numbers stay decimal numbers there until then).
- **Map effect record fields** (layout.h `MapEffect`): +0x06, +0x2c
  (probably a graphics id), +0x32, +0x34 (the value the "stationary"
  make function 0x543f70 sets) are still `unkNN`; the spawn functions in
  map_effects.cpp name the arguments by offset only.
- **Enforce the hex-literal and magic-number lint rules** (session 51:
  `tools/style_lint.py` reports them; after the readability round
  "hex-compare" has 125 hits and "magic-category" 14, report-only; the
  "narrowing" rule is enforced). Convert the rest, then run them with
  `--strict` in `inventory.py check`.
## Typed access leftovers
- **Class layouts left after struct pass S2 (session 45).** The object
  `RawAt`s are gone (two test-only ones in string_members.cpp are marked
  `raw-ok`). Left, by value:
  - `DrawTarget` (graphics_helpers.cpp; Render_PutPixel16 / Entry204:
    pitch +0x90, pixel buffer +0xa4, table +0x204; Volker\DrawBuilding.cpp):
    class not identified (not CLandscapeSurface, which is 0xa8 bytes).
  - `CLogicThread` (logic_thread_layout.h): mirror its RTTI bases
    (DirectX::CGameThread -> Win32::CThread) once their sizes are known;
    tooling/fast.cpp still uses raw offsets +0x3bc (timer) and +0x3dc
    (wait set) - original-mode tooling, so bump
    `S3RE_ORIGINAL_TOOLING_VERSION` if it changes.
  - Per-file partial structs of not yet identified classes:
    framework_helpers.cpp (OwningPointer, DwordPair, CreditsTextStyle,
    LobbyRecord5ebfb0, NetHelpRecord), framework_small.cpp (IsvList,
    IntVector: move to watcom/ headers as WCIsvSList / WCValVector
    layouts), id_allocators.cpp (IdTable), folded_getters.cpp, and the
    single-string classes of string_members.cpp (named after their
    functions; identify the classes, mostly lobby code).
  - Constructors of CScreenGroup (~0x416ca0) and CControl (~0x418000) are
    known now; their remaining unk fields can be named from them.
- `MessageQueue` (logic_thread_layout.h) is opaque; map its ring-buffer
  fields when the net/bounce thread code is reimplemented.
