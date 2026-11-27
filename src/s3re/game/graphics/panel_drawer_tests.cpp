// In-process differential tests of CDrawer::DrawPanel (0x004b4d00,
// panel_drawer*.cpp).
//
// The original (through its trampoline) and ours draw the same random panel
// state with every drawing callee stubbed: the GUI image and animation
// getters (returning fake images and a fake image list), DrawGuiImage,
// DrawSprite, DrawColorIndexed, the drawer's text helpers, the bars,
// PutPixel16, PackColour, Render::Entry204, CTextSurface::DrawText and
// DrawTextF. Each stub records its arguments (pointers to the callers'
// stack temporaries by their contents); Point16, ListImage(Checked),
// BuildingAnimationSlot and the std::string members run. Compared: the
// call logs, the draw data's first 0x500 bytes, the drawer's flags +0xf8 /
// +0xfc and g_panelInputGoods / Columns.

#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/graphics/panel_drawer.h"
#include "game/tables.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace s3re::game {

using DrawPanelFn = void(__fastcall*)(CDrawer* self, void* edx, DrawDataView* data);
void __fastcall CDrawer_DrawPanel(CDrawer* self, void* edx, DrawDataView* data);
extern DrawPanelFn Original_CDrawer_DrawPanel;

namespace {

using panel::Rect;

constexpr std::uintptr_t kGuiImagePointerAt = 0x005faca0;
constexpr std::uintptr_t kDrawGuiImage = 0x005f64c0;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kDrawSprite = 0x005fadb0;
constexpr std::uintptr_t kColourAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kPalettePointer = 0x005fac10;
constexpr std::uintptr_t kDrawColorIndexed = 0x005fc5f0;
constexpr std::uintptr_t kClearTextRect = 0x004d65e0;
constexpr std::uintptr_t kBlitTextRect = 0x004d6740;
constexpr std::uintptr_t kPanelText = 0x004d6a30;
constexpr std::uintptr_t kPanelTextFont1 = 0x004d6b10;
constexpr std::uintptr_t kPanelString = 0x004d6bf0;
constexpr std::uintptr_t kPanelTextArg = 0x004d6dd0;
constexpr std::uintptr_t kPanelTextIndentedArg = 0x004d6eb0;
constexpr std::uintptr_t kPanelTextLeftHalf = 0x004d6f90;
constexpr std::uintptr_t kPanelTextRightHalf = 0x004d7070;
constexpr std::uintptr_t kPanelNumber = 0x004d7150;
constexpr std::uintptr_t kPanelHundredths = 0x004d7230;
constexpr std::uintptr_t kPanelPercent = 0x004d7390;
constexpr std::uintptr_t kPanelTextAt = 0x004d7470;
constexpr std::uintptr_t kPanelTextAtFont1 = 0x004d7550;
constexpr std::uintptr_t kPanelCaption = 0x004d7630;
constexpr std::uintptr_t kPanelTextAtArg = 0x004d78d0;
constexpr std::uintptr_t kDrawValueBar = 0x004d79c0;
constexpr std::uintptr_t kDrawStatisticsBar = 0x004d7bd0;
constexpr std::uintptr_t kPutPixel = 0x004c7ac0;
constexpr std::uintptr_t kPackColour = 0x004c7b00;
constexpr std::uintptr_t kRaceOfPlayer = 0x004c7ab0;
constexpr std::uintptr_t kDrawText = 0x004a8090;
constexpr std::uintptr_t kDrawTextF = 0x004a87d0;

constexpr std::uint32_t kPanelBytes = 0x500;
constexpr std::uint32_t kListImages = 64;

std::uint32_t U32(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

std::uint32_t Hash(const char* text, std::uint32_t length) {
    if (text == nullptr) {
        return 0xffffffffu;
    }
    std::uint32_t hash = 2166136261u;
    for (std::uint32_t i = 0; i < length; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(text[i])) * 16777619u;
    }
    return hash;
}

std::uint32_t HashC(const char* text) {
    return text == nullptr ? 0xfffffffeu : Hash(text, static_cast<std::uint32_t>(std::strlen(text)));
}

// The fake image list AnimationPointer hands out: count byte at +7, the
// image offsets from +8.
struct FakeList {
    std::uint8_t header[8];
    std::uint32_t offsets[kListImages];
};

struct Environment {
    std::vector<std::uint8_t> drawer = std::vector<std::uint8_t>(sizeof(CDrawer));
    std::vector<std::uint8_t> data = std::vector<std::uint8_t>(sizeof(DrawDataView));
    FakeList list{};
    char captions[16][8]{};
};

Environment& Shared() {
    static Environment environment;
    return environment;
}

std::vector<std::uint32_t>* g_log = nullptr;

Rect* __cdecl StubDrawTextF(void* self, Rect* result, std::int32_t font, const Rect* rect, std::uint32_t flags,
                            const char* format, std::uint32_t value) {
    g_log->insert(g_log->end(), {0xdf, U32(self), static_cast<std::uint32_t>(font), static_cast<std::uint32_t>(rect->left),
                                 static_cast<std::uint32_t>(rect->top), static_cast<std::uint32_t>(rect->right),
                                 static_cast<std::uint32_t>(rect->bottom), flags, HashC(format), value});
    *result = {1, 2, 3, 4};
    return result;
}

struct Side {
    std::vector<std::uint32_t> log;
    std::vector<std::uint8_t> data;
    std::int32_t flags[2];
    std::int32_t inputs[8];
};

void Run(DrawPanelFn fn, Environment& env, const std::vector<std::uint8_t>& drawerStart,
         const std::vector<std::uint8_t>& dataStart, Side& side) {
    std::memcpy(env.drawer.data(), drawerStart.data(), drawerStart.size());
    std::memcpy(env.data.data(), dataStart.data(), dataStart.size());
    auto* const drawer = reinterpret_cast<CDrawer*>(env.drawer.data());
    auto* const data = reinterpret_cast<DrawDataView*>(env.data.data());
    for (int i = 0; i < 4; ++i) {
        g_panelInputGoods[i] = 0x5a5a5a5a;
        g_panelInputColumns[i] = 0x5a5a5a5a;
    }
    std::vector<std::uint32_t>& log = side.log;
    g_log = &log;
    const std::uint32_t drawerBase = U32(drawer);
    const std::uint32_t listBase = U32(&env.list);
    const auto rel = [drawerBase](std::uint32_t p) { return p - drawerBase; };
    const auto rect = [&log, drawerBase](std::uint32_t p) {
        const auto* r = reinterpret_cast<const Rect*>(static_cast<std::uintptr_t>(p));
        const bool inDrawer = p >= drawerBase && p < drawerBase + sizeof(CDrawer);
        log.insert(log.end(), {inDrawer ? p - drawerBase : 0xffffffffu, static_cast<std::uint32_t>(r->left),
                               static_cast<std::uint32_t>(r->top), static_cast<std::uint32_t>(r->right),
                               static_cast<std::uint32_t>(r->bottom)});
    };
    const auto at = [](std::uint32_t p) { return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(p)); };
    const auto put = [](std::uint32_t p, std::uint32_t v) { *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(p)) = v; };
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(0));
    stubs.Install<3, kGuiImagePointerAt>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x01, self, a[1], a[2]});
        put(a[0], a[1] * 7 + (a[2] << 24));
        return a[0];
    });
    stubs.Install<7, kDrawGuiImage>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x02, at(self), rel(a[0]), a[1], a[2], a[3], a[4], a[5], a[6]});
        return 0u;
    });
    stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x03, self, a[1]});
        put(a[0], listBase);
        return a[0];
    });
    stubs.Install<5, kDrawSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x04, at(self) - listBase, rel(a[0]), at(a[1]), a[3], a[4]});
        rect(a[2]);
        return 0u;
    });
    stubs.Install<2, kColourAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x05, self, a[1]});
        put(a[0], listBase);
        return a[0];
    });
    stubs.Install<2, kPalettePointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x06, self, a[0], a[1]});
        return a[0] * 3u + 1;
    });
    stubs.Install<4, kDrawColorIndexed>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x07, at(self) - listBase, rel(a[0]), at(a[1]), a[3]});
        rect(a[2]);
        return 0u;
    });
    const auto plain = [&log, rel](std::uint32_t tag, int count) {
        return [&log, tag, count, rel](CGameData*, std::uint32_t self, const std::uint32_t* a) {
            log.push_back(tag);
            log.push_back(rel(self));
            for (int i = 0; i < count; ++i) {
                log.push_back(a[i]);
            }
            return 0u;
        };
    };
    stubs.Install<4, kClearTextRect>(plain(0x10, 4));
    stubs.Install<4, kBlitTextRect>(plain(0x11, 4));
    stubs.Install<3, kPanelText>(plain(0x12, 3));
    stubs.Install<3, kPanelTextFont1>(plain(0x13, 3));
    stubs.Install<4, kPanelTextArg>(plain(0x14, 4));
    stubs.Install<4, kPanelTextIndentedArg>(plain(0x15, 4));
    stubs.Install<3, kPanelTextLeftHalf>(plain(0x16, 3));
    stubs.Install<3, kPanelTextRightHalf>(plain(0x17, 3));
    stubs.Install<3, kPanelNumber>(plain(0x18, 3));
    stubs.Install<3, kPanelPercent>(plain(0x19, 3));
    stubs.Install<4, kPanelTextAt>(plain(0x1a, 4));
    stubs.Install<4, kPanelTextAtFont1>(plain(0x1b, 4));
    stubs.Install<5, kPanelTextAtArg>(plain(0x1c, 5));
    stubs.Install<3, kDrawStatisticsBar>(plain(0x1d, 3));
    stubs.Install<6, kDrawValueBar>(plain(0x1e, 6));
    stubs.Install<3, kPutPixel>(plain(0x1f, 3));
    stubs.Install<4, kPanelHundredths>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x20, rel(self), a[0], a[1], a[2], a[3]});
        return (a[3] * 13u) % 9u;
    });
    stubs.Install<3, kPackColour>([&](CGameData*, std::uint32_t, const std::uint32_t* a) {
        log.insert(log.end(), {0x21, a[0], a[1], a[2]});
        return 0xf800u;
    });
    stubs.Install<1, kRaceOfPlayer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x22, rel(self), a[0]});
        return a[0] & 3u;
    });
    stubs.Install<2, kPanelCaption>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0x23, rel(self), a[0],
                               HashC(reinterpret_cast<const char*>(static_cast<std::uintptr_t>(a[1])))});
        return 0u;
    });
    stubs.Install<6, kPanelString>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        msvc6::String text{};
        std::memcpy(&text, &a[2], sizeof(text));
        log.insert(log.end(), {0x24, rel(self), a[0], a[1], text.len, Hash(text.ptr, text.len),
                               text.ptr != nullptr ? static_cast<std::uint32_t>(static_cast<std::uint8_t>(text.ptr[-1]))
                                                   : 0x100u});
        msvc6::Original_String_Destroy(&text, nullptr);  // the callee owns the by-value string
        return 0u;
    });
    stubs.Install<5, kDrawText>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        const auto* text = reinterpret_cast<const msvc6::String*>(static_cast<std::uintptr_t>(a[4]));
        log.insert(log.end(), {0x25, self, a[1], a[3], text->len, Hash(text->ptr, text->len)});
        rect(a[2]);
        return a[0];
    });
    test::ScopedJump drawTextF(kDrawTextF, reinterpret_cast<const void*>(&StubDrawTextF));
    fn(drawer, nullptr, data);
    side.data.assign(env.data.begin(), env.data.begin() + kPanelBytes);
    side.flags[0] = drawer->panel_redraw;
    side.flags[1] = drawer->panel_buttons_redraw;
    for (int i = 0; i < 4; ++i) {
        side.inputs[i] = g_panelInputGoods[i];
        side.inputs[4 + i] = g_panelInputColumns[i];
    }
    g_log = nullptr;
}

