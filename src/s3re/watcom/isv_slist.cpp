// Watcom's intrusive singly linked list (WCIsvSListBase, the base of
// WCPtrSList<T> / WCValSList<T>; template-independent code shared by every
// instantiation) and its iterator.
//
// The list: +8 the last link (0: empty), +0xc the count; the links form a
// circle (last->next is the first). A WCPtrSList's links are 8 bytes:
// +0 next, +4 the pointer; +0x10 of the list is an optional allocator
// (fastcall size in ECX). The library's error hook (0x59f200, an empty
// function) is called where the original reports misuse; ours leaves the
// call out (it does nothing).
//
// WCIsvSListBase::base_next (0x0060b400): (link, reset) ret 8: reset: the
//   first link (last->next; 0 when empty); else the link after `link`,
//   0 after the last.
// WCIsvSListBase::base_index_check (0x0060b460): (index) ret 4: the index
//   if 0 <= index < count, else 0 (index < 0) or count - 1.
// WCIsvSListBase::base_bfind (0x0060b4b0): (index) ret 4: the link before
//   the checked index (last advanced index times).
// WCIsvSListBase::get (0x0060b500): (index) ret 4: unlinks and returns the
//   link at index (0 for an empty list).
// WCIsvSListBase::clear (0x0060b550): last = count = 0. EAX = 0.
// WCIsvSListBase::clearAndDestroy (0x0060b560): every link through the
//   list's virtual slot 1 (destroy a link, thiscall(link)), then clear.
// WCIsvSListIter::operator+= (0x0060b5f0): (n) ret 4. Iterator: +8 the
//   list, +0xc the current link (0: before the first), +0x10 state (bit 1:
//   past the end). n < 1 counts as 1. Advances n links; reaching the last
//   link with steps left, or starting at it, ends the iteration (current
//   0, state 2) and returns 0; otherwise returns the new current link.
// WCPtrListBase::base_new_link (0x00463b50; was Menu_sub_463b50): (const
//   T* const* value) ret 4: an 8-byte link from the list's allocator (+0x10)
//   or operator new; next = 0, data = *value. EAX = the link (0 if none).

#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::watcom {

#pragma pack(push, 1)
struct SLink {
    SLink* next;      // +0x00
    std::uint32_t data;  // +0x04 a WCPtrSList's pointer
};
struct IsvSList {
    std::uint32_t vtable;  // +0x00 slot 1 (0x04) destroys a link
    std::uint32_t unk04;
    SLink* last;           // +0x08 the last link (its next is the first); 0: empty
    std::int32_t count;    // +0x0c
    void* allocator;       // +0x10 WCPtrSList: link allocator (fastcall size), or 0
};
struct IsvSListIter {
    std::uint32_t vtable;  // +0x00
    std::uint32_t unk04;
    IsvSList* list;        // +0x08
    SLink* current;        // +0x0c 0: before the first
    std::uint32_t state;   // +0x10 bit 1: past the end
};
#pragma pack(pop)
static_assert(sizeof(SLink) == 8);
static_assert(sizeof(IsvSList) == 0x14);
static_assert(sizeof(IsvSListIter) == 0x14);

constexpr std::uint32_t kPastEnd = 2;
constexpr std::uint32_t kLinkBytes = 8;
constexpr std::uint32_t kSlotDestroyLink = 0x04;

using NextFn = SLink*(__fastcall*)(IsvSList* self, void* edx, SLink* link, std::uint32_t reset);
using IndexFn = std::int32_t(__fastcall*)(IsvSList* self, void* edx, std::int32_t index);
using FindFn = SLink*(__fastcall*)(IsvSList* self, void* edx, std::int32_t index);
using ClearFn = std::uint32_t(__fastcall*)(IsvSList* self, void* edx);
using IterAdvanceFn = SLink*(__fastcall*)(IsvSListIter* self, void* edx, std::int32_t steps);
using NewLinkFn = SLink*(__fastcall*)(IsvSList* self, void* edx, const std::uint32_t* value);

