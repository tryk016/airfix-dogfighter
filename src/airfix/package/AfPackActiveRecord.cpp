#include "airfix/package/AfPackActiveRecord.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace airfix::afpack {
namespace {

constexpr std::size_t kHeaderSize = 16U;
constexpr std::size_t kGenerationOffset = 16U;
constexpr std::size_t kCurrentSizeOffset = 24U;
constexpr std::size_t kCurrentDigestOffset = 32U;
constexpr std::size_t kPreviousSizeOffset = 64U;
constexpr std::size_t kPreviousDigestOffset = 72U;

[[nodiscard]] std::uint16_t readU16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t readU64(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

void writeU16(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint32_t value) noexcept {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void writeU64(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint64_t value) noexcept {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

[[nodiscard]] crypto::Sha256Digest readDigest(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) {
    crypto::Sha256Digest digest{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), digest.size(), digest.begin());
    return digest;
}

void writeDigest(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const crypto::Sha256Digest& digest) {
    std::copy(digest.begin(), digest.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] bool isZeroDigest(const crypto::Sha256Digest& digest) noexcept {
    return std::all_of(digest.begin(), digest.end(), [](const std::uint8_t byte) {
        return byte == 0U;
    });
}

void validateReference(
    const ActivePackReference& reference,
    const std::uint64_t maxPackBytes,
    const std::string_view field) {
    if (reference.size == 0U || reference.size > maxPackBytes) {
        throw ActiveRecordError(std::string(field) + " size is outside the configured limit");
    }
    if (isZeroDigest(reference.sha256)) {
        throw ActiveRecordError(std::string(field) + " digest is all zero");
    }
}

void validateRecord(const ActiveRecord& record, const std::uint64_t maxPackBytes) {
    if (record.generation == 0U) {
        throw ActiveRecordError("AFAC generation must be positive");
    }
    validateReference(record.current, maxPackBytes, "current pack");
    if (record.previous.has_value()) {
        validateReference(*record.previous, maxPackBytes, "previous pack");
    }
}

} // namespace

ActiveRecord parseActiveRecord(
    const std::span<const std::uint8_t> bytes,
    const std::uint64_t maxPackBytes) {
    if (bytes.size() < kHeaderSize) {
        throw ActiveRecordError("record is shorter than the AFAC header");
    }
    if (bytes[0] != 'A' || bytes[1] != 'F' || bytes[2] != 'A' || bytes[3] != 'C') {
        throw ActiveRecordError("invalid AFAC magic");
    }
    if (readU16(bytes, 4U) != kActiveRecordVersion) {
        throw ActiveRecordError("unsupported AFAC version");
    }

    const auto flags = readU16(bytes, 6U);
    if ((flags & static_cast<std::uint16_t>(~kActiveRecordPreviousFlag)) != 0U) {
        throw ActiveRecordError("unsupported AFAC flags");
    }
    if (readU32(bytes, 12U) != 0U) {
        throw ActiveRecordError("AFAC reserved field must be zero");
    }

    const bool hasPrevious = (flags & kActiveRecordPreviousFlag) != 0U;
    const auto expectedSize = hasPrevious ?
        kActiveRecordWithPreviousSize : kActiveRecordSize;
    if (readU32(bytes, 8U) != expectedSize || bytes.size() != expectedSize) {
        throw ActiveRecordError("AFAC record size does not match its flags or physical size");
    }

    ActiveRecord record{
        .generation = readU64(bytes, kGenerationOffset),
        .current = {
            .size = readU64(bytes, kCurrentSizeOffset),
            .sha256 = readDigest(bytes, kCurrentDigestOffset),
        },
        .previous = std::nullopt,
    };
    if (hasPrevious) {
        record.previous = ActivePackReference{
            .size = readU64(bytes, kPreviousSizeOffset),
            .sha256 = readDigest(bytes, kPreviousDigestOffset),
        };
    }
    validateRecord(record, maxPackBytes);
    return record;
}

std::vector<std::uint8_t> serializeActiveRecord(
    const ActiveRecord& record,
    const std::uint64_t maxPackBytes) {
    validateRecord(record, maxPackBytes);

    const bool hasPrevious = record.previous.has_value();
    const auto recordSize = hasPrevious ?
        kActiveRecordWithPreviousSize : kActiveRecordSize;
    std::vector<std::uint8_t> bytes(recordSize, 0U);
    bytes[0] = 'A';
    bytes[1] = 'F';
    bytes[2] = 'A';
    bytes[3] = 'C';
    writeU16(bytes, 4U, kActiveRecordVersion);
    writeU16(bytes, 6U, hasPrevious ? kActiveRecordPreviousFlag : 0U);
    writeU32(bytes, 8U, static_cast<std::uint32_t>(recordSize));
    writeU64(bytes, kGenerationOffset, record.generation);
    writeU64(bytes, kCurrentSizeOffset, record.current.size);
    writeDigest(bytes, kCurrentDigestOffset, record.current.sha256);
    if (hasPrevious) {
        writeU64(bytes, kPreviousSizeOffset, record.previous->size);
        writeDigest(bytes, kPreviousDigestOffset, record.previous->sha256);
    }
    return bytes;
}

std::string packFileName(const crypto::Sha256Digest& digest) {
    constexpr std::array<char, 16> hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string filename = "pack-";
    filename.reserve(5U + digest.size() * 2U + 7U);
    for (const auto byte : digest) {
        filename.push_back(hex[byte >> 4U]);
        filename.push_back(hex[byte & 0x0FU]);
    }
    filename += ".afpack";
    return filename;
}

ActiveRecord makeNextActive(
    const crypto::Sha256Digest& currentDigest,
    const std::uint64_t currentSize,
    const std::optional<ActiveRecord>& old,
    const std::uint64_t maxPackBytes) {
    const ActivePackReference current{
        .size = currentSize,
        .sha256 = currentDigest,
    };
    validateReference(current, maxPackBytes, "current pack");

    if (!old.has_value()) {
        return {
            .generation = 1U,
            .current = current,
            .previous = std::nullopt,
        };
    }

    validateRecord(*old, maxPackBytes);
    if (old->generation == std::numeric_limits<std::uint64_t>::max()) {
        throw ActiveRecordError("AFAC generation overflows");
    }
    return {
        .generation = old->generation + 1U,
        .current = current,
        .previous = old->current,
    };
}

} // namespace airfix::afpack
