#include "airfix/input/DesktopInputBridge.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace airfix::input {
namespace {

[[nodiscard]] constexpr bool validAxisValue(const Q15 value) noexcept {
  return value >= q15Min;
}

[[nodiscard]] constexpr bool
validControllerAxis(const DesktopControllerAxis axis) noexcept {
  return axis < DesktopControllerAxis::count;
}

[[nodiscard]] constexpr bool
validControllerControl(const ControllerDigitalControl control) noexcept {
  return control < ControllerDigitalControl::count;
}

} // namespace

bool DesktopInputBridge::key(const ControlId control, const bool pressed,
                             const std::uint64_t timestamp) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  return enqueueButton(keyboardSource, control, pressed, timestamp);
}

bool DesktopInputBridge::mouseButton(const ControlId control,
                                     const bool pressed,
                                     const std::uint64_t timestamp) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  return enqueueButton(mouseSource, control, pressed, timestamp);
}

bool DesktopInputBridge::mouseMotion(const Q15 lookX,
                                     const Q15 lookY) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  if (!validAxisValue(lookX) || !validAxisValue(lookY)) {
    fail();
    return false;
  }
  pendingMouseX_ = clampQ15(static_cast<std::int32_t>(pendingMouseX_) +
                            static_cast<std::int32_t>(lookX));
  pendingMouseY_ = clampQ15(static_cast<std::int32_t>(pendingMouseY_) +
                            static_cast<std::int32_t>(lookY));
  return true;
}

bool DesktopInputBridge::mousePulse(const ControlId control,
                                    const std::uint64_t timestamp) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  if (!control.valid() || !reserveEvents(2U)) {
    fail();
    return false;
  }
  return enqueueButton(mouseSource, control, true, timestamp) &&
         enqueueButton(mouseSource, control, false, timestamp);
}

void DesktopInputBridge::keyboardDisconnected() noexcept {
  router_.cancelSource(keyboardSource);
}

void DesktopInputBridge::mouseDisconnected() noexcept {
  pendingMouseX_ = q15Zero;
  pendingMouseY_ = q15Zero;
  mousePulseActive_ = false;
  router_.cancelSource(mouseSource);
}

bool DesktopInputBridge::connectController(
    const std::uint64_t generation,
    const ControllerSample &initialState) noexcept {
  if (failed_ || generation == 0U || generation <= lastControllerGeneration_ ||
      (!validAxisValue(initialState.bank) ||
       !validAxisValue(initialState.pitch) ||
       !validAxisValue(initialState.lookX) ||
       !validAxisValue(initialState.lookY))) {
    fail();
    return false;
  }

  if (controllerConnected_) {
    router_.cancelSource(controllerSource);
  }
  controllerBridge_.reset();
  controllerBatch_ = {};
  controllerBatch_.generation = generation;
  controllerBatch_.startingState = initialState;
  controllerBatch_.finalState = initialState;
  nextControllerEdgeOrder_ = 1U;
  lastControllerGeneration_ = generation;
  controllerConnected_ = true;

  // Every assignment, remap, or focus regain starts behind a per-source
  // neutral gate, including the first controller seen by the process.
  router_.cancelSource(controllerSource);
  return true;
}

bool DesktopInputBridge::controllerAxis(const DesktopControllerAxis axis,
                                        const Q15 value) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  if (!controllerConnected_ || !validControllerAxis(axis) ||
      !validAxisValue(value)) {
    fail();
    return false;
  }

  switch (axis) {
  case DesktopControllerAxis::bank:
    controllerBatch_.finalState.bank = value;
    break;
  case DesktopControllerAxis::pitch:
    controllerBatch_.finalState.pitch = value;
    break;
  case DesktopControllerAxis::lookX:
    controllerBatch_.finalState.lookX = value;
    break;
  case DesktopControllerAxis::lookY:
    controllerBatch_.finalState.lookY = value;
    break;
  case DesktopControllerAxis::count:
    fail();
    return false;
  }
  return true;
}

bool DesktopInputBridge::controllerButton(
    const ControllerDigitalControl control, const bool pressed) noexcept {
  if (!focused_ || failed_) {
    return !failed_;
  }
  if (!controllerConnected_ || !validControllerControl(control)) {
    fail();
    return false;
  }
  if (controllerBatch_.finalState.pressed(control) == pressed) {
    return true;
  }
  if (controllerBatch_.edgeCount == ControllerInputBatch::edgeCapacity ||
      nextControllerEdgeOrder_ == std::numeric_limits<std::uint64_t>::max()) {
    fail();
    return false;
  }

  controllerBatch_.edges[controllerBatch_.edgeCount] = {
      controllerBatch_.generation,
      nextControllerEdgeOrder_,
      control,
      pressed,
  };
  ++controllerBatch_.edgeCount;
  ++nextControllerEdgeOrder_;
  controllerBatch_.finalState.setPressed(control, pressed);
  return true;
}

