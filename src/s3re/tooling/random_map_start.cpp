// Starting a random-map game without menus (`s3re_launcher --random-map
// SIZE[,SYMMETRY[,FIELD0C[,SEED]]]`, S3RE_RANDOM_MAP), for whole-game checks
// of the random map generator CGameData 0x5166d0 (our GameWorld::Create).
//
// Tooling, not a reimplementation. How the game starts a random map:
//
// - The map-selection dialog (constructor 0x47fea0, result 0x481e90) keeps a
//   Dirk::CMapFile (0x134 bytes, constructor 0x4a5a70) at this+0x2650. When
//   its random entry is chosen it switches the file to memory only (0x4a5f40
//   close, 0x4a60c0 sets +4 = 1, so chunk lookups 0x4a60f0 never read a
//   file) and, when the dialog closes with the random type (this+0x2660 ==
//   2), adds two chunks with CMapFile::AddChunk 0x4a63f0 (thiscall: type,
//   sub-index, data; data dword 1 is the chunk size including the 8-byte
//   header):
//     chunk 5 (0x18 bytes): word 5, word 0, dword 0x18, map size
//       ((combo index + 6) * 64, default 384), dword from the second combo
//       (not read by the simulation), symmetry (third combo), seed (atoi of
//       the seed field; the default text is rand()).
//     chunk 1 (0x20 bytes): word 1, word 0, dword 0x20, dword 2 (random
//       map), number of start positions min(((size - 32)^2 >> 14) + 1, 20),
//       four zero dwords.
//   The free-game dialog (0x4878b0 -> 0x488300) then calls GameSettings::
//   NewGame 0x431ab0 (thiscall, 13 stack arguments, ret 0x34; the map file is
//   argument 6) on the global GameSettings 0x7a7a18.
// - CLogicThread::OnStart (0x53c9e0) finds chunk 5 (0x53cdfe) and calls
//   Create(word seed, size, symmetry) before the map loader 0x4fd540, which
//   finds no landscape chunks (6..10) in such a file.
//
// The menu thread's `-map` start (Menus::CDrawThread::Update 0x493880, state
// 0x14) builds the player setup from a map file (0x4a67c0 reads its chunk 3,
// which a random map doesn't have) and then calls NewGame with that file.
// This pass-through hook on NewGame, only for that call site (return address
// 0x493a56) and only once, builds a random map file exactly like the dialog
// and passes it instead of the -map file; all other arguments (the player
// setup of the -map file: one human, the other slots computer players) stay
// as they are. So the launcher passes a real map file as -map for the player
// setup; the landscape comes from the random map generator.
//
// The seed: Create seeds the RNG through GameWorld::SeedRandom, so with
// `--seed N` (S3RE_SEED) the map is generated from N as well.

#include "hooks.h"
#include "log.h"

#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace s3re::tooling {

namespace {

struct CGameSettings;  // GameSettings at 0x7a7a18, opaque
struct CMapFile;       // Dirk::CMapFile, opaque, 0x134 bytes

using NewGameFn = void(__fastcall*)(CGameSettings* self, void* edx, std::uint32_t a1,
                                    std::uint32_t a2, std::uint32_t a3, void* a4, void* a5,
                                    CMapFile* mapFile, void* a7, std::uint32_t a8,
                                    std::uint32_t a9, std::uint32_t a10, std::uint32_t a11,
                                    std::uint32_t a12, std::uint32_t a13);
using OperatorNewFn = void*(__cdecl*)(std::uint32_t size);
using MapFileCtorFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t mode);
using MapFileMemoryOnlyFn = void(__fastcall*)(CMapFile* self, void* edx);
using MapFileAddChunkFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type,
                                            std::uint32_t subIndex, const void* data);

void __fastcall Hooked_GameSettings_NewGame(CGameSettings* self, void* edx, std::uint32_t a1,
                                            std::uint32_t a2, std::uint32_t a3, void* a4,
                                            void* a5, CMapFile* mapFile, void* a7,
                                            std::uint32_t a8, std::uint32_t a9,
                                            std::uint32_t a10, std::uint32_t a11,
                                            std::uint32_t a12, std::uint32_t a13);

S3RE_PASSTHROUGH(0x00431ab0, GameSettings_NewGame, NewGameFn, Original_GameSettings_NewGame,
                 Hooked_GameSettings_NewGame);

const auto GameOperatorNew = reinterpret_cast<OperatorNewFn>(0x0061efb9);
const auto MapFile_Construct = reinterpret_cast<MapFileCtorFn>(0x004a5a70);
const auto MapFile_SetMemoryOnly = reinterpret_cast<MapFileMemoryOnlyFn>(0x004a60c0);
const auto MapFile_AddChunk = reinterpret_cast<MapFileAddChunkFn>(0x004a63f0);