SLink* __fastcall WCIsvSListBase_Next(IsvSList* self, void* edx, SLink* link, std::uint32_t reset);
std::int32_t __fastcall WCIsvSListBase_IndexCheck(IsvSList* self, void* edx, std::int32_t index);
SLink* __fastcall WCIsvSListBase_BeforeFind(IsvSList* self, void* edx, std::int32_t index);
SLink* __fastcall WCIsvSListBase_Get(IsvSList* self, void* edx, std::int32_t index);
std::uint32_t __fastcall WCIsvSListBase_Clear(IsvSList* self, void* edx);
std::uint32_t __fastcall WCIsvSListBase_ClearAndDestroy(IsvSList* self, void* edx);
SLink* __fastcall WCIsvSListIter_Advance(IsvSListIter* self, void* edx, std::int32_t steps);
SLink* __fastcall WCPtrListBase_NewLink(IsvSList* self, void* edx, const std::uint32_t* value);

S3RE_HOOK(0x0060b400, WCIsvSListBase_Next, NextFn, Original_WCIsvSListBase_Next, WCIsvSListBase_Next);

// Purpose: Steps through a list (the first link, or the one after `link`);
// used by every walk over the game's pointer lists (threads, surfaces,
// screen elements).
SLink* __fastcall WCIsvSListBase_Next(IsvSList* self, void* /*edx*/, SLink* link, std::uint32_t reset) {
    if (reset != 0) {
        return self->last != nullptr ? self->last->next : nullptr;
    }
    return link == self->last ? nullptr : link->next;
}

S3RE_HOOK(0x0060b460, WCIsvSListBase_IndexCheck, IndexFn, Original_WCIsvSListBase_IndexCheck,
          WCIsvSListBase_IndexCheck);

// Purpose: Clamps a list index into range (the library reports a bad index
// through its error hook and goes on with the nearest end).
std::int32_t __fastcall WCIsvSListBase_IndexCheck(IsvSList* self, void* /*edx*/, std::int32_t index) {
    const std::int32_t last = self->count - 1;
    if (index >= 0 && index <= last) {
        return index;
    }
    return index < 0 ? 0 : last;
}

S3RE_HOOK(0x0060b4b0, WCIsvSListBase_BeforeFind, FindFn, Original_WCIsvSListBase_BeforeFind,
          WCIsvSListBase_BeforeFind);

// Purpose: The link before the one at an index (for unlinking it).
SLink* __fastcall WCIsvSListBase_BeforeFind(IsvSList* self, void* /*edx*/, std::int32_t index) {
    using Fn = std::int32_t(__fastcall*)(IsvSList* self, void* edx, std::int32_t index);
    const std::int32_t steps = reinterpret_cast<Fn>(0x0060b460)(self, nullptr, index);
    SLink* link = self->last;
    for (std::int32_t i = 0; i < steps; ++i) {
        link = link->next;
    }
    return link;
}

S3RE_HOOK(0x0060b500, WCIsvSListBase_Get, FindFn, Original_WCIsvSListBase_Get, WCIsvSListBase_Get);

// Purpose: Removes the link at an index from a list and returns it (Watcom's
// get; the game takes threads, surfaces and palettes off their lists with
// get(0)).
SLink* __fastcall WCIsvSListBase_Get(IsvSList* self, void* /*edx*/, std::int32_t index) {
    using Fn = SLink*(__fastcall*)(IsvSList* self, void* edx, std::int32_t index);
    SLink* const before = reinterpret_cast<Fn>(0x0060b4b0)(self, nullptr, index);
    if (before == nullptr) {
        return nullptr;
    }
    SLink* const link = before->next;
    if (link == before) {  // the only link
        self->last = nullptr;
    } else {
        before->next = link->next;
        if (link == self->last) {
            self->last = before;
        }
    }
    --self->count;
    return link;
}

S3RE_HOOK(0x0060b550, WCIsvSListBase_Clear, ClearFn, Original_WCIsvSListBase_Clear, WCIsvSListBase_Clear);

// Purpose: Forgets all links of a list (without freeing them).
std::uint32_t __fastcall WCIsvSListBase_Clear(IsvSList* self, void* /*edx*/) {
    self->last = nullptr;
    self->count = 0;
    return 0;
}

S3RE_HOOK(0x0060b560, WCIsvSListBase_ClearAndDestroy, ClearFn, Original_WCIsvSListBase_ClearAndDestroy,
          WCIsvSListBase_ClearAndDestroy);

