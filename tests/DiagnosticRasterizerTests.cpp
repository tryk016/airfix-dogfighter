#include "airfix/render/DiagnosticRasterizer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using airfix::assets::RgbaImage;
using airfix::render::DiagnosticRasterizerError;
using airfix::render::DiagnosticRasterizerErrorCode;
using airfix::render::DiagnosticRasterizerOptions;
using airfix::render::DiagnosticTextureView;
using airfix::render::DrawMaterial;
using airfix::render::DrawMeshPayload;
using airfix::render::DrawMeshInstance;
using airfix::render::DrawModelPayload;
using airfix::render::DrawRange;
using airfix::render::DrawVertex;
using airfix::render::TexcoordMode;
using airfix::render::TextureAssetId;
using airfix::render::Vec2;
using airfix::render::Vec3;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void requireError(
    const DiagnosticRasterizerErrorCode expected,
    Callable&& callable,
    const std::string& message) {
    try {
        callable();
    }
    catch (const DiagnosticRasterizerError& error) {
        require(error.code() == expected, message + " (wrong error code)");
        return;
    }
    throw std::runtime_error(message + " (no error)");
}

[[nodiscard]] std::array<std::uint8_t, 4> pixel(
    const RgbaImage& image,
    const std::uint32_t x,
    const std::uint32_t y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return {
        image.pixels[offset],
        image.pixels[offset + 1U],
        image.pixels[offset + 2U],
        image.pixels[offset + 3U],
    };
}

[[nodiscard]] RgbaImage solidTexture(
    const std::array<std::uint8_t, 4> color) {
    return RgbaImage{
        .width = 1U,
        .height = 1U,
        .pixels = {color[0], color[1], color[2], color[3]},
    };
}

[[nodiscard]] DiagnosticRasterizerOptions testOptions() {
    DiagnosticRasterizerOptions options;
    options.width = 8U;
    options.height = 8U;
    options.yawRadians = 0.0F;
    options.pitchRadians = 0.0F;
    options.backgroundColor = {1U, 2U, 3U, 255U};
    options.fallbackColor = {201U, 101U, 51U, 255U};
    return options;
}

[[nodiscard]] DrawMeshPayload texturedQuad() {
    DrawMeshPayload mesh;
    mesh.vertices = {
        DrawVertex{Vec3{-1.0F, -1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 1.0F}},
        DrawVertex{Vec3{1.0F, -1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 1.0F}},
        DrawVertex{Vec3{1.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 0.0F}},
        DrawVertex{Vec3{-1.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 0.0F}},
    };
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.materials = {DrawMaterial{
        .sourceReference = 42U,
        .primary = TextureAssetId{7U},
        .secondary = std::nullopt,
        .environment = std::nullopt,
    }};
    mesh.ranges = {DrawRange{
        .firstIndex = 0U,
        .indexCount = 6U,
        .materialSlot = 0U,
        .texcoordMode = TexcoordMode::uv0,
    }};
    mesh.localBounds = {
        .minimum = Vec3{-1.0F, -1.0F, 0.0F},
        .maximum = Vec3{1.0F, 1.0F, 0.0F},
    };
    return mesh;
}

[[nodiscard]] DrawMeshPayload coloredQuad(
    const std::uint32_t textureId,
    const std::uint32_t materialReference) {
    auto mesh = texturedQuad();
    mesh.materials[0].sourceReference = materialReference;
    mesh.materials[0].primary = TextureAssetId{textureId};
    return mesh;
}

