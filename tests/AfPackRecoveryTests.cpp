#include "airfix/archive/UdspArchive.hpp"
#include "airfix/crypto/Sha256.hpp"
#include "airfix/io/DurableFile.hpp"
#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackRecovery.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
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
        throw std::runtime_error("cannot write recovery test fixture");
    }
}

[[nodiscard]] Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read recovery test fixture");
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void corruptSameSize(const std::filesystem::path& path) {
    auto bytes = readBytes(path);
    require(!bytes.empty(), "cannot corrupt an empty recovery test fixture");
    bytes.front() ^= 0xFFU;
    writeBytes(path, bytes);
}

class Fixture final {
public:
    Fixture() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-recovery-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(1U)));
        if (!std::filesystem::create_directory(root_)) {
            throw std::runtime_error("cannot create recovery test directory");
        }
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

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
                .converterVersion = "recovery-tests",
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

struct Rotation {
    airfix::afpack::InstallResult first;
    airfix::afpack::InstallResult second;
};

[[nodiscard]] Rotation installRotation(
    const Fixture& fixture,
    const std::filesystem::path& content,
    const std::string_view prefix) {
    const auto firstPack = fixture.makePack(std::string(prefix) + "-first", 7U);
    const auto secondPack = fixture.makePack(
        std::string(prefix) + "-second", 43U, 577U);
    auto first = airfix::afpack::installPack(firstPack, content, kTransaction1);
    auto second = airfix::afpack::installPack(secondPack, content, kTransaction2);
    return {std::move(first), std::move(second)};
}

[[nodiscard]] airfix::afpack::ActiveRecord readActive(
    const std::filesystem::path& contentRoot) {
    return airfix::afpack::parseActiveRecord(readBytes(contentRoot / "active.afac"));
}

void writeActive(
    const std::filesystem::path& contentRoot,
    const airfix::afpack::ActiveRecord& active) {
    writeBytes(contentRoot / "active.afac",
        airfix::afpack::serializeActiveRecord(active));
}

void requireNoRollbackTemporary(
    const std::filesystem::path& content,
    const std::string_view transactionId) {
    require(!std::filesystem::exists(
            content / ("active-" + std::string(transactionId) + ".afac.partial")),
        "rollback left its active temporary file");
}

void testMissingReadyAndMalformedActive() {
    Fixture fixture;
    const auto emptyContent = fixture.path("empty-content");
    require(std::filesystem::create_directory(emptyContent),
        "cannot create empty content root");
    const auto missing = airfix::afpack::inspectActiveContent(emptyContent);
    require(missing.status() == airfix::afpack::ActiveContentStatus::noContent &&
            !missing.sourceActive().has_value() &&
            missing.contentRoot().is_absolute(),
        "missing active record was not distinguished as no content");

    const auto readyContent = fixture.path("ready-content");
    const auto pack = fixture.makePack("ready", 13U);
    const auto installed = airfix::afpack::installPack(
        pack, readyContent, kTransaction1);
    const auto ready = airfix::afpack::inspectActiveContent(readyContent);
    require(ready.status() == airfix::afpack::ActiveContentStatus::ready &&
            ready.current().has_value() &&
            ready.currentDiagnostic().status == airfix::afpack::CandidateStatus::valid,
        "valid active package was not reported ready");
    require(ready.current()->activeGeneration == installed.active.generation &&
            ready.current()->reference == installed.active.current &&
            ready.current()->path == installed.finalPath &&
            airfix::afpack::localeName(ready.current()->manifest.locale) == "en",
        "ready inspection omitted trusted package metadata");

    writeBytes(readyContent / "active.afac", {'B', 'A', 'D'});
    const auto malformed = airfix::afpack::inspectActiveContent(readyContent);
    require(malformed.status() ==
                airfix::afpack::ActiveContentStatus::malformedActive &&
            !malformed.activeDiagnostic().empty(),
        "malformed active record was confused with missing content");
}

