#include "airfix/content/WorldRoomPublicationGate.hpp"

#include <limits>
#include <utility>

namespace airfix::content {

WorldRoomPublicationGate::WorldRoomPublicationGate(
    std::optional<ContentRevision> activeRevision,
    const std::uint64_t initialSerial) noexcept
    : activeRevision_(std::move(activeRevision)),
      serial_(initialSerial) {}

void WorldRoomPublicationGate::setActiveRevision(
    ContentRevision revision) noexcept {
    ticketOutstanding_ = false;
    activeRevision_ = std::move(revision);
}

void WorldRoomPublicationGate::clearActiveRevision() noexcept {
    ticketOutstanding_ = false;
    activeRevision_.reset();
}

void WorldRoomPublicationGate::invalidate() noexcept {
    ticketOutstanding_ = false;
}

std::optional<WorldRoomPublicationTicket>
WorldRoomPublicationGate::begin(
    const ContentRevision& expectedRevision) noexcept {
    if (!activeRevision_.has_value() ||
        *activeRevision_ != expectedRevision ||
        exhausted()) {
        return std::nullopt;
    }

    ++serial_;
    ticketOutstanding_ = true;
    return WorldRoomPublicationTicket{
        .serial = serial_,
        .expectedRevision = expectedRevision,
    };
}

bool WorldRoomPublicationGate::accepts(
    const WorldRoomPublicationTicket& ticket,
    const ContentRevision& resultRevision) const noexcept {
    return isCurrent(ticket) &&
        resultRevision == ticket.expectedRevision;
}

bool WorldRoomPublicationGate::isCurrent(
    const WorldRoomPublicationTicket& ticket) const noexcept {
    return ticketOutstanding_ &&
        activeRevision_.has_value() &&
        ticket.serial == serial_ &&
        ticket.expectedRevision == *activeRevision_;
}

bool WorldRoomPublicationGate::exhausted() const noexcept {
    return serial_ == std::numeric_limits<std::uint64_t>::max();
}

} // namespace airfix::content
