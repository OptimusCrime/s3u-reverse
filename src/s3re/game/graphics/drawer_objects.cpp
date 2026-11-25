// Queueing the animated objects of the draw data's cells (the unnamed object
// file 0x4d3050..0x4d65e0 between Volker\DrawBuilding.cpp and
// GameDataSave.cpp; called twice by the frame drawer 0x4b26c0).
//
// CDrawer::QueueCellObject (0x004d5a50; was Logic_sub_4d5a50): (DrawData*
//   data, column, row, x, y) thiscall, ret 0x14. The cell is the 48-byte
//   record [column][row] of the 130 x 130 grid at DrawData +0x500
//   (DrawCellObject below). Four GfxDword locals are constructed with 0
//   (0x5fc540). Nothing more if the frame count (the low 7 bits of the word
//   +0xa) is 0. Otherwise, with owner = byte +0x4 (0xff -> 0), type = bits
//   7..14 of the word (masked to a byte) and k = Render::Entry204(owner)
//   0x4c7ab0 (probably the owner's race):
//   - the shadow: CGraphicsData::ShadowAnimationPointer(id) 0x5fabb0 with
//     id = g_cellObjectAnimations[type * 8 + k] into the `data` argument's
//     stack slot, then its frame (count - 1) as GfxDword::ListImageChecked
//     0x5fcce0 (the animation pointer in a local as `this`, the result
//     again into the `data` slot), queued by CDrawer::QueueShadow 0x4d7c90
//     (row 2 * row, x, y - byte +0x0, image);
//   - the sprite: Render::Entry204(owner) again (the owner in the `row`
//     slot, 0 for 0xff), CGraphicsData::AnimationPointer(id) 0x5faaf0 with
//     id = g_cellObjectAnimations[type * 8 + 4 + k] into that slot, the
//     frame's image inlined: frame (count - 1) if below the animation's
//     byte +7, else frame 0, the dword +8 + 4 * frame added to the
//     animation's address; queued by CDrawer::QueueSprite 0x4d7e10 (row
//     2 * row - 1, x, y - byte +0x0, image, 4 * (byte +0x5 & 0x3f)).
//   The table index is 32-bit arithmetic, as the original's.
//   The second call site passes only 4 arguments; the fifth (y) is then
//   whatever its caller's stack holds there (ours reads the same slot).

#include "game/global_access.h"
#include "game/graphics/drawer_layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// One cell's object in the draw data (48 bytes; 130 x 130 at DrawData
// +0x500, [column][row]). Only the fields this code reads are named.
struct DrawCellObject {
    std::uint8_t lift;        // +0x00 drawn this many pixels higher
    std::uint8_t unk01[3];
    std::uint8_t owner;       // +0x04 player (0xff: none, drawn as 0)
    std::uint8_t direction;   // +0x05 low 6 bits: the sprite's parameter / 4
    std::uint8_t unk06[4];
    std::uint16_t animation;  // +0x0a low 7 bits: the frame + 1 (0 = nothing), bits 7..14: the object type
    std::uint8_t unk0c[0x30 - 0x0c];
};
#pragma pack(pop)
static_assert(sizeof(DrawCellObject) == 0x30);

constexpr std::size_t kCellGridOffset = 0x500;
constexpr std::uint32_t kCellGridRows = 130;
constexpr std::uint32_t kNoOwner = 0xff;
constexpr std::uint32_t kFrameMask = 0x7f;
constexpr std::uint32_t kAnimationsPerType = 8;  // 4 shadows, then 4 sprites (by Entry204's value)
constexpr std::uint32_t kSpriteAnimations = 4;

constexpr std::uintptr_t kGfxDwordConstruct = 0x005fc540;
constexpr std::uintptr_t kEntry204 = 0x004c7ab0;
constexpr std::uintptr_t kShadowAnimationPointer = 0x005fabb0;
constexpr std::uintptr_t kListImageChecked = 0x005fcce0;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kQueueShadow = 0x004d7c90;
constexpr std::uintptr_t kQueueSprite = 0x004d7e10;

