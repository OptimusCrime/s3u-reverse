// CLogicThread::ProcessEvents (0x00524fa0), part: a key press (event type
// 1, 0x52547c..0x5268d5).
//
// The key is a Windows virtual key code (signed byte at event +4). With
// the cheat input on (g_cheatMode, after typing O I U J K) only F7 (ends
// it) and F8 (single player: Shift+F8 wins the campaign mission; F8 alone
// picks the campaign from the map name "Single??j?", "...e?", "...r?",
// "...Z??", "...G??" and announces it) count. Otherwise the switch
// (jump table 0x534c00, index 0x534cbc):
//   Enter      multiplayer: open the chat input with the player's name as
//              the prompt, or send the typed line (a type 6 message to the
//              players whose chat flag is set) and close it
//   Tab        drop the settlers at their start health from the current group
//   1..9       select settler group N (Ctrl: store the selection as group N)
//   Space      jump to the oldest notification of the first non-empty list
//   Home       centre the camera on the player's start position
//   arrows     scroll by 2 * g_scrollSpeed
//   Q W E R    build menu pages 1..4;  A S D F  statistics pages 1..4;
//   Y/Z X C V  settler menu pages 1..4; G  the objectives menu (11)
//   B (Alt)    show the build costs;  T  toggle g_victoryProtectActive
//   P          single player: pause (menu 9) / resume
//   numpad + - the volume (+0x664) in steps of 20 within -10000..0
//   F1 F2 F3   switch the display mode (through the draw thread)
//   F5 F6      chat to everybody / to the team (F5 only when the frame's
//              slot158 is set, a local of the players menu: a quirk kept)
//   F12        single player: fast-forward 882 steps
//   O I U J K  the cheat sequence; any other key resets it

#include "game/logic/events/process_events.h"

#include "game/globals.h"
#include "game/settlers/settler_groups.h"
#include "game/tables.h"
#include "game/texts.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"

#include <windows.h>

#include <cstring>

