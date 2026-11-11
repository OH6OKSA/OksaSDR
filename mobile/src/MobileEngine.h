#pragma once

#include "models/BandPlanManager.h"
#include <QJsonArray>
#include "QsoLog.h"
#include "RadioListModel.h"

#include "core/RfkitProtocol.h"
#include "core/TxCoordinator.h"
#include "core/backends/flex/SmartLinkClient.h"
#include "core/backends/flex/WanConnection.h"
#include "core/backends/RadioCapabilities.h"
#include "models/RadioSession.h"
#include "models/TxController.h"

#include <QHash>

#include <functional>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <QTimer>

#include <algorithm>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <memory>

class QJSEngine;
class QQmlEngine;
class QThread;
class Ft8Engine;

namespace AetherSDR {
class AudioEngine;
class RfkitConnection;
class AntennaGeniusModel;
class DxClusterClient;
class FreeDvClient;
class CwRxModel;
class RttyDecoder;
class PskDecoder;
class CwxLocalKeyer;
class IambicKeyer;
class DecoderAudioModel;
class RADEEngine;
class PanadapterModel;
class RadioDiscoverySource;
class SliceModel;
class AgcTCalibrator;
class MidiControlManager;
class CtyDatParser;
struct CallsignInfo;
struct PcmFrame;
struct RadioInfo;
struct SpectrumDecodeScale;
}

