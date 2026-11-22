// Spots from the Reverse Beacon Network and PSK Reporter, and Heard me
// (roadmap 136, the user: "mahdollisimman laaja ja hyvä spotti lähde").
//
// RBN: through the DX cluster, as RBN asks end users to (its telnet nodes
// are for the "retail" clusters): DX Spider's SET/SKIMMER brings its
// curated skimmer spots (spotter "CALL-#"), ACCEPT/RBN keeps those from
// skimmers near this station (View › From: the country, else the
// continent's CQ zones; the whole world's were 6 a second, 2026-11-22 on
// dxc.nc7j.com) and every spot of this station's own call (Heard me).
//
// PSK Reporter: its live MQTT feed (mqtt.pskreporter.info, Tom M0LTE's,
// public, no account), topics filtered on the server: the reports of
// receivers in this station's DXCC (digital spots heard here) and every
// report of this station's own signal (Heard me).

#include "MobileEngine.h"

#include "AdifDxcc.h"
#include "core/AppSettings.h"
#include "core/CtyDatParser.h"
#include "core/DxClusterClient.h"
#include "core/MaidenheadLocator.h"
#include "MiniMqtt.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kRbnKey = QStringLiteral("MobileRbnSpots");
const QString kRbnSetKey = QStringLiteral("MobileRbnSetOnCluster");
const QString kPskKey = QStringLiteral("MobilePskSpots");
const QString kHeardKey = QStringLiteral("MobileHeardMe");
constexpr auto kPskHost = "mqtt.pskreporter.info";
constexpr quint16 kPskPort = 1883;
// A PSK Reporter station spotted again on its band only after this, and at
// most this many new ones a band per window (FT8's few kHz would be a wall
// of labels on the radio's display).
constexpr qint64 kPskRepeatMs = 15 * 60 * 1000;
constexpr int kPskPerBandWindow = 40;
constexpr int kPskLifetimeSec = 15 * 60;
// RBN as PSK Reporter: a skimmer-spotted station once a band in 10 minutes,
// at most this many new ones a band (Europe's skimmers gave 600 a minute).
constexpr qint64 kRbnRepeatMs = 10 * 60 * 1000;
constexpr int kRbnPerBandWindow = 40;
// Heard me keeps the last half hour.
constexpr qint64 kHeardKeepMs = 30 * 60 * 1000;
// PSK Reporter's query API: at most once every five minutes (its terms,
// pskreporter.info/pskdev.html); Heard me asks it for the last half hour
// when turned on or connected, as the live feed has no history and the
// receivers upload every five minutes.
constexpr qint64 kPskQueryGapMs = 5 * 60 * 1000;

bool flag(const QString& key, bool fallback)
{
    const QVariant v = AppSettings::instance().value(key);
    if (!v.isValid() || v.isNull()) {
        return fallback;
    }
    return v.toString() == QLatin1String("True");
}

QString stripSsid(const QString& call)
{
    return call.section(QLatin1Char('-'), 0, 0).trimmed().toUpper();
}

// A compound call's base (OH6OKSA/P -> OH6OKSA).
QString baseOf(const QString& call)
{
    const QString c = stripSsid(call);
    const QStringList parts = c.split(QLatin1Char('/'));
    QString best;
    for (const QString& p : parts) {
        if (p.size() > best.size()) {
            best = p;
        }
    }
    return best;
}

// The continent's CQ zones, for DX Spider's by_zone (it has no continent
// filter). Zone 39 (Indian Ocean) goes with Africa.
QString continentZones(const QString& continent)
{
    if (continent == QLatin1String("EU")) return QStringLiteral("14,15,16,20");
    if (continent == QLatin1String("NA")) return QStringLiteral("1,2,3,4,5,6,7,8");
    if (continent == QLatin1String("SA")) return QStringLiteral("9,10,11,12,13");
    if (continent == QLatin1String("AF")) return QStringLiteral("33,34,35,36,37,38,39");
    if (continent == QLatin1String("AS")) return QStringLiteral("17,18,19,21,22,23,24,25,26");
    if (continent == QLatin1String("OC")) return QStringLiteral("27,28,29,30,31,32");
    return {};
}

