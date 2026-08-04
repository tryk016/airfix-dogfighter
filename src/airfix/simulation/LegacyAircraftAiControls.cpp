#include "airfix/simulation/LegacyAircraftAiControls.hpp"

#include <cfenv>
#include <cfloat>
#include <cmath>
#include <limits>

namespace airfix::simulation {
namespace {

[[nodiscard]] bool validChannel(const std::size_t channel) noexcept {
  return channel < legacyAircraftAiControlChannelCount;
}

[[nodiscard]] double roundedAdd(const double left,
                                const double right) noexcept {
  volatile double result = left + right;
  return result;
}

[[nodiscard]] double roundedSubtract(const double left,
                                     const double right) noexcept {
  volatile double result = left - right;
  return result;
}

[[nodiscard]] double roundedMultiply(const double left,
                                     const double right) noexcept {
  volatile double result = left * right;
  return result;
}

[[nodiscard]] float roundedBinary32(const double value) noexcept {
  volatile float result = static_cast<float>(value);
  return result;
}

[[nodiscard]] double mappedValue(const LegacyAircraftAiControlsState &state,
                                 const std::size_t channel,
                                 const double scale) noexcept {
  const double range =
      std::fabs(roundedSubtract(static_cast<double>(state.maximum[channel]),
                                static_cast<double>(state.minimum[channel])));
  const double halfRange = roundedMultiply(
      range, std::bit_cast<double>(legacyAircraftAiHalfRangeBits));
  const double offset =
      roundedAdd(static_cast<double>(state.raw[channel]), 100.0);
  const double normalized = roundedMultiply(offset, scale);
  const double scaled = roundedMultiply(halfRange, normalized);
  return roundedAdd(scaled, static_cast<double>(state.minimum[channel]));
}

[[nodiscard]] LegacyAircraftAiControlStatus
numericStatus(const LegacyAircraftAiNumericPolicy policy) noexcept {
  if (policy != LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven) {
    return LegacyAircraftAiControlStatus::unsupportedNumericPolicy;
  }
  if (!legacyAircraftAiNumericPolicyAvailable(policy)) {
    return LegacyAircraftAiControlStatus::numericEnvironmentUnavailable;
  }
  return LegacyAircraftAiControlStatus::complete;
}

} // namespace

bool legacyAircraftAiSetRange(LegacyAircraftAiControlsState &state,
                              const std::size_t channel, const float minimum,
                              const float maximum) noexcept {
  if (!validChannel(channel)) {
    return false;
  }
  state.minimum[channel] = minimum;
  state.maximum[channel] = maximum;
  return true;
}

bool legacyAircraftAiSetValue(LegacyAircraftAiControlsState &state,
                              const std::size_t channel,
                              const float value) noexcept {
  if (!validChannel(channel)) {
    return false;
  }
  if (std::isnan(value) || value < legacyAircraftAiRawMinimum) {
    state.raw[channel] = legacyAircraftAiRawMinimum;
  } else if (value > legacyAircraftAiRawMaximum) {
    state.raw[channel] = legacyAircraftAiRawMaximum;
  } else {
    state.raw[channel] = value;
  }
  return true;
}

std::optional<float>
legacyAircraftAiGetAbsolute(const LegacyAircraftAiControlsState &state,
                            const std::size_t channel) noexcept {
  if (!validChannel(channel)) {
    return std::nullopt;
  }
  return state.raw[channel];
}

LegacyAircraftAiControlStatus
legacyAircraftAiAddValue(LegacyAircraftAiControlsState &state,
                         const std::size_t channel, const float delta,
                         const LegacyAircraftAiNumericPolicy policy) noexcept {
  if (!validChannel(channel)) {
    return LegacyAircraftAiControlStatus::invalidChannel;
  }
  const auto status = numericStatus(policy);
  if (status != LegacyAircraftAiControlStatus::complete) {
    return status;
  }
  const double sum = roundedAdd(static_cast<double>(state.raw[channel]),
                                static_cast<double>(delta));
  const float spilled = roundedBinary32(sum);
  static_cast<void>(legacyAircraftAiSetValue(state, channel, spilled));
  return LegacyAircraftAiControlStatus::complete;
}

LegacyAircraftAiChangedResult legacyAircraftAiHasChanged(
    const LegacyAircraftAiControlsState &state, const std::size_t channel,
    const LegacyAircraftAiNumericPolicy policy) noexcept {
  if (!validChannel(channel)) {
    return {.status = LegacyAircraftAiControlStatus::invalidChannel,
            .changed = false};
  }
  const auto status = numericStatus(policy);
  if (status != LegacyAircraftAiControlStatus::complete) {
    return {.status = status, .changed = false};
  }
  const double candidate =
      mappedValue(state, channel,
                  static_cast<double>(std::bit_cast<float>(
                      legacyAircraftAiHasChangedScaleBits)));
  const double cached = static_cast<double>(state.cachedRelative[channel]);
  return {
      .status = LegacyAircraftAiControlStatus::complete,
      .changed =
          !std::isnan(candidate) && !std::isnan(cached) && candidate != cached,
  };
}

LegacyAircraftAiRelativeResult legacyAircraftAiGetRelative(
    LegacyAircraftAiControlsState &state, const std::size_t channel,
    const LegacyAircraftAiNumericPolicy policy) noexcept {
  if (!validChannel(channel)) {
    return {
        .status = LegacyAircraftAiControlStatus::invalidChannel,
        .retainedValue = std::nullopt,
        .cachedBinary32Bits = std::nullopt,
    };
  }
  const auto status = numericStatus(policy);
  if (status != LegacyAircraftAiControlStatus::complete) {
    return {
        .status = status,
        .retainedValue = std::nullopt,
        .cachedBinary32Bits = std::nullopt,
    };
  }
  const double scale =
      std::bit_cast<double>(legacyAircraftAiGetRelativeScaleBits);
  const float cached = roundedBinary32(mappedValue(state, channel, scale));
  state.cachedRelative[channel] = cached;
  const double retained = mappedValue(state, channel, scale);
  return {
      .status = LegacyAircraftAiControlStatus::complete,
      .retainedValue = retained,
      .cachedBinary32Bits = std::bit_cast<std::uint32_t>(cached),
  };
}

bool legacyAircraftAiNumericPolicyAvailable(
    const LegacyAircraftAiNumericPolicy policy) noexcept {
  return policy ==
             LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven &&
         std::numeric_limits<float>::is_iec559 &&
         std::numeric_limits<float>::digits == 24 &&
         std::numeric_limits<double>::is_iec559 &&
         std::numeric_limits<double>::digits == 53 && FLT_EVAL_METHOD == 0 &&
         std::fegetround() == FE_TONEAREST;
}

void legacyAircraftConfigureAiDispatcherRanges(
    LegacyAircraftAiControlsState &state) noexcept {
  static_cast<void>(legacyAircraftAiSetRange(state, 0U, -255.0F, 255.0F));
  static_cast<void>(legacyAircraftAiSetRange(state, 1U, -32.0F, 32.0F));
  static_cast<void>(legacyAircraftAiSetRange(state, 3U, -32.0F, 32.0F));
  static_cast<void>(legacyAircraftAiSetRange(state, 5U, 0.0F, 1.0F));
  static_cast<void>(legacyAircraftAiSetRange(state, 6U, 0.0F, 1.0F));
}

bool legacyAircraftHasExactAiDispatcherRanges(
    const LegacyAircraftAiControlsState &state) noexcept {
  return state.minimum[0U] == -255.0F && state.maximum[0U] == 255.0F &&
         state.minimum[1U] == -32.0F && state.maximum[1U] == 32.0F &&
         state.minimum[3U] == -32.0F && state.maximum[3U] == 32.0F &&
         state.minimum[5U] == 0.0F && state.maximum[5U] == 1.0F &&
         state.minimum[6U] == 0.0F && state.maximum[6U] == 1.0F;
}

} // namespace airfix::simulation
