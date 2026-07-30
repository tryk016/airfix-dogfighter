#pragma once

#include "airfix/input/ControllerInputBatchBridge.hpp"
#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::input {

enum class DesktopControllerAxis : std::uint8_t {
  bank = 0,
  pitch = 1,
  lookX = 2,
  lookY = 3,
  count = 4,
};

struct DesktopInputTick final {
  bool accepted{};
  InputFrame frame{};
};

// Platform-independent state machine used by the SDL3 Windows adapter. Native
// events are translated to stable ControlId/Q15 values before entering here.
// The bridge preserves ordered controller taps, emits one deterministic
// InputFrame per requested tick, and owns all lifecycle neutralization.
class DesktopInputBridge final {
public:
  static constexpr SourceHandle keyboardSource{SourceKind::keyboard, 1U};
  static constexpr SourceHandle mouseSource{SourceKind::mouse, 1U};
  static constexpr SourceHandle controllerSource{SourceKind::controller, 1U};

  DesktopInputBridge() noexcept = default;
  explicit DesktopInputBridge(
      const ControllerInputRuntimeConfiguration &configuration) noexcept;

  [[nodiscard]] bool key(ControlId control, bool pressed,
                         std::uint64_t timestamp = 0U) noexcept;
  [[nodiscard]] bool mouseButton(ControlId control, bool pressed,
                                 std::uint64_t timestamp = 0U) noexcept;
  [[nodiscard]] bool mouseMotion(Q15 lookX, Q15 lookY) noexcept;
  [[nodiscard]] bool mousePulse(ControlId control,
                                std::uint64_t timestamp = 0U) noexcept;
  void keyboardDisconnected() noexcept;
  void mouseDisconnected() noexcept;

  [[nodiscard]] bool
  connectController(std::uint64_t generation,
                    const ControllerSample &initialState = {}) noexcept;
  [[nodiscard]] bool controllerAxis(DesktopControllerAxis axis,
                                    Q15 value) noexcept;
  [[nodiscard]] bool controllerButton(ControllerDigitalControl control,
                                      bool pressed) noexcept;
  [[nodiscard]] bool disconnectController() noexcept;

  void focusLost() noexcept;
  void focusGained() noexcept;
  // Clears every source at a gameplay transaction boundary without changing
  // focus. Held controls must return to neutral before they can act again.
  void resetForGameplayBoundary() noexcept;
  void setContext(InputContext context) noexcept;

  [[nodiscard]] DesktopInputTick tick(std::uint64_t simulationTick) noexcept;

  [[nodiscard]] constexpr bool focused() const noexcept { return focused_; }

  [[nodiscard]] InputContext context() const noexcept {
    return router_.context();
  }

  [[nodiscard]] constexpr bool controllerConnected() const noexcept {
    return controllerConnected_;
  }

  [[nodiscard]] constexpr bool healthy() const noexcept { return !failed_; }

  [[nodiscard]] constexpr std::uint64_t controllerGeneration() const noexcept {
    return controllerConnected_ ? controllerBatch_.generation : 0U;
  }

  [[nodiscard]] constexpr const ResolvedControllerInputProfile *
  controllerProfile() const noexcept {
    return controllerBridge_.controllerProfile();
  }

private:
  [[nodiscard]] bool enqueueButton(SourceHandle source, ControlId control,
                                   bool pressed,
                                   std::uint64_t timestamp) noexcept;
  [[nodiscard]] bool enqueueValue(SourceHandle source, ControlId control,
                                  PhysicalEventKind kind, std::int32_t value,
                                  std::uint64_t timestamp) noexcept;
  [[nodiscard]] bool reserveEvents(std::size_t count) const noexcept;
  [[nodiscard]] bool flushMouse() noexcept;
  [[nodiscard]] bool flushController() noexcept;
  void fail() noexcept;
  void clearControllerState() noexcept;

  InputRouter router_{};
  ControllerInputBatchBridge controllerBridge_{};
  ControllerInputBatch controllerBatch_{};
  std::array<ControllerInputEmission,
             ControllerInputBatchBridge::maximumEmissionCount>
      controllerEmissions_{};
  Q15 pendingMouseX_{};
  Q15 pendingMouseY_{};
  std::uint64_t nextSequence_{1U};
  std::uint64_t nextControllerEdgeOrder_{1U};
  std::uint64_t lastControllerGeneration_{};
  bool mousePulseActive_{};
  bool focused_{true};
  bool controllerConnected_{};
  bool failed_{};
};

} // namespace airfix::input
