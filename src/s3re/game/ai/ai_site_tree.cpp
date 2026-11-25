// AI::SiteTree: the MSVC 6.0 std::_Tree instance (a red-black tree; the
// game's <xtree> code compiled into Rainer\Cevaland.cpp) that holds the
// CEvaLand area records' site candidates: 6-byte values (SiteValue) ordered
// by their first byte, equal keys allowed (_Multi = 1). Layout in
// ai_layout.h (SiteTree, SiteTreeNode); all trees share the nil node
// g_aiSiteTreeNil (0x7e0570, refcount g_aiSiteTreeNilRefs 0x7e0574).
//
// Written after the MSVC 6.0 <xtree> code the binary was compiled from
// (the order of operations, links and colours follow the disassembly).
// Every std::_Lockit of the original is taken at the same place
// (msvc6::GameLockit, including the nested ones of the inlined _Min, _Max,
// _Lrotate and _Rrotate), and the calls the original makes to the other
// tree functions go through their S3.EXE addresses.
//
//   0x005590a0 SiteTree(const less* pred, const allocator* alloc) thiscall,
//              ret 8: allocator byte = *alloc, comparator byte = *pred,
//              _Multi = 1, then _Init inlined; EAX = this.
//   0x00559170 ~SiteTree() thiscall: erase(begin, end) 0x5593a0, delete
//              the head, head = size = 0, then under the lock
//              --g_aiSiteTreeNilRefs (deletes the nil node at 0).
//   0x00559270 insert(SiteTreeInsertResult* out, const SiteValue* v)
//              thiscall, ret 8: descends by key (v->key < node key: left)
//              under the lock; with _Multi always _Insert 0x559f60; else
//              the unique-key checks with --iterator 0x55a260. out->where,
//              out->inserted; bytes 5..7 of `out` are stack leftovers of
//              the original's frame (ours 0). EAX = out.
//   0x005593a0 erase(SiteTreeNode** out, first, last) thiscall, ret 0xc:
//              the whole tree (size != 0, first = begin, last = end):
//              _Erase of the root inlined, root = nil, size 0, leftmost =
//              rightmost = head; otherwise erase(it++) 0x5599b0 one by one
//              (++ through 0x55a320). *out = the result; EAX = out.
//   0x005594d0 clear() thiscall: erase(begin, end) 0x5593a0 into a stack
//              slot; EAX = that slot's address (callers don't use it).
//   0x005599b0 erase(SiteTreeNode** out, where) thiscall, ret 8: ++where
//              (0x55a320), unlink with rebalancing, delete, --size; *out =
//              the next node; EAX = out.
//   0x00559ed0 _Erase(node) thiscall, ret 4: frees a subtree without
//              rebalancing (right subtrees recursively through 0x559ed0).
//   0x00559f60 _Insert(SiteTreeNode** out, x, y, const SiteValue* v)
//              thiscall, ret 0x10: new red node below y (value copied with
//              AI::CopySixBytes 0x55a420), ++size, rebalancing; *out = the
//              node; EAX = out.
//   0x0055a260 iterator::_Dec() thiscall (this = SiteTreeNode**).
//   0x0055a320 iterator::_Inc() thiscall.
//   0x0055a970 _Init() thiscall: makes the nil node on first use,
//              ++refs, a new head (red, parent nil, left = right = itself),
//              size 0.
//   0x0055aa20 SiteTree(const SiteTree& other) thiscall, ret 4: copies
//              the allocator, comparator and _Multi bytes, _Init 0x55a970,
//              _Copy 0x55aa50; EAX = this.
//   0x0055aa50 _Copy(const SiteTree& other) thiscall, ret 4: copies the
//              nodes (the root's left spine inline, right subtrees with
//              0x55abc0), size, leftmost and rightmost.
//   0x0055abc0 _Copy(node, parent) thiscall, ret 8: copies a subtree below
//              parent; EAX = its root (nil for an empty one).
//   0x00580240 erase(SiteTreeNode** out, where) thiscall, ret 8: a
//              second copy of erase's wrapper (std::multimap::erase as
//              T_MAP_WORKER_AGENT::MakeBuildOrders 0x57ce90 calls it):
//              erase 0x5599b0 into the argument slot, *out = the result;
//              EAX = out.
//   0x00580270 iterator::operator--(int) (SiteTreeNode** out, int)
//              thiscall, ret 8: *out = the old node, then _Dec 0x55a260;
//              EAX = out (MakeBuildOrders: --end(), the last candidate).
//
// EAX of _Init, _Erase, ~SiteTree, _Inc and _Dec is a leftover (the
// lock's), not used by the callers.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/lockit.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace s3re::game {

using namespace ai;
using msvc6::GameLockit;

