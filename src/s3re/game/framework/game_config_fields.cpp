// The plain field accessors of the game's configuration object (g_config
// 0x7a7af0, struct GameConfig in game/globals.h; Dirk\ConsoleInit.cpp and
// Dirk\DGlobals.cpp). Each is one or two instructions followed by nop
// padding, which Detours uses (CLAUDE.md, "Detours and tiny functions").
// What the fields mean is mostly unknown; the names give the offsets.
//
// Dword getters: () -> the dword at +0x28 (0x004120a0), +0x78 (0x00412310),
//   +0x7c (0x00412320), +0x80 (0x00412330), +0x94 (0x00412370).
// Folded dword getters (also called on the game settings 0x7a7a18 and menu
//   objects, so named by offset as in folded_getters.cpp): +0x84
//   (0x00412340), +0x8c (0x00412350), +0x90 (0x00412360).
// Dword setters: (value) ret 4: +0x28 (0x004120b0), +0x2c (0x004120c0);
//   EAX = value.
// Byte getters: () -> AL = the byte at +0x30 (0x004120d0), +0x38
//   (0x004120f0), +0x39 (0x00412100), +0x24 (0x00412270), +0x58..+0x5b
//   (0x00412290, 0x004122a0, 0x004122b0, 0x004122c0), +0x5c (0x004122e0),
//   +0x5d (0x004122d0), +0x5e (0x00412810), +0x5f (0x00412820), +0x6c
//   (0x00412800), +0x15a (0x00414020). The rest of EAX is the caller's in
//   the original; ours zero-extends (the callers test AL or store it as a
//   byte).
// Byte setters: (value) ret 4: +0x24 (0x00412280), +0x5c (0x004122f0),
//   +0x189 (0x00414040); EAX: AL = the byte (the rest is the caller's EAX
//   in the original, zero in ours).
// Address getters: () -> this + 0x3c (0x00412110), this + 0x14 (the string
//   0x412130 sets; 0x00412120).
// GameConfig::HasDirectPlay (0x004120e0): () -> 1 if the dword at +0x34 is
//   not 0, else 0 (CConsoleThread::OnStart logs "DirectPlay 6.0 nicht
//   installiert" when it is 0).

#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using ConfigFieldGetFn = std::uint32_t(__fastcall*)(const GameConfig* self, void* edx);
using ConfigFieldSetFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx, std::uint32_t value);
using FoldedGetFn = std::uint32_t(__fastcall*)(const void* self, void* edx);

namespace {

// Reads the dword at `kOffset` of an object (the folded getters' only work).
template <std::size_t kOffset>
std::uint32_t DwordAt(const void* self) {
    std::uint32_t value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(self) + kOffset, sizeof(value));  // raw-ok: folded getter
    return value;
}

}  // namespace


// Purpose: Reads a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Dword28(const GameConfig* self, void*) {
    return self->dword28;
}

// Purpose: Reads a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Dword78(const GameConfig* self, void*) {
    return self->dwords74[1];
}

// Purpose: Reads a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Dword7C(const GameConfig* self, void*) {
    return self->dwords74[2];
}

// Purpose: Reads a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Dword80(const GameConfig* self, void*) {
    return self->dwords74[3];
}

// Purpose: Reads a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Dword94(const GameConfig* self, void*) {
    return self->dwords74[8];
}

// Purpose: Reads a dword field of an object (the same code serves the configuration, the game
// settings and menu objects).
std::uint32_t __fastcall GetMemberDword84(const void* self, void*) {
    return DwordAt<0x84>(self);
}

// Purpose: Reads a dword field of an object (the same code serves the configuration, the game
// settings and menu objects).
std::uint32_t __fastcall GetMemberDword8C(const void* self, void*) {
    return DwordAt<0x8c>(self);
}

// Purpose: Reads a dword field of an object (the same code serves the configuration, the game
// settings and menu objects).
std::uint32_t __fastcall GetMemberDword90(const void* self, void*) {
    return DwordAt<0x90>(self);
}

// Purpose: Stores a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_SetDword28(GameConfig* self, void*, std::uint32_t value) {
    self->dword28 = value;
    return value;
}

