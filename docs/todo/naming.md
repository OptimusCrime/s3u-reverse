# Naming

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Naming
- **Audit provisional names.** Many names were given early from partial
  evidence (e.g. `CGameData::UpdateSettlers`,
  `Logic_sub_*` / `SaveGame_sub_*` / `AI_sub_*` survey names). Review them
  when the functions are worked on; see the naming rule in `CLAUDE.md`.
- **WalkSettler whole-game evidence for shooting and boarding:** no check
  so far calls SpawnProjectile (soldiers shooting from a post) or, probably,
  boards a ship; a recorded late game with fighting and ships would cover
  them (settler_walk.cpp; LOGBOOK session 45).
- **Name constructors** (`re/classes.json` lists the functions that store
  each vtable).
- **Remaining `unk` fields of the world records** (layout.h, session 45
  struct pass): Settler +0x1b/+0x2f, Building +0x01/+0x36/+0x37 and the
  site byte +0x1e, Player +0x8 (bit 0: big temple destroyed)/+0x18/+0x1c (network id)/+0x88/+0x12c,
  the MapEffect graphics fields (+0x6, +0x18..+0x24, +0x2c..+0x39; their
  reader is the draw code), world +0x2d2ec/+0x2d304..+0x2d310. Name them
  when their readers (UpdateSettlers, ExecuteCommands, PrepareDrawData)
  are reimplemented.
- **GamePlayerSetup is RTTI's Dirk::CPlayer (session 56, wip/ui-watcom).**
  The Watcom vectors of the game settings are WCValOrderedVector<Dirk::CPlayer>
  with 0xcc-byte elements, whose element functions are our GamePlayerSetup
  ones (constructor 0x49e8f0, destructor 0x49ec80, operator== 0x49f100);
  Dirk::CTeam (0x40 bytes) holds a WCValSortedVector<int> (+8, probably the
  player ids) and a WCValOrderedVector<Dirk::CPlayer> (+0x24). Rename
  GamePlayerSetup -> Dirk::CPlayer everywhere (inventory, structs, code,
  re/structs.md) in a quiet moment: it touches game_settings*.cpp and the
  savegame writer.
- **Watcom instantiation names (session 56):** the vector destructors and
  constructors verified in bare_vectors.cpp are named after the class whose
  scalar deleting destructor calls them, or `WCBareVectorBase_T::...` with
  the address; the "ctor_or_dtor" survey names of the remaining
  instantiations (lobby and network vectors) can be named the same way.
