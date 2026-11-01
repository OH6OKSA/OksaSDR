// QRZ.com Logbook, both ways (its API, https://logbook.qrz.com/api; every
// action needs a QRZ subscription): every QSO the app logs goes there with
// ACTION=INSERT, queued while offline as for Wavelog, and with Worked before
// from the logbook on, its QSOs are read back with ACTION=FETCH (250 at a
// time after the highest app_qrzlog_logid read) and count as worked before
// beside the app's own log, as Wavelog's do. Requests are name=value pairs
// POSTed with an identifying User-Agent; answers are name=value pairs too
// ("RESULT=OK&COUNT=...&ADIF=..."), the ADIF HTML-escaped.

#include "MobileEngine.h"

#include "QsoLog.h"
#include "SecureStore.h"

#include "core/AppSettings.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kQrzKeySecret = QStringLiteral("qrzlog_key");
constexpr int kQrzMaxPending = 500;
constexpr int kQrzPage = 250;  // QRZ's own advice for paging a big logbook

QUrl qrzLogUrl()
{
    // Development: a stand-in server for tests.
    const QString dev = qEnvironmentVariable("OKSASDR_QRZLOG_URL");
    return QUrl(dev.isEmpty() ? QStringLiteral("https://logbook.qrz.com/api") : dev);
}

// The name=value pairs of an answer before its ADIF (which can hold '&').
QHash<QString, QString> qrzFields(const QByteArray& body)
{
    QHash<QString, QString> fields;
    const qsizetype adif = body.indexOf("ADIF=");
    const QByteArray head = adif >= 0 ? body.left(adif) : body;
    for (const QByteArray& pair : head.split('&')) {
        const qsizetype eq = pair.indexOf('=');
        if (eq > 0) {
            fields.insert(QString::fromUtf8(pair.left(eq)).trimmed().toUpper(),
                          QUrl::fromPercentEncoding(pair.mid(eq + 1)).trimmed());
        }
    }
    return fields;
}

QByteArray unescapeHtml(QByteArray text)
{
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&#039;", "'").replace("&#39;", "'");
    return text.replace("&amp;", "&");
}

// "no QSOs found" and the like: an empty answer, not a failure.
bool qrzNothingFound(const QString& reason)
{
    static const QRegularExpression nothing(QStringLiteral("(no|not) (qsos?|records?|matches|logs?)|not found|nothing"),
                                            QRegularExpression::CaseInsensitiveOption);
    return nothing.match(reason).hasMatch();
}

bool qrzAuthFailed(const QHash<QString, QString>& f)
{
    return f.value(QStringLiteral("STATUS")) == QLatin1String("AUTH") || f.value(QStringLiteral("RESULT")) == QLatin1String("AUTH");
}
}  // namespace

struct MobileEngine::QrzFetch {
    int generation{0};
    bool full{false};
    qint64 afterId{0};      // the next page starts here (QRZ: >= this)
    qint64 lastId{0};       // the highest log id read
    QList<QsoEntry> got;
};

QNetworkReply* MobileEngine::qrzLogPost(const QList<QPair<QString, QString>>& fields)
{
    // Every value fully percent-encoded: ADIF holds '<', '&', '+'.
    QByteArray body = "KEY=" + QUrl::toPercentEncoding(m_qrzLogKey);
    for (const auto& f : fields) {
        body += '&' + f.first.toUtf8() + '=' + QUrl::toPercentEncoding(f.second);
    }
    QNetworkRequest req(qrzLogUrl());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OksaSDR/%1").arg(appVersion()));
    req.setTransferTimeout(60000);
    return m_onlineLogNet->post(req, body);
}

