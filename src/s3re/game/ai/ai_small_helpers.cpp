// Small helpers of the computer players (Rainer\Cevaland.cpp, "land
// evaluation", and Rainer\CompMapWorkerAgent.cpp; batch 26). Globals as in
// ai_helpers.cpp: 0x7e3408 the world, 0x7e3412 / 0x7e3414 the map size (y /
// x, uint16 copies).
//
// CComputerPlayer_DoomNoteId (0x00551780): (id) thiscall, ret 4:
//   CComputerPlayer_DoomNote(id). EAX = its result. Was
//   AI_List32AddValue_551780.
// CComputerPlayer_DoomNoteOf (0x00551760): (note) thiscall, ret 4: passes
//   the note's id (word +8) to 0x551780 (was AI_List32AddWord8_551760).
//   The original loads it with `mov dx`, so the upper half of the pushed
//   dword is the caller's EDX (ours gets it as the EDX parameter);
//   AI_WordListInsert stores only the low word.
//   EAX = 0x551780's result.
// AI_CEvaLand_ResetGrid (0x00554070): () +0x140, +0x144, +0x148,
//   +0x14c, +0x158 = 0 (grid width, height, two more sizes, grid pointer of
//   the land evaluation, see AI_CEvaLand_GridCellWrapped); EAX = 0.
// AI_TileCentre_554880 (0x00554880): (x, y, u16* outX, u16* outY) ret 0x10,
//   ECX unused: *outX = x * 8 + 4, *outY = y * 8 + 4 (the centre of an
//   8x8 block in tiles). EAX = y (the register keeps the last loaded
//   argument).
// AI_TileOwner (0x00599570): fastcall (ECX = const u16* x, EDX =
//   const u16* y): if x < map size x (0x7e3414) and y < map size y
//   (0x7e3412) (16-bit compares): AX = byte +8 of tile (x, y) (world +
//   0x67444 + (x * 0x300 + y) * 24 + 8: Tile::owner, the player owning the
//   ground, per re/structs.md; this comment once guessed terrain); else AX
//   = 0xffff. Callers use AX. The upper half of EAX is (x * 0x300 + y) * 3
//   >> 16 on the map and the caller's EAX off it (ours: 0 there).
// AI_CellCentreOwner (0x00554850): (bx, by) ret 8, ECX unused:
//   AI_TileOwner of (bx * 8 + 4, by * 8 + 4) (dwords stored over its own
//   arguments). EAX as above, where the caller's EAX is bx * 8 + 4.
// AI_CEvaLand_GridCellOfTile (0x00554770): (x, y) thiscall, ret 8:
//   AI_CEvaLand_GridCellWrapped(x / 8, y / 8) (low words only). EAX = the cell.
// AI_BuildingIfUsed_58e1c0 (0x0058e1c0): fastcall (ECX = id; low word): the
//   building record (world + 0x1499e64 + id * 0x3a) if id <= the building
//   count (+0x1499e60, unsigned) and its first byte is not 0xff (unused
//   slot), else null.
// AI_BlackBoardEmptyVirtual (0x0055ec00): (value) thiscall, ret 4: an empty
//   virtual shared by the notes' vtables (slots 2 and 3) and the agents'
//   (slots 11 and 12; identical code folded): calls NoOp_ret4 0x582530
//   (value) with ECX unchanged. EAX = value (loaded for the push, kept by
//   the no-op). Was StaticInitExpr_55ec00 (it is no static initializer).
// BLACK_BOARD_AGENT_DeleteThis (0x0056faf0): () thiscall: slot 1 of every
//   agent and note vtable: unless this is null, its slot 0 (the scalar
//   deleting destructor) with 1. EAX: the destructor's (null: left over;
//   nothing calls the slot).

#include "game/ai/ai_layout.h"
#include "game/globals.h"
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

// ai_helpers.cpp
std::uint32_t __fastcall CComputerPlayer_DoomNote(void* self, void* edx, std::uint32_t value);
void* __fastcall AI_CEvaLand_GridCellWrapped(void* self, void* edx, std::uint32_t x, std::uint32_t y);

using namespace ai;

