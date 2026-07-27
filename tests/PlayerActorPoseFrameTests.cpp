#include "airfix/render/PlayerActorPoseFrame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace airfix::render;

template <typename Frame>
concept CanBorrowFrameView = requires(Frame&& frame) {
    std::forward<Frame>(frame).frameView();
};

static_assert(CanBorrowFrameView<PlayerActorPoseFrame&>);
static_assert(CanBorrowFrameView<const PlayerActorPoseFrame&>);
static_assert(!CanBorrowFrameView<PlayerActorPoseFrame>);
static_assert(!CanBorrowFrameView<const PlayerActorPoseFrame>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ConvertedNodeTransform transform(
    const Mat3& linear,
    const Vec3 translation,
    const float rawScalar = 1.0F) {
    return {
        .linear = linear,
        .translation = translation,
        .rawScalar = rawScalar,
    };
}

[[nodiscard]] ConvertedNodeTransform local0() {
    return transform(
        Mat3{{
            Vec3{1.0F, 1.0F, 0.0F},
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.5F},
        }},
        Vec3{2.0F, -3.0F, 4.0F},
        7.0F);
}

[[nodiscard]] ConvertedNodeTransform local1() {
    return transform(
        Mat3{{
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{-1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 2.0F},
        }},
        Vec3{-5.0F, 6.0F, 8.0F},
        9.0F);
}

[[nodiscard]] ConvertedNodeTransform actorWorld() {
    return transform(
        Mat3{{
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{-3.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.5F},
        }},
        Vec3{11.0F, -13.0F, 17.0F},
        3.0F);
}

[[nodiscard]] PlayerActorSceneBinding binding() {
    return {
        .firstMeshSlot = 3U,
        .meshCount = 2U,
        .firstInstanceIndex = 4U,
        .instanceCount = 2U,
    };
}

[[nodiscard]] PlayerActorSceneInstanceProvenance provenance(
    const std::uint32_t finalInstanceIndex,
    const ConvertedNodeTransform& actorLocal,
    const std::size_t marker) {
    return {
        .actor = {
            .legacySkinSlot = 0U,
            .blueprintIndex = marker,
            .blueprintReference =
                static_cast<std::uint32_t>(100U + marker),
            .physicalMeshIndex = marker,
        },
        .finalInstanceIndex = finalInstanceIndex,
        .actorLocal = actorLocal,
    };
}

[[nodiscard]] std::vector<PlayerActorSceneInstanceProvenance>
validProvenance() {
    return {
        provenance(4U, local0(), 0U),
        provenance(5U, local1(), 1U),
    };
}

[[nodiscard]] DynamicInstancePoseLimits exactLimits() {
    return {
        .maximumInstances = 6U,
        .maximumOverrides = 2U,
        .maximumFrameBytes =
            2U * sizeof(DynamicInstancePoseOverride),
    };
}

