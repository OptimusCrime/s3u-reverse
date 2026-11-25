// CControl, CButton and CImageButton construction, destruction and timer
// tick, and the CScreen message queue's insert (Dirk\dialog.cpp; layouts in
// re/ui_classes.md and screen_element_layout.h). All thiscall. The classes
// derive virtually from CScreenElement: a constructor gets the object's
// start and a "most derived" flag (only the most derived class builds the
// virtual base and sets the vbtable pointer), destructors and virtual
// members get the CScreenElement part ("this" = object + the class's size).
//
// CControl::CControl (0x00417fe0): (RECT by value, most derived) ret 0x14:
//   most derived: vbptr (+4) = 0x655d2c and CScreenElement(rect) 0x415660 on
//   +0x3c. Timer functor (+8) = 0, disabled (+0xc) = 0, the tooltip text
//   (+0x10) empty (its allocator byte = the low byte of the "most derived"
//   argument: the compiler let the allocator share that stack slot), the
//   tooltip (+0x20) = 0; timeGetTime() called twice, the second result is
//   the last movement time (+0x24). Then the vtables (own 0x655d28, the
//   element's 0x655ca4), the vtordisp before the virtual base (vbtable[1] -
//   0x38) and CScreenElement::SetRect 0x4156e0(rect). EAX = this. The mouse
//   fields (+0x28..+0x34) stay uninitialized.
// CControl::~CControl (0x004180a0): vtables and vtordisp as above; the
//   tooltip deleted through its slot 0 (flags 1); the tooltip text's
//   ~string inlined (as 0x401b60). EAX: callers don't use it.
// CControl::OnTimer (0x004183c0, vtable slot 21, the periodic tick): when
//   the mouse is inside (element +0x1d), the element is on a screen
//   (IsOffScreen 0x415840 false), no tooltip is open and the mouse hasn't
//   moved for 600 ms: ShowTooltip 0x418550 (this = the control). Then, with
//   a timer functor (+8): its slot 0 with the element (0 for a null
//   control) and AL = 1, else AL = 0 (only AL is defined).
// CGroupControl::Tick (0x00429f10, this = the group part's element; the
//   control's at this - 0x7c, the group's vbptr at this - 0xb4): OnTimer on
//   the control, then the group's slot 5 (0x416180, `xor al, al`) on its
//   virtual base; EAX = the group's vbtable address with AL = 0.
// CButton::CButton (0x0041a830): (RECT by value, toggle, most derived) ret
//   0x18: most derived: vbptr = 0x655f04 and CScreenElement() 0x415610 on
//   +0x50. CControl(rect, 0); +0x38..+0x44 = 0 (click functor, the state
//   functor +0x40, command id +0x44), toggle (+0x48) = the argument's bit 0,
//   pressed (+0x49) and checked (+0x4a) = 0; vtables 0x655f00 / 0x655e7c,
//   vtordisp vbtable[1] - 0x4c. EAX = this.
// CButton::~CButton (0x0041a900, this = +0x50): vtables and vtordisp as
//   above; a pressed button with a state functor calls its slot 0 with the
//   element (0 for a null button); then ~CControl (this - 0x14). EAX = its.
// CImageButton::CImageButton (0x0041abe0): (const RECT*, toggle, GFX file,
//   normal, pressed and hover frames, most derived) ret 0x1c: most derived:
//   vbptr = 0x655f98 and CScreenElement() on +0x60; CButton(*rect, toggle,
//   0); the four values at +0x4c..+0x58; vtables 0x655f94 / 0x655f10,
//   vtordisp vbtable[1] - 0x5c. EAX = this.
// WCValSList<MsgConsole>::NewLink (0x00429900): (const message*) ret 4: a
//   0x18-byte link from the list's allocator (+0x10, called with ECX =
//   0x18) or operator new; next = 0, the 5 message dwords copied. EAX = the
//   link (0 when out of memory).
// CScreen::PostMessage (0x00417fa0, vtable slot 31): (message by value) ret
//   0x14: the queue is at this - 0x1c: NewLink, WCIsvList::insert 0x60b430
//   and the queue's last link (+8) = the new link; EAX = insert's. Out of
//   memory: the queue's error handler 0x59f200 (an empty function); EAX =
//   0.
//
// Known difference: the original constructors have SEH frames that destroy
// the members built so far when a callee throws; ours don't (SetRect and
// the base constructors don't throw in practice; the tooltip text is still
// empty then).