namespace {

constexpr std::uint32_t kNodeBytes = sizeof(SiteTreeNode);

// The other tree functions, called through their S3.EXE addresses.
constexpr std::uintptr_t kErase = 0x005599b0;
constexpr std::uintptr_t kEraseRange = 0x005593a0;
constexpr std::uintptr_t kEraseSubtree = 0x00559ed0;
constexpr std::uintptr_t kInsertBelow = 0x00559f60;
constexpr std::uintptr_t kDecrement = 0x0055a260;
constexpr std::uintptr_t kIncrement = 0x0055a320;
constexpr std::uintptr_t kInit = 0x0055a970;
constexpr std::uintptr_t kCopyTree = 0x0055aa50;
constexpr std::uintptr_t kCopySubtree = 0x0055abc0;
constexpr std::uintptr_t kCopySixBytes = 0x0055a420;  // AI::CopySixBytes (fastcall dst, src)

SiteTreeNode* Nil() { return g_aiSiteTreeNil; }
SiteTreeNode*& Root(SiteTree* tree) { return tree->head->parent; }
SiteTreeNode*& Leftmost(SiteTree* tree) { return tree->head->left; }
SiteTreeNode*& Rightmost(SiteTree* tree) { return tree->head->right; }

SiteTreeNode* NewNode() { return static_cast<SiteTreeNode*>(msvc6::GameOperatorNew(kNodeBytes)); }

void Increment(SiteTreeNode** it) {
    reinterpret_cast<void(__fastcall*)(SiteTreeNode**, void*)>(kIncrement)(it, nullptr);
}

void Decrement(SiteTreeNode** it) {
    reinterpret_cast<void(__fastcall*)(SiteTreeNode**, void*)>(kDecrement)(it, nullptr);
}

void CopySixBytes(SiteValue* to, const SiteValue* from) {
    reinterpret_cast<void(__fastcall*)(SiteValue*, const SiteValue*)>(kCopySixBytes)(to, from);
}

// _Min / _Max (inlined in the original, each under its own lock).
SiteTreeNode* Min(SiteTreeNode* node) {
    GameLockit lock;
    while (node->left != Nil()) {
        node = node->left;
    }
    return node;
}

SiteTreeNode* Max(SiteTreeNode* node) {
    GameLockit lock;
    while (node->right != Nil()) {
        node = node->right;
    }
    return node;
}

// _Lrotate / _Rrotate (inlined, each under its own lock).
void RotateLeft(SiteTree* tree, SiteTreeNode* x) {
    GameLockit lock;
    SiteTreeNode* y = x->right;
    x->right = y->left;
    if (y->left != Nil()) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x == Root(tree)) {
        Root(tree) = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void RotateRight(SiteTree* tree, SiteTreeNode* x) {
    GameLockit lock;
    SiteTreeNode* y = x->left;
    x->left = y->right;
    if (y->right != Nil()) {
        y->right->parent = x;
    }
    y->parent = x->parent;
    if (x == Root(tree)) {
        Root(tree) = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
}

// The inlined _Init of the constructor 0x5590a0 and of 0x55a970.
void InitHead(SiteTree* tree) {
    GameLockit lock;
    if (Nil() == nullptr) {
        SiteTreeNode* nil = NewNode();
        nil->parent = nullptr;
        nil->color = kTreeBlack;
        g_aiSiteTreeNil = nil;
        nil->left = nullptr;
        nil->right = nullptr;
    }
    ++g_aiSiteTreeNilRefs;
    SiteTreeNode* head = NewNode();
    head->parent = Nil();
    head->color = kTreeRed;
    tree->head = head;
    tree->size = 0;
    head->left = head;
    head->right = head;
}

}  // namespace

using SiteTreeCtorFn = SiteTree*(__fastcall*)(SiteTree* self, void* edx, const std::uint8_t* pred,
                                              const std::uint8_t* alloc);
using SiteTreeVoidFn = void(__fastcall*)(SiteTree* self, void* edx);
using SiteTreeInsertFn = SiteTreeInsertResult*(__fastcall*)(SiteTree* self, void* edx, SiteTreeInsertResult* out,
                                                            const SiteValue* value);
using SiteTreeEraseRangeFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode** out,
                                                         SiteTreeNode* first, SiteTreeNode* last);
using SiteTreeClearFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx);
using SiteTreeEraseFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode** out,
                                                    SiteTreeNode* where);
using SiteTreeEraseSubtreeFn = void(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode* node);
using SiteTreeInsertBelowFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode** out,
                                                          SiteTreeNode* x, SiteTreeNode* y, const SiteValue* value);
using SiteTreeIteratorFn = void(__fastcall*)(SiteTreeNode** self, void* edx);
using SiteTreeCopyCtorFn = SiteTree*(__fastcall*)(SiteTree* self, void* edx, const SiteTree* other);
using SiteTreeCopyFn = void(__fastcall*)(SiteTree* self, void* edx, const SiteTree* other);
using SiteTreeCopySubtreeFn = SiteTreeNode*(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode* node,
                                                         SiteTreeNode* parent);

SiteTree* __fastcall AI_SiteTree_Construct(SiteTree* self, void* edx, const std::uint8_t* pred,
                                           const std::uint8_t* alloc);
void __fastcall AI_SiteTree_Destruct(SiteTree* self, void* edx);
SiteTreeInsertResult* __fastcall AI_SiteTree_insert(SiteTree* self, void* edx, SiteTreeInsertResult* out,
                                                    const SiteValue* value);
SiteTreeNode** __fastcall AI_SiteTree_erase_range(SiteTree* self, void* edx, SiteTreeNode** out,
                                                  SiteTreeNode* first, SiteTreeNode* last);
SiteTreeNode** __fastcall AI_SiteTree_clear(SiteTree* self, void* edx);
SiteTreeNode** __fastcall AI_SiteTree_erase(SiteTree* self, void* edx, SiteTreeNode** out, SiteTreeNode* where);
void __fastcall AI_SiteTree_EraseSubtree(SiteTree* self, void* edx, SiteTreeNode* node);
SiteTreeNode** __fastcall AI_SiteTree_InsertBelow(SiteTree* self, void* edx, SiteTreeNode** out, SiteTreeNode* x,
                                                  SiteTreeNode* y, const SiteValue* value);
void __fastcall AI_SiteTree_iterator_Dec(SiteTreeNode** self, void* edx);
void __fastcall AI_SiteTree_iterator_Inc(SiteTreeNode** self, void* edx);
void __fastcall AI_SiteTree_Init(SiteTree* self, void* edx);
SiteTree* __fastcall AI_SiteTree_CopyConstruct(SiteTree* self, void* edx, const SiteTree* other);
void __fastcall AI_SiteTree_Copy(SiteTree* self, void* edx, const SiteTree* other);
SiteTreeNode* __fastcall AI_SiteTree_CopySubtree(SiteTree* self, void* edx, SiteTreeNode* node, SiteTreeNode* parent);

