// WCBareVectorBase<T>::base_init_upto(n) instantiations (vtable slot 1;
// layout in vector_base.h): when filled <= n (signed), the elements
// filled..n of the block are constructed in place (each address
// null-checked, as placement new does) and filled = n + 1. thiscall,
// ret 4. The library declares the function void; the Watcom vector code
// calls it through the vtable and doesn't read EAX (which is left over
// from the last constructor, or the exception-frame setup), so ours
// returns nothing.
//   0x00428e50 trivially constructible T (shared by 33 vtables): only filled
//   0x0040a1d0 String (16 bytes, inline): allocator byte = the low byte of
//              n (it shares n's stack slot), ptr = len = res = 0
//   0x00429040 CListView::CColumn (0x20 bytes), default constructor 0x4274a0
//   0x004293d0 CListView::CLine (0x14 bytes), default constructor 0x4276b0
//   0x00414650 Dirk::CPlayer (0xcc bytes), GamePlayerSetup 0x49e8f0
//   0x00414b10 Dirk::CTeam (0x40 bytes), Dirk::CTeam::CTeam 0x4a0200
//   0x00463350 DirectX::CRunningSession (0x60 bytes), constructor 0x5bb040
// and (wip/watcom-r6) the lobby's, the network's and the display code's,
// with their element constructors ("(0)": the constructor takes a 0, "(0, 0)" two):
//   0x00462d70 DirectX::CConnectionShortcut (0x40 bytes), 0x5ba300
//   0x0044dee0 LobbyClient::CStatusMessage (0xc bytes), 0x5dc810
//   0x0044dc90 LobbyClient::CChatMessage (0x20 bytes), 0x5dc1e0
//   0x0044d960 LobbyClient::CPrivateChat (0x17c bytes), 0x5dc4e0
//   0x0044d460 LobbyClient::CGroup (0x11c bytes), 0x5da750
//   0x0044d050 LobbyClient::CPlayer (0x54 bytes), 0x5d9e20
//   0x005deae0 Lobby::CSocketAddress (0x24 bytes), 0x5ec9f0 (0, 0)
//   0x005de4f0 Lobby::CImageDescription (0x18 bytes), 0x5ebfb0
//   0x0060ad90 Lobby::CLMMessageBlock (0x24 bytes), 0x601370 (0, 0)
//   0x00541710 Lobby::CGamePlayerStatus (0x18 bytes), 0x5ec1e0
//   0x0060a0f0 Lobby::CLMGeneric (0x14 bytes), 0x601e40
//   0x005dff30 LobbyClient::priv::CLobbyServer (0x20 bytes), 0x5c46b0
//   0x005bdc70 DirectX::CGameNetSession::CPlayerIndex (0x8 bytes), 0x5bd200 (0, 0)
//   0x005b82f0 DirectX::CDisplayMode (0x6c bytes), 0x5b6c30 (0)
//   0x005b8040 DirectX::CDirectDrawDevice (0x30 bytes), 0x5b6740
//   0x0060a7e0 Lobby::priv::CPlayerIndex (0xc bytes), 0x6065f0 (0)
//   0x005df410 Lobby::CLMLobbyServerList::CServerData (0x20 bytes, inline): two
//              empty strings at +0 and +0x10, both with n's low byte as allocator
// The elements are counted with 32-bit wrap-around like the originals' loops
// (callers pass small n).

#include "watcom/vector_base.h"

#include "hooks.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace {

using ConstructElementFn = std::uint32_t(__fastcall*)(std::uint32_t element, void* edx);

// Runs `construct(address)` for the elements filled..n.
template <typename Construct>
void InitUpTo(VectorBase* self, std::int32_t n, std::uint32_t elementSize, Construct construct) {
    if (self->filled > n) {
        return;
    }
    std::uint32_t remaining = static_cast<std::uint32_t>(n) - static_cast<std::uint32_t>(self->filled) + 1;
    std::uint32_t offset = static_cast<std::uint32_t>(self->filled) * elementSize;
    for (; remaining != 0; --remaining, offset += elementSize) {
        const std::uint32_t element = self->data + offset;
        if (element != 0) {
            construct(element);
        }
    }
    self->filled = n + 1;
}

