// UI members that draw through DirectDraw blits and GDI text measuring
// (Dirk\dialog.cpp; layouts in re/ui_classes.md). All thiscall. As in
// ui_control_members.cpp, "this" of the slot-10/11 members is a subobject
// whose own fields sit before it (offsets relative to it).
//
// CStaticSurface::Draw (0x0041df40; this = the element, a CStaticControl):
//   (surface, a, b) ret 0xc: DDBltFast 0x5bf130 on `surface` of the image
//   surface (+0x20) at the element's top-left (CScreenElement::Rect
//   0x4156d0 called twice, top first), no source rect, flags 0. EAX = its.
// CTextView::FillBackground (0x00421a20): (surface, rect) ret 8: the element
//   (vbptr this-0x430) transparent (flag1f): nothing, EAX = the vbtable
//   pointer with the flag byte in AL. Otherwise the area = IntersectRect(
//   rect, client rect this-0x44), the group (this-0x37c) CScreenGroup::
//   FillBackground 0x417460(surface, rect), then the area colour-filled
//   with the dword at this-0x37c (CSurface::DDBlt 0x5bf0c0(area, 0, 0,
//   DDBLT_COLORFILL, fx); only fx.dwSize = 0x64 and dwFillColor set). EAX =
//   DDBlt's.
// CListBox::FillBackground (0x00420340): the same with the vbptr at
//   this-0x440, the client rect this-0x48, the group this-0x38c and the
//   colour this-0x388; the transparency is ignored when the element's
//   parent (+0x18) is a CDropdown part (typeid(*parent) == CDropdown:
//   __RTtypeid 0x61f911 and type_info::operator== 0x61f00c, the CRT's), so
//   a dropdown's list is always filled. Skipped: EAX = vbtable[1].
// CProgressBar::Draw (0x004232f0): (surface, clip, b) ret 0xc: the element
//   rect (vbptr this-0x108), the two colours (this-0x10 background,
//   this-0x14 bar; bytes R, G, B) packed for the screen's pixel format
//   (Dialog::PixelFormat 0x416880: masks 0xf800/0x7e0/0x1f give RGB565,
//   anything else RGB555); the rect inset by (2, 1, 2, 1) and clipped
//   (IntersectRect) is filled with the background if not empty; inset by 1
//   more, its width scaled by value (this-0xc) / max (this-8, unless 0;
//   32-bit signed imul/idiv) and clipped, it is filled with the bar colour
//   if not empty; then CScreenGroup::Draw 0x417480 of the group (this-0x54)
//   with (surface, clip, b). EAX = Draw's.
// CControl::ShowTooltip (0x00418550; this = the control): with no tooltip
//   (+0x20) and a non-empty tooltip text (+0x10, compared with ""
//   through std::string::compare 0x40a140): the root screen (element slot
//   30), its rect; the text measured on the DirectDraw surface's DC
//   (Dialog::Surface 0x416870; IDirectDrawSurface::GetDC, COM slot 0x44,
//   until DD_OK: DDERR_WASSTILLDRAWING retries at once, other results go
//   through CSurface::CheckResult 0x5bf2e0 "CSurface::DDGetDC", which
//   restores a lost surface or throws, and retry while it returns non-zero;
//   SelectObject of Dialog::Font(12) (the old font is not put back),
//   GetTextExtentPoint32A; ReleaseDC, COM slot 0x68, checked with
//   "CSurface::DDReleaseDC" and repeated once if that gives
//   DDERR_SURFACELOST); the mouse position (+0x28/+0x2c) saved at
//   +0x30/+0x34; the box {x, y, x + cx + 5, y + cy} with x = min(mouse x,
//   screen right - (cx + 5)) and y = min(screen bottom - cy, mouse y + 30),
//   both at least 0 (signed); new CStaticText(box, text, font 0x20) (0x44
//   bytes; out of memory: Win32::ThrowOutOfMemory "lpTooltip", dialog.cpp
//   line 0x42d), style 0xc (CStaticText::SetStyle 0x41ca60), added to the
//   screen (group slot 0). EAX = AddChild's; an existing tooltip: EAX = it;
//   an empty text: 0.

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_gdi.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// CStaticSurface (vtable 0x65636c): a CStaticControl with an image surface.
struct StaticSurface {
    ui::CScreenElement element;  // 0x00
    std::uint32_t surface;       // 0x20 the CSurface it shows
};
static_assert(offsetof(StaticSurface, surface) == 0x20);

// DDBLTFX as the fills use it (0x64 bytes; only these two fields are set).
struct BltFx {
    std::uint32_t size;          // 0x00
    std::uint8_t unk04[0x4c];
    std::uint32_t fill_colour;   // 0x50
    std::uint8_t unk54[0x10];
};
static_assert(sizeof(BltFx) == 0x64);

// Dialog::PixelFormat's copy of the screen format (0x7a8020, 8 dwords).
struct ScreenPixelFormat {
    std::uint32_t unk00[4];
    std::uint32_t red_mask;    // 0x10
    std::uint32_t green_mask;  // 0x14
    std::uint32_t blue_mask;   // 0x18
    std::uint32_t unk1c;
};
static_assert(sizeof(ScreenPixelFormat) == 0x20);

// DirectX::CSurface: the IDirectDrawSurface at +8.
struct SurfaceObject {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t interface_;  // 0x08 IDirectDrawSurface*
};
#pragma pack(pop)

