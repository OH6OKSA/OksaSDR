// The Digi tab's keyboard modes (RTTY; PSK31 next): macros and typed text
// transmitted as audio from this device, the way FT8 / FT4 go out (PTT for
// digital audio, the TX slice, the speech processor off, the FTx TX level).
// SmartSDR's RTTY mode transmits the AFSK tones sent to it, so RTTY goes
// out the same in the RTTY mode, DIGL and DIGU, at the tones the way
// receives: the mark always the higher radio frequency (Reverse is for
// receiving a station sent the other way round, not for sending).
//
// The macros follow fldigi's new-install set and the QSO bpsk31.com
// describes ("CQ CQ CQ de CALL CALL CALL K", "CALL de MYCALL ... K", BTU,
// SK), with the CW memories' placeholders: {CALL} = this station, {HIS} =
// the call being worked (the Log tab's), {NAME} = that station's first name
// from the lookup, {RST} = 599, {GRID} = this station's locator.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/RttyEncoder.h"
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
    QString shown = RttyEncoder::sendable(expanded).trimmed();
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
    if (!rttyActive()) {
        return refuse(tr("Choose how RTTY is received first (Digi › Receive in): RTTY, DIGL or DIGU."));
    }
    if (!transmitAvailable()) {
        return refuse(transmitUnavailableReason());
    }
    if (txLocked()) {
        return refuse(tr("Transmit is locked (SB): hold PTT for a second to arm it."));
    }
    if (!m_slice->isTxSlice()) {
        return refuse(tr("Slice %1 is not the TX slice: make it TX to send RTTY from it.").arg(m_slice->letter()));
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
    if (RttyEncoder::sendable(expanded).trimmed().isEmpty()) {
        return refuse(tr("Nothing RTTY can send in that text (letters, figures and . , ? / ( ) - : ' go)."));
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
    // The tones as this way receives them, the mark the higher radio
    // frequency: the lower tone on lower sideband (the RTTY mode, DIGL), the
    // upper on upper sideband (DIGU). Reverse is left out on purpose.
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
    p.amplitude = float(std::pow(10.0, m_digiTxLevelDb / 20.0));
    QString sent;
    const QByteArray audio = RttyEncoder::encode(text, p, &sent);
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
        setPtt(true, QStringLiteral("RTTY transmit"));
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
    qCInfo(lcMobile).noquote() << QStringLiteral("text TX: RTTY in %1, mark %2 Hz, space %3 Hz, %4 baud, %5 characters, %6 s, TX level %7 dB")
                                      .arg(via)
                                      .arg(p.markHz)
                                      .arg(p.spaceHz)
                                      .arg(p.baud)
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
    releasePtt(QStringLiteral("RTTY sent"));
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

} // namespace AetherSDR::mobile
