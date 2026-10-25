**OksaSDR for Android — beta 30.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 30
- **Big logbooks import in seconds:** an ADIF file of about 20 000 contacts froze the app (a black screen until force-stopped). The import now runs in the background in one write, and the display keeps going; the line under the button says *Importing…* and then how many came in. Import the same file again to add what was missing; contacts already in the log are skipped.
- **SmartLink Sign out takes two taps** (the first turns it into *Sign out?* for three seconds), so a stray tap while the Connect page settles does not sign you out.

### Still new from beta 29
- **Sliders the right way round:** the part left of the handle is filled in blue and the rest is a dark track, as on the iPhone, iPad and Mac (Android's colours had them the other way round). The ⋯ menu and the text fields are dark too. Thanks Richard EI9JE.
- **Phone-style keypads:** the IP address and frequency keypads have 1 2 3 on top, as the phone's own number pad, and the delete key is drawn (it showed as a box). Thanks Richard EI9JE.

Earlier betas (14–28): meters Analog / Bar / Two bars with scales and peak hold, the white dashed slice line, FT8 / FT4 transmit fixed and ending when the slice goes back to voice, RADE fixes; the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta30.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 30 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
