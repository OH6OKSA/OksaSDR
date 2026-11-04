# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Next build

Build 154 / beta 67 (the user, 2026-11-04: "Tehdään 1-4", then "Teen nyt vaan kaikki tähän bildiin"): **1. The Antenna Genius and an SO2R TGXL's channel** (Ben Morris's SO2R TGXL on an AG SO2R; his build 153 log, a FLEX-8600 over SmartLink on a mobile network, held nothing of the devices, so first **the devices' own words into the log**: the radio's amplifier status per device once per change, each direct link's info and first status, the PGXL setup group with the code hidden, the AMP meters the radio lists, the first readings and each transmission's highest; then, after 4O3A's AG TCP/IP API and the TGXL description, on the engine's AntennaGeniusModel: Radio Setup › Amp's AG address / Control / remote code, the Amp tab's ports with band, RX / TX antenna and the antennas allowed on the band (port set rxant= / txant= where allowed, asked first, not while transmitting), the engine's band recall made switchable and off (the desktop asserts the last antenna on every band change and at connect), an SO2R TGXL's Channel A / B (activate ch=); genius-mock --ag and --tgxl-so2r; tested against the mock registered with the FLEX: a TX antenna, an RX-only one keeping TX, back, the channel; the mock sends port get's terminator as the engine's model reads it). **2. The 4O3A audit's open points** (PGXL writes from a fresh setup read and no single-key fan write, sequence 1-255, a stale amp handle refreshed; the mock PGXL's meters reach the radio's definitions but not their values: Ben's log will show the real ones). **3. The connection doctor** (Connect page › Check the connection: network, name, where the radio is, TCP 4992 answered / refused / silent with the Local Network hint, the radio's firmware, stations and multiFLEX; SmartLink sign-in, listing and slots; tested against the FLEX, a dead address, a host without the port, a bad name and SmartLink). **4. The text size from the system** (iOS content size category, Android fontScale, View › Text size; Theme.px() on the panels', dialogs' and setup pages' 339 sizes, the operating screen and buttons kept: at 115 % a phone's step and PTT rows no longer fit; the sweep at 100 % identical to build 153's, at 130 % the operating screen unchanged). To verify: Ben's AG and SO2R channel, the text size on an iPhone and an Android phone at their largest settings.

