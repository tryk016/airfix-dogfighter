#include "airfix/input/ControllerInputBatchBridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::input {
namespace {

[[nodiscard]] constexpr bool validControl(
    const ControllerDigitalControl control) noexcept {
    return static_cast<std::uint8_t>(control) <
           static_cast<std::uint8_t>(ControllerDigitalControl::count);
}

[[nodiscard]] constexpr bool validAxis(const Q15 value) noexcept {
    return value >= q15Min;
}

[[nodiscard]] constexpr Q15 stableAxis(const Q15 value) noexcept {
    const auto wide = static_cast<std::int32_t>(value);
    const auto magnitude = wide < 0 ? -wide : wide;
    return magnitude < ControllerInputBatchBridge::stickDeadzone ? q15Zero
                                                                 : value;
}

[[nodiscard]] constexpr bool shouldSubmitAxis(const Q15 previous,
    const Q15 current) noexcept {
    if (previous == current) {
        return false;
    }
    if (previous == q15Zero || current == q15Zero) {
        return true;
    }
    const auto difference = static_cast<std::int32_t>(current) -
                            static_cast<std::int32_t>(previous);
    const auto magnitude = difference < 0 ? -difference : difference;
    return magnitude >= ControllerInputBatchBridge::meaningfulAxisDelta;
}

[[nodiscard]] constexpr std::array<Q15, controllerAxisCount> axes(
    const ControllerSample& sample) noexcept {
    return {sample.bank, sample.pitch, sample.lookX, sample.lookY};
}

[[nodiscard]] constexpr ControllerAxisElement axisElement(
    const std::size_t index) noexcept {
    return index < controllerAxisCount
               ? static_cast<ControllerAxisElement>(index)
               : ControllerAxisElement::count;
}

inline constexpr std::array<ControlId, controllerAxisCount> axisControls{
    controls::controller::leftStickX,
    controls::controller::leftStickY,
    controls::controller::rightStickX,
    controls::controller::rightStickY,
};

[[nodiscard]] constexpr ControllerInputEmission axisEmission(
    const std::size_t index,
    const Q15 value) noexcept {
    return {
        axisControls[index],
        PhysicalEventKind::analog,
        static_cast<std::int32_t>(value),
    };
}

[[nodiscard]] constexpr ControllerInputEmission digitalEmission(
    const ControllerDigitalControl control,
    const bool pressed) noexcept {
    const auto mapping = controllerDigitalMapping(control);
    return {
        mapping.control,
        mapping.kind,
        pressed ? static_cast<std::int32_t>(q15One) : 0,
    };
}

template <std::size_t Capacity>
[[nodiscard]] constexpr bool append(
    std::array<ControllerInputEmission, Capacity>& emissions,
    std::size_t& size,
    const ControllerInputEmission emission) noexcept {
    if (size == Capacity) {
        return false;
    }
    emissions[size] = emission;
    ++size;
    return true;
}

} // namespace

std::optional<Q15> transformControllerAxisForTransport(
    const Q15 raw, const ControllerAxisElement axis,
    const ResolvedControllerInputProfile &profile) noexcept {
  if (!validAxis(raw)) {
    return std::nullopt;
  }
  return transformControllerAxis(stableAxis(raw), axis, profile);
}

bool ControllerSample::pressed(
    const ControllerDigitalControl control) const noexcept {
    switch (control) {
    case ControllerDigitalControl::primaryTrigger:
        return primaryTriggerPressed;
    case ControllerDigitalControl::pause:
        return pausePressed;
    case ControllerDigitalControl::secondaryTrigger:
        return secondaryTriggerPressed;
    case ControllerDigitalControl::throttleUp:
        return throttleUpPressed;
    case ControllerDigitalControl::throttleDown:
        return throttleDownPressed;
    case ControllerDigitalControl::weaponNext:
        return weaponNextPressed;
    case ControllerDigitalControl::rearView:
        return rearViewPressed;
    case ControllerDigitalControl::cameraCycle:
        return cameraCyclePressed;
    case ControllerDigitalControl::missionStatus:
        return missionStatusPressed;
    case ControllerDigitalControl::uiConfirm:
        return uiConfirmPressed;
    case ControllerDigitalControl::uiCancel:
        return uiCancelPressed;
    case ControllerDigitalControl::cameraRecenter:
        return cameraRecenterPressed;
    case ControllerDigitalControl::uiPrevious:
        return uiPreviousPressed;
    case ControllerDigitalControl::uiNext:
        return uiNextPressed;
    case ControllerDigitalControl::count:
        break;
    }
    return false;
}

