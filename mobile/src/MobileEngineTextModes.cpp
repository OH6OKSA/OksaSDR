// The Digi tab's keyboard modes (RTTY, PSK31, PSK63): macros and typed text
// transmitted as audio from this device, the way FT8 / FT4 go out (PTT for
// digital audio, the TX slice, the speech processor off, the FTx TX level).
// SmartSDR's RTTY mode transmits the AFSK tones sent to it, so RTTY goes
// out the same in the RTTY mode, DIGL and DIGU, at the tones the way
// receives: the mark always the higher radio frequency (Reverse is for
// receiving a station sent the other way round, not for sending).
//
// PSK31 / PSK63 (G3PLX, bpsk31.com) run in DIGU, as the standard has them
// (upper sideband; PSK itself is the same on either): the audio frequency of
// the carrier is picked by a tap on the signal, AFC follows it, and text
// goes out on the frequency it is received on, as fldigi sends by default.
//
// The macros follow fldigi's new-install set and the QSO bpsk31.com
// describes ("CQ CQ CQ de CALL CALL CALL K", "CALL de MYCALL ... K", BTU,
// SK), with the CW memories' placeholders: {CALL} = this station, {HIS} =
// the call being worked (the Log tab's), {NAME} = that station's first name
// from the lookup, {RST} = 599, {GRID} = this station's locator.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/PskDecoder.h"
#include "core/PskEncoder.h"
#include "core/RttyEncoder.h"
#include "models/DecoderAudioModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimer>

#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr int kTextMacroCount = 8;
constexpr int kTextSampleRate = 24000;  // playTxDigital's rate
const QString kTextModeKey = QStringLiteral("MobileDigiTextMode");
const QString kPskCarrierKey = QStringLiteral("MobilePskCarrier");
const QString kPskAfcKey = QStringLiteral("MobilePskAfc");
constexpr int kPskDefaultCarrierHz = 1000;

bool isPsk(const QString& mode)
{
    return mode == QLatin1String("PSK31") || mode == QLatin1String("PSK63");
}

struct MacroDefault {
    const char* label;
    const char* text;
};
const MacroDefault kTextMacroDefaults[kTextMacroCount] = {
    {"CQ", "CQ CQ CQ DE {CALL} {CALL} {CALL}\nCQ CQ CQ DE {CALL} {CALL} {CALL} PSE K\n"},
    {"Answer", "{HIS} {HIS} DE {CALL} {CALL} {CALL} KN\n"},
    {"QSO", "{HIS} DE {CALL}\nTNX FOR THE CALL. UR RST {RST} {RST}\nBTU {HIS} DE {CALL} K\n"},
    {"BTU", "BTU {HIS} DE {CALL} K\n"},
    {"73 SK", "TNX FOR QSO {NAME} 73\n{HIS} DE {CALL} SK\n"},
    {"QRZ?", "QRZ? DE {CALL} K\n"},
    {"AGN?", "AGN AGN PSE {HIS} DE {CALL} K\n"},
    {"Locator", "MY LOCATOR IS {GRID} {GRID}\n"},
};

QString macroLabelKey(int index)
{
    return QStringLiteral("MobileTextMacroLabel%1").arg(index + 1);
}

QString macroTextKey(int index)
{
    return QStringLiteral("MobileTextMacro%1").arg(index + 1);
}
} // namespace

QVariantList MobileEngine::textMacros() const
{
    QVariantList list;
    for (int i = 0; i < kTextMacroCount; ++i) {
        list << QVariantMap{
            {QStringLiteral("label"), AppSettings::instance().value(macroLabelKey(i), QString::fromLatin1(kTextMacroDefaults[i].label)).toString()},
            {QStringLiteral("text"), AppSettings::instance().value(macroTextKey(i), QString::fromLatin1(kTextMacroDefaults[i].text)).toString()},
        };
    }
    return list;
}

