// Wavelog / Cloudlog: every QSO the app logs goes to the operator's online
// logbook too, through its API (both share it): the key is checked with
// GET /index.php/api/auth/<key>, a QSO is POST /index.php/api/qso with the
// ADIF record. Unsent records wait in a queue kept in the settings (a phone
// is often offline) and go with the next QSO, the next start or a Save.

#include "MobileEngine.h"

#include "QsoLog.h"
#include "SecureStore.h"

#include "core/AppSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kKeySecret = QStringLiteral("onlinelog_key");
constexpr int kMaxPending = 500;
}  // namespace

void MobileEngine::setupOnlineLog()
{
    m_onlineLogNet = new QNetworkAccessManager(this);
    m_onlineLogKey = loadSecret(kKeySecret);
    const QString pending = AppSettings::instance().value(QStringLiteral("MobileOnlineLogPending")).toString();
    if (!pending.isEmpty()) {
        m_onlineLogPending = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (!m_onlineLogPending.isEmpty()) {
        QTimer::singleShot(5000, this, &MobileEngine::onlineLogFlush);
    }
}

QString MobileEngine::onlineLogBase() const
{
    QString url = AppSettings::instance().value(QStringLiteral("MobileOnlineLogUrl")).toString().trimmed();
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    if (url.isEmpty()) {
        return {};
    }
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://"))) {
        url.prepend(QStringLiteral("https://"));
    }
    // Both accept /index.php/api/...; an address given with index.php keeps it.
    return url.endsWith(QLatin1String("/index.php")) ? url : url + QStringLiteral("/index.php");
}

QVariantMap MobileEngine::onlineLog() const
{
    return {{QStringLiteral("url"), AppSettings::instance().value(QStringLiteral("MobileOnlineLogUrl")).toString()},
            {QStringLiteral("stationId"), AppSettings::instance().value(QStringLiteral("MobileOnlineLogStation")).toString()},
            {QStringLiteral("hasKey"), !m_onlineLogKey.isEmpty()},
            {QStringLiteral("enabled"), !onlineLogBase().isEmpty() && !m_onlineLogKey.isEmpty()},
            {QStringLiteral("pending"), m_onlineLogPending.size()},
            {QStringLiteral("status"), m_onlineLogStatus},
            {QStringLiteral("statusKind"), m_onlineLogStatusKind}};
}

void MobileEngine::setOnlineLog(const QString& url, const QString& key, const QString& stationId)
{
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogUrl"), url.trimmed());
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogStation"), stationId.trimmed());
    AppSettings::instance().save();
    if (url.trimmed().isEmpty()) {
        // Off: the key and the unsent queue go too (they would otherwise
        // reach whatever logbook is set next).
        removeSecret(kKeySecret);
        m_onlineLogKey.clear();
        m_onlineLogPending.clear();
        AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogPending"), QString());
        AppSettings::instance().save();
        m_onlineLogStatus.clear();
        m_onlineLogStatusKind.clear();
        emit onlineLogChanged();
        return;
    }
    if (!key.trimmed().isEmpty()) {
        m_onlineLogKey = key.trimmed();
        saveSecret(kKeySecret, m_onlineLogKey);
    }
    if (m_onlineLogKey.isEmpty()) {
        m_onlineLogStatusKind = QStringLiteral("failed");
        m_onlineLogStatus = tr("Type the API key too (Wavelog / Cloudlog: Account › API keys).");
        emit onlineLogChanged();
        return;
    }
    m_onlineLogStatusKind = QStringLiteral("checking");
    m_onlineLogStatus = tr("Checking the API key…");
    emit onlineLogChanged();
    QNetworkRequest req(QUrl(onlineLogBase() + QStringLiteral("/api/auth/") + m_onlineLogKey));
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_onlineLogNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QString body = QString::fromUtf8(reply->readAll());
        if (reply->error() != QNetworkReply::NoError) {
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("No answer from the logbook: %1").arg(reply->errorString());
        } else if (!body.contains(QLatin1String("<status>Valid</status>"), Qt::CaseInsensitive)) {
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("The logbook does not accept this API key.");
        } else if (!body.contains(QLatin1String("<rights>rw</rights>"), Qt::CaseInsensitive)) {
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("This API key is read-only: make a read & write key.");
        } else {
            m_onlineLogStatusKind = QStringLiteral("ok");
            m_onlineLogStatus = tr("Key accepted: each QSO goes to the logbook too.");
            onlineLogFlush();
        }
        qCInfo(lcMobile).noquote() << "online log:" << m_onlineLogStatus;
        emit onlineLogChanged();
    });
}