namespace s3re::game::events {

namespace {

using msvc6::String;
using msvc6::WString;

// Virtual key codes.
constexpr std::int32_t kKeyTab = 0x09;
constexpr std::int32_t kKeyEnter = 0x0d;
constexpr std::int32_t kKeySpace = 0x20;
constexpr std::int32_t kKeyHome = 0x24;
constexpr std::int32_t kKeyLeft = 0x25;
constexpr std::int32_t kKeyUp = 0x26;
constexpr std::int32_t kKeyRight = 0x27;
constexpr std::int32_t kKeyDown = 0x28;
constexpr std::int32_t kKeyAdd = 0x6b;       // numpad +
constexpr std::int32_t kKeySubtract = 0x6d;  // numpad -
constexpr std::int32_t kKeyF1 = 0x70;
constexpr std::int32_t kKeyF2 = 0x71;
constexpr std::int32_t kKeyF3 = 0x72;
constexpr std::int32_t kKeyF5 = 0x74;
constexpr std::int32_t kKeyF6 = 0x75;
constexpr std::int32_t kKeyF7 = 0x76;
constexpr std::int32_t kKeyF8 = 0x77;
constexpr std::int32_t kKeyF12 = 0x7b;

constexpr std::int32_t kMenuBuild = 1;
constexpr std::int32_t kMenuStatistics = 2;
constexpr std::int32_t kMenuSettlers = 3;
constexpr std::int32_t kMenuMessages = 8;
constexpr std::int32_t kMenuPaused = 9;
constexpr std::int32_t kMenuObjectives = 11;
constexpr std::int32_t kMinVolume = -10000;
constexpr std::int32_t kVolumeStep = 20;
constexpr std::uint32_t kFastForwardSteps = 882;
constexpr std::int32_t kCheatOn = 9;           // g_cheatMode after the sequence
constexpr std::uint32_t kLastCampaignMission = 72;
constexpr std::uint8_t kChatMessageType = 6;   // NetMessage::InGameChat
constexpr std::uint32_t kLineReleaseSemaphore = 299;  // Win32\synchronize.ipp
constexpr std::uint32_t kWholeString = 0xffffffff;

// Callees by address.
constexpr std::uintptr_t kSettlerStartHealth = 0x00510470;  // CGameData (settler) ret 4
constexpr std::uintptr_t kSetDisplayMode = 0x0049ded0;       // Net_sub_49ded0 on g_config (mode - 1) ret 4
constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kPlayerNameString = 0x00435390;     // GameSetup_sub_435390 (slot, out) on g_gameSettings
constexpr std::uintptr_t kLobbyName = 0x005db260;            // Lobby_sub_5db260, ECX = that string
constexpr std::uintptr_t kNarrowToWide = 0x0060b840;         // (ECX = out wstring, EDX = text)
constexpr std::uintptr_t kConcatWcs = 0x00534e50;            // cdecl (out, wstring, wchar text)
constexpr std::uintptr_t kWStringAssignSubstr = 0x00428400;  // (src, pos, n)
constexpr std::uintptr_t kWStringTidy = 0x004286d0;          // (built)
constexpr std::uintptr_t kWStringCopy = 0x00534d30;          // (src)
constexpr std::uintptr_t kWStringAssignChars = 0x004a0f50;   // (text, n)
constexpr std::uintptr_t kStringErase = 0x00407b10;          // (pos, n)
constexpr std::uintptr_t kStringTidy = 0x00402170;           // (built)
constexpr std::uintptr_t kStringDefault = 0x00407880;        // (allocator)
constexpr std::uintptr_t kStringFromCstr = 0x00401a10;       // (text, n)
constexpr std::uintptr_t kStringCstr = 0x00407a50;           // (text, allocator)
constexpr std::uintptr_t kStringSubstr = 0x004078a0;         // (src, pos, n, allocator)
constexpr std::uintptr_t kStringAssignSubstr = 0x00401d60;   // (src, pos, n)
constexpr std::uintptr_t kStringAt = 0x004142d0;             // operator[](pos) -> char*
constexpr std::uintptr_t kMapName = 0x00436a60;              // GameSettings::MapName(out)
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kWcscpy = 0x00621582;
constexpr std::uintptr_t kWcslen = 0x0061fe1b;
constexpr std::uintptr_t kDebugLog = 0x00428720;

std::uint16_t* GroupWords() {
    return reinterpret_cast<std::uint16_t*>(&g_settlerGroups.get());
}

}  // namespace

std::uint16_t& GroupCount(std::int32_t group) {
    return GroupWords()[group];  // group -1 reads before the table, as the original does
}

std::uint16_t& GroupEntry(std::int32_t group, std::int32_t index) {
    return GroupWords()[static_cast<std::int32_t>(SettlerGroups::kGroups) +
                        group * static_cast<std::int32_t>(SettlerGroups::kWords) + index];
}

void ShowWideMessage(Run& run, const msvc6::WString& text) {
    const std::uint32_t colour = g_playerColourIndex[run.world()->local_player];
    using Fn = void(__fastcall*)(CLogicThread*, void*, WString, std::uint32_t);
    reinterpret_cast<Fn>(callee::kShowMessage)(run.thread, nullptr, text, colour);
}

namespace {

// 0x525788: Tab: the settlers of the current group that are at their
// start health leave it.
void DropHealthySettlers(Run& run) {
    if (GroupCount(g_currentSettlerGroup) == 0) {
        return;
    }
    for (std::int32_t i = 0; i < GroupCount(g_currentSettlerGroup); ++i) {
        const std::uint16_t settler = GroupEntry(g_currentSettlerGroup, i);
        const auto health = static_cast<std::uint16_t>(Call<kSettlerStartHealth>(run.world(), settler));
        const std::int32_t group = g_currentSettlerGroup;
        if (run.world()->settlers[settler].health != health) {
            continue;
        }
        g_settlerGroupChanged[group] = 1;
        const std::int32_t last = GroupCount(group) - 1;
        std::int32_t cleared = i;
        for (std::int32_t k = i; k < last; ++k) {
            GroupEntry(group, k) = GroupEntry(group, k + 1);
            cleared = last;
        }
        GroupEntry(group, cleared) = 0;
        --GroupCount(group);
        --i;
    }
}

// 0x5254c0 / 0x5255c2: Enter in multiplayer.
void ChatEnter(Run& run) {
    CLogicThread* thread = run.thread;
    auto* chat = reinterpret_cast<WString*>(thread->chat_text);
    if (thread->chat_open == 0) {
        // Open: the prompt is "<player name>: ".
        thread->chat_open = 1;
        String name{};
        const std::uint32_t nameString = CallOn<kPlayerNameString>(&g_gameSettings, thread->setup_3b8, U(&name));
        const std::uint32_t lobbyName = CallOn<kLobbyName>(reinterpret_cast<void*>(nameString));
        WString wide{};
        using WidenFn = WString*(__fastcall*)(WString* out, std::uint32_t text);
        WString* widened = reinterpret_cast<WidenFn>(kNarrowToWide)(&wide, lobbyName);
        WString prompt{};
        using ConcatFn = WString*(__cdecl*)(WString*, WString*, const void*);
        WString* joined = reinterpret_cast<ConcatFn>(kConcatWcs)(&prompt, widened, &texts::kChatPromptSuffix.get());
        CallOn<kWStringAssignSubstr>(chat, U(joined), 0, g_wstringNpos.get());
        CallOn<kWStringTidy>(&prompt, 1);
        CallOn<kWStringTidy>(&wide, 1);
        CallOn<kStringErase>(&name, 0, g_stringNpos.get());
        CallOn<kStringTidy>(&name, 1);
        thread->chat_prompt_length = chat->len;
        thread->chat_dirty = 1;
        return;
    }
    if (chat->len > thread->chat_prompt_length) {
        // 0x5255d6: send the line: {6, 0, size, 0, slot, length, ?, ?,
        // recipients mask, the wide text}; size = (length + 7) * 2 bytes
        // (in a byte) rounded up to dwords.
        std::uint8_t* message = run.frame->chat_message;
        message[0] = kChatMessageType;
        message[1] = 0;
        message[3] = 0;
        const auto size = static_cast<std::uint8_t>((chat->len + 7) * 2);
        message[2] = static_cast<std::uint8_t>(((size + 3) / 4) << 2);
        message[4] = thread->setup_3b8;
        std::uint32_t recipients = 0;
        for (const auto& player : run.world()->players) {
            if (player.chat_recipient != 0) {
                recipients |= 1u << (static_cast<std::uint32_t>(player.net_slot) & 31);
            }
        }
        std::memcpy(message + 8, &recipients, 4);
        message[5] = static_cast<std::uint8_t>(chat->len);
        const void* text = chat->ptr != nullptr ? static_cast<const void*>(chat->ptr) : &g_wstringNullBuffer.get();
        using CopyFn = void*(__cdecl*)(void*, const void*);
        reinterpret_cast<CopyFn>(kWcscpy)(message + 12, text);
        CallOn<callee::kPushMessage>(thread->toNet, U(message), static_cast<std::uint32_t>(message[2] >> 2), 1);
        // 0x5256bd: show it here too.
        WString copy;
        CallOn<kWStringCopy>(&copy, U(chat));
        ShowWideMessage(run, copy);
    }
    // 0x5256ea: close and empty the input.
    thread->chat_open = 0;
    using LengthFn = std::uint32_t(__cdecl*)(const void*);
    const std::uint32_t length = reinterpret_cast<LengthFn>(kWcslen)(&g_setupWideText.get());
    CallOn<kWStringAssignChars>(chat, U(&g_setupWideText.get()), length);
    thread->chat_dirty = 1;
}

// 0x525a77..0x525cbb: F1..F3: switch the display mode (the draw thread
// waits on the semaphore g_graphicsDataLock).
void SwitchDisplayMode(std::int32_t mode) {
    if (g_logicStartState != 0 || g_displayMode == mode) {
        return;
    }
    CallOn<kSetDisplayMode>(&g_config.get(), mode - 1);
    const auto* semaphore = static_cast<const std::uint8_t*>(g_graphicsDataLock.get());
    HANDLE handle;
    std::memcpy(&handle, semaphore + 8, sizeof(handle));
    if (ReleaseSemaphore(handle, 1, nullptr) == 0) {
        String message;
        CallOn<kStringFromCstr>(&message, U(texts::kEmpty.c_str()), kWholeString);
        const std::uint32_t error = g_importGetLastError.get()();
        using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t, std::uint32_t, const String*);
        reinterpret_cast<ThrowFn>(kThrowWin32Error)(texts::kVerifyReleaseSemaphore, texts::kSynchronizeIpp,
                                                     kLineReleaseSemaphore, error, &message);
    }
    g_logicStartState = static_cast<std::uint32_t>(mode);
}

void ScrollBy(Run& run, std::int32_t dx, std::int32_t dy) {
    CGameData* world = run.world();
    world->camera_x += dx;
    world->camera_y += dy;
    Call<callee::kClampCamera>(world);
    run.world()->redraw = 0;
}

void OpenPage(Run& run, std::int32_t menu, std::int32_t* page, std::int32_t value) {
    run.world()->ui_selection_mode = menu;
    *page = value;
    RefreshUi(run);
}

// 0x525fe2: Space: the first list with a notification.
void JumpToNotification(Run& run) {
    for (std::int32_t list = 0; list < 4; ++list) {
        if (run.world()->notifications[list][0].type == 0) {
            continue;
        }
        Feedback(run);
        run.world()->ui_selection_mode = kMenuMessages;
        Call<callee::kGoToNotification>(run.world(), list + 1);
        RefreshUi(run);
        CallThread<callee::kClearSelection>(run.thread);
        return;
    }
}

// 0x526171 / 0x5261ae: the chat recipients.
void ChatToEverybody(Run& run) {
    if (run.frame->slot158 == 0) {
        return;
    }
    for (auto& player : run.world()->players) {
        if (player.active != 0) {
            player.chat_recipient = 1;
        }
    }
}

void ChatToTeam(Run& run) {
    for (auto& player : run.world()->players) {
        CGameData* world = run.world();
        player.chat_recipient = player.active != 0 && player.team == world->players[world->local_player].team ? 1 : 0;
    }
}

// The campaign chosen by the cheat (0x5265e8 / 0x526745): the
// announcement on the screen and in the debug log.
void AnnounceCampaign(Run& run, const char* format, const char* logFormat, std::int32_t first) {
    char* buffer = run.frame->text;
    using SprintfFn = int(__cdecl*)(char*, const char*, ...);
    reinterpret_cast<SprintfFn>(kSprintf)(buffer, format, g_campaignMission - first, g_campaignMission.get());
    std::uint8_t allocators[3] = {};  // allocator bytes of the temporaries (stack leftovers in the original)
    String text;
    CallOn<kStringDefault>(&text, U(&allocators[0]));
    String fromBuffer;
    const std::uint32_t built = CallOn<kStringCstr>(&fromBuffer, U(buffer), U(&allocators[1]));
    String copy;
    const std::uint32_t substr = CallOn<kStringSubstr>(&copy, built, 0, kWholeString, U(&allocators[2]));
    CallOn<kStringAssignSubstr>(&text, substr, 0, g_stringNpos.get());
    CallOn<kStringTidy>(&copy, 1);
    CallOn<kStringTidy>(&fromBuffer, 1);
    WString wide;
    using WidenFn = WString*(__fastcall*)(WString* out, const String* text);
    reinterpret_cast<WidenFn>(kNarrowToWide)(&wide, &text);
    ShowWideMessage(run, wide);
    CallOn<kStringErase>(&text, 0, g_stringNpos.get());
    CallOn<kStringTidy>(&text, 1);
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLog)(logFormat, g_campaignMission - 0x30, g_campaignMission.get());
}

