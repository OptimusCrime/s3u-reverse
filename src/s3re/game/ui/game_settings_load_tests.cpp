// Tests of GameSettings::LoadSaveGame (game_settings_load.cpp) and the
// local-player code (game_settings_local_player.cpp), and of the vector
// index checks of game_settings_vectors.cpp: the original against ours on
// two identical settings objects of our own (never on g_gameSettings),
// with
//  - every savegame under Settlers3Ultimate\Save (read only: CMapFile::Open
//    in read mode) and synthetic savegames written to %TEMP% (random chunks
//    0, 0x14, 0x1a, 0x1b, 0x1e; some missing), single and multiplayer, with
//    and without the Amazons installed (GameConfig +0x158),
//  - random settings for SetupLocalPlayer, LowestFreeNetId,
//    FindTeamForClan, the team clan scans and the index checks, and random
//    setups for SetType and AttachNetPlayer (its network callee 0x49fbc0
//    replaced by a recording stub, a fake net player).
// The settings vectors get the vtables of the real g_gameSettings. The team
// clan scans read g_gameSettings' player setups: the tests point that
// vector at the test object's for the time of a call. g_gameSettings' IsHost
// byte is held at 0 (SetType would call network code otherwise).
// Compared: results, the settings' plain fields, every vector (headers and
// elements: plain fields, texts), the map file's dictionaries and the
// DebugLog calls (the SLOT line's text pointer by its text).
// Not compared (known differences, see the files' headers): a setup's byte
// +0x0c and +0x7c..+0xa3, which the original's InsertAt copies from an
// uninitialized local of LoadSaveGame (ours: zeroed), and the padding bytes
// no member writes.

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::CMapFile;
using watcom::VectorBase;

using LoadSaveGameFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, const msvc6::String* path,
                                                  std::uint32_t multiplayer);
using ConfigFlagFn = std::uint32_t(__fastcall*)(const GameConfig* self, void* edx);
using LocalPlayerFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, std::uint32_t netPlayer,
                                                 std::uint32_t clan);
using SettingsResultFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
using SettingsArgFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, std::uint32_t argument);
using TeamResultFn = std::uint32_t(__fastcall*)(CTeam* self, void* edx);
using TeamArgFn = std::uint32_t(__fastcall*)(CTeam* self, void* edx, std::uint32_t argument);
using SetTypeFn = void(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type);
using AttachFn = std::uint32_t(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t id, std::uint32_t b,
                                            std::uint32_t netPlayer, std::uint32_t flag);
using SetupResultFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);
using IndexFn = std::int32_t(__fastcall*)(VectorBase* self, void* edx, std::int32_t index);
using ResizeFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::uint32_t n);