constexpr std::uintptr_t kElementRect = 0x004156d0;         // CScreenElement::Rect (ours)
constexpr std::uintptr_t kSurfaceBlt = 0x005bf0c0;          // DirectX::CSurface::DDBlt (ours)
constexpr std::uintptr_t kSurfaceBltFast = 0x005bf130;      // DirectX::CSurface::DDBltFast (ours)
constexpr std::uintptr_t kSurfaceCheckResult = 0x005bf2e0;  // DirectX::CSurface::CheckResult (ours)
constexpr std::uintptr_t kGroupFillBackground = 0x00417460;  // CScreenGroup::FillBackground (ours)
constexpr std::uintptr_t kGroupDraw = 0x00417480;           // CScreenGroup::Draw (ours)
constexpr std::uintptr_t kPixelFormat = 0x00416880;         // Dialog::PixelFormat (ours)
constexpr std::uintptr_t kDialogSurface = 0x00416870;       // Dialog::Surface (ours)
constexpr std::uintptr_t kDialogFont = 0x004168a0;          // Dialog::Font (ours)
constexpr std::uintptr_t kCompareChars = 0x0040a140;        // std::string::compare(pos, n, s, count) (ours)
constexpr std::uintptr_t kStaticTextConstruct = 0x0041c3e0;  // CStaticText::CStaticText (ours)
constexpr std::uintptr_t kStaticTextSetStyle = 0x0041ca60;  // CStaticText::SetStyle (ours)
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;    // Win32::ThrowOutOfMemory (ours)
constexpr std::uintptr_t kOperatorNew = 0x0061efb9;         // the game's operator new
constexpr std::uintptr_t kRtTypeid = 0x0061f911;            // __RTtypeid (CRT)
constexpr std::uintptr_t kTypeInfoEqual = 0x0061f00c;       // type_info::operator== (CRT)

constexpr std::uint32_t kBltColourFill = 0x400;  // DDBLT_COLORFILL
constexpr std::int32_t kWasStillDrawing = static_cast<std::int32_t>(0x8876021c);  // DDERR_WASSTILLDRAWING
constexpr std::int32_t kSurfaceLost = static_cast<std::int32_t>(0x887601c2);      // DDERR_SURFACELOST
constexpr std::uint32_t kGetDCSlot = 0x44;      // IDirectDrawSurface::GetDC
constexpr std::uint32_t kReleaseDCSlot = 0x68;  // IDirectDrawSurface::ReleaseDC

constexpr std::uint32_t kTooltipFont = 12;       // Dialog::Font index for measuring
constexpr std::uint32_t kTooltipTextFont = 0x20;  // the CStaticText's font argument
constexpr std::uint32_t kTooltipStyle = 0xc;
constexpr std::int32_t kTooltipPadding = 5;      // added to the text width
constexpr std::int32_t kTooltipBelowMouse = 30;  // the box starts this far below the mouse
constexpr std::uint32_t kStaticTextBytes = 0x44;
constexpr std::int32_t kTooltipLine = 0x42d;     // dialog.cpp line of the out-of-memory check

// RGB565 (the masks of a 16-bit 5-6-5 screen); anything else is packed
// as RGB555.
constexpr std::uint32_t kRed565 = 0xf800, kGreen565 = 0x7e0, kBlue565 = 0x1f;

// The virtual-base adjustments: `this` + delta + vbtable[1], where the
// vbtable pointer sits at `this` + delta (the class's vbptr).
std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t delta) {
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self + delta);
    return self + delta + vbtable[1];
}

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

using RectFn = RECT*(__fastcall*)(std::uintptr_t self, void* edx);
using BltFn = std::uint32_t(__fastcall*)(std::uint32_t surface, void* edx, const RECT* area, std::uint32_t source,
                                         const RECT* sourceRect, std::uint32_t flags, BltFx* fx);
using BltFastFn = std::uint32_t(__fastcall*)(std::uint32_t surface, void* edx, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t source, const RECT* sourceRect, std::uint32_t flags);
using GroupFillFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t surface, const RECT* rect);
using GroupDrawFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t surface, const RECT* clip,
                                               std::uint32_t b);
using PixelFormatFn = ScreenPixelFormat*(__stdcall*)(ScreenPixelFormat* out);
using DialogSurfaceFn = SurfaceObject*(__cdecl*)();
using DialogFontFn = std::uint32_t(__stdcall*)(std::uint32_t index);
using CheckResultFn = std::int32_t(__fastcall*)(SurfaceObject* self, void* edx, std::int32_t hr, const char* where);
using CompareCharsFn = std::int32_t(__fastcall*)(const msvc6::String* self, void* edx, std::uint32_t pos,
                                                 std::uint32_t n, const char* s, std::uint32_t count);
using GetScreenFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx);
using AddChildFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t child);
using StaticTextFn = std::uint32_t(__fastcall*)(void* self, void* edx, const RECT* rect, const msvc6::String* text,
                                                std::uint32_t font);
using SetStyleFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t style);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);
using OperatorNewFn = void*(__cdecl*)(std::uint32_t size);
using TypeidFn = const void*(__cdecl*)(const void* object);
using TypeInfoEqualFn = std::int32_t(__fastcall*)(const void* self, void* edx, const void* other);
using GetDCFn = std::int32_t(__stdcall*)(std::uint32_t self, HDC* dc);
using ReleaseDCFn = std::int32_t(__stdcall*)(std::uint32_t self, HDC dc);

