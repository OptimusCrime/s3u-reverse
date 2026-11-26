// Changing the display mode of the in-game drawer (Dirk\Draw.cpp and
// Volker\DrawBuilding.cpp's object file).
//
// CDrawer::SetDisplayMode (0x0042ce90): (width, height, bpp, refresh,
//   flags) thiscall, ret 0x14; `flags` is not used. The mode index is 2 for
//   a width of 1024, 1 for 800, else 0. If the drawer has surfaces (+0x7c,
//   read once at the start) it releases them first (virtual slot 5 (1)).
//   It reads the current mode (CDirectDraw::DDGetDisplayMode 0x5bed70 into
//   a local DDSURFACEDESC) and logs "GFX: Old display mode: %ux%ux%u"
//   (width, height, bit count). When width, height and bit count are the
//   same and the refresh rate too (or `refresh` is 0) it logs "GFX:
//   Skipping SetDisplayMode"; otherwise it sends the game window message
//   0x804 (wParam the index; SendMessageA through S3.EXE's import), whose
//   handler switches the mode. Then, if it had surfaces, it creates them
//   again (slot 11 (1)) and asks the draw thread (g_drawThread, if there is
//   one) for a full redraw. EAX (a caller tail-calls it from a virtual
//   slot, so kept exact): with surfaces, RequestFullRedraw's (or slot 11's
//   without a draw thread); without, `refresh` when skipped, else
//   SendMessageA's.
// CDrawer::ChangeDisplayMode (0x004c7490, was Render_sub_4c7490): (options)
//   thiscall, ret 4, AL = bool; called by 0x4b26c0 (applying the in-game
//   options) with an options object whose +0xa0 is the display mode
//   (1..3; 250 also passes). Any other value returns 0 unless
//   g_fastForwarding is set (then nothing changes and it returns 1). For
//   the mode (+0xa0 re-read for modes 2 and 3) that differs from
//   g_displayMode: the layout globals as Render::SetScreenLayout sets them,
//   except that 640x480 gets g_layout730f2c = 4 (SetScreenLayout: 5), then
//   SetDisplayMode(width, height, 16, 0, 0) and g_logicStartState = 250
//   (CLogicThread::Step then redoes its first-step setup). Returns 1. The
//   upper bytes of EAX are the caller's / a callee's in the original (the
//   caller tests AL); ours are 0.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/graphics/drawer_layout.h"
#include "game/texts.h"
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

constexpr std::uintptr_t kDDGetDisplayMode = 0x005bed70;    // DirectX::CDirectDraw::DDGetDisplayMode(desc)
constexpr std::uintptr_t kRequestFullRedraw = 0x005b6730;   // DirectX::CGameDrawThread::RequestFullRedraw
constexpr std::uintptr_t kSetDisplayMode = 0x0042ce90;

constexpr std::uint32_t kSlotReleaseSurfaces = 5;
constexpr std::uint32_t kSlotCreateSurfaces = 11;
constexpr std::uint32_t kMessageSetDisplayMode = 0x804;  // to the game window: wParam = mode index
constexpr std::uint32_t kBitsPerPixel = 16;
constexpr std::int32_t kAnyMode = 250;          // options +0xa0: passes the check
constexpr std::uint32_t kRedoFirstStep = 250;   // g_logicStartState

#pragma pack(push, 1)
// The fields of a DDSURFACEDESC (0x6c bytes) that SetDisplayMode reads.
struct SurfaceDesc {
    std::uint8_t unk00[0x8];
    std::uint32_t height;      // +0x08 dwHeight
    std::uint32_t width;       // +0x0c dwWidth
    std::uint8_t unk10[0x18 - 0x10];
    std::uint32_t refresh;     // +0x18 dwRefreshRate
    std::uint8_t unk1c[0x54 - 0x1c];
    std::uint32_t bit_count;   // +0x54 ddpfPixelFormat.dwRGBBitCount
    std::uint8_t unk58[0x6c - 0x58];
};
// The in-game options object ChangeDisplayMode reads.
struct DisplayOptions {
    std::uint8_t unk00[0xa0];
    std::int32_t display_mode;  // +0xa0 1..3 (g_displayMode values)
};
#pragma pack(pop)
static_assert(sizeof(SurfaceDesc) == 0x6c);
static_assert(offsetof(SurfaceDesc, bit_count) == 0x54);
static_assert(offsetof(DisplayOptions, display_mode) == 0xa0);

