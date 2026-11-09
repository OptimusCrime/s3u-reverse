// Small members of the framework, Watcom container, UI and graphics classes
// (batch 27). thiscall (ECX = the object) unless noted; names are ours
// where the RTTI has none.
//
// WCIsvListBase_Destroy (0x0060b5a0): vtable 0x655fa4; if the list is not
//   empty (+8), the library's error hook 0x59f200 (empty in this build);
//   vtable 0x655438 (the base). EAX = +8. 30 callers.
// WCIsvList_Insert (0x0060b430): (node) ret 4: an intrusive circular list
//   with +8 = the last node, +0xc = the count. Empty: node->next = node,
//   last = node, EAX = node. Otherwise node->next = last->next,
//   last->next = node (the node becomes the first), EAX = the new count.
// WCIntVector_Construct (0x004376b0): (capacity, b) ret 8: +4 = 0, vtable
//   0x655a5c, +0xc = capacity, +0x10 = 0, +8 = new(capacity * 4) (none for
//   0; capacity = 0 when new fails), +0x14 = 0, +0x18 = b, vtable 0x6559f8.
//   EAX = this. 20 callers.
// WCIntVector_Destroy (0x00414590): vtable 0x655a5c; if +8: error hook,
//   operator delete(+8); vtable 0x655438. EAX = delete's result, or 0.
// WCUIntVector_Destroy (0x00458590): the same destructor for another
//   element type (vtable 0x657158 first). 8 callers.
// WCIntVector_ConstructBare (0x00437730): (capacity) ret 4: the first part
//   of WCIntVector_Construct only (+4, vtable 0x655a5c, +0xc, +0x10, +8);
//   EAX = this. 7 callers.
// Lobby::CLMMessageBlock copy constructor (0x00601470): (other) ret 4: the
//   block at +4 copy-constructed (0x600e40) from other's, +0x14..+0x20
//   copied, vtable 0x65d4fc; EAX = this. 9 callers.
// Lobby_ScaleBy4_44c450 (0x0044c450): stdcall (out, a, b, c, d) ret 14h
//   (ECX unused): out = {a * 4, b * 4, c * 4, d * 4}; EAX = out. 21
//   callers (probably a rectangle in map units to pixels).
// Win32_CWin32Handle_Set (0x0060bf00): (handle, owned) ret 8: +4 = handle,
//   +8 = owned; EAX = handle. 11 callers.
// Lobby::CMemoryBlock::operator= (0x00600e80): (other) ret 4: unless the
//   same object: FreeBlock (0x601250), Append(other) (0x601170). EAX = this.
// Win32::CRegistryKey::CRegistryKey (0x00616430): +4 = 0, vtable 0x65d98c;
//   EAX = this.
// Win32::CEvent::CEvent (0x0060c510): CWaitable(0, 0) (0x60bf50), vtables
//   +0 = 0x65d690, +4 = 0x65d684; EAX = this.
// Lobby_HasField4_5da3a0 (0x005da3a0): EAX = (+4 != 0).
// UI_CopyWords_418fb0 (0x00418fb0): fastcall (ECX = to, EDX = from, stack:
//   count) ret 4: copies `count` 16-bit characters; EAX = to.
// UI_WideEos_428880 (0x00428880): (n) ret 4: the wide string's _Eos: +8 =
//   n, word [+4][n] = 0; EAX = n.
// GfxData_FramePointer_5fac70 (0x005fac70): (out, id = group << 16 |
//   frame) ret 8: *out = table(+0x5ac)[group][frame + 2] + base(+0x264)
//   [group]; EAX = out. 23 callers (sprite data of a frame).
// GfxData_AnimationPointer_5faaf0 (0x005faaf0): the same with the table at
//   +0x494, and DebugLog "GFX: Animation not found %u:%u" (group, frame)
//   when frame >= the word +6 of the group's table (it reads on anyway).

#include "game/debug_log.h"
#include "game/framework_layout.h"
#include "game/raw.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CWaitable_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);
// framework_helpers.cpp
std::uint32_t __fastcall Lobby_FreeBlock_601250(void* self, void* edx);
// lobby_memory_block.cpp
std::uint32_t __fastcall Lobby_MemoryBlock_Append(void* self, void* edx, const void* other);
std::uint32_t __fastcall Lobby_MemoryBlock_Copy(void* self, void* edx, const void* other);

