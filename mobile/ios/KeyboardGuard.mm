#include "KeyboardGuard.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <utility>

static std::function<bool(double, double)> s_keeps;

// Qt's gesture has no delegate of its own (Qt 6.12: QIOSKeyboardListener
// adopts the protocol and sets none). This one only says which touches the
// gesture is given; what it does with the others is unchanged.
@interface OksaKeyboardGuard : NSObject <UIGestureRecognizerDelegate>
@end

@implementation OksaKeyboardGuard

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)recognizer shouldReceiveTouch:(UITouch*)touch
{
    (void)recognizer;
    UIView* view = touch.view;
    // Only a touch on one of Qt's own views (QUIView and its Metal subclass)
    // is in the window's coordinates.
    Class qtView = NSClassFromString(@"QUIView");
    if (!s_keeps || !view || !qtView || ![view isKindOfClass:qtView]) {
        return YES;
    }
    const CGPoint at = [touch locationInView:view];
    return s_keeps(at.x, at.y) ? NO : YES;
}

@end

namespace AetherSDR::mobile {

int guardKeyboardFromPresses(std::function<bool(double x, double y)> keeps)
{
    // Kept for the app's life: a gesture does not retain its delegate.
    static OksaKeyboardGuard* guard = [[OksaKeyboardGuard alloc] init];
    if (keeps) {
        s_keeps = std::move(keeps);
    }
    int asking = 0;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* window in static_cast<UIWindowScene*>(scene).windows) {
            for (UIGestureRecognizer* recognizer in window.gestureRecognizers) {
                if (![NSStringFromClass([recognizer class]) containsString:@"QIOSKeyboardListener"]) {
                    continue;
                }
                if (recognizer.delegate == nil) {
                    recognizer.delegate = guard;
                }
                if (recognizer.delegate == guard) {
                    ++asking;
                }
            }
        }
    }
    return asking;
}

} // namespace AetherSDR::mobile
