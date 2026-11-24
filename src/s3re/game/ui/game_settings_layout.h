#pragma once

// Layout of the game settings object (g_gameSettings, the global object at
// 0x7a7a18; no RTTI, so the name is ours; Dirk\GameParams.cpp and
// neighbours). It describes the game being started or loaded: the map, the
// seed, the players. GameSettings::NewGame 0x431ab0 and
// GameSettings::LoadSaveGame 0x432b00 fill it; CLogicThread::OnStart and the
// savegame code read it. Only the fields our code uses are named.

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

struct GameSettings {
    std::uint8_t unk000[0x74];
    std::uint32_t start_goods;   // +0x74 NewGame arg 9: 0 the map's own, 1 low, 2 medium, 3 high (re/game_setup.md;
                                 //       getter GameSettings::StartGoods 0x412300)
    std::uint8_t unk078[0x88 - 0x78];
    std::uint16_t seed;          // +0x88 the game's random seed (CGameData::SeedRandom; re/globals.csv)
    std::uint8_t unk08a[0xb0 - 0x8a];
    std::uint32_t valuesB0[4];   // +0xb0 four values copied as one (GameParams_CopyRectB0 0x437400;
                                 //       read by CNetSession and the savegame code; probably a rectangle)
    std::uint8_t unk0c0[0xcc - 0xc0];
    std::uint8_t randomRaceAmazons;  // +0xcc whether random races include the Amazons (random.cpp)
};
static_assert(offsetof(GameSettings, start_goods) == 0x74);
static_assert(offsetof(GameSettings, seed) == 0x88);
static_assert(offsetof(GameSettings, valuesB0) == 0xb0);
static_assert(offsetof(GameSettings, randomRaceAmazons) == 0xcc);

#pragma pack(pop)

}  // namespace s3re::game
