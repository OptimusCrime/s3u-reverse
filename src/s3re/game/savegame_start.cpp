// Starting the game directly from a savegame (`s3re_launcher --load-save
// <file>`, S3RE_LOAD_SAVE), for whole-game checks on late-game states.
//
// Tooling, not a reimplementation. The game has no command-line option for
// loading a savegame, so this drives the game's own code the way the menus
// do:
//
// - `-map <file>` stores a startup map in the config object (0x7a7af0,
//   string at +0x15c). Menus::CDrawThread::vf04 (0x4932e0) then sets the
//   menu state (this+0x104) to 0x14, and the first call of
//   Menus::CDrawThread::vf13 (0x493880, the menu thread's per-frame update;
//   thiscall, one stack argument, `ret 4`; EAX is passed through) sets up a
//   new game from that map (GameSettings 0x431ab0) and starts it.
// - The load dialog (Menus::priv::CLoadGameDialog, handler 0x47a860) calls
//   GameSettings::LoadSaveGame (0x432b00) on the global GameSettings object
//   (0x7a7a18) with the path ("Save\" + name) and 0 (single player). It
//   returns 0 on success, 1 or 2 on errors (the dialog shows a message). On
//   success the dialog sends result 10, and Menus::CDrawThread::vf14
//   (0x494990, case 10 at 0x494e03) starts the game: this+0x144 = -1,
//   this+0x104 = 4, then 0x496c90(this, 1) (posts message 0x801 once).
// - LoadSaveGame sets GameSettings+0x28 (is a savegame) and keeps the open
//   file at +0x84; CLogicThread::OnStart (0x53c9e0) then calls the savegame
//   loader 0x508fa0 instead of GameWorld::Create/SeedRandom, so the RNG
//   state comes from the file and --seed has no effect.
//
// The launcher passes the save as `-map`, so the menu reaches state 0x14.
// This pass-through hook on vf13 then, on that first call, loads the
// savegame instead and does what the dialog result 10 does (without its
// 0x497020 check of the installed races), clears the startup map (0x413d50,
// as vf13 does after starting a map), and lets the original vf13 run on in
// state 4. If the load fails, the game is ended (exit code 4).

#include "game/globals.h"
#include "game/menus_layout.h"
#include "hooks.h"
#include "log.h"
#include "msvc6/string.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace s3re::game {

namespace {

using DrawThreadUpdateFn = std::uint32_t(__fastcall*)(menus::CDrawThread* self, void* edx, int arg);
using LoadSaveGameFn = int(__fastcall*)(void* settings, void* edx, const msvc6::String* path,
                                        int multiplayer);
using DrawThreadPostFn = void(__fastcall*)(menus::CDrawThread* self, void* edx, int arg);
using ClearStartupMapFn = void(__fastcall*)(void* config, void* edx);

std::uint32_t __fastcall Hooked_MenuDrawThread_Update(menus::CDrawThread* self, void* edx, int arg);

S3RE_PASSTHROUGH(0x00493880, MenuDrawThread_Update, DrawThreadUpdateFn,
                 Original_MenuDrawThread_Update, Hooked_MenuDrawThread_Update);

const auto GameSettings_LoadSaveGame = reinterpret_cast<LoadSaveGameFn>(0x00432b00);
const auto MenuDrawThread_Post = reinterpret_cast<DrawThreadPostFn>(0x00496c90);
const auto Config_ClearStartupMap = reinterpret_cast<ClearStartupMapFn>(0x00413d50);


constexpr std::uint32_t kMenuStartupMap = 0x14;
constexpr std::uint32_t kMenuStartGame = 4;

std::string SaveToLoad() {
    char value[MAX_PATH * 2] = {};
    const DWORD length = GetEnvironmentVariableA("S3RE_LOAD_SAVE", value, sizeof(value));
    return (length > 0 && length < sizeof(value)) ? std::string(value, length) : std::string();
}

bool g_handled = false;  // only touched by the menu thread

void LoadSave(menus::CDrawThread* self, const std::string& path) {
    msvc6::String name{};
    msvc6::Original_String_Tidy(&name, nullptr, 0);
    msvc6::Original_String_AssignChars(&name, nullptr, path.c_str(),
                                       static_cast<std::uint32_t>(path.size()));
    const int result = GameSettings_LoadSaveGame(&g_gameSettings, nullptr, &name, 0);
    msvc6::Original_String_Tidy(&name, nullptr, 1);
    if (result != 0) {
        S3RE_LOG("load-save: GameSettings::LoadSaveGame(%s) failed with %d; ending the game",
                 path.c_str(), result);
        TerminateProcess(GetCurrentProcess(), 4);
        return;
    }
    S3RE_LOG("load-save: loaded %s; starting the game", path.c_str());
    self->unk144 = -1;
    self->menuState = kMenuStartGame;
    MenuDrawThread_Post(self, nullptr, 1);
    Config_ClearStartupMap(&g_config, nullptr);
}

std::uint32_t __fastcall Hooked_MenuDrawThread_Update(menus::CDrawThread* self, void* edx, int arg) {
    if (!g_handled && self->menuState == kMenuStartupMap && self->unk134 == 0) {
        g_handled = true;
        const std::string path = SaveToLoad();
        if (!path.empty()) {
            LoadSave(self, path);
        }
    }
    return Original_MenuDrawThread_Update(self, edx, arg);
}

}  // namespace

}  // namespace s3re::game
