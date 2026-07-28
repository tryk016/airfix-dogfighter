#include "airfix/render/LegacyGameplayCameraCollision.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace airfix::render {
namespace {

constexpr float collisionRadiusScale =
    std::bit_cast<float>(std::uint32_t{0x3F8CCCCDU});
constexpr float aircraftAxisRecoveryRate = 0.25F;

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool finite(
    const std::span<const Vec3> values) noexcept {
    return std::ranges::all_of(values, [](const Vec3& value) {
        return finite(value);
    });
}

[[nodiscard]] double dotZyx(
    const Vec3& left,
    const Vec3& right) noexcept {
    double result =
        static_cast<double>(left.z) *
        static_cast<double>(right.z);
    result +=
        static_cast<double>(left.y) *
        static_cast<double>(right.y);
    result +=
        static_cast<double>(left.x) *
        static_cast<double>(right.x);
    return result;
}

[[nodiscard]] double dotXyz(
    const Vec3& left,
    const Vec3& right) noexcept {
    double result =
        static_cast<double>(left.x) *
        static_cast<double>(right.x);
    result +=
        static_cast<double>(left.y) *
        static_cast<double>(right.y);
    result +=
        static_cast<double>(left.z) *
        static_cast<double>(right.z);
    return result;
}

[[nodiscard]] Vec3 subtractBinary32(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        static_cast<float>(
            static_cast<double>(left.x) - right.x),
        static_cast<float>(
            static_cast<double>(left.y) - right.y),
        static_cast<float>(
            static_cast<double>(left.z) - right.z),
    };
}

[[nodiscard]] Vec3 addBinary32(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        static_cast<float>(
            static_cast<double>(left.x) + right.x),
        static_cast<float>(
            static_cast<double>(left.y) + right.y),
        static_cast<float>(
            static_cast<double>(left.z) + right.z),
    };
}

[[nodiscard]] Vec3 scaleBinary32(
    const Vec3& value,
    const double scale) noexcept {
    return {
        static_cast<float>(scale * value.x),
        static_cast<float>(scale * value.y),
        static_cast<float>(scale * value.z),
    };
}

[[nodiscard]] Vec3 crossBinary32(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        static_cast<float>(
            static_cast<double>(left.y) * right.z -
            static_cast<double>(left.z) * right.y),
        static_cast<float>(
            static_cast<double>(left.z) * right.x -
            static_cast<double>(left.x) * right.z),
        static_cast<float>(
            static_cast<double>(left.x) * right.y -
            static_cast<double>(left.y) * right.x),
    };
}

[[nodiscard]] Vec3 normalizeLegacyAxis(
    const Vec3& value) noexcept {
    const float lengthSquared =
        static_cast<float>(dotXyz(value, value));
    if (lengthSquared == 1.0F) {
        return value;
    }
    const double inverseLength =
        1.0 / std::sqrt(static_cast<double>(lengthSquared));
    return scaleBinary32(value, inverseLength);
}

[[nodiscard]] bool allAbove(
    const double first,
    const double second,
    const double third,
    const float radius) noexcept {
    return radius < first && radius < second && radius < third;
}

[[nodiscard]] bool allBelow(
    const double first,
    const double second,
    const double third,
    const float negativeRadius) noexcept {
    return first < negativeRadius &&
        second < negativeRadius &&
        third < negativeRadius;
}

[[nodiscard]] bool vertexAxisSeparates(
    const Vec3& axisSource,
    const Vec3& relative0,
    const Vec3& relative1,
    const Vec3& relative2,
    const float radius) noexcept {
    const Vec3 axis = normalizeLegacyAxis(axisSource);
    const double first = dotXyz(axis, relative0);
    const double second = dotXyz(axis, relative1);
    const double third = dotXyz(axis, relative2);
    return allAbove(first, second, third, radius) ||
        allBelow(first, second, third, -radius);
}

[[nodiscard]] bool edgeAxisSeparates(
    const Vec3& edge,
    const Vec3& anchorRelative,
    const Vec3& oppositeRelative,
    const float radius) noexcept {
    const double denominator = dotXyz(edge, edge);
    const double along =
        dotXyz(edge, anchorRelative) / denominator;
    const Vec3 projected =
        scaleBinary32(edge, along);
    const Vec3 perpendicular = subtractBinary32(
        anchorRelative, projected);
    const Vec3 axis = normalizeLegacyAxis(perpendicular);
    const double anchorProjection =
        dotXyz(axis, anchorRelative);
    const double oppositeProjection =
        dotXyz(axis, oppositeRelative);
    return (radius < anchorProjection &&
            radius < oppositeProjection) ||
        (anchorProjection < -radius &&
         oppositeProjection < -radius);
}

