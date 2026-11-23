// Lobby::CMemoryBlock, the growable byte buffer of the lobby/network
// protocol (Dirk\protocol.cpp), and the members of Lobby::CLMGeneric (a
// protocol message: a CMemoryBlock at +4 whose first dword is the header)
// that only use it. All thiscall; names are ours where the RTTI has none.
//
// CMemoryBlock (0x10 bytes): +0 vtable 0x65d4f4, +4 data (operator new),
// +8 the size last reserved (not rounded; see Reserve), +0xc used size.
//
// Size (0x00601270): () EAX = +0xc.
// Reserve (0x00601280): (n) ret 4. n == 0: FreeBlock (0x601250: delete the
//   data, +4 = +8 = +0xc = 0). Otherwise the capacity is n rounded up to 16;
//   when that equals +8 nothing happens (sic: +8 holds the unrounded n, so
//   only sizes that are multiples of 16 are ever skipped). New block of
//   that capacity (null: ThrowOutOfMemory("lpNewData", "E:\Siedler3\Dirk\
//   protocol.cpp", 0xc2)); the old data, min(n, used) bytes, is copied and
//   freed; +4 = new block, +8 = n. The used size is not changed, even if it
//   is now larger than n. A catch(...) logs "PROT: During Resize" and
//   rethrows. EAX: not used by callers.
// Append (0x00601170): (const CMemoryBlock& other) ret 4: if other's used
//   size is not 0: Reserve(used + other.used), copy other's bytes after
//   ours, used += other.used.
// AppendBytes (0x006011c0): (const void* p, n) ret 8: if n: Reserve(used +
//   n), copy, used += n. catch(...): "PROT: During Append", rethrow.
// At (0x00600ee0): (index) ret 4: if index == used: Reserve(used + 1),
//   used += 1. EAX = data + index.
// Equal (0x00600ea0): (const CMemoryBlock& other) ret 4: AL = same used size
//   and same bytes. The upper bytes of EAX are those of `this` when the
//   sizes differ or are both 0 (the original computes in EAX = this), else 0.
// CMemoryBlock copy constructor (0x00600e40): (other) ret 4: +4, +8, +0xc =
//   0, vtable, Append(other). EAX = this.
// ~CMemoryBlock (0x00600e70): vtable 0x65d4f4, then FreeBlock (EAX = 0).
//
// CLMGeneric (vtable 0x65d52c): +4 the message bytes (a CMemoryBlock).
// SetHeader (0x00603270): (dword header) ret 4: if the block is empty,
//   AppendBytes(&header, 4), else the four bytes go to positions 0..3 with
//   At(). EAX: not used by callers.
// CLMGeneric(id) (0x00601e80): (id) ret 4: CMemoryBlock() at +4, vtable,
//   SetHeader(id | 0 << 8 | 4 << 16) (message id, a zero byte, the size 4 as
//   a word). EAX = this.
// Equal (0x006027d0): (const CLMGeneric& other) ret 4: the blocks' Equal.
// ~CLMGeneric (0x00602190): vtable 0x65d52c, then ~CMemoryBlock of +4.
//
// The catch(...) blocks of Reserve and AppendBytes are an SEH filter here
// that makes the same DebugLog call and lets the exception pass on (the
// original logs in the handler after unwinding and rethrows; DebugLog is
// silent in the release build).

#include "game/debug_log.h"
#include "game/framework/framework_layout.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace s3re::game {

// framework_helpers.cpp
std::uint32_t __fastcall Lobby_CMemoryBlock_Construct(void* self, void* edx);
std::uint32_t __fastcall Lobby_FreeBlock_601250(void* self, void* edx);
// win32_errors.cpp
void __fastcall Win32_ThrowOutOfMemory(const char* text, const char* file, std::int32_t line);

