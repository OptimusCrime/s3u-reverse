#pragma once

// Layout of the game settings object (g_gameSettings, the global object at
// 0x7a7a18; no RTTI, so the name is ours; Dirk\GameParams.cpp and
// neighbours). It describes the game being started or loaded: the map, the
// seed, the players. GameSettings::NewGame 0x431ab0 and
// GameSettings::LoadSaveGame 0x432b00 fill it; CLogicThread::OnStart and the
// savegame code read it. Only the fields our code uses are named.

#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "watcom/vector_base.h"

namespace s3re::game::fileformats {
struct CMapFile;
}

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

// One player's setup (0xcc bytes; the vector at GameSettings +0x58;
// filled by GamePlayerSetup::Set 0x49f120). Members: constructor 0x49e8f0,
// copy constructor 0x49eaf0, operator= 0x49edf0, destructor 0x49ec80 (it
// owns std::strings at +0x18 and +0xbc, a 16-bit string at +0x28 and a
// Watcom vector at +0x38, which the copy functions don't copy).
struct GamePlayerSetup {
    std::uint32_t net_player;  // +0x00 the network player object (AttachNetPlayer 0x49f190; 0: none)
    std::uint32_t id04;     // +0x04 the second of the "SLOT: Ids: %i %x" (GamePlayerSetup::LogSlot 0x4a01c0);
                            //       AttachNetPlayer's second argument
    std::uint32_t version;  // +0x08 "Version" in that log line (AttachNetPlayer: a size of the net player's
                            //       data, or 0)
    std::uint8_t attach_flag;  // +0x0c AttachNetPlayer's fourth argument
    std::uint8_t unk00d[3];
    std::uint32_t value10;  // +0x10 saved as a byte in chunk 0x14 (getter 0x49f4d0); the first of the
                            //       "SLOT: Ids": the network id (SetupLocalPlayer passes the lowest free one)
    std::uint32_t party;    // +0x14 Set's third argument (getter 0x49f590)
    msvc6::String text18;   // +0x18 the network player's name (AttachNetPlayer; "NET: Reading name %i %s")
    msvc6::WString wtext28; // +0x28 a 16-bit string, reset to the text at 0x7a8248 by SetType / AttachNetPlayer
    std::uint8_t unk038[0x54 - 0x38];  // +0x38 a WCValOrderedVector<unsigned short> (not used here)
    std::uint8_t flag54;    // +0x54
    std::uint8_t unk055[3];
    std::uint32_t value58;  // +0x58
    std::uint32_t type;     // +0x5c 0 human, 1 the local player (SetupLocalPlayer), 2 computer, 3 (unknown)
                            //       (Set's first argument; getter 0x49f350; SetType 0x49f360)
    std::uint8_t byte60;    // +0x60 Set's second argument
    std::uint8_t unk061[3];
    std::uint32_t race;     // +0x64 0xff = random (Set's fourth argument; getter GetMemberDword64 0x49f510)
    std::uint8_t race_byte; // +0x68 Set's fifth argument (a byte; getter 0x49f580)
    std::uint8_t unk069[3];
    std::uint32_t race6c;   // +0x6c a copy of the race (Set when -1; SetType / AttachNetPlayer)
    std::uint32_t party70;  // +0x70 a copy of the party (the same)
    std::uint8_t unk074[4];
    std::uint32_t value78;  // +0x78 cleared by SetType / AttachNetPlayer
    std::uint8_t unk07c[0xa4 - 0x7c];
    std::uint32_t valueA4;  // +0xa4 cleared by SetType / AttachNetPlayer
    std::uint32_t saved_values[3];  // +0xa8 Set's arguments 6..8; chunk 0x14 +0xc..+0x14 (getter 0x49fed0)
    std::uint32_t colour;  // +0xb4 colour index: LoadMap copies it into g_playerColourIndex (getter 0x49fec0)
    std::uint32_t clan;    // +0xb8 "Clan" in the SLOT log line (0x4a01c0; getter Clan 0x4a01b0)
    msvc6::String textBC;  // +0xbc the text that log line ends with (probably the player's name)
};
static_assert(sizeof(GamePlayerSetup) == 0xcc);
static_assert(offsetof(GamePlayerSetup, id04) == 0x04);
static_assert(offsetof(GamePlayerSetup, version) == 0x08);
static_assert(offsetof(GamePlayerSetup, value10) == 0x10);
static_assert(offsetof(GamePlayerSetup, attach_flag) == 0x0c);
static_assert(offsetof(GamePlayerSetup, text18) == 0x18);
static_assert(offsetof(GamePlayerSetup, wtext28) == 0x28);
static_assert(offsetof(GamePlayerSetup, flag54) == 0x54);
static_assert(offsetof(GamePlayerSetup, race6c) == 0x6c);
static_assert(offsetof(GamePlayerSetup, value78) == 0x78);
static_assert(offsetof(GamePlayerSetup, valueA4) == 0xa4);
static_assert(offsetof(GamePlayerSetup, clan) == 0xb8);
static_assert(offsetof(GamePlayerSetup, type) == 0x5c);
static_assert(offsetof(GamePlayerSetup, race) == 0x64);
static_assert(offsetof(GamePlayerSetup, race_byte) == 0x68);
static_assert(offsetof(GamePlayerSetup, saved_values) == 0xa8);
static_assert(offsetof(GamePlayerSetup, colour) == 0xb4);
static_assert(offsetof(GamePlayerSetup, textBC) == 0xbc);

