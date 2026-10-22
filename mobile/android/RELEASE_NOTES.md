**OksaSDR for Android — beta 13.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 93), from the same code.

### New in beta 13
- **Every mode the radio offers** is in the mode chooser: DIGL, SAM, NFM, DFM and RTTY too, and FreeDV's FDVU / FDVL when the radio has the FreeDV waveform.
- **Menus close on a second tap**, and a tap outside one only closes it: it no longer moves the frequency. On a phone, Close sits beside PTT while Controls are open.
- **DAX switch on the TX tab**: shows whether the radio sends DAX (a digital-mode program) or the microphone, and turns DAX off if it was left on after FT8.
- **QRZ.com sign-in result** on the Log tab when you save the account.

Still new from betas 11–12: zoom stops at the radio's narrowest span instead of getting stuck; the Log tab (QRZ.com / prefix lookup, worked before / dupe, POTA / WWFF, ADIF sharing).

### Install or update
1. Download `OksaSDR-android-beta13.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 13 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
