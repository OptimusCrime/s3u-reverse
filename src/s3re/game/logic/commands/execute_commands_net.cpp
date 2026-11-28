// Part of CLogicThread::ExecuteCommands (0x535990), see execute_commands.h:
// DestroyBuilding (0x06) and the multiplayer / UI commands and network
// messages. Most of them only show texts (through the original UI helpers,
// called by address) or drive the multiplayer save; single player sends
// 0xfb at the start and 0xfc / 0xfd at the end of a game.
//
// Texts: CGraphicsData::TextAddress (0x5faaa0) gives a text of the
// graphics data (ids 0x8xxxx), sprintf (0x61f27e) formats it into a 2 KB
// buffer, std::string (0x401a10) holds it, 0x60b840 turns it into a wide
// string and 0x51e440 (thiscall on the thread: wide string by value, colour)
// shows it in the message list of the UI. The wide string's allocator byte
// is a leftover of 0x60b840's own frame (or of our frame for the texts
// built by 0x53ba60); 0x51e440 doesn't keep it, so it doesn't matter.

#include "game/logic/commands/execute_commands.h"

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "msvc6/string.h"
#include "game/enums/buildings.h"
#include "game/enums/commands.h"

#include <cstring>

namespace s3re::game::commands {

namespace {

using layout::Building;
using layout::Player;
using msvc6::String;

// std::wstring of MSVC 6 (allocator byte, pointer, length, capacity), passed by value.
struct WideString {
    std::uint32_t words[4];
};
static_assert(sizeof(WideString) == 16);

constexpr std::uintptr_t kCrushBuilding = 0x004d9250;
constexpr std::uintptr_t kTextAddress = 0x005faaa0;  // CGraphicsData: text by id
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kStringFromChars = 0x00401a10;
constexpr std::uintptr_t kStringDestroy = 0x00401b60;
constexpr std::uintptr_t kWiden = 0x0060b840;           // fastcall (wide string out, narrow string)
constexpr std::uintptr_t kWideFromText = 0x0053ba60;    // thiscall (wide text, allocator*)
constexpr std::uintptr_t kShowMessage = 0x0051e440;     // thiscall on the thread (wide string, colour)
constexpr std::uintptr_t kPlayerSetup = 0x004351e0;     // thiscall on g_gameSettings (player)
constexpr std::uintptr_t kPlayerName = 0x005db260;      // thiscall (std::string* out) -> out
constexpr std::uintptr_t kCStr = 0x00411fc0;            // std::string::c_str
constexpr std::uintptr_t kGameOverFor = 0x00540500;     // thiscall on the thread (0)
constexpr std::uintptr_t kUpdatePlayers = 0x005411b0;   // thiscall on the thread
constexpr std::uintptr_t kNewComputerPlayer = 0x00550900;  // CComputerPlayer::CComputerPlayer
constexpr std::uintptr_t kOperatorNew = 0x0061efb9;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // fastcall (text, file, line)
constexpr std::uintptr_t kSyncDump = 0x004ff6d0;        // thiscall on the world (start, end, kind)
constexpr std::uintptr_t kSaveStart = 0x0050ac40;       // thiscall on the thread (1, 0) -> bool
constexpr std::uintptr_t kSaveCommit = 0x0050af70;      // thiscall on the thread -> bool
constexpr std::uintptr_t kSaveRollBack = 0x0050b3c0;    // thiscall on the thread -> bool
constexpr std::uintptr_t kGetApp = 0x0053ba50;          // returns g_app
constexpr std::uintptr_t kPostAppMessage = 0x0060f480;  // thiscall on app + 0xc (message, 0, 0)

// Text ids of the graphics data.
constexpr std::uint32_t kTextCannotDestroy = 0x8018e;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextPlayerLost = 0x80114;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextPlayerDropped = 0x80116;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextPlayerLeft = 0x80117;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextSaveAllPlayersDone = 0x80125;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextSaveFailedFor = 0x80126;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextSaveRequested = 0x80127;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextSaveFailed = 0x80128;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextDesync = 0x80129;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextChecksumFailure = 0x8012a;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextLagging = 0x80156;  // raw-ok: text id, not an offset
constexpr std::uint32_t kTextYouLag = 0x8015d;  // raw-ok: text id, not an offset
constexpr std::uint32_t kCurrentLanguage = 0xffffffff;

constexpr std::uint32_t kPlayers = 20;
constexpr std::int32_t kPlayerActive = 1;
constexpr std::int32_t kPlayerDropped = 2;
constexpr std::uint32_t kWholeString = 0xffffffff;  // std::string(text, npos)
constexpr std::uint32_t kComputerPlayerBytes = 0x780;
constexpr std::uint32_t kLagLimit = 5000;            // ms, probably (0x22)
constexpr std::uint32_t kSaveRequestTicks = 937;     // 0xfa: a new save only this long after the last
constexpr std::uint32_t kQuitMessage = 0x12;         // posted to the application window (FilmEnd)
constexpr std::uint32_t kSyncKinds = 7;
constexpr std::int32_t kPlayerFlagNoTempleMana = 1;  // Player::unk008 bit 0 (building_done.cpp)
// 0xfe / 0xff assertion lines (Volker\LogicHandleNet.cpp).
constexpr std::uint32_t kDropAssertLine = 2448;
constexpr std::uint32_t kLeftAssertLine = 2485;

std::uint32_t ColourOf(std::uint32_t player) {
    return g_playerColourIndex[player];
}
Player& PlayerAt(CGameData* world, std::uint32_t n) {
    return *(world->players + n);
}
Building& BuildingAt(CGameData* world, std::uint32_t n) {
    return *(world->buildings + n);
}

// ------------------------------------------------------------ text helpers

// The text `id` of the graphics data.
const char* GfxText(std::uint32_t id) {
    using Fn = const char*(__fastcall*)(void* gfx, void* edx, std::uint32_t id, std::uint32_t variant);
    return reinterpret_cast<Fn>(kTextAddress)(g_gfxData.get(), nullptr, id, kCurrentLanguage);
}

template <typename... Args>
void Format(char* buffer, const char* format, Args... args) {
    using Fn = int(__cdecl*)(char*, const char*, ...);
    reinterpret_cast<Fn>(kSprintf)(buffer, format, args...);
}

void ShowWide(Run& run, const WideString& text, std::uint32_t colour) {
    using Fn = void(__fastcall*)(CLogicThread*, void*, WideString, std::uint32_t);
    reinterpret_cast<Fn>(kShowMessage)(run.thread, nullptr, text, colour);
}

// Shows the narrow text in `buffer` (std::string, widened) in `colour`.
void ShowBuffer(Run& run, const char* buffer, std::uint32_t colourPlayer) {
    String text;
    using FromCharsFn = String*(__fastcall*)(String*, void*, const char*, std::uint32_t);
    reinterpret_cast<FromCharsFn>(kStringFromChars)(&text, nullptr, buffer, kWholeString);
    const std::uint32_t colour = ColourOf(colourPlayer);
    WideString wide;
    using WidenFn = WideString*(__fastcall*)(WideString*, const String*);
    reinterpret_cast<WidenFn>(kWiden)(&wide, &text);
    ShowWide(run, wide, colour);
    using DestroyFn = void(__fastcall*)(String*, void*);
    reinterpret_cast<DestroyFn>(kStringDestroy)(&text, nullptr);
}

// Shows the text `id` of the graphics data (formatted without arguments).
void ShowGfxText(Run& run, std::uint32_t id, std::uint32_t colourPlayer) {
    char buffer[2048];
    Format(buffer, GfxText(id));
    ShowBuffer(run, buffer, colourPlayer);
}

// Shows the text `id` formatted with the player's name (from the game setup / lobby).
void ShowPlayerText(Run& run, std::uint32_t id, std::uint32_t player, std::uint32_t colourPlayer) {
    using SetupFn = void*(__fastcall*)(void*, void*, std::uint32_t);
    void* setup = reinterpret_cast<SetupFn>(kPlayerSetup)(&g_gameSettings, nullptr, player);
    String name;
    using NameFn = String*(__fastcall*)(void*, void*, String*);
    String* got = reinterpret_cast<NameFn>(kPlayerName)(setup, nullptr, &name);
    using CStrFn = const char*(__fastcall*)(String*, void*);
    const char* nameText = reinterpret_cast<CStrFn>(kCStr)(got, nullptr);
    char buffer[2048];
    Format(buffer, GfxText(id), nameText);
    using DestroyFn = void(__fastcall*)(String*, void*);
    reinterpret_cast<DestroyFn>(kStringDestroy)(&name, nullptr);
    ShowBuffer(run, buffer, colourPlayer);
}

// Shows a wide text of S3.EXE (0x53ba60 builds the string).
void ShowWideText(Run& run, const void* text, std::uint32_t colourPlayer) {
    const std::uint32_t colour = ColourOf(colourPlayer);
    const std::uint8_t allocator = 0;  // the original passes an unrelated frame byte; not kept by 0x51e440
    WideString wide;
    using Fn = WideString*(__fastcall*)(WideString*, void*, const void*, const std::uint8_t*);
    reinterpret_cast<Fn>(kWideFromText)(&wide, nullptr, text, &allocator);
    ShowWide(run, wide, colour);
}

std::uint32_t LocalPlayer(Run& run) {
    return static_cast<std::uint32_t>(run.world()->local_player);
}

// Pushes {0x21, id, 8, ?, player} (2 dwords) to the network.
void PushReply(Run& run, CommandId id, std::uint8_t player) {
    std::uint8_t* reply = run.frame->reply_message;
    reply[0] = Value(NetMessage::CommandRequest);
    reply[1] = Value(id);
    reply[2] = 8;
    reply[4] = player;
    PushToNet(run.thread, reply, 2);
}

// The player whose network id (Player +0x1c) is `id`, or 20.
std::uint32_t PlayerOfNetId(CGameData* world, std::uint32_t id) {
    std::uint32_t player = 0;
    while (player < kPlayers && static_cast<std::uint32_t>(PlayerAt(world, player).net_slot) != id) {
        ++player;
    }
    return player;
}

bool IsMilitaryBuilding(BuildingType type) {
    return type == BuildingType::SmallTower || type == BuildingType::LargeTower || type == BuildingType::Castle;
}
bool IsBigTemple(BuildingType type) {
    return type == BuildingType::LargePagoda || type == BuildingType::LargeTemple || type == BuildingType::Pyramid ||
           type == BuildingType::AmazonLargeTemple;
}

// 0xfe, 0xff: a computer takes over a player that dropped out or left.
// `upperBits`: the original pushes the bool flag as a whole register whose
// upper bytes hold an unrelated value (the constructor reads only the low
// byte); reproduced for the emulator's call comparison.
void HandOverToComputer(Run& run, std::uint32_t player, std::uint32_t upperBits, std::uint32_t assertLine) {
    CGameData* world = run.world();
    Player& p = PlayerAt(world, player);
    if (p.active != kPlayerActive) {
        return;
    }
    GameDebugLog(texts::kDroppedPlayer, player, static_cast<std::uint32_t>(p.net_slot));
    PlayerAt(run.world(), player).active = kPlayerDropped;
    if (run.world()->objectives_enabled != 0) {
        return;
    }
    using NewFn = void*(__cdecl*)(std::uint32_t);
    void* block = reinterpret_cast<NewFn>(kOperatorNew)(kComputerPlayerBytes);
    std::uint32_t computer = 0;
    if (block != nullptr) {
        world = run.world();
        const std::uint32_t firstComputer = world->flag15394b0 == 0 ? 1 : 0;
        using CtorFn = std::uint32_t(__fastcall*)(void*, void*, CGameData*, MessageQueue*, std::uint32_t, std::uint32_t);
        computer = reinterpret_cast<CtorFn>(kNewComputerPlayer)(block, nullptr, world, run.thread->toNet, player,
                                                                (upperBits & ~0xffu) | firstComputer);
    }
    run.thread->computer_players[player] = computer;
    GameDebugLog(texts::kNewComputer, player, g_gameTick.get());
    if (run.thread->computer_players[player] == 0) {
        using AssertFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
        reinterpret_cast<AssertFn>(kThrowOutOfMemory)(texts::kComputerAssert, texts::kLogicHandleNetFile, assertLine);
    }
    run.world()->flag15394b0 = 1;
}

}  // namespace

// ------------------------------------------------------------ 0x06 DestroyBuilding (0x53729c)
// Payload: building (word), player. A finished big temple marks the player
// (Player +0x08 bit 0: no more temple mana); CrushBuilding; if that fails
// for the local player's building, a message. The selection is cleared.
void DestroyBuilding(Run& run) {
    const std::uint32_t number = run.Word(0);
    const std::uint32_t player = run.Byte(2);
    CGameData* world = run.world();
    const Building& building = BuildingAt(world, number);
    if (building.owner != player) {
        return;
    }
    if (IsBigTemple(TypeOf(building)) && building.buildup == 0) {
        PlayerAt(world, player).unk008 |= kPlayerFlagNoTempleMana;
    }
    if (Call<kCrushBuilding>(run.world(), number) == 0) {
        world = run.world();
        if (BuildingAt(world, number).owner == static_cast<std::uint32_t>(world->local_player)) {
            ShowGfxText(run, kTextCannotDestroy, LocalPlayer(run));  // 0x537353
        }
    }
    if (number == static_cast<std::uint32_t>(g_selectedBuilding)) {
        g_selectedBuilding = 0;
    }
}

// ------------------------------------------------------------ 0xf2 ClearPlayerFlag (0x53a1af)
void ClearPlayerFlag(Run& run) {
    run.thread->player_flags[run.Byte(0)] = 0;
    CallThread<kUpdatePlayers>(run.thread);
}

// ------------------------------------------------------------ 0xf4 SyncCheck (0x53a1ce)
// Payload: player, a byte (logged), seven checksums and the step stamp (big-
// endian dwords at payload 2..29 and 30). Checksums of a newer stamp (or
// stamp 0) are stored; for the same stamp they are compared, and each
// mismatch (unless the simulation is already paused) logs, shows "DESYNC
// ..." and dumps that part of the world; any mismatch pauses the simulation.
namespace {

std::uint32_t BigEndian32(Run& run, std::size_t at) {
    return run.Word(at) << 16 | run.Word(at + 2);
}

void ReportDesync(Run& run, std::uint32_t kind) {
    struct Part {
        const GameText* wide;
        const GameText* log;
    };
    static const Part kParts[kSyncKinds] = {
        {&texts::kWideDesyncWorld, &texts::kDesyncWorld},     {&texts::kWideDesyncEconomy, &texts::kDesyncEconomy},
        {&texts::kWideDesyncTrade, &texts::kDesyncTrade},     {nullptr, nullptr},
        {&texts::kWideDesyncPile, &texts::kDesyncPile},       {&texts::kWideDesyncSettler, &texts::kDesyncSettler},
        {&texts::kWideDesyncBuildings, &texts::kDesyncBuildings},
    };
    const Part& part = kParts[kind];
    if (part.wide == nullptr) {
        return;  // kind 3: no check behind it (jump table 0x53ba34)
    }
    ShowWideText(run, part.wide->c_str(), LocalPlayer(run));
    GameDebugLog(*part.log);
    // The records of that part, from the first to one past the high water mark.
    CGameData* world = run.world();
    const void* start = nullptr;
    const void* end = nullptr;
    switch (kind) {
    case 0:  // the tile rows of the map (0x4830 bytes per row as the original computes it)
        start = &world->tiles[0][0];
        end = reinterpret_cast<const std::uint8_t*>(&world->tiles[0][0]) + world->map_size * 0x4830;
        break;
    case 1: start = world->areas; end = world->areas + world->area_high_water + 1; break;
    case 2: start = world->trades; end = world->trades + world->trade_high_water + 1; break;
    case 4: start = world->piles; end = world->piles + world->pile_high_water + 1; break;
    case 5: start = world->settlers; end = world->settlers + world->settler_high_water + 1; break;
    default: start = world->buildings; end = world->buildings + world->building_high_water + 1; break;
    }
    using DumpFn = void(__fastcall*)(CGameData*, void*, const void*, const void*, std::uint32_t);
    reinterpret_cast<DumpFn>(kSyncDump)(world, nullptr, start, end, kind);
}

}  // namespace

void SyncCheck(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t stamp = BigEndian32(run, 30);
    std::uint32_t* checks = *(&g_syncChecksums[0]) + player * 8;
    std::uint32_t& checkStamp = checks[7];
    std::uint32_t mismatches = 0;
    for (std::uint32_t kind = 0; kind < kSyncKinds; ++kind) {
        const std::uint32_t checksum = BigEndian32(run, 2 + 4 * kind);
        if (stamp == checkStamp && stamp != 0) {
            if (checks[kind] != checksum && g_simulationPaused == 0) {
                GameDebugLog(texts::kCheckSumPlayer, static_cast<std::uint32_t>(run.Byte(1)), checksum, player, kind,
                             checks[kind]);
                ++mismatches;
                ReportDesync(run, kind);
            }
        } else if (stamp > checkStamp || stamp == 0) {
            checks[kind] = checksum;
        }
    }
    if (stamp > checkStamp || stamp == 0) {
        checkStamp = stamp;
    }
    if (static_cast<std::uint16_t>(mismatches) != 0) {
        ++g_simulationPaused.get();
    }
}

// ------------------------------------------------------------ 0xf7..0xfa: the multiplayer save
// 0xf7 (0x53a735): start the save; tell the others whether it worked.
void SaveRequest(Run& run) {
    run.thread->save_acks = 0;
    run.thread->save_failures = 0;
    GameDebugLog(texts::kStartingMultiplayerSave);
    if (static_cast<std::uint8_t>(CallThread<kSaveStart>(run.thread, 1, 0)) != 0) {
        run.thread->save_request_tick = g_gameTick;
        PushReply(run, CommandId::SaveDone, static_cast<std::uint8_t>(run.world()->local_player));
        return;
    }
    PushReply(run, CommandId::SaveFailed, static_cast<std::uint8_t>(run.world()->local_player));
    ShowGfxText(run, kTextSaveFailed, LocalPlayer(run));
}

// 0xf8 (0x53a97e): once every active player has saved, commit.
void SaveDone(Run& run) {
    const std::uint32_t acks = ++run.thread->save_acks;
    std::uint32_t active = 0;
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        if (PlayerAt(run.world(), p).active == kPlayerActive) {
            ++active;
        }
    }
    if (acks != active) {
        return;
    }
    char buffer[2048];
    Format(buffer, GfxText(kTextSaveAllPlayersDone));
    GameDebugLog(texts::kMultiplayerSaveOk);
    if (static_cast<std::uint8_t>(CallThread<kSaveCommit>(run.thread)) == 0) {
        ShowWideText(run, texts::kWideSaveError.c_str(), LocalPlayer(run));
        return;
    }
    ShowBuffer(run, buffer, LocalPlayer(run));
    const std::uint32_t tick = g_gameTick;
    g_global7deb50 = 0;
    g_lastSaveTick = tick;
}

