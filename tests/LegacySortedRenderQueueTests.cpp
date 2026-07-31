#include "airfix/render/LegacySortedRenderQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using airfix::render::LegacySortedRenderItem;
using airfix::render::LegacySortedRenderItemKind;
using airfix::render::LegacySortedRenderItemReference;
using airfix::render::LegacySortedRenderQueueIssueKind;
using airfix::render::LegacySortedRenderQueueLimits;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] LegacySortedRenderItem item(
    const std::uint32_t key,
    const LegacySortedRenderItemKind kind,
    const std::uint32_t payloadIndex) {
    return {
        .sortKey = key,
        .reference = LegacySortedRenderItemReference{
            .kind = kind,
            .payloadIndex = payloadIndex,
        },
    };
}

void testQuantizedDepthKeyUsesExactUnsignedArithmetic() {
    using airfix::render::legacySortedRenderKeyFromQuantizedDepth;

    require(
        legacySortedRenderKeyFromQuantizedDepth(
            std::numeric_limits<std::int32_t>::max()) == 0x00000001U,
        "INT32_MAX key mismatch");
    require(
        legacySortedRenderKeyFromQuantizedDepth(1) == 0x7FFFFFFFU,
        "+1 key mismatch");
    require(
        legacySortedRenderKeyFromQuantizedDepth(0) == 0x80000000U,
        "zero key mismatch");
    require(
        legacySortedRenderKeyFromQuantizedDepth(-1) == 0x80000001U,
        "-1 key mismatch");
    require(
        legacySortedRenderKeyFromQuantizedDepth(
            std::numeric_limits<std::int32_t>::min()) == 0x00000000U,
        "INT32_MIN key mismatch");
}

void testEmptyQueueSucceeds() {
    const auto result =
        airfix::render::buildLegacySortedRenderQueue({});
    require(result.queue.has_value(), "empty queue was rejected");
    require(result.issues.empty(), "empty queue reported an issue");
    require(result.queue->items.empty(), "empty queue produced items");
}

void testSingleItemSucceedsWithoutChangingIdentity() {
    const std::vector<LegacySortedRenderItem> inputChain{
        item(0xFFFFFFFFU, LegacySortedRenderItemKind::custom, 77U),
    };
    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(result.queue.has_value(), "single-item queue was rejected");
    require(
        result.queue->items == inputChain,
        "single-item queue changed item identity");
}

void testUnsignedAscendingOrderAcrossAllKeyBytes() {
    const std::vector<LegacySortedRenderItem> inputChain{
        item(0xFF000000U, LegacySortedRenderItemKind::triangle, 0U),
        item(0x00000001U, LegacySortedRenderItemKind::sprite, 1U),
        item(0x80000000U, LegacySortedRenderItemKind::custom, 2U),
        item(0x00000000U, LegacySortedRenderItemKind::triangle, 3U),
        item(0x00FF0000U, LegacySortedRenderItemKind::sprite, 4U),
        item(0x0000FF00U, LegacySortedRenderItemKind::custom, 5U),
        item(0x7FFFFFFFU, LegacySortedRenderItemKind::triangle, 6U),
        item(0xFFFFFFFFU, LegacySortedRenderItemKind::sprite, 7U),
    };

    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(result.queue.has_value(), "valid mixed-key queue was rejected");
    const std::vector<LegacySortedRenderItem> expected{
        inputChain[3U],
        inputChain[1U],
        inputChain[5U],
        inputChain[4U],
        inputChain[6U],
        inputChain[2U],
        inputChain[0U],
        inputChain[7U],
    };
    require(
        result.queue->items == expected,
        "queue did not sort by unsigned 32-bit key ascending");
}

void testEqualKeysRetainInputChainOrder() {
    // Chronological discovery was triangle, sprite, custom. Native producers
    // prepend, so the sorter receives the reverse sequence below.
    const std::vector<LegacySortedRenderItem> inputChain{
        item(7U, LegacySortedRenderItemKind::custom, 30U),
        item(7U, LegacySortedRenderItemKind::sprite, 20U),
        item(7U, LegacySortedRenderItemKind::triangle, 10U),
    };

    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(result.queue.has_value(), "equal-key queue was rejected");
    require(
        result.queue->items == inputChain,
        "stable radix sort did not preserve native input-chain tie order");
}

