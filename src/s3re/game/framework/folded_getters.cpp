// Member getters that the linker shares between classes (identical code is
// folded into one function, so the names are by field offset).
//
// GetMemberDword64 (0x0049f510): thiscall () -> dword at this+0x64 (mov
//   eax, [ecx+0x64]; ret). Callers use it on the config object 0x7a7af0
//   (NotifyLocalPlayer: the notification level setting, probably), on the
//   game settings and on menu/network objects.
// GetMemberByte189 (0x00414030): thiscall () -> byte at this+0x189 in AL
//   (mov al, [ecx+0x189]; ret; the rest of EAX is the caller's, but every
//   caller uses only AL; ours returns the byte zero-extended). Callers:
//   MinerJobDig 0x4fa220 and UpdateSoldier 0x50e9f0 on the config object
//   (next to the campaign mission number), menu code 0x474010, 0x475640.
// GetMemberDword10 (0x0049f4d0), GetMemberDword14 (0x0049f590),
// GetMemberDword5C (0x0049f350): thiscall () -> the dword at +0x10, +0x14,
//   +0x5c (session 55). On a GamePlayerSetup: +0x10, the party +0x14 and the
//   type +0x5c (GameSettings::SaveSetup's getters); +0x10 is also the error
//   code of a CMapFile stream (CMapFile::GetError tail-jumps to 0x49f4d0),
//   and network and menu objects use them.
// GetMemberDword70 (0x0049dec0, was Net_sub_49dec0; wip/logic-r4): thiscall
//   () -> the dword at +0x70. On the config object the screen layout
//   setting (Render_sub_4c6d40 passes it to SetScreenLayout); network and
//   menu code use it on their objects.
// GetMemberByte68 (0x0049f580): thiscall () -> the byte at +0x68 in AL (the
//   rest of EAX is the caller's; all three callers, 0x4336ac, 0x47199c,
//   0x48df64, use only AL; ours zero-extends). On a GamePlayerSetup: the
//   race byte.
// GamePlayerSetup::SavedValues (0x0049fed0): thiscall (a*, b*, c*) ret 0xc:
//   *a = +0xa8, *b = +0xac, *c = +0xb0. EAX ends as the dword at +0xb0;
//   its three callers (0x4336c8, 0x471a0b, 0x48dfc3) don't read it, ours
//   returns the same value anyway.
// All bodies are followed by nop padding, which Detours uses (CLAUDE.md,
// "Detours and tiny functions").

#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
struct Dword64Owner {
    std::uint8_t unk000[0x64];
    std::uint32_t value;  // 0x64
};
struct Byte189Owner {
    std::uint8_t unk000[0x189];
    std::uint8_t value;  // 0x189
};
struct SavedValuesOwner {
    std::uint8_t unk000[0xa8];
    std::uint32_t values[3];  // 0xa8, 0xac, 0xb0
};
#pragma pack(pop)
static_assert(offsetof(SavedValuesOwner, values) == 0xa8);
static_assert(offsetof(Dword64Owner, value) == 0x64);
static_assert(offsetof(Byte189Owner, value) == 0x189);

}  // namespace

using GetterFn = std::uint32_t(__fastcall*)(void* self, void* edx);

// Purpose: Reads a dword setting of an object (the same code serves several classes), e.g. the
// notification level in the game's configuration.
std::uint32_t __fastcall GetMemberDword64(void* self, void*) {
    return static_cast<const Dword64Owner*>(self)->value;
}

// Purpose: Reads a byte setting of an object (the same code serves several classes), e.g. a flag in
// the game's configuration that the mines and soldiers check together with the campaign mission.
// The original only loads AL; every caller uses only AL (test al, al, or a byte argument), so the
// upper bytes are zero here.
std::uint32_t __fastcall GetMemberByte189(void* self, void*) {
    return static_cast<const Byte189Owner*>(self)->value;
}

// Reads the dword at `kOffset` of an object (the folded getters' only work).
template <std::size_t kOffset>
std::uint32_t DwordAt(const void* self) {
    std::uint32_t value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(self) + kOffset, sizeof(value));  // raw-ok: folded getter
    return value;
}

// Purpose: Reads a dword field of an object (the same code serves several classes): a player
// setup's +0x10 value for the savegame, the error code of a map file's stream.
std::uint32_t __fastcall GetMemberDword10(void* self, void*) {
    return DwordAt<0x10>(self);
}

// Purpose: Reads a dword field of an object (several classes), e.g. a player setup's party.
std::uint32_t __fastcall GetMemberDword14(void* self, void*) {
    return DwordAt<0x14>(self);
}

// Purpose: Reads a dword field of an object (several classes), e.g. a player setup's type (human,
// computer).
std::uint32_t __fastcall GetMemberDword5C(void* self, void*) {
    return DwordAt<0x5c>(self);
}

// Purpose: Reads a dword field of an object (several classes), e.g. the screen layout setting of
// the game's configuration.
std::uint32_t __fastcall GetMemberDword70(void* self, void*) {
    return DwordAt<0x70>(self);
}

// Purpose: Reads a byte field of an object (several classes), e.g. a player setup's race byte.
// The original only loads AL; every caller uses only AL, so the upper bytes are zero here.
std::uint32_t __fastcall GetMemberByte68(void* self, void*) {
    return static_cast<const std::uint8_t*>(self)[0x68];  // raw-ok: folded getter
}

using SavedValuesFn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t* a, std::uint32_t* b,
                                                 std::uint32_t* c);

