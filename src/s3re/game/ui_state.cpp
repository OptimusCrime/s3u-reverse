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

std::uint32_t __fastcall GameWorld_IsUiSelectionMode(GameWorld* world, void*, std::uint32_t mode) {
    return Field(world, kUiSelectionMode) == mode ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsUiField298(GameWorld* world, void*, std::uint32_t value) {
    return Field(world, kUiField298) == value ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsUiSelectionParam1(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return Field(world, kUiSelectionParam1) == value ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsUiSelectionParam2(GameWorld* world, void*,
                                                       std::uint32_t value) {
    return Field(world, kUiSelectionParam2) == value ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsUiSelectionMode4Field298Is2(GameWorld* world, void*) {
    return (Field(world, kUiSelectionMode) == 4 && Field(world, kUiField298) == 2) ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsGameOutcome1(GameWorld*, void*, std::uint32_t) {
    return GameGlobal<std::uint32_t>(global_address::kGameOutcome) == 1 ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_ScreenCenterTileX(GameWorld* world, void*) {
    const std::uint32_t rowsBelowTop = 100u - CameraRow(world);
    const std::uint32_t skew = (Field(world, world_offset::kMapSize) + rowsBelowTop * 2u) >> 1;
    const auto width = GameGlobal<std::uint32_t>(global_address::kScreenWidth);
    const std::uint32_t halfColumns = (DivideTruncating(width + 0x17u, 16) + 0xau) >> 1;
    const std::uint32_t column = DivideTruncating(Field(world, kCameraX) - 0xd0u, 16);
    return column + (ScreenRows(2) - skew + halfColumns);
}

std::uint32_t __fastcall GameWorld_ScreenCenterTileY(GameWorld* world, void*) {
    return ScreenRows(1) + CameraRow(world) * 2u - 200u;
}

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

std::uint32_t __fastcall GameWorld_PushUiFeedback(GameWorld* world, void*, std::uint32_t value) {
    auto& count = WorldField<std::uint32_t>(world, kUiFeedbackCount);
    if (count == kUiFeedbackCapacity) {
        return count;
    }
    WorldField<std::uint32_t>(world, kUiFeedback + count * 4) = value;
    count = count + 1;
    return count;
}

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

}  // namespace s3re::game
