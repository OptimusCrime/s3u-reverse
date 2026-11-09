#pragma once

// The fields of the logic thread object (CLogicThread) that the UI helpers
// (ui_state.cpp, ui_overlays.cpp) use. Its full layout is not mapped; the
// struct is a view of the known fields only (unkXX = unknown).

#include "game/layout.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::ui {

#pragma pack(push, 1)

struct LogicThreadView {
    std::uint8_t unk000[0x74];
    std::uint32_t unk074;                 // 0x074 cleared by ClearPendingUiRefresh
    std::uint8_t unk078[0xc];
    std::uint32_t pending_ui_refresh;     // 0x084 turned into the world's refresh_ui
    std::uint8_t unk088[0x3a0 - 0x88];
    GameWorld* world;                     // 0x3a0
};
static_assert(offsetof(LogicThreadView, unk074) == 0x74);
static_assert(offsetof(LogicThreadView, pending_ui_refresh) == 0x84);
static_assert(offsetof(LogicThreadView, world) == 0x3a0);
static_assert(sizeof(LogicThreadView) == 0x3a4);

#pragma pack(pop)

// The logic thread's fields (CLogicThread is opaque elsewhere).
template <typename Thread>
LogicThreadView* LogicThreadOf(Thread* thread) {
    return reinterpret_cast<LogicThreadView*>(thread);
}

}  // namespace s3re::game::ui