namespace {

constexpr std::uint32_t kIsvListVtable = 0x00655fa4;
constexpr std::uint32_t kContainerBaseVtable = 0x00655438;
constexpr std::uint32_t kIntVectorBaseVtable = 0x00655a5c;
constexpr std::uint32_t kIntVectorVtable = 0x006559f8;
constexpr std::uint32_t kUIntVectorBaseVtable = 0x00657158;
constexpr std::uint32_t kMessageBlockVtable = 0x0065d4fc;
constexpr std::uint32_t kRegistryKeyVtable = 0x0065d98c;
constexpr std::uint32_t kEventVtable = 0x0065d690;
constexpr std::uint32_t kEventHandleVtable = 0x0065d684;
constexpr std::uintptr_t kAnimationNotFound = 0x007a2e9c;  // "GFX: Animation not found %u:%u"

using framework::CLMMessageBlock;
using framework::CRegistryKey;
using framework::CWaitable;
using framework::CWin32Handle;
using framework::GfxData;
using framework::WideString;

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}

#pragma pack(push, 1)
// A Watcom intrusive circular list (WCIsvSList-like).
struct IsvList {
    std::uint32_t vtable;  // +0x00
    std::uint32_t unk04;   // +0x04
    std::uint32_t last;    // +0x08 the last node (its first dword is the next pointer); 0: empty
    std::uint32_t count;   // +0x0c
};
static_assert(offsetof(IsvList, count) == 0x0c);

// A Watcom vector of ints: the vector layout of watcom/ptr_vector.h plus one
// field of the derived class.
struct IntVector {
    watcom::PtrVector vector;  // +0x00
    std::uint32_t unk18;       // +0x18 second constructor argument
};
static_assert(offsetof(IntVector, unk18) == 0x18);
#pragma pack(pop)

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

std::uint32_t GameDelete(void* block) {
    return reinterpret_cast<std::uint32_t(__cdecl*)(void*)>(0x0061e4ef)(block);
}

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

void ConstructIntVectorStart(void* self, std::uint32_t capacity) {
    watcom::PtrVector* const vector = As<watcom::PtrVector>(self);
    vector->unknown04 = 0;
    vector->vtable = kIntVectorBaseVtable;
    vector->capacity = capacity;
    vector->filled = 0;
    if (capacity != 0) {
        void* block = GameNew(capacity << 2);
        vector->data = static_cast<std::uint32_t*>(block);
        if (block == nullptr) {
            vector->capacity = 0;
        }
    } else {
        vector->data = nullptr;
    }
}


}  // namespace

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using This1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using This2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using ScaleFn = std::uint32_t(__stdcall*)(std::uint32_t* out, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                          std::uint32_t d);
using CopyWordsFn = std::uint32_t(__fastcall*)(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count);