// Return address of the NewGame call in Menus::CDrawThread::Update (the
// -map start).
constexpr std::uintptr_t kStartupMapCallReturn = 0x00493a56;
constexpr std::uint32_t kMapFileSize = 0x134;

#pragma pack(push, 1)
struct RandomMapChunk {  // chunk 5
    std::uint16_t type = 5;
    std::uint16_t subIndex = 0;
    std::uint32_t size = 0x18;
    std::uint32_t mapSize = 0;
    std::uint32_t field0c = 0;
    std::uint32_t symmetry = 0;
    std::uint32_t seed = 0;
};
struct MapInfoChunk {  // chunk 1
    std::uint16_t type = 1;
    std::uint16_t subIndex = 0;
    std::uint32_t size = 0x20;
    std::uint32_t kind = 2;  // random map
    std::uint32_t startPositions = 0;
    std::uint32_t unused[4] = {};
};
#pragma pack(pop)
static_assert(sizeof(RandomMapChunk) == 0x18);
static_assert(sizeof(MapInfoChunk) == 0x20);

struct RandomMapParams {
    bool enabled = false;
    std::uint32_t size = 0;
    std::uint32_t symmetry = 0;
    std::uint32_t field0c = 0;
    std::uint32_t seed = 0;
};

// S3RE_RANDOM_MAP = "size,symmetry,field0c,seed" (the launcher fills in all
// four).
RandomMapParams ReadParams() {
    RandomMapParams params;
    char text[128] = {};
    const DWORD length = GetEnvironmentVariableA("S3RE_RANDOM_MAP", text, sizeof(text));
    if (length == 0 || length >= sizeof(text)) {
        return params;
    }
    std::uint32_t values[4] = {};
    const char* cursor = text;
    for (std::uint32_t& value : values) {
        char* end = nullptr;
        value = std::strtoul(cursor, &end, 0);
        if (*end != ',') {
            break;
        }
        cursor = end + 1;
    }
    params.enabled = true;
    params.size = values[0];
    params.symmetry = values[1];
    params.field0c = values[2];
    params.seed = values[3];
    return params;
}

// What the map dialog builds for a random map (0x4808d0, 0x483816, 0x481fa6).
CMapFile* MakeRandomMapFile(const RandomMapParams& params) {
    void* memory = GameOperatorNew(kMapFileSize);
    if (memory == nullptr) {
        return nullptr;
    }
    std::memset(memory, 0, kMapFileSize);
    CMapFile* file = static_cast<CMapFile*>(memory);
    MapFile_Construct(file, nullptr, 0);
    MapFile_SetMemoryOnly(file, nullptr);

    RandomMapChunk random;
    random.mapSize = params.size;
    random.field0c = params.field0c;
    random.symmetry = params.symmetry;
    random.seed = params.seed;
    MapFile_AddChunk(file, nullptr, 5, 0, &random);

    MapInfoChunk info;
    const std::uint32_t distance = params.size - 0x20;
    std::uint32_t positions = ((distance * distance) >> 14) + 1;
    if (positions > 0x13) {
        positions = 0x14;
    }
    info.startPositions = positions;
    MapFile_AddChunk(file, nullptr, 1, 0, &info);
    return file;
}

bool g_handled = false;  // only touched by the menu thread

void __fastcall Hooked_GameSettings_NewGame(CGameSettings* self, void* edx, std::uint32_t a1,
                                            std::uint32_t a2, std::uint32_t a3, void* a4,
                                            void* a5, CMapFile* mapFile, void* a7,
                                            std::uint32_t a8, std::uint32_t a9,
                                            std::uint32_t a10, std::uint32_t a11,
                                            std::uint32_t a12, std::uint32_t a13) {
    if (!g_handled &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kStartupMapCallReturn) {
        g_handled = true;
        const RandomMapParams params = ReadParams();
        if (params.enabled) {
            CMapFile* random = MakeRandomMapFile(params);
            if (random == nullptr) {
                S3RE_LOG("random-map: out of memory; ending the game");
                TerminateProcess(GetCurrentProcess(), 4);
                return;
            }
            S3RE_LOG("random-map: size %u, symmetry %u, field0c %u, seed %u (players from "
                     "the -map file)",
                     params.size, params.symmetry, params.field0c, params.seed);
            // The -map file stays allocated (the menu thread still reads its
            // chunk 0x40 after this call); it is not freed, a small leak.
            mapFile = random;
        }
    }
    Original_GameSettings_NewGame(self, edx, a1, a2, a3, a4, a5, mapFile, a7, a8, a9, a10, a11,
                                  a12, a13);
}

}  // namespace

}  // namespace s3re::tooling
