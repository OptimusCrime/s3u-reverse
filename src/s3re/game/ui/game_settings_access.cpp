// Small accessors of the game settings (g_gameSettings 0x7a7a18,
// game_settings_layout.h; Dirk\GameParams.cpp). All thiscall (ECX = the
// settings object).
//
// GameSettings::GetRandomSeed (0x00437050): () -> AX = the seed word +0x88
//   (`mov ax,[ecx+88h]; ret`: the upper half of EAX is the caller's; both
//   callers, CLogicThread::OnStart, use AX only).
// GameSettings::IsSavegame (0x004373c0): () -> AL = +0x28. Was
//   GameSetup_sub_4373c0.
// GameSettings::Option2A (0x00435aa0), GameSettings::ByClan (0x00435ab0):
//   () -> AL = +0x2a / +0x2b (NewGame's arguments 2 and 3). Were
//   GameSetup_sub_435aa0 / 435ab0.
// GameSettings::RandomRaceAmazons (0x00437450), GameSettings::GoodsObjective
//   (0x00437460): () -> AL = +0xcc / +0xcd. Were GameSetup_sub_437450 /
//   437460.
// GameSettings::String18 (0x00431880): () -> the address of the std::string +0x18, read
//   under the settings' lock (Win32::CLock). Its callers are network code
//   (CNetThread::OnStart, CNetBounceThread's constructor). Was
//   GameSetup_sub_431880.
// GameSettings::LocalPlayerId (0x00434cb0): () -> +0x78 under the lock.
//   Was GameSetup_sub_434cb0.
// GameSettings::TeamCount (0x00435660): () -> the length of the team
//   vector (+0x50) under the lock. Was GameSetup_sub_435660.
// GameSettings::SetValue80 (0x00435a70): (value) ret 4: +0x80 = value under
//   the lock. EAX is the lock's leftover (the only caller,
//   CConsoleThread::vf04, ignores it). Was GameSetup_sub_435a70.
// GameSettings::RequestSlotInfos (0x00437060): () sets +0x8c to 1 and logs
//   "NET: SendSlotInfos requested" (DebugLog, cdecl). Callers:
//   GameSetup_sub_4342a0 and ResolveTeamRequests (EAX unused). Was
//   GameSetup_sub_437060.
// GameSettings::RandomMapParams (0x00436f80): (size*, field0c*, symmetry*,
//   seed*, settingsSeed*) ret 0x14: with chunk 5 in the settings' map file
//   (+0x84; CMapFile::GetChunk(5, 0)) the first four receive its dwords +8,
//   +0xc, +0x10, +0x14 and the chunk is released (ReleaseChunk(5, 0)); in
//   either case *settingsSeed = the dword +0x88 (the seed word and the two
//   bytes after it). EAX: GetChunk's result's leftovers (the only caller,
//   CNetSession::vf03, ignores it). Was GameSetup_sub_436f80.
// GameSettings::DeleteMapFile (0x004373d0): () deletes the map file +0x84
//   through its virtual slot 0 (the scalar deleting destructor, flags 1)
//   when there is one, then clears the pointer. Called by
//   CLogicThread::OnStart once the world is built. Was Logic_sub_4373d0.

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/framework/critical_section.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace s3re::game {

// Purpose: The random seed of the game being started (CLogicThread::OnStart
// seeds the simulation's random generator with it).
std::uint16_t __fastcall GameSettings_GetRandomSeed(const GameSettings* self, void*) {
    return self->seed;
}

// Purpose: Whether the game being set up is a savegame being loaded.
std::uint8_t __fastcall GameSettings_IsSavegame(const GameSettings* self, void*) {
    return self->is_savegame;
}

// Purpose: The second option of the new game (NewGame's argument 2).
std::uint8_t __fastcall GameSettings_Option2A(const GameSettings* self, void*) {
    return self->option2a;
}

// Purpose: Whether the local player's team is chosen by clan.
std::uint8_t __fastcall GameSettings_ByClan(const GameSettings* self, void*) {
    return self->by_clan;
}

// Purpose: Whether random races of this game include the Amazons.
std::uint8_t __fastcall GameSettings_RandomRaceAmazons(const GameSettings* self, void*) {
    return self->randomRaceAmazons;
}

// Purpose: Whether the goods objective of the "new game objectives" is on.
std::uint8_t __fastcall GameSettings_GoodsObjective(const GameSettings* self, void*) {
    return self->goods_objective;
}

// Purpose: Hands the network threads a text of the settings (probably the
// session or player name).
msvc6::String* __fastcall GameSettings_String18(GameSettings* self, void*) {
    GameLock lock(self->lock);
    return &self->string18;
}

// Purpose: The id of the local player (saved with the setup).
std::uint32_t __fastcall GameSettings_LocalPlayerId(GameSettings* self, void*) {
    GameLock lock(self->lock);
    return self->local_player_id;
}

// Purpose: How many teams the game being set up has.
std::uint32_t __fastcall GameSettings_TeamCount(GameSettings* self, void*) {
    GameLock lock(self->lock);
    return self->teams.length;
}

// Purpose: Stores a value the console thread hands over at startup
// (meaning unknown).
void __fastcall GameSettings_SetValue80(GameSettings* self, void*, std::uint32_t value) {
    GameLock lock(self->lock);
    self->value80 = value;
}

// Purpose: Asks the network code to send the slot infos (player slots) again.
void __fastcall GameSettings_RequestSlotInfos(GameSettings* self, void*) {
    self->slot_infos_requested = 1;
    GameDebugLog(texts::kNetSendSlotInfosRequested);
}

// Random-map chunk 5 (the same layout as in game_settings.cpp).
constexpr std::uint32_t kRandomMapChunkType = 5;
#pragma pack(push, 1)
struct RandomMapChunkValues {
    fileformats::ChunkHeader header;
    std::uint32_t map_size;  // +0x08
    std::uint32_t field0c;   // +0x0c
    std::uint32_t symmetry;  // +0x10
    std::uint32_t seed;      // +0x14
};
#pragma pack(pop)
static_assert(sizeof(RandomMapChunkValues) == 0x18);

using GetChunkFn = const void*(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                            std::uint32_t subIndex);
using ReleaseChunkFn = void(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                         std::uint32_t subIndex);
constexpr std::uintptr_t kGetChunkAddress = 0x004a60f0;      // Dirk::CMapFile::GetChunk (verified)
constexpr std::uintptr_t kReleaseChunkAddress = 0x004a6310;  // Dirk::CMapFile::ReleaseChunk (verified)

// Purpose: Gives the network session the random map's parameters (size,
// symmetry, seed) and the game's seed.
void __fastcall GameSettings_RandomMapParams(const GameSettings* self, void*, std::uint32_t* mapSize,
                                             std::uint32_t* field0c, std::uint32_t* symmetry,
                                             std::uint32_t* mapSeed, std::uint32_t* settingsSeed) {
    const auto* chunk = static_cast<const RandomMapChunkValues*>(
        reinterpret_cast<GetChunkFn>(kGetChunkAddress)(self->map_file, nullptr, kRandomMapChunkType, 0));
    if (chunk != nullptr) {
        *mapSize = chunk->map_size;
        *field0c = chunk->field0c;
        *symmetry = chunk->symmetry;
        *mapSeed = chunk->seed;
        reinterpret_cast<ReleaseChunkFn>(kReleaseChunkAddress)(self->map_file, nullptr, kRandomMapChunkType, 0);
    }
    // 0x436fcd: the seed is read as a dword (the word +0x88 and the two
    // bytes after it).
    std::uint32_t seed;
    std::memcpy(&seed, &self->seed, sizeof(seed));
    *settingsSeed = seed;
}

// Purpose: Frees the map or savegame file once the world has been built from it.
void __fastcall GameSettings_DeleteMapFile(GameSettings* self, void*) {
    if (self->map_file != nullptr) {
        using DeletingDestructorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
        const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(self->map_file);
        reinterpret_cast<DeletingDestructorFn>(vtable[0])(self->map_file, nullptr, 1);
    }
    self->map_file = nullptr;
}

using SeedGetterFn = std::uint16_t(__fastcall*)(const GameSettings* self, void* edx);
using ByteGetterFn = std::uint8_t(__fastcall*)(const GameSettings* self, void* edx);
using AddressGetterFn = msvc6::String*(__fastcall*)(GameSettings* self, void* edx);
using LockedGetterFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
using SetterFn = void(__fastcall*)(GameSettings* self, void* edx, std::uint32_t value);
using ActionFn = void(__fastcall*)(GameSettings* self, void* edx);
using RandomMapParamsFn = void(__fastcall*)(const GameSettings* self, void* edx, std::uint32_t* mapSize,
                                            std::uint32_t* field0c, std::uint32_t* symmetry,
                                            std::uint32_t* mapSeed, std::uint32_t* settingsSeed);

S3RE_HOOK(0x00437050, GameSettings_GetRandomSeed, SeedGetterFn, Original_GameSettings_GetRandomSeed,
          GameSettings_GetRandomSeed);
S3RE_HOOK(0x004373c0, GameSettings_IsSavegame, ByteGetterFn, Original_GameSettings_IsSavegame,
          GameSettings_IsSavegame);
S3RE_HOOK(0x00435aa0, GameSettings_Option2A, ByteGetterFn, Original_GameSettings_Option2A, GameSettings_Option2A);
S3RE_HOOK(0x00435ab0, GameSettings_ByClan, ByteGetterFn, Original_GameSettings_ByClan, GameSettings_ByClan);
S3RE_HOOK(0x00437450, GameSettings_RandomRaceAmazons, ByteGetterFn, Original_GameSettings_RandomRaceAmazons,
          GameSettings_RandomRaceAmazons);
S3RE_HOOK(0x00437460, GameSettings_GoodsObjective, ByteGetterFn, Original_GameSettings_GoodsObjective,
          GameSettings_GoodsObjective);
S3RE_HOOK(0x00431880, GameSettings_String18, AddressGetterFn, Original_GameSettings_String18, GameSettings_String18);
S3RE_HOOK(0x00434cb0, GameSettings_LocalPlayerId, LockedGetterFn, Original_GameSettings_LocalPlayerId,
          GameSettings_LocalPlayerId);
S3RE_HOOK(0x00435660, GameSettings_TeamCount, LockedGetterFn, Original_GameSettings_TeamCount,
          GameSettings_TeamCount);
S3RE_HOOK(0x00435a70, GameSettings_SetValue80, SetterFn, Original_GameSettings_SetValue80, GameSettings_SetValue80);
S3RE_HOOK(0x00437060, GameSettings_RequestSlotInfos, ActionFn, Original_GameSettings_RequestSlotInfos,
          GameSettings_RequestSlotInfos);
S3RE_HOOK(0x00436f80, GameSettings_RandomMapParams, RandomMapParamsFn, Original_GameSettings_RandomMapParams,
          GameSettings_RandomMapParams);
S3RE_HOOK(0x004373d0, GameSettings_DeleteMapFile, ActionFn, Original_GameSettings_DeleteMapFile,
          GameSettings_DeleteMapFile);

namespace {

// A settings object with random bytes and a working critical section.
struct RandomSettings {
    alignas(8) std::uint8_t bytes[sizeof(GameSettings) + 16];
    explicit RandomSettings(test::Context& ctx) {
        ctx.rng().Fill(bytes, sizeof(bytes));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes));
    }
    ~RandomSettings() { DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes)); }
    RandomSettings(const RandomSettings&) = delete;
    RandomSettings& operator=(const RandomSettings&) = delete;
    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes); }
};