// Purpose: Destroys every link of a list (through the list's own virtual
// "destroy link") and empties it; used by the list destructors.
std::uint32_t __fastcall WCIsvSListBase_ClearAndDestroy(IsvSList* self, void* /*edx*/) {
    using Next = SLink*(__fastcall*)(IsvSList* self, void* edx, SLink* link, std::uint32_t reset);
    const auto next = reinterpret_cast<Next>(0x0060b400);
    SLink* link = next(self, nullptr, nullptr, 1);
    while (link != nullptr) {
        SLink* const following = next(self, nullptr, link, 0);
        using DestroyFn = void(__fastcall*)(IsvSList* self, void* edx, SLink* link);
        const auto* const vtable = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(self->vtable));
        (*reinterpret_cast<DestroyFn const*>(vtable + kSlotDestroyLink))(self, nullptr, link);
        link = following;
    }
    using Clear = std::uint32_t(__fastcall*)(IsvSList* self, void* edx);
    return reinterpret_cast<Clear>(0x0060b550)(self, nullptr);
}

S3RE_HOOK(0x0060b5f0, WCIsvSListIter_Advance, IterAdvanceFn, Original_WCIsvSListIter_Advance,
          WCIsvSListIter_Advance);

// Purpose: Advances an iterator over a list by n links (the game's walks
// over its surface, palette and thread lists use += 1); 0 at the end.
SLink* __fastcall WCIsvSListIter_Advance(IsvSListIter* self, void* /*edx*/, std::int32_t steps) {
    if (self->list == nullptr || (self->state & kPastEnd) != 0) {
        return nullptr;  // (error hook)
    }
    if (steps < 1) {
        steps = 1;  // (error hook)
    }
    SLink* const last = self->list->last;
    SLink* link = self->current;
    if (link != last) {
        if (link == nullptr) {  // before the first: start at the last, so the first step reaches the first
            link = last;
            self->state = 0;
        }
        for (; steps > 0; --steps) {
            link = link->next;
            if (link == last && steps - 1 > 0) {
                --steps;
                break;  // the end, with steps left
            }
        }
        if (steps == 0) {
            self->current = link;
            return link;
        }
    }
    // Past the end: current 0, state "past the end" (more steps left than
    // one: the error hook).
    self->current = nullptr;
    self->state = kPastEnd;
    return nullptr;
}

S3RE_HOOK(0x00463b50, WCPtrListBase_NewLink, NewLinkFn, Original_WCPtrListBase_NewLink, WCPtrListBase_NewLink);

