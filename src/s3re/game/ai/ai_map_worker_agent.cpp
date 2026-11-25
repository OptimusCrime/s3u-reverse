// T_MAP_WORKER_AGENT (kind 6; Rainer\CompMapWorkerAgent.cpp): the agent
// that turns the building types the computer player has taken for building
// into build orders at concrete sites (computer players A6, session 56;
// re/computer_players.md "The map worker agent").
//
//   0x0057cde0 T_MAP_WORKER_AGENT::Run(), slot 8: every 14th run (byte
//              run_count, reset to 0): for each building type 0..56 whose
//              taken candidate g_aiPlayerData->best[57 + type] has a score,
//              MakeBuildOrders(type, &its x, &its y), then that candidate
//              is cleared (score 0, x = y = 0xffff; g_aiPlayerData re-read
//              for each store). EAX is not used (CComputerPlayer::RunAgents
//              0x551590 ignores slot 8's).
//   0x0057ce90 T_MAP_WORKER_AGENT::MakeBuildOrders(type, const u16* x,
//              const u16* y), ret 0xc: a switch over the type (jump table
//              0x5800cc; types 15 small tower, 49 donkey farm and those
//              outside 1..55 do nothing). For most types:
//              1. AI::IsAllowedBeforeBasicEconomy 0x5721a0 (no: return).
//              2. The area node's site tree of the type's kind of site
//                 (AiArea::site_trees, a fixed index per type; types share
//                 trees, e.g. the temples). Empty: CEvaLand::
//                 RefillAreaSiteList 0x5579d0 (player word, area, type) on
//                 g_aiLandEvaluation and return. Otherwise its last site
//                 (the highest key: _Dec of the head 0x55a260) is taken out
//                 (erase 0x5599b0); from type 35 on through the library
//                 wrappers (empty 0x580230, end 0x580220, it-- 0x580270,
//                 erase 0x580240), with the same effect.
//              3. One or two checks of the site, each returning when it
//                 fails: no order of the type in the area
//                 (AI::HasSiteNoteInArea 0x571d00), none within a distance
//                 (AI::HasSiteNoteNear 0x571c40), no own building of a type
//                 within a distance (AI::HasBuildingNear 0x571d90; the mill
//                 checks waterworks), no forester's bit in the land grid
//                 around (CEvaLand::HasBuildingTypeNear 0x558f00, given the
//                 tile position as a cell position).
//              4. A T_DO_BUILD_HOUSE_NOTE (type, x, y, a value per type) is
//                 added as a root note (CComputerPlayer::AddNote 0x551270,
//                 requester 0). Out of memory: Win32::ThrowOutOfMemory(
//                 "notePtr", CompMapWorkerAgent.cpp, the line per type).
//              Special types: the large tower (16) and the castle (18) are
//              built at (*x, *y) with OutputDebugStringA logs, the castle
//              only where another own castle is within 60 (sic); the small
//              residence (26) goes to the next garrisoned military building
//              (AI::NextGarrisonedMilitaryBuilding 0x571ee0 from
//              residence_cursor, AI::BuildingIfUsed 0x58e1c0); the medium
//              and large residences (27, 28) take the site from the tree of
//              the farm's type pointer (AiArea::site_tree_of_type[24]) after
//              checking the area, and do nothing when it is empty; the
//              marketplace's note runs BLACK_BOARD_NOTE's constructor
//              0x5801b0 first; type 6 (lookout tower) orders a coal mine
//              (type 7) but refills the list for type 6; type 55 asks
//              IsAllowedBeforeBasicEconomy about type 45.
//
// The per-type data (tree, checks, value, line) is the table kRules below;
// the original repeats the code for every case. Arguments the original
// pushes with leftover upper halves are passed as their low words here (all
// callees read words; checked, CEvaLand::RefillAreaSiteList included).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kMakeBuildOrders = 0x0057ce90;
constexpr std::uintptr_t kIsAllowedBeforeBasicEconomy = 0x005721a0;  // fastcall (type)
constexpr std::uintptr_t kRefillAreaSiteList = 0x005579d0;  // CEvaLand (thiscall player, area, type); not ours yet
constexpr std::uintptr_t kTreeDec = 0x0055a260;             // AI::SiteTree::iterator::_Dec (thiscall)
constexpr std::uintptr_t kTreeErase = 0x005599b0;           // AI::SiteTree::erase (thiscall out, where)
constexpr std::uintptr_t kTreePostDecrement = 0x00580270;   // AI::SiteTree::iterator::PostDecrement (thiscall out, 0)
constexpr std::uintptr_t kTreeEraseIterator = 0x00580240;   // AI::SiteTree::erase_580240 (thiscall out, where)
constexpr std::uintptr_t kHasSiteNoteInArea = 0x00571d00;   // fastcall (const u16* type, const u16* area)
constexpr std::uintptr_t kHasSiteNoteNear = 0x00571c40;     // fastcall (type, x; y, distance)
constexpr std::uintptr_t kHasBuildingNear = 0x00571d90;     // fastcall (type, x; y, distance, player)
constexpr std::uintptr_t kHasBuildingTypeNear = 0x00558f00;  // CEvaLand (thiscall cell x, cell y, type, radius)
constexpr std::uintptr_t kNextGarrisonedMilitaryBuilding = 0x00571ee0;  // fastcall (start, player)
constexpr std::uintptr_t kBuildingIfUsed = 0x0058e1c0;      // fastcall (id)
constexpr std::uintptr_t kBlackBoardNoteConstruct = 0x005801b0;  // BLACK_BOARD_NOTE::BLACK_BOARD_NOTE
constexpr std::uintptr_t kDoBuildHouseNoteInit = 0x00565470;     // T_DO_BUILD_HOUSE_NOTE::Init
constexpr std::uintptr_t kDoBuildHouseNoteConstruct = 0x005801c0;  // T_DO_BUILD_HOUSE_NOTE::T_DO_BUILD_HOUSE_NOTE
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;    // Win32::ThrowOutOfMemory (never returns)
constexpr std::uintptr_t kAddNote = 0x00551270;             // CComputerPlayer::AddNote (thiscall)

constexpr std::uint8_t kRunsPerWork = 14;
constexpr std::uint16_t kNoPlace = 0xffff;
constexpr std::uint32_t kNoteBytes = 0x28;
constexpr std::uint32_t kRootNote = kNoNote;
constexpr std::uint32_t kNoRequester = 0;
// The residences without a site list of their own use the farm's tree.
constexpr std::size_t kResidenceSiteTreeType = Value(BuildingType::Farm);

enum class Check : std::uint8_t {
    None,
    NoOrderInArea,    // AI::HasSiteNoteInArea(type, area) must say 0
    NoOrderNear,      // AI::HasSiteNoteNear(type, x, y, distance) must say 0
    NoBuildingNear,   // AI::HasBuildingNear(type, x, y, distance, player) must say 0
    NoTypeInCells,    // CEvaLand::HasBuildingTypeNear(x, y, type, distance) must say 0
};

