#include "airfix/render/DrawMesh.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using airfix::render::ConvertedMeshGeometry;
using airfix::render::ConvertedMeshTriangle;
using airfix::render::ConvertedMeshVertex;
using airfix::render::DrawMaterial;
using airfix::render::DrawMeshError;
using airfix::render::DrawMeshErrorCode;
using airfix::render::DrawMeshLimits;
using airfix::render::TexcoordMode;
using airfix::render::TextureAssetId;
using airfix::render::Vec3;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void requireDrawError(
    const DrawMeshErrorCode expected,
    Callable&& callable,
    const std::string& message) {
    try {
        callable();
    }
    catch (const DrawMeshError& error) {
        require(error.code() == expected, message + " (wrong error code)");
        return;
    }
    throw std::runtime_error(message + " (no error)");
}

[[nodiscard]] std::vector<DrawMaterial> materials() {
    return {
        DrawMaterial{10U, TextureAssetId{100U}, std::nullopt, std::nullopt},
        DrawMaterial{20U, TextureAssetId{200U}, TextureAssetId{201U}, std::nullopt},
    };
}

[[nodiscard]] ConvertedMeshGeometry squareGeometry() {
    ConvertedMeshGeometry mesh;
    mesh.vertices = {
        ConvertedMeshVertex{Vec3{0.0F, 0.0F, 0.0F}},
        ConvertedMeshVertex{Vec3{1.0F, 0.0F, 0.0F}},
        ConvertedMeshVertex{Vec3{1.0F, 1.0F, 0.0F}},
        ConvertedMeshVertex{Vec3{0.0F, 1.0F, 0.0F}},
    };
    mesh.triangles = {
        ConvertedMeshTriangle{
            {0U, 1U, 2U},
            10U,
            std::array<float, 6>{0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        },
        ConvertedMeshTriangle{
            {0U, 2U, 3U},
            10U,
            std::array<float, 6>{0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        },
    };
    return mesh;
}

void testSharedCornersAndBounds() {
    const ConvertedMeshGeometry source = squareGeometry();
    const auto bindings = materials();
    const auto payload = airfix::render::buildDrawMesh(source, bindings);

    require(payload.vertices.size() == 4U, "identical corners were not deduplicated");
    require(payload.indices == std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U},
            "triangle or first-use vertex order changed");
    require(payload.materials.size() == 1U && payload.materials[0] == bindings[0],
            "material table did not retain first use");
    require(payload.ranges.size() == 1U && payload.ranges[0].firstIndex == 0U &&
                payload.ranges[0].indexCount == 6U &&
                payload.ranges[0].texcoordMode == TexcoordMode::uv0,
            "contiguous textured triangles did not share a range");
    require(payload.localBounds.minimum == Vec3{0.0F, 0.0F, 0.0F} &&
                payload.localBounds.maximum == Vec3{1.0F, 1.0F, 0.0F},
            "payload bounds are incorrect");
}

void testUvAndNormalSeams() {
    auto bindings = materials();
    ConvertedMeshGeometry source = squareGeometry();
    (*source.triangles[1].textureCoordinates)[0] = 0.25F;
    auto payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.size() == 5U, "UV seam did not split only its corner");

    source = squareGeometry();
    source.triangles[1].faceNormal = Vec3{0.0F, 0.0F, 1.0F};
    payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.size() == 6U,
            "flat-normal seam did not split the two shared corners");

    source = squareGeometry();
    source.triangles.push_back(source.triangles[0]);
    source.triangles.back().faceNormal.x = -0.0F;
    auto& zeroUvs = *source.triangles.back().textureCoordinates;
    zeroUvs[0] = -0.0F;
    zeroUvs[1] = -0.0F;
    payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.size() == 4U,
            "signed zero was not canonicalized in the seam key");
}

void testMaterialsAndStableRanges() {
    auto bindings = materials();
    ConvertedMeshGeometry source = squareGeometry();
    source.triangles = {
        source.triangles[0],
        source.triangles[0],
        source.triangles[0],
    };
    source.triangles[0].materialReference = 20U;
    source.triangles[1].materialReference = 10U;
    source.triangles[2].materialReference = 20U;

    const auto payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.size() == 3U,
            "material changes unnecessarily duplicated vertices");
    require(payload.materials.size() == 2U &&
                payload.materials[0].sourceReference == 20U &&
                payload.materials[1].sourceReference == 10U,
            "materials are not ordered by first use");
    require(payload.ranges.size() == 3U, "A, B, A was globally regrouped");
    require(payload.ranges[0].materialSlot == 0U &&
                payload.ranges[1].materialSlot == 1U &&
                payload.ranges[2].materialSlot == 0U,
            "A, B, A range material slots are incorrect");
    require(payload.ranges[0].firstIndex == 0U &&
                payload.ranges[1].firstIndex == 3U &&
                payload.ranges[2].firstIndex == 6U,
            "range index order changed");
}

