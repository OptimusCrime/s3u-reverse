// Compiler-generated deleting destructors (MSVC 6.0) of several classes:
// the "scalar deleting destructor" that a vtable's slot 0 (or a `delete p`)
// calls with flags, and the complete-object destructors of classes with a
// virtual base. Each is a few instructions around the class's own
// destructor; ours call those through their S3.EXE addresses.
//
// Scalar deleting destructors (flags) ret 4, EAX = the object:
//   destructor(this); if (flags & 1) operator delete(this).
//     0x0044e300 String (called directly by menu constructors)  ~string 0x401b60
//     0x0044e320 a lobby class with a string at +8             0x441740
//     0x004639c0 a menu class with a string at +0x50           0x45f060
//     0x0046db20 a network class                               Net::DeletePointee 0x49d980
//     0x0041d990 CStaticControl                                0x41d9b0 (jmp ~CScreenElement)
//     0x004993d0 Watcom iterators (copy of 0x4281d0's slot)    ~WCIterExcept 0x499620
//     0x0049cb50 Lobby::CLM* messages (shared by 24 vtables)   0x434680 (jmp ~CLMGeneric)
//     0x004a5b20 Dirk::CMapFile                                ~CMapFile 0x4a5b50
//   and of the Watcom vectors (their destructors: watcom/bare_vectors.cpp,
//   string_vector.cpp) and of Dirk::CTeam:
//     0x0040a1b0 WCValOrderedVector<String>                       0x409d50
//     0x00414390 WCValSortedVector<int>                           0x4143b0
//     0x004281f0 WCValOrderedVector<HFONT>                        0x427de0
//     0x00428210 WCValOrderedVector<CScreenElement*>              0x427e40
//     0x004375d0 WCValOrderedVector<Dirk::CPlayer>                0x4141a0
//     0x00437610 WCValOrderedVector<int>                          0x437570
//     0x0044cd70 WCValSortedVector<String>                        0x44c980
//     0x0044cd90 WCValOrderedVector<unsigned int>                 0x44caa0
//     0x0044cdf0 WCValOrderedVector<LobbyClient::CStatusMessage>  0x44cca0
//     0x00457d10 WCValSortedVector<unsigned int>                  0x457a10
//     0x00462b60 WCValOrderedVector<unsigned short>               0x462870
//     0x00462c50 WCPtrSortedVector<DirectX::CPlayer>              0x462b00
//     0x004724e0 WCPtrSortedVector<CExecuteType>                  0x4721c0
//     0x00472500 WCPtrOrderedVector<CRequestType>                 0x472240
//     0x00472520 WCPtrSortedVector<CAnnounceType>                 0x4722e0
//     0x00472540 WCPtrOrderedVector<CAcknowledgeType>             0x472360
//     0x004726c0 WCValSortedVector<unsigned long>                 0x472440
//     0x00499350 WCValOrderedVector<GameChunkDescription>         0x4990c0
//     0x004993b0 WCValOrderedVector<long>                         0x4992f0
//     0x004375f0 WCValSortedVector<Dirk::CTeam>                   0x414240
//     0x00414b90 Dirk::CTeam                                      0x4a04d0
//   and of the sound classes and their dictionaries:
//     0x005c23b0 DirectX::CSoundFile                            0x5c23d0
//     0x005c2780 DirectX::CMemorySoundFile (thunk to ~CSoundFile) 0x5c27a0
//     0x005c1810 DirectX::CDirectSoundException                 0x5c1830
//     0x005b88c0 DirectX::CAppNotActiveException                0x5b88e0
//     0x005c34b0 WCValSkipListDict<int, CSoundEffectBuffer>     0x5c33e0
//     0x005c34d0 WCSkipListBase<..int, CSoundEffectBuffer..>    0x5c3450
//     0x005ffb20 WCValSkipListDict<uint, CSoundEffect*>         0x5ffa60
//     0x005ffb40 WCSkipListBase<..uint, CSoundEffect*..>        0x5ffb60
//     0x005ffbc0 WCValSkipListDict<uint, uint>                  0x5ffac0
//     0x00614130 Win32::CMCIException                          0x614150
//     0x005ffbe0 WCSkipListBase<..uint, uint..>                 0x5ffc00
//   and (session 58, wip/framework-errors) of the other exception classes:
//     0x005b93a0 DirectX::CDirectInputException                0x5b94c0
//     0x005be3c0 DirectX::CDirectDrawException                 0x5be3e0
//     0x005bfff0 DirectX::CDirectPlayException                 0x5c0010
//     0x0060e590 Win32::CStructuredException                   0x60e490
//     0x0060e5b0 Win32::CRunTimeError                          0x5b93c0
//     0x00610810 Win32::CException                             0x610830
//     0x00612770 Win32::COutOfMemory                           0x612790
//     0x00612c60 Win32::CAssertmentFailed                      0x612d80
//     0x00613320 Win32::CHResult                               0x613340
//     0x00613bb0 Win32::CMMException                           0x613bd0
//     0x00614ee0 Win32::CProgrammingError                      0x612c80
//     0x00615150 Win32::CTimeout                               0x615170
//     0x006003e0 Lobby::priv::CDataError                       0x600400
//   and (wip/app-r4) of the DirectX / Win32 framework classes and their
//   containers in 0x5b5d10..0x61ab10 (27 plain ones; the "sub" ones below
//   are a second base at +4: CGameThread 0x5b6070, CGameDrawThread 0x5b6570,
//   CWin32Waitable 0x60bf90, CEvent 0x60ca00, CTimer 0x60cf70, CThread
//   0x60dfc0); the list with their destructors is the hook list below.
// Scalar deleting destructors of classes with the virtual base
// CScreenElement (reached through a thunk with `this` at the virtual base,
// at +offset of the object): destructor(this), ~CScreenElement 0x4156b0
// (this), then operator delete(this - offset) if (flags & 1); EAX = this -
// offset (the object).
//     0x0042a710 CScreen                       +0x98   ~CScreen 0x42a750
//     0x0042af60 CListBox                      +0x444  ~CListBox 0x421fc0
//     0x0042b340 CDropdown                     +0x66c  ~CDropdown 0x42b380
//     0x0049b610 Menus::priv::CLoadGameDialog  +0x914  0x49b650
// Scalar deleting destructors with the destructor inlined (it only stores
// the vtable): vtable = V; if (flags & 1) operator delete(this); EAX = this.
//     0x00405360 WCExcept      0x655438
//     0x004281d0 WCIterExcept  0x656a38
// Complete-object destructors of a member with a virtual base (no flags):
// p = this + offset; destructor(p); ~CScreenElement(p); EAX = what
// ~CScreenElement leaves in it (callers don't use it).
//     0x00421fa0 CListBox   +0x444  ~CListBox 0x421fc0
//     0x004816d0 CDropdown  +0x66c  ~CDropdown 0x42b380
//   and (session 57) of the other controls:
//     0x0041e270 CImageButton     +0x60  0x41b630
//     0x0041eff0 CVScrollBar      +0x2f4  0x41f010
//     0x00421f30 CEditControl     +0x70  0x421f50
//     0x00423850 CScreenGroup     +0x80  0x416da0
//     0x00439520 CTextButton      +0x74  0x42ab70
//     0x004416e0 CTextView        +0x434  0x42a380
//     0x00441700 CListView        +0x59c  0x423870
//     0x00441720 CAnimatedButton  +0x68  0x42a280
//     0x00476ef0 CButton          +0x50  0x41a900
//     0x00486930 CUpDown          +0x224  0x42b100
// Session 57 (wip/ui-controls), found by their bytes in 0x400000..0x4c0000
// (network and lobby classes left out): the three shapes above, "plain"
// (destructor(this)), "vbase" (+offset of the CScreenElement virtual base,
// as above) and "sub" (a second base at +4: destructor(this), operator
// delete(this - 4) if (flags & 1), EAX = this - 4 either way):
//     address    kind  class                                                        destructor
//     0x0041c560 plain CStaticText                                                  0x41c580
//     0x0041de80 plain CStaticSurface                                               0x41dea0
//     0x00428230 plain WCStack_CScreenElement_WCValSList_CScreenElement             0x427ea0
//     0x00428250 plain WCStack_bool_WCValSList_bool                                 0x427f10
//     0x00428270 plain WCQueue_MsgConsole_WCValSList_MsgConsole                     0x428290
//     0x00428320 plain WCValOrderedVector_CListView_CColumn                         0x427fe0
//     0x00428340 plain WCValOrderedVector_CListView_CLine                           0x428070
//     0x00428360 plain WCValSkipListDict_int_CScreenElement                         0x428120
//     0x00428380 plain WCSkipListBase_WCSkipListDictKeyVal_int_CScreenElement       0x4283a0
//     0x00429730 plain CListView::CColumn                                           0x4275e0
//     0x00429c30 vbase CProgressBar                                                 0x429c70 +0x10c
//     0x0042a060 vbase CDialog                                                      0x42a0a0 +0xc4
//     0x0042a250 vbase CAnimatedButton                                              0x42a280 +0x68
//     0x0042a340 vbase CTextView                                                    0x42a380 +0x434
//     0x0042a630 vbase CScreenGroup                                                 0x416da0 +0x80
//     0x0042a8c0 vbase CControl                                                     0x4180a0 +0x3c
//     0x0042a980 vbase CButton                                                      0x41a900 +0x50
//     0x0042aa60 vbase CImageButton                                                 0x41b630 +0x60
//     0x0042ab40 vbase CTextButton                                                  0x42ab70 +0x74
//     0x0042ad20 vbase CEditControl                                                 0x421f50 +0x70
//     0x0042ae10 vbase CGroupControl                                                0x41e210 +0xb8
//     0x0042b0c0 vbase CUpDown                                                      0x42b100 +0x224
//     0x0042b600 vbase CListView                                                    0x423870 +0x59c
//     0x0042b770 vbase CVScrollBar                                                  0x41f010 +0x2f4
//     0x0042c310 sub   CDrawThread                                                  0x42c340 +0x4
//     0x0042c9b0 plain CDrawer                                                      0x42ca00
//     0x00430440 plain CEventQueueOverflow                                          0x430460
//     0x004308b0 sub   CEventQueue                                                  0x4308e0 +0x4
//     0x00457d30 plain WCValSkipListDict_unsigned_int_String                        0x457a90
//     0x00457d50 plain WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_String      0x457bb0
//     0x00457d70 plain WCValSkipListDict_unsigned_int_WCValOrderedVector_String     0x457d90
//     0x00457e80 plain WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_WCValOrderedVector_String 0x457cb0
//     0x0045ad40 plain CSiedlerMainWindow                                           0x45ad60
//     0x0045d4e0 plain WCValSkipListDict_int_HICON                                  0x45d500
//     0x0045d560 plain WCSkipListBase_WCSkipListDictKeyVal_int_HICON                0x45d580
//     0x00462b80 plain WCPtrSList_Win32_CWin32Waitable                              0x4628f0
//     0x00462ba0 plain WCValSList_Win32_CWin32Waitable                              0x462bc0
//     0x004639a0 plain Win32::CWin32WaitableBag_Win32_CWin32Waitable                0x463020
//     0x00469050 plain Dirk::CMouseSurface                                          0x469070
//     0x00497e20 plain Menus::priv::CDrawer                                         0x497e40
//     0x00499370 plain WCValSkipListDict_int_String                                 0x499150
//     0x00499390 plain WCSkipListBase_WCSkipListDictKeyVal_int_String               0x499290
//     0x0049a190 vbase Menus::priv::CCampaignEndDialog                              0x49a1d0 +0x158
//     0x0049a4b0 vbase Menus::priv::CCreditsDialog                                  0x492620 +0x1a8
//     0x0049a630 vbase Menus::priv::CMessageBox                                     0x492c50 +0xf4
//     0x0049a7a0 vbase Menus::priv::CMainMenu                                       0x49a7e0 +0x96c
//     0x0049ab60 vbase Menus::priv::CGameSetupDialog                                0x486950 +0x32e4
//     0x0049acd0 vbase Menus::priv::CNationSelectDialog                             0x49ad10 +0xbe0
//     0x0049b180 vbase Menus::priv::CBriefingDialog                                 0x49b1c0 +0xaa4
//     0x0049b4a0 vbase Menus::priv::CCampaignDialog                                 0x4797e0 +0xaec
//     0x0049b9d0 vbase Menus::priv::CMapSelectDialog                                0x4816f0 +0x269c
//     0x0049bb40 vbase Menus::priv::CGameStatsDialog                                0x492270 +0x73c
//     0x0049bcb0 vbase Menus::priv::CGameLaunchDialog                               0x49bcf0 +0xcfc
//     0x0049da90 plain CEventFile                                                   0x49dab0
//     0x004a6c00 plain WCValSkipListDict_unsigned_int_int                           0x4a6a40
//     0x004a6c20 plain WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_int         0x4a6c40
//     0x004a6ca0 plain WCValSkipListDict_unsigned_int_void                          0x4a6b40
//     0x004a6cc0 plain WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_void        0x4a6ce0
//     0x004a7840 plain Dirk::CTextSurface                                           0x4a7860
//     0x004a8e70 plain WCValSkipListDict_int_HFONT                                  0x4a8e10
//     0x004a8e90 plain WCSkipListBase_WCSkipListDictKeyVal_int_HFONT                0x4a8eb0

