#pragma once

// S3.EXE functions that our libpng / zlib replacements call: the statically
// linked C runtime and the libpng functions themselves. Calls go through the
// S3.EXE address (they land in our replacement through its hook), so tests
// can stub them and the counting thunk sees them. All cdecl.

#include <cstddef>
#include <cstdint>

namespace s3re::thirdparty {

struct PngStruct;
struct PngInfo;

namespace calls {

// The C runtime (statically linked MSVC 6).
inline constexpr std::uintptr_t kMalloc = 0x00621784;
inline constexpr std::uintptr_t kFree = 0x00621b7e;
inline constexpr std::uintptr_t kCalloc = 0x00622199;
inline constexpr std::uintptr_t kFprintf = 0x006214d6;
inline constexpr std::uintptr_t kFread = 0x0062166d;
inline constexpr std::uintptr_t kLongjmp = 0x00621fc0;

// libpng 1.0.2.
inline constexpr std::uintptr_t kPngError = 0x005a9e20;
inline constexpr std::uintptr_t kPngWarning = 0x005a9e60;
inline constexpr std::uintptr_t kPngChunkError = 0x005a9ea0;
inline constexpr std::uintptr_t kPngFormatBuffer = 0x005a9ef0;
inline constexpr std::uintptr_t kPngChunkWarning = 0x005a9f80;
inline constexpr std::uintptr_t kPngCreateStruct = 0x005aa000;
inline constexpr std::uintptr_t kPngMalloc = 0x005aa060;
inline constexpr std::uintptr_t kPngFree = 0x005aa0a0;
inline constexpr std::uintptr_t kPngReadData = 0x005a9ac0;
inline constexpr std::uintptr_t kPngCalculateCrc = 0x005a4ee0;
inline constexpr std::uintptr_t kPngDefaultReadData = 0x005a9b50;

// zlib 1.1.3.
inline constexpr std::uintptr_t kCrc32 = 0x005aa130;

using MallocFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CallocFn = void*(__cdecl*)(std::size_t count, std::size_t size);
using FprintfFn = int(__cdecl*)(void* file, const char* format, ...);
using FreadFn = std::size_t(__cdecl*)(void* buffer, std::size_t size, std::size_t count, void* file);
using LongjmpFn = void(__cdecl*)(void* jmpbuf, int value);
using PngMessageFn = void(__cdecl*)(PngStruct* png, const char* message);
using PngFormatBufferFn = void(__cdecl*)(PngStruct* png, char* buffer, const char* message);
using PngCreateStructFn = void*(__cdecl*)(std::int32_t type);
using PngMallocFn = void*(__cdecl*)(PngStruct* png, std::uint32_t size);
using PngFreeFn = void(__cdecl*)(PngStruct* png, void* block);
using PngDataFn = void(__cdecl*)(PngStruct* png, std::uint8_t* data, std::uint32_t length);
using Crc32Fn = std::uint32_t(__cdecl*)(std::uint32_t crc, const std::uint8_t* buffer, std::uint32_t length);

template <typename Fn>
Fn At(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

}  // namespace calls
}  // namespace s3re::thirdparty
