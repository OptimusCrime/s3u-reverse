#pragma once

// "throw new DirectX::CAppNotActiveException(string(""), 1)": the throw the
// DirectX wrappers make when a lost surface or buffer can't be restored
// because the application is not active (CSoundEffectBuffer::Restore,
// CDirectDraw::RestoreSurfaces, CSurface::DDRestore). The original builds
// the message through three string temporaries; this does the same calls
// (defined in directx_sound_effect.cpp).

namespace s3re::game::framework {

[[noreturn]] void ThrowAppNotActive();

}  // namespace s3re::game::framework
