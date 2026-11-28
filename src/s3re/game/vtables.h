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
inline constexpr std::uint32_t kCGameData = 0x658c18;          // CGameData (the world object)
inline constexpr std::uint32_t kCWin32Handle = 0x65d654;       // Win32::CWin32Handle
inline constexpr std::uint32_t kCWaitable = 0x65d3c8;          // Win32::CWaitable
inline constexpr std::uint32_t kCWin32Waitable = 0x65d660;     // Win32::CWin32Waitable
inline constexpr std::uint32_t kCWin32Waitable_sub4 = 0x65d670;  // its CWin32Handle part at +4
inline constexpr std::uint32_t kCWindowBase = 0x65d854;        // Win32::CWindowBase
inline constexpr std::uint32_t kCWindow = 0x65d85c;            // Win32::CWindow
inline constexpr std::uint32_t kCMainWindow = 0x65d870;        // Win32::CMainWindow
inline constexpr std::uint32_t kCDirectXMainWindow = 0x65cc30;  // DirectX::CDirectXMainWindow
inline constexpr std::uint32_t kCGameMainWindow = 0x65c9c8;     // DirectX::CGameMainWindow
inline constexpr std::uint32_t kCSemaphore = 0x65d6ac;          // Win32::CSemaphore
inline constexpr std::uint32_t kCSemaphore_sub4 = 0x65d6a0;     // its CWin32Handle part at +4
inline constexpr std::uint32_t kCMutex = 0x65d6c8;              // Win32::CMutex
inline constexpr std::uint32_t kCMutex_sub4 = 0x65d6bc;         // its CWin32Handle part at +4
inline constexpr std::uint32_t kCApplication = 0x65d78c;        // Win32::CApplication
inline constexpr std::uint32_t kCApplication_vbase = 0x65d764;  // its virtual base (+0x10 in RTTI)
inline constexpr std::uint32_t kCApplication_vbase4 = 0x65d758;  // the virtual base's +4 part
inline constexpr std::uint32_t kCMessageQueue = 0x65d6f4;      // Win32::CMessageQueue
inline constexpr std::uint32_t kCThread = 0x65d710;            // Win32::CThread
inline constexpr std::uint32_t kCThread_sub4 = 0x65d704;       // its waitable part at +4
inline constexpr std::uint32_t kCGameThread = 0x65ca04;        // DirectX::CGameThread
inline constexpr std::uint32_t kCGameThread_sub4 = 0x65c9f8;   // its handle part at +4
inline constexpr std::uint32_t kCGameDrawThread = 0x65ca40;       // DirectX::CGameDrawThread
inline constexpr std::uint32_t kCGameDrawThread_sub4 = 0x65ca34;  // its handle part at +4
inline constexpr std::uint32_t kCDrawThread = 0x656ab0;           // CDrawThread (the in-game draw thread)
inline constexpr std::uint32_t kCDrawThread_sub4 = 0x656aa4;      // its handle part at +4
inline constexpr std::uint32_t kCGameDrawer = 0x65ca7c;           // DirectX::CGameDrawer
inline constexpr std::uint32_t kCDrawer = 0x656aec;               // CDrawer (the in-game drawer)
// CGameDrawer's containers (stored by its constructor 0x5b6c80 after the base constructors').
inline constexpr std::uint32_t kWCPtrSList_CSurface = 0x65cae8;                  // +0x10 the surface list
inline constexpr std::uint32_t kWCPtrSList_CPalette = 0x65cad8;                  // +0x28 the palette list
inline constexpr std::uint32_t kWCValOrderedVector_CDirectDrawDevice = 0x65cac4; // +0x40 the devices
inline constexpr std::uint32_t kWCValOrderedVector_CDisplayMode = 0x65cab0;      // +0x5c the display modes
inline constexpr std::uint32_t kCLogicThread = 0x65a14c;       // CLogicThread
inline constexpr std::uint32_t kCLogicThread_sub4 = 0x65a140;  // its handle part at +4
inline constexpr std::uint32_t kCWin32WaitableBag = 0x65733c;  // Win32::CWin32WaitableBag<CWin32Waitable>
inline constexpr std::uint32_t kWaitablePtrSList = 0x65732c;   // its WCPtrSList<CWin32Waitable> at +4
inline constexpr std::uint32_t kWaitableValListBase = 0x657430;  // WCValListBase<CWin32Waitable*, ...>
inline constexpr std::uint32_t kWaitableIsvSListBase = 0x657424;  // WCIsvSListBase<WCNIsvSLink<CWin32Waitable*>>
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
inline constexpr std::uint32_t kCDirectSound = 0x65ceb8;        // DirectX::CDirectSound
inline constexpr std::uint32_t kCPrimaryBuffer = 0x65cee0;      // DirectX::CPrimaryBuffer
inline constexpr std::uint32_t kCSecondaryBuffer = 0x65cef4;    // DirectX::CSecondaryBuffer
inline constexpr std::uint32_t kCSoundEffectBuffer = 0x65cf38;  // DirectX::CSoundEffectBuffer
inline constexpr std::uint32_t kCSoundFile = 0x65cf08;          // DirectX::CSoundFile
inline constexpr std::uint32_t kCMemorySoundFile = 0x65cf20;    // DirectX::CMemorySoundFile
inline constexpr std::uint32_t kCSoundEffect = 0x65cf4c;        // DirectX::CSoundEffect
inline constexpr std::uint32_t kCDirectSoundException = 0x65cea4;   // DirectX::CDirectSoundException
inline constexpr std::uint32_t kCAppNotActiveException = 0x65cbb0;  // DirectX::CAppNotActiveException
inline constexpr std::uint32_t kCCDAudio = 0x65d944;               // Win32::CCDAudio
// CSoundEffect's dictionary of duplicate buffers and its iterators
inline constexpr std::uint32_t kEffectBufferDict = 0x65cf54;          // WCValSkipListDict<int, CSoundEffectBuffer>
inline constexpr std::uint32_t kEffectBufferSkipListBase = 0x65cf70;  // WCSkipListBase<...int, CSoundEffectBuffer...>
inline constexpr std::uint32_t kEffectBufferDictIter = 0x65cf8c;      // WCValSkipListDictIter<int, CSoundEffectBuffer>
inline constexpr std::uint32_t kEffectBufferIterBase = 0x65cf94;      // WCSkipListIterBase<...int, CSoundEffectBuffer...>
inline constexpr std::uint32_t kSoundEffectIterBase = 0x65d478;       // WCSkipListIterBase<...uint, CSoundEffect*...>
inline constexpr std::uint32_t kWCValOrderedVector_int = 0x656b58;    // WCValOrderedVector<int>
// CSoundData (Dirk\SoundData.cpp) and its two skip-list dictionaries
inline constexpr std::uint32_t kCSoundData = 0x65d3ec;               // CSoundData
inline constexpr std::uint32_t kSoundEffectDict = 0x65d41c;          // WCValSkipListDict<uint, DirectX::CSoundEffect*>
inline constexpr std::uint32_t kSoundUseDict = 0x65d400;             // WCValSkipListDict<uint, uint>
inline constexpr std::uint32_t kSoundEffectSkipListBase = 0x65d438;  // WCSkipListBase<...CSoundEffect...>
inline constexpr std::uint32_t kSoundUseSkipListBase = 0x65d454;     // WCSkipListBase<...uint, uint...>
inline constexpr std::uint32_t kStringVector = 0x655440;             // WCValOrderedVector<String>
inline constexpr std::uint32_t kSoundEffectDictIter = 0x65d470;      // WCValSkipListDictIter<uint, CSoundEffect*>
inline constexpr std::uint32_t kUIntVector = 0x656d44;               // WCValOrderedVector<unsigned int>
inline constexpr std::uint32_t kUIntVectorBase = 0x657170;           // its base, set before the destructor
inline constexpr std::uint32_t kCLandscapeSurface = 0x65a520;   // CLandscapeSurface
// UI
inline constexpr std::uint32_t kCScreenElement = 0x655a68;  // CScreenElement
inline constexpr std::uint32_t kCStaticControl = 0x6560d8;  // CStaticControl
inline constexpr std::uint32_t kCStaticText = 0x65615c;     // CStaticText
inline constexpr std::uint32_t kCStaticImage = 0x6561e0;    // CStaticImage
inline constexpr std::uint32_t kCStaticFrame = 0x656264;    // CStaticFrame
inline constexpr std::uint32_t kCStaticColorIndexedFrame = 0x6562e8;  // CStaticColorIndexedFrame
// Controls with the virtual base CScreenElement: the class's own vtable and
// the one its constructor stores in the virtual base (game/ui/controls.cpp).
inline constexpr std::uint32_t kCControl = 0x655d28;              // CControl
inline constexpr std::uint32_t kCControl_element = 0x655ca4;      // its CScreenElement part
inline constexpr std::uint32_t kCButton = 0x655f00;               // CButton
inline constexpr std::uint32_t kCButton_element = 0x655e7c;       // its CScreenElement part
inline constexpr std::uint32_t kCImageButton = 0x655f94;          // CImageButton
inline constexpr std::uint32_t kCImageButton_element = 0x655f10;  // its CScreenElement part
inline constexpr std::uint32_t kCTextButton = 0x6560c8;           // CTextButton
inline constexpr std::uint32_t kCTextButton_element = 0x656044;   // its CScreenElement part
inline constexpr std::uint32_t kCAnimatedButton = 0x656034;          // CAnimatedButton
inline constexpr std::uint32_t kCAnimatedButton_element = 0x655fb0;  // its CScreenElement part
inline constexpr std::uint32_t kCScreenGroup = 0x655b70;           // CScreenGroup
inline constexpr std::uint32_t kCScreenGroup_element = 0x655aec;   // its CScreenElement part
// CScreenGroup's members: the children vector and the three modal stacks.
inline constexpr std::uint32_t kWCValOrderedVector_CScreenElementP = 0x655b9c;  // WCValOrderedVector<CScreenElement*>
inline constexpr std::uint32_t kWCStack_CScreenElementP = 0x655b8c;  // WCStack<CScreenElement*, WCValSList<..>>
inline constexpr std::uint32_t kWCStack_bool = 0x655b7c;             // WCStack<bool, WCValSList<bool>>
inline constexpr std::uint32_t kCGroupControl = 0x655e64;           // CGroupControl
inline constexpr std::uint32_t kCGroupControl_group = 0x655e58;     // its CScreenGroup part (+0x38)
inline constexpr std::uint32_t kCGroupControl_element = 0x655dd4;   // its CScreenElement part
inline constexpr std::uint32_t kCScreen = 0x655c80;                 // CScreen
inline constexpr std::uint32_t kCScreen_element = 0x655bfc;         // its CScreenElement part
inline constexpr std::uint32_t kWCQueue_MsgConsole = 0x655c8c;      // WCQueue<MsgConsole, WCValSList<MsgConsole>> (CScreen +0x7c)
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
inline constexpr std::uint32_t kWCValListBase_bool = 0x655bec;             // WCValListBase<bool, WCIsvSListBase<..>, ..>
inline constexpr std::uint32_t kWCValListBase_CScreenElementP = 0x655bd0;  // WCValListBase<CScreenElement*, ..>
inline constexpr std::uint32_t kWCValListBase_MsgConsole = 0x656a4c;      // WCValListBase<MsgConsole, ..> (CScreen's messages)
inline constexpr std::uint32_t kWCIsvSListBase_MsgConsole = 0x656a40;     // WCIsvSListBase<WCNIsvSLink<MsgConsole>>
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
inline constexpr std::uint32_t kWCBareVectorBase_CColumn = 0x656a20;          // WCBareVectorBase<CListView::CColumn>
inline constexpr std::uint32_t kWCBareVectorBase_CLine = 0x656a2c;            // WCBareVectorBase<CListView::CLine>
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
// wip/watcom-r6: more Watcom instantiations (watcom/*.cpp, msvc6/deleting_destructors.cpp)
inline constexpr std::uint32_t kWCBareVectorBase_CGamePlayerStatus = 0x65a194;  // <Lobby::CGamePlayerStatus>
inline constexpr std::uint32_t kWCBareVectorBase_CDisplayMode = 0x65cb30;       // <DirectX::CDisplayMode>
inline constexpr std::uint32_t kWCBareVectorBase_LobbyPlayer = 0x657070;        // <LobbyClient::CPlayer>
inline constexpr std::uint32_t kWCBareVectorBase_CLMGeneric = 0x65d5e0;         // <Lobby::CLMGeneric>
inline constexpr std::uint32_t kWCBareVectorBase_CPrivateChat = 0x65704c;       // <LobbyClient::CPrivateChat>
inline constexpr std::uint32_t kWCBareVectorBase_CLobbyServer = 0x65d2e0;       // <LobbyClient::priv::CLobbyServer>
inline constexpr std::uint32_t kWCBareVectorBase_CServerData = 0x65d2b8;        // <Lobby::CLMLobbyServerList::CServerData>
inline constexpr std::uint32_t kWCSortedVectorBase_int = 0x655a38;              // WCSortedVectorBase<int>
inline constexpr std::uint32_t kWCValOrderedVector_HFONT = 0x656a00;            // WCValOrderedVector<HFONT__*>
inline constexpr std::uint32_t kWCVectorBase_voidp = 0x6573a8;                  // WCVectorBase<void*>
inline constexpr std::uint32_t kWCOrderedVectorBase_long = 0x658538;            // WCOrderedVectorBase<long>
inline constexpr std::uint32_t kWCSortedVectorBase_uint = 0x65d1b0;             // WCSortedVectorBase<unsigned int>
inline constexpr std::uint32_t kWCVectorBase_String = 0x655a24;                 // WCVectorBase<String>
inline constexpr std::uint32_t kWCSortedVectorBase_String = 0x656d2c;           // WCSortedVectorBase<String>
inline constexpr std::uint32_t kWCSortedVectorBase_CLobbyServer = 0x65d1ec;     // <LobbyClient::priv::CLobbyServer>
inline constexpr std::uint32_t kWCVectorBase_CLobbyServer = 0x65d1d8;           // <LobbyClient::priv::CLobbyServer>
inline constexpr std::uint32_t kWCVectorBase_CServerData = 0x65d2ec;            // <Lobby::CLMLobbyServerList::CServerData>
inline constexpr std::uint32_t kWCOrderedVectorBase_CServerData = 0x65d3d0;     // <Lobby::CLMLobbyServerList::CServerData>
// WCSkipListBase<WCSkipListDictKeyVal<K, V>> and WCValSkipListDict<K, V>
inline constexpr std::uint32_t kWCSkipListBase_IntScreenElement = 0x656934;     // <int, CScreenElement*>
inline constexpr std::uint32_t kWCSkipListBase_UIntString = 0x657200;           // <unsigned int, String>
inline constexpr std::uint32_t kWCSkipListBase_UIntStringVector = 0x65721c;     // <unsigned int, WCValOrderedVector<String>>
inline constexpr std::uint32_t kWCSkipListBase_IntHICON = 0x6572d4;             // <int, HICON__*>
inline constexpr std::uint32_t kWCSkipListBase_ULongExecuteTypes = 0x6575f4;    // <unsigned long, WCPtrSortedVector<CExecuteType>>
inline constexpr std::uint32_t kWCSkipListBase_ULongSyncInfo = 0x657750;        // <unsigned long, CNetSession::CSyncInfo>
inline constexpr std::uint32_t kWCSkipListBase_IntString = 0x6580d4;            // <int, String>
inline constexpr std::uint32_t kWCSkipListBase_IntHFONT = 0x658814;             // <int, HFONT__*>
inline constexpr std::uint32_t kWCSkipListBase_UIntMessageBlock = 0x65d588;     // <unsigned int, Lobby::CLMMessageBlock>
inline constexpr std::uint32_t kWCSkipListBase_HWNDCDAudio = 0x65d968;          // <HWND__*, Win32::CCDAudio*>
inline constexpr std::uint32_t kWCValSkipListDict_UIntMessageBlock = 0x65d56c;  // WCValSkipListDict<unsigned int, Lobby::CLMMessageBlock>
// WCValListBase<T, WCIsvSListBase<WCNIsvSLink<T>>, ...> and its WCIsvSListBase
inline constexpr std::uint32_t kWCValListBase_CGameThreadP = 0x65c928;
inline constexpr std::uint32_t kWCValListBase_CSurfaceP = 0x65caf8;
inline constexpr std::uint32_t kWCValListBase_CPaletteP = 0x65cb14;
inline constexpr std::uint32_t kWCValListBase_CStatusMessage = 0x65d2d0;
inline constexpr std::uint32_t kWCIsvSListBase_CGameThreadP = 0x65c91c;
inline constexpr std::uint32_t kWCIsvSListBase_CSurfaceP = 0x65cb54;
inline constexpr std::uint32_t kWCIsvSListBase_CPaletteP = 0x65cb08;
inline constexpr std::uint32_t kWCIsvSListBase_CStatusMessage = 0x65d2c4;
// iterators
inline constexpr std::uint32_t kWCPtrSListIter_CWin32Waitable = 0x657464;       // WCPtrSListIter<Win32::CWin32Waitable>
inline constexpr std::uint32_t kWCValListIterBase_CGameThreadP = 0x65cb60;      // WCValListIterBase<CGameThread*, WCPtrSList<..>, ..>
inline constexpr std::uint32_t kWCSkipListIterBase_UIntMessageBlock = 0x65d61c;  // <unsigned int, Lobby::CLMMessageBlock>
inline constexpr std::uint32_t kWCBareVectorBase_CImageDescription = 0x65d2a0;  // <Lobby::CImageDescription>
inline constexpr std::uint32_t kWCBareVectorBase_CSocketAddress = 0x65d2ac;     // <Lobby::CSocketAddress>
inline constexpr std::uint32_t kWCBareVectorBase_CDirectDrawDevice = 0x65cb24;  // <DirectX::CDirectDrawDevice>
inline constexpr std::uint32_t kWCVectorBase_CDirectDrawDevice = 0x65cb88;      // WCVectorBase<DirectX::CDirectDrawDevice> (wip/cursor-r7)
inline constexpr std::uint32_t kWCVectorBase_CDisplayMode = 0x65cb9c;           // WCVectorBase<DirectX::CDisplayMode> (wip/cursor-r7)
inline constexpr std::uint32_t kWCValSkipListDict_UIntString = 0x65719c;        // WCValSkipListDict<unsigned int, String>
inline constexpr std::uint32_t kWCValSkipListDict_IntString = 0x6580b8;         // WCValSkipListDict<int, String>
inline constexpr std::uint32_t kWCValSkipListDict_UIntStringVector = 0x6571e4;  // <unsigned int, WCValOrderedVector<String>>
inline constexpr std::uint32_t kWCValSkipListDict_ULongExecuteTypes = 0x6575d8;  // <unsigned long, WCPtrSortedVector<CExecuteType>>
inline constexpr std::uint32_t kWCValSortedVector_uint = 0x6571b8;             // WCValSortedVector<unsigned int>
inline constexpr std::uint32_t kWCVectorBase_LobbyPlayer = 0x65d254;           // WCVectorBase<LobbyClient::CPlayer>
inline constexpr std::uint32_t kWCBareVectorBase_CLMMessageBlock = 0x65d5b8;    // <Lobby::CLMMessageBlock>
inline constexpr std::uint32_t kWCOrderedVectorBase_CLMMessageBlock = 0x65d624;  // <Lobby::CLMMessageBlock>
inline constexpr std::uint32_t kWCOrderedVectorBase_CLMGeneric = 0x65d504;      // <Lobby::CLMGeneric>
inline constexpr std::uint32_t kWCVectorBase_CLMGeneric = 0x65d5a4;             // <Lobby::CLMGeneric>
inline constexpr std::uint32_t kWCVectorBase_CSocketAddress = 0x65d28c;         // <Lobby::CSocketAddress>
inline constexpr std::uint32_t kWCVectorBase_CImageDescription = 0x65d268;      // <Lobby::CImageDescription>
inline constexpr std::uint32_t kWCBareVectorBase_LobbyPlayerIndex = 0x65d610;   // <Lobby::priv::CPlayerIndex>
inline constexpr std::uint32_t kWCBareVectorBase_NetPlayerIndex = 0x65cd0c;     // <DirectX::CGameNetSession::CPlayerIndex>
inline constexpr std::uint32_t kWCSkipNonTempBase = 0x65d638;                 // WCSkipNonTempBase (the skip lists' base)
inline constexpr std::uint32_t kWCValSkipListDict_HWNDCDAudio = 0x65d94c;      // WCValSkipListDict<HWND__*, Win32::CCDAudio*>
inline constexpr std::uint32_t kWCPtrListBase_CWin32Waitable = 0x657454;       // WCPtrListBase<Win32::CWin32Waitable, WCValSList<..*>>
inline constexpr std::uint32_t kWCValSList_CPaletteP = 0x65cb78;               // WCValSList<DirectX::CPalette*>
inline constexpr std::uint32_t kWCValSList_CSurfaceP = 0x65cb68;               // WCValSList<DirectX::CSurface*>
// session 58 (wip/framework-errors): the framework's other exceptions
inline constexpr std::uint32_t kCHResult = 0x65d8e0;      // Win32::CHResult
inline constexpr std::uint32_t kCMCIException = 0x65d908;  // Win32::CMCIException
inline constexpr std::uint32_t kCMMException = 0x65d8f4;   // Win32::CMMException
inline constexpr std::uint32_t kCTimeout = 0x65d930;       // Win32::CTimeout
// Standard library
inline constexpr std::uint32_t kStdLogicError = 0x65a548;  // std::logic_error
inline constexpr std::uint32_t kStdOutOfRange = 0x65a538;  // std::out_of_range
// ---- wip/ddraw-render: DirectDraw wrappers (Ddraw.cpp). The classes with
// a virtual CSurface base have a vbtable per part and their own vtable in
// the CSurface part.
inline constexpr std::uint32_t kCDirectDraw = 0x65cd80;             // DirectX::CDirectDraw
inline constexpr std::uint32_t kCFlipSurface_vbtable = 0x65cdc4;     // DirectX::CFlipSurface
inline constexpr std::uint32_t kCFlipSurface_base = 0x65cdb8;        // its CSurface part
inline constexpr std::uint32_t kCPrimarySurface_vbtable = 0x65cddc;  // DirectX::CPrimarySurface
inline constexpr std::uint32_t kCPrimarySurface_base = 0x65cdd0;     // its CSurface part
inline constexpr std::uint32_t kCPrimaryFlipSurface_vbtable = 0x65cdfc;       // DirectX::CPrimaryFlipSurface
inline constexpr std::uint32_t kCPrimaryFlipSurface_flipVbtable = 0x65cdf4;   // its CFlipSurface part
inline constexpr std::uint32_t kCPrimaryFlipSurface_base = 0x65cde8;          // its CSurface part
inline constexpr std::uint32_t kCOffscreenSurface = 0x65ce08;       // DirectX::COffscreenSurface
inline constexpr std::uint32_t kCOverlaySurface = 0x65ce18;         // DirectX::COverlaySurface
// CGameDrawer's list iterators (local objects of its surface / palette walks).
inline constexpr std::uint32_t kWCListIterBase = 0x65cb44;          // WCListIterBase (its constructor's)
inline constexpr std::uint32_t kSurfaceListIter = 0x65cb3c;         // WCPtrSListIter<DirectX::CSurface>
inline constexpr std::uint32_t kPaletteListIter = 0x65cb4c;         // WCPtrSListIter<DirectX::CPalette>
// wip/ui-gdi batch: the CScreenElement parts of two controls (an element's
// parent is such a part; CListBox::FillBackground asks for its type).
inline constexpr std::uint32_t kCDropdown_element = 0x656600;  // CDropdown's CScreenElement part (+0x66c)
inline constexpr std::uint32_t kCListBox_element = 0x6564a0;   // CListBox's CScreenElement part (+0x444)
// wip/images-r4 batch: PaintLib (src/s3re/thirdparty/paintlib.cpp).
inline constexpr std::uint32_t kCTextException = 0x65c0cc;
inline constexpr std::uint32_t kCPNGDecoder = 0x65c0fc;     // PaintLib CPNGDecoder (libs-r7)
inline constexpr std::uint32_t kCFileSource = 0x65c1a4;     // PaintLib CFileSource (libs-r7)
inline constexpr std::uint32_t kCAnyBmp = 0x65c0dc;
inline constexpr std::uint32_t kCPicDecoder = 0x65c110;
inline constexpr std::uint32_t kCBmp = 0x65c124;
inline constexpr std::uint32_t kCDataSource = 0x65c608;
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
// The controls' (set by the most derived constructor; game/ui/controls.cpp).
inline constexpr std::uint32_t kCControl = 0x655d2c;      // CControl
inline constexpr std::uint32_t kCButton = 0x655f04;       // CButton
inline constexpr std::uint32_t kCImageButton = 0x655f98;  // CImageButton
inline constexpr std::uint32_t kCTextButton = 0x6560cc;      // CTextButton
inline constexpr std::uint32_t kCAnimatedButton = 0x656038;  // CAnimatedButton
inline constexpr std::uint32_t kCScreenGroup = 0x655bac;     // CScreenGroup
inline constexpr std::uint32_t kCGroupControl = 0x655e70;          // CGroupControl
inline constexpr std::uint32_t kCGroupControl_group = 0x655e68;    // its CScreenGroup part (+0x3c)
inline constexpr std::uint32_t kCScreen = 0x655c98;                // CScreen
inline constexpr std::uint32_t kCDirectSoundException = 0x65ceac;   // DirectX::CDirectSoundException
inline constexpr std::uint32_t kCAppNotActiveException = 0x65cbb8;  // DirectX::CAppNotActiveException
// session 58 (wip/framework-errors)
inline constexpr std::uint32_t kCHResult = 0x65d8e8;      // Win32::CHResult
inline constexpr std::uint32_t kCMCIException = 0x65d910;  // Win32::CMCIException
inline constexpr std::uint32_t kCMMException = 0x65d8fc;   // Win32::CMMException
inline constexpr std::uint32_t kCTimeout = 0x65d938;       // Win32::CTimeout
}  // namespace vbtables

