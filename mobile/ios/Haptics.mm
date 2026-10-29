#include "Haptics.h"

#import <UIKit/UIKit.h>

namespace AetherSDR::mobile {

namespace {

// Made once and kept for the run, so a tap comes without a generator's
// start-up delay. Owned (alloc/init): this file is not built with ARC. Only
// touched on the main thread.
UISelectionFeedbackGenerator* s_step = nil;
UIImpactFeedbackGenerator* s_press = nil;
UIImpactFeedbackGenerator* s_release = nil;

} // namespace

void haptic(Haptic kind)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (kind) {
        case Haptic::Step:
            if (!s_step) {
                s_step = [[UISelectionFeedbackGenerator alloc] init];
            }
            [s_step selectionChanged];
            [s_step prepare];
            break;
        case Haptic::Press:
            if (!s_press) {
                s_press = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
            }
            [s_press impactOccurred];
            [s_press prepare];
            break;
        case Haptic::Release:
            if (!s_release) {
                s_release = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
            }
            [s_release impactOccurred];
            break;
        }
    });
}

} // namespace AetherSDR::mobile