void testTwoByTwoUvCornersAndFlip() {
    const RgbaImage texture{
        .width = 2U,
        .height = 2U,
        .pixels = {
            255U, 0U, 0U, 255U,
            0U, 255U, 0U, 255U,
            0U, 0U, 255U, 255U,
            255U, 255U, 255U, 255U,
        },
    };
    const DiagnosticTextureView textureView{TextureAssetId{7U}, &texture};
    auto options = testOptions();
    const auto image = airfix::render::rasterizeDiagnostic(
        texturedQuad(), std::span{&textureView, 1U}, options);

    require(pixel(image, 1U, 1U) == std::array<std::uint8_t, 4>{255U, 0U, 0U, 255U},
            "top-left UV did not sample the top-left texel");
    require(pixel(image, 6U, 1U) == std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U},
            "top-right UV did not sample the top-right texel");
    require(pixel(image, 1U, 6U) == std::array<std::uint8_t, 4>{0U, 0U, 255U, 255U},
            "bottom-left UV did not sample the bottom-left texel");
    require(pixel(image, 6U, 6U) ==
                std::array<std::uint8_t, 4>{255U, 255U, 255U, 255U},
            "bottom-right UV did not sample the bottom-right texel");

    options.flipV = true;
    const auto flipped = airfix::render::rasterizeDiagnostic(
        texturedQuad(), std::span{&textureView, 1U}, options);
    require(pixel(flipped, 1U, 1U) ==
                std::array<std::uint8_t, 4>{0U, 0U, 255U, 255U},
            "explicit V flip did not swap texture rows");
}

void testDepthAndNoCulling() {
    DrawMeshPayload mesh;
    mesh.vertices = {
        DrawVertex{Vec3{-1.0F, -1.0F, -1.0F}, {}, Vec2{0.0F, 0.0F}},
        DrawVertex{Vec3{1.0F, -1.0F, -1.0F}, {}, Vec2{1.0F, 0.0F}},
        DrawVertex{Vec3{0.0F, 1.0F, -1.0F}, {}, Vec2{0.5F, 1.0F}},
        DrawVertex{Vec3{-1.0F, -1.0F, 1.0F}, {}, Vec2{0.0F, 0.0F}},
        DrawVertex{Vec3{0.0F, 1.0F, 1.0F}, {}, Vec2{0.5F, 1.0F}},
        DrawVertex{Vec3{1.0F, -1.0F, 1.0F}, {}, Vec2{1.0F, 0.0F}},
    };
    mesh.indices = {0U, 1U, 2U, 3U, 4U, 5U};
    mesh.materials = {
        DrawMaterial{
            .sourceReference = 1U,
            .primary = TextureAssetId{1U},
            .secondary = std::nullopt,
            .environment = std::nullopt,
        },
        DrawMaterial{
            .sourceReference = 2U,
            .primary = TextureAssetId{2U},
            .secondary = std::nullopt,
            .environment = std::nullopt,
        },
    };
    mesh.ranges = {
        DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
        DrawRange{3U, 3U, 1U, TexcoordMode::uv0},
    };
    mesh.localBounds = {Vec3{-1.0F, -1.0F, -1.0F}, Vec3{1.0F, 1.0F, 1.0F}};

    const auto red = solidTexture({255U, 0U, 0U, 255U});
    const auto green = solidTexture({0U, 255U, 0U, 255U});
    const std::array textureViews{
        DiagnosticTextureView{TextureAssetId{1U}, &red},
        DiagnosticTextureView{TextureAssetId{2U}, &green},
    };
    const auto image = airfix::render::rasterizeDiagnostic(
        mesh, textureViews, testOptions());
    require(pixel(image, 3U, 4U) ==
                std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U},
            "near reverse-wound triangle did not win the depth test");
}

