#include "airfix/render/DiagnosticRasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace airfix::render {
namespace {

struct ProjectedVertex {
    double x{};
    double y{};
    double depth{};
};

struct ViewPoint {
    double x{};
    double y{};
    double z{};
};

[[noreturn]] void fail(
    const DiagnosticRasterizerErrorCode code,
    const std::string_view message) {
    throw DiagnosticRasterizerError(code, std::string(message));
}

[[nodiscard]] std::size_t checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    const std::string_view label) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        fail(DiagnosticRasterizerErrorCode::integerOverflow, label);
    }
    return left * right;
}

[[nodiscard]] std::size_t checkedAdd(
    const std::size_t left,
    const std::size_t right,
    const std::string_view label) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        fail(DiagnosticRasterizerErrorCode::integerOverflow, label);
    }
    return left + right;
}

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Vec2 value) noexcept {
    return finite(value.u) && finite(value.v);
}

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return std::ranges::all_of(value.columns, [](const Vec3 column) {
        return finite(column);
    });
}

[[nodiscard]] Vec3 transformModel(
    const DiagnosticRasterizerOptions& options,
    const Vec3 position) {
    Vec3 transformed = applyRuntimeColumn(options.modelLinear, position);
    transformed.x += options.modelTranslation.x;
    transformed.y += options.modelTranslation.y;
    transformed.z += options.modelTranslation.z;
    if (!finite(transformed)) {
        fail(DiagnosticRasterizerErrorCode::nonFiniteValue,
             "diagnostic model transform produced a non-finite value");
    }
    return transformed;
}

[[nodiscard]] ViewPoint rotateForView(
    const Vec3 position,
    const double sineYaw,
    const double cosineYaw,
    const double sinePitch,
    const double cosinePitch) noexcept {
    const double yawX = cosineYaw * position.x + sineYaw * position.z;
    const double yawZ = -sineYaw * position.x + cosineYaw * position.z;
    return ViewPoint{
        yawX,
        cosinePitch * position.y - sinePitch * yawZ,
        sinePitch * position.y + cosinePitch * yawZ,
    };
}

[[nodiscard]] double edge(
    const ProjectedVertex& first,
    const ProjectedVertex& second,
    const double x,
    const double y) noexcept {
    return (x - first.x) * (second.y - first.y) -
        (y - first.y) * (second.x - first.x);
}

