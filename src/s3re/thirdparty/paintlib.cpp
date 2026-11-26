// PaintLib (Ulrich von Zadow, about 1998), statically linked into S3.EXE:
// the bitmap and decoder classes around its PNG decoder (re/picture_code.md;
// class names from the RTTI, method names PaintLib's). Only the online
// lobby's banner (Banner\Banner.png) is decoded with it. Member functions are
// __thiscall (written as __fastcall with the dummy EDX).
//
// CPicDecoder (vtable 0x65c110): +0x10 the destination bits per pixel.
//   CPicDecoder (0x005a4980): vtable, +0x14 = 0.
//   ~CPicDecoder (0x005a49d0): frees PaintLib's trace file name (global
//     0x7e8bbc) and clears it; scalar_deleting_destructor (0x005a4990).
//   CalcDestBPP (0x005a4cc0): (srcBPP): if +0x10 is 0, it becomes 8 for
//     srcBPP <= 8 (signed), else 32; returns +0x10.
// CBmp (vtable 0x65c124): +4 width, +8 height, +0xc bits per pixel (16 bit),
//   +0x10 palette (256 RGBA entries), +0x18 alpha flag.
//   CBmp (0x005a4ce0), ~CBmp (0x005a4d10), scalar_deleting_destructor
//   (0x005a4cf0).
//   Create (0x005a4d20): (width, height, bpp, alpha): virtual freeMembers
//     (+0x14), then virtual internalCreate (+0x10); returns its EAX.
//   initLocals (0x005a4d50): stores the four values, calls virtual
//     initLineArray (+0x18), and below 16 bits fills a grey palette of 2^bpp
//     entries (2^24 for 32; bpp re-read after the call, the palette pointer
//     re-read per entry, entries indexed by the low byte of the counter).
// CAnyBmp (vtable 0x65c0dc), derived from CBmp: +0x14 the pixel bits, +0x1c
//   the line pointer array.
//   CAnyBmp (0x005a4350): bits = new(0x100), palette = new(0x400), then
//     initLocals(16, 16, 8, 0) (the original's SEH frame only unwinds the
//     base class if that throws; it doesn't).
//   ~CAnyBmp (0x005a43e0): freeMembers inlined, then ~CBmp;
//     scalar_deleting_destructor (0x005a43c0).
//   GetMemUsed (0x005a4460): (bpp / 8) * width * height + 0x20 + 0x20, plus
//     4 * 2^bpp palette bytes below 16 bits.
//   GetBytesPerLine (0x005a44a0): (bpp / 8) * width.
//   internalCreate (0x005a44b0): (width, height, bpp, alpha): bits =
//     new((bpp / 8) * width * height), palette = new(0x400) for 8 bits else
//     null, then initLocals.
//   initLineArray (0x005a4520): lines = new(4 * height), one pointer per row
//     (virtual GetBytesPerLine +0xc apart; height, bits and lines re-read per
//     row); returns the bytes per line.
//   freeMembers (0x005a4570): deletes bits, palette (if set) and lines.
// CTextException (vtable 0x65c0cc): +4 code, +8 heap copy of the text.
//   ~CTextException (0x005a4330), scalar_deleting_destructor (0x005a4290).
// CDataSource (vtable 0x65c608): +4 data, +8 read cursor, +0xc heap copy of
//   the name, +0x10 size.
//   CDataSource (0x005b03f0): name, data, cursor = 0 (size untouched).
//   ~CDataSource (0x005b0460): if data is set, frees the name and clears
//     name, data and cursor; scalar_deleting_destructor (0x005b0410).
//   Attach (0x005b04a0): (name, data, size): copies the name (strlen + 1
//     bytes, no null check of the allocation), data = cursor = data, size;
//     returns data.
//   Release (0x005b0500): frees the name, clears name, data and cursor;
//     returns 0.

#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

using game::g_paintlibTraceFile;
using msvc6::GameOperatorDelete;
using msvc6::GameOperatorNew;

#pragma pack(push, 1)

struct CPicDecoder {
    std::uint32_t vtable;      // 0x00
    std::uint8_t unk04[0xc];   // 0x04
    std::int32_t dest_bpp;     // 0x10
    std::uint32_t unk14;       // 0x14
};
static_assert(offsetof(CPicDecoder, dest_bpp) == 0x10);

struct CBmp {
    std::uint32_t vtable;      // 0x00
    std::int32_t width;        // 0x04
    std::int32_t height;       // 0x08
    std::uint16_t bpp;         // 0x0c
    std::uint16_t pad0e;       // 0x0e
    std::uint8_t* palette;     // 0x10 256 RGBA entries
    std::uint8_t* bits;        // 0x14 CAnyBmp
    std::uint32_t alpha;       // 0x18
    std::uint8_t** lines;      // 0x1c CAnyBmp
};
static_assert(offsetof(CBmp, palette) == 0x10);
static_assert(offsetof(CBmp, lines) == 0x1c);

struct CTextException {
    std::uint32_t vtable;  // 0x00
    std::int32_t code;     // 0x04
    char* text;            // 0x08
};

struct CDataSource {
    std::uint32_t vtable;  // 0x00
    std::uint8_t* data;    // 0x04
    std::uint8_t* cursor;  // 0x08
    char* name;            // 0x0c
    std::uint32_t size;    // 0x10
};
static_assert(sizeof(CDataSource) == 0x14);

