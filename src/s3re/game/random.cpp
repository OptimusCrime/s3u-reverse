// GameWorld::Random (0x004feb70): the simulation's random number generator.
//
// Three 16-bit words of state live inside the world object (so the sequence
// is part of the game state, which is what makes lockstep multiplayer and
// replays possible). Each call:
//
//   result = (a + b) ^ c      ; becomes the new a and is returned
//   t      = c + b
//   b      = ror16(b ^ t, 1)
//   c      = ror16(t, 1)
//
// Original (thiscall, ECX = world):
//   mov  ax, [ecx+2D2DEh]        ; b
//   mov  dx, [ecx+2D2E0h]        ; c
//   push esi
//   mov  si, ax
//   add  si, [ecx+2D2DCh]        ; b + a
//   xor  esi, edx                ; ^ c
//   add  edx, eax                ; t = c + b
//   xor  ax, dx                  ; b ^ t
//   mov  [ecx+2D2E0h], dx
//   ... rotate (b ^ t) right by one into [ecx+2D2DEh]
//   ... rotate t right by one into [ecx+2D2E0h]
//   mov  [ecx+2D2DCh], si
//   mov  ax, si
//   ret
//
// Only AX is defined on return; callers use the low 16 bits.
//
// GameWorld::SeedRandom (0x004feb40) seeds it (thiscall, one stack argument,
// ret 4; only the low 16 bits of the seed are used):
//
//   a = seed, b = seed + 1000, c = seed + 2000
//
// Called from CLogicThread::OnStart: by the world creation function
// (0x005166d0, "creating world") and right after it with the game's seed,
// the 16-bit field +0x88 of the global object at 0x007a7a18, read by
// 0x00437050. That value comes from the game settings; its origin (clock?)
// isn't traced yet.
//
// Our hook logs every seed with its caller. If the environment variable
// S3RE_SEED is set (`s3re_launcher --seed N`), every seed is replaced by N so
// that two games can start from the same random state.

#include "game/game_settings_layout.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/raw.h"
#include "game/world.h"
#include "hooks.h"
#include "log.h"
#include "test/difftest.h"

#include <intrin.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

std::uint16_t Ror16(std::uint16_t value) {
    return static_cast<std::uint16_t>((value >> 1) | (value << 15));
}

}  // namespace

using GameWorldRandomFn = std::uint16_t(__fastcall*)(GameWorld* world, void* edx);

// Purpose: The simulation's random number generator. Everything random in the game world (settler
// decisions, map generation, animations that affect play) draws from it, and because its state
// lives inside the world object, two games with the same seed and the same commands play out
// identically. That is what makes multiplayer lockstep and our replays work.
std::uint16_t __fastcall GameWorld_Random(GameWorld* world, void* /*edx*/) {
    std::uint16_t* state = world->random_state;
    const std::uint16_t a = state[0];
    const std::uint16_t b = state[1];
    const std::uint16_t c = state[2];

    const auto result = static_cast<std::uint16_t>((a + b) ^ c);
    const auto t = static_cast<std::uint16_t>(c + b);
    state[0] = result;
    state[1] = Ror16(static_cast<std::uint16_t>(b ^ t));
    state[2] = Ror16(t);
    return result;
}

S3RE_HOOK(0x004feb70, GameWorld_Random, GameWorldRandomFn, Original_GameWorld_Random,
          GameWorld_Random);

// GameWorld::RandomBelow6 (0x00503880): (Random() & 0xff) % 6, i.e. 0..5
// (EAX, used by all ten callers). Takes no arguments; ECX (the world) is
// passed on to Random. Plain ret.
// Purpose: A random number from 0 to 5, i.e. a random direction on the six-sided tile grid. Used when
// settlers pick a direction to face or wander, and by the landscape update.
std::uint32_t __fastcall GameWorld_RandomBelow6(GameWorld* world, void* /*edx*/) {
    return (GameWorld_Random(world, nullptr) & 0xffu) % 6u;
}

using GameWorldRandomBelowFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

S3RE_HOOK(0x00503880, GameWorld_RandomBelow6, GameWorldRandomBelowFn, Original_GameWorld_RandomBelow6,
          GameWorld_RandomBelow6);

using GameWorldSeedRandomFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t seed);

