#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstdint>
#include <optional>
#include <thread>

namespace airfix::settings {

enum class RenderPresentationPersistencePhase : std::uint8_t {
    preparing,
    saving,
    committing,
};

struct RenderPresentationPersistenceTicket final {
    std::uint64_t serial{};
    std::uint64_t preparationAttempt{};
    render::RenderPresentationSettings persistentBase;
    render::RenderPresentationSettings effectiveSettings;

    [[nodiscard]] friend constexpr bool operator==(
        const RenderPresentationPersistenceTicket&,
        const RenderPresentationPersistenceTicket&) noexcept = default;
};

enum class RenderPresentationPersistenceAdvance : std::uint8_t {
    rejected,
    save,
    commit,
};

// Owner-thread state gate for the asynchronous platform sequence:
//
//   prepare resources -> optional durable save -> final validation -> commit
//
// Storage and resource work run elsewhere but may only report their immutable
// ticket back to the owner. A candidate that became durable before a stale
// surface result is prepared again without returning to the save phase.
class RenderPresentationPersistenceGate final {
  public:
    explicit RenderPresentationPersistenceGate(
        std::uint64_t initialSerial = 0U) noexcept;

    // Starts only while idle. alreadyDurable is true for a value loaded from
    // the settings store and false for a new UI request. Invalid settings,
    // concurrent work, a non-owner call, or serial exhaustion are rejected.
    [[nodiscard]] std::optional<RenderPresentationPersistenceTicket> begin(
        const render::RenderPresentationSettings& persistentBase,
        const render::RenderPresentationSettingsOverride& sessionOverrides,
        bool alreadyDurable) noexcept;

    [[nodiscard]] bool isCurrent(
        const RenderPresentationPersistenceTicket& ticket,
        RenderPresentationPersistencePhase phase) const noexcept;

    // A successful preparation advances either to save or directly to commit.
    [[nodiscard]] RenderPresentationPersistenceAdvance preparationSucceeded(
        const RenderPresentationPersistenceTicket& ticket) noexcept;

    // Records that the exact candidate is durable and ready for final
    // main-thread validation.
    [[nodiscard]] bool saveSucceeded(
        const RenderPresentationPersistenceTicket& ticket) noexcept;

    // A stale final surface/revision after durability returns a fresh attempt
    // ticket. It never returns to saving. At the practically unreachable
    // attempt-counter limit it fails closed while retaining the durable
    // publication obligation in the committing phase.
    [[nodiscard]] std::optional<RenderPresentationPersistenceTicket>
    retryAfterStaleCommit(
        const RenderPresentationPersistenceTicket& ticket) noexcept;

    // Finalizes exactly one successfully validated no-fail publication.
    [[nodiscard]] bool consumeCommitted(
        const RenderPresentationPersistenceTicket& ticket) noexcept;

    // Discards only an exact candidate that has not become durable. Once a
    // save succeeds, renderer publication remains an obligation until commit
    // or process restart; a stale callback cannot abandon it or newer work.
    [[nodiscard]] bool abandon(
        const RenderPresentationPersistenceTicket& ticket) noexcept;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool exhausted() const noexcept;
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }

  private:
    [[nodiscard]] bool ownerThread() const noexcept;
    void clear() noexcept;

    std::thread::id ownerThread_;
    std::optional<RenderPresentationPersistenceTicket> current_;
    RenderPresentationPersistencePhase phase_{
        RenderPresentationPersistencePhase::preparing};
    std::uint64_t serial_{};
    bool durable_{};
};

} // namespace airfix::settings