S3RE_HOOK(0x005590a0, AI_SiteTree_Construct, SiteTreeCtorFn, Original_AI_SiteTree_Construct, AI_SiteTree_Construct);
S3RE_HOOK(0x00559170, AI_SiteTree_Destruct, SiteTreeVoidFn, Original_AI_SiteTree_Destruct, AI_SiteTree_Destruct);
S3RE_HOOK(0x00559270, AI_SiteTree_insert, SiteTreeInsertFn, Original_AI_SiteTree_insert, AI_SiteTree_insert);
S3RE_HOOK(0x005593a0, AI_SiteTree_erase_range, SiteTreeEraseRangeFn, Original_AI_SiteTree_erase_range,
          AI_SiteTree_erase_range);
S3RE_HOOK(0x005594d0, AI_SiteTree_clear, SiteTreeClearFn, Original_AI_SiteTree_clear, AI_SiteTree_clear);
S3RE_HOOK(0x005599b0, AI_SiteTree_erase, SiteTreeEraseFn, Original_AI_SiteTree_erase, AI_SiteTree_erase);
S3RE_HOOK(0x00559ed0, AI_SiteTree_EraseSubtree, SiteTreeEraseSubtreeFn, Original_AI_SiteTree_EraseSubtree,
          AI_SiteTree_EraseSubtree);
S3RE_HOOK(0x00559f60, AI_SiteTree_InsertBelow, SiteTreeInsertBelowFn, Original_AI_SiteTree_InsertBelow,
          AI_SiteTree_InsertBelow);
S3RE_HOOK(0x0055a260, AI_SiteTree_iterator_Dec, SiteTreeIteratorFn, Original_AI_SiteTree_iterator_Dec,
          AI_SiteTree_iterator_Dec);
S3RE_HOOK(0x0055a320, AI_SiteTree_iterator_Inc, SiteTreeIteratorFn, Original_AI_SiteTree_iterator_Inc,
          AI_SiteTree_iterator_Inc);
S3RE_HOOK(0x0055a970, AI_SiteTree_Init, SiteTreeVoidFn, Original_AI_SiteTree_Init, AI_SiteTree_Init);
S3RE_HOOK(0x0055aa20, AI_SiteTree_CopyConstruct, SiteTreeCopyCtorFn, Original_AI_SiteTree_CopyConstruct,
          AI_SiteTree_CopyConstruct);
S3RE_HOOK(0x0055aa50, AI_SiteTree_Copy, SiteTreeCopyFn, Original_AI_SiteTree_Copy, AI_SiteTree_Copy);
using SiteTreePostDecrementFn = SiteTreeNode**(__fastcall*)(SiteTreeNode** self, void* edx, SiteTreeNode** out,
                                                            std::uint32_t unused);
SiteTreeNode** __fastcall AI_SiteTree_EraseIterator(SiteTree* self, void* edx, SiteTreeNode** out,
                                                    SiteTreeNode* where);
SiteTreeNode** __fastcall AI_SiteTree_iterator_PostDecrement(SiteTreeNode** self, void* edx, SiteTreeNode** out,
                                                             std::uint32_t unused);
S3RE_HOOK(0x00580240, AI_SiteTree_EraseIterator, SiteTreeEraseFn, Original_AI_SiteTree_EraseIterator,
          AI_SiteTree_EraseIterator);
S3RE_HOOK(0x00580270, AI_SiteTree_iterator_PostDecrement, SiteTreePostDecrementFn,
          Original_AI_SiteTree_iterator_PostDecrement, AI_SiteTree_iterator_PostDecrement);
S3RE_HOOK(0x0055abc0, AI_SiteTree_CopySubtree, SiteTreeCopySubtreeFn, Original_AI_SiteTree_CopySubtree,
          AI_SiteTree_CopySubtree);

// Purpose: Constructs an empty site tree of a CEvaLand area record (AddArea
// 0x555260 makes 38 per record).
SiteTree* __fastcall AI_SiteTree_Construct(SiteTree* self, void* /*edx*/, const std::uint8_t* pred,
                                           const std::uint8_t* alloc) {
    self->allocator = *alloc;
    self->comparator = *pred;
    self->multi = 1;
    InitHead(self);
    return self;
}

// Purpose: Destroys a site tree: frees its nodes and head, and the shared
// nil node with the last tree.
void __fastcall AI_SiteTree_Destruct(SiteTree* self, void* /*edx*/) {
    SiteTreeNode* result;
    reinterpret_cast<SiteTreeEraseRangeFn>(kEraseRange)(self, nullptr, &result, Leftmost(self), self->head);
    msvc6::GameOperatorDelete(self->head);
    self->head = nullptr;
    self->size = 0;
    GameLockit lock;
    if (--g_aiSiteTreeNilRefs == 0) {
        msvc6::GameOperatorDelete(Nil());
        g_aiSiteTreeNil = nullptr;
    }
}

// Purpose: Adds a site candidate to a site tree, in key order (std::_Tree::
// insert; the site trees allow equal keys).
SiteTreeInsertResult* __fastcall AI_SiteTree_insert(SiteTree* self, void* /*edx*/, SiteTreeInsertResult* out,
                                                    const SiteValue* value) {
    SiteTreeNode* y = self->head;
    SiteTreeNode* x = Root(self);
    bool goLeft = true;
    {
        GameLockit lock;
        while (x != Nil()) {
            y = x;
            goLeft = value->key < x->value.key;
            x = goLeft ? x->left : x->right;
        }
    }
    const auto insertBelow = [&]() {
        SiteTreeNode* node;
        reinterpret_cast<SiteTreeInsertBelowFn>(kInsertBelow)(self, nullptr, &node, x, y, value);
        return node;
    };
    // Bytes 5..7 of the result: the original's stack leftovers (ours 0).
    *out = SiteTreeInsertResult{};
    if (self->multi != 0) {
        out->where = insertBelow();
        out->inserted = 1;
        return out;
    }
    SiteTreeNode* p = y;
    if (goLeft) {
        if (y == Leftmost(self)) {
            out->where = insertBelow();
            out->inserted = 1;
            return out;
        }
        Decrement(&p);
    }
    if (p->value.key < value->key) {
        out->where = insertBelow();
        out->inserted = 1;
        return out;
    }
    out->where = p;
    out->inserted = 0;
    return out;
}

