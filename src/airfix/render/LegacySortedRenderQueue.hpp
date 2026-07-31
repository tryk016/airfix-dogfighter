#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class LegacySortedRenderItemKind : std::uint8_t {
    triangle,
    sprite,
    custom,
};

// Identifies one immutable payload in a caller-owned table for the selected
// item kind. The queue deliberately does not interpret or dereference it.
struct LegacySortedRenderItemReference {
    LegacySortedRenderItemKind kind{LegacySortedRenderItemKind::triangle};
    std::uint32_t payloadIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacySortedRenderItemReference&,
        const LegacySortedRenderItemReference&) = default;
};

struct LegacySortedRenderItem {
    std::uint32_t sortKey{};
    LegacySortedRenderItemReference reference{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacySortedRenderItem&,
        const LegacySortedRenderItem&) = default;
};

struct LegacySortedRenderQueue {
    std::vector<LegacySortedRenderItem> items;
};

enum class LegacySortedRenderQueueIssueKind : std::uint8_t {
    limitExceeded,
    integerOverflow,
    invalidItemKind,
};

struct LegacySortedRenderQueueIssue {
    LegacySortedRenderQueueIssueKind kind{
        LegacySortedRenderQueueIssueKind::limitExceeded};
    std::optional<std::size_t> inputChainIndex;
};

struct LegacySortedRenderQueueLimits {
    std::size_t maximumItems{1'000'000U};
    // The portable four-pass radix implementation uses two complete item
    // arrays. This limit covers both arrays before either allocation occurs;
    // the native intrusive implementation instead used two banks of bucket
    // heads.
    std::size_t maximumWorkingBytes{64U * 1024U * 1024U};
};

struct LegacySortedRenderQueueDescription {
    // Any issue suppresses the complete queue atomically.
    std::optional<LegacySortedRenderQueue> queue;
    std::vector<LegacySortedRenderQueueIssue> issues;
};

// Reproduces only the integer arithmetic after native floating-point-to-long
// conversion. The caller must supply an already converted signed value; this
// helper makes no claim about process-wide x87 precision, rounding, exception,
// NaN, infinity, or overflow behavior.
[[nodiscard]] constexpr std::uint32_t
legacySortedRenderKeyFromQuantizedDepth(
    const std::int32_t quantizedDepth) noexcept {
    return std::uint32_t{0x80000000U} -
        static_cast<std::uint32_t>(quantizedDepth);
}

// Input is the already constructed native input-chain order for exactly one
// RenderRoom scope. Native producers prepend each discovered item before this
// boundary; callers that discover front-to-back must therefore provide the
// reverse discovery sequence. CcLinkSort itself performs a stable four-pass
// unsigned LSD radix sort, so equal keys retain the supplied chain order.
// Different item kinds remain in one queue and are never regrouped.
[[nodiscard]] LegacySortedRenderQueueDescription buildLegacySortedRenderQueue(
    std::span<const LegacySortedRenderItem> inputChainOrder,
    const LegacySortedRenderQueueLimits& limits = {});

} // namespace airfix::render
