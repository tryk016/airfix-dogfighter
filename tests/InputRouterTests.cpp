#include "airfix/input/InputRouter.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::Binding;
using airfix::input::BindingTable;
using airfix::input::ControlId;
using airfix::input::DigitalAction;
using airfix::input::InputContext;
using airfix::input::InputFrame;
using airfix::input::InputRouter;
using airfix::input::PhysicalEvent;
using airfix::input::SourceHandle;
using airfix::input::SourceKind;
using airfix::input::gameplayContext;
using airfix::input::menuContext;
using airfix::input::q15Zero;

constexpr ControlId fireControl{1U};
constexpr ControlId bankControl{2U};
constexpr ControlId weaponControl{3U};
constexpr SourceHandle touchOne{SourceKind::touch, 1U};
constexpr SourceHandle controllerOne{SourceKind::controller, 1U};
constexpr SourceHandle controllerTwo{SourceKind::controller, 2U};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] BindingTable digitalBindings() {
    BindingTable table;
    require(table.add(Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::combatPrimaryFire, gameplayContext)),
        "failed to add touch fire binding");
    require(table.add(Binding::digital(SourceKind::controller, fireControl,
        DigitalAction::combatPrimaryFire, gameplayContext)),
        "failed to add controller fire binding");
    return table;
}

[[nodiscard]] BindingTable axisBindings() {
    BindingTable table;
    require(table.add(Binding::analog(SourceKind::controller, bankControl,
        AnalogAxis::flightBank, gameplayContext,
        airfix::input::PhysicalEventKind::analog,
        airfix::input::q15One, 3000)),
        "failed to add bank binding");
    return table;
}

void testPressHeldRelease() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 100U, touchOne, fireControl, true)), "press was rejected");

    const auto pressed = router.tick(10U);
    require(pressed.pressed(DigitalAction::combatPrimaryFire),
        "press edge missing");
    require(pressed.held(DigitalAction::combatPrimaryFire),
        "press did not establish held state");
    require(!pressed.released(DigitalAction::combatPrimaryFire),
        "press generated a release");

    const auto held = router.tick(11U);
    require(!held.pressed(DigitalAction::combatPrimaryFire),
        "held input repeated a press edge");
    require(held.held(DigitalAction::combatPrimaryFire),
        "held input was lost without an event");

    require(router.enqueue(PhysicalEvent::button(
        2U, 200U, touchOne, fireControl, false)), "release was rejected");
    const auto released = router.tick(12U);
    require(released.released(DigitalAction::combatPrimaryFire),
        "release edge missing");
    require(!released.held(DigitalAction::combatPrimaryFire),
        "release left the action held");
}

void testTapBetweenTicksAndSequenceOrdering() {
    InputRouter router(digitalBindings());
    // Delivery order and timestamp order are intentionally the opposite of the
    // authoritative sequence order.
    require(router.enqueue(PhysicalEvent::button(
        2U, 10U, touchOne, fireControl, false)), "tap release was rejected");
    require(router.enqueue(PhysicalEvent::button(
        1U, 900U, touchOne, fireControl, true)), "tap press was rejected");

    const auto frame = router.tick(20U);
    require(frame.pressed(DigitalAction::combatPrimaryFire),
        "between-tick tap lost its press edge");
    require(frame.released(DigitalAction::combatPrimaryFire),
        "between-tick tap lost its release edge");
    require(!frame.held(DigitalAction::combatPrimaryFire),
        "completed tap remained held");
}

void testMultipleSourcesUseLogicalOr() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)), "touch press was rejected");
    require(router.tick(1U).pressed(DigitalAction::combatPrimaryFire),
        "first source did not press action");

    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, controllerOne, fireControl, true)),
        "controller press was rejected");
    const auto secondPress = router.tick(2U);
    require(secondPress.held(DigitalAction::combatPrimaryFire),
        "second source did not preserve held state");
    require(!secondPress.pressed(DigitalAction::combatPrimaryFire),
        "second source duplicated aggregate press edge");

    require(router.enqueue(PhysicalEvent::button(
        3U, 0U, touchOne, fireControl, false)), "touch release was rejected");
    const auto firstRelease = router.tick(3U);
    require(firstRelease.held(DigitalAction::combatPrimaryFire),
        "one source released an action held by another");
    require(!firstRelease.released(DigitalAction::combatPrimaryFire),
        "one source generated an aggregate release too early");

    require(router.enqueue(PhysicalEvent::button(
        4U, 0U, controllerOne, fireControl, false)),
        "controller release was rejected");
    const auto finalRelease = router.tick(4U);
    require(finalRelease.released(DigitalAction::combatPrimaryFire),
        "last source did not generate aggregate release");
}

