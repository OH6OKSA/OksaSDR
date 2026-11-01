// Wavelog / Cloudlog: every QSO the app logs goes to the operator's online
// logbook too, through its API (both share it): the key is checked with
// GET /index.php/api/auth/<key>, a QSO is POST /index.php/api/qso with the
// ADIF record. Unsent records wait in a queue kept in the settings (a phone
// is often offline) and go with the next QSO, the next start or a Save.
// Wavelog (1.8 and newer) can also be read back: the station locations from
// GET /index.php/api/station_info/<key>, then each one's QSOs from POST
// /index.php/api/get_contacts_adif, all at first and afterwards only those
// above the last QSO id read. They count as worked before (KnownLog).

#include "MobileEngine.h"

#include "QsoLog.h"
#include "SecureStore.h"

#include "core/AppSettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kKeySecret = QStringLiteral("onlinelog_key");
constexpr int kMaxPending = 500;
// QSOs asked for at a time when reading the logbook (Wavelog before 1.8.6
// sends them all at once).
constexpr int kReadLimit = 5000;
}  // namespace

struct MobileEngine::KnownFetch {
    int generation{0};      // m_knownGeneration when it began
    bool full{false};
    QStringList stations;   // station location ids
    int index{0};           // the one being read
    QJsonObject lastIds;    // station id -> the last QSO id read
    QList<QsoEntry> got;
};

void MobileEngine::setupOnlineLog()
{
    m_onlineLogNet = new QNetworkAccessManager(this);
    m_onlineLogKey = loadSecret(kKeySecret);
    const QString pending = AppSettings::instance().value(QStringLiteral("MobileOnlineLogPending")).toString();
    if (!pending.isEmpty()) {
        m_onlineLogPending = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    // A saved logbook is checked at start (and the queue sent), so the Log
    // tab says whether it is connected (it said nothing after a restart).
    if (!onlineLogBase().isEmpty() && !m_onlineLogKey.isEmpty()) {
        QTimer::singleShot(3000, this, &MobileEngine::onlineLogCheck);
    }
    // The logbook's QSOs read before, from the file (in a worker: it can
    // hold a hundred thousand); what is new is read after the key check.
    // Those of another address are not used: that logbook is read anew.
    if (onlineLogReadOn() && !onlineLogBase().isEmpty()
        && AppSettings::instance().value(QStringLiteral("MobileOnlineLogReadFrom")).toString() == onlineLogBase()) {
        QThread* reader = QThread::create([this] {
            QList<QsoEntry> known = KnownLog::read(KnownLog::defaultPath());
            QMetaObject::invokeMethod(this, [this, known = std::move(known)]() mutable {
                // A full read that finished first already holds them all;
                // what a quicker read of the new ones brought comes after.
                if (m_knownFullRead || !onlineLogReadOn()) {
                    return;
                }
                known.append(m_knownQsos);
                m_knownQsos = std::move(known);
                if (m_knownStatus.isEmpty()) {
                    m_knownStatusKind = QStringLiteral("ok");
                    m_knownStatus = tr("%1 QSOs from the logbook count as worked before.").arg(QLocale().toString(qlonglong(m_knownQsos.size())));
                }
                m_wkDirty = true;
                qCInfo(lcMobile) << "online log:" << m_knownQsos.size() << "QSOs read back before";
                emit qsoLogChanged();
                emit onlineLogChanged();
            }, Qt::QueuedConnection);
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
    }
}

void MobileEngine::onlineLogCheck()
{
    if (onlineLogBase().isEmpty() || m_onlineLogKey.isEmpty()) {
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
        } else {
            if (!body.contains(QLatin1String("<rights>rw</rights>"), Qt::CaseInsensitive)) {
                m_onlineLogStatusKind = QStringLiteral("failed");
                m_onlineLogStatus = tr("This API key is read-only: make a read & write key.");
            } else {
                m_onlineLogStatusKind = QStringLiteral("ok");
                m_onlineLogStatus = tr("Key accepted: each QSO goes to the logbook too.");
                onlineLogFlush();
            }
            // Reading needs no write right. A cache from another address is
            // read again from the start.
            if (onlineLogReadOn() && !m_knownReadThisSession) {
                onlineLogFetch(AppSettings::instance().value(QStringLiteral("MobileOnlineLogReadFrom")).toString()
                               != onlineLogBase());
            }
        }
        qCInfo(lcMobile).noquote() << "online log:" << m_onlineLogStatus;
        emit onlineLogChanged();
    });
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
            {QStringLiteral("statusKind"), m_onlineLogStatusKind},
            {QStringLiteral("read"), onlineLogReadOn()},
            {QStringLiteral("reading"), m_knownReading},
            {QStringLiteral("known"), int(m_knownQsos.size())},
            {QStringLiteral("readStatus"), m_knownStatus},
            {QStringLiteral("readStatusKind"), m_knownStatusKind}};
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
        onlineLogForget();
        emit onlineLogChanged();
        return;
    }
    // Saved again: the check reads what is new (from the start for a new
    // address, whose logbook replaces the old one's QSOs at once).
    m_knownReadThisSession = false;
    if (AppSettings::instance().value(QStringLiteral("MobileOnlineLogReadFrom")).toString() != onlineLogBase()
        && !m_knownQsos.isEmpty()) {
        onlineLogForget();
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
    onlineLogCheck();
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
            // Wavelog answers 401 for a wrong station ID too.
            m_onlineLogStatus = tr("The logbook refused the API key or the station ID: %1 QSO(s) wait").arg(m_onlineLogPending.size());
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

bool MobileEngine::onlineLogReadOn() const
{
    return AppSettings::instance().value(QStringLiteral("MobileOnlineLogRead"), QStringLiteral("False")).toString()
           == QLatin1String("True");
}

void MobileEngine::setOnlineLogRead(bool on)
{
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogRead"), on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    if (!on) {
        onlineLogForget();
        emit onlineLogChanged();
        return;
    }
    onlineLogFetch(true);
}