[[nodiscard]] LegacyGameplayCameraSphereContactResult noSphereContact()
    noexcept {
    return {
        .status =
            LegacyGameplayCameraSphereContactStatus::noContact,
        .contact = std::nullopt,
    };
}

[[nodiscard]] LegacyGameplayCameraSphereContactResult sphereContact(
    const float penetrationDepth,
    const Vec3& direction,
    const Vec3& contactPoint,
    const LegacyGameplayCameraSphereContactFeature feature) noexcept {
    if (!std::isfinite(penetrationDepth) ||
        !finite(direction) ||
        !finite(contactPoint)) {
        return noSphereContact();
    }
    return {
        .status =
            LegacyGameplayCameraSphereContactStatus::contact,
        .contact = LegacyGameplayCameraSphereContact{
            .penetrationDepth = penetrationDepth,
            .direction = direction,
            .contactPoint = contactPoint,
            .feature = feature,
        },
    };
}

[[nodiscard]] LegacyGameplayCameraSphereContactResult edgeSphereContact(
    const Vec3& sphereCenter,
    const float sphereRadius,
    const float planeDistance,
    const Vec3& projectedCenter,
    const Vec3& edgeAxisSource,
    const Vec3& relativeVertex,
    const LegacyGameplayCameraSphereContactFeature feature) noexcept {
    const Vec3 edgeAxis =
        normalizeLegacyAxis(edgeAxisSource);
    const double edgeDistance =
        dotXyz(edgeAxis, relativeVertex);
    const double distanceSquared =
        static_cast<double>(planeDistance) * planeDistance +
        edgeDistance * edgeDistance;
    const double radiusSquared =
        static_cast<double>(sphereRadius) * sphereRadius;
    if (!(distanceSquared <= radiusSquared)) {
        return noSphereContact();
    }

    const Vec3 contactPoint = addBinary32(
        projectedCenter,
        scaleBinary32(edgeAxis, edgeDistance));
    const Vec3 direction =
        subtractBinary32(contactPoint, sphereCenter);
    const float depth = static_cast<float>(
        static_cast<double>(sphereRadius) -
        std::sqrt(distanceSquared));
    return sphereContact(
        depth, direction, contactPoint, feature);
}

[[nodiscard]] LegacyGameplayCameraSphereContactResult vertexSphereContact(
    const Vec3& sphereCenter,
    const float sphereRadius,
    const float planeDistance,
    const Vec3& projectedCenter,
    const Vec3& vertex,
    const LegacyGameplayCameraSphereContactFeature feature) noexcept {
    const Vec3 planar =
        subtractBinary32(vertex, projectedCenter);
    const double distanceSquared =
        static_cast<double>(planeDistance) * planeDistance +
        dotXyz(planar, planar);
    const double radiusSquared =
        static_cast<double>(sphereRadius) * sphereRadius;
    // The native vertex path rejects equality, unlike its face/edge paths.
    if (!(distanceSquared < radiusSquared)) {
        return noSphereContact();
    }
    const Vec3 direction =
        subtractBinary32(vertex, sphereCenter);
    const float depth = static_cast<float>(
        static_cast<double>(sphereRadius) -
        std::sqrt(distanceSquared));
    return sphereContact(
        depth, direction, vertex, feature);
}