std::uint32_t __fastcall GameSettings_LoadSaveGame(GameSettings*, void*, const msvc6::String*, std::uint32_t);
std::uint32_t __fastcall GameConfig_RandomRaceAmazons(const GameConfig*, void*);
std::uint32_t __fastcall GameSettings_IsHost(GameSettings*, void*);
std::uint32_t __fastcall GameSettings_IsMultiplayer(GameSettings*, void*);
std::uint32_t __fastcall GameSettings_SetupLocalPlayer(GameSettings*, void*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall GameSettings_LowestFreeNetId(GameSettings*, void*);
std::uint32_t __fastcall GameSettings_FindTeamForClan(GameSettings*, void*, std::uint32_t);
std::uint32_t __fastcall CTeam_FirstClan(CTeam*, void*);
std::uint32_t __fastcall CTeam_CheckClans(CTeam*, void*, std::uint32_t);
void __fastcall GamePlayerSetup_SetType(GamePlayerSetup*, void*, std::uint32_t);
std::uint32_t __fastcall GamePlayerSetup_AttachNetPlayer(GamePlayerSetup*, void*, std::uint32_t, std::uint32_t,
                                                         std::uint32_t, std::uint32_t);
std::uint32_t __fastcall GamePlayerSetup_Clan(const GamePlayerSetup*, void*);
std::int32_t __fastcall PlayerVector_Index(VectorBase*, void*, std::int32_t);
std::int32_t __fastcall TeamVector_Index(VectorBase*, void*, std::int32_t);
std::uint32_t __fastcall PlayerVector_ResizeClampLength(VectorBase*, void*, std::uint32_t);

extern LoadSaveGameFn Original_GameSettings_LoadSaveGame;
extern ConfigFlagFn Original_GameConfig_RandomRaceAmazons;
extern SettingsResultFn Original_GameSettings_IsHost;
extern SettingsResultFn Original_GameSettings_IsMultiplayer;
extern LocalPlayerFn Original_GameSettings_SetupLocalPlayer;
extern SettingsResultFn Original_GameSettings_LowestFreeNetId;
extern SettingsArgFn Original_GameSettings_FindTeamForClan;
extern TeamResultFn Original_CTeam_FirstClan;
extern TeamArgFn Original_CTeam_CheckClans;
extern SetTypeFn Original_GamePlayerSetup_SetType;
extern AttachFn Original_GamePlayerSetup_AttachNetPlayer;
extern SetupResultFn Original_GamePlayerSetup_Clan;
extern IndexFn Original_PlayerVector_Index;
extern IndexFn Original_TeamVector_Index;
extern ResizeFn Original_PlayerVector_ResizeClampLength;

namespace {

using namespace mapfile_test;

constexpr std::size_t kSettingsBytes = 0xd4;
constexpr std::uintptr_t kTeamResize = 0x00437d00;
constexpr std::uintptr_t kPlayerResize = 0x00437820;
constexpr std::uintptr_t kIntResize = 0x00429a00;
constexpr std::uintptr_t kSetupConstruct = 0x0049e8f0;
constexpr std::uintptr_t kTeamConstructId = 0x004a02d0;
constexpr std::uintptr_t kTeamInsertAt = 0x00438090;
constexpr std::uintptr_t kTeamDestroy = 0x004a04d0;
constexpr std::uintptr_t kPlayerInsertAt = 0x004379e0;
constexpr std::uintptr_t kSetupDestroy = 0x0049ec80;
constexpr std::uintptr_t kAddPlayerId = 0x004a06d0;
constexpr std::uintptr_t kIntInsertAt = 0x00428d10;
constexpr std::uintptr_t kWriteChunk = 0x004a6540;
constexpr std::uintptr_t kReadNetPlayerData = 0x0049fbc0;

template <typename R = std::uint32_t, typename... Args>
R Call(std::uintptr_t address, const void* self, Args... args) {
    using Fn = R(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

template <typename T>
std::uint32_t Addr(const T* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

VectorBase* Players(GameSettings* s) {
    return reinterpret_cast<VectorBase*>(&s->player_setups);
}

// ---------------------------------------------------------------------------
// Settings objects of our own
// ---------------------------------------------------------------------------

class TestSettings {
public:
    TestSettings() {
        std::memset(bytes_, 0, sizeof(bytes_));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
        const GameSettings& model = g_gameSettings.get();
        GameSettings* s = get();
        s->teams.vtable = model.teams.vtable;
        s->teams.grow = model.teams.grow;
        s->player_setups.vtable = model.player_setups.vtable;
        s->player_setups_grow = model.player_setups_grow;
        s->net_ids.vtable = model.net_ids.vtable;
        s->net_ids.grow = model.net_ids.grow;
    }
    ~TestSettings() {
        GameSettings* s = get();
        Call(kTeamResize, &s->teams, 0);
        Call(kPlayerResize, &s->player_setups, 0);
        Call(kIntResize, &s->net_ids, 0);
        msvc6::String_Destroy(&s->map_name, nullptr);
        if (s->map_file != nullptr) {
            DeleteMapFile(s->map_file);
        }
        DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
    }
    TestSettings(const TestSettings&) = delete;
    TestSettings& operator=(const TestSettings&) = delete;

    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes_); }

private:
    alignas(8) std::uint8_t bytes_[0x100];
};

bool ModelUsable() {
    const GameSettings& model = g_gameSettings.get();
    return model.teams.vtable != 0 && model.player_setups.vtable != 0 && model.net_ids.vtable != 0;
}

// g_gameSettings' IsHost byte held at 0 and the Amazons flag restored
// afterwards.
class GlobalSettingsGuard {
public:
    GlobalSettingsGuard() : savedHost_(g_gameSettings->is_host), savedAmazons_(g_config->random_race_amazons) {
        g_gameSettings->is_host = 0;
    }
    ~GlobalSettingsGuard() {
        g_gameSettings->is_host = savedHost_;
        g_config->random_race_amazons = savedAmazons_;
    }
    GlobalSettingsGuard(const GlobalSettingsGuard&) = delete;
    GlobalSettingsGuard& operator=(const GlobalSettingsGuard&) = delete;

private:
    std::uint8_t savedHost_;
    std::uint8_t savedAmazons_;
};

// Runs `call` with g_gameSettings' player vector being `settings`' (and
// takes the vector back afterwards: the call may have grown it).
template <typename F>
std::uint32_t WithGlobalPlayers(GameSettings* settings, F call) {
    PlayerSetupVector saved;
    std::memcpy(&saved, &g_gameSettings->player_setups, sizeof(saved));
    std::memcpy(&g_gameSettings->player_setups, &settings->player_setups, sizeof(saved));
    const std::uint32_t result = call();
    std::memcpy(&settings->player_setups, &g_gameSettings->player_setups, sizeof(saved));
    std::memcpy(&g_gameSettings->player_setups, &saved, sizeof(saved));
    return result;
}

// ---------------------------------------------------------------------------
// Describing settings
// ---------------------------------------------------------------------------

// The compared parts of a GamePlayerSetup (see the file comment).
constexpr std::size_t kSetupRanges[][2] = {{0x00, 0x0c}, {0x10, 0x18}, {0x54, 0x55}, {0x58, 0x61},
                                           {0x64, 0x69}, {0x6c, 0x7c}, {0xa4, 0xbc}};

void AppendBytes(std::vector<std::uint32_t>& out, const void* p, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(bytes[i]);
    }
}

void AppendText(std::vector<std::uint32_t>& out, const msvc6::String& s) {
    out.push_back(0x7e000000u | s.len);
    if (s.ptr != nullptr) {
        AppendBytes(out, s.ptr, s.len);
        out.push_back(static_cast<std::uint8_t>(s.ptr[-1]));  // the share count
    }
}

void AppendWideText(std::vector<std::uint32_t>& out, const msvc6::WString& s) {
    out.push_back(0x7f000000u | s.len);
    if (s.ptr != nullptr) {
        for (std::uint32_t i = 0; i < s.len; ++i) {
            out.push_back(s.ptr[i]);
        }
    }
}

void AppendHeader(std::vector<std::uint32_t>& out, const VectorBase& v) {
    out.push_back(v.vtable);
    out.push_back(v.data != 0 ? 1 : 0);
    out.push_back(v.capacity);
    out.push_back(static_cast<std::uint32_t>(v.filled));
    out.push_back(v.length);
    out.push_back(v.grow);
}

void AppendInts(std::vector<std::uint32_t>& out, const VectorBase& v) {
    AppendHeader(out, v);
    for (std::int32_t i = 0; v.data != 0 && i < v.filled; ++i) {
        out.push_back(reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(v.data))[i]);
    }
}

void AppendSetup(std::vector<std::uint32_t>& out, const GamePlayerSetup& setup) {
    for (const auto& range : kSetupRanges) {
        AppendBytes(out, reinterpret_cast<const std::uint8_t*>(&setup) + range[0], range[1] - range[0]);
    }
    AppendText(out, setup.text18);
    AppendText(out, setup.textBC);
    AppendWideText(out, setup.wtext28);
}

void AppendSetups(std::vector<std::uint32_t>& out, const VectorBase& v) {
    AppendHeader(out, v);
    for (std::int32_t i = 0; v.data != 0 && i < v.filled; ++i) {
        AppendSetup(out, reinterpret_cast<const GamePlayerSetup*>(static_cast<std::uintptr_t>(v.data))[i]);
    }
}

std::vector<std::uint32_t> Describe(GameSettings* s) {
    std::vector<std::uint32_t> out;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(s);
    AppendBytes(out, bytes + 0x18, 0x2c - 0x18);
    AppendText(out, s->map_name);
    AppendHeader(out, s->teams);
    for (std::int32_t i = 0; s->teams.data != 0 && i < s->teams.filled; ++i) {
        const CTeam& team = reinterpret_cast<const CTeam*>(static_cast<std::uintptr_t>(s->teams.data))[i];
        out.push_back(static_cast<std::uint32_t>(team.id));
        AppendInts(out, team.player_ids);
        AppendSetups(out, team.players);
    }
    AppendSetups(out, *Players(s));
    AppendBytes(out, bytes + 0x70, 0x84 - 0x70);
    out.push_back(s->map_file != nullptr ? 1 : 0);
    AppendBytes(out, bytes + 0x88, 0x90 - 0x88);
    AppendInts(out, s->net_ids);
    AppendBytes(out, bytes + 0xac, kSettingsBytes - 0xac);
    return out;
}

// The DebugLog calls, the SLOT line's text pointer replaced by its text's
// first bytes.
std::vector<DebugLogCall> Logs(DebugLogRecorder& log) {
    std::vector<DebugLogCall> calls = log.Take();
    for (DebugLogCall& call : calls) {
        if (call.format == texts::kSlotLog.address() && call.count == 8) {
            std::uint32_t head = 0;
            std::memcpy(&head, reinterpret_cast<const char*>(static_cast<std::uintptr_t>(call.args[7])), 3);
            call.args[7] = head;
        }
    }
    return calls;
}

void CompareDescriptions(test::Context& ctx, const char* what, const std::vector<std::uint32_t>& a,
                         const std::vector<std::uint32_t>& b) {
    if (a == b) {
        return;
    }
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
        ++i;
    }
    ctx.Fail(__FILE__, __LINE__, "%s: settings differ at item %u of %u/%u (original %#x, ours %#x)", what,
             static_cast<unsigned>(i), static_cast<unsigned>(a.size()), static_cast<unsigned>(b.size()),
             i < a.size() ? a[i] : 0u, i < b.size() ? b[i] : 0u);
    for (std::size_t k = i >= 8 ? i - 8 : 0; k < i + 3 && k < a.size() && k < b.size(); ++k) {
        ctx.Fail(__FILE__, __LINE__, "    item %u: %#x / %#x", static_cast<unsigned>(k), a[k], b[k]);
    }
}

