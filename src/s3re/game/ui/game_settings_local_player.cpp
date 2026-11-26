// Choosing the local player of a game's setup (Dirk\GameParams.cpp and
// Dirk\nethelp.cpp's units): GameSettings::SetupLocalPlayer, which
// NewGame, LoadSaveGame and the lobby call once the player setups exist,
// and the helpers it uses. Layouts in game_settings_layout.h; tests in
// game_settings_load_tests.cpp. All thiscall. The vector accesses are the
// Watcom index checks of game_settings_vectors.h (inlined in the original).
//
// GameSettings::SetupLocalPlayer (0x00433e50): (netPlayer, clan) ret 8, EAX =
//   the network id it gives the local player. Holds the settings' lock
//   (Win32::CLock). Finds the local player's slot:
//   - loading a savegame (+0x28): DebugLog("CFG: Searching for NetId %i",
//     +0xac), then the first slot whose entry in the net-id vector +0x90
//     (watcom::PtrVector index check) is +0xac;
//   - otherwise without +0x2b: the first slot of type 0 (human);
//   - with +0x2b: team = FindTeamForClan(clan) (0x433a90), the first human
//     slot of that party; then CTeam::CheckClans(clan) (0x4a0e10) on the
//     team of the slot found (the slot's party through the team vector's
//     index check).
//   Each loop runs while the slot is below the player count (unsigned,
//   re-read); not found, it ends at the count (the accesses below clamp
//   it). Then id = LowestFreeNetId (0x433c80), +0x78 = id, SetType(1)
//   (0x49f360) on the slot, AttachNetPlayer(id, netPlayer ? [netPlayer+8] :
//   0, netPlayer, 1) (0x49f190) on it, and LogSlot (0x4a01c0) on every slot,
//   whose index check grows an empty vector inline (operator new, the copy
//   constructor 0x49eaf0, the scalar deleting destructor 0x437fc0 with 0).
// GameSettings::LowestFreeNetId (0x00433c80): () -> EAX. Under the lock, a
//   local WCValSortedVector<int> (WCIntVector_Construct 0x4376b0 with 10, 5;
//   vtable 0x655a0c) gets every setup's network id +0x10 other than -1 (the
//   insert position from the vector's virtual find, slot 3, with mode 3
//   when it is not empty, else 0; InsertAt 0x428d10); the result is the
//   first position i whose id is not i (or the count). The local's block is
//   freed (vtables WCBareVectorBase<int>, then WCExcept).
// GameSettings::FindTeamForClan (0x00433a90): (clan) ret 4 -> EAX: the
//   party of the first setup whose clan (+0xb8) is `clan` (the setup vector's
//   index check grows through 0x438070); else the first team whose
//   CTeam::FirstClan (0x4a0cd0) is 0; else -1. Quirk kept: a matching setup
//   whose party is -1 makes the first loop run forever (it doesn't advance
//   after a match, and -1 means "not found").
// Dirk::CTeam::FirstClan (0x004a0cd0): () -> EAX: the first non-zero clan
//   of the team's players (g_gameSettings.PlayerSetup(id) 0x4351e0 for each
//   id of the int vector +8, which goes through the pointer vector's index
//   check), or 0.
// Dirk::CTeam::CheckClans (0x004a0e10): (clan) ret 4: the same loop; the
//   argument and the first non-zero clan it computes are unused. EAX: the
//   last player's clan (0 without players). Probably a check compiled away.
// GamePlayerSetup::SetType (0x0049f360; Dirk::CPlayer): (type) ret 4, no
//   result. Nothing if +0x5c is `type`. Leaving type 1 (the local player):
//   +0x10 = -1, +0x04, +0x00, +0x58 = 0, byte +0x0c = 0, the 16-bit string
//   +0x28 = the text at 0x7a8248 (std::wstring::assign(const wchar_t*)
//   inlined: _Xlen above 0x7ffffffd, _Grow(n, true)), +0x64 = 0xff (random
//   race) if byte +0x68 is 0 and `type` isn't 3, +0x6c = +0x64, +0x70 = +0x14,
//   +0x18 = "" (assign 0x413d70) unless `type` is 3, +0x78, +0xa4, +0xb8 = 0.
//   Then +0x5c = type; for a type other than 1 and 3 byte +0x54 = 1 and
//   +0x64 = 0xff if byte +0x68 is 0. Last, if g_gameSettings.IsHost()
//   (0x4318a0), 0x49fea0 (network).
// GamePlayerSetup::AttachNetPlayer (0x0049f190): (id, b, netPlayer, flag)
//   ret 0x10: byte +0x0c = flag, +0x04 = b, +0x10 = id, +0x00 = netPlayer.
//   Without a net player byte +0x54 = 1, with a new one 0; in both cases
//   the 16-bit string +0x28 = the text at 0x7a8248 (assign with _Grow
//   0x4288a0 called), +0x6c = +0x64, +0x70 = +0x14, +0x78, +0xa4, +0x58,
//   +0x08, +0xb8 = 0. With a net player: +0x18 = the net player's name (the
//   std::string at netPlayer+0xc, getter 0x5c0f20; std::string::assign(x,
//   0, npos) inlined), DebugLog("NET: Reading name %i %s", id, name),
//   0x49fbc0 (reads the net player's data: network code, called as the
//   original), and with `flag` +0x08 = 0x5c0f70 (a size of the net
//   player's data). EAX: +0x14 without a net player; else +0x08 with
//   `flag`, else 0x49fbc0's result with the low byte 0.
// GamePlayerSetup::Clan (0x004a01b0): () -> EAX = +0xb8.

