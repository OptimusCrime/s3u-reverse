// Watcom vector instantiations: constructors and destructors (layout and
// shared code in vector_base.h). Each address is its own copy of the
// template code; only the vtables and the element size differ, so every
// hook below is a short wrapper around the shared helper.
//
// Destructors of vectors with trivially destructible elements (thiscall,
// no arguments): vtable = WCBareVectorBase<T>; if data: the empty error
// hook 0x59f200, operator delete(data); vtable = WCExcept 0x655438. EAX =
// operator delete's, or 0 without a block. (The derived classes' own
// vtables are not stored: the first store is the bare base's.)
//   address    WCBareVectorBase<T> vtable        class (from its scalar deleting destructor or the vtable)
//   0x004143b0 kWCBareVectorBase_int                    WCValSortedVector<int>
//   0x00427de0 kWCBareVectorBase_HFONT                  WCValOrderedVector<HFONT>
//   0x00427e40 kWCBareVectorBase_CScreenElementP        WCValOrderedVector<CScreenElement*>
//   0x00437570 kWCBareVectorBase_int                    WCValOrderedVector<int>
//   0x0044caa0 kWCBareVectorBase_uint                   WCValOrderedVector<unsigned int>
//   0x0044cca0 kWCBareVectorBase_CStatusMessage         WCValOrderedVector<LobbyClient::CStatusMessage>
//   0x00457a10 kWCBareVectorBase_uint                   WCValSortedVector<unsigned int>
//   0x00462870 kWCBareVectorBase_ushort                 WCValOrderedVector<unsigned short>
//   0x00462b00 kWCBareVectorBase_voidp                  WCPtrSortedVector<DirectX::CPlayer>
//   0x004721c0 kWCBareVectorBase_voidp                  WCPtrSortedVector<CExecuteType>
//   0x00472240 kWCBareVectorBase_voidp                  WCPtrOrderedVector<CRequestType>
//   0x004722e0 kWCBareVectorBase_voidp                  WCPtrSortedVector<CAnnounceType>
//   0x00472360 kWCBareVectorBase_voidp                  WCPtrOrderedVector<CAcknowledgeType>
//   0x00472440 kWCBareVectorBase_ulong                  WCValSortedVector<unsigned long>
//   0x004990c0 kWCBareVectorBase_GameChunkDescription   WCValOrderedVector<GameChunkDescription>
//   0x004992f0 kWCBareVectorBase_long                   WCValOrderedVector<long>
//   0x0044e240 kWCBareVectorBase_CStatusMessage         WCBareVectorBase<LobbyClient::CStatusMessage>
//   0x00462eb0 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00462f10 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00466190 kWCBareVectorBase_uint                   WCBareVectorBase<unsigned int>
//   0x00472660 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00472bb0 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00472c10 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00472e00 kWCBareVectorBase_voidp                  WCBareVectorBase<void*>
//   0x00473000 kWCBareVectorBase_ulong                  WCBareVectorBase<unsigned long>
//   0x00473060 kWCBareVectorBase_ulong                  WCBareVectorBase<unsigned long>
//   0x004a1140 kWCBareVectorBase_ushort                 WCBareVectorBase<unsigned short>
//   0x004145f0 kWCBareVectorBase_int                        (wip/watcom-r6)
//   0x0042f8c0 kWCBareVectorBase_int                        (wip/watcom-r6)
//   0x00499630 kWCBareVectorBase_int                        (wip/watcom-r6)
//   0x0049b7f0 kWCBareVectorBase_GameChunkDescription       (wip/watcom-r6)
//   0x00541690 kWCBareVectorBase_CGamePlayerStatus          (wip/watcom-r6)
//   0x005b7c10 kWCBareVectorBase_CDisplayMode               (wip/watcom-r6)
//   0x005de270 kWCBareVectorBase_uint                       (wip/watcom-r6)
//   0x005de2d0 kWCBareVectorBase_uint                       (wip/watcom-r6)
//   0x005f3de0 kWCBareVectorBase_voidp                      (wip/watcom-r6)
//   0x0061aab0 kWCBareVectorBase_voidp                      (wip/watcom-r6)
//   0x005bdf70 kWCBareVectorBase_voidp                      (libs-r7)
//   0x005bdbd0 kWCBareVectorBase_NetPlayerIndex             (its elements' destructor is the
//              library's empty hook 0x59f200, called per element: left out like the hook)
//
// Destructors of vectors of Dirk::CPlayer (0xcc bytes; our GamePlayerSetup):
// the same, but each of the `filled` elements is destroyed first by
// ~GamePlayerSetup 0x49ec80 (filled and data re-read per element):
//   0x004141a0 WCValOrderedVector<Dirk::CPlayer>, 0x004a11a0 and 0x004146d0 the same body.
// The destructor of the sorted vector of teams (Dirk::CTeam, 0x40 bytes),
// the same with ~CTeam 0x4a04d0:
//   0x00414240 WCValSortedVector<Dirk::CTeam>.
//
// (capacity, grow) constructors, ret 8, EAX = this: +4 = 0, vtable =
// WCBareVectorBase<T>, capacity, filled = 0, data = operator
// new(capacity * size) (none for 0; capacity = 0 if it returns null),
// length = 0, +0x18 = grow, then the class's own vtable.
//   address    size  T                     class
//   0x00437630    4  int                   WCOrderedVectorBase_int
//   0x0044ce10    4  uint                  WCVectorBase_uint
//   0x00462c70    4  voidp                 WCSortedVectorBase_voidp
//   0x00466110    4  uint                  WCOrderedVectorBase_uint
//   0x00472700    4  ulong                 WCSortedVectorBase_ulong
//   0x004727a0    4  voidp                 WCOrderedVectorBase_voidp
//   0x004728a0    4  voidp                 WCPtrSortedVector_CExecuteType
//   0x00472920    4  ulong                 WCVectorBase_ulong
//   0x0044ce90   32  CChatMessage          WCVectorBase_CChatMessage
//   0x00457fb0   32  CChatMessage          WCOrderedVectorBase_CChatMessage
//   0x00457f20  284  CGroup                WCSortedVectorBase_CGroup
//   0x00458500  284  CGroup                WCVectorBase_CGroup
//   0x00457ea0   16  String                WCOrderedVectorBase_String
//   0x00472820   64  CConnectionShortcut   WCVectorBase_CConnectionShortcut
//   0x00462cf0   96  CRunningSession       WCVectorBase_CRunningSession
//   0x00414410    4  int                   WCSortedVectorBase_int
//   0x00427d60    4  HFONT                 WCValOrderedVector_HFONT
//   0x00468330    4  voidp                 WCVectorBase_voidp
//   0x00499580    4  long                  WCOrderedVectorBase_long
//   0x00609f50    4  uint                  WCSortedVectorBase_uint
//   0x00414510   16  String                WCVectorBase_String
//   0x004993f0   16  String                WCSortedVectorBase_String
//   0x005fda00   16  String                WCValOrderedVector_String
//   0x005dde00   32  CLobbyServer          WCSortedVectorBase_CLobbyServer
//   0x005de010   32  CServerData           WCVectorBase_CServerData
//   0x005de090   32  CLobbyServer          WCVectorBase_CLobbyServer
//   0x005f3f40   32  CServerData           WCOrderedVectorBase_CServerData
//   0x005dde80   84  LobbyPlayer           WCVectorBase_LobbyPlayer
//   0x00609ff0   36  CLMMessageBlock       WCOrderedVectorBase_CLMMessageBlock
//   0x00609e50   20  CLMGeneric            WCOrderedVectorBase_CLMGeneric
//   0x0060a070   20  CLMGeneric            WCVectorBase_CLMGeneric
//   0x005ddf90   36  CSocketAddress        WCVectorBase_CSocketAddress
//   0x005ddf10   24  CImageDescription     WCVectorBase_CImageDescription
//   0x005b7ec0   48  CDirectDrawDevice     WCVectorBase_CDirectDrawDevice (libs-r7)
//   0x005b7f40  108  CDisplayMode          WCVectorBase_CDisplayMode (libs-r7)
//
// (capacity) constructors, ret 4, EAX = this: the bare part only.
//   address    size  T
//   0x00437c90   64  CTeam
//   0x00458a60   32  CChatMessage
//   0x004586f0  284  CGroup
//   0x004377a0  204  CPlayer
//   0x00428c10   16  String
//   0x00428e70    4  CScreenElement*
//   0x00429080   32  CListView::CColumn
//   0x00429410   20  CListView::CLine
//   0x005de130    4  uint
//   0x004683b0   64  CConnectionShortcut
//   0x005de330   84  LobbyPlayer
//   0x005df8d0  380  CPrivateChat
//   0x005de570   24  CImageDescription
//   0x00499690   96  CRunningSession (libs-r7)
//
// Copy constructors of vectors of unsigned ints (from) ret 4, EAX = this
// (CopyVector in vector_base.h; each element copied as a dword, the
// source's block re-read per element):
//   0x005f3cc0 WCValSortedVector<unsigned int>   0x6571b8
//   0x005f3e40 WCValOrderedVector<unsigned int>  0x656d44

