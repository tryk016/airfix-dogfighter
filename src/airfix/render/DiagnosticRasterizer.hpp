#pragma once

#include "airfix/assets/LegacyFormats.hpp"
#include "airfix/render/DrawMesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace airfix::render {

struct DiagnosticTextureView {
    TextureAssetId id{};
    const assets::RgbaImage* image{};
};

struct DiagnosticRasterizerOptions {
    std::uint32_t width{512U};
    std::uint32_t height{512U};
    float yawRadians{0.785398163F};
    float pitchRadians{-0.523598776F};
    Mat3 modelLinear{};
    Vec3 modelTranslation{};
    bool flipV{};
    std::array<std::uint8_t, 4> backgroundColor{9U, 14U, 22U, 255U};
    std::array<std::uint8_t, 4> fallbackColor{224U, 96U, 160U, 255U};
    std::size_t maximumPixels{16U * 1024U * 1024U};
    std::size_t maximumVertices{3'000'000U};
    std::size_t maximumIndices{3'000'000U};
    std::size_t maximumMaterials{65'536U};
    std::size_t maximumRanges{1'000'000U};
    std::size_t maximumTextures{65'536U};
    std::size_t maximumTexturePixels{64U * 1024U * 1024U};
    std::size_t maximumWorkingBytes{512U * 1024U * 1024U};
};

enum class DiagnosticRasterizerErrorCode : std::uint8_t {
    invalidDimensions,
    nonFiniteValue,
    invalidBounds,
    malformedMesh,
    missingMaterial,
    duplicateMaterial,
    missingTexture,
    duplicateTexture,
    invalidTexture,
    limitExceeded,
    integerOverflow,
};

class DiagnosticRasterizerError final : public std::runtime_error {
public:
    DiagnosticRasterizerError(
        DiagnosticRasterizerErrorCode code,
        const std::string& message);

    [[nodiscard]] DiagnosticRasterizerErrorCode code() const noexcept;

private:
    DiagnosticRasterizerErrorCode code_;
};

// Produces an owned RGBA8 diagnostic image. The rasterizer is deliberately
// unlit and API-neutral: it uses only the primary texture, or the fixed
// fallback color when a range has no primary texture/UVs. It performs no I/O.
[[nodiscard]] assets::RgbaImage rasterizeDiagnostic(
    const DrawMeshPayload& mesh,
    std::span<const DiagnosticTextureView> textures,
    const DiagnosticRasterizerOptions& options = {});

} // namespace airfix::render
