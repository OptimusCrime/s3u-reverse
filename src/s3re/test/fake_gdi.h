#pragma once

// Scripted GDI functions for differential tests of the UI text code
// (CControl::ShowTooltip, later CStaticText and friends). FakeGdi points
// S3.EXE's import address table slots of SelectObject,
// GetTextExtentPoint32A, SetBkMode, SetTextColor, SetBkColor and TextOutA
// at fakes that log every call (an id, the device context, the values;
// strings by contents) and return values drawn from their own Rng. A test
// runs the original with one FakeGdi and ours with another made from the
// same seed: as long as both make the same calls they see the same
// results, and the logs are compared. The patches go through PatchBytes,
// so a crashing test can't leave them behind. Our replacements call these
// functions through the same slots (game/globals.h, g_importSelectObject
// ...), as the original does.
//
// wip/text-r6: CreateCompatibleDC, DeleteDC, GdiFlush and DrawTextA (the
// text surface, Dirk\Text.cpp) are faked as well. With `failures` set,
// every fake fails now and then the way its VERIFY check tests (null,
// 0 or CLR_INVALID); without it the results are drawn as before.

#include "test/difftest.h"

#include <cstdint>
#include <vector>

namespace s3re::test {

class FakeGdi {
public:
    explicit FakeGdi(std::uint64_t seed, bool failures = false) : rng_(seed), failures_(failures) {}
    ~FakeGdi() { Uninstall(); }
    FakeGdi(const FakeGdi&) = delete;
    FakeGdi& operator=(const FakeGdi&) = delete;

    // Patches the slots and makes this the active fake.
    void Install();
    void Uninstall();

    std::vector<std::uint32_t>& log() { return log_; }

    // State of the fakes (read by fake_gdi.cpp).
    Rng rng_;
    std::vector<std::uint32_t> log_;
    bool installed_ = false;
    bool failures_ = false;
};

}  // namespace s3re::test
