#include "trace/determinism.h"

#include "game/world.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace s3re::trace {
namespace {

constexpr std::uintptr_t kGameTickAddress = 0x007dfd48;  // g_gameTick
constexpr std::size_t kWorldPointerOffset = 0x3a0;       // CLogicThread -> GameWorld*
constexpr std::uint32_t kHashInterval = 32;               // ticks between region hashes

// "Rest" region: everything after the map tile array, up to a cap. The real
// end of the world object is unknown; reads are clamped to committed memory.
constexpr std::size_t kRestRegionCap = 0x800000;

FILE* g_file = nullptr;
bool g_failed = false;
int g_segment = 0;
const void* g_lastWorld = nullptr;
std::uint32_t g_lastTick = 0;
std::uint16_t g_lastRandom[3] = {};
bool g_haveLast = false;
unsigned long g_stepsSeen = 0;
char g_path[MAX_PATH] = {};  // the trace file, for naming dumps next to it
unsigned long g_stepsWithWorld = 0;

int EnabledState() {
    static const int state = [] {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_TRACE", value, sizeof(value));
        return (length > 0 && length < sizeof(value) && std::strstr(value, "determinism"))
                   ? 1
                   : 0;
    }();
    return state;
}

bool OpenFile() {
    if (g_file != nullptr) {
        return true;
    }
    if (g_failed) {
        return false;
    }
    char path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&OpenFile), &self);
    const DWORD length = GetModuleFileNameA(self, path, MAX_PATH);
    char* slash = length > 0 ? std::strrchr(path, '\\') : nullptr;
    if (slash == nullptr) {
        g_failed = true;
        return false;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::snprintf(slash + 1, MAX_PATH - (slash + 1 - path),
                  "determinism-%04u%02u%02u-%02u%02u%02u.tsv", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond);
    if (fopen_s(&g_file, path, "w") != 0 || g_file == nullptr) {
        g_failed = true;
        S3RE_LOG("determinism trace: could not create %s", path);
        return false;
    }
    std::setvbuf(g_file, nullptr, _IOFBF, 1 << 16);
    std::fprintf(g_file, "# s3re determinism trace v2 (header hash skips volatile fields)\n");
    std::fprintf(g_file, "# T <tick> <rng a> <rng b> <rng c>   after each step that changed them\n");
    std::fprintf(g_file, "# H <tick> <header hash> <map hash> <rest hash> <rest bytes>   every "
                         "%u ticks\n", kHashInterval);
    std::fflush(g_file);
    std::memcpy(g_path, path, sizeof(g_path));
    S3RE_LOG("determinism trace: writing %s", path);
    return true;
}

// Number of bytes from `start` (up to `wanted`) that are committed and readable.
std::size_t ReadableLength(const std::uint8_t* start, std::size_t wanted) {
    std::size_t covered = 0;
    while (covered < wanted) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(start + covered, &info, sizeof(info)) == 0) {
            break;
        }
        const DWORD protect = info.Protect & 0xff;
        const bool readable = info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD) == 0 &&
                              protect != PAGE_NOACCESS && protect != PAGE_EXECUTE;
        if (!readable) {
            break;
        }
        const auto* regionEnd = static_cast<const std::uint8_t*>(info.BaseAddress) +
                                info.RegionSize;
        covered = static_cast<std::size_t>(regionEnd - start);
    }
    return covered < wanted ? covered : wanted;
}

// 64-bit FNV-1a over 32-bit words (plus trailing bytes). Only needs to detect
// differences, not resist attacks. Optimized even in debug builds because it
// runs over megabytes.
#pragma optimize("gt", on)
std::uint64_t Hash(std::uint64_t hash, const std::uint8_t* data, std::size_t size) {
    std::size_t i = 0;
    for (; i + 4 <= size; i += 4) {
        std::uint32_t word;
        std::memcpy(&word, data + i, 4);
        hash = (hash ^ word) * 0x100000001b3ull;
    }
    for (; i < size; ++i) {
        hash = (hash ^ data[i]) * 0x100000001b3ull;
    }
    return hash;
}
#pragma optimize("", on)

