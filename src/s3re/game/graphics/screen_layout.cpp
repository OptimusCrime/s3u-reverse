// The display-mode layout and two helpers of the in-game drawer's ambient
// sounds (Volker\DrawBuilding.cpp's object file 0x4c6d40..0x4c85a0).
//
// Render::SetScreenLayout (0x004c6d70, was Render_sub_4c6d70): (mode)
//   ret 4; ECX (the drawer, from 0x4c6d40) not used. For mode 0, 1, 2
//   (640x480, 800x600, 1024x768; 0x4c6d40 reads it from the configuration)
//   sets the layout globals (g_displayMode .. g_drawOriginY, g_layout7dfd58,
//   values below); any other mode changes nothing. EAX: 480 for mode 0, 8
//   for modes 1 and 2, else the mode (the caller ignores it).
// CDrawer::PlayAmbientSounds (0x004c7730, was Render_sub_4c7730;
//   wip/logic-r4): (draw data) thiscall, ret 4; called once per drawn frame
//   (0x4b3f8b). When the draw data's +0x5c is not -1 and sound is on
//   (CSoundData::IsEnabled 0x5ff4d0 on +0x5f6bc): for seven ambient sounds
//   in a fixed order (kAmbientSounds), when the kind's source count is above
//   its minimum (signed) and the CRT's rand() 0x61f802 (called only then)
//   is above its threshold: pan = AmbientSoundPan(kind), volume =
//   AmbientSoundVolume(kind, minimum, full, offset), and the effect
//   GetEffect(id, -1, volume, pan, 0) 0x5ff4f0 plays (CSoundEffect::Play
//   0x5c2fc0). Then, if sound is on (asked again), the draw data's queued
//   sound effects (+0x238: ids, +0x260: count) play with volume and pan 0.
//   EAX not used by the caller.
// CDrawer::ApplyScreenLayout (0x004c6d40, was Render_sub_4c6d40; wip/logic-r4):
//   () thiscall: +0xf0 = -1, then SetScreenLayout(g_config's screen layout,
//   read with the folded getter 0x49dec0); EAX is SetScreenLayout's.
// Render::IsNearView (0x004c7b50, was Render_sub_4c7b50): (x, y) stdcall,
//   ret 8: 1 if the screen position is within 30 pixels around the drawn
//   area (g_drawOriginX - 30 <= x < g_displayWidth + 30 and -30 <= y <=
//   g_displayHeight + 30, signed), else 0. Callers count ambient sound
//   sources with it (0x4d3050, 0x4c85a0).
// CDrawer::AmbientSoundPan (0x004c79f0, was Render_sub_4c79f0): (kind)
//   thiscall, ret 4: the stereo position of an ambient sound in DirectSound
//   pan units, from its sources left and right of the screen centre: with
//   left > right, 10000 - left * 20000 / (left + right), else right * 20000
//   / (left + right) - 10000 (signed 32-bit products and divisions, which
//   fault on left + right == 0, as in the original).
// CDrawer::AmbientSoundVolume (0x004c7a50, was Render_sub_4c7a50): (kind,
//   low, high, offset) thiscall, ret 0x10: the volume in hundredths of a
//   dB: the source count (at most `high`), (count - low) * 1500 / (high -
//   low) + offset - 1500, clamped to -10000..0 (signed).

#include "game/globals.h"
#include "game/graphics/drawer_layout.h"
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

// The globals of one display mode, in the order SetScreenLayout writes them
// for mode 0 (the other modes write the same values in another order; no
// global is read in between, so the order doesn't show).
struct ScreenLayout {
    std::int32_t mode, width, height, bitsPerPixel, viewWidth, viewHeight, layout20, sidebarWidth;
    std::int32_t minimapWidth, layout2c, layout30, layout34, cellSize, originX, originY;
    std::uint32_t result;  // EAX
};

constexpr ScreenLayout kLayouts[] = {
    {1, 640, 480, 16, 504, 480, 437, 136, 128, 5, 10, 142, 3, 8, 152, 480},
    {2, 800, 600, 16, 624, 600, 540, 176, 160, 8, 5, 170, 4, 8, 171, 8},
    {3, 1024, 768, 16, 808, 768, 697, 216, 208, 6, 8, 224, 5, 8, 228, 8},
};