struct SiteCheck {
    Check kind;
    std::uint8_t type;      // the building type asked about
    std::uint8_t distance;  // tiles (cells for NoTypeInCells)
};

// How the original makes the note.
enum class NoteMaking : std::uint8_t {
    Init,         // vtable 0x65a584, T_DO_BUILD_HOUSE_NOTE::Init
    Constructor,  // T_DO_BUILD_HOUSE_NOTE::T_DO_BUILD_HOUSE_NOTE (types 35..55)
    BaseAndInit,  // BLACK_BOARD_NOTE's constructor, then as Init (the marketplace)
};

enum class Kind : std::uint8_t {
    None,          // nothing (15, 49 and unknown types)
    FromTree,      // a site from the type's site tree (step 2 above)
    AtPosition,    // the large tower and castle: at (*x, *y)
    AtMilitary,    // the small residence: at the next garrisoned military building
    FromFarmTree,  // the medium and large residence
};

struct BuildOrderRule {
    Kind kind;
    std::uint8_t note_type;     // the type ordered (6 orders 7)
    std::uint8_t allowed_type;  // the type asked about before the basic economy (55 asks about 45)
    std::uint8_t tree;          // AiArea::site_trees index (FromTree)
    bool wrappers;              // the tree is used through the library wrappers (types 35..55)
    NoteMaking making;
    SiteCheck checks[2];
    std::uint8_t value;         // the note's value
    std::uint16_t line;         // the out-of-memory check's source line
};

constexpr SiteCheck kNoCheck{Check::None, 0, 0};
constexpr SiteCheck InArea(std::uint8_t type) { return {Check::NoOrderInArea, type, 0}; }
constexpr SiteCheck NoteNear(std::uint8_t type, std::uint8_t distance) { return {Check::NoOrderNear, type, distance}; }
constexpr SiteCheck BuildingNear(std::uint8_t type, std::uint8_t distance) {
    return {Check::NoBuildingNear, type, distance};
}
constexpr SiteCheck TypeInCells(std::uint8_t type, std::uint8_t cells) { return {Check::NoTypeInCells, type, cells}; }

constexpr BuildOrderRule Tree(std::uint8_t type, std::uint8_t tree, SiteCheck first, SiteCheck second,
                              std::uint8_t value, std::uint16_t line) {
    return {Kind::FromTree, type, type, tree, false, NoteMaking::Init, {first, second}, value, line};
}
constexpr BuildOrderRule Wrapped(std::uint8_t type, std::uint8_t tree, SiteCheck first, SiteCheck second,
                                 std::uint8_t value, std::uint16_t line) {
    return {Kind::FromTree, type, type, tree, true, NoteMaking::Constructor, {first, second}, value, line};
}
constexpr BuildOrderRule kNothing{Kind::None, 0, 0, 0, false, NoteMaking::Init, {kNoCheck, kNoCheck}, 0, 0};

