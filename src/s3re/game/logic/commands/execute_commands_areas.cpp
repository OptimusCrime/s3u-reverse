// Part of CLogicThread::ExecuteCommands (0x535990), see execute_commands.h:
// the commands that change area settings, a tower's garrison and carriers.
//
// 0x0a ChangeTransportOrder (0x53919d): payload good, operation, player,
//   area (word). Area +0x21a (settings5) is a per-good flag (operations 1 on,
//   2 off), Area +0x1f6 (settings4) the good's place in the transport order
//   (1 = first): 3 to the top, 4 one up, 5 one down, 6 to the race's last
//   place (26 Roman, 27 Egyptian, 29 Asian and Amazon). Good 0 is ignored.
// 0x0b ChangeToolSettings (0x5395b5): payload kind, value, player, area
//   (word). Kinds 1..3: three shares in steps of 5 (Area +0x23e..+0x240,
//   value 0 = less, else more; together at most 100; the first stays >= 5);
//   4..6 / 7..9: Area +0x242..+0x244 up / down by the value within 0..100;
//   10: Area +0x241 = value.
// 0x0c ChangeGarrison (0x539a7f): payload building (word), player,
//   operation. A small tower, large tower or castle (1 / 3 / 6 soldiers;
//   bowmen up to 2 / 3 / 4) changes the soldiers it wants per class
//   (Building +0x1a swordsmen, +0x1c bowmen, +0x20 pikemen): 1 bowmen to the
//   maximum and the rest split, 2..4 one more of a class, 5 one of the class
//   present (swords, pikes, bows) and none of the others, 6..8 one less of a
//   class (not the last soldier unless another class is in the garrison).
// 0x10 ConvertCarriers (0x539f30): payload kind, player, x, y (words),
//   count. Up to `count` idle carriers (or, kind 4, pioneers) of the player
//   on the player's tiles around (x, y) become geologists (1), thieves (2),
//   pioneers (3) or carriers again (4); the local player gets the new units
//   as settler group 0 (or loses the pioneers from the groups).

#include "game/logic/commands/execute_commands.h"

#include "game/globals.h"
#include "game/settlers/settler_groups.h"
#include "game/typed_fields.h"
#include "hooks.h"

#include <cstdint>