#pragma pack(pop)

namespace {

constexpr std::uint32_t kDeleteSelf = 1;            // scalar deleting destructor flag
constexpr std::uint32_t kPaletteBytes = 0x400;      // 256 RGBA entries
constexpr std::uint32_t kDefaultBitsBytes = 0x100;  // CAnyBmp's constructor
constexpr std::int32_t kDefaultSize = 16;
constexpr std::uint16_t kDefaultBpp = 8;
constexpr std::uint16_t kPalettedLimit = 16;        // a palette below 16 bits per pixel
constexpr std::uint32_t kHeaderBytes = 0x20;        // GetMemUsed's two fixed parts

// CBmp's virtual functions used here (slot offset / 4).
constexpr int kSlotGetBytesPerLine = 3;   // +0x0c
constexpr int kSlotInternalCreate = 4;    // +0x10
constexpr int kSlotFreeMembers = 5;       // +0x14
constexpr int kSlotInitLineArray = 6;     // +0x18

constexpr std::uintptr_t kCBmpConstruct = 0x005a4ce0;
constexpr std::uintptr_t kCBmpDestroy = 0x005a4d10;
constexpr std::uintptr_t kCBmpInitLocals = 0x005a4d50;
constexpr std::uintptr_t kCAnyBmpDestroy = 0x005a43e0;
constexpr std::uintptr_t kCPicDecoderDestroy = 0x005a49d0;
constexpr std::uintptr_t kCTextExceptionDestroy = 0x005a4330;

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using InitLocalsFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t width, std::int32_t height,
                                                std::uint32_t bpp, std::uint32_t alpha);

template <typename Fn>
Fn Virtual(const void* object, int slot) {
    const auto* vtable = reinterpret_cast<const std::uintptr_t*>(static_cast<std::uintptr_t>(*static_cast<const std::uint32_t*>(object)));
    return reinterpret_cast<Fn>(vtable[slot]);
}

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

// The copy of a C string on the game's heap (strlen + 1 bytes; the
// allocation is not checked, as in the original).
char* CopyString(const char* text) {
    const std::size_t bytes = std::strlen(text) + 1;
    auto* copy = static_cast<char*>(GameOperatorNew(static_cast<std::uint32_t>(bytes)));
    std::memcpy(copy, text, bytes);
    return copy;
}

}  // namespace

using ThisFn = void*(__fastcall*)(void* self, void* edx);
using VoidThisFn = void(__fastcall*)(void* self, void* edx);
using DeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using CalcDestBppFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t srcBpp);
using CreateFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t width, std::int32_t height,
                                            std::uint32_t bpp, std::uint32_t alpha);
using VoidCreateFn = void(__fastcall*)(void* self, void* edx, std::int32_t width, std::int32_t height,
                                       std::uint32_t bpp, std::uint32_t alpha);
using ValueFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using AttachFn = std::uint8_t*(__fastcall*)(void* self, void* edx, const char* name, std::uint8_t* data,
                                            std::uint32_t size);

void* __fastcall CPicDecoder_Construct(void* self, void* edx);
void* __fastcall CPicDecoder_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void __fastcall CPicDecoder_Destroy(void* self, void* edx);
std::int32_t __fastcall CPicDecoder_CalcDestBPP(void* self, void* edx, std::int32_t srcBpp);
void* __fastcall CBmp_Construct(void* self, void* edx);
void* __fastcall CBmp_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void __fastcall CBmp_Destroy(void* self, void* edx);
std::uint32_t __fastcall CBmp_Create(void* self, void* edx, std::int32_t width, std::int32_t height,
                                     std::uint32_t bpp, std::uint32_t alpha);
void __fastcall CBmp_initLocals(void* self, void* edx, std::int32_t width, std::int32_t height, std::uint32_t bpp,
                                std::uint32_t alpha);
void* __fastcall CAnyBmp_Construct(void* self, void* edx);
void* __fastcall CAnyBmp_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void __fastcall CAnyBmp_Destroy(void* self, void* edx);
std::uint32_t __fastcall CAnyBmp_GetMemUsed(void* self, void* edx);
std::uint32_t __fastcall CAnyBmp_GetBytesPerLine(void* self, void* edx);
void __fastcall CAnyBmp_internalCreate(void* self, void* edx, std::int32_t width, std::int32_t height,
                                       std::uint32_t bpp, std::uint32_t alpha);
std::uint32_t __fastcall CAnyBmp_initLineArray(void* self, void* edx);
void __fastcall CAnyBmp_freeMembers(void* self, void* edx);
void* __fastcall CTextException_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void __fastcall CTextException_Destroy(void* self, void* edx);
void* __fastcall CDataSource_Construct(void* self, void* edx);
void* __fastcall CDataSource_DeletingDestructor(void* self, void* edx, std::uint32_t flags);
void __fastcall CDataSource_Destroy(void* self, void* edx);
std::uint8_t* __fastcall CDataSource_Attach(void* self, void* edx, const char* name, std::uint8_t* data,
                                            std::uint32_t size);
std::uint32_t __fastcall CDataSource_Release(void* self, void* edx);

S3RE_HOOK(0x005a4980, CPicDecoder_Construct, ThisFn, Original_CPicDecoder_Construct, CPicDecoder_Construct);
S3RE_HOOK(0x005a4990, CPicDecoder_DeletingDestructor, DeletingFn, Original_CPicDecoder_DeletingDestructor,
          CPicDecoder_DeletingDestructor);
