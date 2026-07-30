#include "airfix/input/InputRouter.hpp"

#include <algorithm>
#include <cstdint>

namespace airfix::input {
namespace {

[[nodiscard]] constexpr std::int32_t magnitude(const Q15 value) noexcept {
    const auto wide = static_cast<std::int32_t>(value);
    return wide < 0 ? -wide : wide;
}

[[nodiscard]] constexpr Q15 scaledValue(
    const Q15 value, const Q15 scale) noexcept {
    const auto product = static_cast<std::int32_t>(value) *
        static_cast<std::int32_t>(scale);
    return clampQ15(product / static_cast<std::int32_t>(q15One));
}

[[nodiscard]] constexpr bool validContexts(const ContextMask contexts) noexcept {
    return contexts != 0U && (contexts & static_cast<ContextMask>(~allContexts)) == 0U;
}

} // namespace

bool BindingTable::add(const Binding binding) noexcept {
    if (size_ == capacity || binding.sourceKind <= SourceKind::none ||
        binding.sourceKind >= SourceKind::count ||
        binding.physicalKind >= PhysicalEventKind::count ||
        !binding.control.valid() || !validContexts(binding.contexts) ||
        binding.meaningfulThreshold < 0) {
        return false;
    }

    switch (binding.targetKind) {
    case BindingTargetKind::digital:
        if (binding.target >= digitalActionCount ||
            binding.physicalKind == PhysicalEventKind::weaponSelection ||
            binding.meaningfulThreshold == 0) {
            return false;
        }
        break;
    case BindingTargetKind::analog:
        if (binding.target >= analogAxisCount || binding.scale == 0 ||
            binding.physicalKind == PhysicalEventKind::weaponSelection) {
            return false;
        }
        break;
    case BindingTargetKind::weaponSelection:
        if (binding.physicalKind != PhysicalEventKind::weaponSelection) {
            return false;
        }
        break;
    default:
        return false;
    }

    bindings_[size_++] = binding;
    return true;
}

void BindingTable::clear() noexcept {
    bindings_.fill({});
    size_ = 0U;
}

BindingTable BindingTable::defaults() noexcept {
    return makeDefaultBindings();
}

BindingTable makeDefaultBindings() noexcept {
    BindingTable table;
    constexpr auto gameplay = gameplayContext;
    constexpr auto menus = static_cast<ContextMask>(menuContext | modalContext);

    const auto add = [&table](const Binding binding) noexcept {
        static_cast<void>(table.add(binding));
    };

    add(Binding::analog(SourceKind::touch, controls::touch::bank,
        AnalogAxis::flightBank, gameplay));
    add(Binding::analog(SourceKind::touch, controls::touch::pitch,
        AnalogAxis::flightPitch, gameplay));
    add(Binding::analog(SourceKind::touch, controls::touch::throttleSet,
        AnalogAxis::flightThrottleSet, gameplay, PhysicalEventKind::analog,
        q15One, 512, false));
    add(Binding::analog(SourceKind::touch, controls::touch::throttleIncrease,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::touch, controls::touch::throttleDecrease,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::analog(SourceKind::touch, controls::touch::lookX,
        AnalogAxis::cameraLookX, gameplay));
    add(Binding::analog(SourceKind::touch, controls::touch::lookY,
        AnalogAxis::cameraLookY, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::primaryFire,
        DigitalAction::combatPrimaryFire, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::secondaryFire,
        DigitalAction::combatSecondaryFire, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::weaponNext,
        DigitalAction::combatWeaponNext, gameplay));
    add(Binding::weaponSelection(SourceKind::touch,
        controls::touch::weaponSelection, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::rearView,
        DigitalAction::cameraRearView, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::cameraCycle,
        DigitalAction::cameraCycle, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::cameraRecenter,
        DigitalAction::cameraRecenter, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::missionStatus,
        DigitalAction::missionStatus, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::pause,
        DigitalAction::globalPause, gameplay));
    add(Binding::digital(SourceKind::touch, controls::touch::confirm,
        DigitalAction::uiConfirm, menus));
    add(Binding::digital(SourceKind::touch, controls::touch::cancel,
        DigitalAction::uiCancel, menus));
    add(Binding::digital(SourceKind::touch, controls::touch::tabPrevious,
        DigitalAction::uiTabPrevious, menus));
    add(Binding::digital(SourceKind::touch, controls::touch::tabNext,
        DigitalAction::uiTabNext, menus));
    add(Binding::digital(SourceKind::touch, controls::touch::cancel,
        DigitalAction::globalBack, gameplay));

    add(Binding::analog(SourceKind::controller, controls::controller::leftStickX,
        AnalogAxis::flightBank, gameplay));
    add(Binding::analog(SourceKind::controller, controls::controller::leftStickY,
        AnalogAxis::flightPitch, gameplay));
    add(Binding::analog(SourceKind::controller, controls::controller::rightStickX,
        AnalogAxis::cameraLookX, gameplay));
    add(Binding::analog(SourceKind::controller, controls::controller::rightStickY,
        AnalogAxis::cameraLookY, gameplay));
    add(Binding::analog(SourceKind::controller, controls::controller::dpadUp,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::controller, controls::controller::dpadDown,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::digital(SourceKind::controller,
        controls::controller::rightTrigger, DigitalAction::combatPrimaryFire,
        gameplay, PhysicalEventKind::analog, 16384));
    add(Binding::digital(SourceKind::controller,
        controls::controller::leftTrigger, DigitalAction::combatSecondaryFire,
        gameplay, PhysicalEventKind::analog, 16384));
    add(Binding::digital(SourceKind::controller,
        controls::controller::rightShoulder, DigitalAction::combatWeaponNext,
        gameplay));
    add(Binding::digital(SourceKind::controller,
        controls::controller::leftShoulder, DigitalAction::cameraRearView,
        gameplay));
    add(Binding::digital(SourceKind::controller, controls::controller::faceLeft,
        DigitalAction::cameraCycle, gameplay));
    add(Binding::digital(SourceKind::controller, controls::controller::faceTop,
        DigitalAction::missionStatus, gameplay));
    add(Binding::digital(SourceKind::controller,
        controls::controller::rightStickClick, DigitalAction::cameraRecenter,
        gameplay));
    add(Binding::digital(SourceKind::controller, controls::controller::menu,
        DigitalAction::globalPause, gameplay));
    add(Binding::analog(SourceKind::controller, controls::controller::leftStickX,
        AnalogAxis::uiNavigateX, menus));
    add(Binding::analog(SourceKind::controller, controls::controller::leftStickY,
        AnalogAxis::uiNavigateY, menus));
    add(Binding::analog(SourceKind::controller, controls::controller::dpadUp,
        AnalogAxis::uiNavigateY, menus, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::controller, controls::controller::dpadDown,
        AnalogAxis::uiNavigateY, menus, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::digital(SourceKind::controller,
        controls::controller::facePrimary, DigitalAction::uiConfirm, menus));
    add(Binding::digital(SourceKind::controller,
        controls::controller::faceSecondary, DigitalAction::uiCancel, menus));
    add(Binding::digital(SourceKind::controller,
        controls::controller::leftShoulder, DigitalAction::uiTabPrevious,
        menus));
    add(Binding::digital(SourceKind::controller,
        controls::controller::rightShoulder, DigitalAction::uiTabNext,
        menus));
    add(Binding::digital(SourceKind::controller,
        controls::controller::faceSecondary, DigitalAction::globalBack,
        gameplay));

    add(Binding::digital(SourceKind::keyboard, controls::keyboard::space,
        DigitalAction::combatPrimaryFire, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::leftControl,
        DigitalAction::combatSecondaryFire, gameplay));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowLeft,
        AnalogAxis::flightBank, gameplay, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowRight,
        AnalogAxis::flightBank, gameplay, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowUp,
        AnalogAxis::flightPitch, gameplay, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowDown,
        AnalogAxis::flightPitch, gameplay, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::w,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::s,
        AnalogAxis::flightThrottleDelta, gameplay, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::tab,
        DigitalAction::combatWeaponNext, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::r,
        DigitalAction::cameraRearView, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::c,
        DigitalAction::cameraCycle, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::f,
        DigitalAction::cameraRecenter, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::m,
        DigitalAction::missionStatus, gameplay));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::escape,
        DigitalAction::globalPause, gameplay));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowLeft,
        AnalogAxis::uiNavigateX, menus, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowRight,
        AnalogAxis::uiNavigateX, menus, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowUp,
        AnalogAxis::uiNavigateY, menus, PhysicalEventKind::digital,
        q15One, 1));
    add(Binding::analog(SourceKind::keyboard, controls::keyboard::arrowDown,
        AnalogAxis::uiNavigateY, menus, PhysicalEventKind::digital,
        q15Min, 1));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::enter,
        DigitalAction::uiConfirm, menus));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::escape,
        DigitalAction::uiCancel, menus));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::q,
        DigitalAction::uiTabPrevious, menus));
    add(Binding::digital(SourceKind::keyboard, controls::keyboard::e,
        DigitalAction::uiTabNext, menus));

    add(Binding::analog(SourceKind::mouse, controls::mouse::relativeX,
        AnalogAxis::cameraLookX, gameplay, PhysicalEventKind::analog,
        q15One, 1));
    add(Binding::analog(SourceKind::mouse, controls::mouse::relativeY,
        AnalogAxis::cameraLookY, gameplay, PhysicalEventKind::analog,
        q15One, 1));
    add(Binding::digital(SourceKind::mouse, controls::mouse::leftButton,
        DigitalAction::combatPrimaryFire, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::rightButton,
        DigitalAction::combatSecondaryFire, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::middleButton,
        DigitalAction::cameraCycle, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::extraButtonOne,
        DigitalAction::cameraRearView, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::extraButtonTwo,
        DigitalAction::missionStatus, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::wheelUp,
        DigitalAction::combatWeaponNext, gameplay));
    add(Binding::digital(SourceKind::mouse, controls::mouse::wheelDown,
        DigitalAction::combatWeaponNext, gameplay));

    return table;
}