#include "game/globals.h"
#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// CButton's own fields (from the object's start; its CControl part first).
struct CButtonFields {
    std::uint8_t control[0x38];     // 0x00 CControl, without the vtordisp
    std::uint32_t click_functor;    // 0x38 the listener notified on a click
    std::uint32_t unk3c;            // 0x3c
    std::uint32_t state_functor;    // 0x40 called when the pressed state changes
    std::uint32_t command_id;       // 0x44 sent as a type-0x40 message
    std::uint8_t toggle;            // 0x48 a toggle button
    std::uint8_t pressed;           // 0x49
    std::uint8_t checked;           // 0x4a
    std::uint8_t pad4b;
};
static_assert(offsetof(CButtonFields, state_functor) == 0x40);
static_assert(offsetof(CButtonFields, toggle) == 0x48);
static_assert(sizeof(CButtonFields) == 0x4c);
// CImageButton's (after the CButton part and its vtordisp).
struct CImageButtonFields {
    std::uint8_t button[0x4c];      // 0x00 CButton
    std::uint32_t file;             // 0x4c GFX file
    std::uint32_t normal_frame;     // 0x50 (-1 = none)
    std::uint32_t pressed_frame;    // 0x54
    std::uint32_t hover_frame;      // 0x58
};
static_assert(sizeof(CImageButtonFields) == 0x5c);
// The message queue of a CScreen (WCQueue<MsgConsole> over a WCValSList).
struct MessageQueue {
    std::uint8_t unk00[8];
    std::uint32_t last;             // 0x08 the last link (WCIsvList)
    std::uint32_t count;            // 0x0c
    std::uint32_t allocator;        // 0x10 a link allocator (ECX = size), 0 = operator new
};
struct MessageLink {
    std::uint32_t next;
    std::uint32_t message[5];
};
static_assert(sizeof(MessageLink) == 0x18);
#pragma pack(pop)

struct Message {
    std::uint32_t words[5];
};

// Where each class's element part (its "this") sits in the object.
constexpr std::uint32_t kControlElement = 0x3c;
constexpr std::uint32_t kButtonElement = 0x50;
constexpr std::uint32_t kImageButtonElement = 0x60;
// CGroupControl, from its group part's element: the control's element and
// the group's vbtable pointer.
constexpr std::uint32_t kGroupControlControl = 0x7c;
constexpr std::uint32_t kGroupControlVbptr = 0xb4;

constexpr std::uint32_t kTooltipDelay = 600;  // ms without mouse movement
constexpr std::uint32_t kLinkBytes = sizeof(MessageLink);

constexpr std::uintptr_t kElementConstruct = 0x00415610;      // CScreenElement::CScreenElement (ours)
constexpr std::uintptr_t kElementConstructRect = 0x00415660;  // CScreenElement::CScreenElement(rect) (ours)
constexpr std::uintptr_t kElementSetRect = 0x004156e0;        // CScreenElement::SetRect (ours)
constexpr std::uintptr_t kIsOffScreen = 0x00415840;           // CScreenElement::IsOffScreen (ours)
constexpr std::uintptr_t kElementSlot5 = 0x00416180;          // CScreenElement::vf05 (`xor al, al`)
constexpr std::uintptr_t kControlConstruct = 0x00417fe0;      // CControl::CControl (ours)
constexpr std::uintptr_t kControlDestroy = 0x004180a0;        // CControl::~CControl (ours)
constexpr std::uintptr_t kControlOnTimer = 0x004183c0;        // CControl::OnTimer (ours)
constexpr std::uintptr_t kShowTooltip = 0x00418550;           // CControl::ShowTooltip (GDI, original)
constexpr std::uintptr_t kButtonConstruct = 0x0041a830;       // CButton::CButton (ours)
constexpr std::uintptr_t kNewLink = 0x00429900;               // WCValSList<MsgConsole>::NewLink (ours)
constexpr std::uintptr_t kQueueOutOfMemory = 0x0059f200;      // the queue's error handler (empty)
constexpr std::uintptr_t kListInsert = 0x0060b430;            // WCIsvList::insert (ours)

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using ControlConstructFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx, RECT rect,
                                                       std::uint32_t mostDerived);
using ButtonConstructFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx, RECT rect,
                                                      std::uint32_t toggle, std::uint32_t mostDerived);
using AllocatorFn = std::uint32_t(__fastcall*)(std::uint32_t size);

template <typename T>
T& Field(std::uintptr_t address) {  // raw-ok: fields around a virtual base, see the header
    return *reinterpret_cast<T*>(address);
}

// The vbtable's displacement of the virtual base, for the vbptr at `vbptr`.
std::int32_t Displacement(std::uintptr_t vbptr) {
    return Field<const std::int32_t*>(vbptr)[1];
}

std::uintptr_t VirtualBase(std::uintptr_t vbptr) {
    return vbptr + Displacement(vbptr);
}

// Stores a class's vtables: its own at the object's start, the element's in
// the virtual base, and the vtordisp just before the base (the base's
// displacement minus the class's own size before it).
void StoreVtables(std::uintptr_t object, std::uint32_t own, std::uint32_t element, std::int32_t classBytes) {
    Field<std::uint32_t>(object) = own;
    Field<std::uint32_t>(VirtualBase(object + 4)) = element;
    const std::int32_t displacement = Displacement(object + 4);
    Field<std::int32_t>(object + displacement) = displacement - (classBytes - 4);
}

std::uint32_t CallSlot0(std::uint32_t object, std::uint32_t argument) {
    const auto* vtable = Field<const Thiscall1*>(object);
    return vtable[0](object, nullptr, argument);
}

}  // namespace