[[nodiscard]] bool constraintPlaneOverridesUnchecked(
    const Vec3& plane,
    const Vec3& otherPlane,
    const Vec3& requestedMove) noexcept {
    // The native x87 sequence spills the X/Y projected components and
    // residuals to binary32 while retaining the Z path in-register.
    double projection =
        static_cast<double>(otherPlane.x) *
        static_cast<double>(requestedMove.x);
    projection +=
        static_cast<double>(otherPlane.y) *
        static_cast<double>(requestedMove.y);
    projection +=
        static_cast<double>(otherPlane.z) *
        static_cast<double>(requestedMove.z);

    const float projectedX =
        static_cast<float>(projection * otherPlane.x);
    const float projectedY =
        static_cast<float>(projection * otherPlane.y);
    const float residualX = static_cast<float>(
        static_cast<double>(requestedMove.x) - projectedX);
    const float residualY = static_cast<float>(
        static_cast<double>(requestedMove.y) - projectedY);
    const double residualZ =
        static_cast<double>(requestedMove.z) -
        projection * static_cast<double>(otherPlane.z);

    double response =
        residualZ * static_cast<double>(plane.z);
    response +=
        static_cast<double>(residualY) *
        static_cast<double>(plane.y);
    response +=
        static_cast<double>(residualX) *
        static_cast<double>(plane.x);
    return response < 0.0;
}

[[nodiscard]] std::optional<float> reduceFactor(
    const float factor,
    const float correction) noexcept {
    const float reduced = factor - correction;
    if (!std::isfinite(reduced)) {
        return std::nullopt;
    }
    return std::max(0.0F, reduced);
}

[[nodiscard]] std::optional<float> recoverFactor(
    float factor,
    const float rawRefreshArgument) noexcept {
    if (factor < 1.0F) {
        const float increment =
            rawRefreshArgument *
            aircraftAxisRecoveryRate;
        if (!std::isfinite(increment)) {
            return std::nullopt;
        }
        factor += increment;
        if (!std::isfinite(factor)) {
            return std::nullopt;
        }
    }
    return std::clamp(factor, 0.0F, 1.0F);
}

} // namespace

std::optional<float> legacyGameplayCameraCollisionSphereRadius(
    const float nearClipping) noexcept {
    if (!std::isfinite(nearClipping) || !(nearClipping > 0.0F)) {
        return std::nullopt;
    }

    const float radius =
        nearClipping * collisionRadiusScale;
    if (!std::isfinite(radius)) {
        return std::nullopt;
    }
    return radius;
}

std::optional<Vec3> legacyGameplayCameraReduceCollisionAxisFactors(
    const Vec3& currentFactors,
    const Vec3& originalCameraPosition,
    const Vec3& resolvedCameraPosition) noexcept {
    if (!finite(currentFactors) || !finite(originalCameraPosition) ||
        !finite(resolvedCameraPosition)) {
        return std::nullopt;
    }

    const Vec3 delta{
        resolvedCameraPosition.x - originalCameraPosition.x,
        resolvedCameraPosition.y - originalCameraPosition.y,
        resolvedCameraPosition.z - originalCameraPosition.z,
    };
    const Vec3 correction{
        2.0F * std::abs(delta.x),
        2.0F * std::abs(delta.y),
        2.0F * std::abs(delta.z),
    };
    if (!finite(delta) || !finite(correction)) {
        return std::nullopt;
    }

    const auto reducedX =
        reduceFactor(currentFactors.x, correction.x);
    const auto reducedY =
        reduceFactor(currentFactors.y, correction.y);
    const auto reducedZ =
        reduceFactor(currentFactors.z, correction.z);
    if (!reducedX.has_value() || !reducedY.has_value() ||
        !reducedZ.has_value()) {
        return std::nullopt;
    }
    const Vec3 reduced{*reducedX, *reducedY, *reducedZ};
    if (!finite(reduced)) {
        return std::nullopt;
    }
    return reduced;
}

std::optional<Vec3> legacyAircraftRecoverGameplayCameraAxisFactors(
    const Vec3& currentFactors,
    const float rawRefreshArgument,
    const float vehicleField98,
    const bool vehicleFlag460) noexcept {
    if (!finite(currentFactors) ||
        !std::isfinite(rawRefreshArgument) ||
        !std::isfinite(vehicleField98)) {
        return std::nullopt;
    }

    if (!(vehicleField98 > 0.0F) || vehicleFlag460) {
        return Vec3{};
    }

    const auto recoveredX =
        recoverFactor(currentFactors.x, rawRefreshArgument);
    const auto recoveredY =
        recoverFactor(currentFactors.y, rawRefreshArgument);
    const auto recoveredZ =
        recoverFactor(currentFactors.z, rawRefreshArgument);
    if (!recoveredX.has_value() || !recoveredY.has_value() ||
        !recoveredZ.has_value()) {
        return std::nullopt;
    }
    const Vec3 recovered{*recoveredX, *recoveredY, *recoveredZ};
    if (!finite(recovered)) {
        return std::nullopt;
    }
    return recovered;
}

