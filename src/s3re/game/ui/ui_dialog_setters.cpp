// Small members of the UI controls of Dirk\dialog.cpp (wip/dialog-r8): field
// getters and setters, setters that redraw the element, and short wrappers.
// All thiscall unless noted; layouts in re/ui_classes.md. "Invalidate" is
// CScreenElement slot 7 (0x1c) on the element part, reached through the
// object's vbptr where the class derives virtually from CScreenElement
// (element = vbptr address + vbtable[1]). EAX is what the original leaves
// in it: the stored value for a plain setter, the last call's result
// otherwise.
//
// Getters: CScreen::GetScreen (0x00417f90, slot 30 of CScreen's element
//   part: this - 0x98, the screen itself), CButton::IsChecked (0x0041a9a0,
//   AL = +0x4a; the rest of EAX is the caller's), CStaticSurface::GetSurface
//   (0x0041df30, +0x20), CListBox::GetItems (0x0041f240, this + 0x420),
//   CListBox::GetSelection (0x0041f3a0, +0x418), CListView::
//   GetFullyVisibleLineCount / GetLineCount / GetDefaultLineHeight
//   (0x00424570 / 0x00424580 / 0x004245b0: +0x18c / +0xf8 / +0x184).
// Plain setters (value) ret 4, EAX = value: CImageButton::SetGfxFile
//   (0x0041acb0, +0x4c), CStaticText::SetFont (0x0041ca50, +0x3c),
//   CListView::CColumn::SetTooltipTextId (0x004276a0, +0x10; the same
//   folded body sets Dirk::CMapFile's error code), CDialog::SetDefaultButton
//   / SetCancelButton (0x00427920 / 0x00427930, +0xb8 / +0xbc).
// Setters that redraw (EAX = Invalidate's): CStaticText::SetHighlight
//   (0x0041ca20, byte +0x38; Draw uses another font for it; CListView::
//   SetSelection sets it on the selected line's text cells), CStaticText::
//   SetColor (0x0041ca30, +0x20 = 1, +0x24 = colour), CStaticImage::SetImage
//   (0x0041d7c0, +0x20), CStaticFrame::SetFrame (0x0041d9c0, +0x20/+0x24),
//   CStaticColorIndexedFrame::SetFrame (0x0041dbc0, +0x20/+0x24/+0x28),
//   CImageButton::SetImages (0x0041acc0, file, normal, pressed, hover at
//   +0x4c..+0x58; this = the button's start, vbptr +4), CTextButton::
//   SetStyle (0x0041b960, +0x6c; vbptr +4). CEditControl::OnFocusChanged
//   (0x00419b10, slot 25, (bool) ret 4: Invalidate through the vbptr at
//   this - 0x6c, then AL = 1 over Invalidate's EAX).
// Wrappers:
//   CScreen::InitDialogs (0x00417f30): (surface, graphicsData) ret 8:
//     Dialog::Init 0x416190 with ECX = the element part (vbptr +4).
//   CTextView::SetMaxLines (0x00420d40): (n) ret 4: if n != +0x420: store
//     it and Reflow 0x420eb0 (EAX = its); else EAX = n.
//   CTextView::Clear (0x00420730): text +0x410 = "" (assign_cstr_or_empty
//     0x413d70), +0x400 = 0, Reflow. EAX = Reflow's.
//   CTextView::ScrollTo (0x00420d60) / CListView::ScrollTo (0x00425110):
//     (line) ret 4: the scroll bar (+0xc0 / +0x198) SetPosition 0x41e330,
//     then Invalidate (vbptr +4).
//   CStaticText::SetTextId (0x0041c9f0): (id) ret 4: Dialog::GraphicsData
//     0x4168c0, CGraphicsData::TextAddress 0x5faaa0(id, -1) on it, the
//     text +0x28 = that (0x413d70), Invalidate.
//   CDropdown::SetEnabled (0x00422460; slot 0 of CDropdown's own vtable,
//     this = the dropdown's control part): (bool) ret 4: CControl::
//     SetEnabled 0x418810, then the list box (+0x1c8) own vtable slot 0
//     with the same value. EAX = the list box's. Was SetControlFlag0C.
//   CEditControl::NotifyRejectedChar (0x0041a570; this = the edit control's
//     start): with a functor at +0x3c: its slot 0 with the element part
//     (vbptr +4; a null `this` would pass 0, dead); EAX = its. Without:
//     EAX = this.
//   CFunctor2::Invoke (0x004281a0; slot 0 of the CFunctor2 family): (a, b)
//     ret 8: calls the member-function pointer +8 (thiscall) on the object
//     +4 adjusted like a virtual-inheritance pointer to member: object + 4
//     + its vbtable (object+4)[+0x10 bytes] + the offset +0xc. EAX = its.
//   CListView::OnResized (0x004268c0; slot 29 through a thunk): (rect,
//     delta) ret 8: UpdateScrollBar 0x425b50 on this - 0x59c. EAX = its.
//   CListView::Clear (0x00423a20): RemoveColumn 0x4242a0 for column count
//     (+0xdc) - 1 down to 0, RemoveLine 0x4243b0 for line count (+0xf8) - 1
//     down to 0 (each count read once), top line (+0x190) = 0, selection
//     (+0x194) = -1, LayoutLines 0x4252c0. EAX = its.
//   CListView::RemoveAllLines (0x00423a70): the line loop, the two fields,
//     LayoutLines, LayoutCells 0x4258f0. EAX = LayoutCells'.
//   CListView::GetCell (0x00424e60): (line, col) ret 8: the key line << 16
//     + col stored at +0x178 (the cell dictionary's search key, +0x78 of
//     the dictionary at +0x100), WCSkipNonTempBase::base_find 0x60bab0
//     (&key); not found: base_insert 0x60bbe0(&key) (a null cell). EAX =
//     the dword before the returned pointer (the cell); a failed insert
//     reads address 0 (crashes, kept).
//   WCSkipListBase::insert (0x00428180; slot 5): (link) ret 4: base_insert
//     0x60bbe0(link - 8); EAX = (its result != 0).
//   WCValOrderedVector<CColumn>::resize (0x00429940) / <CLine> (0x00429960):
//     (n) ret 4: WCBareVectorBase resize 0x429a80 / 0x429b50; if it
//     succeeded and the entry count (+0x14) > n (unsigned), count = n. EAX
//     = the resize's result, or the count's old... see the code: EAX stays
//     the bare resize's result.
//   WCValSList<bool>::NewLink (0x004298d0): (const bool&) ret 4: an 8-byte
//     link from the list's allocator (+0x10, called with the size in ECX)
//     or operator new; if not null: next = 0, the byte at +4 = the value.
//     EAX = the link.
//   DirectX::CSurface::DDGetDC (0x00419ae0): (HDC*) ret 4: the interface's
//     (+8) GetDC (COM slot 0x44); unless DD_OK or DDERR_WASSTILLDRAWING:
//     CheckResult 0x5bf2e0(hr, "CSurface::DDGetDC"). EAX = hr or its.
//   CScreenElement::ReleaseDC (0x00416080; `this` unused): (surface, hdc)
//     ret 8: the interface's ReleaseDC (COM slot 0x68), CheckResult(hr,
//     "CSurface::DDReleaseDC"); if that gives DDERR_SURFACELOST once more
//     (ReleaseDC, CheckResult). EAX = the last CheckResult's.
//   CScreenElement::PostMessage (0x004160f0; slot 31): (message by value)
//     ret 0x14: the parent (+0x18) element part's slot 31 with the message;
//     no parent: Win32::ThrowAssertmentFailed 0x6119b0("Not connected to
//     screen", dialog.cpp, 434). EAX = the call's.