void InitUpToWith(VectorBase* self, std::int32_t n, std::uint32_t elementSize, std::uintptr_t constructor) {
    InitUpTo(self, n, elementSize, [constructor](std::uint32_t element) {
        reinterpret_cast<ConstructElementFn>(constructor)(element, nullptr);
    });
}

using ConstructElementWithArgFn = std::uint32_t(__fastcall*)(std::uint32_t element, void* edx, std::uint32_t arg);
using ConstructElementWithArgsFn = std::uint32_t(__fastcall*)(std::uint32_t element, void* edx, std::uint32_t a,
                                                              std::uint32_t b);

// The constructors that take arguments get zeros.
void InitUpToWithZero(VectorBase* self, std::int32_t n, std::uint32_t elementSize, std::uintptr_t constructor) {
    InitUpTo(self, n, elementSize, [constructor](std::uint32_t element) {
        reinterpret_cast<ConstructElementWithArgFn>(constructor)(element, nullptr, 0);
    });
}
void InitUpToWithZeros(VectorBase* self, std::int32_t n, std::uint32_t elementSize, std::uintptr_t constructor) {
    InitUpTo(self, n, elementSize, [constructor](std::uint32_t element) {
        reinterpret_cast<ConstructElementWithArgsFn>(constructor)(element, nullptr, 0, 0);
    });
}

void InitEmptyString(std::uint32_t address, std::int32_t n) {
    auto* string = reinterpret_cast<msvc6::String*>(address);
    string->allocator = static_cast<std::uint8_t>(n);  // n's stack slot doubles as the allocator
    string->ptr = nullptr;
    string->len = 0;
    string->res = 0;
}

constexpr std::uintptr_t kColumnConstructor = 0x004274a0;          // CListView::CColumn::CColumn_default
constexpr std::uintptr_t kLineConstructor = 0x004276b0;            // CListView::CLine::CLine_default
constexpr std::uintptr_t kPlayerConstructor = 0x0049e8f0;          // GamePlayerSetup::GamePlayerSetup
constexpr std::uintptr_t kTeamConstructor = 0x004a0200;            // Dirk::CTeam::CTeam (ours)
constexpr std::uintptr_t kRunningSessionConstructor = 0x005bb040;  // DirectX::CRunningSession

}  // namespace

using InitUpToFn = void(__fastcall*)(VectorBase* self, void* edx, std::int32_t n);

// Purpose: Marks the elements up to n of a vector of plain values as in use
// (they need no construction).
void __fastcall InitUpTo_428e50(VectorBase* self, void*, std::int32_t n) {
    if (self->filled <= n) {
        self->filled = n + 1;
    }
}

// Purpose: Constructs empty strings up to element n of a vector of strings.
void __fastcall InitUpTo_40a1d0(VectorBase* self, void*, std::int32_t n) {
    InitUpTo(self, n, sizeof(msvc6::String), [n](std::uint32_t element) {
        auto* string = reinterpret_cast<msvc6::String*>(element);
        string->allocator = static_cast<std::uint8_t>(n);  // n's stack slot doubles as the allocator
        string->ptr = nullptr;
        string->len = 0;
        string->res = 0;
    });
}

// Purpose: Constructs the columns / lines of a list view up to element n.
void __fastcall InitUpTo_429040(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x20, kColumnConstructor);
}
void __fastcall InitUpTo_4293d0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x14, kLineConstructor);
}

// Purpose: Constructs player setups / teams of the game settings up to
// element n.
void __fastcall InitUpTo_414650(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0xcc, kPlayerConstructor);
}
void __fastcall InitUpTo_414b10(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x40, kTeamConstructor);
}

// Purpose: Constructs network session records up to element n.
void __fastcall InitUpTo_463350(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x60, kRunningSessionConstructor);
}