// Per building type 0..55 (the switch at 0x57ced5; the case addresses in
// the comments). Site tree indexes: 0 trees (woodcutter), 1 stones, 2 iron,
// 3 coal, 4 gold, 5 sulfur, 6 gems, 7 fish, 8 water, ...; the expand agent
// (ai_expand_empire_agent.cpp) reads the same trees.
constexpr BuildOrderRule kRules[] = {
    kNothing,                                                      //  0
    Tree(1, 23, InArea(1), kNoCheck, 16, 251),                     //  1 storehouse 0x57cee6
    Tree(2, 0, NoteNear(2, 32), kNoCheck, 16, 291),                //  2 woodcutter 0x57cfb8
    Tree(3, 1, NoteNear(3, 32), BuildingNear(3, 16), 16, 340),     //  3 stonecutter 0x57d088
    Tree(4, 13, InArea(4), kNoCheck, 40, 385),                     //  4 sawmill 0x57d176
    Tree(5, 14, TypeInCells(5, 3), InArea(5), 32, 428),            //  5 forester 0x57d252
    // 6 lookout tower: the coal mine's case (0x57d365; the refill gets type 6)
    {Kind::FromTree, 7, 7, 3, false, NoteMaking::Init, {InArea(7), BuildingNear(7, 8)}, 6, 508},
    Tree(7, 3, InArea(7), BuildingNear(7, 8), 6, 508),             //  7 coal mine 0x57d365
    Tree(8, 4, InArea(8), BuildingNear(8, 8), 6, 547),             //  8 gold mine 0x57d45f
    Tree(9, 2, InArea(9), BuildingNear(9, 8), 6, 586),             //  9 iron mine 0x57d552
    Tree(10, 12, InArea(10), kNoCheck, 32, 625),                   // 10 gold smelter 0x57d64b
    Tree(11, 9, InArea(11), kNoCheck, 32, 664),                    // 11 iron smelter 0x57d721
    Tree(12, 10, InArea(12), kNoCheck, 32, 704),                   // 12 toolsmith 0x57d7fd
    Tree(13, 11, InArea(13), kNoCheck, 48, 744),                   // 13 weaponsmith 0x57d8d3
    Tree(14, 24, InArea(14), kNoCheck, 32, 783),                   // 14 vineyard 0x57d9af
    kNothing,                                                      // 15 small tower
    // 16 large tower 0x57da85
    {Kind::AtPosition, 16, 16, 0, false, NoteMaking::Init, {InArea(16), kNoCheck}, 24, 821},
    Tree(17, 16, InArea(17), BuildingNear(30, 24), 32, 878),       // 17 mill 0x57dc19 (near waterworks, sic)
    // 18 castle 0x57db41
    {Kind::AtPosition, 18, 18, 0, false, NoteMaking::Init, {BuildingNear(18, 60), InArea(18)}, 48, 844},
    Tree(19, 22, InArea(19), kNoCheck, 32, 918),                   // 19 barracks 0x57dd12
    Tree(20, 17, InArea(20), BuildingNear(20, 24), 32, 957),       // 20 bakery 0x57dde8
    Tree(21, 19, InArea(21), BuildingNear(21, 12), 32, 996),       // 21 slaughterhouse 0x57dee1
    Tree(22, 31, InArea(22), BuildingNear(22, 24), 32, 1037),      // 22 distillery 0x57dfd4
    Tree(23, 18, InArea(23), BuildingNear(23, 24), 32, 1076),      // 23 pig farm 0x57e0cd
    Tree(24, 15, InArea(24), kNoCheck, 32, 1115),                  // 24 farm 0x57e1c0
    Tree(25, 7, InArea(25), BuildingNear(25, 24), 32, 1155),       // 25 fishery 0x57e29c
    // 26..28 small, medium, large residence 0x57e38f, 0x57e44b, 0x57e521
    {Kind::AtMilitary, 26, 26, 0, false, NoteMaking::Init, {InArea(26), kNoCheck}, 16, 1186},
    {Kind::FromFarmTree, 27, 27, 0, false, NoteMaking::Init, {InArea(27), kNoCheck}, 18, 1219},
    {Kind::FromFarmTree, 28, 28, 0, false, NoteMaking::Init, {InArea(28), kNoCheck}, 24, 1251},
    Tree(29, 5, InArea(29), BuildingNear(29, 8), 6, 1339),         // 29 sulfur mine 0x57e5fe
    Tree(30, 8, InArea(30), BuildingNear(30, 12), 48, 1378),       // 30 waterworks 0x57e6f2
    Tree(31, 35, InArea(31), kNoCheck, 48, 1417),                  // 31 catapult workshop 0x57e7e5
    Tree(32, 25, InArea(32), kNoCheck, 48, 1456),                  // 32 shipyard 0x57e8bb
    Tree(33, 26, InArea(33), kNoCheck, 48, 1495),                  // 33 harbour 0x57e991
    // 34 marketplace 0x57ea67
    {Kind::FromTree, 34, 34, 27, false, NoteMaking::BaseAndInit, {InArea(34), kNoCheck}, 48, 1534},
    Wrapped(35, 28, InArea(35), kNoCheck, 24, 1573),               // 35 hospital 0x57eb44
    Wrapped(36, 29, InArea(36), kNoCheck, 32, 1612),               // 36 rice farm 0x57ec4f
    Wrapped(37, 6, InArea(37), BuildingNear(37, 8), 6, 1651),      // 37 gem mine 0x57ed5a
    Wrapped(38, 31, InArea(38), kNoCheck, 32, 1690),               // 38 brewery 0x57ee94
    Wrapped(39, 30, InArea(39), kNoCheck, 32, 1729),               // 39 charcoal burner 0x57ef9f
    Wrapped(40, 32, InArea(40), kNoCheck, 32, 1768),               // 40 powder maker 0x57f0aa
    Wrapped(41, 34, InArea(41), kNoCheck, 48, 1807),               // 41 pyramid 0x57f1b5
    Wrapped(42, 33, InArea(42), kNoCheck, 32, 1846),               // 42 sphinx 0x57f2c6
    Wrapped(43, 34, InArea(43), kNoCheck, 48, 1885),               // 43 large temple 0x57f3d7
    Wrapped(44, 33, InArea(44), kNoCheck, 32, 1924),               // 44 small temple 0x57f4e8
    Wrapped(45, 34, InArea(45), kNoCheck, 32, 1963),               // 45 large pagoda 0x57f5f9
    Wrapped(46, 33, InArea(46), kNoCheck, 32, 2002),               // 46 small pagoda 0x57f70a
    Wrapped(47, 35, InArea(47), kNoCheck, 32, 2041),               // 47 ballista workshop 0x57f81b
    Wrapped(48, 35, InArea(48), kNoCheck, 32, 2080),               // 48 cannon workshop 0x57f92c
    kNothing,                                                      // 49 donkey farm
    Wrapped(50, 35, InArea(50), kNoCheck, 32, 2182),               // 50 large gong 0x57fc62
    Wrapped(51, 37, InArea(51), kNoCheck, 32, 2214),               // 51 apiary 0x57fd69
    Wrapped(52, 31, InArea(52), kNoCheck, 32, 2246),               // 52 meadery 0x57fe70
    Wrapped(53, 38, InArea(53), kNoCheck, 32, 2278),               // 53 laboratory 0x57ff77
    Wrapped(54, 33, InArea(54), kNoCheck, 32, 2150),               // 54 Amazon small temple 0x57fb5b
    // 55 Amazon large temple 0x57fa4f (asks about the large pagoda, sic)
    {Kind::FromTree, 55, 45, 34, true, NoteMaking::Constructor, {InArea(55), kNoCheck}, 32, 2118},
};
constexpr std::size_t kRuleCount = sizeof(kRules) / sizeof(kRules[0]);
static_assert(kRuleCount == 56);

// A site taken out of a tree.
struct Site {
    std::uint16_t x, y;
};

AiArea& CurrentArea() { return reinterpret_cast<AiAreaNode*>(g_aiPlayerData.get())->value; }

// 0x57cf0d..0x57cf3d (and every case like it): the tree's last site,
// erased from it.
Site TakeLastSite(SiteTree* tree) {
    SiteTreeNode* it = tree->head;
    using DecFn = void(__fastcall*)(SiteTreeNode**, void*);
    reinterpret_cast<DecFn>(kTreeDec)(&it, nullptr);
    const Site site{it->value.x, it->value.y};
    SiteTreeNode* next = nullptr;
    using EraseFn = SiteTreeNode**(__fastcall*)(SiteTree*, void*, SiteTreeNode**, SiteTreeNode*);
    reinterpret_cast<EraseFn>(kTreeErase)(tree, nullptr, &next, it);
    return site;
}

// 0x57eb56..0x57ebdc (types 35..55): the same through the library's
// wrappers (it = end(); it--; *it; erase(it)).
Site TakeLastSiteWrapped(SiteTree* tree) {
    SiteTreeNode* it = tree->head;  // end() 0x580220
    SiteTreeNode* before = nullptr;
    using PostDecFn = SiteTreeNode**(__fastcall*)(SiteTreeNode**, void*, SiteTreeNode**, std::uint32_t);
    reinterpret_cast<PostDecFn>(kTreePostDecrement)(&it, nullptr, &before, 0);
    const Site site{it->value.x, it->value.y};  // *it 0x580260
    SiteTreeNode* next = nullptr;
    using EraseFn = SiteTreeNode**(__fastcall*)(SiteTree*, void*, SiteTreeNode**, SiteTreeNode*);
    reinterpret_cast<EraseFn>(kTreeEraseIterator)(tree, nullptr, &next, it);
    return site;
}

// The player word the original passes to RefillAreaSiteList: player_number
// and the byte after it (`mov di, word [player + 0x272]`).
std::uint32_t PlayerWord(const ComputerPlayer* player) {
    return static_cast<std::uint32_t>(player->player_number | player->unk273 << 8);
}

void RefillSiteList(std::uint32_t type) {
    const ComputerPlayer* player = g_aiPlayer;
    using RefillFn = void(__fastcall*)(AiLandEvaluation*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<RefillFn>(kRefillAreaSiteList)(reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get()),
                                                    nullptr, PlayerWord(player), player->area & 0xffff, type);
}

