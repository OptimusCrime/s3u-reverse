// A computer player's construction (the unnamed source file before
// Rainer\Cevaland.cpp; re/computer_players.md "Construction").
//
//   0x00550900 CComputerPlayer::CComputerPlayer(world, message queue,
//              player, first) thiscall, ret 0x10: Win32::CObject's
//              constructor 0x610320; world, message_queue, player from the
//              arguments, steps, promotions, setup_index, land_evaluation,
//              data 0; vtable 0x65a530; the first byte non-zero:
//              InitAiGlobals 0x551b80; g_aiMilitaryResetRequest (0x7e3410)
//              takes that byte if it is 0; then Init 0x550a30. EAX = this.
//              (The original's SEH frame would destroy the CObject base if
//              Init threw; ours has none: only an out-of-memory throw of
//              CEvaLand::Acquire / AI::CreateAgent reaches it.)
//   0x00550a30 CComputerPlayer::Init() thiscall: the step counters 0 and
//              the player number word (+0x272, also clearing +0x273);
//              SetCurrentPlayer 0x551a30; handlers and the four note lists
//              0xffff, counts and cursor 0, notes and agents 0; one agent
//              per kind of g_aiAgentKinds (0x7312b0, ends at 0xffff;
//              AI::CreateAgent 0x56f6d0 gets the kind in CX, the upper half
//              is the register's leftover: the table's second word, then
//              `this`'s; AddAgent 0x551400); land_evaluation = CEvaLand::Acquire
//              0x553d30; for every area 0..area_high_water the player owns
//              (Area::player compared with the whole player dword): AddArea
//              0x555260 (area, player's low word), SendToolSetting10
//              (value 1), the race's transport orders
//              (SendChangeTransportOrder 0x58ebc0: (3, 3), then Romans,
//              Asians, Amazons (2, 3), (1, 3); Egyptians (1, 3), (2, 3)) and
//              area conditions (SendSetAreaCondition 0x58ea30: Egyptians
//              (25, 8), Asians (5, 8), (6, 8)), then for every race
//              (2, 8); once per game (g_aiMarkBuildingTypesPending == 1)
//              MarkBuildingTypesInLandGrid 0x5723a0 and the flag 0; area =
//              0xffffffff. EAX: left over.

#include "game/ai/ai_layout.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "game/ai/ai_area_test_world.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kCObjectConstruct = 0x00610320;       // Win32::CObject::CObject (thiscall)
constexpr std::uintptr_t kInitAiGlobals = 0x00551b80;          // CComputerPlayer::InitAiGlobals (thiscall)
constexpr std::uintptr_t kInit = 0x00550a30;                   // CComputerPlayer::Init (thiscall)
constexpr std::uintptr_t kSetCurrentPlayer = 0x00551a30;       // AI::SetCurrentPlayer (fastcall)
constexpr std::uintptr_t kCreateAgent = 0x0056f6d0;            // AI::CreateAgent (fastcall)
constexpr std::uintptr_t kAddAgent = 0x00551400;               // CComputerPlayer::AddAgent (thiscall)
constexpr std::uintptr_t kAcquire = 0x00553d30;                // CEvaLand::Acquire (cdecl)
constexpr std::uintptr_t kAddArea = 0x00555260;                // CEvaLand::AddArea (thiscall)
constexpr std::uintptr_t kToolSetting10 = 0x0058eb40;          // AI::SendToolSetting10 (fastcall)
constexpr std::uintptr_t kChangeTransportOrder = 0x0058ebc0;   // AI::SendChangeTransportOrder (fastcall)
constexpr std::uintptr_t kSetAreaCondition = 0x0058ea30;       // AI::SendSetAreaCondition (fastcall)
constexpr std::uintptr_t kMarkBuildingTypes = 0x005723a0;      // AI::MarkBuildingTypesInLandGrid (cdecl)

constexpr std::uint16_t kEndOfKinds = 0xffff;
constexpr std::uint32_t kNoArea = 0xffffffff;

void SendToolSetting(std::uint8_t player, const std::uint32_t* area, std::uint8_t value) {
    using Fn = std::uint32_t(__fastcall*)(const void* player, const void* area, const void* value);
    reinterpret_cast<Fn>(kToolSetting10)(&player, area, &value);
}

void SendTransportOrder(std::uint8_t player, const std::uint32_t* area, std::uint8_t first, std::uint8_t second) {
    using Fn = std::uint32_t(__fastcall*)(const void* player, const void* area, const void* a, const void* b);
    reinterpret_cast<Fn>(kChangeTransportOrder)(&player, area, &first, &second);
}

