// The CEvaLand's area records and their lists (Rainer\Cevaland.cpp;
// re/computer_players.md "The site trees", layouts AiArea / AiAreaNode /
// AiLandEvaluation in ai_layout.h), and the CEvaLand's construction.
//
//   0x0055a440 CEvaLand::Area::Area(const Area& other) thiscall, ret 4:
//              copies the fields (not the padding bytes +0x02, +0x15..+0x17,
//              +0x3e7) and copy-constructs the 39 site trees in order (32
//              through SiteTree's copy constructor 0x55aa20, 7 inlined as
//              its bytes + _Init 0x55a970 + _Copy 0x55aa50: the same calls);
//              EAX = this.
//   0x00556080 CEvaLand::Area::~Area() thiscall: destroys the 39 site trees
//              from the last to the first (31 through ~SiteTree 0x559170,
//              8 inlined with begin() 0x559260: the same effects). EAX: left
//              over (not used).
//   0x0055a3d0 AreaList::_Construct(void* place, const Area& value)
//              fastcall: if place, the copy constructor there. EAX: left
//              over (not used).
//   0x005594f0 AreaList::insert(AiAreaNode** out, where, const Area& value)
//              thiscall, ret 0xc (std::list::insert; AddArea 0x555260 with
//              where = end): a new 0x744-byte node linked before `where`
//              (MSVC6 _Buynode: a null `where` or prev links the node to
//              itself), value copied with _Construct 0x55a3d0, ++size;
//              *out = the node, EAX = out.
//   0x00558e80 CEvaLand::RemoveArea(area, player) thiscall, ret 8 (low
//              words): the first node of lists[player] with that area id
//              is unlinked, destroyed (~Area 0x556080), freed, --size and
//              areas_changed[player] = 1; EAX = the new size. Not found:
//              EAX = the list's head node.
//   0x00553d30 CEvaLand::Acquire() (no this): ++g_aiEvaLandRefCount; the
//              first call makes the instance (operator new 0x15c, the
//              constructor; null: Win32::ThrowOutOfMemory("pEvalMap",
//              "E:\Siedler3\Rainer\Cevaland.cpp", 70)). EAX = g_aiEvaLand.
//   0x00553e10 CEvaLand::CEvaLand() thiscall: the CRT's eh vector
//              constructor iterator 0x61ff7f (stdcall) builds the 20 lists
//              (12 bytes each; constructor 0x553e70, destructor 0x5591e0),
//              ResetGrid 0x554070, Init 0x553ea0; EAX = this.
//   0x00553ea0 CEvaLand::Init() thiscall: grid width = height = world map
//              size / 8 (the world of g_aiPlayer, signed division), cell
//              count, 64 tiles per cell, cursor 0; the grid (count * 0xc4
//              bytes) from operator new, each cell's 20 bitsets constructed
//              (vector constructor iterator 0x42c9d0 with 0x559240), null:
//              ThrowOutOfMemory("map_part_list_ptr", ..., 141); then the
//              grid zeroed, areas_changed cleared, EvaluateAllCells
//              0x5540a0, SumAllCells 0x5546c0, g_aiLandEvaluation = this.
//              EAX: left over (not used).
//   0x0042c9d0 `vector constructor iterator'(array, size, count, ctor)
//              stdcall, ret 0x10 (compiler helper): ctor(ECX = element)
//              for each of count elements (none for -INT_MAX..0; INT_MIN
//              counts as 0x80000000, the dec / js test). EAX: left over.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kSiteTreeCopyConstruct = 0x0055aa20;  // AI::SiteTree::SiteTree(const SiteTree&)
constexpr std::uintptr_t kSiteTreeDestruct = 0x00559170;       // AI::SiteTree::~SiteTree
constexpr std::uintptr_t kAreaCopy = 0x0055a440;               // CEvaLand::Area::Area(const Area&)
constexpr std::uintptr_t kAreaDestruct = 0x00556080;           // CEvaLand::Area::~Area
constexpr std::uintptr_t kAreaConstruct = 0x0055a3d0;          // AreaList::_Construct (fastcall)
constexpr std::uintptr_t kEvaLandConstruct = 0x00553e10;       // CEvaLand::CEvaLand
constexpr std::uintptr_t kEvaLandInit = 0x00553ea0;            // CEvaLand::Init
constexpr std::uintptr_t kResetGrid = 0x00554070;              // CEvaLand::ResetGrid
constexpr std::uintptr_t kEvaluateAllCells = 0x005540a0;       // CEvaLand::EvaluateAllCells
constexpr std::uintptr_t kSumAllCells = 0x005546c0;            // CEvaLand::SumAllCells
constexpr std::uintptr_t kVectorConstructor = 0x0042c9d0;      // `vector constructor iterator' (stdcall)
constexpr std::uintptr_t kEhVectorConstructor = 0x0061ff7f;    // CRT `eh vector constructor iterator' (stdcall)
constexpr std::uintptr_t kListConstruct = 0x00553e70;          // std::list<Area>::list (thiscall)
constexpr std::uintptr_t kListDestruct = 0x005591e0;           // std::list<Area>::~list (thiscall)
constexpr std::uintptr_t kBitsetConstruct = 0x00559240;        // std::bitset<57>::bitset (thiscall)
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;       // Win32::ThrowOutOfMemory (never returns)

