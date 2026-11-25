// DirectSound wrapper classes of the framework (DirectX\*.cpp; names from
// RTTI and the "where" strings): DirectX::CDirectSound, the buffer classes
// CDirectSoundBuffer, CPrimaryBuffer, CSecondaryBuffer and the
// CSoundEffectBuffer constructor, and the error check that turns a failed
// DirectSound call into the game's C++ exception. Layouts:
// framework_layout.h; COM slots are byte offsets into the interface's
// vtable (IDirectSound / IDirectSoundBuffer as in dsound.h).
//
// DirectX::DSErrorText (0x005c1c10): fastcall (ECX = hr): the name of a
//   DirectSound result from g_dsErrorNames (16 pairs), else "Unknown Error
//   Code". (Used by CDirectSoundException's description.)
// DirectX::CheckDSResult (0x005c1c40): thiscall (ECX unused; the callers
//   pass the interface's address), (hr, where) ret 8: any hr other than 0
//   (DS_OK), success codes included, throws new CDirectSoundException(hr,
//   where, 1) (0x20 bytes, constructor 0x5c15d0, ThrowInfo 0x682dc8; a null
//   from operator new throws the null pointer). EAX = 0.
//
// DirectX::CDirectSound (vtable 0x65ceb8: 0 scalar deleting destructor,
// 1 Restore-if-active 0x5c1dc0, 2 and 3 empty):
//   CDirectSound() 0x005c1cc0: +4 = 0, vtable, g_directSound = this. EAX = this.
//   ~CDirectSound() 0x005c1d00: vtable; Disconnect if +4 is set;
//     g_directSound = 0. EAX: Disconnect's result or the null tested.
//   scalar deleting destructor 0x005c1ce0 (flags) ret 4.
//   Connect 0x005c1d20 (const GUID* device) ret 4: LoadLibraryA("dsound.dll")
//     (null: CheckDSResult(E_NOTIMPL, "...Connect.1")), GetProcAddress(
//     "DirectSoundCreate") (null: E_NOTIMPL, ".2"), DirectSoundCreate(device,
//     &sound, 0) checked (".3"), +4 = sound. `sound` starts as the stack
//     slot that held ECX (this): a creator that succeeds without writing it
//     leaves `this` in +4 (kept). EAX = 0.
//   Disconnect 0x005c1da0: virtual slot 2 (the subclass's release of its
//     buffers), IUnknown::Release of +4, +4 = 0. EAX = Release's result.
//   DSDuplicateSoundBuffer 0x005c1f40 (original, IDirectSoundBuffer** copy)
//     ret 8: IDirectSound::DuplicateSoundBuffer (slot 0x14), checked. EAX = 0.
//
// DirectX::CDirectSoundBuffer (vtable 0x65cecc: 0 scalar deleting
// destructor, 1 Release 0x5c2030, 2 DSRestore 0x5c2100, 3 RestoreAll
// 0x5c20f0):
//   copy constructor 0x005c1fb0 (const CDirectSoundBuffer& other) ret 4:
//     +4, +8 copied, vtable, IUnknown::AddRef of the buffer if any. EAX = this.
//   ~CDirectSoundBuffer 0x005c1fe0: vtable; Release if +4 is set. EAX:
//     Release's result or the null tested.
//   scalar deleting destructor 0x005c1f90 (flags) ret 4.
//   CheckResult 0x005c2000 (hr, where) ret 8: DSERR_BUFFERLOST (0x88780096)
//     calls virtual slot 3 and returns hr; else CheckDSResult(hr, where)
//     (EAX 0).
//   Release 0x005c2030: IUnknown::Release of +4 (not checked), +4 = 0.
//     EAX = Release's result.
//   SetCreateFlags 0x005c2050 (flags) ret 4: +8 = flags.
//   DSCreateSoundBuffer 0x005c2060 (DSBUFFERDESC* desc) ret 4: desc->dwFlags
//     |= +8; IDirectSound::CreateSoundBuffer (slot 0x0c) of g_directSound's
//     interface with (desc, &buffer, 0), checked by CheckDSResult (named
//     "CDirectSound::DSCreateSoundBuffer"); +4 = buffer. `buffer` starts as
//     the stack slot of the desc argument (kept). EAX = buffer.
//   DSGetStatus 0x005c20b0: IDirectSoundBuffer::GetStatus (slot 0x24) into
//     a local that starts as the slot that held ECX (this), CheckResult;
//     EAX = 1 if DSBSTATUS_PLAYING (bit 0) is set, else 0.
//   RestoreAll 0x005c20f0 (slot 3): jumps to g_directSound's virtual slot 1
//     (restore every buffer if the application is active).
//   DSRestore 0x005c2100 (slot 2): IDirectSoundBuffer::Restore (slot 0x50),
//     CheckResult. EAX = CheckResult's result.
//   operator= 0x005c2120 (const CDirectSoundBuffer& other) ret 4: unless
//     self-assignment: +4, +8 copied and AddRef of the buffer if any (the
//     old buffer is not released). EAX = this.
//   DSGetVolume 0x005c2150 (long* volume) ret 4: IDirectSoundBuffer::
//     GetVolume (slot 0x18), CheckResult. EAX = CheckResult's result.
// DirectX::CPrimaryBuffer: constructor 0x005c2180 (base constructor,
//   vtable 0x65cee0; EAX = this); Create 0x005c21c0: DSCreateSoundBuffer with
//   a DSBUFFERDESC {0x14, DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME, 0, 0,
//   null}; EAX from it. The scalar deleting destructor 0x005c21a0 (also
//   CSecondaryBuffer's) runs ~CDirectSoundBuffer (via the thunk 0x5c28d0).
// DirectX::CSecondaryBuffer: constructor 0x005c2200 (vtable 0x65cef4);
//   Create 0x005c2220 (const WAVEFORMATEX* format, bytes) ret 8:
//   DSCreateSoundBuffer with {0x14, DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN
//   | DSBCAPS_CTRLVOLUME, bytes, 0, format}.
// DirectX::CSoundEffectBuffer::CSoundEffectBuffer 0x005c28b0 (effect) ret 4:
//   CSecondaryBuffer(), vtable 0x65cf38, +0xc = effect. EAX = this.
//
// The original CheckDSResult has an exception frame that frees the block
// if the exception's constructor throws; ours has none (as
// DirectX_ThrowIfDDFailed, directx_surface.cpp).

