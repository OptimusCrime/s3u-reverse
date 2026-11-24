#pragma once

// The fields of the in-game drawer (CDrawer, Dirk\Draw.cpp; 0x5f700 bytes,
// base DirectX::CGameDrawer; re/ui_classes.md) that our code uses.
//
// While a frame is drawn, the drawing code (Render_sub_4a9960, 0x4c85a0,
// 0x4d3050, ...) does not blit sprites at once: it queues them per screen
// row ("row", 0..249) into three record pools, and 0x4b26c0 draws the
// rows in order afterwards (0x4b34d3..0x4b3664): first the shadows, then
// the sprites, each sprite followed by its player-colour layer. Every row
// is a chain of records that starts at a sentinel record (shadows and
// sprites: record `row`; colour layers: record `row` + 1, so record 0 means
// "none") and whose last record is always an empty one (tails[row]): a new
// entry is written into that empty record, which then links to a fresh
// empty record (number `used`). 0x4b26c0 resets the pools before a frame
// (tails[i] = i or i + 1, counts 0, used 250 / 250 / 251).

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

// A queued shadow (drawn with GfxDword::DrawShadow 0x5fcdc0).
struct ShadowRecord {
    std::uint32_t next;      // +0x00 next record of the row
    std::int32_t x, y;       // +0x04 screen position (read as words)
    std::uint32_t fraction;  // +0x0c visible-height fraction, 16.16 (0x10000 = all)
    std::uint32_t sprite;    // +0x10 the GfxDword (image)
};
static_assert(sizeof(ShadowRecord) == 0x14);

// A queued sprite (GfxDword::DrawSprite 0x5fadb0).
struct SpriteRecord {
    std::uint32_t next;          // +0x00
    std::int32_t x, y;           // +0x04
    std::uint32_t param;         // +0x0c passed to DrawSprite before the fraction (meaning not named yet)
    std::uint32_t fraction;      // +0x10 visible-height fraction, 16.16
    std::uint32_t colour_layer;  // +0x14 its ColourRecord, drawn right after it (0 = none)
    std::uint32_t sprite;        // +0x18 the GfxDword
};
static_assert(sizeof(SpriteRecord) == 0x1c);

// A queued player-colour layer (GfxDword::DrawColorIndexed 0x5fc5f0).
struct ColourRecord {
    std::uint32_t next;    // +0x00
    std::int32_t x, y;     // +0x04
    std::uint32_t param;   // +0x0c passed to DrawColorIndexed (probably the player's palette)
    std::uint32_t sprite;  // +0x10
};
static_assert(sizeof(ColourRecord) == 0x14);

constexpr std::size_t kDrawRows = 250;

template <typename Record, std::size_t Capacity>
struct DrawQueue {
    Record records[Capacity];
    std::uint32_t tails[kDrawRows];   // the empty last record of each row
    std::int32_t counts[kDrawRows];   // entries queued per row
    std::uint32_t used;               // records handed out so far (the queue is full at Capacity)
};

// Sounds of the surroundings (birds, sea, ...), per kind (17): sources
// counted while drawing, left and right of the screen centre, and in all
// (CDrawer::AmbientSoundPan / AmbientSoundVolume; 0x4c7730 plays them).
constexpr std::size_t kAmbientKinds = 17;

struct CDrawer {
    std::uint8_t unk000[0x90];
    std::int32_t pitch;                               // +0x090 bytes per row of the surfaces below
    std::uint8_t unk094[0xa4 - 0x94];
    std::uint16_t* pixels_a4;                         // +0x0a4 a locked 16-bit surface (statistics bars)
    std::uint8_t unk0a8[0xec - 0xa8];
    std::uint16_t* pixels_ec;                         // +0x0ec another one (debug text glyphs)
    std::uint8_t unk0f0[0x20c - 0xf0];
    DrawQueue<ShadowRecord, 4000> shadows;            // +0x00020c
    DrawQueue<SpriteRecord, 8000> sprites;            // +0x014260
    DrawQueue<ColourRecord, 4000> colour_layers;      // +0x04b534
    std::int32_t ambient_left[kAmbientKinds];         // +0x05f588
    std::int32_t ambient_right[kAmbientKinds];        // +0x05f5cc
    std::int32_t ambient_count[kAmbientKinds];        // +0x05f610
};

#pragma pack(pop)

static_assert(sizeof(void*) == 4, "32-bit build only");
static_assert(offsetof(CDrawer, pitch) == 0x90);
static_assert(offsetof(CDrawer, pixels_a4) == 0xa4);
static_assert(offsetof(CDrawer, pixels_ec) == 0xec);
static_assert(offsetof(CDrawer, shadows) == 0x20c);
static_assert(offsetof(CDrawer, shadows.tails) == 0x13a8c);
static_assert(offsetof(CDrawer, shadows.counts) == 0x13e74);
static_assert(offsetof(CDrawer, shadows.used) == 0x1425c);
static_assert(offsetof(CDrawer, sprites) == 0x14260);
static_assert(offsetof(CDrawer, sprites.tails) == 0x4ad60);
static_assert(offsetof(CDrawer, sprites.counts) == 0x4b148);
static_assert(offsetof(CDrawer, sprites.used) == 0x4b530);
static_assert(offsetof(CDrawer, colour_layers) == 0x4b534);
static_assert(offsetof(CDrawer, colour_layers.tails) == 0x5edb4);
static_assert(offsetof(CDrawer, colour_layers.counts) == 0x5f19c);
static_assert(offsetof(CDrawer, colour_layers.used) == 0x5f584);
static_assert(offsetof(CDrawer, ambient_left) == 0x5f588);
static_assert(offsetof(CDrawer, ambient_right) == 0x5f5cc);
static_assert(offsetof(CDrawer, ambient_count) == 0x5f610);

template <typename T>
CDrawer* DrawerOf(T* self) {
    return reinterpret_cast<CDrawer*>(self);
}

}  // namespace s3re::game