void MobileEngine::setTextMacro(int index, const QString& label, const QString& text)
{
    if (index < 0 || index >= kTextMacroCount) {
        return;
    }
    if (label.trimmed() == QLatin1String(kTextMacroDefaults[index].label)
        && text == QLatin1String(kTextMacroDefaults[index].text)) {
        AppSettings::instance().remove(macroLabelKey(index));
        AppSettings::instance().remove(macroTextKey(index));
    } else {
        AppSettings::instance().setValue(macroLabelKey(index), label.trimmed());
        AppSettings::instance().setValue(macroTextKey(index), text);
    }
    AppSettings::instance().save();
    emit textMacrosChanged();
}

QVariantMap MobileEngine::defaultTextMacro(int index) const
{
    if (index < 0 || index >= kTextMacroCount) {
        return {};
    }
    return {{QStringLiteral("label"), QString::fromLatin1(kTextMacroDefaults[index].label)},
            {QStringLiteral("text"), QString::fromLatin1(kTextMacroDefaults[index].text)}};
}

QStringList MobileEngine::textCalls() const
{
    return heardCallsIn(m_rttyDecodedText);
}

QString MobileEngine::expandTextMacro(const QString& text, QString* error) const
{
    QString out = text;
    const QString call = m_session.radioModel().callsign().trimmed().toUpper();
    if (out.contains(QLatin1String("{CALL}"))) {
        if (call.isEmpty()) {
            *error = tr("The radio has no callsign for {CALL}. Set it in Radio Setup (Setup › Radio › Callsign).");
            return {};
        }
        out.replace(QLatin1String("{CALL}"), call);
    }
    if (out.contains(QLatin1String("{HIS}"))) {
        if (m_workedCall.isEmpty()) {
            *error = tr("No call for {HIS}: tap a heard call on the Digi tab or type it in the Log tab's Call field.");
            return {};
        }
        out.replace(QLatin1String("{HIS}"), m_workedCall);
    }
    if (out.contains(QLatin1String("{GRID}"))) {
        if (myGrid().isEmpty()) {
            *error = tr("No locator for {GRID}: set it in Radio Setup or the FTx tab.");
            return {};
        }
        out.replace(QLatin1String("{GRID}"), myGrid().toUpper());
    }
    out.replace(QLatin1String("{RST}"), QStringLiteral("599"));
    // The operator's first name from the lookup (QRZ's fname); a club's or
    // a special call's name is no first name (OE100OEVSV's "Austrian Amateur
    // Radio Society" went out as "TNX FOR QSO AUSTRIAN 73"). Without one the
    // word goes and the line reads on ("TNX FOR QSO 73").
    QString name;
    if (m_callLookup.value(QStringLiteral("call")).toString().compare(m_workedCall, Qt::CaseInsensitive) == 0) {
        const QString first = m_callLookup.value(QStringLiteral("firstName")).toString().simplified();
        if (!first.isEmpty() && first.count(QLatin1Char(' ')) <= 1 && !first.contains(QLatin1Char('"'))) {
            name = first.section(QLatin1Char(' '), 0, 0).toUpper();
        }
    }
    out.replace(QLatin1String("{NAME}"), name);
    static const QRegularExpression spaces(QStringLiteral("[ ]{2,}"));
    out.replace(spaces, QStringLiteral(" "));
    return out;
}

QString MobileEngine::previewText(const QString& text) const
{
    QString error;
    const QString expanded = expandTextMacro(text, &error);
    if (!error.isEmpty()) {
        return error;
    }
    QString shown = (pskActive() ? PskEncoder::sendable(expanded) : RttyEncoder::sendable(expanded)).trimmed();
    shown.replace(QStringLiteral("\r\n"), QStringLiteral(" / "));
    return shown;
}

bool MobileEngine::sendTextMacro(int index)
{
    const QVariantList list = textMacros();
    if (index < 0 || index >= list.size()) {
        return false;
    }
    return sendText(list.at(index).toMap().value(QStringLiteral("text")).toString());
}

