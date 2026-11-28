// More members of the game's configuration object (g_config 0x7a7af0,
// struct GameConfig in game/globals.h; Dirk\ConsoleInit.cpp and
// Dirk\DGlobals.cpp). All thiscall.
//
// GameConfig::GameConfig (0x0040d170): () -> this. Every field's default:
//   the strings empty (_Tidy(false); their allocator bytes are
//   uninitialized bytes of the original's frame), the String vector +0x3c
//   (WCVectorBase<String>(10, 5) 0x414510, vtable WCValOrderedVector
//   <String>), the flags and values below, the font name +0x134 =
//   "MS Sans Serif" (through string(text) 0x407a50 and string(that, 0, -1)
//   0x4078a0 temporaries, then assign 0x401d60), the int vector +0x16c
//   (WCSortedVectorBase<int>(10, 5) 0x414410, vtable WCValSortedVector
//   <int>). Called by the static initializer 0x40cea0. Renamed by
//   libs-r7.
// GameConfig::~GameConfig (0x0040d470): the int vector (vtable
//   WCSortedVectorBase<int>, ~WCBareVectorBase<int> 0x4145f0), the strings
//   +0x15c .. +0x9c (erase(0, npos) 0x407b10, then _Tidy(true)), the String
//   vector (vtable WCVectorBase<String>, 0x414770), the strings +0x14 and +4
//   (the last one's _Tidy(true) inlined). Called by the static destructor
//   0x40cec0.
// GameConfig::FontName (0x00413070): (std::string* out) ret 4 -> out: a copy
//   of the font name +0x134 (the copy constructor inlined like StartMap's:
//   the allocator byte is the low byte of `out`, then assign(x, 0, npos)).
//   Caller: Dialog::Init. Was App_sub_413070.
// GameConfig::HasCampaignId (0x00413ee0): (int id) ret 4 -> AL: whether the
//   sorted int vector +0x16c holds id (its virtual find, slot 3, with
//   occurrence 0; not called when the vector is empty). Caller:
//   CLoadGameDialog::FillCampaignList. Was App_sub_413ee0.
// GameConfig::SetCampaignId (0x00413f20): (int id, bool add) ret 8: add:
//   unless already there, inserted at the position find(id, 3) gives
//   (watcom::PtrVector::insertAt 0x428d10) and +0x188 set; remove: when
//   there, the entry find(id, 1) gives is removed (the elements after it
//   moved down, length and filled - 1) and +0x188 set (also when that
//   second find fails). The index variable is the `add` argument's stack
//   slot in the original. Callers: CCampaignDialog's constructor and
//   App_sub_410370 (the registry). Was App_sub_413f20.

#include "game/framework/exception_strings.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "watcom/vector_base.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace s3re::game {

namespace {

using VectorConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t capacity, std::uint32_t grow);
using VectorDestructFn = void(__fastcall*)(void* self, void* edx);
constexpr std::uintptr_t kStringVectorConstruct = 0x00414510;  // WCVectorBase<String>(capacity, grow)
constexpr std::uintptr_t kIntVectorConstruct = 0x00414410;     // WCSortedVectorBase<int>(capacity, grow)
constexpr std::uintptr_t kIntVectorDestruct = 0x004145f0;      // ~WCBareVectorBase<int>
constexpr std::uintptr_t kStringVectorDestruct = 0x00414770;   // ~WCBareVectorBase<String>
using InsertAtFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t index, const std::int32_t* value);
constexpr std::uintptr_t kInsertAt = 0x00428d10;  // watcom::PtrVector::insertAt (verified)

constexpr std::uint32_t kVectorCapacity = 10;
constexpr std::uint32_t kVectorGrow = 5;

// The allocator byte the original takes from an uninitialized byte of its
// frame (the spec ignores these bytes).
constexpr std::uint8_t kFrameAllocator = 0;

