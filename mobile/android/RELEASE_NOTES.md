**OksaSDR for Android — beta 39.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 39
- **FT8 / FT4 with Keep CQ:** a station's 73 after your RR73 no longer starts a new QSO, which logged the contact twice. If they send their report again (our RR73 did not reach them), RR73 goes again, logged once.

### Still new from beta 38
- **Audio output works with Bluetooth on:** *Speaker* now plays on the phone's speaker and uses the phone's microphone even with a Bluetooth device connected, and *Bluetooth media* transmits with the phone's microphone; before, the sound and the microphone stayed on the Bluetooth device whatever the choice (thanks Larry Averbeck for the first report).
- **Band takes you back to a band** when the view is outside every band (the slice's band, or the nearest).
- **The decoded CW strip** sits under the waterfall's frequency scale, so the scale stays readable.
- **Meters:** S is no longer a transmit choice (nothing to read on the air).

Earlier betas (14–37): less battery use with the spectrum on the graphics chip (thanks Mike VA3MW), "2 analog" meters, the memory in the top row; a held PTT no longer freezing tablets with an accessibility service on (thanks Kurt DL5EAG), held step buttons (thanks Larry), wider bar meters (thanks Juho); CW paddles at the phone's edges, `{HIS}` in CW memories, QRZ.com details in every logged QSO; the slice volume beside the speaker icon and a log line when the app stops answering; turning the phone keeps the display, the CW strip with [away] / [back] marks; the CQ POTA filter in FTx (thanks Ryland WW4GA) and Heard in CW on the Log tab; RADE receive on DAX channels 2 and 3 and its quick-tap end of over, the FreeDV station list by activity, New call in FTx, big logbooks, a keyboard on the phone; big ADIF imports in seconds, two-tap SmartLink Sign out; sliders, keypads, ⋯ menu and text fields the same on every device (thanks Richard EI9JE); meters Analog / Bar / Two bars with scales and peak hold, the white dashed slice line, FT8 / FT4 transmit fixed and ending when the slice goes back to voice, RADE fixes; the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta39.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 39 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