std::uint32_t __fastcall WCIsvListBase_Destroy(void* self, void* edx);
std::uint32_t __fastcall WCIsvList_Insert(void* self, void* edx, std::uint32_t node);
std::uint32_t __fastcall WCIntVector_Construct(void* self, void* edx, std::uint32_t capacity, std::uint32_t b);
std::uint32_t __fastcall WCIntVector_Destroy(void* self, void* edx);
std::uint32_t __stdcall Lobby_ScaleBy4_44c450(std::uint32_t* out, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                              std::uint32_t d);
std::uint32_t __fastcall WCUIntVector_Destroy(void* self, void* edx);
std::uint32_t __fastcall WCIntVector_ConstructBare(void* self, void* edx, std::uint32_t capacity);
std::uint32_t __fastcall Lobby_CLMMessageBlock_Copy(void* self, void* edx, std::uint32_t other);
std::uint32_t __fastcall Win32_CWin32Handle_Set(void* self, void* edx, std::uint32_t handle, std::uint32_t owned);
std::uint32_t __fastcall Lobby_MemoryBlock_Assign(void* self, void* edx, std::uint32_t other);
std::uint32_t __fastcall Win32_CRegistryKey_Construct(void* self, void* edx);
std::uint32_t __fastcall Win32_CEvent_Construct(void* self, void* edx);
std::uint32_t __fastcall Lobby_HasField4_5da3a0(void* self, void* edx);
std::uint32_t __fastcall UI_CopyWords_418fb0(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count);
std::uint32_t __fastcall UI_WideEos_428880(void* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall GfxData_FramePointer_5fac70(void* self, void* edx, std::uint32_t out, std::uint32_t id);
std::uint32_t __fastcall GfxData_AnimationPointer_5faaf0(void* self, void* edx, std::uint32_t out, std::uint32_t id);

S3RE_HOOK(0x0060b5a0, WCIsvListBase_Destroy, ThisFn, Original_WCIsvListBase_Destroy, WCIsvListBase_Destroy);

// Purpose: Destructor of a Watcom intrusive list (the library would report
// a list that still holds nodes; that hook is empty in this build).
std::uint32_t __fastcall WCIsvListBase_Destroy(void* self, void* /*edx*/) {
    IsvList* const list = As<IsvList>(self);
    list->vtable = kIsvListVtable;
    const std::uint32_t last = list->last;
    list->vtable = kContainerBaseVtable;
    return last;
}

S3RE_HOOK(0x0060b430, WCIsvList_Insert, This1Fn, Original_WCIsvList_Insert, WCIsvList_Insert);

// Purpose: Puts a node at the front of a Watcom intrusive circular list.
std::uint32_t __fastcall WCIsvList_Insert(void* self, void* /*edx*/, std::uint32_t node) {
    IsvList* const list = As<IsvList>(self);
    auto* added = reinterpret_cast<std::uint32_t*>(node);
    const std::uint32_t last = list->last;
    if (last == 0) {
        *added = node;
        list->last = node;
        ++list->count;
        return node;
    }
    *added = *reinterpret_cast<std::uint32_t*>(last);
    *reinterpret_cast<std::uint32_t*>(list->last) = node;
    return ++list->count;
}

S3RE_HOOK(0x004376b0, WCIntVector_Construct, This2Fn, Original_WCIntVector_Construct, WCIntVector_Construct);

// Purpose: Constructor of a Watcom vector of ints with room for `capacity`
// entries.
std::uint32_t __fastcall WCIntVector_Construct(void* self, void* /*edx*/, std::uint32_t capacity, std::uint32_t b) {
    ConstructIntVectorStart(self, capacity);
    IntVector* const vector = As<IntVector>(self);
    vector->vector.length = 0;
    vector->unk18 = b;
    vector->vector.vtable = kIntVectorVtable;
    return Address(self);
}

S3RE_HOOK(0x00414590, WCIntVector_Destroy, ThisFn, Original_WCIntVector_Destroy, WCIntVector_Destroy);

// Purpose: Destructor of a Watcom vector of ints (frees its block).
std::uint32_t __fastcall WCIntVector_Destroy(void* self, void* /*edx*/) {
    watcom::PtrVector* const vector = As<watcom::PtrVector>(self);
    vector->vtable = kIntVectorBaseVtable;
    std::uint32_t result = Address(vector->data);
    if (result != 0) {
        result = GameDelete(vector->data);  // (after the empty error hook 0x59f200)
    }
    vector->vtable = kContainerBaseVtable;
    return result;
}

S3RE_HOOK(0x00458590, WCUIntVector_Destroy, ThisFn, Original_WCUIntVector_Destroy, WCUIntVector_Destroy);

// Purpose: Destructor of a Watcom vector of unsigned ints (frees its block).
std::uint32_t __fastcall WCUIntVector_Destroy(void* self, void* /*edx*/) {
    watcom::PtrVector* const vector = As<watcom::PtrVector>(self);
    vector->vtable = kUIntVectorBaseVtable;
    std::uint32_t result = Address(vector->data);
    if (result != 0) {
        result = GameDelete(vector->data);  // (after the empty error hook 0x59f200)
    }
    vector->vtable = kContainerBaseVtable;
    return result;
}

S3RE_HOOK(0x00437730, WCIntVector_ConstructBare, This1Fn, Original_WCIntVector_ConstructBare,
          WCIntVector_ConstructBare);

// Purpose: Constructor of the base part of a Watcom vector of ints with room
// for `capacity` entries.
std::uint32_t __fastcall WCIntVector_ConstructBare(void* self, void* /*edx*/, std::uint32_t capacity) {
    ConstructIntVectorStart(self, capacity);
    return Address(self);
}

S3RE_HOOK(0x00601470, Lobby_CLMMessageBlock_Copy, This1Fn, Original_Lobby_CLMMessageBlock_Copy,
          Lobby_CLMMessageBlock_Copy);

// Purpose: Copy constructor of a protocol message block (its bytes and
// four fields).
std::uint32_t __fastcall Lobby_CLMMessageBlock_Copy(void* self, void* /*edx*/, std::uint32_t other) {
    CLMMessageBlock* const to = As<CLMMessageBlock>(self);
    CLMMessageBlock* const from = reinterpret_cast<CLMMessageBlock*>(other);
    Lobby_MemoryBlock_Copy(&to->block, nullptr, &from->block);
    for (std::size_t k = 0; k < 4; ++k) {
        to->fields[k] = from->fields[k];
    }
    to->vtable = kMessageBlockVtable;
    return Address(self);
}

S3RE_HOOK(0x0044c450, Lobby_ScaleBy4_44c450, ScaleFn, Original_Lobby_ScaleBy4_44c450, Lobby_ScaleBy4_44c450);

// Purpose: Builds four values times 4 (probably a rectangle converted to
// finer units for the menus).
std::uint32_t __stdcall Lobby_ScaleBy4_44c450(std::uint32_t* out, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                              std::uint32_t d) {
    out[0] = a << 2;
    out[1] = b << 2;
    out[2] = c << 2;
    out[3] = d << 2;
    return Address(out);
}

S3RE_HOOK(0x0060bf00, Win32_CWin32Handle_Set, This2Fn, Original_Win32_CWin32Handle_Set, Win32_CWin32Handle_Set);

// Purpose: Sets a Windows handle wrapper's handle and whether it owns it.
std::uint32_t __fastcall Win32_CWin32Handle_Set(void* self, void* /*edx*/, std::uint32_t handle, std::uint32_t owned) {
    As<CWin32Handle>(self)->handle = handle;
    As<CWin32Handle>(self)->owned = owned;
    return handle;
}

S3RE_HOOK(0x00600e80, Lobby_MemoryBlock_Assign, This1Fn, Original_Lobby_MemoryBlock_Assign,
          Lobby_MemoryBlock_Assign);

// Purpose: Assignment of a protocol buffer (copies the other's bytes).
std::uint32_t __fastcall Lobby_MemoryBlock_Assign(void* self, void* /*edx*/, std::uint32_t other) {
    if (Address(self) != other) {
        Lobby_FreeBlock_601250(self, nullptr);
        Lobby_MemoryBlock_Append(self, nullptr, reinterpret_cast<const void*>(other));
    }
    return Address(self);
}

S3RE_HOOK(0x00616430, Win32_CRegistryKey_Construct, ThisFn, Original_Win32_CRegistryKey_Construct,
          Win32_CRegistryKey_Construct);

// Purpose: Constructor of the registry key wrapper (no key open yet).
std::uint32_t __fastcall Win32_CRegistryKey_Construct(void* self, void* /*edx*/) {
    As<CRegistryKey>(self)->key = 0;
    As<CRegistryKey>(self)->vtable = kRegistryKeyVtable;
    return Address(self);
}

S3RE_HOOK(0x0060c510, Win32_CEvent_Construct, ThisFn, Original_Win32_CEvent_Construct, Win32_CEvent_Construct);

// Purpose: Constructor of the Windows event wrapper (no event yet).
std::uint32_t __fastcall Win32_CEvent_Construct(void* self, void* /*edx*/) {
    Win32_CWaitable_Construct(self, nullptr, 0, 0);
    As<CWaitable>(self)->vtable = kEventVtable;
    As<CWaitable>(self)->handle.vtable = kEventHandleVtable;
    return Address(self);
}

S3RE_HOOK(0x005da3a0, Lobby_HasField4_5da3a0, ThisFn, Original_Lobby_HasField4_5da3a0, Lobby_HasField4_5da3a0);

// Purpose: Whether a lobby object's pointer at +4 is set (meaning not
// identified).
std::uint32_t __fastcall Lobby_HasField4_5da3a0(void* self, void* /*edx*/) {
    // The dword at +4 of an unidentified lobby object (its only known field).
    return RawAt<std::uint32_t>(self, 4) != 0 ? 1 : 0;
}

S3RE_HOOK(0x00418fb0, UI_CopyWords_418fb0, CopyWordsFn, Original_UI_CopyWords_418fb0, UI_CopyWords_418fb0);

// Purpose: Copies 16-bit characters (part of the UI's wide string code).
std::uint32_t __fastcall UI_CopyWords_418fb0(std::uint16_t* to, const std::uint16_t* from, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        to[i] = from[i];
    }
    return Address(to);
}