InputRouter::InputRouter(BindingTable bindings) noexcept
    : bindings_(bindings) {}

bool InputRouter::enqueue(const PhysicalEvent event) noexcept {
    if (!event.source.valid() || !event.control.valid() ||
        queueSize_ == queueCapacity ||
        (hasProcessedSequence_ && event.sequence <= lastProcessedSequence_)) {
        ++rejectedEventCount_;
        return false;
    }

    for (std::size_t index = 0U; index < queueSize_; ++index) {
        if (queue_[index].sequence == event.sequence) {
            ++rejectedEventCount_;
            return false;
        }
    }

    const auto hasBinding = std::any_of(
        bindings_.begin(), bindings_.end(),
        [&event](const Binding& binding) {
            return binding.sourceKind == event.source.kind &&
                binding.control == event.control &&
                binding.physicalKind == event.kind;
        });
    if (!hasBinding) {
        ++rejectedEventCount_;
        return false;
    }

    if (findOrCreateSource(event.source) == sourceCapacity) {
        ++rejectedEventCount_;
        return false;
    }

    queue_[queueSize_++] = event;
    return true;
}

InputFrame InputRouter::tick(const std::uint64_t simulationTick) noexcept {
    InputFrame frame;
    frame.simulationTick = simulationTick;
    frame.pressedBits = pendingPressedBits_;
    frame.releasedBits = pendingReleasedBits_;
    frame.weaponSelection = pendingWeaponSelection_;
    pendingPressedBits_.fill(0U);
    pendingReleasedBits_.fill(0U);
    pendingWeaponSelection_ = noWeaponSelection;

    sortQueue();
    for (std::size_t index = 0U; index < queueSize_; ++index) {
        processEvent(queue_[index], frame.pressedBits, frame.releasedBits,
            frame.weaponSelection);
        lastProcessedSequence_ = queue_[index].sequence;
        hasProcessedSequence_ = true;
    }
    queueSize_ = 0U;

    for (auto& source : sources_) {
        if (source.occupied && !source.neutralGate.isOpen()) {
            source.neutralGate.observe(sourcePhysicalInputsNeutral(source));
        }
    }

    updateAxisOwners();
    if (!neutralGate_.isOpen()) {
        neutralGate_.observe(allPhysicalInputsNeutral());
    }

    frame.heldBits = heldBits_;
    for (std::size_t index = 0U; index < analogAxisCount; ++index) {
        frame.analogValues[index] = axisValue(static_cast<AnalogAxis>(index));
    }
    return frame;
}

