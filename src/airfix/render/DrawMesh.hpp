#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::render {

struct Vec2 {
    float u{};
    float v{};

    [[nodiscard]] friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct DrawVertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};

    [[nodiscard]] friend constexpr bool operator==(
        const DrawVertex&,
        const DrawVertex&) = default;
};

struct TextureAssetId {
    std::uint32_t value{};

    [[nodiscard]] friend constexpr bool operator==(
        const TextureAssetId&,
        const TextureAssetId&) = default;
};

struct DrawMaterial {
    std::uint32_t sourceReference{};
    std::optional<TextureAssetId> primary;
    std::optional<TextureAssetId> secondary;
    std::optional<TextureAssetId> environment;

    [[nodiscard]] friend constexpr bool operator==(
        const DrawMaterial&,
        const DrawMaterial&) = default;
};

enum class TexcoordMode : std::uint8_t {
    none,
    uv0,
};

struct DrawRange {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::uint32_t materialSlot{};
    TexcoordMode texcoordMode{TexcoordMode::none};

    [[nodiscard]] friend constexpr bool operator==(
        const DrawRange&,
        const DrawRange&) = default;
};

struct Bounds3 {
    Vec3 minimum{};
    Vec3 maximum{};

    [[nodiscard]] friend constexpr bool operator==(const Bounds3&, const Bounds3&) = default;
};

struct DrawMeshPayload {
    std::vector<DrawVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawMaterial> materials;
    std::vector<DrawRange> ranges;
    // An empty payload uses zero for both corners.
    Bounds3 localBounds{};
};

struct DrawMeshLimits {
    std::size_t maximumVertices{3'000'000U};
    std::size_t maximumIndices{3'000'000U};
    std::size_t maximumMaterials{65'536U};
    std::size_t maximumRanges{1'000'000U};
    std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

enum class DrawMeshErrorCode : std::uint8_t {
    nonFiniteValue,
    vertexIndexOutOfRange,
    missingMaterial,
    duplicateMaterial,
    limitExceeded,
    integerOverflow,
};

class DrawMeshError final : public std::runtime_error {
public:
    DrawMeshError(DrawMeshErrorCode code, const std::string& message);

    [[nodiscard]] DrawMeshErrorCode code() const noexcept;

private:
    DrawMeshErrorCode code_;
};

// Produces a backend-neutral indexed mesh. Output vertices are split only at
// source-index, UV-presence/value, or flat-normal seams. Materials and draw
// ranges retain first-use and triangle order.
[[nodiscard]] DrawMeshPayload buildDrawMesh(
    const ConvertedMeshGeometry& geometry,
    std::span<const DrawMaterial> materialBindings,
    const DrawMeshLimits& limits = {});

} // namespace airfix::render