void MobileEngine::setupQrzLog()
{
    m_qrzLogKey = loadSecret(kQrzKeySecret);
    const QString pending = AppSettings::instance().value(QStringLiteral("MobileQrzLogPending")).toString();
    if (!pending.isEmpty()) {
        m_qrzLogPending = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (m_qrzLogKey.isEmpty()) {
        return;
    }
    QTimer::singleShot(3500, this, &MobileEngine::qrzLogCheck);
    if (qrzLogReadOn()) {
        QThread* reader = QThread::create([this] {
            QList<QsoEntry> known = KnownLog::read(KnownLog::qrzPath());
            QMetaObject::invokeMethod(this, [this, known = std::move(known)]() mutable {
                if (m_qrzFullRead || !qrzLogReadOn()) {
                    return;
                }
                known.append(m_qrzKnown);
                m_qrzKnown = std::move(known);
                if (m_qrzReadStatus.isEmpty()) {
                    m_qrzReadStatusKind = QStringLiteral("ok");
                    m_qrzReadStatus = tr("%1 QSOs from QRZ.com count as worked before.").arg(m_qrzKnown.size());
                }
                m_wkDirty = true;
                qCInfo(lcMobile) << "QRZ logbook:" << m_qrzKnown.size() << "QSOs read back before";
                emit qsoLogChanged();
                emit qrzLogChanged();
            }, Qt::QueuedConnection);
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
    }
}

QVariantMap MobileEngine::qrzLog() const
{
    return {{QStringLiteral("hasKey"), !m_qrzLogKey.isEmpty()},
            {QStringLiteral("pending"), m_qrzLogPending.size()},
            {QStringLiteral("status"), m_qrzLogStatus},
            {QStringLiteral("statusKind"), m_qrzLogStatusKind},
            {QStringLiteral("read"), qrzLogReadOn()},
            {QStringLiteral("reading"), m_qrzReading},
            {QStringLiteral("known"), int(m_qrzKnown.size())},
            {QStringLiteral("readStatus"), m_qrzReadStatus},
            {QStringLiteral("readStatusKind"), m_qrzReadStatusKind}};
}

void MobileEngine::setQrzLogKey(const QString& key)
{
    if (!key.trimmed().isEmpty()) {
        // Another logbook's key: the old one's QSOs go.
        if (!m_qrzLogKey.isEmpty() && key.trimmed() != m_qrzLogKey) {
            qrzLogForget();
        }
        m_qrzLogKey = key.trimmed();
        saveSecret(kQrzKeySecret, m_qrzLogKey);
    }
    if (m_qrzLogKey.isEmpty()) {
        m_qrzLogStatusKind = QStringLiteral("failed");
        m_qrzLogStatus = tr("Type the logbook's API key (QRZ.com › Logbook › Settings).");
        emit qrzLogChanged();
        return;
    }
    m_qrzReadThisSession = false;
    qrzLogCheck();
}

void MobileEngine::clearQrzLog()
{
    // Off: the key, the unsent queue and the QSOs read back all go.
    removeSecret(kQrzKeySecret);
    m_qrzLogKey.clear();
    m_qrzLogPending.clear();
    AppSettings::instance().setValue(QStringLiteral("MobileQrzLogPending"), QString());
    AppSettings::instance().save();
    m_qrzLogStatus.clear();
    m_qrzLogStatusKind.clear();
    qrzLogForget();
    emit qrzLogChanged();
}

void MobileEngine::qrzLogCheck()
{
    if (m_qrzLogKey.isEmpty()) {
        return;
    }
    m_qrzLogStatusKind = QStringLiteral("checking");
    m_qrzLogStatus = tr("Checking the API key…");
    emit qrzLogChanged();
    // STATUS: OK for a good key (FAIL or AUTH otherwise).
    QNetworkReply* reply = qrzLogPost({{QStringLiteral("ACTION"), QStringLiteral("STATUS")}});
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QHash<QString, QString> f = qrzFields(body);
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            m_qrzLogStatusKind = QStringLiteral("failed");
            m_qrzLogStatus = tr("QRZ.com did not answer: %1").arg(reply->errorString());
        } else if (f.value(QStringLiteral("RESULT")) != QLatin1String("OK")) {
            m_qrzLogStatusKind = QStringLiteral("failed");
            const QString reason = f.value(QStringLiteral("REASON"));
            m_qrzLogStatus = qrzAuthFailed(f) || reason.isEmpty()
                                 ? tr("QRZ.com did not accept this logbook API key (a QRZ subscription is needed).")
                                 : tr("QRZ.com did not accept the key: %1").arg(reason);
        } else {
            m_qrzLogStatusKind = QStringLiteral("ok");
            m_qrzLogStatus = tr("Key accepted: each QSO goes to the QRZ.com logbook too.");
            qrzLogFlush();
            if (qrzLogReadOn() && !m_qrzReadThisSession) {
                qrzLogFetch(AppSettings::instance().value(QStringLiteral("MobileQrzLogReadId")).toString().isEmpty());
            }
        }
        qCInfo(lcMobile).noquote() << "QRZ logbook:" << m_qrzLogStatus;
        emit qrzLogChanged();
    });
}

