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
// GameWorld::SettlerStartHealth (0x00510470): (settler) -> the start health
//   of the settler's type (+0x1); 20 less for race 3 (+0x2) when the type's
//   class is 1..9 (signed). EAX: the table's dword - 20, else the table's
//   word in AX with the upper half of settler << 6 (race 3: of type * 4,
//   i.e. 0) (callers use AL/AX).
//   ret 4.
// GameWorld::CreateSettler (0x0050c9e0): (x, y, type, player) -> the new
//   settler's number, or 0 if none of 1..0x7cfe is free. Raises the
//   high-water mark (unsigned compare), fills the record (race = low byte of
//   the int32 at player record +0x4, world+0x11943e0 + player*0x148; health
//   = SettlerStartHealth; direction = RandomBelow6; counter = Random & 0x1f;
//   the other fields listed above and bytes +0x14..+0x1a, +0x2c cleared)
//   and puts it on the tile (x, y). ret 0x10.
// GameWorld::ClearSettlerJob (0x00510440): (settler): words +0xe, +0xc and
//   +0x22 = 0. EAX = world + settler * 0x40. ret 4.
// GameWorld::RestoreSettlerDirection (0x0050e380): (settler): direction +0x8
//   = the saved +0x1c, counter +0xc = the saved +0x20. EAX = world +
//   settler * 0x40. ret 4.
// GameWorld::IsSettlerJob01Or37 (0x004fa6b0): (job) -> AL = 1 if job (a
//   settler's word +0xe, full 32-bit compare) is 1 or 0x37, else 0; the
//   upper bits of EAX are the argument's. ECX unused. ret 4.
// GameWorld::JobAfter01Or37 (0x004faa40): (job) -> 2 for job 1, 0x35 for
//   job 0x37, else 0. ECX unused. ret 4. Both are used by 0x500860 (a
//   settler update, probably movement: it calls the first and, if true,
//   saves the position in +0x24/+0x26 and sets the job to the second);
//   0x4a33d0 (Dirk\Pathing.cpp) calls the first on a tile's settler.
// All thiscall (ECX = world). Names provisional.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kSettlers = 0x12a5e60;
constexpr std::uint32_t kSettlerBytes = 0x40;
constexpr std::uint32_t kSettlerHighWater = 0x12a5e5c;
constexpr std::uint32_t kLastSettler = 0x7cff;  // never handed out
constexpr std::uint8_t kFreeSettler = 0xff;
constexpr std::uint32_t kPlayerRace = 0x11943e0;  // int32 at player record +0x4
constexpr std::uint32_t kPlayerRecordBytes = 0x148;

const std::int32_t* const kTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);
const std::int32_t* const kTypeStartHealth = reinterpret_cast<const std::int32_t*>(0x006590e4);

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Settler(std::uint32_t settler) {
    return kSettlers + settler * kSettlerBytes;
}

std::uint32_t WorldAddress(GameWorld* world) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

}  // namespace

using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);
using CreateSettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t type, std::uint32_t player);

// Purpose: The health a new settler starts with, by its type (soldiers of one race start a little weaker).
std::uint32_t __fastcall GameWorld_SettlerStartHealth(GameWorld* world, void*, std::uint32_t settler) {
    const std::uint32_t record = Settler(settler);
    const std::uint32_t type = At<std::uint8_t>(world, record + 0x1);
    // The upper half of EAX: what the original's register held before the word load.
    std::uint32_t upper = (settler << 6) & 0xffff0000u;
    if (At<std::uint8_t>(world, record + 0x2) == 3) {
        const std::int32_t typeClass = kTypeClass[type];
        if (typeClass >= 1 && typeClass <= 9) {
            return static_cast<std::uint32_t>(kTypeStartHealth[type] - 20);
        }
        upper = 0;  // type * 4
    }
    const std::uint32_t health = *reinterpret_cast<const std::uint16_t*>(&kTypeStartHealth[type]);
    return upper | health;
}

