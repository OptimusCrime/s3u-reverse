// Settler records: creating a settler and small field resets.
//
// Settlers: 0x40-byte records at world+0x12a5e60 (numbers 1..0x7cfe; byte
// +0x0 player, 0xff = free), high-water mark uint32 at world+0x12a5e5c.
// Fields: +0x1 type, +0x2 race (copied from the player record), +0x3
// health, uint16 +0x4/+0x6 tile, uint32 +0x8 direction (0..5), uint16 +0xc
// a counter (random 0..31 at creation), uint16 +0xe job, uint16 +0x10 /
// +0x12 list links, uint32 +0x1c a saved direction, uint16 +0x20 a saved
// counter, uint16 +0x22 building. A tile's uint16 +0x0 (tile records at
// world+0x67444) is the number of the settler standing on it.
// Settler type tables in S3.EXE's data (int32 per type): 0x658f64 the class
// (1..9 soldiers?, see settlers.cpp), 0x6590e4 the health a new settler
// starts with (20 for most, 100..150 for some, 250 for ships).
//
// CGameData::SettlerStartHealth (0x00510470): (settler) -> the start health
//   of the settler's type (+0x1); 20 less for Amazons (race 3, +0x2) when
//   the type's class is 1..9 (signed; the soldiers). EAX: the table's dword - 20, else the table's
//   word in AX with the upper half of settler << 6 (race 3: of type * 4,
//   i.e. 0) (callers use AL/AX).
//   ret 4.
// CGameData::CreateSettler (0x0050c9e0): (x, y, type, player) -> the new
//   settler's number, or 0 if none of 1..0x7cfe is free. Raises the
//   high-water mark (unsigned compare), fills the record (race = low byte of
//   the int32 at player record +0x4, world+0x11943e0 + player*0x148; health
//   = SettlerStartHealth; direction = RandomBelow6; counter = Random & 0x1f;
//   the other fields listed above and bytes +0x14..+0x1a, +0x2c cleared)
//   and puts it on the tile (x, y). ret 0x10.
// CGameData::ClearSettlerJob (0x00510440): (settler): words +0xe, +0xc and
//   +0x22 = 0. EAX = world + settler * 0x40. ret 4.
// CGameData::RestoreSettlerAnimation (0x0050e380): (settler): animation +0x8
//   = the saved +0x1c, counter +0xc = the saved +0x20. EAX = world +
//   settler * 0x40. ret 4.
// CGameData::IsSettlerJob01Or37 (0x004fa6b0): (job) -> AL = 1 if job (a
//   settler's word +0xe, full 32-bit compare) is Nothing (1) or UnitIdle
//   (55, 0x37), else 0; the upper bits of EAX are the argument's. ECX
//   unused. ret 4.
// CGameData::JobAfter01Or37 (0x004faa40): (job) -> StepAside (2) for job
//   Nothing (1), UnitWalk (53, 0x35) for UnitIdle (55, 0x37), else 0. ECX unused. ret 4. Both are used by 0x500860 (a
//   settler update, probably movement: it calls the first and, if true,
//   saves the position in +0x24/+0x26 and sets the job to the second);
//   0x4a33d0 (Dirk\Pathing.cpp) calls the first on a tile's settler.
// All thiscall (ECX = world). Names provisional.

#include "game/enums/races.h"
#include "game/enums/job_ids.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using layout::Settler;

constexpr std::uint32_t kSettlerBytes = sizeof(Settler);
constexpr std::uint32_t kLastSettler = layout::kMaxSettler + 1;  // 0x7cff, never handed out
constexpr std::uint8_t kFreeSettler = 0xff;

// The soldier classes as SettlerStartHealth tests them (signed 1..9).
constexpr std::int32_t kFirstSoldierClass = 1;
constexpr std::int32_t kLastSoldierClass = 9;
constexpr std::int32_t kAmazonSoldierHealthMalus = 20;
// A new settler's counter: Random() & 31.
constexpr std::uint32_t kCounterMask = 0x1f;

std::uint32_t WorldAddress(CGameData* world) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

}  // namespace

using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);
using CreateSettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t type, std::uint32_t player);

