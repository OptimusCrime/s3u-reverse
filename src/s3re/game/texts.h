#pragma once

// Strings in S3.EXE's data that our replacements pass on: DebugLog format
// strings, exception texts, source file names. One declaration per string,
// the only place its address appears (game/global_access.h); the text is in
// the comment (German ones translated in re/source_files.md where useful).
// The original passes these pointers, so our code passes the same ones.

#include "game/global_access.h"

namespace s3re::game::texts {

// ------------------------------------------------------------ general
inline constexpr GameText kEmpty{0x7a7868};      // "" (assigned instead of a null C string)
inline constexpr GameText kNewline{0x699000};    // "\n"
inline constexpr GameText kFullStop{0x6974a4};   // "."
inline constexpr GameText kCommaLine{0x7a2a34};  // ", line "
inline constexpr GameText kCommaFile{0x7a2a3c};  // ", file "
inline constexpr GameText kAssertmentFailed{0x7a4758};  // "Assertment failed: "
inline constexpr GameText kNotEnoughMemoryAt{0x7a4694};  // "Not enough memory at "

// ------------------------------------------------------------ Dirk\*.cpp
inline constexpr GameText kPathingFile{0x69f9dc};       // "E:\Siedler3\Dirk\Pathing.cpp"
inline constexpr GameText kShouldManage{0x69f9bc};      // "Das solltest du schon schaffen"
inline constexpr GameText kUnsupportedSectorType{0x69f9fc};  // "Unsupported SectorType"
inline constexpr GameText kInvalidArea{0x69fb04};       // "Invalid area"
inline constexpr GameText kInvalidAngle{0x69fb2c};      // "Invalid angle selection"
inline constexpr GameText kRightWallNoDirection{0x69fad8};  // "PATH: Rightwall, No Direction %i %i %i %i"
inline constexpr GameText kRightWallNoWay{0x69fab4};        // "PATH: Rightwall, No way %i %i %i %i"
inline constexpr GameText kRightWallNoAngle{0x69fa8c};      // "PATH: Right wall, no angle %i %i %i %i"
inline constexpr GameText kLeftWallNoDirection{0x69fa60};   // "PATH: Leftwall, no direction %i %i %i %i"
inline constexpr GameText kLeftWallNoWay{0x69fa3c};         // "PATH: Leftwall, no way  %i %i %i %i"
inline constexpr GameText kLeftWallNoAngle{0x69fa14};       // "PATH: Leftwall, no angle %i %i %i %i"
inline constexpr GameText kSaveGamesFile{0x69fcd4};     // "E:\Siedler3\Dirk\SaveGames.cpp"
inline constexpr GameText kChunkName{0x69fccc};         // "lpChunk" (CMapFile::GetChunk's allocation)
inline constexpr GameText kNewChunkName{0x69fcf4};      // "newchunk" (CMapFile::AddChunk's allocation)
inline constexpr GameText kTempName{0x69fd00};          // "lpTemp" (CMapFile::WriteChunk*'s allocations)
inline constexpr GameText kInvalidFileType{0x69fc38};   // "LOAD: Invalid filetype %i %i"
// CGameData::LoadMap (0x4fd540)
inline constexpr GameText kLoadingMap{0x6a5dbc};            // "Loading map..."
inline constexpr GameText kWorldsize{0x6a5dac};             // "Worldsize: %u"
inline constexpr GameText kDeletingBuildingBits{0x6a5d90};  // "Deleting Building-Bits..."
inline constexpr GameText kSettingAllSectors{0x6a5d6c};     // "Setting all sectors for loaded Map"
inline constexpr GameText kLoadingBuildings{0x6a5d54};      // "Loading buildings..."
inline constexpr GameText kAmountOfBuildings{0x6a5d3c};     // "Amount of buildings: %u"
// "Gebäude Typ %u    x,y  %u %u    mapowner %u gameowner %u" (building type, position, map and game owner)
inline constexpr GameText kBuildingTypeAt{0x6a5d00};
inline constexpr GameText kLoadingSettlers{0x6a5cec};       // "Loading settlers..."
inline constexpr GameText kAmountOfSettlers{0x6a5cd4};      // "Amount of settlers: %u"
inline constexpr GameText kLoadingPiles{0x6a5cc0};          // "Loading piles..."
inline constexpr GameText kAmountOfPiles{0x6a5cac};         // "Amount of piles: %u"
inline constexpr GameText kRaceChanged{0x6a5c84};           // "xxx race changed #%u - BuildingType %u"
inline constexpr GameText kRaceNotChanged{0x6a5c58};        // "xxx race NOT changed #%u - BuildingType %u"
// CGameData::PlaceTown (0x506cf0) and AddPlayerTowns (0x5074b0)
inline constexpr GameText kPlacingTown{0x6a6250};           // "placing town: %u at: %u,%u"
inline constexpr GameText kPlacingPlayer{0x6a6390};         // "Placing player %u"
inline constexpr GameText kPositionFound{0x6a636c};         // "Position gefunden nach %u Versuchen" (found after %u attempts)
inline constexpr GameText kMirrorPositionFound{0x6a62d0};   // "Mirror-Position gefunden nach %u Versuchen"
inline constexpr GameText kQuadMirrorPositionFound{0x6a62a0};    // "Quad-Mirror Position gefunden nach %u Versuchen"
inline constexpr GameText kTripleMirrorPositionFound{0x6a626c};  // "Triple-Mirror-Position gefunden nach %u Versuchen"
// "fct AddPlayerTowns: Konnte keine Zufallsposition finden" (could not find a random position)
inline constexpr GameText kNoRandomPosition{0x6a6334};
// "fct AddPlayerTowns: Konnte keine Kreisposition finden" (could not find a circle position)
inline constexpr GameText kNoCirclePosition{0x6a62fc};
// CLogicThread::LoadSaveGame (0x508fa0)
inline constexpr GameText kSavefileVersion{0x6a659c};         // "Savefile Version : %d"
inline constexpr GameText kLoadingVictoryConditions{0x6a6574};  // "LoadGame: loading VictoryConditions..."
inline constexpr GameText kLoadingPlayerColors{0x6a6550};     // "LoadGame: loading PlayerColors..."
inline constexpr GameText kClearingPlayerColors{0x6a652c};    // "LoadGame: clearing PlayerColors..."
inline constexpr GameText kLoadingProdStat{0x6a6508};         // "LoadGame: loading ProdStatArray..."
inline constexpr GameText kClearingProdStat{0x6a64e4};        // "LoadGame: clearing ProdStatArray..."
inline constexpr GameText kLoadingObjective{0x6a64bc};        // "LoadGame: loading NewGameObjective..."
inline constexpr GameText kClearingObjective{0x6a6494};       // "LoadGame: clearing NewGameObjective..."
inline constexpr GameText kLoadingObjectiveTimeout{0x6a6464};   // "LoadGame: loading NewGameObjectiveTimeout..."
inline constexpr GameText kClearingObjectiveTimeout{0x6a6434};  // "LoadGame: clearing NewGameObjectiveTimeout..."
inline constexpr GameText kDroppedEco{0x6a641c};              // "LOAD: Dropped eco %i"
inline constexpr GameText kProtocolFile{0x7a34fc};      // "E:\Siedler3\Dirk\protocol.cpp"
inline constexpr GameText kNewDataName{0x7a3570};       // "lpNewData"
inline constexpr GameText kDuringResize{0x7a355c};      // "PROT: During Resize"
inline constexpr GameText kDuringAppend{0x7a3548};      // "PROT: During Append"
inline constexpr GameText kAnimationNotFound{0x7a2e9c};  // "GFX: Animation not found %u:%u"
inline constexpr GameText kDDBltName{0x7a01e0};         // "CSurface::DDBlt"
inline constexpr GameText kDDBltFastName{0x7a01f0};     // "CSurface::DDBltFast"

// ------------------------------------------------------------ simulation (DebugLog formats)
// Buildings
inline constexpr GameText kStartCrushing{0x6a5824};     // "Start crushing nr. %u art %u"
inline constexpr GameText kCampaign70NoCrush{0x6a57f8};  // "Campaign 70: Building must not be crushed!"
inline constexpr GameText kCrush1{0x6a57f0};            // "crush1"
inline constexpr GameText kCrush2{0x6a57e8};            // "crush2"
inline constexpr GameText kCrush3{0x6a57e0};            // "crush3"
inline constexpr GameText kCrush4{0x6a57d8};            // "crush4"
inline constexpr GameText kCrush5{0x6a57d0};            // "crush5"
inline constexpr GameText kEndOfCrushing{0x6a57c0};     // "End of Crushing"
inline constexpr GameText kDigger{0x6a67d4};            // "digger!"
inline constexpr GameText kBuilder{0x6a67c8};           // "builder!"
inline constexpr GameText kBuildingNotInRange{0x6a5844};  // "DestroyBuilding: Can't find building in range (%u,%u)"
inline constexpr GameText kCannotFindBringers{0x6a6060};  // "TROUBLE: cannot find all people that wanted to bring goods"
inline constexpr GameText kPushingPileInEmptyList{0x6a609c};  // "RemoveStorage: Pushing pile in EmptyList!"
inline constexpr GameText kGoodsOnTheirWay{0x6a5990};   // "%u Waren hierher unterwegs"
inline constexpr GameText kResultAfterReduction{0x6a5970};  // "Ergebnis nach Reduzierung %u"
inline constexpr GameText kBuildingDoneProductionPile{0x6a5908};  // "BuildingDone: Storage.ProductionPile Good = %u"
inline constexpr GameText kPileFoundAtBuilding{0x6a58d8};  // "%u. Warenstapel an Gebaude gefunden! Typ = %u"
inline constexpr GameText kAddBuilding{0x6a58a0};       // "AddBuilding: y = %u - OP1 = %u - OP2 = %u - OP3 = %u"
inline constexpr GameText kIllegalGoodCheck1{0x6a59ac};  // "TROUBLE: illegal goodcheck 1 at building %u %u"
inline constexpr GameText kIllegalGoodCheck2{0x6a59dc};  // "TROUBLE: illegal goodcheck 2 at building %u %u"
inline constexpr GameText kIllegalGoodCheck3{0x6a5a0c};  // "TROUBLE: illegal goodcheck 3 at building %u %u"
inline constexpr GameText kIllegalGoodCheck4{0x6a5a3c};  // "TROUBLE: illegal goodcheck 4 at building %u %u"
inline constexpr GameText kIllegalBuildingOffset{0x6a587c};  // "illegal building offset %u %u %u %u"
inline constexpr GameText kShipProduction{0x6a5950};    // "Schiffsproduktion %u owner %u"
inline constexpr GameText kCentreXY{0x6a5938};          // "centerx,centery = %u,%u"
inline constexpr GameText kBuildGong{0x6a5a6c};         // "Gong bauen - nach SecureProduction (254 wahr gut) byte8 = %u"
// Piles and land
inline constexpr GameText kPileTaken{0x6a6138};         // "Pile %u Taken , eco %u"
inline constexpr GameText kPileDetails{0x6a6150};       // "nr %u  art %u, good %u  x %u y %u"
inline constexpr GameText kPileIncAmountLeaving{0x6a61bc};  // "PileIncAmountLeaving: art = %u - pilno = %u - ..."
inline constexpr GameText kPileIncAmountLeavingBuilding{0x6a6174};  // "PileIncAmountLeaving: Building[i].art = %u - ..."
inline constexpr GameText kPileLost{0x6a6120};          // "Pile %u Lost, eco %u"
inline constexpr GameText kIllegalPileInChangingLand{0x6a60d8};  // "illegal pile in changing land: nr.: %u  amount: %u ..."
inline constexpr GameText kPositionXY{0x6a60c8};        // "(x,y) = (%u,%u)"
inline constexpr GameText kOrderPileFull{0x6a6224};     // "StealGoodFromPile: OrderPile nix mehr frei!"
inline constexpr GameText kPlayerLand{0x6a63a4};        // "Player %u   Land %u  Trees %u  Stone %u ..."
// Settlers
inline constexpr GameText kStepAsideOnSectorZero{0x6a6784};  // "JobNothing/StepAside auf economysector 0   art %u  (x,y) = (%u,%u)"
inline constexpr GameText kTileOwnerDetails{0x6a6740};  // "Owner %u, world[x][y].owner %u, object %u  ecosec %u    worldart %u"
inline constexpr GameText kSettlerOnSectorZero{0x6a6704};  // "Siedler steht auf sector 0, art %u  x,y %u,%u   oldjob %u"
inline constexpr GameText kSpotOutOfMap{0x6a5ffc};      // "Settler tries to reach spot out of map"
inline constexpr GameText kFiveNumbers{0x6a5fec};       // "%u %u %u %u %u"
inline constexpr GameText kNotThere{0x6a5fbc};          // "SnotThere nr: %u  owner %u   art %u   job  %u"
inline constexpr GameText kIllegalSwap{0x6a5f84};       // "TROUBLE: wollte auf illegals Feld wechseln %i %i %i %i"
inline constexpr GameText kIllegalWalkCount{0x6a5f5c};  // "Illegal walkcount vorher %u, Ship %u"
inline constexpr GameText kSearchTarget{0x6a6048};      // "suche Ziel bei %u %u"
inline constexpr GameText kAttackBuilding{0x6a6038};    // "Angriff Gebaeude"
inline constexpr GameText kAttackCatapult{0x6a6024};    // "Angriff Katapult"
inline constexpr GameText kSoldiersLeft{0x6a67fc};      // "TROUBLE ????   Soviele sind ubrig: %u"
inline constexpr GameText kWinsTower{0x6a67dc};         // "Player %u wins tower from %u"
// UpdateSettlers (settler_jobs_*.cpp)
inline constexpr GameText kSettlerOnForeignGround{0x6a5be0};  // "nr. %u art %u owner %u groundowner %u"
inline constexpr GameText kTypeNumber{0x6a5bd0};        // "Typ: %u Nr %u"
inline constexpr GameText kCatapultProduction{0x6a5bb4};  // "Katapultproduktion owner %u"
inline constexpr GameText kShipUnloads{0x6a5ba4};       // "Schiff lädt ab" (ship unloads)
inline constexpr GameText kThiefStealFail{0x6a5b78};    // "Dieb[%u (%u,%u)]: StealGoodFromPile fail!"
inline constexpr GameText kThiefNoPile{0x6a5b34};       // "Dieb[%u (%u,%u)]: SpyTakeGood -> no pile there? so search again! "
inline constexpr GameText kGongProduction{0x6a5b1c};    // "Gongproduktion owner %u"
inline constexpr GameText kDiggerTrouble{0x6a5b04};     // "Digger trouble %u %u"
// Environment and effects
inline constexpr GameText kBuildingHit{0x6a5aec};       // "Gebaeude getroffen %u"
inline constexpr GameText kCatapultHit{0x6a5ad4};       // "Katapult getroffen %u"
inline constexpr GameText kShipSinks{0x6a5ac0};         // "Schiff sinkt !!!"
inline constexpr GameText kShipHit{0x6a5aac};           // "Schiff getroffen %u"
inline constexpr GameText kEffectOverflow{0x730ed4};    // "EFFECT: Effect overflow"
inline constexpr GameText kTooManyColours{0x731278};    // "Error ! zu viele Farben"
// Spells (godly events)
inline constexpr GameText kManaNeeded{0x7311d0};        // "xxx Godevent mana needed: %u at slot: %u"
inline constexpr GameText kRevealMapDone{0x7310c0};     // "xxx GE done: optionviewall SET"
inline constexpr GameText kExecuteConvertGoods{0x730f90};  // "xxx about to execute EventConvertGoods"
inline constexpr GameText kExecuteGodlyGoods{0x730f68};    // "xxx about to execute EventGodlyGoods"
inline constexpr GameText kExecuteGoodsBan{0x730fe0};      // "xxx about to execute EventGoodsBan"
inline constexpr GameText kExecuteGoodsSummon{0x730fb8};   // "xxx about to execute EventGoodsSummon"
inline constexpr GameText kExecuteTreeCurse{0x731004};     // "xxx about to execute EventTreeCurse"
inline constexpr GameText kExecuteRevealMap{0x731028};     // "xxx about to execute EventRevealMap"
inline constexpr GameText kHelp{0x73104c};                 // "HILFE" ("help"), NextTransformTerrain's assertion
inline constexpr GameText kGodlyEventsFile{0x731054};      // "E:\Siedler3\Volker\GodlyEvents.cpp"
inline constexpr GameText kTreeCurseTreesSet{0x731078};    // "xxx GE: TreeCurse: trees set: %u"
inline constexpr GameText kTreeCurseOffsets{0x73109c};     // "xxx GE: TreeCurse iEventOffset: %u"
inline constexpr GameText kGodlyEventsAssert{0x7310e0};    // "bl\xf6dkopf" (Latin-1, "thickhead": the assert text for a race > 3)
inline constexpr GameText kChangeSettler{0x731154};        // "GOD: EventChangeSettler %i %i %i %i"
inline constexpr GameText kConvertGoodsMax{0x73112c};      // "xxx convert goods iEventMaxObjects: %u"
inline constexpr GameText kGoodStolenAndAdded{0x7310ec};   // "xxx good stolen and new one added: %u"
inline constexpr GameText kCannotStealGood{0x731114};      // "xxx cannot steal good"
inline constexpr GameText kGoodsBanMax{0x7311ac};          // "xxx goods ban iEventMaxObjects: %u"
inline constexpr GameText kCannotStealGoodBan{0x731188};   // "xxx cannot steal good (GoodsBan)"
inline constexpr GameText kGoodsPorted{0x731178};          // "xxx ported: %u"
// Random map generator
inline constexpr GameText kCreatingWorld{0x72fef8};     // "creating world"
inline constexpr GameText kErrorTop{0x72fee4};          // "Error top %u %u %u"
inline constexpr GameText kErrorBottom{0x72fecc};       // "Error bottom %u %u %u"
inline constexpr GameText kErrorLeft{0x72feb8};         // "Error left %u %u %u"
inline constexpr GameText kErrorRight{0x72fea0};        // "Error right %u %u %u"
// CLogicThread::ExecuteCommands (Volker\LogicHandleNet.cpp)
inline constexpr GameText kInvalidNetMessage{0x7300fc};  // "Ung\xfcltige Netznachricht: Type: %u" ("invalid network message")
inline constexpr GameText kGodlyEventRange{0x730514};    // "GodlyEvent (ID#)%u GodRange: %u"
inline constexpr GameText kGodlyEventHappened{0x7304cc};  // "Godly Event (ID#)%u happened at %u,%u from priest at %u,%u godrange: %u"
// Formats of the network messages 3..8 (execute_commands_net.cpp)
inline constexpr GameText kMessagePlayerLost{0x730188};      // "Message PlayerLost"
inline constexpr GameText kKillBecauseOfLag{0x730170};       // "LOGIC: Kill wegen lag" ("kill because of lag")
inline constexpr GameText kMessageKillPlayer{0x73015c};      // "Message KillPlayer"
inline constexpr GameText kMessageFilmEnd{0x73014c};         // "Message FilmEnd"
inline constexpr GameText kMessageInGameChat{0x730138};      // "Message InGameChat"
inline constexpr GameText kMessageChecksumFailure{0x730120};  // "Message ChecksumFailure"
// MoveUnits (execute_commands_units.cpp)
inline constexpr GameText kGongAttack{0x730534};         // "GongAttack = true"

// ExecuteCommands: SyncCheck, the multiplayer save, computer players (execute_commands_net.cpp)
inline constexpr GameText kCheckSumPlayer{0x730498};  // "CheckSum player %u = %u - CheckBuffer[%u][%u] = %u"
inline constexpr GameText kDesyncWorld{0x73046c};     // "Desync World!"
inline constexpr GameText kDesyncEconomy{0x73043c};   // "Desync Economy!"
inline constexpr GameText kDesyncTrade{0x730410};     // "Desync Trade!"
inline constexpr GameText kDesyncPile{0x7303e4};      // "Desync Pile!"
inline constexpr GameText kDesyncSettler{0x7303b4};   // "Desync Settler!"
inline constexpr GameText kDesyncBuildings{0x73037c};  // "Desync Buildings!"
inline constexpr GameText kDesync{0x73021c};          // "Desync!"
inline constexpr GameText kStartingMultiplayerSave{0x730360};  // "Starting multiplayersave"
inline constexpr GameText kMultiplayerSaveOk{0x73032c};        // "Multiplayersave was ok! Calling SaveGameCommit()"
inline constexpr GameText kMultiplayerSaveFailed{0x73027c};    // "Multiplayersave failed Calling SaveGameRollBack()"
inline constexpr GameText kDroppedPlayer{0x730200};   // "LOGIC: Dropped player %i %i"
inline constexpr GameText kNewComputer{0x7301e0};     // "Neuer Computer %u bei step %u" ("new computer %u at step %u")
inline constexpr GameText kComputerAssert{0x73019c};  // "lpComputer [r->data [0]]" (assertion text)
inline constexpr GameText kLogicHandleNetFile{0x7301b8};  // "E:\Siedler3\Volker\LogicHandleNet.cpp"
// Wide (UTF-16) texts the logic shows (0x53ba60 builds a wide string from them).
inline constexpr GameText kWideDesyncWorld{0x73047c};      // L"DESYNC World!"
inline constexpr GameText kWideDesyncEconomy{0x73044c};    // L"DESYNC Economy!"
inline constexpr GameText kWideDesyncTrade{0x730420};      // L"DESYNC Trade!"
inline constexpr GameText kWideDesyncPile{0x7303f4};       // L"DESYNC Pile!"
inline constexpr GameText kWideDesyncSettler{0x7303c4};    // L"DESYNC Settler!"
inline constexpr GameText kWideDesyncBuildings{0x730390};  // L"DESYNC Buildings!"
inline constexpr GameText kWideSaveError{0x7302b0};   // L"Error while saving! Check the file attributes and disk space!"
inline constexpr GameText kWideCannotDeleteTemp{0x730224};  // L"Cannot delete the file Save\Multi\Temp.mps!"
// Computer players (Rainer\CompMapWorkerAgent.cpp).
inline constexpr GameText kAiCenterGroup{0x732dd8};  // "Center - player: %d, group: %d \n" (OutputDebugStringA)
inline constexpr GameText kRandom55{0x732a84};       // "RANDOM_55 wert = %u" (value, DebugLog of mission 56)
// "Verteidigung building: %d, group: %d, gegner: %d\n" (defence, enemies)
inline constexpr GameText kAiDefence{0x73303c};
// "Verteidigung building: %d, player: %d, group: %d abgebrochen - keine Feinde mehr\n" (defence ended, no enemies)
inline constexpr GameText kAiDefenceOver{0x732fe8};
// "Attack building: %d, player: %d, group: %d abgebrochen - gehoert mir\n" (attack ended, it is mine; o-umlaut in the binary)
inline constexpr GameText kAiAttackOwnBuilding{0x732fa0};
inline constexpr GameText kAiStealTargetDeleted{0x732f64};  // "Tower steal: Attack target %d is deleted for all armys! \n"
// "Reset global Attack - player: %d, group: %d, building: %d ^gegner ist zu stark\n" (the enemy is too strong)
inline constexpr GameText kAiGlobalAttackReset{0x732f14};
}  // namespace s3re::game::texts