void testAxisOwnershipAndDrift() {
    InputRouter router(axisBindings());
    require(router.enqueue(PhysicalEvent::axis(
        1U, 0U, controllerOne, bankControl, 20000)),
        "first axis event was rejected");
    require(router.tick(1U).analog(AnalogAxis::flightBank) == 20000,
        "first meaningful source did not own axis");

    require(router.enqueue(PhysicalEvent::axis(
        2U, 0U, controllerTwo, bankControl, 1000)),
        "drift event was rejected");
    require(router.tick(2U).analog(AnalogAxis::flightBank) == 20000,
        "idle drift stole axis ownership");

    require(router.enqueue(PhysicalEvent::axis(
        3U, 0U, controllerTwo, bankControl, 25000)),
        "meaningful takeover was rejected");
    require(router.tick(3U).analog(AnalogAxis::flightBank) == 25000,
        "latest meaningful source did not claim axis");

    require(router.enqueue(PhysicalEvent::axis(
        4U, 0U, controllerOne, bankControl, 2000)),
        "old-source drift event was rejected");
    require(router.tick(4U).analog(AnalogAxis::flightBank) == 25000,
        "old-source drift reclaimed axis");

    require(router.enqueue(PhysicalEvent::axis(
        5U, 0U, controllerTwo, bankControl, q15Zero)),
        "owner neutral event was rejected");
    require(router.tick(5U).analog(AnalogAxis::flightBank) == q15Zero,
        "neutral owner produced non-neutral output");
}

void testAxisFallsBackToStillMeaningfulSource() {
    InputRouter router(axisBindings());
    require(router.enqueue(PhysicalEvent::axis(
        1U, 0U, controllerOne, bankControl, 20000)),
        "fallback source event was rejected");
    static_cast<void>(router.tick(1U));
    require(router.enqueue(PhysicalEvent::axis(
        2U, 0U, controllerTwo, bankControl, 25000)),
        "takeover event was rejected");
    require(router.tick(2U).analog(AnalogAxis::flightBank) == 25000,
        "second source did not take ownership");
    require(router.enqueue(PhysicalEvent::axis(
        3U, 0U, controllerTwo, bankControl, q15Zero)),
        "owner neutral event was rejected");
    require(router.tick(3U).analog(AnalogAxis::flightBank) == q15Zero,
        "neutral hysteresis emitted stale owner input");
    require(router.tick(4U).analog(AnalogAxis::flightBank) == 20000,
        "axis did not fall back to a still-meaningful source");
}

void testTouchReclaimsControllerAxisAndDriftCannotStealBack() {
    BindingTable table;
    require(table.add(Binding::analog(SourceKind::controller, bankControl,
        AnalogAxis::flightBank, gameplayContext,
        airfix::input::PhysicalEventKind::analog,
        airfix::input::q15One, 3000)),
        "failed to add controller bank binding");
    require(table.add(Binding::analog(SourceKind::touch, bankControl,
        AnalogAxis::flightBank, gameplayContext,
        airfix::input::PhysicalEventKind::analog,
        airfix::input::q15One, 3000)),
        "failed to add touch bank binding");
    InputRouter router(table);

    require(router.enqueue(PhysicalEvent::axis(
        1U, 0U, controllerOne, bankControl, 20000)),
        "controller bank event was rejected");
    require(router.tick(1U).analog(AnalogAxis::flightBank) == 20000,
        "controller did not initially own bank");

    require(router.enqueue(PhysicalEvent::axis(
        2U, 0U, touchOne, bankControl, -25000)),
        "touch bank event was rejected");
    require(router.tick(2U).analog(AnalogAxis::flightBank) == -25000,
        "meaningful touch did not reclaim bank");

    require(router.enqueue(PhysicalEvent::axis(
        3U, 0U, controllerOne, bankControl, 1000)),
        "controller drift event was rejected");
    require(router.tick(3U).analog(AnalogAxis::flightBank) == -25000,
        "sub-threshold controller drift stole bank back from touch");

    require(router.enqueue(PhysicalEvent::axis(
        4U, 0U, touchOne, bankControl, q15Zero)),
        "touch neutral event was rejected");
    require(router.tick(4U).analog(AnalogAxis::flightBank) == q15Zero,
        "neutral touch owner emitted stale bank");
    require(router.tick(5U).analog(AnalogAxis::flightBank) == q15Zero,
        "controller drift became a meaningful fallback owner");
}

