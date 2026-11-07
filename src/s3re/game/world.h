#pragma once

// The game world object.
//
// The simulation state lives in one very large, non-polymorphic object
// (more than 20 MB of fields). CLogicThread keeps a pointer to it at
// this+0x3a0. Its real name is not known; we call it GameWorld. Until its
// layout is understood we treat it as raw memory and access fields through the
// offsets below. Each offset is confirmed by the functions noted next to it.

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct GameWorld;  // opaque

namespace world_offset {
// uint32: map width/height in tiles (square maps; rows are 0x300 tiles apart).
// Used by GameWorld::UpdateEnvironment (0x4e7810).
constexpr std::size_t kMapSize = 0x2d2d0;
// 3 x uint16: state of the simulation random number generator.
// GameWorld::Random (0x4feb70).
constexpr std::size_t kRandomState = 0x2d2dc;

// Map tiles: 24-byte records, tile (x, y) at kMapTiles + (x * 0x300 + y) * 24.
// From GameWorld::UpdateEnvironment, which reads fields of a tile and its
// neighbours at 0x6742c..0x67455 (+-0x18 = y+-1, +-0x4800 = x+-1). The exact
// start of the record (0x67440 or 0x67444) is not settled yet.
constexpr std::size_t kMapTiles = 0x67440;
constexpr std::size_t kTileBytes = 0x18;
constexpr std::size_t kMapRowTiles = 0x300;
constexpr std::size_t kMapRowBytes = kMapRowTiles * kTileBytes;
}  // namespace world_offset

template <typename T>
T& WorldField(GameWorld* world, std::size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

}  // namespace s3re::game
