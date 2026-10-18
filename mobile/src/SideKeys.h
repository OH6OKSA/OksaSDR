#pragma once

#include <functional>

namespace AetherSDR::mobile {

// Left and right modifier keys of a hardware keyboard where Qt's key events
// cannot tell them apart. On iOS Qt passes no scan code, so left and right
// Option (or Control) arrive as one key; iOS reads them from the
// GameController framework (GCKeyboard) instead. Elsewhere Qt's scan codes
// tell the sides apart and this is unavailable.
enum class SideKey { LeftControl, RightControl, LeftOption, RightOption };

// True where startSideKeys() reports keys (iOS).
bool sideKeysAvailable();
// Calls back on the main thread with each press and release of the keys
// above while the app is in the foreground. Call once.
void startSideKeys(std::function<void(SideKey key, bool pressed)> callback);

} // namespace AetherSDR::mobile
