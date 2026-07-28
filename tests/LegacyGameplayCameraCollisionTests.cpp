#include "airfix/render/LegacyCameraTransform.hpp"
#include "airfix/render/LegacyGameplayCameraCollision.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace airfix::render;

static_assert(noexcept(legacyGameplayCameraCollisionSphereRadius(
    std::declval<float>())));
static_assert(noexcept(
    legacyGameplayCameraReduceCollisionAxisFactors(
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(
    legacyAircraftRecoverGameplayCameraAxisFactors(
        std::declval<const Vec3&>(),
        std::declval<float>(),
        std::declval<float>(),
        std::declval<bool>())));
static_assert(noexcept(
    legacyGameplayCameraConstraintAcceptsPlane(
        std::declval<std::span<const Vec3>>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(
    legacyGameplayCameraConstraintPlaneOverrides(
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(
    legacyGameplayCameraAttemptConstrainedMove(
        std::declval<const Vec3&>(),
        std::declval<std::span<const Vec3>>())));
static_assert(noexcept(
    legacyGameplayCameraSphereTriangleCandidate(
        std::declval<const Vec3&>(),
        std::declval<float>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(
    legacyGameplayCameraSphereTriangleContact(
        std::declval<const Vec3&>(),
        std::declval<float>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(legacyGameplayCameraLineHitPoint(
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>(),
    std::declval<float>())));
static_assert(noexcept(legacyGameplayCameraLookAt(
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>())));
static_assert(std::is_copy_constructible_v<
              LegacyGameplayCameraLookAt>);

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-6F) noexcept {
    return std::abs(actual - expected) <= tolerance;
}

void requireVec(
    const Vec3 actual,
    const Vec3 expected,
    const char* const message,
    const float tolerance = 1.0e-6F) {
    require(
        close(actual.x, expected.x, tolerance) &&
            close(actual.y, expected.y, tolerance) &&
            close(actual.z, expected.z, tolerance),
        message);
}

void testRecoveredConstantsAndSphereRadius() {
    require(
        legacyGameplayCameraCollisionSphereRadius(1.0F)
                .has_value() &&
            std::bit_cast<std::uint32_t>(
                *legacyGameplayCameraCollisionSphereRadius(1.0F)) ==
                0x3F8CCCCDU,
        "collision-radius multiplier bits changed");
    require(
        std::bit_cast<std::uint32_t>(
            legacyGameplayCameraSpherePortalTraceArgument) ==
            0x3E4CCCCDU,
        "sphere portal trace argument bits changed");
    require(
        std::bit_cast<std::uint32_t>(
            legacyGameplayCameraLinePortalTraceArgument) ==
            0x3DCCCCCDU,
        "line portal trace argument bits changed");
    require(
        std::bit_cast<std::uint64_t>(
            legacyGameplayCameraConstraintDuplicateDotThreshold) ==
            0x3FEFF7CED916872BULL &&
            std::bit_cast<std::uint64_t>(
                legacyGameplayCameraConstraintCrossLengthSquaredThreshold) ==
                0x3F1A36E2EB1C432DULL,
        "constraint threshold bits changed");

    const auto radius =
        legacyGameplayCameraCollisionSphereRadius(0.25F);
    require(
        radius.has_value() && close(*radius, 0.275F),
        "default near plane produced the wrong sphere radius");

    require(
        !legacyGameplayCameraCollisionSphereRadius(0.0F)
             .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(-1.0F)
                 .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(
                 std::numeric_limits<float>::infinity())
                 .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(
                 std::numeric_limits<float>::max())
                 .has_value(),
        "invalid or overflowing near plane was accepted");
}

void testCollisionFactorReduction() {
    const auto reduced =
        legacyGameplayCameraReduceCollisionAxisFactors(
            Vec3{1.5F, 0.5F, -0.25F},
            Vec3{10.0F, 20.0F, 30.0F},
            Vec3{10.25F, 19.9F, 31.0F});
    require(reduced.has_value(), "valid factor reduction failed");
    requireVec(
        *reduced,
        Vec3{1.0F, 0.3F, 0.0F},
        "factor correction or lower clamp changed");

    const auto noUpperClamp =
        legacyGameplayCameraReduceCollisionAxisFactors(
            Vec3{2.0F, 1.0F, 1.0F},
            Vec3{},
            Vec3{0.1F, 0.0F, 0.0F});
    require(
        noUpperClamp.has_value() &&
            close(noUpperClamp->x, 1.8F),
        "collision reduction incorrectly added an upper clamp");

    require(
        !legacyGameplayCameraReduceCollisionAxisFactors(
             Vec3{std::numeric_limits<float>::quiet_NaN(),
                  1.0F,
                  1.0F},
             Vec3{},
             Vec3{})
             .has_value() &&
            !legacyGameplayCameraReduceCollisionAxisFactors(
                 Vec3{1.0F, 1.0F, 1.0F},
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F})
                 .has_value() &&
            !legacyGameplayCameraReduceCollisionAxisFactors(
                 Vec3{-std::numeric_limits<float>::max(),
                      1.0F,
                      1.0F},
                 Vec3{},
                 Vec3{
                     std::numeric_limits<float>::max() / 2.0F,
                     0.0F,
                     0.0F})
                 .has_value(),
        "invalid, overflowing delta, or overflowing reduction was accepted");
}

void testAircraftFactorRecovery() {
    const auto recovered =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.99F, 1.5F},
            0.1F,
            1.0F,
            false);
    require(recovered.has_value(), "valid factor recovery failed");
    requireVec(
        *recovered,
        Vec3{0.525F, 1.0F, 1.0F},
        "recovery increment or clamp changed");
    const auto rateProbe =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{}, 1.0F, 1.0F, false);
    require(
        rateProbe.has_value() &&
            std::bit_cast<std::uint32_t>(rateProbe->x) ==
                std::bit_cast<std::uint32_t>(0.25F),
        "aircraft recovery rate changed");

    const auto clearedByHealth =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.5F, 0.5F},
            0.1F,
            0.0F,
            false);
    const auto clearedByInactive =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.5F, 0.5F},
            0.1F,
            1.0F,
            true);
    require(
        clearedByHealth.has_value() &&
            clearedByInactive.has_value(),
        "valid gated recovery failed");
    requireVec(
        *clearedByHealth,
        Vec3{},
        "non-positive vehicle health did not clear factors");
    requireVec(
        *clearedByInactive,
        Vec3{},
        "inactive vehicle did not clear factors");

    const auto negativeRefresh =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.01F, 0.5F, 2.0F},
            -1.0F,
            1.0F,
            false);
    require(
        negativeRefresh.has_value(),
        "native finite negative refresh delta behavior changed");
    requireVec(
        *negativeRefresh,
        Vec3{0.0F, 0.25F, 1.0F},
        "refresh-delta algebra or clamps changed");

    const auto nominalRefresh =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{},
            legacyAircraftNominalRefreshDeltaSeconds,
            1.0F,
            false);
    require(
        nominalRefresh.has_value(),
        "nominal 12 ms aircraft refresh failed");
    requireVec(
        *nominalRefresh,
        Vec3{0.003F, 0.003F, 0.003F},
        "nominal seconds-to-factor recovery changed");

    require(
        !legacyAircraftRecoverGameplayCameraAxisFactors(
             Vec3{1.0F, 1.0F, 1.0F},
             std::numeric_limits<float>::quiet_NaN(),
             1.0F,
             false)
             .has_value() &&
            !legacyAircraftRecoverGameplayCameraAxisFactors(
                 Vec3{-std::numeric_limits<float>::max(),
                      1.0F,
                      1.0F},
                 -std::numeric_limits<float>::max(),
                 1.0F,
                 false)
                 .has_value(),
        "non-finite or overflowing recovery was accepted");
}

