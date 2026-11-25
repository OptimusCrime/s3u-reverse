// The computer players' settler census (Rainer\Cevaland.cpp;
// re/computer_players.md "Census"): AI::RunMilitary 0x58f990 calls it
// every so many steps; it counts the settlers of every player in slices.
//
//   0x0055b880 AI::Census::CountSettlers() thiscall (on g_aiCensus): one
//              step of a nine-step state machine (g_aiCensusStep 0x7e06e0;
//              the slice cursor g_aiCensusCursor 0x7e0578; slice = (settler
//              high-water mark + 1) / 8, unsigned):
//              step 0 clears the counts (soldiers[20][9], other settlers,
//              pioneers, geologists, thieves, all settlers) and empties the
//              catapult vectors (g_aiCatapultVectors: end = begin), sets
//              settlers_counted (+0) to 0 and goes to step 1 (no counting);
//              step 1 counts settlers 1..slice, steps 2..7 the next slice
//              each, step 8 the rest up to the high-water mark, then sets
//              +2 and settlers_counted to 1 and goes back to step 0. A map
//              with a high-water mark below 49 is counted in one go every
//              time (clear, step 0, count all, +2 and +0 = 1). Counted are
//              settlers whose owner is below 20: by type into the soldier
//              kinds (Bowman1..3, Pikeman1..3, Swordsman1..3), geologists
//              (22), thieves (23), pioneers (40) or the other settlers; a
//              catapult (41) is appended to its owner's vector instead
//              (std::vector<u32>::insert at the end, MSVC 6: in place when
//              (capacity - end) >> 2, signed then unsigned, allows; else a
//              new block of size + max(size, 1) entries from operator new
//              0x61efb9, the old block freed with operator delete 0x61e4ef);
//              every counted settler also counts in all_settlers. A step
//              number above 8 (never written by the game) makes the
//              original count a range from uninitialized stack values; ours
//              counts nothing then (unless the small-map path applies,
//              which is defined). EAX is not used by the caller.
//   0x0055c020 AI::UninitializedFillPtrs(first, n, const u32* value)
//              stdcall, ret 0xc: stores *value into n dwords from `first`
//              (n unsigned, 0 does nothing: EAX = 0), skipping a store at
//              address 0 (the check is per element, so a null range writes
//              from address 4 on; std::uninitialized_fill_n). EAX = first +
//              4 * n.

#include "game/ai/ai_layout.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::uint32_t kSlices = 8;
constexpr std::uint32_t kLastStep = 8;
constexpr std::uint32_t kSmallMap = 50;             // fewer settler slots (high-water mark + 1): counted at once
constexpr std::uintptr_t kOperatorNew = 0x0061efb9;
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;

// The census soldier kind of a settler type (soldiers[owner][kind]), or -1.
int SoldierKind(std::uint8_t type) {
    switch (static_cast<SettlerType>(type)) {
    case SettlerType::Bowman1: return 0;
    case SettlerType::Bowman2: return 1;
    case SettlerType::Bowman3: return 2;
    case SettlerType::Pikeman1: return 3;
    case SettlerType::Pikeman2: return 4;
    case SettlerType::Pikeman3: return 5;
    case SettlerType::Swordsman1: return 6;
    case SettlerType::Swordsman2: return 7;
    case SettlerType::Swordsman3: return 8;
    default: return -1;
    }
}

// 0x55b8ad..0x55b92e (and 0x55b9ba..0x55ba45): the counts cleared, the
// catapult vectors emptied.
void ClearCounts(AiAgentData* census) {
    std::memset(census->soldiers, 0, sizeof(census->soldiers));
    std::memset(census->other_settlers, 0, sizeof(census->other_settlers));
    std::memset(census->pioneers, 0, sizeof(census->pioneers));
    std::memset(census->geologists, 0, sizeof(census->geologists));
    std::memset(census->thieves, 0, sizeof(census->thieves));
    std::memset(census->all_settlers, 0, sizeof(census->all_settlers));
    for (AiVector& catapults : g_aiCatapultVectors.get()) {
        catapults.end = catapults.begin;  // erase(begin, end): nothing moves
    }
    census->settlers_counted = 0;
}