constexpr std::uint32_t kTilesPerCell = 64;
constexpr std::int32_t kCellSide = 8;
constexpr std::uint32_t kAcquireLine = 70;    // the line numbers of the out-of-memory texts
constexpr std::uint32_t kGridLine = 141;

using ThisFn = void(__fastcall*)(void* self, void* edx);

void ThrowOutOfMemory(const GameText& text, std::uint32_t line) {
    using Fn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
    reinterpret_cast<Fn>(kThrowOutOfMemory)(text.c_str(), texts::kEvaLandSourceFile.c_str(), line);
}

// The fields the copy constructor copies (0x55a460..0x55a511).
void CopyAreaFields(AiArea& to, const AiArea& from) {
    to.area = from.area;
    to.basic_economy = from.basic_economy;
    to.short_of_wood = from.short_of_wood;
    to.short_of_stone = from.short_of_stone;
    to.force_production = from.force_production;
    to.unk014 = from.unk014;
    to.unk018 = from.unk018;
    to.sites = from.sites;
    std::memcpy(to.best, from.best, sizeof(to.best));
    std::memcpy(to.candidate_values, from.candidate_values, sizeof(to.candidate_values));
    std::memcpy(to.building_counts, from.building_counts, sizeof(to.building_counts));
    std::memcpy(to.type_blocked, from.type_blocked, sizeof(to.type_blocked));
    std::memcpy(to.site_tree_of_type, from.site_tree_of_type, sizeof(to.site_tree_of_type));
}

}  // namespace

using AreaCopyFn = AiArea*(__fastcall*)(AiArea* self, void* edx, const AiArea* other);
using AreaDestructFn = void(__fastcall*)(AiArea* self, void* edx);
using AreaConstructFn = void(__fastcall*)(AiArea* place, const AiArea* value);
using AreaInsertFn = AiAreaNode**(__fastcall*)(AiList* self, void* edx, AiAreaNode** out, AiAreaNode* where,
                                               const AiArea* value);
using RemoveAreaFn = std::uint32_t(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t area,
                                                std::uint32_t player);
using AcquireFn = AiLandEvaluation*(__cdecl*)();
using EvaLandConstructFn = AiLandEvaluation*(__fastcall*)(AiLandEvaluation* self, void* edx);
using EvaLandInitFn = void(__fastcall*)(AiLandEvaluation* self, void* edx);
using VectorConstructorFn = void(__stdcall*)(void* array, std::uint32_t size, std::int32_t count,
                                             std::uintptr_t constructor);

AiArea* __fastcall AI_CEvaLand_Area_Copy(AiArea* self, void* edx, const AiArea* other);
void __fastcall AI_CEvaLand_Area_Destruct(AiArea* self, void* edx);
void __fastcall AI_AreaList_Construct(AiArea* place, const AiArea* value);
AiAreaNode** __fastcall AI_AreaList_insert(AiList* self, void* edx, AiAreaNode** out, AiAreaNode* where,
                                           const AiArea* value);
std::uint32_t __fastcall AI_CEvaLand_RemoveArea(AiLandEvaluation* self, void* edx, std::uint32_t area,
                                                std::uint32_t player);
AiLandEvaluation* __cdecl AI_CEvaLand_Acquire();
AiLandEvaluation* __fastcall AI_CEvaLand_Construct(AiLandEvaluation* self, void* edx);
void __fastcall AI_CEvaLand_Init(AiLandEvaluation* self, void* edx);
void __stdcall VectorConstructorIterator(void* array, std::uint32_t size, std::int32_t count,
                                         std::uintptr_t constructor);

S3RE_HOOK(0x0055a440, AI_CEvaLand_Area_Copy, AreaCopyFn, Original_AI_CEvaLand_Area_Copy, AI_CEvaLand_Area_Copy);
S3RE_HOOK(0x00556080, AI_CEvaLand_Area_Destruct, AreaDestructFn, Original_AI_CEvaLand_Area_Destruct,
          AI_CEvaLand_Area_Destruct);
