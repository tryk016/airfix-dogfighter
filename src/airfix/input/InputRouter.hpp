#pragma once

#include "airfix/input/InputFrame.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace airfix::input {

enum class SourceKind : std::uint8_t {
    none = 0,
    touch = 1,
    controller = 2,
    keyboard = 3,
    mouse = 4,
    motion = 5,
    synthetic = 6,
    count = 7,
};

struct SourceHandle final {
    SourceKind kind{SourceKind::none};
    std::uint16_t instance{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return kind > SourceKind::none && kind < SourceKind::count;
    }

    [[nodiscard]] friend constexpr auto operator<=>(
        const SourceHandle&, const SourceHandle&) noexcept = default;
};

struct ControlId final {
    std::uint16_t value{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr auto operator<=>(
        const ControlId&, const ControlId&) noexcept = default;
};

enum class PhysicalEventKind : std::uint8_t {
    digital = 0,
    analog = 1,
    weaponSelection = 2,
    count = 3,
};

struct PhysicalEvent final {
    std::uint64_t sequence{};
    std::uint64_t timestamp{}; // Diagnostics only; never sampled into InputFrame.
    SourceHandle source{};
    ControlId control{};
    PhysicalEventKind kind{PhysicalEventKind::digital};
    std::int32_t value{};

    [[nodiscard]] static constexpr PhysicalEvent button(
        const std::uint64_t sequence,
        const std::uint64_t timestamp,
        const SourceHandle source,
        const ControlId control,
        const bool down) noexcept {
        return {sequence, timestamp, source, control, PhysicalEventKind::digital,
            down ? static_cast<std::int32_t>(q15One) : 0};
    }

    [[nodiscard]] static constexpr PhysicalEvent axis(
        const std::uint64_t sequence,
        const std::uint64_t timestamp,
        const SourceHandle source,
        const ControlId control,
        const Q15 value) noexcept {
        return {sequence, timestamp, source, control, PhysicalEventKind::analog,
            static_cast<std::int32_t>(value)};
    }

    [[nodiscard]] static constexpr PhysicalEvent selectWeapon(
        const std::uint64_t sequence,
        const std::uint64_t timestamp,
        const SourceHandle source,
        const ControlId control,
        const std::uint8_t selection) noexcept {
        return {sequence, timestamp, source, control,
            PhysicalEventKind::weaponSelection, selection};
    }
};

enum class BindingTargetKind : std::uint8_t {
    digital = 0,
    analog = 1,
    weaponSelection = 2,
    count = 3,
};

struct Binding final {
    SourceKind sourceKind{SourceKind::none};
    ControlId control{};
    PhysicalEventKind physicalKind{PhysicalEventKind::digital};
    BindingTargetKind targetKind{BindingTargetKind::digital};
    std::uint8_t target{};
    ContextMask contexts{};
    Q15 scale{q15One};
    Q15 meaningfulThreshold{1};

    [[nodiscard]] static constexpr Binding digital(
        const SourceKind sourceKind,
        const ControlId control,
        const DigitalAction action,
        const ContextMask contexts,
        const PhysicalEventKind physicalKind = PhysicalEventKind::digital,
        const Q15 actuationThreshold = 1) noexcept {
        return {sourceKind, control, physicalKind, BindingTargetKind::digital,
            static_cast<std::uint8_t>(action), contexts, q15One,
            actuationThreshold};
    }

    [[nodiscard]] static constexpr Binding analog(
        const SourceKind sourceKind,
        const ControlId control,
        const AnalogAxis axis,
        const ContextMask contexts,
        const PhysicalEventKind physicalKind = PhysicalEventKind::analog,
        const Q15 scale = q15One,
        const Q15 meaningfulThreshold = 4096) noexcept {
        return {sourceKind, control, physicalKind, BindingTargetKind::analog,
            static_cast<std::uint8_t>(axis), contexts, scale,
            meaningfulThreshold};
    }

    [[nodiscard]] static constexpr Binding weaponSelection(
        const SourceKind sourceKind,
        const ControlId control,
        const ContextMask contexts) noexcept {
        return {sourceKind, control, PhysicalEventKind::weaponSelection,
            BindingTargetKind::weaponSelection, 0U, contexts, q15One, 1};
    }
};

class BindingTable final {
public:
    static constexpr std::size_t capacity = 96U;

    [[nodiscard]] bool add(Binding binding) noexcept;
    void clear() noexcept;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_ == 0U;
    }