// Purpose: Gives three values of a player setup that the savegame and the setup dialogs store
// (probably start-position data from the map).
std::uint32_t __fastcall GamePlayerSetup_SavedValues(const void* self, void*, std::uint32_t* a, std::uint32_t* b,
                                                     std::uint32_t* c) {
    const auto* owner = static_cast<const SavedValuesOwner*>(self);
    *a = owner->values[0];
    *b = owner->values[1];
    *c = owner->values[2];
    return owner->values[2];
}

S3RE_HOOK(0x0049f4d0, GetMemberDword10, GetterFn, Original_GetMemberDword10, GetMemberDword10);
S3RE_HOOK(0x0049f590, GetMemberDword14, GetterFn, Original_GetMemberDword14, GetMemberDword14);
S3RE_HOOK(0x0049f350, GetMemberDword5C, GetterFn, Original_GetMemberDword5C, GetMemberDword5C);
S3RE_HOOK(0x0049dec0, GetMemberDword70, GetterFn, Original_GetMemberDword70, GetMemberDword70);
S3RE_HOOK(0x0049f580, GetMemberByte68, GetterFn, Original_GetMemberByte68, GetMemberByte68);
S3RE_HOOK(0x0049fed0, GamePlayerSetup_SavedValues, SavedValuesFn, Original_GamePlayerSetup_SavedValues,
          GamePlayerSetup_SavedValues);
S3RE_HOOK(0x0049f510, GetMemberDword64, GetterFn, Original_GetMemberDword64, GetMemberDword64);
S3RE_HOOK(0x00414030, GetMemberByte189, GetterFn, Original_GetMemberByte189, GetMemberByte189);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

S3RE_TEST(GetMemberDword64_MatchesOriginal) {
    std::vector<std::uint8_t> object(0x200);
    for (int i = 0; i < 1000; ++i) {
        ctx.rng().Fill(object.data(), object.size());
        S3RE_EXPECT_EQ(Original_GetMemberDword64(object.data(), nullptr), GetMemberDword64(object.data(), nullptr));
    }
}

S3RE_TEST(GetMemberByte189_MatchesOriginal) {
    std::vector<std::uint8_t> object(0x200);
    for (int i = 0; i < 1000; ++i) {
        ctx.rng().Fill(object.data(), object.size());
        S3RE_EXPECT_EQ(Original_GetMemberByte189(object.data(), nullptr) & 0xff,
                       GetMemberByte189(object.data(), nullptr) & 0xff);
    }
}

namespace {

// The folded getters on random objects; `mask` is the part of EAX compared.
void CompareFolded(test::Context& ctx, GetterFn original, GetterFn ours, std::uint32_t mask) {
    std::vector<std::uint8_t> object(0x200);
    for (int i = 0; i < 1000; ++i) {
        ctx.rng().Fill(object.data(), object.size());
        if (!S3RE_EXPECT_EQ(original(object.data(), nullptr) & mask, ours(object.data(), nullptr) & mask)) {
            return;
        }
    }
}

void CompareSavedValues(test::Context& ctx, SavedValuesFn ours) {
    std::vector<std::uint8_t> object(0x200);
    for (int i = 0; i < 1000; ++i) {
        ctx.rng().Fill(object.data(), object.size());
        std::uint32_t a[4] = {}, b[4] = {};
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        // The outputs may overlap (the same address twice).
        const std::uint32_t pick = ctx.rng().Below(3);
        const std::uint32_t ea =
            Original_GamePlayerSetup_SavedValues(object.data(), nullptr, &a[0], &a[1 + pick % 2], &a[pick]);
        const std::uint32_t eb = ours(object.data(), nullptr, &b[0], &b[1 + pick % 2], &b[pick]);
        if (!S3RE_EXPECT(ea == eb && std::memcmp(a, b, sizeof(a)) == 0)) {
            return;
        }
    }
}

// Wrong: the second output gets the third value.
std::uint32_t __fastcall WrongSavedValues(const void* self, void*, std::uint32_t* a, std::uint32_t* b,
                                          std::uint32_t* c) {
    const auto* owner = static_cast<const SavedValuesOwner*>(self);
    *a = owner->values[0];
    *b = owner->values[2];
    *c = owner->values[2];
    return owner->values[2];
}

}  // namespace

S3RE_TEST(GetMemberDword10_MatchesOriginal) {
    CompareFolded(ctx, Original_GetMemberDword10, &GetMemberDword10, 0xffffffff);
}
S3RE_TEST(GetMemberDword14_MatchesOriginal) {
    CompareFolded(ctx, Original_GetMemberDword14, &GetMemberDword14, 0xffffffff);
}
S3RE_TEST(GetMemberDword5C_MatchesOriginal) {
    CompareFolded(ctx, Original_GetMemberDword5C, &GetMemberDword5C, 0xffffffff);
}
S3RE_TEST(GetMemberDword70_MatchesOriginal) {
    CompareFolded(ctx, Original_GetMemberDword70, &GetMemberDword70, 0xffffffff);
}
S3RE_TEST(GetMemberByte68_MatchesOriginal) {
    CompareFolded(ctx, Original_GetMemberByte68, &GetMemberByte68, 0xff);
}
S3RE_TEST(FoldedGetters_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFolded(probe, Original_GetMemberDword10, &GetMemberDword14, 0xffffffff);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GamePlayerSetup_SavedValues_MatchesOriginal) {
    CompareSavedValues(ctx, &GamePlayerSetup_SavedValues);
}
S3RE_TEST(GamePlayerSetup_SavedValues_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSavedValues(probe, &WrongSavedValues);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
