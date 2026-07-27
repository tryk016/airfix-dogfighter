#import "AirfixGameViewController.h"

#import "AirfixContentCoordinator.h"
#import "AirfixIOSInputCoordinator.h"
#import "AirfixMetalRenderer.h"
#import "AirfixTouchControlsView.h"
#import "AirfixMissionWorldRoomSnapshot.h"

#import <MetalKit/MetalKit.h>

#include "AirfixPrivateMissionConfig.h"
#include "AirfixMissionWorldRoomSnapshot+Private.hpp"
#include "AirfixIOSInputCoordinator+Private.hpp"
#include "airfix/runtime/AppSession.hpp"
#include "airfix/simulation/PlayerAircraftSimulation.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <optional>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] NSString* decodePrivateLogicalPath(
    const std::string_view encoded) {
    if (encoded.empty()) {
        return nil;
    }
    NSString* const base64 = [[NSString alloc]
        initWithBytes:encoded.data()
               length:encoded.size()
             encoding:NSASCIIStringEncoding];
    if (base64 == nil) {
        return nil;
    }
    NSData* const bytes = [[NSData alloc]
        initWithBase64EncodedString:base64
                           options:0];
    if (bytes == nil || bytes.length == 0U) {
        return nil;
    }
    NSString* const logicalPath = [[NSString alloc]
        initWithData:bytes
            encoding:NSUTF8StringEncoding];
    if (logicalPath == nil) {
        return nil;
    }
    return logicalPath;
}

} // namespace

@interface AirfixGameViewController ()
    <AirfixContentCoordinatorDelegate, AirfixIOSInputCoordinatorDelegate> {
    airfix::runtime::AppSession _session;
    airfix::simulation::PlayerAircraftState _playerAircraftState;
    std::optional<airfix::simulation::PlayerSpawnPose> _playerSpawnPose;
    dispatch_queue_t _rendererPreparationQueue;
}
@property(nonatomic, strong) AirfixMetalRenderer* renderer;
@property(nonatomic, strong) UILabel* statusLabel;
@property(nonatomic, strong) UILabel* inputDiagnosticsLabel;
@property(nonatomic, strong) AirfixContentCoordinator* contentCoordinator;
@property(nonatomic, strong) AirfixTouchControlsView* touchControlsView;
@property(nonatomic, strong) AirfixIOSInputCoordinator* inputCoordinator;
@property(nonatomic) BOOL inputPipelineReady;
@property(nonatomic) BOOL simulationPipelineReady;

- (void)updateDiagnosticsLabelWithInputDiagnostics:
    (AirfixInputDiagnostics*)diagnostics;
@end

@implementation AirfixGameViewController