namespace s3re::game::commands {

namespace {

using layout::Area;
using layout::Building;
using layout::Player;
using layout::Settler;
using layout::Tile;

constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;
constexpr std::uintptr_t kRemoveSettlerFromGroups = 0x005428e0;  // ECX = the thread
constexpr std::uintptr_t kUpdateGroupMenu = 0x00542990;          // ECX = the thread

constexpr std::int32_t kMenuAreas = 2;   // ui_2d290[0] is its page
constexpr std::int32_t kMenuPlayer = 3;  // ui_2d2a0 is its page
constexpr std::int32_t kPageTransport = 4;
constexpr std::int32_t kPageShares = 2;
constexpr std::int32_t kPageTools = 3;

constexpr std::uint32_t kGoodsTypes = static_cast<std::uint32_t>(layout::kGoodsTypes);

Area& AreaAt(CGameData* world, std::uint32_t n) {
    return *(world->areas + n);
}
Building& BuildingAt(CGameData* world, std::uint32_t n) {
    return *(world->buildings + n);
}
Settler& SettlerAt(CGameData* world, std::uint32_t n) {
    return *(world->settlers + n);
}

// ------------------------------------------------------------ 0x0a ChangeTransportOrder (0x53919d)

std::uint8_t& OrderOf(Run& run, std::uint32_t area, std::uint32_t good) {
    return *(AreaAt(run.world(), area).settings4 + good);
}

void RefreshTransportPage(Run& run, std::uint32_t player) {
    RefreshIfMenuShows(run, player, kMenuAreas, run.world()->ui_2d290[0], kPageTransport);
}

// The last place of the transport order per race (0x5394d5..0x539578).
void PutAtRaceEnd(Run& run, std::uint32_t area, std::uint32_t i) {
    struct RaceEnd {
        Race race;
        std::uint8_t place;
    };
    constexpr RaceEnd kEnds[] = {{Race::Roman, 26}, {Race::Egyptian, 27}, {Race::Asian, 29}, {Race::Amazon, 29}};
    for (const RaceEnd& end : kEnds) {
        CGameData* world = run.world();
        const Player& owner = *(world->players + AreaAt(world, area).player);
        if (RaceOf(owner) == end.race) {
            OrderOf(run, area, i) = end.place;
        }
    }
}

}  // namespace

void ChangeTransportOrder(Run& run) {
    enum Operation : std::uint32_t { kOn = 1, kOff = 2, kTop = 3, kUp = 4, kDown = 5, kLast = 6 };
    const std::uint32_t good = run.Byte(0);
    const std::uint32_t operation = run.Byte(1);
    const std::uint32_t player = run.Byte(2);
    const std::uint32_t area = run.Word(3);
    Area& settings = AreaAt(run.world(), area);
    if (settings.player != player || good == 0) {
        return;
    }
    std::uint8_t& flag = *(settings.settings5 + good);
    switch (operation) {
    case kOn:
        if (flag == 0) {
            flag = 1;
            RefreshTransportPage(run, player);
        }
        return;
    case kOff:
        if (flag == 1) {
            flag = 0;
            RefreshTransportPage(run, player);
        }
        return;
    case kTop: {
        // 0x539289: the goods before it move one down, it becomes first.
        const std::uint32_t place = OrderOf(run, area, good);
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            std::uint8_t& other = OrderOf(run, area, i);
            if (other != 0 && other < place) {
                ++other;
            } else if (other == place) {
                other = 1;
            }
        }
        RefreshTransportPage(run, player);
        return;
    }
    case kUp: {
        // 0x539318: swaps with every good one place before it.
        const std::uint32_t place = OrderOf(run, area, good);
        if (place == 1) {
            return;
        }
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            std::uint8_t& other = OrderOf(run, area, i);
            if (other == place - 1) {
                ++other;
                --OrderOf(run, area, good);
            }
        }
        RefreshTransportPage(run, player);
        return;
    }
    case kDown: {
        // 0x5393d1: swaps with every good one place after it.
        const std::uint32_t place = OrderOf(run, area, good);
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            std::uint8_t& other = OrderOf(run, area, i);
            if (other == place + 1) {
                --other;
                ++OrderOf(run, area, good);
            }
        }
        RefreshTransportPage(run, player);
        return;
    }
    case kLast: {
        // 0x539476: the goods after it move one up, it (and any good at its
        // place) goes to the race's last place. The original checks the
        // refresh after every good (same result).
        const std::uint32_t place = OrderOf(run, area, good);
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            std::uint8_t& other = OrderOf(run, area, i);
            if (other > place) {
                --other;
            } else if (other == place) {
                PutAtRaceEnd(run, area, i);
            }
            RefreshTransportPage(run, player);
        }
        return;
    }
    default:
        return;
    }
}

// ------------------------------------------------------------ 0x0b ChangeToolSettings (0x5395b5)

