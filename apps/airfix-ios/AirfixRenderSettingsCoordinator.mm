#import "AirfixRenderSettingsCoordinator.h"

#import "AirfixIOSRenderSettingsStore.h"
#import "AirfixMetalRenderer.h"

#include "airfix/settings/RenderPresentationPersistenceGate.hpp"
#include "airfix/settings/RenderPresentationRequestQueue.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace {

constexpr std::uint64_t kImmediateStaleReprepareAttempts = 3U;
constexpr std::uint64_t kInitialPreparationRetryMilliseconds = 50U;
constexpr std::uint64_t kMaximumPreparationRetryMilliseconds = 2'000U;

[[nodiscard]] std::uint64_t preparationRetryMilliseconds(
    const std::uint32_t failureCount) noexcept {
    const auto shift = std::min(failureCount > 0U
        ? failureCount - 1U
        : 0U, 5U);
    return std::min(
        kInitialPreparationRetryMilliseconds << shift,
        kMaximumPreparationRetryMilliseconds);
}

} // namespace

@interface AirfixRenderSettingsCoordinator () {
    __weak AirfixMetalRenderer* _renderer;
    __strong AirfixIOSRenderSettingsStore* _store;
    dispatch_queue_t _preparationQueue;
    airfix::settings::RenderPresentationPersistenceGate _gate;
    airfix::settings::RenderPresentationRequestQueue _requestQueue;
    airfix::render::RenderPresentationSettings _persistentBase;
    airfix::render::RenderPresentationSettingsOverride
        _sessionOverrides;
    __strong AirfixRenderSettingsApplyCompletion
        _pendingCompletion;
    std::optional<
        airfix::settings::RenderPresentationPersistenceTicket>
        _activeTicket;
    __strong AirfixRenderSettingsApplyCompletion
        _activeCompletion;
    std::optional<
        airfix::settings::RenderPresentationRequestTicket>
        _activeRequest;
    std::optional<
        airfix::settings::RenderPresentationPersistenceTicket>
        _scheduledRetryTicket;
    std::uint64_t _preparationRetryGeneration;
    std::uint32_t _preparationFailureCount;
    BOOL _started;
    BOOL _loadCompleted;
    BOOL _startupResolved;
    BOOL _persistenceBlocked;
    BOOL _preparationDispatched;
}

- (void)beginCandidate:
    (const airfix::render::RenderPresentationSettings&)persistentBase
    alreadyDurable:(BOOL)alreadyDurable
    request:(const std::optional<
        airfix::settings::RenderPresentationRequestTicket>&)request;
- (void)completeActiveTicket:
            (const airfix::settings::
                RenderPresentationPersistenceTicket&)ticket
    result:(AirfixRenderSettingsApplyResult)result;
- (void)completeActiveRequest:
            (const airfix::settings::
                RenderPresentationRequestTicket&)request
    result:(AirfixRenderSettingsApplyResult)result;
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
- (void)schedulePreparationRetry:
    (const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket;
- (void)finishCurrentAndStartPending;
@end

@implementation AirfixRenderSettingsCoordinator

- (void)notifyDelegateOfActiveSettings {
    NSAssert(NSThread.isMainThread,
        @"Render settings notifications are main-thread confined");
    id<AirfixRenderSettingsCoordinatorDelegate> delegate = self.delegate;
    [delegate renderSettingsCoordinatorDidPublishActiveSettings:self];
}

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
                [strongSelf notifyDelegateOfActiveSettings];
                [strongSelf finishCurrentAndStartPending];
                return;
            }
            if ([renderer renderPresentationSettings] ==
                resolved.settings) {
                strongSelf->_startupResolved = YES;
                [strongSelf notifyDelegateOfActiveSettings];
                [strongSelf finishCurrentAndStartPending];
                return;
            }
            [strongSelf
                beginCandidate:strongSelf->_persistentBase
                alreadyDurable:YES
                request:std::nullopt];
        }];
}

