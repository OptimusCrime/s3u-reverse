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

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CWin32Handle_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);
void* __fastcall Win32_CWaitable_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);

namespace {

using framework::CPlayer;
using framework::CPlayerIndex;
using framework::CMemoryBlock;
using framework::CThread;
using framework::CWin32Handle;
using framework::SessionDesc;
using framework::WideString;

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}

#pragma pack(push, 1)
// An owning pointer (0x49d980 frees what +0 points to).
struct OwningPointer {
    void* pointee;  // +0x00
};

// A pair of dwords (0x5bd200).
struct DwordPair {
    std::uint32_t first;   // +0x00
    std::uint32_t second;  // +0x04
};

// The credits screen's text record (0x5a0960).
struct CreditsTextStyle {
    std::uint32_t font;    // +0x00 probably a default font or palette (an address in .data)
    std::uint32_t unk04;   // +0x04
    std::uint32_t size;    // +0x08 probably the size (12)
    std::uint32_t colour;  // +0x0c probably the colour (255)
    std::uint32_t unk10;   // +0x10
    std::uint32_t unk14;   // +0x14
};
static_assert(offsetof(CreditsTextStyle, unk14) == 0x14);

// A lobby record with a string at +8 (0x5ebfb0).
struct LobbyRecord5ebfb0 {
    std::uint32_t unk00;  // +0x00
    std::uint32_t unk04;  // +0x04
    msvc6::String text;   // +0x08
};
static_assert(offsetof(LobbyRecord5ebfb0, text) == 0x08);

// The network helper record destroyed by 0x49ec80 (members only).
struct NetHelpRecord {
    std::uint8_t unk00[0x18];
    msvc6::String text18;        // +0x18
    WideString wide28;           // +0x28
    watcom::PtrVector vector38;  // +0x38 a Watcom vector of a trivial type
    std::uint8_t unk50[0xbc - 0x50];
    msvc6::String textBc;        // +0xbc
};
static_assert(offsetof(NetHelpRecord, text18) == 0x18);
static_assert(offsetof(NetHelpRecord, wide28) == 0x28);
static_assert(offsetof(NetHelpRecord, vector38) == 0x38);
static_assert(offsetof(NetHelpRecord, textBc) == 0xbc);
#pragma pack(pop)

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
    msvc6::GameOperatorDelete(As<OwningPointer>(self)->pointee);
    return 0;
}

S3RE_HOOK(0x005bd200, NetSession_SetPair_5bd200, Obj2Fn, Original_NetSession_SetPair_5bd200,
          NetSession_SetPair_5bd200);

// Purpose: Stores two values in a small network-session record (probably
// the constructor of a pair such as player id and index).
std::uint32_t __fastcall NetSession_SetPair_5bd200(void* self, void* /*edx*/, std::uint32_t a, std::uint32_t b) {
    As<DwordPair>(self)->first = a;
    As<DwordPair>(self)->second = b;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005dc810, Lobby_ZeroThreeDwords_5dc810, ObjFn, Original_Lobby_ZeroThreeDwords_5dc810,
          Lobby_ZeroThreeDwords_5dc810);

// Purpose: Empties a three-field lobby record (probably the constructor of
// an empty vector or list header).
std::uint32_t __fastcall Lobby_ZeroThreeDwords_5dc810(void* self, void* /*edx*/) {
    std::uint32_t* const dwords = As<std::uint32_t>(self);  // a record of three dwords
    for (std::size_t k = 0; k < 3; ++k) {
        dwords[k] = 0;
    }
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x006065f0, Lobby_CPlayerIndex_Construct, Obj1Fn, Original_Lobby_CPlayerIndex_Construct,
          Lobby_CPlayerIndex_Construct);

// Purpose: Constructor of the online lobby's player index (a player number
// with one more field).
std::uint32_t __fastcall Lobby_CPlayerIndex_Construct(void* self, void* /*edx*/, std::uint32_t a) {
    CPlayerIndex* const object = As<CPlayerIndex>(self);
    object->index = a;
    object->unk08 = 0;
    object->vtable = vtables::kCPlayerIndex;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00600e00, Lobby_CMemoryBlock_Construct, ObjFn, Original_Lobby_CMemoryBlock_Construct,
          Lobby_CMemoryBlock_Construct);

