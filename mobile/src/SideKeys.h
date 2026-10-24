#pragma once

#include <functional>

namespace AetherSDR::mobile {

// Hardware keyboard keys Qt's key events do not report well enough. On iOS
// Qt passes no scan code, so left and right Option (or Control) arrive as
// one key, and a held Space comes with a release after about 0.1 s (seen on
// an iPad with a Magic Keyboard), which ended Space PTT at once. iOS reads
// these from the GameController framework (GCKeyboard) instead. Elsewhere
// Qt's events do the job and this is unavailable.
enum class SideKey { LeftControl, RightControl, LeftOption, RightOption, Space };

// True where startSideKeys() reports keys (iOS).
bool sideKeysAvailable();
// Calls back on the main thread with each press and release of the keys
// above while the app is in the foreground. Call once.
void startSideKeys(std::function<void(SideKey key, bool pressed)> callback);
// While a text field has focus the GCKeyboard handlers are taken off: on a
// Mac (the iPad app there) GameController kept the typed keys from the text
// fields, so nothing could be written. Back on when focus leaves.
void setSideKeysSuspended(bool suspended);
// Keys GCKeyboard has seen (all of them, not only the side keys), for the
// keyboard diagnostics in the log.
int sideKeysSeen();
// The iPad app running on a Mac (Apple silicon).
bool runningAsIosAppOnMac();

} // namespace AetherSDR::mobile
