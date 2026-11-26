// The player info table (g_playerInfo 0x7acfa8: 20 records of 0x44 bytes,
// globals.h PlayerInfo), filled when a world has been built from a map.
// Tests in player_info_tests.cpp.
//
// CGameData::InitPlayerInfo (0x004fd0d0): () thiscall on the world, no
//   result; called by InitAfterMapLoad 0x4fd020 only. g_strengthDivisor
//   (0x7acfa4, the active player count) = 0; then for each player p:
//   record +0x00, +0x04 = 0; the name +0x08 = "" (std::string::operator=
//   of the static "" inlined: a local string from it, assign(local, 0,
//   npos), the local freed); then by the type of g_gameSettings'
//   setup p (PlayerSetup 0x4351e0, the getter 0x49f350):
//     1 (the local player): in multiplayer (IsMultiplayer 0x4318d0) the
//       lobby's player name (0x5db260 into a local, network code called as
//       the original; assign(name, 0, npos); the local destroyed: erase(0,
//       npos) 0x407b10, _Tidy(true)); else text 0x900c1 (TextAddress
//       0x5faaa0, language -1);
//     3: "(" + the lobby name + ")" (0x408e50, 0x401c70; the temporaries
//       destroyed in the original's order, one _Tidy(true) inlined);
//     2 (computer): text 0x90053;
//     other: the name stays "".
//   A text that TextAddress doesn't have (null) assigns "" directly
//   (assign_cstr_n 0x4021c0, once inlined); otherwise through a local as
//   above. Then +0x18 = the world player's +0x0c (team), +0x1c = p,
//   +0x20..+0x38 = 0; a playing player (its +0x00 non-zero) gets
//   LogPlayerLandResources(p) 0x507f60, +0x88 = 1 and one more in
//   g_strengthDivisor, any other +0x88 = 0. Last g_gameOutcome = -1.
//
// Status: reimplemented with an in-process test; no emulator spec yet
// (S3RE_HOOK_UNVERIFIED, backlog docs/todo/file-formats.md).
//
// Known difference: the local strings' allocator bytes are uninitialized
// stack bytes of the original's frame; ours are 0 (the strings are freed
// in the function, docs/reimplementation_notes.md case b).

#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using msvc6::String;

constexpr std::uint32_t kTypeLocalPlayer = 1;  // GamePlayerSetup::type
constexpr std::uint32_t kTypeComputer = 2;
constexpr std::uint32_t kType3 = 3;            // shown as "(name)"
constexpr std::uint32_t kTextLocalPlayer = 0x900c1;  // the single player's name (a text id, raw-ok)
constexpr std::uint32_t kTextComputer = 0x90053;     // a computer player's name (a text id, raw-ok)
constexpr std::uint32_t kCurrentLanguage = 0xffffffff;
constexpr std::int32_t kOutcomeUndecided = -1;
constexpr std::size_t kZeroedFields = 9;  // PlayerInfo::fields[2..10]

constexpr std::uintptr_t kStringTidy = 0x00402170;
constexpr std::uintptr_t kStringAssignChars = 0x004021c0;
constexpr std::uintptr_t kStringAssignSubstr = 0x00401d60;
constexpr std::uintptr_t kStringErase = 0x00407b10;
constexpr std::uintptr_t kConcatCharsLeft = 0x00408e50;  // text + string
constexpr std::uintptr_t kConcatChars = 0x00401c70;      // string + text
constexpr std::uintptr_t kPlayerSetup = 0x004351e0;      // GameSettings::PlayerSetup
constexpr std::uintptr_t kIsMultiplayer = 0x004318d0;
constexpr std::uintptr_t kLobbyPlayerName = 0x005db260;  // (std::string* out) -> out, network code
constexpr std::uintptr_t kTextAddress = 0x005faaa0;      // CGraphicsData::TextAddress
constexpr std::uintptr_t kLogPlayerLand = 0x00507f60;    // CGameData::LogPlayerLandResources

