// GameWorld::Create (0x005166d0): the random map generator.
//
// (seed, mapSize, symmetry), thiscall, ret 0xc. Called by
// CLogicThread::OnStart (0x53ce35) when the map file holds a random-map
// chunk (chunk 5, DebugLog "Erstelle Zufallswelt %i %i %i" = "creating
// random world"); maps from files are built by the map loader 0x4fd540
// instead. EAX = BuildRegionsAndAreas's (mapSize); OnStart ignores it.
//
// Tile (x, y) at world+0x67444 + (x*0x300 + y)*24. Fields used here: +0x4
// height, +0x5 scratch byte of the stream carving (later the shade; the
// neighbourhood checks call it "byte 9"), +0x6 terrain, +0x7 object, +0x8
// owner, +0xa flags, +0xe word copy of the object, +0x11 resources. All
// coordinates and sizes are signed 32-bit values as in the original, and
// mapSize (world+0x2d2d0) is re-read wherever the original re-reads it
// (nothing here changes it after the start). The phases, in order:
//
//  1. Start: DebugLog("creating world"); SeedRandom(seed) (through the
//     game's address, so the --seed hook applies); world+0x2d274 =
//     symmetry, +0x2d2d0 = mapSize, +0x2d254 = +0x2d258 = 0.
//  2. ResetTiles: every map tile gets height 0, shade 0x1f, terrain,
//     object, flags, search mark, word +0xe, resources and +0x12 = 0, owner
//     0xff, words +0, +0x2, +0x14, +0x16 = 0, byte +0x9 = 0, byte +0x10 =
//     0. Bytes +0xc, +0xd, +0x13, +0x17 keep their values.
//  3. SeedCoarseHeights: every 64th tile (x, y >= 64, < mapSize) gets a
//     random height: within 64 tiles of the border < 120, within 128 tiles
//     < 250, further in 50..249.
//  4. RefineHeights: diamond-square style midpoint subdivision with steps
//     32, 16, ..., 1 (see RefineHeights).
//  5. LowerHeights: height - 30, at least 0.
//  6. LogBorderErrors: DebugLog for every border tile with a height (none
//     expected).
//  7. Mountains, only without symmetry (world+0x2d274 == 0): RaiseRidges
//     (mapSize^2/16 random starts walk uphill marking ridge tiles 0x70 and
//     0xf0), MarkRidgeHeights; then SmoothRidgeSlopes. With symmetry:
//     SmoothSlopes.
//  8. ClassifyTerrain: by height: 0 water (0), < 140 grass (0x10), < 190
//     class 0x20, else 0x80; ridge tiles 0x70 keep theirs.
//  9. Objects (+0x7) and scratch bytes (+0x5) cleared.
// 10. CarveStreams (4*mapSize^2 probes): streams 0x60..0x63 running
//     downhill, see CarveStreams; then RemoveStrayStreamTiles.
// 11. ShapeTerrain: a fixed sequence of terrain passes (borders, beaches,
//     mountain, desert, swamp... growth) and the diagonal marks for
//     symmetric maps.
// 12. PlaceObjects: 94 ScatterObjects and 8 ScatterObjectGroups calls with
//     fixed arguments (trees, stones, decoration), then 5
//     ScatterTileByte11 calls (mineral resources).
// 13. ScatterFish: tiles of terrain class 0 (water) get a random +0x11 = 0..15
//     with probability (65535-40000)/65536.
// 14. Symmetry: ClearDiagonalTileByteB; bit 1 mirrors the map through its
//     centre (point symmetry), bit 0 across the diagonal x = y.
// 15. FinishTiles: word +0xe = object; terrain classes 0x00, 0x50, 0x70,
//     0x80 are blocked (flag 0x01).
// 16. LogBorderErrors again, then BuildRegionsAndAreas (0x51a580).

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace s3re::game {

namespace {

// Tile records (re/structs.md "Tile"): +0x5, the scratch byte of the stream
// carving, is Tile::shade. World fields: the symmetry argument is stored in
// world->unk2d274 (re/world_layout.md: also read by the player placement
// 0x5074b0), +0x2d254/+0x2d258 are camera_drawn_x/y.
using layout::Tile;

constexpr std::uint8_t kRidge = 0x70;
constexpr std::uint8_t kRidgeLow = 0xf0;  // ridge tile that ends up lower (MarkRidgeHeights)
constexpr std::uint8_t kStream = 0x60;

// DebugLog format strings in S3.EXE.
constexpr std::uintptr_t kCreatingWorld = 0x72fef8;  // "creating world"
constexpr std::uintptr_t kErrorTop = 0x72fee4;       // "Error top %u %u %u"
constexpr std::uintptr_t kErrorBottom = 0x72fecc;    // "Error bottom %u %u %u"
constexpr std::uintptr_t kErrorLeft = 0x72feb8;      // "Error left %u %u %u"
constexpr std::uintptr_t kErrorRight = 0x72fea0;     // "Error right %u %u %u"

// The six directions of the hex grid, 1..6 (index 0 unused), as in the
// original's local table: (1,0), (1,1), (0,1), (-1,0), (-1,-1), (0,-1).
constexpr std::int32_t kDx[7] = {0, 1, 1, 0, -1, -1, 0};
constexpr std::int32_t kDy[7] = {0, 0, 1, 1, 0, -1, -1};

std::int32_t MapSize(GameWorld* world) {
    return static_cast<std::int32_t>(world->map_size);
}

// The symmetry argument of Create (world+0x2d274).
std::uint32_t Symmetry(GameWorld* world) {
    return static_cast<std::uint32_t>(world->unk2d274);
}

// Tile (x, y) with the original's 32-bit arithmetic (some passes read and
// write tiles just outside the map).
Tile& TileAt(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAtOffset(world, TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
}

std::uint8_t& Height(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAt(world, x, y).height;
}
std::uint8_t& Scratch(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAt(world, x, y).shade;
}
std::uint8_t& Terrain(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAt(world, x, y).terrain;
}
std::uint8_t& Object(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAt(world, x, y).object;
}

std::uint32_t Random16(GameWorld* world) {
    return GameWorld_Random(world, nullptr);
}

// lo + ((Random & 0xffff) * span) / 65536, signed 32-bit, truncated toward
// zero; the caller stores the low byte.
std::int32_t RandomBetween(GameWorld* world, std::int32_t lo, std::int32_t span) {
    const auto product = static_cast<std::int32_t>(Random16(world) * static_cast<std::uint32_t>(span));
    return lo + product / 65536;
}

// For every row y of [first, last) (outer) and column x of [first, last)
// (inner); `last` is re-evaluated after every tile.
template <typename Last, typename Visit>
void ForEachTile(std::int32_t first, Last last, Visit visit) {
    for (std::int32_t y = first; y < last(); ++y) {
        for (std::int32_t x = first; x < last(); ++x) {
            visit(x, y);
        }
    }
}

// --- 1, 2: start ---------------------------------------------------------

void ResetTiles(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) {
        Tile& tile = TileAt(world, x, y);
        tile.height = 0;
        tile.terrain = 0;
        tile.shade = 0x1f;
        tile.draw_flags = 0;
        tile.flags = 0;
        tile.settler = 0;
        tile.object_number = 0;
        tile.search_mark = 0;
        tile.walk_region = 0;
        tile.area = 0;
        tile.wide_region = 0;
        tile.owner = 0xff;
        tile.seen_object = 0;
        tile.object = 0;
        tile.resources = 0;
        tile.traffic = 0;
    });
}

// --- 3..6: heights ---------------------------------------------------------

void SeedCoarseHeights(GameWorld* world) {
    for (std::int32_t y = 64; y < MapSize(world); y += 64) {
        for (std::int32_t x = 64; x < MapSize(world); x += 64) {
            const std::int32_t size = MapSize(world);
            const auto within = [&](std::int32_t margin) {
                return x < margin || x > size - margin || y < margin || y > size - margin;
            };
            std::int32_t height;
            if (within(0x41)) {
                height = RandomBetween(world, 0, 120);
            } else if (within(0x81)) {
                height = RandomBetween(world, 0, 250);
            } else {
                height = RandomBetween(world, 50, 200);
            }
            Height(world, x, y) = static_cast<std::uint8_t>(height);
        }
    }
}

// One subdivision step of `step`: for every square (x0, y0)..(x2, y2) of
// side 2*step (x2, y2 clamped to mapSize-1 when they hit mapSize), three new
// points get a random height around the mean of the square's corner (x0, y0)
// and one other corner, within +-step*110/64 clamped to 0..255:
//   0: (xm, y0) from (x2, y0); 1: (x0, ym) from (x0, y2); 2: (xm, ym) from
//   (x2, y2).
// Points on the map border get 0 (the 0 range): point 0 when y0 == 0, or for
// steps <= 16 when x0 == 0 or x2 is the last column; point 1 symmetrically;
// point 2 for steps <= 16 when any of its square's sides is on the border.
void RefineStep(GameWorld* world, std::int32_t step) {
    for (std::int32_t y0 = 0; y0 < MapSize(world); y0 += 2 * step) {
        for (std::int32_t x0 = 0; x0 < MapSize(world); x0 += 2 * step) {
            const std::int32_t x2 = x0 + 2 * step;
            const std::int32_t y2 = y0 + 2 * step;
            const std::int32_t xm = x2 - step;
            const std::int32_t ym = y2 - step;
            const std::int32_t size = MapSize(world);
            const std::int32_t x2c = x2 == size ? size - 1 : x2;
            const std::int32_t y2c = y2 == size ? size - 1 : y2;
            for (int point = 0; point < 3; ++point) {
                std::int32_t corner;
                std::int32_t tx;
                std::int32_t ty;
                bool zero;
                const bool small = step <= 16;
                if (point == 0) {
                    corner = Height(world, x2c, y0);
                    tx = xm;
                    ty = y0;
                    zero = ((x0 == 0 || x2c == MapSize(world) - 1) && small) || y0 == 0;
                } else if (point == 1) {
                    corner = Height(world, x0, y2c);
                    tx = x0;
                    ty = ym;
                    zero = ((y0 == 0 || y2c == MapSize(world) - 1) && small) || x0 == 0;
                } else {
                    corner = Height(world, x2c, y2c);
                    tx = xm;
                    ty = ym;
                    zero = small && (x0 == 0 || x2c == MapSize(world) - 1 || y0 == 0 ||
                                     y2c == MapSize(world) - 1);
                }
                std::int32_t lo = 0;
                std::int32_t hi = 0;
                if (!zero) {
                    const std::int32_t mean = (Height(world, x0, y0) + corner) >> 1;
                    const auto spread = static_cast<std::int32_t>(static_cast<std::uint32_t>(step) * 110 >> 6);
                    lo = mean - spread;
                    hi = mean + spread;
                    if (lo < 0) {
                        lo = 0;
                    }
                    if (hi > 0xff) {
                        hi = 0xff;
                    }
                }
                Height(world, tx, ty) = static_cast<std::uint8_t>(RandomBetween(world, lo, hi - lo));
            }
        }
    }
}

void RefineHeights(GameWorld* world) {
    for (std::int32_t step = 32; step >= 1; step >>= 1) {
        RefineStep(world, step);
    }
}

void LowerHeights(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) {
        std::uint8_t& height = Height(world, x, y);
        height = height < 0x1f ? 0 : static_cast<std::uint8_t>(height - 0x1e);
    });
}