std::optional<bool> legacyGameplayCameraConstraintAcceptsPlane(
    const std::span<const Vec3> existingPlanesHeadFirst,
    const Vec3& candidate) noexcept {
    if (!finite(candidate) || !finite(existingPlanesHeadFirst)) {
        return std::nullopt;
    }

    for (const auto& existing : existingPlanesHeadFirst) {
        if (dotZyx(existing, candidate) >
            legacyGameplayCameraConstraintDuplicateDotThreshold) {
            return false;
        }
    }
    return true;
}

std::optional<bool> legacyGameplayCameraConstraintPlaneOverrides(
    const Vec3& plane,
    const Vec3& otherPlane,
    const Vec3& requestedMove) noexcept {
    if (!finite(plane) || !finite(otherPlane) ||
        !finite(requestedMove)) {
        return std::nullopt;
    }
    return constraintPlaneOverridesUnchecked(
        plane, otherPlane, requestedMove);
}

std::optional<Vec3> legacyGameplayCameraAttemptConstrainedMove(
    const Vec3& requestedMove,
    const std::span<const Vec3> planesHeadFirst) noexcept {
    if (!finite(requestedMove) || !finite(planesHeadFirst)) {
        return std::nullopt;
    }

    std::optional<std::size_t> primary;
    std::optional<std::size_t> secondary;
    for (std::size_t index = planesHeadFirst.size(); index > 0U; --index) {
        const std::size_t candidateIndex = index - 1U;
        const auto& candidate = planesHeadFirst[candidateIndex];
        if (!(dotZyx(requestedMove, candidate) < 0.0)) {
            continue;
        }
        if (!primary.has_value()) {
            primary = candidateIndex;
            continue;
        }

        const bool candidateOverridesPrimary =
            constraintPlaneOverridesUnchecked(
                candidate,
                planesHeadFirst[*primary],
                requestedMove);
        const bool primaryOverridesCandidate =
            constraintPlaneOverridesUnchecked(
                planesHeadFirst[*primary],
                candidate,
                requestedMove);
        auto nextSecondary = secondary;
        if (candidateOverridesPrimary) {
            if (primaryOverridesCandidate) {
                nextSecondary = candidateIndex;
                if (secondary.has_value()) {
                    const bool candidateOverridesSecondary =
                        constraintPlaneOverridesUnchecked(
                            candidate,
                            planesHeadFirst[*secondary],
                            requestedMove);
                    const bool secondaryOverridesCandidate =
                        constraintPlaneOverridesUnchecked(
                            planesHeadFirst[*secondary],
                            candidate,
                            requestedMove);
                    nextSecondary = secondary;
                    if (candidateOverridesSecondary) {
                        nextSecondary = candidateIndex;
                        if (secondaryOverridesCandidate) {
                            return Vec3{};
                        }
                    }
                }
            } else {
                primary = candidateIndex;
                if (secondary.has_value()) {
                    const bool candidateOverridesSecondary =
                        constraintPlaneOverridesUnchecked(
                            candidate,
                            planesHeadFirst[*secondary],
                            requestedMove);
                    const bool secondaryOverridesCandidate =
                        constraintPlaneOverridesUnchecked(
                            planesHeadFirst[*secondary],
                            candidate,
                            requestedMove);
                    if (candidateOverridesSecondary &&
                        !secondaryOverridesCandidate) {
                        nextSecondary.reset();
                    }
                }
            }
        }
        secondary = nextSecondary;
    }

    if (!primary.has_value()) {
        return requestedMove;
    }
    if (!secondary.has_value()) {
        const auto& plane = planesHeadFirst[*primary];
        const double projection = dotXyz(requestedMove, plane);
        const float projectedX =
            static_cast<float>(projection * plane.x);
        const float projectedY =
            static_cast<float>(projection * plane.y);
        const double projectedZ =
            projection * static_cast<double>(plane.z);
        const Vec3 result{
            static_cast<float>(
                static_cast<double>(requestedMove.x) - projectedX),
            static_cast<float>(
                static_cast<double>(requestedMove.y) - projectedY),
            static_cast<float>(
                static_cast<double>(requestedMove.z) - projectedZ),
        };
        return finite(result)
            ? std::optional<Vec3>{result}
            : std::nullopt;
    }

    const auto& first = planesHeadFirst[*primary];
    const auto& second = planesHeadFirst[*secondary];
    const float crossX = static_cast<float>(
        static_cast<double>(first.y) * second.z -
        static_cast<double>(first.z) * second.y);
    const float crossY = static_cast<float>(
        static_cast<double>(first.z) * second.x -
        static_cast<double>(first.x) * second.z);
    const float crossZ = static_cast<float>(
        static_cast<double>(first.x) * second.y -
        static_cast<double>(first.y) * second.x);
    const Vec3 cross{crossX, crossY, crossZ};
    if (!finite(cross)) {
        return std::nullopt;
    }

    const double lengthSquared = dotZyx(cross, cross);
    if (!std::isfinite(lengthSquared)) {
        return std::nullopt;
    }
    if (lengthSquared <
        legacyGameplayCameraConstraintCrossLengthSquaredThreshold) {
        return Vec3{};
    }

    double normalizedZ = cross.z;
    float normalizedX = cross.x;
    float normalizedY = cross.y;
    if (lengthSquared != 1.0) {
        const double inverseLength =
            1.0 / std::sqrt(lengthSquared);
        if (!std::isfinite(inverseLength)) {
            return std::nullopt;
        }
        normalizedX =
            static_cast<float>(inverseLength * cross.x);
        normalizedY =
            static_cast<float>(inverseLength * cross.y);
        normalizedZ = inverseLength * cross.z;
    }

    double projectedLength =
        normalizedZ * requestedMove.z;
    projectedLength +=
        static_cast<double>(normalizedY) * requestedMove.y;
    projectedLength +=
        static_cast<double>(normalizedX) * requestedMove.x;
    const Vec3 result{
        static_cast<float>(projectedLength * normalizedX),
        static_cast<float>(projectedLength * normalizedY),
        static_cast<float>(projectedLength * normalizedZ),
    };
    return finite(result)
        ? std::optional<Vec3>{result}
        : std::nullopt;
}

