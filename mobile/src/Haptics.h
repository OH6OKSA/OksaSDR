#pragma once

namespace AetherSDR::mobile {

// A short tap felt in the hand, as the platform's own controls give one:
// Step for a step (the tuning step buttons, − / + at a slider's ends), Press
// and Release for PTT. It follows the system's own setting (iOS System
// Haptics, Android's touch feedback) and does nothing where the device has
// no haptics (iPad, Mac, desktop).
enum class Haptic { Step, Press, Release };
void haptic(Haptic kind);

} // namespace AetherSDR::mobile
