#pragma once

// The Watcom value vectors' operator[] as the compiler inlined it into the
// game settings' members (Dirk\GameParams.cpp; the same code as
// GameSettings::PlayerSetup 0x4351e0, see game_settings_players.cpp): an
// index in 0..length-1 (signed) is used as it is; on a non-empty vector
// an index below 0 becomes 0 and one above becomes length-1. On an empty
// vector element 0 is made: if the capacity is 0, a block of one element is
// allocated (operator new), the first min(filled, 1) elements (signed) are
// copy-constructed into it, all `filled` old elements destroyed, the old
// block freed, filled = min(filled, 1), capacity 1; if operator new fails,
// element 0 of the old block is returned. Then (also with a capacity)
// vtable slot 1 is called with 0 and the length set to 1. The library's
// empty error hooks (0x59f200) on the way are left out.

#include "msvc6/game_heap.h"
#include "watcom/vector_base.h"

#include <cstdint>

namespace s3re::game {

template <std::uint32_t ElementSize, std::uintptr_t CopyConstruct, std::uintptr_t Destruct>
std::uintptr_t InlinedVectorAt(watcom::VectorBase* vector, std::int32_t index) {
    using CopyFn = void*(__fastcall*)(void* self, void* edx, const void* from);
    using DestroyFn = void(__fastcall*)(void* self, void* edx);
    using NotifyFn = void(__fastcall*)(void* self, void* edx, std::uint32_t argument);
    auto at = [](std::uint32_t data, std::int32_t i) {
        return static_cast<std::uintptr_t>(data + static_cast<std::uint32_t>(i) * ElementSize);
    };
    const std::uint32_t length = vector->length;
    const auto last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return at(vector->data, index);
    }
    if (length != 0) {
        return at(vector->data, index < 0 ? 0 : last);
    }
    if (vector->capacity == 0) {
        const std::int32_t keep = vector->filled < 1 ? vector->filled : 1;
        void* block = msvc6::GameOperatorNew(ElementSize);
        if (block == nullptr) {
            return at(vector->data, 0);
        }
        const auto blockAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block));
        for (std::int32_t i = 0; i < keep; ++i) {
            const std::uintptr_t to = at(blockAddress, i);
            if (to != 0) {  // (placement new's check)
                reinterpret_cast<CopyFn>(CopyConstruct)(reinterpret_cast<void*>(to), nullptr,
                                                        reinterpret_cast<const void*>(at(vector->data, i)));
            }
        }
        for (std::int32_t i = 0; i < vector->filled; ++i) {
            reinterpret_cast<DestroyFn>(Destruct)(reinterpret_cast<void*>(at(vector->data, i)), nullptr);
        }
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(vector->data)));
        vector->filled = keep;
        vector->data = blockAddress;
        vector->capacity = 1;
        if (vector->length > 1) {  // (the length is 0 here, so never)
            vector->length = 1;
        }
    }
    const auto* vtable = reinterpret_cast<const NotifyFn*>(static_cast<std::uintptr_t>(vector->vtable));
    vtable[1](vector, nullptr, 0);
    vector->length = 1;
    return at(vector->data, 0);
}

// The variant that grows an empty vector without capacity through an
// out-of-line resize(1) (0x437d00 / 0x437820 / 0x4382a0 / 0x438070, by the
// element type): when that returns 0, element 0 of the old block; with
// `ClampLength`, a length above 1 afterwards becomes 1 (never: it is 0).
template <std::uint32_t ElementSize, std::uintptr_t Resize, bool ClampLength>
std::uintptr_t InlinedVectorAtResize(watcom::VectorBase* vector, std::int32_t index) {
    using ResizeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t length);
    using NotifyFn = void(__fastcall*)(void* self, void* edx, std::uint32_t argument);
    auto at = [vector](std::int32_t i) {
        return static_cast<std::uintptr_t>(vector->data + static_cast<std::uint32_t>(i) * ElementSize);
    };
    const std::uint32_t length = vector->length;
    const auto last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return at(index);
    }
    if (length != 0) {
        return at(index < 0 ? 0 : last);
    }
    if (vector->capacity == 0) {
        if (reinterpret_cast<ResizeFn>(Resize)(vector, nullptr, 1) == 0) {
            return at(0);
        }
        if (ClampLength && vector->length > 1) {
            vector->length = 1;
        }
    }
    const auto* vtable = reinterpret_cast<const NotifyFn*>(static_cast<std::uintptr_t>(vector->vtable));
    vtable[1](vector, nullptr, 0);
    vector->length = 1;
    return at(0);
}

}  // namespace s3re::game
