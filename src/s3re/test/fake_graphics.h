#pragma once

// A stand-in for the game's graphics object (game::gfx::CGraphicsData, the
// pointer g_gfxData), for tests of simulation code that reads animation
// lengths through GfxData_SpriteWord ([spriteTables[group]->sprites[index]
// + views[group]]). While a FakeGraphics exists, the game's pointer points
// at it; the destructor restores the old pointer.
//
// Tests use groups 0..0xaf (the animation tables hold ids beyond the 70
// real groups; the original doesn't check), so the object buffer is larger
// than CGraphicsData. One sprite table (at the start of the data buffer)
// serves every group; sprite headers live in the second half of the data
// buffer.

#include "game/gfx_layout.h"
#include "game/globals.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::test {

class FakeGraphics {
public:
    static constexpr std::uint32_t kGroups = 0xb0;
    static constexpr std::uint32_t kSprites = 0x4000;
    static constexpr std::size_t kDataSize = 512 * 1024;
    static constexpr std::size_t kSpriteArea = kDataSize / 2;  // offset of the sprite headers in the data buffer

    FakeGraphics() : saved_(game::g_gfxData) { game::g_gfxData = &object(); }
    ~FakeGraphics() { game::g_gfxData = saved_; }
    FakeGraphics(const FakeGraphics&) = delete;
    FakeGraphics& operator=(const FakeGraphics&) = delete;

    game::gfx::CGraphicsData& object() { return *reinterpret_cast<game::gfx::CGraphicsData*>(object_.data()); }
    game::gfx::SpriteTable& table() { return *reinterpret_cast<game::gfx::SpriteTable*>(data_.data()); }
    std::uint8_t* sprites() { return data_.data() + kSpriteArea; }

    // Random object bytes, and `id` (group << 16 | index) a valid sprite of
    // the shared table (view 0) at a random place of the sprite area. False
    // (nothing changed) if `id` is not a graphics id.
    bool PrepareSprite(Context& ctx, std::uint32_t id) {
        const std::uint32_t group = id >> 16, index = id & 0xffff;
        if (group >= kGroups || index >= kSprites) {
            return false;
        }
        ctx.rng().Fill(object_.data(), object_.size());
        object().spriteTables[group] = &table();
        object().views[group] = 0;
        table().sprites[index] = Address(sprites() + ctx.rng().Below(0x1000));
        return true;
    }

    // Every group 0..0xaf uses the shared table with view 0.
    void ShareTable() {
        for (std::uint32_t g = 0; g < kGroups; ++g) {
            object().spriteTables[g] = &table();
            object().views[g] = 0;
        }
    }

    // Every sprite id of groups 0..0xaf valid: all sprites point at one
    // header of `fill` bytes.
    void MakeEverySpriteValid(std::uint8_t fill) {
        ShareTable();
        std::memset(sprites(), fill, 0x100);
        for (std::uint32_t k = 0; k < kSprites; ++k) {
            table().sprites[k] = Address(sprites());
        }
    }

private:
    static std::uint32_t Address(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

    std::vector<std::uint8_t> object_ = std::vector<std::uint8_t>(0x1000);
    std::vector<std::uint8_t> data_ = std::vector<std::uint8_t>(kDataSize);
    game::gfx::CGraphicsData* saved_;
};

}  // namespace s3re::test
