**OksaSDR for Android — beta 28.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 28
- **Fixed: FT8 / FT4 transmit again.** Since beta 26 the check that stops voice PTT in DIGU / DIGL also stopped FT8 / FT4's own transmissions ("Transmit refused"). Sorry about that! Thanks Wayne WA2N.

### Still new from beta 27
- **FreeDV tab:** RADE moved here, with FreeDV Reporter: report your station to qso.freedv.org while RADE is on (off by default) and see who is on it, on this band or all bands; tap one to tune there (RADE comes on for a RADE station). Asked by Scott W5SLS.
- **The Digi tab is now FTx** (FT8 / FT4), and the control buttons are all one size (Prof, Mem).
- **The app shows its beta number:** the version in the top row, on the Connect page and in the log ends in (28) for beta 28, as the release is called (before, a build counter).
- **DX cluster status:** a line under the DX cluster button (View tab) says whether it is connected, to which server and as whom, with the spots so far and the latest one's time.
- **RADE no longer leaves a band muted:** a band button, a frequency on another band or another mode ends RADE first; a band that comes back muted says so (tap the speaker). Thanks Michel F5GNM.
- **The display keeps going** while the app is on screen but not in focus (an unfocused split-screen or DeX window, a system prompt); it pauses only in the background.

Earlier betas (14–26): a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta28.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 28 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