bool DesktopInputBridge::disconnectController() noexcept {
  if (failed_) {
    return false;
  }
  if (!controllerConnected_) {
    return true;
  }
  router_.cancelSource(controllerSource);
  clearControllerState();
  return true;
}

void DesktopInputBridge::focusLost() noexcept {
  focused_ = false;
  resetForGameplayBoundary();
}

void DesktopInputBridge::focusGained() noexcept {
  focused_ = true;
  resetForGameplayBoundary();
}

void DesktopInputBridge::resetForGameplayBoundary() noexcept {
  pendingMouseX_ = q15Zero;
  pendingMouseY_ = q15Zero;
  mousePulseActive_ = false;
  clearControllerState();
  router_.lifecycleReset();
}

void DesktopInputBridge::setContext(const InputContext context) noexcept {
  pendingMouseX_ = q15Zero;
  pendingMouseY_ = q15Zero;
  mousePulseActive_ = false;
  router_.setContext(context);
}

DesktopInputTick
DesktopInputBridge::tick(const std::uint64_t simulationTick) noexcept {
  if (failed_ || !flushMouse() || !flushController()) {
    fail();
    return {};
  }
  return {true, router_.tick(simulationTick)};
}

bool DesktopInputBridge::enqueueButton(const SourceHandle source,
                                       const ControlId control,
                                       const bool pressed,
                                       const std::uint64_t timestamp) noexcept {
  return enqueueValue(source, control, PhysicalEventKind::digital,
                      pressed ? static_cast<std::int32_t>(q15One) : 0,
                      timestamp);
}

bool DesktopInputBridge::enqueueValue(const SourceHandle source,
                                      const ControlId control,
                                      const PhysicalEventKind kind,
                                      const std::int32_t value,
                                      const std::uint64_t timestamp) noexcept {
  if (!control.valid() || !reserveEvents(1U)) {
    fail();
    return false;
  }

  const PhysicalEvent event{
      nextSequence_, timestamp, source, control, kind, value,
  };
  if (!router_.enqueue(event)) {
    fail();
    return false;
  }
  ++nextSequence_;
  return true;
}

bool DesktopInputBridge::reserveEvents(const std::size_t count) const noexcept {
  if (count > InputRouter::queueCapacity - router_.queuedEventCount()) {
    return false;
  }
  if (count == 0U) {
    return true;
  }
  const auto available =
      std::numeric_limits<std::uint64_t>::max() - nextSequence_;
  return count <= available;
}

bool DesktopInputBridge::flushMouse() noexcept {
  const bool hasMotion = pendingMouseX_ != q15Zero || pendingMouseY_ != q15Zero;
  const std::size_t emissionCount =
      (mousePulseActive_ ? 2U : 0U) + (hasMotion ? 2U : 0U);
  if (!reserveEvents(emissionCount)) {
    return false;
  }

  using namespace controls::mouse;
  if (mousePulseActive_ &&
      (!enqueueValue(mouseSource, relativeX, PhysicalEventKind::analog, q15Zero,
                     0U) ||
       !enqueueValue(mouseSource, relativeY, PhysicalEventKind::analog, q15Zero,
                     0U))) {
    return false;
  }
  if (hasMotion &&
      (!enqueueValue(mouseSource, relativeX, PhysicalEventKind::analog,
                     pendingMouseX_, 0U) ||
       !enqueueValue(mouseSource, relativeY, PhysicalEventKind::analog,
                     pendingMouseY_, 0U))) {
    return false;
  }

  pendingMouseX_ = q15Zero;
  pendingMouseY_ = q15Zero;
  mousePulseActive_ = hasMotion;
  return true;
}

bool DesktopInputBridge::flushController() noexcept {
  if (!controllerConnected_) {
    return true;
  }

  const auto result = controllerBridge_.process(
      controllerBatch_, std::span{controllerEmissions_});
  if (!result.accepted() || !reserveEvents(result.emissionCount)) {
    return false;
  }
  for (std::size_t index = 0U; index < result.emissionCount; ++index) {
    const auto &emission = controllerEmissions_[index];
    if (!enqueueValue(controllerSource, emission.control, emission.kind,
                      emission.value, 0U)) {
      return false;
    }
  }

  controllerBatch_.startingState = controllerBatch_.finalState;
  controllerBatch_.edgeCount = 0U;
  controllerBatch_.overflowed = false;
  return true;
}

void DesktopInputBridge::fail() noexcept {
  failed_ = true;
  pendingMouseX_ = q15Zero;
  pendingMouseY_ = q15Zero;
  mousePulseActive_ = false;
  clearControllerState();
  router_.lifecycleReset();
}

void DesktopInputBridge::clearControllerState() noexcept {
  controllerConnected_ = false;
  controllerBridge_.reset();
  controllerBatch_ = {};
  controllerEmissions_.fill({});
  nextControllerEdgeOrder_ = 1U;
}

} // namespace airfix::input
