// Small queries on UI state that lives in the world object (see the
// "UI state lives inside the world object" notes in CLAUDE.md), all called
// from the UI code around 0x53fbb0.
//
// CGameData::IsUiSelectionMode (0x005157b0): (mode) -> 1 if the UI
//   selection mode (+0x2d284) equals mode, else 0.
// CGameData::IsUiSelectionParam1 (0x005157f0): the same for the selection's
//   first parameter (+0x2d288).
// CGameData::IsUiSelectionParam2 (0x00515810): the same for the second
//   parameter (+0x2d28c).
// CGameData::IsUiField298 (0x005157d0): the same for +0x2d298 (meaning
//   unknown; written by ProcessEvents and 0x542660/0x542990/0x542b60).
// CGameData::IsUiSelectionMode4Field298Is2 (0x00515830): () -> 1 if the
//   selection mode is 4 and +0x2d298 is 2.
// CGameData::IsGameOutcome1 (0x00515ad0): (unused) -> 1 if the global game
//   outcome (0x730f64) is 1. Its stack argument is ignored.
// All compare full 32-bit values. thiscall (ECX = world), EAX = 1 or 0.
//
// CGameData::ScreenCenterTileX/Y (0x005158f0/0x00515980): the tile at the
//   centre of the game view, from the camera position in pixels
//   (+0x2d25c x, +0x2d260 y), the view size (globals 0x730f18 width,
//   0x730f1c height) and the map size. Tile rows are 9 pixels high
//   ("/ 18" and "/ 9" below) and tiles 16 pixels wide; the map is drawn
//   skewed, hence the y terms in X. All divisions truncate toward zero
//   (signed, as the original's multiply-by-reciprocal code does); sums wrap
//   at 32 bits; the `>> 1`/`>> 2` are unsigned shifts. No arguments, ret 0.
//
// CGameData::ClampCamera (0x0051b4d0): keeps the camera inside the map:
//   x in [0, mapSize*24 - width + 0x167], y in [0, mapSize*9 - width +
//   0x176] (signed compares; the y limit really uses the view width
//   0x730f18, not the height). Only writes a coordinate that is out of
//   range. Called after camera moves in ProcessEvents and by 0x51b560,
//   which returns its EAX: mapSize*9 - width, plus 0x176 if y was clamped
//   at the top. No arguments, ret 0.
//
// CGameData::PushUiFeedback (0x004febe0): (value) appends value to the
//   10-entry dword list at +0x1539484 (count at +0x15394ac) unless it is
//   full. Drained by CLogicThread::PrepareDrawData. EAX = the new count, or
//   10 when the list was full (callers don't use it, but one tail-calls).
// CLogicThread::PushUiFeedback (0x0053f560): (value) calls
//   CGameData::PushUiFeedback on the logic thread's world (this+0x3a0) and
//   returns its EAX. ECX = CLogicThread. ret 4. 288 call sites in the UI
//   code.
//
// CGameData::CenterCameraOnTile (0x0051b560): (x, y) sets the camera to
//   x = (mapSize + 2x + 0x18 - y) * 8 - width/2 and
//   y = y*9 + 0xc8 - height/2 - tile byte +0x4 of (x, y) (probably the
//   tile's height in pixels), with signed halves of the view size truncated
//   toward zero and 32-bit wrapping sums, then calls CGameData::ClampCamera
//   and returns its EAX (one caller tail-returns it). ret 8.
// CGameData::CenterCameraOnLocalPlayer (0x0051b6b0): CenterCameraOnTile on
//   the position (int32 +0x10, +0x14) of the player record of the local
//   player (+0x2d2d4), then clears the int32 +0x2d264. EAX = the
//   CenterCameraOnTile EAX (one caller tail-returns it). Plain ret.

#include "game/globals.h"
#include "game/layout.h"
#include "game/replacements.h"
#include "game/logic/logic_thread_layout.h"
#include "game/settlers/settler_groups.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::uint32_t kUiFeedbackCapacity = 10;
static_assert(sizeof(CGameData::ui_feedback) / sizeof(CGameData::ui_feedback[0]) == kUiFeedbackCapacity);

// The UI fields are int32 in the layout; the original compares them as
// full 32-bit values (as unsigned, which gives the same equality).
std::uint32_t U32(std::int32_t value) {
    return static_cast<std::uint32_t>(value);
}

// Signed division by a positive constant, truncating toward zero, of a
// 32-bit wrapped sum.
std::uint32_t DivideTruncating(std::uint32_t value, std::int32_t divisor) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) / divisor);
}

// Rows of 9 pixels above the camera centre, offset by 0x640 pixels.
std::uint32_t CameraRow(CGameData* world) {
    return DivideTruncating(U32(world->camera_y) + 0x640u, 18);
}

std::uint32_t ScreenRows(std::uint32_t shift) {
    const std::uint32_t height = g_screenHeight;
    return (DivideTruncating(height + 0xffu, 9) + 0xbu) >> shift;
}

}  // namespace

using WorldValueFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t value);
using WorldQueryFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Whether the user interface is in a given selection mode (e.g. building placement). The UI state
// happens to live inside the world object.
std::uint32_t __fastcall CGameData_IsUiSelectionMode(CGameData* world, void*, std::uint32_t mode) {
    return U32(world->ui_selection_mode) == mode ? 1 : 0;
}

// Purpose: Whether a UI state field (meaning unknown) has a given value.
std::uint32_t __fastcall CGameData_IsUiField298(CGameData* world, void*, std::uint32_t value) {
    return U32(world->ui_special_menu) == value ? 1 : 0;
}

// Purpose: Whether the first parameter of the current UI selection (e.g. which building type) has a given
// value.
std::uint32_t __fastcall CGameData_IsUiSelectionParam1(CGameData* world, void*,
                                                       std::uint32_t value) {
    return U32(world->ui_selection_param1) == value ? 1 : 0;
}

// Purpose: Whether the second parameter of the current UI selection has a given value.
std::uint32_t __fastcall CGameData_IsUiSelectionParam2(CGameData* world, void*,
                                                       std::uint32_t value) {
    return U32(world->ui_selection_param2) == value ? 1 : 0;
}

// Purpose: Whether the UI is in selection mode 4 with a particular sub-state. A shortcut check used by the
// UI.
std::uint32_t __fastcall CGameData_IsUiSelectionMode4Field298Is2(CGameData* world, void*) {
    return (U32(world->ui_selection_mode) == 4 && U32(world->ui_special_menu) == 2) ? 1 : 0;
}

// Purpose: Whether the game has ended with outcome 1 (probably 'won'). Queried by the user interface.
std::uint32_t __fastcall CGameData_IsGameOutcome1(CGameData*, void*, std::uint32_t) {
    return g_gameOutcome == 1 ? 1 : 0;
}

