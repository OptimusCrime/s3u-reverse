// Lookups in the loaded graphics (Dirk\graphicsdata.cpp): the CGraphicsData
// table accessors, the GfxDword image-reference accessors, sequence frames
// and 16-bit points. Formats: re/graphics_formats.md.
//
// A GfxDword is one pointer to an image (or image list) in a mapped GFX
// file (constructor 0x5fc540). Image headers:
//   sprite (16-bit, colour-indexed, shadow): dword 0x0c, Point16 size +4,
//     Point16 offset +8, extra byte count +0xc, extra bytes from +0xd, RLE
//     rows at the next even address;
//   GUI image: size +0, offset +4, count +8, extra from +9;
//   landscape texture: size +0, count +4, extra from +5.
// Image list: dword 0x1402, word 8, byte, count byte +7, dword offsets
//   (relative to the list) from +8.
//
// GfxDword accessors (all thiscall):
//   PointAt4 (0x5f6430) (out), ret 4: *out = dword image+4 (sprite size); EAX = out.
//   SpriteRleData16 (0x5f6440): image + 0xd + byte image+0xc, rounded up to even.
//   IsValid (0x5f6460): image != 0 (EAX 0/1).
//   PointAt0 (0x5f6470) (out), ret 4: *out = dword image+0 (GUI/texture size); EAX = out.
//   GuiExtraBytes (0x5f6480): image + 9. GuiExtraCount (0x5f6490): byte image+8.
//   GuiRleData (0x5f64a0): GuiExtraBytes() + GuiExtraCount(), rounded up to even.
//   TextureExtraBytes (0x5fc550): image + 5. TextureExtraCount (0x5fc560): byte image+4.
//   TextureExtraByte (0x5fc570) (i), ret 4: AL = byte (TextureExtraBytes() + i); the
//     rest of EAX is the upper 24 bits of TextureExtraBytes() (reproduced).
//   TextureRleData (0x5fc580): TextureExtraBytes() + TextureExtraCount(), rounded up to even.
//   ListImage (0x5fc5a0) (out, index), ret 8: *out = list + offsets[index] (no check); EAX = out.
//   SpriteExtraBytes (0x5fc5d0): image + 0xd. SpriteExtraCount (0x5fc5e0): byte image+0xc.
//   ListImageChecked (0x5fcce0) (out, index), ret 8: like ListImage, but index
//     < 0 or >= the count byte +7 gives image 0.
//   SpriteRleData (0x5fcda0): SpriteExtraBytes() + SpriteExtraCount(), rounded up to even.
// CGraphicsData (thiscall; id = group << 16 | index):
//   GetPixelFormat (0x5fa730) (out), ret 4: copies the 32-byte DDPIXELFORMAT +4; EAX = out.
//   LanguageCount (0x5faa50): +0xc3c. SetLanguage (0x5faa60) (l), ret 4: +0xc40 = l; EAX = l.
//   ColorIndexedAnimationPointer (0x5fab50), ShadowAnimationPointer (0x5fabb0)
//     (out, id), ret 8: as AnimationPointer 0x5faaf0 with the tables +0x7dc /
//     +0x8f4 and their own "not found" DebugLog (index >= count; the entry
//     is still read); *out = view + offset; EAX = out.
//   PalettePointer (0x5fac10) (id, brightness), ret 8: view + offsets[index]
//     (offsets[0] if index >= count) + (brightness >> 5) * 0x200.
//   GuiImagePointerAt (0x5faca0) (out, id, add), ret 0xc: entry (index +
//     add) of the GUI image table, no check; EAX = out.
//   LandscapeTexturePointer (0x5face0) (out, id), ret 8: entry of the
//     texture table +0x6c4, no check; EAX = out.
//   GetSequenceFrame (0x5fad50) (out, id, frame), ret 0xc: the 24-byte frame
//     record (view + sequences[index] + 4 + frame * 24) resolved by the
//     SequenceFrame constructor into a stack temporary and copied (5 dwords)
//     to out; EAX = out.
// SequenceFrame (0x14 bytes: Point16 offset, body image, colour-indexed
//   image, shadow image, sound dword):
//   SequenceFrame_default (0x5fd7d0): offset (0, 0), the three images 0;
//     the sound (+0x10) is left alone; EAX = this.
//   SequenceFrame (0x5fd800) (gfx, record), ret 8: as the default, then the
//     offset dword from the record; for each id that isn't -1: the body
//     from AnimationPointer + frame word +0x10 (image 0 if the frame is past
//     the list's count), the colour-indexed image from
//     ColorIndexedAnimationPointer + ListImage (frame +0x10, no check), the
//     shadow from ShadowAnimationPointer + ListImageChecked (frame +0x12);
//     sound = record +0x14; EAX = this. Known difference: the original
//     wraps the three lookups in catch(...) handlers that log "GFX: Invalid
//     ... %u or frame %u" (DebugLog, a no-op in the release build) and
//     rethrow; ours has no handlers, so a fault while reading the tables
//     propagates the same way without the log line.
//   OffsetX (0x5fd980) / OffsetY (0x5fd990): AX = word +0 / +2 (the upper
//     half of EAX is the caller's; every caller sign-extends AX).
//   BodyImage, ColorIndexedImage, ShadowImage (0x5fd9a0, 0x5fd9b0, 0x5fd9c0)
//     (out), ret 4: *out = +4 / +8 / +0xc; EAX = out.
//   SoundId (0x5fd9d0): sound & 0xffffff, -1 stays -1.
//   SoundHighByte (0x5fd9f0): sound >> 24, -1 stays -1.
// Point16 (16-bit x, y):
//   Negate (0x5fdad0) (out), ret 4: *out = (-x, -y) built by the Point16
//     constructor 0x5fdab0 on a stack temporary; EAX = out.
//   Add (0x5fdb00) (out, other*), ret 8: *out = (x + other.x, y + other.y),
//     the same way; EAX = out.

#include "game/debug_log.h"
#include "game/graphics/gfx_layout.h"
#include "game/raw.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using gfx::CGraphicsData;

constexpr std::uintptr_t kGfxDwordConstruct = 0x005fc540;
constexpr std::uintptr_t kGuiExtraBytes = 0x005f6480;
constexpr std::uintptr_t kGuiExtraCount = 0x005f6490;
constexpr std::uintptr_t kTextureExtraBytes = 0x005fc550;
constexpr std::uintptr_t kTextureExtraCount = 0x005fc560;
constexpr std::uintptr_t kSpriteExtraBytes = 0x005fc5d0;
constexpr std::uintptr_t kSpriteExtraCount = 0x005fc5e0;
constexpr std::uintptr_t kListImage = 0x005fc5a0;
constexpr std::uintptr_t kListImageChecked = 0x005fcce0;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kColorIndexedAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kShadowAnimationPointer = 0x005fabb0;
constexpr std::uintptr_t kSequenceFrameConstruct = 0x005fd800;
constexpr std::uintptr_t kPoint16Construct = 0x005fdab0;

// "No image" in a sequence frame record.
constexpr std::uint32_t kNoImage = 0xffffffffu;
constexpr std::uint32_t kNoSound = 0xffffffffu;

// Each palette has 8 brightness levels of 256 16-bit colours.
constexpr std::uint32_t kPaletteLevelBytes = 0x200;

