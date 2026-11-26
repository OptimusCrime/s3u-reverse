// CGameData::CGameData (0x004fc990): () thiscall, EAX = this; called by
// the logic thread when it creates the world (0x153a128 bytes, operator
// new). Region names: re/world_layout.md.
//
// In order: Win32::CObject::CObject 0x610320; the map effects table
// (MapEffects::MapEffects 0x5442c0 on +0x14d2bc8); the vtable 0x658c18 and
// DebugLog "Initialising globals"; the ring indexes +0x32870/+0x32874
// and +0x366f8/+0x366fc = 0; the globals 0x7dfd2c..0x7dfd64 (eleven of
// them) and 0x7dfd70..0x7dfd7c = 0, 0x730f4c = 1, 0x730f50 = 3;
// BuildOffsetTable 0x516530 and InitReciprocals 0x4a1eb0; then the record
// tables are zeroed (high-water mark first, then the table with rep stos,
// then the loop that marks every record free: byte 0 = 0xff for settlers,
// buildings, areas and trade records, = 0 for the rest, word 0 = 0 for the
// effects; the zeroing loops are redundant but kept as plain stores here);
// the presence maps (two blocks of 0xb400, then a redundant byte loop);
// the statistics, rings, territory claims, player lists, pile claims and
// VictoryConditions (one rep stos each; VictoryConditions 0x77 bytes); the
// pile claims cursor g_pileClaimsCursor = 0; scalars +0x2d264 = 0,
// +0x2d278 = 1, byte +0x2d250 = 0, +0x2d274 = 0; the 20 player records
// zeroed, then per player: +0x1195d7c[p] = 0, +0x00 = 0, +0x18 = 0, +0x1c
// = -1, +0x80 = 0x80, +0x8c, +0x90, +0xb8..+0xd8 = 0, +0x84 = 0xffff,
// SetPromotionCost(p) 0x508450, +0x20..+0x7f = 0, +0x94..+0xb3 = 1, byte
// +0x124 = 0, +0x128 = 0; the notification tables (redundant loops);
// +0x2d308, +0x2d304, the UI feedback count +0x15394ac = 0, byte +0x15394b0
// = 0; the statistics history +0x38 (0x2d000 bytes) and +0x2d244 = 0;
// InitStatisticsKernel 0x4ff4c0; g_unk7deb4c = 0 and the 0x1000 bytes from
// g_syncChecksums 0x7deb58 (the checksum table and what follows it, up to
// g_syncRandomHistory) = 0.
//
// Evidence: in-process test on two full-size worlds (callees stubbed). No
// emulator spec: the function writes the whole 22 MB world, which the
// emulator's per-byte write tracking can't hold; every game start runs it,
// so the whole-game checks cover it.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/raw.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kObjectConstruct = 0x00610320;    // Win32::CObject::CObject
constexpr std::uintptr_t kMapEffectsConstruct = 0x005442c0;
constexpr std::uintptr_t kBuildOffsetTable = 0x00516530;
constexpr std::uintptr_t kInitReciprocals = 0x004a1eb0;
constexpr std::uintptr_t kSetPromotionCost = 0x00508450;   // (player)
constexpr std::uintptr_t kInitStatisticsKernel = 0x004ff4c0;

constexpr std::size_t kMapEffects = 0x14d2bc8;  // the MapEffects member (raw-ok: region start, re/world_layout.md)
constexpr std::size_t kPlayerScratch = 0x1195d7c;  // int32[20] zeroed per player (raw-ok: no other use known)
constexpr std::size_t kSyncBlockBytes = 0x1000;    // zeroed from g_syncChecksums up to g_syncRandomHistory

// A region the constructor zeroes with rep stos: offset, dwords (raw-ok:
// the original's block sizes, re/world_layout.md names them).
struct Block {
    std::size_t offset;
    std::size_t dwords;
};

