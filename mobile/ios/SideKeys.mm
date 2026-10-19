#include "SideKeys.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

namespace AetherSDR::mobile {

namespace {

std::function<void(SideKey, bool)> s_callback;

// GCKeyboard reports physical keys, left and right apart; its handlers run
// on the main queue unless told otherwise.
void attach(GCKeyboard* keyboard)
{
    if (!keyboard || !keyboard.keyboardInput) {
        return;
    }
    keyboard.keyboardInput.keyChangedHandler = ^(GCKeyboardInput*, GCControllerButtonInput*, GCKeyCode keyCode, BOOL pressed) {
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