void InputRouter::cancelSource(const SourceHandle source) noexcept {
    if (!source.valid()) {
        return;
    }
    // Events already delivered by other sources happened before this cancel.
    // Consume them in sequence order and retain their edges for the next tick,
    // while dropping pending events from the source that was lost.
    sortQueue();
    for (std::size_t index = 0U; index < queueSize_; ++index) {
        const auto& event = queue_[index];
        if (event.source != source) {
            processEvent(event, pendingPressedBits_, pendingReleasedBits_,
                pendingWeaponSelection_);
        }
        lastProcessedSequence_ = event.sequence;
        hasProcessedSequence_ = true;
    }
    queueSize_ = 0U;
    if (source.kind == SourceKind::controller) {
        markNeutralRequired(source);
    }

    const auto sourceIndex = findSource(source);
    if (sourceIndex == sourceCapacity) {
        return;
    }

    sources_[sourceIndex] = {};
    for (auto& owner : axisOwners_) {
        if (owner.valid && owner.sourceIndex == sourceIndex) {
            owner = {};
        }
    }
    recomputeHeldAfterSourceChange();
}

void InputRouter::reset() noexcept {
    clearSemanticState();
    queueSize_ = 0U;
    pendingPressedBits_.fill(0U);
    pendingWeaponSelection_ = noWeaponSelection;
    sources_.fill({});
    axisOwners_.fill({});
    lastProcessedSequence_ = 0U;
    hasProcessedSequence_ = false;
    neutralGate_.close();
}

