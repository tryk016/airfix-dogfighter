#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/package/AfPack.hpp"
#include "airfix/package/AfPackManifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>

namespace airfix::afpack {

inline constexpr std::size_t kDefaultValidationIoBufferBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumValidationIoBufferBytes = 1024U * 1024U;

struct ValidationLimits {
    ParseLimits afpack{};
    ManifestLimits manifest{};
    udsp::ParseLimits udsp{};
    std::uint64_t maxManifestBytes{1024U * 1024U};
    std::size_t ioBufferBytes{kDefaultValidationIoBufferBytes};
};

enum class ValidationPhase : std::uint8_t {
    openingPack,
    hashingPayloads,
    parsingManifest,
    validatingNestedArchives,
    confirmingPayloads,
    complete,
};

struct ValidationProgress {
    ValidationPhase phase{};
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::size_t entriesCompleted{};
    std::size_t entryCount{};
};

using ValidationProgressCallback = std::function<void(const ValidationProgress&)>;

class ValidationCancelled final : public std::runtime_error {
public:
    ValidationCancelled();
};

struct ValidatedPack {
    Pack pack;
    Manifest manifest;
};

// The installer must first copy an untrusted candidate to a private, uniquely
// named staging file that cannot be modified by an external writer. This gate
// validates that staged file only; it never installs or activates the pack.
[[nodiscard]] ValidatedPack validatePack(
    const std::filesystem::path& path,
    const ValidationLimits& limits = {},
    std::stop_token stopToken = {},
    ValidationProgressCallback progress = {});

} // namespace airfix::afpack
