// CSoundData's effect cache (Dirk\SoundData.cpp): GetEffect, which
// creates a DirectX::CSoundEffect from a mapped sample on first use, and
// PurgeEffects, which deletes effects that have stopped playing. Format:
// re/sound_formats.md; layout: sound_layout.h.
//
// CSoundData::GetEffect (0x005ff4f0): (id, variant) ret 8. id = file << 16
//   | index. Disabled: EAX = 0. The sound is tables[file]->sounds[index] +
//   views[file]; variant -1 picks rand() % its variant count (the CRT's
//   rand 0x61f802, unsigned division). The cache key is id << 24 | variant
//   (only the index's low byte survives: a quirk, kept). If `effects` has no
//   entry for the key: new CSoundEffect (0xc0, constructor 0x5c2aa0; null:
//   ThrowOutOfMemory("lpEffectBuffer", line 457)), new CMemorySoundFile
//   (0x38, constructor 0x5c24e0 with (format = sample + 4, data = sample +
//   0x16, bytes = size - 0x16, CSoundData +8); null passes null),
//   CSoundEffect::Load(file, 1) (0x5c2ba0), effects[key] = effect. Then
//   effect = effects[key], lastUse[key] = timeGetTime(), PurgeEffects(-1,
//   -1, key); EAX = effect. The dictionaries' operator[] is inlined in the
//   original (find 0x60bab0, else insert 0x60bbe0 with the key/value pair at
//   +0x78; a failed insert writes through a null pointer, kept). The whole
//   body is inside `try { } catch (...) { DebugLog("Exception during
//   CSoundData::GetEffect (%x)", n); throw; }`, where n is the id until the
//   key is computed and the key afterwards (the original reuses the
//   argument's stack slot). VC6's catch (...) also sees structured
//   exceptions (e.g. a division by zero for a sound without variants).
// CSoundData::PurgeEffects (0x005ff700, was
//   WCVectorBase_unsigned_int::WCVectorBase_unsigned_int): (id, variant,
//   keep) ret 0Ch. now = timeGetTime(); id -1 only once more than 60 s
//   (signed) after +0x144, which becomes now. The keys to delete are
//   collected in a WCValOrderedVector<unsigned int> (10, 5):
//   - id -1: for every effect (a WCValSkipListDictIter over `effects`) whose
//     CSoundEffect::PurgeStoppedDuplicates (0x5c3210) returns true, its key
//     unless now - lastUse[key] is exactly 300,000 ms (a `!=` where `>` was
//     probably meant: every stopped effect is purged; kept);
//   - else: for each variant v of the sound (all if variant is -1, the
//     count re-read every round), key = id << 24 | v, if
//     PurgeStoppedDuplicates(effects[key]) is true (id >> 16 is a signed
//     shift here).
//   Then for every collected key other than `keep`: ~CSoundEffect and
//   operator delete of effects[key] (if not null), remove the key from both
//   dictionaries (0x60be20).
//
// Known difference: GetEffect's catch (...) is an SEH filter that logs and
// lets the exception pass (the original logs in the catch block, after the
// inner frames were unwound, then rethrows): the log entry comes before the
// inner frames' destructors run instead of after (none of them logs).

#include "game/sound/sound_layout.h"

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using fileformats::SkipListDict;
using sound::CSoundData;
using sound::SndSample;
using sound::SndSound;
using sound::SndTable;

constexpr std::uintptr_t kCrtRand = 0x0061f802;
constexpr std::uintptr_t kDictFind = 0x0060bab0;
constexpr std::uintptr_t kDictInsert = 0x0060bbe0;
constexpr std::uintptr_t kDictRemove = 0x0060be20;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;
constexpr std::uintptr_t kSoundEffectConstruct = 0x005c2aa0;
constexpr std::uintptr_t kSoundEffectDestroy = 0x005c2b00;
constexpr std::uintptr_t kSoundEffectLoad = 0x005c2ba0;
constexpr std::uintptr_t kPurgeStoppedDuplicates = 0x005c3210;
constexpr std::uintptr_t kMemorySoundFileConstruct = 0x005c24e0;
constexpr std::uintptr_t kPurgeEffects = 0x005ff700;
constexpr std::uintptr_t kIterConstruct = 0x005ffc60;   // WCSkipListIterBase<...>(dict)
constexpr std::uintptr_t kIterIncrement = 0x00499df0;   // WCSkipListIterBase::operator++
constexpr std::uintptr_t kIterKeyValue = 0x005ffc80;    // the current node's key/value pair
constexpr std::uintptr_t kKeyVectorConstruct = 0x00466110;  // WCBareVectorBase<uint>(capacity, grow)
constexpr std::uintptr_t kKeyVectorInsertAt = 0x00428d10;
constexpr std::uintptr_t kKeyVectorIndex = 0x00428ee0;
constexpr std::uintptr_t kKeyVectorDestroy = 0x00458590;
constexpr std::uintptr_t kNoOp = 0x0059f200;

