// Slot functions of the request agents and the COMPUTER_PLAYER_AGENT
// (computer players, session 55; re/computer_players.md "Requests";
// agent_slot in ai_layout.h).
//
//   0x005656a0 T_DO_BUILD_HOUSE_AGENT::NoteReported(parent, child, result,
//              value), slot 9, ret 0x10: CComputerPlayer::ActivateNoteOf
//              (g_aiPlayer, parent); then parent->state = 2 if the (word)
//              result is 100, else 8 if bit 0 of value is set, else 4.
//   0x00578920 HOUSE_REQUEST_AGENT::NoteReported(parent, child, result,
//              value), slot 9, ret 0x10: by the child's type: a ware
//              request (0) that succeeded (result word != 0) sets bit 1 of
//              parent->state, a ground request (1) bit 0 and stores value
//              in parent->site; both count in parent->reports. After two
//              reports: BLACK_BOARD_NOTE::SetFlags(parent, 8) and
//              ReportToRequester(parent, 100, site) if both succeeded
//              (state 3), else (0, state).
//   0x00576530 HOUSE_GROUND_REQUEST_AGENT::WorkOnNote(note), slot 4, ret 4:
//              up to g_aiPlayerSetup->positions_per_turn spiral positions
//              (re-read every step): unless skip_check (cleared instead),
//              a position on the player's ground (AI::TileOwner, AX) that
//              HOUSE_GROUND_REQUEST_NOTE::CheckAndImproveSite 0x575df0
//              accepts ends the search: found = 1, DeactivateNoteOf,
//              SetFlags(8), ReportToRequester(100, x | y << 16, both signed
//              words added). Otherwise NextSpiralPosition 0x5763a0; when it
//              runs out: SetFlags(8), ReportToRequester(0, 0). AX = 100.
//   0x005846a0 COMPUTER_PLAYER_AGENT::Run(), slot 8: ++runs (word); when it
//              exceeds g_aiPlayerSetup->small_tower_interval * 14 +
//              g_aiPlayerNumber (signed): runs = 0 and
//              AI::RecordBestCandidate(small tower, 20000, -1, -1). EAX:
//              that call's, else the limit.
//
// The slot 9 results only reach the upper halves of the slot 4 results
// (the reporters overwrite AX), which CComputerPlayer::WorkOnNote ignores;
// so EAX of NoteReported is not used (the emulator specs leave it out).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kActivateNoteOf = 0x005515c0;     // CComputerPlayer::ActivateNoteOf (thiscall)
constexpr std::uintptr_t kDeactivateNoteOf = 0x00551610;   // CComputerPlayer::DeactivateNoteOf (thiscall)
constexpr std::uintptr_t kSetFlags = 0x00582440;           // BLACK_BOARD_NOTE::SetFlags (thiscall)
constexpr std::uintptr_t kReportToRequester = 0x00582480;  // BLACK_BOARD_NOTE::ReportToRequester (thiscall)
constexpr std::uintptr_t kTileOwner = 0x00599570;          // AI::TileOwner (fastcall, pointers)
constexpr std::uintptr_t kCheckAndImproveSite = 0x00575df0;  // HOUSE_GROUND_REQUEST_NOTE::CheckAndImproveSite
constexpr std::uintptr_t kNextSpiralPosition = 0x005763a0;   // HOUSE_GROUND_REQUEST_NOTE::NextSpiralPosition
constexpr std::uintptr_t kRecordBestCandidate = 0x0058ec50;  // AI::RecordBestCandidate (fastcall)

constexpr std::uint16_t kSucceeded = 100;   // the result of a successful request
constexpr std::uint16_t kNoteFlagReported = 8;
// T_DO_BUILD_HOUSE_NOTE states (RequestNote::state).
constexpr std::uint16_t kBuildPlaced = 2;
constexpr std::uint16_t kBuildNoSite = 4;
constexpr std::uint16_t kBuildSiteFound = 8;
// HOUSE_REQUEST_NOTE state bits and the reports it waits for.
constexpr std::uint16_t kGroundOk = 1;
constexpr std::uint16_t kWareOk = 2;
constexpr std::uint16_t kBothOk = kGroundOk | kWareOk;
constexpr std::uint16_t kExpectedReports = 2;
// COMPUTER_PLAYER_AGENT.
constexpr std::int32_t kRunsPerInterval = 14;
constexpr std::uint32_t kSmallTowerPriority = 20000;
constexpr std::uint32_t kAnyPlace = 0xffffffff;