// True when the check lets the order through.
bool Passes(const SiteCheck& check, Site site) {
    switch (check.kind) {
    case Check::None: return true;
    case Check::NoOrderInArea: {
        const std::uint16_t type = check.type;
        const auto area = static_cast<std::uint16_t>(g_aiPlayer.get()->area);
        using Fn = std::uint32_t(__fastcall*)(const std::uint16_t*, const std::uint16_t*);
        return reinterpret_cast<Fn>(kHasSiteNoteInArea)(&type, &area) == 0;
    }
    case Check::NoOrderNear: {
        using Fn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
        return reinterpret_cast<Fn>(kHasSiteNoteNear)(check.type, site.x, site.y, check.distance) == 0;
    }
    case Check::NoBuildingNear: {
        using Fn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                              std::uint32_t);
        return reinterpret_cast<Fn>(kHasBuildingNear)(check.type, site.x, site.y, check.distance,
                                                      g_aiPlayerNumber) == 0;
    }
    case Check::NoTypeInCells: {
        using Fn = std::uint32_t(__fastcall*)(AiLandEvaluation*, void*, std::uint32_t, std::uint32_t,
                                              std::uint32_t, std::uint32_t);
        return reinterpret_cast<Fn>(kHasBuildingTypeNear)(
                   reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get()), nullptr, site.x, site.y,
                   check.type, check.distance) == 0;
    }
    }
    return true;
}

bool PassesAll(const BuildOrderRule& rule, Site site) {
    for (const SiteCheck& check : rule.checks) {
        if (!Passes(check, site)) {
            return false;
        }
    }
    return true;
}

void OutputDebugText(const GameText& text) { g_importOutputDebugStringA.get()(text.c_str()); }

// 0x580075..0x580092 (and the copies before it): the note, added as a root
// note. The site is read after the allocation (the large tower and castle
// read their position there).
template <typename SiteFn>
void OrderBuilding(const BuildOrderRule& rule, std::uint32_t type, SiteFn siteAfterNew) {
    void* note = msvc6::GameOperatorNew(kNoteBytes);
    if (note != nullptr) {
        const Site site = siteAfterNew();
        using InitFn = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t);
        switch (rule.making) {
        case NoteMaking::BaseAndInit:
            reinterpret_cast<void*(__fastcall*)(void*, void*)>(kBlackBoardNoteConstruct)(note, nullptr);
            [[fallthrough]];
        case NoteMaking::Init:
            *static_cast<std::uint32_t*>(note) = vtables::kDoBuildHouseNote;
            reinterpret_cast<InitFn>(kDoBuildHouseNoteInit)(note, nullptr, type, site.x, site.y, rule.value);
            break;
        case NoteMaking::Constructor: {
            using ConstructFn = void*(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                   std::uint32_t);
            note = reinterpret_cast<ConstructFn>(kDoBuildHouseNoteConstruct)(note, nullptr, type, site.x, site.y,
                                                                             rule.value);
            break;
        }
        }
    } else {
        using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
        reinterpret_cast<ThrowFn>(kThrowOutOfMemory)(texts::kNotePtr.c_str(), texts::kMapWorkerSourceFile.c_str(),
                                                     rule.line);
    }
    using AddFn = std::uint32_t(__fastcall*)(ComputerPlayer*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<AddFn>(kAddNote)(g_aiPlayer, nullptr, reinterpret_cast<std::uint32_t>(note), kRootNote,
                                      kNoRequester);
}

void OrderAt(const BuildOrderRule& rule, Site site) {
    OrderBuilding(rule, rule.note_type, [site] { return site; });
}

// The types whose site comes from their site tree (step 2 of the header).
void OrderFromTree(const BuildOrderRule& rule, std::uint32_t type) {
    SiteTree& tree = CurrentArea().site_trees[rule.tree];
    if (tree.size == 0) {
        RefillSiteList(type);
        return;
    }
    const Site site = rule.wrappers ? TakeLastSiteWrapped(&tree) : TakeLastSite(&tree);
    if (!PassesAll(rule, site)) {
        return;
    }
    OrderAt(rule, site);
}

bool IsAllowed(const BuildOrderRule& rule) {
    using AllowedFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t edx);
    return reinterpret_cast<AllowedFn>(kIsAllowedBeforeBasicEconomy)(rule.allowed_type, g_aiPlayer.get()->area) != 0;
}

// 0x57da85 / 0x57db41: the large tower and the castle, at the taken
// candidate's place.
void OrderAtPosition(const BuildOrderRule& rule, const std::uint16_t* x, const std::uint16_t* y) {
    const bool castle = rule.note_type == Value(BuildingType::Castle);
    OutputDebugText(castle ? texts::kBuildCastleQ : texts::kBuildLargeTowerQ);
    if (!IsAllowed(rule)) {
        return;
    }
    // The castle's first check is inverted: it needs an own castle near (sic).
    if (castle && Passes(rule.checks[0], Site{*x, *y})) {
        return;
    }
    if (!Passes(rule.checks[castle ? 1 : 0], Site{*x, *y})) {
        return;
    }
    OutputDebugText(castle ? texts::kBuildCastleGo : texts::kBuildLargeTowerGo);
    OrderBuilding(rule, rule.note_type, [x, y] { return Site{*x, *y}; });
}

// 0x57e38f: the small residence goes to the next garrisoned military
// building.
void OrderAtMilitary(const BuildOrderRule& rule, MapWorkerAgent* self) {
    if (!PassesAll(rule, Site{0, 0})) {
        return;
    }
    using NextFn = std::uint32_t(__fastcall*)(std::uint32_t start, std::uint32_t player);
    const std::uint32_t id =
        reinterpret_cast<NextFn>(kNextGarrisonedMilitaryBuilding)(self->residence_cursor, g_aiPlayerNumber);
    self->residence_cursor = static_cast<std::uint16_t>(id);
    using BuildingFn = const layout::Building*(__fastcall*)(std::uint32_t id);
    const layout::Building* building = reinterpret_cast<BuildingFn>(kBuildingIfUsed)(id);
    if (building == nullptr) {
        return;
    }
    OrderAt(rule, Site{building->x, building->y});
}

// 0x57e44b / 0x57e521: the medium and large residence take a site of the
// tree the farm's type points at; nothing when it is empty.
void OrderFromFarmTree(const BuildOrderRule& rule) {
    if (!PassesAll(rule, Site{0, 0})) {
        return;
    }
    SiteTree* tree = CurrentArea().site_tree_of_type[kResidenceSiteTreeType];
    if (tree->size == 0) {
        return;
    }
    OrderAt(rule, TakeLastSite(tree));
}

}  // namespace

