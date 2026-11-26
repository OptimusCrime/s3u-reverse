// DirectX::CSoundEffect (DirectX\Dsound.cpp; RTTI name): one sample in a
// secondary DirectSound buffer (the "master"), plus duplicate buffers that
// share its memory so that the effect can play several times at once; the
// duplicates' WCValSkipListDict<int, CSoundEffectBuffer> template code; the
// restore-if-active entries of CSoundEffectBuffer and CDirectSound. Layout:
// directx_sound_layout.h; COM slots are byte offsets into
// IDirectSoundBuffer's vtable.
//
// CSoundEffect::CSoundEffect (0x005c2aa0): the dictionary (WCSkipNonTempBase
//   0x60b9f0 with (3, 8, 0), vtable 0x65cf70, its key/value pair's buffer
//   CSoundEffectBuffer(0), vtable 0x65cf54), the master CSoundEffectBuffer(
//   this), +0xb8 = +0xbc = 0, vtable 0x65cf4c (+0xa0 and the format are not
//   set). EAX = this.
// ~CSoundEffect (0x005c2b00): vtable, Unload, ~CDirectSoundBuffer(master),
//   the dictionary's destructor (inlined 0x5c33e0). Callers ignore EAX.
// Unload (0x005c2ce0, vtable slot 0): the dictionary cleared (0x60bd80,
//   which destroys the duplicate buffers), master.Release (virtual slot 1),
//   the file deleted (its virtual slot 0 with 1), +0xa0 = +0xb8 = 0. EAX:
//   the last call's result (unused).
// Load (0x005c2ba0): (CSoundFile* file, unused) ret 8: +0xa0 = file;
//   file->Open; GetFormat(&format, &bytes); a temporary block of `bytes`
//   (null: ThrowOutOfMemory("TempBuffer", Dsound.cpp, line 787)); Read(bytes,
//   block); Close; master.SetCreateFlags(DSBCAPS_STATIC), CSecondaryBuffer::
//   Create(format, bytes); Lock(0, bytes, ...) of the whole buffer, the block
//   copied into both parts, Unlock (both checked by CheckResult, "DSLock" /
//   "DSUnlock"); the block freed.
// Restore (0x005c2d20): reloads the sample (Open, a block, Read, Close as
//   in Load, line 827), master.DSRestore (slot 2), fills it as Load does;
//   then for every duplicate: DSRestore, Lock, copy, Unlock, each on its own
//   copy of the duplicate (iterator value 0x5c3640) that is destroyed
//   afterwards. EAX unused.
// Play (0x005c2fc0): (volume, pan, flags) ret 0Ch: if the master is not
//   playing (DSGetStatus): SetPan(pan), SetVolume(volume), Play(0, 0, flags)
//   on it, key 0. Otherwise the key of the last stopped duplicate (key -1
//   if none: a new key from g_soundDuplicateCounter 0x7a0c00, post-
//   incremented, and master.DuplicateInto(the new entry)); then SetPan,
//   SetVolume and Play on that duplicate (each looked up anew: the inlined
//   find/insert twice, operator[] once). Then PurgeStoppedDuplicates. EAX =
//   the key (0 for the master).
// PurgeStoppedDuplicates (0x005c3210): () -> bool (AL). At most once per
//   2,500 ms (timeGetTime against +0xbc, which becomes now): the keys of
//   the stopped duplicates are collected in a WCValOrderedVector<int>(10,
//   5) and removed from the dictionary (0x60be20, which destroys them).
//   True if no duplicate is left and the master is not playing.
// CSoundEffectBuffer::Restore (0x005c28e0, vtable slot 3, what CheckResult
//   calls on DSERR_BUFFERLOST) and CDirectSound slot 1 (0x005c1dc0, what
//   RestoreAll calls): if the application is active (0x5b8c00, 0x5b8c40):
//   effect->Restore / this->slot 3 (CSoundData::Restore); otherwise throw new
//   CAppNotActiveException(string(""), 1) (0x18 bytes; ThrowInfo 0x67aee8;
//   null if operator new fails).
// CSoundEffectBuffer::DuplicateInto (0x005c2a60): (CSoundEffectBuffer* dest)
//   ret 4: dest = *this (operator= 0x5c2120), dest effect = this effect,
//   IUnknown::Release of dest's buffer (unchecked: the reference operator=
//   took), DSDuplicateSoundBuffer(this buffer, &dest buffer), dest effect
//   again. EAX = the effect.
//
// The dictionary of duplicates (WCValSkipListDict<int, CSoundEffectBuffer>;
// nodes [key][CSoundEffectBuffer][forward pointers], pointed at their
// forward array):
//   base_new_node (0x005c3510, slot 3): (const KeyVal* pair, level) ret 8:
//     level * 4 + 0x18 bytes from the user allocator (+0x5c, size in ECX)
//     or operator new; key and a copy of the buffer (copy constructor, then
//     the CSoundEffectBuffer vtable and effect). EAX = the node's forward
//     array, or 0.
//   base_destroy_node (0x005c35c0, slot 4): (node, level) ret 8: unless
//     null: ~CDirectSoundBuffer of the value (thunk 0x5c3730), then the user
//     deallocator (+0x60; ECX = block, EDX = size) or operator delete.
//   operator[] (0x005c3600): (const int* key) ret 4: the key/value pair's
//     key = *key; find (0x60bab0), else insert (0x60bbe0); EAX = the node's
//     value, or 0 if the insert failed.
//   WCValSkipListDictIter::value (0x005c3640): (CSoundEffectBuffer* result)
//     ret 4: a copy of the current node's buffer; without a current node the
//     error hook (0x59f200) and a copy of a default CSoundEffectBuffer(0)
//     (destroyed again). EAX = result.
//
// Quirks kept: Load and Restore pass uninitialized locals to Lock (Load's
// first size starts as its `file` argument's slot); if Lock reports
// DSERR_BUFFERLOST (CheckResult restores and returns instead of throwing)
// without writing them, the copies use these values (ours start with the
// file pointer and zeros). Play's lookups write through a null value when
// an insert fails. Known difference: the throwers' message string leaks
// when the exception unwinds (the original's frame frees it), as in the
// other throwers (win32_errors.cpp).

