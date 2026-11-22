#pragma once

// How our code names data in S3.EXE's image (globals, constant tables,
// strings). S3.EXE always loads at 0x400000, so every such object has a
// fixed address. Each one is declared exactly once, with its type, in
// game/globals.h (variables), game/tables.h (constant tables),
// game/texts.h (strings) or game/vtables.h (vtables and other
// compiler-generated class data); re/globals.csv records what is known
// about it. tools/typed_access_lint.py rejects data addresses anywhere
// else.
//
// Declarations look like
//
//   inline constexpr GameGlobal<std::uint32_t> g_gameTick{0x7dfd48};
//
// and are used like variables: `g_gameTick & 0x3f`, `g_gameTick = 0`,
// `g_settlerTypeClass[type]`, `g_aiWorld->tiles`, `&g_playerTallies`.
// The object only holds the address and is constant-initialized (no
// dynamic initializer), so it works in code the emulator runs, which
// never runs the DLL's startup code.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace s3re::game {

template <typename T>
class GameGlobal {
public:
    explicit constexpr GameGlobal(std::uintptr_t address) : address_(address) {}

    // Not copyable: so that passing a global to a variadic function (such
    // as DebugLog) or to a deducing template fails to compile instead of
    // passing this wrapper; write `.get()` there.
    GameGlobal(const GameGlobal&) = delete;
    GameGlobal& operator=(const GameGlobal&) = delete;

    // The object in S3.EXE's memory.
    T& get() const { return *reinterpret_cast<T*>(address_); }
    operator T&() const { return get(); }
    T* operator&() const { return reinterpret_cast<T*>(address_); }
    constexpr std::uintptr_t address() const { return address_; }

    // Member access: a struct global, or a global that holds a pointer.
    auto operator->() const {
        if constexpr (std::is_pointer_v<T>) {
            return get();
        } else {
            return &get();
        }
    }

    // Element access: array globals and pointer globals.
    template <typename I>
    decltype(auto) operator[](I index) const {
        return get()[index];
    }

    T& operator=(const std::remove_cv_t<T>& value) const { return get() = value; }
    template <typename U>
    T& operator+=(const U& value) const {
        return get() += value;
    }
    template <typename U>
    T& operator-=(const U& value) const {
        return get() -= value;
    }
    template <typename U>
    T& operator|=(const U& value) const {
        return get() |= value;
    }
    template <typename U>
    T& operator&=(const U& value) const {
        return get() &= value;
    }
    template <typename U>
    T& operator^=(const U& value) const {
        return get() ^= value;
    }
    T& operator++() const { return ++get(); }
    T& operator--() const { return --get(); }
    auto operator++(int) const { return get()++; }  // (auto: T may be an array type)
    auto operator--(int) const { return get()--; }

private:
    std::uintptr_t address_;
};

// A string in S3.EXE's data (DebugLog formats, texts, file names). Only
// its address matters: the game passes these pointers around, and our
// replacements pass the same pointers.
class GameText {
public:
    explicit constexpr GameText(std::uintptr_t address) : address_(address) {}

    const char* c_str() const { return reinterpret_cast<const char*>(address_); }
    operator const char*() const { return c_str(); }
    constexpr std::uintptr_t address() const { return address_; }

private:
    std::uintptr_t address_;
};

}  // namespace s3re::game
