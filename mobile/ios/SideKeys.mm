#include "SideKeys.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

namespace AetherSDR::mobile {

namespace {

std::function<void(SideKey, bool)> s_callback;
NSHashTable<GCKeyboard*>* s_keyboards = nil;  // every keyboard attached
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
        s_keyboards = [NSHashTable weakObjectsHashTable];
    }
    [s_keyboards addObject:keyboard];
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

} // namespace

bool sideKeysAvailable()
{
    return true;
}

void setSideKeysSuspended(bool suspended)
{
    if (suspended == s_suspended) {
        return;
    }
    s_suspended = suspended;
    for (GCKeyboard* keyboard in s_keyboards.allObjects) {
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

bool runningAsIosAppOnMac()
{
    if (@available(iOS 14.0, *)) {
        return NSProcessInfo.processInfo.isiOSAppOnMac;
    }
    return false;
}

void startSideKeys(std::function<void(SideKey key, bool pressed)> callback)
{
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