// ---------------------------------------------------------------------------
// LoadSaveGame
// ---------------------------------------------------------------------------

// CoCreateGuid replaced by a counter for the time of a test (a savegame
// without chunk 0x1a gets a new GUID): both versions get the same.
std::uint32_t g_guidCounter = 0;
std::int32_t __stdcall FakeCoCreateGuid(void* guid) {
    const std::uint32_t words[4] = {0x5e7a11d0u, ++g_guidCounter, 0x12345678u, 0x9abcdef0u};
    std::memcpy(guid, words, sizeof(words));
    return 0;
}

class FakeGuids {
public:
    FakeGuids() {
        const CoCreateGuidImport fake = &FakeCoCreateGuid;
        test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importCoCreateGuid.get()), &fake, sizeof(fake));
    }
    ~FakeGuids() { test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importCoCreateGuid.get())); }
    FakeGuids(const FakeGuids&) = delete;
    FakeGuids& operator=(const FakeGuids&) = delete;
};

std::string TestFolder() {
    char buffer[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buffer);
    std::string folder = std::string(buffer) + "s3re-settings-load-" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(folder.c_str(), nullptr);
    return folder;
}

void WriteChunk(CMapFile* file, fileformats::SaveChunk type, std::vector<std::uint8_t> bytes) {
    fileformats::ChunkHeader header{fileformats::ChunkKey(static_cast<std::uint32_t>(type), 0),
                                    static_cast<std::uint32_t>(bytes.size())};
    std::memcpy(bytes.data(), &header, sizeof(header));
    Call<void>(kWriteChunk, file, static_cast<std::uint32_t>(type), 0, Addr(bytes.data()));
}

