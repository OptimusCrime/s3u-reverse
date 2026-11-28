// The sorted list of the local players' network ids (g_gameSettings,
// game_settings_layout.h; Dirk\GameParams.cpp).
//
// GameSettings::SortedPlayerIdsOfType1 (0x00434cd0): (WCValSortedVector<int>*
//   result) ret 4 -> result, thiscall, under the settings' lock. A local
//   WCValSortedVector<int> (WCValVector<int>(10, 5) 0x4376b0, its own
//   vtable) collects, for each player setup i (0..length-1; the vector's
//   index check 0x437950 picks the element) whose type (getter 0x49f350)
//   is 1, the setup's network id (getter 0x49f4d0, +0x10) of element i
//   through the inlined operator[] (growth of an empty vector through
//   resize_clamp_length(1) 0x438070, dead here since i < length), inserted
//   at the position the sorted find (slot 3, occurrence 3) gives
//   (watcom::PtrVector::insertAt 0x428d10). The result is built as
//   WCBareVectorBase<int>(0) 0x437730 with length and grow 0 and the
//   sorted vector's vtable, gets the local's elements (base_copy 0x4732f0),
//   then length = the local's when it has a block (else 0) and grow = the
//   local's; the local is destroyed inline (WCBareVectorBase<int>'s vtable,
//   its block freed when there is one, WCExcept's vtable). Callers: the
//   drawer's peer messages, CNetThread::OnStart and the network code.

#include "game/framework/critical_section.h"
#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "watcom/vector_base.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::game {

namespace {

using IntVectorConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t capacity, std::uint32_t grow);
using BareConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t capacity);
using IndexCheckFn = std::int32_t(__fastcall*)(PlayerSetupVector* self, void* edx, std::int32_t index);
using ResizeClampFn = std::uint32_t(__fastcall*)(PlayerSetupVector* self, void* edx, std::uint32_t length);
using SetupGetterFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);
using FindFn = std::uint32_t(__fastcall*)(watcom::VectorBase* self, void* edx, const std::uint32_t* value,
                                          std::int32_t occurrence, std::int32_t* index);
using InsertAtFn = std::uint32_t(__fastcall*)(watcom::VectorBase* self, void* edx, std::int32_t index,
                                              const std::uint32_t* value);
using BaseCopyFn = void*(__fastcall*)(watcom::VectorBase* self, void* edx, const watcom::VectorBase* from);
using NotifyFn = void(__fastcall*)(PlayerSetupVector* self, void* edx, std::uint32_t argument);
constexpr std::uintptr_t kIntVectorConstruct = 0x004376b0;  // WCValVector<int>(capacity, grow) (verified)
constexpr std::uintptr_t kBareIntConstruct = 0x00437730;    // WCBareVectorBase<int>(capacity) (verified)
constexpr std::uintptr_t kIndexCheck = 0x00437950;          // WCValOrderedVector<CPlayer>::IndexCheck (verified)
constexpr std::uintptr_t kResizeClamp = 0x00438070;         // WCValOrderedVector<CPlayer>::resize_clamp_length
constexpr std::uintptr_t kSetupType = 0x0049f350;           // GamePlayerSetup's type getter (verified)
constexpr std::uintptr_t kSetupNetId = 0x0049f4d0;          // GamePlayerSetup's +0x10 getter (verified)
constexpr std::uintptr_t kInsertAt = 0x00428d10;            // watcom::PtrVector::insertAt (verified)
constexpr std::uintptr_t kBaseCopy = 0x004732f0;            // WCBareVectorBase<void>::base_copy (verified)
constexpr std::uint32_t kLocalCapacity = 10;
constexpr std::uint32_t kLocalGrow = 5;
constexpr std::uint32_t kTypeLocal = 1;  // GamePlayerSetup::type of the local player(s)
constexpr std::uint32_t kFindSlot = 3;
constexpr std::int32_t kInsertPosition = 3;

GamePlayerSetup* SetupAt(PlayerSetupVector* vector, std::int32_t index) {
    return reinterpret_cast<GamePlayerSetup*>(reinterpret_cast<std::uintptr_t>(vector->data) +
                                              static_cast<std::uint32_t>(index) * sizeof(GamePlayerSetup));
}

// The inlined operator[] of this function (0x434d5a..0x434db1): growth of
// an empty vector through resize_clamp_length(1).
GamePlayerSetup* InlinedAt(PlayerSetupVector* vector, std::int32_t index) {
    const std::uint32_t length = vector->length;
    const auto last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return SetupAt(vector, index);
    }
    if (length != 0) {
        return SetupAt(vector, index < 0 ? 0 : last);
    }
    if (vector->capacity == 0 && reinterpret_cast<ResizeClampFn>(kResizeClamp)(vector, nullptr, 1) == 0) {
        return SetupAt(vector, 0);
    }
    const auto* vtable = reinterpret_cast<const NotifyFn*>(vector->vtable);
    vtable[1](vector, nullptr, 0);
    vector->length = 1;
    return SetupAt(vector, 0);
}

}  // namespace

