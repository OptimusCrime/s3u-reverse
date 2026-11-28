// Selection and cell lookup of the list controls (Dirk\dialog.cpp,
// wip/dialog-r8). All thiscall; layouts in re/ui_classes.md.
//
// CListView::GetCellEdit / GetCellStaticText / GetCellImageButton /
//   GetCellStaticFrame / GetCellUpDown (0x00424eb0 / 0x00424f10 /
//   0x00424f70 / 0x00424fd0 / 0x00425030): (line, col) ret 8: the cell
//   lookup of CListView::GetCell 0x424e60 inlined (key line << 16 + col at
//   +0x178, base_find 0x60bab0, else base_insert 0x60bbe0; both failing
//   reads address 0), then dynamic_cast from CScreenElement to the class
//   (__RTDynamicCast 0x61f9f0(cell, 0, CScreenElement, class, 0), the
//   CRT's). EAX = the cast's.
// CListView::SelectLine (0x00425090): (line, bool scrollIntoView) ret 8:
//   CListView::SetSelection 0x426f60(line); if asked and the selection
//   (+0x194) is not -1 and not within [top (+0x190), top + visible lines
//   (+0x188)) (signed; the sum wraps): ScrollTo 0x425110(max(0, selection -
//   min(visible, 2))) (the sign of the wrapped difference decides). Then
//   Invalidate through the vbptr +4. EAX = Invalidate's.
// CListBox::SetSelection (0x0041f3b0): (index, bool ensureVisible) ret 8:
//   +0x418 = index, then the same as SelectLine with top +0x410, visible
//   +0x414 and the scroll call 0x420d60 (the folded CTextView::ScrollTo:
//   the list box's scroll bar is at +0xc0 too).
// CListBox::SelectString (0x0041f4f0): (const string&, bool) ret 8: with
//   items (+0x434, the vector +0x420's entry count): the vector's find
//   (slot 3: (string, 1, &index)); found: SetSelection 0x41f3b0(index,
//   bool), otherwise (or without items) SetSelection(-1, bool). EAX = its.

#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kSkipFind = 0x0060bab0;          // WCSkipNonTempBase::base_find
constexpr std::uintptr_t kSkipInsert = 0x0060bbe0;        // WCSkipNonTempBase::base_insert
constexpr std::uintptr_t kDynamicCast = 0x0061f9f0;       // __RTDynamicCast (CRT)
constexpr std::uintptr_t kListViewSetSelection = 0x00426f60;  // CListView::SetSelection
constexpr std::uintptr_t kListViewScrollTo = 0x00425110;  // CListView::ScrollTo (ours)
constexpr std::uintptr_t kListBoxScrollTo = 0x00420d60;   // CTextView::ScrollTo (ours), folded with the list box's
constexpr std::uintptr_t kListBoxSetSelection = 0x0041f3b0;  // CListBox::SetSelection (ours)

constexpr std::uint32_t kVectorFind = 0x0c;  // WCValOrderedVector slot 3: find(value, occurrence, &index)
constexpr std::int32_t kScrollMargin = 2;    // lines kept visible above a selection scrolled to
constexpr std::int32_t kNoSelection = -1;

namespace off {
constexpr std::int32_t kListViewCells = 0x100;
constexpr std::int32_t kCellsSearchKey = 0x78;
constexpr std::int32_t kListViewVisible = 0x188;
constexpr std::int32_t kListViewTopLine = 0x190;
constexpr std::int32_t kListViewSelection = 0x194;
constexpr std::int32_t kListBoxTop = 0x410;
constexpr std::int32_t kListBoxVisible = 0x414;
constexpr std::int32_t kListBoxSelection = 0x418;
constexpr std::int32_t kListBoxItems = 0x420;
constexpr std::int32_t kListBoxItemCount = 0x434;
}  // namespace off

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

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
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

std::uint32_t Invalidate(std::uintptr_t element) {
    return Slot<Thiscall0>(element, ui::element_slot::kInvalidate)(element, nullptr);
}

