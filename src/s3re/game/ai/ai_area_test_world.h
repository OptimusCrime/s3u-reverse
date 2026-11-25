#pragma once
// Test support for the CEvaLand area tests (ai_area_site_lists.cpp,
// ai_area_events.cpp): a test map with land grid cells, area records with
// random site trees, CEvaLand objects with per-player lists.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game::ai_test {

using namespace ai;

inline constexpr std::uint32_t kTestMap = 72;
inline constexpr std::uint32_t kGridSide = kTestMap / 8;

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiLandEvaluation = land;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

inline void SetUpMap(test::Rng& rng, CGameData* world, std::vector<AiGridCell>& cells) {
    world->map_size = kTestMap;
    const std::uint32_t patch = 4 + rng.Below(24);
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            const std::uint32_t k = (x / patch) * 7 + (y / patch) * 3;
            tile.owner = static_cast<std::uint8_t>(rng.Chance(1, 20) ? rng.Below(4) : k % 4 == 3 ? 0xff : k % 3);
            tile.area = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Below(4) : k % 4);
            const std::uint32_t roll = rng.Below(8);
            tile.terrain = static_cast<std::uint8_t>(roll < 3 ? 0x10 | rng.Below(16) : roll == 3 ? 0x15
                                                     : roll == 4 ? 0x60 | rng.Below(16) : rng.Below(256));
            tile.object = static_cast<std::uint8_t>(rng.Below(256));
            tile.resources = static_cast<std::uint8_t>(rng.Below(256));
            tile.height = static_cast<std::uint8_t>(rng.Below(rng.Chance(1, 2) ? 8 : 30));
            tile.flags = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(256) : 0);
        }
    }
    for (AiGridCell& cell : cells) {
        auto* bytes = reinterpret_cast<std::uint8_t*>(&cell);
        for (std::size_t k = 0; k < offsetof(AiGridCell, building_types); ++k) {
            bytes[k] = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(12) : rng.Next32());
        }
        for (auto& bits : cell.building_types) {
            const std::uint32_t density = rng.Below(4);
            for (std::uint32_t& word : bits) {
                word = density == 0 ? 0 : rng.Next32() & (density == 1 ? rng.Next32() & rng.Next32() : ~0u);
            }
        }
    }
}

inline std::vector<std::uint8_t> TreeValues(const SiteTree& tree) {
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(tree.size), static_cast<std::uint8_t>(tree.size >> 8)};
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
        out.push_back(node->value.key);
        out.push_back(static_cast<std::uint8_t>(node->value.x));
        out.push_back(static_cast<std::uint8_t>(node->value.x >> 8));
        out.push_back(static_cast<std::uint8_t>(node->value.y));
        out.push_back(static_cast<std::uint8_t>(node->value.y >> 8));
        out.push_back(static_cast<std::uint8_t>(node->color));
        node = node->right;
    }
    return out;
}

inline void ConstructSiteTree(SiteTree* tree) {
    const std::uint8_t bytes[2] = {0, 0};
    using Fn = SiteTree*(__fastcall*)(SiteTree*, void*, const std::uint8_t*, const std::uint8_t*);
    reinterpret_cast<Fn>(0x005590a0)(tree, nullptr, &bytes[0], &bytes[1]);
}

inline void MakeRecord(test::Rng& rng, AiArea* record, std::uint16_t area) {
    rng.Fill(record, offsetof(AiArea, site_trees));
    record->area = area;
    for (SiteTree& tree : record->site_trees) {
        ConstructSiteTree(&tree);
        const std::uint32_t count = rng.Chance(1, 2) ? 0 : rng.Below(6);
        for (std::uint32_t k = 0; k < count; ++k) {
            SiteValue value;
            rng.Fill(&value, sizeof(value));
            value.pad = 0;
            SiteTreeInsertResult result;
            using InsertFn = SiteTreeInsertResult*(__fastcall*)(SiteTree*, void*, SiteTreeInsertResult*, const SiteValue*);
            reinterpret_cast<InsertFn>(0x00559270)(&tree, nullptr, &result, &value);
        }
    }
}

inline void DestroyRecord(AiArea* record) {
    reinterpret_cast<void(__fastcall*)(AiArea*, void*)>(0x00556080)(record, nullptr);
}

// A CEvaLand with an empty list per player and the grid.
struct TestLand {
    AiLandEvaluation land{};
    explicit TestLand(std::vector<AiGridCell>& cells) {
        for (AiList& list : land.lists) {
            auto* head = static_cast<AiAreaNode*>(msvc6::GameOperatorNew(sizeof(AiAreaNode)));
            head->next = head->prev = head;
            list.head = reinterpret_cast<AiListNode*>(head);
        }
        land.cells = cells.data();
    }
    ~TestLand() {
        for (AiList& list : land.lists) {
            auto* head = reinterpret_cast<AiAreaNode*>(list.head);
            for (AiAreaNode* node = head->next; node != head;) {
                AiAreaNode* next = node->next;
                DestroyRecord(&node->value);
                msvc6::GameOperatorDelete(node);
                node = next;
            }
            msvc6::GameOperatorDelete(head);
        }
    }
    void Append(std::uint32_t player, const AiArea* record) {
        AiAreaNode* out;
        using Fn = AiAreaNode**(__fastcall*)(AiList*, void*, AiAreaNode**, AiAreaNode*, const AiArea*);
        reinterpret_cast<Fn>(0x005594f0)(&land.lists[player], nullptr, &out,
                                         reinterpret_cast<AiAreaNode*>(land.lists[player].head), record);
    }
};

}  // namespace s3re::game::ai_test
