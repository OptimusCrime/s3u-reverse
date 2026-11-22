#include "replay/command_log.h"

#include "common/recording_format.h"
#include "game/game_settings_layout.h"
#include "game/globals.h"
#include "game/logic_thread_layout.h"
#include "game/random.h"
#include "hooks.h"
#include "log.h"
#include "tooling/random_map_start.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace s3re::replay {

// The logic <-> network queue: ring buffer of uint32 (Win32 event for
// wake-ups), opaque.
using game::MessageQueue;

namespace {

// MessageQueue::Push (0x004309c0): copies `count` dwords into the ring
// buffer and, if `signal` (a bool passed as a full stack slot), sets the
// queue's event. thiscall, ret 0xc.
using PushFn = void(__fastcall*)(MessageQueue* self, void* edx, const std::uint32_t* dwords,
                                 int count, int signal);

void __fastcall Hooked_MessageQueue_Push(MessageQueue* self, void* edx,
                                         const std::uint32_t* dwords, int count, int signal);

S3RE_PASSTHROUGH(0x004309c0, MessageQueue_Push, PushFn, Original_MessageQueue_Push,
                 Hooked_MessageQueue_Push);

constexpr std::uint8_t kStepMessage = 0x20;
constexpr std::uint8_t kCommandMessage = 0x21;

struct Command {
    std::uint32_t slot;
    std::vector<std::uint32_t> dwords;
};

enum class Mode { Off, Record, Replay };

std::atomic<void*> g_logicThread{nullptr};
std::mutex g_mutex;
Mode g_mode = Mode::Off;
bool g_initialized = false;
FILE* g_recordFile = nullptr;
std::vector<Command> g_recorded;  // replay: the whole file
std::size_t g_nextInject = 0;     // replay: next recorded command to push
std::size_t g_nextCompare = 0;    // replay: next recorded command to compare
std::uint32_t g_slot = 0;         // 0x20 messages pushed so far
unsigned long g_commands = 0;     // live 0x21 messages seen
unsigned long g_mismatches = 0;
unsigned long g_paddingDifferences = 0;  // see EqualIgnoringPadding
unsigned long g_matched = 0;
bool g_logCommands = false;
std::uint32_t g_lastStepCountdown = 0;  // record: g_stepCountdown at the last step message
unsigned long g_fastForwards = 0;       // record: F12 fast-forwards seen

FILE* OpenFile(const char* path, const char* mode) {
    FILE* file = nullptr;
    return fopen_s(&file, path, mode) == 0 ? file : nullptr;
}

std::string Env(const char* name) {
    char value[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return (length > 0 && length < sizeof(value)) ? std::string(value) : std::string();
}

// The -map argument of the game's command line ("" if there is none).
std::string MapArgument() {
    const char* p = GetCommandLineA();
    std::string previous;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        std::string token;
        bool quoted = false;
        for (; *p != '\0' && (quoted || (*p != ' ' && *p != '\t')); ++p) {
            if (*p == '"') {
                quoted = !quoted;
            } else {
                token += *p;
            }
        }
        if (_stricmp(previous.c_str(), "-map") == 0) {
            return token;
        }
        previous = token;
    }
    return std::string();
}

// The settings of the running game that a recording must match.
recording::Header CurrentGame() {
    recording::Header header;
    header.version = 3;
    const long seed = game::LastSeed();
    header.seed = seed < 0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(seed);
    header.settings =
        game::g_gameSettings->randomRaceAmazons != 0 ? recording::kSettingAmazons
                                                                       : 0;
    header.map = MapArgument();
    header.setup = tooling::AppliedGameSetup();
    return header;
}

// A version 2 recording must belong to the running game: same seed, race
// setting and map. Logs every difference.
bool MatchesCurrentGame(const recording::Header& recorded) {
    const recording::Header current = CurrentGame();
    bool ok = true;
    if (recorded.seed != current.seed) {
        S3RE_LOG("replay: recorded with seed %u, this game has seed %u", recorded.seed,
                 current.seed);
        ok = false;
    }
    if (recorded.settings != current.settings) {
        S3RE_LOG("replay: recorded with settings %#x, this game has %#x", recorded.settings,
                 current.settings);
        ok = false;
    }
    if (_stricmp(recorded.map.c_str(), current.map.c_str()) != 0) {
        S3RE_LOG("replay: recorded on map '%s', this game runs '%s'", recorded.map.c_str(),
                 current.map.c_str());
        ok = false;
    }
    if (recorded.version >= 3 && recorded.setup != current.setup) {
        const recording::GameSetup& a = recorded.setup;
        const recording::GameSetup& b = current.setup;
        S3RE_LOG("replay: recorded with game setup flags %u size %u symmetry %u field0c %u "
                 "map seed %u goods %u race %u computers %u; this game has flags %u size %u "
                 "symmetry %u field0c %u map seed %u goods %u race %u computers %u",
                 a.flags, a.size, a.symmetry, a.field0c, a.mapSeed, a.goods, a.race,
                 a.computers, b.flags, b.size, b.symmetry, b.field0c, b.mapSeed, b.goods,
                 b.race, b.computers);
        ok = false;
    }
    return ok;
}

void LogSetup(const char* what, const recording::GameSetup& setup) {
    if (setup.IsRandomMap()) {
        S3RE_LOG("%s: random map size %u, symmetry %u, field0c %u, map seed %u, start goods %u, "
                 "human race %u, computer players %u",
                 what, setup.size, setup.symmetry, setup.field0c, setup.mapSeed, setup.goods,
                 setup.race, setup.computers);
    }
}

bool LoadRecording(const std::string& path) {
    FILE* file = OpenFile(path.c_str(), "rb");
    if (file == nullptr) {
        S3RE_LOG("replay: cannot open %s", path.c_str());
        return false;
    }
    const recording::Header header = recording::ReadHeader(file);
    bool ok = header.version != 0;
    if (header.version >= 2) {
        S3RE_LOG("replay: %s (version %d) was recorded with seed %u, settings %#x, map '%s'",
                 path.c_str(), header.version, header.seed, header.settings,
                 header.map.c_str());
        LogSetup("replay", header.setup);
        if (!MatchesCurrentGame(header)) {
            std::fclose(file);
            return false;
        }
    } else if (header.version == 1) {
        S3RE_LOG("replay: %s is a version 1 recording (no seed or map); not checked",
                 path.c_str());
    }
    while (ok) {
        std::uint32_t record[2];
        if (std::fread(record, sizeof(record[0]), 2, file) != 2) {
            break;  // end of file
        }
        if (record[1] == 0 || record[1] > 64) {
            ok = false;
            break;
        }
        Command command{record[0], std::vector<std::uint32_t>(record[1])};
        ok = std::fread(command.dwords.data(), sizeof(std::uint32_t), record[1], file) ==
             record[1];
        g_recorded.push_back(std::move(command));
    }
    std::fclose(file);
    if (!ok) {
        S3RE_LOG("replay: %s is not a valid command recording", path.c_str());
        g_recorded.clear();
        return false;
    }
    S3RE_LOG("replay: loaded %zu commands from %s", g_recorded.size(), path.c_str());
    return true;
}

void Initialize() {
    g_initialized = true;
    g_logCommands = !Env("S3RE_LOG_COMMANDS").empty();
    const std::string recordPath = Env("S3RE_RECORD_COMMANDS");
    const std::string replayPath = Env("S3RE_REPLAY_COMMANDS");
    if (!recordPath.empty()) {
        g_recordFile = OpenFile(recordPath.c_str(), "wb");
        if (g_recordFile == nullptr) {
            S3RE_LOG("record: cannot create %s", recordPath.c_str());
            return;
        }
        const recording::Header header = CurrentGame();
        recording::WriteHeader(g_recordFile, header);
        std::fflush(g_recordFile);
        g_mode = Mode::Record;
        S3RE_LOG("record: writing commands to %s (seed %u, settings %#x, map '%s')",
                 recordPath.c_str(), header.seed, header.settings, header.map.c_str());
        LogSetup("record", header.setup);
    } else if (!replayPath.empty()) {
        if (LoadRecording(replayPath)) {
            g_mode = Mode::Replay;
        } else {
            // Running on without the recording, or with a recording of
            // another game, would silently play a different game; stop
            // instead.
            S3RE_LOG("replay: aborting");
            TerminateProcess(GetCurrentProcess(), 3);
        }
    }
}

std::string Hex(const std::uint32_t* dwords, std::size_t count) {
    std::string text;
    char buffer[12];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(buffer, sizeof(buffer), i == 0 ? "%08x" : " %08x", dwords[i]);
        text += buffer;
    }
    return text;
}

