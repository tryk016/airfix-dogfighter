#import "AirfixGameViewController.h"

#import <MetalKit/MetalKit.h>

#include "airfix/runtime/AppSession.hpp"

@interface AirfixGameViewController () <MTKViewDelegate> {
    airfix::runtime::AppSession _session;
}
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) UILabel* statusLabel;
@end

@implementation AirfixGameViewController

- (void)loadView {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTKView* metalView = [[MTKView alloc] initWithFrame:CGRectZero device:device];
    metalView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
        UIViewAutoresizingFlexibleHeight;
    metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    metalView.clearColor = MTLClearColorMake(0.035, 0.055, 0.085, 1.0);
    metalView.delegate = self;
    metalView.preferredFramesPerSecond = 60;
    self.commandQueue = [device newCommandQueue];
    self.view = metalView;

    UILabel* label = [[UILabel alloc] initWithFrame:CGRectZero];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.numberOfLines = 0;
    label.textAlignment = NSTextAlignmentCenter;
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    label.text = @"Game data not installed\nThe private AFPACK importer is not enabled yet.";
    label.accessibilityLabel = @"Game data not installed";
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
    ((MTKView*)self.view).paused = NO;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView*)view {
    MTLRenderPassDescriptor* descriptor = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (descriptor == nil || drawable == nil || self.commandQueue == nil) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