int bandOrder(const QString& band)
{
    static const QStringList order{QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("60m"),
                                   QStringLiteral("40m"),  QStringLiteral("30m"), QStringLiteral("20m"),
                                   QStringLiteral("17m"),  QStringLiteral("15m"), QStringLiteral("12m"),
                                   QStringLiteral("10m"),  QStringLiteral("6m"),  QStringLiteral("2m")};
    const qsizetype i = order.indexOf(band.toLower());
    return i < 0 ? 99 : int(i);
}

// The band of a frequency, as PSK Reporter names them ("20m").
QString bandFor(double mhz)
{
    struct B { double lo, hi; const char* name; };
    static const B bands[] = {{1.8, 2.0, "160m"},   {3.5, 4.0, "80m"},    {5.25, 5.45, "60m"}, {7.0, 7.3, "40m"},
                              {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"}, {18.068, 18.168, "17m"},
                              {21.0, 21.45, "15m"}, {24.89, 24.99, "12m"}, {28.0, 29.7, "10m"},
                              {50.0, 54.0, "6m"},   {144.0, 148.0, "2m"}};
    for (const B& b : bands) {
        if (mhz >= b.lo && mhz <= b.hi) {
            return QString::fromLatin1(b.name);
        }
    }
    return {};
}
} // namespace

void MobileEngine::setupSpotFeeds()
{
    m_pskMqtt = new MiniMqtt(this);
    connect(m_pskMqtt, &MiniMqtt::message, this, &MobileEngine::onPskReport);
    connect(m_pskMqtt, &MiniMqtt::stateChanged, this, &MobileEngine::spotFeedsChanged);
    // The skimmer spots asked of the cluster at each login (DX Spider keeps
    // them per user, but a filter for this station's place is set anew).
    connect(m_dx, &DxClusterClient::connected, this, [this] {
        QTimer::singleShot(1500, this, [this] { applyRbnToCluster(); });
    });
    m_heardTimer.setSingleShot(true);
    m_heardTimer.setInterval(1000);
    connect(&m_heardTimer, &QTimer::timeout, this, &MobileEngine::heardMeChanged);
    // Old reports leave Heard me.
    m_heardPrune.setInterval(60 * 1000);
    connect(&m_heardPrune, &QTimer::timeout, this, [this] {
        const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kHeardKeepMs;
        const qsizetype before = m_heard.size();
        m_heard.erase(std::remove_if(m_heard.begin(), m_heard.end(),
                                     [cutoff](const HeardReport& r) { return r.ms < cutoff; }),
                      m_heard.end());
        if (m_heard.size() != before) {
            emit heardMeChanged();
        }
        // The PSK Reporter repeat window's bookkeeping.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_pskSeen.begin(); it != m_pskSeen.end();) {
            it = now - it.value() > kPskRepeatMs ? m_pskSeen.erase(it) : std::next(it);
        }
        for (auto it = m_rbnSeen.begin(); it != m_rbnSeen.end();) {
            it = now - it.value() > kRbnRepeatMs ? m_rbnSeen.erase(it) : std::next(it);
        }
    });
    m_heardPrune.start();
    connect(this, &MobileEngine::dxClusterChanged, this, &MobileEngine::spotFeedsChanged);
    // Spots shown chosen before RBN and PSK Reporter had kinds of their own:
    // with Other shown (where the desktop's RBN spots were) they show too.
    AppSettings& settings = AppSettings::instance();
    if (settings.value(QStringLiteral("MobileSpotSourcesV2")).toString() != QLatin1String("True")) {
        const QString saved = settings.value(QStringLiteral("MobileSpotSources")).toString();
        if (!saved.isEmpty() && saved != QLatin1String("none") && saved.split(QLatin1Char(',')).contains(QStringLiteral("other"))) {
            settings.setValue(QStringLiteral("MobileSpotSources"), saved + QStringLiteral(",rbn,psk"));
        }
        settings.setValue(QStringLiteral("MobileSpotSourcesV2"), QStringLiteral("True"));
        settings.save();
    }
}