constexpr std::uint32_t kEffectSize = 0xc0;      // sizeof(DirectX::CSoundEffect)
constexpr std::uint32_t kSoundFileSize = 0x38;   // sizeof(DirectX::CMemorySoundFile)
constexpr std::uint32_t kLineNewEffect = 457;
constexpr std::uint32_t kAnyVariant = 0xffffffff;
constexpr std::uint32_t kPurgeInterval = 60000;       // ms between full purges
constexpr std::uint32_t kKeepPlayedAgo = 300000;      // the "exactly 300 s ago" exception (quirk)
constexpr std::uint32_t kKeyVectorCapacity = 10;
constexpr std::uint32_t kKeyVectorGrowth = 5;

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using NodeFn = std::uint32_t(__fastcall*)(void* dict, void* edx, const std::uint32_t* key);
using ThrowAtFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
using FileConstructFn = void*(__fastcall*)(void* self, void* edx, const void* format, const void* data,
                                           std::uint32_t bytes, std::uint32_t kind);
using LoadFn = void(__fastcall*)(void* self, void* edx, void* file, std::uint32_t own);
using PurgeFn = void(__fastcall*)(void* self, void* edx, std::uint32_t id, std::uint32_t variant, std::uint32_t keep);
using IterConstructFn = void*(__fastcall*)(void* self, void* edx, void* dict);
using VectorConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t capacity, std::uint32_t grow);
using InsertAtFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t index, const std::uint32_t* value);
using IndexFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t index);
using BoolFn = std::uint8_t(__fastcall*)(void* self, void* edx);

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

CSoundData* AsSound(void* self) {
    return static_cast<CSoundData*>(self);
}

// The inlined operator[] of a WCValSkipListDict: the value slot of `key`,
// inserted if missing; 0 (the original's null slot) if the insert failed.
std::uint32_t* ValueSlot(SkipListDict* dict, std::uint32_t key) {
    dict->key_slot = key;
    std::uint32_t node = CallAt<NodeFn>(kDictFind)(dict, nullptr, &dict->key_slot);
    if (node == 0) {
        node = CallAt<NodeFn>(kDictInsert)(dict, nullptr, &dict->key_slot);
        if (node == 0) {
            return nullptr;
        }
    }
    return reinterpret_cast<std::uint32_t*>(node - 4);
}

const SndSound* SoundOf(const CSoundData* sound, std::uint32_t file, std::uint32_t index) {
    const auto* table = reinterpret_cast<const SndTable*>(sound->tables[file]);
    return reinterpret_cast<const SndSound*>(table->sounds[index] + sound->views[file]);
}

bool PurgeStoppedDuplicates(void* effect) {
    return CallAt<BoolFn>(kPurgeStoppedDuplicates)(effect, nullptr) != 0;
}

// The local WCValSkipListDictIter and WCValOrderedVector<unsigned int>.
struct DictIter {
    std::uint32_t vtable;
    std::uint32_t unk04;
    void* dict;
    std::uint32_t node;
    std::uint32_t atEnd;
};
static_assert(sizeof(DictIter) == 0x14);

struct KeyVector {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;  // +0x14
    std::uint32_t grow;
};
static_assert(sizeof(KeyVector) == 0x1c);

