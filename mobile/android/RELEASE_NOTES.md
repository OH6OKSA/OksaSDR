**OksaSDR for Android — beta 8.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 84), from the same code.

### New in beta 8
- **FM repeater** — TX tab in FM modes: offset *Simplex* / *−* / *+* with the shift (0.6, 1.6, 5.0, 7.6 MHz or typed) and a **CTCSS** tone on transmit.
- **Transverter bands** — type *439.000* to tune 439 MHz (it read as kHz before). With an FM step (e.g. 12.5 kHz) the step buttons move by the step, 100 kHz, 1 MHz and 10 MHz.
- The band button of the band you are on is lit.

Still new from beta 7: the spectrum / waterfall divider, transverter bands, smoother audio on networks that reorder packets.

### Install or update
1. Download `OksaSDR-android-beta8.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 8 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