QVariantMap MobileEngine::spotFeeds() const
{
    const QString mqtt = m_pskMqtt ? m_pskMqtt->state() : QStringLiteral("off");
    return {{QStringLiteral("rbn"), flag(kRbnKey, false)},
            {QStringLiteral("psk"), flag(kPskKey, false)},
            {QStringLiteral("heardMe"), flag(kHeardKey, false)},
            {QStringLiteral("rbnSpots"), double(m_rbnSpotsIn)},
            {QStringLiteral("pskState"), mqtt},
            {QStringLiteral("pskError"), m_pskMqtt ? m_pskMqtt->lastError() : QString()},
            {QStringLiteral("pskReports"), double(m_pskMqtt ? m_pskMqtt->messages() : 0)},
            {QStringLiteral("pskSpots"), double(m_pskSpotsIn)},
            {QStringLiteral("myDxcc"), myAdifDxcc()},
            {QStringLiteral("clusterOn"), m_dxState == QLatin1String("connected")}};
}

void MobileEngine::setSpotFeed(const QString& key, bool on)
{
    const QString settingKey = key == QLatin1String("rbn") ? kRbnKey
                             : key == QLatin1String("psk") ? kPskKey
                             : key == QLatin1String("heardMe") ? kHeardKey : QString();
    if (settingKey.isEmpty() || flag(settingKey, false) == on) {
        return;
    }
    AppSettings::instance().setValue(settingKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    qCInfo(lcMobile).noquote() << "spot feeds:" << key << (on ? "on" : "off");
    // A source turned on shows its spots too (the display's Spots shown).
    if (on && (key == QLatin1String("rbn") || key == QLatin1String("psk"))) {
        setSpotSourceShown(key, true);
    }
    if (key == QLatin1String("rbn")) {
        applyRbnToCluster();
    } else {
        updatePskFeed();
    }
    emit spotFeedsChanged();
}

int MobileEngine::myAdifDxcc() const
{
    if (!m_cty) {
        return 0;
    }
    const DxccEntity* e = m_cty->entityForCallsign(baseOf(dxCallsign()));
    return e ? adifDxccForPrefix(e->primaryPrefix.startsWith(QLatin1Char('*')) ? e->primaryPrefix.mid(1) : e->primaryPrefix) : 0;
}

// The cluster's skimmer spots on or off, and the filter for this station's
// place; sent at each login and when the setting or View › From changes.
void MobileEngine::applyRbnToCluster()
{
    if (!m_dx || m_dxState != QLatin1String("connected")) {
        return;
    }
    const bool want = flag(kRbnKey, false);
    if (!want) {
        // Only what this app turned on (the call's own choice on the
        // cluster stays otherwise).
        if (flag(kRbnSetKey, false)) {
            m_dx->sendCommand(QStringLiteral("unset/skimmer"));
            AppSettings::instance().setValue(kRbnSetKey, QStringLiteral("False"));
            AppSettings::instance().save();
            qCInfo(lcMobile) << "RBN: skimmer spots off on the cluster";
        }
        return;
    }
    const QString me = baseOf(dxCallsign());
    const DxccEntity* mine = m_cty ? m_cty->entityForCallsign(me) : nullptr;
    QString near;
    if (mine) {
        const QString prefix = mine->primaryPrefix.startsWith(QLatin1Char('*')) ? mine->primaryPrefix.mid(1) : mine->primaryPrefix;
        near = spotFrom() == QLatin1String("country") ? QStringLiteral("by_dxcc %1").arg(prefix)
                                                       : QStringLiteral("by_zone %1").arg(continentZones(mine->continent));
    }
    // CW, RTTY and PSK: RBN's FT8 / FT4 spots all carry the band's FT8
    // frequency (a stack of labels on one line) and PSK Reporter has those
    // modes far better; beacons left out.
    m_dx->sendCommand(QStringLiteral("set/skimmer cw rtty psk"));
    m_dx->sendCommand(QStringLiteral("clear/rbn all"));
    if (!near.isEmpty() && !near.endsWith(QLatin1Char(' ')) && !me.isEmpty()) {
        m_dx->sendCommand(QStringLiteral("accept/rbn %1 or call %2").arg(near, me));
    }
    AppSettings::instance().setValue(kRbnSetKey, QStringLiteral("True"));
    AppSettings::instance().save();
    qCInfo(lcMobile).noquote() << "RBN: skimmer spots on the cluster," << (near.isEmpty() ? QStringLiteral("all") : near)
                               << "and" << me;
}

// A cluster spot given by a skimmer (spotter CALL-#): RBN's. Returns true
// when it is done here: this station's own call (Heard me) or a band's
// window full; false: onto the radio.
bool MobileEngine::noteRbnSpot(const QString& dxCall, double freqMhz, const QString& spotter, const QString& comment)
{
    ++m_rbnSpotsIn;
    if (baseOf(dxCall) != baseOf(dxCallsign())) {
        // Not this station: onto the radio unless the window is full (then
        // handled here as done).
        return !feedSpotAllowed(m_rbnSeen, m_rbnBandAdds, dxCall, freqMhz, spotter, kRbnRepeatMs, kRbnPerBandWindow);
    }
    if (!flag(kHeardKey, false)) {
        return true;
    }
    static const QRegularExpression snrRe(QStringLiteral("(-?\\d+)\\s*dB"));
    const QRegularExpressionMatch m = snrRe.match(comment);
    HeardReport r;
    r.receiver = stripSsid(spotter);
    r.snr = m.hasMatch() ? m.captured(1).toInt() : -999;
    r.mode = comment.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty).toUpper();
    r.band = bandFor(freqMhz);
    r.source = QStringLiteral("RBN");
    r.ms = QDateTime::currentMSecsSinceEpoch();
    r.km = distanceTo(QString(), r.receiver);
    noteHeard(r);
    return true;
}

