#pragma once

#include "airfix/render/DrawMesh.hpp"

#include <cstdint>
#include <vector>

namespace airfix::render {

// A model is an immutable set of reusable mesh payloads plus an ordered list
// of placed mesh instances. Transforms use the same column-vector convention
// as DrawMesh and LegacyGeometry.
struct DrawMeshInstance {
    std::uint32_t meshSlot{};
    std::uint32_t sourceNodeReference{};
    Mat3 modelLinear{};
    Vec3 modelTranslation{};

    [[nodiscard]] friend constexpr bool operator==(
        const DrawMeshInstance&,
        const DrawMeshInstance&) = default;
};

struct DrawModelPayload {
    std::vector<DrawMeshPayload> meshes;
    std::vector<DrawMeshInstance> instances;
};

} // namespace airfix::render