void testConstraintPlaneAdmissionAndOverrides() {
    const std::array<Vec3, 1U> existing{
        Vec3{1.0F, 0.0F, 0.0F},
    };
    const auto distinct =
        legacyGameplayCameraConstraintAcceptsPlane(
            existing,
            Vec3{
                std::nextafter(0.999F, 0.0F),
                0.0F,
                0.0F,
            });
    const auto duplicate =
        legacyGameplayCameraConstraintAcceptsPlane(
            existing, Vec3{0.999F, 0.0F, 0.0F});
    require(
        distinct == std::optional<bool>{true} &&
            duplicate == std::optional<bool>{false},
        "strict 0.999 plane admission threshold changed");

    constexpr float inverseSqrt2 = 0.70710678118654752440F;
    const Vec3 xPlane{1.0F, 0.0F, 0.0F};
    const Vec3 diagonalPlane{
        inverseSqrt2,
        inverseSqrt2,
        0.0F,
    };
    const Vec3 requestedMove{-1.0F, 1.0F, 0.0F};
    const auto xOverridesDiagonal =
        legacyGameplayCameraConstraintPlaneOverrides(
            xPlane, diagonalPlane, requestedMove);
    const auto diagonalOverridesX =
        legacyGameplayCameraConstraintPlaneOverrides(
            diagonalPlane, xPlane, requestedMove);
    require(
        xOverridesDiagonal == std::optional<bool>{true} &&
            diagonalOverridesX == std::optional<bool>{false},
        "directional Overrides predicate changed");

    const Vec3 nonFinite{
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        0.0F,
    };
    require(
        !legacyGameplayCameraConstraintAcceptsPlane(
             existing, nonFinite)
             .has_value() &&
            !legacyGameplayCameraConstraintPlaneOverrides(
                 xPlane, diagonalPlane, nonFinite)
                 .has_value(),
        "non-finite constraint predicate input was accepted");
}