[[nodiscard]] bool hasIssue(
    const PlayerActorPoseFrame& result,
    const PlayerActorPoseFrameIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

const PlayerActorPoseFrameIssue& onlyIssue(
    const PlayerActorPoseFrame& result,
    const PlayerActorPoseFrameIssueKind kind,
    const std::string& message) {
    require(
        result.overrides.empty() && result.issues.size() == 1U &&
            result.issues.front().kind == kind &&
            result.frameView().overrides.empty(),
        message);
    return result.issues.front();
}

void testExactFrameOwnsOrderedOverridesAndView() {
    const auto world = actorWorld();
    const auto locals = validProvenance();
    const auto result = buildPlayerActorPoseFrame(
        binding(), locals, world, 42U, exactLimits());

    require(result.complete(), "valid exact-limit frame was rejected");
    require(
        result.simulationStep == 42U && result.overrides.size() == 2U,
        "frame identity/count was not preserved");
    require(
        result.overrides[0].instanceIndex == 4U &&
            result.overrides[1].instanceIndex == 5U,
        "overrides did not preserve the strict final-index order");

    const auto expected0 =
        composeNodeTransforms(world, locals[0].actorLocal);
    const auto expected1 =
        composeNodeTransforms(world, locals[1].actorLocal);
    const auto reversed =
        composeNodeTransforms(locals[0].actorLocal, world);
    const auto doubled =
        composeNodeTransforms(world, expected0);
    require(
        result.overrides[0].modelLinear == expected0.linear &&
            result.overrides[0].modelTranslation ==
                expected0.translation &&
            result.overrides[1].modelLinear == expected1.linear &&
            result.overrides[1].modelTranslation ==
                expected1.translation,
        "actor world/local transforms were not composed exactly once");
    require(
        result.overrides[0].modelLinear != reversed.linear &&
            result.overrides[0].modelTranslation !=
                reversed.translation &&
            (result.overrides[0].modelLinear != doubled.linear ||
             result.overrides[0].modelTranslation !=
                doubled.translation),
        "non-commutative operands did not distinguish reverse/double composition");

    const auto view = result.frameView();
    require(
        view.simulationStep == result.simulationStep &&
            view.overrides.data() == result.overrides.data() &&
            view.overrides.size() == result.overrides.size(),
        "frame view did not borrow the owning payload");

    ScenePoseExchange exchange(exactLimits());
    auto scene = exchange.tryBeginScene(6U);
    require(scene.has_value(), "pose exchange rejected the exact scene");
    require(
        exchange.tryPublish(*scene, result.frameView()) ==
            DynamicInstancePosePublishResult::published,
        "owning actor frame view was not publishable");
    const auto lease = exchange.tryAcquire(*scene);
    require(
        lease.has_value() && lease->simulationStep() == 42U &&
            lease->overrides().size() == 2U,
        "published actor frame was not observable by the consumer");

    const auto resolvedActor = lease->resolve(
        4U, Mat3{}, Vec3{99.0F, 98.0F, 97.0F});
    require(
        resolvedActor.modelLinear == expected0.linear &&
            resolvedActor.modelTranslation == expected0.translation,
        "actor instance did not resolve to its dynamic override");
    const Mat3 staticLinear{{
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 3.0F, 0.0F},
        Vec3{0.0F, 0.0F, 4.0F},
    }};
    const Vec3 staticTranslation{-21.0F, 22.0F, -23.0F};
    const auto resolvedStatic =
        lease->resolve(1U, staticLinear, staticTranslation);
    require(
        resolvedStatic.modelLinear == staticLinear &&
            resolvedStatic.modelTranslation == staticTranslation,
        "static instance did not preserve its authored fallback");
}