// Bytes 9..11 of 12-byte command messages are often uninitialized stack
// memory in the original (we have seen bytes of logic-thread stack addresses
// there, differing between runs). They aren't compared byte for byte;
// differences there are counted separately. The simulation only ever sees the
// recorded bytes during a replay, so this affects the diagnostics only.
bool EqualIgnoringPadding(const std::uint32_t* a, const std::uint32_t* b, std::size_t count) {
    if (count != 3) {
        return false;
    }
    return a[0] == b[0] && a[1] == b[1] && (a[2] & 0xff) == (b[2] & 0xff);
}

void LogMismatch(const char* what, const std::uint32_t* live, std::size_t liveCount,
                 const Command* recorded) {
    if (++g_mismatches > 20) {
        return;
    }
    const std::string liveText = live ? "[" + Hex(live, liveCount) + "]" : "(none)";
    const std::string recordedText =
        recorded ? "#" + std::to_string(recorded - g_recorded.data()) + " slot " +
                       std::to_string(recorded->slot) + " [" +
                       Hex(recorded->dwords.data(), recorded->dwords.size()) + "]"
                 : "(none)";
    S3RE_LOG("replay: %s at slot %u: live %s, recorded %s", what, g_slot, liveText.c_str(),
             recordedText.c_str());
}

