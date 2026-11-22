#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

namespace AetherSDR::mobile {

// A subscribe-only MQTT 3.1.1 client over plain TCP (roadmap 136): PSK
// Reporter's live feed (mqtt.pskreporter.info:1883, public, no account).
// The engine's MqttClient needs libmosquitto, which the iOS and Android
// builds do not have; a reader of QoS 0 messages needs only CONNECT,
// SUBSCRIBE / UNSUBSCRIBE, PINGREQ and the PUBLISH it is sent. Connects
// again by itself (15 s, doubling to 5 min) until stop().
class MiniMqtt : public QObject {
    Q_OBJECT

public:
    explicit MiniMqtt(QObject* parent = nullptr);

    // Connects and subscribes to the topics (QoS 0); a running connection
    // with other topics subscribes again.
    void start(const QString& host, quint16 port, const QStringList& topics);
    void stop();
    bool running() const { return m_wanted; }
    // "off", "connecting", "connected", "retrying".
    QString state() const { return m_state; }
    QString lastError() const { return m_error; }
    quint64 messages() const { return m_messages; }

signals:
    void message(const QString& topic, const QByteArray& payload);
    void stateChanged();

private:
    void setState(const QString& state, const QString& error = QString());
    void onConnected();
    void onReadyRead();
    void onDisconnected(const QString& why);
    void sendSubscribe(const QStringList& topics);
    void sendUnsubscribe(const QStringList& topics);
    void scheduleRetry();
    void writePacket(quint8 header, const QByteArray& body);
    static QByteArray utf8Field(const QString& text);

    QTcpSocket m_socket;
    QByteArray m_buffer;
    QTimer m_ping;
    QTimer m_retry;
    QTimer m_answer;   // CONNACK / PINGRESP not answered in time
    QString m_host;
    quint16 m_port{0};
    QStringList m_topics;
    QString m_clientId;
    QString m_state{QStringLiteral("off")};
    QString m_error;
    int m_backoffMs{15000};
    quint16 m_packetId{0};
    quint64 m_messages{0};
    bool m_wanted{false};
    bool m_session{false};   // CONNACK received
};

} // namespace AetherSDR::mobile
