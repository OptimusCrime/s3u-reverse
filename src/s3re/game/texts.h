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
inline constexpr GameText kGodlyEventsFile{0x731054};      // "E:\Siedler3\Volker\GodlyEvents.cpp"
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

}  // namespace s3re::game::texts