// The map border must be at height 0; the original only logs violations.
void LogBorderErrors(GameWorld* world) {
    for (std::int32_t i = 0; i < MapSize(world); ++i) {
        const std::uint8_t top = Height(world, i, 0);
        if (top != 0) {
            GameDebugLog(kErrorTop, i, 0, static_cast<std::uint32_t>(top));
        }
        const std::int32_t bottomSize = MapSize(world);
        const std::uint8_t bottom = Height(world, i, bottomSize - 1);
        if (bottom != 0) {
            GameDebugLog(kErrorBottom, i, bottomSize - 1, static_cast<std::uint32_t>(bottom));
        }
        const std::uint8_t left = Height(world, 0, i);
        if (left != 0) {
            GameDebugLog(kErrorLeft, 0, i, static_cast<std::uint32_t>(left));
        }
        const std::int32_t rightSize = MapSize(world);
        const std::uint8_t right = Height(world, rightSize - 1, i);
        if (right != 0) {
            GameDebugLog(kErrorRight, rightSize - 1, i, static_cast<std::uint32_t>(right));
        }
    }
}

// --- 7: mountain ridges ------------------------------------------------------

// A ridge walker: starts at a peak and climbs along the slope, marking the
// tiles it passes as ridge (0x70) or as "lower ridge" (0xf0). `state` is the
// direction it currently follows (1..4); a step either ends the iteration or
// continues at once with the next state's step ("chain").
struct RidgeWalker {
    GameWorld* world;
    std::int32_t x;
    std::int32_t y;
    int state;
    bool stopped;

    std::int32_t H(std::int32_t dx, std::int32_t dy) const { return Height(world, x + dx, y + dy); }
    std::uint8_t& T(std::int32_t dx, std::int32_t dy) const { return Terrain(world, x + dx, y + dy); }
    bool Room(std::int32_t dx, std::int32_t dy) const {
        return GameWorld_HasAtMostTwoType70Neighbours(world, nullptr, static_cast<std::uint32_t>(x + dx),
                                                      static_cast<std::uint32_t>(y + dy)) != 0;
    }

    enum class Next { kEnd, kChain };

    Next Stop() {
        stopped = true;
        return Next::kEnd;
    }