// The sorted vector's virtual find (WCSortedVectorBase::base_find_with_index,
// slot 3): (const T* value, occurrence, int* index) -> found.
constexpr std::uint32_t kFindSlot = 3;
constexpr std::int32_t kFindAny = 0;
constexpr std::int32_t kFindFirst = 1;
constexpr std::int32_t kFindInsertPosition = 3;

std::uint32_t Find(watcom::VectorBase* vector, const std::int32_t* value, std::int32_t occurrence,
                   std::int32_t* index) {
    using FindFn = std::uint32_t(__fastcall*)(void* self, void* edx, const std::int32_t* value,
                                              std::int32_t occurrence, std::int32_t* index);
    const auto* vtable = reinterpret_cast<const std::uintptr_t*>(static_cast<std::uintptr_t>(vector->vtable));
    return reinterpret_cast<FindFn>(vtable[kFindSlot])(vector, nullptr, value, occurrence, index);
}

watcom::VectorBase* CampaignIds(GameConfig* self) {
    return reinterpret_cast<watcom::VectorBase*>(self->campaign_ids);
}

// The flags +0x58..+0x60 (+0x58 and +0x5f off, the others on).
constexpr std::uint8_t kDefaultFlags58[8] = {0, 1, 1, 1, 1, 1, 1, 0};

}  // namespace

// Purpose: Builds the global configuration object with its defaults at
// program start (the registry values are read later).
GameConfig* __fastcall GameConfig_Construct(GameConfig* self, void*) {
    self->unk000[0] = 0;
    framework::MakeEmpty(&self->string04, kFrameAllocator);
    framework::MakeEmpty(&self->string14, kFrameAllocator);
    self->byte24 = 0;
    self->dword28 = 0;
    self->dword2c = 3;
    self->byte30 = 0;
    self->unk031[0] = 0;
    self->directplay = 0;
    self->byte38 = 0;
    reinterpret_cast<VectorConstructFn>(kStringVectorConstruct)(self->object3c, nullptr, kVectorCapacity, kVectorGrow);
    reinterpret_cast<watcom::VectorBase*>(self->object3c)->vtable = vtables::kWCValOrderedVector_String;
    std::memcpy(self->bytes58, kDefaultFlags58, sizeof(kDefaultFlags58));
    self->unk060[0] = 1;
    self->notification_level = 9;
    self->lobby_value68 = 0x1ff;
    self->byte6c = 1;
    self->screen_layout = 0;
    // +0x74..+0x94 (+0x8c / +0x90: 800 x 600).
    static constexpr std::uint32_t kDwords74[9] = {3, 0, 5, 1, 1, 0, 800, 600, 16};
    std::memcpy(self->dwords74, kDwords74, sizeof(kDwords74));
    self->byte98 = 0;
    framework::MakeEmpty(&self->string9c, kFrameAllocator);
    framework::MakeEmpty(&self->stringAC, kFrameAllocator);
    self->byteBC = 0;
    for (msvc6::String& path : self->paths) {
        framework::MakeEmpty(&path, kFrameAllocator);
    }
    self->dwords120[4] = 16;
    self->dwords120[0] = 0;
    self->dwords120[1] = 5;
    self->dwords120[2] = 0xfffffffeu;
    self->dwords120[3] = 0x1c;
    framework::MakeEmpty(&self->font_name, kFrameAllocator);
    framework::AssignThroughConstructors(&self->font_name, texts::kMsSansSerif.c_str(), &kFrameAllocator);
    self->dword144 = 15;
    self->lobby_pair148[0] = 0;
    self->lobby_pair148[1] = 0;
    self->byte150 = 0;
    self->dword154 = 0xffffffffu;
    self->random_race_amazons = 0;
    self->gfx_group61 = 0;
    self->byte15a = 0;
    framework::MakeEmpty(&self->start_map, kFrameAllocator);
    reinterpret_cast<VectorConstructFn>(kIntVectorConstruct)(self->campaign_ids, nullptr, kVectorCapacity,
                                                             kVectorGrow);
    CampaignIds(self)->vtable = vtables::kWCValSortedVector_int;
    self->campaign_ids_changed = 0;
    self->unk189 = 0;
    return self;
}

