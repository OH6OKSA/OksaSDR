#include "SideKeys.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

#include <QLoggingCategory>
#include <QStringList>

Q_LOGGING_CATEGORY(lcKeyboard, "aether.mobile.keyboard")

// The first responder, for the Mac keyboard diagnostics: an action sent to
// nil goes to it first, and every responder takes this one.
static UIResponder* s_notedResponder = nil;

@interface UIResponder (OksaKeyRouting)
- (void)oksa_noteFirstResponder:(id)sender;
@end

@implementation UIResponder (OksaKeyRouting)
- (void)oksa_noteFirstResponder:(id)sender
{
    (void)sender;
    s_notedResponder = self;
}
@end

namespace AetherSDR::mobile {

namespace {

std::function<void(SideKey, bool)> s_callback;
// Every keyboard attached, held (this file is not ARC: an autoreleased
// table here was freed at once, and build 107 crashed on the first text
// field focused, "-[NSBundle allObjects]: unrecognized selector").
NSMutableArray<GCKeyboard*>* s_keyboards = nil;
bool s_suspended = false;
int s_seen = 0;

// GCKeyboard reports physical keys, left and right apart; its handlers run
// on the main queue unless told otherwise.
void attach(GCKeyboard* keyboard)
{
    if (!keyboard || !keyboard.keyboardInput) {
        return;
    }
    if (!s_keyboards) {
        s_keyboards = [[NSMutableArray alloc] init];  // owned, never released
    }
    if (![s_keyboards containsObject:keyboard]) {
        [s_keyboards addObject:keyboard];
    }
    if (s_suspended) {
        return;  // attached when the text field lets go
    }
    keyboard.keyboardInput.keyChangedHandler = ^(GCKeyboardInput*, GCControllerButtonInput*, GCKeyCode keyCode, BOOL pressed) {
        if (pressed) {
            ++s_seen;
        }
        SideKey key;
        if (keyCode == GCKeyCodeLeftControl) {
            key = SideKey::LeftControl;
        } else if (keyCode == GCKeyCodeRightControl) {
            key = SideKey::RightControl;
        } else if (keyCode == GCKeyCodeLeftAlt) {
            key = SideKey::LeftOption;
        } else if (keyCode == GCKeyCodeRightAlt) {
            key = SideKey::RightOption;
        } else if (keyCode == GCKeyCodeSpacebar) {
            key = SideKey::Space;
        } else {
            return;
        }
        if (s_callback) {
            s_callback(key, pressed == YES);
        }
    };
}

QString className(id object)
{
    return object ? QString::fromNSString(NSStringFromClass([object class])) : QStringLiteral("none");
}

UIResponder* firstResponder()
{
    s_notedResponder = nil;
    [UIApplication.sharedApplication sendAction:@selector(oksa_noteFirstResponder:) to:nil from:nil forEvent:nil];
    return s_notedResponder;
}

QString describe(UIResponder* responder)
{
    if (!responder) {
        return QStringLiteral("none");
    }
    QString text = className(responder);
    if ([responder isKindOfClass:[UIView class]]) {
        UIWindow* window = static_cast<UIView*>(responder).window;
        text += QStringLiteral(" in ") + className(window) + (window.isKeyWindow ? QStringLiteral(" (key)") : QString());
    } else {
        text += QStringLiteral(" before ") + className(responder.nextResponder);
    }
    return text;
}

// UIApplication's sendEvent:, noting key presses on their way in (Mac only).
using SendEvent = void (*)(id, SEL, UIEvent*);
SendEvent s_sendEvent = nullptr;
int s_presses = 0;

void sendEventNoting(id self, SEL cmd, UIEvent* event)
{
    if (event.type == UIEventTypePresses) {
        for (UIPress* press in static_cast<UIPressesEvent*>(event).allPresses) {
            // No characters: what is typed may be a password.
            if (press.phase == UIPressPhaseBegan && ++s_presses <= 3) {
                qCInfo(lcKeyboard).noquote() << QStringLiteral("keyboard: UIKit delivered key press %1 to the app; first responder %2")
                                                    .arg(s_presses)
                                                    .arg(describe(firstResponder()));
            }
        }
    }
    s_sendEvent(self, cmd, event);
}

UIViewController* topViewController()
{
    UIWindow* key = nil;
    UIWindow* any = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* window in static_cast<UIWindowScene*>(scene).windows) {
            if (window.isKeyWindow) {
                key = window;
            }
            if (!any && !window.hidden && window.rootViewController) {
                any = window;
            }
        }
    }
    UIViewController* top = (key.rootViewController ? key : any).rootViewController;
    while (top.presentedViewController) {
        top = top.presentedViewController;
    }
    return top;
}

} // namespace

