// Small members of the container controls (Dirk\dialog.cpp): CScreenGroup's
// destructor and ShowChild, CListBox's layout, the CListView column and
// line records, CImageButton's mouse handlers, CDialog::SetParent.
//
// Classes with a virtual CScreenElement base get, as "this", the address of
// some part of the object; the fields are given relative to it (as in
// ui_control_members.cpp), with the vbptr that leads to the element.
//
// CScreenGroup::~CScreenGroup (0x00416da0, this = the element part, the
//   group's own part 0x80 before it): stores the group's vtable 0x655b70,
//   the element's 0x655aec (through the vbptr this-0x7c) and the vtordisp
//   (displacement - 0x7c, just before the element); then, while the children
//   vector (this-0x74, length this-0x60) is not empty, RemoveChild (slot 1 of
//   the vtable just stored, 0x416fe0) of entry [IndexCheck(0)]; then the
//   destructors of the three Watcom lists (this-0x1c: WCValListBase<bool>
//   0x655bec -> WCIsvSListBase<bool> 0x655be0; this-0x34 and this-0x4c:
//   WCValListBase<CScreenElement*> 0x655bd0 -> 0x655bc4): when the list has
//   entries (+8), the exception hook NoOp 0x59f200 and clearAndDestroy
//   0x60b560; then ~WCIsvListBase 0x60b5a0; and the children vector's
//   (WCBareVectorBase<CScreenElement*> 0x655bb8: with a block, NoOp and
//   operator delete(data); then WCExcept 0x655438). EAX = operator delete's,
//   or 0 without a block.
// CScreenGroup::ShowChild (0x00417090, this = the group's own part): (show,
//   child) ret 8: show: unless the child is in the children vector (this+0xc;
//   its find, slot 0xc (&child, 0, &index), when the vector isn't empty),
//   AddChild (slot 0) it; hide: when it is in there, RemoveChild (slot 1).
//   EAX = the find's (found while showing), AddChild's / RemoveChild's, or
//   0 (hidden, not found).
// CListBox::UpdateLayout (0x0041f550, this = the list box's start): takes the
//   scroll bar (+0xc0, its element through the vbptr +0xc4) out of the group
//   (+0x38; when the children vector +0x44 finds it), then sets the text
//   rectangle +0x3fc..+0x408 from the element's rectangle (vbptr +4) inset by
//   4 (16 on the right when the scroll bar is allowed, +0x3f1, and the items
//   +0x434 don't fit the visible lines +0x414, unsigned); in that case the
//   scroll bar is added again (AddChild). EAX = AddChild's, or the element's
//   displacement (vbtable[1]).
// CListBox::Clear (0x0041f1e0): selection +0x418 = -1, the items (+0x420,
//   WCValVector<String>::resize(0) 0x40a580, length +0x434 = 0); unless the
//   range is fixed (+0x3f0) the range +0x40c = the item count (0); the scroll
//   bar's SetRange 0x41e380(0, range, page +0x414); UpdateLayout. EAX = its.
// CListBox::OnResized (0x004204d0, this = the element, the list box 0x444
//   before it): (rect, delta) ret 8: text top = rect top + 2, bottom = rect
//   bottom - 2, visible lines +0x414 = (bottom - top - 1) / line height
//   +0x3f4 (signed); UpdateLayout. EAX = its.
// CListView::CColumn::CColumn (0x004274a0): index -1, the other seven dwords
//   0. EAX = this.
// CListView::CLine::CLine (0x004276b0): +0 = -1, +4, +8, +0x10 = 0 (+0xc is
//   left alone). EAX = this.
// CListView::CColumn::~CColumn (0x004275e0): the header label +0x1c (when
//   set) deleted through its slot 0 (scalar deleting destructor, flag 1);
//   +0x1c = 0. EAX = the destructor's, or the caller's (unused).
// CListView::CLine::~CLine (0x004277d0): the same for the line's group
//   +0x10, reached through its vbptr (+4) at its element. EAX unused.
// CImageButton::OnMouseEnter (0x0041acf0) / OnMouseLeave (0x0041ad30)
//   (this = the element, the image button 0x60 before it): (message) ret
//   0x14: unless the hover image this-8 is -1, the element's Invalidate
//   (slot 0x1c; through the vbptr this-0x5c); then CControl::OnMouseEnter
//   0x4184b0 (this-0x24) / CButton::OnMouseLeave 0x41ab70 (this-0x10) with
//   the message. EAX = theirs.
// CDialog::SetParent (0x00427d20, this = the element, vbptr this-0xc0):
//   (parent) ret 4: CScreenElement::SetParent 0x4157d0 on the element, then
//   (the vbptr re-read) the element's SetFocus (slot 0xc) with this-0x64
//   (probably the dialog's default control). EAX = SetFocus's.

#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "watcom/ptr_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields around a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

struct Message {
    std::uint32_t words[5];
};
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

