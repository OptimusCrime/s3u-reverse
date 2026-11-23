#pragma once

// File format of command recordings (s3re_launcher --record-commands /
// --replay-commands), shared by the launcher and s3re.dll. See
// src/s3re/tooling/replay/command_log.h for what is recorded. tools/command_log.py
// reads and writes the same formats (convert, list). Readers tell the
// versions apart by the first bytes: "S3RECMD1/2/3" binary, "s3re-recording 4"
// text.
//
// Version 4 (text, written since 2026-11-22; file extension .s3rec;
// recordings are committed to git in recordings/). ASCII, LF line endings
// (CR before LF is accepted). Example:
//
//   s3re-recording 4
//   # The Settlers 3 command recording (s3re). Format: src/common/recording_format.h
//   seed 1234
//   settings 1
//   map Map\MULTI\384-2-Lakes.map
//   setup flags=1 size=768 symmetry=0 field0c=0 map-seed=1234 goods=3 race=255 computers=2
//   # step  message bytes (hex, memory order, 4 bytes per group)
//   12 21fb0c00 9d2b0000 00a8f612  # cmd fb
//
// - Line 1 is exactly "s3re-recording 4" (the magic and the version).
// - Everything from a '#' to the end of a line is a comment; blank lines are
//   ignored. (So the map path can't contain '#'; game paths never do.)
// - Header lines (each at most once, all before the first command):
//     seed N       required. The game seed (the value given to
//                  CGameData::SeedRandom when the game started, after --seed
//                  was applied; 4294967295 = unknown), decimal.
//     settings N   required. Bit 0: random races include the Amazons
//                  (g_gameSettings+0xCC); other bits 0. Decimal.
//     map PATH     required. The rest of the line (trimmed): the -map argument
//                  as given to S3.EXE (relative to the game directory); empty
//                  ("map" alone) if the game was started without -map.
//     setup K=V... optional. The game setup of a --random-map game (the
//                  GameSetup fields below, as in version 3; all 8 keys, each
//                  once, decimal: flags size symmetry field0c map-seed goods
//                  race computers). Written by every v4 recorder; a file
//                  without it (converted from version 2) is not checked
//                  against the running game's setup, as version 2.
//   Unknown keys are an error (a new key means a new version).
// - Command lines: "STEP HEX HEX ...": STEP (decimal) is the number of 0x20
//   step messages pushed before the command (the same key as the "slot" of
//   the binary versions); then the command message exactly as pushed on the
//   logic->net queue, as 1..64 groups of 8 hex digits, each group the 4 bytes
//   of one dword in memory order (so the first group starts with the message
//   type 21 and the command id). The bytes include the uninitialized bytes
//   9..11 of 12-byte commands: they are part of the record and are replayed
//   as recorded. Commands are listed in the order they were pushed.
//   Writers annotate each command line with "# cmd XX" (the command id).
// The DLL writes and flushes each command line as it happens, so a crash or
// a killed game keeps everything recorded so far.
//
// Binary versions (read only; tools/command_log.py convert turns them into
// version 4). All little endian.
// Version 3 ("S3RECMD3", 2026-11-22): version 2 plus the game setup of a game
// started with --random-map, so that a replay starts the same random map with
// the same players:
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
//   uint32 seed          game seed (as in version 4)
//   uint32 settings      as in version 4
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
#include <vector>

