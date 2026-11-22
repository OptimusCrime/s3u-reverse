#pragma once

// Typed views of the category fields of layout.h's records (decided
// 2026-11-22, readability pilot): the fields themselves stay plain integers
// until every file that uses them is converted, so these accessors give
// converted code the enum types of game/enums.h without touching the rest.
// Once a field's users are all converted, the field takes the enum type and
// its accessors go (docs/coding_style.md, "Typed fields").
//
// The conversions are exact: each enum has the field's width, or is wider
// (Race: int32 for the uint8 fields of settlers and buildings).

#include "game/enums.h"
#include "game/layout.h"
#include "game/tables.h"

namespace s3re::game {

// ---------------------------------------------------------------- Settler
inline SettlerJob JobOf(const layout::Settler& settler) {
    return static_cast<SettlerJob>(settler.job);
}
inline void SetJob(layout::Settler& settler, SettlerJob job) {
    settler.job = Value(job);
}
inline SettlerType TypeOf(const layout::Settler& settler) {
    return static_cast<SettlerType>(settler.art);
}
inline Race RaceOf(const layout::Settler& settler) {
    return static_cast<Race>(settler.race);
}

// The class of a settler type (S3.EXE table 0x658f64).
inline SettlerClass ClassOf(SettlerType type) {
    return static_cast<SettlerClass>(g_settlerTypeClass[Value(type)]);
}
inline SettlerClass ClassOf(const layout::Settler& settler) {
    return ClassOf(TypeOf(settler));
}

// ---------------------------------------------------------------- Building
inline BuildingType TypeOf(const layout::Building& building) {
    return static_cast<BuildingType>(building.art);
}
inline Race RaceOf(const layout::Building& building) {
    return static_cast<Race>(building.race);
}

// ---------------------------------------------------------------- Pile
inline Good GoodOf(const layout::Pile& pile) {
    return static_cast<Good>(pile.good);
}

// ---------------------------------------------------------------- Player
inline Race RaceOf(const layout::Player& player) {
    return static_cast<Race>(player.race);
}

}  // namespace s3re::game
