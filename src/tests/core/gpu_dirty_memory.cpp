// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/gpu_dirty_memory_manager.h"

TEST_CASE("GPUDirtyMemoryManager records writes that cross a page boundary",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    // A transform covers one page; this write spans two of them.
    manager.Collect(0x7F8, 0x400);

    REQUIRE(manager.Overlaps(0x7F8, 8));
    REQUIRE(manager.Overlaps(0x800, 0x100));
    REQUIRE(manager.Overlaps(0xBF0, 8));
    REQUIRE_FALSE(manager.Overlaps(0xC00, 1));

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        gathered.emplace_back(address, size);
    };
    manager.Gather(callback);

    // Both pages' parts must be reported; the part on the second page must not be lost.
    bool saw_first_page = false;
    bool saw_second_page = false;
    for (const auto& [address, size] : gathered) {
        if (address < 0x800) {
            REQUIRE(address + size <= 0x800);
            saw_first_page = true;
        } else {
            REQUIRE(address >= 0x800);
            saw_second_page = true;
        }
    }
    REQUIRE(saw_first_page);
    REQUIRE(saw_second_page);
}

TEST_CASE("GPUDirtyMemoryManager records a write larger than one page",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    // 0x1000 through 0x5FFF: the transform page is 2 KiB, so this write touches ten of them.
    manager.Collect(0x1000, 0x5000);

    REQUIRE(manager.Overlaps(0x1000, 8));
    REQUIRE(manager.Overlaps(0x1000 + 0x2000, 8));
    REQUIRE(manager.Overlaps(0x1000 + 0x4000, 8));
    REQUIRE_FALSE(manager.Overlaps(0x1000 + 0x5000, 8));

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        gathered.emplace_back(address, size);
    };
    manager.Gather(callback);

    bool saw_third_page = false;
    for (const auto& [address, size] : gathered) {
        if (address >= 0x1000 + 0x4000) {
            saw_third_page = true;
        }
    }
    REQUIRE(saw_third_page);
}

TEST_CASE("GPUDirtyMemoryManager reports collected writes until they are gathered",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    manager.Collect(0x10040, 0x10);
    manager.Collect(0x20000, 0x40);

    REQUIRE(manager.Overlaps(0x10040, 1));
    REQUIRE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE(manager.Overlaps(0x20000, 4));
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x10080, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x30000, 0x1000));

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        gathered.emplace_back(address, size);
    };
    manager.Gather(callback);

    REQUIRE(gathered.size() == 2);
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE_FALSE(manager.Overlaps(0x20000, 4));
}
