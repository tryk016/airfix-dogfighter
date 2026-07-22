#pragma once

#include "airfix/crypto/Sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::afpack {

inline constexpr std::uint16_t kActiveRecordVersion = 1U;
inline constexpr std::uint16_t kActiveRecordPreviousFlag = 1U;
inline constexpr std::size_t kActiveRecordSize = 64U;
inline constexpr std::size_t kActiveRecordWithPreviousSize = 104U;
inline constexpr std::uint64_t kDefaultMaxActivePackBytes = 512U * 1024U * 1024U;

// AFAC v1 is a canonical, fixed-layout little-endian active-package pointer.
// The 64-byte base record is:
//   magic[4] = "AFAC", version u16, flags u16, recordSize u32,
//   reserved u32, generation u64, currentSize u64, current SHA-256[32].
// When flags bit 0 is set, the record is 104 bytes and appends:
//   previousSize u64, previous SHA-256[32].
// No path is stored. A package filename is derived only from its digest.

class ActiveRecordError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ActivePackReference {
    std::uint64_t size{};
    crypto::Sha256Digest sha256{};

    bool operator==(const ActivePackReference&) const = default;
};

struct ActiveRecord {
    std::uint64_t generation{};
    ActivePackReference current;
    std::optional<ActivePackReference> previous;

    bool operator==(const ActiveRecord&) const = default;
};

[[nodiscard]] ActiveRecord parseActiveRecord(
    std::span<const std::uint8_t> bytes,
    std::uint64_t maxPackBytes = kDefaultMaxActivePackBytes);

[[nodiscard]] std::vector<std::uint8_t> serializeActiveRecord(
    const ActiveRecord& record,
    std::uint64_t maxPackBytes = kDefaultMaxActivePackBytes);

[[nodiscard]] std::string packFileName(const crypto::Sha256Digest& digest);

[[nodiscard]] ActiveRecord makeNextActive(
    const crypto::Sha256Digest& currentDigest,
    std::uint64_t currentSize,
    const std::optional<ActiveRecord>& old = std::nullopt,
    std::uint64_t maxPackBytes = kDefaultMaxActivePackBytes);

} // namespace airfix::afpack