template <typename Fn>
void CompareGetter(test::Context& ctx, Fn original, Fn candidate, std::uint32_t mask) {
    for (int i = 0; i < 200; ++i) {
        RandomSettings settings(ctx);
        const auto expected = static_cast<std::uint32_t>(original(settings.get(), nullptr)) & mask;
        const auto actual = static_cast<std::uint32_t>(candidate(settings.get(), nullptr)) & mask;
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            return;
        }
    }
}

std::uint8_t __fastcall WrongByClan(const GameSettings* self, void*) {
    return self->option2a;
}

}  // namespace

S3RE_TEST(GameSettingsAccess_GettersMatchOriginal) {
    CompareGetter(ctx, Original_GameSettings_GetRandomSeed, &GameSettings_GetRandomSeed, 0xffff);
    CompareGetter(ctx, Original_GameSettings_IsSavegame, &GameSettings_IsSavegame, 0xff);
    CompareGetter(ctx, Original_GameSettings_Option2A, &GameSettings_Option2A, 0xff);
    CompareGetter(ctx, Original_GameSettings_ByClan, &GameSettings_ByClan, 0xff);
    CompareGetter(ctx, Original_GameSettings_RandomRaceAmazons, &GameSettings_RandomRaceAmazons, 0xff);
    CompareGetter(ctx, Original_GameSettings_GoodsObjective, &GameSettings_GoodsObjective, 0xff);
    CompareGetter(ctx, Original_GameSettings_LocalPlayerId, &GameSettings_LocalPlayerId, 0xffffffffu);
    CompareGetter(ctx, Original_GameSettings_TeamCount, &GameSettings_TeamCount, 0xffffffffu);
    for (int i = 0; i < 20; ++i) {
        RandomSettings settings(ctx);
        S3RE_EXPECT_EQ(reinterpret_cast<std::uintptr_t>(Original_GameSettings_String18(settings.get(), nullptr)),
                       reinterpret_cast<std::uintptr_t>(GameSettings_String18(settings.get(), nullptr)));
    }
}

