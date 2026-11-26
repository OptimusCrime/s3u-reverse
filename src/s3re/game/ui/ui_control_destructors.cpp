// Destructors of the grouped controls (Dirk\dialog.cpp), now that
// CScreenGroup::~CScreenGroup (ui_group_members.cpp) is ours. They are the
// compiler's chains of member and base destructors; "this" is the address
// the virtual base's thunks pass (the fields are relative to it).
//
// The CGroupControl part (CControl at -0xb8, CScreenGroup's element at
// "this") ends every chain: ~CScreenGroup 0x416da0(this; 0x80 for an object
// at address 0xb8, the compiler's null check of the base pointer), then
// ~CControl 0x4180a0(this - 0x7c). EAX = ~CControl's.
//
// CGroupControl::~CGroupControl (0x0041e210): the CGroupControl part.
// CDialog::~CDialog (0x0042a0a0): the CGroupControl part at this - 0xc.
// CScreen::~CScreen (0x0042a750): its message list (this-0x1c,
//   WCValListBase<MsgConsole> 0x656a4c -> WCIsvSListBase<MsgConsole>
//   0x656a40: with entries (+8) the exception hook NoOp 0x59f200 and
//   clearAndDestroy 0x60b560, then ~WCIsvListBase 0x60b5a0), then
//   ~CScreenGroup(this - 0x18). EAX = its.
// CVScrollBar::~CVScrollBar (0x0041f010): its four buttons (elements at
//   this-0x5c, -0xdc, -0x15c, -0x1dc: ~CButton 0x41a900 on element-0x10,
//   then ~CScreenElement 0x4156b0 on the element), then the CGroupControl
//   part at this-0x23c.
// CProgressBar::~CProgressBar (0x00429c70): its CStaticText (this-0x58:
//   the text's ~string inlined, then ~CScreenElement), then the
//   CGroupControl part at this-0x54.
// CUpDown::~CUpDown (0x0042b100): the up and down buttons (elements at
//   this-0x48, this-0xc8), the CStaticText at this-0x16c (~string,
//   ~CScreenElement), then the CGroupControl part at this-0x16c.
// CTextView::~CTextView (0x0042a380): its text (std::string at this-0x24,
//   ~string inlined), its scroll bar (~CVScrollBar inlined with this-0x80,
//   followed by ~CScreenElement on this-0x80), then the CGroupControl part
//   at this-0x37c.
// The inlined ~string is the one of 0x401b60 (split, erase, release).

#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

constexpr std::uintptr_t kScreenGroupDestroy = 0x00416da0;    // CScreenGroup::~CScreenGroup (ours)
constexpr std::uintptr_t kControlDestroy = 0x004180a0;        // CControl::~CControl (ours)
constexpr std::uintptr_t kButtonDestroy = 0x0041a900;         // CButton::~CButton (ours)
constexpr std::uintptr_t kElementDestroy = 0x004156b0;        // CScreenElement::~CScreenElement (ours)
constexpr std::uintptr_t kNoOp = 0x0059f200;                  // the containers' exception hook (`ret`)
constexpr std::uintptr_t kClearAndDestroy = 0x0060b560;       // WCIsvSListBase::clearAndDestroy (ours)
constexpr std::uintptr_t kIsvListDestroy = 0x0060b5a0;        // WCIsvListBase::~WCIsvListBase (ours)

constexpr std::ptrdiff_t kGroupControlSize = 0xb8;  // CControl (0x3c) + CScreenGroup (0x80) before the element
constexpr std::ptrdiff_t kButtonSpacing = 0x80;     // the scroll bar's buttons follow each other
constexpr std::ptrdiff_t kButtonControlPart = 0x10;  // ~CButton gets the element - 0x10
constexpr std::ptrdiff_t kLabelText = 0x28;          // CStaticText's text member

std::uint32_t Call0(std::uintptr_t function, std::uintptr_t self) {
    return reinterpret_cast<Thiscall0>(function)(self, nullptr);
}