#include "game/texts.h"
#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

// Callees (S3.EXE addresses; ours where replaced).
constexpr std::uintptr_t kDialogInit = 0x00416190;       // Dialog::Init
constexpr std::uintptr_t kGraphicsData = 0x004168c0;     // Dialog::GraphicsData (ours)
constexpr std::uintptr_t kTextAddress = 0x005faaa0;      // CGraphicsData::TextAddress (ours)
constexpr std::uintptr_t kAssignText = 0x00413d70;       // std::string::assign_cstr_or_empty (ours)
constexpr std::uintptr_t kReflow = 0x00420eb0;           // CTextView::Reflow
constexpr std::uintptr_t kScrollSetPosition = 0x0041e330;  // CVScrollBar::SetPosition (ours)
constexpr std::uintptr_t kControlSetEnabled = 0x00418810;  // CControl::SetEnabled (ours)
constexpr std::uintptr_t kUpdateScrollBar = 0x00425b50;  // CListView::UpdateScrollBar
constexpr std::uintptr_t kRemoveColumn = 0x004242a0;     // CListView::RemoveColumn
constexpr std::uintptr_t kRemoveLine = 0x004243b0;       // CListView::RemoveLine
constexpr std::uintptr_t kLayoutLines = 0x004252c0;      // CListView::LayoutLines
constexpr std::uintptr_t kLayoutCells = 0x004258f0;      // CListView::LayoutCells
constexpr std::uintptr_t kSkipFind = 0x0060bab0;         // WCSkipNonTempBase::base_find
constexpr std::uintptr_t kSkipInsert = 0x0060bbe0;       // WCSkipNonTempBase::base_insert
constexpr std::uintptr_t kColumnBareResize = 0x00429a80;  // WCBareVectorBase<CColumn>::resize
constexpr std::uintptr_t kLineBareResize = 0x00429b50;   // WCBareVectorBase<CLine>::resize
constexpr std::uintptr_t kOperatorNew = 0x0061efb9;      // the game's operator new
constexpr std::uintptr_t kCheckResult = 0x005bf2e0;      // DirectX::CSurface::CheckResult (ours)
constexpr std::uintptr_t kThrowAssert = 0x006119b0;      // Win32::ThrowAssertmentFailed (ours)

constexpr std::int32_t kWasStillDrawing = static_cast<std::int32_t>(0x8876021c);  // DDERR_WASSTILLDRAWING
constexpr std::int32_t kSurfaceLost = static_cast<std::int32_t>(0x887601c2);      // DDERR_SURFACELOST
constexpr std::uint32_t kGetDCSlot = 0x44;      // IDirectDrawSurface::GetDC
constexpr std::uint32_t kReleaseDCSlot = 0x68;  // IDirectDrawSurface::ReleaseDC
constexpr std::int32_t kPostMessageLine = 434;  // dialog.cpp line of the "connected to a screen" assert
constexpr std::uint32_t kNoTextVariant = 0xffffffffu;  // TextAddress's second argument
constexpr std::uint32_t kLinkBytes = 8;         // WCValSList<bool> link: next, value

// Field offsets (from `this` as each member gets it).
namespace off {
constexpr std::int32_t kScreenFromElement = -0x98;  // CScreen's element part -> the screen
constexpr std::int32_t kButtonChecked = 0x4a;
constexpr std::int32_t kStaticSurface = 0x20;
constexpr std::int32_t kListBoxSelection = 0x418;
constexpr std::int32_t kListBoxItems = 0x420;
constexpr std::int32_t kListViewColumnCount = 0xdc;
constexpr std::int32_t kListViewLineCount = 0xf8;
constexpr std::int32_t kListViewCells = 0x100;      // the cell dictionary (key line << 16 | col)
constexpr std::int32_t kCellsSearchKey = 0x78;      // in the dictionary
constexpr std::int32_t kListViewDefaultHeight = 0x184;
constexpr std::int32_t kListViewFullyVisible = 0x18c;
constexpr std::int32_t kListViewTopLine = 0x190;
constexpr std::int32_t kListViewSelection = 0x194;
constexpr std::int32_t kListViewScrollBar = 0x198;
constexpr std::int32_t kListViewFromResized = -0x59c;
constexpr std::int32_t kImageButtonFile = 0x4c;     // then normal, pressed, hover frames
constexpr std::int32_t kStaticTextOwnColour = 0x20;
constexpr std::int32_t kStaticTextColour = 0x24;
constexpr std::int32_t kStaticTextText = 0x28;
constexpr std::int32_t kStaticTextHighlight = 0x38;
constexpr std::int32_t kStaticTextFont = 0x3c;
constexpr std::int32_t kStaticImage = 0x20;         // also the frames' fields +0x20..+0x28
constexpr std::int32_t kColumnTooltip = 0x10;
constexpr std::int32_t kDialogDefault = 0xb8;
constexpr std::int32_t kDialogCancel = 0xbc;
constexpr std::int32_t kTextButtonStyle = 0x6c;
constexpr std::int32_t kEditVbPtr = -0x6c;          // from the edit control's element part
constexpr std::int32_t kEditRejectFunctor = 0x3c;   // from the edit control's start
constexpr std::int32_t kTextViewScrollBar = 0xc0;
constexpr std::int32_t kTextViewLineCount = 0x400;  // cleared by Clear
constexpr std::int32_t kTextViewText = 0x410;
constexpr std::int32_t kTextViewMaxLines = 0x420;
constexpr std::int32_t kDropdownList = 0x1c8;
constexpr std::int32_t kVectorEntries = 0x14;       // WCValOrderedVector's entry count
constexpr std::int32_t kListAllocator = 0x10;       // WCValSList's allocator function
constexpr std::int32_t kSurfaceInterface = 0x08;    // DirectX::CSurface's IDirectDrawSurface
}  // namespace off

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

// The virtual base reached through the vbptr at self + delta.
std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t delta) {
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(self + delta);
    return self + delta + vbtable[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);

struct Message {  // 20 bytes, passed by value
    std::uint32_t words[5];
};
using MessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

std::uint32_t Invalidate(std::uintptr_t element) {
    return Slot<Thiscall0>(element, ui::element_slot::kInvalidate)(element, nullptr);
}

}  // namespace

using GetFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using ByteGetFn = std::uint8_t(__fastcall*)(std::uintptr_t self, void* edx);
using Set1Fn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Set2Fn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using Set3Fn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c);
using Set4Fn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c, std::uint32_t d);
using PostMessageFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, Message message);

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

// Purpose: The root screen of an element tree (a CScreen answers with itself).
std::uint32_t __fastcall CScreen_GetScreen(std::uintptr_t self, void*) {
    return static_cast<std::uint32_t>(self + off::kScreenFromElement);
}

// Purpose: Whether a toggle button is checked.
std::uint8_t __fastcall CButton_IsChecked(std::uintptr_t self, void*) {
    return Field<std::uint8_t>(self, off::kButtonChecked);
}

// Purpose: The off-screen surface a CStaticSurface element shows.
std::uint32_t __fastcall CStaticSurface_GetSurface(std::uintptr_t self, void*) {
    return Field<std::uint32_t>(self, off::kStaticSurface);
}

