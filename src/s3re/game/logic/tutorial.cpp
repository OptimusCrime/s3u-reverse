// The tutorial's texts (Volker\LogicInit.cpp; re/logic_thread.md
// "Tutorial"; wip/logic-r8).
//
// CLogicThread::AdvanceTutorialStep (0x0053f950): () thiscall, EAX not used
//   (UpdateTutorial returns it to Step, which ignores it). Finds the
//   current step (world tutorial_step) in g_tutorialSteps (0x72faf0; no end
//   marker), sets the next step, StartTutorialWait 0x515af0, takes the
//   step's text from the graphics data (TextAddress(id, -1)), makes a
//   std::string of it (empty when the text is null; otherwise through two
//   temporaries as usual), widens it (0x60b840) and shows it with
//   ShowTutorialText(wide, kind); then the wide string is destroyed and the
//   string erased and freed (all inlined in the original).
// CLogicThread::ShowTutorialText (0x00540400): (const wstring& text, kind)
//   thiscall, ret 8, EAX not used. ClearMessages, then cuts the text into
//   lines of at most 75 characters: a line that doesn't reach the end of
//   the text ends after the last space before character 75 (the search
//   goes back without a limit, as in the original). Only the first four
//   lines are shown (ShowMessage with a wstring(text, pos, n) 0x541410):
//   the first with `kind`, the others with 255; with kind 253 the line that
//   reaches the end of the text gets 252 instead. The text's length is
//   re-read after each line. The allocator byte of the line strings is an
//   uninitialized stack byte in the original (never copied by ShowMessage),
//   ours is 0.

#include "game/framework/exception_strings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

namespace {

using msvc6::String;
using msvc6::WString;

constexpr std::uintptr_t kClearMessages = 0x0051e650;      // CLogicThread::ClearMessages
constexpr std::uintptr_t kShowMessage = 0x0051e440;        // CLogicThread::ShowMessage(wstring, kind)
constexpr std::uintptr_t kSubstrConstruct = 0x00541410;    // wstring(const wstring&, pos, n, const allocator&)
constexpr std::uintptr_t kStartTutorialWait = 0x00515af0;  // CGameData::StartTutorialWait
constexpr std::uintptr_t kTextAddress = 0x005faaa0;        // CGraphicsData::TextAddress(id, language)
constexpr std::uintptr_t kWiden = 0x0060b840;              // NarrowToWideString (fastcall wide out, narrow)
constexpr std::uintptr_t kShowTutorialText = 0x00540400;

constexpr std::uint32_t kCurrentLanguage = 0xffffffff;
constexpr std::uint32_t kLineCharacters = 75;
constexpr std::int32_t kShownLines = 4;
constexpr std::uint16_t kSpace = 0x20;
constexpr std::int32_t kKindTutorial = 253;   // a text that waits for the player (probably)
constexpr std::int32_t kKindLastLine = 252;   // its last line
constexpr std::int32_t kKindFollowLine = 255;  // the second to fourth line of a text

using ThreadFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);
using ShowMessageFn = void(__fastcall*)(CLogicThread* self, void* edx, WString text, std::int32_t kind);
using AdvanceFn = void(__fastcall*)(CLogicThread* self, void* edx);
using ShowTutorialTextFn = void(__fastcall*)(CLogicThread* self, void* edx, const WString* text, std::int32_t kind);

// The inlined ~wstring: one reference less of a shared buffer, else freed.
void DestroyWide(WString* s) {
    if (s->ptr != nullptr) {
        std::uint8_t& count = reinterpret_cast<std::uint8_t*>(s->ptr)[-1];
        if (count != 0 && count != 0xff) {
            --count;
        } else {
            msvc6::GameOperatorDelete(s->ptr - 1);
        }
    }
    s->ptr = nullptr;
    s->len = 0;
    s->res = 0;
}

}  // namespace

void __fastcall CLogicThread_ShowTutorialText(CLogicThread* self, void* edx, const WString* text, std::int32_t kind);
void __fastcall CLogicThread_AdvanceTutorialStep(CLogicThread* self, void* edx);

