// Members of the game's configuration object (g_config 0x7a7af0, struct
// GameConfig in game/globals.h; Dirk\DGlobals.cpp). Not a polymorphic
// class, so the name is ours. All thiscall unless noted; the getters are
// followed by nop padding, which Detours uses (CLAUDE.md, "Detours and
// tiny functions").
//
// GameConfig::LobbyValue68 (0x00413b60): () -> the dword at +0x68.
// GameConfig::SetLobbyValue68 (0x00413b70): (value) ret 4: +0x68 = value;
//   EAX = value. Callers: the lobby dialogs.
// GameConfig::GetLobbyPair148 (0x00413b80): (a*, b*) ret 8: *a = +0x148,
//   *b = +0x14c. EAX = the +0x14c value. Caller: CLobbyDialog's constructor.
// GameConfig::SetLobbyPair148 (0x00413ba0): (a, b) ret 8: +0x148 = a,
//   +0x14c = b. EAX = a. Caller: Lobby_sub_448960.
// GameConfig::Byte150 (0x00413bc0), RandomRaceAmazons (0x00413bd0),
//   GfxGroup61 (0x00413be0): () -> the byte at +0x150 / +0x158 / +0x159 in
//   AL (the rest of EAX is the caller's; ours zero-extends: every caller
//   tests AL or passes it on as a byte, checked for 0x413be0's callers in
//   CGraphicsData::Open and PathIndexForFileKind).
// GameConfig::CampaignMissionCD (0x00413bf0): (mission) ret 4, ECX unused:
//   3 for missions below 24, 4 below 48, else 5. The callers (CCampaignDialog,
//   CDrawThread::SetActualScreen / OnGameEnded) pass the result to
//   CDrawThread::PromptForCD 0x497040 as the CD to ask for, so probably the
//   CD a campaign mission is on (the base game, the Mission CD, the Quest of
//   the Amazons).
// GameConfig::StartMap (0x00413c20): (std::string* out) ret 4: out = a copy
//   of the string at +0x15c (the copy constructor inlined: the allocator
//   byte is the low byte of `out`, as in the original's stack slot, then
//   assign(x, 0, npos)). EAX = out. Callers: CDrawThread::OnStart / Update,
//   which pass it to GameSettings 0x431ab0, so probably the map to start.
// GameConfig::ClearStartMap (0x00413d50): () the string at +0x15c =
//   "" (assign_cstr_or_empty 0x413d70 with 0x7a7868); EAX = its.
// GameConfig::PathIndexForFileKind (0x00414050): (kind) ret 4 on g_config
//   itself (ECX unused, the original loads 0x7a7af0 for GfxGroup61): maps a
//   file kind 1..16 to a path slot through one of two tables, picked by
//   GfxGroup61 0x413be0 (called through its address); other kinds: the
//   DGlobals.cpp assert (Win32::ThrowAssertmentFailed 0x6119b0, "Unknown
//   function", line 0x652 or 0x67a), which throws.

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGfxGroup61 = 0x00413be0;              // GameConfig::GfxGroup61 (ours)
constexpr std::uintptr_t kAssignSubstr = 0x00401d60;            // std::string::assign(x, pos, n) (ours)
constexpr std::uintptr_t kAssignTextOrEmpty = 0x00413d70;       // std::string::assign_cstr_or_empty (ours)
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;   // Win32::ThrowAssertmentFailed (ours)
constexpr std::uintptr_t kStringTidy = 0x00402170;              // std::string::_Tidy (ours)
constexpr std::uintptr_t kIsDigit = 0x0061f824;                 // the CRT's isdigit

constexpr std::int32_t kFirstMissionCDMission = 24;  // missions 0..23: CD 3
constexpr std::int32_t kSecondMissionCDMission = 48;  // 24..47: CD 4, then 5

// PathIndexForFileKind's two tables (0x4140f4 and 0x414134 dispatch to
// these results), for kinds 1..16.
constexpr std::int32_t kPathSlotDefault[16] = {0, -1, 1, 2, 3, -1, -1, -1, 3, -1, -2, 3, 3, -1, -2, 3};
constexpr std::int32_t kPathSlotGroup61[16] = {4, 4, 5, 5, 4, 4, -3, -3, 4, -3, -3, 4, 4, -3, 4, 4};
constexpr std::int32_t kAssertLineDefault = 0x652;
constexpr std::int32_t kAssertLineGroup61 = 0x67a;

}  // namespace

