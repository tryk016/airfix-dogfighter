#include "airfix/simulation/LegacyAircraftAiControls.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using airfix::simulation::LegacyAircraftAiControlsState;
using airfix::simulation::LegacyAircraftAiControlStatus;
using airfix::simulation::LegacyAircraftAiNumericPolicy;

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct RangeVector final {
  float minimum;
  float maximum;
  std::array<float, 4U> raw;
  std::array<std::uint32_t, 4U> cacheBits;
  std::array<double, 4U> retained;
  std::array<bool, 4U> changedAfterCache;
};

void testLayoutAndConstants() {
  require(sizeof(LegacyAircraftAiControlsState) == 0x80U,
          "AIControls layout size changed");
  require(std::bit_cast<std::uint32_t>(std::bit_cast<float>(
              airfix::simulation::legacyAircraftAiHasChangedScaleBits)) ==
              0x3C23D70AU,
          "HasChanged scale bits changed");
  require(std::bit_cast<std::uint64_t>(std::bit_cast<double>(
              airfix::simulation::legacyAircraftAiGetRelativeScaleBits)) ==
              0x3F847AE147AE147BULL,
          "GetRelative scale bits changed");
  require(std::bit_cast<std::uint64_t>(std::bit_cast<double>(
              airfix::simulation::legacyAircraftAiHalfRangeBits)) ==
              0x3FE0000000000000ULL,
          "half-range bits changed");
}

void testRangeValueAndAbsoluteOperations() {
  LegacyAircraftAiControlsState state;
  const auto original = state;
  require(
      !airfix::simulation::legacyAircraftAiSetRange(state, 8U, -1.0F, 1.0F) &&
          state == original,
      "invalid SetRange mutated state");
  require(
      airfix::simulation::legacyAircraftAiSetRange(state, 2U, 3.0F, -7.0F) &&
          state.minimum[2U] == 3.0F && state.maximum[2U] == -7.0F,
      "SetRange reordered or changed exact inputs");

  require(airfix::simulation::legacyAircraftAiSetValue(state, 2U, 12.5F) &&
              state.raw[2U] == 12.5F,
          "finite SetValue changed an in-range value");
  require(airfix::simulation::legacyAircraftAiSetValue(
              state, 2U, std::numeric_limits<float>::infinity()) &&
              state.raw[2U] == 100.0F,
          "positive infinity did not select the upper endpoint");
  require(airfix::simulation::legacyAircraftAiSetValue(
              state, 2U, -std::numeric_limits<float>::infinity()) &&
              state.raw[2U] == -100.0F,
          "negative infinity did not select the lower endpoint");
  require(airfix::simulation::legacyAircraftAiSetValue(
              state, 2U, std::numeric_limits<float>::quiet_NaN()) &&
              state.raw[2U] == -100.0F,
          "unordered SetValue did not select the recovered lower endpoint");
  require(!airfix::simulation::legacyAircraftAiSetValue(state, 9U, 0.0F),
          "invalid SetValue succeeded");
  require(
      !airfix::simulation::legacyAircraftAiGetAbsolute(state, 9U).has_value(),
      "invalid GetAbsolute succeeded");
  require(*airfix::simulation::legacyAircraftAiGetAbsolute(state, 2U) ==
              -100.0F,
          "GetAbsolute did not expose the raw array entry");
}

void testAddValueSequence() {
  LegacyAircraftAiControlsState state;
  require(airfix::simulation::legacyAircraftAiSetValue(state, 4U, 90.0F),
          "AddValue setup failed");
  require(airfix::simulation::legacyAircraftAiAddValue(state, 4U, 20.0F) ==
                  LegacyAircraftAiControlStatus::complete &&
              state.raw[4U] == 100.0F,
          "AddValue did not spill then clamp at the upper endpoint");
  require(airfix::simulation::legacyAircraftAiAddValue(state, 4U, -250.0F) ==
                  LegacyAircraftAiControlStatus::complete &&
              state.raw[4U] == -100.0F,
          "AddValue did not clamp at the lower endpoint");
  require(airfix::simulation::legacyAircraftAiAddValue(state, 8U, 1.0F) ==
              LegacyAircraftAiControlStatus::invalidChannel,
          "invalid AddValue channel was accepted");
}