S3RE_HOOK(0x00540400, CLogicThread_ShowTutorialText, ShowTutorialTextFn, Original_CLogicThread_ShowTutorialText,
          CLogicThread_ShowTutorialText);
S3RE_HOOK(0x0053f950, CLogicThread_AdvanceTutorialStep, AdvanceFn, Original_CLogicThread_AdvanceTutorialStep,
          CLogicThread_AdvanceTutorialStep);

// Purpose: Shows a tutorial text in the message area: cut into lines of at
// most 75 characters at spaces, the first four lines shown.
void __fastcall CLogicThread_ShowTutorialText(CLogicThread* self, void* /*edx*/, const WString* text,
                                              std::int32_t kind) {
    reinterpret_cast<ThreadFn>(kClearMessages)(self, nullptr);
    std::uint32_t length = text->len;
    std::int32_t line = 0;
    for (std::uint32_t pos = 0; pos < length; ++line) {
        std::uint32_t end = pos + kLineCharacters;
        if (end >= length) {
            end = length;
        } else {
            const std::uint16_t* at = text->ptr + end - 1;
            while (*(text->ptr != nullptr ? at : g_wstringNullBuffer.get()) != kSpace) {
                --end;
                --at;
            }
        }
        if (line < kShownLines) {
            std::int32_t lineKind = line == 0 ? kind : kKindFollowLine;
            if (kind == kKindTutorial && end >= length) {
                lineKind = kKindLastLine;
            }
            WString part;
            const std::uint8_t allocator = 0;
            using SubstrFn = WString*(__fastcall*)(WString*, void*, const WString*, std::uint32_t, std::uint32_t,
                                                   const std::uint8_t*);
            reinterpret_cast<SubstrFn>(kSubstrConstruct)(&part, nullptr, text, pos, end - pos, &allocator);
            reinterpret_cast<ShowMessageFn>(kShowMessage)(self, nullptr, part, lineKind);
        }
        length = text->len;
        pos = end;
    }
}

// Purpose: Goes on to the next tutorial step and shows its text.
void __fastcall CLogicThread_AdvanceTutorialStep(CLogicThread* self, void* /*edx*/) {
    CGameData* world = self->world;
    std::uint32_t index = 0;
    while (g_tutorialSteps[index].step != world->tutorial_step) {
        ++index;
    }
    const TutorialStep& entry = g_tutorialSteps[index];
    world->tutorial_step = entry.next;
    reinterpret_cast<ThreadFn>(kStartTutorialWait)(reinterpret_cast<CLogicThread*>(self->world), nullptr);
    using TextFn = const char*(__fastcall*)(void* gfx, void* edx, std::uint32_t id, std::uint32_t language);
    const char* textChars = reinterpret_cast<TextFn>(kTextAddress)(g_gfxData.get(), nullptr, entry.text,
                                                                   kCurrentLanguage);
    String narrow;
    framework::MakeEmpty(&narrow, 0);
    if (textChars != nullptr) {
        framework::AssignThroughAssigns(&narrow, textChars, 0);
    }
    WString wide;
    using WidenFn = WString*(__fastcall*)(WString* out, const String* text);
    const WString* widened = reinterpret_cast<WidenFn>(kWiden)(&wide, &narrow);
    reinterpret_cast<ShowTutorialTextFn>(kShowTutorialText)(self, nullptr, widened, entry.kind);
    DestroyWide(&wide);
    framework::EraseAndFree(&narrow);
}

// ---------------------------------------------------------------------------
// Tests: a thread image (random bytes) inside a world-sized-enough buffer
// per side; ClearMessages, ShowMessage, StartTutorialWait, TextAddress and
// ShowTutorialText recording stubs (ShowMessage's text by its characters),
// NarrowToWideString a fake (the narrow text's bytes as characters). The
// texts: random wide strings (spaces often, sometimes none in a long text:
// then the space search runs back into a guard of spaces before the text),
// the tutorial step one of the table's (its texts random narrow strings).
// Compared: the call logs, the thread and the tutorial step.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadOffset = (offsetof(CGameData, tutorial_step) + 0x1000) & ~static_cast<std::size_t>(0xfff);
constexpr std::size_t kTutorialBufferBytes = kThreadOffset + 0x1000;

