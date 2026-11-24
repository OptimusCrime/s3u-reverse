"""Emulator specs, part `net`: specs of the replacements in src/s3re/game/net/.

Not a standalone module: specs/__init__.py executes every part, in
PARTS order, into one shared namespace (common first), so a part uses
the names of common.py and of its own earlier lines without imports.
"""
BLOCK_SOURCE = SCRATCH + 3 * SCRATCH_SLOT_BYTES


def block_size(rng, pool) -> int:
    return rng.choice([0, 1, 4, 15, 16, 17, 32, 48, rng.randrange(0x60)])

SPECS += [
    Spec("Lobby_MemoryBlock_Size", 0x00601270, "thiscall", [], presets=block_world(0)),
    # 0x60134b: the catch(...) handler (only reachable by an exception).
    Spec("Lobby_MemoryBlock_Reserve", 0x00601280, "thiscall", [block_size], eax_mask=0, presets=block_world(0),
         new_fails=0.15, exempt_blocks=(0x0060134b,)),
    Spec("Lobby_MemoryBlock_Append", 0x00601170, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)], eax_mask=0,
         presets=block_world(0), new_fails=0.1),
    # 0x60122d: the catch(...) handler.
    Spec("Lobby_MemoryBlock_AppendBytes", 0x006011c0, "thiscall", [one_of(WORLD_BASE + BLOCK_SOURCE), block_size],
         eax_mask=0, presets=block_world(0), new_fails=0.1, exempt_blocks=(0x0060122d,)),
    Spec("Lobby_MemoryBlock_At", 0x00600ee0, "thiscall", [below(0x42)], presets=block_world(0), new_fails=0.1),
    Spec("Lobby_MemoryBlock_Equal", 0x00600ea0, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(0)),
    Spec("Lobby_MemoryBlock_Copy", 0x00600e40, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER)],
         presets=block_world(0), new_fails=0.1),
    # cover_until: nop padding follows the tail jump.
    Spec("Lobby_MemoryBlock_Destroy", 0x00600e70, "thiscall", [], presets=block_world(0), cover_until=0x00600e7b),
    Spec("Lobby_CLMGeneric_SetHeader", 0x00603270, "thiscall", [anything], eax_mask=0, presets=block_world(4),
         new_fails=0.1),
    Spec("Lobby_CLMGeneric_Construct", 0x00601e80, "thiscall", [anything], presets=block_world(4), new_fails=0.1),
    Spec("Lobby_CLMGeneric_Equal", 0x006027d0, "thiscall", [one_of(WORLD_BASE + BLOCK_OTHER, WORLD_BASE)],
         presets=block_world(4)),
    Spec("Lobby_CLMGeneric_Destroy", 0x00602190, "thiscall", [], presets=block_world(4), cover_until=0x0060219e),
]
