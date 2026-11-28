// Small CLogicThread members (Volker\LogicInit.cpp, LogicDraw.cpp; see
// re/logic_thread.md for the fields).
//
// CLogicThread::Setup (0x0053c970): (a1..a10) thiscall, ret 0x28. Stores the
//   wiring the app (0x405630) hands over before the thread runs: +0x3c0 = a1,
//   the net queues +0x3c4 = a2 / +0x3c8 = a3, byte +0x3b8 = a4, the shared
//   draw slot +0x3cc = a5, the draw event +0x3d4 = a6, the draw-request event
//   +0x3d8 = a7, +0x3a8 = a8, the lobby +0x66c = a9, the lobby client +0x670 =
//   a10. EAX not used (it holds a9 at the end).
// CLogicThread::ClearMessages (0x0051e650): () thiscall: every one of the 20
//   message lines gets lifetime 0 and dirty 1; the current line becomes
//   message_count - 1. EAX not used.
// CLogicThread::AgeMessages (0x0051e680): () thiscall, called once per tick
//   by Step: every line with a lifetime > 0 (signed) loses one tick and is
//   marked dirty. EAX not used.
// CLogicThread::ClearSelection (0x00542630): () thiscall: no settler group
//   shown (g_currentSettlerGroup = -1); if a building was selected,
//   g_selectedBuilding = 0 and +0x4c = 0; then UpdateGroupMenu(-1) (which
//   reads the counts and lists one group before the first, as in the
//   original) and returns its EAX (the world pointer).
// CLogicThread::StoreSettlerGroup (0x00542820): (slot) thiscall, ret 4: when
//   the current group (g_currentSettlerGroup) has settlers, copies its list
//   into group `slot` (probably Ctrl+number), marks that group changed,
//   calls RemoveSettlerFromGroups(settler, slot) for each of its settlers
//   (the count is re-read every time), makes `slot` the current group and
//   returns UpdateGroupMenu(slot) (the world pointer). An empty current group
//   leaves everything alone; EAX is then the caller's (callers don't use
//   it). Neither group number is range-checked.
// CLogicThread::GoodsPriorityPosition (0x00542d40): (area, good) thiscall,
//   ret 8: the position (0..35) of `good` in the transport order list of
//   area `area` (Area::settings4, compared as zero-extended bytes with the
//   whole dword), or 0 when the area number is 0x1000 or more (unsigned),
//   the area is free (player 0xff) or the good isn't in the list.
// LogicRecord24::LogicRecord24 (0x005437e0, was LogicRecord24_args): (a1..a9)
//   thiscall, ret 0x24: the nine dwords of a map-effect draw record in
//   argument order; EAX = this. Called by MapEffect::MakeDrawRecord 0x544100.
//   (The default constructor 0x5437b0 is in graphics_helpers.cpp.)

#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/settlers/settler_groups.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::size_t kMessageLines = sizeof(CLogicThread::messages) / sizeof(CLogicThread::messages[0]);
static_assert(kMessageLines == 20);

constexpr std::uint32_t kAreaLimit = 0x1000;     // area numbers below this are checked
constexpr std::uint8_t kFreeArea = 0xff;         // Area::player of an unused area
constexpr std::int32_t kNoGroup = -1;            // g_currentSettlerGroup: no group shown

// Callees, through their S3.EXE addresses (ECX = the thread).
constexpr std::uintptr_t kRemoveSettlerFromGroups = 0x005428e0;
constexpr std::uintptr_t kUpdateGroupMenu = 0x00542990;

template <std::uintptr_t Address, typename... Args>
std::uint32_t CallThread(CLogicThread* thread, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CLogicThread*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(thread, nullptr, static_cast<std::uint32_t>(args)...);
}

#pragma pack(push, 1)
// The map-effect draw record (DrawData+0xc690c, 100 of them; re/logic_thread.md).
struct LogicRecord24 {
    std::uint32_t values[9];
};
#pragma pack(pop)
static_assert(sizeof(LogicRecord24) == 0x24);

}  // namespace

using SetupFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                           std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                           std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10);
using ThreadFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);
using ThreadValueFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t value);
using ThreadPairFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t a, std::uint32_t b);
using Record24Fn = LogicRecord24*(__fastcall*)(LogicRecord24* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                               std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                               std::uint32_t a7, std::uint32_t a8, std::uint32_t a9);