// The layout globals per display mode as ChangeDisplayMode writes them.
struct ModeLayout {
    std::int32_t mode, width, height, viewWidth, viewHeight, layout20, sidebarWidth;
    std::int32_t minimapWidth, layout2c, layout30, layout34, cellSize, originX, originY;
};

constexpr ModeLayout kModeLayouts[] = {
    {1, 640, 480, 504, 480, 437, 136, 128, 4, 10, 142, 3, 8, 152},  // layout2c 4 (SetScreenLayout: 5)
    {2, 800, 600, 624, 600, 540, 176, 160, 8, 5, 170, 4, 8, 171},
    {3, 1024, 768, 808, 768, 697, 216, 208, 6, 8, 224, 5, 8, 228},
};

std::uint32_t CallSlot(CDrawer* self, std::uint32_t slot, std::uint32_t argument) {
    using Fn = std::uint32_t(__fastcall*)(CDrawer*, void*, std::uint32_t);
    return reinterpret_cast<Fn>(self->vtable[slot])(self, nullptr, argument);
}

std::uint32_t ModeIndex(std::uint32_t width) {
    if (width == 1024) {
        return 2;
    }
    return width == 800 ? 1 : 0;
}

void ApplyModeLayout(const ModeLayout& layout) {
    g_displayMode = layout.mode;
    g_displayWidth = layout.width;
    g_displayHeight = layout.height;
    g_displayBitsPerPixel = static_cast<std::int32_t>(kBitsPerPixel);
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
}

}  // namespace

using SetDisplayModeFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t width,
                                                    std::uint32_t height, std::uint32_t bpp, std::uint32_t refresh,
                                                    std::uint32_t flags);
using ChangeDisplayModeFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, const DisplayOptions* options);

// Purpose: Switches the screen to another resolution (unless it is already
// set), recreating the drawing surfaces around the switch.
std::uint32_t __fastcall CDrawer_SetDisplayMode(CDrawer* self, void*, std::uint32_t width, std::uint32_t height,
                                                std::uint32_t bpp, std::uint32_t refresh, std::uint32_t) {
    const std::uint32_t index = ModeIndex(width);
    const bool hadSurfaces = self->has_surfaces != 0;
    if (hadSurfaces) {
        CallSlot(self, kSlotReleaseSurfaces, 1);
    }
    SurfaceDesc current;
    using GetModeFn = std::uint32_t(__fastcall*)(CDrawer*, void*, SurfaceDesc*);
    reinterpret_cast<GetModeFn>(kDDGetDisplayMode)(self, nullptr, &current);
    GameDebugLog(texts::kOldDisplayMode, current.width, current.height, current.bit_count);
    std::uint32_t result;
    if (current.width == width && current.height == height && current.bit_count == bpp &&
        (refresh == 0 || current.refresh == refresh)) {
        GameDebugLog(texts::kSkippingSetDisplayMode);
        result = refresh;
    } else {
        result = static_cast<std::uint32_t>(
            g_importSendMessageA.get()(self->window, kMessageSetDisplayMode, index, 0));
    }
    if (hadSurfaces) {
        result = CallSlot(self, kSlotCreateSurfaces, 1);
        if (g_drawThread.get() != nullptr) {
            using RedrawFn = std::uint32_t(__fastcall*)(void*, void*);
            result = reinterpret_cast<RedrawFn>(kRequestFullRedraw)(g_drawThread.get(), nullptr);
        }
    }
    return result;
}