// 0xf9 (0x53aaa1): a player's save failed: roll back (once).
void SaveFailed(Run& run) {
    ++run.thread->save_failures;
    const std::uint32_t player = run.Byte(0);
    if (player != LocalPlayer(run)) {
        ShowGfxText(run, kTextSaveFailedFor, player);
    }
    if (run.thread->save_failures != 1) {
        return;
    }
    run.thread->save_request_tick = 0;
    GameDebugLog(texts::kMultiplayerSaveFailed);
    if (static_cast<std::uint8_t>(CallThread<kSaveRollBack>(run.thread)) == 0) {
        ShowWideText(run, texts::kWideCannotDeleteTemp.c_str(), LocalPlayer(run));
    }
}

// 0xfa (0x53abb3): a player asks for a save; the asking machine sends 0xf7.
void SaveAllowed(Run& run) {
    const std::uint32_t last = run.thread->save_request_tick;
    if (g_gameTick - last <= kSaveRequestTicks && last != 0) {
        return;
    }
    const std::uint32_t player = run.Byte(0);
    if (player == LocalPlayer(run)) {
        PushReply(run, CommandId::SaveRequest, static_cast<std::uint8_t>(run.world()->local_player));
    }
    ShowGfxText(run, kTextSaveRequested, player);
}