#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

using ThiscallFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
constexpr std::uintptr_t kScreenElementDestructor = 0x004156b0;  // CScreenElement::~CScreenElement (ours)

std::uint32_t CallDestructor(std::uintptr_t destructor, std::uintptr_t self) {
    return reinterpret_cast<ThiscallFn>(destructor)(self, nullptr);
}

std::uintptr_t DeletingDestructor(std::uintptr_t destructor, std::uintptr_t self, std::uint32_t flags) {
    CallDestructor(destructor, self);
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(self));
    }
    return self;
}

std::uintptr_t VirtualBaseDeletingDestructor(std::uintptr_t destructor, std::uint32_t offset, std::uintptr_t self,
                                             std::uint32_t flags) {
    const std::uintptr_t object = self - offset;
    CallDestructor(destructor, self);
    CallDestructor(kScreenElementDestructor, self);
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(object));
    }
    return object;
}

std::uintptr_t InlineDeletingDestructor(std::uint32_t vtable, std::uintptr_t self, std::uint32_t flags) {
    *reinterpret_cast<std::uint32_t*>(self) = vtable;
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(self));
    }
    return self;
}

std::uintptr_t SubobjectDeletingDestructor(std::uintptr_t destructor, std::uint32_t offset, std::uintptr_t self,
                                          std::uint32_t flags) {
    CallDestructor(destructor, self);
    const std::uintptr_t object = self - offset;
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(object));
    }
    return object;
}

std::uint32_t VirtualBaseDestructor(std::uintptr_t destructor, std::uint32_t offset, std::uintptr_t self) {
    const std::uintptr_t member = self + offset;
    CallDestructor(destructor, member);
    return CallDestructor(kScreenElementDestructor, member);
}

}  // namespace

using DeletingDestructorFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t flags);
using CompleteDestructorFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

// Purpose: Destroys an object and frees it when asked (`delete p`, and
// slot 0 of the class's vtable); one per class.
std::uintptr_t __fastcall ScalarDeletingDestructor_44e300(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00401b60, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_44e320(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00441740, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4639c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0045f060, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_46db20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0049d980, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_41d990(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0041d9b0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4993d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00499620, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49cb50(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00434680, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a5b20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a5b50, self, flags);
}

// Purpose: The same for the Watcom vectors (the menus', the network's, the
// game settings') and the game settings' teams.
std::uintptr_t __fastcall ScalarDeletingDestructor_40a1b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00409d50, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_414390(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004143b0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4281f0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00427de0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428210(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00427e40, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4375d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004141a0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_437610(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00437570, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_44cd70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0044c980, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_44cd90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0044caa0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_44cdf0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0044cca0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_457d10(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00457a10, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_462b60(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00462870, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_462c50(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00462b00, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4724e0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004721c0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_472500(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00472240, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_472520(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004722e0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_472540(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00472360, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4726c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00472440, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_499350(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004990c0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4993b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004992f0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4375f0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00414240, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_414b90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a04d0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5c23b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c23d0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5c2780(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c27a0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5c1810(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c1830, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b88c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b88e0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5c34b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c33e0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5c34d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c3450, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5ffb20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005ffa60, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5ffb40(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005ffb60, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5ffbc0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005ffac0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5ffbe0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005ffc00, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_614130(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00614150, self, flags);
}
// session 58 (wip/framework-errors): the other exception classes
std::uintptr_t __fastcall ScalarDeletingDestructor_5b93a0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b94c0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5be3c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005be3e0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5bfff0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005c0010, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60e590(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0060e490, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60e5b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b93c0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_610810(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00610830, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_612770(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00612790, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_612c60(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00612d80, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_613320(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00613340, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_613bb0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00613bd0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_614ee0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00612c80, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_615150(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00615170, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_6003e0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00600400, self, flags);
}

// Purpose: The same for the screen classes with the virtual base
// CScreenElement (called with `this` at the virtual base).
std::uintptr_t __fastcall ScalarDeletingDestructor_42a710(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042a750, 0x98, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42af60(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00421fc0, 0x444, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42b340(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042b380, 0x66c, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49b610(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049b650, 0x914, self, flags);
}

// Purpose: The same for the Watcom container library's exception base
// classes, whose destructors only store the vtable.
std::uintptr_t __fastcall ScalarDeletingDestructor_405360(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCExcept, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4281d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCIterExcept, self, flags);
}

// Purpose: The same for the UI controls and menus, the Watcom lists, skip
// lists and stacks, and the draw and event-queue classes (session 57).
std::uintptr_t __fastcall ScalarDeletingDestructor_41c560(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0041c580, self, flags);  // CStaticText
}
std::uintptr_t __fastcall ScalarDeletingDestructor_41de80(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0041dea0, self, flags);  // CStaticSurface
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428230(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00427ea0, self, flags);  // WCStack_CScreenElement_WCValSList_CScreenElement
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428250(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00427f10, self, flags);  // WCStack_bool_WCValSList_bool
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428270(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00428290, self, flags);  // WCQueue_MsgConsole_WCValSList_MsgConsole
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428320(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00427fe0, self, flags);  // WCValOrderedVector_CListView_CColumn
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428340(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00428070, self, flags);  // WCValOrderedVector_CListView_CLine
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428360(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00428120, self, flags);  // WCValSkipListDict_int_CScreenElement
}
std::uintptr_t __fastcall ScalarDeletingDestructor_428380(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004283a0, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_int_CScreenElement
}
std::uintptr_t __fastcall ScalarDeletingDestructor_429730(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004275e0, self, flags);  // CListView::CColumn
}
std::uintptr_t __fastcall ScalarDeletingDestructor_429c30(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00429c70, 0x10c, self, flags);  // CProgressBar
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a060(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042a0a0, 0xc4, self, flags);  // CDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a250(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042a280, 0x68, self, flags);  // CAnimatedButton
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a340(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042a380, 0x434, self, flags);  // CTextView
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a630(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00416da0, 0x80, self, flags);  // CScreenGroup
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a8c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x004180a0, 0x3c, self, flags);  // CControl
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42a980(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0041a900, 0x50, self, flags);  // CButton
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42aa60(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0041b630, 0x60, self, flags);  // CImageButton
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42ab40(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042ab70, 0x74, self, flags);  // CTextButton
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42ad20(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00421f50, 0x70, self, flags);  // CEditControl
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42ae10(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0041e210, 0xb8, self, flags);  // CGroupControl
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42b0c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042b100, 0x224, self, flags);  // CUpDown
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42b600(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00423870, 0x59c, self, flags);  // CListView
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42b770(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0041f010, 0x2f4, self, flags);  // CVScrollBar
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42c310(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0042c340, 0x4, self, flags);  // CDrawThread
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42c9b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0042ca00, self, flags);  // CDrawer
}
std::uintptr_t __fastcall ScalarDeletingDestructor_430440(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00430460, self, flags);  // CEventQueueOverflow
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4308b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x004308e0, 0x4, self, flags);  // CEventQueue
}
std::uintptr_t __fastcall ScalarDeletingDestructor_457d30(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00457a90, self, flags);  // WCValSkipListDict_unsigned_int_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_457d50(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00457bb0, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_457d70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00457d90, self, flags);  // WCValSkipListDict_unsigned_int_WCValOrderedVector_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_457e80(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00457cb0, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_WCValOrderedVector_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_45ad40(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0045ad60, self, flags);  // CSiedlerMainWindow
}
std::uintptr_t __fastcall ScalarDeletingDestructor_45d4e0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0045d500, self, flags);  // WCValSkipListDict_int_HICON
}
std::uintptr_t __fastcall ScalarDeletingDestructor_45d560(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0045d580, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_int_HICON
}
std::uintptr_t __fastcall ScalarDeletingDestructor_462b80(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004628f0, self, flags);  // WCPtrSList_Win32_CWin32Waitable
}
std::uintptr_t __fastcall ScalarDeletingDestructor_462ba0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00462bc0, self, flags);  // WCValSList_Win32_CWin32Waitable
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4639a0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00463020, self, flags);  // Win32::CWin32WaitableBag_Win32_CWin32Waitable
}
std::uintptr_t __fastcall ScalarDeletingDestructor_469050(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00469070, self, flags);  // Dirk::CMouseSurface
}
std::uintptr_t __fastcall ScalarDeletingDestructor_497e20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00497e40, self, flags);  // Menus::priv::CDrawer
}
std::uintptr_t __fastcall ScalarDeletingDestructor_499370(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00499150, self, flags);  // WCValSkipListDict_int_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_499390(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00499290, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_int_String
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49a190(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049a1d0, 0x158, self, flags);  // Menus::priv::CCampaignEndDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49a4b0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00492620, 0x1a8, self, flags);  // Menus::priv::CCreditsDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49a630(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00492c50, 0xf4, self, flags);  // Menus::priv::CMessageBox
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49a7a0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049a7e0, 0x96c, self, flags);  // Menus::priv::CMainMenu
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49ab60(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00486950, 0x32e4, self, flags);  // Menus::priv::CGameSetupDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49acd0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049ad10, 0xbe0, self, flags);  // Menus::priv::CNationSelectDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49b180(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049b1c0, 0xaa4, self, flags);  // Menus::priv::CBriefingDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49b4a0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x004797e0, 0xaec, self, flags);  // Menus::priv::CCampaignDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49b9d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x004816f0, 0x269c, self, flags);  // Menus::priv::CMapSelectDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49bb40(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00492270, 0x73c, self, flags);  // Menus::priv::CGameStatsDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49bcb0(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049bcf0, 0xcfc, self, flags);  // Menus::priv::CGameLaunchDialog
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49da90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0049dab0, self, flags);  // CEventFile
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a6c00(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a6a40, self, flags);  // WCValSkipListDict_unsigned_int_int
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a6c20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a6c40, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_int
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a6ca0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a6b40, self, flags);  // WCValSkipListDict_unsigned_int_void
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a6cc0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a6ce0, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_unsigned_int_void
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a7840(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a7860, self, flags);  // Dirk::CTextSurface
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a8e70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a8e10, self, flags);  // WCValSkipListDict_int_HFONT
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a8e90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a8eb0, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_int_HFONT
}