template <typename R = std::uint32_t, typename... Args>
R CallThis(std::uintptr_t address, const void* self, Args... args) {
    using Fn = R(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

template <typename T>
std::uint32_t AddressOf(const T* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

std::uint32_t Npos() {
    return g_stringNpos;
}

void AssignString(String* name, const String* from) {
    CallThis(kStringAssignSubstr, name, AddressOf(from), 0, Npos());
}

// std::string::operator=(const char*) as inlined here: a null text assigns
// "" directly, any other through a local copy.
void AssignText(String* name, const char* text) {
    if (text == nullptr) {
        CallThis(kStringAssignChars, name, AddressOf(texts::kEmpty.c_str()),
                 static_cast<std::uint32_t>(std::strlen(texts::kEmpty)));
        return;
    }
    String local{};
    CallThis<void>(kStringTidy, &local, 0);
    CallThis(kStringAssignChars, &local, AddressOf(text), static_cast<std::uint32_t>(std::strlen(text)));
    AssignString(name, &local);
    CallThis<void>(kStringTidy, &local, 1);
}

// ~std::string as compiled here: erase(0, npos), then _Tidy(true).
void DestroyString(String* s) {
    CallThis(kStringErase, s, 0, Npos());
    CallThis<void>(kStringTidy, s, 1);
}

const GamePlayerSetup* SetupOf(std::uint32_t player) {
    return CallThis<const GamePlayerSetup*>(kPlayerSetup, &g_gameSettings.get(), player);
}

const char* Text(std::uint32_t id) {
    return CallThis<const char*>(kTextAddress, g_gfxData.get(), id, kCurrentLanguage);
}

// The lobby's name of setup `player` into `out`; the returned string.
String* LobbyName(std::uint32_t player, String* out) {
    return CallThis<String*>(kLobbyPlayerName, SetupOf(player), AddressOf(out));
}

void SetName(String* name, std::uint32_t player) {
    const std::uint32_t type = SetupOf(player)->type;  // (the getter 0x49f350)
    if (type == kTypeLocalPlayer) {
        if ((CallThis(kIsMultiplayer, &g_gameSettings.get()) & 0xff) != 0) {
            String lobby;
            AssignString(name, LobbyName(player, &lobby));
            DestroyString(&lobby);
        } else {
            AssignText(name, Text(kTextLocalPlayer));
        }
    } else if (type == kType3) {
        String lobby;
        String* got = LobbyName(player, &lobby);
        String open;
        reinterpret_cast<String*(__fastcall*)(String*, const char*, const String*)>(kConcatCharsLeft)(
            &open, texts::kOpenParenthesis.c_str(), got);
        String closed;
        reinterpret_cast<String*(__fastcall*)(String*, const String*, const char*)>(kConcatChars)(
            &closed, &open, texts::kCloseParenthesis.c_str());
        AssignString(name, &closed);
        DestroyString(&closed);
        DestroyString(&open);  // (its _Tidy inlined in the original)
        DestroyString(&lobby);
    } else if (type == kTypeComputer) {
        AssignText(name, Text(kTextComputer));
    }
}

}  // namespace

using WorldFn = void(__fastcall*)(CGameData* world, void* edx);
void __fastcall CGameData_InitPlayerInfo(CGameData* world, void* edx);
S3RE_HOOK_UNVERIFIED(0x004fd0d0, CGameData_InitPlayerInfo, WorldFn, Original_CGameData_InitPlayerInfo, CGameData_InitPlayerInfo);

// Purpose: Fills the table of player names and values that the statistics
// and the end-of-game screen show (human, lobby or computer names), and
// counts the players taking part.
void __fastcall CGameData_InitPlayerInfo(CGameData* world, void*) {
    g_strengthDivisor = 0;
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        PlayerInfo& info = g_playerInfo[p];
        info.unk00 = 0;
        info.unk04 = 0;
        AssignText(&info.name, texts::kEmpty.c_str());
        SetName(&info.name, p);
        layout::Player& player = world->players[p];
        info.fields[0] = player.team;
        info.fields[1] = static_cast<std::int32_t>(p);
        for (std::size_t k = 0; k < kZeroedFields; ++k) {
            info.fields[2 + k] = 0;
        }
        if (player.active != 0) {
            CallThis(kLogPlayerLand, world, p);
            player.unk088 = 1;
            g_strengthDivisor = g_strengthDivisor + 1;
        } else {
            player.unk088 = 0;
        }
    }
    g_gameOutcome = kOutcomeUndecided;
}

}  // namespace s3re::game
