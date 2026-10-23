**OksaSDR for Android — beta 20.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 100), from the same code.

### New in beta 20
- **Fixed:** with several slices decoding on a busy band the app could stop decoding and then freeze after a few minutes.
- **FT8 / FT4 at full power:** the audio drove the radio 6 dB short, so the power out stayed well below the TX tab's power. **TX level** on the Digi tab (0 to −20 dB, default 0 dB) backs it off if you want.
- A tapped decode flashes yellow, so you see the answer took.
- AGC follows the mode when the app changes it: fast for CW and data, slow for voice; your own choice is kept per kind of mode.

Still new from betas 14–19: FT8 and FT4 in the app (Controls › Digi) on every DIGU slice with WSJT-X colours, the New / New DXCC filter and tap-to-answer, RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import.

### Install or update
1. Download `OksaSDR-android-beta20.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 20 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
