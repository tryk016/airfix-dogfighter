#include "airfix/archive/UdspArchive.hpp"
#include "airfix/crypto/Sha256.hpp"
#include "airfix/io/DurableFile.hpp"
#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::string_view kTransaction1 = "00000000-0000-4000-8000-000000000001";
constexpr std::string_view kTransaction2 = "00000000-0000-4000-8000-000000000002";
constexpr std::string_view kTransaction3 = "00000000-0000-4000-8000-000000000003";
constexpr std::string_view kTransaction4 = "00000000-0000-4000-8000-000000000004";

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Error>
void requireError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const Error&) {
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

[[nodiscard]] Bytes makeUdsp(
    const std::size_t payloadBytes,
    const std::uint8_t seed) {
    constexpr std::string_view directory = "dir";
    constexpr std::string_view name = "entry.bin";
    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto dataOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t index = 0U; index < payloadBytes; ++index) {
        archive.push_back(static_cast<std::uint8_t>(seed + index * 37U));
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

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output || (!bytes.empty() && !output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))) {
        throw std::runtime_error("cannot write installer test fixture");
    }
}

void commitThenReportFailure(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& activePath,
    void*) {
    airfix::io::replaceFileDurable(preparedPath, activePath);
    throw std::runtime_error("synthetic post-rename failure");
}

void commitThenCorruptReadback(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& activePath,
    void*) {
    airfix::io::replaceFileDurable(preparedPath, activePath);
    writeBytes(activePath, {'B', 'A', 'D'});
    throw std::runtime_error("synthetic active readback failure");
}

struct RetryDurabilityState {
    bool called{};
    bool fail{};
};

void retryDurability(
    const std::filesystem::path& activePath,
    const std::filesystem::path& contentRoot,
    void* const opaqueState) {
    auto& state = *static_cast<RetryDurabilityState*>(opaqueState);
    state.called = true;
    if (state.fail) {
        throw std::runtime_error("synthetic retry durability failure");
    }
    airfix::io::syncFile(activePath);
    airfix::io::syncDirectory(contentRoot);
}

[[nodiscard]] Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read installer test fixture");
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

[[nodiscard]] airfix::crypto::Sha256Digest digestFile(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("cannot size installer test fixture");
    }
    return airfix::crypto::sha256FileRegion(path, 0U, size);
}

class Fixture final {
public:
    Fixture() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-installer-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(1U)));
        if (!std::filesystem::create_directory(root_)) {
            throw std::runtime_error("cannot create installer test directory");
        }
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    [[nodiscard]] std::filesystem::path path(const std::string_view name) const {
        return root_ / name;
    }

    [[nodiscard]] std::filesystem::path makePack(
        const std::string_view name,
        const std::uint8_t seed,
        const std::size_t sourcePayload = 257U) const {
        const auto sourcePath = path(std::string(name) + "-Resource.up");
        const auto localePath = path(std::string(name) + "-English.up");
        const auto packPath = path(std::string(name) + ".afpack");
        writeBytes(sourcePath, makeUdsp(sourcePayload, seed));
        writeBytes(localePath, makeUdsp(61U, static_cast<std::uint8_t>(seed + 11U)));
        (void)airfix::afpack::writePack({
            .outputPath = packPath,
            .manifest = {
                .sourceVersion = "1.01",
                .converterVersion = "installer-tests",
                .converterCommit = std::string(name),
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
                    .sourcePath = localePath,
                },
            },
        });
        return packPath;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] airfix::afpack::ActiveRecord readActive(
    const std::filesystem::path& contentRoot) {
    return airfix::afpack::parseActiveRecord(readBytes(contentRoot / "active.afac"));
}

void requireDirectoryEmpty(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return;
    }
    require(std::filesystem::directory_iterator(path) ==
            std::filesystem::directory_iterator(),
        "installer left a private staging file");
}