// Purpose: The item strings of a list box.
std::uint32_t __fastcall CListBox_GetItems(std::uintptr_t self, void*) {
    return static_cast<std::uint32_t>(self + off::kListBoxItems);
}

// Purpose: The selected item of a list box (-1: none).
std::uint32_t __fastcall CListBox_GetSelection(std::uintptr_t self, void*) {
    return Field<std::uint32_t>(self, off::kListBoxSelection);
}

// Purpose: How many lines of a list view fit into its client area.
std::uint32_t __fastcall CListView_GetFullyVisibleLineCount(std::uintptr_t self, void*) {
    return Field<std::uint32_t>(self, off::kListViewFullyVisible);
}

// Purpose: The number of lines of a list view.
std::uint32_t __fastcall CListView_GetLineCount(std::uintptr_t self, void*) {
    return Field<std::uint32_t>(self, off::kListViewLineCount);
}

// Purpose: The line height a list view uses when a line is added without one.
std::uint32_t __fastcall CListView_GetDefaultLineHeight(std::uintptr_t self, void*) {
    return Field<std::uint32_t>(self, off::kListViewDefaultHeight);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

std::uint32_t StoreDword(std::uintptr_t self, std::int32_t offset, std::uint32_t value) {
    Field<std::uint32_t>(self, offset) = value;
    return value;
}

// Purpose: Selects the GFX file an image button's frames come from.
std::uint32_t __fastcall CImageButton_SetGfxFile(std::uintptr_t self, void*, std::uint32_t file) {
    return StoreDword(self, off::kImageButtonFile, file);
}

// Purpose: Selects the font of a static text.
std::uint32_t __fastcall CStaticText_SetFont(std::uintptr_t self, void*, std::uint32_t font) {
    return StoreDword(self, off::kStaticTextFont, font);
}

// Purpose: Sets the text id of a list view column's tooltip (the same code
// sets a map file's error code).
std::uint32_t __fastcall CColumn_SetTooltipTextId(std::uintptr_t self, void*, std::uint32_t textId) {
    return StoreDword(self, off::kColumnTooltip, textId);
}

// Purpose: The button a dialog clicks on Enter.
std::uint32_t __fastcall CDialog_SetDefaultButton(std::uintptr_t self, void*, std::uint32_t button) {
    return StoreDword(self, off::kDialogDefault, button);
}

// Purpose: The button a dialog clicks on Escape.
std::uint32_t __fastcall CDialog_SetCancelButton(std::uintptr_t self, void*, std::uint32_t button) {
    return StoreDword(self, off::kDialogCancel, button);
}

// Purpose: Highlights a static text (a list view's selected line) or not.
std::uint32_t __fastcall CStaticText_SetHighlight(std::uintptr_t self, void*, std::uint32_t highlight) {
    Field<std::uint8_t>(self, off::kStaticTextHighlight) = static_cast<std::uint8_t>(highlight);
    return Invalidate(self);
}

// Purpose: Gives a static text its own colour.
std::uint32_t __fastcall CStaticText_SetColor(std::uintptr_t self, void*, std::uint32_t colour) {
    Field<std::uint8_t>(self, off::kStaticTextOwnColour) = 1;
    Field<std::uint32_t>(self, off::kStaticTextColour) = colour;
    return Invalidate(self);
}

// Purpose: Changes the GUI image a static image shows.
std::uint32_t __fastcall CStaticImage_SetImage(std::uintptr_t self, void*, std::uint32_t image) {
    Field<std::uint32_t>(self, off::kStaticImage) = image;
    return Invalidate(self);
}

// Purpose: Changes the sprite (file, frame) a static frame shows.
std::uint32_t __fastcall CStaticFrame_SetFrame(std::uintptr_t self, void*, std::uint32_t file, std::uint32_t frame) {
    Field<std::uint32_t>(self, off::kStaticImage) = file;
    Field<std::uint32_t>(self, off::kStaticImage + 4) = frame;
    return Invalidate(self);
}

// Purpose: Changes the palette-indexed image a static frame shows.
std::uint32_t __fastcall CStaticColorIndexedFrame_SetFrame(std::uintptr_t self, void*, std::uint32_t a,
                                                           std::uint32_t b, std::uint32_t c) {
    Field<std::uint32_t>(self, off::kStaticImage) = a;
    Field<std::uint32_t>(self, off::kStaticImage + 4) = b;
    Field<std::uint32_t>(self, off::kStaticImage + 8) = c;
    return Invalidate(self);
}

// Purpose: Sets an image button's GFX file and its normal, pressed and hover frames.
std::uint32_t __fastcall CImageButton_SetImages(std::uintptr_t self, void*, std::uint32_t file, std::uint32_t normal,
                                                std::uint32_t pressed, std::uint32_t hover) {
    Field<std::uint32_t>(self, off::kImageButtonFile) = file;
    Field<std::uint32_t>(self, off::kImageButtonFile + 4) = normal;
    Field<std::uint32_t>(self, off::kImageButtonFile + 8) = pressed;
    Field<std::uint32_t>(self, off::kImageButtonFile + 12) = hover;
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Sets a text button's text style.
std::uint32_t __fastcall CTextButton_SetStyle(std::uintptr_t self, void*, std::uint32_t style) {
    Field<std::uint32_t>(self, off::kTextButtonStyle) = style;
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Redraws an edit field when it gains or loses the focus (the cursor).
std::uint32_t __fastcall CEditControl_OnFocusChanged(std::uintptr_t self, void*, std::uint32_t /*focused*/) {
    const std::uint32_t result = Invalidate(VirtualBase(self, off::kEditVbPtr));
    return (result & 0xffffff00u) | 1;  // mov al, 1 over Invalidate's EAX
}

// ---------------------------------------------------------------------------
// Wrappers
// ---------------------------------------------------------------------------

// Purpose: Sets up the dialog globals (surface, graphics, fonts) for a screen.
std::uint32_t __fastcall CScreen_InitDialogs(std::uintptr_t self, void*, std::uint32_t surface,
                                             std::uint32_t graphicsData) {
    return reinterpret_cast<Thiscall2>(kDialogInit)(VirtualBase(self, 4), nullptr, surface, graphicsData);
}

// Purpose: Limits how many lines a text view keeps (older lines are dropped).
std::uint32_t __fastcall CTextView_SetMaxLines(std::uintptr_t self, void*, std::uint32_t maxLines) {
    if (maxLines == Field<std::uint32_t>(self, off::kTextViewMaxLines)) {
        return maxLines;
    }
    Field<std::uint32_t>(self, off::kTextViewMaxLines) = maxLines;
    return reinterpret_cast<Thiscall0>(kReflow)(self, nullptr);
}

// Purpose: Empties a text view (chat and message windows).
std::uint32_t __fastcall CTextView_Clear(std::uintptr_t self, void*) {
    reinterpret_cast<Thiscall1>(kAssignText)(self + off::kTextViewText, nullptr, texts::kEmpty.address());
    Field<std::uint32_t>(self, off::kTextViewLineCount) = 0;
    return reinterpret_cast<Thiscall0>(kReflow)(self, nullptr);
}

std::uint32_t ScrollAndRedraw(std::uintptr_t self, std::int32_t scrollBar, std::uint32_t line) {
    reinterpret_cast<Thiscall1>(kScrollSetPosition)(self + scrollBar, nullptr, line);
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Scrolls a text view so that `line` is its first visible line.
std::uint32_t __fastcall CTextView_ScrollTo(std::uintptr_t self, void*, std::uint32_t line) {
    return ScrollAndRedraw(self, off::kTextViewScrollBar, line);
}

// Purpose: Scrolls a list view so that `line` is its top line.
std::uint32_t __fastcall CListView_ScrollTo(std::uintptr_t self, void*, std::uint32_t line) {
    return ScrollAndRedraw(self, off::kListViewScrollBar, line);
}

// Purpose: Sets a static text to one of the game's texts (by text id).
std::uint32_t __fastcall CStaticText_SetTextId(std::uintptr_t self, void*, std::uint32_t textId) {
    using GraphicsDataFn = std::uint32_t(__stdcall*)();
    const std::uint32_t graphics = reinterpret_cast<GraphicsDataFn>(kGraphicsData)();
    const std::uint32_t text = reinterpret_cast<Thiscall2>(kTextAddress)(graphics, nullptr, textId, kNoTextVariant);
    reinterpret_cast<Thiscall1>(kAssignText)(self + off::kStaticTextText, nullptr, text);
    return Invalidate(self);
}

// Purpose: Enables or disables a dropdown together with its drop list.
std::uint32_t __fastcall CDropdown_SetEnabled(std::uintptr_t self, void*, std::uint32_t enabled) {
    reinterpret_cast<Thiscall1>(kControlSetEnabled)(self, nullptr, enabled);
    const std::uintptr_t list = self + off::kDropdownList;
    return Slot<Thiscall1>(list, 0)(list, nullptr, enabled);
}

// Purpose: Tells an edit field's listener that a typed character was refused
// (probably to beep).
std::uint32_t __fastcall CEditControl_NotifyRejectedChar(std::uintptr_t self, void*) {
    const std::uintptr_t functor = Field<std::uint32_t>(self, off::kEditRejectFunctor);
    if (functor == 0) {
        return static_cast<std::uint32_t>(self);
    }
    const std::uint32_t element = self == 0 ? 0 : static_cast<std::uint32_t>(VirtualBase(self, 4));
    return Slot<Thiscall1>(functor, 0)(functor, nullptr, element);
}

// Purpose: Calls the member function a two-argument UI callback (button
// click, scroll, selection) is bound to.
std::uint32_t __fastcall CFunctor2_Invoke(std::uintptr_t self, void*, std::uint32_t a, std::uint32_t b) {
    const std::uintptr_t object = Field<std::uint32_t>(self, 4);
    const Thiscall2 method = Field<Thiscall2>(self, 8);
    const std::int32_t adjust = Field<std::int32_t>(self, 0xc);
    const std::int32_t vbIndex = Field<std::int32_t>(self, 0x10);
    const auto* vbtable = *reinterpret_cast<const std::uint8_t* const*>(object + 4);
    const std::int32_t displacement = *reinterpret_cast<const std::int32_t*>(vbtable + vbIndex);
    return method(object + 4 + displacement + adjust, nullptr, a, b);
}

// Purpose: Recomputes a list view's scroll bar after it was resized.
std::uint32_t __fastcall CListView_OnResized(std::uintptr_t self, void*, std::uint32_t /*rect*/,
                                             std::uint32_t /*delta*/) {
    return reinterpret_cast<Thiscall0>(kUpdateScrollBar)(self + off::kListViewFromResized, nullptr);
}

void RemoveLines(std::uintptr_t self) {
    for (std::int32_t line = Field<std::int32_t>(self, off::kListViewLineCount) - 1; line >= 0; --line) {
        reinterpret_cast<Thiscall1>(kRemoveLine)(self, nullptr, static_cast<std::uint32_t>(line));
    }
}

// Purpose: Removes every column and line of a list view.
std::uint32_t __fastcall CListView_Clear(std::uintptr_t self, void*) {
    for (std::int32_t column = Field<std::int32_t>(self, off::kListViewColumnCount) - 1; column >= 0; --column) {
        reinterpret_cast<Thiscall1>(kRemoveColumn)(self, nullptr, static_cast<std::uint32_t>(column));
    }
    RemoveLines(self);
    Field<std::uint32_t>(self, off::kListViewTopLine) = 0;
    Field<std::int32_t>(self, off::kListViewSelection) = -1;
    return reinterpret_cast<Thiscall0>(kLayoutLines)(self, nullptr);
}

// Purpose: Removes every line of a list view (keeps the columns).
std::uint32_t __fastcall CListView_RemoveAllLines(std::uintptr_t self, void*) {
    RemoveLines(self);
    Field<std::uint32_t>(self, off::kListViewTopLine) = 0;
    Field<std::int32_t>(self, off::kListViewSelection) = -1;
    reinterpret_cast<Thiscall0>(kLayoutLines)(self, nullptr);
    return reinterpret_cast<Thiscall0>(kLayoutCells)(self, nullptr);
}

// Purpose: The control in a list view cell (line, column), creating an
// empty entry for it if there is none.
std::uint32_t __fastcall CListView_GetCell(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t column) {
    const std::uintptr_t cells = self + off::kListViewCells;
    std::uint32_t& key = Field<std::uint32_t>(cells, off::kCellsSearchKey);
    key = (line << 16) + column;
    const auto keyAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&key));
    std::uint32_t found = reinterpret_cast<Thiscall1>(kSkipFind)(cells, nullptr, keyAddress);
    if (found == 0) {
        found = reinterpret_cast<Thiscall1>(kSkipInsert)(cells, nullptr, keyAddress);
        if (found == 0) {
            return *reinterpret_cast<volatile std::uint32_t*>(found);  // reads address 0, as the original
        }
    }
    return Field<std::uint32_t>(found, -4);
}

// Purpose: The skip-list dictionaries' insert slot: inserts a link (its
// key/value 8 bytes in), true on success.
std::uint32_t __fastcall WCSkipListBase_insert(std::uintptr_t self, void*, std::uint32_t link) {
    return reinterpret_cast<Thiscall1>(kSkipInsert)(self, nullptr, link - 8) != 0 ? 1 : 0;
}

std::uint32_t ResizeAndClamp(std::uintptr_t self, std::uintptr_t bareResize, std::uint32_t size) {
    const std::uint32_t result = reinterpret_cast<Thiscall1>(bareResize)(self, nullptr, size);
    if (result != 0 && Field<std::uint32_t>(self, off::kVectorEntries) > size) {
        Field<std::uint32_t>(self, off::kVectorEntries) = size;
    }
    return result;
}

// Purpose: Resizes a list view's column vector.
std::uint32_t __fastcall WCValOrderedVector_CColumn_resize(std::uintptr_t self, void*, std::uint32_t size) {
    return ResizeAndClamp(self, kColumnBareResize, size);
}

// Purpose: Resizes a list view's line vector.
std::uint32_t __fastcall WCValOrderedVector_CLine_resize(std::uintptr_t self, void*, std::uint32_t size) {
    return ResizeAndClamp(self, kLineBareResize, size);
}

// Purpose: Allocates a link of a list of bools.
std::uint32_t __fastcall WCValSList_bool_NewLink(std::uintptr_t self, void*, const std::uint8_t* value) {
    using AllocatorFn = std::uint32_t(__fastcall*)(std::uint32_t size);
    using OperatorNewFn = std::uint32_t(__cdecl*)(std::uint32_t size);
    const auto allocator = Field<AllocatorFn>(self, off::kListAllocator);
    const std::uint32_t link = allocator != nullptr ? allocator(kLinkBytes)
                                                    : reinterpret_cast<OperatorNewFn>(kOperatorNew)(kLinkBytes);
    if (link != 0) {
        Field<std::uint32_t>(link, 0) = 0;
        Field<std::uint8_t>(link, 4) = *value;
    }
    return link;
}

using CheckResultFn = std::int32_t(__fastcall*)(std::uintptr_t surface, void* edx, std::int32_t hr, const char* where);

// Purpose: Gets a GDI device context for a DirectDraw surface (text drawing).
std::int32_t __fastcall DirectX_CSurface_DDGetDC(std::uintptr_t self, void*, std::uint32_t dcOut) {
    using GetDCFn = std::int32_t(__stdcall*)(std::uint32_t iface, std::uint32_t dc);
    const std::uint32_t iface = Field<std::uint32_t>(self, off::kSurfaceInterface);
    const std::int32_t hr = Slot<GetDCFn>(iface, kGetDCSlot)(iface, dcOut);
    if (hr == 0 || hr == kWasStillDrawing) {
        return hr;
    }
    return reinterpret_cast<CheckResultFn>(kCheckResult)(self, nullptr, hr, texts::kDDGetDC);
}

// Purpose: Gives a surface's device context back (after drawing text).
std::int32_t __fastcall CScreenElement_ReleaseDC(std::uintptr_t /*self*/, void*, std::uintptr_t surface,
                                                 std::uint32_t dc) {
    using ReleaseDCFn = std::int32_t(__stdcall*)(std::uint32_t iface, std::uint32_t dc);
    auto release = [&] {
        const std::uint32_t iface = Field<std::uint32_t>(surface, off::kSurfaceInterface);
        const std::int32_t hr = Slot<ReleaseDCFn>(iface, kReleaseDCSlot)(iface, dc);
        return reinterpret_cast<CheckResultFn>(kCheckResult)(surface, nullptr, hr, texts::kDDReleaseDC);
    };
    const std::int32_t result = release();
    return result == kSurfaceLost ? release() : result;
}

// Purpose: Passes a message up to the element's container (to the screen's queue).
std::uint32_t __fastcall CScreenElement_PostMessage(std::uintptr_t self, void*, Message message) {
    const std::uintptr_t parent = Field<std::uint32_t>(self, offsetof(ui::CScreenElement, parent));
    if (parent == 0) {
        using AssertFn = std::uint32_t(__fastcall*)(const char* expression, const char* file, std::int32_t line);
        return reinterpret_cast<AssertFn>(kThrowAssert)(texts::kNotConnectedToScreen, texts::kDialogFile,
                                                        kPostMessageLine);
    }
    const std::uintptr_t element = VirtualBase(parent, 4);
    return Slot<MessageFn>(element, ui::element_slot::kPostMessage)(element, nullptr, message);
}

S3RE_HOOK(0x00417f90, CScreen_GetScreen, GetFn, Original_CScreen_GetScreen, CScreen_GetScreen);
S3RE_HOOK(0x0041a9a0, CButton_IsChecked, ByteGetFn, Original_CButton_IsChecked, CButton_IsChecked);
S3RE_HOOK(0x0041df30, CStaticSurface_GetSurface, GetFn, Original_CStaticSurface_GetSurface, CStaticSurface_GetSurface);
S3RE_HOOK(0x0041f240, CListBox_GetItems, GetFn, Original_CListBox_GetItems, CListBox_GetItems);
S3RE_HOOK(0x0041f3a0, CListBox_GetSelection, GetFn, Original_CListBox_GetSelection, CListBox_GetSelection);
S3RE_HOOK(0x00424570, CListView_GetFullyVisibleLineCount, GetFn, Original_CListView_GetFullyVisibleLineCount,
          CListView_GetFullyVisibleLineCount);
S3RE_HOOK(0x00424580, CListView_GetLineCount, GetFn, Original_CListView_GetLineCount, CListView_GetLineCount);
S3RE_HOOK(0x004245b0, CListView_GetDefaultLineHeight, GetFn, Original_CListView_GetDefaultLineHeight,
          CListView_GetDefaultLineHeight);
S3RE_HOOK(0x0041acb0, CImageButton_SetGfxFile, Set1Fn, Original_CImageButton_SetGfxFile, CImageButton_SetGfxFile);
S3RE_HOOK(0x0041ca50, CStaticText_SetFont, Set1Fn, Original_CStaticText_SetFont, CStaticText_SetFont);
S3RE_HOOK(0x004276a0, CColumn_SetTooltipTextId, Set1Fn, Original_CColumn_SetTooltipTextId, CColumn_SetTooltipTextId);
S3RE_HOOK(0x00427920, CDialog_SetDefaultButton, Set1Fn, Original_CDialog_SetDefaultButton, CDialog_SetDefaultButton);
S3RE_HOOK(0x00427930, CDialog_SetCancelButton, Set1Fn, Original_CDialog_SetCancelButton, CDialog_SetCancelButton);
S3RE_HOOK(0x0041ca20, CStaticText_SetHighlight, Set1Fn, Original_CStaticText_SetHighlight, CStaticText_SetHighlight);
S3RE_HOOK(0x0041ca30, CStaticText_SetColor, Set1Fn, Original_CStaticText_SetColor, CStaticText_SetColor);
S3RE_HOOK(0x0041d7c0, CStaticImage_SetImage, Set1Fn, Original_CStaticImage_SetImage, CStaticImage_SetImage);
S3RE_HOOK(0x0041d9c0, CStaticFrame_SetFrame, Set2Fn, Original_CStaticFrame_SetFrame, CStaticFrame_SetFrame);
S3RE_HOOK(0x0041dbc0, CStaticColorIndexedFrame_SetFrame, Set3Fn, Original_CStaticColorIndexedFrame_SetFrame,
          CStaticColorIndexedFrame_SetFrame);
S3RE_HOOK(0x0041acc0, CImageButton_SetImages, Set4Fn, Original_CImageButton_SetImages, CImageButton_SetImages);
S3RE_HOOK(0x0041b960, CTextButton_SetStyle, Set1Fn, Original_CTextButton_SetStyle, CTextButton_SetStyle);
S3RE_HOOK(0x00419b10, CEditControl_OnFocusChanged, Set1Fn, Original_CEditControl_OnFocusChanged,
          CEditControl_OnFocusChanged);
S3RE_HOOK(0x00417f30, CScreen_InitDialogs, Set2Fn, Original_CScreen_InitDialogs, CScreen_InitDialogs);
S3RE_HOOK(0x00420d40, CTextView_SetMaxLines, Set1Fn, Original_CTextView_SetMaxLines, CTextView_SetMaxLines);
S3RE_HOOK(0x00420730, CTextView_Clear, GetFn, Original_CTextView_Clear, CTextView_Clear);
S3RE_HOOK(0x00420d60, CTextView_ScrollTo, Set1Fn, Original_CTextView_ScrollTo, CTextView_ScrollTo);
S3RE_HOOK(0x00425110, CListView_ScrollTo, Set1Fn, Original_CListView_ScrollTo, CListView_ScrollTo);
S3RE_HOOK(0x0041c9f0, CStaticText_SetTextId, Set1Fn, Original_CStaticText_SetTextId, CStaticText_SetTextId);
S3RE_HOOK(0x00422460, CDropdown_SetEnabled, Set1Fn, Original_CDropdown_SetEnabled, CDropdown_SetEnabled);
S3RE_HOOK(0x0041a570, CEditControl_NotifyRejectedChar, GetFn, Original_CEditControl_NotifyRejectedChar,
          CEditControl_NotifyRejectedChar);
S3RE_HOOK(0x004281a0, CFunctor2_Invoke, Set2Fn, Original_CFunctor2_Invoke, CFunctor2_Invoke);
S3RE_HOOK(0x004268c0, CListView_OnResized, Set2Fn, Original_CListView_OnResized, CListView_OnResized);
S3RE_HOOK(0x00423a20, CListView_Clear, GetFn, Original_CListView_Clear, CListView_Clear);
S3RE_HOOK(0x00423a70, CListView_RemoveAllLines, GetFn, Original_CListView_RemoveAllLines, CListView_RemoveAllLines);
S3RE_HOOK(0x00424e60, CListView_GetCell, Set2Fn, Original_CListView_GetCell, CListView_GetCell);
S3RE_HOOK(0x00428180, WCSkipListBase_insert, Set1Fn, Original_WCSkipListBase_insert, WCSkipListBase_insert);
S3RE_HOOK(0x00429940, WCValOrderedVector_CColumn_resize, Set1Fn, Original_WCValOrderedVector_CColumn_resize,
          WCValOrderedVector_CColumn_resize);
S3RE_HOOK(0x00429960, WCValOrderedVector_CLine_resize, Set1Fn, Original_WCValOrderedVector_CLine_resize,
          WCValOrderedVector_CLine_resize);
using NewLinkFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, const std::uint8_t* value);
S3RE_HOOK(0x004298d0, WCValSList_bool_NewLink, NewLinkFn, Original_WCValSList_bool_NewLink, WCValSList_bool_NewLink);
using DDGetDCFn = std::int32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t dcOut);
S3RE_HOOK(0x00419ae0, DirectX_CSurface_DDGetDC, DDGetDCFn, Original_DirectX_CSurface_DDGetDC, DirectX_CSurface_DDGetDC);
using ReleaseDCFn = std::int32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uintptr_t surface, std::uint32_t dc);
S3RE_HOOK(0x00416080, CScreenElement_ReleaseDC, ReleaseDCFn, Original_CScreenElement_ReleaseDC,
          CScreenElement_ReleaseDC);
