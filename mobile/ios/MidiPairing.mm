#include "MidiPairing.h"

#import <CoreAudioKit/CoreAudioKit.h>
#import <UIKit/UIKit.h>

// Closes the pairing sheet from its Done button.
@interface OksaMidiPairingCloser : NSObject
- (void)done:(id)sender;
@end

namespace AetherSDR::mobile {

namespace {

UIViewController* topViewController()
{
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* w in static_cast<UIWindowScene*>(scene).windows) {
            if (w.isKeyWindow) {
                window = w;
                break;
            }
        }
        if (window) {
            break;
        }
    }
    UIViewController* top = window.rootViewController;
    while (top.presentedViewController) {
        top = top.presentedViewController;
    }
    return top;
}

} // namespace

bool runningOnMac()
{
    return NSProcessInfo.processInfo.isiOSAppOnMac;
}

bool bluetoothMidiPairingAvailable()
{
    return !runningOnMac();
}

bool showBluetoothMidiPairing()
{
    if (!bluetoothMidiPairingAvailable()) {
        return false;
    }
    UIViewController* top = topViewController();
    if (!top) {
        return false;
    }
    // Not ARC: the closer is kept for the app's life (the button holds its
    // target weakly); the controllers are autoreleased, the presenting
    // controller keeps the navigation controller while it is shown.
    static OksaMidiPairingCloser* closer = [[OksaMidiPairingCloser alloc] init];
    CABTMIDICentralViewController* central = [[[CABTMIDICentralViewController alloc] init] autorelease];
    central.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                       target:closer
                                                       action:@selector(done:)] autorelease];
    UINavigationController* nav = [[[UINavigationController alloc] initWithRootViewController:central] autorelease];
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    [top presentViewController:nav animated:YES completion:nil];
    return true;
}

} // namespace AetherSDR::mobile

@implementation OksaMidiPairingCloser
- (void)done:(id)sender
{
    (void)sender;
    UIViewController* top = AetherSDR::mobile::topViewController();
    if ([top isKindOfClass:[UINavigationController class]]
        && [static_cast<UINavigationController*>(top).viewControllers.firstObject
               isKindOfClass:[CABTMIDICentralViewController class]]) {
        [top dismissViewControllerAnimated:YES completion:nil];
    }
}
@end