std::optional<bool> legacyGameplayCameraSphereTriangleCandidate(
    const Vec3& sphereCenter,
    const float sphereRadius,
    const Vec3& point0,
    const Vec3& point1,
    const Vec3& point2,
    const Vec3& splitNormal) noexcept {
    if (!finite(sphereCenter) ||
        !std::isfinite(sphereRadius) ||
        !(sphereRadius > 0.0F) ||
        !finite(point0) ||
        !finite(point1) ||
        !finite(point2) ||
        !finite(splitNormal)) {
        return std::nullopt;
    }

    const Vec3 relative0 =
        subtractBinary32(point0, sphereCenter);
    const double planeDistance =
        std::abs(dotZyx(relative0, splitNormal));
    if (sphereRadius < planeDistance) {
        return false;
    }

    const Vec3 relative1 =
        subtractBinary32(point1, sphereCenter);
    const Vec3 relative2 =
        subtractBinary32(point2, sphereCenter);
    if (vertexAxisSeparates(
            relative0,
            relative0,
            relative1,
            relative2,
            sphereRadius) ||
        vertexAxisSeparates(
            relative1,
            relative0,
            relative1,
            relative2,
            sphereRadius) ||
        vertexAxisSeparates(
            relative2,
            relative0,
            relative1,
            relative2,
            sphereRadius)) {
        return false;
    }

    const Vec3 edge01 =
        subtractBinary32(point1, point0);
    const Vec3 edge12 =
        subtractBinary32(point2, point1);
    const Vec3 edge20 =
        subtractBinary32(point0, point2);
    if (edgeAxisSeparates(
            edge01, relative0, relative2, sphereRadius) ||
        edgeAxisSeparates(
            edge12, relative1, relative0, sphereRadius) ||
        edgeAxisSeparates(
            edge20, relative2, relative1, sphereRadius)) {
        return false;
    }
    return true;
}

