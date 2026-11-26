// CUpDown (RTTI name, Dirk\dialog.cpp; re/ui_classes.md "CUpDown"), the
// number spinner of the menus (game setup: player count, resources...): a
// CGroupControl with a CStaticText showing the value and two image buttons.
// "this" of the members below is the object's start (the CControl part:
// vbptr at +4, the CScreenElement virtual base behind it).
//
// CUpDown::GetValue (0x00422b80): () -> the value +0x204.
// CUpDown::SetValue (0x00422b90): (value) ret 4: a value outside [min
//   +0x1fc, max +0x200] becomes the bound it passed (below min -> min,
//   above max -> max; with min > max every value below min becomes min,
//   every other max). Unchanged: EAX = the clamped value. Otherwise stored,
//   UpdateText, then the listener +0xb4 (when set) gets slot 0 (the
//   up-down's element: this + vbtable[1] + 4). EAX = the listener's, or
//   UpdateText's without one.
// CUpDown::SetRange (0x00422bf0): (min, max) ret 8: min, then (the value
//   read in between) max stored, then SetValue(value). EAX = its.
// CUpDown::UpdateText (0x00422c20): sprintf(buffer[1024], "%i", value),
//   then string temporaries (string(buffer); a copy; the argument as a
//   copy of the copy, sharing the buffer) passed by value to the text
//   label's CStaticText::SetText 0x41c810 (+0xb8). EAX = SetText's.
// CUpDown::OnButtonClicked (0x00422e50): (button element) ret 4: the
//   buttons' listener. The up button (+0xfc, its element through the vbptr
//   at +0x100): SetValue(value + 1) below max; the down button (+0x17c,
//   vbptr +0x180): SetValue(value - 1) above min; at the bound SetValue
//   (value). Another sender: assert "Invalid sender" (dialog.cpp line 3075).
//   EAX = SetValue's.

#include "game/texts.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
struct CUpDown {
    std::uint32_t vtable;                // 0x000
    const std::int32_t* vbptr;           // 0x004 vbtable: [1] = offset of the CScreenElement part from +4
    std::uint8_t unk008[0xb4 - 0x08];    // the rest of the CGroupControl part
    std::uint32_t listener;              // 0x0b4 value-changed callback object (slot 0 gets the element)
    std::uint8_t text[0x44];             // 0x0b8 CStaticText showing the value
    std::uint32_t upButtonVtable;        // 0x0fc CImageButton "up" (image 0x2000a)
    const std::int32_t* upButtonVbptr;   // 0x100
    std::uint8_t unk104[0x17c - 0x104];
    std::uint32_t downButtonVtable;      // 0x17c CImageButton "down" (image 0x2000b)
    const std::int32_t* downButtonVbptr; // 0x180
    std::uint8_t unk184[0x1fc - 0x184];
    std::int32_t min;                    // 0x1fc
    std::int32_t max;                    // 0x200
    std::int32_t value;                  // 0x204
};
#pragma pack(pop)
static_assert(offsetof(CUpDown, listener) == 0xb4);
static_assert(offsetof(CUpDown, text) == 0xb8);
static_assert(offsetof(CUpDown, upButtonVbptr) == 0x100);
static_assert(offsetof(CUpDown, downButtonVbptr) == 0x180);
static_assert(offsetof(CUpDown, min) == 0x1fc);
static_assert(offsetof(CUpDown, value) == 0x204);

constexpr std::uintptr_t kSetValue = 0x00422b90;          // CUpDown::SetValue (ours)
constexpr std::uintptr_t kUpdateText = 0x00422c20;        // CUpDown::UpdateText (ours)
constexpr std::uintptr_t kSetText = 0x0041c810;           // CStaticText::SetText (ours)
constexpr std::uintptr_t kSprintf = 0x0061f27e;           // the CRT's sprintf
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;  // Win32::ThrowAssertmentFailed (ours)
constexpr std::int32_t kInvalidSenderLine = 3075;         // dialog.cpp line of the assert

constexpr std::size_t kTextBufferSize = 1024;

