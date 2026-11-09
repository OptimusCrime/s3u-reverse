// Small constructors, initializers and clean-up members of the framework,
// network and lobby classes (batch 26). All are thiscall (ECX = the object)
// unless noted; names are ours where the RTTI has none.
//
// Net_DeletePointee_49d980 (0x0049d980): () operator delete(*(this+0)).
//   EAX = whatever delete leaves (callers don't use it). Dirk\nethelp.cpp.
// NetSession_SetPair_5bd200 (0x005bd200): (a, b) +0 = a, +4 = b; EAX =
//   this. ret 8. DirectX\netsession.cpp (probably a pair/key constructor).
// Lobby_ZeroThreeDwords_5dc810 (0x005dc810): () +0, +4, +8 = 0; EAX =
//   this. Dirk\lobbyclient.cpp (probably an empty vector/list header).
// Lobby::priv::CPlayerIndex::CPlayerIndex (0x006065f0): (a) +4 = a, +8 = 0,
//   vtable 0x65d5ec; EAX = this. ret 4.
// Lobby::CMemoryBlock::CMemoryBlock (0x00600e00): () +4, +8, +0xc = 0,
//   vtable 0x65d4f4; EAX = this.
// Lobby_ZeroSixDwords_5ec1e0 (0x005ec1e0): () +0..+0x14 = 0; EAX = this.
// Credits_TextStyle_5a0960 (0x005a0960): () +0x10, +0x14, +4 = 0, +0 =
//   0x69fff0 (an address in the data section, probably a default font or
//   palette), +8 = 0xc, +0xc = 0xff; EAX = this. Credits\cpp_exo_text.cpp.
// Win32::CFile::CFile (0x006195c0): () CWin32Handle(0, 0), vtable
//   0x65d9b8; EAX = this.
// NetSession_Init_5bb040 (0x005bb040): () byte +0x50 = the top byte of
//   ECX (the original stores an uninitialized local that shares its stack
//   slot with the saved ECX: an empty allocator object), +0x54, +0x58,
//   +0x5c = 0, +0 = 0; EAX = this.
// Lobby_Init_5ebfb0 (0x005ebfb0): () +0, +4 = 0, byte +8 = the top byte of
//   ECX (same allocator leftover), +0xc, +0x10, +0x14 = 0; EAX = this.
// Lobby_FreeBlock_601250 (0x00601250): () operator delete(+4), then +4,
//   +0xc, +8 = 0; EAX = 0. Dirk\protocol.cpp.
// DirectX::CPlayer::vf01 (0x005c0ef0), our DirectX_CPlayer_Clear: () +4,
//   +8 = 0; the strings at +0x1c and +0xc are assigned "" (0x7a7868) with
//   0x413d70; EAX = the last assign's result (this + 0xc).
// Win32::CWin32Handle::vf01 (0x0060bf20), our Win32_CWin32Handle_Close: ()
//   if the handle +4 is not 0 and +8 == 1 (the object owns it), CloseHandle
//   (import 0x655180); then +4 = +8 = 0. EAX = CloseHandle's result if it
//   was called, else the handle that was read.
// Win32::CThread::CThread (0x0060df70): (a, b) CWaitable(0, 0), +0x14 = 0,
//   +0x1c = a, +0x18 = 1, +0x20 = b, +0x24 = 0, vtables +0 = 0x65d710 and
//   +4 = 0x65d704; EAX = this. ret 8.

#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CWin32Handle_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);
void* __fastcall Win32_CWaitable_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);

namespace {

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

void GameDelete(void* block) {
    reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(block);
}

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

constexpr std::uint32_t kCPlayerIndexVtable = 0x0065d5ec;
constexpr std::uint32_t kCMemoryBlockVtable = 0x0065d4f4;
constexpr std::uint32_t kCreditsDefault = 0x0069fff0;
constexpr std::uint32_t kCFileVtable = 0x0065d9b8;
constexpr std::uint32_t kCThreadVtable = 0x0065d710;
constexpr std::uint32_t kCThreadWaitableVtable = 0x0065d704;
constexpr std::uintptr_t kEmptyText = 0x007a7868;
constexpr std::uintptr_t kCloseHandleImport = 0x00655180;

std::uint8_t TopByte(const void* self) {
    return static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(self) >> 24);
}

}  // namespace

using ObjFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Obj1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Obj2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);

std::uint32_t __fastcall Net_DeletePointee_49d980(void* self, void* edx);
std::uint32_t __fastcall NetSession_SetPair_5bd200(void* self, void* edx, std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall Lobby_ZeroThreeDwords_5dc810(void* self, void* edx);
std::uint32_t __fastcall Lobby_CPlayerIndex_Construct(void* self, void* edx, std::uint32_t a);
std::uint32_t __fastcall Lobby_CMemoryBlock_Construct(void* self, void* edx);
std::uint32_t __fastcall Lobby_ZeroSixDwords_5ec1e0(void* self, void* edx);
std::uint32_t __fastcall Credits_TextStyle_5a0960(void* self, void* edx);
std::uint32_t __fastcall Win32_CFile_Construct(void* self, void* edx);
std::uint32_t __fastcall NetSession_Init_5bb040(void* self, void* edx);
std::uint32_t __fastcall Lobby_Init_5ebfb0(void* self, void* edx);
std::uint32_t __fastcall Lobby_FreeBlock_601250(void* self, void* edx);
std::uint32_t __fastcall DirectX_CPlayer_Clear(void* self, void* edx);
std::uint32_t __fastcall Win32_CWin32Handle_Close(void* self, void* edx);
std::uint32_t __fastcall Win32_CThread_Construct(void* self, void* edx, std::uint32_t a, std::uint32_t b);

S3RE_HOOK(0x0049d980, Net_DeletePointee_49d980, ObjFn, Original_Net_DeletePointee_49d980, Net_DeletePointee_49d980);

// Purpose: Frees the block an object points to with its first field
// (network helper code; probably the destructor of an owning pointer).
std::uint32_t __fastcall Net_DeletePointee_49d980(void* self, void* /*edx*/) {
    GameDelete(At<void*>(self, 0));
    return 0;
}

S3RE_HOOK(0x005bd200, NetSession_SetPair_5bd200, Obj2Fn, Original_NetSession_SetPair_5bd200,
          NetSession_SetPair_5bd200);

// Purpose: Stores two values in a small network-session record (probably
// the constructor of a pair such as player id and index).
std::uint32_t __fastcall NetSession_SetPair_5bd200(void* self, void* /*edx*/, std::uint32_t a, std::uint32_t b) {
    At<std::uint32_t>(self, 0) = a;
    At<std::uint32_t>(self, 4) = b;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005dc810, Lobby_ZeroThreeDwords_5dc810, ObjFn, Original_Lobby_ZeroThreeDwords_5dc810,
          Lobby_ZeroThreeDwords_5dc810);

// Purpose: Empties a three-field lobby record (probably the constructor of
// an empty vector or list header).
std::uint32_t __fastcall Lobby_ZeroThreeDwords_5dc810(void* self, void* /*edx*/) {
    for (std::size_t offset = 0; offset < 0xc; offset += 4) {
        At<std::uint32_t>(self, offset) = 0;
    }
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x006065f0, Lobby_CPlayerIndex_Construct, Obj1Fn, Original_Lobby_CPlayerIndex_Construct,
          Lobby_CPlayerIndex_Construct);

// Purpose: Constructor of the online lobby's player index (a player number
// with one more field).
std::uint32_t __fastcall Lobby_CPlayerIndex_Construct(void* self, void* /*edx*/, std::uint32_t a) {
    At<std::uint32_t>(self, 4) = a;
    At<std::uint32_t>(self, 8) = 0;
    At<std::uint32_t>(self, 0) = kCPlayerIndexVtable;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00600e00, Lobby_CMemoryBlock_Construct, ObjFn, Original_Lobby_CMemoryBlock_Construct,
          Lobby_CMemoryBlock_Construct);

// Purpose: Constructor of an empty memory block of the online lobby code.
std::uint32_t __fastcall Lobby_CMemoryBlock_Construct(void* self, void* /*edx*/) {
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint32_t>(self, 8) = 0;
    At<std::uint32_t>(self, 0xc) = 0;
    At<std::uint32_t>(self, 0) = kCMemoryBlockVtable;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005ec1e0, Lobby_ZeroSixDwords_5ec1e0, ObjFn, Original_Lobby_ZeroSixDwords_5ec1e0,
          Lobby_ZeroSixDwords_5ec1e0);

// Purpose: Empties a six-field lobby record (probably a constructor).
std::uint32_t __fastcall Lobby_ZeroSixDwords_5ec1e0(void* self, void* /*edx*/) {
    for (std::size_t offset = 0; offset < 0x18; offset += 4) {
        At<std::uint32_t>(self, offset) = 0;
    }
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005a0960, Credits_TextStyle_5a0960, ObjFn, Original_Credits_TextStyle_5a0960, Credits_TextStyle_5a0960);

// Purpose: Sets up a text record of the credits screen with its defaults
// (probably font, size 12 and colour 255).
std::uint32_t __fastcall Credits_TextStyle_5a0960(void* self, void* /*edx*/) {
    At<std::uint32_t>(self, 0x10) = 0;
    At<std::uint32_t>(self, 0x14) = 0;
    At<std::uint32_t>(self, 0) = kCreditsDefault;
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint32_t>(self, 8) = 0xc;
    At<std::uint32_t>(self, 0xc) = 0xff;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x006195c0, Win32_CFile_Construct, ObjFn, Original_Win32_CFile_Construct, Win32_CFile_Construct);

// Purpose: Constructor of the framework's file object (a Windows handle
// wrapper without a file yet).
std::uint32_t __fastcall Win32_CFile_Construct(void* self, void* /*edx*/) {
    Win32_CWin32Handle_Construct(self, nullptr, 0, 0);
    At<std::uint32_t>(self, 0) = kCFileVtable;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005bb040, NetSession_Init_5bb040, ObjFn, Original_NetSession_Init_5bb040, NetSession_Init_5bb040);

// Purpose: Empties a network-session record (probably the constructor of a
// container with an allocator byte at +0x50).
std::uint32_t __fastcall NetSession_Init_5bb040(void* self, void* /*edx*/) {
    At<std::uint8_t>(self, 0x50) = TopByte(self);
    At<std::uint32_t>(self, 0x54) = 0;
    At<std::uint32_t>(self, 0x58) = 0;
    At<std::uint32_t>(self, 0x5c) = 0;
    At<std::uint32_t>(self, 0) = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005ebfb0, Lobby_Init_5ebfb0, ObjFn, Original_Lobby_Init_5ebfb0, Lobby_Init_5ebfb0);

// Purpose: Empties a lobby record (probably the constructor of a record
// holding a container with an allocator byte at +8).
std::uint32_t __fastcall Lobby_Init_5ebfb0(void* self, void* /*edx*/) {
    At<std::uint32_t>(self, 0) = 0;
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint8_t>(self, 8) = TopByte(self);
    At<std::uint32_t>(self, 0xc) = 0;
    At<std::uint32_t>(self, 0x10) = 0;
    At<std::uint32_t>(self, 0x14) = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00601250, Lobby_FreeBlock_601250, ObjFn, Original_Lobby_FreeBlock_601250, Lobby_FreeBlock_601250);

// Purpose: Frees the buffer of a lobby memory block and empties it.
std::uint32_t __fastcall Lobby_FreeBlock_601250(void* self, void* /*edx*/) {
    GameDelete(At<void*>(self, 4));
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint32_t>(self, 0xc) = 0;
    At<std::uint32_t>(self, 8) = 0;
    return 0;
}

S3RE_HOOK(0x005c0ef0, DirectX_CPlayer_Clear, ObjFn, Original_DirectX_CPlayer_Clear, DirectX_CPlayer_Clear);

// Purpose: Resets a network player record: two ids to 0 and both names
// (probably player and computer name) to the empty string.
std::uint32_t __fastcall DirectX_CPlayer_Clear(void* self, void* /*edx*/) {
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint32_t>(self, 8) = 0;
    const char* empty = reinterpret_cast<const char*>(kEmptyText);
    msvc6::String_AssignTextOrEmpty(&At<msvc6::String>(self, 0x1c), nullptr, empty);
    return reinterpret_cast<std::uint32_t>(
        msvc6::String_AssignTextOrEmpty(&At<msvc6::String>(self, 0xc), nullptr, empty));
}

S3RE_HOOK(0x0060bf20, Win32_CWin32Handle_Close, ObjFn, Original_Win32_CWin32Handle_Close, Win32_CWin32Handle_Close);

// Purpose: Closes the Windows handle a framework object owns (events,
// threads, files) and forgets it.
std::uint32_t __fastcall Win32_CWin32Handle_Close(void* self, void* /*edx*/) {
    std::uint32_t result = At<std::uint32_t>(self, 4);
    if (result != 0 && At<std::uint32_t>(self, 8) == 1) {
        using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
        result = static_cast<std::uint32_t>(
            (*reinterpret_cast<CloseHandleFn*>(kCloseHandleImport))(reinterpret_cast<HANDLE>(result)));
    }
    At<std::uint32_t>(self, 4) = 0;
    At<std::uint32_t>(self, 8) = 0;
    return result;
}

S3RE_HOOK(0x0060df70, Win32_CThread_Construct, Obj2Fn, Original_Win32_CThread_Construct, Win32_CThread_Construct);

// Purpose: Constructor of the framework's thread class (the base of the
// game's logic, draw and network threads); the thread itself is started
// later.
std::uint32_t __fastcall Win32_CThread_Construct(void* self, void* /*edx*/, std::uint32_t a, std::uint32_t b) {
    Win32_CWaitable_Construct(self, nullptr, 0, 0);
    At<std::uint32_t>(self, 0x14) = 0;
    At<std::uint32_t>(self, 0x1c) = a;
    At<std::uint32_t>(self, 0x18) = 1;
    At<std::uint32_t>(self, 0x20) = b;
    At<std::uint32_t>(self, 0x24) = 0;
    At<std::uint32_t>(self, 0) = kCThreadVtable;
    At<std::uint32_t>(self, 4) = kCThreadWaitableVtable;
    return reinterpret_cast<std::uint32_t>(self);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each case: two identical random buffers with the object in the middle; the
// original runs on one, ours on the other; EAX (relative to the object when
// it points into the buffer) and every byte must match. The functions that
// free memory get fresh blocks from the game's heap (both sides). The CPlayer
// strings are set up as the empty strings or fresh heap strings.

namespace {

struct FrameworkFunctions {
    ObjFn deletePointee;
    Obj2Fn setPair;
    ObjFn zero3;
    Obj1Fn playerIndex;
    ObjFn memoryBlock;
    ObjFn zero6;
    ObjFn credits;
    ObjFn file;
    ObjFn netInit;
    ObjFn lobbyInit;
    ObjFn freeBlock;
    ObjFn playerClear;
    ObjFn close;
    Obj2Fn thread;
};

FrameworkFunctions OriginalFramework() {
    return {Original_Net_DeletePointee_49d980, Original_NetSession_SetPair_5bd200,
            Original_Lobby_ZeroThreeDwords_5dc810, Original_Lobby_CPlayerIndex_Construct,
            Original_Lobby_CMemoryBlock_Construct, Original_Lobby_ZeroSixDwords_5ec1e0,
            Original_Credits_TextStyle_5a0960, Original_Win32_CFile_Construct,
            Original_NetSession_Init_5bb040, Original_Lobby_Init_5ebfb0,
            Original_Lobby_FreeBlock_601250, Original_DirectX_CPlayer_Clear,
            Original_Win32_CWin32Handle_Close, Original_Win32_CThread_Construct};
}

FrameworkFunctions OurFramework() {
    return {&Net_DeletePointee_49d980, &NetSession_SetPair_5bd200, &Lobby_ZeroThreeDwords_5dc810,
            &Lobby_CPlayerIndex_Construct, &Lobby_CMemoryBlock_Construct, &Lobby_ZeroSixDwords_5ec1e0,
            &Credits_TextStyle_5a0960, &Win32_CFile_Construct, &NetSession_Init_5bb040, &Lobby_Init_5ebfb0,
            &Lobby_FreeBlock_601250, &DirectX_CPlayer_Clear, &Win32_CWin32Handle_Close,
            &Win32_CThread_Construct};
}

constexpr int kOps = 14;
constexpr std::size_t kGuard = 16;
constexpr std::size_t kObject = 0x60;

// Prepares op-specific object contents (identical for both sides apart
// from the heap blocks, which are fresh per side).
void Prepare(int op, std::uint8_t* object, test::Rng& rng, std::uint32_t mode) {
    switch (op) {
        case 0:
            At<void*>(object, 0) = (mode & 1) != 0 ? nullptr : GameNew(8 + (mode & 0x30));
            break;
        case 10:
            At<void*>(object, 4) = (mode & 1) != 0 ? nullptr : GameNew(8 + (mode & 0x30));
            break;
        case 11:
            // Both strings: empty (null), or a fresh unshared buffer with text.
            for (const std::size_t offset : {0x0cu, 0x1cu}) {
                auto& s = At<msvc6::String>(object, offset);
                if ((mode & (offset == 0xc ? 1u : 2u)) != 0) {
                    s.ptr = nullptr;
                    s.len = 0;
                    s.res = 0;
                } else {
                    const std::uint32_t length = rng.Below(40);
                    auto* block = static_cast<char*>(GameNew(length + 2));
                    block[0] = 0;
                    for (std::uint32_t k = 0; k < length; ++k) {
                        block[1 + k] = static_cast<char>('a' + k % 26);
                    }
                    block[1 + length] = 0;
                    s.ptr = block + 1;
                    s.len = length;
                    s.res = length;
                }
            }
            break;
        case 12:
            if ((mode & 3) == 0) {
                At<std::uint32_t>(object, 4) = 0;
            }
            if ((mode & 4) != 0) {
                At<std::uint32_t>(object, 8) = 1;
            }
            break;
        default:
            break;
    }
}

std::uint32_t Run(const FrameworkFunctions& f, int op, std::uint8_t* object, const std::uint32_t* args) {
    switch (op) {
        case 0: return f.deletePointee(object, nullptr);
        case 1: return f.setPair(object, nullptr, args[0], args[1]);
        case 2: return f.zero3(object, nullptr);
        case 3: return f.playerIndex(object, nullptr, args[0]);
        case 4: return f.memoryBlock(object, nullptr);
        case 5: return f.zero6(object, nullptr);
        case 6: return f.credits(object, nullptr);
        case 7: return f.file(object, nullptr);
        case 8: return f.netInit(object, nullptr);
        case 9: return f.lobbyInit(object, nullptr);
        case 10: return f.freeBlock(object, nullptr);
        case 11: return f.playerClear(object, nullptr);
        case 12: return f.close(object, nullptr);
        default: return f.thread(object, nullptr, args[0], args[1]);
    }
}

std::uint32_t Relative(std::uint32_t value, const std::uint8_t* object) {
    const auto base = reinterpret_cast<std::uint32_t>(object);
    return value - base < kObject ? value - base : value;
}

void CompareFramework(test::Context& ctx, const FrameworkFunctions& candidate, int cases) {
    const FrameworkFunctions original = OriginalFramework();
    std::vector<std::uint8_t> a(kGuard * 2 + kObject), b(a.size());
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOps));
        const std::uint32_t mode = ctx.rng().Next32();
        std::uint32_t args[2] = {ctx.rng().Next32(), ctx.rng().Next32()};
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        std::uint8_t* objectA = a.data() + kGuard;
        std::uint8_t* objectB = b.data() + kGuard;
        HANDLE handles[2] = {nullptr, nullptr};
        test::Rng rngA = ctx.rng();
        test::Rng rngB = ctx.rng();
        Prepare(op, objectA, rngA, mode);
        Prepare(op, objectB, rngB, mode);
        if (op == 12 && (mode & 3) != 0) {
            // Real handles, so CloseHandle succeeds (and the leftover ones
            // are closed below when the object doesn't own them).
            handles[0] = CreateEventA(nullptr, FALSE, FALSE, nullptr);
            handles[1] = CreateEventA(nullptr, FALSE, FALSE, nullptr);
            At<HANDLE>(objectA, 4) = handles[0];
            At<HANDLE>(objectB, 4) = handles[1];
        }
        std::uint32_t ra = Run(original, op, objectA, args);
        std::uint32_t rb = Run(candidate, op, objectB, args);
        if (op == 12 && handles[0] != nullptr) {
            const bool owned = (mode & 4) != 0;
            if (!owned) {
                CloseHandle(handles[0]);
                CloseHandle(handles[1]);
                ra = ra == reinterpret_cast<std::uint32_t>(handles[0]) ? 0x4a4a : ra;
                rb = rb == reinterpret_cast<std::uint32_t>(handles[1]) ? 0x4a4a : rb;
            }
        }
        if (op == 0) {
            // The freed pointer stays in the object; the blocks differ per side.
            At<std::uint32_t>(objectA, 0) = At<std::uint32_t>(objectB, 0) = 0;
        }
        if (op == 0 || op == 10) {
            ra = rb = 0;  // what delete leaves in EAX is not part of the contract
        }
        bool same = S3RE_EXPECT_EQ(Relative(ra, objectA), Relative(rb, objectB));
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, mode %08x", i, op, mode);
            return;
        }
    }
}

