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
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::size_t kCameraX = 0x2d25c;
constexpr std::size_t kCameraY = 0x2d260;
constexpr std::size_t kUiSelectionMode = 0x2d284;
constexpr std::size_t kUiSelectionParam1 = 0x2d288;
constexpr std::size_t kUiSelectionParam2 = 0x2d28c;
constexpr std::size_t kUiField298 = 0x2d298;

constexpr std::size_t kTiles = 0x67444;
constexpr std::size_t kTileByte4 = 0x4;
constexpr std::size_t kLogicThreadWorld = 0x3a0;  // in CLogicThread

constexpr std::size_t kUiFeedback = 0x1539484;
constexpr std::size_t kUiFeedbackCount = 0x15394ac;
constexpr std::uint32_t kUiFeedbackCapacity = 10;

std::uint32_t Field(GameWorld* world, std::size_t offset) {
    return WorldField<std::uint32_t>(world, offset);
}

// Signed division by a positive constant, truncating toward zero, of a
// 32-bit wrapped sum.
std::uint32_t DivideTruncating(std::uint32_t value, std::int32_t divisor) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) / divisor);
}

// Rows of 9 pixels above the camera centre, offset by 0x640 pixels.
std::uint32_t CameraRow(GameWorld* world) {
    return DivideTruncating(Field(world, kCameraY) + 0x640u, 18);
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
    return Field(world, kUiSelectionMode) == mode ? 1 : 0;
}

// Purpose: Whether a UI state field (meaning unknown) has a given value.
std::uint32_t __fastcall GameWorld_IsUiField298(GameWorld* world, void*, std::uint32_t value) {
    return Field(world, kUiField298) == value ? 1 : 0;
}

// Purpose: Whether the first parameter of the current UI selection (e.g. which building type) has a given
// value.
std::uint32_t __fastcall GameWorld_IsUiSelectionParam1(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return Field(world, kUiSelectionParam1) == value ? 1 : 0;
}

// Purpose: Whether the second parameter of the current UI selection has a given value.
std::uint32_t __fastcall GameWorld_IsUiSelectionParam2(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return Field(world, kUiSelectionParam2) == value ? 1 : 0;
}

// Purpose: Whether the UI is in selection mode 4 with a particular sub-state. A shortcut check used by the
// UI.
std::uint32_t __fastcall GameWorld_IsUiSelectionMode4Field298Is2(GameWorld* world, void*) {
    return (Field(world, kUiSelectionMode) == 4 && Field(world, kUiField298) == 2) ? 1 : 0;
}

// Purpose: Whether the game has ended with outcome 1 (probably 'won'). Queried by the user interface.
std::uint32_t __fastcall GameWorld_IsGameOutcome1(GameWorld*, void*, std::uint32_t) {
    return GameGlobal<std::uint32_t>(global_address::kGameOutcome) == 1 ? 1 : 0;
}

// Purpose: Which tile is at the centre of the screen (x). Used e.g. to know where the player is looking.
std::uint32_t __fastcall GameWorld_ScreenCenterTileX(GameWorld* world, void*) {
    const std::uint32_t rowsBelowTop = 100u - CameraRow(world);
    const std::uint32_t skew = (Field(world, world_offset::kMapSize) + rowsBelowTop * 2u) >> 1;
    const auto width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
    const std::uint32_t halfColumns = (DivideTruncating(width + 0x17u, 16) + 0xau) >> 1;
    const std::uint32_t column = DivideTruncating(Field(world, kCameraX) - 0xd0u, 16);
    return column + (ScreenRows(2) - skew + halfColumns);
}

// Purpose: Which tile is at the centre of the screen (y).
std::uint32_t __fastcall GameWorld_ScreenCenterTileY(GameWorld* world, void*) {
    return ScreenRows(1) + CameraRow(world) * 2u - 200u;
}

// Purpose: Keeps the camera inside the map after it has been moved.
std::uint32_t __fastcall GameWorld_ClampCamera(GameWorld* world, void*) {
    auto& cameraX = WorldField<std::int32_t>(world, kCameraX);
    auto& cameraY = WorldField<std::int32_t>(world, kCameraY);
    const std::uint32_t size = Field(world, world_offset::kMapSize);
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
    auto& count = WorldField<std::uint32_t>(world, kUiFeedbackCount);
    if (count == kUiFeedbackCapacity) {
        return count;
    }
    WorldField<std::uint32_t>(world, kUiFeedback + count * 4) = value;
    count = count + 1;
    return count;
}

