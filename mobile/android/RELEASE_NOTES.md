**OksaSDR for Android — beta 6.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 81), from the same code.

### New in beta 6
- **SmartLink stays signed in** after the app restarts, and the radio connects again after *Disconnect*. If you are signed out, sign in once more; the password is never stored.
- **Outer screen in landscape** (foldables, small phones): the layout fits again, PTT and the step buttons included; the *Band* button on the waterfall picks bands there.

Still new from beta 5: Iambic A / B and Swap in the CW tab, paddles on a keyboard (Ctrl, Alt, [ ]), the CW refused notice, the break-in note.

### Install or update
1. Download `OksaSDR-android-beta6.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 6 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