S3RE_HOOK(0x004160f0, CScreenElement_PostMessage, PostMessageFn, Original_CScreenElement_PostMessage,
          CScreenElement_PostMessage);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::At;
using kit::kThis;

constexpr std::uint32_t kListBuffer = 0x2c00;  // what the allocation stubs return
constexpr std::size_t kDisplacement = 0x100;    // virtual bases sit this far after their vbptr

// The allocation stubs and the find/insert stubs sometimes return a pointer
// into the buffer, sometimes 0.
std::uint32_t AllocatingEffect(const kit::Call& call, const std::uint32_t*) {
    std::uint32_t hash = call.callee * 2654435761u + call.args[0];
    for (std::uint32_t i = 1; i < call.count; ++i) {
        hash = hash * 31 + call.args[i];
    }
    if (call.callee == kOperatorNew || call.callee == kSkipFind || call.callee == kSkipInsert) {
        return (hash >> 8) % 4 == 0 ? 0 : static_cast<std::uint32_t>(kit::Current().base + kListBuffer + 4 * (hash % 8));
    }
    if (call.callee == kCheckResult) {
        return (hash >> 8) % 3 == 0 ? static_cast<std::uint32_t>(kSurfaceLost) : hash;
    }
    return hash;
}

std::uint32_t Arg(test::Rng& rng) {
    switch (rng.Below(4)) {
    case 0: return 0;
    case 1: return rng.Below(4);
    default: return rng.Next32();
    }
}