void SendAreaCondition(std::uint8_t player, const std::uint32_t* area, std::uint8_t kind, std::uint8_t value) {
    using Fn = std::uint32_t(__fastcall*)(const void* player, const void* area, const void* kind, const void* value);
    reinterpret_cast<Fn>(kSetAreaCondition)(&player, area, &kind, &value);
}

// 0x550b77..0x550ecb: the records and starting settings of an area the
// player owns.
void StartArea(ComputerPlayer& self, std::uint32_t area) {
    using AddAreaFn = void(__fastcall*)(std::uint32_t land, void* edx, std::uint32_t area, std::uint32_t player);
    reinterpret_cast<AddAreaFn>(kAddArea)(self.land_evaluation, nullptr, area, self.player & 0xffff);
    const auto player = static_cast<std::uint8_t>(self.player);
    SendToolSetting(player, &area, 1);
    const std::int32_t race = g_aiWorld.get()->players[self.player].race;
    switch (race) {
    case Value(Race::Roman):
    case Value(Race::Amazon):
        SendTransportOrder(player, &area, 3, 3);
        SendTransportOrder(player, &area, 2, 3);
        SendTransportOrder(player, &area, 1, 3);
        break;
    case Value(Race::Egyptian):
        SendTransportOrder(player, &area, 3, 3);
        SendTransportOrder(player, &area, 1, 3);
        SendTransportOrder(player, &area, 2, 3);
        SendAreaCondition(player, &area, 25, 8);
        break;
    case Value(Race::Asian):
        SendTransportOrder(player, &area, 3, 3);
        SendTransportOrder(player, &area, 2, 3);
        SendTransportOrder(player, &area, 1, 3);
        SendAreaCondition(player, &area, 5, 8);
        SendAreaCondition(player, &area, 6, 8);
        break;
    default:
        break;
    }
    SendAreaCondition(player, &area, 2, 8);
}

}  // namespace

using ComputerPlayerConstructFn = ComputerPlayer*(__fastcall*)(ComputerPlayer* self, void* edx, std::uint32_t world,
                                                               std::uint32_t queue, std::uint32_t player,
                                                               std::uint32_t first);
using ComputerPlayerInitFn = void(__fastcall*)(ComputerPlayer* self, void* edx);

ComputerPlayer* __fastcall CComputerPlayer_Construct(ComputerPlayer* self, void* edx, std::uint32_t world,
                                                     std::uint32_t queue, std::uint32_t player, std::uint32_t first);
void __fastcall CComputerPlayer_Init(ComputerPlayer* self, void* edx);

S3RE_HOOK(0x00550900, CComputerPlayer_Construct, ComputerPlayerConstructFn, Original_CComputerPlayer_Construct,
          CComputerPlayer_Construct);
S3RE_HOOK(0x00550a30, CComputerPlayer_Init, ComputerPlayerInitFn, Original_CComputerPlayer_Init,
          CComputerPlayer_Init);

// Purpose: Makes a computer player at the start of a game (or when a human
// player drops out of a network game).
ComputerPlayer* __fastcall CComputerPlayer_Construct(ComputerPlayer* self, void* /*edx*/, std::uint32_t world,
                                                     std::uint32_t queue, std::uint32_t player, std::uint32_t first) {
    reinterpret_cast<void*(__fastcall*)(void*, void*)>(kCObjectConstruct)(self, nullptr);
    self->world = world;
    self->message_queue = queue;
    self->steps = 0;
    self->promotions = 0;
    self->player = player;
    self->setup_index = 0;
    self->land_evaluation = 0;
    self->data = 0;
    self->vtable = vtables::kCComputerPlayer;
    const auto firstByte = static_cast<std::uint8_t>(first);
    if (firstByte != 0) {
        reinterpret_cast<void(__fastcall*)(void*, void*)>(kInitAiGlobals)(self, nullptr);
    }
    if (g_aiMilitaryResetRequest == 0) {
        g_aiMilitaryResetRequest = firstByte;
    }
    reinterpret_cast<ComputerPlayerInitFn>(kInit)(self, nullptr);
    return self;
}