// Purpose: The same for the DirectX and Win32 framework classes and their
// containers (wip/app-r4); the "sub" ones are a second base at +4.
std::uintptr_t __fastcall ScalarDeletingDestructor_5b5d10(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b5d30, self, flags);  // DirectX::CGameMainWindow
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b6d50(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b6d70, self, flags);  // DirectX::CGameDrawer
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7c70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7950, self, flags);  // WCPtrSList_DirectX_CGameThread
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7c90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b79c0, self, flags);  // WCPtrSList_DirectX_CSurface
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7cb0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7a30, self, flags);  // WCPtrSList_DirectX_CPalette
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7cd0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7ac0, self, flags);  // WCValOrderedVector_DirectX_CDirectDrawDevice
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7cf0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7c10, self, flags);  // WCValOrderedVector_DirectX_CDisplayMode
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7d70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7d90, self, flags);  // WCValSList_DirectX_CSurface
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b7e30(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7e50, self, flags);  // WCValSList_DirectX_CPalette
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b8440(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b8460, self, flags);  // Win32::CWin32WaitableBag_DirectX_CGameThread
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b8540(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b7670, self, flags);  // DirectX::CDirectDrawDevice
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b8fe0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x006100c0, self, flags);  // Win32::CMainWindow
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b9e00(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005b9e40, self, flags);  // DirectX::CDirectInputDevice
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b9e20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005befd0, self, flags);  // DirectX::CDirectInputErrorHandler
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5ba1a0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x005ba1c0, self, flags);  // DirectX::CDIMouse
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60bc80(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00423a10, self, flags);  // WCSkipNonTempBase
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60be90(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0060beb0, self, flags);  // Win32::CWin32Handle
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60f980(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0060f9a0, self, flags);  // Win32::CWindowBase
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60fe70(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0060fe90, self, flags);  // Win32::CWindow
}
std::uintptr_t __fastcall ScalarDeletingDestructor_612040(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00612060, self, flags);  // Win32::CFileNotFound
}
std::uintptr_t __fastcall ScalarDeletingDestructor_6154e0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00615500, self, flags);  // Win32::CCDAudio
}
std::uintptr_t __fastcall ScalarDeletingDestructor_616110(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00616050, self, flags);  // WCValSkipListDict_HWND___Win32_CCDAudio
}
std::uintptr_t __fastcall ScalarDeletingDestructor_616130(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00616150, self, flags);  // WCSkipListBase_WCSkipListDictKeyVal_HWND___Win32_CCDAudio
}
std::uintptr_t __fastcall ScalarDeletingDestructor_616300(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00616320, self, flags);  // Win32::CRegistryValueInfo
}
std::uintptr_t __fastcall ScalarDeletingDestructor_616440(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00616490, self, flags);  // Win32::CRegistryKey
}
std::uintptr_t __fastcall ScalarDeletingDestructor_6195e0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00619600, self, flags);  // Win32::CFile
}
std::uintptr_t __fastcall ScalarDeletingDestructor_61ab10(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0061aab0, self, flags);  // WCValOrderedVector_void
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b6070(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x005b60a0, 0x4, self, flags);  // DirectX::CGameThread
}
std::uintptr_t __fastcall ScalarDeletingDestructor_5b6570(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x005b65a0, 0x4, self, flags);  // DirectX::CGameDrawThread
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60bf90(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0060bfe0, 0x4, self, flags);  // Win32::CWin32Waitable
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60ca00(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0060c790, 0x4, self, flags);  // Win32::CEvent
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60cf70(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0060cfa0, 0x4, self, flags);  // Win32::CTimer
}
std::uintptr_t __fastcall ScalarDeletingDestructor_60dfc0(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0060dff0, 0x4, self, flags);  // Win32::CThread
}

// Purpose: Destroys a list box or dropdown that is a member of a dialog
// (its own destructor, then its virtual base CScreenElement).
std::uint32_t __fastcall CompleteDestructor_421fa0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x00421fc0, 0x444, self);
}
std::uint32_t __fastcall CompleteDestructor_4816d0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042b380, 0x66c, self);
}
std::uint32_t __fastcall CompleteDestructor_41e270(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0041b630, 0x60, self);  // CImageButton
}
std::uint32_t __fastcall CompleteDestructor_41eff0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0041f010, 0x2f4, self);  // CVScrollBar
}
std::uint32_t __fastcall CompleteDestructor_421f30(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x00421f50, 0x70, self);  // CEditControl
}
std::uint32_t __fastcall CompleteDestructor_423850(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x00416da0, 0x80, self);  // CScreenGroup
}
std::uint32_t __fastcall CompleteDestructor_439520(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042ab70, 0x74, self);  // CTextButton
}
std::uint32_t __fastcall CompleteDestructor_4416e0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042a380, 0x434, self);  // CTextView
}
std::uint32_t __fastcall CompleteDestructor_441700(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x00423870, 0x59c, self);  // CListView
}
std::uint32_t __fastcall CompleteDestructor_441720(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042a280, 0x68, self);  // CAnimatedButton
}
std::uint32_t __fastcall CompleteDestructor_476ef0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0041a900, 0x50, self);  // CButton
}
std::uint32_t __fastcall CompleteDestructor_486930(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042b100, 0x224, self);  // CUpDown
}

