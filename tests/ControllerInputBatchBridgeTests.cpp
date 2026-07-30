#include "airfix/input/ControllerInputBatchBridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using airfix::input::ControlId;
using airfix::input::controllerAxisCount;
using airfix::input::ControllerBatchStatus;
using airfix::input::ControllerDigitalControl;
using airfix::input::controllerDigitalControlCount;
using airfix::input::ControllerDigitalEdge;
using airfix::input::controllerDigitalMapping;
using airfix::input::ControllerInputBatch;
using airfix::input::ControllerInputBatchBridge;
using airfix::input::ControllerInputEmission;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::ControllerSample;
using airfix::input::PhysicalEventKind;
using airfix::input::q15One;

using OutputBuffer = std::array<ControllerInputEmission,
    ControllerInputBatchBridge::maximumEmissionCount>;

constexpr ControllerInputEmission sentinel{
    ControlId{65000U},
    PhysicalEventKind::weaponSelection,
    -12345,
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] constexpr ControllerDigitalControl digitalControl(
    const std::size_t index) noexcept {
    return static_cast<ControllerDigitalControl>(index);
}

[[nodiscard]] ControllerInputBatch batch(const std::uint64_t generation,
    const ControllerSample& starting,
    const ControllerSample& final) {
    ControllerInputBatch value{};
    value.generation = generation;
    value.startingState = starting;
    value.finalState = final;
    return value;
}

void fillSentinel(OutputBuffer& output) { output.fill(sentinel); }

void requireUntouched(const OutputBuffer& output, const std::string& context) {
    for (const auto& emission : output) {
        require(emission == sentinel, context + " changed caller output");
    }
}

void requireEmission(const ControllerInputEmission& actual,
    const ControlId control,
    const PhysicalEventKind kind,
    const std::int32_t value,
    const std::string& context) {
    require(actual.control == control, context + " mapped wrong control");
    require(actual.kind == kind, context + " mapped wrong event kind");
    require(actual.value == value, context + " emitted wrong value");
}

[[nodiscard]] std::size_t accept(ControllerInputBatchBridge& bridge,
    const ControllerInputBatch& input,
    OutputBuffer& output) {
    const auto result = bridge.process(input, output);
    require(result.accepted(), "expected batch acceptance");
    return result.emissionCount;
}

void establishNeutral(ControllerInputBatchBridge& bridge,
    const std::uint64_t generation = 1U) {
    OutputBuffer output{};
    require(accept(bridge, batch(generation, {}, {}), output) ==
                controllerAxisCount + controllerDigitalControlCount,
        "neutral full state emitted unexpected count");
}

