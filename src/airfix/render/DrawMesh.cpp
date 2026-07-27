#include "airfix/render/DrawMesh.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <limits>
#include <map>
#include <string_view>

namespace airfix::render {
namespace {

struct VertexKey {
    std::uint32_t sourceIndex{};
    bool hasUv{};
    std::array<std::uint32_t, 2> uvBits{};
    std::array<std::uint32_t, 3> normalBits{};

    [[nodiscard]] friend constexpr auto operator<=>(
        const VertexKey&,
        const VertexKey&) = default;
};

[[noreturn]] void fail(const DrawMeshErrorCode code, const std::string_view message) {
    throw DrawMeshError(code, std::string(message));
}

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] std::uint32_t canonicalFloatBits(const float value) noexcept {
    // Equality treats both signed zero encodings as the same seam value.
    return std::bit_cast<std::uint32_t>(value == 0.0F ? 0.0F : value);
}

[[nodiscard]] std::size_t checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    const std::string_view label) {
    if (right != 0U && left > std::numeric_limits<std::size_t>::max() / right) {
        fail(DrawMeshErrorCode::integerOverflow,
             std::string(label) + " byte/count calculation overflowed");
    }
    return left * right;
}

void accountBytes(
    std::size_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const DrawMeshLimits& limits) {
    const std::size_t addition = checkedMultiply(count, elementSize, "draw payload");
    if (addition > std::numeric_limits<std::size_t>::max() - total) {
        fail(DrawMeshErrorCode::integerOverflow,
             "draw payload byte calculation overflowed");
    }
    total += addition;
    if (total > limits.maximumTotalBytes) {
        fail(DrawMeshErrorCode::limitExceeded, "draw payload exceeds its byte limit");
    }
}

[[nodiscard]] std::uint32_t checkedU32(
    const std::size_t value,
    const std::string_view label) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        fail(DrawMeshErrorCode::integerOverflow,
             std::string(label) + " does not fit in uint32_t");
    }
    return static_cast<std::uint32_t>(value);
}

void requireOutputCapacity(
    const std::size_t current,
    const std::size_t maximum,
    const std::string_view label) {
    if (current >= maximum) {
        fail(DrawMeshErrorCode::limitExceeded,
             std::string("draw payload exceeds its ") + std::string(label) + " limit");
    }
}

[[nodiscard]] VertexKey makeVertexKey(
    const std::uint32_t sourceIndex,
    const std::optional<std::array<float, 6>>& coordinates,
    const std::size_t corner,
    const Vec3 normal) noexcept {
    VertexKey key;
    key.sourceIndex = sourceIndex;
    key.hasUv = coordinates.has_value();
    if (coordinates.has_value()) {
        key.uvBits = {
            canonicalFloatBits((*coordinates)[corner * 2U]),
            canonicalFloatBits((*coordinates)[corner * 2U + 1U]),
        };
    }
    key.normalBits = {
        canonicalFloatBits(normal.x),
        canonicalFloatBits(normal.y),
        canonicalFloatBits(normal.z),
    };
    return key;
}

void updateBounds(Bounds3& bounds, const Vec3 position, const bool first) noexcept {
    if (first) {
        bounds.minimum = position;
        bounds.maximum = position;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, position.x);
    bounds.minimum.y = std::min(bounds.minimum.y, position.y);
    bounds.minimum.z = std::min(bounds.minimum.z, position.z);
    bounds.maximum.x = std::max(bounds.maximum.x, position.x);
    bounds.maximum.y = std::max(bounds.maximum.y, position.y);
    bounds.maximum.z = std::max(bounds.maximum.z, position.z);
}

} // namespace

