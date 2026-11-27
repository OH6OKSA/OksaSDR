# OksaSDR: privacy policy

OksaSDR is a FlexRadio client for iPad, iPhone, Mac and Android, built on the
[AetherSDR](https://github.com/aethersdr/AetherSDR) engine. It is
maintained by Niko Oksa (OH6OKSA). This policy covers the app distributed
through the App Store, TestFlight and as the Android beta from
[this source](https://github.com/OH6OKSA/OksaSDR/tree/mobile/oksasdr).

## Summary

The app collects no personal data. Nothing you do in the app is sent to the
developer. It connects to a third-party service only when you use a feature
that needs it (SmartLink, QRZ.com, a DX cluster, POTA, WWFF, PSK Reporter, your
own Wavelog or Cloudlog logbook, Logger32 on your PC, your amplifier, a time
server for FT8 / FT4, and on Android GitHub for new versions), as listed
below.

## Network use

The app talks to your radio and, only for the features you turn on, to
these services:

- **Your radio.** It connects to the IP address or host name you enter (a
  name is looked up through your device's own DNS), or to a radio
  discovered on your local network. Control commands, spectrum, waterfall and
  audio travel directly between your device and your radio. They go through a
  VPN only if you route them that way. The station name (the app's name, or
  the one you set in Radio Setup) goes to the radio, which shows it to its
  other stations; the radio's GPS position, if it has GPS, is only shown in
  Radio Setup.
- **Local network discovery.** It listens for radio and accessory discovery
  broadcasts on the local network. To find a Hermes-Lite 2 or an ANAN radio,
  which do not announce themselves, it also sends a small discovery probe (a
  broadcast to UDP port 1024) on the local network every five seconds while
  the app runs; the probe carries nothing about you. iOS asks for permission
  ("Local Network") before this is allowed.
- **SmartLink** (FlexRadio), if you sign in on the Connect page: your
  FlexRadio account sign-in and the connection to your radio go through
  FlexRadio's SmartLink service, under FlexRadio's terms. With several
  accounts signed in, only the active one talks to SmartLink at a time.
  *Forgot password?* on the sign-in form sends the e-mail address you
  typed to the same sign-in service, which mails its reset link to that
  address; the app handles no password in it and keeps nothing of it.
- **QRZ.com**, if you save a QRZ.com account in Radio Setup › Logging: the callsign
  you type is looked up there with your account.
- **A DX cluster**, if you turn it on: the app connects to the cluster
  server you choose, signs in with your callsign and receives its spots.
  When you tap **Spot** on the Log tab and confirm, the spot (the call,
  the frequency and the remark shown to you) goes to that cluster under
  your callsign, and the cluster publishes it to its users.
- **Speech recognition**, only while you use the voice button (**🎤**) on
  the Log tab or in Big buttons: the few seconds of you spelling a call are
  recorded by the app and recognised on the device by the app's own
  offline recogniser (Vosk), on an iPhone, iPad, Mac and Android alike, so
  nothing leaves the device. (Up to build 174 an iPhone, iPad or Mac used
  Apple's Speech framework, which may send the recording to Apple's speech
  service.) The app keeps neither the recording nor what was heard, beyond
  the call it puts into the field.
- **POTA (pota.app)**, only when you tap **Self-spot** on the Log tab and
  confirm: a spot of your own activation (your callsign, the park
  reference, the frequency, the mode and the comment shown to you) is sent
  to api.pota.app, which publishes it on pota.app. Nothing is sent
  otherwise.
- **POTA and WWFF park spots (pota.app, wwff.co)**, only if you turn on
  *POTA* or *WWFF* under Park spots on the View tab (off by default): the
  app reads their public lists of activators once a minute while a radio
  is connected. Only the request goes out (the app's name and version);
  no callsign or other data of yours is sent.
- **PSK Reporter**, only if you turn on *Report to PSK Reporter* on the
  FTx tab (off by default): the callsigns, locators, frequencies, SNR and
  times of the FT8 / FT4 stations this device decodes are sent to
  report.pskreporter.info with your callsign, your grid and the app's name
  and version, and are published there, as WSJT-X does.
- **A time server**, while FT8 / FT4 decoding runs: when it starts and
  every 30 minutes, the app asks the time (a standard NTP request, nothing
  about you is sent) from time.cloudflare.com, or when that does not answer
  from pool.ntp.org or time.google.com, and runs FT8 / FT4 on it, as an app
  cannot set the device's clock.
- **FreeDV Reporter** (qso.freedv.org): while the FreeDV tab is open the
  app fetches the list of stations on it (nothing about you is sent). Only
  if you turn on *Report to FreeDV Reporter* (off by default), while RADE
  is on, your callsign, your grid, the app's name and version, the
  frequency, when you transmit, the message you type there and the
  callsigns and SNR of the stations RADE hears are sent to qso.freedv.org
  and shown publicly there, as FreeDV does.
- **Wavelog or Cloudlog**, only if you enter your logbook's address and API
  key in Radio Setup › Logging: each QSO you log is sent to that logbook (your own
  server, or the one you use) with its ADIF fields. QSOs that could not be
  sent wait on the device until they can. With *Worked before from the
  logbook* on, the app also reads your QSOs back from that Wavelog
  logbook (its station locations and each QSO's call, band, mode, date,
  time and CQ zone) and keeps them on the device for worked-before;
  turning it off deletes them from the device.
- **QRZ.com Logbook**, only if you enter your logbook's API key in Radio
  Setup › Logging: each QSO you log is sent to your QRZ.com logbook (logbook.qrz.com)
  with its ADIF fields, and with *Worked before from the logbook* on, the
  app reads your logbook's QSOs back from it (each QSO's call, band, mode,
  date, time and CQ zone are kept on the device for worked-before).
  *Remove* deletes the key and those QSOs from the device.
- **A shared ADIF file**, only if you choose one in Radio Setup › Logging: the app
  reads the file you picked (in iCloud Drive, Google Drive, OneDrive or
  another storage you chose; it goes wherever that storage sends it) and
  adds each QSO you log to its end. The device keeps the system's
  permission to reach that one file and a copy of each QSO's call, band,
  mode, date, time and CQ zone from it; *Remove* deletes both.
- **Logger32**, only if you enter your PC's address in Radio Setup › Logging: each QSO
  you log is sent with its ADIF fields to Logger32 on that PC (your own
  computer, on your network or through your VPN). QSOs that could not be
  sent wait on the device until they can.
- **A logging program on your network** (WSJT-X UDP), only if you enter its
  computer's address in Radio Setup › Logging: each QSO you log is sent
  over UDP, as WSJT-X sends one, to that computer (your own, on your network
  or through your VPN), for a logger such as RUMlogNG or Log4OM.
- **An amplifier** (RF-KIT RF2K-S), only if you enter its address in Radio
  Setup › Amp: while the app is on screen and connected to a radio it reads
  the amplifier's state and meters from that address (your own device, on
  your network or through your VPN), and for up to 20 s in the background
  during a Tune you started. With Control the amplifier on, it also sends
  the changes you confirm there (Operate / Standby, the antenna, an error
  reset, and Standby before a Tune); nothing else is sent to it.
- **An Elecraft KPA1500**, only if you enter its address in Radio Setup ›
  Amp: while the app is on screen and connected to a radio it asks the
  amplifier for its readings and state over the amplifier's UDP server at
  that address (your own device, on your network or through your VPN).
  With Control the amplifier on, it also sends the changes you confirm
  there (Operate / Standby, the antenna, the ATU, its tune and the fault
  clear) over the amplifier's TCP connection; nothing else is sent to it.
- **A 4O3A Power Genius XL or Tuner Genius XL** connected to your radio:
  while the app is connected to the radio it reads the device's status
  directly at the address the radio reports (or one you set in Radio Setup
  › Amp), on your own network or through your VPN. With Control on, it
  sends the changes you confirm on the Amp tab. A remote code the device
  asks for is kept only in this device's secure store (iOS Keychain,
  Android Keystore, the desktop's keyring on Linux) once the device has
  accepted it.
- **GitHub, for new versions** (the Android app from GitHub only), unless
  turned off in Controls › View › New versions: at start and about once a
  day the app reads the latest release from GitHub's public API, to offer a
  newer beta. GitHub sees your device's address and the name "OksaSDR",
  nothing else. Download opens the release in your browser. The iPhone,
  iPad and Mac app does not do this (TestFlight and the App Store update
  it), nor would a Google Play build.
- **Developer link** (hidden on the Log page, under a press and hold on its
  title), off unless turned on: for the developer's
  own devices; it sends the app's log, and screenshots when asked, to the
  address set there, and runs the test plans sent from there (taps,
  drags and readings of the screen; the app blocks transmitting while one
  runs, and keeps none of its screenshots).

The app contains no analytics, advertising, tracking or crash-reporting SDKs,
and it makes no other internet connections. FT8 / FT4 decoding and RADE run
on the device.

## Microphone

The microphone is used only to transmit through your radio (voice, RADE
digital voice) and to record voice keyer messages you choose to record. It
is open the whole time the app is connected to a radio, with this device's
microphone as the radio's input, and in front, once you have allowed it (on
an iPhone, an iPad and a Mac since build 221, on Android and Linux since
Android beta 123): the audio streams to your radio, which transmits only at
PTT or by its VOX, so that the microphone's level and audio chain can be
set without transmitting. iOS and Android show their microphone indicators
for as long. In the background the microphone is closed unless the radio's
VOX is on, and on Android it is not held during a phone call. With a
Bluetooth headset as the app's hands-free set (Audio output *Auto*) it is
the headset's microphone, else the device's own. Before those builds the
microphone was open only while you held PTT or recorded, while the TX or
eSSB tab was shown, with VOX on, and on Android with a Bluetooth headset.
Transmitting from the device requires the radio's mic input to be set to
this device ("Use this device's microphone"). The audio goes only to your
radio and is not recorded or stored, except the voice keyer messages you
record yourself, which stay on the device. iOS and Android ask for
microphone permission first.

## Bluetooth

On an iPhone or iPad the app uses Bluetooth only when you pair a Bluetooth
MIDI controller (Radio Setup › MIDI › Pair a Bluetooth MIDI controller…),
through iOS's own pairing sheet; iOS asks for Bluetooth permission first.
On Android the same button makes the app search for Bluetooth MIDI
controllers for 15 seconds (Android has no pairing screen of its own for
them): Android asks for the *Nearby devices* permission first (on Android
11 and older *Location*, which Android requires for any Bluetooth search;
the app reads no location). The search looks only for devices offering
the Bluetooth MIDI service, and the app keeps the name and Bluetooth
address of a controller you use, so it can open it again by itself. The
controller's knob and button messages are read on the device and never
leave it. The controller's functions and its name are kept in the app's own
storage on your device.

## Camera

The app never uses the camera. iOS requires a camera notice because the audio
library the app includes also contains camera functions.

## Data stored on your device

The app keeps its settings in its own storage on your device. These are the
last radio address you entered, mic gain and similar operating preferences,
your own screen layouts (where each part of the screen goes), and an
identifier the radio uses to recognise this client. They never leave
your device, except as part of the normal connection to your radio.

- **Your QSO log** (the Log tab and FT8 / FT4 contacts) is an ADIF file on
  the device: in the app's folder in Files on iPhone and iPad, in
  Documents › OksaSDR on Android and on a computer. It leaves the device
  only when you share or move it yourself.
- **Sign-ins**: the SmartLink sign-in (one per account signed in), the
  QRZ.com password and the Wavelog / Cloudlog API key are kept in
  the device's secure store (Keychain on Apple devices, the desktop's
  keyring on Linux); a SmartLink
  password is never stored. The SmartLink accounts' e-mail addresses and
  names (as SmartLink gives them) and your favourite SmartLink radios are
  kept in the app's own storage, for the Connect page. So is the radio you
  connected to last (the way it was reached, its serial number or address
  and its name), for *At start › Last radio*; it stays on this device and
  an App setup does not carry it.