// A panel value: mostly small numbers, sometimes the special ones.
std::int32_t PanelValue(test::Rng& rng) {
    switch (rng.Below(10)) {
    case 0: return 0;
    case 1: return -1;
    case 2: return 0xff;
    case 3: return 0xfe;
    case 4: return static_cast<std::int32_t>(rng.Below(4));
    case 5: return static_cast<std::int32_t>(rng.Below(40));
    case 6: return static_cast<std::int32_t>(rng.Below(200));
    case 7: return 100;
    default: return static_cast<std::int32_t>(rng.Below(12));
    }
}

std::int32_t& At(DrawDataView* data, std::uint32_t offset) {
    return data->panel_values[(offset - 0x88) / 4];
}

void FillPanel(test::Rng& rng, Environment& env, DrawDataView* data) {
    std::memset(env.data.data(), 0, kPanelBytes);
    for (std::int32_t& value : data->panel_values) {
        value = PanelValue(rng);
    }
    data->redraw_panel = rng.Chance(3, 4) ? 1 : static_cast<std::int32_t>(rng.Below(3));
    data->unk060 = static_cast<std::int32_t>(rng.Below(14));
    data->unk064 = static_cast<std::int32_t>(rng.Below(6));
    data->unk068 = static_cast<std::int32_t>(rng.Below(14));
    data->unk06c = static_cast<std::int32_t>(rng.Below(6));
    data->unk074 = rng.Below(20);
    data->unk078 = static_cast<std::int32_t>(rng.Below(2));
    data->unk07c = static_cast<std::int32_t>(rng.Below(6));
    data->unk080 = static_cast<std::int32_t>(rng.Below(38));
    data->unk084 = static_cast<std::int32_t>(rng.Below(38));
    data->panel_buttons = rng.Below(16);
    data->panel_buttons_drawn = rng.Chance(1, 2) ? data->panel_buttons : rng.Below(16);
    data->unk090 = static_cast<std::int32_t>(rng.Next32());
    data->unk094 = static_cast<std::int32_t>(rng.Chance(1, 10) ? 4 : rng.Below(4));
    // The building menu's type (15, 16, 18 more often), owner and race.
    static constexpr std::int32_t kTypes[] = {15, 16, 18, 1, 12, 13, 19, 26, 31, 47, 7, 2};
    data->unk0a4 = rng.Chance(1, 2) ? kTypes[rng.Below(12)] : static_cast<std::int32_t>(rng.Below(57));
    At(data, 0xa8) = static_cast<std::int32_t>(rng.Below(5));
    At(data, 0x98) = rng.Chance(2, 3) ? At(data, 0xa8) : static_cast<std::int32_t>(rng.Below(5));
    At(data, 0xac) = rng.Chance(1, 4) ? 0xff : static_cast<std::int32_t>(rng.Below(4));
    At(data, 0xb0) = static_cast<std::int32_t>(rng.Chance(2, 3) ? 0 : rng.Below(3));
    At(data, 0x194) = static_cast<std::int32_t>(rng.Below(3));
    // Menu 11's goods (indexes into the slot table) and menu 3's chosen good.
    for (std::uint32_t i = 0; i < 7; ++i) {
        At(data, 0xfc + 4 * i) = static_cast<std::int32_t>(rng.Below(40));
    }
    data->unk2ec = rng.Below(38);
    for (std::uint16_t& h : data->unk2f0) {
        h = static_cast<std::uint16_t>(rng.Below(60));
    }
    data->unk4f0 = rng.Chance(1, 6) ? 0 : 1 + rng.Below(120);
    for (std::uint16_t& m : data->unk4f6) {
        m = static_cast<std::uint16_t>(rng.Below(200));
    }
    data->unk4ff = static_cast<std::uint8_t>(rng.Below(2));
    for (int k = 0; k < 8; ++k) {
        for (int j = 0; j < 2; ++j) {
            msvc6::String& s = data->panel_captions[k][j];
            s = {};
            if (rng.Chance(2, 3)) {
                char* buffer = env.captions[k * 2 + j];
                std::snprintf(buffer, 8, "c%u", rng.Below(1000));
                s.ptr = buffer;
                s.len = static_cast<std::uint32_t>(std::strlen(buffer));
            }
        }
    }
}

