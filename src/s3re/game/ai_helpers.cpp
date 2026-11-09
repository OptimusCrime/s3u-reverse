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

#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kAiWorld = 0x007e3408;
constexpr std::uintptr_t kAiPlayer = 0x007e3130;
constexpr std::uintptr_t kAiPlayerNumber = 0x007e2eb8;
constexpr std::uintptr_t kAiPlayerSetup = 0x007e2eb4;
constexpr std::uintptr_t kAiPlayerData = 0x007e2eb0;
constexpr std::uintptr_t kPlayerSetupTable = 0x00731488;
constexpr std::uintptr_t kCandidateDefaults = 0x007329d0;

constexpr std::size_t kMapSize = 0x2d2d0;
constexpr std::size_t kAreas = 0xe145e8;
constexpr std::size_t kAreaBytes = 0x2e0;
constexpr std::size_t kAreaPileLists = 0xa;
constexpr std::size_t kPiles = 0x123885c;
constexpr std::size_t kPileBytes = 14;
constexpr std::size_t kBuildingCount = 0x1499e60;
constexpr std::size_t kBuildings = 0x1499e64;
constexpr std::size_t kBuildingBytes = 0x3a;

template <typename T>
T& At(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

std::uint8_t* AiWorld() {
    return At<std::uint8_t*>(kAiWorld);
}

}  // namespace

using SetCurrentPlayerFn = std::uint32_t(__fastcall*)(void* player);
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
    At<void*>(kAiPlayer) = player;
    At<std::uint8_t>(kAiPlayerNumber) = At<std::uint8_t>(player, 0x272);
    At<std::uint32_t>(kAiPlayerSetup) = At<std::uint32_t>(player, 0x770) * 0x180 + kPlayerSetupTable;
    const std::uint32_t data = At<std::uint32_t>(player, 0x778);
    At<std::uint32_t>(kAiPlayerData) = data;
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
    return v >= 0 && v < At<std::int32_t>(AiWorld(), kMapSize);
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
    const std::int32_t size = At<std::int32_t>(AiWorld(), kMapSize);
    if (*x > size) {
        *x = size;
    }
    const std::int32_t size2 = At<std::int32_t>(AiWorld(), kMapSize);
    if (*y > size2) {
        *y = size2;
    }
    return static_cast<std::uint32_t>(size2);
}

S3RE_HOOK(0x0059dc10, AI_AreaGoodsAmount, AreaGoodsAmountFn, Original_AI_AreaGoodsAmount, AI_AreaGoodsAmount);

// Purpose: How many units of a goods type lie on the piles of an economy
// area, as the computer players see it.
std::uint32_t __fastcall AI_AreaGoodsAmount(const std::uint8_t* goods, const std::uint16_t* area) {
    std::uint8_t* world = AiWorld();
    const std::uint32_t list = (static_cast<std::uint32_t>(*area) * kAreaBytes + kAreaPileLists) + *goods * 2u;
    std::uint16_t pile = At<std::uint16_t>(world, kAreas + list);
    std::uint32_t sum = 0;
    while (pile != 0) {
        const std::size_t record = kPiles + static_cast<std::size_t>(pile) * kPileBytes;
        sum += At<std::uint8_t>(world, record + 1);
        pile = At<std::uint16_t>(world, record + 0xc);
    }
    return sum & 0xffff;
}

S3RE_HOOK(0x0058e170, AI_GetBuilding, GetBuildingFn, Original_AI_GetBuilding, AI_GetBuilding);

// Purpose: Looks up a building record by number for the computer players;
// null (with an error flag for a bad number) if there is no such building.
void* __fastcall AI_GetBuilding(std::uint32_t id, std::uint32_t* error) {
    *error = 0;
    std::uint8_t* world = AiWorld();
    const std::uint32_t index = id & 0xffff;
    if (index > At<std::uint32_t>(world, kBuildingCount)) {
        *error = 1;
        return nullptr;
    }
    std::uint8_t* building = world + kBuildings + static_cast<std::size_t>(index) * kBuildingBytes;
    return building[0] != 0xff ? building : nullptr;
}

