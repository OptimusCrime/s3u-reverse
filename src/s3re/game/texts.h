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

// ------------------------------------------------------------ Win32\*.cpp
// The VERIFY expressions and files Win32\Mapping.cpp and Win32\Detach.cpp
// pass to Win32::ThrowWin32Error / ThrowHResult.
inline constexpr GameText kMappingFile{0x7a4f7c};           // "E:\Siedler3\Win32\Mapping.cpp"
inline constexpr GameText kDetachFile{0x7a5080};            // "E:\Siedler3\Win32\Detach.cpp"
inline constexpr GameText kFileHandleValid{0x7a4f5c};       // "hFile != INVALID_HANDLE_VALUE"
inline constexpr GameText kRetvalNotFalse{0x7a4f9c};        // "bRetval != FALSE"
inline constexpr GameText kRetvalValid{0x7a4fec};           // "dwRetval != 0xffffffff"
// "(dwRetval != 0xffffffff) && (GetLastError () != NO_ERROR)"
inline constexpr GameText kRetvalValidHigh{0x7a4fb0};
inline constexpr GameText kMappingNotNull{0x7a5054};        // "hMapping != NULL"
inline constexpr GameText kBaseAddressNotNull{0x7a5068};    // "lpBaseAddress != NULL"
inline constexpr GameText kFalse{0x7a2b50};                 // "false"
inline constexpr GameText kErrorCheck{0x697024};            // "error check"

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
// CLogicThread::SaveGame (0x509980): the folders, the source file and the
// out-of-memory labels of the chunk buffers (the chunks' original names).
inline constexpr GameText kSaveFolder{0x6a66bc};              // "Save"
inline constexpr GameText kSaveMultiFolder{0x69e6dc};         // "Save\Multi"
inline constexpr GameText kSaveFolderPrefix{0x69ec30};        // "Save\"
inline constexpr GameText kGameDataSaveFile{0x6a6698};        // "E:\Siedler3\Volker\GameDataSave.cpp"
inline constexpr GameText kSaveLabelDesc{0x6a6690};           // "desc" (chunk 0)
inline constexpr GameText kSaveLabelGeneral{0x6a6688};        // "general" (1)
inline constexpr GameText kSaveLabelEvents{0x6a6680};         // "events" (2)
inline constexpr GameText kSaveLabelSectors{0x6a6678};        // "sectors" (4)
inline constexpr GameText kSaveLabelPlayer{0x6a6670};         // "player" (6)
inline constexpr GameText kSaveLabelSettler{0x6a6668};        // "settler" (7)
inline constexpr GameText kSaveLabelBuildings{0x6a665c};      // "buildings" (8)
inline constexpr GameText kSaveLabelPiles{0x6a6654};          // "piles" (9)
inline constexpr GameText kSaveLabelDiggerTab{0x6a6648};      // "diggertab" (10)
inline constexpr GameText kSaveLabelActiveSettlers{0x6a6638};  // "activesettlers" (12)
inline constexpr GameText kSaveLabelTrade{0x6a6630};          // "trade" (13)
inline constexpr GameText kSaveLabelTimer{0x6a6628};          // "timer" (14)
inline constexpr GameText kSaveLabelMessages{0x6a661c};       // "messages" (15)
inline constexpr GameText kSaveLabelWarMap{0x6a6614};         // "warmap" (16)
inline constexpr GameText kSaveLabelVictoryConditions{0x6a6600};  // "GCVictoryConditions" (0x18)
inline constexpr GameText kSaveLabelPlayerColors{0x6a65f0};   // "GCPlayerColors" (0x19)
inline constexpr GameText kSaveLabelProdStat{0x6a65e4};       // "GCProdStat" (0x1c)
inline constexpr GameText kSaveLabelObjective{0x6a65d0};      // "GCNewGameObjective" (0x1d)
inline constexpr GameText kSaveLabelObjectiveTimeout{0x6a65b4};  // "GCNewGameObjectiveTimeout" (0x1f)
// CLogicThread::SaveGameToSlot (0x50ac40): the savegame names.
inline constexpr GameText kSlotSaveName{0x6a66d4};            // "slot%2i.sav"
inline constexpr GameText kMultiplayerSaveName{0x6a66c4};     // "Multi	emp.mps"
// GameSettings::SaveSetup (0x433490)
inline constexpr GameText kSavingLocalPlayerId{0x6995bc};     // "CFG: Saving local player id %i"
inline constexpr GameText kProtocolFile{0x7a34fc};      // "E:\Siedler3\Dirk\protocol.cpp"
inline constexpr GameText kNewDataName{0x7a3570};       // "lpNewData"
inline constexpr GameText kDuringResize{0x7a355c};      // "PROT: During Resize"
inline constexpr GameText kDuringAppend{0x7a3548};      // "PROT: During Append"
inline constexpr GameText kAnimationNotFound{0x7a2e9c};  // "GFX: Animation not found %u:%u"
inline constexpr GameText kColorIndexedAnimationNotFound{0x7a2ebc};  // "GFX: ColorIndexedAnimation not found %u:%u"
inline constexpr GameText kShadowAnimationNotFound{0x7a2ee8};  // "GFX: ShadowAnimation not found %u:%u"
inline constexpr GameText kGraphicsDataFile{0x7a2ca4};   // "E:\Siedler3\Dirk\graphicsdata.cpp"
// CGraphicsData (Dirk\graphicsdata.cpp; the loader, gfx_loader.cpp)
inline constexpr GameText kLpMapping{0x7a2c94};          // "lpMapping [i]" (the constructor's out-of-memory check)
inline constexpr GameText kLpFile{0x7a2d5c};             // "lpFile" (Open's out-of-memory check)
inline constexpr GameText kDwResultNotZero{0x7a2cc8};    // "dwResult != 0" (GetCurrentDirectoryA)
inline constexpr GameText kBResult{0x696fec};            // "bResult" (SetCurrentDirectoryA, FindClose)
inline constexpr GameText kSiedler3Prefix{0x7a2d98};     // "siedler3_"
inline constexpr GameText kSiedler3Pattern{0x7a2dac};    // "siedler3_??."
inline constexpr GameText kDatExtension{0x7a2da4};       // ".dat"
inline constexpr GameText kGfxFolder{0x7a2d94};          // "Gfx"
inline constexpr GameText kDotGfxFolder{0x7a2d8c};       // ".\Gfx"
inline constexpr GameText kParentFolder{0x7a2d88};       // ".."
inline constexpr GameText kGfxPreparingDetach{0x7a2d64};  // "GFX: Preparing DAT files for detach"
inline constexpr GameText kGfxDetaching{0x7a2e70};       // "GFX: Detaching DAT files from medium ..."
inline constexpr GameText kWrongGfxFile{0x7a2d44};       // "Falsche Gfx-Datei %i" (wrong graphics file)
// "Wenn man Ctrl-C beim Assemblerlauf drückt, bekommt man halt ungültige
// DAT-Dateien" (press Ctrl-C while the assembler runs and you get invalid
// DAT files): the assertion text of a file whose magic is 0x1404.
inline constexpr GameText kGfxHeaderAssert{0x7a2cf0};
inline constexpr GameText kGfxPixelFormatAssert{0x7a2cd8};  // "Unsupported Pixelformat"
inline constexpr GameText kInvalidShadowMethod{0x7a2f10};  // "GFX: Invalid shadow method"
inline constexpr GameText kDDBltName{0x7a01e0};         // "CSurface::DDBlt"
inline constexpr GameText kDDBltFastName{0x7a01f0};     // "CSurface::DDBltFast"