S3RE_HOOK(0x0044e300, ScalarDeletingDestructor_44e300, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44e300, ScalarDeletingDestructor_44e300);
S3RE_HOOK(0x0044e320, ScalarDeletingDestructor_44e320, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44e320, ScalarDeletingDestructor_44e320);
S3RE_HOOK(0x004639c0, ScalarDeletingDestructor_4639c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4639c0, ScalarDeletingDestructor_4639c0);
S3RE_HOOK(0x0046db20, ScalarDeletingDestructor_46db20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_46db20, ScalarDeletingDestructor_46db20);
S3RE_HOOK(0x0041d990, ScalarDeletingDestructor_41d990, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_41d990, ScalarDeletingDestructor_41d990);
S3RE_HOOK(0x004993d0, ScalarDeletingDestructor_4993d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4993d0, ScalarDeletingDestructor_4993d0);
S3RE_HOOK(0x0049cb50, ScalarDeletingDestructor_49cb50, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49cb50, ScalarDeletingDestructor_49cb50);
S3RE_HOOK(0x004a5b20, ScalarDeletingDestructor_4a5b20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a5b20, ScalarDeletingDestructor_4a5b20);
S3RE_HOOK(0x0042a710, ScalarDeletingDestructor_42a710, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a710, ScalarDeletingDestructor_42a710);
S3RE_HOOK(0x0042af60, ScalarDeletingDestructor_42af60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42af60, ScalarDeletingDestructor_42af60);
S3RE_HOOK(0x0042b340, ScalarDeletingDestructor_42b340, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42b340, ScalarDeletingDestructor_42b340);
S3RE_HOOK(0x0049b610, ScalarDeletingDestructor_49b610, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49b610, ScalarDeletingDestructor_49b610);
S3RE_HOOK(0x00405360, ScalarDeletingDestructor_405360, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_405360, ScalarDeletingDestructor_405360);
S3RE_HOOK(0x004281d0, ScalarDeletingDestructor_4281d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4281d0, ScalarDeletingDestructor_4281d0);
S3RE_HOOK(0x0040a1b0, ScalarDeletingDestructor_40a1b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_40a1b0, ScalarDeletingDestructor_40a1b0);
S3RE_HOOK(0x00414390, ScalarDeletingDestructor_414390, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_414390, ScalarDeletingDestructor_414390);
S3RE_HOOK(0x004281f0, ScalarDeletingDestructor_4281f0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4281f0, ScalarDeletingDestructor_4281f0);
S3RE_HOOK(0x00428210, ScalarDeletingDestructor_428210, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428210, ScalarDeletingDestructor_428210);
S3RE_HOOK(0x004375d0, ScalarDeletingDestructor_4375d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4375d0, ScalarDeletingDestructor_4375d0);
S3RE_HOOK(0x00437610, ScalarDeletingDestructor_437610, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_437610, ScalarDeletingDestructor_437610);
S3RE_HOOK(0x0044cd70, ScalarDeletingDestructor_44cd70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44cd70, ScalarDeletingDestructor_44cd70);
S3RE_HOOK(0x0044cd90, ScalarDeletingDestructor_44cd90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44cd90, ScalarDeletingDestructor_44cd90);
S3RE_HOOK(0x0044cdf0, ScalarDeletingDestructor_44cdf0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44cdf0, ScalarDeletingDestructor_44cdf0);
S3RE_HOOK(0x00457d10, ScalarDeletingDestructor_457d10, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_457d10, ScalarDeletingDestructor_457d10);
S3RE_HOOK(0x00462b60, ScalarDeletingDestructor_462b60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_462b60, ScalarDeletingDestructor_462b60);
S3RE_HOOK(0x00462c50, ScalarDeletingDestructor_462c50, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_462c50, ScalarDeletingDestructor_462c50);
S3RE_HOOK(0x004724e0, ScalarDeletingDestructor_4724e0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4724e0, ScalarDeletingDestructor_4724e0);
S3RE_HOOK(0x00472500, ScalarDeletingDestructor_472500, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_472500, ScalarDeletingDestructor_472500);
S3RE_HOOK(0x00472520, ScalarDeletingDestructor_472520, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_472520, ScalarDeletingDestructor_472520);
S3RE_HOOK(0x00472540, ScalarDeletingDestructor_472540, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_472540, ScalarDeletingDestructor_472540);
S3RE_HOOK(0x004726c0, ScalarDeletingDestructor_4726c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4726c0, ScalarDeletingDestructor_4726c0);
S3RE_HOOK(0x00499350, ScalarDeletingDestructor_499350, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_499350, ScalarDeletingDestructor_499350);
S3RE_HOOK(0x004993b0, ScalarDeletingDestructor_4993b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4993b0, ScalarDeletingDestructor_4993b0);
S3RE_HOOK(0x004375f0, ScalarDeletingDestructor_4375f0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4375f0, ScalarDeletingDestructor_4375f0);
S3RE_HOOK(0x00414b90, ScalarDeletingDestructor_414b90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_414b90, ScalarDeletingDestructor_414b90);
S3RE_HOOK(0x005c23b0, ScalarDeletingDestructor_5c23b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5c23b0, ScalarDeletingDestructor_5c23b0);
S3RE_HOOK(0x005c2780, ScalarDeletingDestructor_5c2780, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5c2780, ScalarDeletingDestructor_5c2780);
S3RE_HOOK(0x005c1810, ScalarDeletingDestructor_5c1810, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5c1810, ScalarDeletingDestructor_5c1810);
S3RE_HOOK(0x005b88c0, ScalarDeletingDestructor_5b88c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b88c0, ScalarDeletingDestructor_5b88c0);
S3RE_HOOK(0x005c34b0, ScalarDeletingDestructor_5c34b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5c34b0, ScalarDeletingDestructor_5c34b0);
S3RE_HOOK(0x005c34d0, ScalarDeletingDestructor_5c34d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5c34d0, ScalarDeletingDestructor_5c34d0);
S3RE_HOOK(0x005ffb20, ScalarDeletingDestructor_5ffb20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5ffb20, ScalarDeletingDestructor_5ffb20);
S3RE_HOOK(0x005ffb40, ScalarDeletingDestructor_5ffb40, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5ffb40, ScalarDeletingDestructor_5ffb40);
S3RE_HOOK(0x005ffbc0, ScalarDeletingDestructor_5ffbc0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5ffbc0, ScalarDeletingDestructor_5ffbc0);
S3RE_HOOK(0x005ffbe0, ScalarDeletingDestructor_5ffbe0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5ffbe0, ScalarDeletingDestructor_5ffbe0);
S3RE_HOOK(0x00614130, ScalarDeletingDestructor_614130, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_614130, ScalarDeletingDestructor_614130);
S3RE_HOOK(0x005b93a0, ScalarDeletingDestructor_5b93a0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b93a0, ScalarDeletingDestructor_5b93a0);
S3RE_HOOK(0x005be3c0, ScalarDeletingDestructor_5be3c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5be3c0, ScalarDeletingDestructor_5be3c0);
S3RE_HOOK(0x005bfff0, ScalarDeletingDestructor_5bfff0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5bfff0, ScalarDeletingDestructor_5bfff0);
S3RE_HOOK(0x0060e590, ScalarDeletingDestructor_60e590, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60e590, ScalarDeletingDestructor_60e590);
S3RE_HOOK(0x0060e5b0, ScalarDeletingDestructor_60e5b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60e5b0, ScalarDeletingDestructor_60e5b0);
S3RE_HOOK(0x00610810, ScalarDeletingDestructor_610810, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_610810, ScalarDeletingDestructor_610810);
S3RE_HOOK(0x00612770, ScalarDeletingDestructor_612770, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_612770, ScalarDeletingDestructor_612770);
S3RE_HOOK(0x00612c60, ScalarDeletingDestructor_612c60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_612c60, ScalarDeletingDestructor_612c60);
S3RE_HOOK(0x00613320, ScalarDeletingDestructor_613320, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_613320, ScalarDeletingDestructor_613320);
S3RE_HOOK(0x00613bb0, ScalarDeletingDestructor_613bb0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_613bb0, ScalarDeletingDestructor_613bb0);
S3RE_HOOK(0x00614ee0, ScalarDeletingDestructor_614ee0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_614ee0, ScalarDeletingDestructor_614ee0);
S3RE_HOOK(0x00615150, ScalarDeletingDestructor_615150, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_615150, ScalarDeletingDestructor_615150);
S3RE_HOOK(0x006003e0, ScalarDeletingDestructor_6003e0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_6003e0, ScalarDeletingDestructor_6003e0);
S3RE_HOOK(0x00421fa0, CompleteDestructor_421fa0, CompleteDestructorFn, Original_CompleteDestructor_421fa0,
          CompleteDestructor_421fa0);
S3RE_HOOK(0x004816d0, CompleteDestructor_4816d0, CompleteDestructorFn, Original_CompleteDestructor_4816d0,
          CompleteDestructor_4816d0);
S3RE_HOOK(0x0041e270, CompleteDestructor_41e270, CompleteDestructorFn, Original_CompleteDestructor_41e270,
          CompleteDestructor_41e270);
S3RE_HOOK(0x0041eff0, CompleteDestructor_41eff0, CompleteDestructorFn, Original_CompleteDestructor_41eff0,
          CompleteDestructor_41eff0);
S3RE_HOOK(0x00421f30, CompleteDestructor_421f30, CompleteDestructorFn, Original_CompleteDestructor_421f30,
          CompleteDestructor_421f30);
S3RE_HOOK(0x00423850, CompleteDestructor_423850, CompleteDestructorFn, Original_CompleteDestructor_423850,
          CompleteDestructor_423850);
S3RE_HOOK(0x00439520, CompleteDestructor_439520, CompleteDestructorFn, Original_CompleteDestructor_439520,
          CompleteDestructor_439520);
S3RE_HOOK(0x004416e0, CompleteDestructor_4416e0, CompleteDestructorFn, Original_CompleteDestructor_4416e0,
          CompleteDestructor_4416e0);
S3RE_HOOK(0x00441700, CompleteDestructor_441700, CompleteDestructorFn, Original_CompleteDestructor_441700,
          CompleteDestructor_441700);
S3RE_HOOK(0x00441720, CompleteDestructor_441720, CompleteDestructorFn, Original_CompleteDestructor_441720,
          CompleteDestructor_441720);
S3RE_HOOK(0x00476ef0, CompleteDestructor_476ef0, CompleteDestructorFn, Original_CompleteDestructor_476ef0,
          CompleteDestructor_476ef0);
S3RE_HOOK(0x00486930, CompleteDestructor_486930, CompleteDestructorFn, Original_CompleteDestructor_486930,
          CompleteDestructor_486930);