// Purpose: Applies the display resolution chosen in the in-game options:
// sets the screen layout for it and switches the display mode.
std::uint32_t __fastcall CDrawer_ChangeDisplayMode(CDrawer* self, void*, const DisplayOptions* options) {
    const std::int32_t chosen = options->display_mode;
    const bool known = (chosen >= 1 && chosen <= 3) || chosen == kAnyMode;  // (1..3 compared unsigned)
    if (!known && g_fastForwarding == 0) {
        return 0;
    }
    for (const ModeLayout& layout : kModeLayouts) {
        if (options->display_mode == layout.mode && g_displayMode != layout.mode) {
            ApplyModeLayout(layout);
            reinterpret_cast<SetDisplayModeFn>(kSetDisplayMode)(self, nullptr, static_cast<std::uint32_t>(layout.width),
                                                                static_cast<std::uint32_t>(layout.height),
                                                                kBitsPerPixel, 0, 0);
            g_logicStartState = kRedoFirstStep;
        }
    }
    return 1;
}

S3RE_HOOK(0x0042ce90, CDrawer_SetDisplayMode, SetDisplayModeFn, Original_CDrawer_SetDisplayMode,
          CDrawer_SetDisplayMode);
S3RE_HOOK(0x004c7490, CDrawer_ChangeDisplayMode, ChangeDisplayModeFn, Original_CDrawer_ChangeDisplayMode,
          CDrawer_ChangeDisplayMode);

// ---------------------------------------------------------------------------
// Tests: a drawer image with a recording vtable (slots 5 and 11 log their
// argument and return a scripted value); DDGetDisplayMode and
// RequestFullRedraw are recording stubs (the former writes a scripted mode,
// mostly one of the three the game uses), SendMessageA's import a
// recording fake; DebugLog captured; g_drawThread null or not. For
// ChangeDisplayMode also the layout globals, g_logicStartState and
// g_fastForwarding (SetDisplayMode runs as installed, ours when hooked).
// ---------------------------------------------------------------------------

namespace {

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
};
Script* g_script = nullptr;

std::uint32_t __fastcall FakeSlot(CDrawer*, void*, std::uint32_t argument) {
    g_script->log.push_back(0x510u);
    g_script->log.push_back(argument);
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeSlot11(CDrawer*, void*, std::uint32_t argument) {
    g_script->log.push_back(0x511u);
    g_script->log.push_back(argument);
    return g_script->rng.Next32();
}
std::int32_t __stdcall FakeSendMessage(void* window, std::uint32_t message, std::uint32_t wParam,
                                       std::uint32_t lParam) {
    g_script->log.insert(g_script->log.end(),
                         {0x5e4du, reinterpret_cast<std::uint32_t>(window), message, wParam, lParam});
    return static_cast<std::int32_t>(g_script->rng.Next32());
}

constexpr std::uint32_t kModes[][3] = {{640, 480, 16}, {800, 600, 16}, {1024, 768, 16}};

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint8_t> drawer;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
    std::vector<std::uint8_t> layout;
    std::uint32_t startState;
};

// The layout globals g_displayMode (0x730f08) .. g_drawOriginY (0x730f40)
// as one block, and g_layout7dfd58.
constexpr std::size_t kLayoutBytes = 0x3c;

struct Case {
    std::vector<std::uint8_t> drawer;
    std::uint64_t seed;
    std::uint32_t args[5];
    bool drawThread;
    DisplayOptions options;
    std::vector<std::uint8_t> layout;
    std::int32_t extra;
    std::uint32_t startState;
    std::int32_t fastForwarding;
};

