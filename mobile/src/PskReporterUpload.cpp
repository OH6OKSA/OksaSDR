#include "PskReporterUpload.h"

#include <QDateTime>
#include <QFile>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr quint16 kPort = 4739;
// Tests: PSK Reporter's checking port, which stores nothing.
quint16 reportPort()
{
    return qEnvironmentVariableIsSet("AETHER_PSKREPORTER_TEST") ? 14739 : kPort;
}
constexpr int kSendIntervalMs = 5 * 60 * 1000;
constexpr qint64 kRepeatSecs = 5 * 60;
constexpr int kMaxPacket = 1400;
constexpr quint16 kReceiverTemplate = 0x9992;
constexpr quint16 kSenderTemplate = 0x9993;

void put16(QByteArray& b, quint16 v)
{
    b.append(char(v >> 8)).append(char(v & 0xff));
}
void put32(QByteArray& b, quint32 v)
{
    put16(b, quint16(v >> 16));
    put16(b, quint16(v & 0xffff));
}
// A PSK Reporter field (enterprise 30351): id with the enterprise bit,
// length (0xffff: variable), enterprise number.
void field(QByteArray& b, quint16 id, quint16 length)
{
    put16(b, 0x8000 | id);
    put16(b, length);
    put32(b, 30351);
}
void putString(QByteArray& b, const QString& s)
{
    const QByteArray u = s.toUtf8().left(254);
    b.append(char(u.size())).append(u);
}
void pad4(QByteArray& set)
{
    while (set.size() % 4) {
        set.append('\0');
    }
}
void setLength(QByteArray& set)
{
    const quint16 n = quint16(set.size());
    set[2] = char(n >> 8);
    set[3] = char(n & 0xff);
}

QByteArray templates()
{
    // Receiver (options template set 3): receiverCallsign 2, receiverLocator 4,
    // decodingSoftware 8.
    QByteArray rx;
    put16(rx, 3);
    put16(rx, 0);
    put16(rx, kReceiverTemplate);
    put16(rx, 3);  // fields
    put16(rx, 0);  // scope fields
    field(rx, 2, 0xffff);
    field(rx, 4, 0xffff);
    field(rx, 8, 0xffff);
    pad4(rx);
    setLength(rx);
    // Sender (template set 2): senderCallsign 1, frequency 5, sNR 6, mode 10,
    // senderLocator 3, informationSource 11, flowStartSeconds (IANA 150).
    QByteArray tx;
    put16(tx, 2);
    put16(tx, 0);
    put16(tx, kSenderTemplate);
    put16(tx, 7);
    field(tx, 1, 0xffff);
    field(tx, 5, 4);
    field(tx, 6, 1);
    field(tx, 10, 0xffff);
    field(tx, 3, 0xffff);
    field(tx, 11, 1);
    put16(tx, 150);
    put16(tx, 4);
    setLength(tx);
    return rx + tx;
}
}  // namespace

PskReporterUpload::PskReporterUpload(QObject* parent)
    : QObject(parent)
    , m_domainId(QRandomGenerator::global()->generate())
{
    m_socket = new QUdpSocket(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(qEnvironmentVariableIsSet("AETHER_PSKREPORTER_TEST") ? 60000 : kSendIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this] { send(false); });
    m_timer->start();
    QHostInfo::lookupHost(QStringLiteral("report.pskreporter.info"), this, [this](const QHostInfo& info) {
        for (const QHostAddress& a : info.addresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                m_server = a;
                break;
            }
        }
        if (m_server.isNull() && !info.addresses().isEmpty()) {
            m_server = info.addresses().first();
        }
        qCInfo(lcMobile) << "pskreporter: server" << m_server.toString();
    });
}

void PskReporterUpload::setReceiver(const QString& callsign, const QString& locator, const QString& software)
{
    m_callsign = callsign.trimmed().toUpper();
    m_locator = locator.trimmed().toUpper();
    m_software = software;
}