constexpr std::int32_t kViewMargin = 30;  // IsNearView: pixels around the drawn area
constexpr std::int32_t kPanRange = 10000;  // DirectSound: DSBPAN_LEFT..DSBPAN_RIGHT
constexpr std::int32_t kSilence = -10000;  // DirectSound: DSBVOLUME_MIN
constexpr std::int32_t kVolumeSpan = 1500;  // AmbientSoundVolume: count low..high spans 15 dB

}  // namespace

using ModeFn = std::uint32_t(__stdcall*)(std::uint32_t mode);
using NearViewFn = std::uint32_t(__stdcall*)(std::int32_t x, std::int32_t y);
using PanFn = std::int32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t kind);
using VolumeFn = std::int32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t kind, std::int32_t low,
                                           std::int32_t high, std::int32_t offset);

// Purpose: Sets up the screen layout (screen size, view, sidebar, minimap and
// cell sizes) for the chosen display resolution.
std::uint32_t __stdcall Render_SetScreenLayout(std::uint32_t mode) {
    if (mode >= sizeof(kLayouts) / sizeof(kLayouts[0])) {
        return mode;
    }
    const ScreenLayout& layout = kLayouts[mode];
    g_displayMode = layout.mode;
    g_displayWidth = layout.width;
    g_displayHeight = layout.height;
    g_displayBitsPerPixel = layout.bitsPerPixel;
    g_screenWidth = static_cast<std::uint32_t>(layout.viewWidth);
    g_screenHeight = static_cast<std::uint32_t>(layout.viewHeight);
    g_layout730f20 = layout.layout20;
    g_sidebarWidth = layout.sidebarWidth;
    g_layout7dfd58 = 0;
    g_minimapWidth = layout.minimapWidth;
    g_layout730f2c = layout.layout2c;
    g_layout730f30 = layout.layout30;
    g_layout730f34 = layout.layout34;
    g_uiCellSize = layout.cellSize;
    g_drawOriginX = static_cast<std::uint32_t>(layout.originX);
    g_drawOriginY = static_cast<std::uint32_t>(layout.originY);
    return layout.result;
}

// Purpose: Whether a screen position is on or just around the visible part
// of the map (e.g. to count the sources of ambient sounds).
std::uint32_t __stdcall Render_IsNearView(std::int32_t x, std::int32_t y) {
    const auto left = static_cast<std::int32_t>(g_drawOriginX.get()) - kViewMargin;
    if (x < left || x >= g_displayWidth + kViewMargin) {
        return 0;
    }
    if (y < -kViewMargin || y > g_displayHeight + kViewMargin) {
        return 0;
    }
    return 1;
}

// Purpose: Where between the left and right speaker an ambient sound plays,
// from where on the screen its sources are.
std::int32_t __fastcall CDrawer_AmbientSoundPan(CDrawer* self, void*, std::uint32_t kind) {
    const std::int32_t left = DrawerOf(self)->ambient_left[kind];
    const std::int32_t right = DrawerOf(self)->ambient_right[kind];
    // (32-bit wrapping products, as the original's lea chain computes them.)
    const auto total = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) + static_cast<std::uint32_t>(right));
    if (left > right) {
        const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(left) * 20000u);
        return kPanRange - scaled / total;
    }
    const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(right) * 20000u);
    return scaled / total - kPanRange;
}