    // The shared middle of states 2 and 3: `a` = fall from here to the
    // neighbour, `b` = fall from the neighbour on; a steep enough first fall
    // marks the neighbour low (state `lowState`, `lowNext`), a steeper second
    // fall marks it as ridge and moves (`move`, state `ridgeState`,
    // `ridgeNext`). Returns kEnd with stopped set when neither applies.
    template <typename Move>
    Next Fork(std::int32_t a, std::int32_t b, std::uint8_t& mark, int lowState, Next lowNext, int ridgeState,
              Next ridgeNext, Move move) {
        if (b <= a) {
            if (a > 5) {
                mark = kRidgeLow;
                state = lowState;
                return lowNext;
            }
            if (b < a) {
                return Stop();
            }
        }
        if (b > 5) {
            mark = kRidge;
            move();
            state = ridgeState;
            return ridgeNext;
        }
        return Stop();
    }

    // First walk: towards -x / +y.
    Next StepFirst() {
        switch (state) {
            case 4:
                if (H(-1, -1) - H(-1, 0) > 5 && Room(-1, -1)) {
                    T(-1, -1) = kRidge;
                    --x;
                    --y;
                    state = 3;
                    return Next::kChain;
                }
                return Stop();
            case 3: {
                if (!Room(-1, 0)) {
                    return Stop();
                }
                const std::int32_t side = H(-1, 0);
                return Fork(H(0, 0) - side, side - H(0, 1), T(-1, 0), 4, Next::kEnd, 2, Next::kChain,
                            [&] { --x; });
            }
            case 2: {
                if (!Room(0, 1)) {
                    return Stop();
                }
                const std::int32_t side = H(0, 1);
                return Fork(H(0, 0) - side, side - H(1, 1), T(0, 1), 3, Next::kEnd, 1, Next::kChain,
                            [&] { ++y; });
            }
            case 1:
                if (H(0, 0) - H(1, 1) <= 5 || !Room(1, 1)) {
                    return Stop();
                }
                T(1, 1) = kRidgeLow;
                state = 2;
                return Next::kEnd;
            default:
                return Next::kEnd;
        }
    }

    // Second walk: towards +x / -y.
    Next StepSecond() {
        switch (state) {
            case 4:
                if (H(0, 0) - H(0, 1) <= 5 || !Room(0, 1)) {
                    return Stop();
                }
                T(0, 1) = kRidgeLow;
                state = 3;
                return Next::kChain;
            case 3: {
                if (!Room(1, 1)) {
                    return Stop();
                }
                const std::int32_t side = H(1, 1);
                // Marks (x+1, y) but moves diagonally to (x+1, y+1).
                return Fork(H(0, 0) - side, side - H(0, 1), T(1, 1), 2, Next::kChain, 4, Next::kEnd,
                            [&] { ++x, ++y; });
            }
            case 2: {
                if (!Room(1, 0)) {
                    return Stop();
                }
                const std::int32_t side = H(1, 0);
                return Fork(H(0, 0) - side, side - H(1, 1), T(1, 0), 1, Next::kChain, 3, Next::kEnd,
                            [&] { ++x; });
            }
            case 1:
                if (H(0, -1) - H(1, 0) <= 5 || !Room(0, -1)) {
                    return Stop();
                }
                T(0, -1) = kRidge;
                --y;
                state = 2;
                return Next::kEnd;
            default:
                return Next::kEnd;
        }
    }
};

void WalkRidge(GameWorld* world, std::int32_t x0, std::int32_t y0) {
    for (int walk = 0; walk < 2; ++walk) {
        RidgeWalker walker{world, x0, y0, 3, false};
        do {
            while ((walk == 0 ? walker.StepFirst() : walker.StepSecond()) == RidgeWalker::Next::kChain) {
            }
            const std::uint8_t height = Height(world, walker.x, walker.y);
            if (height < 0x1e || height > 0x64) {
                break;
            }
        } while (!walker.stopped);
    }
}

std::int32_t RidgeStarts(GameWorld* world) {
    return MapSize(world) * MapSize(world) / 16;
}

// mapSize^2/16 random tiles; a tile at height 31..99 that is more than 10
// above its neighbour (x, y+1) and has no ridge near it becomes a peak: it
// and (x, y+1) are marked and two walks start from it.
void RaiseRidges(GameWorld* world) {
    for (std::int32_t start = 0; start < RidgeStarts(world); ++start) {
        const std::int32_t x =
            static_cast<std::int32_t>(Random16(world) * static_cast<std::uint32_t>(MapSize(world)) - 2) / 65536 + 1;
        const std::int32_t y =
            static_cast<std::int32_t>(Random16(world) * static_cast<std::uint32_t>(MapSize(world) - 3)) / 65536 + 1;
        const std::uint8_t height = Height(world, x, y);
        if (height - 10 <= Height(world, x, y + 1) || height <= 0x1e || height >= 0x64 ||
            GameWorld_IsClearOfType70Nearby(world, nullptr, static_cast<std::uint32_t>(x),
                                            static_cast<std::uint32_t>(y)) == 0) {
            continue;
        }
        Terrain(world, x, y) = kRidge;
        Terrain(world, x, y + 1) = kRidgeLow;
        WalkRidge(world, x, y);
    }
}

// Ridge tiles are raised by 8, low ridge tiles become ridge tiles 8 lower
// (x, y in [1, mapSize-1)).
void MarkRidgeHeights(GameWorld* world) {
    ForEachTile(1, [&] { return MapSize(world) - 1; }, [&](std::int32_t x, std::int32_t y) {
        std::uint8_t& terrain = Terrain(world, x, y);
        std::uint8_t& height = Height(world, x, y);
        if (terrain == kRidge) {
            height = static_cast<std::uint8_t>(height + 8);
        }
        if (terrain == kRidgeLow) {
            terrain = kRidge;
            height = static_cast<std::uint8_t>(height - 8);
        }
    });
}

bool IsRidge(GameWorld* world, std::int32_t x, std::int32_t y) {
    return Terrain(world, x, y) == kRidge;
}

// Repeats until nothing changes: limits the rise from (x, y) to (x, y+1) and
// to (x+1, y+1) to 5, and lets (x, y) drop to at most 7 (23 where the four
// tiles around the pair are ridge) above them. `base` is the height of
// (x, y) before the step.
void SmoothRidgeSlopes(GameWorld* world) {
    bool changed;
    do {
        changed = false;
        ForEachTile(1, [&] { return MapSize(world) - 1; }, [&](std::int32_t x, std::int32_t y) {
            const std::int32_t base = Height(world, x, y);
            const auto limit = [&](std::int32_t nx, std::int32_t ny, bool ridge) {
                std::uint8_t& height = Height(world, nx, ny);
                if (height > base + 5) {
                    height = static_cast<std::uint8_t>(base + 5);
                    changed = true;
                }
                const std::int32_t fall = ridge ? 0x17 : 7;
                if (height < base - fall) {
                    Height(world, x, y) = static_cast<std::uint8_t>(height + fall);
                    changed = true;
                }
            };
            limit(x, y + 1,
                  IsRidge(world, x, y) && IsRidge(world, x, y + 1) && IsRidge(world, x - 1, y) &&
                      IsRidge(world, x + 1, y + 1));
            limit(x + 1, y + 1,
                  IsRidge(world, x, y) && IsRidge(world, x + 1, y + 1) && IsRidge(world, x, y + 1) &&
                      IsRidge(world, x + 1, y));
        });
    } while (changed);
}

