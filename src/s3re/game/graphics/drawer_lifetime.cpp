// CDrawer's constructor and destructor (Dirk\Draw.cpp). Layout:
// game/graphics/drawer_layout.h.
//
// CDrawer::CDrawer (0x0042c820): (thread, directDraw, lobby) ret 0Ch:
//   DirectX::CGameDrawer's constructor (0x5b6c80) with the second argument;
//   then (the original's unwind state 0: when anything below throws,
//   ~CGameDrawer 0x5b6d70 runs on the way out) the image of every queued
//   shadow and colour layer constructed empty (GfxDword::Construct
//   0x5fc540 (0), 4000 each) and every queued sprite's image zeroed (8000);
//   +0x5f6b4 = -1, the graphics/sound pointers and the four surfaces = 0,
//   +0x5f6d0 = thread (the CDrawThread), byte +0x5f6d4 = lobby, the software-cursor byte =
//   0, mouse surface = 0, cursor = -1, text surface = 0, +0x5f6f8 / +0x5f6fc
//   = 0, vtable 0x656aec; the graphics data (operator new 0xc44,
//   CGraphicsData::CGraphicsData 0x5f67c0; the block freed if that throws;
//   Win32::ThrowOutOfMemory("lpGraphicsData", Draw.cpp line 204) when new
//   failed) and the sound data (0x14c, CSoundData::CSoundData 0x5fdb60;
//   "lpSoundData", line 207); CDrawer::ApplyScreenLayout (0x4c6d40). EAX =
//   this.
// CDrawer::~CDrawer (0x0042ca00): vtable 0x656aec; the sound data and the
//   graphics data deleted (slot 0 with 1, if set; the original's unwind
//   state 0: ~CGameDrawer runs also when one of them throws), then
//   ~CGameDrawer. EAX = its.
//
// The unwind actions are __finally blocks (they run during the unwinding,
// innermost first, as the original's).

#include "game/graphics/drawer_layout.h"

#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGameDrawerConstruct = 0x005b6c80;    // DirectX::CGameDrawer::CGameDrawer(dd) ret 4
constexpr std::uintptr_t kGameDrawerDestroy = 0x005b6d70;      // DirectX::CGameDrawer::~CGameDrawer() ret
constexpr std::uintptr_t kImageConstruct = 0x005fc540;         // GfxDword::Construct(image) ret 4
constexpr std::uintptr_t kGraphicsConstruct = 0x005f67c0;      // CGraphicsData::CGraphicsData() ret
constexpr std::uintptr_t kSoundConstruct = 0x005fdb60;         // CSoundData::CSoundData() ret
constexpr std::uintptr_t kApplyScreenLayout = 0x004c6d40;      // CDrawer::ApplyScreenLayout() ret
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;       // Win32::ThrowOutOfMemory(name, file; line)

constexpr std::uint32_t kGraphicsDataSize = 0xc44;  // sizeof(gfx::CGraphicsData)
constexpr std::uint32_t kSoundDataSize = 0x14c;     // sizeof(CSoundData)
constexpr std::uint32_t kGraphicsLine = 204;        // Draw.cpp, the lpGraphicsData check
constexpr std::uint32_t kSoundLine = 207;           // Draw.cpp, the lpSoundData check
constexpr std::int32_t kNoCursor = -1;
constexpr std::uint32_t kSlotDelete = 0;

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* name, const char* file, std::uint32_t line);

// operator new(size), then the constructor on the block (null stays null);
// the block is freed when the constructor throws (unwind states 1 / 2).
void* NewConstructed(std::uint32_t size, std::uintptr_t constructor) {
    void* const block = msvc6::GameOperatorNew(size);
    if (block == nullptr) {
        return nullptr;
    }
    bool constructed = false;
    void* object = nullptr;
    __try {
        object = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(reinterpret_cast<MemberFn>(constructor)(block, nullptr)));
        constructed = true;
    } __finally {
        if (!constructed) {
            msvc6::GameOperatorDelete(block);
        }
    }
    return object;
}