S3RE_HOOK(0x0058ec50, AI_RecordBestCandidate, RecordBestCandidateFn, Original_AI_RecordBestCandidate,
          AI_RecordBestCandidate);

// Purpose: Probably remembers, per building type, the best place a computer
// player has found so far (score and position), after resetting a per-type
// value to its default.
std::uint32_t __fastcall AI_RecordBestCandidate(std::uint32_t index, std::uint32_t score, std::uint32_t a,
                                                std::uint32_t b) {
    const std::uint32_t i = index & 0xffff;
    auto* data = At<std::uint8_t*>(kAiPlayerData);
    At<std::uint16_t>(data, 0x2d2 + i * 2) = At<std::uint16_t>(kCandidateDefaults + i * 2);
    auto& best = At<std::uint16_t>(data, 0x26 + i * 6);
    if (best >= static_cast<std::uint16_t>(score)) {
        return i * 6;
    }
    best = static_cast<std::uint16_t>(score);
    At<std::uint16_t>(data, 0x28 + i * 6) = static_cast<std::uint16_t>(a);
    At<std::uint16_t>(data, (i * 3 + 0x15) * 2) = static_cast<std::uint16_t>(b);
    return i * 3 + 0x15;
}

S3RE_HOOK(0x00554700, AI_GridCell, GridCellFn, Original_AI_GridCell, AI_GridCell);

// Purpose: The cell at (x, y) of a computer player's map evaluation grid
// (Cevaland, "evaluate land").
void* __fastcall AI_GridCell(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t index = y * At<std::uint32_t>(self, 0x140) + x;
    return reinterpret_cast<void*>(At<std::uint32_t>(self, 0x158) + index * 0xc4);
}

S3RE_HOOK(0x00554040, AI_GridFree, GridFreeFn, Original_AI_GridFree, AI_GridFree);

// Purpose: Frees a computer player's map evaluation grid.
void __fastcall AI_GridFree(void* self, void* /*edx*/) {
    void*& grid = At<void*>(self, 0x158);
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
        At<std::uint32_t>(dst, 0) = At<std::uint32_t>(const_cast<void*>(src), 0);
        At<std::uint16_t>(dst, 4) = At<std::uint16_t>(const_cast<void*>(src), 4);
    }
}

S3RE_HOOK(0x0055ca60, AI_SetWord4, SetWord4Fn, Original_AI_SetWord4, AI_SetWord4);

// Purpose: Sets a 16-bit field of a computer player's record (meaning not
// known yet).
std::uint16_t __fastcall AI_SetWord4(void* self, void* /*edx*/, std::uint32_t value) {
    At<std::uint16_t>(self, 4) = static_cast<std::uint16_t>(value);
    return static_cast<std::uint16_t>(value);
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
};

AiFunctions OriginalAi() {
    return {Original_AI_SetCurrentPlayer, Original_AI_WordListInsert, Original_AI_WordListRemove,
            Original_AI_IsOnMap,          Original_AI_ClampToMap,     Original_AI_AreaGoodsAmount,
            Original_AI_GetBuilding,      Original_AI_RecordBestCandidate, Original_AI_GridCell,
            Original_AI_GridFree,         Original_AI_CopySixBytes,   Original_AI_SetWord4};
}

AiFunctions OurAi() {
    return {&AI_SetCurrentPlayer, &AI_WordListInsert, &AI_WordListRemove, &AI_IsOnMap,
            &AI_ClampToMap,       &AI_AreaGoodsAmount, &AI_GetBuilding,   &AI_RecordBestCandidate,
            &AI_GridCell,         &AI_GridFree,       &AI_CopySixBytes,  &AI_SetWord4};
}