#include "game/framework/directx_sound.h"
#include "game/framework/app_not_active.h"
#include "game/framework/directx_sound_layout.h"

#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using framework::CDirectSoundBuffer;
using framework::CSoundEffect;
using framework::CSoundEffectBuffer;
using framework::EffectBufferDict;
using framework::SkipListIter;
using msvc6::String;

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kSkipNonTempConstruct = 0x0060b9f0;  // WCSkipNonTempBase(probability, levels, duplicates)
constexpr std::uintptr_t kDictFind = 0x0060bab0;
constexpr std::uintptr_t kDictInsert = 0x0060bbe0;
constexpr std::uintptr_t kDictClear = 0x0060bd80;
constexpr std::uintptr_t kDictRemove = 0x0060be20;
constexpr std::uintptr_t kDictDestroy = 0x005c33e0;     // ~WCValSkipListDict<int, CSoundEffectBuffer>
constexpr std::uintptr_t kIterConstruct = 0x005c34f0;   // WCSkipListIterBase(dict)
constexpr std::uintptr_t kIterIncrement = 0x00499df0;   // WCSkipListIterBase::operator++
constexpr std::uintptr_t kIterValue = 0x005c3640;
constexpr std::uintptr_t kIterDestroy = 0x00499620;     // ~WCIterExcept
constexpr std::uintptr_t kDictIndex = 0x005c3600;       // operator[]
constexpr std::uintptr_t kNoOp = 0x0059f200;            // the containers' empty error hook
constexpr std::uintptr_t kEffectBufferConstruct = 0x005c28b0;
constexpr std::uintptr_t kBufferCopyConstruct = 0x005c1fb0;
constexpr std::uintptr_t kBufferDestroy = 0x005c1fe0;   // ~CDirectSoundBuffer
constexpr std::uintptr_t kBufferValueDestroy = 0x005c3730;  // the thunk to ~CDirectSoundBuffer(block + 4)
constexpr std::uintptr_t kBufferAssign = 0x005c2120;
constexpr std::uintptr_t kBufferCheckResult = 0x005c2000;
constexpr std::uintptr_t kBufferSetCreateFlags = 0x005c2050;
constexpr std::uintptr_t kBufferDSGetStatus = 0x005c20b0;
constexpr std::uintptr_t kSecondaryCreate = 0x005c2220;
constexpr std::uintptr_t kDuplicateSoundBuffer = 0x005c1f40;
constexpr std::uintptr_t kDuplicateInto = 0x005c2a60;
constexpr std::uintptr_t kEffectUnload = 0x005c2ce0;
constexpr std::uintptr_t kEffectRestore = 0x005c2d20;
constexpr std::uintptr_t kPurgeStoppedDuplicates = 0x005c3210;
constexpr std::uintptr_t kKeyVectorConstruct = 0x004376b0;  // WCValVector<int>(capacity, grow)
constexpr std::uintptr_t kKeyVectorInsertAt = 0x00428d10;
constexpr std::uintptr_t kKeyVectorIndex = 0x00428ee0;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;
constexpr std::uintptr_t kGetApplication = 0x005b8c00;
constexpr std::uintptr_t kApplicationIsActive = 0x005b8c40;
constexpr std::uintptr_t kAppNotActiveConstruct = 0x005b8890;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;

constexpr std::uint32_t kDictProbability = 3;
constexpr std::uint32_t kDictLevels = 8;
constexpr std::uint32_t kStaticBuffer = 8;        // DSBCAPS_STATIC
constexpr std::uint32_t kLineLoadBlock = 787;     // Dsound.cpp lines of the ThrowOutOfMemory calls
constexpr std::uint32_t kLineRestoreBlock = 827;
constexpr std::uint32_t kPurgeInterval = 2500;    // ms between duplicate purges
constexpr std::uint32_t kKeyVectorCapacity = 10;
constexpr std::uint32_t kKeyVectorGrowth = 5;
constexpr std::uint32_t kExceptionBytes = 0x18;   // sizeof(DirectX::CAppNotActiveException)
constexpr std::int32_t kNewKey = -1;
constexpr std::uint32_t kNodeKey = 0x14;          // a node's key sits this far before its forward array
constexpr std::uint32_t kNodeValue = 0x10;        // its CSoundEffectBuffer
constexpr std::uint32_t kNodeBaseBytes = 0x18;    // key, buffer and the level-0 pointer

