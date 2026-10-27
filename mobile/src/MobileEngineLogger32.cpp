// Logger32: every QSO the app logs goes to Logger32 on the operator's PC as
// well (Juho Kontio: phone QSOs into Logger32 without passing ADIF files
// around). Logger32 takes raw ADIF records over TCP (port 52001 by default),
// one record a connection, as other programs send them. Unsent records wait
// in a queue kept in the settings (the PC off, the phone away from home or
// its VPN down) and go with the next QSO, the next start, a Save, or every
// two minutes while some wait.

#include "MobileEngine.h"

#include "QsoLog.h"

#include "core/AppSettings.h"

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kHostKey = QStringLiteral("MobileLogger32Host");
const QString kPendingKey = QStringLiteral("MobileLogger32Pending");
constexpr quint16 kDefaultPort = 52001;
constexpr int kMaxPending = 500;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kRetryMs = 120000;
}  // namespace

void MobileEngine::setupLogger32()
{
    const QString pending = AppSettings::instance().value(kPendingKey).toString();
    if (!pending.isEmpty()) {
        m_logger32Pending = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    m_logger32Retry.setInterval(kRetryMs);
    connect(&m_logger32Retry, &QTimer::timeout, this, &MobileEngine::logger32Flush);
    if (!logger32Host().isEmpty() && !m_logger32Pending.isEmpty()) {
        QTimer::singleShot(5000, this, &MobileEngine::logger32Flush);
    }
}

// "host" or "host:port" from the setting; the port 52001 when not given.
QString MobileEngine::logger32Host() const
{
    return AppSettings::instance().value(kHostKey).toString().trimmed();
}

static bool splitHostPort(const QString& spec, QString* host, quint16* port)
{
    *host = spec;
    *port = kDefaultPort;
    const qsizetype colon = spec.lastIndexOf(QLatin1Char(':'));
    // One colon: host:port (an IPv6 address has several and no port here).
    if (colon > 0 && spec.indexOf(QLatin1Char(':')) == colon) {
        bool ok = false;
        const uint p = spec.mid(colon + 1).toUInt(&ok);
        if (!ok || p == 0 || p > 65535) {
            return false;
        }
        *host = spec.left(colon);
        *port = quint16(p);
    }
    return !host->isEmpty();
}

QVariantMap MobileEngine::logger32() const
{
    return {{QStringLiteral("host"), logger32Host()},
            {QStringLiteral("enabled"), !logger32Host().isEmpty()},
            {QStringLiteral("pending"), m_logger32Pending.size()},
            {QStringLiteral("status"), m_logger32Status},
            {QStringLiteral("statusKind"), m_logger32StatusKind}};
}

void MobileEngine::setLogger32(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitHostPort(spec, &host, &port)) {
        m_logger32StatusKind = QStringLiteral("failed");
        m_logger32Status = tr("Give the PC's address, e.g. 192.168.1.10 or 192.168.1.10:52001.");
        emit logger32Changed();
        return;
    }
    AppSettings::instance().setValue(kHostKey, spec);
    if (spec.isEmpty()) {
        // Off: the unsent queue goes too (it would reach whatever PC is set next).
        m_logger32Pending.clear();
        AppSettings::instance().setValue(kPendingKey, QString());
        m_logger32Retry.stop();
        m_logger32Status.clear();
        m_logger32StatusKind.clear();
    }
    AppSettings::instance().save();
    if (!spec.isEmpty()) {
        logger32Check();
    }
    emit logger32Changed();
}