using Entry204Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t i);
using OutGetterFn = std::uint32_t*(__fastcall*)(void* self, void* edx, std::uint32_t* out, std::uint32_t id);
using ConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t value);
using QueueShadowFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t row, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t sprite);
using QueueSpriteFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t row, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t sprite, std::uint32_t param);

std::uint32_t AnimationId(std::uint32_t index) {
    return (&g_cellObjectAnimations[0])[index];  // 32-bit index, as the original
}

}  // namespace

using QueueCellObjectFn = void(__fastcall*)(CDrawer* self, void* edx, const std::uint8_t* data, std::uint32_t column,
                                            std::uint32_t row, std::uint32_t x, std::uint32_t y);
void __fastcall CDrawer_QueueCellObject(CDrawer* self, void* edx, const std::uint8_t* data, std::uint32_t column,
                                        std::uint32_t row, std::uint32_t x, std::uint32_t y);
S3RE_HOOK(0x004d5a50, CDrawer_QueueCellObject, QueueCellObjectFn, Original_CDrawer_QueueCellObject,
          CDrawer_QueueCellObject);

// Purpose: Queues the shadow and the sprite of an animated object of a map
// cell (from the draw data) for drawing in the frame.
void __fastcall CDrawer_QueueCellObject(CDrawer* self, void*, const std::uint8_t* data, std::uint32_t column,
                                        std::uint32_t row, std::uint32_t x, std::uint32_t y) {
    std::uint32_t locals[4];
    for (std::uint32_t& local : locals) {
        reinterpret_cast<ConstructFn>(kGfxDwordConstruct)(&local, nullptr, 0);
    }
    const auto& cell = *reinterpret_cast<const DrawCellObject*>(
        data + kCellGridOffset + (column * kCellGridRows + row) * sizeof(DrawCellObject));
    const std::uint32_t frames = cell.animation & kFrameMask;
    if (frames == 0) {
        return;
    }
    std::uint32_t owner = cell.owner;
    if (owner == kNoOwner) {
        owner = 0;
    }
    const std::uint32_t typeBase = ((cell.animation >> 7) & 0xff) * kAnimationsPerType;
    const std::uint32_t param = static_cast<std::uint32_t>(cell.direction & 0x3f) * 4;
    const std::int32_t frame = static_cast<std::int32_t>(frames) - 1;
    const std::uint32_t screenY = y - cell.lift;

    // The shadow.
    std::uint32_t slot = 0;
    const std::uint32_t k = reinterpret_cast<Entry204Fn>(kEntry204)(self, nullptr, owner);
    std::uint32_t* animation = reinterpret_cast<OutGetterFn>(kShadowAnimationPointer)(
        self->graphics, nullptr, &slot, AnimationId(typeBase + k));
    locals[2] = *animation;  // (the original's GfxDword local at [esp+0xc])
    const std::uint32_t shadow =
        *reinterpret_cast<OutGetterFn>(kListImageChecked)(&locals[2], nullptr, &slot, static_cast<std::uint32_t>(frame));
    locals[3] = shadow;
    reinterpret_cast<QueueShadowFn>(kQueueShadow)(self, nullptr, 2 * row, x, screenY, shadow);

    // The sprite (the frame's image inlined).
    std::uint32_t spriteSlot = owner;
    const std::uint32_t k2 = reinterpret_cast<Entry204Fn>(kEntry204)(self, nullptr, owner);
    const std::uint32_t sequence = *reinterpret_cast<OutGetterFn>(kAnimationPointer)(
        self->graphics, nullptr, &spriteSlot, AnimationId(typeBase + kSpriteAnimations + k2));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(sequence));
    std::uint32_t offset;
    if (frame >= 0 && frame < static_cast<std::int32_t>(bytes[7])) {
        std::memcpy(&offset, bytes + 8 + 4 * frame, sizeof(offset));
    } else {
        std::memcpy(&offset, bytes + 8, sizeof(offset));
    }
    reinterpret_cast<QueueSpriteFn>(kQueueSprite)(self, nullptr, 2 * row - 1, x, screenY, offset + sequence, param);
}

// ---------------------------------------------------------------------------
// Tests: random cells and arguments; the callees replaced by recording stubs
// (values passed, with pointer arguments to the stack recorded by what they
// point to); the getters write values derived from their arguments, the
// animation pointer one into a random sequence record.
// ---------------------------------------------------------------------------