S3RE_HOOK(0x0041c560, ScalarDeletingDestructor_41c560, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_41c560, ScalarDeletingDestructor_41c560);
S3RE_HOOK(0x0041de80, ScalarDeletingDestructor_41de80, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_41de80, ScalarDeletingDestructor_41de80);
S3RE_HOOK(0x00428230, ScalarDeletingDestructor_428230, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428230, ScalarDeletingDestructor_428230);
S3RE_HOOK(0x00428250, ScalarDeletingDestructor_428250, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428250, ScalarDeletingDestructor_428250);
S3RE_HOOK(0x00428270, ScalarDeletingDestructor_428270, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428270, ScalarDeletingDestructor_428270);
S3RE_HOOK(0x00428320, ScalarDeletingDestructor_428320, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428320, ScalarDeletingDestructor_428320);
S3RE_HOOK(0x00428340, ScalarDeletingDestructor_428340, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428340, ScalarDeletingDestructor_428340);
S3RE_HOOK(0x00428360, ScalarDeletingDestructor_428360, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428360, ScalarDeletingDestructor_428360);
S3RE_HOOK(0x00428380, ScalarDeletingDestructor_428380, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_428380, ScalarDeletingDestructor_428380);
S3RE_HOOK(0x00429730, ScalarDeletingDestructor_429730, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_429730, ScalarDeletingDestructor_429730);
S3RE_HOOK(0x00429c30, ScalarDeletingDestructor_429c30, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_429c30, ScalarDeletingDestructor_429c30);
S3RE_HOOK(0x0042a060, ScalarDeletingDestructor_42a060, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a060, ScalarDeletingDestructor_42a060);
S3RE_HOOK(0x0042a250, ScalarDeletingDestructor_42a250, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a250, ScalarDeletingDestructor_42a250);
S3RE_HOOK(0x0042a340, ScalarDeletingDestructor_42a340, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a340, ScalarDeletingDestructor_42a340);
S3RE_HOOK(0x0042a630, ScalarDeletingDestructor_42a630, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a630, ScalarDeletingDestructor_42a630);
S3RE_HOOK(0x0042a8c0, ScalarDeletingDestructor_42a8c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a8c0, ScalarDeletingDestructor_42a8c0);
S3RE_HOOK(0x0042a980, ScalarDeletingDestructor_42a980, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a980, ScalarDeletingDestructor_42a980);
S3RE_HOOK(0x0042aa60, ScalarDeletingDestructor_42aa60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42aa60, ScalarDeletingDestructor_42aa60);
S3RE_HOOK(0x0042ab40, ScalarDeletingDestructor_42ab40, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42ab40, ScalarDeletingDestructor_42ab40);
S3RE_HOOK(0x0042ad20, ScalarDeletingDestructor_42ad20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42ad20, ScalarDeletingDestructor_42ad20);
S3RE_HOOK(0x0042ae10, ScalarDeletingDestructor_42ae10, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42ae10, ScalarDeletingDestructor_42ae10);
S3RE_HOOK(0x0042b0c0, ScalarDeletingDestructor_42b0c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42b0c0, ScalarDeletingDestructor_42b0c0);
S3RE_HOOK(0x0042b600, ScalarDeletingDestructor_42b600, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42b600, ScalarDeletingDestructor_42b600);
S3RE_HOOK(0x0042b770, ScalarDeletingDestructor_42b770, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42b770, ScalarDeletingDestructor_42b770);
S3RE_HOOK(0x0042c310, ScalarDeletingDestructor_42c310, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42c310, ScalarDeletingDestructor_42c310);
S3RE_HOOK(0x0042c9b0, ScalarDeletingDestructor_42c9b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42c9b0, ScalarDeletingDestructor_42c9b0);
S3RE_HOOK(0x00430440, ScalarDeletingDestructor_430440, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_430440, ScalarDeletingDestructor_430440);
S3RE_HOOK(0x004308b0, ScalarDeletingDestructor_4308b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4308b0, ScalarDeletingDestructor_4308b0);
S3RE_HOOK(0x00457d30, ScalarDeletingDestructor_457d30, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_457d30, ScalarDeletingDestructor_457d30);
S3RE_HOOK(0x00457d50, ScalarDeletingDestructor_457d50, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_457d50, ScalarDeletingDestructor_457d50);
S3RE_HOOK(0x00457d70, ScalarDeletingDestructor_457d70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_457d70, ScalarDeletingDestructor_457d70);
S3RE_HOOK(0x00457e80, ScalarDeletingDestructor_457e80, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_457e80, ScalarDeletingDestructor_457e80);
S3RE_HOOK(0x0045ad40, ScalarDeletingDestructor_45ad40, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_45ad40, ScalarDeletingDestructor_45ad40);
S3RE_HOOK(0x0045d4e0, ScalarDeletingDestructor_45d4e0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_45d4e0, ScalarDeletingDestructor_45d4e0);
S3RE_HOOK(0x0045d560, ScalarDeletingDestructor_45d560, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_45d560, ScalarDeletingDestructor_45d560);
S3RE_HOOK(0x00462b80, ScalarDeletingDestructor_462b80, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_462b80, ScalarDeletingDestructor_462b80);
S3RE_HOOK(0x00462ba0, ScalarDeletingDestructor_462ba0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_462ba0, ScalarDeletingDestructor_462ba0);
S3RE_HOOK(0x004639a0, ScalarDeletingDestructor_4639a0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4639a0, ScalarDeletingDestructor_4639a0);
S3RE_HOOK(0x00469050, ScalarDeletingDestructor_469050, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_469050, ScalarDeletingDestructor_469050);
S3RE_HOOK(0x00497e20, ScalarDeletingDestructor_497e20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_497e20, ScalarDeletingDestructor_497e20);
S3RE_HOOK(0x00499370, ScalarDeletingDestructor_499370, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_499370, ScalarDeletingDestructor_499370);
S3RE_HOOK(0x00499390, ScalarDeletingDestructor_499390, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_499390, ScalarDeletingDestructor_499390);
S3RE_HOOK(0x0049a190, ScalarDeletingDestructor_49a190, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49a190, ScalarDeletingDestructor_49a190);
S3RE_HOOK(0x0049a4b0, ScalarDeletingDestructor_49a4b0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49a4b0, ScalarDeletingDestructor_49a4b0);
S3RE_HOOK(0x0049a630, ScalarDeletingDestructor_49a630, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49a630, ScalarDeletingDestructor_49a630);
S3RE_HOOK(0x0049a7a0, ScalarDeletingDestructor_49a7a0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49a7a0, ScalarDeletingDestructor_49a7a0);
S3RE_HOOK(0x0049ab60, ScalarDeletingDestructor_49ab60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49ab60, ScalarDeletingDestructor_49ab60);
S3RE_HOOK(0x0049acd0, ScalarDeletingDestructor_49acd0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49acd0, ScalarDeletingDestructor_49acd0);
S3RE_HOOK(0x0049b180, ScalarDeletingDestructor_49b180, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49b180, ScalarDeletingDestructor_49b180);
S3RE_HOOK(0x0049b4a0, ScalarDeletingDestructor_49b4a0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49b4a0, ScalarDeletingDestructor_49b4a0);
S3RE_HOOK(0x0049b9d0, ScalarDeletingDestructor_49b9d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49b9d0, ScalarDeletingDestructor_49b9d0);
S3RE_HOOK(0x0049bb40, ScalarDeletingDestructor_49bb40, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49bb40, ScalarDeletingDestructor_49bb40);
S3RE_HOOK(0x0049bcb0, ScalarDeletingDestructor_49bcb0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49bcb0, ScalarDeletingDestructor_49bcb0);
S3RE_HOOK(0x0049da90, ScalarDeletingDestructor_49da90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49da90, ScalarDeletingDestructor_49da90);
S3RE_HOOK(0x004a6c00, ScalarDeletingDestructor_4a6c00, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a6c00, ScalarDeletingDestructor_4a6c00);
S3RE_HOOK(0x004a6c20, ScalarDeletingDestructor_4a6c20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a6c20, ScalarDeletingDestructor_4a6c20);
S3RE_HOOK(0x004a6ca0, ScalarDeletingDestructor_4a6ca0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a6ca0, ScalarDeletingDestructor_4a6ca0);
S3RE_HOOK(0x004a6cc0, ScalarDeletingDestructor_4a6cc0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a6cc0, ScalarDeletingDestructor_4a6cc0);
S3RE_HOOK(0x004a7840, ScalarDeletingDestructor_4a7840, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a7840, ScalarDeletingDestructor_4a7840);
S3RE_HOOK(0x004a8e70, ScalarDeletingDestructor_4a8e70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a8e70, ScalarDeletingDestructor_4a8e70);
S3RE_HOOK(0x004a8e90, ScalarDeletingDestructor_4a8e90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a8e90, ScalarDeletingDestructor_4a8e90);
// wip/app-r4: the framework classes'
S3RE_HOOK(0x005b5d10, ScalarDeletingDestructor_5b5d10, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b5d10, ScalarDeletingDestructor_5b5d10);
S3RE_HOOK(0x005b6d50, ScalarDeletingDestructor_5b6d50, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b6d50, ScalarDeletingDestructor_5b6d50);
S3RE_HOOK(0x005b7c70, ScalarDeletingDestructor_5b7c70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7c70, ScalarDeletingDestructor_5b7c70);
S3RE_HOOK(0x005b7c90, ScalarDeletingDestructor_5b7c90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7c90, ScalarDeletingDestructor_5b7c90);
S3RE_HOOK(0x005b7cb0, ScalarDeletingDestructor_5b7cb0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7cb0, ScalarDeletingDestructor_5b7cb0);
S3RE_HOOK(0x005b7cd0, ScalarDeletingDestructor_5b7cd0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7cd0, ScalarDeletingDestructor_5b7cd0);
S3RE_HOOK(0x005b7cf0, ScalarDeletingDestructor_5b7cf0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7cf0, ScalarDeletingDestructor_5b7cf0);
S3RE_HOOK(0x005b7d70, ScalarDeletingDestructor_5b7d70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7d70, ScalarDeletingDestructor_5b7d70);
S3RE_HOOK(0x005b7e30, ScalarDeletingDestructor_5b7e30, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b7e30, ScalarDeletingDestructor_5b7e30);
S3RE_HOOK(0x005b8440, ScalarDeletingDestructor_5b8440, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b8440, ScalarDeletingDestructor_5b8440);
S3RE_HOOK(0x005b8540, ScalarDeletingDestructor_5b8540, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b8540, ScalarDeletingDestructor_5b8540);
S3RE_HOOK(0x005b8fe0, ScalarDeletingDestructor_5b8fe0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b8fe0, ScalarDeletingDestructor_5b8fe0);
S3RE_HOOK(0x005b9e00, ScalarDeletingDestructor_5b9e00, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b9e00, ScalarDeletingDestructor_5b9e00);
S3RE_HOOK(0x005b9e20, ScalarDeletingDestructor_5b9e20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b9e20, ScalarDeletingDestructor_5b9e20);
S3RE_HOOK(0x005ba1a0, ScalarDeletingDestructor_5ba1a0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5ba1a0, ScalarDeletingDestructor_5ba1a0);
S3RE_HOOK(0x0060bc80, ScalarDeletingDestructor_60bc80, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60bc80, ScalarDeletingDestructor_60bc80);
S3RE_HOOK(0x0060be90, ScalarDeletingDestructor_60be90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60be90, ScalarDeletingDestructor_60be90);
S3RE_HOOK(0x0060f980, ScalarDeletingDestructor_60f980, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60f980, ScalarDeletingDestructor_60f980);
S3RE_HOOK(0x0060fe70, ScalarDeletingDestructor_60fe70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60fe70, ScalarDeletingDestructor_60fe70);
S3RE_HOOK(0x00612040, ScalarDeletingDestructor_612040, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_612040, ScalarDeletingDestructor_612040);
S3RE_HOOK(0x006154e0, ScalarDeletingDestructor_6154e0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_6154e0, ScalarDeletingDestructor_6154e0);
S3RE_HOOK(0x00616110, ScalarDeletingDestructor_616110, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_616110, ScalarDeletingDestructor_616110);
S3RE_HOOK(0x00616130, ScalarDeletingDestructor_616130, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_616130, ScalarDeletingDestructor_616130);
S3RE_HOOK(0x00616300, ScalarDeletingDestructor_616300, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_616300, ScalarDeletingDestructor_616300);
S3RE_HOOK(0x00616440, ScalarDeletingDestructor_616440, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_616440, ScalarDeletingDestructor_616440);
S3RE_HOOK(0x006195e0, ScalarDeletingDestructor_6195e0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_6195e0, ScalarDeletingDestructor_6195e0);
S3RE_HOOK(0x0061ab10, ScalarDeletingDestructor_61ab10, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_61ab10, ScalarDeletingDestructor_61ab10);
S3RE_HOOK(0x005b6070, ScalarDeletingDestructor_5b6070, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b6070, ScalarDeletingDestructor_5b6070);
S3RE_HOOK(0x005b6570, ScalarDeletingDestructor_5b6570, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_5b6570, ScalarDeletingDestructor_5b6570);
S3RE_HOOK(0x0060bf90, ScalarDeletingDestructor_60bf90, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60bf90, ScalarDeletingDestructor_60bf90);
S3RE_HOOK(0x0060ca00, ScalarDeletingDestructor_60ca00, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60ca00, ScalarDeletingDestructor_60ca00);
S3RE_HOOK(0x0060cf70, ScalarDeletingDestructor_60cf70, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60cf70, ScalarDeletingDestructor_60cf70);
S3RE_HOOK(0x0060dfc0, ScalarDeletingDestructor_60dfc0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_60dfc0, ScalarDeletingDestructor_60dfc0);

