# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Next build

To verify from Android beta 42 (checked on the user's phone 2026-10-27, 19:24-19:28: the headset's hands-free link came up 236-722 ms after receive opened, on connect and on every Bluetooth media -> Auto, and receive opened again on it each time; five returns from the background, all with Suspended at once and the display resumed, no GUI stall, no dropped connection): the GUI thread stall after home, another app and back from recents did not come back; when it does, the log has "GUI thread stalled ... (app not in front)" and the qt.qpa.window surface lines around it. Still to check: the microphone of each choice with a short PTT. From beta 41, checked on the user's phone 2026-10-27: quick Audio output changes never left receive silent (about 25 changes, every one reopened); the waterfall fix did not catch the return after another app. From build 125 / beta 40, checked on the user's phone with the headset 2026-10-27: every switch both ways ended with receive open (route errors and refused opens reopened within a second). From build 124 / beta 39: FT8 with Keep CQ on the air, the station's 73 after our RR73 (no second QSO, one log entry). From build 123 / beta 38: Audio output on Android with a Bluetooth headset (Speaker: the phone's speaker and microphone; Bluetooth media: the music link and the phone's microphone, if Qt's one Bluetooth output is the A2DP one; Auto: the headset with its microphone), the user's phone and Larry Averbeck's S26 Ultra. From build 122 / beta 37: the app's CPU in the top row against build 120 on an iPad, iPhone and Android phone with the same view (the spectrum drawing moved to the GPU), the 3D view and the waterfall on Metal, Vulkan and GLES devices, a phone turning and a zoom with the new waterfall ring, "2 analog" on an iPad and a Mac. From build 120 / beta 36: a held PTT on Kurt DL5EAG's Lenovo tablet with its accessibility services on (Lenovo pen, accessibility menu); Audio output Speaker and Bluetooth media on a phone with Bluetooth audio (Larry Averbeck's S26 Ultra), and on an iPhone; the bar meters' new width on devices. Still to verify from earlier builds: the paddles at the phone's edges with a squeeze and the tap on / off 3-minute stop on the air (119); a phone turning on a real Android phone with the 3D view (117); RADE transmit from a device with the quick-tap fix; Two bars with TUNE on a device's microphone (113); Wavelog sending, PSK Reporter spots and a transverter from a device (105). Queued (tester requests): a blank panadapter while audio plays (Larry; waiting for a log), the desktop TX chain on the device (Jeff Stinger: a 10-band parametric EQ with presets, the desktop compressor, later a multiband compressor), the radio's STEP on the step buttons, Wavelog API v2 tokens, the rest of upstream 26.10.3 as its own build.

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

**Android beta 42** (2026-10-27; no iOS build, nothing new for the iPhone and iPad):
- Bluetooth media -> Auto left the headset silent on the user's phone (beta 41), Speaker -> Auto did not: from Speaker, Android ended the receive stream when the headset's hands-free link came up and it reopened there; from Bluetooth media the stream opened while Android switched the headset from its music link and stayed on it, with no route error. Platform.java follows the hands-free link state (ACTION_SCO_AUDIO_STATE_UPDATED); with Auto, after the choice and on connect, MobileEngine checks every quarter second for 4 s and opens receive again when the link came up after it opened (AudioEngine::reopenRx), or once at 4 s with the headset as the route and no link state (LE Audio).
- The waterfall stayed empty after home, another app and back from recents (beta 41): the GUI thread stood still from leaving until the return (no log line for 16 s; receive played on, its own thread), the radio dropped the SmartLink connection, and Qt delivered Active then Suspended; beta 41's check read the activity as stopped. The check now also counts frames (10 or more in a second while Qt says Suspended: on screen, Qt's state set to Active), and OksaActivity counts starts and resumes. For the stall itself, StallWatch watches the GUI thread in the background on Android (from 4 s) and Qt's window surface lines (qt.qpa.window) reach the log. Compiled for Android here (NDK, javac); to verify on the phone.

**Android beta 41** (2026-10-27; the iOS run 126 was cancelled, nothing new for the iPhone and iPad):
- The waterfall stayed paused on screen on Android, sometimes (the user, beta 40): on one return from the background Qt delivered Active and then the Suspended of the earlier stop, in the same millisecond, so the app took itself for the background while on screen (display paused, key PTT refused) until the next state change. OksaActivity now keeps Android's own lifecycle (onStart, onResume, onPause, onStop); while Qt says Suspended, it is read every second and Qt's state set from it (QWindowSystemInterface) when the activity is on screen.
- Audio output changes every two seconds on Android used up the three route restarts in 10 s (Speaker and Bluetooth media each end the receive stream once as the route changes), and Bluetooth stayed silent until the next change (beta 40 log, 16:52:52). Each chosen route (AudioEngine::restartRxSoon) now gets its own restarts. Compiled for Android here (ScreenAwake.cpp with the NDK, OksaActivity.java with javac, the AudioEngine branch by a temporary switch); to verify on the phone.

