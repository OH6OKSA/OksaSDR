**OksaSDR for Android — beta 5.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 80), from the same code.

### New in beta 5
- **CW tab** — with *Paddles*, Iambic A / B and Swap are now in the tab itself, and the dit and dah pads' labels follow Swap.
- **Paddles on a keyboard** — pick which two keys are dit (left) and dah (right): **Ctrl**, now the default, for a PC keyboard or a USB paddle adapter (Vail, VBand); **Alt** for keyboards with no right Ctrl; **[ ]** only on English layouts.
- **CW refused** — if the radio will not key CW, the app says why. If it blames another client while none is on air, restart the radio.
- Keying with **Break-in** off now tells you why nothing goes on air.

Still new from beta 4: CW paddles on the screen, a quieter CW decoder, drag tuning on the step, filter edge and notch drags, spot filters.

### Install or update
1. Download `OksaSDR-android-beta5.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 5 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