bool MobileEngine::sendText(const QString& text)
{
    const auto refuse = [this](const QString& why) {
        qCInfo(lcMobile).noquote() << "text TX refused:" << why;
        setLastError(why);
        return false;
    };
    if (!m_connected || !m_slice) {
        return refuse(tr("Not connected to a radio."));
    }
    if (!textModeActive()) {
        return refuse(tr("Choose the mode first on the Digi tab: RTTY (and how it is received), PSK31 or PSK63."));
    }
    if (!transmitAvailable()) {
        return refuse(transmitUnavailableReason());
    }
    if (txLocked()) {
        return refuse(tr("Transmit is locked (SB): hold PTT for a second to arm it."));
    }
    if (!m_slice->isTxSlice()) {
        return refuse(tr("Slice %1 is not the TX slice: make it TX to send %2 from it.").arg(m_slice->letter(), textMode()));
    }
    if (m_digiTransmitting || m_digiState != QLatin1String("idle")) {
        return refuse(tr("FT8 / FT4 is transmitting: halt it on the FTx tab first."));
    }
    if (m_pttHeld && !m_textTransmitting) {
        return refuse(tr("PTT is held: release it first."));
    }
    QString error;
    const QString expanded = expandTextMacro(text, &error);
    if (!error.isEmpty()) {
        return refuse(error);
    }
    if (pskActive() ? PskEncoder::sendable(expanded).trimmed().isEmpty() : RttyEncoder::sendable(expanded).trimmed().isEmpty()) {
        return refuse(pskActive() ? tr("Nothing PSK can send in that text (ASCII goes).")
                                  : tr("Nothing RTTY can send in that text (letters, figures and . , ? / ( ) - : ' go)."));
    }
    m_textTxQueue << expanded;
    if (!m_textTransmitting) {
        startTextTx();
    } else {
        qCInfo(lcMobile) << "text TX: queued after the text going out," << m_textTxQueue.size() << "waiting";
    }
    return true;
}

