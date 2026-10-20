**OksaSDR for Android — beta 10.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 86), from the same code.

### New in beta 10
- **FM repeater memories** — a memory keeps the shift and CTCSS tone (shown in the list, edited with press and hold), and recalling it transmits on the shifted frequency.
- **Transmit audio when the radio's DAX was left on** (e.g. by FT8 software): PTT with this device's microphone now turns DAX transmit off; before, nothing was modulated.
- **Peak hold** on the meters: *Off*, *Fast*, *Medium* (default) or *Slow*, as on the desktop, in the meter's chooser.
- **OksaSDR Users** on Facebook: questions, tips and news of new builds, https://www.facebook.com/groups/1468327561855382 (for licensed radio amateurs; give your callsign when you join).

Still new from beta 9: the FM repeater offset moves the transmitter, the Mic meter before transmitting, meters that offer only what moves, outlined notch handles.

### Install or update
1. Download `OksaSDR-android-beta10.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 10 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