// Purpose: Builds a control (the base of buttons, edit fields and dialogs):
// no tooltip yet, enabled, the movement clock started, placed at `rect`.
std::uintptr_t __fastcall CControl_Construct(std::uintptr_t self, void*, RECT rect, std::uint32_t mostDerived) {
    auto* control = reinterpret_cast<ui::CControl*>(self);
    if (mostDerived != 0) {
        Field<std::uint32_t>(self + 4) = vbtables::kCControl;
        reinterpret_cast<Thiscall1>(kElementConstructRect)(self + kControlElement, nullptr,
                                                           reinterpret_cast<std::uint32_t>(&rect));
    }
    control->timer_functor = 0;
    control->disabled = 0;
    control->tooltip_text.allocator = static_cast<std::uint8_t>(mostDerived);  // (the shared stack slot)
    control->tooltip_text.ptr = nullptr;
    control->tooltip_text.len = 0;
    control->tooltip_text.res = 0;
    control->tooltip = 0;
    g_importTimeGetTime.get()();  // (called twice; the first result is unused)
    control->last_move_time = g_importTimeGetTime.get()();
    StoreVtables(self, vtables::kCControl, vtables::kCControl_element, kControlElement);
    reinterpret_cast<Thiscall1>(kElementSetRect)(VirtualBase(self + 4), nullptr,
                                                 reinterpret_cast<std::uint32_t>(&rect));
    return self;
}

// Purpose: Destroys a control: closes its tooltip and frees the tooltip
// text.
void __fastcall CControl_Destroy(std::uintptr_t self, void*) {
    const std::uintptr_t object = self - kControlElement;
    auto* control = reinterpret_cast<ui::CControl*>(object);
    StoreVtables(object, vtables::kCControl, vtables::kCControl_element, kControlElement);
    if (control->tooltip != 0) {
        CallSlot0(control->tooltip, 1);  // its scalar deleting destructor
    }
    msvc6::String_Destroy(&control->tooltip_text, nullptr);
}

// Purpose: A control's periodic tick: opens the tooltip once the mouse has
// rested on it for 600 ms, and runs its timer functor (animations, blinking
// cursors). Only AL is defined: 1 when a functor ran.
std::uint32_t __fastcall CControl_OnTimer(std::uintptr_t self, void*) {
    const std::uintptr_t object = self - kControlElement;
    auto* control = reinterpret_cast<ui::CControl*>(object);
    const std::uintptr_t element = VirtualBase(object + 4);
    if (reinterpret_cast<const ui::CScreenElement*>(element)->mouse_inside != 0) {
        const auto offScreen = static_cast<std::uint8_t>(reinterpret_cast<Thiscall0>(kIsOffScreen)(element, nullptr));
        if (offScreen == 0 && control->tooltip == 0) {
            const std::uint32_t now = g_importTimeGetTime.get()();
            if (static_cast<std::int32_t>(now - control->last_move_time) >= static_cast<std::int32_t>(kTooltipDelay)) {
                reinterpret_cast<Thiscall0>(kShowTooltip)(object, nullptr);
            }
        }
    }
    if (control->timer_functor == 0) {
        return 0;
    }
    const std::uint32_t argument = object == 0 ? 0 : static_cast<std::uint32_t>(VirtualBase(object + 4));
    return (CallSlot0(control->timer_functor, argument) & ~0xffu) | 1;
}

// Purpose: The periodic tick of a control that is also a group (list boxes,
// dropdowns, scroll bars): the control's tick, then the group's slot 5.
std::uint32_t __fastcall CGroupControl_Tick(std::uintptr_t self, void*) {
    reinterpret_cast<Thiscall0>(kControlOnTimer)(self - kGroupControlControl, nullptr);
    const std::uintptr_t vbptr = self - kGroupControlVbptr;
    reinterpret_cast<Thiscall0>(kElementSlot5)(VirtualBase(vbptr), nullptr);
    // Slot 5 only clears AL; the rest of EAX is the vbtable address the
    // original loaded last.
    return Field<std::uint32_t>(vbptr) & ~0xffu;
}

// Purpose: Builds a push or toggle button (not pressed, no listeners).
std::uintptr_t __fastcall CButton_Construct(std::uintptr_t self, void*, RECT rect, std::uint32_t toggle,
                                            std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        Field<std::uint32_t>(self + 4) = vbtables::kCButton;
        reinterpret_cast<Thiscall0>(kElementConstruct)(self + kButtonElement, nullptr);
    }
    reinterpret_cast<ControlConstructFn>(kControlConstruct)(self, nullptr, rect, 0);
    auto* button = reinterpret_cast<CButtonFields*>(self);
    button->click_functor = 0;
    button->unk3c = 0;
    button->state_functor = 0;
    button->command_id = 0;
    button->toggle = static_cast<std::uint8_t>(toggle & 1);
    button->pressed = 0;
    button->checked = 0;
    StoreVtables(self, vtables::kCButton, vtables::kCButton_element, kButtonElement);
    return self;
}

// Purpose: Destroys a button; a button destroyed while pressed tells its
// state listener first.
std::uint32_t __fastcall CButton_Destroy(std::uintptr_t self, void*) {
    const std::uintptr_t object = self - kButtonElement;
    StoreVtables(object, vtables::kCButton, vtables::kCButton_element, kButtonElement);
    const auto* button = reinterpret_cast<const CButtonFields*>(object);
    if (button->pressed != 0 && button->state_functor != 0) {
        const std::uint32_t argument = object == 0 ? 0 : static_cast<std::uint32_t>(VirtualBase(object + 4));
        CallSlot0(button->state_functor, argument);
    }
    return reinterpret_cast<Thiscall0>(kControlDestroy)(object + kControlElement, nullptr);
}