constexpr std::uintptr_t kRemoveChild = 0x00416fe0;      // CScreenGroup::RemoveChild (slot 1 of 0x655b70)
constexpr std::uintptr_t kIndexCheck = 0x00428ee0;       // watcom::PtrVector::IndexCheck (ours)
constexpr std::uintptr_t kNoOp = 0x0059f200;             // the containers' exception hook (`ret`)
constexpr std::uintptr_t kClearAndDestroy = 0x0060b560;  // WCIsvSListBase::clearAndDestroy (ours)
constexpr std::uintptr_t kIsvListDestroy = 0x0060b5a0;   // WCIsvListBase::~WCIsvListBase (ours)
constexpr std::uintptr_t kStringVectorResize = 0x0040a580;  // WCValVector<String>::resize (ours)
constexpr std::uintptr_t kScrollSetRange = 0x0041e380;      // CVScrollBar::SetRange (ours)
constexpr std::uintptr_t kUpdateLayout = 0x0041f550;        // CListBox::UpdateLayout (ours)
constexpr std::uintptr_t kControlOnMouseEnter = 0x004184b0;  // CControl::OnMouseEnter (ours)
constexpr std::uintptr_t kButtonOnMouseLeave = 0x0041ab70;   // CButton::OnMouseLeave (ours)
constexpr std::uintptr_t kElementSetParent = 0x004157d0;     // CScreenElement::SetParent (ours)


constexpr std::uint32_t kVectorFindSlot = 0x0c;  // the children vector's find(&value, start, &index)
constexpr std::uint32_t kDeletingDestructorFlag = 1;
constexpr std::int32_t kNoHoverImage = -1;
constexpr std::int32_t kTextInset = 4;            // the list box's text rectangle inside its frame
constexpr std::int32_t kScrollBarSpace = 16;      // right inset with the scroll bar shown
constexpr std::int32_t kResizedMargin = 2;        // OnResized's vertical margin

std::uint32_t VirtualAt(std::uintptr_t object, std::uint32_t slotOffset) {
    return *reinterpret_cast<const std::uint32_t*>(Field<std::uint32_t>(object, 0) + slotOffset);
}

// The element reached through the vbptr at `vbptrAt`: vbptrAt + vbtable[1].
std::uintptr_t ElementThrough(std::uintptr_t vbptrAt) {
    return vbptrAt + reinterpret_cast<const std::int32_t*>(Field<std::uint32_t>(vbptrAt, 0))[1];
}

// The children vector's find (slot 0xc) of `value`; nonzero when found.
std::uint32_t FindChild(std::uintptr_t vector, std::uint32_t value) {
    std::uint32_t index = 0;
    std::uint32_t key = value;
    return reinterpret_cast<Thiscall3>(VirtualAt(vector, kVectorFindSlot))(
        vector, nullptr, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&key)), 0,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&index)));
}

}  // namespace

std::uint32_t __fastcall CScreenGroup_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CScreenGroup_ShowChild(std::uintptr_t self, void* edx, std::uint32_t show,
                                               std::uint32_t child);
std::uint32_t __fastcall CListBox_UpdateLayout(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CListBox_Clear(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CListBox_OnResized(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta);
std::uintptr_t __fastcall CColumn_ConstructDefault(std::uintptr_t self, void* edx);
std::uintptr_t __fastcall CLine_ConstructDefault(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CColumn_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CLine_Destroy(std::uintptr_t self, void* edx);
std::uint32_t __fastcall CImageButton_OnMouseEnter(std::uintptr_t self, void* edx, Message message);
std::uint32_t __fastcall CImageButton_OnMouseLeave(std::uintptr_t self, void* edx, Message message);
std::uint32_t __fastcall CDialog_SetParent(std::uintptr_t self, void* edx, std::uint32_t parent);

using Thiscall0Fn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using ShowChildFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t show, std::uint32_t child);
using OnResizedFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, const RECT* rect, std::uint32_t delta);
using ConstructFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx);
using SetParentFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t parent);

S3RE_HOOK(0x00416da0, CScreenGroup_Destroy, Thiscall0Fn, Original_CScreenGroup_Destroy, CScreenGroup_Destroy);
S3RE_HOOK(0x00417090, CScreenGroup_ShowChild, ShowChildFn, Original_CScreenGroup_ShowChild, CScreenGroup_ShowChild);
S3RE_HOOK(0x0041f550, CListBox_UpdateLayout, Thiscall0Fn, Original_CListBox_UpdateLayout, CListBox_UpdateLayout);
S3RE_HOOK(0x0041f1e0, CListBox_Clear, Thiscall0Fn, Original_CListBox_Clear, CListBox_Clear);
S3RE_HOOK(0x004204d0, CListBox_OnResized, OnResizedFn, Original_CListBox_OnResized, CListBox_OnResized);
S3RE_HOOK(0x004274a0, CColumn_ConstructDefault, ConstructFn, Original_CColumn_ConstructDefault,
          CColumn_ConstructDefault);
S3RE_HOOK(0x004276b0, CLine_ConstructDefault, ConstructFn, Original_CLine_ConstructDefault, CLine_ConstructDefault);
S3RE_HOOK(0x004275e0, CColumn_Destroy, Thiscall0Fn, Original_CColumn_Destroy, CColumn_Destroy);
S3RE_HOOK(0x004277d0, CLine_Destroy, Thiscall0Fn, Original_CLine_Destroy, CLine_Destroy);
S3RE_HOOK(0x0041acf0, CImageButton_OnMouseEnter, MessageFn, Original_CImageButton_OnMouseEnter,
          CImageButton_OnMouseEnter);