// Purpose: Makes a new link for a pointer list (from the list's own
// allocator if it has one).
SLink* __fastcall WCPtrListBase_NewLink(IsvSList* self, void* /*edx*/, const std::uint32_t* value) {
    void* block;
    if (self->allocator != nullptr) {
        using AllocFn = void*(__fastcall*)(std::uint32_t size);
        block = reinterpret_cast<AllocFn>(self->allocator)(kLinkBytes);
    } else {
        block = msvc6::GameOperatorNew(kLinkBytes);
    }
    auto* const link = static_cast<SLink*>(block);
    if (link != nullptr) {
        link->next = nullptr;
        link->data = *value;
    }
    return link;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A circular list of up to 12 links in an arena (random order, sometimes an
// empty or inconsistent count), an iterator over it at a random position.
// Each case runs one function on two copies; compared: EAX (arena-relative),
// the arena, the calls of the list's destroy slot and the allocator.

namespace {

constexpr std::size_t kArenaBytes = 0x200;
constexpr std::size_t kListAt = 0x00;
constexpr std::size_t kIterAt = 0x20;
constexpr std::size_t kValueAt = 0x38;
constexpr std::size_t kLinksAt = 0x40;
constexpr std::size_t kMaxLinks = 12;

std::vector<std::uint32_t>* g_calls = nullptr;
std::uint8_t* g_arena = nullptr;
std::uint8_t g_allocated[2][64];
int g_side = 0;

std::uint32_t Relative(const void* p) {
    const auto* const bytes = static_cast<const std::uint8_t*>(p);
    if (bytes >= g_arena && bytes < g_arena + kArenaBytes) {
        return 0x10000 + static_cast<std::uint32_t>(bytes - g_arena);
    }
    if (bytes >= g_allocated[g_side] && bytes < g_allocated[g_side] + sizeof(g_allocated[0])) {
        return 0x20000 + static_cast<std::uint32_t>(bytes - g_allocated[g_side]);
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

void __fastcall FakeDestroyLink(IsvSList* /*self*/, void* /*edx*/, SLink* link) {
    g_calls->push_back(0xde57);
    g_calls->push_back(Relative(link));
}

void* __fastcall FakeAllocate(std::uint32_t size) {
    g_calls->push_back(0xa110c);
    g_calls->push_back(size);
    return size == kLinkBytes ? g_allocated[g_side] : nullptr;
}

void* __fastcall FakeAllocateNothing(std::uint32_t size) {
    g_calls->push_back(0xa1100);
    g_calls->push_back(size);
    return nullptr;
}

void* g_listVtable[2] = {nullptr, reinterpret_cast<void*>(&FakeDestroyLink)};

struct Functions {
    NextFn next;
    IndexFn indexCheck;
    FindFn beforeFind;
    FindFn get;
    ClearFn clear;
    ClearFn clearAndDestroy;
    IterAdvanceFn advance;
    NewLinkFn newLink;
};

Functions Originals() {
    return {Original_WCIsvSListBase_Next,  Original_WCIsvSListBase_IndexCheck, Original_WCIsvSListBase_BeforeFind,
            Original_WCIsvSListBase_Get,   Original_WCIsvSListBase_Clear,      Original_WCIsvSListBase_ClearAndDestroy,
            Original_WCIsvSListIter_Advance, Original_WCPtrListBase_NewLink};
}

Functions Ours() {
    return {&WCIsvSListBase_Next,  &WCIsvSListBase_IndexCheck, &WCIsvSListBase_BeforeFind, &WCIsvSListBase_Get,
            &WCIsvSListBase_Clear, &WCIsvSListBase_ClearAndDestroy, &WCIsvSListIter_Advance, &WCPtrListBase_NewLink};
}

std::uint32_t RunOp(const Functions& f, int op, std::uint8_t* arena, std::int32_t value, std::uint32_t link) {
    auto* const list = reinterpret_cast<IsvSList*>(arena + kListAt);
    auto* const iter = reinterpret_cast<IsvSListIter*>(arena + kIterAt);
    SLink* const at = link == 0xffffffff ? nullptr : reinterpret_cast<SLink*>(arena + kLinksAt + 8 * link);
    switch (op) {
        case 0: return Relative(f.next(list, nullptr, at, static_cast<std::uint32_t>(value) & 1));
        case 1: return static_cast<std::uint32_t>(f.indexCheck(list, nullptr, value));
        case 2: return Relative(f.beforeFind(list, nullptr, value));
        case 3: return Relative(f.get(list, nullptr, value));
        case 4: return f.clear(list, nullptr);
        case 5: return f.clearAndDestroy(list, nullptr);
        case 6: return Relative(f.advance(iter, nullptr, value));
        default:
            return Relative(f.newLink(list, nullptr, reinterpret_cast<const std::uint32_t*>(arena + kValueAt)));
    }
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    test::Rng& rng = ctx.rng();
    std::vector<std::uint8_t> bytes(kArenaBytes);
    std::vector<std::uint8_t> arenas[2] = {std::vector<std::uint8_t>(kArenaBytes), std::vector<std::uint8_t>(kArenaBytes)};
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(rng.Below(8));
        rng.Fill(bytes.data(), bytes.size());
        // A random word that happens to point into one side's arena or
        // allocation block would be compared relocated on that side only (a
        // false mismatch, seen in a full --test run after a DLL layout
        // change, wip/logic-r4): move such words away.
        for (std::size_t k = 0; k + 4 <= bytes.size(); k += 4) {
            std::uint32_t word;
            std::memcpy(&word, bytes.data() + k, 4);
            const auto inside = [word](const void* base, std::size_t size) {
                const auto start = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base));
                return word - start < size;
            };
            if (inside(g_allocated, sizeof(g_allocated)) || inside(arenas[0].data(), kArenaBytes) ||
                inside(arenas[1].data(), kArenaBytes)) {
                word ^= 0x80000000u;
                std::memcpy(bytes.data() + k, &word, 4);
            }
        }
        const std::uint32_t links = rng.Chance(1, 6) ? 0 : 1 + rng.Below(kMaxLinks);
        // A random circle of the links.
        std::uint32_t order[kMaxLinks];
        for (std::uint32_t k = 0; k < links; ++k) {
            order[k] = k;
        }
        for (std::uint32_t k = links; k > 1; --k) {
            std::swap(order[k - 1], order[rng.Below(k)]);
        }
        std::int32_t value = 0;
        switch (rng.Below(4)) {
            case 0: value = static_cast<std::int32_t>(rng.Below(links + 2)); break;
            case 1: value = -static_cast<std::int32_t>(rng.Below(3)); break;
            case 2: value = static_cast<std::int32_t>(rng.Below(30)); break;
            default: value = static_cast<std::int32_t>(rng.Next32()); break;
        }
        const std::uint32_t link = links == 0 ? 0xffffffff : order[rng.Below(links)];
        const std::uint32_t countMode = rng.Below(8);
        const std::uint32_t iterMode = rng.Below(4);
        const std::uint32_t iterAt = links == 0 ? 0 : order[rng.Below(links)];
        const std::uint32_t state = rng.Chance(1, 4) ? rng.Next32() : (rng.Chance(1, 3) ? 2 : 0);
        const std::uint32_t allocMode = rng.Below(3);
        std::uint32_t results[2];
        std::vector<std::uint32_t> calls[2];
        for (int side = 0; side < 2; ++side) {
            std::vector<std::uint8_t>& a = arenas[side];
            std::memcpy(a.data(), bytes.data(), kArenaBytes);
            std::uint8_t* const base = a.data();
            auto* const list = reinterpret_cast<IsvSList*>(base + kListAt);
            auto* const iter = reinterpret_cast<IsvSListIter*>(base + kIterAt);
            auto linkAt = [&](std::uint32_t k) { return reinterpret_cast<SLink*>(base + kLinksAt + 8 * k); };
            for (std::uint32_t k = 0; k < links; ++k) {
                linkAt(order[k])->next = linkAt(order[(k + 1) % links]);
            }
            list->vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_listVtable));
            list->last = links == 0 ? nullptr : linkAt(order[links - 1]);
            // (A wrong count is fine for a non-empty circle; an empty list
            // with a count would make both walk a null link.)
            list->count = countMode < 6 || links == 0
                              ? static_cast<std::int32_t>(links)
                              : (countMode == 6 ? 0 : static_cast<std::int32_t>(1 + countMode * 3));
            list->allocator = allocMode == 0 ? nullptr
                                             : (allocMode == 1 ? reinterpret_cast<void*>(&FakeAllocate)
                                                               : reinterpret_cast<void*>(&FakeAllocateNothing));
            iter->list = iterMode == 0 ? nullptr : list;
            iter->current = iterMode == 1 || links == 0 ? nullptr : linkAt(iterAt);
            iter->state = state;
            std::memset(g_allocated[side], 0xcc, sizeof(g_allocated[side]));
            g_calls = &calls[side];
            g_arena = base;
            g_side = side;
            results[side] = RunOp(side == 0 ? Originals() : ours, op, base, value, link);
            g_calls = nullptr;
        }
        // A link from operator new: compare its contents, then free it.
        bool same = true;
        if (op == 7 && results[0] >= 0x30000 && results[1] >= 0x30000) {
            auto* const blocks0 = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(results[0]));
            auto* const blocks1 = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(results[1]));
            same &= S3RE_EXPECT_MEM_EQ(blocks0, blocks1, kLinkBytes);
            msvc6::GameOperatorDelete(blocks0);
            msvc6::GameOperatorDelete(blocks1);
            results[0] = results[1] = 0x30000;
        }
        same &= S3RE_EXPECT_EQ(results[0], results[1]);
        same &= S3RE_EXPECT(calls[0] == calls[1]);
        // The arenas hold pointers into themselves: compare them relocated.
        for (std::size_t k = 0; k < kArenaBytes; k += 4) {
            std::uint32_t words[2];
            for (int side = 0; side < 2; ++side) {
                std::memcpy(&words[side], arenas[side].data() + k, 4);
                g_arena = arenas[side].data();
                g_side = side;
                words[side] = Relative(reinterpret_cast<void*>(static_cast<std::uintptr_t>(words[side])));
            }
            same &= S3RE_EXPECT_EQ(words[0], words[1]);
        }
        same &= S3RE_EXPECT_MEM_EQ(g_allocated[0], g_allocated[1], sizeof(g_allocated[0]));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, links %u, value %d", i, op, links, value);
            return;
        }
    }
}

// Wrong: a step count below 1 is taken as 0.
SLink* __fastcall WrongAdvance(IsvSListIter* self, void* edx, std::int32_t steps) {
    if (steps < 1 && self->list != nullptr && (self->state & kPastEnd) == 0) {
        return self->current;
    }
    return WCIsvSListIter_Advance(self, edx, steps);
}

}  // namespace

S3RE_TEST(WatcomIsvSList_MatchOriginal) {
    Compare(ctx, Ours(), 30000);
}

S3RE_TEST(WatcomIsvSList_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.advance = &WrongAdvance;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