// Purpose: How loud an ambient sound plays, from how many of its sources are
// on the screen.
std::int32_t __fastcall CDrawer_AmbientSoundVolume(CDrawer* self, void*, std::uint32_t kind, std::int32_t low,
                                                   std::int32_t high, std::int32_t offset) {
    std::int32_t count = DrawerOf(self)->ambient_count[kind];
    if (count > high) {
        count = high;
    }
    const auto scaled = static_cast<std::int32_t>((static_cast<std::uint32_t>(count) - static_cast<std::uint32_t>(low)) *
                                                  static_cast<std::uint32_t>(kVolumeSpan));
    const auto span = static_cast<std::int32_t>(static_cast<std::uint32_t>(high) - static_cast<std::uint32_t>(low));
    const auto volume = static_cast<std::int32_t>(static_cast<std::uint32_t>(scaled / span) +
                                                  static_cast<std::uint32_t>(offset) -
                                                  static_cast<std::uint32_t>(kVolumeSpan));
    if (volume < kSilence) {
        return kSilence;
    }
    return volume > 0 ? 0 : volume;
}

#pragma pack(push, 1)
// The fields of the draw data (DrawData, draw_data.cpp) PlayAmbientSounds reads.
struct DrawDataSounds {
    std::uint8_t unk000[0x5c];
    std::int32_t ticks;           // +0x5c ticks since the last fill (-1: nothing new)
    std::uint8_t unk060[0x238 - 0x60];
    std::uint32_t effects[10];    // +0x238 sound effect ids queued by the logic
    std::uint32_t effect_count;   // +0x260
};
#pragma pack(pop)
static_assert(offsetof(DrawDataSounds, ticks) == 0x5c);
static_assert(offsetof(DrawDataSounds, effects) == 0x238);
static_assert(offsetof(DrawDataSounds, effect_count) == 0x260);

namespace {

constexpr std::uintptr_t kSoundIsEnabled = 0x005ff4d0;  // CSoundData::IsEnabled
constexpr std::uintptr_t kSoundGetEffect = 0x005ff4f0;  // CSoundData::GetEffect(id, variant, volume, pan, 0)
constexpr std::uintptr_t kEffectPlay = 0x005c2fc0;      // DirectX::CSoundEffect::Play
constexpr std::uintptr_t kCrtRand = 0x0061f802;         // the C runtime's rand()
constexpr std::uintptr_t kAmbientPan = 0x004c79f0;
constexpr std::uintptr_t kAmbientVolume = 0x004c7a50;
constexpr std::int32_t kAnyVariant = -1;

// One ambient sound: the source kind, the count it needs, the rand()
// threshold, the count of full volume, the volume offset (hundredths of a
// dB) and the effect id.
struct AmbientSound {
    std::uint32_t kind;
    std::int32_t minimum;
    std::int32_t chance;
    std::int32_t full;
    std::int32_t offset;
    std::uint32_t effect;
};

constexpr AmbientSound kAmbientSounds[] = {
    {4, 400, 30000, 6000, -2500, 0x43},  {3, 800, 30000, 2000, -2300, 0x44},
    {2, 2000, 30000, 10000, -2500, 0x49}, {0, 8000, 30000, 22000, -2500, 0x4a},
    {6, 200, 29000, 2000, -2000, 0x47},   {1, 2000, 32300, 17000, -2000, 0x45},
    {3, 800, 32000, 2000, -2000, 0x46},
};

bool SoundOn(const CDrawer* drawer) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*);
    return (reinterpret_cast<Fn>(kSoundIsEnabled)(drawer->sound, nullptr) & 0xff) != 0;
}

void PlayEffect(const CDrawer* drawer, std::uint32_t id, std::int32_t volume, std::int32_t pan) {
    using GetFn = void*(__fastcall*)(void*, void*, std::uint32_t, std::int32_t, std::int32_t, std::int32_t,
                                     std::uint32_t);
    void* const effect = reinterpret_cast<GetFn>(kSoundGetEffect)(drawer->sound, nullptr, id, kAnyVariant, volume,
                                                                  pan, 0);
    reinterpret_cast<std::uint32_t(__fastcall*)(void*, void*)>(kEffectPlay)(effect, nullptr);
}

}  // namespace

using PlayAmbientFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, const DrawDataSounds* data);