// Virtual slots (byte offsets into the game objects' vtables).
constexpr std::uint32_t kSlotDelete = 0x00;       // scalar deleting destructor
constexpr std::uint32_t kSlotFileOpen = 0x04;
constexpr std::uint32_t kSlotFileClose = 0x08;
constexpr std::uint32_t kSlotFileGetFormat = 0x0c;
constexpr std::uint32_t kSlotFileRead = 0x10;
constexpr std::uint32_t kSlotBufferRelease = 0x04;
constexpr std::uint32_t kSlotBufferRestore = 0x08;  // DSRestore
constexpr std::uint32_t kSlotSoundRestore = 0x0c;   // CDirectSound slot 3

// IDirectSoundBuffer / IUnknown methods.
constexpr std::uint32_t kComRelease = 0x08;
constexpr std::uint32_t kComPlay = 0x30;
constexpr std::uint32_t kComSetVolume = 0x3c;
constexpr std::uint32_t kComSetPan = 0x40;
constexpr std::uint32_t kComLock = 0x2c;
constexpr std::uint32_t kComUnlock = 0x4c;

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

template <typename Fn>
Fn Method(const void* object, std::uint32_t offset) {
    return *reinterpret_cast<Fn const*>(*static_cast<const std::uint8_t* const*>(object) + offset);
}

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThisArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ThisArgsFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using ThisPtrFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* p);
using ThisPtrsFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* a, const void* b);
using NodeFn = std::uint32_t(__fastcall*)(void* dict, void* edx, const std::int32_t* key);
using CheckFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
using ThrowAtFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);

std::uint32_t Virtual(void* object, std::uint32_t slot) {
    return Method<ThisFn>(object, slot)(object, nullptr);
}

CSoundEffect* AsEffect(void* self) {
    return static_cast<CSoundEffect*>(self);
}

std::int32_t CheckResult(void* buffer, std::int32_t hr, const char* where) {
    return CallAt<CheckFn>(kBufferCheckResult)(buffer, nullptr, hr, where);
}

void* BufferInterface(const void* buffer) {
    return static_cast<const CDirectSoundBuffer*>(buffer)->buffer;
}

void DestroyBuffer(void* buffer) {
    CallAt<ThisFn>(kBufferDestroy)(buffer, nullptr);
}

bool IsPlaying(void* buffer) {
    return CallAt<ThisFn>(kBufferDSGetStatus)(buffer, nullptr) != 0;
}

// The locked parts of a buffer (Lock's out values).
struct LockedParts {
    void* first;
    std::uint32_t firstBytes;
    void* second;
    std::uint32_t secondBytes;
};

void LockBuffer(void* buffer, std::uint32_t bytes, LockedParts* parts) {
    void* const sound = BufferInterface(buffer);
    using LockFn = std::int32_t(__stdcall*)(void* sound, std::uint32_t offset, std::uint32_t bytes, void** first,
                                            std::uint32_t* firstBytes, void** second, std::uint32_t* secondBytes,
                                            std::uint32_t flags);
    const std::int32_t hr = Method<LockFn>(sound, kComLock)(sound, 0, bytes, &parts->first, &parts->firstBytes,
                                                            &parts->second, &parts->secondBytes, 0);
    CheckResult(buffer, hr, texts::kDSLock);
}

void CopyIntoParts(const LockedParts& parts, const std::uint8_t* block) {
    std::memcpy(parts.first, block, parts.firstBytes);
    std::memcpy(parts.second, block + parts.firstBytes, parts.secondBytes);
}

void UnlockBuffer(void* buffer, const LockedParts& parts) {
    void* const sound = BufferInterface(buffer);
    using UnlockFn = std::int32_t(__stdcall*)(void* sound, void* first, std::uint32_t firstBytes, void* second,
                                              std::uint32_t secondBytes);
    const std::int32_t hr =
        Method<UnlockFn>(sound, kComUnlock)(sound, parts.first, parts.firstBytes, parts.second, parts.secondBytes);
    CheckResult(buffer, hr, texts::kDSUnlock);
}

// The sample read from the effect's file into a new block (Load, Restore).
std::uint8_t* ReadSample(CSoundEffect* effect, std::uint32_t line) {
    auto* const block = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(effect->bytes));
    if (block == nullptr) {
        CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kTempBuffer.c_str(), texts::kDsoundCppFile.c_str(), line);
    }
    Method<ThisArgsFn>(effect->file, kSlotFileRead)(effect->file, nullptr, effect->bytes,
                                                     reinterpret_cast<std::uint32_t>(block));
    Virtual(effect->file, kSlotFileClose);
    return block;
}

