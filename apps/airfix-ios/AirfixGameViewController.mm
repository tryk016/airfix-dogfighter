#import "AirfixGameViewController.h"

#include "AirfixAVAudioEngineBackend.hpp"
#import "AirfixContentCoordinator.h"
#import "AirfixControllerCalibrationPanelViewController.h"
#import "AirfixControllerInputProfileCoordinator.h"
#import "AirfixIOSControllerInputProfileStore.h"
#import "AirfixIOSInputCoordinator.h"
#include "AirfixIOSInputStartupPolicy.hpp"
#import "AirfixMetalRenderer.h"
#import "AirfixMissionWorldRoomSnapshot.h"
#import "AirfixRenderSettingsCoordinator.h"
#import "AirfixRenderSettingsPanelViewController.h"
#import "AirfixTouchControlsView.h"

#import <MetalKit/MetalKit.h>

#include "AirfixIOSInputCoordinator+Private.hpp"
#include "AirfixMissionWorldRoomSnapshot+Private.hpp"
#include "AirfixPrivateMissionConfig.h"
#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/render/PlayerActorPoseRuntime.hpp"
#include "airfix/runtime/AppSession.hpp"
#include "airfix/runtime/PlayerAircraftPresentationCoordinator.hpp"
#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace {

constexpr std::array<airfix::audio::AudioVoiceId, 6U> playerAircraftAudioVoices{
    {
        {1U},
        {2U},
        {3U},
        {4U},
        {5U},
        {6U},
    }};

[[nodiscard]] NSString *
decodePrivateLogicalPath(const std::string_view encoded) {
  if (encoded.empty()) {
    return nil;
  }
  NSString *const base64 =
      [[NSString alloc] initWithBytes:encoded.data()
                               length:encoded.size()
                             encoding:NSASCIIStringEncoding];
  if (base64 == nil) {
    return nil;
  }
  NSData *const bytes = [[NSData alloc] initWithBase64EncodedString:base64
                                                            options:0];
  if (bytes == nil || bytes.length == 0U) {
    return nil;
  }
  NSString *const logicalPath =
      [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
  if (logicalPath == nil) {
    return nil;
  }
  return logicalPath;
}

[[nodiscard]] airfix::render::ConvertedNodeTransform
actorWorldFrom(const airfix::simulation::PlayerSpawnPose &pose) noexcept {
  const auto vectorAt = [](const std::array<float, 3U> &value) {
    return airfix::render::Vec3{value[0], value[1], value[2]};
  };
  return {
      .linear =
          {
              .columns =
                  {
                      vectorAt(pose.runtimeWorldRotationColumns[0]),
                      vectorAt(pose.runtimeWorldRotationColumns[1]),
                      vectorAt(pose.runtimeWorldRotationColumns[2]),
                  },
          },
      .translation = vectorAt(pose.runtimeWorldPosition),
      .rawScalar = 1.0F,
  };
}

[[nodiscard]] BOOL magnitudeAtMost(const int16_t value,
                                   const int16_t limit) noexcept {
  const auto wide = static_cast<std::int32_t>(value);
  const auto magnitude = wide < 0 ? -wide : wide;
  return magnitude <= static_cast<std::int32_t>(limit);
}

} // namespace

@interface AirfixGameViewController () <
    AirfixContentCoordinatorDelegate, AirfixIOSInputCoordinatorDelegate,
    AirfixRenderSettingsPanelViewControllerDelegate,
    AirfixControllerCalibrationPanelViewControllerDelegate> {
  airfix::runtime::AppSession _session;
  airfix::runtime::PlayerAircraftPresentationCoordinator
      _playerAircraftPresentation;
  std::optional<airfix::simulation::PlayerSpawnPose> _playerSpawnPose;
  AirfixPlayerActorPoseRuntimeEndpoint _playerActorPoseRuntime;
  AirfixGameplayCameraMissionRuntimeEndpoint _gameplayCameraRuntime;
  std::unique_ptr<airfix::ios::AirfixAVAudioEngineBackend> _audioBackend;
  std::optional<airfix::simulation::LegacyAircraftAudioBindings>
      _playerAircraftAudioBindings;
  dispatch_queue_t _rendererPreparationQueue;
  BOOL _inputFrameConsumerInstalled;
  BOOL _controllerProfileLoadCompleted;
  BOOL _viewVisible;
  NSUInteger _pausedSettingsSelection;
  BOOL _pausedSettingsNavigationLatched;
}
@property(nonatomic, strong) AirfixMetalRenderer *renderer;
@property(nonatomic, strong)
    AirfixRenderSettingsCoordinator *renderSettingsCoordinator;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *inputDiagnosticsLabel;
@property(nonatomic, strong) AirfixContentCoordinator *contentCoordinator;
@property(nonatomic, strong) AirfixTouchControlsView *touchControlsView;
@property(nonatomic, strong) AirfixIOSInputCoordinator *inputCoordinator;
@property(nonatomic, strong)
    AirfixIOSControllerInputProfileStore *controllerInputProfileStore;
@property(nonatomic, strong)
    AirfixControllerInputProfileCoordinator *controllerInputProfileCoordinator;
@property(nonatomic, copy) NSString *controllerInputProfileStatus;
@property(nonatomic, strong) UIButton *resumeButton;
@property(nonatomic, strong) UIButton *renderSettingsButton;
@property(nonatomic, strong) UIButton *controllerCalibrationButton;
@property(nonatomic, strong)
    AirfixRenderSettingsPanelViewController *renderSettingsPanel;
@property(nonatomic, strong)
    AirfixControllerCalibrationPanelViewController *controllerCalibrationPanel;
@property(nonatomic) BOOL inputPipelineReady;
@property(nonatomic) BOOL simulationPipelineReady;

- (void)showRenderSettings;
- (void)closeRenderSettingsPanel;
- (void)showControllerCalibration;
- (void)closeControllerCalibrationPanel;
- (BOOL)isSettingsPanelOpen;
- (void)setPausedSettingsSelection:(NSUInteger)selection
                          announce:(BOOL)announce;
- (void)beginControllerInputProfileLoad;
- (void)startInputCoordinatorIfReady;
- (void)refreshPausedMissionReadiness;
- (void)resumeGameplay;
- (void)updateDiagnosticsLabelWithInputDiagnostics:
    (AirfixInputDiagnostics *)diagnostics;
- (void)handleAudioForcedPause:(airfix::ios::AirfixIOSAudioPauseReason)reason;
@end

@implementation AirfixGameViewController

