#include "airfix/render/LegacyGeometry.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::assets::CcfMeshMetadata;
using airfix::assets::CcfMeshTriangleMetadata;
using airfix::assets::CcfMeshVertexMetadata;
using airfix::render::BasisTransform;
using airfix::render::GeometryError;
using airfix::render::GeometryErrorCode;
using airfix::render::GeometryLimits;
using airfix::render::Mat3;
using airfix::render::UvPolicy;
using airfix::render::Vec3;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) <= 0.00001F;
}

void requireVec(const Vec3 actual, const Vec3 expected, const std::string& message) {
    require(near(actual.x, expected.x) && near(actual.y, expected.y) &&
                near(actual.z, expected.z),
            message);
}

void requireMatrix(const Mat3& actual, const Mat3& expected, const std::string& message) {
    for (std::size_t column = 0U; column < actual.columns.size(); ++column) {
        requireVec(actual.columns[column], expected.columns[column], message);
    }
}

void requireUvs(
    const std::optional<std::array<float, 6>>& actual,
    const std::array<float, 6>& expected,
    const std::string& message) {
    require(actual.has_value(), message + " (missing UVs)");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        require(near((*actual)[index], expected[index]), message);
    }
}

template <typename Callable>
void requireGeometryError(
    const GeometryErrorCode expected,
    Callable&& callable,
    const std::string& message) {
    try {
        callable();
    }
    catch (const GeometryError& error) {
        require(error.code() == expected, message + " (wrong error code)");
        return;
    }
    throw std::runtime_error(message + " (no error)");
}