    [[nodiscard]] constexpr const Binding& operator[](
        const std::size_t index) const noexcept {
        return bindings_[index];
    }

    [[nodiscard]] constexpr const Binding* begin() const noexcept {
        return bindings_.data();
    }

    [[nodiscard]] constexpr const Binding* end() const noexcept {
        return bindings_.data() + size_;
    }

    [[nodiscard]] static BindingTable defaults() noexcept;

private:
    std::array<Binding, capacity> bindings_{};
    std::size_t size_{};
};

[[nodiscard]] BindingTable makeDefaultBindings() noexcept;

class NeutralGate final {
public:
    static constexpr std::uint8_t requiredNeutralTicks = 2U;

    void close() noexcept {
        open_ = false;
        neutralTicks_ = 0U;
    }

    void open() noexcept {
        open_ = true;
        neutralTicks_ = requiredNeutralTicks;
    }

    void observe(const bool neutral) noexcept {
        if (open_) {
            return;
        }
        if (!neutral) {
            neutralTicks_ = 0U;
            return;
        }
        if (neutralTicks_ < requiredNeutralTicks) {
            ++neutralTicks_;
        }
        if (neutralTicks_ == requiredNeutralTicks) {
            open_ = true;
        }
    }

    [[nodiscard]] bool isOpen() const noexcept {
        return open_;
    }

    [[nodiscard]] std::uint8_t neutralTicks() const noexcept {
        return neutralTicks_;
    }

private:
    bool open_{true};
    std::uint8_t neutralTicks_{requiredNeutralTicks};
};

class InputRouter final {
public:
    static constexpr std::size_t queueCapacity = 256U;
    static constexpr std::size_t sourceCapacity = 16U;
    static constexpr std::uint8_t axisOwnershipHysteresisTicks = 2U;

    explicit InputRouter(BindingTable bindings = BindingTable::defaults()) noexcept;

    // The router is single-thread-affine. Platform callbacks must marshal all
    // calls onto the same serialized input/simulation thread as tick().

    // Events may arrive out of order. A tick consumes them in ascending sequence
    // order. Duplicate/stale sequences and events beyond the hard limit fail.
    [[nodiscard]] bool enqueue(PhysicalEvent event) noexcept;
    [[nodiscard]] InputFrame tick(std::uint64_t simulationTick) noexcept;

    // Device/touch loss removes queued events from that source and synthesizes
    // only the releases no longer held by another source.
    void cancelSource(SourceHandle source) noexcept;

    // Lifecycle reset clears transient state and requires two consecutive
    // neutral ticks before new gameplay input is admitted.
    void reset() noexcept;
    void lifecycleReset() noexcept {
        reset();
    }

    // A context transition releases current semantic state. Controls held across
    // the transition remain blocked until their physical input becomes neutral.
    void setContext(InputContext context) noexcept;

    [[nodiscard]] InputContext context() const noexcept {
        return context_;
    }

    [[nodiscard]] std::size_t queuedEventCount() const noexcept {
        return queueSize_;
    }

    [[nodiscard]] std::uint64_t rejectedEventCount() const noexcept {
        return rejectedEventCount_;
    }

    [[nodiscard]] bool neutralGateOpen() const noexcept {
        return neutralGate_.isOpen();
    }

    [[nodiscard]] std::uint8_t neutralGateTicks() const noexcept {
        return neutralGate_.neutralTicks();
    }

private:
    struct SourceState final {
        bool occupied{};
        SourceHandle handle{};
        std::array<Q15, BindingTable::capacity> values{};
        std::array<bool, BindingTable::capacity> blockedUntilNeutral{};
        std::array<std::uint64_t, BindingTable::capacity> lastMeaningfulSequence{};
        NeutralGate neutralGate{};
    };

    struct AxisOwner final {
        bool valid{};
        std::uint8_t sourceIndex{};
        std::uint8_t bindingIndex{};
        std::uint8_t neutralTicks{};
    };