void testFallbackStatesAndShortCircuit() {
    Fixture fixture;
    const auto fallbackContent = fixture.path("fallback-content");
    const auto fallbackRotation = installRotation(fixture, fallbackContent, "fallback");
    corruptSameSize(fallbackRotation.second.finalPath);
    const auto fallback = airfix::afpack::inspectActiveContent(fallbackContent);
    require(fallback.status() ==
                airfix::afpack::ActiveContentStatus::rollbackAvailable &&
            fallback.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::digestMismatch &&
            fallback.previousDiagnostic().status ==
                airfix::afpack::CandidateStatus::valid &&
            fallback.previous().has_value() &&
            fallback.previous()->activeGeneration ==
                fallbackRotation.second.active.generation &&
            fallback.previous()->reference == fallbackRotation.first.active.current,
        "valid previous package was not exposed as a verified rollback");

    const auto shortContent = fixture.path("short-circuit-content");
    const auto shortRotation = installRotation(fixture, shortContent, "short-circuit");
    corruptSameSize(shortRotation.first.finalPath);
    const auto shortCircuit = airfix::afpack::inspectActiveContent(shortContent);
    require(shortCircuit.status() == airfix::afpack::ActiveContentStatus::ready &&
            shortCircuit.current().has_value() &&
            shortCircuit.previousDiagnostic().status ==
                airfix::afpack::CandidateStatus::notInspected,
        "valid current package did not short-circuit an invalid previous package");

    const auto unusableContent = fixture.path("unusable-content");
    const auto unusableRotation = installRotation(fixture, unusableContent, "unusable");
    corruptSameSize(unusableRotation.first.finalPath);
    corruptSameSize(unusableRotation.second.finalPath);
    const auto unusable = airfix::afpack::inspectActiveContent(unusableContent);
    require(unusable.status() == airfix::afpack::ActiveContentStatus::unusable &&
            unusable.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::digestMismatch &&
            unusable.previousDiagnostic().status ==
                airfix::afpack::CandidateStatus::digestMismatch,
        "two invalid package generations were not reported unusable");
}

void testCandidatePathAndContentFailures() {
    Fixture fixture;

    const auto missingContent = fixture.path("candidate-missing");
    const auto missingPack = fixture.makePack("candidate-missing", 17U);
    const auto missingInstall = airfix::afpack::installPack(
        missingPack, missingContent, kTransaction1);
    require(std::filesystem::remove(missingInstall.finalPath),
        "cannot remove missing-candidate fixture");
    const auto missing = airfix::afpack::inspectActiveContent(missingContent);
    require(missing.status() == airfix::afpack::ActiveContentStatus::unusable &&
            missing.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::missing,
        "missing digest-derived package was not diagnosed");

    const auto sizeContent = fixture.path("candidate-size");
    const auto sizePack = fixture.makePack("candidate-size", 19U);
    const auto sizeInstall = airfix::afpack::installPack(
        sizePack, sizeContent, kTransaction1);
    std::filesystem::resize_file(sizeInstall.finalPath, sizeInstall.size - 1U);
    const auto wrongSize = airfix::afpack::inspectActiveContent(sizeContent);
    require(wrongSize.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::sizeMismatch,
        "wrong candidate size was not rejected before hashing");

    const auto digestContent = fixture.path("candidate-digest");
    const auto digestPack = fixture.makePack("candidate-digest", 23U);
    const auto digestInstall = airfix::afpack::installPack(
        digestPack, digestContent, kTransaction1);
    corruptSameSize(digestInstall.finalPath);
    const auto wrongDigest = airfix::afpack::inspectActiveContent(digestContent);
    require(wrongDigest.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::digestMismatch,
        "wrong candidate digest was not rejected");

    const auto typeContent = fixture.path("candidate-type");
    const auto typePack = fixture.makePack("candidate-type", 29U);
    const auto typeInstall = airfix::afpack::installPack(
        typePack, typeContent, kTransaction1);
    require(std::filesystem::remove(typeInstall.finalPath) &&
            std::filesystem::create_directory(typeInstall.finalPath),
        "cannot create wrong-type candidate fixture");
    const auto wrongType = airfix::afpack::inspectActiveContent(typeContent);
    require(wrongType.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::wrongType,
        "directory at digest-derived path was not rejected");

    const auto linkContent = fixture.path("candidate-symlink");
    const auto linkPack = fixture.makePack("candidate-symlink", 31U);
    const auto linkInstall = airfix::afpack::installPack(
        linkPack, linkContent, kTransaction1);
    const auto linkTarget = fixture.path("candidate-symlink-target.afpack");
    std::filesystem::rename(linkInstall.finalPath, linkTarget);
    std::error_code linkError;
    std::filesystem::create_symlink(linkTarget, linkInstall.finalPath, linkError);
    if (!linkError) {
        const auto symlink = airfix::afpack::inspectActiveContent(linkContent);
        require(symlink.currentDiagnostic().status ==
                    airfix::afpack::CandidateStatus::wrongType,
            "symlink at digest-derived path was followed");
    }

    const auto invalidContent = fixture.path("candidate-invalid");
    require(std::filesystem::create_directories(invalidContent / "packs"),
        "cannot create invalid candidate root");
    const Bytes invalidBytes(airfix::afpack::kHeaderSize, 0xA5U);
    const auto invalidDigest = airfix::crypto::sha256(invalidBytes);
    const airfix::afpack::ActiveRecord invalidActive{
        .generation = 1U,
        .current = {
            .size = invalidBytes.size(),
            .sha256 = invalidDigest,
        },
        .previous = std::nullopt,
    };
    writeBytes(
        invalidContent / "packs" / airfix::afpack::packFileName(invalidDigest),
        invalidBytes);
    writeActive(invalidContent, invalidActive);
    const auto invalid = airfix::afpack::inspectActiveContent(invalidContent);
    require(invalid.status() == airfix::afpack::ActiveContentStatus::unusable &&
            invalid.currentDiagnostic().status ==
                airfix::afpack::CandidateStatus::invalidPack,
        "digest-valid malformed package was not rejected semantically");
}