// ------------------------------------------------------------ 0xfb SyncRandomCheck (0x53aca5)
// Payload: index, word (big-endian words), player. Another player's RNG
// history word (g_syncRandomHistory, CLogicThread::Step) that differs
// from ours: "Desync!".
void SyncRandomCheck(Run& run) {
    const std::uint32_t index = run.Word(0);
    const std::uint32_t word = run.Word(2);
    const std::uint32_t player = run.Byte(4);
    if (player == LocalPlayer(run) || g_syncRandomHistory[index] == word) {
        return;
    }
    ShowGfxText(run, kTextDesync, player);
    GameDebugLog(texts::kDesync);
    run.thread->desync_detected = 1;
}

// ------------------------------------------------------------ 0xfc PlayerLost (0x53ad95), 0xfd PlayerWon
// Payload: player. Another player's defeat is shown; without objectives (or
// before the counters stop) the player's military buildings are crushed.
void PlayerLost(Run& run) {
    const std::uint32_t player = run.Byte(0);
    if (player != LocalPlayer(run)) {
        ShowPlayerText(run, kTextPlayerLost, player, player);
    }
    CallThread<kGameOverFor>(run.thread, 0);
    CGameData* world = run.world();
    if (world->objectives_enabled != 0 && world->counters_disabled != 0) {
        return;
    }
    for (std::uint32_t n = 1; n <= run.world()->building_high_water; ++n) {
        world = run.world();
        const Building& building = BuildingAt(world, n);
        if (building.owner != run.Byte(0) || !IsMilitaryBuilding(TypeOf(building))) {
            continue;
        }
        Call<kCrushBuilding>(world, n);
        if (n == static_cast<std::uint32_t>(g_selectedBuilding)) {
            g_selectedBuilding = 0;
        }
    }
}

