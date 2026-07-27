#pragma once

#include "airfix/package/AfPack.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace airfix::afpack {

struct ManifestLimits {
    std::size_t maxInputBytes{1024U * 1024U};
    std::size_t maxStringBytes{16U * 1024U};
    std::size_t maxTotalStringBytes{512U * 1024U};
    std::size_t maxItems{8192U};
    std::size_t maxDepth{32U};
    std::size_t maxEntries{4096U};
};

class ManifestError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ManifestLocale {
    da,
    en,
    no,
    sv,
};

struct ManifestCapabilities {
    bool music{};
    bool multiplayer{};
    bool editors{};
};

struct ManifestEntry {
    std::string path;
    EntryKind kind{};
    std::uint64_t contentSize{};
    std::array<std::uint8_t, 32> sha256{};
};

struct Manifest {
    std::string schema;
    std::uint64_t version{};
    std::string gameId;
    std::string sourceVersion;
    std::string converterVersion;
    std::string converterCommit;
    ManifestLocale locale{};
    ManifestCapabilities capabilities;
    std::vector<ManifestEntry> entries;
};

// Parses the manifest payload, validates the strict v1 schema and initial
// source-archive allowlist, then requires a one-to-one match with the pack table.
[[nodiscard]] Manifest parseManifest(
    std::span<const std::uint8_t> bytes,
    std::span<const Entry> packEntries,
    const ManifestLimits& limits = {});

[[nodiscard]] std::string_view localeName(ManifestLocale locale) noexcept;

} // namespace airfix::afpack
