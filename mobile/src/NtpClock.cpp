#include "NtpClock.h"

#include <QDateTime>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtEndian>

#include <cmath>

namespace AetherSDR::mobile {

Q_LOGGING_CATEGORY(lcNtp, "aether.mobile.ntp")

namespace {

constexpr quint16 kNtpPort = 123;
constexpr int kRequests = 4;
constexpr int kRequestGapMs = 250;
constexpr int kDeadlineMs = 3000;
// Seconds from 1900 (NTP) to 1970 (Unix).
constexpr qint64 kNtpEpochOffsetS = 2208988800LL;
// A round trip longer than this says too little about the offset.
constexpr double kMaxDelayMs = 1500.0;

// Unix milliseconds as an NTP timestamp (seconds since 1900, 32-bit fraction).
void putTimestamp(uchar* at, qint64 unixMs)
{
    const quint32 seconds = quint32(unixMs / 1000 + kNtpEpochOffsetS);
    const quint32 fraction = quint32(double(unixMs % 1000) / 1000.0 * 4294967296.0);
    qToBigEndian(seconds, at);
    qToBigEndian(fraction, at + 4);
}

double timestampMs(const uchar* at)
{
    const double seconds = double(qFromBigEndian<quint32>(at)) - double(kNtpEpochOffsetS);
    const double fraction = double(qFromBigEndian<quint32>(at + 4)) / 4294967296.0;
    return (seconds + fraction) * 1000.0;
}

} // namespace

qint64 NtpClock::deviceMs()
{
    static const qint64 skew = qEnvironmentVariableIntValue("AETHER_CLOCK_SKEW_MS");
    return QDateTime::currentMSecsSinceEpoch() + skew;
}

NtpClock::NtpClock(QObject* parent)
    : QObject(parent)
{
    m_sendTimer.setInterval(kRequestGapMs);
    connect(&m_sendTimer, &QTimer::timeout, this, &NtpClock::sendRequest);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(kDeadlineMs);
    connect(&m_deadline, &QTimer::timeout, this, &NtpClock::finish);
}

void NtpClock::sync(const QString& host)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_host = host;
    m_sent = 0;
    m_haveBest = false;
    QHostInfo::lookupHost(host, this, [this](const QHostInfo& info) {
        m_server = QHostAddress();
        for (const QHostAddress& address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                m_server = address;
                break;
            }
        }
        if (m_server.isNull() && !info.addresses().isEmpty()) {
            m_server = info.addresses().first();
        }
        if (m_server.isNull()) {
            m_busy = false;
            emit failed(tr("%1 did not resolve: %2").arg(m_host, info.errorString()));
            return;
        }
        if (!m_socket) {
            m_socket = new QUdpSocket(this);
            connect(m_socket, &QUdpSocket::readyRead, this, &NtpClock::readAnswers);
        }
        if (m_socket->state() != QAbstractSocket::BoundState) {
            m_socket->bind(m_server.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6
                                                                                : QHostAddress::AnyIPv4, 0);
        }
        sendRequest();
        m_sendTimer.start();
        m_deadline.start();
    });
}

void NtpClock::sendRequest()
{
    if (m_sent >= kRequests) {
        m_sendTimer.stop();
        return;
    }
    // Client mode (3), version 4; our send time in the transmit field comes
    // back as the originate time, which ties each answer to its request.
    QByteArray packet(48, '\0');
    auto* bytes = reinterpret_cast<uchar*>(packet.data());
    bytes[0] = 0x23;
    putTimestamp(bytes + 40, deviceMs());
    m_socket->writeDatagram(packet, m_server, kNtpPort);
    ++m_sent;
}

void NtpClock::readAnswers()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const qint64 t4 = deviceMs();
        const QByteArray data = datagram.data();
        if (!m_busy || data.size() < 48) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
        const int leap = bytes[0] >> 6;
        const int mode = bytes[0] & 0x7;
        const int stratum = bytes[1];
        // A server's answer, synchronized, not a kiss-o'-death (stratum 0).
        if (mode != 4 || leap == 3 || stratum < 1 || stratum > 15) {
            continue;
        }
        const double t1 = timestampMs(bytes + 24);
        const double t2 = timestampMs(bytes + 32);
        const double t3 = timestampMs(bytes + 40);
        const double delay = (double(t4) - t1) - (t3 - t2);
        const double offset = ((t2 - t1) + (t3 - double(t4))) / 2.0;
        if (!std::isfinite(offset) || delay < 0.0 || delay > kMaxDelayMs) {
            continue;
        }
        if (!m_haveBest || delay < m_bestDelayMs) {
            m_haveBest = true;
            m_bestDelayMs = delay;
            m_bestOffsetMs = offset;
        }
    }
}

void NtpClock::finish()
{
    m_sendTimer.stop();
    if (!m_busy) {
        return;
    }
    m_busy = false;
    if (!m_haveBest) {
        emit failed(tr("no answer from %1 (%2)").arg(m_host, m_server.toString()));
        return;
    }
    qCInfo(lcNtp).noquote() << QStringLiteral("%1 (%2): device clock %3 ms off, round trip %4 ms")
                                   .arg(m_host, m_server.toString())
                                   .arg(-m_bestOffsetMs, 0, 'f', 0)
                                   .arg(m_bestDelayMs, 0, 'f', 0);
    emit synced(m_bestOffsetMs, m_bestDelayMs, m_host);
}

} // namespace AetherSDR::mobile
