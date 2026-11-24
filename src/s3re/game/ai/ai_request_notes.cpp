// The request agents' constructors and the request notes' set-up and
// destruction (computer players, package A3 batch 2; re/computer_players.md).
// The request notes (HOUSE_REQUEST_NOTE, HOUSE_GROUND_REQUEST_NOTE,
// LOCAL_WARE_REQUEST_NOTE, T_DO_BUILD_HOUSE_NOTE; RTTI) derive from
// BLACK_BOARD_NOTE; each note type is handled by the request agent of the
// same number (ai::NoteType, ai::AgentKind).
//
// Request agents (made by AI::CreateAgent 0x56f6d0, which stores the
// vtable first): the constructor only sets the kind (AI_SetWord4); EAX is
// not used (the factory returns the object).
//   0x57ab50 LOCAL_WARE_REQUEST_AGENT (kind 0), 0x5764f0
//   HOUSE_GROUND_REQUEST_AGENT (1), 0x5787a0 HOUSE_REQUEST_AGENT (2),
//   0x5654b0 T_DO_BUILD_HOUSE_AGENT (4).
// Request note set-up (thiscall; the callers store the vtable first):
//   0x57ab20 LOCAL_WARE_REQUEST_NOTE (type, x, y) ret 0xc: BLACK_BOARD_NOTE
//     Init(0), +0x1c/+0x1e/+0x20 = the low words. EAX = the first argument's
//     low word (left over from loading it).
//   0x578760 HOUSE_REQUEST_NOTE (type, x, y, value) ret 0x10: Init(2), the
//     four words at +0x1c..+0x22, +0x24 = +0x26 = 0. EAX = 0.
//   0x565470 T_DO_BUILD_HOUSE_NOTE (the same with Init(4)).
//   0x5801c0 T_DO_BUILD_HOUSE_NOTE::T_DO_BUILD_HOUSE_NOTE(type, x, y, value)
//     ret 0x10: vtable 0x65a584, then 0x565470. EAX = this. (Its exception
//     frame has nothing to unwind.)
// BLACK_BOARD_NOTE:
//   0x5801b0 constructor: vtable 0x65a56c; EAX = this.
//   0x565590 destructor: vtable 0x65a56c (EAX: the caller's, unused).
//   0x5655a0 scalar deleting destructor (flags) ret 4: vtable 0x65a56c,
//     operator delete if bit 0; EAX = this.
//   0x56f6b0 the request notes' scalar deleting destructor (flags): the
//     same through 0x565590 (shared by all four subclasses' vtables).

#include "game/ai/ai_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// ai_helpers.cpp, ai_computer_player.cpp
std::uint16_t __fastcall AI_SetWord4(void* self, void* edx, std::uint32_t value);
std::uint32_t __fastcall BLACK_BOARD_NOTE_Init(void* self, void* edx, std::uint32_t type);

using namespace ai;

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using VoidThisFn = void(__fastcall*)(void* self, void* edx);
using DeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using Init3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t type, std::uint32_t x,
                                           std::uint32_t y);
using Init4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t type, std::uint32_t x,
                                           std::uint32_t y, std::uint32_t value);

std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall LOCAL_WARE_REQUEST_NOTE_Init(void* self, void* edx, std::uint32_t type, std::uint32_t x,
                                                     std::uint32_t y);