    [[nodiscard]] static bool bitSet(
        const std::array<std::uint64_t, digitalBitWordCount>& bits,
        DigitalAction action) noexcept;
    static void setBit(
        std::array<std::uint64_t, digitalBitWordCount>& bits,
        DigitalAction action, bool value) noexcept;
    [[nodiscard]] bool bindingActive(const Binding& binding) const noexcept;
    [[nodiscard]] bool bindingNeutral(
        const Binding& binding, Q15 value) const noexcept;
    [[nodiscard]] bool digitalHeld(DigitalAction action) const noexcept;
    [[nodiscard]] bool allPhysicalInputsNeutral() const noexcept;
    [[nodiscard]] bool sourcePhysicalInputsNeutral(
        const SourceState& source) const noexcept;
    [[nodiscard]] Q15 axisValue(AnalogAxis axis) const noexcept;
    [[nodiscard]] std::size_t findSource(SourceHandle source) const noexcept;
    [[nodiscard]] std::size_t findOrCreateSource(SourceHandle source) noexcept;
    void markNeutralRequired(SourceHandle source) noexcept;
    [[nodiscard]] bool consumeNeutralRequirement(SourceHandle source) noexcept;
    void sortQueue() noexcept;
    void processEvent(
        const PhysicalEvent& event,
        std::array<std::uint64_t, digitalBitWordCount>& pressed,
        std::array<std::uint64_t, digitalBitWordCount>& released,
        std::uint8_t& weaponSelection) noexcept;
    void updateAxisOwners() noexcept;
    void selectAxisFallback(std::size_t axisIndex) noexcept;
    void clearSemanticState() noexcept;
    void recomputeHeldAfterSourceChange() noexcept;

    BindingTable bindings_{};
    InputContext context_{InputContext::gameplay};
    NeutralGate neutralGate_{};
    std::array<PhysicalEvent, queueCapacity> queue_{};
    std::size_t queueSize_{};
    std::array<SourceState, sourceCapacity> sources_{};
    std::array<SourceHandle, sourceCapacity> neutralRequiredSources_{};
    std::size_t neutralRequiredSourceCount_{};
    std::array<AxisOwner, analogAxisCount> axisOwners_{};
    std::array<std::uint64_t, digitalBitWordCount> heldBits_{};
    std::array<std::uint64_t, digitalBitWordCount> pendingPressedBits_{};
    std::array<std::uint64_t, digitalBitWordCount> pendingReleasedBits_{};
    std::uint8_t pendingWeaponSelection_{noWeaponSelection};
    std::uint64_t lastProcessedSequence_{};
    bool hasProcessedSequence_{};
    std::uint64_t rejectedEventCount_{};
};

namespace controls {

namespace touch {
inline constexpr ControlId bank{1U};
inline constexpr ControlId pitch{2U};
inline constexpr ControlId throttleSet{3U};
inline constexpr ControlId lookX{4U};
inline constexpr ControlId lookY{5U};
inline constexpr ControlId primaryFire{6U};
inline constexpr ControlId secondaryFire{7U};
inline constexpr ControlId weaponNext{8U};
inline constexpr ControlId weaponSelection{9U};
inline constexpr ControlId rearView{10U};
inline constexpr ControlId cameraCycle{11U};
inline constexpr ControlId cameraRecenter{12U};
inline constexpr ControlId missionStatus{13U};
inline constexpr ControlId pause{14U};
inline constexpr ControlId confirm{15U};
inline constexpr ControlId cancel{16U};
} // namespace touch

namespace controller {
inline constexpr ControlId leftStickX{1U};
inline constexpr ControlId leftStickY{2U};
inline constexpr ControlId rightStickX{3U};
inline constexpr ControlId rightStickY{4U};
inline constexpr ControlId dpadUp{5U};
inline constexpr ControlId dpadDown{6U};
inline constexpr ControlId rightTrigger{7U};
inline constexpr ControlId leftTrigger{8U};
inline constexpr ControlId rightShoulder{9U};
inline constexpr ControlId leftShoulder{10U};
inline constexpr ControlId faceLeft{11U};
inline constexpr ControlId faceTop{12U};
inline constexpr ControlId rightStickClick{13U};
inline constexpr ControlId menu{14U};
inline constexpr ControlId facePrimary{15U};
inline constexpr ControlId faceSecondary{16U};
} // namespace controller

namespace keyboard {
// USB HID usage IDs keep this adapter-independent while remaining recognizable.
inline constexpr ControlId escape{0x29U};
inline constexpr ControlId space{0x2cU};
inline constexpr ControlId arrowRight{0x4fU};
inline constexpr ControlId arrowLeft{0x50U};
inline constexpr ControlId arrowDown{0x51U};
inline constexpr ControlId arrowUp{0x52U};
inline constexpr ControlId leftControl{0xe0U};
inline constexpr ControlId enter{0x28U};
} // namespace keyboard

} // namespace controls

} // namespace airfix::input
