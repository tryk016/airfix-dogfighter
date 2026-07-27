#pragma once

#include <cstdint>

namespace airfix::runtime {

enum class ContentState : std::uint8_t {
    missing,
    validating,
    ready,
    rejected,
};

enum class LifecycleState : std::uint8_t {
    foregroundPaused,
    running,
    inactive,
    background,
};

class AppSession final {
public:
    [[nodiscard]] ContentState contentState() const noexcept;
    [[nodiscard]] LifecycleState lifecycleState() const noexcept;
    [[nodiscard]] bool inputsNeutral() const noexcept;
    [[nodiscard]] std::uint64_t inputResetGeneration() const noexcept;
    [[nodiscard]] bool simulationRunning() const noexcept;

    void setContentState(ContentState state) noexcept;
    [[nodiscard]] bool resume() noexcept;
    void pause() noexcept;
    void noteInputActivity() noexcept;
    void enterInactive() noexcept;
    void enterBackground() noexcept;
    void enterForeground() noexcept;

private:
    void resetInputs() noexcept;

    ContentState contentState_{ContentState::missing};
    LifecycleState lifecycleState_{LifecycleState::foregroundPaused};
    bool inputsNeutral_{true};
    std::uint64_t inputResetGeneration_{};
};

} // namespace airfix::runtime
