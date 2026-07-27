#include "airfix/render/LegacyGameplayCameraPreset.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace airfix::render;

static_assert(
    static_cast<std::uint8_t>(
        LegacyGameplayCameraMode::camera0) == 0U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyGameplayCameraMode::camera1) == 1U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyGameplayCameraMode::camera2) == 2U);
static_assert(legacyGameplayCameraNearDistance == 0.25F);
static_assert(legacyGameplayCameraFarDistance == 200.0F);
static_assert(legacyGameplayCameraHorizontalFovDegrees == 90.0F);
static_assert(noexcept(legacyGameplayCameraPreset(0U, false)));
static_assert(noexcept(nextLegacyGameplayCameraMode(0U)));

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LegacyGameplayCameraPreset requirePreset(
    const std::uint32_t rawMode,
    const bool rearViewHeld,
    const char* const message) {
    const auto preset =
        legacyGameplayCameraPreset(rawMode, rearViewHeld);
    require(preset.has_value(), message);
    return *preset;
}

void testPersistentPresets() {
    require(
        requirePreset(0U, false, "camera0 was rejected") ==
            LegacyGameplayCameraPreset{
                0.7F, {0.0F, 0.1F, -0.75F}},
        "camera0 tuple changed");
    require(
        requirePreset(1U, false, "camera1 was rejected") ==
            LegacyGameplayCameraPreset{
                0.07F, {0.0F, 0.2F, -0.85F}},
        "camera1 tuple changed");
    require(
        requirePreset(2U, false, "camera2 was rejected") ==
            LegacyGameplayCameraPreset{
                0.7F, {0.0F, 1.8F, -0.75F}},
        "camera2 tuple changed");
}

void testHeldRearViewDoesNotChangePersistentMode() {
    const LegacyGameplayCameraPreset expectedRear{
        0.7F, {0.0F, 0.1F, 1.0F}};
    for (std::uint32_t mode = 0U; mode <= 2U; ++mode) {
        require(
            requirePreset(mode, true, "rear view was rejected") ==
                expectedRear,
            "rear-view tuple changed");
    }
    require(
        requirePreset(1U, false, "camera1 restore was rejected") ==
            LegacyGameplayCameraPreset{
                0.07F, {0.0F, 0.2F, -0.85F}},
        "rear release did not resolve the persistent tuple");
}

void testToggleCycle() {
    require(
        nextLegacyGameplayCameraMode(0U) ==
                LegacyGameplayCameraMode::camera1 &&
            nextLegacyGameplayCameraMode(1U) ==
                LegacyGameplayCameraMode::camera2 &&
            nextLegacyGameplayCameraMode(2U) ==
                LegacyGameplayCameraMode::camera0,
        "persistent camera cycle changed");
}

void testInvalidStateFailsClosed() {
    const std::uint32_t maximum =
        std::numeric_limits<std::uint32_t>::max();
    require(
        !legacyGameplayCameraPreset(3U, false).has_value() &&
            !legacyGameplayCameraPreset(3U, true).has_value() &&
            !legacyGameplayCameraPreset(maximum, false).has_value() &&
            !nextLegacyGameplayCameraMode(3U).has_value() &&
            !nextLegacyGameplayCameraMode(maximum).has_value(),
        "invalid persistent camera mode was accepted");
}

void testSelectionDoesNotAllocate() {
    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    float checksum = 0.0F;
    bool allMapped = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const std::uint32_t mode =
            static_cast<std::uint32_t>(index % 3U);
        const auto preset =
            legacyGameplayCameraPreset(mode, (index % 2U) != 0U);
        const auto next = nextLegacyGameplayCameraMode(mode);
        if (!preset.has_value() || !next.has_value()) {
            allMapped = false;
            break;
        }
        checksum += preset->speed + preset->offset.z +
            static_cast<float>(
                static_cast<std::uint8_t>(*next));
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        allMapped && checksum != 0.0F,
        "repeated camera preset selection failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "camera preset selection allocated");
}

} // namespace

int main() {
    try {
        testPersistentPresets();
        testHeldRearViewDoesNotChangePersistentMode();
        testToggleCycle();
        testInvalidStateFailsClosed();
        testSelectionDoesNotAllocate();
        std::cout << "LegacyGameplayCameraPreset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyGameplayCameraPreset tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
