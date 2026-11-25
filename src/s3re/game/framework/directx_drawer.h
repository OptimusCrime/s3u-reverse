#pragma once

// DirectX::CGameDrawer members, CDisplayMode and
// CGameDrawThread::RequestFullRedraw (directx_drawer.cpp): our
// replacements, callable from other replacements and tests.

#include <cstdint>

namespace s3re::game {

void* __fastcall DirectX_CDisplayMode_Construct(void* self, void* edx, const void* desc);
std::uint32_t __fastcall DirectX_CDisplayMode_IsEqual(const void* self, void* edx, const void* other);
void* __fastcall ReturnThis(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawer_Initialize(void* self, void* edx, void* window);
std::uint32_t __fastcall DirectX_CGameDrawer_CreateOnDevice(void* self, void* edx, void* device);
std::uint32_t __fastcall DirectX_CGameDrawer_Create(void* self, void* edx, const void* guid);
std::uint32_t __fastcall DirectX_CGameDrawer_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawer_AddSurface(void* self, void* edx, void* surface);
std::uint32_t __fastcall DirectX_CGameDrawer_SetPrimarySurface(void* self, void* edx, void* primary);
std::uint32_t __fastcall DirectX_CGameDrawer_RemoveSurface(void* self, void* edx, void* surface);
std::uint32_t __fastcall SetMemberByte7CToOne(void* self, void* edx, std::uint32_t unused);
std::uint32_t __fastcall DirectX_CGameDrawer_ReleaseSurfaces(void* self, void* edx, std::uint32_t unused);
void* __fastcall WCListIterBase_Construct(void* self, void* edx, void* list);
std::uint32_t __fastcall DirectX_CGameDrawer_RestoreSurfaces(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawer_ReleasePalettes(void* self, void* edx, std::uint32_t unused);
std::uint32_t __fastcall DirectX_CGameDrawThread_RequestFullRedraw(void* self, void* edx);

extern decltype(&DirectX_CDisplayMode_Construct) Original_DirectX_CDisplayMode_Construct;
extern decltype(&DirectX_CDisplayMode_IsEqual) Original_DirectX_CDisplayMode_IsEqual;
extern decltype(&ReturnThis) Original_ReturnThis;
extern decltype(&DirectX_CGameDrawer_Initialize) Original_DirectX_CGameDrawer_Initialize;
extern decltype(&DirectX_CGameDrawer_CreateOnDevice) Original_DirectX_CGameDrawer_CreateOnDevice;
extern decltype(&DirectX_CGameDrawer_Create) Original_DirectX_CGameDrawer_Create;
extern decltype(&DirectX_CGameDrawer_Destroy) Original_DirectX_CGameDrawer_Destroy;
extern decltype(&DirectX_CGameDrawer_AddSurface) Original_DirectX_CGameDrawer_AddSurface;
extern decltype(&DirectX_CGameDrawer_SetPrimarySurface) Original_DirectX_CGameDrawer_SetPrimarySurface;
extern decltype(&DirectX_CGameDrawer_RemoveSurface) Original_DirectX_CGameDrawer_RemoveSurface;
extern decltype(&SetMemberByte7CToOne) Original_SetMemberByte7CToOne;
extern decltype(&DirectX_CGameDrawer_ReleaseSurfaces) Original_DirectX_CGameDrawer_ReleaseSurfaces;
extern decltype(&WCListIterBase_Construct) Original_WCListIterBase_Construct;
extern decltype(&DirectX_CGameDrawer_RestoreSurfaces) Original_DirectX_CGameDrawer_RestoreSurfaces;
extern decltype(&DirectX_CGameDrawer_ReleasePalettes) Original_DirectX_CGameDrawer_ReleasePalettes;
extern decltype(&DirectX_CGameDrawThread_RequestFullRedraw) Original_DirectX_CGameDrawThread_RequestFullRedraw;

}  // namespace s3re::game
