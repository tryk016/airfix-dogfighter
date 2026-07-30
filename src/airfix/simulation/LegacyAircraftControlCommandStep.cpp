#include "airfix/simulation/LegacyAircraftControlCommandStep.hpp"

#include <optional>
#include <utility>
#include <variant>

namespace airfix::simulation {
namespace {

enum class DecodeDisposition : std::uint8_t {
  decoded,
  ignoredInactive,
  rejected,
};

struct DecodeResult final {
  DecodeDisposition disposition{DecodeDisposition::rejected};
  std::optional<LegacyAircraftControlCommandWrite> write;
};

[[nodiscard]] DecodeResult
decode(const LegacyAircraftNativeTurnEventInput input) noexcept {
  const auto result = legacyAircraftDecodeNativeTurnEvent(input);
  if (result.decoded()) {
    return {
        DecodeDisposition::decoded,
        LegacyAircraftControlCommandWrite{*result.write},
    };
  }
  if (result.ignored()) {
    return {DecodeDisposition::ignoredInactive, std::nullopt};
  }
  return {DecodeDisposition::rejected, std::nullopt};
}

[[nodiscard]] DecodeResult
decode(const LegacyAircraftNativePitchBankEventInput input) noexcept {
  const auto result = legacyAircraftDecodeNativePitchBankEvent(input);
  if (result.decoded()) {
    return {
        DecodeDisposition::decoded,
        LegacyAircraftControlCommandWrite{*result.write},
    };
  }
  if (result.ignored()) {
    return {DecodeDisposition::ignoredInactive, std::nullopt};
  }
  return {DecodeDisposition::rejected, std::nullopt};
}

[[nodiscard]] DecodeResult
decode(const LegacyAircraftNativeThrustEventInput input) noexcept {
  const auto result = legacyAircraftDecodeNativeThrustEvent(input);
  if (result.decoded()) {
    return {
        DecodeDisposition::decoded,
        LegacyAircraftControlCommandWrite{*result.write},
    };
  }
  if (result.ignored()) {
    return {DecodeDisposition::ignoredInactive, std::nullopt};
  }
  return {DecodeDisposition::rejected, std::nullopt};
}

[[nodiscard]] LegacyAircraftControlEventCommitStatus
commit(LegacyAircraftControlEventStateOwner &owner,
       const LegacyAircraftControlCommandWrite &write) noexcept {
  return std::visit(
      [&owner](const auto &typedWrite) noexcept {
        return owner.tryApply(typedWrite);
      },
      write);
}

} // namespace

LegacyAircraftControlCommandStepResult legacyAircraftAdvanceControlCommandStep(
    LegacyAircraftControlCommandStepState state,
    const LegacyAircraftControlCommandInput input) noexcept {
  auto reduced = legacyAircraftReduceControlCommand(state.commandState, input);
  if (!reduced.emitted()) {
    return {
        LegacyAircraftControlCommandStepStatus::unsupportedCommand,
        state,
        std::nullopt,
        std::nullopt,
    };
  }

  state.commandState = reduced.state;
  auto event = std::move(reduced.event);
  const auto decoded = std::visit(
      [](const auto typedEvent) noexcept { return decode(typedEvent); },
      *event);

  if (decoded.disposition == DecodeDisposition::ignoredInactive) {
    return {
        LegacyAircraftControlCommandStepStatus::ignoredInactive,
        state,
        std::move(event),
        std::nullopt,
    };
  }
  if (decoded.disposition != DecodeDisposition::decoded ||
      !decoded.write.has_value()) {
    return {
        LegacyAircraftControlCommandStepStatus::decodeRejected,
        state,
        std::move(event),
        std::nullopt,
    };
  }

  LegacyAircraftControlEventStateOwner owner{state.controlEventState};
  if (commit(owner, *decoded.write) !=
      LegacyAircraftControlEventCommitStatus::committed) {
    return {
        LegacyAircraftControlCommandStepStatus::commitRejected,
        state,
        std::move(event),
        decoded.write,
    };
  }

  state.controlEventState = owner.snapshot();
  return {
      LegacyAircraftControlCommandStepStatus::committed,
      state,
      std::move(event),
      decoded.write,
  };
}

} // namespace airfix::simulation