// ---------------------------------------------------------------------------
// Tests: each destructor with its callees (the class destructors and
// ~CScreenElement) replaced by recording stubs, on objects from the game's
// heap with random bytes, flags 0..3. Compared: the stub calls (`this`
// relative to the object), EAX relative to the object (the virtual-base
// destructors: as returned by the stub), and the object's first bytes when
// it is not freed.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kObjectBytes = 0x3300;  // more than the largest virtual-base offset (0x32e4)

struct DeletingCase {
    DeletingDestructorFn original;
    DeletingDestructorFn ours;
    std::uintptr_t callee;  // 0: the inline kind
    std::uint32_t offset;   // `this` inside the object
};

const DeletingCase kDeletingCases[] = {
    {Original_ScalarDeletingDestructor_44e300, &ScalarDeletingDestructor_44e300, 0x00401b60, 0},
    {Original_ScalarDeletingDestructor_44e320, &ScalarDeletingDestructor_44e320, 0x00441740, 0},
    {Original_ScalarDeletingDestructor_4639c0, &ScalarDeletingDestructor_4639c0, 0x0045f060, 0},
    {Original_ScalarDeletingDestructor_46db20, &ScalarDeletingDestructor_46db20, 0x0049d980, 0},
    {Original_ScalarDeletingDestructor_41d990, &ScalarDeletingDestructor_41d990, 0x0041d9b0, 0},
    {Original_ScalarDeletingDestructor_4993d0, &ScalarDeletingDestructor_4993d0, 0x00499620, 0},
    {Original_ScalarDeletingDestructor_49cb50, &ScalarDeletingDestructor_49cb50, 0x00434680, 0},
    {Original_ScalarDeletingDestructor_4a5b20, &ScalarDeletingDestructor_4a5b20, 0x004a5b50, 0},
    {Original_ScalarDeletingDestructor_42a710, &ScalarDeletingDestructor_42a710, 0x0042a750, 0x98},
    {Original_ScalarDeletingDestructor_42af60, &ScalarDeletingDestructor_42af60, 0x00421fc0, 0x444},
    {Original_ScalarDeletingDestructor_42b340, &ScalarDeletingDestructor_42b340, 0x0042b380, 0x66c},
    {Original_ScalarDeletingDestructor_49b610, &ScalarDeletingDestructor_49b610, 0x0049b650, 0x914},
    {Original_ScalarDeletingDestructor_40a1b0, &ScalarDeletingDestructor_40a1b0, 0x00409d50, 0},
    {Original_ScalarDeletingDestructor_414390, &ScalarDeletingDestructor_414390, 0x004143b0, 0},
    {Original_ScalarDeletingDestructor_4281f0, &ScalarDeletingDestructor_4281f0, 0x00427de0, 0},
    {Original_ScalarDeletingDestructor_428210, &ScalarDeletingDestructor_428210, 0x00427e40, 0},
    {Original_ScalarDeletingDestructor_4375d0, &ScalarDeletingDestructor_4375d0, 0x004141a0, 0},
    {Original_ScalarDeletingDestructor_437610, &ScalarDeletingDestructor_437610, 0x00437570, 0},
    {Original_ScalarDeletingDestructor_44cd70, &ScalarDeletingDestructor_44cd70, 0x0044c980, 0},
    {Original_ScalarDeletingDestructor_44cd90, &ScalarDeletingDestructor_44cd90, 0x0044caa0, 0},
    {Original_ScalarDeletingDestructor_44cdf0, &ScalarDeletingDestructor_44cdf0, 0x0044cca0, 0},
    {Original_ScalarDeletingDestructor_457d10, &ScalarDeletingDestructor_457d10, 0x00457a10, 0},
    {Original_ScalarDeletingDestructor_462b60, &ScalarDeletingDestructor_462b60, 0x00462870, 0},
    {Original_ScalarDeletingDestructor_462c50, &ScalarDeletingDestructor_462c50, 0x00462b00, 0},
    {Original_ScalarDeletingDestructor_4724e0, &ScalarDeletingDestructor_4724e0, 0x004721c0, 0},
    {Original_ScalarDeletingDestructor_472500, &ScalarDeletingDestructor_472500, 0x00472240, 0},
    {Original_ScalarDeletingDestructor_472520, &ScalarDeletingDestructor_472520, 0x004722e0, 0},
    {Original_ScalarDeletingDestructor_472540, &ScalarDeletingDestructor_472540, 0x00472360, 0},
    {Original_ScalarDeletingDestructor_4726c0, &ScalarDeletingDestructor_4726c0, 0x00472440, 0},
    {Original_ScalarDeletingDestructor_499350, &ScalarDeletingDestructor_499350, 0x004990c0, 0},
    {Original_ScalarDeletingDestructor_4993b0, &ScalarDeletingDestructor_4993b0, 0x004992f0, 0},
    {Original_ScalarDeletingDestructor_4375f0, &ScalarDeletingDestructor_4375f0, 0x00414240, 0},
    {Original_ScalarDeletingDestructor_414b90, &ScalarDeletingDestructor_414b90, 0x004a04d0, 0},
    {Original_ScalarDeletingDestructor_5c23b0, &ScalarDeletingDestructor_5c23b0, 0x005c23d0, 0},
    {Original_ScalarDeletingDestructor_5c2780, &ScalarDeletingDestructor_5c2780, 0x005c27a0, 0},
    {Original_ScalarDeletingDestructor_5c1810, &ScalarDeletingDestructor_5c1810, 0x005c1830, 0},
    {Original_ScalarDeletingDestructor_5b88c0, &ScalarDeletingDestructor_5b88c0, 0x005b88e0, 0},
    {Original_ScalarDeletingDestructor_5c34b0, &ScalarDeletingDestructor_5c34b0, 0x005c33e0, 0},
    {Original_ScalarDeletingDestructor_5c34d0, &ScalarDeletingDestructor_5c34d0, 0x005c3450, 0},
    {Original_ScalarDeletingDestructor_5ffb20, &ScalarDeletingDestructor_5ffb20, 0x005ffa60, 0},
    {Original_ScalarDeletingDestructor_5ffb40, &ScalarDeletingDestructor_5ffb40, 0x005ffb60, 0},
    {Original_ScalarDeletingDestructor_5ffbc0, &ScalarDeletingDestructor_5ffbc0, 0x005ffac0, 0},
    {Original_ScalarDeletingDestructor_5ffbe0, &ScalarDeletingDestructor_5ffbe0, 0x005ffc00, 0},
    {Original_ScalarDeletingDestructor_614130, &ScalarDeletingDestructor_614130, 0x00614150, 0},
    {Original_ScalarDeletingDestructor_5b93a0, &ScalarDeletingDestructor_5b93a0, 0x005b94c0, 0},
    {Original_ScalarDeletingDestructor_5be3c0, &ScalarDeletingDestructor_5be3c0, 0x005be3e0, 0},
    {Original_ScalarDeletingDestructor_5bfff0, &ScalarDeletingDestructor_5bfff0, 0x005c0010, 0},
    {Original_ScalarDeletingDestructor_60e590, &ScalarDeletingDestructor_60e590, 0x0060e490, 0},
    {Original_ScalarDeletingDestructor_60e5b0, &ScalarDeletingDestructor_60e5b0, 0x005b93c0, 0},
    {Original_ScalarDeletingDestructor_610810, &ScalarDeletingDestructor_610810, 0x00610830, 0},
    {Original_ScalarDeletingDestructor_612770, &ScalarDeletingDestructor_612770, 0x00612790, 0},
    {Original_ScalarDeletingDestructor_612c60, &ScalarDeletingDestructor_612c60, 0x00612d80, 0},
    {Original_ScalarDeletingDestructor_613320, &ScalarDeletingDestructor_613320, 0x00613340, 0},
    {Original_ScalarDeletingDestructor_613bb0, &ScalarDeletingDestructor_613bb0, 0x00613bd0, 0},
    {Original_ScalarDeletingDestructor_614ee0, &ScalarDeletingDestructor_614ee0, 0x00612c80, 0},
    {Original_ScalarDeletingDestructor_615150, &ScalarDeletingDestructor_615150, 0x00615170, 0},
    {Original_ScalarDeletingDestructor_6003e0, &ScalarDeletingDestructor_6003e0, 0x00600400, 0},
    {Original_ScalarDeletingDestructor_405360, &ScalarDeletingDestructor_405360, 0, 0},
    {Original_ScalarDeletingDestructor_4281d0, &ScalarDeletingDestructor_4281d0, 0, 0},
    {Original_ScalarDeletingDestructor_41c560, &ScalarDeletingDestructor_41c560, 0x0041c580, 0x0},
    {Original_ScalarDeletingDestructor_41de80, &ScalarDeletingDestructor_41de80, 0x0041dea0, 0x0},
    {Original_ScalarDeletingDestructor_428230, &ScalarDeletingDestructor_428230, 0x00427ea0, 0x0},
    {Original_ScalarDeletingDestructor_428250, &ScalarDeletingDestructor_428250, 0x00427f10, 0x0},
    {Original_ScalarDeletingDestructor_428270, &ScalarDeletingDestructor_428270, 0x00428290, 0x0},
    {Original_ScalarDeletingDestructor_428320, &ScalarDeletingDestructor_428320, 0x00427fe0, 0x0},
    {Original_ScalarDeletingDestructor_428340, &ScalarDeletingDestructor_428340, 0x00428070, 0x0},
    {Original_ScalarDeletingDestructor_428360, &ScalarDeletingDestructor_428360, 0x00428120, 0x0},
    {Original_ScalarDeletingDestructor_428380, &ScalarDeletingDestructor_428380, 0x004283a0, 0x0},
    {Original_ScalarDeletingDestructor_429730, &ScalarDeletingDestructor_429730, 0x004275e0, 0x0},
    {Original_ScalarDeletingDestructor_429c30, &ScalarDeletingDestructor_429c30, 0x00429c70, 0x10c},
    {Original_ScalarDeletingDestructor_42a060, &ScalarDeletingDestructor_42a060, 0x0042a0a0, 0xc4},
    {Original_ScalarDeletingDestructor_42a250, &ScalarDeletingDestructor_42a250, 0x0042a280, 0x68},
    {Original_ScalarDeletingDestructor_42a340, &ScalarDeletingDestructor_42a340, 0x0042a380, 0x434},
    {Original_ScalarDeletingDestructor_42a630, &ScalarDeletingDestructor_42a630, 0x00416da0, 0x80},
    {Original_ScalarDeletingDestructor_42a8c0, &ScalarDeletingDestructor_42a8c0, 0x004180a0, 0x3c},
    {Original_ScalarDeletingDestructor_42a980, &ScalarDeletingDestructor_42a980, 0x0041a900, 0x50},
    {Original_ScalarDeletingDestructor_42aa60, &ScalarDeletingDestructor_42aa60, 0x0041b630, 0x60},
    {Original_ScalarDeletingDestructor_42ab40, &ScalarDeletingDestructor_42ab40, 0x0042ab70, 0x74},
    {Original_ScalarDeletingDestructor_42ad20, &ScalarDeletingDestructor_42ad20, 0x00421f50, 0x70},
    {Original_ScalarDeletingDestructor_42ae10, &ScalarDeletingDestructor_42ae10, 0x0041e210, 0xb8},
    {Original_ScalarDeletingDestructor_42b0c0, &ScalarDeletingDestructor_42b0c0, 0x0042b100, 0x224},
    {Original_ScalarDeletingDestructor_42b600, &ScalarDeletingDestructor_42b600, 0x00423870, 0x59c},
    {Original_ScalarDeletingDestructor_42b770, &ScalarDeletingDestructor_42b770, 0x0041f010, 0x2f4},
    {Original_ScalarDeletingDestructor_42c310, &ScalarDeletingDestructor_42c310, 0x0042c340, 0x4},
    {Original_ScalarDeletingDestructor_42c9b0, &ScalarDeletingDestructor_42c9b0, 0x0042ca00, 0x0},
    {Original_ScalarDeletingDestructor_430440, &ScalarDeletingDestructor_430440, 0x00430460, 0x0},
    {Original_ScalarDeletingDestructor_4308b0, &ScalarDeletingDestructor_4308b0, 0x004308e0, 0x4},
    {Original_ScalarDeletingDestructor_457d30, &ScalarDeletingDestructor_457d30, 0x00457a90, 0x0},
    {Original_ScalarDeletingDestructor_457d50, &ScalarDeletingDestructor_457d50, 0x00457bb0, 0x0},
    {Original_ScalarDeletingDestructor_457d70, &ScalarDeletingDestructor_457d70, 0x00457d90, 0x0},
    {Original_ScalarDeletingDestructor_457e80, &ScalarDeletingDestructor_457e80, 0x00457cb0, 0x0},
    {Original_ScalarDeletingDestructor_45ad40, &ScalarDeletingDestructor_45ad40, 0x0045ad60, 0x0},
    {Original_ScalarDeletingDestructor_45d4e0, &ScalarDeletingDestructor_45d4e0, 0x0045d500, 0x0},
    {Original_ScalarDeletingDestructor_45d560, &ScalarDeletingDestructor_45d560, 0x0045d580, 0x0},
    {Original_ScalarDeletingDestructor_462b80, &ScalarDeletingDestructor_462b80, 0x004628f0, 0x0},
    {Original_ScalarDeletingDestructor_462ba0, &ScalarDeletingDestructor_462ba0, 0x00462bc0, 0x0},
    {Original_ScalarDeletingDestructor_4639a0, &ScalarDeletingDestructor_4639a0, 0x00463020, 0x0},
    {Original_ScalarDeletingDestructor_469050, &ScalarDeletingDestructor_469050, 0x00469070, 0x0},
    {Original_ScalarDeletingDestructor_497e20, &ScalarDeletingDestructor_497e20, 0x00497e40, 0x0},
    {Original_ScalarDeletingDestructor_499370, &ScalarDeletingDestructor_499370, 0x00499150, 0x0},
    {Original_ScalarDeletingDestructor_499390, &ScalarDeletingDestructor_499390, 0x00499290, 0x0},
    {Original_ScalarDeletingDestructor_49a190, &ScalarDeletingDestructor_49a190, 0x0049a1d0, 0x158},
    {Original_ScalarDeletingDestructor_49a4b0, &ScalarDeletingDestructor_49a4b0, 0x00492620, 0x1a8},
    {Original_ScalarDeletingDestructor_49a630, &ScalarDeletingDestructor_49a630, 0x00492c50, 0xf4},
    {Original_ScalarDeletingDestructor_49a7a0, &ScalarDeletingDestructor_49a7a0, 0x0049a7e0, 0x96c},
    {Original_ScalarDeletingDestructor_49ab60, &ScalarDeletingDestructor_49ab60, 0x00486950, 0x32e4},
    {Original_ScalarDeletingDestructor_49acd0, &ScalarDeletingDestructor_49acd0, 0x0049ad10, 0xbe0},
    {Original_ScalarDeletingDestructor_49b180, &ScalarDeletingDestructor_49b180, 0x0049b1c0, 0xaa4},
    {Original_ScalarDeletingDestructor_49b4a0, &ScalarDeletingDestructor_49b4a0, 0x004797e0, 0xaec},
    {Original_ScalarDeletingDestructor_49b9d0, &ScalarDeletingDestructor_49b9d0, 0x004816f0, 0x269c},
    {Original_ScalarDeletingDestructor_49bb40, &ScalarDeletingDestructor_49bb40, 0x00492270, 0x73c},
    {Original_ScalarDeletingDestructor_49bcb0, &ScalarDeletingDestructor_49bcb0, 0x0049bcf0, 0xcfc},
    {Original_ScalarDeletingDestructor_49da90, &ScalarDeletingDestructor_49da90, 0x0049dab0, 0x0},
    {Original_ScalarDeletingDestructor_4a6c00, &ScalarDeletingDestructor_4a6c00, 0x004a6a40, 0x0},
    {Original_ScalarDeletingDestructor_4a6c20, &ScalarDeletingDestructor_4a6c20, 0x004a6c40, 0x0},
    {Original_ScalarDeletingDestructor_4a6ca0, &ScalarDeletingDestructor_4a6ca0, 0x004a6b40, 0x0},
    {Original_ScalarDeletingDestructor_4a6cc0, &ScalarDeletingDestructor_4a6cc0, 0x004a6ce0, 0x0},
    {Original_ScalarDeletingDestructor_4a7840, &ScalarDeletingDestructor_4a7840, 0x004a7860, 0x0},
    {Original_ScalarDeletingDestructor_4a8e70, &ScalarDeletingDestructor_4a8e70, 0x004a8e10, 0x0},
    {Original_ScalarDeletingDestructor_4a8e90, &ScalarDeletingDestructor_4a8e90, 0x004a8eb0, 0x0},
    // wip/app-r4: the framework classes'
    {Original_ScalarDeletingDestructor_5b5d10, &ScalarDeletingDestructor_5b5d10, 0x005b5d30, 0x0},
    {Original_ScalarDeletingDestructor_5b6d50, &ScalarDeletingDestructor_5b6d50, 0x005b6d70, 0x0},
    {Original_ScalarDeletingDestructor_5b7c70, &ScalarDeletingDestructor_5b7c70, 0x005b7950, 0x0},
    {Original_ScalarDeletingDestructor_5b7c90, &ScalarDeletingDestructor_5b7c90, 0x005b79c0, 0x0},
    {Original_ScalarDeletingDestructor_5b7cb0, &ScalarDeletingDestructor_5b7cb0, 0x005b7a30, 0x0},
    {Original_ScalarDeletingDestructor_5b7cd0, &ScalarDeletingDestructor_5b7cd0, 0x005b7ac0, 0x0},
    {Original_ScalarDeletingDestructor_5b7cf0, &ScalarDeletingDestructor_5b7cf0, 0x005b7c10, 0x0},
    {Original_ScalarDeletingDestructor_5b7d70, &ScalarDeletingDestructor_5b7d70, 0x005b7d90, 0x0},
    {Original_ScalarDeletingDestructor_5b7e30, &ScalarDeletingDestructor_5b7e30, 0x005b7e50, 0x0},
    {Original_ScalarDeletingDestructor_5b8440, &ScalarDeletingDestructor_5b8440, 0x005b8460, 0x0},
    {Original_ScalarDeletingDestructor_5b8540, &ScalarDeletingDestructor_5b8540, 0x005b7670, 0x0},
    {Original_ScalarDeletingDestructor_5b8fe0, &ScalarDeletingDestructor_5b8fe0, 0x006100c0, 0x0},
    {Original_ScalarDeletingDestructor_5b9e00, &ScalarDeletingDestructor_5b9e00, 0x005b9e40, 0x0},
    {Original_ScalarDeletingDestructor_5b9e20, &ScalarDeletingDestructor_5b9e20, 0x005befd0, 0x0},
    {Original_ScalarDeletingDestructor_5ba1a0, &ScalarDeletingDestructor_5ba1a0, 0x005ba1c0, 0x0},
    {Original_ScalarDeletingDestructor_60bc80, &ScalarDeletingDestructor_60bc80, 0x00423a10, 0x0},
    {Original_ScalarDeletingDestructor_60be90, &ScalarDeletingDestructor_60be90, 0x0060beb0, 0x0},
    {Original_ScalarDeletingDestructor_60f980, &ScalarDeletingDestructor_60f980, 0x0060f9a0, 0x0},
    {Original_ScalarDeletingDestructor_60fe70, &ScalarDeletingDestructor_60fe70, 0x0060fe90, 0x0},
    {Original_ScalarDeletingDestructor_612040, &ScalarDeletingDestructor_612040, 0x00612060, 0x0},
    {Original_ScalarDeletingDestructor_6154e0, &ScalarDeletingDestructor_6154e0, 0x00615500, 0x0},
    {Original_ScalarDeletingDestructor_616110, &ScalarDeletingDestructor_616110, 0x00616050, 0x0},
    {Original_ScalarDeletingDestructor_616130, &ScalarDeletingDestructor_616130, 0x00616150, 0x0},
    {Original_ScalarDeletingDestructor_616300, &ScalarDeletingDestructor_616300, 0x00616320, 0x0},
    {Original_ScalarDeletingDestructor_616440, &ScalarDeletingDestructor_616440, 0x00616490, 0x0},
    {Original_ScalarDeletingDestructor_6195e0, &ScalarDeletingDestructor_6195e0, 0x00619600, 0x0},
    {Original_ScalarDeletingDestructor_61ab10, &ScalarDeletingDestructor_61ab10, 0x0061aab0, 0x0},
    {Original_ScalarDeletingDestructor_5b6070, &ScalarDeletingDestructor_5b6070, 0x005b60a0, 0x4},
    {Original_ScalarDeletingDestructor_5b6570, &ScalarDeletingDestructor_5b6570, 0x005b65a0, 0x4},
    {Original_ScalarDeletingDestructor_60bf90, &ScalarDeletingDestructor_60bf90, 0x0060bfe0, 0x4},
    {Original_ScalarDeletingDestructor_60ca00, &ScalarDeletingDestructor_60ca00, 0x0060c790, 0x4},
    {Original_ScalarDeletingDestructor_60cf70, &ScalarDeletingDestructor_60cf70, 0x0060cfa0, 0x4},
    {Original_ScalarDeletingDestructor_60dfc0, &ScalarDeletingDestructor_60dfc0, 0x0060dff0, 0x4},
};

