#pragma once

#import "AirfixIOSInputCoordinator.h"

#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"
#include "airfix/input/InputFrame.hpp"

#include <functional>
#include <utility>

namespace airfix::ios {

// Installed and invoked on the main thread. The coordinator calls the consumer
// synchronously exactly once for every fixed input tick, preserving digital
// edges that would be lost by polling the coalesced Objective-C diagnostics.
// Consumers must not retain the frame reference beyond the call. A thrown C++
// exception is caught by the bridge, removes the consumer, resets all input,
// marks the coordinator non-operational, and requests a terminal pipeline
// pause without unwinding through Objective-C.
using InputFrameConsumer =
    std::function<void(const input::InputFrame& frame)>;

namespace detail {

bool installInputFrameConsumer(
    AirfixIOSInputCoordinator* coordinator,
    const InputFrameConsumer& consumer) noexcept;
void reportInputFrameConsumerFailure(
    AirfixIOSInputCoordinator* coordinator) noexcept;
bool installControllerInputProfileBeforeStart(
    AirfixIOSInputCoordinator* coordinator,
    const input::ResolvedControllerInputProfile& profile) noexcept;

} // namespace detail

// Type erasure and assignment both live inside exception boundaries, including
// callables whose copy/move constructor throws. Failure is terminal because an
// operational coordinator must always retain its exactly-once frame sink.
template <typename Consumer>
bool setInputFrameConsumer(
    AirfixIOSInputCoordinator* coordinator,
    Consumer&& consumer) noexcept {
    try {
        InputFrameConsumer erased(std::forward<Consumer>(consumer));
        return detail::installInputFrameConsumer(coordinator, erased);
    }
    catch (...) {
        detail::reportInputFrameConsumerFailure(coordinator);
        return false;
    }
}

} // namespace airfix::ios