void MobileEngine::seedHeardMe()
{
    if (!m_onlineLogNet || !m_connected || !flag(kHeardKey, false)) {
        return;
    }
    const QString me = stripSsid(dxCallsign());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (me.isEmpty() || (m_pskQueryMs > 0 && now - m_pskQueryMs < kPskQueryGapMs)) {
        return;
    }
    m_pskQueryMs = now;
    QUrl url(QStringLiteral("https://retrieve.pskreporter.info/query"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("senderCallsign"), me);
    q.addQueryItem(QStringLiteral("flowStartSeconds"), QString::number(-kHeardKeepMs / 1000));
    q.addQueryItem(QStringLiteral("rronly"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("appcontact"), QStringLiteral("OksaSDR"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OksaSDR"));
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_onlineLogNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qCInfo(lcMobile).noquote() << "Heard me: PSK Reporter's query not read -" << reply->errorString();
            return;
        }
        int added = 0;
        QXmlStreamReader xml(reply->readAll());
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("receptionReport")) {
                continue;
            }
            const QXmlStreamAttributes a = xml.attributes();
            HeardReport r;
            r.receiver = a.value(QLatin1String("receiverCallsign")).toString().toUpper();
            r.grid = a.value(QLatin1String("receiverLocator")).toString();
            r.mode = a.value(QLatin1String("mode")).toString();
            r.snr = a.hasAttribute(QLatin1String("sNR")) ? a.value(QLatin1String("sNR")).toInt() : -999;
            r.band = bandFor(a.value(QLatin1String("frequency")).toDouble() / 1e6);
            r.source = QStringLiteral("PSK Reporter");
            r.ms = a.value(QLatin1String("flowStartSeconds")).toLongLong() * 1000;
            r.km = distanceTo(r.grid, r.receiver);
            if (r.receiver.isEmpty() || r.band.isEmpty() || r.ms <= 0) {
                continue;
            }
            noteHeard(r);
            ++added;
        }
        qCInfo(lcMobile).noquote() << "Heard me: PSK Reporter's last half hour," << added << "report(s)";
    });
}

