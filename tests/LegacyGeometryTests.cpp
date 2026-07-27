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
using airfix::render::ConvertedNodeTransform;
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

[[nodiscard]] airfix::assets::CcfSrtMetadata sampleTransform() {
    airfix::assets::CcfSrtMetadata transform;
    transform.position = {2.0F, 3.0F, 4.0F};
    transform.rawScalar = 19.0F;
    // Legacy row-vector encoding of a runtime +90-degree X rotation.
    transform.orientation = {
        airfix::assets::CcfVector3{1.0F, 0.0F, 0.0F},
        airfix::assets::CcfVector3{0.0F, 0.0F, -1.0F},
        airfix::assets::CcfVector3{0.0F, 1.0F, 0.0F},
    };
    return transform;
}

void testLegacyNodeTransformConversion() {
    const Mat3 basis{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const auto source = sampleTransform();
    const auto converted = airfix::render::convertLegacyTransform(
        source, BasisTransform{basis, 2.0F});

    requireVec(converted.translation, Vec3{-6.0F, 4.0F, 8.0F},
               "node transform did not convert translation and units");
    const Mat3 raw{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, -1.0F},
        Vec3{0.0F, 1.0F, 0.0F},
    }};
    const auto basisInverse = airfix::render::inverse(basis);
    require(basisInverse.has_value(), "node conversion test basis is singular");
    const Mat3 expectedLinear = airfix::render::multiply(
        airfix::render::multiply(basis, airfix::render::transpose(raw)),
        *basisInverse);
    requireMatrix(converted.linear, expectedLinear,
                  "node transform did not conjugate the legacy orientation");
    require(converted.rawScalar == 19.0F,
            "node transform interpreted or discarded raw scalar metadata");
}

void testPlacedNodeTransformConversion() {
    const auto ordinary = sampleTransform();
    airfix::assets::CcfPlacedSrtMetadata placed{
        .position = ordinary.position,
        .rawScalar = ordinary.rawScalar,
        .orientation = ordinary.orientation,
    };
    const auto expected = airfix::render::convertLegacyTransform(ordinary);
    const auto converted = airfix::render::convertLegacyTransform(placed);
    requireMatrix(
        converted.linear,
        expected.linear,
        "placed F050 orientation did not use the authored-world conversion");
    requireVec(
        converted.translation,
        expected.translation,
        "placed F050 translation did not use the authored-world conversion");
    require(
        converted.rawScalar == expected.rawScalar,
        "placed F050 conversion changed raw scalar metadata");

    placed.orientation =
        airfix::assets::CcfVector3{1.0F, 0.0F, 0.0F};
    requireGeometryError(GeometryErrorCode::unsupportedOrientation, [&] {
        (void)airfix::render::convertLegacyTransform(placed);
    }, "placed alternate F040 orientation was guessed");

    placed.orientation = std::array<airfix::assets::CcfVector3, 3>{
        airfix::assets::CcfVector3{1.0F, 0.0F, 0.0F},
        airfix::assets::CcfVector3{2.0F, 0.0F, 0.0F},
        airfix::assets::CcfVector3{0.0F, 0.0F, 1.0F},
    };
    requireGeometryError(GeometryErrorCode::singularTransform, [&] {
        (void)airfix::render::convertLegacyTransform(placed);
    }, "placed singular authored orientation was accepted");
}

void testParentRelativeRoundTripAndOrder() {
    const Mat3 parentLinear{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const Mat3 localLinear{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
        Vec3{0.0F, -1.0F, 0.0F},
    }};
    const ConvertedNodeTransform parent{
        .linear = parentLinear,
        .translation = Vec3{10.0F, -2.0F, 5.0F},
        .rawScalar = 101.0F,
    };
    const ConvertedNodeTransform local{
        .linear = localLinear,
        .translation = Vec3{2.0F, 1.0F, -3.0F},
        .rawScalar = 7.0F,
    };

    const auto childWorld = airfix::render::composeNodeTransforms(parent, local);
    const Mat3 expectedChildLinear{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
        Vec3{1.0F, 0.0F, 0.0F},
    }};
    requireMatrix(childWorld.linear, expectedChildLinear,
                  "node composition used the wrong non-commutative order");
    requireVec(childWorld.translation, Vec3{9.0F, 0.0F, 2.0F},
               "node composition used the wrong translation order");
    require(childWorld.rawScalar == local.rawScalar,
            "parent raw scalar leaked into child world transform");
    require(childWorld.linear != airfix::render::multiply(localLinear, parentLinear),
            "test rotations unexpectedly commute");

    const auto derived = airfix::render::deriveLocalTransform(parent, childWorld);
    requireMatrix(derived.linear, local.linear,
                  "parent-relative rotation did not round-trip");
    requireVec(derived.translation, local.translation,
               "parent-relative translation did not round-trip");
    require(derived.rawScalar == local.rawScalar,
            "parent-relative derivation changed raw scalar metadata");

    const auto recomposed = airfix::render::composeNodeTransforms(parent, derived);
    requireMatrix(recomposed.linear, childWorld.linear,
                  "derived local rotation did not recompose to authored world");
    requireVec(recomposed.translation, childWorld.translation,
               "derived local translation did not recompose to authored world");
}