void testConstrainedMovement() {
    const Vec3 requested{-2.0F, -3.0F, 4.0F};
    const auto unchanged =
        legacyGameplayCameraAttemptConstrainedMove(requested, {});
    require(
        unchanged.has_value() && *unchanged == requested,
        "empty constraint changed movement");

    const std::array<Vec3, 1U> xPlane{
        Vec3{1.0F, 0.0F, 0.0F},
    };
    const auto onePlane =
        legacyGameplayCameraAttemptConstrainedMove(
            requested, xPlane);
    require(
        onePlane.has_value(),
        "single-plane constrained movement failed");
    requireVec(
        *onePlane,
        Vec3{0.0F, -3.0F, 4.0F},
        "single-plane projection changed");

    const std::array<Vec3, 2U> xyPlanes{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F},
    };
    const auto twoPlanes =
        legacyGameplayCameraAttemptConstrainedMove(
            requested, xyPlanes);
    require(
        twoPlanes.has_value(),
        "two-plane constrained movement failed");
    requireVec(
        *twoPlanes,
        Vec3{0.0F, 0.0F, 4.0F},
        "two-plane cross-axis projection changed");

    const std::array<Vec3, 3U> xyzPlanes{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    };
    const auto threePlanes =
        legacyGameplayCameraAttemptConstrainedMove(
            Vec3{-1.0F, -1.0F, -1.0F}, xyzPlanes);
    require(
        threePlanes.has_value(),
        "three-plane constrained movement failed");
    requireVec(
        *threePlanes,
        Vec3{},
        "three mutually overriding planes did not stop movement");

    constexpr float narrowY = 0.005F;
    const float narrowX =
        std::sqrt(1.0F - narrowY * narrowY);
    const std::array<Vec3, 2U> narrowPlanes{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{narrowX, narrowY, 0.0F},
    };
    const Vec3 narrowMove{
        -1.0F,
        (narrowX - 1.0F) / narrowY,
        4.0F,
    };
    const auto narrow =
        legacyGameplayCameraAttemptConstrainedMove(
            narrowMove, narrowPlanes);
    require(
        narrow.has_value(),
        "narrow two-plane constraint failed");
    requireVec(
        *narrow,
        Vec3{},
        "sub-threshold cross product did not stop movement");

    constexpr float openY = 0.02F;
    const float openX =
        std::sqrt(1.0F - openY * openY);
    const std::array<Vec3, 2U> openPlanes{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{openX, openY, 0.0F},
    };
    const Vec3 openMove{
        -1.0F,
        (openX - 1.0F) / openY,
        4.0F,
    };
    const auto open =
        legacyGameplayCameraAttemptConstrainedMove(
            openMove, openPlanes);
    require(
        open.has_value(),
        "open two-plane constraint failed");
    requireVec(
        *open,
        Vec3{0.0F, 0.0F, 4.0F},
        "above-threshold cross product changed");

    require(
        !legacyGameplayCameraAttemptConstrainedMove(
             requested,
             std::array<Vec3, 1U>{
                 Vec3{
                     std::numeric_limits<float>::infinity(),
                     0.0F,
                     0.0F,
                 }})
             .has_value(),
        "non-finite constraint plane was accepted");
}