// CListView::GetCell's lookup (0x424e60, inlined in the typed getters).
std::uint32_t LookUpCell(std::uintptr_t self, std::uint32_t line, std::uint32_t column) {
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

std::uint32_t CellAs(std::uintptr_t self, std::uint32_t line, std::uint32_t column, std::uintptr_t type) {
    using DynamicCastFn = std::uint32_t(__cdecl*)(std::uint32_t object, std::int32_t vfDelta, std::uintptr_t source,
                                                  std::uintptr_t target, std::int32_t isReference);
    const std::uint32_t cell = LookUpCell(self, line, column);
    return reinterpret_cast<DynamicCastFn>(kDynamicCast)(cell, 0, type_descriptor::kCScreenElement, type, 0);
}

// The scroll-into-view rule of the list box and list view selections.
template <typename Scroll>
void EnsureVisible(std::uintptr_t self, std::int32_t selectionOffset, std::int32_t topOffset,
                   std::int32_t visibleOffset, Scroll scroll) {
    const std::int32_t selection = Field<std::int32_t>(self, selectionOffset);
    if (selection == kNoSelection) {
        return;
    }
    const std::int32_t top = Field<std::int32_t>(self, topOffset);
    const std::int32_t visible = Field<std::int32_t>(self, visibleOffset);
    const auto end = static_cast<std::int32_t>(static_cast<std::uint32_t>(top) + static_cast<std::uint32_t>(visible));
    if (selection >= top && selection < end) {
        return;
    }
    const std::int32_t margin = visible > kScrollMargin ? kScrollMargin : visible;
    // The original tests the sign of the wrapped difference.
    const auto target = static_cast<std::int32_t>(static_cast<std::uint32_t>(selection) - static_cast<std::uint32_t>(margin));
    scroll(target < 0 ? 0u : static_cast<std::uint32_t>(target));
}

}  // namespace

using CellFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column);
using SelectFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t index, std::uint32_t flag);

// Purpose: The edit field in a list view cell (null if the cell is another control).
std::uint32_t __fastcall CListView_GetCellEdit(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t column) {
    return CellAs(self, line, column, type_descriptor::kCEditControl);
}

// Purpose: The text in a list view cell (null if the cell is another control).
std::uint32_t __fastcall CListView_GetCellStaticText(std::uintptr_t self, void*, std::uint32_t line,
                                                     std::uint32_t column) {
    return CellAs(self, line, column, type_descriptor::kCStaticText);
}

// Purpose: The image button in a list view cell (null if the cell is another control).
std::uint32_t __fastcall CListView_GetCellImageButton(std::uintptr_t self, void*, std::uint32_t line,
                                                      std::uint32_t column) {
    return CellAs(self, line, column, type_descriptor::kCImageButton);
}

// Purpose: The sprite frame in a list view cell (null if the cell is another control).
std::uint32_t __fastcall CListView_GetCellStaticFrame(std::uintptr_t self, void*, std::uint32_t line,
                                                      std::uint32_t column) {
    return CellAs(self, line, column, type_descriptor::kCStaticFrame);
}

// Purpose: The up-down number field in a list view cell (null if the cell is another control).
std::uint32_t __fastcall CListView_GetCellUpDown(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t column) {
    return CellAs(self, line, column, type_descriptor::kCUpDown);
}

// Purpose: Selects a line of a list view and optionally scrolls it into view.
std::uint32_t __fastcall CListView_SelectLine(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t scroll) {
    reinterpret_cast<Thiscall1>(kListViewSetSelection)(self, nullptr, line);
    if (static_cast<std::uint8_t>(scroll) != 0) {
        EnsureVisible(self, off::kListViewSelection, off::kListViewTopLine, off::kListViewVisible,
                      [&](std::uint32_t top) { reinterpret_cast<Thiscall1>(kListViewScrollTo)(self, nullptr, top); });
    }
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Selects an item of a list box (-1: none) and optionally scrolls it into view.
std::uint32_t __fastcall CListBox_SetSelection(std::uintptr_t self, void*, std::uint32_t index,
                                               std::uint32_t ensureVisible) {
    Field<std::uint32_t>(self, off::kListBoxSelection) = index;
    if (static_cast<std::uint8_t>(ensureVisible) != 0) {
        EnsureVisible(self, off::kListBoxSelection, off::kListBoxTop, off::kListBoxVisible,
                      [&](std::uint32_t top) { reinterpret_cast<Thiscall1>(kListBoxScrollTo)(self, nullptr, top); });
    }
    return Invalidate(VirtualBase(self, 4));
}

// Purpose: Selects the list box item with the given text (none if it isn't there).
std::uint32_t __fastcall CListBox_SelectString(std::uintptr_t self, void*, std::uint32_t text,
                                               std::uint32_t ensureVisible) {
    std::uint32_t index = 0;
    std::uint32_t found = 0;
    if (Field<std::uint32_t>(self, off::kListBoxItemCount) != 0) {
        const std::uintptr_t items = self + off::kListBoxItems;
        const auto indexAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&index));
        found = Slot<Thiscall3>(items, kVectorFind)(items, nullptr, text, 1, indexAddress);
    }
    const std::uint32_t selection = found != 0 ? index : static_cast<std::uint32_t>(kNoSelection);
    return reinterpret_cast<Thiscall2>(kListBoxSetSelection)(self, nullptr, selection, ensureVisible);
}