// _ThrowInfo records passed to _CxxThrowException.
namespace throw_info {
inline constexpr std::uintptr_t kCAssertmentFailed = 0x68edf0;
inline constexpr std::uintptr_t kCOutOfMemory = 0x682f58;
inline constexpr std::uintptr_t kCDataError = 0x68bfd0;
inline constexpr std::uintptr_t kDirectDrawException = 0x66a698;
inline constexpr std::uintptr_t kDirectPlayException = 0x681e60;
inline constexpr std::uintptr_t kDirectSoundException = 0x682dc8;  // DirectX::CDirectSoundException*
inline constexpr std::uintptr_t kAppNotActiveException = 0x67aee8;  // DirectX::CAppNotActiveException*
inline constexpr std::uintptr_t kCTimeoutPointer = 0x674f38;  // Win32::CTimeout* (thrown by the queue waits)
inline constexpr std::uintptr_t kCFileNotFoundPointer = 0x67b828;  // Win32::CFileNotFound* (CFile::Open)
// session 58 (wip/framework-errors)
inline constexpr std::uintptr_t kCHResultPointer = 0x68ea98;       // Win32::CHResult* (ThrowWin32Error, ThrowHResult)
inline constexpr std::uintptr_t kCMCIExceptionPointer = 0x68ec98;  // Win32::CMCIException* (ThrowMCIError)
inline constexpr std::uintptr_t kCMMExceptionPointer = 0x682f68;   // Win32::CMMException* (ThrowMMError)
}  // namespace throw_info

