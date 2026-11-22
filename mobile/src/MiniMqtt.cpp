#include "MiniMqtt.h"

#include <QLoggingCategory>
#include <QRandomGenerator>

Q_LOGGING_CATEGORY(lcMiniMqtt, "aether.mobile.mqtt")

namespace AetherSDR::mobile {

namespace {
constexpr int kKeepAliveSec = 60;
constexpr int kPingMs = 45 * 1000;
constexpr int kAnswerMs = 20 * 1000;
constexpr int kMaxBackoffMs = 5 * 60 * 1000;
// mqtt.pskreporter.info leaves a connect unanswered when one address makes
// several within seconds (2026-11-22: the third of three 3 s apart), so the
// first retry waits this long and a change of topics never reconnects.
constexpr int kFirstRetryMs = 15 * 1000;
// A message bigger than this is not PSK Reporter's (a report is ~300 bytes):
// the stream is out of step, so it starts over.
constexpr int kMaxPacket = 256 * 1024;

QByteArray remainingLength(int length)
{
    QByteArray out;
    do {
        quint8 byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        out.append(char(byte));
    } while (length > 0);
    return out;
}
} // namespace

MiniMqtt::MiniMqtt(QObject* parent)
    : QObject(parent)
{
    m_ping.setInterval(kPingMs);
    connect(&m_ping, &QTimer::timeout, this, [this] {
        writePacket(0xC0, {});   // PINGREQ
        if (!m_answer.isActive()) {
            m_answer.start(kAnswerMs);
        }
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_wanted) {
            setState(QStringLiteral("connecting"));
            m_socket.connectToHost(m_host, m_port);
        }
    });
    m_answer.setSingleShot(true);
    connect(&m_answer, &QTimer::timeout, this, [this] {
        qCInfo(lcMiniMqtt).noquote() << "MQTT: no answer from" << m_host << "in" << kAnswerMs / 1000 << "s";
        m_socket.abort();
        onDisconnected(QStringLiteral("no answer"));
    });
    connect(&m_socket, &QTcpSocket::connected, this, &MiniMqtt::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &MiniMqtt::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] { onDisconnected(QStringLiteral("closed")); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onDisconnected(m_socket.errorString());
    });
}

void MiniMqtt::start(const QString& host, quint16 port, const QStringList& topics)
{
    const bool sameServer = m_wanted && host == m_host && port == m_port;
    const QStringList before = m_topics;
    m_host = host;
    m_port = port;
    m_topics = topics;
    m_wanted = true;
    if (sameServer && (m_socket.state() != QAbstractSocket::UnconnectedState || m_retry.isActive())) {
        // Other topics on the running session: subscribed and unsubscribed
        // in place (a reconnect would be refused for a while).
        if (m_session && topics != before) {
            QStringList added;
            QStringList removed;
            for (const QString& t : topics) {
                if (!before.contains(t)) added << t;
            }
            for (const QString& t : before) {
                if (!topics.contains(t)) removed << t;
            }
            sendSubscribe(added);
            sendUnsubscribe(removed);
            qCInfo(lcMiniMqtt).noquote() << "MQTT: topics now" << topics.join(QStringLiteral(", "));
        }
        return;
    }
    m_retry.stop();
    m_backoffMs = kFirstRetryMs;
    m_socket.abort();
    m_buffer.clear();
    setState(QStringLiteral("connecting"));
    qCInfo(lcMiniMqtt).noquote() << "MQTT: connecting to" << host << port << "for" << topics.join(QStringLiteral(", "));
    m_socket.connectToHost(host, port);
}

void MiniMqtt::stop()
{
    if (!m_wanted) {
        return;
    }
    m_wanted = false;
    m_retry.stop();
    m_ping.stop();
    m_answer.stop();
    if (m_session) {
        writePacket(0xE0, {});   // DISCONNECT
        m_socket.flush();
    }
    m_session = false;
    m_socket.abort();
    m_buffer.clear();
    setState(QStringLiteral("off"));
    qCInfo(lcMiniMqtt).noquote() << "MQTT: stopped after" << m_messages << "messages";
}

void MiniMqtt::setState(const QString& state, const QString& error)
{
    if (state == m_state && error == m_error) {
        return;
    }
    m_state = state;
    m_error = error;
    emit stateChanged();
}

QByteArray MiniMqtt::utf8Field(const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    QByteArray out;
    out.append(char((utf8.size() >> 8) & 0xff));
    out.append(char(utf8.size() & 0xff));
    out.append(utf8);
    return out;
}

void MiniMqtt::writePacket(quint8 header, const QByteArray& body)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    QByteArray packet;
    packet.append(char(header));
    packet.append(remainingLength(int(body.size())));
    packet.append(body);
    m_socket.write(packet);
}