// The iterator over the duplicates as the original builds it on its stack.
void StartIterator(SkipListIter* iter, CSoundEffect* effect) {
    CallAt<ThisPtrFn>(kIterConstruct)(iter, nullptr, &effect->duplicates);
    iter->vtable = vtables::kEffectBufferDictIter;
}

bool Next(SkipListIter* iter) {
    return CallAt<ThisFn>(kIterIncrement)(iter, nullptr) != 0;
}

void CurrentBuffer(SkipListIter* iter, CSoundEffectBuffer* copy) {
    CallAt<ThisPtrFn>(kIterValue)(iter, nullptr, copy);
}

// A copy of the iterator's current duplicate, destroyed at the end of its
// scope, also when a DirectSound call throws (the original's frames destroy
// their copies while the exception unwinds them, which releases the copy's
// reference).
class CurrentCopy {
public:
    explicit CurrentCopy(SkipListIter* iter) { CurrentBuffer(iter, &buffer_); }
    ~CurrentCopy() { DestroyBuffer(&buffer_); }
    CurrentCopy(const CurrentCopy&) = delete;
    CurrentCopy& operator=(const CurrentCopy&) = delete;
    CSoundEffectBuffer* get() { return &buffer_; }

private:
    CSoundEffectBuffer buffer_;
};

// The key of the iterator's node (0 and the error hook without one).
std::int32_t CurrentKey(SkipListIter* iter) {
    if (iter->node == 0) {
        CallAt<ThisFn>(kNoOp)(iter, nullptr);
        return 0;
    }
    return *reinterpret_cast<const std::int32_t*>(iter->node - kNodeKey);
}

// Whether the iterator's duplicate has stopped (checked on a copy).
bool CurrentStopped(SkipListIter* iter) {
    CurrentCopy copy(iter);
    return !IsPlaying(copy.get());
}

// The inlined operator[]: the duplicate of `key` (inserted if missing), or
// null if the insert failed.
CSoundEffectBuffer* Lookup(EffectBufferDict* dict, std::int32_t key) {
    dict->keySlot = key;
    std::uint32_t node = CallAt<NodeFn>(kDictFind)(dict, nullptr, &dict->keySlot);
    if (node == 0) {
        node = CallAt<NodeFn>(kDictInsert)(dict, nullptr, &dict->keySlot);
        if (node == 0) {
            return nullptr;
        }
    }
    return reinterpret_cast<CSoundEffectBuffer*>(node - kNodeValue);
}

using BufferValueFn = std::int32_t(__stdcall*)(void* sound, std::int32_t value);
using BufferPlayFn = std::int32_t(__stdcall*)(void* sound, std::uint32_t reserved, std::uint32_t priority,
                                             std::uint32_t flags);

void SetPan(void* buffer, std::int32_t pan) {
    void* const sound = BufferInterface(buffer);
    CheckResult(buffer, Method<BufferValueFn>(sound, kComSetPan)(sound, pan), texts::kDSSetPan);
}

void SetVolume(void* buffer, std::int32_t volume) {
    void* const sound = BufferInterface(buffer);
    CheckResult(buffer, Method<BufferValueFn>(sound, kComSetVolume)(sound, volume), texts::kDSSetVolume);
}

void PlayBuffer(void* buffer, std::uint32_t flags) {
    void* const sound = BufferInterface(buffer);
    CheckResult(buffer, Method<BufferPlayFn>(sound, kComPlay)(sound, 0, 0, flags), texts::kDSPlay);
}

// The local WCValOrderedVector<int> of PurgeStoppedDuplicates.
struct KeyVector {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::int32_t* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;  // +0x14
    std::uint32_t grow;
};
static_assert(sizeof(KeyVector) == 0x1c);

// The vector's destructor, inlined in the original (also run while an
// exception unwinds PurgeStoppedDuplicates).
class KeyVectorScope {
public:
    explicit KeyVectorScope(KeyVector* keys) : keys_(keys) {}
    ~KeyVectorScope() {
        keys_->vtable = vtables::kWCBareVectorBase_int;
        if (keys_->data != nullptr) {
            CallAt<ThisFn>(kNoOp)(keys_, nullptr);
            msvc6::GameOperatorDelete(keys_->data);
        }
    }
    KeyVectorScope(const KeyVectorScope&) = delete;
    KeyVectorScope& operator=(const KeyVectorScope&) = delete;

private:
    KeyVector* keys_;
};

}  // namespace