namespace {

// AI_TileOwner with the caller's EAX (kept in the upper half off the map).
std::uint32_t TileOwnerOrNone(std::uint16_t x, std::uint16_t y, std::uint32_t callerEax) {
    if (x >= g_aiMapSizeX || y >= g_aiMapSizeY) {
        return callerEax | 0xffffu;
    }
    // The upper half of EAX keeps the original's (tile index * 3).
    const std::uint32_t index3 = (static_cast<std::uint32_t>(x) * 0x300 + y) * 3;
    return (index3 & 0xffff0000u) | g_aiWorld.get()->tiles[x][y].owner;
}

}  // namespace

using ThisValueFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using TileCentreFn = std::uint32_t(__stdcall*)(std::uint32_t x, std::uint32_t y, std::uint16_t* outX,
                                               std::uint16_t* outY);
using TileOwnerFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);
using CellOwnerFn = std::uint32_t(__stdcall*)(std::uint32_t bx, std::uint32_t by);
using GridCellOfTileFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using BuildingIfUsedFn = void*(__fastcall*)(std::uint32_t id);

std::uint32_t __fastcall CComputerPlayer_DoomNoteId(void* self, void* edx, std::uint32_t value);
std::uint32_t __fastcall CComputerPlayer_DoomNoteOf(void* self, void* edx, std::uint32_t record);
std::uint32_t __fastcall AI_CEvaLand_ResetGrid(void* self, void* edx);
std::uint32_t __stdcall AI_TileCentre_554880(std::uint32_t x, std::uint32_t y, std::uint16_t* outX,
                                             std::uint16_t* outY);
std::uint32_t __fastcall AI_TileOwner(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __stdcall AI_CellCentreOwner(std::uint32_t bx, std::uint32_t by);
void* __fastcall AI_CEvaLand_GridCellOfTile(void* self, void* edx, std::uint32_t x, std::uint32_t y);
void* __fastcall AI_BuildingIfUsed_58e1c0(std::uint32_t id);

S3RE_HOOK(0x00551780, CComputerPlayer_DoomNoteId, ThisValueFn, Original_CComputerPlayer_DoomNoteId,
          CComputerPlayer_DoomNoteId);

// Purpose: Marks a computer player's blackboard note for deletion by its id
// (a thin wrapper of CComputerPlayer_DoomNote).
std::uint32_t __fastcall CComputerPlayer_DoomNoteId(void* self, void* /*edx*/, std::uint32_t id) {
    return CComputerPlayer_DoomNote(self, nullptr, id);
}

S3RE_HOOK(0x00551760, CComputerPlayer_DoomNoteOf, ThisValueFn, Original_CComputerPlayer_DoomNoteOf,
          CComputerPlayer_DoomNoteOf);

// Purpose: Marks a computer player's blackboard note for deletion (by the
// note's id).
std::uint32_t __fastcall CComputerPlayer_DoomNoteOf(void* self, void* edx, std::uint32_t note) {
    // The note's id (word +8); the original loads it with `mov dx`, so the
    // upper half of the pushed dword is the caller's EDX.
    const std::uint16_t id = RawAtAddress<std::uint16_t>(note + 8);
    const std::uint32_t value = (reinterpret_cast<std::uint32_t>(edx) & 0xffff0000u) | id;
    return CComputerPlayer_DoomNoteId(self, nullptr, value);
}

S3RE_HOOK(0x00554070, AI_CEvaLand_ResetGrid, ThisFn, Original_AI_CEvaLand_ResetGrid,
          AI_CEvaLand_ResetGrid);

// Purpose: Forgets the land evaluation grid of a computer player (sizes and
// pointer to 0, after the grid was freed).
std::uint32_t __fastcall AI_CEvaLand_ResetGrid(void* self, void* /*edx*/) {
    auto* evaluation = static_cast<AiLandEvaluation*>(self);
    evaluation->width = 0;
    evaluation->height = 0;
    evaluation->cell_count = 0;
    evaluation->tiles_per_cell = 0;
    evaluation->cells = nullptr;
    return 0;
}

S3RE_HOOK(0x00554880, AI_TileCentre_554880, TileCentreFn, Original_AI_TileCentre_554880, AI_TileCentre_554880);

// Purpose: The tile in the middle of an 8x8 block of the computer players'
// coarse map grid.
std::uint32_t __stdcall AI_TileCentre_554880(std::uint32_t x, std::uint32_t y, std::uint16_t* outX,
                                             std::uint16_t* outY) {
    *outX = static_cast<std::uint16_t>(x * 8 + 4);
    *outY = static_cast<std::uint16_t>(y * 8 + 4);
    return y;
}

S3RE_HOOK(0x00599570, AI_TileOwner, TileOwnerFn, Original_AI_TileOwner, AI_TileOwner);

// Purpose: The owner of a tile's ground for the computer players (tile byte
// +8), or 0xffff off the map. Was AI_TileByte8_599570.
std::uint32_t __fastcall AI_TileOwner(const std::uint16_t* x, const std::uint16_t* y) {
    return TileOwnerOrNone(*x, *y, 0);
}

S3RE_HOOK(0x00554850, AI_CellCentreOwner, CellOwnerFn, Original_AI_CellCentreOwner,
          AI_CellCentreOwner);

// Purpose: The owner of the ground at the centre of a cell (8x8 tiles) of
// the computer players' land grid. Was AI_BlockCentreByte8_554850.
std::uint32_t __stdcall AI_CellCentreOwner(std::uint32_t bx, std::uint32_t by) {
    const std::uint32_t x = bx * 8 + 4;
    const std::uint32_t y = by * 8 + 4;
    return TileOwnerOrNone(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), x & 0xffff0000u);
}