void testAllDigitalMappings() {
    using namespace airfix::input::controls::controller;
    constexpr std::array expected{
        airfix::input::ControllerDigitalMapping{rightTrigger,
            PhysicalEventKind::analog},
        airfix::input::ControllerDigitalMapping{menu,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{leftTrigger,
            PhysicalEventKind::analog},
        airfix::input::ControllerDigitalMapping{dpadUp,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{dpadDown,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{rightShoulder,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{leftShoulder,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{faceLeft,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{faceTop,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{facePrimary,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{faceSecondary,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{rightStickClick,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{dpadLeft,
            PhysicalEventKind::digital},
        airfix::input::ControllerDigitalMapping{dpadRight,
            PhysicalEventKind::digital},
    };

    static_assert(expected.size() == controllerDigitalControlCount);
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        require(
            controllerDigitalMapping(digitalControl(index)) == expected[index],
            "V1 digital control mapping mismatch");
    }
    require(!controllerDigitalMapping(ControllerDigitalControl::count)
                .control.valid(),
        "out-of-range digital mapping should be invalid");
}

void testNeutralFullState() {
    ControllerInputBatchBridge bridge;
    OutputBuffer output{};
    const auto input = batch(7U, {}, {});
    const auto count = accept(bridge, input, output);

    require(count == 18U,
        "neutral full state must emit four axes and 14 buttons");
    constexpr std::array axisControls{
        airfix::input::controls::controller::leftStickX,
        airfix::input::controls::controller::leftStickY,
        airfix::input::controls::controller::rightStickX,
        airfix::input::controls::controller::rightStickY,
    };
    for (std::size_t index = 0U; index < axisControls.size(); ++index) {
        requireEmission(output[index],
            axisControls[index],
            PhysicalEventKind::analog,
            0,
            "neutral full-state axis");
    }
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        const auto mapping = controllerDigitalMapping(digitalControl(index));
        requireEmission(output[controllerAxisCount + index],
            mapping.control,
            mapping.kind,
            0,
            "neutral full-state digital");
    }
    require(bridge.currentGeneration() == 7U, "generation was not committed");
}

void testNonNeutralFullStateAndDeadzone() {
    ControllerSample sample{};
    sample.bank = 5000;
    sample.pitch = -5000;
    sample.lookX = 4095;
    sample.lookY = -4096;
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        sample.setPressed(digitalControl(index), true);
    }

    ControllerInputBatchBridge bridge;
    OutputBuffer output{};
    const auto count = accept(bridge, batch(2U, sample, sample), output);
    require(count == 18U, "non-neutral full state emitted unexpected count");
    require(output[0U].value == 5000, "bank was changed");
    require(output[1U].value == -5000, "pitch was changed");
    require(output[2U].value == 0, "positive deadzone boundary was wrong");
    require(output[3U].value == -4096, "negative deadzone boundary was wrong");
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        require(output[controllerAxisCount + index].value == q15One,
            "non-neutral full state omitted a held digital control");
    }
}

void testResolvedCalibrationRunsAfterTransportDeadzone() {
    ControllerInputProfileRecord record =
        airfix::input::makeDefaultControllerInputProfileRecord();
    record.axes[0U].innerDeadzoneQ15 = 5000U;
    record.axes[0U].outerSaturationQ15 = 25000U;
    record.axes[0U].sensitivityPermille = 1250U;
    record.axes[0U].inverted = 1U;
    const auto resolved = airfix::input::resolveControllerInputProfile(record);
    require(resolved.complete(), "calibrated profile did not resolve");
    const auto prepared =
        airfix::input::prepareControllerInputRuntimeConfiguration(
            *resolved.profile);
    require(prepared.complete(), "calibrated configuration did not prepare");

    ControllerInputBatchBridge bridge{*prepared.configuration};
    require(bridge.controllerProfile() != nullptr,
        "configured bridge lost its profile");
    ControllerSample sample{};
    sample.bank = 15000;
    OutputBuffer output{};
    const auto count = accept(bridge, batch(17U, {}, sample), output);
    require(count == controllerAxisCount + controllerDigitalControlCount,
        "configured full state emitted an unexpected count");
    requireEmission(output[0U],
        airfix::input::controls::controller::leftStickX,
        PhysicalEventKind::analog,
        -20480,
        "calibrated left-stick axis");

    bridge.reset();
    require(bridge.controllerProfile() != nullptr &&
                bridge.controllerProfile()->record() == record,
        "bridge reset discarded the active calibration");
}

void testConfiguredDefaultPreservesLegacyTransport() {
    const auto resolved = airfix::input::resolveControllerInputProfile(
        airfix::input::makeDefaultControllerInputProfileRecord());
    require(resolved.complete(), "default profile did not resolve");
    const auto prepared =
        airfix::input::prepareControllerInputRuntimeConfiguration(
            *resolved.profile);
    require(prepared.complete(), "default configuration did not prepare");

    ControllerSample sample{};
    sample.bank = 4095;
    sample.pitch = -4096;
    sample.lookX = 12000;
    sample.lookY = -13000;
    const auto input = batch(23U, {}, sample);
    ControllerInputBatchBridge legacy;
    ControllerInputBatchBridge configured{*prepared.configuration};
    OutputBuffer legacyOutput{};
    OutputBuffer configuredOutput{};
    const auto legacyResult = legacy.process(input, legacyOutput);
    const auto configuredResult = configured.process(input, configuredOutput);
    require(legacyResult == configuredResult,
        "default profile changed the legacy bridge result");
    require(configuredOutput[0U].value == 0,
        "profile bypassed the V1 transport deadzone");
    require(configuredOutput[1U].value == -4096,
        "profile changed the inclusive transport boundary");
    for (std::size_t index = 0U; index < legacyResult.emissionCount; ++index) {
        require(legacyOutput[index] == configuredOutput[index],
            "default profile changed a legacy emission");
    }
}

void testConfiguredAxesEmitEveryChangedCalibratedValue() {
    const auto resolved = airfix::input::resolveControllerInputProfile(
        airfix::input::makeDefaultControllerInputProfileRecord());
    require(resolved.complete(), "default profile did not resolve");
    const auto prepared =
        airfix::input::prepareControllerInputRuntimeConfiguration(
            *resolved.profile);
    require(prepared.complete(), "default configuration did not prepare");

    ControllerInputBatchBridge bridge{*prepared.configuration};
    ControllerSample below{};
    below.bank = static_cast<airfix::input::Q15>(
        airfix::input::controllerTriggerActuationQ15 - 384);
    OutputBuffer output{};
    (void)accept(bridge, batch(29U, below, below), output);

    auto above = below;
    above.bank = static_cast<airfix::input::Q15>(
        airfix::input::controllerTriggerActuationQ15 + 116);
    const auto count = accept(bridge, batch(29U, below, above), output);
    require(count == 1U, "profiled bridge filtered a changed calibrated axis");
    requireEmission(output[0U],
        airfix::input::controls::controller::leftStickX,
        PhysicalEventKind::analog,
        above.bank,
        "profiled threshold crossing");
}

void testMeaningfulAxisChanges() {
    ControllerSample initial{};
    initial.bank = 5000;
    initial.pitch = -5000;
    ControllerInputBatchBridge bridge;
    OutputBuffer output{};
    (void)accept(bridge, batch(1U, initial, initial), output);

    auto belowThreshold = initial;
    belowThreshold.bank = 6000;
    belowThreshold.pitch = -6000;
    belowThreshold.lookX = 4095;
    require(accept(bridge, batch(1U, initial, belowThreshold), output) == 0U,
        "sub-threshold/deadzone axes should not emit");

    auto atThreshold = belowThreshold;
    atThreshold.bank = 6024;
    atThreshold.pitch = -6024;
    atThreshold.lookX = 4096;
    require(
        accept(bridge, batch(1U, belowThreshold, atThreshold), output) == 3U,
        "threshold and deadzone exit should emit exactly three axes");
    requireEmission(output[0U],
        airfix::input::controls::controller::leftStickX,
        PhysicalEventKind::analog,
        6024,
        "bank threshold");
    requireEmission(output[1U],
        airfix::input::controls::controller::leftStickY,
        PhysicalEventKind::analog,
        -6024,
        "pitch threshold");
    requireEmission(output[2U],
        airfix::input::controls::controller::rightStickX,
        PhysicalEventKind::analog,
        4096,
        "look deadzone exit");

    auto reenterDeadzone = atThreshold;
    reenterDeadzone.lookX = 4095;
    require(
        accept(bridge, batch(1U, atThreshold, reenterDeadzone), output) == 1U,
        "deadzone re-entry should emit zero");
    require(output[0U].control ==
                    airfix::input::controls::controller::rightStickX &&
                output[0U].value == 0,
        "deadzone re-entry emitted the wrong axis value");
}

void testCompleteTapsForEveryDigitalControl() {
    ControllerInputBatchBridge bridge;
    establishNeutral(bridge);

    ControllerInputBatch input = batch(1U, {}, {});
    std::uint64_t order = 1U;
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        const auto control = digitalControl(index);
        input.edges[input.edgeCount++] =
            ControllerDigitalEdge{1U, order++, control, true};
        input.edges[input.edgeCount++] =
            ControllerDigitalEdge{1U, order++, control, false};
    }

    OutputBuffer output{};
    const auto count = accept(bridge, input, output);
    require(count == controllerDigitalControlCount * 2U,
        "complete taps lost or added digital edges");
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        const auto mapping = controllerDigitalMapping(digitalControl(index));
        requireEmission(output[index * 2U],
            mapping.control,
            mapping.kind,
            q15One,
            "tap press");
        requireEmission(output[index * 2U + 1U],
            mapping.control,
            mapping.kind,
            0,
            "tap release");
    }
}

void testMixedOrderedEdgesAndFinalReconciliation() {
    ControllerInputBatchBridge bridge;
    establishNeutral(bridge, 3U);

    ControllerInputBatch mixed = batch(3U, {}, {});
    mixed.edges[0U] = {3U, 1U, ControllerDigitalControl::throttleUp, true};
    mixed.edges[1U] = {3U, 2U, ControllerDigitalControl::primaryTrigger, true};
    mixed.edges[2U] = {3U, 3U, ControllerDigitalControl::throttleUp, false};
    mixed.edges[3U] = {3U, 4U, ControllerDigitalControl::pause, true};
    mixed.edgeCount = 4U;
    mixed.finalState.primaryTriggerPressed = true;
    mixed.finalState.pausePressed = true;

    OutputBuffer output{};
    require(accept(bridge, mixed, output) == 4U,
        "mixed edge sequence emitted unexpected reconciliation");
    require(output[0U].control == airfix::input::controls::controller::dpadUp &&
                output[0U].value == q15One,
        "mixed edge 0 was reordered");
    require(output[1U].control ==
                    airfix::input::controls::controller::rightTrigger &&
                output[1U].value == q15One,
        "mixed edge 1 was reordered");
    require(output[2U].control == airfix::input::controls::controller::dpadUp &&
                output[2U].value == 0,
        "mixed edge 2 was reordered");
    require(output[3U].control == airfix::input::controls::controller::menu &&
                output[3U].value == q15One,
        "mixed edge 3 was reordered");

    ControllerInputBatch reconcile =
        batch(3U, mixed.finalState, mixed.finalState);
    reconcile.finalState.primaryTriggerPressed = false;
    reconcile.finalState.secondaryTriggerPressed = true;
    require(accept(bridge, reconcile, output) == 2U,
        "final state reconciliation did not emit exact differences");
    require(output[0U].control ==
                    airfix::input::controls::controller::rightTrigger &&
                output[0U].value == 0,
        "final primary release was wrong");
    require(output[1U].control ==
                    airfix::input::controls::controller::leftTrigger &&
                output[1U].value == q15One,
        "final secondary press was wrong");

    ControllerInputBatch alreadyReconciled =
        batch(3U, reconcile.finalState, reconcile.finalState);
    alreadyReconciled.edges[0U] = {3U,
        5U,
        ControllerDigitalControl::secondaryTrigger,
        false};
    alreadyReconciled.edgeCount = 1U;
    alreadyReconciled.finalState.secondaryTriggerPressed = false;
    require(accept(bridge, alreadyReconciled, output) == 1U,
        "an ordered edge was duplicated by final reconciliation");
}

void testGenerationChangeAndReset() {
    ControllerInputBatchBridge bridge;
    ControllerSample first{};
    first.pausePressed = true;
    OutputBuffer output{};
    (void)accept(bridge, batch(10U, first, first), output);

    ControllerSample replacement{};
    replacement.bank = 9000;
    replacement.secondaryTriggerPressed = true;
    const auto replacementCount =
        accept(bridge, batch(11U, replacement, replacement), output);
    require(replacementCount == 18U,
        "new generation was not emitted as full state");
    require(bridge.currentGeneration() == 11U,
        "new generation did not replace current generation");

    bridge.reset();
    require(bridge.currentGeneration() == 0U,
        "reset did not clear current generation");
    require(accept(bridge, batch(11U, replacement, replacement), output) == 18U,
        "reset did not require a fresh full state");
}

void requireRejectedAtomically(ControllerInputBatchBridge& bridge,
    const ControllerInputBatch& input,
    const ControllerBatchStatus expected,
    const std::string& context) {
    OutputBuffer output{};
    fillSentinel(output);
    const auto generation = bridge.currentGeneration();
    const auto result = bridge.process(input, output);
    require(result.status == expected, context + " returned wrong status");
    require(result.emissionCount == 0U, context + " reported output");
    require(bridge.currentGeneration() == generation,
        context + " changed generation");
    requireUntouched(output, context);
}

void testMalformedBatchesAreAtomic() {
    ControllerInputBatchBridge bridge;
    establishNeutral(bridge, 9U);
    const ControllerInputBatch valid = batch(9U, {}, {});

    auto malformed = valid;
    malformed.generation = 0U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidGeneration,
        "zero generation");

    malformed = valid;
    malformed.generation = 8U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidGeneration,
        "generation rollback");

    malformed = valid;
    malformed.overflowed = true;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::inputOverflow,
        "producer overflow");

    malformed = valid;
    malformed.edgeCount = ControllerInputBatch::edgeCapacity + 1U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::inputCapacityExceeded,
        "edge capacity");

    malformed = valid;
    malformed.startingState.pausePressed = true;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::startingStateMismatch,
        "starting state");

    malformed = valid;
    malformed.startingState.bank = static_cast<std::int16_t>(-32768);
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidAxis,
        "starting Q15");

    malformed = valid;
    malformed.finalState.lookY = static_cast<std::int16_t>(-32768);
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidAxis,
        "final Q15");

    malformed = valid;
    malformed.edges[0U] = {8U, 1U, ControllerDigitalControl::pause, true};
    malformed.edgeCount = 1U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidEdgeGeneration,
        "edge generation");

    malformed = valid;
    malformed.edges[0U] = {9U, 0U, ControllerDigitalControl::pause, true};
    malformed.edgeCount = 1U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidEdgeOrder,
        "zero edge order");

    malformed = valid;
    malformed.edges[0U] = {9U, 2U, ControllerDigitalControl::pause, true};
    malformed.edges[1U] = {9U, 2U, ControllerDigitalControl::pause, false};
    malformed.edgeCount = 2U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidEdgeOrder,
        "non-increasing edge order");

    malformed = valid;
    malformed.edges[0U] = {
        9U,
        1U,
        static_cast<ControllerDigitalControl>(controllerDigitalControlCount),
        true,
    };
    malformed.edgeCount = 1U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidEdgeControl,
        "edge control range");

    malformed = valid;
    malformed.edges[0U] = {9U, 1U, ControllerDigitalControl::pause, false};
    malformed.edgeCount = 1U;
    requireRejectedAtomically(bridge,
        malformed,
        ControllerBatchStatus::invalidEdgeTransition,
        "duplicate edge state");

    OutputBuffer output{};
    require(accept(bridge, valid, output) == 0U,
        "malformed rejection changed accepted sample");

    auto oneAcceptedEdge = valid;
    oneAcceptedEdge.edges[0U] = {9U,
        10U,
        ControllerDigitalControl::pause,
        true};
    oneAcceptedEdge.edgeCount = 1U;
    oneAcceptedEdge.finalState.pausePressed = true;
    require(accept(bridge, oneAcceptedEdge, output) == 1U,
        "valid ordered edge was rejected");

    auto staleAcrossBatches =
        batch(9U, oneAcceptedEdge.finalState, oneAcceptedEdge.finalState);
    staleAcrossBatches.edges[0U] = {9U,
        10U,
        ControllerDigitalControl::pause,
        false};
    staleAcrossBatches.edgeCount = 1U;
    staleAcrossBatches.finalState.pausePressed = false;
    requireRejectedAtomically(bridge,
        staleAcrossBatches,
        ControllerBatchStatus::invalidEdgeOrder,
        "cross-batch edge order");
}