// "throw new CAppNotActiveException(string(""), 1)" (also used by the
// DirectDraw wrappers: app_not_active.h).
[[noreturn]] void framework::ThrowAppNotActive() {
    void* object = msvc6::GameOperatorNew(kExceptionBytes);
    if (object != nullptr) {
        const std::uint8_t allocator = 0;  // the original's are uninitialized (copied with 0 by CException)
        String message;  // left alive: the original's unwinding frees it, ours has no frame
        message.allocator = allocator;
        msvc6::String_Tidy(&message, nullptr, 0);
        String text;
        text.allocator = allocator;
        msvc6::String_Tidy(&text, nullptr, 0);
        const char* const empty = texts::kEmpty.c_str();
        msvc6::String_AssignChars(&text, nullptr, empty, msvc6::detail::Length(empty));
        String copy;
        copy.allocator = allocator;
        msvc6::String_Tidy(&copy, nullptr, 0);
        msvc6::String_AssignSubstr(&copy, nullptr, &text, 0, 0xFFFFFFFFu);
        msvc6::String_AssignSubstr(&message, nullptr, &copy, 0, msvc6::detail::Npos());
        msvc6::String_Tidy(&copy, nullptr, 1);
        msvc6::String_Tidy(&text, nullptr, 1);
        object = reinterpret_cast<void*>(CallAt<ThisArgsFn>(kAppNotActiveConstruct)(
            object, nullptr, reinterpret_cast<std::uint32_t>(&message), 1));
    }
    using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);
    CallAt<ThrowFn>(kCxxThrowException)(&object, throw_info::kAppNotActiveException);
    __assume(0);
}

namespace {

bool ApplicationActive() {
    void* const application = reinterpret_cast<void*>(CallAt<ThisFn>(kGetApplication)(nullptr, nullptr));
    return (CallAt<ThisFn>(kApplicationIsActive)(application, nullptr) & 0xFF) == 1;
}

}  // namespace

using EffectThisFn = void*(__fastcall*)(void* self, void* edx);
using EffectValueFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using EffectLoadFn = void(__fastcall*)(void* self, void* edx, void* file, std::uint32_t unused);
using EffectPlayFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t volume, std::int32_t pan,
                                               std::uint32_t flags);
using EffectDuplicateFn = void*(__fastcall*)(void* self, void* edx, void* dest);
using NewNodeFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* pair, std::uint32_t level);
using DestroyNodeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t node, std::uint32_t level);
using DictIndexFn = void*(__fastcall*)(void* self, void* edx, const std::int32_t* key);
using IterValueFn = void*(__fastcall*)(void* self, void* edx, void* result);

void* __fastcall DirectX_CSoundEffect_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffect_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffect_Unload(void* self, void* edx);
void __fastcall DirectX_CSoundEffect_Load(void* self, void* edx, void* file, std::uint32_t unused);
std::uint32_t __fastcall DirectX_CSoundEffect_Restore(void* self, void* edx);
std::int32_t __fastcall DirectX_CSoundEffect_Play(void* self, void* edx, std::int32_t volume, std::int32_t pan,
                                                  std::uint32_t flags);
std::uint32_t __fastcall DirectX_CSoundEffect_PurgeStoppedDuplicates(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSoundEffectBuffer_Restore(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSound_RestoreIfActive(void* self, void* edx);
void* __fastcall DirectX_CSoundEffectBuffer_DuplicateInto(void* self, void* edx, void* dest);
std::uint32_t __fastcall EffectBufferDict_NewNode(void* self, void* edx, const void* pair, std::uint32_t level);
std::uint32_t __fastcall EffectBufferDict_DestroyNode(void* self, void* edx, std::uint32_t node,
                                                      std::uint32_t level);
void* __fastcall EffectBufferDict_Index(void* self, void* edx, const std::int32_t* key);
void* __fastcall EffectBufferDictIter_Value(void* self, void* edx, void* result);

// ---------------------------------------------------------------------------
// CSoundEffect
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c2aa0, DirectX_CSoundEffect_Construct, EffectThisFn, Original_DirectX_CSoundEffect_Construct,
          DirectX_CSoundEffect_Construct);

// Purpose: constructor of a sound effect (no sample loaded yet).
void* __fastcall DirectX_CSoundEffect_Construct(void* self, void* /*edx*/) {
    CSoundEffect* const effect = AsEffect(self);
    EffectBufferDict* const dict = &effect->duplicates;
    using BaseFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t probability, std::uint32_t levels,
                                      std::uint32_t duplicates);
    CallAt<BaseFn>(kSkipNonTempConstruct)(dict, nullptr, kDictProbability, kDictLevels, 0);
    std::uint32_t* const dictVtable = reinterpret_cast<std::uint32_t*>(dict->base);
    *dictVtable = vtables::kEffectBufferSkipListBase;
    CallAt<ThisPtrFn>(kEffectBufferConstruct)(&dict->valueSlot, nullptr, nullptr);
    *dictVtable = vtables::kEffectBufferDict;
    CallAt<ThisPtrFn>(kEffectBufferConstruct)(&effect->master, nullptr, effect);
    effect->bytes = 0;
    effect->lastPurge = 0;
    effect->vtable = vtables::kCSoundEffect;
    return self;
}

S3RE_HOOK(0x005c2b00, DirectX_CSoundEffect_Destroy, EffectValueFn, Original_DirectX_CSoundEffect_Destroy,
          DirectX_CSoundEffect_Destroy);

// Purpose: destructor of a sound effect: releases its buffers and file.
std::uint32_t __fastcall DirectX_CSoundEffect_Destroy(void* self, void* /*edx*/) {
    CSoundEffect* const effect = AsEffect(self);
    effect->vtable = vtables::kCSoundEffect;
    CallAt<ThisFn>(kEffectUnload)(effect, nullptr);
    DestroyBuffer(&effect->master);
    return CallAt<ThisFn>(kDictDestroy)(&effect->duplicates, nullptr);  // inlined in the original
}