S3RE_HOOK(0x005a49d0, CPicDecoder_Destroy, VoidThisFn, Original_CPicDecoder_Destroy, CPicDecoder_Destroy);
S3RE_HOOK(0x005a4cc0, CPicDecoder_CalcDestBPP, CalcDestBppFn, Original_CPicDecoder_CalcDestBPP,
          CPicDecoder_CalcDestBPP);
S3RE_HOOK(0x005a4ce0, CBmp_Construct, ThisFn, Original_CBmp_Construct, CBmp_Construct);
S3RE_HOOK(0x005a4cf0, CBmp_DeletingDestructor, DeletingFn, Original_CBmp_DeletingDestructor, CBmp_DeletingDestructor);
S3RE_HOOK(0x005a4d10, CBmp_Destroy, VoidThisFn, Original_CBmp_Destroy, CBmp_Destroy);
S3RE_HOOK(0x005a4d20, CBmp_Create, CreateFn, Original_CBmp_Create, CBmp_Create);
S3RE_HOOK(0x005a4d50, CBmp_initLocals, VoidCreateFn, Original_CBmp_initLocals, CBmp_initLocals);
S3RE_HOOK(0x005a4350, CAnyBmp_Construct, ThisFn, Original_CAnyBmp_Construct, CAnyBmp_Construct);
S3RE_HOOK(0x005a43c0, CAnyBmp_DeletingDestructor, DeletingFn, Original_CAnyBmp_DeletingDestructor,
          CAnyBmp_DeletingDestructor);
S3RE_HOOK(0x005a43e0, CAnyBmp_Destroy, VoidThisFn, Original_CAnyBmp_Destroy, CAnyBmp_Destroy);
S3RE_HOOK(0x005a4460, CAnyBmp_GetMemUsed, ValueFn, Original_CAnyBmp_GetMemUsed, CAnyBmp_GetMemUsed);
S3RE_HOOK(0x005a44a0, CAnyBmp_GetBytesPerLine, ValueFn, Original_CAnyBmp_GetBytesPerLine, CAnyBmp_GetBytesPerLine);
S3RE_HOOK(0x005a44b0, CAnyBmp_internalCreate, VoidCreateFn, Original_CAnyBmp_internalCreate, CAnyBmp_internalCreate);
S3RE_HOOK(0x005a4520, CAnyBmp_initLineArray, ValueFn, Original_CAnyBmp_initLineArray, CAnyBmp_initLineArray);
S3RE_HOOK(0x005a4570, CAnyBmp_freeMembers, VoidThisFn, Original_CAnyBmp_freeMembers, CAnyBmp_freeMembers);
S3RE_HOOK(0x005a4290, CTextException_DeletingDestructor, DeletingFn, Original_CTextException_DeletingDestructor,
          CTextException_DeletingDestructor);
S3RE_HOOK(0x005a4330, CTextException_Destroy, VoidThisFn, Original_CTextException_Destroy, CTextException_Destroy);
S3RE_HOOK(0x005b03f0, CDataSource_Construct, ThisFn, Original_CDataSource_Construct, CDataSource_Construct);
S3RE_HOOK(0x005b0410, CDataSource_DeletingDestructor, DeletingFn, Original_CDataSource_DeletingDestructor,
          CDataSource_DeletingDestructor);
S3RE_HOOK(0x005b0460, CDataSource_Destroy, VoidThisFn, Original_CDataSource_Destroy, CDataSource_Destroy);
S3RE_HOOK(0x005b04a0, CDataSource_Attach, AttachFn, Original_CDataSource_Attach, CDataSource_Attach);
S3RE_HOOK(0x005b0500, CDataSource_Release, ValueFn, Original_CDataSource_Release, CDataSource_Release);

// ---------------------------------------------------------------------------
// CPicDecoder
// ---------------------------------------------------------------------------

// Purpose: Constructs the decoder base class (CPNGDecoder's constructor
// calls it).
void* __fastcall CPicDecoder_Construct(void* self, void* /*edx*/) {
    auto* decoder = static_cast<CPicDecoder*>(self);
    decoder->vtable = game::vtables::kCPicDecoder;
    decoder->unk14 = 0;
    return self;
}

// Purpose: The decoder's destructor: frees PaintLib's trace file name.
void __fastcall CPicDecoder_Destroy(void* self, void* /*edx*/) {
    static_cast<CPicDecoder*>(self)->vtable = game::vtables::kCPicDecoder;
    if (g_paintlibTraceFile != nullptr) {
        GameOperatorDelete(g_paintlibTraceFile);
    }
    g_paintlibTraceFile = nullptr;
}

// Purpose: Destroys a decoder and frees it if asked to (delete).
void* __fastcall CPicDecoder_DeletingDestructor(void* self, void* edx, std::uint32_t flags) {
    CPicDecoder_Destroy(self, edx);  // inlined in the original
    if ((flags & kDeleteSelf) != 0) {
        GameOperatorDelete(self);
    }
    return self;
}