namespace AetherSDR::mobile {

class NtpClock;

class QsoLog;

// Composition root of the touch client: owns the radio session, discovery and
// the audio engine, and exposes the active slice to QML. Lives on the main
// thread, like RadioModel and its sub-models.
class MobileEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(AetherSDR::mobile::RadioListModel* radios READ radios CONSTANT)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Band plan strip at the bottom of the spectrum, as the desktop's:
    // which plan ("" = off) and the plans bundled with the app.
    Q_PROPERTY(QString bandPlan READ bandPlan WRITE setBandPlan NOTIFY bandPlanChanged)
    // Button colours (remembered): "lit" (dark, blue when on; the default
    // from build 78), "classic" (light, dark when on, as before) or "light"
    // (dark, light when on).
    Q_PROPERTY(QString buttonStyle READ buttonStyle WRITE setButtonStyle NOTIFY buttonStyleChanged)
    // Text size: "system" follows the system's text size (iOS Dynamic Type,
    // Android's font scale), or "100" / "115" / "130" percent. textScale is
    // the factor every text size in the theme is multiplied by, held to
    // 0.88-1.35 so the layouts still fit (the largest accessibility sizes
    // stop at 135 %).
    Q_PROPERTY(QString textSize READ textSize WRITE setTextSize NOTIFY textScaleChanged)
    Q_PROPERTY(double textScale READ textScale NOTIFY textScaleChanged)
    Q_PROPERTY(double systemTextScale READ systemTextScaleValue NOTIFY textScaleChanged)
    // PTT as a switch (tap on, tap off) instead of hold-to-talk; remembered.
    // A latched transmission stops by itself after kPttToggleLimitS.
    Q_PROPERTY(bool pttToggle READ pttToggle WRITE setPttToggle NOTIFY pttToggleChanged)
    Q_PROPERTY(bool pttLatched READ pttLatched NOTIFY pttToggleChanged)
    // Transmit lock (Larry Averbeck: a mistaken touch on PTT or the CW
    // paddles keyed the radio). On, nothing transmits in standby (SB);
    // holding PTT, KEY, TUNE or CQ for a second arms it (OA) for five
    // minutes after the last transmission. Off by default; remembered.
    Q_PROPERTY(bool txLock READ txLock WRITE setTxLock NOTIFY txLockChanged)
    Q_PROPERTY(bool txLocked READ txLocked NOTIFY txLockChanged)
    // With the lock off: in standby (SB) once after each start of the app,
    // until a second's hold arms it for the rest of the run (Larry Averbeck:
    // the operator aware of transmitting from the first press).
    Q_PROPERTY(bool txArmAtStart READ txArmAtStart WRITE setTxArmAtStart NOTIFY txLockChanged)
    Q_PROPERTY(int txArmedSeconds READ txArmedSeconds NOTIFY txLockChanged)
    Q_PROPERTY(QStringList bandPlans READ bandPlans CONSTANT)
    // Band plan lock (Tyler K0OTC, the user): tuning into a CW or phone part
    // of the band plan below 30 MHz sets CW, or LSB / USB by the band (in an
    // all-mode part too), and a mode the part does not use is refused with
    // a notice. Off by default; PLAN by the mode shows it. bandPlanModes:
    // the modes the part at the slice allows (empty: any, or the lock off).
    Q_PROPERTY(bool bandPlanLock READ bandPlanLock WRITE setBandPlanLock NOTIFY bandPlanChanged)
    Q_PROPERTY(QStringList bandPlanModes READ bandPlanModes NOTIFY bandPlanRegionChanged)
    // Receive audio coding: "Auto" (Opus when remote: SmartLink, a tailnet,
    // or the radio reached through a VPN), "Opus" or "None"; applies from
    // the next connection. audioCodingInUse says what this one uses and why.
    Q_PROPERTY(QString audioCompression READ audioCompression WRITE setAudioCompression NOTIFY audioCompressionChanged)
    Q_PROPERTY(QString audioCodingInUse READ audioCodingInUse NOTIFY audioCompressionChanged)
    // Receive audio buffer: "Low" (100 ms, no prebuffer), "Normal" (200 ms,
    // 60 ms prebuffer after a gap) or "Smooth" (400 ms, 150 ms): steadier
    // audio over a jittery link for a little more delay.
    Q_PROPERTY(QString audioBuffer READ audioBuffer WRITE setAudioBuffer NOTIFY audioCompressionChanged)
    // Where receive audio plays: "Auto" (a Bluetooth headset when one is
    // connected, else the speaker), "Speaker" (even with Bluetooth connected)
    // or "Bluetooth" (a Bluetooth device's media link, the device's own
    // microphone). Remembered; applies at once.
    Q_PROPERTY(QString audioOutput READ audioOutput WRITE setAudioOutput NOTIFY audioCompressionChanged)
    // Network quality as the desktop rates it (Excellent ... Poor, "Off"
    // when not connected) and the round trip in ms, for the header.
    Q_PROPERTY(QString networkQuality READ networkQuality NOTIFY networkChanged)
    Q_PROPERTY(int networkRttMs READ networkRttMs NOTIFY networkChanged)
    // The app's CPU as the desktop's status bar shows it (share of all
    // cores, every 2 s), and frames drawn per second (no public GPU load on
    // iOS: a falling frame rate is what shows the graphics not keeping up).
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY performanceChanged)
    Q_PROPERTY(double cpuPercentOfCore READ cpuPercentOfCore NOTIFY performanceChanged)
    // The app's memory in MB, every 2 s: iOS/macOS physical footprint,
    // Android/Linux resident set; 0 where unknown.
    Q_PROPERTY(double memoryMB READ memoryMB NOTIFY performanceChanged)
    // The app's busiest threads in the last 2 s, "name 48 %" each (% of one
    // core), busiest first; empty where the system does not tell (iOS).
    Q_PROPERTY(QStringList busyThreads READ busyThreads NOTIFY performanceChanged)
    Q_PROPERTY(int cpuCores READ cpuCores CONSTANT)
    Q_PROPERTY(int uiFps READ uiFps NOTIFY performanceChanged)
    // "26.10.1 (54)": app version and TestFlight build, for the header.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    // The online manual (docs/mobile/MANUAL.md on the fork's GitHub).
    Q_PROPERTY(QString manualUrl READ manualUrl CONSTANT)
    // The operator's callsign (the radio's; {CALL} in CW memories).
    Q_PROPERTY(QString callsign READ callsign NOTIFY callsignChanged)
    Q_PROPERTY(QString radioName READ radioName NOTIFY connectionStateChanged)
    Q_PROPERTY(double frequencyMhz READ frequencyMhz WRITE setFrequencyMhz NOTIFY frequencyChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QStringList modes READ modes NOTIFY modesChanged)
    Q_PROPERTY(double afGain READ afGain WRITE setAfGain NOTIFY afGainChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double sMeterDbm READ sMeterDbm NOTIFY sMeterChanged)
    Q_PROPERTY(QString sMeterText READ sMeterText NOTIFY sMeterChanged)
    Q_PROPERTY(double panCenterMhz READ panCenterMhz NOTIFY panGeometryChanged)
    Q_PROPERTY(double panBandwidthMhz READ panBandwidthMhz NOTIFY panGeometryChanged)
    Q_PROPERTY(int filterLow READ filterLow NOTIFY filterChanged)
    Q_PROPERTY(int filterHigh READ filterHigh NOTIFY filterChanged)
    Q_PROPERTY(QList<int> filterPresets READ filterPresets NOTIFY modeChanged)
    // The preset nearest the current passband width, or 0 if none is close.
    Q_PROPERTY(int activeFilterPreset READ activeFilterPreset NOTIFY filterChanged)
    Q_PROPERTY(int filterWidth READ filterWidth NOTIFY filterChanged)
    // HF bands, the radio's transverter (XVTR) bands, WWV and GEN.
    // FM repeater and tone of the active slice (FM, NFM, DFM), as the
    // desktop's RX applet: offset direction "simplex" / "down" / "up", the
    // offset in MHz, CTCSS on transmit and its tone ("88.5"). The radio
    // shifts the transmit frequency and sends the tone.
    Q_PROPERTY(bool fmMode READ fmMode NOTIFY modeChanged)
    Q_PROPERTY(QString repeaterDir READ repeaterDir WRITE setRepeaterDir NOTIFY fmChanged)
    Q_PROPERTY(double repeaterOffsetMhz READ repeaterOffsetMhz WRITE setRepeaterOffsetMhz NOTIFY fmChanged)
    Q_PROPERTY(bool ctcssOn READ ctcssOn WRITE setCtcssOn NOTIFY fmChanged)
    Q_PROPERTY(QString ctcssTone READ ctcssTone WRITE setCtcssTone NOTIFY fmChanged)
    Q_PROPERTY(QStringList ctcssTones READ ctcssTones CONSTANT)
    Q_PROPERTY(QStringList bands READ bands NOTIFY bandsChanged)
    // The band button the active slice's frequency falls in, lit as the
    // desktop's: a transverter's whole band, WWV's frequencies, else GEN.
    Q_PROPERTY(QString activeBand READ activeBand NOTIFY frequencyChanged)
    Q_PROPERTY(bool bandsAvailable READ bandsAvailable NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastAddress READ lastAddress NOTIFY lastAddressChanged)
    // Radios entered by IP address or host name (Mike Walker VA3MW: several,
    // friends' radios shared over Tailscale too), in the order added:
    // address (as entered, a name looked up at each connect), name (the
    // user's, may be empty), radio (what the radio called itself at the last
    // connect: nickname and model), last (the one connected or tried last).
    Q_PROPERTY(QVariantList savedRadios READ savedRadios NOTIFY savedRadiosChanged)
    Q_PROPERTY(QString smartLinkState READ smartLinkState NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkAccount READ smartLinkAccount NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkEmail READ smartLinkEmail NOTIFY smartLinkChanged)
    Q_PROPERTY(QVariantList smartLinkRadios READ smartLinkRadios NOTIFY smartLinkRadiosChanged)
    // The connection doctor (MobileEngineDoctor.cpp): what blocks a connection
    // to the last radio tried, step by step in plain words. Each step is
    // {title, state: "running" | "ok" | "warn" | "fail" | "info", detail}.
    Q_PROPERTY(QVariantList connectionCheck READ connectionCheck NOTIFY connectionCheckChanged)
    Q_PROPERTY(bool connectionCheckRunning READ connectionCheckRunning NOTIFY connectionCheckChanged)
    Q_PROPERTY(QString connectionCheckTarget READ connectionCheckTarget NOTIFY connectionCheckChanged)
    Q_PROPERTY(bool remoteSession READ remoteSession NOTIFY connectionStateChanged)
    Q_PROPERTY(bool controlsVisible READ controlsVisible WRITE setControlsVisible NOTIFY controlsVisibleChanged)
    Q_PROPERTY(bool tuneRowsShown READ tuneRowsShown WRITE setTuneRowsShown NOTIFY tuneRowsChanged)
    Q_PROPERTY(bool radioStepButtons READ radioStepButtons WRITE setRadioStepButtons NOTIFY tuneRowsChanged)
    Q_PROPERTY(bool transmitAvailable READ transmitAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(QString transmitUnavailableReason READ transmitUnavailableReason NOTIFY transmitStateChanged)
    Q_PROPERTY(bool canUseDeviceMicrophone READ canUseDeviceMicrophone NOTIFY transmitStateChanged)
    // Receive audio on the radio's own outputs (speaker, headphones, line out)
    // instead of this device: the desktop's PC audio off (Dennis KM6DF: a
    // headset on the radio). Remembered; applied without reconnecting.
    Q_PROPERTY(bool audioOnRadio READ audioOnRadio WRITE setAudioOnRadio NOTIFY audioRouteChanged)
    Q_PROPERTY(bool audioRouteSelectable READ audioRouteSelectable NOTIFY transmitStateChanged)
    // The radio's own microphone input can be chosen (one besides PC).
    Q_PROPERTY(bool canUseRadioMicrophone READ canUseRadioMicrophone NOTIFY transmitStateChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY transmittingChanged)
    Q_PROPERTY(bool nr READ nr WRITE setNr NOTIFY dspChanged)
    Q_PROPERTY(bool nb READ nb WRITE setNb NOTIFY dspChanged)
    Q_PROPERTY(bool anf READ anf WRITE setAnf NOTIFY dspChanged)
    // The radio-side DSP toggles this radio and mode offer (as the desktop
    // VFO's DSP grid): NR NB ANF, APF in CW, the LMS/FFT and 8000-series sets,
    // and the app's own beat cancel (BC) in voice modes.
    Q_PROPERTY(QStringList dspKeys READ dspKeys NOTIFY dspChanged)
    Q_PROPERTY(QString agcMode READ agcMode WRITE setAgcMode NOTIFY dspChanged)
    // AGC threshold (AGC-T, 0…100) of the active slice.
    Q_PROPERTY(int agcThreshold READ agcThreshold WRITE setAgcThreshold NOTIFY dspChanged)
    // Auto AGC-T (roadmap 32 and 48): the radio's own where its Early Access
    // licence has it (radioAvailable, radioOn, radioPending), else the app's
    // measurement (measuring, progress 0-100), with a note for the AGC panel
    // and the value remembered for this band and mode group (-1 none).
    Q_PROPERTY(QVariantMap agcAuto READ agcAuto NOTIFY agcAutoChanged)
    // A MIDI controller (roadmap 54: Lynovation's CTR2-MIDI over Bluetooth or
    // USB): supported, ports, port (the open one), lastDevice, autoConnect,
    // transmit (TX from MIDI allowed), learning (the function waiting for a
    // control), pairing (Bluetooth pairing offered here), onMac, functions
    // [{id, name, category, source, tx}] and import (the last import's result).
    Q_PROPERTY(QVariantMap midi READ midi NOTIFY midiChanged)
    // Big buttons (roadmap 55): the operating page covered by a few large
    // controls (tune down / up by the step, the step, PTT); this session only.
    Q_PROPERTY(bool bigButtons READ bigButtons WRITE setBigButtons NOTIFY bigButtonsChanged)
    // The built-in demo radio on the Connect page (Ian Gilchrist VK1HF):
    // shown unless hidden; and the radios on this network besides it.
    Q_PROPERTY(bool showDemoRadio READ showDemoRadio WRITE setShowDemoRadio NOTIFY showDemoRadioChanged)
    Q_PROPERTY(int lanRadioCount READ lanRadioCount NOTIFY lanRadiosChanged)
    // The last MIDI message, e.g. "CC 100 = 65" (its own signal: a knob
    // turning sends tens a second).
    Q_PROPERTY(QString midiActivity READ midiActivity NOTIFY midiActivityChanged)
    // RF gain (preamp/attenuator) of the shown panadapter, in its own steps.
    Q_PROPERTY(int rfGain READ rfGain WRITE setRfGain NOTIFY rfGainChanged)
    Q_PROPERTY(int rfGainLow READ rfGainLow NOTIFY rfGainChanged)
    Q_PROPERTY(int rfGainHigh READ rfGainHigh NOTIFY rfGainChanged)
    Q_PROPERTY(int rfGainStep READ rfGainStep NOTIFY rfGainChanged)
    // The radio's memory channels: {index, name, freq, mode, group}, by frequency.
    Q_PROPERTY(QVariantList memories READ memories NOTIFY memoriesChanged)
    // QSO log (the Log tab): the newest contacts first, how many there are,
    // the callsign lookup for the call being logged (QRZ.com when an account
    // is set, else cty.dat's country), the call of the spot tapped last, the
    // QRZ.com user name, and the operator's grid when the radio has no GPS.
    Q_PROPERTY(QVariantList recentQsos READ recentQsos NOTIFY qsoLogChanged)
    // Where the log file can be found in the system's file app.
    Q_PROPERTY(QString qsoLogLocation READ qsoLogLocation NOTIFY qsoLogChanged)
    // ---- Digi tab: FT8 / FT4 on this device ----
    // Decoding on or off; "FT8" or "FT4"; the transmit audio frequency (Hz).
    Q_PROPERTY(bool digiRunning READ digiRunning WRITE setDigiRunning NOTIFY digiChanged)
    Q_PROPERTY(QString digiProtocol READ digiProtocol WRITE setDigiProtocol NOTIFY digiChanged)
    Q_PROPERTY(int digiTxHz READ digiTxHz WRITE setDigiTxHz NOTIFY digiChanged)
    // The transmit audio level in dB below full scale (-20 to 0), as WSJT-X's
    // Pwr: below the radio's full drive the power out falls short of the
    // TX tab's power.
    Q_PROPERTY(int digiTxLevelDb READ digiTxLevelDb WRITE setDigiTxLevelDb NOTIFY digiChanged)
    // Keep calling CQ after a QSO that answered our CQ (until Halt TX or
    // the CQ watchdog); the slot CQ goes out in: "auto", "even" or "odd".
    Q_PROPERTY(bool digiAutoCq READ digiAutoCq WRITE setDigiAutoCq NOTIFY digiChanged)
    // Answer CQ (Kurt DL5EAG): while idle, the app answers a CQ heard on the
    // TX slice by itself (one that the Show filter passes, not worked on
    // this band and mode, the rarest first), 5 calls per station, then
    // another. On for this session only; Halt TX, the transmit lock and
    // the presence limit switch it off.
    Q_PROPERTY(bool digiAutoAnswer READ digiAutoAnswer WRITE setDigiAutoAnswer NOTIFY digiChanged)
    // The presence limit (minutes, 1-60): Answer CQ and Keep CQ start no
    // new QSO after this long without a touch or key on the app.
    Q_PROPERTY(int digiPresenceMin READ digiPresenceMin WRITE setDigiPresenceMin NOTIFY digiChanged)
    // Hound (roadmap 10 a): working a DXpedition's Fox in FT8 DXpedition
    // mode, by WSJT-X's rules: calls in the second (odd) slot at 1000-3000
    // Hz, R+report sent by itself on the frequency the Fox called on, the
    // Fox's RR73 ends and logs. FT8 only; on for this session only, off on a
    // band change. digiHoundAllHz: the list shows the pileup above 1000 Hz
    // too (WSJT-X's Rx All Freqs).
    Q_PROPERTY(bool digiHound READ digiHound WRITE setDigiHound NOTIFY digiChanged)
    Q_PROPERTY(bool digiHoundAllHz READ digiHoundAllHz WRITE setDigiHoundAllHz NOTIFY digiChanged)
    // Report decoded stations to PSK Reporter (off by default); the status
    // line under it: what was sent, or why nothing can be.
    Q_PROPERTY(bool pskReporter READ pskReporter WRITE setPskReporter NOTIFY digiChanged)
    Q_PROPERTY(QString pskReporterStatus READ pskReporterStatus NOTIFY digiChanged)
    Q_PROPERTY(QString digiCqParity READ digiCqParity WRITE setDigiCqParity NOTIFY digiChanged)
    // A directed CQ (Kurt DL5EAG): "CQ DX CALL GRID"; "" a plain CQ. The
    // choices are the user's own (1-4 letters or 3 digits, as FT8 carries).
    // Only a standard call can send one (digiCqDirectable).
    Q_PROPERTY(QString digiCqTarget READ digiCqTarget WRITE setDigiCqTarget NOTIFY digiChanged)
    Q_PROPERTY(QStringList digiCqTargets READ digiCqTargets NOTIFY digiChanged)
    Q_PROPERTY(bool digiCqDirectable READ digiCqDirectable NOTIFY digiChanged)
    // Beat cancel can run (every audible slice in a voice mode); the BC
    // button shows dimmed when not.
    Q_PROPERTY(bool beatCancelAvailable READ beatCancelMode NOTIFY dspChanged)
    // The QSO: state ("idle", "cq", "answer"), dx, next message, status line,
    // transmitting, seconds into the slot, slot parity of our transmissions.
    Q_PROPERTY(QVariantMap digi READ digi NOTIFY digiChanged)
    // Decodes, newest first: utc, snr, dt, freq, text, call, cq, toMe, slotOdd,
    // hl (the WSJT-X highlight), newDxcc (CQ from a DXCC not in the log: 2,
    // not on this band: 1), sliceId, slice.
    Q_PROPERTY(QVariantList digiDecodes READ digiDecodes NOTIFY digiDecodesChanged)
    // Which decodes the list shows: "all", "cq", "new" (a new continent, CQ
    // zone or DXCC, also per band) or "dxcc" (a new DXCC, also per band).
    Q_PROPERTY(QString digiFilter READ digiFilter WRITE setDigiFilter NOTIFY digiFilterChanged)
    // FTx tab in the drawer: the decode list scrolls on its own under fixed
    // main buttons (Michael Whaley's option B), or the whole tab scrolls.
    Q_PROPERTY(bool digiListScroll READ digiListScroll WRITE setDigiListScroll NOTIFY digiListScrollChanged)
    // RADE digital voice in the app (FreeDV Radio Autoencoder): on or off,
    // whether this build has it, and its receive state (synced, snr,
    // callsign of the last end of over).
    Q_PROPERTY(bool rade READ radeActive WRITE setRadeActive NOTIFY radeChanged)
    Q_PROPERTY(bool radeAvailable READ radeAvailable CONSTANT)
    Q_PROPERTY(QVariantMap radeStatus READ radeStatus NOTIFY radeChanged)
    // FreeDV Reporter (qso.freedv.org, the FreeDV tab): report this station
    // while RADE runs (off by default; callsign, grid, frequency, transmit
    // and what RADE hears go to the public map), the message shown there,
    // and the state in words.
    Q_PROPERTY(bool freedvReporter READ freedvReporter WRITE setFreedvReporter NOTIFY freedvChanged)
    Q_PROPERTY(QString freedvMessage READ freedvMessage WRITE setFreedvMessage NOTIFY freedvChanged)
    Q_PROPERTY(QString freedvStatus READ freedvStatus NOTIFY freedvChanged)
    // Reporting now: connected and reporting this station (status in green).
    Q_PROPERTY(bool freedvLive READ freedvLive NOTIFY freedvChanged)
    // The stations FreeDV Reporter lists, on this band or all bands, by
    // frequency: {callsign, grid, km, mhz, mode, tx, rxOnly, heard, snr,
    // message, you}. Fetched while the FreeDV tab shows or RADE reports.
    Q_PROPERTY(QVariantList freedvStations READ freedvStations NOTIFY freedvStationsChanged)
    Q_PROPERTY(bool freedvAllBands READ freedvAllBands WRITE setFreedvAllBands NOTIFY freedvStationsChanged)
    // Dial frequencies of the protocol on each band: band, mhz.
    Q_PROPERTY(QVariantList digiFrequencies READ digiFrequencies NOTIFY digiChanged)
    Q_PROPERTY(int qsoCount READ qsoCount NOTIFY qsoLogChanged)
    Q_PROPERTY(QVariantMap callLookup READ callLookup NOTIFY callLookupChanged)
    Q_PROPERTY(QString lastSpotCall READ lastSpotCall NOTIFY lastSpotCallChanged)
    Q_PROPERTY(QString qrzUser READ qrzUser NOTIFY qrzChanged)
    // The sign-in check after Save: the text, and "checking", "ok", "failed"
    // (refused), "unreachable" (QRZ.com did not answer) or "" (not checked
    // this session).
    Q_PROPERTY(QString qrzStatus READ qrzStatus NOTIFY qrzChanged)
    Q_PROPERTY(QString qrzStatusKind READ qrzStatusKind NOTIFY qrzChanged)
    Q_PROPERTY(QString myGrid READ myGrid WRITE setMyGrid NOTIFY qrzChanged)
    // Sending each logged QSO to a Wavelog or Cloudlog logbook (the same
    // API): url, stationId, hasKey, enabled, pending (queued, unsent),
    // status and statusKind ("checking", "ok", "failed" or "").
    Q_PROPERTY(QVariantMap onlineLog READ onlineLog NOTIFY onlineLogChanged)
    // QRZ.com Logbook both ways (MobileEngineQrzLog.cpp): hasKey, pending,
    // status, statusKind; read, reading, known, readStatus, readStatusKind.
    Q_PROPERTY(QVariantMap qrzLog READ qrzLog NOTIFY qrzLogChanged)
    // The shared ADIF file both ways (MobileEngineSharedAdif.cpp): set, name,
    // known, pending, reading, status, statusKind.
    Q_PROPERTY(QVariantMap sharedAdif READ sharedAdif NOTIFY sharedAdifChanged)
    // Logger32 on the operator's PC: each logged QSO as an ADIF record over
    // TCP (MobileEngineLogger32.cpp).
    Q_PROPERTY(QVariantMap logger32 READ logger32 NOTIFY logger32Changed)
    // An RF-KIT RF2K-S amplifier over its REST API (MobileEngineAmp.cpp):
    // set, address, on, control (commands allowed), state ("off",
    // "connecting", "connected", "failed"), error, target, pending (the
    // command waiting for the amplifier's answer: "operate", "standby",
    // "antenna N", "reset"), result / resultOk (the last command's outcome);
    // tune ("", "standby": asking for STANDBY, "carrier": the tune carrier
    // on), tuneLeft (s), tunePower (W);
    // once read: device, name, firmware, operate ("OPERATE" / "STANDBY"),
    // fwd, refl (W), swr, temp (°C), volts, amps, band, freq, status, tuner,
    // tunerSetup, iface, ifaceError, antenna, antennas, antennaList ({label,
    // type, number, state} each). A reading not in yet is absent.
    Q_PROPERTY(QVariantMap amp READ amp NOTIFY ampChanged)
    // 4O3A's Power Genius XL and Tuner Genius XL, as the radio reports and
    // relays them and as the app reads them directly (MobileEngine4o3a.cpp).
    Q_PROPERTY(QVariantMap pgxl READ pgxl NOTIFY pgxlChanged)
    Q_PROPERTY(QVariantMap tgxl READ tgxl NOTIFY tgxlChanged)
    // 4O3A Antenna Genius (MobileEngine4o3a.cpp): read at the address set in
    // Radio Setup › Amp, its two radio ports' band and antennas.
    Q_PROPERTY(QVariantMap ag READ ag NOTIFY agChanged)
    // APD (adaptive pre-distortion) on a radio that offers it (FLEX-8000,
    // Aurora; the radio's "apd configurable=1"): configurable, enabled,
    // equalizerActive, samplers [{ant, selected, available}] for ANT1, ANT2,
    // XVTA, XVTB, as the desktop's TX applet and Radio Setup › APD.
    Q_PROPERTY(QVariantMap apd READ apd NOTIFY apdChanged)
    Q_PROPERTY(bool memoriesWritable READ memoriesWritable NOTIFY memoriesChanged)
    // The radio's tracking notch filters (Flex TNF): {id, freq (MHz),
    // width (Hz), depth 1-3, permanent}; tnfEnabled is the global switch.
    Q_PROPERTY(QVariantList tnfs READ tnfs NOTIFY tnfsChanged)
    Q_PROPERTY(bool tnfEnabled READ tnfEnabled WRITE setTnfEnabled NOTIFY tnfsChanged)
    Q_PROPERTY(bool tnfAvailable READ tnfAvailable NOTIFY tnfsChanged)
    Q_PROPERTY(bool canAddTnf READ canAddTnf NOTIFY tnfsChanged)
    Q_PROPERTY(bool tnfHasDepth READ tnfHasDepth NOTIFY tnfsChanged)
    // Spots the radio holds (from every client, and from this app's DX
    // cluster connection): {index, call, freq (MHz), mode, comment, spotter,
    // color, source}; empty while showSpots is off.
    Q_PROPERTY(QVariantList spots READ spots NOTIFY spotsChanged)
    Q_PROPERTY(bool showSpots READ showSpots WRITE setShowSpots NOTIFY spotsChanged)
    // Spot filters (remembered): modes "all", "cw", "phone" or "digital"
    // (the spot's mode, else its comment or the band plan, as the desktop
    // resolves it) and the newest only, 0 (any) or 5/15/30 minutes.
    Q_PROPERTY(QString spotModes READ spotModes WRITE setSpotModes NOTIFY spotsChanged)
    Q_PROPERTY(int spotMaxAgeMin READ spotMaxAgeMin WRITE setSpotMaxAgeMin NOTIFY spotsChanged)
    // DX cluster: {host, port, callsign (the radio's when not set),
    // callsignFromRadio, autoConnect, state off/connecting/connected, error}.
    Q_PROPERTY(QVariantMap dxCluster READ dxCluster NOTIFY dxClusterChanged)
    // The cluster connection in words (connected to which server as whom,
    // the spots so far and the latest one's time), as the logbook's and PSK
    // Reporter's status lines; kind "ok", "failed" or "" (on the way, off).
    // Its own signal: spots arrive often, and the server fields must not be
    // refreshed under the user's fingers each time.
    Q_PROPERTY(QString dxClusterStatus READ dxClusterStatus NOTIFY dxClusterStatusChanged)
    Q_PROPERTY(QString dxClusterStatusKind READ dxClusterStatusKind NOTIFY dxClusterStatusChanged)
    Q_PROPERTY(QStringList agcModes READ agcModes CONSTANT)
    Q_PROPERTY(int stepHz READ stepHz NOTIFY stepChanged)
    // The slice's step list from the radio (Hz), for the step chooser.
    Q_PROPERTY(QList<int> stepList READ stepList NOTIFY stepChanged)
    Q_PROPERTY(bool ritOn READ ritOn WRITE setRitOn NOTIFY ritXitChanged)
    Q_PROPERTY(int ritHz READ ritHz WRITE setRitHz NOTIFY ritXitChanged)
    Q_PROPERTY(bool xitOn READ xitOn WRITE setXitOn NOTIFY ritXitChanged)
    Q_PROPERTY(int xitHz READ xitHz WRITE setXitHz NOTIFY ritXitChanged)
    Q_PROPERTY(bool squelchAvailable READ squelchAvailable NOTIFY modeChanged)
    Q_PROPERTY(bool squelchOn READ squelchOn WRITE setSquelchOn NOTIFY squelchChanged)
    Q_PROPERTY(int squelchLevel READ squelchLevel WRITE setSquelchLevel NOTIFY squelchChanged)
    Q_PROPERTY(QString rxAntenna READ rxAntenna WRITE setRxAntenna NOTIFY antennaChanged)
    Q_PROPERTY(QStringList rxAntennas READ rxAntennas NOTIFY antennaChanged)
    Q_PROPERTY(QString txAntenna READ txAntenna WRITE setTxAntenna NOTIFY antennaChanged)
    Q_PROPERTY(QStringList txAntennas READ txAntennas NOTIFY antennaChanged)
    Q_PROPERTY(QVariantList slices READ slices NOTIFY slicesChanged)
    // Every GUI client on the radio (multiFLEX), this app's marked: handle,
    // station, program, ours.
    Q_PROPERTY(QVariantList stations READ stations NOTIFY stationsChanged)
    // A radio that takes one station at a time (multiFLEX off, or a
    // one-client licence over SmartLink) with another station on it: the
    // connect waits for an answer. Empty when none; else open, radio, wan and
    // clients [{handle, program, station}].
    Q_PROPERTY(QVariantMap stationConflict READ stationConflict NOTIFY stationConflictChanged)
    Q_PROPERTY(int activeSliceId READ activeSliceId NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddSlice READ canAddSlice NOTIFY slicesChanged)
    // Split, as SmartSDR/AetherSDR: a TX slice beside the RX slice.
    Q_PROPERTY(bool splitActive READ splitActive NOTIFY slicesChanged)
    Q_PROPERTY(bool splitAvailable READ splitAvailable NOTIFY slicesChanged)
    // How split transmits: "slice" (a second slice, as SmartSDR) or "xit"
    // (this slice's XIT, as a Maestro user does it: no second slice, for a
    // radio whose slices other programs hold).
    Q_PROPERTY(QString splitMethod READ splitMethod WRITE setSplitMethod NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddPanadapter READ canAddPanadapter NOTIFY slicesChanged)
    // This client's panadapters. The app shows one; the others still stream.
    Q_PROPERTY(int panCount READ panCount NOTIFY slicesChanged)
    Q_PROPERTY(bool cwMode READ cwMode NOTIFY modeChanged)
    // RTTY (Ben van Schoor): the radio's RTTY mode decoded on the device,
    // as the desktop does (RttyDecoder on the slice's receive audio).
    Q_PROPERTY(bool rttyMode READ rttyMode NOTIFY modeChanged)
    // RTTY received now: the radio's RTTY mode, or DIGL / DIGU chosen for
    // RTTY on the RTTY tab (rttyVia: "RTTY", "DIGL", "DIGU" or "").
    Q_PROPERTY(bool rttyActive READ rttyActive NOTIFY rttyDecoderChanged)
    Q_PROPERTY(bool rttyDecoderEnabled READ rttyDecoderEnabled WRITE setRttyDecoderEnabled NOTIFY rttyDecoderChanged)
    Q_PROPERTY(QString rttyDecodedText READ rttyDecodedText NOTIFY rttyDecoderChanged)
    Q_PROPERTY(QString rttyStrip READ rttyStrip NOTIFY rttyDecoderChanged)
    Q_PROPERTY(qint64 rttyStripStart READ rttyStripStart NOTIFY rttyDecoderChanged)
    // mark / shift (0 = the radio's), baud, reverse, and what is in use:
    // markHz / shiftHz, snr, locked.
    Q_PROPERTY(QVariantMap rtty READ rtty NOTIFY rttyDecoderChanged)
    // PSK31 / PSK63 on the Digi tab (G3PLX's BPSK, in DIGU as the standard
    // has it): the mode chosen ("RTTY", "PSK31", "PSK63"; the radio's RTTY
    // mode is always RTTY), whether PSK is received now (DIGU / DIGL, FT8 /
    // FT4 off), and its state: mode, baud, carrierHz (the audio frequency
    // set, a tap on a signal), trackedHz (where AFC holds it), carrierMhz
    // (its radio frequency), afc, quality (0-100), locked, via.
    Q_PROPERTY(QString textMode READ textMode NOTIFY rttyDecoderChanged)
    Q_PROPERTY(bool pskActive READ pskActive NOTIFY rttyDecoderChanged)
    Q_PROPERTY(QVariantMap psk READ psk NOTIFY rttyDecoderChanged)
    // RTTY or PSK received now: what the macros and the text field send in.
    Q_PROPERTY(bool textModeActive READ textModeActive NOTIFY rttyDecoderChanged)
    // The Digi tab's keyboard modes (RTTY, PSK31, PSK63): macros [{label,
    // text}] as the CW memories ({CALL}, {HIS}, {NAME}, {RST}, {GRID}), the
    // calls the received text holds (after DE, or twice), and whether text
    // is going out.
    Q_PROPERTY(QVariantList textMacros READ textMacros NOTIFY textMacrosChanged)
    Q_PROPERTY(QStringList textCalls READ textCalls NOTIFY rttyDecoderChanged)
    Q_PROPERTY(bool textTransmitting READ textTransmitting NOTIFY textTxChanged)
    Q_PROPERTY(bool cwKeyAvailable READ cwKeyAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(bool cwxAvailable READ cwxAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(bool cwKeyDown READ cwKeyDown NOTIFY cwKeyDownChanged)
    // CW keying on the screen: a straight key (KEY) or paddles (dit, dah)
    // driving the app's iambic keyer (the desktop's IambicKeyer: mode,
    // speed and swapped paddles follow the radio's settings). Remembered.
    Q_PROPERTY(bool cwPaddles READ cwPaddles WRITE setCwPaddles NOTIFY cwChanged)
    // Paddles on a hardware keyboard: "ctrl" (left and right Ctrl, as USB
    // paddle adapters send; the default), "alt" (left and right Alt / Option,
    // for keyboards with no right Ctrl), "brackets" ([ and ]) or "off".
    // Remembered.
    Q_PROPERTY(QString cwKeyboardPaddles READ cwKeyboardPaddles WRITE setCwKeyboardPaddles NOTIFY cwChanged)
    // The radio's iambic mode (0 = A, 1 = B) and swapped paddles, which the
    // app's keyer follows. The radio's own iambic on/off (its key jack) is
    // left alone.
    // iOS: left and right Control / Option come from GameController, as Qt
    // cannot tell them apart there; the engine keys them itself.
    Q_PROPERTY(bool nativeKeyboardSides READ nativeKeyboardSides CONSTANT)
    // The tablet page has the keyboard (no dialog, no text entry): only then
    // does Space from GameController key PTT (iOS). Set by OperatePage.
    Q_PROPERTY(bool keyboardOnPage READ keyboardOnPage WRITE setKeyboardOnPage NOTIFY keyboardOnPageChanged)
    Q_PROPERTY(int cwIambicMode READ cwIambicMode WRITE setCwIambicMode NOTIFY cwChanged)
    Q_PROPERTY(bool cwSwapPaddles READ cwSwapPaddles WRITE setCwSwapPaddles NOTIFY cwChanged)
    Q_PROPERTY(int cwSpeed READ cwSpeed WRITE setCwSpeed NOTIFY cwChanged)
    Q_PROPERTY(int cwPitch READ cwPitch WRITE setCwPitch NOTIFY cwChanged)
    Q_PROPERTY(bool cwBreakIn READ cwBreakIn WRITE setCwBreakIn NOTIFY cwChanged)
    // The radio's CW sidetone and its level (mon_gain_cw), apart from the
    // receive volume; the local sidetone plays at the same level.
    Q_PROPERTY(bool cwSidetone READ cwSidetone WRITE setCwSidetone NOTIFY cwChanged)
    Q_PROPERTY(int cwSidetoneGain READ cwSidetoneGain WRITE setCwSidetoneGain NOTIFY cwChanged)
    Q_PROPERTY(bool cwDecoderEnabled READ cwDecoderEnabled WRITE setCwDecoderEnabled NOTIFY cwDecoderChanged)
    // Decoder sensitivity 0-100 (the desktop's Sens): letters whose cost is at
    // or above 1 - 0.9 x sens/100 are not shown (band noise decodes as such).
    Q_PROPERTY(int cwDecoderSensitivity READ cwDecoderSensitivity WRITE setCwDecoderSensitivity NOTIFY cwDecoderChanged)
    // Tuning lock of the active slice (the radio's slice lock; SliceModel
    // refuses tuning while it is on). tuneBlocked fires on a refused tune.
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    // The radio's quick record and playback on the active slice (SmartSDR's
    // REC / PLAY): playAvailable once something has been recorded.
    Q_PROPERTY(bool recordOn READ recordOn NOTIFY recordPlayChanged)
    Q_PROPERTY(bool playOn READ playOn NOTIFY recordPlayChanged)
    Q_PROPERTY(bool playAvailable READ playAvailable NOTIFY recordPlayChanged)
    // The radio's profiles (as SmartSDR/AetherSDR): global (band, pans,
    // slices…), transmit and mic. Type is "global", "transmit" or "mic".
    Q_PROPERTY(QStringList globalProfiles READ globalProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeGlobalProfile READ activeGlobalProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList txProfiles READ txProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeTxProfile READ activeTxProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList micProfiles READ micProfiles NOTIFY profilesChanged)
    // Named app setups (Larry Averbeck W9FLA, roadmap 17): the app's own
    // settings under a name, switched on the Prof tab, exported as a file
    // and imported on another device. The last one used, "" when none.
    Q_PROPERTY(QStringList appSetups READ appSetups NOTIFY appSetupsChanged)
    Q_PROPERTY(QString activeAppSetup READ activeAppSetup NOTIFY appSetupsChanged)
    Q_PROPERTY(QString activeMicProfile READ activeMicProfile NOTIFY profilesChanged)
    // TX audio passband (radio: transmit filter_low/filter_high).
    Q_PROPERTY(int txFilterLow READ txFilterLow WRITE setTxFilterLow NOTIFY txFilterChanged)
    Q_PROPERTY(int txFilterHigh READ txFilterHigh WRITE setTxFilterHigh NOTIFY txFilterChanged)
    // Radio's 8-band RX/TX equalizer (63 Hz … 8 kHz, −10…+10 dB).
    Q_PROPERTY(QStringList eqBandLabels READ eqBandLabels CONSTANT)
    Q_PROPERTY(bool eqTxEnabled READ eqTxEnabled NOTIFY eqChanged)
    Q_PROPERTY(bool eqRxEnabled READ eqRxEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqTxBands READ eqTxBands NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqRxBands READ eqRxBands NOTIFY eqChanged)
    // CW memories (nine, remembered): sent through CWX; {CALL} becomes the
    // radio's callsign.
    Q_PROPERTY(QStringList cwMemories READ cwMemories NOTIFY cwMemoriesChanged)
    // Display levels (remembered, client-side like the desktop's SW auto
    // black): the waterfall's black level and colour gain shift and stretch
    // the auto-tracked range (Auto off freezes it); the trace is auto-ranged
    // or set by reference level (top) and range.
    Q_PROPERTY(bool wfAutoLevel READ wfAutoLevel WRITE setWfAutoLevel NOTIFY displayLevelsChanged)
    // The display's corner buttons mirrored for a right hand (Larry
    // Averbeck): zoom, Band and B/S at the right, + Slice / + Pan at the left.
    Q_PROPERTY(bool displayButtonsRight READ displayButtonsRight WRITE setDisplayButtonsRight NOTIFY displayLevelsChanged)
    Q_PROPERTY(int wfBlackLevel READ wfBlackLevel WRITE setWfBlackLevel NOTIFY displayLevelsChanged)
    Q_PROPERTY(int wfColorGain READ wfColorGain WRITE setWfColorGain NOTIFY displayLevelsChanged)
    Q_PROPERTY(bool traceAutoRange READ traceAutoRange WRITE setTraceAutoRange NOTIFY displayLevelsChanged)
    Q_PROPERTY(int traceRefDbm READ traceRefDbm WRITE setTraceRefDbm NOTIFY displayLevelsChanged)
    Q_PROPERTY(int traceRangeDb READ traceRangeDb WRITE setTraceRangeDb NOTIFY displayLevelsChanged)
    // 3D stacked trace (the desktop's 3DSS, CPU-drawn) in place of the
    // 2D waterfall.
    Q_PROPERTY(bool spectrum3D READ spectrum3D WRITE setSpectrum3D NOTIFY displayLevelsChanged)
    // The spectrum's share of the display in percent (the desktop's
    // draggable divider, 10-90); 0 is automatic. Remembered.
    Q_PROPERTY(int spectrumSplit READ spectrumSplit WRITE setSpectrumSplit NOTIFY displayLevelsChanged)
    Q_PROPERTY(int spectrum3DFloorDepth READ spectrum3DFloorDepth WRITE setSpectrum3DFloorDepth NOTIFY displayLevelsChanged)
    Q_PROPERTY(int spectrum3DGain READ spectrum3DGain WRITE setSpectrum3DGain NOTIFY displayLevelsChanged)
    Q_PROPERTY(int spectrum3DSmoothing READ spectrum3DSmoothing WRITE setSpectrum3DSmoothing NOTIFY displayLevelsChanged)
    // Each meter's style, remembered: "analog" (arc and needle, also while
    // transmitting), "bar" (one bar) or "two" (two bars, one over the
    // other, each with its own receive and transmit choice: three or four
    // readings at once) or "analog2" (the same two readings as two analog
    // faces side by side where the width allows, else two bars).
    // analogMeters sets both to analog or bar (true only when both are
    // analog).
    Q_PROPERTY(QString meterStyleLeft READ meterStyleLeft NOTIFY displayLevelsChanged)
    Q_PROPERTY(QString meterStyleRight READ meterStyleRight NOTIFY displayLevelsChanged)
    Q_PROPERTY(bool analogMeters READ analogMeters WRITE setAnalogMeters NOTIFY displayLevelsChanged)
    Q_PROPERTY(bool analogLeft READ analogLeft NOTIFY displayLevelsChanged)
    // Both meters' peak hold, as the desktop S meter's decay choice:
    // 0 off, 1 fast, 2 medium (the default), 3 slow. Remembered.
    Q_PROPERTY(int meterPeakHold READ meterPeakHold WRITE setMeterPeakHold NOTIFY displayLevelsChanged)
    Q_PROPERTY(bool analogRight READ analogRight NOTIFY displayLevelsChanged)
    // Sections shown, one under another, in the pinned iPad control panel
    // (as AetherSDR's applet buttons), remembered.
    Q_PROPERTY(QStringList pinnedSections READ pinnedSections WRITE setPinnedSections NOTIFY displayLevelsChanged)
    Q_PROPERTY(QString cwDecodedText READ cwDecodedText NOTIFY cwDecoderChanged)
    // The decoded CW for the strip over the waterfall: whitespace runs as
    // one space; cwStripStart is its first character's place in the whole
    // stream, so lines break at the same characters as text is dropped.
    Q_PROPERTY(QString cwStrip READ cwStrip NOTIFY cwDecoderChanged)
    // The station being worked: the Log tab's Call field, which the CW tab
    // sets too (a heard call); CW memories send it for {HIS}.
    Q_PROPERTY(QString workedCall READ workedCall WRITE setWorkedCall NOTIFY workedCallChanged)
    Q_PROPERTY(qint64 cwStripStart READ cwStripStart NOTIFY cwDecoderChanged)
    Q_PROPERTY(QString cwDecoderStats READ cwDecoderStats NOTIFY cwDecoderChanged)
    // Callsigns in the decoded CW (after DE, or heard twice), newest first,
    // at most three: the Log tab offers them for its Call field.
    Q_PROPERTY(QStringList cwCalls READ cwCalls NOTIFY cwDecoderChanged)
    Q_PROPERTY(int rfPower READ rfPower WRITE setRfPower NOTIFY rfPowerChanged)
    // This device's microphone gain in dB (-20 .. +30), applied before the
    // audio is sent: the radio has no gain stage for its PC input, so its
    // mic level does nothing to this microphone.
    Q_PROPERTY(double micGainDb READ micGainDb WRITE setMicGainDb NOTIFY micGainChanged)
    Q_PROPERTY(double micLevelDbfs READ micLevelDbfs NOTIFY txMetersChanged)
    Q_PROPERTY(double micPeakDbfs READ micPeakDbfs NOTIFY txMetersChanged)
    // Whether the Mic level reads now: the radio meters a mic on its own
    // jacks all the time; this device's microphone is measured here, while
    // it is sent (on the air).
    Q_PROPERTY(bool micMeterLive READ micMeterLive NOTIFY txMetersChanged)
    // This device's microphone processing before it is sent (AetherSDR's
    // client-side TX chain): gateOn, gateThreshold (dB), eqOn, lowCut (Hz,
    // 0 = off), bass, presence (dB), compOn, compThreshold, compRatio,
    // compMakeup. compReductionDb: the compressor's gain reduction now (<= 0).
    Q_PROPERTY(QVariantMap txChain READ txChain NOTIFY txChainChanged)
    Q_PROPERTY(double compReductionDb READ compReductionDb NOTIFY txMetersChanged)
    Q_PROPERTY(double forwardPowerW READ forwardPowerW NOTIFY txMetersChanged)
    // The power the radio is rated for, for the Power meter's scale (Sloan
    // Davis: on an Aurora it turned red past 100 W): the radio's
    // max_power_level, 500 W for an Aurora (AU-), whose max_power_level
    // reports only the exciter's 100 W, as the desktop's gauge (#484).
    Q_PROPERTY(int ratedPowerW READ ratedPowerW NOTIFY ratedPowerChanged)
    Q_PROPERTY(double swr READ swr NOTIFY txMetersChanged)
    Q_PROPERTY(bool swrValid READ swrValid NOTIFY txMetersChanged)
    Q_PROPERTY(bool radioTransmitting READ radioTransmitting NOTIFY txMetersChanged)
    // The radio's RF power setting is a percentage, not watts: an Aurora
    // (AU-510 / AU-520, a 500 W PA) where 29 gives about 145 W.
    Q_PROPERTY(bool rfPowerIsPercent READ rfPowerIsPercent NOTIFY connectionStateChanged)
    Q_PROPERTY(double compressionDb READ compressionDb NOTIFY txMetersChanged)
    Q_PROPERTY(double alcDbfs READ alcDbfs NOTIFY txMetersChanged)
    Q_PROPERTY(bool alcAvailable READ alcAvailable NOTIFY txMetersChanged)
    Q_PROPERTY(double paTempC READ paTempC NOTIFY txMetersChanged)
    Q_PROPERTY(bool hasPaTemp READ hasPaTemp NOTIFY txMetersChanged)
    Q_PROPERTY(double supplyVolts READ supplyVolts NOTIFY txMetersChanged)
    Q_PROPERTY(bool hasSupplyVolts READ hasSupplyVolts NOTIFY txMetersChanged)
    Q_PROPERTY(QStringList meterKinds READ meterKinds CONSTANT)
    Q_PROPERTY(QString meterLeft READ meterLeft WRITE setMeterLeft NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRight READ meterRight WRITE setMeterRight NOTIFY meterSelectionChanged)
    // While transmitting the faces show their own TX choices (default SWR and
    // Power), so receive keeps S while transmit shows SWR.
    Q_PROPERTY(QString meterLeftTx READ meterLeftTx NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRightTx READ meterRightTx NOTIFY meterSelectionChanged)
    // The lower bars of the two-bar style, receive and transmit: with two
    // bars at the left, Power, SWR and Mic all show (asked by VA3MW).
    Q_PROPERTY(QString meterLeft2 READ meterLeft2 NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterLeft2Tx READ meterLeft2Tx NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRight2 READ meterRight2 NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRight2Tx READ meterRight2Tx NOTIFY meterSelectionChanged)
    // Big buttons' two bars (roadmap 58), their own choices: S and the
    // supply voltage in receive, power and SWR in transmit.
    Q_PROPERTY(QString meterBig1 READ meterBig1 NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterBig1Tx READ meterBig1Tx NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterBig2 READ meterBig2 NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterBig2Tx READ meterBig2Tx NOTIFY meterSelectionChanged)
    // Development (--preview-tx): the meters as while transmitting, nothing
    // keyed (screenshots of the transmit layout).
    Q_PROPERTY(bool previewTx MEMBER m_previewTx NOTIFY meterSelectionChanged)
    Q_PROPERTY(bool tuning READ tuning NOTIFY tuneStateChanged)
    Q_PROPERTY(bool tuneAvailable READ tuneAvailable NOTIFY tuneStateChanged)
    Q_PROPERTY(int tunePower READ tunePower WRITE setTunePower NOTIFY tuneStateChanged)
    Q_PROPERTY(bool hasTuner READ hasTuner NOTIFY atuChanged)
    Q_PROPERTY(bool hasTunerMemories READ hasTunerMemories NOTIFY atuChanged)
    Q_PROPERTY(QString atuStatusText READ atuStatusText NOTIFY atuChanged)
    Q_PROPERTY(bool atuTuned READ atuTuned NOTIFY atuChanged)
    Q_PROPERTY(bool atuMemories READ atuMemories WRITE setAtuMemories NOTIFY atuChanged)
    Q_PROPERTY(bool procEnabled READ procEnabled WRITE setProcEnabled NOTIFY txAudioChanged)
    // Phone transmit extras, as the desktop's Phone applet: voxOn, voxLevel,
    // voxDelay, dexpOn, dexpLevel, amCarrier (levels 0-100).
    Q_PROPERTY(QVariantMap phone READ phone NOTIFY phoneChanged)
    Q_PROPERTY(QVariantMap panDisplay READ panDisplay NOTIFY panDisplayChanged)
    // The radio's voice keyer (DVK): 12 slots of up to 10 s, kept in the
    // radio. Each: id, name, seconds (0 = empty).
    Q_PROPERTY(QVariantList dvkSlots READ dvkSlots NOTIFY dvkChanged)
    // "idle", "recording", "preview", "playback", "unavailable".
    Q_PROPERTY(QString dvkState READ dvkState NOTIFY dvkChanged)
    Q_PROPERTY(int dvkActiveId READ dvkActiveId NOTIFY dvkChanged)
    // True: the radio's DVK (licensed); false: the app's own voice macros,
    // recorded and kept on this device and played into the TX audio.
    Q_PROPERTY(bool dvkOnRadio READ dvkOnRadio NOTIFY dvkChanged)
    // Macros panel › Repeat (Robin Bishop, roadmap 33): a voice keyer slot or
    // a CW memory sent again every macroRepeatGap seconds after it ends,
    // until PTT, KEY, Stop, any key, another slot or the switch. The switch
    // starts off at every launch and goes off on disconnect; the gap is kept.
    Q_PROPERTY(bool macroRepeat READ macroRepeat WRITE setMacroRepeat NOTIFY macroRepeatChanged)
    Q_PROPERTY(int macroRepeatGap READ macroRepeatGap WRITE setMacroRepeatGap NOTIFY macroRepeatChanged)
    // The slot repeating ("F2" voice, "M3" CW), "" when none.
    Q_PROPERTY(QString macroRepeatSlot READ macroRepeatSlot NOTIFY macroRepeatChanged)
    // When the next send is due (ms since the epoch, Date.now()), 0 while
    // the slot is being sent or nothing repeats.
    Q_PROPERTY(double macroRepeatDueAt READ macroRepeatDueAt NOTIFY macroRepeatChanged)
    Q_PROPERTY(int procLevel READ procLevel WRITE setProcLevel NOTIFY txAudioChanged)
    Q_PROPERTY(int procLevelMax READ procLevelMax NOTIFY txAudioChanged)
    Q_PROPERTY(bool monitor READ monitor WRITE setMonitor NOTIFY txAudioChanged)
    // The radio's DAX transmit audio (dax=1): a computer program's audio is
    // sent instead of the microphone.
    Q_PROPERTY(bool dax READ dax WRITE setDax NOTIFY txAudioChanged)
    Q_PROPERTY(int monitorGain READ monitorGain WRITE setMonitorGain NOTIFY txAudioChanged)
    Q_PROPERTY(int radioMicLevel READ radioMicLevel WRITE setRadioMicLevel NOTIFY txAudioChanged)

public:
    explicit MobileEngine(QObject* parent = nullptr);
    ~MobileEngine() override;

    // The instance main() constructed; QML's Engine singleton resolves to it.
    static MobileEngine* instance();

    RadioListModel* radios() { return &m_radios; }
    QString connectionState() const { return m_connectionState; }
    QString lastError() const { return m_lastError; }
    QString callsign() const;
    QString bandPlan() const;
    void setBandPlan(const QString& name);
    QString buttonStyle() const;
    QString textSize() const;
    void setTextSize(const QString& size);
    double textScale() const;
    double systemTextScaleValue() const { return m_systemTextScale; }
    void setButtonStyle(const QString& style);
    bool pttToggle() const;
    void setPttToggle(bool on);
    bool pttLatched() const { return m_pttLatched; }
    // Toggle-mode PTT: keys when not transmitting, unkeys when it is.
    Q_INVOKABLE void togglePtt();
    bool txLock() const;
    void setTxLock(bool on);
    bool txArmAtStart() const;
    void setTxArmAtStart(bool on);
    // The lock on and not armed: transmitting is refused.
    bool txLocked() const;
    // Armed: the seconds left (0 in standby or with the lock off).
    int txArmedSeconds() const;
    // Arms at once (a held TUNE or CQ), or after a second held (PTT, KEY,
    // the Space key: down starts it, up before the second cancels it).
    Q_INVOKABLE void armTransmit();
    Q_INVOKABLE void armHold(bool down);
    // Back to standby at once.
    Q_INVOKABLE void standbyTransmit();
    QStringList bandPlans() const;
    // The active plan's segments (empty when off), for the spectrum.
    QVector<BandPlanManager::Segment> bandPlanSegments() const;
    bool bandPlanLock() const;
    void setBandPlanLock(bool on);
    // The mode the band plan gives a frequency ("CW", "LSB", "USB"), or empty
    // where it gives none (data, beacons, mixed segments, VHF and up), and
    // the modes it allows there (empty: any).
    QString bandPlanMode(double mhz, QString* region = nullptr) const;
    QStringList bandPlanModesAt(double mhz) const;
    QStringList bandPlanModes() const;
    // The mode chooser's choice: refused with a notice where the band plan
    // lock does not allow it.
    Q_INVOKABLE bool chooseMode(const QString& mode);
    QString audioCompression() const;
    void setAudioCompression(const QString& mode);
    QString audioCodingInUse() const { return m_audioCodingInUse; }
    QString audioBuffer() const;
    void setAudioBuffer(const QString& mode);
    void applyAudioBuffer();
    // Android: the devices the receive and microphone streams open on, from
    // the operator's Audio output choice (see the .cpp).
    void applyAudioOutputDevice();
    // Android, Auto with a Bluetooth headset: receive opens again once the
    // headset's hands-free link is up, when it opened before the link.
    void watchHeadsetLink();
    QString audioOutput() const;
    void setAudioOutput(const QString& mode);
    QString networkQuality() const;
    double cpuPercent() const { return m_cpuPercent; }
    double cpuPercentOfCore() const { return m_cpuPercentOfCore; }
    double memoryMB() const { return m_memoryMB; }
    QStringList busyThreads() const { return m_busyThreads; }
    int cpuCores() const;
    int uiFps() const { return m_uiFps; }
    // Counts drawn frames (main.cpp connects the window's frameSwapped).
    void countFrame() { ++m_framesThisPeriod; }
    int networkRttMs() const;
    // The details behind it (link timing, packet integrity per stream,
    // traffic), as the desktop's network diagnostics; the dialog polls it.
    Q_INVOKABLE QVariantMap networkDetails();
    // Radio Setup (as SmartSDR's and the desktop's): the radio's own settings
    // a radio needs on first use. radioSetup() reads them (the page polls);
    // setRadioSetup() writes one by key. They change the radio, not the app.
    Q_INVOKABLE QVariantMap radioSetup() const;
    // USB cables in the radio's USB ports (FTDI serial adapters), as
    // SmartSDR's and the desktop's USB Cables: frequency and band data for an
    // amplifier, a tuner or an antenna switch. Each {sn, type (cat, bcd,
    // vbcd, bcd_vbcd, bit, ldpa, passthrough, invalid), enabled, present,
    // name, speed, dataBits, parity, stopBits, flow, source, sourceRxAnt,
    // sourceTxAnt, sourceSlice, autoReport, activeHigh, ldpaBand, preamp,
    // bits: 8 x {enabled, source, sourceRxAnt, sourceTxAnt, sourceSlice,
    // output, band, lowMhz, highMhz, activeHigh, pttDependent, pttDelay,
    // txDelay}}; antennas and slices list the sources' choices.
    Q_INVOKABLE QVariantMap usbCables();
    // One setting as the radio's usb_cable command takes it (FlexLib's
    // names: type, enable, name, speed, data_bits, parity, stop_bits,
    // flow_control, source, source_rx_ant, source_tx_ant, source_slice,
    // auto_report, polarity, band, preamp); a bit's with its index 0-7.
    Q_INVOKABLE void setUsbCable(const QString& sn, const QString& key, const QString& value);
    Q_INVOKABLE void setUsbCableBit(const QString& sn, int bit, const QString& key, const QString& value);
    // Forgets an unplugged cable's settings on the radio.
    Q_INVOKABLE void removeUsbCable(const QString& sn);
    QVariantMap pgxl() const;
    QVariantMap tgxl() const;
    QVariantMap ag() const;
    // The antenna for an AG radio port (1 = A, 2 = B): RX, and TX where the
    // antenna may transmit on the port's band. Control on, not while that
    // port or the radio transmits.
    Q_INVOKABLE void agSelectAntenna(int port, int antenna);
    // An SO2R TGXL's active channel (1 = A, 2 = B): the one its meters and
    // Tune belong to ("activate ch=").
    Q_INVOKABLE void tgxlActivateChannel(int channel);
    // Each confirmed on the page; refused unless control is on (Radio Setup
    // › Amp), the tuner's relays and antenna also while the radio transmits.
    Q_INVOKABLE void pgxlSetOperate(bool on);
    Q_INVOKABLE void pgxlSetFanMode(const QString& mode);
    Q_INVOKABLE void tgxlSetOperate(bool on);
    Q_INVOKABLE void tgxlSetBypass(bool on);
    Q_INVOKABLE void tgxlTune();
    Q_INVOKABLE void tgxlAdjustRelay(int relay, int direction);
    Q_INVOKABLE void tgxlSetAntenna(int antenna);
    // device "pgxl" / "tgxl"; key "control" (bool), "address" (empty: the
    // radio's), "authCode" (the device's remote code, kept once accepted).
    Q_INVOKABLE void setGeniusSetting(const QString& device, const QString& key, const QVariant& value);
    QVariantMap apd() const;
    Q_INVOKABLE void setApdEnabled(bool on);
    Q_INVOKABLE void setApdSampler(const QString& txAntenna, const QString& port);
    Q_INVOKABLE void resetApdEqualizer();
    // The render tool's stand-in for a radio status line ("amplifier 0x1
    // model=PowerGeniusXL ip=... state=IDLE"): fed to the radio model as if
    // the radio had sent it, for devices the test radio does not have.
    void injectRadioStatus(const QString& line);
    Q_INVOKABLE void setRadioSetup(const QString& key, const QVariant& value);
    // Receive › Calibration's Start, as the desktop's: the cal frequency
    // set, freq_error_ppb cleared, radio pll_start, done when the radio's
    // pll_done goes 0 then 1 (calState: "", "starting", "running", "done",
    // "error", "timeout"; radioSetup() carries it).
    Q_INVOKABLE void startCalibration();
    // Radio › Restart the radio (asked first on the page): radio reboot, and
    // the app disconnects as the radio goes away.
    Q_INVOKABLE void rebootRadio();
    // The radio's transverters (Radio Setup › XVTR, as the desktop's tab):
    // index, name, rfMhz, ifMhz, loMhz, loErrorMhz, rxGain, maxPowerDbm,
    // maxPowerMin/Max (the radio's range for that IF), rxOnly, valid.
    // set keys: name, rf_freq, if_freq, lo_error, rx_gain, max_power,
    // rx_only.
    Q_INVOKABLE QVariantList transverters() const;
    Q_INVOKABLE void addTransverter();
    Q_INVOKABLE void setTransverter(int index, const QString& key, const QVariant& value);
    Q_INVOKABLE void removeTransverter(int index);
    // A radio with no callsign looks new: offer setup once per radio.
    Q_INVOKABLE bool setupSuggested() const;
    // First-run hints (HintsDialog): shown once; View › Hints shows them
    // again (hintsRequested).
    Q_PROPERTY(bool hintsShown READ hintsShown WRITE setHintsShown NOTIFY hintsChanged)
    bool hintsShown() const;
    void setHintsShown(bool shown);
    Q_INVOKABLE void showHints();
    Q_INVOKABLE void dismissSetupSuggestion();
    QString appVersion() const;
    QString manualUrl() const;
    // Writes the callsign to the radio (`radio callsign`, as SmartSDR's Radio
    // Setup) and keeps the client-side copy in step.
    Q_INVOKABLE void setCallsign(const QString& callsign);
    QString radioName() const { return m_radioName; }

    double frequencyMhz() const;
    void setFrequencyMhz(double mhz);
    QString mode() const;
    void setMode(const QString& mode);
    QStringList modes() const;
    double afGain() const;
    void setAfGain(double gain);
    bool muted() const;
    void setMuted(bool muted);
    double sMeterDbm() const { return m_sMeterDbm; }
    QString sMeterText() const;
    double panCenterMhz() const;
    double panBandwidthMhz() const;

    int filterLow() const;
    int filterHigh() const;
    // Passband widths (Hz) offered for the current mode.
    QList<int> filterPresets() const;
    int activeFilterPreset() const;
    int filterWidth() const;
    QStringList bands() const;
    bool bandsAvailable() const;
    QString lastAddress() const;
    QVariantList savedRadios() const;
    QVariantList connectionCheck() const { return m_check; }
    bool connectionCheckRunning() const { return m_checkRunning; }
    QString connectionCheckTarget() const { return m_checkTarget; }
    // Runs the doctor for the last radio tried (or an address given).
    Q_INVOKABLE void checkConnection(const QString& target = QString());
    // Entry `index` gets a new address (an IP address or host name, else
    // false) and the user's name for it (empty: the radio's own).
    Q_INVOKABLE bool editSavedRadio(int index, const QString& address, const QString& name);
    Q_INVOKABLE void removeSavedRadio(int index);

    // SmartLink (remote access through FlexRadio's service). State is
    // "signedOut", "signingIn", "signedIn" or "offline" (signed in, server
    // not reachable). The refresh token lives in the iOS Keychain.
    QString smartLinkState() const { return m_smartLinkState; }
    QString smartLinkAccount() const;
    QString smartLinkEmail() const;
    QVariantList smartLinkRadios() const;
    bool remoteSession() const;
    // Whether the operate page shows its control panel or gives the spectrum
    // the room (PTT always stays). Remembered on the device.
    bool controlsVisible() const;
    void setControlsVisible(bool visible);
    // The band row and the step buttons, or hidden behind B/S on the display
    // so the waterfall has their height (Larry Averbeck); remembered.
    bool tuneRowsShown() const;
    void setTuneRowsShown(bool shown);
    // The step buttons from the radio's STEP (±STEP, ×10, ×100), or 1 Hz to
    // 1 kHz (Larry Averbeck); remembered.
    bool radioStepButtons() const;
    void setRadioStepButtons(bool radio);

    // PTT is offered only when connected, the radio can transmit and its mic
    // input is this device (mic_selection=PC); the reason says which is missing.
    bool transmitAvailable() const;
    QString transmitUnavailableReason() const;
    bool canUseDeviceMicrophone() const;
    bool transmitting() const;
    bool nr() const;
    void setNr(bool on);
    bool nb() const;
    void setNb(bool on);
    bool anf() const;
    void setAnf(bool on);
    QStringList dspKeys() const;
    Q_INVOKABLE bool dspOn(const QString& key) const;
    Q_INVOKABLE void setDsp(const QString& key, bool on);
    // Levels of the radio's DSP (0-100), as the desktop's DSP popups: NR, NB,
    // ANF, APF and the 8000-series filters per slice, WNB per panadapter.
    Q_INVOKABLE bool dspHasLevel(const QString& key) const;
    Q_INVOKABLE int dspLevel(const QString& key) const;
    // The slider's top: 100, NRS 50 on firmware 4.3.16+ (the engine's).
    Q_INVOKABLE int dspLevelMaximum(const QString& key) const;
    Q_INVOKABLE void setDspLevel(const QString& key, int level);
    // Beat cancel (BC, client-side): remembered; runs in voice modes only.
    bool beatCancelSetting() const;
    bool beatCancelMode() const;
    void applyBeatCancel();
    // AGC by mode: when this app changes the mode (mode chooser, band
    // button, FT8 band, RADE), the slice takes the AGC kept for that kind of
    // mode: fast for data and CW, slow for voice, until the user picks
    // another one for it. Another client's mode changes are left alone.
    static QString agcModeClass(const QString& mode);
    void armModeAgc() { m_modeAgcUntilMs = QDateTime::currentMSecsSinceEpoch() + 4000; }
    void applyModeAgc();
    qint64 m_modeAgcUntilMs{0};
    QString agcMode() const;
    void setAgcMode(const QString& mode);
    QStringList agcModes() const;
    // RIT / XIT of the active slice, Hz (clamped to +/-9999 like the desktop).
    bool ritOn() const;
    void setRitOn(bool on);
    int ritHz() const;
    void setRitHz(int hz);
    bool xitOn() const;
    void setXitOn(bool on);
    int xitHz() const;
    void setXitHz(int hz);
    // Squelch is offered outside digital and CW modes (desktop rule).
    bool squelchAvailable() const;
    bool squelchOn() const;
    void setSquelchOn(bool on);
    int squelchLevel() const;
    void setSquelchLevel(int level);
    // Antennas of the active slice; lists come from the radio.
    QString rxAntenna() const;
    void setRxAntenna(const QString& antenna);
    QStringList rxAntennas() const;
    QString txAntenna() const;
    void setTxAntenna(const QString& antenna);
    QStringList txAntennas() const;
    // All slices: {id, letter, frequency, mode, active, tx, panId}.
    QVariantList slices() const;
    int activeSliceId() const;
    bool canAddSlice() const;
    bool canAddPanadapter() const;
    int panCount() const;
    // CW: straight key (hold), CWX text through the radio's keyer, speed,
    // pitch, break-in, and the receive decoder (ggmorse, CW modes only).
    bool cwMode() const;
    bool cwKeyAvailable() const;
    bool cwxAvailable() const;
    bool cwKeyDown() const { return m_cwKeyHeld || m_paddleKeyDown.load(std::memory_order_relaxed); }
    bool cwPaddles() const;
    void setCwPaddles(bool on);
    // A tap in CW with autotune on: tunes to the CW signal the display found
    // near the tap (SpectrumItem::cwPeakNear), or to the tapped point, to the
    // hertz rather than the step grid (which can be wider than the CW
    // filter): the slice on a CW signal hears it at the pitch. The radio's
    // own autotune (slice auto_tune) needs SmartSDR+ (the radio answers
    // 50004001 without it), so it is not used.
    Q_INVOKABLE void tuneToCw(double mhz, bool onSignal);
    // Autotune now (the CW tab's Autotune): the slice onto peakMhz, the
    // display's strongest CW signal in the passband (SpectrumItem::
    // cwPeakNear at the slice; <= 0: none found), where it is heard at the
    // pitch whatever the pitch is; with none, a notice. The decoder's pitch
    // is not used: it reports one from band noise too (stuck at its range's
    // bottom) and the offset needs the radio's pitch, which this radio
    // reports as 0.
    Q_INVOKABLE void cwAutotuneNow(double peakMhz);
    // The CW tab's Autotune pressed: the display answers with its peak in
    // the passband (cwAutotuneRequested -> cwAutotuneNow).
    Q_INVOKABLE void requestCwAutotune() { emit cwAutotuneRequested(); }
    QString cwKeyboardPaddles() const;
    void setCwKeyboardPaddles(const QString& keys);
    int cwIambicMode() const;
    void setCwIambicMode(int mode);
    bool nativeKeyboardSides() const;
    bool keyboardOnPage() const { return m_keyboardOnPage; }
    void setKeyboardOnPage(bool on);
    bool cwSwapPaddles() const;
    void setCwSwapPaddles(bool on);
    // A typed frequency in MHz (0 when it is none): with a decimal point
    // MHz if the radio can tune it there, else kHz ("14.074", "439.000" on a
    // transverter, "14074.5"); digits alone MHz on a transverter's band or
    // below 100, else kHz ("439", "7", "14074").
    Q_INVOKABLE double frequencyFromEntry(const QString& entry) const;
    QString activeBand() const;
    bool fmMode() const;
    QString repeaterDir() const;
    void setRepeaterDir(const QString& dir);
    double repeaterOffsetMhz() const;
    // The signed TX offset the slice carries (for the log and dev tests).
    double txOffsetMhz() const;
    void setRepeaterOffsetMhz(double mhz);
    bool ctcssOn() const;
    void setCtcssOn(bool on);
    QString ctcssTone() const;
    void setCtcssTone(const QString& tone);
    QStringList ctcssTones() const;
    // The paddles as held now; the keyer forms the elements.
    Q_INVOKABLE void setCwPaddleState(bool dit, bool dah);
    // Keyboard paddles' diagnostics to the app log (QML's console output
    // does not reach it).
    Q_INVOKABLE void logKeyboardPaddles(const QString& what);
    int cwSpeed() const;
    void setCwSpeed(int wpm);
    int cwPitch() const;
    void setCwPitch(int hz);
    bool cwBreakIn() const;
    void setCwBreakIn(bool on);
    bool cwSidetone() const;
    void setCwSidetone(bool on);
    int cwSidetoneGain() const;
    void setCwSidetoneGain(int gain);
    bool cwDecoderEnabled() const;
    bool rttyMode() const;
    bool rttyActive() const;
    QString rttyVia() const;
    // The RTTY tab's mode row: "RTTY" (the radio's RTTY mode, lower
    // sideband: the standard), "DIGL" or "DIGU" (AFSK, the decoder's
    // polarity following the sideband); FT8 / FT4 decoding stops for DIGx.
    Q_INVOKABLE void setRttyVia(const QString& via);
    bool rttyDecoderEnabled() const;
    void setRttyDecoderEnabled(bool on);
    QString rttyDecodedText() const { return m_rttyDecodedText; }
    QString rttyStrip() const { return m_rttyStrip; }
    qint64 rttyStripStart() const { return m_rttyStripStart; }
    QVariantMap rtty() const;
    // key: mark (Hz, 0 = the radio's), shift (Hz, 0 = the radio's), baud,
    // reverse (bool), squelch (bool). Remembered.
    Q_INVOKABLE void setRtty(const QString& key, const QVariant& value);
    Q_INVOKABLE void clearRttyText();
    // A tap in RTTY: onto the mark of the RTTY signal the display found near
    // the tap (SpectrumItem::rttyMarkNear), to the hertz; with none, the
    // tapped point on the step grid.
    Q_INVOKABLE void tuneToRtty(double mhz, bool onSignal);
    // The RTTY panel's Autotune: asks the display for the mark of the
    // signal in the passband (rttyAutotuneRequested), then
    // rttyAutotuneNow.
    Q_INVOKABLE void rttyAutotune();
    // The slice onto markMhz (<= 0: none found; with notify, a notice says
    // so).
    Q_INVOKABLE void rttyAutotuneNow(double markMhz, bool notify);
    // Tests (render tool rttyfeed): TEXT as RTTY audio at the decoder's
    // mark, shift and speed (Baudot, continuous-phase FSK, a little noise),
    // fed in real time to the running decoder.
    QVariantList textMacros() const;
    // Saved as given; the default's own name and text keep no setting, so
    // the macro follows the default.
    Q_INVOKABLE void setTextMacro(int index, const QString& label, const QString& text);
    // Its default {label, text} (fldigi's set, as bpsk31.com writes a QSO),
    // for the editor to show before Save.
    Q_INVOKABLE QVariantMap defaultTextMacro(int index) const;
    QStringList textCalls() const;
    bool textTransmitting() const { return m_textTransmitting; }
    // Transmit text in the mode received (RTTY: AFSK at the way's tones,
    // the mark the higher radio frequency; PSK: on the frequency it is
    // received on, as fldigi) from the slice, which must be the TX slice.
    // Sent while text is going out, it follows without unkeying. False (and
    // a notice) when it cannot go.
    Q_INVOKABLE bool sendText(const QString& text);
    Q_INVOKABLE bool sendTextMacro(int index);
    // What text would send now, its placeholders filled (the macro editor's
    // preview), or why it could not.
    Q_INVOKABLE QString previewText(const QString& text) const;
    // Stops at once, the rest of the queue dropped.
    Q_INVOKABLE void stopTextTx();
    void testRttyFeed(const QString& text);
    QString textMode() const;
    bool pskActive() const;
    bool textModeActive() const { return rttyActive() || pskActive(); }
    QVariantMap psk() const;
    // The Digi tab's mode row: RTTY (then Receive in picks the way), or
    // PSK31 / PSK63, which put the slice in DIGU and FT8 / FT4 off.
    Q_INVOKABLE void setTextMode(const QString& mode);
    // "carrier" (the audio frequency, Hz), "afc" (bool).
    Q_INVOKABLE void setPsk(const QString& key, const QVariant& value);
    // A tap on the display in PSK: the carrier there (mhz, the signal's
    // centre the display found, SpectrumItem::pskCentreNear, or the tap).
    Q_INVOKABLE void pskTap(double mhz);
    // Tests (render tool pskfeed): TEXT as a PSK signal at the decoder's
    // carrier plus offsetHz, fed to the running decoder.
    void testPskFeed(const QString& text, double offsetHz);
    bool locked() const;
    bool recordOn() const;
    bool playOn() const;
    bool playAvailable() const;
    int agcThreshold() const;
    void setAgcThreshold(int value);
    int rfGain() const;
    // The radio's FFT encoding for the log: "ypixels N, scale A..B dBm".
    QString panScaleInfo() const;
    void setRfGain(int gain);
    int rfGainLow() const;
    int rfGainHigh() const;
    int rfGainStep() const;
    QVariantList memories() const;
    QVariantList recentQsos() const;
    int qsoCount() const;
    QVariantMap callLookup() const { return m_callLookup; }
    QString lastSpotCall() const { return m_lastSpotCall; }
    QString qrzUser() const;
    QString qrzStatus() const;
    QString qrzStatusKind() const;
    QString myGrid() const;
    void setMyGrid(const QString& grid);
    // Looks the call up (debounced by the caller); the answer arrives in
    // callLookup. An empty call clears it.
    Q_INVOKABLE void lookupCallsign(const QString& call);
    // What a contact logged now would carry from the radio: freq (MHz), band,
    // mode, rst (59 or 599), power (W), utc ("HH:MM"), myCall, myGrid.
    Q_INVOKABLE QVariantMap qsoDefaults() const;
    // Logs a contact: fields from the form (call, rstSent, rstRcvd, comment,
    // sig, sigInfo, mySig, mySigInfo, timeOn "HHmm" optional); the radio's
    // frequency, band, mode and power and the lookup's name, QTH, grid and
    // country are added. True when it was written to the log file.
    Q_INVOKABLE bool logQso(const QVariantMap& fields);

    bool digiRunning() const;
    void setDigiRunning(bool on);
    QString digiProtocol() const;
    void setDigiProtocol(const QString& protocol);
    int digiTxHz() const { return m_digiTxHz; }
    void setDigiTxHz(int hz);
    int digiTxLevelDb() const { return m_digiTxLevelDb; }
    void setDigiTxLevelDb(int db);
    bool digiAutoCq() const { return m_digiAutoCq; }
    bool pskReporter() const { return m_pskReporterOn; }
    void setPskReporter(bool on);
    QString pskReporterStatus() const;
    void setDigiAutoCq(bool on);
    bool digiAutoAnswer() const { return m_digiAutoAnswer; }
    void setDigiAutoAnswer(bool on);
    int digiPresenceMin() const { return m_digiPresenceMin; }
    void setDigiPresenceMin(int minutes);
    bool digiHound() const { return m_digiHound; }
    void setDigiHound(bool on);
    bool digiHoundAllHz() const { return m_digiHoundAllHz; }
    void setDigiHoundAllHz(bool on);
    // Seconds left before the presence limit stops the automatic QSOs.
    Q_INVOKABLE int digiPresenceLeft() const;
    // A touch, a click or a key on the app: the operator is there.
    void noteOperator();
    QString digiCqParity() const { return m_digiCqParity; }
    void setDigiCqParity(const QString& parity);
    QString digiCqTarget() const { return m_digiCqTarget; }
    void setDigiCqTarget(const QString& target);
    QStringList digiCqTargets() const { return m_digiCqTargets; }
    bool digiCqDirectable() const;
    // Adds a choice (false if FT8 cannot carry it), or removes one.
    Q_INVOKABLE bool addDigiCqTarget(const QString& target);
    Q_INVOKABLE void removeDigiCqTarget(const QString& target);
    QVariantMap digi() const;
    QVariantList digiDecodes() const { return m_digiDecodes; }
    QString digiFilter() const { return m_digiFilter; }
    void setDigiFilter(const QString& filter);
    bool digiListScroll() const;
    void setDigiListScroll(bool on);
    QVariantList digiFrequencies() const;
    // Calls CQ in the next slot, and keeps calling until someone answers.
    Q_INVOKABLE void digiCallCq();
    // Tests: transmissions only simulated (no PTT, no audio: the QSO logic
    // runs against real decodes without RF), and decodes fed in by hand
    // (on the TX slice unless they say otherwise).
    void setDigiDryRun(bool on) { m_digiDryRun = on; }
    void digiInjectDecodes(QVariantList rows);
    // Answers the station of a decode (an index into digiDecodes).
    Q_INVOKABLE void digiAnswer(int index);
    // Stops transmitting at once and ends the QSO.
    Q_INVOKABLE void digiHalt();
    // Tunes to the protocol's dial frequency on a band, in DIGU.
    Q_INVOKABLE void digiTune(double mhz);
    // Fits the display to the audio band (4 kHz), for picking a TX tone.
    Q_INVOKABLE void digiZoom();
    // Moves the TX audio frequency to the clearest spot (200-2800 Hz) among
    // the stations heard in the last minute on the TX slice, in the slot CQ
    // goes out in when one is chosen; says where on the status line.
    Q_INVOKABLE void digiFindFreeTx();
    // Moves FT8 / FT4 to the active slice by making it the TX slice (a tap
    // in its passband, a band button).
    Q_INVOKABLE void digiBindActiveSlice();

    bool radeAvailable() const;
    bool radeActive() const;
    void setRadeActive(bool on);
    QVariantMap radeStatus() const;
    bool freedvReporter() const { return m_freedvReporterOn; }
    void setFreedvReporter(bool on);
    QString freedvMessage() const { return m_freedvMessage; }
    void setFreedvMessage(const QString& message);
    QString freedvStatus() const;
    QVariantList freedvStations() const { return m_freedvStations; }
    bool freedvAllBands() const { return m_freedvAllBands; }
    void setFreedvAllBands(bool all);
    // A FreeDV tab shown or hidden (the drawer's and the pinned panel's
    // counted apart): the station list is fetched only while one shows.
    Q_INVOKABLE void freedvViewer(bool shown);
    bool freedvLive() const;
    // Tunes to a listed station; RADE comes on when the station uses it.
    Q_INVOKABLE void freedvTune(double mhz, const QString& mode);
    // Contacts with this call before, and whether it is a dupe on this band
    // and mode: {worked, dupe}.
    Q_INVOKABLE QVariantMap workedInfo(const QString& call) const;
    // QRZ.com account for the lookup: the password goes to the platform's
    // secure store, never to the settings. Empty user removes it.
    Q_INVOKABLE void setQrzAccount(const QString& user, const QString& password);
    // Saves the logbook (an empty url turns it off; an empty key keeps the
    // saved one), checks the key and sends what is queued.
    Q_INVOKABLE void setOnlineLog(const QString& url, const QString& key, const QString& stationId);
    QVariantMap onlineLog() const;
    // Reading the logbook back (Wavelog 1.8 and newer): its QSOs, from every
    // station location, count as worked before. On reads it all, then at
    // each start only the new ones; off forgets them. onlineLogRead() reads
    // what is new now.
    Q_INVOKABLE void setOnlineLogRead(bool on);
    Q_INVOKABLE void onlineLogRead();
    // QRZ.com Logbook: the logbook's API key (an empty one keeps the saved
    // key) is checked, then each logged QSO goes there; clearQrzLog() stops
    // it all. Reading back as for Wavelog.
    Q_INVOKABLE void setQrzLogKey(const QString& key);
    Q_INVOKABLE void clearQrzLog();
    Q_INVOKABLE void setQrzLogRead(bool on);
    Q_INVOKABLE void qrzLogRead();
    QVariantMap qrzLog() const;
    // A shared ADIF file picked in the system's picker (iCloud Drive, Google
    // Drive, OneDrive...): read for worked before, each logged QSO added to
    // its end. clearSharedAdif() forgets it (the file stays); sharedAdifRead()
    // reads it again now.
    Q_INVOKABLE void setSharedAdif(const QUrl& url);
    Q_INVOKABLE void clearSharedAdif();
    Q_INVOKABLE void sharedAdifRead();
    QVariantMap sharedAdif() const;
    // "host" or "host:port" (52001 when not given); empty turns it off.
    Q_INVOKABLE void setLogger32(const QString& hostPort);
    QVariantMap logger32() const;
    // The amplifier's "host" or "host:port" (8080 when not given); empty
    // forgets it. A new address turns reading on.
    Q_INVOKABLE void setAmpAddress(const QString& hostPort);
    Q_INVOKABLE void setAmpOn(bool on);
    // Commands to the amplifier (off by default): only with this on does
    // anything but a reading reach it.
    Q_INVOKABLE void setAmpControl(bool on);
    // Each asks once (the page confirms first) and is refused while the
    // radio transmits; the amplifier's own readback shows the result.
    Q_INVOKABLE void ampSetOperate(bool operate);
    Q_INVOKABLE void ampSelectAntenna(int number);
    Q_INVOKABLE void ampResetError();
    // A tune carrier for the amplifier's own tuner (its API cannot start a
    // tune): STANDBY first, then the radio's TUNE at 4-39 W for up to 20 s,
    // kept on while the app is away (the amplifier's screen over VNC), off
    // once the amplifier has tuned. Asked on the page first.
    Q_INVOKABLE void ampTune();
    Q_INVOKABLE void ampTuneStop();
    QVariantMap amp() const;
    // Opens the share sheet with the log as an ADIF file
    // ("OksaSDR-log-<date>.adi"): mail, Files, AirDrop, a logbook app, or
    // TQSL for LoTW. False when there is nothing to share.
    Q_INVOKABLE bool shareQsoLog();
    // A tap felt in the hand (Haptics.h): "step" for a step button or − / +,
    // "press" and "release" for PTT.
    Q_INVOKABLE void haptic(const QString& kind);
    // Felt taps on or off in this app (View), on by default; the system's own
    // setting still applies when on.
    Q_PROPERTY(bool hapticsOn READ hapticsOn WRITE setHapticsOn NOTIFY hapticsChanged)
    // Radio Setup › Amp: the manufacturers chosen to show ("4o3a", "rfkit");
    // a maker with a device set up shows anyway (RadioSetupPage).
    Q_PROPERTY(QStringList ampMakersShown READ ampMakersShown NOTIFY ampMakersChanged)
    QStringList ampMakersShown() const;
    Q_INVOKABLE void toggleAmpMaker(const QString& maker);
    bool hapticsOn() const;
    void setHapticsOn(bool on);
    // Adds the contacts of an ADIF file (a file or content URL from the file
    // picker) to the log, skipping those already in it (call, date, time,
    // band, mode). Returns imported, duplicates and error.
    // Starts an import; adifImported(result) follows (imported, duplicates
    // or error).
    Q_INVOKABLE void importAdif(const QUrl& url);
    QString qsoLogLocation() const;
    // The 3D view fell behind the data on this GPU: back to the 2D
    // waterfall, with a notice.
    void disable3DTooSlow();
    // Marks the 3D view running in front, for the hang check at start.
    void update3DGuard();
    Q_INVOKABLE void recallMemory(int index);
    Q_INVOKABLE void saveMemory(const QString& name);
    Q_INVOKABLE void removeMemory(int index);
    // Changes a memory's name, frequency (MHz) and mode; other fields stay.
    // dir "simplex"/"down"/"up" (empty: unchanged), offsetMhz < 0 unchanged,
    // ctcss < 0 unchanged (0 off, 1 on), tone empty: unchanged.
    Q_INVOKABLE void editMemory(int index, const QString& name, double freqMhz, const QString& mode,
                                const QString& dir = QString(), double offsetMhz = -1.0, int ctcss = -1,
                                const QString& tone = QString());
    bool memoriesWritable() const;
    QVariantList tnfs() const;
    bool tnfEnabled() const;
    void setTnfEnabled(bool on);
    bool tnfAvailable() const;
    bool canAddTnf() const;
    bool tnfHasDepth() const;
    // A notch at freqMhz, or at the passband's centre when 0 (as the
    // desktop's +TNF).
    Q_INVOKABLE void addTnf(double freqMhz = 0.0);
    // A notch confirmed in the display's notch panel (a hold on the display
    // or + TNF opens it, nothing is sent until Add): created at freqMhz, then
    // given the panel's width, depth and permanent once the radio reports it
    // (a Flex creates a notch with its frequency only).
    Q_INVOKABLE void addTnfWith(double freqMhz, int widthHz, int depth, bool permanent);
    // + TNF: the display's notch panel at the passband's centre
    // (tnfCreateRequested), to confirm there.
    Q_INVOKABLE void requestTnfCreate();
    // key: freq (MHz), width (Hz), depth (1-3) or permanent (bool).
    Q_INVOKABLE void setTnf(int id, const QString& key, const QVariant& value);
    Q_INVOKABLE void removeTnf(int id);
    QVariantList spots() const;
    bool showSpots() const;
    void setShowSpots(bool on);
    QString spotModes() const;
    void setSpotModes(const QString& modes);
    int spotMaxAgeMin() const;
    void setSpotMaxAgeMin(int minutes);
    QVariantMap dxCluster() const;
    QString dxClusterStatus() const;
    QString dxClusterStatusKind() const;
    // key: host, port, callsign or autoConnect.
    Q_INVOKABLE void setDxCluster(const QString& key, const QVariant& value);
    Q_INVOKABLE void connectDxCluster();
    Q_INVOKABLE void disconnectDxCluster();
    // Tunes the active slice to a spot (and its mode, as the desktop), and
    // tells the radio it was chosen (for loggers).
    Q_INVOKABLE void tuneToSpot(int index);
    // Development (--spot-test): "add" three test spots near the slice on
    // the radio (source OksaSDR-test, 5 min), "clear" removes them.
    void spotTest(const QString& action);
    // Development (--radio-commands): send one command and log the radio's reply.
    void devCommand(const QString& command);
    // Development (render tool "worked"): how a call stands as a CQ on a band
    // and mode: the FTx highlight and New call, the Log tab's worked count
    // and dupe; and the logbook read-back's state.
    QString workedAudit(const QString& call, const QString& band, const QString& modeKey);
    // Development (render tool "qrzinsert"): one test record for CALL to
    // the QRZ.com Logbook queue only (not the app's log, not Wavelog).
    void devQrzInsert(const QString& call);
    // Development (render tool "shared insert="): a test record for CALL to
    // the shared ADIF file only.
    void devSharedInsert(const QString& call);
    // Development (--cwx-test): the CWX sidetone's key-downs and keyed time
    // since the last CWX text started.
    QString cwxSidetoneStats() const;
    int paddleElements() const { return m_paddleElements.load(std::memory_order_relaxed); }
    QStringList globalProfiles() const;
    QString activeGlobalProfile() const;
    QStringList txProfiles() const;
    QString activeTxProfile() const;
    QStringList micProfiles() const;
    QString activeMicProfile() const;
    Q_INVOKABLE void loadProfile(const QString& type, const QString& name);
    // Global: saves (overwrites) under the name. Transmit/mic: creates a new
    // profile from the current settings (the radio cannot overwrite those).
    Q_INVOKABLE void saveProfile(const QString& type, const QString& name);
    Q_INVOKABLE void deleteProfile(const QString& type, const QString& name);
    QVariantMap agcAuto() const;
    // The AGC panel's Auto: the radio's Auto on / off, or the app's
    // measurement started (pressed again: stopped, AGC-T put back).
    Q_INVOKABLE void agcAutoPress();
    QVariantMap midi() const;
    QString midiActivity() const { return m_midiActivity; }
    bool bigButtons() const { return m_bigButtons; }
    void setBigButtons(bool on);
    bool showDemoRadio() const;
    void setShowDemoRadio(bool on);
    int lanRadioCount() const;
    // Radio Setup › MIDI lists the ports again while it is shown.
    Q_INVOKABLE void midiRefresh();
    Q_INVOKABLE void midiOpen(const QString& port);
    Q_INVOKABLE void midiClose();
    // Learn: the next control moved or pressed on the controller is bound to
    // this function (a knob on the VFO as a relative one).
    Q_INVOKABLE void midiLearn(const QString& paramId);
    Q_INVOKABLE void midiCancelLearn();
    Q_INVOKABLE void midiUnbind(const QString& paramId);
    Q_INVOKABLE void midiClearAll();
    // A SmartSDR ".map" (Lynovation's files for SmartSDR for iOS / macOS) or
    // the desktop's XML profile; replaces the bindings.
    Q_INVOKABLE void midiImport(const QUrl& url);
    // iPhone / iPad: iOS's Bluetooth MIDI pairing sheet.
    Q_INVOKABLE void midiPair();
    Q_INVOKABLE void setMidiAutoConnect(bool on);
    Q_INVOKABLE void setMidiTransmit(bool on);
    // The display's noise floor (SpectrumItem.noiseFloorDbm): the
    // measurement's quiet-spot check compares the S meter with it.
    Q_INVOKABLE void noteSpectrumFloor(double dbm);
    QStringList appSetups() const;
    QString activeAppSetup() const;
    // Each returns {ok: bool, error: text}; apply also {later: the parts that
    // change at the next start}, import {name}.
    Q_INVOKABLE QVariantMap saveAppSetup(const QString& name);
    Q_INVOKABLE QVariantMap applyAppSetup(const QString& name);
    Q_INVOKABLE QVariantMap deleteAppSetup(const QString& name);
    Q_INVOKABLE QVariantMap exportAppSetup(const QString& name);
    Q_INVOKABLE QVariantMap importAppSetup(const QUrl& url);
    int txFilterLow() const;
    int txFilterHigh() const;
    void setTxFilterLow(int hz);
    void setTxFilterHigh(int hz);
    QStringList eqBandLabels() const;
    bool eqTxEnabled() const;
    bool eqRxEnabled() const;
    QVariantList eqTxBands() const;
    QVariantList eqRxBands() const;
    Q_INVOKABLE void setEqEnabled(bool tx, bool on);
    Q_INVOKABLE void setEqBand(bool tx, int band, int dB);
    Q_INVOKABLE void resetEq(bool tx);
    QStringList cwMemories() const;
    Q_INVOKABLE void setCwMemory(int index, const QString& text);
    Q_INVOKABLE void sendCwMemory(int index);
    void setLocked(bool on);
    Q_INVOKABLE void toggleRecord();
    Q_INVOKABLE void togglePlay();
    bool wfAutoLevel() const { return displayInt("MobileWfAuto", 1) != 0; }
    void setWfAutoLevel(bool on) { setDisplayInt("MobileWfAuto", on ? 1 : 0); }
    bool displayButtonsRight() const { return displayInt("MobileDisplayButtonsRight", 0) != 0; }
    void setDisplayButtonsRight(bool on) { setDisplayInt("MobileDisplayButtonsRight", on ? 1 : 0); }
    int wfBlackLevel() const { return displayInt("MobileWfBlack", 50); }
    int spectrumSplit() const { return displayInt("MobileSpectrumSplit", 0); }
    void setSpectrumSplit(int percent) { setDisplayInt("MobileSpectrumSplit", percent <= 0 ? 0 : std::clamp(percent, 10, 90)); }
    void setWfBlackLevel(int v) { setDisplayInt("MobileWfBlack", std::clamp(v, 0, 100)); }
    int wfColorGain() const { return displayInt("MobileWfGain", 50); }
    void setWfColorGain(int v) { setDisplayInt("MobileWfGain", std::clamp(v, 0, 100)); }
    bool traceAutoRange() const { return displayInt("MobileTraceAuto", 1) != 0; }
    void setTraceAutoRange(bool on) { setDisplayInt("MobileTraceAuto", on ? 1 : 0); }
    int traceRefDbm() const { return displayInt("MobileTraceRef", -40); }
    void setTraceRefDbm(int v) { setDisplayInt("MobileTraceRef", std::clamp(v, -140, 0)); }
    int traceRangeDb() const { return displayInt("MobileTraceRange", 90); }
    void setTraceRangeDb(int v) { setDisplayInt("MobileTraceRange", std::clamp(v, 20, 150)); }
    bool spectrum3D() const { return displayInt("MobileSpectrum3D", 0) != 0; }
    void setSpectrum3D(bool on) { setDisplayInt("MobileSpectrum3D", on ? 1 : 0); }
    // The 3D floor (its zero) this many dB under the noise, as the desktop's
    // "3D Floor" (0-24, default 6).
    int spectrum3DFloorDepth() const { return displayInt("Mobile3DFloorDepth", 6); }
    void setSpectrum3DFloorDepth(int v) { setDisplayInt("Mobile3DFloorDepth", std::clamp(v, 0, 24)); }
    // The desktop's 3D Gain (0-100, default 70): the 3D colours' gamma.
    int spectrum3DGain() const { return displayInt("Mobile3DGain", 70); }
    void setSpectrum3DGain(int v) { setDisplayInt("Mobile3DGain", std::clamp(v, 0, 100)); }
    // 3D row smoothing: 0 off (raw rows, as live as the 2D trace), 1 light
    // (a light blend with the previous row only; narrow dips stay), 2 the
    // desktop's (median of three rows, column pass, 40 % previous row).
    int spectrum3DSmoothing() const { return displayInt("Mobile3DSmoothing", 1); }
    void setSpectrum3DSmoothing(int v) { setDisplayInt("Mobile3DSmoothing", std::clamp(v, 0, 2)); }
    bool analogMeters() const { return analogLeft() && analogRight(); }
    void setAnalogMeters(bool on) { setMeterAnalog(QStringLiteral("left"), on); setMeterAnalog(QStringLiteral("right"), on); }
    // side "left" / "right" (or their lower bars "left2" / "right2").
    QString meterStyle(const QString& side) const;
    Q_INVOKABLE void setMeterStyle(const QString& side, const QString& style);
    QString meterStyleLeft() const { return meterStyle(QStringLiteral("left")); }
    QString meterStyleRight() const { return meterStyle(QStringLiteral("right")); }
    bool analogLeft() const { return meterStyleLeft() == QLatin1String("analog"); }
    bool analogRight() const { return meterStyleRight() == QLatin1String("analog"); }
    Q_INVOKABLE void setMeterAnalog(const QString& side, bool on)
    {
        setMeterStyle(side, on ? QStringLiteral("analog") : QStringLiteral("bar"));
    }
    int meterPeakHold() const { return std::clamp(displayInt("MobileMeterPeakHold", 2), 0, 3); }
    void setMeterPeakHold(int mode) { setDisplayInt("MobileMeterPeakHold", std::clamp(mode, 0, 3)); }
    QStringList pinnedSections() const;
    void setPinnedSections(const QStringList& sections);
    void setCwDecoderEnabled(bool on);
    void markCwDecodedAway(bool away);
    QString cwDecodedText() const { return m_cwDecodedText; }
    QString cwStrip() const { return m_cwStrip; }
    QString workedCall() const { return m_workedCall; }
    void setWorkedCall(const QString& call)
    {
        const QString c = call.trimmed().toUpper();
        if (c != m_workedCall) {
            m_workedCall = c;
            m_workedCallSince = c.isEmpty() ? QDateTime() : QDateTime::currentDateTimeUtc();
            emit workedCallChanged();
        }
    }
    // When the call being worked was picked (HHmm UTC), the QSO's start for
    // the Digi tab's Log (it logged the moment Log was pressed, minutes
    // after the QSO began); empty after an hour or with no call.
    Q_INVOKABLE QString workedCallTimeOn() const
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        return m_workedCallSince.isValid() && m_workedCallSince.secsTo(now) < 3600
                   ? m_workedCallSince.toString(QStringLiteral("HHmm")) : QString();
    }
    qint64 cwStripStart() const { return m_cwStripStart; }
    void appendCwDecoded(const QString& text);
    QStringList cwCalls() const;
    QStringList heardCallsIn(const QString& text) const;
    // Development (--cw-text): text as if the CW decoder had received it.
    // Development: QSOs printed to the app log, not written or sent.
    void setQsoLogDryRun(bool on) { m_qsoLogDryRun = on; }
    void addCwDecodedForTest(const QString& text)
    {
        appendCwDecoded(text);
        emit cwDecoderChanged();
    }
    // Development (render tool `status`): a status line as if the radio
    // had sent it, for hardware not at hand (a USB cable).
    void injectStatusForTest(const QString& object, const QMap<QString, QString>& kvs)
    {
        m_session.radioModel().handleStatusForTest(object, kvs);
    }
    int cwDecoderSensitivity() const;
    void setCwDecoderSensitivity(int sens);
    // Development: letters shown / dropped by the sensitivity since start.
    QString cwDecoderCounts() const
    {
        QStringList bins;
        for (int b : m_cwCostBins) {
            bins << QString::number(b);
        }
        return QStringLiteral("%1 shown, %2 dropped, cost tenths [%3]").arg(m_cwShown).arg(m_cwDropped).arg(bins.join(QLatin1Char(' ')));
    }
    QString cwDecoderStats() const { return m_cwDecoderStats; }
    // The active slice's tuning step (radio-reported), Hz.
    int stepHz() const;
    QList<int> stepList() const;
    // The operator's step for the active slice (the desktop's STEP control).
    Q_INVOKABLE void setStepHz(int hz);
    QVariantList stations() const;
    QVariantMap stationConflict() const { return m_stationConflict; }
    // Ends another station's session on the radio (multiFLEX), freeing its
    // slices and panadapters. Never this app's own.
    Q_INVOKABLE void disconnectStation(int handle);
    Q_INVOKABLE void requestStations() { emit stationsRequested(); }
    // what: "gain" (0-100), "pan" (0-100, 50 centre) or "mute" (0/1).
    Q_INVOKABLE void setSliceAudio(int sliceId, const QString& what, double value);
    QVariantMap phone() const;
    Q_INVOKABLE void setPhone(const QString& key, int value);
    // MOX: transmit latched on until pressed again (the desktop's MOX).
    Q_INVOKABLE void toggleMox();
    QVariantList dvkSlots() const;
    QString dvkState() const;
    bool dvkOnRadio() const;
    // A SmartSDR+ feature the radio's license status allows (true until the
    // radio has reported it).
    bool radioFeatureLicensed(const QString& feature) const;
    int dvkActiveId() const;
    // Tap again to stop. Play transmits; record and preview do not.
    Q_INVOKABLE void dvkPlay(int id);
    Q_INVOKABLE void dvkRecord(int id);
    Q_INVOKABLE void dvkPreview(int id);
    Q_INVOKABLE void dvkStop();
    Q_INVOKABLE void dvkClear(int id);
    Q_INVOKABLE void dvkRename(int id, const QString& name);
    bool macroRepeat() const { return m_macroRepeat; }
    void setMacroRepeat(bool on);
    int macroRepeatGap() const;
    void setMacroRepeatGap(int seconds);
    QString macroRepeatSlot() const;
    double macroRepeatDueAt() const { return m_repeatDueAt; }
    // Ends a running repeat (the switch stays as it is).
    Q_INVOKABLE void cancelMacroRepeat(const QString& reason);
    // Radio RF power setting (Flex rfpower, 0..100).
    int rfPower() const;
    void setRfPower(int power);
    // Client-side gain on this device's microphone, 0..100 (PcMicGain).
    double micGainDb() const;
    void setMicGainDb(double db);
    double micLevelDbfs() const { return m_micLevelDbfs; }
    double micPeakDbfs() const { return m_micPeakDbfs; }
    bool micMeterLive() const;
    // A shown TX tab holds this device's microphone open, so its Mic meter
    // moves before transmitting and the level can be set (as the desktop and
    // SmartSDR, which meter the mic in receive too). Nothing is transmitted:
    // the radio keys only on PTT. Not while VOX is on (speech would key it).
    Q_INVOKABLE void holdMicCheck(QObject* holder, bool on);
    QVariantMap txChain() const;
    Q_INVOKABLE void setTxChain(const QString& key, const QVariant& value);
    double compReductionDb() const;
    double forwardPowerW() const { return m_forwardPowerW; }
    int ratedPowerW() const;
    double swr() const { return m_swr; }
    bool swrValid() const { return m_swrValid; }
    // Radio actually on the air (MOX, tune, ATU, VOX); TX-only meters show
    // a value only then.
    bool radioTransmitting() const;
    bool rfPowerIsPercent() const;
    // TX power for the log (ADIF TX_PWR), in watts, 0 when unknown: the last
    // transmission's measured peak (the amplifier's when a linked one is in
    // Operate), else the setting where it is watts.
    int loggedTxPowerW() const;
    // The radio's "Show TX in waterfall" (transmit show_tx_in_waterfall, as
    // SmartSDR's): this app draws its own transmission there only when on.
    bool showTxInWaterfall() const;
    double compressionDb() const { return m_compressionDb; }
    double alcDbfs() const { return m_alcDbfs; }
    bool alcAvailable() const { return m_alcAvailable; }
    double paTempC() const { return m_paTempC; }
    bool hasPaTemp() const;
    double supplyVolts() const { return m_supplyVolts; }
    bool hasSupplyVolts() const;
    // Selectable meters for the two meter faces, remembered on the device.
    QStringList meterKinds() const;
    // The kinds one meter offers: Power, SWR, ALC and Comp only in transmit
    // (the radio reads them only then), Mic only on the left meter (the tablet's side
    // panel, where the TX tab is, covers the right one).
    Q_INVOKABLE QStringList meterKindsFor(const QString& side, bool tx) const;
    QString meterLeft() const;
    void setMeterLeft(const QString& kind);
    QString meterRight() const;
    void setMeterRight(const QString& kind);
    QString meterLeftTx() const { return meterChoice("MobileMeterLeftTx", "SWR"); }
    QString meterRightTx() const { return meterChoice("MobileMeterRightTx", "Power"); }
    // The lower bars: by default what the other faces do not show.
    QString meterLeft2() const;
    QString meterLeft2Tx() const { return meterChoice("MobileMeterLeft2Tx", "Mic"); }
    QString meterRight2() const;
    QString meterRight2Tx() const { return meterChoice("MobileMeterRight2Tx", "ALC"); }
    QString meterBig1() const { return meterChoice("MobileMeterBig1", "S"); }
    QString meterBig1Tx() const { return meterChoice("MobileMeterBig1Tx", "Power"); }
    QString meterBig2() const { return meterChoice("MobileMeterBig2", "Volts"); }
    QString meterBig2Tx() const { return meterChoice("MobileMeterBig2Tx", "SWR"); }
    // side "left"/"right" (the lower bars "left2"/"right2", Big buttons'
    // "big1"/"big2"), tx: the transmit choice instead of the receive one.
    Q_INVOKABLE void setMeter(const QString& side, bool tx, const QString& kind);

    // Tune carrier and the radio's ATU. Both transmit, so both go through
    // the local TxController (Activity::Tune / ::Atu) like PTT.
    bool tuning() const;
    bool tuneAvailable() const;
    int tunePower() const;
    void setTunePower(int power);
    bool hasTuner() const;
    bool hasTunerMemories() const;
    QString atuStatusText() const;
    bool atuTuned() const;
    bool atuMemories() const;
    void setAtuMemories(bool on);

    // Radio-side TX audio: speech processor (PROC), monitor, mic level.
    bool procEnabled() const;
    void setProcEnabled(bool on);
    int procLevel() const;
    void setProcLevel(int level);
    int procLevelMax() const;
    bool monitor() const;
    void setMonitor(bool on);
    bool dax() const;
    void setDax(bool on);
    int monitorGain() const;
    void setMonitorGain(int gain);
    int radioMicLevel() const;
    void setRadioMicLevel(int level);

    Q_INVOKABLE void connectToRadio(int row);
    // A FlexRadio by address or host name, for networks where discovery
    // broadcasts do not arrive (iOS needs a multicast entitlement to receive
    // them), a VPN, or a dynamic DNS name.
    Q_INVOKABLE void connectToAddress(const QString& host);
    Q_INVOKABLE void signInSmartLink(const QString& email, const QString& password);
    Q_INVOKABLE void signOutSmartLink();
    // Connect to the radio at `index` in smartLinkRadios.
    Q_INVOKABLE void connectSmartLinkRadio(int index);
    // Answer to stationConflict: disconnect that station and connect, or 0
    // to cancel the connect.
    Q_INVOKABLE void resolveStationConflict(int handle);
    // Answer to certificateChanged: trust the radio's new certificate or not.
    Q_INVOKABLE void answerCertificate(bool accept);
    // Development aid (--demo): connect once the simulator entry appears.
    void connectToDemoWhenListed();
    Q_INVOKABLE void disconnectRadio();
    // Hold-to-talk: true keys through the local TxController, false releases
    // that same input. Every disconnect or loss of foreground also releases.
    // `reason` names the release in the log ("released", "touch cancelled").
    Q_INVOKABLE void setPtt(bool down, const QString& reason = QString());
    // Tune carrier on/off (latched, like the desktop TUNE button).
    Q_INVOKABLE void toggleTune();
    // ATU: start a tune cycle, or bypass when already tuned on this frequency.
    Q_INVOKABLE void atuPressed();
    // Ends every transmission this client keyed (PTT, tune, ATU cycle);
    // keepAmpTune leaves the amplifier's tune carrier (ampTune) running.
    Q_INVOKABLE void stopTransmit(const QString& reason, bool keepAmpTune = false);
    // Switch the radio's transmit audio input to this device's microphone.
    Q_INVOKABLE void useDeviceMicrophone();
    // The radio's own microphone input: the last one used before PC, else MIC.
    Q_INVOKABLE void useRadioMicrophone();
    bool audioOnRadio() const;
    void setAudioOnRadio(bool radio);
    bool audioRouteSelectable() const;
    bool canUseRadioMicrophone() const;
    // Dev/screenshot runs: leave the radio's mic input alone.
    void setMicDefaultEnabled(bool enabled) { m_micDefaultEnabled = enabled; }
    // Screenshots for public pages: the radio's serial number reads "(hidden)".
    void setRedactIdentity(bool on) { m_redactIdentity = on; }
    // Tune, recentring the pan when the target lies outside it.
    Q_INVOKABLE void tuneTo(double mhz);
    // A typed frequency: tuned, and the view centred on the passband (the
    // user, 2026-11-07: a chosen frequency puts the filter in the middle).
    Q_INVOKABLE void tuneToCentred(double mhz);
    // A drag on the display: no centring after it.
    Q_INVOKABLE void cancelCentring();
    // Band change through the radio's band stack (Flex), which restores the
    // last frequency and mode used on that band.
    Q_INVOKABLE void selectBand(const QString& band);
    Q_INVOKABLE void setFilterWidth(int widthHz);
    // Both filter edges (Hz from the carrier), as a drag of an edge on the
    // display sets them; at least 50 Hz apart.
    Q_INVOKABLE void setFilterEdges(int lowHz, int highHz);
    Q_INVOKABLE void selectSlice(int sliceId);
    Q_INVOKABLE void addSlice();
    // Split on: a TX slice on the same pan, 1 kHz up in CW, 5 kHz up
    // otherwise, same mode, muted, takes transmit and focus (to tune the TX
    // offset); the RX slice keeps listening. Off: transmit back to the RX
    // slice and the TX slice closes.
    Q_INVOKABLE void toggleSplit();
    // Split with the TX slice this many kHz up (the desktop's Split Up 1/5/10);
    // 0 = the mode's default (1 kHz CW, 5 kHz voice). Off when split is on.
    Q_INVOKABLE void toggleSplitUp(int khz);
    // The pan's display settings, as the desktop's overlay menu: average
    // (0-100), fps (5-60), wfRate (1-100, the waterfall's speed), bandZoom.
    QVariantMap panDisplay() const;
    Q_INVOKABLE void setPanDisplay(const QString& key, int value);
    bool splitActive() const;
    QString splitMethod() const;
    void setSplitMethod(const QString& method);
    bool splitAvailable() const;
    Q_INVOKABLE void removeSlice(int sliceId);
    Q_INVOKABLE void makeTxSlice(int sliceId);
    // A new panadapter (with its own slice, created by the radio's defaults).
    Q_INVOKABLE void addPanadapter();
    // Closes this client's panadapters other than the one shown (and the
    // slices on them).
    Q_INVOKABLE void closeOtherPanadapters();
    // Straight key: down while held. Every release path also keys up.
    Q_INVOKABLE void setCwKey(bool down, const QString& reason = QString());
    // True when the radio's keyer took the text.
    Q_INVOKABLE bool sendCwx(const QString& text);
    // Live mode: one character to the keyer as it is typed.
    Q_INVOKABLE void sendCwxChar(const QString& ch);
    Q_INVOKABLE void stopCwx();
    Q_INVOKABLE void clearCwDecoded();
    // Tune by whole tuning steps, landing on the step grid.
    Q_INVOKABLE void tuneBySteps(int steps);
    // Step button: move by hz, landing on a multiple of |hz|. Off the grid
    // the first press only reaches the next grid point in that direction
    // (14.100002 +10 Hz -> 14.100010, -10 Hz -> 14.100000).
    Q_INVOKABLE void tuneByHz(int hz);
    // Tune to a point on the display, rounded to the tuning step.
    Q_INVOKABLE void tuneToSnapped(double mhz);
    // Move the view (pan centre) without retuning; coalesced so a drag sends
    // the radio at most one request per kPanMoveMs.
    Q_INVOKABLE void panTo(double centerMhz);
    // Multiply the pan span by factor (<1 zooms in), centred on the slice.
    Q_INVOKABLE void zoom(double factor);
    // The spectrum view reports its size so the radio sends matching frames.
    Q_INVOKABLE void setPanPixels(int width, int height);

signals:
    void fmChanged();
    void bandsChanged();
    void keyboardOnPageChanged();
    void connectionStateChanged();
    void lastErrorChanged();
    void callsignChanged();
    void bandPlanChanged();
    void hapticsChanged();
    void ampMakersChanged();
    void hintsChanged();
    void hintsRequested();
    void bandPlanRegionChanged();
    void buttonStyleChanged();
    void textScaleChanged();
    void pttToggleChanged();
    void txLockChanged();
    void audioCompressionChanged();
    void networkChanged();
    void performanceChanged();
    void frequencyChanged();
    void modeChanged();
    void modesChanged();
    void afGainChanged();
    void mutedChanged();
    void sMeterChanged();
    void panGeometryChanged();
    void transmittingChanged();
    void workedCallChanged();
    void transmitStateChanged();
    void audioRouteChanged();
    void filterChanged();
    void lastAddressChanged();
    void savedRadiosChanged();
    void connectionCheckChanged();
    void controlsVisibleChanged();
    void tuneRowsChanged();
    void smartLinkChanged();
    void smartLinkRadiosChanged();
    // The radio presented a different TLS certificate than the one pinned
    // for it; the connect waits for answerCertificate().
    void certificateChanged(const QString& host, const QString& expected, const QString& presented);
    void rfPowerChanged();
    void micGainChanged();
    void txMetersChanged();
    void ratedPowerChanged();
    void dspChanged();
    void meterSelectionChanged();
    void tuneStateChanged();
    void atuChanged();
    void txAudioChanged();
    void stepChanged();
    void ritXitChanged();
    void squelchChanged();
    void antennaChanged();
    void slicesChanged();
    void stationsChanged();
    void stationConflictChanged();
    void phoneChanged();
    void panDisplayChanged();
    void dvkChanged();
    void macroRepeatChanged();
    // The connected-stations list was asked for (Radio Setup); the operate
    // page opens it.
    void stationsRequested();
    void cwChanged();
    void cwKeyDownChanged();
    void cwDecoderChanged();
    void rttyDecoderChanged();
    void textMacrosChanged();
    void textTxChanged();
    void displayLevelsChanged();
    void lockedChanged();
    void recordPlayChanged();
    void rfGainChanged();
    void memoriesChanged();
    void qsoLogChanged();
    void adifImported(const QVariantMap& result);
    void digiChanged();
    void radeChanged();
    void freedvChanged();
    void freedvStationsChanged();
    void digiDecodesChanged();
    void cwAutotuneRequested();
    // To the display: the RTTY signal's mark near nearMhz (within halfHz),
    // answered with rttyAutotuneNow. Autotune, and a tap on an RTTY spot
    // (spots give the mark, or the signal's centre).
    void rttyAutotuneRequested(double nearMhz, double halfHz, bool notify);
    // To the display: the centre of the PSK signal near nearMhz (within
    // halfHz), answered with pskTap (a PSK spot, once the display shows it).
    void pskCentreRequested(double nearMhz, double halfHz);
    void digiFilterChanged();
    void digiListScrollChanged();
    void callLookupChanged();
    void lastSpotCallChanged();
    void qrzChanged();
    void onlineLogChanged();
    void qrzLogChanged();
    void sharedAdifChanged();
    void logger32Changed();
    void ampChanged();
    void pgxlChanged();
    void tgxlChanged();
    void agChanged();
    void apdChanged();
    void tnfsChanged();
    void tnfCreateRequested(double freqMhz);
    void txChainChanged();
    void spotsChanged();
    void dxClusterChanged();
    void dxClusterStatusChanged();
    void profilesChanged();
    void appSetupsChanged();
    void agcAutoChanged();
    void midiChanged();
    void bigButtonsChanged();
    void showDemoRadioChanged();
    void lanRadiosChanged();
    void midiActivityChanged();
    void txFilterChanged();
    void eqChanged();
    void cwMemoriesChanged();
    void tuneBlocked();
    // Frames of the active panadapter only. Spectrum bins are dBm and span
    // the pan. Waterfall rows are intensities on a backend-dependent scale
    // covering lowMhz..highMhz, which need not equal the pan's range.
    void spectrumFrame(const QVector<float>& binsDbm);
    void waterfallRow(const QVector<float>& bins, double lowMhz, double highMhz);

private:
    void startAudio();
    void stopAudio();
    void wireRxAudio();
    void onConnectionStateChanged(bool connected);
    void onSliceAdded(SliceModel* slice);
    void selectActiveSlice();
    void bindSlice(SliceModel* slice);
    void bindPan(PanadapterModel* pan);
    void defaultMicToDevice();
    QString meterChoice(const char* key, const char* fallback) const;
    int displayInt(const char* key, int fallback) const;
    void setDisplayInt(const char* key, int value);
    // Sends the pan's size to the radio; once per pan and width (connect and
    // slice selection used to send it five or six times), always when the
    // radio asks (panDimensionsNeeded: it reset to defaults).
    void sendPanDimensions(const QString& panId, bool force = false);
    QString m_sentPanId;
    int m_sentPanWidth{0};
    qint64 m_sentPanSizeMs{0};
    void setConnectionState(const QString& state);
    void setLastError(const QString& error);
    void connectToHostAddress(const QString& entered, const QHostAddress& address);
    void connectWithInfo(const RadioInfo& info, const QString& displayName);
    void wireTransmit();
    void wireTxPacketSink();
    void applyTxAudioCapabilities(bool connected, const RadioCapabilities& caps);
    bool ensureMicrophonePermission();
    void startDeviceMicrophoneCapture();
    void stopDeviceMicrophoneCapture(bool force = false);
    void updateMicCheck();
    void releasePtt(const QString& reason);
    void duckReceive(bool on, const char* why);
    void wireSmartLink();
    void wireCw();
    void updateCwDecoder();
    void updateRttyDecoder();
    // Runs the PSK decoder (run) at the chosen speed and carrier, or stops it.
    void updatePskDecoder(bool run);
    void appendRttyDecoded(const QString& text);
    void appendRttyGated(const QString& text, float conf);
    void resetRttyGate();
    // The radio frequency of the received RTTY signal's mark (tuneToRtty the
    // other way round); 0 when RTTY is not being received.
    double rttyMarkMhz() const;
    // The slice frequency that puts a signal's mark (markMhz) on the tones
    // of a way of receiving RTTY ("RTTY", "DIGL", "DIGU").
    double rttyDialFor(const QString& via, double markMhz) const;
    void startWanConnect();
    void connectSmartLinkRadioNow(int index, const QList<quint32>& disconnectHandles);
    void openStationConflict(bool wan, const QString& radio, const QVariantList& clients, bool full = false);
    void askWanStationConflict();
    void setSmartLinkState(const QString& state);

    RadioSession m_session;
    RadioListModel m_radios;
    std::unique_ptr<RadioDiscoverySource> m_discovery;
    QThread* m_audioThread{nullptr};
    AudioEngine* m_audio{nullptr};
    BandPlanManager m_bandPlan;
    // Band plan lock: the plan part the active slice was in (only a move
    // into another part sets the mode), and when the mode last changed (a
    // spot or memory that sets a mode the part allows wins).
    QString m_planRegionAt;
    QElapsedTimer m_modeSetClock;
    void followBandPlanMode();
    void planRegionNow();
    void notePlanModeChoice();
    void judgePlanModeChoice();
    void refusePlanMode(const QString& mode);
    const BandPlanManager::Segment* bandPlanSegmentAt(double mhz) const;
    // Split: the listening and transmitting slices (-1: none); pending while
    // the TX slice is being created.
    // Traffic rate: bytes and time at the last networkDetails() call.
    qint64 m_netRxBytes{0};
    qint64 m_netTxBytes{0};
    qint64 m_netSampleMs{0};
    // A slice refused on the pan the radio restored for this client: one
    // retry per connection on a fresh panadapter (pending until it arrives).
    bool m_sliceRecoveryTried{false};
    bool m_sliceRecoveryPending{false};
    QString m_audioCodingInUse;
    // Performance sampling (2 s).
    QTimer m_perfTimer;
    QElapsedTimer m_perfClock;
    quint64 m_lastCpuUsecs{0};
    double m_cpuPercent{0.0};
    double m_cpuPercentOfCore{0.0};
    double m_memoryMB{0.0};
    QByteArray m_outputDeviceId;
    QByteArray m_inputDeviceId;
    bool m_outputDeviceApplied{false};
    QTimer m_headsetLinkTimer;
    int m_headsetLinkTicks{0};
    // Android: Qt's default output, to reopen receive when a USB-C or wired
    // headset comes or goes.
    QByteArray m_defaultOutputId;
    bool m_defaultOutputWired{false};
    // Per-thread CPU (Linux and Android, from /proc/self/task): ticks at
    // the last sample, by thread id.
    QHash<int, quint64> m_threadTicks;
    QStringList m_busyThreads;
    // The radio's microphone input seen last before PC (for The radio's mic).
    QString m_lastRadioMic;
    // The FTx partner last handed to the Log tab (only a new one is, so a
    // call typed there meanwhile is not overwritten every decode).
    QString m_digiDxLogged;
    void sampleThreads(quint64 elapsedUs);
    std::atomic<int> m_framesThisPeriod{0};
    int m_uiFps{0};
    // Audio diagnostics: underruns seen at the last 10 s check.
    quint64 m_lastUnderruns{0};
    QTimer m_audioDiagTimer;
    // Sets the core's AudioCompression for the coming connection.
    void applyAudioCompression(const QHostAddress& radio, bool wan);
    bool m_redactIdentity{false};
    QString m_calState;
    QString m_calResult;
    bool m_calRunningSeen{false};
    int m_calPrevPpb{0};  // the offset before a calibration cleared it
    int m_calRun{0};
    int m_splitRxId{-1};
    int m_splitTxId{-1};
    bool m_splitPending{false};
    QList<QMetaObject::Connection> m_rxAudioConnections;
    QList<QMetaObject::Connection> m_sliceConnections;
    QList<QMetaObject::Connection> m_panConnections;
    QMetaObject::Connection m_txSinkConnection;
    // Transmit audio as it leaves for the radio, per PTT press: packets,
    // bytes, the longest gap between sends and packets the dispatch fence
    // stopped. Written on the network thread, read when PTT is released.
    struct TxSendStats {
        std::atomic<int> packets{0};
        std::atomic<int> fenced{0};
        std::atomic<qint64> bytes{0};
        std::atomic<qint64> firstMs{0};
        std::atomic<qint64> lastMs{0};
        std::atomic<qint64> maxGapMs{0};
        void reset()
        {
            packets = 0;
            fenced = 0;
            bytes = 0;
            firstMs = 0;
            lastMs = 0;
            maxGapMs = 0;
        }
    };
    std::shared_ptr<TxSendStats> m_txSendStats{std::make_shared<TxSendStats>()};
    TxCoordinator::Producer m_microphoneProducer;
    TxController::Input m_pttInput;
    TxController::Input m_tuneInput;
    TxController::Input m_atuInput;
    TxController::Input m_cwKeyInput;
    TxController::Input m_cwxInput;
    bool m_cwxLiveInDecoder{false};
    bool m_dvkRecordingMic{false};
    // What the log has said of the radio's license features and
    // subscription, so each is logged once and again on a change.
    QHash<QString, QString> m_loggedLicenseFeatures;
    QString m_loggedLicenseSubscription;
    QList<QObject*> m_micCheckHolders;   // shown TX tabs
    bool m_micCheckOpen{false};          // the microphone is open for them
    std::unique_ptr<QsoLog> m_qsoLog;

    // ---- FT8 / FT4 (MobileEngineDigi.cpp) ----
    void setupDigi();
    void loadDigiSettings();
    void setupOnlineLog();
    // Queue an ADIF record for the logbook and send the queue.
    void onlineLogQueue(const QString& adif);
    void onlineLogFlush();
    void onlineLogCheck();  // the API key, shown on the Log tab
    QString onlineLogBase() const;
    class QNetworkAccessManager* m_onlineLogNet{nullptr};
    QString m_onlineLogKey;  // from the secure store, or set this session
    QStringList m_onlineLogPending;
    bool m_onlineLogSending{false};
    QString m_onlineLogStatus;
    QString m_onlineLogStatusKind;
    // The logbook read back (MobileEngineOnlineLog.cpp): its contacts, apart
    // from the app's own log (KnownLog), and the read in progress.
    struct KnownFetch;
    bool onlineLogReadOn() const;
    void onlineLogFetch(bool full);
    void onlineLogFetchNext(const std::shared_ptr<KnownFetch>& fetch);
    void onlineLogFetchDone(const std::shared_ptr<KnownFetch>& fetch, const QString& error);
    void onlineLogForget();
    QList<QsoEntry> m_knownQsos;
    bool m_knownReading{false};
    bool m_knownReadThisSession{false};
    bool m_knownFullRead{false};  // this session: the file's list is replaced
    int m_knownGeneration{0};     // up at each forget: older reads are dropped
    // ---- QRZ.com Logbook (MobileEngineQrzLog.cpp) ----
    struct QrzFetch;
    void setupQrzLog();
    void qrzLogCheck();
    void qrzLogQueue(const QString& adif);
    void qrzLogFlush();
    bool qrzLogReadOn() const;
    void qrzLogFetch(bool full);
    void qrzLogFetchNext(const std::shared_ptr<QrzFetch>& fetch);
    void qrzLogFetchDone(const std::shared_ptr<QrzFetch>& fetch, const QString& error);
    void qrzLogForget();
    class QNetworkReply* qrzLogPost(const QList<QPair<QString, QString>>& fields);
    QString m_qrzLogKey;
    QStringList m_qrzLogPending;
    bool m_qrzLogSending{false};
    QString m_qrzLogStatus;
    QString m_qrzLogStatusKind;
    QList<QsoEntry> m_qrzKnown;
    bool m_qrzReading{false};
    bool m_qrzReadThisSession{false};
    bool m_qrzFullRead{false};
    int m_qrzGeneration{0};
    QString m_qrzReadStatus;
    QString m_qrzReadStatusKind;
    // ---- The shared ADIF file (MobileEngineSharedAdif.cpp) ----
    void setupSharedAdif();
    void sharedAdifQueue(const QString& adif);
    void sharedAdifFlush();
    QString m_sharedAdifUrl;  // a file URL, or Android's content:// URI
    QStringList m_sharedPending;
    QList<QsoEntry> m_fileKnown;
    bool m_sharedReading{false};
    bool m_sharedWriting{false};
    bool m_sharedWritable{true};
    int m_sharedGeneration{0};
    QString m_sharedStatus;
    QString m_sharedStatusKind;
    QString m_knownStatus;
    QString m_knownStatusKind;
    // ---- Logger32 (MobileEngineLogger32.cpp) ----
    void setupLogger32();
    QString logger32Host() const;
    void logger32Check();
    void logger32Queue(const QString& adif);
    void logger32Flush();
    QStringList m_logger32Pending;
    class QTcpSocket* m_logger32Socket{nullptr};  // the connection in progress
    QTimer m_logger32Retry;
    QString m_logger32Status;
    QString m_logger32StatusKind;
    // ---- 4O3A PGXL / TGXL (MobileEngine4o3a.cpp) ----
    void setup4o3a();
    void connect4o3a();
    void disconnect4o3a();
    bool geniusCommandAllowed(bool tuner, bool notWhileTransmitting, const char* what);
    double pgxlForwardW() const;
    double pgxlSwr() const;
    bool pgxlOperating() const;
    class PgxlConnection* m_pgxlConn{nullptr};
    class TgxlConnection* m_tgxlConn{nullptr};
    AntennaGeniusModel* m_ag{nullptr};
    QString m_agLinkError, m_agNotice;
    // An SO2R TGXL: no 3way in its info reply; its active channel and B side
    // from its status.
    bool m_tgxlSo2r{false};
    int m_tgxlActive{0};
    QVariantMap m_tgxlSides;
    QString m_pgxlLinkError, m_tgxlLinkError, m_pgxlNotice, m_tgxlNotice;
    bool m_pgxlPending{false};
    double m_pgxlDirectFwd{0}, m_pgxlDirectSwr{1}, m_pgxlRelayFwd{0}, m_pgxlRelaySwr{1};
    qint64 m_pgxlRelayAt{0};
    double m_pgxlDrive{-1}, m_pgxlId{0}, m_pgxlTemp{0}, m_pgxlHlTemp{0}, m_pgxlVdd{0};
    int m_pgxlVac{0};
    // ---- The amplifier (MobileEngineAmp.cpp) ----
    void setupAmp();
    void updateAmpLink();
    void updateAmpPollRate();
    bool ampCommandAllowed(const char* what);
    void ampCommandDone(const QString& result, bool ok);
    void startAmpTuneCarrier();
    void checkAmpTune();
    void endAmpTune(const QString& result, bool ok);
    RfkitConnection* m_amp{nullptr};
    Rfkit::Status m_ampStatus;
    bool m_ampWanted{false};
    bool m_ampFastPoll{false};
    bool m_ampSet{false};
    QString m_ampTarget;  // "host:port" being read
    QString m_ampState{QStringLiteral("off")};
    QString m_ampError;
    QString m_ampPending;
    QString m_ampResult;
    bool m_ampResultOk{false};
    QString m_ampTune;  // "", "standby", "carrier"
    QTimer* m_ampTuneTimer{nullptr};
    QElapsedTimer m_ampTuneClock;
    bool m_ampTuneSawTuning{false};
    bool m_ampTuneKeyed{false};  // the radio has reported the carrier on
    int m_ampTuneLeftShown{-1};
    void syncDigiAudio();
    void onDigiSlot(qint64 slotMs);
    void handleDigiDecodes(const QVariantList& decodes, qint64 slotMs);
    void startDigiTx(qint64 slotMs);
    void onDigiTxFinished();
    // Keyboard-mode transmit: the next text keyed (or played on while
    // keyed), and what to do when a clip has gone out.
    void startTextTx();
    void onTextTxFinished();
    QString expandTextMacro(const QString& text, QString* error) const;
    void digiLogQso();
    void digiEnd(const QString& status);
    void setDigiStatus(const QString& status);
    QString digiMyCall() const;
    QString digiReport(int snr) const;
    // The next message of a kind: cq, call, report, rreport, rr73, 73.
    QString digiMessage(const QString& kind) const;
    // The slice FT8 / FT4 decode and transmit on: the TX slice (as with
    // SmartSDR and WSJT-X). Moving the TX badge moves FT8; the active slice
    // does not matter, so several slices neither mix their audio nor move
    // the TX.
    QPointer<SliceModel> m_digiSlice;
    QList<QMetaObject::Connection> m_digiSliceConnections;
    bool m_digiAssignedDax{false};
    SliceModel* digiSlice() const { return m_digiSlice ? m_digiSlice.data() : m_slice.data(); }
    void bindDigiSlice(SliceModel* slice);
    void updateDigiSlice();
    // Slot timing and the transmit audio (fed no receive audio itself).
    std::unique_ptr<Ft8Engine> m_ft8;
    // One decoder per DIGU / DIGL slice (and the TX slice), each on that
    // slice's own DAX channel; their decodes go into one list marked with
    // the slice.
    struct DigiDecoder;
    std::map<int, std::shared_ptr<DigiDecoder>> m_digiDecoders;
    bool m_digiOn{false};
    QString m_rttyRunVia;  // the RTTY decoder's current way (RTTY / DIGL / DIGU)
    int m_rttyRunMarkTone{0};
    // What the log has worked, for WSJT-X's highlighting: calls per band and
    // mode, DXCC entities, continents and CQ zones (each also per band).
    QSet<QString> m_wkCallBandMode, m_wkDxcc, m_wkDxccBand, m_wkCont, m_wkContBand, m_wkZone, m_wkZoneBand;
    // Calls worked at all, and on a band ("CALL|20m"): WSJT-X's New Call
    // and New Call on Band.
    QSet<QString> m_wkCall, m_wkCallBand;
    bool m_wkDirty{true};
    void rebuildWorked();
    QString digiHighlight(const QString& call, bool cq, bool toMe, const QString& band, const QString& modeKey);
    int digiNewDxcc(const QString& call, bool cq, const QString& band);
    // 2 a call not in the log, 1 not on this band, 0 worked on it (CQs only).
    int digiNewCall(const QString& call, bool cq, const QString& band);
    QVariantList m_digiDecodes;
    QString m_digiFilter{QStringLiteral("all")};
    QString m_digiState{QStringLiteral("idle")};
    QString m_digiDx;
    int m_digiRxHz{0};  // the audio Hz the station in the QSO is heard on
    QString m_digiDxGrid;
    QString m_digiDxSig;  // "POTA" when they called CQ POTA (the log's SIG)
    QString m_digiRptSent;
    QString m_digiRptRcvd;
    QString m_digiTxMsg;
    QString m_digiStatus;
    QString m_digiTimeOn;
    int m_digiTxHz{1200};
    bool m_digiTxHzSavePending{false};
    int m_digiTxParity{0};
    int m_digiTries{0};
    int m_digiTxSeq{0};
    bool m_digiTransmitting{false};
    bool m_digiLogged{false};
    bool m_digiEndAfterTx{false};
    // The message of the transmission under way (or the last one): the QSO
    // ends after the closing message itself has gone out.
    QString m_digiSentMsg;
    // The last contact logged: a station that sends again soon after (its
    // 73, or its report when our RR73 did not reach it) is not a new QSO.
    QString m_digiLastLoggedCall;
    QString m_digiLastLoggedBand;
    qint64 m_digiLastLoggedMs{0};

    // ---- RADE (MobileEngineRade.cpp) ----
    void activateRade();
    void deactivateRade();
    void stopRadeBeforeLeaving(const char* why);
    // The middle of the passband for a carrier, with the slice's filter.
    double passbandCentreMhz(double carrierMhz) const;
    // Centres the view on the passband once the radio has reported the
    // slice's frequency, mode and filter (a memory, a band, a spot, a FreeDV
    // station change them): after kCentreSettleMs without a change, within
    // kCentreWithinMs.
    void centreWhenSettled();
    void noteCentreChange();
    void centreOnPassband();
    void restoreAfterRade(SliceModel* s);
    void healRadeMute();
    void requestRadeEoo(quint64 requestId);
    void widenDigiFilter();
    QThread* m_radeThread{nullptr};
    RADEEngine* m_radeEngine{nullptr};
    int m_radeSliceId{-1};
    QString m_radePrevMode;
    int m_radePrevFilterLow{0};
    int m_radePrevFilterHigh{0};
    bool m_radeAssignedDax{false};
    bool m_radePrevDax{false};
    int m_radeDaxChannel{0};
    TransmitModel::PttRelease m_radePttRelease;
    quint64 m_radeEooRequestId{0};
    bool m_radeEooPending{false};
    // Released before MOX came back: the end of over waits for it.
    bool m_radeEooAfterMox{false};
    // RADE receive summary every 10 s (written from the RADE thread).
    QTimer m_radeDiagTimer;
    std::atomic<qint64> m_radeRxBytes{0};
    std::atomic<int> m_radeRxPeak{0};
    std::atomic<int> m_radeRxOther{0};
    bool m_radeTxActive{false};
    std::shared_ptr<std::atomic<bool>> m_radeFallbackFence;
    QList<QMetaObject::Connection> m_radeConnections;
    bool m_radeSynced{false};
    float m_radeSnr{0.0f};
    QString m_radeRxCallsign;
    // FreeDV Reporter (MobileEngineFreeDv.cpp).
    void setupFreeDv();
    // MIDI controller (MobileEngineMidi.cpp).
    void setupMidi();
    void registerMidiFunctions();
    void midiAction(const QString& paramId, float value);
    void midiTune(int steps);
    void midiWatch();
    void midiSaveBindings();
    bool midiTransmitRefused();
    void midiEndTransmit(const QString& why);
    AetherSDR::MidiControlManager* m_midi{nullptr};
    QHash<QString, std::function<void(float)>> m_midiActions;
    QSet<QString> m_midiTxFunctions;
    QTimer m_midiWatchTimer;
    QTimer m_midiTuneIdle;
    QTimer m_midiActivityTimer;
    double m_midiTuneTargetMhz{-1.0};
    QStringList m_midiPorts;
    QString m_midiActivity;
    QString m_midiActivityPending;
    QVariantMap m_midiImport;
    bool m_midiDit{false};
    bool m_midiDah{false};
    // A transmission MIDI keyed is under way: ended when the controller goes.
    bool m_midiKeyed{false};
    // The VFO knob's first messages and steps of each turn go to the log
    // (roadmap 60: what a controller sends is known from a tester's log).
    qint64 m_midiVfoLogMs{0};
    int m_midiVfoRawLogged{0};
    int m_midiVfoStepsLogged{0};
    bool m_bigButtons{false};
    // Auto AGC-T (MobileEngineAgcAuto.cpp).
    void setupAgcAuto();
    void bindAgcAutoSlice();
    QString agcAutoKey() const;
    void scheduleAgcRecall();
    void recallAgcThreshold();
    AetherSDR::AgcTCalibrator* m_agcCal{nullptr};
    bool m_agcMeasuring{false};
    int m_agcProgress{0};
    QString m_agcNote;
    double m_spectrumFloorDbm{-200.0};
    // The pan's latest FFT frame: the strongest bin in the active slice's
    // passband over the frame's noise floor, in dB (NaN: no frame lately).
    double passbandOverFloorDb() const;
    QVector<float> m_lastFftBins;
    double m_lastFftCentre{0.0};
    double m_lastFftWidth{0.0};
    qint64 m_lastFftMs{0};
    QString m_agcRecallKey;
    QTimer m_agcRecallTimer;
    bool m_agcRecalling{false};
    bool m_agcStopping{false};
    bool m_agcKnee{true};
    bool m_agcFlat{false};
    void loadFreeDvSettings();
    // App setups (MobileEngineSetups.cpp): every property's change signal,
    // so the pages read the applied settings.
    void announceAllProperties();
    void updateFreeDv();
    void rebuildFreedvStations();
    FreeDvClient* m_freedv{nullptr};
    bool m_freedvReporterOn{false};
    int m_freedvViewers{0};
    bool m_freedvReporting{false};   // reporting enabled in the client now
    bool m_freedvStarted{false};     // the client's connection wanted (it reconnects by itself)
    QString m_freedvLastRxCall;      // RADE's end-of-over call last passed on
    QString m_freedvLastStatus;      // freedvChanged only when the words change
    bool m_freedvAllBands{false};
    QString m_freedvMessage;
    QString m_freedvError;
    QVariantList m_freedvStations;
    QTimer m_freedvRebuildTimer;     // at most two list updates a second
    QTimer m_freedvAgeTimer;         // "TX n min ago" moves on
    QTimer m_freedvFreqTimer;        // a frequency is reported once tuning settles
    QMetaObject::Connection m_freedvMoxConnection;
    QMetaObject::Connection m_freedvFreqConnection;
    std::unique_ptr<CtyDatParser> m_cty;
    QString m_lookupCall;                // the call callLookup is for
    QString m_qrzStatus;                 // QRZ.com sign-in check, as shown
    QString m_qrzStatusKind;             // "checking", "ok", "failed", "unreachable" or ""
    QVariantMap m_callLookup;
    QString m_lastSpotCall;
    void applyCallInfo(const CallsignInfo& info);
    void pttToggleLimitReached();
    void txArmTick();
    void syncDigiAudioLater();
    int m_dvkRecordSlot{0};
    static QString dvkStateName(int status);
    void ensurePanDbmRange(const QString& panId);
    qint64 m_lastDbmRangeSendMs{0};
    bool m_dbmRangeRetryPending{false};
    int m_splitUpKhz{0};
    void onVoiceClipRecorded(const QByteArray& clip, int rate);
    void onVoiceClipFinished();
    QString m_voiceState{QStringLiteral("idle")};
    int m_voiceActiveId{-1};
    // Macro repeat (roadmap 33): what repeats ("voice" slot id or "cw"
    // memory index), the gap timer, and guards for the engine's own keying.
    void armMacroRepeat(const QString& kind, int index);
    void scheduleMacroRepeat();
    void fireMacroRepeat();
    bool m_macroRepeat{false};
    QString m_repeatKind;
    int m_repeatIndex{-1};
    double m_repeatDueAt{0};
    bool m_repeatFiring{false};
    bool m_repeatSending{false};  // armed slot on the air; its end schedules the next
    bool m_macroKeying{false};
    QTimer m_repeatTimer;
    bool m_radeMetersShown{false};  // the meter choices last offered SNR
    // Listening to one of the app's own clips on this device: the clip as
    // 24 kHz stereo float, fed into the receive output in real time.
    // TX chain settings are written a moment after the last change (a
    // slider drag would otherwise write the settings file on every step).
    QTimer m_txChainSaveTimer;
    bool m_pttLatched{false};
    QTimer m_pttToggleLimit;
    // Transmit lock: armed until (ms since the epoch; 0 = standby), the
    // second's hold that arms, and the countdown's tick.
    qint64 m_txArmedUntilMs{0};
    QTimer m_txArmHold;
    QTimer m_txArmTick;
    bool m_txStartArmed{false};  // txArmAtStart: armed once in this run
    bool txLockRefuses(const char* what);
    // DX cluster: its spots go to the radio ("spot add", as the desktop),
    // deduplicated per call within 1 kHz and sent at most RbnRateLimit a
    // second; the radio shows them to every client and brings them back
    // into its spot list.
    DxClusterClient* m_dx{nullptr};
    bool m_dxWanted{false};
    QString m_dxState{QStringLiteral("off")};
    QString m_dxError;
    QElapsedTimer m_dxConnectedAt;
    int m_dxShortSessions{0};
    int m_dxSpotCount{0};
    QDateTime m_dxLastSpotUtc;
    QHash<QString, QPair<double, qint64>> m_dxSeen;
    QStringList m_dxQueue;
    QTimer m_dxSendTimer;
    QTimer m_spotsNotifyTimer;   // at most two spot list updates a second
    QTimer m_spotsAgeTimer;      // spots age out of the age filter
    void setupDxCluster();
    QString dxCallsign() const;
    QTimer m_voicePreviewTimer;
    QElapsedTimer m_voicePreviewClock;
    // A held step button repeats faster than the radio confirms a tune
    // over SmartLink: the next step counts from the last target.
    qint64 m_stepTargetHz{0};
    QElapsedTimer m_stepClock;
    QByteArray m_voicePreviewPcm;
    qsizetype m_voicePreviewPos{0};
    void feedVoicePreview();
    void stopVoicePreview();
    bool m_cwKeyHeld{false};
    std::unique_ptr<CwRxModel> m_cwDecoder;
    // Sidetone for CWX (memories, CW text): the radio's own keyer sends it,
    // so this plays the same text locally at the same speed, as the desktop.
    std::unique_ptr<CwxLocalKeyer> m_cwxLocalKeyer;
    std::unique_ptr<IambicKeyer> m_iambicKeyer;
    int m_cwShown{0};
    int m_cwDropped{0};
    int m_cwCostBins[10]{};   // letters by cost, tenths (diagnostics)
    TxCoordinator::Request m_cwPaddleInput;
    bool m_cwPaddleHeld{false};
    std::atomic<bool> m_paddleKeyDown{false};
    std::atomic<int> m_paddleElements{0};   // key-downs from the paddles (diagnostics)
    void syncIambicKeyer();
    void releasePaddles();
    // A left or right Control / Option from GameController (SideKey).
    void onSideKey(int key, bool pressed);
    QStringList m_lastBands;
    bool m_keyboardOnPage{false};
    bool m_spacePtt{false};   // PTT held by Space from GameController
    int m_spaceLogged{0};
    bool m_sideDit{false};
    bool m_sideDah{false};
    int m_sideKeysLogged{0};
    // A key or paddle press with break-in off: say why nothing goes on air.
    void noteBreakInOff();
    std::atomic<int> m_cwxSidetoneDowns{0};      // key-downs played (diagnostics)
    std::atomic<qint64> m_cwxSidetoneKeyedMs{0};  // time keyed, ms
    std::chrono::steady_clock::time_point m_cwxSidetoneDownAt;  // keyer thread only
    std::unique_ptr<DecoderAudioModel> m_cwAudio;
    std::unique_ptr<RttyDecoder> m_rttyDecoder;
    // The slice's receive audio for the Digi tab's text modes (RTTY and PSK
    // alike; each decoder runs only in its own mode).
    std::unique_ptr<DecoderAudioModel> m_rttyAudio;
    std::unique_ptr<PskDecoder> m_pskDecoder;
    double m_pskTrackedHz{0.0};
    int m_pskQuality{0};
    bool m_pskLocked{false};
    QString m_rttyDecodedText;
    QString m_rttyStrip;
    qint64 m_rttyStripStart{0};
    float m_rttySnrDb{0.0f};
    bool m_rttyLocked{false};
    // A spot's mark looked for again while its station listens (the
    // display shows no pair then): the spot's frequency and tries left.
    double m_rttySpotMhz{0.0};
    // Keyboard-mode transmit (MobileEngineTextModes.cpp).
    bool m_textTransmitting{false};
    bool m_textProcRestore{false};
    int m_textTxSeq{0};
    QStringList m_textTxQueue;
    int m_rttySpotTries{0};
    // The squelch (appendRttyGated): open, the characters before it opened
    // that may start the text, and one doubtful character held inside a run.
    bool m_rttyGateOpen{false};
    QString m_rttyGatePending;
    QString m_rttyGateHeld;
    QMetaObject::Connection m_rttyMarkConnection;
    QMetaObject::Connection m_rttyShiftConnection;
    QString m_cwDecodedText;
    // The mark added when the app went to the background with the decoder on.
    QString m_cwAwayMark;
    QString m_cwStrip;
    QString m_workedCall;
    QDateTime m_workedCallSince;
    // QSOs waiting for their call's lookup (name, QTH), by call.
    QHash<QString, QVariantMap> m_pendingQsos;
    bool m_qsoLogDryRun{false};
    QVariantMap callInfoMap(const CallsignInfo& info) const;
    QVariantMap ctyFields(const QString& call) const;
    void addLookupFields(QVariantMap& r, const QVariantMap& info) const;
    void completePendingQso(const QString& call, const QVariantMap& info);
    bool writeQso(const QVariantMap& r);
    qint64 m_cwStripStart{0};
    // The strip's end before and after the away mark (stream places).
    qint64 m_cwStripBeforeAway{0};
    qint64 m_cwStripAfterAway{0};
    QString m_cwDecoderStats;
    double m_atuTunedFreqMhz{-1.0};
    double m_compressionDb{0.0};
    double m_alcDbfs{-100.0};
    bool m_alcAvailable{false};
    double m_paTempC{0.0};
    double m_supplyVolts{0.0};
    bool m_pttHeld{false};
    // Receive playback silent while the microphone is keyed (duckReceive).
    bool m_rxDucked{false};
    bool m_micPermissionRequested{false};
    bool m_connected{false};
    // The radio's CW pitch was set to 600 this session (it reported 0).
    bool m_cwPitchRepaired{false};
    void repairCwPitch(int hz);
    bool m_micDefaulted{false};
    bool m_displayActive{true};
    int m_fftFramesSeen{0};
    int m_wfRowsSeen{0};
    quint32 m_otherWfStreamSeen{0};
    bool m_micDefaultEnabled{true};
    RadioCapabilities m_caps;
    SmartLinkClient m_smartLink;
    WanConnection m_wan;
    QList<WanRadioInfo> m_wanRadios;
    WanRadioInfo m_pendingWan;
    QVariantMap m_stationConflict;
    // The SmartLink radio a conflict question is about (-1: a LAN one).
    int m_conflictWanIndex{-1};
    // The SmartLink radio that refused this app, asked about once the app is idle.
    QString m_pendingWanConflict;
    // Slots full on the radio's network: where to connect again (m_lastTarget).
    QString m_conflictLanTarget;
    QVariantMap m_fullConflict;  // the slots-full question, until the connection closes
    bool m_registrationFull{false};
    bool m_registrationRefused{false};
    bool m_wanActive{false};
    bool m_wanUserDisconnect{false};
    // A host name being looked up before connecting (connectToAddress).
    int m_hostLookupId{-1};
    bool m_smartLinkRetried{false};   // one re-sign-in after a refused session
    // A kept sign-in through a network or server outage (roadmap 52): tried
    // again after 5 s, doubling to 60 s, and at once when the network or the
    // app comes back.
    QTimer m_smartLinkRetryTimer;
    int m_smartLinkRetryMs{0};
    void retrySmartLinkSignIn(const QString& why);
    QTimer m_wanReconnectTimer;
    QString m_smartLinkState{QStringLiteral("signedOut")};
    double m_systemTextScale{1.0};   // read at start and on each return to the front
    // The connection doctor's state (MobileEngineDoctor.cpp). m_lastTarget is
    // the radio last tried: an address or host name, or "smartlink:<serial>".
    QString m_lastTarget;
    QString m_checkTarget;
    QVariantList m_check;
    bool m_checkRunning{false};
    int m_checkRun{0};
    void checkStep(const QString& title, const QString& state, const QString& detail);
    void checkFinish();
    void checkSmartLink(const QString& serial);
    void checkAddress(const QString& host, const QHostAddress& address);
    double m_micLevelDbfs{-100.0};
    double m_micPeakDbfs{-100.0};
    double m_forwardPowerW{0.0};
    int m_digiTxLevelDb{0};
    bool m_previewTx{false};
    qint64 m_lastInterlockMs{0};  // when the radio last gave a reason for refusing transmit
    bool m_digiAutoCq{false};
    bool m_digiAutoAnswer{false};
    bool m_digiAutoQso{false};  // the QSO under way is one Answer CQ started
    int m_digiPresenceMin{10};
    qint64 m_operatorMs{0};     // the last touch, click or key on the app
    // Stations Answer CQ called, and when: not called again for a while.
    QHash<QString, qint64> m_digiAutoTried;
    bool digiOperatorAway() const;
    bool digiFilterPasses(const QVariantMap& row) const;
    void digiAutoAnswerPick(const QVariantList& decodes);
    // FT8 / FT4 keying its own tones: past setPtt's voice-only check (DIGU
    // is where they transmit; builds 108-112 refused them there).
    bool m_digiKeying{false};
    bool m_pskReporterOn{false};
    class PskReporterUpload* m_psk{nullptr};
    QString m_digiCqParity{QStringLiteral("auto")};
    QString m_digiCqTarget;
    QStringList m_digiCqTargets;
    bool m_digiFromCq{false};  // the QSO under way answered our CQ
    // Hound: on, the pileup shown, the Hz we call on (back after a QSO at
    // the Fox's frequency; not saved), the R+reports sent in this QSO (the second goes
    // 300 Hz off), the Fox as heard (m_digiDx is its base call, as Hounds
    // call it) and when we last called it (a late report from its queue is
    // still answered for three minutes, its QSO timeout).
    bool m_digiHound{false};
    bool m_digiHoundAllHz{false};
    int m_digiHoundCallHz{0};
    int m_digiHoundPrevHz{0};  // FT8's TX Hz before Hound, back when it goes off
    int m_digiHoundRrpts{0};
    QString m_digiHoundFox;
    qint64 m_digiHoundCallMs{0};
    // A Fox's message to us, handled by the Hound rules (true: taken).
    bool digiHoundHandle(const QVariantMap& d);
    void digiHoundReply(int foxHz, const QString& report, int snr);
    bool m_digiDryRun{false};
    // The radio's speech processor, switched off for FT8 / FT4 transmit
    // (it would distort the tones) and back on when decoding stops.
    bool m_digiProcRestore{false};
    // The pan's width before FT8 / FT4 zoomed it to 4 kHz (0: not zoomed),
    // back when the slice leaves DIGU / DIGL for voice.
    double m_digiPrevPanBw{0.0};
    bool m_importing{false};
    // The decodes' median DT, smoothed: far from 0 means this device's
    // clock is off (FT8 needs it within about a second).
    double m_digiClockOffset{0.0};
    bool m_digiClockKnown{false};
    // The time server FT8 / FT4 run on (NtpClock), when it last answered.
    NtpClock* m_ntp{nullptr};
    QTimer m_ntpTimer;
    QElapsedTimer m_ntpSynced;
    QString m_digiClockServer;
    void digiFinishQso(const QString& status);
    // Both our call and the other station's are nonstandard (OH6OKSA and,
    // say, SN9Q/QRP): FT8 has no message for a report between them, so the
    // QSO is calls and RR73 only (type 4 messages).
    bool digiNoReports() const;
    double m_digiTxPeakW{0.0};
    // The forward power's peak since PTT was keyed (the release logs it).
    double m_pttPeakW{0.0};
    // Peaks of the transmission under way and the last one (radio and a
    // linked amplifier), for the log's TX power.
    bool m_txPeakOn{false};
    double m_txPeakRadioW{0.0};
    double m_txPeakAmpW{0.0};
    double m_lastTxRadioW{0.0};
    double m_lastTxAmpW{0.0};
    qint64 m_lastTxEndMs{0};
    bool linkedAmpOperating() const;
    double linkedAmpForwardW() const;
    double m_digiTxPeakSwr{0.0};
    // The last transmission's peak power and SWR, for the Digi tab.
    double m_digiLastTxW{0.0};
    double m_digiLastTxSwr{0.0};
    double m_swr{1.0};
    bool m_swrValid{false};
    QPointer<SliceModel> m_slice;
    QPointer<PanadapterModel> m_pan;
    QString m_connectionState{QStringLiteral("idle")};
    QString m_lastError;
    QString m_radioName;
    // The saved radio's address this connection was made to, to learn what
    // the radio calls itself (empty for SmartLink and the radios found on
    // the network).
    QString m_savedRadioAddress;
    QJsonArray loadSavedRadios() const;
    void storeSavedRadios(const QJsonArray& radios);
    void rememberSavedRadio(const QString& address);
    void noteSavedRadioName();
    double m_sMeterDbm{-130.0};
    QTimer m_panPixelsTimer;
    QTimer m_panMoveTimer;
    double m_pendingPanCenterMhz{0.0};
    QTimer m_centreSettleTimer;
    qint64 m_centreUntilMs{0};
    double m_centredMhz{0.0};
    // addTnfWith's settings until the radio reports the new notch: the ids
    // there were before it was asked, and until when to wait.
    struct PendingTnf {
        double freqMhz{0.0};
        int widthHz{0};
        int depth{0};
        bool permanent{false};
        QSet<int> known;
        qint64 untilMs{0};
    };
    PendingTnf m_pendingTnf;
    void applyPendingTnf(int id);
    QTimer m_panPreviewEndTimer;
    double m_panPreviewMhz{0.0};
    qint64 m_panPreviewUntilMs{0};
    int m_panWidth{0};
    int m_panHeight{0};
};

// Exposes the existing MobileEngine to QML as the Engine singleton instead of
// letting QML default-construct a second engine.
struct MobileEngineForeign {
    Q_GADGET
    QML_FOREIGN(AetherSDR::mobile::MobileEngine)
    QML_NAMED_ELEMENT(Engine)
    QML_SINGLETON

public:
    static MobileEngine* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
};

} // namespace AetherSDR::mobile
