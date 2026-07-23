#include "airfix/assets/LegacyFormats.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace airfix::assets {
namespace {

[[nodiscard]] std::uint64_t checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checkedMultiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left * right;
}

void requireRange(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::uint64_t limit,
    const std::string_view field) {
    if (offset > limit || checkedAdd(offset, size, field) > limit) {
        throw ParseError(std::string(field) + " is outside the file (offset=" +
            std::to_string(offset) + ", size=" + std::to_string(size) +
            ", limit=" + std::to_string(limit) + ")");
    }
}

[[nodiscard]] std::uint16_t readU16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 2U, bytes.size(), field);
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 4U, bytes.size(), field);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void requireGtiMetadataCapacity(
    const std::size_t chunkCount,
    const std::size_t variantCount,
    const GtiParseLimits& limits) {
    if (variantCount > limits.maximumVariants) {
        throw GtiParseLimitError("GTI exceeds the configured variant limit");
    }
    if (chunkCount != 0U &&
        sizeof(GtiChunk) >
            std::numeric_limits<std::size_t>::max() / chunkCount) {
        throw GtiParseLimitError(
            "GTI metadata size exceeds this process");
    }
    const auto chunkBytes = chunkCount * sizeof(GtiChunk);
    if (variantCount != 0U &&
        sizeof(GtiVariant) >
            std::numeric_limits<std::size_t>::max() / variantCount) {
        throw GtiParseLimitError(
            "GTI metadata size exceeds this process");
    }
    const auto variantBytes = variantCount * sizeof(GtiVariant);
    if (variantBytes > std::numeric_limits<std::size_t>::max() - chunkBytes ||
        chunkBytes + variantBytes > limits.maximumMetadataBytes) {
        throw GtiParseLimitError(
            "GTI exceeds the configured metadata limit");
    }
}

[[nodiscard]] std::size_t nextGtiRecordCount(
    const std::size_t current) {
    if (current == std::numeric_limits<std::size_t>::max()) {
        throw GtiParseLimitError(
            "GTI metadata record count exceeds this process");
    }
    return current + 1U;
}

[[nodiscard]] float readFloat(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    return std::bit_cast<float>(readU32(bytes, offset, field));
}

struct CcfParseBudget {
    std::size_t remainingDescriptors{};

    void consume(const std::string_view field) {
        if (remainingDescriptors == 0U) {
            throw ParseError(std::string(field) + " exceeds the global CCF chunk limit");
        }
        --remainingDescriptors;
    }
};

struct CcfSpatialBudget {
    static constexpr std::size_t kTreeLimit = 100'000U;
    static constexpr std::size_t kNodeLimit = 100'000U;
    static constexpr std::size_t kPolygonLimit = 250'000U;
    static constexpr std::size_t kDepthLimit = 1'024U;

    std::size_t treeCount{};
    std::size_t nodeCount{};
    std::size_t polygonCount{};

    void addTree() {
        if (treeCount >= kTreeLimit) {
            throw ParseError("CCF exceeds the BSP tree limit");
        }
        ++treeCount;
    }

    void addNode() {
        if (nodeCount >= kNodeLimit) {
            throw ParseError("CCF exceeds the BSP node limit");
        }
        ++nodeCount;
    }

    void addPolygon() {
        if (polygonCount >= kPolygonLimit) {
            throw ParseError("CCF exceeds the BSP polygon limit");
        }
        ++polygonCount;
    }
};

[[nodiscard]] std::vector<CcfChunk> parseCcfChunkSequence(
    const std::span<const std::uint8_t> bytes,
    const std::size_t begin,
    const std::size_t end,
    const std::string_view field,
    CcfParseBudget& budget) {
    if (begin > end || end > bytes.size()) {
        throw ParseError(std::string(field) + " has an invalid containing range");
    }
    std::vector<CcfChunk> chunks;
    auto cursor = begin;
    while (cursor < end) {
        requireRange(cursor, 6U, end, std::string(field) + " chunk header");
        const auto id = readU16(bytes, cursor, std::string(field) + " chunk id");
        const auto totalSize = readU32(
            bytes, cursor + 2U, std::string(field) + " chunk size");
        if (totalSize < 6U) {
            throw ParseError(std::string(field) + " chunk is shorter than its header");
        }
        requireRange(cursor, totalSize, end, std::string(field) + " chunk");
        budget.consume(field);
        chunks.push_back({
            .id = id,
            .totalSize = totalSize,
            .offset = cursor,
            .directChildren = {},
        });
        cursor = static_cast<std::size_t>(checkedAdd(
            cursor, totalSize, std::string(field) + " next chunk"));
    }
    return chunks;
}

[[nodiscard]] std::string parseCcfString(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field) {
    constexpr std::uint16_t kStringChunk = 0xF020U;
    constexpr std::uint32_t kMaximumStringBytes = 4097U;
    if (chunk.id != kStringChunk || chunk.totalSize < 10U) {
        throw ParseError(std::string(field) + " is not a valid CCF string chunk");
    }
    const auto payloadOffset = checkedAdd(chunk.offset, 6U, field);
    const auto byteCount = readU32(
        bytes, static_cast<std::size_t>(payloadOffset), field);
    if (byteCount > kMaximumStringBytes ||
        checkedAdd(10U, byteCount, field) != chunk.totalSize) {
        throw ParseError(std::string(field) + " has an invalid byte count");
    }
    // The shipped writer uses a special zero-byte representation for an
    // empty CcString; non-empty strings include their terminal NUL.
    if (byteCount == 0U) {
        return {};
    }
    const auto textOffset = checkedAdd(payloadOffset, 4U, field);
    requireRange(textOffset, byteCount, bytes.size(), field);
    const auto textBegin = static_cast<std::size_t>(textOffset);
    const auto terminator = checkedAdd(textOffset, byteCount - 1U, field);
    if (bytes[static_cast<std::size_t>(terminator)] != 0U) {
        throw ParseError(std::string(field) + " is not NUL terminated");
    }
    for (std::uint32_t index = 0U; index + 1U < byteCount; ++index) {
        if (bytes[textBegin + index] == 0U) {
            throw ParseError(std::string(field) + " contains an embedded NUL");
        }
    }
    return std::string(
        reinterpret_cast<const char*>(bytes.data() + textBegin),
        byteCount - 1U);
}

struct CcfName {
    std::string name;
    std::string prefix;
};

[[nodiscard]] CcfName parseCcfName(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field,
    CcfParseBudget& budget) {
    constexpr std::uint16_t kNameChunk = 0xF010U;
    constexpr std::uint16_t kStringChunk = 0xF020U;
    if (chunk.id != kNameChunk) {
        throw ParseError(std::string(field) + " does not begin with a CCF name");
    }
    const auto begin = checkedAdd(chunk.offset, 6U, field);
    const auto end = checkedAdd(chunk.offset, chunk.totalSize, field);
    const auto children = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        field,
        budget);
    if (children.size() != 2U || children[0].id != kStringChunk ||
        children[1].id != kStringChunk) {
        throw ParseError(std::string(field) + " must contain name and prefix strings");
    }
    return {
        .name = parseCcfString(bytes, children[0], std::string(field) + " name"),
        .prefix = parseCcfString(bytes, children[1], std::string(field) + " prefix"),
    };
}

[[nodiscard]] std::string parseCcfWrappedString(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field,
    CcfParseBudget& budget) {
    constexpr std::uint16_t kStringChunk = 0xF020U;
    const auto begin = checkedAdd(chunk.offset, 6U, field);
    const auto end = checkedAdd(chunk.offset, chunk.totalSize, field);
    const auto children = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        field,
        budget);
    if (children.size() != 1U || children[0].id != kStringChunk) {
        throw ParseError(std::string(field) + " must contain exactly one CCF string");
    }
    return parseCcfString(bytes, children[0], field);
}

[[nodiscard]] CcfRoomMetadata parseCcfRoom(
    std::span<const std::uint8_t> bytes,
    CcfChunk& room,
    CcfParseBudget& budget,
    CcfSpatialBudget& spatialBudget,
    bool primaryBinding);

void validateCcfMaterialVectors(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk) {
    if (chunk.totalSize != 46U) {
        throw ParseError("CCF material vectors have an invalid size");
    }
    const auto first = checkedAdd(chunk.offset, 6U, "CCF material vectors");
    const auto second = checkedAdd(first, 18U, "CCF material vectors");
    if (readU16(bytes, static_cast<std::size_t>(first), "CCF material vector") != 0xF030U ||
        readU32(bytes, static_cast<std::size_t>(first + 2U), "CCF material vector") != 18U ||
        readU16(bytes, static_cast<std::size_t>(second), "CCF material vector") != 0xF030U ||
        readU32(bytes, static_cast<std::size_t>(second + 2U), "CCF material vector") != 18U) {
        throw ParseError("CCF material vectors do not contain two vec3 chunks");
    }
}

