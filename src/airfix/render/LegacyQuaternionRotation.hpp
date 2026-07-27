#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

// CcMatrixRot::FromQuat consumes fields in (w, x, y, z) order. The legacy
// routine does not normalize the value before constructing its matrix.
struct LegacyQuaternion final {
    float w{1.0F};
    float x{};
    float y{};
    float z{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyQuaternion&,
        const LegacyQuaternion&) noexcept = default;
};

enum class LegacyQuaternionRotationIssueKind : std::uint8_t {
    nonFiniteInput,
    nonFiniteOutput,
};

struct LegacyQuaternionRotationIssue final {
    LegacyQuaternionRotationIssueKind kind{
        LegacyQuaternionRotationIssueKind::nonFiniteInput};
};

struct LegacyQuaternionRotationResult final {
    std::optional<Mat3> rotation;
    std::optional<LegacyQuaternionRotationIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return rotation.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

// Reconstructs the 3x3 portion of CcMatrixRot::FromQuat as a mathematical
// column matrix for applyRuntimeColumn. Binary32 inputs are widened to avoid
// premature binary32 rounding while preserving the recovered expression
// grouping. This portable approximation does not claim bit-identical x87
// extended precision for every input.
[[nodiscard]] LegacyQuaternionRotationResult
legacyQuaternionRotation(
    const LegacyQuaternion& quaternion) noexcept;

} // namespace airfix::render
