// Construction, reset and destruction of the game settings (g_gameSettings
// 0x7a7a18, game_settings_layout.h; Dirk\GameParams.cpp and the static
// initializer block of Dirk\ConsoleInit.cpp). All thiscall.
//
// GameSettings::GameSettings (0x00431780): () -> this. The critical section
//   (Win32::CCriticalSection 0x60d650), the strings +0x18 and +0x2c empty
//   (their allocator bytes are an uninitialized byte of the original's
//   frame), the team vector +0x3c (WCBareVectorBase<CTeam>(10) 0x437c90,
//   length 0, grow 5, vtable WCValSortedVector<CTeam>), the player vector
//   +0x58 (0x4377a0(10), the same, WCValOrderedVector<CPlayer>), map file
//   +0x84 = 0, the net-id vector +0x90 (0x437730(10), WCValOrderedVector
//   <int>); then, under the lock, Reset and srand(time(0)) (CRT 0x61f6a7 /
//   0x61f7f5). Called by the static initializer of g_gameSettings.
// GameSettings::Reset (0x00433810): () under the lock: +0x18 = "siedler3.evt"
//   and the map name = "" (each through a temporary built by _Tidy(false) +
//   assign(text, n) 0x4021c0, then assign(temp, 0, npos) 0x401d60; the
//   temporaries' allocator bytes are the frame's), the four flags +0x28..
//   +0x2b = 0, the team and player vectors resized to 0 (0x437d00 /
//   0x437820) with length 0, start goods 0, local player id -1, host 0,
//   +0x8c = 0, the map file deleted (vtable slot 0, flags 1) and cleared,
//   the net-id vector's block freed (operator delete, also of a null block)
//   with filled = min(filled, 0) and data / capacity / length 0, local net
//   id -1, the GUID = GUID_NULL (0x65c0b8), +0xc0, +0xc4 (word), +0xc8,
//   +0xcc, +0xcd = 0. EAX: the lock's leftover (callers: the constructor,
//   NewGame and others; none uses it). Was GameSetup_sub_433810.
// GameSettings::~GameSettings (0x0040cf10): the net-id vector (vtable
//   WCVectorBase<int>, ~WCBareVectorBase<int> 0x414590), the player vector
//   (WCVectorBase<CPlayer>, 0x4146d0), the team vector inlined (vtable
//   WCBareVectorBase<CTeam>; with a block: the library's empty error hook
//   0x59f200, each of the `filled` teams through CTeam's scalar deleting
//   destructor 0x414b90 with flags 0, operator delete; then WCExcept's
//   vtable), the map name and +0x18 (erase(0, npos) and _Tidy(true)
//   inlined), the critical section (0x60d660). Called by the static
//   destructor of g_gameSettings.

#include "game/fileformats/map_file.h"
#include "game/framework/critical_section.h"
#include "game/framework/exception_strings.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "watcom/vector_base.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace s3re::game {

namespace {

using VectorConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t capacity);
using VectorResizeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t length);
using VectorDestructFn = void(__fastcall*)(void* self, void* edx);
using ObjectFn = void(__fastcall*)(void* self, void* edx);
using DeletingDestructorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
constexpr std::uintptr_t kCriticalSectionConstruct = 0x0060d650;  // Win32::CCriticalSection::CCriticalSection
constexpr std::uintptr_t kCriticalSectionDestruct = 0x0060d660;   // Win32::CCriticalSection::~CCriticalSection
constexpr std::uintptr_t kTeamVectorConstruct = 0x00437c90;       // WCBareVectorBase<Dirk::CTeam>(capacity)
constexpr std::uintptr_t kPlayerVectorConstruct = 0x004377a0;     // WCBareVectorBase<Dirk::CPlayer>(capacity)
constexpr std::uintptr_t kIntVectorConstruct = 0x00437730;        // WCBareVectorBase<int>(capacity)
constexpr std::uintptr_t kTeamVectorResize = 0x00437d00;          // WCValSortedVector<Dirk::CTeam>::resize
constexpr std::uintptr_t kPlayerVectorResize = 0x00437820;        // WCValOrderedVector<Dirk::CPlayer>::resize
constexpr std::uintptr_t kIntVectorDestruct = 0x00414590;         // ~WCBareVectorBase<int>
constexpr std::uintptr_t kPlayerVectorDestruct = 0x004146d0;      // ~WCBareVectorBase<Dirk::CPlayer>
constexpr std::uintptr_t kTeamDeletingDestructor = 0x00414b90;    // Dirk::CTeam's scalar deleting destructor
constexpr std::uintptr_t kReset = 0x00433810;                     // GameSettings::Reset (ours, through its hook)
using ResetFn = void(__fastcall*)(GameSettings* self, void* edx);
using TimeFn = std::int32_t(__cdecl*)(std::int32_t* out);
using SrandFn = void(__cdecl*)(std::uint32_t seed);
constexpr std::uintptr_t kTime = 0x0061f6a7;   // the CRT's time
constexpr std::uintptr_t kSrand = 0x0061f7f5;  // the CRT's srand