// Purpose: Constructor of an empty memory block of the online lobby code.
std::uint32_t __fastcall Lobby_CMemoryBlock_Construct(void* self, void* /*edx*/) {
    CMemoryBlock* const block = As<CMemoryBlock>(self);
    block->data = nullptr;
    block->reserved = 0;
    block->used = 0;
    block->vtable = vtables::kCMemoryBlock;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005ec1e0, Lobby_ZeroSixDwords_5ec1e0, ObjFn, Original_Lobby_ZeroSixDwords_5ec1e0,
          Lobby_ZeroSixDwords_5ec1e0);

// Purpose: Empties a six-field lobby record (probably a constructor).
std::uint32_t __fastcall Lobby_ZeroSixDwords_5ec1e0(void* self, void* /*edx*/) {
    std::uint32_t* const dwords = As<std::uint32_t>(self);  // a record of six dwords
    for (std::size_t k = 0; k < 6; ++k) {
        dwords[k] = 0;
    }
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005a0960, Credits_TextStyle_5a0960, ObjFn, Original_Credits_TextStyle_5a0960, Credits_TextStyle_5a0960);

// Purpose: Sets up a text record of the credits screen with its defaults
// (probably font, size 12 and colour 255).
std::uint32_t __fastcall Credits_TextStyle_5a0960(void* self, void* /*edx*/) {
    CreditsTextStyle* const style = As<CreditsTextStyle>(self);
    style->unk10 = 0;
    style->unk14 = 0;
    style->font = static_cast<std::uint32_t>(g_creditsDefaultStyle.address());
    style->unk04 = 0;
    style->size = 12;
    style->colour = 255;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x006195c0, Win32_CFile_Construct, ObjFn, Original_Win32_CFile_Construct, Win32_CFile_Construct);

// Purpose: Constructor of the framework's file object (a Windows handle
// wrapper without a file yet).
std::uint32_t __fastcall Win32_CFile_Construct(void* self, void* /*edx*/) {
    Win32_CWin32Handle_Construct(self, nullptr, 0, 0);
    As<CWin32Handle>(self)->vtable = vtables::kCFile;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005bb040, NetSession_Init_5bb040, ObjFn, Original_NetSession_Init_5bb040, NetSession_Init_5bb040);

// Purpose: Empties a network-session record (probably the constructor of a
// container with an allocator byte at +0x50).
std::uint32_t __fastcall NetSession_Init_5bb040(void* self, void* /*edx*/) {
    SessionDesc* const session = As<SessionDesc>(self);
    session->name.allocator = TopByte(self);
    session->name.ptr = nullptr;
    session->name.len = 0;
    session->name.res = 0;
    session->desc[0] = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005ebfb0, Lobby_Init_5ebfb0, ObjFn, Original_Lobby_Init_5ebfb0, Lobby_Init_5ebfb0);

// Purpose: Empties a lobby record (probably the constructor of a record
// holding a container with an allocator byte at +8).
std::uint32_t __fastcall Lobby_Init_5ebfb0(void* self, void* /*edx*/) {
    LobbyRecord5ebfb0* const record = As<LobbyRecord5ebfb0>(self);
    record->unk00 = 0;
    record->unk04 = 0;
    record->text.allocator = TopByte(self);
    record->text.ptr = nullptr;
    record->text.len = 0;
    record->text.res = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00601250, Lobby_FreeBlock_601250, ObjFn, Original_Lobby_FreeBlock_601250, Lobby_FreeBlock_601250);

// Purpose: Frees the buffer of a lobby memory block and empties it.
std::uint32_t __fastcall Lobby_FreeBlock_601250(void* self, void* /*edx*/) {
    CMemoryBlock* const block = As<CMemoryBlock>(self);
    msvc6::GameOperatorDelete(block->data);
    block->data = nullptr;
    block->used = 0;
    block->reserved = 0;
    return 0;
}

