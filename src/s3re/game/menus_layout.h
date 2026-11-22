#pragma once

// Layouts of the menu classes (Menus::*; Menus::CDrawThread::Update is in
// Dirk\netdialogs.cpp), named after their RTTI classes. Only the fields our code uses are named;
// unkXXX = unknown.

#include "game/screen_element_layout.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::menus {

#pragma pack(push, 1)

// Menus::CDrawThread, the menu thread (RTTI: bases DirectX::CGameDrawThread
// -> DirectX::CGameThread -> Win32::CThread ... at +0, CScreen at +0x54, and
// CScreen's virtual base CScreenElement at +0x150, where the vtable
// 0x6583d8 is stored). Update 0x493880 runs the menu state machine.
struct CDrawThread {
    std::uint8_t unk000[0x54];   // +0x000 the DirectX::CGameDrawThread part
    ui::CScreen screen;          // +0x054
    std::uint8_t unk0ec[0x104 - 0xec];
    std::uint32_t menuState;     // +0x104 0x14: start the startup map, 4: start the game
    std::uint8_t unk108[0x134 - 0x108];
    std::uint8_t unk134;         // +0x134 0 when vf13 is about to start the startup map (checked by our hook)
    std::uint8_t unk135[0x144 - 0x135];
    std::int32_t unk144;         // +0x144 set to -1 when a loaded game starts
    std::uint8_t unk148[0x150 - 0x148];
    ui::CScreenElement element;  // +0x150 the CScreen's virtual base
};
static_assert(offsetof(CDrawThread, screen) == 0x54);
static_assert(offsetof(CDrawThread, menuState) == 0x104);
static_assert(offsetof(CDrawThread, unk134) == 0x134);
static_assert(offsetof(CDrawThread, unk144) == 0x144);
static_assert(offsetof(CDrawThread, element) == 0x150);

#pragma pack(pop)

}  // namespace s3re::game::menus
