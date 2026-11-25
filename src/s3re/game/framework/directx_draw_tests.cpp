// Tests of directx_draw.cpp (the DirectDraw wrapper classes).
//
// Each case picks one function and runs the original on one arena and ours
// on another (the same random bytes, from the game's heap), under two
// FakeDDraw scripts with the same seed. The object under test sits at
// kObject; its interfaces point at fake COM objects, its owner (+0xc) at a
// fake game object whose +4 is the fake IDirectDraw2 (the owner is the
// CDirectDraw that creates and restores the surface). CDirectDraw objects
// get the fake game vtable (slots 4, 5 and 6 are logged). A deleting
// destructor that frees the object gets a heap copy of the object instead.
// Compared: EAX (arena addresses relative; not for the destructors, whose
// EAX callers ignore), the arena, the call logs and the thrown exception.
// g_app points at a fake application whose active flag is random.

#include "game/framework/directx_draw.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/fake_ddraw.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using test::CaughtException;
using test::FakeDDraw;

using F0 = std::uint32_t(__fastcall*)(void* self, void* edx);
using F1 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using F2 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using F3 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c);
using F4 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                      std::uint32_t d);
using F5 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                      std::uint32_t d, std::uint32_t e);

// How the object under test is set up, and where `self` points.
enum class Setup {
    None,         // random bytes
    DirectDraw,   // a CDirectDraw (fake game vtable)
    Surface,      // a CSurface at kObject
    Flip,         // a CFlipSurface object (self = the flip part; the constructors without
                  // mostDerived find the CSurface through the vbptr set before)
    FlipBase,     // ... self = its CSurface part
    Primary,      // a CPrimarySurface object (self = the vbptr)
    PrimaryBase,  // ... self = its CSurface part
    PrimaryFlip,  // a CPrimaryFlipSurface object
    PrimaryFlipBase,
    Palette,
};

// The arguments: values, or arena places.
enum class Arg { Value, Flags, Bool, Desc, DescOrNull, OutSlot, Rect, CapsA, CapsB, PixelFormat, Other, Guid, Fake, Count, Size };

struct Op {
    const char* name;
    const void* original;
    const void* ours;
    std::uint32_t arity;
    Setup setup;
    Arg args[5];
    bool deleting = false;   // a deleting destructor (arg 0 = flags)
    bool ignoreEax = false;  // destructors (EAX unused) and SetPixelFormat (EAX left as the caller had it)
};

constexpr std::size_t kArenaBytes = 0x800;
constexpr std::size_t kObject = 0x100;
constexpr std::size_t kOther = 0x200;    // a second CDirectDraw / a destination surface
constexpr std::size_t kDesc = 0x280;     // a DDSURFACEDESC
constexpr std::size_t kRect = 0x300;     // a RECT / a GUID
constexpr std::size_t kCapsA = 0x400;    // two DDCAPS
constexpr std::size_t kCapsB = 0x580;
constexpr std::size_t kPixelFormat = 0x700;
constexpr std::size_t kOutSlot = 0x740;
constexpr std::size_t kObjectBytes = 0x100;

// The real vbtables (vbtable[1] = the CSurface's offset: 0x10, 4, 0x14).
constexpr std::uint32_t kFlipVbtable = vtables::kCFlipSurface_vbtable;
constexpr std::uint32_t kPrimaryVbtable = vtables::kCPrimarySurface_vbtable;
constexpr std::uint32_t kPrimaryFlipVbtable = vtables::kCPrimaryFlipSurface_vbtable;
constexpr std::uint32_t kPrimaryFlipFlipVbtable = vtables::kCPrimaryFlipSurface_flipVbtable;

template <typename T>
const void* Fn(T f) {
    return reinterpret_cast<const void*>(f);
}

