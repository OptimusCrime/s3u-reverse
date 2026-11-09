// Small helpers of the computer players (Rainer\*.cpp: Cevaland.cpp,
// compGlobs.cpp, CompMapWorkerAgent.cpp and the unnamed file before them).
//
// The AI code reaches the game through globals set before each player's turn
// (AI_SetCurrentPlayer below and 0x551b80):
//   0x7e3408  g_aiWorld: the world (CGameData), from computer player +0x75c
//   0x7e3130  g_aiPlayer: the CComputerPlayer whose turn it is
//   0x7e2eb8  g_aiPlayerNumber (byte): its player number, from +0x272
//   0x7e2eb4  g_aiPlayerSetup: 0x731488 + index * 0x180, index from +0x770
//             (probably the player's entry of a 0x180-byte settings table)
//   0x7e2eb0  g_aiPlayerData: its working data, from +0x778
//
// Functions (calling conventions as in the original):
//   0x00551a30 AI_SetCurrentPlayer(player) fastcall (ECX). Sets the four
//              globals above; EAX = the value stored in g_aiPlayerData.
//   0x00551970 AI_WordListInsert(value, list, count) stdcall: stores value in
//              the first 0xffff slot of a word array; EAX = 1, or 0 if full.
//   0x005519b0 AI_WordListRemove(value, list, count) stdcall: finds value
//              before the first 0xffff, shifts the rest down and puts 0xffff
//              into the last slot; EAX = 1, or 0 if not found.
//   0x00572090 AI_IsOnMap(v) fastcall: 0 <= v < map size (AL only; the rest
//              of EAX is whatever was there or the world pointer).
//   0x00572050 AI_ClampToMap(int* x, int* y) fastcall: clamps both to
//              0..map size (inclusive); EAX = map size.
//   0x0059dc10 AI_AreaGoodsAmount(const u8* goods, const u16* area) fastcall:
//              sums the amount (pile byte +1) over the area's pile list for
//              that goods type (area +0xa + goods * 2, piles linked by +0xc);
//              EAX = the sum & 0xffff.
//   0x0058e170 AI_GetBuilding(id, int* error) fastcall: *error = 0; for an id
//              above the building count *error = 1 and EAX = 0; otherwise the
//              building record (world+0x1499e64 + id * 0x3a), or 0 if its
//              byte +0 (player) is 0xff (free).
//   0x0058ec50 AI_RecordBestCandidate(index, score, a, b) fastcall (ECX =
//              index, EDX = score, a and b on the stack, ret 8): copies word
//              0x7329d0[index] to g_aiPlayerData +0x2d2 + index * 2; if the
//              word at +0x26 + index * 6 is below score, stores score there
//              and a, b at +0x28 / +0x2a. EAX = index * 6, or index * 3 +
//              0x15 after an update (address arithmetic left over).
//   0x00554700 AI grid cell(x, y) thiscall: [this+0x158] + (y * [this+0x140]
//              + x) * 0xc4 (cells of 0xc4 bytes). ret 8.
//   0x00554040 AI grid free() thiscall: operator delete [this+0x158], then 0.
//   0x0055a420 AI_CopySixBytes(dst, src) fastcall: if dst, copies a dword and
//              a word (a 3-word record; probably a container's copy
//              construction in place). EAX not used.
//   0x0055ca60 AI record set word(value) thiscall: calls the empty function
//              0x59f200 (an inlined empty base call), then word +4 = value.
//              ret 4. Only AX is defined (callers don't use EAX).
//   0x00599610 AI_BuildingArea(const u16* building) fastcall (ECX): the area
//              word (+0x14) of the tile under building record *building
//              (x/y words +0xa/+0xc), or 0xffff if x/y are not below the map
//              size words 0x7e3414/0x7e3412 (16-bit compares). The upper half
//              of EAX is left over from the address arithmetic (reproduced:
//              one caller keeps the whole register).
//   0x0058b480 AI_FreeTileArea(x, y) stdcall: -1 unless both pass AI_IsOnMap;
//              else the area word of tile (x, y) of the world at 0x7e7b60 (a
//              second copy of the AI's world pointer, set by 0x58fa7b),
//              except that if the tile is blocked (bit 0 of byte +0xa) it
//              walks to x+1, x+2, ... until a free tile or x = map size, and
//              takes that tile's area (at x = map size, the tile just past
//              the row, as the original reads it). EAX = the word.

#include "game/ai_layout.h"
#include "game/layout.h"
#include "game/raw.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// The 6-byte record AI_CopySixBytes copies (a dword and a word).
#pragma pack(push, 1)
struct SixByteRecord {
    std::uint32_t dword0;
    std::uint16_t word4;
};
#pragma pack(pop)
static_assert(sizeof(SixByteRecord) == 6);

}  // namespace

using SetCurrentPlayerFn = std::uint32_t(__fastcall*)(void* player);
using BuildingAreaFn = std::uint32_t(__fastcall*)(const std::uint16_t* building);
using FreeTileAreaFn = std::uint32_t(__stdcall*)(std::int32_t x, std::int32_t y);
std::uint32_t __fastcall AI_BuildingArea(const std::uint16_t* building);
std::uint32_t __stdcall AI_FreeTileArea(std::int32_t x, std::int32_t y);
using WordListFn = std::uint32_t(__stdcall*)(std::uint32_t value, std::uint16_t* list, std::uint32_t count);
using IsOnMapFn = bool(__fastcall*)(std::int32_t v);
using ClampToMapFn = std::uint32_t(__fastcall*)(std::int32_t* x, std::int32_t* y);
using AreaGoodsAmountFn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
using GetBuildingFn = void*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
using RecordBestCandidateFn = std::uint32_t(__fastcall*)(std::uint32_t index, std::uint32_t score, std::uint32_t a,
                                                         std::uint32_t b);
using GridCellFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using GridFreeFn = void(__fastcall*)(void* self, void* edx);
using CopySixBytesFn = void(__fastcall*)(void* dst, const void* src);
using SetWord4Fn = std::uint16_t(__fastcall*)(void* self, void* edx, std::uint32_t value);

std::uint32_t __fastcall AI_SetCurrentPlayer(void* player);
std::uint32_t __stdcall AI_WordListInsert(std::uint32_t value, std::uint16_t* list, std::uint32_t count);
std::uint32_t __stdcall AI_WordListRemove(std::uint32_t value, std::uint16_t* list, std::uint32_t count);
bool __fastcall AI_IsOnMap(std::int32_t v);
std::uint32_t __fastcall AI_ClampToMap(std::int32_t* x, std::int32_t* y);
std::uint32_t __fastcall AI_AreaGoodsAmount(const std::uint8_t* goods, const std::uint16_t* area);
void* __fastcall AI_GetBuilding(std::uint32_t id, std::uint32_t* error);
std::uint32_t __fastcall AI_RecordBestCandidate(std::uint32_t index, std::uint32_t score, std::uint32_t a,
                                                std::uint32_t b);
void* __fastcall AI_GridCell(void* self, void* edx, std::uint32_t x, std::uint32_t y);
void __fastcall AI_GridFree(void* self, void* edx);
void __fastcall AI_CopySixBytes(void* dst, const void* src);
std::uint16_t __fastcall AI_SetWord4(void* self, void* edx, std::uint32_t value);

