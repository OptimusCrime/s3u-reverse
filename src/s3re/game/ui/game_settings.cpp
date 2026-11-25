// Small getters of the game settings (g_gameSettings, game_settings_layout.h)
// that the simulation calls while a game is set up.
//
// GameSettings::NgoTimeout (0x00437470): () -> the dword +0xd0, the new
//   game objectives' time in minutes. fastcall-compatible thiscall getter.
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
// GameSettings::RandomMapSize (0x00436ff0) and GameSettings::RandomMapSymmetry
//   (0x00437020): () -> the map size (+8) / the symmetry (+0x10) of the
//   random-map chunk 5 of the settings' map file (+0x84; GetChunk(5, 0),
//   then ReleaseChunk(5, 0)), 0 when the file has no such chunk (a map or a
//   savegame). Called by the game launch dialog (CGameLaunchDialog::OnIdle).
// GamePlayerSetup's scalar deleting destructor (0x00437fc0): (flags) ret 4:
//   the destructor 0x49ec80, then operator delete when bit 0 of flags is set;
//   EAX = this. Called by the player-setup vector code (0x433e50, 0x434ec0,
//   0x4379e0).
// All thiscall (ECX = the object).

#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
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

// Purpose: Tells the game start how many minutes the "new game objectives" run.
std::uint32_t __fastcall GameSettings_NgoTimeout(const GameSettings* self, void*) {
    return self->ngo_timeout;
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

// Random-map chunk 5 of the settings' map file (tooling/random_map_start.cpp
// builds it like the map dialog).
constexpr std::uint32_t kRandomMapChunk = 5;
#pragma pack(push, 1)
struct RandomMapChunk {
    fileformats::ChunkHeader header;
    std::uint32_t map_size;  // +0x08
    std::uint32_t field0c;   // +0x0c the map dialog's second combo
    std::uint32_t symmetry;  // +0x10
    std::uint32_t seed;      // +0x14
};
#pragma pack(pop)
static_assert(sizeof(RandomMapChunk) == 0x18);

using GetChunkFn = const void*(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                            std::uint32_t subIndex);
using ReleaseChunkFn = void(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                         std::uint32_t subIndex);
constexpr std::uintptr_t kGetChunk = 0x004a60f0;      // Dirk::CMapFile::GetChunk (verified)
constexpr std::uintptr_t kReleaseChunk = 0x004a6310;  // Dirk::CMapFile::ReleaseChunk (verified)

// A field of chunk 5, or 0 without the chunk.
template <std::uint32_t RandomMapChunk::*Field>
std::uint32_t RandomMapValue(const GameSettings* self) {
    const auto* chunk = static_cast<const RandomMapChunk*>(
        reinterpret_cast<GetChunkFn>(kGetChunk)(self->map_file, nullptr, kRandomMapChunk, 0));
    if (chunk == nullptr) {
        return 0;
    }
    const std::uint32_t value = chunk->*Field;
    reinterpret_cast<ReleaseChunkFn>(kReleaseChunk)(self->map_file, nullptr, kRandomMapChunk, 0);
    return value;
}

// Purpose: The size of the random map being started (0 for a map file), for
// the game launch dialog.
std::uint32_t __fastcall GameSettings_RandomMapSize(const GameSettings* self, void*) {
    return RandomMapValue<&RandomMapChunk::map_size>(self);
}

// Purpose: The symmetry of the random map being started (0 for a map file),
// for the game launch dialog.
std::uint32_t __fastcall GameSettings_RandomMapSymmetry(const GameSettings* self, void*) {
    return RandomMapValue<&RandomMapChunk::symmetry>(self);
}

using SetupDeletingDestructorFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t flags);
constexpr std::uintptr_t kSetupDestruct = 0x0049ec80;  // GamePlayerSetup::~GamePlayerSetup (ours)

// Purpose: Destroys a player setup and frees it when asked (the deleting
// destructor the vector code calls).
GamePlayerSetup* __fastcall GamePlayerSetup_ScalarDeletingDestructor(GamePlayerSetup* self, void*,
                                                                     std::uint32_t flags) {
    reinterpret_cast<void(__fastcall*)(GamePlayerSetup*, void*)>(kSetupDestruct)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x00436ff0, GameSettings_RandomMapSize, SettingsGetterFn, Original_GameSettings_RandomMapSize,
          GameSettings_RandomMapSize);