void testPauseAndFireCanShareOneInputFrame() {
    InputRouter router;
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, airfix::input::controls::touch::primaryFire, true)),
        "same-frame primary-fire press was rejected");
    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, touchOne, airfix::input::controls::touch::pause, true)),
        "same-frame pause press was rejected");

    const auto pressed = router.tick(1U);
    require(pressed.pressed(DigitalAction::combatPrimaryFire) &&
            pressed.held(DigitalAction::combatPrimaryFire),
        "same-frame primary fire was not preserved");
    require(pressed.pressed(DigitalAction::globalPause) &&
            pressed.held(DigitalAction::globalPause),
        "same-frame pause was not preserved");

    require(router.enqueue(PhysicalEvent::button(
        3U, 0U, touchOne, airfix::input::controls::touch::primaryFire, false)),
        "same-frame primary-fire release was rejected");
    require(router.enqueue(PhysicalEvent::button(
        4U, 0U, touchOne, airfix::input::controls::touch::pause, false)),
        "same-frame pause release was rejected");
    const auto released = router.tick(2U);
    require(released.released(DigitalAction::combatPrimaryFire) &&
            released.released(DigitalAction::globalPause),
        "same-frame releases were not preserved");
}

void testCancelAndDisconnect() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, controllerOne, fireControl, true)), "press was rejected");
    static_cast<void>(router.tick(1U));

    router.cancelSource(controllerOne);
    const auto disconnected = router.tick(2U);
    require(disconnected.released(DigitalAction::combatPrimaryFire),
        "disconnect did not synthesize release");
    require(!disconnected.held(DigitalAction::combatPrimaryFire),
        "disconnect left action held");

    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, controllerOne, fireControl, true)),
        "queued press before cancel was rejected");
    router.cancelSource(controllerOne);
    const auto canceledPending = router.tick(3U);
    require(!canceledPending.pressed(DigitalAction::combatPrimaryFire),
        "cancelSource replayed a queued press");
}

void testCancelPreservesAggregateHeldAcrossPendingSource() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, controllerOne, fireControl, true)),
        "first source press was rejected");
    static_cast<void>(router.tick(1U));
    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, controllerTwo, fireControl, true)),
        "pending replacement source press was rejected");

    router.cancelSource(controllerOne);
    const auto handoff = router.tick(2U);
    require(handoff.held(DigitalAction::combatPrimaryFire),
        "cancel lost an action already held by a pending source");
    require(!handoff.pressed(DigitalAction::combatPrimaryFire) &&
        !handoff.released(DigitalAction::combatPrimaryFire),
        "aggregate OR handoff emitted a false edge pair");
}

void testReconnectRequiresNeutral() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, controllerOne, fireControl, true)),
        "initial controller press was rejected");
    static_cast<void>(router.tick(1U));
    router.cancelSource(controllerOne);
    require(router.tick(2U).released(DigitalAction::combatPrimaryFire),
        "controller loss did not release held input");

    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, controllerTwo, fireControl, true)),
        "reconnected held state was rejected");
    require(!router.tick(3U).held(DigitalAction::combatPrimaryFire),
        "reconnect restored a held action before neutral");
    require(router.enqueue(PhysicalEvent::button(
        3U, 0U, controllerTwo, fireControl, false)),
        "reconnected neutral state was rejected");
    require(!router.tick(4U).held(DigitalAction::combatPrimaryFire),
        "first reconnect neutral tick admitted input");
    static_cast<void>(router.tick(5U));
    require(router.enqueue(PhysicalEvent::button(
        4U, 0U, controllerTwo, fireControl, true)),
        "post-neutral reconnect press was rejected");
    require(router.tick(6U).pressed(DigitalAction::combatPrimaryFire),
        "controller did not recover after reconnect neutral gate");
}