void ActivateNote(BlackBoardNote* note) {
    using Fn = std::uint32_t(__fastcall*)(ComputerPlayer* player, void* edx, BlackBoardNote* note);
    reinterpret_cast<Fn>(kActivateNoteOf)(g_aiPlayer, nullptr, note);
}

void SetFlags(BlackBoardNote* note, std::uint32_t bits) {
    using Fn = std::uint32_t(__fastcall*)(BlackBoardNote* note, void* edx, std::uint32_t bits);
    reinterpret_cast<Fn>(kSetFlags)(note, nullptr, bits);
}

std::uint32_t Report(BlackBoardNote* note, std::uint32_t result, std::uint32_t value) {
    using Fn = std::uint32_t(__fastcall*)(BlackBoardNote* note, void* edx, std::uint32_t result, std::uint32_t value);
    return reinterpret_cast<Fn>(kReportToRequester)(note, nullptr, result, value);
}

std::uint32_t CallNote(std::uintptr_t function, HouseGroundRequestNote* note) {
    using Fn = std::uint32_t(__fastcall*)(HouseGroundRequestNote* note, void* edx);
    return reinterpret_cast<Fn>(function)(note, nullptr);
}

}  // namespace

using ReportedFn = void(__fastcall*)(void* self, void* edx, RequestNote* parent, BlackBoardNote* child,
                                     std::uint32_t result, std::uint32_t value);
using GroundWorkFn = std::uint32_t(__fastcall*)(void* self, void* edx, HouseGroundRequestNote* note);
using RunFn = std::uint32_t(__fastcall*)(ComputerPlayerAgent* self, void* edx);

void __fastcall T_DO_BUILD_HOUSE_AGENT_NoteReported(void* self, void* edx, RequestNote* parent, BlackBoardNote* child,
                                                    std::uint32_t result, std::uint32_t value);
void __fastcall HOUSE_REQUEST_AGENT_NoteReported(void* self, void* edx, RequestNote* parent, BlackBoardNote* child,
                                                 std::uint32_t result, std::uint32_t value);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_WorkOnNote(void* self, void* edx, HouseGroundRequestNote* note);
std::uint32_t __fastcall COMPUTER_PLAYER_AGENT_Run(ComputerPlayerAgent* self, void* edx);

S3RE_HOOK(0x005656a0, T_DO_BUILD_HOUSE_AGENT_NoteReported, ReportedFn, Original_T_DO_BUILD_HOUSE_AGENT_NoteReported,
          T_DO_BUILD_HOUSE_AGENT_NoteReported);
S3RE_HOOK(0x00578920, HOUSE_REQUEST_AGENT_NoteReported, ReportedFn, Original_HOUSE_REQUEST_AGENT_NoteReported,
          HOUSE_REQUEST_AGENT_NoteReported);
S3RE_HOOK(0x00576530, HOUSE_GROUND_REQUEST_AGENT_WorkOnNote, GroundWorkFn,
          Original_HOUSE_GROUND_REQUEST_AGENT_WorkOnNote, HOUSE_GROUND_REQUEST_AGENT_WorkOnNote);
S3RE_HOOK(0x005846a0, COMPUTER_PLAYER_AGENT_Run, RunFn, Original_COMPUTER_PLAYER_AGENT_Run, COMPUTER_PLAYER_AGENT_Run);