// Purpose: Plays the sounds of the surroundings (birds, sea, ...) now and
// then, louder the more of their sources are on the screen, and the sound
// effects the game logic queued for this frame.
std::uint32_t __fastcall CDrawer_PlayAmbientSounds(CDrawer* self, void*, const DrawDataSounds* data) {
    if (data->ticks != -1 && SoundOn(self)) {
        for (const AmbientSound& sound : kAmbientSounds) {
            if (self->ambient_count[sound.kind] <= sound.minimum) {
                continue;
            }
            if (reinterpret_cast<int(__cdecl*)()>(kCrtRand)() <= sound.chance) {
                continue;
            }
            using PanFnRaw = std::int32_t(__fastcall*)(CDrawer*, void*, std::uint32_t);
            using VolumeFnRaw = std::int32_t(__fastcall*)(CDrawer*, void*, std::uint32_t, std::int32_t, std::int32_t,
                                                          std::int32_t);
            const std::int32_t pan = reinterpret_cast<PanFnRaw>(kAmbientPan)(self, nullptr, sound.kind);
            const std::int32_t volume = reinterpret_cast<VolumeFnRaw>(kAmbientVolume)(
                self, nullptr, sound.kind, sound.minimum, sound.full, sound.offset);
            PlayEffect(self, sound.effect, volume, pan);
        }
    }
    if (SoundOn(self) && data->effect_count != 0) {
        for (std::uint32_t i = 0; i < data->effect_count; ++i) {
            PlayEffect(self, data->effects[i], 0, 0);
        }
    }
    return 0;
}

S3RE_HOOK(0x004c7730, CDrawer_PlayAmbientSounds, PlayAmbientFn, Original_CDrawer_PlayAmbientSounds,
          CDrawer_PlayAmbientSounds);

using ApplyLayoutFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx);

// Purpose: Sets up the screen layout (sizes of the map view and panels) for
// the display mode chosen in the configuration.
std::uint32_t __fastcall CDrawer_ApplyScreenLayout(CDrawer* self, void*) {
    self->unk0f0 = -1;
    using GetterFn = std::uint32_t(__fastcall*)(const void*, void*);
    const std::uint32_t mode = reinterpret_cast<GetterFn>(0x0049dec0)(&g_config.get(), nullptr);  // GetMemberDword70
    return reinterpret_cast<ModeFn>(0x004c6d70)(mode);  // Render::SetScreenLayout
}

S3RE_HOOK(0x004c6d40, CDrawer_ApplyScreenLayout, ApplyLayoutFn, Original_CDrawer_ApplyScreenLayout,
          CDrawer_ApplyScreenLayout);
S3RE_HOOK(0x004c6d70, Render_SetScreenLayout, ModeFn, Original_Render_SetScreenLayout, Render_SetScreenLayout);
S3RE_HOOK(0x004c7b50, Render_IsNearView, NearViewFn, Original_Render_IsNearView, Render_IsNearView);
S3RE_HOOK(0x004c79f0, CDrawer_AmbientSoundPan, PanFn, Original_CDrawer_AmbientSoundPan, CDrawer_AmbientSoundPan);
S3RE_HOOK(0x004c7a50, CDrawer_AmbientSoundVolume, VolumeFn, Original_CDrawer_AmbientSoundVolume,
          CDrawer_AmbientSoundVolume);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// The layout globals g_displayMode (0x730f08) .. g_drawOriginY (0x730f40)
// as one block, and g_layout7dfd58.
constexpr std::size_t kLayoutBytes = 0x3c;

class SavedLayout {
public:
    SavedLayout() : extra_(g_layout7dfd58) { std::memcpy(saved_, Block(), kLayoutBytes); }
    ~SavedLayout() {
        std::memcpy(Block(), saved_, kLayoutBytes);
        g_layout7dfd58 = extra_;
    }
    static void* Block() { return &g_displayMode.get(); }

private:
    std::uint8_t saved_[kLayoutBytes];
    std::int32_t extra_;
};