S3RE_HOOK(0x0055a3d0, AI_AreaList_Construct, AreaConstructFn, Original_AI_AreaList_Construct, AI_AreaList_Construct);
S3RE_HOOK(0x005594f0, AI_AreaList_insert, AreaInsertFn, Original_AI_AreaList_insert, AI_AreaList_insert);
S3RE_HOOK(0x00558e80, AI_CEvaLand_RemoveArea, RemoveAreaFn, Original_AI_CEvaLand_RemoveArea, AI_CEvaLand_RemoveArea);
S3RE_HOOK(0x00553d30, AI_CEvaLand_Acquire, AcquireFn, Original_AI_CEvaLand_Acquire, AI_CEvaLand_Acquire);
S3RE_HOOK(0x00553e10, AI_CEvaLand_Construct, EvaLandConstructFn, Original_AI_CEvaLand_Construct,
          AI_CEvaLand_Construct);
S3RE_HOOK(0x00553ea0, AI_CEvaLand_Init, EvaLandInitFn, Original_AI_CEvaLand_Init, AI_CEvaLand_Init);
S3RE_HOOK(0x0042c9d0, VectorConstructorIterator, VectorConstructorFn, Original_VectorConstructorIterator,
          VectorConstructorIterator);

// Purpose: Copies an area record of the land evaluation (AddArea appends a
// copy of the record it built to the player's list).
AiArea* __fastcall AI_CEvaLand_Area_Copy(AiArea* self, void* /*edx*/, const AiArea* other) {
    CopyAreaFields(*self, *other);
    using CopyFn = SiteTree*(__fastcall*)(SiteTree* self, void* edx, const SiteTree* other);
    for (std::size_t k = 0; k < std::size(self->site_trees); ++k) {
        reinterpret_cast<CopyFn>(kSiteTreeCopyConstruct)(&self->site_trees[k], nullptr, &other->site_trees[k]);
    }
    return self;
}

// Purpose: Destroys an area record (its 39 site trees).
void __fastcall AI_CEvaLand_Area_Destruct(AiArea* self, void* /*edx*/) {
    for (std::size_t k = std::size(self->site_trees); k-- > 0;) {
        reinterpret_cast<ThisFn>(kSiteTreeDestruct)(&self->site_trees[k], nullptr);
    }
}

// Purpose: Copy-constructs an area record in a new list node.
void __fastcall AI_AreaList_Construct(AiArea* place, const AiArea* value) {
    if (place != nullptr) {
        reinterpret_cast<AreaCopyFn>(kAreaCopy)(place, nullptr, value);
    }
}

// Purpose: Inserts a copy of an area record into a player's area list
// before `where` (AddArea appends).
AiAreaNode** __fastcall AI_AreaList_insert(AiList* self, void* /*edx*/, AiAreaNode** out, AiAreaNode* where,
                                           const AiArea* value) {
    AiAreaNode* const prev = where->prev;
    auto* node = static_cast<AiAreaNode*>(msvc6::GameOperatorNew(sizeof(AiAreaNode)));
    node->next = where != nullptr ? where : node;
    node->prev = prev != nullptr ? prev : node;
    where->prev = node;
    node->prev->next = node;
    reinterpret_cast<AreaConstructFn>(kAreaConstruct)(&node->value, value);
    ++self->count;
    *out = node;
    return out;
}

// Purpose: Removes the record of an economy area from a computer player's
// list when the player loses the area; marks the list as changed.
std::uint32_t __fastcall AI_CEvaLand_RemoveArea(AiLandEvaluation* self, void* /*edx*/, std::uint32_t area,
                                                std::uint32_t player) {
    const std::uint32_t index = player & 0xffff;
    AiList& list = self->lists[index];
    auto* const head = reinterpret_cast<AiAreaNode*>(list.head);
    for (AiAreaNode* node = head->next; node != head; node = node->next) {
        if (node->value.area != static_cast<std::uint16_t>(area)) {
            continue;
        }
        node->prev->next = node->next;
        node->next->prev = node->prev;
        reinterpret_cast<AreaDestructFn>(kAreaDestruct)(&node->value, nullptr);
        msvc6::GameOperatorDelete(node);
        --list.count;
        self->areas_changed[index] = 1;
        return list.count;
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(head));
}

// Purpose: Gives a computer player the shared land evaluation object,
// making it with the first player.
AiLandEvaluation* __cdecl AI_CEvaLand_Acquire() {
    ++g_aiEvaLandRefCount;
    if (g_aiEvaLand.get() != nullptr) {
        return g_aiEvaLand;
    }
    void* memory = msvc6::GameOperatorNew(sizeof(AiLandEvaluation));
    AiLandEvaluation* instance = nullptr;
    if (memory != nullptr) {
        instance = reinterpret_cast<EvaLandConstructFn>(kEvaLandConstruct)(static_cast<AiLandEvaluation*>(memory),
                                                                            nullptr);
    }
    g_aiEvaLand = instance;
    if (instance == nullptr) {
        ThrowOutOfMemory(texts::kEvalMap, kAcquireLine);
    }
    return g_aiEvaLand;
}

