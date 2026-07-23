#import "AirfixGameViewController.h"

#import "AirfixContentCoordinator.h"
#import "AirfixMetalRenderer.h"
#import "AirfixWorldRoomSnapshot.h"

#import <MetalKit/MetalKit.h>

#include "AirfixWorldRoomSnapshot+Private.hpp"
#include "airfix/runtime/AppSession.hpp"

#include <utility>

namespace {

// First private-render smoke selection, verified against the owner's external
// v1.01 content. Mission/campaign selection will replace this explicit request.
NSString* const kInitialWorldLogicalPath = @"Game/Worlds/axis_1.world";
constexpr NSUInteger kInitialPhysicalRoom = 1U;

} // namespace

@interface AirfixGameViewController () <AirfixContentCoordinatorDelegate> {
    airfix::runtime::AppSession _session;
    dispatch_queue_t _rendererPreparationQueue;
}
@property(nonatomic, strong) AirfixMetalRenderer* renderer;
@property(nonatomic, strong) UILabel* statusLabel;
@property(nonatomic, strong) AirfixContentCoordinator* contentCoordinator;
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
        label.accessibilityLabel = @"Metal renderer unavailable";
    }
    else {
        label.text = @"Airfix Dogfighter reconstruction\nMetal renderer ready";
        label.accessibilityLabel =
            @"Airfix Dogfighter reconstruction; Metal renderer ready";
    }
    self.statusLabel = label;

    self.contentCoordinator = [[AirfixContentCoordinator alloc]
        initWithPresentingViewController:self];
    self.contentCoordinator.delegate = self;
    _rendererPreparationQueue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.renderer-preparation",
        DISPATCH_QUEUE_SERIAL);
    [self.contentCoordinator requestWorldAtLogicalPath:kInitialWorldLogicalPath
                                         physicalRoom:kInitialPhysicalRoom];
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
    ]];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self.contentCoordinator start];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (void)applicationWillResignActive {
    [self.contentCoordinator applicationWillResignActive];
    _session.enterInactive();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidEnterBackground {
    [self.contentCoordinator applicationDidEnterBackground];
    _session.enterBackground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationWillEnterForeground {
    [self.contentCoordinator applicationWillEnterForeground];
    _session.enterForeground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidBecomeActive {
    if (_session.lifecycleState() !=
        airfix::runtime::LifecycleState::foregroundPaused) {
        _session.enterForeground();
    }
    [self.contentCoordinator applicationDidBecomeActive];
    const bool resumed = _session.resume();
    ((MTKView*)self.view).paused =
        !(resumed && self.renderer.worldRoomInstalled);
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
        // room has also passed portable loading and the two-phase Metal swap.
        contentState = airfix::runtime::ContentState::validating;
        break;
    case AirfixContentReadinessRejected:
        contentState = airfix::runtime::ContentState::rejected;
        break;
    }
    _session.setContentState(contentState);
    if (contentState != airfix::runtime::ContentState::ready) {
        ((MTKView*)self.view).paused = YES;
    }
}

- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didBeginLoadingWorldAtLogicalPath:(NSString*)worldLogicalPath
        physicalRoom:(NSUInteger)physicalRoom {
    (void)coordinator;
    (void)worldLogicalPath;
    (void)physicalRoom;
    _session.setContentState(airfix::runtime::ContentState::validating);
    ((MTKView*)self.view).paused = YES;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nLoading private room...";
}

- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didLoadWorldRoomSnapshot:(AirfixWorldRoomSnapshot*)snapshot {
    (void)coordinator;
    AirfixMetalRenderer* renderer = self.renderer;
    if (renderer == nil || snapshot == nil) {
        _session.setContentState(airfix::runtime::ContentState::rejected);
        ((MTKView*)self.view).paused = YES;
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
                auto room = airfix::ios::takeLoadedWorldRoom(snapshot);
                preparedRoom =
                    [renderer prepareLoadedRoom:std::move(room)
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
                    isWorldRoomSnapshotCurrent:snapshot]) {
                return;
            }
            if (preparedRoom == nil) {
                strongSelf->_session.setContentState(
                    airfix::runtime::ContentState::rejected);
                ((MTKView*)strongSelf.view).paused = YES;
                NSString* reason = preparationError.localizedDescription;
                strongSelf.statusLabel.text = reason.length == 0U
                    ? @"Airfix Dogfighter reconstruction\nRoom preparation failed"
                    : [@"Airfix Dogfighter reconstruction\n"
                        stringByAppendingString:reason];
                return;
            }

            // Main serializes the final gate check and the constant-time
            // pointer swap, so an invalidation cannot interleave between them.
            if (![strongSelf.contentCoordinator
                    isWorldRoomSnapshotCurrent:snapshot]) {
                return;
            }
            NSError* publicationError = nil;
            if (![renderer publishPreparedRoom:preparedRoom
                                         error:&publicationError]) {
                strongSelf->_session.setContentState(
                    airfix::runtime::ContentState::rejected);
                ((MTKView*)strongSelf.view).paused = YES;
                NSString* reason = publicationError.localizedDescription;
                strongSelf.statusLabel.text = reason.length == 0U
                    ? @"Airfix Dogfighter reconstruction\nRoom publication failed"
                    : [@"Airfix Dogfighter reconstruction\n"
                        stringByAppendingString:reason];
                return;
            }

            strongSelf->_session.setContentState(
                airfix::runtime::ContentState::ready);
            strongSelf.statusLabel.text = [NSString stringWithFormat:
                @"Airfix Dogfighter reconstruction\n"
                 @"Private room ready: %lu meshes, %lu textures, %lu draws",
                static_cast<unsigned long>(snapshot.meshCount),
                static_cast<unsigned long>(snapshot.textureCount),
                static_cast<unsigned long>(snapshot.drawCommandCount)];
            const bool resumed =
                UIApplication.sharedApplication.applicationState ==
                    UIApplicationStateActive &&
                strongSelf->_session.lifecycleState() ==
                    airfix::runtime::LifecycleState::foregroundPaused &&
                strongSelf->_session.resume();
            ((MTKView*)strongSelf.view).paused =
                !(resumed && renderer.worldRoomInstalled);
        });
    });
}

- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didFailLoadingWorldAtLogicalPath:(NSString*)worldLogicalPath
        physicalRoom:(NSUInteger)physicalRoom {
    (void)coordinator;
    (void)worldLogicalPath;
    (void)physicalRoom;
    _session.setContentState(airfix::runtime::ContentState::rejected);
    ((MTKView*)self.view).paused = YES;
    self.statusLabel.text =
        @"Airfix Dogfighter reconstruction\nPrivate room could not be loaded";
}

@end