S3RE_HOOK(0x005c2ce0, DirectX_CSoundEffect_Unload, EffectValueFn, Original_DirectX_CSoundEffect_Unload,
          DirectX_CSoundEffect_Unload);

// Purpose: releases the effect's buffers and deletes its sound file.
std::uint32_t __fastcall DirectX_CSoundEffect_Unload(void* self, void* /*edx*/) {
    CSoundEffect* const effect = AsEffect(self);
    CallAt<ThisFn>(kDictClear)(&effect->duplicates, nullptr);
    std::uint32_t result = Virtual(&effect->master, kSlotBufferRelease);
    if (effect->file != nullptr) {
        result = Method<ThisArgFn>(effect->file, kSlotDelete)(effect->file, nullptr, 1);
    }
    effect->file = nullptr;
    effect->bytes = 0;
    return result;
}

S3RE_HOOK(0x005c2ba0, DirectX_CSoundEffect_Load, EffectLoadFn, Original_DirectX_CSoundEffect_Load,
          DirectX_CSoundEffect_Load);

// Purpose: loads a sample from a sound file into the effect's own
// DirectSound buffer (the effect keeps the file for restoring).
void __fastcall DirectX_CSoundEffect_Load(void* self, void* /*edx*/, void* file, std::uint32_t /*unused*/) {
    CSoundEffect* const effect = AsEffect(self);
    effect->file = file;
    Virtual(file, kSlotFileOpen);
    Method<ThisPtrsFn>(effect->file, kSlotFileGetFormat)(effect->file, nullptr, &effect->format, &effect->bytes);
    std::uint8_t* const block = ReadSample(effect, kLineLoadBlock);
    CallAt<ThisArgFn>(kBufferSetCreateFlags)(&effect->master, nullptr, kStaticBuffer);
    CallAt<ThisArgsFn>(kSecondaryCreate)(&effect->master, nullptr, reinterpret_cast<std::uint32_t>(&effect->format),
                                         effect->bytes);
    // Lock's out values are uninitialized locals in the original; the first
    // size shares the `file` argument's slot (deliberate).
    LockedParts parts{nullptr, reinterpret_cast<std::uint32_t>(file), nullptr, 0};
    LockBuffer(&effect->master, effect->bytes, &parts);
    CopyIntoParts(parts, block);
    UnlockBuffer(&effect->master, parts);
    msvc6::GameOperatorDelete(block);
}

S3RE_HOOK(0x005c2d20, DirectX_CSoundEffect_Restore, EffectValueFn, Original_DirectX_CSoundEffect_Restore,
          DirectX_CSoundEffect_Restore);

// Purpose: refills the effect's buffers from its file after DirectSound
// lost their memory.
std::uint32_t __fastcall DirectX_CSoundEffect_Restore(void* self, void* /*edx*/) {
    CSoundEffect* const effect = AsEffect(self);
    SkipListIter iter;
    StartIterator(&iter, effect);
    Virtual(effect->file, kSlotFileOpen);
    std::uint8_t* const block = ReadSample(effect, kLineRestoreBlock);
    Virtual(&effect->master, kSlotBufferRestore);
    LockedParts parts{nullptr, 0, nullptr, 0};  // uninitialized in the original
    LockBuffer(&effect->master, effect->bytes, &parts);
    CopyIntoParts(parts, block);
    UnlockBuffer(&effect->master, parts);
    while (Next(&iter)) {
        {
            CurrentCopy copy(&iter);
            Virtual(copy.get(), kSlotBufferRestore);
        }
        const std::uint32_t bytes = effect->bytes;
        {
            CurrentCopy copy(&iter);
            LockBuffer(copy.get(), bytes, &parts);
        }
        CopyIntoParts(parts, block);
        CurrentCopy copy(&iter);
        UnlockBuffer(copy.get(), parts);
    }
    msvc6::GameOperatorDelete(block);
    return 0;  // callers ignore EAX
}

S3RE_HOOK(0x005c2fc0, DirectX_CSoundEffect_Play, EffectPlayFn, Original_DirectX_CSoundEffect_Play,
          DirectX_CSoundEffect_Play);