// ---------------------------------------------------------------------------
// CEvaLand::HasBuildingTypeNear (0x00558f00; Rainer\Cevaland.cpp)
// ---------------------------------------------------------------------------
//
// thiscall (x, y, type, radius), ret 0x10: for rings 1..radius (low word;
// below 1: none) walks four sides of 2 * ring cells (directions -x, +y, +x,
// -y) and returns 1 at the first cell on the grid (signed words against the
// width and height) whose per-player bitset (AiGridCell::building_types of
// g_aiPlayerNumber) has the type's bit; else 0. Types from 57 go to
// std::bitset's _Xran 0x559550 (throws). Quirk kept: every ring starts at
// (x + 1, y - 1), so the rings beyond the first are squares shifted towards
// -x / +y rather than rings around (x, y). Its caller (the forester's build
// order) passes a tile position, not a cell position.

namespace {
constexpr std::uintptr_t kGridCell = 0x00554700;     // CEvaLand::GridCell (thiscall x, y)
constexpr std::uintptr_t kBitsetXran = 0x00559550;   // std::bitset::_Xran (throws)
constexpr std::uint32_t kSidesPerRing = 4;
// The step per side (0x558f8a..0x558fb4, jump table 0x55908c).
constexpr std::int32_t kSideStepX[kSidesPerRing] = {-1, 0, 1, 0};
constexpr std::int32_t kSideStepY[kSidesPerRing] = {0, 1, 0, -1};

bool CellHasType(AiLandEvaluation* land, std::int16_t cellX, std::int16_t cellY, std::uint32_t type) {
    if (cellX < 0 || cellX >= static_cast<std::int32_t>(land->width) || cellY < 0 ||
        cellY >= static_cast<std::int32_t>(land->height)) {
        return false;
    }
    using CellFn = AiGridCell*(__fastcall*)(AiLandEvaluation*, void*, std::int32_t, std::int32_t);
    AiGridCell* cell = reinterpret_cast<CellFn>(kGridCell)(land, nullptr, cellX, cellY);
    std::uint32_t* bits = cell->building_types[g_aiPlayerNumber];
    if (type >= kBuildingTypeCount) {
        reinterpret_cast<void(__fastcall*)(std::uint32_t*, void*)>(kBitsetXran)(bits, nullptr);
    }
    return (bits[type >> 5] & (1u << (type & 31))) != 0;
}
}  // namespace

using HasTypeNearFn = std::uint32_t(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t type, std::uint32_t radius);
std::uint32_t __fastcall AI_CEvaLand_HasBuildingTypeNear(AiLandEvaluation* self, void* edx, std::uint32_t x,
                                                         std::uint32_t y, std::uint32_t type, std::uint32_t radius);
S3RE_HOOK(0x00558f00, AI_CEvaLand_HasBuildingTypeNear, HasTypeNearFn, Original_AI_CEvaLand_HasBuildingTypeNear,
          AI_CEvaLand_HasBuildingTypeNear);

// Purpose: Whether the computer player has a building of a type in the
// land-grid cells around a cell (the map worker asks it before ordering a
// forester).
std::uint32_t __fastcall AI_CEvaLand_HasBuildingTypeNear(AiLandEvaluation* self, void* /*edx*/, std::uint32_t x,
                                                         std::uint32_t y, std::uint32_t type, std::uint32_t radius) {
    const std::int32_t rings = static_cast<std::int32_t>(radius & 0xffff);
    if (rings < 1) {
        return 0;
    }
    const std::uint32_t buildingType = type & 0xffff;
    // The ring counter is a dword compared as a signed word (the original
    // would loop forever from a radius of 32768 on; its caller passes 3).
    for (std::uint32_t ring = 1;; ++ring) {
        const std::int32_t sideLength = 2 * static_cast<std::int16_t>(ring);
        std::uint32_t cellX = x + 1;  // (every ring starts here, see above)
        std::uint32_t cellY = y - 1;
        for (std::uint32_t side = 0; side < kSidesPerRing; ++side) {
            for (std::int32_t step = 0; step < sideLength; step = static_cast<std::int16_t>(step + 1)) {
                if (CellHasType(self, static_cast<std::int16_t>(cellX), static_cast<std::int16_t>(cellY),
                                buildingType)) {
                    return 1;
                }
                cellX += static_cast<std::uint32_t>(kSideStepX[side]);
                cellY += static_cast<std::uint32_t>(kSideStepY[side]);
            }
        }
        if (static_cast<std::int16_t>(ring + 1) > rings) {
            return 0;
        }
    }
}

// The library wrappers' dereferences, which the original types 35..55 call
// (MSVC6 iterators; ours read the fields inline):
//   0x00580290 std::list<CEvaLand::Area>::iterator::operator*: EAX = the
//              node + 8 (its value). The callers pass &g_aiPlayerData.
//   0x00580260 AI::SiteTree::iterator::operator*: EAX = the node + 0xc.
using DerefFn = void*(__fastcall*)(void* self, void* edx);

void* __fastcall std_list_iterator_Deref_580290(void* self, void* edx);
void* __fastcall std_multimap_iterator_Deref_580260(void* self, void* edx);

S3RE_HOOK(0x00580290, std_list_iterator_Deref_580290, DerefFn, Original_std_list_iterator_Deref_580290,
          std_list_iterator_Deref_580290);
S3RE_HOOK(0x00580260, std_multimap_iterator_Deref_580260, DerefFn, Original_std_multimap_iterator_Deref_580260,
          std_multimap_iterator_Deref_580260);

// Purpose: The area record an iterator of the computer player's area list
// points at (*it).
void* __fastcall std_list_iterator_Deref_580290(void* self, void* /*edx*/) {
    return &(*static_cast<AiAreaNode**>(self))->value;
}

// Purpose: The site value a site-tree iterator points at (*it).
void* __fastcall std_multimap_iterator_Deref_580260(void* self, void* /*edx*/) {
    return &(*static_cast<SiteTreeNode**>(self))->value;
}

using MapWorkerRunFn = std::uint32_t(__fastcall*)(MapWorkerAgent* self, void* edx);
using MapWorkerOrdersFn = void(__fastcall*)(MapWorkerAgent* self, void* edx, std::uint32_t type,
                                            const std::uint16_t* x, const std::uint16_t* y);

std::uint32_t __fastcall T_MAP_WORKER_AGENT_Run(MapWorkerAgent* self, void* edx);
void __fastcall T_MAP_WORKER_AGENT_MakeBuildOrders(MapWorkerAgent* self, void* edx, std::uint32_t type,
                                                   const std::uint16_t* x, const std::uint16_t* y);

S3RE_HOOK(0x0057cde0, T_MAP_WORKER_AGENT_Run, MapWorkerRunFn, Original_T_MAP_WORKER_AGENT_Run, T_MAP_WORKER_AGENT_Run);
S3RE_HOOK(0x0057ce90, T_MAP_WORKER_AGENT_MakeBuildOrders, MapWorkerOrdersFn,
          Original_T_MAP_WORKER_AGENT_MakeBuildOrders, T_MAP_WORKER_AGENT_MakeBuildOrders);