S3RE_HOOK(0x00424eb0, CListView_GetCellEdit, CellFn, Original_CListView_GetCellEdit, CListView_GetCellEdit);
S3RE_HOOK(0x00424f10, CListView_GetCellStaticText, CellFn, Original_CListView_GetCellStaticText,
          CListView_GetCellStaticText);
S3RE_HOOK(0x00424f70, CListView_GetCellImageButton, CellFn, Original_CListView_GetCellImageButton,
          CListView_GetCellImageButton);
S3RE_HOOK(0x00424fd0, CListView_GetCellStaticFrame, CellFn, Original_CListView_GetCellStaticFrame,
          CListView_GetCellStaticFrame);
S3RE_HOOK(0x00425030, CListView_GetCellUpDown, CellFn, Original_CListView_GetCellUpDown, CListView_GetCellUpDown);
S3RE_HOOK(0x00425090, CListView_SelectLine, SelectFn, Original_CListView_SelectLine, CListView_SelectLine);
S3RE_HOOK(0x0041f3b0, CListBox_SetSelection, SelectFn, Original_CListBox_SetSelection, CListBox_SetSelection);
S3RE_HOOK(0x0041f4f0, CListBox_SelectString, SelectFn, Original_CListBox_SelectString, CListBox_SelectString);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

constexpr std::size_t kLinks = 0x2c00;

// find/insert: a pointer into the buffer or 0 (both 0 together never: that
// reads address 0); the vector's find writes an index; the rest a hash.
std::uint32_t Effect(const kit::Call& call, const std::uint32_t* raw) {
    std::uint32_t hash = call.callee * 2654435761u;
    for (std::uint32_t i = 0; i < call.count; ++i) {
        hash = (hash ^ call.args[i]) * 16777619u;
    }
    const auto base = static_cast<std::uint32_t>(kit::Current().base);
    if (call.callee == kSkipFind) {
        return hash % 3 == 0 ? 0 : base + kLinks + 4 * (hash % 8);
    }
    if (call.callee == kSkipInsert) {
        return base + kLinks + 0x40;
    }
    if (call.callee == kit::kVirtual3Id + kVectorFind) {
        *reinterpret_cast<std::uint32_t*>(raw[2]) = hash % 5;
        return (hash >> 4) % 3 == 0 ? 0 : hash;
    }
    return hash ^ (hash >> 13);
}

std::uint32_t Arg(test::Rng& rng) {
    return rng.Chance(1, 2) ? rng.Below(6) : rng.Next32();
}

// The selection fields: small values, -1, near the top and the edges.
void SelectionFields(test::Rng& r, std::uint8_t* b, std::int32_t selection, std::int32_t top, std::int32_t visible) {
    kit::VbPtr(b, kThis + 4, 0x100, 0);
    const std::int32_t t = r.Chance(1, 8) ? static_cast<std::int32_t>(r.Next32()) : r.Range(-3, 8);
    kit::Put(b, kThis + top, static_cast<std::uint32_t>(t));
    kit::Put(b, kThis + visible, static_cast<std::uint32_t>(r.Chance(1, 8) ? r.Next32() : r.Range(-2, 5)));
    const std::int32_t s = r.Chance(1, 6) ? -1 : r.Chance(1, 8) ? static_cast<std::int32_t>(r.Next32()) : t + r.Range(-4, 8);
    kit::Put(b, kThis + selection, static_cast<std::uint32_t>(s));
}

