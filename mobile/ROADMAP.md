# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Next build

- CW refused: when the radio will not key CW, the app says why instead of
  staying silent. On 2026-10-18 the FLEX-6600 refused every CW key-down
  ("another client is transmitting or sending a CW/CWX message") with no
  client on air, in beta 4 and build 79 alike, until the radio was restarted;
  the cause is not known.
- CW tab (tester feedback): Iambic A / B and swap paddles in the tab itself,
  the keyboard row explained ("keyboard keys as paddles") with Option L/R
  as a choice, a note when break-in is off.

Open from tester feedback: SmartLink sign-in not kept for one tester (log
needed).

## Planned: logging (when to build it is open)

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

Next: verify LAN discovery on the radio's network; APK size (about 150 MB).

## Later

From the comparison with upstream main and tester requests:
- Memory offsets and tones (repeaters); memory groups
- ATU tools and per-band TX settings
- Profile import and export
- QSO recording
- 3D with 768 columns
- Upstream FFT Smooth and De-comb
- Optional VOX mode (microphone always open)
- More of the desktop's TX chain (de-esser, tube, exciter, reverb) and a
  chain order
- CW profiles (new in SmartSDR 4.3)

## Waiting

- **Upstream (aethersdr/AetherSDR)**, left to wait for upstream:
  - [discussion #6409](https://github.com/aethersdr/AetherSDR/discussions/6409): upstreaming, naming, distribution (GPL and Apple);
  - [#6532](https://github.com/aethersdr/AetherSDR/issues/6532): RFC, client-side Beat Cancel;
  - [#6533](https://github.com/aethersdr/AetherSDR/issues/6533): closing the TX slice leaves no slice transmitting;
  - [#6534](https://github.com/aethersdr/AetherSDR/issues/6534): RFC, Opus under Auto over a VPN;
  - [PR #6535](https://github.com/aethersdr/AetherSDR/pull/6535) for #6051: VITA-49 loss counted net of reordering.
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
| CW, CWX, decoder, voice keyer | ✅ (CW on air to verify) | built in, to verify |
| Spots and DX cluster, TNF, memory editing | ✅ | ✅ |
| TX processing (gate, EQ, compressor) | ✅ | ✅ |
| Keyboard, pencil/stylus | ✅ | to verify |
| Phone and tablet layouts | ✅ | ✅ |
| Distribution | TestFlight | APK in GitHub Releases (beta) |