constexpr std::uint32_t kInitialCapacity = 10;
constexpr std::uint32_t kGrowStep = 5;
constexpr std::uint32_t kTeamBytes = 0x40;
constexpr std::uint32_t kNoPlayer = 0xffffffffu;

// The allocator byte the original takes from an uninitialized byte of its
// frame (the spec ignores these bytes).
constexpr std::uint8_t kFrameAllocator = 0;

// target = std::string(text) through a temporary (the compiler's inlined
// constructor: _Tidy(false), assign(text, n)), then assign(temp, 0, npos).
void AssignText(msvc6::String* target, const char* text) {
    msvc6::String temp;
    framework::MakeEmpty(&temp, kFrameAllocator);
    msvc6::String_AssignChars(&temp, nullptr, text, msvc6::detail::Length(text));
    msvc6::String_AssignSubstr(target, nullptr, &temp, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&temp, nullptr, 1);
}

void* VectorAt(watcom::VectorBase& vector) {
    return &vector;
}

}  // namespace

// Purpose: Puts the settings back to "no game set up": default texts, no
// teams or players, no map file, no network ids.
void __fastcall GameSettings_Reset(GameSettings* self, void*) {
    GameLock lock(self->lock);
    AssignText(&self->string18, texts::kSiedler3Evt.c_str());
    AssignText(&self->map_name, texts::kEmpty.c_str());
    self->is_savegame = 0;
    self->multiplayer = 0;
    self->option2a = 0;
    self->by_clan = 0;
    reinterpret_cast<VectorResizeFn>(kTeamVectorResize)(VectorAt(self->teams), nullptr, 0);
    self->teams.length = 0;
    reinterpret_cast<VectorResizeFn>(kPlayerVectorResize)(&self->player_setups, nullptr, 0);
    self->player_setups.length = 0;
    self->start_goods = 0;
    self->local_player_id = kNoPlayer;
    self->is_host = 0;
    self->slot_infos_requested = 0;
    if (self->map_file != nullptr) {
        const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(self->map_file);
        reinterpret_cast<DeletingDestructorFn>(vtable[0])(self->map_file, nullptr, 1);
    }
    self->map_file = nullptr;
    // 0x4339eb: the net-id vector emptied in place (its block freed, even
    // a null one; a negative fill count is kept).
    watcom::VectorBase& netIds = self->net_ids;
    const std::int32_t filled = netIds.filled < 0 ? netIds.filled : 0;
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(netIds.data)));
    netIds.filled = filled;
    netIds.data = 0;
    netIds.capacity = 0;
    netIds.length = 0;
    self->local_net_id = kNoPlayer;
    std::memcpy(self->game_guid, g_guidNull.get(), sizeof(self->game_guid));
    self->saved_tick = 0;
    self->saved_seed = 0;
    self->valueC8 = 0;
    self->randomRaceAmazons = 0;
    self->goods_objective = 0;
}

// Purpose: Builds the global game settings object at program start.
GameSettings* __fastcall GameSettings_Construct(GameSettings* self, void*) {
    reinterpret_cast<ObjectFn>(kCriticalSectionConstruct)(self->lock, nullptr);
    framework::MakeEmpty(&self->string18, kFrameAllocator);
    self->map_name.allocator = kFrameAllocator;
    msvc6::detail::Clear(&self->map_name);
    reinterpret_cast<VectorConstructFn>(kTeamVectorConstruct)(&self->teams, nullptr, kInitialCapacity);
    self->teams.length = 0;
    self->teams.grow = kGrowStep;
    self->teams.vtable = vtables::kWCValSortedVector_CTeam;
    reinterpret_cast<VectorConstructFn>(kPlayerVectorConstruct)(&self->player_setups, nullptr, kInitialCapacity);
    self->player_setups.length = 0;
    self->player_setups_grow = kGrowStep;
    self->player_setups.vtable = vtables::kWCValOrderedVector_CPlayer;
    self->map_file = nullptr;
    reinterpret_cast<VectorConstructFn>(kIntVectorConstruct)(&self->net_ids, nullptr, kInitialCapacity);
    self->net_ids.length = 0;
    self->net_ids.grow = kGrowStep;
    self->net_ids.vtable = vtables::kWCValOrderedVector_int;
    {
        GameLock lock(self->lock);
        reinterpret_cast<ResetFn>(kReset)(self, nullptr);
        reinterpret_cast<SrandFn>(kSrand)(static_cast<std::uint32_t>(reinterpret_cast<TimeFn>(kTime)(nullptr)));
    }
    return self;
}