// 0x55bbac..0x55bd18: the vector's insert(end(), 1, settler).
void AppendCatapult(AiVector& vector, std::uint32_t settler) {
    const std::uint32_t end = vector.end;
    const auto room = static_cast<std::uint32_t>(static_cast<std::int32_t>(vector.unk8 - end) >> 2);
    if (room >= 1) {
        if (end != 0) {
            *reinterpret_cast<std::uint32_t*>(end) = settler;  // UninitializedFillPtrs 0x55c020
        }
        vector.end += 4;
        return;
    }
    const std::uint32_t begin = vector.begin;
    const auto size = [&]() {  // AI::PtrVector::size 0x55bfc0
        return vector.begin == 0 ? 0u
                                 : static_cast<std::uint32_t>(static_cast<std::int32_t>(vector.end - vector.begin) >> 2);
    };
    std::uint32_t grow = 1;
    if (begin != 0) {
        const auto used = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - begin) >> 2);
        if (used > 1) {
            grow = used;
        }
    }
    const auto capacity = static_cast<std::int32_t>(size() + grow);
    using NewFn = std::uint32_t*(__cdecl*)(std::uint32_t bytes);
    std::uint32_t* block =
        reinterpret_cast<NewFn>(kOperatorNew)(static_cast<std::uint32_t>(capacity < 0 ? 0 : capacity) * 4);
    std::uint32_t* out = block;
    for (std::uint32_t in = begin; in != end; in += 4, ++out) {  // UninitializedCopyPtrs 0x55bff0
        if (out != nullptr) {
            *out = *reinterpret_cast<const std::uint32_t*>(in);
        }
    }
    if (out != nullptr) {
        *out = settler;
    }
    using DeleteFn = void(__cdecl*)(void* block);
    reinterpret_cast<DeleteFn>(kOperatorDelete)(reinterpret_cast<void*>(begin));
    vector.unk8 = reinterpret_cast<std::uint32_t>(block) + static_cast<std::uint32_t>(capacity) * 4;
    const std::uint32_t oldSize = size();
    vector.begin = reinterpret_cast<std::uint32_t>(block);
    vector.end = reinterpret_cast<std::uint32_t>(block) + oldSize * 4 + 4;
}

void CountSettler(AiAgentData* census, std::uint32_t id) {
    const layout::Settler& settler = g_aiMapWorld.get()->settlers[id];
    const std::uint8_t owner = settler.owner;
    if (owner >= kPlayers) {
        return;
    }
    const int kind = SoldierKind(settler.art);
    if (kind >= 0) {
        ++census->soldiers[owner][kind];
    } else {
        switch (static_cast<SettlerType>(settler.art)) {
        case SettlerType::Geologist: ++census->geologists[owner]; break;
        case SettlerType::Thief: ++census->thieves[owner]; break;
        case SettlerType::Pioneer: ++census->pioneers[owner]; break;
        case SettlerType::Catapult: AppendCatapult(g_aiCatapultVectors.get()[owner], id); break;
        default: ++census->other_settlers[owner]; break;
        }
    }
    ++census->all_settlers[g_aiMapWorld.get()->settlers[id].owner];
}

}  // namespace

using CountSettlersFn = std::uint32_t(__fastcall*)(AiAgentData* self, void* edx);
using FillFn = std::uint32_t(__stdcall*)(std::uint32_t* first, std::uint32_t n, const std::uint32_t* value);

std::uint32_t __fastcall AI_Census_CountSettlers(AiAgentData* self, void* edx);
std::uint32_t __stdcall AI_UninitializedFillPtrs(std::uint32_t* first, std::uint32_t n, const std::uint32_t* value);

S3RE_HOOK(0x0055b880, AI_Census_CountSettlers, CountSettlersFn, Original_AI_Census_CountSettlers,
          AI_Census_CountSettlers);
S3RE_HOOK(0x0055c020, AI_UninitializedFillPtrs, FillFn, Original_AI_UninitializedFillPtrs, AI_UninitializedFillPtrs);

// Purpose: Counts the settlers of every player for the computer players
// (soldiers by kind, geologists, thieves, pioneers, catapults), a slice of
// the settler array per call.
std::uint32_t __fastcall AI_Census_CountSettlers(AiAgentData* self, void* /*edx*/) {
    const std::uint32_t slice = (g_aiMapWorld.get()->settler_high_water + 1) >> 3;
    std::uint32_t& step = g_aiCensusStep.get();
    std::uint32_t& cursor = g_aiCensusCursor.get();
    std::int32_t first = 0;
    std::int32_t end = 0;
    bool defined = true;
    switch (step) {
    case 0:
        ClearCounts(self);
        ++step;
        return 0;
    case 1:
        cursor = slice + 1;
        step = 2;
        first = 1;
        end = static_cast<std::int32_t>(cursor);
        break;
    case kLastStep:
        step = 0;
        first = static_cast<std::int32_t>(cursor + 1);
        end = static_cast<std::int32_t>(g_aiMapWorld.get()->settler_high_water + 1);
        self->unk002 = 1;
        self->settlers_counted = 1;
        break;
    default:
        if (step < kLastStep) {  // steps 2..7
            ++step;
            first = static_cast<std::int32_t>(cursor + 1);
            cursor = slice + cursor + 1;
            end = static_cast<std::int32_t>(cursor);
        } else {
            defined = false;  // the original's range is uninitialized stack (see the header)
        }
        break;
    }
    static_assert(kSlices == 8);
    if (g_aiMapWorld.get()->settler_high_water + 1 < kSmallMap) {
        step = 0;
        ClearCounts(self);
        first = 1;
        self->unk002 = 1;
        end = static_cast<std::int32_t>(g_aiMapWorld.get()->settler_high_water + 1);
        self->settlers_counted = 1;
        defined = true;
    }
    if (!defined) {
        return 0;
    }
    for (std::int32_t id = first; id < end; ++id) {
        CountSettler(self, static_cast<std::uint32_t>(id));
    }
    return 0;
}