void MobileEngine::qrzLogQueue(const QString& adif)
{
    if (m_qrzLogKey.isEmpty()) {
        return;
    }
    m_qrzLogPending.append(adif);
    while (m_qrzLogPending.size() > kQrzMaxPending) {
        m_qrzLogPending.removeFirst();
    }
    AppSettings::instance().setValue(QStringLiteral("MobileQrzLogPending"), m_qrzLogPending.join(QLatin1Char('\n')));
    AppSettings::instance().save();
    qrzLogFlush();
}

void MobileEngine::qrzLogFlush()
{
    if (m_qrzLogSending || m_qrzLogPending.isEmpty() || m_qrzLogKey.isEmpty()) {
        emit qrzLogChanged();
        return;
    }
    m_qrzLogSending = true;
    const QString adif = m_qrzLogPending.first();
    QNetworkReply* reply = qrzLogPost({{QStringLiteral("ACTION"), QStringLiteral("INSERT")}, {QStringLiteral("ADIF"), adif}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, adif] {
        reply->deleteLater();
        m_qrzLogSending = false;
        const QByteArray body = reply->readAll();
        const QHash<QString, QString> f = qrzFields(body);
        const QString result = f.value(QStringLiteral("RESULT"));
        const QString reason = f.value(QStringLiteral("REASON"));
        const auto drop = [this, &adif] {
            m_qrzLogPending.removeOne(adif);
            AppSettings::instance().setValue(QStringLiteral("MobileQrzLogPending"), m_qrzLogPending.join(QLatin1Char('\n')));
            AppSettings::instance().save();
        };
        if (result == QLatin1String("OK") || result == QLatin1String("REPLACE")
            || reason.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)) {
            // Already there (a duplicate, e.g. sent by Wavelog too) counts as sent.
            drop();
            m_qrzLogStatusKind = QStringLiteral("ok");
            m_qrzLogStatus = m_qrzLogPending.isEmpty() ? tr("QSO sent to the QRZ.com logbook.")
                                                       : tr("Sending to the QRZ.com logbook: %1 left").arg(m_qrzLogPending.size());
            qCInfo(lcMobile).noquote() << "QRZ logbook:" << (result == QLatin1String("FAIL") ? "already there (duplicate)," : "sent,")
                                       << m_qrzLogPending.size() << "left";
            qrzLogFlush();
        } else if (qrzAuthFailed(f)) {
            // The key: nothing dropped, the queue waits for a good one.
            m_qrzLogStatusKind = QStringLiteral("failed");
            m_qrzLogStatus = tr("QRZ.com refused the API key: %1 QSO(s) wait").arg(m_qrzLogPending.size());
            qCWarning(lcMobile).noquote() << "QRZ logbook:" << m_qrzLogStatus;
        } else if (result == QLatin1String("FAIL")) {
            // This record refused (out of date range, wrong station call...):
            // out of the queue's way, still in the app's own log.
            drop();
            m_qrzLogStatusKind = QStringLiteral("failed");
            m_qrzLogStatus = tr("QRZ.com refused a QSO: %1").arg(reason.isEmpty() ? result : reason);
            qCWarning(lcMobile).noquote() << "QRZ logbook:" << m_qrzLogStatus;
        } else {
            // Offline or no answer: kept, tried again with the next QSO or start.
            m_qrzLogStatusKind = QStringLiteral("failed");
            m_qrzLogStatus = tr("QRZ.com not reached (%1): %2 QSO(s) wait")
                                 .arg(reply->error() != QNetworkReply::NoError ? reply->errorString() : QString::fromUtf8(body.left(80)))
                                 .arg(m_qrzLogPending.size());
            qCInfo(lcMobile).noquote() << "QRZ logbook:" << m_qrzLogStatus;
        }
        emit qrzLogChanged();
    });
}

