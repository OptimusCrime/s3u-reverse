// CGameData::FinishInitialising (0x004fce90, was Logic_sub_4fce90; named
// after its DebugLog "*** GAMEDATA Initialising done"): () thiscall, called
// once by CLogicThread::Step on the first step (g_logicStartState 0xfa),
// after the world was loaded. Resets the UI state in the world and in
// S3.EXE's data:
//   - g_unk7dfd40, g_unk7dfd34..3c = 0; refresh_ui = draw_flag_2d280 = 1;
//     the 64 minimap slice flags = 0;
//   - landscape_y = 0, landscape_x = 0, landscape_step = 13; last_search_x /
//     _y = -1; ui_cursor_mode = 6;
//   - the UI block ui_selection_mode .. notification_blink (+0x2d284 ..
//     +0x2d2c8), ui_2d268[3] and the notification jump fields (+0x2d314..) = 0;
//   - no hit areas (g_hitAreaReadCount / DrawCount / ReadHalf = 0);
//     search_generation = 1, draw_call_counter = 0;
//   - the ten settler groups empty and unchanged, group 0 current;
//   - MapEffects::SetMapSize(map size, 200) on the effect table;
//   - chart_max = 0; with new game objectives (objectives_enabled) the UI
//     starts in selection mode 11;
//   - DebugLog("*** GAMEDATA Initialising done").
// EAX not used (the caller ignores it).

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/settlers/settler_groups.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kSetMapSize = 0x00544320;  // MapEffects::SetMapSize (a, b), ECX = the table
constexpr std::int32_t kEffectMargin = 200;          // its second argument (pixels around the map)
constexpr std::int32_t kStartCursorMode = 6;
constexpr std::uint32_t kStartLandscapeStep = 13;
constexpr std::int32_t kObjectivesMenu = 11;          // ui_selection_mode with new game objectives

}  // namespace

using WorldFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Puts the user interface state into its start state once the
// world of a new or loaded game is ready (nothing selected, no groups, the
// effect grid sized for the map).
std::uint32_t __fastcall CGameData_FinishInitialising(CGameData* world, void*) {
    g_unk7dfd40 = 0;
    world->refresh_ui = 1;
    world->draw_flag_2d280 = 1;
    std::memset(&g_minimapSliceDirty.get(), 0, sizeof(g_minimapSliceDirty.get()));
    world->landscape_y = 0;
    world->last_search_x = -1;
    world->last_search_y = -1;
    world->ui_cursor_mode = kStartCursorMode;
    world->landscape_x = 0;
    world->landscape_step = kStartLandscapeStep;
    // +0x2d284 .. +0x2d2c8: selection mode and parameters, special menu, ...
    world->ui_selection_mode = 0;
    world->ui_selection_param1 = 0;
    world->ui_selection_param2 = 0;
    std::memset(world->ui_2d290, 0, sizeof(world->ui_2d290));
    world->ui_special_menu = 0;
    world->ui_2d29c = 0;
    world->ui_2d2a0 = 0;
    std::memset(world->ui_2d2a4, 0, sizeof(world->ui_2d2a4));
    world->notification_blink = 0;
    std::memset(world->ui_2d268, 0, sizeof(world->ui_2d268));
    g_hitAreaReadCount = 0;
    g_hitAreaDrawCount = 0;
    g_hitAreaReadHalf = 0;
    world->search_generation = 1;
    world->draw_call_counter = 0;
    g_unk7dfd34 = 0;
    g_unk7dfd38 = 0;
    g_unk7dfd3c = 0;
    world->jump_return_x = 0;
    world->jump_return_y = 0;
    world->jump_tick = 0;
    std::memset(&g_settlerGroupChanged.get(), 0, sizeof(g_settlerGroupChanged.get()));
    std::memset(GlobalSettlerGroups().counts, 0, sizeof(GlobalSettlerGroups().counts));
    g_currentSettlerGroup = 0;
    using SetMapSizeFn = std::int32_t(__fastcall*)(layout::MapEffectTable*, void*, std::int32_t, std::int32_t);
    reinterpret_cast<SetMapSizeFn>(kSetMapSize)(&world->map_effects, nullptr, static_cast<std::int32_t>(world->map_size),
                                                kEffectMargin);
    world->chart_max = 0;
    if (world->objectives_enabled != 0) {
        world->ui_selection_mode = kObjectivesMenu;
    }
    GameDebugLog(texts::kGameDataInitialisingDone);
    return 0;
}