// 0x5262f9: F8 without Shift: the campaign from the map name.
void CheatCampaignFromMapName(Run& run) {
    std::uint8_t allocators[2] = {};
    String name;
    CallOn<kStringDefault>(&name, U(&allocators[0]));
    String map;
    const std::uint32_t mapName = CallOn<kMapName>(&g_gameSettings, U(&map));
    CallOn<kStringAssignSubstr>(&name, mapName, 0, g_stringNpos.get());
    CallOn<kStringErase>(&map, 0, g_stringNpos.get());
    CallOn<kStringTidy>(&map, 1);
    auto at = [&](std::uint32_t i) { return *reinterpret_cast<const char*>(CallOn<kStringAt>(&name, i)); };
    // 0x526378: "Single" with digits at 8 and 9 (each digit is read twice, as
    // the original does).
    constexpr char kSingle[] = "Single";
    bool matches = name.len >= 13;
    for (std::uint32_t i = 0; matches && i < 6; ++i) {
        matches = at(i) == kSingle[i];
    }
    matches = matches && at(8) >= '0' && at(8) <= '9' && at(9) >= '0' && at(9) <= '9';
    if (matches) {
        auto oneDigit = [&](std::int32_t base) {
            g_campaignMission = base;
            const char digit = at(8);
            g_campaignMission = g_campaignMission + digit - '0';
        };
        auto twoDigits = [&](std::int32_t base) {
            g_campaignMission = base;
            const char ones = at(9);
            const char tens = at(8);
            g_campaignMission = ones + g_campaignMission + tens * 10 - 0x210;  // - '0' * 11
        };
        if (at(7) == 'j') {
            oneDigit(0x28);
        }
        if (at(7) == 'e') {
            oneDigit(0x20);
        }
        if (at(7) == 'r') {
            oneDigit(0x18);
        }
        if (at(7) == 'Z') {
            twoDigits(0x30);
        }
        if (at(7) == 'G') {
            twoDigits(0x3c);
        }
        const auto mission = static_cast<std::uint32_t>(g_campaignMission.get());
        if (mission >= 0x31 && mission <= 0x3c) {
            AnnounceCampaign(run, texts::kCampaignAmazonsVsOthers, texts::kCampaignAmazonsVsOthersLog, 0x30);
        } else if (mission >= 0x3d && mission <= 0x48) {
            AnnounceCampaign(run, texts::kCampaignOthersVsAmazons, texts::kCampaignOthersVsAmazonsLog, 0x3c);
        }
    }
    CallOn<kStringErase>(&name, 0, g_stringNpos.get());
    CallOn<kStringTidy>(&name, 1);
}

