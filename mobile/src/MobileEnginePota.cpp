// A POTA self-spot from the Log tab's My park (roadmap 67 (b), Christoph
// Steinert DM5HF): the activator's own call at the park, on the TX slice's
// frequency and mode, posted to pota.app's spot endpoint. The API is
// unofficial and wants no key; other programs post the same JSON
// (OpenHamClock, hunterlog). The app names itself in the User-Agent and the
// source field, posts only on the operator's tap after showing what goes out,
// and at most once in 30 s. POTA's answer is shown as it came.

#include "MobileEngine.h"

#include "QsoLog.h"

#include "core/AppSettings.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr qint64 kPotaSpotIntervalMs = 30000;
// The park programs' spot lists (roadmap 109) are read once a minute, as the
// desktop's PotaClient reads POTA's: the lists are public and want no key,
// and their volunteers ask for light use.
constexpr int kParkSpotPollMs = 60000;
// A WWFF spot carries its time, not a lifetime: shown for 30 minutes from it.
constexpr qint64 kWwffSpotLifeSec = 1800;

QUrl parkSpotsUrl(const QString& program)
{
    // Development: stand-in lists for tests.
    const bool pota = program == QLatin1String("POTA");
    const QString dev = qEnvironmentVariable(pota ? "OKSASDR_POTA_SPOTS_URL" : "OKSASDR_WWFF_SPOTS_URL");
    if (!dev.isEmpty()) {
        return QUrl(dev);
    }
    return QUrl(pota ? QStringLiteral("https://api.pota.app/spot/activator")
                     : QStringLiteral("https://spots.wwff.co/static/spots.json"));
}

QString parkSpotsKey(const QString& program)
{
    return program == QLatin1String("POTA") ? QStringLiteral("MobilePotaSpots") : QStringLiteral("MobileWwffSpots");
}

bool parkSpotsOn(const QString& program)
{
    return AppSettings::instance().value(parkSpotsKey(program), QStringLiteral("False")).toString() == QLatin1String("True");
}

QUrl potaSpotUrl()
{
    // Development: a stand-in server for tests.
    const QString dev = qEnvironmentVariable("OKSASDR_POTA_SPOT_URL");
    return QUrl(dev.isEmpty() ? QStringLiteral("https://api.pota.app/spot/") : dev);
}

// POTA references: two to four letters or digits, a dash, four or five digits.
QStringList potaParks(const QString& refs)
{
    static const QRegularExpression parkRe(QStringLiteral("^[A-Z0-9]{1,4}-[0-9]{4,5}$"));
    QStringList parks;
    for (const QString& part : refs.toUpper().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        if (parkRe.match(part).hasMatch() && !parks.contains(part)) {
            parks << part;
        }
    }
    return parks;
}
}  // namespace

QString MobileEngine::mySig() const { return m_mySig; }
QString MobileEngine::myRef() const { return m_myRef; }

void MobileEngine::setMySig(const QString& sig)
{
    if (sig != m_mySig) {
        m_mySig = sig;
        emit myParkChanged();
    }
}

void MobileEngine::setMyRef(const QString& ref)
{
    if (ref != m_myRef) {
        m_myRef = ref;
        emit myParkChanged();
    }
}

// What a self-spot would post now, or why there is none: {ok, why, call,
// parks, kHz, mode}.
QVariantMap MobileEngine::potaSpotInfo() const
{
    const QVariantMap d = qsoDefaults();
    const QString call = d.value(QStringLiteral("myCall")).toString().trimmed().toUpper();
    const double mhz = d.value(QStringLiteral("freq")).toDouble();
    // The mode as POTA lists it: FT8 / FT4 while FTx runs, a PSK submode,
    // else the ADIF mode (SSB, CW, AM, FM, RTTY, DATA).
    QString mode;
    if (digiRunning()) {
        mode = digiProtocol();
    } else {
        const auto adif = QsoLog::adifMode(d.value(QStringLiteral("mode")).toString());
        mode = adif.first == QLatin1String("PSK") ? adif.second : adif.first;
    }
    const QStringList parks = potaParks(m_myRef);
    QString why;
    if (m_mySig != QLatin1String("POTA")) {
        why = tr("My park is not POTA.");
    } else if (parks.isEmpty()) {
        why = tr("Type your park's reference (e.g. US-1234) under My park.");
    } else if (call.isEmpty()) {
        why = tr("The radio's callsign is not set (Radio Setup › Radio).");
    } else if (mhz <= 0.0 || mode.isEmpty()) {
        why = tr("No slice to spot from.");
    }
    return {{QStringLiteral("ok"), why.isEmpty()}, {QStringLiteral("why"), why},
            {QStringLiteral("call"), call}, {QStringLiteral("parks"), parks},
            {QStringLiteral("kHz"), QString::number(mhz * 1000.0, 'f', 1)}, {QStringLiteral("mode"), mode}};
}