// Purpose: The build agent hears back from its house request: puts the
// build order back on the active list with the outcome (placed, site found
// but not placed, no site).
void __fastcall T_DO_BUILD_HOUSE_AGENT_NoteReported(void* /*self*/, void* /*edx*/, RequestNote* parent,
                                                    BlackBoardNote* /*child*/, std::uint32_t result,
                                                    std::uint32_t value) {
    ActivateNote(&parent->base);
    if (static_cast<std::uint16_t>(result) == kSucceeded) {
        parent->state = kBuildPlaced;
        return;
    }
    parent->state = (value & 1) != 0 ? kBuildSiteFound : kBuildNoSite;
}

// Purpose: The house request agent collects the reports of its ground and
// ware requests; once both are in, it reports to the build agent (success
// with the site only if both succeeded).
void __fastcall HOUSE_REQUEST_AGENT_NoteReported(void* /*self*/, void* /*edx*/, RequestNote* parent,
                                                 BlackBoardNote* child, std::uint32_t result, std::uint32_t value) {
    auto* request = reinterpret_cast<HouseRequestNote*>(parent);
    switch (child->type) {
    case Value(NoteType::LocalWareRequest):
        if (static_cast<std::uint16_t>(result) != 0) {
            parent->state |= kWareOk;
        }
        ++parent->reports;
        break;
    case Value(NoteType::HouseGroundRequest):
        if (static_cast<std::uint16_t>(result) != 0) {
            parent->state |= kGroundOk;  // (a byte `or` in the original: the same)
        }
        ++parent->reports;
        request->site = value;  // stored either way
        break;
    default: break;
    }
    if (parent->reports != kExpectedReports) {
        return;
    }
    SetFlags(&parent->base, kNoteFlagReported);
    if (parent->state == kBothOk) {
        Report(&parent->base, kSucceeded, request->site);
    } else {
        Report(&parent->base, 0, parent->state);
    }
}

// Purpose: The ground request agent's turn: checks the next few positions
// of its spiral around the wanted place and reports the first acceptable
// site (or that there is none) to the house request.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_WorkOnNote(void* /*self*/, void* /*edx*/,
                                                               HouseGroundRequestNote* note) {
    for (std::uint16_t step = 0; step < g_aiPlayerSetup.get()->positions_per_turn; ++step) {
        if (note->skip_check == 0) {
            const std::uint32_t x = static_cast<std::uint16_t>(note->spiral.x);
            const std::uint32_t y = static_cast<std::uint16_t>(note->spiral.y);
            using OwnerFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y);
            if (static_cast<std::uint16_t>(reinterpret_cast<OwnerFn>(kTileOwner)(&x, &y)) == g_aiPlayerNumber &&
                CallNote(kCheckAndImproveSite, note) != 0) {
                note->found = 1;
                using NoteFn = std::uint32_t(__fastcall*)(ComputerPlayer* player, void* edx, void* note);
                reinterpret_cast<NoteFn>(kDeactivateNoteOf)(g_aiPlayer, nullptr, note);
                SetFlags(&note->request.base, kNoteFlagReported);
                const std::uint32_t site = (static_cast<std::uint32_t>(static_cast<std::int32_t>(note->spiral.y)) << 16) +
                                           static_cast<std::uint32_t>(static_cast<std::int32_t>(note->spiral.x));
                Report(&note->request.base, kSucceeded, site);
                return kSucceeded;
            }
        } else {
            note->skip_check = 0;
        }
        if (CallNote(kNextSpiralPosition, note) == 0) {
            SetFlags(&note->request.base, kNoteFlagReported);
            Report(&note->request.base, 0, 0);
            return kSucceeded;  // (AX 100 either way: the handler is done with the note)
        }
    }
    return kSucceeded;
}

// Purpose: Every so many runs (by the player's setup, staggered by player
// number) the computer player asks for a new small tower, to expand.
std::uint32_t __fastcall COMPUTER_PLAYER_AGENT_Run(ComputerPlayerAgent* self, void* /*edx*/) {
    ++self->runs;
    const std::int32_t limit =
        g_aiPlayerSetup.get()->small_tower_interval * kRunsPerInterval + static_cast<std::int32_t>(g_aiPlayerNumber);
    if (static_cast<std::int32_t>(self->runs) <= limit) {
        return static_cast<std::uint32_t>(limit);
    }
    self->runs = 0;
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kRecordBestCandidate)(Value(BuildingType::SmallTower), kSmallTowerPriority, kAnyPlace,
                                                      kAnyPlace);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Notes in buffers with random bytes; ActivateNoteOf, DeactivateNoteOf,