// Purpose: Picks the output bits per pixel unless the caller asked for one:
// 8 for paletted sources, 32 for the others.
std::int32_t __fastcall CPicDecoder_CalcDestBPP(void* self, void* /*edx*/, std::int32_t srcBpp) {
    auto* decoder = static_cast<CPicDecoder*>(self);
    if (decoder->dest_bpp == 0) {
        decoder->dest_bpp = srcBpp > 8 ? 32 : 8;
    }
    return decoder->dest_bpp;
}

// ---------------------------------------------------------------------------
// CBmp
// ---------------------------------------------------------------------------

// Purpose: Constructs the bitmap base class.
void* __fastcall CBmp_Construct(void* self, void* /*edx*/) {
    static_cast<CBmp*>(self)->vtable = game::vtables::kCBmp;
    return self;
}

// Purpose: The bitmap base class's destructor (nothing to free).
void __fastcall CBmp_Destroy(void* self, void* /*edx*/) {
    static_cast<CBmp*>(self)->vtable = game::vtables::kCBmp;
}

// Purpose: Destroys a bitmap base object and frees it if asked to.
void* __fastcall CBmp_DeletingDestructor(void* self, void* /*edx*/, std::uint32_t flags) {
    static_cast<CBmp*>(self)->vtable = game::vtables::kCBmp;
    if ((flags & kDeleteSelf) != 0) {
        GameOperatorDelete(self);
    }
    return self;
}

// Purpose: (Re)creates the bitmap with a new size and depth: frees the old
// pixels, allocates new ones.
std::uint32_t __fastcall CBmp_Create(void* self, void* /*edx*/, std::int32_t width, std::int32_t height,
                                     std::uint32_t bpp, std::uint32_t alpha) {
    Virtual<MemberFn>(self, kSlotFreeMembers)(self, nullptr);
    return Virtual<CreateFn>(self, kSlotInternalCreate)(self, nullptr, width, height, bpp, alpha);
}

// Purpose: Stores the bitmap's size and depth, sets up the row pointers and,
// for paletted depths, a grey palette.
void __fastcall CBmp_initLocals(void* self, void* /*edx*/, std::int32_t width, std::int32_t height, std::uint32_t bpp,
                                std::uint32_t alpha) {
    auto* bmp = static_cast<CBmp*>(self);
    const std::uint16_t depth = static_cast<std::uint16_t>(bpp);
    bmp->width = width;
    bmp->height = height;
    bmp->bpp = depth;
    bmp->alpha = alpha;
    Virtual<MemberFn>(self, kSlotInitLineArray)(self, nullptr);
    if (depth >= kPalettedLimit) {
        return;
    }
    // bpp is read again: initLineArray could have changed it.
    const std::uint16_t current = bmp->bpp;
    const std::int32_t colours = current == 32 ? 0x1000000 : static_cast<std::int32_t>(1u << (current & 31));
    const std::uint8_t step = static_cast<std::uint8_t>(256 / colours);
    for (std::int32_t i = 0; i < colours; ++i) {
        std::uint8_t* entry = bmp->palette + (i & 0xff) * 4;  // the palette pointer is re-read per entry
        const std::uint8_t grey = static_cast<std::uint8_t>(static_cast<std::uint8_t>(i) * step);
        entry[2] = grey;
        entry[1] = grey;
        entry[0] = grey;
        entry[3] = 0xff;
    }
}

// ---------------------------------------------------------------------------
// CAnyBmp
// ---------------------------------------------------------------------------

// Purpose: Constructs an in-memory bitmap: a 16 x 16, 8-bit default.
void* __fastcall CAnyBmp_Construct(void* self, void* /*edx*/) {
    auto* bmp = static_cast<CBmp*>(self);
    CallAt<ThisFn>(kCBmpConstruct)(self, nullptr);
    bmp->vtable = game::vtables::kCAnyBmp;
    bmp->bits = static_cast<std::uint8_t*>(GameOperatorNew(kDefaultBitsBytes));
    bmp->palette = static_cast<std::uint8_t*>(GameOperatorNew(kPaletteBytes));
    CallAt<InitLocalsFn>(kCBmpInitLocals)(self, nullptr, kDefaultSize, kDefaultSize, kDefaultBpp, 0);
    return self;
}

// Purpose: Frees an in-memory bitmap's pixels, palette and row pointers.
void __fastcall CAnyBmp_freeMembers(void* self, void* /*edx*/) {
    auto* bmp = static_cast<CBmp*>(self);
    GameOperatorDelete(bmp->bits);
    bmp->bits = nullptr;
    if (bmp->palette != nullptr) {
        GameOperatorDelete(bmp->palette);
        bmp->palette = nullptr;
    }
    GameOperatorDelete(bmp->lines);
    bmp->lines = nullptr;
}

// Purpose: The in-memory bitmap's destructor.
void __fastcall CAnyBmp_Destroy(void* self, void* edx) {
    static_cast<CBmp*>(self)->vtable = game::vtables::kCAnyBmp;
    CAnyBmp_freeMembers(self, edx);  // inlined in the original
    CallAt<VoidThisFn>(kCBmpDestroy)(self, nullptr);
}

// Purpose: Destroys an in-memory bitmap and frees it if asked to.
void* __fastcall CAnyBmp_DeletingDestructor(void* self, void* /*edx*/, std::uint32_t flags) {
    CallAt<VoidThisFn>(kCAnyBmpDestroy)(self, nullptr);
    if ((flags & kDeleteSelf) != 0) {
        GameOperatorDelete(self);
    }
    return self;
}