void testSphereTriangleCandidateAxes() {
    const Vec3 point0{-1.0F, -1.0F, 0.0F};
    const Vec3 point1{1.0F, -1.0F, 0.0F};
    const Vec3 point2{0.0F, 1.0F, 0.0F};
    const Vec3 splitNormal{0.0F, 0.0F, 1.0F};

    require(
        legacyGameplayCameraSphereTriangleCandidate(
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            point0,
            point1,
            point2,
            splitNormal) == std::optional<bool>{true},
        "intersecting sphere failed the seven-axis candidate test");
    require(
        legacyGameplayCameraSphereTriangleCandidate(
            Vec3{3.0F, 0.0F, -0.25F},
            0.5F,
            point0,
            point1,
            point2,
            splitNormal) == std::optional<bool>{false},
        "separated sphere passed the seven-axis candidate test");
    require(
        legacyGameplayCameraSphereTriangleCandidate(
            Vec3{0.0F, 0.0F, -0.5F},
            0.5F,
            point0,
            point1,
            point2,
            splitNormal) == std::optional<bool>{true},
        "exact plane tangency was not retained as a candidate");
    require(
        !legacyGameplayCameraSphereTriangleCandidate(
             Vec3{},
             0.0F,
             point0,
             point1,
             point2,
             splitNormal)
             .has_value() &&
            !legacyGameplayCameraSphereTriangleCandidate(
                 Vec3{},
                 1.0F,
                 point0,
                 point1,
                 point2,
                 Vec3{
                     std::numeric_limits<float>::quiet_NaN(),
                     0.0F,
                     1.0F})
                 .has_value(),
        "invalid sphere candidate input was accepted");
}

