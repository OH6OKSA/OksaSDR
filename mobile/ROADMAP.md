# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Next build

Nothing queued yet. To verify in build 103: the iPhone Duo inner screen full size with the iOS 27 SDK (build 102 with the scene manifest alone still ran letterboxed), and on other iPhones and iPads background/resume, SmartLink sign-in and the share sheet under the UIScene life cycle. In build 101: Keep CQ with a real answer on the air, the RX mark following a station, the clock notice on a device with a wrong clock. In build 100 (on the iPad: full drive at 0 dB; 15 W on 15 m was the band's RF power): four slices decoding for an hour without a hang, the tap flash. Build 99 on the iPad: four slices on their own DAX channels, decoding every slot. In build 97: the 3D view over SmartLink on the iPad (no switch to 2D). In build 96: dragging the TX band on a device. In build 94 (FT8 and FT4 QSOs on the iPad worked: OK2VCR, EA3FHP): RADE transmit with a device microphone and receive with a RADE station, the log in Files / Documents and the ADIF import on iOS and Android, the 2D start after a 3D hang. In build 93: FDVU / FDVL with the FreeDV waveform (by the tester who asked); menus closing on a second tap without retuning; the QRZ.com sign-in result with an account. In build 92: zoom in CW down to the minimum and back out; in build 91: the Log tab on the devices
(QRZ.com lookup with an account, the ADIF share sheet on iOS and Android);
the 3D fallback on the tablet that froze; spot labels beside the RF GAIN
badge. Still open from build 86: an FM repeater memory on the air, DAX
off at PTT on the radio that had it on, audio after a call on an iPhone,
the CPU load after an interruption and a SmartLink reconnect. The README
and manual device screenshots are to be retaken. Next: logging step 2
(Wavelog / Cloudlog, WSJT-X UDP and plain ADIF UDP); setting transverters up in the app; the rest of upstream 26.10.3
as its own build.

## Planned: logging (step 1 and the ADIF share in build 91)

QSO logging from the app to any logbook, through the standard ADIF routes
(analysed 2026-10-18):

1. **Quick log and callsign lookup.** Call and RST typed (or the call
   from a tapped spot); frequency, band, mode, UTC time, power and the own
   station from the radio. Name, QTH, grid and LoTW / eQSL / paper-QSL
   from QRZ.com through the engine's existing `CallsignLookupService`
   (cache, photos, distance and bearing; country, continent and CQ zone
   from cty.dat without a QRZ subscription). The QRZ password goes in the
   platform secure store (the desktop uses QtKeychain). POTA / WWFF
   fields (SIG / SIG_INFO, MY_SIG).
2. **A log on the device and destinations**, any of:
   - WSJT-X UDP "Logged ADIF" (port 2237): MacLoggerDX, Log4OM
     (ADIF_MESSAGE), HRD Logbook (QSO forwarding), GridTracker, JTAlert;
   - plain ADIF over UDP (port 2333, WSJT-X's secondary-server format):
     Log4OM, HRD; N1MM+ to confirm on a real install (ADIF import works);
   - Wavelog / Cloudlog HTTP API (`/api/qso`, a read+write key, the
     station profile from `/api/station_info`), also over mobile data;
   - an ADIF file shared to anything (mail, Files, AirDrop), e.g. LoTW
     through TQSL.
   A queue resends to HTTP destinations when the network returns; UDP
   (no acknowledgement) keeps the device log and a resend. UDP needs the
   logbook's computer on the same network or a VPN.
3. **Later:** Wavelog's `/api/radio` (frequency/mode to Wavelog's own log
   form); spots marked by LoTW users and worked-before
   (`/api/logbook_check_callsign`, upstream idea #2809); the CW decoder's
   text offering the call; QRZ.com logbook upload.

Not planned: LoTW upload from the app (needs TQSL signing with the
operator's certificate; the logbook or Wavelog does it); QSY from Wavelog
to the radio (needs a listening server on the phone, not possible in the
iOS background).

## Released

**Build 103** (2026-10-24), iPhone and iPad only:
- iOS CI on the macos-27 image with Xcode 27 (iOS 27 SDK): built with the iOS 26 SDK the app stayed in compatibility mode on the iPhone Duo's inner screen even with the scene manifest.
- FT8 / FT4 state banner at the top of the display (asked by a tester: the state box scrolled away with the decodes); the TX / RX labels moved under it.
- The radio's speech processor off while FT8 / FT4 transmits, on again when decoding stops or on disconnect (asked by a tester).
- A Digi band button binds FT8 to that slice at once: bound to the old TX slice, a CQ pressed before the radio confirmed the move was halted by it.

**Build 102** (2026-10-24), iPhone and iPad only:
- The UIScene life cycle (a scene manifest; Qt 6.12 sets its delegate) so resizable displays (iPhone Duo inner screen, iPad windows) no longer run the app in compatibility mode; portrait upside down on the iPhone.

**Build 101** (2026-10-24), iPhone and iPad, and Android beta 21:
- FT8 / FT4 RX mark (green, the QSO station's audio Hz, or the TX Hz while calling CQ) beside the TX mark; both labels on solid grounds (the TX label vanished into the waterfall on the iPad).
- The QSO state as a box: amber while waiting, red while transmitting.
- Keep CQ (back to CQ after each QSO the CQ brought; the CQ watchdog still stops an unanswered CQ) and CQ slot Auto / Even / Odd.
- A clock notice from the decodes' median DT (beyond ±0.8 s, smoothed).
- BC shown dimmed in CW and data instead of hidden; the FT8 TX log line has the SWR and the band's RF power.
- Tests: ft8-test keepcq runs a QSO without RF (dry transmissions, an answer fed in), AETHER_FT8_RX_LATENCY_MS shifts DT.
Tested on the FLEX (dry, no RF): CQ in the odd slot, an answer, RR73, back to CQ; the RX mark at the answer's Hz; the clock notice with DT shifted 1 s; iPad, phone and 3D layouts.

**Build 100** (2026-10-24), iPhone and iPad, and Android beta 20:
- The FT8 hashed-callsign table (256 entries, probed without bound under a lock) filled up with four busy slices: every decoder stopped and the app froze on the next transmit. Probes are bounded now, the oldest call gives way, 2048 entries, aged once a slot.
- FT8 / FT4 audio at full scale (it was −6 dB: 5.6 W of 10 W on the FLEX, 8.5 W now) and a TX level (0 to −20 dB); the peak power of each transmission in the log.
- A tapped decode flashes (kept across the list rebuild the answer causes).
- AGC by mode on the app's own mode changes, also after the radio brings a band's AGC: fast for CW and data, slow for voice, the user's choice kept per kind.
- The ft8-test removes leftover slices only with clean (it could take a user's slices).
Tested on the FLEX: power at −6 / 0 dB, AGC on USB / DIGU and after a band change, the hash table with 3000 calls, the flash's timing.

**Build 99** (2026-10-24), iPhone and iPad, and Android beta 19:
- FT8 / FT4 decode filter: All, CQ, New, New DXCC (your call, TX and the QSO station always show).
- Bursty audio no longer pads the slot buffer with silence (only a lateness lasting 2 s counts as a gap); the slots after the first decoded nothing on a busy PC and could over SmartLink.
- A decoding slice sharing a DAX channel with another moves to a free one (two slices on channel 2 on the iPad in build 98); the old channel comes back after.
Tested on the FLEX on 40 m: A and B decoding every slot for two minutes, an answer from B, the filters on iPad and iPhone sizes.

**Build 98** (2026-10-23), iPhone and iPad, and Android beta 18:
- FT8 / FT4 decode every DIGU / DIGL slice (a decoder and DAX channel each), rows marked with the slice letter; timing and TX follow the TX slice (adding slices took TX from a running CQ on the iPad in build 97).
- WSJT-X decode colours (CQ, my call, TX, new DXCC / CQ zone / continent, per band) and grey for worked before on the band and mode.
- Tapping a call heard on another slice moves TX, the active slice and the display there and calls it.
Tested on the FLEX on 40 m: A and B decoding at once, a CQ heard on B answered from B at B + 637 Hz; colours checked live on iPad and iPhone sizes.

**Build 97** (2026-10-23), iPhone and iPad, and Android beta 17:
- FT8 / FT4 bound to one slice (DAX channel of its own, made TX before keying, the log's frequency); a tap in another DIGU slice's passband moves it (two slices broke decoding on the iPad in build 96).
- Filter edges back in FT8 / FT4; the iPad's empty PTT row removed.
- 3D slow-GPU check: times how long rows wait, with a 10 s warm-up (it misfired over SmartLink on the iPad).
- CQ rows bold blue; steadier slot timing; an exit crash fixed.
Tested on the FLEX: two slices with a CQ on slice B heard by PSKReporter at B + 637 Hz.

**Build 96** (2026-10-23), iPhone and iPad, and Android beta 16:
- FT8 / FT4: the TX band dragged to any Hz (1 Hz) or set by a tap; − / + step 1 Hz; no scale or passband shading in FT8 / FT4; a drag in the passband no longer retunes (asked after testing build 95 on the iPad).
- A Digi band button starts decoding (on the iPad in build 95 Decode was off, so the tap pick did not show).
- PTT hidden while FT8 / FT4 decodes; the phone's Controls row full width.

**Build 95** (2026-10-23), iPhone and iPad, and Android beta 15:
- FT8 / FT4: a tap inside the passband sets the TX audio frequency (no retune); a 500 Hz scale and the TX band over the passband; band buttons and Zoom fit the display to 4 kHz. Tested on the FLEX.
- iPadOS "Unknown hover state" lines kept out of the log.

**Build 94** (2026-10-23), iPhone and iPad, and Android beta 14:
- FT8 and FT4 in the app (Digi tab): ft8_lib decoding with WSJT-X SNR, CQ / answer, the WSJT-X QSO sequence with type 4 and <hashed> messages for nonstandard calls, logging; transmit audio past the voice chain on its own clock, allowed in DIGU. On the FLEX on 20 m: two 10 W CQs gave 157 PSKReporter spots, a QSO with DB2VS was run and logged, FT4 CQs gave 17 spots.
- RADE digital voice in the app, on any Flex (the desktop's RADE wiring, DAX RX / dax_tx).
- The log in Documents (iOS Files, Android shared Documents copy, desktop Documents/OksaSDR); ADIF import.
- A start in 2D after the app was killed with the 3D view on (a Unisoc T606 tablet hung).

**Build 93** (2026-10-22), iPhone and iPad, and Android beta 13:
- The mode chooser offers every mode the radio lists (DIGL, SAM, NFM, DFM, RTTY, and FreeDV's FDVU / FDVL with the waveform), not only six (asked by the FreeDV team).
- Menus and choosers are modal without dimming: a second tap on their button closes them, and a tap outside never reaches the spectrum to retune; the phone's Controls drawer has Close beside PTT.
- A DAX switch on the TX tab, showing what the radio transmits.
- The QRZ.com account's Save checks the sign-in and shows the result.

**Build 92** (2026-10-21), iPhone and iPad, and Android beta 12:
- Flex pans report their span limits (min_bw / max_bw), so a zoom stops at the radio's minimum (1.23 kHz); past it the radio refused the change and the span stayed stuck until a band change (seen on 160 m after CW).

**Build 91** (2026-10-21), iPhone and iPad, and Android beta 11 (build 87 waited hours for a macos-26 runner and was cancelled; 88-90 were CI test runs; 91 is the same code, built on macos-15):
- Log tab (logging step 1): quick log with the QRZ.com (account in the secure store) or cty.dat lookup, worked before and dupe, reports by mode, POTA / WWFF SIG fields; radio frequency, band, mode, power, UTC and own call and grid; an ADIF 3.1.4 file on the device (QsoLog).
- Share the log as an ADIF file (iOS share sheet with the file; Android through the FileProvider).
- The 3D view falls back to 2D when its frames fall behind the data (a tester's Android 13 tablet froze with it on).
- Spot labels keep clear of the RF GAIN badge.

**Build 86** (2026-10-20), iPhone and iPad, and Android beta 10:
- FM repeater memories: shift and CTCSS tone in the list and the editor; recall sends the desktop's retune and repeater fix-up, so tx_offset_freq moves the transmitter (checked: 29.620 -0.1 recalled transmits on 29.520).
- PTT with this device's microphone turns the radio's DAX transmit off (with DAX on, the radio ignored the app's audio: confirmed on the FLEX); the log says mic input and DAX at each PTT.
- iOS: receive audio (and an open microphone) restarts when the audio session comes back after a call, alarm or Siri.
- Peak hold Off / Fast / Medium / Slow, as the desktop S meter.
- OksaSDR Users group (Facebook) linked from the README, tester guide and TestFlight texts; README: about the author.

**Build 85** (2026-10-20), iPhone and iPad, and Android beta 9:
- FM repeater: the offset sends tx_offset_freq, which moves the transmitter (checked on the air: 29.650 down 0.05 keyed on 29.600).
- The Mic meter (left meter) moves in receive while the TX tab is open: the microphone opens for it, nothing is sent before PTT, not while VOX is on.
- Meters offer only readings that move: receive S, Mic, PA temp, Volts; Power, SWR, ALC, Comp in transmit.
- Notch handles larger and outlined in black, filled with TNF on and hollow with it off; band edges at full strength.
- From upstream main: SmartLink UDP registration keepalives (#6491), retries after a refused TCP connection (#6484), the TX refusal names the reconnect (#6548).
- Tools: TestFlight tester report (local), --repeater-test.

**Build 84** (2026-10-19), iPhone and iPad, and Android beta 8 (build 83 was a CI test, not uploaded):
- Trackpad scrolling follows the fingers on the iPad and on Macs running the iPad app (Qt's iOS trackpad events get a Mac trackpad's phases; the panels had stopped halfway).
- FM repeater offset (Simplex / − / +, shift) and CTCSS tone in the TX tab in FM modes.
- Transverter bands: "439.000" is MHz; FM steps on the step buttons; the active band button is lit.
- CI: iOS compiles once (about 10 min less); Android keeps a compile cache (about 4 min a run instead of 13); uploads only from mobile/oksasdr.

**Build 82** (2026-10-19), iPhone and iPad, and Android beta 7:
- Draggable divider between the spectrum and the waterfall (as the desktop's); double tap for the automatic split; remembered.
- Transverter (XVTR) bands from the radio as band buttons after 6 m (checked with a temporary receive-only 2m transverter: band X0, 144.100 MHz on XVTA).
- Remote audio: a late packet is dropped instead of concealing a false 8-frame jump and playing out of order (choppy audio across reordering paths); reordering no longer counts as loss. Verified in the field: the iPad mini 6 tester who heard choppy audio over VPN and SmartLink reports it gone (2026-10-19).
- iPad Space PTT holds: Space from GameController, only while the operate page has the keyboard.
- Sequence-loss tests updated to the reorder-tolerant count; --select-band dev option.

**Build 81** (2026-10-19), iPhone and iPad, and Android beta 6:
- SmartLink stays signed in after a restart and reconnects after Disconnect (the kept token was overwritten by an empty one; a refused session now signs in again from it).
- iPad keyboard paddles tell left from right Option / Control through GameController (Qt's iOS key events carry no scan code); Command keys nothing.
- Very low landscape fits (Android outer screen, smaller iPhones): metrics from the safe area, no band strip there, no safe-area binding loop.
- Keyboard paddle keys and focus in the log; dev options for the pinned panel.

**Build 80** (2026-10-18), iPhone and iPad, and Android beta 5:
- CW tab: Iambic A / B and Swap ⇄; "Paddles on a keyboard" with Ctrl (the default), Alt ⌥ (Option) or [ ], each explained; the dit and dah labels follow Swap; Command never keys a dit.
- CW refused: the radio's reason shows. On 2026-10-18 the FLEX-6600 refused every CW key-down ("another client is transmitting…") with no client on air until it was restarted; the cause is not known.
- Keying with break-in off says why nothing goes on air.

**Build 79** (2026-10-18), iPhone and iPad, and Android beta 4:
- CW paddles on the screen with the app's iambic keyer (A/B, squeeze; [ ] or Ctrl L/R on a keyboard); straight key, paddles and iambic A and B checked on air.
- CW decoder: Sens filter (default 70) and 8–45 WPM; band noise mostly gone, weak calls decode.
- Passband drag tunes on the tuning step; filter edges and notches drag on the display.
- Spot filters (mode, age); network changes in the log.

**Build 78** (2026-10-18), iPhone and iPad, and Android beta 3:
- Mic level acts on this device's microphone (own gain, −20 to +30 dB, limiter at −1 dBFS); the radio's mic level does not act on its PC input (measured: 6.2 W at level 10, 6.0 W at 90).
- Lit buttons: on = blue, off = dark (chosen from three styles after tester feedback).
- PTT tap on / off mode, with a 3-minute limit.
- Sidetone for CW memories and CW text; busiest threads in Network & performance; Android asks for the microphone on the first connect.

**Build 77** (2026-10-17), iPhone and iPad, and Android beta 2:
- Spots on the display and a DX cluster connection (spots go to the radio); tap to tune.
- TNF: hold on the display to add a notch, tap it to adjust; TNF switch in DSP.
- TX processing of this device's microphone: gate, EQ, compressor.
- Memory editing (name, frequency, mode).
- Voice keyer: Listen also for clips kept on the device.
- Mic meter reads this device's microphone; very low windows show bar meters.

**Build 76** (2026-10-17), iPhone and iPad:
- Radios on the connect page read as buttons.
- Pan slider shows C / L n / R n and snaps to centre.
- Phones open the controls only from the button beside PTT.
- The controls drawer reaches the window's bottom edge.

**Build 75** (2026-10-17), iPhone and iPad:
- OksaSDR app icon.
- Volume and pan sliders follow the finger.
- Meter peak hold resets with the meter's quantity; ALC/Mic below scale read "< min".
- MOX removed; one Mic level; device mic trim in Setup → Phone/CW.
- Callsign edited in Setup → Radio only.

**Build 74** (2026-10-17): the app is called **OksaSDR** (formerly AetherSDR
Mobile); TX soft start limited to iOS.

**Build 70** (2026-10-16): STEP fix and placement, dBm scale after band
change, info panel order, View pan settings, Band zoom, WWV/GEN, split
offsets, meter peak hold.

Earlier builds: see the git log and `TESTFLIGHT_WHATS_NEW.txt`.

## Android (beta)

Same code as iOS (`mobile/android/` holds the platform parts), built and
signed in GitHub Actions (`mobile-android.yml`); betas in
[Releases](https://github.com/OH6OKSA/OksaSDR/releases) (the newest is
marked Latest: [releases/latest](https://github.com/OH6OKSA/OksaSDR/releases/latest)).

Beta 1: receive, spectrum and waterfall, TX, background receive (foreground
service, Qt kept running), SmartLink with the sign-in kept in the Android
Keystore, Bluetooth headsets (hands-free), log sharing, LAN discovery
(multicast lock), crash reasons logged on the next start.

Beta 2: what iOS build 77 brings (spots and DX cluster, TNF, TX
processing, memory editing, voice keyer Listen, Mic meter). Published by CI
from `mobile/android/release.env`.

Beta 3: what iOS build 78 brings (Mic level as the device microphone's
gain, lit buttons, PTT tap on / off, CWX sidetone, busiest threads); the
first beta marked Latest on GitHub.

Beta 4: what iOS build 79 brings (CW paddles, decoder, edge and notch
drags, spot filters); the first published by CI as Latest.

Beta 5: what iOS build 80 brings (iambic and keyboard paddles in the CW
tab, the CW refused notice, the break-in note).

Beta 6: what iOS build 81 brings (SmartLink stays signed in and
reconnects; the outer screen in landscape fits).

Beta 7: what iOS build 82 brings (spectrum / waterfall divider,
transverter bands, smoother audio across reordering networks).

Beta 8: what iOS build 84 brings (FM repeater offset and CTCSS,
transverter entry and FM steps, the lit band button).

Next: verify LAN discovery on the radio's network; APK size (about 150 MB).

## Later

From the comparison with upstream main and tester requests:
- Memory groups (repeater offsets and tones are in build 86)
- ATU tools and per-band TX settings
- Profile import and export
- QSO recording
- 3D with 768 columns
- Upstream FFT Smooth and De-comb
- Optional VOX mode (microphone always open)
- More of the desktop's TX chain (de-esser, tube, exciter, reverb) and a
  chain order
- CW profiles (new in SmartSDR 4.3)

From other FlexRadio mobile clients (PocketSDR for Android, release of
2026-10-20), to keep up with:
- A simple logbook with prefix/country lookup and ADIF export: covered by
  the logging plan above (steps 1 and 2); a reason to start it sooner.
- 4O3A Tuner Genius XL control (direct and through the radio, SO2R and
  1x3): the engine already has the desktop's tuner model (TunerModel),
  so mostly a mobile panel.
- PTT latency measured over SmartLink and VPN (key to RF), and FreeDV /
  RADE on transmit and receive.

## Waiting

- **Upstream (aethersdr/AetherSDR):** no longer pursued (2026-10-22); OksaSDR
  goes its own way and takes upstream changes only when useful.
- **Three meters while transmitting** (Power, SWR and Mic at once, asked in
  the group by an Aurora user): waiting for his answer on the layout (two
  bars stacked on one side, a third meter in the slice row's middle, or
  three bars across).
- **To verify:** LAN discovery on Android at the radio's own network (on the
  next visit to the cabin, where the radio is; SmartSDR 4.3.16 works: a
  tester's FLEX-6600 runs it).
- **Verified:** the TX start (no delay, no whoosh) since build 68.

## Platforms

| Area | iOS / iPadOS | Android |
|---|---|---|
| Connect by IP, SmartLink, VPN | ✅ | ✅ (SmartLink sign-in kept) |
| LAN discovery | ⏳ needs Apple's multicast entitlement | 🧪 built in, to verify |
| Spectrum, waterfall, 3D | ✅ | ✅ |
| RX audio, background audio | ✅ | ✅ (foreground service) |
| Voice TX (device mic, Bluetooth headset) | ✅ | ✅ |
| CW, CWX, decoder, voice keyer | ✅ (straight key and paddles on air) | ✅ (straight key and paddles on air, SmartLink) |
| Spots and DX cluster, TNF, memory editing | ✅ | ✅ |
| TX processing (gate, EQ, compressor) | ✅ | ✅ |
| Keyboard, pencil/stylus | ✅ | to verify |
| Phone and tablet layouts | ✅ | ✅ |
| Distribution | TestFlight | APK in GitHub Releases (beta) |
| Mac (the iPad app on Apple silicon) | ✅ testers run it through TestFlight | — |
| Community | [OksaSDR Users](https://www.facebook.com/groups/1468327561855382) on Facebook | same |