void ChangeToolSettings(Run& run) {
    constexpr std::uint32_t kShareStep = 5;
    constexpr std::int32_t kSharesBeforeRaise = 95;  // a share may grow while the three sum to at most 95
    constexpr std::uint32_t kPercentMax = 100;
    const std::uint32_t kind = run.Byte(0);
    const std::uint8_t value = run.Byte(1);
    const std::uint32_t player = run.Byte(2);
    Area& area = AreaAt(run.world(), run.Word(3));
    if (area.player != player) {
        return;
    }
    if (kind >= 1 && kind <= 3) {
        // 0x53961b..0x539847: a share one step down (the first not below 5) or up.
        std::uint8_t& share = area.settings_extra[kind - 1];
        const std::uint8_t lowest = kind == 1 ? 2 * kShareStep : kShareStep;  // the first share keeps 5
        if (value == 0) {
            if (share < lowest) {
                return;
            }
            share = static_cast<std::uint8_t>(share - kShareStep);
        } else {
            const std::int32_t sum = area.settings_extra[0] + area.settings_extra[1] + area.settings_extra[2];
            if (sum > kSharesBeforeRaise) {
                return;
            }
            share = static_cast<std::uint8_t>(share + kShareStep);
        }
        RefreshIfMenuShows(run, player, kMenuPlayer, run.world()->ui_2d2a0, kPageShares);
        return;
    }
    if (kind >= 4 && kind <= 9) {
        // 0x53984a..0x539a40: a percentage up (4..6) or down (7..9) by the value, within 0..100.
        const bool up = kind <= 6;
        std::uint8_t& percent = area.counters_242[up ? kind - 4 : kind - 7];
        if (up) {
            percent = percent + value < kPercentMax ? static_cast<std::uint8_t>(percent + value)
                                                    : static_cast<std::uint8_t>(kPercentMax);
        } else {
            percent = percent > value ? static_cast<std::uint8_t>(percent - value) : 0;
        }
        RefreshIfMenuShows(run, player, kMenuPlayer, run.world()->ui_2d2a0, kPageTools);
        return;
    }
    if (kind == 10) {
        area.setting_241 = value;  // 0x539a4e
        RefreshIfMenuShows(run, player, kMenuPlayer, run.world()->ui_2d2a0, kPageTools);
    }
}

// ------------------------------------------------------------ 0x0c ChangeGarrison (0x539a7f)

namespace {

// Soldiers in the garrison chain by class (0x539b32..0x539c5c).
struct GarrisonCount {
    std::uint32_t swordsmen = 0;
    std::uint32_t bowmen = 0;
    std::uint32_t pikemen = 0;
};

GarrisonCount CountGarrison(CGameData* world, std::uint32_t first) {
    GarrisonCount count;
    for (std::uint32_t n = first; n != 0;) {
        switch (TypeOf(SettlerAt(world, n))) {
        case SettlerType::Swordsman1:
        case SettlerType::Swordsman2:
        case SettlerType::Swordsman3: ++count.swordsmen; break;
        case SettlerType::Bowman1:
        case SettlerType::Bowman2:
        case SettlerType::Bowman3: ++count.bowmen; break;
        case SettlerType::Pikeman1:
        case SettlerType::Pikeman2:
        case SettlerType::Pikeman3: ++count.pikemen; break;
        default:
            // 0x539c4b: another type continues with settler 0's next (deliberate quirk).
            n = 0;
            break;
        }
        n = SettlerAt(world, n).next;
    }
    return count;
}

// 6..8: one less of a class, keeping at least one soldier unless another class is present.
void OneLess(std::uint8_t& wanted, std::uint32_t others) {
    if (wanted == 0) {
        return;
    }
    if (others == 0 && wanted < 2) {
        return;
    }
    --wanted;
}

}  // namespace

