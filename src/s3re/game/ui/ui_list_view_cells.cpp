// CListView cell setters by column and the scroll bar layout (Dirk\dialog.cpp,
// wip/dialog-r8). All thiscall on the list view (its start; vbptr +4 to the
// CScreenElement part). Layouts in re/ui_classes.md.
//
// The column vector (+0xc8, WCValOrderedVector<CColumn>, entries of 0x20
// bytes, type at +4) is indexed with the Watcom operator[] check inlined
// (ColumnIndex below; the library's error hooks are the empty function
// 0x59f200, so they are not called here): in range (0 <= i <= length - 1,
// signed) i; out of range on a non-empty vector 0 or length - 1; on an
// empty one: with capacity 0 resize(1) (WCBareVectorBase<CColumn>::resize
// 0x429a80; on failure index 0 at once), else / then length clamped to 1,
// the vector's slot 1 with 0, length = 1, index 0.
//
// CListView::SetCellUpDown (0x00424c70): (line, col, min, max, value) ret
//   0x14: if the column's type is 6: the cell (CListView::GetCell's lookup
//   inlined: key line << 16 + col at +0x178, base_find 0x60bab0 / base_insert
//   0x60bbe0, both failing reads address 0) dynamic_cast to CUpDown, then
//   CUpDown::SetRange 0x422bf0(min, max) and SetValue 0x422b90(value) on
//   it (no null check). EAX is not used by any caller (left out).
// CListView::SetCellProgress (0x00424d70): (line, col, maximum, value) ret
//   0x10: the same for type 7 and CProgressBar::SetProgress 0x423160(maximum,
//   value). EAX not used.
// CListView::SetColumnTooltip (0x00423fe0): (col, textId) ret 8: the
//   column's tooltip id (CColumn::SetTooltipTextId 0x4276a0), then for every
//   line (count +0xf8 re-read each time) the cell (key (line << 16) + col)
//   dynamic_cast to CControl gets CControl::SetTooltipTextId 0x418330(id).
//   EAX not used.
// CListView::UpdateScrollBar (0x00425b50): CVScrollBar::SetRange 0x41e380
//   (top line +0x190, line count +0xf8, fully visible +0x18c) on the scroll
//   bar (+0x198); y = the element's top, plus the header height (+0x180)
//   with headers shown (+0x4f2). All lines fit (count <= fully visible,
//   unsigned) and top <= 0 (signed): client rect (+0x4e0) = {left + 4, y + 4,
//   right - 4, bottom - 4}, scroll bar hidden (+0x4f0 = 0). Otherwise the
//   client's right is right - 16, the bar placed at {right - 12, y, right,
//   bottom} (CScreenElement::SetRect 0x4156e0 on its element, vbptr +0x19c)
//   and shown (+0x4f0 = 1). Then CScreenGroup::ShowChild 0x417090 on the
//   rows group (+0x38) with (shown, the bar's element). The element's rect
//   is read and the client rect written in the original's order. EAX =
//   ShowChild's.
// CListView::LayoutColumns (0x00425150): x = the client's left (+0x4e0);
//   unless x > the client's right (+0x4e8, signed, re-read): for each column
//   (count +0xdc, unsigned) its header placed (CColumn::SetPosition 0x427650
//   (x, the element's top, header height +0x180)) and x += its width (+8),
//   until x passes the right edge. Both accesses of a column go through the
//   operator[] check (width first). EAX not used.

#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kSkipFind = 0x0060bab0;           // WCSkipNonTempBase::base_find
constexpr std::uintptr_t kSkipInsert = 0x0060bbe0;         // WCSkipNonTempBase::base_insert
constexpr std::uintptr_t kDynamicCast = 0x0061f9f0;        // __RTDynamicCast (CRT)
constexpr std::uintptr_t kColumnBareResize = 0x00429a80;   // WCBareVectorBase<CColumn>::resize
constexpr std::uintptr_t kUpDownSetRange = 0x00422bf0;     // CUpDown::SetRange (ours)
constexpr std::uintptr_t kUpDownSetValue = 0x00422b90;     // CUpDown::SetValue (ours)
constexpr std::uintptr_t kProgressSetProgress = 0x00423160;  // CProgressBar::SetProgress
constexpr std::uintptr_t kColumnSetTooltip = 0x004276a0;   // CListView::CColumn::SetTooltipTextId (ours)
constexpr std::uintptr_t kControlSetTooltip = 0x00418330;  // CControl::SetTooltipTextId (ours)
constexpr std::uintptr_t kScrollSetRange = 0x0041e380;     // CVScrollBar::SetRange (ours)
constexpr std::uintptr_t kElementSetRect = 0x004156e0;     // CScreenElement::SetRect (ours)
constexpr std::uintptr_t kGroupShowChild = 0x00417090;     // CScreenGroup::ShowChild (ours)
constexpr std::uintptr_t kColumnSetPosition = 0x00427650;  // CListView::CColumn::SetPosition (ours)