namespace s3re::recording {

constexpr char kMagicV1[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '1'};
constexpr char kMagicV2[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '2'};
constexpr char kMagicV3[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '3'};
constexpr char kTextMagic[] = "s3re-recording 4";
constexpr std::uint32_t kSettingAmazons = 1;
constexpr std::uint32_t kSetupRandomMap = 1;
constexpr std::uint32_t kRandomRace = 0xFF;
constexpr std::uint32_t kMaxCommandDwords = 64;

// How a game was set up beyond its map file (version 3 and 4).
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
// Keys of the v4 "setup" line, in GameSetup order.
constexpr const char* kSetupKeys[kSetupFields] = {"flags",    "size",  "symmetry", "field0c",
                                                  "map-seed", "goods", "race",     "computers"};

struct Header {
    int version = 0;  // 0: not a recording
    std::uint32_t seed = 0;
    std::uint32_t settings = 0;
    std::string map;
    bool hasSetup = false;  // version 3, and version 4 with a setup line
    GameSetup setup;
};

struct Command {
    std::uint32_t step = 0;  // 0x20 messages pushed before the command
    std::vector<std::uint32_t> dwords;
};

struct Recording {
    Header header;
    std::vector<Command> commands;
};

namespace detail {

inline bool ReadBinaryHeader(const std::string& data, std::size_t& offset, Header& header) {
    auto take = [&](void* out, std::size_t size) {
        if (data.size() - offset < size) {
            return false;
        }
        std::memcpy(out, data.data() + offset, size);
        offset += size;
        return true;
    };
    char magic[8] = {};
    if (!take(magic, sizeof(magic))) {
        return false;
    }
    int version = 0;
    if (std::memcmp(magic, kMagicV1, sizeof(magic)) == 0) {
        version = 1;
    } else if (std::memcmp(magic, kMagicV2, sizeof(magic)) == 0) {
        version = 2;
    } else if (std::memcmp(magic, kMagicV3, sizeof(magic)) == 0) {
        version = 3;
    } else {
        return false;
    }
    if (version >= 2) {
        std::uint32_t fields[2];
        std::uint16_t mapLength = 0;
        if (!take(fields, sizeof(fields)) || !take(&mapLength, sizeof(mapLength))) {
            return false;
        }
        header.map.resize(mapLength);
        if (mapLength > 0 && !take(&header.map[0], mapLength)) {
            return false;
        }
        header.seed = fields[0];
        header.settings = fields[1];
    }
    if (version == 3) {
        std::uint16_t setupLength = 0;
        if (!take(&setupLength, sizeof(setupLength)) || data.size() - offset < setupLength) {
            return false;
        }
        std::uint32_t setup[kSetupFields] = {0, 0, 0, 0, 0, 0, kRandomRace, 0};
        // Little endian on the only platform this runs on (x86).
        std::memcpy(setup, data.data() + offset,
                    setupLength < sizeof(setup) ? setupLength : sizeof(setup));
        offset += setupLength;
        std::memcpy(&header.setup, setup, sizeof(setup));
        header.hasSetup = true;
    }
    header.version = version;
    return true;
}

inline bool ReadBinaryCommands(const std::string& data, std::size_t offset,
                               std::vector<Command>& commands) {
    while (offset < data.size()) {
        std::uint32_t record[2];
        if (data.size() - offset < sizeof(record)) {
            return false;
        }
        std::memcpy(record, data.data() + offset, sizeof(record));
        offset += sizeof(record);
        if (record[1] == 0 || record[1] > kMaxCommandDwords ||
            data.size() - offset < record[1] * 4u) {
            return false;
        }
        Command command{record[0], std::vector<std::uint32_t>(record[1])};
        std::memcpy(command.dwords.data(), data.data() + offset, record[1] * 4u);
        offset += record[1] * 4u;
        commands.push_back(std::move(command));
    }
    return true;
}

// A decimal uint32 (digits only, no sign, no leading spaces).
inline bool ParseDecimal(const std::string& text, std::uint32_t& value) {
    if (text.empty() || text.size() > 10) {
        return false;
    }
    std::uint64_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    if (result > 0xFFFFFFFFull) {
        return false;
    }
    value = static_cast<std::uint32_t>(result);
    return true;
}

inline int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 8 hex digits = the 4 bytes of one dword in memory order.
inline bool ParseHexDword(const std::string& text, std::uint32_t& value) {
    if (text.size() != 8) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
        const int high = HexDigit(text[2 * i]);
        const int low = HexDigit(text[2 * i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        value |= static_cast<std::uint32_t>(high * 16 + low) << (8 * i);
    }
    return true;
}

inline std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t') ++i;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    return words;
}

inline std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

// Parses a version 4 text recording. `error` gets "line N: reason".
inline bool ParseText(const std::string& data, Recording& out, std::string* error) {
    Header& header = out.header;
    bool haveSeed = false, haveSettings = false, haveMap = false;
    std::size_t position = 0;
    int lineNumber = 0;
    auto fail = [&](const std::string& reason) {
        if (error != nullptr) {
            *error = "line " + std::to_string(lineNumber) + ": " + reason;
        }
        return false;
    };
    while (position < data.size()) {
        std::size_t end = data.find('\n', position);
        if (end == std::string::npos) {
            end = data.size();
        }
        std::string line = data.substr(position, end - position);
        position = end + 1;
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (lineNumber == 1) {
            if (line != kTextMagic) {
                return fail("not \"s3re-recording 4\"");
            }
            continue;
        }
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line[0] >= '0' && line[0] <= '9') {
            const std::vector<std::string> words = SplitWords(line);
            Command command;
            if (!ParseDecimal(words[0], command.step)) {
                return fail("bad step '" + words[0] + "'");
            }
            if (words.size() < 2 || words.size() - 1 > kMaxCommandDwords) {
                return fail("a command needs 1.." + std::to_string(kMaxCommandDwords) +
                            " groups of 8 hex digits");
            }
            for (std::size_t i = 1; i < words.size(); ++i) {
                std::uint32_t dword = 0;
                if (!ParseHexDword(words[i], dword)) {
                    return fail("bad hex group '" + words[i] + "' (8 hex digits expected)");
                }
                command.dwords.push_back(dword);
            }
            out.commands.push_back(std::move(command));
            continue;
        }
        if (!out.commands.empty()) {
            return fail("header line after the first command");
        }
        std::size_t keyEnd = 0;
        while (keyEnd < line.size() && line[keyEnd] != ' ' && line[keyEnd] != '\t') ++keyEnd;
        const std::string key = line.substr(0, keyEnd);
        const std::string value = Trim(line.substr(keyEnd));
        if (key == "seed" || key == "settings") {
            bool& have = key == "seed" ? haveSeed : haveSettings;
            if (have) {
                return fail("'" + key + "' given twice");
            }
            if (!ParseDecimal(value, key == "seed" ? header.seed : header.settings)) {
                return fail("bad " + key + " '" + value + "'");
            }
            have = true;
        } else if (key == "map") {
            if (haveMap) {
                return fail("'map' given twice");
            }
            header.map = value;
            haveMap = true;
        } else if (key == "setup") {
            if (header.hasSetup) {
                return fail("'setup' given twice");
            }
            std::uint32_t fields[kSetupFields] = {};
            bool seen[kSetupFields] = {};
            for (const std::string& word : SplitWords(value)) {
                const std::size_t equals = word.find('=');
                std::size_t index = kSetupFields;
                for (std::size_t i = 0; i < kSetupFields; ++i) {
                    if (equals != std::string::npos && word.compare(0, equals, kSetupKeys[i]) == 0 &&
                        std::strlen(kSetupKeys[i]) == equals) {
                        index = i;
                    }
                }
                if (index == kSetupFields || seen[index] ||
                    !ParseDecimal(word.substr(equals + 1), fields[index])) {
                    return fail("bad or repeated setup field '" + word + "'");
                }
                seen[index] = true;
            }
            for (std::size_t i = 0; i < kSetupFields; ++i) {
                if (!seen[i]) {
                    return fail(std::string("setup lacks '") + kSetupKeys[i] + "'");
                }
            }
            std::memcpy(&header.setup, fields, sizeof(fields));
            header.hasSetup = true;
        } else {
            return fail("unknown line '" + key + "'");
        }
    }
    if (lineNumber == 0) {
        return fail("empty file");
    }
    if (!haveSeed || !haveSettings || !haveMap) {
        return fail("the header needs 'seed', 'settings' and 'map'");
    }
    header.version = 4;
    return true;
}

}  // namespace detail