S3RE_HOOK(0x0041ad30, CImageButton_OnMouseLeave, MessageFn, Original_CImageButton_OnMouseLeave,
          CImageButton_OnMouseLeave);
S3RE_HOOK(0x00427d20, CDialog_SetParent, SetParentFn, Original_CDialog_SetParent, CDialog_SetParent);

namespace {

// One Watcom list member's destructor (0x416e18..0x416ebe).
void DestroyWatcomList(std::uintptr_t list, std::uint32_t derivedVtable, std::uint32_t baseVtable) {
    Field<std::uint32_t>(list, 0) = derivedVtable;
    if (Field<std::uint32_t>(list, 8) != 0) {
        reinterpret_cast<Thiscall0>(kNoOp)(list, nullptr);
        reinterpret_cast<Thiscall0>(kClearAndDestroy)(list, nullptr);
    }
    Field<std::uint32_t>(list, 0) = baseVtable;
    reinterpret_cast<Thiscall0>(kIsvListDestroy)(list, nullptr);
}

}  // namespace

// Purpose: Destructor of a container of UI elements (dialogs, screens,
// grouped controls): takes every child out, then frees its lists.
std::uint32_t __fastcall CScreenGroup_Destroy(std::uintptr_t self, void* /*edx*/) {
    const std::uintptr_t group = self - ui::kGroupThis;
    Field<std::uint32_t>(group, 0) = vtables::kCScreenGroup;
    const std::uintptr_t vbptrAt = self - 0x7c;
    Field<std::uint32_t>(ElementThrough(vbptrAt), 0) = vtables::kCScreenGroup_element;
    const std::int32_t displacement = reinterpret_cast<const std::int32_t*>(Field<std::uint32_t>(vbptrAt, 0))[1];
    Field<std::int32_t>(group, displacement) = displacement - 0x7c;  // the vtordisp before the element

    auto& children = Field<watcom::PtrVector>(self, -0x74);
    while (children.length != 0) {
        const std::uint32_t index =
            reinterpret_cast<watcom::IndexFn>(kIndexCheck)(&children, nullptr, 0);
        const std::uint32_t child = children.data[index];
        reinterpret_cast<Thiscall1>(VirtualAt(group, ui::group_slot::kRemoveChild))(group, nullptr, child);
    }

    DestroyWatcomList(self - 0x1c, vtables::kWCValListBase_bool, vtables::kWCIsvSListBase_bool);
    DestroyWatcomList(self - 0x34, vtables::kWCValListBase_CScreenElementP, vtables::kWCIsvSListBase_CScreenElementP);
    DestroyWatcomList(self - 0x4c, vtables::kWCValListBase_CScreenElementP, vtables::kWCIsvSListBase_CScreenElementP);

    // The children vector (0x416ec3..0x416ef6).
    const auto vector = reinterpret_cast<std::uintptr_t>(&children);
    children.vtable = vtables::kWCBareVectorBase_CScreenElementP;
    std::uint32_t eax = 0;
    if (children.data != nullptr) {
        reinterpret_cast<Thiscall0>(kNoOp)(vector, nullptr);
        eax = msvc6::GameOperatorDelete(children.data);
    }
    children.vtable = vtables::kWCExcept;
    return eax;
}

// Purpose: Shows or hides an element in a container: adds it as a child
// unless it is one already, or removes it.
std::uint32_t __fastcall CScreenGroup_ShowChild(std::uintptr_t self, void* /*edx*/, std::uint32_t show,
                                               std::uint32_t child) {
    const auto& children = Field<watcom::PtrVector>(self, 0x0c);
    const auto vector = reinterpret_cast<std::uintptr_t>(&children);
    if (show != 0) {
        if (children.length != 0) {
            if (const std::uint32_t found = FindChild(vector, child); found != 0) {
                return found;
            }
        }
        return reinterpret_cast<Thiscall1>(VirtualAt(self, ui::group_slot::kAddChild))(self, nullptr, child);
    }
    if (children.length == 0 || FindChild(vector, child) == 0) {
        return 0;
    }
    return reinterpret_cast<Thiscall1>(VirtualAt(self, ui::group_slot::kRemoveChild))(self, nullptr, child);
}

