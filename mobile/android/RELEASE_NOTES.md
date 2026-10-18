**OksaSDR for Android — beta 4.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 79), from the same code.

### New in beta 4
- **CW paddles** — CW tab → Key → *Paddles* puts dit and dah in PTT's place; hold both for iambic squeeze. The app times the elements (iambic A/B and swapped paddles from Radio Setup). A USB paddle adapter (Vail, VBand) works as a keyboard: left/right Ctrl, or [ and ].
- **CW decoder** — far less band noise read as letters (Sens slider, 8–45 WPM); weak stations decode better.
- **Tuning by drag** lands on the tuning step, so the frequency ends round.
- **Filter edges** — drag a passband edge to set any width.
- **Notches** — drag a notch along the spectrum to move it.
- **Spot filters** — View → Spots: CW, phone or digital only, and only the newest (5, 15 or 30 minutes).
- The log notes network changes (Wi-Fi / mobile data), to explain a dropped remote session.

### Install or update
1. Download `OksaSDR-android-beta4.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 4 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