// Purpose: Constructs the land evaluation object: its 20 per-player area
// lists and the evaluated grid.
AiLandEvaluation* __fastcall AI_CEvaLand_Construct(AiLandEvaluation* self, void* /*edx*/) {
    using EhVectorFn = void(__stdcall*)(void* array, std::uint32_t size, std::uint32_t count, std::uintptr_t ctor,
                                        std::uintptr_t dtor);
    reinterpret_cast<EhVectorFn>(kEhVectorConstructor)(self->lists, sizeof(AiList),
                                                       sizeof(self->lists) / sizeof(AiList), kListConstruct,
                                                       kListDestruct);
    reinterpret_cast<EvaLandInitFn>(kResetGrid)(self, nullptr);
    reinterpret_cast<EvaLandInitFn>(kEvaLandInit)(self, nullptr);
    return self;
}

// Purpose: Builds and evaluates the land grid (8 x 8 tiles per cell) for
// the current map; makes this the land evaluation in use.
void __fastcall AI_CEvaLand_Init(AiLandEvaluation* self, void* /*edx*/) {
    const auto* world = reinterpret_cast<const CGameData*>(g_aiPlayer.get()->world);
    const std::int32_t side = static_cast<std::int32_t>(world->map_size) / kCellSide;
    self->tiles_per_cell = kTilesPerCell;
    self->cursor_x = 0;
    self->cursor_y = 0;
    self->width = static_cast<std::uint32_t>(side);
    self->height = static_cast<std::uint32_t>(side);
    const std::int32_t count = side * side;
    self->cell_count = static_cast<std::uint32_t>(count);
    auto* cells = static_cast<AiGridCell*>(msvc6::GameOperatorNew(static_cast<std::uint32_t>(count) * sizeof(AiGridCell)));
    if (cells != nullptr) {
        for (std::int32_t k = 0; k < count; ++k) {
            AiGridCell& cell = cells[k];
            reinterpret_cast<VectorConstructorFn>(kVectorConstructor)(
                cell.building_types, sizeof(cell.building_types[0]),
                static_cast<std::int32_t>(std::size(cell.building_types)), kBitsetConstruct);
        }
    }
    self->cells = cells;
    if (cells == nullptr) {
        ThrowOutOfMemory(texts::kMapPartListPtr, kGridLine);
    }
    std::memset(self->cells, 0, self->cell_count * sizeof(AiGridCell));
    std::memset(self->areas_changed, 0, sizeof(self->areas_changed));
    reinterpret_cast<EvaLandInitFn>(kEvaluateAllCells)(self, nullptr);
    reinterpret_cast<EvaLandInitFn>(kSumAllCells)(self, nullptr);
    g_aiLandEvaluation = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

// Purpose: Compiler helper: runs a constructor on every element of an
// array (the land grid cells' bitsets).
void __stdcall VectorConstructorIterator(void* array, std::uint32_t size, std::int32_t count,
                                         std::uintptr_t constructor) {
    auto* element = static_cast<std::uint8_t*>(array);
    // 0x42c9d4: dec / js on count, so INT_MIN counts as 0x80000000 elements.
    const auto last = static_cast<std::int32_t>(static_cast<std::uint32_t>(count) - 1);
    if (last < 0) {
        return;
    }
    for (std::uint32_t k = 0; k <= static_cast<std::uint32_t>(last); ++k) {
        reinterpret_cast<ThisFn>(constructor)(element, nullptr);
        element += size;
    }
}


// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Area records on the game's heap with random fields and 39 site trees of
// random contents (made by SiteTree's constructor and insert). Copy: into
// two blocks with the same random contents, by the original and by ours;
// the record bytes up to the trees must be equal (so the padding bytes the
// copy skips too) and every tree the same (in-order values and colours,
// size, its object bytes). Destructor: the records' bytes afterwards.
// Lists: two CEvaLand objects whose list of a player is built the same
// way; insert (at a random place) and RemoveArea (a present or missing
// area id) by each side; compared: the lists' area ids and records' first
// fields in order, the sizes, areas_changed and the results (as list
// positions). The CEvaLand: Acquire on a test world (map sizes 0..96) by
// each side, compared: the reference count, the object's lists (empty,
// allocator byte = the list address's high byte), grid fields and cells
// and g_aiLandEvaluation; then released. In-process the tree functions are
// ours on both sides; the emulator compares the original code.

namespace {

constexpr std::uintptr_t kSiteTreeConstruct = 0x005590a0;
constexpr std::uintptr_t kSiteTreeInsert = 0x00559270;
constexpr std::uintptr_t kRelease = 0x00553dc0;  // AI_CEvaLand_Release

void ConstructSiteTree(SiteTree* tree) {
    const std::uint8_t bytes[2] = {0, 0};
    using Fn = SiteTree*(__fastcall*)(SiteTree*, void*, const std::uint8_t*, const std::uint8_t*);
    reinterpret_cast<Fn>(kSiteTreeConstruct)(tree, nullptr, &bytes[0], &bytes[1]);
}

void InsertSite(SiteTree* tree, const SiteValue& value) {
    SiteTreeInsertResult result;
    using Fn = SiteTreeInsertResult*(__fastcall*)(SiteTree*, void*, SiteTreeInsertResult*, const SiteValue*);
    reinterpret_cast<Fn>(kSiteTreeInsert)(tree, nullptr, &result, &value);
}

// A record with random fields and trees (sometimes empty ones).
void MakeArea(test::Rng& rng, AiArea* area) {
    rng.Fill(area, offsetof(AiArea, site_trees));
    for (SiteTree& tree : area->site_trees) {
        ConstructSiteTree(&tree);
        const std::uint32_t count = rng.Chance(1, 3) ? 0 : rng.Below(12);
        for (std::uint32_t k = 0; k < count; ++k) {
            SiteValue value;
            rng.Fill(&value, sizeof(value));
            value.key = static_cast<std::uint8_t>(rng.Below(6));
            InsertSite(&tree, value);
        }
    }
}

void DestroyArea(AiArea* area) {
    AI_CEvaLand_Area_Destruct(area, nullptr);
}

// In order: each node's value, colour; then the tree's size and bytes.
std::vector<std::uint8_t> TreeContents(const SiteTree& tree) {
    std::vector<std::uint8_t> out(reinterpret_cast<const std::uint8_t*>(&tree),
                                  reinterpret_cast<const std::uint8_t*>(&tree) + sizeof(SiteTree));
    std::memset(out.data() + offsetof(SiteTree, head), 0, sizeof(tree.head));
    // (not written by the copy: uninitialized in a new heap block)
    std::memset(out.data() + offsetof(SiteTree, unk02), 0, sizeof(tree.unk02));
    std::memset(out.data() + offsetof(SiteTree, unk09), 0, sizeof(tree.unk09));
    const SiteTreeNode* nil = g_aiSiteTreeNil;
    std::vector<const SiteTreeNode*> stack;
    const SiteTreeNode* node = tree.head->parent;
    while (node != nil || !stack.empty()) {
        while (node != nil) {
            stack.push_back(node);
            node = node->left;
        }
        node = stack.back();
        stack.pop_back();
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&node->value);
        out.insert(out.end(), bytes, bytes + sizeof(SiteValue));
        out.push_back(static_cast<std::uint8_t>(node->color));
        node = node->right;
    }
    return out;
}

// The record bytes up to the trees without the padding the copy skips.
std::vector<std::uint8_t> CopiedFields(const AiArea& area) {
    std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(&area),
                                    reinterpret_cast<const std::uint8_t*>(&area) + offsetof(AiArea, site_trees));
    for (std::size_t pad : {offsetof(AiArea, unk002), offsetof(AiArea, unk002) + 1, offsetof(AiArea, unk015),
                            offsetof(AiArea, unk015) + 1, offsetof(AiArea, unk015) + 2, offsetof(AiArea, unk3e7)}) {
        bytes[pad] = 0;
    }
    return bytes;
}