S3RE_HOOK(0x00428880, UI_WideEos_428880, This1Fn, Original_UI_WideEos_428880, UI_WideEos_428880);

// Purpose: Sets a wide string's length and writes its terminating zero.
std::uint32_t __fastcall UI_WideEos_428880(void* self, void* /*edx*/, std::uint32_t n) {
    WideString* const string = As<WideString>(self);
    string->len = n;
    // 32-bit address arithmetic, as the original's [ptr + n * 2].
    reinterpret_cast<std::uint16_t*>(Address(string->ptr) + n * 2)[0] = 0;
    return n;
}

S3RE_HOOK(0x005fac70, GfxData_FramePointer_5fac70, This2Fn, Original_GfxData_FramePointer_5fac70,
          GfxData_FramePointer_5fac70);

// Purpose: The address of a sprite frame's data in the loaded graphics
// (group << 16 | frame).
std::uint32_t __fastcall GfxData_FramePointer_5fac70(void* self, void* /*edx*/, std::uint32_t out, std::uint32_t id) {
    const std::uint32_t group = id >> 16;
    const std::uint32_t frame = id & 0xffff;
    GfxData* const gfx = As<GfxData>(self);
    const std::uint32_t table = gfx->frameTables5ac[group];
    const std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(table + frame * 4 + 8);
    *reinterpret_cast<std::uint32_t*>(out) = offset + gfx->groupBase[group];
    return out;
}