// Purpose: How many bytes the bitmap uses (pixels, palette, headers).
std::uint32_t __fastcall CAnyBmp_GetMemUsed(void* self, void* /*edx*/) {
    const auto* bmp = static_cast<const CBmp*>(self);
    std::uint32_t extra = kHeaderBytes;
    if (bmp->bpp < kPalettedLimit) {
        extra = (1u << (bmp->bpp & 31)) * 4 + kHeaderBytes;
    }
    const std::uint32_t pixels = (static_cast<std::uint32_t>(bmp->bpp) >> 3) * static_cast<std::uint32_t>(bmp->width) *
                                 static_cast<std::uint32_t>(bmp->height);
    return pixels + extra + kHeaderBytes;
}

// Purpose: The bytes of one pixel row.
std::uint32_t __fastcall CAnyBmp_GetBytesPerLine(void* self, void* /*edx*/) {
    const auto* bmp = static_cast<const CBmp*>(self);
    return (static_cast<std::uint32_t>(bmp->bpp) >> 3) * static_cast<std::uint32_t>(bmp->width);
}

// Purpose: Allocates the pixels (and the palette for 8 bits) of a new size.
void __fastcall CAnyBmp_internalCreate(void* self, void* /*edx*/, std::int32_t width, std::int32_t height,
                                       std::uint32_t bpp, std::uint32_t alpha) {
    auto* bmp = static_cast<CBmp*>(self);
    const std::uint16_t depth = static_cast<std::uint16_t>(bpp);
    bmp->bits = static_cast<std::uint8_t*>(GameOperatorNew(
        (static_cast<std::uint32_t>(depth) >> 3) * static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height)));
    bmp->palette = depth == 8 ? static_cast<std::uint8_t*>(GameOperatorNew(kPaletteBytes)) : nullptr;
    CallAt<InitLocalsFn>(kCBmpInitLocals)(self, nullptr, width, height, bpp, alpha);
}

// Purpose: Builds the array of row pointers into the pixels.
std::uint32_t __fastcall CAnyBmp_initLineArray(void* self, void* /*edx*/) {
    auto* bmp = static_cast<CBmp*>(self);
    bmp->lines = static_cast<std::uint8_t**>(GameOperatorNew(static_cast<std::uint32_t>(bmp->height) << 2));
    const std::uint32_t bytesPerLine = Virtual<ValueFn>(self, kSlotGetBytesPerLine)(self, nullptr);
    std::uint32_t offset = 0;
    for (std::int32_t row = 0; row < bmp->height; ++row) {  // height, bits and lines re-read per row
        bmp->lines[row] = bmp->bits + offset;
        offset += bytesPerLine;
    }
    return bytesPerLine;
}

// ---------------------------------------------------------------------------
// CTextException
// ---------------------------------------------------------------------------

// Purpose: The PaintLib exception's destructor: frees its text.
void __fastcall CTextException_Destroy(void* self, void* /*edx*/) {
    auto* exception = static_cast<CTextException*>(self);
    exception->vtable = game::vtables::kCTextException;
    GameOperatorDelete(exception->text);
}

// Purpose: Destroys a PaintLib exception and frees it if asked to.
void* __fastcall CTextException_DeletingDestructor(void* self, void* edx, std::uint32_t flags) {
    CTextException_Destroy(self, edx);  // inlined in the original
    if ((flags & kDeleteSelf) != 0) {
        GameOperatorDelete(self);
    }
    return self;
}

// ---------------------------------------------------------------------------
// CDataSource
// ---------------------------------------------------------------------------

// Purpose: Constructs the decoder's input source (base of CFileSource).
void* __fastcall CDataSource_Construct(void* self, void* /*edx*/) {
    auto* source = static_cast<CDataSource*>(self);
    source->vtable = game::vtables::kCDataSource;
    source->name = nullptr;
    source->data = nullptr;
    source->cursor = nullptr;
    return self;
}

// Purpose: The input source's destructor: forgets the attached data.
void __fastcall CDataSource_Destroy(void* self, void* /*edx*/) {
    auto* source = static_cast<CDataSource*>(self);
    source->vtable = game::vtables::kCDataSource;
    if (source->data == nullptr) {
        return;
    }
    GameOperatorDelete(source->name);
    source->name = nullptr;
    source->data = nullptr;
    source->cursor = nullptr;
}

// Purpose: Destroys an input source and frees it if asked to.
void* __fastcall CDataSource_DeletingDestructor(void* self, void* edx, std::uint32_t flags) {
    CDataSource_Destroy(self, edx);  // inlined in the original
    if ((flags & kDeleteSelf) != 0) {
        GameOperatorDelete(self);
    }
    return self;
}

// Purpose: Attaches a block of file data (the mapped file) under its name.
std::uint8_t* __fastcall CDataSource_Attach(void* self, void* /*edx*/, const char* name, std::uint8_t* data,
                                            std::uint32_t size) {
    auto* source = static_cast<CDataSource*>(self);
    source->name = CopyString(name);
    source->cursor = data;
    source->data = data;
    source->size = size;
    return data;
}