void testFallbackAndDeterministicSummary() {
    auto mesh = texturedQuad();
    mesh.materials[0].primary = std::nullopt;
    mesh.ranges[0].texcoordMode = TexcoordMode::none;
    const auto options = testOptions();
    const auto first = airfix::render::rasterizeDiagnostic(mesh, {}, options);
    const auto second = airfix::render::rasterizeDiagnostic(mesh, {}, options);
    require(first.pixels == second.pixels, "diagnostic output is not deterministic");
    require(pixel(first, 4U, 4U) == options.fallbackColor,
            "untextured range did not use the exact fallback color");

    std::size_t fallbackPixels = 0U;
    std::uint64_t byteSum = 0U;
    std::uint64_t fnv = 1469598103934665603ULL;
    for (std::size_t offset = 0U; offset < first.pixels.size(); offset += 4U) {
        const std::array<std::uint8_t, 4> color{
            first.pixels[offset], first.pixels[offset + 1U],
            first.pixels[offset + 2U], first.pixels[offset + 3U],
        };
        if (color == options.fallbackColor) {
            ++fallbackPixels;
        }
        for (std::size_t component = 0U; component < 4U; ++component) {
            const auto value = first.pixels[offset + component];
            byteSum += value;
            fnv ^= value;
            fnv *= 1099511628211ULL;
        }
    }
    require(fallbackPixels == 64U, "fallback coverage summary changed");
    require(byteSum == 38912U, "fallback byte-sum summary changed");
    require(fnv == 3058057362635646339ULL,
            "diagnostic exact hash changed: " + std::to_string(fnv));
}

void testValidationAndLimits() {
    auto options = testOptions();
    options.width = 0U;
    requireError(DiagnosticRasterizerErrorCode::invalidDimensions, [&] {
        (void)airfix::render::rasterizeDiagnostic(texturedQuad(), {}, options);
    }, "zero output width was accepted");

    options = testOptions();
    options.maximumPixels = 63U;
    requireError(DiagnosticRasterizerErrorCode::limitExceeded, [&] {
        (void)airfix::render::rasterizeDiagnostic(texturedQuad(), {}, options);
    }, "pixel limit was not enforced");

    auto missingMaterial = texturedQuad();
    missingMaterial.ranges[0].materialSlot = 1U;
    requireError(DiagnosticRasterizerErrorCode::missingMaterial, [&] {
        (void)airfix::render::rasterizeDiagnostic(
            missingMaterial, {}, testOptions());
    }, "missing material was accepted");

    requireError(DiagnosticRasterizerErrorCode::missingTexture, [&] {
        (void)airfix::render::rasterizeDiagnostic(
            texturedQuad(), {}, testOptions());
    }, "missing primary texture was accepted");

    const RgbaImage malformedTexture{.width = 2U, .height = 2U, .pixels = {0U}};
    const DiagnosticTextureView malformedView{TextureAssetId{7U}, &malformedTexture};
    requireError(DiagnosticRasterizerErrorCode::invalidTexture, [&] {
        (void)airfix::render::rasterizeDiagnostic(
            texturedQuad(), std::span{&malformedView, 1U}, testOptions());
    }, "invalid RGBA texture byte count was accepted");

    auto nonFinite = texturedQuad();
    nonFinite.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
    requireError(DiagnosticRasterizerErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::rasterizeDiagnostic(nonFinite, {}, testOptions());
    }, "non-finite vertex was accepted");

    options = testOptions();
    options.modelTranslation.x = std::numeric_limits<float>::infinity();
    requireError(DiagnosticRasterizerErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::rasterizeDiagnostic(
            texturedQuad(), {}, options);
    }, "non-finite model transform was accepted");
}

