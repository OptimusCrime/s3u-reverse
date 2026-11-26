#pragma once

// The operator[] index checks of the game settings' player and team vectors
// (game_settings_vectors.cpp). The Watcom library emits them out of line
// (WCValOrderedVector<Dirk::CPlayer> 0x437950, WCValSortedVector<Dirk::CTeam>
// 0x437e20) and inlines the same code into many callers (LoadSaveGame,
// SetupLocalPlayer, FindTeamForClan); those use these helpers.

#include "watcom/vector_base.h"

#include <cstdint>

namespace s3re::game {

// The index to use for operator[](index): in range (0..length-1, signed)
// the index itself; on a non-empty vector 0 below the range and length-1
// above it; on an empty vector element 0 is made: without capacity the
// vector is resized to one element through `resize1` (the S3.EXE function
// whose address is given: a resize(1) followed by the length clamp; when it
// fails, 0 is returned at once), then vtable slot 1 is called with 0 and
// the length set to 1.
std::int32_t CheckedVectorIndex(watcom::VectorBase* vector, std::int32_t index, std::uint32_t (*resize1)(watcom::VectorBase*));

// The two out-of-line checks' growth: resize 0x437820 / 0x437d00 with the
// length clamped to 1.
std::uint32_t GrowPlayerVectorToOne(watcom::VectorBase* vector);
std::uint32_t GrowTeamVectorToOne(watcom::VectorBase* vector);
// The growth some callers inline instead (SetupLocalPlayer's LogSlot loop):
// InsertAt's reallocation to one element (operator new 0xcc, the copy
// constructor 0x49eaf0, the scalar deleting destructor 0x437fc0 with 0),
// the length clamped to 1.
std::uint32_t GrowPlayerVectorInline(watcom::VectorBase* vector);

inline std::int32_t PlayerVectorIndex(watcom::VectorBase* vector, std::int32_t index) {
    return CheckedVectorIndex(vector, index, &GrowPlayerVectorToOne);
}
inline std::int32_t TeamVectorIndex(watcom::VectorBase* vector, std::int32_t index) {
    return CheckedVectorIndex(vector, index, &GrowTeamVectorToOne);
}

}  // namespace s3re::game
