#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>

class QUdpSocket;

namespace AetherSDR::mobile {

// How far the device clock is from a time server (SNTP, RFC 4330): four
// requests a quarter second apart, the answer with the shortest round trip
// counts. FT8 / FT4 run on the corrected time (Ft8Engine::nowMs), as an app
// cannot set Android's or iOS's clock (Kurt DL5EAG: his tablet drifted and
// an NTP app could not fix it).
class NtpClock : public QObject {
    Q_OBJECT

public:
    explicit NtpClock(QObject* parent = nullptr);

    // Starts a measurement unless one is running; synced or failed follows.
    void sync(const QString& host = QStringLiteral("pool.ntp.org"));
    bool busy() const { return m_busy; }
    // The device clock in Unix ms. AETHER_CLOCK_SKEW_MS (development) puts it
    // off by that much, to check the correction without touching the system.
    static qint64 deviceMs();

signals:
    // offsetMs: add it to the device clock for the server's time.
    void synced(double offsetMs, double delayMs, const QString& server);
    void failed(const QString& reason);

private:
    void sendRequest();
    void readAnswers();
    void finish();

    QUdpSocket* m_socket{nullptr};
    QTimer m_sendTimer;
    QTimer m_deadline;
    QHostAddress m_server;
    QString m_host;
    int m_sent{0};
    bool m_busy{false};
    bool m_haveBest{false};
    double m_bestOffsetMs{0.0};
    double m_bestDelayMs{0.0};
};

} // namespace AetherSDR::mobile