[[nodiscard]] CcfMaterialMetadata parseCcfMaterial(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& material,
    CcfParseBudget& budget) {
    constexpr std::uint16_t kMaterialChunk = 0x2100U;
    if (material.id != kMaterialChunk) {
        throw ParseError("CCF material section contains a non-material chunk");
    }
    const auto payloadBegin = checkedAdd(material.offset, 6U, "CCF material payload");
    const auto materialEnd = checkedAdd(material.offset, material.totalSize, "CCF material end");
    requireRange(payloadBegin, 6U, materialEnd, "CCF material name header");
    CcfChunk nameChunk{
        .id = readU16(bytes, static_cast<std::size_t>(payloadBegin), "CCF material name id"),
        .totalSize = readU32(
            bytes, static_cast<std::size_t>(payloadBegin + 2U), "CCF material name size"),
        .offset = payloadBegin,
        .directChildren = {},
    };
    if (nameChunk.totalSize < 6U) {
        throw ParseError("CCF material name is shorter than its header");
    }
    requireRange(nameChunk.offset, nameChunk.totalSize, materialEnd, "CCF material name");
    const auto name = parseCcfName(bytes, nameChunk, "CCF material name", budget);
    auto cursor = checkedAdd(nameChunk.offset, nameChunk.totalSize, "CCF material reference");
    requireRange(cursor, 4U, materialEnd, "CCF material reference");
    const auto reference = readU32(
        bytes, static_cast<std::size_t>(cursor), "CCF material reference");
    cursor = checkedAdd(cursor, 4U, "CCF material properties");
    material.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(materialEnd),
        "CCF material property",
        budget);

    CcfMaterialMetadata metadata{
        .name = name.name,
        .prefix = name.prefix,
        .reference = reference,
        .primaryTexture = std::nullopt,
        .secondaryTexture = std::nullopt,
        .environmentTexture = std::nullopt,
        .offset = material.offset,
    };
    bool hasVectors = false;
    bool hasFlags = false;
    bool hasMode = false;
    bool hasScalars = false;
    for (const auto& property : material.directChildren) {
        switch (property.id) {
        case 0x2110U:
            if (metadata.primaryTexture.has_value()) {
                throw ParseError("CCF material has duplicate primary textures");
            }
            metadata.primaryTexture = parseCcfWrappedString(
                bytes, property, "CCF material primary texture", budget);
            break;
        case 0x2111U:
            if (metadata.secondaryTexture.has_value()) {
                throw ParseError("CCF material has duplicate secondary textures");
            }
            metadata.secondaryTexture = parseCcfWrappedString(
                bytes, property, "CCF material secondary texture", budget);
            break;
        case 0x2120U:
            if (metadata.environmentTexture.has_value()) {
                throw ParseError("CCF material has duplicate environment textures");
            }
            metadata.environmentTexture = parseCcfWrappedString(
                bytes, property, "CCF material environment texture", budget);
            break;
        case 0x2140U:
            if (hasVectors) {
                throw ParseError("CCF material has duplicate vector properties");
            }
            validateCcfMaterialVectors(bytes, property);
            hasVectors = true;
            break;
        case 0x2150U:
            if (hasFlags || property.totalSize != 12U) {
                throw ParseError("CCF material has invalid flag properties");
            }
            hasFlags = true;
            break;
        case 0x2151U:
            if (hasMode || property.totalSize != 7U) {
                throw ParseError("CCF material has an invalid mode property");
            }
            hasMode = true;
            break;
        case 0x2152U:
            if (hasScalars || property.totalSize != 18U) {
                throw ParseError("CCF material has invalid scalar properties");
            }
            hasScalars = true;
            break;
        default:
            // LoadSceneCcf closes unknown chunks and continues. Retain them in
            // directChildren so later schema versions remain inspectable.
            break;
        }
    }
    return metadata;
}

[[nodiscard]] CcfVector3 parseCcfVector3(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::uint16_t expectedId,
    const std::string_view field) {
    if (chunk.id != expectedId || chunk.totalSize != 18U) {
        throw ParseError(std::string(field) + " has an invalid vec3 chunk");
    }
    const auto payload = checkedAdd(chunk.offset, 6U, field);
    return {
        readFloat(bytes, static_cast<std::size_t>(payload), field),
        readFloat(bytes, static_cast<std::size_t>(payload + 4U), field),
        readFloat(bytes, static_cast<std::size_t>(payload + 8U), field),
    };
}

[[nodiscard]] std::array<CcfVector3, 3> parseCcfOrientation(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    CcfParseBudget& budget) {
    if (chunk.id != 0xF070U || chunk.totalSize != 66U) {
        throw ParseError("CCF mesh orientation has an invalid outer chunk");
    }
    const auto matrixOffset = checkedAdd(chunk.offset, 6U, "CCF mesh orientation");
    CcfChunk matrix{
        .id = readU16(bytes, static_cast<std::size_t>(matrixOffset),
            "CCF mesh orientation matrix id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(matrixOffset + 2U),
            "CCF mesh orientation matrix size"),
        .offset = matrixOffset,
        .directChildren = {},
    };
    if (matrix.id != 0xF050U || matrix.totalSize != 60U) {
        throw ParseError("CCF mesh orientation has an invalid matrix chunk");
    }
    const auto begin = checkedAdd(matrix.offset, 6U, "CCF mesh orientation vectors");
    const auto end = checkedAdd(matrix.offset, matrix.totalSize, "CCF mesh orientation end");
    const auto vectors = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        "CCF mesh orientation vector",
        budget);
    if (vectors.size() != 3U) {
        throw ParseError("CCF mesh orientation must contain three vectors");
    }
    return {
        parseCcfVector3(bytes, vectors[0], 0xF040U, "CCF mesh orientation X"),
        parseCcfVector3(bytes, vectors[1], 0xF040U, "CCF mesh orientation Y"),
        parseCcfVector3(bytes, vectors[2], 0xF040U, "CCF mesh orientation Z"),
    };
}

[[nodiscard]] CcfChunk readContainedCcfChunk(
    const std::span<const std::uint8_t> bytes,
    const std::uint64_t offset,
    const std::uint64_t end,
    const std::string_view field) {
    requireRange(offset, 6U, end, std::string(field) + " header");
    CcfChunk chunk{
        .id = readU16(bytes, static_cast<std::size_t>(offset),
            std::string(field) + " id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(offset + 2U),
            std::string(field) + " size"),
        .offset = offset,
        .directChildren = {},
    };
    if (chunk.totalSize < 6U) {
        throw ParseError(std::string(field) + " is shorter than its header");
    }
    requireRange(chunk.offset, chunk.totalSize, end, field);
    return chunk;
}

[[nodiscard]] CcfFogMetadata parseCcfFog(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& fog,
    CcfParseBudget& budget) {
    if (fog.id != 0x1101U || fog.totalSize != 36U) {
        throw ParseError("CCF room fog has an invalid chunk");
    }
    const auto payload = checkedAdd(fog.offset, 6U, "CCF room fog payload");
    const auto end = checkedAdd(fog.offset, fog.totalSize, "CCF room fog end");
    const auto vectorOffset = checkedAdd(payload, 12U, "CCF room fog color");
    const auto vector = readContainedCcfChunk(
        bytes, vectorOffset, end, "CCF room fog color");
    budget.consume("CCF room fog color");
    if (checkedAdd(vector.offset, vector.totalSize, "CCF room fog color end") != end) {
        throw ParseError("CCF room fog has trailing data");
    }
    return {
        .enabledRaw = readU32(
            bytes, static_cast<std::size_t>(payload), "CCF room fog enabled"),
        .first = readFloat(
            bytes, static_cast<std::size_t>(payload + 4U), "CCF room fog first"),
        .second = readFloat(
            bytes, static_cast<std::size_t>(payload + 8U), "CCF room fog second"),
        .color = parseCcfVector3(
            bytes, vector, 0xF030U, "CCF room fog color"),
        .offset = fog.offset,
    };
}

[[nodiscard]] CcfBspPolygonMetadata parseCcfBspPolygon(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& polygon,
    CcfParseBudget& budget) {
    if (polygon.id != 0xF0C1U || polygon.totalSize != 104U) {
        throw ParseError("CCF BSP polygon has an invalid chunk");
    }
    const auto end = checkedAdd(
        polygon.offset, polygon.totalSize, "CCF BSP polygon end");
    auto cursor = checkedAdd(
        polygon.offset, 6U, "CCF BSP polygon payload");
    std::array<CcfVector3, 5> vectors{};
    for (std::size_t index = 0U; index < vectors.size(); ++index) {
        const auto vector = readContainedCcfChunk(
            bytes, cursor, end, "CCF BSP polygon vector");
        budget.consume("CCF BSP polygon vector");
        vectors[index] = parseCcfVector3(
            bytes, vector, 0xF040U, "CCF BSP polygon vector");
        cursor = checkedAdd(
            cursor, vector.totalSize, "CCF BSP polygon next vector");
    }
    requireRange(cursor, 8U, end, "CCF BSP polygon indices");
    if (checkedAdd(cursor, 8U, "CCF BSP polygon payload end") != end) {
        throw ParseError("CCF BSP polygon has trailing data");
    }
    return {
        .faceCross = vectors[0],
        .faceNormal = vectors[1],
        .point0 = vectors[2],
        .edge01 = vectors[3],
        .edge12 = vectors[4],
        .polygonIndex = readU32(
            bytes, static_cast<std::size_t>(cursor), "CCF BSP polygon index"),
        .placedObjectReference = readU32(
            bytes,
            static_cast<std::size_t>(cursor + 4U),
            "CCF BSP polygon object reference"),
        .offset = polygon.offset,
    };
}