// Purpose: Builds a button drawn with GFX frames (normal, pressed, hover).
std::uintptr_t __fastcall CImageButton_Construct(std::uintptr_t self, void*, const RECT* rect, std::uint32_t toggle,
                                                 std::uint32_t file, std::uint32_t normal, std::uint32_t pressed,
                                                 std::uint32_t hover, std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        Field<std::uint32_t>(self + 4) = vbtables::kCImageButton;
        reinterpret_cast<Thiscall0>(kElementConstruct)(self + kImageButtonElement, nullptr);
    }
    reinterpret_cast<ButtonConstructFn>(kButtonConstruct)(self, nullptr, *rect, toggle, 0);
    auto* button = reinterpret_cast<CImageButtonFields*>(self);
    button->file = file;
    button->normal_frame = normal;
    button->pressed_frame = pressed;
    button->hover_frame = hover;
    StoreVtables(self, vtables::kCImageButton, vtables::kCImageButton_element, kImageButtonElement);
    return self;
}

// Purpose: Allocates a link of a screen's message queue holding a copy of
// the message.
MessageLink* __fastcall MsgConsoleList_NewLink(MessageQueue* self, void*, const Message* message) {
    MessageLink* link = self->allocator != 0
                            ? reinterpret_cast<MessageLink*>(reinterpret_cast<AllocatorFn>(self->allocator)(kLinkBytes))
                            : static_cast<MessageLink*>(msvc6::GameOperatorNew(kLinkBytes));
    if (link != nullptr) {
        link->next = 0;
        std::memcpy(link->message, message->words, sizeof(link->message));
    }
    return link;
}

// Purpose: Queues a UI message on a screen (handled on its next update).
std::uint32_t __fastcall CScreen_PostMessage(std::uintptr_t self, void*, Message message) {
    const std::uintptr_t queue = self - 0x1c;
    const std::uint32_t link =
        reinterpret_cast<Thiscall1>(kNewLink)(queue, nullptr, reinterpret_cast<std::uint32_t>(&message));
    if (link == 0) {
        reinterpret_cast<Thiscall0>(kQueueOutOfMemory)(queue, nullptr);
        return 0;
    }
    const std::uint32_t result = reinterpret_cast<Thiscall1>(kListInsert)(queue, nullptr, link);
    reinterpret_cast<MessageQueue*>(queue)->last = link;
    return result;
}

using DestroyFn = void(__fastcall*)(std::uintptr_t, void*);
using ImageButtonConstructFn = std::uintptr_t(__fastcall*)(std::uintptr_t, void*, const RECT*, std::uint32_t,
                                                           std::uint32_t, std::uint32_t, std::uint32_t,
                                                           std::uint32_t, std::uint32_t);
using NewLinkFn = MessageLink*(__fastcall*)(MessageQueue*, void*, const Message*);
using PostMessageFn = std::uint32_t(__fastcall*)(std::uintptr_t, void*, Message);

S3RE_HOOK(0x00417fe0, CControl_Construct, ControlConstructFn, Original_CControl_Construct, CControl_Construct);
S3RE_HOOK(0x004180a0, CControl_Destroy, DestroyFn, Original_CControl_Destroy, CControl_Destroy);
S3RE_HOOK(0x004183c0, CControl_OnTimer, Thiscall0, Original_CControl_OnTimer, CControl_OnTimer);
S3RE_HOOK(0x00429f10, CGroupControl_Tick, Thiscall0, Original_CGroupControl_Tick, CGroupControl_Tick);
S3RE_HOOK(0x0041a830, CButton_Construct, ButtonConstructFn, Original_CButton_Construct, CButton_Construct);
S3RE_HOOK(0x0041a900, CButton_Destroy, Thiscall0, Original_CButton_Destroy, CButton_Destroy);
S3RE_HOOK(0x0041abe0, CImageButton_Construct, ImageButtonConstructFn, Original_CImageButton_Construct,
          CImageButton_Construct);
S3RE_HOOK(0x00429900, MsgConsoleList_NewLink, NewLinkFn, Original_MsgConsoleList_NewLink, MsgConsoleList_NewLink);
S3RE_HOOK(0x00417fa0, CScreen_PostMessage, PostMessageFn, Original_CScreen_PostMessage, CScreen_PostMessage);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each case runs the original and ours on the same random 0x2000-byte arena
// (set up alike before each run): objects in it, vbtable pointers to small
// tables in it whose displacement keeps the virtual base inside, the
// callees replaced by recording stubs (pointer arguments into the arena as
// offsets, rect arguments by their contents), virtual slot 0 calls through
// a fake vtable recorded too, timeGetTime from a fake clock. Compared: the
// arena, EAX and the calls. The tooltip text of ~CControl is a real string
// on the game's heap (empty, own or shared buffer).

