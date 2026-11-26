// DirectX::CGameDrawer members (DirectX\Gamebase.cpp): the CDirectDraw the
// game draws with keeps every surface it made in a WCPtrSList (+0x10, so a
// lost display restores them all), its palettes in another (+0x28), the
// device and display-mode lists, and the primary surface (+0x78). Also
// DirectX::CDisplayMode and CGameDrawThread::RequestFullRedraw.
// Layout: framework::CGameDrawer (framework_layout.h); its own virtual
// slots used here: 2 (0x08) Destroy, 10 (0x28) set up the display, 11
// (0x2c) create the surfaces (bool). The lists' slots: 1 (0x04) destroy a
// link, 2 (0x08) compare two elements (pointers to them).
//
// DirectX::CDisplayMode::CDisplayMode (0x005b6c30): (desc) ret 4: a copy of
//   the DDSURFACEDESC (0x6c bytes), or zeroes without one. EAX = this.
// DirectX::CDisplayMode::IsEqual (0x005b6c60): (other) ret 4: the 0x6c
//   bytes are equal (EAX 1 / 0).
// ReturnThis (0x005b6c20): EAX = ECX (CreateOnDevice converts its device
//   argument with it).
// CGameDrawer::Initialize (0x005b6ed0): (window) ret 4: +0xc = window;
//   DDDirectDrawEnumerate(EnumDevicesCallback 0x5b7430, this); slot 10;
//   slot 11 (0). EAX = slot 11's result.
// CGameDrawer::CreateOnDevice (0x005b6f60) (device) / Create (0x005b6f90)
//   (GUID): Connect, then DDEnumDisplayModes(0, 0, this, the display-mode
//   callback 0x5b7810). EAX = 0.
// CGameDrawer::Destroy (0x005b6fc0) (slot 2): Disconnect; frees the
//   display-mode array (data, capacity, entries = 0; +0x10 kept only when
//   negative). EAX: what operator delete left (unused).
// CGameDrawer::AddSurface (0x005b7000): (surface) ret 4: appends it to the
//   surface list (a new link: insert, then it becomes the last); without a
//   link the error hook. EAX: insert's result, or 0.
// CGameDrawer::SetPrimarySurface (0x005b7040): (primary) ret 4: appends its
//   CSurface part (via the vbptr; null stays null) like AddSurface, +0x78 =
//   primary, g_primarySurfaceRect = the CSurface's RECT (read through the
//   vbptr again: a null primary would crash). EAX = rect.left.
// CGameDrawer::RemoveSurface (0x005b70c0): (surface) ret 4: finds the
//   surface's index with the list's compare slot (-1 if absent) and removes
//   that link (get(-1) removes the first link: a surface that isn't listed
//   takes the first one with it, as in the original); if it is the primary
//   surface's CSurface part, +0x78 and g_primarySurfaceRect are cleared.
//   EAX: 0 then, otherwise that CSurface part (or 0).
// SetMemberByte7CToOne (0x005b7160): (unused) ret 4: +0x7c = 1; EAX as the
//   caller had it.
// CGameDrawer::ReleaseSurfaces (0x005b7170) (slot 5; argument unused):
//   Release (slot 1) of every listed surface except the primary's CSurface
//   part, then of the primary's; +0x78 and the rect cleared; every link
//   taken off (get(0)), destroyed (list slot 1) and its surface deleted
//   (slot 0 with 1); clearAndDestroy; +0x7c = 0. EAX = 0.
// WCListIterBase::WCListIterBase (0x005b72b0): (list) ret 4: +4 = 0, +8 =
//   list, +0xc = 0, +0x10 = 1, vtable 0x65cb44. EAX = this.
// CGameDrawer::RestoreSurfaces (0x005b72e0) (slot 4): slot 2 (RestoreIfLost)
//   of every listed surface (an iterator over the palettes is built and not
//   used). EAX = 0.
// CGameDrawer::ReleasePalettes (0x005b7390) (slot 6; argument unused):
//   CPalette::Release of every listed palette. EAX = 0.
// CGameDrawThread::RequestFullRedraw (0x005b6730): +0x50 = 1 (the next
//   frame is drawn completely).
//
// The walks use stack iterators as the original does (vtables 0x65cb3c /
// 0x65cb4c, state 1 from the WCListIterBase constructor) and call the list
// code (watcom/isv_slist.cpp) through its S3.EXE addresses.