// Purpose: Constructs the lobby's, the network's and the display code's
// vector elements up to element n.
void __fastcall InitUpTo_462d70(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x40, 0x005ba300);  // DirectX::CConnectionShortcut
}
void __fastcall InitUpTo_44dee0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0xc, 0x005dc810);  // LobbyClient::CStatusMessage
}
void __fastcall InitUpTo_44dc90(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x20, 0x005dc1e0);  // LobbyClient::CChatMessage
}
void __fastcall InitUpTo_44d960(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x17c, 0x005dc4e0);  // LobbyClient::CPrivateChat
}
void __fastcall InitUpTo_44d460(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x11c, 0x005da750);  // LobbyClient::CGroup
}
void __fastcall InitUpTo_44d050(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x54, 0x005d9e20);  // LobbyClient::CPlayer
}
void __fastcall InitUpTo_5deae0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWithZeros(self, n, 0x24, 0x005ec9f0);  // Lobby::CSocketAddress
}
void __fastcall InitUpTo_5de4f0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x18, 0x005ebfb0);  // Lobby::CImageDescription
}
void __fastcall InitUpTo_60ad90(VectorBase* self, void*, std::int32_t n) {
    InitUpToWithZeros(self, n, 0x24, 0x00601370);  // Lobby::CLMMessageBlock
}
void __fastcall InitUpTo_541710(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x18, 0x005ec1e0);  // Lobby::CGamePlayerStatus
}
void __fastcall InitUpTo_60a0f0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x14, 0x00601e40);  // Lobby::CLMGeneric
}
void __fastcall InitUpTo_5dff30(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x20, 0x005c46b0);  // LobbyClient::priv::CLobbyServer
}
void __fastcall InitUpTo_5bdc70(VectorBase* self, void*, std::int32_t n) {
    InitUpToWithZeros(self, n, 0x8, 0x005bd200);  // DirectX::CGameNetSession::CPlayerIndex
}
void __fastcall InitUpTo_5b82f0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWithZero(self, n, 0x6c, 0x005b6c30);  // DirectX::CDisplayMode
}
void __fastcall InitUpTo_5b8040(VectorBase* self, void*, std::int32_t n) {
    InitUpToWith(self, n, 0x30, 0x005b6740);  // DirectX::CDirectDrawDevice
}
void __fastcall InitUpTo_60a7e0(VectorBase* self, void*, std::int32_t n) {
    InitUpToWithZero(self, n, 0xc, 0x006065f0);  // Lobby::priv::CPlayerIndex
}
void __fastcall InitUpTo_5df410(VectorBase* self, void*, std::int32_t n) {
    InitUpTo(self, n, 0x20, [n](std::uint32_t element) {  // Lobby::CLMLobbyServerList::CServerData
        InitEmptyString(element, n);
        InitEmptyString(element + 0x10, n);
    });
}

S3RE_HOOK(0x00428e50, InitUpTo_428e50, InitUpToFn, Original_InitUpTo_428e50, InitUpTo_428e50);
S3RE_HOOK(0x0040a1d0, InitUpTo_40a1d0, InitUpToFn, Original_InitUpTo_40a1d0, InitUpTo_40a1d0);
S3RE_HOOK(0x00429040, InitUpTo_429040, InitUpToFn, Original_InitUpTo_429040, InitUpTo_429040);
S3RE_HOOK(0x004293d0, InitUpTo_4293d0, InitUpToFn, Original_InitUpTo_4293d0, InitUpTo_4293d0);
S3RE_HOOK(0x00414650, InitUpTo_414650, InitUpToFn, Original_InitUpTo_414650, InitUpTo_414650);
S3RE_HOOK(0x00414b10, InitUpTo_414b10, InitUpToFn, Original_InitUpTo_414b10, InitUpTo_414b10);
S3RE_HOOK(0x00463350, InitUpTo_463350, InitUpToFn, Original_InitUpTo_463350, InitUpTo_463350);
S3RE_HOOK(0x00462d70, InitUpTo_462d70, InitUpToFn, Original_InitUpTo_462d70, InitUpTo_462d70);
S3RE_HOOK(0x0044dee0, InitUpTo_44dee0, InitUpToFn, Original_InitUpTo_44dee0, InitUpTo_44dee0);
S3RE_HOOK(0x0044dc90, InitUpTo_44dc90, InitUpToFn, Original_InitUpTo_44dc90, InitUpTo_44dc90);
S3RE_HOOK(0x0044d960, InitUpTo_44d960, InitUpToFn, Original_InitUpTo_44d960, InitUpTo_44d960);
S3RE_HOOK(0x0044d460, InitUpTo_44d460, InitUpToFn, Original_InitUpTo_44d460, InitUpTo_44d460);
S3RE_HOOK(0x0044d050, InitUpTo_44d050, InitUpToFn, Original_InitUpTo_44d050, InitUpTo_44d050);
S3RE_HOOK(0x005deae0, InitUpTo_5deae0, InitUpToFn, Original_InitUpTo_5deae0, InitUpTo_5deae0);
S3RE_HOOK(0x005de4f0, InitUpTo_5de4f0, InitUpToFn, Original_InitUpTo_5de4f0, InitUpTo_5de4f0);
S3RE_HOOK(0x0060ad90, InitUpTo_60ad90, InitUpToFn, Original_InitUpTo_60ad90, InitUpTo_60ad90);
S3RE_HOOK(0x00541710, InitUpTo_541710, InitUpToFn, Original_InitUpTo_541710, InitUpTo_541710);
S3RE_HOOK(0x0060a0f0, InitUpTo_60a0f0, InitUpToFn, Original_InitUpTo_60a0f0, InitUpTo_60a0f0);
S3RE_HOOK(0x005dff30, InitUpTo_5dff30, InitUpToFn, Original_InitUpTo_5dff30, InitUpTo_5dff30);
S3RE_HOOK(0x005bdc70, InitUpTo_5bdc70, InitUpToFn, Original_InitUpTo_5bdc70, InitUpTo_5bdc70);
S3RE_HOOK(0x005b82f0, InitUpTo_5b82f0, InitUpToFn, Original_InitUpTo_5b82f0, InitUpTo_5b82f0);
S3RE_HOOK(0x005b8040, InitUpTo_5b8040, InitUpToFn, Original_InitUpTo_5b8040, InitUpTo_5b8040);
S3RE_HOOK(0x0060a7e0, InitUpTo_60a7e0, InitUpToFn, Original_InitUpTo_60a7e0, InitUpTo_60a7e0);
S3RE_HOOK(0x005df410, InitUpTo_5df410, InitUpToFn, Original_InitUpTo_5df410, InitUpTo_5df410);