void testResetNeutralGate() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)), "initial press was rejected");
    static_cast<void>(router.tick(1U));

    router.lifecycleReset();
    const auto firstNeutral = router.tick(2U);
    require(firstNeutral.released(DigitalAction::combatPrimaryFire),
        "reset did not synthesize release");
    require(!router.neutralGateOpen(),
        "neutral gate opened after only one neutral tick");
    static_cast<void>(router.tick(3U));
    require(router.neutralGateOpen(),
        "neutral gate did not open after two neutral ticks");

    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)),
        "fresh post-reset sequence was rejected");
    require(router.tick(4U).pressed(DigitalAction::combatPrimaryFire),
        "post-gate input was not admitted");

    router.reset();
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)),
        "held input during reset was rejected");
    require(!router.tick(5U).held(DigitalAction::combatPrimaryFire),
        "closed neutral gate leaked held input");
    require(router.neutralGateTicks() == 0U,
        "non-neutral tick advanced neutral gate");
    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, touchOne, fireControl, false)),
        "neutral event during reset was rejected");
    static_cast<void>(router.tick(6U));
    require(!router.neutralGateOpen(),
        "neutral gate opened after one post-input neutral tick");
    static_cast<void>(router.tick(7U));
    require(router.neutralGateOpen(),
        "neutral gate did not reopen after two neutral ticks");
}

void testLatchedThrottleDoesNotBlockNeutralGate() {
    InputRouter router;
    router.lifecycleReset();
    require(router.enqueue(PhysicalEvent::axis(
        1U, 0U, touchOne, airfix::input::controls::touch::throttleSet,
        17000)), "latched throttle state was rejected after reset");

    require(router.tick(1U).analog(AnalogAxis::flightThrottleSet) == 0,
        "latched throttle bypassed the first neutral-gate tick");
    require(!router.neutralGateOpen(),
        "latched throttle opened the gate before two safe ticks");
    require(router.tick(2U).analog(AnalogAxis::flightThrottleSet) == 0,
        "latched throttle acquired ownership before the gate opened");
    require(router.neutralGateOpen(),
        "absolute throttle target blocked the global neutral gate");
    require(router.tick(3U).analog(AnalogAxis::flightThrottleSet) == 17000,
        "latched throttle target was not restored after the gate opened");

    router.setContext(InputContext::menu);
    require(router.tick(4U).analog(AnalogAxis::flightThrottleSet) == 0,
        "gameplay throttle leaked into menu context");
    router.setContext(InputContext::gameplay);
    require(router.tick(5U).analog(AnalogAxis::flightThrottleSet) == 17000,
        "absolute throttle target was blocked across a context round-trip");
}

void testContextSwitchNeutralizesGameplay() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)), "press was rejected");
    static_cast<void>(router.tick(1U));

    router.setContext(InputContext::menu);
    const auto menu = router.tick(2U);
    require(menu.released(DigitalAction::combatPrimaryFire),
        "blocking context did not synthesize gameplay release");
    require(!menu.held(DigitalAction::combatPrimaryFire),
        "gameplay remained held in menu context");

    router.setContext(InputContext::gameplay);
    require(!router.tick(3U).held(DigitalAction::combatPrimaryFire),
        "held physical input was restored after context switch");
    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, touchOne, fireControl, false)), "neutral event was rejected");
    static_cast<void>(router.tick(4U));
    require(router.enqueue(PhysicalEvent::button(
        3U, 0U, touchOne, fireControl, true)), "re-press was rejected");
    require(router.tick(5U).pressed(DigitalAction::combatPrimaryFire),
        "neutral then re-press did not restore gameplay input");
}

