**OksaSDR for Android — beta 7.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 82), from the same code.

### New in beta 7
- **Spectrum / waterfall divider** — drag the line between them (the short bar, right of centre) to give either more room; double-tap it for the automatic split.
- **Transverter (XVTR) bands** — transverters set up in the radio (SmartSDR or the AetherSDR desktop: Radio Setup → XVTR) show as band buttons after 6 m; the radio tunes the RF frequency, uses the XVTR port and does the IF.
- **Smoother audio** on networks that deliver packets out of order (some VPN and SmartLink paths): a late packet no longer causes a burst of filler audio.

Still new from beta 6: SmartLink stays signed in and reconnects after *Disconnect*; the outer screen in landscape fits.

### Install or update
1. Download `OksaSDR-android-beta7.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 7 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