// Symmetric maps: repeats until nothing changes: (x, y+1), (x+1, y+1) and
// (x+1, y) may rise at most 5 above (x, y); a neighbour more than 5 below
// lifts (x, y) to 5 above it, except (x+1, y), which lifts itself by 5 (as
// in the original).
void SmoothSlopes(GameWorld* world) {
    bool changed;
    do {
        changed = false;
        ForEachTile(1, [&] { return MapSize(world) - 1; }, [&](std::int32_t x, std::int32_t y) {
            const std::int32_t base = Height(world, x, y);
            const auto limit = [&](std::int32_t nx, std::int32_t ny, std::uint8_t& lifted) {
                std::uint8_t& height = Height(world, nx, ny);
                if (height > base + 5) {
                    height = static_cast<std::uint8_t>(base + 5);
                    changed = true;
                }
                if (height < base - 5) {
                    lifted = static_cast<std::uint8_t>(height + 5);
                    changed = true;
                }
            };
            limit(x, y + 1, Height(world, x, y));
            limit(x + 1, y + 1, Height(world, x, y));
            limit(x + 1, y, Height(world, x + 1, y));
        });
    } while (changed);
}

// --- 8: terrain from heights ------------------------------------------------

// x, y in [1, mapSize-1).
void ClassifyTerrain(GameWorld* world) {
    ForEachTile(1, [&] { return MapSize(world) - 1; }, [&](std::int32_t x, std::int32_t y) {
        std::uint8_t& terrain = Terrain(world, x, y);
        if (terrain == kRidge) {
            return;
        }
        const std::uint8_t height = Height(world, x, y);
        if (height == 0) {
            terrain = 0;
        } else if (height < 0x8c) {
            terrain = 0x10;
        } else {
            terrain = height < 0xbe ? 0x20 : 0x80;
        }
    });
}

void ClearObjectsAndScratch(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) { Object(world, x, y) = 0; });
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) { Scratch(world, x, y) = 0; });
}

// --- 10: streams -------------------------------------------------------------

bool StreamCanEnter(GameWorld* world, std::int32_t x, std::int32_t y) {
    return Terrain(world, x, y) == 0x10 &&
           GameWorld_IsNeighbourhoodByte9Clear(world, nullptr, static_cast<std::uint32_t>(x),
                                               static_cast<std::uint32_t>(y)) != 0;
}

std::int32_t NextHexDirection(std::int32_t direction) {
    return direction + 1 > 6 ? direction - 5 : direction + 1;
}

// Counts the tiles of the 9x9 square around (x, y) for which `test` holds.
template <typename Test>
std::int32_t CountAround(std::int32_t x, std::int32_t y, Test test) {
    std::int32_t count = 0;
    for (std::int32_t ax = x - 4; ax <= x + 4; ++ax) {
        for (std::int32_t ay = y - 4; ay <= y + 4; ++ay) {
            if (test(ax, ay)) {
                ++count;
            }
        }
    }
    return count;
}

// One stream from (x, y). The scratch byte (+0x5) holds, on a finished
// stream, the direction towards its source plus 1 (2..7), 0x0e at a source
// and source+6 where a stream joins another; while a stream is being traced
// it holds 1.
//   A probe on a tile with an existing stream (scratch >= 2) joins it
//   ("joining", minimum length 7); otherwise the tile must be water-free
//   grass (terrain 0) with more than 25 water tiles (terrain < 0x10) in its
//   9x9 square (minimum length 16).
//   The first step goes to the highest enterable neighbour; then each step
//   scores the three directions ahead (the last one, its left and right
//   neighbours; the same direction at most three times in a row) by the
//   slope around the candidate and takes the best positive score.
//   A long enough stream becomes terrain 0x60 with directions in the
//   scratch bytes; a joined stream then raises the terrain of the stream it
//   joined (0x60 -> 0x61 -> 0x62 -> 0x63) down to that stream's source and
//   marks the walked tiles in the object byte (1).
void CarveStream(GameWorld* world, std::int32_t x, std::int32_t y) {
    const std::uint8_t terrain = Terrain(world, x, y);
    if (terrain != 0 && Scratch(world, x, y) < 2) {
        return;
    }
    const bool joining = Scratch(world, x, y) >= 2;
    std::int32_t minimumLength;
    bool tooDry = false;
    if (joining) {
        minimumLength = 7;
    } else {
        minimumLength = 16;
        tooDry = CountAround(x, y, [&](std::int32_t ax, std::int32_t ay) { return Terrain(world, ax, ay) < 0x10; }) <=
                 25;
    }

    std::int32_t best = 0;
    for (std::int32_t d = 1; d <= 6; ++d) {
        std::uint8_t saved = 0;
        if (joining) {
            saved = Scratch(world, x, y);
            Scratch(world, x, y) = 0;
        }
        const std::int32_t nx = x + kDx[d];
        const std::int32_t ny = y + kDy[d];
        if (StreamCanEnter(world, nx, ny) &&
            (best == 0 || Height(world, x + kDx[best], y + kDy[best]) < Height(world, nx, ny))) {
            best = d;
        }
        if (joining) {
            Scratch(world, x, y) = saved;
        }
    }
    if (best == 0 || tooDry) {
        return;
    }
    if (CountAround(x, y, [&](std::int32_t ax, std::int32_t ay) { return Scratch(world, ax, ay) >= 8; }) != 0) {
        return;
    }
    if (!joining) {
        Scratch(world, x, y) = 1;
    }

    // Trace the stream, marking its tiles with 1.
    std::int32_t length = 1;
    std::int32_t lastDirection = 0;
    std::int32_t run = 0;
    std::int32_t bonus = 2;
    std::int32_t cx = x;
    std::int32_t cy = y;
    std::int32_t direction = best;
    for (;;) {
        cx += kDx[direction];
        cy += kDy[direction];
        const std::int32_t first = direction - 1 == 0 ? 6 : direction - 1;
        std::int32_t scores[3] = {0, 0, 0};
        std::int32_t d = first;
        for (std::int32_t& score : scores) {
            const std::int32_t nx = cx + kDx[d];
            const std::int32_t ny = cy + kDy[d];
            if ((run < 3 || d != lastDirection) && StreamCanEnter(world, nx, ny)) {
                const std::int32_t here = Height(world, nx, ny);
                if (Height(world, cx, cy) - here <= 1) {
                    for (std::int32_t e = 1; e <= 6; ++e) {
                        score += Height(world, nx + kDx[e], ny + kDy[e]) - here + bonus;
                    }
                }
            }
            d = NextHexDirection(d);
        }
        std::int32_t next = 0;
        std::int32_t top = 0;
        d = first;
        for (const std::int32_t score : scores) {
            if (score > top) {
                top = score;
                next = d;
            }
            d = NextHexDirection(d);
        }
        ++length;
        Scratch(world, cx, cy) = 1;
        if (next == 0) {
            break;
        }
        bonus = Height(world, cx + kDx[next], cy + kDy[next]) - Height(world, cx, cy) + 1;
        if (bonus > 2) {
            bonus = 2;
        }
        if (lastDirection == next) {
            ++run;
        } else {
            lastDirection = next;
            run = 1;
        }
        direction = next;
    }

    // Walk the traced tiles again from the start: a long enough stream
    // becomes stream terrain, a short one is erased (its start keeps its
    // mark).
    std::int32_t px = x;
    std::int32_t py = y;
    std::int32_t from = 0;
    for (;;) {
        if (length >= minimumLength) {
            Terrain(world, px, py) = kStream;
            std::uint8_t& scratch = Scratch(world, px, py);
            if (from != 0) {
                const std::int32_t back = from + 3 > 6 ? from - 3 : from + 3;
                scratch = static_cast<std::uint8_t>(back + 1);
            } else if (!joining) {
                scratch = 0x0e;
            } else {
                scratch = static_cast<std::uint8_t>(scratch + 6);
            }
        } else if (from != 0) {
            Scratch(world, px, py) = 0;
        }
        std::int32_t next = 0;
        for (std::int32_t e = 1; e <= 6; ++e) {
            if (Scratch(world, px + kDx[e], py + kDy[e]) == 1) {
                next = e;
            }
        }
        if (next == 0) {
            break;
        }
        px += kDx[next];
        py += kDy[next];
        from = next;
    }

    if (!joining || length < minimumLength) {
        return;
    }
    // Follow the joined stream back to its source, raising its terrain.
    std::int32_t qx = x;
    std::int32_t qy = y;
    for (;;) {
        std::uint8_t& here = Terrain(world, qx, qy);
        if (here == 0x62) {
            here = 0x63;
        }
        if (here == 0x61) {
            here = 0x62;
        }
        if (here == 0x60) {
            here = 0x61;
        }
        const std::int32_t scratch = Scratch(world, qx, qy);
        if (scratch < 2 || scratch > 0x0e) {
            Object(world, qx, qy) = 1;
        }
        if (scratch < 2 || scratch >= 0x0e) {
            break;
        }
        const std::int32_t back = scratch - 1 > 6 ? scratch - 7 : scratch - 1;
        qx += kDx[back];
        qy += kDy[back];
    }
}

