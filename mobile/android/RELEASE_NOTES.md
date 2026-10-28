**OksaSDR for Android — beta 49.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 49
- **A foldable's inner screen keeps the tablet layout in both orientations.** Turned upright, the OnePlus Open's inner screen (698 dp wide) fell under the phone layout's 700 limit; any window at least 600 dp wide now gets the tablet layout, and a near-square screen is laid out the same either way (the controls open from the right, PTT along the bottom).
- **The top row fits a narrow tablet window:** Setup, Manual, Log and Disconnect go behind ⋯ and the network dot drops its words, so the radio's name no longer runs under it. Where the info panel has no room beside the frequency it goes under it, so the band and step buttons and the right meter stay on screen.
- **FTx: Keep CQ sits between CQ and Halt TX** (the narrow panel showed "Keep …"). The CW text, QRZ.com, Wavelog and Logger32 fields put their buttons under them where the hint did not fit; the controls are at least 320 dp wide.

### Still new from betas 43–48
- The volume and AGC panels have their full width in a wider phone window, and the network panel fits a phone (betas 47–48).
- **Logger32:** every QSO you log can go to Logger32 on your PC (Log tab: the PC's address, port 52001). Away from home it needs your VPN; QSOs wait on the phone when the PC is not reached (thanks Juho Kontio).
- **Getting started** at the top of the manual: what each mode (voice, CW, FT8 / FT4, RADE) needs before you go on the air, and where to set it.
- Record and play back (the dot and the triangle in the slice row); F/R hides the band and step buttons; step buttons on the radio's STEP; the eye in password fields (thanks Larry Averbeck); a split window on a big screen fits, with a compact top.

Earlier betas (14–42): Auto after Bluetooth media back to the headset, the waterfall after a return; quick Audio output changes without silence; Audio output switching without losing the sound; FT8 / FT4 on a time server's time (thanks Kurt DL5EAG) and Keep CQ without double logging; Audio output Speaker and Bluetooth media with the phone's microphone (thanks Larry Averbeck), Band back to a band, the CW strip under the scale, no S among the transmit meters; less battery use with the spectrum on the graphics chip (thanks Mike VA3MW), "2 analog" meters, the memory in the top row; a held PTT no longer freezing tablets with an accessibility service on (thanks Kurt DL5EAG), held step buttons (thanks Larry), wider bar meters (thanks Juho); CW paddles at the phone's edges, `{HIS}` in CW memories, QRZ.com details in every logged QSO; the slice volume beside the speaker icon and a log line when the app stops answering; turning the phone keeps the display, the CW strip with [away] / [back] marks; the CQ POTA filter in FTx (thanks Ryland WW4GA) and Heard in CW on the Log tab; RADE receive on DAX channels 2 and 3 and its quick-tap end of over, the FreeDV station list by activity, New call in FTx, big logbooks, a keyboard on the phone; big ADIF imports in seconds, two-tap SmartLink Sign out; sliders, keypads, ⋯ menu and text fields the same on every device (thanks Richard EI9JE); meters Analog / Bar / Two bars with scales and peak hold, the white dashed slice line, FT8 / FT4 transmit fixed and ending when the slice goes back to voice, RADE fixes; the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta49.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 49 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