constexpr std::uint32_t kColumnUpDown = 6;    // CColumn type: CUpDown cells
constexpr std::uint32_t kColumnProgress = 7;  // CColumn type: CProgressBar cells
constexpr std::uint32_t kColumnBytes = 0x20;
constexpr std::uint32_t kVectorLengthChanged = 0x04;  // the vector's slot 1
constexpr LONG kClientInset = 4;
constexpr LONG kScrollBarWidth = 12;
constexpr LONG kClientRightWithBar = 16;  // the client's right edge, inset from the right with a scroll bar

namespace off {
constexpr std::int32_t kColumns = 0xc8;          // WCValOrderedVector<CColumn>
constexpr std::int32_t kRowsGroup = 0x38;
constexpr std::int32_t kLineCount = 0xf8;
constexpr std::int32_t kCells = 0x100;
constexpr std::int32_t kCellsSearchKey = 0x78;
constexpr std::int32_t kHeaderHeight = 0x180;
constexpr std::int32_t kFullyVisible = 0x18c;
constexpr std::int32_t kTopLine = 0x190;
constexpr std::int32_t kScrollBar = 0x198;
constexpr std::int32_t kScrollBarVbPtr = 0x19c;
constexpr std::int32_t kClientRect = 0x4e0;
constexpr std::int32_t kScrollBarShown = 0x4f0;
constexpr std::int32_t kHeadersShown = 0x4f2;
constexpr std::int32_t kColumnType = 4;          // in a CColumn
constexpr std::int32_t kColumnWidth = 8;         // in a CColumn
constexpr std::int32_t kColumnCount = 0xdc;      // the column vector's length
}  // namespace off

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

std::int32_t Displacement(std::uintptr_t self, std::int32_t vbptr) {
    return (*reinterpret_cast<const std::int32_t* const*>(self + vbptr))[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using Thiscall3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

// The inlined operator[] check of the column vector (see the header).
std::uint32_t ColumnIndex(std::uintptr_t columns, std::int32_t index) {
    auto& length = Field<std::uint32_t>(columns, 0x14);
    const auto last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return static_cast<std::uint32_t>(index);
    }
    if (length != 0) {
        return index < 0 ? 0u : static_cast<std::uint32_t>(last);
    }
    if (Field<std::uint32_t>(columns, 0x0c) == 0) {
        if (reinterpret_cast<Thiscall1>(kColumnBareResize)(columns, nullptr, 1) == 0) {
            return 0;
        }
        if (length > 1) {
            length = 1;
        }
    }
    Slot<Thiscall1>(columns, kVectorLengthChanged)(columns, nullptr, 0);
    length = 1;
    return 0;
}

std::uintptr_t Column(std::uintptr_t self, std::int32_t index) {
    const std::uintptr_t columns = self + off::kColumns;
    const std::uint32_t i = ColumnIndex(columns, index);
    return Field<std::uint32_t>(columns, 8) + i * kColumnBytes;
}

// CListView::GetCell's lookup (0x424e60), inlined.
std::uint32_t LookUpCell(std::uintptr_t self, std::uint32_t key) {
    const std::uintptr_t cells = self + off::kCells;
    std::uint32_t& searchKey = Field<std::uint32_t>(cells, off::kCellsSearchKey);
    searchKey = key;
    const auto keyAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&searchKey));
    std::uint32_t found = reinterpret_cast<Thiscall1>(kSkipFind)(cells, nullptr, keyAddress);
    if (found == 0) {
        found = reinterpret_cast<Thiscall1>(kSkipInsert)(cells, nullptr, keyAddress);
        if (found == 0) {
            return *reinterpret_cast<volatile std::uint32_t*>(found);  // reads address 0, as the original
        }
    }
    return Field<std::uint32_t>(found, -4);
}