void testInspectionCancellationAndProgress() {
    Fixture fixture;
    const auto content = fixture.path("progress-content");
    const auto pack = fixture.makePack("progress", 37U, 4096U);
    (void)airfix::afpack::installPack(pack, content, kTransaction1);
    auto limits = airfix::afpack::RecoveryLimits{};
    limits.ioBufferBytes = 17U;

    std::stop_source stop;
    bool requested = false;
    requireError<airfix::afpack::RecoveryCancelled>([&] {
        (void)airfix::afpack::inspectActiveContent(
            content,
            limits,
            stop.get_token(),
            [&](const airfix::afpack::RecoveryProgress& progress) {
                if (!requested && progress.phase ==
                        airfix::afpack::RecoveryPhase::hashingCurrent &&
                    progress.completedBytes > 0U) {
                    requested = true;
                    (void)stop.request_stop();
                }
            });
    });
    require(requested, "recovery inspection cancellation callback was not reached");

    std::vector<airfix::afpack::RecoveryProgress> events;
    const auto ready = airfix::afpack::inspectActiveContent(
        content,
        limits,
        {},
        [&](const airfix::afpack::RecoveryProgress& progress) {
            events.push_back(progress);
        });
    require(ready.status() == airfix::afpack::ActiveContentStatus::ready &&
            !events.empty() &&
            events.front().phase ==
                airfix::afpack::RecoveryPhase::readingActiveRecord &&
            events.back().phase == airfix::afpack::RecoveryPhase::complete,
        "recovery inspection progress endpoints are wrong");
    auto previousPhase = events.front().phase;
    std::uint64_t previousHashBytes = 0U;
    bool sawHash = false;
    bool sawValidation = false;
    for (const auto& event : events) {
        require(event.completedBytes <= event.totalBytes,
            "recovery progress exceeded its byte bound");
        require(static_cast<unsigned>(event.phase) >=
                static_cast<unsigned>(previousPhase),
            "recovery inspection progress phase regressed");
        previousPhase = event.phase;
        if (event.phase == airfix::afpack::RecoveryPhase::hashingCurrent) {
            require(event.candidate == airfix::afpack::CandidateRole::current &&
                    event.completedBytes >= previousHashBytes,
                "current hash progress candidate or byte order is wrong");
            previousHashBytes = event.completedBytes;
            sawHash = true;
        }
        if (event.phase == airfix::afpack::RecoveryPhase::validatingCurrent) {
            require(event.candidate == airfix::afpack::CandidateRole::current,
                "current validation progress omitted its candidate");
            sawValidation = true;
        }
    }
    require(sawHash && sawValidation,
        "recovery inspection progress omitted hash or validation");

    bool observerEscaped = false;
    try {
        (void)airfix::afpack::inspectActiveContent(
            content,
            limits,
            {},
            [](const airfix::afpack::RecoveryProgress& progress) {
                if (progress.phase == airfix::afpack::RecoveryPhase::hashingCurrent &&
                    progress.completedBytes > 0U) {
                    throw std::runtime_error("synthetic recovery observer failure");
                }
            });
    }
    catch (const std::runtime_error& error) {
        observerEscaped =
            std::string_view(error.what()) == "synthetic recovery observer failure";
    }
    require(observerEscaped,
        "pre-commit recovery observer failure was swallowed as invalid content");

    bool typedObserverEscaped = false;
    try {
        (void)airfix::afpack::inspectActiveContent(
            content,
            limits,
            {},
            [](const airfix::afpack::RecoveryProgress& progress) {
                if (progress.phase == airfix::afpack::RecoveryPhase::hashingCurrent &&
                    progress.completedBytes > 0U) {
                    throw airfix::afpack::ParseError(
                        "synthetic typed recovery observer failure");
                }
            });
    }
    catch (const airfix::afpack::ParseError& error) {
        typedObserverEscaped = std::string_view(error.what()) ==
            "synthetic typed recovery observer failure";
    }
    require(typedObserverEscaped,
        "typed pre-commit observer failure was swallowed as invalid content");
}