#include "game/framework/directx_sound.h"

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <cstdint>

namespace s3re::game {

void* __fastcall DirectX_CDirectSoundBuffer_Construct(void* self, void* edx);  // win32_classes.cpp

namespace {

using framework::CDirectSound;
using framework::CDirectSoundBuffer;
using framework::CSoundEffectBuffer;

constexpr std::uintptr_t kDirectSoundExceptionCtor = 0x005c15d0;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uint32_t kExceptionBytes = 0x20;  // sizeof(DirectX::CDirectSoundException)

constexpr std::int32_t kNotImplemented = static_cast<std::int32_t>(0x80004001);  // E_NOTIMPL
constexpr std::int32_t kBufferLost = static_cast<std::int32_t>(0x88780096);      // DSERR_BUFFERLOST
constexpr std::uint32_t kStatusPlaying = 1;                                     // DSBSTATUS_PLAYING

// COM methods (byte offsets into the interface's vtable).
constexpr std::uint32_t kAddRef = 0x04;
constexpr std::uint32_t kRelease = 0x08;
constexpr std::uint32_t kSoundCreateSoundBuffer = 0x0c;
constexpr std::uint32_t kSoundDuplicateSoundBuffer = 0x14;
constexpr std::uint32_t kBufferGetStatus = 0x24;
constexpr std::uint32_t kBufferGetVolume = 0x18;
constexpr std::uint32_t kBufferRestore = 0x50;

// Our virtual slots (byte offsets into the game object's vtable).
constexpr std::uint32_t kSlotRestoreIfActive = 0x04;  // CDirectSound slot 1
constexpr std::uint32_t kSlotReleaseBuffers = 0x08;   // CDirectSound slot 2
constexpr std::uint32_t kSlotRestoreAll = 0x0c;       // CDirectSoundBuffer slot 3

// DSBUFFERDESC (DirectX 5): dwSize, dwFlags, dwBufferBytes, dwReserved,
// lpwfxFormat.
struct BufferDesc {
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t bytes;
    std::uint32_t reserved;
    const void* format;
};
static_assert(sizeof(BufferDesc) == 0x14);
constexpr std::uint32_t kPrimaryFlags = 0x81;    // DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME
constexpr std::uint32_t kSecondaryFlags = 0xe0;  // DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME

template <typename Fn>
Fn ComMethod(void* object, std::uint32_t offset) {
    return *reinterpret_cast<Fn*>(*static_cast<std::uint8_t**>(object) + offset);
}

using ComFn = std::uint32_t(__stdcall*)(void* object);
using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);

std::uint32_t ComRelease(void* object) {
    return ComMethod<ComFn>(object, kRelease)(object);
}

std::uint32_t CallVirtual(void* object, std::uint32_t slot) {
    return ComMethod<ThisFn>(object, slot)(object, nullptr);
}

CDirectSound* AsSound(void* self) { return static_cast<CDirectSound*>(self); }
CDirectSoundBuffer* AsBuffer(void* self) { return static_cast<CDirectSoundBuffer*>(self); }

}  // namespace