// A table whose high-water mark is cleared, zeroed, and its records marked.
struct Table {
    std::size_t mark;      // the high-water mark dword
    std::size_t offset;    // the first record
    std::size_t dwords;    // rep stos count
    std::size_t stride;    // record size
    std::size_t records;
    std::uint8_t marker;   // byte 0 of each record
};

std::uint8_t* Bytes(CGameData* world) {
    return reinterpret_cast<std::uint8_t*>(world);  // raw-ok: the constructor clears whole regions by size
}

void Zero(CGameData* world, const Block& block) {
    std::memset(Bytes(world) + block.offset, 0, block.dwords * 4);
}

void Mark(CGameData* world, std::size_t offset, std::size_t stride, std::size_t records, std::uint8_t value) {
    for (std::size_t r = 0; r < records; ++r) {
        Bytes(world)[offset + r * stride] = value;
    }
}

void ResetTable(CGameData* world, const Table& t) {
    RawAt<std::uint32_t>(world, t.mark) = 0;
    std::memset(Bytes(world) + t.offset, 0, t.dwords * 4);
    Mark(world, t.offset, t.stride, t.records, t.marker);
}

template <typename... Args>
std::uint32_t CallWorld(std::uintptr_t address, void* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// 0x4fca7e..0x4fcbdb: the record tables (raw-ok: offsets and sizes as the
// original's rep stos and marking loops have them).
void ResetRecordTables(CGameData* world) {
    // Effects +0x36700 (10000 x 0x14; word 0 = 0), mark +0x67440.
    RawAt<std::uint32_t>(world, 0x67440) = 0;
    std::memset(Bytes(world) + 0x36700, 0, 0xc350 * 4);
    for (std::size_t r = 0; r < 10000; ++r) {
        RawAt<std::uint16_t>(world, 0x36700 + r * 0x14) = 0;
    }
    ResetTable(world, {0x12a5e5c, 0x12a5e60, 0x7d000, 0x40, 32000, 0xff});  // settlers
    ResetTable(world, {0x1499e60, 0x1499e64, 0xe290, 0x3a, 4000, 0xff});    // buildings
    Zero(world, {0xe114dc, 0x40});                                         // walk-region ids (twice)
    Zero(world, {0xe114dc, 0x40});
    ResetTable(world, {0xe114d8, 0xe145e8, 0xb8000, 0x2e0, 4096, 0xff});   // areas
    RawAt<std::uint32_t>(world, 0xe115dc) = 0;                             // area mark
    Zero(world, {0xe115e4, 0x400});                                        // 4096-id table (twice)
    Zero(world, {0xe115e4, 0x400});
    RawAt<std::uint32_t>(world, 0xe115e0) = 0;
    Zero(world, {0xe125e8, 0x800});                                        // 8192-id table (twice)
    Zero(world, {0xe125e8, 0x800});
    ResetTable(world, {0xe125e4, 0x10f45e8, 0x57e4, 0x5a, 1000, 0xff});    // trade records
    RawAt<std::uint32_t>(world, 0x110a578) = 0;                            // trade mark
    ResetTable(world, {0x1238858, 0x123885c, 0x1b580, 0xe, 32000, 0});     // piles
    std::memset(Bytes(world) + 0x1196024, 0, 0x233ac * 4);                 // levelling jobs (20 x 30)
    Mark(world, 0x1196024, 0x3c2, 600, 0);
    ResetTable(world, {0x14d28a4, 0x14d28a8, 0xc8, 0x10, 50, 0});          // reveal points
    // Presence maps A and B, then the redundant byte loop over both.
    Zero(world, {0xde7448, 0x2d00});
    Zero(world, {0xdf2848, 0x2d00});
}

// 0x4fcc49..0x4fcce3 (raw-ok: the original's blocks).
constexpr Block kBlocks[] = {
    {0x67444, 0x360000},  // (from the effects' end on: the tile map etc.)
    {0x2f990, 0xbb8},     // ring: AI events
    {0x32878, 0xfa0},     // ring: events
    {0x2d320, 0x4b0},     // notifications
    {0x2e5e0, 0x4ec},     // notification timers
    {0x1158788, 0xc73d},  // territory claims
    {0x1195fd4, 0x14},    // per-player race copy
    {0x1223656, 0x5480},  // minimap image
    {0x118a47c, 0x2710},  // player settler lists
};

void ResetPlayer(CGameData* world, std::uint32_t p) {
    layout::Player& player = world->players[p];
    std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(&player);
    RawAt<std::int32_t>(world, kPlayerScratch + 4 * p) = 0;
    // (raw-ok: the player record's fields as the constructor sets them;
    // their names, where known, are in layout::Player)
    RawAt<std::int32_t>(bytes, 0x00) = 0;
    RawAt<std::int32_t>(bytes, 0x18) = 0;
    RawAt<std::int32_t>(bytes, 0x1c) = -1;
    RawAt<std::int32_t>(bytes, 0x80) = 0x80;
    RawAt<std::int32_t>(bytes, 0x8c) = 0;
    RawAt<std::int32_t>(bytes, 0x90) = 0;
    for (std::size_t at = 0xb8; at <= 0xd8; at += 4) {
        RawAt<std::int32_t>(bytes, at) = 0;
    }
    RawAt<std::int32_t>(bytes, 0x84) = 0xffff;
    CallWorld(kSetPromotionCost, world, p);
    std::memset(bytes + 0x20, 0, 0x18 * 4);
    for (std::size_t at = 0x94; at < 0xb4; at += 4) {
        RawAt<std::int32_t>(bytes, at) = 1;
    }
    bytes[0x124] = 0;
    RawAt<std::int32_t>(bytes, 0x128) = 0;
}

}  // namespace