S3RE_HOOK(0x005c0ef0, DirectX_CPlayer_Clear, ObjFn, Original_DirectX_CPlayer_Clear, DirectX_CPlayer_Clear);

// Purpose: Resets a network player record: two ids to 0 and both names
// (probably player and computer name) to the empty string.
std::uint32_t __fastcall DirectX_CPlayer_Clear(void* self, void* /*edx*/) {
    CPlayer* const player = As<CPlayer>(self);
    player->unk04 = 0;
    player->unk08 = 0;
    const char* empty = texts::kEmpty;
    msvc6::String_AssignTextOrEmpty(&player->name1c, nullptr, empty);
    return reinterpret_cast<std::uint32_t>(msvc6::String_AssignTextOrEmpty(&player->name0c, nullptr, empty));
}

S3RE_HOOK(0x0060bf20, Win32_CWin32Handle_Close, ObjFn, Original_Win32_CWin32Handle_Close, Win32_CWin32Handle_Close);

// Purpose: Closes the Windows handle a framework object owns (events,
// threads, files) and forgets it.
std::uint32_t __fastcall Win32_CWin32Handle_Close(void* self, void* /*edx*/) {
    CWin32Handle* const object = As<CWin32Handle>(self);
    std::uint32_t result = object->handle;
    if (result != 0 && object->owned == 1) {
        result = static_cast<std::uint32_t>(g_importCloseHandle.get()(reinterpret_cast<HANDLE>(result)));
    }
    object->handle = 0;
    object->owned = 0;
    return result;
}

S3RE_HOOK(0x0060df70, Win32_CThread_Construct, Obj2Fn, Original_Win32_CThread_Construct, Win32_CThread_Construct);