// Replay: compare a live command with the next recorded one. Live commands
// must come in the same order and in the same slot as recorded.
void CompareLive(const std::uint32_t* dwords, std::size_t count) {
    // Recorded commands of past slots that no live command matched.
    while (g_nextCompare < g_recorded.size() && g_recorded[g_nextCompare].slot < g_slot) {
        LogMismatch("recorded command was not issued live", nullptr, 0,
                    &g_recorded[g_nextCompare]);
        ++g_nextCompare;
    }
    const Command* expected =
        g_nextCompare < g_recorded.size() ? &g_recorded[g_nextCompare] : nullptr;
    const bool sameSlot = expected != nullptr && expected->slot == g_slot &&
                          expected->dwords.size() == count;
    if (sameSlot && std::memcmp(expected->dwords.data(), dwords, count * 4) == 0) {
        ++g_nextCompare;
        ++g_matched;
    } else if (sameSlot && EqualIgnoringPadding(expected->dwords.data(), dwords, count)) {
        ++g_nextCompare;
        ++g_matched;
        ++g_paddingDifferences;
    } else if (sameSlot) {
        LogMismatch("live command differs from recording", dwords, count, expected);
        ++g_nextCompare;
    } else {
        LogMismatch("live command not in recording", dwords, count, expected);
    }
}

// The game's own fast-forward (F12 in single player, ProcessEvents
// 0x525cd2: g_stepCountdown += 882) is a UI event, not a command, so a
// recording doesn't contain it and a replay won't repeat it; it also skips
// work in the simulation (fog of war, minimap), so the replay may diverge
// from there. Warn in the log when it happens while recording.
void WarnAboutFastForward() {
    const std::uint32_t countdown = game::g_stepCountdown;
    if (countdown > g_lastStepCountdown) {
        ++g_fastForwards;
        S3RE_LOG("record: WARNING: the game's fast-forward (F12) was used at slot %u (step "
                 "countdown %u -> %u); a replay doesn't repeat it and may diverge from here",
                 g_slot, g_lastStepCountdown, countdown);
    }
    g_lastStepCountdown = countdown;
}