// DirectX::CDirectSound and its buffers (directx_sound.cpp): DLL names and
// the "where" names CheckDSResult puts into the exceptions.
inline constexpr GameText kDsoundDll{0x7a0e58};             // "dsound.dll"
inline constexpr GameText kDirectSoundCreate{0x7a0e2c};     // "DirectSoundCreate"
inline constexpr GameText kDSConnect1{0x7a0e40};            // "CDirectSound::Connect.1"
inline constexpr GameText kDSConnect2{0x7a0e14};            // "CDirectSound::Connect.2"
inline constexpr GameText kDSConnect3{0x7a0dfc};            // "CDirectSound::Connect.3"
inline constexpr GameText kDSCreateSoundBuffer{0x7a0ebc};   // "CDirectSound::DSCreateSoundBuffer"
inline constexpr GameText kDSDuplicateSoundBuffer{0x7a0e64};  // "CDirectSound::DSDuplicateSoundBuffer"
inline constexpr GameText kDSGetStatus{0x7a0ee0};           // "CDirectSoundBuffer::DSGetStatus"
inline constexpr GameText kDSRestore{0x7a0f00};             // "CDirectSoundBuffer::DSRestore"
inline constexpr GameText kDSGetVolume{0x7a0f20};           // "CDirectSoundBuffer::DSGetVolume"
inline constexpr GameText kDSUnknownError{0x7a0d90};        // "Unknown Error Code"