- (void)loadView {
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  MTKView *metalView = [[MTKView alloc] initWithFrame:CGRectZero device:device];
  metalView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
  metalView.clearColor = MTLClearColorMake(0.035, 0.055, 0.085, 1.0);
  metalView.clearDepth = 1.0;
  metalView.preferredFramesPerSecond = 60;
  metalView.paused = YES;
  metalView.enableSetNeedsDisplay = NO;
  self.view = metalView;

  NSError *rendererError = nil;
  self.renderer =
      [[AirfixMetalRenderer alloc] initWithMetalView:metalView
                                               error:&rendererError];
  metalView.delegate = self.renderer;
  if (self.renderer != nil) {
    self.renderSettingsCoordinator = [[AirfixRenderSettingsCoordinator alloc]
        initWithRenderer:self.renderer];
  }

  UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.numberOfLines = 0;
  label.textAlignment = NSTextAlignmentCenter;
  label.textColor = UIColor.whiteColor;
  label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
  label.adjustsFontForContentSizeCategory = YES;
  if (self.renderer == nil) {
    label.text = @"Metal renderer unavailable\nInitialization failed";
  } else {
    label.text = @"Airfix Dogfighter reconstruction\nMetal renderer ready";
  }
  self.statusLabel = label;

  UILabel *inputDiagnosticsLabel = [[UILabel alloc] initWithFrame:CGRectZero];
  inputDiagnosticsLabel.translatesAutoresizingMaskIntoConstraints = NO;
  inputDiagnosticsLabel.numberOfLines = 5;
  inputDiagnosticsLabel.textAlignment = NSTextAlignmentCenter;
  inputDiagnosticsLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.82];
  inputDiagnosticsLabel.font =
      [UIFont monospacedDigitSystemFontOfSize:12.0 weight:UIFontWeightMedium];
  inputDiagnosticsLabel.adjustsFontForContentSizeCategory = YES;
  inputDiagnosticsLabel.userInteractionEnabled = NO;
  inputDiagnosticsLabel.isAccessibilityElement = NO;
  inputDiagnosticsLabel.accessibilityElementsHidden = YES;
  inputDiagnosticsLabel.text = @"INPUT T0  B +0  P +0  FIRE up\n"
                                "CONTROLLER none\n"
                                "PROFILE starting";
  self.inputDiagnosticsLabel = inputDiagnosticsLabel;

  self.contentCoordinator =
      [[AirfixContentCoordinator alloc] initWithPresentingViewController:self];
  self.contentCoordinator.delegate = self;
  UIButton *resumeButton = [UIButton buttonWithType:UIButtonTypeSystem];
  resumeButton.translatesAutoresizingMaskIntoConstraints = NO;
  [resumeButton setTitle:@"Resume" forState:UIControlStateNormal];
  resumeButton.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  resumeButton.titleLabel.adjustsFontForContentSizeCategory = YES;
  resumeButton.accessibilityIdentifier = @"airfix.pause.resume";
  resumeButton.accessibilityHint =
      @"Resumes gameplay after resetting physical input.";
  resumeButton.hidden = YES;
  [resumeButton addTarget:self
                   action:@selector(resumeGameplay)
         forControlEvents:UIControlEventTouchUpInside];
  self.resumeButton = resumeButton;

  UIButton *renderSettingsButton = [UIButton buttonWithType:UIButtonTypeSystem];
  renderSettingsButton.translatesAutoresizingMaskIntoConstraints = NO;
  [renderSettingsButton setTitle:@"Display settings"
                        forState:UIControlStateNormal];
  renderSettingsButton.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  renderSettingsButton.titleLabel.adjustsFontForContentSizeCategory = YES;
  renderSettingsButton.accessibilityIdentifier =
      @"airfix.settings.display.open";
  renderSettingsButton.accessibilityHint =
      @"Pauses gameplay and opens display settings.";
  [renderSettingsButton addTarget:self
                           action:@selector(showRenderSettings)
                 forControlEvents:UIControlEventTouchUpInside];
  renderSettingsButton.enabled = self.renderSettingsCoordinator != nil;
  self.renderSettingsButton = renderSettingsButton;

  UIButton *controllerCalibrationButton =
      [UIButton buttonWithType:UIButtonTypeSystem];
  controllerCalibrationButton.translatesAutoresizingMaskIntoConstraints = NO;
  [controllerCalibrationButton setTitle:@"Controller settings"
                               forState:UIControlStateNormal];
  controllerCalibrationButton.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  controllerCalibrationButton.titleLabel.adjustsFontForContentSizeCategory =
      YES;
  controllerCalibrationButton.accessibilityIdentifier =
      @"airfix.settings.controller.open";
  controllerCalibrationButton.accessibilityHint =
      @"Pauses gameplay and edits controller calibration and button "
       "bindings for the next launch.";
  [controllerCalibrationButton addTarget:self
                                  action:@selector(showControllerCalibration)
                        forControlEvents:UIControlEventTouchUpInside];
  controllerCalibrationButton.enabled = NO;
  self.controllerCalibrationButton = controllerCalibrationButton;
  _pausedSettingsSelection = 0U;
  _rendererPreparationQueue =
      dispatch_queue_create("com.tryk016.airfixdogfighter.renderer-preparation",
                            DISPATCH_QUEUE_SERIAL);
  const std::string_view configuredSetup =
      airfix::ios::private_mission_config::initialSetupLogicalPathBase64;
  const std::string_view configuredLevel =
      airfix::ios::private_mission_config::initialLevelLogicalPathBase64;
  const std::string_view configuredPlayerObject =
      airfix::ios::private_mission_config::initialPlayerObjectLogicalPathBase64;
  const bool hasCompleteMissionPair =
      !configuredSetup.empty() && !configuredLevel.empty();
  const bool hasAnyMissionPath =
      !configuredSetup.empty() || !configuredLevel.empty();
  if (hasCompleteMissionPair) {
    NSString *const setupLogicalPath =
        decodePrivateLogicalPath(configuredSetup);
    NSString *const levelLogicalPath =
        decodePrivateLogicalPath(configuredLevel);
    NSString *const playerObjectLogicalPath =
        decodePrivateLogicalPath(configuredPlayerObject);
    if (setupLogicalPath != nil && levelLogicalPath != nil &&
        (configuredPlayerObject.empty() || playerObjectLogicalPath != nil)) {
      [self.contentCoordinator
          requestMissionWithSetupLogicalPath:setupLogicalPath
                            levelLogicalPath:levelLogicalPath
                     playerObjectLogicalPath:playerObjectLogicalPath
                         requestedStartIndex:airfix::ios::
                                                 private_mission_config::
                                                     initialStartIndex];
    } else {
      label.text = @"Airfix Dogfighter reconstruction\n"
                   @"Private mission configuration is invalid";
    }
  } else if (!configuredPlayerObject.empty()) {
    label.text = @"Airfix Dogfighter reconstruction\n"
                 @"Private mission configuration is invalid";
  } else if (hasAnyMissionPath) {
    label.text = @"Airfix Dogfighter reconstruction\n"
                 @"Private mission configuration is incomplete";
  }
  UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
    label,
    resumeButton,
    renderSettingsButton,
    controllerCalibrationButton,
    self.contentCoordinator.controlsView,
  ]];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 28.0;
  stack.alignment = UIStackViewAlignmentFill;
  UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:CGRectZero];
  scrollView.translatesAutoresizingMaskIntoConstraints = NO;
  scrollView.alwaysBounceVertical = NO;
  [metalView addSubview:scrollView];
  [scrollView addSubview:stack];
  [metalView addSubview:inputDiagnosticsLabel];

  AirfixTouchControlsView *touchControlsView =
      [[AirfixTouchControlsView alloc] initWithFrame:CGRectZero];
  touchControlsView.translatesAutoresizingMaskIntoConstraints = NO;
  touchControlsView.hidden = YES;
  [metalView addSubview:touchControlsView];
  self.touchControlsView = touchControlsView;
  self.inputCoordinator = [[AirfixIOSInputCoordinator alloc]
      initWithTouchControlsView:touchControlsView];
  self.inputCoordinator.delegate = self;
  self.controllerInputProfileStore =
      [[AirfixIOSControllerInputProfileStore alloc] init];
  self.controllerInputProfileStatus = @"PROFILE starting";
  self.simulationPipelineReady = YES;
  _audioBackend = std::make_unique<airfix::ios::AirfixAVAudioEngineBackend>();

  __weak AirfixGameViewController *weakSelf = self;
  _audioBackend->setForcedPauseHandler(
      [weakSelf](const airfix::ios::AirfixIOSAudioPauseReason reason) {
        AirfixGameViewController *strongSelf = weakSelf;
        if (strongSelf != nil) {
          [strongSelf handleAudioForcedPause:reason];
        }
      });
  _inputFrameConsumerInstalled = airfix::ios::setInputFrameConsumer(
      self.inputCoordinator,
      [weakSelf](const airfix::input::InputFrame &frame) noexcept {
        AirfixGameViewController *strongSelf = weakSelf;
        if (strongSelf == nil) {
          return;
        }
        const bool meaningful =
            frame.analog(airfix::input::AnalogAxis::flightBank) != 0 ||
            frame.analog(airfix::input::AnalogAxis::flightPitch) != 0 ||
            frame.held(airfix::input::DigitalAction::combatPrimaryFire);
        if (meaningful) {
          strongSelf->_session.noteInputActivity();
        }

        // The input pump can emit more than one fixed frame after a
        // display stall. This bridge never infers or replays missing
        // simulation ticks: every eligible delivered frame advances the
        // deterministic state exactly once.
        if (!strongSelf->_session.simulationRunning() ||
            frame.pressed(airfix::input::DigitalAction::globalPause)) {
          return;
        }

        if (strongSelf->_playerSpawnPose.has_value()) {
          const auto advanced =
              strongSelf->_playerAircraftPresentation.tryAdvance(
                  frame, actorWorldFrom(*strongSelf->_playerSpawnPose),
                  strongSelf->_playerActorPoseRuntime);
          if (advanced.accepted()) {
            return;
          }
        }

        // A rejected deterministic transition, an expired actor endpoint,
        // or a non-busy pose publication failure is terminal for this
        // controller instance. Keep the last accepted world state intact,
        // neutralize physical input, and refuse later pause-button resume.
        strongSelf.simulationPipelineReady = NO;
        strongSelf->_audioBackend->setActive(false);
        strongSelf->_session.pause();
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
            @"Simulation halted after an invalid deterministic step";
        [strongSelf
            updateDiagnosticsLabelWithInputDiagnostics:strongSelf
                                                           .inputCoordinator
                                                           .diagnostics];
      });
  self.inputPipelineReady = NO;
  if (!_inputFrameConsumerInstalled) {
    label.text =
        @"Airfix Dogfighter reconstruction\nInput initialization failed";
  }
  [self beginControllerInputProfileLoad];
  [self updateDiagnosticsLabelWithInputDiagnostics:self.inputCoordinator
                                                       .diagnostics];

  UILayoutGuide *safeArea = metalView.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [scrollView.topAnchor constraintEqualToAnchor:safeArea.topAnchor],
    [scrollView.bottomAnchor constraintEqualToAnchor:safeArea.bottomAnchor],
    [scrollView.leadingAnchor constraintEqualToAnchor:safeArea.leadingAnchor],
    [scrollView.trailingAnchor constraintEqualToAnchor:safeArea.trailingAnchor],
    [stack.topAnchor
        constraintEqualToAnchor:scrollView.contentLayoutGuide.topAnchor
                       constant:16.0],
    [stack.bottomAnchor
        constraintEqualToAnchor:scrollView.contentLayoutGuide.bottomAnchor
                       constant:-16.0],
    [stack.leadingAnchor
        constraintEqualToAnchor:scrollView.contentLayoutGuide.leadingAnchor
                       constant:24.0],
    [stack.trailingAnchor
        constraintEqualToAnchor:scrollView.contentLayoutGuide.trailingAnchor
                       constant:-24.0],
    [stack.widthAnchor
        constraintEqualToAnchor:scrollView.frameLayoutGuide.widthAnchor
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
  [self setPausedSettingsSelection:0U announce:NO];
}

