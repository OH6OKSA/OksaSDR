// The connection doctor: what blocks a connection to a radio, in plain words.
//
// Juho Kontio's refused SmartLink connect, Ben Morris's devices that answer at
// home but not on the mobile network, a firewall that dropped the radio's
// connection to a device, the iPad's VPN that was off: each time the app only
// said "timed out" or nothing, and the reason took a long talk to find. The
// doctor goes through the way to the radio one step at a time and says which
// step fails and what to do about it:
//   1. this device's network (an address at all, and which networks);
//   2. a host name looked up;
//   3. where the radio is from here (this network, another private network,
//      a Tailscale address, the internet);
//   4. the radio's own port (TCP 4992): answered, refused, or no answer, and
//      on an answer what the radio says: its version, the stations on it and
//      whether it takes more than one (multiFLEX);
//   or, for SmartLink: signed in, the radio online, its station slots.
// Nothing is changed on the radio: the check connects as a plain client,
// reads, and closes without registering a station.

#include "MobileEngine.h"

#include "core/backends/flex/SmartLinkClient.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {

// iOS asks for the Local Network permission; without it a connection into the
// home network fails as "unreachable" or a timeout. Compiled everywhere.
#if defined(Q_OS_IOS)
constexpr bool kLocalNetworkPermission = true;
#else
constexpr bool kLocalNetworkPermission = false;
#endif

constexpr int kRadioPort = 4992;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kListenMs = 2500;

bool isTailscale(const QHostAddress& a)
{
    // Tailscale's addresses: 100.64.0.0/10 (CGNAT space).
    return a.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10);
}

// This device's IPv4 networks (address and prefix), loopback left out.
QList<QPair<QHostAddress, int>> localNetworks()
{
    QList<QPair<QHostAddress, int>> nets;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLoopback()) {
                nets.append({e.ip(), e.prefixLength() > 0 ? e.prefixLength() : 24});
            }
        }
    }
    return nets;
}

}  // namespace

void MobileEngine::checkStep(const QString& title, const QString& state, const QString& detail)
{
    // A step that was running is replaced by its outcome.
    if (!m_check.isEmpty()) {
        QVariantMap last = m_check.constLast().toMap();
        if (last.value(QStringLiteral("state")) == QLatin1String("running")
            && last.value(QStringLiteral("title")) == title) {
            m_check.removeLast();
        }
    }
    m_check.append(QVariantMap{{QStringLiteral("title"), title},
                               {QStringLiteral("state"), state},
                               {QStringLiteral("detail"), detail}});
    if (state != QLatin1String("running")) {
        qCInfo(lcMobile).noquote() << "connection check:" << title << state << detail;
    }
    emit connectionCheckChanged();
}

void MobileEngine::checkFinish()
{
    m_checkRunning = false;
    emit connectionCheckChanged();
}

void MobileEngine::checkConnection(const QString& target)
{
    const QString t = target.trimmed().isEmpty() ? m_lastTarget : target.trimmed();
    ++m_checkRun;
    m_check.clear();
    m_checkTarget = t;
    m_checkRunning = true;
    emit connectionCheckChanged();
    qCInfo(lcMobile).noquote() << "connection check for" << (t.isEmpty() ? QStringLiteral("(no radio tried)") : t);

    // 1. This device's network.
    const auto nets = localNetworks();
    if (nets.isEmpty()) {
        checkStep(tr("This device's network"), QStringLiteral("fail"),
                  tr("This device has no network address: turn on Wi-Fi or mobile data."));
        checkFinish();
        return;
    }
    QStringList addrs;
    for (const auto& n : nets) {
        addrs << n.first.toString() + QLatin1Char('/') + QString::number(n.second);
    }
    checkStep(tr("This device's network"), QStringLiteral("ok"),
              tr("Connected, at %1.").arg(addrs.join(QStringLiteral(", "))));

    if (t.isEmpty()) {
        checkStep(tr("The radio"), QStringLiteral("info"),
                  tr("No radio tried yet: tap one to connect, then check again if it does not connect."));
        checkFinish();
        return;
    }
    if (t.startsWith(QLatin1String("smartlink:"))) {
        checkSmartLink(t.mid(10));
        return;
    }
    const QHostAddress address(t);
    if (!address.isNull()) {
        checkAddress(t, address);
        return;
    }
    // 2. A host name, looked up first.
    checkStep(tr("Looking up %1").arg(t), QStringLiteral("running"), QString());
    const int run = m_checkRun;
    QHostInfo::lookupHost(t, this, [this, run, t](const QHostInfo& info) {
        if (run != m_checkRun) return;
        QHostAddress found;
        for (const QHostAddress& a : info.addresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                found = a;
                break;
            }
        }
        if (found.isNull()) {
            checkStep(tr("Looking up %1").arg(t), QStringLiteral("fail"),
                      tr("The name was not found (%1). Check the spelling; a dynamic DNS name needs its service "
                         "up to date, a Tailscale name needs the Tailscale app on.")
                          .arg(info.errorString()));
            checkFinish();
            return;
        }
        checkStep(tr("Looking up %1").arg(t), QStringLiteral("ok"), tr("It is %1.").arg(found.toString()));
        checkAddress(t, found);
    });
}