// Purpose: std::uninitialized_fill_n for 4-byte elements, used by the
// computer players' vector code.
std::uint32_t __stdcall AI_UninitializedFillPtrs(std::uint32_t* first, std::uint32_t n, const std::uint32_t* value) {
    if (n == 0) {
        return 0;  // EAX = n
    }
    // The null check is made per element on the advancing address (so only
    // the first store of a null range is skipped, like the original).
    auto at = reinterpret_cast<std::uint32_t>(first);
    for (std::uint32_t k = 0; k < n; ++k, at += 4) {
        if (at != 0) {
            *reinterpret_cast<std::uint32_t*>(at) = *value;
        }
    }
    return at;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// CountSettlers on the real g_aiCensus with a test world (g_aiMapWorld):
// random owners and types, a high-water mark below or above 49, a random
// step and cursor; the catapult vectors are built fresh before each run in
// blocks of the game's operator new (the growth frees them) and compared
// by contents.

namespace {

constexpr std::uint32_t kTestSettlers = 400;
constexpr std::uint32_t kTestVectorEntries = 4;

std::vector<std::uint8_t> Bytes(const void* start, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(start);
    return std::vector<std::uint8_t>(p, p + size);
}

struct VectorStart {
    std::vector<std::uint32_t> entries;
    std::uint32_t capacity = 0;
    bool none = false;
};

void* GameNew(std::uint32_t bytes) {
    using NewFn = void*(__cdecl*)(std::uint32_t bytes);
    return reinterpret_cast<NewFn>(kOperatorNew)(bytes);
}

void GameDelete(void* block) {
    using DeleteFn = void(__cdecl*)(void* block);
    reinterpret_cast<DeleteFn>(kOperatorDelete)(block);
}

void BuildVectors(const std::vector<VectorStart>& starts) {
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        AiVector& vector = g_aiCatapultVectors.get()[p];
        const VectorStart& start = starts[p];
        if (start.none) {
            vector.begin = vector.end = vector.unk8 = 0;
            continue;
        }
        auto* block = static_cast<std::uint32_t*>(GameNew((start.capacity + 1) * 4));
        std::memcpy(block, start.entries.data(), start.entries.size() * 4);
        vector.begin = reinterpret_cast<std::uint32_t>(block);
        vector.end = vector.begin + static_cast<std::uint32_t>(start.entries.size() * 4);
        vector.unk8 = vector.begin + start.capacity * 4;
    }
}

struct VectorEnd {
    std::vector<std::uint8_t> entries;
    std::uint32_t capacity;
    bool operator==(const VectorEnd& o) const { return entries == o.entries && capacity == o.capacity; }
};

std::vector<VectorEnd> TakeVectors() {
    std::vector<VectorEnd> ends;
    for (AiVector& vector : g_aiCatapultVectors.get()) {
        VectorEnd end{{}, 0};
        if (vector.begin != 0) {
            end.entries = Bytes(reinterpret_cast<const void*>(vector.begin), vector.end - vector.begin);
            end.capacity = vector.unk8 - vector.begin;
            GameDelete(reinterpret_cast<void*>(vector.begin));
        }
        vector.begin = vector.end = vector.unk8 = 0;
        ends.push_back(end);
    }
    return ends;
}

void CompareCountSettlers(test::Context& ctx, CountSettlersFn candidate, int cases) {
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    CGameData* savedMapWorld = g_aiMapWorld;
    const std::vector<std::uint8_t> savedCensus = Bytes(&g_aiCensus.get(), sizeof(AiAgentData));
    const std::vector<std::uint8_t> savedVectors = Bytes(&g_aiCatapultVectors.get(), sizeof(AiVector) * kPlayers);
    const std::uint32_t savedStep = g_aiCensusStep;
    const std::uint32_t savedCursor = g_aiCensusCursor;
    g_aiMapWorld = world;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        world->settler_high_water = rng.Chance(1, 3) ? rng.Below(60) : rng.Below(kTestSettlers - 1);
        for (std::uint32_t id = 1; id < kTestSettlers; ++id) {
            layout::Settler& settler = world->settlers[id];
            settler.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 18 + rng.Below(10) : rng.Below(4));
            settler.art = static_cast<std::uint8_t>(rng.Chance(1, 6) ? Value(SettlerType::Catapult) : rng.Below(50));
        }
        std::vector<std::uint8_t> census(sizeof(AiAgentData));
        rng.Fill(census.data(), census.size());
        std::vector<VectorStart> vectors(kPlayers);
        for (VectorStart& v : vectors) {
            v.none = rng.Chance(1, 4);
            const std::uint32_t count = rng.Below(kTestVectorEntries);
            for (std::uint32_t k = 0; k < count; ++k) {
                v.entries.push_back(rng.Next32());
            }
            v.capacity = count + (rng.Chance(1, 2) ? 0 : rng.Below(3));
        }
        const std::uint32_t step = rng.Chance(1, 10) ? 0 : rng.Below(kLastStep + 1);
        const std::uint32_t cursor = rng.Below(kTestSettlers / 2);
        struct Outcome {
            std::vector<std::uint8_t> census;
            std::vector<VectorEnd> vectors;
            std::uint32_t step, cursor;
        };
        auto run = [&](CountSettlersFn f) {
            std::memcpy(&g_aiCensus.get(), census.data(), census.size());
            BuildVectors(vectors);
            g_aiCensusStep = step;
            g_aiCensusCursor = cursor;
            f(&g_aiCensus.get(), nullptr);
            return Outcome{Bytes(&g_aiCensus.get(), sizeof(AiAgentData)), TakeVectors(), g_aiCensusStep,
                           g_aiCensusCursor};
        };
        const Outcome a = run(Original_AI_Census_CountSettlers);
        const Outcome b = run(candidate);
        bool same = S3RE_EXPECT(a.census == b.census);
        same &= S3RE_EXPECT(a.vectors == b.vectors);
        same &= S3RE_EXPECT_EQ(a.step, b.step);
        same &= S3RE_EXPECT_EQ(a.cursor, b.cursor);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, step %u", i, step);
        }
    }
    std::memcpy(&g_aiCensus.get(), savedCensus.data(), savedCensus.size());
    std::memcpy(&g_aiCatapultVectors.get(), savedVectors.data(), savedVectors.size());
    g_aiCensusStep = savedStep;
    g_aiCensusCursor = savedCursor;
    g_aiMapWorld = savedMapWorld;
}