#include "watcom/vector_base.h"

#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace vt = game::vtables;

using DestructorFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx);
using ConstructorFn = VectorBase*(__fastcall*)(VectorBase* self, void* edx, std::uint32_t capacity,
                                               std::uint32_t grow);
using BareConstructorFn = VectorBase*(__fastcall*)(VectorBase* self, void* edx, std::uint32_t capacity);

namespace {

std::uint32_t DestroyTrivialVector(VectorBase* self, std::uint32_t vtable) {
    return DestroyVector(self, vtable, vt::kWCExcept, 0, [](std::uint32_t) {});
}

constexpr std::uintptr_t kDestroyPlayer = 0x0049ec80;  // GamePlayerSetup::~GamePlayerSetup (ours)
constexpr std::uint32_t kPlayerSize = 0xcc;

std::uint32_t DestroyPlayerVector(VectorBase* self) {
    return DestroyVector(self, vt::kWCBareVectorBase_CPlayer, vt::kWCExcept, kPlayerSize, [](std::uint32_t player) {
        reinterpret_cast<void(__fastcall*)(std::uint32_t, void*)>(kDestroyPlayer)(player, nullptr);
    });
}

constexpr std::uintptr_t kDestroyTeam = 0x004a04d0;  // Dirk::CTeam::~CTeam (ours)
constexpr std::uint32_t kTeamSize = 0x40;

std::uint32_t DestroyTeamVector(VectorBase* self) {
    return DestroyVector(self, vt::kWCBareVectorBase_CTeam, vt::kWCExcept, kTeamSize, [](std::uint32_t team) {
        reinterpret_cast<void(__fastcall*)(std::uint32_t, void*)>(kDestroyTeam)(team, nullptr);
    });
}

}  // namespace

// Purpose: Destructors of the game's Watcom vectors of plain values or
// pointers (UI child lists, fonts, ids, chunk lists, network request
// lists, ...): free the element block.
std::uint32_t __fastcall VectorDestructor_4143b0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int);
}
std::uint32_t __fastcall VectorDestructor_427de0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_HFONT);
}
std::uint32_t __fastcall VectorDestructor_427e40(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_CScreenElementP);
}
std::uint32_t __fastcall VectorDestructor_437570(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int);
}
std::uint32_t __fastcall VectorDestructor_44caa0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_uint);
}
std::uint32_t __fastcall VectorDestructor_44cca0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_CStatusMessage);
}
std::uint32_t __fastcall VectorDestructor_457a10(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_uint);
}
std::uint32_t __fastcall VectorDestructor_462870(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_ushort);
}
std::uint32_t __fastcall VectorDestructor_462b00(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_4721c0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472240(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_4722e0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472360(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472440(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_ulong);
}
std::uint32_t __fastcall VectorDestructor_4990c0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_GameChunkDescription);
}
std::uint32_t __fastcall VectorDestructor_4992f0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_long);
}
std::uint32_t __fastcall VectorDestructor_44e240(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_CStatusMessage);
}
std::uint32_t __fastcall VectorDestructor_462eb0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_462f10(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_466190(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_uint);
}
std::uint32_t __fastcall VectorDestructor_472660(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472bb0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472c10(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_472e00(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_473000(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_ulong);
}
std::uint32_t __fastcall VectorDestructor_473060(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_ulong);
}
std::uint32_t __fastcall VectorDestructor_4a1140(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_ushort);
}
std::uint32_t __fastcall VectorDestructor_4145f0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int);
}
std::uint32_t __fastcall VectorDestructor_42f8c0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int);
}
std::uint32_t __fastcall VectorDestructor_499630(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int);
}
std::uint32_t __fastcall VectorDestructor_49b7f0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_GameChunkDescription);
}
std::uint32_t __fastcall VectorDestructor_541690(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_CGamePlayerStatus);
}
std::uint32_t __fastcall VectorDestructor_5b7c10(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_CDisplayMode);
}
std::uint32_t __fastcall VectorDestructor_5de270(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_uint);
}
std::uint32_t __fastcall VectorDestructor_5de2d0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_uint);
}
std::uint32_t __fastcall VectorDestructor_5f3de0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_61aab0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_5bdf70(VectorBase* self, void*) {  // (libs-r7; was a "constructor")
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_voidp);
}
std::uint32_t __fastcall VectorDestructor_5bdbd0(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_NetPlayerIndex);
}
// The network's request-table pairs <key, WCPtrSortedVector<CExecuteType>>:
// the pair's destructor destroys the vector at +4.
std::uint32_t __fastcall VectorDestructor_473290(VectorBase* self, void*) {
    return DestroyTrivialVector(reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(self) + 4),
                                vt::kWCBareVectorBase_voidp);
}

// Purpose: Destructors of the vectors of player setups (Dirk::CPlayer):
// destroy each setup, then free the block.
std::uint32_t __fastcall VectorDestructor_4141a0(VectorBase* self, void*) {
    return DestroyPlayerVector(self);
}
std::uint32_t __fastcall VectorDestructor_4a11a0(VectorBase* self, void*) {
    return DestroyPlayerVector(self);
}
std::uint32_t __fastcall VectorDestructor_4146d0(VectorBase* self, void*) {
    return DestroyPlayerVector(self);
}

