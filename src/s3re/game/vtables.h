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
// DirectX::*
inline constexpr std::uint32_t kCSimpleSurface = 0x65cda0;      // DirectX::CSimpleSurface
inline constexpr std::uint32_t kCSurface = 0x65cda8;            // DirectX::CSurface
inline constexpr std::uint32_t kCDirectSoundBuffer = 0x65cecc;  // DirectX::CDirectSoundBuffer
inline constexpr std::uint32_t kCLandscapeSurface = 0x65a520;   // CLandscapeSurface
// UI
inline constexpr std::uint32_t kCScreenElement = 0x655a68;  // CScreenElement
inline constexpr std::uint32_t kCStaticControl = 0x6560d8;  // CStaticControl
inline constexpr std::uint32_t kCStaticText = 0x65615c;     // CStaticText
// Lobby::*
inline constexpr std::uint32_t kCMemoryBlock = 0x65d4f4;    // Lobby::CMemoryBlock
inline constexpr std::uint32_t kCLMMessageBlock = 0x65d4fc;  // Lobby::CLMMessageBlock
inline constexpr std::uint32_t kCLMGeneric = 0x65d52c;      // Lobby::CLMGeneric
inline constexpr std::uint32_t kCPlayerIndex = 0x65d5ec;    // Lobby::priv::CPlayerIndex
inline constexpr std::uint32_t kCDataError = 0x65d4b8;      // Lobby::priv::CDataError
// Computer players (Rainer\*)
inline constexpr std::uint32_t kCComputerPlayer = 0x65a530;  // CComputerPlayer
inline constexpr std::uint32_t kBlackBoardNote = 0x65a56c;    // BLACK_BOARD_NOTE
inline constexpr std::uint32_t kDoBuildHouseNote = 0x65a584;  // T_DO_BUILD_HOUSE_NOTE
// Watcom container library
inline constexpr std::uint32_t kWCExcept = 0x655438;                    // WCExcept (base of the containers)
inline constexpr std::uint32_t kWCIsvListBase = 0x655fa4;               // WCIsvListBase
inline constexpr std::uint32_t kWCBareVectorBase_int = 0x655a5c;        // WCBareVectorBase<int>
inline constexpr std::uint32_t kWCVectorBase_int = 0x6559f8;            // WCVectorBase<int>
inline constexpr std::uint32_t kWCBareVectorBase_uint = 0x657158;       // WCBareVectorBase<unsigned int>
inline constexpr std::uint32_t kWCBareVectorBase_ushort = 0x657404;     // WCBareVectorBase<unsigned short>
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
}  // namespace throw_info

// Type descriptors (catch types).
namespace type_descriptor {
inline constexpr std::uintptr_t kCTimeoutPointer = 0x698e98;  // ".PAVCTimeout@Win32@@" (ExecuteCommands' catch)
inline constexpr std::uintptr_t kCHResultPointer = 0x6968d8;  // ".PAVCHResult@Win32@@" (CMapFileStream's catches)
inline constexpr std::uintptr_t kCFileNotFoundPointer = 0x69f908;  // ".PAVCFileNotFound@Win32@@" (CMapFileStream::Open)
}  // namespace type_descriptor

}  // namespace s3re::game