S3RE_HOOK(0x005c1c10, DirectX_DSErrorText, DSErrorTextFn, Original_DirectX_DSErrorText, DirectX_DSErrorText);

// Purpose: the name of a DirectSound error code, for the error message of
// a failed sound call.
const char* __fastcall DirectX_DSErrorText(std::int32_t hr) {
    for (const DSErrorName& entry : g_dsErrorNames.get()) {
        if (entry.code == static_cast<std::uint32_t>(hr)) {
            return entry.text;
        }
    }
    return texts::kDSUnknownError.c_str();
}

S3RE_HOOK(0x005c1c40, DirectX_CheckDSResult, CheckDSResultFn, Original_DirectX_CheckDSResult, DirectX_CheckDSResult);

// Purpose: turns a DirectSound result other than DS_OK into the game's C++
// exception (CDirectSoundException with the code and the call's name).
std::uint32_t __fastcall DirectX_CheckDSResult(void* /*self*/, void* /*edx*/, std::int32_t hr, const char* where) {
    if (hr != 0) {
        void* exception = msvc6::GameOperatorNew(kExceptionBytes);
        if (exception != nullptr) {
            using CtorFn = void*(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where, std::uint32_t c);
            exception = reinterpret_cast<CtorFn>(kDirectSoundExceptionCtor)(exception, nullptr, hr, where, 1);
        }
        using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);
        reinterpret_cast<ThrowFn>(kCxxThrowException)(&exception, throw_info::kDirectSoundException);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CDirectSound
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c1cc0, DirectX_CDirectSound_Construct, DSThisFn, Original_DirectX_CDirectSound_Construct,
          DirectX_CDirectSound_Construct);

// Purpose: constructor of the DirectSound object (not connected yet); it
// becomes the one the sound buffers use.
void* __fastcall DirectX_CDirectSound_Construct(void* self, void* /*edx*/) {
    AsSound(self)->sound = nullptr;
    AsSound(self)->vtable = vtables::kCDirectSound;
    g_directSound = self;
    return self;
}

S3RE_HOOK(0x005c1da0, DirectX_CDirectSound_Disconnect, DSValueFn, Original_DirectX_CDirectSound_Disconnect,
          DirectX_CDirectSound_Disconnect);

// Purpose: releases the DirectSound object (after the subclass released
// its buffers).
std::uint32_t __fastcall DirectX_CDirectSound_Disconnect(void* self, void* /*edx*/) {
    CallVirtual(self, kSlotReleaseBuffers);
    const std::uint32_t references = ComRelease(AsSound(self)->sound);
    AsSound(self)->sound = nullptr;
    return references;
}

S3RE_HOOK(0x005c1d00, DirectX_CDirectSound_Destroy, DSValueFn, Original_DirectX_CDirectSound_Destroy,
          DirectX_CDirectSound_Destroy);