// Purpose: Erases the site candidates from first up to last (all of them
// at once when the range is the whole tree).
SiteTreeNode** __fastcall AI_SiteTree_erase_range(SiteTree* self, void* /*edx*/, SiteTreeNode** out,
                                                  SiteTreeNode* first, SiteTreeNode* last) {
    if (self->size == 0 || first != Leftmost(self) || last != self->head) {
        while (first != last) {
            SiteTreeNode* where = first;
            Increment(&first);
            SiteTreeNode* next;
            reinterpret_cast<SiteTreeEraseFn>(kErase)(self, nullptr, &next, where);
        }
        *out = first;
        return out;
    }
    GameLockit lock;
    {
        // _Erase(_Root()) inlined: the root's left spine here, the right
        // subtrees through 0x559ed0.
        GameLockit eraseLock;
        SiteTreeNode* node = Root(self);
        while (node != Nil()) {
            reinterpret_cast<SiteTreeEraseSubtreeFn>(kEraseSubtree)(self, nullptr, node->right);
            SiteTreeNode* left = node->left;
            msvc6::GameOperatorDelete(node);
            node = left;
        }
    }
    Root(self) = Nil();
    self->size = 0;
    Leftmost(self) = self->head;
    Rightmost(self) = self->head;
    *out = Leftmost(self);
    return out;
}

// Purpose: Empties a site tree (RefillAreaSiteList 0x5579d0 clears 15 of
// them before refilling).
SiteTreeNode** __fastcall AI_SiteTree_clear(SiteTree* self, void* /*edx*/) {
    SiteTreeNode* result;
    // EAX is the address of this stack slot in the original; no caller
    // uses it.
    return reinterpret_cast<SiteTreeEraseRangeFn>(kEraseRange)(self, nullptr, &result, Leftmost(self), self->head);
}

namespace {

// 0x559b7e..0x559e80: the rebalancing after a black node was taken out;
// x is the node that took its place (possibly the nil node).
void FixAfterErase(SiteTree* tree, SiteTreeNode* x) {
    while (x != Root(tree) && x->color == kTreeBlack) {
        if (x == x->parent->left) {
            SiteTreeNode* w = x->parent->right;
            if (w->color == kTreeRed) {
                w->color = kTreeBlack;
                x->parent->color = kTreeRed;
                RotateLeft(tree, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == kTreeBlack && w->right->color == kTreeBlack) {
                w->color = kTreeRed;
                x = x->parent;
                continue;
            }
            if (w->right->color == kTreeBlack) {
                w->left->color = kTreeBlack;
                w->color = kTreeRed;
                RotateRight(tree, w);
                w = x->parent->right;
            }
            w->color = x->parent->color;
            x->parent->color = kTreeBlack;
            w->right->color = kTreeBlack;
            RotateLeft(tree, x->parent);
            break;
        }
        SiteTreeNode* w = x->parent->left;
        if (w->color == kTreeRed) {
            w->color = kTreeBlack;
            x->parent->color = kTreeRed;
            RotateRight(tree, x->parent);
            w = x->parent->left;
        }
        if (w->right->color == kTreeBlack && w->left->color == kTreeBlack) {
            w->color = kTreeRed;
            x = x->parent;
            continue;
        }
        if (w->left->color == kTreeBlack) {
            w->right->color = kTreeBlack;
            w->color = kTreeRed;
            RotateLeft(tree, w);
            w = x->parent->left;
        }
        w->color = x->parent->color;
        x->parent->color = kTreeBlack;
        w->left->color = kTreeBlack;
        RotateRight(tree, x->parent);
        break;
    }
    x->color = kTreeBlack;
}

}  // namespace

// Purpose: Takes one site candidate out of a site tree (T_MAP_WORKER_AGENT
// takes the best one when it orders a building); returns the next one.
SiteTreeNode** __fastcall AI_SiteTree_erase(SiteTree* self, void* /*edx*/, SiteTreeNode** out, SiteTreeNode* where) {
    SiteTreeNode* const z = where;
    Increment(&where);
    GameLockit lock;
    SiteTreeNode* y = z;
    SiteTreeNode* x;
    if (z->left == Nil()) {
        x = z->right;
    } else if (z->right == Nil()) {
        x = z->left;
    } else {
        y = Min(z->right);
        x = y->right;
    }
    if (y != z) {
        // 0x559a58: y (z's successor) takes z's place.
        z->left->parent = y;
        y->left = z->left;
        if (y == z->right) {
            x->parent = y;
        } else {
            x->parent = y->parent;
            y->parent->left = x;
            y->right = z->right;
            z->right->parent = y;
        }
        if (Root(self) == z) {
            Root(self) = y;
        } else if (z->parent->left == z) {
            z->parent->left = y;
        } else {
            z->parent->right = y;
        }
        y->parent = z->parent;
        std::swap(y->color, z->color);
        y = z;
    } else {
        // 0x5599fe: z had at most one child, x.
        x->parent = y->parent;
        if (Root(self) == z) {
            Root(self) = x;
        } else if (z->parent->left == z) {
            z->parent->left = x;
        } else {
            z->parent->right = x;
        }
        if (Leftmost(self) == z) {
            Leftmost(self) = z->right == Nil() ? z->parent : Min(x);
        }
        if (Rightmost(self) == z) {
            Rightmost(self) = z->left == Nil() ? z->parent : Max(x);
        }
    }
    if (y->color == kTreeBlack) {
        FixAfterErase(self, x);
    }
    msvc6::GameOperatorDelete(y);
    --self->size;
    *out = where;
    return out;
}

// Purpose: Frees a subtree of site tree nodes without rebalancing (the
// whole-tree erase).
void __fastcall AI_SiteTree_EraseSubtree(SiteTree* self, void* /*edx*/, SiteTreeNode* node) {
    GameLockit lock;
    while (node != Nil()) {
        reinterpret_cast<SiteTreeEraseSubtreeFn>(kEraseSubtree)(self, nullptr, node->right);
        SiteTreeNode* left = node->left;
        msvc6::GameOperatorDelete(node);
        node = left;
    }
}

// Purpose: Links a new site tree node with the value below y (insert has
// found the place) and rebalances the tree.
SiteTreeNode** __fastcall AI_SiteTree_InsertBelow(SiteTree* self, void* /*edx*/, SiteTreeNode** out, SiteTreeNode* x,
                                                  SiteTreeNode* y, const SiteValue* value) {
    GameLockit lock;
    SiteTreeNode* const z = NewNode();
    z->parent = y;
    z->color = kTreeRed;
    z->left = Nil();
    z->right = Nil();
    CopySixBytes(&z->value, value);
    ++self->size;
    if (y == self->head || x != Nil() || value->key < y->value.key) {
        y->left = z;
        if (y == self->head) {
            Root(self) = z;
            Rightmost(self) = z;
        } else if (y == Leftmost(self)) {
            Leftmost(self) = z;
        }
    } else {
        y->right = z;
        if (y == Rightmost(self)) {
            Rightmost(self) = z;
        }
    }
    for (x = z; x != Root(self) && x->parent->color == kTreeRed;) {
        SiteTreeNode* const grandparent = x->parent->parent;
        if (x->parent == grandparent->left) {
            SiteTreeNode* const uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                x->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
                continue;
            }
            if (x == x->parent->right) {
                x = x->parent;
                RotateLeft(self, x);
            }
            x->parent->color = kTreeBlack;
            x->parent->parent->color = kTreeRed;
            RotateRight(self, x->parent->parent);
        } else {
            SiteTreeNode* const uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                x->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
                continue;
            }
            if (x == x->parent->left) {
                x = x->parent;
                RotateRight(self, x);
            }
            x->parent->color = kTreeBlack;
            x->parent->parent->color = kTreeRed;
            RotateLeft(self, x->parent->parent);
        }
    }
    Root(self)->color = kTreeBlack;
    *out = z;
    return out;
}

