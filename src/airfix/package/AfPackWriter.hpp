#pragma once

#include "airfix/package/AfPack.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace airfix::afpack {

struct SourceEntry {
    std::string logicalPath;
    EntryKind kind{};
    std::filesystem::path sourcePath;
};

struct ManifestMetadata {
    std::string sourceVersion{"1.01"};
    std::string converterVersion{"0.1.0"};
    std::string converterCommit{"unknown"};
    std::string locale;
};

struct WriteRequest {
    std::filesystem::path outputPath;
    ManifestMetadata manifest;
    std::vector<SourceEntry> entries;
};

struct WriteResult {
    std::uint64_t archiveSize{};
    std::vector<Entry> entries;
};

[[nodiscard]] WriteResult writePack(const WriteRequest& request);

} // namespace airfix::afpack
