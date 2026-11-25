#pragma once

// The Watcom vector classes (WCBareVectorBase<T> and the classes built on
// it: WCVectorBase, WCOrderedVectorBase, WCSortedVectorBase,
// WCValOrderedVector, WCPtrSortedVector, ...), as the game's many template
// instantiations compile them. Every instantiation has its own copy of the
// same code (own exception-unwind tables, so the linker didn't fold them);
// only the vtables and the element size differ. Layout:
//   +0x00 vtable
//   +0x04 0 after construction (not used by this code)
//   +0x08 data: the element block from the game's operator new
//   +0x0c capacity (elements allocated)
//   +0x10 filled: elements that hold a constructed value (signed)
//   +0x14 length (WCVectorBase and up; set to 0 by their constructors)
//   +0x18 grow: the growth step (WCVectorBase and up; a constructor argument)
// WCExcept (vtable 0x655438) is the root class; every destructor ends by
// storing its vtable. The library's error hooks are the empty function
// 0x59f200 (`ret`) in this build, and the destructors call it before
// freeing a block; we leave that call out.

#include "msvc6/game_heap.h"

#include <cstdint>

namespace s3re::watcom {

#pragma pack(push, 1)
struct VectorBase {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t data;  // address of the element block (0: none)
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;
    std::uint32_t grow;
};
#pragma pack(pop)
static_assert(sizeof(VectorBase) == 0x1c, "must match the game's layout");

// WCBareVectorBase<T>(capacity): +4 = 0, the vtable, capacity, filled = 0,
// and a block of capacity * elementSize bytes (32-bit product; none for
// capacity 0; capacity = 0 when operator new returns null). The elements
// are not constructed (filled stays 0).
inline VectorBase* ConstructBareVector(VectorBase* self, std::uint32_t capacity, std::uint32_t elementSize,
                                       std::uint32_t vtable) {
    self->unk04 = 0;
    self->vtable = vtable;
    self->capacity = capacity;
    self->filled = 0;
    if (capacity == 0) {
        self->data = 0;
        return self;
    }
    self->data = reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(capacity * elementSize));
    if (self->data == 0) {
        self->capacity = 0;
    }
    return self;
}

// WCVectorBase<T>(capacity, grow) and the classes above it: the bare vector,
// then length = 0, grow, and the class's own vtable.
inline VectorBase* ConstructVector(VectorBase* self, std::uint32_t capacity, std::uint32_t grow,
                                   std::uint32_t elementSize, std::uint32_t bareVtable, std::uint32_t vtable) {
    ConstructBareVector(self, capacity, elementSize, bareVtable);
    self->length = 0;
    self->grow = grow;
    self->vtable = vtable;
    return self;
}

// ~WCBareVectorBase<T>: the vtable, then, if there is a block, each of the
// `filled` elements destroyed by `destroyElement(address)` (filled and the
// block re-read per element, as the original does) and the block freed;
// then the root vtable. EAX: operator delete's, or 0 without a block.
template <typename DestroyElement>
std::uint32_t DestroyVector(VectorBase* self, std::uint32_t vtable, std::uint32_t rootVtable,
                            std::uint32_t elementSize, DestroyElement destroyElement) {
    self->vtable = vtable;
    std::uint32_t result = self->data;
    if (result != 0) {
        for (std::int32_t i = 0; i < self->filled; ++i) {
            destroyElement(self->data + static_cast<std::uint32_t>(i) * elementSize);
        }
        result = msvc6::GameOperatorDelete(reinterpret_cast<void*>(self->data));
    }
    self->vtable = rootVtable;
    return result;
}

}  // namespace s3re::watcom