namespace {

struct Log {
    std::vector<std::uint32_t> values;
    std::uint8_t* sequence = nullptr;  // where AnimationPointer's results point
};
Log* g_log = nullptr;

std::uint32_t Mix(std::uint32_t v) {
    v ^= v >> 13;
    v *= 0x9e3779b1u;
    return v ^ (v >> 16);
}

void RunQueueCellObject(QueueCellObjectFn fn, test::Rng& rng, CDrawer* drawer, std::uint8_t* data, Log& log) {
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
    stubs.Install<1, kEntry204>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {1, self, a[0]});
        return Mix(a[0]) & 3;
    });
    stubs.Install<2, kShadowAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        auto* out = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(a[0]));
        log.values.insert(log.values.end(), {2, self, a[1]});
        *out = Mix(a[1] + 1);
        return a[0];
    });
    stubs.Install<2, kListImageChecked>([&](CGameData* world, std::uint32_t self, const std::uint32_t* a) {
        // `this` is a GfxDword local on the stack: its value is recorded.
        const std::uint32_t value =
            *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(world) + self);
        auto* out = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(a[0]));
        log.values.insert(log.values.end(), {3, value, a[1]});
        *out = Mix(a[1] + 7);
        return a[0];
    });
    stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        auto* out = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(a[0]));
        log.values.insert(log.values.end(), {4, self, a[1]});
        *out = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(log.sequence));
        return a[0];
    });
    stubs.Install<4, kQueueShadow>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {5, self, a[0], a[1], a[2], a[3]});
        return a[0];
    });
    stubs.Install<5, kQueueSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {6, self, a[0], a[1], a[2], a[3] - static_cast<std::uint32_t>(
                                                                              reinterpret_cast<std::uintptr_t>(log.sequence)),
                                            a[4]});
        return a[0];
    });
    const std::uint32_t column = rng.Below(kCellGridRows);
    const std::uint32_t row = rng.Below(kCellGridRows);
    const std::uint32_t x = rng.Next32();
    const std::uint32_t y = rng.Chance(1, 2) ? rng.Below(600) : rng.Next32();
    fn(drawer, nullptr, data, column, row, x, y);
}

void CompareQueueCellObject(test::Context& ctx, QueueCellObjectFn candidate, int cases) {
    constexpr std::size_t kDataSize = kCellGridOffset + kCellGridRows * kCellGridRows * sizeof(DrawCellObject);
    std::vector<std::uint8_t> data(kDataSize);
    std::vector<std::uint8_t> drawerBytes(sizeof(CDrawer));
    std::uint8_t sequence[8 + 4 * 128];
    ctx.rng().Fill(data.data(), data.size());  // random cells; the drawer's bytes are only passed on
    ctx.rng().Fill(drawerBytes.data(), drawerBytes.size());
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(sequence, sizeof(sequence));
        sequence[7] = static_cast<std::uint8_t>(ctx.rng().Below(130));
        const std::uint64_t seed = ctx.rng().Next64();
        Log a, b;
        a.sequence = b.sequence = sequence;
        test::Rng ra(seed), rb(seed);
        auto* drawer = reinterpret_cast<CDrawer*>(drawerBytes.data());
        RunQueueCellObject(Original_CDrawer_QueueCellObject, ra, drawer, data.data(), a);
        RunQueueCellObject(candidate, rb, drawer, data.data(), b);
        if (!S3RE_EXPECT(a.values == b.values)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: one pixel lower.
void __fastcall WrongQueueCellObject(CDrawer* self, void* edx, const std::uint8_t* data, std::uint32_t column,
                                     std::uint32_t row, std::uint32_t x, std::uint32_t y) {
    CDrawer_QueueCellObject(self, edx, data, column, row, x, y + 1);
}

}  // namespace

S3RE_TEST(CDrawer_QueueCellObject_MatchesOriginal) {
    CompareQueueCellObject(ctx, &CDrawer_QueueCellObject, 3000);
}

S3RE_TEST(CDrawer_QueueCellObject_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueCellObject(probe, &WrongQueueCellObject, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
