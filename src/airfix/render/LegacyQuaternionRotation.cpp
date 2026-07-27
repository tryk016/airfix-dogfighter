#include "airfix/render/LegacyQuaternionRotation.hpp"

#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

[[nodiscard]] bool finite(
    const LegacyQuaternion& value) noexcept {
    return std::isfinite(value.w) && std::isfinite(value.x) &&
        std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool representableAsFloat(
    const double value) noexcept {
    constexpr double maximumFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    return std::isfinite(value) &&
        value >= -maximumFloat && value <= maximumFloat;
}

[[nodiscard]] LegacyQuaternionRotationResult failure(
    const LegacyQuaternionRotationIssueKind kind) noexcept {
    return {
        .rotation = std::nullopt,
        .issue = LegacyQuaternionRotationIssue{.kind = kind},
    };
}

} // namespace

LegacyQuaternionRotationResult legacyQuaternionRotation(
    const LegacyQuaternion& quaternion) noexcept {
    if (!finite(quaternion)) {
        return failure(
            LegacyQuaternionRotationIssueKind::nonFiniteInput);
    }

    const double w = quaternion.w;
    const double x = quaternion.x;
    const double y = quaternion.y;
    const double z = quaternion.z;

    const double yyPlusZz = y * y + z * z;
    const double xyPlusWz = x * y + w * z;
    const double xzMinusWy = x * z - w * y;

    const double xyMinusWz = x * y - w * z;
    const double xxPlusZz = x * x + z * z;
    const double xwPlusZy = x * w + z * y;

    const double xzPlusWy = x * z + w * y;
    const double zyMinusXw = z * y - x * w;
    const double xxPlusYy = x * x + y * y;

    const double m00 = 1.0 - (yyPlusZz + yyPlusZz);
    const double m10 = xyPlusWz + xyPlusWz;
    const double m20 = xzMinusWy + xzMinusWy;
    const double m01 = xyMinusWz + xyMinusWz;
    const double m11 = 1.0 - (xxPlusZz + xxPlusZz);
    const double m21 = xwPlusZy + xwPlusZy;
    const double m02 = xzPlusWy + xzPlusWy;
    const double m12 = zyMinusXw + zyMinusXw;
    const double m22 = 1.0 - (xxPlusYy + xxPlusYy);

    if (!representableAsFloat(m00) ||
        !representableAsFloat(m10) ||
        !representableAsFloat(m20) ||
        !representableAsFloat(m01) ||
        !representableAsFloat(m11) ||
        !representableAsFloat(m21) ||
        !representableAsFloat(m02) ||
        !representableAsFloat(m12) ||
        !representableAsFloat(m22)) {
        return failure(
            LegacyQuaternionRotationIssueKind::nonFiniteOutput);
    }

    const Mat3 rotation{{
        Vec3{
            static_cast<float>(m00),
            static_cast<float>(m10),
            static_cast<float>(m20),
        },
        Vec3{
            static_cast<float>(m01),
            static_cast<float>(m11),
            static_cast<float>(m21),
        },
        Vec3{
            static_cast<float>(m02),
            static_cast<float>(m12),
            static_cast<float>(m22),
        },
    }};

    if (!finite(rotation)) {
        return failure(
            LegacyQuaternionRotationIssueKind::nonFiniteOutput);
    }

    return {
        .rotation = rotation,
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
