#import "AirfixRenderSettingsCoordinator.h"

#import "AirfixIOSRenderSettingsStore.h"
#import "AirfixMetalRenderer.h"

#include "airfix/settings/RenderPresentationPersistenceGate.hpp"

#include <optional>

namespace {

constexpr std::uint64_t kImmediateStaleReprepareAttempts = 3U;

} // namespace

@interface AirfixRenderSettingsCoordinator () {
    __weak AirfixMetalRenderer* _renderer;
    __strong AirfixIOSRenderSettingsStore* _store;
    dispatch_queue_t _preparationQueue;
    airfix::settings::RenderPresentationPersistenceGate _gate;
    airfix::render::RenderPresentationSettings _persistentBase;
    airfix::render::RenderPresentationSettingsOverride
        _sessionOverrides;
    std::optional<airfix::render::RenderPresentationSettings>
        _pendingPersistentBase;
    std::optional<
        airfix::settings::RenderPresentationPersistenceTicket>
        _activeTicket;
    BOOL _started;
    BOOL _loadCompleted;
    BOOL _startupResolved;
    BOOL _persistenceBlocked;
    BOOL _preparationDispatched;
}

- (void)beginCandidate:
    (const airfix::render::RenderPresentationSettings&)persistentBase
    alreadyDurable:(BOOL)alreadyDurable;