// Purpose: Which tile is at the centre of the screen (x). Used e.g. to know where the player is looking.
std::uint32_t __fastcall CGameData_ScreenCenterTileX(CGameData* world, void*) {
    const std::uint32_t rowsBelowTop = 100u - CameraRow(world);
    const std::uint32_t skew = (world->map_size + rowsBelowTop * 2u) >> 1;
    const std::uint32_t width = g_screenWidth;
    const std::uint32_t halfColumns = (DivideTruncating(width + 0x17u, 16) + 0xau) >> 1;
    const std::uint32_t column = DivideTruncating(U32(world->camera_x) - 0xd0u, 16);
    return column + (ScreenRows(2) - skew + halfColumns);
}

// Purpose: Which tile is at the centre of the screen (y).
std::uint32_t __fastcall CGameData_ScreenCenterTileY(CGameData* world, void*) {
    return ScreenRows(1) + CameraRow(world) * 2u - 200u;
}

// Purpose: Keeps the camera inside the map after it has been moved.
std::uint32_t __fastcall CGameData_ClampCamera(CGameData* world, void*) {
    std::int32_t& cameraX = world->camera_x;
    std::int32_t& cameraY = world->camera_y;
    const std::uint32_t size = world->map_size;
    const std::uint32_t width = g_screenWidth;
    if (cameraX < 0) {
        cameraX = 0;
    }
    const std::uint32_t maxX = size * 24u - width + 0x167u;
    if (cameraX >= static_cast<std::int32_t>(maxX + 1u)) {
        cameraX = static_cast<std::int32_t>(maxX);
    }
    if (cameraY < 0) {
        cameraY = 0;
    }
    std::uint32_t result = size * 9u - width;
    if (cameraY >= static_cast<std::int32_t>(result + 0x177u)) {
        result += 0x176u;
        cameraY = static_cast<std::int32_t>(result);
    }
    return result;
}

// Purpose: Queues a small notification from the game for the drawing/sound side (probably sounds or
// messages for the player). The queue holds up to ten.
std::uint32_t __fastcall CGameData_PushUiFeedback(CGameData* world, void*, std::uint32_t value) {
    std::uint32_t& count = world->ui_feedback_count;
    if (count == kUiFeedbackCapacity) {
        return count;
    }
    world->ui_feedback[count] = value;  // (a count above 10 indexes past the list, as in the original)
    count = count + 1;
    return count;
}

// Purpose: Moves the camera so that a tile is in the middle of the screen (e.g. when jumping to a message's
// location).
std::uint32_t __fastcall CGameData_CenterCameraOnTile(CGameData* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    const auto width = static_cast<std::int32_t>(g_screenWidth);
    const auto height = static_cast<std::int32_t>(g_screenHeight);
    const std::uint32_t size = world->map_size;
    world->camera_x = static_cast<std::int32_t>((size + x * 2u + 0x18u - y) * 8u - static_cast<std::uint32_t>(width / 2));
    // Tile (x, y) addressed in 32 bits like the original (x, y unchecked).
    const std::uint32_t height4 = TileAtOffset(world, TileOffset(x, y)).height;
    world->camera_y =
        static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(height / 2) - height4 + y * 9u + 0xc8u);
    return CGameData_ClampCamera(world, nullptr);
}


// Purpose: The same as CGameData::PushUiFeedback, called from the user interface code through the logic
// thread.
std::uint32_t __fastcall CLogicThread_PushUiFeedback(CLogicThread* self, void*, std::uint32_t value) {
    CGameData* world = LogicThreadOf(self)->world;
    return CGameData_PushUiFeedback(world, nullptr, value);
}

using PositionFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using LogicValueFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t value);

// Purpose: Moves the camera to the local player's home position (probably their castle), e.g. at the start
// of a game.
std::uint32_t __fastcall CGameData_CenterCameraOnLocalPlayer(CGameData* world, void*) {
    // (The local player number is not range-checked.)
    const layout::Player& player = world->players[U32(world->local_player)];
    const std::uint32_t result =
        CGameData_CenterCameraOnTile(world, nullptr, U32(player.start_x), U32(player.start_y));
    world->redraw = 0;
    return result;
}

S3RE_HOOK(0x0051b6b0, CGameData_CenterCameraOnLocalPlayer, WorldQueryFn, Original_CGameData_CenterCameraOnLocalPlayer,
          CGameData_CenterCameraOnLocalPlayer);
S3RE_HOOK(0x0051b560, CGameData_CenterCameraOnTile, PositionFn, Original_CGameData_CenterCameraOnTile,
          CGameData_CenterCameraOnTile);
S3RE_HOOK(0x0053f560, CLogicThread_PushUiFeedback, LogicValueFn, Original_CLogicThread_PushUiFeedback,
          CLogicThread_PushUiFeedback);

// CLogicThread::UpdateGroupMenu (0x00542990, our name): (group) picks the
// special menu (+0x2d298) for one of the ten settler groups in S3.EXE's data
// (uint16 counts at 0x7ad620 + group * 2, 100 settler words each at
// 0x7ad634 + group * 200; probably the selection or hotkey groups, the same
// lists RemoveSettlerFromLists edits):
//   - a group of one settler of type 0x24 that belongs to the local player
//     (+0x2d2d4): menu 1 (unless it already is 1), +0x2d29c = 0;
//   - otherwise, if all its settlers have the same type (byte +0x1 of the
//     0x40-byte records at world+0x12a5e60): type 0x2c -> 2, 0x2b -> 3,
//     0x29 -> 4, 0x28 -> 5, 0x16 -> 6, 0x17 -> 7;
//   - otherwise (or for an empty group) the menu is reset to 0 if it was
//     set.
// Each change sets +0x2d27c = 1 (probably "redraw the menu"). Counts and
// types are compared as zero-extended words / bytes, the player byte with
// the full dword +0x2d2d4. EAX = the world pointer on every path. ret 4.
S3RE_HOOK(0x005157b0, CGameData_IsUiSelectionMode, WorldValueFn,
          Original_CGameData_IsUiSelectionMode, CGameData_IsUiSelectionMode);
S3RE_HOOK(0x005157d0, CGameData_IsUiField298, WorldValueFn, Original_CGameData_IsUiField298,
          CGameData_IsUiField298);
S3RE_HOOK(0x005157f0, CGameData_IsUiSelectionParam1, WorldValueFn,
          Original_CGameData_IsUiSelectionParam1, CGameData_IsUiSelectionParam1);
