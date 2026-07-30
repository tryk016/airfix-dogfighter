#include "airfix/settings/RenderPresentationRequestQueue.hpp"

#include <limits>

namespace airfix::settings {

RenderPresentationRequestQueue::RenderPresentationRequestQueue(
    const std::uint64_t initialSerial) noexcept
    : serial_(initialSerial) {}

RenderPresentationRequestSubmitResult RenderPresentationRequestQueue::submit(
    const render::RenderPresentationSettings &candidate,
    const bool mayStartImmediately) noexcept {
  const auto request = issue(candidate);
  if (!request.has_value()) {
    return {};
  }

  if (phase_ == RenderPresentationRequestPhase::idle && !pending_.has_value() &&
      mayStartImmediately) {
    active_ = request;
    phase_ = RenderPresentationRequestPhase::active;
    return {
        .disposition = RenderPresentationRequestDisposition::start,
        .request = request,
        .superseded = std::nullopt,
    };
  }

  const auto superseded = pending_;
  pending_ = request;
  return {
      .disposition = RenderPresentationRequestDisposition::queued,
      .request = request,
      .superseded = superseded,
  };
}

std::optional<RenderPresentationRequestTicket>
RenderPresentationRequestQueue::activatePending() noexcept {
  if (phase_ != RenderPresentationRequestPhase::idle || active_.has_value() ||
      !pending_.has_value()) {
    return std::nullopt;
  }
  active_ = pending_;
  pending_.reset();
  phase_ = RenderPresentationRequestPhase::active;
  return active_;
}

bool RenderPresentationRequestQueue::beginCompletion(
    const RenderPresentationRequestTicket &request) noexcept {
  if (phase_ != RenderPresentationRequestPhase::active ||
      !active_.has_value() || *active_ != request) {
    return false;
  }
  phase_ = RenderPresentationRequestPhase::deliveringCompletion;
  return true;
}

bool RenderPresentationRequestQueue::finishCompletion(
    const RenderPresentationRequestTicket &request) noexcept {
  if (phase_ != RenderPresentationRequestPhase::deliveringCompletion ||
      !active_.has_value() || *active_ != request) {
    return false;
  }
  active_.reset();
  phase_ = RenderPresentationRequestPhase::idle;
  return true;
}

bool RenderPresentationRequestQueue::hasOutstandingWork() const noexcept {
  return active_.has_value() || pending_.has_value();
}

bool RenderPresentationRequestQueue::exhausted() const noexcept {
  return serial_ == std::numeric_limits<std::uint64_t>::max();
}

std::optional<RenderPresentationRequestTicket>
RenderPresentationRequestQueue::issue(
    const render::RenderPresentationSettings &candidate) noexcept {
  if (exhausted()) {
    return std::nullopt;
  }
  ++serial_;
  return RenderPresentationRequestTicket{
      .serial = serial_,
      .candidate = candidate,
  };
}

} // namespace airfix::settings