[[nodiscard]] CcfBspTreeMetadata parseCcfBspTree(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& root,
    const CcfBspTreeKind kind,
    const CcfBspTreeSource source,
    CcfParseBudget& budget,
    CcfSpatialBudget& spatialBudget) {
    if (root.id != 0xF0C0U) {
        throw ParseError("CCF BSP tree root has an invalid id");
    }
    spatialBudget.addTree();
    CcfBspTreeMetadata tree{
        .kind = kind,
        .source = source,
        .rootNodeIndex = 0U,
        .nodes = {},
        .polygons = {},
        .offset = root.offset,
    };

    enum class ChildSide : std::uint8_t {
        a,
        b,
    };
    struct PendingNode {
        CcfChunk chunk;
        std::optional<std::size_t> parentIndex;
        ChildSide side{ChildSide::a};
        std::size_t depth{};
    };
    std::vector<PendingNode> pending;
    pending.push_back({
        .chunk = root,
        .parentIndex = std::nullopt,
        .side = ChildSide::a,
        .depth = 1U,
    });

    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (current.depth > CcfSpatialBudget::kDepthLimit) {
            throw ParseError("CCF BSP tree exceeds the depth limit");
        }
        spatialBudget.addNode();
        const auto nodeIndex = tree.nodes.size();
        tree.nodes.push_back({
            .childAPresenceRaw = 0U,
            .childBPresenceRaw = 0U,
            .childAIndex = std::nullopt,
            .childBIndex = std::nullopt,
            .splitNormal = {},
            .pointOnPlane = {},
            .polygonIndices = {},
            .trailingChildren = {},
            .offset = current.chunk.offset,
        });
        if (current.parentIndex.has_value()) {
            auto& parent = tree.nodes[*current.parentIndex];
            if (current.side == ChildSide::a) {
                parent.childAIndex = nodeIndex;
            }
            else {
                parent.childBIndex = nodeIndex;
            }
        }

        const auto nodeEnd = checkedAdd(
            current.chunk.offset, current.chunk.totalSize, "CCF BSP node end");
        auto cursor = checkedAdd(
            current.chunk.offset, 6U, "CCF BSP node payload");
        requireRange(cursor, 4U, nodeEnd, "CCF BSP child A presence");
        auto& node = tree.nodes[nodeIndex];
        node.childAPresenceRaw = readU32(
            bytes, static_cast<std::size_t>(cursor), "CCF BSP child A presence");
        cursor = checkedAdd(cursor, 4U, "CCF BSP child A");

        std::optional<CcfChunk> childA;
        if (node.childAPresenceRaw != 0U) {
            childA = readContainedCcfChunk(
                bytes, cursor, nodeEnd, "CCF BSP child A");
            budget.consume("CCF BSP child A");
            if (childA->id != 0xF0C0U) {
                throw ParseError("CCF BSP child A has an invalid id");
            }
            cursor = checkedAdd(
                cursor, childA->totalSize, "CCF BSP child B presence");
        }

        requireRange(cursor, 4U, nodeEnd, "CCF BSP child B presence");
        node.childBPresenceRaw = readU32(
            bytes, static_cast<std::size_t>(cursor), "CCF BSP child B presence");
        cursor = checkedAdd(cursor, 4U, "CCF BSP child B");
        std::optional<CcfChunk> childB;
        if (node.childBPresenceRaw != 0U) {
            childB = readContainedCcfChunk(
                bytes, cursor, nodeEnd, "CCF BSP child B");
            budget.consume("CCF BSP child B");
            if (childB->id != 0xF0C0U) {
                throw ParseError("CCF BSP child B has an invalid id");
            }
            cursor = checkedAdd(
                cursor, childB->totalSize, "CCF BSP split normal");
        }

        const auto splitNormal = readContainedCcfChunk(
            bytes, cursor, nodeEnd, "CCF BSP split normal");
        budget.consume("CCF BSP split normal");
        node.splitNormal = parseCcfVector3(
            bytes, splitNormal, 0xF040U, "CCF BSP split normal");
        cursor = checkedAdd(
            cursor, splitNormal.totalSize, "CCF BSP point on plane");
        const auto pointOnPlane = readContainedCcfChunk(
            bytes, cursor, nodeEnd, "CCF BSP point on plane");
        budget.consume("CCF BSP point on plane");
        node.pointOnPlane = parseCcfVector3(
            bytes, pointOnPlane, 0xF040U, "CCF BSP point on plane");
        cursor = checkedAdd(
            cursor, pointOnPlane.totalSize, "CCF BSP trailing descriptors");

        node.trailingChildren = parseCcfChunkSequence(
            bytes,
            static_cast<std::size_t>(cursor),
            static_cast<std::size_t>(nodeEnd),
            "CCF BSP trailing descriptor",
            budget);
        for (const auto& trailing : node.trailingChildren) {
            if (trailing.id != 0xF0C1U) {
                continue;
            }
            auto polygon = parseCcfBspPolygon(bytes, trailing, budget);
            spatialBudget.addPolygon();
            node.polygonIndices.push_back(tree.polygons.size());
            tree.polygons.push_back(std::move(polygon));
        }

        const auto childDepth = current.depth + 1U;
        if (childB.has_value()) {
            pending.push_back({
                .chunk = std::move(*childB),
                .parentIndex = nodeIndex,
                .side = ChildSide::b,
                .depth = childDepth,
            });
        }
        if (childA.has_value()) {
            pending.push_back({
                .chunk = std::move(*childA),
                .parentIndex = nodeIndex,
                .side = ChildSide::a,
                .depth = childDepth,
            });
        }
    }
    return tree;
}

[[nodiscard]] CcfRoomMetadata parseCcfRoom(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& room,
    CcfParseBudget& budget,
    CcfSpatialBudget& spatialBudget,
    const bool primaryBinding) {
    if (room.id != 0x1100U) {
        throw ParseError("CCF room parser received a non-room chunk");
    }
    const auto roomEnd = checkedAdd(room.offset, room.totalSize, "CCF room end");
    const auto payload = checkedAdd(room.offset, 6U, "CCF room payload");
    requireRange(payload, 6U, roomEnd, "CCF room name header");
    const CcfChunk nameChunk{
        .id = readU16(bytes, static_cast<std::size_t>(payload), "CCF room name id"),
        .totalSize = readU32(
            bytes, static_cast<std::size_t>(payload + 2U), "CCF room name size"),
        .offset = payload,
        .directChildren = {},
    };
    if (nameChunk.totalSize < 6U) {
        throw ParseError("CCF room name is shorter than its header");
    }
    requireRange(nameChunk.offset, nameChunk.totalSize, roomEnd, "CCF room name");
    budget.consume("CCF room name");
    const auto name = parseCcfName(bytes, nameChunk, "CCF room name", budget);
    auto cursor = checkedAdd(
        nameChunk.offset, nameChunk.totalSize, "CCF room reference");
    requireRange(cursor, 4U, roomEnd, "CCF room reference");
    const auto reference = readU32(
        bytes, static_cast<std::size_t>(cursor), "CCF room reference");
    cursor = checkedAdd(cursor, 4U, "CCF room children");
    room.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(roomEnd),
        "CCF room child",
        budget);

    CcfRoomMetadata metadata{
        .name = name.name,
        .prefix = name.prefix,
        .reference = reference,
        .primaryBinding = primaryBinding,
        .fog = std::nullopt,
        .staticBspTrees = {},
        .portalBspTrees = {},
        .directChildren = {},
        .offset = room.offset,
    };
    for (auto& child : room.directChildren) {
        if (child.id == 0x1101U) {
            if (metadata.fog.has_value()) {
                throw ParseError("CCF room contains duplicate fog");
            }
            metadata.fog = parseCcfFog(bytes, child, budget);
            continue;
        }
        if (child.id == 0xF0C0U) {
            metadata.staticBspTrees.push_back(parseCcfBspTree(
                bytes,
                child,
                CcfBspTreeKind::staticTree,
                CcfBspTreeSource::direct,
                budget,
                spatialBudget));
            continue;
        }
        if (child.id != 0x1200U && child.id != 0x1201U) {
            continue;
        }
        const auto wrapperBegin = checkedAdd(
            child.offset, 6U, "CCF BSP wrapper payload");
        const auto wrapperEnd = checkedAdd(
            child.offset, child.totalSize, "CCF BSP wrapper end");
        child.directChildren = parseCcfChunkSequence(
            bytes,
            static_cast<std::size_t>(wrapperBegin),
            static_cast<std::size_t>(wrapperEnd),
            "CCF BSP wrapper child",
            budget);
        const auto kind = child.id == 0x1200U
            ? CcfBspTreeKind::staticTree
            : CcfBspTreeKind::portalTree;
        auto& trees = child.id == 0x1200U
            ? metadata.staticBspTrees
            : metadata.portalBspTrees;
        for (const auto& rootChunk : child.directChildren) {
            if (rootChunk.id != 0xF0C0U) {
                continue;
            }
            trees.push_back(parseCcfBspTree(
                bytes,
                rootChunk,
                kind,
                CcfBspTreeSource::wrapped,
                budget,
                spatialBudget));
        }
    }
    metadata.directChildren = room.directChildren;
    return metadata;
}

[[nodiscard]] CcfPlacedOrientation parseCcfPlacedOrientation(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    CcfParseBudget& budget) {
    if (chunk.id != 0xF070U) {
        throw ParseError("CCF placed orientation has an invalid outer chunk");
    }
    const auto begin = checkedAdd(chunk.offset, 6U, "CCF placed orientation");
    const auto end = checkedAdd(
        chunk.offset, chunk.totalSize, "CCF placed orientation end");
    const auto children = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        "CCF placed orientation child",
        budget);
    if (children.size() != 1U) {
        throw ParseError("CCF placed orientation must contain exactly one child");
    }
    if (children[0].id == 0xF040U) {
        return parseCcfVector3(
            bytes, children[0], 0xF040U, "CCF placed alternate orientation");
    }
    if (children[0].id != 0xF050U || children[0].totalSize != 60U) {
        throw ParseError("CCF placed orientation has an invalid child");
    }
    const auto matrixBegin = checkedAdd(
        children[0].offset, 6U, "CCF placed orientation matrix");
    const auto matrixEnd = checkedAdd(
        children[0].offset,
        children[0].totalSize,
        "CCF placed orientation matrix end");
    const auto vectors = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(matrixBegin),
        static_cast<std::size_t>(matrixEnd),
        "CCF placed orientation vector",
        budget);
    if (vectors.size() != 3U) {
        throw ParseError("CCF placed orientation matrix must contain three vectors");
    }
    return std::array<CcfVector3, 3>{
        parseCcfVector3(bytes, vectors[0], 0xF040U,
            "CCF placed orientation X"),
        parseCcfVector3(bytes, vectors[1], 0xF040U,
            "CCF placed orientation Y"),
        parseCcfVector3(bytes, vectors[2], 0xF040U,
            "CCF placed orientation Z"),
    };
}