// Dirk::CTeam (RTTI name; 0x40 bytes): one team of the game setup, in a
// WCValSortedVector<Dirk::CTeam> of the game settings (members in
// game_teams.cpp and game_settings_vectors.cpp).
struct CTeam {
    std::int32_t id;               // +0x00 -1 from the default constructor; the sort key
    std::uint32_t unk04;           // +0x04 (not copied by the copy constructor or operator=)
    watcom::VectorBase player_ids; // +0x08 WCValSortedVector<int> (vtable 0x655a0c), probably the ids of
                                   //       the team's players
    watcom::VectorBase players;    // +0x24 WCValOrderedVector<Dirk::CPlayer> (vtable 0x656b6c; GamePlayerSetup
                                   //       elements)
};
static_assert(sizeof(CTeam) == 0x40);
static_assert(offsetof(CTeam, player_ids) == 0x08);
static_assert(offsetof(CTeam, players) == 0x24);

// The player-setup vector (a Watcom value vector of GamePlayerSetup, the
// layout of watcom/ptr_vector.h with 0xcc-byte elements). Its element
// access is inlined into GameSettings::PlayerSetup 0x4351e0 and SaveSetup
// 0x433490 (game_settings_players.cpp).
struct PlayerSetupVector {
    std::uint32_t vtable;    // +0x00 slot 1 is called with 0 when an empty vector gets its first element
    std::uint32_t unk04;
    GamePlayerSetup* data;   // +0x08 the elements (the game's operator new)
    std::uint32_t capacity;  // +0x0c elements allocated
    std::int32_t filled;     // +0x10 elements constructed (signed)
    std::uint32_t length;    // +0x14 elements in use
};
static_assert(sizeof(PlayerSetupVector) == 0x18);

