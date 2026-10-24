// The Digi tab: FT8 and FT4 on this device (Ft8Engine), and the QSO
// sequence WSJT-X runs: CQ or an answer, reports, RR73 / 73, then the log.

#include "MobileEngine.h"

#include "Ft8Engine.h"
#include "QsoLog.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/CtyDatParser.h"
#include "core/DecoderPcmAdapter.h"
#include "core/LogManager.h"
#include "models/DecoderAudioModel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QDateTime>
#include <QSet>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>

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

struct MobileEngine::DigiDecoder {
    QPointer<SliceModel> slice;
    std::unique_ptr<Ft8Engine> ft8;
    std::unique_ptr<DecoderAudioModel> audio;
    bool assignedDax{false};
    int savedDax{0};
};

void MobileEngine::setupDigi()
{
    m_ft8 = std::make_unique<Ft8Engine>();
    m_ft8->setProtocol(AppSettings::instance().value(QStringLiteral("MobileDigiProtocol"), QStringLiteral("FT8")).toString()
                               == QLatin1String("FT4")
                           ? Ft8Engine::Protocol::Ft4
                           : Ft8Engine::Protocol::Ft8);
    m_digiTxHz = std::clamp(AppSettings::instance().value(QStringLiteral("MobileDigiTxHz"), 1200).toInt(), 200, 3000);
    m_digiAutoCq = AppSettings::instance().value(QStringLiteral("MobileDigiAutoCq"), QStringLiteral("0")).toString() == QLatin1String("1");
    m_digiCqParity = AppSettings::instance().value(QStringLiteral("MobileDigiCqParity"), QStringLiteral("auto")).toString();
    m_digiTxLevelDb = std::clamp(AppSettings::instance().value(QStringLiteral("MobileDigiTxLevelDb"), 0).toInt(), -20, 0);
    m_digiFilter = AppSettings::instance().value(QStringLiteral("MobileDigiFilter"), QStringLiteral("all")).toString();
    connect(m_ft8.get(), &Ft8Engine::slotStarted, this, &MobileEngine::onDigiSlot);
    connect(this, &MobileEngine::qsoLogChanged, this, [this] { m_wkDirty = true; });
}

bool MobileEngine::digiRunning() const
{
    return m_digiOn;
}

void MobileEngine::setDigiRunning(bool on)
{
    if (!m_ft8 || on == m_digiOn) {
        return;
    }
    if (!on) {
        digiHalt();
        if (m_digiProcRestore) {
            m_digiProcRestore = false;
            setProcEnabled(true);
            qCInfo(lcMobile) << "digi: speech processor back on";
        }
    }
    if (on) {
        // The TX slice; none yet: the active slice takes TX.
        if (!m_session.radioModel().txSlice() && m_slice) {
            m_slice->setTxSlice(true);
        }
        bindDigiSlice(m_session.radioModel().txSlice() ? m_session.radioModel().txSlice() : m_slice.data());
    }
    m_digiOn = on;
    qCInfo(lcMobile) << "digi:" << (on ? "decoding on" : "decoding off") << digiProtocol()
                     << "slice" << (digiSlice() ? digiSlice()->letter() : QString());
    if (!on) {
        bindDigiSlice(nullptr);
    }
    syncDigiAudio();
    emit digiChanged();
}