// Purpose: Creates a new settler (or ship) of a type for a player on a tile: takes a free settler record,
// fills in its starting values and puts it on the map.
std::uint32_t __fastcall GameWorld_CreateSettler(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t type, std::uint32_t player) {
    std::uint32_t settler = 1;
    while (At<std::uint8_t>(world, Settler(settler)) != kFreeSettler) {
        if (settler == kLastSettler) {
            return 0;
        }
        ++settler;
    }
    if (settler == kLastSettler) {
        return 0;
    }
    if (settler > At<std::uint32_t>(world, kSettlerHighWater)) {
        At<std::uint32_t>(world, kSettlerHighWater) = settler;
    }
    const std::uint32_t record = Settler(settler);
    At<std::uint8_t>(world, record + 0x2) =
        static_cast<std::uint8_t>(At<std::uint32_t>(world, kPlayerRace + player * kPlayerRecordBytes));
    At<std::uint16_t>(world, record + 0x4) = static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, record + 0x6) = static_cast<std::uint16_t>(y);
    At<std::uint8_t>(world, record + 0x1) = static_cast<std::uint8_t>(type);
    At<std::uint8_t>(world, record + 0x3) = static_cast<std::uint8_t>(GameWorld_SettlerStartHealth(world, nullptr, settler));
    At<std::uint8_t>(world, record + 0x0) = static_cast<std::uint8_t>(player);
    At<std::uint16_t>(world, record + 0xe) = 0;
    At<std::uint32_t>(world, record + 0x8) = GameWorld_RandomBelow6(world, nullptr);
    At<std::uint16_t>(world, record + 0xc) = static_cast<std::uint16_t>(GameWorld_Random(world, nullptr) & 0x1f);
    At<std::uint16_t>(world, record + 0x10) = 0;
    At<std::uint16_t>(world, record + 0x12) = 0;
    for (std::uint32_t field = 0x15; field <= 0x1a; ++field) {
        At<std::uint8_t>(world, record + field) = 0;
    }
    At<std::uint32_t>(world, record + 0x1c) = 0;
    At<std::uint16_t>(world, record + 0x20) = 0;
    At<std::uint16_t>(world, record + 0x22) = 0;
    At<std::uint8_t>(world, record + 0x14) = 0;
    At<std::uint8_t>(world, record + 0x2c) = 0;
    At<std::uint16_t>(world, kTiles + (x * 0x300 + y) * 24) = static_cast<std::uint16_t>(settler);
    return settler;
}

// Purpose: Clears what a settler is doing (its job, its counter and the building it belongs to), probably
// when it becomes idle.
std::uint32_t __fastcall GameWorld_ClearSettlerJob(GameWorld* world, void*, std::uint32_t settler) {
    const std::uint32_t record = Settler(settler);
    At<std::uint16_t>(world, record + 0xe) = 0;
    At<std::uint16_t>(world, record + 0xc) = 0;
    At<std::uint16_t>(world, record + 0x22) = 0;
    return WorldAddress(world) + settler * kSettlerBytes;
}

// Purpose: Gives a settler back the direction and counter it saved earlier (probably after an
// interruption).
std::uint32_t __fastcall GameWorld_RestoreSettlerDirection(GameWorld* world, void*, std::uint32_t settler) {
    const std::uint32_t record = Settler(settler);
    At<std::uint32_t>(world, record + 0x8) = At<std::uint32_t>(world, record + 0x1c);
    At<std::uint16_t>(world, record + 0xc) = At<std::uint16_t>(world, record + 0x20);
    return WorldAddress(world) + settler * kSettlerBytes;
}

S3RE_HOOK(0x00510470, GameWorld_SettlerStartHealth, SettlerFn, Original_GameWorld_SettlerStartHealth,
          GameWorld_SettlerStartHealth);
S3RE_HOOK(0x0050c9e0, GameWorld_CreateSettler, CreateSettlerFn, Original_GameWorld_CreateSettler,
          GameWorld_CreateSettler);
S3RE_HOOK(0x00510440, GameWorld_ClearSettlerJob, SettlerFn, Original_GameWorld_ClearSettlerJob,
          GameWorld_ClearSettlerJob);
S3RE_HOOK(0x0050e380, GameWorld_RestoreSettlerDirection, SettlerFn, Original_GameWorld_RestoreSettlerDirection,
          GameWorld_RestoreSettlerDirection);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = kSettlers + (kLastSettler + 1) * kSettlerBytes;

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
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
    worlds.Set<std::uint8_t>(Settler(settler) + 0x2,
                             static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 3 : ctx.rng().Below(256)));
    worlds.Set<std::uint8_t>(Settler(settler) + 0x1,
                             static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? ctx.rng().Below(0x50) : ctx.rng().Below(256)));
}

// Wrong: no health malus for race 3.
std::uint32_t __fastcall WrongSettlerStartHealth(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint8_t race = At<std::uint8_t>(world, Settler(settler) + 0x2);
    At<std::uint8_t>(world, Settler(settler) + 0x2) = 0;
    const std::uint32_t result = GameWorld_SettlerStartHealth(world, edx, settler);
    At<std::uint8_t>(world, Settler(settler) + 0x2) = race;
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
        if (At<std::uint8_t>(worlds.original(), Settler(s)) == kFreeSettler) {
            worlds.Set<std::uint8_t>(Settler(s), static_cast<std::uint8_t>(ctx.rng().Below(20)));
        }
    }
    if (first <= kLastSettler) {
        worlds.Set<std::uint8_t>(Settler(first), kFreeSettler);
    }
    const std::uint32_t mark = ctx.rng().Chance(1, 2) ? first - 1 + ctx.rng().Below(3) : ctx.rng().Next32();
    worlds.Set<std::uint32_t>(kSettlerHighWater, mark);
}