void NoSetup(test::Rng&, std::uint8_t*) {}

// The element at `this` has the fake vtable.
void ElementSetup(test::Rng&, std::uint8_t* buffer) { kit::FakeObject(buffer, kThis); }

// A vbptr at this + delta leading to an element with the fake vtable.
template <std::int32_t Delta>
void VbSetup(test::Rng&, std::uint8_t* buffer) {
    kit::VbPtr(buffer, static_cast<std::size_t>(static_cast<std::int32_t>(kThis) + Delta), kDisplacement, 0);
}

// Members whose comparison differs from the plain one (for the harness check).
struct Members {
    Set1Fn setTextId = &CStaticText_SetTextId;
    Set2Fn invoke = &CFunctor2_Invoke;
    GetFn clear = &CListView_Clear;
    Set2Fn getCell = &CListView_GetCell;
    ReleaseDCFn releaseDC = &CScreenElement_ReleaseDC;
};

template <typename Fn, typename Setup>
void CompareGet(test::Context& ctx, const char* name, Fn original, Fn ours, Setup setup, int cases,
                std::uint32_t mask = 0xffffffffu) {
    kit::Compare(
        ctx, name, cases, setup, [&](std::uintptr_t base, test::Rng&) { return original(base + kThis, nullptr); },
        [&](std::uintptr_t base, test::Rng&) { return ours(base + kThis, nullptr); }, mask, &AllocatingEffect);
}