QString MobileEngine::potaSpotStatus() const { return m_potaSpotStatus; }
bool MobileEngine::potaSpotOk() const { return m_potaSpotOk; }

void MobileEngine::setPotaSpotStatus(const QString& text, bool ok)
{
    m_potaSpotStatus = text;
    m_potaSpotOk = ok;
    emit potaSpotChanged();
}

bool MobileEngine::sendPotaSpot(const QString& park, const QString& comment)
{
    const QVariantMap info = potaSpotInfo();
    if (!info.value(QStringLiteral("ok")).toBool()) {
        setPotaSpotStatus(info.value(QStringLiteral("why")).toString(), false);
        return false;
    }
    const QString ref = park.trimmed().toUpper();
    if (!info.value(QStringLiteral("parks")).toStringList().contains(ref)) {
        setPotaSpotStatus(tr("%1 is not under My park.").arg(ref), false);
        return false;
    }
    if (m_potaSpotBusy) {
        return false;
    }
    if (m_potaSpotClock.isValid() && m_potaSpotClock.elapsed() < kPotaSpotIntervalMs) {
        setPotaSpotStatus(tr("Wait %1 s between spots.").arg((kPotaSpotIntervalMs - m_potaSpotClock.elapsed() + 999) / 1000), false);
        return false;
    }
    if (!m_onlineLogNet) {
        return false;
    }
    const QString call = info.value(QStringLiteral("call")).toString();
    const QString kHz = info.value(QStringLiteral("kHz")).toString();
    const QString mode = info.value(QStringLiteral("mode")).toString();
    const QJsonObject spot{{QStringLiteral("activator"), call},
                           {QStringLiteral("spotter"), call},
                           {QStringLiteral("frequency"), kHz},
                           {QStringLiteral("reference"), ref},
                           {QStringLiteral("mode"), mode},
                           {QStringLiteral("source"), QStringLiteral("OksaSDR")},
                           {QStringLiteral("comments"), comment.simplified().left(120)}};
    QNetworkRequest req(potaSpotUrl());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OksaSDR/%1").arg(appVersion()));
    req.setTransferTimeout(15000);
    const QString what = QStringLiteral("%1 at %2, %3 kHz %4").arg(call, ref, kHz, mode);
    qCInfo(lcMobile).noquote() << "POTA: self-spot" << what;
    m_potaSpotBusy = true;
    m_potaSpotClock.start();
    setPotaSpotStatus(tr("Spotting %1…").arg(what), true);
    QNetworkReply* reply = m_onlineLogNet->post(req, QJsonDocument(spot).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, what] {
        reply->deleteLater();
        m_potaSpotBusy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).simplified().left(200);
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            qCInfo(lcMobile).noquote() << "POTA: spotted" << what << "- HTTP" << status;
            setPotaSpotStatus(tr("Spotted on POTA: %1.").arg(what), true);
        } else if (status > 0) {
            qCInfo(lcMobile).noquote() << "POTA: spot refused - HTTP" << status << body;
            setPotaSpotStatus(tr("POTA did not take it (HTTP %1): %2").arg(status).arg(body.isEmpty() ? reply->errorString() : body), false);
        } else {
            qCInfo(lcMobile).noquote() << "POTA: spot not sent -" << reply->errorString();
            setPotaSpotStatus(tr("Not sent: %1").arg(reply->errorString()), false);
        }
    });
    return true;
}


// ---- POTA and WWFF spots on the display (roadmap 109, Robin Bishop G5OUT) --
// Each program's list of current activations, read every minute while it is
// switched on and a radio is connected; a spot not seen before goes to the
// radio's list like the cluster's (so every program on the radio shows it),
// as the desktop's PotaClient does for POTA: "<reference> <park> <mode>" as
// its comment, POTA's own expiry as its lifetime. A tap on it tunes as on any
// spot and gives the Log tab the call and Their park.