std::uint32_t __fastcall HOUSE_REQUEST_NOTE_Init(void* self, void* edx, std::uint32_t type, std::uint32_t x,
                                                std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall T_DO_BUILD_HOUSE_NOTE_Init(void* self, void* edx, std::uint32_t type, std::uint32_t x,
                                                   std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall T_DO_BUILD_HOUSE_NOTE_Construct(void* self, void* edx, std::uint32_t type,
                                                        std::uint32_t x, std::uint32_t y, std::uint32_t value);
std::uint32_t __fastcall BLACK_BOARD_NOTE_Construct(void* self, void* edx);
void __fastcall BLACK_BOARD_NOTE_Destroy(void* self, void* edx);
void* __fastcall BLACK_BOARD_NOTE_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void* __fastcall RequestNote_DeletingDestroy(void* self, void* edx, std::uint32_t flags);

namespace {

RequestNote* AsRequest(void* self) { return static_cast<RequestNote*>(self); }

void SetKind(void* self, AgentKind kind) { AI_SetWord4(self, nullptr, Value(kind)); }

// 0x578760 / 0x565470: the four words and two cleared ones after Init.
void SetRequest(void* self, NoteType type, std::uint32_t buildingType, std::uint32_t x, std::uint32_t y,
                std::uint32_t value) {
    BLACK_BOARD_NOTE_Init(self, nullptr, Value(type));
    RequestNote* note = AsRequest(self);
    note->building_type = static_cast<std::uint16_t>(buildingType);
    note->x = static_cast<std::uint16_t>(x);
    note->y = static_cast<std::uint16_t>(y);
    note->value22 = static_cast<std::uint16_t>(value);
    note->unk24 = 0;
    note->unk26 = 0;
}

}  // namespace

// ---------------------------------------------------------------- request agents

S3RE_HOOK(0x0057ab50, LOCAL_WARE_REQUEST_AGENT_Construct, ThisFn, Original_LOCAL_WARE_REQUEST_AGENT_Construct,
          LOCAL_WARE_REQUEST_AGENT_Construct);

// Purpose: Constructor of the agent that handles requests for goods at a
// building site, probably (kind 0, handles LOCAL_WARE_REQUEST_NOTEs).
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::LocalWareRequest);
    return 0;
}

S3RE_HOOK(0x005764f0, HOUSE_GROUND_REQUEST_AGENT_Construct, ThisFn, Original_HOUSE_GROUND_REQUEST_AGENT_Construct,
          HOUSE_GROUND_REQUEST_AGENT_Construct);

// Purpose: Constructor of the agent that finds ground for a requested
// building, probably (kind 1).
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::HouseGroundRequest);
    return 0;
}

S3RE_HOOK(0x005787a0, HOUSE_REQUEST_AGENT_Construct, ThisFn, Original_HOUSE_REQUEST_AGENT_Construct,
          HOUSE_REQUEST_AGENT_Construct);

// Purpose: Constructor of the agent that handles building requests (kind 2;
// it asks for ground and goods with sub-notes).
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::HouseRequest);
    return 0;
}

S3RE_HOOK(0x005654b0, T_DO_BUILD_HOUSE_AGENT_Construct, ThisFn, Original_T_DO_BUILD_HOUSE_AGENT_Construct,
          T_DO_BUILD_HOUSE_AGENT_Construct);

// Purpose: Constructor of the agent that carries out "build this house"
// orders (kind 4; it turns them into building requests).
std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::DoBuildHouse);
    return 0;
}

// ---------------------------------------------------------------- request notes

S3RE_HOOK(0x0057ab20, LOCAL_WARE_REQUEST_NOTE_Init, Init3Fn, Original_LOCAL_WARE_REQUEST_NOTE_Init,
          LOCAL_WARE_REQUEST_NOTE_Init);

// Purpose: Sets up a request for goods for a building (made by the house
// request agent's slot 3 from its house request).
std::uint32_t __fastcall LOCAL_WARE_REQUEST_NOTE_Init(void* self, void* /*edx*/, std::uint32_t buildingType,
                                                     std::uint32_t x, std::uint32_t y) {
    BLACK_BOARD_NOTE_Init(self, nullptr, Value(NoteType::LocalWareRequest));
    RequestNote* note = AsRequest(self);
    note->building_type = static_cast<std::uint16_t>(buildingType);
    note->x = static_cast<std::uint16_t>(x);
    note->y = static_cast<std::uint16_t>(y);
    return buildingType & 0xffff;  // left in AX by the original
}

S3RE_HOOK(0x00578760, HOUSE_REQUEST_NOTE_Init, Init4Fn, Original_HOUSE_REQUEST_NOTE_Init, HOUSE_REQUEST_NOTE_Init);

// Purpose: Sets up a building request (made by the build-house agent's slot
// 3 from its order).
std::uint32_t __fastcall HOUSE_REQUEST_NOTE_Init(void* self, void* /*edx*/, std::uint32_t buildingType,
                                                std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    SetRequest(self, NoteType::HouseRequest, buildingType, x, y, value);
    return 0;
}

S3RE_HOOK(0x00565470, T_DO_BUILD_HOUSE_NOTE_Init, Init4Fn, Original_T_DO_BUILD_HOUSE_NOTE_Init,
          T_DO_BUILD_HOUSE_NOTE_Init);

