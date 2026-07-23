#include "airfix/content/VerifiedContentSession.hpp"
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

namespace {

using airfix::content::ContentRevision;
using airfix::content::VerifiedContentCancelled;
using airfix::content::VerifiedContentError;
using airfix::content::VerifiedContentLimits;
using airfix::content::VerifiedContentPhase;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

static_assert(!std::is_copy_constructible_v<VerifiedContentSession>);
static_assert(!std::is_copy_assignable_v<VerifiedContentSession>);
static_assert(std::is_nothrow_move_constructible_v<VerifiedContentSession>);
static_assert(std::is_nothrow_move_assignable_v<VerifiedContentSession>);

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
