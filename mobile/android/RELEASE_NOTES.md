**OksaSDR for Android — beta 9.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 85), from the same code.

### New in beta 9
- **FM repeater** — the offset now moves the transmitter (it sent on the receive frequency before).
- **Mic level before going on the air** — choose *Mic* for the left meter; while Controls → TX is open it moves in receive too. Nothing is sent before PTT.
- **Meters** offer only what moves: receive *S*, *Mic*, *PA temp*, *Volts*; *Power*, *SWR*, *ALC* and *Comp* in transmit.
- **Notch handles** are larger and outlined, filled while TNF is on and hollow while off, so they stand out from the waterfall.
- **SmartLink** keeps the radio's audio registration alive as SmartSDR does; a refused connection is retried.

Still new from beta 8: FM repeater offset and CTCSS, 439.000 typed as MHz, the lit band button.

### Install or update
1. Download `OksaSDR-android-beta9.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 9 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