struct CompleteCase {
    CompleteDestructorFn original;
    CompleteDestructorFn ours;
    std::uintptr_t callee;
    std::uint32_t offset;  // the member inside the object
};

const CompleteCase kCompleteCases[] = {
    {Original_CompleteDestructor_421fa0, &CompleteDestructor_421fa0, 0x00421fc0, 0x444},
    {Original_CompleteDestructor_4816d0, &CompleteDestructor_4816d0, 0x0042b380, 0x66c},
    {Original_CompleteDestructor_41e270, &CompleteDestructor_41e270, 0x0041b630, 0x60},
    {Original_CompleteDestructor_41eff0, &CompleteDestructor_41eff0, 0x0041f010, 0x2f4},
    {Original_CompleteDestructor_421f30, &CompleteDestructor_421f30, 0x00421f50, 0x70},
    {Original_CompleteDestructor_423850, &CompleteDestructor_423850, 0x00416da0, 0x80},
    {Original_CompleteDestructor_439520, &CompleteDestructor_439520, 0x0042ab70, 0x74},
    {Original_CompleteDestructor_4416e0, &CompleteDestructor_4416e0, 0x0042a380, 0x434},
    {Original_CompleteDestructor_441700, &CompleteDestructor_441700, 0x00423870, 0x59c},
    {Original_CompleteDestructor_441720, &CompleteDestructor_441720, 0x0042a280, 0x68},
    {Original_CompleteDestructor_476ef0, &CompleteDestructor_476ef0, 0x0041a900, 0x50},
    {Original_CompleteDestructor_486930, &CompleteDestructor_486930, 0x0042b100, 0x224},
};