namespace {

constexpr std::size_t kBytes = 0x2000;
constexpr std::size_t kObject = 0x800;   // the object's start
constexpr std::size_t kTables = 0x1e00;  // vbtables, the fake vtable
constexpr std::size_t kFunctor = 0x1d00; // functor / tooltip objects
constexpr std::size_t kAllocated = 0x1c00;

struct CallLog {
    std::uintptr_t callee;
    std::uint32_t self;
    std::uint32_t args[6];
    std::uint8_t rect[16];
    bool operator==(const CallLog& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

std::uintptr_t g_base = 0;
std::vector<CallLog>* g_log = nullptr;
std::uint32_t g_clock = 0;
std::uint32_t g_allocFails = 0;

std::uint32_t Normalize(std::uint32_t value) {
    return value - g_base < kBytes ? 0xbeef0000u + (value - static_cast<std::uint32_t>(g_base)) : value;
}

std::uint32_t Result(std::uintptr_t callee, std::uint32_t self) {
    return static_cast<std::uint32_t>(callee * 7 + g_log->size() * 0x01010101u) ^ Normalize(self);
}

template <std::size_t>
using Dword = std::uint32_t;

// A thiscall stub with N stack dwords; RectArg >= 0: that argument is a
// rect pointer (recorded by contents, not address).
template <std::uintptr_t Callee, int RectArg, std::size_t... I>
std::uint32_t __fastcall Stub(std::uint32_t self, void*, Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    CallLog entry{Callee, Normalize(self), {}, {}};
    for (std::size_t i = 0; i < sizeof...(I) && i < 6; ++i) {
        entry.args[i] = Normalize(values[i]);
    }
    if constexpr (RectArg >= 0) {
        std::memcpy(entry.rect, reinterpret_cast<const void*>(values[RectArg]), 16);
        entry.args[RectArg] = 0;
    }
    g_log->push_back(entry);
    std::uint32_t result = Result(Callee, self);
    if constexpr (Callee == kNewLink) {
        // A link in the arena (or none), so the insert and the queue see it.
        result = (result & 3) == 0 ? 0 : static_cast<std::uint32_t>(g_base + kAllocated);
    }
    return result;
}

template <std::uintptr_t Callee, int RectArg, std::size_t... I>
void* StubAddress(std::index_sequence<I...>) {
    return reinterpret_cast<void*>(&Stub<Callee, RectArg, I...>);
}

template <std::uintptr_t Callee, int N, int RectArg = -1>
void Install() {
    test::PatchJump(Callee, StubAddress<Callee, RectArg>(std::make_index_sequence<N>{}));
}

constexpr std::uintptr_t kVirtualSlot0 = 1;  // the fake vtable's slot 0 in the log
constexpr std::uintptr_t kAllocatorCall = 2;

std::uint32_t __fastcall FakeSlot0(std::uint32_t self, void*, std::uint32_t argument) {
    g_log->push_back(CallLog{kVirtualSlot0, Normalize(self), {Normalize(argument)}, {}});
    return Result(kVirtualSlot0, self);
}

std::uint32_t __fastcall FakeAllocator(std::uint32_t size) {
    g_log->push_back(CallLog{kAllocatorCall, size, {}, {}});
    return g_allocFails != 0 ? 0 : static_cast<std::uint32_t>(g_base + kAllocated);
}

DWORD WINAPI FakeTimeGetTime() {
    g_clock += 977;
    return g_clock;
}

void InstallStubs() {
    Install<kElementConstruct, 0>();
    Install<kElementConstructRect, 1, 0>();
    Install<kElementSetRect, 1, 0>();
    Install<kIsOffScreen, 0>();
    Install<kControlConstruct, 5>();
    Install<kControlDestroy, 0>();
    Install<kControlOnTimer, 0>();
    Install<kShowTooltip, 0>();
    Install<kButtonConstruct, 6>();
    Install<kNewLink, 1>();
    Install<kListInsert, 1>();
    const void* clock = reinterpret_cast<const void*>(&FakeTimeGetTime);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importTimeGetTime), &clock, sizeof(clock));
}

void RemoveStubs() {
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importTimeGetTime));
    for (std::uintptr_t callee : {kListInsert, kNewLink, kButtonConstruct, kShowTooltip, kControlOnTimer,
                                  kControlDestroy, kControlConstruct, kIsOffScreen, kElementSetRect,
                                  kElementConstructRect, kElementConstruct}) {
        test::UnpatchJump(callee);
    }
}

// The member under test is a callee of others, so its stub is removed
// while it is compared.
struct Unstubbed {
    std::uintptr_t address;
    explicit Unstubbed(std::uintptr_t a) : address(a) {
        if (address != 0) {
            test::UnpatchJump(address);
        }
    }
};

void Put32(std::uint8_t* buffer, std::size_t offset, std::uint32_t value) {
    std::memcpy(buffer + offset, &value, 4);
}

std::uint32_t Address(const std::uint8_t* buffer, std::size_t offset) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + offset));
}

