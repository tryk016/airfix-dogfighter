#include "airfix/render/LegacySortedRenderQueue.hpp"

#include <array>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    LegacySortedRenderQueueDescription& result,
    const LegacySortedRenderQueueIssueKind kind,
    const std::optional<std::size_t> inputChainIndex = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .inputChainIndex = inputChainIndex,
    });
}

[[nodiscard]] bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool validKind(
    const LegacySortedRenderItemKind kind) noexcept {
    return kind == LegacySortedRenderItemKind::triangle ||
        kind == LegacySortedRenderItemKind::sprite ||
        kind == LegacySortedRenderItemKind::custom;
}

} // namespace

LegacySortedRenderQueueDescription buildLegacySortedRenderQueue(
    const std::span<const LegacySortedRenderItem> inputChainOrder,
    const LegacySortedRenderQueueLimits& limits) {
    LegacySortedRenderQueueDescription result;

    if (inputChainOrder.size() > limits.maximumItems) {
        addIssue(result, LegacySortedRenderQueueIssueKind::limitExceeded);
        return result;
    }

    std::size_t arrayBytes = 0U;
    std::size_t workingBytes = 0U;
    if (!checkedMultiply(
            inputChainOrder.size(),
            sizeof(LegacySortedRenderItem),
            arrayBytes) ||
        !checkedMultiply(arrayBytes, 2U, workingBytes)) {
        addIssue(result, LegacySortedRenderQueueIssueKind::integerOverflow);
        return result;
    }
    if (workingBytes > limits.maximumWorkingBytes) {
        addIssue(result, LegacySortedRenderQueueIssueKind::limitExceeded);
        return result;
    }

    for (std::size_t index = 0U; index < inputChainOrder.size(); ++index) {
        if (!validKind(inputChainOrder[index].reference.kind)) {
            addIssue(
                result,
                LegacySortedRenderQueueIssueKind::invalidItemKind,
                index);
            return result;
        }
    }

    std::vector<LegacySortedRenderItem> current(
        inputChainOrder.begin(), inputChainOrder.end());

    std::vector<LegacySortedRenderItem> scratch(inputChainOrder.size());
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        std::array<std::size_t, 256U> counts{};
        for (const LegacySortedRenderItem& item : current) {
            const auto bucket = static_cast<std::uint8_t>(
                item.sortKey >> shift);
            ++counts[bucket];
        }

        std::array<std::size_t, 256U> offsets{};
        for (std::size_t bucket = 1U; bucket < offsets.size(); ++bucket) {
            offsets[bucket] = offsets[bucket - 1U] + counts[bucket - 1U];
        }

        for (const LegacySortedRenderItem& item : current) {
            const auto bucket = static_cast<std::uint8_t>(
                item.sortKey >> shift);
            scratch[offsets[bucket]++] = item;
        }
        current.swap(scratch);
    }

    result.queue = LegacySortedRenderQueue{
        .items = std::move(current),
    };
    return result;
}

} // namespace airfix::render