[[nodiscard]] CcfPlacedLight4310Metadata parseCcfPlacedLight4310(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& property,
    CcfParseBudget& budget) {
    if (property.totalSize < 36U) {
        throw ParseError("CCF placed light 0x4310 property is truncated");
    }
    const auto propertyEnd = checkedAdd(
        property.offset, property.totalSize, "CCF placed light 0x4310 end");
    const auto payload = checkedAdd(
        property.offset, 6U, "CCF placed light 0x4310 payload");
    const auto vectorChunk = readContainedCcfChunk(
        bytes, payload + 4U, propertyEnd, "CCF placed light 0x4310 vector");
    budget.consume("CCF placed light 0x4310 vector");
    const auto vector = parseCcfVector3(
        bytes, vectorChunk, 0xF030U, "CCF placed light 0x4310 vector");
    const auto scalarOffset = checkedAdd(
        vectorChunk.offset,
        vectorChunk.totalSize,
        "CCF placed light 0x4310 scalars");
    requireRange(scalarOffset, 8U, propertyEnd, "CCF placed light 0x4310 scalars");
    const auto optionalBegin = checkedAdd(
        scalarOffset, 8U, "CCF placed light 0x4310 optional texture");

    CcfPlacedLight4310Metadata parsed{
        .first = readFloat(bytes, static_cast<std::size_t>(payload),
            "CCF placed light 0x4310 first value"),
        .vector = vector,
        .second = readFloat(bytes, static_cast<std::size_t>(scalarOffset),
            "CCF placed light 0x4310 second value"),
        .third = readFloat(bytes, static_cast<std::size_t>(scalarOffset + 4U),
            "CCF placed light 0x4310 third value"),
        .texture = std::nullopt,
    };
    if (optionalBegin == propertyEnd) {
        return parsed;
    }
    const auto strings = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(optionalBegin),
        static_cast<std::size_t>(propertyEnd),
        "CCF placed light 0x4310 texture",
        budget);
    if (strings.size() != 1U || strings[0].id != 0xF020U) {
        throw ParseError("CCF placed light 0x4310 must contain zero or one texture string");
    }
    parsed.texture = parseCcfString(
        bytes, strings[0], "CCF placed light 0x4310 texture");
    return parsed;
}

[[nodiscard]] CcfPlacedLight4320Metadata parseCcfPlacedLight4320(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& property,
    CcfParseBudget& budget) {
    if (property.totalSize < 22U) {
        throw ParseError("CCF placed light 0x4320 property is truncated");
    }
    const auto payload = checkedAdd(
        property.offset, 6U, "CCF placed light 0x4320 payload");
    const auto propertyEnd = checkedAdd(
        property.offset, property.totalSize, "CCF placed light 0x4320 end");
    CcfPlacedLight4320Metadata parsed{
        .values = {
            readFloat(bytes, static_cast<std::size_t>(payload),
                "CCF placed light 0x4320 value 0"),
            readFloat(bytes, static_cast<std::size_t>(payload + 4U),
                "CCF placed light 0x4320 value 1"),
            readFloat(bytes, static_cast<std::size_t>(payload + 8U),
                "CCF placed light 0x4320 value 2"),
            readFloat(bytes, static_cast<std::size_t>(payload + 12U),
                "CCF placed light 0x4320 value 3"),
        },
        .textures = std::nullopt,
    };
    const auto optionalBegin = checkedAdd(
        payload, 16U, "CCF placed light 0x4320 optional textures");
    if (optionalBegin == propertyEnd) {
        return parsed;
    }
    const auto strings = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(optionalBegin),
        static_cast<std::size_t>(propertyEnd),
        "CCF placed light 0x4320 textures",
        budget);
    if (strings.size() != 2U || strings[0].id != 0xF020U ||
        strings[1].id != 0xF020U) {
        throw ParseError("CCF placed light 0x4320 must contain zero or two texture strings");
    }
    parsed.textures = std::array<std::string, 2>{
        parseCcfString(bytes, strings[0], "CCF placed light 0x4320 texture 0"),
        parseCcfString(bytes, strings[1], "CCF placed light 0x4320 texture 1"),
    };
    return parsed;
}

[[nodiscard]] CcfPlacedLight4330Metadata parseCcfPlacedLight4330(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& property,
    CcfParseBudget& budget) {
    if (property.totalSize != 32U) {
        throw ParseError("CCF placed light 0x4330 property has an invalid size");
    }
    const auto payload = checkedAdd(
        property.offset, 6U, "CCF placed light 0x4330 payload");
    const auto propertyEnd = checkedAdd(
        property.offset, property.totalSize, "CCF placed light 0x4330 end");
    const auto vectorChunk = readContainedCcfChunk(
        bytes, payload, propertyEnd, "CCF placed light 0x4330 vector");
    budget.consume("CCF placed light 0x4330 vector");
    return {
        .vector = parseCcfVector3(
            bytes, vectorChunk, 0xF040U, "CCF placed light 0x4330 vector"),
        .first = readFloat(bytes, static_cast<std::size_t>(payload + 18U),
            "CCF placed light 0x4330 first value"),
        .second = readFloat(bytes, static_cast<std::size_t>(payload + 22U),
            "CCF placed light 0x4330 second value"),
    };
}

[[nodiscard]] CcfPlacedNodeMetadata parseCcfPlacedNode(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& node,
    CcfParseBudget& budget) {
    if (node.id != 0x4100U && node.id != 0x4200U && node.id != 0x4300U) {
        throw ParseError("CCF placed-node parser received an unsupported chunk");
    }
    const auto nodeEnd = checkedAdd(node.offset, node.totalSize, "CCF placed node end");
    const auto payload = checkedAdd(node.offset, 6U, "CCF placed node payload");
    const auto nameChunk = readContainedCcfChunk(
        bytes, payload, nodeEnd, "CCF placed node name");
    budget.consume("CCF placed node name");
    const auto name = parseCcfName(bytes, nameChunk, "CCF placed node name", budget);
    auto cursor = checkedAdd(
        nameChunk.offset, nameChunk.totalSize, "CCF placed node fields");

    const auto object = node.id == 0x4100U;
    const auto fixedReferenceBytes = object ? 25U : 12U;
    requireRange(cursor, fixedReferenceBytes, nodeEnd, "CCF placed node references");
    const auto reference = readU32(
        bytes, static_cast<std::size_t>(cursor), "CCF placed node reference");
    const auto meshReference = object
        ? readU32(bytes, static_cast<std::size_t>(cursor + 4U),
            "CCF placed object mesh reference")
        : 0U;
    const auto roomOffset = object ? 8U : 4U;
    const auto parentOffset = object ? 12U : 8U;
    const auto roomReference = readU32(
        bytes, static_cast<std::size_t>(cursor + roomOffset),
        "CCF placed node room reference");
    const auto parentReference = readU32(
        bytes, static_cast<std::size_t>(cursor + parentOffset),
        "CCF placed node parent reference");
    const auto rawFlag = object
        ? bytes[static_cast<std::size_t>(cursor + 16U)]
        : static_cast<std::uint8_t>(0U);
    const auto portalType = object
        ? readU32(bytes, static_cast<std::size_t>(cursor + 17U),
            "CCF placed object portal type")
        : 0U;
    const auto portalRoomReference = object
        ? readU32(bytes, static_cast<std::size_t>(cursor + 21U),
            "CCF placed object portal room reference")
        : 0U;
    cursor = checkedAdd(cursor, fixedReferenceBytes, "CCF placed node position");

    const auto positionChunk = readContainedCcfChunk(
        bytes, cursor, nodeEnd, "CCF placed node position");
    budget.consume("CCF placed node position");
    const auto position = parseCcfVector3(
        bytes, positionChunk, 0xF040U, "CCF placed node position");
    cursor = checkedAdd(
        positionChunk.offset, positionChunk.totalSize, "CCF placed node scalar");
    requireRange(cursor, 4U, nodeEnd, "CCF placed node scalar");
    const auto rawScalar = readFloat(
        bytes, static_cast<std::size_t>(cursor), "CCF placed node scalar");
    cursor = checkedAdd(cursor, 4U, "CCF placed node orientation");
    const auto orientationChunk = readContainedCcfChunk(
        bytes, cursor, nodeEnd, "CCF placed node orientation");
    budget.consume("CCF placed node orientation");
    const auto orientation = parseCcfPlacedOrientation(
        bytes, orientationChunk, budget);
    cursor = checkedAdd(
        orientationChunk.offset,
        orientationChunk.totalSize,
        "CCF placed node children");
    node.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(nodeEnd),
        "CCF placed node child",
        budget);

    CcfPlacedNodeMetadata parsed{
        .kind = object
            ? CcfPlacedNodeKind::object
            : (node.id == 0x4200U
                ? CcfPlacedNodeKind::nullNode
                : CcfPlacedNodeKind::light),
        .name = name.name,
        .prefix = name.prefix,
        .currentReference = reference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .transform = {
            .position = position,
            .rawScalar = rawScalar,
            .orientation = orientation,
        },
        .directChildren = node.directChildren,
        .data = object
            ? CcfPlacedNodeData{CcfPlacedObjectMetadata{
                .meshReference = meshReference,
                .rawFlag = rawFlag,
                .portalType = portalType,
                .portalRoomReference = portalRoomReference,
                .propertyF0B0 = std::nullopt,
                .propertyF0B1 = std::nullopt,
                .value4501 = std::nullopt,
                .bsp4101 = std::nullopt,
            }}
            : (node.id == 0x4200U
                ? CcfPlacedNodeData{CcfPlacedNullMetadata{}}
                : CcfPlacedNodeData{CcfPlacedLightMetadata{}}),
        .offset = node.offset,
    };

    for (const auto& property : parsed.directChildren) {
        if (object) {
            auto& objectData = std::get<CcfPlacedObjectMetadata>(parsed.data);
            switch (property.id) {
            case 0xF0B0U:
                if (objectData.propertyF0B0.has_value() || property.totalSize != 10U) {
                    throw ParseError("CCF placed object has an invalid 0xF0B0 property");
                }
                objectData.propertyF0B0 = readU32(bytes,
                    static_cast<std::size_t>(property.offset + 6U),
                    "CCF placed object 0xF0B0 value");
                break;
            case 0xF0B1U:
                if (objectData.propertyF0B1.has_value() || property.totalSize != 10U) {
                    throw ParseError("CCF placed object has an invalid 0xF0B1 property");
                }
                objectData.propertyF0B1 = readU32(bytes,
                    static_cast<std::size_t>(property.offset + 6U),
                    "CCF placed object 0xF0B1 value");
                break;
            case 0x4501U:
                if (objectData.value4501.has_value() || property.totalSize != 10U) {
                    throw ParseError("CCF placed object has an invalid 0x4501 property");
                }
                objectData.value4501 = readU32(bytes,
                    static_cast<std::size_t>(property.offset + 6U),
                    "CCF placed object 0x4501 value");
                break;
            case 0x4101U:
                if (objectData.bsp4101.has_value()) {
                    throw ParseError("CCF placed object repeats its opaque 0x4101 property");
                }
                objectData.bsp4101 = property;
                break;
            default:
                break;
            }
            continue;
        }

        if (node.id == 0x4200U) {
            auto& nullData = std::get<CcfPlacedNullMetadata>(parsed.data);
            if (property.id == 0x4210U) {
                if (nullData.block4210.has_value() || property.totalSize < 10U) {
                    throw ParseError("CCF placed null has an invalid 0x4210 property");
                }
                const auto length = readU32(bytes,
                    static_cast<std::size_t>(property.offset + 6U),
                    "CCF placed null 0x4210 length");
                if (checkedAdd(10U, length, "CCF placed null 0x4210 length") !=
                    property.totalSize) {
                    throw ParseError("CCF placed null 0x4210 length does not match its chunk");
                }
                nullData.block4210 = CcfOpaqueRange{
                    .offset = checkedAdd(
                        property.offset, 10U, "CCF placed null 0x4210 block"),
                    .length = length,
                };
            }
            else if (property.id == 0x4500U) {
                if (nullData.value4500.has_value() || property.totalSize != 10U) {
                    throw ParseError("CCF placed null has an invalid 0x4500 property");
                }
                nullData.value4500 = readU32(bytes,
                    static_cast<std::size_t>(property.offset + 6U),
                    "CCF placed null 0x4500 value");
            }
            continue;
        }

        auto& lightData = std::get<CcfPlacedLightMetadata>(parsed.data);
        switch (property.id) {
        case 0x4310U:
            if (lightData.property4310.has_value()) {
                throw ParseError("CCF placed light repeats its 0x4310 property");
            }
            lightData.property4310 = parseCcfPlacedLight4310(bytes, property, budget);
            break;
        case 0x4320U:
            if (lightData.property4320.has_value()) {
                throw ParseError("CCF placed light repeats its 0x4320 property");
            }
            lightData.property4320 = parseCcfPlacedLight4320(bytes, property, budget);
            break;
        case 0x4330U:
            if (lightData.property4330.has_value()) {
                throw ParseError("CCF placed light repeats its 0x4330 property");
            }
            lightData.property4330 = parseCcfPlacedLight4330(bytes, property, budget);
            break;
        case 0xF0B0U:
            if (lightData.propertyF0B0.has_value() || property.totalSize != 10U) {
                throw ParseError("CCF placed light has an invalid 0xF0B0 property");
            }
            lightData.propertyF0B0 = readU32(bytes,
                static_cast<std::size_t>(property.offset + 6U),
                "CCF placed light 0xF0B0 value");
            break;
        default:
            break;
        }
    }
    return parsed;
}