// The reimplementation, compared against the original by the tests.
// Purpose: Starts the random number generator from a seed at the beginning of a game (each game gets a
// different seed). Our hook also lets the launcher force a seed (--seed), so test runs are
// repeatable.
void __fastcall GameWorld_SeedRandom_Impl(GameWorld* world, void* /*edx*/, std::uint32_t seed) {
    std::uint16_t* state = world->random_state;
    state[0] = static_cast<std::uint16_t>(seed);
    state[1] = static_cast<std::uint16_t>(seed + 1000);
    state[2] = static_cast<std::uint16_t>(seed + 2000);
}

namespace {

// S3RE_SEED as a number, or -1 if unset or invalid.
long SeedOverride() {
    static const long value = [] {
        char text[32] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_SEED", text, sizeof(text));
        if (length == 0 || length >= sizeof(text)) {
            return -1L;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 0);
        return (end != text && *end == '\0' && parsed <= 0xFFFF) ? static_cast<long>(parsed)
                                                                 : -1L;
    }();
    return value;
}

}  // namespace

extern GameWorldSeedRandomFn Original_GameWorld_SeedRandom;

namespace {
std::atomic<long> g_lastSeed{-1};
}  // namespace

long LastSeed() {
    return g_lastSeed.load();
}

// The installed hook: logs the seed, applies the override, then seeds.
void __fastcall GameWorld_SeedRandom(GameWorld* world, void* edx, std::uint32_t seed) {
    const long forced = SeedOverride();
    if (forced >= 0) {
        S3RE_LOG("GameWorld::SeedRandom(world=%p, seed=%u) called from %p; forced to %ld",
                 static_cast<void*>(world), seed & 0xFFFF, _ReturnAddress(), forced);
        seed = static_cast<std::uint32_t>(forced);
    } else {
        S3RE_LOG("GameWorld::SeedRandom(world=%p, seed=%u) called from %p",
                 static_cast<void*>(world), seed & 0xFFFF, _ReturnAddress());
    }
    // Settings that change the start of a game besides the seed. Byte +0xCC
    // of g_gameSettings (0x7a7a18) chooses how CLogicThread::OnStart draws a
    // random race (0x53cf96): Random() % 4 if set (Amazons included), else
    // % 3. GameSetup (0x431ab0) copies it from byte +0x158 of the config
    // object at 0x7a7af0, which is read from the registry at startup
    // (0x40eeef).
    S3RE_LOG("game settings: random race includes Amazons=%u (config byte %u)",
             static_cast<unsigned>(g_gameSettings->randomRaceAmazons),
             static_cast<unsigned>(g_config->random_race_amazons));
    g_lastSeed = static_cast<long>(seed & 0xFFFF);
    if (hooks::OriginalMode()) {
        Original_GameWorld_SeedRandom(world, edx, seed);
    } else {
        GameWorld_SeedRandom_Impl(world, edx, seed);
    }
}

// Stays installed in original mode, for --seed.
S3RE_HOOK_ALWAYS(0x004feb40, GameWorld_SeedRandom, GameWorldSeedRandomFn,
          Original_GameWorld_SeedRandom, GameWorld_SeedRandom);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Bytes around the state that must stay untouched.
constexpr std::size_t kWindowBefore = 64;
constexpr std::size_t kWindowAfter = 64;
constexpr std::size_t kStateSize = 6;

// Runs `candidate` and the original on identical random worlds for
// `callsPerSequence` consecutive calls and compares every result and the
// memory around the state after each call.
void CompareRandom(test::Context& ctx, GameWorldRandomFn candidate, int sequences,
                   int callsPerSequence) {
    // Only the bytes near the state are real; the rest of the (huge) world is
    // never touched, so a small buffer positioned at the right offset is
    // enough. The pointer handed to the function is the virtual world base.
    constexpr std::size_t kBufferSize = kWindowBefore + kStateSize + kWindowAfter;
    std::vector<std::uint8_t> bufferA(kBufferSize);
    std::vector<std::uint8_t> bufferB(kBufferSize);
    auto worldOf = [](std::vector<std::uint8_t>& buffer) {
        return reinterpret_cast<GameWorld*>(buffer.data() + kWindowBefore -
                                            offsetof(GameWorld, random_state));
    };

    for (int sequence = 0; sequence < sequences; ++sequence) {
        ctx.rng().Fill(bufferA.data(), kBufferSize);
        bufferB = bufferA;

        for (int call = 0; call < callsPerSequence; ++call) {
            const std::uint16_t expected = Original_GameWorld_Random(worldOf(bufferA), nullptr);
            const std::uint16_t actual = candidate(worldOf(bufferB), nullptr);

            bool same = S3RE_EXPECT_EQ(expected, actual);
            same &= S3RE_EXPECT_MEM_EQ(bufferA.data(), bufferB.data(), kBufferSize);
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d, call %d", sequence, call);
                return;
            }
        }
    }
}