// SetFlags, ReportToRequester, CheckAndImproveSite and NextSpiralPosition
// stubbed (recorded; the last two answer at random, NextSpiralPosition
// sometimes runs out). TileOwner runs on a small world whose tiles belong
// to players 0..2. RecordBestCandidate runs on a player data buffer.
// Compared: the buffers, the stub calls, AX (WorkOnNote) or EAX (Run).

namespace {

constexpr std::uint32_t kTestMap = 32;

struct StubCall {
    std::uint32_t callee, self, args[2];
    bool operator==(const StubCall& other) const {
        return callee == other.callee && self == other.self && args[0] == other.args[0] && args[1] == other.args[1];
    }
};

struct AgentFunctions {
    ReportedFn build, house;
    GroundWorkFn ground;
    RunFn run;
};

void CompareAgents(test::Context& ctx, const AgentFunctions& candidate, int cases) {
    const AgentFunctions original{Original_T_DO_BUILD_HOUSE_AGENT_NoteReported, Original_HOUSE_REQUEST_AGENT_NoteReported,
                                  Original_HOUSE_GROUND_REQUEST_AGENT_WorkOnNote, Original_COMPUTER_PLAYER_AGENT_Run};
    CGameData* const savedWorld = g_aiWorld;
    ComputerPlayer* const savedPlayer = g_aiPlayer;
    AiPlayerSetup* const savedSetup = g_aiPlayerSetup;
    AiPlayerData* const savedData = g_aiPlayerData;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    const std::uint16_t savedX = g_aiMapSizeX, savedY = g_aiMapSizeY;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);
    std::vector<std::uint8_t> setupBytes(sizeof(AiPlayerSetup));
    auto* setup = reinterpret_cast<AiPlayerSetup*>(setupBytes.data());
    g_aiPlayerSetup = setup;