void ControllerSample::setPressed(const ControllerDigitalControl control,
    const bool value) noexcept {
    switch (control) {
    case ControllerDigitalControl::primaryTrigger:
        primaryTriggerPressed = value;
        break;
    case ControllerDigitalControl::pause:
        pausePressed = value;
        break;
    case ControllerDigitalControl::secondaryTrigger:
        secondaryTriggerPressed = value;
        break;
    case ControllerDigitalControl::throttleUp:
        throttleUpPressed = value;
        break;
    case ControllerDigitalControl::throttleDown:
        throttleDownPressed = value;
        break;
    case ControllerDigitalControl::weaponNext:
        weaponNextPressed = value;
        break;
    case ControllerDigitalControl::rearView:
        rearViewPressed = value;
        break;
    case ControllerDigitalControl::cameraCycle:
        cameraCyclePressed = value;
        break;
    case ControllerDigitalControl::missionStatus:
        missionStatusPressed = value;
        break;
    case ControllerDigitalControl::uiConfirm:
        uiConfirmPressed = value;
        break;
    case ControllerDigitalControl::uiCancel:
        uiCancelPressed = value;
        break;
    case ControllerDigitalControl::cameraRecenter:
        cameraRecenterPressed = value;
        break;
    case ControllerDigitalControl::uiPrevious:
        uiPreviousPressed = value;
        break;
    case ControllerDigitalControl::uiNext:
        uiNextPressed = value;
        break;
    case ControllerDigitalControl::count:
        break;
    }
}

