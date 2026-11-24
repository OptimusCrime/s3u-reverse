// Whether a computer player already plans a building near a position
// (computer players A2, session 53; Rainer\compGlobs.cpp;
// re/computer_players.md). The distance variant of AI_HasSiteNoteInArea
// 0x571d00 (ai_site_queries.cpp).
//
//   0x00571c40 AI::HasSiteNoteNear(type, x, y, distance) fastcall (CX = the
//              building type, EDX = x; stack y, distance), ret 8: an
//              AI::WrappedPos on the AI's map size (constructor 0x597090
//              with x, y, g_aiMapSizeX, g_aiMapSizeY) measures the
//              distance; over the current computer player's 300 notes
//              (g_aiPlayer): a build-house note (type 4) of the same
//              building type, or a ground request (type 2) for another
//              type, whose position (+0x1e / +0x20) is at most `distance`
//              away (WrappedPos::Distance 0x5971f0, AX; 16-bit unsigned
//              compare with the distance's low word) -> 1. Else 0.
//              Callers MakeBuildOrders 0x56cd90 / 0x57ce90 and
//              T_SAFETY_AGENT 0x5868a0.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kConstructWrappedPos = 0x00597090;  // AI::WrappedPos::WrappedPos(x, y, w, h)
constexpr std::uintptr_t kDistance = 0x005971f0;             // AI::WrappedPos::Distance(x, y)

constexpr std::uint16_t kGroundRequestNote = 2;  // HOUSE_GROUND_REQUEST_NOTE
constexpr std::uint16_t kBuildHouseNote = 4;     // T_DO_BUILD_HOUSE_NOTE

using ConstructFn = WrappedPos*(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t width, std::uint32_t height);
using DistanceFn = std::uint32_t(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y);

}  // namespace

using HasNoteNearFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t distance);

std::uint32_t __fastcall AI_HasSiteNoteNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t distance);

S3RE_HOOK(0x00571c40, AI_HasSiteNoteNear, HasNoteNearFn, Original_AI_HasSiteNoteNear, AI_HasSiteNoteNear);

// Purpose: Whether the computer player already has a building order for
// this type (or a ground request for another type) near a position, so that
// it doesn't plan two sites close together.
std::uint32_t __fastcall AI_HasSiteNoteNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t distance) {
    WrappedPos position;  // (the constructor sets every field)
    reinterpret_cast<ConstructFn>(kConstructWrappedPos)(&position, nullptr, x, y, g_aiMapSizeX, g_aiMapSizeY);
    const auto buildingType = static_cast<std::uint16_t>(type);
    const auto limit = static_cast<std::uint16_t>(distance);
    for (std::size_t id = 0; id < kMaxNotes; ++id) {
        const auto* note = reinterpret_cast<const RequestNote*>(g_aiPlayer.get()->notes[id]);
        if (note == nullptr) {
            continue;
        }
        const bool sameType = note->building_type == buildingType;
        const bool candidate = (note->base.type == kBuildHouseNote && sameType) ||
                               (note->base.type == kGroundRequestNote && !sameType);
        if (candidate && static_cast<std::uint16_t>(reinterpret_cast<DistanceFn>(kDistance)(
                             &position, nullptr, note->x, note->y)) <= limit) {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A computer player with 300 note slots: some empty, the others of types
// 0..5 with building types 0..3 and positions on a map of 20..200 tiles;
// random positions, types and distances (with random upper halves where
// only the low word counts).

namespace {

struct TestNote {
    RequestNote note;
    std::uint8_t rest[0x30];
};

void CompareHasSiteNoteNear(test::Context& ctx, HasNoteNearFn candidate, int cases) {
    ComputerPlayer* savedPlayer = g_aiPlayer;
    const std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    std::vector<std::uint8_t> playerBytes(sizeof(ComputerPlayer));
    auto* player = reinterpret_cast<ComputerPlayer*>(playerBytes.data());
    std::vector<TestNote> notes(kMaxNotes);
    g_aiPlayer = player;
    int hits = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const auto size = static_cast<std::uint16_t>(20 + rng.Below(181));
        g_aiMapSizeX = size;
        g_aiMapSizeY = static_cast<std::uint16_t>(rng.Chance(1, 2) ? size : 20 + rng.Below(181));
        const std::uint32_t used = rng.Below(30);  // percent of the slots in use
        for (std::size_t id = 0; id < kMaxNotes; ++id) {
            if (rng.Below(100) >= used) {
                player->notes[id] = nullptr;
                continue;
            }
            RequestNote& note = notes[id].note;
            note.base.type = static_cast<std::uint16_t>(rng.Below(6));
            note.building_type = static_cast<std::uint16_t>(rng.Below(4));
            note.x = static_cast<std::uint16_t>(rng.Below(size));
            note.y = static_cast<std::uint16_t>(rng.Below(size));
            player->notes[id] = &note.base;
        }
        const std::uint32_t type = rng.Below(4) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t x = rng.Below(size);
        const std::uint32_t y = rng.Below(size);
        const std::uint32_t distance = rng.Below(30) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t ra = Original_AI_HasSiteNoteNear(type, x, y, distance);
        const std::uint32_t rb = candidate(type, x, y, distance);
        hits += ra != 0 ? 1 : 0;
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, type %08x, (%u, %u), distance %08x", i, type, x, y, distance);
        }
    }
    g_aiPlayer = savedPlayer;
    g_aiMapSizeX = sizeX;
    g_aiMapSizeY = sizeY;
    if (cases >= 1000 && !S3RE_EXPECT(hits > cases / 10 && hits < cases * 9 / 10)) {
        ctx.Fail(__FILE__, __LINE__, "  %d of %d cases found a note", hits, cases);
    }
}

std::uint32_t __fastcall WrongHasSiteNoteNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t distance) {
    // Wrong: "closer than" instead of "at most".
    return distance == 0 ? 0 : AI_HasSiteNoteNear(type, x, y, distance - 1);
}

}  // namespace

S3RE_TEST(AiHasSiteNoteNear_MatchesOriginal) {
    CompareHasSiteNoteNear(ctx, &AI_HasSiteNoteNear, 3000);
}

S3RE_TEST(AiHasSiteNoteNear_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareHasSiteNoteNear(quiet, &WrongHasSiteNoteNear, 3000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