#include "game/debug_log.h"
#include "game/framework/critical_section.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/ui/game_settings_vectors.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "watcom/ptr_vector.h"
#include "watcom/vector_base.h"

#include <cstdint>

namespace s3re::game {

namespace {

using watcom::VectorBase;

constexpr std::uint32_t kHuman = 0;        // GamePlayerSetup::type
constexpr std::uint32_t kLocalPlayer = 1;  // set by SetupLocalPlayer, reset by SetType
constexpr std::uint32_t kType3 = 3;        // unknown; keeps the race and the text +0x18 in SetType
constexpr std::uint32_t kNoNetId = 0xffffffff;
constexpr std::uint32_t kRandomRace = 0xff;
constexpr std::int32_t kNoTeam = -1;
constexpr std::uint32_t kIdCapacity = 10;  // the local id vector of LowestFreeNetId
constexpr std::uint32_t kIdGrow = 5;
constexpr std::uint32_t kFindInsertPosition = 3;  // the sorted vector's find mode (slot 3)

// The original functions these call (ours in the game, through their
// S3.EXE addresses as the original does).
constexpr std::uintptr_t kFindTeamForClan = 0x00433a90;
constexpr std::uintptr_t kLowestFreeNetId = 0x00433c80;
constexpr std::uintptr_t kCheckClans = 0x004a0e10;
constexpr std::uintptr_t kFirstClan = 0x004a0cd0;
constexpr std::uintptr_t kSetType = 0x0049f360;
constexpr std::uintptr_t kAttachNetPlayer = 0x0049f190;
constexpr std::uintptr_t kLogSlot = 0x004a01c0;
constexpr std::uintptr_t kPlayerSetup = 0x004351e0;      // GameSettings::PlayerSetup
constexpr std::uintptr_t kResizeClampLength = 0x00438070;  // WCValOrderedVector<CPlayer>::resize
constexpr std::uintptr_t kIntVectorConstruct = 0x004376b0;
constexpr std::uintptr_t kIntVectorInsertAt = 0x00428d10;
constexpr std::uintptr_t kAssignTextOrEmpty = 0x00413d70;  // std::string::assign_cstr_or_empty
constexpr std::uintptr_t kWStringGrow = 0x004288a0;
constexpr std::uintptr_t kIsHost = 0x004318a0;              // GameSettings::IsHost
constexpr std::uintptr_t kHostSetupChanged = 0x0049fea0;    // network (not reimplemented)
constexpr std::uintptr_t kNetPlayerName = 0x005c0f20;       // the net player's name (netPlayer + 0xc)
constexpr std::uintptr_t kNetPlayerDataSize = 0x005c0f70;
constexpr std::uintptr_t kReadNetPlayerData = 0x0049fbc0;    // network (not reimplemented)

template <typename R = std::uint32_t, typename... Args>
R CallThis(std::uintptr_t address, const void* self, Args... args) {
    using Fn = R(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

template <typename T>
std::uint32_t AddressOf(const T* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

VectorBase* PlayersOf(GameSettings* settings) {
    return reinterpret_cast<VectorBase*>(&settings->player_setups);
}

GamePlayerSetup* SetupData(VectorBase* players, std::int32_t index) {
    return reinterpret_cast<GamePlayerSetup*>(static_cast<std::uintptr_t>(
        players->data + static_cast<std::uint32_t>(index) * sizeof(GamePlayerSetup)));
}

// operator[] of the player vector (the out-of-line check 0x437950).
GamePlayerSetup* SetupAt(VectorBase* players, std::int32_t index) {
    const std::int32_t i = PlayerVectorIndex(players, index);
    return SetupData(players, i);
}

CTeam* TeamAt(VectorBase* teams, std::int32_t index) {
    const std::int32_t i = TeamVectorIndex(teams, index);
    return reinterpret_cast<CTeam*>(static_cast<std::uintptr_t>(teams->data + static_cast<std::uint32_t>(i) * sizeof(CTeam)));
}

// The int vectors (the net ids, a team's player ids, the local id vector)
// use the pointer vector's code.
std::uint32_t IntAt(VectorBase* ints, std::int32_t index) {
    return watcom::PtrVector_At(reinterpret_cast<watcom::PtrVector*>(ints), index);
}

// SetupLocalPlayer's last index check grows an empty vector inline.
std::uint32_t GrowWithOwnCopy(VectorBase* players) {
    return GrowPlayerVectorInline(players);
}

// FindTeamForClan's first index check grows through 0x438070.
std::uint32_t GrowWithResizeClamp(VectorBase* players) {
    return CallThis(kResizeClampLength, players, 1);
}

// The loop of FirstClan and CheckClans: the first non-zero clan of the
// team's players; `last` gets the last clan read.
std::uint32_t ScanClans(CTeam* team, std::uint32_t* last) {
    std::uint32_t first = 0;
    for (std::int32_t i = 0; static_cast<std::uint32_t>(i) < team->player_ids.length; ++i) {
        const std::uint32_t id = IntAt(&team->player_ids, i);
        const auto* setup = CallThis<const GamePlayerSetup*>(kPlayerSetup, &g_gameSettings.get(), id);
        const std::uint32_t clan = setup->clan;  // (the getter 0x4a01b0)
        *last = clan;
        if (clan != 0 && first == 0) {
            first = clan;
        }
    }
    return first;
}

// std::wstring::assign(const wchar_t*) of SetType and AttachNetPlayer: the
// text at 0x7a8248; `grow` is _Grow(n, true) (inlined in SetType, called in
// AttachNetPlayer).
template <typename Grow>
void AssignSetupWideText(msvc6::WString* text, Grow grow) {
    const std::uint16_t* source = g_setupWideText.get();
    std::uint32_t n = 0;
    while (source[n] != 0) {  // (wcslen 0x61fe1b)
        ++n;
    }
    if (!grow(text, n)) {
        return;
    }
    for (std::uint32_t k = 0; k < n; ++k) {
        text->ptr[k] = source[k];
    }
    text->len = n;
    text->ptr[n] = 0;
}

// The fields SetType and AttachNetPlayer reset alike after the 16-bit text.
void CopyRaceAndParty(GamePlayerSetup* setup) {
    setup->race6c = setup->race;
    setup->party70 = setup->party;
}

}  // namespace

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

std::uint32_t __fastcall GameSettings_SetupLocalPlayer(GameSettings* self, void* edx, std::uint32_t netPlayer,
                                                       std::uint32_t clan);
std::uint32_t __fastcall GameSettings_LowestFreeNetId(GameSettings* self, void* edx);
std::uint32_t __fastcall GameSettings_FindTeamForClan(GameSettings* self, void* edx, std::uint32_t clan);
std::uint32_t __fastcall CTeam_FirstClan(CTeam* self, void* edx);
std::uint32_t __fastcall CTeam_CheckClans(CTeam* self, void* edx, std::uint32_t clan);
void __fastcall GamePlayerSetup_SetType(GamePlayerSetup* self, void* edx, std::uint32_t type);
std::uint32_t __fastcall GamePlayerSetup_AttachNetPlayer(GamePlayerSetup* self, void* edx, std::uint32_t id,
                                                         std::uint32_t b, std::uint32_t netPlayer, std::uint32_t flag);
std::uint32_t __fastcall GamePlayerSetup_Clan(const GamePlayerSetup* self, void* edx);

S3RE_HOOK(0x00433e50, GameSettings_SetupLocalPlayer, LocalPlayerFn, Original_GameSettings_SetupLocalPlayer,
          GameSettings_SetupLocalPlayer);
S3RE_HOOK(0x00433c80, GameSettings_LowestFreeNetId, SettingsResultFn, Original_GameSettings_LowestFreeNetId,
          GameSettings_LowestFreeNetId);
S3RE_HOOK(0x00433a90, GameSettings_FindTeamForClan, SettingsArgFn, Original_GameSettings_FindTeamForClan,
          GameSettings_FindTeamForClan);
S3RE_HOOK(0x004a0cd0, CTeam_FirstClan, TeamResultFn, Original_CTeam_FirstClan, CTeam_FirstClan);
S3RE_HOOK(0x004a0e10, CTeam_CheckClans, TeamArgFn, Original_CTeam_CheckClans, CTeam_CheckClans);
S3RE_HOOK(0x0049f360, GamePlayerSetup_SetType, SetTypeFn, Original_GamePlayerSetup_SetType, GamePlayerSetup_SetType);
S3RE_HOOK(0x0049f190, GamePlayerSetup_AttachNetPlayer, AttachFn, Original_GamePlayerSetup_AttachNetPlayer,
          GamePlayerSetup_AttachNetPlayer);
S3RE_HOOK(0x004a01b0, GamePlayerSetup_Clan, SetupResultFn, Original_GamePlayerSetup_Clan, GamePlayerSetup_Clan);

// Purpose: Decides which player slot of a new or loaded game is the one
// played on this computer, gives it a free network id and marks it as the
// local player (the net player object, if any, is attached to it).
std::uint32_t __fastcall GameSettings_SetupLocalPlayer(GameSettings* self, void*, std::uint32_t netPlayer,
                                                       std::uint32_t clan) {
    GameLock lock(self->lock);
    VectorBase* players = PlayersOf(self);
    std::int32_t slot = 0;
    auto inRange = [self](std::int32_t i) { return static_cast<std::uint32_t>(i) < self->player_setups.length; };
    if (self->is_savegame != 0) {
        GameDebugLog(texts::kSearchingForNetId, self->local_net_id);
        for (; inRange(slot); ++slot) {
            if (IntAt(&self->net_ids, slot) == self->local_net_id) {
                break;
            }
        }
    } else if (self->by_clan == 0) {
        for (; inRange(slot); ++slot) {
            if (SetupAt(players, slot)->type == kHuman) {
                break;
            }
        }
    } else {
        const std::uint32_t team = CallThis(kFindTeamForClan, self, clan);
        for (; inRange(slot); ++slot) {
            if (SetupAt(players, slot)->type == kHuman && SetupAt(players, slot)->party == team) {
                break;
            }
        }
        const std::uint32_t party = SetupAt(players, slot)->party;
        CallThis(kCheckClans, TeamAt(&self->teams, static_cast<std::int32_t>(party)), clan);
    }

    const std::uint32_t id = CallThis(kLowestFreeNetId, self);
    self->local_player_id = id;
    CallThis<void>(kSetType, SetupAt(players, slot), kLocalPlayer);
    // The net player's +8 is read before the index check (0x433ff8).
    const std::uint32_t b = netPlayer != 0 ? *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(netPlayer) + 8)
                                           : 0;
    CallThis(kAttachNetPlayer, SetupAt(players, slot), id, b, netPlayer, 1);

    for (std::int32_t i = 0; inRange(i); ++i) {
        const std::int32_t index = CheckedVectorIndex(players, i, &GrowWithOwnCopy);
        CallThis(kLogSlot, SetupData(players, index));
    }
    return id;
}

// Purpose: The lowest network id no player setup uses yet (given to the
// local player).
std::uint32_t __fastcall GameSettings_LowestFreeNetId(GameSettings* self, void*) {
    GameLock lock(self->lock);
    VectorBase ids;
    CallThis(kIntVectorConstruct, &ids, kIdCapacity, kIdGrow);
    ids.vtable = vtables::kWCValSortedVector_int;
    VectorBase* players = PlayersOf(self);
    for (std::int32_t i = 0; static_cast<std::uint32_t>(i) < self->player_setups.length; ++i) {
        if (SetupAt(players, i)->value10 == kNoNetId) {
            continue;
        }
        std::uint32_t id = SetupAt(players, i)->value10;
        std::int32_t position = 0;
        if (ids.length != 0) {
            using FindFn = void(__fastcall*)(VectorBase*, void*, const std::uint32_t*, std::uint32_t, std::int32_t*);
            reinterpret_cast<const FindFn*>(static_cast<std::uintptr_t>(ids.vtable))[3](&ids, nullptr, &id,
                                                                                          kFindInsertPosition, &position);
        }
        CallThis(kIntVectorInsertAt, &ids, position, AddressOf(&id));
    }
    std::uint32_t free = 0;
    for (; free < ids.length; ++free) {
        if (IntAt(&ids, static_cast<std::int32_t>(free)) != free) {
            break;
        }
    }
    // ~WCValSortedVector<int>, inlined.
    ids.vtable = vtables::kWCBareVectorBase_int;
    if (ids.data != 0) {
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(ids.data)));
    }
    ids.vtable = vtables::kWCExcept;
    return free;
}

// Purpose: The party the local player joins in a game set up by clans:
// the party of the setup with that clan, else the first team without a
// clan, else -1.
std::uint32_t __fastcall GameSettings_FindTeamForClan(GameSettings* self, void*, std::uint32_t clan) {
    std::uint32_t team = static_cast<std::uint32_t>(kNoTeam);
    VectorBase* players = PlayersOf(self);
    std::int32_t i = 0;
    while (static_cast<std::uint32_t>(i) < self->player_setups.length && team == static_cast<std::uint32_t>(kNoTeam)) {
        const std::int32_t index = CheckedVectorIndex(players, i, &GrowWithResizeClamp);
        if (SetupData(players, index)->clan != clan) {
            ++i;
            continue;
        }
        // Deliberately no ++i: a party of -1 repeats the same slot forever, as in the original.
        team = SetupData(players, CheckedVectorIndex(players, i, &GrowWithResizeClamp))->party;
    }
    if (team != static_cast<std::uint32_t>(kNoTeam)) {
        return team;
    }
    for (std::int32_t t = 0; static_cast<std::uint32_t>(t) < self->teams.length;) {
        if (CallThis(kFirstClan, TeamAt(&self->teams, t)) == 0) {
            return static_cast<std::uint32_t>(t);
        }
        ++t;
    }
    return team;
}

// Purpose: The first clan set among a team's players (0: none), so that a
// team without one can be offered to a clan.
std::uint32_t __fastcall CTeam_FirstClan(CTeam* self, void*) {
    std::uint32_t last = 0;
    return ScanClans(self, &last);
}

// Purpose: Probably a check of a team's clans that does nothing in the
// release build: it looks at every player's clan and keeps nothing.
std::uint32_t __fastcall CTeam_CheckClans(CTeam* self, void*, std::uint32_t /*clan: unused*/) {
    std::uint32_t last = 0;
    ScanClans(self, &last);
    return last;
}

// Purpose: Changes what kind of player a setup slot is (human, the local
// player, computer, ...); a slot that stops being the local player loses
// its network identity.
void __fastcall GamePlayerSetup_SetType(GamePlayerSetup* self, void*, std::uint32_t type) {
    if (self->type == type) {
        return;
    }
    if (self->type == kLocalPlayer) {
        self->value10 = kNoNetId;
        self->id04 = 0;
        self->net_player = 0;
        self->value58 = 0;
        self->attach_flag = 0;
        AssignSetupWideText(&self->wtext28, [](msvc6::WString* text, std::uint32_t n) {
            return msvc6::WString_Grow(text, nullptr, n, 1);  // (inlined in the original)
        });
        if (self->race_byte == 0 && type != kType3) {
            self->race = kRandomRace;
        }
        CopyRaceAndParty(self);
        if (type != kType3) {
            CallThis(kAssignTextOrEmpty, &self->text18, reinterpret_cast<std::uintptr_t>(texts::kEmpty.c_str()));
        }
        self->value78 = 0;
        self->valueA4 = 0;
        self->clan = 0;
    }
    self->type = type;
    if (type != kLocalPlayer && type != kType3) {
        self->flag54 = 1;
        if (self->race_byte == 0) {
            self->race = kRandomRace;
        }
    }
    if ((CallThis(kIsHost, &g_gameSettings.get()) & 0xff) != 0) {
        CallThis(kHostSetupChanged, self);
    }
}

// Purpose: Connects a player setup slot with a network player (or with
// none) and gives it its network id; the slot takes the net player's name.
std::uint32_t __fastcall GamePlayerSetup_AttachNetPlayer(GamePlayerSetup* self, void*, std::uint32_t id,
                                                         std::uint32_t b, std::uint32_t netPlayer, std::uint32_t flag) {
    const std::uint32_t previous = self->net_player;
    self->attach_flag = static_cast<std::uint8_t>(flag);
    self->id04 = b;
    self->value10 = id;
    self->net_player = netPlayer;
    if (netPlayer == 0 || netPlayer != previous) {
        self->flag54 = netPlayer == 0 ? 1 : 0;
        AssignSetupWideText(&self->wtext28, [](msvc6::WString* text, std::uint32_t n) {
            return (CallThis(kWStringGrow, text, n, 1) & 0xff) != 0;
        });
        CopyRaceAndParty(self);
        self->value78 = 0;
        self->valueA4 = 0;
        self->value58 = 0;
        self->version = 0;
        self->clan = 0;
    }
    if (self->net_player == 0) {
        return self->party;  // (EAX still holds +0x14 from the reset)
    }
    const auto* name = CallThis<const msvc6::String*>(kNetPlayerName, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(self->net_player)));
    // std::string::assign(name, 0, npos), inlined in the original (the
    // self-assignment branch is dead: the name isn't the setup's string).
    msvc6::String_AssignSubstr(&self->text18, nullptr, name, 0, static_cast<std::uint32_t>(g_stringNpos));
    const char* text = self->text18.ptr != nullptr ? self->text18.ptr : g_stringNullBuffer.get();
    GameDebugLog(texts::kReadingName, id, text);
    const std::uint32_t read = CallThis(kReadNetPlayerData, self);
    if (self->attach_flag == 0) {
        return read & 0xffffff00;  // (AL = the flag)
    }
    self->version = CallThis(kNetPlayerDataSize, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(self->net_player)));
    return self->version;
}

// Purpose: The clan a player setup belongs to (0: none).
std::uint32_t __fastcall GamePlayerSetup_Clan(const GamePlayerSetup* self, void*) {
    return self->clan;
}

}  // namespace s3re::game
