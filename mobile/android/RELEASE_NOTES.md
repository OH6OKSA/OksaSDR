**OksaSDR for Android — beta 23.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 105), from the same code.

### New in beta 23
- **Wavelog / Cloudlog** (Log tab): every QSO you log, by hand or FT8 / FT4, also goes to your online logbook; queued while offline. Save checks the API key and says whether it was accepted.
- **PSK Reporter** (Digi tab, off by default): reports the stations you decode, as WSJT-X does.
- **Transverters** in Radio Setup › XVTR: add, edit and remove them.
- FT8 / FT4: a QSO between two nonstandard calls now completes, without reports; the green RX mark is always shown.
- Tablets and foldables: the controls panel no longer runs off the right edge where it does not fit beside the display (it opens from Controls).

Still new from betas 14–22: FT8 and FT4 in the app (Controls › Digi) on every DIGU slice with WSJT-X colours, RX / TX marks, the state banner, Keep CQ and CQ slot, the New / New DXCC filter, tap-to-answer, full power with TX level, PROC off for FT8, AGC by mode, RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import.

### Install or update
1. Download `OksaSDR-android-beta23.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 23 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