void testOutputCapacityAndMaximumBound() {
    ControllerInputBatchBridge smallBridge;
    ControllerInputBatch neutral = batch(1U, {}, {});
    OutputBuffer output{};
    fillSentinel(output);
    const auto tooSmall =
        smallBridge.process(neutral, std::span{output}.first(15U));
    require(tooSmall.status == ControllerBatchStatus::outputCapacityExceeded,
        "small full-state output returned wrong status");
    require(smallBridge.currentGeneration() == 0U,
        "small output capacity committed bridge state");
    requireUntouched(output, "small full-state output");
    require(accept(smallBridge, neutral, output) == 18U,
        "retry after small output was not a full state");

    ControllerInputBatch maximum = batch(2U, {}, {});
    ControllerSample edgeState{};
    std::uint64_t order = 1U;
    for (std::size_t index = 0U; index < ControllerInputBatch::edgeCapacity;
        ++index) {
        const auto control =
            digitalControl(index % controllerDigitalControlCount);
        const bool pressed = !edgeState.pressed(control);
        maximum.edges[index] = {2U, order++, control, pressed};
        edgeState.setPressed(control, pressed);
    }
    maximum.edgeCount = ControllerInputBatch::edgeCapacity;
    maximum.finalState = edgeState;
    for (std::size_t index = 0U; index < controllerDigitalControlCount;
        ++index) {
        const auto control = digitalControl(index);
        maximum.finalState.setPressed(control,
            !maximum.finalState.pressed(control));
    }

    ControllerInputBatchBridge maximumBridge;
    fillSentinel(output);
    const auto bounded = maximumBridge.process(maximum,
        std::span{output}.first(
            ControllerInputBatchBridge::maximumEmissionCount - 1U));
    require(bounded.status == ControllerBatchStatus::outputCapacityExceeded,
        "maximum-minus-one capacity returned wrong status");
    require(maximumBridge.currentGeneration() == 0U,
        "maximum-minus-one capacity committed state");
    requireUntouched(output, "maximum-minus-one capacity");

    const auto exact = maximumBridge.process(maximum, output);
    require(exact.accepted(), "exact maximum capacity was rejected");
    require(
        exact.emissionCount == ControllerInputBatchBridge::maximumEmissionCount,
        "maximum emission bound was not tight");
}

