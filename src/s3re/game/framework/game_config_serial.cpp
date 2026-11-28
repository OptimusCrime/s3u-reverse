// The serial number check of the game's configuration (g_config 0x7a7af0;
// Dirk\ConsoleInit.cpp).
//
// GameConfig::IsValidSerial (0x00411d10): (const std::string* serial) ret 4
//   -> AL 1 when the serial is well formed, else 0. Thiscall; ECX is only
//   passed on to SerialCheckDigit 0x411fd0 (which ignores it). The serial
//   must be 24 characters with '-' at 4, 9, 14 and 19 (operator[] const
//   0x414370), differ from each of the three blacklisted serials of the
//   table at 0x697108 (compare(const char*) 0x40a0e0; a null entry is
//   compared as "" through compare(0, length, "", 0) 0x40a140), and carry
//   two check digits: character 6 is the check digit of the characters 5,
//   12, 2, 20, 17, 8, 21, 7, 22 and character 3 that of 16, 0, 10, 11, 18,
//   23, 15, 1, 13. Character 6 is read through operator[] const, the others
//   inline (the buffer + i, or the static empty string 0x6553a0 itself
//   without a buffer). Before returning 1 it converts both digit strings
//   with the CRT's atoi (0x61f39d) and drops the results. Callers:
//   GameConfig::ReadRegistry (App_sub_40d700), CMainMenu::CheckSerialNumber,
//   Lobby_sub_439540. Was App_sub_411d10.

#include "game/tables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace s3re::game {