S3RE_HOOK(0x004fce90, CGameData_FinishInitialising, WorldFn, Original_CGameData_FinishInitialising,
          CGameData_FinishInitialising);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// The S3.EXE data the function writes, saved and compared as one picture.
struct ExeState {
    std::int32_t dirty[64];
    std::int32_t changed[10];
    SettlerGroups groups;
    std::int32_t current, hitRead, hitDraw, hitHalf, u34, u38, u3c, u40;

    void Capture() {
        std::memcpy(dirty, &g_minimapSliceDirty.get(), sizeof(dirty));
        std::memcpy(changed, &g_settlerGroupChanged.get(), sizeof(changed));
        groups = g_settlerGroups.get();
        current = g_currentSettlerGroup;
        hitRead = g_hitAreaReadCount;
        hitDraw = g_hitAreaDrawCount;
        hitHalf = g_hitAreaReadHalf;
        u34 = g_unk7dfd34;
        u38 = g_unk7dfd38;
        u3c = g_unk7dfd3c;
        u40 = g_unk7dfd40;
    }
    void Restore() const {
        std::memcpy(&g_minimapSliceDirty.get(), dirty, sizeof(dirty));
        std::memcpy(&g_settlerGroupChanged.get(), changed, sizeof(changed));
        g_settlerGroups.get() = groups;
        g_currentSettlerGroup = current;
        g_hitAreaReadCount = hitRead;
        g_hitAreaDrawCount = hitDraw;
        g_hitAreaReadHalf = hitHalf;
        g_unk7dfd34 = u34;
        g_unk7dfd38 = u38;
        g_unk7dfd3c = u3c;
        g_unk7dfd40 = u40;
    }
};

void CompareFinishInitialising(test::Context& ctx, WorldFn candidate, int cases) {
    ExeState saved;
    saved.Capture();
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (int i = 0; i < cases; ++i) {
        // Random S3.EXE state, the same for both runs.
        ExeState start;
        ctx.rng().Fill(&start, sizeof(start));
        const std::uint8_t objectives = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32());
        const std::uint32_t size = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 64 + ctx.rng().Below(704);
        worlds.Both([&](CGameData* w) {
            w->objectives_enabled = objectives;
            w->map_size = size;
        });
        start.Restore();
        std::vector<DebugLogCall> logA, logB;
        {
            DebugLogRecorder recorder;
            Original_CGameData_FinishInitialising(worlds.original(), nullptr);
            logA = recorder.Take();
        }
        ExeState expected;
        expected.Capture();
        start.Restore();
        {
            DebugLogRecorder recorder;
            candidate(worlds.ours(), nullptr);
            logB = recorder.Take();
        }
        ExeState actual;
        actual.Capture();
        bool same = S3RE_EXPECT_MEM_EQ(&expected, &actual, sizeof(expected));
        same &= S3RE_EXPECT(logA == logB);
        same &= worlds.Checkpoint(ctx);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    saved.Restore();
}

// Wrong: the selection mode for new game objectives is not set.
std::uint32_t __fastcall WrongFinishInitialising(CGameData* world, void* edx) {
    const std::uint32_t r = CGameData_FinishInitialising(world, edx);
    world->ui_selection_mode = 0;
    return r;
}

}  // namespace

S3RE_TEST(CGameData_FinishInitialising_MatchesOriginal) {
    CompareFinishInitialising(ctx, &CGameData_FinishInitialising, 20);
}
S3RE_TEST(CGameData_FinishInitialising_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFinishInitialising(probe, &WrongFinishInitialising, 10);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