// Purpose: Destroys the global configuration object at program exit.
void __fastcall GameConfig_Destruct(GameConfig* self, void*) {
    CampaignIds(self)->vtable = vtables::kWCSortedVectorBase_int;
    reinterpret_cast<VectorDestructFn>(kIntVectorDestruct)(self->campaign_ids, nullptr);
    framework::CallEraseAndFree(&self->start_map);
    framework::CallEraseAndFree(&self->font_name);
    for (int i = 5; i >= 0; --i) {
        framework::CallEraseAndFree(&self->paths[i]);
    }
    framework::CallEraseAndFree(&self->stringAC);
    framework::CallEraseAndFree(&self->string9c);
    reinterpret_cast<watcom::VectorBase*>(self->object3c)->vtable = vtables::kWCVectorBase_String;
    reinterpret_cast<VectorDestructFn>(kStringVectorDestruct)(self->object3c, nullptr);
    framework::CallEraseAndFree(&self->string14);
    framework::CallEraseAndFree(&self->string04);
}

// Purpose: Gives the dialogs the name of the font to draw texts with.
msvc6::String* __fastcall GameConfig_FontName(const GameConfig* self, void*, msvc6::String* out) {
    out->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(out));
    msvc6::detail::Clear(out);
    msvc6::String_AssignSubstr(out, nullptr, &self->font_name, 0, msvc6::detail::Npos());
    return out;
}

// Purpose: Whether an id is in the configuration's sorted id list
// (probably a campaign mission that was won; the load dialog asks).
std::uint32_t __fastcall GameConfig_HasCampaignId(GameConfig* self, void*, std::int32_t id) {
    watcom::VectorBase* ids = CampaignIds(self);
    std::int32_t index = 0;
    if (ids->length != 0 && Find(ids, &id, kFindAny, &index) != 0) {
        return 1;
    }
    return 0;
}

// Purpose: Adds an id to the configuration's sorted id list or removes it
// (probably marks a campaign mission as won), noting that it changed.
void __fastcall GameConfig_SetCampaignId(GameConfig* self, void*, std::int32_t id, std::uint32_t add) {
    watcom::VectorBase* ids = CampaignIds(self);
    std::int32_t index = 0;
    if ((add & 0xff) != 0) {
        if (ids->length != 0 && Find(ids, &id, kFindAny, &index) != 0) {
            return;
        }
        index = 0;
        if (ids->length != 0) {
            Find(ids, &id, kFindInsertPosition, &index);
        }
        reinterpret_cast<InsertAtFn>(kInsertAt)(ids, nullptr, index, &id);
        self->campaign_ids_changed = 1;
        return;
    }
    if (ids->length == 0 || Find(ids, &id, kFindAny, &index) == 0) {
        return;
    }
    index = 0;
    if (ids->length != 0 && Find(ids, &id, kFindFirst, &index) != 0) {
        // 0x413fd9: the elements after it move down one place.
        auto* data = reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(ids->data));
        for (std::int32_t i = index; i < static_cast<std::int32_t>(ids->length) - 1; ++i) {
            data[i] = data[i + 1];
        }
        ids->length -= 1;
        ids->filled -= 1;
    }
    self->campaign_ids_changed = 1;
}

using ConstructFn = GameConfig*(__fastcall*)(GameConfig* self, void* edx);
using DestructFn = void(__fastcall*)(GameConfig* self, void* edx);
using FontNameFn = msvc6::String*(__fastcall*)(const GameConfig* self, void* edx, msvc6::String* out);
using HasIdFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx, std::int32_t id);
using SetIdFn = void(__fastcall*)(GameConfig* self, void* edx, std::int32_t id, std::uint32_t add);
S3RE_HOOK(0x0040d170, GameConfig_Construct, ConstructFn, Original_GameConfig_Construct, GameConfig_Construct);
S3RE_HOOK(0x0040d470, GameConfig_Destruct, DestructFn, Original_GameConfig_Destruct, GameConfig_Destruct);
S3RE_HOOK(0x00413070, GameConfig_FontName, FontNameFn, Original_GameConfig_FontName, GameConfig_FontName);
S3RE_HOOK(0x00413ee0, GameConfig_HasCampaignId, HasIdFn, Original_GameConfig_HasCampaignId,
          GameConfig_HasCampaignId);