void testSphereTriangleClosestFeatures() {
    const Vec3 point0{-1.0F, -1.0F, 0.0F};
    const Vec3 point1{1.0F, -1.0F, 0.0F};
    const Vec3 edge01{2.0F, 0.0F, 0.0F};
    const Vec3 edge02{1.0F, 2.0F, 0.0F};
    const Vec3 faceNormal{0.0F, 0.0F, 1.0F};

    const auto face =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        face.status ==
                LegacyGameplayCameraSphereContactStatus::contact &&
            face.contact.has_value() &&
            face.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::face &&
            close(face.contact->penetrationDepth, 0.25F),
        "closest-face contact classification changed");
    requireVec(
        face.contact->contactPoint,
        Vec3{},
        "closest-face point changed");
    requireVec(
        face.contact->direction,
        faceNormal,
        "closest-face direction was not the native unit normal");

    const Vec3 edgeCenter{0.0F, -1.2F, -0.2F};
    const auto edge =
        legacyGameplayCameraSphereTriangleContact(
            edgeCenter,
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        edge.status ==
                LegacyGameplayCameraSphereContactStatus::contact &&
            edge.contact.has_value() &&
            edge.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::edge01 &&
            close(
                edge.contact->penetrationDepth,
                0.5F - std::sqrt(0.08F)),
        "closest-edge contact classification changed");
    requireVec(
        edge.contact->contactPoint,
        Vec3{0.0F, -1.0F, 0.0F},
        "closest-edge point changed");
    requireVec(
        edge.contact->direction,
        Vec3{0.0F, 0.2F, 0.2F},
        "closest-edge direction was incorrectly normalized");

    constexpr float inverseSqrt5 =
        0.44721359549995793928F;
    const Vec3 edge12Axis{
        2.0F * inverseSqrt5,
        inverseSqrt5,
        0.0F,
    };
    const Vec3 edge12Center{
        0.5F + 0.2F * edge12Axis.x,
        0.2F * edge12Axis.y,
        -0.2F,
    };
    const auto edge12Contact =
        legacyGameplayCameraSphereTriangleContact(
            edge12Center,
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        edge12Contact.contact.has_value() &&
            edge12Contact.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::edge12,
        "second edge branch changed");
    requireVec(
        edge12Contact.contact->contactPoint,
        Vec3{0.5F, 0.0F, 0.0F},
        "second edge point changed");

    const Vec3 edge20Axis{
        -2.0F * inverseSqrt5,
        inverseSqrt5,
        0.0F,
    };
    const Vec3 edge20Center{
        -0.5F + 0.2F * edge20Axis.x,
        0.2F * edge20Axis.y,
        -0.2F,
    };
    const auto edge20Contact =
        legacyGameplayCameraSphereTriangleContact(
            edge20Center,
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        edge20Contact.contact.has_value() &&
            edge20Contact.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::edge20,
        "third edge branch changed");
    requireVec(
        edge20Contact.contact->contactPoint,
        Vec3{-0.5F, 0.0F, 0.0F},
        "third edge point changed");

    const auto vertex0 =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{-1.2F, -1.2F, -0.2F},
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        vertex0.contact.has_value() &&
            vertex0.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::vertex0,
        "first vertex branch changed");
    requireVec(
        vertex0.contact->contactPoint,
        point0,
        "first vertex point changed");

    const Vec3 vertexCenter{1.2F, -1.2F, -0.2F};
    const auto vertex =
        legacyGameplayCameraSphereTriangleContact(
            vertexCenter,
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        vertex.status ==
                LegacyGameplayCameraSphereContactStatus::contact &&
            vertex.contact.has_value() &&
            vertex.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::vertex1 &&
            close(
                vertex.contact->penetrationDepth,
                0.5F - std::sqrt(0.12F)),
        "closest-vertex contact classification changed");
    requireVec(
        vertex.contact->contactPoint,
        point1,
        "closest-vertex point changed");
    requireVec(
        vertex.contact->direction,
        Vec3{-0.2F, 0.2F, 0.2F},
        "closest-vertex direction was incorrectly normalized");

    const auto vertex2 =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{0.0F, 1.3F, -0.2F},
            0.5F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        vertex2.contact.has_value() &&
            vertex2.contact->feature ==
                LegacyGameplayCameraSphereContactFeature::vertex2,
        "third vertex branch changed");
    requireVec(
        vertex2.contact->contactPoint,
        Vec3{0.0F, 1.0F, 0.0F},
        "third vertex point changed");

    const auto tangentVertex =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{1.375F, -1.0F, -0.5F},
            0.625F,
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        tangentVertex.status ==
                LegacyGameplayCameraSphereContactStatus::noContact &&
            !tangentVertex.contact.has_value(),
        "strict native vertex tangent was accepted");

    constexpr auto sentinelBits = std::uint32_t{0xFFC00000U};
    const float sentinel =
        std::bit_cast<float>(sentinelBits);
    const auto sentinelNormal =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            point0,
            edge01,
            edge02,
            Vec3{sentinel, sentinel, sentinel});
    const auto invalid =
        legacyGameplayCameraSphereTriangleContact(
            Vec3{},
            std::numeric_limits<float>::infinity(),
            point0,
            edge01,
            edge02,
            faceNormal);
    require(
        sentinelNormal.status ==
                LegacyGameplayCameraSphereContactStatus::noContact &&
            invalid.status ==
                LegacyGameplayCameraSphereContactStatus::invalidInput,
        "sentinel or invalid closest-feature boundary changed");
}

void testLineHitPoint() {
    const Vec3 anchor{10.0F, 20.0F, 30.0F};
    const Vec3 camera{14.0F, 12.0F, 34.0F};

    const auto start =
        legacyGameplayCameraLineHitPoint(anchor, camera, 0.0F);
    const auto middle =
        legacyGameplayCameraLineHitPoint(anchor, camera, 0.25F);
    const auto end =
        legacyGameplayCameraLineHitPoint(anchor, camera, 1.0F);
    require(
        start.has_value() && middle.has_value() &&
            end.has_value(),
        "valid line fractions failed");
    requireVec(*start, anchor, "zero fraction changed anchor");
    requireVec(
        *middle,
        Vec3{11.0F, 18.0F, 31.0F},
        "line interpolation order changed");
    requireVec(*end, camera, "unit fraction changed camera");

    require(
        !legacyGameplayCameraLineHitPoint(
             anchor, camera, -0.01F)
             .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 anchor, camera, 1.01F)
                 .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 anchor,
                 camera,
                 std::numeric_limits<float>::quiet_NaN())
                 .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 0.5F)
                 .has_value(),
        "invalid fraction or overflowing segment was accepted");
}