// Purpose: Sets up an order to build a house (e.g. a small tower to expand
// the land, 0x56d510).
std::uint32_t __fastcall T_DO_BUILD_HOUSE_NOTE_Init(void* self, void* /*edx*/, std::uint32_t buildingType,
                                                   std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    SetRequest(self, NoteType::DoBuildHouse, buildingType, x, y, value);
    return 0;
}

S3RE_HOOK(0x005801c0, T_DO_BUILD_HOUSE_NOTE_Construct, Init4Fn, Original_T_DO_BUILD_HOUSE_NOTE_Construct,
          T_DO_BUILD_HOUSE_NOTE_Construct);

// Purpose: Constructor of an order to build a house.
std::uint32_t __fastcall T_DO_BUILD_HOUSE_NOTE_Construct(void* self, void* /*edx*/, std::uint32_t buildingType,
                                                        std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    AsRequest(self)->base.vtable = vtables::kDoBuildHouseNote;
    T_DO_BUILD_HOUSE_NOTE_Init(self, nullptr, buildingType, x, y, value);
    return reinterpret_cast<std::uint32_t>(self);
}

// ---------------------------------------------------------------- BLACK_BOARD_NOTE

S3RE_HOOK(0x005801b0, BLACK_BOARD_NOTE_Construct, ThisFn, Original_BLACK_BOARD_NOTE_Construct,
          BLACK_BOARD_NOTE_Construct);

// Purpose: The base notes' constructor (only the vtable).
std::uint32_t __fastcall BLACK_BOARD_NOTE_Construct(void* self, void* /*edx*/) {
    static_cast<BlackBoardNote*>(self)->vtable = vtables::kBlackBoardNote;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00565590, BLACK_BOARD_NOTE_Destroy, VoidThisFn, Original_BLACK_BOARD_NOTE_Destroy,
          BLACK_BOARD_NOTE_Destroy);

// Purpose: The base notes' destructor (only the vtable).
void __fastcall BLACK_BOARD_NOTE_Destroy(void* self, void* /*edx*/) {
    static_cast<BlackBoardNote*>(self)->vtable = vtables::kBlackBoardNote;
}

S3RE_HOOK(0x005655a0, BLACK_BOARD_NOTE_DeletingDestroy, DeletingFn, Original_BLACK_BOARD_NOTE_DeletingDestroy,
          BLACK_BOARD_NOTE_DeletingDestroy);

// Purpose: "Delete this note" of a plain BLACK_BOARD_NOTE (vtable slot 0;
// CComputerPlayer::DeleteNote calls it).
void* __fastcall BLACK_BOARD_NOTE_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    BLACK_BOARD_NOTE_Destroy(self, nullptr);  // inlined in the original
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x0056f6b0, RequestNote_DeletingDestroy, DeletingFn, Original_RequestNote_DeletingDestroy,
          RequestNote_DeletingDestroy);

// Purpose: "Delete this note" of the four request notes (their vtables
// share it): they add nothing to destroy.
void* __fastcall RequestNote_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    BLACK_BOARD_NOTE_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// An object of 0x40 bytes (random, with guard bytes) per side; every
// function on random arguments; compared: the bytes and EAX (where the
// callers use it). The deleting destructors run without bit 0 (the buffer
// is ours; the emulator specs cover the free).