// Purpose: Sets a computer player up: empty blackboard, its agents, the
// shared land evaluation with a record per economy area it owns, and the
// starting transport and production settings of those areas.
void __fastcall CComputerPlayer_Init(ComputerPlayer* self, void* /*edx*/) {
    const auto playerWord = static_cast<std::uint16_t>(self->player);
    self->timer274 = 0;
    // A word store: the player number and the byte after it.
    self->player_number = static_cast<std::uint8_t>(playerWord);
    self->unk273 = static_cast<std::uint8_t>(playerWord >> 8);
    self->unk276 = 0;
    self->timer278 = 0;
    self->unk27a = 0;
    self->timer27c = 0;
    reinterpret_cast<std::uint32_t(__fastcall*)(void*)>(kSetCurrentPlayer)(self);
    std::fill(std::begin(self->handlers), std::end(self->handlers), kNoAgent);
    std::fill(std::begin(self->notes), std::end(self->notes), nullptr);
    std::fill(std::begin(self->agents), std::end(self->agents), nullptr);
    std::fill(std::begin(self->root_notes), std::end(self->root_notes), kNoNote);
    self->root_note_count = 0;
    std::fill(std::begin(self->pending_notes), std::end(self->pending_notes), kNoNote);
    self->pending_note_count = 0;
    std::fill(std::begin(self->active_notes), std::end(self->active_notes), kNoNote);
    self->active_note_count = 0;
    self->active_cursor = 0;
    std::fill(std::begin(self->doomed_notes), std::end(self->doomed_notes), kNoNote);
    self->doomed_note_count = 0;

    // 0x550b01: the agents. CX holds the kind; the register's upper half is
    // the table's second word after the first (dword) read, then `this`'s
    // (ECX was reloaded with it for AddAgent).
    std::uint32_t upper = static_cast<std::uint32_t>(g_aiAgentKinds[1]) << 16;
    for (std::uint32_t k = 0; g_aiAgentKinds[k] != kEndOfKinds; k = (k + 1) & 0xffff) {
        using CreateFn = std::uint32_t(__fastcall*)(std::uint32_t kind);
        const std::uint32_t agent = reinterpret_cast<CreateFn>(kCreateAgent)(upper | g_aiAgentKinds[k]);
        upper = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xffff0000u;
        using AddFn = void(__fastcall*)(void* self, void* edx, std::uint32_t agent);
        reinterpret_cast<AddFn>(kAddAgent)(self, nullptr, agent);
    }
    self->land_evaluation =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(__cdecl*)()>(kAcquire)()));

    // 0x550b3e: the player's areas (the high-water mark re-read each time;
    // none at all for 0xffffffff).
    for (std::uint32_t area = 0; g_aiWorld.get()->area_high_water != kNoArea &&
                                 area < g_aiWorld.get()->area_high_water + 1;
         ++area) {
        if (g_aiWorld.get()->areas[area].player == self->player) {
            StartArea(*self, area);
        }
    }
    if (g_aiMarkBuildingTypesPending == 1) {
        reinterpret_cast<void(__cdecl*)()>(kMarkBuildingTypes)();
        g_aiMarkBuildingTypesPending = 0;
    }
    self->area = kNoArea;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The site-list test map (ai_area_test_world.h) with areas 0..high-water
// owned by players 0..2 or nobody, player races 0..3 (sometimes others).
// The shared CEvaLand exists already (a test land with empty lists,
// reference count 1..3): Acquire hands it out; MessageQueue::Push and the
// host check are stubbed (commands recorded); MarkBuildingTypesInLandGrid
// is stubbed (its calls counted). Compared after Init / the constructor on
// two ComputerPlayer objects with the same random bytes: the object (the
// agent pointers as the agents' vtables and kinds), the land's lists (area
// ids and counts of the new records), the commands, the reference count
// and the globals the functions set. The agents are deleted afterwards
// through the destructor 0x5509d0 (which also releases the land).