// Purpose: destructor of the DirectSound object: disconnects it and
// unregisters it.
std::uint32_t __fastcall DirectX_CDirectSound_Destroy(void* self, void* /*edx*/) {
    AsSound(self)->vtable = vtables::kCDirectSound;
    std::uint32_t result = 0;
    if (AsSound(self)->sound != nullptr) {
        result = DirectX_CDirectSound_Disconnect(self, nullptr);
    }
    g_directSound = nullptr;
    return result;
}

S3RE_HOOK(0x005c1ce0, DirectX_CDirectSound_DeletingDestroy, DSDeletingFn,
          Original_DirectX_CDirectSound_DeletingDestroy, DirectX_CDirectSound_DeletingDestroy);

// Purpose: the compiler-generated "delete this object" entry of the vtable.
void* __fastcall DirectX_CDirectSound_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    DirectX_CDirectSound_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x005c1d20, DirectX_CDirectSound_Connect, DSConnectFn, Original_DirectX_CDirectSound_Connect,
          DirectX_CDirectSound_Connect);

// Purpose: loads dsound.dll and creates the DirectSound object for a
// sound device (null: the default device).
std::uint32_t __fastcall DirectX_CDirectSound_Connect(void* self, void* /*edx*/, const void* device) {
    void* const library = g_importLoadLibraryA.get()(texts::kDsoundDll);
    if (library == nullptr) {
        DirectX_CheckDSResult(&AsSound(self)->sound, nullptr, kNotImplemented, texts::kDSConnect1);
    }
    using CreateFn = std::int32_t(__stdcall*)(const void* device, void** sound, void* outer);
    const auto create = reinterpret_cast<CreateFn>(g_importGetProcAddress.get()(library, texts::kDirectSoundCreate));
    if (create == nullptr) {
        DirectX_CheckDSResult(&AsSound(self)->sound, nullptr, kNotImplemented, texts::kDSConnect2);
    }
    void* sound = self;  // the original's local is the stack slot that held ECX (deliberate)
    const std::int32_t hr = create(device, &sound, nullptr);
    DirectX_CheckDSResult(&AsSound(self)->sound, nullptr, hr, texts::kDSConnect3);
    AsSound(self)->sound = sound;
    return 0;
}

S3RE_HOOK(0x005c1f40, DirectX_CDirectSound_DSDuplicateSoundBuffer, DSDuplicateFn,
          Original_DirectX_CDirectSound_DSDuplicateSoundBuffer, DirectX_CDirectSound_DSDuplicateSoundBuffer);

// Purpose: makes a second DirectSound buffer that shares an existing one's
// sound data (to play the same effect several times at once).
std::uint32_t __fastcall DirectX_CDirectSound_DSDuplicateSoundBuffer(void* self, void* /*edx*/, void* original,
                                                                    void** copy) {
    void* const sound = AsSound(self)->sound;
    using DuplicateFn = std::int32_t(__stdcall*)(void* sound, void* original, void** copy);
    const std::int32_t hr = ComMethod<DuplicateFn>(sound, kSoundDuplicateSoundBuffer)(sound, original, copy);
    return DirectX_CheckDSResult(&AsSound(self)->sound, nullptr, hr, texts::kDSDuplicateSoundBuffer);
}

// ---------------------------------------------------------------------------
// CDirectSoundBuffer
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c2030, DirectX_CDirectSoundBuffer_Release, DSValueFn, Original_DirectX_CDirectSoundBuffer_Release,
          DirectX_CDirectSoundBuffer_Release);

// Purpose: releases the DirectSound buffer and forgets it.
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_Release(void* self, void* /*edx*/) {
    const std::uint32_t references = ComRelease(AsBuffer(self)->buffer);
    AsBuffer(self)->buffer = nullptr;
    return references;
}

S3RE_HOOK(0x005c1fe0, DirectX_CDirectSoundBuffer_Destroy, DSValueFn, Original_DirectX_CDirectSoundBuffer_Destroy,
          DirectX_CDirectSoundBuffer_Destroy);

