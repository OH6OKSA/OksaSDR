#pragma once

#include <QString>

#include <functional>

namespace AetherSDR::mobile {

// iOS: one play-and-record audio session for the whole app, set up before
// any audio starts. Receive audio keeps playing in the background (with the
// "audio" background mode), the microphone works without a category switch
// mid-stream, output goes to the speaker (or Bluetooth / headphones), and
// the session comes back after a call or Siri. A no-op elsewhere.
void configureAudioSession();

// Called (on the main thread) whenever the session becomes active again
// after an interruption or a launch in the background, so the audio that
// could not open meanwhile can be restarted.
void setAudioSessionReactivatedHandler(std::function<void()> handler);

// Where receive audio plays (Audio › Audio output), from the next moment it
// can: "Auto" a connected Bluetooth headset's hands-free link with its
// microphone, else the speaker; "Speaker" the device's own speaker and
// microphone even with Bluetooth connected; "Bluetooth" a Bluetooth device's
// media link (better sound than hands-free) with the device's microphone.
// iOS (AudioSession.mm) and Android (android/ScreenAwake.cpp, while a radio
// is connected); a no-op elsewhere.
void setAudioOutputMode(const QString& mode);

} // namespace AetherSDR::mobile