- (void)requestPersistentSettings:
            (const airfix::render::RenderPresentationSettings&)settings
    completion:(AirfixRenderSettingsApplyCompletion)completion {
    NSAssert(NSThread.isMainThread,
        @"Render settings requests are main-thread confined");
    NSParameterAssert(completion != nil);
    if (airfix::render::validateRenderPresentationSettings(settings)
            .has_value()) {
        completion(
            AirfixRenderSettingsApplyResultInvalidCandidate);
        return;
    }
    if (_persistenceBlocked || _renderer == nil) {
        completion(
            AirfixRenderSettingsApplyResultPersistenceUnavailable);
        return;
    }
    const auto submission = _requestQueue.submit(
        settings, _loadCompleted && !_gate.busy());
    if (submission.disposition ==
        airfix::settings::
            RenderPresentationRequestDisposition::exhausted) {
        completion(
            AirfixRenderSettingsApplyResultPersistenceUnavailable);
        return;
    }
    if (submission.disposition ==
        airfix::settings::
            RenderPresentationRequestDisposition::queued) {
        AirfixRenderSettingsApplyCompletion superseded =
            _pendingCompletion;
        _pendingCompletion = [completion copy];
        if (submission.superseded.has_value() &&
            superseded != nil) {
            superseded(
                AirfixRenderSettingsApplyResultSuperseded);
        }
        return;
    }

    if (!submission.request.has_value()) {
        completion(
            AirfixRenderSettingsApplyResultPersistenceUnavailable);
        return;
    }
    _activeRequest = submission.request;
    _activeCompletion = [completion copy];
    [self beginCandidate:settings
          alreadyDurable:NO
                 request:submission.request];
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
    if (_scheduledRetryTicket.has_value() &&
        *_scheduledRetryTicket == *_activeTicket) {
        ++_preparationRetryGeneration;
        _scheduledRetryTicket.reset();
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

- (BOOL)applying {
    NSAssert(NSThread.isMainThread,
        @"Render settings application state is main-thread confined");
    return _gate.busy() || _requestQueue.hasOutstandingWork();
}

- (airfix::render::RenderPresentationSettings)
    persistentSettings {
    NSAssert(NSThread.isMainThread,
        @"Persistent render settings are main-thread confined");
    return _persistentBase;
}

- (airfix::render::RenderPresentationSettings)
    activeSettings {
    NSAssert(NSThread.isMainThread,
        @"Active render settings are main-thread confined");
    AirfixMetalRenderer* renderer = _renderer;
    if (renderer == nil) {
        return {};
    }
    return [renderer renderPresentationSettings];
}

- (void)beginCandidate:
    (const airfix::render::RenderPresentationSettings&)persistentBase
    alreadyDurable:(BOOL)alreadyDurable
    request:(const std::optional<
        airfix::settings::RenderPresentationRequestTicket>&)request {
    NSAssert(NSThread.isMainThread,
        @"Render settings publication gate is main-thread confined");
    const auto ticket = _gate.begin(
        persistentBase,
        _sessionOverrides,
        alreadyDurable == YES);
    if (!ticket.has_value()) {
        if (!alreadyDurable && request.has_value()) {
            [self
                completeActiveRequest:*request
                result:
                    AirfixRenderSettingsApplyResultPersistenceUnavailable];
        }
        return;
    }
    _activeTicket = ticket;
    ++_preparationRetryGeneration;
    _scheduledRetryTicket.reset();
    _preparationFailureCount = 0U;
    [self launchPreparation:*ticket];
}

- (void)completeActiveTicket:
            (const airfix::settings::
                RenderPresentationPersistenceTicket&)ticket
    result:(const AirfixRenderSettingsApplyResult)result {
    NSAssert(NSThread.isMainThread,
        @"Render settings completions are main-thread confined");
    if (!_activeRequest.has_value() ||
        _activeRequest->candidate != ticket.persistentBase) {
        [self finishCurrentAndStartPending];
        return;
    }
    [self completeActiveRequest:*_activeRequest result:result];
}

- (void)completeActiveRequest:
            (const airfix::settings::
                RenderPresentationRequestTicket&)request
    result:(const AirfixRenderSettingsApplyResult)result {
    NSAssert(NSThread.isMainThread,
        @"Render settings request completions are main-thread confined");
    const auto completedRequest = request;
    if (!_activeRequest.has_value() ||
        *_activeRequest != completedRequest ||
        !_requestQueue.beginCompletion(completedRequest)) {
        return;
    }

    AirfixRenderSettingsApplyCompletion completion = nil;
    completion = _activeCompletion;
    _activeCompletion = nil;
    _activeRequest.reset();
    if (completion != nil) {
        completion(result);
    }
    if (!_requestQueue.finishCompletion(completedRequest)) {
        return;
    }
    [self finishCurrentAndStartPending];
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
        [self schedulePreparationRetry:ticket];
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
                [strongSelf schedulePreparationRetry:ticket];
                return;
            }
            strongSelf->_preparationFailureCount = 0U;
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
                    AirfixRenderSettingsApplyResult result =
                        AirfixRenderSettingsApplyResultSaveFailed;
                    switch (outcome.error) {
                    case airfix::ios::
                        RenderSettingsStorageError::storageUnavailable:
                    case airfix::ios::
                        RenderSettingsStorageError::persistenceBlocked:
                        strongSelf->_persistenceBlocked = YES;
                        result =
                            AirfixRenderSettingsApplyResultPersistenceUnavailable;
                        break;
                    case airfix::ios::
                        RenderSettingsStorageError::invalidSettings:
                        result =
                            AirfixRenderSettingsApplyResultInvalidCandidate;
                        break;
                    case airfix::ios::
                        RenderSettingsStorageError::none:
                    case airfix::ios::
                        RenderSettingsStorageError::saveFailed:
                    case airfix::ios::
                        RenderSettingsStorageError::commitUnknown:
                        break;
                    }
                    (void)strongSelf->_gate.abandon(ticket);
                    strongSelf->_activeTicket.reset();
                    [strongSelf
                        completeActiveTicket:ticket
                        result:result];
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
        [self notifyDelegateOfActiveSettings];
        [self
            completeActiveTicket:ticket
            result:AirfixRenderSettingsApplyResultApplied];
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

- (void)schedulePreparationRetry:
    (const airfix::settings::
        RenderPresentationPersistenceTicket&)ticket {
    if (_preparationDispatched ||
        !_gate.isCurrent(
            ticket,
            airfix::settings::
                RenderPresentationPersistencePhase::preparing)) {
        return;
    }
    if (_scheduledRetryTicket.has_value() &&
        *_scheduledRetryTicket == ticket) {
        return;
    }
    if (_preparationFailureCount <
        std::numeric_limits<std::uint32_t>::max()) {
        ++_preparationFailureCount;
    }
    _scheduledRetryTicket = ticket;
    const auto generation = ++_preparationRetryGeneration;
    const auto delay = preparationRetryMilliseconds(
        _preparationFailureCount);
    __weak AirfixRenderSettingsCoordinator* weakSelf = self;
    dispatch_after(
        dispatch_time(
            DISPATCH_TIME_NOW,
            static_cast<std::int64_t>(delay * NSEC_PER_MSEC)),
        dispatch_get_main_queue(),
        ^{
            AirfixRenderSettingsCoordinator* strongSelf = weakSelf;
            if (strongSelf == nil ||
                strongSelf->_preparationRetryGeneration != generation ||
                !strongSelf->_scheduledRetryTicket.has_value() ||
                *strongSelf->_scheduledRetryTicket != ticket) {
                return;
            }
            strongSelf->_scheduledRetryTicket.reset();
            if (strongSelf->_gate.isCurrent(
                    ticket,
                    airfix::settings::
                        RenderPresentationPersistencePhase::
                            preparing)) {
                [strongSelf launchPreparation:ticket];
            }
        });
}

- (void)finishCurrentAndStartPending {
    if (_gate.busy() || !_loadCompleted ||
        !_requestQueue.hasOutstandingWork()) {
        return;
    }
    const auto pending = _requestQueue.activatePending();
    if (!pending.has_value()) {
        return;
    }
    AirfixRenderSettingsApplyCompletion completion =
        _pendingCompletion;
    _pendingCompletion = nil;
    _activeRequest = pending;
    _activeCompletion = completion;
    if (_persistenceBlocked || _renderer == nil) {
        [self
            completeActiveRequest:*pending
            result:
                AirfixRenderSettingsApplyResultPersistenceUnavailable];
        return;
    }
    [self beginCandidate:pending->candidate
          alreadyDurable:NO
                 request:pending];
}

@end