// A COM method of the IDirectDrawSurface (`slot` is the byte offset).
template <typename Fn>
Fn ComMethod(std::uint32_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

// A virtual function of a CScreenElement part or a group (`slot` in bytes).
template <typename Fn>
Fn Virtual(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

// The colour-fill blit both FillBackgrounds end with.
std::uint32_t ColourFill(std::uint32_t surface, const RECT* area, std::uint32_t colour) {
    BltFx fx;
    fx.size = sizeof(BltFx);
    fx.fill_colour = colour;
    return reinterpret_cast<BltFn>(kSurfaceBlt)(surface, nullptr, area, 0, nullptr, kBltColourFill, &fx);
}

// 0x423368..0x4233fc: an RGB dword (R in the low byte) as a 16-bit pixel.
std::uint32_t PackColour(std::uint32_t rgb, bool rgb565) {
    const std::uint32_t r = rgb & 0xff, g = (rgb >> 8) & 0xff, b = (rgb >> 16) & 0xff;
    if (rgb565) {
        return (r & 0xf8) << 8 | (g & 0xfc) << 3 | b >> 3;
    }
    return (r & 0xf8) << 7 | (g & 0xf8) << 2 | b >> 3;
}

}  // namespace

// Purpose: Draws a static image control: blits its surface to the screen
// surface at the control's position.
std::uint32_t __fastcall CStaticSurface_Draw(StaticSurface* self, void*, std::uint32_t surface, std::uint32_t,
                                             std::uint32_t) {
    const auto element = reinterpret_cast<std::uintptr_t>(self);
    const std::uint32_t y = reinterpret_cast<RectFn>(kElementRect)(element, nullptr)->top;
    const std::uint32_t x = reinterpret_cast<RectFn>(kElementRect)(element, nullptr)->left;
    return reinterpret_cast<BltFastFn>(kSurfaceBltFast)(surface, nullptr, x, y, self->surface, nullptr, 0);
}

// Purpose: Fills a text view's background in its colour (nothing for a
// transparent one), after its children's group.
std::uint32_t __fastcall CTextView_FillBackground(std::uintptr_t self, void*, std::uint32_t surface, const RECT* rect) {
    const std::uintptr_t vbtable = Field<std::uint32_t>(self, -0x430);
    const auto* element = reinterpret_cast<const ui::CScreenElement*>(VirtualBase(self, -0x430));
    if (element->flag1f != 0) {
        return (vbtable & 0xffffff00u) | element->flag1f;  // (EAX left over: mov eax, vbptr; mov al, flag)
    }
    RECT area;
    IntersectRect(&area, rect, &Field<RECT>(self, -0x44));
    reinterpret_cast<GroupFillFn>(kGroupFillBackground)(self - 0x37c, nullptr, surface, rect);
    return ColourFill(surface, &area, Field<std::uint32_t>(self, -0x37c));
}

// Purpose: Fills a list box's background in its colour; a transparent list
// box is filled anyway when it is a dropdown's list.
std::uint32_t __fastcall CListBox_FillBackground(std::uintptr_t self, void*, std::uint32_t surface, const RECT* rect) {
    const auto* element = reinterpret_cast<const ui::CScreenElement*>(VirtualBase(self, -0x440));
    bool inDropdown = false;
    if (element->parent != 0) {
        const void* type = reinterpret_cast<TypeidFn>(kRtTypeid)(reinterpret_cast<const void*>(element->parent));
        inDropdown = reinterpret_cast<TypeInfoEqualFn>(kTypeInfoEqual)(
                         type, nullptr, reinterpret_cast<const void*>(type_descriptor::kCDropdown)) != 0;
    }
    if (!inDropdown && element->flag1f != 0) {
        const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self - 0x440);
        return static_cast<std::uint32_t>(vbtable[1]);  // (EAX left over)
    }
    RECT area;
    IntersectRect(&area, rect, &Field<RECT>(self, -0x48));
    reinterpret_cast<GroupFillFn>(kGroupFillBackground)(self - 0x38c, nullptr, surface, rect);
    return ColourFill(surface, &area, Field<std::uint32_t>(self, -0x388));
}

// Purpose: Draws a progress bar: the background, the filled part for the
// current value, then its children.
std::uint32_t __fastcall CProgressBar_Draw(std::uintptr_t self, void*, std::uint32_t surface, const RECT* clip,
                                           std::uint32_t b) {
    const std::uintptr_t element = VirtualBase(self, -0x108);
    RECT box = reinterpret_cast<const ui::CScreenElement*>(element)->rect;
    ScreenPixelFormat format;
    reinterpret_cast<PixelFormatFn>(kPixelFormat)(&format);
    const bool rgb565 = format.red_mask == kRed565 && format.green_mask == kGreen565 && format.blue_mask == kBlue565;
    const std::uint32_t background = PackColour(Field<std::uint32_t>(self, -0x10), rgb565);
    const std::uint32_t bar = PackColour(Field<std::uint32_t>(self, -0x14), rgb565);

    BltFx fx;  // shared by both fills, as in the original
    box.left += 2;
    box.top += 1;
    box.right -= 2;
    box.bottom -= 1;
    IntersectRect(&box, &box, clip);
    if (box.right - box.left > 0) {
        fx.size = sizeof(BltFx);
        fx.fill_colour = background;
        reinterpret_cast<BltFn>(kSurfaceBlt)(surface, nullptr, &box, 0, nullptr, kBltColourFill, &fx);
    }
    box.left += 1;
    box.top += 1;
    box.right -= 1;
    box.bottom -= 1;
    const std::int32_t maximum = Field<std::int32_t>(self, -0x8);
    if (maximum != 0) {
        // imul (32 bits, wrapping) then idiv, as the original.
        const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(box.right - box.left) *
                                                      static_cast<std::uint32_t>(Field<std::int32_t>(self, -0xc)));
        box.right = box.left + scaled / maximum;
    }
    IntersectRect(&box, &box, clip);
    if (box.right - box.left > 0) {
        fx.size = sizeof(BltFx);
        fx.fill_colour = bar;
        reinterpret_cast<BltFn>(kSurfaceBlt)(surface, nullptr, &box, 0, nullptr, kBltColourFill, &fx);
    }
    return reinterpret_cast<GroupDrawFn>(kGroupDraw)(self - 0x54, nullptr, surface, clip, b);
}

namespace {

// 0x418612..0x41863a: GetDC until DD_OK (a lost surface is restored by
// CheckResult, other failures throw there).
HDC SurfaceGetDC(SurfaceObject* surface) {
    HDC dc = nullptr;
    for (;;) {
        std::int32_t hr = ComMethod<GetDCFn>(surface->interface_, kGetDCSlot)(surface->interface_, &dc);
        if (hr == 0) {
            break;
        }
        if (hr != kWasStillDrawing) {
            hr = reinterpret_cast<CheckResultFn>(kSurfaceCheckResult)(surface, nullptr, hr, texts::kDDGetDC);
            if (hr == 0) {
                break;
            }
        }
    }
    return dc;
}

// 0x41867b..0x4186c0: ReleaseDC, once more if the surface was lost.
void SurfaceReleaseDC(SurfaceObject* surface, HDC dc) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::int32_t hr = ComMethod<ReleaseDCFn>(surface->interface_, kReleaseDCSlot)(surface->interface_, dc);
        const std::int32_t result =
            reinterpret_cast<CheckResultFn>(kSurfaceCheckResult)(surface, nullptr, hr, texts::kDDReleaseDC);
        if (result != kSurfaceLost) {
            break;
        }
    }
}

}  // namespace

