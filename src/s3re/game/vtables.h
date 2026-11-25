#pragma once

// Compiler-generated class data in S3.EXE's .rdata that our replacements
// store or pass: vtables (class names from the RTTI, re/classes.md),
// virtual-base tables and C++ exception throw information. These are values
// (an object's vtable pointer is set to them), so they are plain constants;
// each address appears only here (see game/global_access.h).

#include <cstdint>

namespace s3re::game {

namespace vtables {
// Win32::*
inline constexpr std::uint32_t kCObject = 0x65d67c;            // Win32::CObject
inline constexpr std::uint32_t kCWin32Handle = 0x65d654;       // Win32::CWin32Handle
inline constexpr std::uint32_t kCWaitable = 0x65d3c8;          // Win32::CWaitable
inline constexpr std::uint32_t kCWin32Waitable = 0x65d660;     // Win32::CWin32Waitable
inline constexpr std::uint32_t kCWin32Waitable_sub4 = 0x65d670;  // its CWin32Handle part at +4
inline constexpr std::uint32_t kCWindowBase = 0x65d854;        // Win32::CWindowBase
inline constexpr std::uint32_t kCMessageQueue = 0x65d6f4;      // Win32::CMessageQueue
inline constexpr std::uint32_t kCThread = 0x65d710;            // Win32::CThread
inline constexpr std::uint32_t kCThread_sub4 = 0x65d704;       // its waitable part at +4
inline constexpr std::uint32_t kCEvent = 0x65d690;             // Win32::CEvent
inline constexpr std::uint32_t kCEvent_sub4 = 0x65d684;        // its handle part at +4
inline constexpr std::uint32_t kCFile = 0x65d9b8;              // Win32::CFile
inline constexpr std::uint32_t kCFileMapping = 0x65d9c4;       // Win32::CFileMapping
inline constexpr std::uint32_t kCGraphicsData = 0x65d3e4;      // CGraphicsData
// Its view list's vtables: WCValOrderedVector<void*> and the base classes
// the constructor and destructor pass through.
inline constexpr std::uint32_t kMappingViewVector = 0x65d9d0;  // WCValOrderedVector<void*>
inline constexpr std::uint32_t kMappingViewVectorBase = 0x657440;  // WCBareVectorBase<void*>
inline constexpr std::uint32_t kMappingViewVectorRoot = 0x655438;  // the destructor's last vtable
inline constexpr std::uint32_t kCRegistryKey = 0x65d98c;       // Win32::CRegistryKey
inline constexpr std::uint32_t kCRegistryValueInfo = 0x65d984;  // Win32::CRegistryValueInfo
inline constexpr std::uint32_t kCException = 0x65d890;         // Win32::CException
inline constexpr std::uint32_t kCRunTimeError = 0x65d744;      // Win32::CRunTimeError
inline constexpr std::uint32_t kCProgrammingError = 0x65d91c;  // Win32::CProgrammingError
inline constexpr std::uint32_t kCAssertmentFailed = 0x65d8cc;  // Win32::CAssertmentFailed
inline constexpr std::uint32_t kCOutOfMemory = 0x65d8b8;       // Win32::COutOfMemory
// Dirk::CMapFile and its chunk cache (Watcom skip-list dictionaries)
inline constexpr std::uint32_t kCMapFile = 0x658770;               // Dirk::CMapFile
inline constexpr std::uint32_t kChunkDict = 0x658794;              // WCValSkipListDict<unsigned int, void*>
inline constexpr std::uint32_t kReferenceDict = 0x658778;          // WCValSkipListDict<unsigned int, int>
// The skip-list base classes the dictionaries pass through while
// ~CMapFile (0x4a5b50) destroys them.
inline constexpr std::uint32_t kReferenceSkipListBase = 0x6587cc;  // WCSkipListBase<WCSkipListDictKeyVal<uint, int>>
inline constexpr std::uint32_t kChunkSkipListBase = 0x6587b0;      // WCSkipListBase<WCSkipListDictKeyVal<uint, void*>>
// DirectX::*
inline constexpr std::uint32_t kCSimpleSurface = 0x65cda0;      // DirectX::CSimpleSurface
inline constexpr std::uint32_t kCSurface = 0x65cda8;            // DirectX::CSurface
inline constexpr std::uint32_t kCDirectSoundBuffer = 0x65cecc;  // DirectX::CDirectSoundBuffer
inline constexpr std::uint32_t kCLandscapeSurface = 0x65a520;   // CLandscapeSurface
// UI
inline constexpr std::uint32_t kCScreenElement = 0x655a68;  // CScreenElement
inline constexpr std::uint32_t kCStaticControl = 0x6560d8;  // CStaticControl
inline constexpr std::uint32_t kCStaticText = 0x65615c;     // CStaticText
inline constexpr std::uint32_t kCStaticImage = 0x6561e0;    // CStaticImage
inline constexpr std::uint32_t kCStaticFrame = 0x656264;    // CStaticFrame
inline constexpr std::uint32_t kCStaticColorIndexedFrame = 0x6562e8;  // CStaticColorIndexedFrame
// Lobby::*
inline constexpr std::uint32_t kCMemoryBlock = 0x65d4f4;    // Lobby::CMemoryBlock
inline constexpr std::uint32_t kCLMMessageBlock = 0x65d4fc;  // Lobby::CLMMessageBlock
inline constexpr std::uint32_t kCLMGeneric = 0x65d52c;      // Lobby::CLMGeneric
inline constexpr std::uint32_t kCPlayerIndex = 0x65d5ec;    // Lobby::priv::CPlayerIndex
inline constexpr std::uint32_t kCDataError = 0x65d4b8;      // Lobby::priv::CDataError
// Computer players (Rainer\*)
inline constexpr std::uint32_t kCComputerPlayer = 0x65a530;  // CComputerPlayer
inline constexpr std::uint32_t kBlackBoardNote = 0x65a56c;    // BLACK_BOARD_NOTE
inline constexpr std::uint32_t kBlackBoardAgent = 0x65a828;   // BLACK_BOARD_AGENT (the base class)
inline constexpr std::uint32_t kDoBuildHouseNote = 0x65a584;  // T_DO_BUILD_HOUSE_NOTE
inline constexpr std::uint32_t kHouseRequestNote = 0x65a558;        // HOUSE_REQUEST_NOTE
inline constexpr std::uint32_t kHouseGroundRequestNote = 0x65a598;  // HOUSE_GROUND_REQUEST_NOTE
inline constexpr std::uint32_t kLocalWareRequestNote = 0x65a5ac;    // LOCAL_WARE_REQUEST_NOTE
// The agents (made by AI::CreateAgent 0x56f6d0; kinds in ai::AgentKind).
inline constexpr std::uint32_t kLocalWareRequestAgent = 0x65a7f0;    // LOCAL_WARE_REQUEST_AGENT (kind 0)
inline constexpr std::uint32_t kHouseGroundRequestAgent = 0x65a7b8;  // HOUSE_GROUND_REQUEST_AGENT (1)
inline constexpr std::uint32_t kHouseRequestAgent = 0x65a780;        // HOUSE_REQUEST_AGENT (2)
inline constexpr std::uint32_t kGlobalExpandEmpireAgent = 0x65a748;  // GLOBAL_EXPAND_EMPIRE_AGENT (3)
inline constexpr std::uint32_t kDoBuildHouseAgent = 0x65a710;        // T_DO_BUILD_HOUSE_AGENT (4)
inline constexpr std::uint32_t kComputerPlayerAgent = 0x65a6d8;      // COMPUTER_PLAYER_AGENT (5)
inline constexpr std::uint32_t kMapWorkerAgent = 0x65a6a0;           // T_MAP_WORKER_AGENT (6)
inline constexpr std::uint32_t kAttackAgent = 0x65a668;              // T_ATTACK_AGENT (7)
inline constexpr std::uint32_t kEconomyAgent = 0x65a630;             // T_ECONOMY_AGENT (8)
inline constexpr std::uint32_t kSafetyAgent = 0x65a5f8;              // T_SAFETY_AGENT (9)
inline constexpr std::uint32_t kCentrumCheckAgent = 0x65a5c0;        // CCentrumCheckAgent (10)
// Watcom container library
inline constexpr std::uint32_t kWCExcept = 0x655438;                    // WCExcept (base of the containers)
inline constexpr std::uint32_t kWCIsvListBase = 0x655fa4;               // WCIsvListBase
inline constexpr std::uint32_t kWCIterExcept = 0x656a38;                // WCIterExcept (base of the iterators)
inline constexpr std::uint32_t kWCIsvSListBase_CScreenElementP = 0x655bc4;  // WCIsvSListBase<WCNIsvSLink<CScreenElement*>>
inline constexpr std::uint32_t kWCIsvSListBase_bool = 0x655be0;            // WCIsvSListBase<WCNIsvSLink<bool>>
// WCSkipListIterBase<WCSkipListDictKeyVal<K, V>> (watcom/list_iterators.cpp)
inline constexpr std::uint32_t kWCSkipListIterBase_CExecuteTypes = 0x65778c;  // <unsigned long, WCPtrSortedVector<CExecuteType>>
inline constexpr std::uint32_t kWCSkipListIterBase_CSyncInfo = 0x657794;      // <unsigned long, CNetSession::CSyncInfo>
inline constexpr std::uint32_t kWCSkipListIterBase_IntString = 0x65854c;      // <int, String>
inline constexpr std::uint32_t kWCBareVectorBase_int = 0x655a5c;        // WCBareVectorBase<int>
inline constexpr std::uint32_t kWCVectorBase_int = 0x6559f8;            // WCVectorBase<int>
inline constexpr std::uint32_t kWCBareVectorBase_uint = 0x657158;       // WCBareVectorBase<unsigned int>
inline constexpr std::uint32_t kWCBareVectorBase_ushort = 0x657404;     // WCBareVectorBase<unsigned short>
inline constexpr std::uint32_t kWCValOrderedVector_ushort = 0x657410;  // WCValOrderedVector<unsigned short>
// More WCBareVectorBase<T> instantiations (watcom/bare_vectors.cpp)
inline constexpr std::uint32_t kWCBareVectorBase_voidp = 0x657440;            // WCBareVectorBase<void*>
inline constexpr std::uint32_t kWCBareVectorBase_HFONT = 0x656a14;            // WCBareVectorBase<HFONT__*>
inline constexpr std::uint32_t kWCBareVectorBase_CScreenElementP = 0x655bb8;  // WCBareVectorBase<CScreenElement*>
inline constexpr std::uint32_t kWCBareVectorBase_CStatusMessage = 0x65702c;   // <LobbyClient::CStatusMessage>
inline constexpr std::uint32_t kWCBareVectorBase_ulong = 0x6576a0;            // WCBareVectorBase<unsigned long>
inline constexpr std::uint32_t kWCBareVectorBase_long = 0x65852c;             // WCBareVectorBase<long>
inline constexpr std::uint32_t kWCBareVectorBase_GameChunkDescription = 0x657e80;
inline constexpr std::uint32_t kWCBareVectorBase_String = 0x655454;           // WCBareVectorBase<String>
inline constexpr std::uint32_t kWCValOrderedVector_String = 0x655440;         // WCValOrderedVector<String>
inline constexpr std::uint32_t kWCBareVectorBase_CPlayer = 0x655a50;          // WCBareVectorBase<Dirk::CPlayer>
inline constexpr std::uint32_t kWCBareVectorBase_CTeam = 0x6559d8;            // WCBareVectorBase<Dirk::CTeam>
inline constexpr std::uint32_t kWCBareVectorBase_CChatMessage = 0x657164;     // <LobbyClient::CChatMessage>
inline constexpr std::uint32_t kWCBareVectorBase_CGroup = 0x657094;           // <LobbyClient::CGroup>
inline constexpr std::uint32_t kWCBareVectorBase_CConnectionShortcut = 0x657364;  // <DirectX::CConnectionShortcut>
inline constexpr std::uint32_t kWCBareVectorBase_CRunningSession = 0x65739c;  // <DirectX::CRunningSession>
// The classes the (capacity, grow) constructors end with.
inline constexpr std::uint32_t kWCOrderedVectorBase_int = 0x656b98;           // WCOrderedVectorBase<int>
inline constexpr std::uint32_t kWCVectorBase_uint = 0x657170;                 // WCVectorBase<unsigned int>
inline constexpr std::uint32_t kWCSortedVectorBase_voidp = 0x6573d4;          // WCSortedVectorBase<void*>
inline constexpr std::uint32_t kWCOrderedVectorBase_uint = 0x6574bc;          // WCOrderedVectorBase<unsigned int>
inline constexpr std::uint32_t kWCSortedVectorBase_ulong = 0x65776c;          // WCSortedVectorBase<unsigned long>
inline constexpr std::uint32_t kWCOrderedVectorBase_voidp = 0x65779c;         // WCOrderedVectorBase<void*>
inline constexpr std::uint32_t kWCPtrSortedVector_CExecuteType = 0x657670;    // WCPtrSortedVector<CExecuteType>
inline constexpr std::uint32_t kWCVectorBase_ulong = 0x65765c;                // WCVectorBase<unsigned long>
inline constexpr std::uint32_t kWCVectorBase_CChatMessage = 0x657108;         // WCVectorBase<LobbyClient::CChatMessage>
inline constexpr std::uint32_t kWCOrderedVectorBase_CChatMessage = 0x65711c;  // <LobbyClient::CChatMessage>
inline constexpr std::uint32_t kWCSortedVectorBase_CGroup = 0x6570b0;         // <LobbyClient::CGroup>
inline constexpr std::uint32_t kWCVectorBase_CGroup = 0x6571d0;               // WCVectorBase<LobbyClient::CGroup>
inline constexpr std::uint32_t kWCOrderedVectorBase_String = 0x657130;        // WCOrderedVectorBase<String>
inline constexpr std::uint32_t kWCVectorBase_CConnectionShortcut = 0x6574e4;  // <DirectX::CConnectionShortcut>
inline constexpr std::uint32_t kWCVectorBase_CRunningSession = 0x657370;      // <DirectX::CRunningSession>
// Dirk::CTeam's members (game/ui/game_teams.cpp)
inline constexpr std::uint32_t kWCValSortedVector_int = 0x655a0c;             // WCValSortedVector<int> (+8)
inline constexpr std::uint32_t kWCValOrderedVector_CPlayer = 0x656b6c;        // WCValOrderedVector<Dirk::CPlayer> (+0x24)
// Standard library
inline constexpr std::uint32_t kStdLogicError = 0x65a548;  // std::logic_error
inline constexpr std::uint32_t kStdOutOfRange = 0x65a538;  // std::out_of_range
}  // namespace vtables

// Virtual-base tables (vbptr targets) of the exception classes with a
// virtual base.
namespace vbtables {
inline constexpr std::uint32_t kCException = 0x65d898;         // Win32::CException
inline constexpr std::uint32_t kCRunTimeError = 0x65d884;      // Win32::CRunTimeError
inline constexpr std::uint32_t kCProgrammingError = 0x65d924;  // Win32::CProgrammingError
inline constexpr std::uint32_t kCAssertmentFailed = 0x65d8d4;  // Win32::CAssertmentFailed
inline constexpr std::uint32_t kCOutOfMemory = 0x65d8c0;       // Win32::COutOfMemory
inline constexpr std::uint32_t kCDataError = 0x65d4c0;         // Lobby::priv::CDataError
}  // namespace vbtables

// _ThrowInfo records passed to _CxxThrowException.
namespace throw_info {
inline constexpr std::uintptr_t kCAssertmentFailed = 0x68edf0;
inline constexpr std::uintptr_t kCOutOfMemory = 0x682f58;
inline constexpr std::uintptr_t kCDataError = 0x68bfd0;
inline constexpr std::uintptr_t kDirectDrawException = 0x66a698;
inline constexpr std::uintptr_t kDirectPlayException = 0x681e60;
inline constexpr std::uintptr_t kCTimeoutPointer = 0x674f38;  // Win32::CTimeout* (thrown by the queue waits)
inline constexpr std::uintptr_t kCFileNotFoundPointer = 0x67b828;  // Win32::CFileNotFound* (CFile::Open)
}  // namespace throw_info

// Type descriptors (catch types).
namespace type_descriptor {
inline constexpr std::uintptr_t kCTimeoutPointer = 0x698e98;  // ".PAVCTimeout@Win32@@" (ExecuteCommands' catch)
inline constexpr std::uintptr_t kCHResultPointer = 0x6968d8;  // ".PAVCHResult@Win32@@" (CMapFileStream's catches)
inline constexpr std::uintptr_t kCFileNotFoundPointer = 0x69f908;  // ".PAVCFileNotFound@Win32@@" (CMapFileStream::Open)
}  // namespace type_descriptor

}  // namespace s3re::game
