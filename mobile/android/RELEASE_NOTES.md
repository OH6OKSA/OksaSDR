**OksaSDR for Android — beta 31.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 31
- **RADE receive fixed:** RADE decoded nothing on a slice whose DAX channel was 2 or 3, and a quick RADE off / on could leave it without audio. Checked on a FLEX: DL5AI synced at up to 11 dB SNR and his callsign came through.
- **FreeDV station list by activity:** stations transmitting now first, then the most recently heard (*TX 3 min ago*), so you can go and listen while they are active. A tap on an SSB station turns RADE off, so you hear it.
- **New call on the FTx tab,** as WSJT-X: cyan for a call never worked, light cyan for one not worked on this band, and a *New call* filter.
- **Big logbooks use little memory:** only a small index stays in memory (120 000 contacts about 17 MB) and the log is read in the background at start.
- **RADE:** a quick PTT tap now sends its end of over with your callsign.
- **A keyboard on the phone** (Bluetooth, DeX) tunes and keys as on a tablet (arrows, Space PTT, F, F-keys); in a very low landscape window the *Band* button opens the bands.
- **The slice line is left out in voice modes;** the frequency stays beside the passband.

### Still new from beta 30
- **Big logbooks import in seconds:** an ADIF file of about 20 000 contacts froze the app (a black screen until force-stopped). The import now runs in the background in one write, and the display keeps going; the line under the button says *Importing…* and then how many came in. Import the same file again to add what was missing; contacts already in the log are skipped.
- **SmartLink Sign out takes two taps** (the first turns it into *Sign out?* for three seconds), so a stray tap while the Connect page settles does not sign you out.

Earlier betas (14–29): sliders, keypads, ⋯ menu and text fields the same on every device (thanks Richard EI9JE); meters Analog / Bar / Two bars with scales and peak hold, the white dashed slice line, FT8 / FT4 transmit fixed and ending when the slice goes back to voice, RADE fixes; the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta31.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 31 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