// 0x42c84a..0x42c995: everything after the base constructor.
void ConstructMembers(CDrawer* self, void* thread, std::uint32_t lobby) {
    for (ShadowRecord& record : self->shadows.records) {
        reinterpret_cast<Member1Fn>(kImageConstruct)(&record.sprite, nullptr, 0);
    }
    for (SpriteRecord& record : self->sprites.records) {
        record.sprite = 0;  // (GfxDword's constructor inlined)
    }
    for (ColourRecord& record : self->colour_layers.records) {
        reinterpret_cast<Member1Fn>(kImageConstruct)(&record.sprite, nullptr, 0);
    }
    self->unk05f6b4 = -1;
    self->graphics = nullptr;
    self->sound = nullptr;
    self->primary_surface = nullptr;
    self->back_surface = nullptr;
    self->draw_surface = nullptr;
    self->surface_5f6cc = nullptr;
    self->draw_thread = thread;
    self->lobby = static_cast<std::uint8_t>(lobby);
    self->software_cursor = 0;
    self->mouse_surface = nullptr;
    self->cursor_id = kNoCursor;
    self->text_surface = nullptr;
    self->flip_callback = nullptr;
    self->unk05f6fc = 0;
    self->vtable = reinterpret_cast<void* const*>(vtables::kCDrawer);
    self->graphics = NewConstructed(kGraphicsDataSize, kGraphicsConstruct);
    if (self->graphics == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kGraphicsDataNotNull, texts::kDrawFile,
                                                                kGraphicsLine);
    }
    self->sound = NewConstructed(kSoundDataSize, kSoundConstruct);
    if (self->sound == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kSoundDataNotNull, texts::kDrawFile,
                                                                kSoundLine);
    }
    reinterpret_cast<MemberFn>(kApplyScreenLayout)(self, nullptr);
}

void ConstructMembersGuarded(CDrawer* self, void* thread, std::uint32_t lobby) {
    bool done = false;
    __try {
        ConstructMembers(self, thread, lobby);
        done = true;
    } __finally {
        if (!done) {
            reinterpret_cast<MemberFn>(kGameDrawerDestroy)(self, nullptr);
        }
    }
}

void DeleteDataGuarded(CDrawer* self) {
    bool done = false;
    __try {
        if (self->sound != nullptr) {
            void* const object = self->sound;
            reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[kSlotDelete])(object, nullptr, 1);
        }
        if (self->graphics != nullptr) {
            void* const object = self->graphics;
            reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[kSlotDelete])(object, nullptr, 1);
        }
        done = true;
    } __finally {
        if (!done) {
            reinterpret_cast<MemberFn>(kGameDrawerDestroy)(self, nullptr);
        }
    }
}

}  // namespace

void* __fastcall CDrawer_Construct(CDrawer* self, void* edx, void* thread, void* directDraw, std::uint32_t lobby);
std::uint32_t __fastcall CDrawer_Destroy(CDrawer* self, void* edx);

using ConstructFn = decltype(&CDrawer_Construct);
using DestroyFn = decltype(&CDrawer_Destroy);

S3RE_HOOK(0x0042c820, CDrawer_Construct, ConstructFn, Original_CDrawer_Construct, CDrawer_Construct);
S3RE_HOOK(0x0042ca00, CDrawer_Destroy, DestroyFn, Original_CDrawer_Destroy, CDrawer_Destroy);

// Purpose: constructor of the in-game drawer: empty draw queues, no
// surfaces yet, and its graphics and sound data objects.
void* __fastcall CDrawer_Construct(CDrawer* self, void* /*edx*/, void* thread, void* directDraw, std::uint32_t lobby) {
    reinterpret_cast<Member1Fn>(kGameDrawerConstruct)(self, nullptr, reinterpret_cast<std::uint32_t>(directDraw));
    ConstructMembersGuarded(self, thread, lobby);
    return self;
}

// Purpose: destructor of the in-game drawer: deletes its sound and
// graphics data, then the framework part.
std::uint32_t __fastcall CDrawer_Destroy(CDrawer* self, void* /*edx*/) {
    self->vtable = reinterpret_cast<void* const*>(vtables::kCDrawer);
    DeleteDataGuarded(self);
    return reinterpret_cast<MemberFn>(kGameDrawerDestroy)(self, nullptr);
}