void writeColor(
    std::vector<std::uint8_t>& pixels,
    const std::size_t pixelIndex,
    const std::array<std::uint8_t, 4>& color) {
    const std::size_t offset = pixelIndex * 4U;
    std::copy(color.begin(), color.end(), pixels.begin() +
        static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::array<std::uint8_t, 4> sampleNearest(
    const assets::RgbaImage& texture,
    double u,
    double v,
    const bool flipV) {
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(flipV ? 1.0 - v : v, 0.0, 1.0);
    const auto textureX = std::min(
        static_cast<std::uint32_t>(u * texture.width), texture.width - 1U);
    const auto textureY = std::min(
        static_cast<std::uint32_t>(v * texture.height), texture.height - 1U);
    const std::size_t pixelIndex =
        static_cast<std::size_t>(textureY) * texture.width + textureX;
    const std::size_t offset = pixelIndex * 4U;
    return {
        texture.pixels[offset],
        texture.pixels[offset + 1U],
        texture.pixels[offset + 2U],
        texture.pixels[offset + 3U],
    };
}

} // namespace

DiagnosticRasterizerError::DiagnosticRasterizerError(
    const DiagnosticRasterizerErrorCode code,
    const std::string& message)
    : std::runtime_error(message), code_(code) {}

DiagnosticRasterizerErrorCode DiagnosticRasterizerError::code() const noexcept {
    return code_;
}

assets::RgbaImage rasterizeDiagnostic(
    const DrawMeshPayload& mesh,
    const std::span<const DiagnosticTextureView> textures,
    const DiagnosticRasterizerOptions& options) {
    if (options.width == 0U || options.height == 0U) {
        fail(DiagnosticRasterizerErrorCode::invalidDimensions,
             "diagnostic dimensions must be non-zero");
    }
    if (!finite(options.yawRadians) || !finite(options.pitchRadians) ||
        !finite(options.modelLinear) || !finite(options.modelTranslation)) {
        fail(DiagnosticRasterizerErrorCode::nonFiniteValue,
             "diagnostic view or model transform is non-finite");
    }

    const std::size_t pixelCount = checkedMultiply(
        options.width, options.height, "diagnostic pixel count overflows");
    if (pixelCount > options.maximumPixels) {
        fail(DiagnosticRasterizerErrorCode::limitExceeded,
             "diagnostic pixel limit exceeded");
    }
    if (mesh.vertices.size() > options.maximumVertices ||
        mesh.indices.size() > options.maximumIndices ||
        mesh.materials.size() > options.maximumMaterials ||
        mesh.ranges.size() > options.maximumRanges ||
        textures.size() > options.maximumTextures) {
        fail(DiagnosticRasterizerErrorCode::limitExceeded,
             "diagnostic input limit exceeded");
    }

    const std::size_t outputBytes = checkedMultiply(
        pixelCount, 4U, "diagnostic output byte count overflows");
    const std::size_t depthBytes = checkedMultiply(
        pixelCount, sizeof(double), "diagnostic depth byte count overflows");
    const std::size_t projectedBytes = checkedMultiply(
        mesh.vertices.size(), sizeof(ProjectedVertex),
        "diagnostic projected vertex byte count overflows");
    const std::size_t workingBytes = checkedAdd(
        checkedAdd(outputBytes, depthBytes, "diagnostic working size overflows"),
        projectedBytes,
        "diagnostic working size overflows");
    if (workingBytes > options.maximumWorkingBytes) {
        fail(DiagnosticRasterizerErrorCode::limitExceeded,
             "diagnostic working byte limit exceeded");
    }

    const auto& bounds = mesh.localBounds;
    if (!finite(bounds.minimum) || !finite(bounds.maximum)) {
        fail(DiagnosticRasterizerErrorCode::nonFiniteValue,
             "diagnostic bounds contain a non-finite value");
    }
    if (bounds.minimum.x > bounds.maximum.x ||
        bounds.minimum.y > bounds.maximum.y ||
        bounds.minimum.z > bounds.maximum.z) {
        fail(DiagnosticRasterizerErrorCode::invalidBounds,
             "diagnostic bounds are inverted");
    }

    for (const auto& vertex : mesh.vertices) {
        if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.uv)) {
            fail(DiagnosticRasterizerErrorCode::nonFiniteValue,
                 "diagnostic vertex contains a non-finite value");
        }
        if (vertex.position.x < bounds.minimum.x ||
            vertex.position.y < bounds.minimum.y ||
            vertex.position.z < bounds.minimum.z ||
            vertex.position.x > bounds.maximum.x ||
            vertex.position.y > bounds.maximum.y ||
            vertex.position.z > bounds.maximum.z) {
            fail(DiagnosticRasterizerErrorCode::invalidBounds,
                 "diagnostic bounds do not contain every vertex");
        }
    }

    std::unordered_set<std::uint32_t> materialReferences;
    materialReferences.reserve(mesh.materials.size());
    for (const auto& material : mesh.materials) {
        if (!materialReferences.insert(material.sourceReference).second) {
            fail(DiagnosticRasterizerErrorCode::duplicateMaterial,
                 "diagnostic material reference is duplicated");
        }
    }

    std::unordered_map<std::uint32_t, const assets::RgbaImage*> textureById;
    textureById.reserve(textures.size());
    std::size_t totalTexturePixels = 0U;
    for (const auto& texture : textures) {
        if (texture.image == nullptr) {
            fail(DiagnosticRasterizerErrorCode::invalidTexture,
                 "diagnostic texture view is null");
        }
        const auto [iterator, inserted] = textureById.emplace(
            texture.id.value, texture.image);
        (void)iterator;
        if (!inserted) {
            fail(DiagnosticRasterizerErrorCode::duplicateTexture,
                 "diagnostic texture id is duplicated");
        }
        const auto& image = *texture.image;
        if (image.width == 0U || image.height == 0U) {
            fail(DiagnosticRasterizerErrorCode::invalidTexture,
                 "diagnostic texture dimensions are zero");
        }
        const std::size_t texturePixels = checkedMultiply(
            image.width, image.height, "diagnostic texture size overflows");
        const std::size_t textureBytes = checkedMultiply(
            texturePixels, 4U, "diagnostic texture byte count overflows");
        if (image.pixels.size() != textureBytes) {
            fail(DiagnosticRasterizerErrorCode::invalidTexture,
                 "diagnostic texture RGBA byte count is invalid");
        }
        totalTexturePixels = checkedAdd(
            totalTexturePixels, texturePixels,
            "diagnostic aggregate texture size overflows");
        if (totalTexturePixels > options.maximumTexturePixels) {
            fail(DiagnosticRasterizerErrorCode::limitExceeded,
                 "diagnostic texture pixel limit exceeded");
        }
    }

    if (mesh.indices.size() % 3U != 0U) {
        fail(DiagnosticRasterizerErrorCode::malformedMesh,
             "diagnostic index count is not divisible by three");
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            fail(DiagnosticRasterizerErrorCode::malformedMesh,
                 "diagnostic vertex index is out of range");
        }
    }

    std::size_t expectedFirstIndex = 0U;
    for (const auto& range : mesh.ranges) {
        if (range.firstIndex != expectedFirstIndex ||
            range.firstIndex % 3U != 0U || range.indexCount == 0U ||
            range.indexCount % 3U != 0U) {
            fail(DiagnosticRasterizerErrorCode::malformedMesh,
                 "diagnostic draw ranges are not a contiguous triangle partition");
        }
        const std::size_t rangeEnd = checkedAdd(
            range.firstIndex, range.indexCount,
            "diagnostic range end overflows");
        if (rangeEnd > mesh.indices.size()) {
            fail(DiagnosticRasterizerErrorCode::malformedMesh,
                 "diagnostic draw range exceeds the index buffer");
        }
        if (range.materialSlot >= mesh.materials.size()) {
            fail(DiagnosticRasterizerErrorCode::missingMaterial,
                 "diagnostic draw range references a missing material");
        }
        switch (range.texcoordMode) {
        case TexcoordMode::none:
        case TexcoordMode::uv0:
            break;
        default:
            fail(DiagnosticRasterizerErrorCode::malformedMesh,
                 "diagnostic draw range has an unknown texture-coordinate mode");
        }
        expectedFirstIndex = rangeEnd;
    }
    if (expectedFirstIndex != mesh.indices.size()) {
        fail(DiagnosticRasterizerErrorCode::malformedMesh,
             "diagnostic draw ranges do not cover the index buffer");
    }
    if (mesh.indices.empty() != mesh.ranges.empty()) {
        fail(DiagnosticRasterizerErrorCode::malformedMesh,
             "diagnostic empty index/range state is inconsistent");
    }

    for (const auto& material : mesh.materials) {
        if (material.primary.has_value() &&
            !textureById.contains(material.primary->value)) {
            fail(DiagnosticRasterizerErrorCode::missingTexture,
                 "diagnostic primary texture is missing");
        }
    }

    assets::RgbaImage result{
        .width = options.width,
        .height = options.height,
        .pixels = std::vector<std::uint8_t>(outputBytes),
    };
    for (std::size_t index = 0U; index < pixelCount; ++index) {
        writeColor(result.pixels, index, options.backgroundColor);
    }
    if (mesh.vertices.empty()) {
        return result;
    }

    const double sineYaw = std::sin(options.yawRadians);
    const double cosineYaw = std::cos(options.yawRadians);
    const double sinePitch = std::sin(options.pitchRadians);
    const double cosinePitch = std::cos(options.pitchRadians);

    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (const float x : {bounds.minimum.x, bounds.maximum.x}) {
        for (const float y : {bounds.minimum.y, bounds.maximum.y}) {
            for (const float z : {bounds.minimum.z, bounds.maximum.z}) {
                const ViewPoint corner = rotateForView(
                    transformModel(options, Vec3{x, y, z}),
                    sineYaw, cosineYaw, sinePitch, cosinePitch);
                minimumX = std::min(minimumX, corner.x);
                maximumX = std::max(maximumX, corner.x);
                minimumY = std::min(minimumY, corner.y);
                maximumY = std::max(maximumY, corner.y);
            }
        }
    }
    const double centerX = (minimumX + maximumX) * 0.5;
    const double centerY = (minimumY + maximumY) * 0.5;
    const double extentX = maximumX - minimumX;
    const double extentY = maximumY - minimumY;
    constexpr double kFitFraction = 0.90;
    double scale = std::numeric_limits<double>::infinity();
    if (extentX > 0.0) {
        scale = std::min(scale, options.width * kFitFraction / extentX);
    }
    if (extentY > 0.0) {
        scale = std::min(scale, options.height * kFitFraction / extentY);
    }
    if (!std::isfinite(scale)) {
        scale = 1.0;
    }

    std::vector<ProjectedVertex> projected;
    projected.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        const ViewPoint view = rotateForView(
            transformModel(options, vertex.position),
            sineYaw, cosineYaw, sinePitch, cosinePitch);
        const ProjectedVertex projectedVertex{
            options.width * 0.5 + (view.x - centerX) * scale,
            options.height * 0.5 - (view.y - centerY) * scale,
            view.z,
        };
        if (!std::isfinite(projectedVertex.x) ||
            !std::isfinite(projectedVertex.y) ||
            !std::isfinite(projectedVertex.depth)) {
            fail(DiagnosticRasterizerErrorCode::nonFiniteValue,
                 "diagnostic projection produced a non-finite value");
        }
        projected.push_back(projectedVertex);
    }

    std::vector<double> depthBuffer(
        pixelCount, -std::numeric_limits<double>::infinity());
    constexpr double kEdgeTolerance = 1.0e-12;
    for (const auto& range : mesh.ranges) {
        const auto& material = mesh.materials[range.materialSlot];
        const assets::RgbaImage* texture = nullptr;
        if (range.texcoordMode == TexcoordMode::uv0 && material.primary.has_value()) {
            texture = textureById.at(material.primary->value);
        }

        const std::size_t rangeEnd =
            static_cast<std::size_t>(range.firstIndex) + range.indexCount;
        for (std::size_t first = range.firstIndex; first < rangeEnd; first += 3U) {
            const auto index0 = mesh.indices[first];
            const auto index1 = mesh.indices[first + 1U];
            const auto index2 = mesh.indices[first + 2U];
            const auto& point0 = projected[index0];
            const auto& point1 = projected[index1];
            const auto& point2 = projected[index2];
            const double area = edge(point0, point1, point2.x, point2.y);
            if (area == 0.0 || !std::isfinite(area)) {
                continue;
            }

            const double minimumScreenX = std::min({point0.x, point1.x, point2.x});
            const double maximumScreenX = std::max({point0.x, point1.x, point2.x});
            const double minimumScreenY = std::min({point0.y, point1.y, point2.y});
            const double maximumScreenY = std::max({point0.y, point1.y, point2.y});
            const auto firstX = static_cast<std::int64_t>(std::max(
                0.0, std::ceil(minimumScreenX - 0.5)));
            const auto lastX = static_cast<std::int64_t>(std::min(
                static_cast<double>(options.width - 1U),
                std::floor(maximumScreenX - 0.5)));
            const auto firstY = static_cast<std::int64_t>(std::max(
                0.0, std::ceil(minimumScreenY - 0.5)));
            const auto lastY = static_cast<std::int64_t>(std::min(
                static_cast<double>(options.height - 1U),
                std::floor(maximumScreenY - 0.5)));

            for (std::int64_t y = firstY; y <= lastY; ++y) {
                for (std::int64_t x = firstX; x <= lastX; ++x) {
                    const double sampleX = static_cast<double>(x) + 0.5;
                    const double sampleY = static_cast<double>(y) + 0.5;
                    const double weight0 = edge(point1, point2, sampleX, sampleY) / area;
                    const double weight1 = edge(point2, point0, sampleX, sampleY) / area;
                    const double weight2 = edge(point0, point1, sampleX, sampleY) / area;
                    if (weight0 < -kEdgeTolerance || weight1 < -kEdgeTolerance ||
                        weight2 < -kEdgeTolerance) {
                        continue;
                    }

                    const double depth = weight0 * point0.depth +
                        weight1 * point1.depth + weight2 * point2.depth;
                    const std::size_t pixelIndex = static_cast<std::size_t>(y) *
                        options.width + static_cast<std::size_t>(x);
                    if (depth <= depthBuffer[pixelIndex]) {
                        continue;
                    }
                    depthBuffer[pixelIndex] = depth;

                    if (texture == nullptr) {
                        writeColor(result.pixels, pixelIndex, options.fallbackColor);
                        continue;
                    }
                    const auto& vertex0 = mesh.vertices[index0];
                    const auto& vertex1 = mesh.vertices[index1];
                    const auto& vertex2 = mesh.vertices[index2];
                    const double u = weight0 * vertex0.uv.u +
                        weight1 * vertex1.uv.u + weight2 * vertex2.uv.u;
                    const double v = weight0 * vertex0.uv.v +
                        weight1 * vertex1.uv.v + weight2 * vertex2.uv.v;
                    writeColor(
                        result.pixels, pixelIndex,
                        sampleNearest(*texture, u, v, options.flipV));
                }
            }
        }
    }
    return result;
}

} // namespace airfix::render