// Purpose: Opens a control's tooltip: a text box below the mouse, sized to
// the text and kept on the screen, added to the root screen.
std::uint32_t __fastcall CControl_ShowTooltip(ui::CControl* self, void*) {
    if (self->tooltip != 0) {
        return self->tooltip;
    }
    const char* empty = texts::kEmpty;
    const std::int32_t differs = reinterpret_cast<CompareCharsFn>(kCompareChars)(
        &self->tooltip_text, nullptr, 0, self->tooltip_text.len, empty, static_cast<std::uint32_t>(std::strlen(empty)));
    if (differs == 0) {
        return 0;
    }
    const auto control = reinterpret_cast<std::uintptr_t>(self);
    const std::uintptr_t screen = Virtual<GetScreenFn>(VirtualBase(control, 4), ui::element_slot::kGetScreen)(
        VirtualBase(control, 4), nullptr);
    const RECT screenRect = *reinterpret_cast<RectFn>(kElementRect)(VirtualBase(screen, 4), nullptr);

    // Measure the text on the screen surface's DC.
    SurfaceObject* surface = reinterpret_cast<DialogSurfaceFn>(kDialogSurface)();
    const HDC dc = SurfaceGetDC(surface);
    const std::uint32_t font = reinterpret_cast<DialogFontFn>(kDialogFont)(kTooltipFont);
    g_importSelectObject.get()(dc, reinterpret_cast<void*>(font));
    const char* text = self->tooltip_text.ptr != nullptr ? self->tooltip_text.ptr : g_stringNullBuffer.get();
    SIZE size;
    g_importGetTextExtentPoint32A.get()(dc, text, static_cast<std::int32_t>(self->tooltip_text.len), &size);
    SurfaceReleaseDC(reinterpret_cast<DialogSurfaceFn>(kDialogSurface)(), dc);

    // The box below the mouse, kept inside the screen.
    const auto mouseX = static_cast<std::int32_t>(self->mouse_x);
    const auto mouseY = static_cast<std::int32_t>(self->mouse_y);
    self->tooltip_x = self->mouse_x;
    self->tooltip_y = self->mouse_y;
    const std::int32_t width = size.cx + kTooltipPadding;
    std::int32_t x = screenRect.right - width < mouseX ? screenRect.right - width : mouseX;
    if (x <= 0) {
        x = 0;
    }
    std::int32_t y = screenRect.bottom - size.cy < mouseY + kTooltipBelowMouse ? screenRect.bottom - size.cy
                                                                                 : mouseY + kTooltipBelowMouse;
    if (y <= 0) {
        y = 0;
    }
    const RECT box{x, y, x + width, y + size.cy};

    void* block = reinterpret_cast<OperatorNewFn>(kOperatorNew)(kStaticTextBytes);
    self->tooltip = block != nullptr ? reinterpret_cast<StaticTextFn>(kStaticTextConstruct)(
                                           block, nullptr, &box, &self->tooltip_text, kTooltipTextFont)
                                     : 0;
    if (self->tooltip == 0) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kTooltipExpression, texts::kDialogFile,
                                                                kTooltipLine);
    }
    reinterpret_cast<SetStyleFn>(kStaticTextSetStyle)(self->tooltip, nullptr, kTooltipStyle);
    const std::uintptr_t root = Virtual<GetScreenFn>(VirtualBase(control, 4), ui::element_slot::kGetScreen)(
        VirtualBase(control, 4), nullptr);
    return Virtual<AddChildFn>(root, ui::group_slot::kAddChild)(root, nullptr, self->tooltip);
}

using StaticSurfaceDrawFn = std::uint32_t(__fastcall*)(StaticSurface* self, void* edx, std::uint32_t surface,
                                                       std::uint32_t a, std::uint32_t b);
using FillBackgroundFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t surface,
                                                    const RECT* rect);
using ProgressDrawFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t surface,
                                                  const RECT* clip, std::uint32_t b);
using ShowTooltipFn = std::uint32_t(__fastcall*)(ui::CControl* self, void* edx);

S3RE_HOOK(0x0041df40, CStaticSurface_Draw, StaticSurfaceDrawFn, Original_CStaticSurface_Draw, CStaticSurface_Draw);
S3RE_HOOK(0x00421a20, CTextView_FillBackground, FillBackgroundFn, Original_CTextView_FillBackground,
          CTextView_FillBackground);
S3RE_HOOK(0x00420340, CListBox_FillBackground, FillBackgroundFn, Original_CListBox_FillBackground,
          CListBox_FillBackground);