[[nodiscard]] CcfMeshPaintMetadata parseCcfMeshPaint(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    CcfParseBudget& budget) {
    if (chunk.id != 0xF090U || chunk.totalSize < 10U) {
        throw ParseError("CCF mesh paint has an invalid chunk");
    }
    const auto payload = checkedAdd(chunk.offset, 6U, "CCF mesh paint");
    CcfMeshPaintMetadata paint{
        .type = readU32(bytes, static_cast<std::size_t>(payload), "CCF mesh paint type"),
        .colors = {},
    };
    const auto expectedColors = paint.type == 1U
        ? 1U
        : (paint.type == 3U || paint.type == 4U ? 3U : 0U);
    if (expectedColors == 0U) {
        // The original dispatch ignores paint modes it does not understand.
        // Preserve the raw type without interpreting the remaining payload.
        return paint;
    }
    auto cursor = checkedAdd(payload, 4U, "CCF mesh paint colors");
    const auto paintEnd = checkedAdd(chunk.offset, chunk.totalSize, "CCF mesh paint end");
    for (std::size_t index = 0U; index < expectedColors; ++index) {
        requireRange(cursor, 6U, paintEnd, "CCF mesh paint color header");
        CcfChunk color{
            .id = readU16(bytes, static_cast<std::size_t>(cursor),
                "CCF mesh paint color id"),
            .totalSize = readU32(bytes, static_cast<std::size_t>(cursor + 2U),
                "CCF mesh paint color size"),
            .offset = cursor,
            .directChildren = {},
        };
        requireRange(color.offset, color.totalSize, paintEnd, "CCF mesh paint color");
        budget.consume("CCF mesh paint color");
        paint.colors.push_back(parseCcfVector3(
            bytes, color, 0xF030U, "CCF mesh paint color"));
        cursor = checkedAdd(cursor, color.totalSize, "CCF mesh next paint color");
    }
    // CloseChunk skips any loader-compatible extension tail after the required
    // color prefix. It may be opaque rather than another nested chunk.
    return paint;
}

[[nodiscard]] CcfMeshVertexMetadata parseCcfMeshVertex(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& vertex,
    CcfParseBudget& budget) {
    if (vertex.id != 0x3110U || vertex.totalSize < 24U) {
        throw ParseError("CCF mesh vertex has an invalid chunk");
    }
    const auto positionOffset = checkedAdd(vertex.offset, 6U, "CCF mesh vertex position");
    CcfChunk positionChunk{
        .id = readU16(bytes, static_cast<std::size_t>(positionOffset),
            "CCF mesh vertex position id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(positionOffset + 2U),
            "CCF mesh vertex position size"),
        .offset = positionOffset,
        .directChildren = {},
    };
    const auto position = parseCcfVector3(
        bytes, positionChunk, 0xF040U, "CCF mesh vertex position");
    const auto childBegin = checkedAdd(
        positionChunk.offset, positionChunk.totalSize, "CCF mesh vertex properties");
    const auto vertexEnd = checkedAdd(vertex.offset, vertex.totalSize, "CCF mesh vertex end");
    vertex.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(childBegin),
        static_cast<std::size_t>(vertexEnd),
        "CCF mesh vertex property",
        budget);
    CcfMeshVertexMetadata metadata{
        .position = position,
        .optionalVector = std::nullopt,
        .loaderVector = std::nullopt,
        .value4500 = std::nullopt,
        .offset = vertex.offset,
    };
    for (const auto& property : vertex.directChildren) {
        switch (property.id) {
        case 0xF040U:
            if (metadata.optionalVector.has_value()) {
                throw ParseError("CCF mesh vertex repeats its optional vector");
            }
            metadata.optionalVector = parseCcfVector3(
                bytes, property, 0xF040U, "CCF mesh vertex optional vector");
            break;
        case 0xF030U:
            if (metadata.loaderVector.has_value()) {
                throw ParseError("CCF mesh vertex repeats its loader vector");
            }
            metadata.loaderVector = parseCcfVector3(
                bytes, property, 0xF030U, "CCF mesh vertex loader vector");
            break;
        case 0x4500U:
            if (metadata.value4500.has_value() || property.totalSize != 10U) {
                throw ParseError("CCF mesh vertex has an invalid 0x4500 property");
            }
            metadata.value4500 = readU32(
                bytes, static_cast<std::size_t>(property.offset + 6U),
                "CCF mesh vertex 0x4500 value");
            break;
        default:
            // Unknown properties remain available in vertex.directChildren.
            break;
        }
    }
    return metadata;
}

[[nodiscard]] CcfMeshTriangleMetadata parseCcfMeshTriangle(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& triangle,
    CcfParseBudget& budget) {
    if (triangle.id != 0x3120U || triangle.totalSize < 22U) {
        throw ParseError("CCF mesh triangle has an invalid chunk");
    }
    const auto payload = checkedAdd(triangle.offset, 6U, "CCF mesh triangle payload");
    CcfMeshTriangleMetadata metadata{
        .vertexIndices = {
            readU32(bytes, static_cast<std::size_t>(payload), "CCF triangle index 0"),
            readU32(bytes, static_cast<std::size_t>(payload + 4U), "CCF triangle index 1"),
            readU32(bytes, static_cast<std::size_t>(payload + 8U), "CCF triangle index 2"),
        },
        .materialReference = readU32(
            bytes, static_cast<std::size_t>(payload + 12U), "CCF triangle material"),
        .textureCoordinates = std::nullopt,
        .paint = std::nullopt,
        .offset = triangle.offset,
    };
    const auto childBegin = checkedAdd(payload, 16U, "CCF mesh triangle properties");
    const auto triangleEnd = checkedAdd(
        triangle.offset, triangle.totalSize, "CCF mesh triangle end");
    triangle.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(childBegin),
        static_cast<std::size_t>(triangleEnd),
        "CCF mesh triangle property",
        budget);
    for (const auto& property : triangle.directChildren) {
        switch (property.id) {
        case 0xF060U: {
            if (metadata.textureCoordinates.has_value() || property.totalSize != 30U) {
                throw ParseError("CCF mesh triangle has invalid texture coordinates");
            }
            const auto coordinates = checkedAdd(
                property.offset, 6U, "CCF mesh texture coordinates");
            std::array<float, 6> values{};
            for (std::size_t index = 0U; index < values.size(); ++index) {
                values[index] = readFloat(
                    bytes,
                    static_cast<std::size_t>(coordinates + index * 4U),
                    "CCF mesh texture coordinate");
            }
            metadata.textureCoordinates = values;
            break;
        }
        case 0xF090U:
            if (metadata.paint.has_value()) {
                throw ParseError("CCF mesh triangle repeats its paint property");
            }
            metadata.paint = parseCcfMeshPaint(bytes, property, budget);
            break;
        default:
            // Unknown properties remain available in triangle.directChildren.
            break;
        }
    }
    return metadata;
}

