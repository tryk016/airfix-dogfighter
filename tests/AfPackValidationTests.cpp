#include "airfix/archive/UdspArchive.hpp"
#include "airfix/package/AfPackValidation.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Error>
void requireErrorContaining(
    const std::function<void()>& action,
    const std::string_view expected) {
    try {
        action();
    }
    catch (const Error& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
            "exception did not contain the expected diagnostic");
        return;
    }
    throw std::runtime_error("expected exception was not thrown");
}

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void appendRecord(
    Bytes& bytes,
    const std::uint32_t hash,
    const std::uint32_t nameOffset,
    const std::uint32_t field08,
    const std::uint32_t field0C,
    const std::uint32_t field10,
    const std::uint32_t field14) {
    appendU32(bytes, hash);
    appendU32(bytes, nameOffset);
    appendU32(bytes, field08);
    appendU32(bytes, field0C);
    appendU32(bytes, field10);
    appendU32(bytes, field14);
}

[[nodiscard]] Bytes makeUdsp(const std::size_t payloadBytes = 3U) {
    constexpr std::string_view directory = "dir";
    constexpr std::string_view name = "entry.bin";
    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto dataOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t index = 0U; index < payloadBytes; ++index) {
        archive.push_back(static_cast<std::uint8_t>(index * 37U + 11U));
    }

    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(archive, airfix::udsp::nameHash(directory), 0U, 0U, 0U, 1U, 0U);
    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive,
        airfix::udsp::nameHash(name),
        static_cast<std::uint32_t>(directory.size() + 1U),
        0U,
        static_cast<std::uint32_t>(payloadBytes),
        static_cast<std::uint32_t>(payloadBytes),
        dataOffset);
    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.push_back(0U);
    archive.insert(archive.end(), name.begin(), name.end());
    archive.push_back(0U);

    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, airfix::udsp::kRecordSize);
    writeU32(archive, 12U, directoryOffset);
    writeU32(archive, 16U,
        static_cast<std::uint32_t>(archive.size()) - stringOffset);
    writeU32(archive, 20U, stringOffset);
    writeU32(archive, 24U, airfix::udsp::kRecordSize);
    writeU32(archive, 28U, fileOffset);
    return archive;
}

class Fixture final {
public:
    Fixture() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-validation-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(1U)));
        if (!std::filesystem::create_directory(root_)) {
            throw std::runtime_error("cannot create validation test directory");
        }
        packPath_ = root_ / "candidate.afpack";
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void write(const Bytes& source, const Bytes& localization) {
        const auto sourcePath = root_ / "Resource.up";
        const auto localizationPath = root_ / "English.up";
        writeBytes(sourcePath, source);
        writeBytes(localizationPath, localization);
        result_ = airfix::afpack::writePack({
            .outputPath = packPath_,
            .manifest = {
                .sourceVersion = "1.01",
                .converterVersion = "validation-tests",
                .converterCommit = "synthetic",
                .locale = "en",
            },
            .entries = {
                {
                    .logicalPath = "source/Resource.up",
                    .kind = airfix::afpack::EntryKind::sourceArchive,
                    .sourcePath = sourcePath,
                },
                {
                    .logicalPath = "localization/English.up",
                    .kind = airfix::afpack::EntryKind::localization,
                    .sourcePath = localizationPath,
                },
            },
        });
    }

    [[nodiscard]] const std::filesystem::path& packPath() const noexcept {
        return packPath_;
    }

    [[nodiscard]] const airfix::afpack::WriteResult& result() const noexcept {
        return result_;
    }

private:
    static void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
        std::ofstream output(path, std::ios::binary);
        if (!output || (!bytes.empty() && !output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))) {
            throw std::runtime_error("cannot write validation test input");
        }
    }

    std::filesystem::path root_;
    std::filesystem::path packPath_;
    airfix::afpack::WriteResult result_;
};

[[nodiscard]] const airfix::afpack::Entry& findEntry(
    const std::vector<airfix::afpack::Entry>& entries,
    const std::string_view path) {
    const auto match = std::find_if(entries.begin(), entries.end(),
        [&](const auto& entry) { return entry.path == path; });
    if (match == entries.end()) {
        throw std::runtime_error("test AFPACK entry missing");
    }
    return *match;
}