- The in-app diagnostic log (what the app did: connections, commands sent
  to the radio, errors) is also written to a file in the app's own
  storage as it runs, so that after a crash the next start can show the
  run that crashed (Log says the previous run ended in a crash); each
  start replaces the run before the last. It stays on the device and is
  shared only if you copy or share it yourself, or tap Log › Send to the
  developer: that opens a mail in your own mail app, addressed to
  oh6oksa@gmail.com with the log (this run's and the previous run's)
  attached as a text file, and nothing
  is sent until you send that mail. The attached file stays in the app's
  own storage until the next Send to the developer replaces it.
- **App setups** (Prof tab): a setup you save is a copy of the app's
  settings in the app's own storage; the passwords and keys it holds
  (QRZ.com, the logbooks' API keys, the 4O3A devices' codes) stay in the
  device's secure store under the setup's name. Not the SmartLink sign-in
  and not the voice keyer's recordings. **Export** writes the setup to a
  file and opens the share sheet: that file holds those passwords and keys
  as plain text, the app says so before it is made, and it goes only where
  you send it. **Import** reads a setup file you choose. Delete removes a
  setup and its keys.

Deleting the app deletes all of this data (on Android the copy of the log
in Documents › OksaSDR stays until you delete it).

## TestFlight

Apple's TestFlight service may collect usage information, crash reports and
any feedback you choose to send, as described in Apple's TestFlight terms and
privacy policy. If you send feedback, the developer receives it through App
Store Connect. That information is used only to fix problems in the app.

## Children

The app is a tool for licensed radio amateurs and is not directed at
children.

## Contact

Questions about this policy: oh6oksa@gmail.com

Last updated: 2026-11-26 (the microphone open the whole time the app is connected and in front, on every device; Android: the microphone held open for a Bluetooth headset while the app is in front; earlier: Forgot password? on the SmartLink sign-in; the radio used last, kept for At start › Last radio; Android: GitHub for new versions; the microphone with VOX and for the TX tab's meter, speech recognition on the device everywhere, SmartLink accounts and favourites, Bluetooth MIDI search on Android; the Elecraft KPA1500, Bluetooth MIDI controllers, App setups)