- (void)beginControllerInputProfileLoad {
  NSAssert(NSThread.isMainThread,
           @"Controller profile startup belongs to main");
  if (_controllerProfileLoadCompleted ||
      self.controllerInputProfileStore == nil) {
    return;
  }

  __weak AirfixGameViewController *weakSelf = self;
  [self.controllerInputProfileStore loadWithCompletion:^(
                                        const airfix::ios::
                                            ControllerInputProfileLoadOutcome
                                                outcome) {
    AirfixGameViewController *strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf->_controllerProfileLoadCompleted) {
      return;
    }

    std::optional<airfix::input::ResolvedControllerInputProfile>
        selectedProfile;
    std::optional<airfix::input::ControllerInputProfileRecord> selectedRecord;
    BOOL persistenceAvailable = NO;
    BOOL repairRequired = NO;
    NSString *status = @"PROFILE defaults";
    if (outcome.result.has_value()) {
      selectedProfile = outcome.result->profile;
      selectedRecord = outcome.result->profile.record();
      persistenceAvailable = !outcome.result->persistenceBlocked;
      repairRequired =
          airfix::settings::controllerInputProfileNeedsRepair(*outcome.result);
      switch (outcome.result->source) {
      case airfix::settings::ControllerInputProfileLoadSource::defaults:
        if (outcome.result->current.status ==
                airfix::settings::ControllerInputProfileFileStatus::malformed ||
            outcome.result->current.status ==
                airfix::settings::ControllerInputProfileFileStatus::oversized ||
            outcome.result->backup.status ==
                airfix::settings::ControllerInputProfileFileStatus::malformed ||
            outcome.result->backup.status ==
                airfix::settings::ControllerInputProfileFileStatus::oversized) {
          status = @"PROFILE defaults recovered";
        } else {
          status = @"PROFILE defaults";
        }
        break;
      case airfix::settings::ControllerInputProfileLoadSource::current:
        status = @"PROFILE current";
        break;
      case airfix::settings::ControllerInputProfileLoadSource::backup:
        status = @"PROFILE backup recovered";
        break;
      }
      if (outcome.result->persistenceBlocked) {
        status = [status stringByAppendingString:@"  read-only"];
      }
    } else {
      const auto fallback = airfix::input::resolveControllerInputProfile(
          airfix::input::makeDefaultControllerInputProfileRecord());
      if (fallback.complete()) {
        selectedProfile = *fallback.profile;
        selectedRecord = fallback.profile->record();
      }
      status = @"PROFILE defaults  storage unavailable";
    }

    const bool profileInstalled =
        selectedProfile.has_value() &&
        airfix::ios::detail::installControllerInputProfileBeforeStart(
            strongSelf.inputCoordinator, *selectedProfile);
    if (profileInstalled && selectedRecord.has_value()) {
      strongSelf.controllerInputProfileCoordinator =
          [[AirfixControllerInputProfileCoordinator alloc]
                     initWithStore:strongSelf.controllerInputProfileStore
                     activeProfile:*selectedRecord
                 persistentProfile:*selectedRecord
              persistenceAvailable:persistenceAvailable
                    repairRequired:repairRequired];
    }
    strongSelf.controllerCalibrationButton.enabled =
        strongSelf.controllerInputProfileCoordinator != nil;
    strongSelf->_controllerProfileLoadCompleted = YES;
    strongSelf.controllerInputProfileStatus =
        profileInstalled ? status : @"PROFILE initialization failed";
    strongSelf.inputPipelineReady =
        strongSelf->_inputFrameConsumerInstalled && profileInstalled;
    if (!strongSelf.inputPipelineReady) {
      strongSelf.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                                     "Input initialization failed";
    }
    [strongSelf
        updateDiagnosticsLabelWithInputDiagnostics:strongSelf.inputCoordinator
                                                       .diagnostics];
    [strongSelf startInputCoordinatorIfReady];
  }];
}