void testRollbackSuccessAndCancellation() {
    Fixture fixture;
    const auto content = fixture.path("rollback-content");
    const auto rotation = installRotation(fixture, content, "rollback");
    corruptSameSize(rotation.second.finalPath);
    const auto inspection = airfix::afpack::inspectActiveContent(content);
    require(inspection.status() ==
            airfix::afpack::ActiveContentStatus::rollbackAvailable,
        "rollback fixture was not recoverable");

    std::vector<airfix::afpack::RecoveryProgress> commitEvents;
    const auto result = airfix::afpack::commitRollback(
        inspection,
        kTransaction3,
        {},
        {},
        [&](const airfix::afpack::RecoveryProgress& progress) {
            commitEvents.push_back(progress);
        });
    require(result.active.generation == 3U &&
            result.active.current == rotation.first.active.current &&
            result.active.previous ==
                std::optional<airfix::afpack::ActivePackReference>(
                    rotation.second.active.current) &&
            result.currentPackPath == rotation.first.finalPath &&
            readActive(content) == result.active,
        "rollback did not rotate references and generation exactly");
    requireNoRollbackTemporary(content, kTransaction3);
    require(!commitEvents.empty() &&
            commitEvents.front().phase ==
                airfix::afpack::RecoveryPhase::verifyingRollbackPack &&
            commitEvents.back().phase == airfix::afpack::RecoveryPhase::complete,
        "rollback commit progress endpoints are wrong");
    auto previousCommitPhase = commitEvents.front().phase;
    bool sawInitialStaleCheck = false;
    bool sawWrite = false;
    bool sawFinalStaleCheck = false;
    bool sawCommit = false;
    for (const auto& event : commitEvents) {
        require(event.completedBytes <= event.totalBytes &&
                static_cast<unsigned>(event.phase) >=
                    static_cast<unsigned>(previousCommitPhase),
            "rollback commit progress phase or byte bounds regressed");
        previousCommitPhase = event.phase;
        sawInitialStaleCheck = sawInitialStaleCheck ||
            event.phase == airfix::afpack::RecoveryPhase::checkingStaleActive;
        sawWrite = sawWrite ||
            event.phase == airfix::afpack::RecoveryPhase::writingRollbackRecord;
        sawFinalStaleCheck = sawFinalStaleCheck ||
            event.phase == airfix::afpack::RecoveryPhase::checkingFinalStaleActive;
        sawCommit = sawCommit ||
            event.phase == airfix::afpack::RecoveryPhase::committingRollbackRecord;
    }
    require(sawInitialStaleCheck && sawWrite && sawFinalStaleCheck && sawCommit,
        "rollback commit progress omitted a transaction phase");

    const auto after = airfix::afpack::inspectActiveContent(content);
    require(after.status() == airfix::afpack::ActiveContentStatus::ready &&
            after.current()->activeGeneration == 3U &&
            after.current()->reference == rotation.first.active.current &&
            after.previousDiagnostic().status ==
                airfix::afpack::CandidateStatus::notInspected,
        "committed rollback did not become ready or rechecked corrupt previous");

    const auto cancelledContent = fixture.path("rollback-cancelled-content");
    const auto cancelledRotation = installRotation(
        fixture, cancelledContent, "rollback-cancelled");
    corruptSameSize(cancelledRotation.second.finalPath);
    const auto cancelledInspection =
        airfix::afpack::inspectActiveContent(cancelledContent);
    const auto activeBefore = readBytes(cancelledContent / "active.afac");
    auto limits = airfix::afpack::RecoveryLimits{};
    limits.ioBufferBytes = 17U;
    std::stop_source stop;
    bool requested = false;
    requireError<airfix::afpack::RecoveryCancelled>([&] {
        (void)airfix::afpack::commitRollback(
            cancelledInspection,
            kTransaction4,
            limits,
            stop.get_token(),
            [&](const airfix::afpack::RecoveryProgress& progress) {
                if (!requested && progress.phase ==
                        airfix::afpack::RecoveryPhase::verifyingRollbackPack &&
                    progress.completedBytes > 0U) {
                    requested = true;
                    (void)stop.request_stop();
                }
            });
    });
    require(requested &&
            readBytes(cancelledContent / "active.afac") == activeBefore,
        "cancelled rollback changed the active record");
    requireNoRollbackTemporary(cancelledContent, kTransaction4);
}