void CompareScreenLayout(test::Context& ctx, ModeFn candidate, int cases) {
    const SavedLayout saved;
    std::uint8_t start[kLayoutBytes], expected[kLayoutBytes];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(start, sizeof(start));
        const std::int32_t extra = static_cast<std::int32_t>(ctx.rng().Next32());
        const std::uint32_t mode = ctx.rng().Chance(1, 5) ? ctx.rng().Next32() : ctx.rng().Below(4);
        std::memcpy(SavedLayout::Block(), start, kLayoutBytes);
        g_layout7dfd58 = extra;
        const std::uint32_t expectedResult = Original_Render_SetScreenLayout(mode);
        std::memcpy(expected, SavedLayout::Block(), kLayoutBytes);
        const std::int32_t expectedExtra = g_layout7dfd58;
        std::memcpy(SavedLayout::Block(), start, kLayoutBytes);
        g_layout7dfd58 = extra;
        const std::uint32_t actualResult = candidate(mode);
        bool same = S3RE_EXPECT_EQ(expectedResult, actualResult);
        same &= S3RE_EXPECT_MEM_EQ(expected, SavedLayout::Block(), kLayoutBytes);
        same &= S3RE_EXPECT_EQ(expectedExtra, g_layout7dfd58.get());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mode %u", i, mode);
            return;
        }
    }
}

// Wrong: 800x600 gets the cell size of 640x480.
std::uint32_t __stdcall WrongScreenLayout(std::uint32_t mode) {
    const std::uint32_t r = Render_SetScreenLayout(mode);
    if (mode == 1) {
        g_uiCellSize = 3;
    }
    return r;
}

std::int32_t Edge(test::Context& ctx, std::int32_t around) {
    return ctx.rng().Chance(1, 2) ? around + ctx.rng().Range(-2, 2) : static_cast<std::int32_t>(ctx.rng().Next32());
}

}  // namespace

S3RE_TEST(Render_SetScreenLayout_MatchesOriginal) {
    CompareScreenLayout(ctx, &Render_SetScreenLayout, 500);
}
S3RE_TEST(Render_SetScreenLayout_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScreenLayout(probe, &WrongScreenLayout, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// ApplyScreenLayout on random drawers and configured modes; compared: EAX,
// the drawer, the layout globals.
void CompareApplyLayout(test::Context& ctx, ApplyLayoutFn candidate, int cases) {
    const SavedLayout saved;
    const std::uint32_t savedMode = g_config->screen_layout;
    test::WorldPair drawers(ctx, sizeof(CDrawer));
    std::uint8_t start[kLayoutBytes], expected[kLayoutBytes];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(start, sizeof(start));
        const std::uint32_t mode = ctx.rng().Chance(1, 5) ? ctx.rng().Next32() : ctx.rng().Below(4);
        g_config->screen_layout = mode;
        drawers.Both([&](CGameData* w) { DrawerOf(w)->unk0f0 = static_cast<std::int32_t>(i); });
        std::memcpy(SavedLayout::Block(), start, kLayoutBytes);
        const std::uint32_t a = Original_CDrawer_ApplyScreenLayout(DrawerOf(drawers.original()), nullptr);
        std::memcpy(expected, SavedLayout::Block(), kLayoutBytes);
        std::memcpy(SavedLayout::Block(), start, kLayoutBytes);
        const std::uint32_t b = candidate(DrawerOf(drawers.ours()), nullptr);
        bool same = S3RE_EXPECT_EQ(a, b);
        same &= S3RE_EXPECT_MEM_EQ(expected, SavedLayout::Block(), kLayoutBytes);
        same &= drawers.Same(ctx, 0, sizeof(CDrawer));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mode %u", i, mode);
            break;
        }
    }
    g_config->screen_layout = savedMode;
}

// Wrong: +0xf0 is left alone.
std::uint32_t __fastcall WrongApplyLayout(CDrawer* self, void*) {
    return Render_SetScreenLayout(g_config->screen_layout);
}

}  // namespace