namespace {

constexpr std::uint32_t kSerialLength = 24;
constexpr std::uint32_t kDashPositions[] = {4, 9, 14, 19};
constexpr std::uint32_t kBlacklistEntries = 3;
// The characters each check digit covers, in the order they are copied.
constexpr std::uint32_t kFirstPart[] = {5, 12, 2, 20, 17, 8, 21, 7, 22};
constexpr std::uint32_t kSecondPart[] = {16, 0, 10, 11, 18, 23, 15, 1, 13};
constexpr std::uint32_t kFirstCheckPosition = 6;
constexpr std::uint32_t kSecondCheckPosition = 3;

using IndexConstFn = const char*(__fastcall*)(const msvc6::String* self, void* edx, std::uint32_t pos);
using CompareTextFn = std::int32_t(__fastcall*)(const msvc6::String* self, void* edx, const char* s);
using CompareCharsFn = std::int32_t(__fastcall*)(const msvc6::String* self, void* edx, std::uint32_t pos,
                                                 std::uint32_t n0, const char* s, std::uint32_t count);
using CheckDigitFn = std::uint32_t(__fastcall*)(const void* self, void* edx, const char* text);
using AtoiFn = int(__cdecl*)(const char* text);
constexpr std::uintptr_t kIndexConst = 0x00414370;   // std::string::operator[] const (verified)
constexpr std::uintptr_t kCompareText = 0x0040a0e0;  // std::string::compare(const char*) (verified)
constexpr std::uintptr_t kCompareChars = 0x0040a140; // std::string::compare(pos, n0, s, count) (verified)
constexpr std::uintptr_t kCheckDigit = 0x00411fd0;   // GameConfig::SerialCheckDigit (verified)
constexpr std::uintptr_t kAtoi = 0x0061f39d;         // the CRT's atoi

char SerialChar(const msvc6::String* serial, std::uint32_t pos) {
    return reinterpret_cast<IndexConstFn>(kIndexConst)(serial, nullptr, pos)[0];
}

// The inlined operator[] const: the buffer + pos, or the static empty
// string itself (pos ignored) without a buffer.
char InlineChar(const msvc6::String* serial, std::uint32_t pos) {
    return serial->ptr != nullptr ? serial->ptr[pos] : g_stringNullBuffer.get()[0];
}

bool IsBlacklisted(const msvc6::String* serial) {
    for (std::uint32_t i = 0; i < kBlacklistEntries; ++i) {
        const char* entry = g_serialBlacklist.get()[i];
        std::int32_t order;
        if (entry != nullptr) {
            order = reinterpret_cast<CompareTextFn>(kCompareText)(serial, nullptr, entry);
        } else {
            // 0x411d8c: the inlined compare("") of a null text.
            const char* empty = g_stringNullBuffer.get();
            order = reinterpret_cast<CompareCharsFn>(kCompareChars)(serial, nullptr, 0, serial->len, empty,
                                                                     static_cast<std::uint32_t>(std::strlen(empty)));
        }
        if (order == 0) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
void CopyPart(const msvc6::String* serial, const std::uint32_t (&positions)[N], char (&out)[N + 1]) {
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = InlineChar(serial, positions[i]);
    }
    out[N] = 0;
}

}  // namespace

// Purpose: Checks whether a serial number is well formed and not one of the
// blacklisted ones (at startup, in the main menu and in the lobby).
std::uint32_t __fastcall GameConfig_IsValidSerial(const void* self, void*, const msvc6::String* serial) {
    if (serial->len != kSerialLength) {
        return 0;
    }
    for (const std::uint32_t dash : kDashPositions) {
        if (SerialChar(serial, dash) != '-') {
            return 0;
        }
    }
    if (IsBlacklisted(serial)) {
        return 0;
    }
    const char firstCheck = SerialChar(serial, kFirstCheckPosition);
    const char secondCheck = InlineChar(serial, kSecondCheckPosition);
    char firstPart[std::size(kFirstPart) + 1];
    char secondPart[std::size(kSecondPart) + 1];
    CopyPart(serial, kFirstPart, firstPart);
    CopyPart(serial, kSecondPart, secondPart);
    const auto checkDigit = reinterpret_cast<CheckDigitFn>(kCheckDigit);
    if (static_cast<char>(checkDigit(self, nullptr, firstPart)) != firstCheck) {
        return 0;
    }
    if (static_cast<char>(checkDigit(self, nullptr, secondPart)) != secondCheck) {
        return 0;
    }
    // 0x411f87: both parts are converted and the results dropped.
    reinterpret_cast<AtoiFn>(kAtoi)(firstPart);
    reinterpret_cast<AtoiFn>(kAtoi)(secondPart);
    return 1;
}

using IsValidSerialFn = std::uint32_t(__fastcall*)(const void* self, void* edx, const msvc6::String* serial);
S3RE_HOOK(0x00411d10, GameConfig_IsValidSerial, IsValidSerialFn, Original_GameConfig_IsValidSerial,
          GameConfig_IsValidSerial);

namespace {

// A random serial: digits (sometimes another character) with the dashes
// mostly in place and, in most cases, the right check digits; sometimes a
// blacklisted serial, another length or no buffer.
std::string RandomSerial(test::Context& ctx) {
    auto& rng = ctx.rng();
    if (rng.Below(16) == 0) {
        const char* entry = g_serialBlacklist.get()[rng.Below(kBlacklistEntries)];
        return entry != nullptr ? std::string(entry) : std::string();
    }
    std::string text(rng.Below(8) == 0 ? rng.Below(30) : kSerialLength, '0');
    for (char& c : text) {
        c = static_cast<char>(rng.Below(20) == 0 ? 'A' + rng.Below(26) : '0' + rng.Below(10));
    }
    if (text.size() == kSerialLength) {
        for (const std::uint32_t dash : kDashPositions) {
            if (rng.Below(20) != 0) {
                text[dash] = '-';
            }
        }
        char first[std::size(kFirstPart) + 1] = {};
        char second[std::size(kSecondPart) + 1] = {};
        for (std::size_t i = 0; i < std::size(kFirstPart); ++i) {
            first[i] = text[kFirstPart[i]];
        }
        for (std::size_t i = 0; i < std::size(kSecondPart); ++i) {
            second[i] = text[kSecondPart[i]];
        }
        const auto checkDigit = reinterpret_cast<CheckDigitFn>(kCheckDigit);
        if (rng.Below(4) != 0) {
            text[kFirstCheckPosition] = static_cast<char>(checkDigit(nullptr, nullptr, first));
        }
        if (rng.Below(4) != 0) {
            text[kSecondCheckPosition] = static_cast<char>(checkDigit(nullptr, nullptr, second));
        }
    }
    return text;
}

std::uint32_t __fastcall WrongIsValidSerial(const void* self, void* edx, const msvc6::String* serial) {
    // Wrong: skips the second check digit.
    if (serial->len == kSerialLength && serial->ptr != nullptr) {
        const char saved = serial->ptr[kSecondCheckPosition];
        char* text = serial->ptr;
        char first[std::size(kSecondPart) + 1];
        CopyPart(serial, kSecondPart, first);
        text[kSecondCheckPosition] = static_cast<char>(reinterpret_cast<CheckDigitFn>(kCheckDigit)(self, edx, first));
        const std::uint32_t result = GameConfig_IsValidSerial(self, edx, serial);
        text[kSecondCheckPosition] = saved;
        return result;
    }
    return GameConfig_IsValidSerial(self, edx, serial);
}

int CountDifferences(test::Context& ctx, IsValidSerialFn candidate, int cases, int* accepted) {
    int differences = 0;
    for (int i = 0; i < cases; ++i) {
        const std::string text = RandomSerial(ctx);
        msvc6::String serial;
        if (text.empty() && ctx.rng().Below(2) == 0) {
            msvc6::detail::Clear(&serial);
        } else {
            msvc6::String_FromChars(&serial, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
        }
        const std::uint32_t expected = Original_GameConfig_IsValidSerial(nullptr, nullptr, &serial) & 0xff;
        const std::uint32_t actual = candidate(nullptr, nullptr, &serial) & 0xff;
        if (expected != actual) {
            ++differences;
        }
        if (expected != 0 && accepted != nullptr) {
            ++*accepted;
        }
        msvc6::String_Destroy(&serial, nullptr);
    }
    return differences;
}

}  // namespace

S3RE_TEST(GameConfigSerial_MatchesOriginal) {
    int accepted = 0;
    S3RE_EXPECT_EQ(0, CountDifferences(ctx, &GameConfig_IsValidSerial, 4000, &accepted));
    S3RE_EXPECT(accepted > 100);  // the valid path is reached
}

S3RE_TEST(GameConfigSerial_ComparisonDetectsWrongImplementation) {
    S3RE_EXPECT(CountDifferences(ctx, &WrongIsValidSerial, 4000, nullptr) > 0);
}

}  // namespace s3re::game