void testMissingEmptyAndMismatchedActorFailAtomically() {
    const auto locals = validProvenance();
    onlyIssue(
        buildPlayerActorPoseFrame(
            std::nullopt,
            locals,
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::missingActorBinding,
        "missing binding was not rejected atomically");

    auto empty = binding();
    empty.instanceCount = 0U;
    onlyIssue(
        buildPlayerActorPoseFrame(
            empty,
            {},
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::emptyActorBinding,
        "empty actor was not an explicit failure");

    auto mismatched = binding();
    mismatched.instanceCount = 1U;
    onlyIssue(
        buildPlayerActorPoseFrame(
            mismatched,
            locals,
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::bindingInstanceCountMismatch,
        "binding/provenance size mismatch was accepted");
}

void testBindingRangesAndRepresentabilityFailClosed() {
    auto locals = validProvenance();

    auto meshOverflow = binding();
    meshOverflow.firstMeshSlot =
        std::numeric_limits<std::size_t>::max();
    onlyIssue(
        buildPlayerActorPoseFrame(
            meshOverflow,
            locals,
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::meshBindingRangeOverflow,
        "overflowing mesh binding range was accepted");

    auto instanceOverflow = binding();
    instanceOverflow.firstInstanceIndex =
        std::numeric_limits<std::size_t>::max();
    onlyIssue(
        buildPlayerActorPoseFrame(
            instanceOverflow,
            locals,
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::instanceBindingRangeOverflow,
        "overflowing instance binding range was accepted");

    if constexpr (
        std::numeric_limits<std::size_t>::max() >
        std::numeric_limits<std::uint32_t>::max()) {
        auto unrepresentable = binding();
        unrepresentable.firstInstanceIndex =
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()) +
            1U;
        onlyIssue(
            buildPlayerActorPoseFrame(
                unrepresentable,
                locals,
                actorWorld(),
                1U,
                exactLimits()),
            PlayerActorPoseFrameIssueKind::
                instanceIndexNotRepresentable,
            "non-uint32 instance range was accepted");
    }
}

void testCountByteAndInstanceLimitsAreExact() {
    const auto locals = validProvenance();
    require(
        buildPlayerActorPoseFrame(
            binding(), locals, actorWorld(), 7U, exactLimits())
            .complete(),
        "exact limits were rejected");

    auto oneBelow = exactLimits();
    --oneBelow.maximumOverrides;
    onlyIssue(
        buildPlayerActorPoseFrame(
            binding(), locals, actorWorld(), 7U, oneBelow),
        PlayerActorPoseFrameIssueKind::overrideLimitExceeded,
        "N-1 override limit published partial output");

    oneBelow = exactLimits();
    --oneBelow.maximumFrameBytes;
    onlyIssue(
        buildPlayerActorPoseFrame(
            binding(), locals, actorWorld(), 7U, oneBelow),
        PlayerActorPoseFrameIssueKind::frameByteLimitExceeded,
        "N-1 byte limit published partial output");

    oneBelow = exactLimits();
    --oneBelow.maximumInstances;
    onlyIssue(
        buildPlayerActorPoseFrame(
            binding(), locals, actorWorld(), 7U, oneBelow),
        PlayerActorPoseFrameIssueKind::instanceCeilingExceeded,
        "N-1 instance ceiling published partial output");
}

void testGapsOrderDuplicatesAndOutOfRangeAreTyped() {
    const auto assertMismatch =
        [](std::vector<PlayerActorSceneInstanceProvenance> values,
           const std::size_t issueIndex,
           const std::size_t expected,
           const std::uint32_t actual,
           const std::string& label) {
            const auto result = buildPlayerActorPoseFrame(
                binding(),
                values,
                actorWorld(),
                1U,
                exactLimits());
            const auto& issue = onlyIssue(
                result,
                PlayerActorPoseFrameIssueKind::
                    provenanceInstanceIndexMismatch,
                label + " was not rejected atomically");
            require(
                issue.actorInstanceIndex == issueIndex &&
                    issue.expectedFinalInstanceIndex == expected &&
                    issue.actualFinalInstanceIndex == actual,
                label + " did not retain typed index diagnostics");
        };

    auto values = validProvenance();
    values[1].finalInstanceIndex = 6U;
    assertMismatch(values, 1U, 5U, 6U, "gap");

    values = validProvenance();
    values[0].finalInstanceIndex = 5U;
    values[1].finalInstanceIndex = 4U;
    assertMismatch(values, 0U, 4U, 5U, "out-of-order pair");

    values = validProvenance();
    values[1].finalInstanceIndex = 4U;
    assertMismatch(values, 1U, 5U, 4U, "duplicate");

    values = validProvenance();
    values[1].finalInstanceIndex =
        std::numeric_limits<std::uint32_t>::max();
    assertMismatch(
        values,
        1U,
        5U,
        std::numeric_limits<std::uint32_t>::max(),
        "out-of-range provenance");
}

void testNonFiniteAndSingularInputsAreTyped() {
    const auto assertTransformIssue =
        [](const PlayerActorPoseFrame& result,
           const PlayerActorPoseFrameIssueKind kind,
           const GeometryErrorCode geometry,
           const std::string& label) {
            const auto& issue = onlyIssue(result, kind, label);
            require(
                issue.geometryError == geometry,
                label + " lost its geometry error");
        };

    auto invalidWorld = actorWorld();
    invalidWorld.translation.x =
        std::numeric_limits<float>::infinity();
    assertTransformIssue(
        buildPlayerActorPoseFrame(
            binding(),
            validProvenance(),
            invalidWorld,
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::invalidActorWorldTransform,
        GeometryErrorCode::nonFiniteValue,
        "non-finite actor world");

    invalidWorld = actorWorld();
    invalidWorld.linear.columns[0] = {0.0F, 0.0F, 0.0F};
    assertTransformIssue(
        buildPlayerActorPoseFrame(
            binding(),
            validProvenance(),
            invalidWorld,
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::invalidActorWorldTransform,
        GeometryErrorCode::singularTransform,
        "singular actor world");

    auto invalidLocals = validProvenance();
    invalidLocals[1].actorLocal.rawScalar =
        std::numeric_limits<float>::quiet_NaN();
    const auto nonFiniteLocal = buildPlayerActorPoseFrame(
        binding(),
        invalidLocals,
        actorWorld(),
        1U,
        exactLimits());
    assertTransformIssue(
        nonFiniteLocal,
        PlayerActorPoseFrameIssueKind::invalidActorLocalTransform,
        GeometryErrorCode::nonFiniteValue,
        "non-finite actor local");
    require(
        nonFiniteLocal.issues.front().actorInstanceIndex == 1U,
        "invalid local did not identify its provenance index");

    invalidLocals = validProvenance();
    invalidLocals[0].actorLocal.linear.columns[0] =
        {0.0F, 0.0F, 0.0F};
    assertTransformIssue(
        buildPlayerActorPoseFrame(
            binding(),
            invalidLocals,
            actorWorld(),
            1U,
            exactLimits()),
        PlayerActorPoseFrameIssueKind::invalidActorLocalTransform,
        GeometryErrorCode::singularTransform,
        "singular actor local");
}

void testFiniteOperandsWithNonFiniteCompositionFailAtomically() {
    const Mat3 extreme{{
        Vec3{1.0e20F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0e-10F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0e-10F},
    }};
    const auto extremeWorld =
        transform(extreme, Vec3{0.0F, 0.0F, 0.0F});
    auto locals = validProvenance();
    locals[0].actorLocal =
        transform(extreme, Vec3{0.0F, 0.0F, 0.0F});

    require(
        std::isfinite(determinant(extreme)) &&
            determinant(extreme) != 0.0F,
        "test extreme transform is not independently valid");
    const auto result = buildPlayerActorPoseFrame(
        binding(), locals, extremeWorld, 1U, exactLimits());
    const auto& issue = onlyIssue(
        result,
        PlayerActorPoseFrameIssueKind::invalidComposedTransform,
        "non-finite composition published a partial frame");
    require(
        issue.actorInstanceIndex == 0U &&
            issue.geometryError == GeometryErrorCode::nonFiniteValue,
        "composition failure lost its typed provenance/geometry details");
}

void testPublicContractTypesRemainPortable() {
    static_assert(
        std::is_same_v<
            decltype(PlayerActorPoseFrame{}.simulationStep),
            std::uint64_t>);
    static_assert(
        std::is_same_v<
            decltype(DynamicInstancePoseOverride{}.instanceIndex),
            std::uint32_t>);
    static_assert(
        PlayerActorPoseFrameIssueKind::frameByteSizeOverflow !=
            PlayerActorPoseFrameIssueKind::frameByteLimitExceeded);
    require(
        !hasIssue(
            buildPlayerActorPoseFrame(
                binding(),
                validProvenance(),
                actorWorld(),
                1U,
                exactLimits()),
            PlayerActorPoseFrameIssueKind::frameByteSizeOverflow),
        "valid byte accounting reported overflow");
}

} // namespace

int main() {
    try {
        testExactFrameOwnsOrderedOverridesAndView();
        testMissingEmptyAndMismatchedActorFailAtomically();
        testBindingRangesAndRepresentabilityFailClosed();
        testCountByteAndInstanceLimitsAreExact();
        testGapsOrderDuplicatesAndOutOfRangeAreTyped();
        testNonFiniteAndSingularInputsAreTyped();
        testFiniteOperandsWithNonFiniteCompositionFailAtomically();
        testPublicContractTypesRemainPortable();
        std::cout << "PlayerActorPoseFrame tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PlayerActorPoseFrame tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