void testInstallRotateAndIdempotence() {
    Fixture fixture;
    const auto firstPack = fixture.makePack("first", 3U);
    const auto secondPack = fixture.makePack("second", 41U, 389U);
    const auto content = fixture.path("content");
    const auto save = fixture.path("save.dat");
    writeBytes(save, {0x53U, 0x41U, 0x56U, 0x45U});

    const auto first = airfix::afpack::installPack(
        firstPack, content, kTransaction1);
    require(first.activeChanged && !first.reusedExisting,
        "first install result flags are wrong");
    require(first.active.generation == 1U && !first.active.previous.has_value(),
        "first install did not create generation one");
    require(first.active.current.sha256 == first.digest &&
            first.active.current.size == first.size,
        "first active reference does not match installed pack");
    require(std::filesystem::is_regular_file(first.finalPath),
        "first content-addressed pack is absent");
    require(readActive(content) == first.active,
        "first active record was not durably rebound");

    const auto second = airfix::afpack::installPack(
        secondPack, content, kTransaction2);
    require(second.activeChanged && !second.reusedExisting,
        "second install result flags are wrong");
    require(second.active.generation == 2U &&
            second.active.previous ==
                std::optional<airfix::afpack::ActivePackReference>(first.active.current),
        "second install did not rotate current to previous");
    require(std::filesystem::is_regular_file(first.finalPath) &&
            std::filesystem::is_regular_file(second.finalPath),
        "pack rotation removed a content-addressed pack");
    require(readBytes(save) == Bytes({0x53U, 0x41U, 0x56U, 0x45U}),
        "installer touched a save outside contentRoot");

    const auto activeBeforeReimport = readBytes(content / "active.afac");
    const auto repeated = airfix::afpack::installPack(
        secondPack, content, kTransaction3);
    require(repeated.reusedExisting && !repeated.activeChanged,
        "reimport did not report idempotent reuse");
    require(repeated.active.generation == 2U && repeated.active == second.active,
        "reimport advanced or changed the active generation");
    require(readBytes(content / "active.afac") == activeBeforeReimport,
        "reimport rewrote the active record");
    requireDirectoryEmpty(content / "staging");
}

void testInvalidAndCancelledPreserveActive() {
    Fixture fixture;
    const auto firstPack = fixture.makePack("base", 5U);
    const auto secondPack = fixture.makePack("cancelled", 71U, 4096U);
    const auto invalidPack = fixture.path("invalid.afpack");
    writeBytes(invalidPack, {0x00U, 0x01U, 0x02U, 0x03U});
    const auto content = fixture.path("content");
    (void)airfix::afpack::installPack(firstPack, content, kTransaction1);
    const auto activeBefore = readBytes(content / "active.afac");

    requireError<airfix::afpack::ParseError>([&] {
        (void)airfix::afpack::installPack(invalidPack, content, kTransaction2);
    });
    require(readBytes(content / "active.afac") == activeBefore,
        "invalid pack changed the active record");
    requireDirectoryEmpty(content / "staging");

    std::stop_source stop;
    auto limits = airfix::afpack::InstallLimits{};
    limits.ioBufferBytes = 17U;
    bool requested = false;
    requireError<airfix::afpack::InstallCancelled>([&] {
        (void)airfix::afpack::installPack(
            secondPack,
            content,
            kTransaction3,
            limits,
            stop.get_token(),
            [&](const airfix::afpack::InstallProgress& progress) {
                if (!requested && progress.phase ==
                        airfix::afpack::InstallPhase::copyingSource &&
                    progress.completedBytes > 0U) {
                    requested = true;
                    (void)stop.request_stop();
                }
            });
    });
    require(requested, "installer cancellation callback was not reached");
    require(readBytes(content / "active.afac") == activeBefore,
        "cancelled install changed the active record");
    requireDirectoryEmpty(content / "staging");
    require(!std::filesystem::exists(
            content / ("active-" + std::string(kTransaction3) + ".afac.partial")),
        "cancelled install left its active temporary file");
}

void testMalformedActiveFailsWithoutChange() {
    Fixture fixture;
    const auto pack = fixture.makePack("malformed-active", 9U);
    const auto content = fixture.path("content");
    require(std::filesystem::create_directory(content),
        "cannot create malformed-active content root");
    const Bytes malformed{'B', 'A', 'D'};
    writeBytes(content / "active.afac", malformed);

    requireError<airfix::afpack::ActiveRecordError>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1);
    });
    require(readBytes(content / "active.afac") == malformed,
        "malformed active record was modified");
    const auto digest = digestFile(pack);
    require(std::filesystem::is_regular_file(
            content / "packs" / airfix::afpack::packFileName(digest)),
        "safe orphan pack was not retained after active-record failure");
    requireDirectoryEmpty(content / "staging");
    require(!std::filesystem::exists(
            content / ("active-" + std::string(kTransaction1) + ".afac.partial")),
        "malformed active record left an active temporary file");
}

