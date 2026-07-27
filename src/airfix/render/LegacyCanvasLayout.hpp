#pragma once

#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyCanvasPoint final {
    float x{};
    float y{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyCanvasPoint&,
        const LegacyCanvasPoint&) noexcept = default;
};

struct LegacyCanvasRect final {
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyCanvasRect&,
        const LegacyCanvasRect&) noexcept = default;
};

// Evidence-derived constants recovered from the Windows binary. These values
// describe legacy content coordinates; they are not an iOS layout policy.
namespace recovered_legacy_canvas {

inline constexpr float width = 640.0F;
inline constexpr float height = 480.0F;

inline constexpr float gameplayLeft = 35.0F;
inline constexpr float gameplayTop = 35.0F;
inline constexpr float gameplayRight = 555.0F;
inline constexpr float gameplayBottom = 345.0F;
inline constexpr float gameplayWidth = gameplayRight - gameplayLeft;
inline constexpr float gameplayHeight = gameplayBottom - gameplayTop;
inline constexpr LegacyCanvasRect gameplayRect{
    gameplayLeft,
    gameplayTop,
    gameplayWidth,
    gameplayHeight,
};
inline constexpr LegacyCanvasPoint gameplayCentre{
    (gameplayLeft + gameplayRight) * 0.5F,
    (gameplayTop + gameplayBottom) * 0.5F,
};

} // namespace recovered_legacy_canvas

enum class LegacyCanvasLayoutBuildIssueKind : std::uint8_t {
    nonFiniteTargetRect,
    targetExtentNotPositive,
    nonFiniteDerivedLayout,
};

struct LegacyCanvasLayoutBuildIssue final {
    LegacyCanvasLayoutBuildIssueKind kind{
        LegacyCanvasLayoutBuildIssueKind::nonFiniteTargetRect};
};

class LegacyCanvasAspectFitLayout;

struct LegacyCanvasAspectFitBuildResult;

// Port policy: aspect-fit the recovered 640x480 canvas into a validated target
// rectangle. This policy is backend-neutral and does not account for UIKit,
// Metal, display scale, orientation, or safe-area insets.
[[nodiscard]] LegacyCanvasAspectFitBuildResult
buildLegacyCanvasAspectFitLayout(
    const LegacyCanvasRect& targetRect) noexcept;

class LegacyCanvasAspectFitLayout final {
public:
    [[nodiscard]] constexpr LegacyCanvasRect targetRect() const noexcept {
        return targetRect_;
    }

    [[nodiscard]] constexpr LegacyCanvasRect fittedRect() const noexcept {
        return fittedRect_;
    }

    [[nodiscard]] constexpr float scale() const noexcept {
        return scale_;
    }

private:
    friend LegacyCanvasAspectFitBuildResult
    buildLegacyCanvasAspectFitLayout(
        const LegacyCanvasRect& targetRect) noexcept;

    constexpr LegacyCanvasAspectFitLayout(
        const LegacyCanvasRect targetRect,
        const LegacyCanvasRect fittedRect,
        const float scale) noexcept
        : targetRect_(targetRect),
          fittedRect_(fittedRect),
          scale_(scale) {}

    const LegacyCanvasRect targetRect_;
    const LegacyCanvasRect fittedRect_;
    const float scale_;
};

struct LegacyCanvasAspectFitBuildResult final {
    std::optional<LegacyCanvasAspectFitLayout> layout;
    std::optional<LegacyCanvasLayoutBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return layout.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

} // namespace airfix::render