void InputRouter::setContext(const InputContext context) noexcept {
    if (contextMask(context) == 0U || context == context_) {
        return;
    }

    // Edges retained by an earlier source cancellation still belong to the old
    // context. Drop any press (and matching release) the consumer never sampled,
    // while preserving releases for actions that had already been observed held.
    auto heldBeforeTransition = heldBits_;
    for (std::size_t word = 0U; word < digitalBitWordCount; ++word) {
        const auto unsampledPresses = pendingPressedBits_[word];
        pendingPressedBits_[word] = 0U;
        pendingReleasedBits_[word] &= ~unsampledPresses;
        heldBeforeTransition[word] &= ~unsampledPresses;
    }
    pendingWeaponSelection_ = noWeaponSelection;

    // Pending delivery belongs to the old context. Apply it to physical source
    // state so a control held across the boundary is blocked, but do not expose
    // semantic edges that the consumer never sampled in the old context.
    std::array<std::uint64_t, digitalBitWordCount> ignoredPressed{};
    std::array<std::uint64_t, digitalBitWordCount> ignoredReleased{};
    std::uint8_t ignoredWeaponSelection = noWeaponSelection;
    sortQueue();
    for (std::size_t index = 0U; index < queueSize_; ++index) {
        processEvent(queue_[index], ignoredPressed, ignoredReleased,
            ignoredWeaponSelection);
        lastProcessedSequence_ = queue_[index].sequence;
        hasProcessedSequence_ = true;
    }
    queueSize_ = 0U;
    heldBits_ = heldBeforeTransition;
    clearSemanticState();
    context_ = context;
    for (auto& source : sources_) {
        if (!source.occupied) {
            continue;
        }
        for (std::size_t bindingIndex = 0U;
             bindingIndex < bindings_.size(); ++bindingIndex) {
            source.blockedUntilNeutral[bindingIndex] =
                bindings_[bindingIndex].blocksNeutralGate &&
                !bindingNeutral(
                    bindings_[bindingIndex], source.values[bindingIndex]);
        }
    }
}

bool InputRouter::bitSet(
    const std::array<std::uint64_t, digitalBitWordCount>& bits,
    const DigitalAction action) noexcept {
    const auto index = toIndex(action);
    return index < digitalActionCount &&
        (bits[index / 64U] & (std::uint64_t{1U} << (index % 64U))) != 0U;
}