Build 153 / beta 66 (the user, 2026-11-04: the crash log "Lisää seuraavaan buildiin"; Juho Kontio's crash and Ben Morris's PGXL / TGXL comment from build 152): **The SmartLink crash at Connect** (Juho: multiFLEX off, AetherSDR at his remote station, build 152 still crashed at Connect; his .ips from build 150 a SIGSEGV from the SmartLink socket): reproduced by a simulated refusal of "client gui" with AddressSanitizer: WanConnection::processLine erased a response callback's map node after the callback, whose refusal had ended the session and cleared the map (heap use after free); the callback now leaves the map before it runs. Upstream main still has this and build 152's destructor bug (upstream not pursued). **No retry after the radio refuses the app** (it only met the same refusal), an error saying why, and **Another station is on the radio** (Main.qml): on the radio's network with multiFLEX off the engine pauses before registering (multiFlexConflictDetected; the app sat connected with nothing), over SmartLink before connecting when the licence allows one station (the desktop's confirmClientSlotAvailability; the FLEX-6600M reports licensed_clients=2) and after a refusal from SmartLink's station list; Disconnect it and connect / Cancel. Tested on the FLEX with the cabin iMac's AetherSDR on it, multiFLEX off and a refusal simulated by local test patches (removed). **The crash log** (LogBuffer): kept lines also to run.log as they come, a crash signal's name and call stack (backtrace / _Unwind_Backtrace, alternate stack, then the system's own handling), the next start's notice and Log › Previous run (Share / Copy); tested with SIGSEGV. **PGXL / TGXL wording** (Ben Morris, iPhone 17 Pro Max, real devices: Control on, but where to Operate?): where the buttons are under Control, the address as the device's own (the reported one in the empty field, "the radio reports none"), "Read directly … (the radio does not report it)", *Use the address the radio reports* / *Clear the address*; the iPad over LTE and WireGuard reached genius-mock.py at an entered address (10.9.0.5 → 192.168.1.232), as a VPN user would. Then (the user: "voidaanko simuloida tilanne jossa radio luulee, että meillä on oikeasti pgxl ja tgxl"): **genius-mock.py --radio** registers the stand-ins with the FLEX as paired devices do (4O3A's PGXL amplifier-to-radio API: a non-GUI client per device, amplifier create, keepalive, ping; no interlock, no meters), and the radio then connects to the devices' own ports and relays: the PGXL gets operate=0/1, the TGXL operate set= / bypass set= and autotune, and with --radio-tune the TGXL's autotune keys the radio's tune carrier (transmit tune 1 / 0) as the real tuner does. Tested from this PC and the user's iPad over the home network, WireGuard on LTE and SmartLink without a VPN: the address filled from the radio, Operate / Standby, Bypass, Tune keying the radio (15 W), the relays, the fan, the remote code asked, entered and kept, a mock restart as a link outage. **4O3A's TGXL protocol description** (upstream discussion #2595, WP4SK; the user: these are on main, read them) checked against all the PGXL / TGXL code by a separate read: relays numbered 1 = C1, 2 = L, 3 = C2 (we sent 0..2: L moved C1, C2 moved L, in builds up to 152 and on the desktop), 3way=1 in info for the 1x3 model (the radio maps it to one_by_three, seen on the FLEX), operate set= / bypass set= and the device's state= / bypass= direct for a tuner the radio does not report, swr of either sign, a port live with a band or a frequency whatever feeds it; left unverified by the documents: antA's base, the PGXL setup write and its authcode, the PGXL sequence past 255, the SO2R model's channel. **Ben's real cause** (his FB update: On took continuous tapping): every telemetry frame rebuilt Radio Setup's device rows (the state maps in the Repeater's model), so a tap landed on a replaced button; a fixed model now, and the Amp tab's relay rows a count. Also: the radio's refusal of a relayed amp / tuner verb shown (FlexBackend's reply sink; a Tune refused with 5000003C did nothing on screen), Radio Setup's device headings and texts (which code, why asked, what works without a direct link), the Forward bars 0-100 W under 100 W, fast polling from the key. **Split by XIT** (Sebastian DG5CST, e-mail: his FLEX-6400M's second slice held by AetherSDR at his remote PC; a Maestro user splits by XIT): Controls › TX › Split, SPLIT then sets the active slice's xit_on / xit_freq (1 kHz CW, 5 kHz voice; hold +1 / +2 / +5, XIT's ±9.999 kHz), read back from another client on the FLEX. **The decoder's text in the Macros panel** (Tyler K0OTC, e-mail: the panel covered the CW decoder). **RIT / XIT steps on their grid** (the user on Android: from 9999 the steps went …9, −1 past zero). To verify: Juho's case on his radio, Ben's devices (the relays, the 1x3 switch, the meters the real PGXL creates on the radio), Seb's split by XIT.

Build 152 / beta 65 (the user, 2026-11-04: "Kaikki muut lisätään buildiin", then "mennään suoraan siihen tilanteeseen, että buildissa nuo laitteet toimivat ei pelkät mittarit"; N1MM-style broadcast left waiting, the tailnet shim noted under Later): **4O3A PGXL and TGXL** on the engine's AmpModel / TunerModel / PgxlConnection / TgxlConnection as the desktop wires them (MobileEngine4o3a.cpp, GeniusControls.qml, Radio Setup › Amp), readings and control, the remote code in the secure store; safety as agreed (control off by default, every change confirmed, the tuner's relays and antenna not while transmitting; the devices' own SWR / power / temperature guards and the radio's interlock not duplicated). Tested against genius-mock.py (real devices' frames from upstream's protocol tests) with the radio's device status fed in by the render tool's radiostatus. **APD** on a FLEX-8000 / Aurora (TX tab, Radio Setup › Transmit), tested with fed-in apd status. **The Power meter's scale** from the radio's rated power (Sloan Davis, TestFlight: an Aurora red past 100 W; the desktop's #484 rule), 2 kW with a PGXL in Operate. **The radio list's address on its own line** (Mike VA3MW's screenshot). **A SmartLink crash** (Juho Kontio: the app crashed with AetherSDR at his remote station): reproduced with the cabin PC's AetherSDR (started over SSH) and this app over SmartLink; AddressSanitizer showed ~WanConnection's socket signalling into members already destroyed (a heap use after free); fixed in the engine, clean under ASan over SmartLink and LAN. **The 4O3A readings on the four meters** (the user: the meters are the point, Controls only show more). **The meter chooser's receive and transmit in framed boxes tagged RX / TX** (the user: the border between them was only a small title). ccache in build-mobile. To verify: a real PGXL / TGXL (Mike VA3MW?), APD on a FLEX-8000 / Aurora (Mike's AU-520), the Aurora scale; calibration, reference and restart by the user's own run (scripts in the scratchpad).

Build 151 / beta 64 (the user, 2026-11-03: "Toteuta 1-5 ja vastaa Larrylle", then for the build's scope "Radio Setup -auditointi"; 2026-11-04 Mike Walker VA3MW's radio list, "Buildiin 151"): **Several radios by IP address or host name** (Mike, e-mail: by IP or by name, Tailscale MagicDNS too, as AetherSDR's in-radio tailnet shim puts a radio on a tailnet; with one remembered address changing radios took trial and error, and friends share radios): the Connect page keeps a list (MobileSavedRadios: address, the user's name, what the radio calls itself learned at each connect), a tap connects, + Add a radio, ✎ to name, change or remove (asked again), the old single address moved in; checked against the FLEX by the controls at 430x932 and 1366x1024. Configuring the in-radio shim itself (a Docker waveform on radios that run them, provisioned over the radio's LAN) is later. Larry Averbeck's five points from FB (beta 62, a Galaxy S26 Ultra in portrait with a stylus): the transmit lock's countdown never ran (a lambda with Qt::UniqueConnection, refused by Qt; a member function now) and **TX › At start: Hold to arm**; **View › Zoom buttons Left / Right**; **AppScrollBar** (8 px, always interactive) everywhere and **AppComboBox** lists inside the window's safe area. Then **the rest of SmartSDR's Radio Setup**, from an audit of the desktop's tabs against the app's (the user's rule: an operator may have only this app): a new RX tab (frequency calibration as the desktop runs it, cal_freq, freq_error_ppb cleared, radio pll_start, pll_done 0 → 1, a refused start putting the offset back; the 10 MHz reference; filter sharpness per mode), and on the other tabs the station name (the handshake's protocol-safe name, kept for later connects: AppSettings::renameEffectiveStation), multiFLEX, options, license, GPS, Restart the radio, the CW sideband, CWX sync, the RTTY mark default, the front speaker, the mic meter in receive, the radio's address with DHCP / static and private-address enforcement. Tested on the FLEX-6600M (4.3.16): each write by its control on the page and set back (station name, mic meter, CWX sync, CW sideband, RTTY mark, front speaker, filter levels and Auto, calibration frequency and offset at their values); the restart, DHCP, calibration, reference and private-address questions opened and cancelled. Found: the radio answers filter_sharpness, cw synccwx and rtty_mark_default with R|0 and no status, so the page showed the old value and a second tap sent it again (Voice Auto went off on the radio, set back): RadioModel::setFilterSharpness / setFilterSharpnessAuto / setRttyMarkDefault and the engine's CWX sync now follow the radio's OK, as FlexLib's setters. Then every write path was audited the same way (the user: "Eihän meillä ole muita vastaavia ongelmia"): the render tool's new setupget reads what the page shows, and each of 31 Radio Setup keys was written, read, set back and read on the FLEX: all follow. Two more had no status: radio name (sent unsanitized too) and radio callsign, where the page kept the old call and setting it back sent nothing (the radio held OH6OKS until set back by hand): RadioModel::setRadioNickname (FlexLib's character set) and setRadioCallsign follow the OK. interlock set answers 0x5000002D yet applies and reports the value, so the page is right there. xvtr create / set / remove all report (a test transverter made, set and removed on a side connection); the app's other raw commands (pan band_zoom and dBm range, profiles, memories, spots, USB cables) report or follow their OK, as tested before. The same firmware refuses radio set mf_enable, binaural_rx and low_latency_digital_modes with 0x50001000 (could not parse; the desktop sends the same and fails the same), so those three are shown, not set; enforce_private_ip_connections is shown only (the permission check stopped even a same-value write). Layout sweep against build 151's baseline: 27 sizes, no layout decision changed, 0 QML warnings; side by side only the band's signals differ. To verify: a real calibration, a reference change, a static address and the restart, each only with the user's word (the cabin radio may drop off the network); the countdown and Hold to arm on Larry's Galaxy.

Build 150 / beta 63 (the user, 2026-11-03: "Lisää 1-3 buildiin", and "tutkia muita mahdollisia focus ongelmia"): **Macros by PTT** (Tyler Akerman K0OTC, e-mail: the voice keyer at the bottom of the TX tab and the CW memories halfway down the CW tab took scrolling past sliders on a phone): a Macros button beside Controls (phone), before the controls button in a phone's landscape row, and beside PTT (tablet) opens MacrosPanel over the display, the F-keys' mapping: CW memories with the call being worked, the Digi tab's RTTY / PSK macros, the voice keyer; buttons only, not modal, PTT in use, closed by the drawer; checked on phones and tablets in USB, CW and RTTY without sending (Tyler: "That sounds like a perfect solution"). **Tune for the RF2K-S's own tuner** (Jeff Stinger, FB build 149: he tunes by sending a carrier and pressing Tune on the amplifier's screen over VNC, and on one phone the carrier stopped as the app left the screen): RF-KIT's API cannot start a tune (upstream's applet shows the tuner read only; RFC #6712's TUNE needs a community-modified server; the amplifier's TRX tune interface is Icom / Yaesu / Kenwood's; over TCI the RF2K-S takes only the frequency: upstream #2597 read its passive TCI client), so the Amp tab's Tune (Control on, asked first) puts it in STANDBY, keys the radio's TUNE at the TX tab's Tune power (4-39 W, the manual's 7.2) and keeps it with the app away for 20 s at most, stopping once the amplifier reports a tune done, in OPERATE, over 45 W, a link lost, Stop or 20 s. Keyed at 5 W on the FLEX against the stand-in (rf2k-mock.py's press-tune), with a separate read-only client logging the radio's interlock: every stop path, and the app killed (kill -9) left the radio to unkey by itself as the connection closed. The user would rather not have had a carrier with the app away; built bounded at the user's word. **The keyboard on the page** (the user's iPad, build 149's devlink log: Space reached an EQ slider): OperateKeys skipped controls inside the page (the pinned panel), a ComboBox kept the keyboard after its list closed, and the page's forceActiveFocus handed the focus down nested scopes; fixed, and the render tool's focussweep gives the keyboard to every control that takes it (no press) and reports any that keeps it: all 12 pinned sections (1366x1024), every drawer tab (430x932, FM's CTCSS list too) and Radio Setup, 0 kept. Also: the tablet page's fit probe never below half size (a band change dropped the slice and its icons threw "Incorrect argument radius" on the iPad); the controls tabs' widest label measured again with the type size (with the Amp tab at start the 7 + 6 rows fell to 5 + 5 + 3). Layout sweep against build 149's: 27 sizes, no layout decision changed, no QML warnings (side by side caught the tablet's PTT row growing into the display, fixed before). Found on the way: in UDP mode the RF2K-S follows N1MM's radio-info broadcasts, so an OksaSDR frequency broadcast (Fuad Arnautovic asked about one) would feed it without a cable: later. To verify: Tune with a real RF2K-S (Jeff is off the air for a long time; Sebastian?), the Macros panel by Tyler on his iPhone 15 Pro Max and iPad Pro.

Build 149 / beta 62 (the user, 2026-11-03, after Sebastian Schu DG5CST's test of build 148 on his RF2K-S: "The readings are all fine!"; all three of his and the user's points, to every tester in the same build): step 2 of the amplifier plan (below), Operate / Standby, the internal antenna and the error reset from the Amp tab behind Radio Setup › Amp › Control the amplifier (off by default), each confirmed, refused while the radio transmits, one at a time, shown only from the amplifier's readback; a 3-second peak marker on Forward, as the desktop's amplifier panel, with the amplifier read every 250 ms while the radio transmits (500 ms otherwise; Sebastian: "a peakhold would come handy due to the slow poll rate"); the amplifier's power, reflected power, SWR, temperature, voltage and current on every meter, receive and transmit (Sebastian asked for the output in the small right meter; the user: every meter). Tested against mobile/tools/rf2k-mock.py by the real taps: Control off sends nothing; Cancel and a tap beside the question send nothing; Operate, antenna 2, the reset (clearing the stand-in's error) and Standby each one write; a disabled antenna none; a refusal (409) shows the amplifier's reason; a slow amplifier shows Asking the amplifier… and holds the next change; the amplifier gone at the command shows No answer, then the lost link; keyed on the FLEX (5 W, USB, no audio, so no RF) the open question closed and nothing went, the poll at 4 a second, back to 2 after. RfkitConnection reports each command's outcome (commandFinished: accepted, refused with the reason, or unanswered), with tests. To verify: Sebastian on his RF2K-S (Operate / Standby, his antennas, a reset with the cause cleared). And, the user asking while it was built ("onko meillä ... PTT output valinta ja timings sekä tx band settings"; and: the user has no other program, so the radio's basic settings must be settable from OksaSDR): Radio Setup › Transmit got what keying an amplifier needs, after SmartSDR's Radio Setup and the desktop's Transmit tab / TX Band Settings: each output's delay after PTT (RCA TX1-3, ACC TX), the TX REQ inputs' polarity (active high asks first), and the TX band settings per band (RF and tune power, TX1-3 / ACC TX outputs, RCA / ACC TX REQ, PTT inhibit, HW ALC; turning a TX REQ on while active high asks first). The echo audit (--echo-audit-only) wrote each from the engine and read it back on the FLEX (19 settings, 0 without an echo, 0 not restored), and the page's own controls were checked there too. Found while testing: the FLEX shows the TX profile (and its keying) of the station holding Local PTT; the render client's Default profile has none, so a test transmitting from it leaves the radio showing 0 ms and no TX1 until the user's own station transmits again. Then the user: the COM port settings for the amplifier's frequency data (SmartSDR's and the desktop's USB Cables; the user had an ACOM 2000A and an RF2K-S on them) and everything else an amplifier needs: Radio Setup got a USB tab (the engine's UsbCableModel, FlexLib's commands: CAT with source and serial port, BCD, Bit's eight lines, LDPA, passthrough; forget an unplugged cable), Transmit got Inhibit during TUNE (the engine already applied the desktop's TuneInhibit* settings on TUNE / ATU; the page sets them) and PTT mic override. Found on the radio: it answers usb_cable set with R|0 and no status (the new value shows on the next connect), so the app applies a setting to the model on the radio's OK. A test tap reached the user's real "Acom 2000a" CAT cable (source tx_slice → tx_ant) while stand-in cables were meant; set back at once from the page and checked on a fresh connect; the tests since use stand-ins named to sort after it and log every command's cable. Not done: the 4O3A PGXL / TGXL over the network (need a tester); an audit of the rest of SmartSDR's Radio Setup (RX, filters, GPS, network) against the app's.

Build 148 / beta 61 (the user, 2026-11-02: "tehdään nuo molemmat", the render script fixes and the amplifier's read-only step): the RF-KIT RF2K-S read only (step 1 of the amplifier plan, below): Radio Setup › Amp and the Controls Amp tab, upstream's RfkitConnection in a read-only mode, tested against the stand-in mobile/tools/rf2k-mock.py with every failure (refused, not an RF-KIT, timed out, link lost) and the switch, background and forget paths, never a PUT or POST. To verify: Sebastian Schu DG5CST on his RF2K-S. The render tool: --only NAME,... renders just the pictures named (a minute or two instead of 15-18), update-docs.sh --sweep runs the layout sweep first and once, and the manual's RADE and FT8 runs give the TX slice back (--screenshot quit before the tests' restore; FT8's tuning moved TX too), checked on the FLEX.

Build 147 / beta 60 (the user, 2026-11-02: "Ota 1-3 ja digi välilehden vaihe 2"): PSK31 / PSK63, step 2 of the Digi tab (below), and three things from the user's build 146 tests on the iPad: a macro's Default shows the original in the editor (it closed it, so what came back could not be seen); ⌘1-⌘9, ⌘0 as F1-F10 (the Magic Keyboard has no F-keys; Ctrl on Android; Qt hands ⌘ on as ControlModifier); RTTY's bit clock sampling mid-bit (each edge pulled it toward the bit edges: -3 dB errors 1.3-4.0 % to 0.1-0.6 %, -6 dB 14.8-21.7 % to 8.1-12.6 %, tests/rtty_decoder_weak_signal_test). PSK: PskVaricode / PskEncoder / PskDecoder in the core (tests/psk_roundtrip_test: clean both speeds 6 / 12 / 20 Hz off, PSK31 -10 dB 1.9 %, -12 dB ~17 %, PSK63 -8 dB 1.4 %, a station 100 Hz away left alone, noise alone through the squelch none, AFC holding after the signal); the Digi tab's Mode row, a tap setting the carrier at the signal's centre, AFC, a green band on the display, PSK spots, the macros and field sending on the frequency received, Log as ADIF PSK / PSK31 or PSK63. Live: LZ1AEY's PSK31 CQ decoded on 14.07109 (2026-11-02 ~17:05 local); a QSO attempt found him gone, and the next run met the cabin's power cut (one phase out, the radio and its router unreachable ~17:15-17:50). Then the first PSK31 QSO: our CQ on 14.0705 at 25 W answered by F6FPU (Eric, Roussillon nr Lyon, JN25JI), reports and names exchanged through the tab's macros and field (his report IMD -16 dB, quality 98 % on his over), his 73; the run's over wait missed his final SK (decoded "va"), so the QSO went in by the Log path with its start time (logqso: PSK / PSK31 14.070500, 25 W, name and QTH from QRZ.com, sent to Wavelog). No PSK Reporter reports of the CQ (few receivers report PSK31). Not tested live: the RTTY clock fix (no RTTY on 20 m in the evening); the Digi tab's Log QSO time, which was the moment Log was pressed, now the time the call was picked. Also in 147: Pentti Pärnänen OH6PP's Mac typing (build 146 log: "first responder: none", the app's window not key, so UIKit gave the keys to no one and macOS beeped; typing worked after a text box opened and closed): when a text field takes focus on a Mac with no key routing, the app activates its window as Qt does (key window, its view the first responder), and logs the routing before and after. Not testable here (no Mac): to verify on Pentti's MacBook. Layout sweep against build 146's: 27 sizes, no layout decision changed, no QML warnings (a first run straight after the screenshots, the RADE shot having moved TX to slice C, showed the TX tag's 27 px; run again with TX on A, clean); build 147's is the new baseline. Screenshots refreshed, the manual's PSK31 picture (mode-psk) new; the Digi tab in PSK checked at 360 px and on the iPad. Later for PSK: reporting PSK31 decodes to PSK Reporter, as fldigi does.

Build 146 / beta 59 (2026-11-02, the user: "tee ha push"): RTTY transmit, step 1 of the Digi tab (below): the RTTY tab became Digi, with the calls heard as buttons, eight macros after fldigi's set (F1-F8, press and hold edits, a line showing what a macro sends), Text to send, Stop (Esc) and Log; RttyEncoder (ITA2 with US figures, 45.45 baud CPFSK, 1.5 stop bits, a mark lead and two LTRS) sends AFSK from the TX slice in RTTY, DIGL and DIGU through the FT8 / FT4 path. Switching RTTY / DIGL / DIGU keeps the station tuned in (the dial stayed put, the signal 2.3 kHz off the tones: the user's DIGU test on build 145); calls with long suffixes (OE100OEVSV) among the calls heard; {NAME} only from an operator's first name in the lookup (it sent a club's "AUSTRIAN"). Tested: the encoder against the decoder on both sidebands (10 checks); live at 10 W on 14.0875, RBN skimmers in eight countries decoding the CQs on the mark; the first QSO, OE100OEVSV (Vienna) on 7.0433 at 25 W, answered, worked and logged by the tab's buttons and sent to Wavelog; IT9EJW decoded in RTTY and DIGU on 21.0964. Layout sweep against build 144's (no build 145 sweep was kept): 27 sizes, no layout decision changed, no QML warnings; build 146's sweep is the new baseline. Screenshots refreshed; the manual's Digi tab picture now shows RTTY on, and a new one the calls heard and the macros (RTTY step 3). The manual: Logger32's coming logger32.ini setting for the address it listens on (Juho Kontio, 2026-11-02: a test version from Logger32's developer listens on all networks with it, no port forward; the firewall still needs the port). Not in a released Logger32 yet: the port forward stays the way until then; update the manual when the release has it. To verify: RTTY transmit from an iPhone, an iPad and an Android phone (the audio path is FT8's, verified on devices); F1-F8 and Esc on the iPad's Magic Keyboard.

Build 145 / beta 58 (2026-11-02, straight to the testers, the user: "meehän nämä nyt noilla korjauksilla"): RTTY received in DIGL / DIGU logs as RTTY on the signal's mark frequency (it went in as DATA on the carrier, 2.1-2.3 kHz off), and an RTTY spot keeps DIGL / DIGU, tuning its mark onto the mark tone (it switched the slice to the radio's RTTY mode). The conventions from the user's sources (vkcc on groups.io, rttycontesting.com) and DXLab WinWarbler: the mark the higher radio frequency, lower sideband the tradition, upper sideband as common with the tones reversed; spots and loggers give the mark. Tested on the FLEX: a spot on 14.085 tuned DIGU to 14.082705, DIGL to 14.087125, RTTY to 14.085, each keeping its mode; the Log tab showing 14.085 MHz RTTY on DIGU. Build 144 / beta 57 went to the testers the same morning (Mobile release workflow). Open: Pentti Pärnänen OH6PP's MacBook (macOS 27.0.1, build 144): typing into the Log tab's callsign field beeps and no key reaches the field; no keyboard code changed since 142; waiting for his full log (after a restart, and with the press-and-hold text box opened) before a fix.

Build 144 / beta 57, a preview on the user's own devices first (mobile/PREVIEW, 2026-11-02), in place of 143 (the user, 2026-11-01: RTTY needs a normal operating model like FTx, both LSB and USB, LSB the standard; read the standards, test everything): RTTY with its own Controls tab, the radio's RTTY mode (LSB), DIGL and DIGU AFSK with the decoder's mark following the sideband and the tap tuning each, Reverse fixed (its space was a shift below the mark tone), the squelch reading confidence on the settled stop bit (it dropped upper-sideband letters), the RTTY line kept with decoding off. Tested with test signals on both sidebands through the tab, FT8 / mode / RADE interplay, the tap tuning on live signals; not yet decoded on a live amateur RTTY station (none on the air during the tests): to do when there is activity. Next for RTTY: the decoder's bit clock toward mid-bit (weak-signal errors 12 % to 3 % at -12 dB in a simulation of its loop), after a live test. Layout sweep against build 143's: 27 sizes, no layout decision changed, no QML warnings; build 144's sweep is the new baseline; screenshots refreshed (the twelve-button Controls grid, the RTTY tab). Also in 144, from the 143 preview: Worked before from the Wavelog logbook, the QRZ.com Logbook both ways and a shared ADIF file in iCloud Drive / Google Drive / OneDrive (the two-way sync, below); counts with the locale's thousands separator. To verify there before the release: the QRZ.com Logbook with the user's real key (the key check, a QSO sent, the read-back's count against the logbook's); a shared file in iCloud Drive on the iPhone, the iPad and the Mac (chosen, read, a QSO added, still reached after a restart) and in Google Drive and OneDrive on Android (whether Drive appends with "wa" or the app rewrites the file); Wavelog's read on a device. Layout sweep against build 142's: 27 sizes, no layout decision changed (only the Log tab grew, checked at 440 and 360 px and on the iPad); build 143's sweep is the new baseline.

To verify in build 142 / beta 56: Backspace on Pentti Pärnänen OH6PP's MacBook in the Log tab's fields and the frequency entry, held Backspace repeating; a double click in a text field on a Mac opens no text box, a press and hold still does; the QRZ.com status in amber, not red, when QRZ.com does not answer (Sloan Davis's iPad), and red for a wrong password.

To verify in build 141 / beta 55: a voice transmission with receive on a phone's speaker, the start clean on a second receiver (the user heard the burst on Android beta 54; Jeff Stinger on his Pixel 7); a USB-C headset on Android (Jeff), and plugging one in during a session; an ADIF file from Drive picked on Kurt DL5EAG's tablet; the transmit lock by finger on a phone and an iPad (a short press does nothing, a second's hold arms, OA counts down) and Space on the iPad's Magic Keyboard; Answer CQ on a device (Kurt) and the presence limit counting down; an RTTY tap and an RTTY spot on a phone.

To verify in build 140 / beta 54: Space PTT on the user's iPad with the Magic Keyboard after typing a call in the pinned Log panel (a tap outside the field, or logging, gives the keyboard back); RTTY with a real station (decoded live in build 141's tests); the Mac text box only on press and hold, typing straight into the fields (Pentti Pärnänen OH6PP's MacBook).

To verify in build 139 / beta 53: Autotune on the CW tab and a tap beside a real, keyed CW signal (tested here on the 10 MHz carrier only); the CW pitch notice on a radio that reports 0; the notch panel by finger on a device (a hold, a width, Add; a tap outside leaves none); the eSSB tab on the air at low power with a second receiver; FTx Scroll › List only in the full-screen drawer on a phone and in the iPad's drawer (Michael Whaley offered to pilot it); VoiceOver and TalkBack on a device (the names read out, a double tap acts, a swipe steps a slider; a screen-reader user's view welcome). To verify in build 138 / beta 52: the − / + on pan, power, mic, DSP and Radio Setup levels with a finger; Show my transmission in the waterfall off and on during a transmission; an FTx band change clearing the old band's decodes; the Local Network hint on an iPhone with the permission off (Settings › Privacy & Security › Local Network); "In use by" on a SmartLink card. To verify in build 137 / beta 51: the felt taps on an iPhone and an Android phone (PTT press and release, the step buttons, − / +; none on an iPad or a Mac); the taller rows beside and under the frequency on a phone upright (STEP, the antennas, width, AGC, lock, REC and PLAY with a thumb); every panel's Close at the top right and its rows scrolling in a low window; connecting by host name (ABC on the keypad) over a VPN (Sebastian Schu); − / + on AGC-T, RF gain, volume and squelch (Larry Averbeck); the full-screen drawer with FTx on a phone and the iPhone Duo's inner screen (Michael Whaley). To verify in build 134 / beta 48 (and 133 / 47): the AGC panel from AGC-F and the volume panel from the speaker in the OnePlus Open's inner screen and in a narrow iPad window; the network panel on an iPhone and on a phone on its side (scrolls to Close). To verify in build 132 / beta 46: Logger32 with Juho Kontio's own Logger32 (QSOs arriving, the queue when the PC is off, over his VPN); the iPad in a narrow Split View or Stage Manager window (PTT on screen, the wrapped top). To verify in Android beta 45: the app in a split or pop-up window on the OnePlus Open's inner screen (portrait layout with the wrapped two-line header, PTT on screen), and the phone page as before on the outer screen in both orientations. To verify in build 129 / beta 43: record and playback on a device (the dot, then the triangle; with PTT during playback on the air at low power, a recording of speech, not band noise); F/R and Band on a phone in both orientations and on an iPad; the step buttons with STEP ×1 ×10 ×100; the eye in the SmartLink sign-in (Larry Averbeck's S26 Ultra). From beta 42 (checked on the user's phone 2026-10-27): the headset's hands-free reopen on connect and on Bluetooth media -> Auto; no background stall in five returns. Still to check: the microphone of each Audio output choice with a short PTT. From beta 41, checked on the user's phone 2026-10-27: quick Audio output changes never left receive silent (about 25 changes, every one reopened); the waterfall fix did not catch the return after another app. From build 125 / beta 40, checked on the user's phone with the headset 2026-10-27: every switch both ways ended with receive open (route errors and refused opens reopened within a second). From build 124 / beta 39: FT8 with Keep CQ on the air, the station's 73 after our RR73 (no second QSO, one log entry). From build 123 / beta 38: Audio output on Android with a Bluetooth headset (Speaker: the phone's speaker and microphone; Bluetooth media: the music link and the phone's microphone, if Qt's one Bluetooth output is the A2DP one; Auto: the headset with its microphone), the user's phone and Larry Averbeck's S26 Ultra. From build 122 / beta 37: the app's CPU in the top row against build 120 on an iPad, iPhone and Android phone with the same view (the spectrum drawing moved to the GPU), the 3D view and the waterfall on Metal, Vulkan and GLES devices, a phone turning and a zoom with the new waterfall ring, "2 analog" on an iPad and a Mac. From build 120 / beta 36: a held PTT on Kurt DL5EAG's Lenovo tablet with its accessibility services on (Lenovo pen, accessibility menu); Audio output Speaker and Bluetooth media on a phone with Bluetooth audio (Larry Averbeck's S26 Ultra), and on an iPhone; the bar meters' new width on devices. Still to verify from earlier builds: the paddles at the phone's edges with a squeeze and the tap on / off 3-minute stop on the air (119); a phone turning on a real Android phone with the 3D view (117); RADE transmit from a device with the quick-tap fix; Two bars with TUNE on a device's microphone (113); Wavelog sending, PSK Reporter spots and a transverter from a device (105). Queued (tester requests): a blank panadapter while audio plays (Larry; waiting for a log), an Amp tab for the Power Genius XL and Tuner Genius XL (Dennis Daniel, who may test it; the amplifier build below), the desktop TX chain on the eSSB tab (Jeff Stinger: a 10-band parametric EQ with presets, the desktop compressor, later a multiband compressor; with the de-esser, tube, exciter, reverb and a chain order, the engine has them: a build of its own, tested on the air), one-button setups (Jeff Stinger, 2026-11-01: one press to go from the phone's own speaker and microphone to a headset and change every setting with it, like the voice keyer's buttons: a named setup holding the app's Audio output, the radio's mic profile (the Prof tab loads them one at a time now) and the eSSB settings), a port after the radio's address for a direct connection (Sebastian Schu DG5CST, 2026-10-31: his router forwards e.g. tcp 56034 to the radio; the radio needs two ports: TCP for the commands (4992 on a LAN) and UDP for audio and the panadapter (the app sends to 4991 and 4992, the radio back to the app's own port); SmartLink with manual port forwarding gives both (TCP 4994, UDP 4993 by default) and the app already takes them from the SmartLink server, so first ask whether his 56034 is SmartLink's manual TCP port and which UDP port he forwards; a direct connection without SmartLink would need host, TCP port and UDP port, [IPv6] too, ":" on the address keypad, remembered), Wavelog API v2 tokens, the rest of upstream 26.10.3 as its own build.

## Planned: the Digi tab, keyboard modes as one (asked 2026-11-02)

The user: PSK31 too (bpsk31.com), and every keyboard digital mode thought
of as one whole, since they all need the same ways to answer, log and so
on: one Digi button in Controls, the mode chosen under it, each mode with
ready operating macros. FTx stays its own (its traffic runs by itself);
PSK and RTTY follow the same message norms. The user chose step 1 first
and the RTTY / DIGL / DIGU tuning fix to wait for it (no build 146 alone).

Conventions read first: fldigi runs RTTY, PSK31/63 and the rest from one
window with one macro set (its new-install defaults: CQ "CQ CQ CQ de
<MYCALL> x3 pse k", ANS "<CALL> <CALL> de <MYCALL> x3 kn", QSO "<CALL> de
<MYCALL>", KN "btu <NAME> <CALL> de <MYCALL> k", SK "tnx fer QSO <NAME>,
73 ... <CALL> de <MYCALL> sk", Me/Qth, Brag, Log QSO); a callsign clicked
in the received text goes to the log. bpsk31.com: PSK31 on USB, 14.070
and the band list, the same QSO shape ("CQ CQ CQ de CALL CALL CALL K",
"CALL de MYCALL pse K", BTU, SK), 595 instead of an automatic 599 to a
dirty signal, ALC at zero. SmartSDR's RTTY mode transmits the AFSK tones
sent to it (the mark set in its DSP settings), so RTTY goes out as audio
from this app in the RTTY mode, DIGL and DIGU alike, through the path
FT8 / FT4 use (PTT for digital audio, the TX slice, PROC off, the TX
level).

1. Step 1, RTTY (build 146 / beta 59): the RTTY tab becomes Digi; the
   received text with the calls heard (after DE, or twice) as buttons
   that set the call being worked (the Log tab's, as on the CW tab);
   macros like the CW memories (tap sends, press and hold edits; {CALL},
   {HIS}, {NAME} from the lookup, {RST}) with fldigi's set as defaults;
   a field to type and send; Stop; Log QSO (RTTY on the mark). Transmit:
   Baudot with letters / figures shifts and unshift on space, 45.45 baud
   CPFSK at the way's tones (mark the higher radio frequency), a short
   mark lead and tail. Tested: the encoder against the decoder on the
   desktop, every macro and the field by taps, then a live low-power
   transmission heard back (a second receiver or an RTTY skimmer).
2. Step 2, PSK31 / PSK63 (build 147 / beta 60): the mode row on the Digi
   tab; BPSK at 31.25 / 62.5 baud, Varicode, receive with AFC and a tap
   on the waterfall, transmit with the same macros; ADIF PSK / PSK31.
   Tested with test signals, then live on 14.070.
3. Later: contest macros (serial numbers), more modes (Olivia, MFSK...).

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

## Planned: two-way logbook sync (asked 2026-11-01)

The user: Wavelog is the main QSO database for many, so the app should
also read from it, and from every logbook it can reach when it starts
(cloud or self-hosted); for desktop programs, a shared cloud folder. The
app would pull the log at start (and after that only what is new), merge it
into its own log without duplicates (Wavelog's own rule: call, band and
mode within 15 minutes, FT4 as MFSK / FT4), so worked-before, New DXCC and
the FTx colours know everything; each QSO logged here keeps going out as
now. What each service offers (checked in their source or docs):
1. **Wavelog** (first): `POST /index.php/api/get_contacts_adif` with the
   existing key (a read-only key works), the station profile from
   `api/station_info/<key>`, and `fetchfromid` for only the new QSOs
   (Wavelog 1.8 and newer; `limit`, JSON with chosen fields from 2.4.1).
   Wavelog 3.1's API v2 (`api/v2/qso`, a `wl2_` token with `qso:read`)
   pages 5000 at a time.
2. **QRZ.com Logbook**: `ACTION=FETCH` with the logbook's key, 250 at a time
   after a log id (needs a QRZ subscription; upload with `ACTION=INSERT`).
3. **LoTW**: `lotwreport.adi` with the LoTW user name and password, QSOs and
   confirmations since the last fetch (the password in the secure store).
4. **A shared ADIF file** in iCloud Drive, Google Drive, Dropbox, OneDrive
   or Nextcloud, picked once: read at start, each new QSO appended, for
   desktop loggers (Log4OM can watch such a file; N1MM, DXKeeper and
   MacLoggerDX have no internet API).
5. **Club Log**: the whole log only (`getadif.php`, email and an app
   password, an API key from Club Log).
6. **Cloudlog**: no download API; worked-before one call at a time
   (`logbook_worked_status`).
Not possible: eQSL (others' cards only), HRDLog (upload only).
Decided (the user, 2026-11-01): Wavelog first, the pulled QSOs a separate
list. Step 1 is built (local, for the next build): Log tab › Wavelog ›
Worked before from the logbook (off by default) reads every station
location's QSOs (station_info, then get_contacts_adif 5000 at a time with
fetchfromid; at each start only the new ones, Read now at once) into
KnownLog (logbook-known.tsv in the app's data folder: call, band, mode,
submode, date, time, CQ zone). They count for worked before and dupes in
every mode, New call / DXCC, the FTx colours and Answer CQ, never go into
the ADIF log nor out again; a QSO the app sent there counts once (call,
band, mode group within 15 minutes). Tested against a mock Wavelog
(paging, two stations, the dedupe, an incremental read, an old Wavelog's
404, an unreachable server) and the user's own Wavelog (19 362 QSOs read
in about 25 s, 650 kB on disk). Steps 2 and 4 too (the user, 2026-11-01:
"kaksisuuntainen qrz lisätään", and a shared folder where a logger has no
cloud sync): QRZ.com Logbook both ways (Log tab: the logbook's API key;
each QSO with ACTION=INSERT, queued, a duplicate counted as sent; Worked
before from the logbook with ACTION=FETCH, 250 a page after the highest
app_qrzlog_logid, the HTML-escaped ADIF; qrzlog-known.tsv), and a shared
ADIF file (Choose… in the system picker: iOS keeps Qt's security-scoped
bookmark, Android takes a lasting read and write grant and appends through
the ContentResolver with "wa", rewriting where a provider cannot append;
read at start and on Read now, each QSO appended, never a new file in its
place; sharedfile-known.tsv keeps its last read). Each contact counts once
across the app's log and all three. Tested against a QRZ.com Logbook
stand-in (a wrong key, paging 250 + 250 + 103, INSERT and a duplicate, an
empty FETCH, Wavelog's and QRZ's copies of one QSO counted once) and on
desktop files (a Log4OM-style file with CRLF and no final newline, an
empty file, a file gone and back); to test on devices: a real QRZ
logbook key, and files in iCloud Drive, Google Drive and OneDrive on an
iPhone, an iPad, a Mac and Android. Next: LoTW confirmations, Club Log.

## Planned: usability (review of 2026-10-29)

From a review of every panel and drawer tab in 20 window sizes (phones
both ways, the iPhone Duo's and OnePlus Open's both screens, iPads, Mac,
split windows) and a look at the other FlexRadio clients. The goal: the
app feels like the phone's own apps. Which build each goes into is
decided one at a time; the ones marked *to discuss* need a closer talk
first.

Touch and feel:
- **Bigger touch areas** for the most used controls: the info row's items
  (slice, TX, speaker, lock, REC, PLAY, NB / NR) are about 22x33 px and
  STEP, ANT and AGC about 24 px tall, where Apple asks for 44 pt and Google
  48 dp. The touch area grows to 44 px; what shows can stay the size it is.
- **One panel base** for every panel: title, Close in the same place
  (top right, as in the AGC, volume, filter and meter panels now; TNF and
  Network still have it at the bottom) and scrolling where the window is
  low.
- **Haptics:** a light tap felt on PTT press and release, the step buttons
  and the slider steps. None anywhere yet. *To discuss:* which controls,
  and a setting to turn it off.

Text and large screens:
- **A text scale that follows the system text size** (iOS Dynamic Type,
  Android font scale): sizes are fixed pixels now, about 80 texts are
  11–13 px and the phone's info row about 10 px. One scale in the theme,
  13 px at the smallest. *To discuss:* how far it may grow before rows
  wrap or move to a second line, and checking every layout at the large
  sizes.
- **Tablets and Mac:** the scale stops at 1.15, so a 13" iPad shows
  12–16 px text; the connect page stretches over the full width (a
  maximum width, centred). On a MacBook the window's short side is about
  950–1100 px, so the scale stays near 1.0: phone-sized text, seen from
  twice the distance, and a stretched phone layout; an external monitor
  more so. A Mac window can be any size, so every size in between must
  work too, and a right click acts as the long press. *To discuss:* views
  side by side on a large screen (FTx and the log, two panadapters)
  instead of a larger phone layout.

Finding things:
- ~~A long press on the spectrum opens a menu~~: no menu (2026-10-30).
  Gone through with the user, only the notch and a memory "here" were left
  (a slice, a tap tune, FTx's bars, CW Autotune and zoom have their own
  controls; Mem's Save keeps the frequency being heard). The long press
  instead opens the notch panel for a new notch, set and confirmed with
  Add, as it added notches too easily by accident: *done in build 139*.
  The hidden gestures (the hold for a notch, the dBm scale drag, the
  divider's double tap, the SPLIT and memory holds, a tap that resets a
  value) go into the first-run hints.
- **First-run hints:** three or four short hints on the first start.
  *To discuss:* whether to have them, and which.

Screen reader:
- **VoiceOver and TalkBack:** names for every control *in build 139*
  (290 on the FLEX, none without; 18 before), a double tap acting, a
  swipe stepping a slider. Still to come: tuning that works with the
  screen reader (the frequency read out, a swipe up or down = one step).
  *To discuss:* with a tester who uses a screen reader.

Small ones: done in build 138 ("In use by" on SmartLink cards, 4.5:1
tags, scroll bars wherever a page scrolls).

Later, *to discuss:* a connection doctor (what blocks a connection:
the local network permission, the route, SmartLink, the radio's client
slots, in plain words).

Proposed order (a proposal, each build still decided on its own):
1. **Build A**, small but felt: the info row's touch areas, one panel
   base, haptics on PTT and the steps. *In build 137* (the header about
   30 px taller: 36 / 40 px rows, not a full 44).
2. **Build B:** the text scale with the system text size, the tablet
   scale, the connect page's maximum width, "In use by". *The connect
   page's width and "In use by" in build 138.*
3. **Build C:** first-run hints (the hidden gestures), tuning with the
   screen reader (its names, and the notch confirmation instead of the
   long-press menu, in build 139).
4. **Later:** views side by side on a tablet, the connection doctor.

## Released

**Build 142** (2026-11-01), iPhone and iPad, and Android beta 56:
- Backspace on a Mac (Pentti Pärnänen OH6PP, build 140: typing worked, Backspace did nothing). With the input method off, Qt's UIKit key mapping sends Backspace as key 8 with the text "\b", which a text field ignores; an application filter (AppleControlKeys) sends key 8 and 127 on as Qt::Key_Backspace, which also gives an iPad keyboard's Backspace to the frequency keypad. Checked with a new render tool command (keys, with \b sent the way UIKit sends it): "OH6PPX" became "OH6P", and stayed "OH6PPX" without the filter.
- The Mac text box no longer opens on a double click (Pentti): the second click's release did not always reach the field, so the hold timer ran on; any release anywhere and a double click stop it, and a press soon after a release arms nothing. Only a Mac shows the box, so not testable here.
- QRZ.com not answering (Sloan Davis, iPad Pro, build 140: "Socket timing out"): the account was saved, only the check after Save timed out (the user's Android phone timed out in the same hour; no other tester reported it). A network failure now shows in amber that the account is saved and lookups will try again; a refusal stays red ("did not accept the sign-in").
- The manual's Operating by mode (the user: a picture of every mode and the steps for every feature by mode): SSB, CW, AM and SAM, FM with repeaters, FT8 / FT4, RTTY, RADE and what works in every mode, eight new pictures rendered against the FLEX by update-manual-screenshots.sh (its new "modes" plan).
- Checked against build 141's sweep: 27 sizes, no layout decision changed (layout-diff clean; the info panel cut on phones on their side as before); the pictures differ only in the band's signals. Build 142's sweep is the new baseline.

**Build 141** (2026-11-01), iPhone and iPad, and Android beta 55:
- RTTY the right way round: the radio's RTTY mode is LSB (mark 2125, space 2295 Hz in the audio), and build 140 decoded nothing until Reverse. A tap on a signal, an RTTY spot or the panel's Autotune tunes onto the mark (SpectrumItem::rttyMarkNear: two peaks a shift apart; a spot retries for 8 s while the display fills), and an RTTY spot sets the radio's RTTY mode (it was DIGL). Squelch (on by default) holds back text below the decoder's confidence. Checked live in a Sunday RTTY contest (R8UT, UN8PT, RA6LIS, SP4NKJ decoded; a spot and Autotune from the space tone landed on the mark).
- No burst of band noise at the start of a transmission (Jeff Stinger, Pixel 7; the user heard it on Android beta 54 too): the microphone opens about 100 ms after the press and the radio keys 150-400 ms after it, and until then a phone's speaker played receive into the microphone. Receive now goes silent at the press (AudioEngine::setRxDucked; Qt 6.12 applies the volume as AAudio pulls from its buffer, so what is queued goes silent too) and comes back at the release, or with MON on 250 ms after the radio keys. Checked on the FLEX with a silent USB PTT, MON off and on; not yet with a real speaker and microphone.
- USB-C headsets on Android (Jeff Stinger): Qt 6.12.0's QtAudioDeviceManager does not know TYPE_USB_HEADSET, so Qt opened the speaker and the built-in microphone. A patched copy (mobile/android/qt-patches) is compiled into Qt's Qt6AndroidMultimedia.jar when the Android build is configured; plugging a headset in or out reopens receive.
- ADIF import on Android (Kurt DL5EAG): Qt turned the *.adi filter into application/octet-stream, so an ADIF file stored as text (Drive) was greyed out; no filter now.
- Transmit lock (Larry Averbeck): Controls › TX › Lock, off by default. In standby (SB) every way to transmit is refused (PTT, the CW keys and paddles, CW text and memories, TUNE, the ATU, voice macros, the radio's voice keyer, CQ, a tapped decode); a second's hold on PTT, KEY, Space, TUNE or CQ arms it (OA m:ss) for five minutes after the last transmission. Checked on the FLEX without transmitting, on the iPad and a phone both ways.
- FTx Answer CQ (Kurt DL5EAG): the app answers CQs by itself while no QSO or CQ of ours runs: a CQ on the TX slice that Show passes, not worked on this band and mode, the rarest first; five calls a station, then the next (a station called is left alone for 30 minutes); directed CQs only where we are them, contest CQs never. The presence limit (1-60 min, the user: each operator sets it) stops Answer CQ and Keep CQ after that long without a touch on the app. Live on 20 m at 10 W: OM7AMA, EX7CQ and HA7NB worked and logged with nobody touching anything; two stations got five calls and the next was taken; a 1-minute limit switched it off.
- FT8 / FT4: a QSO ended when the transmission under way ended, even if that was still the previous message, so a reply decoded after our slot had begun lost its RR73 or 73; it ends after the closing message itself now.
- Checked against build 139's sweep (no build 140 sweep was kept; build 141's is now the baseline): 27 sizes, no layout decision changed by the code. The differences came from the radio's state: the slice shown was not the TX slice this time, so its TX tag (and 27-37 px of the info panel) was gone and the panel's scale rose a little. The FTx CQ row with Answer CQ checked at 360, 390 and 844 px wide and in the iPad's panel, with More too.

**Build 140** (2026-10-31), iPhone and iPad, and Android beta 54:
- Space PTT on an iPad with a Magic Keyboard (the user: after typing in the pinned Log panel Space no longer keyed, and nothing gave it back). The devlink log showed the Log tab's callsign field holding the keyboard: the app's buttons take no focus, and while a text field has it the GameController keyboard (Space) is off. Now a press anywhere outside a text field lets go of it (TextFocusRelease, an application event filter), and logging a contact does too; the log names the focused field. Checked on the FLEX by taps through the window, and with a new render tool command (focusaudit) over every text field in view (iPad layout with every section pinned: 19 fields, Radio Setup's 24) and 16 popups: each field took and released the keyboard, the page kept it after every popup; the phone's Log drawer the same.
- RTTY decoding (Ben van Schoor asked after build 138): the radio's RTTY mode decoded on the device with the desktop's RttyDecoder on the slice's receive audio, the text in the CW strip along the top of the waterfall, a tap on it for the RTTY panel (decode on / off, shift Auto / 170 / 425 / 850, 45.45 / 50 / 75 baud, reverse, mark Auto / 2125 / 1275, lock and SNR, the text, Clear). Checked on the FLEX with a synthetic RTTY signal through the app (render tool rttyfeed: decoded in full, locked, SNR 4 dB); no RTTY on the air at the time (TC1TR spotted on 7.080 not heard). The panel's first versions hung the app in a polish loop (a ScrollView sized from its text, rows wider than the panel); it is built as the notch panel now, the strip takes its tap (it tuned the display too).
- Mac (Pentti Pärnänen OH6PP, MacBook Air, build 139): the text box's OK pressed Return, which in the Log tab logged the half-typed call, and the box opened on a click on the field's text but not beside it. Keys reach the fields directly now; the box opens only on press and hold, and its OK only fills the field. Not testable here.
- Sebastian Schu asked for a port with the address (tcp 56034); asked back whether he means the RF2K-S or the radio. Build 139's FB post was left with a GitHub card and the paragraphs flattened; the user posted it.

**Build 139** (2026-10-30), iPhone and iPad, and Android beta 53:
- CW Autotune acts when pressed (the user found it doing nothing in build 138: the button only switched a setting, and my test had tapped the display by script instead of pressing it): the strongest CW signal in the passband to the hertz, a notice with none; a tap in CW always lands on the nearest signal. The decoder's pitch was tried and left out (it reports a pitch from band noise, stuck at ~203 Hz). Checked by the user's own path on the radio, 2D and 3D: 10.000012-13 MHz from 163 Hz and ±90 Hz off, the notice and no tune on an empty stretch.
- The radio reported CW pitch 0 for this app's client (since 2026-10-27; a fresh client is told 600): CW at zero beat silent, the decoder deaf, the CW tab showing 100. FlexBackend now carries a reported 0, and the app sets 600 Hz once a session with a notice (the user's choice). Checked: 0 -> "cw pitch 600" accepted, the next connect reports 600, no notice.
- Render tool: `ready` (waits for the connection), `tapmhz`, `holdmhz`, `tapname` (a real tap on a named control), `span`, `tnfs`, `txchain`, `a11y`; objectNames for the CW Decoder and 2D / 3D buttons. A new working rule after the miss: test the user's own path, name buttons for what they do, report exactly what was tested; and a gate for device control (amplifiers): read-only first, a test plan per command, off by default, tester preview, confirmations.
- Notches by confirmation (testers added them by accident): a hold on the display or + TNF opens the notch panel for a new notch, drawn on the display only, with width, depth, permanent and the 10 Hz steps; Add creates it (tnf create, then the settings once the radio reports the id); Close or a tap outside leaves none (modal while creating, so that tap does not tune). The title to 10 Hz. Checked on the FLEX by the user's path: a hold sent nothing; 200 Hz + Deep + Add gave a 200 Hz depth 2 notch; a tap outside and Close left none and did not tune; + TNF + Add put one at the passband centre; Remove. The first runs found two plan errors (a 3 kHz pan, a tap that hit +10 Hz) and a binding loop on Remove, and left notches on the radio, removed.
- eSSB tab (the user's call: the radio's own audio settings stay in Audio and TX; the device's microphone processing gets its own tab, as the desktop's Aetherial chain): gate, EQ, compressor moved from TX. Eleven tabs in rows of six, the twelfth kept for the amplifier and tuner (the user); the buttons' type shrinks alike where six do not fit (a 698x668 side drawer cut "FreeDV").
- Screen-reader names for every control: 290 on the FLEX, none without (render tool a11y lists them); press actions and slider steps checked through Qt's accessibility interface (a11ypress / a11yinc). Password fields give their placeholder as the description (Qt gives them no name).
- FTx Scroll › List only (Michael Whaley's option B): the main buttons fixed, the settings under More, only the decode list scrolling with its bar; Whole tab as before; lower than 520 px the whole tab scrolls. Testing with live decodes found a hang: judged from the layout, the choice flipped with the page's scroll bar (rows wrap) and Qt looped; it is now judged from the view's height alone. Checked at 390x844, 440x956, 956x440 and the iPad drawer.
- Ben van Schoor asked about FTx and RTTY decoding (RTTY queued).
- The FB post for build 138 went up before the miss was found; the next post covers 138 and 139 together (the user).

**Build 138** (2026-10-30), iPhone and iPad, and Android beta 52:
- CW autotune (the user's idea of 2026-10-27): a tap in CW lands on the CW signal nearest it (SpectrumItem::cwPeakNear: the strongest bin within ~14 px, at least 60 Hz, 8 dB over the bins around it, interpolated) to the hertz; checked on the radio against the 10 MHz carrier (a tap 132 Hz away tuned to 10.000012 MHz). The radio's own autotune (slice auto_tune) answers 50004001 without SmartSDR+, so it is not used. Autotune on the CW tab.
- − / + (FineSlider, liveDrag for values the radio takes on release) on pan (the user), RF / tune power, mic and monitor level, TX filter, the microphone processing, phone levels, CW speed and pitch, DSP levels and Radio Setup's levels.
- Radio Setup › Transmit › Show my transmission in the waterfall: the radio's show_tx_in_waterfall; the app draws its own TX rows only when on (the user: on 20 m they lit the whole span). Checked on the radio with a 4 s TUNE at 10 W each way.
- FTx: a slice's old-band decodes leave the list on a band change (the others stay; checked on the radio); the time line stays dim while a time server is in use (the user's OnePlus clock +496 ms; DT median 0.10 s).
- SmartLink cards "In use by" and the station; the connect page at most 560 wide; the iOS Local Network hint on unreachable home-network addresses (Larry Maleszewski); scroll bars wherever a page scrolls (PageScroll); 4.5:1 tags (the lit button was 4.50, the review's 3.8 mismeasured).
- Checked against build 137 itself with the new render-sweep.py (all sizes in one run on one connection, ~4 min for 35 sizes instead of ~25): no layout decision changed at any size or font; every header and panel pair looked at; the DSP level rows on a phone and an iPad with NR and NB on.

**Build 137** (2026-10-29), iPhone and iPad, and Android beta 51:
- Larry Averbeck's three requests: − / + at the ends of the AGC-T, RF gain, each slice's volume and the squelch sliders (FineSlider: a tap one step, held it keeps stepping, each step its own command; checked on the radio: + + − gave AGC-T 51, a held + 58, a held − 52); Close at the top right of the AGC and volume panels; F/R is now B/S (bands / steps).
- Build A of the usability review: one panel base (PanelContent: the title and Close at the top right stay in place, the rows under them scroll in a low window with a bar) for the AGC, volume, filter, meter, band, notch, network, stations and voice keyer panels (Close was at the bottom in the last five); the phone's header rows upright 24 → 36 px (STEP, antennas, width, AGC) and 33 → 40 px (slice, speaker, lock, REC, PLAY, flags), the icons at least 36 px wide (the user chose about +30 px of header over a full 44); haptics (Haptics.h: UISelectionFeedbackGenerator / UIImpactFeedbackGenerator on iOS, View.performHapticFeedback on Android, following the system setting) on PTT press and release, the step buttons and every − / + step.
- Connect by host name (Sebastian Schu DG5CST): ABC on the address keypad opens the device's keyboard (a hardware keyboard types letters on the keypad); the name is looked up (QHostInfo, the first IPv4 address) and remembered as typed. Checked on the radio with 192-168-0-40.nip.io; an unknown name says so and keeps the saved address.
- The controls drawer full screen upright (▲ beside Close; Michael Whaley), on the phone and on the tablet page's upright drawer (the iPhone Duo's inner screen showed four rows and no FTx decodes).
- A render audit of every panel and controls tab at 27 window sizes, the testers' MacBook windows and external displays among them, fonts 16 and 17.
- The tablet page's info panel fit, after the user circled an empty room beside the frequency on the iPhone Duo's inner screen in pictures that had been passed: the wider icons stay in the one-line rows; the probes the fit is measured from are shown at opacity 0 instead of hidden (a hidden positioner left out parts that appear later, so a probe could stay short and the panel be cut, as 626 x 890 was in one run of three); a third probe checks the fitted scale, stepping down to 0.66 at most. Small phones on their side keep the narrow icons and the DSP flags.
- Checked against build 136 itself (built from origin, run at every size in DIGU and USB): layout-diff.py, a new render tool check that compares the layout decisions size by size, finds no change on the tablet page, iPads, Macs or the small landscape phones, only the intended taller rows and wider icons on the phones; every old | new picture pair looked at.

**Build 136** (2026-10-28), iPhone and iPad, and Android beta 50:
- Android: the connection dropped in the background (beta 49, 22:26, the user's OnePlus Open in the middle of an FT8 QSO; the "GUI thread stalled (app not in front)" open since beta 42). The log showed Inactive, the surface gone, then "called createSurface(), waiting for Surface to be ready..." and the GUI thread stalled 29 s; the radio closed the connection after 15 s without pings. Qt 6.12's Android window, on Suspended, destroys the surface and then sends an unexposing expose and waits for the render thread; a frame that thread was drawing at that moment found the surface marked not created, asked for a new one and waited for it, which only came when the app returned. The app now unexposes its windows synchronously in an event filter on the application state change event, which Qt sends before the signal its window acts on: the render thread finishes its frame (the old surface still marked created, so it fails fast) and stops before the surface goes. Compiled against the desktop Qt 6.12 (the filter fires before the signal); on the device it is still to be seen.
- The tablet page keeps the info panel beside the frequency, as in landscape (the user: upright, the OnePlus inner screen's top was "miten sattuu"): where the row is short of room, the frequency and the panel's type shrink to fit (676 of 700 upright on the OnePlus, 97 %; the iPhone Duo upright 68 %), measured from two hidden probes at 100 % and 70 % so the scale does not feed back into its own measure; under the frequency only below 66 %. The panel's paddings shrink with its type. Rendered at 698x668, 751x616, 626x812, 890x606, 1024x768 (pinned), 600x834, 640x700, 1366x1024 and the phones 390x844, 406x903, 844x390, 533x505.
- FTx: the time line under PSK Reporter always shows (the user: on iOS it never did, the clock being a few ms off): the server and the device clock's offset in ms, amber from 0.3 s; before the first answer, that FT8 / FT4 run on the device's clock.

**Build 135** (2026-10-28), iPhone and iPad, and Android beta 49:
- A big screen keeps the tablet layout in both orientations (the user, OnePlus Open inner screen, devlink screenshots): upright it is 698 dp wide and fell under the phone layout's 700; the iPhone Duo's inner screen (626 x 890 pt) did the same. The limit is now 600 (band buttons may shrink to 34 so 13 bands fit), and the tablet page's portrait test uses the room inside the status and navigation bars, so the near-square OnePlus screen (698 x 668 of 751) is laid out the same both ways. In portrait the controls pin under the display only from 1050 pt tall (every iPad; 683 x 1004 left the display no height); lower, the drawer.
- The tablet page in a narrow window: the info panel goes under the frequency by the frequency row's measured width (an HF frequency at least) instead of a fixed 700: at 751 dp the row kept its width and pushed the page past the right edge (GEN, +1k and the right meter cut off). Under 1000 pt the top row puts Setup, Manual, Log and Disconnect behind ⋯ and the badge drops its words: in the devices' type the full row needs about 900, and the radio's name ran under "Good". The drawer and the pinned panel are at least 320 wide (287 on a 1024 window cut "TX 2170 Hz", "New DXCC" and the voice keyer's F10).
- FTx: Keep CQ between CQ and Halt TX; in the slot row the "CQ slot" label, in the devices' larger default type, left it "Keep …" (the user's report). The CW text, QRZ.com, Wavelog and Logger32 rows put the field over its buttons where its hint does not fit.
- Render tooling: `--font-px` sets the default type size to a device's (Android about 16, iOS 17); offscreen text was 12 px, so shots fitted where the phones did not. Checked with it: every controls tab scrolled through at 698 x 668 (drawer 320), 1024 x 768 (pinned 320) and 390 x 844; the top row, frequency row and drawer at 751 x 616, 698 x 668, 640 x 700, 626 x 812 and 890 x 606 (iPhone Duo), 600 x 834, 683 x 1004, 744 x 1133, 1133 x 744, 820 x 1180, 1180 x 820, 1024 x 1366, 1366 x 1024, and the phones 639 x 700, 466 x 678, 390 x 844, 844 x 390, 533 x 505; every popup by a tap in both OnePlus and both iPhone Duo orientations (frequency keypad, mode, step, both antennas, filter and DSP, AGC, volume, both meters' choosers, RF gain, the network panel, ⋯, and a slice's hold menu). The doc screenshot scripts pause 20 s after each run on the radio: it takes two GUI clients, and back-to-back runs with a phone connected found both taken.

**Build 134** (2026-10-28), iPhone and iPad, and Android beta 48:
- The AGC panel in the wrapped header (Android beta 45, iOS build 132), the user's report right after build 133: AgcChooser took its width from its parent, the antennas / width / AGC piece of the wrapped header, so the modes read "off … … f…" and the AGC-T and RF gain sliders had no room. It now takes up to 420 within the window (the panadapter's RF gain chooser keeps its own). The filter, step, split and antenna choosers size to their content and were not affected. Rendered on the FLEX at 683x735 by a tap on AGC-F, 533x505, 390x844, 844x390 and 1366x1024.
- Every popup checked by a tap after that (the user: no more back-and-forth builds): the frequency keypad, mode, step, RX and TX antenna, filter (3K and the DSP flags), AGC, volume, both meters' choosers, RF gain, the ⋯ menu, the network panel and a slice's hold menu (iPad: also Radio Setup and Log), in eight windows: 683x735 and 507x1024 (wrapped header), 390x844, 406x903, 844x390, 533x505, 1366x1024 and 1024x1366. All open whole and in the window except the network panel: on a phone its title ran over the badge, and on a phone on its side it was taller than the window (Close off screen). The badge now goes under the title where both do not fit, and the panel scrolls within the window.
- Render tooling: shot plan taps and holds go through the window system. A press sent straight to the window had a press position of (0,0), so the panadapter's DragHandler took a tap on the RF gain label (it never opened in a render, and a hold there tuned).

**Build 133** (2026-10-28), iPhone and iPad, and Android beta 47:
- The volume panel in the wrapped header (Android beta 45, iOS build 132): it took its width from the info panel's parent, which the wrapped header made the speaker / lock / record piece a few icons wide; the user's OnePlus Open showed the sliders squeezed and Mute past the edge (devlink screenshot). It now takes up to 360 within the window. Rendered on the FLEX at 683x735 by a tap on the speaker, 533x505, 406x903 and 844x390.

**Build 132** (2026-10-28), iPhone and iPad, and Android beta 46 (for iOS also betas 44 and 45's layout fixes):
- Logger32 (Juho Kontio, iPhone 17 Pro): the Log tab's Logger32 field (host or host:port, 52001 by default) sends each logged QSO as its ADIF record on a TCP connection of its own; unsent records wait in a queue in the settings (500 at most) and go with the next QSO, the next start, a Save, or every two minutes. Save checks with a bare connection. Checked against a listener here (the record arrived; the queue held and went on the next start); the privacy policy and the manual name it.
- The manual's Getting started (a tester could not find the FT8 settings): what each mode needs (once: connect, a slice, the radio's callsign, the grid; transmit: the TX slice, the PC mic input, RF power; voice, CW, FT8 / FT4, RADE, logging), with where each is set. The Android notes' earlier-betas list lost a doubled phrase.

**Android beta 45** (2026-10-27; no iOS build):
- Beta 44 on the user's split window (about 533 dp wide): the portrait layout fitted, but its phone header (made for 390-440 dp) left half of both rows empty. From 480 dp wide in portrait the frequency, mode, STEP and the slice's groups flow along the lines and wrap only where the room ends, on the phone flag row's height with no gap between lines (the user found the first try too loose). Rendered on the FLEX at 533x505, 533x574, 600x700 and 440x956 (unchanged).

**Android beta 44** (2026-10-27; no iOS build):
- A near-square phone window (the user's split window on the OnePlus Open's inner screen, about 533 x 510 dp after the system bars) took the phone page's landscape layout, whose one-line header beta 43's record and play icons made too long: the page grew past the right edge, PTT with it (devlink screenshot). Landscape now needs width over 1.3 x height; the landscape header clips at the right instead of widening the page; a short portrait window (under 620 dp) lets the display go down to 100 dp. LogBuffer drops Qt's "Skipping create egl on invalid or not yet created surface" (50 a second since qt.qpa.window debug went on in beta 42). Rendered on the FLEX at 533x505, 533x560, 600x430, 844x390, 956x440 and 390x844.

**Build 129** (2026-10-27), iPhone and iPad, and Android beta 43 (for iOS the first build since 125: runs 126 and 127 were Android-only and cancelled, 128 a CI-only push that built nothing):
- Record and playback (the user): the radio's quick record per slice (SmartSDR's REC / PLAY; SliceModel::setRecordOn / setPlayOn) as a dot and a triangle at the end of the slice row; recording stops playback and the other way round; held PTT during playback transmits it. Checked on the FLEX (slice C, 14.118 USB, 2 W on 20 m for the test): record and play as the radio reported them; PTT 3 s during playback peaked at 0.2 W (a recording of band noise), the same PTT without playback 0.0 W; the radio put back after (100 W, 14.074 DIGU). PTT released now logs the over's peak forward power.
- Larry Averbeck's wishes (FB, build 122 and beta 42): F/R with − / + / Band hides the band row and the step buttons (remembered; View › Bands and steps), Band then opens the band panel, which has Close; the step buttons on the radio's STEP (View › STEP ×1 ×10 ×100: ±STEP, ±10 STEP, ±100 STEP); an eye in the SmartLink, QRZ.com and Wavelog / Cloudlog key fields shows what was typed (PasswordField). Rendered on the FLEX on phone portrait and landscape and iPad portrait and landscape.

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

- **TGXL SO2R and the Antenna Genius** (2026-11-04, the user: after build 153, with Ben Morris's setup: an SO2R TGXL on an Antenna Genius SO2R): the active channel (status active=1|2) shown and switched (activate ch=1|2), the A / B readings per channel, and the Antenna Genius (4O3A's AG TCP/IP API, github.com/4o3a/genius-api-docs, port 9007) read, then controlled. Built from Ben's app log (Log › Copy after Controls › Amp), not from the documents alone. With it the audit's open points: antA's base, the PGXL setup write and its authcode from a stale read, the PGXL sequence past 255, the meters and interlock a real PGXL creates on the radio (genius-mock to create them too).
- **Tailnet shim setup** (noted 2026-11-04, the user: it belongs to the desktop; installing it from a mobile device is not on the roadmap now, kept for the record): upstream AetherSDR's flex-tailnet-shim is a Docker waveform inside a FLEX-8000 / Aurora that puts the radio on the owner's tailnet (a single-use Tailscale auth key, an allow list, sharing the station's 4O3A devices), set up once over the radio's LAN from the desktop's Waveforms › Remote Access. OksaSDR already connects to a radio on a tailnet by its MagicDNS name or 100.x address (the radio list, build 151); what the app would add is only the install and provisioning (download and upload of the 8 MB image, the provisioning API on port 48992, the admin token in the Keychain).

From the comparison with upstream main and tester requests:
- Memory groups (repeater offsets and tones are in build 86)
- ATU tools and per-band TX settings
- Profile import and export
- QSO recording
- 3D with 768 columns
- Upstream FFT Smooth and De-comb
- Optional VOX mode (microphone always open)
- More of the desktop's TX chain (de-esser, tube, exciter, reverb) and a
  chain order, on the eSSB tab (build 139 gave the chain its tab)
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
- CW autotune: done in build 138.

Tester requests (2026-10-29, Michael Whaley AF0F, after trying the
full-screen drawer of build 137):
- FTx: the main buttons fixed at the top and only the decode list scrolling
  under them (his option B), as a choice beside today's one-piece scroll,
  with a scroll bar that shows it is a scroll (nested scrolls confuse
  otherwise). Some want the spectrum, some the decodes, some both: the
  choice is the point. He offered to pilot it. *Done in build 139:*
  Scroll › List only, the settings under More.
- Fox / Hound and SuperFox, to work a DXpedition from the phone (his use:
  grabbing a rare one at work). A bigger piece: Hound mode's rules (calling
  above 1000 Hz, the Fox's replies) and the SuperFox decoder. *To discuss.*

Amplifiers and the tuner (planned 2026-10-29, its own build; asked by
Sebastian Schu DG5CST for the RF2K-S, Bill Arcand for the Tuner Genius and
Dennis Daniel for the FlexRadio amplifier and tuner):
- **Step 1, the RF2K-S read only (build 148 / beta 61):** RfkitProtocol /
  RfkitConnection taken from upstream (#6596, tested there on an RF2K-S,
  firmware G200C267) with their tests, plus a read-only mode (only GET
  requests reach the wire; the control calls are refused, tested on the
  loopback stand-in) and a poll period (500 ms on the phone, 3 requests a
  tick). Radio Setup › **Amp**: the address (host or host:port, 8080 by
  default), Read the amplifier on / off, Forget. Controls › **Amp**, the
  13th button, only once an amplifier is set up (rows of seven then): name,
  OPERATE / STANDBY shown, forward / reflected / SWR bars on the
  amplifier's scales, temperature, voltage, current, band, frequency,
  antenna, tuner, interface, status, firmware, the amplifier's error.
  Read while the app is on screen and connected to a radio. Tested with
  mobile/tools/rf2k-mock.py (the API's documents, keying cycles; counts
  any PUT / POST): readings live, refused, not an RF-KIT, timed out, link
  lost, off and back, background and back, forgotten; 0 writes in every
  run. Sebastian checked it on his RF2K-S (2026-11-03, port 8081, 615 W
  at 8 W drive, SWR 1.06): "The readings are all fine!"
- **Step 2, control (build 149 / beta 62):** Radio Setup › Amp › **Control
  the amplifier** (off by default; Forget turns it off; with it off the
  connection stays read-only). The Amp tab: **Mode** Standby / Operate,
  **Antenna** 1-4 (the ones the amplifier lists as set up; EXT shown, not
  switched, as upstream), **Reset error**. Each a confirmation naming the
  change (the action button names it too), closed by itself when the
  radio keys, the link drops or Control goes off; refused while the radio
  transmits (no relay switching under RF); one at a time (Asking the
  amplifier…); never latched, the tab shows the readback; a refusal shows
  the amplifier's reason, a lost answer says the state is unknown until
  read. Forward gets the desktop's 3-second peak marker; the poll is
  250 ms while transmitting. The meters offer the amplifier's six
  readings. Next: Sebastian's check of the controls; then the 4O3A Power
  Genius XL / Tuner Genius XL (Dennis Daniel) when someone can test.
- Controls gets an **Amp** tab (the twelfth button went to Digi in build
  140; the Amp tab is the 13th and shows only when set up): the operating
  side, the state, Operate / Standby, Tune, the meters. Radio Setup's tab
  holds the setup (addresses, turning them on).
- Radio Setup gets an **Amplifier & tuner** tab:
  - **4O3A Power Genius XL**, found through the radio: model, state,
    Operate / Standby (relayed by the radio, so over SmartLink too).
  - **4O3A Tuner Genius XL**, found through the radio: Operate / Bypass and
    Tune (the radio's `tgxl autotune`). Later, with the TGXL's own
    connection and its code: a surer autotune, the relays and the 1x3's
    antenna.
  - **RF-KIT RF2K-S**, the amplifier's own REST API (upstream #6596,
    26.10.4; RfkitProtocol / RfkitConnection taken as they are): address
    (or host name) and port, free to choose (Sebastian's is 8081: his
    router uses 8080), the link's state, Operate / Standby, internal
    antenna 1-4, error reset (asks first); band, frequency, tuner and
    interface shown. Not over SmartLink: the phone must reach the
    amplifier's address (same network or VPN).
- The meters can show them, offered while one is there: amplifier power,
  SWR, temperature and current (RF2K-S voltage, PGXL drive); the tuner's
  power and SWR. The engine has the PGXL / TGXL meters already
  (MeterModel ampMetersChanged, ampVitalsChanged, tgxlMetersChanged); take
  upstream's #6658 with it (SmartSDR 4.3 amp / tuner meter handles
  unsigned).
- Testing: no hardware here. The RF2K-S against a local mock of its API
  first, then Sebastian on his amplifier (he offered, 2026-10-29). PGXL and
  TGXL need a tester with one; not released untested on a real device (Mike
  VA3MW agreed: "He needs the equipment to test and integrate").
- The gate for anything that changes a device's state (the user,
  2026-10-30: the smoke must stay in the amplifier):
  1. Read-only first: power, SWR, temperature, state. No command that
     changes the device goes out in the first step.
  2. A written test plan for every control command (what it does, the
     device's reply, the error handling), and mock tests with the failures
     too (link lost, a wrong reply, a slow device).
  3. Off by default, and first only in a preview build for the tester with
     the device; to everyone only after the tester's check. (The RF2K-S
     control went to every tester at once, off by default, by the user's
     choice on 2026-11-03 after Sebastian's read-only check.)
  4. Every state change (Operate, antenna, error reset, tune) behind a
     confirmation, never one stray tap.

## Waiting

- **Upstream (aethersdr/AetherSDR):** no longer pursued (2026-10-22); OksaSDR
  goes its own way and takes upstream changes only when useful.
- **To verify:** LAN discovery on Android at the radio's own network (on the
  next visit to the cabin, where the radio is; SmartSDR 4.3.16 works: a
  tester's FLEX-6600 runs it).
- **Verified:** the TX start without a delay since build 68. The whoosh at the start came back with receive on a phone's speaker (Jeff Stinger, the user on Android beta 54): fixed in build 141, to verify on a device.

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