// Purpose: Stores a dword setting of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_SetDword2C(GameConfig* self, void*, std::uint32_t value) {
    self->dword2c = value;
    return value;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte30(const GameConfig* self, void*) {
    return self->byte30;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte38(const GameConfig* self, void*) {
    return self->byte38;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte39(const GameConfig* self, void*) {
    return self->byte39;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte24(const GameConfig* self, void*) {
    return self->byte24;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte58(const GameConfig* self, void*) {
    return self->bytes58[0];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte59(const GameConfig* self, void*) {
    return self->bytes58[1];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5A(const GameConfig* self, void*) {
    return self->bytes58[2];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5B(const GameConfig* self, void*) {
    return self->bytes58[3];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5C(const GameConfig* self, void*) {
    return self->bytes58[4];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5D(const GameConfig* self, void*) {
    return self->bytes58[5];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5E(const GameConfig* self, void*) {
    return self->bytes58[6];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte5F(const GameConfig* self, void*) {
    return self->bytes58[7];
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte6C(const GameConfig* self, void*) {
    return self->byte6c;
}

// Purpose: Reads a flag of the game's configuration (meaning not known yet).
std::uint32_t __fastcall GameConfig_Byte15A(const GameConfig* self, void*) {
    return self->byte15a;
}

// Purpose: Stores a flag of the game's configuration.
std::uint32_t __fastcall GameConfig_SetByte24(GameConfig* self, void*, std::uint32_t value) {
    self->byte24 = static_cast<std::uint8_t>(value);
    return self->byte24;
}

// Purpose: Stores a flag of the game's configuration.
std::uint32_t __fastcall GameConfig_SetByte5C(GameConfig* self, void*, std::uint32_t value) {
    self->bytes58[4] = static_cast<std::uint8_t>(value);
    return self->bytes58[4];
}

// Purpose: Stores a flag of the game's configuration (the campaign flag GetMemberByte189 reads).
std::uint32_t __fastcall GameConfig_SetByte189(GameConfig* self, void*, std::uint32_t value) {
    self->unk189 = static_cast<std::uint8_t>(value);
    return self->unk189;
}

// Purpose: Gives the address of an object inside the configuration (+0x3c; probably a container).
std::uint32_t __fastcall GameConfig_Object3C(const GameConfig* self, void*) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self->object3c));
}

// Purpose: Gives the address of the configuration's string at +0x14.
std::uint32_t __fastcall GameConfig_String14(const GameConfig* self, void*) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&self->string14));
}

// Purpose: Whether DirectPlay (needed for multiplayer) was found at startup.
std::uint32_t __fastcall GameConfig_HasDirectPlay(const GameConfig* self, void*) {
    return self->directplay != 0 ? 1 : 0;
}


S3RE_HOOK(0x004120a0, GameConfig_Dword28, ConfigFieldGetFn, Original_GameConfig_Dword28, GameConfig_Dword28);
S3RE_HOOK(0x00412310, GameConfig_Dword78, ConfigFieldGetFn, Original_GameConfig_Dword78, GameConfig_Dword78);
S3RE_HOOK(0x00412320, GameConfig_Dword7C, ConfigFieldGetFn, Original_GameConfig_Dword7C, GameConfig_Dword7C);
S3RE_HOOK(0x00412330, GameConfig_Dword80, ConfigFieldGetFn, Original_GameConfig_Dword80, GameConfig_Dword80);
S3RE_HOOK(0x00412370, GameConfig_Dword94, ConfigFieldGetFn, Original_GameConfig_Dword94, GameConfig_Dword94);
S3RE_HOOK(0x00412340, GetMemberDword84, FoldedGetFn, Original_GetMemberDword84, GetMemberDword84);
S3RE_HOOK(0x00412350, GetMemberDword8C, FoldedGetFn, Original_GetMemberDword8C, GetMemberDword8C);
S3RE_HOOK(0x00412360, GetMemberDword90, FoldedGetFn, Original_GetMemberDword90, GetMemberDword90);
S3RE_HOOK(0x004120b0, GameConfig_SetDword28, ConfigFieldSetFn, Original_GameConfig_SetDword28, GameConfig_SetDword28);
S3RE_HOOK(0x004120c0, GameConfig_SetDword2C, ConfigFieldSetFn, Original_GameConfig_SetDword2C, GameConfig_SetDword2C);
S3RE_HOOK(0x004120d0, GameConfig_Byte30, ConfigFieldGetFn, Original_GameConfig_Byte30, GameConfig_Byte30);
S3RE_HOOK(0x004120f0, GameConfig_Byte38, ConfigFieldGetFn, Original_GameConfig_Byte38, GameConfig_Byte38);
S3RE_HOOK(0x00412100, GameConfig_Byte39, ConfigFieldGetFn, Original_GameConfig_Byte39, GameConfig_Byte39);
S3RE_HOOK(0x00412270, GameConfig_Byte24, ConfigFieldGetFn, Original_GameConfig_Byte24, GameConfig_Byte24);
S3RE_HOOK(0x00412290, GameConfig_Byte58, ConfigFieldGetFn, Original_GameConfig_Byte58, GameConfig_Byte58);
S3RE_HOOK(0x004122a0, GameConfig_Byte59, ConfigFieldGetFn, Original_GameConfig_Byte59, GameConfig_Byte59);
S3RE_HOOK(0x004122b0, GameConfig_Byte5A, ConfigFieldGetFn, Original_GameConfig_Byte5A, GameConfig_Byte5A);
S3RE_HOOK(0x004122c0, GameConfig_Byte5B, ConfigFieldGetFn, Original_GameConfig_Byte5B, GameConfig_Byte5B);
S3RE_HOOK(0x004122e0, GameConfig_Byte5C, ConfigFieldGetFn, Original_GameConfig_Byte5C, GameConfig_Byte5C);
S3RE_HOOK(0x004122d0, GameConfig_Byte5D, ConfigFieldGetFn, Original_GameConfig_Byte5D, GameConfig_Byte5D);
S3RE_HOOK(0x00412810, GameConfig_Byte5E, ConfigFieldGetFn, Original_GameConfig_Byte5E, GameConfig_Byte5E);
S3RE_HOOK(0x00412820, GameConfig_Byte5F, ConfigFieldGetFn, Original_GameConfig_Byte5F, GameConfig_Byte5F);
S3RE_HOOK(0x00412800, GameConfig_Byte6C, ConfigFieldGetFn, Original_GameConfig_Byte6C, GameConfig_Byte6C);
S3RE_HOOK(0x00414020, GameConfig_Byte15A, ConfigFieldGetFn, Original_GameConfig_Byte15A, GameConfig_Byte15A);
S3RE_HOOK(0x00412280, GameConfig_SetByte24, ConfigFieldSetFn, Original_GameConfig_SetByte24, GameConfig_SetByte24);
S3RE_HOOK(0x004122f0, GameConfig_SetByte5C, ConfigFieldSetFn, Original_GameConfig_SetByte5C, GameConfig_SetByte5C);
S3RE_HOOK(0x00414040, GameConfig_SetByte189, ConfigFieldSetFn, Original_GameConfig_SetByte189, GameConfig_SetByte189);
S3RE_HOOK(0x00412110, GameConfig_Object3C, ConfigFieldGetFn, Original_GameConfig_Object3C, GameConfig_Object3C);
S3RE_HOOK(0x00412120, GameConfig_String14, ConfigFieldGetFn, Original_GameConfig_String14, GameConfig_String14);
S3RE_HOOK(0x004120e0, GameConfig_HasDirectPlay, ConfigFieldGetFn, Original_GameConfig_HasDirectPlay, GameConfig_HasDirectPlay);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

enum class Kind { Get32, Get8, Set32, Set8, Address, Folded };

struct Accessor {
    const char* name;
    Kind kind;
    const void* original;
    const void* ours;
};

// Wrong versions for the harness check.
std::uint32_t __fastcall WrongDword28(const GameConfig* self, void*) {
    return self->dword2c;
}
std::uint32_t __fastcall WrongSetByte5C(GameConfig* self, void*, std::uint32_t value) {
    self->bytes58[5] = static_cast<std::uint8_t>(value);
    return value & 0xff;
}
std::uint32_t __fastcall WrongHasDirectPlay(const GameConfig* self, void*) {
    return self->directplay;
}

std::vector<Accessor> Accessors() {
    return {
        {"GameConfig_Dword28", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_Dword28), reinterpret_cast<const void*>(&GameConfig_Dword28)},
        {"GameConfig_Dword78", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_Dword78), reinterpret_cast<const void*>(&GameConfig_Dword78)},
        {"GameConfig_Dword7C", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_Dword7C), reinterpret_cast<const void*>(&GameConfig_Dword7C)},
        {"GameConfig_Dword80", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_Dword80), reinterpret_cast<const void*>(&GameConfig_Dword80)},
        {"GameConfig_Dword94", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_Dword94), reinterpret_cast<const void*>(&GameConfig_Dword94)},
        {"GetMemberDword84", Kind::Folded, reinterpret_cast<const void*>(Original_GetMemberDword84), reinterpret_cast<const void*>(&GetMemberDword84)},
        {"GetMemberDword8C", Kind::Folded, reinterpret_cast<const void*>(Original_GetMemberDword8C), reinterpret_cast<const void*>(&GetMemberDword8C)},
        {"GetMemberDword90", Kind::Folded, reinterpret_cast<const void*>(Original_GetMemberDword90), reinterpret_cast<const void*>(&GetMemberDword90)},
        {"GameConfig_SetDword28", Kind::Set32, reinterpret_cast<const void*>(Original_GameConfig_SetDword28), reinterpret_cast<const void*>(&GameConfig_SetDword28)},
        {"GameConfig_SetDword2C", Kind::Set32, reinterpret_cast<const void*>(Original_GameConfig_SetDword2C), reinterpret_cast<const void*>(&GameConfig_SetDword2C)},
        {"GameConfig_Byte30", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte30), reinterpret_cast<const void*>(&GameConfig_Byte30)},
        {"GameConfig_Byte38", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte38), reinterpret_cast<const void*>(&GameConfig_Byte38)},
        {"GameConfig_Byte39", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte39), reinterpret_cast<const void*>(&GameConfig_Byte39)},
        {"GameConfig_Byte24", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte24), reinterpret_cast<const void*>(&GameConfig_Byte24)},
        {"GameConfig_Byte58", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte58), reinterpret_cast<const void*>(&GameConfig_Byte58)},
        {"GameConfig_Byte59", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte59), reinterpret_cast<const void*>(&GameConfig_Byte59)},
        {"GameConfig_Byte5A", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5A), reinterpret_cast<const void*>(&GameConfig_Byte5A)},
        {"GameConfig_Byte5B", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5B), reinterpret_cast<const void*>(&GameConfig_Byte5B)},
        {"GameConfig_Byte5C", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5C), reinterpret_cast<const void*>(&GameConfig_Byte5C)},
        {"GameConfig_Byte5D", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5D), reinterpret_cast<const void*>(&GameConfig_Byte5D)},
        {"GameConfig_Byte5E", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5E), reinterpret_cast<const void*>(&GameConfig_Byte5E)},
        {"GameConfig_Byte5F", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte5F), reinterpret_cast<const void*>(&GameConfig_Byte5F)},
        {"GameConfig_Byte6C", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte6C), reinterpret_cast<const void*>(&GameConfig_Byte6C)},
        {"GameConfig_Byte15A", Kind::Get8, reinterpret_cast<const void*>(Original_GameConfig_Byte15A), reinterpret_cast<const void*>(&GameConfig_Byte15A)},
        {"GameConfig_SetByte24", Kind::Set8, reinterpret_cast<const void*>(Original_GameConfig_SetByte24), reinterpret_cast<const void*>(&GameConfig_SetByte24)},
        {"GameConfig_SetByte5C", Kind::Set8, reinterpret_cast<const void*>(Original_GameConfig_SetByte5C), reinterpret_cast<const void*>(&GameConfig_SetByte5C)},
        {"GameConfig_SetByte189", Kind::Set8, reinterpret_cast<const void*>(Original_GameConfig_SetByte189), reinterpret_cast<const void*>(&GameConfig_SetByte189)},
        {"GameConfig_Object3C", Kind::Address, reinterpret_cast<const void*>(Original_GameConfig_Object3C), reinterpret_cast<const void*>(&GameConfig_Object3C)},
        {"GameConfig_String14", Kind::Address, reinterpret_cast<const void*>(Original_GameConfig_String14), reinterpret_cast<const void*>(&GameConfig_String14)},
        {"GameConfig_HasDirectPlay", Kind::Get32, reinterpret_cast<const void*>(Original_GameConfig_HasDirectPlay), reinterpret_cast<const void*>(&GameConfig_HasDirectPlay)},
    };
}