// Purpose: Destructor of the game settings' sorted vector of teams: destroy
// each team, then free the block.
std::uint32_t __fastcall VectorDestructor_414240(VectorBase* self, void*) {
    return DestroyTeamVector(self);
}

// Purpose: Constructors of Watcom vectors with room for `capacity`
// elements and a growth step (the elements are constructed later, as they
// are added).
VectorBase* __fastcall VectorConstructor_437630(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_int, vt::kWCOrderedVectorBase_int);
}
VectorBase* __fastcall VectorConstructor_44ce10(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_uint, vt::kWCVectorBase_uint);
}
VectorBase* __fastcall VectorConstructor_462c70(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_voidp, vt::kWCSortedVectorBase_voidp);
}
VectorBase* __fastcall VectorConstructor_466110(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_uint, vt::kWCOrderedVectorBase_uint);
}
VectorBase* __fastcall VectorConstructor_472700(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_ulong, vt::kWCSortedVectorBase_ulong);
}
VectorBase* __fastcall VectorConstructor_4727a0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_voidp, vt::kWCOrderedVectorBase_voidp);
}
VectorBase* __fastcall VectorConstructor_4728a0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_voidp, vt::kWCPtrSortedVector_CExecuteType);
}
VectorBase* __fastcall VectorConstructor_472920(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_ulong, vt::kWCVectorBase_ulong);
}
VectorBase* __fastcall VectorConstructor_44ce90(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CChatMessage, vt::kWCVectorBase_CChatMessage);
}
VectorBase* __fastcall VectorConstructor_457fb0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CChatMessage, vt::kWCOrderedVectorBase_CChatMessage);
}
VectorBase* __fastcall VectorConstructor_457f20(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 284, vt::kWCBareVectorBase_CGroup, vt::kWCSortedVectorBase_CGroup);
}
VectorBase* __fastcall VectorConstructor_458500(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 284, vt::kWCBareVectorBase_CGroup, vt::kWCVectorBase_CGroup);
}
VectorBase* __fastcall VectorConstructor_457ea0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 16, vt::kWCBareVectorBase_String, vt::kWCOrderedVectorBase_String);
}
VectorBase* __fastcall VectorConstructor_472820(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 64, vt::kWCBareVectorBase_CConnectionShortcut, vt::kWCVectorBase_CConnectionShortcut);
}
VectorBase* __fastcall VectorConstructor_462cf0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 96, vt::kWCBareVectorBase_CRunningSession, vt::kWCVectorBase_CRunningSession);
}
VectorBase* __fastcall VectorConstructor_414410(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_int, vt::kWCSortedVectorBase_int);
}
VectorBase* __fastcall VectorConstructor_427d60(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_HFONT, vt::kWCValOrderedVector_HFONT);
}
VectorBase* __fastcall VectorConstructor_468330(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_voidp, vt::kWCVectorBase_voidp);
}
VectorBase* __fastcall VectorConstructor_499580(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_long, vt::kWCOrderedVectorBase_long);
}
VectorBase* __fastcall VectorConstructor_609f50(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 4, vt::kWCBareVectorBase_uint, vt::kWCSortedVectorBase_uint);
}
VectorBase* __fastcall VectorConstructor_414510(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 16, vt::kWCBareVectorBase_String, vt::kWCVectorBase_String);
}
VectorBase* __fastcall VectorConstructor_4993f0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 16, vt::kWCBareVectorBase_String, vt::kWCSortedVectorBase_String);
}
VectorBase* __fastcall VectorConstructor_5fda00(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 16, vt::kWCBareVectorBase_String, vt::kWCValOrderedVector_String);
}
VectorBase* __fastcall VectorConstructor_5dde00(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CLobbyServer, vt::kWCSortedVectorBase_CLobbyServer);
}
VectorBase* __fastcall VectorConstructor_5de010(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CServerData, vt::kWCVectorBase_CServerData);
}
VectorBase* __fastcall VectorConstructor_5de090(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CLobbyServer, vt::kWCVectorBase_CLobbyServer);
}
VectorBase* __fastcall VectorConstructor_5f3f40(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CServerData, vt::kWCOrderedVectorBase_CServerData);
}
VectorBase* __fastcall VectorConstructor_5dde80(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x54, vt::kWCBareVectorBase_LobbyPlayer, vt::kWCVectorBase_LobbyPlayer);
}
VectorBase* __fastcall VectorConstructor_609ff0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x24, vt::kWCBareVectorBase_CLMMessageBlock, vt::kWCOrderedVectorBase_CLMMessageBlock);
}
VectorBase* __fastcall VectorConstructor_609e50(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x14, vt::kWCBareVectorBase_CLMGeneric, vt::kWCOrderedVectorBase_CLMGeneric);
}
VectorBase* __fastcall VectorConstructor_60a070(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x14, vt::kWCBareVectorBase_CLMGeneric, vt::kWCVectorBase_CLMGeneric);
}
VectorBase* __fastcall VectorConstructor_5ddf90(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x24, vt::kWCBareVectorBase_CSocketAddress, vt::kWCVectorBase_CSocketAddress);
}
VectorBase* __fastcall VectorConstructor_5ddf10(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x18, vt::kWCBareVectorBase_CImageDescription, vt::kWCVectorBase_CImageDescription);
}
VectorBase* __fastcall VectorConstructor_5b7ec0(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x30, vt::kWCBareVectorBase_CDirectDrawDevice,
                           vt::kWCVectorBase_CDirectDrawDevice);
}
VectorBase* __fastcall VectorConstructor_5b7f40(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 0x6c, vt::kWCBareVectorBase_CDisplayMode, vt::kWCVectorBase_CDisplayMode);
}

// Purpose: Constructors of the bare part of a Watcom vector (room for
// `capacity` elements); the derived constructors inlined around them set
// the rest.
VectorBase* __fastcall BareVectorConstructor_437c90(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 64, vt::kWCBareVectorBase_CTeam);
}
VectorBase* __fastcall BareVectorConstructor_458a60(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 32, vt::kWCBareVectorBase_CChatMessage);
}
VectorBase* __fastcall BareVectorConstructor_4586f0(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 284, vt::kWCBareVectorBase_CGroup);
}
VectorBase* __fastcall BareVectorConstructor_4377a0(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 204, vt::kWCBareVectorBase_CPlayer);
}
VectorBase* __fastcall BareVectorConstructor_428c10(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 16, vt::kWCBareVectorBase_String);
}
VectorBase* __fastcall BareVectorConstructor_428e70(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 4, vt::kWCBareVectorBase_CScreenElementP);
}
VectorBase* __fastcall BareVectorConstructor_429080(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 32, vt::kWCBareVectorBase_CColumn);
}
VectorBase* __fastcall BareVectorConstructor_429410(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 20, vt::kWCBareVectorBase_CLine);
}
VectorBase* __fastcall BareVectorConstructor_5de130(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 4, vt::kWCBareVectorBase_uint);
}
VectorBase* __fastcall BareVectorConstructor_4683b0(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 64, vt::kWCBareVectorBase_CConnectionShortcut);
}
VectorBase* __fastcall BareVectorConstructor_499690(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 0x60, vt::kWCBareVectorBase_CRunningSession);
}
VectorBase* __fastcall BareVectorConstructor_5de330(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 0x54, vt::kWCBareVectorBase_LobbyPlayer);
}
VectorBase* __fastcall BareVectorConstructor_5df8d0(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 0x17c, vt::kWCBareVectorBase_CPrivateChat);
}
VectorBase* __fastcall BareVectorConstructor_5de570(VectorBase* self, void*, std::uint32_t capacity) {
    return ConstructBareVector(self, capacity, 0x18, vt::kWCBareVectorBase_CImageDescription);
}

