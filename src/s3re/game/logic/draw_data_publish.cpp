// CLogicThread::PublishDrawData (0x0051f0e0; Volker\LogicDraw.cpp): ()
//   thiscall; the end of PrepareDrawData 0x51f110 (which returns its EAX).
//   Exchanges the filled draw-data buffer (+0x3d0) into the shared slot
//   (*(+0x3cc), InterlockedExchange through S3.EXE's import), keeps the
//   buffer that was there as the next one to fill, and sets the draw event
//   (+0x3d4, Win32::CEvent::Set 0x42cf80); EAX is Set's.

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kEventSet = 0x0042cf80;  // Win32::CEvent::Set (thiscall)

}  // namespace

using PublishFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: Hands the draw data the logic thread just filled to the draw
// thread (swapping buffers with it) and wakes the draw thread up.
std::uint32_t __fastcall CLogicThread_PublishDrawData(CLogicThread* self, void*) {
    const std::int32_t previous = g_importInterlockedExchange.get()(
        static_cast<volatile std::int32_t*>(self->draw_slot),
        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(self->draw_data)));
    self->draw_data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(previous)));
    using SetFn = std::uint32_t(__fastcall*)(void*, void*);
    return reinterpret_cast<SetFn>(kEventSet)(self->draw_event, nullptr);
}

S3RE_HOOK(0x0051f0e0, CLogicThread_PublishDrawData, PublishFn, Original_CLogicThread_PublishDrawData,
          CLogicThread_PublishDrawData);

// ---------------------------------------------------------------------------
// Tests: random thread images whose slot pointer leads to a local slot with
// a random value; CEvent::Set is a recording stub returning a random value.
// Compared: EAX, the thread, the slot, the calls.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadBytes = sizeof(CLogicThread);

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint8_t> thread;
    std::uint32_t slot;
    std::vector<test::StubCall> calls;
};

Outcome Run(PublishFn function, const std::vector<std::uint8_t>& thread, std::uint32_t slotValue,
            std::uint32_t setResult) {
    Outcome out;
    out.thread = thread;
    alignas(4) volatile std::uint32_t slot = slotValue;
    CLogicThread* t = reinterpret_cast<CLogicThread*>(out.thread.data());
    t->draw_slot = const_cast<std::uint32_t*>(&slot);
    test::CalleeStubs stubs;
    stubs.Install<0, kEventSet>([setResult](CGameData*, std::uint32_t, const std::uint32_t*) { return setResult; });
    out.eax = function(t, nullptr);
    t->draw_slot = nullptr;  // (a local's address, differs between the runs)
    out.slot = slot;
    out.calls = stubs.Take();
    return out;
}

void ComparePublish(test::Context& ctx, PublishFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::vector<std::uint8_t> thread(kThreadBytes);
        ctx.rng().Fill(thread.data(), kThreadBytes);
        const std::uint32_t slotValue = ctx.rng().Next32();
        const std::uint32_t setResult = ctx.rng().Next32();
        const Outcome expected = Run(Original_CLogicThread_PublishDrawData, thread, slotValue, setResult);
        const Outcome actual = Run(candidate, thread, slotValue, setResult);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(expected.thread.data(), actual.thread.data(), kThreadBytes);
        same &= S3RE_EXPECT_EQ(expected.slot, actual.slot);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: the buffer is copied into the slot but the old one isn't kept.
std::uint32_t __fastcall WrongPublish(CLogicThread* self, void*) {
    *static_cast<volatile std::uint32_t*>(self->draw_slot) =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self->draw_data));
    using SetFn = std::uint32_t(__fastcall*)(void*, void*);
    return reinterpret_cast<SetFn>(kEventSet)(self->draw_event, nullptr);
}

}  // namespace

S3RE_TEST(CLogicThread_PublishDrawData_MatchesOriginal) {
    ComparePublish(ctx, &CLogicThread_PublishDrawData, 2000);
}

S3RE_TEST(CLogicThread_PublishDrawData_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePublish(probe, &WrongPublish, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
