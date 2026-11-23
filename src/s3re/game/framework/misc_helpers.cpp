// Small helpers from several files that don't fit elsewhere yet.
//
// GameParams_CopyRectB0 (0x00437400): (out) thiscall, ret 4: copies the four
//   dwords at this+0xb0 to out; EAX = out. Unnamed file between
//   Dirk\GameParams.cpp and Dirk\LobbyDialogs.cpp; called by CNetSession and
//   the savegame code (probably a rectangle or four values of the game
//   settings).
// Watcom_CopyByte (0x004584f0): (dst, src) cdecl: *dst = *src (one byte);
//   EAX = src. Called by the Watcom vectors' destructors (probably the
//   copy of an empty allocator or a one-byte element).
// Lobby_EqualField4 (0x00606ac0): (other) thiscall, ret 4: AL = (this+4 ==
//   other+4); the upper 24 bits of EAX are those of this+4's dword (left
//   over from the load). Dirk\protocol.cpp; probably operator== of a small
//   id class.
// Picture_FreeIfBoth (0x005aa0a0): (a, b) cdecl: if a and b are not null,
//   the C runtime's free(b) (0x621b7e); EAX = 0 when either is null, else
//   whatever free left in EAX (callers pass it on, so ours returns the same
//   register). Probably a memory callback of the PNG/zlib code in
//   Picture (free(opaque, address) shape).

#include "game/framework/framework_layout.h"
#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kCrtFree = 0x00621b7e;

}  // namespace

using CopyRectFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t* out);
using CopyByteFn = const void*(__cdecl*)(std::uint8_t* dst, const std::uint8_t* src);
using EqualFn = std::uint32_t(__fastcall*)(const void* self, void* edx, const void* other);
using FreeIfBothFn = std::uint32_t(__cdecl*)(const void* a, void* b);

void* __fastcall GameParams_CopyRectB0(void* self, void* edx, std::uint32_t* out);
const void* __cdecl Watcom_CopyByte(std::uint8_t* dst, const std::uint8_t* src);
std::uint32_t __fastcall Lobby_EqualField4(const void* self, void* edx, const void* other);
std::uint32_t __cdecl Picture_FreeIfBoth(const void* a, void* b);

S3RE_HOOK(0x00437400, GameParams_CopyRectB0, CopyRectFn, Original_GameParams_CopyRectB0, GameParams_CopyRectB0);

// Purpose: Returns a copy of four values stored at +0xb0 of a settings-like
// object (probably a rectangle), used by the network session and savegames.
void* __fastcall GameParams_CopyRectB0(void* self, void* /*edx*/, std::uint32_t* out) {
    // Called on g_gameSettings (0x7a7a18).
    const GameSettings* const settings = static_cast<const GameSettings*>(self);
    for (std::size_t k = 0; k < 4; ++k) {
        out[k] = settings->valuesB0[k];
    }
    return out;
}

S3RE_HOOK(0x004584f0, Watcom_CopyByte, CopyByteFn, Original_Watcom_CopyByte, Watcom_CopyByte);

// Purpose: Copies one byte (used by the Watcom container code, probably
// for an empty allocator or a one-byte element).
const void* __cdecl Watcom_CopyByte(std::uint8_t* dst, const std::uint8_t* src) {
    *dst = *src;
    return src;
}

S3RE_HOOK(0x00606ac0, Lobby_EqualField4, EqualFn, Original_Lobby_EqualField4, Lobby_EqualField4);

// Purpose: Whether two lobby objects have the same id (the dword at +4);
// probably an operator== of the online protocol code.
std::uint32_t __fastcall Lobby_EqualField4(const void* self, void* /*edx*/, const void* other) {
    const std::uint32_t mine = static_cast<const framework::LobbyIdObject*>(self)->id;
    const bool same = mine == static_cast<const framework::LobbyIdObject*>(other)->id;
    return (mine & 0xffffff00u) | (same ? 1u : 0u);
}

S3RE_HOOK(0x005aa0a0, Picture_FreeIfBoth, FreeIfBothFn, Original_Picture_FreeIfBoth, Picture_FreeIfBoth);

// Purpose: Frees a block of the picture decoder's memory (probably the
// zlib/PNG "free" callback: nothing happens without a context or a block).
std::uint32_t __cdecl Picture_FreeIfBoth(const void* a, void* b) {
    if (a == nullptr || b == nullptr) {
        return 0;
    }
    return reinterpret_cast<std::uint32_t(__cdecl*)(void*)>(kCrtFree)(b);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct MiscFunctions {
    CopyRectFn copyRect;
    CopyByteFn copyByte;
    EqualFn equal;
    FreeIfBothFn freeIfBoth;
};

void CompareMisc(test::Context& ctx, const MiscFunctions& candidate, int cases) {
    const MiscFunctions original{Original_GameParams_CopyRectB0, Original_Watcom_CopyByte, Original_Lobby_EqualField4,
                                 Original_Picture_FreeIfBoth};
    std::vector<std::uint8_t> a(0x100), b(0x100);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(4));
        ctx.rng().Fill(a.data(), a.size());
        if (op == 2 && ctx.rng().Chance(1, 2)) {
            std::memcpy(a.data() + 0x84, a.data() + 4, 4);  // equal ids
        }
        b = a;
        std::uint32_t ra = 0, rb = 0;
        switch (op) {
            case 0:
                ra = static_cast<std::uint32_t>(static_cast<std::uint8_t*>(original.copyRect(a.data(), nullptr, reinterpret_cast<std::uint32_t*>(a.data() + 8))) - a.data());
                rb = static_cast<std::uint32_t>(static_cast<std::uint8_t*>(candidate.copyRect(b.data(), nullptr, reinterpret_cast<std::uint32_t*>(b.data() + 8))) - b.data());
                break;
            case 1:
                ra = static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(original.copyByte(a.data() + 3, a.data() + 0x40)) - a.data());
                rb = static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(candidate.copyByte(b.data() + 3, b.data() + 0x40)) - b.data());
                break;
            case 2:
                ra = original.equal(a.data(), nullptr, a.data() + 0x80);
                rb = candidate.equal(b.data(), nullptr, b.data() + 0x80);
                break;
            default: {
                // Only the null cases: the block case frees memory, which
                // the emulator spec covers (free is probed there).
                const bool first = ctx.rng().Chance(1, 2);
                ra = original.freeIfBoth(first ? nullptr : a.data(), first ? a.data() : nullptr);
                rb = candidate.freeIfBoth(first ? nullptr : b.data(), first ? b.data() : nullptr);
                break;
            }
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: compares only the low byte of the ids.
std::uint32_t __fastcall WrongEqual(const void* self, void* edx, const void* other) {
    const std::uint32_t r = Lobby_EqualField4(self, edx, other);
    const bool lowSame = static_cast<const std::uint8_t*>(self)[4] == static_cast<const std::uint8_t*>(other)[4];
    return (r & ~0xffu) | (lowSame ? 1u : 0u);
}

}  // namespace

S3RE_TEST(MiscHelpers_MatchOriginal) {
    CompareMisc(ctx, {&GameParams_CopyRectB0, &Watcom_CopyByte, &Lobby_EqualField4, &Picture_FreeIfBoth}, 20000);
}

S3RE_TEST(MiscHelpers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMisc(probe, {&GameParams_CopyRectB0, &Watcom_CopyByte, &WrongEqual, &Picture_FreeIfBoth}, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
