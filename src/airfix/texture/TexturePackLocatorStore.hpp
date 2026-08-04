#pragma once

#include "airfix/texture/TexturePackLocator.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::texture {

enum class TexturePackLocatorFileStatus : std::uint8_t {
    notInspected,
    missing,
    valid,
    futureSchema,
    malformed,
    oversized,
    wrongTypeOrLinked,
    ioUnavailable,
};

struct TexturePackLocatorFileDiagnostic final {
    TexturePackLocatorFileStatus status{
        TexturePackLocatorFileStatus::notInspected};
    std::optional<std::uint16_t> schemaVersion;
};

enum class TexturePackLocatorLoadSource : std::uint8_t {
    none,
    current,
    backup,
};

struct TexturePackLocatorLoadResult final {
    std::optional<TexturePackLocatorRecord> locator;
    TexturePackLocatorLoadSource source{TexturePackLocatorLoadSource::none};
    TexturePackLocatorFileDiagnostic current;
    TexturePackLocatorFileDiagnostic backup;
    bool persistenceBlocked{};

    [[nodiscard]] bool ready() const noexcept {
        return locator.has_value();
    }
};

enum class TexturePackLocatorSaveStatus : std::uint8_t {
    unchanged,
    committed,
    committedAfterReadback,
};

struct TexturePackLocatorSaveResult final {
    TexturePackLocatorSaveStatus status{
        TexturePackLocatorSaveStatus::unchanged};
    bool backupRotated{};
};

enum class TexturePackLocatorStoreErrorKind : std::uint8_t {
    invalidDirectory,
    invalidLocator,
    persistenceBlocked,
    saveFailed,
    commitUnknown,
};

class TexturePackLocatorStoreError final : public std::runtime_error {
  public:
    TexturePackLocatorStoreError(
        TexturePackLocatorStoreErrorKind kind,
        std::optional<TexturePackLocatorRecord> requestedLocator,
        const char* message);

    [[nodiscard]] TexturePackLocatorStoreErrorKind kind() const noexcept {
        return kind_;
    }

    [[nodiscard]] const std::optional<TexturePackLocatorRecord>&
    requestedLocator() const noexcept {
        return requestedLocator_;
    }

  private:
    TexturePackLocatorStoreErrorKind kind_;
    std::optional<TexturePackLocatorRecord> requestedLocator_;
};

// The injected directory must be an application-private leaf with one
// serialized owner. Diagnostics and exceptions are fixed and path-free.
[[nodiscard]] TexturePackLocatorLoadResult loadTexturePackLocator(
    const std::filesystem::path& directory);

// Valid current state is rotated to backup before atomic publication. A
// malformed current is replaced without promotion; future-schema and unsafe
// entries are retained and block downgrade writes.
[[nodiscard]] TexturePackLocatorSaveResult saveTexturePackLocator(
    const std::filesystem::path& directory,
    const TexturePackLocatorRecord& candidate);

} // namespace airfix::texture
