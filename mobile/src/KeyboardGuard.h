#pragma once

#include <functional>

namespace AetherSDR::mobile {

// iOS: Qt puts the on-screen keyboard away when a touch moves onto the
// keyboard's frame (its hide-the-keyboard gesture: QIOSKeyboardListener in
// the platform plugin resigns the first responder, and the text field loses
// its focus). PTT stands right above the keyboard while a call is typed in
// the Log tab, and an iPad's keyboard frame passes touches on a little under
// its top edge (build 213 in landscape: presses came in up to 13 pt inside
// the frame), so a press low on PTT, or a held finger that rolls down, took
// the keyboard away. With this the gesture is not given a touch that begins
// where keeps(x, y) is true (x, y in points in the app's window; asked on
// the main thread at the start of each touch while the keyboard is up).
// Call at start and again when the keyboard comes up: the gesture is on the
// window only once Qt has put it there. Returns how many of Qt's gestures
// now ask; 0 where there is none (Android, desktop) or none was found.
int guardKeyboardFromPresses(std::function<bool(double x, double y)> keeps);

} // namespace AetherSDR::mobile