void MobileEngine::syncDigiAudio()
{
    RadioModel& rm = m_session.radioModel();
    const bool run = m_connected && m_digiOn && m_ft8;
    // Which slices decode: every DIGU / DIGL slice, and the TX slice.
    QSet<int> want;
    if (run) {
        for (SliceModel* s : rm.slices()) {
            if (s && (s->mode() == QLatin1String("DIGU") || s->mode() == QLatin1String("DIGL") || s == digiSlice())) {
                want.insert(s->sliceId());
            }
        }
    }
    for (auto it = m_digiDecoders.begin(); it != m_digiDecoders.end();) {
        const std::shared_ptr<DigiDecoder>& d = it->second;
        if (!d->slice || !want.contains(it->first)) {
            d->audio->setEnabled(false);
            d->audio->setSlice(nullptr);
            if (d->slice && d->assignedDax) {
                d->slice->setDaxChannel(d->savedDax);
            }
            qCInfo(lcMobile) << "digi: decoder off for slice" << it->first;
            it = m_digiDecoders.erase(it);
        } else {
            ++it;
        }
    }
    for (int id : std::as_const(want)) {
        if (m_digiDecoders.count(id)) {
            continue;
        }
        SliceModel* slice = rm.slice(id);
        if (!slice) {
            continue;
        }
        auto d = std::make_shared<DigiDecoder>();
        d->slice = slice;
        // Its own DAX channel, so the other slices' audio is not mixed in
        // (without one the decoder hears every audible slice; a new slice
        // can come with the channel of the slice it was made from, and two
        // slices on one channel are mixed too). The old one comes back when
        // the decoder goes.
        QSet<int> used;
        for (SliceModel* other : rm.slices()) {
            if (other && other != slice) {
                used.insert(other->daxChannel());
            }
        }
        if (slice->daxChannel() < 1 || slice->daxChannel() > 8 || used.contains(slice->daxChannel())) {
            for (int ch = 1; ch <= 8; ++ch) {
                if (!used.contains(ch)) {
                    d->savedDax = slice->daxChannel();
                    slice->setDaxChannel(ch);
                    d->assignedDax = true;
                    break;
                }
            }
        }
        d->ft8 = std::make_unique<Ft8Engine>();
        d->ft8->setProtocol(m_ft8->protocol());
        d->ft8->setRunning(true);
        d->audio = std::make_unique<DecoderAudioModel>(rm, DecoderAudioModel::Consumer::Rtty);
        connect(d->audio.get(), &DecoderAudioModel::pcmReady, d->ft8.get(), &Ft8Engine::feed);
        connect(d->audio.get(), &DecoderAudioModel::sourceReset, d->ft8.get(), &Ft8Engine::resetAudio);
        connect(d->ft8.get(), &Ft8Engine::decoded, this, [this, id](const QVector<Ft8Engine::Decode>& decodes, qint64 slotMs) {
            SliceModel* sl = m_session.radioModel().slice(id);
            const QString my = digiMyCall();
            const qint64 slotLen = qint64(m_ft8->slotSeconds() * 1000);
            const QString utc = QDateTime::fromMSecsSinceEpoch(slotMs, QTimeZone::UTC).toString(QStringLiteral("HHmmss"));
            const QString band = sl ? QsoLog::bandOf(sl->frequency()) : QString();
            const QString modeKey = digiProtocol();
            QVariantList list;
            for (const Ft8Engine::Decode& dec : decodes) {
                const Ft8Engine::Parsed p = Ft8Engine::parse(dec.text);
                const bool toMe = !my.isEmpty() && p.to == my;
                list.append(QVariantMap{{QStringLiteral("utc"), utc},
                                        {QStringLiteral("snr"), dec.snr},
                                        {QStringLiteral("dt"), QString::number(dec.dt, 'f', 1)},
                                        {QStringLiteral("freq"), int(std::lround(dec.freq))},
                                        {QStringLiteral("text"), dec.text},
                                        {QStringLiteral("call"), p.de},
                                        {QStringLiteral("cq"), p.cq},
                                        {QStringLiteral("toMe"), toMe},
                                        {QStringLiteral("hl"), digiHighlight(p.de, p.cq, toMe, band, modeKey)},
                                        {QStringLiteral("newDxcc"), digiNewDxcc(p.de, p.cq, band)},
                                        {QStringLiteral("sliceId"), id},
                                        {QStringLiteral("slice"), sl ? sl->letter() : QString()},
                                        {QStringLiteral("slotOdd"), (slotMs / slotLen) % 2 == 1}});
            }
            // The device clock: a busy slot's median DT, smoothed.
            if (decodes.size() >= 4) {
                std::vector<double> dts;
                for (const Ft8Engine::Decode& dec : decodes) {
                    dts.push_back(dec.dt);
                }
                std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
                const double median = dts[dts.size() / 2];
                const bool wasWarn = m_digiClockKnown && std::abs(m_digiClockOffset) > 0.8;
                m_digiClockOffset = m_digiClockKnown ? 0.7 * m_digiClockOffset + 0.3 * median : median;
                m_digiClockKnown = true;
                const bool warn = std::abs(m_digiClockOffset) > 0.8;
                if (warn != wasWarn) {
                    qCInfo(lcMobile).noquote() << QStringLiteral("digi: clock offset %1 s").arg(m_digiClockOffset, 0, 'f', 2);
                    emit digiChanged();
                }
            }
            qCInfo(lcMobile).noquote() << QStringLiteral("digi: %1 %2 decodes").arg(utc).arg(list.size())
                                       << "slice" << (sl ? sl->letter() : QString::number(id));
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
        d->audio->setSlice(slice);
        d->audio->setEnabled(true);
        connect(slice, &SliceModel::modeChanged, this, [this] { QTimer::singleShot(0, this, &MobileEngine::syncDigiAudio); },
                Qt::UniqueConnection);
        connect(slice, &QObject::destroyed, this, [this] { QTimer::singleShot(0, this, &MobileEngine::syncDigiAudio); });
        qCInfo(lcMobile) << "digi: decoder on for slice" << slice->letter() << "DAX channel" << slice->daxChannel();
        m_digiDecoders.emplace(id, d);
    }
}

void MobileEngine::rebuildWorked()
{
    m_wkCallBandMode.clear();
    m_wkDxcc.clear();
    m_wkDxccBand.clear();
    m_wkCont.clear();
    m_wkContBand.clear();
    m_wkZone.clear();
    m_wkZoneBand.clear();
    for (const QVariantMap& r : m_qsoLog->records()) {
        const QString call = r.value(QStringLiteral("CALL")).toString().toUpper();
        const QString band = r.value(QStringLiteral("BAND")).toString().toLower();
        const QString sub = r.value(QStringLiteral("SUBMODE")).toString().toUpper();
        const QString mode = sub.isEmpty() ? r.value(QStringLiteral("MODE")).toString().toUpper() : sub;
        m_wkCallBandMode.insert(call + QLatin1Char('|') + band + QLatin1Char('|') + mode);
        const DxccEntity* e = m_cty ? m_cty->entityForCallsign(call) : nullptr;
        if (!e) {
            continue;
        }
        const int zone = r.value(QStringLiteral("CQZ")).toInt() > 0 ? r.value(QStringLiteral("CQZ")).toInt() : e->cqZone;
        m_wkDxcc.insert(e->primaryPrefix);
        m_wkDxccBand.insert(e->primaryPrefix + QLatin1Char('|') + band);
        m_wkCont.insert(e->continent);
        m_wkContBand.insert(e->continent + QLatin1Char('|') + band);
        m_wkZone.insert(QString::number(zone));
        m_wkZoneBand.insert(QString::number(zone) + QLatin1Char('|') + band);
    }
    m_wkDirty = false;
}

QString MobileEngine::digiHighlight(const QString& call, bool cq, bool toMe, const QString& band, const QString& modeKey)
{
    // WSJT-X's default highlighting, in its order: my call; for a CQ a new
    // continent, CQ zone or DXCC (overall, then on the band); a CQ from a
    // station already worked on this band and mode marked worked (B4);
    // any other CQ.
    if (toMe) {
        return QStringLiteral("mycall");
    }
    if (!cq || call.isEmpty()) {
        return {};
    }
    if (m_wkDirty) {
        rebuildWorked();
    }
    const QString b = band.toLower();
    if (const DxccEntity* e = m_cty ? m_cty->entityForCallsign(call) : nullptr) {
        const QString zone = QString::number(e->cqZone);
        if (!m_wkCont.contains(e->continent)) {
            return QStringLiteral("continent");
        }
        if (!m_wkContBand.contains(e->continent + QLatin1Char('|') + b)) {
            return QStringLiteral("continentBand");
        }
        if (!m_wkZone.contains(zone)) {
            return QStringLiteral("cqzone");
        }
        if (!m_wkZoneBand.contains(zone + QLatin1Char('|') + b)) {
            return QStringLiteral("cqzoneBand");
        }
        if (!m_wkDxcc.contains(e->primaryPrefix)) {
            return QStringLiteral("dxcc");
        }
        if (!m_wkDxccBand.contains(e->primaryPrefix + QLatin1Char('|') + b)) {
            return QStringLiteral("dxccBand");
        }
    }
    if (m_wkCallBandMode.contains(call.toUpper() + QLatin1Char('|') + b + QLatin1Char('|') + modeKey)) {
        return QStringLiteral("b4");
    }
    return QStringLiteral("cq");
}

int MobileEngine::digiNewDxcc(const QString& call, bool cq, const QString& band)
{
    // Apart from the highlight, which a new continent or zone outranks.
    if (!cq || call.isEmpty() || !m_cty) {
        return 0;
    }
    if (m_wkDirty) {
        rebuildWorked();
    }
    const DxccEntity* e = m_cty->entityForCallsign(call);
    if (!e) {
        return 0;
    }
    if (!m_wkDxcc.contains(e->primaryPrefix)) {
        return 2;
    }
    return m_wkDxccBand.contains(e->primaryPrefix + QLatin1Char('|') + band.toLower()) ? 0 : 1;
}

void MobileEngine::setDigiFilter(const QString& filter)
{
    static const QStringList known{QStringLiteral("all"), QStringLiteral("cq"), QStringLiteral("new"), QStringLiteral("dxcc")};
    if (filter == m_digiFilter || !known.contains(filter)) {
        return;
    }
    m_digiFilter = filter;
    AppSettings::instance().setValue(QStringLiteral("MobileDigiFilter"), filter);
    AppSettings::instance().save();
    emit digiFilterChanged();
}

void MobileEngine::bindDigiSlice(SliceModel* slice)
{
    if (slice == m_digiSlice.data()) {
        return;
    }
    for (const QMetaObject::Connection& c : std::as_const(m_digiSliceConnections)) {
        disconnect(c);
    }
    m_digiSliceConnections.clear();
    m_digiSlice = slice;
    if (slice) {
        m_digiSliceConnections.append(connect(slice, &SliceModel::frequencyChanged, this, &MobileEngine::digiChanged));
        m_digiSliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::digiChanged));
        m_digiSliceConnections.append(connect(slice, &QObject::destroyed, this, [this] {
            m_digiSliceConnections.clear();
            digiHalt();
            QTimer::singleShot(0, this, &MobileEngine::updateDigiSlice);
        }));
        qCInfo(lcMobile) << "digi: TX slice" << slice->letter();
    }
    syncDigiAudio();
    emit digiChanged();
}

