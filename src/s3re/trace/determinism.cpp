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

// S3RE_DUMP_TICK: at this tick, the world header is also written to
// <trace>-header-<tick>.bin, to find out which bytes differ between runs.
// 0 if unset.
std::uint32_t DumpTick() {
    static const std::uint32_t value = [] {
        char text[32] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_DUMP_TICK", text, sizeof(text));
        return (length > 0 && length < sizeof(text))
                   ? static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10))
                   : 0u;
    }();
    return value;
}

void DumpHeader(const std::uint8_t* world, std::uint32_t tick) {
    char path[MAX_PATH + 32];
    std::snprintf(path, sizeof(path), "%.*s-header-%u.bin",
                  static_cast<int>(std::strlen(g_path) - 4), g_path, tick);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
        S3RE_LOG("determinism trace: could not create %s", path);
        return;
    }
    const std::size_t size =
        ReadableLength(world, s3re::game::world_offset::kMapTiles);
    std::fwrite(world, 1, size, file);
    std::fclose(file);
    S3RE_LOG("determinism trace: dumped %zu header bytes at tick %u to %s", size, tick, path);
}

constexpr std::uint64_t kHashSeed = 0xcbf29ce484222325ull;

struct RegionHashes {
    std::uint64_t header;
    std::uint64_t map;
    std::uint64_t rest;
    std::size_t restBytes;
};

// Header bytes left out of the header hash. They differ between otherwise
// identical runs because the code writing them runs a timing-dependent
// number of times (per step, not per tick). Found 2026-11-07 by diffing
// header dumps (--dump-tick); see LOGBOOK.md.
struct ByteRange {
    std::size_t begin;
    std::size_t end;
};
constexpr ByteRange kVolatileHeaderRanges[] = {
    // Last search result position (x, y), written by 0x4ff0a0/0x4ff170,
    // called from 0x524fa0 on every step (probably mouse/UI driven).
    {0x2d2e4, 0x2d2ec},
    // Byte counter incremented on every call of 0x51f110 (called from
    // CLogicThread::Step between the update phases; fills the object at
    // CLogicThread+0x3d0, probably for the draw thread).
    {0x2d301, 0x2d302},
};

std::uint64_t HashHeader(const std::uint8_t* world, std::size_t size) {
    std::uint64_t hash = kHashSeed;
    std::size_t position = 0;
    for (const ByteRange& skip : kVolatileHeaderRanges) {
        if (skip.begin >= size) {
            break;
        }
        hash = Hash(hash, world + position, skip.begin - position);
        position = skip.end < size ? skip.end : size;
    }
    return Hash(hash, world + position, size - position);
}

// Must not contain objects with destructors because of __try.
bool HashRegions(const std::uint8_t* world, std::uint32_t mapSize, RegionHashes* out) {
    __try {
        using namespace s3re::game::world_offset;
        out->header = HashHeader(world, ReadableLength(world, kMapTiles));

        out->map = kHashSeed;
        if (mapSize > 0 && mapSize <= kMapRowTiles) {
            for (std::uint32_t row = 0; row < mapSize; ++row) {
                const std::uint8_t* rowStart = world + kMapTiles + row * kMapRowBytes;
                const std::size_t rowBytes = (mapSize + 1) * kTileBytes;
                out->map = Hash(out->map, rowStart, ReadableLength(rowStart, rowBytes));
            }
        }

        const std::uint8_t* rest = world + kMapTiles + kMapRowTiles * kMapRowBytes;
        out->restBytes = ReadableLength(rest, kRestRegionCap);
        out->rest = Hash(kHashSeed, rest, out->restBytes);
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

    if (newTick && tick == DumpTick()) {
        DumpHeader(world, tick);
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
