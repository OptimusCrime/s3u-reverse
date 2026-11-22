// Small helpers of the computer players (Rainer\Cevaland.cpp, "land
// evaluation", and Rainer\CompMapWorkerAgent.cpp; batch 26). Globals as in
// ai_helpers.cpp: 0x7e3408 the world, 0x7e3412 / 0x7e3414 the map size (y /
// x, uint16 copies).
//
// AI_List32AddValue_551780 (0x00551780): (value) thiscall, ret 4:
//   AI_List32Add(value). EAX = its result.
// AI_List32AddWord8_551760 (0x00551760): (record) thiscall, ret 4: passes
//   the word at record+8 to 0x551780. The original loads it with `mov dx`,
//   so the upper half of the pushed dword is the caller's EDX (ours gets it
//   as the EDX parameter); AI_WordListInsert stores only the low word.
//   EAX = 0x551780's result.
// AI_EvaLand_ResetGrid_554070 (0x00554070): () +0x140, +0x144, +0x148,
//   +0x14c, +0x158 = 0 (grid width, height, two more sizes, grid pointer of
//   the land evaluation, see AI_GridCellWrapped); EAX = 0.
// AI_TileCentre_554880 (0x00554880): (x, y, u16* outX, u16* outY) ret 0x10,
//   ECX unused: *outX = x * 8 + 4, *outY = y * 8 + 4 (the centre of an
//   8x8 block in tiles). EAX = y (the register keeps the last loaded
//   argument).
// AI_TileByte8_599570 (0x00599570): fastcall (ECX = const u16* x, EDX =
//   const u16* y): if x < map size x (0x7e3414) and y < map size y
//   (0x7e3412) (16-bit compares): AX = byte +8 of tile (x, y) (world +
//   0x67444 + (x * 0x300 + y) * 24 + 8: Tile::owner, the player owning the
//   ground, per re/structs.md; this comment once guessed terrain); else AX
//   = 0xffff. Callers use AX. The upper half of EAX is (x * 0x300 + y) * 3
//   >> 16 on the map and the caller's EAX off it (ours: 0 there).
// AI_BlockCentreByte8_554850 (0x00554850): (bx, by) ret 8, ECX unused:
//   AI_TileByte8 of (bx * 8 + 4, by * 8 + 4) (dwords stored over its own
//   arguments). EAX as above, where the caller's EAX is bx * 8 + 4.
// AI_EvaLand_GridCellOfTile_554770 (0x00554770): (x, y) thiscall, ret 8:
//   AI_GridCellWrapped(x / 8, y / 8) (low words only). EAX = the cell.
// AI_BuildingIfUsed_58e1c0 (0x0058e1c0): fastcall (ECX = id; low word): the
//   building record (world + 0x1499e64 + id * 0x3a) if id <= the building
//   count (+0x1499e60, unsigned) and its first byte is not 0xff (unused
//   slot), else null.

#include "game/ai_layout.h"
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
std::uint32_t __fastcall AI_List32Add(void* self, void* edx, std::uint32_t value);
void* __fastcall AI_GridCellWrapped(void* self, void* edx, std::uint32_t x, std::uint32_t y);

using namespace ai;