std::uint32_t __fastcall WrongCountSettlers(AiAgentData* self, void* edx) {
    // Wrong: the last slice stops one short.
    if (g_aiCensusStep == kLastStep && g_aiMapWorld.get()->settler_high_water + 1 >= kSmallMap) {
        --g_aiMapWorld.get()->settler_high_water;
        const std::uint32_t result = AI_Census_CountSettlers(self, edx);
        ++g_aiMapWorld.get()->settler_high_water;
        return result;
    }
    return AI_Census_CountSettlers(self, edx);
}

}  // namespace

S3RE_TEST(AiCensusSettlers_MatchOriginal) {
    CompareCountSettlers(ctx, &AI_Census_CountSettlers, 2000);
    // UninitializedFillPtrs on guarded buffers (and a null destination).
    for (int i = 0; i < 500 && ctx.failures() == 0; ++i) {
        std::uint32_t a[12], b[12];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t n = ctx.rng().Below(9);
        const bool null = ctx.rng().Chance(1, 5) && n <= 1;  // (a longer null range writes to address 4)
        const std::uint32_t ra = Original_AI_UninitializedFillPtrs(null ? nullptr : a + 1, n, &value);
        const std::uint32_t rb = AI_UninitializedFillPtrs(null ? nullptr : b + 1, n, &value);
        bool same = S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        same &= S3RE_EXPECT_EQ(null || n == 0 ? ra : ra - reinterpret_cast<std::uint32_t>(a),
                               null || n == 0 ? rb : rb - reinterpret_cast<std::uint32_t>(b));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  fill case %d", i);
        }
    }
}

S3RE_TEST(AiCensusSettlers_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareCountSettlers(quiet, &WrongCountSettlers, 2000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
