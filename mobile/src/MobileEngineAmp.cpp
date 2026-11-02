// An RF-KIT RF2K-S amplifier, read over its own REST API (Sebastian Schu
// DG5CST asked for his; others for the FlexRadio amplifier and tuner, which
// come later): the first step is readings only. RfkitConnection (upstream #6596, tested there
// on an RF2K-S) runs read-only, so only GET requests ever reach the
// amplifier; Operate / Standby, the antenna and the error reset come after a
// test on a real amplifier (the roadmap's hardware gate).
//
// The amplifier is a device on the operator's network, not behind the
// radio: the phone reaches it directly (same network or a VPN), never over
// SmartLink. It is read while the app is on screen and connected to a
// radio, every half second (the desktop's 250 ms is more than a phone needs).

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/RfkitConnection.h"

#include <QGuiApplication>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kHostKey = QStringLiteral("MobileAmpRf2kHost");
const QString kOnKey = QStringLiteral("MobileAmpRf2kOn");
constexpr int kPollMs = 500;

// "host" or "host:port"; the API's port 8080 when not given. False for a
// port that is not a number in range.
bool splitAmpAddress(const QString& spec, QString* host, quint16* port)
{
    *host = spec;
    *port = Rfkit::kDefaultPort;
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

QString operateName(Rfkit::OperateMode mode)
{
    return mode == Rfkit::OperateMode::Operate ? QStringLiteral("OPERATE") : QStringLiteral("STANDBY");
}
}  // namespace

void MobileEngine::setupAmp()
{
    m_amp = new RfkitConnection(this);
    m_amp->setReadOnly(true);
    m_amp->setPollInterval(kPollMs);
    // Keeps trying every 5 s while it should be read (the amplifier off,
    // the VPN down): the state shows "connecting" or the last failure.
    m_amp->setAutoReconnect(true);
    connect(m_amp, &RfkitConnection::connected, this, [this] {
        m_ampState = QStringLiteral("connected");
        m_ampError.clear();
        qCInfo(lcMobile).noquote() << "amp: RF2K-S connected at" << m_amp->description() << "(read only)";
        emit ampChanged();
    });
    connect(m_amp, &RfkitConnection::disconnected, this, [this] {
        // A close of our own (off, background, another address) is not a
        // lost link: updateAmpLink() has set the state already.
        if (!m_ampWanted) {
            return;
        }
        m_ampStatus = {};
        m_ampState = QStringLiteral("connecting");
        qCInfo(lcMobile) << "amp: RF2K-S link lost";
        emit ampChanged();
    });
    connect(m_amp, &RfkitConnection::connectionFailed, this, [this](const QString& error) {
        if (!m_ampWanted) {
            return;
        }
        m_ampStatus = {};
        if (m_ampState != QLatin1String("failed") || m_ampError != error) {
            qCInfo(lcMobile).noquote() << "amp: RF2K-S at" << m_amp->description() << "not reached:" << error;
        }
        m_ampState = QStringLiteral("failed");
        m_ampError = error;
        emit ampChanged();
    });
    connect(m_amp, &RfkitConnection::statusUpdated, this, [this](const Rfkit::Status& status) {
        m_ampStatus = status;
        emit ampChanged();
    });
    connect(this, &MobileEngine::connectionStateChanged, this, &MobileEngine::updateAmpLink);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MobileEngine::updateAmpLink);
}

// Read while it is set up and on, the app on screen and connected to a
// radio (the Amp tab is on the operating page); else the link is closed.
void MobileEngine::updateAmpLink()
{
    QString host;
    quint16 port = 0;
    const QString spec = AppSettings::instance().value(kHostKey).toString().trimmed();
    const bool on = AppSettings::instance().value(kOnKey).toString() == QLatin1String("1");
    const Qt::ApplicationState app = qApp->applicationState();
    const bool onScreen = app == Qt::ApplicationActive || app == Qt::ApplicationInactive;
    const bool want = on && splitAmpAddress(spec, &host, &port) && m_connected && onScreen;
    const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
    if (want == m_ampWanted && (!want || target == m_ampTarget)) {
        return;
    }
    // Closed first in every case, as our own close (another address too).
    m_ampWanted = false;
    m_amp->disconnect();
    m_ampWanted = want;
    m_ampTarget = want ? target : QString();
    m_ampStatus = {};
    m_ampError.clear();
    if (want) {
        m_ampState = QStringLiteral("connecting");
        qCInfo(lcMobile).noquote() << "amp: reading the RF2K-S at" << target;
        m_amp->connectNetwork(host, port);
    } else {
        m_ampState = QStringLiteral("off");
        qCInfo(lcMobile) << "amp: not read";
    }
    emit ampChanged();
}