// Type descriptors (catch types).
namespace type_descriptor {
inline constexpr std::uintptr_t kCTimeoutPointer = 0x698e98;  // ".PAVCTimeout@Win32@@" (ExecuteCommands' catch)
inline constexpr std::uintptr_t kCAppNotActiveExceptionPointer = 0x698dd0;  // ".PAVCAppNotActiveException@DirectX@@" (CGameDrawThread::Step's catch)
inline constexpr std::uintptr_t kCHResultPointer = 0x6968d8;  // ".PAVCHResult@Win32@@" (CMapFileStream's catches)
inline constexpr std::uintptr_t kCFileNotFoundPointer = 0x69f908;  // ".PAVCFileNotFound@Win32@@" (CMapFileStream::Open)
// ".PAVCDirectSoundException@DirectX@@" (CSoundData::Open's and Restore's catches)
inline constexpr std::uintptr_t kCDirectSoundExceptionPointer = 0x697510;
// wip/ui-gdi batch: ".?AVCDropdown@@" (CListBox::FillBackground 0x420340 asks typeid(*parent) == it)
inline constexpr std::uintptr_t kCDropdown = 0x6985a8;
// wip/dialog-r8 batch: dynamic_cast operands of the CListView cell getters and CScreenGroup::PushModal.
inline constexpr std::uintptr_t kCScreenElement = 0x6976a0;  // ".?AVCScreenElement@@"
inline constexpr std::uintptr_t kCEditControl = 0x697ce0;    // ".?AVCEditControl@@"
inline constexpr std::uintptr_t kCStaticText = 0x697fa8;     // ".?AVCStaticText@@"
inline constexpr std::uintptr_t kCImageButton = 0x697e60;    // ".?AVCImageButton@@"
inline constexpr std::uintptr_t kCStaticFrame = 0x698230;    // ".?AVCStaticFrame@@"
inline constexpr std::uintptr_t kCUpDown = 0x698750;         // ".?AVCUpDown@@"
inline constexpr std::uintptr_t kCScreenGroup = 0x697960;    // ".?AVCScreenGroup@@"
inline constexpr std::uintptr_t kCControl = 0x697cb8;        // ".?AVCControl@@"
inline constexpr std::uintptr_t kCProgressBar = 0x6987f0;    // ".?AVCProgressBar@@"
}  // namespace type_descriptor

}  // namespace s3re::game
