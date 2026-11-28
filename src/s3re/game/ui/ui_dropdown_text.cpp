// Setting an edit field's text from an 8-bit string, and a dropdown's
// reaction to a pick from its list (Dirk\dialog.cpp, wip/dialog-r8). All
// thiscall; layouts in re/ui_classes.md.
//
// CEditControl::SetText (0x00418b40; this = the edit control's start):
//   (const std::string&) ret 4: NarrowToWideString 0x60b840 (fastcall:
//   result = a local 16-bit string, source) and CEditControl::SetTextW
//   0x418d40 with the pointer it returns; then the local's destructor
//   inlined (MSVC 6 strings: a buffer whose count byte (just before it) is
//   0 or 0xff is freed with operator delete (buffer - 2), otherwise the
//   count is decremented). EAX = SetTextW's without a buffer, delete's
//   after a free, else SetTextW's with AL = the decremented count. (The
//   original's exception frame only destroys the local when SetTextW
//   throws; ours has none.)
// CDropdown::OnListSelect (0x00422670; the list box's listener, this = the
//   dropdown's group part): (list, index) ret 8: the drop list closed
//   (the group (+0x38) slot 1, RemoveChild, with the list box's element:
//   vbptr +0x1cc; the list box is a member, so its null test is dead), the
//   "open" byte (+0x660) cleared, the edit field (+0xb8) SetText 0x418b40
//   with the picked item (the list box's items (CListBox::GetItems
//   0x41f240), the string vector's operator[] check inlined, resizing an
//   empty vector with WCValVector<String>::resize 0x40a580; 16-byte
//   entries); then the dropdown's listener (+0xb4) slot 0 with (the
//   element (vbptr +4), index). EAX = the listener's, else SetText's.

#include "game/ui/member_test_kit.h"
#include "game/ui/screen_element_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kNarrowToWide = 0x0060b840;     // NarrowToWideString (fastcall)
constexpr std::uintptr_t kEditSetTextW = 0x00418d40;     // CEditControl::SetTextW
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;   // the game's operator delete
constexpr std::uintptr_t kListBoxGetItems = 0x0041f240;  // CListBox::GetItems (ours)
constexpr std::uintptr_t kStringVectorResize = 0x0040a580;  // WCValVector<String>::resize (ours)
constexpr std::uintptr_t kEditSetText = 0x00418b40;      // CEditControl::SetText (ours)

constexpr std::uint8_t kUnshared = 0;     // count byte: the buffer has one owner
constexpr std::uint8_t kFrozen = 0xff;    // count byte: never shared
constexpr std::uint32_t kStringBytes = 16;  // an MSVC 6 std::string
constexpr std::uint32_t kVectorLengthChanged = 0x04;

namespace off {  // CDropdown, from its group part
constexpr std::int32_t kGroup = 0x38;
constexpr std::int32_t kListener = 0xb4;
constexpr std::int32_t kEdit = 0xb8;
constexpr std::int32_t kListBox = 0x1c8;
constexpr std::int32_t kListBoxVbPtr = 0x1cc;
constexpr std::int32_t kOpen = 0x660;
}  // namespace off

template <typename T>
T& Field(std::uintptr_t self, std::int32_t offset) {  // raw-ok: fields before a virtual base, see the header
    return *reinterpret_cast<T*>(self + offset);
}

std::uintptr_t VirtualBase(std::uintptr_t self, std::int32_t vbptr) {
    return self + vbptr + (*reinterpret_cast<const std::int32_t* const*>(self + vbptr))[1];
}

template <typename Fn>
Fn Slot(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uint32_t* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot / 4]);
}

using Thiscall0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using Thiscall1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using Thiscall2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);

// An MSVC 6 16-bit string as the local of SetText (16 bytes).
struct WideLocal {
    std::uint32_t allocator;
    std::uint8_t* buffer;  // the count byte sits just before it
    std::uint32_t length;
    std::uint32_t capacity;
};
static_assert(sizeof(WideLocal) == kStringBytes);

// The string vector's inlined operator[] check (see the header of
// ui_list_view_cells.cpp; here resize is WCValVector<String>::resize).
std::uint32_t StringIndex(std::uintptr_t vector, std::int32_t index) {
    auto& length = Field<std::uint32_t>(vector, 0x14);
    const auto last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return static_cast<std::uint32_t>(index);
    }
    if (length != 0) {
        return index < 0 ? 0u : static_cast<std::uint32_t>(last);
    }
    if (Field<std::uint32_t>(vector, 0x0c) == 0) {
        if (reinterpret_cast<Thiscall1>(kStringVectorResize)(vector, nullptr, 1) == 0) {
            return 0;
        }
        if (length > 1) {
            length = 1;
        }
    }
    Slot<Thiscall1>(vector, kVectorLengthChanged)(vector, nullptr, 0);
    length = 1;
    return 0;
}

}  // namespace

using SetTextFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t text);
using OnListSelectFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t list,
                                                  std::uint32_t index);

// Purpose: Sets an edit field's text from an 8-bit string (names, chat lines).
std::uint32_t __fastcall CEditControl_SetText(std::uintptr_t self, void*, std::uint32_t text) {
    using WidenFn = std::uint32_t(__fastcall*)(WideLocal* result, std::uint32_t source);
    using DeleteFn = std::uint32_t(__cdecl*)(void* block);
    WideLocal wide;  // filled by NarrowToWideString
    const std::uint32_t widened = reinterpret_cast<WidenFn>(kNarrowToWide)(&wide, text);
    std::uint32_t result = reinterpret_cast<Thiscall1>(kEditSetTextW)(self, nullptr, widened);
    // ~wstring, inlined.
    if (wide.buffer != nullptr) {
        const std::uint8_t count = wide.buffer[-1];
        if (count == kUnshared || count == kFrozen) {
            result = reinterpret_cast<DeleteFn>(kOperatorDelete)(wide.buffer - 2);
        } else {
            wide.buffer[-1] = static_cast<std::uint8_t>(count - 1);
            result = (result & 0xffffff00u) | wide.buffer[-1];
        }
    }
    return result;
}

// Purpose: A dropdown's handler for a pick from its list: closes the list,
// shows the picked text and tells the dropdown's listener.
std::uint32_t __fastcall CDropdown_OnListSelect(std::uintptr_t self, void*, std::uint32_t /*list*/,
                                                std::uint32_t index) {
    const std::uintptr_t listBox = self + off::kListBox;  // a member: never null
    const std::uintptr_t listElement = listBox == 0 ? 0 : VirtualBase(self, off::kListBoxVbPtr);
    const std::uintptr_t group = self + off::kGroup;
    Slot<Thiscall1>(group, ui::group_slot::kRemoveChild)(group, nullptr, static_cast<std::uint32_t>(listElement));
    Field<std::uint8_t>(self, off::kOpen) = 0;
    const std::uintptr_t items = reinterpret_cast<Thiscall0>(kListBoxGetItems)(listBox, nullptr);
    const std::uint32_t i = StringIndex(items, static_cast<std::int32_t>(index));
    const std::uint32_t item = Field<std::uint32_t>(items, 8) + i * kStringBytes;
    const std::uint32_t result = reinterpret_cast<Thiscall1>(kEditSetText)(self + off::kEdit, nullptr, item);
    const std::uintptr_t listener = Field<std::uint32_t>(self, off::kListener);
    if (listener == 0) {
        return result;
    }
    const std::uintptr_t element = self == 0 ? 0 : VirtualBase(self, 4);  // a null `this` is dead
    return Slot<Thiscall2>(listener, 0)(listener, nullptr, static_cast<std::uint32_t>(element), index);
}

S3RE_HOOK(0x00418b40, CEditControl_SetText, SetTextFn, Original_CEditControl_SetText, CEditControl_SetText);
S3RE_HOOK(0x00422670, CDropdown_OnListSelect, OnListSelectFn, Original_CDropdown_OnListSelect, CDropdown_OnListSelect);

// ---------------------------------------------------------------------------
// Tests (game/ui/member_test_kit.h)
// ---------------------------------------------------------------------------

