#pragma once

#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::input {

// Stable V1 values deliberately mirror the platform adapter handoff. Keep
// platform frameworks out of this contract: adapters translate their native
// samples into these plain C++ values before reconciliation.
enum class ControllerDigitalControl : std::uint8_t {
    primaryTrigger = 0,
    pause = 1,
    secondaryTrigger = 2,
    throttleUp = 3,
    throttleDown = 4,
    weaponNext = 5,
    rearView = 6,
    cameraCycle = 7,
    missionStatus = 8,
    uiConfirm = 9,
    uiCancel = 10,
    cameraRecenter = 11,
    uiPrevious = 12,
    uiNext = 13,
    count = 14,
};

inline constexpr std::size_t controllerDigitalControlCount =
    static_cast<std::size_t>(ControllerDigitalControl::count);
inline constexpr std::size_t controllerAxisCount = 4U;

struct ControllerSample final {
    Q15 bank{};
    Q15 pitch{};
    Q15 lookX{};
    Q15 lookY{};
    bool primaryTriggerPressed{};
    bool pausePressed{};
    bool secondaryTriggerPressed{};
    bool throttleUpPressed{};
    bool throttleDownPressed{};
    bool weaponNextPressed{};
    bool rearViewPressed{};
    bool cameraCyclePressed{};
    bool missionStatusPressed{};
    bool uiConfirmPressed{};
    bool uiCancelPressed{};
    bool cameraRecenterPressed{};
    bool uiPreviousPressed{};
    bool uiNextPressed{};

    [[nodiscard]] bool pressed(ControllerDigitalControl control) const noexcept;
    void setPressed(ControllerDigitalControl control, bool value) noexcept;

    [[nodiscard]] friend constexpr bool operator==(const ControllerSample&,
        const ControllerSample&) noexcept = default;
};

struct ControllerDigitalEdge final {
    std::uint64_t generation{};
    std::uint64_t order{};
    ControllerDigitalControl control{ControllerDigitalControl::primaryTrigger};
    bool pressed{};

    [[nodiscard]] friend constexpr bool operator==(const ControllerDigitalEdge&,
        const ControllerDigitalEdge&) noexcept = default;
};

struct ControllerInputBatch final {
    static constexpr std::size_t edgeCapacity = 64U;

    std::uint64_t generation{};
    ControllerSample startingState{};
    ControllerSample finalState{};
    std::array<ControllerDigitalEdge, edgeCapacity> edges{};
    std::size_t edgeCount{};
    bool overflowed{};
};

struct ControllerInputEmission final {
    ControlId control{};
    PhysicalEventKind kind{PhysicalEventKind::digital};
    std::int32_t value{};

    [[nodiscard]] friend constexpr bool operator==(
        const ControllerInputEmission&,
        const ControllerInputEmission&) noexcept = default;
};

struct ControllerDigitalMapping final {
    ControlId control{};
    PhysicalEventKind kind{PhysicalEventKind::digital};

    [[nodiscard]] friend constexpr bool operator==(
        const ControllerDigitalMapping&,
        const ControllerDigitalMapping&) noexcept = default;
};

[[nodiscard]] constexpr ControllerDigitalMapping controllerDigitalMapping(
    const ControllerDigitalControl control) noexcept {
    using namespace controls::controller;
    switch (control) {
    case ControllerDigitalControl::primaryTrigger:
        return {rightTrigger, PhysicalEventKind::analog};
    case ControllerDigitalControl::pause:
        return {menu, PhysicalEventKind::digital};
    case ControllerDigitalControl::secondaryTrigger:
        return {leftTrigger, PhysicalEventKind::analog};
    case ControllerDigitalControl::throttleUp:
        return {dpadUp, PhysicalEventKind::digital};
    case ControllerDigitalControl::throttleDown:
        return {dpadDown, PhysicalEventKind::digital};
    case ControllerDigitalControl::weaponNext:
        return {rightShoulder, PhysicalEventKind::digital};
    case ControllerDigitalControl::rearView:
        return {leftShoulder, PhysicalEventKind::digital};
    case ControllerDigitalControl::cameraCycle:
        return {faceLeft, PhysicalEventKind::digital};
    case ControllerDigitalControl::missionStatus:
        return {faceTop, PhysicalEventKind::digital};
    case ControllerDigitalControl::uiConfirm:
        return {facePrimary, PhysicalEventKind::digital};
    case ControllerDigitalControl::uiCancel:
        return {faceSecondary, PhysicalEventKind::digital};
    case ControllerDigitalControl::cameraRecenter:
        return {rightStickClick, PhysicalEventKind::digital};
    case ControllerDigitalControl::uiPrevious:
        return {dpadLeft, PhysicalEventKind::digital};
    case ControllerDigitalControl::uiNext:
        return {dpadRight, PhysicalEventKind::digital};
    case ControllerDigitalControl::count:
        break;
    }
    return {};
}

enum class ControllerBatchStatus : std::uint8_t {
    accepted = 0,
    invalidGeneration,
    inputOverflow,
    inputCapacityExceeded,
    invalidAxis,
    calibrationFailed,
    startingStateMismatch,
    invalidEdgeGeneration,
    invalidEdgeOrder,
    invalidEdgeControl,
    invalidEdgeTransition,
    outputCapacityExceeded,
};

struct ControllerBatchResult final {
    ControllerBatchStatus status{ControllerBatchStatus::accepted};
    std::size_t emissionCount{};

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return status == ControllerBatchStatus::accepted;
    }

    [[nodiscard]] friend constexpr bool operator==(const ControllerBatchResult&,
        const ControllerBatchResult&) noexcept = default;
};

class ControllerInputBatchBridge final {
public:
    static constexpr std::int32_t stickDeadzone = 4096;
    static constexpr std::int32_t meaningfulAxisDelta = 1024;
    static constexpr std::size_t maximumEmissionCount =
        controllerAxisCount + (controllerDigitalControlCount * 2U) +
        ControllerInputBatch::edgeCapacity;

    ControllerInputBatchBridge() noexcept = default;
    explicit ControllerInputBatchBridge(
        const ControllerInputRuntimeConfiguration& configuration) noexcept
        : configuration_(configuration) {}

    // The output order is deterministic: final continuous axes, a starting
    // digital full-state for a new generation, ordered edges, then any final
    // digital reconciliation. No caller output or bridge state changes on
    // rejection, including insufficient output capacity.
    [[nodiscard]] ControllerBatchResult process(
        const ControllerInputBatch& batch,
        std::span<ControllerInputEmission> output) noexcept;

    void reset() noexcept;

    [[nodiscard]] constexpr std::uint64_t currentGeneration() const noexcept {
        return hasGeneration_ ? generation_ : 0U;
    }

    [[nodiscard]] constexpr const ResolvedControllerInputProfile*
    controllerProfile() const noexcept {
        return configuration_ ? &configuration_->profile() : nullptr;
    }

private:
    std::optional<ControllerInputRuntimeConfiguration> configuration_;
    std::uint64_t generation_{};
    std::uint64_t lastEdgeOrder_{};
    ControllerSample acceptedSample_{};
    std::array<Q15, controllerAxisCount> submittedAxes_{};
    bool hasGeneration_{};
};

static_assert(ControllerInputBatchBridge::maximumEmissionCount == 96U);

} // namespace airfix::input
