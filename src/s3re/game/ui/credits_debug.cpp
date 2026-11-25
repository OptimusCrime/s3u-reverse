// The credits screen's debug reports and its timer (code after the
// computer players in S3.EXE, 0x59e6f0..0x59f2a0; the credits' own source
// files Credits\cmy_*.cpp; session 56). The reports go to
// OutputDebugStringA (S3.EXE's import), which a release game shows nowhere.
//
//   0x0059e6f0 Credits::DebugPrint(text) fastcall: OutputDebugStringA(text).
//   0x0059e700 Credits::DebugPrintLine(text) fastcall: the text, then "\n".
//   0x0059e720 Credits::DebugPrint2(a, b) fastcall: DebugPrint(a), (b).
//   0x0059e730 Credits::DebugPrintFileLine(file, line) fastcall: sprintf
//              (0x61f27e) "%s %d" into a 128-byte stack buffer, then
//              DebugPrintLine.
//   0x0059e760 Credits::ReportCaughtUnknown(message, file; line) ret 4:
//              "catched (...?): ", the message, DebugPrintFileLine.
//   0x0059e790 Credits::ReportCaughtException(exception, message; file,
//              line) ret 8: "catched cmy: ", the message, ": ", the empty
//              0x59f200 on the exception (G: its description, compiled
//              out), DebugPrintFileLine.
//   0x0059e7d0 Credits::ReportCreatingException(message, file; line) ret 4:
//              DebugPrint2("creating exception: ", message),
//              DebugPrintFileLine.
// EAX of these is not used by their callers (asm_tools eax-use).
//
// cmy::impuls::Timer (thiscall; 14 bytes: +0 owner, +4 interval, +8 the
// next time, +0xc catch-up flag, +0xd clamp flag):
//   0x0059f210 Timer(owner) ret 4: +0 = owner, both flags 0; EAX = this.
//   0x0059f230 Fire(now) ret 4: owner->vf0(now) (the owner's only virtual),
//              then next += interval; EAX = interval.
//   0x0059f250 Update(now) ret 4: nothing while next > now (signed); with
//              the catch-up flag Fire until next > now, else Fire once and
//              with the clamp flag pull a next still before now up to now.
//              (EAX is not used by its caller.)
//   0x0059f2a0 Restart(now) ret 4: next = now + interval; EAX = next.

#include "game/texts.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kDebugPrint = 0x0059e6f0;
constexpr std::uintptr_t kDebugPrintLine = 0x0059e700;
constexpr std::uintptr_t kDebugPrint2 = 0x0059e720;
constexpr std::uintptr_t kDebugPrintFileLine = 0x0059e730;
constexpr std::uintptr_t kDescribeException = 0x0059f200;  // empty (NoOp)
constexpr std::uintptr_t kTimerFire = 0x0059f230;
constexpr std::uintptr_t kSprintf = 0x0061f27e;  // the CRT's sprintf
constexpr std::size_t kFileLineBuffer = 128;

using PrintFn = void(__fastcall*)(const char* text);
using Print2Fn = void(__fastcall*)(const char* a, const char* b);
using FileLineFn = void(__fastcall*)(const char* file, std::uint32_t line);

void Print(const char* text) { reinterpret_cast<PrintFn>(kDebugPrint)(text); }
void PrintFileLine(const char* file, std::uint32_t line) { reinterpret_cast<FileLineFn>(kDebugPrintFileLine)(file, line); }

}  // namespace

namespace cmy {
#pragma pack(push, 1)
// cmy::impuls::Timer (our name; embedded at +0x18 of the credits' Taktbar).
struct Timer {
    void* owner;            // 0x0 an object with one virtual, called on each tick (vf0(now))
    std::int32_t interval;  // 0x4
    std::int32_t next;      // 0x8 the next time it fires
    std::uint8_t catch_up;  // 0xc Update fires until next is past now
    std::uint8_t clamp;     // 0xd Update pulls a late next up to now
};
#pragma pack(pop)
static_assert(offsetof(Timer, next) == 8);
static_assert(offsetof(Timer, clamp) == 0xd);
}  // namespace cmy

using ReportFn = void(__fastcall*)(const char* message, const char* file, std::uint32_t line);
using ReportExceptionFn = void(__fastcall*)(void* exception, const char* message, const char* file,
                                            std::uint32_t line);
using TimerConstructFn = cmy::Timer*(__fastcall*)(cmy::Timer* self, void* edx, void* owner);
using TimerNowFn = std::uint32_t(__fastcall*)(cmy::Timer* self, void* edx, std::int32_t now);