using GameDataConstructFn = CGameData*(__fastcall*)(CGameData* self, void* edx);

// Purpose: Builds an empty world: every table cleared and its records
// marked free, the player records set to their starting values, the
// offset, reciprocal and statistics tables computed.
CGameData* __fastcall CGameData_Construct(CGameData* self, void*) {
    CallWorld(kObjectConstruct, self);
    CallWorld(kMapEffectsConstruct, Bytes(self) + kMapEffects);
    RawAt<std::uint32_t>(self, 0) = vtables::kCGameData;
    GameDebugLog(texts::kInitialisingGlobals);
    RawAt<std::uint32_t>(self, 0x32870) = 0;  // (raw-ok: the AI event ring's indexes)
    RawAt<std::uint32_t>(self, 0x32874) = 0;
    RawAt<std::uint32_t>(self, 0x366f8) = 0;  // (raw-ok: the event ring's indexes)
    RawAt<std::uint32_t>(self, 0x366fc) = 0;
    g_stepCountdown = 0;
    g_fastForwarding = 0;
    g_unk7dfd34 = 0;
    g_unk7dfd38 = 0;
    g_unk7dfd3c = 0;
    g_logicStartState = 0;
    g_gameTick = 0;
    g_lastDrawnTick = 0;
    g_hitAreaReadCount = 0;
    g_hitAreaDrawCount = 0;
    g_hitAreaReadHalf = 0;
    g_unk730f4c = 1;
    g_unk7dfd70 = 0;
    g_unk7dfd74 = 0;
    g_unk7dfd78 = 0;
    g_unk7dfd7c = 0;
    g_unk730f50 = 3;
    CallWorld(kBuildOffsetTable, self);
    CallWorld(kInitReciprocals, self);
    ResetRecordTables(self);
    for (const Block& block : kBlocks) {
        Zero(self, block);
    }
    std::memset(Bytes(self) + 0x15394b1, 0, 0x77);  // VictoryConditions (raw-ok)
    Zero(self, {0x1195dd4, 0x80});                   // pile claims
    g_pileClaimsCursor = 0;
    RawAt<std::int32_t>(self, 0x2d264) = 0;  // (raw-ok: UI scalars, re/world_layout.md)
    RawAt<std::int32_t>(self, 0x2d278) = 1;
    RawAt<std::uint8_t>(self, 0x2d250) = 0;
    RawAt<std::int32_t>(self, 0x2d274) = 0;
    std::memset(&self->players[0], 0, sizeof(self->players));
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        ResetPlayer(self, p);
    }
    RawAt<std::int32_t>(self, 0x2d308) = 0;  // tutorial step (raw-ok)
    RawAt<std::int32_t>(self, 0x2d304) = 0;  // tutorial wait (raw-ok)
    RawAt<std::uint32_t>(self, 0x15394ac) = 0;  // UI feedback count (raw-ok)
    RawAt<std::uint8_t>(self, 0x15394b0) = 0;
    std::memset(Bytes(self) + 0x38, 0, 0xb400 * 4);  // statistics history (raw-ok)
    RawAt<std::int32_t>(self, 0x2d244) = 0;
    CallWorld(kInitStatisticsKernel, self);
    g_unk7deb4c = 0;
    std::memset(&g_syncChecksums.get(), 0, kSyncBlockBytes);
    return self;
}