// Purpose: Detaches the data (CFileSource::Close calls it).
std::uint32_t __fastcall CDataSource_Release(void* self, void* /*edx*/) {
    auto* source = static_cast<CDataSource*>(self);
    GameOperatorDelete(source->name);
    source->name = nullptr;
    source->data = nullptr;
    source->cursor = nullptr;
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Objects are 0x40-byte buffers; heap pointers in them come from the game's
// operator new, separately for the two runs, and are compared by whether
// they are null (pointer fields are cleared to compare the rest). A fake
// vtable of test functions records the virtual calls.

namespace {

std::vector<std::uint32_t>* g_calls = nullptr;
std::uint32_t g_virtualResult = 0;

std::uint32_t __fastcall FakeGetBytesPerLine(void* /*self*/, void* /*edx*/) {
    g_calls->push_back(0xc);
    return g_virtualResult;
}

std::uint32_t __fastcall FakeInternalCreate(void* /*self*/, void* /*edx*/, std::int32_t width, std::int32_t height,
                                            std::uint32_t bpp, std::uint32_t alpha) {
    g_calls->insert(g_calls->end(), {0x10, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), bpp, alpha});
    return g_virtualResult ^ 0x5555;
}

std::uint32_t __fastcall FakeFreeMembers(void* /*self*/, void* /*edx*/) {
    g_calls->push_back(0x14);
    return 0;
}

std::uint32_t __fastcall FakeInitLineArray(void* self, void* /*edx*/) {
    g_calls->push_back(0x18);
    auto* bmp = static_cast<CBmp*>(self);
    if ((g_virtualResult & 0x100) != 0) {
        bmp->bpp ^= 1;  // initLocals reads bpp again after this call
    }
    if ((g_virtualResult & 0xfe00) == 0x200 && bmp->bpp < 16) {
        bmp->bpp = 32;  // then the palette loop runs 2^24 times
    }
    return 0;
}

const std::uintptr_t kFakeVtable[8] = {0, 0, 0,
                                       reinterpret_cast<std::uintptr_t>(&FakeGetBytesPerLine),
                                       reinterpret_cast<std::uintptr_t>(&FakeInternalCreate),
                                       reinterpret_cast<std::uintptr_t>(&FakeFreeMembers),
                                       reinterpret_cast<std::uintptr_t>(&FakeInitLineArray), 0};

struct PaintlibFunctions {
    CalcDestBppFn calcDestBpp;
    ThisFn picConstruct, bmpConstruct, dataConstruct;
    VoidThisFn bmpDestroy;
    DeletingFn bmpDeleting;
    CreateFn create;
    VoidCreateFn initLocals;
    ValueFn memUsed, bytesPerLine, initLineArray;
    VoidCreateFn internalCreate;
};

struct Object {
    std::uint8_t bytes[0x40];
};

std::uint32_t RunPaintlib(const PaintlibFunctions& f, int op, Object& o, std::uint8_t* palette,
                          std::vector<std::uint32_t>& calls, const std::uint32_t* v) {
    g_calls = &calls;
    g_virtualResult = v[5];
    auto* bmp = reinterpret_cast<CBmp*>(o.bytes);
    std::uint32_t result = 0xdeadbeef;
    switch (op) {
        case 0: result = static_cast<std::uint32_t>(f.calcDestBpp(&o, nullptr, static_cast<std::int32_t>(v[0]))); break;
        case 1: result = f.picConstruct(&o, nullptr) == &o; break;
        case 2: result = f.bmpConstruct(&o, nullptr) == &o; break;
        case 3: result = f.dataConstruct(&o, nullptr) == &o; break;
        case 4: f.bmpDestroy(&o, nullptr); break;
        case 5: result = f.bmpDeleting(&o, nullptr, 0) == &o; break;
        case 6:
            bmp->vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFakeVtable));
            result = f.create(&o, nullptr, static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1]), v[2], v[3]);
            break;
        case 7:
            bmp->vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFakeVtable));
            bmp->palette = palette;
            f.initLocals(&o, nullptr, static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1]), v[2], v[3]);
            bmp->palette = nullptr;
            break;
        case 8: result = f.memUsed(&o, nullptr); break;
        case 9: result = f.bytesPerLine(&o, nullptr); break;
        case 10: {
            bmp->vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFakeVtable));
            bmp->bits = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(v[4] & ~3u));
            bmp->height = static_cast<std::int32_t>(v[1] % 300);
            result = f.initLineArray(&o, nullptr);
            // Row pointers relative to the bits; then the array goes back.
            for (std::int32_t row = 0; row < bmp->height; ++row) {
                calls.push_back(static_cast<std::uint32_t>(bmp->lines[row] - bmp->bits));
            }
            GameOperatorDelete(bmp->lines);
            bmp->lines = nullptr;
            break;
        }
        default: {
            bmp->vtable = game::vtables::kCAnyBmp;  // initLocals' virtual call: the real initLineArray
            const std::uint32_t depth = v[2] % 3 == 0 ? 8 : (v[2] % 3 == 1 ? 24 : 32);
            f.internalCreate(&o, nullptr, static_cast<std::int32_t>(v[0] % 64 + 1), static_cast<std::int32_t>(v[1] % 64 + 1),
                             depth, v[3]);
            calls.push_back(bmp->palette != nullptr);
            calls.push_back(bmp->bits != nullptr);
            if (bmp->palette != nullptr) {
                calls.insert(calls.end(), reinterpret_cast<std::uint32_t*>(bmp->palette),
                             reinterpret_cast<std::uint32_t*>(bmp->palette) + 256);
            }
            for (std::int32_t row = 0; row < bmp->height; ++row) {
                calls.push_back(static_cast<std::uint32_t>(bmp->lines[row] - bmp->bits));
            }
            GameOperatorDelete(bmp->bits);
            GameOperatorDelete(bmp->palette);
            GameOperatorDelete(bmp->lines);
            bmp->bits = nullptr;
            bmp->palette = nullptr;
            bmp->lines = nullptr;
            break;
        }
    }
    g_calls = nullptr;
    return result;
}