void MobileEngine::onlineLogQueue(const QString& adif)
{
    if (onlineLogBase().isEmpty() || m_onlineLogKey.isEmpty()) {
        return;
    }
    m_onlineLogPending.append(adif);
    while (m_onlineLogPending.size() > kMaxPending) {
        m_onlineLogPending.removeFirst();
    }
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogPending"), m_onlineLogPending.join(QLatin1Char('\n')));
    AppSettings::instance().save();
    onlineLogFlush();
}

void MobileEngine::onlineLogFlush()
{
    if (m_onlineLogSending || m_onlineLogPending.isEmpty() || onlineLogBase().isEmpty() || m_onlineLogKey.isEmpty()) {
        emit onlineLogChanged();
        return;
    }
    m_onlineLogSending = true;
    const QString adif = m_onlineLogPending.first();
    QJsonObject body{{QStringLiteral("key"), m_onlineLogKey},
                     {QStringLiteral("station_profile_id"), AppSettings::instance().value(QStringLiteral("MobileOnlineLogStation"), QStringLiteral("1")).toString()},
                     {QStringLiteral("type"), QStringLiteral("adif")},
                     {QStringLiteral("string"), adif}};
    QNetworkRequest req(QUrl(onlineLogBase() + QStringLiteral("/api/qso")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(20000);
    QNetworkReply* reply = m_onlineLogNet->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, adif] {
        reply->deleteLater();
        m_onlineLogSending = false;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QString status = answer.value(QStringLiteral("status")).toString();
        if (reply->error() == QNetworkReply::NoError && (status == QLatin1String("created") || http == 201)) {
            m_onlineLogPending.removeOne(adif);
            AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogPending"), m_onlineLogPending.join(QLatin1Char('\n')));
            AppSettings::instance().save();
            m_onlineLogStatusKind = QStringLiteral("ok");
            m_onlineLogStatus = m_onlineLogPending.isEmpty() ? tr("QSO sent to the logbook.")
                                                             : tr("Sending to the logbook: %1 left").arg(m_onlineLogPending.size());
            qCInfo(lcMobile).noquote() << "online log: sent," << m_onlineLogPending.size() << "left";
            onlineLogFlush();
        } else if (http == 401 || http == 403) {
            // The key: nothing is dropped, the queue waits for a good one.
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("The logbook refused the API key: %1 QSO(s) wait").arg(m_onlineLogPending.size());
            qCWarning(lcMobile).noquote() << "online log:" << m_onlineLogStatus;
        } else if (reply->error() == QNetworkReply::NoError || http == 400) {
            // This record refused (a bad or duplicate QSO): out of the
            // queue's way, still in the app's own log.
            m_onlineLogPending.removeOne(adif);
            AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogPending"), m_onlineLogPending.join(QLatin1Char('\n')));
            AppSettings::instance().save();
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("The logbook refused a QSO: %1").arg(answer.value(QStringLiteral("reason")).toString(status.isEmpty() ? QString::number(http) : status));
            qCWarning(lcMobile).noquote() << "online log:" << m_onlineLogStatus;
        } else {
            // Offline or the server away: kept, tried again with the next QSO or start.
            m_onlineLogStatusKind = QStringLiteral("failed");
            m_onlineLogStatus = tr("Logbook not reached (%1): %2 QSO(s) wait").arg(reply->errorString()).arg(m_onlineLogPending.size());
            qCInfo(lcMobile).noquote() << "online log:" << m_onlineLogStatus;
        }
        emit onlineLogChanged();
    });
}

}  // namespace AetherSDR::mobile