// Purpose: Hands the logic thread its connections before it starts: the
// queues to and from the network thread, the draw-data exchange with the
// draw thread and the lobby objects.
std::uint32_t __fastcall CLogicThread_Setup(CLogicThread* self, void*, std::uint32_t a1, std::uint32_t a2,
                                            std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                            std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10) {
    CLogicThread* thread = LogicThreadOf(self);
    thread->ui_events = reinterpret_cast<MessageQueue*>(a1);
    thread->toNet = reinterpret_cast<MessageQueue*>(a2);
    thread->fromNet = reinterpret_cast<MessageQueue*>(a3);
    thread->setup_3b8 = static_cast<std::uint8_t>(a4);
    thread->draw_slot = reinterpret_cast<void*>(a5);
    thread->draw_event = reinterpret_cast<void*>(a6);
    thread->draw_request_event = reinterpret_cast<void*>(a7);
    thread->setup_3a8 = a8;
    thread->lobby = reinterpret_cast<void*>(a9);
    thread->lobby_client = reinterpret_cast<void*>(a10);
    return a9;
}

// Purpose: Removes all messages from the screen (their lines expire at once
// and are redrawn).
std::uint32_t __fastcall CLogicThread_ClearMessages(CLogicThread* self, void*) {
    CLogicThread* thread = LogicThreadOf(self);
    for (MessageLine& line : thread->messages) {
        line.lifetime = 0;
        line.dirty = 1;
    }
    thread->message_index = thread->message_count - 1;
    return static_cast<std::uint32_t>(thread->message_index);
}

// Purpose: Counts down the time the on-screen messages stay visible, once per
// game tick.
std::uint32_t __fastcall CLogicThread_AgeMessages(CLogicThread* self, void*) {
    for (MessageLine& line : LogicThreadOf(self)->messages) {
        if (line.lifetime > 0) {
            line.lifetime = line.lifetime - 1;
            line.dirty = 1;
        }
    }
    return 0;
}

// Purpose: Deselects everything in the user interface (no settler group, no
// building) and resets the group's special menu.
std::uint32_t __fastcall CLogicThread_ClearSelection(CLogicThread* self, void*) {
    g_currentSettlerGroup = kNoGroup;
    if (g_selectedBuilding != 0) {
        g_selectedBuilding = 0;
        LogicThreadOf(self)->group_selection_changed = 0;
    }
    return CallThread<kUpdateGroupMenu>(self, static_cast<std::uint32_t>(g_currentSettlerGroup.get()));
}

// Purpose: Stores the settlers of the current selection as settler group
// `slot` (probably the player's numbered groups), taking them out of the
// other groups, and shows that group.
std::uint32_t __fastcall CLogicThread_StoreSettlerGroup(CLogicThread* self, void*, std::uint32_t slot) {
    SettlerGroups& groups = GlobalSettlerGroups();
    const std::uint32_t current = static_cast<std::uint32_t>(g_currentSettlerGroup.get());
    // (32-bit index arithmetic for both group numbers, as in the original.)
    const std::uint16_t count = groups.counts[current];
    if (count == 0) {
        return 0;  // (the original leaves the caller's EAX; no caller uses it)
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        groups.lists[slot][i] = groups.lists[current][i];
    }
    groups.counts[slot] = count;
    g_settlerGroupChanged[slot] = 1;
    // The count is re-read after every call (RemoveSettlerFromGroups skips
    // `slot`, so it doesn't change).
    for (std::uint32_t i = 0; i < groups.counts[slot]; ++i) {
        CallThread<kRemoveSettlerFromGroups>(self, groups.lists[slot][i], slot);
    }
    g_currentSettlerGroup = static_cast<std::int32_t>(slot);
    return CallThread<kUpdateGroupMenu>(self, slot);
}

// Purpose: Where a good stands in an area's transport order (probably shown
// by the transport priority menu).
std::uint32_t __fastcall CLogicThread_GoodsPriorityPosition(CLogicThread* self, void*, std::uint32_t area,
                                                            std::uint32_t good) {
    if (area >= kAreaLimit) {
        return 0;
    }
    const layout::Area& record = LogicThreadOf(self)->world->areas[area];
    if (record.player == kFreeArea) {
        return 0;
    }
    for (std::uint32_t position = 0; position < layout::kGoodsTypes; ++position) {
        if (record.settings4[position] == good) {
            return position;
        }
    }
    return 0;
}

// Purpose: Fills a map-effect draw record (graphic, position, frame and
// brightness of one visible effect) for the draw thread.
LogicRecord24* __fastcall LogicRecord24_Construct_args(LogicRecord24* self, void*, std::uint32_t a1,
                                                       std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                                                       std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                                       std::uint32_t a8, std::uint32_t a9) {
    const std::uint32_t values[9] = {a1, a2, a3, a4, a5, a6, a7, a8, a9};
    std::memcpy(self->values, values, sizeof(values));
    return self;
}

S3RE_HOOK(0x0053c970, CLogicThread_Setup, SetupFn, Original_CLogicThread_Setup, CLogicThread_Setup);
S3RE_HOOK(0x0051e650, CLogicThread_ClearMessages, ThreadFn, Original_CLogicThread_ClearMessages,
          CLogicThread_ClearMessages);
