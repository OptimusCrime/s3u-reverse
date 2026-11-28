// Layout of the text view and clicks on a list box (Dirk\dialog.cpp,
// wip/dialog-r8). All thiscall; layouts in re/ui_classes.md.
//
// CTextView::UpdateLayout (0x00420d90; this = the text view's start, vbptr
//   +4 to the element): with more lines (+0x408) than fit (+0x404; signed)
//   the client rect (+0x3f0) = the element's rect inset by 4 with right -
//   16 and the scroll bar (+0xc0, its element through vbptr +0xc4) shown
//   (CScreenGroup::ShowChild 0x417090 on +0x38 with (1, bar)); otherwise
//   inset by 4 on all sides and the bar hidden (0, bar). (The bar is a
//   member, so its null test is dead.) Then Reflow 0x420eb0, the bar's
//   CVScrollBar::SetRange 0x41e380(first line +0x400, lines, fitting), and
//   Invalidate. The rect is read and the client rect written in the
//   original's order. EAX = Invalidate's.
// CTextView::OnResized (0x00421bb0; slot 29 through a thunk; this = the
//   text view + 0x434): (rect*, size delta) ret 8: whether the view showed
//   its end before (lines - first < fitting, signed); client top = rect top
//   + 2, bottom = rect bottom - 2, fitting = (bottom - top - 1) / line
//   height (+0x40c; signed division); the bar placed at {right - 12, top,
//   right, bottom} of `rect` (SetRect 0x4156e0); its range set; UpdateLayout
//   0x420d90; if it showed its end and (lines - first < 3 or lines >= first
//   + fitting), ScrollTo 0x420d60(max(0, lines - fitting + 4)). EAX = the
//   ScrollTo's, the line count when it doesn't scroll, UpdateLayout's when it
//   didn't show its end.
// CListBox::OnLButtonDown (0x004203f0; slot 12; this = the list box's
//   element part, its fields before it): (message by value) ret 0x14:
//   CControl::OnLButtonDown 0x418380 (this - 0x408) with the message;
//   disabled (byte this - 0x438): AL = 0 over that call's EAX. Otherwise, if
//   the click (x, y: the words at message + 8 / + 10) is inside the item
//   rect (this - 0x48; PtInRect): the selection (this - 0x2c) = (y - item
//   top) / line height (this - 0x50) + first visible (this - 0x34), -1 if
//   not below the item count (this - 0x10, unsigned); the listener (this -
//   0x390) slot 0 with (the element (vbptr this - 0x440), selection);
//   Invalidate; AL = 1 over Invalidate's EAX. Outside: EAX = 0.

#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGroupShowChild = 0x00417090;   // CScreenGroup::ShowChild (ours)
constexpr std::uintptr_t kReflow = 0x00420eb0;           // CTextView::Reflow
constexpr std::uintptr_t kScrollSetRange = 0x0041e380;   // CVScrollBar::SetRange (ours)
constexpr std::uintptr_t kElementSetRect = 0x004156e0;   // CScreenElement::SetRect (ours)
constexpr std::uintptr_t kUpdateLayout = 0x00420d90;     // CTextView::UpdateLayout (ours)
constexpr std::uintptr_t kTextViewScrollTo = 0x00420d60;  // CTextView::ScrollTo (ours)
constexpr std::uintptr_t kControlOnLButtonDown = 0x00418380;  // CControl::OnLButtonDown (ours)

constexpr LONG kClientInset = 4;
constexpr LONG kClientRightWithBar = 16;
constexpr LONG kScrollBarWidth = 12;
constexpr LONG kResizeInset = 2;          // OnResized's client top/bottom inset
constexpr std::int32_t kEndMargin = 3;    // fewer lines below the first than this: scroll to the end
constexpr std::int32_t kEndOvershoot = 4;  // the end scroll leaves this many lines of room

namespace off {  // CTextView, from its start
constexpr std::int32_t kRowsGroup = 0x38;
constexpr std::int32_t kScrollBar = 0xc0;
constexpr std::int32_t kScrollBarVbPtr = 0xc4;
constexpr std::int32_t kClientRect = 0x3f0;
constexpr std::int32_t kFirstLine = 0x400;
constexpr std::int32_t kFitting = 0x404;
constexpr std::int32_t kLines = 0x408;
constexpr std::int32_t kLineHeight = 0x40c;
constexpr std::int32_t kFromResized = -0x434;  // OnResized's this -> the text view
}  // namespace off