void ChangeGarrison(Run& run) {
    enum Operation : std::uint32_t {
        kSpread = 1, kMoreSwords = 2, kMoreBows = 3, kMorePikes = 4, kOnlyPresent = 5,
        kLessSwords = 6, kLessBows = 7, kLessPikes = 8,
    };
    const std::uint32_t number = run.Word(0);
    const std::uint32_t player = run.Byte(2);
    const std::uint32_t operation = run.Byte(3);
    CGameData* world = run.world();
    Building& building = BuildingAt(world, number);
    if (building.owner == player) {
        // 0x539adb: the sizes per building type (kept in the frame, FrameLocals).
        FrameLocals& frame = *run.frame;
        switch (TypeOf(building)) {
        case BuildingType::SmallTower: frame.garrison_soldiers = 1; frame.garrison_bowmen = 2; break;
        case BuildingType::LargeTower: frame.garrison_soldiers = 3; frame.garrison_bowmen = 3; break;
        case BuildingType::Castle: frame.garrison_soldiers = 6; frame.garrison_bowmen = 4; break;
        default: break;
        }
        const std::uint32_t soldiers = frame.garrison_soldiers;
        const std::uint32_t bowmen = frame.garrison_bowmen;
        const GarrisonCount present = CountGarrison(world, building.garrison);
        layout::BuildingMilitaryBytes& wanted = building.military;
        switch (operation) {
        case kSpread: {
            // 0x539c6d: bowmen to the maximum, the rest alternately to swords and pikes.
            wanted.class2_wanted = static_cast<std::uint8_t>(bowmen);
            const std::uint32_t taken = wanted.class1_wanted + wanted.class3_wanted;
            if (taken == soldiers) {
                break;
            }
            for (std::uint32_t left = soldiers - taken; left != 0; --left) {
                if (wanted.class1_wanted <= wanted.class3_wanted) {
                    ++wanted.class1_wanted;
                } else {
                    ++wanted.class3_wanted;
                }
            }
            break;
        }
        case kMoreSwords:
            if (wanted.class3_wanted + static_cast<std::uint32_t>(wanted.class1_wanted) != soldiers) {
                ++wanted.class1_wanted;
            }
            break;
        case kMoreBows:
            if (wanted.class2_wanted != bowmen) {
                ++wanted.class2_wanted;
            }
            break;
        case kMorePikes:
            if (wanted.class1_wanted + static_cast<std::uint32_t>(wanted.class3_wanted) != soldiers) {
                ++wanted.class3_wanted;
            }
            break;
        case kOnlyPresent:
            // 0x539dbb: one soldier of the first class present (swords, pikes, bows), swords if none.
            wanted.class1_wanted = present.swordsmen != 0 || (present.pikemen == 0 && present.bowmen == 0) ? 1 : 0;
            wanted.class2_wanted = present.swordsmen == 0 && present.pikemen == 0 && present.bowmen != 0 ? 1 : 0;
            wanted.class3_wanted = present.swordsmen == 0 && present.pikemen != 0 ? 1 : 0;
            break;
        case kLessSwords: OneLess(wanted.class1_wanted, present.pikemen + present.bowmen); break;
        case kLessBows: OneLess(wanted.class2_wanted, present.pikemen + present.swordsmen); break;
        case kLessPikes: OneLess(wanted.class3_wanted, present.bowmen + present.swordsmen); break;
        default: break;
        }
    }
    RefreshIfBuildingShown(run, number);
}

// ------------------------------------------------------------ 0x10 ConvertCarriers (0x539f30)

namespace {

bool IsConvertibleJob(SettlerJob job) {
    return job == SettlerJob::Nothing || job == SettlerJob::StepAside || job == SettlerJob::UnitIdle ||
           job == SettlerJob::UnitWalk;
}

bool IsNewUnit(std::uint32_t type) {
    return type == Value(SettlerType::Geologist) || type == Value(SettlerType::Thief) ||
           type == Value(SettlerType::Pioneer);
}

}  // namespace