// A vbptr at `offset` pointing at a vbtable (slot k) whose displacement
// lands the virtual base `base` bytes after the vbptr, or a little further
// (never inside the class's own fields: the vtable stores would overwrite
// them, in the original too).
void VbTable(test::Rng& rng, std::uint8_t* buffer, std::size_t offset, std::int32_t base, int k) {
    const std::size_t table = kTables + 8 * k;
    Put32(buffer, offset, Address(buffer, table));
    const std::int32_t displacement = base + 4 * rng.Range(0, 4);
    Put32(buffer, table + 4, static_cast<std::uint32_t>(displacement));
}

// A functor/tooltip object at kFunctor + 0x10 * k whose vtable is the fake
// one (slot 0 = FakeSlot0); returns its address.
std::uint32_t FakeObject(std::uint8_t* buffer, int k) {
    const std::size_t vtable = kTables + 0x100;
    Put32(buffer, vtable, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&FakeSlot0)));
    Put32(buffer, kFunctor + 0x10 * k, Address(buffer, vtable));
    return Address(buffer, kFunctor + 0x10 * k);
}

struct Run {
    std::uint32_t eax;
    std::vector<CallLog> calls;
};

template <typename Setup, typename Call>
bool Compare(test::Context& ctx, const char* name, std::uintptr_t unstub, int cases, Setup setup, Call callOriginal,
             Call callOurs, std::uint32_t eaxMask = 0xffffffffu) {
    static std::uint8_t buffer[kBytes], afterOriginal[kBytes];
    InstallStubs();
    bool ok = true;
    {
        const Unstubbed unstubbed(unstub);
        for (int i = 0; ok && i < cases; ++i) {
            const std::uint64_t seed = ctx.rng().Next64();
            const std::uint32_t clock = ctx.rng().Next32();
            auto run = [&](const Call& call, Run& out) {
                test::Rng rng(seed);
                setup(rng, buffer);
                g_base = reinterpret_cast<std::uintptr_t>(buffer);
                g_log = &out.calls;
                g_clock = clock;
                g_allocFails = static_cast<std::uint32_t>(seed >> 60) & 1;
                out.eax = Normalize(call(buffer, seed) & eaxMask);
                g_log = nullptr;
            };
            Run a{}, b{};
            run(callOriginal, a);
            std::memcpy(afterOriginal, buffer, kBytes);
            run(callOurs, b);
            if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT_EQ(a.calls.size(), b.calls.size()) ||
                !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT_MEM_EQ(afterOriginal, buffer, kBytes)) {
                ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
                ok = false;
            }
        }
    }
    RemoveStubs();
    return ok;
}

using CallFn = std::uint32_t (*)(std::uint8_t*, std::uint64_t);

std::uint32_t Arg(std::uint64_t seed, int k) {
    return test::Rng(seed * 31 + static_cast<std::uint64_t>(k)).Next32();
}

RECT ArgRect(std::uint64_t seed) {
    return RECT{static_cast<LONG>(Arg(seed, 10)), static_cast<LONG>(Arg(seed, 11)), static_cast<LONG>(Arg(seed, 12)),
                static_cast<LONG>(Arg(seed, 13))};
}

std::uint32_t MostDerived(std::uint64_t seed) {
    const std::uint32_t v = Arg(seed, 20);
    return (v & 3) == 0 ? 0 : (v & 3) == 1 ? 1 : v;
}

// The members (ours or wrong ones) under test.
struct Members {
    ControlConstructFn controlConstruct = &CControl_Construct;
    DestroyFn controlDestroy = &CControl_Destroy;
    Thiscall0 onTimer = &CControl_OnTimer;
    Thiscall0 tick = &CGroupControl_Tick;
    ButtonConstructFn buttonConstruct = &CButton_Construct;
    Thiscall0 buttonDestroy = &CButton_Destroy;
    ImageButtonConstructFn imageButtonConstruct = &CImageButton_Construct;
    NewLinkFn newLink = &MsgConsoleList_NewLink;
    PostMessageFn postMessage = &CScreen_PostMessage;
};

const Members* g_members = nullptr;

void RandomArena(test::Rng& rng, std::uint8_t* buffer) {
    rng.Fill(buffer, kBytes);
}

// A constructor's arena: the vbptr (+4) set up for the base at `element`.
template <std::int32_t Element>
void ConstructorArena(test::Rng& rng, std::uint8_t* buffer) {
    RandomArena(rng, buffer);
    VbTable(rng, buffer, kObject + 4, Element - 4, 0);
}

// A control (element at kObject + 0x3c) with mouse_inside, tooltip, the
// last movement time and a timer functor varied.
void ControlArena(test::Rng& rng, std::uint8_t* buffer) {
    RandomArena(rng, buffer);
    VbTable(rng, buffer, kObject + 4, 0x38, 0);
    std::int32_t displacement = 0;
    std::memcpy(&displacement, buffer + kTables + 4, 4);
    const std::size_t element = kObject + 4 + displacement;
    buffer[element + 0x1d] = rng.Chance(1, 4) ? 0 : static_cast<std::uint8_t>(rng.Chance(1, 2) ? 1 : rng.Next32());
    Put32(buffer, kObject + 0x20, rng.Chance(2, 3) ? 0 : FakeObject(buffer, 0));
    Put32(buffer, kObject + 0x08, rng.Chance(1, 3) ? 0 : FakeObject(buffer, 1));
}