using ConfigGetterFn = std::uint32_t(__fastcall*)(const GameConfig* self, void* edx);
using ConfigSetFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx, std::uint32_t value);
using ConfigGetPairFn = std::uint32_t(__fastcall*)(const GameConfig* self, void* edx, std::uint32_t* a,
                                                   std::uint32_t* b);
using ConfigSetPairFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx, std::uint32_t a, std::uint32_t b);
using ConfigMissionCDFn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::int32_t mission);
using ConfigStartMapFn = msvc6::String*(__fastcall*)(const GameConfig* self, void* edx, msvc6::String* out);
using ConfigClearFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx);
using ConfigPathIndexFn = std::int32_t(__fastcall*)(const void* self, void* edx, std::int32_t kind);
using ConfigPathStringFn = msvc6::String*(__fastcall*)(const GameConfig* self, void* edx, msvc6::String* out,
                                                       std::uint32_t index);
using ConfigCheckDigitFn = std::uint32_t(__fastcall*)(const void* self, void* edx, const char* text);

// Purpose: Reads a configuration value the lobby dialogs use.
std::uint32_t __fastcall GameConfig_LobbyValue68(const GameConfig* self, void*) {
    return self->lobby_value68;
}

// Purpose: Stores a configuration value the lobby dialogs use.
std::uint32_t __fastcall GameConfig_SetLobbyValue68(GameConfig* self, void*, std::uint32_t value) {
    self->lobby_value68 = value;
    return value;
}

// Purpose: Gives two configuration values the lobby dialog shows (probably
// remembered between sessions).
std::uint32_t __fastcall GameConfig_GetLobbyPair148(const GameConfig* self, void*, std::uint32_t* a,
                                                    std::uint32_t* b) {
    *a = self->lobby_pair148[0];
    const std::uint32_t second = self->lobby_pair148[1];
    *b = second;
    return second;
}

// Purpose: Stores the two configuration values the lobby dialog keeps.
std::uint32_t __fastcall GameConfig_SetLobbyPair148(GameConfig* self, void*, std::uint32_t a, std::uint32_t b) {
    self->lobby_pair148[0] = a;
    self->lobby_pair148[1] = b;
    return a;
}

// Purpose: Reads a configuration flag the main menu and the load dialog
// check. The original only loads AL (see the header).
std::uint32_t __fastcall GameConfig_Byte150(const GameConfig* self, void*) {
    return self->byte150;
}

// Purpose: Reads a configuration flag (the game settings copy it as their
// "random race may be Amazons" flag; the menus check it too).
std::uint32_t __fastcall GameConfig_RandomRaceAmazons(const GameConfig* self, void*) {
    return self->random_race_amazons;
}

// Purpose: Reads the configuration flag that makes the graphics loader load
// GFX group 61 and picks the second path table.
std::uint32_t __fastcall GameConfig_GfxGroup61(const GameConfig* self, void*) {
    return self->gfx_group61;
}

// Purpose: The CD the game asks for before a campaign mission (probably).
std::uint32_t __fastcall GameConfig_CampaignMissionCD(const void*, void*, std::int32_t mission) {
    if (mission < kFirstMissionCDMission) {
        return 3;
    }
    return mission >= kSecondMissionCDMission ? 5 : 4;
}

// Purpose: Gives a copy of the configuration's start map name (probably the
// map given on the command line).
msvc6::String* __fastcall GameConfig_StartMap(const GameConfig* self, void*, msvc6::String* out) {
    // The inlined copy constructor: the allocator byte comes from the
    // argument's stack slot (the low byte of `out`), then _Tidy(false) and
    // assign(x, 0, npos).
    out->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(out));
    out->ptr = nullptr;
    out->len = 0;
    out->res = 0;
    const std::uint32_t npos = g_stringNpos.get();  // read from its static member, as the original does
    reinterpret_cast<msvc6::StringAssignSubstrFn>(kAssignSubstr)(out, nullptr, &self->start_map, 0, npos);
    return out;
}

