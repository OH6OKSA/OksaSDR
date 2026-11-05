#pragma once

namespace AetherSDR::mobile {

// The system's text size as a factor of its default: iOS Dynamic Type
// (Settings › Display & Brightness › Text Size, and Accessibility's larger
// sizes), Android's font scale (Settings › Display › Font size). 1.0 at the
// default; a desktop build reads OKSASDR_TEXT_SCALE (for the render tool).
// Read again when the app comes back to the front, as the setting can change
// while it is away.
double systemTextScale();

// Below 1.0 when the screen is narrower than the phone layout's 375 points,
// which on a supported iPhone only iOS Display Zoom (Larger Text) makes: 320
// points across. The app is then laid out at 375 points and drawn scaled to
// the screen (QT_SCALE_FACTOR), where it was cut off at the right (Evgeny
// R0ACS). Read once, before the application starts; 1.0 elsewhere.
double narrowScreenScale();

} // namespace AetherSDR::mobile
