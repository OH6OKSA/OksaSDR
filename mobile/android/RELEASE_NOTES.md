**OksaSDR for Android — beta 15.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 95), from the same code.

### New in beta 15
- **FT8 / FT4: pick your TX frequency on the waterfall**: tap inside the passband to set the TX audio frequency. A 500 Hz scale and a red TX band show where the tones go; the band buttons (and Zoom) fit the display to the 4 kHz audio band.

Still new from beta 14: FT8 and FT4 in the app (Controls › Digi), RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import, a 2D start after a 3D hang.

### Install or update
1. Download `OksaSDR-android-beta15.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 15 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
