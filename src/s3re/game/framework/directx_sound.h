#pragma once

// The DirectSound wrapper classes (directx_sound.cpp): our replacements,
// callable from other replacements and tests.

#include <cstdint>

namespace s3re::game {

using DSErrorTextFn = const char*(__fastcall*)(std::int32_t hr);
using CheckDSResultFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
using DSThisFn = void*(__fastcall*)(void* self, void* edx);
using DSValueFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DSDeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using DSConnectFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* device);
using DSDuplicateFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* original, void** copy);
using DSCopyFn = void*(__fastcall*)(void* self, void* edx, const void* other);
using DSCheckResultFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
using DSSetFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using DSCreateFn = void*(__fastcall*)(void* self, void* edx, void* desc);
using DSGetVolumeFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t* volume);
using DSPrimaryCreateFn = void*(__fastcall*)(void* self, void* edx);
using DSSecondaryCreateFn = void*(__fastcall*)(void* self, void* edx, const void* format, std::uint32_t bytes);

const char* __fastcall DirectX_DSErrorText(std::int32_t hr);
std::uint32_t __fastcall DirectX_CheckDSResult(void* self, void* edx, std::int32_t hr, const char* where);
void* __fastcall DirectX_CDirectSound_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSound_Destroy(void* self, void* edx);
void* __fastcall DirectX_CDirectSound_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall DirectX_CDirectSound_Connect(void* self, void* edx, const void* device);
std::uint32_t __fastcall DirectX_CDirectSound_Disconnect(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSound_DSDuplicateSoundBuffer(void* self, void* edx, void* original,
                                                                    void** copy);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_Release(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_Destroy(void* self, void* edx);
void* __fastcall DirectX_CDirectSoundBuffer_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void* __fastcall DirectX_CPrimaryBuffer_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void* __fastcall DirectX_CDirectSoundBuffer_CopyConstruct(void* self, void* edx, const void* other);
void* __fastcall DirectX_CDirectSoundBuffer_Assign(void* self, void* edx, const void* other);
std::int32_t __fastcall DirectX_CDirectSoundBuffer_CheckResult(void* self, void* edx, std::int32_t hr,
                                                               const char* where);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_SetCreateFlags(void* self, void* edx, std::uint32_t flags);
void* __fastcall DirectX_CDirectSoundBuffer_DSCreateSoundBuffer(void* self, void* edx, void* desc);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_DSGetStatus(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_RestoreAll(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectSoundBuffer_DSRestore(void* self, void* edx);
std::int32_t __fastcall DirectX_CDirectSoundBuffer_DSGetVolume(void* self, void* edx, std::int32_t* volume);
void* __fastcall DirectX_CPrimaryBuffer_Construct(void* self, void* edx);
void* __fastcall DirectX_CPrimaryBuffer_Create(void* self, void* edx);
void* __fastcall DirectX_CSecondaryBuffer_Construct(void* self, void* edx);
void* __fastcall DirectX_CSecondaryBuffer_Create(void* self, void* edx, const void* format, std::uint32_t bytes);
void* __fastcall DirectX_CSoundEffectBuffer_Construct(void* self, void* edx, const void* effect);

extern DSErrorTextFn Original_DirectX_DSErrorText;
extern CheckDSResultFn Original_DirectX_CheckDSResult;
extern DSThisFn Original_DirectX_CDirectSound_Construct;
extern DSValueFn Original_DirectX_CDirectSound_Destroy;
extern DSDeletingFn Original_DirectX_CDirectSound_DeletingDestroy;
extern DSConnectFn Original_DirectX_CDirectSound_Connect;
extern DSValueFn Original_DirectX_CDirectSound_Disconnect;
extern DSDuplicateFn Original_DirectX_CDirectSound_DSDuplicateSoundBuffer;
extern DSValueFn Original_DirectX_CDirectSoundBuffer_Release;
extern DSValueFn Original_DirectX_CDirectSoundBuffer_Destroy;
extern DSDeletingFn Original_DirectX_CDirectSoundBuffer_DeletingDestroy;
extern DSDeletingFn Original_DirectX_CPrimaryBuffer_DeletingDestroy;
extern DSCopyFn Original_DirectX_CDirectSoundBuffer_CopyConstruct;
extern DSCopyFn Original_DirectX_CDirectSoundBuffer_Assign;
extern DSCheckResultFn Original_DirectX_CDirectSoundBuffer_CheckResult;
extern DSSetFn Original_DirectX_CDirectSoundBuffer_SetCreateFlags;
extern DSCreateFn Original_DirectX_CDirectSoundBuffer_DSCreateSoundBuffer;
extern DSValueFn Original_DirectX_CDirectSoundBuffer_DSGetStatus;
extern DSValueFn Original_DirectX_CDirectSoundBuffer_RestoreAll;
extern DSValueFn Original_DirectX_CDirectSoundBuffer_DSRestore;
extern DSGetVolumeFn Original_DirectX_CDirectSoundBuffer_DSGetVolume;
extern DSThisFn Original_DirectX_CPrimaryBuffer_Construct;
extern DSPrimaryCreateFn Original_DirectX_CPrimaryBuffer_Create;
extern DSThisFn Original_DirectX_CSecondaryBuffer_Construct;
extern DSSecondaryCreateFn Original_DirectX_CSecondaryBuffer_Create;
extern DSCopyFn Original_DirectX_CSoundEffectBuffer_Construct;

}  // namespace s3re::game