S3RE_HOOK(0x00551a30, AI_SetCurrentPlayer, SetCurrentPlayerFn, Original_AI_SetCurrentPlayer, AI_SetCurrentPlayer);

// Purpose: Makes a computer player the current one before it takes its turn:
// the AI code finds "its" player number, settings and working data through
// globals.
std::uint32_t __fastcall AI_SetCurrentPlayer(void* player) {
    const auto* computer = static_cast<const ComputerPlayer*>(player);
    Global<void*>(kAiPlayer) = player;
    Global<std::uint8_t>(kAiPlayerNumber) = computer->player_number;
    Global<std::uint32_t>(kAiPlayerSetup) =
        computer->setup_index * kPlayerSetupBytes + static_cast<std::uint32_t>(kPlayerSetupTable);
    const std::uint32_t data = computer->data;
    Global<std::uint32_t>(kAiPlayerData) = data;
    return data;
}

S3RE_HOOK(0x00551970, AI_WordListInsert, WordListFn, Original_AI_WordListInsert, AI_WordListInsert);

// Purpose: Adds a number (probably a building or settler id) to a computer
// player's fixed-size list, in the first free (0xffff) slot.
std::uint32_t __stdcall AI_WordListInsert(std::uint32_t value, std::uint16_t* list, std::uint32_t count) {
    const auto n = static_cast<std::uint16_t>(count);
    for (std::uint16_t i = 0; i < n; ++i) {
        if (list[i] == 0xffff) {
            list[i] = static_cast<std::uint16_t>(value);
            return 1;
        }
    }
    return 0;
}

S3RE_HOOK(0x005519b0, AI_WordListRemove, WordListFn, Original_AI_WordListRemove, AI_WordListRemove);

// Purpose: Removes a number from such a list, keeping the remaining entries
// packed at the front.
std::uint32_t __stdcall AI_WordListRemove(std::uint32_t value, std::uint16_t* list, std::uint32_t count) {
    const auto n = static_cast<std::uint16_t>(count);
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::uint16_t entry = list[i];
        if (entry == static_cast<std::uint16_t>(value)) {
            for (std::int32_t j = i; j < static_cast<std::int32_t>(n) - 1; ++j) {
                list[j] = list[j + 1];
            }
            list[n - 1] = 0xffff;
            return 1;
        }
        if (entry == 0xffff) {
            return 0;
        }
    }
    return 0;
}

S3RE_HOOK(0x00572090, AI_IsOnMap, IsOnMapFn, Original_AI_IsOnMap, AI_IsOnMap);

// Purpose: Whether a coordinate lies on the map, for the computer players.
bool __fastcall AI_IsOnMap(std::int32_t v) {
    return v >= 0 && v < static_cast<std::int32_t>(AiWorld()->map_size);
}

S3RE_HOOK(0x00572050, AI_ClampToMap, ClampToMapFn, Original_AI_ClampToMap, AI_ClampToMap);

// Purpose: Pulls a position a computer player computed back onto the map.
std::uint32_t __fastcall AI_ClampToMap(std::int32_t* x, std::int32_t* y) {
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
    const auto size = static_cast<std::int32_t>(AiWorld()->map_size);
    if (*x > size) {
        *x = size;
    }
    const auto size2 = static_cast<std::int32_t>(AiWorld()->map_size);
    if (*y > size2) {
        *y = size2;
    }
    return static_cast<std::uint32_t>(size2);
}

S3RE_HOOK(0x0059dc10, AI_AreaGoodsAmount, AreaGoodsAmountFn, Original_AI_AreaGoodsAmount, AI_AreaGoodsAmount);

// Purpose: How many units of a goods type lie on the piles of an economy
// area, as the computer players see it.
std::uint32_t __fastcall AI_AreaGoodsAmount(const std::uint8_t* goods, const std::uint16_t* area) {
    GameWorld* world = AiWorld();
    // Neither the area (word) nor the goods type (byte) is range-checked:
    // indexes past the arrays read what lies there, as the original does.
    std::uint16_t pile = world->areas[*area].piles_by_goods[*goods];
    std::uint32_t sum = 0;
    while (pile != 0) {
        const layout::Pile& record = world->piles[pile];
        sum += record.amount;
        pile = record.next;
    }
    return sum & 0xffff;
}

S3RE_HOOK(0x0058e170, AI_GetBuilding, GetBuildingFn, Original_AI_GetBuilding, AI_GetBuilding);

// Purpose: Looks up a building record by number for the computer players;
// null (with an error flag for a bad number) if there is no such building.
void* __fastcall AI_GetBuilding(std::uint32_t id, std::uint32_t* error) {
    *error = 0;
    GameWorld* world = AiWorld();
    const std::uint32_t index = id & 0xffff;
    if (index > world->building_high_water) {
        *error = 1;
        return nullptr;
    }
    layout::Building* building = &world->buildings[index];
    return building->owner != 0xff ? building : nullptr;
}

S3RE_HOOK(0x0058ec50, AI_RecordBestCandidate, RecordBestCandidateFn, Original_AI_RecordBestCandidate,
          AI_RecordBestCandidate);

// Purpose: Probably remembers, per building type, the best place a computer
// player has found so far (score and position), after resetting a per-type
// value to its default.
std::uint32_t __fastcall AI_RecordBestCandidate(std::uint32_t index, std::uint32_t score, std::uint32_t a,
                                                std::uint32_t b) {
    const std::uint32_t i = index & 0xffff;
    auto* data = Global<AiPlayerData*>(kAiPlayerData);
    // (The index is not range-checked: the arrays are indexed past their
    // declared ends like the original's address arithmetic.)
    data->candidate_values[i] = reinterpret_cast<const std::uint16_t*>(kCandidateDefaults)[i];
    AiCandidate& candidate = data->best[i];
    if (candidate.score >= static_cast<std::uint16_t>(score)) {
        return i * 6;
    }
    candidate.score = static_cast<std::uint16_t>(score);
    candidate.a = static_cast<std::uint16_t>(a);
    candidate.b = static_cast<std::uint16_t>(b);  // the original addresses it as (i * 3 + 0x15) * 2
    return i * 3 + 0x15;
}

S3RE_HOOK(0x00554700, AI_GridCell, GridCellFn, Original_AI_GridCell, AI_GridCell);

// Purpose: The cell at (x, y) of a computer player's map evaluation grid
// (Cevaland, "evaluate land").
void* __fastcall AI_GridCell(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    const std::uint32_t index = y * evaluation->width + x;
    // 32-bit address arithmetic (wraps like the original's for any x, y).
    return reinterpret_cast<void*>(reinterpret_cast<std::uint32_t>(evaluation->cells) +
                                   index * static_cast<std::uint32_t>(sizeof(AiGridCell)));
}

S3RE_HOOK(0x00554040, AI_GridFree, GridFreeFn, Original_AI_GridFree, AI_GridFree);

// Purpose: Frees a computer player's map evaluation grid.
void __fastcall AI_GridFree(void* self, void* /*edx*/) {
    AiGridCell*& grid = static_cast<AiLandEvaluation*>(self)->cells;
    if (grid != nullptr) {
        reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(grid);
        grid = nullptr;
    }
}