S3RE_HOOK(0x00515810, CGameData_IsUiSelectionParam2, WorldValueFn,
          Original_CGameData_IsUiSelectionParam2, CGameData_IsUiSelectionParam2);
S3RE_HOOK(0x00515830, CGameData_IsUiSelectionMode4Field298Is2, WorldQueryFn,
          Original_CGameData_IsUiSelectionMode4Field298Is2,
          CGameData_IsUiSelectionMode4Field298Is2);
S3RE_HOOK(0x00515ad0, CGameData_IsGameOutcome1, WorldValueFn, Original_CGameData_IsGameOutcome1,
          CGameData_IsGameOutcome1);
S3RE_HOOK(0x005158f0, CGameData_ScreenCenterTileX, WorldQueryFn,
          Original_CGameData_ScreenCenterTileX, CGameData_ScreenCenterTileX);
S3RE_HOOK(0x00515980, CGameData_ScreenCenterTileY, WorldQueryFn,
          Original_CGameData_ScreenCenterTileY, CGameData_ScreenCenterTileY);
S3RE_HOOK(0x0051b4d0, CGameData_ClampCamera, WorldQueryFn, Original_CGameData_ClampCamera,
          CGameData_ClampCamera);
S3RE_HOOK(0x004febe0, CGameData_PushUiFeedback, WorldValueFn, Original_CGameData_PushUiFeedback,
          CGameData_PushUiFeedback);

namespace {


}  // namespace

using GroupMenuFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t group);
std::uint32_t __fastcall CLogicThread_UpdateGroupMenu(CLogicThread* self, void* edx, std::uint32_t group);

S3RE_HOOK(0x00542990, CLogicThread_UpdateGroupMenu, GroupMenuFn, Original_CLogicThread_UpdateGroupMenu,
          CLogicThread_UpdateGroupMenu);

// Purpose: Chooses which special command menu the game shows for a group of
// settlers (e.g. ships or other units with their own commands), probably
// after the group changed or was selected.
std::uint32_t __fastcall CLogicThread_UpdateGroupMenu(CLogicThread* self, void* /*edx*/, std::uint32_t group) {
    CGameData* world = LogicThreadOf(self)->world;
    // The group number is not checked (32-bit address arithmetic, as in the
    // original).
    const std::uint32_t count = GlobalSettlerGroups().counts[group];
    const std::uint16_t* settlers = GlobalSettlerGroups().lists[group];
    auto owner = [&](std::uint32_t settler) { return static_cast<std::uint32_t>(world->settlers[settler].owner); };
    auto type = [&](std::uint32_t settler) { return static_cast<std::uint32_t>(world->settlers[settler].art); };
    std::uint32_t changed = 1;
    if (count == 1) {
        const std::uint32_t settler = settlers[0];
        if (owner(settler) == U32(world->local_player) && type(settler) == 0x24 && U32(world->ui_special_menu) != 1) {
            world->ui_special_menu = 1;
            world->ui_2d29c = 0;
            world->refresh_ui = 1;
            return reinterpret_cast<std::uint32_t>(world);
        }
    }
    if (count != 0) {
        const std::uint32_t firstType = type(settlers[0]);
        bool same = true;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (type(settlers[i]) != firstType) {
                same = false;
            }
        }
        if (same) {
            std::uint32_t menu = 0;
            switch (firstType) {
                case 0x2c: menu = 2; break;
                case 0x2b: menu = 3; break;
                case 0x29: menu = 4; break;
                case 0x28: menu = 5; break;
                case 0x16: menu = 6; break;
                case 0x17: menu = 7; break;
                default: break;
            }
            if (menu != 0) {
                world->ui_special_menu = static_cast<std::int32_t>(menu);
                world->refresh_ui = 1;
                return reinterpret_cast<std::uint32_t>(world);
            }
        }
        changed = 1;
    }
    if (world->ui_special_menu != 0) {
        world->ui_special_menu = 0;
        world->refresh_ui = static_cast<std::int32_t>(changed);
    }
    return reinterpret_cast<std::uint32_t>(world);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kUiWorld = offsetof(CGameData, search_generation);  // the UI block and the map size
constexpr std::size_t kFeedbackWorld = offsetof(CGameData, ui_feedback_count) + 4;
constexpr std::size_t kFeedbackBytes = sizeof(CGameData::ui_feedback) + sizeof(CGameData::ui_feedback_count);
static_assert(offsetof(CGameData, ui_feedback_count) == offsetof(CGameData, ui_feedback) + sizeof(CGameData::ui_feedback));

// Two fake logic threads, each pointing at one of the worlds.
struct ThreadPair {
    explicit ThreadPair(test::WorldPair& worlds) {
        LogicThreadOf(a)->world = worlds.original();
        LogicThreadOf(b)->world = worlds.ours();
    }
    CLogicThread* original() { return reinterpret_cast<CLogicThread*>(a); }
    CLogicThread* ours() { return reinterpret_cast<CLogicThread*>(b); }
    std::uint8_t a[sizeof(CLogicThread)] = {};
    std::uint8_t b[sizeof(CLogicThread)] = {};
};

// Sets a game global for the duration of a test and restores it after.
template <typename T>
class GlobalOverride {
public:
    explicit GlobalOverride(const GameGlobal<T>& global) : global_(global), saved_(global) {}
    ~GlobalOverride() { global_ = saved_; }
    void Set(std::uint32_t value) { global_ = static_cast<T>(value); }

private:
    const GameGlobal<T>& global_;
    T saved_;
};

std::uint32_t InterestingValue(test::Context& ctx) {
    switch (ctx.rng().Below(4)) {
    case 0:
        return ctx.rng().Next32();
    case 1:
        return static_cast<std::uint32_t>(ctx.rng().Range(-3, 3));
    default:
        return ctx.rng().Below(8);
    }
}

// The UI fields, the camera and the globals get values around the
// interesting ones; the whole (small) world is compared after every case.
void CompareUiQuery(test::Context& ctx, WorldValueFn original, WorldValueFn candidate,
                    int cases) {
    test::WorldPair worlds(ctx, kUiWorld);
    GlobalOverride width(g_screenWidth);
    GlobalOverride height(g_screenHeight);
    GlobalOverride outcome(g_gameOutcome);
    for (int i = 0; i < cases; ++i) {
        for (const std::size_t field :
             {offsetof(CGameData, ui_selection_mode), offsetof(CGameData, ui_selection_param1),
              offsetof(CGameData, ui_selection_param2), offsetof(CGameData, ui_special_menu),
              offsetof(CGameData, camera_x), offsetof(CGameData, camera_y), offsetof(CGameData, map_size)}) {
            worlds.Set<std::uint32_t>(field, InterestingValue(ctx));
        }
        width.Set(ctx.rng().Chance(1, 2) ? 640 + ctx.rng().Below(1024) : ctx.rng().Next32());
        height.Set(ctx.rng().Chance(1, 2) ? 480 + ctx.rng().Below(768) : ctx.rng().Next32());
        outcome.Set(InterestingValue(ctx));
        const std::uint32_t value = InterestingValue(ctx);
        const std::uint32_t expected = original(worlds.original(), nullptr, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: argument %#x", i, value);
            return;
        }
    }
}