S3RE_HOOK(0x00413f20, GameConfig_SetCampaignId, SetIdFn, Original_GameConfig_SetCampaignId,
          GameConfig_SetCampaignId);

namespace {

constexpr std::size_t kConfigBytes = sizeof(GameConfig);
constexpr std::uint32_t kStringOffsets[] = {0x04, 0x14, 0x9c, 0xac, 0xc0, 0xd0, 0xe0, 0xf0, 0x100, 0x110, 0x134, 0x15c};
constexpr std::uint32_t kVectorOffsets[] = {0x3c, 0x16c};

std::string Text(const msvc6::String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

// Two configurations: strings by text and capacity (not their allocator
// bytes or buffers), vectors without their block address, the rest byte
// for byte.
bool SameConfig(test::Context& ctx, const std::uint8_t* a, const std::uint8_t* b) {
    std::uint8_t maskedA[kConfigBytes];
    std::uint8_t maskedB[kConfigBytes];
    std::memcpy(maskedA, a, kConfigBytes);
    std::memcpy(maskedB, b, kConfigBytes);
    for (const std::uint32_t offset : kStringOffsets) {
        const auto& sa = *reinterpret_cast<const msvc6::String*>(a + offset);
        const auto& sb = *reinterpret_cast<const msvc6::String*>(b + offset);
        if (!S3RE_EXPECT(Text(sa) == Text(sb) && sa.res == sb.res && (sa.ptr == nullptr) == (sb.ptr == nullptr))) {
            ctx.Fail(__FILE__, __LINE__, "  string +0x%x", offset);
            return false;
        }
        std::memset(maskedA + offset, 0, sizeof(msvc6::String));
        std::memset(maskedB + offset, 0, sizeof(msvc6::String));
    }
    for (const std::uint32_t offset : kVectorOffsets) {
        auto* va = reinterpret_cast<watcom::VectorBase*>(maskedA + offset);
        auto* vb = reinterpret_cast<watcom::VectorBase*>(maskedB + offset);
        if (!S3RE_EXPECT((va->data == 0) == (vb->data == 0))) {
            return false;
        }
        va->data = 0;
        vb->data = 0;
    }
    return S3RE_EXPECT_MEM_EQ(maskedA, maskedB, kConfigBytes);
}

using ConstructFn = GameConfig*(__fastcall*)(GameConfig* self, void* edx);
using DestructFn = void(__fastcall*)(GameConfig* self, void* edx);
using FontNameFn = msvc6::String*(__fastcall*)(const GameConfig* self, void* edx, msvc6::String* out);
using HasIdFn = std::uint32_t(__fastcall*)(GameConfig* self, void* edx, std::int32_t id);
using SetIdFn = void(__fastcall*)(GameConfig* self, void* edx, std::int32_t id, std::uint32_t add);

struct ConfigMembers {
    ConstructFn construct;
    DestructFn destruct;
    FontNameFn fontName;
    HasIdFn hasId;
    SetIdFn setId;
};

// Constructs a configuration on random memory with the original and the
// candidate, compares them, copies the font name out, runs a random
// sequence of campaign id changes and queries, compares again and destroys
// both.
void CompareMembers(test::Context& ctx, const ConfigMembers& candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        alignas(8) std::uint8_t bytesA[kConfigBytes];
        ctx.rng().Fill(bytesA, sizeof(bytesA));
        alignas(8) std::uint8_t bytesB[kConfigBytes];
        std::memcpy(bytesB, bytesA, sizeof(bytesA));
        auto* a = reinterpret_cast<GameConfig*>(bytesA);
        auto* b = reinterpret_cast<GameConfig*>(bytesB);
        if (!S3RE_EXPECT(Original_GameConfig_Construct(a, nullptr) == a && candidate.construct(b, nullptr) == b) ||
            !SameConfig(ctx, bytesA, bytesB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: after the constructor", i);
            return;
        }
        msvc6::String outA;
        msvc6::String outB;
        Original_GameConfig_FontName(a, nullptr, &outA);
        candidate.fontName(b, nullptr, &outB);
        const bool sameFont = S3RE_EXPECT(Text(outA) == Text(outB) && outA.res == outB.res &&
                                          outA.allocator == static_cast<std::uint8_t>(
                                              reinterpret_cast<std::uintptr_t>(&outA)) &&
                                          outB.allocator == static_cast<std::uint8_t>(
                                              reinterpret_cast<std::uintptr_t>(&outB)));
        msvc6::String_Destroy(&outA, nullptr);
        msvc6::String_Destroy(&outB, nullptr);
        if (!sameFont) {
            return;
        }
        const std::uint32_t steps = ctx.rng().Below(40);
        for (std::uint32_t k = 0; k < steps; ++k) {
            const auto id = static_cast<std::int32_t>(ctx.rng().Below(16)) - 3;
            if (ctx.rng().Chance(1, 3)) {
                if (!S3RE_EXPECT_EQ(Original_GameConfig_HasCampaignId(a, nullptr, id) & 0xff,
                                    candidate.hasId(b, nullptr, id) & 0xff)) {
                    return;
                }
            } else {
                const std::uint32_t add = ctx.rng().Chance(2, 3) ? 1 : 0x100;
                Original_GameConfig_SetCampaignId(a, nullptr, id, add);
                candidate.setId(b, nullptr, id, add);
            }
        }
        const auto* ia = reinterpret_cast<const watcom::VectorBase*>(a->campaign_ids);
        const auto* ib = reinterpret_cast<const watcom::VectorBase*>(b->campaign_ids);
        const bool sameIds =
            SameConfig(ctx, bytesA, bytesB) &&
            S3RE_EXPECT(ia->length == 0 ||
                        std::memcmp(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(ia->data)),
                                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(ib->data)),
                                    ia->length * 4) == 0);
        Original_GameConfig_Destruct(a, nullptr);
        candidate.destruct(b, nullptr);
        if (!sameIds) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: after %u id steps", i, steps);
            return;
        }
        if (!S3RE_EXPECT(a->string04.ptr == nullptr && b->string04.ptr == nullptr && a->font_name.ptr == nullptr &&
                         b->font_name.ptr == nullptr &&
                         reinterpret_cast<const watcom::VectorBase*>(a->object3c)->vtable ==
                             reinterpret_cast<const watcom::VectorBase*>(b->object3c)->vtable &&
                         ia->vtable == ib->vtable)) {
            return;
        }
    }
}

// Wrong: appends a new id instead of inserting it in order.
void __fastcall WrongSetCampaignId(GameConfig* self, void* edx, std::int32_t id, std::uint32_t add) {
    watcom::VectorBase* ids = CampaignIds(self);
    if ((add & 0xff) != 0 && GameConfig_HasCampaignId(self, edx, id) == 0) {
        reinterpret_cast<InsertAtFn>(kInsertAt)(ids, nullptr, static_cast<std::int32_t>(ids->length), &id);
        self->campaign_ids_changed = 1;
        return;
    }
    GameConfig_SetCampaignId(self, edx, id, add);
}

}  // namespace

S3RE_TEST(GameConfigMembers_MatchOriginal) {
    CompareMembers(ctx,
                   {&GameConfig_Construct, &GameConfig_Destruct, &GameConfig_FontName, &GameConfig_HasCampaignId,
                    &GameConfig_SetCampaignId},
                   300);
}

S3RE_TEST(GameConfigMembers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMembers(probe,
                   {&GameConfig_Construct, &GameConfig_Destruct, &GameConfig_FontName, &GameConfig_HasCampaignId,
                    &WrongSetCampaignId},
                   100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