void MobileEngine::setupParkSpots()
{
    m_parkSpotTimer.setInterval(kParkSpotPollMs);
    connect(&m_parkSpotTimer, &QTimer::timeout, this, [this] { pollParkSpots(); });
}

// One line for what the lists hold now ("Listed now (06:47 UTC): POTA 13,
// WWFF 1."), one for a list not read.
QVariantMap MobileEngine::parkSpots() const
{
    QStringList listed;
    QStringList failed;
    for (const QString& program : {QStringLiteral("POTA"), QStringLiteral("WWFF")}) {
        if (!parkSpotsOn(program)) {
            continue;
        }
        if (m_parkSpotFailed.contains(program)) {
            failed << tr("%1 not read (%2)").arg(program, m_parkSpotFailed.value(program));
        } else if (m_parkSpotCount.contains(program)) {
            listed << QStringLiteral("%1 %2").arg(program).arg(m_parkSpotCount.value(program));
        }
    }
    QStringList lines;
    if (!listed.isEmpty()) {
        lines << tr("Listed now (%1 UTC): %2.").arg(m_parkSpotReadUtc.toString(QStringLiteral("HH:mm")), listed.join(QStringLiteral(", ")));
    }
    if (!failed.isEmpty()) {
        lines << tr("%1; tried again in a minute.").arg(failed.join(QStringLiteral(", ")));
    }
    const bool ok = failed.isEmpty();
    return {{QStringLiteral("pota"), parkSpotsOn(QStringLiteral("POTA"))},
            {QStringLiteral("wwff"), parkSpotsOn(QStringLiteral("WWFF"))},
            {QStringLiteral("status"), lines.join(QLatin1Char('\n'))},
            {QStringLiteral("ok"), ok}};
}

void MobileEngine::setParkSpots(const QString& program, bool on)
{
    if (program != QLatin1String("POTA") && program != QLatin1String("WWFF")) {
        return;
    }
    if (parkSpotsOn(program) != on) {
        AppSettings::instance().setValue(parkSpotsKey(program), on ? QStringLiteral("True") : QStringLiteral("False"));
        qCInfo(lcMobile).noquote() << "spots:" << program << (on ? "on" : "off");
    }
    m_parkSpotCount.remove(program);
    m_parkSpotFailed.remove(program);
    if (!on) {
        // Its spots already on the radio age out there; once on again, the
        // ones still listed go again.
        const QString prefix = program + QLatin1Char(':');
        m_parkSpotSent.removeIf([&prefix](const QString& id) { return id.startsWith(prefix); });
        m_parkSpots.removeIf([&program](const QHash<QString, ParkSpot>::iterator it) { return it->sig == program; });
    }
    const bool wasPolling = m_parkSpotTimer.isActive();
    updateParkSpotPolling();
    if (on && wasPolling) {
        pollParkSpots(program);  // the other program was already being read
    }
    emit parkSpotsChanged();
}

// Read while either program is on and a radio is connected; the first read
// at once.
void MobileEngine::updateParkSpotPolling()
{
    const bool want = m_connected && (parkSpotsOn(QStringLiteral("POTA")) || parkSpotsOn(QStringLiteral("WWFF")));
    if (want && !m_parkSpotTimer.isActive()) {
        m_parkSpotTimer.start();
        pollParkSpots();
    } else if (!want && m_parkSpotTimer.isActive()) {
        m_parkSpotTimer.stop();
        // Another radio, or this one after a restart, gets them again.
        m_parkSpotSent.clear();
    }
}

void MobileEngine::pollParkSpots(const QString& only)
{
    if (!m_onlineLogNet || !m_connected) {
        return;
    }
    for (const QString& program : {QStringLiteral("POTA"), QStringLiteral("WWFF")}) {
        if (!parkSpotsOn(program) || (!only.isEmpty() && only != program)) {
            continue;
        }
        QNetworkRequest req(parkSpotsUrl(program));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OksaSDR/%1").arg(appVersion()));
        req.setTransferTimeout(15000);
        QNetworkReply* reply = m_onlineLogNet->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, program] {
            reply->deleteLater();
            if (!parkSpotsOn(program)) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError) {
                const QString why = status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
                if (m_parkSpotFailed.value(program) != why) {
                    qCInfo(lcMobile).noquote() << "spots:" << program << "not read -" << why;
                }
                m_parkSpotFailed.insert(program, why);
                emit parkSpotsChanged();
                return;
            }
            readParkSpots(program, reply->readAll());
        });
    }
}

