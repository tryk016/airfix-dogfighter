#include "airfix/simulation/LegacyAircraftAiControlDispatcher.hpp"

#include <array>
#include <cfenv>
#include <cmath>
#include <limits>

namespace airfix::simulation {
namespace {

struct Route final {
  std::uint8_t channel;
  LegacyAircraftAiNativeEvent event;
};

constexpr std::array<Route, 5U> routes{{
    {0U, LegacyAircraftAiNativeEvent::thrustSet},
    {1U, LegacyAircraftAiNativeEvent::pitchSet},
    {3U, LegacyAircraftAiNativeEvent::bankSet},
    {5U, LegacyAircraftAiNativeEvent::primaryAttack},
    {6U, LegacyAircraftAiNativeEvent::secondaryAttack},
}};

[[nodiscard]] bool
validChannelState(const LegacyAircraftAiControlsState &controls,
                  const std::size_t channel) noexcept {
  const float raw = controls.raw[channel];
  return std::isfinite(controls.minimum[channel]) &&
         std::isfinite(controls.maximum[channel]) && std::isfinite(raw) &&
         raw >= legacyAircraftAiRawMinimum &&
         raw <= legacyAircraftAiRawMaximum &&
         std::isfinite(controls.cachedRelative[channel]);
}

[[nodiscard]] LegacyAircraftAiDispatchStatus
mapStatus(const LegacyAircraftAiControlStatus status) noexcept {
  switch (status) {
  case LegacyAircraftAiControlStatus::complete:
    return LegacyAircraftAiDispatchStatus::completed;
  case LegacyAircraftAiControlStatus::invalidChannel:
    return LegacyAircraftAiDispatchStatus::invalidConfiguration;
  case LegacyAircraftAiControlStatus::unsupportedNumericPolicy:
    return LegacyAircraftAiDispatchStatus::unsupportedNumericPolicy;
  case LegacyAircraftAiControlStatus::numericEnvironmentUnavailable:
    return LegacyAircraftAiDispatchStatus::numericEnvironmentUnavailable;
  }
  return LegacyAircraftAiDispatchStatus::invalidConfiguration;
}

[[nodiscard]] bool payloadFrom(const double retained,
                               std::int32_t &payload) noexcept {
  if (!std::isfinite(retained)) {
    return false;
  }
  const double rounded = std::nearbyint(retained);
  if (rounded < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      rounded > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return false;
  }
  payload = static_cast<std::int32_t>(rounded);
  return true;
}

} // namespace

LegacyAircraftAiDispatchResult legacyAircraftDispatchAiControlEvents(
    LegacyAircraftAiControlsState &controls,
    const LegacyAircraftAiEventSink &sink,
    const LegacyAircraftAiNumericPolicy policy) noexcept {
  if (sink.save == nullptr || sink.process == nullptr) {
    return {.status = LegacyAircraftAiDispatchStatus::invalidSink};
  }
  if (!legacyAircraftHasExactAiDispatcherRanges(controls)) {
    return {.status = LegacyAircraftAiDispatchStatus::invalidConfiguration};
  }
  if (!legacyAircraftAiNumericPolicyAvailable(policy)) {
    return {
        .status =
            policy ==
                    LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven
                ? LegacyAircraftAiDispatchStatus::numericEnvironmentUnavailable
                : LegacyAircraftAiDispatchStatus::unsupportedNumericPolicy,
    };
  }

  LegacyAircraftAiDispatchResult result{
      .status = LegacyAircraftAiDispatchStatus::completed,
  };
  for (const auto &route : routes) {
    if (!legacyAircraftHasExactAiDispatcherRanges(controls) ||
        !validChannelState(controls, route.channel)) {
      result.status = LegacyAircraftAiDispatchStatus::invalidConfiguration;
      return result;
    }
    const auto changed =
        legacyAircraftAiHasChanged(controls, route.channel, policy);
    if (!changed.complete()) {
      result.status = mapStatus(changed.status);
      return result;
    }
    if (!changed.changed) {
      continue;
    }

    const auto relative =
        legacyAircraftAiGetRelative(controls, route.channel, policy);
    if (!relative.complete()) {
      result.status = mapStatus(relative.status);
      return result;
    }
    std::int32_t payload = 0;
    if (!payloadFrom(*relative.retainedValue, payload)) {
      result.status = LegacyAircraftAiDispatchStatus::payloadOutOfRange;
      return result;
    }
    const LegacyAircraftAiDispatchedEvent event{
        .event = route.event,
        .channel = route.channel,
        .payload = payload,
    };
    sink.save(sink.context, event);
    sink.process(sink.context, event);
    ++result.emittedCount;
  }
  return result;
}

} // namespace airfix::simulation
