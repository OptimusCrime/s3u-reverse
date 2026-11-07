#include "replay/command_log.h"

#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace s3re::replay {

struct MessageQueue;  // opaque: ring buffer of uint32 (Win32 event for wake-ups)

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

constexpr std::size_t kLogicToNetQueueOffset = 0x3c4;  // CLogicThread -> MessageQueue*
constexpr std::uint8_t kStepMessage = 0x20;
constexpr std::uint8_t kCommandMessage = 0x21;
constexpr char kMagic[8] = {'S', '3', 'R', 'E', 'C', 'M', 'D', '1'};

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

FILE* OpenFile(const char* path, const char* mode) {
    FILE* file = nullptr;
    return fopen_s(&file, path, mode) == 0 ? file : nullptr;
}

std::string Env(const char* name) {
    char value[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return (length > 0 && length < sizeof(value)) ? std::string(value) : std::string();
}

bool LoadRecording(const std::string& path) {
    FILE* file = OpenFile(path.c_str(), "rb");
    if (file == nullptr) {
        S3RE_LOG("replay: cannot open %s", path.c_str());
        return false;
    }
    char magic[sizeof(kMagic)] = {};
    bool ok = std::fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
              std::memcmp(magic, kMagic, sizeof(kMagic)) == 0;
    while (ok) {
        std::uint32_t header[2];
        if (std::fread(header, sizeof(header[0]), 2, file) != 2) {
            break;  // end of file
        }
        if (header[1] == 0 || header[1] > 64) {
            ok = false;
            break;
        }
        Command command{header[0], std::vector<std::uint32_t>(header[1])};
        ok = std::fread(command.dwords.data(), sizeof(std::uint32_t), header[1], file) ==
             header[1];
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
        std::fwrite(kMagic, 1, sizeof(kMagic), g_recordFile);
        std::fflush(g_recordFile);
        g_mode = Mode::Record;
        S3RE_LOG("record: writing commands to %s", recordPath.c_str());
    } else if (!replayPath.empty()) {
        if (LoadRecording(replayPath)) {
            g_mode = Mode::Replay;
        } else {
            // Running on without the recording would silently play a
            // different game; stop instead.
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
    if (logic == nullptr || count <= 0 ||
        self != *reinterpret_cast<MessageQueue* const*>(static_cast<std::uint8_t*>(logic) +
                                                        kLogicToNetQueueOffset)) {
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
                 *reinterpret_cast<void**>(static_cast<std::uint8_t*>(logicThread) +
                                           kLogicToNetQueueOffset));
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
        S3RE_LOG("record: %lu commands in %u step slots", g_commands, g_slot);
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