#define S3RE_OP(name, arity, setup, ...) \
    Op { #name, Fn(Original_##name), Fn(&name), arity, setup, __VA_ARGS__ }

std::vector<Op> Ops() {
    using A = Arg;
    using S = Setup;
    std::vector<Op> ops = {
        S3RE_OP(DirectX_CDirectDraw_Construct, 1, S::None, {A::Other}),
        S3RE_OP(DirectX_CDirectDraw_DeletingDestroy, 1, S::DirectDraw, {A::Value}, true, false),
        S3RE_OP(DirectX_CDirectDraw_Destroy, 0, S::DirectDraw, {}, false, true),
        S3RE_OP(DirectX_CDirectDraw_DDDirectDrawEnumerate, 2, S::DirectDraw, {A::Value, A::Value}),
        S3RE_OP(DirectX_CDirectDraw_Connect, 1, S::DirectDraw, {A::Guid}),
        S3RE_OP(DirectX_CDirectDraw_Disconnect, 0, S::DirectDraw, {}),
        S3RE_OP(DirectX_CDirectDraw_DDCreateSurface, 2, S::DirectDraw, {A::Desc, A::OutSlot}),
        S3RE_OP(DirectX_CDirectDraw_DDEnumDisplayModes, 4, S::DirectDraw, {A::Flags, A::DescOrNull, A::Value, A::Value}),
        S3RE_OP(DirectX_CDirectDraw_DDRestoreDisplayMode, 0, S::DirectDraw, {}),
        S3RE_OP(DirectX_CDirectDraw_DDGetDisplayMode, 1, S::DirectDraw, {A::Desc}),
        S3RE_OP(DirectX_CDirectDraw_DDGetCaps, 2, S::DirectDraw, {A::CapsA, A::CapsB}),
        S3RE_OP(DirectX_CDirectDraw_DDWaitForVerticalBlank, 2, S::DirectDraw, {A::Flags, A::Value}),
        S3RE_OP(DirectX_CDirectDraw_RestoreSurfaces, 0, S::DirectDraw, {}),
        S3RE_OP(DirectX_CSurface_DDLock, 4, S::Surface, {A::Rect, A::Desc, A::Flags, A::Value}),
        S3RE_OP(DirectX_CSurface_Lock, 3, S::Surface, {A::Rect, A::Desc, A::Flags}),
        S3RE_OP(DirectX_CSimpleSurface_DDGetBltStatus, 1, S::Surface, {A::Flags}),
        S3RE_OP(DirectX_CSimpleSurface_GetInterface, 0, S::Surface, {}),
        S3RE_OP(DirectX_CSurface_UseOwnInterfaceForDrawing, 0, S::Surface, {}),
        S3RE_OP(DirectX_CSimpleSurface_SetDirectDraw, 1, S::Surface, {A::Value}),
        S3RE_OP(DirectX_CSurface_DDGetSurfaceDesc, 0, S::Surface, {}),
        S3RE_OP(DirectX_CSurface_RestoreIfLost, 0, S::Surface, {}),
        S3RE_OP(DirectX_CSurface_SetCaps, 1, S::Surface, {A::Value}),
        S3RE_OP(DirectX_CSurface_SetPixelFormat, 1, S::Surface, {A::PixelFormat}, false, true),
        S3RE_OP(DirectX_CSurface_DDRestore, 0, S::Surface, {}),
        S3RE_OP(DirectX_CFlipSurface_Construct, 2, S::Flip, {A::Count, A::Bool}),
        S3RE_OP(DirectX_CFlipSurface_DeletingDestroy, 1, S::FlipBase, {A::Value}, true, false),
        S3RE_OP(DirectX_CFlipSurface_Destroy, 0, S::FlipBase, {}, false, true),
        S3RE_OP(DirectX_CFlipSurface_GetBackBuffer, 0, S::Flip, {}),
        S3RE_OP(DirectX_CFlipSurface_Flip, 1, S::Flip, {A::Fake}),
        S3RE_OP(DirectX_CFlipSurface_DDFlip, 2, S::Flip, {A::Fake, A::Flags}),
        S3RE_OP(DirectX_CPrimarySurface_Construct, 1, S::Primary, {A::Bool}),
        S3RE_OP(DirectX_CPrimarySurface_DeletingDestroy, 1, S::PrimaryBase, {A::Value}, true, false),
        S3RE_OP(DirectX_CPrimarySurface_Create, 0, S::Primary, {}),
        S3RE_OP(DirectX_CPrimaryFlipSurface_Construct, 2, S::PrimaryFlip, {A::Count, A::Bool}),
        S3RE_OP(DirectX_CPrimaryFlipSurface_DeletingDestroy, 1, S::PrimaryFlipBase, {A::Value}, true, false),
        S3RE_OP(DirectX_CPrimaryFlipSurface_Destroy, 0, S::PrimaryFlipBase, {}, false, true),
        S3RE_OP(DirectX_CPrimaryFlipSurface_Create, 0, S::PrimaryFlip, {}),
        S3RE_OP(DirectX_COffscreenSurface_Construct, 0, S::None, {}),
        S3RE_OP(DirectX_COffscreenSurface_DeletingDestroy, 1, S::Surface, {A::Value}, true, false),
        S3RE_OP(DirectX_COffscreenSurface_Create, 2, S::Surface, {A::Size, A::Size}),
        S3RE_OP(DirectX_COffscreenSurface_RestoreIfLost, 0, S::Surface, {}),
        S3RE_OP(DirectX_COverlaySurface_Construct, 0, S::None, {}),
        S3RE_OP(DirectX_COverlaySurface_Create, 2, S::Surface, {A::Size, A::Size}),
        S3RE_OP(DirectX_COverlaySurface_Hide, 1, S::Surface, {A::Other}),
        S3RE_OP(DirectX_COverlaySurface_DDUpdateOverlay, 5, S::Surface,
                {A::Rect, A::Other, A::Rect, A::Flags, A::Value}),
        S3RE_OP(DirectX_CPalette_Release, 0, S::Palette, {}),
    };
    return ops;
}

#undef S3RE_OP

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

void Put(std::uint8_t* at, std::uint32_t value) {
    std::memcpy(at, &value, 4);
}

void* RandomFakeSurface(test::Rng& rng) {
    return FakeDDraw::Surface(rng.Below(8));
}

// A CSurface at `at`: fake interfaces, the fake owner, a version.
void SetUpSurface(test::Rng& rng, std::uint8_t* at, bool nullInterface) {
    auto* const surface = reinterpret_cast<framework::CSurface*>(at);
    surface->simple.createdInterface = nullInterface ? nullptr : RandomFakeSurface(rng);
    surface->simple.surface = RandomFakeSurface(rng);
    surface->simple.owner = FakeDDraw::Game(0);
    static const std::uint32_t kVersions[] = {1, 2, 3, 3, 0, 4};
    surface->simple.version = rng.Chance(1, 8) ? rng.Next32() : kVersions[rng.Below(6)];
    if (rng.Chance(1, 2)) {
        surface->pixelFormat[0] = 0;
    }
}

struct Case {
    const Op* op;
    std::uint8_t* arena;
    std::uint8_t* object;  // the object (in the arena, or a heap copy for a deleting destructor)
    std::uint32_t selfOffset;
    std::uint32_t args[5];
};

std::uint32_t Dispatch(const void* fn, const Case* c) {
    void* const self = c->object + c->selfOffset;
    const std::uint32_t* a = c->args;
    switch (c->op->arity) {
        case 0: return reinterpret_cast<F0>(fn)(self, nullptr);
        case 1: return reinterpret_cast<F1>(fn)(self, nullptr, a[0]);
        case 2: return reinterpret_cast<F2>(fn)(self, nullptr, a[0], a[1]);
        case 3: return reinterpret_cast<F3>(fn)(self, nullptr, a[0], a[1], a[2]);
        case 4: return reinterpret_cast<F4>(fn)(self, nullptr, a[0], a[1], a[2], a[3]);
        default: return reinterpret_cast<F5>(fn)(self, nullptr, a[0], a[1], a[2], a[3], a[4]);
    }
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const void* fn, const Case* c) {
    Outcome out;
    __try {
        out.eax = Dispatch(fn, c);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

std::uint32_t Relative(std::uint32_t value, const std::uint8_t* arena, const std::uint8_t* object) {
    const auto base = Address(arena);
    if (value >= base && value < base + kArenaBytes) {
        return 0x10000 + (value - base);
    }
    const auto objectBase = Address(object);
    if (value >= objectBase && value < objectBase + kObjectBytes) {
        return 0x20000 + (value - objectBase);
    }
    return value;
}

std::uint32_t ArgValue(test::Rng& rng, Arg arg, std::uint8_t* arena) {
    switch (arg) {
        case Arg::Value: return rng.Chance(1, 4) ? rng.Below(4) : rng.Next32();
        case Arg::Flags: return rng.Chance(1, 3) ? 0 : (rng.Chance(1, 2) ? rng.Below(0x1000) : rng.Next32());
        case Arg::Bool: return rng.Chance(1, 2) ? rng.Below(2) : rng.Next32();
        case Arg::Desc: return Address(arena + kDesc);
        case Arg::DescOrNull: return rng.Chance(1, 4) ? 0 : Address(arena + kDesc);
        case Arg::OutSlot: return Address(arena + kOutSlot);
        case Arg::Rect: return rng.Chance(1, 3) ? 0 : Address(arena + kRect);
        case Arg::CapsA: return Address(arena + kCapsA);
        case Arg::CapsB: return Address(arena + kCapsB);
        case Arg::PixelFormat: return Address(arena + kPixelFormat);
        case Arg::Other: return rng.Chance(1, 3) ? 0 : Address(arena + kOther);
        case Arg::Guid: return rng.Chance(1, 2) ? 0 : Address(arena + kRect);
        case Arg::Fake: return rng.Chance(1, 2) ? 0 : Address(RandomFakeSurface(rng));
        case Arg::Count: {
            static const std::uint32_t kCounts[] = {1, 1, 2, 2, 0, 3};
            return rng.Chance(1, 8) ? rng.Next32() : kCounts[rng.Below(6)];
        }
        case Arg::Size: return rng.Chance(1, 2) ? 640 + 160 * rng.Below(3) : rng.Below(5000);
    }
    return 0;
}

// The object and its neighbours; returns the offset of `self` in the object.
std::uint32_t SetUp(test::Rng& rng, const Op& op, std::uint8_t* arena) {
    std::uint8_t* const object = arena + kObject;
    // The owner: +4 the fake IDirectDraw2 (DDCreateSurface reads it).
    void* const directDraw = FakeDDraw::DirectDraw();
    std::memcpy(FakeDDraw::Game(0)->fields, &directDraw, 4);
    // The other CDirectDraw / destination surface: its interfaces.
    Put(arena + kOther + 4, Address(RandomFakeSurface(rng)));
    // A desc with flags (DDCreateSurface logs the fields they name).
    Put(arena + kDesc + 4, rng.Chance(1, 2) ? rng.Next32() : 0x1027);
    switch (op.setup) {
        case Setup::None: return 0;
        case Setup::DirectDraw: {
            auto* const draw = reinterpret_cast<framework::CDirectDraw*>(object);
            draw->vtable = Address(FakeDDraw::GameVtable());
            const bool owns = rng.Chance(1, 2);
            draw->owns = owns ? 1 : (rng.Chance(1, 2) ? 0 : static_cast<std::uint8_t>(rng.Below(256)));
            const bool nullAllowed = op.deleting || op.ignoreEax || op.original == Fn(Original_DirectX_CDirectDraw_Construct);
            draw->directDraw = nullAllowed && rng.Chance(1, 4) ? nullptr : directDraw;
            return 0;
        }
        case Setup::Surface:
            SetUpSurface(rng, object, op.deleting && rng.Chance(1, 4));
            return 0;
        case Setup::Flip:
        case Setup::FlipBase: {
            auto* const flip = reinterpret_cast<framework::CFlipSurfaceObject*>(object);
            flip->flip.vbtable = kFlipVbtable;
            flip->flip.unk08 = rng.Chance(1, 2) ? nullptr : FakeDDraw::Game(2);
            flip->flip.backBuffer = rng.Chance(1, 2) ? nullptr : FakeDDraw::Game(3);
            SetUpSurface(rng, object + 0x10, op.setup == Setup::FlipBase && rng.Chance(1, 4));
            return op.setup == Setup::FlipBase ? 0x10 : 0;
        }
        case Setup::Primary:
        case Setup::PrimaryBase:
            Put(object, kPrimaryVbtable);
            SetUpSurface(rng, object + 4, op.setup == Setup::PrimaryBase && rng.Chance(1, 4));
            return op.setup == Setup::PrimaryBase ? 4 : 0;
        case Setup::PrimaryFlip:
        case Setup::PrimaryFlipBase: {
            auto* const both = reinterpret_cast<framework::CPrimaryFlipSurfaceObject*>(object);
            both->vbtable = kPrimaryFlipVbtable;
            both->flip.vbtable = kPrimaryFlipFlipVbtable;
            static const std::uint32_t kCounts[] = {1, 1, 2, 2, 0, 3};
            both->flip.backBufferCount = rng.Chance(1, 8) ? rng.Next32() : kCounts[rng.Below(6)];
            both->flip.unk08 = rng.Chance(1, 2) ? nullptr : FakeDDraw::Game(2);
            both->flip.backBuffer = rng.Chance(1, 2) ? nullptr : FakeDDraw::Game(3);
            SetUpSurface(rng, object + 0x14, op.setup == Setup::PrimaryFlipBase && rng.Chance(1, 4));
            return op.setup == Setup::PrimaryFlipBase ? 0x14 : 0;
        }
        case Setup::Palette:
            Put(object + 4, rng.Chance(1, 4) ? 0 : Address(FakeDDraw::Palette(rng.Below(4))));
            return 0;
    }
    return 0;
}

constexpr std::size_t kActiveFlag = 0x1c;  // DirectX::CApplication::IsActive's byte
std::uint8_t g_fakeApplication[0x40];

bool CompareOnce(test::Context& ctx, const std::vector<Op>& ops, const std::vector<Op>& candidates,
                 std::size_t index) {
    test::Rng& rng = ctx.rng();
    const Op& op = ops[index];
    FakeDDraw::ResetObjects();
    FakeDDraw::SetGameSlotArgs(3, 0);  // owner: RestoreSurfaces
    FakeDDraw::SetGameSlotArgs(4, 0);  // CDirectDraw slot 4: restore every surface
    FakeDDraw::SetGameSlotArgs(5, 1);  // slot 5: ReleaseSurfaces(bool)
    FakeDDraw::SetGameSlotArgs(6, 1);  // slot 6: ReleasePalettes(bool)
    FakeDDraw::ResetObjects();
    std::vector<std::uint8_t> bytes(kArenaBytes);
    rng.Fill(bytes.data(), bytes.size());
    const std::uint32_t selfOffset = SetUp(rng, op, bytes.data());
    std::uint8_t gameFields[0xfc];
    std::memcpy(gameFields, FakeDDraw::Game(0)->fields, sizeof(gameFields));

    Case cases[2];
    std::uint8_t* arenas[2];
    for (int side = 0; side < 2; ++side) {
        arenas[side] = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kArenaBytes));
        std::memcpy(arenas[side], bytes.data(), kArenaBytes);
    }
    std::uint32_t args[5] = {};
    std::uint32_t rawArgs[5] = {};
    for (std::uint32_t i = 0; i < op.arity; ++i) {
        rawArgs[i] = ArgValue(rng, op.args[i], arenas[0]);
    }
    const bool deletes = op.deleting && (rawArgs[0] & 1) != 0;
    for (int side = 0; side < 2; ++side) {
        Case& c = cases[side];
        c.op = &(side == 0 ? ops : candidates)[index];
        c.arena = arenas[side];
        c.selfOffset = selfOffset;
        c.object = arenas[side] + kObject;
        if (deletes) {
            c.object = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kObjectBytes));
            std::memcpy(c.object, arenas[side] + kObject, kObjectBytes);
        }
        for (std::uint32_t i = 0; i < op.arity; ++i) {
            // Arena pointers point into this side's arena.
            const std::uint32_t base = Address(arenas[0]);
            args[i] = rawArgs[i] >= base && rawArgs[i] < base + kArenaBytes
                          ? rawArgs[i] - base + Address(arenas[side])
                          : rawArgs[i];
            c.args[i] = args[i];
        }
    }

    const std::uint64_t seed = rng.Next64();
    test::FakeDDrawConfig config;
    config.failChance = rng.Below(6);
    config.noInterfaceChance = rng.Below(8);
    const std::uint8_t active = rng.Chance(3, 4) ? 1 : (rng.Chance(1, 2) ? 0 : static_cast<std::uint8_t>(rng.Below(256)));
    std::uint8_t* const savedApp = g_app.get();
    FakeDDraw fakes[2] = {FakeDDraw(seed, config), FakeDDraw(seed, config)};
    Outcome outcomes[2];
    for (int side = 0; side < 2; ++side) {
        FakeDDraw::ResetObjects();
        std::memcpy(FakeDDraw::Game(0)->fields, gameFields, sizeof(gameFields));
        g_app = g_fakeApplication;
        g_fakeApplication[kActiveFlag] = active;
        fakes[side].SetArena(arenas[side], kArenaBytes);
        fakes[side].Install();
        outcomes[side] = Run(side == 0 ? op.original : candidates[index].ours, &cases[side]);
        fakes[side].Uninstall();
        g_app = savedApp;
    }

    // GetBackBuffer's new block differs per side: compare its contents and
    // EAX as "the new block", then forget it.
    const bool backBuffer = op.original == Fn(Original_DirectX_CFlipSurface_GetBackBuffer);
    void* made[2] = {};
    if (backBuffer) {
        for (int side = 0; side < 2; ++side) {
            auto* const flip = reinterpret_cast<framework::CFlipSurface*>(arenas[side] + kObject);
            if (flip->backBuffer != nullptr && flip->backBuffer != FakeDDraw::Game(3)) {
                made[side] = flip->backBuffer;
                flip->backBuffer = reinterpret_cast<void*>(0xbb);
                if (outcomes[side].eax == Address(made[side])) {
                    outcomes[side].eax = 0xbb;
                }
            }
        }
    }
    bool same = S3RE_EXPECT(outcomes[0].caught == outcomes[1].caught);
    if (!outcomes[0].caught.threw && !op.ignoreEax) {
        same &= S3RE_EXPECT_EQ(Relative(outcomes[0].eax, arenas[0], cases[0].object),
                               Relative(outcomes[1].eax, arenas[1], cases[1].object));
    }
    same &= S3RE_EXPECT(fakes[0].log_ == fakes[1].log_);
    same &= S3RE_EXPECT_EQ(made[0] != nullptr, made[1] != nullptr);
    if (made[0] != nullptr && made[1] != nullptr) {
        same &= S3RE_EXPECT_MEM_EQ(made[0], made[1], sizeof(framework::CSimpleSurface));
    }
    if (!deletes) {
        same &= S3RE_EXPECT_MEM_EQ(arenas[0], arenas[1], kArenaBytes);
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  op %s, fail chance %u", op.name, config.failChance);
        for (std::size_t i = 0; i < fakes[0].log_.size() || i < fakes[1].log_.size(); ++i) {
            ctx.Fail(__FILE__, __LINE__, "  log[%u]: %#x %#x", static_cast<unsigned>(i),
                     i < fakes[0].log_.size() ? fakes[0].log_[i] : 0xdead,
                     i < fakes[1].log_.size() ? fakes[1].log_[i] : 0xdead);
            if (i > 60) {
                break;
            }
        }
    }
    for (int side = 0; side < 2; ++side) {
        msvc6::GameOperatorDelete(arenas[side]);
    }
    return same;
}