// Purpose: plays the effect with a volume and pan: on its own buffer if
// that is idle, else on a stopped duplicate, else on a new duplicate.
// Returns the key of the buffer used (0 for the effect's own).
std::int32_t __fastcall DirectX_CSoundEffect_Play(void* self, void* /*edx*/, std::int32_t volume, std::int32_t pan,
                                                  std::uint32_t flags) {
    CSoundEffect* const effect = AsEffect(self);
    EffectBufferDict* const dict = &effect->duplicates;
    SkipListIter iter;
    StartIterator(&iter, effect);
    std::int32_t key = kNewKey;
    if (!IsPlaying(&effect->master)) {
        key = 0;
        SetPan(&effect->master, pan);
        SetVolume(&effect->master, volume);
        PlayBuffer(&effect->master, flags);
    } else {
        while (Next(&iter)) {
            if (CurrentStopped(&iter)) {
                key = CurrentKey(&iter);
            }
        }
        if (key == kNewKey) {
            key = static_cast<std::int32_t>(g_soundDuplicateCounter);
            g_soundDuplicateCounter = g_soundDuplicateCounter + 1;
            CallAt<ThisPtrFn>(kDuplicateInto)(&effect->master, nullptr, Lookup(dict, key));
        }
        SetPan(Lookup(dict, key), pan);
        SetVolume(Lookup(dict, key), volume);
        PlayBuffer(CallAt<DictIndexFn>(kDictIndex)(dict, nullptr, &key), flags);
    }
    CallAt<ThisFn>(kPurgeStoppedDuplicates)(effect, nullptr);
    iter.vtable = vtables::kEffectBufferDictIter;
    CallAt<ThisFn>(kIterDestroy)(&iter, nullptr);
    return key;
}

S3RE_HOOK(0x005c3210, DirectX_CSoundEffect_PurgeStoppedDuplicates, EffectValueFn,
          Original_DirectX_CSoundEffect_PurgeStoppedDuplicates, DirectX_CSoundEffect_PurgeStoppedDuplicates);

// Purpose: at most every 2.5 s, deletes the duplicate buffers that have
// stopped; true when the effect is idle (no duplicates, its own buffer
// stopped), so that the sound data may delete it.
std::uint32_t __fastcall DirectX_CSoundEffect_PurgeStoppedDuplicates(void* self, void* /*edx*/) {
    CSoundEffect* const effect = AsEffect(self);
    EffectBufferDict* const dict = &effect->duplicates;
    SkipListIter iter;
    StartIterator(&iter, effect);
    KeyVector keys;
    CallAt<ThisArgsFn>(kKeyVectorConstruct)(&keys, nullptr, kKeyVectorCapacity, kKeyVectorGrowth);
    keys.vtable = vtables::kWCValOrderedVector_int;
    const KeyVectorScope keysScope(&keys);
    std::uint8_t idle = 0;
    const std::uint32_t now = g_importTimeGetTime.get()();
    if (now - effect->lastPurge >= kPurgeInterval) {
        effect->lastPurge = now;
        while (Next(&iter)) {
            if (CurrentStopped(&iter)) {
                std::int32_t key = CurrentKey(&iter);
                CallAt<ThisArgsFn>(kKeyVectorInsertAt)(&keys, nullptr, keys.length,
                                                       reinterpret_cast<std::uint32_t>(&key));
            }
        }
        for (std::uint32_t i = 0; i < keys.length; ++i) {
            const std::uint32_t index = CallAt<ThisArgFn>(kKeyVectorIndex)(&keys, nullptr, i);
            dict->keySlot = keys.data[index];
            CallAt<NodeFn>(kDictRemove)(dict, nullptr, &dict->keySlot);
        }
    }
    const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(dict->base + framework::kDictCount);
    if (count == 0 && !IsPlaying(&effect->master)) {
        idle = 1;
    }
    return idle;  // AL (callers test only AL); the vector is destroyed on the way out
}

// ---------------------------------------------------------------------------
// CSoundEffectBuffer, CDirectSound: restoring
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c28e0, DirectX_CSoundEffectBuffer_Restore, EffectValueFn, Original_DirectX_CSoundEffectBuffer_Restore,
          DirectX_CSoundEffectBuffer_Restore);

// Purpose: a lost effect buffer: refills its effect if the game is the
// active application, else throws "application not active".
std::uint32_t __fastcall DirectX_CSoundEffectBuffer_Restore(void* self, void* /*edx*/) {
    if (!ApplicationActive()) {
        framework::ThrowAppNotActive();
    }
    return CallAt<ThisFn>(kEffectRestore)(static_cast<CSoundEffectBuffer*>(self)->effect, nullptr);
}

S3RE_HOOK(0x005c1dc0, DirectX_CDirectSound_RestoreIfActive, EffectValueFn,
          Original_DirectX_CDirectSound_RestoreIfActive, DirectX_CDirectSound_RestoreIfActive);

// Purpose: restores every sound buffer (the subclass's slot 3, CSoundData::
// Restore) if the game is the active application, else throws.
std::uint32_t __fastcall DirectX_CDirectSound_RestoreIfActive(void* self, void* /*edx*/) {
    if (!ApplicationActive()) {
        framework::ThrowAppNotActive();
    }
    return Virtual(self, kSlotSoundRestore);
}

S3RE_HOOK(0x005c2a60, DirectX_CSoundEffectBuffer_DuplicateInto, EffectDuplicateFn,
          Original_DirectX_CSoundEffectBuffer_DuplicateInto, DirectX_CSoundEffectBuffer_DuplicateInto);