// A synthetic savegame with random setup chunks (each may be missing).
void WriteSyntheticSave(test::Rng& rng, const std::string& path) {
    CMapFile* file = OpenMapFile(path, fileformats::kOpenWrite);
    if (!rng.Chance(1, 12)) {
        std::vector<std::uint8_t> header(sizeof(fileformats::SaveHeaderChunk));
        rng.Fill(header.data(), header.size());
        const std::uint32_t length = rng.Chance(1, 5) ? 0 : rng.Below(60);
        for (std::uint32_t i = 0; i < length; ++i) {
            header[0x20 + i] = static_cast<std::uint8_t>('A' + rng.Below(26));
        }
        header[0x20 + length] = 0;
        WriteChunk(file, fileformats::SaveChunk::Header, header);
    }
    if (!rng.Chance(1, 12)) {
        std::vector<std::uint8_t> players(sizeof(fileformats::SaveSetupPlayersChunk));
        rng.Fill(players.data(), players.size());
        const std::uint32_t count = rng.Chance(1, 6) ? rng.Below(21) : rng.Below(6);
        std::memcpy(players.data() + 8, &count, 4);
        for (std::uint32_t r = 0; r < 20; ++r) {
            std::uint8_t* record = players.data() + 0xc + r * 0x18;
            if (!rng.Chance(1, 8)) {  // mostly values like the game's
                record[4] = static_cast<std::uint8_t>(rng.Below(8));
                record[5] = static_cast<std::uint8_t>(rng.Chance(1, 10) ? rng.Below(30) : rng.Below(count + 1));
                record[6] = static_cast<std::uint8_t>(rng.Below(5));
                record[7] = static_cast<std::uint8_t>(rng.Below(5));
                record[8] = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0xff : rng.Below(4));
            } else {
                record[6] = static_cast<std::uint8_t>(rng.Below(24));
            }
        }
        WriteChunk(file, fileformats::SaveChunk::SetupPlayers, players);
    }
    if (rng.Chance(4, 5)) {
        std::vector<std::uint8_t> game(sizeof(fileformats::SaveSetup1AChunk));
        rng.Fill(game.data(), game.size());
        WriteChunk(file, fileformats::SaveChunk::Setup1A, game);
    }
    if (rng.Chance(4, 5)) {
        std::vector<std::uint8_t> options(sizeof(fileformats::SaveSetup1BChunk));
        rng.Fill(options.data(), options.size());
        WriteChunk(file, fileformats::SaveChunk::Setup1B, options);
    }
    if (rng.Chance(3, 4)) {
        std::vector<std::uint8_t> objectives(sizeof(fileformats::SaveSetup1EChunk));
        rng.Fill(objectives.data(), objectives.size());
        objectives[8] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(3));
        WriteChunk(file, fileformats::SaveChunk::Setup1E, objectives);
    }
    DeleteMapFile(file);
}

void CompareLoad(test::Context& ctx, LoadSaveGameFn candidate, const std::string& path, std::uint32_t multiplayer,
                 std::uint8_t amazons) {
    g_config->random_race_amazons = amazons;
    msvc6::String name;
    msvc6::String_FromChars(&name, nullptr, path.c_str(), static_cast<std::uint32_t>(path.size()));
    TestSettings a;
    TestSettings b;
    DebugLogRecorder log;
    const RandomState random = RandomState::Save();
    g_guidCounter = 0;
    const std::uint32_t ra = Original_GameSettings_LoadSaveGame(a.get(), nullptr, &name, multiplayer);
    const std::vector<DebugLogCall> logA = Logs(log);
    const RandomState randomA = RandomState::Save();
    random.Restore();
    g_guidCounter = 0;
    const std::uint32_t rb = candidate(b.get(), nullptr, &name, multiplayer);
    const std::vector<DebugLogCall> logB = Logs(log);
    S3RE_EXPECT(RandomState::Save() == randomA);
    if (ra != rb) {
        ctx.Fail(__FILE__, __LINE__, "%s (mp %u): result original %u, ours %u", path.c_str(), multiplayer, ra, rb);
    }
    CompareDescriptions(ctx, path.c_str(), Describe(a.get()), Describe(b.get()));
    if (logA != logB) {
        ctx.Fail(__FILE__, __LINE__, "%s: DebugLog calls differ (%u / %u)", path.c_str(),
                 static_cast<unsigned>(logA.size()), static_cast<unsigned>(logB.size()));
    }
    if (a.get()->map_file != nullptr && b.get()->map_file != nullptr) {
        // The dictionaries' value slots are never written (operator new
        // leaves them as they were on the heap, in both).
        for (CMapFile* file : {a.get()->map_file, b.get()->map_file}) {
            file->chunks.value_slot = 0;
            file->references.value_slot = 0;
        }
        CompareFiles(ctx, a.get()->map_file, b.get()->map_file);
    }
    msvc6::String_Destroy(&name, nullptr);
}

