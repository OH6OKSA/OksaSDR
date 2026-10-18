**OksaSDR for Android — beta 3.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 78), from the same code.

### New in beta 3
- **Mic level now works with the phone's microphone** — TX tab → Mic level is the phone microphone's own gain, −20 to +30 dB, and the Mic meter follows it. The radio's mic level never acted on the phone's microphone (the radio has no gain stage for its network input), so speech could stay low with only a few watts out. Set it so speech peaks near −10 dB on the Mic meter while you transmit.
- **Clearer buttons** — a button that is on is lit blue, the others dark.
- **PTT as a switch** — TX tab → PTT → *Tap on / off*: tap to transmit, tap again to stop; it stops by itself after 3 minutes.
- **CW sidetone for memories and CW text** while they are sent.
- **Busiest threads** in Network & performance, to see whether one part of the app is at its limit.

### Since beta 2 (still new to many)
- Spots and the DX cluster, notch filters (TNF), processing of the phone's microphone (gate, EQ, compressor), memory editing, voice keyer *Listen*; beta 2 also asks for the microphone on the first connect.

### Install or update
1. Download `OksaSDR-android-beta3.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 3 installs over beta 1 or 2 and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
