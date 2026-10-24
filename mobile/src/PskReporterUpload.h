#pragma once

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;
class QUdpSocket;

namespace AetherSDR::mobile {

// Reports the stations this app decodes (FT8 / FT4) to PSK Reporter, as
// WSJT-X does: IPFIX over UDP to report.pskreporter.info:4739 with the
// receiver template (our call, locator, software) and the sender template
// (call, frequency, SNR, mode, information source, time, locator). Each call
// goes at most once per band every 5 minutes, packets at most every 5
// minutes and under 1400 bytes, the templates in the first packets and then
// once an hour (https://pskreporter.info/pskdev.html).
class PskReporterUpload : public QObject {
    Q_OBJECT
public:
    explicit PskReporterUpload(QObject* parent = nullptr);

    void setReceiver(const QString& callsign, const QString& locator, const QString& software);
    // A decoded station; repeats within 5 minutes on the same band are
    // dropped.
    void addSpot(const QString& callsign, const QString& locator, qint64 frequencyHz, int snr, const QString& mode);
    // Sends what is queued now (decoding stopped, the app closing).
    void flush();
    int spotsSent() const { return m_sent; }
    int spotsQueued() const { return m_spots.size(); }

    // The packets, built without a socket (tests).
    QVector<QByteArray> buildPackets(qint64 nowSecs, bool withTemplates);

signals:
    void changed();

private:
    struct Spot {
        QString callsign;
        QString locator;
        qint64 frequencyHz{0};
        int snr{0};
        QString mode;
        qint64 timeSecs{0};
    };
    void send(bool force);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_timer{nullptr};
    QHostAddress m_server;
    QString m_callsign;
    QString m_locator;
    QString m_software;
    QVector<Spot> m_spots;
    QHash<QString, qint64> m_lastSeen;  // call|band -> seconds
    quint32 m_domainId{0};
    quint32 m_sequence{0};
    int m_packets{0};
    qint64 m_templatesAt{0};
    int m_sent{0};
};

}  // namespace AetherSDR::mobile