// Purpose: Lays out a list box: the text area inside its frame, with the
// scroll bar shown only when the items don't fit.
std::uint32_t __fastcall CListBox_UpdateLayout(std::uintptr_t self, void* /*edx*/) {
    const auto scrollBar = static_cast<std::uint32_t>(ElementThrough(self + 0xc4));
    const std::uintptr_t group = self + 0x38;
    if (Field<watcom::PtrVector>(self, 0x44).length != 0 && FindChild(self + 0x44, scrollBar) != 0) {
        reinterpret_cast<Thiscall1>(VirtualAt(group, ui::group_slot::kRemoveChild))(group, nullptr, scrollBar);
    }
    // The element's rectangle (through the vbptr +4, re-read per field as
    // the original does).
    auto elementRect = [self](std::int32_t field) {
        const std::int32_t displacement = reinterpret_cast<const std::int32_t*>(Field<std::uint32_t>(self, 4))[1];
        return Field<std::int32_t>(self, displacement + 4 + 8 + field);
    };
    const bool scrollBarShown =
        Field<std::uint8_t>(self, 0x3f1) != 0 && Field<std::uint32_t>(self, 0x434) > Field<std::uint32_t>(self, 0x414);
    Field<std::int32_t>(self, 0x3fc) = elementRect(0) + kTextInset;
    Field<std::int32_t>(self, 0x400) = elementRect(4) + kTextInset;
    Field<std::int32_t>(self, 0x404) = elementRect(8) - (scrollBarShown ? kScrollBarSpace : kTextInset);
    Field<std::int32_t>(self, 0x408) = elementRect(12) - kTextInset;
    if (scrollBarShown) {
        return reinterpret_cast<Thiscall1>(VirtualAt(group, ui::group_slot::kAddChild))(group, nullptr, scrollBar);
    }
    return static_cast<std::uint32_t>(reinterpret_cast<const std::int32_t*>(Field<std::uint32_t>(self, 4))[1]);
}

// Purpose: Removes all items of a list box.
std::uint32_t __fastcall CListBox_Clear(std::uintptr_t self, void* /*edx*/) {
    Field<std::int32_t>(self, 0x418) = -1;  // no selection
    reinterpret_cast<Thiscall1>(kStringVectorResize)(self + 0x420, nullptr, 0);
    Field<std::uint32_t>(self, 0x434) = 0;
    if (Field<std::uint8_t>(self, 0x3f0) == 0) {
        Field<std::uint32_t>(self, 0x40c) = Field<std::uint32_t>(self, 0x434);
    }
    reinterpret_cast<Thiscall3>(kScrollSetRange)(self + 0xc0, nullptr, 0, Field<std::uint32_t>(self, 0x40c),
                                                 Field<std::uint32_t>(self, 0x414));
    return reinterpret_cast<Thiscall0>(kUpdateLayout)(self, nullptr);
}

// Purpose: Reacts to a list box's new size: how many lines fit, then the
// layout.
std::uint32_t __fastcall CListBox_OnResized(std::uintptr_t self, void* /*edx*/, const RECT* rect,
                                           std::uint32_t /*delta*/) {
    const std::uintptr_t box = self - 0x444;
    Field<std::int32_t>(box, 0x400) = rect->top + kResizedMargin;
    Field<std::int32_t>(box, 0x408) = rect->bottom - kResizedMargin;
    const auto height = static_cast<std::int32_t>(Field<std::uint32_t>(box, 0x408) - Field<std::uint32_t>(box, 0x400) - 1);
    Field<std::int32_t>(box, 0x414) = height / Field<std::int32_t>(box, 0x3f4);
    return reinterpret_cast<Thiscall0>(kUpdateLayout)(box, nullptr);
}

// Purpose: Default constructor of a list view column record (no index).
std::uintptr_t __fastcall CColumn_ConstructDefault(std::uintptr_t self, void* /*edx*/) {
    Field<std::int32_t>(self, 0) = -1;
    for (std::int32_t offset = 4; offset < 0x20; offset += 4) {
        Field<std::uint32_t>(self, offset) = 0;
    }
    return self;
}

// Purpose: Default constructor of a list view line record (no index).
std::uintptr_t __fastcall CLine_ConstructDefault(std::uintptr_t self, void* /*edx*/) {
    Field<std::int32_t>(self, 0) = -1;
    Field<std::uint32_t>(self, 4) = 0;
    Field<std::uint32_t>(self, 8) = 0;
    Field<std::uint32_t>(self, 0x10) = 0;
    return self;
}

// Purpose: Destructor of a list view column record: deletes its header
// label.
std::uint32_t __fastcall CColumn_Destroy(std::uintptr_t self, void* /*edx*/) {
    std::uint32_t eax = 0;
    if (const std::uint32_t header = Field<std::uint32_t>(self, 0x1c); header != 0) {
        eax = reinterpret_cast<Thiscall1>(VirtualAt(header, ui::element_slot::kDeletingDestructor))(
            header, nullptr, kDeletingDestructorFlag);
    }
    Field<std::uint32_t>(self, 0x1c) = 0;
    return eax;
}

// Purpose: Destructor of a list view line record: deletes the group that
// holds its cells.
std::uint32_t __fastcall CLine_Destroy(std::uintptr_t self, void* /*edx*/) {
    std::uint32_t eax = 0;
    if (const std::uint32_t cells = Field<std::uint32_t>(self, 0x10); cells != 0) {
        const std::uintptr_t element = ElementThrough(cells + 4);
        eax = reinterpret_cast<Thiscall1>(VirtualAt(element, ui::element_slot::kDeletingDestructor))(
            element, nullptr, kDeletingDestructorFlag);
    }
    Field<std::uint32_t>(self, 0x10) = 0;
    return eax;
}

namespace {

// The image buttons' hover redraw: unless there is no hover image.
void InvalidateIfHoverImage(std::uintptr_t self) {
    if (Field<std::int32_t>(self, -8) != kNoHoverImage) {
        const std::uintptr_t element = ElementThrough(self - 0x5c);
        reinterpret_cast<Thiscall0>(VirtualAt(element, ui::element_slot::kInvalidate))(element, nullptr);
    }
}

}  // namespace