void MobileEngine::onlineLogRead()
{
    onlineLogFetch(AppSettings::instance().value(QStringLiteral("MobileOnlineLogReadFrom")).toString() != onlineLogBase());
}

void MobileEngine::onlineLogForget()
{
    // A read under way finishes into nothing (its generation is old).
    ++m_knownGeneration;
    m_knownQsos.clear();
    m_knownStatus.clear();
    m_knownStatusKind.clear();
    m_knownReadThisSession = false;
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogReadIds"), QString());
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogReadFrom"), QString());
    AppSettings::instance().save();
    KnownLog::remove(KnownLog::defaultPath());
    m_wkDirty = true;
    emit qsoLogChanged();
}

void MobileEngine::onlineLogFetch(bool full)
{
    if (m_knownReading) {
        return;
    }
    if (onlineLogBase().isEmpty() || m_onlineLogKey.isEmpty()) {
        m_knownStatusKind = QStringLiteral("failed");
        m_knownStatus = tr("Save the logbook's address and API key first.");
        emit onlineLogChanged();
        return;
    }
    auto fetch = std::make_shared<KnownFetch>();
    fetch->generation = m_knownGeneration;
    fetch->full = full;
    if (!full) {
        fetch->lastIds = QJsonDocument::fromJson(
            AppSettings::instance().value(QStringLiteral("MobileOnlineLogReadIds")).toString().toUtf8()).object();
    }
    m_knownReading = true;
    m_knownReadThisSession = true;
    qCInfo(lcMobile) << "online log read:" << (full ? "all QSOs" : "the new QSOs");
    m_knownStatusKind = QStringLiteral("checking");
    m_knownStatus = tr("Reading the logbook…");
    emit onlineLogChanged();
    // Every station location of the key's owner: worked before counts them all.
    QNetworkRequest req(QUrl(onlineLogBase() + QStringLiteral("/api/station_info/") + m_onlineLogKey));
    req.setTransferTimeout(20000);
    QNetworkReply* reply = m_onlineLogNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fetch] {
        reply->deleteLater();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && http == 0) {
            onlineLogFetchDone(fetch, tr("Logbook not reached (%1).").arg(reply->errorString()));
            return;
        }
        if (http == 401 || http == 403) {
            onlineLogFetchDone(fetch, tr("The logbook refused the API key."));
            return;
        }
        for (const QJsonValue& v : doc.array()) {
            const QString id = v.toObject().value(QStringLiteral("station_id")).toVariant().toString();
            if (!id.isEmpty()) {
                fetch->stations.append(id);
            }
        }
        if (!doc.isArray()) {
            onlineLogFetchDone(fetch, tr("This logbook cannot be read: it needs Wavelog 1.8 or newer."));
            return;
        }
        if (fetch->stations.isEmpty()) {
            onlineLogFetchDone(fetch, tr("The logbook has no station locations."));
            return;
        }
        onlineLogFetchNext(fetch);
    });
}

