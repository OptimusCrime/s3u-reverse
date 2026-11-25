// CGameData::InitAfterMapLoad (0x004fd020): () thiscall, after a map (not a
// savegame) was loaded into a new world (CLogicThread::OnStart). In order:
// InitTileShadesAndMapEdge 0x519e80, RebuildFogOfWar 0x518e70 twice,
// BuildMinimap 0x4fb650, ClearPlayerListLengths 0x50c9c0, InitPlayerInfo
// 0x4fd0d0; counter_110a57c = 0, camera_drawn_x/y (+0x2d254/+0x2d258) =
// 10,000,000 (so the first frame redraws everything, probably);
// CenterCameraOnLocalPlayer 0x51b6b0; fog_cycle = 0, minimap_cycle = 63;
// then for every active player (active != 0): the 48 settler-list heads
// cleared and spell_mana[i] = g_spellInitialMana[g_raceSpells[race][i]]
// for its eight spells (the race unchecked, as the original). EAX unused.

#include "game/tables.h"
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

constexpr std::uintptr_t kInitTileShadesAndMapEdge = 0x00519e80;
constexpr std::uintptr_t kRebuildFogOfWar = 0x00518e70;
constexpr std::uintptr_t kBuildMinimap = 0x004fb650;
constexpr std::uintptr_t kClearPlayerListLengths = 0x0050c9c0;
constexpr std::uintptr_t kInitPlayerInfo = 0x004fd0d0;
constexpr std::uintptr_t kCenterCameraOnLocalPlayer = 0x0051b6b0;

constexpr std::int32_t kCameraNotDrawn = 10000000;
constexpr std::uint32_t kMinimapCycleStart = 63;
constexpr std::uint32_t kSpellsPerRace = 8;

void Call(std::uintptr_t address, CGameData* world) {
    reinterpret_cast<void(__fastcall*)(CGameData*, void*)>(address)(world, nullptr);
}

}  // namespace

using InitAfterMapLoadFn = void(__fastcall*)(CGameData* world, void* edx);
void __fastcall CGameData_InitAfterMapLoad(CGameData* world, void* edx);
S3RE_HOOK(0x004fd020, CGameData_InitAfterMapLoad, InitAfterMapLoadFn, Original_CGameData_InitAfterMapLoad,
          CGameData_InitAfterMapLoad);

// Purpose: Finishes a world built from a map: shades, fog of war, minimap,
// the players' info and start mana, the camera on the local player.
void __fastcall CGameData_InitAfterMapLoad(CGameData* world, void*) {
    Call(kInitTileShadesAndMapEdge, world);
    Call(kRebuildFogOfWar, world);
    Call(kRebuildFogOfWar, world);
    Call(kBuildMinimap, world);
    Call(kClearPlayerListLengths, world);
    Call(kInitPlayerInfo, world);
    world->counter_110a57c = 0;
    world->camera_drawn_x = kCameraNotDrawn;
    world->camera_drawn_y = kCameraNotDrawn;
    Call(kCenterCameraOnLocalPlayer, world);
    world->fog_cycle = 0;
    world->minimap_cycle = kMinimapCycleStart;
    for (layout::Player& player : world->players) {
        if (player.active == 0) {
            continue;
        }
        std::memset(player.settler_lists, 0, sizeof(player.settler_lists));
        for (std::uint32_t i = 0; i < kSpellsPerRace; ++i) {
            // 32-bit index arithmetic, as the original (the race is not checked).
            const std::uint32_t spell =
                (&g_raceSpells[0][0])[static_cast<std::uint32_t>(player.race) * kSpellsPerRace + i];
            player.spell_mana[i] = static_cast<std::int32_t>((&g_spellInitialMana[0])[spell]);
        }
    }
}

// ---------------------------------------------------------------------------
// Tests: random worlds up to the end of the players; the six callees
// replaced by recording stubs (their order compared); races 0..4 (race 4 is
// past the table, as in the original).
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, players) + sizeof(CGameData::players) + 64;

std::vector<test::StubCall> RunInit(InitAfterMapLoadFn fn, CGameData* world) {
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    stubs.Install<0, kInitTileShadesAndMapEdge>();
    stubs.Install<0, kRebuildFogOfWar>();
    stubs.Install<0, kBuildMinimap>();
    stubs.Install<0, kClearPlayerListLengths>();
    stubs.Install<0, kInitPlayerInfo>();
    stubs.Install<0, kCenterCameraOnLocalPlayer>();
    fn(world, nullptr);
    return stubs.Take();
}

void CompareInit(test::Context& ctx, InitAfterMapLoadFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        test::WorldPair worlds(ctx, kTestWorld);
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            const std::int32_t active = ctx.rng().Chance(1, 3) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32() | 1);
            const std::int32_t race = static_cast<std::int32_t>(ctx.rng().Below(5));
            worlds.Both([&](CGameData* w) {
                w->players[p].active = active;
                w->players[p].race = race;
            });
        }
        const auto callsA = RunInit(Original_CGameData_InitAfterMapLoad, worlds.original());
        const auto callsB = RunInit(candidate, worlds.ours());
        if (!S3RE_EXPECT(callsA == callsB) || !worlds.Same(ctx, 0, kTestWorld)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: an inactive player's lists are cleared too.
void __fastcall WrongInitAfterMapLoad(CGameData* world, void* edx) {
    CGameData_InitAfterMapLoad(world, edx);
    for (layout::Player& player : world->players) {
        if (player.active == 0) {
            player.settler_lists[0] = 0;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_InitAfterMapLoad_MatchesOriginal) {
    CompareInit(ctx, &CGameData_InitAfterMapLoad, 4);
}

S3RE_TEST(CGameData_InitAfterMapLoad_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInit(probe, &WrongInitAfterMapLoad, 2);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