// Purpose: Steps a site tree iterator back to the previous candidate (from
// the head: to the last one).
void __fastcall AI_SiteTree_iterator_Dec(SiteTreeNode** self, void* /*edx*/) {
    GameLockit lock;
    SiteTreeNode* node = *self;
    if (node->color == kTreeRed && node->parent->parent == node) {
        *self = node->right;  // the head: rightmost
    } else if (node->left != Nil()) {
        *self = Max(node->left);
    } else {
        SiteTreeNode* parent = node->parent;
        while (*self == parent->left) {
            *self = parent;
            parent = parent->parent;
        }
        *self = parent;
    }
}

// Purpose: Steps a site tree iterator on to the next candidate.
void __fastcall AI_SiteTree_iterator_Inc(SiteTreeNode** self, void* /*edx*/) {
    GameLockit lock;
    SiteTreeNode* node = *self;
    if (node->right != Nil()) {
        *self = Min(node->right);
        return;
    }
    SiteTreeNode* parent = node->parent;
    while (*self == parent->right) {
        *self = parent;
        parent = parent->parent;
    }
    if ((*self)->right != parent) {  // (not the head of a one-node tree)
        *self = parent;
    }
}

// Purpose: Gives a site tree its head node (and makes the shared nil node
// on first use); the start of the copy constructor.
void __fastcall AI_SiteTree_Init(SiteTree* self, void* /*edx*/) {
    InitHead(self);
}

// Purpose: Copies a site tree (the area record copy 0x55a440 copies all
// of a record's trees).
SiteTree* __fastcall AI_SiteTree_CopyConstruct(SiteTree* self, void* /*edx*/, const SiteTree* other) {
    self->allocator = other->allocator;
    self->comparator = other->comparator;
    self->multi = other->multi;
    reinterpret_cast<SiteTreeVoidFn>(kInit)(self, nullptr);
    reinterpret_cast<SiteTreeCopyFn>(kCopyTree)(self, nullptr, other);
    return self;
}

// Purpose: Copies another site tree's nodes into this (empty) one.
void __fastcall AI_SiteTree_Copy(SiteTree* self, void* /*edx*/, const SiteTree* other) {
    GameLockit lock;
    SiteTreeNode* root;
    {
        // _Copy(other's root, head) inlined: the root's left spine here.
        SiteTreeNode* parent = self->head;
        SiteTreeNode* node = other->head->parent;
        GameLockit copyLock;
        root = node;
        for (; node != Nil(); node = node->left) {
            const std::uint32_t color = node->color;
            SiteTreeNode* const copy = NewNode();
            copy->color = color;
            if (root == node) {
                root = copy;
            }
            copy->parent = parent;
            copy->right = reinterpret_cast<SiteTreeCopySubtreeFn>(kCopySubtree)(self, nullptr, node->right, copy);
            CopySixBytes(&copy->value, &node->value);
            parent->left = copy;
            parent = copy;
        }
        parent->left = Nil();
    }
    Root(self) = root;
    self->size = other->size;
    if (Root(self) != Nil()) {
        Leftmost(self) = Min(Root(self));
        Rightmost(self) = Max(Root(self));
    } else {
        Leftmost(self) = self->head;
        Rightmost(self) = self->head;
    }
}

// Purpose: Copies a subtree of a site tree below `parent` (recursively for
// the right subtrees); returns the copy's root.
SiteTreeNode* __fastcall AI_SiteTree_CopySubtree(SiteTree* self, void* /*edx*/, SiteTreeNode* node,
                                                 SiteTreeNode* parent) {
    GameLockit lock;
    SiteTreeNode* root = node;
    for (; node != Nil(); node = node->left) {
        const std::uint32_t color = node->color;
        SiteTreeNode* const copy = NewNode();
        copy->parent = parent;
        copy->color = color;
        if (root == node) {
            root = copy;
        }
        copy->right = reinterpret_cast<SiteTreeCopySubtreeFn>(kCopySubtree)(self, nullptr, node->right, copy);
        std::memcpy(&copy->value, &node->value, sizeof(SiteValue));  // (inline dword + word copy)
        parent->left = copy;
        parent = copy;
    }
    parent->left = Nil();
    return root;
}