S3RE_HOOK(0x004232f0, CProgressBar_Draw, ProgressDrawFn, Original_CProgressBar_Draw, CProgressBar_Draw);
S3RE_HOOK(0x00418550, CControl_ShowTooltip, ShowTooltipFn, Original_CControl_ShowTooltip, CControl_ShowTooltip);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Every callee is replaced by a recording stub (pointer arguments into the
// caller's frame by contents); IntersectRect and the CRT's typeid run for
// real (the parents get real vtables). Both versions run on the same buffer,
// set up alike before each, so pointers into it need no translation.

namespace {

constexpr std::size_t kBytes = 0x2000;
constexpr std::size_t kThis = 0x1000;      // `this` of the member under test
constexpr std::size_t kTable = 0x1800;     // vbtables
constexpr std::size_t kParent = 0x1900;    // a parent element
constexpr std::size_t kSurface = 0x1980;   // a CSurface
constexpr std::size_t kIface = 0x19c0;     // its IDirectDrawSurface
constexpr std::size_t kComTable = 0x1a00;  // the interface's vtable (0x80 slots)
constexpr std::size_t kScreen = 0x1c00;    // the root screen (group part)
constexpr std::size_t kScreenTable = 0x1d00;  // its vtable / the element's vtable
constexpr std::size_t kBlock = 0x1e00;     // what operator new returns

std::uintptr_t g_base = 0;
std::vector<std::uint32_t>* g_log = nullptr;
test::Rng* g_stubRng = nullptr;

std::uint32_t Normalize(std::uint32_t value) {
    return value - static_cast<std::uint32_t>(g_base) < kBytes ? value - static_cast<std::uint32_t>(g_base) + 0xb0000000u
                                                               : value;
}

void Log(std::initializer_list<std::uint32_t> values) {
    for (std::uint32_t v : values) {
        g_log->push_back(Normalize(v));
    }
}
void LogRect(const RECT* rect) {
    Log({static_cast<std::uint32_t>(rect->left), static_cast<std::uint32_t>(rect->top),
         static_cast<std::uint32_t>(rect->right), static_cast<std::uint32_t>(rect->bottom)});
}

std::uint32_t __fastcall BltStub(std::uint32_t surface, void*, const RECT* area, std::uint32_t source,
                                 const RECT* sourceRect, std::uint32_t flags, BltFx* fx) {
    Log({0xb1700001, surface, source, reinterpret_cast<std::uint32_t>(sourceRect), flags, fx->size, fx->fill_colour});
    LogRect(area);
    return g_stubRng->Next32();
}
std::uint32_t __fastcall BltFastStub(std::uint32_t surface, void*, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t source, const RECT* sourceRect, std::uint32_t flags) {
    Log({0xb1700002, surface, x, y, source, reinterpret_cast<std::uint32_t>(sourceRect), flags});
    return g_stubRng->Next32();
}
std::uint32_t __fastcall GroupFillStub(std::uintptr_t self, void*, std::uint32_t surface, const RECT* rect) {
    Log({0xb1700003, static_cast<std::uint32_t>(self), surface});
    LogRect(rect);
    return g_stubRng->Next32();
}
std::uint32_t __fastcall GroupDrawStub(std::uintptr_t self, void*, std::uint32_t surface, const RECT* clip,
                                       std::uint32_t b) {
    Log({0xb1700004, static_cast<std::uint32_t>(self), surface, b});
    LogRect(clip);
    return g_stubRng->Next32();
}
ScreenPixelFormat* __stdcall PixelFormatStub(ScreenPixelFormat* out) {
    Log({0xb1700005});
    g_stubRng->Fill(out, sizeof(*out));
    if (g_stubRng->Chance(1, 2)) {
        out->red_mask = kRed565;
        out->green_mask = kGreen565;
        out->blue_mask = kBlue565;
        if (g_stubRng->Chance(1, 4)) {  // one mask off
            (&out->red_mask)[g_stubRng->Below(3)] ^= 1u << g_stubRng->Below(32);
        }
    }
    return out;
}
SurfaceObject* __cdecl DialogSurfaceStub() {
    Log({0xb1700006});
    return reinterpret_cast<SurfaceObject*>(g_base + kSurface);
}
std::uint32_t __stdcall DialogFontStub(std::uint32_t index) {
    Log({0xb1700007, index});
    return 0xf0e70000u + index;
}
std::int32_t __fastcall CheckResultStub(SurfaceObject* self, void*, std::int32_t hr, const char* where) {
    Log({0xb1700008, reinterpret_cast<std::uint32_t>(self), static_cast<std::uint32_t>(hr),
         reinterpret_cast<std::uint32_t>(where)});
    // 0 ends the loops, a lost surface repeats ReleaseDC, else hr itself.
    const std::uint32_t pick = g_stubRng->Below(4);
    return pick < 2 ? 0 : pick == 2 ? kSurfaceLost : hr;
}
std::int32_t __stdcall GetDCStub(std::uint32_t self, HDC* dc) {
    Log({0xb1700009, self});
    static const std::int32_t kResults[] = {0, 0, 0, kWasStillDrawing, kSurfaceLost, static_cast<std::int32_t>(0x88760005)};
    *dc = reinterpret_cast<HDC>(static_cast<std::uintptr_t>(0xdc000000u + g_stubRng->Below(0x10000)));
    return kResults[g_stubRng->Below(6)];
}
std::int32_t __stdcall ReleaseDCStub(std::uint32_t self, HDC dc) {
    Log({0xb170000a, self, reinterpret_cast<std::uint32_t>(dc)});
    return g_stubRng->Chance(1, 2) ? 0 : kSurfaceLost;
}
std::uintptr_t __fastcall GetScreenStub(std::uintptr_t self, void*) {
    Log({0xb170000b, static_cast<std::uint32_t>(self)});
    return g_base + kScreen;
}
std::uint32_t __fastcall AddChildStub(std::uintptr_t self, void*, std::uint32_t child) {
    Log({0xb170000c, static_cast<std::uint32_t>(self), child});
    return g_stubRng->Next32();
}
void* __cdecl NewStub(std::uint32_t size) {
    Log({0xb170000d, size});
    return g_stubRng->Chance(1, 8) ? nullptr : reinterpret_cast<void*>(g_base + kBlock);
}
std::uint32_t __fastcall StaticTextStub(void* self, void*, const RECT* rect, const msvc6::String* text,
                                        std::uint32_t font) {
    Log({0xb170000e, reinterpret_cast<std::uint32_t>(self), reinterpret_cast<std::uint32_t>(text), font});
    LogRect(rect);
    return g_stubRng->Chance(1, 8) ? 0 : reinterpret_cast<std::uint32_t>(self);
}
std::uint32_t __fastcall SetStyleStub(std::uint32_t self, void*, std::uint32_t style) {
    Log({0xb170000f, self, style});
    return style;
}
void __fastcall ThrowOutOfMemoryStub(const char* text, const char* file, std::int32_t line) {
    Log({0xb1700010, reinterpret_cast<std::uint32_t>(text), reinterpret_cast<std::uint32_t>(file),
         static_cast<std::uint32_t>(line)});
}

struct StubPatch {
    std::uintptr_t address;
    const void* stub;
};
const StubPatch kStubs[] = {
    {kSurfaceBlt, reinterpret_cast<const void*>(&BltStub)},
    {kSurfaceBltFast, reinterpret_cast<const void*>(&BltFastStub)},
    {kGroupFillBackground, reinterpret_cast<const void*>(&GroupFillStub)},
    {kGroupDraw, reinterpret_cast<const void*>(&GroupDrawStub)},
    {kPixelFormat, reinterpret_cast<const void*>(&PixelFormatStub)},
    {kDialogSurface, reinterpret_cast<const void*>(&DialogSurfaceStub)},
    {kDialogFont, reinterpret_cast<const void*>(&DialogFontStub)},
    {kSurfaceCheckResult, reinterpret_cast<const void*>(&CheckResultStub)},
    {kOperatorNew, reinterpret_cast<const void*>(&NewStub)},
    {kStaticTextConstruct, reinterpret_cast<const void*>(&StaticTextStub)},
    {kStaticTextSetStyle, reinterpret_cast<const void*>(&SetStyleStub)},
    {kThrowOutOfMemory, reinterpret_cast<const void*>(&ThrowOutOfMemoryStub)},
};

void Put(std::uint8_t* buffer, std::size_t offset, std::uint32_t value) {
    std::memcpy(buffer + offset, &value, 4);
}
// Writes a pointer to `target` (an offset in the buffer) at `offset`.
void Link(std::uint8_t* buffer, std::size_t offset, std::size_t target) {
    Put(buffer, offset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + target)));
}
// The vbptr at this + delta -> a vbtable whose displacement puts the
// element at `element` (an offset in the buffer).
void VbTable(std::uint8_t* buffer, std::size_t vbptr, std::size_t slot, std::size_t element) {
    Link(buffer, vbptr, kTable + slot);
    Put(buffer, kTable + slot + 4, static_cast<std::uint32_t>(element - vbptr));
}