// Purpose: destructor of a sound buffer wrapper: releases the buffer.
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_Destroy(void* self, void* /*edx*/) {
    AsBuffer(self)->vtable = vtables::kCDirectSoundBuffer;
    if (AsBuffer(self)->buffer == nullptr) {
        return 0;
    }
    return DirectX_CDirectSoundBuffer_Release(self, nullptr);
}

S3RE_HOOK(0x005c1f90, DirectX_CDirectSoundBuffer_DeletingDestroy, DSDeletingFn,
          Original_DirectX_CDirectSoundBuffer_DeletingDestroy, DirectX_CDirectSoundBuffer_DeletingDestroy);

// Purpose: the compiler-generated "delete this object" entry of the vtable.
void* __fastcall DirectX_CDirectSoundBuffer_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    DirectX_CDirectSoundBuffer_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x005c21a0, DirectX_CPrimaryBuffer_DeletingDestroy, DSDeletingFn,
          Original_DirectX_CPrimaryBuffer_DeletingDestroy, DirectX_CPrimaryBuffer_DeletingDestroy);

// Purpose: the "delete this object" entry of the primary and secondary
// buffers' vtables (their destructor is the base one).
void* __fastcall DirectX_CPrimaryBuffer_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    DirectX_CDirectSoundBuffer_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

namespace {

// Copies the buffer pointer and flags and takes a reference.
void CopyBuffer(CDirectSoundBuffer* to, const CDirectSoundBuffer* from) {
    to->buffer = from->buffer;
    to->createFlags = from->createFlags;
}

void AddRefBuffer(void* buffer) {
    if (buffer != nullptr) {
        ComMethod<ComFn>(buffer, kAddRef)(buffer);
    }
}

}  // namespace

S3RE_HOOK(0x005c1fb0, DirectX_CDirectSoundBuffer_CopyConstruct, DSCopyFn,
          Original_DirectX_CDirectSoundBuffer_CopyConstruct, DirectX_CDirectSoundBuffer_CopyConstruct);

// Purpose: copy constructor of a sound buffer wrapper (both share the
// DirectSound buffer, which gets one more reference).
void* __fastcall DirectX_CDirectSoundBuffer_CopyConstruct(void* self, void* /*edx*/, const void* other) {
    CopyBuffer(AsBuffer(self), static_cast<const CDirectSoundBuffer*>(other));
    AsBuffer(self)->vtable = vtables::kCDirectSoundBuffer;
    AddRefBuffer(AsBuffer(self)->buffer);
    return self;
}

S3RE_HOOK(0x005c2120, DirectX_CDirectSoundBuffer_Assign, DSCopyFn, Original_DirectX_CDirectSoundBuffer_Assign,
          DirectX_CDirectSoundBuffer_Assign);

// Purpose: assignment of a sound buffer wrapper (shares the other's
// buffer; the old one keeps its reference, as in the original).
void* __fastcall DirectX_CDirectSoundBuffer_Assign(void* self, void* /*edx*/, const void* other) {
    if (self != other) {
        CopyBuffer(AsBuffer(self), static_cast<const CDirectSoundBuffer*>(other));
        AddRefBuffer(AsBuffer(self)->buffer);
    }
    return self;
}

S3RE_HOOK(0x005c2000, DirectX_CDirectSoundBuffer_CheckResult, DSCheckResultFn,
          Original_DirectX_CDirectSoundBuffer_CheckResult, DirectX_CDirectSoundBuffer_CheckResult);

// Purpose: checks the result of a call on a sound buffer: a lost buffer
// restores all sound buffers, other failures become the game's exception.
std::int32_t __fastcall DirectX_CDirectSoundBuffer_CheckResult(void* self, void* /*edx*/, std::int32_t hr,
                                                               const char* where) {
    if (hr == kBufferLost) {
        CallVirtual(self, kSlotRestoreAll);
        return hr;
    }
    return static_cast<std::int32_t>(DirectX_CheckDSResult(&AsBuffer(self)->buffer, nullptr, hr, where));
}

S3RE_HOOK(0x005c2050, DirectX_CDirectSoundBuffer_SetCreateFlags, DSSetFn,
          Original_DirectX_CDirectSoundBuffer_SetCreateFlags, DirectX_CDirectSoundBuffer_SetCreateFlags);

