#pragma once

// The game world object.
//
// The simulation state lives in one very large object (CGameData, 0x153a128
// bytes). CLogicThread keeps a pointer to it at this+0x3a0. We call it
// GameWorld; its layout (typed fields, every offset static_asserted) is in
// game/layout.h. Field notes: re/world_layout.md and re/structs.md.

#include "game/layout.h"