S3RE_TEST(GameSettingsAccess_SettersMatchOriginal) {
    for (int i = 0; i < 100; ++i) {
        RandomSettings a(ctx);
        RandomSettings b(ctx);
        std::memcpy(b.bytes + sizeof(CRITICAL_SECTION), a.bytes + sizeof(CRITICAL_SECTION),
                    sizeof(a.bytes) - sizeof(CRITICAL_SECTION));
        const std::uint32_t value = ctx.rng().Next32();
        Original_GameSettings_SetValue80(a.get(), nullptr, value);
        GameSettings_SetValue80(b.get(), nullptr, value);
        Original_GameSettings_RequestSlotInfos(a.get(), nullptr);
        GameSettings_RequestSlotInfos(b.get(), nullptr);
        if (!S3RE_EXPECT_EQ(0, std::memcmp(a.bytes + sizeof(CRITICAL_SECTION), b.bytes + sizeof(CRITICAL_SECTION),
                                           sizeof(a.bytes) - sizeof(CRITICAL_SECTION)))) {
            return;
        }
    }
}

S3RE_TEST(GameSettingsAccess_ComparisonDetectsWrongImplementation) {
    int differences = 0;
    for (int i = 0; i < 50; ++i) {
        RandomSettings settings(ctx);
        if (Original_GameSettings_ByClan(settings.get(), nullptr) != WrongByClan(settings.get(), nullptr)) {
            ++differences;
        }
    }
    S3RE_EXPECT(differences > 0);
}

