#pragma once

// Layouts of the small framework, DirectX, lobby and graphics objects whose
// fields our replacements touch (Win32\*, DirectX\*, Dirk\protocol.cpp,
// Dirk\graphicsdata.cpp...). Only the fields used so far are named;
// "unkXX" means unknown (XX = offset). Every struct is packed and checked
// with static_asserts, like the world layout in layout.h. Total sizes are
// asserted only where they are known (from operator new sizes or RTTI
// evidence); the other structs end after the last known field.
//
// The DLL is 32-bit like S3.EXE, so pointer fields are 4 bytes.

#include "msvc6/string.h"
#include "watcom/ptr_vector.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::framework {

static_assert(sizeof(void*) == 4, "the layouts below assume a 32-bit build");

#pragma pack(push, 1)

// ---------------------------------------------------------------------------
// Win32 framework (Win32\Classes.cpp and neighbours)
// ---------------------------------------------------------------------------

// Win32::CObject: the root class (vtable only).
struct CObject {
    std::uint32_t vtable;  // +0x00
};
static_assert(sizeof(CObject) == 4);

// Win32::CWin32Handle: a Windows handle and whether the object owns it.
struct CWin32Handle {
    std::uint32_t vtable;  // +0x00
    std::uint32_t handle;  // +0x04
    std::uint32_t owned;   // +0x08 1: closed by Close (probably "close on destruction")
};
static_assert(sizeof(CWin32Handle) == 0xc);

// Win32::CFile (vtable 0x65d9b8, 0xc bytes, Win32\Mapping.cpp): a
// CWin32Handle whose handle is a file (CreateFileA). Members:
// win32_mapping.cpp.

// WCValOrderedVector<void*> as Win32::CFileMapping embeds it (0x1c bytes;
// vtable 0x65d9d0, slot 3 = WCOrderedVectorBase::base_find_from 0x499da0):
// the watcom::PtrVector layout plus the growth step. The constructor
// leaves unk04 = 0x7f (probably the Watcom container's exception-state
// flags; never read by the mapping code).
struct WCPtrOrderedVector {
    std::uint32_t vtable;    // +0x00
    std::uint32_t unk04;     // +0x04 0x7f after the constructor
    std::uint32_t data;      // +0x08 the entries (game operator new)
    std::uint32_t capacity;  // +0x0c entries allocated (10 at first)
    std::int32_t filled;     // +0x10 entries that hold a value
    std::int32_t length;     // +0x14 entries in use
    std::uint32_t grow;      // +0x18 entries added when full (5)
};
static_assert(offsetof(WCPtrOrderedVector, data) == 0x08);
static_assert(offsetof(WCPtrOrderedVector, length) == 0x14);
static_assert(sizeof(WCPtrOrderedVector) == 0x1c);

// Win32::CFileMapping (vtable 0x65d9c4, 0x2c bytes from CGraphicsData's
// operator new): a CWin32Handle holding the mapping object, the CFile it
// maps (owned: Close deletes it) and the list of views mapped from it.
struct CFileMapping {
    CWin32Handle handle;        // +0x00 (vtable, mapping handle, owned)
    std::uint32_t file;         // +0x0c Win32::CFile* or 0 (a mapping of the paging file)
    WCPtrOrderedVector views;   // +0x10 addresses returned by MapView
};
static_assert(offsetof(CFileMapping, file) == 0x0c);
static_assert(offsetof(CFileMapping, views) == 0x10);
static_assert(sizeof(CFileMapping) == 0x2c);

// Win32::CWaitable (also the layout of Win32::CEvent): a CWin32Handle
// sub-object at +4.
struct CWaitable {
    std::uint32_t vtable;  // +0x00
    CWin32Handle handle;   // +0x04 (its vtable is the second vtable of the object)
    std::uint32_t unk10;   // +0x10
};
static_assert(offsetof(CWaitable, handle) == 0x04);
static_assert(offsetof(CWaitable, unk10) == 0x10);

// Win32::CThread: a CWaitable plus the thread's start parameters.
struct CThread {
    CWaitable waitable;    // +0x00
    std::uint32_t unk14;   // +0x14
    std::uint32_t unk18;   // +0x18 1 after construction
    std::uint32_t unk1c;   // +0x1c first constructor argument
    std::uint32_t unk20;   // +0x20 second constructor argument
    std::uint32_t unk24;   // +0x24
};
static_assert(offsetof(CThread, unk14) == 0x14);
static_assert(offsetof(CThread, unk1c) == 0x1c);
static_assert(offsetof(CThread, unk24) == 0x24);

// Win32::CWindowBase.
struct CWindowBase {
    std::uint32_t vtable;  // +0x00
    std::uint32_t window;  // +0x04 the window handle
};

// Win32::CMessageQueue (a thread's Windows message queue wrapper; not the
// logic/net MessageQueue of logic_thread_layout.h).
struct CMessageQueue {
    std::uint32_t vtable;  // +0x00
    std::uint32_t window;  // +0x04 the HWND filter passed to GetMessageA (0 = all windows)
};

