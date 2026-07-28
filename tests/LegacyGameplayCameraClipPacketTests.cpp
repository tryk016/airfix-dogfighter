#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

std::atomic<std::size_t> allocationCount{0U};
std::atomic<bool> countAllocations{false};

[[nodiscard]] void *allocate(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void *const memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

void *operator new(const std::size_t size) {
    return allocate(size);
}

void *operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *const memory) noexcept {
    std::free(memory);
}

void operator delete[](void *const memory) noexcept {
    std::free(memory);
}

void operator delete(void *const memory, const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void *const memory, const std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace airfix::render;

static_assert(noexcept(buildLegacyGameplayCameraClipPacket(
    std::declval<const LegacyGameplayCameraPoseSnapshot &>())));
static_assert(noexcept(buildLegacyGameplayCameraBootstrapClipPacket(
    std::declval<const LegacyGameplayCameraBootstrapInput &>())));
static_assert(noexcept(std::declval<const LegacyGameplayCameraClipPacket &>()
                           .project(std::declval<const Vec3 &>())));
static_assert(std::is_copy_constructible_v<LegacyGameplayCameraClipPacket>);
static_assert(!std::is_copy_assignable_v<LegacyGameplayCameraClipPacket>);

void require(const bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(const float actual,
                         const float expected,
                         const float tolerance = 2.0e-5F) noexcept {
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyGameplayCameraPoseSnapshot pose() {
    const LegacyGameplayCameraFrameSnapshot frame{
        .simulationStep = 42U,
        .publicationGeneration = 7U,
        .state =
            {
                .roomState =
                    {
                        .runtimeWorldPosition = {0.0F, 0.0F, -10.0F},
                        .worldRoomIndex = 3U,
                    },
                .axisFactors = {0.25F, 0.5F, 0.75F},
            },
    };
    const auto built =
        buildLegacyGameplayCameraPoseSnapshot(frame, {0.0F, 0.0F, 0.0F});
    require(built.complete(), "pose fixture failed");
    return *built.snapshot;
}

void testAcceptedPointMatchesScalarProjectionAfterDivide() {
    const auto sourcePose = pose();
    const auto built = buildLegacyGameplayCameraClipPacket(sourcePose);
    require(built.complete(), "default clip packet failed");

    const auto clip = built.packet->project({1.0F, 2.0F, 0.0F});
    const auto scalar = sourcePose.project({1.0F, 2.0F, 0.0F});
    require(clip.complete() && scalar.complete(),
            "accepted world point did not project");

    const auto &point = *clip.point;
    const float ndcX = point.x / point.w;
    const float ndcY = point.y / point.w;
    const float ndcDepth = point.z / point.w;
    const float expectedNdcX = 2.0F * scalar.point->projected.point.x /
                                   built.packet->logicalCanvasWidth() -
                               1.0F;
    const float expectedNdcY = 1.0F - 2.0F * scalar.point->projected.point.y /
                                          built.packet->logicalCanvasHeight();
    require(close(ndcX, expectedNdcX) && close(ndcY, expectedNdcY) &&
                close(ndcDepth, scalar.point->projected.legacyRhw) &&
                point.cameraSpacePosition ==
                    scalar.point->cameraSpacePosition &&
                point.withinRecoveredDepthRange,
            "homogeneous divide changed recovered projection or depth");
}

void testNearAndFarPlanesRemainExplicit() {
    const auto built = buildLegacyGameplayCameraClipPacket(pose());
    const auto nearPoint = built.packet->project({0.0F, 0.0F, -9.75F});
    const auto farPoint = built.packet->project({0.0F, 0.0F, 190.0F});
    const auto beforeNear = built.packet->project({0.0F, 0.0F, -9.875F});
    const auto beyondFar = built.packet->project({0.0F, 0.0F, 191.0F});

    require(nearPoint.complete() && nearPoint.point->w == 1.0F &&
                nearPoint.point->z == 1.0F &&
                nearPoint.point->withinRecoveredDepthRange &&
                farPoint.complete() && farPoint.point->w == 800.0F &&
                close(farPoint.point->z / farPoint.point->w, 0.00125F) &&
                farPoint.point->farClipDistance == 0.0F &&
                farPoint.point->withinRecoveredDepthRange &&
                beforeNear.complete() && beforeNear.point->w < 1.0F &&
                !beforeNear.point->withinRecoveredDepthRange &&
                beyondFar.complete() &&
                beyondFar.point->farClipDistance < 0.0F &&
                !beyondFar.point->withinRecoveredDepthRange,
            "near homogeneous plane or separate far clip changed");
}

void testOffsetCentreAndCanvasHeight() {
    const auto sourceFrame = pose().frame();
    LegacyGameplayCameraPoseConfig poseConfig;
    poseConfig.projection.windowWidth = 800.0F;
    poseConfig.projection.centre = {300.0F, 210.0F};
    const auto sourcePose = buildLegacyGameplayCameraPoseSnapshot(
        sourceFrame, {0.0F, 0.0F, 0.0F}, poseConfig);
    require(sourcePose.complete(), "offset pose fixture failed");
    const auto built = buildLegacyGameplayCameraClipPacket(
        *sourcePose.snapshot, {.logicalCanvasHeight = 600.0F});
    require(built.complete(), "offset clip packet failed");
    const auto clip = built.packet->project({1.0F, -1.0F, 0.0F});
    const auto scalar = sourcePose.snapshot->project({1.0F, -1.0F, 0.0F});

    require(clip.complete() && scalar.complete() &&
                close(clip.point->x / clip.point->w,
                      2.0F * scalar.point->projected.point.x / 800.0F - 1.0F) &&
                close(clip.point->y / clip.point->w,
                      1.0F - 2.0F * scalar.point->projected.point.y / 600.0F),
            "offset projection centre or logical height was ignored");
}

void testFailuresDoNotExposePartialResults() {
    const auto sourcePose = pose();
    const auto zero = buildLegacyGameplayCameraClipPacket(
        sourcePose, {.logicalCanvasHeight = 0.0F});
    const auto nan = buildLegacyGameplayCameraClipPacket(
        sourcePose,
        {
            .logicalCanvasHeight = std::numeric_limits<float>::quiet_NaN(),
        });
    const auto built = buildLegacyGameplayCameraClipPacket(sourcePose);
    const auto invalidWorld = built.packet->project({
        std::numeric_limits<float>::infinity(),
        0.0F,
        0.0F,
    });
    const auto overflow = built.packet->project({
        std::numeric_limits<float>::max(),
        0.0F,
        0.0F,
    });

    require(!zero.complete() && !zero.packet.has_value() &&
                zero.issue->kind == LegacyGameplayCameraClipBuildIssueKind::
                                        invalidLogicalCanvasHeight &&
                !nan.complete() && !nan.packet.has_value() &&
                !invalidWorld.complete() && !invalidWorld.point.has_value() &&
                invalidWorld.issue->kind ==
                    LegacyGameplayCameraClipIssueKind::transformFailed &&
                invalidWorld.issue->transformIssue.has_value() &&
                !overflow.complete() && !overflow.point.has_value() &&
                overflow.issue->kind ==
                    LegacyGameplayCameraClipIssueKind::nonFiniteDerivedValue &&
                !overflow.issue->transformIssue.has_value(),
            "invalid clip build or projection exposed a partial result");
}

void testBootstrapOwnsTheExplicitCamera0StartPolicy() {
    const auto built = buildLegacyGameplayCameraBootstrapClipPacket({
        .vehicleWorldPosition = {10.0F, 20.0F, 30.0F},
        .vehicleWorldRotation =
            {
                .columns =
                    {
                        Vec3{1.0F, 0.0F, 0.0F},
                        Vec3{0.0F, 1.0F, 0.0F},
                        Vec3{0.0F, 0.0F, 1.0F},
                    },
            },
        .worldRoomIndex = 17U,
    });
    require(built.complete(), "camera0 bootstrap failed");

    const auto &packet = *built.packet;
    const auto &frame = packet.pose().frame();
    require(frame.simulationStep == 0U && frame.publicationGeneration == 1U &&
                frame.state.roomState.runtimeWorldPosition ==
                    Vec3{10.0F, 20.1F, 29.25F} &&
                frame.state.roomState.worldRoomIndex == 17U &&
                frame.state.axisFactors == Vec3{1.0F, 1.0F, 1.0F} &&
                packet.pose().vehicleWorldAnchor() == Vec3{10.0F, 20.0F, 30.0F},
            "bootstrap changed the explicit camera0 target or provenance");

    const auto anchor = packet.project(packet.pose().vehicleWorldAnchor());
    require(anchor.complete() && anchor.point->withinRecoveredDepthRange,
            "bootstrap did not produce a usable gameplay view");
}

void testBootstrapRejectsInvalidActorTransforms() {
    const auto nonFinitePosition =
        buildLegacyGameplayCameraBootstrapClipPacket({
            .vehicleWorldPosition =
                {
                    std::numeric_limits<float>::infinity(),
                    0.0F,
                    0.0F,
                },
            .vehicleWorldRotation =
                {
                    .columns =
                        {
                            Vec3{1.0F, 0.0F, 0.0F},
                            Vec3{0.0F, 1.0F, 0.0F},
                            Vec3{0.0F, 0.0F, 1.0F},
                        },
                },
        });
    require(!nonFinitePosition.complete() &&
                !nonFinitePosition.packet.has_value() &&
                nonFinitePosition.issue->kind ==
                    LegacyGameplayCameraBootstrapIssueKind::chaseFailed &&
                nonFinitePosition.issue->chaseIssue.has_value() &&
                nonFinitePosition.issue->chaseIssue->kind ==
                    LegacyGameplayCameraChaseIssueKind::nonFiniteInput &&
                !nonFinitePosition.issue->poseIssue.has_value() &&
                !nonFinitePosition.issue->clipPacketIssue.has_value(),
            "invalid bootstrap actor transform exposed a partial camera");
}

void testBuildAndProjectionDoNotAllocate() {
    const auto sourcePose = pose();
    bool buildComplete = false;
    bool projectionComplete = false;

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U; iteration < 4'096U; ++iteration) {
        const auto built = buildLegacyGameplayCameraClipPacket(sourcePose);
        const auto projected = built.packet->project({1.0F, 2.0F, 3.0F});
        buildComplete = built.complete();
        projectionComplete = projected.complete();
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(buildComplete && projectionComplete,
            "clip allocation probe failed");
    require(allocationCount.load(std::memory_order_relaxed) == 0U,
            "clip packet build or projection allocated");
}

} // namespace

int main() {
    try {
        testAcceptedPointMatchesScalarProjectionAfterDivide();
        testNearAndFarPlanesRemainExplicit();
        testOffsetCentreAndCanvasHeight();
        testFailuresDoNotExposePartialResults();
        testBootstrapOwnsTheExplicitCamera0StartPolicy();
        testBootstrapRejectsInvalidActorTransforms();
        testBuildAndProjectionDoNotAllocate();
        std::cout << "LegacyGameplayCameraClipPacket tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraClipPacket tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
