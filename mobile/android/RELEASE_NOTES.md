**OksaSDR for Android — beta 29.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 29
- **Sliders the right way round:** the part left of the handle is filled in blue and the rest is a dark track, as on the iPhone, iPad and Mac (Android's colours had them the other way round). The ⋯ menu and the text fields are dark too. Thanks Richard EI9JE.
- **Phone-style keypads:** the IP address and frequency keypads have 1 2 3 on top, as the phone's own number pad, and the delete key is drawn (it showed as a box). Thanks Richard EI9JE.

### Still new from beta 28
- **Meters: Analog, Bar or Two bars** for each meter (tap a meter › Style). Analog stays analog while transmitting; with Two bars each bar has its own receive and transmit choice, so up to four readings show. Bars have a scale like the analog meter (S1…S9, +20/+40/+60) and a clear amber peak hold marker. Thanks Mike VA3MW.
- **The slice is a white dashed line** with its frequency beside it, apart from the yellow filter edges.
- **FT8 / FT4 end when the slice goes back to voice:** switching the FT8 slice to LSB / USB (by hand or by a band button) ends decoding, so PTT comes back and the view widens from the 4 kHz FT8 view. Thanks Mike VA3MW.
- **Fixed: FT8 / FT4 transmit again.** Since beta 26 the check that stops voice PTT in DIGU / DIGL also stopped FT8 / FT4's own transmissions ("Transmit refused"). Sorry about that! Thanks Wayne WA2N.
- **RADE:** the filter widths centre on RADE's 1500 Hz while it is on; Disconnect, an FTx band button and Decode end RADE first; a slice RADE muted when the app closed or the link dropped is unmuted on the next connection; RADE off always restores a voice mode and filter. Thanks Michel F5GNM.

Earlier betas (14–27): the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta29.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 29 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