ControllerBatchResult ControllerInputBatchBridge::process(
    const ControllerInputBatch& batch,
    const std::span<ControllerInputEmission> output) noexcept {
    if (batch.generation == 0U) {
        return {ControllerBatchStatus::invalidGeneration, 0U};
    }
    if (hasGeneration_ && batch.generation < generation_) {
        return {ControllerBatchStatus::invalidGeneration, 0U};
    }
    if (batch.overflowed) {
        return {ControllerBatchStatus::inputOverflow, 0U};
    }
    if (batch.edgeCount > ControllerInputBatch::edgeCapacity) {
        return {ControllerBatchStatus::inputCapacityExceeded, 0U};
    }

    const auto startingAxes = axes(batch.startingState);
    const auto finalAxes = axes(batch.finalState);
    for (const auto value : startingAxes) {
        if (!validAxis(value)) {
            return {ControllerBatchStatus::invalidAxis, 0U};
        }
    }
    for (const auto value : finalAxes) {
        if (!validAxis(value)) {
            return {ControllerBatchStatus::invalidAxis, 0U};
        }
    }

    const bool fullState = !hasGeneration_ || generation_ < batch.generation;
    if (!fullState && batch.startingState != acceptedSample_) {
        return {ControllerBatchStatus::startingStateMismatch, 0U};
    }

    auto digitalState = batch.startingState;
    std::uint64_t candidateLastOrder = fullState ? 0U : lastEdgeOrder_;
    for (std::size_t index = 0U; index < batch.edgeCount; ++index) {
        const auto& edge = batch.edges[index];
        if (edge.generation != batch.generation) {
            return {ControllerBatchStatus::invalidEdgeGeneration, 0U};
        }
        if (edge.order == 0U || edge.order <= candidateLastOrder) {
            return {ControllerBatchStatus::invalidEdgeOrder, 0U};
        }
        if (!validControl(edge.control)) {
            return {ControllerBatchStatus::invalidEdgeControl, 0U};
        }
        if (digitalState.pressed(edge.control) == edge.pressed) {
            return {ControllerBatchStatus::invalidEdgeTransition, 0U};
        }
        digitalState.setPressed(edge.control, edge.pressed);
        candidateLastOrder = edge.order;
    }

    std::array<ControllerInputEmission, maximumEmissionCount> scratch{};
    std::size_t emissionCount = 0U;
    auto candidateSubmittedAxes = submittedAxes_;

    for (std::size_t index = 0U; index < controllerAxisCount; ++index) {
        if (configuration_.has_value() &&
            !configuration_->usesControllerControl(axisControls[index])) {
            continue;
        }
        Q15 stabilized{};
        if (configuration_.has_value()) {
          const auto calibrated = transformControllerAxisForTransport(
              finalAxes[index], axisElement(index), configuration_->profile());
          if (!calibrated.has_value()) {
            return {ControllerBatchStatus::calibrationFailed, 0U};
          }
            stabilized = *calibrated;
        } else {
          stabilized = stableAxis(finalAxes[index]);
        }
        const bool changed = candidateSubmittedAxes[index] != stabilized;
        const bool shouldSubmit =
            configuration_.has_value()
                ? changed
                : shouldSubmitAxis(candidateSubmittedAxes[index], stabilized);
        if (fullState || shouldSubmit) {
            if (!append(scratch,
                    emissionCount,
                    axisEmission(index, stabilized))) {
                return {ControllerBatchStatus::outputCapacityExceeded, 0U};
            }
            candidateSubmittedAxes[index] = stabilized;
        }
    }

    if (fullState) {
        for (std::size_t index = 0U; index < controllerDigitalControlCount;
            ++index) {
            const auto control = static_cast<ControllerDigitalControl>(index);
            const auto mapping = controllerDigitalMapping(control);
            if (configuration_.has_value() &&
                !configuration_->usesControllerControl(mapping.control)) {
                continue;
            }
            if (!append(scratch,
                    emissionCount,
                    digitalEmission(control,
                        batch.startingState.pressed(control)))) {
                return {ControllerBatchStatus::outputCapacityExceeded, 0U};
            }
        }
    }

    auto replayState = batch.startingState;
    for (std::size_t index = 0U; index < batch.edgeCount; ++index) {
        const auto& edge = batch.edges[index];
        const auto mapping = controllerDigitalMapping(edge.control);
        if (configuration_.has_value() &&
            !configuration_->usesControllerControl(mapping.control)) {
            replayState.setPressed(edge.control, edge.pressed);
            continue;
        }
        if (!append(scratch,
                emissionCount,
                digitalEmission(edge.control, edge.pressed))) {
            return {ControllerBatchStatus::outputCapacityExceeded, 0U};
        }
        replayState.setPressed(edge.control, edge.pressed);
    }

    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        const auto control = static_cast<ControllerDigitalControl>(index);
        const bool finalPressed = batch.finalState.pressed(control);
        if (replayState.pressed(control) == finalPressed) {
            continue;
        }
        const auto mapping = controllerDigitalMapping(control);
        if (configuration_.has_value() &&
            !configuration_->usesControllerControl(mapping.control)) {
            replayState.setPressed(control, finalPressed);
            continue;
        }
        if (!append(scratch,
                emissionCount,
                digitalEmission(control, finalPressed))) {
            return {ControllerBatchStatus::outputCapacityExceeded, 0U};
        }
        replayState.setPressed(control, finalPressed);
    }

    if (output.size() < emissionCount) {
        return {ControllerBatchStatus::outputCapacityExceeded, 0U};
    }
    for (std::size_t index = 0U; index < emissionCount; ++index) {
        output[index] = scratch[index];
    }

    generation_ = batch.generation;
    lastEdgeOrder_ = candidateLastOrder;
    acceptedSample_ = batch.finalState;
    submittedAxes_ = candidateSubmittedAxes;
    hasGeneration_ = true;
    return {ControllerBatchStatus::accepted, emissionCount};
}

void ControllerInputBatchBridge::reset() noexcept {
    generation_ = 0U;
    lastEdgeOrder_ = 0U;
    acceptedSample_ = {};
    submittedAxes_ = {};
    hasGeneration_ = false;
}

} // namespace airfix::input