void MobileEngine::updatePskFeed()
{
    if (!m_pskMqtt) {
        return;
    }
    seedHeardMe();
    const bool psk = flag(kPskKey, false);
    const bool heard = flag(kHeardKey, false);
    QStringList topics;
    const QString me = stripSsid(dxCallsign());
    const int dxcc = myAdifDxcc();
    // Topic: pskr/filter/v2/{band}/{mode}/{tx_call}/{rx_call}/{tx_grid}/
    // {rx_grid}/{tx_dxcc}/{rx_dxcc} (mqtt.pskreporter.info).
    if (heard && !me.isEmpty()) {
        topics << QStringLiteral("pskr/filter/v2/+/+/%1/#").arg(me);
    }
    if (psk && dxcc > 0) {
        topics << QStringLiteral("pskr/filter/v2/+/+/+/+/+/+/+/%1").arg(dxcc);
    }
    if (!m_connected || topics.isEmpty()) {
        m_pskMqtt->stop();
        return;
    }
    m_pskMqtt->start(QString::fromLatin1(kPskHost), kPskPort, topics);
}

void MobileEngine::onPskReport(const QString& topic, const QByteArray& payload)
{
    Q_UNUSED(topic)
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    if (o.isEmpty()) {
        return;
    }
    const QString sender = o.value(QLatin1String("sc")).toString().toUpper();
    const QString receiver = o.value(QLatin1String("rc")).toString().toUpper();
    const double hz = o.value(QLatin1String("f")).toDouble();
    const QString mode = o.value(QLatin1String("md")).toString();
    const QString band = o.value(QLatin1String("b")).toString().toLower();
    const int snr = o.value(QLatin1String("rp")).toInt(-999);
    const QString rxGrid = o.value(QLatin1String("rl")).toString();
    if (sender.isEmpty() || receiver.isEmpty() || hz <= 0.0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // This station's own signal, heard by someone: Heard me.
    if (baseOf(sender) == baseOf(dxCallsign())) {
        if (flag(kHeardKey, false)) {
            HeardReport r;
            r.receiver = receiver;
            r.grid = rxGrid;
            r.snr = snr;
            r.mode = mode;
            r.band = band.isEmpty() ? bandFor(hz / 1e6) : band;
            r.source = QStringLiteral("PSK Reporter");
            r.ms = now;
            r.km = distanceTo(rxGrid, receiver);
            qCInfo(lcMobile).noquote() << "Heard me:" << receiver << rxGrid << mode << snr << "dB on" << r.band;
            noteHeard(r);
        }
        return;
    }
    if (!flag(kPskKey, false) || !m_connected) {
        return;
    }
    // Heard in this country: a station of this country is no DX.
    const int myDxcc = myAdifDxcc();
    if (o.value(QLatin1String("sa")).toInt() == myDxcc) {
        return;
    }
    // The band's window: once a station, at most this many new ones.
    if (!feedSpotAllowed(m_pskSeen, m_pskBandAdds, sender, hz / 1e6, receiver, kPskRepeatMs, kPskPerBandWindow)) {
        return;
    }
    const QString comment = QStringLiteral("%1 %2 dB heard by %3%4")
                                .arg(mode)
                                .arg(snr)
                                .arg(receiver)
                                .arg(rxGrid.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(rxGrid.left(4)));
    if (queueRadioSpot(sender, hz / 1e6, QStringLiteral("PSKReporter"), receiver, comment, kPskLifetimeSec,
                       QStringLiteral("#66CDAA"))) {
        ++m_pskSpotsIn;
    }
}

bool MobileEngine::feedSpotAllowed(QHash<QString, qint64>& seen, QHash<QString, QList<qint64>>& bandAdds,
                                   const QString& call, double mhz, const QString& spotter, qint64 windowMs, int perBand)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString band = bandFor(mhz);
    // Only the radio's bands (PSK Reporter has QO-100 at 10 GHz too).
    if (band.isEmpty()) {
        return false;
    }
    const QString key = call.toUpper() + QLatin1Char('|') + band;
    if (const auto it = seen.constFind(key); it != seen.constEnd() && now - it.value() < windowMs) {
        return false;
    }
    QList<qint64>& adds = bandAdds[band];
    adds.erase(std::remove_if(adds.begin(), adds.end(), [now, windowMs](qint64 t) { return now - t > windowMs; }), adds.end());
    // Full: only a station the operator hunts still goes on.
    if (adds.size() >= perBand && spotAlertReason(call, mhz, spotter).isEmpty()) {
        return false;
    }
    seen.insert(key, now);
    adds << now;
    return true;
}

