// Mouse and timer handlers of two controls (Dirk\dialog.cpp, wip/dialog-r8).
// Both thiscall on the element part (their own fields before it); layouts
// in re/ui_classes.md.
//
// CVScrollBar::OnLButtonDown (0x0041e470; slot 12): (message by value) ret
//   0x14: CControl::OnLButtonDown 0x418380 (this - 0x2b8) with the message;
//   then, if the click (x, y: the words at message + 8 / + 10) is in the
//   track (this - 0x24; PtInRect) and the range (this - 0x14) is not 0: the
//   new position = _ftol((y - track top) / (track bottom - track top) *
//   range) (x87: the int difference divided by the int height, times the
//   int range, each step rounded to double), minus half the page (this -
//   0xc, halved toward zero), at most range - page, at least 0. If that
//   differs from the position (this - 0x10): store it, call the listener
//   (this - 0x240) slot 0 with (the element (vbptr this - 0x2f0), position)
//   and Invalidate. EAX: outside the track 0; else AL = 1 over the last
//   value in EAX (PtInRect's 1, the position, or Invalidate's).
// CAnimatedButton::OnTimer (0x0041b1b0; slot 21): steps the frame countdown
//   (this - 0x18); when it runs out the state (this - 0x1c: 0 idle, 1 hot,
//   2/3 their "returning" variants) drops by 2 (if >= 2) and the frames
//   restart. States 0/2 become 3 when the button (its CButton part, this -
//   0x68) is pressed or checked (CButton::IsPressed 0x41a990, IsChecked
//   0x41a9a0; restart); 1/3 become 2 when it is neither (no restart). A
//   state whose sequence id (this - 0x14 + 4 * state) is -1 falls back by 2
//   (if >= 2; restart). On a restart: a sequence of -1 sets the countdown to
//   -1, otherwise countdown = (CGraphicsData::SequenceFrameCount 0x5fad20(
//   sequence) & 0xffff) - 1 (Dialog::GraphicsData 0x4168c0). Always
//   Invalidate (vbptr this - 0x64); AL = 1 over its EAX.

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

constexpr std::uintptr_t kControlOnLButtonDown = 0x00418380;  // CControl::OnLButtonDown (ours)
constexpr std::uintptr_t kButtonIsPressed = 0x0041a990;       // CButton::IsPressed (ours)
constexpr std::uintptr_t kButtonIsChecked = 0x0041a9a0;       // CButton::IsChecked (ours)
constexpr std::uintptr_t kGraphicsData = 0x004168c0;          // Dialog::GraphicsData (ours)
constexpr std::uintptr_t kSequenceFrameCount = 0x005fad20;    // CGraphicsData::SequenceFrameCount (ours)

constexpr std::int32_t kNoSequence = -1;

namespace sb {  // CVScrollBar, from its element part
constexpr std::int32_t kControl = -0x2b8;
constexpr std::int32_t kVbPtr = -0x2f0;
constexpr std::int32_t kListener = -0x240;
constexpr std::int32_t kTrack = -0x24;
constexpr std::int32_t kRange = -0x14;
constexpr std::int32_t kPosition = -0x10;
constexpr std::int32_t kPage = -0x0c;
}  // namespace sb

namespace ab {  // CAnimatedButton, from its element part
constexpr std::int32_t kButton = -0x68;
constexpr std::int32_t kVbPtr = -0x64;
constexpr std::int32_t kState = -0x1c;
constexpr std::int32_t kCountdown = -0x18;
constexpr std::int32_t kSequences = -0x14;  // 4 sequence ids, by state
}  // namespace ab

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t vbptr) {
    return self + vbptr + (*reinterpret_cast<const std::int32_t* const*>(self + vbptr))[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);

struct Message {  // 20 bytes, passed by value
    std::uint32_t words[5];
};
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

std::uint32_t Invalidate(std::uintptr_t element) {
    return Slot<Thiscall0>(element, ui::element_slot::kInvalidate)(element, nullptr);
}

std::uint32_t WithAlOne(std::uint32_t eax) {
    return (eax & 0xffffff00u) | 1;  // mov al, 1
}

// The CRT's _ftol (0x61fe98): truncation through a 64-bit integer; EAX is
// the low dword (NaN and values outside the int64 range: 0).
std::uint32_t Ftol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

}  // namespace

using OnLButtonDownFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);
using OnTimerFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