std::uint32_t CellAs(std::uintptr_t self, std::uint32_t key, std::uintptr_t type) {
    using DynamicCastFn = std::uint32_t(__cdecl*)(std::uint32_t object, std::int32_t vfDelta, std::uintptr_t source,
                                                  std::uintptr_t target, std::int32_t isReference);
    const std::uint32_t cell = LookUpCell(self, key);
    return reinterpret_cast<DynamicCastFn>(kDynamicCast)(cell, 0, type_descriptor::kCScreenElement, type, 0);
}

}  // namespace

using SetUpDownFn = void(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column,
                                      std::uint32_t minimum, std::uint32_t maximum, std::uint32_t value);
using SetProgressFn = void(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column,
                                        std::uint32_t maximum, std::uint32_t value);
using SetTooltipFn = void(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t column, std::uint32_t textId);
using UpdateFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

// Purpose: Sets the range and value of the up-down field in a list view cell.
void __fastcall CListView_SetCellUpDown(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t column,
                                        std::uint32_t minimum, std::uint32_t maximum, std::uint32_t value) {
    const std::uintptr_t entry = Column(self, static_cast<std::int32_t>(column));
    if (Field<std::uint32_t>(entry, off::kColumnType) != kColumnUpDown) {
        return;
    }
    const std::uint32_t upDown = CellAs(self, (line << 16) + column, type_descriptor::kCUpDown);
    reinterpret_cast<Thiscall2>(kUpDownSetRange)(upDown, nullptr, minimum, maximum);
    reinterpret_cast<Thiscall1>(kUpDownSetValue)(upDown, nullptr, value);
}

// Purpose: Sets the progress shown in a list view cell (e.g. a player's loading progress).
void __fastcall CListView_SetCellProgress(std::uintptr_t self, void*, std::uint32_t line, std::uint32_t column,
                                          std::uint32_t maximum, std::uint32_t value) {
    const std::uintptr_t entry = Column(self, static_cast<std::int32_t>(column));
    if (Field<std::uint32_t>(entry, off::kColumnType) != kColumnProgress) {
        return;
    }
    const std::uint32_t bar = CellAs(self, (line << 16) + column, type_descriptor::kCProgressBar);
    reinterpret_cast<Thiscall2>(kProgressSetProgress)(bar, nullptr, maximum, value);
}

// Purpose: Gives a list view column (its header and every cell) a tooltip text.
void __fastcall CListView_SetColumnTooltip(std::uintptr_t self, void*, std::uint32_t column, std::uint32_t textId) {
    const std::uintptr_t entry = Column(self, static_cast<std::int32_t>(column));
    reinterpret_cast<Thiscall1>(kColumnSetTooltip)(entry, nullptr, textId);
    std::uint32_t key = column;
    for (std::uint32_t line = 0; line < Field<std::uint32_t>(self, off::kLineCount); ++line, key += 0x10000) {
        const std::uint32_t control = CellAs(self, key, type_descriptor::kCControl);
        reinterpret_cast<Thiscall1>(kControlSetTooltip)(control, nullptr, textId);
    }
}

