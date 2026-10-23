#pragma once

#include "models/BandPlanManager.h"
#include "RadioListModel.h"

#include "core/TxCoordinator.h"
#include "core/backends/flex/SmartLinkClient.h"
#include "core/backends/flex/WanConnection.h"
#include "core/backends/RadioCapabilities.h"
#include "models/RadioSession.h"
#include "models/TxController.h"

#include <QHash>
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

#include <memory>

class QJSEngine;
class QQmlEngine;
class QThread;
class Ft8Engine;

namespace AetherSDR {
class AudioEngine;
class DxClusterClient;
class CwRxModel;
class CwxLocalKeyer;
class IambicKeyer;
class DecoderAudioModel;
class RADEEngine;
class PanadapterModel;
class RadioDiscoverySource;
class SliceModel;
class CtyDatParser;
struct CallsignInfo;
struct PcmFrame;
struct RadioInfo;
struct SpectrumDecodeScale;
}

namespace AetherSDR::mobile {

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
    // PTT as a switch (tap on, tap off) instead of hold-to-talk; remembered.
    // A latched transmission stops by itself after kPttToggleLimitS.
    Q_PROPERTY(bool pttToggle READ pttToggle WRITE setPttToggle NOTIFY pttToggleChanged)
    Q_PROPERTY(bool pttLatched READ pttLatched NOTIFY pttToggleChanged)
    Q_PROPERTY(QStringList bandPlans READ bandPlans CONSTANT)
    // Receive audio coding: "Auto" (Opus when remote: SmartLink, a tailnet,
    // or the radio reached through a VPN), "Opus" or "None"; applies from
    // the next connection. audioCodingInUse says what this one uses and why.
    Q_PROPERTY(QString audioCompression READ audioCompression WRITE setAudioCompression NOTIFY audioCompressionChanged)
    Q_PROPERTY(QString audioCodingInUse READ audioCodingInUse NOTIFY audioCompressionChanged)
    // Receive audio buffer: "Low" (100 ms, no prebuffer), "Normal" (200 ms,
    // 60 ms prebuffer after a gap) or "Smooth" (400 ms, 150 ms): steadier
    // audio over a jittery link for a little more delay.
    Q_PROPERTY(QString audioBuffer READ audioBuffer WRITE setAudioBuffer NOTIFY audioCompressionChanged)
    // Network quality as the desktop rates it (Excellent ... Poor, "Off"
    // when not connected) and the round trip in ms, for the header.
    Q_PROPERTY(QString networkQuality READ networkQuality NOTIFY networkChanged)
    Q_PROPERTY(int networkRttMs READ networkRttMs NOTIFY networkChanged)
    // The app's CPU as the desktop's status bar shows it (share of all
    // cores, every 2 s), and frames drawn per second (no public GPU load on
    // iOS: a falling frame rate is what shows the graphics not keeping up).
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY performanceChanged)
    Q_PROPERTY(double cpuPercentOfCore READ cpuPercentOfCore NOTIFY performanceChanged)
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
    Q_PROPERTY(QString smartLinkState READ smartLinkState NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkAccount READ smartLinkAccount NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkEmail READ smartLinkEmail NOTIFY smartLinkChanged)
    Q_PROPERTY(QVariantList smartLinkRadios READ smartLinkRadios NOTIFY smartLinkRadiosChanged)
    Q_PROPERTY(bool remoteSession READ remoteSession NOTIFY connectionStateChanged)
    Q_PROPERTY(bool controlsVisible READ controlsVisible WRITE setControlsVisible NOTIFY controlsVisibleChanged)
    Q_PROPERTY(bool transmitAvailable READ transmitAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(QString transmitUnavailableReason READ transmitUnavailableReason NOTIFY transmitStateChanged)
    Q_PROPERTY(bool canUseDeviceMicrophone READ canUseDeviceMicrophone NOTIFY transmitStateChanged)
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
    // The QSO: state ("idle", "cq", "answer"), dx, next message, status line,
    // transmitting, seconds into the slot, slot parity of our transmissions.
    Q_PROPERTY(QVariantMap digi READ digi NOTIFY digiChanged)
    // Decodes, newest first: utc, snr, dt, freq, text, call, cq, toMe, slotOdd.
    Q_PROPERTY(QVariantList digiDecodes READ digiDecodes NOTIFY digiDecodesChanged)
    // RADE digital voice in the app (FreeDV Radio Autoencoder): on or off,
    // whether this build has it, and its receive state (synced, snr,
    // callsign of the last end of over).
    Q_PROPERTY(bool rade READ radeActive WRITE setRadeActive NOTIFY radeChanged)
    Q_PROPERTY(bool radeAvailable READ radeAvailable CONSTANT)
    Q_PROPERTY(QVariantMap radeStatus READ radeStatus NOTIFY radeChanged)
    // Dial frequencies of the protocol on each band: band, mhz.
    Q_PROPERTY(QVariantList digiFrequencies READ digiFrequencies NOTIFY digiChanged)
    Q_PROPERTY(int qsoCount READ qsoCount NOTIFY qsoLogChanged)
    Q_PROPERTY(QVariantMap callLookup READ callLookup NOTIFY callLookupChanged)
    Q_PROPERTY(QString lastSpotCall READ lastSpotCall NOTIFY lastSpotCallChanged)
    Q_PROPERTY(QString qrzUser READ qrzUser NOTIFY qrzChanged)
    // The sign-in check after Save: the text, and "checking", "ok", "failed"
    // or "" (not checked this session).
    Q_PROPERTY(QString qrzStatus READ qrzStatus NOTIFY qrzChanged)
    Q_PROPERTY(QString qrzStatusKind READ qrzStatusKind NOTIFY qrzChanged)
    Q_PROPERTY(QString myGrid READ myGrid WRITE setMyGrid NOTIFY qrzChanged)
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
    Q_PROPERTY(int activeSliceId READ activeSliceId NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddSlice READ canAddSlice NOTIFY slicesChanged)
    // Split, as SmartSDR/AetherSDR: a TX slice beside the RX slice.
    Q_PROPERTY(bool splitActive READ splitActive NOTIFY slicesChanged)
    Q_PROPERTY(bool splitAvailable READ splitAvailable NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddPanadapter READ canAddPanadapter NOTIFY slicesChanged)
    // This client's panadapters. The app shows one; the others still stream.
    Q_PROPERTY(int panCount READ panCount NOTIFY slicesChanged)
    Q_PROPERTY(bool cwMode READ cwMode NOTIFY modeChanged)
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
    Q_PROPERTY(bool cwDecoderEnabled READ cwDecoderEnabled WRITE setCwDecoderEnabled NOTIFY cwDecoderChanged)
    // Decoder sensitivity 0-100 (the desktop's Sens): letters whose cost is at
    // or above 1 - 0.9 x sens/100 are not shown (band noise decodes as such).
    Q_PROPERTY(int cwDecoderSensitivity READ cwDecoderSensitivity WRITE setCwDecoderSensitivity NOTIFY cwDecoderChanged)
    // Tuning lock of the active slice (the radio's slice lock; SliceModel
    // refuses tuning while it is on). tuneBlocked fires on a refused tune.
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    // The radio's profiles (as SmartSDR/AetherSDR): global (band, pans,
    // slices…), transmit and mic. Type is "global", "transmit" or "mic".
    Q_PROPERTY(QStringList globalProfiles READ globalProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeGlobalProfile READ activeGlobalProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList txProfiles READ txProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeTxProfile READ activeTxProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList micProfiles READ micProfiles NOTIFY profilesChanged)
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
    // Meter faces: analog gauges (arc and needle) instead of bars.
    // Analog or bar, each meter on its own; analogMeters sets both (true
    // only when both are analog).
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
    Q_PROPERTY(QString cwDecoderStats READ cwDecoderStats NOTIFY cwDecoderChanged)
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
    Q_PROPERTY(double swr READ swr NOTIFY txMetersChanged)
    Q_PROPERTY(bool swrValid READ swrValid NOTIFY txMetersChanged)
    Q_PROPERTY(bool radioTransmitting READ radioTransmitting NOTIFY txMetersChanged)
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
    void setButtonStyle(const QString& style);
    bool pttToggle() const;
    void setPttToggle(bool on);
    bool pttLatched() const { return m_pttLatched; }
    // Toggle-mode PTT: keys when not transmitting, unkeys when it is.
    Q_INVOKABLE void togglePtt();
    QStringList bandPlans() const;
    // The active plan's segments (empty when off), for the spectrum.
    QVector<BandPlanManager::Segment> bandPlanSegments() const;
    QString audioCompression() const;
    void setAudioCompression(const QString& mode);
    QString audioCodingInUse() const { return m_audioCodingInUse; }
    QString audioBuffer() const;
    void setAudioBuffer(const QString& mode);
    void applyAudioBuffer();
    QString networkQuality() const;
    double cpuPercent() const { return m_cpuPercent; }
    double cpuPercentOfCore() const { return m_cpuPercentOfCore; }
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
    Q_INVOKABLE void setRadioSetup(const QString& key, const QVariant& value);
    // A radio with no callsign looks new: offer setup once per radio.
    Q_INVOKABLE bool setupSuggested() const;
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
    Q_INVOKABLE void setDspLevel(const QString& key, int level);
    // Beat cancel (BC, client-side): remembered; runs in voice modes only.
    bool beatCancelSetting() const;
    bool beatCancelMode() const;
    void applyBeatCancel();
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
    bool cwDecoderEnabled() const;
    bool locked() const;
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
    QVariantMap digi() const;
    QVariantList digiDecodes() const { return m_digiDecodes; }
    QVariantList digiFrequencies() const;
    // Calls CQ in the next slot, and keeps calling until someone answers.
    Q_INVOKABLE void digiCallCq();
    // Answers the station of a decode (an index into digiDecodes).
    Q_INVOKABLE void digiAnswer(int index);
    // Stops transmitting at once and ends the QSO.
    Q_INVOKABLE void digiHalt();
    // Tunes to the protocol's dial frequency on a band, in DIGU.
    Q_INVOKABLE void digiTune(double mhz);
    // Fits the display to the audio band (4 kHz), for picking a TX tone.
    Q_INVOKABLE void digiZoom();

    bool radeAvailable() const;
    bool radeActive() const;
    void setRadeActive(bool on);
    QVariantMap radeStatus() const;
    // Contacts with this call before, and whether it is a dupe on this band
    // and mode: {worked, dupe}.
    Q_INVOKABLE QVariantMap workedInfo(const QString& call) const;
    // QRZ.com account for the lookup: the password goes to the platform's
    // secure store, never to the settings. Empty user removes it.
    Q_INVOKABLE void setQrzAccount(const QString& user, const QString& password);
    // Opens the share sheet with the log as an ADIF file
    // ("OksaSDR-log-<date>.adi"): mail, Files, AirDrop, a logbook app, or
    // TQSL for LoTW. False when there is nothing to share.
    Q_INVOKABLE bool shareQsoLog();
    // Adds the contacts of an ADIF file (a file or content URL from the file
    // picker) to the log, skipping those already in it (call, date, time,
    // band, mode). Returns imported, duplicates and error.
    Q_INVOKABLE QVariantMap importAdif(const QUrl& url);
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
    bool wfAutoLevel() const { return displayInt("MobileWfAuto", 1) != 0; }
    void setWfAutoLevel(bool on) { setDisplayInt("MobileWfAuto", on ? 1 : 0); }
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
    // Each side falls back to the old shared setting.
    bool analogLeft() const { return displayInt("MobileMeterAnalogLeft", displayInt("MobileMeterAnalog", 0)) != 0; }
    bool analogRight() const { return displayInt("MobileMeterAnalogRight", displayInt("MobileMeterAnalog", 0)) != 0; }
    Q_INVOKABLE void setMeterAnalog(const QString& side, bool on)
    {
        setDisplayInt(side == QLatin1String("left") ? "MobileMeterAnalogLeft" : "MobileMeterAnalogRight", on ? 1 : 0);
    }
    int meterPeakHold() const { return std::clamp(displayInt("MobileMeterPeakHold", 2), 0, 3); }
    void setMeterPeakHold(int mode) { setDisplayInt("MobileMeterPeakHold", std::clamp(mode, 0, 3)); }
    QStringList pinnedSections() const;
    void setPinnedSections(const QStringList& sections);
    void setCwDecoderEnabled(bool on);
    QString cwDecodedText() const { return m_cwDecodedText; }
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
    double swr() const { return m_swr; }
    bool swrValid() const { return m_swrValid; }
    // Radio actually on the air (MOX, tune, ATU, VOX); TX-only meters show
    // a value only then.
    bool radioTransmitting() const;
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
    // side "left"/"right", tx: the transmit choice instead of the receive one.
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
    // A FlexRadio by address, for networks where discovery broadcasts do not
    // arrive (iOS needs a multicast entitlement to receive them).
    Q_INVOKABLE void connectToAddress(const QString& host);
    Q_INVOKABLE void signInSmartLink(const QString& email, const QString& password);
    Q_INVOKABLE void signOutSmartLink();
    // Connect to the radio at `index` in smartLinkRadios.
    Q_INVOKABLE void connectSmartLinkRadio(int index);
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
    // Ends every transmission this client keyed (PTT, tune, ATU cycle).
    Q_INVOKABLE void stopTransmit(const QString& reason);
    // Switch the radio's transmit audio input to this device's microphone.
    Q_INVOKABLE void useDeviceMicrophone();
    // Dev/screenshot runs: leave the radio's mic input alone.
    void setMicDefaultEnabled(bool enabled) { m_micDefaultEnabled = enabled; }
    // Screenshots for public pages: the radio's serial number reads "(hidden)".
    void setRedactIdentity(bool on) { m_redactIdentity = on; }
    // Tune, recentring the pan when the target lies outside it.
    Q_INVOKABLE void tuneTo(double mhz);
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
    Q_INVOKABLE void sendCwx(const QString& text);
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
    void buttonStyleChanged();
    void pttToggleChanged();
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
    void transmitStateChanged();
    void filterChanged();
    void lastAddressChanged();
    void controlsVisibleChanged();
    void smartLinkChanged();
    void smartLinkRadiosChanged();
    // The radio presented a different TLS certificate than the one pinned
    // for it; the connect waits for answerCertificate().
    void certificateChanged(const QString& host, const QString& expected, const QString& presented);
    void rfPowerChanged();
    void micGainChanged();
    void txMetersChanged();
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
    void phoneChanged();
    void panDisplayChanged();
    void dvkChanged();
    // The connected-stations list was asked for (Radio Setup); the operate
    // page opens it.
    void stationsRequested();
    void cwChanged();
    void cwKeyDownChanged();
    void cwDecoderChanged();
    void displayLevelsChanged();
    void lockedChanged();
    void rfGainChanged();
    void memoriesChanged();
    void qsoLogChanged();
    void digiChanged();
    void radeChanged();
    void digiDecodesChanged();
    void callLookupChanged();
    void lastSpotCallChanged();
    void qrzChanged();
    void tnfsChanged();
    void txChainChanged();
    void spotsChanged();
    void dxClusterChanged();
    void profilesChanged();
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
    void connectWithInfo(const RadioInfo& info, const QString& displayName);
    void wireTransmit();
    void wireTxPacketSink();
    void applyTxAudioCapabilities(bool connected, const RadioCapabilities& caps);
    bool ensureMicrophonePermission();
    void startDeviceMicrophoneCapture();
    void stopDeviceMicrophoneCapture(bool force = false);
    void updateMicCheck();
    void releasePtt(const QString& reason);
    void wireSmartLink();
    void wireCw();
    void updateCwDecoder();
    void startWanConnect();
    void setSmartLinkState(const QString& state);

    RadioSession m_session;
    RadioListModel m_radios;
    std::unique_ptr<RadioDiscoverySource> m_discovery;
    QThread* m_audioThread{nullptr};
    AudioEngine* m_audio{nullptr};
    BandPlanManager m_bandPlan;
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
    // Per-thread CPU (Linux and Android, from /proc/self/task): ticks at
    // the last sample, by thread id.
    QHash<int, quint64> m_threadTicks;
    QStringList m_busyThreads;
    void sampleThreads(quint64 elapsedUs);
    std::atomic<int> m_framesThisPeriod{0};
    int m_uiFps{0};
    // Audio diagnostics: underruns seen at the last 10 s check.
    quint64 m_lastUnderruns{0};
    QTimer m_audioDiagTimer;
    // Sets the core's AudioCompression for the coming connection.
    void applyAudioCompression(const QHostAddress& radio, bool wan);
    bool m_redactIdentity{false};
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
    QList<QObject*> m_micCheckHolders;   // shown TX tabs
    bool m_micCheckOpen{false};          // the microphone is open for them
    std::unique_ptr<QsoLog> m_qsoLog;

    // ---- FT8 / FT4 (MobileEngineDigi.cpp) ----
    void setupDigi();
    void syncDigiAudio();
    void onDigiSlot(qint64 slotMs);
    void handleDigiDecodes(const QVariantList& decodes, qint64 slotMs);
    void startDigiTx(qint64 slotMs);
    void onDigiTxFinished();
    void digiLogQso();
    void digiEnd(const QString& status);
    void setDigiStatus(const QString& status);
    QString digiMyCall() const;
    QString digiReport(int snr) const;
    // The next message of a kind: cq, call, report, rreport, rr73, 73.
    QString digiMessage(const QString& kind) const;
    std::unique_ptr<Ft8Engine> m_ft8;
    std::unique_ptr<DecoderAudioModel> m_digiAudio;
    QVariantList m_digiDecodes;
    QString m_digiState{QStringLiteral("idle")};
    QString m_digiDx;
    QString m_digiDxGrid;
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

    // ---- RADE (MobileEngineRade.cpp) ----
    void activateRade();
    void deactivateRade();
    QThread* m_radeThread{nullptr};
    RADEEngine* m_radeEngine{nullptr};
    int m_radeSliceId{-1};
    bool m_radePrevMute{false};
    QString m_radePrevMode;
    int m_radePrevFilterLow{0};
    int m_radePrevFilterHigh{0};
    bool m_radeAssignedDax{false};
    bool m_radePrevDax{false};
    int m_radeDaxChannel{0};
    TransmitModel::PttRelease m_radePttRelease;
    quint64 m_radeEooRequestId{0};
    bool m_radeEooPending{false};
    bool m_radeTxActive{false};
    std::shared_ptr<std::atomic<bool>> m_radeFallbackFence;
    QList<QMetaObject::Connection> m_radeConnections;
    bool m_radeSynced{false};
    float m_radeSnr{0.0f};
    QString m_radeRxCallsign;
    std::unique_ptr<CtyDatParser> m_cty;
    QString m_lookupCall;                // the call callLookup is for
    QString m_qrzStatus;                 // QRZ.com sign-in check, as shown
    QString m_qrzStatusKind;             // "checking", "ok", "failed" or ""
    QVariantMap m_callLookup;
    QString m_lastSpotCall;
    void applyCallInfo(const CallsignInfo& info);
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
    // Listening to one of the app's own clips on this device: the clip as
    // 24 kHz stereo float, fed into the receive output in real time.
    // TX chain settings are written a moment after the last change (a
    // slider drag would otherwise write the settings file on every step).
    QTimer m_txChainSaveTimer;
    bool m_pttLatched{false};
    QTimer m_pttToggleLimit;
    // DX cluster: its spots go to the radio ("spot add", as the desktop),
    // deduplicated per call within 1 kHz and sent at most RbnRateLimit a
    // second; the radio shows them to every client and brings them back
    // into its spot list.
    DxClusterClient* m_dx{nullptr};
    bool m_dxWanted{false};
    QString m_dxState{QStringLiteral("off")};
    QString m_dxError;
    QHash<QString, QPair<double, qint64>> m_dxSeen;
    QStringList m_dxQueue;
    QTimer m_dxSendTimer;
    QTimer m_spotsNotifyTimer;   // at most two spot list updates a second
    QTimer m_spotsAgeTimer;      // spots age out of the age filter
    void setupDxCluster();
    QString dxCallsign() const;
    QTimer m_voicePreviewTimer;
    QElapsedTimer m_voicePreviewClock;
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
    QString m_cwDecodedText;
    QString m_cwDecoderStats;
    double m_atuTunedFreqMhz{-1.0};
    double m_compressionDb{0.0};
    double m_alcDbfs{-100.0};
    bool m_alcAvailable{false};
    double m_paTempC{0.0};
    double m_supplyVolts{0.0};
    bool m_pttHeld{false};
    bool m_micPermissionRequested{false};
    bool m_connected{false};
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
    bool m_wanActive{false};
    bool m_wanUserDisconnect{false};
    bool m_smartLinkRetried{false};   // one re-sign-in after a refused session
    QTimer m_wanReconnectTimer;
    QString m_smartLinkState{QStringLiteral("signedOut")};
    double m_micLevelDbfs{-100.0};
    double m_micPeakDbfs{-100.0};
    double m_forwardPowerW{0.0};
    double m_swr{1.0};
    bool m_swrValid{false};
    QPointer<SliceModel> m_slice;
    QPointer<PanadapterModel> m_pan;
    QString m_connectionState{QStringLiteral("idle")};
    QString m_lastError;
    QString m_radioName;
    double m_sMeterDbm{-130.0};
    QTimer m_panPixelsTimer;
    QTimer m_panMoveTimer;
    double m_pendingPanCenterMhz{0.0};
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