// Purpose: sets extra DSBCAPS flags for when the buffer is created.
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_SetCreateFlags(void* self, void* /*edx*/, std::uint32_t flags) {
    AsBuffer(self)->createFlags = flags;
    return flags;  // EAX keeps the argument
}

S3RE_HOOK(0x005c2060, DirectX_CDirectSoundBuffer_DSCreateSoundBuffer, DSCreateFn,
          Original_DirectX_CDirectSoundBuffer_DSCreateSoundBuffer, DirectX_CDirectSoundBuffer_DSCreateSoundBuffer);

// Purpose: creates the DirectSound buffer from a buffer description
// through the game's DirectSound object.
void* __fastcall DirectX_CDirectSoundBuffer_DSCreateSoundBuffer(void* self, void* /*edx*/, void* desc) {
    static_cast<BufferDesc*>(desc)->flags |= AsBuffer(self)->createFlags;
    CDirectSound* const owner = static_cast<CDirectSound*>(g_directSound.get());
    void* const sound = owner->sound;
    void* buffer = desc;  // the original's local is the desc argument's stack slot (deliberate)
    using CreateFn = std::int32_t(__stdcall*)(void* sound, void* desc, void** buffer, void* outer);
    const std::int32_t hr = ComMethod<CreateFn>(sound, kSoundCreateSoundBuffer)(sound, desc, &buffer, nullptr);
    DirectX_CheckDSResult(&owner->sound, nullptr, hr, texts::kDSCreateSoundBuffer);
    AsBuffer(self)->buffer = buffer;
    return buffer;
}

S3RE_HOOK(0x005c20b0, DirectX_CDirectSoundBuffer_DSGetStatus, DSValueFn,
          Original_DirectX_CDirectSoundBuffer_DSGetStatus, DirectX_CDirectSoundBuffer_DSGetStatus);

// Purpose: whether the buffer is playing.
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_DSGetStatus(void* self, void* /*edx*/) {
    void* const buffer = AsBuffer(self)->buffer;
    // The original's local is the stack slot that held ECX (deliberate).
    std::uint32_t status = reinterpret_cast<std::uint32_t>(self);
    using GetStatusFn = std::int32_t(__stdcall*)(void* buffer, std::uint32_t* status);
    const std::int32_t hr = ComMethod<GetStatusFn>(buffer, kBufferGetStatus)(buffer, &status);
    DirectX_CDirectSoundBuffer_CheckResult(self, nullptr, hr, texts::kDSGetStatus);
    return (status & kStatusPlaying) != 0 ? 1 : 0;
}

S3RE_HOOK(0x005c20f0, DirectX_CDirectSoundBuffer_RestoreAll, DSValueFn,
          Original_DirectX_CDirectSoundBuffer_RestoreAll, DirectX_CDirectSoundBuffer_RestoreAll);

// Purpose: a lost buffer restores every sound buffer through the
// DirectSound object (if the game is the active application).
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_RestoreAll(void* /*self*/, void* /*edx*/) {
    return CallVirtual(g_directSound.get(), kSlotRestoreIfActive);
}

S3RE_HOOK(0x005c2100, DirectX_CDirectSoundBuffer_DSRestore, DSValueFn,
          Original_DirectX_CDirectSoundBuffer_DSRestore, DirectX_CDirectSoundBuffer_DSRestore);

// Purpose: restores the memory of a lost DirectSound buffer.
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_DSRestore(void* self, void* /*edx*/) {
    void* const buffer = AsBuffer(self)->buffer;
    const std::int32_t hr = ComMethod<std::int32_t(__stdcall*)(void*)>(buffer, kBufferRestore)(buffer);
    return static_cast<std::uint32_t>(DirectX_CDirectSoundBuffer_CheckResult(self, nullptr, hr, texts::kDSRestore));
}

S3RE_HOOK(0x005c2150, DirectX_CDirectSoundBuffer_DSGetVolume, DSGetVolumeFn,
          Original_DirectX_CDirectSoundBuffer_DSGetVolume, DirectX_CDirectSoundBuffer_DSGetVolume);