void ConvertCarriers(Run& run) {
    constexpr std::uint32_t kSearchOffsets = 3000;  // MapOffset table entries searched
    constexpr std::uint8_t kClearMark = 0x7f;       // Settler::flags & 0x7f: the carrier mark 0x80 goes
    const std::uint32_t kind = run.Byte(0);
    const std::uint32_t player = run.Byte(1);
    const std::uint32_t x = run.Word(2);
    const std::uint32_t y = run.Word(4);
    std::uint32_t left = run.Byte(6);
    SettlerType from = SettlerType::Carrier;
    SettlerType to = SettlerType::Carrier;
    switch (kind) {
    case 1: to = SettlerType::Geologist; break;
    case 2: to = SettlerType::Thief; break;
    case 3: to = SettlerType::Pioneer; break;
    case 4: from = SettlerType::Pioneer; break;
    default: return;
    }
    SettlerGroups& groups = g_settlerGroups;
    if (static_cast<std::uint32_t>(run.world()->local_player) == player) {
        groups.counts[0] = 0;  // 0x53a00d: the local player's new units become group 0
        g_settlerGroupChanged[0] = 1;
    }
    for (std::uint32_t i = 0; left != 0 && i < kSearchOffsets; ++i) {
        CGameData* world = run.world();
        const layout::MapOffset& offset = *(world->offsets + i);
        const Tile& tile = TileAtOffset(world, TileOffset(x + static_cast<std::uint32_t>(offset.dx),
                                                         y + static_cast<std::uint32_t>(offset.dy)));
        const std::uint32_t n = tile.settler;
        if (n == 0) {
            continue;
        }
        const Settler& settler = SettlerAt(world, n);
        if (settler.owner != player || settler.art != Value(from) || tile.owner != player ||
            !IsConvertibleJob(JobOf(settler))) {
            continue;
        }
        // 0x53a0dc
        Call<kResetSettlerToIdle>(world, n);
        SettlerAt(run.world(), n).art = Value(to);
        SettlerAt(run.world(), n).flags &= kClearMark;
        --left;
        SetJob(SettlerAt(run.world(), n), SettlerJob::New);
        if (IsNewUnit(Value(to))) {
            if (static_cast<std::uint32_t>(run.world()->local_player) == player) {
                // 0x53a157: add it to group 0 and show that group.
                const std::uint16_t count = groups.counts[0];
                ++groups.counts[0];
                g_settlerGroupChanged[0] = 1;
                *(&groups.lists[0][0] + count) = static_cast<std::uint16_t>(n);  // no limit check (as the original)
                g_currentSettlerGroup = 0;
                run.thread->group_selection_changed = 1;
                g_selectedBuilding = 0;
                CallThread<kUpdateGroupMenu>(run.thread, static_cast<std::uint32_t>(g_currentSettlerGroup));
            }
        } else if (to == SettlerType::Carrier) {
            CallThread<kRemoveSettlerFromGroups>(run.thread, n, static_cast<std::uint32_t>(g_currentSettlerGroup));
        }
    }
}

}  // namespace s3re::game::commands

// ---------------------------------------------------------------------------
// Tests: commands 0x0a, 0x0b, 0x0c and 0x10 (execute_commands_test.h).
// ---------------------------------------------------------------------------

#include "game/logic/commands/execute_commands_test.h"

#include <cstring>