bool macTextEntryAvailable()
{
    return runningAsIosAppOnMac();
}

void showMacTextEntry(const QString& title, const QString& message, const QString& text, bool secret,
                      const QString& okLabel, const QString& cancelLabel,
                      std::function<void(bool ok, const QString& text)> done)
{
    UIViewController* top = topViewController();
    if (!top) {
        qCInfo(lcKeyboard) << "keyboard: no view controller to show the text box on";
        done(false, QString());
        return;
    }
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:title.toNSString()
                                                                   message:message.toNSString()
                                                            preferredStyle:UIAlertControllerStyleAlert];
    NSString* initial = text.toNSString();
    [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
        field.text = initial;
        field.secureTextEntry = secret;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.spellCheckingType = UITextSpellCheckingTypeNo;
        field.clearButtonMode = UITextFieldViewModeWhileEditing;
        field.returnKeyType = UIReturnKeyDone;
    }];
    // Not ARC: a __block pointer is not retained by the blocks, so the
    // alert and its actions do not keep each other alive. Exactly one of the
    // two actions runs and frees the callback.
    __block UIAlertController* alertRef = alert;
    auto* callback = new std::function<void(bool, const QString&)>(std::move(done));
    UIAlertAction* cancel = [UIAlertAction actionWithTitle:cancelLabel.toNSString()
                                                     style:UIAlertActionStyleCancel
                                                   handler:^(UIAlertAction*) {
        (*callback)(false, QString());
        delete callback;
    }];
    UIAlertAction* ok = [UIAlertAction actionWithTitle:okLabel.toNSString()
                                                 style:UIAlertActionStyleDefault
                                               handler:^(UIAlertAction*) {
        NSString* typed = alertRef.textFields.firstObject.text;
        (*callback)(true, typed ? QString::fromNSString(typed) : QString());
        delete callback;
    }];
    [alert addAction:cancel];
    [alert addAction:ok];
    alert.preferredAction = ok;  // Return in the field is OK
    [top presentViewController:alert animated:YES completion:nil];
}

void startMacKeyDiagnostics()
{
    if (!runningAsIosAppOnMac() || s_sendEvent) {
        return;
    }
    Method method = class_getInstanceMethod([UIApplication class], @selector(sendEvent:));
    if (method) {
        s_sendEvent = reinterpret_cast<SendEvent>(method_setImplementation(method, reinterpret_cast<IMP>(&sendEventNoting)));
    }
}

QString macKeyRouting()
{
    if (!runningAsIosAppOnMac()) {
        return {};
    }
    QStringList windows;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* window in static_cast<UIWindowScene*>(scene).windows) {
            windows << className(window) + (window.isKeyWindow ? QStringLiteral(" key") : QString())
                           + (window.hidden ? QStringLiteral(" hidden") : QString());
        }
        windows << QStringLiteral("scene state %1").arg(static_cast<int>(scene.activationState));
    }
    return QStringLiteral("windows: %1; first responder: %2; UIKit key presses so far: %3")
        .arg(windows.join(QStringLiteral(", ")), describe(firstResponder()))
        .arg(s_presses);
}

bool runningAsIosAppOnMac()
{
    if (@available(iOS 14.0, *)) {
        return NSProcessInfo.processInfo.isiOSAppOnMac;
    }
    return false;
}

bool sideKeysAvailable()
{
    return !runningAsIosAppOnMac();  // on a Mac Qt's key events serve (startSideKeys)
}

void setSideKeysSuspended(bool suspended)
{
    if (suspended == s_suspended) {
        return;
    }
    s_suspended = suspended;
    if (!s_keyboards) {
        return;  // none attached (a Mac, or no keyboard yet)
    }
    for (GCKeyboard* keyboard in [NSArray arrayWithArray:s_keyboards]) {
        if (suspended) {
            keyboard.keyboardInput.keyChangedHandler = nil;
        } else {
            attach(keyboard);
        }
    }
}

int sideKeysSeen()
{
    return s_seen;
}


void startSideKeys(std::function<void(SideKey key, bool pressed)> callback)
{
    // On a Mac the GameController keyboard took the typed keys from Qt: no
    // text field got any (seen in the log: 0 key events reached the field,
    // GameController saw them all). There Qt's own key events serve.
    if (runningAsIosAppOnMac()) {
        return;
    }
    s_callback = std::move(callback);
    attach(GCKeyboard.coalescedKeyboard);
    // A keyboard attached later (or reattached) is a new GCKeyboard.
    [[NSNotificationCenter defaultCenter] addObserverForName:GCKeyboardDidConnectNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification* note) {
        attach((GCKeyboard*)note.object);
    }];
}

} // namespace AetherSDR::mobile