// ---------------------------------------------------------------------------
// Tests: a random drawer image; CGameDrawer's constructor / destructor,
// the CGraphicsData / CSoundData constructors and ApplyScreenLayout are
// recording stubs (the data constructors return their block; they and
// ApplyScreenLayout throw a game exception in some cases, which the test
// catches: compared then are the unwinding's calls: the freed block, and
// ~CGameDrawer); GfxDword::Construct runs. The heap blocks are real game
// heap blocks, recorded as "block 1 / 2" and freed afterwards when the
// code under test kept them. The destructor: the sound / graphics data
// null or fake objects whose deleting destructor records (and may throw).
// ---------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uint32_t kBlockTag = 0xb10c0000u;

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t throwAt = 0;  // 1 graphics constructor, 2 sound constructor, 3 ApplyScreenLayout, 4/5 deletes
    std::uint32_t blocks[2] = {};
    void* thrownObject = nullptr;
};
Script* g_script = nullptr;

void MaybeThrow(std::uint32_t point) {
    if (g_script->throwAt != point) {
        return;
    }
    void* object = g_script->thrownObject;
    using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&object, throw_info::kCHResultPointer);
}

std::uint32_t Tag(std::uint32_t address) {
    for (std::uint32_t i = 0; i < 2; ++i) {
        if (address != 0 && address == g_script->blocks[i]) {
            return kBlockTag + i + 1;
        }
    }
    return address;
}

std::uint32_t __fastcall FakeDeleteSound(void*, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xde50u, flags});
    MaybeThrow(4);
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeDeleteGraphics(void*, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xde60u, flags});
    MaybeThrow(5);
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeExceptionDelete(void*, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xe0de1u, flags});
    return 0;
}

struct Objects {
    const void* sound;
    const void* graphics;
    const void* exception;
    std::uint8_t exceptionRest[0x1c];  // (CatchGameException reads a CHResult's code at +0x18)
};

struct Outcome {
    std::uint32_t eax = 0;
    std::vector<std::uint8_t> drawer;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    test::CaughtException caught;
};

struct Guarded {
    bool construct;
    ConstructFn constructFn;
    DestroyFn destroyFn;
    CDrawer* drawer;
    const std::uint32_t* args;
};

std::uint32_t CallTarget(const Guarded* g) {
    if (g->construct) {
        const std::uint32_t* a = g->args;
        const void* result = g->constructFn(g->drawer, nullptr, reinterpret_cast<void*>(a[0]),
                                            reinterpret_cast<void*>(a[1]), a[2]);
        return result == g->drawer ? 1 : 0;
    }
    return g->destroyFn(g->drawer, nullptr);
}

