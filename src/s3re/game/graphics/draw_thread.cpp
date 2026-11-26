// Menus::CDrawThread (Dirk\dialog.cpp): the draw thread's small helpers.
//
// CDrawThread::SignalFrameDone (0x0042c810): thiscall (): Win32::CEvent::Set
//   (0x42cf80) on the event at +0x64 (mov ecx, [ecx+0x64]; jmp Set): tells
//   the logic thread that the draw data was taken. EAX = Set's.

#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kEventSet = 0x0042cf80;  // Win32::CEvent::Set (thiscall)

#pragma pack(push, 1)
struct DrawThreadView {
    std::uint8_t unk00[0x64];
    void* frame_done;  // +0x64 the CEvent set when a frame's draw data was taken
};
#pragma pack(pop)
static_assert(offsetof(DrawThreadView, frame_done) == 0x64);

using SetFn = std::uint32_t(__fastcall*)(void* self, void* edx);

}  // namespace

std::uint32_t __fastcall CDrawThread_SignalFrameDone(void* self, void* edx);
using SignalFrameDoneFn = decltype(&CDrawThread_SignalFrameDone);

S3RE_HOOK(0x0042c810, CDrawThread_SignalFrameDone, SignalFrameDoneFn, Original_CDrawThread_SignalFrameDone,
          CDrawThread_SignalFrameDone);

// Purpose: tells the logic thread that the draw thread has taken the
// current draw data (so it may fill the next one).
std::uint32_t __fastcall CDrawThread_SignalFrameDone(void* self, void* /*edx*/) {
    return reinterpret_cast<SetFn>(kEventSet)(static_cast<DrawThreadView*>(self)->frame_done, nullptr);
}

// ---------------------------------------------------------------------------
// Tests: CEvent::Set stubbed (records its ECX, returns a value of it).
// ---------------------------------------------------------------------------

namespace {

struct SetCall {
    std::uint32_t eax;
    std::vector<test::StubCall> calls;
};

SetCall RunSignal(SignalFrameDoneFn fn, DrawThreadView* thread) {
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kEventSet>([](game::CGameData*, std::uint32_t self, const std::uint32_t*) {
        return self ^ 0x5e7u;
    });
    SetCall out;
    out.eax = fn(thread, nullptr);
    out.calls = stubs.Take();
    return out;
}

void CompareSignal(test::Context& ctx, SignalFrameDoneFn candidate, int cases) {
    DrawThreadView thread{};
    for (int n = 0; n < cases; ++n) {
        ctx.rng().Fill(&thread, sizeof(thread));
        const SetCall a = RunSignal(Original_CDrawThread_SignalFrameDone, &thread);
        const SetCall b = RunSignal(candidate, &thread);
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", n);
            break;
        }
    }
}

// Wrong: sets the event at +0x60.
std::uint32_t __fastcall WrongSignalFrameDone(void* self, void* /*edx*/) {
    void* const event = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + 0x60);  // raw-ok: deliberately wrong
    return reinterpret_cast<SetFn>(kEventSet)(event, nullptr);
}

}  // namespace

S3RE_TEST(DrawThreadSignal_MatchesOriginal) {
    CompareSignal(ctx, &CDrawThread_SignalFrameDone, 200);
}

S3RE_TEST(DrawThreadSignal_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSignal(probe, &WrongSignalFrameDone, 50);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