// 0x5262a7: the cheat input.
void CheatKey(Run& run, std::int32_t key) {
    if (key == kKeyF7) {
        g_cheatMode = 0;
        return;
    }
    if (key != kKeyF8 || IsMultiplayer()) {
        return;
    }
    if (run.event().modifiers == kModShift) {
        const auto mission = static_cast<std::uint32_t>(g_campaignMission.get());
        if (mission >= 1 && mission <= kLastCampaignMission) {
            g_gameOutcome = 1;  // won
        }
        return;
    }
    CheatCampaignFromMapName(run);
}

void TogglePause(Run& run) {
    if (IsMultiplayer()) {
        return;
    }
    CLogicThread* thread = run.thread;
    if (thread->paused == 0) {
        thread->paused = 1;
        run.world()->ui_selection_mode = kMenuPaused;
    } else {
        thread->paused = 0;
        run.world()->ui_selection_mode = 0;
    }
    RefreshUi(run);
}

}  // namespace

void KeyDown(Run& run) {
    CLogicThread* thread = run.thread;
    const std::int32_t key = run.event().key;
    if (g_cheatMode != 0) {
        CheatKey(run, key);
        return;
    }
    CGameData* world = run.world();
    const std::int32_t step = g_scrollSpeed * 2;
    switch (key) {
    case kKeyEnter:
        if (IsMultiplayer()) {
            ChatEnter(run);
        }
        break;
    case 'P': TogglePause(run); break;
    case kKeyTab: DropHealthySettlers(run); break;
    case kKeyUp: ScrollBy(run, 0, -step); break;
    case kKeyDown: ScrollBy(run, 0, step); break;
    case kKeyLeft: ScrollBy(run, -step, 0); break;
    case kKeyRight: ScrollBy(run, step, 0); break;
    case kKeyHome: Call<callee::kCenterCameraOnLocalPlayer>(world); break;
    case kKeyAdd: {
        const std::int32_t volume = thread->volume + kVolumeStep;
        thread->volume = volume >= 0 ? 0 : volume;
        break;
    }
    case kKeySubtract: {
        const std::int32_t volume = thread->volume - kVolumeStep;
        thread->volume = volume > kMinVolume ? volume : kMinVolume;
        break;
    }
    case kKeyF1: SwitchDisplayMode(1); break;
    case kKeyF2: SwitchDisplayMode(2); break;
    case kKeyF3: SwitchDisplayMode(3); break;
    case kKeyF12:
        if (!IsMultiplayer()) {
            g_stepCountdown = g_stepCountdown + kFastForwardSteps;
        }
        break;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        if ((run.event().modifiers & kModCtrl) != 0) {
            CallThread<callee::kStoreSettlerGroup>(thread, key - '0');
        } else {
            CallThread<callee::kSelectSettlerGroup>(thread, key - '0');
        }
        break;
    case 'Q': OpenPage(run, kMenuBuild, &world->ui_selection_param1, 1); break;
    case 'W': OpenPage(run, kMenuBuild, &world->ui_selection_param1, 2); break;
    case 'E': OpenPage(run, kMenuBuild, &world->ui_selection_param1, 3); break;
    case 'R': OpenPage(run, kMenuBuild, &world->ui_selection_param1, 4); break;
    case 'A': OpenPage(run, kMenuStatistics, &world->ui_2d290[0], 1); break;
    case 'S': OpenPage(run, kMenuStatistics, &world->ui_2d290[0], 2); break;
    case 'D': OpenPage(run, kMenuStatistics, &world->ui_2d290[0], 3); break;
    case 'F': OpenPage(run, kMenuStatistics, &world->ui_2d290[0], 4); break;
    case 'Y':
    case 'Z': OpenPage(run, kMenuSettlers, &world->ui_2d2a0, 1); break;
    case 'X': OpenPage(run, kMenuSettlers, &world->ui_2d2a0, 2); break;
    case 'C': OpenPage(run, kMenuSettlers, &world->ui_2d2a0, 3); break;
    case 'V': OpenPage(run, kMenuSettlers, &world->ui_2d2a0, 4); break;
    case kKeySpace: JumpToNotification(run); break;
    case 'T': g_victoryProtectActive = g_victoryProtectActive == 0 ? 1 : 0; break;
    case 'G':
        if (world->objectives_enabled != 0) {
            Feedback(run);
            run.world()->ui_selection_mode = kMenuObjectives;
            RefreshUi(run);
            CallThread<callee::kClearSelection>(thread);
        }
        break;
    case 'B':
        if ((run.event().modifiers & kModAlt) != 0) {
            g_showBuildCosts = g_showBuildCosts == 0 ? 1 : 0;
            RefreshUi(run);
        }
        break;
    case kKeyF5: ChatToEverybody(run); break;
    case kKeyF6: ChatToTeam(run); break;
    // 0x526203..0x526297: the cheat sequence O I U J K.
    case 'O': g_cheatKeys = 1; break;
    case 'I':
        if (g_cheatKeys == 1) {
            g_cheatKeys = 2;
        }
        break;
    case 'U':
        if (g_cheatKeys == 2) {
            g_cheatKeys = 3;
        }
        break;
    case 'J':
        if (g_cheatKeys == 3) {
            g_cheatKeys = 4;
        }
        break;
    case 'K':
        if (g_cheatKeys == 4) {
            g_cheatKeys = 5;
            g_cheatMode = kCheatOn;
        } else if (g_cheatKeys == 5) {
            g_cheatMode = kCheatOn;
        }
        break;
    default: g_cheatKeys = 0; break;
    }
}

}  // namespace s3re::game::events