// Wrong in one corner case only: when b ^ t is odd, the rotated-in top bit
// is dropped.
std::uint16_t __fastcall WrongRandom(GameWorld* world, void* /*edx*/) {
    std::uint16_t* state = world->random_state;
    const std::uint16_t a = state[0];
    const std::uint16_t b = state[1];
    const std::uint16_t c = state[2];
    const auto result = static_cast<std::uint16_t>((a + b) ^ c);
    const auto t = static_cast<std::uint16_t>(c + b);
    state[0] = result;
    state[1] = static_cast<std::uint16_t>((b ^ t) >> 1);
    state[2] = Ror16(t);
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_Random_MatchesOriginal) {
    CompareRandom(ctx, &GameWorld_Random, 2000, 500);
}

S3RE_TEST(GameWorld_Random_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRandom(probe, &WrongRandom, 2000, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_RandomBelow6_MatchesOriginal) {
    constexpr std::size_t kBufferSize = kWindowBefore + kStateSize + kWindowAfter;
    std::vector<std::uint8_t> bufferA(kBufferSize);
    std::vector<std::uint8_t> bufferB(kBufferSize);
    auto worldOf = [](std::vector<std::uint8_t>& buffer) {
        return reinterpret_cast<GameWorld*>(buffer.data() + kWindowBefore - offsetof(GameWorld, random_state));
    };
    for (int sequence = 0; sequence < 200; ++sequence) {
        ctx.rng().Fill(bufferA.data(), kBufferSize);
        bufferB = bufferA;
        for (int call = 0; call < 500; ++call) {
            const std::uint32_t expected = Original_GameWorld_RandomBelow6(worldOf(bufferA), nullptr);
            const std::uint32_t actual = GameWorld_RandomBelow6(worldOf(bufferB), nullptr);
            bool same = S3RE_EXPECT_EQ(expected, actual);
            same &= S3RE_EXPECT_MEM_EQ(bufferA.data(), bufferB.data(), kBufferSize);
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d, call %d", sequence, call);
                return;
            }
        }
    }
}

namespace {

void CompareSeedRandom(test::Context& ctx, GameWorldSeedRandomFn candidate, int cases) {
    constexpr std::size_t kBufferSize = kWindowBefore + kStateSize + kWindowAfter;
    std::vector<std::uint8_t> bufferA(kBufferSize);
    std::vector<std::uint8_t> bufferB(kBufferSize);
    auto worldOf = [](std::vector<std::uint8_t>& buffer) {
        return reinterpret_cast<GameWorld*>(buffer.data() + kWindowBefore -
                                            offsetof(GameWorld, random_state));
    };
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(bufferA.data(), kBufferSize);
        bufferB = bufferA;
        // Mostly full 32-bit values (the high half must be ignored), plus
        // values near the 16-bit wrap-around.
        const std::uint32_t seed = ctx.rng().Chance(1, 4)
                                       ? 0xFFFFu - ctx.rng().Below(2100)
                                       : ctx.rng().Next32();
        Original_GameWorld_SeedRandom(worldOf(bufferA), nullptr, seed);
        candidate(worldOf(bufferB), nullptr, seed);
        if (!S3RE_EXPECT_MEM_EQ(bufferA.data(), bufferB.data(), kBufferSize)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, seed 0x%08x", i, seed);
            return;
        }
    }
}

// Wrong: b gets seed + 100 instead of seed + 1000.
void __fastcall WrongSeedRandom(GameWorld* world, void* /*edx*/, std::uint32_t seed) {
    std::uint16_t* state = world->random_state;
    state[0] = static_cast<std::uint16_t>(seed);
    state[1] = static_cast<std::uint16_t>(seed + 100);
    state[2] = static_cast<std::uint16_t>(seed + 2000);
}

}  // namespace

S3RE_TEST(GameWorld_SeedRandom_MatchesOriginal) {
    CompareSeedRandom(ctx, &GameWorld_SeedRandom_Impl, 100000);
}

S3RE_TEST(GameWorld_SeedRandom_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSeedRandom(probe, &WrongSeedRandom, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