S3RE_HOOK(0x00554770, AI_CEvaLand_GridCellOfTile, GridCellOfTileFn, Original_AI_CEvaLand_GridCellOfTile,
          AI_CEvaLand_GridCellOfTile);

// Purpose: The land evaluation grid cell (8x8 tiles) that contains a tile.
void* __fastcall AI_CEvaLand_GridCellOfTile(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    return AI_CEvaLand_GridCellWrapped(self, nullptr, (x & 0xffff) >> 3, (y & 0xffff) >> 3);
}

S3RE_HOOK(0x0058e1c0, AI_BuildingIfUsed_58e1c0, BuildingIfUsedFn, Original_AI_BuildingIfUsed_58e1c0,
          AI_BuildingIfUsed_58e1c0);

// Purpose: The record of a building for the computer players, or null for
// an id past the building count or an unused slot.
void* __fastcall AI_BuildingIfUsed_58e1c0(std::uint32_t id) {
    CGameData* world = g_aiWorld.get();
    id &= 0xffff;
    if (id > world->building_high_water) {
        return nullptr;
    }
    layout::Building* record = &world->buildings[id];
    return record->owner != 0xff ? record : nullptr;
}

constexpr std::uintptr_t kNoOpRet4 = 0x00582530;  // NoOp_ret4 (ret 4)

std::uint32_t __fastcall AI_BlackBoardEmptyVirtual(void* self, void* edx, std::uint32_t value);

S3RE_HOOK(0x0055ec00, AI_BlackBoardEmptyVirtual, ThisValueFn, Original_AI_BlackBoardEmptyVirtual,
          AI_BlackBoardEmptyVirtual);

// Purpose: A virtual of the AI notes and agents that does nothing (none of
// the computer player's code calls those slots, as far as seen).
std::uint32_t __fastcall AI_BlackBoardEmptyVirtual(void* self, void* /*edx*/, std::uint32_t value) {
    reinterpret_cast<ThisValueFn>(kNoOpRet4)(self, nullptr, value);
    return value;
}

std::uint32_t __fastcall BLACK_BOARD_AGENT_DeleteThis(void* self, void* edx);

S3RE_HOOK(0x0056faf0, BLACK_BOARD_AGENT_DeleteThis, ThisFn, Original_BLACK_BOARD_AGENT_DeleteThis,
          BLACK_BOARD_AGENT_DeleteThis);