bool SameArea(test::Context& ctx, const AiArea& a, const AiArea& b, bool padding = true) {
    bool same = padding ? S3RE_EXPECT_MEM_EQ(&a, &b, offsetof(AiArea, site_trees))
                        : S3RE_EXPECT(CopiedFields(a) == CopiedFields(b));
    for (std::size_t k = 0; k < std::size(a.site_trees) && same; ++k) {
        same &= S3RE_EXPECT(TreeContents(a.site_trees[k]) == TreeContents(b.site_trees[k]));
    }
    return same;
}

AiAreaNode* NewListHead() {
    auto* head = static_cast<AiAreaNode*>(msvc6::GameOperatorNew(sizeof(AiAreaNode)));
    head->next = head->prev = head;
    return head;
}

// The list's records' area ids and first fields, in order, and its size.
std::vector<std::uint32_t> ListContents(const AiList& list) {
    std::vector<std::uint32_t> out{list.count};
    const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
    for (const AiAreaNode* node = head->next; node != head; node = node->next) {
        out.push_back(node->value.area);
        out.push_back(node->value.basic_economy);
        out.push_back(node->value.site_trees[7].size);
        out.push_back(node->prev->next == node ? 1 : 0);
    }
    return out;
}

int PositionOf(const AiList& list, const void* node) {
    const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
    int k = 0;
    for (const AiAreaNode* n = head->next; n != head; n = n->next, ++k) {
        if (n == node) {
            return k;
        }
    }
    return node == head ? -1 : -2;
}