template <typename Setup>
void CompareSet1(test::Context& ctx, const char* name, Set1Fn original, Set1Fn ours, Setup setup, int cases,
                 std::uint32_t mask = 0xffffffffu) {
    kit::Compare(
        ctx, name, cases, setup,
        [&](std::uintptr_t base, test::Rng& r) { return original(base + kThis, nullptr, Arg(r)); },
        [&](std::uintptr_t base, test::Rng& r) { return ours(base + kThis, nullptr, Arg(r)); }, mask,
        &AllocatingEffect);
}

template <typename Setup>
void CompareSet2(test::Context& ctx, const char* name, Set2Fn original, Set2Fn ours, Setup setup, int cases) {
    kit::Compare(
        ctx, name, cases, setup,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r);
            return original(base + kThis, nullptr, a, Arg(r));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r);
            return ours(base + kThis, nullptr, a, Arg(r));
        },
        0xffffffffu, &AllocatingEffect);
}

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kDialogInit, 2>()
        .Std<kGraphicsData, 0>()
        .This<kTextAddress, 2>()
        .This<kAssignText, 1>()
        .This<kReflow, 0>()
        .This<kScrollSetPosition, 1>()
        .This<kControlSetEnabled, 1>()
        .This<kUpdateScrollBar, 0>()
        .This<kRemoveColumn, 1>()
        .This<kRemoveLine, 1>()
        .This<kLayoutLines, 0>()
        .This<kLayoutCells, 0>()
        .This<kSkipFind, 1>()
        .This<kSkipInsert, 1>()
        .This<kColumnBareResize, 1>()
        .This<kLineBareResize, 1>()
        .Cdecl<kOperatorNew, 1>()
        .This<kCheckResult, 2>()
        .Fast<kThrowAssert, 1>();

    CompareGet(ctx, "GetScreen", Original_CScreen_GetScreen, &CScreen_GetScreen, NoSetup, cases);
    CompareGet(ctx, "IsChecked", Original_CButton_IsChecked, &CButton_IsChecked, NoSetup, cases, 0xff);
    CompareGet(ctx, "GetSurface", Original_CStaticSurface_GetSurface, &CStaticSurface_GetSurface, NoSetup, cases);
    CompareGet(ctx, "GetItems", Original_CListBox_GetItems, &CListBox_GetItems, NoSetup, cases);
    CompareGet(ctx, "GetSelection", Original_CListBox_GetSelection, &CListBox_GetSelection, NoSetup, cases);
    CompareGet(ctx, "FullyVisible", Original_CListView_GetFullyVisibleLineCount, &CListView_GetFullyVisibleLineCount,
               NoSetup, cases);
    CompareGet(ctx, "LineCount", Original_CListView_GetLineCount, &CListView_GetLineCount, NoSetup, cases);
    CompareGet(ctx, "DefaultHeight", Original_CListView_GetDefaultLineHeight, &CListView_GetDefaultLineHeight,
               NoSetup, cases);
    CompareSet1(ctx, "SetGfxFile", Original_CImageButton_SetGfxFile, &CImageButton_SetGfxFile, NoSetup, cases);
    CompareSet1(ctx, "SetFont", Original_CStaticText_SetFont, &CStaticText_SetFont, NoSetup, cases);
    CompareSet1(ctx, "SetTooltipTextId", Original_CColumn_SetTooltipTextId, &CColumn_SetTooltipTextId, NoSetup, cases);
    CompareSet1(ctx, "SetDefaultButton", Original_CDialog_SetDefaultButton, &CDialog_SetDefaultButton, NoSetup, cases);
    CompareSet1(ctx, "SetCancelButton", Original_CDialog_SetCancelButton, &CDialog_SetCancelButton, NoSetup, cases);
    CompareSet1(ctx, "SetHighlight", Original_CStaticText_SetHighlight, &CStaticText_SetHighlight, ElementSetup, cases);
    CompareSet1(ctx, "SetColor", Original_CStaticText_SetColor, &CStaticText_SetColor, ElementSetup, cases);
    CompareSet1(ctx, "SetImage", Original_CStaticImage_SetImage, &CStaticImage_SetImage, ElementSetup, cases);
    CompareSet2(ctx, "SetFrame", Original_CStaticFrame_SetFrame, &CStaticFrame_SetFrame, ElementSetup, cases);
    kit::Compare(
        ctx, "ColorIndexedSetFrame", cases, ElementSetup,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r), b = Arg(r);
            return Original_CStaticColorIndexedFrame_SetFrame(base + kThis, nullptr, a, b, Arg(r));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r), b = Arg(r);
            return CStaticColorIndexedFrame_SetFrame(base + kThis, nullptr, a, b, Arg(r));
        });
    kit::Compare(
        ctx, "SetImages", cases, VbSetup<4>,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r), b = Arg(r), c = Arg(r);
            return Original_CImageButton_SetImages(base + kThis, nullptr, a, b, c, Arg(r));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r), b = Arg(r), c = Arg(r);
            return CImageButton_SetImages(base + kThis, nullptr, a, b, c, Arg(r));
        });
    CompareSet1(ctx, "TextButtonSetStyle", Original_CTextButton_SetStyle, &CTextButton_SetStyle, VbSetup<4>, cases);
    CompareSet1(ctx, "EditOnFocusChanged", Original_CEditControl_OnFocusChanged, &CEditControl_OnFocusChanged,
                VbSetup<off::kEditVbPtr>, cases);
    CompareSet2(ctx, "InitDialogs", Original_CScreen_InitDialogs, &CScreen_InitDialogs, VbSetup<4>, cases);
    // SetMaxLines: the stored maximum sometimes equal to the argument (0..3).
    CompareSet1(ctx, "SetMaxLines", Original_CTextView_SetMaxLines, &CTextView_SetMaxLines,
                [](test::Rng& r, std::uint8_t* b) { kit::Put(b, kThis + off::kTextViewMaxLines, r.Below(4)); },
                cases);
    CompareGet(ctx, "TextViewClear", Original_CTextView_Clear, &CTextView_Clear, NoSetup, cases);
    CompareSet1(ctx, "TextViewScrollTo", Original_CTextView_ScrollTo, &CTextView_ScrollTo, VbSetup<4>, cases);
    CompareSet1(ctx, "ListViewScrollTo", Original_CListView_ScrollTo, &CListView_ScrollTo, VbSetup<4>, cases);
    CompareSet1(ctx, "SetTextId", Original_CStaticText_SetTextId, ours.setTextId, ElementSetup, cases);
    CompareSet1(ctx, "DropdownSetEnabled", Original_CDropdown_SetEnabled, &CDropdown_SetEnabled,
                [](test::Rng&, std::uint8_t* b) { kit::FakeObject(b, kThis + off::kDropdownList); }, cases);
    // NotifyRejectedChar: no functor, or a functor object with the fake vtable.
    CompareGet(ctx, "NotifyRejectedChar", Original_CEditControl_NotifyRejectedChar, &CEditControl_NotifyRejectedChar,
               [](test::Rng& r, std::uint8_t* b) {
                   kit::VbPtr(b, kThis + 4, kDisplacement, 0);
                   if (r.Chance(1, 3)) {
                       kit::Put(b, kThis + off::kEditRejectFunctor, 0);
                   } else {
                       kit::Link(b, kThis + off::kEditRejectFunctor, 0x2a00);
                       kit::FakeObject(b, 0x2a00);
                   }
               },
               cases);
    // Invoke: the object at 0x2000 (its vbptr at +4 -> a vbtable at
    // kVbTables + 8 with random small entries), the method one of the fake
    // virtual stubs (thiscall, 2 arguments: slot 11).
    CompareSet2(ctx, "FunctorInvoke", Original_CFunctor2_Invoke, ours.invoke,
                [](test::Rng& r, std::uint8_t* b) {
                    kit::Link(b, kThis + 4, 0x2000);
                    kit::Link(b, 0x2004, kit::kVbTables + 8);
                    for (int k = 0; k < 2; ++k) {
                        kit::Put(b, kit::kVbTables + 8 + 4 * k, static_cast<std::uint32_t>(r.Range(-0x80, 0x80)));
                    }
                    std::uint32_t method = 0;
                    std::memcpy(&method, b + kit::kVtable + 4 * 11, 4);
                    kit::Put(b, kThis + 8, method);
                    kit::Put(b, kThis + 0xc, static_cast<std::uint32_t>(r.Range(-0x40, 0x40)));
                    kit::Put(b, kThis + 0x10, 4 * r.Below(2));
                },
                cases);
    CompareSet2(ctx, "ListViewOnResized", Original_CListView_OnResized, &CListView_OnResized, NoSetup, cases);
    // The counts small (sometimes 0 or negative).
    auto counts = [](test::Rng& r, std::uint8_t* b) {
        kit::Put(b, kThis + off::kListViewColumnCount, static_cast<std::uint32_t>(r.Range(-2, 5)));
        kit::Put(b, kThis + off::kListViewLineCount, static_cast<std::uint32_t>(r.Range(-2, 5)));
    };
    CompareGet(ctx, "ListViewClear", Original_CListView_Clear, ours.clear, counts, cases);
    CompareGet(ctx, "RemoveAllLines", Original_CListView_RemoveAllLines, &CListView_RemoveAllLines, counts, cases);
    // GetCell: find/insert return a pointer into the buffer or 0; both 0
    // would read address 0 (as the original): those cases are skipped by
    // giving insert's 0 a pointer (the effect returns 0 only for find then).
    kit::Compare(
        ctx, "GetCell", cases, NoSetup,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r);
            return Original_CListView_GetCell(base + kThis, nullptr, a, Arg(r));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r);
            return ours.getCell(base + kThis, nullptr, a, Arg(r));
        },
        0xffffffffu, [](const kit::Call& call, const std::uint32_t* raw) -> std::uint32_t {
            if (call.callee == kSkipFind) {
                return AllocatingEffect(call, raw);
            }
            return static_cast<std::uint32_t>(kit::Current().base + kListBuffer + 0x40);
        });
    CompareSet1(ctx, "SkipListInsert", Original_WCSkipListBase_insert, &WCSkipListBase_insert, NoSetup, cases);
    auto smallCount = [](test::Rng& r, std::uint8_t* b) {
        kit::Put(b, kThis + off::kVectorEntries, r.Below(6));
    };
    CompareSet1(ctx, "ColumnResize", Original_WCValOrderedVector_CColumn_resize, &WCValOrderedVector_CColumn_resize,
                smallCount, cases);
    CompareSet1(ctx, "LineResize", Original_WCValOrderedVector_CLine_resize, &WCValOrderedVector_CLine_resize,
                smallCount, cases);
    // NewLink: the allocator a fake virtual stub (fastcall-compatible: it
    // takes ECX and pops nothing: slot 4, no arguments) or none.
    kit::Compare(
        ctx, "NewLink", cases,
        [](test::Rng& r, std::uint8_t* b) {
            if (r.Chance(1, 2)) {
                kit::Put(b, kThis + off::kListAllocator, 0);
            } else {
                std::uint32_t stub = 0;
                std::memcpy(&stub, b + kit::kVtable + 4 * 4, 4);
                kit::Put(b, kThis + off::kListAllocator, stub);
            }
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return Original_WCValSList_bool_NewLink(base + kThis, nullptr,
                                                    reinterpret_cast<const std::uint8_t*>(base + 0x100 + r.Below(8)));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return WCValSList_bool_NewLink(base + kThis, nullptr,
                                           reinterpret_cast<const std::uint8_t*>(base + 0x100 + r.Below(8)));
        },
        0xffffffffu, [](const kit::Call& call, const std::uint32_t* raw) -> std::uint32_t {
            if (call.callee == kit::kVirtualId + 16 || call.callee == kOperatorNew) {
                return (call.self + call.args[0]) % 4 == 0 ? 0
                                                           : static_cast<std::uint32_t>(kit::Current().base + kListBuffer);
            }
            return AllocatingEffect(call, raw);
        });
    // DDGetDC: the interface at 0x2400 (fake COM vtable; GetDC returns DD_OK,
    // still drawing or another code by the hash).
    auto surface = [](test::Rng&, std::uint8_t* b) {
        kit::Link(b, kThis + off::kSurfaceInterface, 0x2400);
        kit::FakeComObject(b, 0x2400);
    };
    auto hresults = [](const kit::Call& call, const std::uint32_t* raw) -> std::uint32_t {
        const std::uint32_t h = AllocatingEffect(call, raw);
        if (call.callee >= kit::kComId) {
            switch (h % 4) {
            case 0: return 0;
            case 1: return static_cast<std::uint32_t>(kWasStillDrawing);
            case 2: return static_cast<std::uint32_t>(kSurfaceLost);
            default: return h;
            }
        }
        return h;
    };
    kit::Compare(
        ctx, "DDGetDC", cases, surface,
        [&](std::uintptr_t base, test::Rng& r) {
            return static_cast<std::uint32_t>(Original_DirectX_CSurface_DDGetDC(base + kThis, nullptr, Arg(r)));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return static_cast<std::uint32_t>(DirectX_CSurface_DDGetDC(base + kThis, nullptr, Arg(r)));
        },
        0xffffffffu, hresults);
    kit::Compare(
        ctx, "ReleaseDC", cases, surface,
        [&](std::uintptr_t base, test::Rng& r) {
            return static_cast<std::uint32_t>(Original_CScreenElement_ReleaseDC(Arg(r), nullptr, base + kThis, Arg(r)));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return static_cast<std::uint32_t>(ours.releaseDC(Arg(r), nullptr, base + kThis, Arg(r)));
        },
        0xffffffffu, hresults);
    // PostMessage: no parent, or a parent at 0x2200 whose vbptr (+4) leads
    // to an element with the fake vtable.
    kit::Compare(
        ctx, "PostMessage", cases,
        [](test::Rng& r, std::uint8_t* b) {
            if (r.Chance(1, 4)) {
                kit::Put(b, kThis + offsetof(ui::CScreenElement, parent), 0);
            } else {
                kit::Link(b, kThis + offsetof(ui::CScreenElement, parent), 0x2200);
                kit::VbPtr(b, 0x2204, 0x40, 1);
            }
        },
        [&](std::uintptr_t base, test::Rng& r) {
            Message m{};
            for (auto& w : m.words) {
                w = Arg(r);
            }
            return Original_CScreenElement_PostMessage(base + kThis, nullptr, m);
        },
        [&](std::uintptr_t base, test::Rng& r) {
            Message m{};
            for (auto& w : m.words) {
                w = Arg(r);
            }
            return CScreenElement_PostMessage(base + kThis, nullptr, m);
        });
}