// 4*mapSize^2 probes at positions p = 0, 151, 302, ... (mod mapSize^2),
// tile (p % mapSize, p / mapSize); probes 8 tiles or more from the border
// start a stream with probability 2000/65536.
void CarveStreams(GameWorld* world) {
    std::int32_t position = 0;
    for (std::int32_t probe = 0; probe < MapSize(world) * MapSize(world) * 4; ++probe) {
        const std::int32_t size = MapSize(world);
        const std::int32_t area = size * size;
        const std::int32_t y = position / size;
        const std::int32_t x = position - size * y;
        position += 0x97;
        if (position >= area) {
            position -= area;
        }
        if (x < 8 || x > size - 8 || y < 8 || y > size - 8 || Random16(world) >= 2000) {
            continue;
        }
        CarveStream(world, x, y);
    }
}

// A stream tile next to anything but stream, grass (0x10), 0x30 or water
// (0) on the map (offset table entries 1..6) turns back into grass.
void RemoveStrayStreamTiles(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) {
        std::uint8_t& terrain = Terrain(world, x, y);
        if ((terrain & 0xf0) != kStream) {
            return;
        }
        bool foreign = false;
        for (std::uint32_t entry = 1; entry <= 6; ++entry) {
            const std::int32_t nx = x + world->offsets[entry].dx;
            const std::int32_t ny = y + world->offsets[entry].dy;
            if (GameWorld_IsOnMap(world, nullptr, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)) ==
                0) {
                continue;
            }
            const std::uint8_t other = Terrain(world, nx, ny);
            if (other != 0x60 && other != 0x61 && other != 0x62 && other != 0x63 && other != 0x10 && other != 0x30 &&
                other != 0) {
                foreign = true;
            }
        }
        if (foreign) {
            terrain = 0x10;
        }
    });
}

// --- 11, 12: fixed terrain passes and objects --------------------------------

void ReplaceNextTo(GameWorld* world, std::uint32_t terrain, std::uint32_t neighbour, std::uint32_t replacement) {
    GameWorld_ReplaceTerrainNextTo(world, nullptr, terrain, neighbour, replacement);
}
void Replace(GameWorld* world, std::uint32_t from, std::uint32_t to) {
    GameWorld_ReplaceTerrainType(world, nullptr, from, to);
}
void Expand(GameWorld* world, std::uint32_t terrain, std::uint32_t into, int times) {
    for (int i = 0; i < times; ++i) {
        GameWorld_ExpandTerrain(world, nullptr, terrain, into);
    }
}
void Sprinkle(GameWorld* world, std::uint32_t into, std::uint32_t terrain, std::uint32_t density) {
    GameWorld_ScatterTerrainNear(world, nullptr, into, terrain, density);
}
void Grow(GameWorld* world, std::uint32_t terrain, std::uint32_t into) {
    for (const std::uint32_t percent : {0x50u, 0x3cu, 0x28u, 0x14u}) {
        GameWorld_GrowTerrain(world, nullptr, terrain, into, percent);
    }
}