void testPc53NearestVectors() {
  constexpr std::array<RangeVector, 3U> vectors{{
      {
          -255.0F,
          255.0F,
          {-100.0F, 0.0F, 50.0F, 100.0F},
          {0xC37F0000U, 0x00000000U, 0x42FF0000U, 0x437F0000U},
          {-255.0, 0.0, 127.5, 255.0},
          {false, true, true, true},
      },
      {
          -32.0F,
          32.0F,
          {-100.0F, 0.0F, 50.0F, 100.0F},
          {0xC2000000U, 0x00000000U, 0x41800000U, 0x42000000U},
          {-32.0, 0.0, 16.0, 32.0},
          {false, true, true, true},
      },
      {
          0.0F,
          1.0F,
          {-100.0F, 0.0F, 50.0F, 100.0F},
          {0x00000000U, 0x3F000000U, 0x3F400000U, 0x3F800000U},
          {0.0, 0.5, 0.75, 1.0},
          {false, true, true, true},
      },
  }};

  for (const auto &vector : vectors) {
    for (std::size_t index = 0U; index < vector.raw.size(); ++index) {
      LegacyAircraftAiControlsState state;
      require(airfix::simulation::legacyAircraftAiSetRange(
                  state, 0U, vector.minimum, vector.maximum) &&
                  airfix::simulation::legacyAircraftAiSetValue(
                      state, 0U, vector.raw[index]),
              "numeric vector setup failed");
      const auto relative =
          airfix::simulation::legacyAircraftAiGetRelative(state, 0U);
      require(relative.complete(), "GetRelative vector failed");
      require(*relative.cachedBinary32Bits == vector.cacheBits[index],
              "GetRelative cache bits changed");
      require(*relative.retainedValue == vector.retained[index],
              "GetRelative retained value changed");
      const auto changed =
          airfix::simulation::legacyAircraftAiHasChanged(state, 0U);
      require(changed.complete() &&
                  changed.changed == vector.changedAfterCache[index],
              "HasChanged PC53/RNE discriminator changed");
    }
  }
}

void testUnsupportedAndUnavailablePoliciesFailClosed() {
  LegacyAircraftAiControlsState state;
  require(airfix::simulation::legacyAircraftAiSetRange(state, 0U, -255.0F,
                                                       255.0F) &&
              airfix::simulation::legacyAircraftAiSetValue(state, 0U, 50.0F),
          "policy setup failed");
  const auto before = state;
  const auto unsupported = airfix::simulation::legacyAircraftAiGetRelative(
      state, 0U, LegacyAircraftAiNumericPolicy::unsupported);
  require(unsupported.status ==
                  LegacyAircraftAiControlStatus::unsupportedNumericPolicy &&
              state == before,
          "unsupported policy mutated cache");

  const int originalRounding = std::fegetround();
  if (originalRounding != -1 && std::fesetround(FE_DOWNWARD) == 0) {
    const auto unavailable =
        airfix::simulation::legacyAircraftAiGetRelative(state, 0U);
    require(
        unavailable.status ==
                LegacyAircraftAiControlStatus::numericEnvironmentUnavailable &&
            state == before,
        "non-nearest environment did not fail before mutation");
    require(std::fesetround(originalRounding) == 0,
            "rounding environment could not be restored");
  }
}

void testUnorderedComparisonIsUnchanged() {
  LegacyAircraftAiControlsState state;
  require(airfix::simulation::legacyAircraftAiSetRange(state, 0U, -1.0F, 1.0F),
          "unordered setup failed");
  state.cachedRelative[0U] = std::numeric_limits<float>::quiet_NaN();
  const auto result = airfix::simulation::legacyAircraftAiHasChanged(state, 0U);
  require(result.complete() && !result.changed,
          "unordered comparison did not report unchanged");
}

void testDispatcherRangeConfiguration() {
  LegacyAircraftAiControlsState state;
  state.minimum[2U] = 12.0F;
  state.maximum[2U] = 24.0F;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  require(airfix::simulation::legacyAircraftHasExactAiDispatcherRanges(state),
          "dispatcher ranges were not configured exactly");
  require(state.minimum[2U] == 12.0F && state.maximum[2U] == 24.0F,
          "dispatcher range setup touched an unrelated channel");
}

} // namespace

int main() {
  try {
    testLayoutAndConstants();
    testRangeValueAndAbsoluteOperations();
    testAddValueSequence();
    testPc53NearestVectors();
    testUnsupportedAndUnavailablePoliciesFailClosed();
    testUnorderedComparisonIsUnchanged();
    testDispatcherRangeConfiguration();
    std::cout << "Legacy aircraft AI controls tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
