#include "tooling/trace/determinism.h"

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/world.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace s3re::trace {
namespace {

constexpr std::uint32_t kHashInterval = 32;               // ticks between region hashes

using s3re::game::CGameData;
using s3re::game::layout::Tile;

// The three hashed regions (trace v5, game/layout.h): the header is
// everything before the tile array, the map is the first mapSize tiles of
// the first mapSize rows, and the rest runs from the end of the tile array
// to the end of the world object (reads are clamped to committed memory).
constexpr std::size_t kTilesBegin = offsetof(CGameData, tiles);
constexpr std::size_t kTilesEnd = kTilesBegin + sizeof(CGameData::tiles);
constexpr std::size_t kWorldBytes = sizeof(CGameData);
constexpr std::size_t kMapRowTiles = s3re::game::layout::kMapRowTiles;
constexpr std::size_t kTileBytes = sizeof(Tile);
constexpr std::size_t kMapRowBytes = kMapRowTiles * kTileBytes;

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
    std::fprintf(g_file, "# s3re determinism trace v5 (layout.h regions: tiles from 0x67444; header hash skips volatile fields; rest hash skips MapEffect padding)\n");
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

// S3RE_DUMP_HEADERS (`--dump-headers`): at every hashed tick, append the
// world header to <trace>-headers.bin as records of (uint32 tick, uint32
// size, header bytes). Finds short-lived header differences that fall
// between --dump-tick ticks (tools/diff_world_dumps.py --headers).
FILE* g_headerFile = nullptr;

bool DumpHeadersEnabled() {
    static const bool value = GetEnvironmentVariableA("S3RE_DUMP_HEADERS", nullptr, 0) > 0;
    return value;
}

void AppendHeader(const std::uint8_t* world, std::uint32_t tick) {
    if (g_headerFile == nullptr) {
        char path[MAX_PATH + 32];
        std::snprintf(path, sizeof(path), "%.*s-headers.bin",
                      static_cast<int>(std::strlen(g_path) - 4), g_path);
        if (fopen_s(&g_headerFile, path, "wb") != 0 || g_headerFile == nullptr) {
            g_headerFile = nullptr;
            return;
        }
        S3RE_LOG("determinism trace: writing headers to %s", path);
    }
    const std::uint32_t size = static_cast<std::uint32_t>(ReadableLength(world, kTilesBegin));
    std::fwrite(&tick, sizeof(tick), 1, g_headerFile);
    std::fwrite(&size, sizeof(size), 1, g_headerFile);
    std::fwrite(world, 1, size, g_headerFile);
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
    const std::size_t size = ReadableLength(world, kWorldBytes);
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
    // Then the window's selection (+0x2d244): set by ProcessEvents, read
    // only by PrepareDrawData to choose what to chart. (Up to trace v4 this
    // range started at 0x2d02c and also hid the last 12 statistics bytes.)
    {offsetof(CGameData, chart), offsetof(CGameData, objective_timeout)},
    // Camera position (x, y) and a second (target?) pair, in pixels. Moved
    // by the UI (and by command 2, which the game's movie recorder uses);
    // 0x5158f0/0x515980 turn it into the tile at the screen centre.
    // Then a "redraw" flag (+0x2d264): set by the UI, the simulation and
    // Step, read only by PrepareDrawData, which hands it to the draw thread
    // and resets it (0x51f566/0x51f5d8), a timing-dependent number of times.
    {offsetof(CGameData, camera_drawn_x), offsetof(CGameData, ui_2d268)},
    // Draw hand-off flags, also reset by PrepareDrawData: minimap ready
    // (+0x2d278, set by CGameData::BuildMinimap) and +0x2d280. Between them
    // the "refresh the UI" flag (+0x2d27c): set by ExecuteCommands when a
    // command touches what the local player's open menu shows (0x53709f).
    {offsetof(CGameData, minimap_ready), offsetof(CGameData, ui_selection_mode)},
    // UI selection mode and two parameters; set by the UI, the command
    // executor only reads them to refresh overlays. Then the sub-page of UI
    // menu 2 (+0x2d290), read by ExecuteCommands only to decide whether to
    // set the refresh flag.
    {offsetof(CGameData, ui_selection_mode), offsetof(CGameData, ui_2d290) + 4},
    // UI menu sub-page; set by ProcessEvents, read by ExecuteCommands only
    // to decide whether to set the refresh flag above.
    {offsetof(CGameData, ui_2d2a0), offsetof(CGameData, ui_2d2a4)},
    // Notification blink bits for the draw thread: PrepareDrawData (0x51f110)
    // copies the old value into the draw data, resets it and sets bits 1/2/
    // 4/8 for the pending notifications (+0x2d320, +0x2d7d0, +0x2dc80,
    // +0x2e130) when bit 5 of the step counter +0x2d2d8 is set; it is read
    // only there (0x51f748, 0x51f7e0) and otherwise only initialized by
    // 0x4fce90. PrepareDrawData runs a timing-dependent number of times, so
    // the value differs between normal speed and --fast (found 2026-11-10,
    // savegame "slot 1.sav", header dumps).
    {offsetof(CGameData, notification_blink), offsetof(CGameData, ui_2d2cc)},
    // Last search result position (x, y), written by 0x4ff0a0/0x4ff170,
    // called from 0x524fa0 on every step (volatile).
    {offsetof(CGameData, last_search_x), offsetof(CGameData, unk2d2ec)},
    // UI cursor mode: set by CLogicThread::ProcessEvents, read by
    // CLogicThread::PrepareDrawData.
    {offsetof(CGameData, ui_cursor_mode), offsetof(CGameData, landscape_x)},
    // Search generation counter (CGameData::NextSearchGeneration; UI
    // overlays bump it too) and the byte counter incremented on every call
    // of PrepareDrawData (volatile).
    {offsetof(CGameData, search_generation), offsetof(CGameData, pad2d302)},
};
static_assert(kHeaderSkips[0].begin == 0x2d038 && kHeaderSkips[1].begin == 0x2d254 &&
              kHeaderSkips[3].end == 0x2d294 && kHeaderSkips[4].begin == 0x2d2a0 &&
              kHeaderSkips[5].begin == 0x2d2c8 &&
              kHeaderSkips[8].end == 0x2d302, "same ranges as trace v4 except the first");
// After the map tile array: the 10-entry UI -> draw feedback list and its
// count (filled via 0x53f560 by the UI handler, drained by PrepareDrawData).
constexpr ByteRange kRestSkips[] = {
    // Scratch grids (2 x 31x31 bytes) of the building footprint check
    // 0x4da440 (via 0x4d99e0), which the UI's placement preview, the command
    // executor and the computer player all call; fully rewritten per call.
    {offsetof(CGameData, footprint_scratch), offsetof(CGameData, minimap)},
    {offsetof(CGameData, ui_feedback), offsetof(CGameData, flag15394b0)},
};
// The 5000 MapEffect records (0x4C bytes) of the member at +0x14D2BC8
// (re/structs.md): their alignment padding (+0x2A..+0x2B, +0x3B,
// +0x3E..+0x3F; no constructor writes it and no code reads it) is copied
// from uninitialized stack memory when a record is added (0x5445E0). It
// differed between two runs loaded from the same savegame (2026-11-09,
// tools/check_save.py), so it is left out of the rest hash.
constexpr std::size_t kMapEffectsBegin = offsetof(CGameData, map_effects);
constexpr std::size_t kMapEffectBytes = sizeof(s3re::game::layout::MapEffect);
constexpr std::size_t kMapEffectCount =
    sizeof(s3re::game::layout::MapEffectTable::records) / kMapEffectBytes;
constexpr ByteRange kMapEffectPadding[] = {{0x2a, 0x2c}, {0x3b, 0x3c}, {0x3e, 0x40}};

// Bytes of every map tile: search_mark (+0xb, "visited" mark of searches,
// stamped with the search generation; UI overlays use it too) and
// overlay_value (+0xc, written by the overlay functions 0x523560/0x523840
// and PrepareDrawData). Also bit 7 of draw_flags (+0x9), a "redraw this
// tile" flag: set by simulation code when a tile changes, cleared by
// PrepareDrawData for the tiles in view.
constexpr std::size_t kTileSkipBegin = offsetof(Tile, search_mark);
constexpr std::size_t kTileSkipEnd = offsetof(Tile, overlay_value) + 1;
constexpr std::size_t kTileRedrawByte = offsetof(Tile, draw_flags);
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

// Hashes world[begin, end) like HashSkipping with kRestSkips, but with the
// MapEffect padding bytes zeroed.
std::uint64_t HashRest(std::uint64_t hash, const std::uint8_t* world, std::size_t begin,
                       std::size_t end) {
    const std::size_t effectsEnd = kMapEffectsBegin + kMapEffectCount * kMapEffectBytes;
    if (end <= kMapEffectsBegin || begin >= effectsEnd) {
        return HashSkipping(hash, world, begin, end - begin, kRestSkips);
    }
    // No skip range overlaps the MapEffect array.
    hash = HashSkipping(hash, world, begin, kMapEffectsBegin - begin, kRestSkips);
    const std::size_t recordsEnd = end < effectsEnd ? end : effectsEnd;
    std::uint8_t record[kMapEffectBytes];
    std::size_t position = kMapEffectsBegin;
    for (; position + kMapEffectBytes <= recordsEnd; position += kMapEffectBytes) {
        std::memcpy(record, world + position, kMapEffectBytes);
        for (const ByteRange& padding : kMapEffectPadding) {
            std::memset(record + padding.begin, 0, padding.end - padding.begin);
        }
        hash = Hash(hash, record, kMapEffectBytes);
    }
    return HashSkipping(hash, world, position, end - position, kRestSkips);
}

// Must not contain objects with destructors because of __try.
bool HashRegions(const std::uint8_t* world, std::uint32_t mapSize, RegionHashes* out) {
    __try {
        out->header =
            HashSkipping(kHashSeed, world, 0, ReadableLength(world, kTilesBegin), kHeaderSkips);

        out->map = kHashSeed;
        if (mapSize > 0 && mapSize <= kMapRowTiles) {
            for (std::uint32_t row = 0; row < mapSize; ++row) {
                const std::uint8_t* rowStart = world + kTilesBegin + row * kMapRowBytes;
                const std::size_t rowBytes = mapSize * kTileBytes;
                out->map = HashTiles(out->map, rowStart,
                                     ReadableLength(rowStart, rowBytes) / kTileBytes);
            }
        }

        const std::size_t restBegin = kTilesEnd;
        out->restBytes = ReadableLength(world + restBegin, kWorldBytes - restBegin);
        out->rest = HashRest(kHashSeed, world, restBegin, restBegin + out->restBytes);
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
    const auto* thread = static_cast<const s3re::game::CLogicThread*>(logicThread);
    const auto* world = reinterpret_cast<const std::uint8_t*>(thread->world);
    if (world == nullptr) {
        return;
    }
    ++g_stepsWithWorld;
    if (!OpenFile()) {
        return;
    }

    const std::uint32_t tick = game::g_gameTick;
    const auto* fields = reinterpret_cast<const CGameData*>(world);
    std::uint16_t random[3];
    std::memcpy(random, fields->random_state, sizeof(random));
    const std::uint32_t mapSize = fields->map_size;

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
        if (DumpHeadersEnabled()) {
            AppendHeader(world, tick);
        }
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
    if (g_headerFile != nullptr) {
        std::fclose(g_headerFile);
        g_headerFile = nullptr;
    }
}

}  // namespace s3re::trace