using CopyConstructorFn = VectorBase*(__fastcall*)(VectorBase* self, void* edx, const VectorBase* from);

namespace {
VectorBase* CopyWordVector(VectorBase* self, const VectorBase* from, std::uint32_t vtable) {
    return CopyVector(self, from, vtable, 4, [](std::int32_t, std::uint32_t to, std::uint32_t source) {
        *reinterpret_cast<std::uint32_t*>(to) = *reinterpret_cast<const std::uint32_t*>(source);
    });
}
}  // namespace

// Purpose: Copy constructors of the lobby's vectors of ids.
VectorBase* __fastcall VectorCopy_5f3cc0(VectorBase* self, void*, const VectorBase* from) {
    return CopyWordVector(self, from, vt::kWCValSortedVector_uint);
}
VectorBase* __fastcall VectorCopy_5f3e40(VectorBase* self, void*, const VectorBase* from) {
    return CopyWordVector(self, from, vt::kUIntVector);
}

S3RE_HOOK(0x005f3cc0, VectorCopy_5f3cc0, CopyConstructorFn, Original_VectorCopy_5f3cc0, VectorCopy_5f3cc0);
S3RE_HOOK(0x005f3e40, VectorCopy_5f3e40, CopyConstructorFn, Original_VectorCopy_5f3e40, VectorCopy_5f3e40);
S3RE_HOOK(0x004143b0, VectorDestructor_4143b0, DestructorFn, Original_VectorDestructor_4143b0,
          VectorDestructor_4143b0);
S3RE_HOOK(0x00427de0, VectorDestructor_427de0, DestructorFn, Original_VectorDestructor_427de0,
          VectorDestructor_427de0);
S3RE_HOOK(0x00427e40, VectorDestructor_427e40, DestructorFn, Original_VectorDestructor_427e40,
          VectorDestructor_427e40);
S3RE_HOOK(0x00437570, VectorDestructor_437570, DestructorFn, Original_VectorDestructor_437570,
          VectorDestructor_437570);
S3RE_HOOK(0x0044caa0, VectorDestructor_44caa0, DestructorFn, Original_VectorDestructor_44caa0,
          VectorDestructor_44caa0);
S3RE_HOOK(0x0044cca0, VectorDestructor_44cca0, DestructorFn, Original_VectorDestructor_44cca0,
          VectorDestructor_44cca0);
S3RE_HOOK(0x00457a10, VectorDestructor_457a10, DestructorFn, Original_VectorDestructor_457a10,
          VectorDestructor_457a10);
S3RE_HOOK(0x00462870, VectorDestructor_462870, DestructorFn, Original_VectorDestructor_462870,
          VectorDestructor_462870);
S3RE_HOOK(0x00462b00, VectorDestructor_462b00, DestructorFn, Original_VectorDestructor_462b00,
          VectorDestructor_462b00);
S3RE_HOOK(0x004721c0, VectorDestructor_4721c0, DestructorFn, Original_VectorDestructor_4721c0,
          VectorDestructor_4721c0);
S3RE_HOOK(0x00472240, VectorDestructor_472240, DestructorFn, Original_VectorDestructor_472240,
          VectorDestructor_472240);
S3RE_HOOK(0x004722e0, VectorDestructor_4722e0, DestructorFn, Original_VectorDestructor_4722e0,
          VectorDestructor_4722e0);
S3RE_HOOK(0x00472360, VectorDestructor_472360, DestructorFn, Original_VectorDestructor_472360,
          VectorDestructor_472360);
S3RE_HOOK(0x00472440, VectorDestructor_472440, DestructorFn, Original_VectorDestructor_472440,
          VectorDestructor_472440);
S3RE_HOOK(0x004990c0, VectorDestructor_4990c0, DestructorFn, Original_VectorDestructor_4990c0,
          VectorDestructor_4990c0);
S3RE_HOOK(0x004992f0, VectorDestructor_4992f0, DestructorFn, Original_VectorDestructor_4992f0,
          VectorDestructor_4992f0);
S3RE_HOOK(0x0044e240, VectorDestructor_44e240, DestructorFn, Original_VectorDestructor_44e240,
          VectorDestructor_44e240);
S3RE_HOOK(0x00462eb0, VectorDestructor_462eb0, DestructorFn, Original_VectorDestructor_462eb0,
          VectorDestructor_462eb0);
S3RE_HOOK(0x00462f10, VectorDestructor_462f10, DestructorFn, Original_VectorDestructor_462f10,
          VectorDestructor_462f10);
S3RE_HOOK(0x00466190, VectorDestructor_466190, DestructorFn, Original_VectorDestructor_466190,
          VectorDestructor_466190);
S3RE_HOOK(0x00472660, VectorDestructor_472660, DestructorFn, Original_VectorDestructor_472660,
          VectorDestructor_472660);
S3RE_HOOK(0x00472bb0, VectorDestructor_472bb0, DestructorFn, Original_VectorDestructor_472bb0,
          VectorDestructor_472bb0);
S3RE_HOOK(0x00472c10, VectorDestructor_472c10, DestructorFn, Original_VectorDestructor_472c10,
          VectorDestructor_472c10);
S3RE_HOOK(0x00472e00, VectorDestructor_472e00, DestructorFn, Original_VectorDestructor_472e00,
          VectorDestructor_472e00);
S3RE_HOOK(0x00473000, VectorDestructor_473000, DestructorFn, Original_VectorDestructor_473000,
          VectorDestructor_473000);
S3RE_HOOK(0x00473060, VectorDestructor_473060, DestructorFn, Original_VectorDestructor_473060,
          VectorDestructor_473060);
S3RE_HOOK(0x004a1140, VectorDestructor_4a1140, DestructorFn, Original_VectorDestructor_4a1140,
          VectorDestructor_4a1140);
S3RE_HOOK(0x004141a0, VectorDestructor_4141a0, DestructorFn, Original_VectorDestructor_4141a0,
          VectorDestructor_4141a0);
S3RE_HOOK(0x004a11a0, VectorDestructor_4a11a0, DestructorFn, Original_VectorDestructor_4a11a0,
          VectorDestructor_4a11a0);
S3RE_HOOK(0x00414240, VectorDestructor_414240, DestructorFn, Original_VectorDestructor_414240,
          VectorDestructor_414240);
S3RE_HOOK(0x00437630, VectorConstructor_437630, ConstructorFn, Original_VectorConstructor_437630,
          VectorConstructor_437630);
S3RE_HOOK(0x0044ce10, VectorConstructor_44ce10, ConstructorFn, Original_VectorConstructor_44ce10,
          VectorConstructor_44ce10);
S3RE_HOOK(0x00462c70, VectorConstructor_462c70, ConstructorFn, Original_VectorConstructor_462c70,
          VectorConstructor_462c70);
