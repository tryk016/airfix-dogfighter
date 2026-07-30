#pragma once

#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftThrustEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftTurnEventReducer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace airfix::simulation {

// The eight bool-valued commands stored by the recovered native command
// callback. Values follow the byte layout of its callback subobject.
enum class LegacyAircraftControlCommand : std::uint8_t {
  turnLeft = 0,
  turnRight = 1,
  thrustIncrease = 2,
  thrustDecrease = 3,
  pitchUp = 4,
  pitchDown = 5,
  bankLeft = 6,
  bankRight = 7,
  count = 8,
};

// Caller-owned snapshot of those eight command flags. Every bit is assigned;
// there are no reserved bits or hidden ordering fields.
struct LegacyAircraftControlCommandState final {
  std::uint8_t activeCommandBits{};

  [[nodiscard]] constexpr bool
  active(const LegacyAircraftControlCommand command) const noexcept {
    const auto index = static_cast<std::size_t>(command);
    if (index >=
        static_cast<std::size_t>(LegacyAircraftControlCommand::count)) {
      return false;
    }
    return (activeCommandBits &
            static_cast<std::uint8_t>(std::uint8_t{1U} << index)) != 0U;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftControlCommandState &,
             const LegacyAircraftControlCommandState &) noexcept = default;
};

struct LegacyAircraftControlCommandInput final {
  LegacyAircraftControlCommand command{LegacyAircraftControlCommand::turnLeft};
  bool active{};
  bool vehicleInactive{};
  LegacyAircraftAngularSetNumericPolicy angularNumericPolicy{
      LegacyAircraftAngularSetNumericPolicy::startupPc53RoundToNearestEven};
};

using LegacyAircraftControlCommandEventInput =
    std::variant<LegacyAircraftNativeTurnEventInput,
                 LegacyAircraftNativePitchBankEventInput,
                 LegacyAircraftNativeThrustEventInput>;

enum class LegacyAircraftControlCommandReduceStatus : std::uint8_t {
  emitted,
  unsupportedCommand,
};

struct LegacyAircraftControlCommandReduceResult final {
  LegacyAircraftControlCommandReduceStatus status{
      LegacyAircraftControlCommandReduceStatus::unsupportedCommand};
  LegacyAircraftControlCommandState state{};
  std::optional<LegacyAircraftControlCommandEventInput> event;

  [[nodiscard]] constexpr bool emitted() const noexcept {
    return status == LegacyAircraftControlCommandReduceStatus::emitted &&
           event.has_value();
  }

  [[nodiscard]] constexpr bool failed() const noexcept {
    return status ==
               LegacyAircraftControlCommandReduceStatus::unsupportedCommand &&
           !event.has_value();
  }
};

// Reduces one already-ordered, already-parsed bool command invocation. The
// native callback first overwrites its own flag, then emits its signed payload;
// on false it emits the opposite command's payload if that flag remains set,
// otherwise zero. Every recognized invocation emits exactly one event,
// including repeated true/false values and zero releases.
//
// The result deliberately remains a typed native-event input. Existing native
// event reducers own the vehicle-inactive gate and numeric conversion, and the
// separate control-event owner owns mutation. This boundary owns no argv
// parsing, InputFrame/Q15 conversion, device state, queue, cross-producer
// ordering, repeat cadence, lifecycle reset, scheduler, or timing.
[[nodiscard]] LegacyAircraftControlCommandReduceResult
legacyAircraftReduceControlCommand(
    LegacyAircraftControlCommandState state,
    LegacyAircraftControlCommandInput input) noexcept;

} // namespace airfix::simulation