using Thiscall0 = std::uint32_t(__fastcall*)(void* self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using SetTextFn = std::uint32_t(__fastcall*)(void* self, void* edx, msvc6::String text);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);

// The element reached through a vbptr stored at `vbptrField` (vbptr +
// vbtable[1]).
std::uintptr_t ElementThrough(const std::int32_t* const& vbptrField) {
    return reinterpret_cast<std::uintptr_t>(&vbptrField) + vbptrField[1];
}

}  // namespace

using GetValueFn = std::int32_t(__fastcall*)(CUpDown* self, void* edx);
using SetValueFn = std::uint32_t(__fastcall*)(CUpDown* self, void* edx, std::int32_t value);
using SetRangeFn = std::uint32_t(__fastcall*)(CUpDown* self, void* edx, std::int32_t min, std::int32_t max);
using UpdateTextFn = std::uint32_t(__fastcall*)(CUpDown* self, void* edx);
using OnButtonClickedFn = std::uint32_t(__fastcall*)(CUpDown* self, void* edx, std::uintptr_t button);

std::int32_t __fastcall CUpDown_GetValue(CUpDown* self, void* edx);
std::uint32_t __fastcall CUpDown_SetValue(CUpDown* self, void* edx, std::int32_t value);
std::uint32_t __fastcall CUpDown_SetRange(CUpDown* self, void* edx, std::int32_t min, std::int32_t max);
std::uint32_t __fastcall CUpDown_UpdateText(CUpDown* self, void* edx);
std::uint32_t __fastcall CUpDown_OnButtonClicked(CUpDown* self, void* edx, std::uintptr_t button);

S3RE_HOOK(0x00422b80, CUpDown_GetValue, GetValueFn, Original_CUpDown_GetValue, CUpDown_GetValue);
S3RE_HOOK(0x00422b90, CUpDown_SetValue, SetValueFn, Original_CUpDown_SetValue, CUpDown_SetValue);
S3RE_HOOK(0x00422bf0, CUpDown_SetRange, SetRangeFn, Original_CUpDown_SetRange, CUpDown_SetRange);
S3RE_HOOK(0x00422c20, CUpDown_UpdateText, UpdateTextFn, Original_CUpDown_UpdateText, CUpDown_UpdateText);
S3RE_HOOK(0x00422e50, CUpDown_OnButtonClicked, OnButtonClickedFn, Original_CUpDown_OnButtonClicked,
          CUpDown_OnButtonClicked);

// Purpose: Returns the number a spinner shows.
std::int32_t __fastcall CUpDown_GetValue(CUpDown* self, void* /*edx*/) {
    return self->value;
}

// Purpose: Sets the number a spinner shows (kept within its range); on a
// change the text is updated and the owner notified.
std::uint32_t __fastcall CUpDown_SetValue(CUpDown* self, void* /*edx*/, std::int32_t value) {
    if (value < self->min) {
        value = self->min;
    } else if (value > self->max) {
        value = self->max;
    }
    if (value == self->value) {
        return static_cast<std::uint32_t>(value);
    }
    self->value = value;
    const std::uint32_t eax = reinterpret_cast<Thiscall0>(kUpdateText)(self, nullptr);
    const std::uint32_t listener = self->listener;
    if (listener == 0) {
        return eax;
    }
    // The original passes 0 for a null up-down (dead: this is never null).
    const std::uintptr_t element = ElementThrough(self->vbptr);
    const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(listener);
    return reinterpret_cast<Thiscall1>(vtable[0])(reinterpret_cast<void*>(listener), nullptr,
                                                 static_cast<std::uint32_t>(element));
}

// Purpose: Sets a spinner's range and re-clamps its value.
std::uint32_t __fastcall CUpDown_SetRange(CUpDown* self, void* /*edx*/, std::int32_t min, std::int32_t max) {
    self->min = min;
    const std::int32_t value = self->value;
    self->max = max;
    return reinterpret_cast<Thiscall1>(kSetValue)(self, nullptr, static_cast<std::uint32_t>(value));
}