void testModelSharedFitAndDepth() {
    const auto red = solidTexture({255U, 0U, 0U, 255U});
    const auto green = solidTexture({0U, 255U, 0U, 255U});
    const std::array textureViews{
        DiagnosticTextureView{TextureAssetId{1U}, &red},
        DiagnosticTextureView{TextureAssetId{2U}, &green},
    };

    DrawModelPayload separated;
    separated.meshes = {coloredQuad(1U, 10U), coloredQuad(2U, 20U)};
    separated.instances = {
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 100U,
            .modelTranslation = Vec3{-2.0F, 0.0F, 0.0F},
        },
        DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference = 200U,
            .modelTranslation = Vec3{2.0F, 0.0F, 0.0F},
        },
    };
    auto options = testOptions();
    options.width = 20U;
    options.height = 10U;
    const auto fitted = airfix::render::rasterizeDiagnosticModel(
        separated, textureViews, options);
    require(pixel(fitted, 4U, 5U) ==
                std::array<std::uint8_t, 4>{255U, 0U, 0U, 255U},
            "left instance was not placed in the shared model fit");
    require(pixel(fitted, 16U, 5U) ==
                std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U},
            "right instance was not placed in the shared model fit");
    require(pixel(fitted, 10U, 5U) == options.backgroundColor,
            "model instances were fitted independently instead of together");

    options.modelLinear.columns = {
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, -1.0F},
    };
    const auto outerRotated = airfix::render::rasterizeDiagnosticModel(
        separated, textureViews, options);
    require(pixel(outerRotated, 4U, 5U) ==
                std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U} &&
            pixel(outerRotated, 16U, 5U) ==
                std::array<std::uint8_t, 4>{255U, 0U, 0U, 255U},
            "global model transform was not applied outside instance transforms");

    DrawModelPayload overlapping;
    overlapping.meshes = separated.meshes;
    overlapping.instances = {
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 300U,
            .modelTranslation = Vec3{0.0F, 0.0F, -1.0F},
        },
        DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference = 400U,
            .modelTranslation = Vec3{0.0F, 0.0F, 1.0F},
        },
    };
    const auto first = airfix::render::rasterizeDiagnosticModel(
        overlapping, textureViews, testOptions());
    const auto second = airfix::render::rasterizeDiagnosticModel(
        overlapping, textureViews, testOptions());
    require(first.pixels == second.pixels,
            "multi-instance diagnostic output is not deterministic");
    require(pixel(first, 4U, 4U) ==
                std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U},
            "near model instance did not win the shared depth test");
}

void testModelValidationAndAggregateLimits() {
    DrawModelPayload model;
    model.meshes = {coloredQuad(1U, 10U)};
    model.instances = {DrawMeshInstance{.meshSlot = 1U}};
    requireError(DiagnosticRasterizerErrorCode::missingMesh, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(
            model, {}, testOptions());
    }, "out-of-range model mesh slot was accepted");

    model.instances[0].meshSlot = 0U;
    model.instances[0].modelLinear.columns[1].y =
        std::numeric_limits<float>::quiet_NaN();
    requireError(DiagnosticRasterizerErrorCode::nonFiniteValue, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(
            model, {}, testOptions());
    }, "non-finite model instance transform was accepted");

    DrawModelPayload aggregate;
    aggregate.meshes = {coloredQuad(1U, 10U), coloredQuad(2U, 20U)};
    auto options = testOptions();
    options.maximumVertices = 7U;
    requireError(DiagnosticRasterizerErrorCode::limitExceeded, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(aggregate, {}, options);
    }, "aggregate mesh vertex limit was not enforced");

    options = testOptions();
    options.maximumMeshes = 1U;
    requireError(DiagnosticRasterizerErrorCode::limitExceeded, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(aggregate, {}, options);
    }, "mesh count limit was not enforced");

    aggregate.meshes.resize(1U);
    aggregate.instances = {
        DrawMeshInstance{.meshSlot = 0U},
        DrawMeshInstance{.meshSlot = 0U},
    };
    options = testOptions();
    options.maximumInstances = 1U;
    requireError(DiagnosticRasterizerErrorCode::limitExceeded, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(aggregate, {}, options);
    }, "instance count limit was not enforced");

    options = testOptions();
    options.maximumVertices = 7U;
    requireError(DiagnosticRasterizerErrorCode::limitExceeded, [&] {
        (void)airfix::render::rasterizeDiagnosticModel(aggregate, {}, options);
    }, "expanded instance vertex limit was not enforced");
}

} // namespace

int main() {
    try {
        testTwoByTwoUvCornersAndFlip();
        testDepthAndNoCulling();
        testFallbackAndDeterministicSummary();
        testValidationAndLimits();
        testModelSharedFitAndDepth();
        testModelValidationAndAggregateLimits();
        std::cout << "Diagnostic rasterizer tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Diagnostic rasterizer tests failed: " << error.what() << '\n';
        return 1;
    }
}