S3RE_HOOK(0x00466110, VectorConstructor_466110, ConstructorFn, Original_VectorConstructor_466110,
          VectorConstructor_466110);
S3RE_HOOK(0x00472700, VectorConstructor_472700, ConstructorFn, Original_VectorConstructor_472700,
          VectorConstructor_472700);
S3RE_HOOK(0x004727a0, VectorConstructor_4727a0, ConstructorFn, Original_VectorConstructor_4727a0,
          VectorConstructor_4727a0);
S3RE_HOOK(0x004728a0, VectorConstructor_4728a0, ConstructorFn, Original_VectorConstructor_4728a0,
          VectorConstructor_4728a0);
S3RE_HOOK(0x00472920, VectorConstructor_472920, ConstructorFn, Original_VectorConstructor_472920,
          VectorConstructor_472920);
S3RE_HOOK(0x0044ce90, VectorConstructor_44ce90, ConstructorFn, Original_VectorConstructor_44ce90,
          VectorConstructor_44ce90);
S3RE_HOOK(0x00457fb0, VectorConstructor_457fb0, ConstructorFn, Original_VectorConstructor_457fb0,
          VectorConstructor_457fb0);
S3RE_HOOK(0x00457f20, VectorConstructor_457f20, ConstructorFn, Original_VectorConstructor_457f20,
          VectorConstructor_457f20);
S3RE_HOOK(0x00458500, VectorConstructor_458500, ConstructorFn, Original_VectorConstructor_458500,
          VectorConstructor_458500);
S3RE_HOOK(0x00457ea0, VectorConstructor_457ea0, ConstructorFn, Original_VectorConstructor_457ea0,
          VectorConstructor_457ea0);
S3RE_HOOK(0x00472820, VectorConstructor_472820, ConstructorFn, Original_VectorConstructor_472820,
          VectorConstructor_472820);
S3RE_HOOK(0x00462cf0, VectorConstructor_462cf0, ConstructorFn, Original_VectorConstructor_462cf0,
          VectorConstructor_462cf0);
S3RE_HOOK(0x00437c90, BareVectorConstructor_437c90, BareConstructorFn, Original_BareVectorConstructor_437c90,
          BareVectorConstructor_437c90);
S3RE_HOOK(0x00458a60, BareVectorConstructor_458a60, BareConstructorFn, Original_BareVectorConstructor_458a60,
          BareVectorConstructor_458a60);
S3RE_HOOK(0x004586f0, BareVectorConstructor_4586f0, BareConstructorFn, Original_BareVectorConstructor_4586f0,
          BareVectorConstructor_4586f0);
S3RE_HOOK(0x004377a0, BareVectorConstructor_4377a0, BareConstructorFn, Original_BareVectorConstructor_4377a0,
          BareVectorConstructor_4377a0);
S3RE_HOOK(0x00428c10, BareVectorConstructor_428c10, BareConstructorFn, Original_BareVectorConstructor_428c10,
          BareVectorConstructor_428c10);
S3RE_HOOK(0x00428e70, BareVectorConstructor_428e70, BareConstructorFn, Original_BareVectorConstructor_428e70,
          BareVectorConstructor_428e70);
S3RE_HOOK(0x00429080, BareVectorConstructor_429080, BareConstructorFn, Original_BareVectorConstructor_429080,
          BareVectorConstructor_429080);
S3RE_HOOK(0x00429410, BareVectorConstructor_429410, BareConstructorFn, Original_BareVectorConstructor_429410,
          BareVectorConstructor_429410);
S3RE_HOOK(0x004145f0, VectorDestructor_4145f0, DestructorFn, Original_VectorDestructor_4145f0,
          VectorDestructor_4145f0);
S3RE_HOOK(0x0042f8c0, VectorDestructor_42f8c0, DestructorFn, Original_VectorDestructor_42f8c0,
          VectorDestructor_42f8c0);
S3RE_HOOK(0x00499630, VectorDestructor_499630, DestructorFn, Original_VectorDestructor_499630,
          VectorDestructor_499630);
S3RE_HOOK(0x0049b7f0, VectorDestructor_49b7f0, DestructorFn, Original_VectorDestructor_49b7f0,
          VectorDestructor_49b7f0);
S3RE_HOOK(0x00541690, VectorDestructor_541690, DestructorFn, Original_VectorDestructor_541690,
          VectorDestructor_541690);
S3RE_HOOK(0x005b7c10, VectorDestructor_5b7c10, DestructorFn, Original_VectorDestructor_5b7c10,
          VectorDestructor_5b7c10);
S3RE_HOOK(0x005de270, VectorDestructor_5de270, DestructorFn, Original_VectorDestructor_5de270,
          VectorDestructor_5de270);
S3RE_HOOK(0x005de2d0, VectorDestructor_5de2d0, DestructorFn, Original_VectorDestructor_5de2d0,
          VectorDestructor_5de2d0);
S3RE_HOOK(0x005f3de0, VectorDestructor_5f3de0, DestructorFn, Original_VectorDestructor_5f3de0,
          VectorDestructor_5f3de0);
S3RE_HOOK(0x0061aab0, VectorDestructor_61aab0, DestructorFn, Original_VectorDestructor_61aab0,
          VectorDestructor_61aab0);
S3RE_HOOK(0x005bdf70, VectorDestructor_5bdf70, DestructorFn, Original_VectorDestructor_5bdf70,
          VectorDestructor_5bdf70);
S3RE_HOOK(0x004146d0, VectorDestructor_4146d0, DestructorFn, Original_VectorDestructor_4146d0,
          VectorDestructor_4146d0);
S3RE_HOOK(0x00414410, VectorConstructor_414410, ConstructorFn, Original_VectorConstructor_414410,
          VectorConstructor_414410);
S3RE_HOOK(0x00427d60, VectorConstructor_427d60, ConstructorFn, Original_VectorConstructor_427d60,
          VectorConstructor_427d60);
S3RE_HOOK(0x00468330, VectorConstructor_468330, ConstructorFn, Original_VectorConstructor_468330,
          VectorConstructor_468330);
S3RE_HOOK(0x00499580, VectorConstructor_499580, ConstructorFn, Original_VectorConstructor_499580,
          VectorConstructor_499580);
S3RE_HOOK(0x00609f50, VectorConstructor_609f50, ConstructorFn, Original_VectorConstructor_609f50,
          VectorConstructor_609f50);
S3RE_HOOK(0x00414510, VectorConstructor_414510, ConstructorFn, Original_VectorConstructor_414510,
          VectorConstructor_414510);
S3RE_HOOK(0x004993f0, VectorConstructor_4993f0, ConstructorFn, Original_VectorConstructor_4993f0,
          VectorConstructor_4993f0);
S3RE_HOOK(0x005fda00, VectorConstructor_5fda00, ConstructorFn, Original_VectorConstructor_5fda00,
          VectorConstructor_5fda00);
S3RE_HOOK(0x005dde00, VectorConstructor_5dde00, ConstructorFn, Original_VectorConstructor_5dde00,
          VectorConstructor_5dde00);
S3RE_HOOK(0x005de010, VectorConstructor_5de010, ConstructorFn, Original_VectorConstructor_5de010,
          VectorConstructor_5de010);