S3RE_HOOK(0x00437020, GameSettings_RandomMapSymmetry, SettingsGetterFn, Original_GameSettings_RandomMapSymmetry,
          GameSettings_RandomMapSymmetry);
S3RE_HOOK(0x00437fc0, GamePlayerSetup_ScalarDeletingDestructor, SetupDeletingDestructorFn,
          Original_GamePlayerSetup_ScalarDeletingDestructor, GamePlayerSetup_ScalarDeletingDestructor);
S3RE_HOOK(0x00412300, GameSettings_StartGoods, SettingsGetterFn, Original_GameSettings_StartGoods,
          GameSettings_StartGoods);
S3RE_HOOK(0x00437470, GameSettings_NgoTimeout, SettingsGetterFn, Original_GameSettings_NgoTimeout,
          GameSettings_NgoTimeout);
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
std::uint32_t __fastcall WrongNgoTimeout(const GameSettings* self, void*) {
    return self->ngo_timeout + 1;
}

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

// RandomMapSize / RandomMapSymmetry on settings whose map file is a memory
// file with or without chunk 5 (random values), built twice from the same
// random state: the result and the two files afterwards (dictionaries,
// cached chunks, refcounts) must match.
void CompareRandomMap(test::Context& ctx, SettingsGetterFn original, SettingsGetterFn candidate, int cases) {
    using AddChunkFn = std::uint32_t(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                                  std::uint32_t subIndex, const void* data);
    using SetMemoryOnlyFn = void(__fastcall*)(fileformats::CMapFile* self, void* edx);
    const auto addChunk = reinterpret_cast<AddChunkFn>(0x004a63f0);        // Dirk::CMapFile::AddChunk
    const auto setMemoryOnly = reinterpret_cast<SetMemoryOnlyFn>(0x004a60c0);  // Dirk::CMapFile::SetMemoryOnly
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const bool withChunk = rng.Chance(3, 4);
        const bool otherChunk = rng.Chance(1, 2);
        RandomMapChunk chunk{};
        rng.Fill(&chunk, sizeof(chunk));
        chunk.header.key = kRandomMapChunk;
        chunk.header.size = sizeof(chunk);
        RandomMapChunk other = chunk;
        other.header.key = 1;
        auto build = [&]() {
            fileformats::CMapFile* file = mapfile_test::NewMapFile();
            setMemoryOnly(file, nullptr);
            if (otherChunk) {
                addChunk(file, nullptr, 1, 0, &other);
            }
            if (withChunk) {
                addChunk(file, nullptr, kRandomMapChunk, 0, &chunk);
            }
            return file;
        };
        const mapfile_test::RandomState random = mapfile_test::RandomState::Save();
        fileformats::CMapFile* a = build();
        random.Restore();
        fileformats::CMapFile* b = build();
        alignas(4) std::uint8_t settingsA[sizeof(GameSettings)] = {};
        alignas(4) std::uint8_t settingsB[sizeof(GameSettings)] = {};
        reinterpret_cast<GameSettings*>(settingsA)->map_file = a;
        reinterpret_cast<GameSettings*>(settingsB)->map_file = b;
        const std::uint32_t expected = original(reinterpret_cast<GameSettings*>(settingsA), nullptr);
        const std::uint32_t actual = candidate(reinterpret_cast<GameSettings*>(settingsB), nullptr);
        const bool same = S3RE_EXPECT_EQ(expected, actual);
        mapfile_test::CompareFiles(ctx, a, b);
        mapfile_test::DeleteMapFile(a);
        mapfile_test::DeleteMapFile(b);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: chunk %d", i, withChunk ? 1 : 0);
            return;
        }
    }
}

// Wrong: reads the dialog's second combo instead of the symmetry.
std::uint32_t __fastcall WrongRandomMapSymmetry(const GameSettings* self, void*) {
    return RandomMapValue<&RandomMapChunk::field0c>(self);
}