namespace {


using framework::CLMGeneric;
using framework::CMemoryBlock;

CMemoryBlock* Block(void* self) {
    return static_cast<CMemoryBlock*>(self);
}

const CMemoryBlock* Block(const void* self) {
    return static_cast<const CMemoryBlock*>(self);
}

CLMGeneric* Generic(void* self) {
    return static_cast<CLMGeneric*>(self);
}

const CLMGeneric* Generic(const void* self) {
    return static_cast<const CLMGeneric*>(self);
}

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

void GameDelete(void* block) {
    reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(block);
}

int LogAndPass(const GameText& text) {
    GameDebugLog(text);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

using SizeFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using OneArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ObjArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* other);
using AppendBytesFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* p, std::uint32_t n);
using AtFn = std::uint8_t*(__fastcall*)(void* self, void* edx, std::uint32_t index);

std::uint32_t __fastcall Lobby_MemoryBlock_Size(void* self, void* edx);
std::uint32_t __fastcall Lobby_MemoryBlock_Reserve(void* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall Lobby_MemoryBlock_Append(void* self, void* edx, const void* other);
std::uint32_t __fastcall Lobby_MemoryBlock_AppendBytes(void* self, void* edx, const void* p, std::uint32_t n);
std::uint8_t* __fastcall Lobby_MemoryBlock_At(void* self, void* edx, std::uint32_t index);
std::uint32_t __fastcall Lobby_MemoryBlock_Equal(void* self, void* edx, const void* other);
std::uint32_t __fastcall Lobby_MemoryBlock_Copy(void* self, void* edx, const void* other);
std::uint32_t __fastcall Lobby_MemoryBlock_Destroy(void* self, void* edx);
std::uint32_t __fastcall Lobby_CLMGeneric_SetHeader(void* self, void* edx, std::uint32_t header);
std::uint32_t __fastcall Lobby_CLMGeneric_Construct(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall Lobby_CLMGeneric_Equal(void* self, void* edx, const void* other);
std::uint32_t __fastcall Lobby_CLMGeneric_Destroy(void* self, void* edx);

S3RE_HOOK(0x00601270, Lobby_MemoryBlock_Size, SizeFn, Original_Lobby_MemoryBlock_Size, Lobby_MemoryBlock_Size);

// Purpose: The number of bytes in a protocol buffer.
std::uint32_t __fastcall Lobby_MemoryBlock_Size(void* self, void* /*edx*/) {
    return Block(self)->used;
}

namespace {

void Reserve(void* self, std::uint32_t n) {
    if (n == 0) {
        Lobby_FreeBlock_601250(self, nullptr);
        return;
    }
    const std::uint32_t capacity = (((n - 1) >> 4) + 1) << 4;
    if (capacity == Block(self)->reserved) {
        return;
    }
    auto* block = static_cast<std::uint8_t*>(GameNew(capacity));
    if (block == nullptr) {
        Win32_ThrowOutOfMemory(texts::kNewDataName, texts::kProtocolFile, 0xc2);
    }
    auto* old = Block(self)->data;
    if (old != nullptr) {
        const std::uint32_t used = Block(self)->used;
        std::memcpy(block, old, used < n ? used : n);
        GameDelete(old);
    }
    Block(self)->data = block;
    Block(self)->reserved = n;
}

void AppendBytes(void* self, const void* p, std::uint32_t n) {
    if (n != 0) {
        Lobby_MemoryBlock_Reserve(self, nullptr, Block(self)->used + n);
        std::memcpy(Block(self)->data + Block(self)->used, p, n);
        Block(self)->used += n;
    }
}

}  // namespace

S3RE_HOOK(0x00601280, Lobby_MemoryBlock_Reserve, OneArgFn, Original_Lobby_MemoryBlock_Reserve,
          Lobby_MemoryBlock_Reserve);

// Purpose: Makes a protocol buffer big enough for n bytes (moving the data
// to a new block), or frees it for 0.
std::uint32_t __fastcall Lobby_MemoryBlock_Reserve(void* self, void* /*edx*/, std::uint32_t n) {
    __try {
        Reserve(self, n);
    } __except (LogAndPass(texts::kDuringResize)) {
    }
    return 0;
}

S3RE_HOOK(0x00601170, Lobby_MemoryBlock_Append, ObjArgFn, Original_Lobby_MemoryBlock_Append,
          Lobby_MemoryBlock_Append);

// Purpose: Appends the bytes of another protocol buffer.
std::uint32_t __fastcall Lobby_MemoryBlock_Append(void* self, void* /*edx*/, const void* other) {
    const std::uint32_t n = Block(other)->used;
    if (n != 0) {
        Lobby_MemoryBlock_Reserve(self, nullptr, Block(self)->used + n);
        std::memcpy(Block(self)->data + Block(self)->used, Block(other)->data, n);
        Block(self)->used += n;
    }
    return 0;
}

S3RE_HOOK(0x006011c0, Lobby_MemoryBlock_AppendBytes, AppendBytesFn, Original_Lobby_MemoryBlock_AppendBytes,
          Lobby_MemoryBlock_AppendBytes);

// Purpose: Appends n bytes to a protocol buffer (building a message).
std::uint32_t __fastcall Lobby_MemoryBlock_AppendBytes(void* self, void* /*edx*/, const void* p, std::uint32_t n) {
    __try {
        AppendBytes(self, p, n);
    } __except (LogAndPass(texts::kDuringAppend)) {
    }
    return 0;
}

S3RE_HOOK(0x00600ee0, Lobby_MemoryBlock_At, AtFn, Original_Lobby_MemoryBlock_At, Lobby_MemoryBlock_At);

// Purpose: The address of byte `index` of a protocol buffer; writing just
// past the end grows it by one byte.
std::uint8_t* __fastcall Lobby_MemoryBlock_At(void* self, void* /*edx*/, std::uint32_t index) {
    const std::uint32_t used = Block(self)->used;
    if (index == used) {
        Lobby_MemoryBlock_Reserve(self, nullptr, used + 1);
        Block(self)->used = used + 1;
    }
    return Block(self)->data + index;
}

S3RE_HOOK(0x00600ea0, Lobby_MemoryBlock_Equal, ObjArgFn, Original_Lobby_MemoryBlock_Equal, Lobby_MemoryBlock_Equal);

// Purpose: Compares two protocol buffers byte by byte (e.g. two messages).
std::uint32_t __fastcall Lobby_MemoryBlock_Equal(void* self, void* /*edx*/, const void* other) {
    const auto thisBits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xFFFFFF00u;
    const std::uint32_t n = Block(self)->used;
    if (n != Block(other)->used) {
        return thisBits;
    }
    if (n == 0) {
        return thisBits | 1;
    }
    return std::memcmp(Block(self)->data, Block(other)->data, n) == 0 ? 1 : 0;
}

S3RE_HOOK(0x00600e40, Lobby_MemoryBlock_Copy, ObjArgFn, Original_Lobby_MemoryBlock_Copy, Lobby_MemoryBlock_Copy);

// Purpose: Copy constructor of a protocol buffer.
std::uint32_t __fastcall Lobby_MemoryBlock_Copy(void* self, void* /*edx*/, const void* other) {
    Block(self)->data = nullptr;
    Block(self)->reserved = 0;
    Block(self)->used = 0;
    Block(self)->vtable = vtables::kCMemoryBlock;
    Lobby_MemoryBlock_Append(self, nullptr, other);
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

S3RE_HOOK(0x00600e70, Lobby_MemoryBlock_Destroy, SizeFn, Original_Lobby_MemoryBlock_Destroy,
          Lobby_MemoryBlock_Destroy);

// Purpose: Destructor of a protocol buffer (frees its data).
std::uint32_t __fastcall Lobby_MemoryBlock_Destroy(void* self, void* /*edx*/) {
    Block(self)->vtable = vtables::kCMemoryBlock;
    return Lobby_FreeBlock_601250(self, nullptr);
}

S3RE_HOOK(0x00603270, Lobby_CLMGeneric_SetHeader, OneArgFn, Original_Lobby_CLMGeneric_SetHeader,
          Lobby_CLMGeneric_SetHeader);

// Purpose: Writes a protocol message's header dword (message id and size)
// at the start of its bytes.
std::uint32_t __fastcall Lobby_CLMGeneric_SetHeader(void* self, void* /*edx*/, std::uint32_t header) {
    void* block = &Generic(self)->block;
    if (Lobby_MemoryBlock_Size(block, nullptr) == 0) {
        return Lobby_MemoryBlock_AppendBytes(block, nullptr, &header, 4);
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        *Lobby_MemoryBlock_At(block, nullptr, i) = static_cast<std::uint8_t>(header >> (8 * i));
    }
    return 0;
}

S3RE_HOOK(0x00601e80, Lobby_CLMGeneric_Construct, OneArgFn, Original_Lobby_CLMGeneric_Construct,
          Lobby_CLMGeneric_Construct);

// Purpose: Constructor of a protocol message with the given message id and
// an empty body (header only).
std::uint32_t __fastcall Lobby_CLMGeneric_Construct(void* self, void* /*edx*/, std::uint32_t id) {
    Lobby_CMemoryBlock_Construct(&Generic(self)->block, nullptr);
    const std::uint32_t header = (id & 0xFFu) | (4u << 16);
    Generic(self)->vtable = vtables::kCLMGeneric;
    Lobby_CLMGeneric_SetHeader(self, nullptr, header);
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

S3RE_HOOK(0x006027d0, Lobby_CLMGeneric_Equal, ObjArgFn, Original_Lobby_CLMGeneric_Equal, Lobby_CLMGeneric_Equal);

// Purpose: Compares two protocol messages byte by byte.
std::uint32_t __fastcall Lobby_CLMGeneric_Equal(void* self, void* /*edx*/, const void* other) {
    return Lobby_MemoryBlock_Equal(&Generic(self)->block, nullptr, &Generic(other)->block);
}

S3RE_HOOK(0x00602190, Lobby_CLMGeneric_Destroy, SizeFn, Original_Lobby_CLMGeneric_Destroy, Lobby_CLMGeneric_Destroy);

// Purpose: Destructor of a protocol message.
std::uint32_t __fastcall Lobby_CLMGeneric_Destroy(void* self, void* /*edx*/) {
    Generic(self)->vtable = vtables::kCLMGeneric;
    return Lobby_MemoryBlock_Destroy(&Generic(self)->block, nullptr);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Random sequences of operations on two sets of four messages (CLMGeneric:
// vtable, block), one set through the originals and one through ours, on
// the game's heap. After every step: EAX where callers use it, the vtables,
// the +8 / +0xc fields, whether data is null, and the used bytes of every
// block (the data pointers themselves differ).

namespace {

struct Ops {
    SizeFn size;
    OneArgFn reserve;
    ObjArgFn append;
    AppendBytesFn appendBytes;
    AtFn at;
    ObjArgFn equal;
    ObjArgFn copy;
    SizeFn destroy;
    OneArgFn setHeader;
    OneArgFn construct;
    ObjArgFn genericEqual;
    SizeFn genericDestroy;
};

Ops OriginalOps() {
    return {Original_Lobby_MemoryBlock_Size,     Original_Lobby_MemoryBlock_Reserve,
            Original_Lobby_MemoryBlock_Append,   Original_Lobby_MemoryBlock_AppendBytes,
            Original_Lobby_MemoryBlock_At,       Original_Lobby_MemoryBlock_Equal,
            Original_Lobby_MemoryBlock_Copy,     Original_Lobby_MemoryBlock_Destroy,
            Original_Lobby_CLMGeneric_SetHeader, Original_Lobby_CLMGeneric_Construct,
            Original_Lobby_CLMGeneric_Equal,     Original_Lobby_CLMGeneric_Destroy};
}

Ops OurOps() {
    return {&Lobby_MemoryBlock_Size,     &Lobby_MemoryBlock_Reserve, &Lobby_MemoryBlock_Append,
            &Lobby_MemoryBlock_AppendBytes, &Lobby_MemoryBlock_At,   &Lobby_MemoryBlock_Equal,
            &Lobby_MemoryBlock_Copy,     &Lobby_MemoryBlock_Destroy, &Lobby_CLMGeneric_SetHeader,
            &Lobby_CLMGeneric_Construct, &Lobby_CLMGeneric_Equal,    &Lobby_CLMGeneric_Destroy};
}

constexpr int kMessages = 4;

struct Message {
    std::uint32_t vtable;
    std::uint32_t blockVtable;
    std::uint8_t* data;
    std::uint32_t reserved;
    std::uint32_t used;
};
static_assert(sizeof(Message) == 0x14, "CLMGeneric layout");

void* BlockOf(Message& m) {
    return &m.blockVtable;
}

bool SameMessages(test::Context& ctx, const Message* a, const Message* b) {
    bool same = true;
    for (int i = 0; i < kMessages && same; ++i) {
        same &= S3RE_EXPECT_EQ(a[i].vtable, b[i].vtable);
        same &= S3RE_EXPECT_EQ(a[i].blockVtable, b[i].blockVtable);
        same &= S3RE_EXPECT_EQ(a[i].reserved, b[i].reserved);
        same &= S3RE_EXPECT_EQ(a[i].used, b[i].used);
        same &= S3RE_EXPECT_EQ(a[i].data == nullptr, b[i].data == nullptr);
        if (same && a[i].data != nullptr) {
            // Only up to the capacity: Reserve can leave used > reserved.
            const std::uint32_t capacity = (((a[i].reserved - 1) >> 4) + 1) << 4;
            const std::uint32_t n = a[i].used < capacity ? a[i].used : capacity;
            same &= S3RE_EXPECT_MEM_EQ(a[i].data, b[i].data, n);
        }
    }
    return same;
}

void CompareBlocks(test::Context& ctx, const Ops& candidate, int sequences) {
    const Ops original = OriginalOps();
    std::uint8_t bytes[64];
    for (int sequence = 0; sequence < sequences; ++sequence) {
        Message a[kMessages] = {};
        Message b[kMessages] = {};
        for (int i = 0; i < kMessages; ++i) {
            const std::uint32_t id = ctx.rng().Next32();
            original.construct(&a[i], nullptr, id);
            candidate.construct(&b[i], nullptr, id);
        }
        for (int step = 0; step < 30; ++step) {
            const int op = static_cast<int>(ctx.rng().Below(11));
            const int i = static_cast<int>(ctx.rng().Below(kMessages));
            const int j = static_cast<int>(ctx.rng().Below(kMessages));
            const std::uint32_t n = ctx.rng().Chance(1, 4) ? ctx.rng().Below(4) * 16 : ctx.rng().Below(60);
            ctx.rng().Fill(bytes, sizeof(bytes));
            std::uint32_t ra = 0, rb = 0;
            bool compareResult = true;
            switch (op) {
                case 0:
                    ra = original.size(BlockOf(a[i]), nullptr);
                    rb = candidate.size(BlockOf(b[i]), nullptr);
                    break;
                case 1:
                    if (n < a[i].used) {
                        // Shrinking below the used size makes later appends
                        // write past the block (the original's own bug).
                        continue;
                    }
                    original.reserve(BlockOf(a[i]), nullptr, n);
                    candidate.reserve(BlockOf(b[i]), nullptr, n);
                    compareResult = false;
                    break;
                case 2:
                    if (i == j) {
                        continue;  // appending a block to itself reads freed memory in the original
                    }
                    original.append(BlockOf(a[i]), nullptr, BlockOf(a[j]));
                    candidate.append(BlockOf(b[i]), nullptr, BlockOf(b[j]));
                    compareResult = false;
                    break;
                case 3:
                    original.appendBytes(BlockOf(a[i]), nullptr, bytes, n);
                    candidate.appendBytes(BlockOf(b[i]), nullptr, bytes, n);
                    compareResult = false;
                    break;
                case 4: {
                    const std::uint32_t index = ctx.rng().Below(a[i].used + 1);
                    std::uint8_t* pa = original.at(BlockOf(a[i]), nullptr, index);
                    std::uint8_t* pb = candidate.at(BlockOf(b[i]), nullptr, index);
                    ra = static_cast<std::uint32_t>(pa - a[i].data);
                    rb = static_cast<std::uint32_t>(pb - b[i].data);
                    *pa = *pb = bytes[0];
                    break;
                }
                case 5:
                    ra = original.equal(BlockOf(a[i]), nullptr, BlockOf(a[j]));
                    rb = candidate.equal(BlockOf(b[i]), nullptr, BlockOf(b[j]));
                    // Upper bytes: those of `this` on some paths.
                    ra = (ra & 0xFFu) | ((ra & 0xFFFFFF00u) == (reinterpret_cast<std::uintptr_t>(BlockOf(a[i])) &
                                                                 0xFFFFFF00u) ? 0x100u : 0u);
                    rb = (rb & 0xFFu) | ((rb & 0xFFFFFF00u) == (reinterpret_cast<std::uintptr_t>(BlockOf(b[i])) &
                                                                 0xFFFFFF00u) ? 0x100u : 0u);
                    break;
                case 6:
                    ra = original.genericEqual(&a[i], nullptr, &a[j]) & 0xFFu;
                    rb = candidate.genericEqual(&b[i], nullptr, &b[j]) & 0xFFu;
                    break;
                case 7:
                    if (i == j) {
                        continue;
                    }
                    original.destroy(BlockOf(a[i]), nullptr);
                    candidate.destroy(BlockOf(b[i]), nullptr);
                    ra = original.copy(BlockOf(a[i]), nullptr, BlockOf(a[j])) ==
                         reinterpret_cast<std::uintptr_t>(BlockOf(a[i]));
                    rb = candidate.copy(BlockOf(b[i]), nullptr, BlockOf(b[j])) ==
                         reinterpret_cast<std::uintptr_t>(BlockOf(b[i]));
                    break;
                case 8: {
                    const std::uint32_t header = ctx.rng().Next32();
                    original.setHeader(&a[i], nullptr, header);
                    candidate.setHeader(&b[i], nullptr, header);
                    compareResult = false;
                    break;
                }
                case 9: {
                    // Destroy and rebuild a message (EAX of the destructor
                    // is FreeBlock's 0; the constructor returns this).
                    const std::uint32_t id = ctx.rng().Next32();
                    ra = original.genericDestroy(&a[i], nullptr);
                    rb = candidate.genericDestroy(&b[i], nullptr);
                    ra |= original.construct(&a[i], nullptr, id) == reinterpret_cast<std::uintptr_t>(&a[i]) ? 2 : 0;
                    rb |= candidate.construct(&b[i], nullptr, id) == reinterpret_cast<std::uintptr_t>(&b[i]) ? 2 : 0;
                    break;
                }
                default:
                    ra = original.destroy(BlockOf(a[i]), nullptr);
                    rb = candidate.destroy(BlockOf(b[i]), nullptr);
                    break;
            }
            bool same = !compareResult || S3RE_EXPECT_EQ(ra, rb);
            same &= SameMessages(ctx, a, b);
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  sequence %d step %d: op %d i=%d j=%d n=%u", sequence, step, op, i, j,
                         n);
                return;
            }
        }
        for (int i = 0; i < kMessages; ++i) {
            original.genericDestroy(&a[i], nullptr);
            candidate.genericDestroy(&b[i], nullptr);
        }
    }
}

std::uint32_t __fastcall WrongReserve(void* self, void* edx, std::uint32_t n) {
    Lobby_MemoryBlock_Reserve(self, edx, n);
    if (n != 0) {
        Block(self)->reserved = (((n - 1) >> 4) + 1) << 4;  // wrong: stores the rounded size
    }
    return 0;
}

}  // namespace

S3RE_TEST(LobbyMemoryBlock_MatchOriginal) {
    CompareBlocks(ctx, OurOps(), 3000);
}

S3RE_TEST(LobbyMemoryBlock_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Ops wrong = OurOps();
    wrong.reserve = &WrongReserve;
    CompareBlocks(probe, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