// Purpose: reads the buffer's volume (the game's master volume when used
// on the primary buffer).
std::int32_t __fastcall DirectX_CDirectSoundBuffer_DSGetVolume(void* self, void* /*edx*/, std::int32_t* volume) {
    void* const buffer = AsBuffer(self)->buffer;
    using GetVolumeFn = std::int32_t(__stdcall*)(void* buffer, std::int32_t* volume);
    const std::int32_t hr = ComMethod<GetVolumeFn>(buffer, kBufferGetVolume)(buffer, volume);
    return DirectX_CDirectSoundBuffer_CheckResult(self, nullptr, hr, texts::kDSGetVolume);
}

// ---------------------------------------------------------------------------
// CPrimaryBuffer, CSecondaryBuffer, CSoundEffectBuffer
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c2180, DirectX_CPrimaryBuffer_Construct, DSThisFn, Original_DirectX_CPrimaryBuffer_Construct,
          DirectX_CPrimaryBuffer_Construct);

// Purpose: constructor of the primary buffer wrapper (the sound card's
// mixing buffer: its format and the master volume).
void* __fastcall DirectX_CPrimaryBuffer_Construct(void* self, void* /*edx*/) {
    DirectX_CDirectSoundBuffer_Construct(self, nullptr);
    AsBuffer(self)->vtable = vtables::kCPrimaryBuffer;
    return self;
}

S3RE_HOOK(0x005c21c0, DirectX_CPrimaryBuffer_Create, DSPrimaryCreateFn, Original_DirectX_CPrimaryBuffer_Create,
          DirectX_CPrimaryBuffer_Create);

// Purpose: creates the primary DirectSound buffer.
void* __fastcall DirectX_CPrimaryBuffer_Create(void* self, void* /*edx*/) {
    BufferDesc desc{sizeof(BufferDesc), kPrimaryFlags, 0, 0, nullptr};
    return DirectX_CDirectSoundBuffer_DSCreateSoundBuffer(self, nullptr, &desc);
}

S3RE_HOOK(0x005c2200, DirectX_CSecondaryBuffer_Construct, DSThisFn, Original_DirectX_CSecondaryBuffer_Construct,
          DirectX_CSecondaryBuffer_Construct);

// Purpose: constructor of a secondary buffer wrapper (one sound's samples).
void* __fastcall DirectX_CSecondaryBuffer_Construct(void* self, void* /*edx*/) {
    DirectX_CDirectSoundBuffer_Construct(self, nullptr);
    AsBuffer(self)->vtable = vtables::kCSecondaryBuffer;
    return self;
}

S3RE_HOOK(0x005c2220, DirectX_CSecondaryBuffer_Create, DSSecondaryCreateFn,
          Original_DirectX_CSecondaryBuffer_Create, DirectX_CSecondaryBuffer_Create);

// Purpose: creates a secondary DirectSound buffer for samples of the given
// format and size, with volume, pan and frequency control.
void* __fastcall DirectX_CSecondaryBuffer_Create(void* self, void* /*edx*/, const void* format, std::uint32_t bytes) {
    BufferDesc desc{sizeof(BufferDesc), kSecondaryFlags, bytes, 0, format};
    return DirectX_CDirectSoundBuffer_DSCreateSoundBuffer(self, nullptr, &desc);
}

S3RE_HOOK(0x005c28b0, DirectX_CSoundEffectBuffer_Construct, DSCopyFn, Original_DirectX_CSoundEffectBuffer_Construct,
          DirectX_CSoundEffectBuffer_Construct);

// Purpose: constructor of the buffer of one playing sound effect (it
// remembers its effect for restoring).
void* __fastcall DirectX_CSoundEffectBuffer_Construct(void* self, void* /*edx*/, const void* effect) {
    DirectX_CSecondaryBuffer_Construct(self, nullptr);
    CSoundEffectBuffer* const object = static_cast<CSoundEffectBuffer*>(self);
    object->base.vtable = vtables::kCSoundEffectBuffer;
    object->effect = const_cast<void*>(effect);
    return self;
}

}  // namespace s3re::game
