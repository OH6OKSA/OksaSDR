**OksaSDR for Android — beta 17.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 97), from the same code.

### New in beta 17
- **FT8 / FT4 stay on their own slice** with several slices open: switching the active slice no longer breaks decoding. That slice gets its own DAX channel and becomes the TX slice when the app transmits. Tap inside another DIGU slice's passband (or press a band button there) to move FT8 to it.
- The filter edge lines are back in FT8 / FT4; on tablets the waterfall fills the space PTT left.
- The 3D view no longer switches itself to 2D right after connecting over SmartLink (turn 3D on again in View if it did).
- CQ rows in the decode list are bold blue.

Still new from betas 14–16: FT8 and FT4 in the app (Controls › Digi) with the TX band dragged on the display, RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import.

### Install or update
1. Download `OksaSDR-android-beta17.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 17 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