void testDeterministicReplay() {
    ControllerInputBatch first = batch(41U, {}, {});
    first.finalState.bank = 7000;
    first.finalState.lookY = -8000;
    first.edges[0U] = {41U, 3U, ControllerDigitalControl::uiConfirm, true};
    first.edges[1U] = {41U, 4U, ControllerDigitalControl::uiConfirm, false};
    first.edgeCount = 2U;

    ControllerInputBatch second =
        batch(41U, first.finalState, first.finalState);
    second.finalState.bank = 8024;
    second.finalState.cameraCyclePressed = true;

    ControllerInputBatchBridge left;
    ControllerInputBatchBridge right;
    OutputBuffer leftOutput{};
    OutputBuffer rightOutput{};

    const auto leftFirst = left.process(first, leftOutput);
    const auto rightFirst = right.process(first, rightOutput);
    require(leftFirst == rightFirst, "first deterministic result differed");
    for (std::size_t index = 0U; index < leftFirst.emissionCount; ++index) {
        require(leftOutput[index] == rightOutput[index],
            "first deterministic emissions differed");
    }

    const auto leftSecond = left.process(second, leftOutput);
    const auto rightSecond = right.process(second, rightOutput);
    require(leftSecond == rightSecond, "second deterministic result differed");
    for (std::size_t index = 0U; index < leftSecond.emissionCount; ++index) {
        require(leftOutput[index] == rightOutput[index],
            "second deterministic emissions differed");
    }
    require(left.currentGeneration() == right.currentGeneration(),
        "deterministic bridge states diverged");
}

static_assert(noexcept(std::declval<ControllerInputBatchBridge&>().process(
    std::declval<const ControllerInputBatch&>(),
    std::declval<std::span<ControllerInputEmission>>())));
static_assert(noexcept(std::declval<ControllerInputBatchBridge&>().reset()));

} // namespace

int main() {
    try {
        testAllDigitalMappings();
        testNeutralFullState();
        testNonNeutralFullStateAndDeadzone();
        testResolvedCalibrationRunsAfterTransportDeadzone();
        testConfiguredDefaultPreservesLegacyTransport();
        testConfiguredAxesEmitEveryChangedCalibratedValue();
        testMeaningfulAxisChanges();
        testCompleteTapsForEveryDigitalControl();
        testMixedOrderedEdgesAndFinalReconciliation();
        testGenerationChangeAndReset();
        testMalformedBatchesAreAtomic();
        testOutputCapacityAndMaximumBound();
        testDeterministicReplay();
    } catch (const std::exception& error) {
        std::cerr << "ControllerInputBatchBridgeTests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "ControllerInputBatchBridgeTests passed\n";
    return 0;
}
