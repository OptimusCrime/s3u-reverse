#pragma once

// File format of command recordings (s3re_launcher --record-commands /
// --replay-commands), shared by the launcher and s3re.dll. See
// src/s3re/replay/command_log.h for what is recorded.
//
// All little endian.
//
// Version 3 ("S3RECMD3", written since 2026-11-22): version 2 plus the game
// setup of a game started with --random-map, so that a replay starts the
// same random map with the same players:
//   char   magic[8]      "S3RECMD3"
//   uint32 seed, uint32 settings, uint16 mapLength, map   as in version 2
//   uint16 setupLength   then setupLength bytes of game setup (readers take
//                        the fields they know and skip the rest; 0 = none):
//     uint32 flags       bit 0: random map (the fields below are valid)
//     uint32 size        map size (chunk 5 of the random map file)
//     uint32 symmetry    symmetry bits
//     uint32 field0c     the map dialog's second value (chunk 5 +0x0c)
//     uint32 mapSeed     the random map's seed (chunk 5 +0x14)
//     uint32 goods       start goods (GameSettings+0x74): 0 = the map's
//                        own, 1 low, 2 medium, 3 high
//     uint32 race        race of the human player: 0 Roman, 1 Egyptian,
//                        2 Asian, 3 Amazon, 0xFF random
//     uint32 computers   computer opponents, each in a party of its own;
//                        0 = the player setup of the -map file
//   then the commands, as in version 1.
// Version 2 ("S3RECMD2"): the recording says which game it belongs to, so
// that a replay can't silently run in a different game:
//   char   magic[8]      "S3RECMD2"
//   uint32 seed          game seed (the value given to CGameData::SeedRandom
//                        when the game started, after --seed was applied)
//   uint32 settings      bit 0: random races include the Amazons
//                        (g_gameSettings+0xCC); other bits 0
//   uint16 mapLength     then mapLength bytes: the -map argument as given to
//                        S3.EXE (ANSI, relative to the game directory);
//                        0 if the game was started without -map
//   then the commands, as in version 1.
// Version 1 ("S3RECMD1"): magic, then the commands only. Replaying one needs
// the right --seed and map on the command line.
// Each command: uint32 slot, uint32 dwordCount (1..64), uint32 dwords[].

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace s3re::recording {

constexpr char kMagicV1[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '1'};
constexpr char kMagicV2[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '2'};
constexpr char kMagicV3[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '3'};
constexpr std::uint32_t kSettingAmazons = 1;
constexpr std::uint32_t kSetupRandomMap = 1;
constexpr std::uint32_t kRandomRace = 0xFF;

// How a game was set up beyond its map file (version 3).
struct GameSetup {
    std::uint32_t flags = 0;  // kSetupRandomMap
    std::uint32_t size = 0;
    std::uint32_t symmetry = 0;
    std::uint32_t field0c = 0;
    std::uint32_t mapSeed = 0;
    std::uint32_t goods = 0;
    std::uint32_t race = kRandomRace;
    std::uint32_t computers = 0;

    bool IsRandomMap() const { return (flags & kSetupRandomMap) != 0; }
    bool operator==(const GameSetup& other) const {
        return flags == other.flags && size == other.size && symmetry == other.symmetry &&
               field0c == other.field0c && mapSeed == other.mapSeed && goods == other.goods &&
               race == other.race && computers == other.computers;
    }
    bool operator!=(const GameSetup& other) const { return !(*this == other); }
};
constexpr std::uint32_t kSetupFields = 8;
static_assert(sizeof(GameSetup) == kSetupFields * 4);

struct Header {
    int version = 0;  // 0: not a recording
    std::uint32_t seed = 0;
    std::uint32_t settings = 0;
    std::string map;
    GameSetup setup;  // version 3 only
};

// Reads the header; leaves `file` at the first command. version 0 on error.
inline Header ReadHeader(std::FILE* file) {
    Header header;
    char magic[8] = {};
    if (std::fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return header;
    }
    if (std::memcmp(magic, kMagicV1, sizeof(magic)) == 0) {
        header.version = 1;
        return header;
    }
    int version = 0;
    if (std::memcmp(magic, kMagicV2, sizeof(magic)) == 0) {
        version = 2;
    } else if (std::memcmp(magic, kMagicV3, sizeof(magic)) == 0) {
        version = 3;
    } else {
        return header;
    }
    std::uint32_t fields[2];
    std::uint16_t mapLength = 0;
    if (std::fread(fields, sizeof(fields[0]), 2, file) != 2 ||
        std::fread(&mapLength, sizeof(mapLength), 1, file) != 1) {
        return header;
    }
    header.map.resize(mapLength);
    if (mapLength > 0 && std::fread(&header.map[0], 1, mapLength, file) != mapLength) {
        return header;
    }
    if (version == 3) {
        std::uint16_t setupLength = 0;
        if (std::fread(&setupLength, sizeof(setupLength), 1, file) != 1) {
            return header;
        }
        std::uint32_t setup[kSetupFields] = {0, 0, 0, 0, 0, 0, kRandomRace, 0};
        unsigned char byte = 0;
        for (std::uint16_t i = 0; i < setupLength; ++i) {
            if (std::fread(&byte, 1, 1, file) != 1) {
                return header;
            }
            if (i < sizeof(setup)) {
                // Little endian on the only platform this runs on (x86).
                reinterpret_cast<unsigned char*>(setup)[i] = byte;
            }
        }
        std::memcpy(&header.setup, setup, sizeof(setup));
    }
    header.seed = fields[0];
    header.settings = fields[1];
    header.version = version;
    return header;
}

// Writes a version 3 header.
inline bool WriteHeader(std::FILE* file, const Header& header) {
    const std::uint32_t fields[2] = {header.seed, header.settings};
    const auto mapLength = static_cast<std::uint16_t>(header.map.size());
    const auto setupLength = static_cast<std::uint16_t>(sizeof(GameSetup));
    return std::fwrite(kMagicV3, 1, sizeof(kMagicV3), file) == sizeof(kMagicV3) &&
           std::fwrite(fields, sizeof(fields[0]), 2, file) == 2 &&
           std::fwrite(&mapLength, sizeof(mapLength), 1, file) == 1 &&
           (mapLength == 0 || std::fwrite(header.map.data(), 1, mapLength, file) == mapLength) &&
           std::fwrite(&setupLength, sizeof(setupLength), 1, file) == 1 &&
           std::fwrite(&header.setup, sizeof(GameSetup), 1, file) == 1;
}

}  // namespace s3re::recording