#pragma pack(push, 1)
// One reference to an image or image list (the GfxDword object).
struct GfxDword {
    std::uint32_t image;  // +0x00 address in a mapped GFX file
};

struct SpriteHeader {
    std::uint32_t headerSize;  // +0x00 0x0c
    std::uint32_t size;        // +0x04 Point16 width, height
    std::uint32_t offset;      // +0x08 Point16 x, y
    std::uint8_t extraCount;   // +0x0c
};
static_assert(offsetof(SpriteHeader, extraCount) == 0x0c);

struct GuiImageHeader {
    std::uint32_t size;        // +0x00 Point16 width, height
    std::uint32_t offset;      // +0x04 Point16 x, y
    std::uint8_t extraCount;   // +0x08
};
static_assert(offsetof(GuiImageHeader, extraCount) == 0x08);

struct TextureHeader {
    std::uint32_t size;        // +0x00 Point16 width, height
    std::uint8_t extraCount;   // +0x04
};
static_assert(offsetof(TextureHeader, extraCount) == 0x04);

struct ImageList {
    std::uint8_t unk00[7];     // dword 0x1402, word 8, byte 0
    std::uint8_t count;        // +0x07
    std::uint32_t offsets[1];  // +0x08 relative to the list
};
static_assert(offsetof(ImageList, offsets) == 0x08);

// A frame record of a sequence (24 bytes in the file).
struct SequenceRecord {
    std::uint32_t offset;          // +0x00 Point16
    std::uint32_t bodyId;          // +0x04 animation id (group << 16 | list) or -1
    std::uint32_t colorIndexedId;  // +0x08
    std::uint32_t shadowId;        // +0x0c
    std::uint16_t frame;           // +0x10 image in the body and colour-indexed lists
    std::uint16_t shadowFrame;     // +0x12
    std::uint32_t sound;           // +0x14 low 24 bits sound id, high byte a threshold; -1 none
};
static_assert(sizeof(SequenceRecord) == 24);

// A sequence: the frame count, then the frame records.
constexpr std::uint32_t kSequenceRecordsOffset = 4;

// A resolved sequence frame.
struct SequenceFrame {
    std::int16_t offsetX;           // +0x00
    std::int16_t offsetY;           // +0x02
    std::uint32_t body;             // +0x04 image address
    std::uint32_t colorIndexed;     // +0x08
    std::uint32_t shadow;           // +0x0c
    std::uint32_t sound;            // +0x10
};
static_assert(sizeof(SequenceFrame) == 0x14);

struct Point16 {
    std::uint16_t x;  // +0x00
    std::uint16_t y;  // +0x02
};
#pragma pack(pop)

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}
template <typename T>
const T* TypedAt(std::uint32_t address) {
    return reinterpret_cast<const T*>(address);
}

std::uint32_t RoundUpToEven(std::uint32_t address) {
    return (address & 1) ? address + 1 : address;
}

using Getter0Fn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Getter1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Getter2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Getter3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

template <typename F, typename... A>
std::uint32_t Call(std::uintptr_t address, void* self, A... args) {
    return reinterpret_cast<F>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// GfxDword::Construct (0x5fc540) on a temporary: the value it stores.
std::uint32_t Construct(std::uint32_t value) {
    GfxDword temporary;
    Call<Getter1Fn>(kGfxDwordConstruct, &temporary, value);
    return temporary.image;
}

// Point16::Point16 (0x5fdab0) on a temporary.
std::uint32_t MakePoint16(std::uint32_t x, std::uint32_t y) {
    std::uint32_t temporary;
    Call<Getter2Fn>(kPoint16Construct, &temporary, x, y);
    return temporary;
}

std::uint32_t& OutAt(std::uint32_t out) {
    return *reinterpret_cast<std::uint32_t*>(out);
}

}  // namespace

// ---------------------------------------------------------------------------
// GfxDword accessors
// ---------------------------------------------------------------------------