// Purpose: Lists the network ids of the local player setups, sorted (for
// the network threads and the drawer's connection messages).
watcom::VectorBase* __fastcall GameSettings_SortedPlayerIdsOfType1(GameSettings* self, void*,
                                                                   watcom::VectorBase* result) {
    GameLock lock(self->lock);
    watcom::VectorBase local;
    reinterpret_cast<IntVectorConstructFn>(kIntVectorConstruct)(&local, nullptr, kLocalCapacity, kLocalGrow);
    local.vtable = vtables::kWCValSortedVector_int;
    PlayerSetupVector* setups = &self->player_setups;
    for (std::uint32_t i = 0; i < self->player_setups.length; ++i) {
        const std::int32_t checked = reinterpret_cast<IndexCheckFn>(kIndexCheck)(setups, nullptr,
                                                                                 static_cast<std::int32_t>(i));
        if (reinterpret_cast<SetupGetterFn>(kSetupType)(SetupAt(setups, checked), nullptr) != kTypeLocal) {
            continue;
        }
        const std::uint32_t id =
            reinterpret_cast<SetupGetterFn>(kSetupNetId)(InlinedAt(setups, static_cast<std::int32_t>(i)), nullptr);
        std::int32_t position = 0;
        if (local.length != 0) {
            const auto* vtable = reinterpret_cast<const std::uintptr_t*>(static_cast<std::uintptr_t>(local.vtable));
            reinterpret_cast<FindFn>(vtable[kFindSlot])(&local, nullptr, &id, kInsertPosition, &position);
        }
        reinterpret_cast<InsertAtFn>(kInsertAt)(&local, nullptr, position, &id);
    }
    reinterpret_cast<BareConstructFn>(kBareIntConstruct)(result, nullptr, 0);
    result->length = 0;
    result->grow = 0;
    result->vtable = vtables::kWCValSortedVector_int;
    reinterpret_cast<BaseCopyFn>(kBaseCopy)(result, nullptr, &local);
    result->grow = local.grow;
    result->length = result->data != 0 ? local.length : 0;
    // 0x434e5d: the local destroyed (~WCBareVectorBase<int> inlined).
    local.vtable = vtables::kWCBareVectorBase_int;
    if (local.data != 0) {
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(local.data)));
    }
    local.vtable = vtables::kWCExcept;
    return result;
}

using SortedIdsFn = watcom::VectorBase*(__fastcall*)(GameSettings* self, void* edx, watcom::VectorBase* result);
S3RE_HOOK(0x00434cd0, GameSettings_SortedPlayerIdsOfType1, SortedIdsFn, Original_GameSettings_SortedPlayerIdsOfType1,
          GameSettings_SortedPlayerIdsOfType1);

namespace {

// Settings with a player-setup vector of 0..6 setups of random types and
// ids (the plain fields only: the getters read nothing else).
struct IdsSettings {
    alignas(8) std::uint8_t bytes[sizeof(GameSettings)];
    std::vector<GamePlayerSetup> setups;
    explicit IdsSettings(const std::vector<GamePlayerSetup>& from) : setups(from) {
        std::memset(bytes, 0, sizeof(bytes));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes));
        PlayerSetupVector& v = get()->player_setups;
        v.vtable = 0;  // not called: every index is in range
        v.data = setups.empty() ? nullptr : setups.data();
        v.capacity = static_cast<std::uint32_t>(setups.size());
        v.filled = static_cast<std::int32_t>(setups.size());
        v.length = static_cast<std::uint32_t>(setups.size());
    }
    ~IdsSettings() { DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes)); }
    IdsSettings(const IdsSettings&) = delete;
    IdsSettings& operator=(const IdsSettings&) = delete;
    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes); }
};

struct IdsOutcome {
    std::uint32_t vtable, unk04, capacity, length, grow;
    std::int32_t filled;
    std::vector<std::uint32_t> ids;
    bool operator==(const IdsOutcome& o) const {
        return vtable == o.vtable && unk04 == o.unk04 && capacity == o.capacity && length == o.length &&
               grow == o.grow && filled == o.filled && ids == o.ids;
    }
};

IdsOutcome RunIds(SortedIdsFn fn, const std::vector<GamePlayerSetup>& setups) {
    IdsSettings settings(setups);
    watcom::VectorBase result;
    std::memset(&result, 0xcd, sizeof(result));
    fn(settings.get(), nullptr, &result);
    IdsOutcome out{result.vtable, result.unk04, result.capacity, result.length, result.grow, result.filled, {}};
    const auto* data = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(result.data));
    for (std::int32_t i = 0; data != nullptr && i < result.filled; ++i) {
        out.ids.push_back(data[i]);
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(result.data)));
    return out;
}

void CompareIds(test::Context& ctx, SortedIdsFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::vector<GamePlayerSetup> setups(ctx.rng().Below(7));
        for (GamePlayerSetup& setup : setups) {
            std::memset(&setup, 0, sizeof(setup));
            setup.type = ctx.rng().Chance(1, 2) ? kTypeLocal : ctx.rng().Below(4);
            setup.value10 = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(8);
        }
        const IdsOutcome a = RunIds(Original_GameSettings_SortedPlayerIdsOfType1, setups);
        const IdsOutcome b = RunIds(candidate, setups);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u setups, %u / %u ids", i,
                     static_cast<unsigned>(setups.size()), static_cast<unsigned>(a.ids.size()),
                     static_cast<unsigned>(b.ids.size()));
            return;
        }
    }
}

// Wrong: keeps the ids in setup order.
watcom::VectorBase* __fastcall WrongSortedIds(GameSettings* self, void* edx, watcom::VectorBase* result) {
    GameSettings_SortedPlayerIdsOfType1(self, edx, result);
    auto* data = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(result->data));
    if (result->length >= 2 && data[0] != data[1]) {
        std::swap(data[0], data[1]);
    }
    return result;
}

}  // namespace

S3RE_TEST(GameSettingsIds_MatchesOriginal) {
    CompareIds(ctx, &GameSettings_SortedPlayerIdsOfType1, 2000);
}

S3RE_TEST(GameSettingsIds_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIds(probe, &WrongSortedIds, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