// The current node's key/value pair (null and the error hook past the end).
const std::uint32_t* CurrentPair(DictIter* iter) {
    if (iter->node == 0) {
        CallAt<MemberFn>(kNoOp)(iter, nullptr);
        return nullptr;
    }
    return reinterpret_cast<const std::uint32_t*>(CallAt<MemberFn>(kIterKeyValue)(iter, nullptr));
}

// The collection pass over every effect (id -1).
void CollectStoppedEffects(CSoundData* sound, KeyVector* keys, std::uint32_t now) {
    DictIter iter;
    CallAt<IterConstructFn>(kIterConstruct)(&iter, nullptr, &sound->effects);
    iter.vtable = vtables::kSoundEffectDictIter;
    while (CallAt<MemberFn>(kIterIncrement)(&iter, nullptr) != 0) {
        const std::uint32_t* pair = CurrentPair(&iter);
        void* const effect = pair != nullptr ? reinterpret_cast<void*>(pair[1]) : nullptr;
        if (!PurgeStoppedDuplicates(effect)) {
            continue;
        }
        pair = CurrentPair(&iter);
        const std::uint32_t key = pair != nullptr ? pair[0] : 0;
        const std::uint32_t* const lastUse = ValueSlot(&sound->lastUse, key);
        if (now - *lastUse != kKeepPlayedAgo) {
            std::uint32_t value = key;  // the original passes its own stack slot
            CallAt<InsertAtFn>(kKeyVectorInsertAt)(keys, nullptr, keys->length, &value);
        }
    }
}

// The collection pass over one sound's variants.
void CollectStoppedVariants(CSoundData* sound, KeyVector* keys, std::uint32_t id, std::uint32_t variant) {
    const std::int32_t file = static_cast<std::int32_t>(id) >> 16;  // sar in this function
    const SndSound* const entry = SoundOf(sound, static_cast<std::uint32_t>(file), id & 0xffff);
    for (std::uint32_t v = 0; v < entry->count; ++v) {
        if (variant != kAnyVariant && variant != v) {
            continue;
        }
        const std::uint32_t key = (id << 24) | v;
        void* const effect = reinterpret_cast<void*>(*ValueSlot(&sound->effects, key));
        if (PurgeStoppedDuplicates(effect)) {
            std::uint32_t value = key;
            CallAt<InsertAtFn>(kKeyVectorInsertAt)(keys, nullptr, keys->length, &value);
        }
    }
}

std::uint32_t KeyAt(KeyVector* keys, std::uint32_t i) {
    return keys->data[CallAt<IndexFn>(kKeyVectorIndex)(keys, nullptr, i)];
}

}  // namespace

void* __fastcall CSoundData_GetEffect(void* self, void* edx, std::uint32_t id, std::uint32_t variant);
void __fastcall CSoundData_PurgeEffects(void* self, void* edx, std::uint32_t id, std::uint32_t variant,
                                        std::uint32_t keep);

using SndGetEffectFn = decltype(&CSoundData_GetEffect);
using SndPurgeEffectsFn = decltype(&CSoundData_PurgeEffects);

S3RE_HOOK(0x005ff700, CSoundData_PurgeEffects, SndPurgeEffectsFn, Original_CSoundData_PurgeEffects,
          CSoundData_PurgeEffects);

// Purpose: deletes sound effects that have stopped playing (all of them
// about once a minute, or the variants of one sound), to free the memory of
// their DirectSound buffers.
void __fastcall CSoundData_PurgeEffects(void* self, void* /*edx*/, std::uint32_t id, std::uint32_t variant,
                                        std::uint32_t keep) {
    CSoundData* const sound = AsSound(self);
    const std::uint32_t now = g_importTimeGetTime.get()();
    if (id == kAnyVariant && static_cast<std::int32_t>(now - sound->createdTime - kPurgeInterval) <= 0) {
        return;
    }
    KeyVector keys;
    CallAt<VectorConstructFn>(kKeyVectorConstruct)(&keys, nullptr, kKeyVectorCapacity, kKeyVectorGrowth);
    keys.vtable = vtables::kUIntVector;
    sound->createdTime = now;
    if (id == kAnyVariant) {
        CollectStoppedEffects(sound, &keys, now);
    } else {
        CollectStoppedVariants(sound, &keys, id, variant);
    }
    for (std::uint32_t i = 0; i < keys.length; ++i) {
        if (KeyAt(&keys, i) == keep) {
            continue;
        }
        void* const effect = reinterpret_cast<void*>(*ValueSlot(&sound->effects, KeyAt(&keys, i)));
        if (effect != nullptr) {
            CallAt<MemberFn>(kSoundEffectDestroy)(effect, nullptr);
            msvc6::GameOperatorDelete(effect);
        }
        sound->effects.key_slot = KeyAt(&keys, i);
        CallAt<NodeFn>(kDictRemove)(&sound->effects, nullptr, &sound->effects.key_slot);
        sound->lastUse.key_slot = KeyAt(&keys, i);
        CallAt<NodeFn>(kDictRemove)(&sound->lastUse, nullptr, &sound->lastUse.key_slot);
    }
    keys.vtable = vtables::kUIntVectorBase;
    CallAt<MemberFn>(kKeyVectorDestroy)(&keys, nullptr);
}