void PlayerWon(Run& run) {
    CallThread<kGameOverFor>(run.thread, 0);
}

// ------------------------------------------------------------ 0xfe DropPlayer (0x53aefe), 0xff PlayerLeft (0x53b145)
// Payload: player. An active player becomes 2 and a computer takes over
// (without objectives); a message; 0xfe also sends the kill message.
void DropPlayer(Run& run) {
    const std::uint32_t player = run.Byte(0);
    if (PlayerAt(run.world(), player).active != kPlayerActive) {
        return;
    }
    HandOverToComputer(run, player, player * 41, kDropAssertLine);  // upper bits: the original's EDX (player * 41)
    ShowPlayerText(run, kTextPlayerDropped, run.Byte(0), run.Byte(0));
    std::uint8_t* kill = run.frame->kill_message;
    kill[1] = static_cast<std::uint8_t>(PlayerAt(run.world(), run.Byte(0)).net_slot);
    PushToNet(run.thread, kill, kill[2] >> 2);
    run.thread->player_flags[run.Byte(0)] = 0;
    CallThread<kUpdatePlayers>(run.thread);
}

void PlayerLeft(Run& run) {
    const std::uint32_t player = run.Byte(0);
    if (PlayerAt(run.world(), player).active != kPlayerActive) {
        return;
    }
    // Upper bits: the original's ECX (the world pointer).
    HandOverToComputer(run, player, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(run.world())),
                       kLeftAssertLine);
    ShowPlayerText(run, kTextPlayerLeft, run.Byte(0), run.Byte(0));
    run.thread->player_flags[run.Byte(0)] = 0;
    CallThread<kUpdatePlayers>(run.thread);
}