// Purpose: The mouse moves onto an image button: redraw it (for the hover
// image), then the control's handling (tooltip timer).
std::uint32_t __fastcall CImageButton_OnMouseEnter(std::uintptr_t self, void* /*edx*/, Message message) {
    InvalidateIfHoverImage(self);
    return reinterpret_cast<MessageFn>(kControlOnMouseEnter)(self - 0x24, nullptr, message);
}

// Purpose: The mouse leaves an image button: redraw it, then the button's
// handling.
std::uint32_t __fastcall CImageButton_OnMouseLeave(std::uintptr_t self, void* /*edx*/, Message message) {
    InvalidateIfHoverImage(self);
    return reinterpret_cast<MessageFn>(kButtonOnMouseLeave)(self - 0x10, nullptr, message);
}

// Purpose: Attaches a dialog to its parent, then gives the focus to its
// default control (probably).
std::uint32_t __fastcall CDialog_SetParent(std::uintptr_t self, void* /*edx*/, std::uint32_t parent) {
    reinterpret_cast<Thiscall1>(kElementSetParent)(ElementThrough(self - 0xc0), nullptr, parent);
    const std::uintptr_t element = ElementThrough(self - 0xc0);
    return reinterpret_cast<Thiscall1>(VirtualAt(element, ui::element_slot::kSetFocus))(
        element, nullptr, Field<std::uint32_t>(self, -0x64));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each member runs, original and ours, on one random 0x1800-byte buffer set
// up alike before each (so pointers into it need no translation): `this`
// 0x800 bytes in, vbtables in a table area, objects' vtables pointing at a
// fake vtable whose slots record their calls (ECX and arguments as offsets
// into the buffer; pointer arguments outside it, such as the find's stack
// key, by the value they point at) and return values fixed by the call. The
// S3.EXE callees are CalleeStubs (recorded; RemoveChild takes the last
// child out so the destructor's loop ends). Compared: EAX, the buffer, the
// calls.

namespace {

constexpr std::size_t kBytes = 0x1800;
constexpr std::size_t kThis = 0x800;
constexpr std::size_t kTable = 0x1700;

struct Call {
    std::uint32_t what;
    std::uint32_t self;
    std::uint32_t args[5];
    bool operator==(const Call& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

std::uint8_t g_buffer[kBytes];
std::vector<Call>* g_calls = nullptr;

std::uint32_t Rel(std::uint32_t value) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_buffer));
    return value - base < kBytes ? 0xbeef0000u + (value - base) : value;
}

template <std::size_t>
using Dword = std::uint32_t;

// A fake vtable slot with sizeof...(I) stack arguments.
template <std::uint32_t Slot, std::size_t... I>
std::uint32_t __fastcall SlotStub(std::uint32_t self, void*, Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    Call call{0x5100 + Slot, Rel(self), {}};
    for (std::size_t i = 0; i < sizeof...(I); ++i) {
        call.args[i] = Rel(values[i]);
    }
    if constexpr (Slot == kVectorFindSlot) {
        // find(&key, start, &index): the key by value (a stack slot of each).
        call.args[0] = Rel(*reinterpret_cast<const std::uint32_t*>(values[0]));
        call.args[2] = *reinterpret_cast<const std::uint32_t*>(values[2]);
    }
    g_calls->push_back(call);
    const std::uint32_t h = (Slot * 7919u + static_cast<std::uint32_t>(g_calls->size()) * 31u) ^ call.self;
    return (h & 3) == 0 ? 0 : h;
}

template <std::uint32_t Slot, std::size_t... I>
std::uintptr_t SlotAddress(std::index_sequence<I...>) {
    return reinterpret_cast<std::uintptr_t>(&SlotStub<Slot, I...>);
}

std::uintptr_t g_fakeVtable[32];

void FillFakeVtable() {
    for (auto& slot : g_fakeVtable) {
        slot = 0;
    }
    g_fakeVtable[0] = SlotAddress<0x00>(std::make_index_sequence<1>{});  // AddChild / deleting destructor
    g_fakeVtable[1] = SlotAddress<0x04>(std::make_index_sequence<1>{});  // RemoveChild
    g_fakeVtable[3] = SlotAddress<0x0c>(std::make_index_sequence<3>{});  // find / SetFocus (1 argument: see below)
    g_fakeVtable[7] = SlotAddress<0x1c>(std::make_index_sequence<0>{});  // Invalidate
}

// SetFocus (slot 0xc of an element) takes one argument: CDialog's test
// uses this vtable instead.
std::uintptr_t g_focusVtable[32];

std::uint32_t FakeVtable() {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_fakeVtable));
}

void Link(std::size_t offset, std::size_t target) {
    const auto p = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_buffer + target));
    std::memcpy(g_buffer + offset, &p, 4);
}

void Put(std::size_t offset, std::uint32_t value) {
    std::memcpy(g_buffer + offset, &value, 4);
}

// A vbptr at buffer offset `at` with its table at kTable + slot and the
// displacement `displacement`.
void VbPtr(std::size_t at, std::size_t slot, std::int32_t displacement) {
    Link(at, kTable + slot);
    std::memcpy(g_buffer + kTable + slot + 4, &displacement, 4);
}