namespace lb {  // CListBox, from its element part
constexpr std::int32_t kControl = -0x408;
constexpr std::int32_t kDisabled = -0x438;
constexpr std::int32_t kVbPtr = -0x440;
constexpr std::int32_t kListener = -0x390;
constexpr std::int32_t kLineHeight = -0x50;
constexpr std::int32_t kItemRect = -0x48;
constexpr std::int32_t kFirstVisible = -0x34;
constexpr std::int32_t kSelection = -0x2c;
constexpr std::int32_t kItemCount = -0x10;
}  // namespace lb

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

std::int32_t Displacement(std::uintptr_t self, std::int32_t vbptr) {
    return (*reinterpret_cast<const std::int32_t* const*>(self + vbptr))[1];
}

std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t vbptr) {
    return self + vbptr + Displacement(self, vbptr);
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

struct Message {  // 20 bytes, passed by value
    std::uint32_t words[5];
};
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

std::uint32_t Invalidate(std::uintptr_t element) {
    return Slot<Thiscall0>(element, ui::element_slot::kInvalidate)(element, nullptr);
}

void SetScrollRange(std::uintptr_t view) {
    reinterpret_cast<Thiscall3>(kScrollSetRange)(view + off::kScrollBar, nullptr,
                                                 Field<std::uint32_t>(view, off::kFirstLine),
                                                 Field<std::uint32_t>(view, off::kLines),
                                                 Field<std::uint32_t>(view, off::kFitting));
}

}  // namespace

using UpdateLayoutFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using OnResizedFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta);
using OnLButtonDownFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

// Purpose: Fits a text view's client area and scroll bar to its rectangle
// (the bar only when the text is longer than the view) and rewraps the text.
std::uint32_t __fastcall CTextView_UpdateLayout(std::uintptr_t self, void*) {
    auto rect = [&]() -> const RECT& {
        return Field<RECT>(self + 4 + Displacement(self, 4), offsetof(ui::CScreenElement, rect));
    };
    const bool showBar = Field<std::int32_t>(self, off::kLines) > Field<std::int32_t>(self, off::kFitting);
    RECT& client = Field<RECT>(self, off::kClientRect);
    client.left = rect().left + kClientInset;
    client.top = rect().top + kClientInset;
    client.right = rect().right - (showBar ? kClientRightWithBar : kClientInset);
    client.bottom = rect().bottom - kClientInset;
    const std::uintptr_t bar = self + off::kScrollBar;  // a member: never null
    const std::uintptr_t barElement = bar == 0 ? 0 : VirtualBase(self, off::kScrollBarVbPtr);
    reinterpret_cast<Thiscall2>(kGroupShowChild)(self + off::kRowsGroup, nullptr, showBar ? 1 : 0,
                                                 static_cast<std::uint32_t>(barElement));
    reinterpret_cast<Thiscall0>(kReflow)(self, nullptr);
    SetScrollRange(self);
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Lays a text view out for its new size; a view that showed the
// end of its text (a chat) keeps showing it.
std::uint32_t __fastcall CTextView_OnResized(std::uintptr_t self, void*, const RECT* rect, std::uint32_t /*delta*/) {
    const std::uintptr_t view = self + off::kFromResized;
    const bool showedEnd = Field<std::int32_t>(view, off::kLines) - Field<std::int32_t>(view, off::kFirstLine) <
                           Field<std::int32_t>(view, off::kFitting);
    RECT& client = Field<RECT>(view, off::kClientRect);
    client.top = rect->top + kResizeInset;
    client.bottom = rect->bottom - kResizeInset;
    Field<std::int32_t>(view, off::kFitting) =
        (client.bottom - client.top - 1) / Field<std::int32_t>(view, off::kLineHeight);
    const RECT bar{rect->right - kScrollBarWidth, rect->top, rect->right, rect->bottom};
    reinterpret_cast<Thiscall1>(kElementSetRect)(VirtualBase(view, off::kScrollBarVbPtr), nullptr,
                                                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bar)));
    SetScrollRange(view);
    const std::uint32_t laidOut = reinterpret_cast<Thiscall0>(kUpdateLayout)(view, nullptr);
    if (!showedEnd) {
        return laidOut;
    }
    const std::int32_t lines = Field<std::int32_t>(view, off::kLines);
    const std::int32_t first = Field<std::int32_t>(view, off::kFirstLine);
    if (lines - first >= kEndMargin && lines < Field<std::int32_t>(view, off::kFitting) + first) {
        return static_cast<std::uint32_t>(lines);
    }
    const std::int32_t target = lines - Field<std::int32_t>(view, off::kFitting) + kEndOvershoot;
    return reinterpret_cast<Thiscall1>(kTextViewScrollTo)(view, nullptr, target < 0 ? 0u : static_cast<std::uint32_t>(target));
}