// S3RE_DUMP_TICK: comma-separated ticks (up to 16) at which the hashed world
// memory (header, map tiles and the rest region, as far as readable) is also
// written to <trace>-world-<tick>.bin, to find out which bytes differ
// between runs.
bool IsDumpTick(std::uint32_t tick) {
    struct Ticks {
        std::uint32_t values[16] = {};
        int count = 0;
    };
    static const Ticks ticks = [] {
        Ticks result;
        char text[256] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_DUMP_TICK", text, sizeof(text));
        if (length == 0 || length >= sizeof(text)) {
            return result;
        }
        const char* cursor = text;
        while (*cursor != '\0' && result.count < 16) {
            char* end = nullptr;
            const unsigned long value = std::strtoul(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            result.values[result.count++] = static_cast<std::uint32_t>(value);
            cursor = *end == ',' ? end + 1 : end;
        }
        return result;
    }();
    for (int i = 0; i < ticks.count; ++i) {
        if (ticks.values[i] == tick && tick != 0) {
            return true;
        }
    }
    return false;
}

void DumpWorld(const std::uint8_t* world, std::uint32_t tick) {
    char path[MAX_PATH + 32];
    std::snprintf(path, sizeof(path), "%.*s-world-%u.bin",
                  static_cast<int>(std::strlen(g_path) - 4), g_path, tick);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
        S3RE_LOG("determinism trace: could not create %s", path);
        return;
    }
    using namespace s3re::game::world_offset;
    const std::size_t size =
        ReadableLength(world, kMapTiles + kMapRowTiles * kMapRowBytes + kRestRegionCap);
    std::fwrite(world, 1, size, file);
    std::fclose(file);
    S3RE_LOG("determinism trace: dumped %zu world bytes at tick %u to %s", size, tick, path);
}

constexpr std::uint64_t kHashSeed = 0xcbf29ce484222325ull;

struct RegionHashes {
    std::uint64_t header;
    std::uint64_t map;
    std::uint64_t rest;
    std::size_t restBytes;
};

// World bytes left out of the hashes. Two kinds:
//  - volatile: written by code that runs a timing-dependent number of times
//    (per step, not per tick); differ between otherwise identical runs.
//  - UI/presentation state that the game keeps inside the world object; it
//    changes with the player's mouse input, which a replay doesn't repeat.
// Found 2026-11-07 by diffing world dumps (--dump-tick) of identical runs
// and of a played game against its replay (RNG identical throughout); the
// owners were then checked in the code. See LOGBOOK.md.
struct ByteRange {
    std::size_t begin;
    std::size_t end;
};
// Offsets from the start of the world object, sorted, non-overlapping.
constexpr ByteRange kHeaderSkips[] = {
    // Statistics chart for drawing: graph buffer, maximum and axis labels,
    // computed by 0x4ff550 only while the statistics window is open (called
    // only from PrepareDrawData; otherwise only initialized by 0x4fce90).
    {0x2d02c, 0x2d244},
    // Camera position (x, y) and a second (target?) pair, in pixels. Moved
    // by the UI (and by command 2, which the game's movie recorder uses);
    // 0x5158f0/0x515980 turn it into the tile at the screen centre.
    {0x2d254, 0x2d264},
    // "Refresh the UI" flag: set by ExecuteCommands when a command touches
    // what the local player's open menu shows (0x53709f).
    {0x2d27c, 0x2d280},
    // UI selection mode and two parameters; set by the UI, the command
    // executor only reads them to refresh overlays. Then the sub-page of UI
    // menu 2 (+0x2d290), read by ExecuteCommands only to decide whether to
    // set the refresh flag.
    {0x2d284, 0x2d294},
    // UI menu sub-page; set by ProcessEvents, read by ExecuteCommands only
    // to decide whether to set the refresh flag above.
    {0x2d2a0, 0x2d2a4},
    // Last search result position (x, y), written by 0x4ff0a0/0x4ff170,
    // called from 0x524fa0 on every step (volatile).
    {0x2d2e4, 0x2d2ec},
    // UI cursor mode: set by CLogicThread::ProcessEvents, read by
    // CLogicThread::PrepareDrawData.
    {0x2d2f0, 0x2d2f4},
    // Search generation counter (GameWorld::NextSearchGeneration; UI
    // overlays bump it too) and the byte counter incremented on every call
    // of PrepareDrawData (volatile).
    {0x2d300, 0x2d302},
};
// After the map tile array: the 10-entry UI -> draw feedback list and its
// count (filled via 0x53f560 by the UI handler, drained by PrepareDrawData).
constexpr ByteRange kRestSkips[] = {
    // Scratch grids (2 x 31x31 bytes) of the building footprint check
    // 0x4da440 (via 0x4d99e0), which the UI's placement preview, the command
    // executor and the computer player all call; fully rewritten per call.
    {0x1222ed4, 0x1223656},
    {0x1539484, 0x15394b0},
};
// Bytes of every map tile: +15 "visited" mark of searches (stamped with the
// search generation; UI overlays use it too), +16 overlay value for drawing
// (written by the overlay functions 0x523560/0x523840 and PrepareDrawData).
// Also bit 7 of +13, a "redraw this tile" flag: set by simulation code when
// a tile changes, cleared by PrepareDrawData for the tiles in view.
constexpr std::size_t kTileSkipBegin = 15;
constexpr std::size_t kTileSkipEnd = 17;
constexpr std::size_t kTileRedrawByte = 13;
constexpr std::uint8_t kTileRedrawBit = 0x80;

