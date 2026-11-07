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

// Byte offset, inside a tile record, of the "visited" mark used by searches
// (stamped with the search generation). Absolute offset 0x6744f, used by
// GameWorld::NextSearchGeneration (0x4ff3f0).
constexpr std::size_t kTileSearchMark = 15;

// uint8: current search generation, and a counter PrepareDrawData increments
// that NextSearchGeneration resets.
constexpr std::size_t kSearchGeneration = 0x2d300;
constexpr std::size_t kSearchGenerationAux = 0x2d301;

// Statistics (start of the world object):
// int32[13] smoothing kernel 3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3
// (GameWorld::InitStatisticsKernel 0x4ff4c0).
constexpr std::size_t kStatisticsKernel = 0x4;
constexpr std::size_t kStatisticsKernelSize = 13;
// uint8[20 players][36 categories][256 samples], newest sample first
// (GameWorld::ShiftStatistics 0x4ff470, GameWorld::AddStatistic 0x4ff510).
constexpr std::size_t kStatisticsHistory = 0x38;
constexpr std::size_t kStatisticsPlayers = 20;
constexpr std::size_t kStatisticsCategories = 36;
constexpr std::size_t kStatisticsSamples = 256;
constexpr std::size_t kStatisticsPlayerBytes = kStatisticsCategories * kStatisticsSamples;

// Per-player records, 0x148 bytes each; int32 at +0 is non-zero for players
// taking part (ShiftStatistics skips the others).
constexpr std::size_t kPlayerRecords = 0x11943dc;
constexpr std::size_t kPlayerRecordBytes = 0x148;
}  // namespace world_offset

template <typename T>
T& WorldField(GameWorld* world, std::size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

}  // namespace s3re::game
