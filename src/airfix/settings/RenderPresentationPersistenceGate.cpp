#include "airfix/settings/RenderPresentationPersistenceGate.hpp"

#include <limits>

namespace airfix::settings {

RenderPresentationPersistenceGate::RenderPresentationPersistenceGate(
    const std::uint64_t initialSerial) noexcept
    : ownerThread_(std::this_thread::get_id()),
      serial_(initialSerial) {}

std::optional<RenderPresentationPersistenceTicket>
RenderPresentationPersistenceGate::begin(
    const render::RenderPresentationSettings& persistentBase,
    const render::RenderPresentationSettingsOverride& sessionOverrides,
    const bool alreadyDurable) noexcept {
    const auto resolved = render::resolveRenderPresentationSettings(
        persistentBase, sessionOverrides);
    if (!ownerThread() || current_.has_value() || exhausted() ||
        !resolved.accepted()) {
        return std::nullopt;
    }

    ++serial_;
    current_ = RenderPresentationPersistenceTicket{
        .serial = serial_,
        .preparationAttempt = 0U,
        .persistentBase = persistentBase,
        .effectiveSettings = resolved.settings,
    };
    phase_ = RenderPresentationPersistencePhase::preparing;
    durable_ = alreadyDurable;
    return current_;
}

bool RenderPresentationPersistenceGate::isCurrent(
    const RenderPresentationPersistenceTicket& ticket,
    const RenderPresentationPersistencePhase phase) const noexcept {
    return ownerThread() && current_.has_value() &&
        *current_ == ticket && phase_ == phase;
}

RenderPresentationPersistenceAdvance
RenderPresentationPersistenceGate::preparationSucceeded(
    const RenderPresentationPersistenceTicket& ticket) noexcept {
    if (!isCurrent(
            ticket, RenderPresentationPersistencePhase::preparing)) {
        return RenderPresentationPersistenceAdvance::rejected;
    }
    if (durable_) {
        phase_ = RenderPresentationPersistencePhase::committing;
        return RenderPresentationPersistenceAdvance::commit;
    }
    phase_ = RenderPresentationPersistencePhase::saving;
    return RenderPresentationPersistenceAdvance::save;
}

bool RenderPresentationPersistenceGate::saveSucceeded(
    const RenderPresentationPersistenceTicket& ticket) noexcept {
    if (!isCurrent(ticket, RenderPresentationPersistencePhase::saving)) {
        return false;
    }
    durable_ = true;
    phase_ = RenderPresentationPersistencePhase::committing;
    return true;
}

std::optional<RenderPresentationPersistenceTicket>
RenderPresentationPersistenceGate::retryAfterStaleCommit(
    const RenderPresentationPersistenceTicket& ticket) noexcept {
    if (!durable_ ||
        !isCurrent(
            ticket, RenderPresentationPersistencePhase::committing) ||
        ticket.preparationAttempt ==
            std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }

    ++current_->preparationAttempt;
    phase_ = RenderPresentationPersistencePhase::preparing;
    return current_;
}

bool RenderPresentationPersistenceGate::consumeCommitted(
    const RenderPresentationPersistenceTicket& ticket) noexcept {
    if (!durable_ ||
        !isCurrent(
            ticket, RenderPresentationPersistencePhase::committing)) {
        return false;
    }
    clear();
    return true;
}

bool RenderPresentationPersistenceGate::abandon(
    const RenderPresentationPersistenceTicket& ticket) noexcept {
    if (!ownerThread() || !current_.has_value() ||
        *current_ != ticket || durable_) {
        return false;
    }
    clear();
    return true;
}

bool RenderPresentationPersistenceGate::busy() const noexcept {
    return ownerThread() && current_.has_value();
}

bool RenderPresentationPersistenceGate::exhausted() const noexcept {
    return serial_ == std::numeric_limits<std::uint64_t>::max();
}

bool RenderPresentationPersistenceGate::ownerThread() const noexcept {
    return std::this_thread::get_id() == ownerThread_;
}

void RenderPresentationPersistenceGate::clear() noexcept {
    current_.reset();
    phase_ = RenderPresentationPersistencePhase::preparing;
    durable_ = false;
}

} // namespace airfix::settings
