// Small getters of the game settings (g_gameSettings, game_settings_layout.h)
// that the simulation calls while a game is set up.
//
// GameSettings::StartGoods (0x00412300): () -> the start goods setting
//   (+0x74; 0 the map's own, 1 low, 2 medium, 3 high). `mov eax,[ecx+74h];
//   ret`, hooked through its padding. Called by CGameData::PlaceTown and the
//   menus.
// GamePlayerSetup::Colour (0x0049fec0): () -> the player's colour index
//   (+0xb4 of the 0xcc-byte player setup). `mov eax,[ecx+0B4h]; ret`.
//   Called by CGameData::LoadMap (g_playerColourIndex) and network code.
// All thiscall (ECX = the object).

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

using PlayerSetupGetterFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);

// Purpose: Tells the map loading which colour a player has.
std::uint32_t __fastcall GamePlayerSetup_Colour(const GamePlayerSetup* self, void*) {
    return self->colour;
}

S3RE_HOOK(0x00412300, GameSettings_StartGoods, SettingsGetterFn, Original_GameSettings_StartGoods,
          GameSettings_StartGoods);
S3RE_HOOK(0x0049fec0, GamePlayerSetup_Colour, PlayerSetupGetterFn, Original_GamePlayerSetup_Colour,
          GamePlayerSetup_Colour);

namespace {

template <typename Object, typename Fn>
void CompareGetter(test::Context& ctx, Fn original, Fn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        Object settings;
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

// Wrong: reads the dword after the field.
std::uint32_t __fastcall WrongColour(const GamePlayerSetup* self, void*) {
    std::uint32_t value;
    std::memcpy(&value, self->unk0b8, sizeof(value));
    return value;
}

}  // namespace

S3RE_TEST(GameSettings_StartGoods_MatchesOriginal) {
    CompareGetter<GameSettings>(ctx, Original_GameSettings_StartGoods, &GameSettings_StartGoods, 1000);
}
S3RE_TEST(GameSettings_StartGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGetter<GameSettings>(probe, Original_GameSettings_StartGoods, &WrongStartGoods, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GamePlayerSetup_Colour_MatchesOriginal) {
    CompareGetter<GamePlayerSetup>(ctx, Original_GamePlayerSetup_Colour, &GamePlayerSetup_Colour, 1000);
}
S3RE_TEST(GamePlayerSetup_Colour_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGetter<GamePlayerSetup>(probe, Original_GamePlayerSetup_Colour, &WrongColour, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