void testNoUvIsSeparateMode() {
    auto bindings = materials();
    ConvertedMeshGeometry source = squareGeometry();
    source.triangles = {source.triangles[0], source.triangles[0]};
    source.triangles[1].textureCoordinates.reset();

    const auto payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.size() == 6U,
            "UV presence was not included in the seam key");
    require(payload.ranges.size() == 2U &&
                payload.ranges[0].texcoordMode == TexcoordMode::uv0 &&
                payload.ranges[1].texcoordMode == TexcoordMode::none,
            "no-UV triangles did not use their own draw mode/range");
    require(payload.vertices[3].uv == airfix::render::Vec2{},
            "no-UV vertex did not receive deterministic zero UV storage");
}

void testMaterialValidation() {
    const ConvertedMeshGeometry source = squareGeometry();
    auto bindings = materials();
    bindings.push_back(bindings[0]);
    requireDrawError(DrawMeshErrorCode::duplicateMaterial, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "duplicate material binding was accepted");

    bindings = {materials()[1]};
    requireDrawError(DrawMeshErrorCode::missingMaterial, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "missing triangle material binding was accepted");
}

void testGeometryValidation() {
    auto bindings = materials();
    ConvertedMeshGeometry source = squareGeometry();
    source.vertices[0].position.x = std::numeric_limits<float>::infinity();
    requireDrawError(DrawMeshErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "non-finite position was accepted");

    source = squareGeometry();
    source.triangles[0].faceNormal.y = std::numeric_limits<float>::quiet_NaN();
    requireDrawError(DrawMeshErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "non-finite normal was accepted");

    source = squareGeometry();
    (*source.triangles[0].textureCoordinates)[1] =
        std::numeric_limits<float>::infinity();
    requireDrawError(DrawMeshErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "non-finite UV was accepted");

    source = squareGeometry();
    source.triangles[0].vertexIndices[2] = 4U;
    requireDrawError(DrawMeshErrorCode::vertexIndexOutOfRange, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings);
    }, "out-of-range source vertex was accepted");
}

void testLimits() {
    const ConvertedMeshGeometry source = squareGeometry();
    const auto bindings = materials();

    DrawMeshLimits limits;
    limits.maximumVertices = 3U;
    requireDrawError(DrawMeshErrorCode::limitExceeded, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings, limits);
    }, "vertex limit was not enforced");

    limits = {};
    limits.maximumIndices = 5U;
    requireDrawError(DrawMeshErrorCode::limitExceeded, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings, limits);
    }, "index limit was not enforced");

    limits = {};
    limits.maximumMaterials = 0U;
    requireDrawError(DrawMeshErrorCode::limitExceeded, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings, limits);
    }, "material limit was not enforced");

    limits = {};
    limits.maximumRanges = 0U;
    requireDrawError(DrawMeshErrorCode::limitExceeded, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings, limits);
    }, "range limit was not enforced");

    const auto payload = airfix::render::buildDrawMesh(source, bindings);
    const std::size_t payloadBytes = payload.vertices.size() * sizeof(payload.vertices[0]) +
        payload.indices.size() * sizeof(payload.indices[0]) +
        payload.materials.size() * sizeof(payload.materials[0]) +
        payload.ranges.size() * sizeof(payload.ranges[0]);
    limits = {};
    limits.maximumTotalBytes = payloadBytes - 1U;
    requireDrawError(DrawMeshErrorCode::limitExceeded, [&] {
        (void)airfix::render::buildDrawMesh(source, bindings, limits);
    }, "combined byte limit was not enforced");
}

void testEmptyPayload() {
    const ConvertedMeshGeometry source;
    const std::array<DrawMaterial, 0> bindings{};
    const auto payload = airfix::render::buildDrawMesh(source, bindings);
    require(payload.vertices.empty() && payload.indices.empty() &&
                payload.materials.empty() && payload.ranges.empty(),
            "empty geometry produced draw data");
    require(payload.localBounds == airfix::render::Bounds3{},
            "empty payload bounds are not deterministic zeros");
}

} // namespace

int main() {
    try {
        testSharedCornersAndBounds();
        testUvAndNormalSeams();
        testMaterialsAndStableRanges();
        testNoUvIsSeparateMode();
        testMaterialValidation();
        testGeometryValidation();
        testLimits();
        testEmptyPayload();
        std::cout << "Draw mesh tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Draw mesh tests failed: " << error.what() << '\n';
        return 1;
    }
}
