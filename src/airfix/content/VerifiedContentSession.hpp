#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/package/AfPack.hpp"
#include "airfix/package/AfPackActiveRecord.hpp"
#include "airfix/package/AfPackManifest.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace airfix::afpack {
class ActiveContentLease;
}

namespace airfix::testing {
struct AuthenticatedStreamIdentityProbe;
}

namespace airfix::content {

inline constexpr std::size_t kDefaultVerifiedContentIoBufferBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumVerifiedContentIoBufferBytes = 1024U * 1024U;

struct ContentRevision {
    std::uint64_t generation{};
    afpack::ActivePackReference pack;

    bool operator==(const ContentRevision&) const = default;
};

struct VerifiedContentLimits {
    afpack::ParseLimits afpack{};
    afpack::ManifestLimits manifest{};
    udsp::ParseLimits udsp{};
    std::uint64_t maxPackBytes{afpack::kDefaultMaxActivePackBytes};
    std::uint64_t maxManifestBytes{1024U * 1024U};
    std::size_t ioBufferBytes{kDefaultVerifiedContentIoBufferBytes};
};

enum class VerifiedContentPhase : std::uint8_t {
    inspectingStream,
    hashingPack,
    parsingPack,
    parsingManifest,
    openingSourceArchive,
    complete,
};

struct VerifiedContentProgress {
    VerifiedContentPhase phase{};
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
};

using VerifiedContentProgressCallback =
    std::function<void(const VerifiedContentProgress&)>;

class VerifiedContentError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class VerifiedContentCancelled final : public std::runtime_error {
public:
    VerifiedContentCancelled();
};

class VerifiedContentTransactionIdentity final {
public:
    VerifiedContentTransactionIdentity(
        const VerifiedContentTransactionIdentity&) noexcept = default;
    VerifiedContentTransactionIdentity& operator=(
        const VerifiedContentTransactionIdentity&) noexcept = default;
    VerifiedContentTransactionIdentity(
        VerifiedContentTransactionIdentity&&) noexcept = default;
    VerifiedContentTransactionIdentity& operator=(
        VerifiedContentTransactionIdentity&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return marker_ != nullptr;
    }

    friend bool operator==(
        const VerifiedContentTransactionIdentity&,
        const VerifiedContentTransactionIdentity&) = default;

private:
    friend class VerifiedContentSession;

    struct Marker final {};

    explicit VerifiedContentTransactionIdentity(
        std::shared_ptr<const Marker> marker) noexcept
        : marker_(std::move(marker)) {}

    std::shared_ptr<const Marker> marker_;
};

// Owns the one seekable stream that was authenticated against an active
// content revision. Every AFPACK and nested Resource.up operation remains
// bound to this handle; sourceLabel is diagnostic text and is never opened.
// The handle must come from app-private immutable content storage. Installation,
// activation and cleanup must serialize writers for its entire lifetime.
class VerifiedContentSession final {
public:
    VerifiedContentSession(const VerifiedContentSession&) = delete;
    VerifiedContentSession& operator=(const VerifiedContentSession&) = delete;
    VerifiedContentSession(VerifiedContentSession&&) noexcept = default;
    VerifiedContentSession& operator=(VerifiedContentSession&&) noexcept = default;
    ~VerifiedContentSession() = default;

    [[nodiscard]] static VerifiedContentSession open(
        std::unique_ptr<std::ifstream> input,
        std::string sourceLabel,
        ContentRevision trustedRevision,
        const VerifiedContentLimits& limits = {},
        std::stop_token stopToken = {},
        VerifiedContentProgressCallback progress = {});

    // Adopts the exact handle and metadata already authenticated by recovery.
    // This performs no reopen, rehash or metadata reparse.
    [[nodiscard]] static VerifiedContentSession adopt(
        afpack::ActiveContentLease&& lease);

    [[nodiscard]] const ContentRevision& revision() const noexcept {
        return revision_;
    }
    [[nodiscard]] const afpack::Pack& pack() const noexcept { return pack_; }
    [[nodiscard]] const afpack::Manifest& manifest() const noexcept {
        return manifest_;
    }
    [[nodiscard]] const udsp::Archive& sourceArchive() const noexcept {
        return sourceArchive_;
    }
    [[nodiscard]] std::size_t sourcePackEntryIndex() const noexcept {
        return sourcePackEntryIndex_;
    }
    [[nodiscard]] const std::string& sourceLabel() const noexcept {
        return sourceLabel_;
    }
    // Opaque identity of the exact authenticated stream handle. The token is
    // invalid for a moved-from session and changes when another session is
    // move-assigned, even if both sessions describe the same content revision.
    [[nodiscard]] VerifiedContentTransactionIdentity transactionIdentity()
        const noexcept {
        return VerifiedContentTransactionIdentity(transactionMarker_);
    }

    // These operations share one seekable handle and are intentionally
    // serialized by the caller; concurrent reads on a session are unsupported.
    [[nodiscard]] std::vector<std::uint8_t> readSourceFilePrefix(
        std::size_t fileIndex,
        std::size_t maximumBytes);
    [[nodiscard]] std::vector<std::uint8_t> readSourceFile(
        std::size_t fileIndex,
        std::size_t outputLimit);

private:
    friend struct airfix::testing::AuthenticatedStreamIdentityProbe;

    VerifiedContentSession(
        std::unique_ptr<std::ifstream> input,
        std::shared_ptr<
            const VerifiedContentTransactionIdentity::Marker> transactionMarker,
        std::string sourceLabel,
        ContentRevision revision,
        afpack::Pack pack,
        afpack::Manifest manifest,
        udsp::Archive sourceArchive,
        std::size_t sourcePackEntryIndex) noexcept;

    std::unique_ptr<std::ifstream> input_;
    std::shared_ptr<
        const VerifiedContentTransactionIdentity::Marker> transactionMarker_;
    std::string sourceLabel_;
    ContentRevision revision_;
    afpack::Pack pack_;
    afpack::Manifest manifest_;
    udsp::Archive sourceArchive_;
    std::size_t sourcePackEntryIndex_{};
};

} // namespace airfix::content