S3RE_HOOK(0x0055a420, AI_CopySixBytes, CopySixBytesFn, Original_AI_CopySixBytes, AI_CopySixBytes);

// Purpose: Copies a small 6-byte record into place (probably when a
// container of the computer player grows).
void __fastcall AI_CopySixBytes(void* dst, const void* src) {
    if (dst != nullptr) {
        auto* to = static_cast<SixByteRecord*>(dst);
        const auto* from = static_cast<const SixByteRecord*>(src);
        to->dword0 = from->dword0;
        to->word4 = from->word4;
    }
}

S3RE_HOOK(0x0055ca60, AI_SetWord4, SetWord4Fn, Original_AI_SetWord4, AI_SetWord4);

// Purpose: Sets a 16-bit field of a computer player's record (meaning not
// known yet).
std::uint16_t __fastcall AI_SetWord4(void* self, void* /*edx*/, std::uint32_t value) {
    // The only known field of this record (layout otherwise unknown).
    RawAt<std::uint16_t>(self, 4) = static_cast<std::uint16_t>(value);
    return static_cast<std::uint16_t>(value);
}

S3RE_HOOK(0x00599610, AI_BuildingArea, BuildingAreaFn, Original_AI_BuildingArea, AI_BuildingArea);

// Purpose: The economy area a building stands in, for the computer players
// (0xffff for a position off the map).
std::uint32_t __fastcall AI_BuildingArea(const std::uint16_t* building) {
    GameWorld* world = AiWorld();
    const std::uint32_t id = *building;
    const layout::Building& record = world->buildings[id];
    const std::uint16_t x = record.door_x;
    const std::uint16_t y = record.door_y;
    if (x >= Global<std::uint16_t>(kAiMapSizeX) || y >= Global<std::uint16_t>(kAiMapSizeY)) {
        return ((id * 29) & 0xffff0000u) | 0xffffu;  // `or ax, 0FFFFh` on 29 * id
    }
    // The original indexes [world + (tile + 0x44d9) * 24], which is the
    // tile's area word (0x44d9 * 24 = tiles + Tile::area); the upper half of
    // EAX keeps that index's.
    const std::uint32_t index = x * 0x300u + y + 0x44d9u;
    static_assert(0x44d9 * kTileBytes == offsetof(GameWorld, tiles) + offsetof(layout::Tile, area), "tile area word");
    return (index & 0xffff0000u) | TileAtOffset(world, TileOffset(x, y)).area;
}

S3RE_HOOK(0x0058b480, AI_FreeTileArea, FreeTileAreaFn, Original_AI_FreeTileArea, AI_FreeTileArea);

// Purpose: The economy area at a map position for the computer players,
// skipping blocked tiles in the +x direction; -1 off the map.
std::uint32_t __stdcall AI_FreeTileArea(std::int32_t x, std::int32_t y) {
    if (!AI_IsOnMap(x) || !AI_IsOnMap(y)) {
        return 0xffffffffu;
    }
    GameWorld* world = Global<GameWorld*>(kAiMapWorld);
    // At x = map size the tile is the one just past the row (32-bit offset
    // arithmetic, as the original reads it).
    auto tile = [&](std::int32_t tx) -> layout::Tile& {
        return TileAtOffset(world, TileOffset(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(y)));
    };
    if ((tile(x).flags & layout::kTileBlocked) != 0) {
        const auto size = static_cast<std::int32_t>(world->map_size);
        do {
            ++x;
        } while (x < size && (tile(x).flags & layout::kTileBlocked) != 0);
    }
    return tile(x).area;
}

// ---------------------------------------------------------------------------
// Batch 25
// ---------------------------------------------------------------------------
//
//   0x0059dac0 AI_CountBuildings(player, type, area) fastcall (CL, DL, one
//              stack word; ret 4): walks building ids 1, 2, ... through
//              AI_GetBuilding until it reports an error; counts buildings
//              of that player (byte +0) and type (byte +6) whose byte +0x21
//              is 0, if their area (AI_BuildingArea) equals `area` or
//              `area` is not 0 (sic: a non-zero area counts every such
//              building). AX = the count; the upper half of EAX is the error
//              flag's (0).
//   0x005996c0 AI_AreaAlongRow(x, y) fastcall (CX, DX): 0xffff off the AI
//              map (16-bit compares with 0x7e3414 / 0x7e3412); else the area
//              word of tile (x, y), or, while that is 0, of x+1, x+2, ... (at
//              most 16 steps, and only while x+k < map size x - 1). Only AX is
//              used by callers; the upper half is reproduced (the last tile
//              index * 3, or 0 after stopping at the edge) except off the
//              map, where the original keeps its caller's.
//   0x00551910 AI_List32Add(value) thiscall, ret 4: AI_WordListInsert(value,
//              this+0x200, 50); on success ++word this+0x264. EAX = the
//              insert's result.
//   0x00554730 AI_GridCellWrapped(x, y) thiscall, ret 8: like AI_GridCell
//              with x mod width (+0x140) and y mod height (+0x144).
//   0x00554530 AI_GridSumAround(x, y) thiscall, ret 8: in cell (x, y) (low
//              words) clears byte +0xf, words +0x10..+0x1c and bytes
//              +0x1e..+0x20, then adds the 3x3 cells around it (16-bit
//              coordinates x-1..x+1, y-1..y+1 that are on the grid): byte +4
//              into byte +0xf, bytes +3 and +5..+0xa into the words; bytes
//              +0xb/+0xc/+0xd are summed and stored as sum * 100 / 576 into
//              bytes +0x1e/+0x1f/+0x20 (via the multiply 0x38e38e39 >> 39).
//              EAX = the low half of the last multiplication.
//   0x0055b770 AI_ResetAgentData() thiscall: zeroes a computer player's
//              working data (dwords +0x18e4..+0x1ad4, +0x714 (0x474 dwords),
//              +0x444 (0xb4), five arrays of 20 dwords at +0x2b4/+0x304/
//              +0x354/+0x3a4/+0x3f4, bytes +0, +1, +2, +0x2ac, dwords +0x25c..
//              (20), 20x10 words at +4 and 20x10 bytes at +0x194) and empties
//              the 20 vectors at 0x7e7a24 (end = begin, 16-byte entries).
//              EAX = 10.
//   0x00553e00 AI_ClearGlobals7e0568(): 0x7e0568 = 0x7e056c = 0; EAX = 0.
//   0x00559260 AI_ListFirst(out) thiscall, ret 4: *out = **(this+4) (the
//              first node after the list head); EAX = out.

using CountBuildingsFn = std::uint32_t(__fastcall*)(std::uint32_t player, std::uint32_t type, std::uint32_t area);
using AreaAlongRowFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
using List32AddFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using GridSumFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using AgentResetFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ClearGlobalsFn = std::uint32_t(__cdecl*)();
using ListFirstFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t* out);