std::uint32_t __fastcall GfxDword_PointAt4(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall GfxDword_SpriteRleData16(void* self, void* edx);
std::uint32_t __fastcall GfxDword_IsValid(void* self, void* edx);
std::uint32_t __fastcall GfxDword_PointAt0(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall GfxDword_GuiExtraBytes(void* self, void* edx);
std::uint32_t __fastcall GfxDword_GuiExtraCount(void* self, void* edx);
std::uint32_t __fastcall GfxDword_GuiRleData(void* self, void* edx);
std::uint32_t __fastcall GfxDword_TextureExtraBytes(void* self, void* edx);
std::uint32_t __fastcall GfxDword_TextureExtraCount(void* self, void* edx);
std::uint32_t __fastcall GfxDword_TextureExtraByte(void* self, void* edx, std::uint32_t index);
std::uint32_t __fastcall GfxDword_TextureRleData(void* self, void* edx);
std::uint32_t __fastcall GfxDword_ListImage(void* self, void* edx, std::uint32_t out, std::uint32_t index);
std::uint32_t __fastcall GfxDword_SpriteExtraBytes(void* self, void* edx);
std::uint32_t __fastcall GfxDword_SpriteExtraCount(void* self, void* edx);
std::uint32_t __fastcall GfxDword_ListImageChecked(void* self, void* edx, std::uint32_t out, std::uint32_t index);
std::uint32_t __fastcall GfxDword_ListImageChecked_4b19c0(void* self, void* edx, std::uint32_t out,
                                                          std::uint32_t index);
std::uint32_t __fastcall GfxDword_Construct_609900(void* self, void* edx, std::uint32_t value);
std::uint32_t __fastcall GfxDword_SpriteRleData(void* self, void* edx);

S3RE_HOOK(0x005f6430, GfxDword_PointAt4, Getter1Fn, Original_GfxDword_PointAt4, GfxDword_PointAt4);

// Purpose: a sprite's size (width, height) for drawing and clipping.
std::uint32_t __fastcall GfxDword_PointAt4(void* self, void* /*edx*/, std::uint32_t out) {
    OutAt(out) = TypedAt<SpriteHeader>(As<GfxDword>(self)->image)->size;
    return out;
}

S3RE_HOOK(0x005f6440, GfxDword_SpriteRleData16, Getter0Fn, Original_GfxDword_SpriteRleData16,
          GfxDword_SpriteRleData16);

// Purpose: where a 16-bit sprite's RLE rows start (after its header and
// extra bytes).
std::uint32_t __fastcall GfxDword_SpriteRleData16(void* self, void* /*edx*/) {
    const std::uint32_t image = As<GfxDword>(self)->image;
    return RoundUpToEven(image + sizeof(SpriteHeader) + TypedAt<SpriteHeader>(image)->extraCount);
}

S3RE_HOOK(0x005f6460, GfxDword_IsValid, Getter0Fn, Original_GfxDword_IsValid, GfxDword_IsValid);

// Purpose: whether an image reference is set.
std::uint32_t __fastcall GfxDword_IsValid(void* self, void* /*edx*/) {
    return As<GfxDword>(self)->image != 0 ? 1 : 0;
}

S3RE_HOOK(0x005f6470, GfxDword_PointAt0, Getter1Fn, Original_GfxDword_PointAt0, GfxDword_PointAt0);

// Purpose: a GUI image's or texture's size (width, height).
std::uint32_t __fastcall GfxDword_PointAt0(void* self, void* /*edx*/, std::uint32_t out) {
    OutAt(out) = TypedAt<GuiImageHeader>(As<GfxDword>(self)->image)->size;
    return out;
}

S3RE_HOOK(0x005f6480, GfxDword_GuiExtraBytes, Getter0Fn, Original_GfxDword_GuiExtraBytes, GfxDword_GuiExtraBytes);

// Purpose: the extra bytes of a GUI image (after its header).
std::uint32_t __fastcall GfxDword_GuiExtraBytes(void* self, void* /*edx*/) {
    return As<GfxDword>(self)->image + sizeof(GuiImageHeader);
}

S3RE_HOOK(0x005f6490, GfxDword_GuiExtraCount, Getter0Fn, Original_GfxDword_GuiExtraCount, GfxDword_GuiExtraCount);

// Purpose: the number of extra bytes of a GUI image.
std::uint32_t __fastcall GfxDword_GuiExtraCount(void* self, void* /*edx*/) {
    return TypedAt<GuiImageHeader>(As<GfxDword>(self)->image)->extraCount;
}

S3RE_HOOK(0x005f64a0, GfxDword_GuiRleData, Getter0Fn, Original_GfxDword_GuiRleData, GfxDword_GuiRleData);

// Purpose: where a GUI image's RLE rows start.
std::uint32_t __fastcall GfxDword_GuiRleData(void* self, void* /*edx*/) {
    const std::uint32_t extra = Call<Getter0Fn>(kGuiExtraBytes, self);
    return RoundUpToEven(extra + Call<Getter0Fn>(kGuiExtraCount, self));
}

S3RE_HOOK(0x005fc550, GfxDword_TextureExtraBytes, Getter0Fn, Original_GfxDword_TextureExtraBytes,
          GfxDword_TextureExtraBytes);

// Purpose: the extra bytes of a landscape texture (after its header).
std::uint32_t __fastcall GfxDword_TextureExtraBytes(void* self, void* /*edx*/) {
    return As<GfxDword>(self)->image + sizeof(TextureHeader);
}

S3RE_HOOK(0x005fc560, GfxDword_TextureExtraCount, Getter0Fn, Original_GfxDword_TextureExtraCount,
          GfxDword_TextureExtraCount);

// Purpose: the number of extra bytes of a landscape texture.
std::uint32_t __fastcall GfxDword_TextureExtraCount(void* self, void* /*edx*/) {
    return TypedAt<TextureHeader>(As<GfxDword>(self)->image)->extraCount;
}

S3RE_HOOK(0x005fc570, GfxDword_TextureExtraByte, Getter1Fn, Original_GfxDword_TextureExtraByte,
          GfxDword_TextureExtraByte);

// Purpose: one extra byte of a landscape texture (the landscape code reads
// them one by one, probably colour or blending information).
std::uint32_t __fastcall GfxDword_TextureExtraByte(void* self, void* /*edx*/, std::uint32_t index) {
    const std::uint32_t extra = Call<Getter0Fn>(kTextureExtraBytes, self);
    // AL is the byte; the rest of EAX is left from the pointer, as the original.
    return (extra & 0xffffff00u) | *TypedAt<std::uint8_t>(extra + index);
}

S3RE_HOOK(0x005fc580, GfxDword_TextureRleData, Getter0Fn, Original_GfxDword_TextureRleData,
          GfxDword_TextureRleData);

// Purpose: where a landscape texture's RLE rows start.
std::uint32_t __fastcall GfxDword_TextureRleData(void* self, void* /*edx*/) {
    const std::uint32_t extra = Call<Getter0Fn>(kTextureExtraBytes, self);
    return RoundUpToEven(extra + Call<Getter0Fn>(kTextureExtraCount, self));
}

S3RE_HOOK(0x005fc5a0, GfxDword_ListImage, Getter2Fn, Original_GfxDword_ListImage, GfxDword_ListImage);

// Purpose: picks one image of an image list (no range check).
std::uint32_t __fastcall GfxDword_ListImage(void* self, void* /*edx*/, std::uint32_t out, std::uint32_t index) {
    const std::uint32_t list = As<GfxDword>(self)->image;
    OutAt(out) = Construct(list + TypedAt<ImageList>(list)->offsets[index]);
    return out;
}

S3RE_HOOK(0x005fc5d0, GfxDword_SpriteExtraBytes, Getter0Fn, Original_GfxDword_SpriteExtraBytes,
          GfxDword_SpriteExtraBytes);

// Purpose: the extra bytes of a sprite (after its header).
std::uint32_t __fastcall GfxDword_SpriteExtraBytes(void* self, void* /*edx*/) {
    return As<GfxDword>(self)->image + sizeof(SpriteHeader);
}

S3RE_HOOK(0x005fc5e0, GfxDword_SpriteExtraCount, Getter0Fn, Original_GfxDword_SpriteExtraCount,
          GfxDword_SpriteExtraCount);

// Purpose: the number of extra bytes of a sprite.
std::uint32_t __fastcall GfxDword_SpriteExtraCount(void* self, void* /*edx*/) {
    return TypedAt<SpriteHeader>(As<GfxDword>(self)->image)->extraCount;
}

S3RE_HOOK(0x005fcce0, GfxDword_ListImageChecked, Getter2Fn, Original_GfxDword_ListImageChecked,
          GfxDword_ListImageChecked);

// Purpose: picks one image of an image list, image 0 if the index is out
// of range.
std::uint32_t __fastcall GfxDword_ListImageChecked(void* self, void* /*edx*/, std::uint32_t out,
                                                   std::uint32_t index) {
    const std::uint32_t list = As<GfxDword>(self)->image;
    const std::int32_t signedIndex = static_cast<std::int32_t>(index);
    const bool inRange = signedIndex >= 0 && signedIndex < static_cast<std::int32_t>(TypedAt<ImageList>(list)->count);
    OutAt(out) = Construct(list + TypedAt<ImageList>(list)->offsets[inRange ? index : 0]);
    return out;
}

S3RE_HOOK(0x004b19c0, GfxDword_ListImageChecked_4b19c0, Getter2Fn, Original_GfxDword_ListImageChecked_4b19c0,
          GfxDword_ListImageChecked_4b19c0);

// Purpose: the same as ListImageChecked, a copy in Volker\DrawBuilding.cpp's
// object file that stores the image without the GfxDword constructor (the
// building, settler and frame drawers call it).
std::uint32_t __fastcall GfxDword_ListImageChecked_4b19c0(void* self, void* /*edx*/, std::uint32_t out,
                                                          std::uint32_t index) {
    const std::uint32_t list = As<GfxDword>(self)->image;
    const std::int32_t signedIndex = static_cast<std::int32_t>(index);
    const bool inRange = signedIndex >= 0 && signedIndex < static_cast<std::int32_t>(TypedAt<ImageList>(list)->count);
    OutAt(out) = list + TypedAt<ImageList>(list)->offsets[inRange ? index : 0];
    return out;
}

S3RE_HOOK(0x00609900, GfxDword_Construct_609900, Getter1Fn, Original_GfxDword_Construct_609900,
          GfxDword_Construct_609900);

// Purpose: a copy of GfxDword::Construct (0x5fc540; +0 = value, EAX = this)
// in another object file; the building drawer 0x4a9960 and the lobby
// 0x6042e0 call it.
std::uint32_t __fastcall GfxDword_Construct_609900(void* self, void* /*edx*/, std::uint32_t value) {
    As<GfxDword>(self)->image = value;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005fcda0, GfxDword_SpriteRleData, Getter0Fn, Original_GfxDword_SpriteRleData, GfxDword_SpriteRleData);

// Purpose: where a colour-indexed or shadow sprite's RLE rows start.
std::uint32_t __fastcall GfxDword_SpriteRleData(void* self, void* /*edx*/) {
    const std::uint32_t extra = Call<Getter0Fn>(kSpriteExtraBytes, self);
    return RoundUpToEven(extra + Call<Getter0Fn>(kSpriteExtraCount, self));
}

// ---------------------------------------------------------------------------
// CGraphicsData accessors
// ---------------------------------------------------------------------------

std::uint32_t __fastcall CGraphicsData_GetPixelFormat(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall CGraphicsData_LanguageCount(void* self, void* edx);
std::uint32_t __fastcall CGraphicsData_SetLanguage(void* self, void* edx, std::uint32_t language);
std::uint32_t __fastcall CGraphicsData_ColorIndexedAnimationPointer(void* self, void* edx, std::uint32_t out,
                                                                    std::uint32_t id);
std::uint32_t __fastcall CGraphicsData_ShadowAnimationPointer(void* self, void* edx, std::uint32_t out,
                                                              std::uint32_t id);
std::uint32_t __fastcall CGraphicsData_PalettePointer(void* self, void* edx, std::uint32_t id,
                                                      std::uint32_t brightness);
std::uint32_t __fastcall CGraphicsData_GuiImagePointerAt(void* self, void* edx, std::uint32_t out, std::uint32_t id,
                                                         std::uint32_t add);
std::uint32_t __fastcall CGraphicsData_LandscapeTexturePointer(void* self, void* edx, std::uint32_t out,
                                                               std::uint32_t id);
std::uint32_t __fastcall CGraphicsData_GetSequenceFrame(void* self, void* edx, std::uint32_t out, std::uint32_t id,
                                                        std::uint32_t frame);

namespace {

std::uint32_t GroupOf(std::uint32_t id) {
    return id >> 16;
}
std::uint32_t IndexOf(std::uint32_t id) {
    return id & 0xffff;
}

// ColorIndexedAnimationPointer / ShadowAnimationPointer: the image list's
// address, after the (only logged) range check.
std::uint32_t CheckedListPointer(const CGraphicsData* gfx, const gfx::OffsetTable* const (&tables)[gfx::kGroups],
                                 const GameText& notFound, std::uint32_t id) {
    const std::uint32_t group = GroupOf(id);
    const std::uint32_t index = IndexOf(id);
    if (index >= tables[group]->count) {
        GameDebugLog(notFound, group, index);
    }
    // Read again after the call, as the original does.
    return Construct(tables[group]->offsets[index] + gfx->views[group]);
}

}  // namespace

S3RE_HOOK(0x005fa730, CGraphicsData_GetPixelFormat, Getter1Fn, Original_CGraphicsData_GetPixelFormat,
          CGraphicsData_GetPixelFormat);

// Purpose: the pixel format the graphics files were loaded for.
std::uint32_t __fastcall CGraphicsData_GetPixelFormat(void* self, void* /*edx*/, std::uint32_t out) {
    std::memcpy(reinterpret_cast<void*>(out), As<CGraphicsData>(self)->pixelFormat,
                sizeof(CGraphicsData::pixelFormat));
    return out;
}

S3RE_HOOK(0x005faa50, CGraphicsData_LanguageCount, Getter0Fn, Original_CGraphicsData_LanguageCount,
          CGraphicsData_LanguageCount);

// Purpose: how many languages the loaded text files have.
std::uint32_t __fastcall CGraphicsData_LanguageCount(void* self, void* /*edx*/) {
    return As<CGraphicsData>(self)->languageCount;
}

S3RE_HOOK(0x005faa60, CGraphicsData_SetLanguage, Getter1Fn, Original_CGraphicsData_SetLanguage,
          CGraphicsData_SetLanguage);

// Purpose: chooses the language of the game texts.
std::uint32_t __fastcall CGraphicsData_SetLanguage(void* self, void* /*edx*/, std::uint32_t language) {
    As<CGraphicsData>(self)->language = language;
    return language;
}

S3RE_HOOK(0x005fab50, CGraphicsData_ColorIndexedAnimationPointer, Getter2Fn,
          Original_CGraphicsData_ColorIndexedAnimationPointer, CGraphicsData_ColorIndexedAnimationPointer);

// Purpose: the image list of an animation's player-colour layer.
std::uint32_t __fastcall CGraphicsData_ColorIndexedAnimationPointer(void* self, void* /*edx*/, std::uint32_t out,
                                                                    std::uint32_t id) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    OutAt(out) = CheckedListPointer(gfx, gfx->colorIndexedTables, texts::kColorIndexedAnimationNotFound, id);
    return out;
}

S3RE_HOOK(0x005fabb0, CGraphicsData_ShadowAnimationPointer, Getter2Fn, Original_CGraphicsData_ShadowAnimationPointer,
          CGraphicsData_ShadowAnimationPointer);

// Purpose: the image list of an animation's shadows.
std::uint32_t __fastcall CGraphicsData_ShadowAnimationPointer(void* self, void* /*edx*/, std::uint32_t out,
                                                              std::uint32_t id) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    OutAt(out) = CheckedListPointer(gfx, gfx->shadowTables, texts::kShadowAnimationNotFound, id);
    return out;
}

S3RE_HOOK(0x005fac10, CGraphicsData_PalettePointer, Getter2Fn, Original_CGraphicsData_PalettePointer,
          CGraphicsData_PalettePointer);

// Purpose: the 256 colours of a palette at a brightness (for the
// colour-indexed player-colour layers).
std::uint32_t __fastcall CGraphicsData_PalettePointer(void* self, void* /*edx*/, std::uint32_t id,
                                                      std::uint32_t brightness) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    const std::uint32_t group = GroupOf(id);
    std::uint32_t index = IndexOf(id);
    const gfx::PaletteTable* const table = gfx->paletteTables[group];
    if (index >= table->count) {
        index = 0;
    }
    return table->offsets[index] + gfx->views[group] + (brightness >> 5) * kPaletteLevelBytes;
}

