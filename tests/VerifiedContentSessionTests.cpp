#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackRecovery.hpp"
#include "support/SyntheticContent.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace airfix::testing {

struct AuthenticatedStreamIdentityProbe final {
    [[nodiscard]] static const void* of(
        const afpack::ActiveContentLease& lease) noexcept {
        return lease.input_.get();
    }

    [[nodiscard]] static const void* of(
        const content::VerifiedContentSession& session) noexcept {
        return session.input_.get();
    }
};

} // namespace airfix::testing

namespace {

using airfix::content::ContentRevision;
using airfix::content::VerifiedContentCancelled;
using airfix::content::VerifiedContentError;
using airfix::content::VerifiedContentLimits;
using airfix::content::VerifiedContentPhase;
using airfix::content::VerifiedContentSession;
using airfix::content::VerifiedContentTransactionIdentity;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view kAdoptionTransaction =
    "00000000-0000-4000-8000-000000000101";

static_assert(!std::is_copy_constructible_v<VerifiedContentSession>);
static_assert(!std::is_copy_assignable_v<VerifiedContentSession>);
static_assert(std::is_nothrow_move_constructible_v<VerifiedContentSession>);
static_assert(std::is_nothrow_move_assignable_v<VerifiedContentSession>);
static_assert(!std::is_default_constructible_v<
    VerifiedContentTransactionIdentity>);
static_assert(std::is_copy_constructible_v<
    VerifiedContentTransactionIdentity>);
static_assert(std::is_copy_assignable_v<
    VerifiedContentTransactionIdentity>);

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

[[nodiscard]] SyntheticAfPack representativePack() {
    return airfix::testing::makeSyntheticAfPack({
        UdspInputEntry{
            .logicalPath = "Data/Probe.bin",
            .bytes = {0x10U, 0x20U, 0x30U, 0x40U},
        },
        UdspInputEntry{
            .logicalPath = "Game/Worlds/Test.world",
            .bytes = {'W', 'O', 'R', 'L', 'D'},
        },
    });
}

[[nodiscard]] ContentRevision revisionFor(
    const SyntheticAfPack& pack,
    const std::uint64_t generation = 17U) {
    return {
        .generation = generation,
        .pack = {
            .size = pack.size,
            .sha256 = pack.sha256,
        },
    };
}

class TestInputStore final {
public:
    ~TestInputStore() {
        for (const auto& path : paths_) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    TestInputStore(const TestInputStore&) = delete;
    TestInputStore& operator=(const TestInputStore&) = delete;

    [[nodiscard]] static TestInputStore& shared() {
        static TestInputStore store;
        return store;
    }

    [[nodiscard]] std::unique_ptr<std::ifstream> open(
        const SyntheticAfPack& pack) {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        const auto path = std::filesystem::temp_directory_path() /
            ("airfix-verified-input-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(
                    1U, std::memory_order_relaxed)) + ".afpack");
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output || (!pack.bytes.empty() && !output.write(
                    reinterpret_cast<const char*>(pack.bytes.data()),
                    static_cast<std::streamsize>(pack.bytes.size())))) {
                throw std::runtime_error(
                    "failed to create verified content test input");
            }
        }
        paths_.push_back(path);
        auto input = std::make_unique<std::ifstream>(
            path, std::ios::binary | std::ios::ate);
        if (!*input) {
            throw std::runtime_error(
                "failed to open verified content test input");
        }
        return input;
    }

private:
    TestInputStore() = default;

    std::vector<std::filesystem::path> paths_;
};

class RecoveryFixture final {
public:
    explicit RecoveryFixture(const SyntheticAfPack& pack) {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        root_ = std::filesystem::temp_directory_path() /
            ("airfix-adopted-content-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(
                    1U, std::memory_order_relaxed)));
        if (!std::filesystem::create_directory(root_)) {
            throw std::runtime_error(
                "failed to create adopted content test root");
        }
        inputPath_ = root_ / "input.afpack";
        contentRoot_ = root_ / "content";
        std::ofstream output(
            inputPath_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output || (!pack.bytes.empty() && !output.write(
                reinterpret_cast<const char*>(pack.bytes.data()),
                static_cast<std::streamsize>(pack.bytes.size())))) {
            throw std::runtime_error(
                "failed to create adopted content test input");
        }
    }

    ~RecoveryFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    RecoveryFixture(const RecoveryFixture&) = delete;
    RecoveryFixture& operator=(const RecoveryFixture&) = delete;

    [[nodiscard]] const std::filesystem::path& inputPath() const noexcept {
        return inputPath_;
    }

    [[nodiscard]] const std::filesystem::path& contentRoot() const noexcept {
        return contentRoot_;
    }