// Purpose: Forgets the start map name once the menu has used it.
std::uint32_t __fastcall GameConfig_ClearStartMap(GameConfig* self, void*) {
    using AssignFn = std::uint32_t(__fastcall*)(msvc6::String* self, void* edx, const char* text);
    return reinterpret_cast<AssignFn>(kAssignTextOrEmpty)(&self->start_map, nullptr, texts::kEmpty);
}

// Purpose: Which of the configured data paths holds the files of a kind
// (maps, savegames, ...); negative values are special slots.
std::int32_t __fastcall GameConfig_PathIndexForFileKind(const void*, void*, std::int32_t kind) {
    const bool group61 =
        static_cast<std::uint8_t>(reinterpret_cast<ConfigGetterFn>(kGfxGroup61)(&g_config, nullptr)) != 0;
    const std::uint32_t index = static_cast<std::uint32_t>(kind - 1);
    if (index < 16) {
        return group61 ? kPathSlotGroup61[index] : kPathSlotDefault[index];
    }
    using ThrowFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);
    reinterpret_cast<ThrowFn>(kThrowAssertmentFailed)(texts::kUnknownFunction, texts::kDGlobalsFile,
                                                      group61 ? kAssertLineGroup61 : kAssertLineDefault);
    return 0;  // (not reached: the assert throws)
}

// Purpose: Gives a copy of the configuration's serial number (CMainMenu::
// CheckSerialNumber and the lobby read it).
msvc6::String* __fastcall GameConfig_SerialNumber(const GameConfig* self, void*, msvc6::String* out) {
    // The same inlined copy constructor as StartMap's.
    out->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(out));
    out->ptr = nullptr;
    out->len = 0;
    out->res = 0;
    const std::uint32_t npos = g_stringNpos.get();
    reinterpret_cast<msvc6::StringAssignSubstrFn>(kAssignSubstr)(out, nullptr, &self->string04, 0, npos);
    return out;
}

// Purpose: Gives a copy of one of the configured data paths (the slot
// PathIndexForFileKind picks; 5 and anything else give the last one).
msvc6::String* __fastcall GameConfig_PathString(const GameConfig* self, void*, msvc6::String* out,
                                                std::uint32_t index) {
    out->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(out));
    constexpr std::uint32_t kLastPath = 5;
    if (index < kLastPath) {
        reinterpret_cast<msvc6::StringTidyFn>(kStringTidy)(out, nullptr, 0);
    } else {
        out->ptr = nullptr;  // (the last case has _Tidy(false) inlined)
        out->len = 0;
        out->res = 0;
    }
    // n is the constant -1 here, not read from npos.
    reinterpret_cast<msvc6::StringAssignSubstrFn>(kAssignSubstr)(out, nullptr,
                                                                 &self->paths[index < kLastPath ? index : kLastPath],
                                                                 0, 0xffffffffu);
    return out;
}

namespace {

// A dword of one of the serial tables at `index` (any index: the original
// reads past the tables; 32-bit address arithmetic as in the original).
std::int32_t SerialTable(const std::int32_t& table, std::int32_t index) {
    const std::uint32_t address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&table)) +
                                  static_cast<std::uint32_t>(index) * 4u;
    return *reinterpret_cast<const std::int32_t*>(static_cast<std::uintptr_t>(address));  // raw-ok: table read past its end
}

constexpr std::int32_t kSerialPositions = 12;
constexpr std::int32_t kSerialRow = 10;

}  // namespace