S3RE_HOOK(0x005de090, VectorConstructor_5de090, ConstructorFn, Original_VectorConstructor_5de090,
          VectorConstructor_5de090);
S3RE_HOOK(0x005f3f40, VectorConstructor_5f3f40, ConstructorFn, Original_VectorConstructor_5f3f40,
          VectorConstructor_5f3f40);
S3RE_HOOK(0x005de130, BareVectorConstructor_5de130, BareConstructorFn, Original_BareVectorConstructor_5de130,
          BareVectorConstructor_5de130);
S3RE_HOOK(0x004683b0, BareVectorConstructor_4683b0, BareConstructorFn, Original_BareVectorConstructor_4683b0,
          BareVectorConstructor_4683b0);
S3RE_HOOK(0x00499690, BareVectorConstructor_499690, BareConstructorFn, Original_BareVectorConstructor_499690,
          BareVectorConstructor_499690);
S3RE_HOOK(0x00473290, VectorDestructor_473290, DestructorFn, Original_VectorDestructor_473290,
          VectorDestructor_473290);
S3RE_HOOK(0x005bdbd0, VectorDestructor_5bdbd0, DestructorFn, Original_VectorDestructor_5bdbd0,
          VectorDestructor_5bdbd0);
S3RE_HOOK(0x005dde80, VectorConstructor_5dde80, ConstructorFn, Original_VectorConstructor_5dde80,
          VectorConstructor_5dde80);
S3RE_HOOK(0x00609ff0, VectorConstructor_609ff0, ConstructorFn, Original_VectorConstructor_609ff0,
          VectorConstructor_609ff0);
S3RE_HOOK(0x00609e50, VectorConstructor_609e50, ConstructorFn, Original_VectorConstructor_609e50,
          VectorConstructor_609e50);
S3RE_HOOK(0x0060a070, VectorConstructor_60a070, ConstructorFn, Original_VectorConstructor_60a070,
          VectorConstructor_60a070);
S3RE_HOOK(0x005ddf90, VectorConstructor_5ddf90, ConstructorFn, Original_VectorConstructor_5ddf90,
          VectorConstructor_5ddf90);
S3RE_HOOK(0x005ddf10, VectorConstructor_5ddf10, ConstructorFn, Original_VectorConstructor_5ddf10,
          VectorConstructor_5ddf10);
S3RE_HOOK(0x005b7ec0, VectorConstructor_5b7ec0, ConstructorFn, Original_VectorConstructor_5b7ec0,
          VectorConstructor_5b7ec0);
S3RE_HOOK(0x005b7f40, VectorConstructor_5b7f40, ConstructorFn, Original_VectorConstructor_5b7f40,
          VectorConstructor_5b7f40);
S3RE_HOOK(0x005de330, BareVectorConstructor_5de330, BareConstructorFn, Original_BareVectorConstructor_5de330,
          BareVectorConstructor_5de330);
S3RE_HOOK(0x005df8d0, BareVectorConstructor_5df8d0, BareConstructorFn, Original_BareVectorConstructor_5df8d0,
          BareVectorConstructor_5df8d0);
S3RE_HOOK(0x005de570, BareVectorConstructor_5de570, BareConstructorFn, Original_BareVectorConstructor_5de570,
          BareVectorConstructor_5de570);

// ---------------------------------------------------------------------------
// Tests: every instantiation, original against ours, on random objects.
// Constructors: capacity 0..64 (sometimes 2^30 + 0..3, so the product
// wraps); the objects' bytes (data only as null or not)
// and EAX. Destructors: random objects with a block from the game's
// operator new (or none) and filled -2..6; the objects' bytes and, without
// a block, EAX (with one it is the heap's). ~GamePlayerSetup is replaced by
// a recording stub during the destructor runs (its calls compared as
// offsets into the block).
// ---------------------------------------------------------------------------

namespace {

struct DestructorPair {
    DestructorFn original;
    DestructorFn ours;
    std::uint32_t address;
};
struct ConstructorPair {
    ConstructorFn original;
    ConstructorFn ours;
    std::uint32_t address;
};
struct BareConstructorPair {
    BareConstructorFn original;
    BareConstructorFn ours;
    std::uint32_t address;
};

// 0x473290 on the destructor test's vectors: the pair around them.
std::uint32_t __fastcall Original473290OnVector(VectorBase* vector, void*) {
    return Original_VectorDestructor_473290(reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(vector) - 4),
                                            nullptr);
}
std::uint32_t __fastcall Ours473290OnVector(VectorBase* vector, void*) {
    return VectorDestructor_473290(reinterpret_cast<VectorBase*>(reinterpret_cast<std::uintptr_t>(vector) - 4), nullptr);
}