void MobileEngine::checkAddress(const QString& host, const QHostAddress& address)
{
    // 3. Where the radio is from here.
    bool sameNetwork = false;
    for (const auto& n : localNetworks()) {
        if (address.isInSubnet(n.first, n.second)) {
            sameNetwork = true;
        }
    }
    const bool privateAddress = address.isPrivateUse() || address.isLinkLocal();
    QString where;
    if (sameNetwork) {
        where = tr("%1 is on this device's own network.").arg(address.toString());
    } else if (isTailscale(address)) {
        where = tr("%1 is a Tailscale address: the Tailscale app must be on, on this device and at the radio's end.")
                    .arg(address.toString());
    } else if (privateAddress) {
        where = tr("%1 is on another private network: it is reached only through a router route or a VPN "
                   "(WireGuard, Tailscale's subnet router) that is on now.")
                    .arg(address.toString());
    } else {
        where = tr("%1 is on the internet: the radio's router must forward TCP 4992 and UDP 4991 to it "
                   "(SmartLink needs no forwarding).")
                    .arg(address.toString());
    }
    checkStep(tr("Where the radio is"), QStringLiteral("info"), where);

    // 4. The radio's own port.
    const QString title = tr("The radio's port (TCP %1)").arg(kRadioPort);
    checkStep(title, QStringLiteral("running"), tr("Connecting to %1…").arg(address.toString()));
    auto* socket = new QTcpSocket(this);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    const int run = m_checkRun;
    auto lines = std::make_shared<QStringList>();
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);

    auto finishWith = [this, socket, run, done](const QString& t, const QString& state, const QString& detail) {
        if (*done) return;
        *done = true;
        socket->abort();
        socket->deleteLater();
        if (run != m_checkRun) return;
        checkStep(t, state, detail);
    };

    connect(socket, &QTcpSocket::connected, this, [this, socket, timer, title, run, address] {
        if (run != m_checkRun) return;
        checkStep(title, QStringLiteral("ok"), tr("%1 answers on the radio's port.").arg(address.toString()));
        // Read what the radio says, without registering a station.
        socket->write("C1|sub radio all\nC2|sub client all\n");
        timer->start(kListenMs);
    });
    connect(socket, &QTcpSocket::readyRead, this, [socket, lines, buffer] {
        *buffer += socket->readAll();
        int nl;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            lines->append(QString::fromUtf8(buffer->left(nl)).trimmed());
            buffer->remove(0, nl + 1);
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket, title, address, privateAddress, sameNetwork, finishWith](QAbstractSocket::SocketError e) {
        if (socket->state() == QAbstractSocket::ConnectedState) return;
        QString detail;
        if (e == QAbstractSocket::ConnectionRefusedError) {
            detail = tr("%1 answers, but nothing listens on the radio's port: the address is another device, "
                        "or the radio is starting up or off.")
                         .arg(address.toString());
        } else {
            detail = tr("No answer from %1 (%2).").arg(address.toString(), socket->errorString());
            if (!sameNetwork && privateAddress) {
                detail += QLatin1Char(' ') + tr("Is the VPN on, and does it route the radio's network?");
            } else if (sameNetwork) {
                detail += QLatin1Char(' ') + tr("Is the radio on, and is this its address now (its router may "
                                                "have given it another)?");
            }
            if (kLocalNetworkPermission && privateAddress) {
                detail += QLatin1Char(' ') + tr("And check that OksaSDR may use the local network: Settings › "
                                                "Privacy & Security › Local Network › OksaSDR.");
            }
        }
        finishWith(title, QStringLiteral("fail"), detail);
        checkFinish();
    });
    connect(timer, &QTimer::timeout, this, [this, socket, lines, run, title, address, finishWith, done] {
        if (*done) return;
        if (socket->state() != QAbstractSocket::ConnectedState) {
            finishWith(title, QStringLiteral("fail"),
                       tr("No answer from %1 in %2 s: the address is not reached from here (wrong network, the "
                          "VPN off, or a firewall).")
                           .arg(address.toString())
                           .arg(kConnectTimeoutMs / 1000));
            checkFinish();
            return;
        }
        *done = true;
        socket->abort();
        socket->deleteLater();
        if (run != m_checkRun) return;
        // What the radio said: its version, multiFLEX, the stations on it.
        QString version;
        QString mf;
        QStringList stations;
        for (const QString& l : std::as_const(*lines)) {
            if (l.startsWith(QLatin1Char('V'))) version = l.mid(1);
            const int bar = l.indexOf(QLatin1Char('|'));
            const QString body = bar >= 0 ? l.mid(bar + 1) : l;
            if (body.startsWith(QLatin1String("radio ")) && body.contains(QLatin1String("mf_enable="))) {
                mf = body.section(QLatin1String("mf_enable="), 1).section(QLatin1Char(' '), 0, 0);
            }
            if (body.startsWith(QLatin1String("client ")) && body.contains(QLatin1String(" connected"))
                && body.contains(QLatin1String("program="))) {
                const QString program = body.section(QLatin1String("program="), 1).section(QLatin1Char(' '), 0, 0);
                const QString station = body.section(QLatin1String("station="), 1).section(QLatin1Char(' '), 0, 0);
                if (!program.isEmpty()) {
                    stations << (station.isEmpty() ? program
                                                   : program + QStringLiteral(" (") + QString(station).replace(QChar(0x7f), QLatin1Char(' ')) + QLatin1Char(')'));
                }
            }
        }
        const QString who = stations.isEmpty() ? tr("no station") : stations.join(QStringLiteral(", "));
        QString detail = tr("Firmware protocol %1. On the radio now: %2.")
                             .arg(version.isEmpty() ? QStringLiteral("?") : version, who);
        QString state = QStringLiteral("ok");
        if (mf == QLatin1String("0") && !stations.isEmpty()) {
            state = QStringLiteral("warn");
            detail += QLatin1Char(' ') + tr("multiFLEX is off: the radio takes one station at a time, and one is on "
                                            "it. The app asks to disconnect it when you connect.");
        } else if (mf == QLatin1String("1")) {
            detail += QLatin1Char(' ') + tr("multiFLEX is on: more than one station may connect, up to the radio's "
                                            "licence.");
        }
        checkStep(tr("What the radio says"), state, detail);
        checkStep(tr("Spectrum and audio"), QStringLiteral("info"),
                  tr("They come over UDP once connected: over a VPN its MTU must pass them (the app asks the radio "
                     "for 1400-byte packets). An empty display with the radio connected points there."));
        checkFinish();
    });

    socket->connectToHost(address, kRadioPort);
    timer->start(kConnectTimeoutMs);
}

