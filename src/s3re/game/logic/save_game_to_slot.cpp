// CLogicThread::SaveGameToSlot (0x0050ac40), Volker\GameDataSave.cpp:
// (bool multiplayer, int slot) ret 8, thiscall on the logic thread; returns
// SaveGame's bool in AL. Callers: CLogicThread::ProcessEvents (the eight
// save keys / menu entries of single player) and ExecuteCommands (command
// 0xf8, the multiplayer save).
//
// Single player: the name is sprintf("slot%2i.sav", slot) (the game's
// sprintf 0x61f27e). Multiplayer: the game's GUID (GameSettings::GameGuid
// 0x437400 on g_gameSettings) is formatted with UuidToStringA, the name is
// "Multi\temp.mps" (the GUID text is not used; RpcStringFreeA frees it at
// the end). Then CLogicThread::SaveGame (0x509980) with the name, and the
// name is destroyed (inlined erase() and _Tidy(true)).
//
// The original builds the name through two temporaries that share one
// buffer (assign, then two whole-string assigns); ours assigns the text
// once, which makes the same single allocation. The name's allocator byte
// is an uninitialized local in the original; it is destroyed before the
// function returns, so ours uses 0 (docs/reimplementation_notes.md).
// The imports are called through S3.EXE's import table (globals.h).

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using GameGuidFn = std::uint32_t*(__fastcall*)(const GameSettings* self, void* edx, std::uint32_t* out);
// SaveGame's result is a bool in AL; it is passed on as the byte it is.
using SaveGameFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, const msvc6::String* name);
constexpr std::uintptr_t kSprintf = 0x0061f27e;   // the CRT's sprintf
constexpr std::uintptr_t kGameGuid = 0x00437400;  // GameSettings::GameGuid (verified)
constexpr std::uintptr_t kSaveGame = 0x00509980;  // CLogicThread::SaveGame (verified)

// Saves under `text` (a C string) and destroys the name again; SaveGame's
// result byte.
std::uint8_t SaveUnder(CLogicThread* thread, const char* text) {
    msvc6::String name{};
    msvc6::String_AssignChars(&name, nullptr, text, static_cast<std::uint32_t>(std::strlen(text)));
    const auto saved = static_cast<std::uint8_t>(reinterpret_cast<SaveGameFn>(kSaveGame)(thread, nullptr, &name));
    msvc6::String_Destroy(&name, nullptr);
    return saved;
}

}  // namespace

using SaveGameToSlotFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t multiplayer,
                                                    std::int32_t slot);
std::uint32_t __fastcall CLogicThread_SaveGameToSlot(CLogicThread* self, void* edx, std::uint32_t multiplayer,
                                                     std::int32_t slot);

S3RE_HOOK(0x0050ac40, CLogicThread_SaveGameToSlot, SaveGameToSlotFn, Original_CLogicThread_SaveGameToSlot,
          CLogicThread_SaveGameToSlot);

// Purpose: Saves the running game into a numbered savegame slot ("slot 3")
// or, in a multiplayer game, into the shared multiplayer save file.
std::uint32_t __fastcall CLogicThread_SaveGameToSlot(CLogicThread* self, void* /*edx*/, std::uint32_t multiplayer,
                                                     std::int32_t slot) {
    if (static_cast<std::uint8_t>(multiplayer) == 0) {
        char fileName[32];
        reinterpret_cast<SprintfFn>(kSprintf)(fileName, texts::kSlotSaveName.c_str(), slot);
        return SaveUnder(self, fileName);
    }
    std::uint32_t guid[4];
    char* guidText = nullptr;  // (an uninitialized local in the original; UuidToStringA sets it)
    g_importUuidToStringA.get()(reinterpret_cast<GameGuidFn>(kGameGuid)(&g_gameSettings.get(), nullptr, guid),
                                &guidText);
    const std::uint8_t saved = SaveUnder(self, texts::kMultiplayerSaveName.c_str());
    g_importRpcStringFreeA.get()(&guidText);
    return saved;
}

}  // namespace s3re::game