- (void)loadView {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTKView* metalView = [[MTKView alloc] initWithFrame:CGRectZero device:device];
    metalView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
        UIViewAutoresizingFlexibleHeight;
    metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    metalView.clearColor = MTLClearColorMake(0.035, 0.055, 0.085, 1.0);
    metalView.clearDepth = 1.0;
    metalView.preferredFramesPerSecond = 60;
    metalView.paused = YES;
    metalView.enableSetNeedsDisplay = NO;
    self.view = metalView;

    NSError* rendererError = nil;
    self.renderer = [[AirfixMetalRenderer alloc] initWithMetalView:metalView
                                                            error:&rendererError];
    metalView.delegate = self.renderer;

    UILabel* label = [[UILabel alloc] initWithFrame:CGRectZero];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.numberOfLines = 0;
    label.textAlignment = NSTextAlignmentCenter;
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    label.adjustsFontForContentSizeCategory = YES;
    if (self.renderer == nil) {
        label.text = @"Metal renderer unavailable\nInitialization failed";
    }
    else {
        label.text = @"Airfix Dogfighter reconstruction\nMetal renderer ready";
    }
    self.statusLabel = label;

    UILabel* inputDiagnosticsLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    inputDiagnosticsLabel.translatesAutoresizingMaskIntoConstraints = NO;
    inputDiagnosticsLabel.numberOfLines = 4;
    inputDiagnosticsLabel.textAlignment = NSTextAlignmentCenter;
    inputDiagnosticsLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.82];
    inputDiagnosticsLabel.font = [UIFont monospacedDigitSystemFontOfSize:12.0
                                                                  weight:
        UIFontWeightMedium];
    inputDiagnosticsLabel.adjustsFontForContentSizeCategory = YES;
    inputDiagnosticsLabel.userInteractionEnabled = NO;
    inputDiagnosticsLabel.isAccessibilityElement = NO;
    inputDiagnosticsLabel.accessibilityElementsHidden = YES;
    inputDiagnosticsLabel.text = @"INPUT T0  B +0  P +0  FIRE up\nCONTROLLER none";
    self.inputDiagnosticsLabel = inputDiagnosticsLabel;

    self.contentCoordinator = [[AirfixContentCoordinator alloc]
        initWithPresentingViewController:self];
    self.contentCoordinator.delegate = self;
    _rendererPreparationQueue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.renderer-preparation",
        DISPATCH_QUEUE_SERIAL);
    const std::string_view configuredSetup =
        airfix::ios::private_mission_config::
            initialSetupLogicalPathBase64;
    const std::string_view configuredLevel =
        airfix::ios::private_mission_config::
            initialLevelLogicalPathBase64;
    const std::string_view configuredPlayerObject =
        airfix::ios::private_mission_config::
            initialPlayerObjectLogicalPathBase64;
    const bool hasCompleteMissionPair =
        !configuredSetup.empty() && !configuredLevel.empty();
    const bool hasAnyMissionPath =
        !configuredSetup.empty() || !configuredLevel.empty();
    if (hasCompleteMissionPair) {
        NSString* const setupLogicalPath =
            decodePrivateLogicalPath(configuredSetup);
        NSString* const levelLogicalPath =
            decodePrivateLogicalPath(configuredLevel);
        NSString* const playerObjectLogicalPath =
            decodePrivateLogicalPath(configuredPlayerObject);
        if (setupLogicalPath != nil && levelLogicalPath != nil &&
            (configuredPlayerObject.empty() ||
             playerObjectLogicalPath != nil)) {
            [self.contentCoordinator
                requestMissionWithSetupLogicalPath:setupLogicalPath
                                  levelLogicalPath:levelLogicalPath
                           playerObjectLogicalPath:
                               playerObjectLogicalPath
                               requestedStartIndex:
                                   airfix::ios::private_mission_config::
                                       initialStartIndex];
        }
        else {
            label.text =
                @"Airfix Dogfighter reconstruction\n"
                 @"Private mission configuration is invalid";
        }
    }
    else if (!configuredPlayerObject.empty()) {
        label.text =
            @"Airfix Dogfighter reconstruction\n"
             @"Private mission configuration is invalid";
    }
    else if (hasAnyMissionPath) {
        label.text =
            @"Airfix Dogfighter reconstruction\n"
             @"Private mission configuration is incomplete";
    }
    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        label,
        self.contentCoordinator.controlsView,
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 28.0;
    stack.alignment = UIStackViewAlignmentFill;
    UIScrollView* scrollView = [[UIScrollView alloc] initWithFrame:CGRectZero];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.alwaysBounceVertical = NO;
    [metalView addSubview:scrollView];
    [scrollView addSubview:stack];
    [metalView addSubview:inputDiagnosticsLabel];

    AirfixTouchControlsView* touchControlsView =
        [[AirfixTouchControlsView alloc] initWithFrame:CGRectZero];
    touchControlsView.translatesAutoresizingMaskIntoConstraints = NO;
    touchControlsView.hidden = YES;
    [metalView addSubview:touchControlsView];
    self.touchControlsView = touchControlsView;
    self.inputCoordinator = [[AirfixIOSInputCoordinator alloc]
        initWithTouchControlsView:touchControlsView];
    self.inputCoordinator.delegate = self;
    self.simulationPipelineReady = YES;

    __weak AirfixGameViewController* weakSelf = self;
    self.inputPipelineReady = airfix::ios::setInputFrameConsumer(
        self.inputCoordinator,
        [weakSelf](const airfix::input::InputFrame& frame) noexcept {
            AirfixGameViewController* strongSelf = weakSelf;
            if (strongSelf == nil) {
                return;
            }
            const bool meaningful =
                frame.analog(airfix::input::AnalogAxis::flightBank) != 0 ||
                frame.analog(airfix::input::AnalogAxis::flightPitch) != 0 ||
                frame.held(
                    airfix::input::DigitalAction::combatPrimaryFire);
            if (meaningful) {
                strongSelf->_session.noteInputActivity();
            }

            // The input pump can emit more than one fixed frame after a
            // display stall. This bridge never infers or replays missing
            // simulation ticks: every eligible delivered frame advances the
            // deterministic state exactly once.
            if (!strongSelf->_session.simulationRunning() ||
                frame.pressed(
                    airfix::input::DigitalAction::globalPause)) {
                return;
            }

            const auto advanced = airfix::simulation::advance(
                strongSelf->_playerAircraftState, frame);
            if (advanced) {
                strongSelf->_playerAircraftState = advanced.state;
                return;
            }

            // A rejected deterministic transition is terminal for this
            // controller instance. Keep the last accepted world state intact,
            // neutralize physical input, and refuse later pause-button resume.
            strongSelf.simulationPipelineReady = NO;
            strongSelf->_session.pause();
            [strongSelf.inputCoordinator resetForGameplayBoundary];
            ((MTKView*)strongSelf.view).paused = YES;
            strongSelf.touchControlsView.hidden = YES;
            strongSelf.statusLabel.text =
                @"Airfix Dogfighter reconstruction\n"
                 @"Simulation halted after an invalid deterministic step";
            [strongSelf
                updateDiagnosticsLabelWithInputDiagnostics:
                    strongSelf.inputCoordinator.diagnostics];
        });
    if (!self.inputPipelineReady) {
        label.text =
            @"Airfix Dogfighter reconstruction\nInput initialization failed";
    }
    [self updateDiagnosticsLabelWithInputDiagnostics:
        self.inputCoordinator.diagnostics];

    UILayoutGuide* safeArea = metalView.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [scrollView.topAnchor constraintEqualToAnchor:safeArea.topAnchor],
        [scrollView.bottomAnchor constraintEqualToAnchor:safeArea.bottomAnchor],
        [scrollView.leadingAnchor constraintEqualToAnchor:safeArea.leadingAnchor],
        [scrollView.trailingAnchor constraintEqualToAnchor:safeArea.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.topAnchor
                                         constant:16.0],
        [stack.bottomAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.bottomAnchor
                                            constant:-16.0],
        [stack.leadingAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.leadingAnchor
                                             constant:24.0],
        [stack.trailingAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.trailingAnchor
                                              constant:-24.0],
        [stack.widthAnchor constraintEqualToAnchor:scrollView.frameLayoutGuide.widthAnchor
                                          constant:-48.0],
        [inputDiagnosticsLabel.bottomAnchor
            constraintEqualToAnchor:safeArea.bottomAnchor
                           constant:-8.0],
        [inputDiagnosticsLabel.centerXAnchor
            constraintEqualToAnchor:safeArea.centerXAnchor],
        [touchControlsView.topAnchor constraintEqualToAnchor:metalView.topAnchor],
        [touchControlsView.bottomAnchor
            constraintEqualToAnchor:metalView.bottomAnchor],
        [touchControlsView.leadingAnchor
            constraintEqualToAnchor:metalView.leadingAnchor],
        [touchControlsView.trailingAnchor
            constraintEqualToAnchor:metalView.trailingAnchor],
    ]];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self.contentCoordinator start];
    [self.inputCoordinator start];
}

