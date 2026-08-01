#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"

#include <atomic>
#include <cmath>
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

void *operator new(const std::size_t size) {
  recordAllocation();
  if (void *memory = std::malloc(size == 0U ? 1U : size); memory != nullptr) {
    return memory;
  }
  throw std::bad_alloc();
}

void *operator new[](const std::size_t size) { return ::operator new(size); }

void operator delete(void *memory) noexcept { std::free(memory); }

void operator delete[](void *memory) noexcept { ::operator delete(memory); }

void operator delete(void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

namespace {

using namespace airfix::render;
using namespace recovered_legacy_aircraft_hud_rolling_digits;

static_assert(digitCount == 4U);
static_assert(atlasWidth == 16U && atlasHeight == 128U);
static_assert(glyphWidth == 7.0F && glyphHeight == 9.0F &&
              glyphPitchY == 11.0F && destinationPitchX == 8.0F);
static_assert(noexcept(makeLegacyAircraftHudRollingDigitsState(0)));
static_assert(noexcept(advanceLegacyAircraftHudRollingDigits({}, 0, 0.0F)));
static_assert(noexcept(buildLegacyAircraftHudRollingDigitsPlan({}, 0.0F, 0.0F,
                                                               0U, false)));

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-6F) noexcept {
  return std::fabs(actual - expected) <= tolerance;
}

void testNativeFourCharacterInitialization() {
  const auto zero = makeLegacyAircraftHudRollingDigitsState(0);
  require(zero.digitPositions ==
                  std::array<float, digitCount>{0.0F, 0.0F, 0.0F, 0.0F} &&
              !zero.retentionFactorValid && zero.cachedElapsedSeconds == 0.0F,
          "zero did not use native %04d initialization");

  const auto hundred = makeLegacyAircraftHudRollingDigitsState(100);
  require(hundred.digitPositions ==
              std::array<float, digitCount>{0.0F, 1.0F, 0.0F, 0.0F},
          "100 did not become 0100");

  const auto overflow = makeLegacyAircraftHudRollingDigitsState(12345);
  require(overflow.digitPositions ==
              std::array<float, digitCount>{1.0F, 2.0F, 3.0F, 4.0F},
          "native first-four-character overflow behavior changed");

  const auto negative = makeLegacyAircraftHudRollingDigitsState(-1);
  require(negative.digitPositions ==
              std::array<float, digitCount>{-3.0F, 0.0F, 0.0F, 1.0F},
          "native leading-minus conversion changed");
}

void testCachedRetentionAndShortestCyclicRoute() {
  auto state = makeLegacyAircraftHudRollingDigitsState(9999);
  const auto first = advanceLegacyAircraftHudRollingDigits(state, 0, 0.1F);
  require(first.ready() && first.state.retentionFactorValid &&
              first.state.cachedElapsedSeconds == 0.1F &&
              close(first.state.retentionFactor, 0.46762423F) &&
              first.state.digitPositions[0] > 9.0F &&
              first.state.digitPositions[0] < 10.0F,
          "9-to-0 did not take the recovered forward cyclic route");

  const auto second =
      advanceLegacyAircraftHudRollingDigits(first.state, 0, 0.1F);
  require(second.ready() &&
              second.state.retentionFactor == first.state.retentionFactor &&
              second.state.cachedElapsedSeconds ==
                  first.state.cachedElapsedSeconds,
          "equal elapsed value did not retain the cached pow factor");

  state = makeLegacyAircraftHudRollingDigitsState(0);
  const auto tie = advanceLegacyAircraftHudRollingDigits(state, 5000, 0.1F);
  require(tie.ready() && tie.state.digitPositions[0] > 0.0F &&
              tie.state.digitPositions[0] < 5.0F,
          "exact distance five incorrectly used the alternate cyclic target");
}

void testWrapAndInvalidRetainedPositionRecovery() {
  auto state = makeLegacyAircraftHudRollingDigitsState(9999);
  auto step = advanceLegacyAircraftHudRollingDigits(state, 0, 0.5F);
  require(step.ready(), "valid wrap step was rejected");
  for (std::size_t index = 0U; index < digitCount; ++index) {
    require(step.state.digitPositions[index] >= 0.0F &&
                step.state.digitPositions[index] < 10.0F,
            "wrapped position escaped [0,10)");
  }

  state = step.state;
  state.digitPositions[2U] = std::numeric_limits<float>::quiet_NaN();
  step = advanceLegacyAircraftHudRollingDigits(state, 0, 0.5F);
  require(step.ready() && step.state.digitPositions[2U] == 0.0F,
          "native invalid-position reset to +0 was not retained");
}

void testInvalidElapsedFailsWithoutMutation() {
  const auto state = makeLegacyAircraftHudRollingDigitsState(100);
  auto result = advanceLegacyAircraftHudRollingDigits(
      state, 200, std::numeric_limits<float>::quiet_NaN());
  require(!result.ready() &&
              result.status == LegacyAircraftHudRollingDigitsAdvanceStatus::
                                   elapsedSecondsNotFinite &&
              result.state == state,
          "non-finite elapsed value mutated state");

  result = advanceLegacyAircraftHudRollingDigits(state, 200, -0.1F);
  require(!result.ready() &&
              result.status == LegacyAircraftHudRollingDigitsAdvanceStatus::
                                   elapsedSecondsNegative &&
              result.state == state,
          "negative elapsed value mutated state");
}

void testExactVerticalAtlasPlan() {
  LegacyAircraftHudRollingDigitsState state{
      .digitPositions = {0.0F, 1.0F, 2.5F, 9.0F}};
  const auto plan = buildLegacyAircraftHudRollingDigitsPlan(
      state, 100.0F, 200.0F, 0x7FFFFFFFU, true);
  require(plan.ready() && plan.commandCount == digitCount &&
              plan.command(digitCount) == nullptr,
          "valid rolling-digit plan was incomplete");

  const auto *zero = plan.command(0U);
  const auto *one = plan.command(1U);
  const auto *fractional = plan.command(2U);
  const auto *nine = plan.command(3U);
  require(zero != nullptr && zero->slotIndex == 0U &&
              zero->destinationRect ==
                  LegacyCanvasRect{100.0F, 201.0F, 7.0F, 9.0F} &&
              zero->sourceRect ==
                  LegacyAircraftHudRollingDigitSourceRect{0.0F, 1.0F, 7.0F,
                                                          10.0F} &&
              zero->colourArgb == 0x7FFFFFFFU,
          "zero glyph rectangle changed");
  require(
      one != nullptr &&
          one->destinationRect ==
              LegacyCanvasRect{108.0F, 201.0F, 7.0F, 9.0F} &&
          one->sourceRect ==
              LegacyAircraftHudRollingDigitSourceRect{0.0F, 12.0F, 7.0F, 21.0F},
      "one glyph pitch changed");
  require(fractional != nullptr && fractional->sourceRect ==
                                       LegacyAircraftHudRollingDigitSourceRect{
                                           0.0F, 28.5F, 7.0F, 37.5F},
          "fractional source window no longer rolls between glyphs");
  require(nine != nullptr && nine->sourceRect.bottom == 109.0F &&
              nine->sourceRect.bottom <= static_cast<float>(atlasHeight),
          "ninth glyph escaped the verified 16x128 atlas contract");
}

void testNativePerSlotSuppressionAndAtomicFailures() {
  LegacyAircraftHudRollingDigitsState state{
      .digitPositions = {-1.0F, 1.0F, std::numeric_limits<float>::quiet_NaN(),
                         10.0F}};
  const auto sparse = buildLegacyAircraftHudRollingDigitsPlan(
      state, 10.0F, 20.0F, 0xFFFFFFFFU, true);
  require(sparse.ready() && sparse.commandCount == 1U &&
              sparse.command(0U)->slotIndex == 1U &&
              sparse.command(0U)->destinationRect.x == 18.0F,
          "invalid native digit slots were not skipped independently");

  const auto missing = buildLegacyAircraftHudRollingDigitsPlan(
      state, 10.0F, 20.0F, 0xFFFFFFFFU, false);
  require(!missing.ready() &&
              missing.status ==
                  LegacyAircraftHudRollingDigitsPlanStatus::atlasUnavailable &&
              missing.commandCount == 0U,
          "missing atlas published partial commands");

  const auto invalidOrigin = buildLegacyAircraftHudRollingDigitsPlan(
      state, std::numeric_limits<float>::infinity(), 20.0F, 0xFFFFFFFFU, true);
  require(!invalidOrigin.ready() &&
              invalidOrigin.status ==
                  LegacyAircraftHudRollingDigitsPlanStatus::originNotFinite &&
              invalidOrigin.commandCount == 0U,
          "non-finite origin published partial commands");
}

void testSteadyStateDoesNotAllocate() {
  auto state = makeLegacyAircraftHudRollingDigitsState(0);
  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_release);
  bool ready = true;
  std::size_t checksum = 0U;
  for (std::size_t index = 0U; index < 1024U; ++index) {
    const auto step = advanceLegacyAircraftHudRollingDigits(
        state, static_cast<std::int32_t>(index), 1.0F / 60.0F);
    ready = ready && step.ready();
    state = step.state;
    const auto plan = buildLegacyAircraftHudRollingDigitsPlan(
        state, 100.0F, 200.0F, 0xFFFFFFFFU, true);
    ready = ready && plan.ready();
    checksum += plan.commandCount;
  }
  trackAllocations.store(false, std::memory_order_release);
  require(ready && checksum != 0U &&
              allocationCount.load(std::memory_order_relaxed) == 0U,
          "rolling-digit advance or plan allocated in steady state");
}

} // namespace

int main() {
  try {
    testNativeFourCharacterInitialization();
    testCachedRetentionAndShortestCyclicRoute();
    testWrapAndInvalidRetainedPositionRecovery();
    testInvalidElapsedFailsWithoutMutation();
    testExactVerticalAtlasPlan();
    testNativePerSlotSuppressionAndAtomicFailures();
    testSteadyStateDoesNotAllocate();
    std::cout << "LegacyAircraftHudRollingDigits tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    trackAllocations.store(false, std::memory_order_release);
    std::cerr << "LegacyAircraftHudRollingDigits tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