// Purpose: Shows a spinner's value as its text.
std::uint32_t __fastcall CUpDown_UpdateText(CUpDown* self, void* /*edx*/) {
    char buffer[kTextBufferSize];
    reinterpret_cast<SprintfFn>(kSprintf)(buffer, texts::kIntFormat, self->value);
    // The argument string first (its _Tidy(false)), then string(buffer),
    // a copy of it, and the argument assigned from the copy (0x422c57..0x422cf8).
    msvc6::String argument{};
    msvc6::String_Tidy(&argument, nullptr, 0);
    msvc6::String text{};
    msvc6::String_AssignChars(&text, nullptr, buffer, msvc6::detail::Length(buffer));
    msvc6::String copy{};
    msvc6::String_Tidy(&copy, nullptr, 0);
    msvc6::String_AssignSubstr(&copy, nullptr, &text, 0, 0xFFFFFFFFu);
    msvc6::String_AssignSubstr(&argument, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_TidyFree(&copy, nullptr);  // the temporaries' destructors (inlined _Tidy(true))
    msvc6::String_TidyFree(&text, nullptr);
    return reinterpret_cast<SetTextFn>(kSetText)(self->text, nullptr, argument);
}

// Purpose: Reacts to a click on a spinner's up or down button: one step
// up or down within the range.
std::uint32_t __fastcall CUpDown_OnButtonClicked(CUpDown* self, void* /*edx*/, std::uintptr_t button) {
    std::int32_t value = self->value;
    if (button == ElementThrough(self->upButtonVbptr)) {
        if (value < self->max) {
            ++value;
        }
    } else if (button == ElementThrough(self->downButtonVbptr)) {
        if (value > self->min) {
            --value;
        }
    } else {
        reinterpret_cast<AssertFn>(kThrowAssertmentFailed)(texts::kInvalidSender, texts::kDialogFile,
                                                           kInvalidSenderLine);
    }
    return reinterpret_cast<Thiscall1>(kSetValue)(self, nullptr, static_cast<std::uint32_t>(value));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One random CUpDown buffer (with guard bytes), set up alike before each
// version runs (so pointers into it need no translation); the vbtables are
// small tables after the object, the listener a fake object whose slot 0
// records its call. GetValue/SetValue/SetRange/OnButtonClicked run with
// UpdateText, SetValue (except when it is the one tested) and the assert
// stubbed (recorded, EAX fixed by the call; the assert returns here);
// UpdateText runs with SetText replaced by a recorder that keeps the text
// and count byte it got (then destroys the argument like SetText). Values
// and bounds come from a set with the edge cases. Compared: EAX, the
// buffer, the stub calls, the listener calls.

namespace {

constexpr std::size_t kGuard = 16;
constexpr std::size_t kTables = kGuard + sizeof(CUpDown);  // three 8-byte vbtables after the object
constexpr std::size_t kBuffer = kTables + 3 * 8 + kGuard;

struct Setup {
    std::uint8_t bytes[kBuffer];
    std::uintptr_t fakeVtable[1];
    std::uintptr_t fakeListener[2];  // {vtable, unused}
};
Setup g_setup;

CUpDown* Object() {
    return reinterpret_cast<CUpDown*>(g_setup.bytes + kGuard);
}

std::uint32_t Relative(std::uintptr_t value) {
    return static_cast<std::uint32_t>(value - reinterpret_cast<std::uintptr_t>(Object()));
}

struct Call {
    std::uint32_t kind;  // 0 listener, 1 assert, 2 SetText
    std::uint32_t self;
    std::uint32_t args[3];
    char text[16];
    bool operator==(const Call& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};
std::vector<Call>* g_calls = nullptr;

std::uint32_t __fastcall ListenerSlot0(void* self, void*, std::uint32_t element) {
    g_calls->push_back(Call{0, Relative(reinterpret_cast<std::uintptr_t>(self)), {Relative(element)}, {}});
    return 0x11570000u + static_cast<std::uint32_t>(g_calls->size());
}

void __fastcall AssertRecorder(const char* expression, const char* file, std::int32_t line) {
    g_calls->push_back(Call{1,
                            0,
                            {static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(expression)),
                             static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(file)),
                             static_cast<std::uint32_t>(line)},
                            {}});
}

std::uint32_t __fastcall SetTextRecorder(void* self, void*, msvc6::String text) {
    Call call{2, Relative(reinterpret_cast<std::uintptr_t>(self)), {text.len, text.res, 0xffff}, {}};
    if (text.ptr != nullptr) {
        call.args[2] = static_cast<std::uint8_t>(text.ptr[-1]);
        std::memcpy(call.text, text.ptr, text.len < sizeof(call.text) ? text.len + 1 : sizeof(call.text));
    }
    g_calls->push_back(call);
    msvc6::String_Destroy(&text, nullptr);
    return 0x5e77e000u + static_cast<std::uint32_t>(g_calls->size());
}

std::int32_t PickValue(test::Rng& rng) {
    static constexpr std::int32_t kValues[] = {0, 1, -1, 2, 5, 10, 0x7fffffff, -0x7fffffff - 1, 0x7ffffffe};
    return rng.Chance(1, 3) ? static_cast<std::int32_t>(rng.Next32())
                            : kValues[rng.Below(sizeof(kValues) / sizeof(kValues[0]))];
}

// Random bytes, the three vbptrs pointing at their tables, the listener
// (null or the fake), min/max/value.
void Prepare(test::Rng& rng) {
    rng.Fill(g_setup.bytes, sizeof(g_setup.bytes));
    CUpDown* self = Object();
    auto* tables = reinterpret_cast<std::int32_t*>(g_setup.bytes + kTables);
    self->vbptr = tables;
    self->upButtonVbptr = tables + 2;
    self->downButtonVbptr = tables + 4;
    for (int k = 0; k < 3; ++k) {
        tables[2 * k + 1] = rng.Range(-0x40, 0x200);
    }
    if (rng.Chance(1, 4)) {
        tables[3] = tables[5] + 0x80;  // both buttons' elements at the same place (the up button wins)
    }
    g_setup.fakeVtable[0] = reinterpret_cast<std::uintptr_t>(&ListenerSlot0);
    g_setup.fakeListener[0] = reinterpret_cast<std::uintptr_t>(g_setup.fakeVtable);
    self->listener =
        rng.Chance(1, 3) ? 0 : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_setup.fakeListener));
    self->min = PickValue(rng);
    self->max = rng.Chance(1, 2) ? self->min + static_cast<std::int32_t>(rng.Below(20)) : PickValue(rng);
    self->value = rng.Chance(1, 2) ? self->min + static_cast<std::int32_t>(rng.Below(20)) - 2 : PickValue(rng);
}