// ------------------------------------------------------------ network messages
// 3 (0x53b3bc): a player lost because of lag: the kill message for its
// network id (byte 1) and, if it is active, 0xfe for it.
void NetPlayerLost(Run& run) {
    GameDebugLog(texts::kMessagePlayerLost);
    const std::uint32_t player = PlayerOfNetId(run.world(), run.Id());
    GameDebugLog(texts::kKillBecauseOfLag);
    if (player >= kPlayers) {
        return;
    }
    std::uint8_t* kill = run.frame->kill_message;
    kill[1] = run.Id();
    PushToNet(run.thread, kill, kill[2] >> 2);
    if (PlayerAt(run.world(), player).active != 0) {
        PushReply(run, CommandId::DropPlayer, static_cast<std::uint8_t>(player));
    }
}

// 4 (0x53b4fc): the end of a recorded film: quit (window message 0x12).
void NetFilmEnd(Run&) {
    GameDebugLog(texts::kMessageFilmEnd);
    using GetAppFn = std::uint8_t*(__cdecl*)();
    std::uint8_t* app = reinterpret_cast<GetAppFn>(kGetApp)();
    using PostFn = void(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<PostFn>(kPostAppMessage)(app + 0xc, nullptr, kQuitMessage, 0, 0);
}

// 5 (0x53b483): kill the player with network id byte 1: 0xff for it.
void NetKillPlayer(Run& run) {
    GameDebugLog(texts::kMessageKillPlayer);
    const std::uint32_t player = PlayerOfNetId(run.world(), run.Id());
    if (player >= kPlayers || PlayerAt(run.world(), player).active == 0) {
        return;
    }
    PushReply(run, CommandId::PlayerLeft, static_cast<std::uint8_t>(player));
}

// 6 (0x53b521): a chat line (wide text from message byte 12) of network id byte 4.
void NetInGameChat(Run& run) {
    GameDebugLog(texts::kMessageInGameChat);
    const std::uint32_t player = PlayerOfNetId(run.world(), run.Byte(0));
    if (player >= kPlayers) {
        return;
    }
    ShowWideText(run, &run.frame->message.bytes[12], player);
}

// 8 (0x53b59a): another machine's checksums differ (network id byte 4).
void NetChecksumFailure(Run& run) {
    GameDebugLog(texts::kMessageChecksumFailure);
    const std::uint32_t player = PlayerOfNetId(run.world(), run.Byte(0));
    if (player >= kPlayers) {
        return;
    }
    ShowGfxText(run, kTextChecksumFailure, player);
}

// 0x22 (0x53b654): lag per network id (words at message byte 10 + 4 * id;
// ours at byte 6): above 5000 a message per lagging player, and one when
// we lag ourselves.
void NetLagReport(Run& run) {
    const std::uint8_t* message = run.frame->message.bytes;
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const Player& p = PlayerAt(run.world(), player);
        if (p.active != kPlayerActive) {
            continue;
        }
        std::uint16_t lag = 0;
        std::memcpy(&lag, message + 10 + static_cast<std::uint32_t>(p.net_slot) * 4, 2);
        if (lag > kLagLimit) {
            ShowPlayerText(run, kTextLagging, player, player);
        }
        std::uint16_t ownLag = 0;
        std::memcpy(&ownLag, message + 6, 2);
        if (ownLag > kLagLimit && player == LocalPlayer(run)) {
            ShowPlayerText(run, kTextYouLag, player, player);
        }
    }
}

}  // namespace s3re::game::commands