void RunLoadCases(test::Context& ctx, LoadSaveGameFn candidate, int synthetic) {
    if (!ModelUsable()) {
        ctx.Fail(__FILE__, __LINE__, "g_gameSettings has no vector vtables (not constructed)");
        return;
    }
    GlobalSettingsGuard guard;
    FakeGuids guids;
    std::vector<std::string> files;
    ListFiles(GameFolder() + "\\Save", files);
    files.push_back(GameFolder() + "\\Save\\does-not-exist.sav");
    for (const std::string& path : files) {
        for (std::uint32_t multiplayer = 0; multiplayer < 2; ++multiplayer) {
            CompareLoad(ctx, candidate, path, multiplayer, 1);
        }
        CompareLoad(ctx, candidate, path, 0, 0);
    }
    const std::string folder = TestFolder();
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < synthetic; ++i) {
        const std::string path = folder + "\\synthetic.sav";
        WriteSyntheticSave(rng, path);
        CompareLoad(ctx, candidate, path, rng.Below(2), static_cast<std::uint8_t>(rng.Below(2)));
        DeleteFileA(path.c_str());
    }
    RemoveDirectoryA(folder.c_str());
}

// Wrong: the first setup's colour changed.
std::uint32_t __fastcall WrongLoad(GameSettings* self, void* edx, const msvc6::String* path, std::uint32_t mp) {
    const std::uint32_t result = GameSettings_LoadSaveGame(self, edx, path, mp);
    if (self->player_setups.length > 0 && self->player_setups.data != nullptr) {
        self->player_setups.data[0].colour ^= 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Random settings for the local-player code
// ---------------------------------------------------------------------------

std::uint32_t SmallOrAny(test::Rng& rng, std::uint32_t bound) {
    return rng.Chance(1, 8) ? rng.Next32() : rng.Below(bound);
}

// Builds random settings into `s` from `seed` (the same objects for the
// same seed): setups, teams with player ids, net ids, flags.
void BuildSettings(std::uint64_t seed, GameSettings* s, bool somePlayers) {
    test::Rng rng(seed);
    s->is_savegame = static_cast<std::uint8_t>(rng.Below(2));
    s->by_clan = static_cast<std::uint8_t>(rng.Below(2));
    s->local_net_id = SmallOrAny(rng, 6);
    s->local_player_id = rng.Next32();
    std::uint32_t players = rng.Chance(1, 6) ? 0 : rng.Below(8);
    if (somePlayers && players == 0) {
        // SetupLocalPlayer on an empty player vector works on the
        // uninitialized setup the index check makes (the game always has
        // players there); the emulator spec covers that growth.
        players = 1;
    }
    for (std::uint32_t i = 0; i < players; ++i) {
        GamePlayerSetup setup;
        Call(kSetupConstruct, &setup);
        auto* bytes = reinterpret_cast<std::uint8_t*>(&setup);
        rng.Fill(bytes, 0x18);
        rng.Fill(bytes + 0x54, 0xbc - 0x54);
        setup.net_player = 0;
        setup.value10 = rng.Chance(1, 4) ? 0xffffffff : rng.Below(8);
        setup.type = rng.Below(4);
        setup.party = rng.Below(4);
        setup.clan = rng.Chance(1, 2) ? 0 : rng.Below(4);
        setup.race_byte = static_cast<std::uint8_t>(rng.Below(2));
        Call(kPlayerInsertAt, Players(s), s->player_setups.length, Addr(&setup));
        Call(kSetupDestroy, &setup);
    }
    std::uint32_t teams = rng.Below(5);
    if (somePlayers && teams == 0) {
        teams = 1;  // the same for the teams (SetupLocalPlayer by clan uses the slot's team)
    }
    for (std::uint32_t t = 0; t < teams; ++t) {
        CTeam team;
        Call(kTeamConstructId, &team, t);
        const std::uint32_t ids = rng.Below(4);
        for (std::uint32_t k = 0; k < ids; ++k) {
            Call(kAddPlayerId, &team, rng.Below(players + 1));
        }
        Call(kTeamInsertAt, &s->teams, s->teams.length, Addr(&team));
        Call(kTeamDestroy, &team);
    }
    const std::uint32_t netIds = rng.Chance(1, 3) ? players : rng.Below(8);
    for (std::uint32_t k = 0; k < netIds; ++k) {
        std::uint32_t id = SmallOrAny(rng, 6);
        Call(kIntInsertAt, &s->net_ids, s->net_ids.length, Addr(&id));
    }
    if (netIds == 0) {
        // A block for the index check to use, its element set: the check
        // makes an empty vector's element 0 "filled" without writing it
        // (slot 1, base_init_upto 0x428e50), so both versions would keep
        // what the heap block held (the emulator spec covers the growth).
        Call(kIntResize, &s->net_ids, 1);
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(s->net_ids.data)) = 0x1d1d;
    }
}

struct LocalFns {
    LocalPlayerFn setupLocal;
    SettingsResultFn lowestFree;
    SettingsArgFn findTeam;
    TeamResultFn firstClan;
    TeamArgFn checkClans;
    IndexFn playerIndex;
    IndexFn teamIndex;
    ResizeFn resizeClamp;
};

LocalFns OriginalLocalFns() {
    return {Original_GameSettings_SetupLocalPlayer, Original_GameSettings_LowestFreeNetId,
            Original_GameSettings_FindTeamForClan,  Original_CTeam_FirstClan,
            Original_CTeam_CheckClans,              Original_PlayerVector_Index,
            Original_TeamVector_Index,              Original_PlayerVector_ResizeClampLength};
}

LocalFns OurLocalFns() {
    return {&GameSettings_SetupLocalPlayer, &GameSettings_LowestFreeNetId, &GameSettings_FindTeamForClan,
            &CTeam_FirstClan,               &CTeam_CheckClans,             &PlayerVector_Index,
            &TeamVector_Index,              &PlayerVector_ResizeClampLength};
}

constexpr int kLocalOps = 7;

// One operation on settings built from `seed`; the result.
std::uint32_t RunLocalOp(const LocalFns& fns, int op, std::uint64_t seed, GameSettings* s) {
    BuildSettings(seed, s, op == 0);
    test::Rng rng(seed ^ 0x5eed);
    const std::uint32_t clan = rng.Below(5);
    switch (op) {
        case 0: return WithGlobalPlayers(s, [&] { return fns.setupLocal(s, nullptr, 0, clan); });
        case 1: return fns.lowestFree(s, nullptr);
        case 2: return WithGlobalPlayers(s, [&] { return fns.findTeam(s, nullptr, clan); });
        case 3:
        case 4: {
            if (s->teams.length == 0) {
                return 0xdead;
            }
            auto* team = reinterpret_cast<CTeam*>(static_cast<std::uintptr_t>(s->teams.data)) +
                         rng.Below(s->teams.length);
            return WithGlobalPlayers(s, [&] {
                return op == 3 ? fns.firstClan(team, nullptr) : fns.checkClans(team, nullptr, clan);
            });
        }
        case 5: {
            const std::int32_t index = rng.Range(-3, 10);
            const bool teams = rng.Chance(1, 2);
            if (rng.Chance(1, 3)) {  // an empty vector, with or without a block
                Call(teams ? kTeamResize : kPlayerResize, teams ? &s->teams : Players(s), rng.Below(2));
                if (teams) {
                    s->teams.length = 0;
                } else {
                    s->player_setups.length = 0;
                }
            }
            return static_cast<std::uint32_t>(teams ? fns.teamIndex(&s->teams, nullptr, index)
                                                    : fns.playerIndex(Players(s), nullptr, index));
        }
        default: return fns.resizeClamp(Players(s), nullptr, rng.Below(6));
    }
}

void CompareLocalOps(test::Context& ctx, const LocalFns& candidate, int cases) {
    if (!ModelUsable()) {
        ctx.Fail(__FILE__, __LINE__, "g_gameSettings has no vector vtables (not constructed)");
        return;
    }
    GlobalSettingsGuard guard;
    const LocalFns original = OriginalLocalFns();
    test::Rng& rng = ctx.rng();
    for (int c = 0; c < cases; ++c) {
        const int op = c % kLocalOps;
        const std::uint64_t seed = rng.Next64();
        TestSettings a;
        TestSettings b;
        DebugLogRecorder log;
        const std::uint32_t ra = RunLocalOp(original, op, seed, a.get());
        const std::vector<DebugLogCall> logA = Logs(log);
        const std::uint32_t rb = RunLocalOp(candidate, op, seed, b.get());
        const std::vector<DebugLogCall> logB = Logs(log);
        if (ra != rb) {
            ctx.Fail(__FILE__, __LINE__, "op %d case %d: result original %#x, ours %#x", op, c, ra, rb);
        }
        const std::string what = "local op " + std::to_string(op) + " case " + std::to_string(c);
        CompareDescriptions(ctx, what.c_str(), Describe(a.get()), Describe(b.get()));
        if (logA != logB) {
            ctx.Fail(__FILE__, __LINE__, "op %d case %d: DebugLog calls differ", op, c);
        }
    }
}

// Wrongs, one per operation.
std::uint32_t __fastcall WrongSetupLocal(GameSettings* self, void* edx, std::uint32_t net, std::uint32_t clan) {
    return GameSettings_SetupLocalPlayer(self, edx, net, clan) + 1;
}
std::uint32_t __fastcall WrongLowestFree(GameSettings* self, void* edx) {
    const std::uint32_t free = GameSettings_LowestFreeNetId(self, edx);
    return free == 0 ? 7 : free - 1;
}
std::uint32_t __fastcall WrongFindTeam(GameSettings* self, void* edx, std::uint32_t clan) {
    return GameSettings_FindTeamForClan(self, edx, clan + 1);
}
std::uint32_t __fastcall WrongFirstClan(CTeam* self, void* edx) {
    return CTeam_CheckClans(self, edx, 0);  // the last clan instead of the first
}
std::uint32_t __fastcall WrongCheckClans(CTeam* self, void* edx, std::uint32_t) {
    return CTeam_FirstClan(self, edx);
}
// Wrong: an index above the range becomes the length instead of length - 1.
std::int32_t __fastcall WrongIndex(VectorBase* self, void* edx, std::int32_t index) {
    if (self->length != 0 && index >= static_cast<std::int32_t>(self->length)) {
        return static_cast<std::int32_t>(self->length);
    }
    return PlayerVector_Index(self, edx, index);
}
std::uint32_t __fastcall WrongResizeClamp(VectorBase* self, void* edx, std::uint32_t n) {
    return PlayerVector_ResizeClampLength(self, edx, n + 1);
}

// ---------------------------------------------------------------------------
// SetType, AttachNetPlayer, Clan, GameConfig's flag
// ---------------------------------------------------------------------------

// A fake network player: +4 a pointer to a block whose +0xc is the data
// size (0x5c0f70), +0xc its name (a std::string, 0x5c0f20).
struct FakeNetPlayer {
    std::uint32_t vtable;
    std::uint32_t data;
    std::uint32_t unk08;
    msvc6::String name;
    std::uint32_t block[4];
};

struct SetupFns {
    SetTypeFn setType;
    AttachFn attach;
    SetupResultFn clan;
    ConfigFlagFn amazons;
};

void BuildSetup(std::uint64_t seed, GamePlayerSetup* setup, FakeNetPlayer* net) {
    test::Rng rng(seed);
    Call(kSetupConstruct, setup);
    auto* bytes = reinterpret_cast<std::uint8_t*>(setup);
    rng.Fill(bytes, 0x18);
    rng.Fill(bytes + 0x54, 0xbc - 0x54);
    setup->type = rng.Chance(1, 6) ? rng.Next32() : rng.Below(4);
    setup->race_byte = static_cast<std::uint8_t>(rng.Below(2));
    setup->net_player = rng.Chance(1, 3) ? Addr(net) : 0;
    for (msvc6::String* s : {&setup->text18, &setup->textBC}) {
        const std::uint32_t n = rng.Below(3) == 0 ? 0 : rng.Below(40);
        const std::string text(n, 'q');
        msvc6::String_AssignChars(s, nullptr, text.c_str(), n);
    }
    std::memset(net, 0, sizeof(*net));
    net->data = Addr(net->block);
    net->block[3] = rng.Next32();
    const std::uint32_t n = rng.Below(30);
    const std::string name(n, 'n');
    msvc6::String_AssignChars(&net->name, nullptr, name.c_str(), n);
}

std::uint32_t RunSetupOp(const SetupFns& fns, int op, std::uint64_t seed, GamePlayerSetup* setup, FakeNetPlayer* net) {
    BuildSetup(seed, setup, net);
    test::Rng rng(seed ^ 0xa77ac4);
    switch (op) {
        case 0:
            fns.setType(setup, nullptr, rng.Chance(1, 6) ? setup->type : rng.Below(5));
            return 0;
        case 1: {
            const std::uint32_t netPlayer = rng.Chance(1, 3) ? 0 : (rng.Chance(1, 2) ? setup->net_player : Addr(net));
            return fns.attach(setup, nullptr, rng.Below(8), rng.Next32(), netPlayer, rng.Chance(1, 2) ? 0 : 0x101);
        }
        case 2: return fns.clan(setup, nullptr);
        default: {
            g_config->random_race_amazons = static_cast<std::uint8_t>(rng.Below(3));
            return fns.amazons(&g_config.get(), nullptr) & 0xff;
        }
    }
}

std::vector<std::uint32_t> DescribeSetup(const GamePlayerSetup& setup) {
    std::vector<std::uint32_t> out;
    AppendBytes(out, &setup, 0x0d);  // here +0x0c is compared too (BuildSetup set it)
    AppendBytes(out, reinterpret_cast<const std::uint8_t*>(&setup) + 0x10, 0x08);
    AppendBytes(out, reinterpret_cast<const std::uint8_t*>(&setup) + 0x54, 0xbc - 0x54);
    AppendText(out, setup.text18);
    AppendText(out, setup.textBC);
    AppendWideText(out, setup.wtext28);
    return out;
}

void CompareSetupOps(test::Context& ctx, const SetupFns& candidate, int cases) {
    GlobalSettingsGuard guard;
    const SetupFns original{Original_GamePlayerSetup_SetType, Original_GamePlayerSetup_AttachNetPlayer,
                            Original_GamePlayerSetup_Clan, Original_GameConfig_RandomRaceAmazons};
    test::Rng& rng = ctx.rng();
    for (int c = 0; c < cases; ++c) {
        const int op = c % 4;
        const std::uint64_t seed = rng.Next64();
        test::CalleeStubs stubs;
        stubs.Install<0, kReadNetPlayerData>([seed](CGameData*, std::uint32_t, const std::uint32_t*) {
            return static_cast<std::uint32_t>(seed >> 8);
        });
        GamePlayerSetup a;
        GamePlayerSetup b;
        FakeNetPlayer netA;
        FakeNetPlayer netB;
        DebugLogRecorder log;
        stubs.SetWorld(reinterpret_cast<CGameData*>(&a));
        const std::uint32_t ra = RunSetupOp(original, op, seed, &a, &netA);
        const std::vector<test::StubCall> callsA = stubs.Take();
        std::vector<DebugLogCall> logA = log.Take();
        stubs.SetWorld(reinterpret_cast<CGameData*>(&b));
        const std::uint32_t rb = RunSetupOp(candidate, op, seed, &b, &netB);
        const std::vector<test::StubCall> callsB = stubs.Take();
        std::vector<DebugLogCall> logB = log.Take();
        for (std::vector<DebugLogCall>* logs : {&logA, &logB}) {
            for (DebugLogCall& call : *logs) {
                call.args[1] = 0;  // the name's buffer (the texts are compared in the setups)
            }
        }
        // net_player holds the fake's address, which differs: compare it as a marker.
        if (a.net_player == Addr(&netA)) {
            a.net_player = 1u << 31;
        }
        if (b.net_player == Addr(&netB)) {
            b.net_player = 1u << 31;
        }
        if (ra != rb) {
            ctx.Fail(__FILE__, __LINE__, "setup op %d case %d: result original %#x, ours %#x", op, c, ra, rb);
        }
        CompareDescriptions(ctx, "setup op", DescribeSetup(a), DescribeSetup(b));
        if (!(callsA == callsB) || logA != logB) {
            ctx.Fail(__FILE__, __LINE__, "setup op %d case %d: calls or DebugLog differ", op, c);
        }
        Call(kSetupDestroy, &a);
        Call(kSetupDestroy, &b);
        msvc6::String_Destroy(&netA.name, nullptr);
        msvc6::String_Destroy(&netB.name, nullptr);
    }
}

void __fastcall WrongSetType(GamePlayerSetup* self, void* edx, std::uint32_t type) {
    GamePlayerSetup_SetType(self, edx, type);
    self->flag54 ^= 2;
}
std::uint32_t __fastcall WrongAttach(GamePlayerSetup* self, void* edx, std::uint32_t id, std::uint32_t b,
                                     std::uint32_t net, std::uint32_t flag) {
    return GamePlayerSetup_AttachNetPlayer(self, edx, id + 1, b, net, flag);
}
std::uint32_t __fastcall WrongClan(const GamePlayerSetup* self, void*) {
    return self->colour;
}
std::uint32_t __fastcall WrongAmazons(const GameConfig* self, void* edx) {
    return GameConfig_RandomRaceAmazons(self, edx) ^ 1;
}

const SetupFns kOurSetupFns{&GamePlayerSetup_SetType, &GamePlayerSetup_AttachNetPlayer, &GamePlayerSetup_Clan,
                            &GameConfig_RandomRaceAmazons};

// The two lock getters on random flag bytes: AL compared.
void CompareFlags(test::Context& ctx, SettingsResultFn host, SettingsResultFn multiplayer, int cases) {
    for (int c = 0; c < cases; ++c) {
        TestSettings s;
        s.get()->is_host = static_cast<std::uint8_t>(ctx.rng().Next32());
        s.get()->multiplayer = static_cast<std::uint8_t>(ctx.rng().Next32());
        const std::uint32_t a1 = Original_GameSettings_IsHost(s.get(), nullptr) & 0xff;
        const std::uint32_t b1 = host(s.get(), nullptr) & 0xff;
        const std::uint32_t a2 = Original_GameSettings_IsMultiplayer(s.get(), nullptr) & 0xff;
        const std::uint32_t b2 = multiplayer(s.get(), nullptr) & 0xff;
        if (a1 != b1 || a2 != b2) {
            ctx.Fail(__FILE__, __LINE__, "case %d: IsHost %#x / %#x, IsMultiplayer %#x / %#x", c, a1, b1, a2, b2);
        }
    }
}

std::uint32_t __fastcall WrongIsHost(GameSettings* self, void* edx) {
    return GameSettings_IsMultiplayer(self, edx);
}

}  // namespace

