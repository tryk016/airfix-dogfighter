#pragma once

#include "airfix/simulation/LegacyAircraftAiControls.hpp"

#include <cstddef>
#include <cstdint>

namespace airfix::simulation {

enum class LegacyAircraftAiNativeEvent : std::uint8_t {
  pitchSet = 0x5F,
  thrustSet = 0x63,
  bankSet = 0x65,
  primaryAttack = 0xB9,
  secondaryAttack = 0xBA,
};

struct LegacyAircraftAiDispatchedEvent final {
  LegacyAircraftAiNativeEvent event{LegacyAircraftAiNativeEvent::thrustSet};
  std::uint8_t channel{};
  std::int32_t payload{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftAiDispatchedEvent &,
             const LegacyAircraftAiDispatchedEvent &) noexcept = default;
};

using LegacyAircraftAiEventCallback = void (*)(
    void *context, const LegacyAircraftAiDispatchedEvent &event) noexcept;

struct LegacyAircraftAiEventSink final {
  void *context{};
  LegacyAircraftAiEventCallback save{};
  LegacyAircraftAiEventCallback process{};
};

enum class LegacyAircraftAiDispatchStatus : std::uint8_t {
  completed,
  invalidSink,
  invalidConfiguration,
  unsupportedNumericPolicy,
  numericEnvironmentUnavailable,
  payloadOutOfRange,
};

struct LegacyAircraftAiDispatchResult final {
  LegacyAircraftAiDispatchStatus status{
      LegacyAircraftAiDispatchStatus::invalidConfiguration};
  std::uint8_t emittedCount{};

  [[nodiscard]] constexpr bool completed() const noexcept {
    return status == LegacyAircraftAiDispatchStatus::completed;
  }
};

// Reconstructs only the confirmed AirCraft five-channel dispatcher. Each
// changed row executes completely in native order:
//   HasChanged -> GetRelative/cache -> PC53/RNE integer conversion
//   -> sink.save -> sink.process
// before the next channel is inspected. The callbacks are deliberately
// synchronous; this boundary does not batch, sort, deduplicate, replay, or
// attach a clock/source identity. Callback-side mutations are visible to the
// next channel, just as an immediate native Process call would be.
//
// A valid sink and the exact recovered ranges are required before mutation.
// Later callback corruption fails at that exact point after already completed
// rows; this immediate dispatcher is intentionally not transactional.
[[nodiscard]] LegacyAircraftAiDispatchResult
legacyAircraftDispatchAiControlEvents(
    LegacyAircraftAiControlsState &controls,
    const LegacyAircraftAiEventSink &sink,
    LegacyAircraftAiNumericPolicy policy =
        LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven) noexcept;

} // namespace airfix::simulation