void __fastcall Credits_DebugPrint(const char* text);
void __fastcall Credits_DebugPrintLine(const char* text);
void __fastcall Credits_DebugPrint2(const char* a, const char* b);
void __fastcall Credits_DebugPrintFileLine(const char* file, std::uint32_t line);
void __fastcall Credits_ReportCaughtUnknown(const char* message, const char* file, std::uint32_t line);
void __fastcall Credits_ReportCaughtException(void* exception, const char* message, const char* file,
                                              std::uint32_t line);
void __fastcall Credits_ReportCreatingException(const char* message, const char* file, std::uint32_t line);
cmy::Timer* __fastcall cmy_impuls_Timer_Construct(cmy::Timer* self, void* edx, void* owner);
std::uint32_t __fastcall cmy_impuls_Timer_Fire(cmy::Timer* self, void* edx, std::int32_t now);
std::uint32_t __fastcall cmy_impuls_Timer_Update(cmy::Timer* self, void* edx, std::int32_t now);
std::uint32_t __fastcall cmy_impuls_Timer_Restart(cmy::Timer* self, void* edx, std::int32_t now);

S3RE_HOOK(0x0059e6f0, Credits_DebugPrint, PrintFn, Original_Credits_DebugPrint, Credits_DebugPrint);
S3RE_HOOK(0x0059e700, Credits_DebugPrintLine, PrintFn, Original_Credits_DebugPrintLine, Credits_DebugPrintLine);
S3RE_HOOK(0x0059e720, Credits_DebugPrint2, Print2Fn, Original_Credits_DebugPrint2, Credits_DebugPrint2);
S3RE_HOOK(0x0059e730, Credits_DebugPrintFileLine, FileLineFn, Original_Credits_DebugPrintFileLine,
          Credits_DebugPrintFileLine);
S3RE_HOOK(0x0059e760, Credits_ReportCaughtUnknown, ReportFn, Original_Credits_ReportCaughtUnknown,
          Credits_ReportCaughtUnknown);
S3RE_HOOK(0x0059e790, Credits_ReportCaughtException, ReportExceptionFn, Original_Credits_ReportCaughtException,
          Credits_ReportCaughtException);
S3RE_HOOK(0x0059e7d0, Credits_ReportCreatingException, ReportFn, Original_Credits_ReportCreatingException,
          Credits_ReportCreatingException);
S3RE_HOOK(0x0059f210, cmy_impuls_Timer_Construct, TimerConstructFn, Original_cmy_impuls_Timer_Construct,
          cmy_impuls_Timer_Construct);
S3RE_HOOK(0x0059f230, cmy_impuls_Timer_Fire, TimerNowFn, Original_cmy_impuls_Timer_Fire, cmy_impuls_Timer_Fire);
S3RE_HOOK(0x0059f250, cmy_impuls_Timer_Update, TimerNowFn, Original_cmy_impuls_Timer_Update, cmy_impuls_Timer_Update);
S3RE_HOOK(0x0059f2a0, cmy_impuls_Timer_Restart, TimerNowFn, Original_cmy_impuls_Timer_Restart,
          cmy_impuls_Timer_Restart);

// Purpose: The credits code's debug output of a text.
void __fastcall Credits_DebugPrint(const char* text) { g_importOutputDebugStringA.get()(text); }

// Purpose: The credits code's debug output of a text and a line break.
void __fastcall Credits_DebugPrintLine(const char* text) {
    g_importOutputDebugStringA.get()(text);
    g_importOutputDebugStringA.get()(texts::kNewline.c_str());
}

// Purpose: The credits code's debug output of two texts.
void __fastcall Credits_DebugPrint2(const char* a, const char* b) {
    Print(a);
    Print(b);
}

// Purpose: The credits code's debug output of a source file and line.
void __fastcall Credits_DebugPrintFileLine(const char* file, std::uint32_t line) {
    char text[kFileLineBuffer];
    reinterpret_cast<int(__cdecl*)(char*, const char*, ...)>(kSprintf)(text, texts::kFileLineFormat.c_str(), file, line);
    reinterpret_cast<PrintFn>(kDebugPrintLine)(text);
}

// Purpose: The credits code's report of an unknown exception it caught.
void __fastcall Credits_ReportCaughtUnknown(const char* message, const char* file, std::uint32_t line) {
    Print(texts::kCaughtUnknown.c_str());
    Print(message);
    PrintFileLine(file, line);
}