void testContextSwitchConsumesPendingInputInOldContext() {
    BindingTable table;
    require(table.add(Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::globalBack, gameplayContext)),
        "failed to add gameplay binding");
    require(table.add(Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::uiCancel, menuContext)),
        "failed to add menu binding");
    InputRouter router(table);
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)),
        "pending pre-transition press was rejected");

    router.setContext(InputContext::menu);
    const auto menu = router.tick(1U);
    require(!menu.pressed(DigitalAction::uiCancel) &&
        !menu.held(DigitalAction::uiCancel),
        "pre-transition press leaked into the new context");

    require(router.enqueue(PhysicalEvent::button(
        2U, 0U, touchOne, fireControl, false)),
        "post-transition neutral event was rejected");
    static_cast<void>(router.tick(2U));
    require(router.enqueue(PhysicalEvent::button(
        3U, 0U, touchOne, fireControl, true)),
        "post-neutral menu press was rejected");
    require(router.tick(3U).pressed(DigitalAction::uiCancel),
        "new-context action did not recover after neutral");
}

void testContextSwitchDropsRetainedUnsampledPress() {
    InputRouter router(digitalBindings());
    require(router.enqueue(PhysicalEvent::button(
        1U, 0U, touchOne, fireControl, true)),
        "pre-cancel pending press was rejected");
    router.cancelSource(controllerOne);
    router.setContext(InputContext::menu);
    const auto menu = router.tick(1U);
    require(!menu.pressed(DigitalAction::combatPrimaryFire) &&
        !menu.released(DigitalAction::combatPrimaryFire) &&
        !menu.held(DigitalAction::combatPrimaryFire),
        "retained old-context press leaked across transition");
}

void testQueueLimitIsStrict() {
    InputRouter router(digitalBindings());
    for (std::size_t index = 0U; index < InputRouter::queueCapacity; ++index) {
        require(router.enqueue(PhysicalEvent::button(
            static_cast<std::uint64_t>(index + 1U), 0U, touchOne,
            fireControl, (index % 2U) == 0U)),
            "event inside queue capacity was rejected");
    }
    require(router.queuedEventCount() == InputRouter::queueCapacity,
        "queue did not reach its documented capacity");
    require(!router.enqueue(PhysicalEvent::button(
        InputRouter::queueCapacity + 1U, 0U, touchOne, fireControl, true)),
        "event beyond queue capacity was accepted");
    require(router.rejectedEventCount() == 1U,
        "queue overflow was not reported");
}

void testUnboundEventsDoNotConsumeSourceCapacity() {
    InputRouter router(digitalBindings());
    for (std::size_t index = 0U; index < InputRouter::sourceCapacity + 1U; ++index) {
        const SourceHandle unbound{SourceKind::synthetic,
            static_cast<std::uint16_t>(index + 1U)};
        require(!router.enqueue(PhysicalEvent::button(
            static_cast<std::uint64_t>(index + 1U), 0U, unbound,
            ControlId{99U}, true)),
            "unbound event was accepted");
    }
    require(router.queuedEventCount() == 0U,
        "unbound events consumed queue capacity");
    require(router.enqueue(PhysicalEvent::button(
        100U, 0U, touchOne, fireControl, true)),
        "unbound sources exhausted valid source capacity");
}

void testTimestampDoesNotAffectFrame() {
    InputRouter earlyTimestamp(digitalBindings());
    InputRouter lateTimestamp(digitalBindings());
    require(earlyTimestamp.enqueue(PhysicalEvent::button(
        1U, 1U, touchOne, fireControl, true)), "early press rejected");
    require(earlyTimestamp.enqueue(PhysicalEvent::button(
        2U, 2U, touchOne, fireControl, false)), "early release rejected");
    require(lateTimestamp.enqueue(PhysicalEvent::button(
        1U, 999999U, touchOne, fireControl, true)), "late press rejected");
    require(lateTimestamp.enqueue(PhysicalEvent::button(
        2U, 7U, touchOne, fireControl, false)), "late release rejected");

    const InputFrame early = earlyTimestamp.tick(42U);
    const InputFrame late = lateTimestamp.tick(42U);
    require(early == late, "platform timestamp changed deterministic InputFrame");
}