namespace {

using namespace ai_test;

constexpr std::uintptr_t kIsHost = 0x004318a0;
constexpr std::uintptr_t kPush = 0x004309c0;
constexpr std::uintptr_t kDestructor = 0x005509d0;  // CComputerPlayer::~CComputerPlayer (thiscall)
constexpr std::uint32_t kQueue = 0x5152a000;

using Message = std::array<std::uint8_t, 12>;

struct Outcome {
    std::vector<std::uint8_t> object;
    std::vector<std::uint32_t> lists;
    std::vector<Message> messages;
    std::uint32_t refs = 0;
    std::uint32_t markPending = 0;
    std::uint8_t militaryReset = 0;
    std::uint32_t marks = 0;
    std::vector<std::uint32_t> globals;
    bool operator==(const Outcome& o) const {
        return object == o.object && lists == o.lists && messages == o.messages && refs == o.refs &&
               markPending == o.markPending && militaryReset == o.militaryReset && marks == o.marks &&
               globals == o.globals;
    }
};

std::vector<std::uint8_t> ObjectBytes(const ComputerPlayer& player) {
    std::vector<std::uint8_t> out(reinterpret_cast<const std::uint8_t*>(&player),
                                  reinterpret_cast<const std::uint8_t*>(&player) + sizeof(ComputerPlayer));
    std::memset(out.data() + offsetof(ComputerPlayer, agents), 0, sizeof(player.agents));
    for (const BlackBoardAgent* agent : player.agents) {
        const auto* tagged = reinterpret_cast<const AiTaggedRecord*>(agent);
        out.push_back(agent == nullptr ? 0xee : static_cast<std::uint8_t>(tagged->tag));
        const std::uint32_t vtable = agent == nullptr ? 0 : tagged->unk00;
        out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(&vtable),
                   reinterpret_cast<const std::uint8_t*>(&vtable) + 4);
    }
    return out;
}

std::vector<std::uint32_t> ListSummary(const AiLandEvaluation& land) {
    std::vector<std::uint32_t> out;
    for (const AiList& list : land.lists) {
        out.push_back(list.count);
        const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
        for (const AiAreaNode* node = head->next; node != head; node = node->next) {
            out.push_back(node->value.area);
            out.push_back(node->value.sites);
            out.push_back(node->value.force_production);
        }
    }
    return out;
}

// The AI globals SetCurrentPlayer writes.
std::vector<std::uint32_t> AiGlobals() {
    return {static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_aiPlayer.get())),
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_aiPlayerData.get())),
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_aiPlayerSetup.get())), g_aiPlayerNumber};
}

struct InitFns {
    ComputerPlayerConstructFn construct;
    ComputerPlayerInitFn init;
};