// Purpose: Selects the list box item that was clicked and tells the listener.
std::uint32_t __fastcall CListBox_OnLButtonDown(std::uintptr_t self, void*, Message message) {
    const auto x = static_cast<LONG>(message.words[2] & 0xffff);
    const auto y = static_cast<LONG>(message.words[2] >> 16);
    const std::uint32_t handled =
        reinterpret_cast<MessageFn>(kControlOnLButtonDown)(self + lb::kControl, nullptr, message);
    if (Field<std::uint8_t>(self, lb::kDisabled) != 0) {
        return handled & 0xffffff00u;  // xor al, al over the handler's EAX
    }
    if (PtInRect(&Field<RECT>(self, lb::kItemRect), POINT{x, y}) == 0) {
        return 0;
    }
    std::int32_t& selection = Field<std::int32_t>(self, lb::kSelection);
    selection = (y - Field<RECT>(self, lb::kItemRect).top) / Field<std::int32_t>(self, lb::kLineHeight) +
                Field<std::int32_t>(self, lb::kFirstVisible);
    if (static_cast<std::uint32_t>(selection) >= Field<std::uint32_t>(self, lb::kItemCount)) {
        selection = -1;
    }
    if (const std::uintptr_t listener = Field<std::uint32_t>(self, lb::kListener); listener != 0) {
        // The element of the list box (its null test is dead: it is `this`'s own).
        const std::uintptr_t element = self + lb::kVbPtr - 4 == 0 ? 0 : VirtualBase(self, lb::kVbPtr);
        Slot<Thiscall2>(listener, 0)(listener, nullptr, static_cast<std::uint32_t>(element),
                                     static_cast<std::uint32_t>(selection));
    }
    return (Invalidate(VirtualBase(self, lb::kVbPtr)) & 0xffffff00u) | 1;
}

S3RE_HOOK(0x00420d90, CTextView_UpdateLayout, UpdateLayoutFn, Original_CTextView_UpdateLayout, CTextView_UpdateLayout);
S3RE_HOOK(0x00421bb0, CTextView_OnResized, OnResizedFn, Original_CTextView_OnResized, CTextView_OnResized);
S3RE_HOOK(0x004203f0, CListBox_OnLButtonDown, OnLButtonDownFn, Original_CListBox_OnLButtonDown, CListBox_OnLButtonDown);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

std::int32_t Small(test::Rng& r) {
    return r.Chance(1, 10) ? static_cast<std::int32_t>(r.Next32() >> 1) : r.Range(-3, 14);
}

// The text view (`this` - kFromResized for OnResized): element and bar
// behind their vbptrs, small counts, a line height 1..16 (or negative).
void TextView(test::Rng& r, std::uint8_t* b, std::size_t view) {
    const std::size_t element = kit::VbPtr(b, view + 4, 0x600, 0);
    kit::VbPtr(b, view + off::kScrollBarVbPtr, 0x700, 1);
    for (int k = 0; k < 4; ++k) {
        kit::Put(b, element + 8 + 4 * k, static_cast<std::uint32_t>(r.Range(-20, 300)));
    }
    for (std::int32_t field : {off::kFirstLine, off::kFitting, off::kLines}) {
        kit::Put(b, view + field, static_cast<std::uint32_t>(Small(r)));
    }
    kit::Put(b, view + off::kLineHeight, static_cast<std::uint32_t>(r.Chance(1, 8) ? -r.Range(1, 9) : r.Range(1, 16)));
}

constexpr std::size_t kResizedView = kThis - 0x434 + 0x400;  // the view for OnResized; `this` = view + 0x434