void testArgumentsAndLimits() {
    Fixture fixture;
    const auto pack = fixture.makePack("arguments", 13U);
    const auto content = fixture.path("content");
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(pack, content, "../../escape");
    });
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(
            pack, content, "00000000-0000-4000-8000-00000000000A");
    });
    require(!std::filesystem::exists(content),
        "invalid transaction id created contentRoot");

    const auto wrongExtension = fixture.path("candidate.bin");
    writeBytes(wrongExtension, readBytes(pack));
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(wrongExtension, content, kTransaction1);
    });

    auto limits = airfix::afpack::InstallLimits{};
    limits.ioBufferBytes = 0U;
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1, limits);
    });
    limits = {};
    limits.ioBufferBytes = airfix::afpack::kMaximumInstallIoBufferBytes + 1U;
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1, limits);
    });
    limits = {};
    limits.maxPackBytes = std::filesystem::file_size(pack) - 1U;
    requireError<std::runtime_error>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1, limits);
    });
    limits = {};
    limits.maxPackBytes = limits.validation.afpack.maxArchiveSize + 1U;
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1, limits);
    });
    limits = {};
    limits.validation.ioBufferBytes =
        airfix::afpack::kMaximumValidationIoBufferBytes + 1U;
    requireError<std::invalid_argument>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1, limits);
    });

    const auto nestedContent = fixture.path("missing-parent") / "content";
    requireError<std::runtime_error>([&] {
        (void)airfix::afpack::installPack(pack, nestedContent, kTransaction1);
    });
    require(!std::filesystem::exists(fixture.path("missing-parent")),
        "installer recursively created an unspecified content-root parent");
}

void testCorruptExistingFinalIsRejected() {
    Fixture fixture;
    const auto pack = fixture.makePack("collision", 17U);
    const auto content = fixture.path("content");
    require(std::filesystem::create_directory(content),
        "cannot create collision content root");
    require(std::filesystem::create_directory(content / "staging"),
        "cannot create collision staging directory");
    require(std::filesystem::create_directory(content / "packs"),
        "cannot create collision packs directory");
    const auto digest = digestFile(pack);
    const auto finalPath = content / "packs" / airfix::afpack::packFileName(digest);
    const auto packSize = std::filesystem::file_size(pack);
    const Bytes corrupt(static_cast<std::size_t>(packSize), 0xA5U);
    writeBytes(finalPath, corrupt);

    requireError<std::runtime_error>([&] {
        (void)airfix::afpack::installPack(pack, content, kTransaction1);
    });
    require(readBytes(finalPath) == corrupt,
        "installer modified a corrupt digest-name collision");
    require(!std::filesystem::exists(content / "active.afac"),
        "digest-name collision created an active record");
    requireDirectoryEmpty(content / "staging");
}

void testProgressAndCommittedCallback() {
    Fixture fixture;
    const auto pack = fixture.makePack("progress", 23U, 1024U);
    const auto content = fixture.path("content");
    auto limits = airfix::afpack::InstallLimits{};
    limits.ioBufferBytes = 17U;
    limits.validation.ioBufferBytes = 19U;
    std::vector<airfix::afpack::InstallProgress> events;
    bool threwAtCommit = false;
    const auto result = airfix::afpack::installPack(
        pack,
        content,
        kTransaction1,
        limits,
        {},
        [&](const airfix::afpack::InstallProgress& progress) {
            events.push_back(progress);
            if (!threwAtCommit && progress.phase ==
                    airfix::afpack::InstallPhase::committingActiveRecord) {
                threwAtCommit = true;
                throw std::runtime_error("observer failure after commit boundary");
            }
        });
    require(result.activeChanged && threwAtCommit,
        "committed callback behavior was not exercised");
    require(readActive(content) == result.active,
        "commit callback failure invalidated activation");
    require(!events.empty() &&
            events.front().phase == airfix::afpack::InstallPhase::preparingDirectories &&
            events.back().phase == airfix::afpack::InstallPhase::complete,
        "installer progress endpoints are wrong");

    auto previousPhase = events.front().phase;
    std::uint64_t previousCopyBytes = 0U;
    bool sawCopy = false;
    bool sawHash = false;
    bool sawValidation = false;
    bool sawCommit = false;
    for (const auto& event : events) {
        require(event.completedBytes <= event.totalBytes &&
                event.entriesCompleted <= event.entryCount,
            "installer progress exceeded its bounds");
        require(static_cast<unsigned>(event.phase) >=
                static_cast<unsigned>(previousPhase),
            "installer progress phase order regressed");
        previousPhase = event.phase;
        if (event.phase == airfix::afpack::InstallPhase::copyingSource) {
            require(event.completedBytes >= previousCopyBytes,
                "installer copy progress regressed");
            previousCopyBytes = event.completedBytes;
            sawCopy = true;
        }
        sawHash = sawHash ||
            event.phase == airfix::afpack::InstallPhase::hashingStagedPack;
        if (event.phase == airfix::afpack::InstallPhase::validatingStagedPack) {
            require(event.validationPhase.has_value(),
                "validation progress omitted its nested phase");
            sawValidation = true;
        }
        sawCommit = sawCommit ||
            event.phase == airfix::afpack::InstallPhase::committingActiveRecord;
    }
    require(sawCopy && sawHash && sawValidation && sawCommit,
        "installer progress omitted a required phase");
}