void MobileEngine::digiBindActiveSlice()
{
    if (!m_slice || m_slice.data() == m_digiSlice.data()) {
        return;
    }
    // FT8 follows TX: the active slice takes it, and updateDigiSlice binds
    // it when the radio confirms. Bound now too, so a tone set right after
    // lands on this slice.
    digiHalt();
    m_slice->setTxSlice(true);
    bindDigiSlice(m_slice.data());
}

void MobileEngine::updateDigiSlice()
{
    if (!digiRunning()) {
        return;
    }
    SliceModel* tx = m_session.radioModel().txSlice();
    if (!tx || tx == m_digiSlice.data()) {
        return;
    }
    // TX moved (the badge, another client, a profile): FT8 goes with it,
    // and a QSO under way on the old slice ends.
    if (m_digiState != QLatin1String("idle") || m_digiTransmitting) {
        digiHalt();
    }
    bindDigiSlice(tx);
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
    for (auto& [id, d] : m_digiDecoders) {
        d->ft8->setProtocol(m_ft8->protocol());
        d->ft8->resetAudio();
    }
    AppSettings::instance().setValue(QStringLiteral("MobileDigiProtocol"), digiProtocol());
    AppSettings::instance().save();
    m_digiDecodes.clear();
    emit digiDecodesChanged();
    emit digiChanged();
}

