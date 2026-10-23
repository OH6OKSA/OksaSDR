// The Digi tab: FT8 and FT4 on this device (Ft8Engine), and the QSO
// sequence WSJT-X runs: CQ or an answer, reports, RR73 / 73, then the log.

#include "MobileEngine.h"

#include "Ft8Engine.h"
#include "QsoLog.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DecoderPcmAdapter.h"
#include "core/LogManager.h"
#include "models/DecoderAudioModel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

using namespace AetherSDR;

namespace AetherSDR::mobile {

namespace {

constexpr int kMaxDecodes = 80;
// The same message this many times without the reply it waits for ends
// the QSO (WSJT-X's Tx watchdog does the same in time).
constexpr int kMaxTries = 6;
constexpr int kMaxCqCalls = 10;
// Keying and the audio path to the radio, so the tones reach the air at the
// nominal 0.5 s into the slot.
constexpr double kTxLatencyS = 0.25;

struct BandFreq {
    const char* band;
    double ft8;
    double ft4;
};
constexpr BandFreq kBandFreqs[] = {
    {"160m", 1.840, 0.0},     {"80m", 3.573, 3.575},    {"60m", 5.357, 0.0},
    {"40m", 7.074, 7.0475},   {"30m", 10.136, 10.140},  {"20m", 14.074, 14.080},
    {"17m", 18.100, 18.104},  {"15m", 21.074, 21.140},  {"12m", 24.915, 24.919},
    {"10m", 28.074, 28.180},  {"6m", 50.313, 50.318},   {"2m", 144.174, 0.0},
};

}  // namespace

void MobileEngine::setupDigi()
{
    RadioModel& rm = m_session.radioModel();
    m_ft8 = std::make_unique<Ft8Engine>();
    m_ft8->setProtocol(AppSettings::instance().value(QStringLiteral("MobileDigiProtocol"), QStringLiteral("FT8")).toString()
                               == QLatin1String("FT4")
                           ? Ft8Engine::Protocol::Ft4
                           : Ft8Engine::Protocol::Ft8);
    m_digiTxHz = std::clamp(AppSettings::instance().value(QStringLiteral("MobileDigiTxHz"), 1200).toInt(), 200, 3000);
    // The selected receiver's audio, apart from the speaker stream (the
    // decoder tap the CW decoder uses too).
    m_digiAudio = std::make_unique<DecoderAudioModel>(rm, DecoderAudioModel::Consumer::Rtty);
    connect(m_digiAudio.get(), &DecoderAudioModel::pcmReady, m_ft8.get(), &Ft8Engine::feed);
    connect(m_digiAudio.get(), &DecoderAudioModel::sourceReset, m_ft8.get(), &Ft8Engine::resetAudio);
    connect(m_ft8.get(), &Ft8Engine::slotStarted, this, &MobileEngine::onDigiSlot);
    connect(m_ft8.get(), &Ft8Engine::decoded, this, [this](const QVector<Ft8Engine::Decode>& decodes, qint64 slotMs) {
        const QString my = digiMyCall();
        const qint64 slotLen = qint64(m_ft8->slotSeconds() * 1000);
        const QString utc = QDateTime::fromMSecsSinceEpoch(slotMs, QTimeZone::UTC).toString(QStringLiteral("HHmmss"));
        QVariantList list;
        for (const Ft8Engine::Decode& d : decodes) {
            const Ft8Engine::Parsed p = Ft8Engine::parse(d.text);
            list.append(QVariantMap{{QStringLiteral("utc"), utc},
                                    {QStringLiteral("snr"), d.snr},
                                    {QStringLiteral("dt"), QString::number(d.dt, 'f', 1)},
                                    {QStringLiteral("freq"), int(std::lround(d.freq))},
                                    {QStringLiteral("text"), d.text},
                                    {QStringLiteral("call"), p.de},
                                    {QStringLiteral("cq"), p.cq},
                                    {QStringLiteral("toMe"), !my.isEmpty() && p.to == my},
                                    {QStringLiteral("slotOdd"), (slotMs / slotLen) % 2 == 1}});
        }
        qCInfo(lcMobile).noquote() << QStringLiteral("digi: %1 %2 decodes").arg(utc).arg(list.size());
        for (const QVariant& v : std::as_const(list)) {
            const QVariantMap m = v.toMap();
            qCDebug(lcMobile).noquote() << QStringLiteral("digi: %1 %2 %3 %4 %5")
                                               .arg(utc)
                                               .arg(m.value(QStringLiteral("snr")).toInt(), 3)
                                               .arg(m.value(QStringLiteral("dt")).toString())
                                               .arg(m.value(QStringLiteral("freq")).toInt(), 4)
                                               .arg(m.value(QStringLiteral("text")).toString());
        }
        handleDigiDecodes(list, slotMs);
    });
}

bool MobileEngine::digiRunning() const
{
    return m_ft8 && m_ft8->running();
}

void MobileEngine::setDigiRunning(bool on)
{
    if (!m_ft8 || on == m_ft8->running()) {
        return;
    }
    if (!on) {
        digiHalt();
    }
    m_ft8->setRunning(on);
    qCInfo(lcMobile) << "digi:" << (on ? "decoding on" : "decoding off") << digiProtocol();
    syncDigiAudio();
    emit digiChanged();
}

void MobileEngine::syncDigiAudio()
{
    if (!m_digiAudio) {
        return;
    }
    const bool run = m_connected && digiRunning() && m_slice;
    m_digiAudio->setSlice(run ? m_slice.data() : nullptr);
    m_digiAudio->setEnabled(run);
}

QString MobileEngine::digiProtocol() const
{
    return m_ft8 && m_ft8->protocol() == Ft8Engine::Protocol::Ft4 ? QStringLiteral("FT4") : QStringLiteral("FT8");
}

void MobileEngine::setDigiProtocol(const QString& protocol)
{
    if (!m_ft8 || protocol == digiProtocol()) {
        return;
    }
    digiHalt();
    m_ft8->setProtocol(protocol == QLatin1String("FT4") ? Ft8Engine::Protocol::Ft4 : Ft8Engine::Protocol::Ft8);
    m_ft8->resetAudio();
    AppSettings::instance().setValue(QStringLiteral("MobileDigiProtocol"), digiProtocol());
    AppSettings::instance().save();
    m_digiDecodes.clear();
    emit digiDecodesChanged();
    emit digiChanged();
}

void MobileEngine::setDigiTxHz(int hz)
{
    hz = std::clamp(hz, 200, 3000);
    if (hz == m_digiTxHz) {
        return;
    }
    m_digiTxHz = hz;
    AppSettings::instance().setValue(QStringLiteral("MobileDigiTxHz"), hz);
    AppSettings::instance().save();
    emit digiChanged();
}

QVariantMap MobileEngine::digi() const
{
    return {{QStringLiteral("state"), m_digiState},
            {QStringLiteral("dx"), m_digiDx},
            {QStringLiteral("txMsg"), m_digiTxMsg},
            {QStringLiteral("status"), m_digiStatus},
            {QStringLiteral("transmitting"), m_digiTransmitting},
            {QStringLiteral("parity"), m_digiTxParity == 1 ? QStringLiteral("odd") : QStringLiteral("even")},
            {QStringLiteral("slotSeconds"), m_ft8 ? m_ft8->slotSeconds() : 15.0},
            {QStringLiteral("myCall"), digiMyCall()}};
}

QVariantList MobileEngine::digiFrequencies() const
{
    const bool ft4 = digiProtocol() == QLatin1String("FT4");
    QVariantList out;
    for (const BandFreq& b : kBandFreqs) {
        const double mhz = ft4 ? b.ft4 : b.ft8;
        if (mhz > 0.0) {
            out.append(QVariantMap{{QStringLiteral("band"), QString::fromLatin1(b.band)}, {QStringLiteral("mhz"), mhz}});
        }
    }
    return out;
}

QString MobileEngine::digiMyCall() const
{
    return m_session.radioModel().callsign().trimmed().toUpper();
}

QString MobileEngine::digiReport(int snr) const
{
    return QString::asprintf("%+03d", std::clamp(snr, -30, 30));
}

QString MobileEngine::digiMessage(const QString& kind) const
{
    const QString my = digiMyCall();
    const QString dx = m_digiDx;
    const bool standard = Ft8Engine::isStandardCall(my);
    const QString grid = myGrid().left(4).toUpper();
    const QString myGrid4 = grid.size() == 4 ? QStringLiteral(" ") + grid : QString();
    if (kind == QLatin1String("cq")) {
        return standard ? QStringLiteral("CQ %1%2").arg(my, myGrid4) : QStringLiteral("CQ %1").arg(my);
    }
    if (kind == QLatin1String("call")) {
        return standard ? QStringLiteral("%1 %2%3").arg(dx, my, myGrid4) : QStringLiteral("<%1> %2").arg(dx, my);
    }
    if (kind == QLatin1String("report")) {
        return QString::fromLatin1(standard ? "%1 %2 %3" : "%1 <%2> %3").arg(dx, my, m_digiRptSent);
    }
    if (kind == QLatin1String("rreport")) {
        return QString::fromLatin1(standard ? "%1 %2 R%3" : "%1 <%2> R%3").arg(dx, my, m_digiRptSent);
    }
    if (kind == QLatin1String("rr73")) {
        return QString::fromLatin1(standard ? "%1 %2 RR73" : "<%1> %2 RR73").arg(dx, my);
    }
    return QString::fromLatin1(standard ? "%1 %2 73" : "<%1> %2 73").arg(dx, my);
}

void MobileEngine::setDigiStatus(const QString& status)
{
    m_digiStatus = status;
    qCInfo(lcMobile).noquote() << "digi:" << status;
    emit digiChanged();
}

void MobileEngine::digiTune(double mhz)
{
    if (!m_slice || mhz <= 0.0) {
        return;
    }
    m_slice->setMode(QStringLiteral("DIGU"));
    // Usually another band: the pan moves with it (a plain tune outside the
    // pan is refused by the radio).
    m_slice->tuneAndRecenter(mhz);
    qCInfo(lcMobile) << "digi: tuned" << mhz << "MHz DIGU";
    // Then the audio band fills the display, once the pan has followed.
    QTimer::singleShot(800, this, [this] { digiZoom(); });
}

void MobileEngine::digiZoom()
{
    if (!m_slice || !m_pan) {
        return;
    }
    // 4 kHz around the 0-3 kHz audio band, on its side of the dial.
    const double sign = m_slice->mode() == QLatin1String("DIGL") ? -1.0 : 1.0;
    double bandwidth = 0.004;
    if (m_pan->bandwidthLimitsKnown() && m_pan->minBandwidthMhz() < m_pan->maxBandwidthMhz()) {
        bandwidth = std::clamp(bandwidth, m_pan->minBandwidthMhz(), m_pan->maxBandwidthMhz());
    }
    m_session.radioModel().requestPanCenter(m_pan->panId(), m_slice->frequency() + sign * 0.0015, bandwidth);
}

void MobileEngine::digiCallCq()
{
    const QString my = digiMyCall();
    if (my.isEmpty()) {
        setLastError(tr("Set the radio's callsign first (Radio Setup)."));
        return;
    }
    if (!digiRunning()) {
        setDigiRunning(true);
    }
    const qint64 slotLen = qint64(m_ft8->slotSeconds() * 1000);
    m_digiState = QStringLiteral("cq");
    m_digiDx.clear();
    m_digiDxGrid.clear();
    m_digiRptSent.clear();
    m_digiRptRcvd.clear();
    m_digiLogged = false;
    m_digiEndAfterTx = false;
    m_digiTxMsg = digiMessage(QStringLiteral("cq"));
    m_digiTxParity = int((Ft8Engine::nowMs() / slotLen + 1) % 2);
    m_digiTries = 0;
    setDigiStatus(tr("Calling CQ"));
}

void MobileEngine::digiAnswer(int index)
{
    if (index < 0 || index >= m_digiDecodes.size()) {
        return;
    }
    const QString my = digiMyCall();
    if (my.isEmpty()) {
        setLastError(tr("Set the radio's callsign first (Radio Setup)."));
        return;
    }
    const QVariantMap d = m_digiDecodes.at(index).toMap();
    const Ft8Engine::Parsed p = Ft8Engine::parse(d.value(QStringLiteral("text")).toString());
    if (p.de.isEmpty() || p.de == my) {
        return;
    }
    m_digiState = QStringLiteral("answer");
    m_digiDx = p.de;
    m_digiDxGrid = Ft8Engine::isGrid(p.extra) ? p.extra : QString();
    m_digiRptSent = digiReport(d.value(QStringLiteral("snr")).toInt());
    m_digiRptRcvd.clear();
    m_digiLogged = false;
    m_digiEndAfterTx = false;
    m_digiTimeOn = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HHmm"));
    m_digiTxParity = d.value(QStringLiteral("slotOdd")).toBool() ? 0 : 1;
    // They called us: answer with the report; else call them.
    m_digiTxMsg = digiMessage(p.to == my ? QStringLiteral("report") : QStringLiteral("call"));
    m_digiTries = 0;
    setDigiStatus(tr("Calling %1").arg(m_digiDx));
}

void MobileEngine::digiHalt()
{
    if (m_digiTransmitting) {
        QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
        m_digiTransmitting = false;
        releasePtt(QStringLiteral("FT8 halted"));
        m_session.radioModel().setDigitalVoiceTxSlice(-1);
    }
    if (m_digiState != QLatin1String("idle")) {
        digiEnd(tr("Stopped"));
    }
}

void MobileEngine::digiEnd(const QString& status)
{
    m_digiState = QStringLiteral("idle");
    m_digiDx.clear();
    m_digiTxMsg.clear();
    m_digiTries = 0;
    m_digiEndAfterTx = false;
    setDigiStatus(status);
}

void MobileEngine::onDigiSlot(qint64 slotMs)
{
    if (!m_ft8 || m_digiState == QLatin1String("idle") || m_digiTxMsg.isEmpty() || m_digiTransmitting) {
        return;
    }
    const qint64 slotLen = qint64(m_ft8->slotSeconds() * 1000);
    if (int((slotMs / slotLen) % 2) != m_digiTxParity) {
        return;
    }
    const int limit = m_digiState == QLatin1String("cq") && m_digiDx.isEmpty() ? kMaxCqCalls : kMaxTries;
    if (m_digiTries >= limit) {
        digiEnd(m_digiDx.isEmpty() ? tr("No answer to CQ") : tr("No answer from %1").arg(m_digiDx));
        return;
    }
    startDigiTx(slotMs);
}

void MobileEngine::startDigiTx(qint64 slotMs)
{
    if (!transmitAvailable()) {
        digiEnd(transmitUnavailableReason());
        return;
    }
    const double elapsed = (Ft8Engine::nowMs() - slotMs) / 1000.0;
    if (elapsed > 2.0) {
        return;  // too late in the slot; the next one of ours
    }
    QString error;
    QByteArray audio = m_ft8->encode(m_digiTxMsg, m_digiTxHz, &error);
    if (audio.isEmpty()) {
        digiEnd(error);
        return;
    }
    const double lead = std::max(0.0, 0.5 - elapsed - kTxLatencyS);
    audio.prepend(QByteArray(int(lead * Ft8Engine::kTxSampleRate) * int(sizeof(float)), '\0'));
    // Digital-mode audio from this device: allowed in DIGU like RADE (voice
    // PTT is refused there).
    RadioModel& rm = m_session.radioModel();
    if (SliceModel* tx = rm.txSlice()) {
        rm.setDigitalVoiceTxSlice(tx->sliceId());
    }
    setPtt(true, QStringLiteral("FT8 transmit"));
    if (!m_pttHeld) {
        rm.setDigitalVoiceTxSlice(-1);
        digiEnd(tr("Transmit refused"));
        return;
    }
    m_digiTransmitting = true;
    ++m_digiTries;
    AudioEngine* engine = m_audio;
    QMetaObject::invokeMethod(engine, [engine, audio] { engine->playTxDigital(audio); }, Qt::QueuedConnection);
    // Never left keyed: if the audio has not ended 2 s after it should, stop.
    const int seq = ++m_digiTxSeq;
    const int durationMs = int(qint64(audio.size() / int(sizeof(float))) * 1000 / Ft8Engine::kTxSampleRate);
    QTimer::singleShot(durationMs + 2000, this, [this, seq] {
        if (m_digiTransmitting && seq == m_digiTxSeq) {
            qCWarning(lcMobile) << "digi: transmit audio did not end in time; unkeying";
            QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
            onDigiTxFinished();
        }
    });
    qCInfo(lcMobile).noquote() << QStringLiteral("digi: TX \"%1\" at %2 Hz, %3 s into the slot, try %4")
                                      .arg(m_digiTxMsg)
                                      .arg(m_digiTxHz)
                                      .arg(elapsed, 0, 'f', 2)
                                      .arg(m_digiTries);
    emit digiChanged();
}

void MobileEngine::onDigiTxFinished()
{
    m_digiTransmitting = false;
    releasePtt(QStringLiteral("FT8 sent"));
    m_session.radioModel().setDigitalVoiceTxSlice(-1);
    if (m_digiEndAfterTx) {
        digiEnd(m_digiLogged ? tr("QSO with %1 logged").arg(m_digiDx) : tr("Done"));
        return;
    }
    emit digiChanged();
}

void MobileEngine::digiLogQso()
{
    if (m_digiLogged || m_digiDx.isEmpty()) {
        return;
    }
    const bool ft4 = digiProtocol() == QLatin1String("FT4");
    QVariantMap fields{{QStringLiteral("call"), m_digiDx},
                       {QStringLiteral("rstSent"), m_digiRptSent},
                       {QStringLiteral("rstRcvd"), m_digiRptRcvd},
                       {QStringLiteral("timeOn"), m_digiTimeOn},
                       {QStringLiteral("adifMode"), ft4 ? QStringLiteral("MFSK") : QStringLiteral("FT8")},
                       {QStringLiteral("adifSubmode"), ft4 ? QStringLiteral("FT4") : QString()}};
    if (!m_digiDxGrid.isEmpty()) {
        fields.insert(QStringLiteral("grid"), m_digiDxGrid);
    }
    m_digiLogged = logQso(fields);
}

void MobileEngine::handleDigiDecodes(const QVariantList& decodes, qint64 slotMs)
{
    Q_UNUSED(slotMs)
    // Newest first.
    QVariantList merged = decodes;
    merged.append(m_digiDecodes);
    while (merged.size() > kMaxDecodes) {
        merged.removeLast();
    }
    m_digiDecodes = merged;
    emit digiDecodesChanged();

    if (m_digiState == QLatin1String("idle")) {
        return;
    }
    const QString my = digiMyCall();
    for (const QVariant& v : decodes) {
        const QVariantMap d = v.toMap();
        const Ft8Engine::Parsed p = Ft8Engine::parse(d.value(QStringLiteral("text")).toString());
        if (p.to != my || p.de.isEmpty()) {
            continue;
        }
        const QString extra = p.extra;
        const bool report = Ft8Engine::isReport(extra) && !extra.startsWith(QLatin1Char('R'));
        const bool rReport = Ft8Engine::isReport(extra) && extra.startsWith(QLatin1Char('R'));
        if (m_digiState == QLatin1String("cq") && m_digiDx.isEmpty()) {
            // Someone answered our CQ.
            m_digiDx = p.de;
            m_digiDxGrid = Ft8Engine::isGrid(extra) ? extra : QString();
            m_digiRptSent = digiReport(d.value(QStringLiteral("snr")).toInt());
            m_digiTimeOn = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HHmm"));
            m_digiLogged = false;
            if (report) {
                m_digiRptRcvd = extra;
            }
            m_digiTxMsg = digiMessage(report ? QStringLiteral("rreport") : QStringLiteral("report"));
            m_digiTries = 0;
            setDigiStatus(tr("%1 answered").arg(m_digiDx));
            return;
        }
        if (p.de != m_digiDx) {
            continue;
        }
        if (report) {
            m_digiRptRcvd = extra;
            if (m_digiRptSent.isEmpty()) {
                m_digiRptSent = digiReport(d.value(QStringLiteral("snr")).toInt());
            }
            m_digiTxMsg = digiMessage(QStringLiteral("rreport"));
            m_digiTries = 0;
            setDigiStatus(tr("Report %1 from %2").arg(extra, m_digiDx));
        } else if (rReport) {
            m_digiRptRcvd = extra.mid(1);
            digiLogQso();
            m_digiTxMsg = digiMessage(QStringLiteral("rr73"));
            m_digiEndAfterTx = true;
            m_digiTries = 0;
            setDigiStatus(tr("Report %1 from %2; sending RR73").arg(m_digiRptRcvd, m_digiDx));
        } else if (extra == QLatin1String("RR73") || extra == QLatin1String("RRR")) {
            digiLogQso();
            m_digiTxMsg = digiMessage(QStringLiteral("73"));
            m_digiEndAfterTx = true;
            m_digiTries = 0;
            setDigiStatus(tr("%1 from %2; sending 73").arg(extra, m_digiDx));
        } else if (extra == QLatin1String("73")) {
            digiLogQso();
            digiEnd(tr("QSO with %1 logged").arg(p.de));
        }
        return;
    }
}

}  // namespace AetherSDR::mobile