double MobileEngine::distanceTo(const QString& grid, const QString& call) const
{
    double myLat = 0.0;
    double myLon = 0.0;
    bool mine = MaidenheadLocator::toLatLon(myGrid(), myLat, myLon);
    if (!mine && m_cty) {
        if (const DxccEntity* e = m_cty->entityForCallsign(baseOf(dxCallsign())); e && e->hasLatLon) {
            myLat = e->latitude;
            myLon = e->longitude;
            mine = true;
        }
    }
    if (!mine) {
        return -1.0;
    }
    double lat = 0.0;
    double lon = 0.0;
    if (!MaidenheadLocator::toLatLon(grid, lat, lon)) {
        // A skimmer's place by its country (RBN gives no locator).
        const DxccEntity* e = m_cty ? m_cty->entityForCallsign(baseOf(call)) : nullptr;
        if (!e || !e->hasLatLon) {
            return -1.0;
        }
        lat = e->latitude;
        lon = e->longitude;
    }
    return MaidenheadLocator::distanceKm(myLat, myLon, lat, lon);
}

void MobileEngine::noteHeard(const HeardReport& report)
{
    // The latest report of each receiver on a band.
    for (HeardReport& r : m_heard) {
        if (r.receiver == report.receiver && r.band == report.band) {
            r = report;
            if (!m_heardTimer.isActive()) {
                m_heardTimer.start();
            }
            return;
        }
    }
    m_heard << report;
    if (!m_heardTimer.isActive()) {
        m_heardTimer.start();
    }
}

// Heard me by band: how many heard this station in the last half hour, the
// farthest and the best report, and when last.
QVariantList MobileEngine::heardMe() const
{
    struct Row {
        QString band;
        QStringList receivers;
        QSet<QString> modes;
        QSet<QString> sources;
        QString farCall;
        double farKm{-1.0};
        QString bestCall;
        int bestSnr{-999};
        qint64 last{0};
    };
    QHash<QString, Row> rows;
    for (const HeardReport& r : m_heard) {
        Row& row = rows[r.band];
        row.band = r.band;
        if (!row.receivers.contains(r.receiver)) {
            row.receivers << r.receiver;
        }
        if (!r.mode.isEmpty()) {
            row.modes.insert(r.mode);
        }
        row.sources.insert(r.source);
        if (r.km > row.farKm) {
            row.farKm = r.km;
            row.farCall = r.receiver;
        }
        if (r.snr > row.bestSnr && r.snr != -999) {
            row.bestSnr = r.snr;
            row.bestCall = r.receiver;
        }
        row.last = std::max(row.last, r.ms);
    }
    QList<Row> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), [](const Row& a, const Row& b) { return bandOrder(a.band) < bandOrder(b.band); });
    QVariantList out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Row& r : sorted) {
        QStringList modes(r.modes.begin(), r.modes.end());
        modes.sort();
        QStringList sources(r.sources.begin(), r.sources.end());
        sources.sort();
        out << QVariantMap{{QStringLiteral("band"), r.band},
                           {QStringLiteral("count"), r.receivers.size()},
                           {QStringLiteral("modes"), modes.join(QStringLiteral(", "))},
                           {QStringLiteral("sources"), sources.join(QStringLiteral(" + "))},
                           {QStringLiteral("farCall"), r.farCall},
                           {QStringLiteral("farKm"), r.farKm < 0 ? -1 : qRound(r.farKm)},
                           {QStringLiteral("bestCall"), r.bestCall},
                           {QStringLiteral("bestSnr"), r.bestSnr},
                           {QStringLiteral("agoMin"), int((now - r.last) / 60000)}};
    }
    return out;
}

void MobileEngine::clearHeardMe()
{
    m_heard.clear();
    emit heardMeChanged();
}

} // namespace AetherSDR::mobile