S3RE_HOOK(0x004fc990, CGameData_Construct, GameDataConstructFn, Original_CGameData_Construct, CGameData_Construct);

// ---------------------------------------------------------------------------
// Tests: two full-size worlds with the same random bytes; the callees are
// recording stubs (SetPromotionCost writes a random field of the player,
// which the zeroing after it must overwrite in both); the globals the
// constructor writes are saved and restored. Compared: the whole worlds,
// the globals, the calls, the DebugLog calls, EAX (as world-relative).
// ---------------------------------------------------------------------------

namespace {

struct GlobalsSnapshot {
    std::uint32_t values[20];
    std::vector<std::uint8_t> sync;
    std::uint16_t cursor;
    bool operator==(const GlobalsSnapshot& o) const {
        return std::memcmp(values, o.values, sizeof(values)) == 0 && sync == o.sync && cursor == o.cursor;
    }
};

GlobalsSnapshot TakeGlobals() {
    GlobalsSnapshot s{};
    const std::uint32_t v[] = {g_stepCountdown, static_cast<std::uint32_t>(g_fastForwarding.get()),
                               static_cast<std::uint32_t>(g_unk7dfd34.get()), static_cast<std::uint32_t>(g_unk7dfd38.get()),
                               static_cast<std::uint32_t>(g_unk7dfd3c.get()), g_logicStartState, g_gameTick, g_lastDrawnTick,
                               static_cast<std::uint32_t>(g_hitAreaReadCount.get()), static_cast<std::uint32_t>(g_hitAreaDrawCount.get()),
                               static_cast<std::uint32_t>(g_hitAreaReadHalf.get()), static_cast<std::uint32_t>(g_unk730f4c.get()),
                               static_cast<std::uint32_t>(g_unk7dfd70.get()), static_cast<std::uint32_t>(g_unk7dfd74.get()),
                               static_cast<std::uint32_t>(g_unk7dfd78.get()), static_cast<std::uint32_t>(g_unk7dfd7c.get()),
                               static_cast<std::uint32_t>(g_unk730f50.get()), static_cast<std::uint32_t>(g_unk7deb4c.get()), 0, 0};
    std::memcpy(s.values, v, sizeof(v));
    s.sync.resize(kSyncBlockBytes);
    std::memcpy(s.sync.data(), &g_syncChecksums.get(), kSyncBlockBytes);
    s.cursor = g_pileClaimsCursor;
    return s;
}

void RestoreGlobals(const GlobalsSnapshot& s) {
    g_stepCountdown = s.values[0];
    g_fastForwarding = static_cast<std::int32_t>(s.values[1]);
    g_unk7dfd34 = static_cast<std::int32_t>(s.values[2]);
    g_unk7dfd38 = static_cast<std::int32_t>(s.values[3]);
    g_unk7dfd3c = static_cast<std::int32_t>(s.values[4]);
    g_logicStartState = s.values[5];
    g_gameTick = s.values[6];
    g_lastDrawnTick = s.values[7];
    g_hitAreaReadCount = static_cast<std::int32_t>(s.values[8]);
    g_hitAreaDrawCount = static_cast<std::int32_t>(s.values[9]);
    g_hitAreaReadHalf = static_cast<std::int32_t>(s.values[10]);
    g_unk730f4c = static_cast<std::int32_t>(s.values[11]);
    g_unk7dfd70 = static_cast<std::int32_t>(s.values[12]);
    g_unk7dfd74 = static_cast<std::int32_t>(s.values[13]);
    g_unk7dfd78 = static_cast<std::int32_t>(s.values[14]);
    g_unk7dfd7c = static_cast<std::int32_t>(s.values[15]);
    g_unk730f50 = static_cast<std::int32_t>(s.values[16]);
    g_unk7deb4c = static_cast<std::int32_t>(s.values[17]);
    std::memcpy(&g_syncChecksums.get(), s.sync.data(), kSyncBlockBytes);
    g_pileClaimsCursor = s.cursor;
}

struct Outcome {
    std::uint32_t eax;
    GlobalsSnapshot globals;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
};

Outcome Run(GameDataConstructFn f, CGameData* world, const GlobalsSnapshot& start, std::uint64_t seed) {
    RestoreGlobals(start);
    Outcome out;
    test::Rng rng(seed);
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    stubs.Install<0, kObjectConstruct>();
    stubs.Install<0, kMapEffectsConstruct>();
    stubs.Install<0, kBuildOffsetTable>();
    stubs.Install<0, kInitReciprocals>();
    stubs.Install<0, kInitStatisticsKernel>();
    stubs.Install<1, kSetPromotionCost>([&rng](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        // A write into the part of the player record the constructor zeroes next.
        auto* bytes = reinterpret_cast<std::uint8_t*>(&w->players[args[0] % layout::kPlayers]);
        bytes[0x20 + rng.Below(0x60)] = static_cast<std::uint8_t>(rng.Next32());
        return 0u;
    });
    DebugLogRecorder logs;
    out.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(f(world, nullptr)) -
                                         reinterpret_cast<std::uintptr_t>(world));
    out.logs = logs.Take();
    out.calls = stubs.Take();
    out.globals = TakeGlobals();
    return out;
}