#include "game/framework/directx_drawer.h"

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using framework::CGameDrawer;
using framework::CSurface;
using framework::PtrSList;
using framework::PtrSListIter;

constexpr std::uintptr_t kDDDirectDrawEnumerate = 0x005beb90;
constexpr std::uintptr_t kConnect = 0x005bec00;
constexpr std::uintptr_t kDisconnect = 0x005bec50;
constexpr std::uintptr_t kDDEnumDisplayModes = 0x005bed10;
constexpr std::uintptr_t kPaletteRelease = 0x005bfd60;
constexpr std::uintptr_t kReturnThis = 0x005b6c20;
constexpr std::uintptr_t kListIterConstruct = 0x005b72b0;
constexpr std::uintptr_t kListNext = 0x0060b400;
constexpr std::uintptr_t kListInsert = 0x0060b430;
constexpr std::uintptr_t kListGet = 0x0060b500;
constexpr std::uintptr_t kListClearAndDestroy = 0x0060b560;
constexpr std::uintptr_t kListIterAdvance = 0x0060b5f0;
constexpr std::uintptr_t kListNewLink = 0x00463b50;
// Callbacks the drawer hands to DirectDraw.
constexpr std::uintptr_t kEnumDevicesCallback = 0x005b7430;
constexpr std::uintptr_t kEnumModesCallback = 0x005b7810;

constexpr std::uint32_t kDisplayModeBytes = 0x6c;  // a DDSURFACEDESC

// Virtual slots (byte offsets).
constexpr std::uint32_t kSlotDelete = 0x00;
constexpr std::uint32_t kSlotRelease = 0x04;         // CSurface slot 1
constexpr std::uint32_t kSlotRestoreIfLost = 0x08;   // CSurface slot 2
constexpr std::uint32_t kSlotSetUpDisplay = 0x28;    // CGameDrawer slot 10
constexpr std::uint32_t kSlotCreateSurfaces = 0x2c;  // CGameDrawer slot 11
constexpr std::uint32_t kListSlotDestroyLink = 0x04;
constexpr std::uint32_t kListSlotEquivalent = 0x08;

// DirectX::CGameDrawThread's flag (re/directx_framework.md): Step passes it
// to slot 13 (draw a frame) as "full redraw".
#pragma pack(push, 1)
struct GameDrawThreadFields {
    std::uint8_t unk00[0x50];
    std::uint32_t fullRedraw;  // +0x50
};
#pragma pack(pop)
static_assert(offsetof(GameDrawThreadFields, fullRedraw) == 0x50);

// A WCPtrSList link: next, the element pointer.
struct Link {
    Link* next;
    void* element;
};

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

template <typename Fn>
Fn Method(const void* object, std::uint32_t offset) {
    return *reinterpret_cast<Fn const*>(*static_cast<const std::uint8_t* const*>(object) + offset);
}

template <typename Fn>
Fn ViaGame(std::uintptr_t address, Fn /*replacement*/) {
    return reinterpret_cast<Fn>(address);
}

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThisArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ThisPtrFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* a);
using ThisPtrsFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* a, const void* b);

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

CGameDrawer* AsDrawer(void* self) { return static_cast<CGameDrawer*>(self); }

// The CSurface part of an object with a virtual CSurface base (the primary
// surface): vbtable[1] from its vbptr.
CSurface* SurfacePart(void* object) {
    const auto* const vbtable = reinterpret_cast<const std::int32_t*>(
        static_cast<std::uintptr_t>(*static_cast<const std::uint32_t*>(object)));
    return reinterpret_cast<CSurface*>(static_cast<std::uint8_t*>(object) + vbtable[1]);
}

CSurface* PrimaryPartOrNull(const CGameDrawer* drawer) {
    return drawer->primary != nullptr ? SurfacePart(drawer->primary) : nullptr;
}