LegacyGameplayCameraSphereContactResult
legacyGameplayCameraSphereTriangleContact(
    const Vec3& sphereCenter,
    const float sphereRadius,
    const Vec3& point0,
    const Vec3& storedEdge01,
    const Vec3& storedEdge02,
    const Vec3& faceNormal) noexcept {
    if (!finite(sphereCenter) ||
        !std::isfinite(sphereRadius) ||
        !(sphereRadius > 0.0F) ||
        !finite(point0) ||
        !finite(storedEdge01) ||
        !finite(storedEdge02)) {
        return {
            .status =
                LegacyGameplayCameraSphereContactStatus::invalidInput,
            .contact = std::nullopt,
        };
    }
    // The shipped format's all-0xFFC00000 face-normal sentinel is allowed by
    // the arena but fails every ordered native comparison, producing no
    // contact. Other non-finite callers fail closed.
    if (!finite(faceNormal)) {
        const auto sentinel = [](const float component) {
            return std::bit_cast<std::uint32_t>(component) ==
                0xFFC00000U;
        };
        if (sentinel(faceNormal.x) &&
            sentinel(faceNormal.y) &&
            sentinel(faceNormal.z)) {
            return noSphereContact();
        }
        return {
            .status =
                LegacyGameplayCameraSphereContactStatus::invalidInput,
            .contact = std::nullopt,
        };
    }

    const Vec3 point0FromCenter =
        subtractBinary32(point0, sphereCenter);
    const float planeDistance = static_cast<float>(
        std::abs(dotZyx(point0FromCenter, faceNormal)));
    if (!(planeDistance <= sphereRadius) ||
        !(0.0F <= planeDistance)) {
        return noSphereContact();
    }
    const Vec3 projectedCenter = addBinary32(
        sphereCenter,
        scaleBinary32(faceNormal, planeDistance));

    const Vec3 edge01 = storedEdge01;
    const Vec3 edge12 =
        subtractBinary32(storedEdge02, storedEdge01);
    const Vec3 edge20{
        -storedEdge02.x,
        -storedEdge02.y,
        -storedEdge02.z,
    };
    const Vec3 relative0 =
        subtractBinary32(point0, projectedCenter);
    const Vec3 relative1 =
        addBinary32(relative0, edge01);
    const Vec3 relative2 =
        addBinary32(relative1, edge12);
    const Vec3 point1 =
        addBinary32(point0, edge01);
    const Vec3 point2 =
        addBinary32(point0, storedEdge02);
    const Vec3 axis01 =
        crossBinary32(edge01, faceNormal);
    const Vec3 axis12 =
        crossBinary32(edge12, faceNormal);
    const Vec3 axis20 =
        crossBinary32(edge20, faceNormal);

    const double side01 = dotXyz(axis01, relative0);
    const double side12 = dotXyz(axis12, relative1);
    const double side20 = dotXyz(axis20, relative2);
    if (side01 < 0.0) {
        const double endpointProduct =
            dotXyz(edge01, relative0) *
            dotXyz(edge01, relative1);
        if (endpointProduct < 0.0) {
            return edgeSphereContact(
                sphereCenter,
                sphereRadius,
                planeDistance,
                projectedCenter,
                axis01,
                relative0,
                LegacyGameplayCameraSphereContactFeature::edge01);
        }
    }
    if (!(side01 < 0.0) &&
        !(side12 < 0.0) &&
        !(side20 < 0.0)) {
        return sphereContact(
            static_cast<float>(
                static_cast<double>(sphereRadius) -
                planeDistance),
            faceNormal,
            projectedCenter,
            LegacyGameplayCameraSphereContactFeature::face);
    }
    if (side12 < 0.0) {
        const double endpointProduct =
            dotXyz(edge12, relative2) *
            dotXyz(edge12, relative1);
        if (endpointProduct < 0.0) {
            return edgeSphereContact(
                sphereCenter,
                sphereRadius,
                planeDistance,
                projectedCenter,
                axis12,
                relative1,
                LegacyGameplayCameraSphereContactFeature::edge12);
        }
    }

    if (side20 < 0.0) {
        const double endpointProduct =
            dotXyz(edge20, relative0) *
            dotXyz(edge20, relative2);
        if (endpointProduct < 0.0) {
            return edgeSphereContact(
                sphereCenter,
                sphereRadius,
                planeDistance,
                projectedCenter,
                axis20,
                relative2,
                LegacyGameplayCameraSphereContactFeature::edge20);
        }
    }

    const auto vertex0 = vertexSphereContact(
        sphereCenter,
        sphereRadius,
        planeDistance,
        projectedCenter,
        point0,
        LegacyGameplayCameraSphereContactFeature::vertex0);
    if (vertex0.status ==
        LegacyGameplayCameraSphereContactStatus::contact) {
        return vertex0;
    }
    const auto vertex1 = vertexSphereContact(
        sphereCenter,
        sphereRadius,
        planeDistance,
        projectedCenter,
        point1,
        LegacyGameplayCameraSphereContactFeature::vertex1);
    if (vertex1.status ==
        LegacyGameplayCameraSphereContactStatus::contact) {
        return vertex1;
    }
    return vertexSphereContact(
        sphereCenter,
        sphereRadius,
        planeDistance,
        projectedCenter,
        point2,
        LegacyGameplayCameraSphereContactFeature::vertex2);
}

