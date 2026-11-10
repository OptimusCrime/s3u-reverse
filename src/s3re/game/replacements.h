#pragma once

// Our replacements that other replacements call.
//
// When an original function calls another original function that we have
// already replaced, our version of the caller calls our version of the
// callee directly (in the game the call would land in it anyway, through the
// hook). Declarations only; each is defined, documented and tested in the
// file named next to it.

#include "game/world.h"

#include <cstdint>

namespace s3re::game {

// area_piles.cpp
std::uint32_t __fastcall GameWorld_PileIncAmountLeaving(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_AddTilePileToArea(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_CreatePile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t goods, std::uint32_t amount, std::uint32_t kind);

// area_split.cpp
std::uint32_t __fastcall GameWorld_AllocArea(GameWorld* world, void* edx, std::uint32_t player);
std::uint32_t __fastcall GameWorld_BlockTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_UnblockTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// building_goods.cpp
std::uint32_t __fastcall GameWorld_BuildingInputWord2A(GameWorld* world, void* edx, std::uint32_t building,
                                                       std::uint32_t goods);
std::uint32_t __fastcall GameWorld_SetBuildingInputWord2A(GameWorld* world, void* edx, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value);
std::uint32_t __fastcall GameWorld_SetBuildingInputWord24(GameWorld* world, void* edx, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value);

// flood_fills.cpp
std::uint32_t __fastcall GameWorld_FloodMarkAreaTerrain5To7(GameWorld* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall GameWorld_FloodSetByte10MarkedTiles(GameWorld* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall GameWorld_FloodSetWord16RoomyTiles(GameWorld* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall GameWorld_FloodAssignMarkedTiles(GameWorld* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t area, std::uint32_t player);
std::uint32_t __fastcall GameWorld_FloodMoveArea(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t newArea);
void __fastcall GameWorld_SplitTileByte10Region(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_SplitWord16RegionsAround(GameWorld* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y);
std::uint32_t __fastcall GameWorld_MoveTileToAreaAndQueue(GameWorld* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t oldArea,
                                                          std::uint32_t newArea);
std::uint32_t __fastcall GameWorld_FloodFillTileWord16(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall GameWorld_FloodFillTileByte10(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall GameWorld_MergeTileByte10Regions(GameWorld* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y);
std::uint32_t __fastcall GameWorld_AssignWord16RegionsAround(GameWorld* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y);
std::uint32_t __fastcall GameWorld_MergeTileWord16Regions(GameWorld* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y);

// id_allocators.cpp
std::uint32_t __fastcall GameWorld_FindFreeId256(GameWorld* world, void* edx);
std::uint32_t __fastcall GameWorld_FindFreeId4096(GameWorld* world, void* edx);
std::uint32_t __fastcall GameWorld_FindFreeId8192(GameWorld* world, void* edx);

// map_generation.cpp
void __fastcall GameWorld_ReplaceTerrainNextTo(GameWorld* world, void* edx, std::uint32_t terrain,
                                               std::uint32_t neighbour, std::uint32_t replacement);
std::uint32_t __fastcall GameWorld_ExpandTerrain(GameWorld* world, void* edx, std::uint32_t terrain,
                                                 std::uint32_t into);
std::uint32_t __fastcall GameWorld_MarkTerrainEdge(GameWorld* world, void* edx, std::uint32_t terrain);
std::uint32_t __fastcall GameWorld_ScatterTileByte11(GameWorld* world, void* edx, std::uint32_t base,
                                                     std::uint32_t density);
std::uint32_t __fastcall GameWorld_ScatterTerrainNear(GameWorld* world, void* edx, std::uint32_t required,
                                                      std::uint32_t terrain, std::uint32_t density);
std::uint32_t __fastcall GameWorld_ScatterObjects(GameWorld* world, void* edx, std::uint32_t required,
                                                  std::uint32_t object, std::uint32_t density,
                                                  std::uint32_t blockMode, std::uint32_t extra);
std::uint32_t __fastcall GameWorld_ScatterObjectGroups(GameWorld* world, void* edx, std::uint32_t required,
                                                       std::uint32_t low, std::uint32_t high, std::uint32_t density,
                                                       std::uint32_t blockMode, std::uint32_t extra);

// map_marks.cpp
void __fastcall GameWorld_SetDiagonalTileByteB(GameWorld* world, void* edx, std::uint32_t flags);
void __fastcall GameWorld_ClearDiagonalTileByteB(GameWorld* world, void* edx, std::uint32_t flags);

// map_regions.cpp
std::uint32_t __fastcall GameWorld_BuildRegionsAndAreas(GameWorld* world, void* edx);

// map_directions.cpp
std::uint32_t __stdcall NeighbourX(std::uint32_t x, std::uint32_t direction);
std::uint32_t __stdcall NeighbourY(std::uint32_t y, std::uint32_t direction);

// map.cpp
std::uint32_t __fastcall GameWorld_IsOnMap(GameWorld* world, void* edx, std::uint32_t x,
                                           std::uint32_t y);
std::uint32_t __fastcall GameWorld_IsInsideMapBorder(GameWorld* world, void* edx, std::uint32_t x,
                                                     std::uint32_t y);

// map_areas.cpp
std::uint32_t __fastcall GameWorld_AssignTileToArea(GameWorld* world, void* edx, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t area);
std::uint32_t __fastcall GameWorld_RemoveTileFromArea(GameWorld* world, void* edx, std::uint32_t x,
                                                      std::uint32_t y);
std::uint32_t __fastcall GameWorld_SetTileAreaMarked(GameWorld* world, void* edx, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t value);

// map_neighbourhood.cpp
std::uint32_t __fastcall GameWorld_IsFreeOfSettlerType29Nearby(GameWorld* world, void* edx, std::uint32_t x,
                                                               std::uint32_t y);
std::uint32_t __fastcall GameWorld_ReplaceTerrainType(GameWorld* world, void* edx,
                                                      std::uint32_t oldType, std::uint32_t newType);
std::uint32_t __fastcall GameWorld_IsNeighbourhoodTerrain(GameWorld* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t terrain);
std::uint32_t __fastcall GameWorld_AreOffsetTilesClear(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t count);
std::uint32_t __fastcall GameWorld_IsNeighbourhoodByte9Clear(GameWorld* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y);
std::uint32_t __fastcall GameWorld_HasAtMostTwoType70Neighbours(GameWorld* world, void* edx, std::uint32_t x,
                                                                std::uint32_t y);
std::uint32_t __fastcall GameWorld_IsClearOfType70Nearby(GameWorld* world, void* edx, std::uint32_t x,
                                                         std::uint32_t y);

// players.cpp
std::uint32_t __fastcall GameWorld_AddPlayerCounter(GameWorld* world, void* edx, std::uint32_t player,
                                                    std::uint32_t index, std::uint32_t amount);

// map_terrain.cpp
std::uint32_t __fastcall GameWorld_SpreadTerrainRandomly(GameWorld* world, void* edx, std::uint32_t from,
                                                         std::uint32_t neighbour, std::uint32_t to,
                                                         std::uint32_t percent);
std::uint32_t __fastcall GameWorld_ReplaceTerrainF3(GameWorld* world, void* edx, std::uint32_t newType);
std::uint32_t __fastcall GameWorld_GrowTerrain(GameWorld* world, void* edx, std::uint32_t from, std::uint32_t into,
                                               std::uint32_t percent);
std::uint32_t __fastcall GameWorld_UpdateBorderFlagsAround(GameWorld* world, void* edx, std::uint32_t x,
                                                           std::uint32_t y);
std::uint32_t __fastcall GameWorld_TileShade(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// random.cpp
std::uint16_t __fastcall GameWorld_Random(GameWorld* world, void* edx);
std::uint32_t __fastcall GameWorld_RandomBelow6(GameWorld* world, void* edx);

// settler_records.cpp
std::uint32_t __fastcall GameWorld_CreateSettler(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t type, std::uint32_t player);

// settler_queries.cpp
std::uint32_t __fastcall GameWorld_SetSettlerTarget(GameWorld* world, void* edx, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_RemoveSettlerFromLists(GameWorld* world, void* edx, std::uint32_t settler);

// ui_state.cpp
std::uint32_t __fastcall GameWorld_ClampCamera(GameWorld* world, void* edx);
std::uint32_t __fastcall GameWorld_CenterCameraOnTile(GameWorld* world, void* edx, std::uint32_t x,
                                                      std::uint32_t y);
std::uint32_t __fastcall GameWorld_PushUiFeedback(GameWorld* world, void* edx, std::uint32_t value);

// world_search.cpp
void __fastcall GameWorld_NextSearchGeneration(GameWorld* world, void* edx);
std::uint32_t __stdcall TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                     std::uint32_t y1);

// world_tables.cpp
std::uint32_t __fastcall GameWorld_QueueEvent(GameWorld* world, void* edx, std::uint32_t type, std::uint32_t a,
                                              std::uint32_t b, std::uint32_t c);
std::uint32_t __fastcall GameWorld_QueueMarkedTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t player);
std::uint32_t __fastcall GameWorld_QueuePendingEvent(GameWorld* world, void* edx, std::uint32_t type,
                                                     std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall GameWorld_QueueMarkedTileTerrain5To7(GameWorld* world, void* edx,
                                                              std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_QueueMarkedTileNoOwner(GameWorld* world, void* edx,
                                                          std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_ReplaceTileWord16AndQueue(GameWorld* world, void* edx,
                                                             std::uint32_t x, std::uint32_t y,
                                                             std::uint32_t expected,
                                                             std::uint32_t value);
std::uint32_t __fastcall GameWorld_ReplaceTileByte10AndQueue(GameWorld* world, void* edx,
                                                             std::uint32_t x, std::uint32_t y,
                                                             std::uint32_t expected,
                                                             std::uint32_t value);

}  // namespace s3re::game