// Purpose: Takes a site candidate out of an area's site tree for
// T_MAP_WORKER_AGENT's build orders; returns the next one.
SiteTreeNode** __fastcall AI_SiteTree_EraseIterator(SiteTree* self, void* /*edx*/, SiteTreeNode** out,
                                                    SiteTreeNode* where) {
    SiteTreeNode* next;
    reinterpret_cast<SiteTreeEraseFn>(kErase)(self, nullptr, &next, where);
    *out = next;
    return out;
}

// Purpose: it-- on a site tree iterator (T_MAP_WORKER_AGENT steps back from
// end() to the best candidate).
SiteTreeNode** __fastcall AI_SiteTree_iterator_PostDecrement(SiteTreeNode** self, void* /*edx*/, SiteTreeNode** out,
                                                             std::uint32_t /*unused*/) {
    SiteTreeNode* const old = *self;
    Decrement(self);
    *out = old;
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Two trees in the game's heap, A worked on by the original functions and B
// by ours, go through the same random operations (insert with small keys
// so that equal keys are common, erase, erase of a range or of everything,
// clear, ++ / -- of iterators, copies); after each one the trees must have
// the same shape, colours, values, parent links, leftmost / rightmost and
// size, and the results must point at the same in-order positions. Both
// trees share the game's nil node, so the nil node's parent (which erase
// sets) is compared right after each side's operation. The nil node's
// creation and release are compared separately (globals saved and
// restored). In-process the trees' inner calls (0x55a320, 0x5599b0, ...)
// reach our replacements on both sides; the emulator spec compares the
// pure original code, including the order of the _Lockit calls.

namespace {

struct SiteTreeFns {
    SiteTreeCtorFn construct;
    SiteTreeVoidFn destruct;
    SiteTreeInsertFn insert;
    SiteTreeEraseRangeFn eraseRange;
    SiteTreeClearFn clear;
    SiteTreeEraseFn erase;
    SiteTreeIteratorFn increment;
    SiteTreeIteratorFn decrement;
    SiteTreeCopyCtorFn copyConstruct;
    SiteTreeVoidFn init;
    SiteTreeEraseFn eraseIterator;
    SiteTreePostDecrementFn postDecrement;
};

SiteTreeFns OriginalSiteTreeFns() {
    return {Original_AI_SiteTree_Construct,    Original_AI_SiteTree_Destruct,     Original_AI_SiteTree_insert,
            Original_AI_SiteTree_erase_range,  Original_AI_SiteTree_clear,        Original_AI_SiteTree_erase,
            Original_AI_SiteTree_iterator_Inc, Original_AI_SiteTree_iterator_Dec, Original_AI_SiteTree_CopyConstruct,
            Original_AI_SiteTree_Init,         Original_AI_SiteTree_EraseIterator,
            Original_AI_SiteTree_iterator_PostDecrement};
}

SiteTreeFns OurSiteTreeFns() {
    return {&AI_SiteTree_Construct,    &AI_SiteTree_Destruct,     &AI_SiteTree_insert,
            &AI_SiteTree_erase_range,  &AI_SiteTree_clear,        &AI_SiteTree_erase,
            &AI_SiteTree_iterator_Inc, &AI_SiteTree_iterator_Dec, &AI_SiteTree_CopyConstruct,
            &AI_SiteTree_Init,         &AI_SiteTree_EraseIterator, &AI_SiteTree_iterator_PostDecrement};
}

// The nodes in order; index -1 is the head.
struct TreeIndex {
    std::vector<SiteTreeNode*> nodes;
    std::map<const SiteTreeNode*, int> position;

    explicit TreeIndex(const SiteTree& tree) {
        position[tree.head] = -1;
        Walk(tree.head->parent);
    }
    void Walk(SiteTreeNode* node) {
        if (node == Nil()) {
            return;
        }
        Walk(node->left);
        position[node] = static_cast<int>(nodes.size());
        nodes.push_back(node);
        Walk(node->right);
    }
    // -2: the nil node, -3: something else.
    int Of(const SiteTreeNode* node) const {
        if (node == Nil()) {
            return -2;
        }
        const auto it = position.find(node);
        return it == position.end() ? -3 : it->second;
    }
    SiteTreeNode* At(const SiteTree& tree, int index) const {
        return index < 0 || index >= static_cast<int>(nodes.size()) ? tree.head : nodes[index];
    }
};

bool SameSubtree(test::Context& ctx, const TreeIndex& ia, const TreeIndex& ib, const SiteTreeNode* a,
                 const SiteTreeNode* b) {
    if ((a == Nil()) != (b == Nil())) {
        ctx.Fail(__FILE__, __LINE__, "  shape differs at position %d / %d", ia.Of(a), ib.Of(b));
        return false;
    }
    if (a == Nil()) {
        return true;
    }
    bool same = S3RE_EXPECT_EQ(ia.Of(a), ib.Of(b));
    same &= S3RE_EXPECT_EQ(a->color, b->color);
    same &= S3RE_EXPECT_MEM_EQ(&a->value, &b->value, sizeof(SiteValue));
    same &= S3RE_EXPECT_EQ(ia.Of(a->parent), ib.Of(b->parent));
    return same && SameSubtree(ctx, ia, ib, a->left, b->left) && SameSubtree(ctx, ia, ib, a->right, b->right);
}

bool SameTree(test::Context& ctx, const SiteTree& a, const SiteTree& b) {
    const TreeIndex ia(a);
    const TreeIndex ib(b);
    bool same = S3RE_EXPECT_EQ(a.size, b.size);
    same &= S3RE_EXPECT_EQ(a.size, static_cast<std::uint32_t>(ia.nodes.size()));
    same &= S3RE_EXPECT_EQ(a.allocator, b.allocator);
    same &= S3RE_EXPECT_EQ(a.comparator, b.comparator);
    same &= S3RE_EXPECT_EQ(a.multi, b.multi);
    same &= S3RE_EXPECT_EQ(a.head->color, b.head->color);
    same &= S3RE_EXPECT_EQ(ia.Of(a.head->left), ib.Of(b.head->left));
    same &= S3RE_EXPECT_EQ(ia.Of(a.head->right), ib.Of(b.head->right));
    if (a.head->parent != Nil() && b.head->parent != Nil()) {
        same &= S3RE_EXPECT_EQ(ia.Of(a.head->parent->parent), ib.Of(b.head->parent->parent));
    }
    return same && SameSubtree(ctx, ia, ib, a.head->parent, b.head->parent);
}

// Where erase left the nil node's parent link (the tests set it to null
// before each side's erase; no tree code reads it there): -4 untouched.
int NilParentIndex(const SiteTree& tree) {
    return Nil()->parent == nullptr ? -4 : TreeIndex(tree).Of(Nil()->parent);
}

SiteValue RandomSiteValue(test::Rng& rng) {
    SiteValue value;
    value.key = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(8));
    value.pad = static_cast<std::uint8_t>(rng.Next32());
    value.x = static_cast<std::uint16_t>(rng.Next32());
    value.y = static_cast<std::uint16_t>(rng.Next32());
    return value;
}

void CompareSiteTrees(test::Context& ctx, const SiteTreeFns& ours, int cases) {
    const SiteTreeFns original = OriginalSiteTreeFns();
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t refs = g_aiSiteTreeNilRefs;
        const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(rng.Next32()),
                                       static_cast<std::uint8_t>(rng.Next32())};
        SiteTree a{};
        SiteTree b{};
        S3RE_EXPECT_EQ(original.construct(&a, nullptr, &bytes[0], &bytes[1]), &a);
        S3RE_EXPECT_EQ(ours.construct(&b, nullptr, &bytes[0], &bytes[1]), &b);
        if (rng.Chance(1, 4)) {
            a.multi = b.multi = 0;  // the unique-key path of insert
        }
        bool same = SameTree(ctx, a, b);
        const int steps = static_cast<int>(rng.Below(80));
        for (int step = 0; step < steps && same; ++step) {
            const TreeIndex ia(a);
            const TreeIndex ib(b);
            const int n = static_cast<int>(ia.nodes.size());
            const int op = static_cast<int>(rng.Below(n == 0 ? 4 : 11));
            int resultA = 0;
            int resultB = 0;
            int nilA = 0;
            int nilB = 0;
            if (op <= 3) {
                const SiteValue value = RandomSiteValue(rng);
                SiteTreeInsertResult ra{};
                SiteTreeInsertResult rb{};
                S3RE_EXPECT_EQ(original.insert(&a, nullptr, &ra, &value), &ra);
                S3RE_EXPECT_EQ(ours.insert(&b, nullptr, &rb, &value), &rb);
                same &= S3RE_EXPECT_EQ(ra.inserted, rb.inserted);
                resultA = TreeIndex(a).Of(ra.where);
                resultB = TreeIndex(b).Of(rb.where);
            } else if (op <= 5) {
                const int k = static_cast<int>(rng.Below(n));
                SiteTreeNode* oa = nullptr;
                SiteTreeNode* ob = nullptr;
                Nil()->parent = nullptr;
                const bool wrapper = rng.Chance(1, 4);
                S3RE_EXPECT_EQ((wrapper ? original.eraseIterator : original.erase)(&a, nullptr, &oa, ia.nodes[k]), &oa);
                nilA = NilParentIndex(a);
                Nil()->parent = nullptr;
                S3RE_EXPECT_EQ((wrapper ? ours.eraseIterator : ours.erase)(&b, nullptr, &ob, ib.nodes[k]), &ob);
                nilB = NilParentIndex(b);
                resultA = TreeIndex(a).Of(oa);
                resultB = TreeIndex(b).Of(ob);
            } else if (op == 6) {
                int first = static_cast<int>(rng.Below(n + 1));
                int last = first + static_cast<int>(rng.Below(n + 1 - first));
                if (rng.Chance(1, 3)) {
                    first = 0;
                    last = n;
                }
                SiteTreeNode* oa = nullptr;
                SiteTreeNode* ob = nullptr;
                Nil()->parent = nullptr;
                S3RE_EXPECT_EQ(original.eraseRange(&a, nullptr, &oa, ia.At(a, first), ia.At(a, last)), &oa);
                nilA = NilParentIndex(a);
                Nil()->parent = nullptr;
                S3RE_EXPECT_EQ(ours.eraseRange(&b, nullptr, &ob, ib.At(b, first), ib.At(b, last)), &ob);
                nilB = NilParentIndex(b);
                resultA = TreeIndex(a).Of(oa);
                resultB = TreeIndex(b).Of(ob);
            } else if (op == 7) {
                if (rng.Chance(1, 4)) {
                    original.clear(&a, nullptr);
                    ours.clear(&b, nullptr);
                }
            } else if (op <= 9) {
                const int k = static_cast<int>(rng.Below(n + 1)) - 1;  // -1: the head
                SiteTreeNode* ita = ia.At(a, k);
                SiteTreeNode* itb = ib.At(b, k);
                SiteTreeNode* olda = nullptr;
                SiteTreeNode* oldb = nullptr;
                if (op == 8) {
                    original.increment(&ita, nullptr);
                    ours.increment(&itb, nullptr);
                } else if (rng.Chance(1, 2)) {
                    original.decrement(&ita, nullptr);
                    ours.decrement(&itb, nullptr);
                } else {
                    S3RE_EXPECT_EQ(original.postDecrement(&ita, nullptr, &olda, 0), &olda);
                    S3RE_EXPECT_EQ(ours.postDecrement(&itb, nullptr, &oldb, 0), &oldb);
                    same &= S3RE_EXPECT_EQ(ia.Of(olda), ib.Of(oldb));
                }
                resultA = ia.Of(ita);
                resultB = ib.Of(itb);
            } else {
                SiteTree ca{};
                SiteTree cb{};
                S3RE_EXPECT_EQ(original.copyConstruct(&ca, nullptr, &a), &ca);
                S3RE_EXPECT_EQ(ours.copyConstruct(&cb, nullptr, &b), &cb);
                same &= SameTree(ctx, ca, cb);
                same &= SameTree(ctx, a, ca);
                original.destruct(&ca, nullptr);
                ours.destruct(&cb, nullptr);
            }
            same &= S3RE_EXPECT_EQ(resultA, resultB);
            same &= S3RE_EXPECT_EQ(nilA, nilB);
            same &= S3RE_EXPECT_EQ(Nil()->color, kTreeBlack);
            same &= SameTree(ctx, a, b);
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  case %d, step %d, operation %d", i, step, op);
            }
        }
        original.destruct(&a, nullptr);
        ours.destruct(&b, nullptr);
        S3RE_EXPECT_EQ(g_aiSiteTreeNilRefs.get(), refs);
    }
}

