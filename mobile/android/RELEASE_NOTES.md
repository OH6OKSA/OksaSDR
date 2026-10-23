**OksaSDR for Android — beta 14.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 94), from the same code.

### New in beta 14
- **FT8 and FT4 in the app** (Controls › Digi): decode the band, tap a station to answer it or press CQ; the app runs the QSO (reports, RR73 / 73) and logs it. No computer, DAX or WSJT-X needed.
- **RADE digital voice** (FreeDV) in the app, on any Flex radio: RADE on the Digi tab, then talk with PTT as usual.
- **The log in a folder you can see**: a copy in Files › Documents › OksaSDR › oksasdr-log.adi (Android 9 and 10 ask for storage permission once). **Import contacts (ADIF)** on the Log tab adds an ADIF file.
- After a hang with the 3D view on, the app starts with the 2D waterfall.

Still new from beta 13: every mode the radio offers (DIGL, SAM, NFM, RTTY, FreeDV FDVU / FDVL), menus that close on a second tap, a DAX switch on the TX tab, the QRZ.com sign-in result.

### Install or update
1. Download `OksaSDR-android-beta14.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 14 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