    std::vector<StubCall>* log = nullptr;
    test::Rng* stubRng = nullptr;
    std::uint32_t checkOdds = 2, nextOdds = 8;
    const auto record = [&](std::uintptr_t callee, std::uint32_t self, const std::uint32_t* args, int count) {
        StubCall call{static_cast<std::uint32_t>(callee), self, {0, 0}};
        for (int k = 0; k < count; ++k) {
            call.args[k] = args[k];
        }
        log->push_back(call);
    };
    // The answers come from a sequence per case, replayed for the second run.
    std::vector<std::uint32_t> answers;
    std::size_t answerIndex = 0;
    const auto answer = [&](std::uint32_t odds) -> std::uint32_t {
        if (answerIndex == answers.size()) {
            answers.push_back(stubRng->Chance(1, odds) ? 0 : 1 + stubRng->Below(5));
        }
        return answers[answerIndex++];
    };
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<1, kActivateNoteOf>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kActivateNoteOf, self, args, 1);
        return 0u;
    });
    stubs.Install<1, kDeactivateNoteOf>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kDeactivateNoteOf, self, args, 1);
        return 0u;
    });
    stubs.Install<1, kSetFlags>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kSetFlags, self, args, 1);
        return 0u;
    });
    stubs.Install<2, kReportToRequester>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kReportToRequester, self, args, 2);
        return 0u;
    });
    stubs.Install<0, kCheckAndImproveSite>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        record(kCheckAndImproveSite, self, nullptr, 0);
        return answer(checkOdds) % 2;
    });
    stubs.Install<0, kNextSpiralPosition>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        record(kNextSpiralPosition, self, nullptr, 0);
        // the stub moves the spiral like the real one would (both runs alike)
        auto* note = reinterpret_cast<HouseGroundRequestNote*>(self);
        note->spiral.x = static_cast<std::int16_t>((note->spiral.x + 3) % (kTestMap + 2));
        note->spiral.y = static_cast<std::int16_t>((note->spiral.y + 5) % (kTestMap + 2));
        return answer(nextOdds);
    });
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].owner = static_cast<std::uint8_t>((x / 8 + y / 8) % 4 == 3 ? 0xff : (x / 8 + y / 8) % 3);
        }
    }
    constexpr std::size_t kBytes = 0x60;
    std::vector<std::uint8_t> dataA(sizeof(AiPlayerData) + 16), dataB(sizeof(AiPlayerData) + 16);
    int reports = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        stubRng = &rng;
        answers.clear();
        std::uint8_t a[kBytes], b[kBytes], childA[kBytes], childB[kBytes];
        rng.Fill(a, kBytes);
        rng.Fill(childA, kBytes);
        g_aiPlayer = reinterpret_cast<ComputerPlayer*>(rng.Next32());
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
        rng.Fill(dataA.data(), dataA.size());
        dataB = dataA;
        setup->positions_per_turn = static_cast<std::uint16_t>(rng.Chance(1, 10) ? 0 : 1 + rng.Below(6));
        setup->small_tower_interval = static_cast<std::uint16_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(8));
        checkOdds = 1 + rng.Below(4);
        nextOdds = 1 + rng.Below(10);
        const int op = static_cast<int>(rng.Below(4));
        const std::uint32_t result = rng.Chance(1, 2) ? 100 | (rng.Chance(1, 3) ? rng.Next32() & 0xffff0000u : 0)
                                                      : rng.Below(3);
        const std::uint32_t value = rng.Next32();
        auto* noteA = reinterpret_cast<HouseGroundRequestNote*>(a);
        auto* childNote = reinterpret_cast<BlackBoardNote*>(childA);
        switch (op) {
        case 1:
            childNote->type = static_cast<std::uint16_t>(rng.Chance(1, 8) ? rng.Below(5) : rng.Below(2));
            reinterpret_cast<RequestNote*>(a)->reports = static_cast<std::uint16_t>(rng.Below(3));
            reinterpret_cast<RequestNote*>(a)->state = static_cast<std::uint16_t>(rng.Below(4));
            break;
        case 2:
            noteA->spiral.x = static_cast<std::int16_t>(rng.Below(kTestMap + 2));
            noteA->spiral.y = static_cast<std::int16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap));
            noteA->skip_check = rng.Chance(1, 4) ? rng.Next32() : 0;
            break;
        case 3: {
            auto* agent = reinterpret_cast<ComputerPlayerAgent*>(a);
            const std::int32_t limit = setup->small_tower_interval * kRunsPerInterval + g_aiPlayerNumber;
            if (rng.Chance(1, 2)) {
                agent->runs = static_cast<std::uint16_t>(limit + static_cast<std::int32_t>(rng.Below(3)) - 1);
            }
            break;
        }
        default: break;
        }
        std::memcpy(b, a, kBytes);
        std::memcpy(childB, childA, kBytes);
        std::vector<StubCall> logA, logB;
        std::uint32_t ra = 0, rb = 0;
        const auto run = [&](const AgentFunctions& f, std::uint8_t* note, std::uint8_t* child) -> std::uint32_t {
            switch (op) {
            case 0:
                f.build(nullptr, nullptr, reinterpret_cast<RequestNote*>(note), reinterpret_cast<BlackBoardNote*>(child),
                        result, value);
                return 0;
            case 1:
                f.house(nullptr, nullptr, reinterpret_cast<RequestNote*>(note), reinterpret_cast<BlackBoardNote*>(child),
                        result, value);
                return 0;
            case 2: return f.ground(nullptr, nullptr, reinterpret_cast<HouseGroundRequestNote*>(note)) & 0xffff;
            default: return f.run(reinterpret_cast<ComputerPlayerAgent*>(note), nullptr);
            }
        };
        log = &logA;
        answerIndex = 0;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        ra = run(original, a, childA);
        log = &logB;
        answerIndex = 0;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        rb = run(candidate, b, childB);
        // The notes live in different buffers: compare the recorded `this`
        // relative to each buffer.
        for (StubCall& call : logA) {
            call.self -= call.self - reinterpret_cast<std::uint32_t>(a) < kBytes ? reinterpret_cast<std::uint32_t>(a) : 0;
            call.args[0] -= call.args[0] - reinterpret_cast<std::uint32_t>(a) < kBytes ? reinterpret_cast<std::uint32_t>(a) : 0;
        }
        for (StubCall& call : logB) {
            call.self -= call.self - reinterpret_cast<std::uint32_t>(b) < kBytes ? reinterpret_cast<std::uint32_t>(b) : 0;
            call.args[0] -= call.args[0] - reinterpret_cast<std::uint32_t>(b) < kBytes ? reinterpret_cast<std::uint32_t>(b) : 0;
        }
        for (const StubCall& call : logA) {
            reports += call.callee == kReportToRequester ? 1 : 0;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a, b, kBytes);
        same &= S3RE_EXPECT_MEM_EQ(childA, childB, kBytes);
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        same &= S3RE_EXPECT(logA == logB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d (%u vs %u calls)", i, op, static_cast<unsigned>(logA.size()),
                     static_cast<unsigned>(logB.size()));
        }
        stubs.Take();
    }
    if (cases >= 1000 && !S3RE_EXPECT(reports >= cases / 10)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d reports", reports);
    }
    g_aiWorld = savedWorld;
    g_aiPlayer = savedPlayer;
    g_aiPlayerSetup = savedSetup;
    g_aiPlayerData = savedData;
    g_aiPlayerNumber = savedNumber;
    g_aiMapSizeX = savedX;
    g_aiMapSizeY = savedY;
}