// Wrong: does not raise the high-water mark.
std::uint32_t __fastcall WrongCreateSettler(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t type, std::uint32_t player) {
    const std::uint32_t mark = At<std::uint32_t>(world, kSettlerHighWater);
    const std::uint32_t result = GameWorld_CreateSettler(world, edx, x, y, type, player);
    At<std::uint32_t>(world, kSettlerHighWater) = mark;
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
        worlds.Set<std::uint32_t>(kPlayerRace + player * kPlayerRecordBytes,
                                  ctx.rng().Chance(1, 2) ? 3 : ctx.rng().Chance(1, 2) ? ctx.rng().Below(4) : ctx.rng().Next32());
        const std::uint32_t expected = Original_GameWorld_CreateSettler(worlds.original(), nullptr, x, y, type, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, type, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) type %u player %u", i, x, y,
                     type, player);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_SettlerStartHealth_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_GameWorld_SettlerStartHealth, &GameWorld_SettlerStartHealth, 100000,
                     [&](test::WorldPair& worlds, std::uint32_t settler) { SetUpHealthCase(ctx, worlds, settler); });
}
S3RE_TEST(GameWorld_SettlerStartHealth_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSettlerFn(probe, Original_GameWorld_SettlerStartHealth, &WrongSettlerStartHealth, 10000,
                     [&](test::WorldPair& worlds, std::uint32_t settler) { SetUpHealthCase(probe, worlds, settler); });
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_CreateSettler_MatchesOriginal) {
    CompareCreateSettler(ctx, &GameWorld_CreateSettler, 2000);
}
S3RE_TEST(GameWorld_CreateSettler_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCreateSettler(probe, &WrongCreateSettler, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ClearSettlerJob_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_GameWorld_ClearSettlerJob, &GameWorld_ClearSettlerJob, 20000,
                     [](test::WorldPair&, std::uint32_t) {});
}
S3RE_TEST(GameWorld_RestoreSettlerDirection_MatchesOriginal) {
    CompareSettlerFn(ctx, Original_GameWorld_RestoreSettlerDirection, &GameWorld_RestoreSettlerDirection, 20000,
                     [](test::WorldPair&, std::uint32_t) {});
}

// ---------------------------------------------------------------------------
// Job helpers
// ---------------------------------------------------------------------------

using JobFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t job);

// Purpose: Whether a settler's job is one of two (1 or 0x37) that are probably "walking somewhere" states,
// which the settler update and the path finder treat specially.
std::uint32_t __fastcall GameWorld_IsSettlerJob01Or37(GameWorld*, void*, std::uint32_t job) {
    return (job & 0xffffff00u) | ((job == 1 || job == 0x37) ? 1u : 0u);
}

// Purpose: The job that follows job 1 (2) or job 0x37 (0x35), probably once the settler has arrived;
// 0 for other jobs.
std::uint32_t __fastcall GameWorld_JobAfter01Or37(GameWorld*, void*, std::uint32_t job) {
    if (job == 1) {
        return 2;
    }
    if (job == 0x37) {
        return 0x35;
    }
    return 0;
}

S3RE_HOOK(0x004fa6b0, GameWorld_IsSettlerJob01Or37, JobFn, Original_GameWorld_IsSettlerJob01Or37,
          GameWorld_IsSettlerJob01Or37);
S3RE_HOOK(0x004faa40, GameWorld_JobAfter01Or37, JobFn, Original_GameWorld_JobAfter01Or37, GameWorld_JobAfter01Or37);

namespace {

// Jobs 0..0x3ff and random 32-bit values; EAX must match exactly.
void CompareJobFn(test::Context& ctx, JobFn original, JobFn candidate) {
    for (std::uint32_t i = 0; i < 0x2000; ++i) {
        const std::uint32_t job = i < 0x400 ? i : (ctx.rng().Chance(1, 2) ? ctx.rng().Next32()
                                                                            : (ctx.rng().Next32() & ~0xffu) | 0x37);
        if (!S3RE_EXPECT_EQ(original(nullptr, nullptr, job), candidate(nullptr, nullptr, job))) {
            ctx.Fail(__FILE__, __LINE__, "  job 0x%x", job);
            return;
        }
    }
}

// Wrong: compares only the low byte.
std::uint32_t __fastcall WrongIsSettlerJob01Or37(GameWorld*, void*, std::uint32_t job) {
    const std::uint32_t low = job & 0xff;
    return (job & 0xffffff00u) | ((low == 1 || low == 0x37) ? 1u : 0u);
}

}  // namespace

S3RE_TEST(GameWorld_IsSettlerJob01Or37_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_IsSettlerJob01Or37, &GameWorld_IsSettlerJob01Or37);
}
S3RE_TEST(GameWorld_IsSettlerJob01Or37_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJobFn(probe, Original_GameWorld_IsSettlerJob01Or37, &WrongIsSettlerJob01Or37);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_JobAfter01Or37_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_JobAfter01Or37, &GameWorld_JobAfter01Or37);
}

}  // namespace s3re::game