namespace {

// RandomMapParams on settings whose map file is a memory file with or
// without chunk 5 (random values), built twice from the same random state:
// the five outputs (pre-filled alike) and the files afterwards must match.
void CompareRandomMapParams(test::Context& ctx, RandomMapParamsFn candidate, int cases) {
    using AddChunkFn = std::uint32_t(__fastcall*)(fileformats::CMapFile* self, void* edx, std::uint32_t type,
                                                  std::uint32_t subIndex, const void* data);
    using SetMemoryOnlyFn = void(__fastcall*)(fileformats::CMapFile* self, void* edx);
    const auto addChunk = reinterpret_cast<AddChunkFn>(0x004a63f0);            // Dirk::CMapFile::AddChunk
    const auto setMemoryOnly = reinterpret_cast<SetMemoryOnlyFn>(0x004a60c0);  // Dirk::CMapFile::SetMemoryOnly
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const bool withChunk = rng.Chance(3, 4);
        RandomMapChunkValues chunk{};
        rng.Fill(&chunk, sizeof(chunk));
        chunk.header.key = kRandomMapChunkType;
        chunk.header.size = sizeof(chunk);
        auto build = [&]() {
            fileformats::CMapFile* file = mapfile_test::NewMapFile();
            setMemoryOnly(file, nullptr);
            if (withChunk) {
                addChunk(file, nullptr, kRandomMapChunkType, 0, &chunk);
            }
            return file;
        };
        const mapfile_test::RandomState random = mapfile_test::RandomState::Save();
        fileformats::CMapFile* a = build();
        random.Restore();
        fileformats::CMapFile* b = build();
        alignas(4) std::uint8_t settingsA[sizeof(GameSettings)];
        rng.Fill(settingsA, sizeof(settingsA));
        alignas(4) std::uint8_t settingsB[sizeof(GameSettings)];
        std::memcpy(settingsB, settingsA, sizeof(settingsA));
        reinterpret_cast<GameSettings*>(settingsA)->map_file = a;
        reinterpret_cast<GameSettings*>(settingsB)->map_file = b;
        std::uint32_t outA[5];
        rng.Fill(outA, sizeof(outA));
        std::uint32_t outB[5];
        std::memcpy(outB, outA, sizeof(outA));
        Original_GameSettings_RandomMapParams(reinterpret_cast<GameSettings*>(settingsA), nullptr, &outA[0], &outA[1],
                                              &outA[2], &outA[3], &outA[4]);
        candidate(reinterpret_cast<GameSettings*>(settingsB), nullptr, &outB[0], &outB[1], &outB[2], &outB[3],
                  &outB[4]);
        const bool same = S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA));
        mapfile_test::CompareFiles(ctx, a, b);
        mapfile_test::DeleteMapFile(a);
        mapfile_test::DeleteMapFile(b);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: chunk %d", i, withChunk ? 1 : 0);
            return;
        }
    }
}

