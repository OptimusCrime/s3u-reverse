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
// GameSettings::MapName (0x00436a60): (std::string* result) ret 4 ->
//   result: a copy of the map name (+0x2c). The string copy constructor
//   inlined: the result's allocator byte is the low byte of the result's
//   own address (its stack slot doubles as the empty allocator argument),
//   its fields are cleared, then assign(name, 0, npos): the result shares
//   the name's buffer when that isn't frozen or nearly full (count below
//   0xfe), else gets its own copy. Called by CLogicThread::SaveGame (the
//   savegame header), the game launch dialog and the lobby status.
// All thiscall (ECX = the object).

#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

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

using MapNameFn = msvc6::String*(__fastcall*)(const GameSettings* self, void* edx, msvc6::String* result);

// Purpose: Gives the name of the map being played (written into savegames
// and shown in the game launch dialog and the lobby; probably the map name).
msvc6::String* __fastcall GameSettings_MapName(const GameSettings* self, void*, msvc6::String* result) {
    result->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(result));
    msvc6::detail::Clear(result);
    // 0x436a7e: the inlined assign takes min(length, 0xffffffff) characters,
    // the whole name, as assign(x, 0, npos) does.
    msvc6::String_AssignSubstr(result, nullptr, &self->map_name, 0, msvc6::detail::Npos());
    return result;
}

S3RE_HOOK(0x00412300, GameSettings_StartGoods, SettingsGetterFn, Original_GameSettings_StartGoods,
          GameSettings_StartGoods);
S3RE_HOOK(0x0049fec0, GamePlayerSetup_Colour, PlayerSetupGetterFn, Original_GamePlayerSetup_Colour,
          GamePlayerSetup_Colour);
S3RE_HOOK(0x00436a60, GameSettings_MapName, MapNameFn, Original_GameSettings_MapName, GameSettings_MapName);

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

// MapName on a settings object whose map name is an empty string, a text
// with its buffer unshared, shared, nearly full (0xfd, 0xfe) or frozen: the
// result (allocator byte, fields, shared or not, text) and the name's count
// byte afterwards must match. Each result is destroyed again before the
// next run, which uses the same result object.
struct MapNameOutcome {
    std::uint8_t allocator;
    bool shared;
    std::uint32_t len, res;
    std::string text;
    std::uint8_t countAfter;
    bool operator==(const MapNameOutcome& o) const {
        return allocator == o.allocator && shared == o.shared && len == o.len && res == o.res && text == o.text &&
               countAfter == o.countAfter;
    }
};

MapNameOutcome RunMapName(MapNameFn fn, GameSettings& settings, msvc6::String& result,
                          const std::uint8_t (&resultBytes)[sizeof(msvc6::String)]) {
    std::memcpy(&result, resultBytes, sizeof(result));
    const std::uint8_t count = settings.map_name.ptr != nullptr ? settings.map_name.ptr[-1] : 0;
    fn(&settings, nullptr, &result);
    MapNameOutcome out{result.allocator, result.ptr != nullptr && result.ptr == settings.map_name.ptr, result.len,
                       result.res, result.ptr != nullptr ? std::string(result.ptr, result.len) : std::string(),
                       static_cast<std::uint8_t>(settings.map_name.ptr != nullptr ? settings.map_name.ptr[-1] : 0)};
    if (out.shared) {
        settings.map_name.ptr[-1] = static_cast<char>(count);  // the reference the result took
        msvc6::detail::Clear(&result);
    } else {
        msvc6::String_Destroy(&result, nullptr);
    }
    return out;
}

void CompareMapName(test::Context& ctx, MapNameFn candidate, int cases) {
    alignas(4) std::uint8_t settingsBytes[sizeof(GameSettings)];
    auto& settings = *reinterpret_cast<GameSettings*>(settingsBytes);
    msvc6::String result;
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(settingsBytes, sizeof(settingsBytes));
        std::string text(rng.Below(4) == 0 ? 0 : 1 + rng.Below(60), 'm');
        for (char& c : text) {
            c = static_cast<char>('A' + rng.Below(26));
        }
        msvc6::String_FromChars(&settings.map_name, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
        static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
        const std::uint8_t count = kCounts[rng.Below(static_cast<std::uint32_t>(std::size(kCounts)))];
        if (settings.map_name.ptr != nullptr) {
            settings.map_name.ptr[-1] = static_cast<char>(count);
        }
        std::uint8_t resultBytes[sizeof(msvc6::String)];
        rng.Fill(resultBytes, sizeof(resultBytes));
        const MapNameOutcome a = RunMapName(Original_GameSettings_MapName, settings, result, resultBytes);
        const MapNameOutcome b = RunMapName(candidate, settings, result, resultBytes);
        if (settings.map_name.ptr != nullptr) {
            settings.map_name.ptr[-1] = 0;
        }
        msvc6::String_Destroy(&settings.map_name, nullptr);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length %u, count 0x%02x", i, static_cast<unsigned>(text.size()),
                     count);
            return;
        }
    }
}

// Wrong: never shares the name's buffer.
msvc6::String* __fastcall WrongMapName(const GameSettings* self, void*, msvc6::String* result) {
    result->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(result));
    msvc6::detail::Clear(result);
    msvc6::String_AssignChars(result, nullptr, msvc6::detail::CStr(&self->map_name), self->map_name.len);
    return result;
}

}  // namespace

S3RE_TEST(GameSettings_MapName_MatchesOriginal) {
    CompareMapName(ctx, &GameSettings_MapName, 2000);
}
S3RE_TEST(GameSettings_MapName_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapName(probe, &WrongMapName, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

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