// Purpose: The health a new settler starts with, by its type (soldiers of one race start a little weaker).
std::uint32_t __fastcall CGameData_SettlerStartHealth(CGameData* world, void*, std::uint32_t settler) {
    const Settler& record = world->settlers[settler];
    const std::uint32_t type = record.art;
    // The upper half of EAX: what the original's register held before the word load.
    std::uint32_t upper = (settler << 6) & 0xffff0000u;
    if (RaceOf(record) == Race::Amazon) {
        const std::int32_t typeClass = Value(ClassOf(record));
        if (typeClass >= kFirstSoldierClass && typeClass <= kLastSoldierClass) {
            return static_cast<std::uint32_t>(g_settlerTypeStartHealth[type] - kAmazonSoldierHealthMalus);
        }
        upper = 0;  // type * 4
    }
    const std::uint32_t health = static_cast<std::uint16_t>(g_settlerTypeStartHealth[type]);
    return upper | health;
}

// Purpose: Creates a new settler (or ship) of a type for a player on a tile: takes a free settler record,
// fills in its starting values and puts it on the map.
std::uint32_t __fastcall CGameData_CreateSettler(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t type, std::uint32_t player) {
    std::uint32_t settler = 1;
    while (world->settlers[settler].owner != kFreeSettler) {
        if (settler == kLastSettler) {
            return 0;
        }
        ++settler;
    }
    if (settler == kLastSettler) {
        return 0;
    }
    if (settler > world->settler_high_water) {
        world->settler_high_water = settler;
    }
    Settler& record = world->settlers[settler];
    // Low byte of the int32 player race.
    record.race = static_cast<std::uint8_t>(world->players[player].race);
    record.x = static_cast<std::uint16_t>(x);
    record.y = static_cast<std::uint16_t>(y);
    record.art = static_cast<std::uint8_t>(type);
    record.health = static_cast<std::uint8_t>(CGameData_SettlerStartHealth(world, nullptr, settler));
    record.owner = static_cast<std::uint8_t>(player);
    SetJob(record, SettlerJob::New);
    record.direction = CGameData_RandomBelow6(world, nullptr);
    record.counter = static_cast<std::uint16_t>(CGameData_Random(world, nullptr) & kCounterMask);
    record.next = 0;
    record.prev = 0;
    // Bytes +0x15..+0x1a, in this order.
    record.slot = 0;
    record.frame = 0;
    record.second_unit = 0;
    record.tool_type = 0;
    record.step_time = 0;
    record.good = 0;
    record.saved_animation = 0;
    record.saved_counter = 0;
    record.building = 0;
    record.flags = 0;
    record.path_mode = 0;
    // The original's 32-bit tile arithmetic (x, y are not range-checked).
    TileAtOffset(world, TileOffset(x, y)).settler = static_cast<std::uint16_t>(settler);
    return settler;
}

// Purpose: Clears what a settler is doing (its job, its counter and the building it belongs to), probably
// when it becomes idle.
std::uint32_t __fastcall CGameData_ClearSettlerJob(CGameData* world, void*, std::uint32_t settler) {
    Settler& record = world->settlers[settler];
    SetJob(record, SettlerJob::New);
    record.counter = 0;
    record.building = 0;
    return WorldAddress(world) + settler * kSettlerBytes;
}

// Purpose: Gives a settler back the animation and counter it saved earlier (probably after an
// interruption).
std::uint32_t __fastcall CGameData_RestoreSettlerAnimation(CGameData* world, void*, std::uint32_t settler) {
    Settler& record = world->settlers[settler];
    record.animation = record.saved_animation;
    record.counter = record.saved_counter;
    return WorldAddress(world) + settler * kSettlerBytes;
}

S3RE_HOOK(0x00510470, CGameData_SettlerStartHealth, SettlerFn, Original_CGameData_SettlerStartHealth,
          CGameData_SettlerStartHealth);
S3RE_HOOK(0x0050c9e0, CGameData_CreateSettler, CreateSettlerFn, Original_CGameData_CreateSettler,
          CGameData_CreateSettler);
S3RE_HOOK(0x00510440, CGameData_ClearSettlerJob, SettlerFn, Original_CGameData_ClearSettlerJob,
          CGameData_ClearSettlerJob);
S3RE_HOOK(0x0050e380, CGameData_RestoreSettlerAnimation, SettlerFn, Original_CGameData_RestoreSettlerAnimation,
          CGameData_RestoreSettlerAnimation);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, settlers) + (kLastSettler + 1) * kSettlerBytes;