// Purpose: The credits code's report of one of its own exceptions it caught.
void __fastcall Credits_ReportCaughtException(void* exception, const char* message, const char* file,
                                              std::uint32_t line) {
    Print(texts::kCaughtCmy.c_str());
    Print(message);
    Print(texts::kColonSpace.c_str());
    reinterpret_cast<void(__fastcall*)(void*)>(kDescribeException)(exception);
    PrintFileLine(file, line);
}

// Purpose: The credits code's report of an exception it is about to throw.
void __fastcall Credits_ReportCreatingException(const char* message, const char* file, std::uint32_t line) {
    reinterpret_cast<Print2Fn>(kDebugPrint2)(texts::kCreatingException.c_str(), message);
    PrintFileLine(file, line);
}

// Purpose: Constructor of the credits' tick timer.
cmy::Timer* __fastcall cmy_impuls_Timer_Construct(cmy::Timer* self, void* /*edx*/, void* owner) {
    self->owner = owner;
    self->catch_up = 0;
    self->clamp = 0;
    return self;
}

// Purpose: One tick of the credits' timer: tells its owner and schedules
// the next tick.
std::uint32_t __fastcall cmy_impuls_Timer_Fire(cmy::Timer* self, void* /*edx*/, std::int32_t now) {
    using TickFn = void(__fastcall*)(void* owner, void* edx, std::int32_t now);
    void* owner = self->owner;
    reinterpret_cast<TickFn>((*static_cast<void***>(owner))[0])(owner, nullptr, now);
    self->next += self->interval;
    return static_cast<std::uint32_t>(self->interval);
}

// Purpose: Lets the credits' timer tick when its time has come (catching
// up on missed ticks, or skipping them).
std::uint32_t __fastcall cmy_impuls_Timer_Update(cmy::Timer* self, void* /*edx*/, std::int32_t now) {
    if (self->next > now) {
        return 0;  // (EAX is not used)
    }
    const auto fire = reinterpret_cast<TimerNowFn>(kTimerFire);
    if (self->catch_up != 0) {
        do {
            fire(self, nullptr, now);
        } while (self->next <= now);
        return 0;
    }
    fire(self, nullptr, now);
    if (self->clamp != 0 && self->next < now) {
        self->next = now;
    }
    return 0;
}

// Purpose: Restarts the credits' timer: the next tick one interval from now.
std::uint32_t __fastcall cmy_impuls_Timer_Restart(cmy::Timer* self, void* /*edx*/, std::int32_t now) {
    self->next = self->interval + now;
    return static_cast<std::uint32_t>(self->next);
}

// ---------------------------------------------------------------------------
// Tests: OutputDebugStringA's import goes to a recorder (the texts are
// compared); the timer's owner is a fake object whose virtual records the
// calls.
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string>* g_creditsTexts = nullptr;
void __stdcall RecordCreditsText(const char* text) {
    if (g_creditsTexts != nullptr) {
        g_creditsTexts->push_back(text);
    }
}

struct PrintFunctions {
    PrintFn print, line;
    Print2Fn print2;
    FileLineFn fileLine;
    ReportFn unknown, creating;
    ReportExceptionFn exception;
};

void ComparePrints(test::Context& ctx, const PrintFunctions& candidate, int cases) {
    const PrintFunctions original{Original_Credits_DebugPrint,         Original_Credits_DebugPrintLine,
                                  Original_Credits_DebugPrint2,        Original_Credits_DebugPrintFileLine,
                                  Original_Credits_ReportCaughtUnknown, Original_Credits_ReportCreatingException,
                                  Original_Credits_ReportCaughtException};
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordCreditsText);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));
    const char* const words[] = {"", "OutOfMemory", "file klemmt(fehlt?)", "E:\\Siedler3\\Credits\\cmy_x.cpp", "a"};
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const char* a = words[rng.Below(5)];
        const char* b = words[rng.Below(5)];
        const std::uint32_t line = rng.Chance(1, 4) ? rng.Next32() : rng.Below(3000);
        const int op = static_cast<int>(rng.Below(7));
        std::vector<std::string> textsA, textsB;
        const auto run = [&](const PrintFunctions& f) {
            switch (op) {
            case 0: f.print(a); break;
            case 1: f.line(a); break;
            case 2: f.print2(a, b); break;
            case 3: f.fileLine(a, line); break;
            case 4: f.unknown(a, b, line); break;
            case 5: f.creating(a, b, line); break;
            default: f.exception(&textsA, a, b, line); break;
            }
        };
        g_creditsTexts = &textsA;
        run(original);
        g_creditsTexts = &textsB;
        run(candidate);
        g_creditsTexts = nullptr;
        if (!S3RE_EXPECT(textsA == textsB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d (%u vs %u texts)", i, op, static_cast<unsigned>(textsA.size()),
                     static_cast<unsigned>(textsB.size()));
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
}