// The deleting destructor on two player setups built alike (the original
// constructor, random plain fields and texts), with flags 0..3: EAX (as
// "this") and, when not freed, the setups' plain bytes and string fields.
void CompareSetupDelete(test::Context& ctx, SetupDeletingDestructorFn candidate, int cases) {
    using SetupConstructFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
    const auto construct = reinterpret_cast<SetupConstructFn>(0x0049e8f0);
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t flags = rng.Below(4);
        const std::uint64_t seed = rng.Next64();
        auto make = [&]() {
            test::Rng fill(seed);
            auto* setup = static_cast<GamePlayerSetup*>(msvc6::GameOperatorNew(sizeof(GamePlayerSetup)));
            construct(setup, nullptr);
            std::string text(fill.Below(30), 'q');
            msvc6::String_AssignChars(&setup->text18, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
            auto* bytes = reinterpret_cast<std::uint8_t*>(setup);
            fill.Fill(bytes, 0x18);  // the plain fields (+0x7c..+0xa0 are left uninitialized by the constructor)
            fill.Fill(bytes + 0x54, 0xbc - 0x54);
            return setup;
        };
        GamePlayerSetup* a = make();
        GamePlayerSetup* b = make();
        GamePlayerSetup* ra = Original_GamePlayerSetup_ScalarDeletingDestructor(a, nullptr, flags);
        GamePlayerSetup* rb = candidate(b, nullptr, flags);
        bool same = S3RE_EXPECT(ra == a && rb == b);
        if ((flags & 1) == 0) {
            // The plain fields, and the strings' fields (the members' heap
            // pointers differ between the two).
            const auto* pa = reinterpret_cast<const std::uint8_t*>(a);
            const auto* pb = reinterpret_cast<const std::uint8_t*>(b);
            same = S3RE_EXPECT_MEM_EQ(pa, pb, 0x18) && S3RE_EXPECT_MEM_EQ(pa + 0x54, pb + 0x54, 0xbc - 0x54) &&
                   S3RE_EXPECT((a->text18.ptr == nullptr) == (b->text18.ptr == nullptr)) &&
                   S3RE_EXPECT(a->text18.len == b->text18.len && a->textBC.len == b->textBC.len) && same;
            msvc6::GameOperatorDelete(a);
            msvc6::GameOperatorDelete(b);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: flags %u", i, flags);
            return;
        }
    }
}

// Wrong: flags 2 skips the destructor.
GamePlayerSetup* __fastcall WrongSetupDelete(GamePlayerSetup* self, void* edx, std::uint32_t flags) {
    return flags == 2 ? self : GamePlayerSetup_ScalarDeletingDestructor(self, edx, flags);
}

}  // namespace

S3RE_TEST(GameSettings_RandomMapSize_MatchesOriginal) {
    CompareRandomMap(ctx, Original_GameSettings_RandomMapSize, &GameSettings_RandomMapSize, 300);
}
S3RE_TEST(GameSettings_RandomMapSymmetry_MatchesOriginal) {
    CompareRandomMap(ctx, Original_GameSettings_RandomMapSymmetry, &GameSettings_RandomMapSymmetry, 300);
}
S3RE_TEST(GameSettings_RandomMap_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRandomMap(probe, Original_GameSettings_RandomMapSymmetry, &WrongRandomMapSymmetry, 50);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GamePlayerSetup_ScalarDeletingDestructor_MatchesOriginal) {
    CompareSetupDelete(ctx, &GamePlayerSetup_ScalarDeletingDestructor, 500);
}
S3RE_TEST(GamePlayerSetup_ScalarDeletingDestructor_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupDelete(probe, &WrongSetupDelete, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettings_MapName_MatchesOriginal) {
    CompareMapName(ctx, &GameSettings_MapName, 2000);
}
S3RE_TEST(GameSettings_MapName_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapName(probe, &WrongMapName, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettings_NgoTimeout_MatchesOriginal) {
    CompareGetter<GameSettings>(ctx, Original_GameSettings_NgoTimeout, &GameSettings_NgoTimeout, 1000);
}
S3RE_TEST(GameSettings_NgoTimeout_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGetter<GameSettings>(probe, Original_GameSettings_NgoTimeout, &WrongNgoTimeout, 100);
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