- (void)viewDidDisappear:(BOOL)animated {
    const bool wasRunning = _session.simulationRunning();
    _session.pause();
    ((MTKView*)self.view).paused = YES;
    if (wasRunning) {
        self.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
             @"Gameplay paused; press pause or controller menu to resume";
    }
    [self.inputCoordinator stop];
    [super viewDidDisappear:animated];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (void)applicationWillResignActive {
    [self.inputCoordinator applicationWillResignActive];
    [self.contentCoordinator applicationWillResignActive];
    _session.enterInactive();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidEnterBackground {
    [self.inputCoordinator applicationDidEnterBackground];
    [self.contentCoordinator applicationDidEnterBackground];
    _session.enterBackground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationWillEnterForeground {
    [self.inputCoordinator applicationWillEnterForeground];
    [self.contentCoordinator applicationWillEnterForeground];
    _session.enterForeground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidBecomeActive {
    if (_session.lifecycleState() !=
        airfix::runtime::LifecycleState::foregroundPaused) {
        _session.enterForeground();
    }
    [self.inputCoordinator applicationDidBecomeActive];
    [self.contentCoordinator applicationDidBecomeActive];
    // Becoming active never resumes gameplay. The player must use the pause
    // control or a controller menu button after every lifecycle transition.
    ((MTKView*)self.view).paused = YES;
    if (_session.contentState() == airfix::runtime::ContentState::ready &&
        self.inputPipelineReady &&
        self.simulationPipelineReady &&
        self.inputCoordinator.isOperational) {
        self.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
             @"Gameplay paused; press pause or controller menu to resume";
    }
}

- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didChangeReadiness:(AirfixContentReadiness)readiness {
    (void)coordinator;
    airfix::runtime::ContentState contentState =
        airfix::runtime::ContentState::missing;
    switch (readiness) {
    case AirfixContentReadinessMissing:
        contentState = airfix::runtime::ContentState::missing;
        break;
    case AirfixContentReadinessValidating:
        contentState = airfix::runtime::ContentState::validating;
        break;
    case AirfixContentReadinessReady:
        // A valid AFPACK is necessary but not renderable until the requested
        // mission has also passed portable loading and the Metal transaction.
        contentState = airfix::runtime::ContentState::validating;
        break;
    case AirfixContentReadinessRejected:
        contentState = airfix::runtime::ContentState::rejected;
        break;
    }
    _session.setContentState(contentState);
    if (contentState != airfix::runtime::ContentState::ready) {
        [self.inputCoordinator resetForGameplayBoundary];
        ((MTKView*)self.view).paused = YES;
        self.touchControlsView.hidden = YES;
    }
}

- (void)contentCoordinatorDidBeginLoadingMission:
    (AirfixContentCoordinator*)coordinator {
    (void)coordinator;
    _session.setContentState(airfix::runtime::ContentState::validating);
    // Keep the previously committed mission state intact until the new room,
    // spawn pose, and fresh deterministic input state commit together.
    [self.inputCoordinator resetForGameplayBoundary];
    [self updateDiagnosticsLabelWithInputDiagnostics:
        self.inputCoordinator.diagnostics];
    ((MTKView*)self.view).paused = YES;
    self.touchControlsView.hidden = YES;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nLoading private mission...";
}

- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didLoadMissionWorldRoomSnapshot:
            (AirfixMissionWorldRoomSnapshot*)snapshot {
    AirfixMetalRenderer* renderer = self.renderer;
    if (renderer == nil || snapshot == nil) {
        if (snapshot != nil) {
            [coordinator abandonMissionWorldRoomSnapshot:snapshot];
        }
        _session.setContentState(airfix::runtime::ContentState::rejected);
        [self.inputCoordinator resetForGameplayBoundary];
        ((MTKView*)self.view).paused = YES;
        self.touchControlsView.hidden = YES;
        self.statusLabel.text =
            @"Airfix Dogfighter reconstruction\nMetal renderer unavailable";
        return;
    }

    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nPreparing Metal resources...";
    __weak AirfixGameViewController* weakSelf = self;
    dispatch_async(_rendererPreparationQueue, ^{
        AirfixPreparedMetalRoom* preparedRoom = nil;
        NSError* preparationError = nil;
        @try {
            try {
                auto room =
                    airfix::ios::takeLoadedMissionWorldRoom(snapshot);
                preparedRoom =
                    [renderer
                        prepareLoadedMissionRoom:std::move(room)
                                          error:&preparationError];
            }
            catch (...) {
                preparationError = [NSError
                    errorWithDomain:@"com.tryk016.airfixdogfighter.renderer"
                               code:1
                           userInfo:@{
                               NSLocalizedDescriptionKey :
                                   @"The loaded room handoff failed."
                           }];
            }
        }
        @catch (NSException* exception) {
            (void)exception;
            preparationError = [NSError
                errorWithDomain:@"com.tryk016.airfixdogfighter.renderer"
                           code:2
                       userInfo:@{
                           NSLocalizedDescriptionKey :
                               @"Metal room preparation raised an exception."
                       }];
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            AirfixGameViewController* strongSelf = weakSelf;
            if (strongSelf == nil ||
                ![strongSelf.contentCoordinator
                    isMissionWorldRoomSnapshotCurrent:snapshot]) {
                return;
            }
            if (preparedRoom == nil) {
                [strongSelf.contentCoordinator
                    abandonMissionWorldRoomSnapshot:snapshot];
                strongSelf->_session.setContentState(
                    airfix::runtime::ContentState::rejected);
                [strongSelf.inputCoordinator resetForGameplayBoundary];
                ((MTKView*)strongSelf.view).paused = YES;
                strongSelf.touchControlsView.hidden = YES;
                NSString* reason = preparationError.localizedDescription;
                strongSelf.statusLabel.text = reason.length == 0U
                    ? @"Airfix Dogfighter reconstruction\nRoom preparation failed"
                    : [@"Airfix Dogfighter reconstruction\n"
                        stringByAppendingString:reason];
                return;
            }

            NSError* publicationError = nil;
            const auto playerSpawnPose =
                airfix::ios::missionWorldRoomPlayerSpawnPose(snapshot);
            if (!playerSpawnPose.has_value()) {
                [strongSelf.contentCoordinator
                    abandonMissionWorldRoomSnapshot:snapshot];
                strongSelf->_session.setContentState(
                    airfix::runtime::ContentState::rejected);
                [strongSelf.inputCoordinator resetForGameplayBoundary];
                ((MTKView*)strongSelf.view).paused = YES;
                strongSelf.touchControlsView.hidden = YES;
                strongSelf.statusLabel.text =
                    @"Airfix Dogfighter reconstruction\n"
                     @"Player start publication failed";
                return;
            }
            if (![renderer
                    validatePreparedRoomForCommit:preparedRoom
                                           error:&publicationError]) {
                [strongSelf.contentCoordinator
                    abandonMissionWorldRoomSnapshot:snapshot];
                strongSelf->_session.setContentState(
                    airfix::runtime::ContentState::rejected);
                [strongSelf.inputCoordinator resetForGameplayBoundary];
                ((MTKView*)strongSelf.view).paused = YES;
                strongSelf.touchControlsView.hidden = YES;
                NSString* reason = publicationError.localizedDescription;
                strongSelf.statusLabel.text = reason.length == 0U
                    ? @"Airfix Dogfighter reconstruction\nRoom publication failed"
                    : [@"Airfix Dogfighter reconstruction\n"
                        stringByAppendingString:reason];
                return;
            }

            // Main owns this complete transaction. Validation is read-only;
            // consuming the exact ticket is immediately followed by the
            // renderer's no-fail constant-time pointer swap.
            if (![strongSelf.contentCoordinator
                    isMissionWorldRoomSnapshotCurrent:snapshot] ||
                ![strongSelf.contentCoordinator
                    consumeMissionWorldRoomSnapshot:snapshot]) {
                return;
            }
            [renderer commitValidatedPreparedRoom:preparedRoom];
            strongSelf->_playerSpawnPose = *playerSpawnPose;
            strongSelf->_playerAircraftState = {};

            strongSelf->_session.setContentState(
                airfix::runtime::ContentState::ready);
            [strongSelf.inputCoordinator resetForGameplayBoundary];
            const BOOL inputOperational =
                strongSelf.inputPipelineReady &&
                strongSelf.simulationPipelineReady &&
                strongSelf.inputCoordinator.isOperational;
            strongSelf.touchControlsView.hidden = !inputOperational;
            if (inputOperational) {
                strongSelf.statusLabel.text = [NSString stringWithFormat:
                    @"Airfix Dogfighter reconstruction\n"
                     @"Private mission ready: %lu meshes, %lu textures, %lu draws\n"
                     @"Press pause or controller menu to start",
                    static_cast<unsigned long>(snapshot.meshCount),
                    static_cast<unsigned long>(snapshot.textureCount),
                    static_cast<unsigned long>(snapshot.drawCommandCount)];
            }
            else if (!strongSelf.simulationPipelineReady) {
                strongSelf.statusLabel.text =
                    @"Airfix Dogfighter reconstruction\n"
                     @"Simulation halted; gameplay cannot resume";
            }
            else {
                strongSelf.statusLabel.text =
                    @"Airfix Dogfighter reconstruction\n"
                     @"Private mission ready; input pipeline unavailable";
            }
            ((MTKView*)strongSelf.view).paused = YES;
        });
    });
}

- (void)contentCoordinatorDidFailLoadingMission:
    (AirfixContentCoordinator*)coordinator {
    (void)coordinator;
    _session.setContentState(airfix::runtime::ContentState::rejected);
    [self.inputCoordinator resetForGameplayBoundary];
    ((MTKView*)self.view).paused = YES;
    self.touchControlsView.hidden = YES;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nPrivate mission could not be loaded";
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
    didRequestPauseForReason:(AirfixInputPauseReason)reason {
    if (coordinator != self.inputCoordinator) {
        return;
    }

    MTKView* metalView = (MTKView*)self.view;
    if (reason != AirfixInputPauseReasonUserControl) {
        _session.pause();
        metalView.paused = YES;
        if (reason == AirfixInputPauseReasonInputPipelineFailure) {
            self.inputPipelineReady = NO;
            self.touchControlsView.hidden = YES;
            self.statusLabel.text =
                @"Airfix Dogfighter reconstruction\nInput pipeline failed";
        }
        else if (reason == AirfixInputPauseReasonControllerDisconnected) {
            self.statusLabel.text =
                @"Airfix Dogfighter reconstruction\n"
                 @"Controller disconnected; gameplay paused";
        }
        else if (reason == AirfixInputPauseReasonInputOverflow) {
            self.statusLabel.text =
                @"Airfix Dogfighter reconstruction\n"
                 @"Input stream reset; gameplay paused";
        }
        return;
    }

    if (_session.lifecycleState() == airfix::runtime::LifecycleState::running) {
        _session.pause();
        metalView.paused = YES;
        self.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
             @"Gameplay paused; press pause or controller menu to resume";
        return;
    }

    const BOOL mayResume =
        UIApplication.sharedApplication.applicationState ==
            UIApplicationStateActive &&
        self.inputPipelineReady &&
        self.simulationPipelineReady &&
        self.inputCoordinator.isOperational &&
        self.renderer.missionWorldRoomInstalled;
    const bool resumed = mayResume && _session.resume();
    metalView.paused = !resumed;
    if (resumed) {
        self.statusLabel.text =
            @"Airfix Dogfighter reconstruction\nGameplay running";
    }
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
        didChangeControllerState:(AirfixGameControllerState*)state {
    if (coordinator != self.inputCoordinator) {
        return;
    }
    (void)state;
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
        didUpdateDiagnostics:(AirfixInputDiagnostics*)diagnostics {
    if (coordinator != self.inputCoordinator) {
        return;
    }
    [self updateDiagnosticsLabelWithInputDiagnostics:diagnostics];
}

- (void)updateDiagnosticsLabelWithInputDiagnostics:
    (AirfixInputDiagnostics*)diagnostics {
    if (diagnostics == nil) {
        return;
    }
    NSString* source = @"none";
    if (diagnostics.lastMeaningfulSource == AirfixInputSourceTouch) {
        source = @"touch";
    }
    else if (diagnostics.lastMeaningfulSource ==
             AirfixInputSourceController) {
        source = @"controller";
    }
    NSString* fire = diagnostics.primaryHeld ? @"down" : @"up";
    NSString* controller = diagnostics.isControllerConnected
        ? @"connected"
        : @"none";
    const auto& simulation = _playerAircraftState;
    const std::uint64_t simulationHash =
        airfix::simulation::canonicalHash(simulation);
    NSString* simulationStatus =
        self.simulationPipelineReady ? @"ok" : @"failed";
    self.inputDiagnosticsLabel.text = [NSString stringWithFormat:
        @"INPUT T%llu  B %+d  P %+d  FIRE %@\n"
         @"CONTROLLER %@  SOURCE %@\n"
         @"SIM STEP %llu  HASH %016llX  %@\n"
         @"INTENT B %+d  P %+d  FIRE %@  PRESS %llu  RELEASE %llu",
        static_cast<unsigned long long>(diagnostics.tick),
        diagnostics.bank,
        diagnostics.pitch,
        fire,
        controller,
        source,
        static_cast<unsigned long long>(simulation.completedSteps),
        static_cast<unsigned long long>(simulationHash),
        simulationStatus,
        static_cast<int>(simulation.bankIntentQ15),
        static_cast<int>(simulation.pitchIntentQ15),
        simulation.primaryFireHeld ? @"down" : @"up",
        static_cast<unsigned long long>(
            simulation.primaryFirePressCount),
        static_cast<unsigned long long>(
            simulation.primaryFireReleaseCount)];
}

@end
