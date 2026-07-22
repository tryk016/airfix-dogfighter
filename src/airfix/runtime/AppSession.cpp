#include "airfix/runtime/AppSession.hpp"

namespace airfix::runtime {

ContentState AppSession::contentState() const noexcept {
    return contentState_;
}

LifecycleState AppSession::lifecycleState() const noexcept {
    return lifecycleState_;
}

bool AppSession::inputsNeutral() const noexcept {
    return inputsNeutral_;
}

std::uint64_t AppSession::inputResetGeneration() const noexcept {
    return inputResetGeneration_;
}

bool AppSession::simulationRunning() const noexcept {
    return lifecycleState_ == LifecycleState::running;
}

void AppSession::setContentState(const ContentState state) noexcept {
    contentState_ = state;
    if (state != ContentState::ready) {
        if (lifecycleState_ == LifecycleState::running) {
            lifecycleState_ = LifecycleState::foregroundPaused;
        }
        resetInputs();
    }
}

bool AppSession::resume() noexcept {
    if (contentState_ != ContentState::ready ||
        lifecycleState_ != LifecycleState::foregroundPaused) {
        return false;
    }
    lifecycleState_ = LifecycleState::running;
    return true;
}

void AppSession::pause() noexcept {
    if (lifecycleState_ == LifecycleState::running) {
        lifecycleState_ = LifecycleState::foregroundPaused;
    }
    resetInputs();
}

void AppSession::noteInputActivity() noexcept {
    if (lifecycleState_ == LifecycleState::running) {
        inputsNeutral_ = false;
    }
}

void AppSession::enterInactive() noexcept {
    lifecycleState_ = LifecycleState::inactive;
    resetInputs();
}

void AppSession::enterBackground() noexcept {
    lifecycleState_ = LifecycleState::background;
    resetInputs();
}

void AppSession::enterForeground() noexcept {
    lifecycleState_ = LifecycleState::foregroundPaused;
    resetInputs();
}

void AppSession::resetInputs() noexcept {
    inputsNeutral_ = true;
    ++inputResetGeneration_;
}

} // namespace airfix::runtime