// Purpose: Every 14th run the map worker turns each building type the
// computer player has taken for building into a build order, and clears
// the taken candidate.
std::uint32_t __fastcall T_MAP_WORKER_AGENT_Run(MapWorkerAgent* self, void* /*edx*/) {
    ++self->run_count;
    if (self->run_count < kRunsPerWork) {
        return self->run_count;  // (EAX is not used; the original's upper bytes are leftovers)
    }
    self->run_count = 0;
    AiPlayerData* data = g_aiPlayerData;
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        const AiCandidate& taken = data->best[kOrderedCandidates + type];
        if (taken.score == 0) {
            continue;
        }
        const std::uint16_t x = taken.a;
        const std::uint16_t y = taken.b;
        reinterpret_cast<MapWorkerOrdersFn>(kMakeBuildOrders)(self, nullptr, type, &x, &y);
        // (g_aiPlayerData is read again for each store, 0x57ce40..0x57ce6b)
        g_aiPlayerData.get()->best[kOrderedCandidates + type].score = 0;
        g_aiPlayerData.get()->best[kOrderedCandidates + type].a = kNoPlace;
        g_aiPlayerData.get()->best[kOrderedCandidates + type].b = kNoPlace;
        data = g_aiPlayerData;
    }
    return reinterpret_cast<std::uint32_t>(data);
}

// Purpose: The map worker's build order for one building type: takes the
// best remaining site the land evaluation listed for the type in the
// current economy area (asking for the list to be refilled when it is
// empty), checks that no order or building of the kind is already near,
// and posts a T_DO_BUILD_HOUSE_NOTE for it.
void __fastcall T_MAP_WORKER_AGENT_MakeBuildOrders(MapWorkerAgent* self, void* /*edx*/, std::uint32_t type,
                                                   const std::uint16_t* x, const std::uint16_t* y) {
    const std::uint32_t index = type & 0xffff;  // the jump table covers 1..55
    if (index == 0 || index >= kRuleCount) {
        return;
    }
    const BuildOrderRule& rule = kRules[index];
    switch (rule.kind) {
    case Kind::None: return;
    case Kind::AtPosition: OrderAtPosition(rule, x, y); return;
    default: break;
    }
    if (!IsAllowed(rule)) {
        return;
    }
    switch (rule.kind) {
    case Kind::FromTree: OrderFromTree(rule, type); break;
    case Kind::AtMilitary: OrderAtMilitary(rule, self); break;
    case Kind::FromFarmTree: OrderFromFarmTree(rule); break;
    default: break;
    }
}

}  // namespace s3re::game

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// MakeBuildOrders: the callees with their own evidence are stubbed and
// recorded (IsAllowedBeforeBasicEconomy, RefillAreaSiteList, the tree's
// _Dec / erase / it-- / erase wrappers on fake nodes, HasSiteNoteNear,
// HasBuildingNear, HasBuildingTypeNear, NextGarrisonedMilitaryBuilding,
// BuildingIfUsed, AddNote with the new note's bytes); HasSiteNoteInArea runs
// on a few notes and a small map, the note constructors run, and
// OutputDebugStringA's import goes to a recorder. The stubs' arguments are
// compared by their low words (the original pushes register leftovers in the
// upper halves) and stack addresses are left out. Run: MakeBuildOrders
// stubbed (it records the type and the two words it is given); compared the
// agent, the area node and the calls. HasBuildingTypeNear and the two
// dereferences: direct comparisons on a small grid / random nodes.

namespace s3re::game {
namespace {

constexpr std::uint16_t kWorkerMap = 64;
constexpr std::size_t kFakeNodes = 6;
constexpr std::size_t kWorkerNotes = 8;
constexpr std::size_t kWorkerNoteBytes = 0x28;

struct SavedWorkerGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    AiPlayerData* data = g_aiPlayerData;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    std::uint32_t land = g_aiLandEvaluation;
    ~SavedWorkerGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiPlayerData = data;
        g_aiPlayerNumber = number;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        g_aiLandEvaluation = land;
    }
};

std::vector<std::uint32_t>* g_debugTexts = nullptr;
void __stdcall RecordDebugText(const char* text) {
    if (g_debugTexts != nullptr) {
        g_debugTexts->push_back(reinterpret_cast<std::uint32_t>(text));
    }
}

struct WorkerNote {
    std::uint32_t self, parent, requester;
    std::uint8_t bytes[kWorkerNoteBytes];
};

struct WorkerScene {
    test::WorldBuffer worldBuffer{sizeof(CGameData)};
    std::vector<std::uint8_t> playerBytes = std::vector<std::uint8_t>(sizeof(ComputerPlayer));
    std::vector<RequestNote> notes = std::vector<RequestNote>(kWorkerNotes);
    std::vector<std::uint8_t> dataBytes = std::vector<std::uint8_t>(sizeof(AiAreaNode));
    std::vector<SiteTreeNode> nodes = std::vector<SiteTreeNode>(kFakeNodes);
    SiteTree farmTree{};
    layout::Building building{};
    AiLandEvaluation land{};

    ComputerPlayer* player() { return reinterpret_cast<ComputerPlayer*>(playerBytes.data()); }
    AiAreaNode* node() { return reinterpret_cast<AiAreaNode*>(dataBytes.data()); }

    void Randomize(test::Rng& rng) {
        CGameData* world = worldBuffer.world();
        g_aiWorld = world;
        g_aiMapSizeX = kWorkerMap;
        g_aiMapSizeY = kWorkerMap;
        g_aiPlayer = player();
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataBytes.data());
        g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
        std::memset(playerBytes.data(), 0, playerBytes.size());
        player()->player_number = g_aiPlayerNumber;
        player()->unk273 = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(256) : 0);
        const std::uint32_t area = 1 + rng.Below(3);
        player()->area = area | (rng.Chance(1, 3) ? rng.Next32() & 0xffff0000u : 0);
        rng.Fill(notes.data(), notes.size() * sizeof(RequestNote));
        for (RequestNote& note : notes) {
            note.base.type = static_cast<std::uint16_t>(rng.Chance(2, 3) ? Value(NoteType::DoBuildHouse) : rng.Below(5));
            note.building_type = static_cast<std::uint16_t>(rng.Below(57));
            note.x = static_cast<std::uint16_t>(rng.Below(kWorkerMap));
            note.y = static_cast<std::uint16_t>(rng.Below(kWorkerMap));
            world->tiles[note.x][note.y].area = static_cast<std::uint16_t>(rng.Chance(1, 2) ? area : rng.Below(4));
            if (rng.Chance(1, 2)) {
                player()->notes[rng.Below(kMaxNotes)] = &note.base;
            }
        }
        rng.Fill(dataBytes.data(), dataBytes.size());
        for (SiteTree& tree : node()->value.site_trees) {
            tree.size = rng.Chance(1, 4) ? 0 : 1 + rng.Below(5);
        }
        farmTree.size = rng.Chance(1, 4) ? 0 : 1 + rng.Below(5);
        node()->value.site_tree_of_type[kResidenceSiteTreeType] = &farmTree;
        rng.Fill(nodes.data(), nodes.size() * sizeof(SiteTreeNode));
        rng.Fill(&building, sizeof(building));
    }
};