void __fastcall WrongHouseReported(void* self, void* edx, RequestNote* parent, BlackBoardNote* child,
                                   std::uint32_t result, std::uint32_t value) {
    // Wrong: the ground request's site only on success.
    auto* request = reinterpret_cast<HouseRequestNote*>(parent);
    const std::uint32_t site = request->site;
    HOUSE_REQUEST_AGENT_NoteReported(self, edx, parent, child, result, value);
    if (child->type == Value(NoteType::HouseGroundRequest) && static_cast<std::uint16_t>(result) == 0) {
        request->site = site;
    }
}

std::uint32_t __fastcall WrongRun(ComputerPlayerAgent* self, void* edx) {
    // Wrong: the limit counts as reached.
    const std::int32_t limit =
        g_aiPlayerSetup.get()->small_tower_interval * kRunsPerInterval + static_cast<std::int32_t>(g_aiPlayerNumber);
    if (static_cast<std::int32_t>(static_cast<std::uint16_t>(self->runs + 1)) == limit) {
        self->runs = static_cast<std::uint16_t>(limit);  // one more, then the original's path
    }
    return COMPUTER_PLAYER_AGENT_Run(self, edx);
}

}  // namespace

S3RE_TEST(AiRequestAgents_MatchOriginal) {
    CompareAgents(ctx,
                  {&T_DO_BUILD_HOUSE_AGENT_NoteReported, &HOUSE_REQUEST_AGENT_NoteReported,
                   &HOUSE_GROUND_REQUEST_AGENT_WorkOnNote, &COMPUTER_PLAYER_AGENT_Run},
                  4000);
}

S3RE_TEST(AiRequestAgents_ComparisonDetectsWrongImplementation) {
    const AgentFunctions wrongs[] = {
        {&T_DO_BUILD_HOUSE_AGENT_NoteReported, &WrongHouseReported, &HOUSE_GROUND_REQUEST_AGENT_WorkOnNote,
         &COMPUTER_PLAYER_AGENT_Run},
        {&T_DO_BUILD_HOUSE_AGENT_NoteReported, &HOUSE_REQUEST_AGENT_NoteReported, &HOUSE_GROUND_REQUEST_AGENT_WorkOnNote,
         &WrongRun},
    };
    for (const AgentFunctions& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareAgents(quiet, wrong, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