// The CGroupControl part's destructors (0x41e22d..0x41e256 and its copies).
std::uint32_t DestroyGroupControl(std::uintptr_t self) {
    Call0(kScreenGroupDestroy, self - kGroupControlSize != 0 ? self : 0x80);
    return Call0(kControlDestroy, self - 0x7c);
}

// An embedded button: ~CButton, then its element's destructor.
void DestroyButton(std::uintptr_t element) {
    Call0(kButtonDestroy, element - kButtonControlPart);
    Call0(kElementDestroy, element);
}

// An embedded CStaticText: its text's ~string, then the element's.
void DestroyLabel(std::uintptr_t label) {
    msvc6::String_Destroy(reinterpret_cast<msvc6::String*>(label + kLabelText), nullptr);
    Call0(kElementDestroy, label);
}

// ~CVScrollBar's body (0x41f02e..0x41f0cc).
std::uint32_t DestroyScrollBar(std::uintptr_t self) {
    for (int k = 0; k < 4; ++k) {
        DestroyButton(self - 0x5c - k * kButtonSpacing);
    }
    return DestroyGroupControl(self - 0x23c);
}

}  // namespace

using DestroyFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

std::uint32_t __fastcall CGroupControl_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CDialog_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CScreen_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CVScrollBar_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CProgressBar_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CUpDown_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CTextView_Destroy(std::uintptr_t self, void* edx);

S3RE_HOOK(0x0041e210, CGroupControl_Destroy, DestroyFn, Original_CGroupControl_Destroy, CGroupControl_Destroy);
S3RE_HOOK(0x0042a0a0, CDialog_Destroy, DestroyFn, Original_CDialog_Destroy, CDialog_Destroy);
S3RE_HOOK(0x0042a750, CScreen_Destroy, DestroyFn, Original_CScreen_Destroy, CScreen_Destroy);
S3RE_HOOK(0x0041f010, CVScrollBar_Destroy, DestroyFn, Original_CVScrollBar_Destroy, CVScrollBar_Destroy);
S3RE_HOOK(0x00429c70, CProgressBar_Destroy, DestroyFn, Original_CProgressBar_Destroy, CProgressBar_Destroy);
S3RE_HOOK(0x0042b100, CUpDown_Destroy, DestroyFn, Original_CUpDown_Destroy, CUpDown_Destroy);
S3RE_HOOK(0x0042a380, CTextView_Destroy, DestroyFn, Original_CTextView_Destroy, CTextView_Destroy);

// Purpose: Destructor of a control that contains other elements (buttons
// of a scroll bar, a list's rows...).
std::uint32_t __fastcall CGroupControl_Destroy(std::uintptr_t self, void* /*edx*/) {
    return DestroyGroupControl(self);
}

// Purpose: Destructor of a dialog window of the menus.
std::uint32_t __fastcall CDialog_Destroy(std::uintptr_t self, void* /*edx*/) {
    return DestroyGroupControl(self - 0xc);
}

// Purpose: Destructor of a whole menu screen: its queued messages, then the
// container.
std::uint32_t __fastcall CScreen_Destroy(std::uintptr_t self, void* /*edx*/) {
    const std::uintptr_t messages = self - 0x1c;
    auto& vtable = *reinterpret_cast<std::uint32_t*>(messages);
    vtable = vtables::kWCValListBase_MsgConsole;
    if (*reinterpret_cast<const std::uint32_t*>(messages + 8) != 0) {
        Call0(kNoOp, messages);
        Call0(kClearAndDestroy, messages);
    }
    vtable = vtables::kWCIsvSListBase_MsgConsole;
    Call0(kIsvListDestroy, messages);
    return Call0(kScreenGroupDestroy, self - 0x18);
}

// Purpose: Destructor of a scroll bar: its four buttons, then the control.
std::uint32_t __fastcall CVScrollBar_Destroy(std::uintptr_t self, void* /*edx*/) {
    return DestroyScrollBar(self);
}

