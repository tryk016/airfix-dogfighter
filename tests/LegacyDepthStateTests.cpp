#include "airfix/render/LegacyDepthState.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

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
        LegacyDepthMode::opaqueDepthTestWrite) == 1U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyDepthMode::unconditionalDepthWrite) == 2U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyDepthMode::layerDepthTestNoWrite) == 3U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyDepthMode::overlayNoDepthNoWrite) == 4U);
static_assert(legacyReverseDepthClearValue == 0.0F);
static_assert(noexcept(legacyDepthStateForMode(0U)));

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LegacyDepthState requireState(
    const std::uint32_t rawMode,
    const char* const message) {
    const auto state = legacyDepthStateForMode(rawMode);
    require(state.has_value(), message);
    return *state;
}

void testRecoveredModes() {
    require(
        requireState(1U, "mode one was rejected") ==
            LegacyDepthState{
                LegacyDepthMode::opaqueDepthTestWrite,
                LegacyDepthCompare::greaterEqual,
                true,
            },
        "mode one mapping changed");
    require(
        requireState(2U, "mode two was rejected") ==
            LegacyDepthState{
                LegacyDepthMode::unconditionalDepthWrite,
                LegacyDepthCompare::always,
                true,
            },
        "mode two mapping changed");
    require(
        requireState(3U, "mode three was rejected") ==
            LegacyDepthState{
                LegacyDepthMode::layerDepthTestNoWrite,
                LegacyDepthCompare::greaterEqual,
                false,
            },
        "mode three mapping changed");
    require(
        requireState(4U, "mode four was rejected") ==
            LegacyDepthState{
                LegacyDepthMode::overlayNoDepthNoWrite,
                LegacyDepthCompare::always,
                false,
            },
        "mode four mapping changed");
}

void testInvalidModesFailClosed() {
    require(
        !legacyDepthStateForMode(0U).has_value() &&
            !legacyDepthStateForMode(5U).has_value() &&
            !legacyDepthStateForMode(
                 std::numeric_limits<std::uint32_t>::max())
                 .has_value(),
        "invalid raw depth mode was accepted");
}

void testMappingDoesNotAllocate() {
    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    std::uint32_t checksum = 0U;
    bool allMapped = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const std::uint32_t rawMode =
            static_cast<std::uint32_t>((index % 4U) + 1U);
        const auto state = legacyDepthStateForMode(rawMode);
        if (!state.has_value()) {
            allMapped = false;
            break;
        }
        checksum += state->writeEnabled ? rawMode : rawMode * 2U;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(allMapped && checksum != 0U, "repeated mode mapping failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "depth-state mapping allocated");
}

} // namespace

int main() {
    try {
        testRecoveredModes();
        testInvalidModesFailClosed();
        testMappingDoesNotAllocate();
        std::cout << "LegacyDepthState tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyDepthState tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
