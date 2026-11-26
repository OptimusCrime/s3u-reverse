#pragma once

// The DirectDraw wrapper classes (DirectX\Ddraw.cpp; directx_draw.cpp):
// DirectX::CDirectDraw, the surface classes and CPalette. Our replacements,
// callable from other replacements and tests. Pointer arguments the game
// passes through to DirectDraw unchanged are plain pointers.

#include <cstdint>

namespace s3re::game {

// ---- DirectX::CDirectDraw
void* __fastcall DirectX_CDirectDraw_Construct(void* self, void* edx, const void* other);
void* __fastcall DirectX_CDirectDraw_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall DirectX_CDirectDraw_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectDraw_DDDirectDrawEnumerate(void* self, void* edx, void* callback,
                                                                   void* context);
std::uint32_t __fastcall DirectX_CDirectDraw_Connect(void* self, void* edx, const void* device);
std::uint32_t __fastcall DirectX_CDirectDraw_Disconnect(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectDraw_DDCreateSurface(void* self, void* edx, void* desc, void** out);
std::uint32_t __fastcall DirectX_CDirectDraw_DDEnumDisplayModes(void* self, void* edx, std::uint32_t flags,
                                                                void* desc, void* context, void* callback);
std::uint32_t __fastcall DirectX_CDirectDraw_DDRestoreDisplayMode(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectDraw_DDGetDisplayMode(void* self, void* edx, void* desc);
std::uint32_t __fastcall DirectX_CDirectDraw_DDGetCaps(void* self, void* edx, void* driverCaps, void* helCaps);
std::uint32_t __fastcall DirectX_CDirectDraw_DDWaitForVerticalBlank(void* self, void* edx, std::uint32_t flags,
                                                                    void* event);
std::uint32_t __fastcall DirectX_CDirectDraw_RestoreSurfaces(void* self, void* edx);

// ---- DirectX::CSimpleSurface / CSurface
std::int32_t __fastcall DirectX_CSurface_DDLock(void* self, void* edx, void* rect, void* desc, std::uint32_t flags,
                                                void* event);
std::uint32_t __fastcall DirectX_CSurface_Lock(void* self, void* edx, void* rect, void* desc, std::uint32_t flags);
std::int32_t __fastcall DirectX_CSimpleSurface_DDGetBltStatus(void* self, void* edx, std::uint32_t flags);
void* __fastcall DirectX_CSimpleSurface_GetInterface(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSurface_UseOwnInterfaceForDrawing(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSimpleSurface_SetDirectDraw(void* self, void* edx, void* owner);
std::uint32_t __fastcall DirectX_CSurface_DDGetSurfaceDesc(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSurface_RestoreIfLost(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSurface_SetCaps(void* self, void* edx, std::uint32_t caps);
std::uint32_t __fastcall DirectX_CSurface_SetPixelFormat(void* self, void* edx, const void* format);
std::int32_t __fastcall DirectX_CSurface_DDRestore(void* self, void* edx);

// ---- DirectX::CFlipSurface (`base` = the CSurface part where noted)
void* __fastcall DirectX_CFlipSurface_Construct(void* self, void* edx, std::uint32_t backBuffers,
                                                std::uint32_t mostDerived);
void* __fastcall DirectX_CFlipSurface_DeletingDestroy(void* base, void* edx, std::uint32_t flags);
void __fastcall DirectX_CFlipSurface_Destroy(void* base, void* edx);
void* __fastcall DirectX_CFlipSurface_GetBackBuffer(void* self, void* edx);
std::int32_t __fastcall DirectX_CFlipSurface_Flip(void* self, void* edx, void* target);
std::int32_t __fastcall DirectX_CFlipSurface_DDFlip(void* self, void* edx, void* target, std::uint32_t flags);

// ---- DirectX::CPrimarySurface / CPrimaryFlipSurface
void* __fastcall DirectX_CPrimarySurface_Construct(void* self, void* edx, std::uint32_t mostDerived);
void* __fastcall DirectX_CPrimarySurface_DeletingDestroy(void* base, void* edx, std::uint32_t flags);
std::uint32_t __fastcall DirectX_CPrimarySurface_Create(void* self, void* edx);
void* __fastcall DirectX_CPrimaryFlipSurface_Construct(void* self, void* edx, std::uint32_t backBuffers,
                                                       std::uint32_t mostDerived);
void* __fastcall DirectX_CPrimaryFlipSurface_DeletingDestroy(void* base, void* edx, std::uint32_t flags);
void __fastcall DirectX_CPrimaryFlipSurface_Destroy(void* base, void* edx);
std::uint32_t __fastcall DirectX_CPrimaryFlipSurface_Create(void* self, void* edx);

// ---- DirectX::COffscreenSurface / COverlaySurface / CPalette
void* __fastcall DirectX_COffscreenSurface_Construct(void* self, void* edx);
void* __fastcall DirectX_COffscreenSurface_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall DirectX_COffscreenSurface_Create(void* self, void* edx, std::uint32_t width,
                                                          std::uint32_t height);
std::uint32_t __fastcall DirectX_COffscreenSurface_RestoreIfLost(void* self, void* edx);
void* __fastcall DirectX_COverlaySurface_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_COverlaySurface_Create(void* self, void* edx, std::uint32_t width,
                                                        std::uint32_t height);
std::uint32_t __fastcall DirectX_COverlaySurface_Hide(void* self, void* edx, void* destination);
std::uint32_t __fastcall DirectX_COverlaySurface_DDUpdateOverlay(void* self, void* edx, void* sourceRect,
                                                                 void* destination, void* destinationRect,
                                                                 std::uint32_t flags, void* fx);
std::uint32_t __fastcall DirectX_CPalette_Release(void* self, void* edx);

extern decltype(&DirectX_CDirectDraw_Construct) Original_DirectX_CDirectDraw_Construct;
extern decltype(&DirectX_CDirectDraw_DeletingDestroy) Original_DirectX_CDirectDraw_DeletingDestroy;
extern decltype(&DirectX_CDirectDraw_Destroy) Original_DirectX_CDirectDraw_Destroy;
extern decltype(&DirectX_CDirectDraw_DDDirectDrawEnumerate) Original_DirectX_CDirectDraw_DDDirectDrawEnumerate;
extern decltype(&DirectX_CDirectDraw_Connect) Original_DirectX_CDirectDraw_Connect;
extern decltype(&DirectX_CDirectDraw_Disconnect) Original_DirectX_CDirectDraw_Disconnect;
extern decltype(&DirectX_CDirectDraw_DDCreateSurface) Original_DirectX_CDirectDraw_DDCreateSurface;
extern decltype(&DirectX_CDirectDraw_DDEnumDisplayModes) Original_DirectX_CDirectDraw_DDEnumDisplayModes;
extern decltype(&DirectX_CDirectDraw_DDRestoreDisplayMode) Original_DirectX_CDirectDraw_DDRestoreDisplayMode;
extern decltype(&DirectX_CDirectDraw_DDGetDisplayMode) Original_DirectX_CDirectDraw_DDGetDisplayMode;
extern decltype(&DirectX_CDirectDraw_DDGetCaps) Original_DirectX_CDirectDraw_DDGetCaps;
extern decltype(&DirectX_CDirectDraw_DDWaitForVerticalBlank) Original_DirectX_CDirectDraw_DDWaitForVerticalBlank;
extern decltype(&DirectX_CDirectDraw_RestoreSurfaces) Original_DirectX_CDirectDraw_RestoreSurfaces;
extern decltype(&DirectX_CSurface_DDLock) Original_DirectX_CSurface_DDLock;
extern decltype(&DirectX_CSurface_Lock) Original_DirectX_CSurface_Lock;
extern decltype(&DirectX_CSimpleSurface_DDGetBltStatus) Original_DirectX_CSimpleSurface_DDGetBltStatus;
extern decltype(&DirectX_CSimpleSurface_GetInterface) Original_DirectX_CSimpleSurface_GetInterface;
extern decltype(&DirectX_CSurface_UseOwnInterfaceForDrawing) Original_DirectX_CSurface_UseOwnInterfaceForDrawing;
extern decltype(&DirectX_CSimpleSurface_SetDirectDraw) Original_DirectX_CSimpleSurface_SetDirectDraw;
extern decltype(&DirectX_CSurface_DDGetSurfaceDesc) Original_DirectX_CSurface_DDGetSurfaceDesc;
extern decltype(&DirectX_CSurface_RestoreIfLost) Original_DirectX_CSurface_RestoreIfLost;
extern decltype(&DirectX_CSurface_SetCaps) Original_DirectX_CSurface_SetCaps;
extern decltype(&DirectX_CSurface_SetPixelFormat) Original_DirectX_CSurface_SetPixelFormat;
extern decltype(&DirectX_CSurface_DDRestore) Original_DirectX_CSurface_DDRestore;
extern decltype(&DirectX_CFlipSurface_Construct) Original_DirectX_CFlipSurface_Construct;
extern decltype(&DirectX_CFlipSurface_DeletingDestroy) Original_DirectX_CFlipSurface_DeletingDestroy;
extern decltype(&DirectX_CFlipSurface_Destroy) Original_DirectX_CFlipSurface_Destroy;
extern decltype(&DirectX_CFlipSurface_GetBackBuffer) Original_DirectX_CFlipSurface_GetBackBuffer;
extern decltype(&DirectX_CFlipSurface_Flip) Original_DirectX_CFlipSurface_Flip;
extern decltype(&DirectX_CFlipSurface_DDFlip) Original_DirectX_CFlipSurface_DDFlip;
extern decltype(&DirectX_CPrimarySurface_Construct) Original_DirectX_CPrimarySurface_Construct;
extern decltype(&DirectX_CPrimarySurface_DeletingDestroy) Original_DirectX_CPrimarySurface_DeletingDestroy;
extern decltype(&DirectX_CPrimarySurface_Create) Original_DirectX_CPrimarySurface_Create;
extern decltype(&DirectX_CPrimaryFlipSurface_Construct) Original_DirectX_CPrimaryFlipSurface_Construct;
extern decltype(&DirectX_CPrimaryFlipSurface_DeletingDestroy) Original_DirectX_CPrimaryFlipSurface_DeletingDestroy;
extern decltype(&DirectX_CPrimaryFlipSurface_Destroy) Original_DirectX_CPrimaryFlipSurface_Destroy;
extern decltype(&DirectX_CPrimaryFlipSurface_Create) Original_DirectX_CPrimaryFlipSurface_Create;
extern decltype(&DirectX_COffscreenSurface_Construct) Original_DirectX_COffscreenSurface_Construct;
extern decltype(&DirectX_COffscreenSurface_DeletingDestroy) Original_DirectX_COffscreenSurface_DeletingDestroy;
extern decltype(&DirectX_COffscreenSurface_Create) Original_DirectX_COffscreenSurface_Create;
extern decltype(&DirectX_COffscreenSurface_RestoreIfLost) Original_DirectX_COffscreenSurface_RestoreIfLost;
extern decltype(&DirectX_COverlaySurface_Construct) Original_DirectX_COverlaySurface_Construct;
extern decltype(&DirectX_COverlaySurface_Create) Original_DirectX_COverlaySurface_Create;
extern decltype(&DirectX_COverlaySurface_Hide) Original_DirectX_COverlaySurface_Hide;
extern decltype(&DirectX_COverlaySurface_DDUpdateOverlay) Original_DirectX_COverlaySurface_DDUpdateOverlay;
extern decltype(&DirectX_CPalette_Release) Original_DirectX_CPalette_Release;

}  // namespace s3re::game