// Purpose: Constructor of the framework's thread class (the base of the
// game's logic, draw and network threads); the thread itself is started
// later.
std::uint32_t __fastcall Win32_CThread_Construct(void* self, void* /*edx*/, std::uint32_t a, std::uint32_t b) {
    Win32_CWaitable_Construct(self, nullptr, 0, 0);
    CThread* const thread = As<CThread>(self);
    thread->unk14 = 0;
    thread->unk1c = a;
    thread->unk18 = 1;
    thread->unk20 = b;
    thread->unk24 = 0;
    thread->waitable.vtable = vtables::kCThread;
    thread->waitable.handle.vtable = vtables::kCThread_sub4;
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
            As<OwningPointer>(object)->pointee = (mode & 1) != 0 ? nullptr : msvc6::GameOperatorNew(8 + (mode & 0x30));
            break;
        case 10:
            As<CMemoryBlock>(object)->data =
                (mode & 1) != 0 ? nullptr : static_cast<std::uint8_t*>(msvc6::GameOperatorNew(8 + (mode & 0x30)));
            break;
        case 11: {
            // Both strings: empty (null), or a fresh unshared buffer with text.
            CPlayer* const player = As<CPlayer>(object);
            for (msvc6::String* const string : {&player->name0c, &player->name1c}) {
                auto& s = *string;
                if ((mode & (string == &player->name0c ? 1u : 2u)) != 0) {
                    s.ptr = nullptr;
                    s.len = 0;
                    s.res = 0;
                } else {
                    const std::uint32_t length = rng.Below(40);
                    auto* block = static_cast<char*>(msvc6::GameOperatorNew(length + 2));
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
        }
        case 12:
            if ((mode & 3) == 0) {
                As<CWin32Handle>(object)->handle = 0;
            }
            if ((mode & 4) != 0) {
                As<CWin32Handle>(object)->owned = 1;
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
            As<CWin32Handle>(objectA)->handle = reinterpret_cast<std::uint32_t>(handles[0]);
            As<CWin32Handle>(objectB)->handle = reinterpret_cast<std::uint32_t>(handles[1]);
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
            As<OwningPointer>(objectA)->pointee = As<OwningPointer>(objectB)->pointee = nullptr;
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
    As<CThread>(self)->unk18 = 0;  // wrong: not marked as "1"
    return r;
}

std::uint32_t __fastcall WrongNetInit(void* self, void* edx) {
    const std::uint32_t r = NetSession_Init_5bb040(self, edx);
    As<SessionDesc>(self)->name.allocator = static_cast<std::uint8_t>(TopByte(self) ^ 0x5a);  // wrong: not the top byte of ECX
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
// GamePlayerSetup_Destruct (0x0049ec80)
// ---------------------------------------------------------------------------
//
// () thiscall, the GamePlayerSetup destructor (game_settings_layout.h;
// members only, no vtable of its own; Dirk\
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
void __fastcall GamePlayerSetup_Destruct(void* self, void* edx);

namespace {

}  // namespace

S3RE_HOOK(0x0049ec80, GamePlayerSetup_Destruct, DestroyMembersFn, Original_GamePlayerSetup_Destruct,
          GamePlayerSetup_Destruct);

// Purpose: Destructor of a network helper record with three texts and a
// list (probably a message or session description of the network code).
void __fastcall GamePlayerSetup_Destruct(void* self, void* /*edx*/) {
    NetHelpRecord* const record = As<NetHelpRecord>(self);
    msvc6::String_Destroy(&record->textBc, nullptr);
    record->vector38.vtable = vtables::kWCBareVectorBase_ushort;
    if (record->vector38.data != nullptr) {
        using NoOpFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<NoOpFn>(0x0059f200)(&record->vector38, nullptr);
        msvc6::GameOperatorDelete(record->vector38.data);
    }
    record->vector38.vtable = vtables::kWCExcept;
    auto* wide = reinterpret_cast<std::uint8_t*>(record->wide28.ptr);
    if (wide != nullptr) {
        const std::uint8_t count = wide[-1];
        if (count != 0 && count != 0xff) {
            wide[-1] = static_cast<std::uint8_t>(count - 1);
        } else {
            msvc6::GameOperatorDelete(wide - 2);
        }
    }
    record->wide28.ptr = nullptr;
    record->wide28.len = 0;
    record->wide28.res = 0;
    msvc6::String_Destroy(&record->text18, nullptr);
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
    NetHelpRecord* const record = As<NetHelpRecord>(object);
    msvc6::String* const strings[2] = {&record->textBc, &record->text18};
    for (int k = 0; k < 2; ++k) {
        auto& s = *strings[k];
        if ((mode >> k) & 1) {
            s.ptr = nullptr;
            s.len = 0;
            s.res = 0;
            continue;
        }
        auto* block = static_cast<char*>(msvc6::GameOperatorNew(length + 2));
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
    record->vector38.data = ((mode >> 2) & 1) != 0 ? nullptr : static_cast<std::uint32_t*>(msvc6::GameOperatorNew(24));
    if (((mode >> 3) & 1) != 0) {
        record->wide28.ptr = nullptr;
    } else {
        auto* block = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(16));
        block[1] = counts[2];
        buffers.wide = block + 2;
        record->wide28.ptr = reinterpret_cast<std::uint16_t*>(buffers.wide);
    }
    return buffers;
}

void CompareSetupDestruct(test::Context& ctx, DestroyMembersFn candidate, int cases) {
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
        Original_GamePlayerSetup_Destruct(a, nullptr);
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
        // The freed vector buffers differ.
        As<NetHelpRecord>(a)->vector38.data = As<NetHelpRecord>(b)->vector38.data = nullptr;
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, mode %08x", i, mode);
            return;
        }
    }
}

void __fastcall WrongNetDestroy(void* self, void* edx) {
    GamePlayerSetup_Destruct(self, edx);
    As<NetHelpRecord>(self)->vector38.vtable = vtables::kWCBareVectorBase_ushort;  // wrong: the base vtable is not restored
}

}  // namespace

S3RE_TEST(GamePlayerSetup_Destruct_MatchOriginal) {
    CompareSetupDestruct(ctx, &GamePlayerSetup_Destruct, 5000);
}

S3RE_TEST(GamePlayerSetup_Destruct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupDestruct(probe, &WrongNetDestroy, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
