#include "airfix/package/AfPackActiveRecord.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using airfix::afpack::ActivePackReference;
using airfix::afpack::ActiveRecord;
using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] airfix::crypto::Sha256Digest digestFrom(const std::uint8_t first) {
    airfix::crypto::Sha256Digest digest{};
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        digest[index] = static_cast<std::uint8_t>(first + index);
    }
    return digest;
}

[[nodiscard]] ActiveRecord recordWithoutPrevious() {
    return {
        .generation = 0x0102030405060708ULL,
        .current = {.size = 0x11223344U, .sha256 = digestFrom(1U)},
        .previous = std::nullopt,
    };
}

[[nodiscard]] ActiveRecord recordWithPrevious() {
    auto record = recordWithoutPrevious();
    record.previous = ActivePackReference{
        .size = 0x05667788U,
        .sha256 = digestFrom(0x81U),
    };
    return record;
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireActiveError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::afpack::ActiveRecordError&) {
        return;
    }
    throw std::runtime_error("expected ActiveRecordError");
}

void writeU16(Bytes& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void writeU64(Bytes& bytes, const std::size_t offset, const std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void testRoundTrips() {
    for (const auto& record : {recordWithoutPrevious(), recordWithPrevious()}) {
        const auto bytes = airfix::afpack::serializeActiveRecord(record);
        require(airfix::afpack::parseActiveRecord(bytes) == record,
            "AFAC round trip changed a record");
        require(airfix::afpack::serializeActiveRecord(record) == bytes,
            "AFAC serialization is not deterministic");
    }
}

void testCanonicalBytes() {
    const auto record = recordWithoutPrevious();
    const auto bytes = airfix::afpack::serializeActiveRecord(record);
    require(bytes.size() == airfix::afpack::kActiveRecordSize,
        "base AFAC has the wrong size");
    require(std::equal(bytes.begin(), bytes.begin() + 4, "AFAC"),
        "AFAC magic is not canonical");
    require(bytes[4] == 1U && bytes[5] == 0U && bytes[6] == 0U && bytes[7] == 0U,
        "AFAC version or flags are not little-endian");
    require(bytes[8] == 64U && bytes[9] == 0U && bytes[10] == 0U && bytes[11] == 0U,
        "AFAC recordSize is not canonical");
    require(std::all_of(bytes.begin() + 12, bytes.begin() + 16,
        [](const std::uint8_t byte) { return byte == 0U; }),
        "AFAC reserved bytes are not zero");
    require(bytes[16] == 0x08U && bytes[23] == 0x01U,
        "AFAC generation is not little-endian");
    require(bytes[24] == 0x44U && bytes[27] == 0x11U,
        "AFAC current size is not little-endian");
    require(std::equal(record.current.sha256.begin(), record.current.sha256.end(),
        bytes.begin() + 32), "AFAC current digest changed");

    const auto withPrevious = airfix::afpack::serializeActiveRecord(recordWithPrevious());
    require(withPrevious.size() == airfix::afpack::kActiveRecordWithPreviousSize &&
        withPrevious[6] == 1U && withPrevious[8] == 104U,
        "extended AFAC header is not canonical");
}

void testMalformedRecords() {
    const auto base = airfix::afpack::serializeActiveRecord(recordWithoutPrevious());

    for (std::size_t size = 0U; size < base.size(); ++size) {
        requireActiveError([&base, size] {
            static_cast<void>(airfix::afpack::parseActiveRecord(
                std::span<const std::uint8_t>(base).first(size)));
        });
    }

    auto extra = base;
    extra.push_back(0U);
    requireActiveError([&extra] {
        static_cast<void>(airfix::afpack::parseActiveRecord(extra));
    });

    const std::vector<std::pair<std::size_t, std::uint8_t>> byteMutations{
        {0U, 'X'},
        {4U, 2U},
        {6U, 2U},
        {8U, 63U},
        {12U, 1U},
    };
    for (const auto [offset, value] : byteMutations) {
        auto malformed = base;
        malformed[offset] = value;
        requireActiveError([&malformed] {
            static_cast<void>(airfix::afpack::parseActiveRecord(malformed));
        });
    }

    auto zeroGeneration = base;
    writeU64(zeroGeneration, 16U, 0U);
    requireActiveError([&zeroGeneration] {
        static_cast<void>(airfix::afpack::parseActiveRecord(zeroGeneration));
    });

    auto zeroSize = base;
    writeU64(zeroSize, 24U, 0U);
    requireActiveError([&zeroSize] {
        static_cast<void>(airfix::afpack::parseActiveRecord(zeroSize));
    });

    auto largeSize = base;
    writeU64(largeSize, 24U, 101U);
    requireActiveError([&largeSize] {
        static_cast<void>(airfix::afpack::parseActiveRecord(largeSize, 100U));
    });

    auto zeroDigest = base;
    std::fill(zeroDigest.begin() + 32, zeroDigest.begin() + 64, 0U);
    requireActiveError([&zeroDigest] {
        static_cast<void>(airfix::afpack::parseActiveRecord(zeroDigest));
    });

    auto inconsistentPrevious = base;
    writeU16(inconsistentPrevious, 6U, 1U);
    requireActiveError([&inconsistentPrevious] {
        static_cast<void>(airfix::afpack::parseActiveRecord(inconsistentPrevious));
    });

    auto badExtended = airfix::afpack::serializeActiveRecord(recordWithPrevious());
    writeU64(badExtended, 64U, 0U);
    requireActiveError([&badExtended] {
        static_cast<void>(airfix::afpack::parseActiveRecord(badExtended));
    });
    badExtended = airfix::afpack::serializeActiveRecord(recordWithPrevious());
    std::fill(badExtended.begin() + 72, badExtended.end(), 0U);
    requireActiveError([&badExtended] {
        static_cast<void>(airfix::afpack::parseActiveRecord(badExtended));
    });

    auto badRecord = recordWithoutPrevious();
    badRecord.current.size = 0U;
    requireActiveError([&badRecord] {
        static_cast<void>(airfix::afpack::serializeActiveRecord(badRecord));
    });
}

void testFilename() {
    airfix::crypto::Sha256Digest digest{};
    digest[0] = 0x00U;
    digest[1] = 0x01U;
    digest[2] = 0x0AU;
    digest[3] = 0x10U;
    digest[4] = 0xABU;
    digest[5] = 0xCDU;
    digest[6] = 0xEFU;
    const auto filename = airfix::afpack::packFileName(digest);
    require(filename ==
        "pack-00010a10abcdef00000000000000000000000000000000000000000000000000.afpack",
        "derived AFPACK filename is not exact lowercase hex");
}

void testRotation() {
    const auto firstDigest = digestFrom(1U);
    const auto secondDigest = digestFrom(33U);
    const auto thirdDigest = digestFrom(65U);

    const auto first = airfix::afpack::makeNextActive(firstDigest, 11U);
    require(first.generation == 1U && first.current.sha256 == firstDigest &&
        first.current.size == 11U && !first.previous.has_value(),
        "first AFAC generation is wrong");

    const auto second = airfix::afpack::makeNextActive(secondDigest, 22U, first);
    require(second.generation == 2U && second.current.sha256 == secondDigest &&
        second.previous == std::optional<ActivePackReference>(first.current),
        "AFAC did not rotate current to previous");

    const auto third = airfix::afpack::makeNextActive(thirdDigest, 33U, second);
    require(third.generation == 3U && third.previous ==
        std::optional<ActivePackReference>(second.current) &&
        third.previous != second.previous,
        "AFAC retained an obsolete previous generation");

    auto overflow = third;
    overflow.generation = std::numeric_limits<std::uint64_t>::max();
    requireActiveError([&overflow, &firstDigest] {
        static_cast<void>(airfix::afpack::makeNextActive(firstDigest, 44U, overflow));
    });

    requireActiveError([&firstDigest] {
        static_cast<void>(airfix::afpack::makeNextActive(firstDigest, 101U, std::nullopt, 100U));
    });
    requireActiveError([] {
        static_cast<void>(airfix::afpack::makeNextActive({}, 1U));
    });
}

} // namespace

int main() {
    try {
        testRoundTrips();
        testCanonicalBytes();
        testMalformedRecords();
        testFilename();
        testRotation();
    }
    catch (const std::exception& error) {
        std::cerr << "AfPackActiveRecordTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "AfPackActiveRecordTests passed\n";
    return 0;
}