// The list code (watcom/isv_slist.cpp).
Link* ListNext(PtrSList* list, Link* link, std::uint32_t reset) {
    using Fn = Link*(__fastcall*)(PtrSList* self, void* edx, Link* link, std::uint32_t reset);
    return CallAt<Fn>(kListNext)(list, nullptr, link, reset);
}
Link* ListGet(PtrSList* list, std::int32_t index) {
    using Fn = Link*(__fastcall*)(PtrSList* self, void* edx, std::int32_t index);
    return CallAt<Fn>(kListGet)(list, nullptr, index);
}
std::uint32_t ListClearAndDestroy(PtrSList* list) {
    return CallAt<ThisFn>(kListClearAndDestroy)(list, nullptr);
}
bool Advance(PtrSListIter* iterator) {
    using Fn = void*(__fastcall*)(PtrSListIter* self, void* edx, std::int32_t steps);
    return CallAt<Fn>(kListIterAdvance)(iterator, nullptr, 1) != nullptr;
}
// The iterator's current element (the library's error hook and null
// without a list or a current link).
void* Current(const PtrSListIter& iterator) {
    if (iterator.list != nullptr && iterator.current != nullptr) {
        return static_cast<const Link*>(iterator.current)->element;
    }
    return nullptr;
}
void DestroyLink(PtrSList* list, Link* link) {
    Method<ThisPtrFn>(list, kListSlotDestroyLink)(list, nullptr, link);
}

// A stack iterator as the walks build it: the WCListIterBase constructor,
// then the derived iterator's vtable.
PtrSListIter MakeIterator(PtrSList* list, std::uint32_t vtable) {
    PtrSListIter iterator;
    using Fn = void*(__fastcall*)(PtrSListIter* self, void* edx, PtrSList* list);
    CallAt<Fn>(kListIterConstruct)(&iterator, nullptr, list);
    iterator.vtable = vtable;
    return iterator;
}

// AddSurface's and SetPrimarySurface's append: a new link for `element`,
// inserted and made the last; without one the error hook. Returns insert's
// result or 0.
std::uint32_t Append(PtrSList* list, void* element) {
    using NewLinkFn = Link*(__fastcall*)(PtrSList* self, void* edx, void* const* value);
    Link* const link = CallAt<NewLinkFn>(kListNewLink)(list, nullptr, &element);
    if (link == nullptr) {
        return 0;  // (error hook)
    }
    const std::uint32_t result = CallAt<ThisPtrFn>(kListInsert)(list, nullptr, link);
    list->last = link;
    return result;
}

// Takes every link off the list, destroys it and deletes its element
// (ReleaseSurfaces, and the destructor).
void DeleteAllElements(PtrSList* list) {
    while (list->last != nullptr) {
        Link* const link = ListGet(list, 0);
        if (link == nullptr) {
            continue;
        }
        void* const element = link->element;
        DestroyLink(list, link);
        if (element != nullptr) {
            using DeleteFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
            Method<DeleteFn>(element, kSlotDelete)(element, nullptr, 1);
        }
    }
}

void ClearPrimaryRect() {
    for (std::int32_t& value : g_primarySurfaceRect.get()) {
        value = 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// DirectX::CDisplayMode, ReturnThis
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005b6c30, DirectX_CDisplayMode_Construct, decltype(&DirectX_CDisplayMode_Construct),
          Original_DirectX_CDisplayMode_Construct, DirectX_CDisplayMode_Construct);

// Purpose: A display mode record (a DirectDraw surface description), as the
// drawer collects them from DDEnumDisplayModes.
void* __fastcall DirectX_CDisplayMode_Construct(void* self, void* /*edx*/, const void* desc) {
    if (desc != nullptr) {
        std::memcpy(self, desc, kDisplayModeBytes);
    } else {
        std::memset(self, 0, kDisplayModeBytes);
    }
    return self;
}

S3RE_HOOK(0x005b6c60, DirectX_CDisplayMode_IsEqual, decltype(&DirectX_CDisplayMode_IsEqual),
          Original_DirectX_CDisplayMode_IsEqual, DirectX_CDisplayMode_IsEqual);

// Purpose: Compares two display modes (their whole descriptions).
std::uint32_t __fastcall DirectX_CDisplayMode_IsEqual(const void* self, void* /*edx*/, const void* other) {
    return std::memcmp(self, other, kDisplayModeBytes) == 0 ? 1 : 0;
}

S3RE_HOOK(0x005b6c20, ReturnThis, decltype(&ReturnThis), Original_ReturnThis, ReturnThis);

// Purpose: Returns its object (a conversion operator the compiler kept as a
// function; CreateOnDevice turns a device into its GUID with it).
void* __fastcall ReturnThis(void* self, void* /*edx*/) {
    return self;
}

// ---------------------------------------------------------------------------
// DirectX::CGameDrawer
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005b6ed0, DirectX_CGameDrawer_Initialize, decltype(&DirectX_CGameDrawer_Initialize),
          Original_DirectX_CGameDrawer_Initialize, DirectX_CGameDrawer_Initialize);