const DestructorPair kDestructors[] = {
    {Original_VectorDestructor_4143b0, &VectorDestructor_4143b0, 0x4143b0},
    {Original_VectorDestructor_427de0, &VectorDestructor_427de0, 0x427de0},
    {Original_VectorDestructor_427e40, &VectorDestructor_427e40, 0x427e40},
    {Original_VectorDestructor_437570, &VectorDestructor_437570, 0x437570},
    {Original_VectorDestructor_44caa0, &VectorDestructor_44caa0, 0x44caa0},
    {Original_VectorDestructor_44cca0, &VectorDestructor_44cca0, 0x44cca0},
    {Original_VectorDestructor_457a10, &VectorDestructor_457a10, 0x457a10},
    {Original_VectorDestructor_462870, &VectorDestructor_462870, 0x462870},
    {Original_VectorDestructor_462b00, &VectorDestructor_462b00, 0x462b00},
    {Original_VectorDestructor_4721c0, &VectorDestructor_4721c0, 0x4721c0},
    {Original_VectorDestructor_472240, &VectorDestructor_472240, 0x472240},
    {Original_VectorDestructor_4722e0, &VectorDestructor_4722e0, 0x4722e0},
    {Original_VectorDestructor_472360, &VectorDestructor_472360, 0x472360},
    {Original_VectorDestructor_472440, &VectorDestructor_472440, 0x472440},
    {Original_VectorDestructor_4990c0, &VectorDestructor_4990c0, 0x4990c0},
    {Original_VectorDestructor_4992f0, &VectorDestructor_4992f0, 0x4992f0},
    {Original_VectorDestructor_44e240, &VectorDestructor_44e240, 0x44e240},
    {Original_VectorDestructor_462eb0, &VectorDestructor_462eb0, 0x462eb0},
    {Original_VectorDestructor_462f10, &VectorDestructor_462f10, 0x462f10},
    {Original_VectorDestructor_466190, &VectorDestructor_466190, 0x466190},
    {Original_VectorDestructor_472660, &VectorDestructor_472660, 0x472660},
    {Original_VectorDestructor_472bb0, &VectorDestructor_472bb0, 0x472bb0},
    {Original_VectorDestructor_472c10, &VectorDestructor_472c10, 0x472c10},
    {Original_VectorDestructor_472e00, &VectorDestructor_472e00, 0x472e00},
    {Original_VectorDestructor_473000, &VectorDestructor_473000, 0x473000},
    {Original_VectorDestructor_473060, &VectorDestructor_473060, 0x473060},
    {Original_VectorDestructor_4a1140, &VectorDestructor_4a1140, 0x4a1140},
    {Original_VectorDestructor_4141a0, &VectorDestructor_4141a0, 0x4141a0},
    {Original_VectorDestructor_4a11a0, &VectorDestructor_4a11a0, 0x4a11a0},
    {Original_VectorDestructor_414240, &VectorDestructor_414240, 0x414240},
    {Original_VectorDestructor_4145f0, &VectorDestructor_4145f0, 0x4145f0},
    {Original_VectorDestructor_42f8c0, &VectorDestructor_42f8c0, 0x42f8c0},
    {Original_VectorDestructor_499630, &VectorDestructor_499630, 0x499630},
    {Original_VectorDestructor_49b7f0, &VectorDestructor_49b7f0, 0x49b7f0},
    {Original_VectorDestructor_541690, &VectorDestructor_541690, 0x541690},
    {Original_VectorDestructor_5b7c10, &VectorDestructor_5b7c10, 0x5b7c10},
    {Original_VectorDestructor_5de270, &VectorDestructor_5de270, 0x5de270},
    {Original_VectorDestructor_5de2d0, &VectorDestructor_5de2d0, 0x5de2d0},
    {Original_VectorDestructor_5f3de0, &VectorDestructor_5f3de0, 0x5f3de0},
    {Original_VectorDestructor_61aab0, &VectorDestructor_61aab0, 0x61aab0},
    {Original_VectorDestructor_5bdf70, &VectorDestructor_5bdf70, 0x5bdf70},
    {Original_VectorDestructor_4146d0, &VectorDestructor_4146d0, 0x4146d0},
    {Original_VectorDestructor_5bdbd0, &VectorDestructor_5bdbd0, 0x5bdbd0},
    {&Original473290OnVector, &Ours473290OnVector, 0x473290},
};
const ConstructorPair kConstructors[] = {
    {Original_VectorConstructor_437630, &VectorConstructor_437630, 0x437630},
    {Original_VectorConstructor_44ce10, &VectorConstructor_44ce10, 0x44ce10},
    {Original_VectorConstructor_462c70, &VectorConstructor_462c70, 0x462c70},
    {Original_VectorConstructor_466110, &VectorConstructor_466110, 0x466110},
    {Original_VectorConstructor_472700, &VectorConstructor_472700, 0x472700},
    {Original_VectorConstructor_4727a0, &VectorConstructor_4727a0, 0x4727a0},
    {Original_VectorConstructor_4728a0, &VectorConstructor_4728a0, 0x4728a0},
    {Original_VectorConstructor_472920, &VectorConstructor_472920, 0x472920},
    {Original_VectorConstructor_44ce90, &VectorConstructor_44ce90, 0x44ce90},
    {Original_VectorConstructor_457fb0, &VectorConstructor_457fb0, 0x457fb0},
    {Original_VectorConstructor_457f20, &VectorConstructor_457f20, 0x457f20},
    {Original_VectorConstructor_458500, &VectorConstructor_458500, 0x458500},
    {Original_VectorConstructor_457ea0, &VectorConstructor_457ea0, 0x457ea0},
    {Original_VectorConstructor_472820, &VectorConstructor_472820, 0x472820},
    {Original_VectorConstructor_462cf0, &VectorConstructor_462cf0, 0x462cf0},
    {Original_VectorConstructor_414410, &VectorConstructor_414410, 0x414410},
    {Original_VectorConstructor_427d60, &VectorConstructor_427d60, 0x427d60},
    {Original_VectorConstructor_468330, &VectorConstructor_468330, 0x468330},
    {Original_VectorConstructor_499580, &VectorConstructor_499580, 0x499580},
    {Original_VectorConstructor_609f50, &VectorConstructor_609f50, 0x609f50},
    {Original_VectorConstructor_414510, &VectorConstructor_414510, 0x414510},
    {Original_VectorConstructor_4993f0, &VectorConstructor_4993f0, 0x4993f0},
    {Original_VectorConstructor_5fda00, &VectorConstructor_5fda00, 0x5fda00},
    {Original_VectorConstructor_5dde00, &VectorConstructor_5dde00, 0x5dde00},
    {Original_VectorConstructor_5de010, &VectorConstructor_5de010, 0x5de010},
    {Original_VectorConstructor_5de090, &VectorConstructor_5de090, 0x5de090},
    {Original_VectorConstructor_5f3f40, &VectorConstructor_5f3f40, 0x5f3f40},
    {Original_VectorConstructor_5dde80, &VectorConstructor_5dde80, 0x5dde80},
    {Original_VectorConstructor_609ff0, &VectorConstructor_609ff0, 0x609ff0},
    {Original_VectorConstructor_609e50, &VectorConstructor_609e50, 0x609e50},
    {Original_VectorConstructor_60a070, &VectorConstructor_60a070, 0x60a070},
    {Original_VectorConstructor_5ddf90, &VectorConstructor_5ddf90, 0x5ddf90},
    {Original_VectorConstructor_5ddf10, &VectorConstructor_5ddf10, 0x5ddf10},
    {Original_VectorConstructor_5b7ec0, &VectorConstructor_5b7ec0, 0x5b7ec0},
    {Original_VectorConstructor_5b7f40, &VectorConstructor_5b7f40, 0x5b7f40},
};
const BareConstructorPair kBareConstructors[] = {
    {Original_BareVectorConstructor_437c90, &BareVectorConstructor_437c90, 0x437c90},
    {Original_BareVectorConstructor_458a60, &BareVectorConstructor_458a60, 0x458a60},
    {Original_BareVectorConstructor_4586f0, &BareVectorConstructor_4586f0, 0x4586f0},
    {Original_BareVectorConstructor_4377a0, &BareVectorConstructor_4377a0, 0x4377a0},
    {Original_BareVectorConstructor_428c10, &BareVectorConstructor_428c10, 0x428c10},
    {Original_BareVectorConstructor_428e70, &BareVectorConstructor_428e70, 0x428e70},
    {Original_BareVectorConstructor_429080, &BareVectorConstructor_429080, 0x429080},
    {Original_BareVectorConstructor_429410, &BareVectorConstructor_429410, 0x429410},
    {Original_BareVectorConstructor_5de130, &BareVectorConstructor_5de130, 0x5de130},
    {Original_BareVectorConstructor_4683b0, &BareVectorConstructor_4683b0, 0x4683b0},
    {Original_BareVectorConstructor_499690, &BareVectorConstructor_499690, 0x499690},
    {Original_BareVectorConstructor_5de330, &BareVectorConstructor_5de330, 0x5de330},
    {Original_BareVectorConstructor_5df8d0, &BareVectorConstructor_5df8d0, 0x5df8d0},
    {Original_BareVectorConstructor_5de570, &BareVectorConstructor_5de570, 0x5de570},
};

std::uint32_t RandomCapacity(test::Rng& rng) {
    if (rng.Chance(1, 4)) {
        return 0;
    }
    if (rng.Chance(1, 10)) {
        // The product wraps to a small block for every element size here
        // (the emulator spec tries any value; a huge block may succeed for
        // one side and fail for the other in the real heap).
        return 0x40000000u + rng.Below(4);
    }
    return rng.Below(65);
}