namespace {

// PlayAmbientSounds on random drawers (source counts around the minimums)
// and draw data (ticks -1 or not, up to 10 queued effects); IsEnabled,
// GetEffect and Play are recording stubs (IsEnabled scripted, GetEffect
// returns a fake effect), Pan and Volume run (verified), the CRT's rand
// state is the same for both. Compared: the calls (with the computed pan
// and volume) and the rand state afterwards.
std::uint8_t g_fakeEffect[16];

void ComparePlayAmbient(test::Context& ctx, PlayAmbientFn candidate, int cases) {
    using GetPtdFn = std::uint8_t*(__cdecl*)();
    std::uint8_t* const ptd = reinterpret_cast<GetPtdFn>(0x0062293a)();  // _getptd: rand() state at +0x14
    std::uint32_t& randState = *reinterpret_cast<std::uint32_t*>(ptd + 0x14);  // raw-ok: CRT per-thread data
    const std::uint32_t savedRand = randState;
    test::WorldPair drawers(ctx, sizeof(CDrawer));
    std::vector<std::uint8_t> data(sizeof(DrawDataSounds));
    static const std::int32_t kCounts[] = {0, 199, 200, 201, 400, 401, 800, 801, 2000, 2001, 8000, 8001, 30000};
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(data.data(), data.size());
        auto* d = reinterpret_cast<DrawDataSounds*>(data.data());
        d->ticks = ctx.rng().Chance(1, 4) ? -1 : ctx.rng().Range(0, 10);
        d->effect_count = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(11);
        drawers.Both([&](CGameData* w) { DrawerOf(w)->sound = reinterpret_cast<void*>(0x1234); });
        for (std::size_t k = 0; k < kAmbientKinds; ++k) {
            const std::int32_t count = kCounts[ctx.rng().Below(13)];
            const std::int32_t side = static_cast<std::int32_t>(ctx.rng().Below(40)) + 1;
            drawers.Both([&](CGameData* w) {
                DrawerOf(w)->ambient_count[k] = count;
                DrawerOf(w)->ambient_left[k] = side;
                DrawerOf(w)->ambient_right[k] = side + 3;
            });
        }
        const std::uint32_t seed = ctx.rng().Next32();
        const std::uint64_t script = ctx.rng().Next64();
        std::vector<test::StubCall> calls[2];
        std::uint32_t randAfter[2];
        for (int run = 0; run < 2; ++run) {
            test::Rng enabled(script);
            test::CalleeStubs stubs;
            stubs.SetWorld(nullptr);
            stubs.Install<0, kSoundIsEnabled>(
                [&enabled](CGameData*, std::uint32_t, const std::uint32_t*) { return enabled.Chance(4, 5) ? 1u : 0u; });
            stubs.Install<5, kSoundGetEffect>([](CGameData*, std::uint32_t, const std::uint32_t*) {
                return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_fakeEffect));
            });
            stubs.Install<0, kEffectPlay>();
            randState = seed;
            CDrawer* drawer = DrawerOf(run == 0 ? drawers.original() : drawers.ours());
            (run == 0 ? Original_CDrawer_PlayAmbientSounds : candidate)(drawer, nullptr, d);
            calls[run] = stubs.Take();
            randAfter[run] = randState;
        }
        bool same = S3RE_EXPECT(calls[0] == calls[1]);
        same &= S3RE_EXPECT_EQ(randAfter[0], randAfter[1]);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    randState = savedRand;
}

// Wrong: the birds (kind 4) already play at exactly 400 sources.
std::uint32_t __fastcall WrongPlayAmbient(CDrawer* self, void* edx, const DrawDataSounds* data) {
    const std::int32_t count = self->ambient_count[4];
    if (count == 400) {
        self->ambient_count[4] = 401;
    }
    const std::uint32_t r = CDrawer_PlayAmbientSounds(self, edx, data);
    self->ambient_count[4] = count;
    return r;
}

}  // namespace