// Win32::CRegistryKey.
struct CRegistryKey {
    std::uint32_t vtable;  // +0x00
    std::uint32_t key;     // +0x04 probably the HKEY (0: none open)
};

// Win32::CRegistryValueInfo: a registry value's name and a number.
struct CRegistryValueInfo {
    std::uint32_t vtable;  // +0x00
    msvc6::String name;    // +0x04
    std::uint32_t value;   // +0x14 probably the value's type or size
};
static_assert(offsetof(CRegistryValueInfo, name) == 0x04);
static_assert(offsetof(CRegistryValueInfo, value) == 0x14);

// Win32::CException (0x18 bytes) and its derived classes CRunTimeError,
// COutOfMemory, CProgrammingError and Lobby::priv::CDataError (same size):
// classes with virtual bases, so +4 is the virtual-base pointer.
struct CException {
    std::uint32_t vtable;   // +0x00
    std::uint32_t vbase;    // +0x04 virtual-base pointer (set by the most derived constructor)
    msvc6::String message;  // +0x08
};
static_assert(sizeof(CException) == 0x18);
static_assert(offsetof(CException, message) == 0x08);

// Win32::CAssertmentFailed (0x24 bytes): a CProgrammingError with the
// failed assertion.
struct CAssertmentFailed {
    CException base;         // +0x00
    const char* expression;  // +0x18 C text
    const char* file;        // +0x1c C text
    std::uint32_t line;      // +0x20
};
static_assert(sizeof(CAssertmentFailed) == 0x24);
static_assert(offsetof(CAssertmentFailed, expression) == 0x18);

// ---------------------------------------------------------------------------
// DirectX wrappers (DirectX\*.cpp)
// ---------------------------------------------------------------------------

// Any COM object (e.g. an IDirectDrawSurface) as raw memory: its first
// dword points at its method table. Used to build fake interfaces in tests.
struct ComObject {
    void* const* vtable;  // +0x00
};
static_assert(sizeof(ComObject) == 4);

// DirectX::CSimpleSurface.
struct CSimpleSurface {
    std::uint32_t vtable;    // +0x00
    void* createdInterface;  // +0x04 the interface the surface was created with (released by Release)
    void* surface;           // +0x08 the IDirectDrawSurface DDBlt uses (probably a later version)
    void* owner;             // +0x0c probably the DirectDraw owner (its slot 0x0c restores lost surfaces)
    std::uint32_t unk10;     // +0x10
};
static_assert(sizeof(CSimpleSurface) == 0x14);
static_assert(offsetof(CSimpleSurface, owner) == 0x0c);

// DirectX::CSurface, derived from CSimpleSurface.
struct CSurface {
    CSimpleSurface simple;  // +0x00
    std::uint32_t unk14;    // +0x14
    std::uint32_t unk18;    // +0x18
    std::uint8_t unk1c[0x48 - 0x1c];
    std::uint32_t unk48;    // +0x48
};
static_assert(offsetof(CSurface, unk14) == 0x14);
static_assert(offsetof(CSurface, unk18) == 0x18);
static_assert(offsetof(CSurface, unk48) == 0x48);

// CLandscapeSurface (the map's terrain surface, Volker\landscape.cpp), derived
// from DirectX::COffscreenSurface -> CSurface; 0xa8 bytes (operator new at
// 0x42efb0). Constructor 0x54bd30: +0x90 = its argument (CDrawer's
// CGraphicsData), the four tables allocated (sizes below).
struct CLandscapeSurface {
    CSurface surface;  // +0x00
    std::uint8_t unk4c[0x90 - 0x4c];
    void* graphics;    // +0x90 the CGraphicsData (game/graphics/gfx_layout.h)
    void* table94;     // +0x94 new(0x364) (freed by the destructor)
    void* unk98;       // +0x98 (not freed by the destructor)
    void* table9c;     // +0x9c new(1,000,000)
    void* colours;     // +0xa0 new(0x202): uint16 colour slots 0..256 (Landscape_FindOrAddColour)
    void* tableA4;     // +0xa4 new(0x10000)
};
static_assert(offsetof(CLandscapeSurface, graphics) == 0x90);
static_assert(offsetof(CLandscapeSurface, table94) == 0x94);
static_assert(offsetof(CLandscapeSurface, table9c) == 0x9c);
static_assert(offsetof(CLandscapeSurface, colours) == 0xa0);
static_assert(offsetof(CLandscapeSurface, tableA4) == 0xa4);
static_assert(sizeof(CLandscapeSurface) == 0xa8);

// DirectX::CDirectSound (vtable 0x65ceb8): the
// DirectSound object. Its constructor registers it in g_directSound
// (0x7e8c0c), which the buffers use to create themselves.
struct CDirectSound {
    std::uint32_t vtable;  // +0x00
    void* sound;           // +0x04 IDirectSound* (0 until Connect)
};
static_assert(sizeof(CDirectSound) == 8);

