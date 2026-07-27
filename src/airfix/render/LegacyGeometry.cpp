#include "airfix/render/LegacyGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace airfix::render {
namespace {

[[nodiscard]] constexpr Vec3 cross(const Vec3 left, const Vec3 right) noexcept {
    return Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] constexpr float dot(const Vec3 left, const Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vec3 invalidVector() noexcept {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    return Vec3{invalid, invalid, invalid};
}

[[nodiscard]] Vec3 normalizeDirection(const Vec3 value) noexcept {
    const double x = value.x;
    const double y = value.y;
    const double z = value.z;
    const double length = std::hypot(x, y, z);
    if (!std::isfinite(length) || length == 0.0) {
        return invalidVector();
    }
    return Vec3{
        static_cast<float>(x / length),
        static_cast<float>(y / length),
        static_cast<float>(z / length),
    };
}

[[nodiscard]] Vec3 normalizedLegacyFaceNormal(
    const Vec3 position0,
    const Vec3 position1,
    const Vec3 position2,
    const Vec3 degenerateFallback) noexcept {
    const double ax = static_cast<double>(position2.x) - position0.x;
    const double ay = static_cast<double>(position2.y) - position0.y;
    const double az = static_cast<double>(position2.z) - position0.z;
    const double bx = static_cast<double>(position1.x) - position0.x;
    const double by = static_cast<double>(position1.y) - position0.y;
    const double bz = static_cast<double>(position1.z) - position0.z;
    const double x = ay * bz - az * by;
    const double y = az * bx - ax * bz;
    const double z = ax * by - ay * bx;
    const double length = std::hypot(x, y, z);
    if (!std::isfinite(length)) {
        return invalidVector();
    }
    if (length == 0.0) {
        return normalizeDirection(degenerateFallback);
    }
    return Vec3{
        static_cast<float>(x / length),
        static_cast<float>(y / length),
        static_cast<float>(z / length),
    };
}

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return std::ranges::all_of(value.columns, [](const Vec3 column) {
        return finite(column);
    });
}

[[noreturn]] void fail(const GeometryErrorCode code, const std::string_view message) {
    throw GeometryError(code, std::string(message));
}

void requireFinite(const Vec3 value, const std::string_view label) {
    if (!finite(value)) {
        fail(GeometryErrorCode::nonFiniteValue, std::string(label) + " contains a non-finite value");
    }
}

void requireFinite(const Mat3& value, const std::string_view label) {
    if (!finite(value)) {
        fail(GeometryErrorCode::nonFiniteValue, std::string(label) + " contains a non-finite value");
    }
}

[[nodiscard]] Vec3 fromCcf(const assets::CcfVector3& value) noexcept {
    return Vec3{value[0], value[1], value[2]};
}

[[nodiscard]] Mat3 fromCcf(const std::array<assets::CcfVector3, 3>& value) noexcept {
    return Mat3{{fromCcf(value[0]), fromCcf(value[1]), fromCcf(value[2])}};
}

[[nodiscard]] Vec3 transformPosition(
    const Mat3& sourceToRuntime,
    const float scale,
    const Vec3 source) {
    requireFinite(source, "source position");
    Vec3 converted = applyRuntimeColumn(sourceToRuntime, source);
    converted.x *= scale;
    converted.y *= scale;
    converted.z *= scale;
    requireFinite(converted, "converted position");
    return converted;
}

void requireValidBasis(const BasisTransform& basis) {
    requireFinite(basis.sourceToRuntime, "basis");
    const float basisDeterminant = determinant(basis.sourceToRuntime);
    if (!finite(basisDeterminant)) {
        fail(GeometryErrorCode::nonFiniteValue, "basis determinant is non-finite");
    }
    if (basisDeterminant == 0.0F) {
        fail(GeometryErrorCode::singularBasis, "basis is singular");
    }
    if (!finite(basis.runtimeUnitsPerSourceUnit) ||
        basis.runtimeUnitsPerSourceUnit <= 0.0F) {
        fail(GeometryErrorCode::invalidScale, "runtime scale must be positive and finite");
    }
}

void requireValidNodeTransform(
    const ConvertedNodeTransform& transform,
    const std::string_view label) {
    requireFinite(transform.linear, std::string(label) + " linear transform");
    requireFinite(transform.translation, std::string(label) + " translation");
    if (!finite(transform.rawScalar)) {
        fail(GeometryErrorCode::nonFiniteValue,
             std::string(label) + " raw scalar is non-finite");
    }
    const float linearDeterminant = determinant(transform.linear);
    if (!finite(linearDeterminant)) {
        fail(GeometryErrorCode::nonFiniteValue,
             std::string(label) + " determinant is non-finite");
    }
    if (linearDeterminant == 0.0F) {
        fail(GeometryErrorCode::singularTransform,
             std::string(label) + " linear transform is singular");
    }
}

[[nodiscard]] constexpr Vec3 subtract(const Vec3 left, const Vec3 right) noexcept {
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr Vec3 add(const Vec3 left, const Vec3 right) noexcept {
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

void rotateLegacyByXAxis(Mat3& matrix, const float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    for (auto& column : matrix.columns) {
        const float oldY = column.y;
        column.y = sine * column.z + cosine * oldY;
        column.z = cosine * column.z - sine * oldY;
    }
}

void rotateLegacyByYAxis(Mat3& matrix, const float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    for (auto& column : matrix.columns) {
        const float oldX = column.x;
        column.x = sine * column.z + cosine * oldX;
        column.z = cosine * column.z - sine * oldX;
    }
}

void rotateLegacyByZAxis(Mat3& matrix, const float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    for (auto& column : matrix.columns) {
        const float oldX = column.x;
        column.x = sine * column.y + cosine * oldX;
        column.y = cosine * column.y - sine * oldX;
    }
}

} // namespace

GeometryError::GeometryError(const GeometryErrorCode code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

GeometryErrorCode GeometryError::code() const noexcept {
    return code_;
}

Vec3 applyLegacyRow(const Mat3& rawLegacyMatrix, const Vec3& vector) noexcept {
    return Vec3{
        dot(vector, rawLegacyMatrix.columns[0]),
        dot(vector, rawLegacyMatrix.columns[1]),
        dot(vector, rawLegacyMatrix.columns[2]),
    };
}

Vec3 applyRuntimeColumn(const Mat3& matrix, const Vec3& vector) noexcept {
    return Vec3{
        matrix.columns[0].x * vector.x + matrix.columns[1].x * vector.y +
            matrix.columns[2].x * vector.z,
        matrix.columns[0].y * vector.x + matrix.columns[1].y * vector.y +
            matrix.columns[2].y * vector.z,
        matrix.columns[0].z * vector.x + matrix.columns[1].z * vector.y +
            matrix.columns[2].z * vector.z,
    };
}

Mat3 transpose(const Mat3& matrix) noexcept {
    return Mat3{{
        Vec3{matrix.columns[0].x, matrix.columns[1].x, matrix.columns[2].x},
        Vec3{matrix.columns[0].y, matrix.columns[1].y, matrix.columns[2].y},
        Vec3{matrix.columns[0].z, matrix.columns[1].z, matrix.columns[2].z},
    }};
}

Mat3 multiply(const Mat3& left, const Mat3& right) noexcept {
    return Mat3{{
        applyRuntimeColumn(left, right.columns[0]),
        applyRuntimeColumn(left, right.columns[1]),
        applyRuntimeColumn(left, right.columns[2]),
    }};
}

float determinant(const Mat3& matrix) noexcept {
    return dot(matrix.columns[0], cross(matrix.columns[1], matrix.columns[2]));
}

std::optional<Mat3> inverse(const Mat3& matrix) noexcept {
    if (!finite(matrix)) {
        return std::nullopt;
    }

    const float value = determinant(matrix);
    if (!finite(value) || value == 0.0F) {
        return std::nullopt;
    }

    const Vec3 row0 = cross(matrix.columns[1], matrix.columns[2]);
    const Vec3 row1 = cross(matrix.columns[2], matrix.columns[0]);
    const Vec3 row2 = cross(matrix.columns[0], matrix.columns[1]);
    const float reciprocal = 1.0F / value;
    const Mat3 result = transpose(Mat3{{
        Vec3{row0.x * reciprocal, row0.y * reciprocal, row0.z * reciprocal},
        Vec3{row1.x * reciprocal, row1.y * reciprocal, row1.z * reciprocal},
        Vec3{row2.x * reciprocal, row2.y * reciprocal, row2.z * reciprocal},
    }});
    if (!finite(result)) {
        return std::nullopt;
    }
    return result;
}

bool reversesOrientation(const Mat3& sourceToRuntime) noexcept {
    return determinant(sourceToRuntime) < 0.0F;
}

ConvertedNodeTransform convertLegacyTransform(
    const assets::CcfSrtMetadata& source,
    const BasisTransform& basis) {
    requireValidBasis(basis);
    if (!finite(source.rawScalar)) {
        fail(GeometryErrorCode::nonFiniteValue, "raw scalar is non-finite");
    }

    ConvertedNodeTransform result{
        .linear = toRuntimeColumnMatrix(fromCcf(source.orientation), basis.sourceToRuntime),
        .translation = transformPosition(
            basis.sourceToRuntime,
            basis.runtimeUnitsPerSourceUnit,
            fromCcf(source.position)),
        .rawScalar = source.rawScalar,
    };
    requireValidNodeTransform(result, "converted node");
    return result;
}

ConvertedNodeTransform convertLegacyTransform(
    const assets::CcfPlacedSrtMetadata& source,
    const BasisTransform& basis) {
    const auto* orientation =
        std::get_if<std::array<assets::CcfVector3, 3>>(
            &source.orientation);
    if (orientation == nullptr) {
        fail(
            GeometryErrorCode::unsupportedOrientation,
            "placed F040 orientation semantics are not established");
    }
    return convertLegacyTransform(
        assets::CcfSrtMetadata{
            .position = source.position,
            .rawScalar = source.rawScalar,
            .orientation = *orientation,
        },
        basis);
}

ConvertedNodeTransform convertLegacyAxisRotationWorldPose(
    const Vec3& sourceWorldPosition,
    const std::array<float, 3>& axisRotationRadians,
    const BasisTransform& basis) {
    requireValidBasis(basis);
    requireFinite(sourceWorldPosition, "legacy world position");
    for (const auto radians : axisRotationRadians) {
        if (!finite(radians)) {
            fail(GeometryErrorCode::nonFiniteValue,
                 "legacy axis rotation contains a non-finite value");
        }
    }

    const auto result = tryConvertLegacyAxisRotationWorldPose(
        sourceWorldPosition, axisRotationRadians, basis);
    if (!result.has_value()) {
        fail(GeometryErrorCode::singularTransform,
             "legacy axis-rotation world pose is invalid");
    }
    return *result;
}

std::optional<ConvertedNodeTransform>
tryConvertLegacyAxisRotationWorldPose(
    const Vec3& sourceWorldPosition,
    const std::array<float, 3>& axisRotationRadians,
    const BasisTransform& basis) noexcept {
    if (!finite(sourceWorldPosition) ||
        !finite(basis.sourceToRuntime) ||
        !finite(basis.runtimeUnitsPerSourceUnit) ||
        basis.runtimeUnitsPerSourceUnit <= 0.0F) {
        return std::nullopt;
    }
    for (const auto radians : axisRotationRadians) {
        if (!finite(radians)) {
            return std::nullopt;
        }
    }
    const auto basisInverse = inverse(basis.sourceToRuntime);
    if (!basisInverse.has_value()) {
        return std::nullopt;
    }

    Mat3 rawLegacyMatrix;
    rotateLegacyByZAxis(rawLegacyMatrix, axisRotationRadians[2]);
    rotateLegacyByXAxis(rawLegacyMatrix, axisRotationRadians[0]);
    rotateLegacyByYAxis(rawLegacyMatrix, axisRotationRadians[1]);
    if (!finite(rawLegacyMatrix)) {
        return std::nullopt;
    }

    Vec3 translation =
        applyRuntimeColumn(basis.sourceToRuntime, sourceWorldPosition);
    translation.x *= basis.runtimeUnitsPerSourceUnit;
    translation.y *= basis.runtimeUnitsPerSourceUnit;
    translation.z *= basis.runtimeUnitsPerSourceUnit;
    const Mat3 runtimeRotation = multiply(
        multiply(basis.sourceToRuntime, transpose(rawLegacyMatrix)),
        *basisInverse);
    if (!finite(translation) || !finite(runtimeRotation)) {
        return std::nullopt;
    }
    const float runtimeDeterminant = determinant(runtimeRotation);
    if (!finite(runtimeDeterminant) || runtimeDeterminant == 0.0F) {
        return std::nullopt;
    }

    return ConvertedNodeTransform{
        .linear = runtimeRotation,
        .translation = translation,
        .rawScalar = 1.0F,
    };
}

ConvertedNodeTransform deriveLocalTransform(
    const ConvertedNodeTransform& parentWorld,
    const ConvertedNodeTransform& childWorld) {
    requireValidNodeTransform(parentWorld, "parent world");
    requireValidNodeTransform(childWorld, "child world");
    const auto parentInverse = inverse(parentWorld.linear);
    if (!parentInverse.has_value()) {
        fail(GeometryErrorCode::singularTransform,
             "parent world linear transform is singular");
    }

    ConvertedNodeTransform result{
        .linear = multiply(*parentInverse, childWorld.linear),
        .translation = applyRuntimeColumn(
            *parentInverse,
            subtract(childWorld.translation, parentWorld.translation)),
        .rawScalar = childWorld.rawScalar,
    };
    requireValidNodeTransform(result, "derived local");
    return result;
}

ConvertedNodeTransform composeNodeTransforms(
    const ConvertedNodeTransform& parentWorld,
    const ConvertedNodeTransform& local) {
    requireValidNodeTransform(parentWorld, "parent world");
    requireValidNodeTransform(local, "local");

    ConvertedNodeTransform result{
        .linear = multiply(parentWorld.linear, local.linear),
        .translation = add(
            applyRuntimeColumn(parentWorld.linear, local.translation),
            parentWorld.translation),
        .rawScalar = local.rawScalar,
    };
    requireValidNodeTransform(result, "composed world");
    return result;
}

std::optional<ConvertedNodeTransform>
tryComposeNodeTransforms(
    const ConvertedNodeTransform& parentWorld,
    const ConvertedNodeTransform& local) noexcept {
    const auto validTransform =
        [](const ConvertedNodeTransform& transform) noexcept {
            if (!finite(transform.linear) ||
                !finite(transform.translation) ||
                !finite(transform.rawScalar)) {
                return false;
            }
            const float linearDeterminant =
                determinant(transform.linear);
            return finite(linearDeterminant) &&
                linearDeterminant != 0.0F;
        };
    if (!validTransform(parentWorld) || !validTransform(local)) {
        return std::nullopt;
    }

    ConvertedNodeTransform result{
        .linear = multiply(parentWorld.linear, local.linear),
        .translation = add(
            applyRuntimeColumn(parentWorld.linear, local.translation),
            parentWorld.translation),
        .rawScalar = local.rawScalar,
    };
    if (!validTransform(result)) {
        return std::nullopt;
    }
    return result;
}

Mat3 toRuntimeColumnMatrix(
    const Mat3& rawLegacyMatrix,
    const Mat3& sourceToRuntime) {
    requireFinite(rawLegacyMatrix, "legacy orientation");
    requireFinite(sourceToRuntime, "basis");
    const auto basisInverse = inverse(sourceToRuntime);
    if (!basisInverse.has_value()) {
        fail(GeometryErrorCode::singularBasis, "basis is singular");
    }

    const Mat3 converted = multiply(
        multiply(sourceToRuntime, transpose(rawLegacyMatrix)),
        *basisInverse);
    requireFinite(converted, "converted orientation");
    return converted;
}

Vec3 legacyFaceNormal(
    const Vec3& position0,
    const Vec3& position1,
    const Vec3& position2) noexcept {
    return normalizedLegacyFaceNormal(
        position0, position1, position2, Vec3{0.0F, 1.0F, 0.0F});
}

ConvertedMeshGeometry convertLegacyGeometry(
    const assets::CcfMeshMetadata& source,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const GeometryLimits& limits) {
    requireValidBasis(basis);
    const float basisDeterminant = determinant(basis.sourceToRuntime);
    if (source.vertices.size() > limits.maxVertices ||
        source.triangles.size() > limits.maxTriangles) {
        fail(GeometryErrorCode::limitExceeded, "mesh exceeds conversion limits");
    }
    if (!finite(source.scalar)) {
        fail(GeometryErrorCode::nonFiniteValue, "raw scalar is non-finite");
    }

    ConvertedMeshGeometry result;
    result.name = source.name;
    result.prefix = source.prefix;
    result.reference = source.reference;
    result.selectionFlagA = source.selectionFlagA;
    result.selectionFlagB = source.selectionFlagB;
    result.linkReference = source.linkReference;
    result.translation = transformPosition(
        basis.sourceToRuntime,
        basis.runtimeUnitsPerSourceUnit,
        fromCcf(source.position));
    result.rawScalar = source.scalar;
    result.orientation = toRuntimeColumnMatrix(
        fromCcf(source.orientation),
        basis.sourceToRuntime);
    result.windingReversed = basisDeterminant < 0.0F;
    const auto basisInverse = inverse(basis.sourceToRuntime);
    if (!basisInverse.has_value()) {
        fail(GeometryErrorCode::singularBasis, "basis is singular");
    }
    const Vec3 degenerateNormalFallback = normalizeDirection(applyRuntimeColumn(
        transpose(*basisInverse), Vec3{0.0F, 1.0F, 0.0F}));
    requireFinite(degenerateNormalFallback, "degenerate normal fallback");

    result.vertices.reserve(source.vertices.size());
    for (const auto& vertex : source.vertices) {
        result.vertices.push_back(ConvertedMeshVertex{transformPosition(
            basis.sourceToRuntime,
            basis.runtimeUnitsPerSourceUnit,
            fromCcf(vertex.position))});
    }

    result.triangles.reserve(source.triangles.size());
    for (const auto& triangle : source.triangles) {
        auto indices = triangle.vertexIndices;
        for (const std::uint32_t index : indices) {
            if (index >= result.vertices.size()) {
                fail(GeometryErrorCode::vertexIndexOutOfRange,
                     "triangle vertex index is out of range");
            }
        }

        auto textureCoordinates = triangle.textureCoordinates;
        if (textureCoordinates.has_value()) {
            for (const float coordinate : *textureCoordinates) {
                if (!finite(coordinate)) {
                    fail(GeometryErrorCode::nonFiniteValue,
                         "texture coordinate is non-finite");
                }
            }
            if (uvPolicy == UvPolicy::flipVExplicit) {
                (*textureCoordinates)[1] = 1.0F - (*textureCoordinates)[1];
                (*textureCoordinates)[3] = 1.0F - (*textureCoordinates)[3];
                (*textureCoordinates)[5] = 1.0F - (*textureCoordinates)[5];
            }
        }

        if (result.windingReversed) {
            std::swap(indices[1], indices[2]);
            if (textureCoordinates.has_value()) {
                std::swap((*textureCoordinates)[2], (*textureCoordinates)[4]);
                std::swap((*textureCoordinates)[3], (*textureCoordinates)[5]);
            }
        }

        const Vec3 normal = normalizedLegacyFaceNormal(
            result.vertices[indices[0]].position,
            result.vertices[indices[1]].position,
            result.vertices[indices[2]].position,
            degenerateNormalFallback);
        requireFinite(normal, "face normal");
        result.triangles.push_back(ConvertedMeshTriangle{
            indices,
            triangle.materialReference,
            textureCoordinates,
            normal,
        });
    }

    return result;
}

} // namespace airfix::render