void InputRouter::setBit(
    std::array<std::uint64_t, digitalBitWordCount>& bits,
    const DigitalAction action, const bool value) noexcept {
    const auto index = toIndex(action);
    if (index >= digitalActionCount) {
        return;
    }
    const auto mask = std::uint64_t{1U} << (index % 64U);
    if (value) {
        bits[index / 64U] |= mask;
    }
    else {
        bits[index / 64U] &= ~mask;
    }
}

bool InputRouter::bindingActive(const Binding& binding) const noexcept {
    return (binding.contexts & contextMask(context_)) != 0U;
}

bool InputRouter::bindingNeutral(
    const Binding& binding, const Q15 value) const noexcept {
    if (binding.targetKind == BindingTargetKind::weaponSelection) {
        return true;
    }
    const auto valueMagnitude = magnitude(value);
    const auto threshold = static_cast<std::int32_t>(binding.meaningfulThreshold);
    if (binding.targetKind == BindingTargetKind::digital) {
        return valueMagnitude < threshold;
    }
    return valueMagnitude <= threshold;
}

bool InputRouter::digitalHeld(const DigitalAction action) const noexcept {
    if (!neutralGate_.isOpen()) {
        return false;
    }
    for (const auto& source : sources_) {
        if (!source.occupied || !source.neutralGate.isOpen()) {
            continue;
        }
        for (std::size_t bindingIndex = 0U;
             bindingIndex < bindings_.size(); ++bindingIndex) {
            const auto& binding = bindings_[bindingIndex];
            if (binding.targetKind == BindingTargetKind::digital &&
                binding.target == static_cast<std::uint8_t>(action) &&
                binding.sourceKind == source.handle.kind &&
                bindingActive(binding) &&
                !source.blockedUntilNeutral[bindingIndex] &&
                !bindingNeutral(binding, source.values[bindingIndex])) {
                return true;
            }
        }
    }
    return false;
}

bool InputRouter::allPhysicalInputsNeutral() const noexcept {
    for (const auto& source : sources_) {
        if (source.occupied && !sourcePhysicalInputsNeutral(source)) {
            return false;
        }
    }
    return true;
}

bool InputRouter::sourcePhysicalInputsNeutral(
    const SourceState& source) const noexcept {
    for (std::size_t bindingIndex = 0U;
         bindingIndex < bindings_.size(); ++bindingIndex) {
        const auto& binding = bindings_[bindingIndex];
        if (binding.sourceKind == source.handle.kind &&
            binding.blocksNeutralGate &&
            !bindingNeutral(binding, source.values[bindingIndex])) {
            return false;
        }
    }
    return true;
}

Q15 InputRouter::axisValue(const AnalogAxis axis) const noexcept {
    if (!neutralGate_.isOpen()) {
        return q15Zero;
    }
    const auto& owner = axisOwners_[toIndex(axis)];
    if (!owner.valid || owner.sourceIndex >= sourceCapacity ||
        owner.bindingIndex >= bindings_.size()) {
        return q15Zero;
    }
    const auto& source = sources_[owner.sourceIndex];
    const auto& binding = bindings_[owner.bindingIndex];
    if (!source.occupied || !source.neutralGate.isOpen() ||
        binding.targetKind != BindingTargetKind::analog ||
        binding.target != static_cast<std::uint8_t>(axis) ||
        !bindingActive(binding) ||
        source.blockedUntilNeutral[owner.bindingIndex] ||
        bindingNeutral(binding, source.values[owner.bindingIndex])) {
        return q15Zero;
    }
    return scaledValue(source.values[owner.bindingIndex], binding.scale);
}

std::size_t InputRouter::findSource(const SourceHandle source) const noexcept {
    for (std::size_t index = 0U; index < sourceCapacity; ++index) {
        if (sources_[index].occupied && sources_[index].handle == source) {
            return index;
        }
    }
    return sourceCapacity;
}