void MiniMqtt::onConnected()
{
    // A new client id at each connect: the broker keeps a closed one's
    // session a while.
    m_clientId = QStringLiteral("OksaSDR-%1").arg(QRandomGenerator::global()->generate() & 0xffffff, 6, 16, QLatin1Char('0'));
    // CONNECT: "MQTT", level 4 (3.1.1), a clean session, the keep-alive,
    // the client id; no user name, no will.
    QByteArray body = utf8Field(QStringLiteral("MQTT"));
    body.append(char(4));
    body.append(char(0x02));
    body.append(char((kKeepAliveSec >> 8) & 0xff));
    body.append(char(kKeepAliveSec & 0xff));
    body.append(utf8Field(m_clientId));
    writePacket(0x10, body);
    m_answer.start(kAnswerMs);
}

void MiniMqtt::sendSubscribe(const QStringList& topics)
{
    if (topics.isEmpty()) {
        return;
    }
    QByteArray body;
    m_packetId = quint16(m_packetId % 65535 + 1);
    body.append(char((m_packetId >> 8) & 0xff));
    body.append(char(m_packetId & 0xff));
    for (const QString& topic : topics) {
        body.append(utf8Field(topic));
        body.append(char(0));   // QoS 0
    }
    writePacket(0x82, body);
}

void MiniMqtt::sendUnsubscribe(const QStringList& topics)
{
    if (topics.isEmpty()) {
        return;
    }
    QByteArray body;
    m_packetId = quint16(m_packetId % 65535 + 1);
    body.append(char((m_packetId >> 8) & 0xff));
    body.append(char(m_packetId & 0xff));
    for (const QString& topic : topics) {
        body.append(utf8Field(topic));
    }
    writePacket(0xA2, body);
}

void MiniMqtt::onReadyRead()
{
    m_buffer.append(m_socket.readAll());
    while (m_buffer.size() >= 2) {
        // The fixed header: type, then 1-4 bytes of remaining length.
        int length = 0;
        int multiplier = 1;
        int pos = 1;
        bool complete = false;
        while (pos < m_buffer.size() && pos <= 4) {
            const quint8 byte = quint8(m_buffer.at(pos));
            length += (byte & 0x7f) * multiplier;
            multiplier *= 128;
            ++pos;
            if ((byte & 0x80) == 0) {
                complete = true;
                break;
            }
        }
        if (!complete) {
            if (pos > 4) {
                m_socket.abort();
                onDisconnected(QStringLiteral("a malformed packet"));
            }
            return;
        }
        if (length > kMaxPacket) {
            m_socket.abort();
            onDisconnected(QStringLiteral("a packet of %1 bytes").arg(length));
            return;
        }
        if (m_buffer.size() < pos + length) {
            return;
        }
        const quint8 type = quint8(m_buffer.at(0)) >> 4;
        const quint8 flags = quint8(m_buffer.at(0)) & 0x0f;
        const QByteArray body = m_buffer.mid(pos, length);
        m_buffer.remove(0, pos + length);
        switch (type) {
        case 2: {   // CONNACK
            m_answer.stop();
            const int code = body.size() >= 2 ? quint8(body.at(1)) : -1;
            if (code != 0) {
                qCInfo(lcMiniMqtt).noquote() << "MQTT: connection refused, code" << code;
                m_socket.abort();
                onDisconnected(QStringLiteral("refused (code %1)").arg(code));
                return;
            }
            m_session = true;
            m_backoffMs = kFirstRetryMs;
            m_ping.start();
            sendSubscribe(m_topics);
            setState(QStringLiteral("connected"));
            qCInfo(lcMiniMqtt).noquote() << "MQTT: connected to" << m_host;
            break;
        }
        case 3: {   // PUBLISH
            if (body.size() < 2) {
                break;
            }
            const int topicLength = (quint8(body.at(0)) << 8) | quint8(body.at(1));
            if (body.size() < 2 + topicLength) {
                break;
            }
            const QString topic = QString::fromUtf8(body.mid(2, topicLength));
            int payloadAt = 2 + topicLength;
            if (((flags >> 1) & 0x03) > 0) {
                payloadAt += 2;   // a packet id (QoS 1 / 2; not asked for)
            }
            ++m_messages;
            emit message(topic, body.mid(payloadAt));
            break;
        }
        case 9:   // SUBACK
            qCInfo(lcMiniMqtt).noquote() << "MQTT: subscribed to" << m_topics.size() << "topic(s)";
            break;
        case 13:   // PINGRESP
            m_answer.stop();
            break;
        default:
            break;
        }
    }
}

void MiniMqtt::onDisconnected(const QString& why)
{
    m_ping.stop();
    m_answer.stop();
    m_session = false;
    m_buffer.clear();
    if (!m_wanted) {
        return;
    }
    if (m_retry.isActive()) {
        return;
    }
    qCInfo(lcMiniMqtt).noquote() << "MQTT:" << m_host << why << "- again in" << m_backoffMs / 1000 << "s";
    setState(QStringLiteral("retrying"), why);
    scheduleRetry();
}

void MiniMqtt::scheduleRetry()
{
    m_retry.start(m_backoffMs);
    m_backoffMs = std::min(kMaxBackoffMs, m_backoffMs * 2);
}

} // namespace AetherSDR::mobile