std::uint32_t Relative(std::uint32_t value, CGameData* world) {
    const std::uint32_t offset = value - WorldAddress(world);
    return offset < kTestWorld ? offset : value;
}

// Compares a settler function on random settlers: EAX (relative to the
// world when it points into it) and the whole world every 256 cases.
template <typename Setup>
void CompareSettlerFn(test::Context& ctx, SettlerFn original, SettlerFn candidate, int cases, Setup setup) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(kLastSettler + 1);
        setup(worlds, settler);
        const std::uint32_t expected = Relative(original(worlds.original(), nullptr, settler), worlds.original());
        const std::uint32_t actual = Relative(candidate(worlds.ours(), nullptr, settler), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): settler %u", i, settler);
            return;
        }
    }
}

void SetUpHealthCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settler) {
    const auto race = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? Value(Race::Amazon) : ctx.rng().Below(256));
    worlds.Both([&](CGameData* w) { w->settlers[settler].race = race; });
    const auto type = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? ctx.rng().Below(0x50) : ctx.rng().Below(256));
    worlds.Both([&](CGameData* w) { w->settlers[settler].art = type; });
}

// Wrong: no health malus for Amazons.
std::uint32_t __fastcall WrongSettlerStartHealth(CGameData* world, void* edx, std::uint32_t settler) {
    const std::uint8_t race = world->settlers[settler].race;
    world->settlers[settler].race = 0;
    const std::uint32_t result = CGameData_SettlerStartHealth(world, edx, settler);
    world->settlers[settler].race = race;
    return result;
}

// Settlers 1..first-1 in use, `first` free (sometimes 0x7cfe, 0x7cff or
// none: all in use).
void SetUpFreeSettler(test::Context& ctx, test::WorldPair& worlds) {
    std::uint32_t first = 1 + ctx.rng().Below(200);
    if (ctx.rng().Chance(1, 40)) {
        static constexpr std::uint32_t kEdges[] = {kLastSettler - 1, kLastSettler, kLastSettler + 1};
        first = kEdges[ctx.rng().Below(3)];
    }
    for (std::uint32_t s = 1; s < first && s <= kLastSettler; ++s) {
        if (worlds.original()->settlers[s].owner == kFreeSettler) {
            const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
            worlds.Both([&](CGameData* w) { w->settlers[s].owner = owner; });
        }
    }
    if (first <= kLastSettler) {
        worlds.Both([&](CGameData* w) { w->settlers[first].owner = kFreeSettler; });
    }
    const std::uint32_t mark = ctx.rng().Chance(1, 2) ? first - 1 + ctx.rng().Below(3) : ctx.rng().Next32();
    worlds.Both([&](CGameData* w) { w->settler_high_water = mark; });
}

// Wrong: does not raise the high-water mark.
std::uint32_t __fastcall WrongCreateSettler(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t type, std::uint32_t player) {
    const std::uint32_t mark = world->settler_high_water;
    const std::uint32_t result = CGameData_CreateSettler(world, edx, x, y, type, player);
    world->settler_high_water = mark;
    return result;
}

void CompareCreateSettler(test::Context& ctx, CreateSettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        SetUpFreeSettler(ctx, worlds);
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t type = ctx.rng().Chance(3, 4) ? ctx.rng().Below(0x50) : ctx.rng().Next32();
        const std::uint32_t player = ctx.rng().Below(20);
        const auto race = static_cast<std::int32_t>(
            ctx.rng().Chance(1, 2) ? Value(Race::Amazon)
                                   : ctx.rng().Chance(1, 2) ? ctx.rng().Below(kRaces) : ctx.rng().Next32());
        worlds.Both([&](CGameData* w) { w->players[player].race = race; });
        const std::uint32_t expected = Original_CGameData_CreateSettler(worlds.original(), nullptr, x, y, type, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, type, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) type %u player %u", i, x, y,
                     type, player);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_SettlerStartHealth_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_CGameData_SettlerStartHealth, &CGameData_SettlerStartHealth, 100000,
                     [&](test::WorldPair& worlds, std::uint32_t settler) { SetUpHealthCase(ctx, worlds, settler); });
}
S3RE_TEST(CGameData_SettlerStartHealth_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSettlerFn(probe, Original_CGameData_SettlerStartHealth, &WrongSettlerStartHealth, 10000,
                     [&](test::WorldPair& worlds, std::uint32_t settler) { SetUpHealthCase(probe, worlds, settler); });
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_CreateSettler_MatchesOriginal) {
    CompareCreateSettler(ctx, &CGameData_CreateSettler, 2000);
}
S3RE_TEST(CGameData_CreateSettler_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCreateSettler(probe, &WrongCreateSettler, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ClearSettlerJob_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_CGameData_ClearSettlerJob, &CGameData_ClearSettlerJob, 20000,
                     [](test::WorldPair&, std::uint32_t) {});
}
S3RE_TEST(CGameData_RestoreSettlerAnimation_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_CGameData_RestoreSettlerAnimation, &CGameData_RestoreSettlerAnimation, 20000,
                     [](test::WorldPair&, std::uint32_t) {});
}

