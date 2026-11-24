#pragma once

// The game's categories as named values: settler jobs and types, settler
// classes, building types, goods, races, and a few terrain values.
//
// Each enum's underlying type is the width of the record field that holds
// it (layout.h), so a field can later take the enum type without changing
// the layout. Until the fields do, game/typed_fields.h converts (JobOf,
// TypeOf, GoodOf, RaceOf, ...). Values are decimal; the comment gives the
// hex form the disassembly shows.
//
// Names come from evidence in S3.EXE: its German DebugLog strings, what the
// code does with a value (which worker a building requests, which goods it
// asks for, ...), and the type tables at 0x658f64 (class), 0x6590e4 (start
// health) and 0x659264 (consumer building types). Public Settlers 3 tools
// list the same building, settler and goods ids; they were used as hints
// and every name used here agrees with the binary's behaviour. Evidence and
// confidence per value: re/enums.md. A value whose meaning is not known yet
// is a placeholder with its decimal number (SettlerJob::Job390), so it can
// be found and renamed later.
//
// One header per category in game/enums/ (split 2026-11-23); this file
// includes them all. Include only the headers you use.

#include "game/enums/value.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/enums/spells.h"
#include "game/enums/job_ids.h"
#include "game/enums/commands.h"
#include "game/enums/terrain.h"
#include "game/enums/directions.h"