// Purpose: Jumps a scroll bar to where its track was clicked and tells the listener.
std::uint32_t __fastcall CVScrollBar_OnLButtonDown(std::uintptr_t self, void*, Message message) {
    const auto x = static_cast<LONG>(message.words[2] & 0xffff);
    const auto y = static_cast<std::int32_t>(message.words[2] >> 16);
    reinterpret_cast<MessageFn>(kControlOnLButtonDown)(self + sb::kControl, nullptr, message);
    const BOOL inside = PtInRect(&Field<RECT>(self, sb::kTrack), POINT{x, y});
    if (inside == 0) {
        return 0;
    }
    const std::int32_t range = Field<std::int32_t>(self, sb::kRange);
    if (range == 0) {
        return WithAlOne(static_cast<std::uint32_t>(inside));
    }
    const RECT& track = Field<RECT>(self, sb::kTrack);
    const std::int32_t page = Field<std::int32_t>(self, sb::kPage);
    // fild / fidiv / fimul: the int operands exactly, each result rounded to double.
    const double fraction = static_cast<double>(y - track.top) / static_cast<double>(track.bottom - track.top);
    auto position = static_cast<std::int32_t>(Ftol(fraction * static_cast<double>(range)));
    position -= page / 2;
    if (position >= range - page) {
        position = range - page;
    }
    if (position <= 0) {
        position = 0;
    }
    if (position == Field<std::int32_t>(self, sb::kPosition)) {
        return WithAlOne(static_cast<std::uint32_t>(position));
    }
    Field<std::int32_t>(self, sb::kPosition) = position;
    if (const std::uintptr_t listener = Field<std::uint32_t>(self, sb::kListener); listener != 0) {
        // The element's null test is dead (it is this scroll bar's own).
        const std::uintptr_t element = self + sb::kVbPtr - 4 == 0 ? 0 : VirtualBase(self, sb::kVbPtr);
        Slot<Thiscall2>(listener, 0)(listener, nullptr, static_cast<std::uint32_t>(element),
                                     static_cast<std::uint32_t>(position));
    }
    return WithAlOne(Invalidate(VirtualBase(self, sb::kVbPtr)));
}

// Purpose: Animates a button: steps its frames and switches between the
// idle and the pressed/hot animation.
std::uint32_t __fastcall CAnimatedButton_OnTimer(std::uintptr_t self, void*) {
    using ByteFn = std::uint8_t(__fastcall*)(std::uintptr_t self, void* edx);
    std::int32_t& state = Field<std::int32_t>(self, ab::kState);
    std::int32_t& countdown = Field<std::int32_t>(self, ab::kCountdown);
    auto sequenceOf = [&](std::int32_t s) {  // the original indexes without a bounds check
        return Field<std::int32_t>(self, static_cast<std::int32_t>(static_cast<std::uint32_t>(ab::kSequences) +
                                                                    4u * static_cast<std::uint32_t>(s)));
    };
    auto pressedOrChecked = [&] {
        const std::uintptr_t button = self + ab::kButton;
        return reinterpret_cast<ByteFn>(kButtonIsPressed)(button, nullptr) != 0 ||
               reinterpret_cast<ByteFn>(kButtonIsChecked)(button, nullptr) != 0;
    };

    bool restart = false;
    countdown = static_cast<std::int32_t>(static_cast<std::uint32_t>(countdown) - 1);
    if (countdown < 0) {
        if (state >= 2) {
            state -= 2;
        }
        restart = true;
    }
    switch (static_cast<std::uint32_t>(state)) {
    case 0:
    case 2:
        if (pressedOrChecked()) {
            state = 3;
            restart = true;
        }
        break;
    case 1:
    case 3:
        if (!pressedOrChecked()) {
            state = 2;
            restart = false;
        }
        break;
    default: break;
    }
    if (sequenceOf(state) == kNoSequence && state >= 2) {
        state -= 2;
        restart = true;
    }
    if (restart) {
        const std::int32_t sequence = sequenceOf(state);
        if (sequence == kNoSequence) {
            countdown = kNoSequence;
        } else {
            using GraphicsDataFn = std::uint32_t(__stdcall*)();
            const std::uint32_t graphics = reinterpret_cast<GraphicsDataFn>(kGraphicsData)();
            const std::uint32_t frames = reinterpret_cast<Thiscall1>(kSequenceFrameCount)(
                graphics, nullptr, static_cast<std::uint32_t>(sequence));
            countdown = static_cast<std::int32_t>((frames & 0xffff) - 1);
        }
    }
    return WithAlOne(Invalidate(VirtualBase(self, ab::kVbPtr)));
}

S3RE_HOOK(0x0041e470, CVScrollBar_OnLButtonDown, OnLButtonDownFn, Original_CVScrollBar_OnLButtonDown,
          CVScrollBar_OnLButtonDown);
S3RE_HOOK(0x0041b1b0, CAnimatedButton_OnTimer, OnTimerFn, Original_CAnimatedButton_OnTimer, CAnimatedButton_OnTimer);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

// The button queries answer 0 or 1 (by the call's hash).
std::uint32_t Effect(const kit::Call& call, const std::uint32_t*) {
    std::uint32_t hash = call.callee * 2654435761u ^ call.self;
    for (std::uint32_t i = 0; i < call.count; ++i) {
        hash = (hash ^ call.args[i]) * 16777619u;
    }
    if (call.callee == kButtonIsPressed || call.callee == kButtonIsChecked) {
        return (hash & 0xffffff00u) | ((hash >> 20) % 3 == 0 ? 1u : 0u);
    }
    return hash ^ (hash >> 7);
}