// ---------------------------------------------------------------------------
// Job helpers
// ---------------------------------------------------------------------------

using JobFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t job);

// Purpose: Whether a settler's job is one of two (Nothing 1 or UnitIdle 55, 0x37) that are probably
// "standing around" states, which the settler update and the path finder treat specially.
std::uint32_t __fastcall CGameData_IsSettlerJob01Or37(CGameData*, void*, std::uint32_t job) {
    // Full 32-bit compares; AL holds the result, the rest of EAX is the argument's.
    const bool idle = job == Value(SettlerJob::Nothing) || job == Value(SettlerJob::UnitIdle);
    return (job & 0xffffff00u) | (idle ? 1u : 0u);
}

// Purpose: The job that follows job Nothing (StepAside) or UnitIdle (UnitWalk), probably when the settler
// has to make room; 0 for other jobs.
std::uint32_t __fastcall CGameData_JobAfter01Or37(CGameData*, void*, std::uint32_t job) {
    if (job == Value(SettlerJob::Nothing)) {
        return Value(SettlerJob::StepAside);
    }
    if (job == Value(SettlerJob::UnitIdle)) {
        return Value(SettlerJob::UnitWalk);
    }
    return 0;
}

S3RE_HOOK(0x004fa6b0, CGameData_IsSettlerJob01Or37, JobFn, Original_CGameData_IsSettlerJob01Or37,
          CGameData_IsSettlerJob01Or37);
S3RE_HOOK(0x004faa40, CGameData_JobAfter01Or37, JobFn, Original_CGameData_JobAfter01Or37, CGameData_JobAfter01Or37);

namespace {

// Jobs 0..0x3ff and random 32-bit values; EAX must match exactly.
void CompareJobFn(test::Context& ctx, JobFn original, JobFn candidate) {
    for (std::uint32_t i = 0; i < 0x2000; ++i) {
        const std::uint32_t job = i < 0x400 ? i : (ctx.rng().Chance(1, 2) ? ctx.rng().Next32()
                                                                            : (ctx.rng().Next32() & ~0xffu) | Value(SettlerJob::UnitIdle));
        if (!S3RE_EXPECT_EQ(original(nullptr, nullptr, job), candidate(nullptr, nullptr, job))) {
            ctx.Fail(__FILE__, __LINE__, "  job 0x%x", job);
            return;
        }
    }
}

// Wrong: compares only the low byte.
std::uint32_t __fastcall WrongIsSettlerJob01Or37(CGameData*, void*, std::uint32_t job) {
    const std::uint32_t low = job & 0xff;
    return (job & 0xffffff00u) | ((low == Value(SettlerJob::Nothing) || low == Value(SettlerJob::UnitIdle)) ? 1u : 0u);
}

}  // namespace

S3RE_TEST(CGameData_IsSettlerJob01Or37_MatchesOriginal) {
    CompareJobFn(ctx, Original_CGameData_IsSettlerJob01Or37, &CGameData_IsSettlerJob01Or37);
}
S3RE_TEST(CGameData_IsSettlerJob01Or37_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJobFn(probe, Original_CGameData_IsSettlerJob01Or37, &WrongIsSettlerJob01Or37);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_JobAfter01Or37_MatchesOriginal) {
    CompareJobFn(ctx, Original_CGameData_JobAfter01Or37, &CGameData_JobAfter01Or37);
}

}  // namespace s3re::game