// ---------------------------------------------------------------------------
// Tests: random vectors (filled -2..8, n -2..8, a block of 10 elements with
// random bytes, sometimes none) with the element constructors replaced by
// recording stubs; compared: the vector, the block and the constructor
// calls (as offsets into the block).
// ---------------------------------------------------------------------------

namespace {

struct Case {
    InitUpToFn original;
    InitUpToFn ours;
    std::uint32_t elementSize;
};

const Case kCases[] = {
    {Original_InitUpTo_428e50, &InitUpTo_428e50, 4},     {Original_InitUpTo_40a1d0, &InitUpTo_40a1d0, 16},
    {Original_InitUpTo_429040, &InitUpTo_429040, 0x20},  {Original_InitUpTo_4293d0, &InitUpTo_4293d0, 0x14},
    {Original_InitUpTo_414650, &InitUpTo_414650, 0xcc},  {Original_InitUpTo_414b10, &InitUpTo_414b10, 0x40},
    {Original_InitUpTo_463350, &InitUpTo_463350, 0x60},
    {Original_InitUpTo_462d70, &InitUpTo_462d70, 0x40},
    {Original_InitUpTo_44dee0, &InitUpTo_44dee0, 0xc},
    {Original_InitUpTo_44dc90, &InitUpTo_44dc90, 0x20},
    {Original_InitUpTo_44d960, &InitUpTo_44d960, 0x17c},
    {Original_InitUpTo_44d460, &InitUpTo_44d460, 0x11c},
    {Original_InitUpTo_44d050, &InitUpTo_44d050, 0x54},
    {Original_InitUpTo_5deae0, &InitUpTo_5deae0, 0x24},
    {Original_InitUpTo_5de4f0, &InitUpTo_5de4f0, 0x18},
    {Original_InitUpTo_60ad90, &InitUpTo_60ad90, 0x24},
    {Original_InitUpTo_541710, &InitUpTo_541710, 0x18},
    {Original_InitUpTo_60a0f0, &InitUpTo_60a0f0, 0x14},
    {Original_InitUpTo_5dff30, &InitUpTo_5dff30, 0x20},
    {Original_InitUpTo_5bdc70, &InitUpTo_5bdc70, 0x8},
    {Original_InitUpTo_5b82f0, &InitUpTo_5b82f0, 0x6c},
    {Original_InitUpTo_5b8040, &InitUpTo_5b8040, 0x30},
    {Original_InitUpTo_60a7e0, &InitUpTo_60a7e0, 0xc},
    {Original_InitUpTo_5df410, &InitUpTo_5df410, 0x20},
};

constexpr std::uint32_t kElements = 10;

struct Run {
    VectorBase vector;
    std::vector<std::uint8_t> block;
    std::vector<test::StubCall> calls;
};

bool Compare(test::Context& ctx, const Case& c, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase initial;
        ctx.rng().Fill(&initial, sizeof(initial));
        initial.filled = ctx.rng().Range(-2, 8);
        std::int32_t n = ctx.rng().Range(-2, 8);
        // Elements below 0 lie before the block: keep them inside it.
        std::vector<std::uint8_t> block((kElements + 2) * c.elementSize);
        ctx.rng().Fill(block.data(), block.size());
        const bool withBlock = ctx.rng().Chance(7, 8);
        if (!withBlock) {
            // Only element 0 of a null block is skipped; others would be
            // wild addresses (in both versions).
            initial.filled = 0;
            n = n < 0 ? n : 0;
        }
        // The block pointer passed is 2 elements into the buffer.
        auto shifted = [&](InitUpToFn fn) {
            Run run{initial, block, {}};
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(run.block.data()) + 2 * c.elementSize;
            run.vector.data = withBlock ? base : 0;
            test::CalleeStubs stubs;
            stubs.Install<0, kColumnConstructor>();
            stubs.Install<0, kLineConstructor>();
            stubs.Install<0, kPlayerConstructor>();
            stubs.Install<0, kTeamConstructor>();
            stubs.Install<0, kRunningSessionConstructor>();
            stubs.Install<0, 0x005ba300>();
            stubs.Install<0, 0x005dc810>();
            stubs.Install<0, 0x005dc1e0>();
            stubs.Install<0, 0x005dc4e0>();
            stubs.Install<0, 0x005da750>();
            stubs.Install<0, 0x005d9e20>();
            stubs.Install<2, 0x005ec9f0>();
            stubs.Install<0, 0x005ebfb0>();
            stubs.Install<2, 0x00601370>();
            stubs.Install<0, 0x005ec1e0>();
            stubs.Install<0, 0x00601e40>();
            stubs.Install<0, 0x005c46b0>();
            stubs.Install<2, 0x005bd200>();
            stubs.Install<1, 0x005b6c30>();
            stubs.Install<0, 0x005b6740>();
            stubs.Install<1, 0x006065f0>();
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(base));
            fn(&run.vector, nullptr, n);
            run.calls = stubs.Take();
            run.vector.data = 0;
            return run;
        };
        const Run a = shifted(c.original);
        const Run b = shifted(c.ours);
        if (!S3RE_EXPECT_MEM_EQ(&a.vector, &b.vector, sizeof(a.vector)) ||
            !S3RE_EXPECT(a.block == b.block) || !S3RE_EXPECT(a.calls == b.calls)) {
            ctx.Fail(__FILE__, __LINE__, "  size 0x%x case %d: filled %d, n %d, block %d", c.elementSize, i,
                     initial.filled, n, withBlock);
            return false;
        }
    }
    return true;
}

// Wrong: the String version leaves the allocator byte alone.
void __fastcall WrongStringInit(VectorBase* self, void*, std::int32_t n) {
    InitUpTo(self, n, sizeof(msvc6::String), [](std::uint32_t element) {
        auto* string = reinterpret_cast<msvc6::String*>(element);
        string->ptr = nullptr;
        string->len = 0;
        string->res = 0;
    });
}
// Wrong: one element fewer.
void __fastcall WrongPlayerInit(VectorBase* self, void* edx, std::int32_t n) {
    InitUpTo_414650(self, edx, n - 1);
    if (self->filled == n) {
        self->filled = n + 1;
    }
}

}  // namespace

S3RE_TEST(WatcomVectorInitUpTo_MatchOriginal) {
    for (const Case& c : kCases) {
        if (!Compare(ctx, c, 300)) {
            return;
        }
    }
}

S3RE_TEST(WatcomVectorInitUpTo_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Compare(probe, {Original_InitUpTo_40a1d0, &WrongStringInit, 16}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Compare(probe, {Original_InitUpTo_414650, &WrongPlayerInit, 0xcc}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