void testGeneralInverseAndTranslationOnly() {
    const ConvertedNodeTransform parent{
        .linear = Mat3{{
            Vec3{2.0F, 0.0F, 0.0F},
            Vec3{1.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
        }},
        .translation = Vec3{4.0F, -5.0F, 6.0F},
        .rawScalar = 50.0F,
    };
    const ConvertedNodeTransform child{
        .linear = Mat3{},
        .translation = Vec3{11.0F, -3.0F, 2.0F},
        .rawScalar = 3.0F,
    };
    const auto local = airfix::render::deriveLocalTransform(parent, child);
    const auto roundTrip = airfix::render::composeNodeTransforms(parent, local);
    requireMatrix(roundTrip.linear, child.linear,
                  "general parent inverse did not round-trip linear transform");
    requireVec(roundTrip.translation, child.translation,
               "general parent inverse did not round-trip translation");

    const ConvertedNodeTransform translatedParent{
        .translation = Vec3{7.0F, 8.0F, 9.0F},
        .rawScalar = 1.0F,
    };
    const ConvertedNodeTransform translatedChild{
        .translation = Vec3{10.0F, 14.0F, 18.0F},
        .rawScalar = 2.0F,
    };
    const auto translatedLocal = airfix::render::deriveLocalTransform(
        translatedParent, translatedChild);
    requireVec(translatedLocal.translation, Vec3{3.0F, 6.0F, 9.0F},
               "translation-only local transform is incorrect");
}

void testNodeTransformValidationFailures() {
    auto source = sampleTransform();
    const Mat3 singular{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    requireGeometryError(GeometryErrorCode::singularBasis, [&] {
        (void)airfix::render::convertLegacyTransform(
            source, BasisTransform{singular, 1.0F});
    }, "node transform accepted a singular conversion basis");
    requireGeometryError(GeometryErrorCode::invalidScale, [&] {
        (void)airfix::render::convertLegacyTransform(
            source, BasisTransform{Mat3{}, -1.0F});
    }, "node transform accepted a negative unit scale");

    source.rawScalar = std::numeric_limits<float>::infinity();
    requireGeometryError(GeometryErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::convertLegacyTransform(source);
    }, "node transform accepted a non-finite raw scalar");
    source = sampleTransform();
    source.orientation = {
        airfix::assets::CcfVector3{1.0F, 0.0F, 0.0F},
        airfix::assets::CcfVector3{2.0F, 0.0F, 0.0F},
        airfix::assets::CcfVector3{0.0F, 0.0F, 1.0F},
    };
    requireGeometryError(GeometryErrorCode::singularTransform, [&] {
        (void)airfix::render::convertLegacyTransform(source);
    }, "node transform accepted a singular authored orientation");

    const ConvertedNodeTransform singularParent{
        .linear = singular,
        .rawScalar = 1.0F,
    };
    const ConvertedNodeTransform identityChild{
        .rawScalar = 2.0F,
    };
    requireGeometryError(GeometryErrorCode::singularTransform, [&] {
        (void)airfix::render::deriveLocalTransform(singularParent, identityChild);
    }, "local derivation accepted a singular parent transform");

    ConvertedNodeTransform nonFiniteLocal{
        .rawScalar = 3.0F,
    };
    nonFiniteLocal.translation.x = std::numeric_limits<float>::quiet_NaN();
    requireGeometryError(GeometryErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::composeNodeTransforms(identityChild, nonFiniteLocal);
    }, "node composition accepted a non-finite local transform");
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

void testMissionStartAxisRotationConversion() {
    constexpr float halfPi = 1.57079632679489661923F;
    const auto identity =
        airfix::render::convertLegacyAxisRotationWorldPose(
            Vec3{2.0F, 3.0F, 4.0F}, {0.0F, 0.0F, 0.0F});
    requireVec(identity.translation, Vec3{2.0F, 3.0F, 4.0F},
               "zero start pose changed source position");
    requireMatrix(identity.linear, Mat3{},
                  "zero start axis rotation was not identity");

    const auto x = airfix::render::convertLegacyAxisRotationWorldPose(
        {}, {halfPi, 0.0F, 0.0F});
    requireVec(
        airfix::render::applyRuntimeColumn(
            x.linear, Vec3{0.0F, 1.0F, 0.0F}),
        Vec3{0.0F, 0.0F, 1.0F},
        "positive legacy X did not rotate +Y toward +Z");

    const auto y = airfix::render::convertLegacyAxisRotationWorldPose(
        {}, {0.0F, halfPi, 0.0F});
    requireVec(
        airfix::render::applyRuntimeColumn(
            y.linear, Vec3{1.0F, 0.0F, 0.0F}),
        Vec3{0.0F, 0.0F, 1.0F},
        "positive legacy Y did not rotate +X toward +Z");
    requireVec(
        airfix::render::applyRuntimeColumn(
            y.linear, Vec3{0.0F, 0.0F, 1.0F}),
        Vec3{-1.0F, 0.0F, 0.0F},
        "positive legacy Y did not rotate +Z toward -X");

    const auto z = airfix::render::convertLegacyAxisRotationWorldPose(
        {}, {0.0F, 0.0F, halfPi});
    requireVec(
        airfix::render::applyRuntimeColumn(
            z.linear, Vec3{1.0F, 0.0F, 0.0F}),
        Vec3{0.0F, 1.0F, 0.0F},
        "positive legacy Z did not rotate +X toward +Y");

    constexpr std::array<float, 3> mixed{0.3F, -0.7F, 1.1F};
    const auto mixedPose =
        airfix::render::convertLegacyAxisRotationWorldPose({}, mixed);
    const float cx = std::cos(mixed[0]);
    const float sx = std::sin(mixed[0]);
    const float cy = std::cos(mixed[1]);
    const float sy = std::sin(mixed[1]);
    const float cz = std::cos(mixed[2]);
    const float sz = std::sin(mixed[2]);
    const Mat3 runtimeX{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, cx, sx},
        Vec3{0.0F, -sx, cx},
    }};
    const Mat3 runtimeYNegative{{
        Vec3{cy, 0.0F, sy},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-sy, 0.0F, cy},
    }};
    const Mat3 runtimeZ{{
        Vec3{cz, sz, 0.0F},
        Vec3{-sz, cz, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    requireMatrix(
        mixedPose.linear,
        airfix::render::multiply(
            runtimeZ,
            airfix::render::multiply(runtimeX, runtimeYNegative)),
        "mixed start rotation did not preserve Z-X-Y legacy order");

    const Mat3 basis{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const auto converted =
        airfix::render::convertLegacyAxisRotationWorldPose(
            Vec3{2.0F, 3.0F, 4.0F}, mixed,
            BasisTransform{basis, 2.0F});
    requireVec(converted.translation, Vec3{-6.0F, 4.0F, 8.0F},
               "start pose did not use the room basis and unit scale");
    const auto inverseBasis = airfix::render::inverse(basis);
    require(inverseBasis.has_value(), "valid start basis was singular");
    requireMatrix(
        converted.linear,
        airfix::render::multiply(
            airfix::render::multiply(basis, mixedPose.linear),
            *inverseBasis),
        "start rotation was not conjugated through the room basis");

    (void)airfix::render::convertLegacyAxisRotationWorldPose(
        {}, {std::numeric_limits<float>::max(), 0.0F, 0.0F});
    requireGeometryError(
        GeometryErrorCode::nonFiniteValue,
        [] {
            (void)airfix::render::convertLegacyAxisRotationWorldPose(
                {}, {0.0F, std::numeric_limits<float>::infinity(), 0.0F});
        },
        "non-finite start angle was accepted");
    requireGeometryError(
        GeometryErrorCode::singularBasis,
        [] {
            const Mat3 singular{{
                Vec3{1.0F, 0.0F, 0.0F},
                Vec3{2.0F, 0.0F, 0.0F},
                Vec3{0.0F, 0.0F, 1.0F},
            }};
            (void)airfix::render::convertLegacyAxisRotationWorldPose(
                {}, {}, BasisTransform{singular, 1.0F});
        },
        "start pose accepted a singular basis");
    require(
        !airfix::render::tryConvertLegacyAxisRotationWorldPose(
             {}, {0.0F, std::numeric_limits<float>::infinity(), 0.0F})
             .has_value(),
        "no-throw start conversion accepted a non-finite angle");
    const Mat3 singular{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    require(
        !airfix::render::tryConvertLegacyAxisRotationWorldPose(
             {}, {}, BasisTransform{singular, 1.0F})
             .has_value(),
        "no-throw start conversion accepted a singular basis");
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
        testLegacyNodeTransformConversion();
        testPlacedNodeTransformConversion();
        testParentRelativeRoundTripAndOrder();
        testGeneralInverseAndTranslationOnly();
        testNodeTransformValidationFailures();
        testMissionStartAxisRotationConversion();
        testValidationFailures();
        std::cout << "Legacy geometry tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy geometry tests failed: " << error.what() << '\n';
        return 1;
    }
}
