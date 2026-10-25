# OksaSDR: privacy policy

OksaSDR is a FlexRadio client for iPad, iPhone, Mac and Android, built on the
[AetherSDR](https://github.com/aethersdr/AetherSDR) engine. It is
maintained by Niko Oksa (OH6OKSA). This policy covers the app distributed
through TestFlight and as the Android beta from
[this source](https://github.com/OH6OKSA/OksaSDR/tree/mobile/oksasdr).

## Summary

The app collects no personal data. Nothing you do in the app is sent to the
developer. It connects to a third-party service only when you use a feature
that needs it (SmartLink, QRZ.com, a DX cluster, PSK Reporter, your own
Wavelog or Cloudlog logbook), as listed below.

## Network use

The app talks to your radio and, only for the features you turn on, to
these services:

- **Your radio.** It connects to the IP address you enter, or to a radio
  discovered on your local network. Control commands, spectrum, waterfall and
  audio travel directly between your device and your radio. They go through a
  VPN only if you route them that way.
- **Local network discovery.** It listens for radio and accessory discovery
  broadcasts on the local network. iOS asks for permission ("Local Network")
  before this is allowed.
- **SmartLink** (FlexRadio), if you sign in on the Connect page: your
  FlexRadio account sign-in and the connection to your radio go through
  FlexRadio's SmartLink service, under FlexRadio's terms.
- **QRZ.com**, if you save a QRZ.com account on the Log tab: the callsign
  you type is looked up there with your account.
- **A DX cluster**, if you turn it on: the app connects to the cluster
  server you choose, signs in with your callsign and receives its spots.
- **PSK Reporter**, only if you turn on *Report to PSK Reporter* on the
  FTx tab (off by default): the callsigns, locators, frequencies, SNR and
  times of the FT8 / FT4 stations this device decodes are sent to
  report.pskreporter.info with your callsign, your grid and the app's name
  and version, and are published there, as WSJT-X does.
- **FreeDV Reporter** (qso.freedv.org): while the FreeDV tab is open the
  app fetches the list of stations on it (nothing about you is sent). Only
  if you turn on *Report to FreeDV Reporter* (off by default), while RADE
  is on, your callsign, your grid, the app's name and version, the
  frequency, when you transmit, the message you type there and the
  callsigns and SNR of the stations RADE hears are sent to qso.freedv.org
  and shown publicly there, as FreeDV does.
- **Wavelog or Cloudlog**, only if you enter your logbook's address and API
  key on the Log tab: each QSO you log is sent to that logbook (your own
  server, or the one you use) with its ADIF fields. QSOs that could not be
  sent wait on the device until they can.
- **Developer link** (Log tab), off unless turned on: for the developer's
  own devices; it sends the app's log to the address set there.

The app contains no analytics, advertising, tracking or crash-reporting SDKs,
and it makes no other internet connections. FT8 / FT4 decoding and RADE run
on the device.

## Microphone

The microphone is used only to transmit through your radio (voice, RADE
digital voice) and to record voice keyer messages you choose to record, and
it is open only while you hold PTT or record. Transmitting from the device requires the radio's mic
input to be set to this device ("Use this device's microphone"). The audio
goes only to your radio and is not recorded or stored, except the voice
keyer messages you record yourself, which stay on the device. iOS and
Android ask for microphone permission first.

## Camera

The app never uses the camera. iOS requires a camera notice because the audio
library the app includes also contains camera functions.

## Data stored on your device

The app keeps its settings in its own storage on your device. These are the
last radio address you entered, mic gain and similar operating preferences,
and an identifier the radio uses to recognise this client. They never leave
your device, except as part of the normal connection to your radio.

- **Your QSO log** (the Log tab and FT8 / FT4 contacts) is an ADIF file on
  the device: in the app's folder in Files on iPhone and iPad, in
  Documents › OksaSDR on Android and on a computer. It leaves the device
  only when you share or move it yourself.
- **Sign-ins**: the SmartLink sign-in, the QRZ.com password and the
  Wavelog / Cloudlog API key are kept in
  the device's secure store (Keychain on Apple devices); a SmartLink
  password is never stored.
- The in-app diagnostic log is kept in memory only. It is shared only if
  you copy or share it yourself.

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

Last updated: 2026-10-24 (PSK Reporter, Wavelog / Cloudlog)