// Purpose: Destructor of a progress bar: its number label, then the control.
std::uint32_t __fastcall CProgressBar_Destroy(std::uintptr_t self, void* /*edx*/) {
    DestroyLabel(self - 0x58);
    return DestroyGroupControl(self - 0x54);
}

// Purpose: Destructor of a number spinner: its two buttons and its label,
// then the control.
std::uint32_t __fastcall CUpDown_Destroy(std::uintptr_t self, void* /*edx*/) {
    DestroyButton(self - 0x48);
    DestroyButton(self - 0xc8);
    DestroyLabel(self - 0x16c);
    return DestroyGroupControl(self - 0x16c);
}

// Purpose: Destructor of a multi-line text view: its text, its scroll bar,
// then the control.
std::uint32_t __fastcall CTextView_Destroy(std::uintptr_t self, void* /*edx*/) {
    msvc6::String_Destroy(reinterpret_cast<msvc6::String*>(self - 0x24), nullptr);
    const std::uintptr_t scrollBar = self - 0x80;
    DestroyScrollBar(scrollBar);
    Call0(kElementDestroy, scrollBar);
    return DestroyGroupControl(self - 0x37c);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One random 0x800-byte buffer, `this` 0x600 bytes in, set up alike for
// both versions; the strings the destructors release (none, own, shared
// with an outside string, frozen) are rebuilt before each run. The
// destructors they call are CalleeStubs (recorded with ECX relative to the
// buffer, EAX fixed by the call). Compared: EAX, the calls, the buffer and
// the outside strings' count bytes and texts.

namespace {

constexpr std::size_t kBytes = 0x800;
constexpr std::size_t kThis = 0x600;

struct Case {
    const char* name;
    DestroyFn original;
    DestroyFn ours;
    std::ptrdiff_t strings[2];  // string members relative to this (0 = none)
};

std::uint8_t g_buffer[kBytes];

std::uint32_t Fixed(CGameData*, std::uint32_t self, const std::uint32_t*) {
    return (self * 5) ^ 0xd0000000u;
}

bool CompareDestructor(test::Context& ctx, const Case& c, int cases) {
    static std::uint8_t afterOriginal[kBytes];
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        std::uint32_t eax[2] = {};
        std::vector<test::StubCall> calls[2];
        msvc6::String outside[2][2] = {};
        {
            test::CalleeStubs stubs;
            stubs.Install<0, kScreenGroupDestroy>(&Fixed);
            stubs.Install<0, kControlDestroy>(&Fixed);
            stubs.Install<0, kElementDestroy>(&Fixed);
            stubs.Install<0, kButtonDestroy>(&Fixed);
            stubs.Install<0, kClearAndDestroy>(&Fixed);
            stubs.Install<0, kIsvListDestroy>(&Fixed);
            stubs.Install<0, kNoOp>(&Fixed);
            stubs.SetWorld(reinterpret_cast<CGameData*>(g_buffer));
            for (int side = 0; side < 2; ++side) {
                test::Rng rng(seed);
                rng.Fill(g_buffer, kBytes);
                for (int s = 0; s < 2; ++s) {
                    if (c.strings[s] == 0) {
                        continue;
                    }
                    auto* member = reinterpret_cast<msvc6::String*>(g_buffer + kThis + c.strings[s]);
                    msvc6::detail::Clear(member);
                    const int kind = static_cast<int>(rng.Below(4));  // 0 none, 1 own, 2 shared, 3 frozen
                    char text[24];
                    const std::uint32_t length = rng.Below(sizeof(text));
                    for (std::uint32_t k = 0; k < length; ++k) {
                        text[k] = static_cast<char>(1 + rng.Below(255));
                    }
                    if (kind != 0) {
                        msvc6::String_AssignChars(member, nullptr, text, length);
                    }
                    if (kind == 2 && member->ptr != nullptr) {
                        msvc6::String_AssignSubstr(&outside[side][s], nullptr, member, 0, 0xFFFFFFFFu);
                    }
                    if (kind == 3) {
                        msvc6::String_Freeze(member, nullptr);
                    }
                }
                const auto self = reinterpret_cast<std::uintptr_t>(g_buffer + kThis);
                eax[side] = side == 0 ? c.original(self, nullptr) : c.ours(self, nullptr);
                calls[side] = stubs.Take();
                if (side == 0) {
                    std::memcpy(afterOriginal, g_buffer, kBytes);
                }
            }
        }
        bool same = S3RE_EXPECT_EQ(eax[0], eax[1]) && S3RE_EXPECT(calls[0] == calls[1]) &&
                    S3RE_EXPECT_MEM_EQ(afterOriginal, g_buffer, kBytes);
        for (int s = 0; s < 2; ++s) {
            const msvc6::String& a = outside[0][s];
            const msvc6::String& b = outside[1][s];
            same = same && S3RE_EXPECT_EQ(a.ptr == nullptr, b.ptr == nullptr);
            if (same && a.ptr != nullptr) {
                same = S3RE_EXPECT_EQ(static_cast<std::uint8_t>(a.ptr[-1]), static_cast<std::uint8_t>(b.ptr[-1])) &&
                       S3RE_EXPECT_MEM_EQ(a.ptr, b.ptr, a.len + 1);
            }
            msvc6::String_Destroy(&outside[0][s], nullptr);
            msvc6::String_Destroy(&outside[1][s], nullptr);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", c.name, i);
            return false;
        }
    }
    return true;
}

struct Members {
    DestroyFn groupControl = &CGroupControl_Destroy;
    DestroyFn dialog = &CDialog_Destroy;
    DestroyFn screen = &CScreen_Destroy;
    DestroyFn scrollBar = &CVScrollBar_Destroy;
    DestroyFn progressBar = &CProgressBar_Destroy;
    DestroyFn upDown = &CUpDown_Destroy;
    DestroyFn textView = &CTextView_Destroy;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    const Case all[] = {
        {"~CGroupControl", Original_CGroupControl_Destroy, ours.groupControl, {0, 0}},
        {"~CDialog", Original_CDialog_Destroy, ours.dialog, {0, 0}},
        {"~CScreen", Original_CScreen_Destroy, ours.screen, {0, 0}},
        {"~CVScrollBar", Original_CVScrollBar_Destroy, ours.scrollBar, {0, 0}},
        {"~CProgressBar", Original_CProgressBar_Destroy, ours.progressBar, {-0x58 + kLabelText, 0}},
        {"~CUpDown", Original_CUpDown_Destroy, ours.upDown, {-0x16c + kLabelText, 0}},
        {"~CTextView", Original_CTextView_Destroy, ours.textView, {-0x24, 0}},
    };
    for (const Case& c : all) {
        CompareDestructor(ctx, c, cases);
    }
}

// Wrong: the scroll bar's buttons destroyed in the opposite order.
std::uint32_t __fastcall WrongScrollBarDestroy(std::uintptr_t self, void*) {
    for (int k = 3; k >= 0; --k) {
        DestroyButton(self - 0x5c - k * kButtonSpacing);
    }
    return DestroyGroupControl(self - 0x23c);
}

// Wrong: the label's text released without the split (forgotten).
std::uint32_t __fastcall WrongProgressBarDestroy(std::uintptr_t self, void*) {
    msvc6::detail::Clear(reinterpret_cast<msvc6::String*>(self - 0x58 + kLabelText));
    Call0(kElementDestroy, self - 0x58);
    return DestroyGroupControl(self - 0x54);
}

}  // namespace

S3RE_TEST(UiControlDestructors_MatchOriginal) {
    CompareAll(ctx, Members{}, 1000);
}

S3RE_TEST(UiControlDestructors_ComparisonDetectsWrongImplementation) {
    Members wrong;
    wrong.scrollBar = &WrongScrollBarDestroy;
    wrong.progressBar = &WrongProgressBarDestroy;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAll(probe, wrong, 200);
    S3RE_EXPECT(probe.failures() >= 2);
}

}  // namespace s3re::game