// ---------------------------------------------------------------------------
// Tests (execute_commands_test.h): only this file's commands and messages.
// The UI and lobby callees are replaced by recorders that log what they get
// (texts, not stack addresses; offsets of world and thread pointers) into a
// log compared between the original and ours; the simulation callees by
// CalleeStubs. std::string, the wide-string conversions and sprintf run as
// they are.
// ---------------------------------------------------------------------------

#include "game/logic/commands/execute_commands_test.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace s3re::game::commands {

namespace {

using bench::Bench;
using bench::MessageBuilder;

constexpr std::uint32_t kTestBuildings = 30;

// What the recorders saw, per side.
struct NetTestState {
    CGameData* world = nullptr;
    std::string log;
    std::uint32_t saveStart = 0, saveCommit = 0, saveRollBack = 0;  // per case, the results of the save calls
};
NetTestState& NetState() {
    static NetTestState state;
    return state;
}

void Log(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    NetState().log += line;
    NetState().log += "; ";
}

std::uint32_t Rel(const void* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) -
                                      reinterpret_cast<std::uintptr_t>(NetState().world));
}

const char* __fastcall FakeGfxText(void*, void*, std::uint32_t id, std::uint32_t variant) {
    Log("text %x/%x", id, variant);
    switch (id) {
    case kTextPlayerLost:
    case kTextPlayerDropped:
    case kTextPlayerLeft:
    case kTextLagging:
    case kTextYouLag:
        return "%s: player text";
    default:
        return id % 2 == 0 ? "even text" : "odd text";
    }
}

void __fastcall FakeShowMessage(CLogicThread* thread, void*, WideString text, std::uint32_t colour) {
    std::string content;
    const auto* chars = reinterpret_cast<const wchar_t*>(static_cast<std::uintptr_t>(text.words[1]));
    for (std::uint32_t i = 0; chars != nullptr && i < text.words[2] && i < 200; ++i) {
        content += static_cast<char>(chars[i]);
    }
    // Word 0 holds the allocator byte (a frame leftover) and padding: not compared.
    Log("show %x '%s' len %u res %u colour %u", Rel(thread), content.c_str(), text.words[2], text.words[3], colour);
}

void* __fastcall FakePlayerSetup(void* settings, void*, std::uint32_t player) {
    Log("setup %u%s", player, settings == &g_gameSettings ? "" : " (other object)");
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + player));
}

String* __fastcall FakePlayerName(void* setup, void*, String* out) {
    char name[32];
    std::snprintf(name, sizeof(name), "Name%u", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(setup)) - 0x1000);
    Log("name of %s", name);
    using FromCharsFn = String*(__fastcall*)(String*, void*, const char*, std::uint32_t);
    reinterpret_cast<FromCharsFn>(kStringFromChars)(out, nullptr, name, kWholeString);
    return out;
}

std::uint32_t __fastcall FakeComputerPlayer(void*, void*, CGameData* world, MessageQueue* toNet, std::uint32_t player,
                                            std::uint32_t flag) {
    // The constructor reads only the low byte of the flag (the original leaves a register's upper bytes there).
    Log("computer %x %x %u %u", Rel(world), Rel(toNet), player, flag & 0xff);
    return 0x5000 + player;
}

void __fastcall FakeSyncDump(CGameData* world, void*, const void* start, const void* end, std::uint32_t kind) {
    Log("dump %x %x %x %u", Rel(world), Rel(start), Rel(end), kind);
}

void __fastcall FakePostAppMessage(void* self, void*, std::uint32_t message, std::uint32_t a, std::uint32_t b) {
    Log("post %d %x %x %x", static_cast<int>(static_cast<std::uint8_t*>(self) - g_app.get()), message, a, b);
}

struct NetTestPatches {
    test::ScopedJump gfx{kTextAddress, reinterpret_cast<const void*>(&FakeGfxText)};
    test::ScopedJump show{kShowMessage, reinterpret_cast<const void*>(&FakeShowMessage)};
    test::ScopedJump setup{kPlayerSetup, reinterpret_cast<const void*>(&FakePlayerSetup)};
    test::ScopedJump name{kPlayerName, reinterpret_cast<const void*>(&FakePlayerName)};
    test::ScopedJump computer{kNewComputerPlayer, reinterpret_cast<const void*>(&FakeComputerPlayer)};
    test::ScopedJump dump{kSyncDump, reinterpret_cast<const void*>(&FakeSyncDump)};
    test::ScopedJump post{kPostAppMessage, reinterpret_cast<const void*>(&FakePostAppMessage)};
};

void InstallNetStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kCrushBuilding>([](CGameData*, std::uint32_t, const std::uint32_t* a) { return a[0] % 3 == 0; });
    stubs.Install<1, kGameOverFor>();
    stubs.Install<0, kUpdatePlayers>();
    stubs.Install<2, kSaveStart>([](CGameData*, std::uint32_t, const std::uint32_t*) { return NetState().saveStart; });
    stubs.Install<0, kSaveCommit>([](CGameData*, std::uint32_t, const std::uint32_t*) { return NetState().saveCommit; });
    stubs.Install<0, kSaveRollBack>(
        [](CGameData*, std::uint32_t, const std::uint32_t*) { return NetState().saveRollBack; });
}

std::uint8_t Hi(std::uint32_t v) {
    return static_cast<std::uint8_t>(v >> 8);
}
std::uint8_t Lo(std::uint32_t v) {
    return static_cast<std::uint8_t>(v);
}

void SetUpNetWorld(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    bench.worlds.Both([&](CGameData* w) {
        w->building_high_water = kTestBuildings;
        w->map_size = 384;
        w->area_high_water = 100;
        w->trade_high_water = 50;
        w->pile_high_water = 500;
        w->settler_high_water = 900;
    });
    const std::uint8_t types[] = {15, 16, 18, 41, 43, 45, 55, 2, 1, 33};
    for (std::uint32_t n = 0; n <= kTestBuildings + 1; ++n) {
        const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        const std::uint8_t type = types[rng.Below(10)];
        const auto buildup = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(200));
        bench.worlds.Both([&](CGameData* w) {
            w->buildings[n].owner = owner;
            w->buildings[n].art = type;
            w->buildings[n].buildup = buildup;
        });
    }
}

// Per case: players, UI and save state, globals.
void RandomizeNetCase(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    const auto local = static_cast<std::int32_t>(rng.Below(4));
    const auto objectives = static_cast<std::uint8_t>(rng.Below(2));
    const auto countersDisabled = static_cast<std::uint8_t>(rng.Below(2));
    const auto firstComputer = static_cast<std::uint8_t>(rng.Below(2));
    std::int32_t active[20];
    std::int32_t netId[20];
    for (std::uint32_t p = 0; p < 20; ++p) {
        active[p] = static_cast<std::int32_t>(rng.Chance(2, 3) ? 1 : rng.Below(3));
        netId[p] = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Below(20) : (p + 3) % 20);
    }
    const auto acks = rng.Below(6);
    const auto failures = rng.Below(3);
    const std::uint32_t tick = 5000 + rng.Below(5000);
    const std::uint32_t saveTick = rng.Chance(1, 3) ? 0 : tick - rng.Below(2000);
    bench.worlds.Both([&](CGameData* w) {
        w->local_player = local;
        w->objectives_enabled = objectives;
        w->counters_disabled = countersDisabled;
        w->flag15394b0 = firstComputer;
        for (std::uint32_t p = 0; p < 20; ++p) {
            w->players[p].active = active[p];
            w->players[p].net_slot = netId[p];
        }
        CLogicThread* thread = bench.Thread(w);
        thread->save_acks = acks;
        thread->save_failures = failures;
        thread->save_request_tick = saveTick;
    });
    g_gameTick = tick;
    g_selectedBuilding = static_cast<std::int32_t>(rng.Below(kTestBuildings + 2));
    if (rng.Chance(1, 4)) {
        g_simulationPaused = static_cast<std::uint16_t>(rng.Below(2));
    }
    NetState().saveStart = rng.Below(2);
    NetState().saveCommit = rng.Below(2) * 0x101;  // only AL counts
    NetState().saveRollBack = rng.Below(2) ? 0x100 : 1;
}