// Purpose: Starts the drawer for the game window: lists the DirectDraw
// devices, sets up the display mode (slot 10) and creates the surfaces
// (slot 11).
std::uint32_t __fastcall DirectX_CGameDrawer_Initialize(void* self, void* /*edx*/, void* window) {
    AsDrawer(self)->window = window;
    using EnumerateFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uintptr_t callback, void* context);
    CallAt<EnumerateFn>(kDDDirectDrawEnumerate)(self, nullptr, kEnumDevicesCallback, self);
    Method<ThisFn>(self, kSlotSetUpDisplay)(self, nullptr);
    return Method<ThisArgFn>(self, kSlotCreateSurfaces)(self, nullptr, 0);
}

namespace {

// Create's and CreateOnDevice's common part.
std::uint32_t ConnectAndListModes(void* self, const void* device) {
    CallAt<ThisPtrFn>(kConnect)(self, nullptr, device);
    using EnumFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t flags, void* desc, void* context,
                                              std::uintptr_t callback);
    return CallAt<EnumFn>(kDDEnumDisplayModes)(self, nullptr, 0, nullptr, self, kEnumModesCallback);
}

}  // namespace

S3RE_HOOK(0x005b6f60, DirectX_CGameDrawer_CreateOnDevice, decltype(&DirectX_CGameDrawer_CreateOnDevice),
          Original_DirectX_CGameDrawer_CreateOnDevice, DirectX_CGameDrawer_CreateOnDevice);

// Purpose: Connects to DirectDraw on a chosen device and collects its
// display modes.
std::uint32_t __fastcall DirectX_CGameDrawer_CreateOnDevice(void* self, void* /*edx*/, void* device) {
    void* const guid = reinterpret_cast<void*>(CallAt<ThisFn>(kReturnThis)(device, nullptr));
    return ConnectAndListModes(self, guid);
}

S3RE_HOOK(0x005b6f90, DirectX_CGameDrawer_Create, decltype(&DirectX_CGameDrawer_Create),
          Original_DirectX_CGameDrawer_Create, DirectX_CGameDrawer_Create);

// Purpose: Connects to DirectDraw (a device GUID or the default) and
// collects the display modes.
std::uint32_t __fastcall DirectX_CGameDrawer_Create(void* self, void* /*edx*/, const void* guid) {
    return ConnectAndListModes(self, guid);
}

S3RE_HOOK(0x005b6fc0, DirectX_CGameDrawer_Destroy, decltype(&DirectX_CGameDrawer_Destroy),
          Original_DirectX_CGameDrawer_Destroy, DirectX_CGameDrawer_Destroy);

// Purpose: Disconnects from DirectDraw and forgets the display modes.
std::uint32_t __fastcall DirectX_CGameDrawer_Destroy(void* self, void* /*edx*/) {
    CallAt<ThisFn>(kDisconnect)(self, nullptr);
    framework::ValOrderedVector& modes = AsDrawer(self)->modes;
    const std::int32_t kept = modes.unk10 < 0 ? modes.unk10 : 0;
    msvc6::GameOperatorDelete(modes.data);
    modes.unk10 = kept;
    modes.data = nullptr;
    modes.capacity = 0;
    modes.entries = 0;
    return 0;
}