void MobileEngine::checkSmartLink(const QString& serial)
{
    if (m_smartLinkState != QLatin1String("signedIn")) {
        checkStep(tr("SmartLink"), QStringLiteral("fail"),
                  tr("Not signed in to SmartLink: sign in on the Connect page (your FlexRadio account)."));
        checkFinish();
        return;
    }
    checkStep(tr("SmartLink"), QStringLiteral("ok"), tr("Signed in."));
    const WanRadioInfo* radio = nullptr;
    for (const WanRadioInfo& r : std::as_const(m_wanRadios)) {
        if (r.serial == serial) radio = &r;
    }
    if (!radio) {
        checkStep(tr("The radio on SmartLink"), QStringLiteral("fail"),
                  tr("The radio is not in your SmartLink list: it is off, has no internet, or SmartLink is not "
                     "enabled on it (SmartSDR's Radio Setup)."));
        checkFinish();
        return;
    }
    const QString name = radio->nickname.isEmpty() ? radio->model : radio->nickname;
    const bool available = radio->status.compare(QLatin1String("Available"), Qt::CaseInsensitive) == 0
                           || radio->status.compare(QLatin1String("In Use"), Qt::CaseInsensitive) == 0;
    checkStep(tr("The radio on SmartLink"), available ? QStringLiteral("ok") : QStringLiteral("warn"),
              tr("%1 (%2) is listed: %3.").arg(name, radio->model, radio->status.isEmpty() ? tr("no status") : radio->status));
    QStringList stations;
    for (const QString& p : radio->guiClientPrograms.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        stations << p.trimmed();
    }
    const int used = int(stations.size());
    const QString who = stations.isEmpty() ? tr("no station") : stations.join(QStringLiteral(", "));
    if (radio->licensedClients <= 1 && used >= 1) {
        checkStep(tr("Station slots"), QStringLiteral("warn"),
                  tr("On the radio now: %1. It takes one station at a time: the app asks to disconnect that one "
                     "when you connect, or turn multiFLEX on in SmartSDR's Radio Setup.").arg(who));
    } else if (used >= radio->licensedClients) {
        checkStep(tr("Station slots"), QStringLiteral("fail"),
                  tr("On the radio now: %1, all %2 of its station slots. Disconnect one, then connect.")
                      .arg(who).arg(radio->licensedClients));
    } else {
        checkStep(tr("Station slots"), QStringLiteral("ok"),
                  tr("On the radio now: %1; %2 of %3 slots free.").arg(who).arg(radio->licensedClients - used).arg(radio->licensedClients));
    }
    if (radio->requiresHolePunch) {
        checkStep(tr("The way in"), QStringLiteral("info"),
                  tr("The radio's router has no port forward or UPnP: SmartLink opens the way by hole punching, "
                     "which some mobile networks block. A forwarded port in the radio's router (SmartSDR's "
                     "SmartLink settings) is the sure way."));
    }
    checkFinish();
}

}  // namespace AetherSDR::mobile
