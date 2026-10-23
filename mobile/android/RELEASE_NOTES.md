**OksaSDR for Android — beta 18.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 98), from the same code.

### New in beta 18
- **FT8 / FT4 decode every DIGU / DIGL slice at once**, each on its own DAX channel; every row starts with its slice letter.
- **WSJT-X colours** in the decode list: green CQ, red your call, yellow your own TX, pink to magenta a new DXCC, amber a new CQ zone, red-pink a new continent (lighter = new on this band); grey = already in your log on this band and mode.
- **Tap a call to answer it:** TX, the active slice and the display move to that slice and the app starts calling.
- FT8 / FT4 follow the TX slice: adding slices no longer takes TX away from a running CQ.

Still new from betas 14–17: FT8 and FT4 in the app (Controls › Digi) with the TX band dragged on the display, RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import, the 3D view fixed over SmartLink.

### Install or update
1. Download `OksaSDR-android-beta18.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 18 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