void testValidPack() {
    Fixture fixture;
    fixture.write(makeUdsp(), makeUdsp(7U));
    const auto validated = airfix::afpack::validatePack(fixture.packPath());
    require(validated.pack.entries().size() == 3U, "validated pack entry count mismatch");
    require(validated.manifest.entries.size() == 2U,
        "validated manifest entry count mismatch");
    require(validated.manifest.locale == airfix::afpack::ManifestLocale::en,
        "validated manifest locale mismatch");
}

void testDigestMutation() {
    Fixture fixture;
    fixture.write(makeUdsp(31U), makeUdsp());
    const auto& entry = findEntry(fixture.result().entries, "source/Resource.up");
    std::fstream file(fixture.packPath(), std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
    char byte{};
    file.read(&byte, 1);
    file.seekp(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x5AU);
    file.write(&byte, 1);
    file.flush();
    require(static_cast<bool>(file), "could not mutate validation payload");
    file.close();

    requireErrorContaining<airfix::afpack::ParseError>([&] {
        (void)airfix::afpack::validatePack(fixture.packPath());
    }, "SHA-256 mismatch: source/Resource.up");
}

void testMalformedNestedArchive() {
    Fixture fixture;
    fixture.write({0x00U, 0x01U, 0x02U, 0x03U}, makeUdsp());
    requireErrorContaining<airfix::udsp::ParseError>([&] {
        (void)airfix::afpack::validatePack(fixture.packPath());
    }, "UDSP header");
}

void testCancellationDuringHashing() {
    Fixture fixture;
    fixture.write(makeUdsp(1024U), makeUdsp(1024U));
    std::stop_source stop;
    bool cancelledFromCallback = false;
    auto limits = airfix::afpack::ValidationLimits{};
    limits.ioBufferBytes = 17U;
    requireErrorContaining<airfix::afpack::ValidationCancelled>([&] {
        (void)airfix::afpack::validatePack(
            fixture.packPath(), limits, stop.get_token(),
            [&](const airfix::afpack::ValidationProgress& progress) {
                if (progress.phase == airfix::afpack::ValidationPhase::hashingPayloads &&
                    progress.completedBytes > 0U) {
                    cancelledFromCallback = true;
                    (void)stop.request_stop();
                }
            });
    }, "AFPACK validation cancelled");
    require(cancelledFromCallback, "hash cancellation callback was not reached");
}

void testCancellationBeforeNestedArchives() {
    Fixture fixture;
    fixture.write({0xAAU, 0xBBU, 0xCCU}, {0x10U, 0x20U});
    std::stop_source stop;
    bool reachedNestedPhase = false;
    requireErrorContaining<airfix::afpack::ValidationCancelled>([&] {
        (void)airfix::afpack::validatePack(
            fixture.packPath(), {}, stop.get_token(),
            [&](const airfix::afpack::ValidationProgress& progress) {
                if (progress.phase ==
                    airfix::afpack::ValidationPhase::validatingNestedArchives) {
                    reachedNestedPhase = true;
                    (void)stop.request_stop();
                }
            });
    }, "AFPACK validation cancelled");
    require(reachedNestedPhase, "nested cancellation callback was not reached");
}

void testMutationDuringManifestPhase() {
    Fixture fixture;
    fixture.write(makeUdsp(257U), makeUdsp());
    const auto& source = findEntry(fixture.result().entries, "source/Resource.up");
    bool mutated = false;

    requireErrorContaining<airfix::afpack::ParseError>([&] {
        (void)airfix::afpack::validatePack(
            fixture.packPath(), {}, {},
            [&](const airfix::afpack::ValidationProgress& progress) {
                if (mutated || progress.phase !=
                    airfix::afpack::ValidationPhase::parsingManifest) {
                    return;
                }
                const auto mutationOffset = source.dataOffset + airfix::udsp::kHeaderSize;
                std::fstream file(
                    fixture.packPath(), std::ios::binary | std::ios::in | std::ios::out);
                file.seekg(static_cast<std::streamoff>(mutationOffset), std::ios::beg);
                char byte{};
                file.read(&byte, 1);
                file.seekp(static_cast<std::streamoff>(mutationOffset), std::ios::beg);
                byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0xA5U);
                file.write(&byte, 1);
                file.flush();
                require(static_cast<bool>(file),
                    "could not mutate staged payload during validation");
                mutated = true;
            });
    }, "payload changed during validation: source/Resource.up");
    require(mutated, "manifest-phase mutation callback was not reached");
}