// CSoundData (Dirk\SoundData.cpp; the sound loader, sound/sound_data.cpp)
inline constexpr GameText kSoundDataFile{0x7a3120};        // "E:\Siedler3\Dirk\SoundData.cpp"
inline constexpr GameText kSndFolder{0x7a3230};            // "Snd"
inline constexpr GameText kDotSndFolder{0x7a3228};         // ".\Snd"
inline constexpr GameText kSndPreparingDetach{0x7a3204};   // "SND: Preparing DAT files for detach"
inline constexpr GameText kSndDetaching{0x7a3314};         // "SND: Detaching DAT files from medium ..."
inline constexpr GameText kDSCannotCreate{0x7a32f0};       // "DSOUND: Cannot create DSound object"
inline constexpr GameText kDSSetCooperativeLevel{0x7a32cc};  // "CDirectSound::DSSetCooperativeLevel"
inline constexpr GameText kDSNoPriorityLevel{0x7a32a0};    // "DSOUND: Cannot optain priority coop level"
inline constexpr GameText kDSSetFormat{0x7a3270};          // "CPrimaryBuffer::DSSetFormat"
// "DSOUND: PrimaryBuffer is not capable of requested format"
inline constexpr GameText kDSFormatNotCapable{0x7a3234};
inline constexpr GameText kDSPlay{0x7a1250};                // "CDirectSoundBuffer::DSPlay"
inline constexpr GameText kDSSetVolume{0x7a126c};           // "CDirectSoundBuffer::DSSetVolume"
inline constexpr GameText kInvalidSoundFormat{0x7a328c};    // "Invalid soundformat"
inline constexpr GameText kLpPrimary{0x6991c0};             // "lpPrimary"
inline constexpr GameText kDSCannotRestore{0x7a31dc};       // "DSOUND: Cannot restore DSound object"
inline constexpr GameText kLpEffectBuffer{0x7a336c};        // "lpEffectBuffer"
inline constexpr GameText kGetEffectException{0x7a3340};    // "Exception during CSoundData::GetEffect (%x)"