void testRollbackRejectsStaleInspectionAndPack() {
    Fixture fixture;

    const auto activeContent = fixture.path("stale-active-content");
    const auto activeRotation = installRotation(fixture, activeContent, "stale-active");
    corruptSameSize(activeRotation.second.finalPath);
    const auto activeInspection = airfix::afpack::inspectActiveContent(activeContent);
    const auto thirdPack = fixture.makePack("stale-active-third", 83U);
    const auto third = airfix::afpack::installPack(
        thirdPack, activeContent, kTransaction3);
    requireError<airfix::afpack::StaleActiveContent>([&] {
        (void)airfix::afpack::commitRollback(
            activeInspection, kTransaction4);
    });
    require(readActive(activeContent) == third.active,
        "stale rollback overwrote a newer active generation");

    const auto packContent = fixture.path("stale-pack-content");
    const auto packRotation = installRotation(fixture, packContent, "stale-pack");
    corruptSameSize(packRotation.second.finalPath);
    const auto packInspection = airfix::afpack::inspectActiveContent(packContent);
    const auto packActiveBefore = readBytes(packContent / "active.afac");
    corruptSameSize(packRotation.first.finalPath);
    requireError<airfix::afpack::StaleActiveContent>([&] {
        (void)airfix::afpack::commitRollback(
            packInspection, kTransaction3);
    });
    require(readBytes(packContent / "active.afac") == packActiveBefore,
        "rollback committed a previous pack changed after inspection");

    const auto raceContent = fixture.path("stale-race-content");
    const auto raceRotation = installRotation(fixture, raceContent, "stale-race");
    corruptSameSize(raceRotation.second.finalPath);
    const auto raceInspection = airfix::afpack::inspectActiveContent(raceContent);
    auto racedActive = *raceInspection.sourceActive();
    ++racedActive.generation;
    std::size_t staleChecks = 0U;
    bool changedBeforeCommit = false;
    requireError<airfix::afpack::StaleActiveContent>([&] {
        (void)airfix::afpack::commitRollback(
            raceInspection,
            kTransaction4,
            {},
            {},
            [&](const airfix::afpack::RecoveryProgress& progress) {
                if (progress.phase == airfix::afpack::RecoveryPhase::
                        checkingFinalStaleActive &&
                    ++staleChecks == 1U) {
                    writeActive(raceContent, racedActive);
                    changedBeforeCommit = true;
                }
            });
    });
    require(changedBeforeCommit && readActive(raceContent) == racedActive,
        "second stale-active check did not reject a pre-commit replacement");
    requireNoRollbackTemporary(raceContent, kTransaction4);
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

[[nodiscard]] airfix::afpack::ActiveContentInspection makeFallbackInspection(
    const Fixture& fixture,
    const std::filesystem::path& content,
    const std::string_view prefix) {
    const auto rotation = installRotation(fixture, content, prefix);
    corruptSameSize(rotation.second.finalPath);
    return airfix::afpack::inspectActiveContent(content);
}

void testRollbackCommitBoundaryRecovery() {
    Fixture fixture;

    const auto committedContent = fixture.path("committed-content");
    const auto committedInspection = makeFallbackInspection(
        fixture, committedContent, "committed");
    RetryDurabilityState committedRetry;
    const airfix::afpack::testing::InstallHooks committedHooks{
        .commitActive = commitThenReportFailure,
        .retryDurability = retryDurability,
        .context = &committedRetry,
    };
    const auto committed = airfix::afpack::testing::commitRollbackWithHooks(
        committedInspection,
        kTransaction3,
        committedHooks);
    require(committedRetry.called && committed.active.generation == 3U &&
            readActive(committedContent) == committed.active,
        "post-rename rollback failure was not rebound and resynchronized");
    requireNoRollbackTemporary(committedContent, kTransaction3);

    const auto failedRetryContent = fixture.path("failed-retry-content");
    const auto failedRetryInspection = makeFallbackInspection(
        fixture, failedRetryContent, "failed-retry");
    RetryDurabilityState failedRetry{
        .called = false,
        .fail = true,
    };
    const airfix::afpack::testing::InstallHooks failedRetryHooks{
        .commitActive = commitThenReportFailure,
        .retryDurability = retryDurability,
        .context = &failedRetry,
    };
    bool retryUnknown = false;
    try {
        (void)airfix::afpack::testing::commitRollbackWithHooks(
            failedRetryInspection,
            kTransaction3,
            failedRetryHooks);
    }
    catch (const airfix::afpack::InstallCommitUnknown& error) {
        retryUnknown = error.requestedActive().generation == 3U;
    }
    require(retryUnknown && failedRetry.called,
        "failed rollback durability retry was not reported commit-unknown");

    const auto corruptContent = fixture.path("corrupt-readback-content");
    const auto corruptInspection = makeFallbackInspection(
        fixture, corruptContent, "corrupt-readback");
    const airfix::afpack::testing::InstallHooks corruptHooks{
        .commitActive = commitThenCorruptReadback,
        .retryDurability = nullptr,
        .context = nullptr,
    };
    bool corruptUnknown = false;
    try {
        (void)airfix::afpack::testing::commitRollbackWithHooks(
            corruptInspection,
            kTransaction3,
            corruptHooks);
    }
    catch (const airfix::afpack::InstallCommitUnknown& error) {
        corruptUnknown = error.requestedActive().generation == 3U;
    }
    require(corruptUnknown &&
            readBytes(corruptContent / "active.afac") == Bytes({'B', 'A', 'D'}),
        "unreadable post-rollback active record was not commit-unknown");
}

} // namespace

int main() {
    try {
        testMissingReadyAndMalformedActive();
        testFallbackStatesAndShortCircuit();
        testCandidatePathAndContentFailures();
        testInspectionCancellationAndProgress();
        testRollbackSuccessAndCancellation();
        testRollbackRejectsStaleInspectionAndPack();
        testRollbackCommitBoundaryRecovery();
        std::cout << "all AFPACK recovery tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK recovery test failure: " << error.what() << '\n';
        return 1;
    }
}