private:
    std::filesystem::path root_;
    std::filesystem::path inputPath_;
    std::filesystem::path contentRoot_;
};

[[nodiscard]] std::unique_ptr<std::ifstream> streamFor(
    const SyntheticAfPack& pack) {
    return TestInputStore::shared().open(pack);
}

[[nodiscard]] VerifiedContentSession open(
    const SyntheticAfPack& pack,
    const ContentRevision& revision,
    const VerifiedContentLimits& limits = {},
    const std::stop_token stopToken = {},
    airfix::content::VerifiedContentProgressCallback progress = {},
    std::string sourceLabel = "synthetic-active.afpack") {
    return VerifiedContentSession::open(
        streamFor(pack),
        std::move(sourceLabel),
        revision,
        limits,
        stopToken,
        std::move(progress));
}

void testValidSourceArchiveAndRevision() {
    const auto pack = representativePack();
    const auto trusted = revisionFor(pack, 42U);
    auto session = open(pack, trusted);

    require(session.revision() == trusted, "trusted revision was not retained");
    require(session.revision().generation == 42U, "generation changed");
    require(session.pack().archiveSize() == pack.size, "AFPACK size mismatch");
    require(
        session.manifest().gameId == "airfix-dogfighter",
        "strict manifest was not retained");
    require(
        session.sourcePackEntryIndex() < session.pack().entries().size(),
        "source AFPACK entry index is out of range");
    require(
        session.pack().entries()[session.sourcePackEntryIndex()].path ==
            "source/Resource.up",
        "wrong source AFPACK entry was selected");

    const auto lookup = session.sourceArchive().lookup("Data/Probe.bin");
    require(
        lookup.status == airfix::udsp::LookupStatus::unique,
        "valid nested source file was not found");
    require(
        session.readSourceFile(lookup.fileIndex, 4U) ==
            Bytes({0x10U, 0x20U, 0x30U, 0x40U}),
        "nested source file was not read from the authenticated stream");

    VerifiedContentSession moved = std::move(session);
    require(
        moved.revision() == trusted,
        "moving a verified session lost its revision");
}

void testAdoptsReadyRecoveryLease() {
    const auto pack = representativePack();
    RecoveryFixture fixture(pack);
    const auto installed = airfix::afpack::installPack(
        fixture.inputPath(),
        fixture.contentRoot(),
        kAdoptionTransaction);
    auto inspection = airfix::afpack::inspectActiveContent(
        fixture.contentRoot());

    require(
        inspection.status() == airfix::afpack::ActiveContentStatus::ready &&
            inspection.current().has_value(),
        "installed package did not produce a ready recovery lease");
    require(
        inspection.current()->activeGeneration() ==
                installed.active.generation &&
            inspection.current()->reference() == installed.active.current &&
            inspection.current()->pack().archiveSize() == pack.size &&
            inspection.current()->manifest().converterVersion ==
                "synthetic-test",
        "recovery lease omitted authenticated package metadata");
    const auto leaseLookup =
        inspection.current()->sourceArchive().lookup("Data/Probe.bin");
    require(
        leaseLookup.status == airfix::udsp::LookupStatus::unique &&
            inspection.current()->sourcePackEntryIndex() <
                inspection.current()->pack().entries().size() &&
            inspection.current()->pack()
                    .entries()[inspection.current()->sourcePackEntryIndex()]
                    .path == "source/Resource.up",
        "recovery lease omitted the authenticated nested source archive");

    auto lease = std::move(inspection).takeReadyLease();
    const void* const authenticatedStream =
        airfix::testing::AuthenticatedStreamIdentityProbe::of(lease);
    require(
        authenticatedStream != nullptr,
        "ready lease did not retain its authenticated stream");
    auto session = VerifiedContentSession::adopt(std::move(lease));
    require(
        airfix::testing::AuthenticatedStreamIdentityProbe::of(session) ==
            authenticatedStream,
        "adoption replaced rather than transferred the authenticated stream");
    requireError<VerifiedContentError>([&] {
        (void)VerifiedContentSession::adopt(std::move(lease));
    });
    const ContentRevision expectedRevision{
        .generation = installed.active.generation,
        .pack = installed.active.current,
    };

    require(
        session.revision() == expectedRevision,
        "adoption changed the active content revision");
    require(
        session.pack().archiveSize() == pack.size &&
            session.manifest().converterVersion == "synthetic-test" &&
            session.sourcePackEntryIndex() <
                session.pack().entries().size() &&
            session.pack().entries()[session.sourcePackEntryIndex()].path ==
                "source/Resource.up",
        "adoption lost AFPACK, manifest, or source entry metadata");
    const auto sessionLookup =
        session.sourceArchive().lookup("Data/Probe.bin");
    require(
        sessionLookup.status == airfix::udsp::LookupStatus::unique,
        "adoption lost the nested source archive");
    require(
        session.readSourceFile(sessionLookup.fileIndex, 4U) ==
            Bytes({0x10U, 0x20U, 0x30U, 0x40U}),
        "adopted session could not read from the authenticated source handle");
}