struct Members {
    UpdateLayoutFn update = &CTextView_UpdateLayout;
    OnResizedFn resized = &CTextView_OnResized;
    OnLButtonDownFn click = &CListBox_OnLButtonDown;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kGroupShowChild, 2>()
        .This<kReflow, 0>()
        .This<kScrollSetRange, 3>()
        .This<kElementSetRect, 1>()
        .This<kUpdateLayout, 0>()
        .This<kTextViewScrollTo, 1>()
        .This<kControlOnLButtonDown, 5>();
    kit::Compare(
        ctx, "UpdateLayout", cases, [](test::Rng& r, std::uint8_t* b) { TextView(r, b, kThis); },
        [](std::uintptr_t base, test::Rng&) { return Original_CTextView_UpdateLayout(base + kThis, nullptr); },
        [&](std::uintptr_t base, test::Rng&) { return ours.update(base + kThis, nullptr); });
    // OnResized: `this` = kResizedView + 0x434; the rect in the buffer.
    auto resized = [](OnResizedFn fn) {
        return [fn](std::uintptr_t base, test::Rng&) {
            return fn(base + kResizedView + 0x434, nullptr, reinterpret_cast<const RECT*>(base + 0x80), 0);
        };
    };
    kit::Compare(
        ctx, "TextViewOnResized", cases,
        [](test::Rng& r, std::uint8_t* b) {
            TextView(r, b, kResizedView);
            for (int k = 0; k < 4; ++k) {
                kit::Put(b, 0x80 + 4 * k, static_cast<std::uint32_t>(r.Range(-20, 200)));
            }
        },
        resized(Original_CTextView_OnResized), resized(ours.resized));
    // OnLButtonDown: the item rect and counts small; the listener none or a
    // fake object; the click near the rect.
    auto click = [](OnLButtonDownFn fn) {
        return [fn](std::uintptr_t base, test::Rng& r) {
            Message m{};
            for (auto& w : m.words) {
                w = r.Next32();
            }
            m.words[2] = static_cast<std::uint32_t>(r.Range(-5, 120)) & 0xffff;
            m.words[2] |= (static_cast<std::uint32_t>(r.Range(-5, 120)) & 0xffff) << 16;
            return fn(base + kThis, nullptr, m);
        };
    };
    kit::Compare(
        ctx, "ListBoxOnLButtonDown", cases,
        [](test::Rng& r, std::uint8_t* b) {
            kit::VbPtr(b, kThis + lb::kVbPtr, 0x500, 0);
            b[kThis + lb::kDisabled] = static_cast<std::uint8_t>(r.Chance(1, 4) ? 1 : 0);
            const std::int32_t rect[4] = {r.Range(-5, 20), r.Range(-5, 20), r.Range(20, 120), r.Range(20, 120)};
            std::memcpy(b + kThis + lb::kItemRect, rect, sizeof(rect));
            kit::Put(b, kThis + lb::kLineHeight, static_cast<std::uint32_t>(r.Chance(1, 8) ? -r.Range(1, 9) : r.Range(1, 16)));
            kit::Put(b, kThis + lb::kFirstVisible, static_cast<std::uint32_t>(r.Range(-3, 10)));
            kit::Put(b, kThis + lb::kItemCount, r.Below(20));
            if (r.Chance(1, 3)) {
                kit::Put(b, kThis + lb::kListener, 0);
            } else {
                kit::Link(b, kThis + lb::kListener, 0x2400);
            }
            // The listener (0x2400): its vtable (0x2500) has a 2-argument
            // recording stub (the fake vtable's slot 11) in slot 0.
            std::uint32_t twoArgs = 0;
            std::memcpy(&twoArgs, b + kit::kVtable + 4 * 11, 4);
            kit::Link(b, 0x2400, 0x2500);
            kit::Put(b, 0x2500, twoArgs);
        },
        click(Original_CListBox_OnLButtonDown), click(ours.click));
}

std::uint32_t __fastcall WrongUpdate(std::uintptr_t self, void* edx) {
    const std::uint32_t result = CTextView_UpdateLayout(self, edx);
    Field<RECT>(self, off::kClientRect).bottom += 1;
    return result;
}
std::uint32_t __fastcall WrongResized(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta) {
    RECT shifted = *rect;
    shifted.bottom += 1;  // one more pixel: the fitting count differs now and then
    return CTextView_OnResized(self, edx, &shifted, delta);
}
std::uint32_t __fastcall WrongClick(std::uintptr_t self, void* edx, Message message) {
    message.words[2] += 0x10000;  // one pixel lower
    return CListBox_OnLButtonDown(self, edx, message);
}

}  // namespace

S3RE_TEST(UiTextViewLayout_MatchOriginal) {
    CompareAll(ctx, Members{}, 800);
}

S3RE_TEST(UiTextViewLayout_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 3; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.update = &WrongUpdate;
        } else if (k == 1) {
            wrong.resized = &WrongResized;
        } else {
            wrong.click = &WrongClick;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 300);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