namespace s3re::game::commands {

namespace {

using bench::Bench;
using bench::MessageBuilder;

constexpr std::uint32_t kAreasTestAreas = 16;
constexpr std::uint32_t kMilitaryBuildings = 20;  // 1..20 towers and castles, 21..40 others
constexpr std::uint32_t kAreasTestBuildings = 40;
constexpr std::uint32_t kAreasTestSettlers = 200;
constexpr std::uint32_t kGarrisonEnd = 199;  // settler 0's next: a one-soldier chain
constexpr std::uint32_t kAreasTestMap = 400;

// The S3.EXE globals ConvertCarriers and the tests change.
struct AreaGlobals {
    SettlerGroups groups;
    std::int32_t changed[10];
    std::int32_t current;
    std::int32_t selected;
    void Take() {
        groups = g_settlerGroups;
        std::memcpy(changed, &g_settlerGroupChanged, sizeof(changed));
        current = g_currentSettlerGroup;
        selected = g_selectedBuilding;
    }
    void Put() const {
        g_settlerGroups.get() = groups;
        std::memcpy(&g_settlerGroupChanged, changed, sizeof(changed));
        g_currentSettlerGroup = current;
        g_selectedBuilding = selected;
    }
    bool operator==(const AreaGlobals& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

struct SavedAreaGlobals {
    AreaGlobals saved;
    SavedAreaGlobals() { saved.Take(); }
    ~SavedAreaGlobals() { saved.Put(); }
};

void InstallAreaStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](CGameData* w, std::uint32_t, const std::uint32_t* a) {
        w->settlers[a[0] % 32000].counter = 0x1234;  // a visible side effect
        return a[0] + 7;
    });
    stubs.Install<1, kUpdateGroupMenu>();
    stubs.Install<2, kRemoveSettlerFromGroups>();
}

const std::uint8_t kAreaSettlerTypes[] = {5, 32, 35, 8, 30, 33, 15, 31, 34, 0, 0, 40, 40, 22};

void SetUpAreaWorld(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    test::WorldPair& worlds = bench.worlds;
    for (std::uint32_t p = 0; p < 4; ++p) {
        const auto race = static_cast<std::int32_t>(p == 3 ? rng.Below(5) : p);
        worlds.Both([&](CGameData* w) { w->players[p].race = race; });
    }
    for (std::uint32_t a = 0; a < kAreasTestAreas; ++a) {
        Area area;
        rng.Fill(&area, sizeof(area));
        area.player = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        for (std::uint32_t g = 0; g < layout::kGoodsTypes; ++g) {
            area.settings4[g] = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Below(256) : rng.Below(36));
            area.settings5[g] = static_cast<std::uint8_t>(rng.Below(3));
        }
        for (auto& share : area.settings_extra) {
            share = static_cast<std::uint8_t>(5 * rng.Below(15));
        }
        for (auto& percent : area.counters_242) {
            percent = static_cast<std::uint8_t>(rng.Below(101));
        }
        worlds.Both([&](CGameData* w) { w->areas[a] = area; });
    }
    // Settlers: soldiers for the garrison chains, carriers, pioneers and others on the map.
    for (std::uint32_t n = 0; n <= kAreasTestSettlers; ++n) {
        Settler s;
        rng.Fill(&s, sizeof(s));
        s.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        s.art = kAreaSettlerTypes[rng.Below(static_cast<std::uint32_t>(sizeof(kAreaSettlerTypes)))];
        const std::uint16_t jobs[] = {1, 2, 0x37, 0x35, 0, 3};
        s.job = jobs[rng.Below(6)];
        s.next = 0;
        if (n == 0) {
            s.next = static_cast<std::uint16_t>(kGarrisonEnd);
        }
        if (n == kGarrisonEnd) {
            s.art = 5;
        }
        worlds.Both([&](CGameData* w) { w->settlers[n] = s; });
    }
    // Garrison chains: settlers 1..120 in chains of up to 6, one per military building.
    std::uint32_t next = 1;
    const std::uint8_t military[] = {15, 16, 18};
    for (std::uint32_t b = 1; b <= kAreasTestBuildings; ++b) {
        const bool isMilitary = b <= kMilitaryBuildings;
        const auto owner = static_cast<std::uint8_t>(isMilitary && rng.Chance(1, 8) ? 0xff : rng.Below(4));
        const auto type = isMilitary ? military[rng.Below(3)] : static_cast<std::uint8_t>(1 + rng.Below(14));
        const std::uint32_t length = isMilitary ? rng.Below(7) : 0;
        std::uint16_t first = 0;
        if (length != 0 && next + length < 121) {
            first = static_cast<std::uint16_t>(next);
            for (std::uint32_t k = 0; k < length; ++k, ++next) {
                const auto link = static_cast<std::uint16_t>(k + 1 == length ? 0 : next + 1);
                worlds.Both([&](CGameData* w) { w->settlers[next].next = link; });
            }
        }
        // Wanted soldiers within the building's size (1 / 3 / 6).
        const std::uint32_t size = type == 15 ? 1 : type == 16 ? 3 : 6;
        const std::uint32_t swords = rng.Below(size + 1);
        const std::uint32_t pikes = rng.Below(size - swords + 1);
        const auto bows = static_cast<std::uint8_t>(rng.Below(3));
        worlds.Both([&](CGameData* w) {
            Building& building = w->buildings[b];
            building.owner = owner;
            building.art = type;
            building.garrison = first;
            building.military.class1_wanted = static_cast<std::uint8_t>(swords);
            building.military.class3_wanted = static_cast<std::uint8_t>(pikes);
            building.military.class2_wanted = bows;
        });
    }
    // Tiles of the conversion: settlers and owners; the offset table: small steps.
    for (std::uint32_t x = 0; x < kAreasTestMap; ++x) {
        for (std::uint32_t y = 0; y < kAreasTestMap; ++y) {
            const auto settler = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : rng.Below(kAreasTestSettlers));
            const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
            worlds.Both([&](CGameData* w) {
                w->tiles[x][y].settler = settler;
                w->tiles[x][y].owner = owner;
            });
        }
    }
    for (std::uint32_t i = 0; i < 3000; ++i) {
        const layout::MapOffset offset{rng.Range(-25, 25), rng.Range(-25, 25), static_cast<std::int32_t>(i / 10),
                                       0};
        worlds.Both([&](CGameData* w) { w->offsets[i] = offset; });
    }
}

