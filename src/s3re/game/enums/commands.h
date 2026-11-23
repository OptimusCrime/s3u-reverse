#pragma once

// Net messages and command ids of ExecuteCommands.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- net messages and commands
// Byte 0 of a message the network side sends to the logic thread (queue
// CLogicThread+0x3c8), dispatched by CLogicThread::ExecuteCommands 0x535990
// (jump table 0x53b850 / index 0x53b874). Names from its DebugLog strings
// ("Message PlayerLost", ...); evidence and the payloads in
// re/execute_commands.md and re/enums.md.
enum class NetMessage : std::uint8_t {
    Command = 1,           // a command (byte 1 = CommandId), sent back by the net thread in step order
    StepGrant = 2,         // dword 1: the logic may run up to this step; ends ExecuteCommands
    PlayerLost = 3,        // "Message PlayerLost", "LOGIC: Kill wegen lag"
    FilmEnd = 4,           // "Message FilmEnd"
    KillPlayer = 5,        // "Message KillPlayer"
    InGameChat = 6,        // "Message InGameChat"
    ChecksumFailure = 8,   // "Message ChecksumFailure"
    LagReport = 34,        // 0x22: per-player words compared with 5000 (lag warnings, probably)
    // Logic -> network (CLogicThread+0x3c4):
    StepRequest = 32,      // 0x20 {0x20, 0, 8, 0, step}: pushed by every ExecuteCommands call
    CommandRequest = 33,   // 0x21: a command (byte 1 = CommandId), sent back as Command in step order
};

// Byte 1 of a Command message (and of the 0x21 messages the logic pushes):
// the player's (or computer player's) command. The jump table of
// ExecuteCommands (0x53b898, index 0x53b934) has a handler for each of
// these; other ids do nothing. Names are ours (what the handler does);
// "probably" ones are marked in re/enums.md.
enum class CommandId : std::uint8_t {
    PlaceBuilding = 1,          // CheckBuildingSite(x, y, type, player, mode 1)
    ScrollCamera = 2,           // camera += (dx, dy), ClampCamera (UI)
    SetSettlerGroup = 3,        // player_lists[player][group] = the settlers
    SetBuildingWorking = 4,     // request flag 0x01 on/off; off recalls the carriers
    SetWorkArea = 5,            // work tile of a production building / site
    DestroyBuilding = 6,        // CrushBuilding
    ChangeGoodsPriority = 7,    // area settings1[good] +/- 1 (0..20)
    SetGoodsSetting = 8,        // area settings2[good]
    MoveUnits = 9,              // send the selected units to a tile
    ChangeTransportOrder = 10,  // area settings4/5: transport order list
    ChangeToolSettings = 11,    // area settings_extra/setting_241/counters_242 (percentages, menu 3)
    ChangeGarrison = 12,        // soldiers wanted per class in a tower or castle
    MoveUnitsMode1 = 13,        // MoveUnits with mode 1
    MoveUnitsMode2 = 14,        // MoveUnits with mode 2
    MoveUnitsMode3 = 15,        // MoveUnits with mode 3
    ConvertCarriers = 16,       // carriers -> types 0x16 / 0x17 / 0x28 or back
    SetAreaCondition = 17,      // area condition_enabled[kind], ResetAreaConditionCounters
    SetTradeRoute = 18,         // harbour / marketplace route points and orders
    SetWorkshopProduction = 19, // water buildings and shipyard: production state
    SetShipyardDock = 20,       // BlockBuildingLine + PlaceDockLine (shipyard)
    CastSpell = 21,             // a priest's spell (godly event)
    UnloadShip = 22,            // a ship (type 0x2c) lands its passengers at the shore
    SetHarbourDock = 23,        // BlockBuildingLine + PlaceDockLine (harbour)
    PromoteSoldiers = 24,       // soldier level up for mana, SetPromotionCost
    LoadShip = 25,              // load units into a ship from a water building
    HoldPosition = 27,          // 0x1b: soldiers of a group keep their position
    SetStorehouseGoods = 28,    // 0x1c: storehouse pile goods (ChangePileGood)
    RevealMap = 241,            // 0xf1: g_mapRevealed (0x7dfd84) = 1
    ClearPlayerFlag = 242,      // 0xf2: CLogicThread+0x235[player] = 0, 0x5411b0
    SyncCheck = 244,            // 0xf4: world checksums ("Desync World!" ...)
    PrioritizeSite = 245,       // 0xf5: all four request bits + RecallCarriersOfOtherSites
    UnprioritizeSite = 246,     // 0xf6: clear the request bits (+ RecallSiteCarriers)
    SaveRequest = 247,          // 0xf7: multiplayer save ("Multiplayersave ...")
    SaveDone = 248,             // 0xf8: SaveGameCommit when all players saved
    SaveFailed = 249,           // 0xf9: SaveGameRollBack
    SaveAllowed = 250,          // 0xfa: save request within the tick limit
    SyncRandomCheck = 251,      // 0xfb: another player's RNG history word (g_syncRandomHistory 0x7dfb58) differs: "Desync!" (was CheckVersion)
    PlayerLost = 252,           // 0xfc: the player lost (military buildings crushed)
    PlayerWon = 253,            // 0xfd
    DropPlayer = 254,           // 0xfe: "LOGIC: Dropped player", a computer takes over ("Neuer Computer")
    PlayerLeft = 255,           // 0xff: the same for a player who left
};

}  // namespace s3re::game
