#pragma once

// Raw byte access, for memory whose layout is genuinely unknown or
// unstructured (scratch buffers, byte arrays copied as a whole, test
// buffers). Everything with a known layout uses the typed structs in
// game/layout.h (the world and its records) or a small packed struct next
// to the code (other objects). Don't add new raw offsets for known fields.

#include <cstddef>
#include <cstdint>

namespace s3re::game {

// The T at `offset` bytes from `base`.
template <typename T>
T& RawAt(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

template <typename T>
const T& RawAt(const void* base, std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(base) + offset);
}

// The T at a computed address held as an integer (e.g. a pointer the game
// keeps in a dword). Not for globals: those are declared in game/globals.h,
// tables.h and texts.h (see game/global_access.h).
template <typename T>
T& RawAtAddress(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

// Byte distance of `field` from `base` (e.g. to compare one typed field of
// two test buffers).
inline std::size_t OffsetIn(const void* base, const void* field) {
    return static_cast<std::size_t>(static_cast<const std::uint8_t*>(field) - static_cast<const std::uint8_t*>(base));
}

}  // namespace s3re::game