S3RE_HOOK(0x005faaf0, GfxData_AnimationPointer_5faaf0, This2Fn, Original_GfxData_AnimationPointer_5faaf0,
          GfxData_AnimationPointer_5faaf0);

// Purpose: The address of an animation's data in the loaded graphics, with
// the original's (only logged) range check.
std::uint32_t __fastcall GfxData_AnimationPointer_5faaf0(void* self, void* /*edx*/, std::uint32_t out,
                                                         std::uint32_t id) {
    const std::uint32_t group = id >> 16;
    const std::uint32_t frame = id & 0xffff;
    GfxData* const gfx = As<GfxData>(self);
    const std::uint32_t table = gfx->animationTables[group];
    if (frame >= *reinterpret_cast<const std::uint16_t*>(table + 6)) {
        GameDebugLog(kAnimationNotFound, group, frame);
    }
    const std::uint32_t offset =
        *reinterpret_cast<const std::uint32_t*>(gfx->animationTables[group] + frame * 4 + 8);
    *reinterpret_cast<std::uint32_t*>(out) = offset + gfx->groupBase[group];
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each function runs on two identical random buffers (object plus guard
// bytes; pointer fields set where needed), one with the original and one
// with ours. Compared: EAX and every byte. Heap blocks (int vector) are
// compared by null-ness; DebugLog calls are recorded and compared.

namespace {

constexpr std::size_t kBytes = 0x40;

struct Pair {
    std::uint8_t a[kBytes];
    std::uint8_t b[kBytes];
};

struct Functions {
    ThisFn isvDestroy;
    This1Fn isvInsert;
    This2Fn intCtor;
    ThisFn intDtor;
    ScaleFn scale;
    This2Fn handleSet;
    This1Fn blockAssign;
    ThisFn registryKey;
    ThisFn event;
    ThisFn hasField4;
    CopyWordsFn copyWords;
    This1Fn wideEos;
    This2Fn framePointer;
    This2Fn animationPointer;
    ThisFn uintDtor;
    This1Fn intCtorBare;
    This1Fn messageBlockCopy;
};

Functions OriginalFunctions() {
    return {Original_WCIsvListBase_Destroy,     Original_WCIsvList_Insert,
            Original_WCIntVector_Construct,     Original_WCIntVector_Destroy,
            Original_Lobby_ScaleBy4_44c450,     Original_Win32_CWin32Handle_Set,
            Original_Lobby_MemoryBlock_Assign,  Original_Win32_CRegistryKey_Construct,
            Original_Win32_CEvent_Construct,    Original_Lobby_HasField4_5da3a0,
            Original_UI_CopyWords_418fb0,       Original_UI_WideEos_428880,
            Original_GfxData_FramePointer_5fac70, Original_GfxData_AnimationPointer_5faaf0,
            Original_WCUIntVector_Destroy,      Original_WCIntVector_ConstructBare,
            Original_Lobby_CLMMessageBlock_Copy};
}

Functions OurFunctions() {
    return {&WCIsvListBase_Destroy,     &WCIsvList_Insert,         &WCIntVector_Construct,
            &WCIntVector_Destroy,       &Lobby_ScaleBy4_44c450,    &Win32_CWin32Handle_Set,
            &Lobby_MemoryBlock_Assign,  &Win32_CRegistryKey_Construct, &Win32_CEvent_Construct,
            &Lobby_HasField4_5da3a0,    &UI_CopyWords_418fb0,      &UI_WideEos_428880,
            &GfxData_FramePointer_5fac70, &GfxData_AnimationPointer_5faaf0, &WCUIntVector_Destroy,
            &WCIntVector_ConstructBare, &Lobby_CLMMessageBlock_Copy};
}

// Puts a pointer to `target` (a position in the same buffer) at `offset`.
void Link(std::uint8_t* buffer, std::size_t offset, std::size_t target) {
    const std::uint32_t p = Address(buffer + target);
    std::memcpy(buffer + offset, &p, 4);
}

// Replaces pointers into each buffer by their offsets, so both compare.
void Unlink(std::uint8_t* buffer) {
    for (std::size_t i = 0; i + 4 <= kBytes; i += 4) {
        std::uint32_t v;
        std::memcpy(&v, buffer + i, 4);
        if (v >= Address(buffer) && v < Address(buffer + kBytes)) {
            v = 0xbeef0000u + (v - Address(buffer));
            std::memcpy(buffer + i, &v, 4);
        }
    }
}

void CompareSmall(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original = OriginalFunctions();
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(17));
        Pair p;
        ctx.rng().Fill(p.a, kBytes);
        const std::uint32_t x = ctx.rng().Chance(1, 3) ? ctx.rng().Below(8) : ctx.rng().Next32();
        const std::uint32_t y = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        // Pointer fields per op (into each side's own buffer).
        const bool linked = ctx.rng().Chance(1, 2);
        auto setUp = [&](std::uint8_t* buffer) {
            switch (op) {
                case 1:  // last node: 0 or a node in the buffer, whose next is a node too
                    if (!linked) {
                        As<IsvList>(buffer)->last = 0;
                    } else {
                        Link(buffer, 8, 0x20);
                        Link(buffer, 0x20, 0x28);
                    }
                    break;
                case 3:
                case 14:
                    As<watcom::PtrVector>(buffer)->data = nullptr;  // no block: nothing to free
                    break;
                case 6:  // a memory block at 0x10 to assign from (empty, so nothing is read)
                    for (framework::CMemoryBlock* block : {As<framework::CMemoryBlock>(buffer),
                                                           As<framework::CMemoryBlock>(buffer + 0x10)}) {
                        block->data = nullptr;
                        block->reserved = 0;
                        block->used = 0;
                    }
                    break;
                case 11:  // wide string buffer inside the buffer
                    Link(buffer, 4, 0x20);
                    break;
                default:
                    break;
            }
        };
        std::memcpy(p.b, p.a, kBytes);
        setUp(p.a);
        setUp(p.b);
        std::uint32_t ra = 0, rb = 0;
        bool compareResult = true;
        std::vector<DebugLogCall> logsA, logsB;
        switch (op) {
            case 0:
                ra = original.isvDestroy(p.a, nullptr);
                rb = candidate.isvDestroy(p.b, nullptr);
                break;
            case 1: {
                const bool node = ctx.rng().Chance(1, 2);
                ra = original.isvInsert(p.a, nullptr, Address(p.a + (node ? 0x30 : 0x38)));
                rb = candidate.isvInsert(p.b, nullptr, Address(p.b + (node ? 0x30 : 0x38)));
                if (ra >= Address(p.a) && ra < Address(p.a + kBytes)) {
                    ra -= Address(p.a);
                    rb -= Address(p.b);
                }
                break;
            }
            case 2: {
                const std::uint32_t capacity = ctx.rng().Below(6);
                ra = original.intCtor(p.a, nullptr, capacity, y);
                rb = candidate.intCtor(p.b, nullptr, capacity, y);
                ra = ra == Address(p.a);
                rb = rb == Address(p.b);
                watcom::PtrVector* const va = As<watcom::PtrVector>(p.a);
                watcom::PtrVector* const vb = As<watcom::PtrVector>(p.b);
                compareResult &= S3RE_EXPECT_EQ(va->data == nullptr, vb->data == nullptr);
                va->data = vb->data = nullptr;
                break;
            }
            case 3:
                ra = original.intDtor(p.a, nullptr);
                rb = candidate.intDtor(p.b, nullptr);
                break;
            case 4: {
                auto* outA = reinterpret_cast<std::uint32_t*>(p.a + 8);
                auto* outB = reinterpret_cast<std::uint32_t*>(p.b + 8);
                const std::uint32_t c = ctx.rng().Next32(), d = ctx.rng().Next32();
                ra = original.scale(outA, x, y, c, d) - Address(p.a);
                rb = candidate.scale(outB, x, y, c, d) - Address(p.b);
                break;
            }
            case 5:
                ra = original.handleSet(p.a, nullptr, x, y);
                rb = candidate.handleSet(p.b, nullptr, x, y);
                break;
            case 6: {
                const bool self = ctx.rng().Chance(1, 4);
                ra = original.blockAssign(p.a, nullptr, Address(self ? p.a : p.a + 0x10)) - Address(p.a);
                rb = candidate.blockAssign(p.b, nullptr, Address(self ? p.b : p.b + 0x10)) - Address(p.b);
                break;
            }
            case 7:
                ra = original.registryKey(p.a, nullptr) - Address(p.a);
                rb = candidate.registryKey(p.b, nullptr) - Address(p.b);
                break;
            case 8:
                ra = original.event(p.a, nullptr) - Address(p.a);
                rb = candidate.event(p.b, nullptr) - Address(p.b);
                break;
            case 9:
                ra = original.hasField4(p.a, nullptr);
                rb = candidate.hasField4(p.b, nullptr);
                break;
            case 10: {
                const std::uint32_t count = ctx.rng().Below(9);
                ra = original.copyWords(reinterpret_cast<std::uint16_t*>(p.a + 0x20),
                                        reinterpret_cast<const std::uint16_t*>(p.a + 2), count) - Address(p.a);
                rb = candidate.copyWords(reinterpret_cast<std::uint16_t*>(p.b + 0x20),
                                         reinterpret_cast<const std::uint16_t*>(p.b + 2), count) - Address(p.b);
                break;
            }
            case 11: {
                const std::uint32_t n = ctx.rng().Below(15);
                ra = original.wideEos(p.a, nullptr, n);
                rb = candidate.wideEos(p.b, nullptr, n);
                break;
            }
            case 14:
                ra = original.uintDtor(p.a, nullptr);
                rb = candidate.uintDtor(p.b, nullptr);
                break;
            case 15:
            case 16: {
                std::uint32_t blockA = 0, blockB = 0;
                if (op == 15) {
                    const std::uint32_t capacity = ctx.rng().Below(6);
                    ra = original.intCtorBare(p.a, nullptr, capacity) == Address(p.a);
                    rb = candidate.intCtorBare(p.b, nullptr, capacity) == Address(p.b);
                    blockA = Address(As<watcom::PtrVector>(p.a)->data);
                    blockB = Address(As<watcom::PtrVector>(p.b)->data);
                    As<watcom::PtrVector>(p.a)->data = As<watcom::PtrVector>(p.b)->data = nullptr;
                } else {
                    // Source message: block with `used` heap bytes, four fields.
                    std::uint8_t source[0x24];
                    ctx.rng().Fill(source, sizeof(source));
                    const std::uint32_t used = ctx.rng().Below(20);
                    auto* data = static_cast<std::uint8_t*>(GameNew(used + 1));
                    ctx.rng().Fill(data, used + 1);
                    static_assert(sizeof(source) >= sizeof(CLMMessageBlock));
                    As<CLMMessageBlock>(source)->block.data = data;
                    As<CLMMessageBlock>(source)->block.used = used;
                    ra = original.messageBlockCopy(p.a, nullptr, Address(source)) == Address(p.a);
                    rb = candidate.messageBlockCopy(p.b, nullptr, Address(source)) == Address(p.b);
                    blockA = Address(As<CLMMessageBlock>(p.a)->block.data);
                    blockB = Address(As<CLMMessageBlock>(p.b)->block.data);
                    As<CLMMessageBlock>(p.a)->block.data = As<CLMMessageBlock>(p.b)->block.data = nullptr;
                    if (blockA != 0 && blockB != 0) {
                        compareResult &= S3RE_EXPECT_MEM_EQ(reinterpret_cast<void*>(blockA),
                                                            reinterpret_cast<void*>(blockB), used);
                    }
                }
                compareResult &= S3RE_EXPECT_EQ(blockA == 0, blockB == 0);
                break;
            }
            default: {
                // Graphics object: tables are the buffer itself (both table
                // arrays point at offset 0x10 of a small second buffer).
                static std::uint8_t gfxA[0x700], gfxB[0x700];
                static std::uint8_t tableA[0x40], tableB[0x40];
                ctx.rng().Fill(gfxA, sizeof(gfxA));
                ctx.rng().Fill(tableA, sizeof(tableA));
                const std::uint32_t group = ctx.rng().Below(4);
                const std::uint32_t frame = ctx.rng().Below(6);
                const std::size_t tableField = op == 12 ? offsetof(GfxData, frameTables5ac)
                                                        : offsetof(GfxData, animationTables);
                std::memcpy(gfxB, gfxA, sizeof(gfxA));
                std::memcpy(tableB, tableA, sizeof(tableA));
                const std::uint32_t ta = Address(tableA), tb = Address(tableB);
                std::memcpy(gfxA + tableField + group * 4, &ta, 4);
                std::memcpy(gfxB + tableField + group * 4, &tb, 4);
                const std::uint32_t id = group << 16 | frame;
                {
                    DebugLogRecorder recorder;
                    ra = (op == 12 ? original.framePointer : original.animationPointer)(gfxA, nullptr,
                                                                                       Address(p.a + 8), id);
                    logsA = recorder.Take();
                }
                {
                    DebugLogRecorder recorder;
                    rb = (op == 12 ? candidate.framePointer : candidate.animationPointer)(gfxB, nullptr,
                                                                                         Address(p.b + 8), id);
                    logsB = recorder.Take();
                }
                ra -= Address(p.a);
                rb -= Address(p.b);
                compareResult &= S3RE_EXPECT(logsA.size() == logsB.size());
                for (std::size_t k = 0; compareResult && k < logsA.size(); ++k) {
                    compareResult &= S3RE_EXPECT(logsA[k] == logsB[k]);
                }
                compareResult &= S3RE_EXPECT_MEM_EQ(gfxA, gfxB, tableField);
                break;
            }
        }
        Unlink(p.a);
        Unlink(p.b);
        bool same = compareResult;
        same &= S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(p.a, p.b, kBytes);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, x 0x%x, y 0x%x", i, op, x, y);
            return;
        }
    }
}

std::uint32_t __fastcall WrongInsert(void* self, void* edx, std::uint32_t node) {
    const std::uint32_t r = WCIsvList_Insert(self, edx, node);
    if (As<IsvList>(self)->count > 1) {
        As<IsvList>(self)->last = node;  // wrong: the node becomes the last one
    }
    return r;
}

}  // namespace

S3RE_TEST(FrameworkSmall27_MatchOriginal) {
    CompareSmall(ctx, OurFunctions(), 30000);
}

S3RE_TEST(FrameworkSmall27_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = OurFunctions();
    wrong.isvInsert = &WrongInsert;
    CompareSmall(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