- (void)startInputCoordinatorIfReady {
  NSAssert(NSThread.isMainThread, @"Input coordinator startup belongs to main");
  const bool shouldStart = airfix::ios::startup_policy::shouldStartInput({
      .viewVisible = static_cast<bool>(_viewVisible),
      .profileLoadCompleted =
          static_cast<bool>(_controllerProfileLoadCompleted),
      .inputPipelineReady = static_cast<bool>(self.inputPipelineReady),
      .applicationActive = UIApplication.sharedApplication.applicationState ==
                           UIApplicationStateActive,
  });
  if (!shouldStart) {
    return;
  }
  [self.inputCoordinator start];
  [self refreshPausedMissionReadiness];
}

- (void)refreshPausedMissionReadiness {
  NSAssert(NSThread.isMainThread, @"Paused mission readiness belongs to main");
  if ([self isSettingsPanelOpen]) {
    [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
    [self.inputCoordinator resetForGameplayBoundary];
    self.touchControlsView.hidden = YES;
    self.resumeButton.hidden = YES;
    ((MTKView *)self.view).paused = YES;
    return;
  }

  const bool contentReady =
      _session.contentState() == airfix::runtime::ContentState::ready;
  const bool rendererInstalled = self.renderer.missionWorldRoomInstalled;
  if (!contentReady || !rendererInstalled) {
    return;
  }

  [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
  [self.inputCoordinator resetForGameplayBoundary];
  const bool gameplayReady = airfix::ios::startup_policy::shouldOfferResume({
      .contentReady = contentReady,
      .rendererInstalled = rendererInstalled,
      .inputPipelineReady = static_cast<bool>(self.inputPipelineReady),
      .simulationPipelineReady =
          static_cast<bool>(self.simulationPipelineReady),
      .inputOperational =
          static_cast<bool>(self.inputCoordinator.isOperational),
      .settingsPanelClosed = true,
  });
  self.touchControlsView.hidden = YES;
  self.resumeButton.hidden = !gameplayReady;
  [self setPausedSettingsSelection:_pausedSettingsSelection announce:NO];
  if (gameplayReady) {
    self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                            @"Private mission ready\n"
                            @"Select Resume or press controller B to start";
  } else if (!self.simulationPipelineReady) {
    self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                            @"Simulation halted; gameplay cannot resume";
  } else {
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
        @"Private mission ready; input pipeline unavailable";
  }
  ((MTKView *)self.view).paused = YES;
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  _viewVisible = YES;
  [self.renderSettingsCoordinator start];
  [self.renderSettingsCoordinator notifyPresentationSurfaceAvailable];
  [self.contentCoordinator start];
  [self startInputCoordinatorIfReady];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self.renderSettingsCoordinator notifyPresentationSurfaceAvailable];
}

- (void)viewDidDisappear:(BOOL)animated {
  _viewVisible = NO;
  const bool wasRunning = _session.simulationRunning();
  _audioBackend->setActive(false);
  _session.pause();
  ((MTKView *)self.view).paused = YES;
  if (wasRunning) {
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
        @"Gameplay paused; press pause or controller menu to resume";
  }
  [self.inputCoordinator stop];
  [super viewDidDisappear:animated];
}

- (void)showRenderSettings {
  NSAssert(NSThread.isMainThread,
           @"The display-settings overlay belongs to main");
  if ([self isSettingsPanelOpen] || self.renderSettingsCoordinator == nil) {
    return;
  }
  if (!self.renderSettingsCoordinator.readyForPresentation) {
    self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                             "Display settings are still starting";
    return;
  }
  if (self.renderSettingsCoordinator.applying) {
    self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                             "Display settings are still being applied";
    return;
  }

  _audioBackend->setActive(false);
  _session.pause();
  ((MTKView *)self.view).paused = YES;
  [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
  self.touchControlsView.hidden = YES;

  AirfixRenderSettingsPanelViewController *panel =
      [[AirfixRenderSettingsPanelViewController alloc]
          initWithCoordinator:self.renderSettingsCoordinator];
  panel.delegate = self;
  [self addChildViewController:panel];
  panel.view.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:panel.view];
  [NSLayoutConstraint activateConstraints:@[
    [panel.view.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [panel.view.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    [panel.view.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [panel.view.trailingAnchor
        constraintEqualToAnchor:self.view.trailingAnchor],
  ]];
  [panel didMoveToParentViewController:self];
  self.renderSettingsPanel = panel;
  self.renderSettingsButton.enabled = NO;
  self.controllerCalibrationButton.enabled = NO;
  self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                           "Gameplay paused while display settings are open";
}

- (void)closeRenderSettingsPanel {
  NSAssert(NSThread.isMainThread,
           @"The display-settings overlay belongs to main");
  AirfixRenderSettingsPanelViewController *panel = self.renderSettingsPanel;
  if (panel == nil) {
    return;
  }

  panel.delegate = nil;
  [panel willMoveToParentViewController:nil];
  [panel.view removeFromSuperview];
  [panel removeFromParentViewController];
  self.renderSettingsPanel = nil;
  self.renderSettingsButton.enabled = self.renderSettingsCoordinator != nil;
  self.controllerCalibrationButton.enabled =
      self.controllerInputProfileCoordinator != nil &&
      !self.controllerInputProfileCoordinator.isSaving;

  [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
  [self.inputCoordinator resetForGameplayBoundary];
  const BOOL gameplayReady =
      _session.contentState() == airfix::runtime::ContentState::ready &&
      self.inputPipelineReady && self.simulationPipelineReady &&
      self.inputCoordinator.isOperational &&
      self.renderer.missionWorldRoomInstalled;
  self.touchControlsView.hidden = YES;
  self.resumeButton.hidden = !gameplayReady;
  _audioBackend->setActive(false);
  _session.pause();
  ((MTKView *)self.view).paused = YES;
  if (gameplayReady) {
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
         "Settings closed; select Resume or press controller B";
  }
  _pausedSettingsSelection = 0U;
  [self setPausedSettingsSelection:_pausedSettingsSelection announce:NO];
  UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                  self.renderSettingsButton);
}

- (void)showControllerCalibration {
  NSAssert(NSThread.isMainThread,
           @"The controller-settings overlay belongs to main");
  AirfixControllerInputProfileCoordinator *coordinator =
      self.controllerInputProfileCoordinator;
  if ([self isSettingsPanelOpen] || coordinator == nil ||
      coordinator.isSaving) {
    return;
  }

  _audioBackend->setActive(false);
  _session.pause();
  ((MTKView *)self.view).paused = YES;
  [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
  self.touchControlsView.hidden = YES;

  AirfixControllerCalibrationPanelViewController *panel =
      [[AirfixControllerCalibrationPanelViewController alloc]
          initWithCoordinator:coordinator];
  panel.delegate = self;
  [self addChildViewController:panel];
  panel.view.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:panel.view];
  [NSLayoutConstraint activateConstraints:@[
    [panel.view.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [panel.view.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    [panel.view.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [panel.view.trailingAnchor
        constraintEqualToAnchor:self.view.trailingAnchor],
  ]];
  [panel didMoveToParentViewController:self];
  self.controllerCalibrationPanel = panel;
  self.renderSettingsButton.enabled = NO;
  self.controllerCalibrationButton.enabled = NO;
  self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                           "Gameplay paused while controller settings are open";
}

- (void)closeControllerCalibrationPanel {
  NSAssert(NSThread.isMainThread,
           @"The controller-settings overlay belongs to main");
  AirfixControllerCalibrationPanelViewController *panel =
      self.controllerCalibrationPanel;
  if (panel == nil) {
    return;
  }
  const BOOL persistedDuringPresentation = panel.persistedDuringPresentation;

  panel.delegate = nil;
  [panel willMoveToParentViewController:nil];
  [panel.view removeFromSuperview];
  [panel removeFromParentViewController];
  self.controllerCalibrationPanel = nil;
  self.renderSettingsButton.enabled = self.renderSettingsCoordinator != nil;
  self.controllerCalibrationButton.enabled =
      self.controllerInputProfileCoordinator != nil &&
      !self.controllerInputProfileCoordinator.isSaving;

  const auto active = [self.controllerInputProfileCoordinator activeProfile];
  const auto persistent =
      [self.controllerInputProfileCoordinator persistentProfile];
  if (active != persistent) {
    self.controllerInputProfileStatus = @"PROFILE saved for restart";
    [self updateDiagnosticsLabelWithInputDiagnostics:self.inputCoordinator
                                                         .diagnostics];
  } else if (persistedDuringPresentation) {
    self.controllerInputProfileStatus = @"PROFILE current";
    [self updateDiagnosticsLabelWithInputDiagnostics:self.inputCoordinator
                                                         .diagnostics];
  }

  [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
  [self.inputCoordinator resetForGameplayBoundary];
  const BOOL gameplayReady =
      _session.contentState() == airfix::runtime::ContentState::ready &&
      self.inputPipelineReady && self.simulationPipelineReady &&
      self.inputCoordinator.isOperational &&
      self.renderer.missionWorldRoomInstalled;
  self.touchControlsView.hidden = YES;
  self.resumeButton.hidden = !gameplayReady;
  _audioBackend->setActive(false);
  _session.pause();
  ((MTKView *)self.view).paused = YES;
  if (gameplayReady) {
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
         "Controller settings closed; select Resume or press controller B";
  }
  _pausedSettingsSelection = 1U;
  [self setPausedSettingsSelection:_pausedSettingsSelection announce:NO];
  UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                  self.controllerCalibrationButton);
}

- (BOOL)isSettingsPanelOpen {
  return self.renderSettingsPanel != nil ||
         self.controllerCalibrationPanel != nil;
}

- (void)setPausedSettingsSelection:(NSUInteger)selection
                          announce:(BOOL)announce {
  const NSUInteger normalized =
      std::min(selection, static_cast<NSUInteger>(1U));
  _pausedSettingsSelection = normalized;
  NSArray<UIButton *> *buttons = @[
    self.renderSettingsButton,
    self.controllerCalibrationButton,
  ];
  for (NSUInteger index = 0U; index < buttons.count; ++index) {
    UIButton *button = buttons[index];
    const BOOL active = index == normalized;
    button.layer.borderColor =
        (active ? UIColor.systemYellowColor : UIColor.clearColor).CGColor;
    button.layer.borderWidth = active ? 2.5 : 0.0;
    button.layer.cornerRadius = 8.0;
  }
  if (announce && buttons[normalized].enabled) {
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                    buttons[normalized]);
  }
}

- (void)resumeGameplay {
  NSAssert(NSThread.isMainThread, @"Explicit gameplay resume belongs to main");
  if ([self isSettingsPanelOpen]) {
    return;
  }
  const BOOL mayResume = UIApplication.sharedApplication.applicationState ==
                             UIApplicationStateActive &&
                         self.inputPipelineReady &&
                         self.simulationPipelineReady &&
                         self.inputCoordinator.isOperational &&
                         self.renderSettingsCoordinator.readyForPresentation &&
                         self.renderer.missionWorldRoomInstalled;
  if (!mayResume) {
    return;
  }

  [self.inputCoordinator setInputContext:AirfixNativeInputContextGameplay];
  [self.inputCoordinator resetForGameplayBoundary];
  const bool resumed = _session.resume();
  ((MTKView *)self.view).paused = !resumed;
  if (!resumed) {
    [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
    [self.inputCoordinator resetForGameplayBoundary];
    return;
  }

  _audioBackend->setActive(true);
  self.resumeButton.hidden = YES;
  self.touchControlsView.hidden = NO;
  self.statusLabel.text = @"Airfix Dogfighter reconstruction\nGameplay running";
}

- (void)renderSettingsPanelViewControllerDidFinish:
    (AirfixRenderSettingsPanelViewController *)panel {
  if (panel != self.renderSettingsPanel) {
    return;
  }
  [self closeRenderSettingsPanel];
}

- (void)controllerCalibrationPanelViewControllerDidFinish:
    (AirfixControllerCalibrationPanelViewController *)panel {
  if (panel != self.controllerCalibrationPanel) {
    return;
  }
  [self closeControllerCalibrationPanel];
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
  _audioBackend->setActive(false);
  _session.enterInactive();
  ((MTKView *)self.view).paused = YES;
}

- (void)applicationDidEnterBackground {
  [self.inputCoordinator applicationDidEnterBackground];
  [self.contentCoordinator applicationDidEnterBackground];
  _audioBackend->setActive(false);
  _session.enterBackground();
  ((MTKView *)self.view).paused = YES;
}

- (void)applicationWillEnterForeground {
  [self.inputCoordinator applicationWillEnterForeground];
  [self.contentCoordinator applicationWillEnterForeground];
  _audioBackend->setActive(false);
  _session.enterForeground();
  ((MTKView *)self.view).paused = YES;
}

- (void)applicationDidBecomeActive {
  if (_session.lifecycleState() !=
      airfix::runtime::LifecycleState::foregroundPaused) {
    _session.enterForeground();
  }
  [self startInputCoordinatorIfReady];
  [self.inputCoordinator applicationDidBecomeActive];
  [self.contentCoordinator applicationDidBecomeActive];
  [self.renderSettingsCoordinator notifyPresentationSurfaceAvailable];
  // Becoming active never resumes gameplay. The player must use the pause
  // control or a controller menu button after every lifecycle transition.
  ((MTKView *)self.view).paused = YES;
  if (![self isSettingsPanelOpen] &&
      _session.contentState() == airfix::runtime::ContentState::ready &&
      self.inputPipelineReady && self.simulationPipelineReady &&
      self.inputCoordinator.isOperational) {
    [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
    self.touchControlsView.hidden = YES;
    self.resumeButton.hidden = NO;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
        @"Gameplay paused; choose settings with the stick, A opens, B resumes";
  }
}

- (void)contentCoordinator:(AirfixContentCoordinator *)coordinator
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
    _audioBackend->setActive(false);
    [self.inputCoordinator resetForGameplayBoundary];
    ((MTKView *)self.view).paused = YES;
    self.touchControlsView.hidden = YES;
    self.resumeButton.hidden = YES;
  }
}

- (void)contentCoordinatorDidBeginLoadingMission:
    (AirfixContentCoordinator *)coordinator {
  (void)coordinator;
  _audioBackend->setActive(false);
  _session.setContentState(airfix::runtime::ContentState::validating);
  // Keep the previously committed mission state intact until the new room,
  // spawn pose, and fresh deterministic input state commit together.
  [self.inputCoordinator resetForGameplayBoundary];
  [self updateDiagnosticsLabelWithInputDiagnostics:self.inputCoordinator
                                                       .diagnostics];
  ((MTKView *)self.view).paused = YES;
  self.touchControlsView.hidden = YES;
  self.resumeButton.hidden = YES;
  self.statusLabel.text =
      @"Airfix Dogfighter reconstruction\nLoading private mission...";
}

- (void)contentCoordinator:(AirfixContentCoordinator *)coordinator
    didLoadMissionWorldRoomSnapshot:(AirfixMissionWorldRoomSnapshot *)snapshot {
  AirfixMetalRenderer *renderer = self.renderer;
  if (renderer == nil || snapshot == nil) {
    if (snapshot != nil) {
      [coordinator abandonMissionWorldRoomSnapshot:snapshot];
    }
    _session.setContentState(airfix::runtime::ContentState::rejected);
    [self.inputCoordinator resetForGameplayBoundary];
    ((MTKView *)self.view).paused = YES;
    self.touchControlsView.hidden = YES;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nMetal renderer unavailable";
    return;
  }

  self.statusLabel.text =
      @"Airfix Dogfighter reconstruction\nPreparing Metal resources...";
  __weak AirfixGameViewController *weakSelf = self;
  dispatch_async(_rendererPreparationQueue, ^{
    AirfixPreparedMetalRoom *preparedRoom = nil;
    std::shared_ptr<airfix::content::LoadedLegacyAircraftAudioClips>
        preparedAudioClips;
    NSError *preparationError = nil;
    @try {
      try {
        auto room = airfix::ios::takeLoadedMissionWorldRoom(snapshot);
        auto audioClips =
            airfix::ios::takeLoadedLegacyAircraftAudioClips(snapshot);
        auto crosshairs =
            airfix::ios::takeLoadedLegacyWeaponCrosshairTextures(snapshot);
        auto healthGauge =
            airfix::ios::takeLoadedLegacyAircraftHealthGaugeTextures(snapshot);
        auto rollingDigits =
            airfix::ios::takeLoadedLegacyAircraftHudRollingDigitTextures(
                snapshot);
        auto hudInstruments =
            airfix::ios::takeLoadedLegacyAircraftHudInstrumentTextures(
                snapshot);
        auto hudWeaponPanels =
            airfix::ios::takeLoadedLegacyAircraftHudWeaponPanelTextures(
                snapshot);
        auto hudIdentityStatus =
            airfix::ios::takeLoadedLegacyAircraftHudIdentityStatusTextures(
                snapshot);
        preparedAudioClips =
            std::make_shared<airfix::content::LoadedLegacyAircraftAudioClips>(
                std::move(audioClips));
        preparedRoom =
            [renderer prepareLoadedMissionRoom:std::move(room)
                              weaponCrosshairs:std::move(crosshairs)
                           aircraftHealthGauge:std::move(healthGauge)
                      aircraftHudRollingDigits:std::move(rollingDigits)
                        aircraftHudInstruments:std::move(hudInstruments)
                       aircraftHudWeaponPanels:std::move(hudWeaponPanels)
                     aircraftHudIdentityStatus:std::move(hudIdentityStatus)
                                         error:&preparationError];
      } catch (...) {
        preparationError =
            [NSError errorWithDomain:@"com.tryk016.airfixdogfighter.renderer"
                                code:1
                            userInfo:@{
                              NSLocalizedDescriptionKey :
                                  @"The loaded room handoff failed."
                            }];
      }
    } @catch (NSException *exception) {
      (void)exception;
      preparationError =
          [NSError errorWithDomain:@"com.tryk016.airfixdogfighter.renderer"
                              code:2
                          userInfo:@{
                            NSLocalizedDescriptionKey :
                                @"Metal room preparation raised an exception."
                          }];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      AirfixGameViewController *strongSelf = weakSelf;
      if (strongSelf == nil ||
          ![strongSelf.contentCoordinator
              isMissionWorldRoomSnapshotCurrent:snapshot]) {
        return;
      }
      if (preparedRoom == nil || preparedAudioClips == nullptr) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        NSString *reason = preparationError.localizedDescription;
        strongSelf.statusLabel.text =
            reason.length == 0U
                ? @"Airfix Dogfighter reconstruction\nRoom preparation failed"
                : [@"Airfix Dogfighter reconstruction\n"
                      stringByAppendingString:reason];
        return;
      }

      NSError *publicationError = nil;
      const auto playerSpawnPose =
          airfix::ios::missionWorldRoomPlayerSpawnPose(snapshot);
      if (!playerSpawnPose.has_value()) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                                      @"Player start publication failed";
        return;
      }
      if (![renderer validatePreparedRoomForCommit:preparedRoom
                                             error:&publicationError]) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        NSString *reason = publicationError.localizedDescription;
        strongSelf.statusLabel.text =
            reason.length == 0U
                ? @"Airfix Dogfighter reconstruction\nRoom publication failed"
                : [@"Airfix Dogfighter reconstruction\n"
                      stringByAppendingString:reason];
        return;
      }

      const auto playerActorPoseRuntime =
          [renderer playerActorPoseRuntimeEndpointForPreparedRoom:preparedRoom];
      if (playerActorPoseRuntime.has_value() &&
          playerActorPoseRuntime->expired()) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                                      @"Player pose runtime publication failed";
        return;
      }

      const auto gameplayCameraRuntime = [renderer
          gameplayCameraMissionRuntimeEndpointForPreparedRoom:preparedRoom];
      if (gameplayCameraRuntime.expired()) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
            @"Gameplay camera runtime publication failed";
        return;
      }

      const auto aircraftAudioBindings =
          preparedAudioClips->bindings(playerAircraftAudioVoices);
      if (!aircraftAudioBindings.has_value()) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                                      @"Aircraft audio bindings are invalid";
        return;
      }

      std::unique_ptr<airfix::ios::AirfixAVAudioEngineBackend>
          preparedAudioBackend;
      bool audioPreparationFailed = false;
      try {
        preparedAudioBackend =
            std::make_unique<airfix::ios::AirfixAVAudioEngineBackend>();
        audioPreparationFailed =
            preparedAudioBackend->outputState() ==
            airfix::ios::AirfixIOSAudioOutputState::initializationFailed;
        if (!audioPreparationFailed) {
          for (const auto &clip : preparedAudioClips->clipViews()) {
            if (preparedAudioBackend->registerPcm16Clip(clip) !=
                airfix::ios::AirfixIOSAudioClipRegistrationResult::registered) {
              audioPreparationFailed = true;
              break;
            }
          }
        }
      } catch (...) {
        audioPreparationFailed = true;
      }
      if (audioPreparationFailed || preparedAudioBackend == nullptr) {
        [strongSelf.contentCoordinator
            abandonMissionWorldRoomSnapshot:snapshot];
        strongSelf->_session.setContentState(
            airfix::runtime::ContentState::rejected);
        [strongSelf.inputCoordinator resetForGameplayBoundary];
        ((MTKView *)strongSelf.view).paused = YES;
        strongSelf.touchControlsView.hidden = YES;
        strongSelf.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                                      @"Aircraft audio preparation failed";
        return;
      }
      preparedAudioBackend->setForcedPauseHandler(
          [weakSelf](const airfix::ios::AirfixIOSAudioPauseReason reason) {
            AirfixGameViewController *owner = weakSelf;
            if (owner != nil) {
              [owner handleAudioForcedPause:reason];
            }
          });

      // Main owns this complete transaction. Validation is read-only;
      // consuming the exact ticket is immediately followed by the
      // renderer and audio backends' no-fail ownership swaps.
      if (![strongSelf.contentCoordinator
              isMissionWorldRoomSnapshotCurrent:snapshot] ||
          ![strongSelf.contentCoordinator
              consumeMissionWorldRoomSnapshot:snapshot]) {
        return;
      }
      strongSelf->_audioBackend = std::move(preparedAudioBackend);
      [renderer commitValidatedPreparedRoom:preparedRoom];
      strongSelf->_playerActorPoseRuntime = playerActorPoseRuntime;
      // This weak endpoint is committed with the same snapshot but is
      // deliberately not advanced from the 60 Hz input consumer. The
      // recovered camera requires distinct live AirCraft positions,
      // factor gates, and the scheduler delta in seconds.
      strongSelf->_gameplayCameraRuntime = gameplayCameraRuntime;
      strongSelf->_playerSpawnPose = *playerSpawnPose;
      strongSelf->_playerAircraftPresentation =
          airfix::runtime::PlayerAircraftPresentationCoordinator{};
      strongSelf->_playerAircraftAudioBindings = *aircraftAudioBindings;

      strongSelf->_session.setContentState(
          airfix::runtime::ContentState::ready);
      [strongSelf.inputCoordinator
          setInputContext:AirfixNativeInputContextMenu];
      [strongSelf.inputCoordinator resetForGameplayBoundary];
      const BOOL inputOperational = strongSelf.inputPipelineReady &&
                                    strongSelf.simulationPipelineReady &&
                                    strongSelf.inputCoordinator.isOperational;
      strongSelf.touchControlsView.hidden = YES;
      strongSelf.resumeButton.hidden =
          [strongSelf isSettingsPanelOpen] || !inputOperational;
      if (inputOperational) {
        strongSelf.statusLabel.text = [NSString
            stringWithFormat:
                @"Airfix Dogfighter reconstruction\n"
                @"Private mission ready: %lu meshes, %lu textures, %lu draws\n"
                @"Select Resume or press controller B to start",
                static_cast<unsigned long>(snapshot.meshCount),
                static_cast<unsigned long>(snapshot.textureCount),
                static_cast<unsigned long>(snapshot.drawCommandCount)];
      } else if (!strongSelf.simulationPipelineReady) {
        strongSelf.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
            @"Simulation halted; gameplay cannot resume";
      } else {
        strongSelf.statusLabel.text =
            @"Airfix Dogfighter reconstruction\n"
            @"Private mission ready; input pipeline unavailable";
      }
      ((MTKView *)strongSelf.view).paused = YES;
    });
  });
}