void testDiscreteWeaponSelection() {
    BindingTable table;
    require(table.add(Binding::weaponSelection(SourceKind::touch,
        weaponControl, gameplayContext)), "weapon binding rejected");
    InputRouter router(table);
    require(router.enqueue(PhysicalEvent::selectWeapon(
        1U, 123U, touchOne, weaponControl, 4U)),
        "weapon selection event rejected");
    const auto selected = router.tick(1U);
    require(selected.hasWeaponSelection() && selected.weaponSelection == 4U,
        "weapon selection missing from frame");
    require(!router.tick(2U).hasWeaponSelection(),
        "weapon selection incorrectly persisted into next frame");
    require(router.enqueue(PhysicalEvent::selectWeapon(
        2U, 124U, touchOne, weaponControl,
        airfix::input::weaponSlotCount)),
        "out-of-range weapon event was rejected before bounded processing");
    require(!router.tick(3U).hasWeaponSelection(),
        "out-of-range weapon selection reached the deterministic frame");
}

void testDefaultBindingsCoverNativeV1Surface() {
    constexpr SourceHandle touch{SourceKind::touch, 7U};
    InputRouter touchRouter;
    std::uint64_t sequence = 1U;

    const auto enqueueTouchAxis = [&](const ControlId control,
                                      const airfix::input::Q15 value) {
        require(touchRouter.enqueue(PhysicalEvent::axis(
            sequence++, 0U, touch, control, value)),
            "default touch axis event was rejected");
    };
    const auto enqueueTouchButton = [&](const ControlId control) {
        require(touchRouter.enqueue(PhysicalEvent::button(
            sequence++, 0U, touch, control, true)),
            "default touch button event was rejected");
    };

    enqueueTouchAxis(airfix::input::controls::touch::bank, 12000);
    enqueueTouchAxis(airfix::input::controls::touch::pitch, -13000);
    enqueueTouchAxis(airfix::input::controls::touch::throttleSet, 17000);
    enqueueTouchAxis(airfix::input::controls::touch::lookX, 9000);
    enqueueTouchAxis(airfix::input::controls::touch::lookY, -8000);
    enqueueTouchButton(airfix::input::controls::touch::primaryFire);
    enqueueTouchButton(airfix::input::controls::touch::secondaryFire);
    enqueueTouchButton(airfix::input::controls::touch::weaponNext);
    enqueueTouchButton(airfix::input::controls::touch::rearView);
    enqueueTouchButton(airfix::input::controls::touch::cameraCycle);
    enqueueTouchButton(airfix::input::controls::touch::cameraRecenter);
    enqueueTouchButton(airfix::input::controls::touch::missionStatus);
    enqueueTouchButton(airfix::input::controls::touch::pause);
    require(touchRouter.enqueue(PhysicalEvent::selectWeapon(
        sequence++, 0U, touch,
        airfix::input::controls::touch::weaponSelection, 6U)),
        "default touch weapon selection was rejected");

    const auto touchFrame = touchRouter.tick(1U);
    require(touchFrame.analog(AnalogAxis::flightBank) == 12000 &&
            touchFrame.analog(AnalogAxis::flightPitch) == -13000 &&
            touchFrame.analog(AnalogAxis::flightThrottleSet) == 17000 &&
            touchFrame.analog(AnalogAxis::cameraLookX) == 9000 &&
            touchFrame.analog(AnalogAxis::cameraLookY) == -8000,
        "default touch axes did not reach their semantic targets");
    for (const auto action : {
             DigitalAction::combatPrimaryFire,
             DigitalAction::combatSecondaryFire,
             DigitalAction::combatWeaponNext,
             DigitalAction::cameraRearView,
             DigitalAction::cameraCycle,
             DigitalAction::cameraRecenter,
             DigitalAction::missionStatus,
             DigitalAction::globalPause,
         }) {
        require(touchFrame.pressed(action) && touchFrame.held(action),
            "default touch button did not reach its semantic target");
    }
    require(touchFrame.hasWeaponSelection() &&
            touchFrame.weaponSelection == 6U,
        "default touch direct weapon selection was not transported");

    InputRouter touchThrottleRouter;
    require(touchThrottleRouter.enqueue(PhysicalEvent::button(
        1U, 0U, touch, airfix::input::controls::touch::throttleIncrease,
        true)), "touch throttle-increase press was rejected");
    require(touchThrottleRouter.tick(1U).analog(
            AnalogAxis::flightThrottleDelta) == airfix::input::q15One,
        "touch throttle-increase did not produce positive delta");
    require(touchThrottleRouter.enqueue(PhysicalEvent::button(
        2U, 0U, touch, airfix::input::controls::touch::throttleIncrease,
        false)), "touch throttle-increase release was rejected");
    require(touchThrottleRouter.enqueue(PhysicalEvent::button(
        3U, 0U, touch, airfix::input::controls::touch::throttleDecrease,
        true)), "touch throttle-decrease press was rejected");
    require(touchThrottleRouter.tick(2U).analog(
            AnalogAxis::flightThrottleDelta) == airfix::input::q15Min,
        "touch throttle-decrease did not produce negative delta");

    InputRouter menuRouter;
    menuRouter.setContext(InputContext::menu);
    require(menuRouter.enqueue(PhysicalEvent::button(
        1U, 0U, controllerOne,
        airfix::input::controls::controller::facePrimary, true)),
        "controller menu confirm was rejected");
    require(menuRouter.enqueue(PhysicalEvent::button(
        2U, 0U, controllerOne,
        airfix::input::controls::controller::faceSecondary, true)),
        "controller menu cancel was rejected");
    require(menuRouter.enqueue(PhysicalEvent::button(
        3U, 0U, controllerOne,
        airfix::input::controls::controller::leftShoulder, true)),
        "controller previous-tab event was rejected");
    require(menuRouter.enqueue(PhysicalEvent::button(
        4U, 0U, controllerOne,
        airfix::input::controls::controller::rightShoulder, true)),
        "controller next-tab event was rejected");
    const auto menuFrame = menuRouter.tick(1U);
    require(menuFrame.pressed(DigitalAction::uiConfirm) &&
            menuFrame.pressed(DigitalAction::uiCancel) &&
            menuFrame.pressed(DigitalAction::uiTabPrevious) &&
            menuFrame.pressed(DigitalAction::uiTabNext),
        "default controller menu actions are incomplete");

    InputRouter menuDpadRouter;
    menuDpadRouter.setContext(InputContext::menu);
    require(menuDpadRouter.enqueue(PhysicalEvent::button(
        1U, 0U, controllerOne,
        airfix::input::controls::controller::dpadUp, true)),
        "controller menu D-pad up was rejected");
    const auto menuDpadUp = menuDpadRouter.tick(1U);
    require(menuDpadUp.analog(AnalogAxis::uiNavigateY) ==
                airfix::input::q15One &&
            menuDpadUp.analog(AnalogAxis::flightThrottleDelta) == q15Zero,
        "controller menu D-pad up leaked into gameplay or mapped downward");

    InputRouter menuDpadHorizontalRouter;
    menuDpadHorizontalRouter.setContext(InputContext::menu);
    require(menuDpadHorizontalRouter.enqueue(PhysicalEvent::button(
                1U, 0U, controllerOne,
                airfix::input::controls::controller::dpadLeft, true)),
            "controller menu D-pad left was rejected");
    const auto menuDpadLeft = menuDpadHorizontalRouter.tick(1U);
    require(
        menuDpadLeft.analog(AnalogAxis::uiNavigateX) == airfix::input::q15Min &&
            menuDpadLeft.analog(AnalogAxis::flightBank) == q15Zero,
        "controller menu D-pad left leaked into gameplay or mapped rightward");

    InputRouter modalDpadRouter;
    modalDpadRouter.setContext(InputContext::modal);
    require(modalDpadRouter.enqueue(PhysicalEvent::axis(
        1U, 0U, controllerOne,
        airfix::input::controls::controller::leftStickX, 12000)),
        "controller modal horizontal navigation was rejected");
    require(modalDpadRouter.enqueue(PhysicalEvent::button(
        2U, 0U, controllerOne,
        airfix::input::controls::controller::facePrimary, true)),
        "controller modal confirm was rejected");
    require(modalDpadRouter.enqueue(PhysicalEvent::button(
        3U, 0U, controllerOne,
        airfix::input::controls::controller::faceSecondary, true)),
        "controller modal cancel was rejected");
    require(modalDpadRouter.enqueue(PhysicalEvent::button(
        4U, 0U, controllerOne,
        airfix::input::controls::controller::leftShoulder, true)),
        "controller modal previous-tab event was rejected");
    require(modalDpadRouter.enqueue(PhysicalEvent::button(
        5U, 0U, controllerOne,
        airfix::input::controls::controller::rightShoulder, true)),
        "controller modal next-tab event was rejected");
    require(modalDpadRouter.enqueue(PhysicalEvent::button(
        6U, 0U, controllerOne,
        airfix::input::controls::controller::dpadDown, true)),
        "controller modal D-pad down was rejected");
    const auto modalDpadDown = modalDpadRouter.tick(1U);
    require(modalDpadDown.analog(AnalogAxis::uiNavigateX) == 12000 &&
            modalDpadDown.analog(AnalogAxis::uiNavigateY) ==
                airfix::input::q15Min &&
            modalDpadDown.analog(AnalogAxis::flightThrottleDelta) == q15Zero &&
            modalDpadDown.analog(AnalogAxis::flightBank) == q15Zero &&
            modalDpadDown.pressed(DigitalAction::uiConfirm) &&
            modalDpadDown.pressed(DigitalAction::uiCancel) &&
            modalDpadDown.pressed(DigitalAction::uiTabPrevious) &&
            modalDpadDown.pressed(DigitalAction::uiTabNext) &&
            !modalDpadDown.pressed(DigitalAction::globalPause) &&
            !modalDpadDown.pressed(DigitalAction::combatPrimaryFire) &&
            !modalDpadDown.pressed(DigitalAction::combatWeaponNext),
        "controller modal bindings leaked into gameplay or mapped incorrectly");
}