// Purpose: Shows the list view's scroll bar only when not all lines fit,
// and sizes the client area to match.
std::uint32_t __fastcall CListView_UpdateScrollBar(std::uintptr_t self, void*) {
    const std::uintptr_t scrollBar = self + off::kScrollBar;
    const std::int32_t elementTop = Field<LONG>(self + 4 + Displacement(self, 4), offsetof(ui::CScreenElement, rect) + 4);
    reinterpret_cast<Thiscall3>(kScrollSetRange)(scrollBar, nullptr, Field<std::uint32_t>(self, off::kTopLine),
                                                 Field<std::uint32_t>(self, off::kLineCount),
                                                 Field<std::uint32_t>(self, off::kFullyVisible));
    LONG y = elementTop;
    if (Field<std::uint8_t>(self, off::kHeadersShown) != 0) {
        y += Field<LONG>(self, off::kHeaderHeight);
    }
    // The element's rect, read afresh each time (as the original).
    auto rect = [&]() -> const RECT& {
        return Field<RECT>(self + 4 + Displacement(self, 4), offsetof(ui::CScreenElement, rect));
    };
    RECT& client = Field<RECT>(self, off::kClientRect);
    const bool allFit = Field<std::uint32_t>(self, off::kLineCount) <= Field<std::uint32_t>(self, off::kFullyVisible) &&
                        Field<std::int32_t>(self, off::kTopLine) <= 0;
    const LONG left = rect().left;
    client.top = y + kClientInset;
    client.left = left + kClientInset;
    if (allFit) {
        client.right = rect().right - kClientInset;
        const LONG bottom = rect().bottom;
        Field<std::uint8_t>(self, off::kScrollBarShown) = 0;
        client.bottom = bottom - kClientInset;
    } else {
        client.right = rect().right - kClientRightWithBar;
        client.bottom = rect().bottom - kClientInset;
        const LONG right = rect().right;
        const LONG bottom = rect().bottom;
        const RECT bar{right - kScrollBarWidth, y, right, bottom};
        const std::uintptr_t barElement = self + off::kScrollBarVbPtr + Displacement(self, off::kScrollBarVbPtr);
        reinterpret_cast<Thiscall1>(kElementSetRect)(barElement, nullptr,
                                                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bar)));
        Field<std::uint8_t>(self, off::kScrollBarShown) = 1;
    }
    // The bar's element (a null scroll bar would give 0: dead, it is a member).
    const std::uintptr_t barElement =
        scrollBar == 0 ? 0 : self + off::kScrollBarVbPtr + Displacement(self, off::kScrollBarVbPtr);
    return reinterpret_cast<Thiscall2>(kGroupShowChild)(self + off::kRowsGroup, nullptr,
                                                        Field<std::uint8_t>(self, off::kScrollBarShown),
                                                        static_cast<std::uint32_t>(barElement));
}

// Purpose: Places the column headers of a list view left to right.
void __fastcall CListView_LayoutColumns(std::uintptr_t self, void*) {
    const RECT& client = Field<RECT>(self, off::kClientRect);
    LONG x = client.left;
    if (x > client.right) {
        return;
    }
    for (std::uint32_t i = 0; i < Field<std::uint32_t>(self, off::kColumnCount); ++i) {
        const LONG start = x;
        x += Field<LONG>(Column(self, static_cast<std::int32_t>(i)), off::kColumnWidth);
        const std::uintptr_t column = Column(self, static_cast<std::int32_t>(i));
        const auto height = Field<std::uint32_t>(self, off::kHeaderHeight);
        const LONG top = Field<LONG>(self + 4 + Displacement(self, 4), offsetof(ui::CScreenElement, rect) + 4);
        reinterpret_cast<Thiscall3>(kColumnSetPosition)(column, nullptr, static_cast<std::uint32_t>(start),
                                                        static_cast<std::uint32_t>(top), height);
        if (x > client.right) {
            break;
        }
    }
}

using LayoutFn = void(__fastcall*)(std::uintptr_t self, void* edx);
S3RE_HOOK(0x00425150, CListView_LayoutColumns, LayoutFn, Original_CListView_LayoutColumns, CListView_LayoutColumns);
S3RE_HOOK(0x00424c70, CListView_SetCellUpDown, SetUpDownFn, Original_CListView_SetCellUpDown, CListView_SetCellUpDown);
S3RE_HOOK(0x00424d70, CListView_SetCellProgress, SetProgressFn, Original_CListView_SetCellProgress,
          CListView_SetCellProgress);
S3RE_HOOK(0x00423fe0, CListView_SetColumnTooltip, SetTooltipFn, Original_CListView_SetColumnTooltip,
          CListView_SetColumnTooltip);
S3RE_HOOK(0x00425b50, CListView_UpdateScrollBar, UpdateFn, Original_CListView_UpdateScrollBar,
          CListView_UpdateScrollBar);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

constexpr std::size_t kLinks = 0x2c00;
constexpr std::size_t kColumnData = 0x2000;  // 8 columns of 0x20 bytes

