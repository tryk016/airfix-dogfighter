#include "airfix/render/LegacyDynamicBsp.hpp"

#include "airfix/assets/MissionWorldSpatialLineTrace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace airfix::render {
namespace {

constexpr double splitNegativeTolerance = -1.0e-6;
constexpr double splitPositiveTolerance = 1.0e-6;
constexpr double minimumFragmentArea = 1.0e-4;

struct PolygonSeed final {
    Vec3 faceCross{};
    Vec3 faceNormal{};
    Vec3 point0{};
    Vec3 edge01{};
    Vec3 edge12{};
    std::uint32_t triangleIndex{};
    std::uint32_t materialReference{};
    std::optional<std::uint32_t> materialCollisionMode2152;
};

struct WorkingPolygon final {
    PolygonSeed seed;
    std::vector<Vec3> vertices;
};

enum class ChildSide : std::uint8_t {
    a,
    b,
};

struct BuildTask final {
    std::vector<WorkingPolygon> polygons;
    std::optional<std::size_t> parentNodeIndex;
    ChildSide parentSide{ChildSide::a};
    std::size_t depth{};
};

struct BuildBudget final {
    std::size_t workingPolygons{};
    std::size_t workingVertices{};
};

struct SplitResult final {
    bool coplanar{};
    std::optional<WorkingPolygon> negative;
    std::optional<WorkingPolygon> positive;
};

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) &&
        finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] Vec3 subtract(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

[[nodiscard]] Vec3 add(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] Vec3 scale(
    const Vec3& value,
    const double factor) noexcept {
    return {
        static_cast<float>(factor * value.x),
        static_cast<float>(factor * value.y),
        static_cast<float>(factor * value.z),
    };
}

[[nodiscard]] Vec3 cross(
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

[[nodiscard]] double dot(
    const Vec3& left,
    const Vec3& right) noexcept {
    double result =
        static_cast<double>(left.z) * right.z;
    result +=
        static_cast<double>(left.y) * right.y;
    result +=
        static_cast<double>(left.x) * right.x;
    return result;
}

[[nodiscard]] double planeDistance(
    const Vec3& point,
    const PolygonSeed& plane) noexcept {
    return dot(subtract(point, plane.point0), plane.faceNormal);
}

[[nodiscard]] std::optional<Vec3> normalized(
    const Vec3& value) noexcept {
    const double lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > 0.0)) {
        return std::nullopt;
    }
    if (lengthSquared == 1.0) {
        return value;
    }
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    const auto result = scale(value, inverseLength);
    return finite(result)
        ? std::optional<Vec3>{result}
        : std::nullopt;
}