// The fixed objects: the surface and its COM vtable, the screen (vbptr at
// +4, its element vtable slot 0 = AddChild), the element vtable with
// GetScreen.
void SetUpObjects(test::Rng& rng, std::uint8_t* buffer) {
    Link(buffer, kSurface + 8, kIface);
    Link(buffer, kIface, kComTable);
    Put(buffer, kComTable + kGetDCSlot, reinterpret_cast<std::uint32_t>(&GetDCStub));
    Put(buffer, kComTable + kReleaseDCSlot, reinterpret_cast<std::uint32_t>(&ReleaseDCStub));
    Link(buffer, kScreen, kScreenTable);
    Put(buffer, kScreenTable + ui::group_slot::kAddChild, reinterpret_cast<std::uint32_t>(&AddChildStub));
    Put(buffer, kScreenTable + ui::element_slot::kGetScreen, reinterpret_cast<std::uint32_t>(&GetScreenStub));
    VbTable(buffer, kScreen + 4, 0x40, kScreen + 0x40 + rng.Below(8) * 4);
}

struct Run {
    std::uint32_t eax;
    std::vector<std::uint32_t> calls;
};

using Setup = void (*)(test::Rng& rng, std::uint8_t* buffer);

// Runs `call(base)` with the stubs installed (stub results from `stubSeed`).
template <typename Call>
Run RunOn(std::uint8_t* buffer, std::uint64_t stubSeed, Call call) {
    Run run{};
    test::Rng stubRng(stubSeed);
    test::FakeGdi gdi(stubSeed ^ 0x9d1);
    gdi.Install();
    g_base = reinterpret_cast<std::uintptr_t>(buffer);
    g_log = &run.calls;
    g_stubRng = &stubRng;
    run.eax = Normalize(call(reinterpret_cast<std::uintptr_t>(buffer)));
    g_log = nullptr;
    g_stubRng = nullptr;
    gdi.Uninstall();
    run.calls.insert(run.calls.end(), gdi.log().begin(), gdi.log().end());
    return run;
}

template <typename CallA, typename CallB>
bool CompareMember(test::Context& ctx, const char* name, Setup setup, CallA callOriginal, CallB callOurs, int cases) {
    static std::uint8_t buffer[kBytes], afterOriginal[kBytes];
    for (const StubPatch& patch : kStubs) {
        test::PatchJump(patch.address, patch.stub);
    }
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint64_t argumentSeed = ctx.rng().Next64();
        const std::uint64_t stubSeed = ctx.rng().Next64();
        test::Rng fillA(seed), fillB(seed);
        setup(fillA, buffer);
        const Run ra = RunOn(buffer, stubSeed, [&](std::uintptr_t base) { return callOriginal(base, argumentSeed); });
        std::memcpy(afterOriginal, buffer, kBytes);
        setup(fillB, buffer);
        const Run rb = RunOn(buffer, stubSeed, [&](std::uintptr_t base) { return callOurs(base, argumentSeed); });
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT(ra.calls == rb.calls) ||
            !S3RE_EXPECT_MEM_EQ(afterOriginal, buffer, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
            ok = false;
        }
    }
    for (auto it = std::rbegin(kStubs); it != std::rend(kStubs); ++it) {
        test::UnpatchJump(it->address);
    }
    return ok;
}

// Random rects: mostly small and ordered, sometimes anything.
void RandomRect(test::Rng& rng, std::uint8_t* buffer, std::size_t offset) {
    std::int32_t r[4];
    if (rng.Chance(1, 8)) {
        rng.Fill(r, sizeof(r));
    } else {
        r[0] = rng.Range(-20, 640);
        r[1] = rng.Range(-20, 480);
        r[2] = r[0] + rng.Range(-4, 300);
        r[3] = r[1] + rng.Range(-4, 200);
    }
    std::memcpy(buffer + offset, r, sizeof(r));
}