void testCommitFailureRecovery() {
    Fixture fixture;
    const auto pack = fixture.makePack("commit-recovery", 29U);
    const auto committedContent = fixture.path("committed-content");
    RetryDurabilityState committedRetry;
    const airfix::afpack::testing::InstallHooks committedHooks {
        .commitActive = commitThenReportFailure,
        .retryDurability = retryDurability,
        .context = &committedRetry,
    };
    const auto committed = airfix::afpack::testing::installPackWithHooks(
        pack,
        committedContent,
        kTransaction1,
        committedHooks);
    require(committed.activeChanged && committed.active.generation == 1U &&
            committedRetry.called,
        "post-rename failure was not rebound as a committed install");
    require(readActive(committedContent) == committed.active,
        "post-rename recovery returned an unbound active result");
    require(!std::filesystem::exists(
            committedContent /
                ("active-" + std::string(kTransaction1) + ".afac.partial")),
        "post-rename recovery left an active temporary file");

    const auto unknownContent = fixture.path("unknown-content");
    const airfix::afpack::testing::InstallHooks unknownHooks {
        .commitActive = commitThenCorruptReadback,
        .retryDurability = nullptr,
        .context = nullptr,
    };
    bool sawUnknown = false;
    try {
        (void)airfix::afpack::testing::installPackWithHooks(
            pack,
            unknownContent,
            kTransaction2,
            unknownHooks);
    }
    catch (const airfix::afpack::InstallCommitUnknown& error) {
        sawUnknown = true;
        require(error.requestedActive().generation == 1U &&
                error.requestedActive().current.sha256 == digestFile(pack),
            "commit-unknown error omitted the requested active generation");
    }
    require(sawUnknown,
        "unreadable post-commit active record was not reported as commit-unknown");
    require(readBytes(unknownContent / "active.afac") == Bytes({'B', 'A', 'D'}),
        "commit-unknown test did not reach the readback failure state");

    const auto retryFailureContent = fixture.path("retry-failure-content");
    RetryDurabilityState failedRetry {
        .called = false,
        .fail = true,
    };
    const airfix::afpack::testing::InstallHooks retryFailureHooks {
        .commitActive = commitThenReportFailure,
        .retryDurability = retryDurability,
        .context = &failedRetry,
    };
    bool retryWasUnknown = false;
    try {
        (void)airfix::afpack::testing::installPackWithHooks(
            pack,
            retryFailureContent,
            kTransaction3,
            retryFailureHooks);
    }
    catch (const airfix::afpack::InstallCommitUnknown& error) {
        retryWasUnknown = true;
        require(error.requestedActive().generation == 1U,
            "retry-failure commit-unknown omitted its requested generation");
    }
    require(retryWasUnknown && failedRetry.called,
        "failed durability retry did not produce commit-unknown");
}

} // namespace

int main() {
    try {
        testInstallRotateAndIdempotence();
        testInvalidAndCancelledPreserveActive();
        testMalformedActiveFailsWithoutChange();
        testArgumentsAndLimits();
        testCorruptExistingFinalIsRejected();
        testProgressAndCommittedCallback();
        testCommitFailureRecovery();
        std::cout << "all AFPACK installer tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK installer test failure: " << error.what() << '\n';
        return 1;
    }
}