[[nodiscard]] bool orthonormal(const Mat3& value) noexcept {
    constexpr double tolerance = 1.0e-5;
    if (!finite(value)) {
        return false;
    }
    for (std::size_t column = 0U; column < 3U; ++column) {
        if (std::abs(
                dot(value.columns[column], value.columns[column]) -
                1.0) > tolerance) {
            return false;
        }
        for (std::size_t other = column + 1U;
             other < 3U;
             ++other) {
            if (std::abs(dot(
                    value.columns[column],
                    value.columns[other])) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] assets::CcfVector3 sourceVector(
    const Vec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] Vec3 runtimeVector(
    const assets::CcfVector3& value) noexcept {
    return {value[0], value[1], value[2]};
}

void addIssue(
    LegacyDynamicBspMesh& result,
    const LegacyDynamicBspBuildIssueKind kind,
    const std::optional<std::size_t> triangleIndex = std::nullopt,
    const std::optional<std::uint32_t> materialReference =
        std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .triangleIndex = triangleIndex,
        .materialReference = materialReference,
    });
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool checkedBytes(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    if (elementSize != 0U &&
        count >
            std::numeric_limits<std::uint64_t>::max() /
                elementSize) {
        return false;
    }
    const auto bytes =
        static_cast<std::uint64_t>(count) * elementSize;
    if (bytes >
        std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += bytes;
    return true;
}

[[nodiscard]] std::optional<std::uint32_t> materialMode(
    const std::span<const LegacyDynamicBspMaterialBinding> bindings,
    const std::uint32_t reference) noexcept {
    const auto iterator = std::lower_bound(
        bindings.begin(),
        bindings.end(),
        reference,
        [](const LegacyDynamicBspMaterialBinding& binding,
           const std::uint32_t value) {
            return binding.sourceReference < value;
        });
    if (iterator != bindings.end() &&
        iterator->sourceReference == reference) {
        return iterator->collisionMode2152;
    }
    return std::nullopt;
}

[[nodiscard]] bool consumeWorkingPolygon(
    BuildBudget& budget,
    const std::size_t vertexCount,
    const LegacyDynamicBspBuildLimits& limits) noexcept {
    std::size_t polygons = 0U;
    std::size_t vertices = 0U;
    if (!checkedAdd(
            budget.workingPolygons, 1U, polygons) ||
        !checkedAdd(
            budget.workingVertices, vertexCount, vertices) ||
        polygons > limits.maximumWorkingPolygons ||
        vertices > limits.maximumWorkingVertices) {
        return false;
    }
    budget.workingPolygons = polygons;
    budget.workingVertices = vertices;
    return true;
}

[[nodiscard]] double fragmentArea(
    const std::span<const Vec3> vertices) noexcept {
    if (vertices.size() < 3U) {
        return 0.0;
    }
    double result = 0.0;
    const auto& anchor = vertices.front();
    for (std::size_t index = 2U;
         index < vertices.size();
         ++index) {
        const auto first =
            subtract(vertices[index - 1U], anchor);
        const auto second =
            subtract(vertices[index], anchor);
        const auto areaVector = cross(first, second);
        const double lengthSquared = dot(areaVector, areaVector);
        if (!std::isfinite(lengthSquared) ||
            lengthSquared < 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        result += std::sqrt(lengthSquared) * 0.5;
    }
    return result;
}

[[nodiscard]] std::optional<WorkingPolygon> makeFragment(
    const WorkingPolygon& source,
    std::vector<Vec3> vertices,
    BuildBudget& budget,
    const LegacyDynamicBspBuildLimits& limits) {
    const double area = fragmentArea(vertices);
    if (!std::isfinite(area)) {
        return std::nullopt;
    }
    if (area < minimumFragmentArea) {
        return WorkingPolygon{};
    }
    if (!consumeWorkingPolygon(
            budget, vertices.size(), limits)) {
        throw LegacyDynamicBspBuildIssueKind::limitExceeded;
    }
    return WorkingPolygon{
        .seed = source.seed,
        .vertices = std::move(vertices),
    };
}

[[nodiscard]] SplitResult splitPolygon(
    const WorkingPolygon& polygon,
    const PolygonSeed& plane,
    BuildBudget& budget,
    const LegacyDynamicBspBuildLimits& limits) {
    std::vector<double> distances;
    distances.reserve(polygon.vertices.size());
    bool noPositiveTolerance = true;
    bool noNegativeTolerance = true;
    bool allNonNegative = true;
    bool allNegative = true;
    for (const auto& vertex : polygon.vertices) {
        const double distance = planeDistance(vertex, plane);
        if (!std::isfinite(distance)) {
            throw LegacyDynamicBspBuildIssueKind::invalidGeometry;
        }
        distances.push_back(distance);
        if (distance >= splitPositiveTolerance) {
            noPositiveTolerance = false;
        }
        if (distance <= splitNegativeTolerance) {
            noNegativeTolerance = false;
        }
        if (distance < 0.0) {
            allNonNegative = false;
        }
        else {
            allNegative = false;
        }
    }

    if (noPositiveTolerance && noNegativeTolerance) {
        return {
            .coplanar = true,
            .negative = std::nullopt,
            .positive = std::nullopt,
        };
    }

    if (allNegative || allNonNegative) {
        auto copied = makeFragment(
            polygon, polygon.vertices, budget, limits);
        if (!copied.has_value()) {
            throw LegacyDynamicBspBuildIssueKind::invalidGeometry;
        }
        if (copied->vertices.empty()) {
            return {};
        }
        if (allNegative) {
            return {
                .coplanar = false,
                .negative = std::move(*copied),
                .positive = std::nullopt,
            };
        }
        return {
            .coplanar = false,
            .negative = std::nullopt,
            .positive = std::move(*copied),
        };
    }

    std::vector<Vec3> negative;
    std::vector<Vec3> positive;
    negative.reserve(polygon.vertices.size() + 1U);
    positive.reserve(polygon.vertices.size() + 1U);
    for (std::size_t index = 0U;
         index < polygon.vertices.size();
         ++index) {
        const auto next =
            (index + 1U) % polygon.vertices.size();
        const auto& currentVertex = polygon.vertices[index];
        const auto& nextVertex = polygon.vertices[next];
        const double currentDistance = distances[index];
        const double nextDistance = distances[next];
        if (currentDistance < 0.0) {
            negative.push_back(currentVertex);
        }
        else {
            positive.push_back(currentVertex);
        }

        const bool currentNonNegative =
            currentDistance >= 0.0;
        const bool nextNonNegative = nextDistance >= 0.0;
        if (currentNonNegative != nextNonNegative) {
            const double denominator =
                nextDistance - currentDistance;
            if (!std::isfinite(denominator) ||
                denominator == 0.0) {
                throw LegacyDynamicBspBuildIssueKind::invalidGeometry;
            }
            const double fraction =
                -currentDistance / denominator;
            const auto edge =
                subtract(nextVertex, currentVertex);
            const auto intersection =
                add(currentVertex, scale(edge, fraction));
            if (!finite(intersection)) {
                throw LegacyDynamicBspBuildIssueKind::invalidGeometry;
            }
            // Native writes the same computed intersection to the positive
            // fragment first and the negative fragment second.
            positive.push_back(intersection);
            negative.push_back(intersection);
        }
    }

    auto negativeFragment = makeFragment(
        polygon, std::move(negative), budget, limits);
    auto positiveFragment = makeFragment(
        polygon, std::move(positive), budget, limits);
    if (!negativeFragment.has_value() ||
        !positiveFragment.has_value()) {
        throw LegacyDynamicBspBuildIssueKind::invalidGeometry;
    }
    if (negativeFragment->vertices.empty()) {
        negativeFragment.reset();
    }
    if (positiveFragment->vertices.empty()) {
        positiveFragment.reset();
    }
    return {
        .negative = std::move(negativeFragment),
        .positive = std::move(positiveFragment),
    };
}

[[nodiscard]] std::size_t chooseSplitter(
    const std::span<const WorkingPolygon> polygons) noexcept {
    float bestScore = -1.0F;
    std::size_t bestIndex = 0U;
    const float reciprocal =
        1.0F / static_cast<float>(polygons.size());
    for (std::size_t candidateIndex = 0U;
         candidateIndex < polygons.size();
         ++candidateIndex) {
        std::size_t negativeCount = 0U;
        std::size_t positiveCount = 0U;
        const auto& plane = polygons[candidateIndex].seed;
        for (std::size_t polygonIndex = 0U;
             polygonIndex < polygons.size();
             ++polygonIndex) {
            if (polygonIndex == candidateIndex) {
                continue;
            }
            bool noPositive = true;
            bool noNegative = true;
            for (const auto& vertex :
                 polygons[polygonIndex].vertices) {
                const double distance =
                    planeDistance(vertex, plane);
                if (distance >= splitPositiveTolerance) {
                    noPositive = false;
                }
                if (distance <= splitNegativeTolerance) {
                    noNegative = false;
                }
            }
            if (noPositive) {
                if (!noNegative) {
                    ++negativeCount;
                }
            }
            else if (noNegative) {
                ++positiveCount;
            }
            else {
                ++negativeCount;
                ++positiveCount;
            }
        }
        const auto total =
            static_cast<float>(
                negativeCount + positiveCount);
        const auto imbalance =
            static_cast<float>(
                negativeCount > positiveCount
                    ? negativeCount - positiveCount
                    : positiveCount - negativeCount);
        const float score =
            (2.0F - total * reciprocal) *
            (2.0F - imbalance * reciprocal);
        if (bestScore < score) {
            bestScore = score;
            bestIndex = candidateIndex;
        }
    }
    return bestIndex;
}

[[nodiscard]] bool appendNodePolygon(
    LegacyDynamicBspMesh& result,
    const PolygonSeed& seed,
    const LegacyDynamicBspBuildLimits& limits) {
    if (result.localArena.polygons.size() >=
        limits.maximumRetainedPolygons) {
        return false;
    }
    result.localArena.polygons.push_back({
        .faceCross = sourceVector(seed.faceCross),
        .faceNormal = sourceVector(seed.faceNormal),
        .point0 = sourceVector(seed.point0),
        .edge01 = sourceVector(seed.edge01),
        .edge12 = sourceVector(seed.edge12),
        .polygonIndex = seed.triangleIndex,
        .placedObjectReference = 0U,
        .materialCollisionMode2152 =
            seed.materialCollisionMode2152,
        .portalWorldRoomIndex = std::nullopt,
        .portalMeshSelectionFlagB = false,
        .portalType = 0U,
        .portalObjectVisible = false,
    });
    result.polygonMaterialReferences.push_back(
        seed.materialReference);
    return true;
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceStatus
mapStaticStatus(
    const MissionWorldRuntimeSpatialLineTraceStatus status) noexcept {
    switch (status) {
    case MissionWorldRuntimeSpatialLineTraceStatus::noHit:
        return MissionWorldRuntimeCombinedLineTraceStatus::noHit;
    case MissionWorldRuntimeSpatialLineTraceStatus::hit:
        return MissionWorldRuntimeCombinedLineTraceStatus::hit;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidArena:
        return MissionWorldRuntimeCombinedLineTraceStatus::invalidArena;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidWorldRoom:
        return MissionWorldRuntimeCombinedLineTraceStatus::invalidWorldRoom;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidInput:
        return MissionWorldRuntimeCombinedLineTraceStatus::invalidInput;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidBasis:
        return MissionWorldRuntimeCombinedLineTraceStatus::invalidBasis;
    case MissionWorldRuntimeSpatialLineTraceStatus::
        traversalDepthExceeded:
        return MissionWorldRuntimeCombinedLineTraceStatus::
            traversalDepthExceeded;
    case MissionWorldRuntimeSpatialLineTraceStatus::outOfSegmentHit:
        return MissionWorldRuntimeCombinedLineTraceStatus::
            outOfSegmentHit;
    }
    return MissionWorldRuntimeCombinedLineTraceStatus::invalidArena;
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceStatus
mapLocalStatus(
    const assets::MissionWorldSpatialLineTraceStatus status) noexcept {
    switch (status) {
    case assets::MissionWorldSpatialLineTraceStatus::noHit:
        return MissionWorldRuntimeCombinedLineTraceStatus::noHit;
    case assets::MissionWorldSpatialLineTraceStatus::hit:
        return MissionWorldRuntimeCombinedLineTraceStatus::hit;
    case assets::MissionWorldSpatialLineTraceStatus::invalidArena:
    case assets::MissionWorldSpatialLineTraceStatus::invalidWorldRoom:
        return MissionWorldRuntimeCombinedLineTraceStatus::
            invalidDynamicMesh;
    case assets::MissionWorldSpatialLineTraceStatus::invalidInput:
        return MissionWorldRuntimeCombinedLineTraceStatus::invalidInput;
    case assets::MissionWorldSpatialLineTraceStatus::
        traversalDepthExceeded:
        return MissionWorldRuntimeCombinedLineTraceStatus::
            traversalDepthExceeded;
    }
    return MissionWorldRuntimeCombinedLineTraceStatus::
        invalidDynamicMesh;
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult failure(
    const MissionWorldRuntimeCombinedLineTraceStatus status) noexcept {
    return {
        .status = status,
        .hit = std::nullopt,
    };
}

[[nodiscard]] bool validPortalMetadata(
    const LegacyDynamicBspLineObject& object,
    const std::size_t worldRoomCount) noexcept {
    if (object.portalType < -1 || object.portalType > 1) {
        return false;
    }
    if (object.portalType == -1) {
        return !object.portalWorldRoomIndex.has_value();
    }
    return object.portalWorldRoomIndex.has_value() &&
        *object.portalWorldRoomIndex < worldRoomCount;
}

[[nodiscard]] float composeLegacyFraction(
    const float outer,
    const float inner) noexcept {
    // The native x87 expression remains unspilled until the final binary32
    // field store. Exact binary32 inputs fit this deterministic binary64
    // staging; controlled x87 traces are still required for bitwise parity.
    return static_cast<float>(
        static_cast<double>(outer) +
        (1.0 - static_cast<double>(outer)) *
            static_cast<double>(inner));
}

[[nodiscard]] Vec3 interpolate(
    const Vec3& start,
    const Vec3& end,
    const float fraction) noexcept {
    return {
        static_cast<float>(
            static_cast<double>(start.x) +
            static_cast<double>(fraction) *
                (static_cast<double>(end.x) - start.x)),
        static_cast<float>(
            static_cast<double>(start.y) +
            static_cast<double>(fraction) *
                (static_cast<double>(end.y) - start.y)),
        static_cast<float>(
            static_cast<double>(start.z) +
            static_cast<double>(fraction) *
                (static_cast<double>(end.z) - start.z)),
    };
}

} // namespace

bool LegacyDynamicBspMesh::complete() const noexcept {
    return issues.empty() &&
        localArena.complete() &&
        polygonMaterialReferences.size() ==
            localArena.polygons.size() &&
        std::isfinite(localBoundingRadius) &&
        localBoundingRadius >= 0.0F;
}

LegacyDynamicBspMesh buildLegacyDynamicBsp(
    const ConvertedMeshGeometry& geometry,
    const std::span<const LegacyDynamicBspMaterialBinding>
        materialBindings,
    const LegacyDynamicBspBuildLimits& limits) {
    LegacyDynamicBspMesh result;
    try {
        if (limits.maximumDepth == 0U ||
            limits.maximumDepth >
                assets::kMissionWorldSpatialMaximumTraceDepth ||
            geometry.vertices.size() >
                limits.maximumVertices ||
            geometry.triangles.size() >
                limits.maximumTriangles ||
            materialBindings.size() >
                limits.maximumMaterialBindings) {
            addIssue(
                result,
                LegacyDynamicBspBuildIssueKind::limitExceeded);
            return result;
        }

        std::vector<LegacyDynamicBspMaterialBinding> sortedBindings(
            materialBindings.begin(), materialBindings.end());
        std::sort(
            sortedBindings.begin(),
            sortedBindings.end(),
            [](const LegacyDynamicBspMaterialBinding& left,
               const LegacyDynamicBspMaterialBinding& right) {
                return left.sourceReference < right.sourceReference;
            });
        for (std::size_t index = 1U;
             index < sortedBindings.size();
             ++index) {
            if (sortedBindings[index - 1U].sourceReference ==
                sortedBindings[index].sourceReference) {
                addIssue(
                    result,
                    LegacyDynamicBspBuildIssueKind::
                        duplicateMaterialBinding,
                    std::nullopt,
                    sortedBindings[index].sourceReference);
                return result;
            }
        }

        double maximumRadiusSquared = 0.0;
        for (const auto& vertex : geometry.vertices) {
            if (!finite(vertex.position)) {
                addIssue(
                    result,
                    LegacyDynamicBspBuildIssueKind::invalidGeometry);
                return result;
            }
            const double radiusSquared =
                dot(vertex.position, vertex.position);
            if (!std::isfinite(radiusSquared) ||
                radiusSquared < 0.0) {
                addIssue(
                    result,
                    LegacyDynamicBspBuildIssueKind::invalidGeometry);
                return result;
            }
            maximumRadiusSquared =
                std::max(maximumRadiusSquared, radiusSquared);
        }
        result.localBoundingRadius =
            static_cast<float>(std::sqrt(maximumRadiusSquared));
        if (!std::isfinite(result.localBoundingRadius)) {
            addIssue(
                result,
                LegacyDynamicBspBuildIssueKind::invalidGeometry);
            return result;
        }

        BuildBudget budget;
        std::vector<WorkingPolygon> initial;
        initial.reserve(geometry.triangles.size());
        for (std::size_t reverseIndex =
                 geometry.triangles.size();
             reverseIndex > 0U;
             --reverseIndex) {
            const auto triangleIndex = reverseIndex - 1U;
            const auto& triangle =
                geometry.triangles[triangleIndex];
            const auto first =
                static_cast<std::size_t>(
                    triangle.vertexIndices[0]);
            const auto second =
                static_cast<std::size_t>(
                    triangle.vertexIndices[1]);
            const auto third =
                static_cast<std::size_t>(
                    triangle.vertexIndices[2]);
            if (first >= geometry.vertices.size() ||
                second >= geometry.vertices.size() ||
                third >= geometry.vertices.size() ||
                triangleIndex >
                    std::numeric_limits<std::uint32_t>::max()) {
                addIssue(
                    result,
                    LegacyDynamicBspBuildIssueKind::invalidGeometry,
                    triangleIndex,
                    triangle.materialReference);
                return result;
            }
            const auto point0 =
                geometry.vertices[first].position;
            const auto point1 =
                geometry.vertices[second].position;
            const auto point2 =
                geometry.vertices[third].position;
            const auto edge01 = subtract(point1, point0);
            const auto edge12 = subtract(point2, point1);
            const auto faceCross = cross(edge01, edge12);
            const auto faceNormal = normalized(faceCross);
            if (!faceNormal.has_value() ||
                !consumeWorkingPolygon(budget, 3U, limits)) {
                addIssue(
                    result,
                    faceNormal.has_value()
                        ? LegacyDynamicBspBuildIssueKind::limitExceeded
                        : LegacyDynamicBspBuildIssueKind::invalidGeometry,
                    triangleIndex,
                    triangle.materialReference);
                return result;
            }
            initial.push_back({
                .seed = {
                    .faceCross = faceCross,
                    .faceNormal = *faceNormal,
                    .point0 = point0,
                    .edge01 = edge01,
                    .edge12 = edge12,
                    .triangleIndex =
                        static_cast<std::uint32_t>(triangleIndex),
                    .materialReference =
                        triangle.materialReference,
                    .materialCollisionMode2152 = materialMode(
                        sortedBindings,
                        triangle.materialReference),
                },
                .vertices = {point0, point1, point2},
            });
        }

        result.localArena.rooms.resize(1U);
        if (!initial.empty()) {
            std::vector<BuildTask> pending;
            pending.push_back({
                .polygons = std::move(initial),
                .parentNodeIndex = std::nullopt,
                .parentSide = ChildSide::a,
                .depth = 1U,
            });
            while (!pending.empty()) {
                auto task = std::move(pending.back());
                pending.pop_back();
                if (task.depth > limits.maximumDepth ||
                    result.localArena.nodes.size() >=
                        limits.maximumNodes) {
                    throw LegacyDynamicBspBuildIssueKind::limitExceeded;
                }

                const auto splitterIndex =
                    chooseSplitter(task.polygons);
                const auto splitter =
                    task.polygons[splitterIndex].seed;
                const auto nodeIndex =
                    result.localArena.nodes.size();
                const auto firstPolygonIndex =
                    result.localArena.polygons.size();
                result.localArena.nodes.push_back({
                    .childAIndex = std::nullopt,
                    .childBIndex = std::nullopt,
                    .splitNormal =
                        sourceVector(splitter.faceNormal),
                    .pointOnPlane =
                        sourceVector(splitter.point0),
                    .firstPolygonIndex = firstPolygonIndex,
                    .polygonCount = 0U,
                });
                if (task.parentNodeIndex.has_value()) {
                    if (*task.parentNodeIndex >= nodeIndex) {
                        throw LegacyDynamicBspBuildIssueKind::
                            invalidGeometry;
                    }
                    auto& parent =
                        result.localArena.nodes[
                            *task.parentNodeIndex];
                    if (task.parentSide == ChildSide::a) {
                        parent.childAIndex = nodeIndex;
                    }
                    else {
                        parent.childBIndex = nodeIndex;
                    }
                }

                std::vector<PolygonSeed> coplanar;
                std::vector<WorkingPolygon> negative;
                std::vector<WorkingPolygon> positive;
                coplanar.reserve(task.polygons.size());
                negative.reserve(task.polygons.size());
                positive.reserve(task.polygons.size());
                for (std::size_t polygonIndex = 0U;
                     polygonIndex < task.polygons.size();
                     ++polygonIndex) {
                    if (polygonIndex == splitterIndex) {
                        continue;
                    }
                    auto split = splitPolygon(
                        task.polygons[polygonIndex],
                        splitter,
                        budget,
                        limits);
                    if (split.coplanar) {
                        coplanar.push_back(
                            task.polygons[polygonIndex].seed);
                        continue;
                    }
                    if (split.negative.has_value()) {
                        negative.push_back(
                            std::move(*split.negative));
                    }
                    if (split.positive.has_value()) {
                        positive.push_back(
                            std::move(*split.positive));
                    }
                }

                // AddToNode prepends every record. The splitter is inserted
                // first, then coplanars in input-list order.
                for (auto iterator = coplanar.rbegin();
                     iterator != coplanar.rend();
                     ++iterator) {
                    if (!appendNodePolygon(
                            result, *iterator, limits)) {
                        throw LegacyDynamicBspBuildIssueKind::
                            limitExceeded;
                    }
                }
                if (!appendNodePolygon(
                        result, splitter, limits)) {
                    throw LegacyDynamicBspBuildIssueKind::
                        limitExceeded;
                }
                result.localArena.nodes[nodeIndex].polygonCount =
                    result.localArena.polygons.size() -
                    firstPolygonIndex;

                // AddToList prepends split fragments.
                std::reverse(negative.begin(), negative.end());
                std::reverse(positive.begin(), positive.end());
                const auto childDepth = task.depth + 1U;
                if (!positive.empty()) {
                    pending.push_back({
                        .polygons = std::move(positive),
                        .parentNodeIndex = nodeIndex,
                        .parentSide = ChildSide::b,
                        .depth = childDepth,
                    });
                }
                if (!negative.empty()) {
                    pending.push_back({
                        .polygons = std::move(negative),
                        .parentNodeIndex = nodeIndex,
                        .parentSide = ChildSide::a,
                        .depth = childDepth,
                    });
                }
            }

            result.localArena.treeReferences.push_back(0U);
            result.localArena.trees.push_back({
                .kind = assets::CcfBspTreeKind::staticTree,
                .sourceIndex = 0U,
                .physicalRoomIndex = 0U,
                .worldRoomIndex = 0U,
                .rootNodeIndex = 0U,
                .firstNodeIndex = 0U,
                .nodeCount = result.localArena.nodes.size(),
            });
            result.localArena.rooms[0].staticTreeCount = 1U;
        }

        std::uint64_t retainedBytes = 0U;
        if (!checkedBytes(
                retainedBytes,
                result.localArena.rooms.size(),
                sizeof(assets::MissionWorldSpatialRoom)) ||
            !checkedBytes(
                retainedBytes,
                result.localArena.treeReferences.size(),
                sizeof(std::size_t)) ||
            !checkedBytes(
                retainedBytes,
                result.localArena.trees.size(),
                sizeof(assets::MissionWorldSpatialTree)) ||
            !checkedBytes(
                retainedBytes,
                result.localArena.nodes.size(),
                sizeof(assets::MissionWorldSpatialNode)) ||
            !checkedBytes(
                retainedBytes,
                result.localArena.polygons.size(),
                sizeof(assets::MissionWorldSpatialPolygon)) ||
            !checkedBytes(
                retainedBytes,
                result.polygonMaterialReferences.size(),
                sizeof(std::uint32_t))) {
            addIssue(
                result,
                LegacyDynamicBspBuildIssueKind::integerOverflow);
            result.localArena = {};
            result.polygonMaterialReferences.clear();
            return result;
        }
        if (retainedBytes > limits.maximumRetainedBytes) {
            addIssue(
                result,
                LegacyDynamicBspBuildIssueKind::
                    retainedByteLimitExceeded);
            result.localArena = {};
            result.polygonMaterialReferences.clear();
            return result;
        }
        result.localArena.retainedPayloadBytes =
            retainedBytes -
            static_cast<std::uint64_t>(
                result.polygonMaterialReferences.size()) *
                sizeof(std::uint32_t);
        result.retainedPayloadBytes = retainedBytes;
        return result;
    }
    catch (const LegacyDynamicBspBuildIssueKind kind) {
        addIssue(result, kind);
    }
    catch (const std::bad_alloc&) {
        addIssue(
            result,
            LegacyDynamicBspBuildIssueKind::allocationFailure);
    }
    result.localArena = {};
    result.polygonMaterialReferences.clear();
    result.localBoundingRadius = 0.0F;
    result.retainedPayloadBytes = 0U;
    return result;
}

MissionWorldRuntimeCombinedLineTraceResult
traceMissionWorldRuntimeCombinedLine(
    const assets::MissionWorldSpatialArena& staticArena,
    const BasisTransform& runtimeBasis,
    const std::size_t worldRoomIndex,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const std::span<const LegacyDynamicBspMesh> dynamicMeshes,
    const std::span<const LegacyDynamicBspLineObject> dynamicObjects,
    const MissionWorldRuntimeCombinedLineTraceOptions& options) noexcept {
    if (!finite(runtimeStart) || !finite(runtimeEnd)) {
        return failure(
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput);
    }
    if (dynamicObjects.size() >
        options.maximumDynamicObjects) {
        return failure(
            MissionWorldRuntimeCombinedLineTraceStatus::
                dynamicObjectLimitExceeded);
    }

    const auto staticResult =
        traceMissionWorldRuntimeSpatialLine(
            staticArena,
            runtimeBasis,
            worldRoomIndex,
            assets::CcfBspTreeKind::staticTree,
            runtimeStart,
            runtimeEnd,
            options.staticTrace);
    std::optional<MissionWorldRuntimeCombinedLineHit> nearest;
    float nearestFraction =
        std::numeric_limits<float>::max();
    if (staticResult.status ==
            MissionWorldRuntimeSpatialLineTraceStatus::hit ||
        staticResult.status ==
            MissionWorldRuntimeSpatialLineTraceStatus::
                outOfSegmentHit) {
        if (!staticResult.hit.has_value() ||
            staticResult.hit->polygonIndex >=
                staticArena.polygons.size()) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidArena);
        }
        const auto& hit = *staticResult.hit;
        const auto& polygon =
            staticArena.polygons[hit.polygonIndex];
        nearestFraction = hit.legacyFraction;
        nearest = MissionWorldRuntimeCombinedLineHit{
            .kind =
                MissionWorldRuntimeCombinedLineHitKind::staticRoom,
            .legacyFraction = hit.legacyFraction,
            .runtimePoint = hit.runtimePoint,
            .runtimePlaneNormal = hit.runtimePlaneNormal,
            .materialCollisionMode2152 =
                polygon.materialCollisionMode2152,
            .ownerWorldRoomIndex =
                hit.ownerWorldRoomIndex,
            .treeIndex = hit.treeIndex,
            .nodeIndex = hit.nodeIndex,
            .polygonIndex = hit.polygonIndex,
            .portalWorldRoomIndex =
                hit.portalWorldRoomIndex,
            .dynamicObjectIndex = std::nullopt,
            .dynamicMeshIndex = std::nullopt,
            .sourceTriangleIndex = std::nullopt,
            .sourceMaterialReference = std::nullopt,
            .actorObjectId = 0U,
            .portalType = -1,
            .portalObjectVisible = false,
            .reverseFacing = hit.reverseFacing,
            .withinRequestedSegment =
                hit.withinRequestedSegment,
        };
    }
    else if (staticResult.status !=
             MissionWorldRuntimeSpatialLineTraceStatus::noHit) {
        return failure(mapStaticStatus(staticResult.status));
    }

    const auto direction =
        subtract(runtimeEnd, runtimeStart);
    const Vec3 midpoint{
        static_cast<float>(
            (static_cast<double>(runtimeStart.x) +
             runtimeEnd.x) *
            0.5),
        static_cast<float>(
            (static_cast<double>(runtimeStart.y) +
             runtimeEnd.y) *
            0.5),
        static_cast<float>(
            (static_cast<double>(runtimeStart.z) +
             runtimeEnd.z) *
            0.5),
    };
    const float halfLength = static_cast<float>(
        std::sqrt(dot(direction, direction)) * 0.5);
    if (!finite(direction) || !finite(midpoint) ||
        !std::isfinite(halfLength)) {
        return failure(
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput);
    }

    for (std::size_t objectIndex = 0U;
         objectIndex < dynamicObjects.size();
         ++objectIndex) {
        const auto& object = dynamicObjects[objectIndex];
        if (!object.active) {
            continue;
        }
        if (!validPortalMetadata(
                object, staticArena.rooms.size())) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject);
        }
        if (object.meshIndex >= dynamicMeshes.size()) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject);
        }
        const auto& mesh = dynamicMeshes[object.meshIndex];
        if (!mesh.complete()) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicMesh);
        }
        if (!orthonormal(object.objectLocalToRuntime) ||
            !finite(object.runtimeTranslation)) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject);
        }

        const auto midpointDelta =
            subtract(midpoint, object.runtimeTranslation);
        const double broadRadius =
            static_cast<double>(mesh.localBoundingRadius) +
            halfLength;
        if (!std::isfinite(broadRadius) ||
            dot(midpointDelta, midpointDelta) >
                broadRadius * broadRadius) {
            continue;
        }

        const auto runtimeToLocal =
            transpose(object.objectLocalToRuntime);
        const auto localStart = applyRuntimeColumn(
            runtimeToLocal,
            subtract(runtimeStart, object.runtimeTranslation));
        const auto localEnd = applyRuntimeColumn(
            runtimeToLocal,
            subtract(runtimeEnd, object.runtimeTranslation));
        if (!finite(localStart) || !finite(localEnd)) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject);
        }
        const auto localResult =
            assets::traceMissionWorldSpatialLine(
                mesh.localArena,
                0U,
                assets::CcfBspTreeKind::staticTree,
                sourceVector(localStart),
                sourceVector(localEnd),
                options.staticTrace);
        if (localResult.status ==
                assets::MissionWorldSpatialLineTraceStatus::noHit) {
            continue;
        }
        if (localResult.status !=
                assets::MissionWorldSpatialLineTraceStatus::hit ||
            !localResult.hit.has_value()) {
            return failure(mapLocalStatus(localResult.status));
        }
        const auto& localHit = *localResult.hit;
        if (!std::isfinite(localHit.fraction) ||
            localHit.polygonIndex >=
                mesh.localArena.polygons.size() ||
            localHit.polygonIndex >=
                mesh.polygonMaterialReferences.size()) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicMesh);
        }
        if (!(localHit.fraction < nearestFraction)) {
            continue;
        }

        const auto localNormal =
            runtimeVector(localHit.normal);
        const auto transformedNormal = normalized(
            applyRuntimeColumn(
                object.objectLocalToRuntime,
                localNormal));
        const auto localPoint =
            runtimeVector(localHit.point);
        const auto runtimePoint = add(
            applyRuntimeColumn(
                object.objectLocalToRuntime,
                localPoint),
            object.runtimeTranslation);
        if (!transformedNormal.has_value() ||
            !finite(runtimePoint)) {
            return failure(
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject);
        }
        const auto& polygon =
            mesh.localArena.polygons[
                localHit.polygonIndex];
        nearestFraction = localHit.fraction;
        nearest = MissionWorldRuntimeCombinedLineHit{
            .kind =
                MissionWorldRuntimeCombinedLineHitKind::
                    dynamicObject,
            .legacyFraction = localHit.fraction,
            .runtimePoint = runtimePoint,
            .runtimePlaneNormal = *transformedNormal,
            .materialCollisionMode2152 =
                polygon.materialCollisionMode2152,
            .ownerWorldRoomIndex = std::nullopt,
            .treeIndex = std::nullopt,
            .nodeIndex = std::nullopt,
            .polygonIndex = std::nullopt,
            .portalWorldRoomIndex =
                object.portalWorldRoomIndex,
            .dynamicObjectIndex = objectIndex,
            .dynamicMeshIndex = object.meshIndex,
            .sourceTriangleIndex = polygon.polygonIndex,
            .sourceMaterialReference =
                mesh.polygonMaterialReferences[
                    localHit.polygonIndex],
            .actorObjectId = object.actorObjectId,
            .portalType = object.portalType,
            .portalObjectVisible =
                object.portalObjectVisible,
            .reverseFacing = localHit.reverseFacing,
            .withinRequestedSegment =
                localHit.fraction >= 0.0F &&
                localHit.fraction <= 1.0F,
        };
    }

    if (!nearest.has_value()) {
        return {
            .status =
                MissionWorldRuntimeCombinedLineTraceStatus::noHit,
            .hit = std::nullopt,
        };
    }
    // The point is retained from the exact static/local trace path. This
    // interpolation is evaluated only as a finite sanity check because native
    // projectile callers independently interpolate the original segment.
    const auto checkedPoint = interpolate(
        runtimeStart, runtimeEnd, nearest->legacyFraction);
    if (!finite(checkedPoint)) {
        return failure(
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput);
    }
    if (!(nearest->legacyFraction >= 0.0F) ||
        !(nearest->legacyFraction <= 1.0F)) {
        nearest->withinRequestedSegment = false;
        return {
            .status =
                MissionWorldRuntimeCombinedLineTraceStatus::
                    outOfSegmentHit,
            .hit = nearest,
        };
    }
    return {
        .status =
            MissionWorldRuntimeCombinedLineTraceStatus::hit,
        .hit = nearest,
    };
}