void FreeList(AiList& list) {
    auto* head = reinterpret_cast<AiAreaNode*>(list.head);
    for (AiAreaNode* node = head->next; node != head;) {
        AiAreaNode* next = node->next;
        DestroyArea(&node->value);
        msvc6::GameOperatorDelete(node);
        node = next;
    }
    msvc6::GameOperatorDelete(head);
}

struct AreaFns {
    AreaCopyFn copy;
    AreaDestructFn destruct;
    AreaInsertFn insert;
    RemoveAreaFn remove;
};

void CompareAreas(test::Context& ctx, const AreaFns& ours, int cases) {
    const AreaFns original{Original_AI_CEvaLand_Area_Copy, Original_AI_CEvaLand_Area_Destruct,
                           Original_AI_AreaList_insert, Original_AI_CEvaLand_RemoveArea};
    const std::uint32_t refs = g_aiSiteTreeNilRefs;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        bool same = true;
        const int what = static_cast<int>(rng.Below(3));
        if (what == 0) {
            // Copy and destroy.
            auto* source = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
            auto* a = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
            auto* b = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
            MakeArea(rng, source);
            rng.Fill(a, sizeof(AiArea));
            std::memcpy(b, a, sizeof(AiArea));
            same &= S3RE_EXPECT_EQ(original.copy(a, nullptr, source), a);
            same &= S3RE_EXPECT_EQ(ours.copy(b, nullptr, source), b);
            same &= SameArea(ctx, *a, *b);
            for (std::size_t k = 0; k < std::size(a->site_trees); ++k) {  // (the padding isn't copied)
                const std::vector<std::uint8_t> copy = TreeContents(a->site_trees[k]);
                const std::vector<std::uint8_t> from = TreeContents(source->site_trees[k]);
                same &= S3RE_EXPECT(a->site_trees[k].size == source->site_trees[k].size &&
                                    std::equal(copy.begin() + sizeof(SiteTree), copy.end(),
                                               from.begin() + sizeof(SiteTree), from.end()));
            }
            original.destruct(a, nullptr);
            ours.destruct(b, nullptr);
            same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(AiArea));
            DestroyArea(source);
            msvc6::GameOperatorDelete(source);
            msvc6::GameOperatorDelete(a);
            msvc6::GameOperatorDelete(b);
        } else {
            // A player's list in two objects, built alike.
            auto* landA = static_cast<AiLandEvaluation*>(msvc6::GameOperatorNew(sizeof(AiLandEvaluation)));
            auto* landB = static_cast<AiLandEvaluation*>(msvc6::GameOperatorNew(sizeof(AiLandEvaluation)));
            rng.Fill(landA, sizeof(AiLandEvaluation));
            std::memcpy(landB, landA, sizeof(AiLandEvaluation));
            const std::uint32_t player = rng.Below(20);
            AiList& listA = landA->lists[player];
            AiList& listB = landB->lists[player];
            listA.head = reinterpret_cast<AiListNode*>(NewListHead());
            listB.head = reinterpret_cast<AiListNode*>(NewListHead());
            listA.count = listB.count = 0;
            const std::uint32_t count = rng.Below(6);
            auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
            for (std::uint32_t k = 0; k < count; ++k) {
                MakeArea(rng, record);
                record->area = static_cast<std::uint16_t>(rng.Below(5));
                AiAreaNode* out;
                Original_AI_AreaList_insert(&listA, nullptr, &out, reinterpret_cast<AiAreaNode*>(listA.head), record);
                Original_AI_AreaList_insert(&listB, nullptr, &out, reinterpret_cast<AiAreaNode*>(listB.head), record);
                DestroyArea(record);
            }
            if (what == 1) {
                MakeArea(rng, record);
                const int position = static_cast<int>(rng.Below(count + 1));
                auto at = [&](AiList& list) {
                    auto* node = reinterpret_cast<AiAreaNode*>(list.head)->next;
                    for (int k = 0; k < position; ++k) {
                        node = node->next;
                    }
                    return node;
                };
                AiAreaNode* outA = nullptr;
                AiAreaNode* outB = nullptr;
                same &= S3RE_EXPECT_EQ(original.insert(&listA, nullptr, &outA, at(listA), record), &outA);
                same &= S3RE_EXPECT_EQ(ours.insert(&listB, nullptr, &outB, at(listB), record), &outB);
                same &= S3RE_EXPECT_EQ(PositionOf(listA, outA), PositionOf(listB, outB));
                same &= SameArea(ctx, outA->value, outB->value, /*padding=*/false);  // (new heap blocks)
                DestroyArea(record);
            } else {
                const auto area = static_cast<std::uint32_t>(rng.Below(6) | (rng.Chance(1, 4) ? rng.Next32() << 16 : 0));
                const std::uint32_t who = player | (rng.Chance(1, 4) ? rng.Next32() << 16 : 0);
                const std::uint32_t ra = original.remove(landA, nullptr, area, who);
                const std::uint32_t rb = ours.remove(landB, nullptr, area, who);
                if (ra == reinterpret_cast<std::uintptr_t>(listA.head)) {
                    same &= S3RE_EXPECT_EQ(rb, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(listB.head)));
                } else {
                    same &= S3RE_EXPECT_EQ(ra, rb);
                }
                same &= S3RE_EXPECT_MEM_EQ(landA->areas_changed, landB->areas_changed, sizeof(landA->areas_changed));
            }
            msvc6::GameOperatorDelete(record);
            same &= S3RE_EXPECT(ListContents(listA) == ListContents(listB));
            FreeList(listA);
            FreeList(listB);
            msvc6::GameOperatorDelete(landA);
            msvc6::GameOperatorDelete(landB);
        }
        same &= S3RE_EXPECT_EQ(g_aiSiteTreeNilRefs.get(), refs);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, operation %d", i, what);
        }
    }
}