// Hashes world[begin, begin + size) without the bytes in `skips`.
template <std::size_t N>
std::uint64_t HashSkipping(std::uint64_t hash, const std::uint8_t* world, std::size_t begin,
                           std::size_t size, const ByteRange (&skips)[N]) {
    std::size_t position = begin;
    const std::size_t end = begin + size;
    for (const ByteRange& skip : skips) {
        if (skip.end <= position || skip.begin >= end) {
            continue;
        }
        hash = Hash(hash, world + position, skip.begin - position);
        position = skip.end < end ? skip.end : end;
    }
    return Hash(hash, world + position, end - position);
}

// Hashes `tiles` map tiles, leaving out the per-tile UI bytes.
using s3re::game::world_offset::kMapRowBytes;
using s3re::game::world_offset::kTileBytes;
std::uint64_t HashTiles(std::uint64_t hash, const std::uint8_t* row, std::size_t tiles) {
    std::uint8_t buffer[kMapRowBytes];
    std::memcpy(buffer, row, tiles * kTileBytes);
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        std::uint8_t* record = buffer + tile * kTileBytes;
        std::memset(record + kTileSkipBegin, 0, kTileSkipEnd - kTileSkipBegin);
        record[kTileRedrawByte] &= static_cast<std::uint8_t>(~kTileRedrawBit);
    }
    return Hash(hash, buffer, tiles * kTileBytes);
}

// Must not contain objects with destructors because of __try.
bool HashRegions(const std::uint8_t* world, std::uint32_t mapSize, RegionHashes* out) {
    __try {
        using namespace s3re::game::world_offset;
        out->header =
            HashSkipping(kHashSeed, world, 0, ReadableLength(world, kMapTiles), kHeaderSkips);

        out->map = kHashSeed;
        if (mapSize > 0 && mapSize <= kMapRowTiles) {
            for (std::uint32_t row = 0; row < mapSize; ++row) {
                const std::uint8_t* rowStart = world + kMapTiles + row * kMapRowBytes;
                const std::size_t rowBytes = (mapSize + 1) * kTileBytes;
                out->map = HashTiles(out->map, rowStart,
                                     ReadableLength(rowStart, rowBytes) / kTileBytes);
            }
        }

        const std::size_t restBegin = kMapTiles + kMapRowTiles * kMapRowBytes;
        out->restBytes = ReadableLength(world + restBegin, kRestRegionCap);
        out->rest = HashSkipping(kHashSeed, world, restBegin, out->restBytes, kRestSkips);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool DeterminismEnabled() {
    return EnabledState() == 1;
}

void OnLogicStep(const void* logicThread) {
    if (!DeterminismEnabled() || logicThread == nullptr) {
        return;
    }
    ++g_stepsSeen;
    const auto* thread = static_cast<const std::uint8_t*>(logicThread);
    const auto* world =
        *reinterpret_cast<const std::uint8_t* const*>(thread + kWorldPointerOffset);
    if (world == nullptr) {
        return;
    }
    ++g_stepsWithWorld;
    if (!OpenFile()) {
        return;
    }

    using namespace s3re::game::world_offset;
    const std::uint32_t tick = *reinterpret_cast<const std::uint32_t*>(kGameTickAddress);
    std::uint16_t random[3];
    std::memcpy(random, world + kRandomState, sizeof(random));
    const std::uint32_t mapSize = *reinterpret_cast<const std::uint32_t*>(world + kMapSize);

    // A new game (different world, or the tick going backwards) starts a new
    // segment so traces of several games in one session stay comparable.
    if (!g_haveLast || world != g_lastWorld || tick < g_lastTick) {
        ++g_segment;
        std::fprintf(g_file, "# segment %d: mapSize=%u first tick=%u\n", g_segment, mapSize,
                     tick);
        g_lastWorld = world;
        g_haveLast = false;
    }

    const bool changed = !g_haveLast || tick != g_lastTick ||
                         std::memcmp(random, g_lastRandom, sizeof(random)) != 0;
    if (!changed) {
        return;
    }
    const bool newTick = !g_haveLast || tick != g_lastTick;
    g_lastTick = tick;
    std::memcpy(g_lastRandom, random, sizeof(random));
    g_haveLast = true;

    std::fprintf(g_file, "T\t%u\t%u\t%u\t%u\n", tick, random[0], random[1], random[2]);

    if (newTick && IsDumpTick(tick)) {
        DumpWorld(world, tick);
    }

    if (newTick && tick % kHashInterval == 0) {
        RegionHashes hashes{};
        if (HashRegions(world, mapSize, &hashes)) {
            std::fprintf(g_file, "H\t%u\t%016llx\t%016llx\t%016llx\t%zu\n", tick, hashes.header,
                         hashes.map, hashes.rest, hashes.restBytes);
        } else {
            std::fprintf(g_file, "# hashing failed at tick %u\n", tick);
        }
        std::fflush(g_file);
    }
}

void CloseDeterminismTrace() {
    if (DeterminismEnabled()) {
        S3RE_LOG("determinism trace: %lu step(s) seen, %lu with a world, %d segment(s)",
                 g_stepsSeen, g_stepsWithWorld, g_segment);
    }
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

}  // namespace s3re::trace