std::size_t InputRouter::findOrCreateSource(const SourceHandle source) noexcept {
    const auto existing = findSource(source);
    if (existing != sourceCapacity) {
        return existing;
    }
    for (std::size_t index = 0U; index < sourceCapacity; ++index) {
        if (!sources_[index].occupied) {
            sources_[index] = {};
            sources_[index].occupied = true;
            sources_[index].handle = source;
            if (consumeNeutralRequirement(source)) {
                sources_[index].neutralGate.close();
            }
            return index;
        }
    }
    return sourceCapacity;
}

void InputRouter::markNeutralRequired(const SourceHandle source) noexcept {
    for (std::size_t index = 0U; index < neutralRequiredSourceCount_; ++index) {
        if (neutralRequiredSources_[index] == source) {
            return;
        }
    }
    if (neutralRequiredSourceCount_ == sourceCapacity) {
        for (std::size_t index = 1U; index < sourceCapacity; ++index) {
            neutralRequiredSources_[index - 1U] = neutralRequiredSources_[index];
        }
        --neutralRequiredSourceCount_;
    }
    neutralRequiredSources_[neutralRequiredSourceCount_++] = source;
}

bool InputRouter::consumeNeutralRequirement(const SourceHandle source) noexcept {
    for (std::size_t index = 0U; index < neutralRequiredSourceCount_; ++index) {
        const auto matches = neutralRequiredSources_[index] == source ||
            (source.kind == SourceKind::controller &&
                neutralRequiredSources_[index].kind == SourceKind::controller);
        if (!matches) {
            continue;
        }
        for (std::size_t move = index + 1U;
             move < neutralRequiredSourceCount_; ++move) {
            neutralRequiredSources_[move - 1U] = neutralRequiredSources_[move];
        }
        --neutralRequiredSourceCount_;
        neutralRequiredSources_[neutralRequiredSourceCount_] = {};
        return true;
    }
    return false;
}

void InputRouter::sortQueue() noexcept {
    // Stable insertion sort is deterministic, allocation-free, and appropriate
    // for the small bounded batch normally accumulated between fixed ticks.
    for (std::size_t index = 1U; index < queueSize_; ++index) {
        const auto event = queue_[index];
        auto destination = index;
        while (destination > 0U &&
            event.sequence < queue_[destination - 1U].sequence) {
            queue_[destination] = queue_[destination - 1U];
            --destination;
        }
        queue_[destination] = event;
    }
}

void InputRouter::processEvent(
    const PhysicalEvent& event,
    std::array<std::uint64_t, digitalBitWordCount>& pressed,
    std::array<std::uint64_t, digitalBitWordCount>& released,
    std::uint8_t& weaponSelection) noexcept {
    const auto sourceIndex = findSource(event.source);
    if (sourceIndex == sourceCapacity) {
        ++rejectedEventCount_;
        return;
    }
    auto& source = sources_[sourceIndex];
    const auto value = clampQ15(event.value);

    for (std::size_t bindingIndex = 0U;
         bindingIndex < bindings_.size(); ++bindingIndex) {
        const auto& binding = bindings_[bindingIndex];
        if (binding.sourceKind != event.source.kind ||
            binding.control != event.control ||
            binding.physicalKind != event.kind) {
            continue;
        }

        if (binding.targetKind == BindingTargetKind::weaponSelection) {
            if (neutralGate_.isOpen() && source.neutralGate.isOpen() &&
                bindingActive(binding) &&
                event.value >= 0 && event.value < weaponSlotCount) {
                weaponSelection = static_cast<std::uint8_t>(event.value);
            }
            continue;
        }

        if (binding.targetKind == BindingTargetKind::digital) {
            const auto action = static_cast<DigitalAction>(binding.target);
            const auto before = digitalHeld(action);
            source.values[bindingIndex] = value;
            if (bindingNeutral(binding, value)) {
                source.blockedUntilNeutral[bindingIndex] = false;
            }
            const auto after = digitalHeld(action);
            if (!before && after) {
                setBit(pressed, action, true);
            }
            if (before && !after) {
                setBit(released, action, true);
            }
            setBit(heldBits_, action, after);
            continue;
        }

        source.values[bindingIndex] = value;
        if (bindingNeutral(binding, value)) {
            source.blockedUntilNeutral[bindingIndex] = false;
        }
        else if (neutralGate_.isOpen() && source.neutralGate.isOpen() &&
            bindingActive(binding) &&
            !source.blockedUntilNeutral[bindingIndex]) {
            source.lastMeaningfulSequence[bindingIndex] = event.sequence;
            axisOwners_[binding.target] = {
                true,
                static_cast<std::uint8_t>(sourceIndex),
                static_cast<std::uint8_t>(bindingIndex),
                0U,
            };
        }
    }
}