std::uint32_t Effect(const kit::Call& call, const std::uint32_t*) {
    std::uint32_t hash = call.callee * 2654435761u;
    for (std::uint32_t i = 0; i < call.count; ++i) {
        hash = (hash ^ call.args[i]) * 16777619u;
    }
    hash ^= call.self;
    const auto base = static_cast<std::uint32_t>(kit::Current().base);
    if (call.callee == kSkipFind) {
        return hash % 3 == 0 ? 0 : base + kLinks + 4 * (hash % 8);
    }
    if (call.callee == kSkipInsert) {
        return base + kLinks + 0x40;
    }
    if (call.callee == kColumnBareResize) {
        return hash % 3 == 0 ? 0 : 1;
    }
    return hash ^ (hash >> 9);
}

// The column vector: fake vtable, data in the buffer, small lengths and
// capacities (often 0), column types 5..8.
void Columns(test::Rng& r, std::uint8_t* b) {
    const std::size_t v = kThis + off::kColumns;
    kit::FakeObject(b, v);
    kit::Link(b, v + 8, kColumnData);
    kit::Put(b, v + 0x0c, r.Chance(1, 2) ? 0 : r.Below(8));
    kit::Put(b, v + 0x14, r.Chance(1, 3) ? 0 : r.Below(8));
    for (int k = 0; k < 8; ++k) {
        kit::Put(b, kColumnData + kColumnBytes * k + off::kColumnType, 5 + r.Below(4));
    }
    kit::Put(b, kThis + off::kLineCount, r.Below(5));
}

std::uint32_t Index(test::Rng& r) {
    return static_cast<std::uint32_t>(r.Chance(1, 8) ? static_cast<std::int32_t>(r.Next32()) : r.Range(-2, 9));
}

// UpdateScrollBar: the element (vbptr +4) and the bar's element (vbptr
// +0x19c) inside the buffer; small rect, counts and top lines.
void ScrollBarObject(test::Rng& r, std::uint8_t* b) {
    const std::size_t element = kit::VbPtr(b, kThis + 4, 0x600, 0);
    kit::VbPtr(b, kThis + off::kScrollBarVbPtr, 0x500, 1);
    for (int k = 0; k < 4; ++k) {
        kit::Put(b, element + 8 + 4 * k, static_cast<std::uint32_t>(r.Range(-20, 300)));
    }
    kit::Put(b, kThis + off::kLineCount, r.Chance(1, 8) ? r.Next32() : r.Below(12));
    kit::Put(b, kThis + off::kFullyVisible, r.Chance(1, 8) ? r.Next32() : r.Below(12));
    kit::Put(b, kThis + off::kTopLine, static_cast<std::uint32_t>(r.Chance(1, 8) ? static_cast<std::int32_t>(r.Next32())
                                                                                   : r.Range(-2, 3)));
    b[kThis + off::kHeadersShown] = static_cast<std::uint8_t>(r.Below(3));
}