// Purpose: Computes the check digit of a serial number (or of one part of
// it): a Verhoeff-like checksum over the digits; a non-digit starts a second
// sum, the second non-digit ends the text. Returns the digit as a character.
std::uint32_t __fastcall GameConfig_SerialCheckDigit(const void*, void*, const char* text) {
    using IsDigitFn = int(__cdecl*)(int c);
    std::int32_t first = 0;    // the sum before the separator
    std::int32_t second = -1;  // the sum after it (-1: no separator yet)
    std::int32_t separator = -1;
    for (std::int32_t i = 0; text[i] != 0; ++i) {
        const int c = static_cast<signed char>(text[i]);
        if (reinterpret_cast<IsDigitFn>(kIsDigit)(c) != 0 && i != separator) {
            // The character code, not the digit, indexes the row (reads
            // past the position table; reproduced).
            const std::int32_t permuted = SerialTable(g_serialPermute.get(), (i % kSerialPositions) * kSerialRow + c);
            if (second >= 0) {
                second = SerialTable(g_serialMultiply.get(), second * kSerialRow + permuted);
            } else {
                first = SerialTable(g_serialMultiply.get(), first * kSerialRow + permuted);
            }
            continue;
        }
        if (separator >= 0 && i != separator) {
            break;
        }
        separator = i;
        second = 0;
    }
    const std::int32_t combined = SerialTable(g_serialInverse.get(), first) * kSerialRow +
                                  SerialTable(g_serialInverse.get(), second);
    const std::int32_t value = SerialTable(g_serialMultiply.get(), combined);
    return static_cast<std::uint32_t>(SerialTable(g_serialFinal.get(), (separator % kSerialPositions) * kSerialRow + value) + '0');
}

S3RE_HOOK(0x00413b60, GameConfig_LobbyValue68, ConfigGetterFn, Original_GameConfig_LobbyValue68,
          GameConfig_LobbyValue68);
S3RE_HOOK(0x00413b70, GameConfig_SetLobbyValue68, ConfigSetFn, Original_GameConfig_SetLobbyValue68,
          GameConfig_SetLobbyValue68);
S3RE_HOOK(0x00413b80, GameConfig_GetLobbyPair148, ConfigGetPairFn, Original_GameConfig_GetLobbyPair148,
          GameConfig_GetLobbyPair148);
S3RE_HOOK(0x00413ba0, GameConfig_SetLobbyPair148, ConfigSetPairFn, Original_GameConfig_SetLobbyPair148,
          GameConfig_SetLobbyPair148);
S3RE_HOOK(0x00413bc0, GameConfig_Byte150, ConfigGetterFn, Original_GameConfig_Byte150, GameConfig_Byte150);
S3RE_HOOK(0x00413bd0, GameConfig_RandomRaceAmazons, ConfigGetterFn, Original_GameConfig_RandomRaceAmazons,
          GameConfig_RandomRaceAmazons);
S3RE_HOOK(0x00413be0, GameConfig_GfxGroup61, ConfigGetterFn, Original_GameConfig_GfxGroup61, GameConfig_GfxGroup61);
S3RE_HOOK(0x00413bf0, GameConfig_CampaignMissionCD, ConfigMissionCDFn, Original_GameConfig_CampaignMissionCD,
          GameConfig_CampaignMissionCD);
S3RE_HOOK(0x00413c20, GameConfig_StartMap, ConfigStartMapFn, Original_GameConfig_StartMap, GameConfig_StartMap);
S3RE_HOOK(0x00413d50, GameConfig_ClearStartMap, ConfigClearFn, Original_GameConfig_ClearStartMap,
          GameConfig_ClearStartMap);
S3RE_HOOK(0x00414050, GameConfig_PathIndexForFileKind, ConfigPathIndexFn, Original_GameConfig_PathIndexForFileKind,
          GameConfig_PathIndexForFileKind);
S3RE_HOOK(0x00411a90, GameConfig_SerialNumber, ConfigStartMapFn, Original_GameConfig_SerialNumber,
          GameConfig_SerialNumber);
S3RE_HOOK(0x00412b00, GameConfig_PathString, ConfigPathStringFn, Original_GameConfig_PathString, GameConfig_PathString);
S3RE_HOOK(0x00411fd0, GameConfig_SerialCheckDigit, ConfigCheckDigitFn, Original_GameConfig_SerialCheckDigit,
          GameConfig_SerialCheckDigit);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct ConfigFunctions {
    ConfigGetterFn lobbyValue68;
    ConfigSetFn setLobbyValue68;
    ConfigGetPairFn getPair;
    ConfigSetPairFn setPair;
    ConfigGetterFn byte150;
    ConfigGetterFn randomRaceAmazons;
    ConfigGetterFn gfxGroup61;
    ConfigMissionCDFn missionCD;
    ConfigStartMapFn startMap;
    ConfigClearFn clearStartMap;
    ConfigPathIndexFn pathIndex;
    ConfigStartMapFn serialNumber;
    ConfigPathStringFn pathString;
    ConfigCheckDigitFn checkDigit;
};

