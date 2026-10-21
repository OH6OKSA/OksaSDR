**OksaSDR for Android — beta 11.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 88), from the same code.

### New in beta 11
- **Log tab** — quick QSO logging: type the call (or tap the spot you tuned to), see who it is (QRZ.com with your account, else the country from the prefix), worked before / dupe, reports, POTA / WWFF. Frequency, mode, power and time come from the radio.
- **Share the log** as an ADIF file (Gmail, Drive, your logbook app or TQSL for LoTW).
- **3D view** switches itself to the 2D waterfall on a tablet or phone too slow for it (it froze one tester's tablet).
- Spot labels keep clear of the RF GAIN badge on the phone.

Still new from beta 10: FM repeater memories, DAX turned off at PTT with this device's microphone, peak hold choices.

### Install or update
1. Download `OksaSDR-android-beta11.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 11 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
