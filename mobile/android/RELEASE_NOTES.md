**OksaSDR for Android — beta 58.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight), from the same code.

### New in beta 58
- **RTTY on either sideband, logged and spotted right:** received in DIGL or DIGU, a QSO you log goes in as RTTY on the signal's mark frequency, as in the radio's RTTY mode (it went in as DATA on the carrier, 2 kHz off), and a tap on an RTTY spot keeps DIGL / DIGU, tuning so the spot's mark lands on the mark tone. Lower sideband is the tradition, but many stations run AFSK on the upper sideband: the signal is the same as long as the mark is the higher frequency, so both receive it.

### Still new from beta 57
- **RTTY has its own tab**, Controls › RTTY: receive in the radio's RTTY mode (lower sideband, the standard) or as AFSK in DIGL or DIGU, the decoder following the sideband (on the upper sideband the mark is the upper tone); a tap on a signal tunes it in for each. **Reverse** works now (it put the space on a wrong tone), and the squelch no longer drops letters of upper-sideband or reversed signals. With decoding off, the line over the waterfall still opens the settings.
- **Worked before from your logbooks, in every mode:** Log tab › Wavelog › **Worked before from the logbook** reads your Wavelog logbook's QSOs (every station location; at each start only the new ones), so worked before, dupes, New call / DXCC and the FTx colours know them. They stay apart from the app's own log, and a QSO the app sent there counts once.
- **QRZ.com Logbook both ways** (needs a QRZ subscription): paste the logbook's API key on the Log tab; every QSO you log goes there, and with Worked before from the logbook on, its QSOs count as worked before.
- **A shared ADIF file** in Google Drive, OneDrive, Dropbox or Nextcloud for a desktop logger without a cloud connection (Log4OM, N1MM…): Choose… it on the Log tab; its QSOs count as worked before, and every QSO you log is added to its end.

### Still new from beta 56
- When QRZ.com does not answer, the Log tab says your account is saved and lookups will try again (thanks Sloan Davis); the manual's **Operating by mode**.

### Still new from beta 55
- **Transmit lock** (thanks Larry Averbeck): Controls › TX › Lock; in standby nothing transmits until a second's hold on PTT, KEY, Space, TUNE or CQ arms it for 5 minutes. **FTx Answer CQ** (thanks Kurt DL5EAG): the rarest CQs first, 5 calls each, stopping after a time without a touch on the app that you set (1–60 min). **No burst of band noise at PTT** from the phone's speaker and **USB-C headsets** (thanks Jeff Stinger); the ADIF picker offers every file (thanks Kurt); RTTY the right way round with tap, spot, Autotune and Squelch; a QSO always ends with its RR73 or 73.

### Still new from beta 54
- A tap outside a text field gives the keyboard back to the page; **RTTY decoding** with its panel (thanks Ben van Schoor).

### Still new from beta 53
- CW Autotune acts when pressed; a CW pitch of 0 Hz from the radio becomes 600 Hz; notches only after **Add** in the notch panel; the **eSSB** tab (this device's microphone processing); FTx **Scroll › List only** (thanks Michael Whaley); TalkBack names for every control.

### Still new from beta 52
- − and + on every slider that sets an exact value (pan, RF and tune power, mic and monitor level, TX filter, DSP levels, CW speed and pitch, Radio Setup's levels); Radio Setup › Transmit › Show my transmission in the waterfall; FTx: a band change clears the old band's decodes, the time line dim while a time server keeps FT8 on time; **In use by** on SmartLink cards, a readable connect page on a tablet, scroll bars, clearer tags.

### Still new from beta 51
- − / + on AGC-T, RF gain, volume and squelch, B/S, Close at the top right of every panel (thanks Larry Averbeck); bigger touch targets and felt taps; connect by host name (thanks Sebastian DG5CST); full-screen controls for FTx (thanks Michael Whaley).

### Still new from betas 49–50
- The connection no longer drops when the app goes to the background; a foldable's inner screen upright looks as on its side; the FTx time sync line; the tablet layout on a foldable's inner screen in both orientations, ⋯ in a narrow window, Keep CQ between CQ and Halt TX.

### Still new from betas 43–48
- The volume and AGC panels have their full width in a wider phone window, and the network panel fits a phone (betas 47–48).
- **Logger32:** every QSO you log can go to Logger32 on your PC (Log tab: the PC's address, port 52001). Away from home it needs your VPN; QSOs wait on the phone when the PC is not reached (thanks Juho Kontio).
- **Getting started** at the top of the manual: what each mode (voice, CW, FT8 / FT4, RADE) needs before you go on the air, and where to set it.
- Record and play back (the dot and the triangle in the slice row); F/R (now B/S) hides the band and step buttons; step buttons on the radio's STEP; the eye in password fields (thanks Larry Averbeck); a split window on a big screen fits, with a compact top.

Earlier betas (14–42): Auto after Bluetooth media back to the headset, the waterfall after a return; quick Audio output changes without silence; Audio output switching without losing the sound; FT8 / FT4 on a time server's time (thanks Kurt DL5EAG) and Keep CQ without double logging; Audio output Speaker and Bluetooth media with the phone's microphone (thanks Larry Averbeck), Band back to a band, the CW strip under the scale, no S among the transmit meters; less battery use with the spectrum on the graphics chip (thanks Mike VA3MW), "2 analog" meters, the memory in the top row; a held PTT no longer freezing tablets with an accessibility service on (thanks Kurt DL5EAG), held step buttons (thanks Larry), wider bar meters (thanks Juho); CW paddles at the phone's edges, `{HIS}` in CW memories, QRZ.com details in every logged QSO; the slice volume beside the speaker icon and a log line when the app stops answering; turning the phone keeps the display, the CW strip with [away] / [back] marks; the CQ POTA filter in FTx (thanks Ryland WW4GA) and Heard in CW on the Log tab; RADE receive on DAX channels 2 and 3 and its quick-tap end of over, the FreeDV station list by activity, New call in FTx, big logbooks, a keyboard on the phone; big ADIF imports in seconds, two-tap SmartLink Sign out; sliders, keypads, ⋯ menu and text fields the same on every device (thanks Richard EI9JE); meters Analog / Bar / Two bars with scales and peak hold, the white dashed slice line, FT8 / FT4 transmit fixed and ending when the slice goes back to voice, RADE fixes; the FreeDV tab with FreeDV Reporter (asked by Scott W5SLS), FTx and one-size control buttons, the beta number in brackets, the DX cluster status line, the display going on while the app is on screen but not in focus; a swipe in the open controls drawer no longer tunes, PTT in DIGU / DIGL says why it does not transmit, three meters while transmitting (asked by Mike VA3MW); FT8 and FT4 in the app (Controls › FTx) on every DIGU slice with WSJT-X colours, Keep CQ, Last TX and Free, PSK Reporter; Wavelog / Cloudlog; transverter setup; RADE digital voice; the log in Files › Documents › OksaSDR.

### Install or update
1. Download `OksaSDR-android-beta57.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 57 installs over earlier betas and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