struct Members {
    OnLButtonDownFn click = &CVScrollBar_OnLButtonDown;
    OnTimerFn timer = &CAnimatedButton_OnTimer;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kControlOnLButtonDown, 5>()
        .This<kButtonIsPressed, 0>()
        .This<kButtonIsChecked, 0>()
        .Std<kGraphicsData, 0>()
        .This<kSequenceFrameCount, 1>();
    auto click = [](OnLButtonDownFn fn) {
        return [fn](std::uintptr_t base, test::Rng& r) {
            Message m{};
            for (auto& w : m.words) {
                w = r.Next32();
            }
            m.words[2] = (static_cast<std::uint32_t>(r.Range(-5, 120)) & 0xffff) |
                         ((static_cast<std::uint32_t>(r.Range(-5, 220)) & 0xffff) << 16);
            return fn(base + kThis, nullptr, m);
        };
    };
    // The track sometimes empty (height 0: a division by zero in x87, inf or
    // NaN), the range and page small or huge, the listener none or a
    // 2-argument stub.
    kit::Compare(
        ctx, "ScrollBarOnLButtonDown", cases,
        [](test::Rng& r, std::uint8_t* b) {
            kit::VbPtr(b, kThis + sb::kVbPtr, 0x600, 0);
            const std::int32_t top = r.Range(-5, 60);
            const std::int32_t track[4] = {r.Range(-5, 20), top, r.Range(20, 120),
                                           r.Chance(1, 10) ? top : top + r.Range(-3, 160)};
            std::memcpy(b + kThis + sb::kTrack, track, sizeof(track));
            auto value = [&] { return static_cast<std::uint32_t>(r.Chance(1, 8) ? r.Next32() : r.Range(-3, 40)); };
            kit::Put(b, kThis + sb::kRange, r.Chance(1, 8) ? 0 : value());
            kit::Put(b, kThis + sb::kPage, value());
            kit::Put(b, kThis + sb::kPosition, r.Below(20));
            if (r.Chance(1, 3)) {
                kit::Put(b, kThis + sb::kListener, 0);
            } else {
                kit::Link(b, kThis + sb::kListener, 0x2400);
            }
            std::uint32_t twoArgs = 0;
            std::memcpy(&twoArgs, b + kit::kVtable + 4 * 11, 4);
            kit::Link(b, 0x2400, 0x2500);
            kit::Put(b, 0x2500, twoArgs);
        },
        click(Original_CVScrollBar_OnLButtonDown), click(ours.click));
    // OnTimer: states -1..5 (out-of-range ones index the sequences without a
    // check: they stay inside the buffer), countdowns around 0, sequence ids
    // -1 or small.
    kit::Compare(
        ctx, "AnimatedButtonOnTimer", cases,
        [](test::Rng& r, std::uint8_t* b) {
            kit::VbPtr(b, kThis + ab::kVbPtr, 0x600, 0);
            kit::Put(b, kThis + ab::kState, static_cast<std::uint32_t>(r.Range(-1, 5)));
            kit::Put(b, kThis + ab::kCountdown,
                     static_cast<std::uint32_t>(r.Chance(1, 10) ? static_cast<std::int32_t>(0x80000000u) : r.Range(-2, 3)));
            for (int k = -1; k < 6; ++k) {
                kit::Put(b, kThis + ab::kSequences + 4 * k, r.Chance(1, 3) ? 0xffffffffu : r.Below(50));
            }
        },
        [](std::uintptr_t base, test::Rng&) { return Original_CAnimatedButton_OnTimer(base + kThis, nullptr); },
        [&](std::uintptr_t base, test::Rng&) { return ours.timer(base + kThis, nullptr); }, 0xffffffffu, &Effect);
}

std::uint32_t __fastcall WrongClick(std::uintptr_t self, void* edx, Message message) {
    Field<std::int32_t>(self, sb::kPage) += 2;  // a page one line taller
    const std::uint32_t result = CVScrollBar_OnLButtonDown(self, edx, message);
    Field<std::int32_t>(self, sb::kPage) -= 2;
    return result;
}
std::uint32_t __fastcall WrongTimer(std::uintptr_t self, void* edx) {
    Field<std::int32_t>(self, ab::kCountdown) -= 1;  // steps twice
    return CAnimatedButton_OnTimer(self, edx);
}

}  // namespace

S3RE_TEST(UiControlInput_MatchOriginal) {
    CompareAll(ctx, Members{}, 1500);
}

S3RE_TEST(UiControlInput_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.click = &WrongClick;
        } else {
            wrong.timer = &WrongTimer;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 300);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