namespace {

// AI_TileByte8 with the caller's EAX (kept in the upper half off the map).
std::uint32_t TileByte8(std::uint16_t x, std::uint16_t y, std::uint32_t callerEax) {
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
using TileByteFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);
using BlockByteFn = std::uint32_t(__stdcall*)(std::uint32_t bx, std::uint32_t by);
using GridCellOfTileFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using BuildingIfUsedFn = void*(__fastcall*)(std::uint32_t id);

std::uint32_t __fastcall AI_List32AddValue_551780(void* self, void* edx, std::uint32_t value);
std::uint32_t __fastcall AI_List32AddWord8_551760(void* self, void* edx, std::uint32_t record);
std::uint32_t __fastcall AI_EvaLand_ResetGrid_554070(void* self, void* edx);
std::uint32_t __stdcall AI_TileCentre_554880(std::uint32_t x, std::uint32_t y, std::uint16_t* outX,
                                             std::uint16_t* outY);
std::uint32_t __fastcall AI_TileByte8_599570(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __stdcall AI_BlockCentreByte8_554850(std::uint32_t bx, std::uint32_t by);
void* __fastcall AI_EvaLand_GridCellOfTile_554770(void* self, void* edx, std::uint32_t x, std::uint32_t y);
void* __fastcall AI_BuildingIfUsed_58e1c0(std::uint32_t id);

S3RE_HOOK(0x00551780, AI_List32AddValue_551780, ThisValueFn, Original_AI_List32AddValue_551780,
          AI_List32AddValue_551780);

// Purpose: Adds a number to a computer player's 50-entry list (a thin
// wrapper of AI_List32Add).
std::uint32_t __fastcall AI_List32AddValue_551780(void* self, void* /*edx*/, std::uint32_t value) {
    return AI_List32Add(self, nullptr, value);
}

S3RE_HOOK(0x00551760, AI_List32AddWord8_551760, ThisValueFn, Original_AI_List32AddWord8_551760,
          AI_List32AddWord8_551760);

// Purpose: Adds the id stored in a record (word +8, probably a building or
// settler id) to a computer player's 50-entry list.
std::uint32_t __fastcall AI_List32AddWord8_551760(void* self, void* edx, std::uint32_t record) {
    // The only known field of the record (layout otherwise unknown).
    const std::uint16_t word = RawAtAddress<std::uint16_t>(record + 8);
    const std::uint32_t value = (reinterpret_cast<std::uint32_t>(edx) & 0xffff0000u) | word;
    return AI_List32AddValue_551780(self, nullptr, value);
}

S3RE_HOOK(0x00554070, AI_EvaLand_ResetGrid_554070, ThisFn, Original_AI_EvaLand_ResetGrid_554070,
          AI_EvaLand_ResetGrid_554070);

// Purpose: Forgets the land evaluation grid of a computer player (sizes and
// pointer to 0, after the grid was freed).
std::uint32_t __fastcall AI_EvaLand_ResetGrid_554070(void* self, void* /*edx*/) {
    auto* evaluation = static_cast<AiLandEvaluation*>(self);
    evaluation->width = 0;
    evaluation->height = 0;
    evaluation->unk148 = 0;
    evaluation->unk14c = 0;
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

S3RE_HOOK(0x00599570, AI_TileByte8_599570, TileByteFn, Original_AI_TileByte8_599570, AI_TileByte8_599570);

// Purpose: A map byte of a tile for the computer players (tile byte +8,
// the owner of the ground), or 0xffff off the map.
std::uint32_t __fastcall AI_TileByte8_599570(const std::uint16_t* x, const std::uint16_t* y) {
    return TileByte8(*x, *y, 0);
}

S3RE_HOOK(0x00554850, AI_BlockCentreByte8_554850, BlockByteFn, Original_AI_BlockCentreByte8_554850,
          AI_BlockCentreByte8_554850);

// Purpose: The map byte (the owner of the ground) at the centre of an 8x8
// block of the computer players' coarse grid.
std::uint32_t __stdcall AI_BlockCentreByte8_554850(std::uint32_t bx, std::uint32_t by) {
    const std::uint32_t x = bx * 8 + 4;
    const std::uint32_t y = by * 8 + 4;
    return TileByte8(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), x & 0xffff0000u);
}

S3RE_HOOK(0x00554770, AI_EvaLand_GridCellOfTile_554770, GridCellOfTileFn, Original_AI_EvaLand_GridCellOfTile_554770,
          AI_EvaLand_GridCellOfTile_554770);

// Purpose: The land evaluation grid cell (8x8 tiles) that contains a tile.
void* __fastcall AI_EvaLand_GridCellOfTile_554770(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    return AI_GridCellWrapped(self, nullptr, (x & 0xffff) >> 3, (y & 0xffff) >> 3);
}

S3RE_HOOK(0x0058e1c0, AI_BuildingIfUsed_58e1c0, BuildingIfUsedFn, Original_AI_BuildingIfUsed_58e1c0,
          AI_BuildingIfUsed_58e1c0);

// Purpose: The record of a building for the computer players, or null for
// an id past the building count or an unused slot.
void* __fastcall AI_BuildingIfUsed_58e1c0(std::uint32_t id) {
    GameWorld* world = g_aiWorld.get();
    id &= 0xffff;
    if (id > world->building_high_water) {
        return nullptr;
    }
    layout::Building* record = &world->buildings[id];
    return record->owner != 0xff ? record : nullptr;
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
    TileByteFn tileByte;
    BlockByteFn blockByte;
    GridCellOfTileFn cellOfTile;
    BuildingIfUsedFn buildingIfUsed;
};

AiSmallFunctions OriginalAiSmall() {
    return {Original_AI_List32AddValue_551780, Original_AI_List32AddWord8_551760,
            Original_AI_EvaLand_ResetGrid_554070, Original_AI_TileCentre_554880,
            Original_AI_TileByte8_599570,      Original_AI_BlockCentreByte8_554850,
            Original_AI_EvaLand_GridCellOfTile_554770, Original_AI_BuildingIfUsed_58e1c0};
}

AiSmallFunctions OurAiSmall() {
    return {&AI_List32AddValue_551780, &AI_List32AddWord8_551760, &AI_EvaLand_ResetGrid_554070,
            &AI_TileCentre_554880,      &AI_TileByte8_599570,      &AI_BlockCentreByte8_554850,
            &AI_EvaLand_GridCellOfTile_554770, &AI_BuildingIfUsed_58e1c0};
}

constexpr std::size_t kObjectBytes = 0x300;
constexpr std::size_t kMaxBuildings = 2100;
constexpr std::size_t kWorldBytes = offsetof(GameWorld, buildings) + (kMaxBuildings + 1) * sizeof(layout::Building);
static_assert(kObjectBytes >= sizeof(AiList50Owner) && kObjectBytes >= sizeof(AiLandEvaluation));

void CompareObjects(test::Context& ctx, const AiSmallFunctions& candidate, int cases) {
    const AiSmallFunctions original = OriginalAiSmall();
    std::vector<std::uint8_t> a(kObjectBytes), b(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(5));
        ctx.rng().Fill(a.data(), a.size());
        // The buffer is both the list owner and the land evaluation. The
        // list (50 words) holds small values with some free slots.
        auto* list = reinterpret_cast<AiList50Owner*>(a.data());
        for (std::uint16_t& entry : list->entries) {
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
    GameWorld* const savedWorld = g_aiWorld;
    const std::uint16_t savedSizeX = g_aiMapSizeX, savedSizeY = g_aiMapSizeY;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        const std::uint16_t sizeX = static_cast<std::uint16_t>(ctx.rng().Below(300));
        const std::uint16_t sizeY = static_cast<std::uint16_t>(ctx.rng().Below(0x300));
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        const std::uint32_t count = ctx.rng().Below(kMaxBuildings - 50);
        worlds.Both([&](GameWorld* w) { w->building_high_water = count; });
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
                worlds.Both([&](GameWorld* w) { w->buildings[id & 0xffff].owner = 0xff; });
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
    return AI_List32AddValue_551780(self, nullptr, RawAtAddress<std::uint16_t>(record + 6));
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

}  // namespace s3re::game