// Every accessor on random objects (getters compare EAX, AL for the byte
// ones; setters also the object afterwards).
bool CompareAccessors(test::Context& ctx, const std::vector<Accessor>& accessors, int cases) {
    std::vector<std::uint8_t> a(0x200), b(0x200);
    for (const Accessor& accessor : accessors) {
        for (int i = 0; i < cases; ++i) {
            ctx.rng().Fill(a.data(), a.size());
            b = a;
            const std::uint32_t value = ctx.rng().Next32();
            std::uint32_t ea = 0, eb = 0;
            std::uint32_t mask = 0xffffffff;
            switch (accessor.kind) {
            case Kind::Get32:
            case Kind::Get8:
            case Kind::Folded:
                ea = reinterpret_cast<FoldedGetFn>(accessor.original)(a.data(), nullptr);
                eb = reinterpret_cast<FoldedGetFn>(accessor.ours)(b.data(), nullptr);
                mask = accessor.kind == Kind::Get8 ? 0xff : 0xffffffff;
                break;
            case Kind::Address:
                ea = reinterpret_cast<FoldedGetFn>(accessor.original)(a.data(), nullptr) -
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(a.data()));
                eb = reinterpret_cast<FoldedGetFn>(accessor.ours)(b.data(), nullptr) -
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(b.data()));
                break;
            case Kind::Set32:
            case Kind::Set8:
                ea = reinterpret_cast<ConfigFieldSetFn>(accessor.original)(reinterpret_cast<GameConfig*>(a.data()),
                                                                          nullptr, value);
                eb = reinterpret_cast<ConfigFieldSetFn>(accessor.ours)(reinterpret_cast<GameConfig*>(b.data()),
                                                                      nullptr, value);
                mask = accessor.kind == Kind::Set8 ? 0xff : 0xffffffff;
                break;
            }
            if (!S3RE_EXPECT_EQ(ea & mask, eb & mask) || !S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
                ctx.Fail(__FILE__, __LINE__, "  %s case %d", accessor.name, i);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

S3RE_TEST(GameConfigFields_MatchOriginal) {
    CompareAccessors(ctx, Accessors(), 300);
}

S3RE_TEST(GameConfigFields_ComparisonDetectsWrongImplementation) {
    const struct {
        const char* name;
        const void* wrong;
    } kWrong[] = {
        {"GameConfig_Dword28", reinterpret_cast<const void*>(&WrongDword28)},
        {"GameConfig_SetByte5C", reinterpret_cast<const void*>(&WrongSetByte5C)},
        {"GameConfig_HasDirectPlay", reinterpret_cast<const void*>(&WrongHasDirectPlay)},
    };
    for (const auto& wrong : kWrong) {
        std::vector<Accessor> accessors;
        for (Accessor accessor : Accessors()) {
            if (std::strcmp(accessor.name, wrong.name) == 0) {
                accessor.ours = wrong.wrong;
                accessors.push_back(accessor);
            }
        }
        test::Context quiet("wrong", 5, true);
        if (!S3RE_EXPECT(accessors.size() == 1 && !CompareAccessors(quiet, accessors, 300))) {
            ctx.Fail(__FILE__, __LINE__, "  the wrong %s was not caught", wrong.name);
        }
    }
}

}  // namespace s3re::game