// Wrong implementations (one per harness check).
std::uint32_t __fastcall WrongSetTextId(std::uintptr_t self, void* edx, std::uint32_t id) {
    return CStaticText_SetTextId(self, edx, id + 1);
}
std::uint32_t __fastcall WrongInvoke(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b) {
    return CFunctor2_Invoke(self, edx, b, a);  // arguments swapped
}
std::uint32_t __fastcall WrongClear(std::uintptr_t self, void* edx) {
    Field<std::int32_t>(self, off::kListViewColumnCount) -= 1;  // one column left
    return CListView_Clear(self, edx);
}
std::uint32_t __fastcall WrongGetCell(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column) {
    return CListView_GetCell(self, edx, column, line);
}
std::int32_t __fastcall WrongReleaseDC(std::uintptr_t self, void* edx, std::uintptr_t surface, std::uint32_t dc) {
    using ReleaseFn = std::int32_t(__stdcall*)(std::uint32_t iface, std::uint32_t dc);
    const std::uint32_t iface = Field<std::uint32_t>(surface, off::kSurfaceInterface);
    const std::int32_t hr = Slot<ReleaseFn>(iface, kReleaseDCSlot)(iface, dc);  // no retry
    (void)self;
    (void)edx;
    return reinterpret_cast<CheckResultFn>(kCheckResult)(surface, nullptr, hr, texts::kDDReleaseDC);
}

}  // namespace

S3RE_TEST(UiDialogSetters_MatchOriginal) {
    CompareAll(ctx, Members{}, 400);
}

S3RE_TEST(UiDialogSetters_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 5; ++k) {
        Members wrong;
        switch (k) {
        case 0: wrong.setTextId = &WrongSetTextId; break;
        case 1: wrong.invoke = &WrongInvoke; break;
        case 2: wrong.clear = &WrongClear; break;
        case 3: wrong.getCell = &WrongGetCell; break;
        default: wrong.releaseDC = &WrongReleaseDC; break;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