void CompareMakeBuildOrders(test::Context& ctx, MapWorkerOrdersFn candidate, int cases) {
    SavedWorkerGlobals saved;
    WorkerScene scene;
    test::Rng* stubRng = nullptr;
    std::vector<std::uint32_t> answers;
    std::size_t answerIndex = 0;
    const auto answer = [&](std::uint32_t odds) -> std::uint32_t {
        if (answerIndex == answers.size()) {
            answers.push_back(stubRng->Chance(1, odds) ? 1 + stubRng->Below(3) : 0);
        }
        return answers[answerIndex++];
    };
    std::vector<WorkerNote>* notes = nullptr;
    const auto fakeNode = [&](std::uint32_t pick) { return &scene.nodes[pick % kFakeNodes]; };
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsAllowedBeforeBasicEconomy>(
        [&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(8) == 0 ? 1u : 0u; });
    stubs.Install<3, kRefillAreaSiteList>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0u; });
    stubs.Install<0, kTreeDec>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        *reinterpret_cast<SiteTreeNode**>(self) = fakeNode(answer(2) + answer(2));
        return 0u;
    });
    stubs.Install<2, kTreeErase>([](CGameData*, std::uint32_t, const std::uint32_t* args) { return args[0]; });
    stubs.Install<2, kTreePostDecrement>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        *reinterpret_cast<SiteTreeNode**>(args[0]) = *reinterpret_cast<SiteTreeNode**>(self);
        *reinterpret_cast<SiteTreeNode**>(self) = fakeNode(answer(2) + answer(2));
        return args[0];
    });
    stubs.Install<2, kTreeEraseIterator>([](CGameData*, std::uint32_t, const std::uint32_t* args) { return args[0]; });
    stubs.Install<2, kHasSiteNoteNear>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(4); });
    stubs.Install<3, kHasBuildingNear>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(3); });
    stubs.Install<4, kHasBuildingTypeNear>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(4); });
    stubs.Install<0, kNextGarrisonedMilitaryBuilding>(
        [&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(8) != 0 ? 0xffffu : 0x12340000u + answer(1); });
    stubs.Install<0, kBuildingIfUsed>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return answer(5) == 0 ? reinterpret_cast<std::uint32_t>(&scene.building) : 0u;
    });
    stubs.Install<3, kAddNote>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        WorkerNote note{self, args[1], args[2], {}};
        std::memcpy(note.bytes, reinterpret_cast<const void*>(args[0]), kWorkerNoteBytes);
        reinterpret_cast<RequestNote*>(note.bytes)->unk1a = 0;  // (never written: heap bytes)
        notes->push_back(note);
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(args[0]));
        return 0u;
    });
    std::vector<std::uint32_t> textsA, textsB;
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordDebugText);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));
    // Stack addresses (iterators, results) are left out; values are compared
    // by their low words.
    const auto normalize = [](std::vector<test::StubCall> calls) {
        for (test::StubCall& call : calls) {
            const bool iterator = call.callee == kTreeDec || call.callee == kTreePostDecrement;
            const bool erase = call.callee == kTreeErase || call.callee == kTreeEraseIterator;
            call.self = iterator ? 0 : call.self & 0xffff;
            for (std::uint32_t& arg : call.args) {
                arg &= 0xffff;
            }
            if (iterator || erase) {
                call.args[0] = 0;
            }
            if (call.callee == kHasBuildingNear) {
                call.args[2] &= 0xff;  // the player: `mov cl, [g_aiPlayerNumber]; push ecx`
            }
        }
        return calls;
    };
    int ordered = 0, refills = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        stubRng = &rng;
        answers.clear();
        scene.Randomize(rng);
        const std::uint32_t type = rng.Chance(1, 30) ? rng.Next32() : (1 + rng.Below(55)) | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint16_t x = static_cast<std::uint16_t>(rng.Next32());
        const std::uint16_t y = static_cast<std::uint16_t>(rng.Next32());
        MapWorkerAgent agentA{}, agentB{};
        rng.Fill(&agentA, sizeof(agentA));
        agentB = agentA;
        std::vector<WorkerNote> notesA, notesB;
        notes = &notesA;
        g_debugTexts = &textsA;
        answerIndex = 0;
        Original_T_MAP_WORKER_AGENT_MakeBuildOrders(&agentA, nullptr, type, &x, &y);
        const std::vector<test::StubCall> callsA = normalize(stubs.Take());
        notes = &notesB;
        g_debugTexts = &textsB;
        answerIndex = 0;
        candidate(&agentB, nullptr, type, &x, &y);
        const std::vector<test::StubCall> callsB = normalize(stubs.Take());
        g_debugTexts = nullptr;
        ordered += notesA.empty() ? 0 : 1;
        for (const test::StubCall& call : callsA) {
            refills += call.callee == kRefillAreaSiteList ? 1 : 0;
        }
        bool same = S3RE_EXPECT(callsA == callsB);
        same &= S3RE_EXPECT_EQ(notesA.size(), notesB.size());
        for (std::size_t k = 0; same && k < notesA.size(); ++k) {
            same &= S3RE_EXPECT_EQ(notesA[k].self, notesB[k].self);
            same &= S3RE_EXPECT_EQ(notesA[k].parent, notesB[k].parent);
            same &= S3RE_EXPECT_EQ(notesA[k].requester, notesB[k].requester);
            same &= S3RE_EXPECT_MEM_EQ(notesA[k].bytes, notesB[k].bytes, kWorkerNoteBytes);
        }
        same &= S3RE_EXPECT(textsA == textsB);
        same &= S3RE_EXPECT_MEM_EQ(&agentA, &agentB, sizeof(agentA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, type %u (%u vs %u calls)", i, type & 0xffff,
                     static_cast<unsigned>(callsA.size()), static_cast<unsigned>(callsB.size()));
            for (std::size_t k = 0; k < callsA.size() && k < callsB.size(); ++k) {
                if (!(callsA[k] == callsB[k])) {
                    ctx.Fail(__FILE__, __LINE__, "  call %u: %08x self %08x %08x %08x %08x vs self %08x %08x %08x %08x",
                             static_cast<unsigned>(k), static_cast<unsigned>(callsA[k].callee), callsA[k].self,
                             callsA[k].args[0], callsA[k].args[1], callsA[k].args[2], callsB[k].self,
                             callsB[k].args[0], callsB[k].args[1], callsB[k].args[2]);
                    break;
                }
            }
        }
        textsA.clear();
        textsB.clear();
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
    if (cases >= 1000) {
        S3RE_EXPECT(ordered > cases / 10);
        S3RE_EXPECT(refills > cases / 20);
    }
}