std::uint8_t AreaHi(std::uint32_t v) {
    return static_cast<std::uint8_t>(v >> 8);
}
std::uint8_t AreaLo(std::uint32_t v) {
    return static_cast<std::uint8_t>(v);
}

void AddAreaCommand(test::Context& ctx, Bench& bench, MessageBuilder& m) {
    test::Rng& rng = ctx.rng();
    const auto player = static_cast<std::uint8_t>(rng.Chance(1, 10) ? rng.Below(20) : rng.Below(4));
    const std::uint32_t area = rng.Below(kAreasTestAreas);
    // Mostly the area's own player (the area commands do nothing for others).
    const std::uint8_t areaOwner = bench.worlds.original()->areas[area].player;
    const auto areaPlayer = static_cast<std::uint8_t>(rng.Chance(7, 10) ? areaOwner : player);
    switch (rng.Below(4)) {
    case 0:
        m.Command(CommandId::ChangeTransportOrder,
                  {AreaLo(rng.Chance(1, 10) ? rng.Below(256) : rng.Below(36)), AreaLo(rng.Below(8)), areaPlayer,
                   AreaHi(area), AreaLo(area)});
        break;
    case 1:
        m.Command(CommandId::ChangeToolSettings,
                  {AreaLo(rng.Below(12)), AreaLo(rng.Chance(1, 3) ? 0 : rng.Below(rng.Chance(1, 2) ? 30 : 256)),
                   areaPlayer, AreaHi(area), AreaLo(area)});
        break;
    case 2: {
        // A tower or castle of any player, or another building of another
        // player (for the player's own other buildings the original reuses
        // sizes from its frame, which the test can't reproduce).
        const std::uint32_t building = 1 + rng.Below(kAreasTestBuildings);
        std::uint8_t who = rng.Chance(2, 3) ? bench.worlds.original()->buildings[building].owner : player;
        if (building > kMilitaryBuildings) {
            const std::uint8_t owner = bench.worlds.original()->buildings[building].owner;
            if (owner == who) {
                who = static_cast<std::uint8_t>((owner + 1) % 4);
            }
        }
        m.Command(CommandId::ChangeGarrison, {AreaHi(building), AreaLo(building), who, AreaLo(rng.Below(10))});
        break;
    }
    default: {
        const std::uint32_t x = 30 + rng.Below(kAreasTestMap - 60);
        const std::uint32_t y = 30 + rng.Below(kAreasTestMap - 60);
        m.Command(CommandId::ConvertCarriers, {AreaLo(rng.Below(6)), player, AreaHi(x), AreaLo(x), AreaHi(y),
                                               AreaLo(y), AreaLo(rng.Below(rng.Chance(1, 4) ? 40 : 6))});
        break;
    }
    }
}