std::uint32_t __fastcall AI_CountBuildings(std::uint32_t player, std::uint32_t type, std::uint32_t area);
std::uint32_t __fastcall AI_AreaAlongRow(std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_List32Add(void* self, void* edx, std::uint32_t value);
void* __fastcall AI_GridCellWrapped(void* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_GridSumAround(void* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_ResetAgentData(void* self, void* edx);
std::uint32_t __cdecl AI_ClearGlobals7e0568();
void* __fastcall AI_ListFirst(void* self, void* edx, std::uint32_t* out);

S3RE_HOOK(0x0059dac0, AI_CountBuildings, CountBuildingsFn, Original_AI_CountBuildings, AI_CountBuildings);

// Purpose: Counts a computer player's buildings of one type (probably "how
// many sawmills do I have"), optionally restricted to an economy area.
std::uint32_t __fastcall AI_CountBuildings(std::uint32_t player, std::uint32_t type, std::uint32_t area) {
    std::uint32_t count = 0;
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const auto* building = static_cast<const layout::Building*>(AI_GetBuilding(id, &error));
        if (building != nullptr && building->owner == static_cast<std::uint8_t>(player) && building->buildup == 0 &&
            building->art == static_cast<std::uint8_t>(type)) {
            const std::uint16_t number = static_cast<std::uint16_t>(id);
            const auto found = static_cast<std::uint16_t>(AI_BuildingArea(&number));
            if (static_cast<std::uint16_t>(area) == found || static_cast<std::uint16_t>(area) != 0) {
                ++count;
            }
        }
    } while (error == 0);
    return (error & 0xffff0000u) | (count & 0xffffu);
}

S3RE_HOOK(0x005996c0, AI_AreaAlongRow, AreaAlongRowFn, Original_AI_AreaAlongRow, AI_AreaAlongRow);

// Purpose: The economy area at a map position for the computer players; on
// a tile without an area it looks a few tiles further in +x (0xffff off the
// map).
std::uint32_t __fastcall AI_AreaAlongRow(std::uint32_t x, std::uint32_t y) {
    const std::uint16_t sizeX = Global<std::uint16_t>(kAiMapSizeX);
    if (static_cast<std::uint16_t>(x) >= sizeX || static_cast<std::uint16_t>(y) >= Global<std::uint16_t>(kAiMapSizeY)) {
        return 0xffff;
    }
    GameWorld* world = AiWorld();
    y &= 0xffff;
    // The original's register holds (tile index + 0x44d9) * 3; times 8 it is
    // the offset of the tile's area word (see AI_BuildingArea).
    const auto index3 = [&](std::uint32_t tx) { return ((tx & 0xffff) * 0x300 + y + 0x44d9) * 3; };
    const auto area = [&](std::uint32_t tx) { return TileAtOffset(world, TileOffset(tx & 0xffff, y)).area; };
    std::uint32_t upper = index3(x);
    if (area(x) == 0) {
        for (int steps = 0; steps < 16; ++steps) {
            ++x;
            if (static_cast<std::int32_t>(x & 0xffff) >= static_cast<std::int32_t>(sizeX) - 1) {
                upper = 0;
                break;
            }
            upper = index3(x);
            if (area(x) != 0) {
                break;
            }
        }
    }
    return (upper & 0xffff0000u) | area(x);
}

S3RE_HOOK(0x00551910, AI_List32Add, List32AddFn, Original_AI_List32Add, AI_List32Add);

// Purpose: Adds a number to a computer player's list of up to 50 entries
// (+0x200) and counts it (+0x264); probably a list of buildings or orders.
std::uint32_t __fastcall AI_List32Add(void* self, void* /*edx*/, std::uint32_t value) {
    auto* owner = static_cast<AiList50Owner*>(self);
    const std::uint32_t added = AI_WordListInsert(value, owner->entries, 50);
    if (added != 0) {
        ++owner->count;
    }
    return added;
}

S3RE_HOOK(0x00554730, AI_GridCellWrapped, GridCellFn, Original_AI_GridCellWrapped, AI_GridCellWrapped);

// Purpose: A cell of a computer player's map evaluation grid, with the
// coordinates wrapped around the grid size.
void* __fastcall AI_GridCellWrapped(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    const std::uint32_t width = evaluation->width;
    const std::uint32_t row = y % evaluation->height;
    const std::uint32_t column = x % width;
    return reinterpret_cast<std::uint8_t*>(evaluation->cells) + (row * width + column) * sizeof(AiGridCell);
}

S3RE_HOOK(0x00554530, AI_GridSumAround, GridSumFn, Original_AI_GridSumAround, AI_GridSumAround);

// Purpose: Sums the evaluation values of the 3x3 grid cells around a cell
// into it (probably how good the surroundings of a place are for a
// building), three of them scaled to percentages.
std::uint32_t __fastcall AI_GridSumAround(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    auto* cell = static_cast<AiGridCell*>(AI_GridCell(self, nullptr, x & 0xffff, y & 0xffff));
    cell->sum0f = 0;
    cell->sum10 = 0;
    for (std::uint16_t& sum : cell->sums12) {
        sum = 0;
    }
    cell->percent1e = 0;
    cell->percent1f = 0;
    cell->percent20 = 0;
    std::uint32_t sumB = 0, sumC = 0, sumD = 0;
    for (std::uint32_t i = 0; i < 3; ++i) {
        for (std::uint32_t j = 0; j < 3; ++j) {
            const auto cx = static_cast<std::int16_t>(i + x - 1);
            const auto cy = static_cast<std::int16_t>(j + y - 1);
            if (cx < 0 || static_cast<std::uint32_t>(static_cast<std::int32_t>(cx)) >= evaluation->width ||
                cy < 0 || static_cast<std::uint32_t>(static_cast<std::int32_t>(cy)) >= evaluation->height) {
                continue;
            }
            const auto* other = static_cast<const AiGridCell*>(
                AI_GridCell(self, nullptr, static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)));
            cell->sum0f = static_cast<std::uint8_t>(cell->sum0f + other->value04);
            cell->sum10 = static_cast<std::uint16_t>(cell->sum10 + other->value03);
            for (std::size_t k = 0; k < 6; ++k) {
                cell->sums12[k] = static_cast<std::uint16_t>(cell->sums12[k] + other->values05[k]);
            }
            sumB += other->value0b;
            sumC += other->value0c;
            sumD += other->value0d;
        }
    }
    std::uint32_t low = 0;
    const auto percent = [&low](std::uint32_t sum) {
        const std::uint64_t product = static_cast<std::uint64_t>(sum * 100u) * 0x38e38e39u;
        low = static_cast<std::uint32_t>(product);
        return static_cast<std::uint8_t>((product >> 32) >> 7);
    };
    cell->percent1e = percent(sumB);
    cell->percent1f = percent(sumC);
    cell->percent20 = percent(sumD);
    return low;
}

S3RE_HOOK(0x0055b770, AI_ResetAgentData, AgentResetFn, Original_AI_ResetAgentData, AI_ResetAgentData);

// Purpose: Clears a computer player's working data (probably at the start
// of a game or when it is created), and empties 20 shared vectors.
std::uint32_t __fastcall AI_ResetAgentData(void* self, void* /*edx*/) {
    auto* data = static_cast<AiAgentData*>(self);
    for (std::uint32_t& value : data->dwords18e4) {
        value = 0;
    }
    std::memset(data->dwords714, 0, sizeof(data->dwords714));
    data->unk001 = 0;
    std::memset(data->dwords444, 0, sizeof(data->dwords444));
    for (std::uint32_t* array : {data->dwords3a4, data->dwords354, data->dwords2b4, data->dwords304, data->dwords3f4}) {
        std::memset(array, 0, sizeof(data->dwords3a4));
    }
    AiVector* vectors = AgentVectors();
    for (std::size_t k = 0; k < kAgentVectorCount; ++k) {
        vectors[k].end = vectors[k].begin;
    }
    data->unk000 = 0;
    data->unk002 = 0;
    data->unk2ac = 0;
    for (std::size_t k = 0; k < 20; ++k) {
        data->dwords25c[k] = 0;
        data->dwords2b4[k] = 0;
        data->dwords304[k] = 0;
        data->dwords354[k] = 0;
        data->dwords3f4[k] = 0;
        for (std::size_t j = 0; j < 10; ++j) {
            data->words004[k][j] = 0;
            data->bytes194[k][j] = 0;
        }
    }
    return 10;
}

S3RE_HOOK(0x00553e00, AI_ClearGlobals7e0568, ClearGlobalsFn, Original_AI_ClearGlobals7e0568, AI_ClearGlobals7e0568);

// Purpose: Resets two AI globals (0x7e0568, 0x7e056c; meaning unknown)
// when a computer player's turn is set up.
std::uint32_t __cdecl AI_ClearGlobals7e0568() {
    Global<std::uint32_t>(kEvaLandRefCount) = 0;
    Global<std::uint32_t>(kEvaLandInstance) = 0;
    return 0;
}

S3RE_HOOK(0x00559260, AI_ListFirst, ListFirstFn, Original_AI_ListFirst, AI_ListFirst);

// Purpose: The first entry of a linked list (the node after the head at
// this+4), as an iterator written to `out`.
void* __fastcall AI_ListFirst(void* self, void* /*edx*/, std::uint32_t* out) {
    *out = static_cast<const AiList*>(self)->head->next;
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct AiFunctions {
    SetCurrentPlayerFn setCurrentPlayer;
    WordListFn insert;
    WordListFn remove;
    IsOnMapFn isOnMap;
    ClampToMapFn clamp;
    AreaGoodsAmountFn areaGoods;
    GetBuildingFn getBuilding;
    RecordBestCandidateFn recordBest;
    GridCellFn gridCell;
    GridFreeFn gridFree;
    CopySixBytesFn copySix;
    SetWord4Fn setWord4;
    BuildingAreaFn buildingArea;
    FreeTileAreaFn freeTileArea;
};

AiFunctions OriginalAi() {
    return {Original_AI_SetCurrentPlayer, Original_AI_WordListInsert, Original_AI_WordListRemove,
            Original_AI_IsOnMap,          Original_AI_ClampToMap,     Original_AI_AreaGoodsAmount,
            Original_AI_GetBuilding,      Original_AI_RecordBestCandidate, Original_AI_GridCell,
            Original_AI_GridFree,         Original_AI_CopySixBytes,   Original_AI_SetWord4,
            Original_AI_BuildingArea,     Original_AI_FreeTileArea};
}

AiFunctions OurAi() {
    return {&AI_SetCurrentPlayer, &AI_WordListInsert, &AI_WordListRemove, &AI_IsOnMap,
            &AI_ClampToMap,       &AI_AreaGoodsAmount, &AI_GetBuilding,   &AI_RecordBestCandidate,
            &AI_GridCell,         &AI_GridFree,       &AI_CopySixBytes,  &AI_SetWord4,
            &AI_BuildingArea,     &AI_FreeTileArea};
}

// The globals the functions read or write, saved and restored around a test.
struct SavedGlobals {
    std::uint32_t values[5];
    static constexpr std::uintptr_t kAddresses[5] = {kAiWorld, kAiPlayer, kAiPlayerSetup, kAiPlayerData,
                                                     kAiPlayerNumber};
    SavedGlobals() {
        for (int i = 0; i < 4; ++i) {
            values[i] = Global<std::uint32_t>(kAddresses[i]);
        }
        values[4] = Global<std::uint8_t>(kAiPlayerNumber);
    }
    ~SavedGlobals() {
        for (int i = 0; i < 4; ++i) {
            Global<std::uint32_t>(kAddresses[i]) = values[i];
        }
        Global<std::uint8_t>(kAiPlayerNumber) = static_cast<std::uint8_t>(values[4]);
    }
};

std::uint16_t RandomListValue(test::Rng& rng) {
    return rng.Chance(1, 3) ? 0xffff : static_cast<std::uint16_t>(rng.Below(6));
}

// A list of `length` distinct random piles for (area, goods), the same in
// both worlds; the head is 0 for an empty list.
void BuildPileList(test::Context& ctx, test::WorldPair& worlds, std::uint32_t area, std::uint32_t goods,
                   int length) {
    // Distinct piles (a repeated one would close a cycle): consecutive
    // ranges of a random stride.
    std::uint16_t next = 0;
    const std::uint32_t start = 1 + ctx.rng().Below(20000);
    const std::uint32_t stride = 1 + ctx.rng().Below(1000);
    for (int k = 0; k < length; ++k) {
        const auto pile = static_cast<std::uint16_t>(start + static_cast<std::uint32_t>(k) * stride);
        worlds.Both([&](GameWorld* w) { w->piles[pile].next = next; });
        next = pile;
    }
    worlds.Both([&](GameWorld* w) { w->areas[area].piles_by_goods[goods] = next; });
}

void CompareAi(test::Context& ctx, const AiFunctions& candidate, int cases) {
    const AiFunctions original = OriginalAi();
    SavedGlobals saved;
    const std::uint32_t savedMapSizes = Global<std::uint32_t>(kAiMapSizeY);  // both words
    const std::uint32_t savedMapWorld = Global<std::uint32_t>(kAiMapWorld);
    struct Restore {
        std::uint32_t sizes, world;
        ~Restore() {
            Global<std::uint32_t>(kAiMapSizeY) = sizes;
            Global<std::uint32_t>(kAiMapWorld) = world;
        }
    } restore{savedMapSizes, savedMapWorld};
    test::WorldPair worlds(ctx, 0x153a128);
    std::vector<std::uint8_t> dataA(0x2000), dataB(0x2000);
    ctx.rng().Fill(dataA.data(), dataA.size());
    dataB = dataA;
    auto useWorld = [&](bool ours) {
        Global<void*>(kAiWorld) = ours ? static_cast<void*>(worlds.ours()) : static_cast<void*>(worlds.original());
        Global<void*>(kAiPlayerData) = ours ? dataB.data() : dataA.data();
        Global<void*>(kAiMapWorld) = ours ? static_cast<void*>(worlds.ours()) : static_cast<void*>(worlds.original());
    };
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(14));
        bool same = true;
        switch (op) {
            case 0: {
                std::uint8_t player[0x780];
                ctx.rng().Fill(player, sizeof(player));
                reinterpret_cast<ComputerPlayer*>(player)->setup_index = ctx.rng().Below(64);
                const std::uint32_t ra = original.setCurrentPlayer(player);
                const std::uint32_t ga[4] = {Global<std::uint32_t>(kAiPlayer), Global<std::uint8_t>(kAiPlayerNumber),
                                             Global<std::uint32_t>(kAiPlayerSetup), Global<std::uint32_t>(kAiPlayerData)};
                Global<std::uint32_t>(kAiPlayer) = 0;
                Global<std::uint8_t>(kAiPlayerNumber) = 0;
                Global<std::uint32_t>(kAiPlayerSetup) = 0;
                Global<std::uint32_t>(kAiPlayerData) = 0;
                const std::uint32_t rb = candidate.setCurrentPlayer(player);
                same &= S3RE_EXPECT_EQ(ra, rb);
                same &= S3RE_EXPECT_EQ(ga[0], Global<std::uint32_t>(kAiPlayer));
                same &= S3RE_EXPECT_EQ(ga[1], static_cast<std::uint32_t>(Global<std::uint8_t>(kAiPlayerNumber)));
                same &= S3RE_EXPECT_EQ(ga[2], Global<std::uint32_t>(kAiPlayerSetup));
                same &= S3RE_EXPECT_EQ(ga[3], Global<std::uint32_t>(kAiPlayerData));
                break;
            }
            case 1:
            case 2: {
                std::uint16_t la[12], lb[12];
                for (auto& v : la) {
                    v = RandomListValue(ctx.rng());
                }
                std::memcpy(lb, la, sizeof(la));
                const std::uint32_t count = ctx.rng().Chance(1, 8) ? (ctx.rng().Next32() & 0xffff0000u) | ctx.rng().Below(11)
                                                                   : ctx.rng().Below(11);
                const std::uint32_t value = (ctx.rng().Next32() & 0xffff0000u) | RandomListValue(ctx.rng());
                const WordListFn fa = op == 1 ? original.insert : original.remove;
                const WordListFn fb = op == 1 ? candidate.insert : candidate.remove;
                same &= S3RE_EXPECT_EQ(fa(value, la, count), fb(value, lb, count));
                same &= S3RE_EXPECT_MEM_EQ(la, lb, sizeof(la));
                break;
            }
            case 3: {
                const std::int32_t size = ctx.rng().Range(-4, 1024);
                worlds.Both([&](GameWorld* w) { w->map_size = static_cast<std::uint32_t>(size); });
                const std::int32_t v = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                              : ctx.rng().Range(-3, 1030);
                useWorld(false);
                const bool ra = original.isOnMap(v);
                useWorld(true);
                const bool rb = candidate.isOnMap(v);
                same &= S3RE_EXPECT_EQ(ra, rb);
                break;
            }
            case 4: {
                const std::int32_t size = ctx.rng().Range(-4, 1024);
                worlds.Both([&](GameWorld* w) { w->map_size = static_cast<std::uint32_t>(size); });
                std::int32_t xa = ctx.rng().Range(-50, 1100), ya = ctx.rng().Range(-50, 1100);
                if (ctx.rng().Chance(1, 8)) {
                    xa = static_cast<std::int32_t>(ctx.rng().Next32());
                }
                std::int32_t xb = xa, yb = ya;
                useWorld(false);
                const std::uint32_t ra = original.clamp(&xa, &ya);
                useWorld(true);
                const std::uint32_t rb = candidate.clamp(&xb, &yb);
                same &= S3RE_EXPECT_EQ(ra, rb);
                same &= S3RE_EXPECT_EQ(xa, xb);
                same &= S3RE_EXPECT_EQ(ya, yb);
                break;
            }
            case 5: {
                const std::uint16_t area = static_cast<std::uint16_t>(ctx.rng().Below(4096));
                const std::uint8_t goods = static_cast<std::uint8_t>(ctx.rng().Below(256));
                BuildPileList(ctx, worlds, area, goods, static_cast<int>(ctx.rng().Below(8)));
                useWorld(false);
                const std::uint32_t ra = original.areaGoods(&goods, &area);
                useWorld(true);
                const std::uint32_t rb = candidate.areaGoods(&goods, &area);
                same &= S3RE_EXPECT_EQ(ra, rb);
                break;
            }
            case 6: {
                const std::uint32_t count = ctx.rng().Below(3000);
                worlds.Both([&](GameWorld* w) { w->building_high_water = count; });
                const std::uint32_t id = (ctx.rng().Next32() & 0xffff0000u) | ctx.rng().Below(3200);
                if (ctx.rng().Chance(1, 3)) {
                    worlds.Both([&](GameWorld* w) { w->buildings[id & 0xffff].owner = 0xff; });
                }
                std::uint32_t ea = 7, eb = 7;
                useWorld(false);
                auto* ra = static_cast<std::uint8_t*>(original.getBuilding(id, &ea));
                useWorld(true);
                auto* rb = static_cast<std::uint8_t*>(candidate.getBuilding(id, &eb));
                same &= S3RE_EXPECT_EQ(ra == nullptr, rb == nullptr);
                if (ra != nullptr && rb != nullptr) {
                    same &= S3RE_EXPECT_EQ(ra - worlds.originalBytes(), rb - worlds.oursBytes());
                }
                same &= S3RE_EXPECT_EQ(ea, eb);
                break;
            }
            case 7: {
                const std::uint32_t index = (ctx.rng().Next32() & 0xffff0000u) | ctx.rng().Below(0x300);
                const std::uint32_t score = ctx.rng().Next32();
                const std::uint32_t a = ctx.rng().Next32(), b = ctx.rng().Next32();
                if (ctx.rng().Chance(1, 2)) {
                    const auto low = static_cast<std::uint16_t>(score - ctx.rng().Below(3));
                    reinterpret_cast<AiPlayerData*>(dataA.data())->best[index & 0xffff].score = low;
                    reinterpret_cast<AiPlayerData*>(dataB.data())->best[index & 0xffff].score = low;
                }
                useWorld(false);
                const std::uint32_t ra = original.recordBest(index, score, a, b);
                useWorld(true);
                const std::uint32_t rb = candidate.recordBest(index, score, a, b);
                same &= S3RE_EXPECT_EQ(ra, rb);
                same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
                break;
            }
            case 8: {
                std::uint8_t object[0x160];
                ctx.rng().Fill(object, sizeof(object));
                const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
                same &= S3RE_EXPECT_EQ(original.gridCell(object, nullptr, x, y), candidate.gridCell(object, nullptr, x, y));
                break;
            }
            case 9: {
                std::uint8_t oa[0x160], ob[0x160];
                ctx.rng().Fill(oa, sizeof(oa));
                const bool empty = ctx.rng().Chance(1, 3);
                auto* newFn = reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9);
                static_assert(sizeof(oa) >= sizeof(AiLandEvaluation));
                auto& gridA = reinterpret_cast<AiLandEvaluation*>(oa)->cells;
                auto& gridB = reinterpret_cast<AiLandEvaluation*>(ob)->cells;
                gridA = empty ? nullptr : static_cast<AiGridCell*>(newFn(16));
                std::memcpy(ob, oa, sizeof(oa));
                gridB = empty ? nullptr : static_cast<AiGridCell*>(newFn(16));
                void* blockB = gridB;
                original.gridFree(oa, nullptr);
                candidate.gridFree(ob, nullptr);
                same &= S3RE_EXPECT_MEM_EQ(oa, ob, sizeof(oa));
                (void)blockB;
                break;
            }
            case 10: {
                std::uint8_t src[8], da[12], db[12];
                ctx.rng().Fill(src, sizeof(src));
                ctx.rng().Fill(da, sizeof(da));
                std::memcpy(db, da, sizeof(da));
                const bool null = ctx.rng().Chance(1, 4);
                original.copySix(null ? nullptr : da + 2, src);
                candidate.copySix(null ? nullptr : db + 2, src);
                same &= S3RE_EXPECT_MEM_EQ(da, db, sizeof(da));
                break;
            }
            case 12: {
                const std::uint16_t id = static_cast<std::uint16_t>(ctx.rng().Below(3200));
                auto coordinate = [&]() {
                    return static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(420));
                };
                const std::uint16_t doorX = coordinate();
                const std::uint16_t doorY = coordinate();
                worlds.Both([&](GameWorld* w) {
                    w->buildings[id].door_x = doorX;
                    w->buildings[id].door_y = doorY;
                });
                Global<std::uint16_t>(kAiMapSizeX) = static_cast<std::uint16_t>(ctx.rng().Below(420));
                Global<std::uint16_t>(kAiMapSizeY) = static_cast<std::uint16_t>(ctx.rng().Below(420));
                useWorld(false);
                const std::uint32_t ra = original.buildingArea(&id);
                useWorld(true);
                const std::uint32_t rb = candidate.buildingArea(&id);
                same &= S3RE_EXPECT_EQ(ra, rb);
                break;
            }
            case 13: {
                const std::int32_t size = ctx.rng().Range(1, 400);
                worlds.Both([&](GameWorld* w) { w->map_size = static_cast<std::uint32_t>(size); });
                const std::int32_t x = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                               : ctx.rng().Range(-2, size + 1);
                const std::int32_t y = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                               : ctx.rng().Range(-2, size + 1);
                if (x >= 0 && x < size && y >= 0 && y < size) {
                    // A run of blocked tiles from x on (often to the map edge).
                    const std::int32_t run = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Range(1, size - x + 1);
                    for (std::int32_t k = 0; k <= run && x + k <= size; ++k) {
                        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                        const auto value = static_cast<std::uint8_t>(k < run ? flags | layout::kTileBlocked
                                                                             : flags & ~layout::kTileBlocked);
                        worlds.Both([&](GameWorld* w) { w->tiles[x + k][y].flags = value; });
                    }
                }
                useWorld(false);
                const std::uint32_t ra = original.freeTileArea(x, y);
                useWorld(true);
                const std::uint32_t rb = candidate.freeTileArea(x, y);
                same &= S3RE_EXPECT_EQ(ra, rb);
                break;
            }
            default: {
                std::uint8_t oa[12], ob[12];
                ctx.rng().Fill(oa, sizeof(oa));
                std::memcpy(ob, oa, sizeof(oa));
                const std::uint32_t value = ctx.rng().Next32();
                same &= S3RE_EXPECT_EQ(original.setWord4(oa + 2, nullptr, value),
                                       candidate.setWord4(ob + 2, nullptr, value));
                same &= S3RE_EXPECT_MEM_EQ(oa, ob, sizeof(oa));
                break;
            }
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
    if (!worlds.Checkpoint(ctx)) {
        ctx.Fail(__FILE__, __LINE__, "  worlds differ at the end");
    }
}