AiArea* __fastcall WrongAreaCopy(AiArea* self, void* edx, const AiArea* other) {
    // Wrong: copies the padding byte +0x3e7 too.
    AI_CEvaLand_Area_Copy(self, edx, other);
    self->unk3e7 = other->unk3e7;
    return self;
}

std::uint32_t __fastcall WrongRemoveArea(AiLandEvaluation* self, void* edx, std::uint32_t area, std::uint32_t player) {
    // Wrong: doesn't mark the list as changed.
    const std::uint32_t changed = self->areas_changed[player & 0xffff];
    const std::uint32_t result = AI_CEvaLand_RemoveArea(self, edx, area, player);
    self->areas_changed[player & 0xffff] = changed;
    return result;
}

// ---- the CEvaLand

struct SavedEvaLandGlobals {
    std::uint32_t refs = g_aiEvaLandRefCount;
    AiLandEvaluation* instance = g_aiEvaLand;
    std::uint32_t land = g_aiLandEvaluation;
    ComputerPlayer* player = g_aiPlayer;
    CGameData* world = g_aiWorld;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedEvaLandGlobals() {
        g_aiEvaLandRefCount = refs;
        g_aiEvaLand = instance;
        g_aiLandEvaluation = land;
        g_aiPlayer = player;
        g_aiWorld = world;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

struct EvaLandOutcome {
    std::uint32_t refs;
    bool same_instance;
    bool lists_ok;
    bool is_current;
    std::vector<std::uint8_t> fields;  // +0xf0..+0x158
    std::vector<std::uint8_t> cells;
};

EvaLandOutcome RunAcquire(AcquireFn acquire, std::uint32_t refs, AiLandEvaluation* existing) {
    g_aiEvaLandRefCount = refs;
    g_aiEvaLand = existing;
    g_aiLandEvaluation = 0x1234;
    AiLandEvaluation* land = acquire();
    EvaLandOutcome out{};
    out.refs = g_aiEvaLandRefCount;
    out.same_instance = land == g_aiEvaLand.get();
    out.is_current = g_aiLandEvaluation.get() == reinterpret_cast<std::uintptr_t>(land);
    if (land != existing) {
        out.lists_ok = true;
        for (const AiList& list : land->lists) {
            const auto* head = list.head;
            out.lists_ok &= list.count == 0 && head->next == reinterpret_cast<std::uintptr_t>(head) &&
                            head->prev == reinterpret_cast<std::uintptr_t>(head) &&
                            (list.unk0 & 0xff) == (reinterpret_cast<std::uintptr_t>(&list) >> 24);
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(land);
        out.fields.assign(bytes + offsetof(AiLandEvaluation, areas_changed),
                          bytes + offsetof(AiLandEvaluation, cells));
        if (land->cells != nullptr) {
            const auto* cells = reinterpret_cast<const std::uint8_t*>(land->cells);
            out.cells.assign(cells, cells + land->cell_count * sizeof(AiGridCell));
        }
        g_aiEvaLandRefCount = 1;
        reinterpret_cast<std::uint32_t(__cdecl*)()>(kRelease)();
    }
    return out;
}

void CompareEvaLandConstruction(test::Context& ctx, AcquireFn acquire, int cases) {
    SavedEvaLandGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<std::uint8_t> player(sizeof(ComputerPlayer));
    reinterpret_cast<ComputerPlayer*>(player.data())->world =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    g_aiPlayer = reinterpret_cast<ComputerPlayer*>(player.data());
    g_aiWorld = world;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t size = rng.Chance(1, 10) ? rng.Below(8) : 8 + rng.Below(88);
        world->map_size = size;
        g_aiMapSizeX = static_cast<std::uint16_t>(size);
        g_aiMapSizeY = static_cast<std::uint16_t>(size);
        for (std::uint32_t x = 0; x < size + 8; ++x) {
            for (std::uint32_t y = 0; y < size + 8; ++y) {
                layout::Tile& tile = world->tiles[x][y];
                tile.terrain = static_cast<std::uint8_t>(rng.Below(256));
                tile.object = static_cast<std::uint8_t>(rng.Below(256));
                tile.height = static_cast<std::uint8_t>(rng.Below(20));
                tile.resources = static_cast<std::uint8_t>(rng.Below(256));
                tile.owner = static_cast<std::uint8_t>(rng.Below(4) == 3 ? 0xff : rng.Below(3));
            }
        }
        const std::uint32_t refs = rng.Below(3);
        AiLandEvaluation* existing = nullptr;
        std::vector<std::uint8_t> dummy(sizeof(AiLandEvaluation));
        if (rng.Chance(1, 5)) {
            existing = reinterpret_cast<AiLandEvaluation*>(dummy.data());
        }
        const EvaLandOutcome a = RunAcquire(Original_AI_CEvaLand_Acquire, refs, existing);
        const EvaLandOutcome b = RunAcquire(acquire, refs, existing);
        bool same = S3RE_EXPECT_EQ(a.refs, b.refs);
        same &= S3RE_EXPECT_EQ(a.same_instance, b.same_instance);
        same &= S3RE_EXPECT_EQ(a.is_current, b.is_current);
        same &= S3RE_EXPECT_EQ(a.lists_ok, b.lists_ok);
        same &= S3RE_EXPECT(a.fields == b.fields);
        same &= S3RE_EXPECT(a.cells == b.cells);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, map size %u", i, size);
        }
    }
}

void __fastcall CountingConstructor(void* self, void* /*edx*/) {
    auto* bytes = static_cast<std::uint8_t*>(self);
    bytes[0] = static_cast<std::uint8_t>(bytes[0] + 1);
    bytes[1] = 0x5a;
}

void CompareVectorConstructor(test::Context& ctx, VectorConstructorFn ours) {
    for (std::int32_t count = -3; count <= 12; ++count) {
        for (std::uint32_t size : {1u, 3u, 8u}) {
            std::uint8_t a[128] = {};
            std::uint8_t b[128] = {};
            Original_VectorConstructorIterator(a + 4, size, count,
                                               reinterpret_cast<std::uintptr_t>(&CountingConstructor));
            ours(b + 4, size, count, reinterpret_cast<std::uintptr_t>(&CountingConstructor));
            if (!S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
                ctx.Fail(__FILE__, __LINE__, "  count %d, size %u", count, size);
                return;
            }
        }
    }
}

void __stdcall WrongVectorConstructor(void* array, std::uint32_t size, std::int32_t count, std::uintptr_t ctor) {
    // Wrong: one element too few.
    VectorConstructorIterator(array, size, count - 1, ctor);
}

AiLandEvaluation* __cdecl WrongAcquire() {
    // Wrong: the grid is evaluated for a map one cell larger.
    CGameData* world = reinterpret_cast<CGameData*>(g_aiPlayer.get()->world);
    world->map_size += 8;
    AiLandEvaluation* land = AI_CEvaLand_Acquire();
    world->map_size -= 8;
    return land;
}

AreaFns OurAreaFns() {
    return {&AI_CEvaLand_Area_Copy, &AI_CEvaLand_Area_Destruct, &AI_AreaList_insert, &AI_CEvaLand_RemoveArea};
}

}  // namespace

S3RE_TEST(AiAreaRecords_MatchOriginal) {
    CompareAreas(ctx, OurAreaFns(), 600);
    CompareEvaLandConstruction(ctx, &AI_CEvaLand_Acquire, 60);
    CompareVectorConstructor(ctx, &VectorConstructorIterator);
}

S3RE_TEST(AiAreaRecords_ComparisonDetectsWrongImplementation) {
    {
        AreaFns wrong = OurAreaFns();
        wrong.copy = &WrongAreaCopy;
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareAreas(quiet, wrong, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        AreaFns wrong = OurAreaFns();
        wrong.remove = &WrongRemoveArea;
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareAreas(quiet, wrong, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEvaLandConstruction(quiet, &WrongAcquire, 60);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareVectorConstructor(quiet, &WrongVectorConstructor);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
