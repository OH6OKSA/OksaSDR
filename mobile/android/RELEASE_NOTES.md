**OksaSDR for Android — beta 12.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 92), from the same code.

### New in beta 12
- **Zoom no longer gets stuck**: zooming in stops at the radio's narrowest span (1.2 kHz). Zoomed in past it (easy in CW), the span stayed stuck until a band change.

Still new from beta 11: the **Log tab** (quick QSO logging with QRZ.com / prefix lookup, worked before / dupe, POTA / WWFF, sharing the log as an ADIF file), the 3D view falling back to 2D on a slow device, spot labels clear of the RF GAIN badge.

### Install or update
1. Download `OksaSDR-android-beta12.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 12 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
