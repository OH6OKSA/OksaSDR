#pragma once

namespace AetherSDR::mobile {

// Keeps the display from auto-locking while a radio is connected (iOS idle
// timer; Android keep-screen-on flag). On Android it also runs the
// connection's foreground service, so receive continues in the background.
// A no-op on platforms without either.
void setKeepScreenAwake(bool awake);

// Asks the system for time to finish a reconnect in the background (iOS:
// a background task, about 30 s). The app otherwise runs in the background
// only while it plays receive audio, and that stops when a session drops, so
// iOS suspended it halfway through the reconnect (roadmap 84 j). Held again
// while held does nothing; released when the session is up or given up.
// A no-op elsewhere (Android's foreground service keeps the app running).
void setBackgroundTimeHeld(bool held);

} // namespace AetherSDR::mobile