S3RE_TEST(GameSettingsFlags_MatchOriginal) {
    CompareFlags(ctx, &GameSettings_IsHost, &GameSettings_IsMultiplayer, 200);
}

S3RE_TEST(GameSettingsFlags_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFlags(probe, &WrongIsHost, &GameSettings_IsMultiplayer, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettings_LoadSaveGame_MatchesOriginal) {
    RunLoadCases(ctx, &GameSettings_LoadSaveGame, 60);
}

S3RE_TEST(GameSettings_LoadSaveGame_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    RunLoadCases(probe, &WrongLoad, 4);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettingsLocalPlayer_MatchesOriginal) {
    CompareLocalOps(ctx, OurLocalFns(), kLocalOps * 120);
}

S3RE_TEST(GameSettingsLocalPlayer_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < kLocalOps; ++k) {
        LocalFns wrong = OurLocalFns();
        switch (k) {
            case 0: wrong.setupLocal = &WrongSetupLocal; break;
            case 1: wrong.lowestFree = &WrongLowestFree; break;
            case 2: wrong.findTeam = &WrongFindTeam; break;
            case 3: wrong.firstClan = &WrongFirstClan; break;
            case 4: wrong.checkClans = &WrongCheckClans; break;
            case 5: wrong.playerIndex = &WrongIndex; break;
            case 6: wrong.resizeClamp = &WrongResizeClamp; break;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareLocalOps(probe, wrong, kLocalOps * 40);
        if (!S3RE_EXPECT(probe.failures() > 0)) {
            ctx.Fail(__FILE__, __LINE__, "  wrong implementation %d not caught", k);
        }
    }
}

S3RE_TEST(GamePlayerSetupState_MatchesOriginal) {
    CompareSetupOps(ctx, kOurSetupFns, 4 * 150);
}

S3RE_TEST(GamePlayerSetupState_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 4; ++k) {
        SetupFns wrong = kOurSetupFns;
        switch (k) {
            case 0: wrong.setType = &WrongSetType; break;
            case 1: wrong.attach = &WrongAttach; break;
            case 2: wrong.clan = &WrongClan; break;
            case 3: wrong.amazons = &WrongAmazons; break;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareSetupOps(probe, wrong, 4 * 40);
        if (!S3RE_EXPECT(probe.failures() > 0)) {
            ctx.Fail(__FILE__, __LINE__, "  wrong implementation %d not caught", k);
        }
    }
}

}  // namespace s3re::game