ConfigFunctions OriginalConfigFunctions() {
    return {Original_GameConfig_LobbyValue68,   Original_GameConfig_SetLobbyValue68,
            Original_GameConfig_GetLobbyPair148, Original_GameConfig_SetLobbyPair148,
            Original_GameConfig_Byte150,         Original_GameConfig_RandomRaceAmazons,
            Original_GameConfig_GfxGroup61,      Original_GameConfig_CampaignMissionCD,
            Original_GameConfig_StartMap,        Original_GameConfig_ClearStartMap,
            Original_GameConfig_PathIndexForFileKind,
            Original_GameConfig_SerialNumber,    Original_GameConfig_PathString,
            Original_GameConfig_SerialCheckDigit};
}

ConfigFunctions OurConfigFunctions() {
    return {&GameConfig_LobbyValue68,   &GameConfig_SetLobbyValue68, &GameConfig_GetLobbyPair148,
            &GameConfig_SetLobbyPair148, &GameConfig_Byte150,        &GameConfig_RandomRaceAmazons,
            &GameConfig_GfxGroup61,      &GameConfig_CampaignMissionCD, &GameConfig_StartMap,
            &GameConfig_ClearStartMap,   &GameConfig_PathIndexForFileKind,
            &GameConfig_SerialNumber,    &GameConfig_PathString,      &GameConfig_SerialCheckDigit};
}

constexpr std::size_t kConfigBytes = 0x200;

// The plain getters and setters on random objects.
bool CompareAccessors(test::Context& ctx, const ConfigFunctions& ours, int cases) {
    const ConfigFunctions original = OriginalConfigFunctions();
    std::vector<std::uint8_t> a(kConfigBytes), b(kConfigBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        auto* ca = reinterpret_cast<GameConfig*>(a.data());
        auto* cb = reinterpret_cast<GameConfig*>(b.data());
        const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
        std::uint32_t outA[3] = {1, 2, 3}, outB[3] = {1, 2, 3};
        const bool same = ctx.rng().Chance(1, 4);  // both outputs the same address
        bool ok = S3RE_EXPECT_EQ(original.lobbyValue68(ca, nullptr), ours.lobbyValue68(cb, nullptr)) &&
                  S3RE_EXPECT_EQ(original.byte150(ca, nullptr) & 0xff, ours.byte150(cb, nullptr) & 0xff) &&
                  S3RE_EXPECT_EQ(original.randomRaceAmazons(ca, nullptr) & 0xff,
                                 ours.randomRaceAmazons(cb, nullptr) & 0xff) &&
                  S3RE_EXPECT_EQ(original.gfxGroup61(ca, nullptr) & 0xff, ours.gfxGroup61(cb, nullptr) & 0xff) &&
                  S3RE_EXPECT_EQ(original.getPair(ca, nullptr, &outA[0], &outA[same ? 0 : 1]),
                                 ours.getPair(cb, nullptr, &outB[0], &outB[same ? 0 : 1])) &&
                  S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA)) &&
                  S3RE_EXPECT_EQ(original.setLobbyValue68(ca, nullptr, x), ours.setLobbyValue68(cb, nullptr, x)) &&
                  S3RE_EXPECT_EQ(original.setPair(ca, nullptr, x, y), ours.setPair(cb, nullptr, x, y));
        // Missions around the two limits, and any value.
        const std::int32_t mission = ctx.rng().Chance(1, 4) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                            : ctx.rng().Range(-3, 52);
        ok = ok && S3RE_EXPECT_EQ(original.missionCD(ca, nullptr, mission), ours.missionCD(cb, nullptr, mission)) &&
             S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!ok) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

using FromCharsFn = msvc6::String*(__fastcall*)(msvc6::String* self, void* edx, const char* s, std::uint32_t n);