void testKindsRemainInOneUngroupedQueue() {
    const std::vector<LegacySortedRenderItem> inputChain{
        item(10U, LegacySortedRenderItemKind::triangle, 5U),
        item(20U, LegacySortedRenderItemKind::custom, 4U),
        item(10U, LegacySortedRenderItemKind::sprite, 3U),
        item(20U, LegacySortedRenderItemKind::triangle, 2U),
        item(10U, LegacySortedRenderItemKind::triangle, 1U),
    };

    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(result.queue.has_value(), "cross-kind queue was rejected");
    const std::vector<LegacySortedRenderItem> expected{
        inputChain[0U],
        inputChain[2U],
        inputChain[4U],
        inputChain[1U],
        inputChain[3U],
    };
    require(
        result.queue->items == expected,
        "queue regrouped by item kind or lost stable prepend order");
}

void testLimitsFailBeforePublishingPartialOutput() {
    const std::vector<LegacySortedRenderItem> inputChain{
        item(2U, LegacySortedRenderItemKind::triangle, 1U),
        item(1U, LegacySortedRenderItemKind::sprite, 2U),
    };

    LegacySortedRenderQueueLimits itemLimit;
    itemLimit.maximumItems = 1U;
    auto result = airfix::render::buildLegacySortedRenderQueue(
        inputChain, itemLimit);
    require(!result.queue.has_value(), "item limit published a queue");
    require(
        result.issues.size() == 1U &&
            result.issues[0U].kind ==
                LegacySortedRenderQueueIssueKind::limitExceeded &&
            !result.issues[0U].inputChainIndex.has_value(),
        "item limit issue mismatch");

    LegacySortedRenderQueueLimits byteLimit;
    byteLimit.maximumWorkingBytes =
        2U * inputChain.size() * sizeof(LegacySortedRenderItem) - 1U;
    result = airfix::render::buildLegacySortedRenderQueue(
        inputChain, byteLimit);
    require(!result.queue.has_value(), "working-byte limit published a queue");
    require(
        result.issues.size() == 1U &&
            result.issues[0U].kind ==
                LegacySortedRenderQueueIssueKind::limitExceeded,
        "working-byte limit issue mismatch");

    byteLimit.maximumWorkingBytes += 1U;
    result = airfix::render::buildLegacySortedRenderQueue(
        inputChain, byteLimit);
    require(
        result.queue.has_value(),
        "exact working-byte limit rejected a valid queue");

    LegacySortedRenderQueueLimits exactItemLimit;
    exactItemLimit.maximumItems = inputChain.size();
    result = airfix::render::buildLegacySortedRenderQueue(
        inputChain, exactItemLimit);
    require(
        result.queue.has_value(),
        "exact item-count limit rejected a valid queue");
}

void testInvalidKindReportsInputChainIndex() {
    const std::vector<LegacySortedRenderItem> inputChain{
        item(3U, LegacySortedRenderItemKind::triangle, 1U),
        item(
            2U,
            static_cast<LegacySortedRenderItemKind>(0xFFU),
            2U),
        item(1U, LegacySortedRenderItemKind::custom, 3U),
    };

    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(!result.queue.has_value(), "invalid item kind published a queue");
    require(
        result.issues.size() == 1U &&
            result.issues[0U].kind ==
                LegacySortedRenderQueueIssueKind::invalidItemKind &&
            result.issues[0U].inputChainIndex == 1U,
        "invalid item kind context mismatch");
}

void testInputIsNotMutated() {
    std::vector<LegacySortedRenderItem> inputChain{
        item(3U, LegacySortedRenderItemKind::triangle, 1U),
        item(1U, LegacySortedRenderItemKind::sprite, 2U),
        item(2U, LegacySortedRenderItemKind::custom, 3U),
    };
    const auto original = inputChain;
    const auto result =
        airfix::render::buildLegacySortedRenderQueue(inputChain);
    require(result.queue.has_value(), "valid queue was rejected");
    require(inputChain == original, "queue builder mutated input chain");
}

} // namespace

int main() {
    try {
        testQuantizedDepthKeyUsesExactUnsignedArithmetic();
        testEmptyQueueSucceeds();
        testSingleItemSucceedsWithoutChangingIdentity();
        testUnsignedAscendingOrderAcrossAllKeyBytes();
        testEqualKeysRetainInputChainOrder();
        testKindsRemainInOneUngroupedQueue();
        testLimitsFailBeforePublishingPartialOutput();
        testInvalidKindReportsInputChainIndex();
        testInputIsNotMutated();
    }
    catch (const std::exception& error) {
        std::cerr << "LegacySortedRenderQueueTests failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