// Water depth rings, beaches and the four grown landscapes (0x40, 0x50,
// 0x90, 0x18), each: edge marks, sprinkled seeds, growth steps, the edge
// marks turned into grass, random growth, transition rings.
void ShapeTerrain(GameWorld* world) {
    ReplaceNextTo(world, 0x00, 0x10, 0xff);
    ReplaceNextTo(world, 0x10, 0xff, 0xfe);
    Replace(world, 0xff, 0x30);
    Replace(world, 0xfe, 0x30);
    ReplaceNextTo(world, 0x20, 0x10, 0x11);
    ReplaceNextTo(world, 0x20, 0x11, 0x21);
    ReplaceNextTo(world, 0x80, 0x20, 0x23);
    ReplaceNextTo(world, 0x80, 0x23, 0x81);
    ReplaceNextTo(world, 0x00, 0x30, 0xff);
    ReplaceNextTo(world, 0x00, 0xff, 0x01);
    for (std::uint32_t depth = 1; depth <= 5; ++depth) {
        ReplaceNextTo(world, 0x00, depth, depth + 1);
    }
    Replace(world, 0x00, 0x07);
    Replace(world, 0xff, 0x00);

    GameWorld_MarkTerrainEdge(world, nullptr, 0x10);
    Sprinkle(world, 0x10, 0x40, 2);
    Expand(world, 0x40, 0x10, 5);
    Sprinkle(world, 0x10, 0x40, 1);
    Expand(world, 0x40, 0x10, 6);
    GameWorld_ReplaceTerrainF3(world, nullptr, 0x10);
    Grow(world, 0x40, 0x10);
    ReplaceNextTo(world, 0x40, 0x10, 0x14);
    ReplaceNextTo(world, 0x40, 0x14, 0x41);

    GameWorld_MarkTerrainEdge(world, nullptr, 0x10);
    Sprinkle(world, 0x10, 0x50, 1);
    Expand(world, 0x50, 0x10, 2);
    Sprinkle(world, 0x10, 0x50, 1);
    Expand(world, 0x50, 0x10, 2);
    Sprinkle(world, 0x10, 0x50, 1);
    Expand(world, 0x50, 0x10, 1);
    GameWorld_ReplaceTerrainF3(world, nullptr, 0x10);
    Grow(world, 0x50, 0x10);
    ReplaceNextTo(world, 0x50, 0x10, 0x15);
    ReplaceNextTo(world, 0x50, 0x15, 0x51);

    GameWorld_MarkTerrainEdge(world, nullptr, 0x10);
    Sprinkle(world, 0x10, 0x90, 1);
    Expand(world, 0x90, 0x10, 3);
    Sprinkle(world, 0x10, 0x90, 1);
    Expand(world, 0x90, 0x10, 3);
    GameWorld_ReplaceTerrainF3(world, nullptr, 0x10);
    Grow(world, 0x90, 0x10);
    ReplaceNextTo(world, 0x90, 0x10, 0x17);
    ReplaceNextTo(world, 0x90, 0x17, 0x91);

    GameWorld_MarkTerrainEdge(world, nullptr, 0x10);
    Sprinkle(world, 0x10, 0x18, 3);
    Expand(world, 0x18, 0x10, 2);
    Sprinkle(world, 0x10, 0x18, 2);
    Expand(world, 0x18, 0x10, 2);
    Sprinkle(world, 0x10, 0x18, 1);
    Expand(world, 0x18, 0x10, 3);
    GameWorld_ReplaceTerrainF3(world, nullptr, 0x10);
    Grow(world, 0x18, 0x10);

    const std::uint32_t symmetry = Symmetry(world);
    if (symmetry != 0) {
        GameWorld_SetDiagonalTileByteB(world, nullptr, symmetry);
    }
    Sprinkle(world, 0x10, 0x12, 2);
    Sprinkle(world, 0x10, 0x13, 2);
    Sprinkle(world, 0x20, 0x22, 2);
}

struct ObjectRule {
    std::uint8_t required;
    std::uint8_t object;
    std::uint8_t density;
    std::uint8_t blockMode;
    std::uint8_t extra;
};

// ScatterObjects calls, in the original's order (required terrain, object,
// density, block mode, extra).
constexpr ObjectRule kObjectsBeforeGroups[] = {
    {0x10, 0x01, 1, 2, 1},   {0x10, 0x02, 1, 0, 0},   {0x10, 0x03, 1, 0, 0},   {0x10, 0x04, 1, 0, 0},
    {0x10, 0x05, 1, 0, 0},   {0x10, 0x06, 1, 0, 0},   {0x10, 0x07, 1, 0, 0},   {0x10, 0x08, 1, 0, 0},
    {0x10, 0x09, 1, 0, 0},   {0x10, 0x0a, 1, 0, 0},   {0x10, 0x0b, 1, 0, 0},   {0x10, 0x0c, 1, 0, 0},
    {0x10, 0x0d, 1, 2, 0},   {0x10, 0x0e, 1, 2, 0},   {0x10, 0x0f, 1, 2, 0},   {0x10, 0x10, 1, 2, 0},
    {0x10, 0x11, 1, 2, 0},   {0x10, 0x12, 1, 2, 0},   {0x10, 0x13, 1, 2, 0},   {0x10, 0x14, 1, 2, 0},
    {0x30, 0x1d, 5, 2, 0},   {0x30, 0x1e, 5, 2, 0},   {0x30, 0x1f, 5, 0, 0},   {0x30, 0x20, 5, 2, 0},
    {0x30, 0x21, 5, 2, 0},   {0x10, 0x22, 1, 2, 1},   {0x10, 0x15, 1, 0, 0},   {0x10, 0x16, 1, 0, 0},
    {0x10, 0x17, 1, 0, 0},   {0x10, 0x18, 1, 0, 0},   {0x10, 0x19, 1, 0, 0},   {0x10, 0x1a, 1, 0, 0},
    {0x10, 0x1b, 1, 0, 0},   {0x10, 0x1c, 1, 0, 0},   {0x10, 0x23, 1, 0, 0},   {0x10, 0x24, 1, 0, 0},
    {0x10, 0x25, 1, 0, 0},   {0x10, 0x26, 1, 0, 0},   {0x10, 0x27, 1, 0, 0},   {0x10, 0x28, 1, 0, 0},
    {0x10, 0x29, 1, 0, 0},   {0x10, 0x2a, 1, 0, 0},   {0x40, 0x2b, 3, 1, 2},   {0x40, 0x2c, 3, 1, 2},
    {0x40, 0x2d, 6, 1, 1},   {0x40, 0x2e, 6, 1, 1},   {0x40, 0x2f, 6, 1, 1},   {0x40, 0x30, 6, 0, 0},
    {0x40, 0x31, 3, 0, 0},   {0x10, 0x32, 1, 0, 0},   {0x10, 0x33, 1, 0, 0},   {0x10, 0x34, 1, 0, 0},
    {0x10, 0x35, 1, 0, 0},   {0x10, 0x36, 1, 0, 0},   {0x10, 0x37, 1, 0, 0},   {0x10, 0x38, 1, 0, 0},
    {0x10, 0x39, 1, 0, 0},   {0x10, 0x3a, 1, 0, 0},   {0x10, 0x3b, 1, 0, 0},   {0x10, 0x3c, 1, 0, 0},
    {0x10, 0x3d, 1, 0, 0},   {0x50, 0x3e, 150, 0, 0}, {0x50, 0x3f, 150, 0, 0}, {0x50, 0x40, 150, 0, 0},
    {0x50, 0x41, 150, 0, 0}, {0x50, 0x42, 150, 0, 0}, {0x50, 0x43, 150, 0, 0},
};

struct GroupRule {
    std::uint8_t required;
    std::uint8_t low;
    std::uint8_t high;
    std::uint8_t density;
    std::uint8_t blockMode;
    std::uint8_t extra;
};

// ScatterObjectGroups calls: forests (0x44..0x51) and stone fields
// (0x73..0x7e).
constexpr GroupRule kGroups[] = {
    {0x10, 0x44, 0x45, 11, 1, 2}, {0x10, 0x46, 0x47, 11, 1, 2}, {0x10, 0x48, 0x49, 11, 1, 2},
    {0x10, 0x4a, 0x4b, 11, 1, 2}, {0x10, 0x4c, 0x4d, 11, 1, 2}, {0x40, 0x4e, 0x4f, 11, 1, 2},
    {0x10, 0x50, 0x51, 11, 1, 2}, {0x10, 0x73, 0x7e, 55, 2, 1},
};