void PskReporterUpload::addSpot(const QString& callsign, const QString& locator, qint64 frequencyHz, int snr,
                                const QString& mode)
{
    const QString call = callsign.trimmed().toUpper();
    if (call.isEmpty() || call.startsWith(QLatin1Char('<')) || frequencyHz <= 0) {
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString key = call + QLatin1Char('|') + QString::number(frequencyHz / 1000000);
    if (const auto it = m_lastSeen.constFind(key); it != m_lastSeen.cend() && now - *it < kRepeatSecs) {
        return;
    }
    m_lastSeen.insert(key, now);
    m_spots.append({call, locator.trimmed().toUpper(), frequencyHz, snr, mode, now});
    emit changed();
}

QVector<QByteArray> PskReporterUpload::buildPackets(qint64 nowSecs, bool withTemplates)
{
    QVector<QByteArray> packets;
    QByteArray rxData;
    put16(rxData, kReceiverTemplate);
    put16(rxData, 0);
    putString(rxData, m_callsign);
    putString(rxData, m_locator);
    putString(rxData, m_software);
    pad4(rxData);
    setLength(rxData);
    const QByteArray tmpl = withTemplates ? templates() : QByteArray();

    int i = 0;
    while (i < m_spots.size()) {
        QByteArray txData;
        put16(txData, kSenderTemplate);
        put16(txData, 0);
        int records = 0;
        while (i < m_spots.size()) {
            const Spot& s = m_spots.at(i);
            QByteArray r;
            putString(r, s.callsign);
            put32(r, quint32(s.frequencyHz));
            r.append(char(qint8(qBound(-127, s.snr, 127))));
            putString(r, s.mode);
            putString(r, s.locator);
            r.append(char(1));  // informationSource: automatically extracted
            put32(r, quint32(s.timeSecs));
            if (16 + tmpl.size() + rxData.size() + txData.size() + r.size() + 3 > kMaxPacket && records > 0) {
                break;
            }
            txData.append(r);
            ++records;
            ++i;
        }
        pad4(txData);
        setLength(txData);
        QByteArray p;
        put16(p, 10);  // IPFIX
        put16(p, 0);   // length, below
        put32(p, quint32(nowSecs));
        put32(p, m_sequence);
        put32(p, m_domainId);
        p.append(tmpl).append(rxData).append(txData);
        const quint16 n = quint16(p.size());
        p[2] = char(n >> 8);
        p[3] = char(n & 0xff);
        packets.append(p);
        m_sequence += quint32(records);
    }
    return packets;
}

void PskReporterUpload::flush()
{
    send(true);
}

void PskReporterUpload::send(bool force)
{
    Q_UNUSED(force)
    if (m_spots.isEmpty() || m_callsign.isEmpty() || m_locator.size() < 4 || m_server.isNull()) {
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Templates in the first three packets, then once an hour.
    const bool withTemplates = m_packets < 3 || now - m_templatesAt > 3600;
    const QVector<QByteArray> packets = buildPackets(now, withTemplates);
    for (const QByteArray& p : packets) {
        m_socket->writeDatagram(p, m_server, reportPort());
        if (qEnvironmentVariableIsSet("AETHER_PSKREPORTER_DUMP")) {  // tests: the packets as sent
            QFile f(qEnvironmentVariable("AETHER_PSKREPORTER_DUMP") + QStringLiteral("-%1.bin").arg(m_packets));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(p);
            }
        }
        ++m_packets;
    }
    if (withTemplates) {
        m_templatesAt = now;
    }
    m_sent += m_spots.size();
    qCInfo(lcMobile).noquote() << QStringLiteral("pskreporter: %1 spots in %2 packet(s), %3 sent this session")
                                      .arg(m_spots.size())
                                      .arg(packets.size())
                                      .arg(m_sent);
    m_spots.clear();
    // Old entries out of the repeat filter.
    for (auto it = m_lastSeen.begin(); it != m_lastSeen.end();) {
        it = now - it.value() > kRepeatSecs ? m_lastSeen.erase(it) : std::next(it);
    }
    emit changed();
}

}  // namespace AetherSDR::mobile