void __fastcall WrongFileLine(const char* file, std::uint32_t line) {
    // Wrong: the line number off by one.
    Credits_DebugPrintFileLine(file, line + 1);
}

// The fake owner: its one virtual records `now`.
struct FakeOwner {
    void* const* vtable;
    std::vector<std::int32_t>* calls;
};
void __fastcall FakeTick(FakeOwner* self, void* /*edx*/, std::int32_t now) { self->calls->push_back(now); }
void* const kFakeOwnerVtable[1] = {reinterpret_cast<void*>(&FakeTick)};

struct TimerFunctions {
    TimerConstructFn construct;
    TimerNowFn fire, update, restart;
};

void CompareTimer(test::Context& ctx, const TimerFunctions& candidate, int cases) {
    const TimerFunctions original{Original_cmy_impuls_Timer_Construct, Original_cmy_impuls_Timer_Fire,
                                  Original_cmy_impuls_Timer_Update, Original_cmy_impuls_Timer_Restart};
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        std::vector<std::int32_t> callsA, callsB;
        FakeOwner ownerA{kFakeOwnerVtable, &callsA}, ownerB{kFakeOwnerVtable, &callsB};
        cmy::Timer a{}, b{};
        rng.Fill(&a, sizeof(a));
        a.interval = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Range(-3, 0) : rng.Range(1, 40));
        a.catch_up = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        a.clamp = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        const std::int32_t now = rng.Range(-1000, 1000);
        a.next = now + rng.Range(-200, 50);
        if (a.catch_up != 0 && a.interval <= 0) {
            a.interval = 1;  // (the original would loop forever)
        }
        b = a;
        a.owner = &ownerA;
        b.owner = &ownerB;
        const int op = static_cast<int>(rng.Below(4));
        std::uint32_t ra = 0, rb = 0;
        switch (op) {
        case 0:
            ra = reinterpret_cast<std::uint32_t>(original.construct(&a, nullptr, &ownerA)) - reinterpret_cast<std::uint32_t>(&a);
            rb = reinterpret_cast<std::uint32_t>(candidate.construct(&b, nullptr, &ownerB)) - reinterpret_cast<std::uint32_t>(&b);
            break;
        case 1:
            ra = original.fire(&a, nullptr, now);
            rb = candidate.fire(&b, nullptr, now);
            break;
        case 2:
            original.update(&a, nullptr, now);  // (EAX is not used)
            candidate.update(&b, nullptr, now);
            break;
        default:
            ra = original.restart(&a, nullptr, now);
            rb = candidate.restart(&b, nullptr, now);
            break;
        }
        a.owner = nullptr;
        b.owner = nullptr;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        same &= S3RE_EXPECT(callsA == callsB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
}

std::uint32_t __fastcall WrongUpdate(cmy::Timer* self, void* edx, std::int32_t now) {
    // Wrong: no clamping.
    const std::uint8_t clamp = self->clamp;
    self->clamp = 0;
    const std::uint32_t result = cmy_impuls_Timer_Update(self, edx, now);
    self->clamp = clamp;
    return result;
}

}  // namespace

S3RE_TEST(CreditsDebug_MatchOriginal) {
    ComparePrints(ctx,
                  {&Credits_DebugPrint, &Credits_DebugPrintLine, &Credits_DebugPrint2, &Credits_DebugPrintFileLine,
                   &Credits_ReportCaughtUnknown, &Credits_ReportCreatingException, &Credits_ReportCaughtException},
                  2000);
    CompareTimer(ctx, {&cmy_impuls_Timer_Construct, &cmy_impuls_Timer_Fire, &cmy_impuls_Timer_Update,
                       &cmy_impuls_Timer_Restart},
                 4000);
}

S3RE_TEST(CreditsDebug_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        ComparePrints(quiet,
                      {&Credits_DebugPrint, &Credits_DebugPrintLine, &Credits_DebugPrint2, &WrongFileLine,
                       &Credits_ReportCaughtUnknown, &Credits_ReportCreatingException, &Credits_ReportCaughtException},
                      2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareTimer(quiet, {&cmy_impuls_Timer_Construct, &cmy_impuls_Timer_Fire, &WrongUpdate,
                             &cmy_impuls_Timer_Restart},
                     4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