// A string member that is `text` (empty: no buffer) with the given
// reference count byte.
void MakeMember(msvc6::String& s, const char* text, std::uint8_t count) {
    s.ptr = nullptr;
    s.len = 0;
    s.res = 0;
    if (text[0] != 0) {
        reinterpret_cast<FromCharsFn>(0x00401a10)(&s, nullptr, text, 0xffffffff);
        s.ptr[-1] = static_cast<char>(count);
    }
}

void FreeString(msvc6::String* s) {
    using DestroyFn = void(__fastcall*)(msvc6::String* self, void* edx);
    if (s->ptr != nullptr) {
        s->ptr[-1] = 0;  // unshared: the destructor frees it
    }
    reinterpret_cast<DestroyFn>(0x00401b60)(s, nullptr);
}

// A string's allocator byte relative to the low byte of its own address
// (the copy takes it from there, and the two runs use different buffers).
std::uint8_t LowByteDelta(const msvc6::String& s) {
    return static_cast<std::uint8_t>(s.allocator - static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(&s)));
}

enum class CopyOp { StartMap, ClearStartMap, SerialNumber, PathString };

// The string member an operation reads.
msvc6::String& SourceOf(GameConfig* config, CopyOp op, std::uint32_t index) {
    switch (op) {
    case CopyOp::SerialNumber:
        return config->string04;
    case CopyOp::PathString:
        return config->paths[index < 5 ? index : 5];
    default:
        return config->start_map;
    }
}

// The string copies (StartMap, SerialNumber, PathString) and ClearStartMap
// on configs whose string is empty, unshared, shared, nearly full of
// references or frozen; the copy goes to a separate string or (rarely) to
// the source itself.
bool CompareStringMembers(test::Context& ctx, const ConfigFunctions& ours, int cases) {
    const ConfigFunctions original = OriginalConfigFunctions();
    static const char* const kTexts[] = {"", "Map\\User\\Test.map", "x", "Ein langer Kartenname mit Leerzeichen"};
    static const std::uint8_t kCounts[] = {0, 1, 0xfd, 0xfe, 0xff};
    for (int i = 0; i < cases; ++i) {
        alignas(4) std::uint8_t bufA[kConfigBytes], bufB[kConfigBytes];
        ctx.rng().Fill(bufA, sizeof(bufA));
        std::memcpy(bufB, bufA, sizeof(bufA));
        auto* ca = reinterpret_cast<GameConfig*>(bufA);
        auto* cb = reinterpret_cast<GameConfig*>(bufB);
        const auto op = static_cast<CopyOp>(ctx.rng().Below(4));
        const std::uint32_t index = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(7);
        const char* text = kTexts[ctx.rng().Below(4)];
        const std::uint8_t count = kCounts[ctx.rng().Below(5)];
        msvc6::String& sa = SourceOf(ca, op, index);
        msvc6::String& sb = SourceOf(cb, op, index);
        MakeMember(sa, text, count);
        MakeMember(sb, text, count);
        const bool clear = op == CopyOp::ClearStartMap;
        const bool self = !clear && ctx.rng().Chance(1, 8);
        // Separate outputs at the same offset of two buffers (the allocator
        // byte is the low byte of the address).
        alignas(16) std::uint8_t outsA[64], outsB[64];
        const std::size_t at = ctx.rng().Below(12) * 4;
        auto* outA = self ? &sa : reinterpret_cast<msvc6::String*>(outsA + at);
        auto* outB = self ? &sb : reinterpret_cast<msvc6::String*>(outsB + at);
        bool ok = true;
        if (clear) {
            const std::uint32_t ea = original.clearStartMap(ca, nullptr);
            const std::uint32_t eb = ours.clearStartMap(cb, nullptr);
            ok = S3RE_EXPECT_EQ(ea - reinterpret_cast<std::uint32_t>(ca), eb - reinterpret_cast<std::uint32_t>(cb));
        } else {
            const msvc6::String* ra = op == CopyOp::StartMap       ? original.startMap(ca, nullptr, outA)
                                      : op == CopyOp::SerialNumber ? original.serialNumber(ca, nullptr, outA)
                                                                   : original.pathString(ca, nullptr, outA, index);
            const msvc6::String* rb = op == CopyOp::StartMap       ? ours.startMap(cb, nullptr, outB)
                                      : op == CopyOp::SerialNumber ? ours.serialNumber(cb, nullptr, outB)
                                                                   : ours.pathString(cb, nullptr, outB, index);
            ok = S3RE_EXPECT(ra == outA && rb == outB) &&
                 S3RE_EXPECT_EQ(LowByteDelta(*outA), LowByteDelta(*outB)) &&
                 S3RE_EXPECT_EQ(outA->len, outB->len) && S3RE_EXPECT_EQ(outA->res, outB->res) &&
                 S3RE_EXPECT_EQ(outA->ptr == sa.ptr, outB->ptr == sb.ptr) &&
                 S3RE_EXPECT_EQ(outA->ptr == nullptr, outB->ptr == nullptr);
            if (ok && outA->ptr != nullptr) {
                ok = S3RE_EXPECT(std::strcmp(outA->ptr, outB->ptr) == 0) && S3RE_EXPECT_EQ(outA->ptr[-1], outB->ptr[-1]);
            }
        }
        // The source itself: its fields and count, the rest of the config unchanged.
        const std::size_t member = static_cast<std::size_t>(reinterpret_cast<std::uint8_t*>(&sa) - bufA);
        ok = ok && S3RE_EXPECT_EQ(sa.len, sb.len) && S3RE_EXPECT_EQ(sa.res, sb.res) &&
             S3RE_EXPECT_EQ(self ? LowByteDelta(sa) : sa.allocator, self ? LowByteDelta(sb) : sb.allocator) &&
             S3RE_EXPECT_EQ(sa.ptr == nullptr, sb.ptr == nullptr) &&
             S3RE_EXPECT(sa.ptr == nullptr || (std::strcmp(sa.ptr, sb.ptr) == 0 && sa.ptr[-1] == sb.ptr[-1])) &&
             S3RE_EXPECT(std::memcmp(bufA, bufB, member) == 0) &&
             S3RE_EXPECT(std::memcmp(bufA + member + 16, bufB + member + 16, sizeof(bufA) - member - 16) == 0);
        if (!clear && !self) {
            if (outA->ptr != sa.ptr) FreeString(outA);
            if (outB->ptr != sb.ptr) FreeString(outB);
        }
        FreeString(&sa);
        FreeString(&sb);
        if (!ok) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (op %d, index %u, text \"%s\", count %u, self %d)", i,
                     static_cast<int>(op), index, text, count, self);
            return false;
        }
    }
    return true;
}