struct TutorialLog {
    std::vector<std::uint32_t> events;
    std::uintptr_t base = 0;
    const char* text = nullptr;  // TextAddress's result
};
TutorialLog* g_tutorialLog = nullptr;

std::uint32_t Rel(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - g_tutorialLog->base);
}

void __fastcall FakeShowMessage(CLogicThread* self, void*, WString text, std::int32_t kind) {
    auto& e = g_tutorialLog->events;
    e.insert(e.end(), {0x5400u, Rel(self), text.len, static_cast<std::uint32_t>(kind)});
    for (std::uint32_t i = 0; i < text.len; ++i) {
        e.push_back(text.ptr[i]);
    }
    DestroyWide(&text);
}

void __fastcall FakeShowTutorialText(CLogicThread* self, void*, const WString* text, std::int32_t kind) {
    auto& e = g_tutorialLog->events;
    e.insert(e.end(), {0x5470u, Rel(self), text->len, static_cast<std::uint32_t>(kind)});
    for (std::uint32_t i = 0; i < text->len; ++i) {
        e.push_back(text->ptr[i]);
    }
}

std::uint16_t g_fakeWide[0x200];
WString* __fastcall FakeWiden(WString* out, const String* narrow) {
    std::uint32_t n = 0;
    for (; narrow->ptr != nullptr && n < narrow->len && n + 1 < 0x200; ++n) {
        g_fakeWide[n] = static_cast<std::uint8_t>(narrow->ptr[n]);
    }
    g_fakeWide[n] = 0;
    *out = WString{};
    out->ptr = n == 0 ? nullptr : g_fakeWide;
    out->len = n;
    out->res = n;
    g_tutorialLog->events.insert(g_tutorialLog->events.end(), {0x5710u, n});
    return out;
}

// A wide text in `words` (a guard of spaces before it, a zero after),
// frozen (count byte 0xff) so that the line strings copy it.
WString MakeText(test::Rng& rng, std::vector<std::uint16_t>& words) {
    const std::uint32_t length =
        rng.Chance(1, 8) ? 0 : (rng.Chance(1, 2) ? rng.Below(80) : rng.Below(400));
    const bool spaces = !rng.Chance(1, 6);
    words.assign(length + 8, kSpace);
    words[6] = 0xff00;  // count byte 0xff in the high byte of the word before the text
    for (std::uint32_t i = 0; i < length; ++i) {
        words[7 + i] = spaces && rng.Chance(1, 4) ? kSpace : static_cast<std::uint16_t>('a' + rng.Below(26));
    }
    words[7 + length] = 0;
    WString text{};
    text.ptr = length == 0 && rng.Chance(1, 2) ? nullptr : &words[7];
    text.len = length;
    text.res = length;
    return text;
}

struct TutorialRun {
    std::vector<std::uint8_t> thread;
    std::int32_t step = 0;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> events;
};