// Wrong: the clamp allows size - 1 at most.
std::uint32_t __fastcall WrongClamp(std::int32_t* x, std::int32_t* y) {
    const std::uint32_t size = AI_ClampToMap(x, y);
    if (*x == static_cast<std::int32_t>(size) && size > 0) {
        *x -= 1;
    }
    return size;
}

}  // namespace

S3RE_TEST(AiHelpers_MatchOriginal) {
    CompareAi(ctx, OurAi(), 60000);
}

S3RE_TEST(AiHelpers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    AiFunctions wrong = OurAi();
    wrong.clamp = &WrongClamp;
    CompareAi(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

// Batch 25 functions: original and ours on identical copies of the state
// (two worlds for the AI world pointer, buffers for the objects, S3.EXE's
// globals saved, set and restored around each side).

namespace {

struct Ai25Functions {
    CountBuildingsFn countBuildings;
    AreaAlongRowFn areaAlongRow;
    List32AddFn list32Add;
    GridCellFn gridCellWrapped;
    GridSumFn gridSumAround;
    AgentResetFn resetAgentData;
    ClearGlobalsFn clearGlobals;
    ListFirstFn listFirst;
};

Ai25Functions OriginalAi25() {
    return {Original_AI_CountBuildings, Original_AI_AreaAlongRow,  Original_AI_List32Add,
            Original_AI_GridCellWrapped, Original_AI_GridSumAround, Original_AI_ResetAgentData,
            Original_AI_ClearGlobals7e0568, Original_AI_ListFirst};
}

Ai25Functions OurAi25() {
    return {&AI_CountBuildings, &AI_AreaAlongRow,  &AI_List32Add,     &AI_GridCellWrapped,
            &AI_GridSumAround,  &AI_ResetAgentData, &AI_ClearGlobals7e0568, &AI_ListFirst};
}

// S3.EXE memory the functions write: the two globals and the 20 vectors
// (plus the dword after them).
constexpr std::uintptr_t kGlobalsLow = kEvaLandRefCount;
static_assert(kEvaLandInstance == kGlobalsLow + 4);
constexpr std::uintptr_t kVectorsLow = kAgentVectors;
constexpr std::size_t kVectorsBytes = kAgentVectorCount * sizeof(AiVector) + 4;
static_assert(kVectorsBytes == 0x7e7b68 - 0x7e7a24);

void CompareAi25(test::Context& ctx, const Ai25Functions& candidate, int cases) {
    const Ai25Functions original = OriginalAi25();
    SavedGlobals saved;
    std::vector<std::uint8_t> savedVectors(kVectorsBytes), savedPair(8);
    std::memcpy(savedVectors.data(), reinterpret_cast<void*>(kVectorsLow), kVectorsBytes);
    std::memcpy(savedPair.data(), reinterpret_cast<void*>(kGlobalsLow), 8);
    const std::uint32_t savedMapSizes = Global<std::uint32_t>(kAiMapSizeY);
    test::WorldPair worlds(ctx, 0x153a128);
    std::vector<std::uint8_t> a(0x8000), b(0x8000), vectorsA(kVectorsBytes), pairA(8);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(8));
        ctx.rng().Fill(a.data(), a.size());
        ctx.rng().Fill(reinterpret_cast<void*>(kVectorsLow), kVectorsBytes);
        ctx.rng().Fill(reinterpret_cast<void*>(kGlobalsLow), 8);
        Global<std::uint16_t>(kAiMapSizeX) = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 1 + ctx.rng().Below(400));
        Global<std::uint16_t>(kAiMapSizeY) = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 1 + ctx.rng().Below(400));
        std::uint32_t args[3] = {ctx.rng().Next32(), ctx.rng().Next32(), ctx.rng().Next32()};
        switch (op) {
            case 0: {
                const std::uint32_t count = ctx.rng().Below(60);
                worlds.Both([&](GameWorld* w) { w->building_high_water = count; });
                args[0] = (args[0] & 0xffffff00u) | ctx.rng().Below(3);
                args[1] = (args[1] & 0xffffff00u) | ctx.rng().Below(3);
                args[2] = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(4);
                Global<std::uint16_t>(kAiMapSizeX) = 400;
                Global<std::uint16_t>(kAiMapSizeY) = 400;
                for (std::uint32_t id = 1; id <= count; ++id) {
                    const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(3));
                    const auto art = static_cast<std::uint8_t>(ctx.rng().Below(3));
                    const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4));
                    const auto x = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? 500 : ctx.rng().Below(400));
                    const auto y = static_cast<std::uint16_t>(ctx.rng().Below(400));
                    worlds.Both([&](GameWorld* w) {
                        layout::Building& record = w->buildings[id];
                        record.owner = owner;
                        record.art = art;
                        record.buildup = buildup;
                        record.door_x = x;
                        record.door_y = y;
                    });
                    if (x < 400) {
                        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(4));
                        worlds.Both([&](GameWorld* w) { w->tiles[x][y].area = area; });
                    }
                }
                break;
            }
            case 1: {
                Global<std::uint16_t>(kAiMapSizeX) = static_cast<std::uint16_t>(1 + ctx.rng().Below(400));
                Global<std::uint16_t>(kAiMapSizeY) = static_cast<std::uint16_t>(1 + ctx.rng().Below(400));
                const std::uint32_t x = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(410);
                const std::uint32_t y = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(410);
                args[0] = x;
                args[1] = y;
                for (std::uint32_t k = 0; k < 20; ++k) {
                    const std::uint32_t tx = (x + k) & 0xffff;
                    if (tx < 0x300 && (y & 0xffff) < 0x300) {
                        const auto area = static_cast<std::uint16_t>(ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Next32());
                        worlds.Both([&](GameWorld* w) { w->tiles[tx][y & 0xffff].area = area; });
                    }
                }
                break;
            }
            case 2:
                for (std::size_t k = 0; k < 50; ++k) {
                    reinterpret_cast<AiList50Owner*>(a.data())->entries[k] =
                        ctx.rng().Chance(1, 10) ? 0xffff : static_cast<std::uint16_t>(ctx.rng().Below(9));
                }
                args[0] = ctx.rng().Chance(1, 4) ? 0xffff : ctx.rng().Below(9);
                break;
            case 3:
            case 4: {
                const std::uint32_t w = 1 + ctx.rng().Below(8), h = 1 + ctx.rng().Below(8);
                reinterpret_cast<AiLandEvaluation*>(a.data())->width = w;
                reinterpret_cast<AiLandEvaluation*>(a.data())->height = h;
                if (op == 4) {
                    args[0] = (args[0] & 0xffff0000u) | ctx.rng().Below(w);
                    args[1] = (args[1] & 0xffff0000u) | ctx.rng().Below(h);
                }
                break;
            }
            default:
                break;
        }
        b = a;
        std::memcpy(vectorsA.data(), reinterpret_cast<void*>(kVectorsLow), kVectorsBytes);
        std::memcpy(pairA.data(), reinterpret_cast<void*>(kGlobalsLow), 8);
        auto run = [&](const Ai25Functions& f, std::uint8_t* object, bool ours) -> std::uint32_t {
            Global<void*>(kAiWorld) = ours ? static_cast<void*>(worlds.ours()) : static_cast<void*>(worlds.original());
            // The buffer is the land evaluation (op 3, 4) or a list (op 7).
            reinterpret_cast<AiLandEvaluation*>(object)->cells = reinterpret_cast<AiGridCell*>(object + 0x200);
            reinterpret_cast<AiList*>(object)->head = reinterpret_cast<AiListNode*>(object + 0x100);
            switch (op) {
                case 0: return f.countBuildings(args[0], args[1], args[2]);
                case 1: return f.areaAlongRow(args[0], args[1]) & 0xffff;
                case 2: return f.list32Add(object, nullptr, args[0]);
                case 3: return static_cast<std::uint32_t>(static_cast<std::uint8_t*>(f.gridCellWrapped(object, nullptr, args[0], args[1])) - object);
                case 4: return f.gridSumAround(object, nullptr, args[0], args[1]);
                case 5: return f.resetAgentData(object, nullptr);
                case 6: return f.clearGlobals();
                default: {
                    std::uint32_t out = 0;
                    void* r = f.listFirst(object, nullptr, &out);
                    return (r == &out ? 0x5a5a5a5au : 0) ^ out;  // the first node (the same dword on both sides)
                }
            }
        };
        const std::uint32_t ra = run(original, a.data(), false);
        std::vector<std::uint8_t> vectorsAfterA(kVectorsBytes), pairAfterA(8);
        std::memcpy(vectorsAfterA.data(), reinterpret_cast<void*>(kVectorsLow), kVectorsBytes);
        std::memcpy(pairAfterA.data(), reinterpret_cast<void*>(kGlobalsLow), 8);
        std::memcpy(reinterpret_cast<void*>(kVectorsLow), vectorsA.data(), kVectorsBytes);
        std::memcpy(reinterpret_cast<void*>(kGlobalsLow), pairA.data(), 8);
        const std::uint32_t rb = run(candidate, b.data(), true);
        // The pointers run() stores point into each side's own buffer.
        std::memcpy(b.data() + offsetof(AiLandEvaluation, cells), a.data() + offsetof(AiLandEvaluation, cells), 4);
        if (op != 5) {  // (ResetAgentData clears +4 itself)
            std::memcpy(b.data() + offsetof(AiList, head), a.data() + offsetof(AiList, head), 4);
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        same &= S3RE_EXPECT_MEM_EQ(vectorsAfterA.data(), reinterpret_cast<void*>(kVectorsLow), kVectorsBytes);
        same &= S3RE_EXPECT_MEM_EQ(pairAfterA.data(), reinterpret_cast<void*>(kGlobalsLow), 8);
        if (!same || ((i % 64 == 63) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    std::memcpy(reinterpret_cast<void*>(kVectorsLow), savedVectors.data(), kVectorsBytes);
    std::memcpy(reinterpret_cast<void*>(kGlobalsLow), savedPair.data(), 8);
    Global<std::uint32_t>(kAiMapSizeY) = savedMapSizes;
}

// Wrong: the 3x3 sum skips the centre cell.
std::uint32_t __fastcall WrongGridSumAround(void* self, void* edx, std::uint32_t x, std::uint32_t y) {
    auto* cell = static_cast<AiGridCell*>(AI_GridCell(self, nullptr, x & 0xffff, y & 0xffff));
    AiGridCell saved;
    std::memcpy(&saved, cell, sizeof(saved));
    std::memset(cell, 0, offsetof(AiGridCell, unk0e));  // the centre's values read as 0
    const std::uint32_t result = AI_GridSumAround(self, edx, x, y);
    std::memcpy(cell, &saved, offsetof(AiGridCell, unk0e));
    return result;
}

}  // namespace

S3RE_TEST(AiHelpers25_MatchOriginal) {
    CompareAi25(ctx, OurAi25(), 20000);
}

S3RE_TEST(AiHelpers25_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Ai25Functions wrong = OurAi25();
    wrong.gridSumAround = &WrongGridSumAround;
    CompareAi25(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
