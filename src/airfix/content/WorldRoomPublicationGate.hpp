#pragma once

#include "airfix/content/VerifiedContentSession.hpp"

#include <cstdint>
#include <optional>

namespace airfix::content {

struct WorldRoomPublicationTicket {
    std::uint64_t serial{};
    ContentRevision expectedRevision;

    bool operator==(const WorldRoomPublicationTicket&) const = default;
};

// Main-thread/serial-queue state gate for asynchronous room-load callbacks.
// The class performs no synchronization; its owner must serialize all calls.
class WorldRoomPublicationGate final {
public:
    explicit WorldRoomPublicationGate(
        std::optional<ContentRevision> activeRevision = std::nullopt,
        std::uint64_t initialSerial = 0U) noexcept;

    // Changing the active revision invalidates any outstanding ticket, even
    // when the new revision compares equal to the previous one.
    void setActiveRevision(ContentRevision revision) noexcept;
    void clearActiveRevision() noexcept;
    void invalidate() noexcept;

    // Starts a new request only for the currently active revision. A new
    // ticket invalidates the previous one. Once serial reaches UINT64_MAX,
    // no further ticket is issued rather than wrapping to zero.
    [[nodiscard]] std::optional<WorldRoomPublicationTicket> begin(
        const ContentRevision& expectedRevision) noexcept;

    // Use for progress and failure callbacks that carry no completed room.
    [[nodiscard]] bool isCurrent(
        const WorldRoomPublicationTicket& ticket) const noexcept;

    // A callback is current only when its ticket remains outstanding and all
    // three revision identities agree.
    [[nodiscard]] bool accepts(
        const WorldRoomPublicationTicket& ticket,
        const ContentRevision& resultRevision) const noexcept;

    [[nodiscard]] const std::optional<ContentRevision>& activeRevision()
        const noexcept {
        return activeRevision_;
    }
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }
    [[nodiscard]] bool hasOutstandingTicket() const noexcept {
        return ticketOutstanding_;
    }
    [[nodiscard]] bool exhausted() const noexcept;

private:
    std::optional<ContentRevision> activeRevision_;
    std::uint64_t serial_{};
    bool ticketOutstanding_{};
};

} // namespace airfix::content
