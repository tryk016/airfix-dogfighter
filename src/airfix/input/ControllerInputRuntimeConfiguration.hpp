#pragma once

#include "airfix/input/ControllerInputProfile.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace airfix::input {

struct ControllerInputRuntimeConfigurationResult;

// Immutable, internally consistent controller configuration for a fresh input
// pipeline. It keeps the resolved calibration/remapping record together with
// the exact BindingTable and physical-control usage mask compiled from that
// record so native adapters cannot accidentally publish mismatched halves or
// submit controls that the router does not bind.
class ControllerInputRuntimeConfiguration final {
public:
  static constexpr std::size_t physicalControlCount = 18U;

  ControllerInputRuntimeConfiguration(
      const ControllerInputRuntimeConfiguration &) noexcept = default;
  ControllerInputRuntimeConfiguration &
  operator=(const ControllerInputRuntimeConfiguration &) noexcept = default;

  [[nodiscard]] constexpr const ResolvedControllerInputProfile &
  profile() const noexcept {
    return profile_;
  }

  [[nodiscard]] constexpr const BindingTable &bindings() const noexcept {
    return bindings_;
  }

  [[nodiscard]] constexpr bool
  usesControllerControl(const ControlId control) const noexcept {
    const auto value = static_cast<std::size_t>(control.value);
    return value > 0U && value <= usedControls_.size() &&
           usedControls_[value - 1U];
  }

private:
  constexpr ControllerInputRuntimeConfiguration(
      const ResolvedControllerInputProfile &profile,
      const BindingTable &bindings,
      const std::array<bool, physicalControlCount> &usedControls) noexcept
      : profile_(profile), bindings_(bindings), usedControls_(usedControls) {}

  ResolvedControllerInputProfile profile_;
  BindingTable bindings_{};
  std::array<bool, physicalControlCount> usedControls_{};

  friend ControllerInputRuntimeConfigurationResult
  prepareControllerInputRuntimeConfiguration(
      const ResolvedControllerInputProfile &) noexcept;
};

struct ControllerInputRuntimeConfigurationResult final {
  std::optional<ControllerInputRuntimeConfiguration> configuration;
  std::optional<ControllerInputProfileIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return configuration.has_value() && !issue.has_value();
  }
};

// Performs the final binding compilation without mutating an active router.
// Native adapters publish the returned configuration only by constructing a
// fresh router/bridge pair before input processing starts. Live replacement
// remains unsupported until a host-owned pause transaction exists.
[[nodiscard]] ControllerInputRuntimeConfigurationResult
prepareControllerInputRuntimeConfiguration(
    const ResolvedControllerInputProfile &profile) noexcept;

} // namespace airfix::input