std::optional<Vec3> legacyGameplayCameraLineHitPoint(
    const Vec3& vehicleWorldAnchor,
    const Vec3& cameraPosition,
    const float hitFraction) noexcept {
    if (!finite(vehicleWorldAnchor) || !finite(cameraPosition) ||
        !std::isfinite(hitFraction) || hitFraction < 0.0F ||
        hitFraction > 1.0F) {
        return std::nullopt;
    }

    const Vec3 delta{
        cameraPosition.x - vehicleWorldAnchor.x,
        cameraPosition.y - vehicleWorldAnchor.y,
        cameraPosition.z - vehicleWorldAnchor.z,
    };
    const Vec3 hitPoint{
        vehicleWorldAnchor.x + hitFraction * delta.x,
        vehicleWorldAnchor.y + hitFraction * delta.y,
        vehicleWorldAnchor.z + hitFraction * delta.z,
    };
    if (!finite(delta) || !finite(hitPoint)) {
        return std::nullopt;
    }
    return hitPoint;
}

std::optional<LegacyGameplayCameraLookAt>
legacyGameplayCameraLookAt(
    const Vec3& vehicleWorldAnchor,
    const Vec3& finalCameraPosition) noexcept {
    if (!finite(vehicleWorldAnchor) ||
        !finite(finalCameraPosition)) {
        return std::nullopt;
    }

    const Vec3 direction{
        vehicleWorldAnchor.x - finalCameraPosition.x,
        vehicleWorldAnchor.y - finalCameraPosition.y,
        vehicleWorldAnchor.z - finalCameraPosition.z,
    };
    if (!finite(direction) ||
        (direction.x == 0.0F && direction.y == 0.0F &&
         direction.z == 0.0F)) {
        return std::nullopt;
    }

    // FromDirection evaluates these operations with x87 intermediates in the
    // legacy binary. Double staging avoids premature binary32 overflow while
    // remaining an explicit portable approximation, not a bit-parity claim.
    const double x = direction.x;
    const double y = direction.y;
    const double z = direction.z;
    const double horizontal =
        std::sqrt(z * z + x * x);
    const double axisX = std::atan2(y, horizontal);
    const double axisY = std::atan2(x, z);
    if (!std::isfinite(horizontal) || !std::isfinite(axisX) ||
        !std::isfinite(axisY)) {
        return std::nullopt;
    }

    const Vec3 axisRotationRadians{
        static_cast<float>(axisX),
        static_cast<float>(axisY),
        0.0F,
    };
    if (!finite(axisRotationRadians)) {
        return std::nullopt;
    }

    // The existing axis-rotation reconstruction publishes transpose(raw) as
    // a runtime column matrix. Transpose it back to the raw CcMatrixRot/SRT
    // linear matrix expected by the legacy camera world-to-view relation.
    const auto runtimePose =
        tryConvertLegacyAxisRotationWorldPose(
            {},
            std::array<float, 3>{
                axisRotationRadians.x,
                axisRotationRadians.y,
                axisRotationRadians.z,
            });
    if (!runtimePose.has_value()) {
        return std::nullopt;
    }
    const Mat3 cameraWorldLinear =
        transpose(runtimePose->linear);
    if (!finite(cameraWorldLinear)) {
        return std::nullopt;
    }

    return LegacyGameplayCameraLookAt{
        .direction = direction,
        .axisRotationRadians = axisRotationRadians,
        .cameraWorldLinear = cameraWorldLinear,
    };
}

} // namespace airfix::render