// Installs the stubs a case needs (its class destructor, ~CScreenElement).
// The stub of ~CScreenElement returns a value fixed by the call, so the
// complete-object destructors' EAX is comparable.
template <std::uintptr_t... Callees>
void InstallMatching(test::CalleeStubs& stubs, std::uintptr_t callee) {
    ((callee == Callees ? stubs.Install<0, Callees>() : void()), ...);
}

void InstallStubs(test::CalleeStubs& stubs, std::uintptr_t callee) {
    InstallMatching<0x00401b60, 0x00441740, 0x0045f060, 0x0049d980, 0x0041d9b0, 0x00499620,
                  0x00434680, 0x004a5b50, 0x0042a750, 0x00421fc0, 0x0042b380, 0x0049b650,
                  0x00409d50, 0x004143b0, 0x00427de0, 0x00427e40, 0x004141a0, 0x00437570,
                  0x0044c980, 0x0044caa0, 0x0044cca0, 0x00457a10, 0x00462870, 0x00462b00,
                  0x004721c0, 0x00472240, 0x004722e0, 0x00472360, 0x00472440, 0x004990c0,
                  0x004992f0, 0x004a04d0, 0x00414240, 0x005c23d0, 0x005c27a0, 0x005c1830, 0x005b88e0,
                  0x005c33e0, 0x005c3450, 0x005ffa60, 0x005ffb60, 0x005ffac0, 0x005ffc00, 0x00614150>(stubs, callee);
    InstallMatching<
                  0x00416da0, 0x004180a0, 0x0041a900, 0x0041b630, 0x0041c580, 0x0041dea0,
                  0x0041e210, 0x0041f010, 0x00421f50, 0x00423870, 0x004275e0, 0x00427ea0,
                  0x00427f10, 0x00427fe0, 0x00428070, 0x00428120, 0x00428290, 0x004283a0,
                  0x00429c70, 0x0042a0a0, 0x0042a280, 0x0042a380, 0x0042ab70, 0x0042b100,
                  0x0042c340, 0x0042ca00, 0x00430460, 0x004308e0, 0x00457a90, 0x00457bb0,
                  0x00457cb0, 0x00457d90, 0x0045ad60, 0x0045d500, 0x0045d580, 0x004628f0,
                  0x00462bc0, 0x00463020, 0x00469070, 0x004797e0, 0x004816f0, 0x00486950,
                  0x00492270, 0x00492620, 0x00492c50, 0x00497e40, 0x00499150, 0x00499290,
                  0x0049a1d0, 0x0049a7e0, 0x0049ad10, 0x0049b1c0, 0x0049bcf0, 0x0049dab0,
                  0x004a6a40, 0x004a6b40, 0x004a6c40, 0x004a6ce0, 0x004a7860, 0x004a8e10,
                  0x004a8eb0>(stubs, callee);
    // session 58 (wip/framework-errors): the other exception classes' destructors
    InstallMatching<0x005b94c0, 0x005be3e0, 0x005c0010, 0x0060e490, 0x005b93c0, 0x00610830, 0x00612790,
                    0x00612d80, 0x00613340, 0x00613bd0, 0x00612c80, 0x00615170, 0x00600400>(stubs, callee);
    // wip/app-r4: the framework classes' destructors
    InstallMatching<0x005b5d30, 0x005b6d70, 0x005b7950, 0x005b79c0, 0x005b7a30, 0x005b7ac0, 0x005b7c10, 0x005b7d90, 0x005b7e50, 0x005b8460, 0x005b7670, 0x006100c0, 0x005b9e40, 0x005befd0, 0x005ba1c0, 0x00423a10, 0x0060beb0, 0x0060f9a0, 0x0060fe90, 0x00612060, 0x00615500, 0x00616050, 0x00616150, 0x00616320, 0x00616490, 0x00619600, 0x0061aab0, 0x005b60a0, 0x005b65a0, 0x0060bfe0, 0x0060c790, 0x0060cfa0, 0x0060dff0>(stubs, callee);
    stubs.Install<0, kScreenElementDestructor>(
        [](game::CGameData*, std::uint32_t self, const std::uint32_t*) { return self * 7 + 1; });
}

struct Run {
    std::uintptr_t eax;  // relative to the object
    std::vector<test::StubCall> calls;
    std::uint8_t bytes[16];  // the object's first bytes (when not freed)
};

template <typename Call>
Run RunCase(std::uintptr_t callee, const std::uint8_t* initial, bool freed, Call call) {
    auto* object = static_cast<std::uint8_t*>(GameOperatorNew(kObjectBytes));
    std::memcpy(object, initial, kObjectBytes);
    Run run{};
    {
        test::CalleeStubs stubs;
        InstallStubs(stubs, callee);
        stubs.SetWorld(reinterpret_cast<game::CGameData*>(object));
        run.eax = call(reinterpret_cast<std::uintptr_t>(object)) - reinterpret_cast<std::uintptr_t>(object);
        run.calls = stubs.Take();
    }
    if (!freed) {
        std::memcpy(run.bytes, object, sizeof(run.bytes));
        GameOperatorDelete(object);
    }
    return run;
}

bool CompareDeleting(test::Context& ctx, const DeletingCase& c, int cases) {
    std::vector<std::uint8_t> initial(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(initial.data(), initial.size());
        const std::uint32_t flags = ctx.rng().Below(4);
        const bool freed = (flags & 1) != 0;
        const Run a = RunCase(c.callee, initial.data(), freed, [&](std::uintptr_t object) {
            return c.original(object + c.offset, nullptr, flags);
        });
        const Run b = RunCase(c.callee, initial.data(), freed, [&](std::uintptr_t object) {
            return c.ours(object + c.offset, nullptr, flags);
        });
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls) ||
            !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes))) {
            ctx.Fail(__FILE__, __LINE__, "  callee 0x%x case %d: flags %u", c.callee, i, flags);
            return false;
        }
    }
    return true;
}

bool CompareComplete(test::Context& ctx, const CompleteCase& c, int cases) {
    std::vector<std::uint8_t> initial(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(initial.data(), initial.size());
        const Run a = RunCase(c.callee, initial.data(), false,
                              [&](std::uintptr_t object) { return c.original(object, nullptr); });
        const Run b = RunCase(c.callee, initial.data(), false,
                              [&](std::uintptr_t object) { return c.ours(object, nullptr); });
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls) ||
            !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes))) {
            ctx.Fail(__FILE__, __LINE__, "  callee 0x%x case %d", c.callee, i);
            return false;
        }
    }
    return true;
}

// Wrong implementations.
std::uintptr_t __fastcall WrongVirtualBaseDeleting(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042b380, 0x66c, self, flags) + 0x66c;  // returns `this`, not the object
}
std::uintptr_t __fastcall WrongInline(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCIterExcept, self, flags);  // 0x405360 stores WCExcept
}
std::uintptr_t __fastcall WrongSubobject(std::uintptr_t self, void*, std::uint32_t flags) {
    return SubobjectDeletingDestructor(0x0042c340, 0, self, flags);  // frees `this`, not this - 4
}
std::uint32_t __fastcall WrongComplete(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042b380, 0x444, self);  // the list box's offset
}

}  // namespace

S3RE_TEST(DeletingDestructors_MatchOriginal) {
    for (const DeletingCase& c : kDeletingCases) {
        if (!CompareDeleting(ctx, c, 200)) {
            return;
        }
    }
    for (const CompleteCase& c : kCompleteCases) {
        if (!CompareComplete(ctx, c, 200)) {
            return;
        }
    }
}

S3RE_TEST(DeletingDestructors_ComparisonDetectsWrongImplementation) {
    const DeletingCase wrong[] = {
        {Original_ScalarDeletingDestructor_42b340, &WrongVirtualBaseDeleting, 0x0042b380, 0x66c},
        {Original_ScalarDeletingDestructor_405360, &WrongInline, 0, 0},
        {Original_ScalarDeletingDestructor_42c310, &WrongSubobject, 0x0042c340, 4},
    };
    for (const DeletingCase& c : wrong) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDeleting(probe, c, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareComplete(probe, {Original_CompleteDestructor_4816d0, &WrongComplete, 0x0042b380, 0x66c}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::msvc6