void CompareInit(test::Context& ctx, const InitFns& ours, int cases) {
    const InitFns original{Original_CComputerPlayer_Construct, Original_CComputerPlayer_Init};
    SavedGlobals saved;
    ComputerPlayer* const savedPlayer = g_aiPlayer;
    AiPlayerData* const savedData = g_aiPlayerData;
    AiPlayerSetup* const savedSetup = g_aiPlayerSetup;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    const std::uint32_t savedRefs = g_aiEvaLandRefCount;
    AiLandEvaluation* const savedLand = g_aiEvaLand;
    const std::uint32_t savedPending = g_aiMarkBuildingTypesPending;
    const std::uint8_t savedReset = g_aiMilitaryResetRequest;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    g_aiWorld = world;
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);

    std::vector<Message>* log = nullptr;
    std::uint32_t marks = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsHost>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 1u; });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        Message message{};
        std::memcpy(message.data(), reinterpret_cast<const void*>(args[0]), 4 * (args[1] < 3 ? args[1] : 3));
        message[3] = 0;  // (an unwritten header byte)
        message[11] ^= static_cast<std::uint8_t>(self == kQueue ? 0 : 0xff);
        log->push_back(message);
        return 0u;
    });
    stubs.Install<0, kMarkBuildingTypes>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        ++marks;
        return 0u;
    });
    int areas = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 10 == 0) {
            SetUpMap(rng, world, cells);
        }
        world->area_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(8);
        for (std::uint32_t a = 0; a < 9; ++a) {
            world->areas[a].player = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0xff : rng.Below(3));
        }
        for (layout::Player& p : world->players) {
            p.race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Below(8) : rng.Below(4));
        }
        const bool construct = rng.Chance(1, 2);
        const std::uint32_t player = rng.Chance(1, 10) ? rng.Below(3) | 0x10000u : rng.Below(3);
        const std::uint32_t first = rng.Chance(1, 2) ? 0 : rng.Next32();
        const std::uint32_t refs = 1 + rng.Below(3);
        const std::uint32_t pending = rng.Chance(1, 2) ? 1 : rng.Below(3);
        const std::uint8_t reset = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(255));
        std::vector<std::uint8_t> start(sizeof(ComputerPlayer));
        rng.Fill(start.data(), start.size());
        Outcome outcome[2];
        const InitFns* fns[2] = {&original, &ours};
        for (int side = 0; side < 2; ++side) {
            TestLand land(cells);
            land.land.width = land.land.height = kGridSide;
            land.land.cell_count = kGridSide * kGridSide;
            land.land.tiles_per_cell = 64;
            g_aiEvaLand = &land.land;
            g_aiEvaLandRefCount = refs;
            g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land.land);
            g_aiMarkBuildingTypesPending = pending;
            g_aiMilitaryResetRequest = reset;
            marks = 0;
            std::vector<std::uint8_t> bytes = start;
            auto* object = reinterpret_cast<ComputerPlayer*>(bytes.data());
            object->world = reinterpret_cast<std::uint32_t>(world);
            object->player = player;
            object->setup_index = start[0] % 3u;
            object->message_queue = kQueue;
            Outcome& out = outcome[side];
            log = &out.messages;
            if (construct) {
                if (!S3RE_EXPECT_EQ(fns[side]->construct(object, nullptr, reinterpret_cast<std::uint32_t>(world),
                                                         kQueue, player, first),
                                    object)) {
                    ctx.Fail(__FILE__, __LINE__, "  case %d: EAX", i);
                }
            } else {
                fns[side]->init(object, nullptr);
            }
            out.object = ObjectBytes(*object);
            // (each side's own land: compared as "is the land")
            const std::uint32_t isLand = object->land_evaluation == reinterpret_cast<std::uint32_t>(&land.land) ? 1 : 0;
            std::memcpy(out.object.data() + offsetof(ComputerPlayer, land_evaluation), &isLand, 4);
            out.lists = ListSummary(land.land);
            out.refs = g_aiEvaLandRefCount;
            out.markPending = g_aiMarkBuildingTypesPending;
            out.militaryReset = g_aiMilitaryResetRequest;
            out.marks = marks;
            out.globals = AiGlobals();
            out.globals[0] = g_aiPlayer.get() == object ? 1 : 0;  // (each side's own object)
            stubs.Take();
            // Agents and notes go with the destructor (which releases the
            // land: one reference more than before, so it stays).
            reinterpret_cast<void(__fastcall*)(void*, void*)>(kDestructor)(object, nullptr);
            g_aiEvaLand = savedLand;
        }
        areas += static_cast<int>(outcome[0].lists.size());
        if (!(outcome[0] == outcome[1])) {
            ctx.Fail(__FILE__, __LINE__, "  pending %u/%u reset %u/%u marks %u/%u globals %u %u %u %u / %u %u %u %u",
                     outcome[0].markPending, outcome[1].markPending, outcome[0].militaryReset,
                     outcome[1].militaryReset, outcome[0].marks, outcome[1].marks, outcome[0].globals[0],
                     outcome[0].globals[1], outcome[0].globals[2], outcome[0].globals[3], outcome[1].globals[0],
                     outcome[1].globals[1], outcome[1].globals[2], outcome[1].globals[3]);
        }
        if (outcome[0].object != outcome[1].object) {
            std::size_t k = 0;
            while (k < outcome[0].object.size() && outcome[0].object[k] == outcome[1].object[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__, "  object byte %u: %02x / %02x", static_cast<unsigned>(k),
                     outcome[0].object[k], outcome[1].object[k]);
        }
        if (!S3RE_EXPECT(outcome[0] == outcome[1])) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, player %08x: object %d lists %d messages %d (%u/%u) refs %d",
                     i, construct ? "constructor" : "Init", player, outcome[0].object == outcome[1].object ? 1 : 0,
                     outcome[0].lists == outcome[1].lists ? 1 : 0, outcome[0].messages == outcome[1].messages ? 1 : 0,
                     static_cast<unsigned>(outcome[0].messages.size()),
                     static_cast<unsigned>(outcome[1].messages.size()), outcome[0].refs == outcome[1].refs ? 1 : 0);
        }
    }
    g_aiPlayer = savedPlayer;
    g_aiPlayerData = savedData;
    g_aiPlayerSetup = savedSetup;
    g_aiPlayerNumber = savedNumber;
    g_aiEvaLandRefCount = savedRefs;
    g_aiEvaLand = savedLand;
    g_aiMarkBuildingTypesPending = savedPending;
    g_aiMilitaryResetRequest = savedReset;
    S3RE_EXPECT(areas > cases * 20);
}

void __fastcall WrongInit(ComputerPlayer* self, void* edx) {
    // Wrong: the area pointer stays as it was.
    const std::uint32_t area = self->area;
    CComputerPlayer_Init(self, edx);
    self->area = area;
}

}  // namespace

S3RE_TEST(AiComputerPlayerInit_MatchOriginal) {
    CompareInit(ctx, {&CComputerPlayer_Construct, &CComputerPlayer_Init}, 300);
}

S3RE_TEST(AiComputerPlayerInit_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareInit(quiet, {&CComputerPlayer_Construct, &WrongInit}, 100);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