void MobileEngine::readParkSpots(const QString& program, const QByteArray& body)
{
    static const QRegularExpression refRe(QStringLiteral("^[A-Z0-9]{1,6}-[0-9]{4,5}$"));
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        const QString why = tr("not a spot list");
        if (m_parkSpotFailed.value(program) != why) {
            qCInfo(lcMobile).noquote() << "spots:" << program << "answered something else than its list";
        }
        m_parkSpotFailed.insert(program, why);
        emit parkSpotsChanged();
        return;
    }
    const bool pota = program == QLatin1String("POTA");
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString color = AppSettings::instance()
                              .value(pota ? QStringLiteral("PotaSpotColor") : QStringLiteral("WwffSpotColor"),
                                     pota ? QStringLiteral("#FFFF00") : QStringLiteral("#66FF66"))
                              .toString();
    int listed = 0;
    int placed = 0;
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject o = value.toObject();
        const QString call = o.value(QStringLiteral("activator")).toString().trimmed().toUpper();
        const QString ref = o.value(QStringLiteral("reference")).toString().trimmed().toUpper();
        const QString mode = o.value(QStringLiteral("mode")).toString().trimmed().toUpper();
        QString id;
        double kHz = 0.0;
        QString park;
        qint64 lifeSec = 0;
        if (pota) {
            if (o.value(QStringLiteral("invalid")).toBool()) {
                continue;
            }
            id = QString::number(o.value(QStringLiteral("spotId")).toVariant().toLongLong());
            kHz = o.value(QStringLiteral("frequency")).toVariant().toDouble();
            park = o.value(QStringLiteral("name")).toString();
            if (park.isEmpty()) {
                park = o.value(QStringLiteral("parkName")).toString();
            }
            lifeSec = o.value(QStringLiteral("expire")).toInt();
            if (lifeSec <= 0) {
                lifeSec = 600;  // the desktop's
            }
        } else {
            id = QString::number(o.value(QStringLiteral("id")).toVariant().toLongLong());
            kHz = o.value(QStringLiteral("frequency_khz")).toVariant().toDouble();
            park = o.value(QStringLiteral("reference_name")).toString();
            const qint64 at = o.value(QStringLiteral("spot_time")).toVariant().toLongLong();
            lifeSec = at > 0 ? kWwffSpotLifeSec - (nowMs / 1000 - at) : 0;
        }
        if (call.isEmpty() || kHz <= 0.0 || lifeSec < 60 || !refRe.match(ref).hasMatch()) {
            continue;
        }
        ++listed;
        // The activator's park, for a tap on any spot of theirs.
        m_parkSpots.insert(call, {program, ref, nowMs + lifeSec * 1000});
        if (m_parkSpotSent.contains(program + QLatin1Char(':') + id)) {
            continue;
        }
        m_parkSpotSent.insert(program + QLatin1Char(':') + id);
        QString comment = ref;
        if (!park.trimmed().isEmpty()) {
            comment += QLatin1Char(' ') + park.simplified().left(40);
        }
        if (!mode.isEmpty()) {
            comment += QLatin1Char(' ') + mode;
        }
        const QString spotter = o.value(QStringLiteral("spotter")).toString().trimmed();
        if (queueRadioSpot(call, kHz / 1000.0, program, spotter.isEmpty() ? program : spotter, comment, int(lifeSec), color)) {
            ++placed;
        }
    }
    // Activators gone from the lists.
    m_parkSpots.removeIf([nowMs](const QHash<QString, ParkSpot>::iterator it) { return it->untilMs <= nowMs; });
    if (placed > 0 || m_parkSpotFailed.contains(program) || !m_parkSpotCount.contains(program)) {
        qCInfo(lcMobile).noquote() << "spots:" << program << "lists" << listed << "activators," << placed << "new to the radio";
    }
    m_parkSpotFailed.remove(program);
    m_parkSpotCount.insert(program, listed);
    m_parkSpotReadUtc = QDateTime::currentDateTimeUtc();
    emit parkSpotsChanged();
}

}  // namespace AetherSDR::mobile