// The check digit on serial-like texts: digits, a separator or two,
// letters, high characters, empty.
bool CompareCheckDigit(test::Context& ctx, const ConfigFunctions& ours, int cases) {
    const ConfigFunctions original = OriginalConfigFunctions();
    static const char kChars[] = "0123456789012345678901234567890123456789--- ABCxyz\xe4\xfc\x80\xff";
    for (int i = 0; i < cases; ++i) {
        char text[40] = {};
        const std::uint32_t length = ctx.rng().Below(30);
        const bool digitsOnly = ctx.rng().Chance(1, 3);
        for (std::uint32_t k = 0; k < length; ++k) {
            text[k] = digitsOnly ? static_cast<char>('0' + ctx.rng().Below(10))
                                 : kChars[ctx.rng().Below(sizeof(kChars) - 1)];
        }
        const std::uint32_t ea = original.checkDigit(nullptr, nullptr, text);
        const std::uint32_t eb = ours.checkDigit(nullptr, nullptr, text);
        if (!S3RE_EXPECT_EQ(ea, eb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, text \"%s\"", i, text);
            return false;
        }
    }
    return true;
}

// PathIndexForFileKind with g_config's flag set either way; kinds outside
// 1..16 throw the assert (caught and compared).
struct PathResult {
    std::int32_t value = 0;
    test::CaughtException caught;
};

PathResult RunPathIndex(ConfigPathIndexFn fn, std::int32_t kind) {
    PathResult result;
    __try {
        result.value = fn(&g_config, nullptr, kind);
    } __except (test::CatchGameException(GetExceptionInformation(), &result.caught)) {
    }
    return result;
}