struct Run {
    std::uint32_t eax;
    std::vector<Call> calls;
    std::vector<test::StubCall> stubCalls;
};

template <typename Setup, typename Calls>
bool CompareCases(test::Context& ctx, const char* name, int cases, Setup setup, Calls calls,
                  void (*install)(test::CalleeStubs&)) {
    static std::uint8_t afterOriginal[kBytes];
    FillFakeVtable();
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        Run runs[2];
        {
            test::CalleeStubs stubs;
            install(stubs);
            stubs.SetWorld(reinterpret_cast<CGameData*>(g_buffer));
            for (int side = 0; side < 2; ++side) {
                test::Rng rng(seed);
                setup(rng);
                g_calls = &runs[side].calls;
                runs[side].eax = Rel(calls(side == 0, reinterpret_cast<std::uintptr_t>(g_buffer + kThis), seed));
                runs[side].stubCalls = stubs.Take();
                g_calls = nullptr;
                if (side == 0) {
                    std::memcpy(afterOriginal, g_buffer, kBytes);
                }
            }
        }
        if (!S3RE_EXPECT_EQ(runs[0].eax, runs[1].eax) || !S3RE_EXPECT(runs[0].calls == runs[1].calls) ||
            !S3RE_EXPECT(runs[0].stubCalls == runs[1].stubCalls) || !S3RE_EXPECT_MEM_EQ(afterOriginal, g_buffer, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
            return false;
        }
    }
    return true;
}

void NoStubs(test::CalleeStubs&) {}

// operator delete (cdecl): recorded, nothing freed (the blocks are in the
// buffer).
std::uint32_t __cdecl DeleteRecorder(void* block) {
    g_calls->push_back(Call{0xde1e, Rel(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block))), {}});
    return 0xde1e7e00u;
}

std::uint32_t Fixed(CGameData*, std::uint32_t self, const std::uint32_t* args) {
    return (self * 3) ^ args[0] ^ 0x51ab0000u;
}

// The members (a wrong one replaces ours for the harness check).
struct Members {
    Thiscall0Fn groupDestroy = &CScreenGroup_Destroy;
    ShowChildFn showChild = &CScreenGroup_ShowChild;
    Thiscall0Fn updateLayout = &CListBox_UpdateLayout;
    Thiscall0Fn listClear = &CListBox_Clear;
    OnResizedFn listResized = &CListBox_OnResized;
    ConstructFn columnConstruct = &CColumn_ConstructDefault;
    ConstructFn lineConstruct = &CLine_ConstructDefault;
    Thiscall0Fn columnDestroy = &CColumn_Destroy;
    Thiscall0Fn lineDestroy = &CLine_Destroy;
    MessageFn mouseEnter = &CImageButton_OnMouseEnter;
    MessageFn mouseLeave = &CImageButton_OnMouseLeave;
    SetParentFn setParent = &CDialog_SetParent;
};

Message MessageFrom(std::uint64_t seed) {
    test::Rng rng(seed ^ 0x3e55a6e);
    Message m{};
    rng.Fill(&m, sizeof(m));
    return m;
}