// ~CControl's tooltip text: empty, an own buffer or a shared one (count
// 1..0xfe or frozen 0xff), on the game heap.
void ControlStringArena(test::Rng& rng, std::uint8_t* buffer) {
    ControlArena(rng, buffer);
    auto* text = reinterpret_cast<msvc6::String*>(buffer + kObject + 0x10);
    text->ptr = nullptr;
    text->len = 0;
    text->res = 0;
    if (rng.Chance(1, 4)) {
        return;
    }
    const std::uint32_t length = rng.Below(40);
    const std::uint32_t capacity = length + rng.Below(8);
    auto* block = static_cast<char*>(msvc6::GameOperatorNew(capacity + 2));
    const std::uint32_t pick = rng.Below(4);
    block[0] = static_cast<char>(pick == 0 ? 0 : pick == 1 ? 0xff : 1 + rng.Below(0xfd));
    for (std::uint32_t i = 0; i < length; ++i) {
        block[1 + i] = static_cast<char>('a' + rng.Below(26));
    }
    block[1 + length] = 0;
    text->ptr = block + 1;
    text->len = length;
    text->res = capacity;
}

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    g_members = &ours;
    // CControl::CControl: this = the object.
    Compare(
        ctx, "CControl::CControl", kControlConstruct, cases, ConstructorArena<0x3c>,
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(Original_CControl_Construct(reinterpret_cast<std::uintptr_t>(b + kObject),
                                                                          nullptr, ArgRect(s), MostDerived(s)));
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(
                g_members->controlConstruct(reinterpret_cast<std::uintptr_t>(b + kObject), nullptr, ArgRect(s), MostDerived(s)));
        }});
    // ~CControl: the text's buffer is freed (or its count lowered) by both;
    // a fresh copy is set up for each run, and any surviving buffer is
    // compared through the arena (the count byte) and then freed.
    Compare(
        ctx, "CControl::~CControl", kControlDestroy, cases, ControlStringArena,
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            const auto* text = reinterpret_cast<msvc6::String*>(b + kObject + 0x10);
            char* block = text->ptr != nullptr ? text->ptr - 1 : nullptr;
            const std::uint8_t count = block != nullptr ? static_cast<std::uint8_t>(block[0]) : 0;
            Original_CControl_Destroy(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c), nullptr);
            const std::uint32_t after = block != nullptr && count != 0 && count != 0xff ? static_cast<std::uint8_t>(block[0]) : 0x100;
            if (after != 0x100) {
                msvc6::GameOperatorDelete(block);
            }
            return after;
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            const auto* text = reinterpret_cast<msvc6::String*>(b + kObject + 0x10);
            char* block = text->ptr != nullptr ? text->ptr - 1 : nullptr;
            const std::uint8_t count = block != nullptr ? static_cast<std::uint8_t>(block[0]) : 0;
            g_members->controlDestroy(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c), nullptr);
            const std::uint32_t after = block != nullptr && count != 0 && count != 0xff ? static_cast<std::uint8_t>(block[0]) : 0x100;
            if (after != 0x100) {
                msvc6::GameOperatorDelete(block);
            }
            return after;
        }});
    Compare(
        ctx, "CControl::OnTimer", kControlOnTimer, cases, ControlArena,
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return Original_CControl_OnTimer(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c), nullptr);
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return g_members->onTimer(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c), nullptr);
        }},
        0xff);
    // CGroupControl::Tick: this = kObject + 0x3c + 0x7c; the group's vbptr
    // at this - 0xb4.
    Compare(
        ctx, "CGroupControl::Tick", 0, cases,
        [](test::Rng& rng, std::uint8_t* buffer) {
            RandomArena(rng, buffer);
            VbTable(rng, buffer, kObject + 0x3c + 0x7c - 0xb4, 0x100, 1);
        },
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return Original_CGroupControl_Tick(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c + 0x7c), nullptr);
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return g_members->tick(reinterpret_cast<std::uintptr_t>(b + kObject + 0x3c + 0x7c), nullptr);
        }});
    Compare(
        ctx, "CButton::CButton", kButtonConstruct, cases, ConstructorArena<0x50>,
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(Original_CButton_Construct(reinterpret_cast<std::uintptr_t>(b + kObject),
                                                                         nullptr, ArgRect(s), Arg(s, 1), MostDerived(s)));
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(g_members->buttonConstruct(reinterpret_cast<std::uintptr_t>(b + kObject),
                                                                         nullptr, ArgRect(s), Arg(s, 1), MostDerived(s)));
        }});
    Compare(
        ctx, "CButton::~CButton", 0, cases,
        [](test::Rng& rng, std::uint8_t* buffer) {
            RandomArena(rng, buffer);
            VbTable(rng, buffer, kObject + 4, 0x4c, 0);
            buffer[kObject + 0x49] = rng.Chance(1, 3) ? 0 : static_cast<std::uint8_t>(1 + rng.Below(255));
            Put32(buffer, kObject + 0x40, rng.Chance(1, 3) ? 0 : FakeObject(buffer, 2));
        },
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return Original_CButton_Destroy(reinterpret_cast<std::uintptr_t>(b + kObject + 0x50), nullptr);
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            return g_members->buttonDestroy(reinterpret_cast<std::uintptr_t>(b + kObject + 0x50), nullptr);
        }});
    // CImageButton: the rect argument points into the arena.
    Compare(
        ctx, "CImageButton::CImageButton", 0, cases, ConstructorArena<0x60>,
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(Original_CImageButton_Construct(
                reinterpret_cast<std::uintptr_t>(b + kObject), nullptr, reinterpret_cast<const RECT*>(b + 0x100),
                Arg(s, 1), Arg(s, 2), Arg(s, 3), Arg(s, 4), Arg(s, 5), MostDerived(s)));
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            return static_cast<std::uint32_t>(g_members->imageButtonConstruct(
                reinterpret_cast<std::uintptr_t>(b + kObject), nullptr, reinterpret_cast<const RECT*>(b + 0x100),
                Arg(s, 1), Arg(s, 2), Arg(s, 3), Arg(s, 4), Arg(s, 5), MostDerived(s)));
        }});
    // NewLink: the list's allocator (fake, sometimes failing) or operator
    // new (that link's contents are compared, then freed).
    Compare(
        ctx, "WCValSList<MsgConsole>::NewLink", kNewLink, cases,
        [](test::Rng& rng, std::uint8_t* buffer) {
            RandomArena(rng, buffer);
            Put32(buffer, kObject + 0x10,
                  rng.Chance(1, 2) ? 0 : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&FakeAllocator)));
        },
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            MessageLink* link = Original_MsgConsoleList_NewLink(reinterpret_cast<MessageQueue*>(b + kObject), nullptr,
                                                                reinterpret_cast<const Message*>(b + 0x100));
            return Normalize(reinterpret_cast<std::uint32_t>(link)) == reinterpret_cast<std::uint32_t>(link) &&
                           link != nullptr
                       ? [&] {
                             std::memcpy(b + 0x1b00, link, sizeof(*link));
                             msvc6::GameOperatorDelete(link);
                             return 0x12345678u;
                         }()
                       : reinterpret_cast<std::uint32_t>(link);
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t) {
            MessageLink* link = g_members->newLink(reinterpret_cast<MessageQueue*>(b + kObject), nullptr,
                                                   reinterpret_cast<const Message*>(b + 0x100));
            return Normalize(reinterpret_cast<std::uint32_t>(link)) == reinterpret_cast<std::uint32_t>(link) &&
                           link != nullptr
                       ? [&] {
                             std::memcpy(b + 0x1b00, link, sizeof(*link));
                             msvc6::GameOperatorDelete(link);
                             return 0x12345678u;
                         }()
                       : reinterpret_cast<std::uint32_t>(link);
        }});
    Compare(
        ctx, "CScreen::PostMessage", 0, cases, RandomArena,
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            const Message m{{Arg(s, 0), Arg(s, 1), Arg(s, 2), Arg(s, 3), Arg(s, 4)}};
            return Original_CScreen_PostMessage(reinterpret_cast<std::uintptr_t>(b + kObject), nullptr, m);
        }},
        CallFn{[](std::uint8_t* b, std::uint64_t s) {
            const Message m{{Arg(s, 0), Arg(s, 1), Arg(s, 2), Arg(s, 3), Arg(s, 4)}};
            return g_members->postMessage(reinterpret_cast<std::uintptr_t>(b + kObject), nullptr, m);
        }});
}