namespace {

namespace kit = ui::kit;
using kit::kThis;

constexpr std::size_t kWideBuffer = 0x2c10;  // the widened text's buffer (its count byte before it)
constexpr std::size_t kItems = 0x2000;       // the list box's string vector (fake vtable)
constexpr std::size_t kItemData = 0x2100;

// NarrowToWideString fills its result: no buffer, or one whose count byte
// is 0, 0xff or shared (by the call's hash); GetItems returns the vector;
// resize succeeds or fails; the rest a hash.
std::uint32_t Effect(const kit::Call& call, const std::uint32_t* raw) {
    std::uint32_t hash = call.callee * 2654435761u ^ call.self;
    for (std::uint32_t i = 0; i < call.count; ++i) {
        hash = (hash ^ call.args[i]) * 16777619u;
    }
    const auto base = static_cast<std::uint32_t>(kit::Current().base);
    if (call.callee == kNarrowToWide) {
        auto* local = reinterpret_cast<WideLocal*>(raw[0]);
        std::memset(local, 0, sizeof(*local));
        local->allocator = hash & 0xff;
        if (hash % 5 != 0) {
            auto* buffer = reinterpret_cast<std::uint8_t*>(base + kWideBuffer);
            static const std::uint8_t kCounts[] = {0, 0xff, 1, 2, 7, 0xfe};
            buffer[-1] = kCounts[(hash >> 8) % 6];
            local->buffer = buffer;
            local->length = (hash >> 12) % 9;
        }
        return (hash >> 3) % 2 == 0 ? raw[0] : hash;
    }
    if (call.callee == kListBoxGetItems) {
        return base + kItems;
    }
    if (call.callee == kStringVectorResize) {
        return (hash >> 5) % 3 == 0 ? 0 : 1;
    }
    return hash ^ (hash >> 11);
}

struct Members {
    SetTextFn setText = &CEditControl_SetText;
    OnListSelectFn select = &CDropdown_OnListSelect;
};

void CompareAll(test::Context& ctx, const Members& ours, int cases) {
    kit::Stubs stubs;
    stubs.FastOut<kNarrowToWide, 0>()
        .This<kEditSetTextW, 1>()
        .Cdecl<kOperatorDelete, 1>()
        .This<kListBoxGetItems, 0>()
        .This<kStringVectorResize, 1>()
        .This<kEditSetText, 1>();
    kit::Compare(
        ctx, "EditSetText", cases, [](test::Rng&, std::uint8_t*) {},
        [](std::uintptr_t base, test::Rng& r) {
            return Original_CEditControl_SetText(base + kThis, nullptr, static_cast<std::uint32_t>(base + r.Below(64)));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            return ours.setText(base + kThis, nullptr, static_cast<std::uint32_t>(base + r.Below(64)));
        },
        0xffffffffu, &Effect);
    // OnListSelect: the group and the listener with fake vtables (the
    // listener's slot 0 a 2-argument stub), the list box's element and the
    // dropdown's behind their vbptrs, the string vector small or empty.
    kit::Compare(
        ctx, "DropdownOnListSelect", cases,
        [](test::Rng& r, std::uint8_t* b) {
            kit::FakeObject(b, kThis + off::kGroup);
            kit::VbPtr(b, kThis + off::kListBoxVbPtr, 0x400, 0);
            kit::VbPtr(b, kThis + 4, 0x700, 1);
            kit::FakeObject(b, kItems);
            kit::Link(b, kItems + 8, kItemData);
            kit::Put(b, kItems + 0x0c, r.Chance(1, 2) ? 0 : r.Below(6));
            kit::Put(b, kItems + 0x14, r.Chance(1, 3) ? 0 : r.Below(6));
            if (r.Chance(1, 3)) {
                kit::Put(b, kThis + off::kListener, 0);
            } else {
                kit::Link(b, kThis + off::kListener, 0x2400);
            }
            std::uint32_t twoArgs = 0;
            std::memcpy(&twoArgs, b + kit::kVtable + 4 * 11, 4);
            kit::Link(b, 0x2400, 0x2500);
            kit::Put(b, 0x2500, twoArgs);
        },
        [](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t list = r.Next32();
            return Original_CDropdown_OnListSelect(base + kThis, nullptr, list,
                                                   static_cast<std::uint32_t>(r.Chance(1, 8) ? r.Next32() : r.Range(-2, 7)));
        },
        [&](std::uintptr_t base, test::Rng& r) {
            const std::uint32_t list = r.Next32();
            return ours.select(base + kThis, nullptr, list,
                               static_cast<std::uint32_t>(r.Chance(1, 8) ? r.Next32() : r.Range(-2, 7)));
        },
        0xffffffffu, &Effect);
}

std::uint32_t __fastcall WrongSetText(std::uintptr_t self, void* edx, std::uint32_t text) {
    using WidenFn = std::uint32_t(__fastcall*)(WideLocal* result, std::uint32_t source);
    WideLocal wide;
    const std::uint32_t widened = reinterpret_cast<WidenFn>(kNarrowToWide)(&wide, text);
    (void)edx;
    return reinterpret_cast<Thiscall1>(kEditSetTextW)(self, nullptr, widened);  // the local leaks
}
std::uint32_t __fastcall WrongSelect(std::uintptr_t self, void* edx, std::uint32_t list, std::uint32_t index) {
    Field<std::uint8_t>(self, off::kOpen) = 1;
    const std::uint32_t result = CDropdown_OnListSelect(self, edx, list, index);
    Field<std::uint8_t>(self, off::kOpen) = 1;  // stays open
    return result;
}

}  // namespace

S3RE_TEST(UiDropdownText_MatchOriginal) {
    CompareAll(ctx, Members{}, 1000);
}

S3RE_TEST(UiDropdownText_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        Members wrong;
        if (k == 0) {
            wrong.setText = &WrongSetText;
        } else {
            wrong.select = &WrongSelect;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareAll(probe, wrong, 300);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