// DirectX::CDirectSoundBuffer (vtable 0x65cecc) and its subclasses
// CPrimaryBuffer (0x65cee0), CSecondaryBuffer (0x65cef4) and
// CSoundEffectBuffer (0x65cf38, + the owning effect at +0x0c).
struct CDirectSoundBuffer {
    std::uint32_t vtable;       // +0x00
    void* buffer;               // +0x04 IDirectSoundBuffer* (0 until created)
    std::uint32_t createFlags;  // +0x08 DSBCAPS flags or'ed into the buffer description
};
static_assert(sizeof(CDirectSoundBuffer) == 12);

struct CSoundEffectBuffer {
    CDirectSoundBuffer base;  // +0x00
    void* effect;             // +0x0c the DirectX::CSoundEffect this buffer plays
};
static_assert(sizeof(CSoundEffectBuffer) == 0x10);

// DirectX::CPlayer (a network player record).
struct CPlayer {
    std::uint32_t vtable;   // +0x00
    std::uint32_t unk04;    // +0x04 probably an id
    std::uint32_t unk08;    // +0x08 probably an id
    msvc6::String name0c;   // +0x0c probably the player name
    msvc6::String name1c;   // +0x1c probably the computer name
};
static_assert(offsetof(CPlayer, name0c) == 0x0c);
static_assert(offsetof(CPlayer, name1c) == 0x1c);

// Network session description (DirectX\netsession.cpp; constructors
// 0x5bb040 and 0x5bb0b0): a copy of DirectPlay's DPSESSIONDESC2 (80 bytes,
// copied as dwords) and the session name as a string.
struct SessionDesc {
    std::uint32_t desc[0x0c];   // +0x00 DPSESSIONDESC2 up to the name (dwSize first)
    const char* sessionName;    // +0x30 lpszSessionNameA
    std::uint32_t descTail[7];  // +0x34 the rest of the DPSESSIONDESC2
    msvc6::String name;         // +0x50
};
static_assert(offsetof(SessionDesc, sessionName) == 0x30);
static_assert(offsetof(SessionDesc, name) == 0x50);

// ---------------------------------------------------------------------------
// Lobby protocol (Dirk\protocol.cpp)
// ---------------------------------------------------------------------------

// Lobby::CMemoryBlock (0x10 bytes): a growable byte buffer.
struct CMemoryBlock {
    std::uint32_t vtable;    // +0x00
    std::uint8_t* data;      // +0x04 (operator new)
    std::uint32_t reserved;  // +0x08 the size last reserved (not rounded)
    std::uint32_t used;      // +0x0c
};
static_assert(sizeof(CMemoryBlock) == 0x10);

// Lobby::CLMGeneric: a protocol message (its bytes, the first dword is the
// header).
struct CLMGeneric {
    std::uint32_t vtable;  // +0x00
    CMemoryBlock block;    // +0x04
};
static_assert(sizeof(CLMGeneric) == 0x14);

// Lobby::CLMMessageBlock: a protocol message block with four more fields.
struct CLMMessageBlock {
    std::uint32_t vtable;      // +0x00
    CMemoryBlock block;        // +0x04
    std::uint32_t fields[4];   // +0x14..+0x20 copied by the copy constructor
};
static_assert(offsetof(CLMMessageBlock, fields) == 0x14);

// Lobby::priv::CPlayerIndex.
struct CPlayerIndex {
    std::uint32_t vtable;  // +0x00
    std::uint32_t index;   // +0x04
    std::uint32_t unk08;   // +0x08
};

// An unidentified lobby class (Dirk\lobbyclient.cpp, Dirk\protocol.cpp)
// with an id at +4: Lobby_EqualField4 (0x606ac0) compares two ids,
// Lobby_HasField4 (0x5da3a0) tests for one, and its assignment 0x5db590
// copies the id, two strings, a byte +0x28, a dword +0x2c and a container
// at +0x9c (0x451120 calls all three on the same object).
struct LobbyIdObject {
    std::uint32_t unk00;   // +0x00 probably the vtable
    std::uint32_t id;      // +0x04
    msvc6::String unk08;   // +0x08
    msvc6::String unk18;   // +0x18
};
static_assert(offsetof(LobbyIdObject, id) == 0x04);
static_assert(offsetof(LobbyIdObject, unk18) == 0x18);

// ---------------------------------------------------------------------------
// Strings of 16-bit characters (the UI's wide string; same shape as
// msvc6::String)
// ---------------------------------------------------------------------------

struct WideString {
    std::uint8_t allocator;  // +0x00
    std::uint8_t pad01[3];
    std::uint16_t* ptr;      // +0x04 characters; the byte before them is a reference count
    std::uint32_t len;       // +0x08
    std::uint32_t res;       // +0x0c
};
static_assert(sizeof(WideString) == 0x10);
static_assert(offsetof(WideString, ptr) == 0x04);

#pragma pack(pop)

}  // namespace s3re::game::framework