DrawMeshError::DrawMeshError(const DrawMeshErrorCode code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

DrawMeshErrorCode DrawMeshError::code() const noexcept {
    return code_;
}

DrawMeshPayload buildDrawMesh(
    const ConvertedMeshGeometry& geometry,
    const std::span<const DrawMaterial> materialBindings,
    const DrawMeshLimits& limits) {
    if (materialBindings.size() > limits.maximumMaterials) {
        fail(DrawMeshErrorCode::limitExceeded, "material bindings exceed the material limit");
    }

    std::map<std::uint32_t, const DrawMaterial*> bindingsByReference;
    for (const DrawMaterial& binding : materialBindings) {
        const auto [unused, inserted] = bindingsByReference.emplace(
            binding.sourceReference, &binding);
        (void)unused;
        if (!inserted) {
            fail(DrawMeshErrorCode::duplicateMaterial,
                 "material bindings contain a duplicate source reference");
        }
    }

    for (const ConvertedMeshVertex& vertex : geometry.vertices) {
        if (!finite(vertex.position)) {
            fail(DrawMeshErrorCode::nonFiniteValue,
                 "source vertex position contains a non-finite value");
        }
    }

    const std::size_t expectedIndices = checkedMultiply(
        geometry.triangles.size(), 3U, "triangle index");
    if (expectedIndices > limits.maximumIndices) {
        fail(DrawMeshErrorCode::limitExceeded, "draw payload exceeds its index limit");
    }
    (void)checkedU32(expectedIndices, "index count");
    const std::size_t indexBytes = checkedMultiply(
        expectedIndices, sizeof(std::uint32_t), "index buffer");
    if (indexBytes > limits.maximumTotalBytes) {
        fail(DrawMeshErrorCode::limitExceeded, "draw payload exceeds its byte limit");
    }

    DrawMeshPayload result;
    result.indices.reserve(expectedIndices);
    std::map<VertexKey, std::uint32_t> vertexSlots;
    std::map<std::uint32_t, std::uint32_t> materialSlots;
    std::size_t totalBytes = 0U;

    for (const ConvertedMeshTriangle& triangle : geometry.triangles) {
        if (!finite(triangle.faceNormal)) {
            fail(DrawMeshErrorCode::nonFiniteValue,
                 "triangle normal contains a non-finite value");
        }
        if (triangle.textureCoordinates.has_value()) {
            for (const float coordinate : *triangle.textureCoordinates) {
                if (!finite(coordinate)) {
                    fail(DrawMeshErrorCode::nonFiniteValue,
                         "triangle texture coordinates contain a non-finite value");
                }
            }
        }

        const auto bindingIt = bindingsByReference.find(triangle.materialReference);
        if (bindingIt == bindingsByReference.end()) {
            fail(DrawMeshErrorCode::missingMaterial,
                 "triangle refers to a material without a binding");
        }

        std::uint32_t materialSlot{};
        const auto existingMaterial = materialSlots.find(triangle.materialReference);
        if (existingMaterial == materialSlots.end()) {
            requireOutputCapacity(
                result.materials.size(), limits.maximumMaterials, "material");
            materialSlot = checkedU32(result.materials.size(), "material slot");
            accountBytes(totalBytes, 1U, sizeof(DrawMaterial), limits);
            result.materials.push_back(*bindingIt->second);
            materialSlots.emplace(triangle.materialReference, materialSlot);
        }
        else {
            materialSlot = existingMaterial->second;
        }

        std::array<std::uint32_t, 3> drawIndices{};
        for (std::size_t corner = 0U; corner < drawIndices.size(); ++corner) {
            const std::uint32_t sourceIndex = triangle.vertexIndices[corner];
            if (sourceIndex >= geometry.vertices.size()) {
                fail(DrawMeshErrorCode::vertexIndexOutOfRange,
                     "triangle vertex index is out of range");
            }

            const VertexKey key = makeVertexKey(
                sourceIndex, triangle.textureCoordinates, corner, triangle.faceNormal);
            const auto existingVertex = vertexSlots.find(key);
            if (existingVertex != vertexSlots.end()) {
                drawIndices[corner] = existingVertex->second;
                continue;
            }

            requireOutputCapacity(
                result.vertices.size(), limits.maximumVertices, "vertex");
            const std::uint32_t drawIndex = checkedU32(
                result.vertices.size(), "draw vertex index");
            DrawVertex drawVertex{
                geometry.vertices[sourceIndex].position,
                triangle.faceNormal,
                {},
            };
            if (triangle.textureCoordinates.has_value()) {
                drawVertex.uv = Vec2{
                    (*triangle.textureCoordinates)[corner * 2U],
                    (*triangle.textureCoordinates)[corner * 2U + 1U],
                };
            }
            accountBytes(totalBytes, 1U, sizeof(DrawVertex), limits);
            updateBounds(result.localBounds, drawVertex.position, result.vertices.empty());
            result.vertices.push_back(drawVertex);
            vertexSlots.emplace(key, drawIndex);
            drawIndices[corner] = drawIndex;
        }

        const TexcoordMode texcoordMode = triangle.textureCoordinates.has_value()
            ? TexcoordMode::uv0
            : TexcoordMode::none;
        const bool continuesRange = !result.ranges.empty() &&
            result.ranges.back().materialSlot == materialSlot &&
            result.ranges.back().texcoordMode == texcoordMode;
        if (continuesRange) {
            if (result.ranges.back().indexCount >
                std::numeric_limits<std::uint32_t>::max() - 3U) {
                fail(DrawMeshErrorCode::integerOverflow,
                     "draw range index count overflowed");
            }
            result.ranges.back().indexCount += 3U;
        }
        else {
            requireOutputCapacity(result.ranges.size(), limits.maximumRanges, "range");
            accountBytes(totalBytes, 1U, sizeof(DrawRange), limits);
            result.ranges.push_back(DrawRange{
                checkedU32(result.indices.size(), "range first index"),
                3U,
                materialSlot,
                texcoordMode,
            });
        }

        accountBytes(totalBytes, drawIndices.size(), sizeof(std::uint32_t), limits);
        result.indices.insert(result.indices.end(), drawIndices.begin(), drawIndices.end());
    }

    return result;
}

} // namespace airfix::render