void MobileEngine::devQrzInsert(const QString& call)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    qrzLogQueue(QsoLog::adifRecord({{QStringLiteral("CALL"), call.toUpper()},
                                    {QStringLiteral("QSO_DATE"), now.toString(QStringLiteral("yyyyMMdd"))},
                                    {QStringLiteral("TIME_ON"), now.toString(QStringLiteral("HHmm"))},
                                    {QStringLiteral("BAND"), QStringLiteral("20m")},
                                    {QStringLiteral("MODE"), QStringLiteral("FT8")}}));
}

bool MobileEngine::qrzLogReadOn() const
{
    return AppSettings::instance().value(QStringLiteral("MobileQrzLogRead"), QStringLiteral("False")).toString()
           == QLatin1String("True");
}

void MobileEngine::setQrzLogRead(bool on)
{
    AppSettings::instance().setValue(QStringLiteral("MobileQrzLogRead"), on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    if (!on) {
        qrzLogForget();
        emit qrzLogChanged();
        return;
    }
    qrzLogFetch(true);
}

void MobileEngine::qrzLogRead()
{
    qrzLogFetch(AppSettings::instance().value(QStringLiteral("MobileQrzLogReadId")).toString().isEmpty());
}

void MobileEngine::qrzLogForget()
{
    ++m_qrzGeneration;
    m_qrzKnown.clear();
    m_qrzReadStatus.clear();
    m_qrzReadStatusKind.clear();
    m_qrzReadThisSession = false;
    AppSettings::instance().setValue(QStringLiteral("MobileQrzLogReadId"), QString());
    AppSettings::instance().save();
    KnownLog::remove(KnownLog::qrzPath());
    m_wkDirty = true;
    emit qsoLogChanged();
}

void MobileEngine::qrzLogFetch(bool full)
{
    if (m_qrzReading) {
        return;
    }
    if (m_qrzLogKey.isEmpty()) {
        m_qrzReadStatusKind = QStringLiteral("failed");
        m_qrzReadStatus = tr("Save the logbook's API key first.");
        emit qrzLogChanged();
        return;
    }
    auto fetch = std::make_shared<QrzFetch>();
    fetch->generation = m_qrzGeneration;
    fetch->full = full;
    if (!full) {
        fetch->lastId = AppSettings::instance().value(QStringLiteral("MobileQrzLogReadId")).toString().toLongLong();
        fetch->afterId = fetch->lastId + 1;
    }
    m_qrzReading = true;
    m_qrzReadThisSession = true;
    m_qrzReadStatusKind = QStringLiteral("checking");
    m_qrzReadStatus = tr("Reading the QRZ.com logbook…");
    qCInfo(lcMobile) << "QRZ logbook read:" << (full ? "all QSOs" : "the new QSOs");
    emit qrzLogChanged();
    qrzLogFetchNext(fetch);
}

void MobileEngine::qrzLogFetchNext(const std::shared_ptr<QrzFetch>& fetch)
{
    if (fetch->generation != m_qrzGeneration) {
        qrzLogFetchDone(fetch, QString());
        return;
    }
    QNetworkReply* reply = qrzLogPost({{QStringLiteral("ACTION"), QStringLiteral("FETCH")},
                                       {QStringLiteral("OPTION"),
                                        QStringLiteral("TYPE:ADIF,MAX:%1,AFTERLOGID:%2").arg(kQrzPage).arg(fetch->afterId)}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, fetch] {
        reply->deleteLater();
        QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            qrzLogFetchDone(fetch, tr("QRZ.com not reached (%1).").arg(reply->errorString()));
            return;
        }
        const QHash<QString, QString> f = qrzFields(body);
        const QString result = f.value(QStringLiteral("RESULT"));
        const QString reason = f.value(QStringLiteral("REASON"));
        if (result != QLatin1String("OK")) {
            if (qrzAuthFailed(f)) {
                qrzLogFetchDone(fetch, tr("QRZ.com refused the API key."));
            } else if (qrzNothingFound(reason) || f.value(QStringLiteral("COUNT")) == QLatin1String("0")) {
                qrzLogFetchDone(fetch, QString());  // nothing (more) to read
            } else {
                qrzLogFetchDone(fetch, tr("QRZ.com did not send the logbook: %1").arg(reason.isEmpty() ? result : reason));
            }
            return;
        }
        // The records in a worker: 250 a page, a big logbook many pages.
        QThread* worker = QThread::create([this, body = std::move(body), fetch]() mutable {
            QList<QsoEntry> entries;
            qint64 maxId = 0;
            const qsizetype at = body.indexOf("ADIF=");
            if (at >= 0) {
                QsoLog::parseAdifEach(unescapeHtml(body.mid(at + 5)), [&](QVariantMap&& r) {
                    maxId = std::max(maxId, r.value(QStringLiteral("APP_QRZLOG_LOGID")).toLongLong());
                    if (!r.contains(QStringLiteral("BAND")) && r.contains(QStringLiteral("FREQ"))) {
                        r.insert(QStringLiteral("BAND"), QsoLog::bandOf(r.value(QStringLiteral("FREQ")).toDouble()));
                    }
                    QsoEntry e = QsoLog::entryOf(r);
                    if (!e.call.isEmpty()) {
                        entries.append(std::move(e));
                    }
                });
            }
            body.clear();
            QMetaObject::invokeMethod(this, [this, entries = std::move(entries), maxId, fetch]() mutable {
                const qsizetype n = entries.size();
                fetch->got.append(entries);
                fetch->lastId = std::max(fetch->lastId, maxId);
                // A full page: more may follow after the highest id.
                if (n < kQrzPage || maxId <= 0) {
                    qrzLogFetchDone(fetch, QString());
                    return;
                }
                fetch->afterId = maxId + 1;
                m_qrzReadStatus = tr("Reading the QRZ.com logbook… %1 QSOs").arg(fetch->got.size());
                emit qrzLogChanged();
                qrzLogFetchNext(fetch);
            }, Qt::QueuedConnection);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    });
}

void MobileEngine::qrzLogFetchDone(const std::shared_ptr<QrzFetch>& fetch, const QString& error)
{
    m_qrzReading = false;
    if (fetch->generation != m_qrzGeneration) {
        if (qrzLogReadOn() && !m_qrzLogKey.isEmpty()) {
            qrzLogFetch(true);
        }
        emit qrzLogChanged();
        return;
    }
    if (!error.isEmpty()) {
        m_qrzReadStatusKind = QStringLiteral("failed");
        m_qrzReadStatus = error;
        qCWarning(lcMobile).noquote() << "QRZ logbook read:" << error;
        emit qrzLogChanged();
        return;
    }
    const int added = int(fetch->got.size());
    if (fetch->full) {
        m_qrzKnown = fetch->got;
        m_qrzFullRead = true;
    } else {
        m_qrzKnown.append(fetch->got);
    }
    AppSettings::instance().setValue(QStringLiteral("MobileQrzLogReadId"), QString::number(fetch->lastId));
    AppSettings::instance().save();
    const QString path = KnownLog::qrzPath();
    const bool full = fetch->full;
    QList<QsoEntry> toWrite = full ? m_qrzKnown : fetch->got;
    if (full || !toWrite.isEmpty()) {
        QThread* writer = QThread::create([path, full, toWrite = std::move(toWrite)] {
            if (!(full ? KnownLog::write(path, toWrite) : KnownLog::append(path, toWrite))) {
                qCWarning(lcMobile) << "QRZ logbook read: cannot write" << path;
            }
        });
        connect(writer, &QThread::finished, writer, &QObject::deleteLater);
        writer->start();
    }
    m_qrzReadStatusKind = QStringLiteral("ok");
    m_qrzReadStatus = full ? tr("%1 QSOs from QRZ.com count as worked before.").arg(m_qrzKnown.size())
                           : tr("%1 QSOs from QRZ.com count as worked before (%2 new).").arg(m_qrzKnown.size()).arg(added);
    qCInfo(lcMobile).noquote() << "QRZ logbook read:" << m_qrzKnown.size() << "QSOs," << added << "read now, last log id"
                               << fetch->lastId;
    m_wkDirty = true;
    emit qsoLogChanged();
    emit qrzLogChanged();
}

}  // namespace AetherSDR::mobile