// Purpose: Destroys the global game settings object at program exit.
void __fastcall GameSettings_Destruct(GameSettings* self, void*) {
    self->net_ids.vtable = vtables::kWCVectorBase_int;
    reinterpret_cast<VectorDestructFn>(kIntVectorDestruct)(&self->net_ids, nullptr);
    self->player_setups.vtable = vtables::kWCVectorBase_CPlayer;
    reinterpret_cast<VectorDestructFn>(kPlayerVectorDestruct)(&self->player_setups, nullptr);
    watcom::DestroyVector(&self->teams, vtables::kWCBareVectorBase_CTeam, vtables::kWCExcept, kTeamBytes,
                          [](std::uint32_t team) {
                              reinterpret_cast<DeletingDestructorFn>(kTeamDeletingDestructor)(
                                  reinterpret_cast<void*>(static_cast<std::uintptr_t>(team)), nullptr, 0);
                          });
    framework::EraseAndFree(&self->map_name);
    framework::EraseAndFree(&self->string18);
    reinterpret_cast<ObjectFn>(kCriticalSectionDestruct)(self->lock, nullptr);
}

using ConstructFn = GameSettings*(__fastcall*)(GameSettings* self, void* edx);
S3RE_HOOK(0x00433810, GameSettings_Reset, ResetFn, Original_GameSettings_Reset, GameSettings_Reset);
S3RE_HOOK(0x00431780, GameSettings_Construct, ConstructFn, Original_GameSettings_Construct, GameSettings_Construct);
S3RE_HOOK(0x0040cf10, GameSettings_Destruct, ResetFn, Original_GameSettings_Destruct, GameSettings_Destruct);