S3RE_HOOK(0x0051e680, CLogicThread_AgeMessages, ThreadFn, Original_CLogicThread_AgeMessages,
          CLogicThread_AgeMessages);
S3RE_HOOK(0x00542630, CLogicThread_ClearSelection, ThreadFn, Original_CLogicThread_ClearSelection,
          CLogicThread_ClearSelection);
S3RE_HOOK(0x00542820, CLogicThread_StoreSettlerGroup, ThreadValueFn, Original_CLogicThread_StoreSettlerGroup,
          CLogicThread_StoreSettlerGroup);
S3RE_HOOK(0x00542d40, CLogicThread_GoodsPriorityPosition, ThreadPairFn,
          Original_CLogicThread_GoodsPriorityPosition, CLogicThread_GoodsPriorityPosition);
S3RE_HOOK(0x005437e0, LogicRecord24_Construct_args, Record24Fn, Original_LogicRecord24_Construct_args,
          LogicRecord24_Construct_args);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadBytes = sizeof(CLogicThread);

// Two thread images with the same random bytes; their world pointers point
// at the two worlds (when given).
struct Threads {
    Threads(test::Context& ctx, test::WorldPair* worlds) {
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(b));
        if (worlds != nullptr) {
            LogicThreadOf(a)->world = worlds->original();
            LogicThreadOf(b)->world = worlds->ours();
        }
    }
    CLogicThread* original() { return reinterpret_cast<CLogicThread*>(a); }
    CLogicThread* ours() { return reinterpret_cast<CLogicThread*>(b); }
    // The bytes before and after the world pointer.
    bool Same(test::Context& ctx) {
        constexpr std::size_t world = offsetof(CLogicThread, world);
        return S3RE_EXPECT_MEM_EQ(a, b, world) &&
               S3RE_EXPECT_MEM_EQ(a + world + 4, b + world + 4, kThreadBytes - world - 4);
    }
    alignas(4) std::uint8_t a[kThreadBytes];
    alignas(4) std::uint8_t b[kThreadBytes];
};

// Saves the settler groups, the group globals and restores them afterwards.
class SavedGroupState {
public:
    SavedGroupState()
        : groups_(g_settlerGroups.get()),
          current_(g_currentSettlerGroup),
          building_(g_selectedBuilding) {
        std::memcpy(changed_, &g_settlerGroupChanged.get(), sizeof(changed_));
    }
    ~SavedGroupState() {
        g_settlerGroups.get() = groups_;
        g_currentSettlerGroup = current_;
        g_selectedBuilding = building_;
        std::memcpy(&g_settlerGroupChanged.get(), changed_, sizeof(changed_));
    }

private:
    SettlerGroups groups_;
    std::int32_t current_;
    std::int32_t building_;
    std::int32_t changed_[10];
};

