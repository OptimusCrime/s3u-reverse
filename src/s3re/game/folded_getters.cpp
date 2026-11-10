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
// Both bodies are followed by nop padding, which Detours uses (CLAUDE.md,
// "Detours and tiny functions").

#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
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
#pragma pack(pop)
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

}  // namespace s3re::game