[[nodiscard]] CcfMeshMetadata parseCcfMesh(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& mesh,
    CcfParseBudget& budget) {
    constexpr std::size_t kVertexLimit = 65'536U;
    constexpr std::size_t kTriangleLimit = 131'072U;
    if (mesh.id != 0x3100U) {
        throw ParseError("CCF mesh parser received a non-mesh chunk");
    }
    const auto payloadBegin = checkedAdd(mesh.offset, 6U, "CCF mesh payload");
    const auto meshEnd = checkedAdd(mesh.offset, mesh.totalSize, "CCF mesh end");
    requireRange(payloadBegin, 6U, meshEnd, "CCF mesh name header");
    CcfChunk nameChunk{
        .id = readU16(bytes, static_cast<std::size_t>(payloadBegin), "CCF mesh name id"),
        .totalSize = readU32(
            bytes, static_cast<std::size_t>(payloadBegin + 2U), "CCF mesh name size"),
        .offset = payloadBegin,
        .directChildren = {},
    };
    if (nameChunk.totalSize < 6U) {
        throw ParseError("CCF mesh name is shorter than its header");
    }
    requireRange(nameChunk.offset, nameChunk.totalSize, meshEnd, "CCF mesh name");
    const auto name = parseCcfName(bytes, nameChunk, "CCF mesh name", budget);
    auto cursor = checkedAdd(nameChunk.offset, nameChunk.totalSize, "CCF mesh fields");
    requireRange(cursor, 10U, meshEnd, "CCF mesh references and flags");
    const auto reference = readU32(bytes, static_cast<std::size_t>(cursor),
        "CCF mesh reference");
    const auto selectionFlagA = bytes[static_cast<std::size_t>(cursor + 4U)];
    const auto selectionFlagB = bytes[static_cast<std::size_t>(cursor + 5U)];
    const auto linkReference = readU32(bytes, static_cast<std::size_t>(cursor + 6U),
        "CCF mesh link reference");
    cursor = checkedAdd(cursor, 10U, "CCF mesh position");

    requireRange(cursor, 18U, meshEnd, "CCF mesh position");
    CcfChunk positionChunk{
        .id = readU16(bytes, static_cast<std::size_t>(cursor), "CCF mesh position id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(cursor + 2U),
            "CCF mesh position size"),
        .offset = cursor,
        .directChildren = {},
    };
    const auto position = parseCcfVector3(
        bytes, positionChunk, 0xF040U, "CCF mesh position");
    cursor = checkedAdd(cursor, positionChunk.totalSize, "CCF mesh scalar");
    requireRange(cursor, 4U, meshEnd, "CCF mesh scalar");
    const auto scalar = readFloat(bytes, static_cast<std::size_t>(cursor), "CCF mesh scalar");
    cursor = checkedAdd(cursor, 4U, "CCF mesh orientation");

    requireRange(cursor, 66U, meshEnd, "CCF mesh orientation");
    CcfChunk orientationChunk{
        .id = readU16(bytes, static_cast<std::size_t>(cursor), "CCF mesh orientation id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(cursor + 2U),
            "CCF mesh orientation size"),
        .offset = cursor,
        .directChildren = {},
    };
    const auto orientation = parseCcfOrientation(bytes, orientationChunk, budget);
    cursor = checkedAdd(cursor, orientationChunk.totalSize, "CCF mesh children");
    mesh.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(meshEnd),
        "CCF mesh child",
        budget);

    CcfMeshMetadata metadata{
        .name = name.name,
        .prefix = name.prefix,
        .reference = reference,
        .selectionFlagA = selectionFlagA,
        .selectionFlagB = selectionFlagB,
        .linkReference = linkReference,
        .position = position,
        .scalar = scalar,
        .orientation = orientation,
        .vertices = {},
        .triangles = {},
        .vampireMode = std::nullopt,
        .value4501 = std::nullopt,
        .propertyF0B2 = std::nullopt,
        .range = std::nullopt,
        .offset = mesh.offset,
    };
    for (auto& child : mesh.directChildren) {
        switch (child.id) {
        case 0x3110U:
            if (metadata.vertices.size() >= kVertexLimit) {
                throw ParseError("CCF mesh exceeds the vertex limit");
            }
            metadata.vertices.push_back(parseCcfMeshVertex(bytes, child, budget));
            break;
        case 0x3120U:
            if (metadata.triangles.size() >= kTriangleLimit) {
                throw ParseError("CCF mesh exceeds the triangle limit");
            }
            metadata.triangles.push_back(parseCcfMeshTriangle(bytes, child, budget));
            break;
        case 0xF0A0U:
            if (metadata.vampireMode.has_value() || child.totalSize != 10U) {
                throw ParseError("CCF mesh has an invalid 0xF0A0 property");
            }
            metadata.vampireMode = readU32(
                bytes, static_cast<std::size_t>(child.offset + 6U),
                "CCF mesh vampire mode");
            break;
        case 0x4501U:
            if (metadata.value4501.has_value() || child.totalSize != 10U) {
                throw ParseError("CCF mesh has an invalid 0x4501 property");
            }
            metadata.value4501 = readU32(
                bytes, static_cast<std::size_t>(child.offset + 6U),
                "CCF mesh 0x4501 value");
            break;
        case 0xF0B2U:
            if (metadata.propertyF0B2.has_value() || child.totalSize != 7U) {
                throw ParseError("CCF mesh has an invalid 0xF0B2 property");
            }
            metadata.propertyF0B2 = bytes[static_cast<std::size_t>(child.offset + 6U)];
            break;
        case 0xF0D0U: {
            if (metadata.range.has_value() || child.totalSize != 18U) {
                throw ParseError("CCF mesh has an invalid 0xF0D0 property");
            }
            const auto payload = checkedAdd(child.offset, 6U, "CCF mesh range");
            metadata.range = CcfMeshRangeMetadata{
                .enabled = readU32(bytes, static_cast<std::size_t>(payload),
                    "CCF mesh range enabled"),
                .first = readFloat(bytes, static_cast<std::size_t>(payload + 4U),
                    "CCF mesh range first"),
                .second = readFloat(bytes, static_cast<std::size_t>(payload + 8U),
                    "CCF mesh range second"),
            };
            break;
        }
        default:
            // Unknown children remain available in mesh.directChildren.
            break;
        }
    }
    for (const auto& triangle : metadata.triangles) {
        for (const auto index : triangle.vertexIndices) {
            if (index >= metadata.vertices.size()) {
                throw ParseError("CCF mesh triangle index exceeds the vertex table");
            }
        }
    }
    return metadata;
}

[[nodiscard]] CcfBlueprintMetadata parseCcfNonMeshBlueprint(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& blueprint,
    CcfParseBudget& budget) {
    if (blueprint.id != 0x4200U && blueprint.id != 0x4300U) {
        throw ParseError("CCF blueprint parser received an unsupported chunk");
    }
    const auto payloadBegin = checkedAdd(
        blueprint.offset, 6U, "CCF non-mesh blueprint payload");
    const auto blueprintEnd = checkedAdd(
        blueprint.offset, blueprint.totalSize, "CCF non-mesh blueprint end");
    requireRange(payloadBegin, 6U, blueprintEnd, "CCF non-mesh blueprint name header");
    CcfChunk nameChunk{
        .id = readU16(bytes, static_cast<std::size_t>(payloadBegin),
            "CCF non-mesh blueprint name id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(payloadBegin + 2U),
            "CCF non-mesh blueprint name size"),
        .offset = payloadBegin,
        .directChildren = {},
    };
    if (nameChunk.totalSize < 6U) {
        throw ParseError("CCF non-mesh blueprint name is shorter than its header");
    }
    requireRange(
        nameChunk.offset, nameChunk.totalSize, blueprintEnd,
        "CCF non-mesh blueprint name");
    const auto name = parseCcfName(
        bytes, nameChunk, "CCF non-mesh blueprint name", budget);
    auto cursor = checkedAdd(
        nameChunk.offset, nameChunk.totalSize, "CCF non-mesh blueprint references");
    requireRange(cursor, 12U, blueprintEnd, "CCF non-mesh blueprint references");
    const auto reference = readU32(
        bytes, static_cast<std::size_t>(cursor), "CCF non-mesh blueprint reference");
    const auto auxiliaryReference = readU32(
        bytes, static_cast<std::size_t>(cursor + 4U),
        "CCF non-mesh blueprint auxiliary reference");
    const auto parentReference = readU32(
        bytes, static_cast<std::size_t>(cursor + 8U),
        "CCF non-mesh blueprint parent reference");
    cursor = checkedAdd(cursor, 12U, "CCF non-mesh blueprint position");

    requireRange(cursor, 18U, blueprintEnd, "CCF non-mesh blueprint position");
    CcfChunk positionChunk{
        .id = readU16(bytes, static_cast<std::size_t>(cursor),
            "CCF non-mesh blueprint position id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(cursor + 2U),
            "CCF non-mesh blueprint position size"),
        .offset = cursor,
        .directChildren = {},
    };
    const auto position = parseCcfVector3(
        bytes, positionChunk, 0xF040U, "CCF non-mesh blueprint position");
    cursor = checkedAdd(cursor, positionChunk.totalSize, "CCF non-mesh blueprint scalar");
    requireRange(cursor, 4U, blueprintEnd, "CCF non-mesh blueprint scalar");
    const auto rawScalar = readFloat(
        bytes, static_cast<std::size_t>(cursor), "CCF non-mesh blueprint scalar");
    cursor = checkedAdd(cursor, 4U, "CCF non-mesh blueprint orientation");

    requireRange(cursor, 66U, blueprintEnd, "CCF non-mesh blueprint orientation");
    CcfChunk orientationChunk{
        .id = readU16(bytes, static_cast<std::size_t>(cursor),
            "CCF non-mesh blueprint orientation id"),
        .totalSize = readU32(bytes, static_cast<std::size_t>(cursor + 2U),
            "CCF non-mesh blueprint orientation size"),
        .offset = cursor,
        .directChildren = {},
    };
    const auto orientation = parseCcfOrientation(bytes, orientationChunk, budget);
    cursor = checkedAdd(
        cursor, orientationChunk.totalSize, "CCF non-mesh blueprint children");
    blueprint.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(blueprintEnd),
        "CCF non-mesh blueprint child",
        budget);
    return {
        .kind = blueprint.id == 0x4200U
            ? CcfBlueprintKind::nullNode
            : CcfBlueprintKind::light,
        .name = name.name,
        .prefix = name.prefix,
        .reference = reference,
        .auxiliaryReference = auxiliaryReference,
        .parentReference = parentReference,
        .authoredTransform = {
            .position = position,
            .rawScalar = rawScalar,
            .orientation = orientation,
        },
        .meshIndex = std::nullopt,
        .offset = blueprint.offset,
    };
}

} // namespace