void MobileEngine::onlineLogFetchNext(const std::shared_ptr<KnownFetch>& fetch)
{
    if (fetch->index >= fetch->stations.size() || fetch->generation != m_knownGeneration) {
        onlineLogFetchDone(fetch, QString());
        return;
    }
    const QString station = fetch->stations.at(fetch->index);
    QJsonObject body{{QStringLiteral("key"), m_onlineLogKey},
                     {QStringLiteral("station_id"), station.toInt()},
                     {QStringLiteral("fetchfromid"), fetch->lastIds.value(station).toInteger()},
                     {QStringLiteral("limit"), kReadLimit}};
    QNetworkRequest req(QUrl(onlineLogBase() + QStringLiteral("/api/get_contacts_adif")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(60000);
    QNetworkReply* reply = m_onlineLogNet->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, fetch, station] {
        reply->deleteLater();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && http == 0) {
            onlineLogFetchDone(fetch, tr("Logbook not reached (%1).").arg(reply->errorString()));
            return;
        }
        if (http == 401 || http == 403) {
            onlineLogFetchDone(fetch, tr("The logbook refused the API key."));
            return;
        }
        if (http == 404) {
            onlineLogFetchDone(fetch, tr("This logbook cannot be read: it needs Wavelog 1.8 or newer (Cloudlog cannot send its QSOs)."));
            return;
        }
        // Thousands of records: the JSON and the ADIF in a worker.
        QThread* worker = QThread::create([this, data = std::move(data), fetch, station]() mutable {
            const QJsonObject answer = QJsonDocument::fromJson(data).object();
            data.clear();
            QList<QsoEntry> entries;
            if (answer.value(QStringLiteral("status")).toString() == QLatin1String("successfull")
                || answer.contains(QStringLiteral("lastfetchedid"))) {
                QsoLog::parseAdifEach(answer.value(QStringLiteral("adif")).toString().toUtf8(), [&](QVariantMap&& r) {
                    if (!r.contains(QStringLiteral("BAND")) && r.contains(QStringLiteral("FREQ"))) {
                        r.insert(QStringLiteral("BAND"), QsoLog::bandOf(r.value(QStringLiteral("FREQ")).toDouble()));
                    }
                    QsoEntry e = QsoLog::entryOf(r);
                    if (!e.call.isEmpty()) {
                        entries.append(std::move(e));
                    }
                });
            }
            QMetaObject::invokeMethod(this, [this, answer, entries = std::move(entries), fetch, station]() mutable {
                if (!answer.contains(QStringLiteral("lastfetchedid"))) {
                    const QString why = answer.value(QStringLiteral("reason")).toString(answer.value(QStringLiteral("message")).toString());
                    onlineLogFetchDone(fetch, why.isEmpty() ? tr("This logbook cannot be read: it needs Wavelog 1.8 or newer.")
                                                            : tr("The logbook did not send its QSOs: %1").arg(why));
                    return;
                }
                const qint64 last = answer.value(QStringLiteral("lastfetchedid")).toVariant().toLongLong();
                const int exported = answer.value(QStringLiteral("exported_qsos")).toInt();
                fetch->got.append(entries);
                if (last > fetch->lastIds.value(station).toInteger()) {
                    fetch->lastIds.insert(station, last);
                }
                // A full batch: more may wait at this station.
                if (exported < kReadLimit || entries.isEmpty()) {
                    ++fetch->index;
                }
                m_knownStatus = tr("Reading the logbook… %1 QSOs").arg(QLocale().toString(qlonglong(fetch->got.size())));
                emit onlineLogChanged();
                onlineLogFetchNext(fetch);
            }, Qt::QueuedConnection);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    });
}

void MobileEngine::onlineLogFetchDone(const std::shared_ptr<KnownFetch>& fetch, const QString& error)
{
    m_knownReading = false;
    if (fetch->generation != m_knownGeneration) {
        // Forgotten meanwhile (off, or another address): nothing kept; on
        // again, read it all.
        if (onlineLogReadOn()) {
            onlineLogFetch(true);
        }
        emit onlineLogChanged();
        return;
    }
    if (!error.isEmpty()) {
        m_knownStatusKind = QStringLiteral("failed");
        m_knownStatus = error;
        qCWarning(lcMobile).noquote() << "online log read:" << error;
        emit onlineLogChanged();
        return;
    }
    const int added = int(fetch->got.size());
    if (fetch->full) {
        m_knownQsos = fetch->got;
        m_knownFullRead = true;
    } else {
        m_knownQsos.append(fetch->got);
    }
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogReadIds"),
                                     QString::fromUtf8(QJsonDocument(fetch->lastIds).toJson(QJsonDocument::Compact)));
    AppSettings::instance().setValue(QStringLiteral("MobileOnlineLogReadFrom"), onlineLogBase());
    AppSettings::instance().save();
    // The file in a worker: the whole list after a full read, else the new.
    const QString path = KnownLog::defaultPath();
    const bool full = fetch->full;
    QList<QsoEntry> toWrite = full ? m_knownQsos : fetch->got;
    if (full || !toWrite.isEmpty()) {
        QThread* writer = QThread::create([path, full, toWrite = std::move(toWrite)] {
            const bool ok = full ? KnownLog::write(path, toWrite) : KnownLog::append(path, toWrite);
            if (!ok) {
                qCWarning(lcMobile) << "online log read: cannot write" << path;
            }
        });
        connect(writer, &QThread::finished, writer, &QObject::deleteLater);
        writer->start();
    }
    m_knownStatusKind = QStringLiteral("ok");
    m_knownStatus = full ? tr("%1 QSOs from the logbook count as worked before.").arg(QLocale().toString(qlonglong(m_knownQsos.size())))
                         : tr("%1 QSOs from the logbook count as worked before (%2 new).").arg(QLocale().toString(qlonglong(m_knownQsos.size()))).arg(QLocale().toString(added));
    qCInfo(lcMobile).noquote() << "online log read:" << m_knownQsos.size() << "QSOs," << added << "read now from"
                               << fetch->stations.size() << "station location(s)";
    m_wkDirty = true;
    emit qsoLogChanged();
    emit onlineLogChanged();
}

}  // namespace AetherSDR::mobile
