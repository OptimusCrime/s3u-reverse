#pragma once

// The ten settler groups in S3.EXE's data (not in the world): uint16 counts
// at 0x7ad620 and lists of 100 settler numbers each at 0x7ad634 (probably
// the player's selection groups). Used by GameWorld::RemoveSettlerFromLists
// (settler_queries.cpp) and CLogicThread::RemoveSettlerFromGroups
// (logic_helpers.cpp).

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)
struct SettlerGroups {
    static constexpr std::size_t kGroups = 10;
    static constexpr std::size_t kWords = 100;

    std::uint16_t counts[kGroups];        // 0x7ad620
    std::uint16_t lists[kGroups][kWords];  // 0x7ad634
};
#pragma pack(pop)
static_assert(sizeof(SettlerGroups) == 0x7e4);  // 0x7ad620..0x7ade04
static_assert(offsetof(SettlerGroups, lists) == 0x14);

constexpr std::uintptr_t kSettlerGroupsAddress = 0x007ad620;

inline SettlerGroups& GlobalSettlerGroups() {
    return *reinterpret_cast<SettlerGroups*>(kSettlerGroupsAddress);
}

}  // namespace s3re::game