TutorialRun RunTutorial(std::uint8_t* buffer, const std::vector<std::uint8_t>& image, bool advance,
                        ShowTutorialTextFn show, AdvanceFn step, const WString* text, std::int32_t kind,
                        std::int32_t tutorialStep, const char* stepText) {
    auto* world = reinterpret_cast<CGameData*>(buffer);
    auto* thread = reinterpret_cast<CLogicThread*>(buffer + kThreadOffset);
    std::memcpy(thread, image.data(), image.size());
    thread->world = world;
    world->tutorial_step = tutorialStep;
    TutorialLog log;
    log.base = reinterpret_cast<std::uintptr_t>(buffer);
    log.text = stepText;
    g_tutorialLog = &log;
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    stubs.Install<0, kClearMessages>();
    stubs.Install<0, kStartTutorialWait>();
    stubs.Install<2, kTextAddress>([](CGameData*, std::uint32_t, const std::uint32_t*) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_tutorialLog->text));
    });
    test::ScopedJump showMessage{kShowMessage, reinterpret_cast<const void*>(&FakeShowMessage)};
    test::ScopedJump widen{kWiden, reinterpret_cast<const void*>(&FakeWiden)};
    if (advance) {
        test::ScopedJump showText{kShowTutorialText, reinterpret_cast<const void*>(&FakeShowTutorialText)};
        step(thread, nullptr);
    } else {
        show(thread, nullptr, text, kind);
    }
    g_tutorialLog = nullptr;
    TutorialRun run;
    run.thread.assign(reinterpret_cast<std::uint8_t*>(thread), reinterpret_cast<std::uint8_t*>(thread) + image.size());
    reinterpret_cast<CLogicThread*>(run.thread.data())->world = nullptr;
    run.step = world->tutorial_step;
    run.calls = stubs.Take();
    for (test::StubCall& call : run.calls) {
        if (call.callee == kTextAddress) {
            call.self = 0;  // (g_gfxData)
        }
    }
    run.events = std::move(log.events);
    return run;
}

void CompareTutorial(test::Context& ctx, bool advance, ShowTutorialTextFn show, AdvanceFn step, int cases) {
    test::WorldBuffer a(kTutorialBufferBytes), b(kTutorialBufferBytes);
    std::vector<std::uint8_t> image(sizeof(CLogicThread));
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(image.data(), image.size());
        std::vector<std::uint16_t> words;
        const WString text = MakeText(ctx.rng(), words);
        static const std::int32_t kKinds[] = {253, 253, 254, 0, 255};
        const std::int32_t kind = kKinds[ctx.rng().Below(5)];
        const std::int32_t tutorialStep = g_tutorialSteps[ctx.rng().Below(40)].step;
        std::string stepText;
        const std::uint32_t textLength = ctx.rng().Below(200);
        for (std::uint32_t k = 0; k < textLength; ++k) {
            stepText += static_cast<char>(ctx.rng().Chance(1, 6) ? ' ' : 'a' + ctx.rng().Below(26));
        }
        const char* stepChars = ctx.rng().Chance(1, 8) ? nullptr : stepText.c_str();
        const TutorialRun expected =
            RunTutorial(a.data(), image, advance, Original_CLogicThread_ShowTutorialText,
                        Original_CLogicThread_AdvanceTutorialStep, &text, kind, tutorialStep, stepChars);
        const TutorialRun actual = RunTutorial(b.data(), image, advance, show, step, &text, kind, tutorialStep, stepChars);
        bool same = S3RE_EXPECT(expected.thread == actual.thread);
        same &= S3RE_EXPECT_EQ(expected.step, actual.step);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.events == actual.events);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %s, length %u, kind %d", i, advance ? "advance" : "show",
                     text.len, kind);
            return;
        }
    }
}

// Wrong: every line of a kind-253 text gets 252.
void __fastcall WrongShowTutorialText(CLogicThread* self, void* edx, const WString* text, std::int32_t kind) {
    CLogicThread_ShowTutorialText(self, edx, text, kind == kKindTutorial ? kKindLastLine : kind);
}

// Wrong: the kind of the step after the current one.
void __fastcall WrongAdvanceTutorialStep(CLogicThread* self, void* edx) {
    if (self->world->tutorial_step == 20) {
        self->world->tutorial_step = 22;
    }
    CLogicThread_AdvanceTutorialStep(self, edx);
}

}  // namespace

S3RE_TEST(CLogicThread_Tutorial_MatchesOriginal) {
    CompareTutorial(ctx, false, &CLogicThread_ShowTutorialText, &CLogicThread_AdvanceTutorialStep, 3000);
    CompareTutorial(ctx, true, &CLogicThread_ShowTutorialText, &CLogicThread_AdvanceTutorialStep, 1000);
}

S3RE_TEST(CLogicThread_Tutorial_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareTutorial(probe, false, &WrongShowTutorialText, &CLogicThread_AdvanceTutorialStep, 1000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareTutorial(probe, true, &CLogicThread_ShowTutorialText, &WrongAdvanceTutorialStep, 1000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