// The globals the functions read or write, saved and restored around a test.
struct SavedGlobals {
    std::uint32_t values[5];
    static constexpr std::uintptr_t kAddresses[5] = {kAiWorld, kAiPlayer, kAiPlayerSetup, kAiPlayerData,
                                                     kAiPlayerNumber};
    SavedGlobals() {
        for (int i = 0; i < 4; ++i) {
            values[i] = At<std::uint32_t>(kAddresses[i]);
        }
        values[4] = At<std::uint8_t>(kAiPlayerNumber);
    }
    ~SavedGlobals() {
        for (int i = 0; i < 4; ++i) {
            At<std::uint32_t>(kAddresses[i]) = values[i];
        }
        At<std::uint8_t>(kAiPlayerNumber) = static_cast<std::uint8_t>(values[4]);
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
        worlds.Set<std::uint16_t>(kPiles + pile * kPileBytes + 0xc, next);
        next = pile;
    }
    worlds.Set<std::uint16_t>(kAreas + area * kAreaBytes + kAreaPileLists + goods * 2, next);
}

void CompareAi(test::Context& ctx, const AiFunctions& candidate, int cases) {
    const AiFunctions original = OriginalAi();
    SavedGlobals saved;
    test::WorldPair worlds(ctx, 0x153a128);
    std::vector<std::uint8_t> dataA(0x2000), dataB(0x2000);
    ctx.rng().Fill(dataA.data(), dataA.size());
    dataB = dataA;
    auto useWorld = [&](bool ours) {
        At<void*>(kAiWorld) = ours ? static_cast<void*>(worlds.ours()) : static_cast<void*>(worlds.original());
        At<void*>(kAiPlayerData) = ours ? dataB.data() : dataA.data();
    };
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(12));
        bool same = true;
        switch (op) {
            case 0: {
                std::uint8_t player[0x780];
                ctx.rng().Fill(player, sizeof(player));
                At<std::uint32_t>(player, 0x770) = ctx.rng().Below(64);
                const std::uint32_t ra = original.setCurrentPlayer(player);
                const std::uint32_t ga[4] = {At<std::uint32_t>(kAiPlayer), At<std::uint8_t>(kAiPlayerNumber),
                                             At<std::uint32_t>(kAiPlayerSetup), At<std::uint32_t>(kAiPlayerData)};
                At<std::uint32_t>(kAiPlayer) = 0;
                At<std::uint8_t>(kAiPlayerNumber) = 0;
                At<std::uint32_t>(kAiPlayerSetup) = 0;
                At<std::uint32_t>(kAiPlayerData) = 0;
                const std::uint32_t rb = candidate.setCurrentPlayer(player);
                same &= S3RE_EXPECT_EQ(ra, rb);
                same &= S3RE_EXPECT_EQ(ga[0], At<std::uint32_t>(kAiPlayer));
                same &= S3RE_EXPECT_EQ(ga[1], static_cast<std::uint32_t>(At<std::uint8_t>(kAiPlayerNumber)));
                same &= S3RE_EXPECT_EQ(ga[2], At<std::uint32_t>(kAiPlayerSetup));
                same &= S3RE_EXPECT_EQ(ga[3], At<std::uint32_t>(kAiPlayerData));
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
                worlds.Set<std::int32_t>(kMapSize, size);
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
                worlds.Set<std::int32_t>(kMapSize, size);
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
                worlds.Set<std::uint32_t>(kBuildingCount, ctx.rng().Below(3000));
                const std::uint32_t id = (ctx.rng().Next32() & 0xffff0000u) | ctx.rng().Below(3200);
                if (ctx.rng().Chance(1, 3)) {
                    worlds.Set<std::uint8_t>(kBuildings + (id & 0xffff) * kBuildingBytes, 0xff);
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
                    std::memcpy(dataA.data() + 0x26 + (index & 0xffff) * 6, &low, 2);
                    std::memcpy(dataB.data() + 0x26 + (index & 0xffff) * 6, &low, 2);
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
                At<void*>(oa, 0x158) = empty ? nullptr : newFn(16);
                std::memcpy(ob, oa, sizeof(oa));
                At<void*>(ob, 0x158) = empty ? nullptr : newFn(16);
                void* blockB = At<void*>(ob, 0x158);
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

}  // namespace s3re::game