// The element part of a FillBackground/Draw member: vbptr at this + delta,
// the element at kThis + 0x100 with a random flag1f and (optionally) a
// parent with a real vtable.
void SetUpElement(test::Rng& rng, std::uint8_t* buffer, std::int32_t delta) {
    rng.Fill(buffer, kBytes);
    SetUpObjects(rng, buffer);
    const std::size_t element = kThis + 0x100;
    VbTable(buffer, kThis + delta, 0, element);
    buffer[element + 0x1f] = rng.Chance(1, 2) ? 0 : static_cast<std::uint8_t>(rng.Range(1, 255));
    RandomRect(rng, buffer, element + 8);
    static const std::uint32_t kParentVtables[] = {vtables::kCDropdown_element, vtables::kCListBox_element,
                                                   vtables::kCScreenElement};
    const std::uint32_t pick = rng.Below(4);
    if (pick == 3) {
        Put(buffer, element + 0x18, 0);
    } else {
        Link(buffer, element + 0x18, kParent);
        Put(buffer, kParent, kParentVtables[pick]);
    }
}

void SetUpTextView(test::Rng& rng, std::uint8_t* buffer) {
    SetUpElement(rng, buffer, -0x430);
    RandomRect(rng, buffer, kThis - 0x44);
}
void SetUpListBox(test::Rng& rng, std::uint8_t* buffer) {
    SetUpElement(rng, buffer, -0x440);
    RandomRect(rng, buffer, kThis - 0x48);
}
void SetUpProgressBar(test::Rng& rng, std::uint8_t* buffer) {
    SetUpElement(rng, buffer, -0x108);
    // value and max: mostly sensible, sometimes anything (not INT_MIN / -1).
    std::int32_t maximum = rng.Chance(1, 6) ? 0 : rng.Range(-5, 200);
    std::int32_t value = rng.Range(-10, 250);
    if (rng.Chance(1, 8)) {
        value = static_cast<std::int32_t>(rng.Next32());
        maximum = static_cast<std::int32_t>(rng.Next32() | 2);
    }
    Put(buffer, kThis - 0x8, static_cast<std::uint32_t>(maximum));
    Put(buffer, kThis - 0xc, static_cast<std::uint32_t>(value));
}
void SetUpStaticSurface(test::Rng& rng, std::uint8_t* buffer) {
    rng.Fill(buffer, kBytes);
    RandomRect(rng, buffer, kThis + 8);
}

// The control of ShowTooltip at kThis: vbptr at +4 -> its element (vtable
// with GetScreen), a tooltip or none, a tooltip text (a std::string with a
// buffer in the test buffer, empty or not), the mouse position.
void SetUpControl(test::Rng& rng, std::uint8_t* buffer) {
    rng.Fill(buffer, kBytes);
    SetUpObjects(rng, buffer);
    const std::size_t element = kThis + 0x3c + rng.Below(4) * 4;
    VbTable(buffer, kThis + 4, 0, element);
    Link(buffer, element, kScreenTable);
    Put(buffer, kThis + 0x20, rng.Chance(1, 6) ? rng.Next32() | 1 : 0);
    static const char* const kTexts[] = {"", "Holzfaeller", "Ein sehr langer Tooltip mit vielen Worten", "x"};
    const char* text = kTexts[rng.Below(4)];
    const std::size_t chars = 0x1800 - 0x100;  // the string's buffer
    std::memcpy(buffer + chars, text, std::strlen(text) + 1);
    if (text[0] == 0 && rng.Chance(1, 2)) {
        Put(buffer, kThis + 0x14, 0);  // no buffer
    } else {
        Link(buffer, kThis + 0x14, chars);
    }
    Put(buffer, kThis + 0x18, static_cast<std::uint32_t>(std::strlen(text)));
    Put(buffer, kThis + 0x1c, 31);
    // The screen rect (the screen's element) and the mouse.
    const std::uint32_t screenElement = static_cast<std::uint32_t>(kScreen + 4) +
                                        *reinterpret_cast<std::uint32_t*>(buffer + kTable + 0x40 + 4);
    std::int32_t screen[4] = {0, 0, rng.Chance(1, 2) ? 640 : 1024, rng.Chance(1, 2) ? 480 : 768};
    if (rng.Chance(1, 8)) {
        rng.Fill(screen, sizeof(screen));
    }
    std::memcpy(buffer + screenElement + 8, screen, sizeof(screen));
    Put(buffer, kThis + 0x28, static_cast<std::uint32_t>(rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32())
                                                                         : rng.Range(-20, 1100)));
    Put(buffer, kThis + 0x2c, static_cast<std::uint32_t>(rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32())
                                                                         : rng.Range(-40, 800)));
}

struct Members {
    StaticSurfaceDrawFn staticSurfaceDraw = &CStaticSurface_Draw;
    FillBackgroundFn textViewFill = &CTextView_FillBackground;
    FillBackgroundFn listBoxFill = &CListBox_FillBackground;
    ProgressDrawFn progressDraw = &CProgressBar_Draw;
    ShowTooltipFn showTooltip = &CControl_ShowTooltip;
};

std::uint32_t Arg(std::uint64_t seed, int k) {
    return test::Rng(seed + k).Next32();
}

// The clip/rect argument, a rect in the buffer.
constexpr std::size_t kArgRect = 0x1f00;