namespace {

constexpr std::size_t kObjectBytes = 0x40;

struct RequestFunctions {
    ThisFn agents[4];
    Init3Fn localWare;
    Init4Fn houseRequest;
    Init4Fn doBuildInit;
    Init4Fn doBuildConstruct;
    ThisFn noteConstruct;
    VoidThisFn noteDestroy;
    DeletingFn noteDeleting;
    DeletingFn requestDeleting;
};

RequestFunctions OriginalRequests() {
    return {{Original_LOCAL_WARE_REQUEST_AGENT_Construct, Original_HOUSE_GROUND_REQUEST_AGENT_Construct,
             Original_HOUSE_REQUEST_AGENT_Construct, Original_T_DO_BUILD_HOUSE_AGENT_Construct},
            Original_LOCAL_WARE_REQUEST_NOTE_Init,
            Original_HOUSE_REQUEST_NOTE_Init,
            Original_T_DO_BUILD_HOUSE_NOTE_Init,
            Original_T_DO_BUILD_HOUSE_NOTE_Construct,
            Original_BLACK_BOARD_NOTE_Construct,
            Original_BLACK_BOARD_NOTE_Destroy,
            Original_BLACK_BOARD_NOTE_DeletingDestroy,
            Original_RequestNote_DeletingDestroy};
}

RequestFunctions OurRequests() {
    return {{&LOCAL_WARE_REQUEST_AGENT_Construct, &HOUSE_GROUND_REQUEST_AGENT_Construct,
             &HOUSE_REQUEST_AGENT_Construct, &T_DO_BUILD_HOUSE_AGENT_Construct},
            &LOCAL_WARE_REQUEST_NOTE_Init,
            &HOUSE_REQUEST_NOTE_Init,
            &T_DO_BUILD_HOUSE_NOTE_Init,
            &T_DO_BUILD_HOUSE_NOTE_Construct,
            &BLACK_BOARD_NOTE_Construct,
            &BLACK_BOARD_NOTE_Destroy,
            &BLACK_BOARD_NOTE_DeletingDestroy,
            &RequestNote_DeletingDestroy};
}

void CompareRequests(test::Context& ctx, const RequestFunctions& candidate, int cases) {
    const RequestFunctions original = OriginalRequests();
    std::vector<std::uint8_t> a(kObjectBytes), b(kObjectBytes);
    constexpr int kOps = 12;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOps));
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        std::uint32_t args[4];
        for (std::uint32_t& arg : args) {
            arg = ctx.rng().Chance(1, 2) ? ctx.rng().Below(64) : ctx.rng().Next32();
        }
        const std::uint32_t flags = ctx.rng().Next32() & ~1u;
        std::uint32_t eaxMask = 0xffffffffu;
        auto run = [&](const RequestFunctions& f, std::uint8_t* object) -> std::uint32_t {
            const auto base = reinterpret_cast<std::uint32_t>(object);
            switch (op) {
                case 0: case 1: case 2: case 3: return f.agents[op](object, nullptr);
                case 4: return f.localWare(object, nullptr, args[0], args[1], args[2]);
                case 5: return f.houseRequest(object, nullptr, args[0], args[1], args[2], args[3]);
                case 6: return f.doBuildInit(object, nullptr, args[0], args[1], args[2], args[3]);
                case 7: return f.doBuildConstruct(object, nullptr, args[0], args[1], args[2], args[3]) - base;
                case 8: return f.noteConstruct(object, nullptr) - base;
                case 9: f.noteDestroy(object, nullptr); return 0;
                case 10: return reinterpret_cast<std::uint32_t>(f.noteDeleting(object, nullptr, flags)) - base;
                default: return reinterpret_cast<std::uint32_t>(f.requestDeleting(object, nullptr, flags)) - base;
            }
        };
        if (op < 4 || op == 9) {
            eaxMask = 0;  // the agents' constructors: the caller's upper half (unused); the destructor: unchanged
        }
        const std::uint32_t ra = run(original, a.data());
        const std::uint32_t rb = run(candidate, b.data());
        bool same = S3RE_EXPECT_EQ(ra & eaxMask, rb & eaxMask);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
}

// Wrong: HOUSE_REQUEST_NOTE's init forgets to clear +0x26.
std::uint32_t __fastcall WrongHouseRequestInit(void* self, void* /*edx*/, std::uint32_t type, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t value) {
    const std::uint16_t keep = AsRequest(self)->unk26;
    HOUSE_REQUEST_NOTE_Init(self, nullptr, type, x, y, value);
    AsRequest(self)->unk26 = keep;
    return 0;
}

// Wrong: the HOUSE_REQUEST agent's kind.
std::uint32_t __fastcall WrongAgentKind(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::HouseGroundRequest);
    return 0;
}

}  // namespace

S3RE_TEST(AiRequestNotes_MatchOriginal) {
    CompareRequests(ctx, OurRequests(), 20000);
}

S3RE_TEST(AiRequestNotes_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        RequestFunctions wrong = OurRequests();
        change(wrong);
        CompareRequests(quiet, wrong, 5000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](RequestFunctions& f) { f.houseRequest = &WrongHouseRequestInit; });
    probe([](RequestFunctions& f) { f.agents[2] = &WrongAgentKind; });
    probe([](RequestFunctions& f) { f.doBuildInit = &HOUSE_REQUEST_NOTE_Init; });  // type 2 instead of 4
}

}  // namespace s3re::game