namespace {

std::string Text(const msvc6::String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

// The parts of a vector that don't depend on where the heap put its block.
bool SameVector(test::Context& ctx, const void* a, const void* b) {
    const auto* va = static_cast<const watcom::VectorBase*>(a);
    const auto* vb = static_cast<const watcom::VectorBase*>(b);
    return S3RE_EXPECT(va->vtable == vb->vtable && va->unk04 == vb->unk04 && va->capacity == vb->capacity &&
                       va->filled == vb->filled && va->length == vb->length && va->grow == vb->grow &&
                       (va->data == 0) == (vb->data == 0));
}

// Two settings objects after the original and ours: plain fields byte for
// byte, strings by text and capacity, vectors without their block address
// (the strings' allocator bytes are the original frame's leftovers).
bool SameSettings(test::Context& ctx, const GameSettings& a, const GameSettings& b) {
    const auto* pa = reinterpret_cast<const std::uint8_t*>(&a);
    const auto* pb = reinterpret_cast<const std::uint8_t*>(&b);
    return S3RE_EXPECT(Text(a.string18) == Text(b.string18) && a.string18.res == b.string18.res) &&
           S3RE_EXPECT(Text(a.map_name) == Text(b.map_name) && a.map_name.res == b.map_name.res) &&
           S3RE_EXPECT_MEM_EQ(pa + 0x28, pb + 0x28, 4) && SameVector(ctx, &a.teams, &b.teams) &&
           S3RE_EXPECT(a.player_setups.vtable == b.player_setups.vtable &&
                       a.player_setups.capacity == b.player_setups.capacity &&
                       a.player_setups.filled == b.player_setups.filled &&
                       a.player_setups.length == b.player_setups.length &&
                       a.player_setups_grow == b.player_setups_grow &&
                       (a.player_setups.data == nullptr) == (b.player_setups.data == nullptr)) &&
           S3RE_EXPECT_MEM_EQ(pa + 0x74, pb + 0x74, 0x84 - 0x74) && S3RE_EXPECT(a.map_file == b.map_file) &&
           S3RE_EXPECT_MEM_EQ(pa + 0x88, pb + 0x88, 0x90 - 0x88) && SameVector(ctx, &a.net_ids, &b.net_ids) &&
           S3RE_EXPECT_MEM_EQ(pa + 0xac, pb + 0xac, sizeof(GameSettings) - 0xac);
}

struct FakeFile {
    const std::uintptr_t* vtable;
    std::uint32_t calls;
};

void* __fastcall FakeFileDelete(FakeFile* self, void*, std::uint32_t flags) {
    self->calls += flags;
    return self;
}

const std::uintptr_t kFakeFileVtable[] = {reinterpret_cast<std::uintptr_t>(&FakeFileDelete)};

using DestructFn = void(__fastcall*)(GameSettings* self, void* edx);

struct LifetimeFunctions {
    ConstructFn construct;
    ResetFn reset;
    DestructFn destruct;
};

// Builds a settings object on random memory, fills it like a game setup
// (texts, a map file, net ids), resets it, fills and destroys it, with the
// original functions on one object and the candidates on the other.
void CompareLifetime(test::Context& ctx, const LifetimeFunctions& candidate, int cases) {
    using IntInsertFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t index, const std::uint32_t* v);
    const auto insertInt = reinterpret_cast<IntInsertFn>(0x00428d10);  // watcom::PtrVector::insertAt (verified)
    for (int i = 0; i < cases; ++i) {
        alignas(8) std::uint8_t bytesA[sizeof(GameSettings)];
        ctx.rng().Fill(bytesA, sizeof(bytesA));
        alignas(8) std::uint8_t bytesB[sizeof(GameSettings)];
        std::memcpy(bytesB, bytesA, sizeof(bytesA));
        auto* a = reinterpret_cast<GameSettings*>(bytesA);
        auto* b = reinterpret_cast<GameSettings*>(bytesB);
        Original_GameSettings_Construct(a, nullptr);
        candidate.construct(b, nullptr);
        if (!SameSettings(ctx, *a, *b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: after the constructor", i);
            return;
        }
        FakeFile fileA{kFakeFileVtable, 0};
        FakeFile fileB{kFakeFileVtable, 0};
        const std::uint32_t netIds = ctx.rng().Below(4);
        const std::uint32_t name = ctx.rng().Below(40);
        auto fill = [&](GameSettings* s, FakeFile* file) {
            std::string text(name, 'x');
            msvc6::String_AssignChars(&s->map_name, nullptr, text.c_str(), name);
            s->map_file = ctx.rng().Chance(1, 2) ? reinterpret_cast<fileformats::CMapFile*>(file) : nullptr;
            for (std::uint32_t k = 0; k < netIds; ++k) {
                insertInt(&s->net_ids, nullptr, static_cast<std::int32_t>(k), &k);
            }
            s->local_player_id = 3;
            s->saved_tick = 77;
        };
        test::Rng saved = ctx.rng();
        fill(a, &fileA);
        ctx.rng() = saved;
        fill(b, &fileB);
        Original_GameSettings_Reset(a, nullptr);
        candidate.reset(b, nullptr);
        if (!SameSettings(ctx, *a, *b) || !S3RE_EXPECT_EQ(fileA.calls, fileB.calls)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: after Reset", i);
            return;
        }
        saved = ctx.rng();
        fill(a, &fileA);
        ctx.rng() = saved;
        fill(b, &fileB);
        a->map_file = nullptr;
        b->map_file = nullptr;
        Original_GameSettings_Destruct(a, nullptr);
        candidate.destruct(b, nullptr);
        if (!S3RE_EXPECT(a->teams.vtable == b->teams.vtable && a->net_ids.vtable == b->net_ids.vtable &&
                         a->player_setups.vtable == b->player_setups.vtable && a->map_name.ptr == nullptr &&
                         b->map_name.ptr == nullptr && b->string18.ptr == nullptr)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: after the destructor", i);
            return;
        }
    }
}

// Wrong: Reset keeps the local player id.
void __fastcall WrongReset(GameSettings* self, void* edx) {
    const std::uint32_t id = self->local_player_id;
    GameSettings_Reset(self, edx);
    self->local_player_id = id;
}

}  // namespace

S3RE_TEST(GameSettingsLifetime_MatchesOriginal) {
    CompareLifetime(ctx, {&GameSettings_Construct, &GameSettings_Reset, &GameSettings_Destruct}, 200);
}

S3RE_TEST(GameSettingsLifetime_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLifetime(probe, {&GameSettings_Construct, &WrongReset, &GameSettings_Destruct}, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