constexpr ObjectRule kObjectsAfterGroups[] = {
    {0x10, 0x44, 1, 1, 2}, {0x10, 0x45, 1, 1, 2}, {0x10, 0x46, 1, 1, 2}, {0x10, 0x47, 1, 1, 2},
    {0x10, 0x48, 1, 1, 2}, {0x10, 0x49, 1, 1, 2}, {0x10, 0x4a, 1, 1, 2}, {0x10, 0x4b, 1, 1, 2},
    {0x10, 0x4c, 1, 1, 2}, {0x10, 0x4d, 1, 1, 2}, {0x40, 0x4e, 1, 1, 2}, {0x40, 0x4f, 1, 1, 2},
    {0x10, 0x50, 9, 1, 2}, {0x10, 0x51, 9, 1, 2}, {0x10, 0x73, 1, 2, 1}, {0x10, 0x74, 1, 2, 1},
    {0x10, 0x75, 1, 2, 1}, {0x10, 0x76, 1, 2, 1}, {0x10, 0x77, 1, 2, 1}, {0x10, 0x78, 1, 2, 1},
    {0x10, 0x79, 1, 2, 1}, {0x10, 0x7a, 1, 2, 1}, {0x10, 0x7b, 1, 2, 1}, {0x10, 0x7c, 1, 2, 1},
    {0x10, 0x7d, 1, 2, 1}, {0x10, 0x7e, 1, 2, 1}, {0x10, 0x7f, 1, 0, 0},
};

// ScatterTileByte11 calls (base, density): resources 0x50.., 0x40.., ...
constexpr std::uint16_t kResources[][2] = {{0x50, 30}, {0x40, 20}, {0x30, 60}, {0x20, 100}, {0x10, 300}};

void PlaceObjects(GameWorld* world) {
    for (const ObjectRule& rule : kObjectsBeforeGroups) {
        GameWorld_ScatterObjects(world, nullptr, rule.required, rule.object, rule.density, rule.blockMode, rule.extra);
    }
    for (const GroupRule& rule : kGroups) {
        GameWorld_ScatterObjectGroups(world, nullptr, rule.required, rule.low, rule.high, rule.density,
                                      rule.blockMode, rule.extra);
    }
    for (const ObjectRule& rule : kObjectsAfterGroups) {
        GameWorld_ScatterObjects(world, nullptr, rule.required, rule.object, rule.density, rule.blockMode, rule.extra);
    }
    for (const auto& resource : kResources) {
        GameWorld_ScatterTileByte11(world, nullptr, resource[0], resource[1]);
    }
}

// --- 13..16: finishing -------------------------------------------------------

// Water tiles (terrain class 0) of [0, mapSize-1)^2: with probability
// (Random > 40000) a random amount 0..15 (probably fish).
void ScatterFish(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world) - 1; }, [&](std::int32_t x, std::int32_t y) {
        if ((Terrain(world, x, y) & 0xf0) != 0 || Random16(world) <= 40000) {
            return;
        }
        TileAt(world, x, y).resources = static_cast<std::uint8_t>(Random16(world) & 0xf);
    });
}

// The fields a symmetric copy carries: height, terrain, object, flags and
// resources.
void CopyTileFields(Tile& to, const Tile& from) {
    to.height = from.height;
    to.terrain = from.terrain;
    to.object = from.object;
    to.flags = from.flags;
    to.resources = from.resources;
}

// Bit 1: tile (j, i) is copied to (n-1-i, n-1-j) for i in [0, n-1), j in
// [0, n-1-i) (in that order, in place). Bit 0: (j, i) to (i, j) for i in
// [1, n), j in [0, i).
void ApplySymmetry(GameWorld* world) {
    // The original tests the low byte of the symmetry field.
    if ((static_cast<std::uint8_t>(Symmetry(world)) & 2) != 0) {
        for (std::int32_t i = 0; i < MapSize(world) - 1; ++i) {
            for (std::int32_t j = 0; j < MapSize(world) - i - 1; ++j) {
                const std::int32_t size = MapSize(world);
                CopyTileFields(TileAt(world, size - 1 - i, size - 1 - j), TileAt(world, j, i));
            }
        }
    }
    if ((static_cast<std::uint8_t>(Symmetry(world)) & 1) != 0) {
        for (std::int32_t i = 1; i < MapSize(world); ++i) {
            for (std::int32_t j = 0; j < i; ++j) {
                CopyTileFields(TileAt(world, i, j), TileAt(world, j, i));
            }
        }
    }
}

void FinishTiles(GameWorld* world) {
    ForEachTile(0, [&] { return MapSize(world); }, [&](std::int32_t x, std::int32_t y) {
        Tile& tile = TileAt(world, x, y);
        tile.seen_object = tile.object;
        const std::uint8_t kind = tile.terrain & 0xf0;
        if (kind == 0x00 || kind == 0x50 || kind == 0x70 || kind == 0x80) {
            tile.flags |= layout::kTileBlocked;
        }
    });
}

void SeedRandomThroughGame(GameWorld* world, std::uint32_t seed) {
    using SeedFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t seed);
    reinterpret_cast<SeedFn>(0x004feb40)(world, nullptr, seed);
}

}  // namespace

using CreateFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t seed, std::int32_t mapSize,
                                            std::uint32_t symmetry);

// Purpose: Generates a random map ("random world" in the game setup): heights, mountains, water, rivers,
// landscapes, trees, stones and resources, optionally mirrored for fair multiplayer starts, then the
// regions and areas.
std::uint32_t __fastcall GameWorld_Create(GameWorld* world, void*, std::uint32_t seed, std::int32_t mapSize,
                                          std::uint32_t symmetry) {
    GameDebugLog(kCreatingWorld);
    SeedRandomThroughGame(world, seed);
    world->unk2d274 = static_cast<std::int32_t>(symmetry);
    world->map_size = static_cast<std::uint32_t>(mapSize);
    world->camera_drawn_x = 0;
    world->camera_drawn_y = 0;
    if (mapSize > 0) {
        ResetTiles(world);
    }

    SeedCoarseHeights(world);
    RefineHeights(world);
    LowerHeights(world);
    LogBorderErrors(world);

    if (Symmetry(world) == 0) {
        RaiseRidges(world);
        MarkRidgeHeights(world);
    }
    if (Symmetry(world) == 0) {
        SmoothRidgeSlopes(world);
    } else {
        SmoothSlopes(world);
    }
    ClassifyTerrain(world);
    ClearObjectsAndScratch(world);

    CarveStreams(world);
    RemoveStrayStreamTiles(world);

    ShapeTerrain(world);
    PlaceObjects(world);
    ScatterFish(world);
    const std::uint32_t symmetryAfter = Symmetry(world);
    if (symmetryAfter != 0) {
        GameWorld_ClearDiagonalTileByteB(world, nullptr, symmetryAfter);
    }
    ApplySymmetry(world);
    FinishTiles(world);
    LogBorderErrors(world);
    return GameWorld_BuildRegionsAndAreas(world, nullptr);
}

