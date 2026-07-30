#include "airfix/simulation/LegacyAircraftControlCommandReducer.hpp"

#include <cstddef>
#include <cstdint>

namespace airfix::simulation {
namespace {

enum class EventFamily : std::uint8_t {
  turn,
  pitch,
  bank,
  thrust,
};

struct CommandDescriptor final {
  LegacyAircraftControlCommand opposite{};
  EventFamily family{};
  std::int32_t activePayload{};
};

[[nodiscard]] bool descriptorFor(const LegacyAircraftControlCommand command,
                                 CommandDescriptor &descriptor) noexcept {
  using Command = LegacyAircraftControlCommand;

  switch (command) {
  case Command::turnLeft:
    descriptor = {Command::turnRight, EventFamily::turn, -32};
    return true;
  case Command::turnRight:
    descriptor = {Command::turnLeft, EventFamily::turn, 32};
    return true;
  case Command::thrustIncrease:
    descriptor = {Command::thrustDecrease, EventFamily::thrust, 255};
    return true;
  case Command::thrustDecrease:
    descriptor = {Command::thrustIncrease, EventFamily::thrust, -255};
    return true;
  case Command::pitchUp:
    descriptor = {Command::pitchDown, EventFamily::pitch, 32};
    return true;
  case Command::pitchDown:
    descriptor = {Command::pitchUp, EventFamily::pitch, -32};
    return true;
  case Command::bankLeft:
    descriptor = {Command::bankRight, EventFamily::bank, -32};
    return true;
  case Command::bankRight:
    descriptor = {Command::bankLeft, EventFamily::bank, 32};
    return true;
  case Command::count:
    return false;
  }

  return false;
}

void setActive(LegacyAircraftControlCommandState &state,
               const LegacyAircraftControlCommand command,
               const bool active) noexcept {
  const auto index = static_cast<std::size_t>(command);
  const auto bit = static_cast<std::uint8_t>(std::uint8_t{1U} << index);
  if (active) {
    state.activeCommandBits =
        static_cast<std::uint8_t>(state.activeCommandBits | bit);
  } else {
    state.activeCommandBits =
        static_cast<std::uint8_t>(state.activeCommandBits & ~bit);
  }
}

[[nodiscard]] LegacyAircraftControlCommandEventInput
makeEvent(const EventFamily family, const std::int32_t payload,
          const LegacyAircraftControlCommandInput &input) noexcept {
  switch (family) {
  case EventFamily::turn:
    return LegacyAircraftNativeTurnEventInput{
        LegacyAircraftNativeTurnEvent::turnSet,
        payload,
        input.vehicleInactive,
        input.angularNumericPolicy,
    };
  case EventFamily::pitch:
    return LegacyAircraftNativePitchBankEventInput{
        LegacyAircraftNativePitchBankEvent::pitchSet,
        payload,
        input.vehicleInactive,
        input.angularNumericPolicy,
    };
  case EventFamily::bank:
    return LegacyAircraftNativePitchBankEventInput{
        LegacyAircraftNativePitchBankEvent::bankSet,
        payload,
        input.vehicleInactive,
        input.angularNumericPolicy,
    };
  case EventFamily::thrust:
    return LegacyAircraftNativeThrustEventInput{
        LegacyAircraftNativeThrustEvent::thrustApply,
        payload,
        input.vehicleInactive,
    };
  }

  return LegacyAircraftNativeTurnEventInput{};
}

} // namespace

LegacyAircraftControlCommandReduceResult legacyAircraftReduceControlCommand(
    LegacyAircraftControlCommandState state,
    const LegacyAircraftControlCommandInput input) noexcept {
  CommandDescriptor descriptor;
  if (!descriptorFor(input.command, descriptor)) {
    return {
        LegacyAircraftControlCommandReduceStatus::unsupportedCommand,
        state,
        std::nullopt,
    };
  }

  setActive(state, input.command, input.active);

  std::int32_t payload = descriptor.activePayload;
  if (!input.active) {
    payload = state.active(descriptor.opposite) ? -descriptor.activePayload : 0;
  }

  return {
      LegacyAircraftControlCommandReduceStatus::emitted,
      state,
      makeEvent(descriptor.family, payload, input),
  };
}

} // namespace airfix::simulation