struct Result {
    std::uint32_t eax = 0;
    std::vector<test::StubCall> stubCalls;
    std::vector<Call> calls;
    std::uint8_t bytes[kBuffer] = {};
};

// Prepares the buffer from `seed`, runs fn(object) with the stubs and
// keeps the results.
template <typename Fn>
void RunOne(test::CalleeStubs* stubs, std::uint64_t seed, Fn fn, Result& result) {
    test::Rng rng(seed);
    Prepare(rng);
    g_calls = &result.calls;
    if (stubs != nullptr) {
        stubs->SetWorld(reinterpret_cast<CGameData*>(Object()));
    }
    result.eax = fn(Object());
    if (stubs != nullptr) {
        result.stubCalls = stubs->Take();
    }
    g_calls = nullptr;
    std::memcpy(result.bytes, g_setup.bytes, kBuffer);
}

bool SameResults(test::Context& ctx, const Result& a, const Result& b) {
    return S3RE_EXPECT_EQ(a.eax, b.eax) && S3RE_EXPECT(a.stubCalls == b.stubCalls) && S3RE_EXPECT(a.calls == b.calls) &&
           S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, kBuffer);
}

struct Members {
    GetValueFn getValue = &CUpDown_GetValue;
    SetValueFn setValue = &CUpDown_SetValue;
    SetRangeFn setRange = &CUpDown_SetRange;
    OnButtonClickedFn onButtonClicked = &CUpDown_OnButtonClicked;
};