// Save: whether Logger32 answers there (a connection, nothing sent), then
// the queue.
void MobileEngine::logger32Check()
{
    QString host;
    quint16 port = 0;
    if (!splitHostPort(logger32Host(), &host, &port) || m_logger32Socket) {
        return;
    }
    m_logger32StatusKind = QStringLiteral("checking");
    m_logger32Status = tr("Connecting to Logger32…");
    emit logger32Changed();
    auto* socket = new QTcpSocket(this);
    m_logger32Socket = socket;
    auto* timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    const auto done = [this, socket](bool ok, const QString& why) {
        m_logger32Socket = nullptr;
        socket->deleteLater();
        m_logger32StatusKind = ok ? QStringLiteral("ok") : QStringLiteral("failed");
        m_logger32Status = ok ? tr("Logger32 answers: each QSO goes there too.")
                              : tr("Logger32 not reached (%1).").arg(why);
        qCInfo(lcMobile).noquote() << "Logger32:" << m_logger32Status;
        emit logger32Changed();
        if (ok) {
            logger32Flush();
        }
    };
    connect(socket, &QTcpSocket::connected, this, [socket, timeout, done] {
        timeout->stop();
        socket->disconnectFromHost();
        done(true, QString());
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, done](QAbstractSocket::SocketError) {
        if (m_logger32Socket == socket) {
            done(false, socket->errorString());
        }
    });
    connect(timeout, &QTimer::timeout, this, [this, socket, done] {
        if (m_logger32Socket == socket) {
            socket->abort();
            done(false, tr("no answer in 5 s"));
        }
    });
    timeout->start(kConnectTimeoutMs);
    socket->connectToHost(host, port);
}

void MobileEngine::logger32Queue(const QString& adif)
{
    if (logger32Host().isEmpty()) {
        return;
    }
    m_logger32Pending.append(adif);
    while (m_logger32Pending.size() > kMaxPending) {
        m_logger32Pending.removeFirst();
    }
    AppSettings::instance().setValue(kPendingKey, m_logger32Pending.join(QLatin1Char('\n')));
    AppSettings::instance().save();
    logger32Flush();
}

// The oldest waiting record on a connection of its own; on success the
// next, on failure all wait for the next try.
void MobileEngine::logger32Flush()
{
    QString host;
    quint16 port = 0;
    if (m_logger32Socket || m_logger32Pending.isEmpty() || !splitHostPort(logger32Host(), &host, &port)) {
        if (m_logger32Pending.isEmpty()) {
            m_logger32Retry.stop();
        }
        emit logger32Changed();
        return;
    }
    const QString adif = m_logger32Pending.first();
    const QByteArray bytes = adif.toUtf8() + "\r\n";
    auto* socket = new QTcpSocket(this);
    m_logger32Socket = socket;
    auto* timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    const auto failed = [this, socket](const QString& why) {
        if (m_logger32Socket != socket) {
            return;
        }
        m_logger32Socket = nullptr;
        socket->abort();
        socket->deleteLater();
        m_logger32StatusKind = QStringLiteral("failed");
        m_logger32Status = tr("Logger32 not reached (%1): %2 QSO(s) wait").arg(why).arg(m_logger32Pending.size());
        qCInfo(lcMobile).noquote() << "Logger32:" << m_logger32Status;
        m_logger32Retry.start();
        emit logger32Changed();
    };
    connect(socket, &QTcpSocket::connected, this, [socket, timeout, bytes] {
        timeout->stop();
        socket->write(bytes);
        // Closed once written: disconnected follows the last byte out.
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket, adif] {
        if (m_logger32Socket != socket) {
            return;
        }
        m_logger32Socket = nullptr;
        socket->deleteLater();
        m_logger32Pending.removeOne(adif);
        AppSettings::instance().setValue(kPendingKey, m_logger32Pending.join(QLatin1Char('\n')));
        AppSettings::instance().save();
        m_logger32StatusKind = QStringLiteral("ok");
        m_logger32Status = m_logger32Pending.isEmpty() ? tr("QSO sent to Logger32.")
                                                       : tr("Sending to Logger32: %1 left").arg(m_logger32Pending.size());
        qCInfo(lcMobile).noquote() << "Logger32: sent," << m_logger32Pending.size() << "left";
        logger32Flush();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [socket, failed](QAbstractSocket::SocketError error) {
        // The PC closing first after taking the record is no failure.
        if (error != QAbstractSocket::RemoteHostClosedError) {
            failed(socket->errorString());
        }
    });
    connect(timeout, &QTimer::timeout, this, [failed] { failed(tr("no answer in 5 s")); });
    timeout->start(kConnectTimeoutMs);
    socket->connectToHost(host, port);
}

}  // namespace AetherSDR::mobile