void Compare(test::Context& ctx, const std::vector<Op>& candidates, int cases) {
    const std::vector<Op> ops = Ops();
    for (int i = 0; i < cases; ++i) {
        const std::size_t index = ctx.rng().Below(static_cast<std::uint32_t>(ops.size()));
        if (!CompareOnce(ctx, ops, candidates, index)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: the lock flags are not masked by the interface version.
std::uint32_t __fastcall WrongLock(void* self, void* edx, void* rect, void* desc, std::uint32_t flags) {
    static_cast<framework::CSurface*>(self)->simple.version = 0;
    return DirectX_CSurface_Lock(self, edx, rect, desc, flags);
}

// Wrong: the version is always 3.
std::uint32_t __fastcall WrongCreateSurface(void* self, void* edx, void* desc, void** out) {
    DirectX_CDirectDraw_DDCreateSurface(self, edx, desc, out);
    return 3;
}

void Replace(std::vector<Op>& ops, const void* original, const void* wrong) {
    for (Op& op : ops) {
        if (op.original == original) {
            op.ours = wrong;
        }
    }
}

}  // namespace

S3RE_TEST(DirectXDraw_MatchOriginal) {
    Compare(ctx, Ops(), 30000);
}

S3RE_TEST(DirectXDraw_ComparisonDetectsWrongImplementation) {
    std::vector<Op> wrong = Ops();
    Replace(wrong, Fn(Original_DirectX_CSurface_Lock), Fn(&WrongLock));
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ops();
    Replace(wrong, Fn(Original_DirectX_CDirectDraw_DDCreateSurface), Fn(&WrongCreateSurface));
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2, wrong, 3000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
