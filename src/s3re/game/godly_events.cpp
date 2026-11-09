// God events (spells), from the original source file
// Volker\GodlyEvents.cpp (see re/source_files.md).
//
// Player records: 0x148 bytes at world+0x11943dc; +0x4 race, +0x8c mana,
// +0x94 int32[8] mana needed per spell slot (Player::race / mana /
// spell_mana). The spells of each race are
// listed in the table at 0x00658d64 in S3.EXE (8 int32 per race, 32-byte
// stride), e.g. race 0: 5, 3, 9, 10, 21, 18, 8, 11.
//
// GameWorld::HasManaForSpell (0x0054b240): (player, spell): slot = the
//   index of `spell` in the race's list (8 if it isn't there, which reads
//   the dword after the list); DebugLog("xxx Godevent mana needed: %u at
//   slot: %u", needed, slot); returns mana >= needed (unsigned) in AL. The
//   other bits of EAX are those of `needed` (setae only writes AL),
//   reproduced. 18 call sites in the AI code around 0x548340. ret 8.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

using layout::Player;

constexpr std::uint32_t kSpellSlots = 8;
static_assert(sizeof(Player::spell_mana) == kSpellSlots * sizeof(std::int32_t));

// int32[race][8]: the spells of each race, in S3.EXE.
constexpr std::uintptr_t kRaceSpells = 0x00658d64;
constexpr std::uintptr_t kManaNeededMessage = 0x007311d0;  // "xxx Godevent mana needed: %u at slot: %u"

}  // namespace

using HasManaFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player,
                                             std::uint32_t spell);

namespace {

template <bool kSigned>
std::uint32_t HasManaForSpell(GameWorld* world, std::uint32_t player, std::uint32_t spell) {
    Player& record = world->players[player];  // player is not checked
    const auto race = static_cast<std::uint32_t>(record.race);
    // The original walks from kRaceSpells + race * 32 (32-bit address arithmetic).
    const auto* spells = reinterpret_cast<const std::uint32_t*>(kRaceSpells + race * 32u);
    std::uint32_t slot = 0;
    while (slot < kSpellSlots && spells[slot] != spell) {
        ++slot;
    }
    // Slot 8 (spell not in the list) reads the dword after spell_mana
    // (Player::unk0b4[0]), like the original.
    const auto needed = static_cast<std::uint32_t>(record.spell_mana[slot]);
    GameDebugLog(kManaNeededMessage, needed, slot);
    const auto mana = static_cast<std::uint32_t>(record.mana);
    const bool enough = kSigned ? static_cast<std::int32_t>(mana) >= static_cast<std::int32_t>(needed) : mana >= needed;
    return (needed & 0xffffff00u) | (enough ? 1u : 0u);
}

}  // namespace

// Purpose: Whether a player has enough mana (collected through their temples) to cast a given spell ('god
// event'). Used by the computer players before they cast.
std::uint32_t __fastcall GameWorld_HasManaForSpell(GameWorld* world, void*, std::uint32_t player,
                                                   std::uint32_t spell) {
    return HasManaForSpell<false>(world, player, spell);
}

S3RE_HOOK(0x0054b240, GameWorld_HasManaForSpell, HasManaFn, Original_GameWorld_HasManaForSpell,
          GameWorld_HasManaForSpell);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestPlayers = 20;
constexpr std::size_t kTestWorld = offsetof(GameWorld, players) + (kTestPlayers + 1) * sizeof(Player);

// Wrong: compares signed.
std::uint32_t __fastcall WrongHasManaForSpell(GameWorld* world, void*, std::uint32_t player, std::uint32_t spell) {
    return HasManaForSpell<true>(world, player, spell);
}

void CompareHasMana(test::Context& ctx, HasManaFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t player = ctx.rng().Below(kTestPlayers);
        const std::uint32_t race = ctx.rng().Below(5);
        worlds.Both([&](GameWorld* w) { w->players[player].race = static_cast<std::int32_t>(race); });
        const auto* spells = reinterpret_cast<const std::uint32_t*>(kRaceSpells + race * 32u);
        const std::uint32_t spell = ctx.rng().Chance(4, 5) ? spells[ctx.rng().Below(kSpellSlots)] : ctx.rng().Below(40);
        auto value = [&] {
            switch (ctx.rng().Below(3)) {
            case 0:
                return ctx.rng().Next32();
            case 1:
                return ctx.rng().Below(0x200);
            default:
                return ctx.rng().Next32() | 0x80000000u;
            }
        };
        const auto mana = static_cast<std::int32_t>(value());
        worlds.Both([&](GameWorld* w) { w->players[player].mana = mana; });
        // Slots 0..7 and the dword after them (read for a spell not in the list).
        for (std::uint32_t slot = 0; slot <= kSpellSlots; ++slot) {
            const auto needed = static_cast<std::int32_t>(value());
            worlds.Both([&](GameWorld* w) { w->players[player].spell_mana[slot] = needed; });
        }
        const std::uint32_t expected = Original_GameWorld_HasManaForSpell(worlds.original(), nullptr, player, spell);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player, spell);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, spell %u", i, player, spell);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(GameWorld_HasManaForSpell_MatchesOriginal) {
    CompareHasMana(ctx, &GameWorld_HasManaForSpell, 20000);
}
S3RE_TEST(GameWorld_HasManaForSpell_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareHasMana(probe, &WrongHasManaForSpell, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