void ComparePaintlib(test::Context& ctx, const PaintlibFunctions& candidate, int cases) {
    const PaintlibFunctions original{Original_CPicDecoder_CalcDestBPP, Original_CPicDecoder_Construct,
                                     Original_CBmp_Construct,          Original_CDataSource_Construct,
                                     Original_CBmp_Destroy,            Original_CBmp_DeletingDestructor,
                                     Original_CBmp_Create,             Original_CBmp_initLocals,
                                     Original_CAnyBmp_GetMemUsed,      Original_CAnyBmp_GetBytesPerLine,
                                     Original_CAnyBmp_initLineArray,   Original_CAnyBmp_internalCreate};
    std::vector<std::uint8_t> paletteA(0x400), paletteB(0x400);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(12));
        Object a{};
        ctx.rng().Fill(&a, sizeof(a));
        std::uint32_t v[6];
        for (auto& value : v) {
            value = ctx.rng().Next32();
        }
        if (ctx.rng().Chance(1, 2)) {
            v[0] = static_cast<std::uint32_t>(ctx.rng().Range(-3, 40));
            v[2] = static_cast<std::uint32_t>(ctx.rng().Range(0, 33));
        }
        if (op == 0 && ctx.rng().Chance(1, 2)) {
            reinterpret_cast<CPicDecoder*>(a.bytes)->dest_bpp = 0;
        }
        if (op == 8 || op == 9) {
            reinterpret_cast<CBmp*>(a.bytes)->bpp = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Range(0, 40) : v[4]);
        }
        ctx.rng().Fill(paletteA.data(), paletteA.size());
        paletteB = paletteA;
        Object b = a;
        std::vector<std::uint32_t> callsA, callsB;
        const std::uint32_t ra = RunPaintlib(original, op, a, paletteA.data(), callsA, v);
        const std::uint32_t rb = RunPaintlib(candidate, op, b, paletteB.data(), callsB, v);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(callsA == callsB);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        same &= S3RE_EXPECT_MEM_EQ(paletteA.data(), paletteB.data(), paletteA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: GetMemUsed without the second header.
std::uint32_t __fastcall WrongMemUsed(void* self, void* edx) {
    return CAnyBmp_GetMemUsed(self, edx) - kHeaderBytes;
}

// The destructors and the functions that free: run on objects whose
// pointers come from the game's heap; afterwards the objects must match.
struct FreeingFunctions {
    VoidThisFn picDestroy;
    DeletingFn picDeleting;
    VoidThisFn anyDestroy, freeMembers;
    DeletingFn anyDeleting;
    VoidThisFn textDestroy;
    DeletingFn textDeleting;
    VoidThisFn dataDestroy;
    DeletingFn dataDeleting;
    AttachFn attach;
    ValueFn release;
    ThisFn anyConstruct;
};

// The heap pointers an op's object holds (offsets), set from the game's heap.
std::vector<std::size_t> HeapFields(int op) {
    switch (op) {
        case 2: case 3: case 4: return {0x10, 0x14, 0x1c};
        case 5: case 6: return {0x08};
        case 7: case 8: case 10: return {0x0c};
        default: return {};
    }
}

void FillHeapPointers(Object& o, int op, bool nullPalette, bool nullData) {
    for (const std::size_t offset : HeapFields(op)) {
        void* block = GameOperatorNew(16);
        std::memcpy(o.bytes + offset, &block, 4);
    }
    if ((op >= 2 && op <= 4) && nullPalette) {
        GameOperatorDelete(reinterpret_cast<CBmp*>(o.bytes)->palette);
        reinterpret_cast<CBmp*>(o.bytes)->palette = nullptr;
    }
    if ((op == 7 || op == 8 || op == 10) && nullData) {
        reinterpret_cast<CDataSource*>(o.bytes)->data = nullptr;
    }
}

// After a run: a heap pointer that is still there becomes a marker (the two
// runs had different blocks); one that was cleared stays 0.
void NormalizeHeapPointers(Object& o, const Object& before, int op) {
    for (const std::size_t offset : HeapFields(op)) {
        if (std::memcmp(o.bytes + offset, before.bytes + offset, 4) == 0) {
            const std::uint32_t marker = 0xaaaa0000u + static_cast<std::uint32_t>(offset);
            std::memcpy(o.bytes + offset, &marker, 4);
        }
    }
}

std::uint32_t RunFreeing(const FreeingFunctions& f, int op, Object& o, const char* name, std::uint32_t v) {
    auto* bmp = reinterpret_cast<CBmp*>(o.bytes);
    auto* source = reinterpret_cast<CDataSource*>(o.bytes);
    std::uint32_t result = 0;
    switch (op) {
        case 0: f.picDestroy(&o, nullptr); break;
        case 1: result = f.picDeleting(&o, nullptr, 0) == &o; break;
        case 2: f.anyDestroy(&o, nullptr); break;
        case 3: f.freeMembers(&o, nullptr); break;
        case 4: result = f.anyDeleting(&o, nullptr, 0) == &o; break;
        case 5: f.textDestroy(&o, nullptr); break;
        case 6: result = f.textDeleting(&o, nullptr, 0) == &o; break;
        case 7: f.dataDestroy(&o, nullptr); break;
        case 8: result = f.dataDeleting(&o, nullptr, 0) == &o; break;
        case 9: {
            auto* data = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(v));
            result = f.attach(&o, nullptr, name, data, v ^ 0x1234) == data;
            result |= (std::strcmp(source->name, name) == 0) << 1;
            GameOperatorDelete(source->name);
            source->name = nullptr;
            break;
        }
        case 10: result = f.release(&o, nullptr); break;
        default:
            result = f.anyConstruct(&o, nullptr) == &o;
            result |= (bmp->bits != nullptr) << 1;
            for (int row = 0; row < 16; ++row) {
                result += static_cast<std::uint32_t>(bmp->lines[row] - bmp->bits) * (row + 3);
            }
            result ^= *reinterpret_cast<std::uint32_t*>(bmp->palette + 4 * 77);  // a grey palette entry
            GameOperatorDelete(bmp->bits);
            GameOperatorDelete(bmp->palette);
            GameOperatorDelete(bmp->lines);
            bmp->bits = nullptr;
            bmp->palette = nullptr;
            bmp->lines = nullptr;
            break;
    }
    return result;
}

}  // namespace