bool CompareAll(test::Context& ctx, const Members& ours, int cases) {
    const Members original{Original_CStaticSurface_Draw, Original_CTextView_FillBackground,
                           Original_CListBox_FillBackground, Original_CProgressBar_Draw, Original_CControl_ShowTooltip};
    bool ok = true;
    ok = ok && CompareMember(
                   ctx, "CStaticSurface::Draw", &SetUpStaticSurface,
                   [&](std::uintptr_t base, std::uint64_t s) {
                       return original.staticSurfaceDraw(reinterpret_cast<StaticSurface*>(base + kThis), nullptr,
                                                         Arg(s, 0), Arg(s, 1), Arg(s, 2));
                   },
                   [&](std::uintptr_t base, std::uint64_t s) {
                       return ours.staticSurfaceDraw(reinterpret_cast<StaticSurface*>(base + kThis), nullptr, Arg(s, 0),
                                                     Arg(s, 1), Arg(s, 2));
                   },
                   cases);
    static Setup s_setup = nullptr;
    auto fill = [&](const char* name, Setup setup, FillBackgroundFn a, FillBackgroundFn b) {
        s_setup = setup;
        return CompareMember(
            ctx, name, [](test::Rng& rng, std::uint8_t* buffer) { s_setup(rng, buffer); RandomRect(rng, buffer, kArgRect); },
            [&](std::uintptr_t base, std::uint64_t s) {
                return a(base + kThis, nullptr, Arg(s, 0), reinterpret_cast<const RECT*>(base + kArgRect));
            },
            [&](std::uintptr_t base, std::uint64_t s) {
                return b(base + kThis, nullptr, Arg(s, 0), reinterpret_cast<const RECT*>(base + kArgRect));
            },
            cases);
    };
    ok = ok && fill("CTextView::FillBackground", &SetUpTextView, original.textViewFill, ours.textViewFill);
    ok = ok && fill("CListBox::FillBackground", &SetUpListBox, original.listBoxFill, ours.listBoxFill);
    ok = ok && CompareMember(
                   ctx, "CProgressBar::Draw",
                   [](test::Rng& rng, std::uint8_t* buffer) {
                       SetUpProgressBar(rng, buffer);
                       RandomRect(rng, buffer, kArgRect);
                   },
                   [&](std::uintptr_t base, std::uint64_t s) {
                       return original.progressDraw(base + kThis, nullptr, Arg(s, 0),
                                                    reinterpret_cast<const RECT*>(base + kArgRect), Arg(s, 1));
                   },
                   [&](std::uintptr_t base, std::uint64_t s) {
                       return ours.progressDraw(base + kThis, nullptr, Arg(s, 0),
                                                reinterpret_cast<const RECT*>(base + kArgRect), Arg(s, 1));
                   },
                   cases);
    ok = ok && CompareMember(
                   ctx, "CControl::ShowTooltip", &SetUpControl,
                   [&](std::uintptr_t base, std::uint64_t) {
                       return original.showTooltip(reinterpret_cast<ui::CControl*>(base + kThis), nullptr);
                   },
                   [&](std::uintptr_t base, std::uint64_t) {
                       return ours.showTooltip(reinterpret_cast<ui::CControl*>(base + kThis), nullptr);
                   },
                   cases);
    return ok;
}

// Wrong versions for the harness check.
std::uint32_t __fastcall WrongStaticSurfaceDraw(StaticSurface* self, void*, std::uint32_t surface, std::uint32_t,
                                                std::uint32_t) {
    const RECT* r = reinterpret_cast<RectFn>(kElementRect)(reinterpret_cast<std::uintptr_t>(self), nullptr);
    reinterpret_cast<RectFn>(kElementRect)(reinterpret_cast<std::uintptr_t>(self), nullptr);
    return reinterpret_cast<BltFastFn>(kSurfaceBltFast)(surface, nullptr, r->top, r->left, self->surface, nullptr, 0);
}
std::uint32_t __fastcall WrongListBoxFill(std::uintptr_t self, void* edx, std::uint32_t surface, const RECT* rect) {
    // Ignores the dropdown exception.
    const auto* element = reinterpret_cast<const ui::CScreenElement*>(VirtualBase(self, -0x440));
    if (element->parent != 0) {
        const void* type = reinterpret_cast<TypeidFn>(kRtTypeid)(reinterpret_cast<const void*>(element->parent));
        reinterpret_cast<TypeInfoEqualFn>(kTypeInfoEqual)(type, nullptr,
                                                          reinterpret_cast<const void*>(type_descriptor::kCDropdown));
    }
    if (element->flag1f != 0) {
        const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self - 0x440);
        return static_cast<std::uint32_t>(vbtable[1]);
    }
    return CListBox_FillBackground(self, edx, surface, rect);
}
std::uint32_t __fastcall WrongProgressDraw(std::uintptr_t self, void* edx, std::uint32_t surface, const RECT* clip,
                                           std::uint32_t b) {
    // Packs both colours as RGB565.
    Field<std::uint32_t>(self, -0x10) &= 0xfffffbff;
    return CProgressBar_Draw(self, edx, surface, clip, b);
}
std::uint32_t __fastcall WrongShowTooltip(ui::CControl* self, void* edx) {
    self->mouse_y += 1;  // the box one pixel lower
    const std::uint32_t result = CControl_ShowTooltip(self, edx);
    self->mouse_y -= 1;
    return result;
}

}  // namespace

S3RE_TEST(UiGdiMembers_MatchOriginal) {
    CompareAll(ctx, Members{}, 1500);
}

S3RE_TEST(UiGdiMembers_ComparisonDetectsWrongImplementation) {
    const struct {
        const char* name;
        void (*patch)(Members&);
    } kWrong[] = {
        {"CStaticSurface::Draw", [](Members& m) { m.staticSurfaceDraw = &WrongStaticSurfaceDraw; }},
        {"CListBox::FillBackground", [](Members& m) { m.listBoxFill = &WrongListBoxFill; }},
        {"CProgressBar::Draw", [](Members& m) { m.progressDraw = &WrongProgressDraw; }},
        {"CControl::ShowTooltip", [](Members& m) { m.showTooltip = &WrongShowTooltip; }},
    };
    for (const auto& wrong : kWrong) {
        Members candidate;
        wrong.patch(candidate);
        test::Context quiet("wrong", 11, true);
        if (!S3RE_EXPECT(!CompareAll(quiet, candidate, 1500))) {
            ctx.Fail(__FILE__, __LINE__, "  the wrong %s was not caught", wrong.name);
        }
    }
}

}  // namespace s3re::game