// Rewrites one push on the logic->net queue. Returns the dwords to forward.
std::vector<std::uint32_t> Process(const std::uint32_t* dwords, int count) {
    std::vector<std::uint32_t> out;
    out.reserve(count);
    int index = 0;
    while (index < count) {
        const std::uint8_t type = static_cast<std::uint8_t>(dwords[index] & 0xff);
        const std::uint8_t sizeBytes = static_cast<std::uint8_t>((dwords[index] >> 16) & 0xff);
        int length = sizeBytes / 4;
        if (length <= 0 || index + length > count) {
            // Not a message we understand; pass the rest through unchanged.
            S3RE_LOG("command log: unexpected message %08x (count %d); passed through",
                     dwords[index], count - index);
            out.insert(out.end(), dwords + index, dwords + count);
            break;
        }
        const std::uint32_t* message = dwords + index;
        index += length;

        if (type == kCommandMessage) {
            ++g_commands;
            if (g_logCommands) {
                S3RE_LOG("command slot=%u [%s]", g_slot, Hex(message, length).c_str());
            }
            if (g_mode == Mode::Record) {
                const std::uint32_t header[2] = {g_slot, static_cast<std::uint32_t>(length)};
                std::fwrite(header, sizeof(header[0]), 2, g_recordFile);
                std::fwrite(message, sizeof(std::uint32_t), length, g_recordFile);
                std::fflush(g_recordFile);
            }
            if (g_mode == Mode::Replay) {
                CompareLive(message, length);
                continue;  // dropped; the recorded copy is injected instead
            }
        } else if (type == kStepMessage) {
            if (g_mode == Mode::Record) {
                WarnAboutFastForward();
            }
            if (g_mode == Mode::Replay) {
                while (g_nextInject < g_recorded.size() &&
                       g_recorded[g_nextInject].slot <= g_slot) {
                    const Command& command = g_recorded[g_nextInject++];
                    out.insert(out.end(), command.dwords.begin(), command.dwords.end());
                }
            }
            ++g_slot;
        }
        out.insert(out.end(), message, message + length);
    }
    return out;
}

void __fastcall Hooked_MessageQueue_Push(MessageQueue* self, void* edx,
                                         const std::uint32_t* dwords, int count, int signal) {
    void* const logic = g_logicThread.load(std::memory_order_acquire);
    if (logic == nullptr || count <= 0 || self != static_cast<const game::CLogicThread*>(logic)->toNet) {
        Original_MessageQueue_Push(self, edx, dwords, count, signal);
        return;
    }
    std::vector<std::uint32_t> out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_initialized) {
            Initialize();
        }
        if (g_mode == Mode::Off && !g_logCommands) {
            out.assign(dwords, dwords + count);
        } else {
            out = Process(dwords, count);
        }
    }
    if (!out.empty()) {
        Original_MessageQueue_Push(self, edx, out.data(), static_cast<int>(out.size()), signal);
    }
}

}  // namespace

void SetLogicThread(void* logicThread) {
    void* expected = nullptr;
    if (g_logicThread.compare_exchange_strong(expected, logicThread)) {
        S3RE_LOG("command log: logic thread %p, logic->net queue %p", logicThread,
                 static_cast<void*>(static_cast<const game::CLogicThread*>(logicThread)->toNet));
    } else if (expected != logicThread) {
        S3RE_LOG("command log: a second logic thread %p appeared (first %p); ignored",
                 logicThread, expected);
    }
}

void CloseCommandLog() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_mode == Mode::Record && g_recordFile != nullptr) {
        std::fclose(g_recordFile);
        g_recordFile = nullptr;
        S3RE_LOG("record: %lu commands in %u step slots, %lu fast-forwards (F12)", g_commands,
                 g_slot, g_fastForwards);
    } else if (g_mode == Mode::Replay) {
        S3RE_LOG("replay: injected %zu of %zu recorded commands over %u step slots; "
                 "%lu live commands, %lu matched the recording (%lu only in padding bytes), "
                 "%lu mismatches",
                 g_nextInject, g_recorded.size(), g_slot, g_commands, g_matched,
                 g_paddingDifferences, g_mismatches);
    }
    g_mode = Mode::Off;
}

}  // namespace s3re::replay