S3RE_HOOK(0x005166d0, GameWorld_Create, CreateFn, Original_GameWorld_Create, GameWorld_Create);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorldSize = sizeof(GameWorld);
// The three id tables with their marks (walk_region_high_water up to the
// area records; includes area_high_water).
constexpr std::size_t kIdTables = offsetof(GameWorld, walk_region_high_water);
constexpr std::size_t kIdTablesEnd = offsetof(GameWorld, areas);
constexpr std::uint32_t kAreaCount = 4096;

// The writable sections of S3.EXE (its globals), for checking that Create
// writes none of them.
std::vector<std::uint8_t> SnapshotGlobals() {
    std::vector<std::uint8_t> bytes;
    auto* base = reinterpret_cast<std::uint8_t*>(0x400000);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
            continue;
        }
        const std::uint8_t* start = base + section->VirtualAddress;
        bytes.insert(bytes.end(), start, start + section->Misc.VirtualSize);
    }
    return bytes;
}

void BuildOffsetTable(GameWorld* world) {
    using BuildFn = void(__fastcall*)(GameWorld* world, void* edx);
    reinterpret_cast<BuildFn>(0x00516530)(world, nullptr);
}

// The state a freshly constructed world has where Create and its callees
// depend on it: the offset table, empty tile ring, empty id tables, free
// areas. With `zeroed` everything else is zero as well (as after the world
// constructor), otherwise it keeps the random fill (garbage outside the map
// and in the tile fields Create doesn't write).
void PrepareWorld(test::WorldPair& worlds, bool zeroed) {
    worlds.Both([&](GameWorld* w) {
        auto* bytes = reinterpret_cast<std::uint8_t*>(w);  // raw-ok: whole test buffer
        if (zeroed) {
            std::memset(bytes, 0, kWorldSize);
        }
        w->tile_ring_index = {};      // tile ring read/write index
        w->pending_event_index = {};  // pending-event ring read/write index
        w->event_index.write = 0;     // event ring write index
        std::memset(bytes + kIdTables, 0, kIdTablesEnd - kIdTables);
        for (std::uint32_t area = 0; area < kAreaCount; ++area) {
            w->areas[area].player = 0xff;
        }
    });
    BuildOffsetTable(worlds.original());
    BuildOffsetTable(worlds.ours());
}

struct CreateCase {
    std::uint32_t seed;
    std::int32_t size;
    std::uint32_t symmetry;
};

// Runs Create on both worlds for every case (each on a freshly prepared
// world) and compares EAX, the whole world, the DebugLog calls and the
// S3.EXE globals.
void CompareCreate(test::Context& ctx, CreateFn candidate, const CreateCase* cases, int count, bool zeroed) {
    test::WorldPair worlds(ctx, kWorldSize);
    for (int i = 0; i < count; ++i) {
        const CreateCase& c = cases[i];
        PrepareWorld(worlds, zeroed);
        DebugLogRecorder log;
        const std::vector<std::uint8_t> before = SnapshotGlobals();
        const std::uint32_t expected = Original_GameWorld_Create(worlds.original(), nullptr, c.seed, c.size, c.symmetry);
        const std::vector<DebugLogCall> expectedLog = log.Take();
        const std::vector<std::uint8_t> afterOriginal = SnapshotGlobals();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, c.seed, c.size, c.symmetry);
        const std::vector<DebugLogCall> actualLog = log.Take();
        const std::vector<std::uint8_t> afterOurs = SnapshotGlobals();
        bool same = S3RE_EXPECT_EQ(expected, actual) && worlds.Checkpoint(ctx);
        same = S3RE_EXPECT_EQ(expectedLog.size(), actualLog.size()) && same;
        for (std::size_t k = 0; same && k < expectedLog.size(); ++k) {
            same = S3RE_EXPECT(expectedLog[k] == actualLog[k]);
        }
        same = S3RE_EXPECT(before == afterOriginal) && S3RE_EXPECT(afterOriginal == afterOurs) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: seed %u, size %d, symmetry %u, %s world", i, c.seed, c.size,
                     c.symmetry, zeroed ? "zeroed" : "random");
            return;
        }
    }
}

// Map sizes of the game's random maps are multiples of 64 (probably 128 ..
// 768); odd sizes and the symmetry bits cover the remaining paths.
constexpr CreateCase kRealCases[] = {
    {1234, 384, 0}, {7, 256, 1}, {65535, 256, 2}, {42, 192, 3}, {31337, 768, 0}, {99, 128, 0},
};
constexpr CreateCase kOddCases[] = {
    {5, 100, 0}, {6, 64, 3}, {77, 200, 2}, {123, 13, 0}, {9, 0, 0}, {11, 70, 1},
};

// Wrong: one tile's height off by one.
std::uint32_t __fastcall WrongCreateHeight(GameWorld* world, void* edx, std::uint32_t seed, std::int32_t mapSize,
                                           std::uint32_t symmetry) {
    const std::uint32_t result = GameWorld_Create(world, edx, seed, mapSize, symmetry);
    Height(world, mapSize / 2, mapSize / 3) ^= 1;
    return result;
}

// Wrong: an extra DebugLog call.
std::uint32_t __fastcall WrongCreateLog(GameWorld* world, void* edx, std::uint32_t seed, std::int32_t mapSize,
                                        std::uint32_t symmetry) {
    GameDebugLog(kErrorTop, 0, 0, 0u);
    return GameWorld_Create(world, edx, seed, mapSize, symmetry);
}

}  // namespace

S3RE_TEST(GameWorld_Create_MatchesOriginal) {
    CompareCreate(ctx, &GameWorld_Create, kRealCases, static_cast<int>(std::size(kRealCases)), /*zeroed=*/true);
}
// Odd sizes on a zeroed world; on a world full of garbage only real sizes
// (with odd sizes the height refinement writes past the map, and a ridge
// walk may then wander over garbage heights out of the tile array).
S3RE_TEST(GameWorld_Create_OddSizes_MatchesOriginal) {
    CompareCreate(ctx, &GameWorld_Create, kOddCases, static_cast<int>(std::size(kOddCases)), /*zeroed=*/true);
}
S3RE_TEST(GameWorld_Create_GarbageWorld_MatchesOriginal) {
    CompareCreate(ctx, &GameWorld_Create, kRealCases, 4, /*zeroed=*/false);
}
S3RE_TEST(GameWorld_Create_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCreate(probe, &WrongCreateHeight, kRealCases + 5, 1, true);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probeLog(ctx.name(), 1, /*quiet=*/true);
    CompareCreate(probeLog, &WrongCreateLog, kRealCases + 5, 1, true);
    S3RE_EXPECT(probeLog.failures() > 0);
}

}  // namespace s3re::game