S3RE_TEST(CDrawer_PlayAmbientSounds_MatchesOriginal) {
    ComparePlayAmbient(ctx, &CDrawer_PlayAmbientSounds, 3000);
}
S3RE_TEST(CDrawer_PlayAmbientSounds_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePlayAmbient(probe, &WrongPlayAmbient, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CDrawer_ApplyScreenLayout_MatchesOriginal) {
    CompareApplyLayout(ctx, &CDrawer_ApplyScreenLayout, 300);
}
S3RE_TEST(CDrawer_ApplyScreenLayout_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareApplyLayout(probe, &WrongApplyLayout, 50);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(Render_IsNearView_MatchesOriginal) {
    const std::uint32_t savedX = g_drawOriginX;
    const std::int32_t savedW = g_displayWidth, savedH = g_displayHeight;
    for (int i = 0; i < 20000; ++i) {
        g_drawOriginX = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(200);
        g_displayWidth = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32()) : 640 + static_cast<std::int32_t>(ctx.rng().Below(400));
        g_displayHeight = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32()) : 480 + static_cast<std::int32_t>(ctx.rng().Below(300));
        const std::int32_t x = ctx.rng().Chance(1, 2) ? Edge(ctx, static_cast<std::int32_t>(g_drawOriginX.get()) - 30)
                                                      : Edge(ctx, g_displayWidth + 30);
        const std::int32_t y = ctx.rng().Chance(1, 2) ? Edge(ctx, -30) : Edge(ctx, g_displayHeight + 30);
        if (!S3RE_EXPECT_EQ(Original_Render_IsNearView(x, y), Render_IsNearView(x, y))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%d, %d)", i, x, y);
            break;
        }
    }
    g_drawOriginX = savedX;
    g_displayWidth = savedW;
    g_displayHeight = savedH;
}

namespace {

// Small source counts (left + right never 0, no overflowing quotient) and
// random volume ranges (high > low).
void CompareAmbient(test::Context& ctx, PanFn pan, VolumeFn volume, int cases) {
    test::WorldPair drawers(ctx, sizeof(CDrawer));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t kind = ctx.rng().Below(kAmbientKinds);
        const std::int32_t left = static_cast<std::int32_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Below(100000) : ctx.rng().Below(40));
        const std::int32_t right = static_cast<std::int32_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Below(100000) : ctx.rng().Below(40)) +
                                   (left == 0 ? 1 : 0);
        const std::int32_t count = static_cast<std::int32_t>(ctx.rng().Below(3000)) - 100;
        drawers.Both([&](CGameData* w) {
            DrawerOf(w)->ambient_left[kind] = left;
            DrawerOf(w)->ambient_right[kind] = right;
            DrawerOf(w)->ambient_count[kind] = count;
        });
        const std::int32_t low = static_cast<std::int32_t>(ctx.rng().Below(1000));
        const std::int32_t high = low + 1 + static_cast<std::int32_t>(ctx.rng().Below(3000));
        const std::int32_t offset = ctx.rng().Range(-12000, 3000);
        bool same = S3RE_EXPECT_EQ(Original_CDrawer_AmbientSoundPan(DrawerOf(drawers.original()), nullptr, kind),
                                   pan(DrawerOf(drawers.ours()), nullptr, kind));
        same &= S3RE_EXPECT_EQ(Original_CDrawer_AmbientSoundVolume(DrawerOf(drawers.original()), nullptr, kind, low,
                                                                   high, offset),
                               volume(DrawerOf(drawers.ours()), nullptr, kind, low, high, offset));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: kind %u, %d/%d, count %d (%d..%d, %d)", i, kind, left, right,
                     count, low, high, offset);
            return;
        }
    }
    drawers.Checkpoint(ctx);
}

// Wrong: the volume is clamped at -10000 only.
std::int32_t __fastcall WrongAmbientSoundVolume(CDrawer* self, void*, std::uint32_t kind, std::int32_t low,
                                                std::int32_t high, std::int32_t offset) {
    std::int32_t count = DrawerOf(self)->ambient_count[kind];
    if (count > high) {
        count = high;
    }
    const std::int32_t v = (count - low) * kVolumeSpan / (high - low) + offset - kVolumeSpan;
    return v < kSilence ? kSilence : v;
}

}  // namespace

S3RE_TEST(CDrawer_AmbientSound_MatchesOriginal) {
    CompareAmbient(ctx, &CDrawer_AmbientSoundPan, &CDrawer_AmbientSoundVolume, 20000);
}
S3RE_TEST(CDrawer_AmbientSound_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAmbient(probe, &CDrawer_AmbientSoundPan, &WrongAmbientSoundVolume, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
