#pragma once

// Test helpers for functions that work on the (20+ MB) world object.
//
// WorldPair holds two identical world-sized buffers: the original function
// runs on one, our version on the other, and the buffers must stay
// identical. Filling 20 MB with random bytes for every case would be slow,
// so the buffers are filled once and the cases run on the evolving state;
// a full comparison runs every few cases (Checkpoint) and the test compares
// the bytes it expects to change after every case.

#include "game/raw.h"
#include "game/world.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::test {

// Zeroed, committed memory for a world buffer.
class WorldBuffer {
public:
    explicit WorldBuffer(std::size_t size)
        : size_(size),
          data_(static_cast<std::uint8_t*>(
              VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) {}
    ~WorldBuffer() { VirtualFree(data_, 0, MEM_RELEASE); }
    WorldBuffer(const WorldBuffer&) = delete;
    WorldBuffer& operator=(const WorldBuffer&) = delete;

    std::uint8_t* data() { return data_; }
    std::size_t size() const { return size_; }
    game::CGameData* world() { return reinterpret_cast<game::CGameData*>(data_); }

private:
    std::size_t size_;
    std::uint8_t* data_;
};

class WorldPair {
public:
    // Both buffers get the same random contents.
    WorldPair(Context& ctx, std::size_t size) : original_(size), ours_(size) {
        ctx.rng().Fill(original_.data(), size);
        std::memcpy(ours_.data(), original_.data(), size);
    }

    game::CGameData* original() { return original_.world(); }
    game::CGameData* ours() { return ours_.world(); }
    std::uint8_t* originalBytes() { return original_.data(); }
    std::uint8_t* oursBytes() { return ours_.data(); }
    std::size_t size() const { return original_.size(); }

    // Writes the same value into both buffers (setting up a case).
    template <typename T>
    void Set(std::size_t offset, T value) {
        std::memcpy(original_.data() + offset, &value, sizeof(T));
        std::memcpy(ours_.data() + offset, &value, sizeof(T));
    }

    // Compares `size` bytes at `offset`.
    bool Same(Context& ctx, std::size_t offset, std::size_t size) {
        return ctx.ExpectMemEq(original_.data() + offset, ours_.data() + offset, size,
                               "original world", "our world", __FILE__, __LINE__);
    }

    // Runs `f(world)` on both buffers (typed set-up of a case), e.g.
    // worlds.Both([&](game::CGameData* w) { w->map_size = size; });
    template <typename F>
    void Both(F f) {
        f(original());
        f(ours());
    }

    // Compares the field `select(world)` returns a reference to, e.g.
    // worlds.SameField(ctx, [&](game::CGameData* w) -> auto& { return w->tiles[x][y]; });
    template <typename F>
    bool SameField(Context& ctx, F select) {
        auto& field = select(original());
        return Same(ctx, game::OffsetIn(original(), &field), sizeof(field));
    }

    // Byte offset of the field `select(world)` returns a reference to.
    template <typename F>
    std::size_t OffsetOf(F select) {
        return game::OffsetIn(original(), &select(original()));
    }

    // Compares the whole buffers.
    bool Checkpoint(Context& ctx) { return Same(ctx, 0, size()); }

private:
    WorldBuffer original_;
    WorldBuffer ours_;
};

}  // namespace s3re::test