// ------------------------------------------------------------ simulation (DebugLog formats)
inline constexpr GameText kGameDataInitialisingDone{0x6a5c38};  // "*** GAMEDATA Initialising done" (CGameData::FinishInitialising)
inline constexpr GameText kSectorsDone{0x72ff08};               // "sectors done" (CGameData::InitTileShadesAndMapEdge)
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
// Computer players (Rainer\CompMapWorkerAgent.cpp)
inline constexpr GameText kAiCountResourceDefault{0x733070};  // "RAINER - DEFAULT IN SWITCH CountResourceInOwnersLand"
inline constexpr GameText kAiCountAroundDefault{0x7330a8};  // "RAINER - DEFAULT IN SWITCH CountPlayersResourcesAround"
// Computer players (Rainer\CompDoBuildHouseReq.cpp, the residences condition 0x568890)
inline constexpr GameText kAiResidentialRandom{0x731bf4};  // "RAINER - RESIDENTIAL random wert = %u" (sprintf)
inline constexpr GameText kAiResidentialStage1{0x731bd4};  // "RAINER - RESIDENTIAL Stufe 1 "
inline constexpr GameText kAiResidentialStage2{0x731bb4};  // "RAINER - RESIDENTIAL Stufe 2 "
inline constexpr GameText kAiResidentialStage3{0x731b94};  // "RAINER - RESIDENTIAL Stufe 3 "
// Computer players (Rainer\compGlobs.cpp, CompHouseReq.cpp: ai_force_production.cpp)
inline constexpr GameText kForceProductionOn{0x7325f0};   // "FORCEPRODUCTION AN player: %u, ecosector %u" + newline
inline constexpr GameText kForceProductionOff{0x732620};  // "FORCEPRODUCTION AUS player: %u, ecosector %u" + newline
// "COMP_ForceProduction - EcoSector gehoert nicht dem Spieler !?!" + newline (o-umlaut in the
// binary; the sector isn't the player's)
inline constexpr GameText kForceProductionNotOwner{0x7325b0};
// "COMP_EmergencyPowersActWood - EcoSector gehoert nicht dem Spieler !?!" + newline
inline constexpr GameText kEmergencyWoodNotOwner{0x732698};
inline constexpr GameText kAiNoSuitableTool{0x73273c};  // "KEIN PASSENDES WERKZEUG VORHANDEN" + newline (no suitable tool)
inline constexpr GameText kAiNoChildNote{0x731b78};  // "Kein ChildNote gefunden! " + newline (no child note found)
// The out-of-memory checks of the request agents' TakeNote (Win32::ThrowOutOfMemory's text and file).
inline constexpr GameText kNotePtr{0x731b44};                 // "notePtr"
inline constexpr GameText kBuildHouseSourceFile{0x731b4c};    // "E:\Siedler3\Rainer\CompDoBuildHouseReq.cpp"
inline constexpr GameText kHouseRequestSourceFile{0x732718};  // "E:\Siedler3\Rainer\CompHouseReq.cpp"
// GLOBAL_EXPAND_EMPIRE_AGENT::MakeBuildOrders 0x56cd90 (Rainer\CompGExpand.cpp; DebugLog).
inline constexpr GameText kCompGExpandSourceFile{0x731c44};  // "E:\Siedler3\Rainer\CompGExpand.cpp"
inline constexpr GameText kExpandStart{0x732014};          // "RAINER: ***** GLOBAL_EXPAND  AUSDEHNEN *****"
inline constexpr GameText kExpandLongNotExpanded{0x731fe0};  // "RAINER GLOBAL_EXPAND  LANGE NICHT MEHR AUSGEDEHNT"
// Questions (no sites of that kind left): "RAINER: GLOBAL_EXPAND <resource>? "
inline constexpr GameText kExpandTreesQ{0x731fc0};      // "... B<a-umlaut>UME? " (trees)
inline constexpr GameText kExpandStonesQ{0x731fa0};     // "... STEINE? "
inline constexpr GameText kExpandIronQ{0x731f80};       // "... EISEN? "
inline constexpr GameText kExpandCoalQ{0x731f60};       // "... KOHLE? "
inline constexpr GameText kExpandGoldQ{0x731f40};       // "... GOLD? "
inline constexpr GameText kExpandFishQ{0x731f20};       // "... FISCH? "
inline constexpr GameText kExpandWaterQ{0x731f00};      // "... WASSER?"
inline constexpr GameText kExpandFarmlandQ{0x731edc};   // "... FARMLAND? "
inline constexpr GameText kExpandSwampQ{0x731ebc};      // "... SUMPF? " (rice land)
inline constexpr GameText kExpandSulfurQ{0x731e98};     // "... SCHWEFEL? "
inline constexpr GameText kExpandGemsQ{0x731e74};       // "... EDELSTEINE? "
inline constexpr GameText kExpandVineyardQ{0x731e50};   // "... WEINBERG? "
inline constexpr GameText kExpandResources{0x731e2c};   // "RAINER: GLOBAL_EXPAND ROHSTOFFE "
// "RAINER: GLOBAL_EXPANDkeine brauchbare Resource gefunden ES: %i" (no usable resource found; the area)
inline constexpr GameText kExpandNothingFound{0x731dec};
// Found: "RAINER: GLOBAL_EXPAND <resource>!"
inline constexpr GameText kExpandTreesFound{0x731dcc};
inline constexpr GameText kExpandStonesFound{0x731dac};  // "... STEIEN! " (sic)
inline constexpr GameText kExpandWaterFound{0x731d8c};
inline constexpr GameText kExpandFishFound{0x731d6c};
inline constexpr GameText kExpandSwampFound{0x731d54};   // "RAINER: GLOBAL_EXPAND  " (no word)
inline constexpr GameText kExpandSulfurFound{0x731d34};
inline constexpr GameText kExpandGemsFound{0x731d10};
inline constexpr GameText kExpandVineyardFound{0x731cec};
inline constexpr GameText kExpandCoalFound{0x731ccc};
inline constexpr GameText kExpandIronFound{0x731cac};
inline constexpr GameText kExpandGoldFound{0x731c8c};
inline constexpr GameText kExpandFarmlandFound{0x731c68};
// T_MAP_WORKER_AGENT::MakeBuildOrders 0x57ce90 (OutputDebugStringA; Rainer\CompMapWorkerAgent.cpp).
inline constexpr GameText kMapWorkerSourceFile{0x7327d8};  // "E:\Siedler3\Rainer\CompMapWorkerAgent.cpp"
inline constexpr GameText kBuildLargeTowerQ{0x7327b8};     // "ooo RAINER Baue BigTower ???" + newline
inline constexpr GameText kBuildLargeTowerGo{0x732798};    // "ooo RAINER Baue BigTower !!!" + newline
inline constexpr GameText kBuildCastleQ{0x73277c};         // "ooo RAINER Baue Castle ???" + newline
inline constexpr GameText kBuildCastleGo{0x732760};        // "ooo RAINER Baue Castle !!!" + newline
// The credits code's debug reports (ui/credits_debug.cpp; OutputDebugStringA).
inline constexpr GameText kFileLineFormat{0x7330e0};     // "%s %d"
inline constexpr GameText kCaughtUnknown{0x7330e8};      // "catched (...?): "
inline constexpr GameText kCaughtCmy{0x7330fc};          // "catched cmy: "
inline constexpr GameText kColonSpace{0x69a240};         // ": "
inline constexpr GameText kCreatingException{0x73310c};  // "creating exception: "
// "RAINER: Haus das fertig sein soll gibt es nicht mehr!" (the finished house no longer exists; AI::OnBuildingFinished)
inline constexpr GameText kAiFinishedHouseGone{0x7326e0};
// The out-of-memory checks of the land evaluation (CEvaLand::Acquire, Init).
inline constexpr GameText kEvalMap{0x7312e8};            // "pEvalMap"
inline constexpr GameText kEvaLandSourceFile{0x7312f4};  // "E:\Siedler3\Rainer\Cevaland.cpp"
inline constexpr GameText kMapPartListPtr{0x731314};     // "map_part_list_ptr"
// CEvaLand::ProcessPendingEvents 0x554980 (DebugLog texts).
inline constexpr GameText kAiSiteBuildingGone{0x7313b8};     // "RAINER: Haus das Gebaut werden soll gibt es nicht mehr!"
inline constexpr GameText kAiOwnerChangeBuildingGone{0x73137c};  // "RAINER: Haus das Besitzer wechseln soll gibt es nicht mehr!"
inline constexpr GameText kAiSameTypeStillThere{0x73134c};   // "RAINER: Hier ist noch ein Haus dieses Types!"
inline constexpr GameText kAiDemolitionMixup{0x731328};      // "RAINER: Durcheinander bei ABRISS!"
// COMP_FoodForMinesOptimum 0x572a30 (OutputDebugStringA texts; each ends in a line feed).
inline constexpr GameText kFoodForMinesHeader{0x73257c};     // "COMP_FoodForMinesOptimum player: %u, ecosector %u" (sprintf)
inline constexpr GameText kFoodForMinesNotOwned{0x732534};   // "COMP_FoodForMinesOptimum - EcoSector gehört nicht dem Spieler ???????"
inline constexpr GameText kFoodForMinesBread{0x732504};      // "*** COMP_FoodForMinesOptimum - Brot vorhanden"
inline constexpr GameText kFoodForMinesNoBread{0x7324d0};    // "*** COMP_FoodForMinesOptimum - KEIN Brot vorhanden"
inline constexpr GameText kFoodForMinesMeat{0x73249c};       // "*** COMP_FoodForMinesOptimum - Fleisch vorhanden"
inline constexpr GameText kFoodForMinesNoMeat{0x732464};     // "*** COMP_FoodForMinesOptimum - KEIN Fleisch vorhanden"
inline constexpr GameText kFoodForMinesFish{0x732434};       // "*** COMP_FoodForMinesOptimum - Fisch vorhanden"
inline constexpr GameText kFoodForMinesNoFish{0x7323fc};     // "*** COMP_FoodForMinesOptimum - KEIN Fisch vorhanden"
// COMP_FoodForMines 0x5724e0 (OutputDebugStringA texts; each ends in a line feed).
inline constexpr GameText kMineFoodNotOwned{0x7323bc};       // "COMP_FoodForMines - EcoSector gehört nicht dem Spieler ???????"
inline constexpr GameText kMineFoodEntry{0x73239c};          // "*** COMP_FoodForMines entry "
inline constexpr GameText kMineFoodForceProduction{0x732374};  // "*** COMP_FoodForMines ForceProduction"
inline constexpr GameText kMineFoodHeader{0x732334};         // "*** COMP_FoodForMines Food for mines player: %u, ecosector %u" (sprintf)
inline constexpr GameText kMineFoodCoalPercent{0x732300};    // "*** COMP_FoodForMines Food for coalmine percent %i" (sprintf)
inline constexpr GameText kMineFoodIronPercent{0x7322cc};    // "*** COMP_FoodForMines Food for oremine percent %i" (sprintf)
inline constexpr GameText kMineFoodGoldPercent{0x732298};    // "*** COMP_FoodForMines Food for goldmine percent %i" (sprintf)
// The out-of-memory check of AI::CreateAgent 0x56f6d0.
inline constexpr GameText kAgentPtr{0x73228c};              // "agentPtr"
inline constexpr GameText kCompGlobsSourceFile{0x732098};   // "E:\Siedler3\Rainer\compGlobs.cpp"
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
// "RESET wegen abgelaufenem Attackdelay player: %d, group: %d\n" (the attack delay ran out; AI::ArmyState::TickAttackDelays 0x594000)
inline constexpr GameText kAiAttackDelayReset{0x732e34};
// "Catapult fight - player: %d, group: %d, settler: %d \n" (AI::Army::AttackEnemyCatapults 0x593180)
inline constexpr GameText kAiCatapultFight{0x732dfc};
// "Gathering: player: %d, group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiGathering{0x732c9c};
// "Attack or Steal -  player: %d, group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiAttackOrSteal{0x732c60};
// "RAINER -> Eine Turm ist nicht besetzt : UnoccupiedTowers=true\n" (a tower has no garrison; AI::RunMilitary 0x58f990)
inline constexpr GameText kAiTowerUnoccupied{0x732c20};
// "Renew Patrouillie point 0 -player: %d,  group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiRenewPatrolPoint0{0x732bdc};
// "RAINER -> UnoccupiedTowers - n\xe4chster Turm gefunden!\n" (next tower found; AI::RunMilitary 0x58f990)
inline constexpr GameText kAiUnoccupiedTowerFound{0x732ba4};
// "RAINER -> UnoccupiedTowers - schickt Truppe zum Turm\n" (sends a troop to the tower; AI::RunMilitary 0x58f990)
inline constexpr GameText kAiSendTroopToTower{0x732b6c};
// "Next Step Patrouilie point 0 -player: %d, group: %d, building: %d \n" (both step bits set; AI::RunMilitary 0x58f990)
inline constexpr GameText kAiNextPatrolPoint0Both{0x732b28};
// "Next Step Patrouilie point 0 - player: %d, group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiNextPatrolPoint0{0x732ae0};
// "Next Step Patrouilie point 1 - player: %d, group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiNextPatrolPoint1{0x732a98};
// "New Patrouilie point 0 - player: %d, group: %d, building: %d \n" (AI::RunMilitary 0x58f990)
inline constexpr GameText kAiNewPatrolPoint0{0x732a44};
// "Marcus: FOE ATTACK EXCEPTION!" (DebugLog of T_ATTACK_AGENT::Run 0x55ec10 when RunMilitary throws)
inline constexpr GameText kAiFoeAttackException{0x731464};
// "Start Attack - player: %d, group: %d, building: %d \n" (AI::StartAttack 0x594080)
inline constexpr GameText kAiStartAttack{0x732edc};
// "Start Gather - player: %d, group: %d, building: %d \n" (AI::StartAttack 0x594080)
inline constexpr GameText kAiStartGather{0x732ea4};
// "Start Steal - player: %d, group: %d, building: %d \n" (AI::StartAttack 0x594080)
inline constexpr GameText kAiStartSteal{0x732e70};
// "Delete Catapult nr. %i Iterator: %x start: %x , end: %x, size: %i\n" (AI::ProcessWorldEvents 0x590da0, event 1/5 of a catapult)
inline constexpr GameText kAiDeleteCatapult{0x732d94};
// "player: %d, Army %d ist killed\n" (AI::ProcessWorldEvents 0x590da0)
inline constexpr GameText kAiArmyKilled{0x732d74};
// "Einigeln - player: %d, group: %d, building: %d \n" (dig in; AI::ProcessWorldEvents 0x590da0, event 4)
inline constexpr GameText kAiDigIn{0x732d40};
// "Defend Tower - player: %d, group: %d, building: %d \n" (AI::ProcessWorldEvents 0x590da0, event 4)
inline constexpr GameText kAiDefendTower{0x732d08};
// "WRONG EVENT - readID: %d type: %d data1: %d data2: %d" (DebugLog of AI::ProcessWorldEvents 0x590da0 for an unknown event type)
inline constexpr GameText kAiWrongEvent{0x732cd0};
}  // namespace s3re::game::texts