void testTransactionIdentitySurvivesMovesWithoutReplacementAba() {
    const auto pack = representativePack();
    const auto trusted = revisionFor(pack, 73U);

    auto original = open(pack, trusted);
    const auto originalIdentity = original.transactionIdentity();
    const auto copiedIdentity = originalIdentity;
    require(
        originalIdentity.valid() && copiedIdentity == originalIdentity,
        "transaction identity was not valid and copyable");

    auto exactSession = std::move(original);
    require(
        !original.transactionIdentity().valid(),
        "moved-from session retained a transaction identity");
    require(
        exactSession.transactionIdentity() == originalIdentity,
        "moving the exact session changed its transaction identity");

    auto replacement = open(pack, trusted);
    const auto replacementIdentity = replacement.transactionIdentity();
    require(
        replacementIdentity.valid() &&
            replacementIdentity != originalIdentity,
        "same-revision replacement reused the original transaction identity");

    original = std::move(replacement);
    require(
        original.transactionIdentity() == replacementIdentity &&
            original.transactionIdentity() != copiedIdentity,
        "move-assigned replacement produced an ABA transaction identity");

    auto displacedReplacement = std::move(original);
    original = std::move(exactSession);
    require(
        original.transactionIdentity() == originalIdentity,
        "moving the exact original session back lost its identity");
    require(
        displacedReplacement.transactionIdentity() == replacementIdentity &&
            displacedReplacement.transactionIdentity() != originalIdentity,
        "replacement identity collapsed into the restored original identity");

    exactSession = std::move(displacedReplacement);
    original = open(pack, trusted);
    require(
        original.transactionIdentity() != originalIdentity &&
            original.transactionIdentity() != replacementIdentity,
        "new same-revision session reused a still-live transaction identity");
}

void testWrongExpectedSizeFailsAtomically() {
    const auto pack = representativePack();
    auto trusted = revisionFor(pack);
    ++trusted.pack.size;

    std::optional<VerifiedContentSession> result;
    requireError<VerifiedContentError>([&] {
        result.emplace(open(pack, trusted));
    });
    require(!result.has_value(), "size mismatch published a partial session");
}

void testWrongExpectedDigestFailsAtomically() {
    const auto pack = representativePack();
    auto trusted = revisionFor(pack);
    trusted.pack.sha256.front() ^= 0x80U;

    std::optional<VerifiedContentSession> result;
    requireError<VerifiedContentError>([&] {
        result.emplace(open(pack, trusted));
    });
    require(!result.has_value(), "digest mismatch published a partial session");
}

void testInvalidGenerationFailsAtomically() {
    const auto pack = representativePack();
    auto trusted = revisionFor(pack);
    trusted.generation = 0U;

    std::optional<VerifiedContentSession> result;
    requireError<VerifiedContentError>([&] {
        result.emplace(open(pack, trusted));
    });
    require(
        !result.has_value(),
        "invalid generation published a partial session");
}

void testPreCancelledOpenFailsAtomically() {
    const auto pack = representativePack();
    std::stop_source cancellation;
    cancellation.request_stop();

    std::optional<VerifiedContentSession> result;
    requireError<VerifiedContentCancelled>([&] {
        result.emplace(open(
            pack,
            revisionFor(pack),
            {},
            cancellation.get_token()));
    });
    require(!result.has_value(), "pre-cancel published a partial session");
}

void testCancellationFromHashProgressFailsAtomically() {
    const auto pack = representativePack();
    std::stop_source cancellation;
    VerifiedContentLimits limits;
    limits.ioBufferBytes = 7U;
    std::size_t nonzeroHashReports = 0U;

    std::optional<VerifiedContentSession> result;
    requireError<VerifiedContentCancelled>([&] {
        result.emplace(open(
            pack,
            revisionFor(pack),
            limits,
            cancellation.get_token(),
            [&](const airfix::content::VerifiedContentProgress& progress) {
                if (progress.phase == VerifiedContentPhase::hashingPack &&
                    progress.completedBytes != 0U) {
                    ++nonzeroHashReports;
                    cancellation.request_stop();
                }
            }));
    });
    require(nonzeroHashReports == 1U, "hash cancellation callback was not reached");
    require(
        !result.has_value(),
        "hash-progress cancellation published a partial session");
}