std::uint32_t gtiBitsPerPixel(const std::uint32_t format) noexcept {
    switch (format) {
    case 1U: return 4U;
    case 2U:
    case 3U:
    case 9U:
    case 0x16U: return 8U;
    case 4U:
    case 5U:
    case 6U:
    case 0x0AU:
    case 0x0BU:
    case 0x0CU:
    case 0x12U:
    case 0x13U:
    case 0x17U:
    case 0x18U:
    case 0x19U:
    case 0x1AU:
    case 0x1CU:
    case 0x1DU:
    case 0x1EU:
    case 0x20U:
    case 0x21U: return 16U;
    case 7U:
    case 0x14U: return 24U;
    case 8U:
    case 0x15U:
    case 0x1BU:
    case 0x1FU: return 32U;
    default: return 0U;
    }
}

std::uint64_t expectedGtiPixelBytes(
    const std::uint32_t format,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t mipmapLevels) {
    const auto bits = gtiBitsPerPixel(format);
    if (bits == 0U || mipmapLevels == 0U || mipmapLevels > 16U) {
        throw ParseError("GTI uses an unsupported pixel format or mipmap count");
    }
    const auto pixels = checkedMultiply(width, height, "GTI pixel count");
    const auto baseBytes = checkedMultiply(pixels, bits, "GTI base bits") / 8U;
    std::uint64_t total = 0U;
    for (std::uint32_t level = 0U; level < mipmapLevels; ++level) {
        total = checkedAdd(total, baseBytes >> (level * 2U), "GTI mipmap bytes");
    }
    return total;
}

namespace {

[[nodiscard]] std::uint32_t decodableGtiBitsPerPixel(
    const GtiVariant& variant) {
    std::uint32_t bits = 0U;
    switch (variant.format) {
    case 3U: bits = 8U; break;
    case 4U:
    case 6U: bits = 16U; break;
    case 7U: bits = 24U; break;
    case 8U: bits = 32U; break;
    default:
        throw ParseError("GTI RGBA decoder does not implement this format");
    }

    if (variant.width == 0U || variant.height == 0U ||
        variant.mipmapLevels == 0U || variant.mipmapLevels > 16U) {
        throw ParseError("GTI image metadata is outside supported decode bounds");
    }
    if ((variant.format == 3U || variant.format == 4U) &&
        (variant.paletteEntries == 0U || variant.paletteEntries > 256U)) {
        throw ParseError("GTI paletted format has an invalid palette size");
    }
    if (variant.format != 3U && variant.format != 4U &&
        variant.paletteEntries != 0U) {
        throw ParseError("GTI direct-color format must not contain a palette");
    }
    return bits;
}

void requireGtiMipSource(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const GtiMipLevelLayout& layout) {
    const auto pixelDataEnd = checkedAdd(
        variant.pixelDataOffset, variant.pixelDataSize, "GTI pixel data extent");
    requireRange(
        layout.sourceOffset, layout.sourceSize, pixelDataEnd, "GTI mip pixels");
    requireRange(layout.sourceOffset, layout.sourceSize, bytes.size(), "GTI mip pixels");
}

[[nodiscard]] RgbaImage decodeGtiMipLayoutRgba(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const GtiMipLevelLayout& layout,
    const std::size_t outputLimit) {
    if (!layout.exactTexelLayout) {
        throw ParseError("GTI legacy mip size does not match its nominal texel layout");
    }
    requireGtiMipSource(bytes, variant, layout);

    const auto pixelCount = checkedMultiply(
        layout.width, layout.height, "GTI mip pixel count");
    const auto outputBytes = checkedMultiply(pixelCount, 4U, "GTI mip RGBA output");
    if (outputBytes > outputLimit ||
        outputBytes > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("GTI RGBA output exceeds the configured limit");
    }

    const auto paletteBytes = checkedMultiply(
        variant.paletteEntries, 4U, "GTI palette bytes");
    if (variant.pixelDataOffset < paletteBytes) {
        throw ParseError("GTI palette offset underflows");
    }
    const auto paletteOffset = variant.pixelDataOffset - paletteBytes;
    requireRange(paletteOffset, paletteBytes, bytes.size(), "GTI palette");

    RgbaImage image{
        .width = layout.width,
        .height = layout.height,
        .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(outputBytes)),
    };
    const auto source = bytes.subspan(
        static_cast<std::size_t>(layout.sourceOffset),
        static_cast<std::size_t>(layout.sourceSize));
    const auto palette = static_cast<std::size_t>(paletteOffset);

    const auto writePixel = [&image](
        const std::size_t index,
        const std::uint8_t red,
        const std::uint8_t green,
        const std::uint8_t blue,
        const std::uint8_t alpha) {
        const auto output = index * 4U;
        image.pixels[output] = red;
        image.pixels[output + 1U] = green;
        image.pixels[output + 2U] = blue;
        image.pixels[output + 3U] = alpha;
    };
    const auto palettePixel = [&](
        const std::size_t outputIndex,
        const std::uint8_t index,
        const std::uint8_t alpha) {
        if (index >= variant.paletteEntries) {
            throw ParseError("GTI palette index is outside the palette");
        }
        const auto entry = palette + static_cast<std::size_t>(index) * 4U;
        // CcColorInt/ARGB8888 is little-endian B,G,R,A in memory. The stored
        // palette alpha is not used by either paletted texture format.
        writePixel(
            outputIndex,
            bytes[entry + 2U],
            bytes[entry + 1U],
            bytes[entry],
            alpha);
    };

    for (std::size_t index = 0U; index < pixelCount; ++index) {
        switch (variant.format) {
        case 3U:
            palettePixel(index, source[index], 0xFFU);
            break;
        case 4U:
            palettePixel(index, source[index * 2U], source[index * 2U + 1U]);
            break;
        case 6U: {
            const auto packed = static_cast<std::uint16_t>(source[index * 2U]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(source[index * 2U + 1U]) << 8U);
            // Match the original conversion: nibbles occupy the high four
            // output bits (0xF becomes 0xF0 rather than replicated 0xFF).
            writePixel(
                index,
                static_cast<std::uint8_t>((packed & 0x0F00U) >> 4U),
                static_cast<std::uint8_t>(packed & 0x00F0U),
                static_cast<std::uint8_t>((packed & 0x000FU) << 4U),
                static_cast<std::uint8_t>((packed & 0xF000U) >> 8U));
            break;
        }
        case 7U:
            writePixel(
                index,
                source[index * 3U],
                source[index * 3U + 1U],
                source[index * 3U + 2U],
                0xFFU);
            break;
        case 8U:
            writePixel(
                index,
                source[index * 4U + 2U],
                source[index * 4U + 1U],
                source[index * 4U],
                source[index * 4U + 3U]);
            break;
        default:
            throw ParseError("GTI RGBA decoder does not implement this format");
        }
    }
    return image;
}

} // namespace

std::vector<GtiMipLevelLayout> describeGtiMipLevels(
    const GtiVariant& variant) {
    const auto bits = decodableGtiBitsPerPixel(variant);
    const auto basePixelCount = checkedMultiply(
        variant.width, variant.height, "GTI base pixel count");
    const auto baseBytes = checkedMultiply(
        basePixelCount, bits, "GTI base bits") / 8U;

    std::vector<GtiMipLevelLayout> layouts;
    layouts.reserve(variant.mipmapLevels);
    auto sourceOffset = variant.pixelDataOffset;
    for (std::uint32_t level = 0U; level < variant.mipmapLevels; ++level) {
        const auto width = std::max(1U, variant.width >> level);
        const auto height = std::max(1U, variant.height >> level);
        const auto sourceSize = baseBytes >> (level * 2U);
        const auto requiredPixels = checkedMultiply(
            width, height, "GTI mip pixel count");
        const auto requiredTexelBytes = checkedMultiply(
            requiredPixels, bits, "GTI mip texel bits") / 8U;
        layouts.push_back({
            .level = level,
            .width = width,
            .height = height,
            .sourceOffset = sourceOffset,
            .sourceSize = sourceSize,
            .requiredTexelBytes = requiredTexelBytes,
            .exactTexelLayout = sourceSize == requiredTexelBytes,
        });
        sourceOffset = checkedAdd(sourceOffset, sourceSize, "GTI mip source offset");
    }
    return layouts;
}

RgbaImage decodeGtiMipLevelRgba(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const std::uint32_t level,
    const std::size_t outputLimit) {
    const auto layouts = describeGtiMipLevels(variant);
    if (level >= layouts.size()) {
        throw ParseError("GTI mip level index is outside the image");
    }
    return decodeGtiMipLayoutRgba(bytes, variant, layouts[level], outputLimit);
}

RgbaMipChain decodeGtiMipChainRgba(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const std::size_t outputLimit) {
    const auto layouts = describeGtiMipLevels(variant);
    std::uint64_t totalOutputBytes = 0U;
    for (const auto& layout : layouts) {
        if (!layout.exactTexelLayout) {
            throw ParseError("GTI legacy mip size does not match its nominal texel layout");
        }
        requireGtiMipSource(bytes, variant, layout);
        const auto levelOutputBytes = checkedMultiply(
            checkedMultiply(layout.width, layout.height, "GTI mip pixel count"),
            4U,
            "GTI mip RGBA output");
        totalOutputBytes = checkedAdd(
            totalOutputBytes, levelOutputBytes, "GTI mip-chain RGBA output");
        if (levelOutputBytes > outputLimit || totalOutputBytes > outputLimit ||
            levelOutputBytes > std::numeric_limits<std::size_t>::max() ||
            totalOutputBytes > std::numeric_limits<std::size_t>::max()) {
            throw ParseError("GTI RGBA output exceeds the configured limit");
        }
    }

    RgbaMipChain chain;
    chain.levels.reserve(layouts.size());
    for (const auto& layout : layouts) {
        chain.levels.push_back(
            decodeGtiMipLayoutRgba(bytes, variant, layout, outputLimit));
    }
    return chain;
}

RgbaImage decodeGtiBaseRgba(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const std::size_t outputLimit) {
    return decodeGtiMipLevelRgba(bytes, variant, 0U, outputLimit);
}

