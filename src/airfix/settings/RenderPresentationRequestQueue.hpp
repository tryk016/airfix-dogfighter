#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstdint>
#include <optional>

namespace airfix::settings {

struct RenderPresentationRequestTicket final {
  std::uint64_t serial{};
  render::RenderPresentationSettings candidate;

  [[nodiscard]] friend constexpr bool
  operator==(const RenderPresentationRequestTicket &,
             const RenderPresentationRequestTicket &) noexcept = default;
};

enum class RenderPresentationRequestDisposition : std::uint8_t {
  start,
  queued,
  exhausted,
};

struct RenderPresentationRequestSubmitResult final {
  RenderPresentationRequestDisposition disposition{
      RenderPresentationRequestDisposition::exhausted};
  std::optional<RenderPresentationRequestTicket> request;
  std::optional<RenderPresentationRequestTicket> superseded;
};

enum class RenderPresentationRequestPhase : std::uint8_t {
  idle,
  active,
  deliveringCompletion,
};

// Portable owner-side arbitration for complete render-settings snapshots.
//
// Platform code owns the callback objects and the prepare/save/publish
// transaction. This queue owns only immutable request identities and the
// latest-wins decision. Completion is deliberately two-phase so a callback may
// synchronously enqueue another request without starting concurrent work.
class RenderPresentationRequestQueue final {
public:
  explicit RenderPresentationRequestQueue(
      std::uint64_t initialSerial = 0U) noexcept;

  // candidate must already have passed the render-settings validator.
  // A request starts only when mayStartImmediately is true and no request is
  // active or waiting. Otherwise it becomes the sole pending request and
  // returns the complete pending request that it superseded, if any.
  [[nodiscard]] RenderPresentationRequestSubmitResult
  submit(const render::RenderPresentationSettings &candidate,
         bool mayStartImmediately) noexcept;

  // Promotes the pending request only while no active request or completion
  // delivery exists.
  [[nodiscard]] std::optional<RenderPresentationRequestTicket>
  activatePending() noexcept;

  // Completion is accepted only for the exact active immutable ticket.
  [[nodiscard]] bool
  beginCompletion(const RenderPresentationRequestTicket &request) noexcept;
  [[nodiscard]] bool
  finishCompletion(const RenderPresentationRequestTicket &request) noexcept;

  [[nodiscard]] bool hasOutstandingWork() const noexcept;
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] RenderPresentationRequestPhase phase() const noexcept {
    return phase_;
  }
  [[nodiscard]] const std::optional<RenderPresentationRequestTicket> &
  activeRequest() const noexcept {
    return active_;
  }
  [[nodiscard]] const std::optional<RenderPresentationRequestTicket> &
  pendingRequest() const noexcept {
    return pending_;
  }

private:
  std::optional<RenderPresentationRequestTicket>
  issue(const render::RenderPresentationSettings &candidate) noexcept;

  std::optional<RenderPresentationRequestTicket> active_;
  std::optional<RenderPresentationRequestTicket> pending_;
  RenderPresentationRequestPhase phase_{RenderPresentationRequestPhase::idle};
  std::uint64_t serial_{};
};

} // namespace airfix::settings