// The nil node's creation by the first tree and release by the last one.
void CompareSiteTreeNil(test::Context& ctx, const SiteTreeFns& ours) {
    SiteTreeNode* const savedNil = g_aiSiteTreeNil;
    const std::uint32_t savedRefs = g_aiSiteTreeNilRefs;
    const SiteTreeFns original = OriginalSiteTreeFns();
    struct Seen {
        std::uint32_t refs;
        SiteTreeNode nil;
        SiteTreeNode head;
        std::uint32_t size;
        std::uint8_t bytes[2];
        std::uint32_t refsAfter;
        bool released;
    };
    const auto run = [&](const SiteTreeFns& f, bool viaInit) {
        g_aiSiteTreeNil = nullptr;
        g_aiSiteTreeNilRefs = 0;
        SiteTree tree{};
        tree.size = 77;
        if (viaInit) {
            f.init(&tree, nullptr);  // _Init alone (the copy constructor's first step)
        } else {
            const std::uint8_t bytes[2] = {1, 2};
            f.construct(&tree, nullptr, &bytes[0], &bytes[1]);
        }
        Seen seen{};
        seen.refs = g_aiSiteTreeNilRefs;
        seen.nil = *g_aiSiteTreeNil.get();
        seen.head = *tree.head;
        seen.size = tree.size;
        seen.bytes[0] = tree.allocator;
        seen.bytes[1] = tree.comparator;
        S3RE_EXPECT(tree.head->left == tree.head && tree.head->right == tree.head);
        S3RE_EXPECT(tree.head->parent == g_aiSiteTreeNil.get());
        f.destruct(&tree, nullptr);
        seen.refsAfter = g_aiSiteTreeNilRefs;
        seen.released = g_aiSiteTreeNil.get() == nullptr;
        return seen;
    };
    for (bool viaInit : {false, true}) {
        const Seen a = run(original, viaInit);
        const Seen b = run(ours, viaInit);
        S3RE_EXPECT_EQ(a.refs, b.refs);
        S3RE_EXPECT_EQ(a.nil.left, b.nil.left);
        S3RE_EXPECT_EQ(a.nil.parent, b.nil.parent);
        S3RE_EXPECT_EQ(a.nil.right, b.nil.right);
        S3RE_EXPECT_EQ(a.nil.color, b.nil.color);
        S3RE_EXPECT_EQ(a.head.color, b.head.color);
        S3RE_EXPECT_EQ(a.size, b.size);
        S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes));
        S3RE_EXPECT_EQ(a.refsAfter, b.refsAfter);
        S3RE_EXPECT_EQ(a.released, b.released);
    }
    g_aiSiteTreeNil = savedNil;
    g_aiSiteTreeNilRefs = savedRefs;
}