void MobileEngine::startTextTx()
{
    if (m_textTxQueue.isEmpty() || !m_slice) {
        return;
    }
    const QString text = m_textTxQueue.takeFirst();
    const double amplitude = std::pow(10.0, m_digiTxLevelDb / 20.0);
    QString sent;
    QByteArray audio;
    QString what;
    if (pskActive()) {
        // On the frequency it is received on (AFC's), as fldigi sends.
        const QVariantMap k = psk();
        PskEncoder::Params p;
        p.carrierHz = k.value(QStringLiteral("trackedHz")).toDouble();
        p.baud = k.value(QStringLiteral("baud")).toDouble();
        p.sampleRate = kTextSampleRate;
        p.amplitude = amplitude;
        audio = PskEncoder::encode(text, p, &sent);
        what = QStringLiteral("%1 in %2 at %3 Hz (%4 MHz)")
                   .arg(k.value(QStringLiteral("mode")).toString(), k.value(QStringLiteral("via")).toString())
                   .arg(p.carrierHz, 0, 'f', 1)
                   .arg(k.value(QStringLiteral("carrierMhz")).toDouble(), 0, 'f', 6);
    } else {
        // The tones as this way receives them, the mark the higher radio
        // frequency: the lower tone on lower sideband (the RTTY mode, DIGL),
        // the upper on upper sideband (DIGU). Reverse is left out on purpose.
        const QVariantMap r = rtty();
        const QString via = r.value(QStringLiteral("via")).toString();
        const double tone = r.value(QStringLiteral("markHz")).toDouble();
        const double shift = r.value(QStringLiteral("shiftHz")).toDouble();
        const bool usb = via == QLatin1String("DIGU");
        RttyEncoder::Params p;
        p.markHz = usb ? tone + shift : tone;
        p.spaceHz = usb ? tone : tone + shift;
        p.baud = r.value(QStringLiteral("baud")).toDouble();
        p.sampleRate = kTextSampleRate;
        p.amplitude = amplitude;
        audio = RttyEncoder::encode(text, p, &sent);
        what = QStringLiteral("RTTY in %1, mark %2 Hz, space %3 Hz, %4 baud").arg(via).arg(p.markHz).arg(p.spaceHz).arg(p.baud);
    }
    const int durationMs = int(qint64(audio.size() / int(sizeof(float))) * 1000 / kTextSampleRate);

    RadioModel& rm = m_session.radioModel();
    if (!m_textTransmitting) {
        rm.setDigitalVoiceTxSlice(m_slice->sliceId());
        // As for FT8 / FT4: a speech processor left on distorts the tones.
        if (procEnabled()) {
            m_textProcRestore = true;
            setProcEnabled(false);
        }
        m_digiKeying = true;
        setPtt(true, QStringLiteral("%1 transmit").arg(textMode()));
        m_digiKeying = false;
        if (!m_pttHeld) {
            rm.setDigitalVoiceTxSlice(-1);
            if (m_textProcRestore) {
                m_textProcRestore = false;
                setProcEnabled(true);
            }
            m_textTxQueue.clear();
            setLastError(tr("Transmit refused."));
            return;
        }
        m_textTransmitting = true;
        emit textTxChanged();
    }
    // What goes out shows in the received text as this station's ([TX]
    // lines, which the heard calls leave out).
    QString shown = sent;
    shown.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = shown.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    for (QString& line : lines) {
        line.prepend(QStringLiteral("[TX] "));
    }
    const bool atLineStart = m_rttyDecodedText.isEmpty() || m_rttyDecodedText.endsWith(QLatin1Char('\n'));
    appendRttyDecoded((atLineStart ? QString() : QStringLiteral("\n")) + lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    emit rttyDecoderChanged();

    AudioEngine* engine = m_audio;
    QMetaObject::invokeMethod(engine, [engine, audio] { engine->playTxDigital(audio); }, Qt::QueuedConnection);
    // Never left keyed: if the audio has not ended 2 s after it should, stop.
    const int seq = ++m_textTxSeq;
    QTimer::singleShot(durationMs + 2000, this, [this, seq] {
        if (m_textTransmitting && seq == m_textTxSeq) {
            qCWarning(lcMobile) << "text TX: the audio did not end in time; unkeying";
            stopTextTx();
        }
    });
    qCInfo(lcMobile).noquote() << QStringLiteral("text TX: %1, %2 characters, %3 s, TX level %4 dB")
                                      .arg(what)
                                      .arg(sent.size())
                                      .arg(durationMs / 1000.0, 0, 'f', 1)
                                      .arg(m_digiTxLevelDb);
}

void MobileEngine::onTextTxFinished()
{
    // More waiting: on without unkeying (each has its own mark lead).
    if (!m_textTxQueue.isEmpty() && m_pttHeld) {
        startTextTx();
        return;
    }
    m_textTransmitting = false;
    ++m_textTxSeq;
    releasePtt(QStringLiteral("%1 sent").arg(textMode()));
    if (m_textProcRestore) {
        m_textProcRestore = false;
        setProcEnabled(true);
    }
    m_session.radioModel().setDigitalVoiceTxSlice(-1);
    qCInfo(lcMobile) << "text TX: done";
    emit textTxChanged();
}

void MobileEngine::stopTextTx()
{
    m_textTxQueue.clear();
    if (!m_textTransmitting) {
        return;
    }
    QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
    appendRttyDecoded(QStringLiteral("[TX] (stopped)\n"));
    onTextTxFinished();
    emit rttyDecoderChanged();
}

QString MobileEngine::textMode() const
{
    // The radio's RTTY mode is RTTY whatever was chosen last.
    if (m_slice && m_slice->mode() == QLatin1String("RTTY")) {
        return QStringLiteral("RTTY");
    }
    const QString mode = AppSettings::instance().value(kTextModeKey, QStringLiteral("RTTY")).toString();
    return isPsk(mode) ? mode : QStringLiteral("RTTY");
}

bool MobileEngine::pskActive() const
{
    if (!m_slice || !isPsk(textMode()) || m_digiOn || radeActive()) {
        return false;
    }
    const QString mode = m_slice->mode();
    return mode == QLatin1String("DIGU") || mode == QLatin1String("DIGL");
}

QVariantMap MobileEngine::psk() const
{
    const QString mode = isPsk(textMode()) ? textMode() : QStringLiteral("PSK31");
    const double carrier = std::clamp(AppSettings::instance().value(kPskCarrierKey, QString::number(kPskDefaultCarrierHz)).toDouble(), 200.0, 3000.0);
    const bool running = m_pskDecoder && m_pskDecoder->isRunning();
    const double tracked = running && m_pskTrackedHz > 0.0 ? m_pskTrackedHz : carrier;
    const QString via = m_slice ? m_slice->mode() : QString();
    const double sign = via == QLatin1String("DIGL") ? -1.0 : 1.0;
    const double dial = m_slice ? m_slice->frequency() : 0.0;
    return {{QStringLiteral("mode"), mode},
            {QStringLiteral("baud"), mode == QLatin1String("PSK63") ? 62.5 : 31.25},
            {QStringLiteral("carrierHz"), carrier},
            {QStringLiteral("trackedHz"), tracked},
            {QStringLiteral("carrierMhz"), dial + sign * tracked / 1e6},
            {QStringLiteral("afc"), AppSettings::instance().value(kPskAfcKey, QStringLiteral("True")).toString() == QLatin1String("True")},
            {QStringLiteral("quality"), running ? std::max(0, m_pskQuality) : 0},
            {QStringLiteral("locked"), running && m_pskLocked},
            {QStringLiteral("via"), via}};
}

void MobileEngine::setTextMode(const QString& mode)
{
    if (!m_slice || (mode != QLatin1String("RTTY") && !isPsk(mode))) {
        return;
    }
    if (mode == QLatin1String("RTTY")) {
        AppSettings::instance().setValue(kTextModeKey, mode);
        AppSettings::instance().save();
        // RTTY's standard way unless RTTY is already received some way.
        if (!rttyActive()) {
            setRttyVia(QStringLiteral("RTTY"));
        } else {
            updateRttyDecoder();
        }
        qCInfo(lcMobile) << "Digi text mode RTTY";
        return;
    }
    AppSettings::instance().setValue(kTextModeKey, mode);
    // RTTY's DIGL / DIGU choice gives way; FT8 / FT4 decode every DIGU slice.
    AppSettings::instance().setValue(QStringLiteral("MobileRttyDigital"), QStringLiteral("False"));
    AppSettings::instance().save();
    if (m_digiOn) {
        qCInfo(lcMobile) << mode << "on the Digi tab: FT8 / FT4 decoding off";
        setDigiRunning(false);
    }
    qCInfo(lcMobile) << "Digi text mode" << mode << "on slice" << m_slice->letter();
    if (m_slice->mode() != QLatin1String("DIGU") && m_slice->mode() != QLatin1String("DIGL")) {
        setMode(QStringLiteral("DIGU"));  // modeChanged brings the decoder
    } else {
        updateRttyDecoder();
    }
}

void MobileEngine::setPsk(const QString& key, const QVariant& value)
{
    if (key == QLatin1String("carrier")) {
        const double hz = std::clamp(value.toDouble(), 200.0, 3000.0);
        AppSettings::instance().setValue(kPskCarrierKey, QString::number(std::lround(hz)));
        m_pskTrackedHz = double(std::lround(hz));
        if (m_pskDecoder) {
            m_pskDecoder->setCarrierHz(double(std::lround(hz)));
        }
        resetRttyGate();
    } else if (key == QLatin1String("afc")) {
        AppSettings::instance().setValue(kPskAfcKey, value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
        if (m_pskDecoder) {
            m_pskDecoder->setAfc(value.toBool());
        }
    } else {
        return;
    }
    AppSettings::instance().save();
    emit rttyDecoderChanged();
}

void MobileEngine::pskTap(double mhz)
{
    if (!pskActive() || !m_slice) {
        return;
    }
    const double sign = m_slice->mode() == QLatin1String("DIGL") ? -1.0 : 1.0;
    const double hz = sign * (mhz - m_slice->frequency()) * 1e6;
    if (hz < 200.0 || hz > 3000.0) {
        setLastError(tr("Tap a PSK signal inside the slice's passband (200-3000 Hz above the frequency in DIGU)."));
        return;
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("PSK tap: %1 MHz, carrier %2 Hz").arg(mhz, 0, 'f', 6).arg(hz, 0, 'f', 1);
    setPsk(QStringLiteral("carrier"), hz);
}

void MobileEngine::updatePskDecoder(bool run)
{
    if (!m_pskDecoder) {
        return;
    }
    if (run) {
        const QVariantMap k = psk();
        m_pskDecoder->setBaud(k.value(QStringLiteral("baud")).toDouble());
        m_pskDecoder->setAfc(k.value(QStringLiteral("afc")).toBool());
        if (!m_pskDecoder->isRunning()) {
            m_pskDecoder->setCarrierHz(k.value(QStringLiteral("carrierHz")).toDouble());
            m_pskTrackedHz = k.value(QStringLiteral("carrierHz")).toDouble();
            m_pskDecoder->start();
            qCInfo(lcMobile) << "PSK decoder on:" << k.value(QStringLiteral("mode")).toString() << "in"
                             << k.value(QStringLiteral("via")).toString() << "carrier" << k.value(QStringLiteral("carrierHz")).toDouble() << "Hz";
        }
    } else if (m_pskDecoder->isRunning()) {
        m_pskDecoder->stop();
        m_pskLocked = false;
        m_pskQuality = 0;
        qCInfo(lcMobile) << "PSK decoder off";
    }
}

void MobileEngine::testPskFeed(const QString& text, double offsetHz)
{
    if (!m_pskDecoder || !m_pskDecoder->isRunning()) {
        qCWarning(lcMobile) << "PSK test: the decoder is not running";
        return;
    }
    const QVariantMap k = psk();
    PskEncoder::Params p;
    p.carrierHz = k.value(QStringLiteral("carrierHz")).toDouble() + offsetHz;
    p.baud = k.value(QStringLiteral("baud")).toDouble();
    p.sampleRate = kTextSampleRate;
    p.amplitude = 0.3;
    // A line of its own, as an over ends.
    const QByteArray audio = PskEncoder::encode(text + QLatin1Char('\n'), p);
    const auto* f = reinterpret_cast<const float*>(audio.constData());
    auto samples = std::make_shared<std::vector<float>>(f, f + audio.size() / int(sizeof(float)));
    // A little noise, as off the air.
    quint32 seed = 12345;
    for (float& v : *samples) {
        seed = seed * 1664525u + 1013904223u;
        v += float((seed >> 8) / double(1 << 24) - 0.5) * 0.06f;
    }
    qCInfo(lcMobile) << "PSK test:" << text.size() << "characters," << samples->size() / double(kTextSampleRate) << "s at"
                     << p.carrierHz << "Hz," << k.value(QStringLiteral("mode")).toString();
    // In real time, 100 ms at a time, the radio's audio held off meanwhile.
    m_rttyAudio->setEnabled(false);
    auto at = std::make_shared<size_t>(0);
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, samples, at, timer] {
        const size_t n = std::min<size_t>(2400, samples->size() - *at);
        if (n == 0 || !m_pskDecoder || !m_pskDecoder->isRunning()) {
            timer->deleteLater();
            updateRttyDecoder();  // the radio's audio back
            return;
        }
        QByteArray stereo(int(n * 2 * sizeof(float)), Qt::Uninitialized);
        auto* out = reinterpret_cast<float*>(stereo.data());
        for (size_t i = 0; i < n; ++i) {
            out[2 * i] = out[2 * i + 1] = (*samples)[*at + i];
        }
        *at += n;
        m_pskDecoder->feedAudio(stereo);
    });
    timer->start();
}

} // namespace AetherSDR::mobile