bool ComparePathIndex(test::Context& ctx, const ConfigFunctions& ours, int cases) {
    const ConfigFunctions original = OriginalConfigFunctions();
    std::uint8_t& flag = g_config->gfx_group61;
    const std::uint8_t saved = flag;
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        flag = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint8_t>(ctx.rng().Range(1, 255));
        const std::int32_t kind = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                          : ctx.rng().Range(-1, 18);
        const PathResult a = RunPathIndex(original.pathIndex, kind);
        const PathResult b = RunPathIndex(ours.pathIndex, kind);
        if (!S3RE_EXPECT(a.caught == b.caught) || !S3RE_EXPECT_EQ(a.value, b.value)) {
            ctx.Fail(__FILE__, __LINE__, "  kind %d, flag %u", kind, flag);
            ok = false;
        }
    }
    flag = saved;
    return ok;
}

// Wrong versions for the harness checks.
std::uint32_t __fastcall WrongMissionCD(const void*, void*, std::int32_t mission) {
    return mission <= kFirstMissionCDMission ? 3 : (mission >= kSecondMissionCDMission ? 5 : 4);
}
msvc6::String* __fastcall WrongStartMap(const GameConfig* self, void* edx, msvc6::String* out) {
    GameConfig_StartMap(self, edx, out);
    out->allocator = 0;
    return out;
}
std::int32_t __fastcall WrongPathIndex(const void* self, void* edx, std::int32_t kind) {
    return kind == 11 ? -1 : GameConfig_PathIndexForFileKind(self, edx, kind);
}
msvc6::String* __fastcall WrongPathString(const GameConfig* self, void* edx, msvc6::String* out, std::uint32_t index) {
    GameConfig_PathString(self, edx, out, index);
    out->allocator = static_cast<std::uint8_t>(out->allocator + (index >= 5 ? 1 : 0));  // the default case's byte off
    return out;
}
std::uint32_t __fastcall WrongCheckDigit(const void* self, void* edx, const char* text) {
    // Uses the digit's value instead of its character code.
    char copy[64] = {};
    for (int i = 0; i < 63 && text[i] != 0; ++i) {
        copy[i] = text[i] >= '5' && text[i] <= '9' ? static_cast<char>(text[i] - 5) : text[i];
    }
    return GameConfig_SerialCheckDigit(self, edx, copy);
}

}  // namespace

S3RE_TEST(GameConfig_MatchesOriginal) {
    const ConfigFunctions ours = OurConfigFunctions();
    CompareAccessors(ctx, ours, 2000) && CompareStringMembers(ctx, ours, 2000) && ComparePathIndex(ctx, ours, 500) &&
        CompareCheckDigit(ctx, ours, 3000);
}

S3RE_TEST(GameConfig_ComparisonDetectsWrongImplementation) {
    const struct {
        const char* name;
        void (*patch)(ConfigFunctions&);
        int which;
    } kWrong[] = {
        {"mission CD", [](ConfigFunctions& f) { f.missionCD = &WrongMissionCD; }, 0},
        {"start map", [](ConfigFunctions& f) { f.startMap = &WrongStartMap; }, 1},
        {"path index", [](ConfigFunctions& f) { f.pathIndex = &WrongPathIndex; }, 2},
        {"path string", [](ConfigFunctions& f) { f.pathString = &WrongPathString; }, 1},
        {"check digit", [](ConfigFunctions& f) { f.checkDigit = &WrongCheckDigit; }, 3},
    };
    for (const auto& wrong : kWrong) {
        ConfigFunctions candidate = OurConfigFunctions();
        wrong.patch(candidate);
        test::Context quiet("wrong", 7, true);
        const bool passed = wrong.which == 0   ? CompareAccessors(quiet, candidate, 2000)
                            : wrong.which == 1 ? CompareStringMembers(quiet, candidate, 1000)
                            : wrong.which == 2 ? ComparePathIndex(quiet, candidate, 500)
                                               : CompareCheckDigit(quiet, candidate, 3000);
        if (!S3RE_EXPECT(!passed)) {
            ctx.Fail(__FILE__, __LINE__, "  the wrong %s was not caught", wrong.name);
        }
    }
}

}  // namespace s3re::game