**Build 125** (2026-10-27), iPhone and iPad (no change for them), and Android beta 40:
- Audio output on Android, beta 39 on the user's phone with a Bluetooth headset: Speaker worked only through Bluetooth media first, and Bluetooth then did not come back. The log: Auto -> Speaker opened the receive stream on the built-in speaker, and 0.6 s later the sink stopped with IOError (Android ends an AAudio stream when the route changes under it: the headset's call link closing, the microphone stream reopening, Platform.useSpeaker switching the audio mode); a later open, during a route change, failed on every format, and receive stayed off for every choice after. Now the microphone stream opens before the receive stream, Speaker no longer takes the speaker as the communication device (the streams open on the built-in speaker and microphone themselves), and on Android RX reopens after such an error (three times in 10 s at most) and keeps trying every 0.5 s for 3 s (AudioEngine::restartRxSoon, also asked after each device change while connected). The Android-only branches were compiled here by a temporary switch; to verify on the phone.

**Build 124** (2026-10-27), iPhone and iPad, and Android beta 39:
- FT8 / FT4 logged a contact twice with Keep CQ (the user, build 122 on the iPad: F1IQH and F8MRQ on 12 m, F8MRQ three times): Keep CQ (build 104) goes back to CQ at once, and there any message to our call from a new station counted as an answer, so the station's 73 after our RR73 started a new QSO; its software answered the report with an R-report and the contact was logged again. Before Keep CQ the app went idle after a QSO and the 73 found nothing to start. Now a sign-off (73, RR73, RRR) never starts a QSO; an R-report from the station just logged (its report again, our RR73 lost) gets RR73 again without a second log entry; other R-reports and calls from the station just logged are not new calls; and the same call on the same band within 10 minutes is never logged twice. Checked dry on the FLEX (no RF) with --ft8-test keepcq,signoff: answer, R-10 logged once, 73 ignored (CQ went on), R-10 again got RR73 again with no second log; keepcq,nonstd unchanged.
- FT8 / FT4 on a time server's time (Kurt DL5EAG: his tablet's clock drifts and an NTP app cannot set Android's clock): NtpClock sends four SNTP requests to time.cloudflare.com (then pool.ntp.org, then time.google.com when one does not answer) when decoding starts and every 30 minutes, keeps the answer with the shortest round trip, and Ft8Engine::nowMs (all FT8 / FT4 timing: slots, transmit, decode windows) adds the correction, as do the log's times. The FTx tab says so when the device's clock is 0.3 s or more off; the privacy policy names the time server. Checked on the FLEX with the device clock skewed 2 s back (AETHER_CLOCK_SKEW_MS, development): the server measured -1.994 s, round trip 20 ms; 508 decodes in 2 minutes with a median DT of 0.1 s.

