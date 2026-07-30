#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/settings/RenderPresentationSettingsCodec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::settings {

enum class RenderSettingsFileStatus : std::uint8_t {
    notInspected,
    missing,
    valid,
    futureSchema,
    malformed,
    oversized,
    wrongTypeOrLinked,
    ioUnavailable,
};

struct RenderSettingsFileDiagnostic final {
    RenderSettingsFileStatus status{RenderSettingsFileStatus::notInspected};
    std::optional<std::uint32_t> schemaVersion;

    [[nodiscard]] friend constexpr bool
    operator==(const RenderSettingsFileDiagnostic&,
               const RenderSettingsFileDiagnostic&) noexcept = default;
};

enum class RenderSettingsLoadSource : std::uint8_t {
    defaults,
    current,
    backup,
};

struct RenderSettingsLoadResult final {
    render::RenderPresentationSettings settings;
    RenderSettingsLoadSource source{RenderSettingsLoadSource::defaults};
    RenderSettingsFileDiagnostic current;
    RenderSettingsFileDiagnostic backup;
    bool persistenceBlocked{};

    [[nodiscard]] constexpr bool recoveredFromBackup() const noexcept {
        return source == RenderSettingsLoadSource::backup;
    }
};

enum class RenderSettingsSaveStatus : std::uint8_t {
    unchanged,
    committed,
    committedAfterReadback,
};

struct RenderSettingsSaveResult final {
    RenderSettingsSaveStatus status{RenderSettingsSaveStatus::unchanged};
    bool backupRotated{};
};

enum class RenderSettingsStoreErrorKind : std::uint8_t {
    invalidDirectory,
    invalidSettings,
    persistenceBlocked,
    saveFailed,
    commitUnknown,
};

class RenderSettingsStoreError final : public std::runtime_error {
  public:
    RenderSettingsStoreError(
        RenderSettingsStoreErrorKind kind,
        std::optional<render::RenderPresentationSettingsRecord> requestedRecord,
        const char* message);

    [[nodiscard]] RenderSettingsStoreErrorKind kind() const noexcept { return kind_; }

    // A commit-unknown caller can perform a fresh load and compare semantic
    // state without receiving any host path or record bytes from the error.
    [[nodiscard]] const std::optional<render::RenderPresentationSettingsRecord>&
    requestedRecord() const noexcept {
        return requestedRecord_;
    }

  private:
    RenderSettingsStoreErrorKind kind_;
    std::optional<render::RenderPresentationSettingsRecord> requestedRecord_;
};

// settingsDirectory is an injected application-private leaf. Production
// adapters derive it from SDL's preference directory or iOS Application
// Support; tests use only a temporary parent. Missing settings are defaults.
// No diagnostic or exception from this API contains the resolved host path.
[[nodiscard]] RenderSettingsLoadResult
loadRenderPresentationSettings(const std::filesystem::path& settingsDirectory);

// The caller serializes access to one directory. A valid current document is
// rotated to backup before the new candidate is atomically published. A
// malformed current is never promoted. Future-schema documents are preserved
// byte-for-byte and block downgrade writes.
[[nodiscard]] RenderSettingsSaveResult
saveRenderPresentationSettings(const std::filesystem::path& settingsDirectory,
                               const render::RenderPresentationSettings& candidate);

namespace testing {

struct RenderSettingsStoreHooks final {
    using ReplaceCallback = void (*)(const std::filesystem::path& prepared,
                                     const std::filesystem::path& target, void* context);
    using RetryDurabilityCallback = void (*)(const std::filesystem::path& target,
                                             const std::filesystem::path& directory, void* context);

    ReplaceCallback replace{};
    RetryDurabilityCallback retryDurability{};
    void* context{};
};

[[nodiscard]] RenderSettingsSaveResult
saveRenderPresentationSettingsWithHooks(const std::filesystem::path& settingsDirectory,
                                        const render::RenderPresentationSettings& candidate,
                                        const RenderSettingsStoreHooks& hooks);

} // namespace testing

} // namespace airfix::settings