// Wrong: swaps the size and the symmetry.
void __fastcall WrongRandomMapParams(const GameSettings* self, void* edx, std::uint32_t* mapSize,
                                     std::uint32_t* field0c, std::uint32_t* symmetry, std::uint32_t* mapSeed,
                                     std::uint32_t* settingsSeed) {
    GameSettings_RandomMapParams(self, edx, symmetry, field0c, mapSize, mapSeed, settingsSeed);
}

// A fake map file whose vtable slot 0 records its calls.
struct FakeMapFile {
    const std::uintptr_t* vtable;
    std::uint32_t calls;
    std::uint32_t flags;
};

void* __fastcall FakeDeletingDestructor(FakeMapFile* self, void*, std::uint32_t flags) {
    ++self->calls;
    self->flags = flags;
    return self;
}

const std::uintptr_t kFakeMapFileVtable[] = {reinterpret_cast<std::uintptr_t>(&FakeDeletingDestructor)};

using DeleteMapFileFn = void(__fastcall*)(GameSettings* self, void* edx);

void CompareDeleteMapFile(test::Context& ctx, DeleteMapFileFn candidate) {
    for (int i = 0; i < 100; ++i) {
        const bool withFile = ctx.rng().Chance(3, 4);
        FakeMapFile fileA{kFakeMapFileVtable, 0, 0};
        FakeMapFile fileB{kFakeMapFileVtable, 0, 0};
        alignas(4) std::uint8_t settingsA[sizeof(GameSettings)];
        ctx.rng().Fill(settingsA, sizeof(settingsA));
        alignas(4) std::uint8_t settingsB[sizeof(GameSettings)];
        std::memcpy(settingsB, settingsA, sizeof(settingsA));
        reinterpret_cast<GameSettings*>(settingsA)->map_file =
            withFile ? reinterpret_cast<fileformats::CMapFile*>(&fileA) : nullptr;
        reinterpret_cast<GameSettings*>(settingsB)->map_file =
            withFile ? reinterpret_cast<fileformats::CMapFile*>(&fileB) : nullptr;
        Original_GameSettings_DeleteMapFile(reinterpret_cast<GameSettings*>(settingsA), nullptr);
        candidate(reinterpret_cast<GameSettings*>(settingsB), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(settingsA, settingsB, sizeof(settingsA)) ||
            !S3RE_EXPECT(fileA.calls == fileB.calls && fileA.flags == fileB.flags)) {
            return;
        }
    }
}

// Wrong: deletes without the "free memory" flag.
void __fastcall WrongDeleteMapFile(GameSettings* self, void*) {
    if (self->map_file != nullptr) {
        using DeletingDestructorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
        const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(self->map_file);
        reinterpret_cast<DeletingDestructorFn>(vtable[0])(self->map_file, nullptr, 0);
    }
    self->map_file = nullptr;
}

}  // namespace

S3RE_TEST(GameSettingsAccess_RandomMapParamsMatchesOriginal) {
    CompareRandomMapParams(ctx, &GameSettings_RandomMapParams, 300);
}

S3RE_TEST(GameSettingsAccess_RandomMapParams_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRandomMapParams(probe, &WrongRandomMapParams, 50);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettingsAccess_DeleteMapFileMatchesOriginal) {
    CompareDeleteMapFile(ctx, &GameSettings_DeleteMapFile);
}

S3RE_TEST(GameSettingsAccess_DeleteMapFile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDeleteMapFile(probe, &WrongDeleteMapFile);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
