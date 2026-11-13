#include "Kpa1500Reader.h"

#include <QDateTime>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QNetworkDatagram>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {

// Every cycle: the main supplies (a sleeping amplifier answers nothing
// else, so this keeps it heard), and what moves while the radio transmits
// (forward, reflected, SWR, the drive, the PA's supply and current).
const QStringList& fastQueries()
{
    static const QStringList kFast{QStringLiteral("ON"), QStringLiteral("PWF"), QStringLiteral("PWR"),
                                   QStringLiteral("SW"), QStringLiteral("PWI"), QStringLiteral("VI")};
    return kFast;
}

// One a cycle, in turn: the rest, which changes only now and then (a full
// round in 5 s at the receive rate).
const QStringList& slowQueries()
{
    static const QStringList kSlow{QStringLiteral("TM"), QStringLiteral("OS"), QStringLiteral("FL"),
                                   QStringLiteral("BN"), QStringLiteral("AN"), QStringLiteral("AM"),
                                   QStringLiteral("AI"), QStringLiteral("TP"), QStringLiteral("RV")};
    return kSlow;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

}  // namespace

Kpa1500Reader::Kpa1500Reader(QObject* parent)
    : QObject(parent)
{
    m_parser.setMessageCallback([this](const Kpa1500::Message& m) {
        if (Kpa1500::applyMessage(m, m_status)) {
            emit statusUpdated();
        }
    });
    connect(&m_socket, &QUdpSocket::readyRead, this, &Kpa1500Reader::onDatagrams);
    m_cycleTimer.setInterval(kCycleMs);
    connect(&m_cycleTimer, &QTimer::timeout, this, &Kpa1500Reader::cycle);
    m_paceTimer.setInterval(kPaceMs);
    connect(&m_paceTimer, &QTimer::timeout, this, &Kpa1500Reader::sendNext);
}

QString Kpa1500Reader::description() const
{
    return m_host.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(m_host).arg(m_port);
}

void Kpa1500Reader::start(const QString& host, quint16 port)
{
    stop();
    m_host = host;
    m_port = port;
    m_startMs = nowMs();
    if (m_socket.state() != QAbstractSocket::BoundState && !m_socket.bind(QHostAddress::Any, 0)) {
        goSilent(m_socket.errorString());
    }
    const QHostAddress literal(host);
    if (!literal.isNull()) {
        m_address = literal;
    } else {
        m_lookupId = QHostInfo::lookupHost(host, this, [this, host](const QHostInfo& info) {
            if (info.lookupId() != m_lookupId) {
                return;  // an older start's lookup
            }
            m_lookupId = -1;
            QHostAddress found;
            for (const QHostAddress& a : info.addresses()) {
                if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                    found = a;
                    break;
                }
            }
            if (found.isNull() && !info.addresses().isEmpty()) {
                found = info.addresses().constFirst();
            }
            if (found.isNull()) {
                goSilent(tr("%1 not found").arg(host));
                return;
            }
            m_address = found;
            cycle();
        });
    }
    m_cycleTimer.start();
    cycle();
}

void Kpa1500Reader::stop()
{
    if (m_lookupId >= 0) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    m_cycleTimer.stop();
    m_paceTimer.stop();
    m_queue.clear();
    m_address.clear();
    m_parser.reset();
    m_status = {};
    m_answering = false;
    m_silentSaid = false;
    m_lastAnswerMs = 0;
    m_slowIndex = 0;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.close();
    }
}

void Kpa1500Reader::setFast(bool fast)
{
    m_cycleTimer.setInterval(fast ? kFastCycleMs : kCycleMs);
}

void Kpa1500Reader::cycle()
{
    checkSilence();
    // A name not found: looked up again every 5 s, as an amplifier off or a
    // VPN down is tried again.
    if (m_address.isNull() && m_lookupId < 0 && nowMs() - m_startMs >= 5000) {
        start(m_host, m_port);
        return;
    }
    // A name still being looked up, or the last cycle not sent out yet (a
    // slow link): no pile-up of queries.
    if (m_address.isNull() || !m_queue.isEmpty()) {
        return;
    }
    // Supplies off: only ^ON (and ^RV) answer (the reference: available
    // while "sleeping"), so only ^ON is asked until they are on.
    if (m_status.powerOn && !*m_status.powerOn) {
        m_queue = {QStringLiteral("ON")};
        if (!m_status.firmware) {
            m_queue << QStringLiteral("RV");
        }
        sendNext();
        m_paceTimer.start();
        return;
    }
    // Extra queries first: a tune's ^TP is read at the start of every cycle.
    m_queue = m_extra + fastQueries();
    const QStringList& slow = slowQueries();
    m_queue << slow.at(m_slowIndex % slow.size());
    m_slowIndex = (m_slowIndex + 1) % slow.size();
    sendNext();
    m_paceTimer.start();
}

void Kpa1500Reader::queryNow(const QString& cmd)
{
    if (m_address.isNull()) {
        return;
    }
    m_queue.removeAll(cmd);
    m_queue.prepend(cmd);
    if (!m_paceTimer.isActive()) {
        sendNext();
        m_paceTimer.start();
    }
}

void Kpa1500Reader::sendNext()
{
    if (m_queue.isEmpty() || m_address.isNull()) {
        m_paceTimer.stop();
        return;
    }
    const QByteArray query = Kpa1500::buildQuery(m_queue.takeFirst());
    if (!query.isEmpty()) {
        m_socket.writeDatagram(query, m_address, m_port);
    }
    if (m_queue.isEmpty()) {
        m_paceTimer.stop();
    }
}

void Kpa1500Reader::onDatagrams()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        // Only the amplifier's own answers count.
        if (m_address.isNull() || datagram.senderPort() != m_port
            || !datagram.senderAddress().isEqual(m_address, QHostAddress::ConvertV4MappedToIPv4)) {
            continue;
        }
        m_lastAnswerMs = nowMs();
        if (!m_answering) {
            m_answering = true;
            m_silentSaid = false;
            qCInfo(lcMobile).noquote() << "amp: the KPA1500 at" << description() << "answers (UDP, read only)";
            emit answered();
        }
        // One answer a datagram: nothing carries over to the next.
        m_parser.reset();
        m_parser.feed(datagram.data());
    }
}

void Kpa1500Reader::checkSilence()
{
    const qint64 since = m_lastAnswerMs > 0 ? m_lastAnswerMs : m_startMs;
    if (m_address.isNull() || nowMs() - since < kSilenceMs) {
        return;
    }
    goSilent(m_answering ? tr("no answer for %1 s").arg(kSilenceMs / 1000) : tr("no answer"));
}

void Kpa1500Reader::goSilent(const QString& why)
{
    const bool wasAnswering = m_answering;
    m_answering = false;
    if (wasAnswering) {
        // Old readings are not shown as if they were live.
        m_status = {};
        emit statusUpdated();
    }
    if (m_silentSaid && !wasAnswering) {
        return;
    }
    m_silentSaid = true;
    qCInfo(lcMobile).noquote() << "amp: the KPA1500 at" << description() << "-" << why;
    emit silent(why);
}

}  // namespace AetherSDR::mobile
