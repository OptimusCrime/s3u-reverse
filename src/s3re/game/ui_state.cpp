// Small queries on UI state that lives in the world object (see the
// "UI state lives inside the world object" notes in CLAUDE.md), all called
// from the UI code around 0x53fbb0.
//
// GameWorld::IsUiSelectionMode (0x005157b0): (mode) -> 1 if the UI
//   selection mode (+0x2d284) equals mode, else 0.
// GameWorld::IsUiSelectionParam1 (0x005157f0): the same for the selection's
//   first parameter (+0x2d288).
// GameWorld::IsUiSelectionParam2 (0x00515810): the same for the second
//   parameter (+0x2d28c).
// GameWorld::IsUiField298 (0x005157d0): the same for +0x2d298 (meaning
//   unknown; written by ProcessEvents and 0x542660/0x542990/0x542b60).
// GameWorld::IsUiSelectionMode4Field298Is2 (0x00515830): () -> 1 if the
//   selection mode is 4 and +0x2d298 is 2.
// GameWorld::IsGameOutcome1 (0x00515ad0): (unused) -> 1 if the global game
//   outcome (0x730f64) is 1. Its stack argument is ignored.
// All compare full 32-bit values. thiscall (ECX = world), EAX = 1 or 0.
//
// GameWorld::ScreenCenterTileX/Y (0x005158f0/0x00515980): the tile at the
//   centre of the game view, from the camera position in pixels
//   (+0x2d25c x, +0x2d260 y), the view size (globals 0x730f18 width,
//   0x730f1c height) and the map size. Tile rows are 9 pixels high
//   ("/ 18" and "/ 9" below) and tiles 16 pixels wide; the map is drawn
//   skewed, hence the y terms in X. All divisions truncate toward zero
//   (signed, as the original's multiply-by-reciprocal code does); sums wrap
//   at 32 bits; the `>> 1`/`>> 2` are unsigned shifts. No arguments, ret 0.
//
// GameWorld::ClampCamera (0x0051b4d0): keeps the camera inside the map:
//   x in [0, mapSize*24 - width + 0x167], y in [0, mapSize*9 - width +
//   0x176] (signed compares; the y limit really uses the view width
//   0x730f18, not the height). Only writes a coordinate that is out of
//   range. Called after camera moves in ProcessEvents and by 0x51b560,
//   which returns its EAX: mapSize*9 - width, plus 0x176 if y was clamped
//   at the top. No arguments, ret 0.
//
// GameWorld::PushUiFeedback (0x004febe0): (value) appends value to the
//   10-entry dword list at +0x1539484 (count at +0x15394ac) unless it is
//   full. Drained by CLogicThread::PrepareDrawData. EAX = the new count, or
//   10 when the list was full (callers don't use it, but one tail-calls).
// CLogicThread::PushUiFeedback (0x0053f560): (value) calls
//   GameWorld::PushUiFeedback on the logic thread's world (this+0x3a0) and
//   returns its EAX. ECX = CLogicThread. ret 4. 288 call sites in the UI
//   code.
//
// GameWorld::CenterCameraOnTile (0x0051b560): (x, y) sets the camera to
//   x = (mapSize + 2x + 0x18 - y) * 8 - width/2 and
//   y = y*9 + 0xc8 - height/2 - tile byte +0x4 of (x, y) (probably the
//   tile's height in pixels), with signed halves of the view size truncated
//   toward zero and 32-bit wrapping sums, then calls GameWorld::ClampCamera
//   and returns its EAX (one caller tail-returns it). ret 8.
// GameWorld::CenterCameraOnLocalPlayer (0x0051b6b0): CenterCameraOnTile on
//   the position (int32 +0x10, +0x14) of the player record of the local
//   player (+0x2d2d4), then clears the int32 +0x2d264. EAX = the
//   CenterCameraOnTile EAX (one caller tail-returns it). Plain ret.

#include "game/globals.h"
#include "game/layout.h"
#include "game/replacements.h"
#include "game/ui_layout.h"
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
static_assert(sizeof(GameWorld::ui_feedback) / sizeof(GameWorld::ui_feedback[0]) == kUiFeedbackCapacity);

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
std::uint32_t CameraRow(GameWorld* world) {
    return DivideTruncating(U32(world->camera_y) + 0x640u, 18);
}