// Parses a whole recording (any version) from its bytes. Returns false (and
// a reason in `error`) if it isn't a valid recording.
inline bool ParseRecording(const std::string& data, Recording& out, std::string* error) {
    out = Recording{};
    if (data.compare(0, sizeof(kTextMagic) - 1, kTextMagic) == 0) {
        if (!detail::ParseText(data, out, error)) {
            out = Recording{};
            return false;
        }
        return true;
    }
    std::size_t offset = 0;
    if (!detail::ReadBinaryHeader(data, offset, out.header)) {
        out = Recording{};
        if (error != nullptr) *error = "not a command recording";
        return false;
    }
    if (!detail::ReadBinaryCommands(data, offset, out.commands)) {
        out = Recording{};
        if (error != nullptr) *error = "truncated or invalid command record";
        return false;
    }
    return true;
}

// Reads and parses a whole recording file (opened in binary mode).
inline bool ReadRecording(std::FILE* file, Recording& out, std::string* error) {
    std::string data;
    char buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        data.append(buffer, got);
    }
    return ParseRecording(data, out, error);
}

// The version 4 header text (ends with a newline). header.setup is always
// written (the recorder knows the running game's setup).
inline std::string FormatHeader(const Header& header) {
    std::string text = std::string(kTextMagic) + "\n";
    text += "# The Settlers 3 command recording (s3re). Format: src/common/recording_format.h\n";
    text += "seed " + std::to_string(header.seed) + "\n";
    text += "settings " + std::to_string(header.settings) + "\n";
    text += header.map.empty() ? std::string("map\n") : "map " + header.map + "\n";
    if (header.hasSetup) {
        std::uint32_t fields[kSetupFields];
        std::memcpy(fields, &header.setup, sizeof(fields));
        text += "setup";
        for (std::uint32_t i = 0; i < kSetupFields; ++i) {
            text += std::string(" ") + kSetupKeys[i] + "=" + std::to_string(fields[i]);
        }
        text += "\n";
    }
    text += "# step  message bytes (hex, memory order, 4 bytes per group)\n";
    return text;
}

// One version 4 command line (ends with a newline).
inline std::string FormatCommand(std::uint32_t step, const std::uint32_t* dwords,
                                 std::size_t count) {
    static const char kHex[] = "0123456789abcdef";
    std::string text = std::to_string(step);
    for (std::size_t i = 0; i < count; ++i) {
        text += ' ';
        for (int b = 0; b < 4; ++b) {
            const unsigned byte = (dwords[i] >> (8 * b)) & 0xff;
            text += kHex[byte >> 4];
            text += kHex[byte & 15];
        }
    }
    const unsigned id = (dwords[0] >> 8) & 0xff;
    text += "  # cmd ";
    text += kHex[id >> 4];
    text += kHex[id & 15];
    text += '\n';
    return text;
}

}  // namespace s3re::recording