void CompareConstruct(test::Context& ctx, GameDataConstructFn candidate, int cases) {
    const GlobalsSnapshot saved = TakeGlobals();
    for (int i = 0; i < cases; ++i) {
        test::WorldPair worlds(ctx, sizeof(CGameData));
        GlobalsSnapshot start = saved;
        for (std::uint32_t& v : start.values) {
            v = ctx.rng().Next32();
        }
        ctx.rng().Fill(start.sync.data(), start.sync.size());
        start.cursor = static_cast<std::uint16_t>(ctx.rng().Next32());
        const std::uint64_t seed = ctx.rng().Next64();
        const Outcome expected = Run(Original_CGameData_Construct, worlds.original(), start, seed);
        const Outcome actual = Run(candidate, worlds.ours(), start, seed);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT(expected.globals == actual.globals);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        same &= worlds.Same(ctx, 0, sizeof(CGameData));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    RestoreGlobals(saved);
}

// Wrong: the buildings' free marker is left out.
CGameData* __fastcall WrongConstruct(CGameData* self, void* edx) {
    CGameData* result = CGameData_Construct(self, edx);
    Bytes(self)[0x1499e64] = 0;
    return result;
}

}  // namespace

S3RE_TEST(CGameData_Construct_MatchesOriginal) {
    CompareConstruct(ctx, &CGameData_Construct, 3);
}

S3RE_TEST(CGameData_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareConstruct(probe, &WrongConstruct, 1);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