void requireLooksAtPositiveZ(
    const Vec3 camera,
    const Vec3 anchor,
    const char* const message) {
    const auto lookAt =
        legacyGameplayCameraLookAt(anchor, camera);
    require(lookAt.has_value(), message);

    const auto transform = buildLegacyCameraTransform(
        LegacyCameraTransformConfig{
            .linear = lookAt->cameraWorldLinear,
            .translation = camera,
        });
    require(
        transform.complete() && transform.transform.has_value(),
        "look-at matrix was not a valid camera transform");
    const auto cameraSpace =
        transform.transform->transform(anchor);
    require(
        cameraSpace.complete() &&
            cameraSpace.cameraSpacePosition.has_value(),
        "look-at target did not transform");

    const float expectedDistance = std::sqrt(
        lookAt->direction.x * lookAt->direction.x +
        lookAt->direction.y * lookAt->direction.y +
        lookAt->direction.z * lookAt->direction.z);
    requireVec(
        *cameraSpace.cameraSpacePosition,
        Vec3{0.0F, 0.0F, expectedDistance},
        message,
        2.0e-5F);
}

void testLookAtAxesAndCameraConvention() {
    const auto forward = legacyGameplayCameraLookAt(
        Vec3{0.0F, 0.0F, 5.0F}, Vec3{});
    require(forward.has_value(), "forward look-at failed");
    requireVec(
        forward->axisRotationRadians,
        Vec3{},
        "forward look-at was not zero rotation");
    require(
        forward->cameraWorldLinear == Mat3{},
        "forward look-at was not identity");

    constexpr float halfPi = 1.57079632679489661923F;
    const auto right = legacyGameplayCameraLookAt(
        Vec3{5.0F, 0.0F, 0.0F}, Vec3{});
    const auto up = legacyGameplayCameraLookAt(
        Vec3{0.0F, 5.0F, 0.0F}, Vec3{});
    require(
        right.has_value() && up.has_value(),
        "axis-aligned look-at failed");
    require(
        close(right->axisRotationRadians.x, 0.0F) &&
            close(right->axisRotationRadians.y, halfPi),
        "right look-at yaw sign changed");
    require(
        close(up->axisRotationRadians.x, halfPi) &&
            close(up->axisRotationRadians.y, 0.0F),
        "up look-at pitch sign changed");

    requireLooksAtPositiveZ(
        Vec3{},
        Vec3{5.0F, 0.0F, 0.0F},
        "right target did not map to positive camera Z");
    requireLooksAtPositiveZ(
        Vec3{},
        Vec3{0.0F, 5.0F, 0.0F},
        "up target did not map to positive camera Z");
    requireLooksAtPositiveZ(
        Vec3{10.0F, -4.0F, 7.0F},
        Vec3{-2.0F, 5.0F, 19.0F},
        "diagonal target did not map to positive camera Z");

    const auto diagonal = legacyGameplayCameraLookAt(
        Vec3{1.0F, 1.0F, 1.0F}, Vec3{});
    require(
        diagonal.has_value(),
        "unit diagonal look-at failed");
    constexpr float inverseSqrt2 = 0.70710678118654752440F;
    constexpr float inverseSqrt3 = 0.57735026918962576451F;
    constexpr float inverseSqrt6 = 0.40824829046386301637F;
    requireVec(
        diagonal->cameraWorldLinear.columns[0],
        Vec3{inverseSqrt2, 0.0F, -inverseSqrt2},
        "diagonal camera right column changed",
        2.0e-6F);
    requireVec(
        diagonal->cameraWorldLinear.columns[1],
        Vec3{-inverseSqrt6, 2.0F * inverseSqrt6, -inverseSqrt6},
        "diagonal camera up column changed",
        2.0e-6F);
    requireVec(
        diagonal->cameraWorldLinear.columns[2],
        Vec3{inverseSqrt3, inverseSqrt3, inverseSqrt3},
        "diagonal camera forward column changed",
        2.0e-6F);

    require(
        !legacyGameplayCameraLookAt(Vec3{}, Vec3{})
             .has_value() &&
            !legacyGameplayCameraLookAt(
                 Vec3{std::numeric_limits<float>::infinity(),
                      0.0F,
                      0.0F},
                 Vec3{})
                 .has_value() &&
            !legacyGameplayCameraLookAt(
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F})
                 .has_value(),
        "degenerate, non-finite, or overflowing look-at was accepted");
}