void CompareMessages(test::Context& ctx, ThreadFn original, ThreadFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        Threads threads(ctx, nullptr);
        for (int line = 0; line < 20; ++line) {
            const std::int32_t lifetime = ctx.rng().Chance(1, 2) ? ctx.rng().Range(-3, 3) : static_cast<std::int32_t>(ctx.rng().Next32());
            LogicThreadOf(threads.a)->messages[line].lifetime = lifetime;
            LogicThreadOf(threads.b)->messages[line].lifetime = lifetime;
        }
        original(threads.original(), nullptr);
        candidate(threads.ours(), nullptr);
        if (!threads.Same(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: a lifetime of exactly 1 is not counted down.
std::uint32_t __fastcall WrongAgeMessages(CLogicThread* self, void*) {
    for (MessageLine& line : LogicThreadOf(self)->messages) {
        if (line.lifetime > 1) {
            line.lifetime = line.lifetime - 1;
            line.dirty = 1;
        }
    }
    return 0;
}

}  // namespace

S3RE_TEST(CLogicThread_Setup_MatchesOriginal) {
    for (int i = 0; i < 2000; ++i) {
        Threads threads(ctx, nullptr);
        std::uint32_t a[10];
        for (std::uint32_t& value : a) {
            value = ctx.rng().Next32();
        }
        Original_CLogicThread_Setup(threads.original(), nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                                    a[8], a[9]);
        CLogicThread_Setup(threads.ours(), nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        if (!S3RE_EXPECT_MEM_EQ(threads.a, threads.b, kThreadBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

S3RE_TEST(CLogicThread_ClearMessages_MatchesOriginal) {
    CompareMessages(ctx, Original_CLogicThread_ClearMessages, &CLogicThread_ClearMessages, 2000);
}

S3RE_TEST(CLogicThread_AgeMessages_MatchesOriginal) {
    CompareMessages(ctx, Original_CLogicThread_AgeMessages, &CLogicThread_AgeMessages, 2000);
}

S3RE_TEST(CLogicThread_AgeMessages_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMessages(probe, Original_CLogicThread_AgeMessages, &WrongAgeMessages, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Random groups (lists of small and arbitrary settler numbers) and change flags.
void RandomGroups(test::Context& ctx) {
    SettlerGroups& groups = GlobalSettlerGroups();
    for (std::size_t g = 0; g < SettlerGroups::kGroups; ++g) {
        const std::uint32_t count = ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(101);
        groups.counts[g] = static_cast<std::uint16_t>(count);
        for (std::uint32_t k = 0; k < SettlerGroups::kWords; ++k) {
            groups.lists[g][k] = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 1 + ctx.rng().Below(8)
                                                                                    : ctx.rng().Below(32000));
        }
    }
    for (std::size_t g = 0; g < SettlerGroups::kGroups; ++g) {
        g_settlerGroupChanged[g] = static_cast<std::int32_t>(ctx.rng().Below(3));
    }
}

using GroupCall = std::uint32_t (*)(CLogicThread* thread, std::uint32_t argument, bool original);

// Runs `call` on both sides from the same group state (the groups and the
// group globals live in S3.EXE's data, so each side runs from a copy of the
// state and its result is saved).
void CompareGroupFunction(test::Context& ctx, GroupCall call, int cases) {
    const SavedGroupState saved;
    test::WorldPair worlds(ctx, offsetof(CGameData, settlers) + 32000 * sizeof(layout::Settler));
    for (int i = 0; i < cases; ++i) {
        Threads threads(ctx, &worlds);
        RandomGroups(ctx);
        g_currentSettlerGroup = ctx.rng().Chance(1, 10) ? -1 : static_cast<std::int32_t>(ctx.rng().Below(10));
        g_selectedBuilding = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::int32_t>(ctx.rng().Below(5000));
        const std::uint32_t argument = ctx.rng().Below(10);
        const SettlerGroups before = g_settlerGroups.get();
        std::int32_t changedBefore[10];
        std::memcpy(changedBefore, &g_settlerGroupChanged.get(), sizeof(changedBefore));
        const std::int32_t currentBefore = g_currentSettlerGroup;
        const std::int32_t buildingBefore = g_selectedBuilding;

        const std::uint32_t expected = call(threads.original(), argument, true);
        const SettlerGroups originalGroups = g_settlerGroups.get();
        std::int32_t originalChanged[10];
        std::memcpy(originalChanged, &g_settlerGroupChanged.get(), sizeof(originalChanged));
        const std::int32_t originalCurrent = g_currentSettlerGroup;
        const std::int32_t originalBuilding = g_selectedBuilding;

        g_settlerGroups.get() = before;
        std::memcpy(&g_settlerGroupChanged.get(), changedBefore, sizeof(changedBefore));
        g_currentSettlerGroup = currentBefore;
        g_selectedBuilding = buildingBefore;
        const std::uint32_t actual = call(threads.ours(), argument, false);

        // The world pointer (UpdateGroupMenu's result) compares as "world".
        const auto normalized = [](std::uint32_t result, CGameData* w) {
            return result == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w)) ? 0xffffffffu : result;
        };
        bool same = S3RE_EXPECT_EQ(normalized(expected, worlds.original()), normalized(actual, worlds.ours()));
        same &= S3RE_EXPECT_MEM_EQ(&originalGroups, &g_settlerGroups.get(), sizeof(SettlerGroups));
        same &= S3RE_EXPECT_MEM_EQ(originalChanged, &g_settlerGroupChanged.get(), sizeof(originalChanged));
        same &= S3RE_EXPECT_EQ(originalCurrent, g_currentSettlerGroup.get());
        same &= S3RE_EXPECT_EQ(originalBuilding, g_selectedBuilding.get());
        same &= threads.Same(ctx);
        same &= worlds.Same(ctx, offsetof(CGameData, refresh_ui), offsetof(CGameData, ui_2d29c) + 4 - offsetof(CGameData, refresh_ui));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: argument %u, current %d", i, argument, currentBefore);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

std::uint32_t CallClearSelection(CLogicThread* thread, std::uint32_t, bool original) {
    return original ? Original_CLogicThread_ClearSelection(thread, nullptr)
                    : CLogicThread_ClearSelection(thread, nullptr);
}

std::uint32_t CallStoreSettlerGroup(CLogicThread* thread, std::uint32_t slot, bool original) {
    // The empty-group path leaves the caller's EAX in the original: compare
    // the result only when the group is stored.
    const std::uint32_t current = static_cast<std::uint32_t>(g_currentSettlerGroup.get());
    const bool empty = GlobalSettlerGroups().counts[current] == 0;
    const std::uint32_t result = original ? Original_CLogicThread_StoreSettlerGroup(thread, nullptr, slot)
                                          : CLogicThread_StoreSettlerGroup(thread, nullptr, slot);
    return empty ? 0 : result;
}

// Wrong: the stored group is not marked changed.
std::uint32_t CallWrongStoreSettlerGroup(CLogicThread* thread, std::uint32_t slot, bool original) {
    const std::uint32_t result = CallStoreSettlerGroup(thread, slot, original);
    if (!original) {
        g_settlerGroupChanged[slot] = 0;
    }
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ClearSelection_MatchesOriginal) {
    CompareGroupFunction(ctx, &CallClearSelection, 2000);
}

S3RE_TEST(CLogicThread_StoreSettlerGroup_MatchesOriginal) {
    CompareGroupFunction(ctx, &CallStoreSettlerGroup, 3000);
}

S3RE_TEST(CLogicThread_StoreSettlerGroup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGroupFunction(probe, &CallWrongStoreSettlerGroup, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

void CompareGoodsPriorityPosition(test::Context& ctx, ThreadPairFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, areas) + 4096 * sizeof(layout::Area));
    Threads threads(ctx, &worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t area = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(0x1001);
        const std::uint32_t good = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(40);
        if (area < kAreaLimit) {
            const std::uint8_t player = ctx.rng().Chance(1, 5) ? kFreeArea : static_cast<std::uint8_t>(ctx.rng().Below(20));
            std::uint8_t order[layout::kGoodsTypes];
            for (std::uint8_t& value : order) {
                value = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(40) : ctx.rng().Next32());
            }
            worlds.Both([&](CGameData* w) {
                w->areas[area].player = player;
                std::memcpy(w->areas[area].settings4, order, sizeof(order));
            });
        }
        const std::uint32_t expected = Original_CLogicThread_GoodsPriorityPosition(threads.original(), nullptr, area, good);
        const std::uint32_t actual = candidate(threads.ours(), nullptr, area, good);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: area %#x, good %#x", i, area, good);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: finds the last occurrence instead of the first.
std::uint32_t __fastcall WrongGoodsPriorityPosition(CLogicThread* self, void* edx, std::uint32_t area,
                                                    std::uint32_t good) {
    const std::uint32_t first = CLogicThread_GoodsPriorityPosition(self, edx, area, good);
    if (area >= kAreaLimit || LogicThreadOf(self)->world->areas[area].player == kFreeArea) {
        return first;
    }
    std::uint32_t last = first;
    for (std::uint32_t p = 0; p < layout::kGoodsTypes; ++p) {
        if (LogicThreadOf(self)->world->areas[area].settings4[p] == good) {
            last = p;
        }
    }
    return last;
}

}  // namespace

S3RE_TEST(CLogicThread_GoodsPriorityPosition_MatchesOriginal) {
    CompareGoodsPriorityPosition(ctx, &CLogicThread_GoodsPriorityPosition, 20000);
}

S3RE_TEST(CLogicThread_GoodsPriorityPosition_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGoodsPriorityPosition(probe, &WrongGoodsPriorityPosition, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(LogicRecord24_Construct_args_MatchesOriginal) {
    for (int i = 0; i < 2000; ++i) {
        LogicRecord24 a[3], b[3];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(b));
        std::uint32_t v[9];
        for (std::uint32_t& value : v) {
            value = ctx.rng().Next32();
        }
        LogicRecord24* ra = Original_LogicRecord24_Construct_args(&a[1], nullptr, v[0], v[1], v[2], v[3], v[4], v[5],
                                                                  v[6], v[7], v[8]);
        LogicRecord24* rb = LogicRecord24_Construct_args(&b[1], nullptr, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
                                                         v[7], v[8]);
        if (!S3RE_EXPECT(ra == &a[1] && rb == &b[1]) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}


// CLogicThread::SelectSettlerGroup (0x00542660): (group) thiscall, ret 4; ProcessEvents calls it for a
//   group hotkey. An empty group does nothing. A group that isn't the current one becomes current: the
//   thread's +0x4c = 1, no building selected, selection mode 5, special menu 0, refresh the UI, and
//   UpdateGroupMenu(group) (its EAX is returned; the original also sums the positions of the local
//   player's settlers there without using them). Pressing the current group again centres the camera
//   on the average position (unsigned division) of its settlers that belong to the local player (none:
//   nothing) and clears world+0x2d264 (redraw). The group number is not range-checked. EAX not used
//   by the caller.
// CLogicThread::SelectSettlersOfType (0x00542b60): (type, mode) thiscall, ret 8; ProcessEvents calls it
//   (probably for a double click on a settler, mode 1..3; any other mode does nothing). From the tile at
//   the screen centre, the first 3000 (mode 3: 19995) entries of the world's ring table (offsets
//   sorted by distance) are visited in order: a tile on the map (IsOnMap) with a settler of the local
//   player (owner compared with the whole dword) and of `type` (the type byte compared with the whole
//   dword) that is not yet in the list is appended to settler group 0's list (RemoveSettlerFromGroups(
//   settler, 0) for each), up to 100 settlers (mode 1: one). If any were found, group 0 holds them and
//   becomes current (changed, thread +0x4c = 1, no building, selection mode 5, special menu 0, refresh)
//   and UpdateGroupMenu(0) runs. EAX not used by the callers.

namespace {

constexpr std::uintptr_t kCenterCameraOnTile = 0x0051b560;  // ECX = the world
constexpr std::uintptr_t kScreenCenterTileX = 0x005158f0;
constexpr std::uintptr_t kScreenCenterTileY = 0x00515980;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::int32_t kGroupSelectionMode = 5;  // ui_selection_mode of a selected settler group

template <std::uintptr_t Address, typename... Args>
std::uint32_t CallWorld(CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}

// What becomes of the UI when a settler group is selected (0x54270f, 0x542ce1).
void ShowGroup(CLogicThread* thread) {
    thread->group_selection_changed = 1;
    g_selectedBuilding = 0;
    thread->world->ui_selection_mode = kGroupSelectionMode;
    thread->world->ui_special_menu = 0;
    thread->world->refresh_ui = 1;
}

}  // namespace

using SelectTypeFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t type, std::uint32_t mode);

// Purpose: Selects a numbered settler group (hotkey); pressing it again
// moves the camera to the group.
std::uint32_t __fastcall CLogicThread_SelectSettlerGroup(CLogicThread* self, void*, std::uint32_t group) {
    CLogicThread* thread = LogicThreadOf(self);
    SettlerGroups& groups = GlobalSettlerGroups();
    const std::uint16_t count = groups.counts[group];
    if (count == 0) {
        return 0;  // (the caller's EAX in the original; not used)
    }
    if (static_cast<std::uint32_t>(g_currentSettlerGroup.get()) != group) {
        g_currentSettlerGroup = static_cast<std::int32_t>(group);
        ShowGroup(thread);
        return CallThread<kUpdateGroupMenu>(self, static_cast<std::uint32_t>(g_currentSettlerGroup.get()));
    }
    CGameData* world = thread->world;
    std::uint32_t sumX = 0, sumY = 0, own = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const layout::Settler& settler = world->settlers[groups.lists[group][i]];
        if (static_cast<std::uint32_t>(settler.owner) == static_cast<std::uint32_t>(world->local_player)) {
            sumX += settler.x;
            sumY += settler.y;
            ++own;
        }
    }
    if (own == 0) {
        return static_cast<std::uint32_t>(world->local_player);  // (what the original leaves in EAX)
    }
    CallWorld<kCenterCameraOnTile>(world, sumX / own, sumY / own);
    thread->world->redraw = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

// Purpose: Selects the local player's settlers of one type around the
// screen centre (probably a double click on a settler: the nearest one, or
// up to 100 nearby, or on a larger area) as settler group 0.
std::uint32_t __fastcall CLogicThread_SelectSettlersOfType(CLogicThread* self, void*, std::uint32_t type,
                                                           std::uint32_t mode) {
    if (mode != 1 && mode != 2 && mode != 3) {
        return 0;
    }
    CLogicThread* thread = LogicThreadOf(self);
    const std::uint32_t centreX = CallWorld<kScreenCenterTileX>(thread->world);
    const std::uint32_t centreY = CallWorld<kScreenCenterTileY>(thread->world);
    const std::uint32_t wanted = mode == 1 ? 1 : 100;
    const std::uint32_t offsets = mode == 3 ? 19995 : 3000;
    SettlerGroups& groups = GlobalSettlerGroups();
    std::uint16_t* const list = groups.lists[0];
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < offsets && found != wanted; ++i) {
        CGameData* world = thread->world;
        const layout::MapOffset& offset = world->offsets[i];
        const std::uint32_t x = centreX + static_cast<std::uint32_t>(offset.dx);
        const std::uint32_t y = centreY + static_cast<std::uint32_t>(offset.dy);
        if (CallWorld<kIsOnMap>(world, x, y) == 0) {
            continue;
        }
        CGameData* const w = thread->world;
        const std::uint32_t number = TileAtOffset(w, TileOffset(x, y)).settler;
        if (number == 0) {
            continue;
        }
        const layout::Settler& settler = w->settlers[number];
        if (static_cast<std::uint32_t>(settler.owner) != static_cast<std::uint32_t>(w->local_player) ||
            static_cast<std::uint32_t>(settler.art) != type) {
            continue;
        }
        bool known = false;
        for (std::uint32_t k = 0; k < found; ++k) {
            if (list[k] == number) {
                known = true;
            }
        }
        if (known) {
            continue;
        }
        list[found] = static_cast<std::uint16_t>(number);
        CallThread<kRemoveSettlerFromGroups>(self, number, 0u);
        ++found;
    }
    if (found == 0) {
        return 0;
    }
    groups.counts[0] = static_cast<std::uint16_t>(found);
    g_currentSettlerGroup = 0;
    g_settlerGroupChanged[0] = 1;
    ShowGroup(thread);
    return CallThread<kUpdateGroupMenu>(self, static_cast<std::uint32_t>(g_currentSettlerGroup.get()));
}

S3RE_HOOK(0x00542660, CLogicThread_SelectSettlerGroup, ThreadValueFn, Original_CLogicThread_SelectSettlerGroup,
          CLogicThread_SelectSettlerGroup);
S3RE_HOOK(0x00542b60, CLogicThread_SelectSettlersOfType, SelectTypeFn, Original_CLogicThread_SelectSettlersOfType,
          CLogicThread_SelectSettlersOfType);

namespace {

// A full world with a ring table (distance-sorted small offsets), a map,
// settlers of a few players and types on the tiles around the centre, and
// random settler groups.
struct SelectionWorld {
    explicit SelectionWorld(test::Context& ctx) : worlds(ctx, sizeof(CGameData)), threads(ctx, &worlds) {
        std::vector<layout::MapOffset> table(20000);
        std::uint32_t n = 0;
        for (std::int32_t d = 0; n < 20000; ++d) {
            for (std::int32_t dx = -d; dx <= d && n < 20000; ++dx) {
                for (std::int32_t dy = -d; dy <= d && n < 20000; ++dy) {
                    if ((dx < 0 ? -dx : dx) == d || (dy < 0 ? -dy : dy) == d) {
                        table[n++] = layout::MapOffset{dx, dy, d, 0};
                    }
                }
            }
        }
        worlds.Both([&](CGameData* w) { std::memcpy(w->offsets, table.data(), sizeof(w->offsets)); });
    }
    test::WorldPair worlds;
    Threads threads;
};

void PrepareSelection(test::Context& ctx, SelectionWorld& s) {
    const std::uint32_t size = 64 + ctx.rng().Below(200);
    const auto local = static_cast<std::int32_t>(ctx.rng().Below(4));
    const std::int32_t cameraX = ctx.rng().Range(0, 3000), cameraY = ctx.rng().Range(0, 2000);
    s.worlds.Both([&](CGameData* w) {
        w->map_size = size;
        w->local_player = local;
        w->camera_x = cameraX;
        w->camera_y = cameraY;
    });
    // Settlers on a patch of tiles (most of the map is empty).
    for (std::uint32_t x = 0; x < size; ++x) {
        for (std::uint32_t y = 0; y < size; ++y) {
            const std::uint16_t number = static_cast<std::uint16_t>(ctx.rng().Chance(1, 6) ? 1 + ctx.rng().Below(31999) : 0);
            s.worlds.Both([&](CGameData* w) { w->tiles[x][y].settler = number; });
        }
    }
    for (std::uint32_t n = 0; n < 32000; ++n) {  // (every settler: the groups name any of them)
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? local : ctx.rng().Below(4));
        const auto art = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(size));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(size));
        s.worlds.Both([&](CGameData* w) {
            w->settlers[n].owner = owner;
            w->settlers[n].art = art;
            w->settlers[n].x = x;
            w->settlers[n].y = y;
        });
    }
}

using SelectionCall = std::uint32_t (*)(CLogicThread* thread, std::uint32_t a, std::uint32_t b, bool original);

void CompareSelection(test::Context& ctx, SelectionCall call, bool byType, int cases) {
    const SavedGroupState saved;
    const std::uint32_t savedWidth = g_screenWidth, savedHeight = g_screenHeight;
    g_screenWidth = 504;
    g_screenHeight = 480;
    SelectionWorld s(ctx);
    for (int i = 0; i < cases; ++i) {
        if (i % 50 == 0) {
            PrepareSelection(ctx, s);
        }
        RandomGroups(ctx);
        g_currentSettlerGroup = ctx.rng().Chance(1, 10) ? -1 : static_cast<std::int32_t>(ctx.rng().Below(10));
        g_selectedBuilding = static_cast<std::int32_t>(ctx.rng().Below(5000));
        // (Group -1 would read settler numbers from the data before the
        // groups, past the settler array: not tested.)
        const bool again = g_currentSettlerGroup >= 0 && ctx.rng().Chance(1, 2);
        const std::uint32_t a = byType ? ctx.rng().Below(4)
                                       : again ? static_cast<std::uint32_t>(g_currentSettlerGroup.get()) : ctx.rng().Below(10);
        const std::uint32_t b = byType ? (ctx.rng().Chance(1, 8) ? ctx.rng().Below(6) : 1 + ctx.rng().Below(3)) : 0;
        const SettlerGroups before = g_settlerGroups.get();
        std::int32_t changedBefore[10];
        std::memcpy(changedBefore, &g_settlerGroupChanged.get(), sizeof(changedBefore));
        const std::int32_t currentBefore = g_currentSettlerGroup, buildingBefore = g_selectedBuilding;

        const std::uint32_t expected = call(s.threads.original(), a, b, true);
        const SettlerGroups originalGroups = g_settlerGroups.get();
        std::int32_t originalChanged[10];
        std::memcpy(originalChanged, &g_settlerGroupChanged.get(), sizeof(originalChanged));
        const std::int32_t originalCurrent = g_currentSettlerGroup, originalBuilding = g_selectedBuilding;

        g_settlerGroups.get() = before;
        std::memcpy(&g_settlerGroupChanged.get(), changedBefore, sizeof(changedBefore));
        g_currentSettlerGroup = currentBefore;
        g_selectedBuilding = buildingBefore;
        const std::uint32_t actual = call(s.threads.ours(), a, b, false);

        bool same = S3RE_EXPECT_EQ(expected, actual);
        same &= S3RE_EXPECT_MEM_EQ(&originalGroups, &g_settlerGroups.get(), sizeof(SettlerGroups));
        same &= S3RE_EXPECT_MEM_EQ(originalChanged, &g_settlerGroupChanged.get(), sizeof(originalChanged));
        same &= S3RE_EXPECT_EQ(originalCurrent, g_currentSettlerGroup.get());
        same &= S3RE_EXPECT_EQ(originalBuilding, g_selectedBuilding.get());
        same &= s.threads.Same(ctx);
        same &= s.worlds.Same(ctx, offsetof(CGameData, chart), offsetof(CGameData, notifications) - offsetof(CGameData, chart));
        if (!same || (i % 100 == 99 && !s.worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u), current %d", i, a, b, currentBefore);
            break;
        }
    }
    g_screenWidth = savedWidth;
    g_screenHeight = savedHeight;
}

// The results compare as "world", "thread" or 0 (the empty-group path's
// caller EAX is left out).
std::uint32_t Normalized(std::uint32_t result, CLogicThread* thread) {
    if (result == reinterpret_cast<std::uint32_t>(thread->world)) {
        return 1;
    }
    return result == reinterpret_cast<std::uint32_t>(thread) ? 2 : result;
}

std::uint32_t CallSelectGroup(CLogicThread* thread, std::uint32_t group, std::uint32_t, bool original) {
    const bool empty = GlobalSettlerGroups().counts[group] == 0;
    const std::uint32_t r = original ? Original_CLogicThread_SelectSettlerGroup(thread, nullptr, group)
                                     : CLogicThread_SelectSettlerGroup(thread, nullptr, group);
    return empty ? 0 : Normalized(r, thread);
}

std::uint32_t CallSelectType(CLogicThread* thread, std::uint32_t type, std::uint32_t mode, bool original) {
    // (EAX: not used by the callers, and the "nothing found" paths leave
    // registers of the original's own; only the side effects are compared.)
    if (original) {
        Original_CLogicThread_SelectSettlersOfType(thread, nullptr, type, mode);
    } else {
        CLogicThread_SelectSettlersOfType(thread, nullptr, type, mode);
    }
    return 0;
}

// Wrong: the farthest settler found is dropped.
std::uint32_t CallWrongSelectType(CLogicThread* thread, std::uint32_t type, std::uint32_t mode, bool original) {
    const std::uint16_t before = GlobalSettlerGroups().counts[0];
    CallSelectType(thread, type, mode, original);
    const std::uint16_t after = GlobalSettlerGroups().counts[0];
    if (!original && after >= 2 && after != before) {
        GlobalSettlerGroups().counts[0] = static_cast<std::uint16_t>(after - 1);
    }
    return 0;
}

}  // namespace

// Wrong: the camera jump leaves the redraw flag set.
static std::uint32_t CallWrongSelectGroup(CLogicThread* thread, std::uint32_t group, std::uint32_t b, bool original) {
    const std::uint32_t r = CallSelectGroup(thread, group, b, original);
    if (!original && r == 2) {
        LogicThreadOf(thread)->world->redraw = 1;
    }
    return r;
}

S3RE_TEST(CLogicThread_SelectSettlerGroup_MatchesOriginal) {
    CompareSelection(ctx, &CallSelectGroup, false, 2000);
}
S3RE_TEST(CLogicThread_SelectSettlerGroup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSelection(probe, &CallWrongSelectGroup, false, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CLogicThread_SelectSettlersOfType_MatchesOriginal) {
    CompareSelection(ctx, &CallSelectType, true, 600);
}
S3RE_TEST(CLogicThread_SelectSettlersOfType_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSelection(probe, &CallWrongSelectType, true, 600);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