void testDefaultBindingsAreBounded() {
    const auto defaults = BindingTable::defaults();
    require(!defaults.empty(), "default binding table is empty");
    require(defaults.size() <= BindingTable::capacity,
        "default binding table exceeded fixed capacity");
}

void testInvalidBindingThresholdIsRejected() {
    BindingTable table;
    require(!table.add(Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::combatPrimaryFire, gameplayContext,
        airfix::input::PhysicalEventKind::digital, 0)),
        "zero digital threshold created a permanently-held binding");
}

void testInvalidBindingKindsAreRejected() {
    BindingTable table;
    auto invalidTarget = Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::combatPrimaryFire, gameplayContext);
    invalidTarget.targetKind = static_cast<airfix::input::BindingTargetKind>(255U);
    require(!table.add(invalidTarget), "invalid binding target kind was accepted");

    auto invalidPhysical = Binding::digital(SourceKind::touch, fireControl,
        DigitalAction::combatPrimaryFire, gameplayContext);
    invalidPhysical.physicalKind =
        static_cast<airfix::input::PhysicalEventKind>(255U);
    require(!table.add(invalidPhysical), "invalid physical event kind was accepted");
}

void testInvalidContextIsIgnored() {
    InputRouter router(digitalBindings());
    router.setContext(static_cast<InputContext>(255U));
    require(router.context() == InputContext::gameplay,
        "invalid context replaced the active context");
}

} // namespace

