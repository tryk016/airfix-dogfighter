#include "airfix/runtime/PlayerAircraftPresentationCoordinator.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace airfix;

using runtime::PlayerAircraftPresentationCoordinator;
using runtime::PlayerAircraftPresentationStepStatus;
using render::ConvertedNodeTransform;
using render::DynamicInstancePoseLease;
using render::DynamicInstancePoseLimits;
using render::DynamicInstancePoseOverride;
using render::GeometryErrorCode;
using render::PlayerActorPoseRuntime;
using render::PlayerActorPoseRuntimePublishResult;
using render::PlayerActorSceneBinding;
using render::PlayerActorSceneInstanceProvenance;
using render::PlayerActorVisualProvenance;
using render::Vec3;

static_assert(noexcept(PlayerAircraftPresentationCoordinator{}));
static_assert(noexcept(
    std::declval<const PlayerAircraftPresentationCoordinator&>()
        .state()));
static_assert(noexcept(
    std::declval<const PlayerAircraftPresentationCoordinator&>()
        .stateHash()));
static_assert(noexcept(
    std::declval<const PlayerAircraftPresentationCoordinator&>()
        .healthy()));
static_assert(noexcept(
    std::declval<PlayerAircraftPresentationCoordinator&>()
        .tryAdvance(
            std::declval<const input::InputFrame&>(),
            std::declval<const ConvertedNodeTransform&>())));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ConvertedNodeTransform world(
    const Vec3 translation = {}) {
    return {
        .linear = {},
        .translation = translation,
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] input::InputFrame inputFrame(
    const std::uint64_t tick,
    const input::Q15 bank = input::q15Zero) {
    input::InputFrame frame{
        .simulationTick = tick,
    };
    frame.analogValues[input::toIndex(
        input::AnalogAxis::flightBank)] = bank;
    return frame;
}

void pressAndHold(
    input::InputFrame& frame,
    const input::DigitalAction action) {
    const auto index = input::toIndex(action);
    const auto bit =
        std::uint64_t{1U} << static_cast<unsigned>(index % 64U);
    frame.pressedBits[index / 64U] |= bit;
    frame.heldBits[index / 64U] |= bit;
}

[[nodiscard]] PlayerActorSceneInstanceProvenance provenance() {
    return {
        .actor =
            PlayerActorVisualProvenance{
                .legacySkinSlot = 0U,
                .blueprintIndex = 0U,
                .blueprintReference = 1U,
                .physicalMeshIndex = 0U,
            },
        .finalInstanceIndex = 0U,
        .actorLocal = world(),
    };
}

[[nodiscard]] std::shared_ptr<PlayerActorPoseRuntime>
buildPoseRuntime(const std::uint64_t initialStep = 0U) {
    const PlayerActorSceneBinding binding{
        .firstMeshSlot = 0U,
        .meshCount = 1U,
        .firstInstanceIndex = 0U,
        .instanceCount = 1U,
    };
    const std::vector<PlayerActorSceneInstanceProvenance> values{
        provenance(),
    };
    const DynamicInstancePoseLimits limits{
        .maximumInstances = 1U,
        .maximumOverrides = 1U,
        .maximumFrameBytes =
            sizeof(DynamicInstancePoseOverride),
    };
    auto built = PlayerActorPoseRuntime::create(
        binding, values, 1U, world(), initialStep, limits);
    require(built.complete(), "pose runtime fixture failed");
    return std::shared_ptr<PlayerActorPoseRuntime>{
        std::move(built.runtime)};
}

[[nodiscard]] DynamicInstancePoseLease acquire(
    PlayerActorPoseRuntime& poseRuntime,
    const std::string& message) {
    auto lease = poseRuntime.tryAcquire();
    require(lease.has_value(), message);
    return std::move(*lease);
}

void testHeadlessAdvanceCommitsStateAndHash() {
    PlayerAircraftPresentationCoordinator coordinator;
    const simulation::PlayerAircraftState initial{};
    require(
        coordinator.healthy() &&
            coordinator.state() == initial &&
            coordinator.stateHash() ==
                simulation::canonicalHash(initial),
        "initial state surface mismatch");

    auto frame = inputFrame(41U, 12'345);
    pressAndHold(
        frame, input::DigitalAction::combatPrimaryFire);
    const auto expected = simulation::advance(initial, frame);
    require(expected.accepted(), "test frame was invalid");

    const auto result = coordinator.tryAdvance(
        frame, world({3.0F, 5.0F, 7.0F}));
    require(
        result.status ==
                PlayerAircraftPresentationStepStatus::
                    advancedWithoutPoseEndpoint &&
            result.accepted() && !result.terminal() &&
            result.simulationError ==
                simulation::PlayerAircraftAdvanceError::none &&
            !result.poseOutcome.has_value(),
        "headless step returned the wrong result");
    require(
        coordinator.healthy() &&
            coordinator.state() == expected.state &&
            coordinator.stateHash() ==
                simulation::canonicalHash(expected.state) &&
            coordinator.state().primaryFireHeld &&
            coordinator.state().primaryFirePressCount == 1U,
        "headless step did not commit the exact simulation state");
}

void testPublishedPoseAndStateCommitTogether() {
    PlayerAircraftPresentationCoordinator coordinator;
    const auto poseRuntime = buildPoseRuntime();
    const auto actorWorld = world({11.0F, -13.0F, 17.0F});
    const auto frame = inputFrame(1U, -4'321);

    const auto result = coordinator.tryAdvance(
        frame,
        actorWorld,
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        result.status ==
                PlayerAircraftPresentationStepStatus::
                    advancedAndPosePublished &&
            result.accepted() &&
            result.poseOutcome.has_value() &&
            result.poseOutcome->result ==
                PlayerActorPoseRuntimePublishResult::published,
        "live endpoint was not reported as published");
    require(
        coordinator.healthy() &&
            coordinator.state().completedSteps == 1U &&
            coordinator.state().lastInputTick == 1U &&
            coordinator.state().bankIntentQ15 == -4'321,
        "published step did not commit simulation state");

    const auto lease = acquire(
        *poseRuntime, "published pose was unavailable");
    require(
        lease.simulationStep() == 1U &&
            lease.resolve(0U, {}, {}).modelTranslation ==
                actorWorld.translation,
        "published pose did not use the committed simulation step");
}

void testBusyPoseIsAHealthyCommittedStep() {
    PlayerAircraftPresentationCoordinator coordinator;
    const auto poseRuntime = buildPoseRuntime();
    std::optional<DynamicInstancePoseLease> initialLease{
        acquire(*poseRuntime, "initial pose was unavailable")};

    const auto first = coordinator.tryAdvance(
        inputFrame(1U),
        world({1.0F, 0.0F, 0.0F}),
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        first.status ==
                PlayerAircraftPresentationStepStatus::
                    advancedAndPosePublished,
        "first pose update failed");
    std::optional<DynamicInstancePoseLease> secondLease{
        acquire(*poseRuntime, "second pose slot was unavailable")};

    const auto busy = coordinator.tryAdvance(
        inputFrame(2U),
        world({2.0F, 0.0F, 0.0F}),
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        busy.status ==
                PlayerAircraftPresentationStepStatus::
                    advancedWhilePoseBusy &&
            busy.accepted() &&
            busy.poseOutcome.has_value() &&
            busy.poseOutcome->result ==
                PlayerActorPoseRuntimePublishResult::busy &&
            coordinator.healthy() &&
            coordinator.state().completedSteps == 2U &&
            coordinator.state().lastInputTick == 2U,
        "busy pose exchange was not treated as a committed step");

    initialLease.reset();
    secondLease.reset();
    const auto recovered = coordinator.tryAdvance(
        inputFrame(3U),
        world({3.0F, 0.0F, 0.0F}),
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        recovered.status ==
                PlayerAircraftPresentationStepStatus::
                    advancedAndPosePublished &&
            coordinator.state().completedSteps == 3U,
        "pose publication did not recover after busy");
    const auto latest = acquire(
        *poseRuntime, "recovered pose was unavailable");
    require(
        latest.simulationStep() == 3U,
        "busy recovery used a stale simulation step");
}

void testExpiredEndpointIsTerminalAndAtomic() {
    PlayerAircraftPresentationCoordinator coordinator;
    auto poseRuntime = buildPoseRuntime();
    const std::weak_ptr<PlayerActorPoseRuntime> expired{
        poseRuntime};
    poseRuntime.reset();
    const auto before = coordinator.state();
    const auto beforeHash = coordinator.stateHash();

    const auto rejected = coordinator.tryAdvance(
        inputFrame(1U),
        world(),
        runtime::PlayerActorPoseRuntimeEndpoint{expired});
    require(
        rejected.status ==
                PlayerAircraftPresentationStepStatus::
                    poseEndpointExpired &&
            rejected.terminal() && !rejected.accepted() &&
            !rejected.poseOutcome.has_value() &&
            !coordinator.healthy() &&
            coordinator.state() == before &&
            coordinator.stateHash() == beforeHash,
        "expired endpoint did not reject atomically");

    const auto later =
        coordinator.tryAdvance(inputFrame(2U), world());
    require(
        later.status ==
                PlayerAircraftPresentationStepStatus::unhealthy &&
            coordinator.state() == before,
        "terminal coordinator accepted a later headless step");
}

void testPoseFailureIsTerminalAndAtomic() {
    PlayerAircraftPresentationCoordinator coordinator;
    const auto poseRuntime = buildPoseRuntime();
    auto invalidWorld = world();
    invalidWorld.translation.x =
        std::numeric_limits<float>::infinity();
    const auto before = coordinator.state();
    const auto beforeHash = coordinator.stateHash();

    const auto rejected = coordinator.tryAdvance(
        inputFrame(1U),
        invalidWorld,
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        rejected.status ==
                PlayerAircraftPresentationStepStatus::
                    posePublicationRejected &&
            rejected.terminal() &&
            rejected.poseOutcome.has_value() &&
            rejected.poseOutcome->result ==
                PlayerActorPoseRuntimePublishResult::
                    invalidActorWorld &&
            rejected.poseOutcome->geometryError ==
                GeometryErrorCode::nonFiniteValue &&
            !coordinator.healthy() &&
            coordinator.state() == before &&
            coordinator.stateHash() == beforeHash,
        "non-busy pose failure did not reject atomically");

    const auto lease = acquire(
        *poseRuntime, "initial pose disappeared after rejection");
    require(
        lease.simulationStep() == 0U,
        "pose failure changed the last published frame");
}

void testSimulationFailureIsTerminalAndSkipsPose() {
    PlayerAircraftPresentationCoordinator coordinator;
    const auto poseRuntime = buildPoseRuntime();
    auto invalidFrame = inputFrame(1U);
    invalidFrame.schemaVersion =
        input::inputFrameSchemaVersion + 1U;
    const auto before = coordinator.state();

    const auto rejected = coordinator.tryAdvance(
        invalidFrame,
        world({9.0F, 8.0F, 7.0F}),
        runtime::PlayerActorPoseRuntimeEndpoint{poseRuntime});
    require(
        rejected.status ==
                PlayerAircraftPresentationStepStatus::
                    simulationRejected &&
            rejected.simulationError ==
                simulation::PlayerAircraftAdvanceError::
                    unsupportedInputSchema &&
            rejected.terminal() &&
            !rejected.poseOutcome.has_value() &&
            !coordinator.healthy() &&
            coordinator.state() == before,
        "simulation rejection did not preserve state and diagnostics");

    const auto lease = acquire(
        *poseRuntime, "initial pose unavailable after rejection");
    require(
        lease.simulationStep() == 0U,
        "rejected simulation step reached presentation");
}

} // namespace

int main() {
    try {
        testHeadlessAdvanceCommitsStateAndHash();
        testPublishedPoseAndStateCommitTogether();
        testBusyPoseIsAHealthyCommittedStep();
        testExpiredEndpointIsTerminalAndAtomic();
        testPoseFailureIsTerminalAndAtomic();
        testSimulationFailureIsTerminalAndSkipsPose();
        std::cout
            << "PlayerAircraftPresentationCoordinator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "PlayerAircraftPresentationCoordinator tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
