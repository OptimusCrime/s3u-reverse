// Small getters of the game settings (g_gameSettings, game_settings_layout.h)
// that the simulation calls while a game is set up.
//
// GameSettings::StartGoods (0x00412300): () -> the start goods setting
//   (+0x74; 0 the map's own, 1 low, 2 medium, 3 high). `mov eax,[ecx+74h];
//   ret`, hooked through its padding. Called by CGameData::PlaceTown and the
//   menus.
// All thiscall (ECX = the settings object).

#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

using SettingsGetterFn = std::uint32_t(__fastcall*)(const GameSettings* self, void* edx);

// Purpose: Tells the town placement how many goods and settlers the players start with.
std::uint32_t __fastcall GameSettings_StartGoods(const GameSettings* self, void*) {
    return self->start_goods;
}

S3RE_HOOK(0x00412300, GameSettings_StartGoods, SettingsGetterFn, Original_GameSettings_StartGoods,
          GameSettings_StartGoods);

namespace {

void CompareGetter(test::Context& ctx, SettingsGetterFn original, SettingsGetterFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        GameSettings settings;
        ctx.rng().Fill(&settings, sizeof(settings));
        const std::uint32_t expected = original(&settings, nullptr);
        const std::uint32_t actual = candidate(&settings, nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            return;
        }
    }
}

// Wrong: reads the byte before the field.
std::uint32_t __fastcall WrongStartGoods(const GameSettings* self, void*) {
    std::uint32_t value;
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(self) + offsetof(GameSettings, start_goods) - 1,
                sizeof(value));
    return value;
}

}  // namespace

S3RE_TEST(GameSettings_StartGoods_MatchesOriginal) {
    CompareGetter(ctx, Original_GameSettings_StartGoods, &GameSettings_StartGoods, 1000);
}
S3RE_TEST(GameSettings_StartGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGetter(probe, Original_GameSettings_StartGoods, &WrongStartGoods, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