// GetValue, SetValue, SetRange, OnButtonClicked.
void CompareMembers(test::Context& ctx, const Members& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const int which = static_cast<int>(ctx.rng().Below(4));
        const std::int32_t arg0 = PickValue(ctx.rng());
        const std::int32_t arg1 = PickValue(ctx.rng());
        const int sender = static_cast<int>(ctx.rng().Below(5));  // 0/1 up, 2/3 down, 4 other
        auto call = [&](bool original, CUpDown* self) -> std::uint32_t {
            switch (which) {
            case 0:
                return static_cast<std::uint32_t>(original ? Original_CUpDown_GetValue(self, nullptr)
                                                           : ours.getValue(self, nullptr));
            case 1:
                return original ? Original_CUpDown_SetValue(self, nullptr, arg0) : ours.setValue(self, nullptr, arg0);
            case 2:
                return original ? Original_CUpDown_SetRange(self, nullptr, arg0, arg1)
                                : ours.setRange(self, nullptr, arg0, arg1);
            default: {
                std::uintptr_t button = ElementThrough(self->vbptr) + 1;  // not a button
                if (sender < 2) {
                    button = ElementThrough(self->upButtonVbptr);
                } else if (sender < 4) {
                    button = ElementThrough(self->downButtonVbptr);
                }
                return original ? Original_CUpDown_OnButtonClicked(self, nullptr, button)
                                : ours.onButtonClicked(self, nullptr, button);
            }
            }
        };
        Result a, b;
        {
            test::CalleeStubs stubs;
            stubs.Install<0, kUpdateText>(
                [](CGameData*, std::uint32_t self, const std::uint32_t*) { return self ^ 0x7e57u; });
            if (which != 1) {
                stubs.Install<1, kSetValue>(
                    [](CGameData*, std::uint32_t self, const std::uint32_t* args) { return self + args[0] * 3; });
            }
            test::ScopedJump assertJump(kThrowAssertmentFailed, reinterpret_cast<const void*>(&AssertRecorder));
            RunOne(&stubs, seed, [&](CUpDown* self) { return call(true, self); }, a);
            RunOne(&stubs, seed, [&](CUpDown* self) { return call(false, self); }, b);
        }
        if (!SameResults(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: member %d, sender %d", i, which, sender);
            return;
        }
    }
}

// Wrong: a value below the minimum is kept.
std::uint32_t __fastcall WrongSetValue(CUpDown* self, void* edx, std::int32_t value) {
    if (value < self->min) {
        self->value = value;
        return static_cast<std::uint32_t>(value);
    }
    return CUpDown_SetValue(self, edx, value);
}

void CompareUpdateText(test::Context& ctx, UpdateTextFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        Result a, b;
        {
            test::ScopedJump jump(kSetText, reinterpret_cast<const void*>(&SetTextRecorder));
            RunOne(nullptr, seed, [&](CUpDown* self) { return Original_CUpDown_UpdateText(self, nullptr); }, a);
            RunOne(nullptr, seed, [&](CUpDown* self) { return candidate(self, nullptr); }, b);
        }
        if (!SameResults(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: value %d", i, reinterpret_cast<CUpDown*>(a.bytes + kGuard)->value);
            return;
        }
    }
}

// Wrong: the value formatted as unsigned (differs for negative values).
std::uint32_t __fastcall WrongUpdateText(CUpDown* self, void*) {
    char buffer[kTextBufferSize];
    reinterpret_cast<SprintfFn>(kSprintf)(buffer, "%u", self->value);
    msvc6::String argument{};
    msvc6::String_AssignChars(&argument, nullptr, buffer, msvc6::detail::Length(buffer));
    return reinterpret_cast<SetTextFn>(kSetText)(self->text, nullptr, argument);
}

}  // namespace

S3RE_TEST(CUpDown_Members_MatchOriginal) {
    CompareMembers(ctx, Members{}, 20000);
}
S3RE_TEST(CUpDown_Members_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Members wrong;
    wrong.setValue = &WrongSetValue;
    CompareMembers(probe, wrong, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CUpDown_UpdateText_MatchesOriginal) {
    CompareUpdateText(ctx, &CUpDown_UpdateText, 3000);
}
S3RE_TEST(CUpDown_UpdateText_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUpdateText(probe, &WrongUpdateText, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
