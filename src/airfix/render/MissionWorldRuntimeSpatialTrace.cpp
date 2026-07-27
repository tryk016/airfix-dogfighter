#include "airfix/render/MissionWorldRuntimeSpatialTrace.hpp"

#include <cmath>

namespace airfix::render {
namespace {

struct PreparedSpatialBasis final {
    Mat3 runtimeToSource{};
    Mat3 sourcePlaneNormalToRuntime{};
    float sourceUnitsPerRuntimeUnit{};
};

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] std::optional<PreparedSpatialBasis> prepareBasis(
    const BasisTransform& basis) noexcept {
    if (!finite(basis.sourceToRuntime) ||
        !std::isfinite(basis.runtimeUnitsPerSourceUnit) ||
        !(basis.runtimeUnitsPerSourceUnit > 0.0F)) {
        return std::nullopt;
    }
    const auto runtimeToSource = inverse(basis.sourceToRuntime);
    const float sourceUnitsPerRuntimeUnit =
        1.0F / basis.runtimeUnitsPerSourceUnit;
    if (!runtimeToSource.has_value() ||
        !finite(*runtimeToSource) ||
        !std::isfinite(sourceUnitsPerRuntimeUnit)) {
        return std::nullopt;
    }
    const Mat3 sourcePlaneNormalToRuntime =
        transpose(*runtimeToSource);
    if (!finite(sourcePlaneNormalToRuntime)) {
        return std::nullopt;
    }
    return PreparedSpatialBasis{
        .runtimeToSource = *runtimeToSource,
        .sourcePlaneNormalToRuntime =
            sourcePlaneNormalToRuntime,
        .sourceUnitsPerRuntimeUnit =
            sourceUnitsPerRuntimeUnit,
    };
}

[[nodiscard]] std::optional<assets::CcfVector3> sourcePoint(
    const Vec3& runtimePoint,
    const PreparedSpatialBasis& basis) noexcept {
    if (!finite(runtimePoint)) {
        return std::nullopt;
    }
    const Vec3 scaled{
        runtimePoint.x * basis.sourceUnitsPerRuntimeUnit,
        runtimePoint.y * basis.sourceUnitsPerRuntimeUnit,
        runtimePoint.z * basis.sourceUnitsPerRuntimeUnit,
    };
    const Vec3 converted =
        applyRuntimeColumn(basis.runtimeToSource, scaled);
    if (!finite(scaled) || !finite(converted)) {
        return std::nullopt;
    }
    return assets::CcfVector3{
        converted.x,
        converted.y,
        converted.z,
    };
}

[[nodiscard]] std::optional<Vec3> runtimePoint(
    const assets::CcfVector3& sourcePoint,
    const BasisTransform& basis) noexcept {
    const Vec3 source{
        sourcePoint[0],
        sourcePoint[1],
        sourcePoint[2],
    };
    Vec3 converted =
        applyRuntimeColumn(basis.sourceToRuntime, source);
    converted.x *= basis.runtimeUnitsPerSourceUnit;
    converted.y *= basis.runtimeUnitsPerSourceUnit;
    converted.z *= basis.runtimeUnitsPerSourceUnit;
    if (!finite(source) || !finite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] std::optional<Vec3> runtimePlaneNormal(
    const assets::CcfVector3& sourceNormal,
    const PreparedSpatialBasis& basis) noexcept {
    const Vec3 source{
        sourceNormal[0],
        sourceNormal[1],
        sourceNormal[2],
    };
    const Vec3 converted = applyRuntimeColumn(
        basis.sourcePlaneNormalToRuntime, source);
    if (!finite(source) || !finite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] std::optional<MissionWorldRuntimeSpatialLineHit>
adaptHit(
    const assets::MissionWorldSpatialLineHit& sourceHit,
    const BasisTransform& runtimeBasis,
    const PreparedSpatialBasis& preparedBasis) noexcept {
    if (!std::isfinite(sourceHit.fraction)) {
        return std::nullopt;
    }
    const auto point = runtimePoint(sourceHit.point, runtimeBasis);
    const auto normal =
        runtimePlaneNormal(sourceHit.normal, preparedBasis);
    if (!point.has_value() || !normal.has_value()) {
        return std::nullopt;
    }
    return MissionWorldRuntimeSpatialLineHit{
        .legacyFraction = sourceHit.fraction,
        .runtimePoint = *point,
        .runtimePlaneNormal = *normal,
        .ownerWorldRoomIndex =
            sourceHit.ownerWorldRoomIndex,
        .treeIndex = sourceHit.treeIndex,
        .nodeIndex = sourceHit.nodeIndex,
        .polygonIndex = sourceHit.polygonIndex,
        .portalWorldRoomIndex =
            sourceHit.portalWorldRoomIndex,
        .reverseFacing = sourceHit.reverseFacing,
        .withinRequestedSegment =
            sourceHit.fraction >= 0.0F &&
            sourceHit.fraction <= 1.0F,
    };
}

[[nodiscard]] MissionWorldRuntimeSpatialLineTraceStatus
lineStatus(
    const assets::MissionWorldSpatialLineTraceStatus status) noexcept {
    switch (status) {
    case assets::MissionWorldSpatialLineTraceStatus::noHit:
        return MissionWorldRuntimeSpatialLineTraceStatus::noHit;
    case assets::MissionWorldSpatialLineTraceStatus::hit:
        return MissionWorldRuntimeSpatialLineTraceStatus::hit;
    case assets::MissionWorldSpatialLineTraceStatus::invalidArena:
        return MissionWorldRuntimeSpatialLineTraceStatus::invalidArena;
    case assets::MissionWorldSpatialLineTraceStatus::invalidWorldRoom:
        return MissionWorldRuntimeSpatialLineTraceStatus::invalidWorldRoom;
    case assets::MissionWorldSpatialLineTraceStatus::invalidInput:
        return MissionWorldRuntimeSpatialLineTraceStatus::invalidInput;
    case assets::MissionWorldSpatialLineTraceStatus::
        traversalDepthExceeded:
        return MissionWorldRuntimeSpatialLineTraceStatus::
            traversalDepthExceeded;
    }
    return MissionWorldRuntimeSpatialLineTraceStatus::invalidArena;
}

[[nodiscard]] MissionWorldRuntimePortalTraceStatus portalStatus(
    const assets::MissionWorldPortalTraceStatus status) noexcept {
    switch (status) {
    case assets::MissionWorldPortalTraceStatus::noTransition:
        return MissionWorldRuntimePortalTraceStatus::noTransition;
    case assets::MissionWorldPortalTraceStatus::transition:
        return MissionWorldRuntimePortalTraceStatus::transition;
    case assets::MissionWorldPortalTraceStatus::invalidArena:
        return MissionWorldRuntimePortalTraceStatus::invalidArena;
    case assets::MissionWorldPortalTraceStatus::invalidWorldRoom:
        return MissionWorldRuntimePortalTraceStatus::invalidWorldRoom;
    case assets::MissionWorldPortalTraceStatus::invalidInput:
        return MissionWorldRuntimePortalTraceStatus::invalidInput;
    case assets::MissionWorldPortalTraceStatus::
        traversalDepthExceeded:
        return MissionWorldRuntimePortalTraceStatus::
            traversalDepthExceeded;
    case assets::MissionWorldPortalTraceStatus::
        transitionLimitExceeded:
        return MissionWorldRuntimePortalTraceStatus::
            transitionLimitExceeded;
    case assets::MissionWorldPortalTraceStatus::outOfSegmentHit:
        return MissionWorldRuntimePortalTraceStatus::outOfSegmentHit;
    }
    return MissionWorldRuntimePortalTraceStatus::invalidArena;
}

} // namespace

MissionWorldRuntimeSpatialLineTraceResult
traceMissionWorldRuntimeSpatialLine(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const std::size_t worldRoomIndex,
    const assets::CcfBspTreeKind treeKind,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const assets::MissionWorldSpatialLineTraceOptions& options) noexcept {
    const auto preparedBasis = prepareBasis(runtimeBasis);
    if (!preparedBasis.has_value()) {
        return {
            .status =
                MissionWorldRuntimeSpatialLineTraceStatus::invalidBasis,
            .hit = std::nullopt,
        };
    }
    const auto start = sourcePoint(runtimeStart, *preparedBasis);
    const auto end = sourcePoint(runtimeEnd, *preparedBasis);
    if (!start.has_value() || !end.has_value()) {
        return {
            .status =
                MissionWorldRuntimeSpatialLineTraceStatus::invalidInput,
            .hit = std::nullopt,
        };
    }

    const auto sourceResult = assets::traceMissionWorldSpatialLine(
        arena, worldRoomIndex, treeKind, *start, *end, options);
    const auto status = lineStatus(sourceResult.status);
    if (sourceResult.status !=
            assets::MissionWorldSpatialLineTraceStatus::hit) {
        if (sourceResult.hit.has_value()) {
            return {
                .status =
                    MissionWorldRuntimeSpatialLineTraceStatus::
                        invalidArena,
                .hit = std::nullopt,
            };
        }
        return {
            .status = status,
            .hit = std::nullopt,
        };
    }
    if (!sourceResult.hit.has_value()) {
        return {
            .status =
                MissionWorldRuntimeSpatialLineTraceStatus::invalidArena,
            .hit = std::nullopt,
        };
    }
    const auto hit =
        adaptHit(*sourceResult.hit, runtimeBasis, *preparedBasis);
    if (!hit.has_value()) {
        return {
            .status =
                MissionWorldRuntimeSpatialLineTraceStatus::invalidInput,
            .hit = std::nullopt,
        };
    }
    if (!hit->withinRequestedSegment) {
        return {
            .status =
                MissionWorldRuntimeSpatialLineTraceStatus::
                    outOfSegmentHit,
            .hit = *hit,
        };
    }
    return {
        .status = status,
        .hit = *hit,
    };
}

MissionWorldRuntimePortalTraceResult
traceMissionWorldRuntimePortalTransition(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const std::size_t worldRoomIndex,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const assets::MissionWorldPortalTraceOptions& options) noexcept {
    const auto preparedBasis = prepareBasis(runtimeBasis);
    if (!preparedBasis.has_value()) {
        return {
            .status =
                MissionWorldRuntimePortalTraceStatus::invalidBasis,
            .hit = std::nullopt,
            .targetWorldRoomIndex = std::nullopt,
            .transitionCount = 0U,
        };
    }
    const auto start = sourcePoint(runtimeStart, *preparedBasis);
    const auto end = sourcePoint(runtimeEnd, *preparedBasis);
    if (!start.has_value() || !end.has_value()) {
        return {
            .status =
                MissionWorldRuntimePortalTraceStatus::invalidInput,
            .hit = std::nullopt,
            .targetWorldRoomIndex = std::nullopt,
            .transitionCount = 0U,
        };
    }

    auto strictOptions = options;
    strictOptions.requireHitsWithinSegment = true;
    const auto sourceResult =
        assets::traceMissionWorldPortalTransition(
            arena, worldRoomIndex, *start, *end, strictOptions);
    const auto status = portalStatus(sourceResult.status);
    std::optional<MissionWorldRuntimeSpatialLineHit> hit;
    if (sourceResult.hit.has_value()) {
        hit = adaptHit(
            *sourceResult.hit, runtimeBasis, *preparedBasis);
        if (!hit.has_value()) {
            return {
                .status =
                    MissionWorldRuntimePortalTraceStatus::invalidInput,
                .hit = std::nullopt,
                .targetWorldRoomIndex = std::nullopt,
                .transitionCount = 0U,
            };
        }
    }
    if (sourceResult.status ==
            assets::MissionWorldPortalTraceStatus::transition &&
        !sourceResult.targetWorldRoomIndex.has_value()) {
        return {
            .status =
                MissionWorldRuntimePortalTraceStatus::invalidArena,
            .hit = std::nullopt,
            .targetWorldRoomIndex = std::nullopt,
            .transitionCount = 0U,
        };
    }
    return {
        .status = status,
        .hit = hit,
        .targetWorldRoomIndex =
            sourceResult.targetWorldRoomIndex,
        .transitionCount = sourceResult.transitionCount,
    };
}

} // namespace airfix::render