Outcome Run(const Case& c, bool change, SetDisplayModeFn set, ChangeDisplayModeFn changeMode) {
    Outcome out;
    Script script;
    script.rng = test::Rng(c.seed);
    g_script = &script;
    out.drawer = c.drawer;
    CDrawer* drawer = reinterpret_cast<CDrawer*>(out.drawer.data());
    void* vtable[16] = {};
    for (void*& slot : vtable) {
        slot = reinterpret_cast<void*>(&FakeSlot);
    }
    vtable[kSlotCreateSurfaces] = reinterpret_cast<void*>(&FakeSlot11);
    drawer->vtable = vtable;
    static std::uint8_t drawThread[0x60];
    g_drawThread = c.drawThread ? static_cast<void*>(drawThread) : nullptr;
    std::memcpy(&g_displayMode.get(), c.layout.data(), kLayoutBytes);
    g_layout7dfd58 = c.extra;
    g_logicStartState = c.startState;
    g_fastForwarding = c.fastForwarding;
    const void* fake = reinterpret_cast<const void*>(&FakeSendMessage);
    test::PatchBytes(g_importSendMessageA.address(), &fake, sizeof(fake));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
        stubs.Install<1, kDDGetDisplayMode>([&script](CGameData*, std::uint32_t, const std::uint32_t* args) {
            auto* desc = reinterpret_cast<SurfaceDesc*>(static_cast<std::uintptr_t>(args[0]));
            std::memset(desc, 0, sizeof(SurfaceDesc));
            const std::uint32_t* mode = kModes[script.rng.Below(3)];
            desc->width = script.rng.Chance(1, 8) ? script.rng.Below(2000) : mode[0];
            desc->height = script.rng.Chance(1, 8) ? script.rng.Below(2000) : mode[1];
            desc->bit_count = script.rng.Chance(1, 8) ? script.rng.Below(33) : mode[2];
            desc->refresh = script.rng.Chance(1, 2) ? 0 : 60 + script.rng.Below(3) * 10;
            return 0u;
        });
        stubs.Install<0, kRequestFullRedraw>(
            [&script](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        DebugLogRecorder logs;
        const auto* options = &c.options;
        out.eax = change ? changeMode(drawer, nullptr, options)
                         : set(drawer, nullptr, c.args[0], c.args[1], c.args[2], c.args[3], c.args[4]);
        out.calls = stubs.Take();
        out.logs = logs.Take();
    }
    test::UnpatchBytes(g_importSendMessageA.address());
    for (test::StubCall& call : out.calls) {
        if (call.callee == kDDGetDisplayMode) {
            call.args[0] = 0;  // (a local of each frame)
        }
        if (call.callee == kRequestFullRedraw) {
            call.self = 1;  // (the static fake draw thread)
        }
    }
    drawer->vtable = nullptr;
    out.log = script.log;
    out.layout.resize(kLayoutBytes + 4);
    std::memcpy(out.layout.data(), &g_displayMode.get(), kLayoutBytes);
    const std::int32_t extra = g_layout7dfd58;
    std::memcpy(out.layout.data() + kLayoutBytes, &extra, 4);
    out.startState = g_logicStartState;
    g_script = nullptr;
    return out;
}

std::uint32_t PickDimension(test::Rng& rng, std::size_t which) {
    if (rng.Chance(1, 6)) {
        return rng.Below(2000);
    }
    return kModes[rng.Below(3)][which];
}

void CompareDisplayMode(test::Context& ctx, bool change, SetDisplayModeFn set, ChangeDisplayModeFn changeMode,
                        int cases) {
    std::uint8_t savedLayout[kLayoutBytes];
    std::memcpy(savedLayout, &g_displayMode.get(), kLayoutBytes);
    const std::int32_t savedExtra = g_layout7dfd58;
    const std::uint32_t savedStart = g_logicStartState;
    const std::int32_t savedFast = g_fastForwarding;
    void* const savedThread = g_drawThread;
    for (int i = 0; i < cases; ++i) {
        Case c;
        c.drawer.resize(sizeof(CDrawer));
        ctx.rng().Fill(c.drawer.data(), c.drawer.size());
        reinterpret_cast<CDrawer*>(c.drawer.data())->has_surfaces =
            static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : (ctx.rng().Chance(1, 4) ? ctx.rng().Below(256) : 1));
        c.seed = ctx.rng().Next64();
        c.args[0] = PickDimension(ctx.rng(), 0);
        c.args[1] = PickDimension(ctx.rng(), 1);
        c.args[2] = ctx.rng().Chance(1, 6) ? ctx.rng().Below(33) : 16;
        c.args[3] = ctx.rng().Chance(1, 2) ? 0 : 60 + ctx.rng().Below(3) * 10;
        c.args[4] = ctx.rng().Next32();
        c.drawThread = ctx.rng().Chance(2, 3);
        ctx.rng().Fill(&c.options, sizeof(c.options));
        c.options.display_mode = ctx.rng().Chance(1, 6) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                        : (ctx.rng().Chance(1, 8) ? kAnyMode : ctx.rng().Range(0, 4));
        c.layout.resize(kLayoutBytes);
        ctx.rng().Fill(c.layout.data(), kLayoutBytes);
        std::int32_t current = ctx.rng().Range(0, 4);
        std::memcpy(c.layout.data(), &current, 4);  // g_displayMode
        c.extra = static_cast<std::int32_t>(ctx.rng().Next32());
        c.startState = ctx.rng().Next32();
        c.fastForwarding = ctx.rng().Chance(1, 3) ? 1 : 0;
        const Outcome expected = Run(c, change, Original_CDrawer_SetDisplayMode, Original_CDrawer_ChangeDisplayMode);
        const Outcome actual = Run(c, change, set, changeMode);
        const std::uint32_t mask = change ? 0xffu : 0xffffffffu;
        bool same = S3RE_EXPECT_EQ(expected.eax & mask, actual.eax & mask);
        same &= S3RE_EXPECT_MEM_EQ(expected.drawer.data(), actual.drawer.data(), sizeof(CDrawer));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        same &= S3RE_EXPECT_MEM_EQ(expected.layout.data(), actual.layout.data(), kLayoutBytes + 4);
        same &= S3RE_EXPECT_EQ(expected.startState, actual.startState);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%s)", i, change ? "ChangeDisplayMode" : "SetDisplayMode");
            break;
        }
    }
    std::memcpy(&g_displayMode.get(), savedLayout, kLayoutBytes);
    g_layout7dfd58 = savedExtra;
    g_logicStartState = savedStart;
    g_fastForwarding = savedFast;
    g_drawThread = savedThread;
}