// Compares two constructed objects (the data pointers only as null or
// not) and frees their blocks.
bool SameConstructed(test::Context& ctx, VectorBase& a, VectorBase& b) {
    const bool nullA = a.data == 0, nullB = b.data == 0;
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(a.data));
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(b.data));
    a.data = b.data = 0;
    return S3RE_EXPECT_EQ(nullA, nullB) && S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
}

bool CompareConstructors(test::Context& ctx, const ConstructorPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        const std::uint32_t capacity = RandomCapacity(ctx.rng());
        const std::uint32_t grow = ctx.rng().Next32();
        VectorBase* ra = pair.original(&a, nullptr, capacity, grow);
        VectorBase* rb = pair.ours(&b, nullptr, capacity, grow);
        if (!S3RE_EXPECT(ra == &a && rb == &b) || !SameConstructed(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: capacity %u", pair.address, i, capacity);
            return false;
        }
    }
    return true;
}

bool CompareBareConstructors(test::Context& ctx, const BareConstructorPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        const std::uint32_t capacity = RandomCapacity(ctx.rng());
        VectorBase* ra = pair.original(&a, nullptr, capacity);
        VectorBase* rb = pair.ours(&b, nullptr, capacity);
        if (!S3RE_EXPECT(ra == &a && rb == &b) || !SameConstructed(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: capacity %u", pair.address, i, capacity);
            return false;
        }
    }
    return true;
}

// Runs a destructor with ~GamePlayerSetup and ~CTeam stubbed; its calls are recorded
// as offsets into the vector's block.
std::vector<test::StubCall> RunDestructor(DestructorFn fn, VectorBase& vector, std::uint32_t& eax) {
    test::CalleeStubs stubs;
    stubs.Install<0, kDestroyPlayer>();
    stubs.Install<0, kDestroyTeam>();
    stubs.SetWorld(reinterpret_cast<game::CGameData*>(vector.data));
    eax = fn(&vector, nullptr);
    return stubs.Take();
}

bool CompareDestructors(test::Context& ctx, const DestructorPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        a.filled = ctx.rng().Range(-2, 6);
        std::memcpy(&b, &a, sizeof(a));
        const bool block = ctx.rng().Chance(3, 4);
        a.data = block ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(6 * kPlayerSize)) : 0;
        b.data = block ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(6 * kPlayerSize)) : 0;
        std::uint32_t ra = 0, rb = 0;
        const auto callsA = RunDestructor(pair.original, a, ra);
        const auto callsB = RunDestructor(pair.ours, b, rb);
        a.data = b.data = 0;  // freed (or none)
        bool same = S3RE_EXPECT(callsA == callsB) && S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!block) {
            same = S3RE_EXPECT_EQ(ra, rb) && same;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: block %d, filled %d", pair.address, i, block, a.filled);
            return false;
        }
    }
    return true;
}

// Wrong implementations, one per kind.
// (The first vtable store is overwritten by the last one, so it is not
// observable; a wrong EAX is.)
std::uint32_t __fastcall WrongDestructor(VectorBase* self, void*) {
    return DestroyTrivialVector(self, vt::kWCBareVectorBase_int) | 1;
}
std::uint32_t __fastcall WrongPlayerDestructor(VectorBase* self, void*) {
    --self->filled;  // one element fewer
    const std::uint32_t result = DestroyPlayerVector(self);
    ++self->filled;
    return result;
}
VectorBase* __fastcall WrongConstructor(VectorBase* self, void*, std::uint32_t capacity, std::uint32_t grow) {
    return ConstructVector(self, capacity, grow, 32, vt::kWCBareVectorBase_CChatMessage,
                           vt::kWCOrderedVectorBase_CChatMessage);  // 0x457fb0's class, not 0x44ce90's
}
VectorBase* __fastcall WrongBareConstructor(VectorBase* self, void* edx, std::uint32_t capacity) {
    BareVectorConstructor_437c90(self, edx, capacity);
    self->unk04 = 1;
    return self;
}

// The copy constructors: a source with 0..6 random words in a block (or
// a capacity <= 0); compared: EAX, the copy's fields (data as null or not)
// and its words.
bool CompareCopies(test::Context& ctx, CopyConstructorFn original, CopyConstructorFn ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase from;
        ctx.rng().Fill(&from, sizeof(from));
        std::uint32_t words[6];
        ctx.rng().Fill(words, sizeof(words));
        from.capacity = ctx.rng().Chance(1, 8) ? 0x80000000u + ctx.rng().Below(3) : ctx.rng().Below(7);
        from.filled = static_cast<std::int32_t>(from.capacity <= 6 ? ctx.rng().Below(from.capacity + 1) : 0);
        from.data = reinterpret_cast<std::uintptr_t>(words);
        VectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        VectorBase* ra = original(&a, nullptr, &from);
        VectorBase* rb = ours(&b, nullptr, &from);
        bool same = S3RE_EXPECT(ra == &a && rb == &b) && S3RE_EXPECT_EQ(a.data == 0, b.data == 0);
        if (same && a.data != 0) {
            same = S3RE_EXPECT_MEM_EQ(reinterpret_cast<void*>(a.data), reinterpret_cast<void*>(b.data),
                                      4 * static_cast<std::uint32_t>(a.filled));
        }
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(a.data));
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(b.data));
        a.data = b.data = 0;
        same = same && S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: capacity 0x%x, filled %d", i, from.capacity, from.filled);
            return false;
        }
    }
    return true;
}

// Wrong: the length copied even without a block.
VectorBase* __fastcall WrongCopy(VectorBase* self, void* edx, const VectorBase* from) {
    VectorCopy_5f3cc0(self, edx, from);
    self->length = from->length;
    return self;
}

}  // namespace

S3RE_TEST(WatcomVectorCopies_MatchOriginal) {
    CompareCopies(ctx, Original_VectorCopy_5f3cc0, &VectorCopy_5f3cc0, 300) &&
        CompareCopies(ctx, Original_VectorCopy_5f3e40, &VectorCopy_5f3e40, 300);
}

S3RE_TEST(WatcomVectorCopies_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCopies(probe, Original_VectorCopy_5f3cc0, &WrongCopy, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(WatcomVectorDestructors_MatchOriginal) {
    for (const DestructorPair& pair : kDestructors) {
        if (!CompareDestructors(ctx, pair, 300)) {
            return;
        }
    }
}
S3RE_TEST(WatcomVectorConstructors_MatchOriginal) {
    for (const ConstructorPair& pair : kConstructors) {
        if (!CompareConstructors(ctx, pair, 300)) {
            return;
        }
    }
    for (const BareConstructorPair& pair : kBareConstructors) {
        if (!CompareBareConstructors(ctx, pair, 300)) {
            return;
        }
    }
}
S3RE_TEST(WatcomVectors_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDestructors(probe, {Original_VectorDestructor_4143b0, &WrongDestructor, 0x4143b0}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDestructors(probe, {Original_VectorDestructor_4141a0, &WrongPlayerDestructor, 0x4141a0}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareConstructors(probe, {Original_VectorConstructor_44ce90, &WrongConstructor, 0x44ce90}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareBareConstructors(probe, {Original_BareVectorConstructor_437c90, &WrongBareConstructor, 0x437c90},
                                50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
