#import "AirfixGameViewController.h"

#import "AirfixMetalRenderer.h"

#import <MetalKit/MetalKit.h>

#include "airfix/runtime/AppSession.hpp"

@interface AirfixGameViewController () {
    airfix::runtime::AppSession _session;
}
@property(nonatomic, strong) AirfixMetalRenderer* renderer;
@property(nonatomic, strong) UILabel* statusLabel;
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
    if (self.renderer == nil) {
        NSString* reason = rendererError.localizedDescription ?: @"Unknown Metal initialization error.";
        label.text = [@"Metal renderer unavailable\n" stringByAppendingString:reason];
        label.accessibilityLabel = @"Metal renderer unavailable";
    }
    else {
        label.text = @"Public Metal smoke test\nGame data not installed";
        label.accessibilityLabel = @"Public Metal smoke test; game data not installed";
    }
    self.statusLabel = label;
    [metalView addSubview:label];

    UILayoutGuide* safeArea = metalView.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [label.centerXAnchor constraintEqualToAnchor:safeArea.centerXAnchor],
        [label.centerYAnchor constraintEqualToAnchor:safeArea.centerYAnchor],
        [label.leadingAnchor constraintGreaterThanOrEqualToAnchor:safeArea.leadingAnchor
                                                          constant:24.0],
        [label.trailingAnchor constraintLessThanOrEqualToAnchor:safeArea.trailingAnchor
                                                        constant:-24.0],
    ]];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (void)applicationWillResignActive {
    _session.enterInactive();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidEnterBackground {
    _session.enterBackground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationWillEnterForeground {
    _session.enterForeground();
    ((MTKView*)self.view).paused = YES;
}

- (void)applicationDidBecomeActive {
    if (_session.lifecycleState() !=
        airfix::runtime::LifecycleState::foregroundPaused) {
        _session.enterForeground();
    }
    (void)_session.resume();
    if (self.renderer != nil) {
        ((MTKView*)self.view).paused = NO;
    }
}

@end