- (void)contentCoordinatorDidFailLoadingMission:
    (AirfixContentCoordinator *)coordinator {
  (void)coordinator;
  _audioBackend->setActive(false);
  _playerAircraftAudioBindings.reset();
  _session.setContentState(airfix::runtime::ContentState::rejected);
  [self.inputCoordinator resetForGameplayBoundary];
  ((MTKView *)self.view).paused = YES;
  self.touchControlsView.hidden = YES;
  self.resumeButton.hidden = YES;
  self.statusLabel.text =
      @"Airfix Dogfighter reconstruction\nPrivate mission could not be loaded";
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator *)coordinator
    didRequestPauseForReason:(AirfixInputPauseReason)reason {
  if (coordinator != self.inputCoordinator) {
    return;
  }

  MTKView *metalView = (MTKView *)self.view;
  if ([self isSettingsPanelOpen] &&
      reason == AirfixInputPauseReasonUserControl) {
    _audioBackend->setActive(false);
    _session.pause();
    metalView.paused = YES;
    return;
  }
  if (reason != AirfixInputPauseReasonUserControl) {
    _audioBackend->setActive(false);
    _session.pause();
    metalView.paused = YES;
    if (reason == AirfixInputPauseReasonInputPipelineFailure) {
      self.inputPipelineReady = NO;
      self.touchControlsView.hidden = YES;
      self.resumeButton.hidden = YES;
      self.statusLabel.text =
          @"Airfix Dogfighter reconstruction\nInput pipeline failed";
    } else if (reason == AirfixInputPauseReasonControllerDisconnected) {
      if (![self isSettingsPanelOpen]) {
        [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
        self.resumeButton.hidden = !self.renderer.missionWorldRoomInstalled;
      }
      self.touchControlsView.hidden = YES;
      self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                              @"Controller disconnected; gameplay paused";
    } else if (reason == AirfixInputPauseReasonInputOverflow) {
      if (![self isSettingsPanelOpen]) {
        [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
        self.resumeButton.hidden = !self.renderer.missionWorldRoomInstalled;
      }
      self.touchControlsView.hidden = YES;
      self.statusLabel.text = @"Airfix Dogfighter reconstruction\n"
                              @"Input stream reset; gameplay paused";
    }
    return;
  }

  if (_session.lifecycleState() == airfix::runtime::LifecycleState::running) {
    _audioBackend->setActive(false);
    _session.pause();
    metalView.paused = YES;
    [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
    self.touchControlsView.hidden = YES;
    self.resumeButton.hidden = NO;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\n"
        @"Gameplay paused; choose settings with the stick, A opens, B resumes";
    return;
  }

  [self resumeGameplay];
}

- (void)handleAudioForcedPause:
    (const airfix::ios::AirfixIOSAudioPauseReason)reason {
  _session.pause();
  [self.inputCoordinator resetForGameplayBoundary];
  ((MTKView *)self.view).paused = YES;
  if (![self isSettingsPanelOpen]) {
    [self.inputCoordinator setInputContext:AirfixNativeInputContextMenu];
    self.touchControlsView.hidden = YES;
    self.resumeButton.hidden = !self.renderer.missionWorldRoomInstalled;
  }

  NSString *detail = @"Audio interrupted; gameplay paused";
  switch (reason) {
  case airfix::ios::AirfixIOSAudioPauseReason::interruption:
    detail = @"Audio interrupted; gameplay paused";
    break;
  case airfix::ios::AirfixIOSAudioPauseReason::outputRouteLost:
    detail = @"Audio output changed; gameplay paused";
    break;
  case airfix::ios::AirfixIOSAudioPauseReason::mediaServicesReset:
    detail = @"Audio services restarted; gameplay paused";
    break;
  }
  self.statusLabel.text =
      [@"Airfix Dogfighter reconstruction\n" stringByAppendingString:detail];
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator *)coordinator
    didChangeControllerState:(AirfixGameControllerState *)state {
  if (coordinator != self.inputCoordinator) {
    return;
  }
  (void)state;
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator *)coordinator
        didUpdateUIInput:(AirfixUIInputSnapshot *)input {
  if (coordinator != self.inputCoordinator) {
    return;
  }
  if (self.controllerCalibrationPanel != nil) {
    [self.controllerCalibrationPanel consumeUIInputSnapshot:input];
    return;
  }
  if (self.renderSettingsPanel != nil) {
    [self.renderSettingsPanel consumeUIInputSnapshot:input];
    return;
  }
  if (input.cancelPressed) {
    [self resumeGameplay];
    return;
  }

  if (magnitudeAtMost(input.navigationY,
                      airfix::input::uiNavigationReleaseQ15)) {
    _pausedSettingsNavigationLatched = NO;
  }
  if (!_pausedSettingsNavigationLatched &&
      input.navigationY >= airfix::input::uiNavigationActuationQ15) {
    _pausedSettingsNavigationLatched = YES;
    [self setPausedSettingsSelection:0U announce:YES];
  } else if (!_pausedSettingsNavigationLatched &&
             input.navigationY <= -airfix::input::uiNavigationActuationQ15 &&
             self.controllerCalibrationButton.enabled) {
    _pausedSettingsNavigationLatched = YES;
    [self setPausedSettingsSelection:1U announce:YES];
  }
  if (input.tabPreviousPressed) {
    [self setPausedSettingsSelection:0U announce:YES];
  }
  if (input.tabNextPressed && self.controllerCalibrationButton.enabled) {
    [self setPausedSettingsSelection:1U announce:YES];
  }
  if (input.confirmPressed) {
    if (_pausedSettingsSelection == 1U &&
        self.controllerCalibrationButton.enabled) {
      [self showControllerCalibration];
    } else {
      [self showRenderSettings];
    }
  }
}

- (void)inputCoordinator:(AirfixIOSInputCoordinator *)coordinator
    didUpdateDiagnostics:(AirfixInputDiagnostics *)diagnostics {
  if (coordinator != self.inputCoordinator) {
    return;
  }
  [self updateDiagnosticsLabelWithInputDiagnostics:diagnostics];
}

- (void)updateDiagnosticsLabelWithInputDiagnostics:
    (AirfixInputDiagnostics *)diagnostics {
  if (diagnostics == nil) {
    return;
  }
  NSString *source = @"none";
  if (diagnostics.lastMeaningfulSource == AirfixInputSourceTouch) {
    source = @"touch";
  } else if (diagnostics.lastMeaningfulSource == AirfixInputSourceController) {
    source = @"controller";
  }
  NSString *fire = diagnostics.primaryHeld ? @"down" : @"up";
  NSString *controller =
      diagnostics.isControllerConnected ? @"connected" : @"none";
  const auto &simulation = _playerAircraftPresentation.state();
  const std::uint64_t simulationHash = _playerAircraftPresentation.stateHash();
  NSString *simulationStatus = self.simulationPipelineReady ? @"ok" : @"failed";
  NSString *profileStatus =
      self.controllerInputProfileStatus ?: @"PROFILE starting";
  self.inputDiagnosticsLabel.text = [NSString
      stringWithFormat:
          @"INPUT T%llu  B %+d  P %+d  FIRE %@\n"
          @"CONTROLLER %@  SOURCE %@\n"
          @"SIM STEP %llu  HASH %016llX  %@\n"
          @"INTENT B %+d  P %+d  FIRE %@  PRESS %llu  RELEASE %llu\n"
          @"%@",
          static_cast<unsigned long long>(diagnostics.tick), diagnostics.bank,
          diagnostics.pitch, fire, controller, source,
          static_cast<unsigned long long>(simulation.completedSteps),
          static_cast<unsigned long long>(simulationHash), simulationStatus,
          static_cast<int>(simulation.bankIntentQ15),
          static_cast<int>(simulation.pitchIntentQ15),
          simulation.primaryFireHeld ? @"down" : @"up",
          static_cast<unsigned long long>(simulation.primaryFirePressCount),
          static_cast<unsigned long long>(simulation.primaryFireReleaseCount),
          profileStatus];
}

@end
