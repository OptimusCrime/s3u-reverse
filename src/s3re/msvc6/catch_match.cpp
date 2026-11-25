#include "msvc6/catch_match.h"

#include <cstring>

namespace s3re::msvc6 {

namespace {

constexpr DWORD kCxxExceptionCode = 0xe06d7363;
constexpr ULONG_PTR kCxxMagic = 0x19930520;

#pragma pack(push, 4)
struct CatchableType {
    std::uint32_t properties;
    const std::uint8_t* type;  // TypeDescriptor: vtable, spare, then the decorated name
    std::int32_t mdisp, pdisp, vdisp;
    std::int32_t size;
    void* copy;
};
struct CatchableTypeArray {
    std::int32_t count;
    const CatchableType* types[1];
};
struct ThrowInfo {
    std::uint32_t attributes;
    void* unwind;
    void* forward_compat;
    const CatchableTypeArray* catchable;
};
#pragma pack(pop)

bool IsType(const CatchableType* type, std::uintptr_t descriptor, const char* name) {
    if (reinterpret_cast<std::uintptr_t>(type->type) == descriptor) {
        return true;
    }
    return std::strcmp(reinterpret_cast<const char*>(type->type + 8), name) == 0;
}

void* AdjustedPointer(const EXCEPTION_RECORD* record, const CatchableType* type) {
    auto* thrown = *reinterpret_cast<std::uint8_t**>(record->ExceptionInformation[1]);
    if (thrown == nullptr) {
        return nullptr;
    }
    std::uint8_t* adjusted = thrown + type->mdisp;
    if (type->pdisp >= 0) {
        const auto* vbtable = *reinterpret_cast<std::uint8_t* const*>(thrown + type->pdisp);
        adjusted += *reinterpret_cast<const std::int32_t*>(vbtable + type->vdisp) + type->pdisp;
    }
    return adjusted;
}

}  // namespace

CatchMatch MatchPointerCatch(const EXCEPTION_RECORD* record, std::uintptr_t descriptor, const char* decoratedName) {
    if (record->ExceptionCode != kCxxExceptionCode || record->NumberParameters < 3 ||
        record->ExceptionInformation[0] != kCxxMagic || record->ExceptionInformation[2] == 0) {
        return {};
    }
    const auto* info = reinterpret_cast<const ThrowInfo*>(record->ExceptionInformation[2]);
    const CatchableTypeArray* types = info->catchable;
    for (std::int32_t i = 0; i < types->count; ++i) {
        if (IsType(types->types[i], descriptor, decoratedName)) {
            return {true, AdjustedPointer(record, types->types[i])};
        }
    }
    return {};
}

void DeleteCaughtException(void* object) {
    if (object == nullptr) {
        return;
    }
    using DeleteFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
    const auto* vtable = *static_cast<void* const* const*>(object);
    reinterpret_cast<DeleteFn>(vtable[0])(object, nullptr, 1);
}

}  // namespace s3re::msvc6
