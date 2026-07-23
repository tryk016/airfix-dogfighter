#pragma once

#include "airfix/render/TextureRuntimePlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class GtiUploadDataIssueKind : std::uint8_t {
    parseFailure,
    planFailure,
    decodeFailure,
    planMismatch,
    limitExceeded,
    integerOverflow,
};

struct GtiUploadDataIssue {
    GtiUploadDataIssueKind kind{GtiUploadDataIssueKind::parseFailure};
    std::optional<GtiUploadIssueKind> planIssue;
    std::optional<std::size_t> variantIndex;
    std::optional<std::uint32_t> level;
};

struct GtiUploadDataLimits {
    // Checked before parsing so an oversized untrusted source cannot cause
    // parser work or metadata allocation.
    std::size_t maximumSourceBytes{512U * 1024U * 1024U};
    // Forwarded unchanged to describeGtiUpload. Parser hard bounds remain
    // independently enforced by parseGti and describeGtiUpload.
    GtiUploadLimits upload{};
};

struct GtiUploadPreparation {
    // Both payload fields are published together only after the decoded data
    // has been checked against every field and byte total in the plan.
    std::optional<GtiUploadPlan> plan;
    std::vector<assets::RgbaImage> uploadLevels;
    std::vector<GtiUploadDataIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return plan.has_value() && issues.empty();
    }
};

// Parses, plans, and decodes one complete GTI source transaction. Authored
// chains decode every selected mip. Legacy dimension anomalies decode only the
// base level and leave lower-level generation to the eventual render backend.
// This function makes no sRGB, premultiplication, vertical-orientation, or
// blending decision.
[[nodiscard]] GtiUploadPreparation prepareGtiUpload(
    const TextureImportRequest& request,
    std::span<const std::uint8_t> gtiBytes,
    const GtiUploadDataLimits& limits = {});

} // namespace airfix::render
