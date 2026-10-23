# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Next build

Queued: **FT8 and FT4 in the app** (Digi tab: decoding, CQ / answer, the
WSJT-X QSO sequence, logging; tested on 20 m with PSKReporter spots and a
QSO), **RADE digital voice in the app** (receive path tested on the radio;
transmit to try on a device), and a start in 2D after a hang with the 3D
view on. To verify in build 93: FDVU / FDVL with the FreeDV waveform (by the tester who asked); menus closing on a second tap without retuning; the QRZ.com sign-in result with an account. In build 92: zoom in CW down to the minimum and back out; in build 91: the Log tab on the devices
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