int CompareDrawPanel(test::Context& ctx, DrawPanelFn candidate, int cases, bool quiet) {
    Environment& env = Shared();
    const std::int32_t savedCell = g_uiCellSize;
    const std::uint32_t savedX = g_drawOriginX, savedY = g_drawOriginY;
    const std::int32_t savedMode = g_displayMode, savedWidth = g_displayWidth, savedHeight = g_displayHeight;
    const std::int32_t saved2c = g_layout730f2c, saved30 = g_layout730f30, saved34 = g_layout730f34;
    const std::int32_t savedMinimap[3] = {g_minimapMode, g_minimapShowBuildings, g_minimapShowSettlers};
    const std::int32_t savedMission = g_campaignMission;
    const std::uint8_t savedCosts = g_showBuildCosts;
    const std::uint32_t savedDivisor = g_strengthDivisor;
    std::int32_t savedInputs[8];
    for (int i = 0; i < 4; ++i) {
        savedInputs[i] = g_panelInputGoods[i];
        savedInputs[4 + i] = g_panelInputColumns[i];
    }
    int failures = 0;
    for (int n = 0; n < cases && failures == 0; ++n) {
        test::Rng& rng = ctx.rng();
        g_uiCellSize = rng.Range(3, 5);
        g_drawOriginX = rng.Below(64);
        g_drawOriginY = rng.Below(64);
        g_displayMode = rng.Range(1, 3);
        g_displayWidth = rng.Range(640, 1024);
        g_displayHeight = rng.Range(480, 768);
        g_layout730f2c = rng.Range(0, 20);
        g_layout730f30 = rng.Range(0, 20);
        g_layout730f34 = rng.Range(100, 230);
        g_minimapMode = rng.Range(0, 2);
        g_minimapShowBuildings = rng.Range(0, 1);
        g_minimapShowSettlers = rng.Range(0, 2);
        static constexpr std::int32_t kMissions[] = {0, 55, 67, 3};
        g_campaignMission = kMissions[rng.Below(4)];
        g_showBuildCosts = static_cast<std::uint8_t>(rng.Below(2));
        g_strengthDivisor = rng.Below(20);

        env.list = {};
        env.list.header[7] = static_cast<std::uint8_t>(rng.Below(10));
        for (std::uint32_t& offset : env.list.offsets) {
            offset = 16 * (1 + rng.Below(500));
        }
        std::vector<std::uint8_t> drawerStart(sizeof(CDrawer), 0);
        auto* drawer = reinterpret_cast<CDrawer*>(drawerStart.data());
        drawer->graphics = reinterpret_cast<void*>(0x6a000);
        drawer->text_surface = reinterpret_cast<void*>(0x75000);
        drawer->panel_left = rng.Range(0, 100);
        drawer->panel_top = rng.Range(0, 100);
        drawer->panel_right = rng.Range(100, 300);
        drawer->panel_bottom = rng.Range(100, 500);
        drawer->panel_redraw = static_cast<std::int32_t>(rng.Below(3));
        drawer->panel_buttons_redraw = static_cast<std::int32_t>(rng.Below(3));
        auto* data = reinterpret_cast<DrawDataView*>(env.data.data());
        FillPanel(rng, env, data);
        const std::vector<std::uint8_t> dataStart(env.data.begin(), env.data.end());

        Side a, b;
        Run(Original_CDrawer_DrawPanel, env, drawerStart, dataStart, a);
        Run(candidate, env, drawerStart, dataStart, b);
        const bool sameFlags = std::memcmp(a.flags, b.flags, sizeof(a.flags)) == 0;
        const bool sameInputs = std::memcmp(a.inputs, b.inputs, sizeof(a.inputs)) == 0;
        if (a.log == b.log && a.data == b.data && sameFlags && sameInputs) {
            continue;
        }
        ++failures;
        if (quiet) {
            continue;
        }
        std::size_t first = 0;
        while (first < a.log.size() && first < b.log.size() && a.log[first] == b.log[first]) {
            ++first;
        }
        const auto* d = reinterpret_cast<const DrawDataView*>(dataStart.data());
        ctx.Fail(__FILE__, __LINE__,
                 "case %d: menu %d page74 %u page6c %d page7c %d type %d: logs %u / %u entries, first difference "
                 "at %u, data same %d flags same %d inputs same %d",
                 n, d->unk060, d->unk074, d->unk06c, d->unk07c, d->unk0a4, static_cast<unsigned>(a.log.size()),
                 static_cast<unsigned>(b.log.size()), static_cast<unsigned>(first), a.data == b.data, sameFlags,
                 sameInputs);
        for (std::size_t i = first > 3 ? first - 3 : 0; i < first + 6; ++i) {
            ctx.Fail(__FILE__, __LINE__, "  [%u] %08x %08x", static_cast<unsigned>(i),
                     i < a.log.size() ? a.log[i] : 0xdeadu, i < b.log.size() ? b.log[i] : 0xdeadu);
        }
    }
    g_uiCellSize = savedCell;
    g_drawOriginX = savedX;
    g_drawOriginY = savedY;
    g_displayMode = savedMode;
    g_displayWidth = savedWidth;
    g_displayHeight = savedHeight;
    g_layout730f2c = saved2c;
    g_layout730f30 = saved30;
    g_layout730f34 = saved34;
    g_minimapMode = savedMinimap[0];
    g_minimapShowBuildings = savedMinimap[1];
    g_minimapShowSettlers = savedMinimap[2];
    g_campaignMission = savedMission;
    g_showBuildCosts = savedCosts;
    g_strengthDivisor = savedDivisor;
    for (int i = 0; i < 4; ++i) {
        g_panelInputGoods[i] = savedInputs[i];
        g_panelInputColumns[i] = savedInputs[4 + i];
    }
    return failures;
}

// A wrong version: the race seen one higher.
void __fastcall WrongDrawPanel(CDrawer* self, void* edx, DrawDataView* data) {
    data->unk094 += 1;
    CDrawer_DrawPanel(self, edx, data);
    data->unk094 -= 1;
}

}  // namespace

S3RE_TEST(CDrawer_DrawPanel_MatchesOriginal) {
    CompareDrawPanel(ctx, CDrawer_DrawPanel, 3000, false);
}

S3RE_TEST(CDrawer_DrawPanel_ComparisonDetectsWrongImplementation) {
    S3RE_EXPECT(CompareDrawPanel(ctx, WrongDrawPanel, 300, true) > 0);
}

}  // namespace s3re::game