- (void)launchPreparation:
    (const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket;
- (void)finishPreparation:
    (AirfixPreparedMetalPresentation*)prepared
    ticket:(const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket;
- (void)publishPrepared:
    (AirfixPreparedMetalPresentation*)prepared
    ticket:(const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket;
- (void)finishCurrentAndStartPending;
@end

@implementation AirfixRenderSettingsCoordinator

- (instancetype)initWithRenderer:(AirfixMetalRenderer*)renderer {
    NSParameterAssert(renderer != nil);
    self = [super init];
    if (self != nil) {
        _renderer = renderer;
        _store = [[AirfixIOSRenderSettingsStore alloc] init];
        _preparationQueue = dispatch_queue_create(
            "com.tryk016.airfixdogfighter.render-settings-prepare",
            DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)start {
    NSAssert(NSThread.isMainThread,
        @"Render settings startup is main-thread confined");
    if (_started) {
        return;
    }
    _started = YES;

    __weak AirfixRenderSettingsCoordinator* weakSelf = self;
    [_store loadWithCompletion:
        ^(const airfix::ios::RenderSettingsLoadOutcome outcome) {
            AirfixRenderSettingsCoordinator* strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf->_loadCompleted) {
                return;
            }
            strongSelf->_loadCompleted = YES;
            strongSelf->_persistentBase = outcome.result.settings;
            strongSelf->_persistenceBlocked =
                outcome.error !=
                    airfix::ios::RenderSettingsStorageError::none ||
                outcome.result.persistenceBlocked;

            const auto resolved =
                airfix::render::resolveRenderPresentationSettings(
                    strongSelf->_persistentBase,
                    strongSelf->_sessionOverrides);
            AirfixMetalRenderer* renderer = strongSelf->_renderer;
            if (!resolved.accepted() || renderer == nil) {
                strongSelf->_startupResolved = YES;
                return;
            }
            if ([renderer renderPresentationSettings] ==
                resolved.settings) {
                strongSelf->_startupResolved = YES;
                [strongSelf finishCurrentAndStartPending];
                return;
            }
            [strongSelf
                beginCandidate:strongSelf->_persistentBase
                alreadyDurable:YES];
        }];
}

- (void)requestPersistentSettings:
    (const airfix::render::RenderPresentationSettings&)settings {
    NSAssert(NSThread.isMainThread,
        @"Render settings requests are main-thread confined");
    if (airfix::render::validateRenderPresentationSettings(settings)
            .has_value()) {
        return;
    }
    if (!_loadCompleted || _gate.busy()) {
        _pendingPersistentBase = settings;
        return;
    }
    if (_persistenceBlocked) {
        return;
    }
    [self beginCandidate:settings alreadyDurable:NO];
}

- (void)notifyPresentationSurfaceAvailable {
    NSAssert(NSThread.isMainThread,
        @"Render settings surface notifications are main-thread confined");
    if (!_gate.busy() || !_activeTicket.has_value() ||
        _preparationDispatched ||
        !_gate.isCurrent(
            *_activeTicket,
            airfix::settings::
                RenderPresentationPersistencePhase::preparing)) {
        return;
    }
    [self launchPreparation:*_activeTicket];
}

- (BOOL)readyForPresentation {
    NSAssert(NSThread.isMainThread,
        @"Render settings readiness is main-thread confined");
    return _loadCompleted && _startupResolved;
}

- (BOOL)persistenceAvailable {
    NSAssert(NSThread.isMainThread,
        @"Render settings availability is main-thread confined");
    return _loadCompleted && !_persistenceBlocked;
}

- (airfix::render::RenderPresentationSettings)
    persistentSettings {
    NSAssert(NSThread.isMainThread,
        @"Persistent render settings are main-thread confined");
    return _persistentBase;
}

- (void)beginCandidate:
    (const airfix::render::RenderPresentationSettings&)persistentBase
    alreadyDurable:(BOOL)alreadyDurable {
    NSAssert(NSThread.isMainThread,
        @"Render settings publication gate is main-thread confined");
    const auto ticket = _gate.begin(
        persistentBase,
        _sessionOverrides,
        alreadyDurable == YES);
    if (!ticket.has_value()) {
        if (!alreadyDurable) {
            _pendingPersistentBase = persistentBase;
        }
        return;
    }
    _activeTicket = ticket;
    [self launchPreparation:*ticket];
}

- (void)launchPreparation:
    (const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket {
    AirfixMetalRenderer* renderer = _renderer;
    if (renderer == nil ||
        !_gate.isCurrent(
            ticket,
            airfix::settings::
                RenderPresentationPersistencePhase::preparing)) {
        return;
    }

    NSError* captureError = nil;
    AirfixMetalPresentationRequest* request =
        [renderer
            captureRenderPresentationRequest:
                ticket.effectiveSettings
            error:&captureError];
    (void)captureError;
    if (request == nil) {
        _preparationDispatched = NO;
        return;
    }

    _preparationDispatched = YES;
    __weak AirfixRenderSettingsCoordinator* weakSelf = self;
    dispatch_async(_preparationQueue, ^{
        NSError* preparationError = nil;
        AirfixPreparedMetalPresentation* prepared =
            [renderer
                prepareCapturedRenderPresentationRequest:request
                error:&preparationError];
        (void)preparationError;
        dispatch_async(dispatch_get_main_queue(), ^{
            AirfixRenderSettingsCoordinator* strongSelf = weakSelf;
            if (strongSelf == nil) {
                return;
            }
            strongSelf->_preparationDispatched = NO;
            if (!strongSelf->_gate.isCurrent(
                    ticket,
                    airfix::settings::
                        RenderPresentationPersistencePhase::
                            preparing)) {
                return;
            }
            if (prepared == nil) {
                return;
            }
            [strongSelf finishPreparation:prepared
                                   ticket:ticket];
        });
    });
}

- (void)finishPreparation:
    (AirfixPreparedMetalPresentation*)prepared
    ticket:(const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket {
    const auto advance = _gate.preparationSucceeded(ticket);
    if (advance ==
        airfix::settings::
            RenderPresentationPersistenceAdvance::rejected) {
        return;
    }
    if (advance ==
        airfix::settings::
            RenderPresentationPersistenceAdvance::commit) {
        [self publishPrepared:prepared ticket:ticket];
        return;
    }

    __weak AirfixRenderSettingsCoordinator* weakSelf = self;
    [_store
        saveSettings:ticket.persistentBase
        completion:
            ^(const airfix::ios::RenderSettingsSaveOutcome outcome) {
                AirfixRenderSettingsCoordinator* strongSelf =
                    weakSelf;
                if (strongSelf == nil ||
                    !strongSelf->_gate.isCurrent(
                        ticket,
                        airfix::settings::
                            RenderPresentationPersistencePhase::
                                saving)) {
                    return;
                }
                if (!outcome.durable ||
                    !strongSelf->_gate.saveSucceeded(ticket)) {
                    (void)strongSelf->_gate.abandon(ticket);
                    strongSelf->_activeTicket.reset();
                    [strongSelf finishCurrentAndStartPending];
                    return;
                }
                strongSelf->_persistentBase =
                    ticket.persistentBase;
                [strongSelf publishPrepared:prepared
                                     ticket:ticket];
            }];
}

- (void)publishPrepared:
    (AirfixPreparedMetalPresentation*)prepared
    ticket:(const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket {
    AirfixMetalRenderer* renderer = _renderer;
    NSError* publicationError = nil;
    if (renderer != nil &&
        [renderer publishPreparedRenderPresentation:prepared
                                             error:&publicationError]) {
        (void)publicationError;
        if (!_gate.consumeCommitted(ticket)) {
            return;
        }
        _activeTicket.reset();
        _startupResolved = YES;
        [self finishCurrentAndStartPending];
        return;
    }
    (void)publicationError;

    const auto retry = _gate.retryAfterStaleCommit(ticket);
    if (!retry.has_value()) {
        return;
    }
    _activeTicket = retry;
    if (retry->preparationAttempt <=
        kImmediateStaleReprepareAttempts) {
        [self launchPreparation:*retry];
    }
}

- (void)finishCurrentAndStartPending {
    if (_gate.busy() || !_loadCompleted ||
        !_pendingPersistentBase.has_value()) {
        return;
    }
    const auto pending = *_pendingPersistentBase;
    _pendingPersistentBase.reset();
    if (_persistenceBlocked) {
        return;
    }
    [self beginCandidate:pending alreadyDurable:NO];
}

@end
