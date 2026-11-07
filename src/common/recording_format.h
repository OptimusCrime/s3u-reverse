#pragma once

// File format of command recordings (s3re_launcher --record-commands /
// --replay-commands), shared by the launcher and s3re.dll. See
// src/s3re/replay/command_log.h for what is recorded.
//
// All little endian.
//
// Version 2 ("S3RECMD2"): the recording says which game it belongs to, so
// that a replay can't silently run in a different game:
//   char   magic[8]      "S3RECMD2"
//   uint32 seed          game seed (the value given to GameWorld::SeedRandom
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
constexpr std::uint32_t kSettingAmazons = 1;

struct Header {
    int version = 0;  // 0: not a recording
    std::uint32_t seed = 0;
    std::uint32_t settings = 0;
    std::string map;
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
    if (std::memcmp(magic, kMagicV2, sizeof(magic)) != 0) {
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
    header.seed = fields[0];
    header.settings = fields[1];
    header.version = 2;
    return header;
}

// Writes a version 2 header.
inline bool WriteHeader(std::FILE* file, const Header& header) {
    const std::uint32_t fields[2] = {header.seed, header.settings};
    const auto mapLength = static_cast<std::uint16_t>(header.map.size());
    return std::fwrite(kMagicV2, 1, sizeof(kMagicV2), file) == sizeof(kMagicV2) &&
           std::fwrite(fields, sizeof(fields[0]), 2, file) == 2 &&
           std::fwrite(&mapLength, sizeof(mapLength), 1, file) == 1 &&
           (mapLength == 0 || std::fwrite(header.map.data(), 1, mapLength, file) == mapLength);
}

}  // namespace s3re::recording