struct GameSettings {
    std::uint8_t lock[0x18];     // +0x00 a Win32::CCriticalSection (a CRITICAL_SECTION): PlayerSetup holds it
    std::uint8_t unk018[0x28 - 0x18];
    std::uint8_t is_savegame;    // +0x28 1: a savegame is being loaded (LoadSaveGame); selects how
                                 //       SetupLocalPlayer 0x433e50 finds the local player
    std::uint8_t multiplayer;    // +0x29 NewGame arg 1 / LoadSaveGame arg 2 (getter IsMultiplayer 0x4318d0;
                                 //       F12 fast-forward only when 0)
    std::uint8_t option2a;       // +0x2a NewGame arg 2 (0 when a savegame is loaded)
    std::uint8_t by_clan;        // +0x2b NewGame arg 3: SetupLocalPlayer picks the local player's team by
                                 //       clan (FindTeamForClan 0x433a90); 0 when a savegame is loaded
    msvc6::String map_name;      // +0x2c probably the map's name: GameSettings::MapName 0x436a60 returns a copy,
                                 //       CLogicThread::SaveGame writes it into the savegame header
    watcom::VectorBase teams;    // +0x3c WCValSortedVector<Dirk::CTeam> (CTeam elements; length +0x50)
    PlayerSetupVector player_setups;  // +0x58 one record per player; its length (+0x6c) is the player count
                                      //       GameSettings::SaveSetup 0x433490 saves
    std::uint32_t player_setups_grow;  // +0x70 the player vector's growth step (watcom::VectorBase +0x18)
    std::uint32_t start_goods;   // +0x74 NewGame arg 9: 0 the map's own, 1 low, 2 medium, 3 high (re/game_setup.md;
                                 //       getter GameSettings::StartGoods 0x412300)
    std::uint32_t local_player_id;  // +0x78 logged by SaveSetup ("CFG: Saving local player id %i"), chunk 0x1a
    std::uint8_t is_host;        // +0x7c GameSettings::IsHost 0x4318a0; LoadSaveGame sets it to 1
    std::uint8_t unk07d[0x84 - 0x7d];
    fileformats::CMapFile* map_file;  // +0x84 the map or savegame being started (a random map: a memory file
                                      //       with chunk 5, re/game_setup.md)
    std::uint16_t seed;          // +0x88 the game's random seed (CGameData::SeedRandom; re/globals.csv)
    std::uint8_t unk08a[0x90 - 0x8a];
    watcom::VectorBase net_ids;  // +0x90 a vector of ints (watcom::PtrVector code; length +0xa4): the network
                                 //       id of each player slot (LoadSaveGame fills it from chunk 0x14 +4 and
                                 //       logs "NET: Mapping %i to %i")
    std::uint32_t local_net_id;  // +0xac the local player's network id: a savegame's chunk 0x1a +8 (the saved
                                 //       +0x78), -1 before, 0 for a savegame without chunk 0x1a ("CFG:
                                 //       Searching for NetId %i", SetupLocalPlayer)
    std::uint32_t game_guid[4];  // +0xb0 the game's GUID (NewGame; GameSettings::GameGuid 0x437400 copies it,
                                 //       CLogicThread::SaveGameToSlot turns it into text with UuidToStringA)
    std::uint32_t saved_tick;    // +0xc0 a savegame's tick (chunk 0x1a +0x1c, "CFG: Counter %i")
    std::uint16_t saved_seed;    // +0xc4 a savegame's RNG word (chunk 0x1a +0x20)
    std::uint8_t unk0c6[2];
    std::uint32_t valueC8;       // +0xc8 read by CNetThread::OnStart and CNetSession; chunk 0x1b
    std::uint8_t randomRaceAmazons;  // +0xcc whether random races include the Amazons (random.cpp)
    std::uint8_t goods_objective;    // +0xcd the "NGO" goods objective is on (re/game_setup.md); chunk 0x1e
    std::uint8_t unk0ce[2];
    std::uint32_t ngo_timeout;       // +0xd0 the new game objectives' time in minutes (GameSettings::NgoTimeout
                                     //       0x437470; CGameData::ChooseNewGameObjectives logs it)
};
static_assert(offsetof(GameSettings, is_savegame) == 0x28);
static_assert(offsetof(GameSettings, by_clan) == 0x2b);
static_assert(offsetof(GameSettings, map_name) == 0x2c);
static_assert(offsetof(GameSettings, teams) == 0x3c);
static_assert(offsetof(GameSettings, player_setups_grow) == 0x70);
static_assert(offsetof(GameSettings, is_host) == 0x7c);
static_assert(offsetof(GameSettings, net_ids) == 0x90);
static_assert(offsetof(GameSettings, local_net_id) == 0xac);
static_assert(offsetof(GameSettings, saved_tick) == 0xc0);
static_assert(offsetof(GameSettings, saved_seed) == 0xc4);
static_assert(offsetof(GameSettings, player_setups) == 0x58);
static_assert(offsetof(GameSettings, player_setups) + offsetof(PlayerSetupVector, length) == 0x6c);
static_assert(offsetof(GameSettings, start_goods) == 0x74);
static_assert(offsetof(GameSettings, local_player_id) == 0x78);
static_assert(offsetof(GameSettings, map_file) == 0x84);
static_assert(offsetof(GameSettings, seed) == 0x88);
static_assert(offsetof(GameSettings, game_guid) == 0xb0);
static_assert(offsetof(GameSettings, valueC8) == 0xc8);
static_assert(offsetof(GameSettings, randomRaceAmazons) == 0xcc);
static_assert(offsetof(GameSettings, goods_objective) == 0xcd);
static_assert(offsetof(GameSettings, ngo_timeout) == 0xd0);

#pragma pack(pop)

}  // namespace s3re::game
