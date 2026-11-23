#pragma once

// Value(E): an enum's underlying integer. Part of game/enums.h (the
// umbrella; see its header comment for the conventions of all enums).

#include <type_traits>

namespace s3re::game {

// The value of an enum as its underlying integer (table indexes, calls of
// original functions, comparisons with raw fields).
template <typename E>
constexpr std::underlying_type_t<E> Value(E value) {
    return static_cast<std::underlying_type_t<E>>(value);
}

}  // namespace s3re::game