class PoisonFile final {
public:
    PoisonFile() {
        static std::uint64_t sequence = 0U;
        const auto suffix = ++sequence;
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-poison-content-" + std::to_string(suffix) + ".afpack");
        const Bytes poison{'N', 'O', 'T', '-', 'A', '-', 'P', 'A', 'C', 'K'};
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(poison.data()),
            static_cast<std::streamsize>(poison.size()));
        if (!output) {
            throw std::runtime_error("failed to create poison content file");
        }
    }

    PoisonFile(const PoisonFile&) = delete;
    PoisonFile& operator=(const PoisonFile&) = delete;

    ~PoisonFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void testDiagnosticLabelIsNeverReopened() {
    const auto pack = representativePack();
    const PoisonFile poison;
    auto session = open(
        pack,
        revisionFor(pack),
        {},
        {},
        {},
        poison.path().string());

    require(
        session.sourceLabel() == poison.path().string(),
        "diagnostic source label was not retained");
    const auto lookup = session.sourceArchive().lookup("Data/Probe.bin");
    require(
        lookup.status == airfix::udsp::LookupStatus::unique,
        "poison label caused the implementation to reopen the wrong file");
    require(
        session.readSourceFile(lookup.fileIndex, 4U).size() == 4U,
        "authenticated stream was not preserved after open");
}

void testMalformedSourceArchiveFailsAtomically() {
    const auto pack = airfix::testing::makeSyntheticAfPackFromArchives(
        Bytes{'N', 'O', 'T', '-', 'U', 'D', 'S', 'P'});

    std::optional<VerifiedContentSession> result;
    requireError<airfix::udsp::ParseError>([&] {
        result.emplace(open(pack, revisionFor(pack)));
    });
    require(
        !result.has_value(),
        "malformed source archive published a partial session");
}

void testExactPackAndHashBufferLimits() {
    const auto pack = representativePack();
    const auto trusted = revisionFor(pack);

    {
        VerifiedContentLimits limits;
        limits.maxPackBytes = pack.size;
        limits.afpack.maxArchiveSize = pack.size;
        limits.ioBufferBytes = 1U;
        auto session = open(pack, trusted, limits);
        require(
            session.revision() == trusted,
            "exact pack limit or minimum hash buffer was rejected");
    }
    {
        VerifiedContentLimits limits;
        limits.maxPackBytes = pack.size;
        limits.afpack.maxArchiveSize = pack.size;
        limits.ioBufferBytes =
            airfix::content::kMaximumVerifiedContentIoBufferBytes;
        auto session = open(pack, trusted, limits);
        require(
            session.revision() == trusted,
            "maximum hash buffer boundary was rejected");
    }
    {
        VerifiedContentLimits limits;
        limits.maxPackBytes = pack.size - 1U;
        requireError<VerifiedContentError>([&] {
            (void)open(pack, trusted, limits);
        });
    }
    {
        VerifiedContentLimits limits;
        limits.ioBufferBytes = 0U;
        requireError<std::invalid_argument>([&] {
            (void)open(pack, trusted, limits);
        });
    }
    {
        VerifiedContentLimits limits;
        limits.ioBufferBytes =
            airfix::content::kMaximumVerifiedContentIoBufferBytes + 1U;
        requireError<std::invalid_argument>([&] {
            (void)open(pack, trusted, limits);
        });
    }
}

} // namespace

int main() {
    try {
        testValidSourceArchiveAndRevision();
        testAdoptsReadyRecoveryLease();
        testTransactionIdentitySurvivesMovesWithoutReplacementAba();
        testWrongExpectedSizeFailsAtomically();
        testWrongExpectedDigestFailsAtomically();
        testInvalidGenerationFailsAtomically();
        testPreCancelledOpenFailsAtomically();
        testCancellationFromHashProgressFailsAtomically();
        testDiagnosticLabelIsNeverReopened();
        testMalformedSourceArchiveFailsAtomically();
        testExactPackAndHashBufferLimits();
    }
    catch (const std::exception& error) {
        std::cerr << "Verified content session test failure: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "Verified content session tests passed\n";
    return 0;
}