struct Members {
    SetUpDownFn upDown = &CListView_SetCellUpDown;
    SetProgressFn progress = &CListView_SetCellProgress;
    SetTooltipFn tooltip = &CListView_SetColumnTooltip;
    UpdateFn update = &CListView_UpdateScrollBar;
    LayoutFn layout = &CListView_LayoutColumns;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.This<kSkipFind, 1>()
        .This<kSkipInsert, 1>()
        .Cdecl<kDynamicCast, 5>()
        .This<kColumnBareResize, 1>()
        .This<kUpDownSetRange, 2>()
        .This<kUpDownSetValue, 1>()
        .This<kProgressSetProgress, 2>()
        .This<kColumnSetTooltip, 1>()
        .This<kControlSetTooltip, 1>()
        .This<kScrollSetRange, 3>()
        .This<kElementSetRect, 1>()
        .This<kGroupShowChild, 2>()
        .This<kColumnSetPosition, 3>();
    const SetUpDownFn originalUpDown = Original_CListView_SetCellUpDown;
    kit::Compare(
        ctx, "SetCellUpDown", cases, Columns,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t line = r.Below(4), column = Index(r), a = r.Next32(), c = r.Next32();
            originalUpDown(base + kThis, nullptr, line, column, a, c, r.Next32());
            return 0u;
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t line = r.Below(4), column = Index(r), a = r.Next32(), c = r.Next32();
            ours.upDown(base + kThis, nullptr, line, column, a, c, r.Next32());
            return 0u;
        },
        0, &Effect);
    const SetProgressFn originalProgress = Original_CListView_SetCellProgress;
    kit::Compare(
        ctx, "SetCellProgress", cases, Columns,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t line = r.Below(4), column = Index(r), a = r.Next32();
            originalProgress(base + kThis, nullptr, line, column, a, r.Next32());
            return 0u;
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t line = r.Below(4), column = Index(r), a = r.Next32();
            ours.progress(base + kThis, nullptr, line, column, a, r.Next32());
            return 0u;
        },
        0, &Effect);
    const SetTooltipFn originalTooltip = Original_CListView_SetColumnTooltip;
    kit::Compare(
        ctx, "SetColumnTooltip", cases, Columns,
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t column = Index(r);
            originalTooltip(base + kThis, nullptr, column, r.Next32());
            return 0u;
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t column = Index(r);
            ours.tooltip(base + kThis, nullptr, column, r.Next32());
            return 0u;
        },
        0, &Effect);
    kit::Compare(
        ctx, "UpdateScrollBar", cases, ScrollBarObject,
        [&](std::uintptr_t base, test::Rng&) { return Original_CListView_UpdateScrollBar(base + kThis, nullptr); },
        [&](std::uintptr_t base, test::Rng&) { return ours.update(base + kThis, nullptr); }, 0xffffffffu, &Effect);
    // LayoutColumns: the column vector as above, widths small (sometimes
    // negative), the client's left and right small, the element behind its vbptr.
    kit::Compare(
        ctx, "LayoutColumns", cases,
        [](test::Rng& r, std::uint8_t* b) {
            Columns(r, b);
            kit::VbPtr(b, kThis + 4, 0x600, 0);
            for (int k = 0; k < 8; ++k) {
                kit::Put(b, kColumnData + kColumnBytes * k + off::kColumnWidth, static_cast<std::uint32_t>(r.Range(-5, 60)));
            }
            kit::Put(b, kThis + off::kClientRect, static_cast<std::uint32_t>(r.Range(-10, 50)));
            kit::Put(b, kThis + off::kClientRect + 8, static_cast<std::uint32_t>(r.Range(-10, 200)));
        },
        [&](std::uintptr_t base, test::Rng&) {
            Original_CListView_LayoutColumns(base + kThis, nullptr);
            return 0u;
        },
        [&](std::uintptr_t base, test::Rng&) {
            ours.layout(base + kThis, nullptr);
            return 0u;
        },
        0, &Effect);
}

void __fastcall WrongUpDown(std::uintptr_t self, void* edx, std::uint32_t line, std::uint32_t column,
                            std::uint32_t minimum, std::uint32_t maximum, std::uint32_t value) {
    CListView_SetCellUpDown(self, edx, line, column, maximum, minimum, value);  // range swapped
}
void __fastcall WrongTooltip(std::uintptr_t self, void* edx, std::uint32_t column, std::uint32_t textId) {
    // Without the column's own tooltip.
    std::uint32_t key = column;
    for (std::uint32_t line = 0; line < Field<std::uint32_t>(self, off::kLineCount); ++line, key += 0x10000) {
        reinterpret_cast<Thiscall1>(kControlSetTooltip)(CellAs(self, key, type_descriptor::kCControl), nullptr, textId);
    }
    (void)edx;
}
void __fastcall WrongLayout(std::uintptr_t self, void* edx) {
    Field<RECT>(self, off::kClientRect).right += 1;  // one more pixel of room
    CListView_LayoutColumns(self, edx);
    Field<RECT>(self, off::kClientRect).right -= 1;
}
std::uint32_t __fastcall WrongUpdate(std::uintptr_t self, void* edx) {
    const std::uint32_t result = CListView_UpdateScrollBar(self, edx);
    Field<RECT>(self, off::kClientRect).right -= 1;
    return result;
}

}  // namespace

S3RE_TEST(UiListViewCells_MatchOriginal) {
    CompareAll(ctx, Members{}, 600);
}

S3RE_TEST(UiListViewCells_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 4; ++k) {
        Members wrong;
        if (k == 3) {
            wrong.layout = &WrongLayout;
        } else if (k == 0) {
            wrong.upDown = &WrongUpDown;
        } else if (k == 1) {
            wrong.tooltip = &WrongTooltip;
        } else {
            wrong.update = &WrongUpdate;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
