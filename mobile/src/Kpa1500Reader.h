#pragma once

#include "core/Kpa1500Protocol.h"

#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>

namespace AetherSDR::mobile {

// An Elecraft KPA1500 read over its UDP server (roadmap 61, Rich Yost N2RY).
//
// The amplifier takes "a single TCP client and any number of UDP clients"
// (KPA1500 Programming Reference V3, p.6), so reading over UDP leaves the
// one TCP connection to Elecraft's own software or a logger. Over UDP the
// reference says: one command a packet, at most one answer; the amplifier
// never sends by itself. So each query goes in its own datagram, a few
// tens of milliseconds apart (no flow control, a small input buffer), and
// the answers, which echo their command, are matched by name whatever
// order they come in. A lost answer leaves that reading one cycle old.
//
// Read only by construction: this class builds nothing but bare queries
// (Kpa1500::buildQuery); there is no way through it to change the
// amplifier. Control goes over the amplifier's TCP connection
// (Kpa1500Connection, MobileEngineKpa.cpp), only with Control on.
class Kpa1500Reader : public QObject {
    Q_OBJECT

public:
    explicit Kpa1500Reader(QObject* parent = nullptr);

    // Reads host:port until stop(); a name is looked up first.
    void start(const QString& host, quint16 port);
    void stop();
    // A cycle every 250 ms (the radio transmitting) or 500 ms.
    void setFast(bool fast);
    // Asked next, ahead of the cycle (a command's readback, e.g. "OS").
    void queryNow(const QString& cmd);
    // Asked every cycle as well (while a tune runs: "TP", "FL").
    void setExtraQueries(const QStringList& cmds) { m_extra = cmds; }

    bool answering() const { return m_answering; }
    QString description() const;
    const Kpa1500::Status& status() const { return m_status; }

signals:
    // The amplifier answers again after silence (or for the first time).
    void answered();
    // No answer for kSilenceMs, or the name was not found; reading goes on.
    void silent(const QString& why);
    void statusUpdated();

private:
    void cycle();
    void sendNext();
    void onDatagrams();
    void checkSilence();
    void goSilent(const QString& why);

    QUdpSocket m_socket;
    QHostAddress m_address;
    QString m_host;
    quint16 m_port{0};
    int m_lookupId{-1};
    Kpa1500::MessageParser m_parser;
    Kpa1500::Status m_status;
    QTimer m_cycleTimer;
    QTimer m_paceTimer;
    QStringList m_queue;
    QStringList m_extra;
    int m_slowIndex{0};
    qint64 m_startMs{0};
    qint64 m_lastAnswerMs{0};
    bool m_answering{false};
    bool m_silentSaid{false};

    static constexpr int kCycleMs = 500;
    static constexpr int kFastCycleMs = 250;
    static constexpr int kPaceMs = 30;
    static constexpr int kSilenceMs = 3000;
};

}  // namespace AetherSDR::mobile