std::uint32_t ScreenRows(std::uint32_t shift) {
    const auto height = GameGlobal<std::uint32_t>(global_address::kScreenHeight);
    return (DivideTruncating(height + 0xffu, 9) + 0xbu) >> shift;
}

}  // namespace

using WorldValueFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t value);
using WorldQueryFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

// Purpose: Whether the user interface is in a given selection mode (e.g. building placement). The UI state
// happens to live inside the world object.
std::uint32_t __fastcall GameWorld_IsUiSelectionMode(GameWorld* world, void*, std::uint32_t mode) {
    return U32(world->ui_selection_mode) == mode ? 1 : 0;
}

// Purpose: Whether a UI state field (meaning unknown) has a given value.
std::uint32_t __fastcall GameWorld_IsUiField298(GameWorld* world, void*, std::uint32_t value) {
    return U32(world->ui_special_menu) == value ? 1 : 0;
}

// Purpose: Whether the first parameter of the current UI selection (e.g. which building type) has a given
// value.
std::uint32_t __fastcall GameWorld_IsUiSelectionParam1(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return U32(world->ui_selection_param1) == value ? 1 : 0;
}

// Purpose: Whether the second parameter of the current UI selection has a given value.
std::uint32_t __fastcall GameWorld_IsUiSelectionParam2(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return U32(world->ui_selection_param2) == value ? 1 : 0;
}

// Purpose: Whether the UI is in selection mode 4 with a particular sub-state. A shortcut check used by the
// UI.
std::uint32_t __fastcall GameWorld_IsUiSelectionMode4Field298Is2(GameWorld* world, void*) {
    return (U32(world->ui_selection_mode) == 4 && U32(world->ui_special_menu) == 2) ? 1 : 0;
}

// Purpose: Whether the game has ended with outcome 1 (probably 'won'). Queried by the user interface.
std::uint32_t __fastcall GameWorld_IsGameOutcome1(GameWorld*, void*, std::uint32_t) {
    return GameGlobal<std::uint32_t>(global_address::kGameOutcome) == 1 ? 1 : 0;
}

// Purpose: Which tile is at the centre of the screen (x). Used e.g. to know where the player is looking.
std::uint32_t __fastcall GameWorld_ScreenCenterTileX(GameWorld* world, void*) {
    const std::uint32_t rowsBelowTop = 100u - CameraRow(world);
    const std::uint32_t skew = (world->map_size + rowsBelowTop * 2u) >> 1;
    const auto width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
    const std::uint32_t halfColumns = (DivideTruncating(width + 0x17u, 16) + 0xau) >> 1;
    const std::uint32_t column = DivideTruncating(U32(world->camera_x) - 0xd0u, 16);
    return column + (ScreenRows(2) - skew + halfColumns);
}

// Purpose: Which tile is at the centre of the screen (y).
std::uint32_t __fastcall GameWorld_ScreenCenterTileY(GameWorld* world, void*) {
    return ScreenRows(1) + CameraRow(world) * 2u - 200u;
}

// Purpose: Keeps the camera inside the map after it has been moved.
std::uint32_t __fastcall GameWorld_ClampCamera(GameWorld* world, void*) {
    std::int32_t& cameraX = world->camera_x;
    std::int32_t& cameraY = world->camera_y;
    const std::uint32_t size = world->map_size;
    const std::uint32_t width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
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
std::uint32_t __fastcall GameWorld_PushUiFeedback(GameWorld* world, void*, std::uint32_t value) {
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
std::uint32_t __fastcall GameWorld_CenterCameraOnTile(GameWorld* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    const auto width = static_cast<std::int32_t>(GameGlobal<std::uint32_t>(global_address::kScreenWidth));
    const auto height = static_cast<std::int32_t>(GameGlobal<std::uint32_t>(global_address::kScreenHeight));
    const std::uint32_t size = world->map_size;
    world->camera_x = static_cast<std::int32_t>((size + x * 2u + 0x18u - y) * 8u - static_cast<std::uint32_t>(width / 2));
    // Tile (x, y) addressed in 32 bits like the original (x, y unchecked).
    const std::uint32_t height4 = TileAtOffset(world, TileOffset(x, y)).height;
    world->camera_y =
        static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(height / 2) - height4 + y * 9u + 0xc8u);
    return GameWorld_ClampCamera(world, nullptr);
}

struct CLogicThread;  // opaque

// Purpose: The same as GameWorld::PushUiFeedback, called from the user interface code through the logic
// thread.
std::uint32_t __fastcall CLogicThread_PushUiFeedback(CLogicThread* self, void*, std::uint32_t value) {
    GameWorld* world = ui::LogicThreadOf(self)->world;
    return GameWorld_PushUiFeedback(world, nullptr, value);
}

using PositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using LogicValueFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t value);