bool CompareAll(test::Context& ctx, const Members& ours, int cases) {
    bool ok = true;

    // ~CScreenGroup: this = kThis, vbptr this-0x7c (element at this, +0..12),
    // children at this-0x74 with 0..4 entries in the buffer.
    ok &= CompareCases(
        ctx, "~CScreenGroup", cases,
        [](test::Rng& rng) {
            rng.Fill(g_buffer, kBytes);
            VbPtr(kThis - 0x7c, 0, 0x7c + 4 * static_cast<std::int32_t>(rng.Below(4)));
            const std::size_t children = kThis - 0x74;
            Link(children + 8, 0x100);
            Put(children + 0xc, 1 + rng.Below(6));
            Put(children + 0x10, rng.Below(6));
            Put(children + 0x14, rng.Below(5));
            if (rng.Chance(1, 5)) {
                Put(children + 8, 0);
                Put(children + 0x14, 0);
            }
            for (std::size_t list : {kThis - 0x1c, kThis - 0x34, kThis - 0x4c}) {
                Put(list + 8, rng.Chance(1, 2) ? 0 : rng.Next32());
            }
        },
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            test::ScopedJump deleteJump(msvc6::kOperatorDelete, reinterpret_cast<const void*>(&DeleteRecorder));
            return original ? Original_CScreenGroup_Destroy(self, nullptr) : ours.groupDestroy(self, nullptr);
        },
        [](test::CalleeStubs& stubs) {
            // RemoveChild: takes the last child out (length - 1).
            stubs.Install<1, kRemoveChild>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
                std::uint8_t* buffer = g_buffer;  // the stubs' world
                std::uint32_t length = 0;
                std::memcpy(&length, buffer + kThis - 0x60, 4);
                --length;
                std::memcpy(buffer + kThis - 0x60, &length, 4);
                return args[0] ^ 0x4e40u;
            });
            stubs.Install<0, kClearAndDestroy>();
            stubs.Install<0, kIsvListDestroy>();
            stubs.Install<0, kNoOp>();
        });

    // ShowChild: this = kThis (the group's own part), vtable fake, children
    // vector at +0xc (its vtable fake, length 0..2).
    ok &= CompareCases(
        ctx, "CScreenGroup::ShowChild", cases,
        [](test::Rng& rng) {
            rng.Fill(g_buffer, kBytes);
            Put(kThis, FakeVtable());
            Put(kThis + 0xc, FakeVtable());
            Put(kThis + 0xc + 0x14, rng.Below(3));
        },
        [&](bool original, std::uintptr_t self, std::uint64_t seed) {
            const std::uint32_t show = (seed & 1) != 0 ? 0 : static_cast<std::uint32_t>(seed >> 8);
            const std::uint32_t child = static_cast<std::uint32_t>(seed >> 32);
            return original ? Original_CScreenGroup_ShowChild(self, nullptr, show, child)
                            : ours.showChild(self, nullptr, show, child);
        },
        NoStubs);

    // CListBox: this = kThis; vbptrs +4 (element +0x444) and +0xc4 (scroll
    // bar element); group +0x38 and its children vector +0x44 with fake
    // vtables; counts around the visible lines.
    auto listSetup = [](test::Rng& rng) {
        rng.Fill(g_buffer, kBytes);
        VbPtr(kThis + 4, 0, 0x440 + 4 * static_cast<std::int32_t>(rng.Below(3)));
        VbPtr(kThis + 0xc4, 8, 0x200 + 4 * static_cast<std::int32_t>(rng.Below(3)));
        Put(kThis + 0x38, FakeVtable());
        Put(kThis + 0x44, FakeVtable());
        Put(kThis + 0x44 + 0x14, rng.Below(3));
        g_buffer[kThis + 0x3f0] = static_cast<std::uint8_t>(rng.Below(2) * rng.Below(256));
        g_buffer[kThis + 0x3f1] = static_cast<std::uint8_t>(rng.Below(2) * rng.Below(256));
        const std::uint32_t visible = rng.Chance(1, 4) ? rng.Next32() : rng.Below(10);
        Put(kThis + 0x414, visible);
        Put(kThis + 0x434, rng.Chance(1, 4) ? rng.Next32() : visible + rng.Below(5) - 2);
        Put(kThis + 0x3f4, rng.Chance(1, 4) ? (rng.Next32() & 0x7fffffffu) | 1 : 1 + rng.Below(30));
    };
    ok &= CompareCases(
        ctx, "CListBox::UpdateLayout", cases, listSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            return original ? Original_CListBox_UpdateLayout(self, nullptr) : ours.updateLayout(self, nullptr);
        },
        NoStubs);
    ok &= CompareCases(
        ctx, "CListBox::Clear", cases, listSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            return original ? Original_CListBox_Clear(self, nullptr) : ours.listClear(self, nullptr);
        },
        [](test::CalleeStubs& stubs) {
            stubs.Install<1, kStringVectorResize>(&Fixed);
            stubs.Install<3, kScrollSetRange>(&Fixed);
            stubs.Install<0, kUpdateLayout>([](CGameData*, std::uint32_t self, const std::uint32_t*) {
                return self ^ 0x1a70u;
            });
        });
    ok &= CompareCases(
        ctx, "CListBox::OnResized", cases, listSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t seed) {
            test::Rng rng(seed ^ 0x5eed);
            RECT rect{};
            rng.Fill(&rect, sizeof(rect));
            if (rng.Chance(1, 2)) {
                rect.top = rng.Range(-100, 600);
                rect.bottom = rect.top + rng.Range(-20, 400);
            }
            const std::uint32_t delta = rng.Next32();
            return original ? Original_CListBox_OnResized(self + 0x444, nullptr, &rect, delta)
                            : ours.listResized(self + 0x444, nullptr, &rect, delta);
        },
        [](test::CalleeStubs& stubs) {
            stubs.Install<0, kUpdateLayout>([](CGameData*, std::uint32_t self, const std::uint32_t*) {
                return self ^ 0x1a70u;
            });
        });

    // The column and line records: this = kThis; the header label / the
    // cells group (vbptr +4, element +0x40..) in the buffer, or none.
    auto recordSetup = [](test::Rng& rng) {
        rng.Fill(g_buffer, kBytes);
        Link(kThis + 0x1c, 0x300);
        Put(0x300, FakeVtable());
        Link(kThis + 0x10, 0x400);
        VbPtr(0x404, 16, 0x40 + 4 * static_cast<std::int32_t>(rng.Below(3)));
        for (std::size_t element : {0x444u, 0x448u, 0x44cu}) {
            Put(element, FakeVtable());
        }
        if (rng.Chance(1, 4)) {
            Put(kThis + 0x1c, 0);
        }
        if (rng.Chance(1, 4)) {
            Put(kThis + 0x10, 0);
        }
    };
    ok &= CompareCases(
        ctx, "CColumn::CColumn", cases, recordSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            return static_cast<std::uint32_t>(original ? Original_CColumn_ConstructDefault(self, nullptr)
                                                       : ours.columnConstruct(self, nullptr));
        },
        NoStubs);
    ok &= CompareCases(
        ctx, "CLine::CLine", cases, recordSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            return static_cast<std::uint32_t>(original ? Original_CLine_ConstructDefault(self, nullptr)
                                                       : ours.lineConstruct(self, nullptr));
        },
        NoStubs);
    // The destructors' EAX without a record is the caller's: compared as 0.
    ok &= CompareCases(
        ctx, "CColumn::~CColumn", cases, recordSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            const bool none = Field<std::uint32_t>(self, 0x1c) == 0;
            const std::uint32_t eax = original ? Original_CColumn_Destroy(self, nullptr) : ours.columnDestroy(self, nullptr);
            return none ? 0 : eax;
        },
        NoStubs);
    ok &= CompareCases(
        ctx, "CLine::~CLine", cases, recordSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t) {
            const bool none = Field<std::uint32_t>(self, 0x10) == 0;
            const std::uint32_t eax = original ? Original_CLine_Destroy(self, nullptr) : ours.lineDestroy(self, nullptr);
            return none ? 0 : eax;
        },
        NoStubs);

    // CImageButton: this = kThis (the element), vbptr this-0x5c, hover image
    // this-8 (-1 or not).
    auto buttonSetup = [](test::Rng& rng) {
        rng.Fill(g_buffer, kBytes);
        VbPtr(kThis - 0x5c, 0, 0x5c + 4 * static_cast<std::int32_t>(rng.Below(3)));
        for (std::size_t element : {kThis, kThis + 4, kThis + 8}) {
            Put(element, FakeVtable());
        }
        if (rng.Chance(1, 2)) {
            Put(kThis - 8, 0xFFFFFFFFu);
        }
    };
    auto messageStubs = [](test::CalleeStubs& stubs) {
        stubs.Install<5, kControlOnMouseEnter>(&Fixed);
        stubs.Install<5, kButtonOnMouseLeave>(&Fixed);
    };
    ok &= CompareCases(
        ctx, "CImageButton::OnMouseEnter", cases, buttonSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t seed) {
            const Message m = MessageFrom(seed);
            return original ? Original_CImageButton_OnMouseEnter(self, nullptr, m) : ours.mouseEnter(self, nullptr, m);
        },
        messageStubs);
    ok &= CompareCases(
        ctx, "CImageButton::OnMouseLeave", cases, buttonSetup,
        [&](bool original, std::uintptr_t self, std::uint64_t seed) {
            const Message m = MessageFrom(seed);
            return original ? Original_CImageButton_OnMouseLeave(self, nullptr, m) : ours.mouseLeave(self, nullptr, m);
        },
        messageStubs);

    // CDialog::SetParent: this = kThis (the element), vbptr this-0xc0; the
    // element's vtable has SetFocus (slot 0xc) with one argument. SetParent
    // stubbed; its effect moves the displacement (the original re-reads it).
    ok &= CompareCases(
        ctx, "CDialog::SetParent", cases,
        [](test::Rng& rng) {
            rng.Fill(g_buffer, kBytes);
            VbPtr(kThis - 0xc0, 0, 0xc0 + 4 * static_cast<std::int32_t>(rng.Below(3)));
            for (std::size_t element : {kThis, kThis + 4, kThis + 8, kThis + 12}) {
                Put(element, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_focusVtable)));
            }
        },
        [&](bool original, std::uintptr_t self, std::uint64_t seed) {
            g_focusVtable[3] = SlotAddress<0x10c>(std::make_index_sequence<1>{});  // SetFocus (a label of its own)
            const auto parent = static_cast<std::uint32_t>(seed >> 16);
            return original ? Original_CDialog_SetParent(self, nullptr, parent) : ours.setParent(self, nullptr, parent);
        },
        [](test::CalleeStubs& stubs) {
            stubs.Install<1, kElementSetParent>([](CGameData*, std::uint32_t self, const std::uint32_t* args) {
                std::uint8_t* buffer = g_buffer;  // the stubs' world
                std::int32_t displacement = 0;
                std::memcpy(&displacement, buffer + kTable + 4, 4);
                displacement += (args[0] & 1) != 0 ? 4 : 0;
                std::memcpy(buffer + kTable + 4, &displacement, 4);
                return self ^ args[0];
            });
        });
    return ok;
}

// Wrong versions for the harness check.
std::uint32_t __fastcall WrongUpdateLayout(std::uintptr_t self, void* edx) {
    const std::uint32_t eax = CListBox_UpdateLayout(self, edx);
    Field<std::int32_t>(self, 0x404) += Field<std::uint32_t>(self, 0x434) == Field<std::uint32_t>(self, 0x414) ? 1 : 0;
    return eax;
}

std::uint32_t __fastcall WrongShowChild(std::uintptr_t self, void* edx, std::uint32_t show, std::uint32_t child) {
    return CScreenGroup_ShowChild(self, edx, show & 0xff, child);  // wrong: only the low byte of the flag
}

}  // namespace

S3RE_TEST(UiGroupMembers_MatchOriginal) {
    CompareAll(ctx, Members{}, 3000);
}

S3RE_TEST(UiGroupMembers_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Members wrong;
        wrong.updateLayout = &WrongUpdateLayout;
        CompareAll(probe, wrong, 3000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 2, /*quiet=*/true);
        Members wrong;
        wrong.showChild = &WrongShowChild;
        CompareAll(probe, wrong, 3000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