int main() {
    try {
        testPressHeldRelease();
        testTapBetweenTicksAndSequenceOrdering();
        testMultipleSourcesUseLogicalOr();
        testAxisOwnershipAndDrift();
        testAxisFallsBackToStillMeaningfulSource();
        testTouchReclaimsControllerAxisAndDriftCannotStealBack();
        testPauseAndFireCanShareOneInputFrame();
        testCancelAndDisconnect();
        testCancelPreservesAggregateHeldAcrossPendingSource();
        testReconnectRequiresNeutral();
        testResetNeutralGate();
        testLatchedThrottleDoesNotBlockNeutralGate();
        testContextSwitchNeutralizesGameplay();
        testContextSwitchConsumesPendingInputInOldContext();
        testContextSwitchDropsRetainedUnsampledPress();
        testQueueLimitIsStrict();
        testUnboundEventsDoNotConsumeSourceCapacity();
        testTimestampDoesNotAffectFrame();
        testDiscreteWeaponSelection();
        testDefaultBindingsCoverNativeV1Surface();
        testDefaultBindingsAreBounded();
        testInvalidBindingThresholdIsRejected();
        testInvalidBindingKindsAreRejected();
        testInvalidContextIsIgnored();
        std::cout << "all input router tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "input router test failure: " << error.what() << '\n';
        return 1;
    }
}