namespace {

// The body of GetEffect (inside the original's try block). `logged` is the
// value the catch block would log.
void* GetEffectBody(CSoundData* sound, std::uint32_t id, std::uint32_t variant, std::uint32_t* logged) {
    if (sound->enabled == 0) {
        return nullptr;
    }
    const std::uint32_t file = id >> 16;
    const SndSound* const entry = SoundOf(sound, file, id & 0xffff);
    if (variant == kAnyVariant) {
        const auto random = static_cast<std::uint32_t>(CallAt<int(__cdecl*)()>(kCrtRand)());
        variant = random % entry->count;
    }
    const std::uint32_t key = (id << 24) | variant;
    *logged = key;
    sound->effects.key_slot = key;
    if (CallAt<NodeFn>(kDictFind)(&sound->effects, nullptr, &sound->effects.key_slot) == 0) {
        const auto* const sample =
            reinterpret_cast<const SndSample*>(entry->variants[variant] + sound->views[file]);
        void* const block = msvc6::GameOperatorNew(kEffectSize);
        void* effect = nullptr;
        if (block != nullptr) {
            effect = CallAt<void*(__fastcall*)(void*, void*)>(kSoundEffectConstruct)(block, nullptr);
        }
        if (effect == nullptr) {
            CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpEffectBuffer, texts::kSoundDataFile, kLineNewEffect);
        }
        void* const fileBlock = msvc6::GameOperatorNew(kSoundFileSize);
        void* soundFile = nullptr;
        if (fileBlock != nullptr) {
            soundFile = CallAt<FileConstructFn>(kMemorySoundFileConstruct)(fileBlock, nullptr, sample->format,
                                                                           sample->data, sample->size - 0x16,
                                                                           sound->format);
        }
        CallAt<LoadFn>(kSoundEffectLoad)(effect, nullptr, soundFile, 1);
        *ValueSlot(&sound->effects, key) = reinterpret_cast<std::uint32_t>(effect);
    }
    void* const effect = reinterpret_cast<void*>(*ValueSlot(&sound->effects, key));
    std::uint32_t* const lastUse = ValueSlot(&sound->lastUse, key);
    *lastUse = g_importTimeGetTime.get()();
    CallAt<PurgeFn>(kPurgeEffects)(sound, nullptr, kAnyVariant, kAnyVariant, key);
    return effect;
}

// catch (...) { DebugLog(...); throw; }: logs and lets every exception pass.
int LogGetEffectException(std::uint32_t logged) {
    GameDebugLog(texts::kGetEffectException, logged);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

S3RE_HOOK(0x005ff4f0, CSoundData_GetEffect, SndGetEffectFn, Original_CSoundData_GetEffect, CSoundData_GetEffect);

// Purpose: the sound effect object of a sound (and variant) for playing,
// created from the SND file on first use and cached.
void* __fastcall CSoundData_GetEffect(void* self, void* /*edx*/, std::uint32_t id, std::uint32_t variant) {
    std::uint32_t logged = id;
    __try {
        return GetEffectBody(AsSound(self), id, variant, &logged);
    } __except (LogGetEffectException(logged)) {
    }
    return nullptr;  // never reached (the filter doesn't take exceptions)
}

}  // namespace s3re::game