void MobileEngine::setDigiAutoCq(bool on)
{
    if (on == m_digiAutoCq) {
        return;
    }
    m_digiAutoCq = on;
    AppSettings::instance().setValue(QStringLiteral("MobileDigiAutoCq"), on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    emit digiChanged();
}

void MobileEngine::setDigiCqParity(const QString& parity)
{
    if (parity == m_digiCqParity
        || !QStringList{QStringLiteral("auto"), QStringLiteral("even"), QStringLiteral("odd")}.contains(parity)) {
        return;
    }
    m_digiCqParity = parity;
    AppSettings::instance().setValue(QStringLiteral("MobileDigiCqParity"), parity);
    AppSettings::instance().save();
    // A CQ under way moves to that slot from the next one.
    if (m_digiState == QLatin1String("cq") && m_digiDx.isEmpty() && parity != QLatin1String("auto")) {
        m_digiTxParity = parity == QLatin1String("odd") ? 1 : 0;
    }
    emit digiChanged();
}

void MobileEngine::digiInjectDecodes(QVariantList rows)
{
    const int txId = digiSlice() ? digiSlice()->sliceId() : -1;
    for (QVariant& v : rows) {
        QVariantMap m = v.toMap();
        if (!m.contains(QStringLiteral("sliceId"))) {
            m.insert(QStringLiteral("sliceId"), txId);
        }
        v = m;
    }
    handleDigiDecodes(rows, Ft8Engine::nowMs());
}

bool MobileEngine::digiNoReports() const
{
    return !m_digiDx.isEmpty() && !m_digiDx.startsWith(QLatin1Char('<')) && !Ft8Engine::isStandardCall(digiMyCall())
           && !Ft8Engine::isStandardCall(m_digiDx);
}

void MobileEngine::digiFinishQso(const QString& status)
{
    // With Keep CQ on, a QSO that answered our CQ goes straight back to CQ
    // in the same slot.
    if (m_digiAutoCq && m_digiFromCq) {
        const int parity = m_digiTxParity;
        digiCallCq();
        m_digiTxParity = parity;
        setDigiStatus(tr("%1; calling CQ").arg(status));
        return;
    }
    digiEnd(status);
}

void MobileEngine::setDigiTxLevelDb(int db)
{
    db = std::clamp(db, -20, 0);
    if (db == m_digiTxLevelDb) {
        return;
    }
    m_digiTxLevelDb = db;
    AppSettings::instance().setValue(QStringLiteral("MobileDigiTxLevelDb"), db);
    AppSettings::instance().save();
    emit digiChanged();
}

void MobileEngine::setDigiTxHz(int hz)
{
    hz = std::clamp(hz, 200, 3000);
    if (hz == m_digiTxHz) {
        return;
    }
    m_digiTxHz = hz;
    // A drag sets it many times a second: saved once it settles.
    if (!m_digiTxHzSavePending) {
        m_digiTxHzSavePending = true;
        QTimer::singleShot(800, this, [this] {
            m_digiTxHzSavePending = false;
            AppSettings::instance().setValue(QStringLiteral("MobileDigiTxHz"), m_digiTxHz);
            AppSettings::instance().save();
        });
    }
    emit digiChanged();
}

QVariantMap MobileEngine::digi() const
{
    return {{QStringLiteral("state"), m_digiState},
            {QStringLiteral("dx"), m_digiDx},
            {QStringLiteral("clockOffset"), m_digiClockOffset},
            {QStringLiteral("clockWarn"), m_digiClockKnown && std::abs(m_digiClockOffset) > 0.8},
            // Where the other station is heard (WSJT-X's green RX mark): the
            // QSO's station, or while calling CQ our own TX audio frequency.
            // Always shown, as WSJT-X's: the last station worked stays
            // marked after the QSO; none yet, or calling CQ, our own TX Hz.
            {QStringLiteral("rxHz"), m_digiRxHz > 0 ? m_digiRxHz : m_digiTxHz},
            {QStringLiteral("txMsg"), m_digiTxMsg},
            {QStringLiteral("status"), m_digiStatus},
            {QStringLiteral("transmitting"), m_digiTransmitting},
            {QStringLiteral("parity"), m_digiTxParity == 1 ? QStringLiteral("odd") : QStringLiteral("even")},
            {QStringLiteral("slotSeconds"), m_ft8 ? m_ft8->slotSeconds() : 15.0},
            {QStringLiteral("myCall"), digiMyCall()},
            {QStringLiteral("sliceMhz"), digiSlice() ? digiSlice()->frequency() : 0.0},
            {QStringLiteral("sliceMode"), digiSlice() ? digiSlice()->mode() : QString()},
            {QStringLiteral("sliceLetter"), digiSlice() ? digiSlice()->letter() : QString()},
            {QStringLiteral("sliceActive"), digiSlice() == m_slice.data()}};
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
    // A band picked on the Digi tab works on the active slice, which takes
    // TX (FT8 follows the TX slice).
    if (digiRunning()) {
        digiBindActiveSlice();
    } else if (!m_slice->isTxSlice()) {
        m_slice->setTxSlice(true);
    }
    armModeAgc();
    m_slice->setMode(QStringLiteral("DIGU"));
    applyModeAgc();  // also when it was in DIGU already
    // Usually another band: the pan moves with it (a plain tune outside the
    // pan is refused by the radio).
    m_slice->tuneAndRecenter(mhz);
    qCInfo(lcMobile) << "digi: tuned" << mhz << "MHz DIGU";
    // A band picked on the Digi tab means FT8 / FT4: decoding starts.
    if (!digiRunning() && !radeActive()) {
        setDigiRunning(true);
        // On this slice, whose TX the radio has not confirmed yet: bound to
        // the old TX slice, a CQ pressed meanwhile stopped when the move
        // came through.
        bindDigiSlice(m_slice.data());
    }
    // Then the audio band fills the display, once the pan has followed.
    QTimer::singleShot(800, this, [this] { digiZoom(); });
}

void MobileEngine::digiZoom()
{
    SliceModel* slice = digiSlice();
    if (!slice || !m_pan) {
        return;
    }
    // 4 kHz around the 0-3 kHz audio band, on its side of the dial.
    const double sign = slice->mode() == QLatin1String("DIGL") ? -1.0 : 1.0;
    double bandwidth = 0.004;
    if (m_pan->bandwidthLimitsKnown() && m_pan->minBandwidthMhz() < m_pan->maxBandwidthMhz()) {
        bandwidth = std::clamp(bandwidth, m_pan->minBandwidthMhz(), m_pan->maxBandwidthMhz());
    }
    m_session.radioModel().requestPanCenter(m_pan->panId(), slice->frequency() + sign * 0.0015, bandwidth);
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
    m_digiRxHz = 0;
    m_digiDxGrid.clear();
    m_digiRptSent.clear();
    m_digiRptRcvd.clear();
    m_digiLogged = false;
    m_digiEndAfterTx = false;
    m_digiTxMsg = digiMessage(QStringLiteral("cq"));
    m_digiFromCq = true;
    m_digiTxParity = m_digiCqParity == QLatin1String("even") ? 0
                     : m_digiCqParity == QLatin1String("odd") ? 1
                     : int((Ft8Engine::nowMs() / slotLen + 1) % 2);
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
    if (p.de.isEmpty() || p.de == my || d.value(QStringLiteral("hl")) == QLatin1String("tx")) {
        return;
    }
    // Heard on another slice: TX and the display go there (the key waits
    // for the radio to confirm the TX move).
    const int sliceId = d.value(QStringLiteral("sliceId"), -1).toInt();
    if (SliceModel* there = m_session.radioModel().slice(sliceId); there && there != digiSlice()) {
        digiHalt();
        there->setTxSlice(true);
        bindDigiSlice(there);
        selectSlice(sliceId);
        QTimer::singleShot(300, this, [this] { digiZoom(); });
        qCInfo(lcMobile) << "digi: answering on slice" << there->letter();
    }
    m_digiState = QStringLiteral("answer");
    m_digiFromCq = false;
    m_digiDx = p.de;
    m_digiRxHz = d.value(QStringLiteral("freq")).toInt();
    m_digiDxGrid = Ft8Engine::isGrid(p.extra) ? p.extra : QString();
    m_digiRptSent = digiReport(d.value(QStringLiteral("snr")).toInt());
    m_digiRptRcvd.clear();
    m_digiLogged = false;
    m_digiEndAfterTx = false;
    m_digiTimeOn = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HHmm"));
    m_digiTxParity = d.value(QStringLiteral("slotOdd")).toBool() ? 0 : 1;
    // They called us: answer with the report; else call them. With two
    // nonstandard calls there is no report: the call, then RR73.
    if (digiNoReports()) {
        m_digiRptSent.clear();
        m_digiTxMsg = digiMessage(p.to == my ? QStringLiteral("rr73") : QStringLiteral("call"));
        if (p.to == my) {
            digiLogQso();
            m_digiEndAfterTx = true;
        }
    } else {
        m_digiTxMsg = digiMessage(p.to == my ? QStringLiteral("report") : QStringLiteral("call"));
    }
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
        if (m_digiDx.isEmpty()) {
            digiEnd(tr("No answer to CQ"));  // the CQ watchdog, Keep CQ or not
        } else {
            digiFinishQso(tr("No answer from %1").arg(m_digiDx));
        }
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
    // The TX level: the tone is made at full scale.
    {
        const float gain = float(std::pow(10.0, m_digiTxLevelDb / 20.0));
        auto* f = reinterpret_cast<float*>(audio.data());
        for (qsizetype i = 0, n = audio.size() / qsizetype(sizeof(float)); i < n; ++i) {
            f[i] *= gain;
        }
    }
    const double lead = std::max(0.0, 0.5 - elapsed - kTxLatencyS);
    audio.prepend(QByteArray(int(lead * Ft8Engine::kTxSampleRate) * int(sizeof(float)), '\0'));
    // Digital-mode audio from this device: allowed in DIGU like RADE (voice
    // PTT is refused there).
    RadioModel& rm = m_session.radioModel();
    // FT8 is on the TX slice; just after a band button or a tap moved TX
    // the radio may not have confirmed it yet: the key waits for that
    // (PTT is checked against the TX slice), within this slot's start.
    if (SliceModel* own = digiSlice(); own && !own->isTxSlice()) {
        QTimer::singleShot(150, this, [this, slotMs] { startDigiTx(slotMs); });
        return;
    }
    if (m_digiDryRun) {
        m_digiTransmitting = true;
        ++m_digiTries;
        const int ms = int(qint64(audio.size() / int(sizeof(float))) * 1000 / Ft8Engine::kTxSampleRate);
        qCInfo(lcMobile).noquote() << QStringLiteral("digi: dry TX \"%1\", try %2, %3 slot")
                                          .arg(m_digiTxMsg)
                                          .arg(m_digiTries)
                                          .arg((slotMs / qint64(m_ft8->slotSeconds() * 1000)) % 2 ? QStringLiteral("odd") : QStringLiteral("even"));
        QTimer::singleShot(ms, this, [this] {
            m_digiTransmitting = false;
            if (m_digiEndAfterTx) {
                digiFinishQso(m_digiLogged ? tr("QSO with %1 logged").arg(m_digiDx) : tr("Done"));
            } else {
                emit digiChanged();
            }
        });
        emit digiChanged();
        return;
    }
    if (SliceModel* tx = digiSlice()) {
        rm.setDigitalVoiceTxSlice(tx->sliceId());
    }
    // The radio's speech processor off while FT8 / FT4 runs (asked by a
    // tester: a PROC left on would distort the tones); on again when
    // decoding stops.
    if (procEnabled()) {
        m_digiProcRestore = true;
        setProcEnabled(false);
        qCInfo(lcMobile) << "digi: speech processor off for FT8 / FT4";
    }
    setPtt(true, QStringLiteral("FT8 transmit"));
    if (!m_pttHeld) {
        rm.setDigitalVoiceTxSlice(-1);
        digiEnd(tr("Transmit refused"));
        return;
    }
    m_digiTransmitting = true;
    m_digiTxPeakW = 0.0;
    m_digiTxPeakSwr = 0.0;
    ++m_digiTries;
    // Our own transmission in the list (yellow, as WSJT-X).
    {
        SliceModel* own = digiSlice();
        const QVariantMap row{{QStringLiteral("utc"), QDateTime::fromMSecsSinceEpoch(slotMs, QTimeZone::UTC).toString(QStringLiteral("HHmmss"))},
                              {QStringLiteral("snr"), QString()},
                              {QStringLiteral("dt"), QString()},
                              {QStringLiteral("freq"), m_digiTxHz},
                              {QStringLiteral("text"), m_digiTxMsg},
                              {QStringLiteral("hl"), QStringLiteral("tx")},
                              {QStringLiteral("sliceId"), own ? own->sliceId() : -1},
                              {QStringLiteral("slice"), own ? own->letter() : QString()}};
        m_digiDecodes.prepend(row);
        while (m_digiDecodes.size() > kMaxDecodes) {
            m_digiDecodes.removeLast();
        }
        emit digiDecodesChanged();
    }
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
    // The power and SWR say why the power out is short (the band's RF
    // power, SWR foldback) when the TX level is not the reason.
    qCInfo(lcMobile).noquote() << QStringLiteral("digi: TX ended, peak %1 W, SWR %2, TX level %3 dB, RF power %4 W")
                                      .arg(m_digiTxPeakW, 0, 'f', 1)
                                      .arg(m_digiTxPeakSwr > 0 ? QString::number(m_digiTxPeakSwr, 'f', 1) : QStringLiteral("-"))
                                      .arg(m_digiTxLevelDb)
                                      .arg(rfPower());
    releasePtt(QStringLiteral("FT8 sent"));
    m_session.radioModel().setDigitalVoiceTxSlice(-1);
    if (m_digiEndAfterTx) {
        digiFinishQso(m_digiLogged ? tr("QSO with %1 logged").arg(m_digiDx) : tr("Done"));
        return;
    }
    emit digiChanged();
}

void MobileEngine::digiLogQso()
{
    if (m_digiLogged || m_digiDx.isEmpty()) {
        return;
    }
    if (m_digiDryRun) {  // a test's made-up QSO stays out of the log
        m_digiLogged = true;
        qCInfo(lcMobile) << "digi: dry run, not logged:" << m_digiDx;
        return;
    }
    const bool ft4 = digiProtocol() == QLatin1String("FT4");
    QVariantMap fields{{QStringLiteral("call"), m_digiDx},
                       {QStringLiteral("freqMhz"), digiSlice() ? digiSlice()->frequency() : 0.0},
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
    const int txId = digiSlice() ? digiSlice()->sliceId() : -1;
    for (const QVariant& v : decodes) {
        const QVariantMap d = v.toMap();
        // The QSO runs on the TX slice; the other slices only listen.
        if (d.value(QStringLiteral("sliceId"), txId).toInt() != txId) {
            continue;
        }
        const Ft8Engine::Parsed p = Ft8Engine::parse(d.value(QStringLiteral("text")).toString());
        if (!m_digiDx.isEmpty() && p.de == m_digiDx && d.value(QStringLiteral("freq")).toInt() != m_digiRxHz) {
            m_digiRxHz = d.value(QStringLiteral("freq")).toInt();
            emit digiChanged();
        }
        if (p.to != my || p.de.isEmpty()) {
            continue;
        }
        const QString extra = p.extra;
        const bool report = Ft8Engine::isReport(extra) && !extra.startsWith(QLatin1Char('R'));
        const bool rReport = Ft8Engine::isReport(extra) && extra.startsWith(QLatin1Char('R'));
        if (m_digiState == QLatin1String("cq") && m_digiDx.isEmpty()) {
            // Someone answered our CQ.
            m_digiDx = p.de;
            m_digiRxHz = d.value(QStringLiteral("freq")).toInt();
            m_digiDxGrid = Ft8Engine::isGrid(extra) ? extra : QString();
            m_digiRptSent = digiReport(d.value(QStringLiteral("snr")).toInt());
            m_digiTimeOn = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HHmm"));
            m_digiLogged = false;
            if (digiNoReports()) {
                // Their answer is the contact: no report can follow.
                m_digiRptSent.clear();
                digiLogQso();
                m_digiTxMsg = digiMessage(QStringLiteral("rr73"));
                m_digiEndAfterTx = true;
                m_digiTries = 0;
                setDigiStatus(tr("%1 answered; two nonstandard calls, no reports: sending RR73").arg(m_digiDx));
                return;
            }
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
        if (digiNoReports()) {
            // Calls and RR73 only: whatever they send us means they hear us.
            m_digiRptSent.clear();
            digiLogQso();
            if (extra == QLatin1String("RR73") || extra == QLatin1String("RRR") || extra == QLatin1String("73")) {
                m_digiTxMsg = digiMessage(QStringLiteral("73"));
            } else {
                m_digiTxMsg = digiMessage(QStringLiteral("rr73"));
            }
            m_digiEndAfterTx = true;
            m_digiTries = 0;
            setDigiStatus(tr("%1 heard us; two nonstandard calls, no reports").arg(m_digiDx));
            return;
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
            digiFinishQso(tr("QSO with %1 logged").arg(p.de));
        }
        return;
    }
}

}  // namespace AetherSDR::mobile