S3RE_TEST(Paintlib_MatchOriginal) {
    ComparePaintlib(ctx,
                    {&CPicDecoder_CalcDestBPP, &CPicDecoder_Construct, &CBmp_Construct, &CDataSource_Construct,
                     &CBmp_Destroy, &CBmp_DeletingDestructor, &CBmp_Create, &CBmp_initLocals, &CAnyBmp_GetMemUsed,
                     &CAnyBmp_GetBytesPerLine, &CAnyBmp_initLineArray, &CAnyBmp_internalCreate},
                    6000);
}

S3RE_TEST(Paintlib_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePaintlib(probe,
                    {&CPicDecoder_CalcDestBPP, &CPicDecoder_Construct, &CBmp_Construct, &CDataSource_Construct,
                     &CBmp_Destroy, &CBmp_DeletingDestructor, &CBmp_Create, &CBmp_initLocals, &WrongMemUsed,
                     &CAnyBmp_GetBytesPerLine, &CAnyBmp_initLineArray, &CAnyBmp_internalCreate},
                    6000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(Paintlib_FreeingMatchOriginal) {
    const FreeingFunctions original{Original_CPicDecoder_Destroy,     Original_CPicDecoder_DeletingDestructor,
                                    Original_CAnyBmp_Destroy,         Original_CAnyBmp_freeMembers,
                                    Original_CAnyBmp_DeletingDestructor, Original_CTextException_Destroy,
                                    Original_CTextException_DeletingDestructor, Original_CDataSource_Destroy,
                                    Original_CDataSource_DeletingDestructor, Original_CDataSource_Attach,
                                    Original_CDataSource_Release,     Original_CAnyBmp_Construct};
    const FreeingFunctions ours{&CPicDecoder_Destroy,     &CPicDecoder_DeletingDestructor, &CAnyBmp_Destroy,
                                &CAnyBmp_freeMembers,     &CAnyBmp_DeletingDestructor,     &CTextException_Destroy,
                                &CTextException_DeletingDestructor, &CDataSource_Destroy, &CDataSource_DeletingDestructor,
                                &CDataSource_Attach,      &CDataSource_Release,            &CAnyBmp_Construct};
    char* const savedTrace = g_paintlibTraceFile;
    for (int i = 0; i < 2000; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(12));
        Object a{};
        ctx.rng().Fill(&a, sizeof(a));
        char name[40];
        ctx.rng().Fill(name, sizeof(name));
        name[ctx.rng().Below(39)] = '\0';
        const std::uint32_t v = ctx.rng().Next32();
        Object b = a;
        const bool trace = ctx.rng().Chance(1, 2);
        const bool nullPalette = ctx.rng().Chance(1, 4);
        const bool nullData = ctx.rng().Chance(1, 3);
        FillHeapPointers(a, op, nullPalette, nullData);
        FillHeapPointers(b, op, nullPalette, nullData);
        const Object beforeA = a;
        const Object beforeB = b;
        g_paintlibTraceFile = trace ? static_cast<char*>(GameOperatorNew(8)) : nullptr;
        const std::uint32_t ra = RunFreeing(original, op, a, name, v);
        const bool traceA = g_paintlibTraceFile != nullptr;
        GameOperatorDelete(g_paintlibTraceFile);
        g_paintlibTraceFile = trace ? static_cast<char*>(GameOperatorNew(8)) : nullptr;
        const std::uint32_t rb = RunFreeing(ours, op, b, name, v);
        const bool traceB = g_paintlibTraceFile != nullptr;
        GameOperatorDelete(g_paintlibTraceFile);
        g_paintlibTraceFile = nullptr;
        NormalizeHeapPointers(a, beforeA, op);
        NormalizeHeapPointers(b, beforeB, op);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_EQ(traceA, traceB);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    g_paintlibTraceFile = savedTrace;
}

}  // namespace s3re::thirdparty