// Purpose: makes `dest` a duplicate of this buffer (a second DirectSound
// buffer on the same sample memory).
void* __fastcall DirectX_CSoundEffectBuffer_DuplicateInto(void* self, void* /*edx*/, void* dest) {
    CSoundEffectBuffer* const source = static_cast<CSoundEffectBuffer*>(self);
    CSoundEffectBuffer* const copy = static_cast<CSoundEffectBuffer*>(dest);
    CallAt<ThisPtrFn>(kBufferAssign)(copy, nullptr, source);
    copy->effect = source->effect;
    void* const shared = copy->base.buffer;
    Method<std::uint32_t(__stdcall*)(void*)>(shared, kComRelease)(shared);
    CallAt<ThisPtrsFn>(kDuplicateSoundBuffer)(g_directSound.get(), nullptr, source->base.buffer, &copy->base.buffer);
    copy->effect = source->effect;
    return source->effect;
}

// ---------------------------------------------------------------------------
// WCValSkipListDict<int, CSoundEffectBuffer>
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c3510, EffectBufferDict_NewNode, NewNodeFn, Original_EffectBufferDict_NewNode,
          EffectBufferDict_NewNode);

// Purpose: allocates a node of the duplicates' dictionary with a copy of
// the key and buffer.
std::uint32_t __fastcall EffectBufferDict_NewNode(void* self, void* /*edx*/, const void* pair, std::uint32_t level) {
    const auto* const dict = static_cast<const fileformats::SkipListDict*>(self);
    const std::uint32_t size = level * 4 + kNodeBaseBytes;
    void* block;
    if (dict->allocate != 0) {
        using AllocateFn = void*(__fastcall*)(std::uint32_t size);
        block = reinterpret_cast<AllocateFn>(dict->allocate)(size);
    } else {
        block = msvc6::GameOperatorNew(size);
    }
    if (block == nullptr) {
        return 0;
    }
    auto* const key = static_cast<std::int32_t*>(block);
    const auto* const source = static_cast<const std::uint8_t*>(pair);
    *key = *reinterpret_cast<const std::int32_t*>(source);
    auto* const value = reinterpret_cast<CSoundEffectBuffer*>(key + 1);
    const auto* const sourceValue = reinterpret_cast<const CSoundEffectBuffer*>(source + 4);
    CallAt<ThisPtrFn>(kBufferCopyConstruct)(value, nullptr, sourceValue);
    value->effect = sourceValue->effect;
    value->base.vtable = vtables::kCSoundEffectBuffer;
    return reinterpret_cast<std::uint32_t>(block) + kNodeKey;
}

S3RE_HOOK(0x005c35c0, EffectBufferDict_DestroyNode, DestroyNodeFn, Original_EffectBufferDict_DestroyNode,
          EffectBufferDict_DestroyNode);

// Purpose: frees a node of the duplicates' dictionary (releasing its
// buffer).
std::uint32_t __fastcall EffectBufferDict_DestroyNode(void* self, void* /*edx*/, std::uint32_t node,
                                                      std::uint32_t level) {
    if (node == 0) {
        return 0;
    }
    void* const block = reinterpret_cast<void*>(node - kNodeKey);
    CallAt<ThisFn>(kBufferValueDestroy)(block, nullptr);
    const auto* const dict = static_cast<const fileformats::SkipListDict*>(self);
    if (dict->deallocate != 0) {
        using DeallocateFn = std::uint32_t(__fastcall*)(void* block, std::uint32_t size);
        return reinterpret_cast<DeallocateFn>(dict->deallocate)(block, level * 4 + kNodeBaseBytes);
    }
    return msvc6::GameOperatorDelete(block);
}

S3RE_HOOK(0x005c3600, EffectBufferDict_Index, DictIndexFn, Original_EffectBufferDict_Index, EffectBufferDict_Index);

// Purpose: the duplicate buffer of a key (inserted if missing).
void* __fastcall EffectBufferDict_Index(void* self, void* /*edx*/, const std::int32_t* key) {
    return Lookup(static_cast<EffectBufferDict*>(self), *key);
}

S3RE_HOOK(0x005c3640, EffectBufferDictIter_Value, IterValueFn, Original_EffectBufferDictIter_Value,
          EffectBufferDictIter_Value);

// Purpose: a copy of the iterator's current duplicate buffer.
void* __fastcall EffectBufferDictIter_Value(void* self, void* /*edx*/, void* result) {
    SkipListIter* const iter = static_cast<SkipListIter*>(self);
    CSoundEffectBuffer* const copy = static_cast<CSoundEffectBuffer*>(result);
    if (iter->node == 0) {
        CallAt<ThisFn>(kNoOp)(iter, nullptr);
        CSoundEffectBuffer empty;
        CallAt<ThisPtrFn>(kEffectBufferConstruct)(&empty, nullptr, nullptr);
        CallAt<ThisPtrFn>(kBufferCopyConstruct)(copy, nullptr, &empty);
        copy->base.vtable = vtables::kCSoundEffectBuffer;
        copy->effect = empty.effect;
        DestroyBuffer(&empty);
        return result;
    }
    const auto* const source = reinterpret_cast<const CSoundEffectBuffer*>(iter->node - kNodeValue);
    CallAt<ThisPtrFn>(kBufferCopyConstruct)(copy, nullptr, source);
    copy->base.vtable = vtables::kCSoundEffectBuffer;
    copy->effect = source->effect;
    return result;
}

}  // namespace s3re::game