void RandomizeAreaUi(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    const auto local = static_cast<std::int32_t>(rng.Below(4));
    const auto menu = static_cast<std::int32_t>(rng.Below(6));
    const auto page = static_cast<std::int32_t>(rng.Below(6));
    const auto page2 = static_cast<std::int32_t>(rng.Below(5));
    bench.worlds.Both([&](CGameData* w) {
        w->local_player = local;
        w->ui_selection_mode = menu;
        w->ui_2d290[0] = page;
        w->ui_2d2a0 = page2;
        w->refresh_ui = 0;
    });
    g_selectedBuilding = static_cast<std::int32_t>(rng.Below(kMilitaryBuildings + 2));
    g_currentSettlerGroup = static_cast<std::int32_t>(rng.Below(10));
    g_settlerGroups->counts[0] = static_cast<std::uint16_t>(rng.Below(50));
}

// The original runs first (side A); its globals are kept and the starting
// values restored for our run, then both are compared.
AreaGlobals g_areasStart;
AreaGlobals g_areasAfterOriginal;
ExecuteCommandsFn g_areasOriginal = nullptr;

std::uint32_t __fastcall OriginalKeepingGlobals(CLogicThread* self, void* edx) {
    g_areasStart.Take();
    const std::uint32_t result = g_areasOriginal(self, edx);
    g_areasAfterOriginal.Take();
    g_areasStart.Put();
    return result;
}

void CompareAreaCommands(test::Context& ctx, ExecuteCommandsFn candidate, int cases) {
    SavedAreaGlobals saved;
    Bench bench(ctx);
    SetUpAreaWorld(ctx, bench);
    InstallAreaStubs(bench.stubs);
    test::Rng& rng = ctx.rng();
    g_areasOriginal = Original_CLogicThread_ExecuteCommands;
    for (int i = 0; i < cases; ++i) {
        RandomizeAreaUi(ctx, bench);
        const std::uint32_t step = 1 + rng.Below(1000);
        bench.SetStep(step);
        MessageBuilder messages;
        const std::uint32_t count = 1 + rng.Below(4);
        for (std::uint32_t k = 0; k < count; ++k) {
            AddAreaCommand(ctx, bench, messages);
        }
        messages.Grant(step + 1);
        Original_CLogicThread_ExecuteCommands = &OriginalKeepingGlobals;
        const bool same = bench.Run(ctx, i, messages, -1, candidate);
        Original_CLogicThread_ExecuteCommands = g_areasOriginal;
        if (!same) {
            return;
        }
        AreaGlobals ours;
        ours.Take();
        if (!(ours == g_areasAfterOriginal)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: globals (settler groups, selection) differ", i);
            return;
        }
    }
    bench.Finish(ctx);
}

// Wrong: the first tool share goes down in steps of 10.
std::uint32_t __fastcall WrongAreaCommands(CLogicThread* self, void* edx) {
    CGameData* world = self->world;
    std::uint8_t before[kAreasTestAreas];
    for (std::uint32_t a = 0; a < kAreasTestAreas; ++a) {
        before[a] = world->areas[a].settings_extra[0];
    }
    const std::uint32_t result = CLogicThread_ExecuteCommands(self, edx);
    for (std::uint32_t a = 0; a < kAreasTestAreas; ++a) {
        std::uint8_t& share = world->areas[a].settings_extra[0];
        if (before[a] == share + 5) {
            share = static_cast<std::uint8_t>(share - 5);
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ExecuteCommands_Areas_MatchesOriginal) {
    CompareAreaCommands(ctx, &CLogicThread_ExecuteCommands, 3000);
}
S3RE_TEST(CLogicThread_ExecuteCommands_Areas_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAreaCommands(probe, &WrongAreaCommands, 800);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::commands
