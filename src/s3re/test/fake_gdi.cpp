#include "test/fake_gdi.h"

#include "game/globals.h"
#include "test/callee_stub.h"

#include <cstring>
#include <initializer_list>

namespace s3re::test {

namespace {

using namespace s3re::game;

FakeGdi* g_active = nullptr;

enum CallId : std::uint32_t {
    kCallSelectObject = 0x6d10001,
    kCallGetTextExtentPoint32,
    kCallSetBkMode,
    kCallSetTextColor,
    kCallSetBkColor,
    kCallTextOut,
    kCallCreateCompatibleDC,
    kCallDeleteDC,
    kCallGdiFlush,
    kCallDrawText,
};

// With failures on: true for about one call in eight.
bool Fails() {
    return g_active->failures_ && g_active->rng_.Chance(1, 8);
}

constexpr std::uint32_t kClrInvalid = 0xffffffff;

void Log(std::initializer_list<std::uint32_t> values) {
    g_active->log_.insert(g_active->log_.end(), values);
}

std::uint32_t U(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

// The first `length` characters (at most 256), by contents.
void LogText(const char* text, std::int32_t length) {
    if (text == nullptr) {
        Log({0xffffffff});
        return;
    }
    const std::int32_t n = length < 0 ? 0 : (length > 256 ? 256 : length);
    std::uint32_t hash = 2166136261u;
    for (std::int32_t i = 0; i < n; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(text[i])) * 16777619u;
    }
    Log({static_cast<std::uint32_t>(n), hash});
}

void* __stdcall FakeSelectObject(void* dc, void* object) {
    Log({kCallSelectObject, U(dc), U(object)});
    if (Fails()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(g_active->rng_.Next32());
}

int __stdcall FakeGetTextExtentPoint32(void* dc, const char* text, std::int32_t length, void* size) {
    Log({kCallGetTextExtentPoint32, U(dc), static_cast<std::uint32_t>(length), size != nullptr ? 1u : 0u});
    LogText(text, length);
    Rng& rng = g_active->rng_;
    // Mostly text-like sizes; sometimes anything (the callers clamp).
    std::int32_t extent[2];
    if (rng.Chance(1, 8)) {
        extent[0] = static_cast<std::int32_t>(rng.Next32());
        extent[1] = static_cast<std::int32_t>(rng.Next32());
    } else {
        extent[0] = rng.Range(0, 700);
        extent[1] = rng.Range(0, 40);
    }
    if (size != nullptr) {
        std::memcpy(size, extent, sizeof(extent));
    }
    return rng.Chance(1, 8) ? 0 : 1;
}

std::int32_t __stdcall FakeSetBkMode(void* dc, std::int32_t mode) {
    Log({kCallSetBkMode, U(dc), static_cast<std::uint32_t>(mode)});
    if (Fails()) {
        return 0;
    }
    return g_active->rng_.Range(0, 2);
}

std::uint32_t __stdcall FakeSetTextColor(void* dc, std::uint32_t colour) {
    Log({kCallSetTextColor, U(dc), colour});
    if (Fails()) {
        return kClrInvalid;
    }
    return g_active->rng_.Next32() & 0xffffff;
}

std::uint32_t __stdcall FakeSetBkColor(void* dc, std::uint32_t colour) {
    Log({kCallSetBkColor, U(dc), colour});
    if (Fails()) {
        return kClrInvalid;
    }
    return g_active->rng_.Next32() & 0xffffff;
}

int __stdcall FakeTextOut(void* dc, std::int32_t x, std::int32_t y, const char* text, std::int32_t length) {
    Log({kCallTextOut, U(dc), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
         static_cast<std::uint32_t>(length)});
    LogText(text, length);
    return g_active->rng_.Chance(1, 8) ? 0 : 1;
}

void* __stdcall FakeCreateCompatibleDC(void* dc) {
    Log({kCallCreateCompatibleDC, U(dc)});
    if (Fails()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(g_active->rng_.Next32() | 1);
}

int __stdcall FakeDeleteDC(void* dc) {
    Log({kCallDeleteDC, U(dc)});
    return Fails() ? 0 : 1;
}

int __stdcall FakeGdiFlush() {
    Log({kCallGdiFlush});
    return Fails() ? 0 : 1;
}

// DrawTextA(dc, text, length, rect, format): logs the rect and returns a
// text-like height (sometimes anything).
std::int32_t __stdcall FakeDrawText(void* dc, const char* text, std::int32_t length, void* rect,
                                    std::uint32_t format) {
    Log({kCallDrawText, U(dc), static_cast<std::uint32_t>(length), format});
    LogText(text, length);
    if (rect != nullptr) {
        std::uint32_t r[4];
        std::memcpy(r, rect, sizeof(r));
        Log({r[0], r[1], r[2], r[3]});
    }
    Rng& rng = g_active->rng_;
    return rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(0, 60);
}

struct Slot {
    std::uintptr_t address;
    const void* fake;
};

const Slot kSlots[] = {
    {g_importSelectObject.address(), reinterpret_cast<const void*>(&FakeSelectObject)},
    {g_importGetTextExtentPoint32A.address(), reinterpret_cast<const void*>(&FakeGetTextExtentPoint32)},
    {g_importSetBkMode.address(), reinterpret_cast<const void*>(&FakeSetBkMode)},
    {g_importSetTextColor.address(), reinterpret_cast<const void*>(&FakeSetTextColor)},
    {g_importSetBkColor.address(), reinterpret_cast<const void*>(&FakeSetBkColor)},
    {g_importTextOutA.address(), reinterpret_cast<const void*>(&FakeTextOut)},
    {g_importCreateCompatibleDC.address(), reinterpret_cast<const void*>(&FakeCreateCompatibleDC)},
    {g_importDeleteDC.address(), reinterpret_cast<const void*>(&FakeDeleteDC)},
    {g_importGdiFlush.address(), reinterpret_cast<const void*>(&FakeGdiFlush)},
    {g_importDrawTextA.address(), reinterpret_cast<const void*>(&FakeDrawText)},
};

}  // namespace

void FakeGdi::Install() {
    g_active = this;
    for (const Slot& slot : kSlots) {
        PatchBytes(slot.address, &slot.fake, sizeof(slot.fake));
    }
    installed_ = true;
}

void FakeGdi::Uninstall() {
    if (!installed_) {
        return;
    }
    for (const Slot& slot : kSlots) {
        UnpatchBytes(slot.address);
    }
    installed_ = false;
    g_active = nullptr;
}

}  // namespace s3re::test