void MobileEngine::setAmpAddress(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitAmpAddress(spec, &host, &port)) {
        setLastError(tr("Amplifier address: use host or host:port, e.g. 192.168.1.60:8080."));
        return;
    }
    AppSettings::instance().setValue(kHostKey, spec);
    // A new address is meant to be read; forgetting it turns it off.
    AppSettings::instance().setValue(kOnKey, spec.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    AppSettings::instance().save();
    updateAmpLink();
    emit ampChanged();
}

void MobileEngine::setAmpOn(bool on)
{
    AppSettings::instance().setValue(kOnKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    updateAmpLink();
    emit ampChanged();
}

QVariantMap MobileEngine::amp() const
{
    const QString spec = AppSettings::instance().value(kHostKey).toString().trimmed();
    QVariantMap m{{QStringLiteral("set"), !spec.isEmpty()},
                  {QStringLiteral("address"), spec},
                  {QStringLiteral("on"), AppSettings::instance().value(kOnKey).toString() == QLatin1String("1")},
                  {QStringLiteral("state"), m_ampState},
                  {QStringLiteral("error"), m_ampError},
                  {QStringLiteral("target"), m_ampTarget}};
    const Rfkit::Status& s = m_ampStatus;
    // A reading not in yet is left out (the page shows "–"), never 0.
    if (s.info) {
        m.insert(QStringLiteral("device"), s.info->device);
        m.insert(QStringLiteral("name"), s.info->customName);
        m.insert(QStringLiteral("firmware"), QStringLiteral("%1 / %2").arg(s.info->guiVersion).arg(s.info->controllerVersion));
    }
    if (s.power) {
        m.insert(QStringLiteral("fwd"), s.power->forward.value);
        m.insert(QStringLiteral("refl"), s.power->reflected.value);
        m.insert(QStringLiteral("swr"), s.power->swr.value);
        m.insert(QStringLiteral("temp"), Rfkit::temperatureCelsius(s.power->temperature));
        m.insert(QStringLiteral("volts"), s.power->voltage.value);
        m.insert(QStringLiteral("amps"), s.power->current.value);
    }
    if (s.data) {
        m.insert(QStringLiteral("band"), Rfkit::formatBand(s.data->band));
        m.insert(QStringLiteral("freq"), Rfkit::formatFrequency(s.data->frequency));
        m.insert(QStringLiteral("status"), s.data->status);
    }
    if (s.operate) {
        m.insert(QStringLiteral("operate"), operateName(*s.operate));
    }
    if (s.tuner) {
        m.insert(QStringLiteral("tuner"), Rfkit::tunerModeLabel(s.tuner->mode));
        m.insert(QStringLiteral("tunerSetup"), Rfkit::tunerSetupLabel(s.tuner->setup));
    }
    if (s.interfaceState) {
        m.insert(QStringLiteral("iface"), Rfkit::interfaceLabel(s.interfaceState->active));
        m.insert(QStringLiteral("ifaceError"), s.interfaceState->error);
    }
    if (s.antennas) {
        QStringList available;
        for (const Rfkit::AntennaWithState& a : *s.antennas) {
            QString label = Rfkit::antennaLabel(a.antenna);
            if (a.antenna.type == Rfkit::AntennaType::External && a.externalPort > 0) {
                label += QStringLiteral(" %1").arg(a.externalPort);
            }
            if (a.state == Rfkit::AntennaState::Active) {
                m.insert(QStringLiteral("antenna"), label);
            }
            if (a.state != Rfkit::AntennaState::Disabled) {
                available << label;
            }
        }
        m.insert(QStringLiteral("antennas"), available.join(QStringLiteral(", ")));
    }
    return m;
}

}  // namespace AetherSDR::mobile