S3RE_HOOK(0x005b7000, DirectX_CGameDrawer_AddSurface, decltype(&DirectX_CGameDrawer_AddSurface),
          Original_DirectX_CGameDrawer_AddSurface, DirectX_CGameDrawer_AddSurface);

// Purpose: Registers a surface with the drawer, so it is restored and
// released with the others.
std::uint32_t __fastcall DirectX_CGameDrawer_AddSurface(void* self, void* /*edx*/, void* surface) {
    return Append(&AsDrawer(self)->surfaces, surface);
}

S3RE_HOOK(0x005b7040, DirectX_CGameDrawer_SetPrimarySurface, decltype(&DirectX_CGameDrawer_SetPrimarySurface),
          Original_DirectX_CGameDrawer_SetPrimarySurface, DirectX_CGameDrawer_SetPrimarySurface);

// Purpose: Registers the primary (screen) surface and keeps its size as the
// screen rectangle.
std::uint32_t __fastcall DirectX_CGameDrawer_SetPrimarySurface(void* self, void* /*edx*/, void* primary) {
    CGameDrawer* const drawer = AsDrawer(self);
    Append(&drawer->surfaces, primary != nullptr ? SurfacePart(primary) : nullptr);
    drawer->primary = primary;
    const std::int32_t* const rect = SurfacePart(primary)->rect;
    std::int32_t* const screen = g_primarySurfaceRect.get();
    screen[0] = rect[0];
    screen[3] = rect[3];
    screen[1] = rect[1];
    screen[2] = rect[2];
    return static_cast<std::uint32_t>(rect[0]);
}

S3RE_HOOK(0x005b70c0, DirectX_CGameDrawer_RemoveSurface, decltype(&DirectX_CGameDrawer_RemoveSurface),
          Original_DirectX_CGameDrawer_RemoveSurface, DirectX_CGameDrawer_RemoveSurface);

// Purpose: Unregisters a surface (when it is deleted); forgets the primary
// surface if it was that one.
std::uint32_t __fastcall DirectX_CGameDrawer_RemoveSurface(void* self, void* /*edx*/, void* surface) {
    CGameDrawer* const drawer = AsDrawer(self);
    PtrSList* const list = &drawer->surfaces;
    std::int32_t index = 0;
    Link* link = ListNext(list, nullptr, 1);
    for (;; ++index) {
        if (link == nullptr) {
            index = -1;
            break;
        }
        if (Method<ThisPtrsFn>(list, kListSlotEquivalent)(list, nullptr, &link->element, &surface) != 0) {
            break;
        }
        link = ListNext(list, link, 0);
    }
    if (Link* const removed = ListGet(list, index); removed != nullptr) {
        DestroyLink(list, removed);
    }
    CSurface* const primary = PrimaryPartOrNull(drawer);
    if (surface != primary) {
        return Address(primary);
    }
    drawer->primary = nullptr;
    ClearPrimaryRect();
    return 0;
}

S3RE_HOOK(0x005b7160, SetMemberByte7CToOne, decltype(&SetMemberByte7CToOne), Original_SetMemberByte7CToOne,
          SetMemberByte7CToOne);

// Purpose: Marks the drawer's surfaces as made (the base version of the
// "create surfaces" slot).
std::uint32_t __fastcall SetMemberByte7CToOne(void* self, void* /*edx*/, std::uint32_t /*unused*/) {
    AsDrawer(self)->surfacesMade = 1;
    return 0;  // the original leaves EAX as the caller had it
}

S3RE_HOOK(0x005b7170, DirectX_CGameDrawer_ReleaseSurfaces, decltype(&DirectX_CGameDrawer_ReleaseSurfaces),
          Original_DirectX_CGameDrawer_ReleaseSurfaces, DirectX_CGameDrawer_ReleaseSurfaces);

