// Small virtual functions of the computer players' agents (computer players,
// session 55; re/computer_players.md "Agents"; agent_slot in ai_layout.h):
// the note types the request agents handle (slot 2), the ground request
// agent's TakeNote (slot 3), the economy agent's slot 11 and the
// BLACK_BOARD_AGENT destructors (slot 0).
//
//   0x005654c0 T_DO_BUILD_HOUSE_AGENT::GetNoteTypes(u16* types), ret 4:
//              *types = 4 (T_DO_BUILD_HOUSE_NOTE); AX = 1 (the upper half is
//              the pointer's: EAX held it).
//   0x00576500 HOUSE_GROUND_REQUEST_AGENT::GetNoteTypes: the same, type 1.
//   0x005787b0 HOUSE_REQUEST_AGENT::GetNoteTypes: type 2.
//   0x0057ab60 LOCAL_WARE_REQUEST_AGENT::GetNoteTypes: type 0.
//   0x00576510 HOUSE_GROUND_REQUEST_AGENT::TakeNote(note), ret 4 (also the
//              LOCAL_WARE_REQUEST_AGENT's slot 3): note->handler = the
//              agent's kind (+4), CComputerPlayer::ActivateNoteOf
//              (g_aiPlayer, note); AX = 0 (the upper half is
//              ActivateNoteOf's).
//   0x00567870 T_ECONOMY_AGENT::vf11(arg), ret 4: calls the empty NoOp_ret4
//              0x582530 (an inlined base call) with the argument, then
//              clears dword +0x14; EAX = the argument.
//   0x0056fac0 BLACK_BOARD_AGENT::~BLACK_BOARD_AGENT(): stores the base
//              vtable 0x65a828 (EAX untouched).
//   0x0056fad0 BLACK_BOARD_AGENT::subclass_scalar_deleting_destructor
//              (flags), slot 0 of the ten derived agents: the base
//              destructor, operator delete if bit 0; EAX = this.
//   0x0056faa0 BLACK_BOARD_AGENT::scalar_deleting_destructor(flags): the
//              same with the base destructor inlined.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kActivateNoteOf = 0x005515c0;  // CComputerPlayer::ActivateNoteOf (thiscall)
constexpr std::uintptr_t kNoOpRet4 = 0x00582530;        // NoOp_ret4 (an empty base slot)
constexpr std::uintptr_t kAgentDestructor = 0x0056fac0; // BLACK_BOARD_AGENT::~BLACK_BOARD_AGENT

// GetNoteTypes: one type; AX = 1 over the pointer's upper half.
std::uint32_t WriteOneType(std::uint16_t* types, NoteType type) {
    *types = Value(type);
    return (reinterpret_cast<std::uint32_t>(types) & 0xffff0000u) | 1;
}

// EconomyAgent +0x14 (the first dword of unk14).
struct AgentDword14 {
    std::uint8_t unk00[0x14];
    std::uint32_t value;
};
static_assert(offsetof(AgentDword14, value) == 0x14);

}  // namespace

using TypesFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint16_t* types);
using NoteFn = std::uint32_t(__fastcall*)(void* self, void* edx, BlackBoardNote* note);
using ArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t arg);
using VoidFn = void(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_GetNoteTypes(void* self, void* edx, std::uint16_t* types);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes(void* self, void* edx, std::uint16_t* types);
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_GetNoteTypes(void* self, void* edx, std::uint16_t* types);
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_GetNoteTypes(void* self, void* edx, std::uint16_t* types);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_TakeNote(void* self, void* edx, BlackBoardNote* note);
std::uint32_t __fastcall T_ECONOMY_AGENT_vf11(void* self, void* edx, std::uint32_t arg);
void __fastcall BLACK_BOARD_AGENT_Destructor(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_AGENT_SubclassDeletingDestructor(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall BLACK_BOARD_AGENT_DeletingDestructor(void* self, void* edx, std::uint32_t flags);

S3RE_HOOK(0x005654c0, T_DO_BUILD_HOUSE_AGENT_GetNoteTypes, TypesFn, Original_T_DO_BUILD_HOUSE_AGENT_GetNoteTypes,
          T_DO_BUILD_HOUSE_AGENT_GetNoteTypes);
S3RE_HOOK(0x00576500, HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes, TypesFn,
          Original_HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes, HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes);
S3RE_HOOK(0x005787b0, HOUSE_REQUEST_AGENT_GetNoteTypes, TypesFn, Original_HOUSE_REQUEST_AGENT_GetNoteTypes,
          HOUSE_REQUEST_AGENT_GetNoteTypes);
S3RE_HOOK(0x0057ab60, LOCAL_WARE_REQUEST_AGENT_GetNoteTypes, TypesFn, Original_LOCAL_WARE_REQUEST_AGENT_GetNoteTypes,
          LOCAL_WARE_REQUEST_AGENT_GetNoteTypes);
S3RE_HOOK(0x00576510, HOUSE_GROUND_REQUEST_AGENT_TakeNote, NoteFn, Original_HOUSE_GROUND_REQUEST_AGENT_TakeNote,
          HOUSE_GROUND_REQUEST_AGENT_TakeNote);
S3RE_HOOK(0x00567870, T_ECONOMY_AGENT_vf11, ArgFn, Original_T_ECONOMY_AGENT_vf11, T_ECONOMY_AGENT_vf11);
S3RE_HOOK(0x0056fac0, BLACK_BOARD_AGENT_Destructor, VoidFn, Original_BLACK_BOARD_AGENT_Destructor,
          BLACK_BOARD_AGENT_Destructor);
S3RE_HOOK(0x0056fad0, BLACK_BOARD_AGENT_SubclassDeletingDestructor, ArgFn,
          Original_BLACK_BOARD_AGENT_SubclassDeletingDestructor, BLACK_BOARD_AGENT_SubclassDeletingDestructor);
S3RE_HOOK(0x0056faa0, BLACK_BOARD_AGENT_DeletingDestructor, ArgFn, Original_BLACK_BOARD_AGENT_DeletingDestructor,
          BLACK_BOARD_AGENT_DeletingDestructor);

// Purpose: Tells the computer player which notes the build agent handles
// (T_DO_BUILD_HOUSE_NOTE).
std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_GetNoteTypes(void* /*self*/, void* /*edx*/, std::uint16_t* types) {
    return WriteOneType(types, NoteType::DoBuildHouse);
}

// Purpose: The same for the ground request agent (HOUSE_GROUND_REQUEST_NOTE).
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes(void* /*self*/, void* /*edx*/,
                                                                 std::uint16_t* types) {
    return WriteOneType(types, NoteType::HouseGroundRequest);
}

// Purpose: The same for the house request agent (HOUSE_REQUEST_NOTE).
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_GetNoteTypes(void* /*self*/, void* /*edx*/, std::uint16_t* types) {
    return WriteOneType(types, NoteType::HouseRequest);
}

// Purpose: The same for the local ware request agent (LOCAL_WARE_REQUEST_NOTE).
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_GetNoteTypes(void* /*self*/, void* /*edx*/,
                                                               std::uint16_t* types) {
    return WriteOneType(types, NoteType::LocalWareRequest);
}

// Purpose: A request agent takes a new note: records itself as the note's
// handler and puts the note on the active list.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_TakeNote(void* self, void* /*edx*/, BlackBoardNote* note) {
    note->handler = static_cast<const BlackBoardAgent*>(self)->kind;
    using ActivateFn = std::uint32_t(__fastcall*)(ComputerPlayer* player, void* edx, BlackBoardNote* note);
    const std::uint32_t result = reinterpret_cast<ActivateFn>(kActivateNoteOf)(g_aiPlayer, nullptr, note);
    return result & 0xffff0000u;  // AX = 0
}

// Purpose: Unknown (slot 11 of the economy agent): passes the argument to
// the empty base slot and clears the agent's dword +0x14.
std::uint32_t __fastcall T_ECONOMY_AGENT_vf11(void* self, void* /*edx*/, std::uint32_t arg) {
    using Fn = void(__fastcall*)(void* self, void* edx, std::uint32_t arg);
    reinterpret_cast<Fn>(kNoOpRet4)(self, nullptr, arg);
    static_cast<AgentDword14*>(self)->value = 0;
    return arg;
}

// Purpose: The computer-player agent base class's destructor (restores the
// base vtable).
void __fastcall BLACK_BOARD_AGENT_Destructor(void* self, void* /*edx*/) {
    *static_cast<std::uint32_t*>(self) = vtables::kBlackBoardAgent;
}

// Purpose: "Delete this agent" of the ten derived agents.
std::uint32_t __fastcall BLACK_BOARD_AGENT_SubclassDeletingDestructor(void* self, void* /*edx*/,
                                                                      std::uint32_t flags) {
    reinterpret_cast<VoidFn>(kAgentDestructor)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return reinterpret_cast<std::uint32_t>(self);
}

// Purpose: "Delete this agent" of the base class itself.
std::uint32_t __fastcall BLACK_BOARD_AGENT_DeletingDestructor(void* self, void* /*edx*/, std::uint32_t flags) {
    *static_cast<std::uint32_t*>(self) = vtables::kBlackBoardAgent;  // the destructor, inlined
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return reinterpret_cast<std::uint32_t>(self);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Random agents, notes and pointers; ActivateNoteOf stubbed (recorded with
// its arguments). The deleting destructors get blocks of the game's heap
// when bit 0 is set (then only EAX is compared: the blocks are freed).
// Compared: EAX, the touched memory, the stub calls.

namespace {

struct StubCall {
    std::uint32_t callee, self, arg;
    bool operator==(const StubCall& other) const {
        return callee == other.callee && self == other.self && arg == other.arg;
    }
};

struct SlotFunctions {
    TypesFn types[4];
    NoteFn takeNote;
    ArgFn vf11;
    VoidFn destructor;
    ArgFn subclassDelete, baseDelete;
};

SlotFunctions OriginalSlots() {
    return {{Original_T_DO_BUILD_HOUSE_AGENT_GetNoteTypes, Original_HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes,
             Original_HOUSE_REQUEST_AGENT_GetNoteTypes, Original_LOCAL_WARE_REQUEST_AGENT_GetNoteTypes},
            Original_HOUSE_GROUND_REQUEST_AGENT_TakeNote,
            Original_T_ECONOMY_AGENT_vf11,
            Original_BLACK_BOARD_AGENT_Destructor,
            Original_BLACK_BOARD_AGENT_SubclassDeletingDestructor,
            Original_BLACK_BOARD_AGENT_DeletingDestructor};
}

SlotFunctions OurSlots() {
    return {{&T_DO_BUILD_HOUSE_AGENT_GetNoteTypes, &HOUSE_GROUND_REQUEST_AGENT_GetNoteTypes,
             &HOUSE_REQUEST_AGENT_GetNoteTypes, &LOCAL_WARE_REQUEST_AGENT_GetNoteTypes},
            &HOUSE_GROUND_REQUEST_AGENT_TakeNote,
            &T_ECONOMY_AGENT_vf11,
            &BLACK_BOARD_AGENT_Destructor,
            &BLACK_BOARD_AGENT_SubclassDeletingDestructor,
            &BLACK_BOARD_AGENT_DeletingDestructor};
}

void CompareSlots(test::Context& ctx, const SlotFunctions& candidate, int cases) {
    const SlotFunctions original = OriginalSlots();
    ComputerPlayer* const savedPlayer = g_aiPlayer;
    std::vector<StubCall>* log = nullptr;
    std::uint32_t activateResult = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<1, kActivateNoteOf>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        log->push_back({kActivateNoteOf, self, args[0]});
        return activateResult;
    });
    constexpr std::size_t kBytes = 0x40;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        std::uint8_t a[kBytes], b[kBytes];
        rng.Fill(a, kBytes);
        std::memcpy(b, a, kBytes);
        g_aiPlayer = reinterpret_cast<ComputerPlayer*>(rng.Next32());
        activateResult = rng.Next32();
        const std::uint32_t arg = rng.Chance(1, 3) ? rng.Below(4) : rng.Next32();
        const int op = static_cast<int>(rng.Below(9));
        std::vector<StubCall> logA, logB;
        const auto run = [&](const SlotFunctions& f, std::uint8_t* bytes) -> std::uint32_t {
            switch (op) {
            case 0:
            case 1:
            case 2:
            case 3: return f.types[op](nullptr, nullptr, reinterpret_cast<std::uint16_t*>(bytes + 2));
            case 4: return f.takeNote(bytes + 0x20, nullptr, reinterpret_cast<BlackBoardNote*>(bytes));
            case 5: return f.vf11(bytes, nullptr, arg);
            case 6: f.destructor(bytes, nullptr); return 0;
            case 7: return f.subclassDelete(bytes, nullptr, arg) - reinterpret_cast<std::uint32_t>(bytes);
            default: return f.baseDelete(bytes, nullptr, arg) - reinterpret_cast<std::uint32_t>(bytes);
            }
        };
        if (op >= 7 && (arg & 1) != 0) {
            auto* blockA = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kBytes));
            auto* blockB = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kBytes));
            std::memcpy(blockA, a, kBytes);
            std::memcpy(blockB, a, kBytes);
            const ArgFn fa = op == 7 ? original.subclassDelete : original.baseDelete;
            const ArgFn fb = op == 7 ? candidate.subclassDelete : candidate.baseDelete;
            const std::uint32_t da = fa(blockA, nullptr, arg) - reinterpret_cast<std::uint32_t>(blockA);
            const std::uint32_t db = fb(blockB, nullptr, arg) - reinterpret_cast<std::uint32_t>(blockB);
            if (!S3RE_EXPECT_EQ(da, db)) {
                ctx.Fail(__FILE__, __LINE__, "  case %d, op %d (deleting)", i, op);
            }
            continue;
        }
        log = &logA;
        std::uint32_t ra = run(original, a);
        log = &logB;
        std::uint32_t rb = run(candidate, b);
        if (op <= 3) {  // the pointer's upper half
            ra -= reinterpret_cast<std::uint32_t>(a) & 0xffff0000u;
            rb -= reinterpret_cast<std::uint32_t>(b) & 0xffff0000u;
        }
        for (StubCall& call : logA) {  // the buffers' addresses differ
            call.arg -= call.arg >= reinterpret_cast<std::uint32_t>(a) &&
                                call.arg < reinterpret_cast<std::uint32_t>(a) + kBytes
                            ? reinterpret_cast<std::uint32_t>(a)
                            : 0;
        }
        for (StubCall& call : logB) {
            call.arg -= call.arg >= reinterpret_cast<std::uint32_t>(b) &&
                                call.arg < reinterpret_cast<std::uint32_t>(b) + kBytes
                            ? reinterpret_cast<std::uint32_t>(b)
                            : 0;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a, b, kBytes);
        same &= S3RE_EXPECT(logA == logB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
        stubs.Take();
    }
    g_aiPlayer = savedPlayer;
}

std::uint32_t __fastcall WrongTypes(void* /*self*/, void* /*edx*/, std::uint16_t* types) {
    // Wrong: the house request's type.
    return WriteOneType(types, NoteType::HouseRequest);
}

std::uint32_t __fastcall WrongBaseDelete(void* self, void* /*edx*/, std::uint32_t flags) {
    // Wrong: keeps the subclass vtable.
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return reinterpret_cast<std::uint32_t>(self);
}

}  // namespace

S3RE_TEST(AiAgentSlots_MatchOriginal) {
    CompareSlots(ctx, OurSlots(), 3000);
}

S3RE_TEST(AiAgentSlots_ComparisonDetectsWrongImplementation) {
    SlotFunctions wrong = OurSlots();
    wrong.types[0] = &WrongTypes;
    SlotFunctions wrong2 = OurSlots();
    wrong2.baseDelete = &WrongBaseDelete;
    for (const SlotFunctions& w : {wrong, wrong2}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSlots(quiet, w, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