void testCancellationDuringConfirmation() {
    Fixture fixture;
    fixture.write(makeUdsp(1024U), makeUdsp(1024U));
    std::stop_source stop;
    bool reachedConfirmation = false;
    auto limits = airfix::afpack::ValidationLimits{};
    limits.ioBufferBytes = 19U;
    requireErrorContaining<airfix::afpack::ValidationCancelled>([&] {
        (void)airfix::afpack::validatePack(
            fixture.packPath(), limits, stop.get_token(),
            [&](const airfix::afpack::ValidationProgress& progress) {
                if (progress.phase ==
                        airfix::afpack::ValidationPhase::confirmingPayloads &&
                    progress.completedBytes > 0U) {
                    reachedConfirmation = true;
                    (void)stop.request_stop();
                }
            });
    }, "AFPACK validation cancelled");
    require(reachedConfirmation, "confirmation cancellation callback was not reached");
}

void testManifestLimits() {
    Fixture fixture;
    fixture.write(makeUdsp(), makeUdsp());
    const auto& manifest = findEntry(fixture.result().entries, "manifest.json");
    require(manifest.storedSize > 1U, "synthetic manifest is unexpectedly small");

    auto limits = airfix::afpack::ValidationLimits{};
    limits.maxManifestBytes = manifest.storedSize - 1U;
    requireErrorContaining<airfix::afpack::ParseError>([&] {
        (void)airfix::afpack::validatePack(fixture.packPath(), limits);
    }, "entry exceeds requested read limit");

    limits = {};
    limits.manifest.maxInputBytes = 16U;
    requireErrorContaining<std::invalid_argument>([&] {
        (void)airfix::afpack::validatePack(fixture.packPath(), limits);
    }, "must not exceed the manifest parser input limit");

    limits = {};
    limits.ioBufferBytes = airfix::afpack::kMaximumValidationIoBufferBytes + 1U;
    requireErrorContaining<std::invalid_argument>([&] {
        (void)airfix::afpack::validatePack(fixture.packPath(), limits);
    }, "I/O buffer size is outside limits");
}

void testProgressOrderingAndBounds() {
    Fixture fixture;
    fixture.write(makeUdsp(73U), makeUdsp(41U));
    auto limits = airfix::afpack::ValidationLimits{};
    limits.ioBufferBytes = 13U;
    std::vector<airfix::afpack::ValidationProgress> events;
    (void)airfix::afpack::validatePack(
        fixture.packPath(), limits, {},
        [&](const auto& progress) { events.push_back(progress); });

    require(!events.empty(), "validation did not report progress");
    require(events.front().phase == airfix::afpack::ValidationPhase::openingPack,
        "first validation phase mismatch");
    require(events.back().phase == airfix::afpack::ValidationPhase::complete,
        "last validation phase mismatch");

    auto previousPhase = events.front().phase;
    std::uint64_t previousHashBytes = 0U;
    bool sawHashing = false;
    bool sawManifest = false;
    bool sawNested = false;
    bool sawConfirmation = false;
    for (const auto& event : events) {
        require(event.completedBytes <= event.totalBytes,
            "progress byte count exceeded its total");
        require(event.entriesCompleted <= event.entryCount,
            "progress entry count exceeded its total");
        require(static_cast<unsigned>(event.phase) >= static_cast<unsigned>(previousPhase),
            "validation phase order regressed");
        previousPhase = event.phase;
        if (event.phase == airfix::afpack::ValidationPhase::hashingPayloads) {
            require(event.completedBytes >= previousHashBytes,
                "hash progress byte count regressed");
            previousHashBytes = event.completedBytes;
            sawHashing = true;
        }
        sawManifest = sawManifest ||
            event.phase == airfix::afpack::ValidationPhase::parsingManifest;
        sawNested = sawNested ||
            event.phase == airfix::afpack::ValidationPhase::validatingNestedArchives;
        sawConfirmation = sawConfirmation ||
            event.phase == airfix::afpack::ValidationPhase::confirmingPayloads;
    }
    require(sawHashing && sawManifest && sawNested && sawConfirmation,
        "validation omitted a required progress phase");
}

} // namespace

int main() {
    try {
        testValidPack();
        testDigestMutation();
        testMalformedNestedArchive();
        testCancellationDuringHashing();
        testCancellationBeforeNestedArchives();
        testMutationDuringManifestPhase();
        testCancellationDuringConfirmation();
        testManifestLimits();
        testProgressOrderingAndBounds();
        std::cout << "all AFPACK validation tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK validation test failure: " << error.what() << '\n';
        return 1;
    }
}