S3RE_HOOK(0x005faca0, CGraphicsData_GuiImagePointerAt, Getter3Fn, Original_CGraphicsData_GuiImagePointerAt,
          CGraphicsData_GuiImagePointerAt);

// Purpose: a GUI image a given number of entries after another (e.g. the
// states of a button).
std::uint32_t __fastcall CGraphicsData_GuiImagePointerAt(void* self, void* /*edx*/, std::uint32_t out,
                                                         std::uint32_t id, std::uint32_t add) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    const std::uint32_t group = GroupOf(id);
    OutAt(out) = gfx->guiImageTables[group]->offsets[IndexOf(id) + add] + gfx->views[group];
    return out;
}

S3RE_HOOK(0x005face0, CGraphicsData_LandscapeTexturePointer, Getter2Fn,
          Original_CGraphicsData_LandscapeTexturePointer, CGraphicsData_LandscapeTexturePointer);

// Purpose: a landscape texture (128 x 128 tile graphics).
std::uint32_t __fastcall CGraphicsData_LandscapeTexturePointer(void* self, void* /*edx*/, std::uint32_t out,
                                                               std::uint32_t id) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    const std::uint32_t group = GroupOf(id);
    OutAt(out) = Construct(gfx->landscapeTextureTables[group]->offsets[IndexOf(id)] + gfx->views[group]);
    return out;
}