// Purpose: Moves the camera so that a tile is in the middle of the screen (e.g. when jumping to a message's
// location).
std::uint32_t __fastcall GameWorld_CenterCameraOnTile(GameWorld* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    const auto width = static_cast<std::int32_t>(GameGlobal<std::uint32_t>(global_address::kScreenWidth));
    const auto height = static_cast<std::int32_t>(GameGlobal<std::uint32_t>(global_address::kScreenHeight));
    const std::uint32_t size = Field(world, world_offset::kMapSize);
    WorldField<std::uint32_t>(world, kCameraX) =
        (size + x * 2u + 0x18u - y) * 8u - static_cast<std::uint32_t>(width / 2);
    const std::uint32_t tileByte4 = WorldField<std::uint8_t>(world, kTiles + (x * 0x300u + y) * 24u + kTileByte4);
    WorldField<std::uint32_t>(world, kCameraY) =
        0u - static_cast<std::uint32_t>(height / 2) - tileByte4 + y * 9u + 0xc8u;
    return GameWorld_ClampCamera(world, nullptr);
}

struct CLogicThread;  // opaque

// Purpose: The same as GameWorld::PushUiFeedback, called from the user interface code through the logic
// thread.
std::uint32_t __fastcall CLogicThread_PushUiFeedback(CLogicThread* self, void*, std::uint32_t value) {
    GameWorld* world = *reinterpret_cast<GameWorld**>(reinterpret_cast<std::uint8_t*>(self) + kLogicThreadWorld);
    return GameWorld_PushUiFeedback(world, nullptr, value);
}

using PositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using LogicValueFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t value);

namespace {

constexpr std::size_t kLocalPlayer = 0x2d2d4;
constexpr std::size_t kCameraField264 = 0x2d264;
constexpr std::size_t kPlayerRecords = 0x11943dc;
constexpr std::size_t kPlayerRecordBytes = 0x148;
constexpr std::size_t kPlayerPositionX = 0x10;
constexpr std::size_t kPlayerPositionY = 0x14;

}  // namespace