// Adapts a function without arguments to CompareUiQuery.
template <WorldQueryFn* Original, WorldQueryFn Ours>
struct IgnoreArgument {
    static std::uint32_t __fastcall CallOriginal(CGameData* world, void* edx, std::uint32_t) {
        return (*Original)(world, edx);
    }
    static std::uint32_t __fastcall CallOurs(CGameData* world, void* edx, std::uint32_t) {
        return Ours(world, edx);
    }
};

void ComparePushUiFeedback(test::Context& ctx, WorldValueFn original, WorldValueFn candidate,
                           int cases) {
    test::WorldPair worlds(ctx, kFeedbackWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(kUiFeedbackCapacity + 1);
        worlds.Both([&](CGameData* w) { w->ui_feedback_count = count; });
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = original(worlds.original(), nullptr, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(CGameData, ui_feedback), kFeedbackBytes) ||
            (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: rounds the column down instead of toward zero.
std::uint32_t __fastcall WrongScreenCenterTileX(CGameData* world, void* edx) {
    const auto column = static_cast<std::int32_t>(U32(world->camera_x) - 0xd0u);
    return CGameData_ScreenCenterTileX(world, edx) - DivideTruncating(column, 16) +
           static_cast<std::uint32_t>(column >> 4);
}

// Wrong: uses the view height for the y limit.
std::uint32_t __fastcall WrongClampCamera(CGameData* world, void* edx) {
    const std::uint32_t width = g_screenWidth;
    g_screenWidth = g_screenHeight.get();
    std::int32_t x = world->camera_x;
    const std::uint32_t result = CGameData_ClampCamera(world, edx);
    g_screenWidth = width;
    world->camera_x = x;
    const std::uint32_t size = world->map_size;
    if (x < 0) {
        x = 0;
    }
    const std::uint32_t maxX = size * 24u - width + 0x167u;
    if (x >= static_cast<std::int32_t>(maxX + 1u)) {
        x = static_cast<std::int32_t>(maxX);
    }
    world->camera_x = x;
    return result;
}

}  // namespace

S3RE_TEST(CGameData_CenterCameraOnTile_MatchesOriginal) {
    test::WorldPair worlds(ctx, offsetof(CGameData, tiles) + 400 * kTileRowBytes);
    GlobalOverride width(g_screenWidth);
    GlobalOverride height(g_screenHeight);
    for (int i = 0; i < 5000; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        width.Set(ctx.rng().Chance(3, 4) ? 640 + ctx.rng().Below(1024) : InterestingValue(ctx));
        height.Set(ctx.rng().Chance(3, 4) ? 480 + ctx.rng().Below(768) : InterestingValue(ctx));
        const std::uint32_t x = ctx.rng().Below(400);
        const std::uint32_t y = ctx.rng().Below(400);
        const std::uint32_t expected = Original_CGameData_CenterCameraOnTile(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = CGameData_CenterCameraOnTile(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(CGameData, camera_x), 8) ||
            (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u)", i, x, y);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(CGameData_CenterCameraOnLocalPlayer_MatchesOriginal) {
    test::WorldPair worlds(ctx, offsetof(CGameData, players) + 21 * sizeof(layout::Player));
    GlobalOverride width(g_screenWidth);
    GlobalOverride height(g_screenHeight);
    for (int i = 0; i < 3000; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        width.Set(640 + ctx.rng().Below(1024));
        height.Set(480 + ctx.rng().Below(768));
        const std::uint32_t player = ctx.rng().Below(20);
        const auto startX = static_cast<std::int32_t>(ctx.rng().Below(384));
        const auto startY = static_cast<std::int32_t>(ctx.rng().Below(384));
        worlds.Both([&](CGameData* w) {
            w->local_player = static_cast<std::int32_t>(player);
            w->players[player].start_x = startX;
            w->players[player].start_y = startY;
        });
        const std::uint32_t expected = Original_CGameData_CenterCameraOnLocalPlayer(worlds.original(), nullptr);
        const std::uint32_t actual = CGameData_CenterCameraOnLocalPlayer(worlds.ours(), nullptr);
        // camera_x, camera_y and redraw
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(CGameData, camera_x), 12) ||
            (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(CLogicThread_PushUiFeedback_MatchesOriginal) {
    test::WorldPair worlds(ctx, kFeedbackWorld);
    ThreadPair threads(worlds);
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t count = ctx.rng().Below(kUiFeedbackCapacity + 1);
        worlds.Both([&](CGameData* w) { w->ui_feedback_count = count; });
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = Original_CLogicThread_PushUiFeedback(threads.original(), nullptr, value);
        const std::uint32_t actual = CLogicThread_PushUiFeedback(threads.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(CGameData, ui_feedback), kFeedbackBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(CGameData_IsUiSelectionMode_MatchesOriginal) {
    CompareUiQuery(ctx, Original_CGameData_IsUiSelectionMode, &CGameData_IsUiSelectionMode, 5000);
}
S3RE_TEST(CGameData_IsUiField298_MatchesOriginal) {
    CompareUiQuery(ctx, Original_CGameData_IsUiField298, &CGameData_IsUiField298, 5000);
}
S3RE_TEST(CGameData_IsUiSelectionParam1_MatchesOriginal) {
    CompareUiQuery(ctx, Original_CGameData_IsUiSelectionParam1, &CGameData_IsUiSelectionParam1,
                   5000);
}
S3RE_TEST(CGameData_IsUiSelectionParam2_MatchesOriginal) {
    CompareUiQuery(ctx, Original_CGameData_IsUiSelectionParam2, &CGameData_IsUiSelectionParam2,
                   5000);
}
S3RE_TEST(CGameData_IsUiSelectionMode4Field298Is2_MatchesOriginal) {
    using Adapter = IgnoreArgument<&Original_CGameData_IsUiSelectionMode4Field298Is2,
                                   &CGameData_IsUiSelectionMode4Field298Is2>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(CGameData_IsGameOutcome1_MatchesOriginal) {
    CompareUiQuery(ctx, Original_CGameData_IsGameOutcome1, &CGameData_IsGameOutcome1, 5000);
}
S3RE_TEST(CGameData_ScreenCenterTileX_MatchesOriginal) {
    using Adapter =
        IgnoreArgument<&Original_CGameData_ScreenCenterTileX, &CGameData_ScreenCenterTileX>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(CGameData_ScreenCenterTileX_ComparisonDetectsWrongImplementation) {
    using Adapter =
        IgnoreArgument<&Original_CGameData_ScreenCenterTileX, &WrongScreenCenterTileX>;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUiQuery(probe, &Adapter::CallOriginal, &Adapter::CallOurs, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ScreenCenterTileY_MatchesOriginal) {
    using Adapter =
        IgnoreArgument<&Original_CGameData_ScreenCenterTileY, &CGameData_ScreenCenterTileY>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(CGameData_ClampCamera_MatchesOriginal) {
    using Adapter = IgnoreArgument<&Original_CGameData_ClampCamera, &CGameData_ClampCamera>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(CGameData_ClampCamera_ComparisonDetectsWrongImplementation) {
    using Adapter = IgnoreArgument<&Original_CGameData_ClampCamera, &WrongClampCamera>;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUiQuery(probe, &Adapter::CallOriginal, &Adapter::CallOurs, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_PushUiFeedback_MatchesOriginal) {
    ComparePushUiFeedback(ctx, Original_CGameData_PushUiFeedback, &CGameData_PushUiFeedback, 5000);
}

namespace {

constexpr std::size_t kGroupBytes = sizeof(SettlerGroups);  // 0x7ad620..0x7ade04

// Saves the ten groups in S3.EXE's data and restores them afterwards.
class SavedGroups {
public:
    SavedGroups() { std::memcpy(saved_, Base(), kGroupBytes); }
    ~SavedGroups() { std::memcpy(Base(), saved_, kGroupBytes); }
    static SettlerGroups* Base() { return &GlobalSettlerGroups(); }

private:
    std::uint8_t saved_[kGroupBytes];
};

void CompareGroupMenu(test::Context& ctx, GroupMenuFn candidate, int cases) {
    const SavedGroups saved;
    test::WorldPair worlds(ctx, offsetof(CGameData, settlers) + 0x10000 * sizeof(layout::Settler));
    ThreadPair threads(worlds);
    static constexpr std::uint8_t kTypes[] = {0x24, 0x2c, 0x2b, 0x29, 0x28, 0x16, 0x17, 0x01, 0x23};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t group = ctx.rng().Below(10);
        const std::uint32_t count = ctx.rng().Chance(1, 3) ? ctx.rng().Below(2) : ctx.rng().Below(101);
        GlobalSettlerGroups().counts[group] = static_cast<std::uint16_t>(count);
        const std::uint8_t type = kTypes[ctx.rng().Below(sizeof(kTypes))];
        const bool allSame = ctx.rng().Chance(2, 3);
        const std::uint32_t localPlayer = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(4);
        worlds.Both([&](CGameData* w) { w->local_player = static_cast<std::int32_t>(localPlayer); });
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t settler = ctx.rng().Below(0x10000);
            GlobalSettlerGroups().lists[group][k] = static_cast<std::uint16_t>(settler);
            const std::uint8_t settlerType = allSame || ctx.rng().Chance(1, 2) ? type : kTypes[ctx.rng().Below(sizeof(kTypes))];
            const auto settlerOwner =
                static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? localPlayer : ctx.rng().Below(4));
            worlds.Both([&](CGameData* w) {
                w->settlers[settler].art = settlerType;
                w->settlers[settler].owner = settlerOwner;
            });
        }
        const std::uint32_t menu = ctx.rng().Chance(1, 2) ? ctx.rng().Below(8) : ctx.rng().Next32();
        const std::uint32_t refresh = ctx.rng().Next32();
        const std::uint32_t field29c = ctx.rng().Next32();
        worlds.Both([&](CGameData* w) {
            w->ui_special_menu = static_cast<std::int32_t>(menu);
            w->refresh_ui = static_cast<std::int32_t>(refresh);
            w->ui_2d29c = static_cast<std::int32_t>(field29c);
        });
        const std::uint32_t expected = Original_CLogicThread_UpdateGroupMenu(threads.original(), nullptr, group);
        const std::uint32_t actual = candidate(threads.ours(), nullptr, group);
        const auto base = [](CGameData* w) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w)); };
        // refresh_ui (+0x2d27c) .. ui_2d29c (+0x2d29c)
        const std::size_t first = offsetof(CGameData, refresh_ui);
        const std::size_t end = offsetof(CGameData, ui_2d29c) + 4;
        if (!S3RE_EXPECT_EQ(expected - base(worlds.original()), actual - base(worlds.ours())) ||
            !worlds.Same(ctx, first, end - first)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: group %u, count %u, type %#x", i, group, count, type);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: type 0x17 gets menu 6 like 0x16.
std::uint32_t __fastcall WrongGroupMenu(CLogicThread* self, void* edx, std::uint32_t group) {
    const std::uint32_t result = CLogicThread_UpdateGroupMenu(self, edx, group);
    CGameData* world = LogicThreadOf(self)->world;
    if (world->ui_special_menu == 7) {
        world->ui_special_menu = 6;
    }
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_UpdateGroupMenu_MatchesOriginal) {
    CompareGroupMenu(ctx, &CLogicThread_UpdateGroupMenu, 20000);
}
S3RE_TEST(CLogicThread_UpdateGroupMenu_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGroupMenu(probe, &WrongGroupMenu, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

// Batch 26.
//
// CLogicThread::ClearPendingUiRefresh (0x0053f530, our name): () thiscall on
//   the logic thread: +0x74 = 0; if +0x84 is set, +0x84 = 0 and the world's
//   "refresh the UI" flag (+0x2d27c) = 1. EAX = 0, or the world pointer when
//   the flag was set (callers ignore it). Called eight times by
//   ProcessEvents.

using LogicThreadFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);
std::uint32_t __fastcall CLogicThread_ClearPendingUiRefresh(CLogicThread* self, void* edx);

S3RE_HOOK(0x0053f530, CLogicThread_ClearPendingUiRefresh, LogicThreadFn, Original_CLogicThread_ClearPendingUiRefresh,
          CLogicThread_ClearPendingUiRefresh);

// Purpose: Turns a pending UI request of the logic thread (+0x84) into a
// redraw of the menus (probably after an input event was handled).
std::uint32_t __fastcall CLogicThread_ClearPendingUiRefresh(CLogicThread* self, void* /*edx*/) {
    CLogicThread* thread = LogicThreadOf(self);
    thread->unk074 = 0;
    if (thread->pending_ui_refresh == 0) {
        return 0;
    }
    thread->pending_ui_refresh = 0;
    CGameData* world = thread->world;
    world->refresh_ui = 1;
    return reinterpret_cast<std::uint32_t>(world);
}

namespace {

void CompareClearPendingUiRefresh(test::Context& ctx, LogicThreadFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, refresh_ui) + 4);
    std::uint8_t threadA[sizeof(CLogicThread)], threadB[sizeof(CLogicThread)];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(threadA, sizeof(threadA));
        if (ctx.rng().Chance(1, 2)) {
            LogicThreadOf(threadA)->pending_ui_refresh = 0;
        }
        std::memcpy(threadB, threadA, sizeof(threadB));
        LogicThreadOf(threadA)->world = worlds.original();
        LogicThreadOf(threadB)->world = worlds.ours();
        const std::uint32_t refresh = ctx.rng().Next32();
        worlds.Both([&](CGameData* w) { w->refresh_ui = static_cast<std::int32_t>(refresh); });
        const std::uint32_t ra = Original_CLogicThread_ClearPendingUiRefresh(reinterpret_cast<CLogicThread*>(threadA), nullptr);
        const std::uint32_t rb = candidate(reinterpret_cast<CLogicThread*>(threadB), nullptr);
        const std::uint32_t expected = ra == reinterpret_cast<std::uint32_t>(worlds.original()) ? 1 : ra;
        const std::uint32_t actual = rb == reinterpret_cast<std::uint32_t>(worlds.ours()) ? 1 : rb;
        bool same = S3RE_EXPECT_EQ(expected, actual);
        same &= S3RE_EXPECT_MEM_EQ(threadA, threadB, offsetof(CLogicThread, world));
        same &= worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->refresh_ui; });
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

std::uint32_t __fastcall WrongClearPendingUiRefresh(CLogicThread* self, void* edx) {
    const std::uint32_t r = CLogicThread_ClearPendingUiRefresh(self, edx);
    LogicThreadOf(self)->unk074 = 1;  // wrong: +0x74 not cleared
    return r;
}

}  // namespace

S3RE_TEST(CLogicThread_ClearPendingUiRefresh_MatchesOriginal) {
    CompareClearPendingUiRefresh(ctx, &CLogicThread_ClearPendingUiRefresh, 5000);
}

S3RE_TEST(CLogicThread_ClearPendingUiRefresh_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareClearPendingUiRefresh(probe, &WrongClearPendingUiRefresh, 500);
    S3RE_EXPECT(probe.failures() > 0);
}


// Session 54 (logic-misc).
//
// CGameData::TutorialWaitDone (0x00515b00): () thiscall: while the tutorial
//   wait countdown (+0x2d304, set to 100000 by StartTutorialWait 0x515af0,
//   to 1 by a click in ProcessEvents) is not 0, counts it down and returns
//   0; returns 1 once it is 0. Called by UpdateTutorial.
// CGameData::PickHitArea (0x004ff0a0, was CGameData::FindNearest_a): (x, y)
//   thiscall, ret 8: the hit area (g_hitAreas, the half g_hitAreaReadHalf,
//   g_hitAreaReadCount entries) nearest to the screen position (x, y) with
//   a squared distance below 2000 (32-bit wrapping squares, signed
//   compares; the first of equal ones) gives its two values to
//   last_search_x / _y (+0x2d2e4 / +0x2d2e8); none: both 0. A count of 0
//   leaves them alone, a negative count clears them. Called by
//   ProcessEvents for clicks (probably to find the object clicked). EAX
//   not used.

namespace {

constexpr std::int32_t kPickRadiusSquared = 2000;
constexpr std::int32_t kNoneYet = 0x7fffffff;

}  // namespace

using PickFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::int32_t x, std::int32_t y);

// Purpose: Whether the tutorial's wait before the next lesson is over
// (counting it down on each call).
std::uint32_t __fastcall CGameData_TutorialWaitDone(CGameData* world, void*) {
    if (world->tutorial_wait == 0) {
        return 1;
    }
    world->tutorial_wait = world->tutorial_wait - 1;
    return 0;
}

// Purpose: Finds the drawn object nearest to a click on the screen (from the
// hit areas the draw thread recorded) and remembers it.
std::uint32_t __fastcall CGameData_PickHitArea(CGameData* world, void*, std::int32_t x, std::int32_t y) {
    const std::int32_t count = g_hitAreaReadCount;
    if (count == 0) {
        return 0;
    }
    std::int32_t best = kNoneYet;
    std::uint32_t value1 = 0, value2 = 0;
    if (count > 0) {
        // Every second entry, from entry g_hitAreaReadHalf (not checked).
        const HitArea* area = &g_hitAreas.get()[static_cast<std::uint32_t>(g_hitAreaReadHalf.get())];
        for (std::int32_t n = count; n != 0; --n, area += 2) {
            const std::uint32_t dx = static_cast<std::uint32_t>(x) - static_cast<std::uint32_t>(area->x);
            const std::uint32_t dy = static_cast<std::uint32_t>(y) - static_cast<std::uint32_t>(area->y);
            const auto distance = static_cast<std::int32_t>(dy * dy + dx * dx);
            if (distance < kPickRadiusSquared && distance < best) {
                value1 = area->value1;
                value2 = area->value2;
                best = distance;
            }
        }
    }
    if (best == kNoneYet) {
        world->last_search_x = 0;
        world->last_search_y = 0;
    } else {
        world->last_search_x = static_cast<std::int32_t>(value1);
        world->last_search_y = static_cast<std::int32_t>(value2);
    }
    return 0;
}

S3RE_HOOK(0x00515b00, CGameData_TutorialWaitDone, WorldQueryFn, Original_CGameData_TutorialWaitDone,
          CGameData_TutorialWaitDone);
S3RE_HOOK(0x004ff0a0, CGameData_PickHitArea, PickFn, Original_CGameData_PickHitArea, CGameData_PickHitArea);

namespace {

void CompareTutorialWait(test::Context& ctx, WorldQueryFn original, WorldQueryFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, tutorial_step));
    for (int i = 0; i < cases; ++i) {
        const auto wait = static_cast<std::int32_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(3) : ctx.rng().Next32());
        worlds.Both([&](CGameData* w) { w->tutorial_wait = wait; });
        const std::uint32_t expected = original(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: wait %d", i, wait);
            return;
        }
    }
}

void ComparePickHitArea(test::Context& ctx, PickFn candidate, int cases) {
    static HitArea saved[20000];
    std::memcpy(saved, &g_hitAreas.get(), sizeof(saved));
    const std::int32_t savedCount = g_hitAreaReadCount, savedHalf = g_hitAreaReadHalf;
    test::WorldPair worlds(ctx, offsetof(CGameData, unk2d2ec));
    for (int i = 0; i < cases; ++i) {
        const std::int32_t x = ctx.rng().Range(-50, 1100), y = ctx.rng().Range(-50, 800);
        const std::uint32_t kind = ctx.rng().Below(8);
        g_hitAreaReadCount = kind == 0 ? 0 : kind == 1 ? -static_cast<std::int32_t>(ctx.rng().Below(5)) - 1
                                                       : static_cast<std::int32_t>(ctx.rng().Below(kind == 2 ? 10000 : 200));
        g_hitAreaReadHalf = static_cast<std::int32_t>(ctx.rng().Below(2));
        // Entries near (x, y) or anywhere (x/y as int16, sometimes far off).
        const std::uint32_t used = static_cast<std::uint32_t>(g_hitAreaReadCount > 0 ? g_hitAreaReadCount : 0);
        for (std::uint32_t k = 0; k < used; ++k) {
            HitArea& a = g_hitAreas[2 * k + static_cast<std::uint32_t>(g_hitAreaReadHalf.get())];
            const bool close = ctx.rng().Chance(1, 4);
            a.x = static_cast<std::int16_t>(close ? x + ctx.rng().Range(-50, 50) : static_cast<std::int32_t>(ctx.rng().Next32()));
            a.y = static_cast<std::int16_t>(close ? y + ctx.rng().Range(-50, 50) : static_cast<std::int32_t>(ctx.rng().Next32()));
            a.value1 = static_cast<std::uint16_t>(ctx.rng().Next32());
            a.value2 = static_cast<std::uint16_t>(ctx.rng().Next32());
        }
        Original_CGameData_PickHitArea(worlds.original(), nullptr, x, y);
        candidate(worlds.ours(), nullptr, x, y);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: count %d, half %d", i, g_hitAreaReadCount.get(), g_hitAreaReadHalf.get());
            break;
        }
    }
    std::memcpy(&g_hitAreas.get(), saved, sizeof(saved));
    g_hitAreaReadCount = savedCount;
    g_hitAreaReadHalf = savedHalf;
}

// Wrong: the last of equally near hit areas wins.
std::uint32_t __fastcall WrongPickHitArea(CGameData* world, void* edx, std::int32_t x, std::int32_t y) {
    const std::int32_t count = g_hitAreaReadCount;
    if (count <= 0) {
        return CGameData_PickHitArea(world, edx, x, y);
    }
    std::int32_t best = kNoneYet;
    for (std::int32_t k = 0; k < count; ++k) {
        const HitArea& a = g_hitAreas[2 * static_cast<std::uint32_t>(k) + static_cast<std::uint32_t>(g_hitAreaReadHalf.get())];
        const std::int32_t d = (x - a.x) * (x - a.x) + (y - a.y) * (y - a.y);
        if (d < kPickRadiusSquared && d <= best) {
            world->last_search_x = a.value1;
            world->last_search_y = a.value2;
            best = d;
        }
    }
    if (best == kNoneYet) {
        world->last_search_x = 0;
        world->last_search_y = 0;
    }
    return 0;
}

}  // namespace

S3RE_TEST(CGameData_TutorialWaitDone_MatchesOriginal) {
    CompareTutorialWait(ctx, Original_CGameData_TutorialWaitDone, &CGameData_TutorialWaitDone, 3000);
}
S3RE_TEST(CGameData_PickHitArea_MatchesOriginal) {
    ComparePickHitArea(ctx, &CGameData_PickHitArea, 3000);
}
S3RE_TEST(CGameData_PickHitArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePickHitArea(probe, &WrongPickHitArea, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// Session 55 (settings).
//
// CGameData::TileAtScreenPoint (0x004ff170, was CGameData::FindNearest_b):
//   (x, y) thiscall, ret 8: the map tile under the screen position (x, y)
//   goes to last_search_x / _y (+0x2d2e4 / +0x2d2e8); nothing happens when
//   x is left of the game view (x < g_sidebarWidth). Called by
//   ProcessEvents for clicks and the mouse cursor. EAX not used.
//   The screen position is moved into map pixels (camera_drawn_x/_y
//   +0x2d254/+0x2d258, minus 280 and the sidebar shift of the display mode:
//   32, 64, 96 for modes 1..3; y minus 200), then into the tile grid:
//   column = trunc((px + 796) / 8) - mapSize - 100; an odd column is a
//   half-row lower (y - 9); row = trunc((py + 1800) / 18) - 100; tile x =
//   column/2 (rounded up when odd, arithmetic shift) + row, tile y = odd +
//   2 * row. Then it walks the tiles down the screen (x + 1, y + 2, 18
//   pixels per step) until a tile's height (tile byte +4; 0 off the map) no
//   longer reaches the point, and takes that tile or the one before,
//   whichever is nearer (the distances compared unsigned). The result is
//   clamped to the map. Divisions truncate toward zero, sums wrap at 32
//   bits; the on-map checks are signed (the walk tests the row offset x *
//   0x300 for < 0, not x: with a tile x below about -2^31 / 0x300, from
//   camera or screen values far outside the map, the offset wraps positive
//   and the height is read far outside the world; kept).

namespace {

constexpr std::int32_t kViewOriginX = 280;          // 0x118
constexpr std::int32_t kViewOriginY = 200;          // 0xc8
constexpr std::int32_t kColumnBias = 796;           // 0x31c
constexpr std::int32_t kRowBias = 1800;             // 0x708
constexpr std::int32_t kGridBias = 100;             // 0x64
constexpr std::int32_t kRowPixels = 18;             // a tile row (two y steps)
constexpr std::int32_t kHalfRowPixels = 9;
constexpr std::int32_t kColumnPixels = 8;

std::int32_t Wrap(std::uint32_t value) {
    return static_cast<std::int32_t>(value);
}
std::int32_t Add(std::int32_t a, std::int32_t b) {
    return Wrap(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// The sidebar shift per display mode (0x4ff196..0x4ff1b5).
std::int32_t DisplayModeShift(std::int32_t mode) {
    switch (mode) {
    case 1: return 32;
    case 2: return 64;
    case 3: return 96;
    default: return 0;
    }
}

// The height of the tile at row offset `rowOffset` (x * 0x300) and y; the
// address is computed like the original's ([world + (rowOffset + y) * 24 + 0x67448]).
std::uint8_t HeightAt(const CGameData* world, std::int32_t rowOffset, std::int32_t y) {
    const std::uint32_t index = static_cast<std::uint32_t>(rowOffset) + static_cast<std::uint32_t>(y);
    return (&world->tiles[0][0] + index)->height;
}

}  // namespace

using TileAtScreenPointFn = void(__fastcall*)(CGameData* world, void* edx, std::int32_t x, std::int32_t y);

// Purpose: Finds the map tile under a screen position (a click or the mouse
// cursor) and remembers it for the UI.
void __fastcall CGameData_TileAtScreenPoint(CGameData* world, void*, std::int32_t screenX, std::int32_t screenY) {
    if (screenX < g_sidebarWidth) {
        return;
    }
    std::int32_t px = Add(Add(world->camera_drawn_x, screenX), -kViewOriginX);
    px = Add(px, -DisplayModeShift(g_displayMode));
    std::int32_t py = Add(Add(world->camera_drawn_y, screenY), -kViewOriginY);
    const auto size = static_cast<std::int32_t>(world->map_size);

    const std::int32_t column = Add(Add(Add(px, kColumnBias) / kColumnPixels, -size), -kGridBias);
    std::int32_t x;
    std::int32_t y;
    if ((column & 1) == 0) {
        x = column >> 1;
        y = 0;
    } else {
        x = (column >> 1) + 1;
        y = 1;
        py = Add(py, -kHalfRowPixels);
    }
    const std::int32_t row = Add(Add(py, kRowBias) / kRowPixels, -kGridBias);
    x = Add(x, row);
    y = Add(y, Add(row, row));
    const std::int32_t offset = Add(py, Wrap(static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(-kRowPixels)));

    // How far the point is below the start tile's top (its height counted).
    std::int32_t below = offset;
    if (x >= 0 && x < size && y >= 0 && y < size) {
        below = Add(world->tiles[x][y].height, offset);
    }
    // Walk down until a tile's top is below the point.
    std::int32_t step = -offset;
    std::int32_t rowOffset = Wrap(static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(layout::kMapRowTiles));
    std::int32_t above = 0;
    while (above == 0) {
        rowOffset = Add(rowOffset, static_cast<std::int32_t>(layout::kMapRowTiles));
        x = Add(x, 1);
        y = Add(y, 2);
        step = Add(step, kRowPixels);
        std::int32_t distance = step;
        if (rowOffset >= 0 && x < size && y >= 0 && y < size) {
            distance = Add(step, -static_cast<std::int32_t>(HeightAt(world, rowOffset, y)));
        }
        if (distance < 0) {
            below = Wrap(0u - static_cast<std::uint32_t>(distance));
        } else {
            above = distance;
        }
    }
    if (static_cast<std::uint32_t>(below) < static_cast<std::uint32_t>(above)) {
        x = Add(x, -1);
        y = Add(y, -2);
    }
    x = Add(x, 1);
    y = Add(y, 2);
    x = x < 0 ? 0 : x;
    x = x < size ? x : size - 1;
    y = y < 0 ? 0 : y;
    y = y < size ? y : size - 1;
    world->last_search_x = x;
    world->last_search_y = y;
}

S3RE_HOOK(0x004ff170, CGameData_TileAtScreenPoint, TileAtScreenPointFn, Original_CGameData_TileAtScreenPoint,
          CGameData_TileAtScreenPoint);

namespace {

constexpr std::uint32_t kTestMapTiles = 64;

void CompareTileAtScreenPoint(test::Context& ctx, TileAtScreenPointFn candidate, int cases) {
    const std::int32_t savedMode = g_displayMode, savedSidebar = g_sidebarWidth;
    test::WorldPair worlds(ctx, offsetof(CGameData, tiles) + kTestMapTiles * sizeof(CGameData::tiles[0]));
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t size = rng.Chance(1, 20) ? rng.Below(3) : 4 + rng.Below(kTestMapTiles - 3);
        // Far-off positions stay within a million pixels: past about 2^31 / 0x300
        // tiles the walk's row offset wraps and both functions read far outside
        // the map (the original faults).
        const std::int32_t cameraX = rng.Chance(1, 10) ? rng.Range(-1000000, 1000000)
                                                       : rng.Range(-200, static_cast<std::int32_t>(size) * 16 + 600);
        const std::int32_t cameraY = rng.Chance(1, 10) ? rng.Range(-1000000, 1000000)
                                                       : rng.Range(-200, static_cast<std::int32_t>(size) * 9 + 300);
        const std::int32_t screenX = rng.Chance(1, 20) ? rng.Range(-1000000, 1000000) : rng.Range(-50, 1100);
        const std::int32_t screenY = rng.Chance(1, 20) ? rng.Range(-1000000, 1000000) : rng.Range(-50, 800);
        g_displayMode = static_cast<std::int32_t>(rng.Below(5));
        static constexpr std::int32_t kSidebars[] = {136, 176, 216, 0, -5};
        g_sidebarWidth = kSidebars[rng.Below(5)];
        const bool flat = rng.Chance(1, 4);
        worlds.Both([&](CGameData* w) {
            w->map_size = size;
            w->camera_drawn_x = cameraX;
            w->camera_drawn_y = cameraY;
        });
        for (std::uint32_t tx = 0; tx < size; ++tx) {
            for (std::uint32_t ty = 0; ty < size; ++ty) {
                const auto h = static_cast<std::uint8_t>(flat ? rng.Below(8) : rng.Next32());
                worlds.Both([&](CGameData* w) { w->tiles[tx][ty].height = h; });
            }
        }
        Original_CGameData_TileAtScreenPoint(worlds.original(), nullptr, screenX, screenY);
        candidate(worlds.ours(), nullptr, screenX, screenY);
        if (!worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->last_search_x; }) ||
            !worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->last_search_y; })) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u camera %d,%d screen %d,%d mode %d", i, size, cameraX,
                     cameraY, screenX, screenY, g_displayMode.get());
            break;
        }
    }
    S3RE_EXPECT(worlds.Same(ctx, 0, worlds.size()));
    g_displayMode = savedMode;
    g_sidebarWidth = savedSidebar;
}

// Wrong: always takes the tile where the walk stopped.
void __fastcall WrongTileAtScreenPoint(CGameData* world, void* edx, std::int32_t x, std::int32_t y) {
    CGameData_TileAtScreenPoint(world, edx, x, y);
    if (x >= g_sidebarWidth && world->last_search_x > 0 && world->last_search_y > 1 &&
        ((x ^ y) & 3) == 0) {
        world->last_search_x -= 1;
        world->last_search_y -= 2;
    }
}

}  // namespace

S3RE_TEST(CGameData_TileAtScreenPoint_MatchesOriginal) {
    CompareTileAtScreenPoint(ctx, &CGameData_TileAtScreenPoint, 5000);
}
S3RE_TEST(CGameData_TileAtScreenPoint_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileAtScreenPoint(probe, &WrongTileAtScreenPoint, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