// Purpose: Releases and deletes every surface of the drawer (before a
// display mode change or at the end), the primary surface last.
std::uint32_t __fastcall DirectX_CGameDrawer_ReleaseSurfaces(void* self, void* /*edx*/, std::uint32_t /*unused*/) {
    CGameDrawer* const drawer = AsDrawer(self);
    PtrSListIter iterator = MakeIterator(&drawer->surfaces, vtables::kSurfaceListIter);
    while (Advance(&iterator)) {
        const CSurface* const primary = PrimaryPartOrNull(drawer);
        void* const surface = Current(iterator);
        if (surface != primary) {
            Method<ThisFn>(Current(iterator), kSlotRelease)(Current(iterator), nullptr);
        }
    }
    if (drawer->primary != nullptr) {
        CSurface* const primary = SurfacePart(drawer->primary);
        Method<ThisFn>(primary, kSlotRelease)(primary, nullptr);
    }
    drawer->primary = nullptr;
    ClearPrimaryRect();
    DeleteAllElements(&drawer->surfaces);
    const std::uint32_t result = ListClearAndDestroy(&drawer->surfaces);
    drawer->surfacesMade = 0;
    return result;
}

S3RE_HOOK(0x005b72b0, WCListIterBase_Construct, decltype(&WCListIterBase_Construct), Original_WCListIterBase_Construct,
          WCListIterBase_Construct);

// Purpose: Constructor of a Watcom list iterator (before the first
// element).
void* __fastcall WCListIterBase_Construct(void* self, void* /*edx*/, void* list) {
    auto* const iterator = static_cast<PtrSListIter*>(self);
    iterator->unk04 = 0;
    iterator->list = static_cast<PtrSList*>(list);
    iterator->current = nullptr;
    iterator->state = 1;
    iterator->vtable = vtables::kWCListIterBase;
    return self;
}

S3RE_HOOK(0x005b72e0, DirectX_CGameDrawer_RestoreSurfaces, decltype(&DirectX_CGameDrawer_RestoreSurfaces),
          Original_DirectX_CGameDrawer_RestoreSurfaces, DirectX_CGameDrawer_RestoreSurfaces);

// Purpose: Restores every surface DirectDraw lost (after Alt+Tab or a mode
// change).
std::uint32_t __fastcall DirectX_CGameDrawer_RestoreSurfaces(void* self, void* /*edx*/) {
    CGameDrawer* const drawer = AsDrawer(self);
    PtrSListIter surfaces = MakeIterator(&drawer->surfaces, vtables::kSurfaceListIter);
    // The original also builds an iterator over the palettes (unused).
    PtrSListIter palettes = MakeIterator(&drawer->palettes, vtables::kPaletteListIter);
    (void)palettes;
    while (Advance(&surfaces)) {
        void* const surface = Current(surfaces);
        Method<ThisFn>(surface, kSlotRestoreIfLost)(surface, nullptr);
    }
    return 0;
}

S3RE_HOOK(0x005b7390, DirectX_CGameDrawer_ReleasePalettes, decltype(&DirectX_CGameDrawer_ReleasePalettes),
          Original_DirectX_CGameDrawer_ReleasePalettes, DirectX_CGameDrawer_ReleasePalettes);

// Purpose: Releases the DirectDraw palette of every palette the drawer
// keeps.
std::uint32_t __fastcall DirectX_CGameDrawer_ReleasePalettes(void* self, void* /*edx*/, std::uint32_t /*unused*/) {
    CGameDrawer* const drawer = AsDrawer(self);
    // The original builds this iterator inline (the same fields as the
    // WCListIterBase constructor's).
    PtrSListIter iterator{vtables::kPaletteListIter, 0, &drawer->palettes, nullptr, 1};
    while (Advance(&iterator)) {
        CallAt<ThisFn>(kPaletteRelease)(Current(iterator), nullptr);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DirectX::CGameDrawThread
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005b6730, DirectX_CGameDrawThread_RequestFullRedraw,
          decltype(&DirectX_CGameDrawThread_RequestFullRedraw), Original_DirectX_CGameDrawThread_RequestFullRedraw,
          DirectX_CGameDrawThread_RequestFullRedraw);

// Purpose: Asks the draw thread to draw the next frame completely (after a
// display mode change).
std::uint32_t __fastcall DirectX_CGameDrawThread_RequestFullRedraw(void* self, void* /*edx*/) {
    static_cast<GameDrawThreadFields*>(self)->fullRedraw = 1;
    return 0;  // the original leaves EAX as the caller had it
}

}  // namespace s3re::game