MissionWorldRuntimeCombinedLineTraceResult
traceMissionWorldRuntimeCombinedPortalLine(
    const assets::MissionWorldSpatialArena& staticArena,
    const BasisTransform& runtimeBasis,
    const std::size_t worldRoomIndex,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const std::span<const LegacyDynamicBspMesh> dynamicMeshes,
    const std::span<const LegacyDynamicBspLineObject> dynamicObjects,
    const std::span<const LegacyDynamicBspRoomObjectRange>
        roomObjectRanges,
    const MissionWorldRuntimeCombinedPortalLineTraceOptions& options)
    noexcept {
    if (options.maximumPortalTransitions >
            assets::kMissionWorldSpatialMaximumPortalTransitions ||
        roomObjectRanges.size() != staticArena.rooms.size()) {
        return failure(
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput);
    }

    std::size_t currentRoom = worldRoomIndex;
    Vec3 currentStart = runtimeStart;
    std::array<
        float,
        assets::kMissionWorldSpatialMaximumPortalTransitions>
        portalFractions{};
    std::size_t transitionCount = 0U;

    while (true) {
        if (currentRoom >= roomObjectRanges.size()) {
            return {
                .status =
                    MissionWorldRuntimeCombinedLineTraceStatus::
                        invalidWorldRoom,
                .hit = std::nullopt,
                .portalTransitionCount = transitionCount,
            };
        }
        const auto& range = roomObjectRanges[currentRoom];
        if (range.firstObjectIndex > dynamicObjects.size() ||
            range.objectCount >
                dynamicObjects.size() - range.firstObjectIndex) {
            return {
                .status =
                    MissionWorldRuntimeCombinedLineTraceStatus::
                        invalidDynamicObject,
                .hit = std::nullopt,
                .portalTransitionCount = transitionCount,
            };
        }

        const auto roomObjects = dynamicObjects.subspan(
            range.firstObjectIndex, range.objectCount);
        auto current = traceMissionWorldRuntimeCombinedLine(
            staticArena,
            runtimeBasis,
            currentRoom,
            currentStart,
            runtimeEnd,
            dynamicMeshes,
            roomObjects,
            options.combinedLine);
        current.portalTransitionCount = transitionCount;
        if (current.hit.has_value() &&
            current.hit->dynamicObjectIndex.has_value()) {
            const auto localIndex =
                *current.hit->dynamicObjectIndex;
            if (localIndex >= range.objectCount ||
                range.firstObjectIndex >
                    std::numeric_limits<std::size_t>::max() -
                        localIndex) {
                return {
                    .status =
                        MissionWorldRuntimeCombinedLineTraceStatus::
                            invalidDynamicObject,
                    .hit = std::nullopt,
                    .portalTransitionCount = transitionCount,
                };
            }
            current.hit->dynamicObjectIndex =
                range.firstObjectIndex + localIndex;
        }

        if (current.status ==
            MissionWorldRuntimeCombinedLineTraceStatus::noHit) {
            // Native recursive GetBspCollision returns false when a
            // transparent portal has no later collision.
            return current;
        }
        if (current.status !=
                MissionWorldRuntimeCombinedLineTraceStatus::hit ||
            !current.hit.has_value()) {
            return current;
        }

        auto& hit = *current.hit;
        const bool transparentPortal =
            hit.kind ==
                MissionWorldRuntimeCombinedLineHitKind::dynamicObject &&
            hit.portalType == 0 &&
            hit.portalObjectVisible;
        if (!transparentPortal) {
            if (transitionCount != 0U) {
                float composedFraction = hit.legacyFraction;
                // Native recursion composes while unwinding, so deeper
                // fractions round to binary32 before the outer expression.
                for (std::size_t index = transitionCount;
                     index > 0U;
                     --index) {
                    composedFraction = composeLegacyFraction(
                        portalFractions[index - 1U],
                        composedFraction);
                }
                hit.legacyFraction = composedFraction;
                hit.withinRequestedSegment =
                    hit.legacyFraction >= 0.0F &&
                    hit.legacyFraction <= 1.0F;
                if (!std::isfinite(hit.legacyFraction) ||
                    !hit.withinRequestedSegment) {
                    current.status =
                        MissionWorldRuntimeCombinedLineTraceStatus::
                            outOfSegmentHit;
                }
            }
            return current;
        }

        if (!hit.portalWorldRoomIndex.has_value() ||
            *hit.portalWorldRoomIndex >= staticArena.rooms.size()) {
            current.status =
                MissionWorldRuntimeCombinedLineTraceStatus::
                    invalidDynamicObject;
            return current;
        }
        if (transitionCount >=
            options.maximumPortalTransitions) {
            current.status =
                MissionWorldRuntimeCombinedLineTraceStatus::
                    portalTransitionLimitExceeded;
            return current;
        }

        portalFractions[transitionCount] = hit.legacyFraction;
        currentStart = hit.runtimePoint;
        currentRoom = *hit.portalWorldRoomIndex;
        ++transitionCount;
    }
}

} // namespace airfix::render
