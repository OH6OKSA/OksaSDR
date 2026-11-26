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

#ifdef Q_OS_ANDROID
// Android (android/ScreenAwake.cpp), Auto with a Bluetooth headset: its
// hands-free route taken again, as a connect takes it, after a transmission
// (roadmap 144); returns the route, or "" when no headset route is in use.
QString reassertHeadsetRoute();
// The headset's state for the log: Android's communication device, the
// hands-free link up or down, its media audio on or off, the audio mode.
QString headsetState();
// Whether Auto has a Bluetooth headset's hands-free route while connected.
bool headsetInUse();
// Whether that route is really had: Android's communication device is the
// headset and its hands-free link is up (roadmap 201).
bool headsetRouteOk();
// The headset's route let go, to be taken again (reassertHeadsetRoute) a
// moment later: down and up, as Audio output › Speaker and back to Auto
// does by hand, which brought receive back on the user's phone.
void dropHeadsetRoute();
// The route worked out anew from what is connected now (a headset switched
// off while it was the route).
void reapplyAudioRoute();
// Whether Android lists a Bluetooth headset as a call device now: tells a
// headset that is gone from one Android gave no route this time (roadmap
// 214).
bool headsetListed();
// Android's audio mode (AudioManager.MODE_*): 1 ringing, 2 a phone call,
// 4 call screening; during those the headset's route is the call's.
int androidAudioMode();
// The app's own call mode taken again for the headset's route, just before
// the microphone held for the link is closed: the link then stays up
// through the closing (roadmap 218).
void holdHeadsetMode();
#endif

} // namespace AetherSDR::mobile