S3RE_HOOK(0x005fad50, CGraphicsData_GetSequenceFrame, Getter3Fn, Original_CGraphicsData_GetSequenceFrame,
          CGraphicsData_GetSequenceFrame);

// Purpose: one frame of an animation sequence (a settler's walk or work
// cycle): its offset, body, player-colour and shadow images and sound.
std::uint32_t __fastcall CGraphicsData_GetSequenceFrame(void* self, void* /*edx*/, std::uint32_t out,
                                                        std::uint32_t id, std::uint32_t frame) {
    const CGraphicsData* const gfx = As<CGraphicsData>(self);
    const std::uint32_t group = GroupOf(id);
    const std::uint32_t sequence = gfx->sequenceTables[group]->sequences[IndexOf(id)] + gfx->views[group];
    const std::uint32_t record = sequence + kSequenceRecordsOffset + frame * sizeof(SequenceRecord);
    SequenceFrame temporary;
    const auto* const built = reinterpret_cast<const SequenceFrame*>(
        Call<Getter2Fn>(kSequenceFrameConstruct, &temporary, reinterpret_cast<std::uint32_t>(self), record));
    std::memcpy(reinterpret_cast<void*>(out), built, sizeof(SequenceFrame));
    return out;
}

// ---------------------------------------------------------------------------
// SequenceFrame and Point16
// ---------------------------------------------------------------------------