// Wrong implementations.
std::uintptr_t __fastcall WrongControlConstruct(std::uintptr_t self, void* edx, RECT rect, std::uint32_t mostDerived) {
    CControl_Construct(self, edx, rect, mostDerived);
    reinterpret_cast<ui::CControl*>(self)->tooltip_text.allocator = 0;  // the allocator byte is the argument's
    return self;
}
std::uint32_t __fastcall WrongOnTimer(std::uintptr_t self, void*) {
    // Ignores the tooltip delay.
    const std::uintptr_t object = self - kControlElement;
    auto* control = reinterpret_cast<ui::CControl*>(object);
    if (reinterpret_cast<const ui::CScreenElement*>(VirtualBase(object + 4))->mouse_inside != 0 &&
        control->tooltip == 0) {
        reinterpret_cast<Thiscall0>(kShowTooltip)(object, nullptr);
    }
    if (control->timer_functor == 0) {
        return 0;
    }
    return CallSlot0(control->timer_functor, static_cast<std::uint32_t>(VirtualBase(object + 4))) | 1;
}
std::uint32_t __fastcall WrongButtonDestroy(std::uintptr_t self, void*) {
    // Forgets the state listener.
    const std::uintptr_t object = self - kButtonElement;
    StoreVtables(object, vtables::kCButton, vtables::kCButton_element, kButtonElement);
    return reinterpret_cast<Thiscall0>(kControlDestroy)(object + kControlElement, nullptr);
}

}  // namespace

S3RE_TEST(UiControls_MatchOriginal) {
    CompareAll(ctx, Members{}, 400);
}

S3RE_TEST(UiControls_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 3; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.controlConstruct = &WrongControlConstruct;
        } else if (k == 1) {
            wrong.onTimer = &WrongOnTimer;
        } else {
            wrong.buttonDestroy = &WrongButtonDestroy;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 60);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
