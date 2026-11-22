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
std::uint32_t __fastcall CGameData_PileIncAmountLeaving(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_AddTilePileToArea(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_CreatePile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t goods, std::uint32_t amount, std::uint32_t kind);

// area_split.cpp
std::uint32_t __fastcall CGameData_AllocArea(CGameData* world, void* edx, std::uint32_t player);
std::uint32_t __fastcall CGameData_BlockTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_UnblockTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// building_goods.cpp
std::uint32_t __fastcall CGameData_BuildingInputWord2A(CGameData* world, void* edx, std::uint32_t building,
                                                       std::uint32_t goods);
std::uint32_t __fastcall CGameData_SetBuildingInputWord2A(CGameData* world, void* edx, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value);
std::uint32_t __fastcall CGameData_SetBuildingInputWord24(CGameData* world, void* edx, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value);

// flood_fills.cpp
std::uint32_t __fastcall CGameData_FloodMarkAreaTerrain5To7(CGameData* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall CGameData_FloodSetByte10MarkedTiles(CGameData* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall CGameData_FloodSetWord16RoomyTiles(CGameData* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall CGameData_FloodAssignMarkedTiles(CGameData* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t area, std::uint32_t player);
std::uint32_t __fastcall CGameData_FloodMoveArea(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t newArea);
void __fastcall CGameData_SplitTileByte10Region(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_SplitWord16RegionsAround(CGameData* world, void* edx, std::uint32_t x,
                                                            std::uint32_t y);
std::uint32_t __fastcall CGameData_MoveTileToAreaAndQueue(CGameData* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t oldArea,
                                                          std::uint32_t newArea);
std::uint32_t __fastcall CGameData_FloodFillTileWord16(CGameData* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall CGameData_FloodFillTileByte10(CGameData* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall CGameData_MergeTileByte10Regions(CGameData* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y);
std::uint32_t __fastcall CGameData_AssignWord16RegionsAround(CGameData* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y);
std::uint32_t __fastcall CGameData_MergeTileWord16Regions(CGameData* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y);

// id_allocators.cpp
std::uint32_t __fastcall CGameData_FindFreeId256(CGameData* world, void* edx);
std::uint32_t __fastcall CGameData_FindFreeId4096(CGameData* world, void* edx);
std::uint32_t __fastcall CGameData_FindFreeId8192(CGameData* world, void* edx);

// map_generation.cpp
void __fastcall CGameData_ReplaceTerrainNextTo(CGameData* world, void* edx, std::uint32_t terrain,
                                               std::uint32_t neighbour, std::uint32_t replacement);
std::uint32_t __fastcall CGameData_ExpandTerrain(CGameData* world, void* edx, std::uint32_t terrain,
                                                 std::uint32_t into);
std::uint32_t __fastcall CGameData_MarkTerrainEdge(CGameData* world, void* edx, std::uint32_t terrain);
std::uint32_t __fastcall CGameData_ScatterTileByte11(CGameData* world, void* edx, std::uint32_t base,
                                                     std::uint32_t density);
std::uint32_t __fastcall CGameData_ScatterTerrainNear(CGameData* world, void* edx, std::uint32_t required,
                                                      std::uint32_t terrain, std::uint32_t density);
std::uint32_t __fastcall CGameData_ScatterObjects(CGameData* world, void* edx, std::uint32_t required,
                                                  std::uint32_t object, std::uint32_t density,
                                                  std::uint32_t blockMode, std::uint32_t extra);
std::uint32_t __fastcall CGameData_ScatterObjectGroups(CGameData* world, void* edx, std::uint32_t required,
                                                       std::uint32_t low, std::uint32_t high, std::uint32_t density,
                                                       std::uint32_t blockMode, std::uint32_t extra);

// map_marks.cpp
void __fastcall CGameData_SetDiagonalTileByteB(CGameData* world, void* edx, std::uint32_t flags);
void __fastcall CGameData_ClearDiagonalTileByteB(CGameData* world, void* edx, std::uint32_t flags);

// map_regions.cpp
std::uint32_t __fastcall CGameData_BuildRegionsAndAreas(CGameData* world, void* edx);

// map_directions.cpp
std::uint32_t __stdcall NeighbourX(std::uint32_t x, std::uint32_t direction);
std::uint32_t __stdcall NeighbourY(std::uint32_t y, std::uint32_t direction);

// map.cpp
std::uint32_t __fastcall CGameData_IsOnMap(CGameData* world, void* edx, std::uint32_t x,
                                           std::uint32_t y);
std::uint32_t __fastcall CGameData_IsInsideMapBorder(CGameData* world, void* edx, std::uint32_t x,
                                                     std::uint32_t y);

// map_areas.cpp
std::uint32_t __fastcall CGameData_AssignTileToArea(CGameData* world, void* edx, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t area);
std::uint32_t __fastcall CGameData_RemoveTileFromArea(CGameData* world, void* edx, std::uint32_t x,
                                                      std::uint32_t y);
std::uint32_t __fastcall CGameData_SetTileAreaMarked(CGameData* world, void* edx, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t value);

// map_neighbourhood.cpp
std::uint32_t __fastcall CGameData_IsFreeOfSettlerType29Nearby(CGameData* world, void* edx, std::uint32_t x,
                                                               std::uint32_t y);
std::uint32_t __fastcall CGameData_ReplaceTerrainType(CGameData* world, void* edx,
                                                      std::uint32_t oldType, std::uint32_t newType);
std::uint32_t __fastcall CGameData_IsNeighbourhoodTerrain(CGameData* world, void* edx, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t terrain);
std::uint32_t __fastcall CGameData_AreOffsetTilesClear(CGameData* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t count);
std::uint32_t __fastcall CGameData_IsNeighbourhoodByte9Clear(CGameData* world, void* edx, std::uint32_t x,
                                                             std::uint32_t y);
std::uint32_t __fastcall CGameData_HasAtMostTwoType70Neighbours(CGameData* world, void* edx, std::uint32_t x,
                                                                std::uint32_t y);
std::uint32_t __fastcall CGameData_IsClearOfType70Nearby(CGameData* world, void* edx, std::uint32_t x,
                                                         std::uint32_t y);

// players.cpp
std::uint32_t __fastcall CGameData_AddPlayerCounter(CGameData* world, void* edx, std::uint32_t player,
                                                    std::uint32_t index, std::uint32_t amount);

// map_terrain.cpp
std::uint32_t __fastcall CGameData_SpreadTerrainRandomly(CGameData* world, void* edx, std::uint32_t from,
                                                         std::uint32_t neighbour, std::uint32_t to,
                                                         std::uint32_t percent);
std::uint32_t __fastcall CGameData_ReplaceTerrainF3(CGameData* world, void* edx, std::uint32_t newType);
std::uint32_t __fastcall CGameData_GrowTerrain(CGameData* world, void* edx, std::uint32_t from, std::uint32_t into,
                                               std::uint32_t percent);
std::uint32_t __fastcall CGameData_UpdateBorderFlagsAround(CGameData* world, void* edx, std::uint32_t x,
                                                           std::uint32_t y);
std::uint32_t __fastcall CGameData_TileShade(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// random.cpp
std::uint16_t __fastcall CGameData_Random(CGameData* world, void* edx);
std::uint32_t __fastcall CGameData_RandomBelow6(CGameData* world, void* edx);

// logic_helpers.cpp
std::uint32_t __fastcall CGameData_WearPath(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// settler_records.cpp
std::uint32_t __fastcall CGameData_IsSettlerJob01Or37(CGameData* world, void* edx, std::uint32_t job);
std::uint32_t __fastcall CGameData_CreateSettler(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t type, std::uint32_t player);

// settler_queries.cpp
std::uint32_t __fastcall CGameData_SetSettlerTarget(CGameData* world, void* edx, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_RemoveSettlerFromLists(CGameData* world, void* edx, std::uint32_t settler);

// settler_walk.cpp
std::uint32_t __fastcall CGameData_AddTileTraffic(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_IsWalkingJob(CGameData* world, void* edx, std::uint32_t job);
std::uint32_t __fastcall CGameData_IsStationaryJob(CGameData* world, void* edx, std::uint32_t job);

// settlers.cpp
std::uint32_t __fastcall CGameData_SettlerGroup(CGameData* world, void* edx, std::uint32_t settler);

// ui_state.cpp
std::uint32_t __fastcall CGameData_ClampCamera(CGameData* world, void* edx);
std::uint32_t __fastcall CGameData_CenterCameraOnTile(CGameData* world, void* edx, std::uint32_t x,
                                                      std::uint32_t y);
std::uint32_t __fastcall CGameData_PushUiFeedback(CGameData* world, void* edx, std::uint32_t value);

// world_search.cpp
void __fastcall CGameData_NextSearchGeneration(CGameData* world, void* edx);
std::uint32_t __stdcall TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                     std::uint32_t y1);

// world_tables.cpp
std::uint32_t __fastcall CGameData_QueueEvent(CGameData* world, void* edx, std::uint32_t type, std::uint32_t a,
                                              std::uint32_t b, std::uint32_t c);
std::uint32_t __fastcall CGameData_QueueMarkedTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t player);
std::uint32_t __fastcall CGameData_QueuePendingEvent(CGameData* world, void* edx, std::uint32_t type,
                                                     std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall CGameData_QueueMarkedTileTerrain5To7(CGameData* world, void* edx,
                                                              std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_QueueMarkedTileNoOwner(CGameData* world, void* edx,
                                                          std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_ReplaceTileWord16AndQueue(CGameData* world, void* edx,
                                                             std::uint32_t x, std::uint32_t y,
                                                             std::uint32_t expected,
                                                             std::uint32_t value);
std::uint32_t __fastcall CGameData_ReplaceTileByte10AndQueue(CGameData* world, void* edx,
                                                             std::uint32_t x, std::uint32_t y,
                                                             std::uint32_t expected,
                                                             std::uint32_t value);

// win32_errors.cpp (the framework's assert; never returns)
void __fastcall Win32_ThrowAssertmentFailed(const char* expression, const char* file, std::int32_t line);

}  // namespace s3re::game