// Purpose: Moves the camera to the local player's home position (probably their castle), e.g. at the start
// of a game.
std::uint32_t __fastcall GameWorld_CenterCameraOnLocalPlayer(GameWorld* world, void*) {
    // (The local player number is not range-checked.)
    const layout::Player& player = world->players[U32(world->local_player)];
    const std::uint32_t result =
        GameWorld_CenterCameraOnTile(world, nullptr, U32(player.start_x), U32(player.start_y));
    world->redraw = 0;
    return result;
}

S3RE_HOOK(0x0051b6b0, GameWorld_CenterCameraOnLocalPlayer, WorldQueryFn, Original_GameWorld_CenterCameraOnLocalPlayer,
          GameWorld_CenterCameraOnLocalPlayer);
S3RE_HOOK(0x0051b560, GameWorld_CenterCameraOnTile, PositionFn, Original_GameWorld_CenterCameraOnTile,
          GameWorld_CenterCameraOnTile);
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
S3RE_HOOK(0x005157b0, GameWorld_IsUiSelectionMode, WorldValueFn,
          Original_GameWorld_IsUiSelectionMode, GameWorld_IsUiSelectionMode);
S3RE_HOOK(0x005157d0, GameWorld_IsUiField298, WorldValueFn, Original_GameWorld_IsUiField298,
          GameWorld_IsUiField298);
S3RE_HOOK(0x005157f0, GameWorld_IsUiSelectionParam1, WorldValueFn,
          Original_GameWorld_IsUiSelectionParam1, GameWorld_IsUiSelectionParam1);
S3RE_HOOK(0x00515810, GameWorld_IsUiSelectionParam2, WorldValueFn,
          Original_GameWorld_IsUiSelectionParam2, GameWorld_IsUiSelectionParam2);
S3RE_HOOK(0x00515830, GameWorld_IsUiSelectionMode4Field298Is2, WorldQueryFn,
          Original_GameWorld_IsUiSelectionMode4Field298Is2,
          GameWorld_IsUiSelectionMode4Field298Is2);
S3RE_HOOK(0x00515ad0, GameWorld_IsGameOutcome1, WorldValueFn, Original_GameWorld_IsGameOutcome1,
          GameWorld_IsGameOutcome1);
S3RE_HOOK(0x005158f0, GameWorld_ScreenCenterTileX, WorldQueryFn,
          Original_GameWorld_ScreenCenterTileX, GameWorld_ScreenCenterTileX);
S3RE_HOOK(0x00515980, GameWorld_ScreenCenterTileY, WorldQueryFn,
          Original_GameWorld_ScreenCenterTileY, GameWorld_ScreenCenterTileY);
S3RE_HOOK(0x0051b4d0, GameWorld_ClampCamera, WorldQueryFn, Original_GameWorld_ClampCamera,
          GameWorld_ClampCamera);
S3RE_HOOK(0x004febe0, GameWorld_PushUiFeedback, WorldValueFn, Original_GameWorld_PushUiFeedback,
          GameWorld_PushUiFeedback);

namespace {

constexpr std::uintptr_t kGroupCounts = 0x007ad620;  // uint16[10], S3.EXE data
constexpr std::uintptr_t kGroupSettlers = 0x007ad634;  // uint16[10][100]

std::uint16_t GroupWord(std::uintptr_t address) {
    return *reinterpret_cast<const std::uint16_t*>(address);
}

}  // namespace

using GroupMenuFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t group);
std::uint32_t __fastcall CLogicThread_UpdateGroupMenu(CLogicThread* self, void* edx, std::uint32_t group);

S3RE_HOOK(0x00542990, CLogicThread_UpdateGroupMenu, GroupMenuFn, Original_CLogicThread_UpdateGroupMenu,
          CLogicThread_UpdateGroupMenu);

// Purpose: Chooses which special command menu the game shows for a group of
// settlers (e.g. ships or other units with their own commands), probably
// after the group changed or was selected.
std::uint32_t __fastcall CLogicThread_UpdateGroupMenu(CLogicThread* self, void* /*edx*/, std::uint32_t group) {
    GameWorld* world = ui::LogicThreadOf(self)->world;
    const std::uint32_t count = GroupWord(kGroupCounts + group * 2);
    const std::uintptr_t settlers = kGroupSettlers + group * 200;
    auto owner = [&](std::uint32_t settler) { return static_cast<std::uint32_t>(world->settlers[settler].owner); };
    auto type = [&](std::uint32_t settler) { return static_cast<std::uint32_t>(world->settlers[settler].art); };
    std::uint32_t changed = 1;
    if (count == 1) {
        const std::uint32_t settler = GroupWord(settlers);
        if (owner(settler) == U32(world->local_player) && type(settler) == 0x24 && U32(world->ui_special_menu) != 1) {
            world->ui_special_menu = 1;
            world->ui_2d29c = 0;
            world->refresh_ui = 1;
            return reinterpret_cast<std::uint32_t>(world);
        }
    }
    if (count != 0) {
        const std::uint32_t firstType = type(GroupWord(settlers));
        bool same = true;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (type(GroupWord(settlers + i * 2)) != firstType) {
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

constexpr std::size_t kUiWorld = offsetof(GameWorld, search_generation);  // the UI block and the map size
constexpr std::size_t kFeedbackWorld = offsetof(GameWorld, ui_feedback_count) + 4;
constexpr std::size_t kFeedbackBytes = sizeof(GameWorld::ui_feedback) + sizeof(GameWorld::ui_feedback_count);
static_assert(offsetof(GameWorld, ui_feedback_count) == offsetof(GameWorld, ui_feedback) + sizeof(GameWorld::ui_feedback));

// Two fake logic threads, each pointing at one of the worlds.
struct ThreadPair {
    explicit ThreadPair(test::WorldPair& worlds) {
        ui::LogicThreadOf(a)->world = worlds.original();
        ui::LogicThreadOf(b)->world = worlds.ours();
    }
    CLogicThread* original() { return reinterpret_cast<CLogicThread*>(a); }
    CLogicThread* ours() { return reinterpret_cast<CLogicThread*>(b); }
    std::uint8_t a[sizeof(ui::LogicThreadView)] = {};
    std::uint8_t b[sizeof(ui::LogicThreadView)] = {};
};

// Sets a game global for the duration of a test and restores it after.
class GlobalOverride {
public:
    explicit GlobalOverride(std::uintptr_t address)
        : address_(address), saved_(GameGlobal<std::uint32_t>(address)) {}
    ~GlobalOverride() { GameGlobal<std::uint32_t>(address_) = saved_; }
    void Set(std::uint32_t value) { GameGlobal<std::uint32_t>(address_) = value; }

private:
    std::uintptr_t address_;
    std::uint32_t saved_;
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
    GlobalOverride width(global_address::kScreenWidth);
    GlobalOverride height(global_address::kScreenHeight);
    GlobalOverride outcome(global_address::kGameOutcome);
    for (int i = 0; i < cases; ++i) {
        for (const std::size_t field :
             {offsetof(GameWorld, ui_selection_mode), offsetof(GameWorld, ui_selection_param1),
              offsetof(GameWorld, ui_selection_param2), offsetof(GameWorld, ui_special_menu),
              offsetof(GameWorld, camera_x), offsetof(GameWorld, camera_y), offsetof(GameWorld, map_size)}) {
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
    static std::uint32_t __fastcall CallOriginal(GameWorld* world, void* edx, std::uint32_t) {
        return (*Original)(world, edx);
    }
    static std::uint32_t __fastcall CallOurs(GameWorld* world, void* edx, std::uint32_t) {
        return Ours(world, edx);
    }
};

void ComparePushUiFeedback(test::Context& ctx, WorldValueFn original, WorldValueFn candidate,
                           int cases) {
    test::WorldPair worlds(ctx, kFeedbackWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(kUiFeedbackCapacity + 1);
        worlds.Both([&](GameWorld* w) { w->ui_feedback_count = count; });
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = original(worlds.original(), nullptr, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(GameWorld, ui_feedback), kFeedbackBytes) ||
            (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: rounds the column down instead of toward zero.
std::uint32_t __fastcall WrongScreenCenterTileX(GameWorld* world, void* edx) {
    const auto column = static_cast<std::int32_t>(U32(world->camera_x) - 0xd0u);
    return GameWorld_ScreenCenterTileX(world, edx) - DivideTruncating(column, 16) +
           static_cast<std::uint32_t>(column >> 4);
}

// Wrong: uses the view height for the y limit.
std::uint32_t __fastcall WrongClampCamera(GameWorld* world, void* edx) {
    const auto width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
    GameGlobal<std::uint32_t>(global_address::kScreenWidth) =
        GameGlobal<std::uint32_t>(global_address::kScreenHeight);
    std::int32_t x = world->camera_x;
    const std::uint32_t result = GameWorld_ClampCamera(world, edx);
    GameGlobal<std::uint32_t>(global_address::kScreenWidth) = width;
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

S3RE_TEST(GameWorld_CenterCameraOnTile_MatchesOriginal) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, tiles) + 400 * kTileRowBytes);
    GlobalOverride width(global_address::kScreenWidth);
    GlobalOverride height(global_address::kScreenHeight);
    for (int i = 0; i < 5000; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        width.Set(ctx.rng().Chance(3, 4) ? 640 + ctx.rng().Below(1024) : InterestingValue(ctx));
        height.Set(ctx.rng().Chance(3, 4) ? 480 + ctx.rng().Below(768) : InterestingValue(ctx));
        const std::uint32_t x = ctx.rng().Below(400);
        const std::uint32_t y = ctx.rng().Below(400);
        const std::uint32_t expected = Original_GameWorld_CenterCameraOnTile(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = GameWorld_CenterCameraOnTile(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(GameWorld, camera_x), 8) ||
            (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u)", i, x, y);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(GameWorld_CenterCameraOnLocalPlayer_MatchesOriginal) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, players) + 21 * sizeof(layout::Player));
    GlobalOverride width(global_address::kScreenWidth);
    GlobalOverride height(global_address::kScreenHeight);
    for (int i = 0; i < 3000; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        width.Set(640 + ctx.rng().Below(1024));
        height.Set(480 + ctx.rng().Below(768));
        const std::uint32_t player = ctx.rng().Below(20);
        const auto startX = static_cast<std::int32_t>(ctx.rng().Below(384));
        const auto startY = static_cast<std::int32_t>(ctx.rng().Below(384));
        worlds.Both([&](GameWorld* w) {
            w->local_player = static_cast<std::int32_t>(player);
            w->players[player].start_x = startX;
            w->players[player].start_y = startY;
        });
        const std::uint32_t expected = Original_GameWorld_CenterCameraOnLocalPlayer(worlds.original(), nullptr);
        const std::uint32_t actual = GameWorld_CenterCameraOnLocalPlayer(worlds.ours(), nullptr);
        // camera_x, camera_y and redraw
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(GameWorld, camera_x), 12) ||
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
        worlds.Both([&](GameWorld* w) { w->ui_feedback_count = count; });
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = Original_CLogicThread_PushUiFeedback(threads.original(), nullptr, value);
        const std::uint32_t actual = CLogicThread_PushUiFeedback(threads.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, offsetof(GameWorld, ui_feedback), kFeedbackBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(GameWorld_IsUiSelectionMode_MatchesOriginal) {
    CompareUiQuery(ctx, Original_GameWorld_IsUiSelectionMode, &GameWorld_IsUiSelectionMode, 5000);
}
S3RE_TEST(GameWorld_IsUiField298_MatchesOriginal) {
    CompareUiQuery(ctx, Original_GameWorld_IsUiField298, &GameWorld_IsUiField298, 5000);
}
S3RE_TEST(GameWorld_IsUiSelectionParam1_MatchesOriginal) {
    CompareUiQuery(ctx, Original_GameWorld_IsUiSelectionParam1, &GameWorld_IsUiSelectionParam1,
                   5000);
}
S3RE_TEST(GameWorld_IsUiSelectionParam2_MatchesOriginal) {
    CompareUiQuery(ctx, Original_GameWorld_IsUiSelectionParam2, &GameWorld_IsUiSelectionParam2,
                   5000);
}
S3RE_TEST(GameWorld_IsUiSelectionMode4Field298Is2_MatchesOriginal) {
    using Adapter = IgnoreArgument<&Original_GameWorld_IsUiSelectionMode4Field298Is2,
                                   &GameWorld_IsUiSelectionMode4Field298Is2>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(GameWorld_IsGameOutcome1_MatchesOriginal) {
    CompareUiQuery(ctx, Original_GameWorld_IsGameOutcome1, &GameWorld_IsGameOutcome1, 5000);
}
S3RE_TEST(GameWorld_ScreenCenterTileX_MatchesOriginal) {
    using Adapter =
        IgnoreArgument<&Original_GameWorld_ScreenCenterTileX, &GameWorld_ScreenCenterTileX>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(GameWorld_ScreenCenterTileX_ComparisonDetectsWrongImplementation) {
    using Adapter =
        IgnoreArgument<&Original_GameWorld_ScreenCenterTileX, &WrongScreenCenterTileX>;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUiQuery(probe, &Adapter::CallOriginal, &Adapter::CallOurs, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ScreenCenterTileY_MatchesOriginal) {
    using Adapter =
        IgnoreArgument<&Original_GameWorld_ScreenCenterTileY, &GameWorld_ScreenCenterTileY>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(GameWorld_ClampCamera_MatchesOriginal) {
    using Adapter = IgnoreArgument<&Original_GameWorld_ClampCamera, &GameWorld_ClampCamera>;
    CompareUiQuery(ctx, &Adapter::CallOriginal, &Adapter::CallOurs, 5000);
}
S3RE_TEST(GameWorld_ClampCamera_ComparisonDetectsWrongImplementation) {
    using Adapter = IgnoreArgument<&Original_GameWorld_ClampCamera, &WrongClampCamera>;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUiQuery(probe, &Adapter::CallOriginal, &Adapter::CallOurs, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_PushUiFeedback_MatchesOriginal) {
    ComparePushUiFeedback(ctx, Original_GameWorld_PushUiFeedback, &GameWorld_PushUiFeedback, 5000);
}

namespace {

constexpr std::size_t kGroupBytes = 20 + 10 * 200;  // 0x7ad620..0x7ade04

// Saves the ten groups in S3.EXE's data and restores them afterwards.
class SavedGroups {
public:
    SavedGroups() { std::memcpy(saved_, Base(), kGroupBytes); }
    ~SavedGroups() { std::memcpy(Base(), saved_, kGroupBytes); }
    static std::uint8_t* Base() { return reinterpret_cast<std::uint8_t*>(kGroupCounts); }

private:
    std::uint8_t saved_[kGroupBytes];
};

void CompareGroupMenu(test::Context& ctx, GroupMenuFn candidate, int cases) {
    const SavedGroups saved;
    test::WorldPair worlds(ctx, offsetof(GameWorld, settlers) + 0x10000 * sizeof(layout::Settler));
    ThreadPair threads(worlds);
    static constexpr std::uint8_t kTypes[] = {0x24, 0x2c, 0x2b, 0x29, 0x28, 0x16, 0x17, 0x01, 0x23};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t group = ctx.rng().Below(10);
        const std::uint32_t count = ctx.rng().Chance(1, 3) ? ctx.rng().Below(2) : ctx.rng().Below(101);
        *reinterpret_cast<std::uint16_t*>(kGroupCounts + group * 2) = static_cast<std::uint16_t>(count);
        const std::uint8_t type = kTypes[ctx.rng().Below(sizeof(kTypes))];
        const bool allSame = ctx.rng().Chance(2, 3);
        const std::uint32_t localPlayer = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(4);
        worlds.Both([&](GameWorld* w) { w->local_player = static_cast<std::int32_t>(localPlayer); });
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t settler = ctx.rng().Below(0x10000);
            *reinterpret_cast<std::uint16_t*>(kGroupSettlers + group * 200 + k * 2) = static_cast<std::uint16_t>(settler);
            const std::uint8_t settlerType = allSame || ctx.rng().Chance(1, 2) ? type : kTypes[ctx.rng().Below(sizeof(kTypes))];
            const auto settlerOwner =
                static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? localPlayer : ctx.rng().Below(4));
            worlds.Both([&](GameWorld* w) {
                w->settlers[settler].art = settlerType;
                w->settlers[settler].owner = settlerOwner;
            });
        }
        const std::uint32_t menu = ctx.rng().Chance(1, 2) ? ctx.rng().Below(8) : ctx.rng().Next32();
        const std::uint32_t refresh = ctx.rng().Next32();
        const std::uint32_t field29c = ctx.rng().Next32();
        worlds.Both([&](GameWorld* w) {
            w->ui_special_menu = static_cast<std::int32_t>(menu);
            w->refresh_ui = static_cast<std::int32_t>(refresh);
            w->ui_2d29c = static_cast<std::int32_t>(field29c);
        });
        const std::uint32_t expected = Original_CLogicThread_UpdateGroupMenu(threads.original(), nullptr, group);
        const std::uint32_t actual = candidate(threads.ours(), nullptr, group);
        const auto base = [](GameWorld* w) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w)); };
        // refresh_ui (+0x2d27c) .. ui_2d29c (+0x2d29c)
        const std::size_t first = offsetof(GameWorld, refresh_ui);
        const std::size_t end = offsetof(GameWorld, ui_2d29c) + 4;
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
    GameWorld* world = ui::LogicThreadOf(self)->world;
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
    ui::LogicThreadView* thread = ui::LogicThreadOf(self);
    thread->unk074 = 0;
    if (thread->pending_ui_refresh == 0) {
        return 0;
    }
    thread->pending_ui_refresh = 0;
    GameWorld* world = thread->world;
    world->refresh_ui = 1;
    return reinterpret_cast<std::uint32_t>(world);
}

namespace {

void CompareClearPendingUiRefresh(test::Context& ctx, LogicThreadFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, refresh_ui) + 4);
    std::uint8_t threadA[sizeof(ui::LogicThreadView)], threadB[sizeof(ui::LogicThreadView)];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(threadA, sizeof(threadA));
        if (ctx.rng().Chance(1, 2)) {
            ui::LogicThreadOf(threadA)->pending_ui_refresh = 0;
        }
        std::memcpy(threadB, threadA, sizeof(threadB));
        ui::LogicThreadOf(threadA)->world = worlds.original();
        ui::LogicThreadOf(threadB)->world = worlds.ours();
        const std::uint32_t refresh = ctx.rng().Next32();
        worlds.Both([&](GameWorld* w) { w->refresh_ui = static_cast<std::int32_t>(refresh); });
        const std::uint32_t ra = Original_CLogicThread_ClearPendingUiRefresh(reinterpret_cast<CLogicThread*>(threadA), nullptr);
        const std::uint32_t rb = candidate(reinterpret_cast<CLogicThread*>(threadB), nullptr);
        const std::uint32_t expected = ra == reinterpret_cast<std::uint32_t>(worlds.original()) ? 1 : ra;
        const std::uint32_t actual = rb == reinterpret_cast<std::uint32_t>(worlds.ours()) ? 1 : rb;
        bool same = S3RE_EXPECT_EQ(expected, actual);
        same &= S3RE_EXPECT_MEM_EQ(threadA, threadB, offsetof(ui::LogicThreadView, world));
        same &= worlds.SameField(ctx, [](GameWorld* w) -> auto& { return w->refresh_ui; });
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

std::uint32_t __fastcall WrongClearPendingUiRefresh(CLogicThread* self, void* edx) {
    const std::uint32_t r = CLogicThread_ClearPendingUiRefresh(self, edx);
    ui::LogicThreadOf(self)->unk074 = 1;  // wrong: +0x74 not cleared
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

}  // namespace s3re::game
