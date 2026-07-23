#pragma once

#include "airfix/assets/LegacyFormats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::render {

struct Vec3 {
    float x{};
    float y{};
    float z{};

    [[nodiscard]] friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

// Column-major mathematical representation. This says nothing about an API's
// in-memory matrix layout.
struct Mat3 {
    std::array<Vec3, 3> columns{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    };

    [[nodiscard]] friend constexpr bool operator==(const Mat3&, const Mat3&) = default;
};

enum class UvPolicy : std::uint8_t {
    preserveRaw,
    flipVExplicit,
};

struct BasisTransform {
    Mat3 sourceToRuntime{};
    float runtimeUnitsPerSourceUnit{1.0F};
};

struct GeometryLimits {
    std::size_t maxVertices{1'000'000U};
    std::size_t maxTriangles{1'000'000U};
};

enum class GeometryErrorCode : std::uint8_t {
    nonFiniteValue,
    singularBasis,
    singularTransform,
    unsupportedOrientation,
    invalidScale,
    limitExceeded,
    vertexIndexOutOfRange,
};

class GeometryError final : public std::runtime_error {
public:
    GeometryError(GeometryErrorCode code, const std::string& message);

    [[nodiscard]] GeometryErrorCode code() const noexcept;

private:
    GeometryErrorCode code_;
};

struct ConvertedMeshVertex {
    Vec3 position{};
};

struct ConvertedMeshTriangle {
    std::array<std::uint32_t, 3> vertexIndices{};
    std::uint32_t materialReference{};
    std::optional<std::array<float, 6>> textureCoordinates;
    Vec3 faceNormal{};
};

struct ConvertedMeshGeometry {
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    std::uint8_t selectionFlagA{};
    std::uint8_t selectionFlagB{};
    std::uint32_t linkReference{};
    Vec3 translation{};
    float rawScalar{};
    Mat3 orientation{};
    bool windingReversed{};
    std::vector<ConvertedMeshVertex> vertices;
    std::vector<ConvertedMeshTriangle> triangles;
};

struct ConvertedNodeTransform {
    Mat3 linear{};
    Vec3 translation{};
    float rawScalar{};
};

// Legacy source space is right-handed: +X right, +Y up, +Z forward.
// Raw CCF orientation columns were consumed by legacy row vectors.
[[nodiscard]] Vec3 applyLegacyRow(const Mat3& rawLegacyMatrix, const Vec3& vector) noexcept;
[[nodiscard]] Vec3 applyRuntimeColumn(const Mat3& matrix, const Vec3& vector) noexcept;
[[nodiscard]] Mat3 transpose(const Mat3& matrix) noexcept;
[[nodiscard]] Mat3 multiply(const Mat3& left, const Mat3& right) noexcept;
[[nodiscard]] float determinant(const Mat3& matrix) noexcept;
[[nodiscard]] std::optional<Mat3> inverse(const Mat3& matrix) noexcept;
[[nodiscard]] bool reversesOrientation(const Mat3& sourceToRuntime) noexcept;

// Converts an authored legacy/world SRT into the runtime column-vector
// convention. rawScalar is retained as metadata and is never applied to the
// linear transform or translation.
[[nodiscard]] ConvertedNodeTransform convertLegacyTransform(
    const assets::CcfSrtMetadata& source,
    const BasisTransform& basis = {});

// Placed F050 transforms use the same authored-world convention. The
// loader-supported alternate F040 orientation remains semantically unproven
// and is rejected instead of being guessed as a matrix or identity.
[[nodiscard]] ConvertedNodeTransform convertLegacyTransform(
    const assets::CcfPlacedSrtMetadata& source,
    const BasisTransform& basis = {});

// The blueprint loader stores authored world transforms. These helpers derive
// the parent-relative transform used by the runtime scene graph and compose it
// back using column-vector order.
[[nodiscard]] ConvertedNodeTransform deriveLocalTransform(
    const ConvertedNodeTransform& parentWorld,
    const ConvertedNodeTransform& childWorld);
[[nodiscard]] ConvertedNodeTransform composeNodeTransforms(
    const ConvertedNodeTransform& parentWorld,
    const ConvertedNodeTransform& local);

// Converts raw row-vector orientation to the runtime column-vector convention:
// B * transpose(raw) * inverse(B).
[[nodiscard]] Mat3 toRuntimeColumnMatrix(
    const Mat3& rawLegacyMatrix,
    const Mat3& sourceToRuntime);

// Preserves the legacy operand order, normalizes, and returns +Y for a
// degenerate triangle.
[[nodiscard]] Vec3 legacyFaceNormal(
    const Vec3& position0,
    const Vec3& position1,
    const Vec3& position2) noexcept;

[[nodiscard]] ConvertedMeshGeometry convertLegacyGeometry(
    const assets::CcfMeshMetadata& source,
    const BasisTransform& basis = {},
    UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const GeometryLimits& limits = {});

} // namespace airfix::render