// Wrong: the leftmost link isn't updated when the first node goes.
SiteTreeNode** __fastcall WrongSiteTreeErase(SiteTree* self, void* edx, SiteTreeNode** out, SiteTreeNode* where) {
    const bool first = where == Leftmost(self);
    SiteTreeNode** result = AI_SiteTree_erase(self, edx, out, where);
    if (first && self->size > 1) {
        Leftmost(self) = Leftmost(self)->parent;
    }
    return result;
}

// Wrong: a new key goes before the equal ones already there.
SiteTreeInsertResult* __fastcall WrongSiteTreeInsert(SiteTree* self, void* edx, SiteTreeInsertResult* out,
                                                     const SiteValue* value) {
    SiteValue lower = *value;
    const bool shifted = lower.key > 0;
    if (shifted) {
        --lower.key;  // lands after the smaller keys...
    }
    SiteTreeInsertResult* result = AI_SiteTree_insert(self, edx, out, &lower);
    if (shifted && out->inserted != 0) {
        ++out->where->value.key;  // ...then gets its own key back
    }
    return result;
}

// Wrong: reports "not inserted" for a new key (also the emulator
// self-test's pairing next to insert's ignored padding bytes).
SiteTreeInsertResult* __fastcall WrongSiteTreeInsertFlag(SiteTree* self, void* edx, SiteTreeInsertResult* out,
                                                         const SiteValue* value) {
    SiteTreeInsertResult* result = AI_SiteTree_insert(self, edx, out, value);
    if (self->size == 1) {
        out->inserted = 0;
    }
    return result;
}

}  // namespace

S3RE_TEST(AiSiteTree_MatchOriginal) {
    CompareSiteTrees(ctx, OurSiteTreeFns(), 1500);
    CompareSiteTreeNil(ctx, OurSiteTreeFns());
}

S3RE_TEST(AiSiteTree_ComparisonDetectsWrongImplementation) {
    {
        SiteTreeFns wrong = OurSiteTreeFns();
        wrong.erase = &WrongSiteTreeErase;
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSiteTrees(quiet, wrong, 1500);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        SiteTreeFns wrong = OurSiteTreeFns();
        wrong.insert = &WrongSiteTreeInsert;
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSiteTrees(quiet, wrong, 1500);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        SiteTreeFns wrong = OurSiteTreeFns();
        wrong.insert = &WrongSiteTreeInsertFlag;
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSiteTrees(quiet, wrong, 1500);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
