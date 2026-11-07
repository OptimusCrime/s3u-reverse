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

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

std::uint16_t Ror16(std::uint16_t value) {
    return static_cast<std::uint16_t>((value >> 1) | (value << 15));
}

}  // namespace

using GameWorldRandomFn = std::uint16_t(__fastcall*)(GameWorld* world, void* edx);

std::uint16_t __fastcall GameWorld_Random(GameWorld* world, void* /*edx*/) {
    std::uint16_t* state = &WorldField<std::uint16_t>(world, world_offset::kRandomState);
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
                                            world_offset::kRandomState);
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
    std::uint16_t* state = &WorldField<std::uint16_t>(world, world_offset::kRandomState);
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

}  // namespace s3re::game
