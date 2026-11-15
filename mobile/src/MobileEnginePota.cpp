// A POTA self-spot from the Log tab's My park (roadmap 67 (b), Christoph
// Steinert DM5HF): the activator's own call at the park, on the TX slice's
// frequency and mode, posted to pota.app's spot endpoint. The API is
// unofficial and wants no key; other programs post the same JSON
// (OpenHamClock, hunterlog). The app names itself in the User-Agent and the
// source field, posts only on the operator's tap after showing what goes out,
// and at most once in 30 s. POTA's answer is shown as it came.

#include "MobileEngine.h"

#include "QsoLog.h"

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

}  // namespace AetherSDR::mobile