void AddNetMessage(test::Context& ctx, MessageBuilder& m) {
    test::Rng& rng = ctx.rng();
    const auto player = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Below(20) : rng.Below(4));
    switch (rng.Below(20)) {
    case 0: {
        const std::uint32_t b = rng.Below(kTestBuildings + 1);
        m.Command(CommandId::DestroyBuilding, {Hi(b), Lo(b), player});
        break;
    }
    case 1: m.Command(CommandId::ClearPlayerFlag, {player}); break;
    case 2:
    case 3: {
        const std::uint32_t p = rng.Below(4);
        std::vector<std::uint8_t> payload = {static_cast<std::uint8_t>(p), Lo(rng.Next32())};
        const std::uint32_t current = g_syncChecksums[p][7];
        for (std::uint32_t k = 0; k < 7; ++k) {
            const std::uint32_t value = rng.Chance(3, 4) ? g_syncChecksums[p][k] : rng.Below(4);
            payload.insert(payload.end(), {Lo(value >> 24), Lo(value >> 16), Hi(value), Lo(value)});
        }
        const std::uint32_t stamps[] = {current, current + 1, current - 1, 0, current, current};
        const std::uint32_t stamp = stamps[rng.Below(6)];
        payload.insert(payload.end(), {Lo(stamp >> 24), Lo(stamp >> 16), Hi(stamp), Lo(stamp)});
        std::vector<std::uint8_t> bytes = {Value(NetMessage::Command), Value(CommandId::SyncCheck), 0, 0};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        while (bytes.size() % 4 != 0) {
            bytes.push_back(0);
        }
        bytes[2] = static_cast<std::uint8_t>(bytes.size());
        m.Bytes(bytes);
        break;
    }
    case 4: m.Command(CommandId::SaveRequest, {player}); break;
    case 5: m.Command(CommandId::SaveDone, {player}); break;
    case 6: m.Command(CommandId::SaveFailed, {player}); break;
    case 7: m.Command(CommandId::SaveAllowed, {player}); break;
    case 8: {
        const std::uint32_t index = rng.Below(32);
        const std::uint32_t word = rng.Chance(1, 2) ? g_syncRandomHistory[index] : rng.Below(65536);
        m.Command(CommandId::SyncRandomCheck, {Hi(index), Lo(index), Hi(word), Lo(word), player});
        break;
    }
    case 9: m.Command(CommandId::PlayerLost, {player}); break;
    case 10: m.Command(CommandId::PlayerWon, {player}); break;
    case 11: m.Command(CommandId::DropPlayer, {player}); break;
    case 12: m.Command(CommandId::PlayerLeft, {player}); break;
    case 13: m.Raw(Value(NetMessage::PlayerLost), Lo(rng.Below(22)), {}); break;
    case 14: m.Raw(Value(NetMessage::FilmEnd), 0, {}); break;
    case 15: m.Raw(Value(NetMessage::KillPlayer), Lo(rng.Below(22)), {}); break;
    case 16:
        m.Raw(Value(NetMessage::InGameChat), 0,
              {Lo(rng.Below(22)), 1, 2, 3, 4, 5, 6, 7, 'H', 0, 'i', 0, Lo('0' + rng.Below(10)), 0, 0, 0});
        break;
    case 17: m.Raw(Value(NetMessage::ChecksumFailure), 0, {Lo(rng.Below(22)), Lo(rng.Next32())}); break;
    default: {
        std::vector<std::uint8_t> payload(6 + 4 * 20 + 2);
        const std::uint16_t lags[] = {0, 4000, 5000, 5001, 60000};
        for (std::size_t at = 2; at + 1 < payload.size(); at += 4) {
            const std::uint16_t lag = lags[rng.Below(5)];
            payload[at] = Lo(lag);
            payload[at + 1] = Hi(lag);
        }
        std::vector<std::uint8_t> bytes = {Value(NetMessage::LagReport), 0, 0, 0};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        bytes[2] = static_cast<std::uint8_t>(bytes.size());
        m.Bytes(bytes);
        break;
    }
    }
}

struct SavedNetGlobals {
    std::uint32_t tick = g_gameTick;
    std::int32_t selected = g_selectedBuilding;
    std::uint16_t paused = g_simulationPaused;
    std::int32_t flag7deb50 = g_global7deb50;
    std::uint32_t lastSave = g_lastSaveTick;
    std::uint32_t checks[20][8];
    SavedNetGlobals() { std::memcpy(checks, &g_syncChecksums, sizeof(checks)); }
    ~SavedNetGlobals() {
        g_gameTick = tick;
        g_selectedBuilding = selected;
        g_simulationPaused = paused;
        g_global7deb50 = flag7deb50;
        g_lastSaveTick = lastSave;
        std::memcpy(&g_syncChecksums, checks, sizeof(checks));
    }
};

void CompareNetCommands(test::Context& ctx, ExecuteCommandsFn candidate, int cases) {
    SavedNetGlobals saved;
    Bench bench(ctx);
    SetUpNetWorld(ctx, bench);
    InstallNetStubs(bench.stubs);
    NetTestPatches patches;
    bench.globalRegions = {{g_simulationPaused.address(), 2},
                           {g_syncChecksums.address(), sizeof(std::uint32_t) * 20 * 8},
                           {g_global7deb50.address(), 4},
                           {g_lastSaveTick.address(), 4},
                           {g_selectedBuilding.address(), 4}};
    bench.beforeSide = [](CGameData* world) {
        NetState().world = world;
        NetState().log.clear();
    };
    bench.afterSide = [](CGameData*) { return NetState().log; };
    // Start with small, partly matching checksum tables.
    for (std::uint32_t p = 0; p < 20; ++p) {
        for (std::uint32_t k = 0; k < 8; ++k) {
            g_syncChecksums[p][k] = ctx.rng().Below(4);
        }
    }
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < cases; ++i) {
        RandomizeNetCase(ctx, bench);
        const std::uint32_t step = 1 + rng.Below(1000);
        bench.SetStep(step);
        MessageBuilder messages;
        const std::uint32_t count = 1 + rng.Below(4);
        for (std::uint32_t k = 0; k < count; ++k) {
            AddNetMessage(ctx, messages);
        }
        messages.Grant(step + 1);
        if (!bench.Run(ctx, i, messages, -1, candidate)) {
            return;
        }
    }
    bench.Finish(ctx);
}

// Wrong: SyncCheck doesn't pause the simulation after a mismatch.
std::uint32_t __fastcall WrongNetCommands(CLogicThread* self, void* edx) {
    const std::uint16_t paused = g_simulationPaused;
    const std::uint32_t result = CLogicThread_ExecuteCommands(self, edx);
    if (g_simulationPaused != paused) {
        g_simulationPaused = paused;
    }
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ExecuteCommands_Net_MatchesOriginal) {
    CompareNetCommands(ctx, &CLogicThread_ExecuteCommands, 3000);
}
S3RE_TEST(CLogicThread_ExecuteCommands_Net_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNetCommands(probe, &WrongNetCommands, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::commands