void testPrimitivesDoNotAllocateOrMutateInputs() {
    const Vec3 factors{0.2F, 0.4F, 0.6F};
    const Vec3 original{1.0F, 2.0F, 3.0F};
    const Vec3 resolved{1.1F, 1.9F, 3.05F};
    const Vec3 anchor{4.0F, 5.0F, 6.0F};
    const Vec3 camera{-1.0F, 2.0F, -3.0F};
    const std::array<Vec3, 2U> planes{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F},
    };
    const Vec3 triangle0{-1.0F, -1.0F, 0.0F};
    const Vec3 triangle1{1.0F, -1.0F, 0.0F};
    const Vec3 triangle2{0.0F, 1.0F, 0.0F};
    const Vec3 triangleEdge01{2.0F, 0.0F, 0.0F};
    const Vec3 triangleEdge02{1.0F, 2.0F, 0.0F};
    const Vec3 triangleNormal{0.0F, 0.0F, 1.0F};

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    float checksum = 0.0F;
    bool complete = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto radius =
            legacyGameplayCameraCollisionSphereRadius(0.25F);
        const auto reduced =
            legacyGameplayCameraReduceCollisionAxisFactors(
                factors, original, resolved);
        const auto recovered =
            legacyAircraftRecoverGameplayCameraAxisFactors(
                factors, 0.1F, 1.0F, false);
        const auto accepts =
            legacyGameplayCameraConstraintAcceptsPlane(
                planes, Vec3{0.0F, 0.0F, 1.0F});
        const auto overrides =
            legacyGameplayCameraConstraintPlaneOverrides(
                planes[0],
                planes[1],
                Vec3{-1.0F, -1.0F, 1.0F});
        const auto constrained =
            legacyGameplayCameraAttemptConstrainedMove(
                Vec3{-1.0F, -1.0F, 1.0F}, planes);
        const auto sphereCandidate =
            legacyGameplayCameraSphereTriangleCandidate(
                Vec3{0.0F, 0.0F, -0.25F},
                0.5F,
                triangle0,
                triangle1,
                triangle2,
                triangleNormal);
        const auto sphereContact =
            legacyGameplayCameraSphereTriangleContact(
                Vec3{0.0F, 0.0F, -0.25F},
                0.5F,
                triangle0,
                triangleEdge01,
                triangleEdge02,
                triangleNormal);
        const auto hit = legacyGameplayCameraLineHitPoint(
            anchor, camera, 0.5F);
        const auto lookAt =
            legacyGameplayCameraLookAt(anchor, camera);
        if (!radius.has_value() || !reduced.has_value() ||
            !recovered.has_value() || !accepts.has_value() ||
            !overrides.has_value() || !constrained.has_value() ||
            sphereCandidate != std::optional<bool>{true} ||
            !sphereContact.contact.has_value() ||
            !hit.has_value() || !lookAt.has_value()) {
            complete = false;
        } else {
            checksum += *radius + reduced->x + recovered->y +
                static_cast<float>(*accepts) +
                static_cast<float>(*overrides) + constrained->z +
                sphereContact.contact->penetrationDepth +
                hit->z + lookAt->cameraWorldLinear.columns[0].x;
        }
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        complete && std::isfinite(checksum),
        "repeated primitive evaluation failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "camera collision primitives allocated");
    require(
        factors == Vec3{0.2F, 0.4F, 0.6F} &&
            original == Vec3{1.0F, 2.0F, 3.0F} &&
            resolved == Vec3{1.1F, 1.9F, 3.05F} &&
            anchor == Vec3{4.0F, 5.0F, 6.0F} &&
            camera == Vec3{-1.0F, 2.0F, -3.0F} &&
            planes[0] == Vec3{1.0F, 0.0F, 0.0F} &&
            planes[1] == Vec3{0.0F, 1.0F, 0.0F},
        "camera primitive mutated an input");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndSphereRadius();
        testCollisionFactorReduction();
        testAircraftFactorRecovery();
        testConstraintPlaneAdmissionAndOverrides();
        testConstrainedMovement();
        testSphereTriangleCandidateAxes();
        testSphereTriangleClosestFeatures();
        testLineHitPoint();
        testLookAtAxesAndCameraConvention();
        testPrimitivesDoNotAllocateOrMutateInputs();
        std::cout
            << "LegacyGameplayCameraCollision tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr
            << "LegacyGameplayCameraCollision tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