// Wrong: the refresh rate is always compared.
std::uint32_t __fastcall WrongSetDisplayMode(CDrawer* self, void* edx, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t bpp, std::uint32_t refresh, std::uint32_t flags) {
    return CDrawer_SetDisplayMode(self, edx, width, height, bpp, refresh == 0 ? 1 : refresh, flags);
}

// Wrong: 640x480 gets SetScreenLayout's layout2c (5).
std::uint32_t __fastcall WrongChangeDisplayMode(CDrawer* self, void* edx, const DisplayOptions* options) {
    const std::int32_t before = g_displayMode;
    const std::uint32_t result = CDrawer_ChangeDisplayMode(self, edx, options);
    if (before != 1 && g_displayMode == 1) {
        g_layout730f2c = 5;
    }
    return result;
}

}  // namespace

S3RE_TEST(CDrawer_SetDisplayMode_MatchesOriginal) {
    CompareDisplayMode(ctx, false, &CDrawer_SetDisplayMode, nullptr, 1000);
}
S3RE_TEST(CDrawer_SetDisplayMode_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDisplayMode(probe, false, &WrongSetDisplayMode, nullptr, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawer_ChangeDisplayMode_MatchesOriginal) {
    CompareDisplayMode(ctx, true, nullptr, &CDrawer_ChangeDisplayMode, 1000);
}
S3RE_TEST(CDrawer_ChangeDisplayMode_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDisplayMode(probe, true, nullptr, &WrongChangeDisplayMode, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