GtiMetadata parseGti(
    const std::span<const std::uint8_t> bytes,
    const GtiParseLimits& limits) {
    if (bytes.size() < 8U || readU32(bytes, 0U, "GTI magic") != kGtiMagic) {
        throw ParseError("invalid GTI magic");
    }
    if (readU32(bytes, 4U, "GTI version") != kGtiVersion) {
        throw ParseError("unsupported GTI version");
    }

    GtiMetadata metadata;
    std::set<std::uint32_t> formats;
    std::size_t cursor = 8U;
    while (cursor < bytes.size()) {
        requireRange(cursor, 8U, bytes.size(), "GTI chunk header");
        const auto id = readU32(bytes, cursor, "GTI chunk id");
        const auto payloadSize = readU32(bytes, cursor + 4U, "GTI chunk size");
        const auto payloadOffset = checkedAdd(cursor, 8U, "GTI chunk payload");
        const auto declaredEnd = checkedAdd(payloadOffset, payloadSize, "GTI chunk payload");
        auto availablePayloadSize = static_cast<std::uint64_t>(payloadSize);
        if (declaredEnd > bytes.size()) {
            // Some shipped GTI files end inside the declared extent of their
            // final Imag variant. The original loader seeks to the declared
            // end (which terminates its scan) but reads the selected variant
            // using format-derived byte counts. Preserve this observed quirk
            // without allowing a following chunk or an undersized metadata
            // header.
            if (id != kGtiImageChunk || payloadOffset > bytes.size()) {
                throw ParseError("GTI non-image chunk payload exceeds the file");
            }
            availablePayloadSize = bytes.size() - payloadOffset;
            metadata.terminalDeclaredOverrun = true;
        }
        requireGtiMetadataCapacity(
            nextGtiRecordCount(metadata.chunks.size()),
            metadata.variants.size(),
            limits);
        metadata.chunks.push_back({
            .id = id,
            .payloadSize = payloadSize,
            .payloadOffset = payloadOffset,
        });

        if (id == kGtiChecksumChunk) {
            if (payloadSize != 4U) {
                throw ParseError("invalid GTI checksum chunk");
            }
            metadata.checksum = readU32(
                bytes, static_cast<std::size_t>(payloadOffset), "GTI checksum");
            ++metadata.checksumChunkCount;
        }
        else if (id == kGtiImageChunk) {
            if (availablePayloadSize < 20U) {
                throw ParseError("GTI image chunk is shorter than its metadata");
            }
            const auto payload = static_cast<std::size_t>(payloadOffset);
            const auto format = readU32(bytes, payload, "GTI format");
            const auto width = readU32(bytes, payload + 4U, "GTI width");
            const auto height = readU32(bytes, payload + 8U, "GTI height");
            const auto paletteEntries = readU32(bytes, payload + 12U, "GTI palette count");
            const auto mipmapLevels = readU32(bytes, payload + 16U, "GTI mipmap count");
            if (width == 0U || height == 0U || width > 32768U || height > 32768U ||
                paletteEntries > 65536U || mipmapLevels == 0U || mipmapLevels > 16U) {
                throw ParseError("GTI image metadata is outside supported bounds");
            }
            const auto paletteBytes = static_cast<std::uint64_t>(paletteEntries) * 4U;
            if (paletteBytes > availablePayloadSize - 20U) {
                throw ParseError("GTI palette exceeds image chunk");
            }
            requireGtiMetadataCapacity(
                metadata.chunks.size(),
                nextGtiRecordCount(metadata.variants.size()),
                limits);
            if (!formats.insert(format).second) {
                throw ParseError("GTI contains duplicate image format");
            }
            const auto pixelDataOffset = checkedAdd(
                payloadOffset, checkedAdd(20U, paletteBytes, "GTI image metadata"),
                "GTI pixel data");
            const auto actualPixelBytes = checkedAdd(
                payloadOffset, availablePayloadSize, "GTI image end") - pixelDataOffset;
            const auto expectedPixelBytes = expectedGtiPixelBytes(
                format, width, height, mipmapLevels);
            if (actualPixelBytes < expectedPixelBytes) {
                throw ParseError("GTI image chunk is shorter than its computed mipmap data");
            }
            metadata.variants.push_back({
                .format = format,
                .width = width,
                .height = height,
                .paletteEntries = paletteEntries,
                .mipmapLevels = mipmapLevels,
                .pixelDataOffset = pixelDataOffset,
                .pixelDataSize = static_cast<std::uint32_t>(actualPixelBytes),
                .expectedPixelDataSize = expectedPixelBytes,
                .trailingBytes = actualPixelBytes - expectedPixelBytes,
            });
        }
        cursor = declaredEnd > bytes.size()
            ? bytes.size()
            : static_cast<std::size_t>(declaredEnd);
    }
    if (metadata.variants.empty()) {
        throw ParseError("GTI contains no image variant");
    }
    return metadata;
}

CcfMetadata parseCcf(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 14U || readU32(bytes, 0U, "CCF magic") != kCcfMagic) {
        throw ParseError("invalid CCF magic");
    }
    if (readU32(bytes, 4U, "CCF version") != kCcfVersion) {
        throw ParseError("unsupported CCF version");
    }
    const auto rootId = readU16(bytes, 8U, "CCF root id");
    const auto rootSize = readU32(bytes, 10U, "CCF root size");
    if (rootId != 1U || rootSize < 6U ||
        checkedAdd(8U, rootSize, "CCF root") != bytes.size()) {
        throw ParseError("CCF has an invalid root chunk");
    }

    CcfMetadata metadata{
        .rootId = rootId,
        .rootSize = rootSize,
        .topLevelChunks = {},
        .rooms = {},
        .materials = {},
        .meshes = {},
        .blueprints = {},
        .placedNodes = {},
    };
    constexpr std::size_t kCcfDescriptorLimit = 250'000U;
    CcfParseBudget budget{
        .remainingDescriptors = std::min(kCcfDescriptorLimit, bytes.size() / 6U),
    };
    CcfSpatialBudget spatialBudget;
    metadata.topLevelChunks = parseCcfChunkSequence(
        bytes, 14U, bytes.size(), "CCF root", budget);
    constexpr std::size_t kCcfRoomLimit = 100'000U;
    constexpr std::size_t kCcfMeshLimit = 100'000U;
    constexpr std::size_t kCcfBlueprintLimit = 100'000U;
    constexpr std::size_t kCcfPlacedNodeLimit = 100'000U;
    constexpr std::size_t kCcfVertexLimit = 2'000'000U;
    constexpr std::size_t kCcfTriangleLimit = 4'000'000U;
    std::size_t vertexCount = 0U;
    std::size_t triangleCount = 0U;
    for (auto& section : metadata.topLevelChunks) {
        const auto childBegin = checkedAdd(section.offset, 6U, "CCF section payload");
        const auto childEnd = checkedAdd(section.offset, section.totalSize, "CCF section end");
        section.directChildren = parseCcfChunkSequence(
            bytes,
            static_cast<std::size_t>(childBegin),
            static_cast<std::size_t>(childEnd),
            "CCF section",
            budget);
        if (section.id == 0x1000U) {
            for (auto& room : section.directChildren) {
                if (room.id != 0x1100U) {
                    continue;
                }
                if (metadata.rooms.size() >= kCcfRoomLimit) {
                    throw ParseError("CCF exceeds the room limit");
                }
                metadata.rooms.push_back(parseCcfRoom(
                    bytes,
                    room,
                    budget,
                    spatialBudget,
                    metadata.rooms.empty()));
            }
        }
        else if (section.id == 0x2000U) {
            for (auto& material : section.directChildren) {
                if (material.id == 0x2100U) {
                    metadata.materials.push_back(parseCcfMaterial(bytes, material, budget));
                }
            }
        }
        else if (section.id == 0x3000U) {
            for (auto& blueprint : section.directChildren) {
                if (blueprint.id == 0x3100U) {
                    if (metadata.blueprints.size() >= kCcfBlueprintLimit) {
                        throw ParseError("CCF exceeds the blueprint limit");
                    }
                    if (metadata.meshes.size() >= kCcfMeshLimit) {
                        throw ParseError("CCF exceeds the mesh limit");
                    }
                    auto parsed = parseCcfMesh(bytes, blueprint, budget);
                    if (parsed.vertices.size() > kCcfVertexLimit - vertexCount ||
                        parsed.triangles.size() > kCcfTriangleLimit - triangleCount) {
                        throw ParseError("CCF exceeds the geometry descriptor limit");
                    }
                    vertexCount += parsed.vertices.size();
                    triangleCount += parsed.triangles.size();
                    metadata.blueprints.push_back({
                        .kind = CcfBlueprintKind::mesh,
                        .name = parsed.name,
                        .prefix = parsed.prefix,
                        .reference = parsed.reference,
                        .auxiliaryReference = std::nullopt,
                        .parentReference = parsed.linkReference,
                        .authoredTransform = {
                            .position = parsed.position,
                            .rawScalar = parsed.scalar,
                            .orientation = parsed.orientation,
                        },
                        .meshIndex = metadata.meshes.size(),
                        .offset = parsed.offset,
                    });
                    metadata.meshes.push_back(std::move(parsed));
                }
                else if (blueprint.id == 0x4200U || blueprint.id == 0x4300U) {
                    if (metadata.blueprints.size() >= kCcfBlueprintLimit) {
                        throw ParseError("CCF exceeds the blueprint limit");
                    }
                    metadata.blueprints.push_back(
                        parseCcfNonMeshBlueprint(bytes, blueprint, budget));
                }
            }
        }
        else if (section.id == 0x4000U) {
            for (auto& node : section.directChildren) {
                if (node.id != 0x4100U && node.id != 0x4200U && node.id != 0x4300U) {
                    continue;
                }
                if (metadata.placedNodes.size() >= kCcfPlacedNodeLimit) {
                    throw ParseError("CCF exceeds the placed-node limit");
                }
                metadata.placedNodes.push_back(parseCcfPlacedNode(bytes, node, budget));
            }
        }
    }
    return metadata;
}

} // namespace airfix::assets