void __fastcall WrongMakeBuildOrders(MapWorkerAgent* self, void* edx, std::uint32_t type, const std::uint16_t* x,
                                     const std::uint16_t* y) {
    // Wrong: the mill checks for other mills instead of waterworks.
    if ((type & 0xffff) == Value(BuildingType::Mill)) {
        BuildOrderRule rule = kRules[Value(BuildingType::Mill)];
        rule.checks[1].type = Value(BuildingType::Mill);
        if (IsAllowed(rule)) {
            OrderFromTree(rule, type);
        }
        return;
    }
    T_MAP_WORKER_AGENT_MakeBuildOrders(self, edx, type, x, y);
}

void CompareMapWorkerRun(test::Context& ctx, MapWorkerRunFn candidate, int cases) {
    SavedWorkerGlobals saved;
    std::vector<std::uint8_t> dataA(sizeof(AiPlayerData)), dataB(sizeof(AiPlayerData));
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    struct OrderCall {
        std::uint32_t type, x, y;
        bool operator==(const OrderCall& other) const { return type == other.type && x == other.x && y == other.y; }
    };
    std::vector<OrderCall>* orders = nullptr;
    stubs.Install<3, kMakeBuildOrders>([&](CGameData*, std::uint32_t, const std::uint32_t* args) {
        orders->push_back({args[0], *reinterpret_cast<const std::uint16_t*>(args[1]),
                           *reinterpret_cast<const std::uint16_t*>(args[2])});
        return 0u;
    });
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(dataA.data(), dataA.size());
        for (std::size_t type = 0; type < kBuildingTypeCount; ++type) {
            if (rng.Chance(2, 3)) {
                reinterpret_cast<AiPlayerData*>(dataA.data())->best[kOrderedCandidates + type].score = 0;
            }
        }
        dataB = dataA;
        MapWorkerAgent agentA{}, agentB{};
        rng.Fill(&agentA, sizeof(agentA));
        agentA.run_count = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 12 + rng.Below(3) : rng.Below(256));
        agentB = agentA;
        std::vector<OrderCall> ordersA, ordersB;
        orders = &ordersA;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        Original_T_MAP_WORKER_AGENT_Run(&agentA, nullptr);
        orders = &ordersB;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        candidate(&agentB, nullptr);
        stubs.Take();
        bool same = S3RE_EXPECT(ordersA == ordersB);
        same &= S3RE_EXPECT_MEM_EQ(&agentA, &agentB, sizeof(agentA));
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
}

std::uint32_t __fastcall WrongMapWorkerRun(MapWorkerAgent* self, void* edx) {
    // Wrong: works every 13th run.
    if (self->run_count == kRunsPerWork - 2) {
        ++self->run_count;
    }
    return T_MAP_WORKER_AGENT_Run(self, edx);
}

void CompareHasBuildingTypeNear(test::Context& ctx, HasTypeNearFn candidate, int cases) {
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    constexpr std::uint32_t kGrid = 12;
    std::vector<AiGridCell> cells(kGrid * kGrid);
    AiLandEvaluation land{};
    int found = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(20));
        land.width = rng.Below(kGrid + 1);
        land.height = rng.Below(kGrid + 1);
        land.cells = cells.data();
        const std::uint32_t type = rng.Below(57) | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        for (AiGridCell& cell : cells) {
            std::uint32_t* bits = cell.building_types[g_aiPlayerNumber];
            bits[0] = rng.Chance(1, 50) ? rng.Next32() : 0;
            bits[1] = rng.Chance(1, 50) ? rng.Next32() : 0;
            if (rng.Chance(1, 30)) {
                bits[(type & 0xffff) >> 5] |= 1u << (type & 31);
            }
        }
        const auto coordinate = [&rng]() {
            return (static_cast<std::uint32_t>(rng.Range(-3, 14)) & 0xffff) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        };
        const std::uint32_t x = coordinate();
        const std::uint32_t y = coordinate();
        const std::uint32_t radius = rng.Below(7) | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t ra = Original_AI_CEvaLand_HasBuildingTypeNear(&land, nullptr, x, y, type, radius);
        const std::uint32_t rb = candidate(&land, nullptr, x, y, type, radius);
        found += ra != 0 ? 1 : 0;
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    if (cases >= 1000) {
        S3RE_EXPECT(found > cases / 10 && found < cases * 9 / 10);
    }
    g_aiPlayerNumber = savedNumber;
}

std::uint32_t __fastcall WrongHasBuildingTypeNear(AiLandEvaluation* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t type, std::uint32_t radius) {
    // Wrong: proper rings around (x, y) (each starting at (x + ring, y - ring)).
    const std::int32_t rings = static_cast<std::int32_t>(radius & 0xffff);
    for (std::int32_t ring = 1; ring <= rings; ++ring) {
        if (AI_CEvaLand_HasBuildingTypeNear(self, edx, x + ring - 1, y - ring + 1, type, 1) != 0) {
            return 1;
        }
    }
    return 0;
}

void CompareDereferences(test::Context& ctx, DerefFn listCandidate, DerefFn treeCandidate, int cases) {
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        std::uint32_t node = ctx.rng().Next32();
        bool same = S3RE_EXPECT_EQ(Original_std_list_iterator_Deref_580290(&node, nullptr), listCandidate(&node, nullptr));
        same &= S3RE_EXPECT_EQ(Original_std_multimap_iterator_Deref_580260(&node, nullptr), treeCandidate(&node, nullptr));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
}

void* __fastcall WrongDeref(void* self, void* edx) {
    // Wrong: the tree node's key instead of its value start.
    return static_cast<std::uint8_t*>(std_multimap_iterator_Deref_580260(self, edx)) - 1;
}

}  // namespace

S3RE_TEST(AiMapWorkerAgent_MatchOriginal) {
    CompareMakeBuildOrders(ctx, &T_MAP_WORKER_AGENT_MakeBuildOrders, 6000);
    CompareMapWorkerRun(ctx, &T_MAP_WORKER_AGENT_Run, 2000);
    CompareHasBuildingTypeNear(ctx, &AI_CEvaLand_HasBuildingTypeNear, 4000);
    CompareDereferences(ctx, &std_list_iterator_Deref_580290, &std_multimap_iterator_Deref_580260, 200);
}

S3RE_TEST(AiMapWorkerAgent_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareMakeBuildOrders(quiet, &WrongMakeBuildOrders, 6000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareMapWorkerRun(quiet, &WrongMapWorkerRun, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareHasBuildingTypeNear(quiet, &WrongHasBuildingTypeNear, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareDereferences(quiet, &std_list_iterator_Deref_580290, &WrongDeref, 200);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