**Build 123** (2026-10-27), iPhone and iPad, and Android beta 38:
- Audio output on Android (the user, beta 37, a phone with a Bluetooth headset: the choice did nothing): Qt 6.12 opens the receive and microphone streams each on one device (AAudio with that device's id; its QtAudioDeviceManager lists one Bluetooth device, ranks Bluetooth before the built-in speaker and microphone, and routes by itself when a stream opens), so Android's communication device and speakerphone never moved them, and the log showed both streams on "Bluetooth" whatever the choice. Now the choice picks the devices the streams open on (restarting running ones): Speaker the built-in speaker and microphone, Bluetooth media Qt's default output and the built-in microphone, Auto Qt's defaults. The Android branch was compiled here by a temporary switch; the routing is to verify on the phone. On the iPad the choices already worked (build 122's log: Speaker route Speaker, Bluetooth route BluetoothA2DPOutput); the help text in the app and the manual now says which speaker and microphone each choice uses. Band from outside every band (the user: a pan dragged from 40 m to 6.94 MHz, Band did nothing): the radio's band zoom does nothing there, so the pan goes first to the slice's band, or else the nearest band's middle; checked on the FLEX from 13.990 (the whole of 20 m with its band plan). The decoded CW strip starts under the waterfall's frequency scale (SpectrumItem's new axisHeight), which it covered; rendered on the FLEX at 440x956 in CW with decoded text. S left out of the transmit meter choices (the user; a stored S falls back to the side's default).

**Build 122** (2026-10-27), iPhone and iPad, and Android beta 37 (build 121 did not compile for iOS: the memory readout's mach include sat inside the Linux block):
- The spectrum and waterfall on the GPU (Mike VA3MW: the app took much of the battery on an iPhone and an iPad even at a low frame rate in 2D). SpectrumItem was a QQuickPaintedItem: every spectrum frame and waterfall row repainted the whole item with QPainter at the screen's pixel density and sent it to the GPU as a new texture. It is now a QQuickItem with scene graph nodes: the waterfall is a ring of rows in one texture (WaterfallTexture, a QSGTexture whose commitTextureOperations sends only the rows written since the last frame, the whole picture only after a resize, zoom or retune), shown as the ring's two parts; the trace and its gradient fill are vertex geometry, the grid rectangles; the frequency axis and band plan strip are an image painted in updatePolish only when the view or the plan changes; a frame is drawn only when data came, and in 3D the trace's frames draw nothing. The software renderer (screenshots without a GPU) paints the trace into an image. Measured with Qt's frame timing on Mesa llvmpipe (this PC has no GPU): the CPU part of a frame went from 4.6-5.7 ms to 0.07-0.12 ms; the texture took two whole pictures at the start and single rows after. Checked on the FLEX: landscape, portrait, a turn, a zoom out and back (the history remapped), the 3D view, and TUNE 5 s at the radio's tune power on 14.250 (the TX rows from the trace), the frequency put back. "2 analog" meters (the user): the second reading of a side as an analog face beside the first, mirrored on the right, on iPad and Mac; where a face would be under 60 px, the two readings are two bars as before; the chooser hides it on phones. Rendered on the FLEX at 1180x820, 820x1180, 1440x900, 390x844 and in the TX preview (SWR, Mic, ALC, Power). The app's memory in the top row (the user): iOS / macOS physical footprint (task_info TASK_VM_INFO), Android resident set; also in the network details and the minute's performance log line. Development: the desktop build asks for an OpenGL 3.3 core context, so the 3D view's GLSL 150 shaders load on Mesa (it stayed empty in a compatibility context); AETHER_3D_KEEP keeps the 3D view on a GPU too slow for it. CI: a preview build (mobile/PREVIEW: internal TestFlight group only, the APK a run artifact) and the Mobile release workflow that hands the same builds to the testers.

**Build 120** (2026-10-26), iPhone and iPad, and Android beta 36:
- Android: Qt's accessibility bridge only with a screen reader. Kurt DL5EAG's Lenovo tablet froze into "not responding" about 5 s into every held PTT; his screenshots of Settings › Accessibility showed the Lenovo pen service and the accessibility menu on, TalkBack off. Qt 6.12 turns its bridge on for any accessibility service (AccessibilityStateChangeListener) and answers each query by blocking Android's UI thread on the Qt thread. OksaActivity (our QtActivity) sets QT_ANDROID_DISABLE_ACCESSIBILITY before Qt starts unless touch exploration is on, and follows touch exploration after; compiled against Qt's and Android's jars here, the full build is CI's. Audio output (Larry Averbeck, S26 Ultra: no or weak sound with Bluetooth on): Controls › Audio › Auto (the hands-free headset as before), Speaker (Android: the built-in speaker as the communication device; iOS: no Bluetooth options) or Bluetooth media (Android's media route; iOS: A2DP only, the built-in microphone); remembered, applied at once; the routing itself is to verify on devices. Step buttons repeat while held (Larry): from 0.4 s, 8 a second; a step within 0.5 s of the last counts from its target, so a radio slow to confirm does not hold the repeat back. Checked on the FLEX over real SmartLink with the new shot-plan hold: +100 held 2 s made 14 steps, −100 1 s 5, +1k 1.5 s 9, the radio at the target at release; on the LAN 1 s made 5; the frequency was put back. Bar meters take the width the slice chips leave (Juho: the free room beside the TX slice chip): up to 320 px in landscape (was 110), the second chip column's room in portrait with one slice; analog faces keep their size (a wider face grows taller; tried, it took 30 px from the spectrum). Rendered with the demo and on the FLEX with three slices. Development: --connect smartlink, shot-plan hold and freq.

**Build 119** (2026-10-26), iPhone and iPad, and Android beta 35:
- the CW strip over the waterfall fills as writing does, the top line first and then the bottom one, which moves up when full, breaking between words (the user, on the iPad). The engine keeps a strip of the decoded text with whitespace runs as one space and its first character's place in the whole stream (cwStrip, cwStripStart; all appends go through appendCwDecoded); the strip wraps the new characters as they come, so a written line stays as it is, and starts over from the tail when cleared, the away mark is taken back or the width changes. Rendered on the FLEX on the phone in three steps (one line, two, moved up, breaks between words) and on the iPad. Phone paddles at the edges (the user, Android): dit at the left edge and dah at the right, Controls between in portrait (Close in the drawer) and the step buttons in landscape, one paddle per thumb; PttButton's paddleSide shows one paddle and both halves share the page's paddleState. In a low panadapter (a phone in landscape) the CW strip sits between the zoom / Band and + Slice / + Pan buttons instead of under them. Rendered on the FLEX at 412x915 (and the drawer open), 915x412 and 844x390. The call being worked (Engine.workedCall): the Log tab's Call field and the CW tab's new Call row (heard calls as buttons, the pick with ✕) are the same call, and CW memories send it for {HIS} ({CALL} stays your own); a memory with {HIS} and no call says so and sends nothing. Checked on the FLEX: a tap on K0II on the CW tab filled the Log's Call field (lookup United States), and M12 "{HIS} TU" sent "cwx send K0II TU" at 10 W on 14.045 CW; with no call it was refused. Power, frequency, mode and M12 restored. A real FT8 QSO with the desktop development build's own QRZ.com, Wavelog and DX cluster (secrets now kept on the desktop): --ft8-test 300,answer on 20 m at 10 W answered CQ IZ5CMG JN53, R+04 / -02, RR73 and 73; the QSO went into the ADIF log and to Wavelog ("online log: sent, 0 left"), the DX cluster stayed connected the whole run (26 spots).
Every logged QSO gets the lookup's fields (the user: "all modes that fill the call in"): logQso takes the Log tab's card when it is for the call, else the lookup cache, else starts a QRZ.com lookup and writes the QSO when it answers (at most 8 s; on app close at once); without an account, or when the lookup fails, cty.dat's country, continent and CQ zone. An empty field keeps what the QSO had (an FT8 grid). Checked on the FLEX with LogDry (printed, not logged or sent): IZ5CMG waited for QRZ.com and got name, QTH, JN53pg, Italy; again from the cache; PD4RD with EU and CQ zone 14 from cty.dat; an unknown call got cty.dat's country and kept its FT8 grid. IZ5CMG sent a QSL card for the test QSO within ten minutes. Two connects that never were (Kurt DL5EAG's log warned at every tap: "unique connections require a pointer to member function"): Qt::UniqueConnection refuses a lambda, so the tap on / off PTT's 3-minute limit never stopped a transmission, and FT8 / FT4's resync on the slice's mode change never ran; both are member functions now. Checked on the FLEX: tap on / off at 10 W on 14.250 USB keyed and unkeyed without the warning, an FT8 decode run without it. Kurt's tablet (Lenovo TB128FU): tap on / off works, a held PTT gives "not responding" in about 5 s every time; the trace has Android's UI thread in JNI NewString, the path Qt's accessibility bridge takes after a blocking call to the GUI thread, so an accessibility service on the tablet is the lead (asked).

**Build 118** (2026-10-26), iPhone and iPad, and Android beta 34:
- the info panel's speaker shows the slice volume as a number beside it (Jae Smith A26 and Jeff Schmeichel S22 reported low receive audio, and a new slice plays at 50). Tried as a ramp filling beside the cone (too small to read on a phone) and as the number on the icon (cramped, 100 did not fit). Rendered on the FLEX on the phone at 3x (portrait, 915x412, 844x390) and on the iPad at 2x. Jeff confirmed it: at 100 the audio was much better and he could take the equalizer back down, so no receive boost. Stall watch (Kurt DL5EAG, Lenovo TB128FU on Android 13: "not responding" about 4.7 s into a held PTT, in 3D and in 2D): StallWatch posts a beat to the GUI thread every 200 ms from its own thread and logs "GUI thread stalled N ms" at 1, 2, 4 ... s with transmit, the window's last event and the last frame, writes the same to stall.txt for a run Android closes, and logs "no frame drawn" when the render stops for 2 s while the GUI thread answers. Checked on the FLEX with `--keys Stall=2500` and a run killed mid-stall (the next start logged "previous run"). The Android symbol check now lays the symbols out with the system's fallbacks and names the fonts that draw them (the old check named ☰ ▾ ⌫ missing on phones that drew them); run once on the desktop through a temporary switch. Four bar meters were already possible (Two bars at both ends; a tap anywhere on a meter opens its chooser).

**Build 117** (2026-10-26), iPhone and iPad, and Android beta 33:
- The decoded CW strip over the waterfall (WA2N, beta 32): the decoder's line breaks after long pauses (the app 17 minutes in the background on CW decode) stacked a column of text over the spectrum, as eliding works on single lines only. Now two lines, clipped, cut by hand into full lines of the monospace font, newest at the bottom right. Rendered on the FLEX with the 11 lines from his screenshot at 412x915 and 1366x1024; at cover-screen sizes (340x374, 260x300) the whole operate page does not fit, not only the strip.
- CW decoder marks: `[away HH:MM]` when the app leaves the front with the decoder running, `[back HH:MM]` when it returns; back with nothing new, the away mark goes. Checked on the FLEX with `--keys Away,Back,...,Text=...`.
- A phone turning (WA2N, beta 30: dark for seconds, the waterfall empty): Theme's window width and height update one after the other, so the square window in between read as a tablet and Main's Loader built the tablet page and then the phone page again, with a new spectrum, 3D view and waterfall. The page choice now settles with Qt.callLater. The waterfall keeps its rows at one pixel each and never loses height (a lower window shows the newest rows); the width is scaled from the picture before the turn's burst of sizes. Checked on the FLEX with ShotPlan's new `size` command: 412x915 → 915x412 → 412x915 and 1366x1024 → 1024x1366 → back, the waterfall full 0.1 s after each turn.
- OperateKeys' `focusChanged` function clashed with the Item signal (a QML warning in WA2N's log); renamed `pageFocusChanged`. The function did run before too: Space PTT released on focus loss in both versions. Checked on the FLEX: Space keyed 10 W on 14.074 USB, taking the focus off the page released it (`xmit 0`); mode, power and DAX transmit restored.

**Build 116** (2026-10-25), iPhone and iPad, and Android beta 32:
- FTx: Show › CQ POTA (Ryland WW4GA): CQ rows whose second word is POTA. Answering one logs SIG POTA (the park is not in an FT8 message, so SIG_INFO stays empty). The Show row is now two rows of three (one row from 560 pt). Rendered on the FLEX in the iPad's pinned panel and drawer and on 440 and 390 pt phones.
- Log tab: Heard in CW. MobileEngine::cwCalls takes the callsigns in the decoded CW that come after DE or are heard twice (a prefix, a digit, a suffix ending in a letter, portable parts either side; the cty table must know it; not your own call, not [TX] lines), newest first, three at most (two under 420 pt); a tap puts one in the Call field. Checked with sample texts and a --cw-text run (DL/OH2BUA/P and K4ABC offered, 5NN / 599 / TU not); two --cw-decode-test runs on 7.0235 and 14.025 heard only noise (the CQ WW SSB weekend).
- No visible change: `--shot-plan` (ShotPlan) renders the manual's 42 pictures in about 6 minutes (one run per window size instead of one of about 45 s per picture), and the notch popup's sample notch leaves with the popup.

**Build 115** (2026-10-25), iPhone and iPad, and Android beta 31:
- RADE receive: RADEEngine::feedRxAudio decodes only what it gets as channel 1 (the desktop's DAX channel); the app handed the slice's own channel, so a slice on DAX 2 (a tester's iPad) or 3 decoded nothing while a strong RADE signal showed in the passband. The app picks the slice's channel and hands it as 1. And a DAX stream create sent while the old stream's remove was decided but not yet sent got the old stream back, which the remove then took (FT8 decoders letting the channel go, RADE taking it while its engine started: no audio at all): PanadapterStream now waits with the create until RadioModel has sent the remove (notifyDaxRemoveSent). Every 10 s RADE logs what reached its decoder (kB, peak dBFS, synced and SNR), and sync changes. Checked on the FLEX on 3.622 MHz: synced at up to 11 dB SNR in three runs, "end of over from DL5AI" decoded, also when the create had to wait.
- RADE transmit: a PTT released before the radio's MOX came back asked for the end of over while the audio gate (opened by MOX) was shut, so its frames and the callsign were dropped ("PTT gate closed (EOO race?)"), and the late MOX began the over again. The end of over now waits for MOX (3 s at most) and that MOX does not restart it. Checked on the FLEX with a quick tap on 14.250: 0.19 s on the air, no dropped frames, released.
- FreeDV list: stations transmitting first, then by the server's last_tx (FreeDvClient now takes it from tx_report, also from the bulk snapshot), then by frequency; "TX n min ago", refreshed every 30 s while shown. A tap on a non-RADE (SSB) station turns RADE off and sets its LSB / USB (a tester heard nothing from an LSB station with RADE still on).
- FTx: New Call and New Call on Band (WSJT-X's colours #00ffff / #99ffff, after the new continent, zone and DXCC) and a New call filter (its own row flag, so a new DXCC that is also a new call shows); the Show row wraps to two rows in the iPad's pinned panel.
- QSO log in memory: a compact entry per contact (call, band, mode, submode, date, time, CQ zone; full records only in the file, the newest 50 kept for the Log tab), parsed one record at a time; read in a worker at start. Measured: 20 001 contacts +3.3 MB (was +54 MB), 120 001 +17 MB, read in 2.8 s in the background; 100 000 imported in 4.4 s.
- A very low landscape window (under 400 pt) has no band row: the Band button there opens the bands and Whole band (WA2N on beta 30: "I can't find a way to change bands in landscape").
- The phone page takes the keyboard as the tablet page (OperateKeys, shared): arrows, Shift, Space PTT, + / −, F, F1–F12, Esc and the CW paddles. Checked with a --keys test on both pages.
- No slice line in voice modes (the user: not needed in phone); the frequency flag stays. The CW callsign notice points to Radio Setup. The notch preview uses a notch in view (else a sample one in the passband) and the No slice preview draws no slice.

**Build 114** (2026-10-25), iPhone and iPad, and Android beta 30 (also brings beta 29's sliders, keypads, ⋯ menu and text fields to the iPhone and iPad):
- ADIF import of a big logbook: about 20 000 contacts froze the display, iOS closed the app (the contacts written by then were kept: 19 324 on the iPad) and Android hung with a black screen until force-stopped (17 912 written). Each contact was appended with its own file open and, on Android, followed by a copy of the whole growing log to Documents/OksaSDR (QsoLog::append → writeMirror), all on the GUI thread. The import now reads the file on the GUI thread (Android content:// URLs), then parses, skips duplicates and writes all new contacts in one append and one mirror copy in a worker thread (QsoLog::writeRecords, one writer at a time under a mutex); the GUI thread adds them to the list and adifImported() reports the counts ("Importing…" meanwhile). Measured on the desktop: 20 000 contacts in 0.85 s, the same file again (all duplicates) in 0.4 s; a 20 001-contact log loads in about 0.3 s more at start and takes about 54 MB more memory.
- SmartLink Sign out takes two taps (the first arms it as "Sign out?" for 3 s): after the force stop, Android signed in from the kept token and 0.8 s later went to signedOut with no failure logged, which only the Sign out button does; the account row appears where the "Signing in" line was, as the page grows.

**Android beta 29** (2026-10-25), Android only (no TestFlight build; iPhone and iPad get it with the next build):
- Sliders: the Basic style takes a slider's fill from palette.dark and its track from palette.midlight, and Android's system palette has dark darker than midlight, so the filled part looked empty and the rest filled (EI9JE; the Linux renders had it too, iOS and the Mac did not). The window now sets the palette (dark = the app's blue, midlight = the raised surface, window / windowText / light for menus, base / text / placeholderText for text fields), so sliders, switches, progress bars and text fields look the same everywhere: the ⋯ menu is dark instead of a light system menu, and text fields are dark as on an iPad in dark mode (a light system palette made them white, as in the manual's pictures). Checked on the iPad (build 112, iOS palette): its sliders were already the right way round.
- The IP address and frequency keypads have the phone order (1 2 3 on top) instead of the calculator's (EI9JE), and the delete key is drawn (BackspaceIcon): "⌫" is missing from Android's font and showed as a box. Checked in renders on the phone and the iPad.

**Build 113** (2026-10-25), iPhone and iPad, and Android beta 28:
- Meters: each meter's style is Analog, Bar or Two bars (MobileMeterStyleLeft / Right; until chosen the analog setting, and a left meter whose second transmit bar was chosen keeps two bars). Analog is no longer forced into bars while transmitting (VA3MW: no choice for the third meter, and the analog face turned into bars on the air). With Two bars each bar has its own receive and transmit choice (the lower bars' MobileMeterLeft2 / Right2 and their Tx), so up to four readings. The bar face (BarGauge) has the analog face's scale (shared MeterMarks: S1…S9, +20/+40/+60 in red; five values and the alarm range on the others), the name and value above it as on the analog face, a thicker bar and an amber peak marker across it; a low bar keeps the ticks only; "-91 dBm" drops its unit where it does not fit (it showed "… dBm", also on the analog face). Checked on the FLEX with TUNE at 8 W: SWR 1.2 on the left upper bar with its peak marker, Power 8 W on the analog face; rendered on the iPhone portrait and landscape and the iPad.
- The slice marker is a white dashed line with the frequency beside it at the foot of the spectrum (on the side away from the passband; in 3D at the top of the waterfall): the orange line beside the yellow filter edges read as a third edge.
- FT8 / FT4 decoding stayed on when the FT8 slice went back to voice (VA3MW, build 112 on an iPhone: a band button brought DIGU from the radio's band memory, then LSB by hand and no PTT, which hides while decoding). The FT8 slice leaving DIGU / DIGL for 1.2 s ends decoding (after RADE's end the radio's LSB echo comes 0.1 s before the FTx button's DIGU, which must not stop it), and the pan goes back from the 4 kHz FT8 view to its earlier width. Checked on the FLEX: LSB during decoding ended it, PTT came back and the pan went back to 32 kHz; RADE on, then the FTx 20m button kept decoding.
- RADE: the filter chooser's widths centre on RADE's 1500 Hz while RADE is on (DIGL presets had centred on the radio's digl_offset, 2210 Hz); the radio answers RADE's mode change with that mode's remembered filter after RADE's own (from LSB it stayed 3 kHz), so RADE sets its filter again once the mode is in. Disconnect ends RADE first (its unmute was dropped once the link was down); a settings flag notes RADE's mute and the next connection unmutes the slice and restores voice if the app closed or the link dropped during RADE. RADE off always unmutes (not the split TX slice) and no longer takes its own DIGL as the mode to restore (a quick off/on did): unknown, it is the band's voice mode with a 2.7 kHz filter. An FTx band button and Decode end RADE first and open a filter too narrow for FT8 to 3 kHz. Checked on the FLEX: SSB → RADE → off back to LSB −2800..−100 unmuted; RADE → FTx 20m decoding on 14.074; Disconnect and a killed app during RADE both came back unmuted in LSB; the 300 and 2k presets under RADE gave −1650..−1350 and −2500..−500.
- FT8 / FT4 transmit was refused from build 108 / beta 26 on (WA2N's log on beta 27: "PTT press ignored: The TX slice is in DIGU … digi: Transmit refused"): the voice-PTT check added in build 108 exempted RADE but not FT8 / FT4, which keys through the same setPtt. FT8 / FT4 keying now passes it. Checked on the FLEX: a CQ went out at 8.4 W (SWR 1.2) and DL1EGU's answer got the report; voice PTT in DIGU is still refused (nothing keyed).

**Build 112** (2026-10-25), iPhone and iPad, and Android beta 27:
- FreeDV tab: RADE moved from the Digi tab (now FTx), FreeDV Reporter for the app's RADE (asked by W5SLS: his station never showed on qso.freedv.org) through the engine's FreeDvClient, as the desktop: off by default; while RADE is on it reports callsign, grid, version, frequency (once tuning settles), transmit (not tune / ATU) and RADE's SNR and end-of-over calls; the message field; the os field now says ios / android. The station list (fetched only while a FreeDV tab shows; the drawer's and the pinned panel's counted apart) lists this band or all bands by frequency with grid, distance, mode, TX now, whom each hears and its message; a tap tunes there and turns RADE on for a RADEV1 station. Checked on the FLEX, receive only: qso.freedv.org listed OH6OKSA KP32EQ OksaSDR 14.2360 Receiving, the app's own list showed it outlined, and it left the site when RADE went off; a tap on the demo tuned to 14.236 and turned RADE on.
- The control buttons are one size, in a grid of five (or one row on a wide panel), with short labels (Prof, Mem, FTx, FreeDV); the sections' headings keep the full names.
- The display (spectrum, waterfall, 3D) paused whenever the app was not Active; iOS reports Inactive while the app is still on screen (a system prompt, Control Center), and a tester's iPhone Duo (build 111, iOS 27.2) stayed Inactive for three minutes while in use, its display frozen. It now pauses only when the app is suspended (in the background); every app state change is in the log. Transmit still stops on Inactive.
- RADE left a band muted (F5GNM: no audio on 40 m only after trying RADE there, not cured by restarting the radio, cured by reinstalling): RADE mutes the slice and sets DIGU / DIGL, and the radio keeps each band's slice settings per client when the slice leaves the band, so a band change under RADE stored the band muted (a reinstall is a new client to the radio). A band button, a frequency on another band or a mode other than DIGU / DIGL now ends RADE first; two seconds after a band change a muted slice says so. Checked on the FLEX: RADE on 7.180, the 20m button unmuted the slice before the band change, and 40 m came back unmuted.
- Android shows the beta's number in brackets (release.env's TAG android-beta-N shows N; the tester saw (39) for beta 26, a run counter, and the compile check's run at that). versionCode stays the workflow's run number, which only grows.
- DX cluster status line (View tab), as the logbook's and PSK Reporter's: connected to which server as whom, the spots so far and the latest one's time (UTC), connecting, or the reason it failed (its own property and signal, so arriving spots do not refresh the server fields being edited).

**Build 111** (2026-10-25), iPhone and iPad (Mac only), Android unchanged (run 110 was the unsigned compile check on mobile/ci-check, never on TestFlight):
- Mac typing, third step: build 109's log showed no input-method event (the filter worked) and still no key event anywhere in Qt for the whole session, while build 107's GameController had seen the keys: the Mac hands the app the keys, but UIKit passes none to Qt's view (another Qt iPad app on a Mac has the same, unsolved). A focused text field on a Mac now opens Apple's own text box (UIAlertController with a UITextField) holding the field's text; OK or Return types it into the field key by key and presses Return (validators, textEdited, accepted and editingFinished as for typing), then the field lets go of focus so the box does not reopen. Tested on the desktop with AETHER_MAC_ENTRY_TEST (the Log tab's comment field: "Test 123 äö/" in, focus released, one box). The log now notes the first key presses UIKit hands the app (UIApplication sendEvent:, no characters), the windows, the key window and the first responder, so the next log tells where the keys go if the box does not take them either. The key-event log no longer shows the typed text (a password could be typed).

**Build 109** (2026-10-24), iPhone and iPad, and Android beta 26:
- Mac typing, second step: build 108's log showed GameController seeing no keys and still no key or text event at the field, so the GameController handler was not the cause. Qt's iOS view hands a key press to Qt only when no input method is active (quiview.mm handlePresses), and on a Mac the typed text never reaches Qt's text responder. On a Mac the text fields now report no input method (an application event filter answers ImEnabled false), so each press comes with its text as a key event, as on a desktop; tested with Qt Quick's TextField / TextArea (letters, ä ö, /, Backspace, Return). The log names the first keys that arrive. To verify on the tester's MacBook.
- A sideways swipe inside the open controls drawer tuned the radio (Android beta 25, WA2N): Qt passes a drag inside a popup on to the drag handlers under it (taps it blocks; reproduced with a plain Qt test). The display, the FT8 TX band, the filter edges and the notch handles leave the drawer's area out (containmentMask); the part of the display still in view tunes as before. Checked on the FLEX: a drag above the drawer tunes, the same drag inside it does not.
- Android beta 26 also carries build 108's PTT message for DIGU / DIGL.

**Build 108** (2026-10-24), iPhone and iPad only:
- Fixed the build 107 crash on focusing a text field with a hardware keyboard attached: the keyboard table was autoreleased in a non-ARC file and freed ("-[NSBundle allObjects]: unrecognized selector", a tester's MacBook Pro); now an owned array.
- On a Mac the GameController keyboard handler is not started at all (Pentti's log: 0 key events reached the field, GameController saw every key); Qt's key events serve there, as on a desktop.
- Voice PTT with the TX slice in DIGU / DIGL: a plain message and no key or microphone (an Aurora tester saw only "did not accept"; the radio refuses voice in DIGU); the radio's own refusal reason no longer overwritten by the generic one.

**Build 107** (2026-10-24), iPhone and iPad, and Android beta 25:
- Three TX meters (asked by VA3MW; layout B of three shown): while transmitting the left meter splits into two bars, its own TX choice over a second one (default Mic), Power at the right; the second bar has its own chooser. --preview-tx shows the layout without keying.
- Mac: text fields take the keyboard (the GameController keyboard handler for Ctrl paddles / Space PTT is off while a field has focus); keyboard diagnostics in the log (Mac or not, the focused field, key and input-method events that reached it, keys GameController saw).

**Build 106** (2026-10-24), iPhone and iPad, and Android beta 24:
- FT8 / FT4: Last TX (peak power and SWR of the last transmission beside the band's RF power); Free (the TX tone to the widest gap among the stations heard in the last minute, in the CQ slot's period when chosen; tested on a busy 40 m: 50 signals, 2170 Hz clear by 23 Hz).
- PROC off only during each FT8 / FT4 transmission (and back on at quit): a screenshot run that quit mid-transmission had left it off.
- A saved QRZ.com account and Wavelog / Cloudlog key are checked at start, so the Log tab shows whether they sign in (it was blank after a restart; asked after build 105).

**Build 105** (2026-10-24), iPhone and iPad, and Android beta 23:
- Wavelog / Cloudlog: each logged QSO to the online logbook (API v1: auth check, POST api/qso, a queue kept in the settings for offline). Checked against the developer's Wavelog (key valid rw; a dry-run QSO with station 1 accepted).
- PSK Reporter: decoded stations reported over IPFIX (report.pskreporter.info:4739), off by default; packets checked byte by byte (templates, receiver and 55 sender records in two packets).
- Transverter setup in Radio Setup › XVTR (tested on the FLEX: created, set, removed).
- FT8 QSOs between two nonstandard calls without reports (stalled with SN9Q/QRP on the air).
- Thanks to Vesa OH6HPS, the first iPhone Duo user, for testing builds 102–105 on the device.
- The FT8 / FT4 RX mark always shown (it showed only during a QSO; asked after build 104 on the iPad): the last station worked, else the TX tone.
- The tablet layout pins the controls at the side only where the band buttons still fit beside them (the iPhone Duo's inner screen in landscape pushed the panel off the right edge in build 104); else the drawer. The info row goes under the frequency by the left column's width.

**Build 104** (2026-10-24), iPhone and iPad, and Android beta 22 (build 103 never ran: a wrong runner label):
- iOS CI on the xcode-27 image (the label macos-27 does not exist: the first try sat queued for four hours) (iOS 27 SDK): built with the iOS 26 SDK the app stayed in compatibility mode on the iPhone Duo's inner screen even with the scene manifest.
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

Ideas to think over later (2026-10-27):
- From upstream 26.10.3 (the engine fixes that matter here, taken as one
  build and checked on the radio at low power): per-band TUNE RCA
  suppression (#6524) and restoring only the outputs TUNE inhibited
  (#6526); the interlock source list (#6493); Auto squelch by mode and per
  slice and pan (#6464); the radio's Auto AGC-T where the firmware has it
  (#6508); the firmware-dependent NR level range (#6498); CWX quote
  encoding and macro status. Later perhaps: voice keyer recordings on the
  device (#6292), the DeepCW / DeepFist CW decoders (#4817, models
  downloaded on first use).
- 4O3A Power Genius XL (PGXL) control, with the Tuner Genius XL panel.
- Record the band and play it back, also on the air (the radio's slice
  record and play, which the engine has: SliceModel::setRecordOn /
  setPlayOn).
- CW autotune: a tap near a CW signal centres it on the pitch, even when the
  tap misses it (the peak in the spectrum around the tap).

## Waiting

- **Upstream (aethersdr/AetherSDR):** no longer pursued (2026-10-22); OksaSDR
  goes its own way and takes upstream changes only when useful.
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
