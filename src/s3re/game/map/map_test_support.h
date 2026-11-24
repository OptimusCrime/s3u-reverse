#pragma once

// Test support for the map-loading tests (player_towns_tests.cpp,
// load_map_tests.cpp): fresh worlds as a new game has them, random maps,
// and the S3.EXE globals a function writes.

#include "game/world.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game::map_test {

// A world as CLogicThread::OnStart has it right after `new CGameData`:
// zeroed memory, then the original constructor (CGameData::CGameData
// 0x4fc990: offset table, free records, effect table, statistics).
inline void ConstructWorld(CGameData* world) {
    std::memset(world, 0, sizeof(CGameData));  // raw-ok: the whole test buffer, as operator new + zeroing
    using ConstructFn = CGameData*(__fastcall*)(CGameData* self, void* edx);
    reinterpret_cast<ConstructFn>(0x004fc990)(world, nullptr);
}

// CGameData::GenerateRandomMap (0x5166d0, replaced and verified) on a world.
inline void GenerateRandomMap(CGameData* world, std::uint32_t seed, std::int32_t size, std::uint32_t symmetry) {
    using GenerateFn = std::uint32_t(__fastcall*)(CGameData* self, void* edx, std::uint32_t seed, std::int32_t size,
                                                  std::uint32_t symmetry);
    reinterpret_cast<GenerateFn>(0x005166d0)(world, nullptr, seed, size, symmetry);
}

// The writable sections of S3.EXE (its globals).
class Globals {
public:
    Globals() { Snapshot(bytes_); }

    // Compares the globals after the original's run with the state before
    // it, puts back every byte the original changed (so that our run starts
    // from the same globals), and returns the original's result.
    std::vector<std::uint8_t> TakeOriginal() {
        std::vector<std::uint8_t> after;
        Snapshot(after);
        Restore(bytes_, after);
        return after;
    }

    // The globals now (after our run).
    static std::vector<std::uint8_t> Now() {
        std::vector<std::uint8_t> bytes;
        Snapshot(bytes);
        return bytes;
    }

    // First differing offset into the snapshot, or -1.
    static std::ptrdiff_t FirstDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                                          std::ptrdiff_t skipFrom = -1, std::ptrdiff_t skipTo = -1) {
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const auto index = static_cast<std::ptrdiff_t>(i);
            if (a[i] != b[i] && !(index >= skipFrom && index < skipTo)) {
                return index;
            }
        }
        return a.size() == b.size() ? -1 : static_cast<std::ptrdiff_t>((std::min)(a.size(), b.size()));
    }

    // The S3.EXE address of a snapshot offset.
    static std::uintptr_t AddressOf(std::ptrdiff_t offset) {
        std::uintptr_t result = 0;
        ForEachSection([&](std::uint8_t* start, std::size_t size) {
            if (result == 0 && offset >= 0 && static_cast<std::size_t>(offset) < size) {
                result = reinterpret_cast<std::uintptr_t>(start) + static_cast<std::size_t>(offset);
            }
            offset -= static_cast<std::ptrdiff_t>(size);
        });
        return result;
    }

private:
    template <typename F>
    static void ForEachSection(F f) {
        auto* base = reinterpret_cast<std::uint8_t*>(0x400000);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
                f(base + section->VirtualAddress, static_cast<std::size_t>(section->Misc.VirtualSize));
            }
        }
    }

    static void Snapshot(std::vector<std::uint8_t>& bytes) {
        bytes.clear();
        ForEachSection([&](std::uint8_t* start, std::size_t size) { bytes.insert(bytes.end(), start, start + size); });
    }

    // Writes `before` back where it differs from `after`.
    static void Restore(const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& after) {
        std::size_t offset = 0;
        ForEachSection([&](std::uint8_t* start, std::size_t size) {
            for (std::size_t i = 0; i < size; ++i) {
                if (before[offset + i] != after[offset + i]) {
                    start[i] = before[offset + i];
                }
            }
            offset += size;
        });
    }

    std::vector<std::uint8_t> bytes_;
};

}  // namespace s3re::game::map_test