// Purpose: Deletes an AI agent or note through its own destructor (a
// "delete this" helper; none of the AI code calls it, as far as seen).
std::uint32_t __fastcall BLACK_BOARD_AGENT_DeleteThis(void* self, void* /*edx*/) {
    if (self == nullptr) {
        return 0;
    }
    constexpr std::uint32_t kDeleteMemory = 1;
    const auto* vtable = *static_cast<const std::uintptr_t* const*>(self);
    return reinterpret_cast<ThisValueFn>(vtable[agent_slot::kDeletingDestructor])(self, nullptr, kDeleteMemory);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Objects (computer player, land evaluation) are random 0x300-byte buffers;
// the map and building functions run on a world pair with the AI globals
// pointed at the world of each side (restored afterwards).

namespace {

struct AiSmallFunctions {
    ThisValueFn addValue;
    ThisValueFn addWord8;
    ThisFn resetGrid;
    TileCentreFn tileCentre;
    TileOwnerFn tileByte;
    CellOwnerFn blockByte;
    GridCellOfTileFn cellOfTile;
    BuildingIfUsedFn buildingIfUsed;
};

AiSmallFunctions OriginalAiSmall() {
    return {Original_CComputerPlayer_DoomNoteId, Original_CComputerPlayer_DoomNoteOf,
            Original_AI_CEvaLand_ResetGrid, Original_AI_TileCentre_554880,
            Original_AI_TileOwner,      Original_AI_CellCentreOwner,
            Original_AI_CEvaLand_GridCellOfTile, Original_AI_BuildingIfUsed_58e1c0};
}

AiSmallFunctions OurAiSmall() {
    return {&CComputerPlayer_DoomNoteId, &CComputerPlayer_DoomNoteOf, &AI_CEvaLand_ResetGrid,
            &AI_TileCentre_554880,      &AI_TileOwner,      &AI_CellCentreOwner,
            &AI_CEvaLand_GridCellOfTile, &AI_BuildingIfUsed_58e1c0};
}

constexpr std::size_t kObjectBytes = 0x300;
constexpr std::size_t kMaxBuildings = 2100;
constexpr std::size_t kWorldBytes = offsetof(CGameData, buildings) + (kMaxBuildings + 1) * sizeof(layout::Building);
// The buffer holds the doomed notes of a ComputerPlayer (only that part).
static_assert(kObjectBytes >= offsetof(ComputerPlayer, doomed_note_count) + 2 &&
              kObjectBytes >= sizeof(AiLandEvaluation));

void CompareObjects(test::Context& ctx, const AiSmallFunctions& candidate, int cases) {
    const AiSmallFunctions original = OriginalAiSmall();
    std::vector<std::uint8_t> a(kObjectBytes), b(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(5));
        ctx.rng().Fill(a.data(), a.size());
        // The buffer is both the computer player and the land evaluation.
        // The doomed notes (50 words) hold small ids with some free slots.
        auto* player = reinterpret_cast<ComputerPlayer*>(a.data());
        for (std::uint16_t& entry : player->doomed_notes) {
            entry = ctx.rng().Chance(1, 4) ? 0xffff : static_cast<std::uint16_t>(ctx.rng().Below(64));
        }
        auto* evaluation = reinterpret_cast<AiLandEvaluation*>(a.data());
        for (std::uint32_t* field : {&evaluation->width, &evaluation->height}) {  // grid size, never 0
            *field = 1 + ctx.rng().Below(ctx.rng().Chance(1, 8) ? 0xffffffffu : 80);
        }
        b = a;
        const std::uint32_t value = ctx.rng().Chance(1, 2) ? ctx.rng().Below(64) : ctx.rng().Next32();
        const std::uint32_t edx = ctx.rng().Next32();
        const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
        std::uint32_t ra = 0, rb = 0;
        switch (op) {
            case 0:
                ra = original.addValue(a.data(), nullptr, value);
                rb = candidate.addValue(b.data(), nullptr, value);
                break;
            case 1: {
                // The record is 16 bytes inside the object's own buffer (+0x2c0).
                ra = original.addWord8(a.data(), reinterpret_cast<void*>(edx),
                                       reinterpret_cast<std::uint32_t>(a.data() + 0x2c0));
                rb = candidate.addWord8(b.data(), reinterpret_cast<void*>(edx),
                                        reinterpret_cast<std::uint32_t>(b.data() + 0x2c0));
                break;
            }
            case 2:
                ra = original.resetGrid(a.data(), nullptr);
                rb = candidate.resetGrid(b.data(), nullptr);
                break;
            case 3:
                ra = original.tileCentre(x, y, reinterpret_cast<std::uint16_t*>(a.data() + 0x10),
                                         reinterpret_cast<std::uint16_t*>(a.data() + 0x13));
                rb = candidate.tileCentre(x, y, reinterpret_cast<std::uint16_t*>(b.data() + 0x10),
                                          reinterpret_cast<std::uint16_t*>(b.data() + 0x13));
                break;
            default:
                // The grid pointer (cells) is the same random value on both sides.
                ra = reinterpret_cast<std::uint32_t>(original.cellOfTile(a.data(), nullptr, x, y));
                rb = reinterpret_cast<std::uint32_t>(candidate.cellOfTile(b.data(), nullptr, x, y));
                break;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

void CompareWorld(test::Context& ctx, const AiSmallFunctions& candidate, int cases) {
    const AiSmallFunctions original = OriginalAiSmall();
    test::WorldPair worlds(ctx, kWorldBytes);
    CGameData* const savedWorld = g_aiWorld;
    const std::uint16_t savedSizeX = g_aiMapSizeX, savedSizeY = g_aiMapSizeY;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        const std::uint16_t sizeX = static_cast<std::uint16_t>(ctx.rng().Below(300));
        const std::uint16_t sizeY = static_cast<std::uint16_t>(ctx.rng().Below(0x300));
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        const std::uint32_t count = ctx.rng().Below(kMaxBuildings - 50);
        worlds.Both([&](CGameData* w) { w->building_high_water = count; });
        std::uint32_t ra = 0, rb = 0;
        if (op == 0) {
            const std::uint16_t x = static_cast<std::uint16_t>(ctx.rng().Below(sizeX + 8u));
            const std::uint16_t y = static_cast<std::uint16_t>(ctx.rng().Below(sizeY + 8u));
            g_aiWorld = worlds.original();
            ra = original.tileByte(&x, &y);
            g_aiWorld = worlds.ours();
            rb = candidate.tileByte(&x, &y);
            ra &= 0xffff;  // callers use AX; the upper half off the map is the caller's EAX
            rb &= 0xffff;
        } else if (op == 1) {
            const std::uint32_t bx = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(sizeX / 8 + 3u);
            const std::uint32_t by = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(sizeY / 8 + 3u);
            const std::uint16_t cx = static_cast<std::uint16_t>(bx * 8 + 4);
            const std::uint16_t cy = static_cast<std::uint16_t>(by * 8 + 4);
            if (cx < sizeX && cy < sizeY || ctx.rng().Chance(1, 2)) {
                g_aiWorld = worlds.original();
                ra = original.blockByte(bx, by);
                g_aiWorld = worlds.ours();
                rb = candidate.blockByte(bx, by);
                // Callers compare AX only. In-process the original calls our
                // 0x599570 (hooked), whose upper half differs off the map.
                ra &= 0xffff;
                rb &= 0xffff;
            }
        } else {
            const std::uint32_t id = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(kMaxBuildings);
            if ((id & 0xffff) <= kMaxBuildings && ctx.rng().Chance(1, 3)) {
                worlds.Both([&](CGameData* w) { w->buildings[id & 0xffff].owner = 0xff; });
            }
            g_aiWorld = worlds.original();
            void* pa = original.buildingIfUsed(id);
            g_aiWorld = worlds.ours();
            void* pb = candidate.buildingIfUsed(id);
            ra = pa == nullptr ? 0 : static_cast<std::uint32_t>(static_cast<std::uint8_t*>(pa) - worlds.originalBytes());
            rb = pb == nullptr ? 0 : static_cast<std::uint32_t>(static_cast<std::uint8_t*>(pb) - worlds.oursBytes());
        }
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    g_aiWorld = savedWorld;
    g_aiMapSizeX = savedSizeX;
    g_aiMapSizeY = savedSizeY;
}

std::uint32_t __fastcall WrongAddWord8(void* self, void* /*edx*/, std::uint32_t record) {
    // Wrong: reads the word at +6 instead of +8.
    return CComputerPlayer_DoomNoteId(self, nullptr, RawAtAddress<std::uint16_t>(record + 6));
}

void* __fastcall WrongBuildingIfUsed(std::uint32_t id) {
    // Wrong: id == count is treated as past the end.
    id &= 0xffff;
    if (id == g_aiWorld.get()->building_high_water) {
        return nullptr;
    }
    return AI_BuildingIfUsed_58e1c0(id);
}

}  // namespace

S3RE_TEST(AiSmallHelpers_MatchOriginal) {
    CompareObjects(ctx, OurAiSmall(), 20000);
    CompareWorld(ctx, OurAiSmall(), 20000);
}

S3RE_TEST(AiSmallHelpers_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        AiSmallFunctions wrong = OurAiSmall();
        wrong.addWord8 = &WrongAddWord8;
        CompareObjects(probe, wrong, 5000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        AiSmallFunctions wrong = OurAiSmall();
        wrong.buildingIfUsed = &WrongBuildingIfUsed;
        CompareWorld(probe, wrong, 20000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

// AI_BlackBoardEmptyVirtual: EAX and the stack (callee pops the argument)
// for random values and objects. The wrong version returns the object.
namespace {

void CompareEmptyVirtual(test::Context& ctx, ThisValueFn ours, int cases) {
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const std::uint32_t value = ctx.rng().Next32();
        void* self = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ctx.rng().Next32()));  // never dereferenced
        const std::uint32_t a = Original_AI_BlackBoardEmptyVirtual(self, nullptr, value);
        const std::uint32_t b = ours(self, nullptr, value);
        if (!S3RE_EXPECT_EQ(a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, value %08x", i, value);
        }
    }
}

std::uint32_t __fastcall WrongEmptyVirtual(void* self, void* edx, std::uint32_t value) {
    AI_BlackBoardEmptyVirtual(self, edx, value);
    return reinterpret_cast<std::uint32_t>(self);  // Wrong: EAX = this
}

}  // namespace

S3RE_TEST(AiEmptyVirtual_MatchOriginal) {
    CompareEmptyVirtual(ctx, &AI_BlackBoardEmptyVirtual, 2000);
}

S3RE_TEST(AiEmptyVirtual_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareEmptyVirtual(quiet, &WrongEmptyVirtual, 100);
    S3RE_EXPECT(quiet.failures() > 0);
}

// BLACK_BOARD_AGENT_DeleteThis: a fake object whose slot 0 records `this`
// and the flags and answers at random; null objects too. The wrong version
// passes flags 0.
namespace {

struct DeleteCall {
    void* self = nullptr;
    std::uint32_t flags = 0;
    int count = 0;
};
DeleteCall g_deleteCall;
std::uint32_t g_deleteAnswer = 0;

std::uint32_t __fastcall RecordDelete(void* self, void*, std::uint32_t flags) {
    g_deleteCall = {self, flags, g_deleteCall.count + 1};
    return g_deleteAnswer;
}

void CompareDeleteThis(test::Context& ctx, ThisFn ours, int cases) {
    const std::uintptr_t vtable[1] = {reinterpret_cast<std::uintptr_t>(&RecordDelete)};
    struct {
        const std::uintptr_t* vtable;
        std::uint32_t data;
    } object{vtable, 0};
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        void* self = ctx.rng().Chance(1, 4) ? nullptr : &object;
        g_deleteAnswer = ctx.rng().Next32();
        DeleteCall calls[2];
        std::uint32_t results[2];
        const ThisFn fns[2] = {Original_BLACK_BOARD_AGENT_DeleteThis, ours};
        for (int side = 0; side < 2; ++side) {
            g_deleteCall = {};
            results[side] = fns[side](self, nullptr);
            calls[side] = g_deleteCall;
        }
        bool same = S3RE_EXPECT_EQ(calls[0].count, calls[1].count);
        same &= S3RE_EXPECT(calls[0].self == calls[1].self);
        same &= S3RE_EXPECT_EQ(calls[0].flags, calls[1].flags);
        if (self != nullptr) {  // (null: EAX is the caller's in the original)
            same &= S3RE_EXPECT_EQ(results[0], results[1]);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
}

std::uint32_t __fastcall WrongDeleteThis(void* self, void*) {
    if (self == nullptr) {
        return 0;
    }
    return RecordDelete(self, nullptr, 0);  // Wrong: keeps the memory
}

}  // namespace

S3RE_TEST(AiDeleteThis_MatchOriginal) {
    CompareDeleteThis(ctx, &BLACK_BOARD_AGENT_DeleteThis, 1000);
}

S3RE_TEST(AiDeleteThis_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareDeleteThis(quiet, &WrongDeleteThis, 100);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