void InputRouter::updateAxisOwners() noexcept {
    if (!neutralGate_.isOpen()) {
        axisOwners_.fill({});
        return;
    }

    for (std::size_t axisIndex = 0U; axisIndex < axisOwners_.size(); ++axisIndex) {
        auto& owner = axisOwners_[axisIndex];
        if (!owner.valid || owner.sourceIndex >= sourceCapacity ||
            owner.bindingIndex >= bindings_.size()) {
            owner = {};
            selectAxisFallback(axisIndex);
            continue;
        }
        const auto& source = sources_[owner.sourceIndex];
        const auto& binding = bindings_[owner.bindingIndex];
        if (!source.occupied || !source.neutralGate.isOpen() ||
            !bindingActive(binding) ||
            source.blockedUntilNeutral[owner.bindingIndex]) {
            owner = {};
            selectAxisFallback(axisIndex);
            continue;
        }
        if (bindingNeutral(binding, source.values[owner.bindingIndex])) {
            if (owner.neutralTicks < axisOwnershipHysteresisTicks) {
                ++owner.neutralTicks;
            }
            if (owner.neutralTicks >= axisOwnershipHysteresisTicks) {
                owner = {};
                selectAxisFallback(axisIndex);
            }
        }
        else {
            owner.neutralTicks = 0U;
        }
    }
}

void InputRouter::selectAxisFallback(const std::size_t axisIndex) noexcept {
    if (!neutralGate_.isOpen() || axisIndex >= axisOwners_.size()) {
        return;
    }

    bool found = false;
    std::uint64_t newestSequence = 0U;
    AxisOwner candidate{};
    for (std::size_t sourceIndex = 0U; sourceIndex < sourceCapacity; ++sourceIndex) {
        const auto& source = sources_[sourceIndex];
        if (!source.occupied || !source.neutralGate.isOpen()) {
            continue;
        }
        for (std::size_t bindingIndex = 0U;
             bindingIndex < bindings_.size(); ++bindingIndex) {
            const auto& binding = bindings_[bindingIndex];
            if (binding.targetKind != BindingTargetKind::analog ||
                binding.target != axisIndex ||
                binding.sourceKind != source.handle.kind ||
                !bindingActive(binding) ||
                source.blockedUntilNeutral[bindingIndex] ||
                bindingNeutral(binding, source.values[bindingIndex])) {
                continue;
            }
            const auto sequence = source.lastMeaningfulSequence[bindingIndex];
            if (!found || sequence > newestSequence) {
                found = true;
                newestSequence = sequence;
                candidate = {
                    true,
                    static_cast<std::uint8_t>(sourceIndex),
                    static_cast<std::uint8_t>(bindingIndex),
                    0U,
                };
            }
        }
    }
    if (found) {
        axisOwners_[axisIndex] = candidate;
    }
}

void InputRouter::clearSemanticState() noexcept {
    for (std::size_t word = 0U; word < digitalBitWordCount; ++word) {
        pendingReleasedBits_[word] |= heldBits_[word];
    }
    heldBits_.fill(0U);
    axisOwners_.fill({});
}

void InputRouter::recomputeHeldAfterSourceChange() noexcept {
    for (std::size_t index = 0U; index < digitalActionCount; ++index) {
        const auto action = static_cast<DigitalAction>(index);
        const auto before = bitSet(heldBits_, action);
        const auto after = digitalHeld(action);
        if (before && !after) {
            setBit(pendingReleasedBits_, action, true);
        }
        setBit(heldBits_, action, after);
    }
}

} // namespace airfix::input