std::uint32_t __fastcall WrongThread(void* self, void* edx, std::uint32_t a, std::uint32_t b) {
    const std::uint32_t r = Win32_CThread_Construct(self, edx, a, b);
    At<std::uint32_t>(self, 0x18) = 0;  // wrong: not marked as "1"
    return r;
}

std::uint32_t __fastcall WrongNetInit(void* self, void* edx) {
    const std::uint32_t r = NetSession_Init_5bb040(self, edx);
    At<std::uint8_t>(self, 0x50) = static_cast<std::uint8_t>(TopByte(self) ^ 0x5a);  // wrong: not the top byte of ECX
    return r;
}

}  // namespace

S3RE_TEST(FrameworkHelpers_MatchOriginal) {
    CompareFramework(ctx, OurFramework(), 20000);
}

S3RE_TEST(FrameworkHelpers_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        FrameworkFunctions wrong = OurFramework();
        if (k == 0) {
            wrong.thread = &WrongThread;
        } else {
            wrong.netInit = &WrongNetInit;
        }
        CompareFramework(probe, wrong, 3000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

// ---------------------------------------------------------------------------
// NetHelp_Destroy_49ec80 (0x0049ec80)
// ---------------------------------------------------------------------------
//
// () thiscall, a destructor (members only, no vtable of its own; Dirk\
// nethelp.cpp, 26 callers): in this order
//   - ~string of +0xbc (inlined: erase(), then _Tidy(true));
//   - a Watcom vector at +0x38: vtable 0x657404; if its buffer +0x40 is not
//     null, NoOp 0x59f200 (the inlined element clean-up of a trivial type)
//     and operator delete(buffer); then the base vtable 0x655438 (WCExcept);
//   - a string of 16-bit characters at +0x28 (ptr +0x2c): its buffer's
//     count byte (the byte before the text) is decremented, or the block
//     (text - 2) is freed when the count is 0 or 0xff; +0x2c..+0x34 = 0;
//   - ~string of +0x18 (inlined the same way).
// EAX is not used by callers.

using DestroyMembersFn = void(__fastcall*)(void* self, void* edx);
void __fastcall NetHelp_Destroy_49ec80(void* self, void* edx);

namespace {

constexpr std::uint32_t kNetVectorVtable = 0x00657404;
constexpr std::uint32_t kWCExceptVtable = 0x00655438;

}  // namespace

S3RE_HOOK(0x0049ec80, NetHelp_Destroy_49ec80, DestroyMembersFn, Original_NetHelp_Destroy_49ec80,
          NetHelp_Destroy_49ec80);

// Purpose: Destructor of a network helper record with three texts and a
// list (probably a message or session description of the network code).
void __fastcall NetHelp_Destroy_49ec80(void* self, void* /*edx*/) {
    msvc6::String_Destroy(&At<msvc6::String>(self, 0xbc), nullptr);
    At<std::uint32_t>(self, 0x38) = kNetVectorVtable;
    if (At<void*>(self, 0x40) != nullptr) {
        using NoOpFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<NoOpFn>(0x0059f200)(static_cast<std::uint8_t*>(self) + 0x38, nullptr);
        GameDelete(At<void*>(self, 0x40));
    }
    At<std::uint32_t>(self, 0x38) = kWCExceptVtable;
    auto* wide = At<std::uint8_t*>(self, 0x2c);
    if (wide != nullptr) {
        const std::uint8_t count = wide[-1];
        if (count != 0 && count != 0xff) {
            wide[-1] = static_cast<std::uint8_t>(count - 1);
        } else {
            GameDelete(wide - 2);
        }
    }
    At<std::uint32_t>(self, 0x2c) = 0;
    At<std::uint32_t>(self, 0x30) = 0;
    At<std::uint32_t>(self, 0x34) = 0;
    msvc6::String_Destroy(&At<msvc6::String>(self, 0x18), nullptr);
}

namespace {

constexpr std::size_t kNetObject = 0xd0;

struct NetBuffers {
    char* narrow[2] = {};   // buffers of the strings at +0xbc / +0x18 (text pointers)
    std::uint8_t* wide = nullptr;
};

// Sets up one side's object: strings with a fresh buffer (count from
// `counts`), or none; the vector buffer; the wide string buffer.
NetBuffers PrepareNet(std::uint8_t* object, std::uint32_t mode, const std::uint8_t* counts, std::uint32_t length) {
    NetBuffers buffers;
    const std::size_t offsets[2] = {0xbc, 0x18};
    for (int k = 0; k < 2; ++k) {
        auto& s = At<msvc6::String>(object, offsets[k]);
        if ((mode >> k) & 1) {
            s.ptr = nullptr;
            s.len = 0;
            s.res = 0;
            continue;
        }
        auto* block = static_cast<char*>(GameNew(length + 2));
        block[0] = static_cast<char>(counts[k]);
        for (std::uint32_t c = 0; c < length; ++c) {
            block[1 + c] = static_cast<char>('a' + (c + k) % 26);
        }
        block[1 + length] = 0;
        s.ptr = block + 1;
        s.len = length;
        s.res = length;
        buffers.narrow[k] = s.ptr;
    }
    At<void*>(object, 0x40) = ((mode >> 2) & 1) != 0 ? nullptr : GameNew(24);
    if (((mode >> 3) & 1) != 0) {
        At<void*>(object, 0x2c) = nullptr;
    } else {
        auto* block = static_cast<std::uint8_t*>(GameNew(16));
        block[1] = counts[2];
        buffers.wide = block + 2;
        At<void*>(object, 0x2c) = buffers.wide;
    }
    return buffers;
}

void CompareNetDestroy(test::Context& ctx, DestroyMembersFn candidate, int cases) {
    std::uint8_t a[kNetObject], b[kNetObject];
    static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfe, 0xff};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t mode = ctx.rng().Next32();
        const std::uint8_t counts[3] = {kCounts[ctx.rng().Below(6)], kCounts[ctx.rng().Below(6)],
                                        kCounts[ctx.rng().Below(6)]};
        const std::uint32_t length = ctx.rng().Below(40);
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(b));
        const NetBuffers ba = PrepareNet(a, mode, counts, length);
        const NetBuffers bb = PrepareNet(b, mode, counts, length);
        Original_NetHelp_Destroy_49ec80(a, nullptr);
        candidate(b, nullptr);
        bool same = true;
        // Count bytes of the buffers that survive (shared ones are only
        // released): the strings' buffers are freed or split away; compare
        // the counts where the original kept the block.
        for (int k = 0; k < 2; ++k) {
            const std::uint8_t count = counts[k];
            if (ba.narrow[k] != nullptr && count != 0 && count != 0xff) {
                same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(ba.narrow[k][-1]),
                                       static_cast<std::uint8_t>(bb.narrow[k][-1]));
            }
        }
        if (ba.wide != nullptr && counts[2] != 0 && counts[2] != 0xff) {
            same &= S3RE_EXPECT_EQ(ba.wide[-1], bb.wide[-1]);
        }
        At<std::uint32_t>(a, 0x40) = At<std::uint32_t>(b, 0x40) = 0;  // the freed vector buffers differ
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, mode %08x", i, mode);
            return;
        }
    }
}

void __fastcall WrongNetDestroy(void* self, void* edx) {
    NetHelp_Destroy_49ec80(self, edx);
    At<std::uint32_t>(self, 0x38) = kNetVectorVtable;  // wrong: the base vtable is not restored
}

}  // namespace

S3RE_TEST(NetHelpDestroy_MatchOriginal) {
    CompareNetDestroy(ctx, &NetHelp_Destroy_49ec80, 5000);
}

S3RE_TEST(NetHelpDestroy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNetDestroy(probe, &WrongNetDestroy, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
