**OksaSDR for Android — beta 19.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 99), from the same code.

### New in beta 19
- **FT8 / FT4 filter:** Show All, CQ, New (new continent, CQ zone or DXCC) or New DXCC. Your call, your TX and the station you are working always show.
- FT8 / FT4 keep decoding every slot over SmartLink and slow links (when the audio came in bursts, the slots after the first could decode nothing).
- Every decoding slice gets its own DAX channel, so a new slice no longer shows the same stations twice.

Still new from betas 14–18: FT8 and FT4 in the app (Controls › Digi) on every DIGU slice with WSJT-X colours and tap-to-answer, RADE digital voice, the log in Files › Documents › OksaSDR, ADIF import.

### Install or update
1. Download `OksaSDR-android-beta19.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 19 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