// Purpose: Moves the camera to the local player's home position (probably their castle), e.g. at the start
// of a game.
std::uint32_t __fastcall GameWorld_CenterCameraOnLocalPlayer(GameWorld* world, void*) {
    const std::size_t record = kPlayerRecords + Field(world, kLocalPlayer) * kPlayerRecordBytes;
    const std::uint32_t result =
        GameWorld_CenterCameraOnTile(world, nullptr, Field(world, record + kPlayerPositionX),
                                     Field(world, record + kPlayerPositionY));
    WorldField<std::uint32_t>(world, kCameraField264) = 0;
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
constexpr std::size_t kSettlers = 0x12a5e60;
constexpr std::size_t kSettlerBytes = 0x40;
constexpr std::size_t kUiField29c = 0x2d29c;
constexpr std::size_t kUiRedraw = 0x2d27c;

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
    GameWorld* world = *reinterpret_cast<GameWorld**>(reinterpret_cast<std::uint8_t*>(self) + kLogicThreadWorld);
    const std::uint32_t count = GroupWord(kGroupCounts + group * 2);
    const std::uintptr_t settlers = kGroupSettlers + group * 200;
    auto settlerByte = [&](std::uint32_t settler, std::size_t field) {
        return static_cast<std::uint32_t>(WorldField<std::uint8_t>(world, kSettlers + settler * kSettlerBytes + field));
    };
    std::uint32_t changed = 1;
    if (count == 1) {
        const std::uint32_t settler = GroupWord(settlers);
        if (settlerByte(settler, 0) == Field(world, kLocalPlayer) && settlerByte(settler, 1) == 0x24 &&
            Field(world, kUiField298) != 1) {
            WorldField<std::uint32_t>(world, kUiField298) = 1;
            WorldField<std::uint32_t>(world, kUiField29c) = 0;
            WorldField<std::uint32_t>(world, kUiRedraw) = 1;
            return reinterpret_cast<std::uint32_t>(world);
        }
    }
    if (count != 0) {
        const std::uint32_t type = settlerByte(GroupWord(settlers), 1);
        bool same = true;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (settlerByte(GroupWord(settlers + i * 2), 1) != type) {
                same = false;
            }
        }
        if (same) {
            std::uint32_t menu = 0;
            switch (type) {
                case 0x2c: menu = 2; break;
                case 0x2b: menu = 3; break;
                case 0x29: menu = 4; break;
                case 0x28: menu = 5; break;
                case 0x16: menu = 6; break;
                case 0x17: menu = 7; break;
                default: break;
            }
            if (menu != 0) {
                WorldField<std::uint32_t>(world, kUiField298) = menu;
                WorldField<std::uint32_t>(world, kUiRedraw) = 1;
                return reinterpret_cast<std::uint32_t>(world);
            }
        }
        changed = 1;
    }
    if (Field(world, kUiField298) != 0) {
        WorldField<std::uint32_t>(world, kUiField298) = 0;
        WorldField<std::uint32_t>(world, kUiRedraw) = changed;
    }
    return reinterpret_cast<std::uint32_t>(world);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kUiWorld = 0x2d300;

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
        for (std::size_t field : {kUiSelectionMode, kUiSelectionParam1, kUiSelectionParam2,
                                  kUiField298, kCameraX, kCameraY, world_offset::kMapSize}) {
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
    test::WorldPair worlds(ctx, kUiFeedbackCount + 4);
    for (int i = 0; i < cases; ++i) {
        worlds.Set<std::uint32_t>(kUiFeedbackCount, ctx.rng().Below(kUiFeedbackCapacity + 1));
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = original(worlds.original(), nullptr, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, kUiFeedback, kUiFeedbackCount + 4 - kUiFeedback) ||
            (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: rounds the column down instead of toward zero.
std::uint32_t __fastcall WrongScreenCenterTileX(GameWorld* world, void* edx) {
    const auto column = static_cast<std::int32_t>(Field(world, kCameraX) - 0xd0u);
    return GameWorld_ScreenCenterTileX(world, edx) - DivideTruncating(column, 16) +
           static_cast<std::uint32_t>(column >> 4);
}

// Wrong: uses the view height for the y limit.
std::uint32_t __fastcall WrongClampCamera(GameWorld* world, void* edx) {
    const auto width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
    GameGlobal<std::uint32_t>(global_address::kScreenWidth) =
        GameGlobal<std::uint32_t>(global_address::kScreenHeight);
    std::int32_t x = WorldField<std::int32_t>(world, kCameraX);
    const std::uint32_t result = GameWorld_ClampCamera(world, edx);
    GameGlobal<std::uint32_t>(global_address::kScreenWidth) = width;
    WorldField<std::int32_t>(world, kCameraX) = x;
    const std::uint32_t size = Field(world, world_offset::kMapSize);
    if (x < 0) {
        x = 0;
    }
    const std::uint32_t maxX = size * 24u - width + 0x167u;
    if (x >= static_cast<std::int32_t>(maxX + 1u)) {
        x = static_cast<std::int32_t>(maxX);
    }
    WorldField<std::int32_t>(world, kCameraX) = x;
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_CenterCameraOnTile_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTiles + 400 * 0x300 * 24);
    GlobalOverride width(global_address::kScreenWidth);
    GlobalOverride height(global_address::kScreenHeight);
    for (int i = 0; i < 5000; ++i) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx));
        width.Set(ctx.rng().Chance(3, 4) ? 640 + ctx.rng().Below(1024) : InterestingValue(ctx));
        height.Set(ctx.rng().Chance(3, 4) ? 480 + ctx.rng().Below(768) : InterestingValue(ctx));
        const std::uint32_t x = ctx.rng().Below(400);
        const std::uint32_t y = ctx.rng().Below(400);
        const std::uint32_t expected = Original_GameWorld_CenterCameraOnTile(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = GameWorld_CenterCameraOnTile(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, kCameraX, 8) ||
            (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u)", i, x, y);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(GameWorld_CenterCameraOnLocalPlayer_MatchesOriginal) {
    test::WorldPair worlds(ctx, kPlayerRecords + 21 * kPlayerRecordBytes);
    GlobalOverride width(global_address::kScreenWidth);
    GlobalOverride height(global_address::kScreenHeight);
    for (int i = 0; i < 3000; ++i) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, ctx.rng().Chance(1, 2) ? 384 : InterestingValue(ctx));
        width.Set(640 + ctx.rng().Below(1024));
        height.Set(480 + ctx.rng().Below(768));
        const std::uint32_t player = ctx.rng().Below(20);
        worlds.Set<std::uint32_t>(kLocalPlayer, player);
        worlds.Set<std::uint32_t>(kPlayerRecords + player * kPlayerRecordBytes + kPlayerPositionX, ctx.rng().Below(384));
        worlds.Set<std::uint32_t>(kPlayerRecords + player * kPlayerRecordBytes + kPlayerPositionY, ctx.rng().Below(384));
        const std::uint32_t expected = Original_GameWorld_CenterCameraOnLocalPlayer(worlds.original(), nullptr);
        const std::uint32_t actual = GameWorld_CenterCameraOnLocalPlayer(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, kCameraX, 12) ||
            (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(CLogicThread_PushUiFeedback_MatchesOriginal) {
    test::WorldPair worlds(ctx, kUiFeedbackCount + 4);
    // Two fake logic threads, each pointing at one of the worlds.
    std::uint8_t threadA[kLogicThreadWorld + 4] = {};
    std::uint8_t threadB[kLogicThreadWorld + 4] = {};
    *reinterpret_cast<GameWorld**>(threadA + kLogicThreadWorld) = worlds.original();
    *reinterpret_cast<GameWorld**>(threadB + kLogicThreadWorld) = worlds.ours();
    for (int i = 0; i < 2000; ++i) {
        worlds.Set<std::uint32_t>(kUiFeedbackCount, ctx.rng().Below(kUiFeedbackCapacity + 1));
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected =
            Original_CLogicThread_PushUiFeedback(reinterpret_cast<CLogicThread*>(threadA), nullptr, value);
        const std::uint32_t actual = CLogicThread_PushUiFeedback(reinterpret_cast<CLogicThread*>(threadB), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, kUiFeedback, kUiFeedbackCount + 4 - kUiFeedback)) {
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
    test::WorldPair worlds(ctx, kSettlers + 0x10000 * kSettlerBytes);
    std::uint8_t threadA[kLogicThreadWorld + 4] = {};
    std::uint8_t threadB[kLogicThreadWorld + 4] = {};
    *reinterpret_cast<GameWorld**>(threadA + kLogicThreadWorld) = worlds.original();
    *reinterpret_cast<GameWorld**>(threadB + kLogicThreadWorld) = worlds.ours();
    static constexpr std::uint8_t kTypes[] = {0x24, 0x2c, 0x2b, 0x29, 0x28, 0x16, 0x17, 0x01, 0x23};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t group = ctx.rng().Below(10);
        const std::uint32_t count = ctx.rng().Chance(1, 3) ? ctx.rng().Below(2) : ctx.rng().Below(101);
        *reinterpret_cast<std::uint16_t*>(kGroupCounts + group * 2) = static_cast<std::uint16_t>(count);
        const std::uint8_t type = kTypes[ctx.rng().Below(sizeof(kTypes))];
        const bool allSame = ctx.rng().Chance(2, 3);
        const std::uint32_t localPlayer = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(4);
        worlds.Set<std::uint32_t>(kLocalPlayer, localPlayer);
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t settler = ctx.rng().Below(0x10000);
            *reinterpret_cast<std::uint16_t*>(kGroupSettlers + group * 200 + k * 2) = static_cast<std::uint16_t>(settler);
            const std::uint8_t settlerType = allSame || ctx.rng().Chance(1, 2) ? type : kTypes[ctx.rng().Below(sizeof(kTypes))];
            worlds.Set<std::uint8_t>(kSettlers + settler * kSettlerBytes + 1, settlerType);
            worlds.Set<std::uint8_t>(kSettlers + settler * kSettlerBytes,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? localPlayer : ctx.rng().Below(4)));
        }
        worlds.Set<std::uint32_t>(kUiField298, ctx.rng().Chance(1, 2) ? ctx.rng().Below(8) : ctx.rng().Next32());
        worlds.Set<std::uint32_t>(kUiRedraw, ctx.rng().Next32());
        worlds.Set<std::uint32_t>(kUiField29c, ctx.rng().Next32());
        const std::uint32_t expected =
            Original_CLogicThread_UpdateGroupMenu(reinterpret_cast<CLogicThread*>(threadA), nullptr, group);
        const std::uint32_t actual = candidate(reinterpret_cast<CLogicThread*>(threadB), nullptr, group);
        const auto base = [](GameWorld* w) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w)); };
        if (!S3RE_EXPECT_EQ(expected - base(worlds.original()), actual - base(worlds.ours())) ||
            !worlds.Same(ctx, kUiRedraw, kUiField29c + 4 - kUiRedraw)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: group %u, count %u, type %#x", i, group, count, type);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: type 0x17 gets menu 6 like 0x16.
std::uint32_t __fastcall WrongGroupMenu(CLogicThread* self, void* edx, std::uint32_t group) {
    const std::uint32_t result = CLogicThread_UpdateGroupMenu(self, edx, group);
    GameWorld* world = *reinterpret_cast<GameWorld**>(reinterpret_cast<std::uint8_t*>(self) + kLogicThreadWorld);
    if (Field(world, kUiField298) == 7) {
        WorldField<std::uint32_t>(world, kUiField298) = 6;
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

}  // namespace s3re::game