struct Members {
    CellFn cells[5] = {&CListView_GetCellEdit, &CListView_GetCellStaticText, &CListView_GetCellImageButton,
                       &CListView_GetCellStaticFrame, &CListView_GetCellUpDown};
    SelectFn selectLine = &CListView_SelectLine;
    SelectFn listSelect = &CListBox_SetSelection;
    SelectFn selectString = &CListBox_SelectString;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kSkipFind, 1>()
        .This<kSkipInsert, 1>()
        .Cdecl<kDynamicCast, 5>()
        .This<kListViewSetSelection, 1>()
        .This<kListViewScrollTo, 1>()
        .This<kListBoxScrollTo, 1>()
        .This<kListBoxSetSelection, 2>();
    const CellFn originals[5] = {Original_CListView_GetCellEdit, Original_CListView_GetCellStaticText,
                                 Original_CListView_GetCellImageButton, Original_CListView_GetCellStaticFrame,
                                 Original_CListView_GetCellUpDown};
    auto none = [](test::Rng&, std::uint8_t*) {};
    for (int k = 0; k < 5; ++k) {
        kit::Compare(
            ctx, "GetCellAs", cases, none,
            [&](std::uintptr_t base, test::Rng& r) {
                const std::uint32_t line = Arg(r);
                return originals[k](base + kThis, nullptr, line, Arg(r));
            },
            [&](std::uintptr_t base, test::Rng& r) {
                const std::uint32_t line = Arg(r);
                return ours.cells[k](base + kThis, nullptr, line, Arg(r));
            },
            0xffffffffu, &Effect);
    }
    auto call2 = [](SelectFn fn) {
        return [fn](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t a = Arg(r);
            return fn(base + kThis, nullptr, a, r.Chance(1, 4) ? 0x100 : Arg(r));
        };
    };
    kit::Compare(
        ctx, "SelectLine", cases,
        [](test::Rng& r, std::uint8_t* b) {
            SelectionFields(r, b, off::kListViewSelection, off::kListViewTopLine, off::kListViewVisible);
        },
        call2(Original_CListView_SelectLine), call2(ours.selectLine), 0xffffffffu, &Effect);
    // The list box stores the index itself: it is drawn like a selection.
    auto listBoxCall = [](SelectFn fn) {
        return [fn](std::uintptr_t base, test::Rng& r) {
            const std::int32_t top = *reinterpret_cast<const std::int32_t*>(base + kThis + off::kListBoxTop);
            const std::uint32_t index = r.Chance(1, 6)   ? 0xffffffffu
                                        : r.Chance(1, 8) ? r.Next32()
                                                         : static_cast<std::uint32_t>(top + r.Range(-4, 8));
            return fn(base + kThis, nullptr, index, r.Chance(1, 4) ? 0x100 : Arg(r));
        };
    };
    kit::Compare(
        ctx, "ListBoxSetSelection", cases,
        [](test::Rng& r, std::uint8_t* b) {
            SelectionFields(r, b, off::kListBoxSelection, off::kListBoxTop, off::kListBoxVisible);
        },
        listBoxCall(Original_CListBox_SetSelection), listBoxCall(ours.listSelect), 0xffffffffu, &Effect);
    kit::Compare(
        ctx, "SelectString", cases,
        [](test::Rng& r, std::uint8_t* b) {
            kit::FakeObject3(b, kThis + off::kListBoxItems);
            kit::Put(b, kThis + off::kListBoxItemCount, r.Chance(1, 4) ? 0 : r.Below(5) + 1);
        },
        [](std::uintptr_t base, test::Rng& r) {
            return Original_CListBox_SelectString(base + kThis, nullptr, static_cast<std::uint32_t>(base + 0x40),
                                                  Arg(r));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return ours.selectString(base + kThis, nullptr, static_cast<std::uint32_t>(base + 0x40), Arg(r));
        },
        0xffffffffu, &Effect);
}

std::uint32_t __fastcall WrongCell(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column) {
    return CListView_GetCellStaticText(self, edx, line, column);  // the wrong type
}
std::uint32_t __fastcall WrongSelectLine(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t scroll) {
    // Margin 1 instead of 2.
    reinterpret_cast<Thiscall1>(kListViewSetSelection)(self, nullptr, line);
    if (static_cast<std::uint8_t>(scroll) != 0) {
        const std::int32_t selection = Field<std::int32_t>(self, off::kListViewSelection);
        const std::int32_t top = Field<std::int32_t>(self, off::kListViewTopLine);
        const std::int32_t visible = Field<std::int32_t>(self, off::kListViewVisible);
        if (selection != -1 && (selection < top || selection >= top + visible)) {
            reinterpret_cast<Thiscall1>(kListViewScrollTo)(self, nullptr, selection < 1 ? 0 : selection - 1);
        }
    }
    (void)edx;
    return Invalidate(VirtualBase(self, 4));
}
std::uint32_t __fastcall WrongSelectString(std::uintptr_t self, void* edx, std::uint32_t text, std::uint32_t flag) {
    std::uint32_t index = 0;
    const std::uintptr_t items = self + off::kListBoxItems;
    const auto indexAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&index));
    const std::uint32_t found = Slot<Thiscall3>(items, kVectorFind)(items, nullptr, text, 1, indexAddress);  // no count check
    (void)edx;
    return reinterpret_cast<Thiscall2>(kListBoxSetSelection)(self, nullptr, found != 0 ? index : 0xffffffffu, flag);
}

}  // namespace

S3RE_TEST(UiListSelection_MatchOriginal) {
    CompareAll(ctx, Members{}, 500);
}

S3RE_TEST(UiListSelection_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 3; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.cells[0] = &WrongCell;
        } else if (k == 1) {
            wrong.selectLine = &WrongSelectLine;
        } else {
            wrong.selectString = &WrongSelectString;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