std::uint32_t __fastcall SequenceFrame_SequenceFrame_default(void* self, void* edx);
std::uint32_t __fastcall SequenceFrame_SequenceFrame(void* self, void* edx, std::uint32_t gfx, std::uint32_t record);
std::uint32_t __fastcall SequenceFrame_OffsetX(void* self, void* edx);
std::uint32_t __fastcall SequenceFrame_OffsetY(void* self, void* edx);
std::uint32_t __fastcall SequenceFrame_BodyImage(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall SequenceFrame_ColorIndexedImage(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall SequenceFrame_ShadowImage(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall SequenceFrame_SoundId(void* self, void* edx);
std::uint32_t __fastcall SequenceFrame_SoundHighByte(void* self, void* edx);
std::uint32_t __fastcall Point16_Negate(void* self, void* edx, std::uint32_t out);
std::uint32_t __fastcall Point16_Add(void* self, void* edx, std::uint32_t out, std::uint32_t other);

namespace {

// The part both constructors share (0x5fd7d0, 0x5fd823..0x5fd83c).
void ClearSequenceFrame(SequenceFrame* frame) {
    Call<Getter2Fn>(kPoint16Construct, frame, 0, 0);
    frame->body = 0;
    Call<Getter1Fn>(kGfxDwordConstruct, &frame->colorIndexed, 0);
    Call<Getter1Fn>(kGfxDwordConstruct, &frame->shadow, 0);
}

}  // namespace

S3RE_HOOK(0x005fd7d0, SequenceFrame_SequenceFrame_default, Getter0Fn, Original_SequenceFrame_SequenceFrame_default,
          SequenceFrame_SequenceFrame_default);

// Purpose: an empty sequence frame (no images).
std::uint32_t __fastcall SequenceFrame_SequenceFrame_default(void* self, void* /*edx*/) {
    ClearSequenceFrame(As<SequenceFrame>(self));
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005fd800, SequenceFrame_SequenceFrame, Getter2Fn, Original_SequenceFrame_SequenceFrame,
          SequenceFrame_SequenceFrame);

// Purpose: resolves a frame record of an animation sequence into the
// images to draw (body, player colour, shadow) and its sound.
std::uint32_t __fastcall SequenceFrame_SequenceFrame(void* self, void* /*edx*/, std::uint32_t gfx,
                                                     std::uint32_t recordAddress) {
    SequenceFrame* const frame = As<SequenceFrame>(self);
    void* const graphics = reinterpret_cast<void*>(gfx);
    ClearSequenceFrame(frame);
    const SequenceRecord* const record = TypedAt<SequenceRecord>(recordAddress);
    std::memcpy(frame, &record->offset, sizeof(record->offset));
    if (record->bodyId != kNoImage) {
        const std::uint32_t index = record->frame;
        GfxDword list;
        const auto* const listRef = reinterpret_cast<const GfxDword*>(
            Call<Getter2Fn>(kAnimationPointer, graphics, reinterpret_cast<std::uint32_t>(&list), record->bodyId));
        const ImageList* const images = TypedAt<ImageList>(listRef->image);
        // Inlined ListImageChecked (the index is a zero-extended word, never negative).
        const std::uint32_t chosen = index < images->count ? index : 0;
        frame->body = listRef->image + images->offsets[chosen];
    }
    if (record->colorIndexedId != kNoImage) {
        GfxDword list;
        GfxDword image;
        const auto listRef = Call<Getter2Fn>(kColorIndexedAnimationPointer, graphics,
                                             reinterpret_cast<std::uint32_t>(&list), record->colorIndexedId);
        const auto* const result = reinterpret_cast<const GfxDword*>(Call<Getter2Fn>(
            kListImage, reinterpret_cast<void*>(listRef), reinterpret_cast<std::uint32_t>(&image), record->frame));
        frame->colorIndexed = result->image;
    }
    if (record->shadowId != kNoImage) {
        GfxDword list;
        GfxDword image;
        const auto listRef = Call<Getter2Fn>(kShadowAnimationPointer, graphics, reinterpret_cast<std::uint32_t>(&list),
                                             record->shadowId);
        const auto* const result = reinterpret_cast<const GfxDword*>(
            Call<Getter2Fn>(kListImageChecked, reinterpret_cast<void*>(listRef),
                            reinterpret_cast<std::uint32_t>(&image), record->shadowFrame));
        frame->shadow = result->image;
    }
    frame->sound = record->sound;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x005fd980, SequenceFrame_OffsetX, Getter0Fn, Original_SequenceFrame_OffsetX, SequenceFrame_OffsetX);

// Purpose: the x offset of a sequence frame (where to draw it relative to
// the settler).
std::uint32_t __fastcall SequenceFrame_OffsetX(void* self, void* /*edx*/) {
    // Only AX is defined (callers sign-extend it).
    return static_cast<std::uint16_t>(As<SequenceFrame>(self)->offsetX);
}

S3RE_HOOK(0x005fd990, SequenceFrame_OffsetY, Getter0Fn, Original_SequenceFrame_OffsetY, SequenceFrame_OffsetY);

// Purpose: the y offset of a sequence frame.
std::uint32_t __fastcall SequenceFrame_OffsetY(void* self, void* /*edx*/) {
    return static_cast<std::uint16_t>(As<SequenceFrame>(self)->offsetY);
}

S3RE_HOOK(0x005fd9a0, SequenceFrame_BodyImage, Getter1Fn, Original_SequenceFrame_BodyImage, SequenceFrame_BodyImage);

// Purpose: the body image of a sequence frame.
std::uint32_t __fastcall SequenceFrame_BodyImage(void* self, void* /*edx*/, std::uint32_t out) {
    OutAt(out) = As<SequenceFrame>(self)->body;
    return out;
}

S3RE_HOOK(0x005fd9b0, SequenceFrame_ColorIndexedImage, Getter1Fn, Original_SequenceFrame_ColorIndexedImage,
          SequenceFrame_ColorIndexedImage);

// Purpose: the player-colour image of a sequence frame.
std::uint32_t __fastcall SequenceFrame_ColorIndexedImage(void* self, void* /*edx*/, std::uint32_t out) {
    OutAt(out) = As<SequenceFrame>(self)->colorIndexed;
    return out;
}

S3RE_HOOK(0x005fd9c0, SequenceFrame_ShadowImage, Getter1Fn, Original_SequenceFrame_ShadowImage,
          SequenceFrame_ShadowImage);

// Purpose: the shadow image of a sequence frame.
std::uint32_t __fastcall SequenceFrame_ShadowImage(void* self, void* /*edx*/, std::uint32_t out) {
    OutAt(out) = As<SequenceFrame>(self)->shadow;
    return out;
}

S3RE_HOOK(0x005fd9d0, SequenceFrame_SoundId, Getter0Fn, Original_SequenceFrame_SoundId, SequenceFrame_SoundId);

// Purpose: the sound a sequence frame plays (-1: none).
std::uint32_t __fastcall SequenceFrame_SoundId(void* self, void* /*edx*/) {
    const std::uint32_t sound = As<SequenceFrame>(self)->sound;
    return sound == kNoSound ? kNoSound : sound & 0xffffff;
}

S3RE_HOOK(0x005fd9f0, SequenceFrame_SoundHighByte, Getter0Fn, Original_SequenceFrame_SoundHighByte,
          SequenceFrame_SoundHighByte);

// Purpose: the high byte of a sequence frame's sound (probably a chance or
// distance threshold for playing it; -1: none).
std::uint32_t __fastcall SequenceFrame_SoundHighByte(void* self, void* /*edx*/) {
    const std::uint32_t sound = As<SequenceFrame>(self)->sound;
    return sound == kNoSound ? kNoSound : sound >> 24;
}

S3RE_HOOK(0x005fdad0, Point16_Negate, Getter1Fn, Original_Point16_Negate, Point16_Negate);

// Purpose: the negated point (e.g. to undo a drawing offset).
std::uint32_t __fastcall Point16_Negate(void* self, void* /*edx*/, std::uint32_t out) {
    const Point16* const point = As<Point16>(self);
    OutAt(out) = MakePoint16(static_cast<std::uint16_t>(0 - point->x), static_cast<std::uint16_t>(0 - point->y));
    return out;
}

S3RE_HOOK(0x005fdb00, Point16_Add, Getter2Fn, Original_Point16_Add, Point16_Add);

// Purpose: the sum of two points (a position plus an offset).
std::uint32_t __fastcall Point16_Add(void* self, void* /*edx*/, std::uint32_t out, std::uint32_t otherAddress) {
    const Point16* const point = As<Point16>(self);
    const Point16* const other = TypedAt<Point16>(otherAddress);
    OutAt(out) = MakePoint16(static_cast<std::uint16_t>(other->x + point->x),
                             static_cast<std::uint16_t>(other->y + point->y));
    return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Inputs live in one shared random buffer: a CGraphicsData object at its
// start whose 70 groups point at tables inside the buffer (offsets chosen so
// that every looked-up address lands inside it), image lists, images and
// sequence records. Each function runs once as the original and once as
// ours with separate output areas (an object to write into plus guard
// bytes), compared with EAX (relative to the output area where it is an
// address there) and the DebugLog calls.

namespace {

constexpr std::uint32_t kDataBytes = 0x20000;
constexpr std::uint32_t kTableArea = 0x1000;     // tables: kTableArea + group * kTableBytes
constexpr std::uint32_t kTableBytes = 0x200;
constexpr std::uint32_t kListArea = 0x10000;     // image lists and images
constexpr std::uint32_t kTableEntries = 64;

alignas(16) std::uint8_t g_data[kDataBytes];

std::uint32_t Address(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

// An image list at `at`: a count and offsets (relative to the list) of
// images in the list area.
void WriteList(test::Context& ctx, std::uint32_t at) {
    auto* const list = reinterpret_cast<ImageList*>(g_data + at);
    list->count = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(40));
    for (std::uint32_t i = 0; i < 48; ++i) {
        list->offsets[i] = 0x200 + ctx.rng().Below(0x1000);
    }
}

// The shared inputs: the gfx object with every group's views (the base of
// g_data, sometimes another value for the arithmetic) and tables.
void BuildData(test::Context& ctx) {
    ctx.rng().Fill(g_data, sizeof(g_data));
    auto* const gfx = reinterpret_cast<CGraphicsData*>(g_data);
    for (std::uint32_t group = 0; group < gfx::kGroups; ++group) {
        gfx->views[group] = Address(g_data);
        const std::uint32_t table = kTableArea + group * kTableBytes;
        auto* const offsets = reinterpret_cast<gfx::OffsetTable*>(g_data + table);
        offsets->count = static_cast<std::uint16_t>(ctx.rng().Below(kTableEntries + 4));
        // Offsets of lists (each 0x200 bytes apart) in the list area.
        for (std::uint32_t i = 0; i < kTableEntries + 8; ++i) {
            reinterpret_cast<std::uint32_t*>(g_data + table + 8)[i] =
                kListArea + 0x100 * ctx.rng().Below(0x80) + 2 * ctx.rng().Below(8);
        }
        const auto* const asOffsets = reinterpret_cast<const gfx::OffsetTable*>(g_data + table);
        gfx->textTables[group] = reinterpret_cast<const gfx::TextTable*>(asOffsets);
        gfx->animationTables[group] = asOffsets;
        gfx->guiImageTables[group] = asOffsets;
        gfx->landscapeTextureTables[group] = asOffsets;
        gfx->colorIndexedTables[group] = asOffsets;
        gfx->shadowTables[group] = asOffsets;
        gfx->paletteTables[group] = reinterpret_cast<const gfx::PaletteTable*>(asOffsets);
        gfx->sequenceTables[group] = reinterpret_cast<const gfx::SequenceTable*>(asOffsets);
    }
    // Every 0x100-aligned list position holds a list (the images of a list
    // are anywhere in the list area: random bytes).
    for (std::uint32_t at = kListArea; at < kListArea + 0x80 * 0x100; at += 0x100) {
        WriteList(ctx, at);
    }
}

// Sequence records: ids of animations (group 0..69, index below 64) or -1.
std::uint32_t RandomAnimationId(test::Context& ctx) {
    if (ctx.rng().Chance(1, 4)) {
        return kNoImage;
    }
    return ctx.rng().Below(gfx::kGroups) << 16 | ctx.rng().Below(kTableEntries + 2);
}

void WriteSequenceRecord(test::Context& ctx, std::uint32_t at) {
    auto* const record = reinterpret_cast<SequenceRecord*>(g_data + at);
    record->offset = ctx.rng().Next32();
    record->bodyId = RandomAnimationId(ctx);
    record->colorIndexedId = RandomAnimationId(ctx);
    record->shadowId = RandomAnimationId(ctx);
    record->frame = static_cast<std::uint16_t>(ctx.rng().Below(48));
    record->shadowFrame = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(48));
    record->sound = ctx.rng().Chance(1, 3) ? kNoSound : ctx.rng().Next32();
}

struct OutputArea {
    std::uint8_t bytes[0x40];
};
OutputArea g_outA;
OutputArea g_outB;

// Calls a function of one of the families below on (self, out, a, b).
struct Candidate {
    Getter0Fn g0;
    Getter1Fn g1;
    Getter2Fn g2;
    Getter3Fn g3;
};

std::uint32_t Invoke(const Candidate& f, void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (f.g0) {
        return f.g0(self, nullptr);
    }
    if (f.g1) {
        return f.g1(self, nullptr, a);
    }
    if (f.g2) {
        return f.g2(self, nullptr, a, b);
    }
    return f.g3(self, nullptr, a, b, c);
}

enum class Self { Image, Gfx, Frame, Point };
enum class Arg { None, Out, Index, Id, Brightness, Record, Any, Point };

struct Case {
    const char* name;
    Candidate original;
    Candidate ours;
    Self self;
    Arg args[3];
};

#define S3RE_G0(f) Candidate{Original_##f, nullptr, nullptr, nullptr}, Candidate{f, nullptr, nullptr, nullptr}
#define S3RE_G1(f) Candidate{nullptr, Original_##f, nullptr, nullptr}, Candidate{nullptr, f, nullptr, nullptr}
#define S3RE_G2(f) Candidate{nullptr, nullptr, Original_##f, nullptr}, Candidate{nullptr, nullptr, f, nullptr}
#define S3RE_G3(f) Candidate{nullptr, nullptr, nullptr, Original_##f}, Candidate{nullptr, nullptr, nullptr, f}

std::vector<Case> AllCases() {
    return {
        {"PointAt4", S3RE_G1(GfxDword_PointAt4), Self::Image, {Arg::Out}},
        {"SpriteRleData16", S3RE_G0(GfxDword_SpriteRleData16), Self::Image, {}},
        {"IsValid", S3RE_G0(GfxDword_IsValid), Self::Image, {}},
        {"PointAt0", S3RE_G1(GfxDword_PointAt0), Self::Image, {Arg::Out}},
        {"GuiExtraBytes", S3RE_G0(GfxDword_GuiExtraBytes), Self::Image, {}},
        {"GuiExtraCount", S3RE_G0(GfxDword_GuiExtraCount), Self::Image, {}},
        {"GuiRleData", S3RE_G0(GfxDword_GuiRleData), Self::Image, {}},
        {"TextureExtraBytes", S3RE_G0(GfxDword_TextureExtraBytes), Self::Image, {}},
        {"TextureExtraCount", S3RE_G0(GfxDword_TextureExtraCount), Self::Image, {}},
        {"TextureExtraByte", S3RE_G1(GfxDword_TextureExtraByte), Self::Image, {Arg::Index}},
        {"TextureRleData", S3RE_G0(GfxDword_TextureRleData), Self::Image, {}},
        {"ListImage", S3RE_G2(GfxDword_ListImage), Self::Image, {Arg::Out, Arg::Index}},
        {"SpriteExtraBytes", S3RE_G0(GfxDword_SpriteExtraBytes), Self::Image, {}},
        {"SpriteExtraCount", S3RE_G0(GfxDword_SpriteExtraCount), Self::Image, {}},
        {"ListImageChecked", S3RE_G2(GfxDword_ListImageChecked), Self::Image, {Arg::Out, Arg::Index}},
        {"ListImageChecked_4b19c0", S3RE_G2(GfxDword_ListImageChecked_4b19c0), Self::Image, {Arg::Out, Arg::Index}},
        {"Construct_609900", S3RE_G1(GfxDword_Construct_609900), Self::Point, {Arg::Any}},
        {"SpriteRleData", S3RE_G0(GfxDword_SpriteRleData), Self::Image, {}},
        {"GetPixelFormat", S3RE_G1(CGraphicsData_GetPixelFormat), Self::Gfx, {Arg::Out}},
        {"LanguageCount", S3RE_G0(CGraphicsData_LanguageCount), Self::Gfx, {}},
        {"SetLanguage", S3RE_G1(CGraphicsData_SetLanguage), Self::Gfx, {Arg::Any}},
        {"ColorIndexedAnimationPointer", S3RE_G2(CGraphicsData_ColorIndexedAnimationPointer), Self::Gfx,
         {Arg::Out, Arg::Id}},
        {"ShadowAnimationPointer", S3RE_G2(CGraphicsData_ShadowAnimationPointer), Self::Gfx, {Arg::Out, Arg::Id}},
        {"PalettePointer", S3RE_G2(CGraphicsData_PalettePointer), Self::Gfx, {Arg::Id, Arg::Brightness}},
        {"GuiImagePointerAt", S3RE_G3(CGraphicsData_GuiImagePointerAt), Self::Gfx, {Arg::Out, Arg::Id, Arg::Index}},
        {"LandscapeTexturePointer", S3RE_G2(CGraphicsData_LandscapeTexturePointer), Self::Gfx, {Arg::Out, Arg::Id}},
        {"GetSequenceFrame", S3RE_G3(CGraphicsData_GetSequenceFrame), Self::Gfx, {Arg::Out, Arg::Id, Arg::Index}},
        {"SequenceFrame_default", S3RE_G0(SequenceFrame_SequenceFrame_default), Self::Frame, {}},
        {"SequenceFrame", S3RE_G2(SequenceFrame_SequenceFrame), Self::Frame, {Arg::Any, Arg::Record}},
        {"OffsetX", S3RE_G0(SequenceFrame_OffsetX), Self::Frame, {}},
        {"OffsetY", S3RE_G0(SequenceFrame_OffsetY), Self::Frame, {}},
        {"BodyImage", S3RE_G1(SequenceFrame_BodyImage), Self::Frame, {Arg::Out}},
        {"ColorIndexedImage", S3RE_G1(SequenceFrame_ColorIndexedImage), Self::Frame, {Arg::Out}},
        {"ShadowImage", S3RE_G1(SequenceFrame_ShadowImage), Self::Frame, {Arg::Out}},
        {"SoundId", S3RE_G0(SequenceFrame_SoundId), Self::Frame, {}},
        {"SoundHighByte", S3RE_G0(SequenceFrame_SoundHighByte), Self::Frame, {}},
        {"Point16_Negate", S3RE_G1(Point16_Negate), Self::Point, {Arg::Out}},
        {"Point16_Add", S3RE_G2(Point16_Add), Self::Point, {Arg::Out, Arg::Point}},
    };
}

#undef S3RE_G0
#undef S3RE_G1
#undef S3RE_G2
#undef S3RE_G3

// Where the sequence records of GetSequenceFrame and SequenceFrame live,
// and the sequence tables pointing at them.
constexpr std::uint32_t kRecordArea = 0x18000;
constexpr std::uint32_t kSequenceTableArea = 0xa000;
constexpr std::uint32_t kSequenceTableBytes = 0x120;

void CompareImages(test::Context& ctx, bool wrongSoundId, int rounds) {
    std::vector<Case> cases = AllCases();
    if (wrongSoundId) {
        for (Case& c : cases) {
            if (std::strcmp(c.name, "SoundId") == 0) {
                c.ours.g0 = [](void* self, void*) -> std::uint32_t {
                    return As<SequenceFrame>(self)->sound & 0xffffff;  // wrong: -1 not kept
                };
            }
        }
    }
    for (int round = 0; round < rounds; ++round) {
        BuildData(ctx);
        // The sequence tables point at records in the record area (the
        // sequence's frame count dword first).
        for (std::uint32_t i = 0; i < 0x200; ++i) {
            WriteSequenceRecord(ctx, kRecordArea + 4 + i * sizeof(SequenceRecord));
        }
        auto* const gfx = reinterpret_cast<CGraphicsData*>(g_data);
        for (std::uint32_t group = 0; group < gfx::kGroups; ++group) {
            const std::uint32_t at = kSequenceTableArea + group * kSequenceTableBytes;
            auto* const table = reinterpret_cast<std::uint32_t*>(g_data + at + 8);
            for (std::uint32_t i = 0; i < kTableEntries + 8; ++i) {
                table[i] = kRecordArea + sizeof(SequenceRecord) * ctx.rng().Below(0x100);
            }
            gfx->sequenceTables[group] = reinterpret_cast<const gfx::SequenceTable*>(g_data + at);
        }
        for (int i = 0; i < 200; ++i) {
            const Case& c = cases[ctx.rng().Below(static_cast<std::uint32_t>(cases.size()))];
            // self: an image reference to somewhere in the list area, the gfx
            // object, a frame in the output area (random contents), a point.
            std::uint8_t frameOrPoint[0x20];
            ctx.rng().Fill(frameOrPoint, sizeof(frameOrPoint));
            GfxDword image{Address(g_data) + kListArea + 0x100 * ctx.rng().Below(0x80) +
                           (ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(0x80))};
            if (ctx.rng().Chance(1, 20)) {
                image.image = 0;  // IsValid
                if (c.self == Self::Image && std::strcmp(c.name, "IsValid") != 0) {
                    continue;
                }
            }
            if (c.self == Self::Frame && ctx.rng().Chance(1, 4)) {
                reinterpret_cast<SequenceFrame*>(frameOrPoint)->sound = kNoSound;
            }
            std::uint32_t argsA[3];
            std::uint32_t argsB[3];
            for (int k = 0; k < 3; ++k) {
                std::uint32_t value = 0;
                switch (c.args[k]) {
                case Arg::None: break;
                case Arg::Out: value = 8; break;  // offset in the output area
                case Arg::Index: value = ctx.rng().Below(40); break;
                case Arg::Id:
                    value = ctx.rng().Below(gfx::kGroups) << 16 | ctx.rng().Below(kTableEntries + 2);
                    break;
                case Arg::Brightness: value = ctx.rng().Chance(1, 8) ? ctx.rng().Below(0x1000) : ctx.rng().Below(256); break;
                case Arg::Record: value = Address(g_data) + kRecordArea + 4 + sizeof(SequenceRecord) * ctx.rng().Below(0x1ff); break;
                case Arg::Any: value = ctx.rng().Next32(); break;
                case Arg::Point: value = 0x20; break;  // a point in the output area
                }
                argsA[k] = argsB[k] = value;
                if (c.args[k] == Arg::Out || c.args[k] == Arg::Point) {
                    argsA[k] = Address(g_outA.bytes) + value;
                    argsB[k] = Address(g_outB.bytes) + value;
                }
            }
            // SequenceFrame's gfx argument is the gfx object.
            if (c.self == Self::Frame && c.args[0] == Arg::Any && c.args[1] == Arg::Record) {
                argsA[0] = argsB[0] = Address(g_data);
            }
            ctx.rng().Fill(g_outA.bytes, sizeof(g_outA.bytes));
            std::memcpy(g_outB.bytes, g_outA.bytes, sizeof(g_outA.bytes));
            // Frames and points are copied into the output areas (self).
            void* selfA = nullptr;
            void* selfB = nullptr;
            // SetLanguage writes: one copy of the object per side.
            static CGraphicsData gfxA;
            static CGraphicsData gfxB;
            switch (c.self) {
            case Self::Image: selfA = selfB = &image; break;
            case Self::Gfx:
                if (std::strcmp(c.name, "SetLanguage") == 0) {
                    std::memcpy(&gfxA, g_data, sizeof(CGraphicsData));
                    std::memcpy(&gfxB, g_data, sizeof(CGraphicsData));
                    selfA = &gfxA;
                    selfB = &gfxB;
                } else {
                    selfA = selfB = g_data;
                }
                break;
            case Self::Frame:
            case Self::Point:
                std::memcpy(g_outA.bytes + 0x28, frameOrPoint, 0x14);
                std::memcpy(g_outB.bytes + 0x28, frameOrPoint, 0x14);
                selfA = g_outA.bytes + 0x28;
                selfB = g_outB.bytes + 0x28;
                break;
            }
            std::uint32_t ra = 0;
            std::uint32_t rb = 0;
            std::vector<DebugLogCall> logsA;
            std::vector<DebugLogCall> logsB;
            {
                DebugLogRecorder recorder;
                ra = Invoke(c.original, selfA, argsA[0], argsA[1], argsA[2]);
                logsA = recorder.Take();
            }
            {
                DebugLogRecorder recorder;
                rb = Invoke(c.ours, selfB, argsB[0], argsB[1], argsB[2]);
                logsB = recorder.Take();
            }
            // Addresses in the output areas (out arguments, frames returning
            // this) compare relative to their area.
            // (Construct_609900 returns its `this`, in the output area too.)
            const bool returnsOutput = c.args[0] == Arg::Out || c.self == Self::Frame ||
                                       std::strcmp(c.name, "Construct_609900") == 0;
            if (returnsOutput && ra - Address(g_outA.bytes) < sizeof(g_outA.bytes)) {
                ra -= Address(g_outA.bytes);
                rb -= Address(g_outB.bytes);
            }
            if (std::strcmp(c.name, "OffsetX") == 0 || std::strcmp(c.name, "OffsetY") == 0) {
                ra &= 0xffff;
                rb &= 0xffff;
            }
            bool same = S3RE_EXPECT_EQ(ra, rb);
            same &= S3RE_EXPECT_MEM_EQ(g_outA.bytes, g_outB.bytes, sizeof(g_outA.bytes));
            same &= S3RE_EXPECT_MEM_EQ(&gfxA, &gfxB, sizeof(CGraphicsData));
            same &= S3RE_EXPECT(logsA.size() == logsB.size());
            for (std::size_t k = 0; same && k < logsA.size(); ++k) {
                same &= S3RE_EXPECT(logsA[k] == logsB[k]);
            }
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  round %d, case %d: %s", round, i, c.name);
                return;
            }
        }
    }
}

}  // namespace

S3RE_TEST(GfxImages_MatchOriginal) {
    CompareImages(ctx, false, 300);
}

S3RE_TEST(GfxImages_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareImages(probe, true, 30);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