void GuardedCall(const Guarded* g, std::uint32_t* eax, test::CaughtException* caught) {
    __try {
        *eax = CallTarget(g);
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
}

Outcome Run(const std::vector<std::uint8_t>& start, std::uint64_t seed, std::uint32_t throwAt, bool construct,
            const std::uint32_t* args, bool hasSound, bool hasGraphics, ConstructFn constructFn,
            DestroyFn destroyFn) {
    Outcome out;
    Script script;
    script.rng = test::Rng(seed);
    script.throwAt = throwAt;
    g_script = &script;
    out.drawer = start;
    CDrawer* const drawer = reinterpret_cast<CDrawer*>(out.drawer.data());
    static void* soundVtable[1] = {reinterpret_cast<void*>(&FakeDeleteSound)};
    static void* graphicsVtable[1] = {reinterpret_cast<void*>(&FakeDeleteGraphics)};
    static void* exceptionVtable[1] = {reinterpret_cast<void*>(&FakeExceptionDelete)};
    Objects objects{soundVtable, graphicsVtable, exceptionVtable, {}};
    script.thrownObject = &objects.exception;
    if (!construct) {
        drawer->sound = hasSound ? &objects.sound : nullptr;
        drawer->graphics = hasGraphics ? &objects.graphics : nullptr;
    }
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
        auto value = [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); };
        stubs.Install<1, kGameDrawerConstruct>(value);
        stubs.Install<0, kGameDrawerDestroy>(value);
        stubs.Install<0, kGraphicsConstruct>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
            const std::uint32_t block = self + reinterpret_cast<std::uint32_t>(drawer);
            script.blocks[0] = block;
            MaybeThrow(1);
            return block;
        });
        stubs.Install<0, kSoundConstruct>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
            const std::uint32_t block = self + reinterpret_cast<std::uint32_t>(drawer);
            script.blocks[1] = block;
            MaybeThrow(2);
            return block;
        });
        stubs.Install<0, kApplyScreenLayout>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
            MaybeThrow(3);
            return script.rng.Next32();
        });
        const Guarded g{construct, constructFn, destroyFn, drawer, args};
        GuardedCall(&g, &out.eax, &out.caught);
        out.calls = stubs.Take();
    }
    const auto base = reinterpret_cast<std::uint32_t>(drawer);
    for (test::StubCall& call : out.calls) {
        call.self = Tag(call.self + base);
        if (call.self == base) {
            call.self = 0xd0;  // (the drawer copy of each run)
        }
    }
    // Blocks the code under test kept (a constructor that threw had its block freed).
    if (construct) {
        if (throwAt != 1 && script.blocks[0] != 0) {
            msvc6::GameOperatorDelete(reinterpret_cast<void*>(script.blocks[0]));
        }
        if (throwAt != 2 && script.blocks[1] != 0) {
            msvc6::GameOperatorDelete(reinterpret_cast<void*>(script.blocks[1]));
        }
        drawer->graphics = reinterpret_cast<void*>(Tag(reinterpret_cast<std::uint32_t>(drawer->graphics)));
        drawer->sound = reinterpret_cast<void*>(Tag(reinterpret_cast<std::uint32_t>(drawer->sound)));
    } else {
        drawer->sound = drawer->sound == &objects.sound ? reinterpret_cast<void*>(1) : drawer->sound;
        drawer->graphics = drawer->graphics == &objects.graphics ? reinterpret_cast<void*>(2) : drawer->graphics;
    }
    out.log = script.log;
    g_script = nullptr;
    return out;
}

void Compare(test::Context& ctx, bool construct, ConstructFn constructFn, DestroyFn destroyFn, int cases) {
    std::vector<std::uint8_t> start(sizeof(CDrawer));
    ctx.rng().Fill(start.data(), start.size());
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint32_t throwAt = ctx.rng().Chance(1, 2) ? 0 : (construct ? 1 + ctx.rng().Below(3) : 4 + ctx.rng().Below(2));
        std::uint32_t args[3] = {ctx.rng().Next32(), ctx.rng().Next32(), ctx.rng().Next32()};
        const bool hasSound = ctx.rng().Chance(2, 3);
        const bool hasGraphics = ctx.rng().Chance(2, 3);
        const Outcome expected = Run(start, seed, throwAt, construct, args, hasSound, hasGraphics,
                                     Original_CDrawer_Construct, Original_CDrawer_Destroy);
        const Outcome actual = Run(start, seed, throwAt, construct, args, hasSound, hasGraphics, constructFn,
                                   destroyFn);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(expected.drawer.data(), actual.drawer.data(), sizeof(CDrawer));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.caught == actual.caught);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%s, throw at %u)", i, construct ? "construct" : "destroy",
                     throwAt);
            return;
        }
    }
}

// Wrong: the destructor's unwinding skips ~CGameDrawer.
std::uint32_t __fastcall WrongDestroy(CDrawer* self, void* /*edx*/) {
    self->vtable = reinterpret_cast<void* const*>(vtables::kCDrawer);
    if (self->sound != nullptr) {
        void* const object = self->sound;
        reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[kSlotDelete])(object, nullptr, 1);
    }
    if (self->graphics != nullptr) {
        void* const object = self->graphics;
        reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[kSlotDelete])(object, nullptr, 1);
    }
    return reinterpret_cast<MemberFn>(kGameDrawerDestroy)(self, nullptr);
}

}  // namespace

S3RE_TEST(CDrawer_Construct_MatchesOriginal) { Compare(ctx, true, CDrawer_Construct, nullptr, 60); }
S3RE_TEST(CDrawer_Destroy_MatchesOriginal) { Compare(ctx, false, nullptr, CDrawer_Destroy, 100); }
S3RE_TEST(CDrawer_Destroy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, false, nullptr, &WrongDestroy, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