[[nodiscard]] CcfMeshMetadata sampleMesh() {
    CcfMeshMetadata mesh;
    mesh.name = "Asymmetric";
    mesh.prefix = "Test";
    mesh.reference = 17U;
    mesh.selectionFlagA = 1U;
    mesh.linkReference = 23U;
    mesh.position = {2.0F, 3.0F, 4.0F};
    mesh.scalar = 2.5F;
    mesh.orientation = {
        airfix::assets::CcfVector3{1.0F, 2.0F, 3.0F},
        airfix::assets::CcfVector3{0.0F, 1.0F, 4.0F},
        airfix::assets::CcfVector3{5.0F, 6.0F, 0.0F},
    };
    mesh.vertices = {
        CcfMeshVertexMetadata{{0.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{{1.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{{0.0F, 1.0F, 0.0F}},
    };
    CcfMeshTriangleMetadata triangle;
    triangle.vertexIndices = {0U, 1U, 2U};
    triangle.materialReference = 99U;
    triangle.textureCoordinates = std::array<float, 6>{
        0.1F, 0.2F,
        0.3F, 0.4F,
        0.5F, 0.6F,
    };
    mesh.triangles.push_back(triangle);
    return mesh;
}

void testAsymmetricDefaultIdentity() {
    const CcfMeshMetadata source = sampleMesh();
    const auto converted = airfix::render::convertLegacyGeometry(source);

    require(converted.name == source.name && converted.prefix == source.prefix,
            "identity conversion lost mesh identity");
    require(converted.reference == 17U && converted.linkReference == 23U,
            "identity conversion lost references");
    requireVec(converted.translation, Vec3{2.0F, 3.0F, 4.0F},
               "identity conversion changed translation");
    require(converted.rawScalar == 2.5F, "identity conversion interpreted raw scalar");
    require(!converted.windingReversed, "identity basis reversed winding");
    require(converted.vertices.size() == 3U &&
                converted.vertices[1].position == Vec3{1.0F, 0.0F, 0.0F},
            "identity conversion changed vertices");
    require(converted.triangles[0].vertexIndices ==
                std::array<std::uint32_t, 3>{0U, 1U, 2U},
            "identity conversion changed indices");
    require(converted.triangles[0].textureCoordinates == source.triangles[0].textureCoordinates,
            "default conversion changed raw UVs");

    const Mat3 expectedOrientation{{
        Vec3{1.0F, 0.0F, 5.0F},
        Vec3{2.0F, 1.0F, 6.0F},
        Vec3{3.0F, 4.0F, 0.0F},
    }};
    requireMatrix(converted.orientation, expectedOrientation,
                  "identity basis did not transpose raw orientation");
}

void testLegacyRowMatrixColumns() {
    const Mat3 raw{{
        Vec3{1.0F, 2.0F, 3.0F},
        Vec3{4.0F, 5.0F, 6.0F},
        Vec3{7.0F, 8.0F, 9.0F},
    }};
    requireVec(airfix::render::applyLegacyRow(raw, Vec3{1.0F, 0.0F, 0.0F}),
               Vec3{1.0F, 4.0F, 7.0F}, "legacy row X basis mismatch");
    requireVec(airfix::render::applyLegacyRow(raw, Vec3{0.0F, 1.0F, 0.0F}),
               Vec3{2.0F, 5.0F, 8.0F}, "legacy row Y basis mismatch");
    requireVec(airfix::render::applyLegacyRow(raw, Vec3{0.0F, 0.0F, 1.0F}),
               Vec3{3.0F, 6.0F, 9.0F}, "legacy row Z basis mismatch");
}

void testPositiveNinetyDegreesAroundX() {
    // Raw columns encode the transpose of a standard column-vector +90 X rotation.
    const Mat3 raw{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, -1.0F},
        Vec3{0.0F, 1.0F, 0.0F},
    }};
    const Mat3 converted = airfix::render::toRuntimeColumnMatrix(raw, Mat3{});
    requireVec(airfix::render::applyRuntimeColumn(converted, Vec3{0.0F, 1.0F, 0.0F}),
               Vec3{0.0F, 0.0F, 1.0F}, "+90 X did not send +Y to +Z");
}

void testLegacyWindingNormal() {
    const Vec3 normal = airfix::render::legacyFaceNormal(
        Vec3{0.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 3.0F, 0.0F});
    requireVec(normal, Vec3{0.0F, 0.0F, -1.0F},
               "legacy operand order should produce a normalized -Z");
    requireVec(airfix::render::legacyFaceNormal(
                   Vec3{0.0F, 0.0F, 0.0F},
                   Vec3{0.0F, 1.0F, 0.0F},
                   Vec3{1.0F, 0.0F, 0.0F}),
               Vec3{0.0F, 0.0F, 1.0F}, "swapped winding should produce +Z");
    requireVec(airfix::render::legacyFaceNormal(
                   Vec3{0.0F, 0.0F, 0.0F},
                   Vec3{1.0F, 0.0F, 0.0F},
                   Vec3{2.0F, 0.0F, 0.0F}),
               Vec3{0.0F, 1.0F, 0.0F},
               "degenerate legacy triangle should use +Y fallback");
}

void testReflectedBasisSwapsExactlyOnce() {
    CcfMeshMetadata source = sampleMesh();
    const BasisTransform reflected{
        Mat3{{
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        }},
        1.0F,
    };
    const auto converted = airfix::render::convertLegacyGeometry(source, reflected);
    require(converted.windingReversed, "reflected basis did not mark winding reversal");
    require(converted.triangles[0].vertexIndices ==
                std::array<std::uint32_t, 3>{0U, 2U, 1U},
            "reflected basis did not swap indices 1 and 2 exactly once");
    requireVec(converted.triangles[0].faceNormal, Vec3{0.0F, 0.0F, 1.0F},
               "reflected triangle normal is inconsistent with the swapped winding");
    requireUvs(converted.triangles[0].textureCoordinates, {
        0.1F, 0.2F,
        0.5F, 0.6F,
        0.3F, 0.4F,
    }, "reflected winding did not keep UVs attached to their corners");
}

void testUvPolicies() {
    const CcfMeshMetadata source = sampleMesh();
    const auto preserved = airfix::render::convertLegacyGeometry(source);
    require(preserved.triangles[0].textureCoordinates == source.triangles[0].textureCoordinates,
            "preserveRaw changed UVs");

    const auto flipped = airfix::render::convertLegacyGeometry(
        source, BasisTransform{}, UvPolicy::flipVExplicit);
    requireUvs(flipped.triangles[0].textureCoordinates, {
        0.1F, 0.8F,
        0.3F, 0.6F,
        0.5F, 0.4F,
    }, "flipVExplicit did not flip only V coordinates");
}

void testBasisConjugationAndScaling() {
    const Mat3 basis{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const auto basisInverse = airfix::render::inverse(basis);
    require(basisInverse.has_value(), "valid basis did not have an inverse");
    requireMatrix(airfix::render::multiply(basis, *basisInverse), Mat3{},
                  "basis inverse is incorrect");

    CcfMeshMetadata source = sampleMesh();
    const auto converted = airfix::render::convertLegacyGeometry(
        source, BasisTransform{basis, 2.0F});
    requireVec(converted.translation, Vec3{-6.0F, 4.0F, 8.0F},
               "basis and unit scale were not applied to translation");
    requireVec(converted.vertices[1].position, Vec3{0.0F, 2.0F, 0.0F},
               "basis and unit scale were not applied to vertices");
    require(converted.rawScalar == source.scalar, "unit scale changed raw scalar");

    const Mat3 expected = airfix::render::multiply(
        airfix::render::multiply(
            basis,
            airfix::render::transpose(Mat3{{
                Vec3{1.0F, 2.0F, 3.0F},
                Vec3{0.0F, 1.0F, 4.0F},
                Vec3{5.0F, 6.0F, 0.0F},
            }})),
        *basisInverse);
    requireMatrix(converted.orientation, expected, "basis conjugation mismatch");

    const Vec3 sourceVector{0.25F, -2.0F, 1.5F};
    const Mat3 raw{{
        Vec3{1.0F, 2.0F, 3.0F},
        Vec3{0.0F, 1.0F, 4.0F},
        Vec3{5.0F, 6.0F, 0.0F},
    }};
    const Vec3 runtimeInput = airfix::render::applyRuntimeColumn(basis, sourceVector);
    const Vec3 runtimeOutput = airfix::render::applyRuntimeColumn(
        converted.orientation, runtimeInput);
    const Vec3 expectedRuntimeOutput = airfix::render::applyRuntimeColumn(
        basis, airfix::render::applyLegacyRow(raw, sourceVector));
    requireVec(runtimeOutput, expectedRuntimeOutput,
               "basis conversion does not preserve the legacy transform property");

    source.vertices[2].position = {2.0F, 0.0F, 0.0F};
    const auto degenerate = airfix::render::convertLegacyGeometry(
        source, BasisTransform{basis, 2.0F});
    requireVec(degenerate.triangles[0].faceNormal, Vec3{-1.0F, 0.0F, 0.0F},
               "degenerate normal fallback was not transformed into runtime basis");

    const Mat3 shear{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{1.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const auto sheared = airfix::render::convertLegacyGeometry(
        source, BasisTransform{shear, 1.0F});
    requireVec(sheared.triangles[0].faceNormal, Vec3{0.0F, 1.0F, 0.0F},
               "normal fallback must use the inverse-transpose under shear");
}

void testValidationFailures() {
    CcfMeshMetadata source = sampleMesh();
    const Mat3 singular{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    requireGeometryError(GeometryErrorCode::singularBasis, [&] {
        (void)airfix::render::convertLegacyGeometry(source, BasisTransform{singular, 1.0F});
    }, "singular basis was accepted");

    requireGeometryError(GeometryErrorCode::invalidScale, [&] {
        (void)airfix::render::convertLegacyGeometry(source, BasisTransform{Mat3{}, 0.0F});
    }, "zero scale was accepted");

    requireGeometryError(GeometryErrorCode::invalidScale, [&] {
        (void)airfix::render::convertLegacyGeometry(
            source,
            BasisTransform{Mat3{}, std::numeric_limits<float>::infinity()});
    }, "non-finite scale was accepted");

    source.vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    requireGeometryError(GeometryErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::convertLegacyGeometry(source);
    }, "non-finite vertex was accepted");
    source = sampleMesh();

    requireGeometryError(GeometryErrorCode::limitExceeded, [&] {
        (void)airfix::render::convertLegacyGeometry(
            source, BasisTransform{}, UvPolicy::preserveRaw, GeometryLimits{2U, 1U});
    }, "vertex limit was not enforced");

    source.triangles[0].vertexIndices[2] = 3U;
    requireGeometryError(GeometryErrorCode::vertexIndexOutOfRange, [&] {
        (void)airfix::render::convertLegacyGeometry(source);
    }, "out-of-range triangle index was accepted");
}

} // namespace

int main() {
    try {
        testAsymmetricDefaultIdentity();
        testLegacyRowMatrixColumns();
        testPositiveNinetyDegreesAroundX();
        testLegacyWindingNormal();
        testReflectedBasisSwapsExactlyOnce();
        testUvPolicies();
        testBasisConjugationAndScaling();
        testValidationFailures();
        std::cout << "Legacy geometry tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy geometry tests failed: " << error.what() << '\n';
        return 1;
    }
}
