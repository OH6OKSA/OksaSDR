#pragma once

namespace AetherSDR::mobile {

// The system's text size as a factor of its default: iOS Dynamic Type
// (Settings › Display & Brightness › Text Size, and Accessibility's larger
// sizes), Android's font scale (Settings › Display › Font size). 1.0 at the
// default; a desktop build reads OKSASDR_TEXT_SCALE (for the render tool).
// Read again when the app comes back to the front, as the setting can change
// while it is away.
double systemTextScale();

} // namespace AetherSDR::mobile
