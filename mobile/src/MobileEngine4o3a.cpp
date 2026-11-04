// 4O3A's Power Genius XL amplifier and Tuner Genius XL tuner, which the
// radio integrates (Mike Walker VA3MW's station; the user, build 152: both
// working, not only their meters). Built on the engine's own models, as the
// desktop wires them (MainWindow_Wiring.cpp):
//
// - The radio reports each device ("amplifier <h> model=PowerGeniusXL /
//   TunerGeniusXL ...") into AmpModel / TunerModel, and relays Operate /
//   Standby, the tuner's Operate / Bypass and its meters: that works over
//   SmartLink too.
// - The app also reaches each device directly at the address the radio
//   reports (or one set in Radio Setup › Amp): the PGXL on port 9008 for its
//   supply, temperatures, ports and fan, the TGXL on 9010 for its relays,
//   antenna switch and fast meters. Only on the same network or a VPN.
//
// The devices protect themselves (SWR, power, temperature; the radio's
// interlock waits for the amplifier; the tuner keys the radio for its own
// tune and switches its amplifier PTT as set in its utility), so the app adds
// only what a touch screen needs: control is off until turned on in Radio
// Setup, every change is confirmed on the page, and the tuner's relays and
// antenna switch, whose protection 4O3A does not describe, are not moved
// while the radio transmits.

#include "MobileEngine.h"

#include <algorithm>
#include <memory>
#include "SecureStore.h"

#include "core/AppSettings.h"
#include "core/PgxlConnection.h"
#include "core/TgxlConnection.h"
#include "models/AmpModel.h"
#include "models/AntennaGeniusModel.h"
#include "models/MeterModel.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kPgxlControlKey = QStringLiteral("MobilePgxlControl");
const QString kTgxlControlKey = QStringLiteral("MobileTgxlControl");
const QString kPgxlIpKey = QStringLiteral("PGXL_ManualIp");
const QString kTgxlIpKey = QStringLiteral("TGXL_ManualIp");
const QString kPgxlAuthSecret = QStringLiteral("PgxlAuthCode");
const QString kTgxlAuthSecret = QStringLiteral("TgxlAuthCode");
const QString kAgControlKey = QStringLiteral("MobileAgControl");
const QString kAgIpKey = QStringLiteral("AG_ManualIp");
const QString kAgAuthSecret = QStringLiteral("AgAuthCode");
// A relayed reading counts as live this long (AccessoryPanelWidgets' 1.5 s).
constexpr qint64 kRelayFreshMs = 1500;

// Reflected power from forward power and SWR, for the Amp Refl meter.
double reflectedWatts(double fwd, double swr)
{
    if (fwd <= 0.0 || swr <= 1.0) return 0.0;
    const double rho = (swr - 1.0) / (swr + 1.0);
    return fwd * rho * rho;
}

bool settingOn(const QString& key)
{
    return AppSettings::instance().value(key, QStringLiteral("False")).toString() == QLatin1String("True");
}
}

void MobileEngine::setup4o3a()
{
    RadioModel& rm = m_session.radioModel();
    m_pgxlConn = new PgxlConnection(this);
    m_tgxlConn = new TgxlConnection(this);
    m_pgxlConn->setAutoReconnect(true);
    m_tgxlConn->setAutoReconnect(true);
    rm.amplifier().setDirectConnection(m_pgxlConn);
    rm.tunerModel().setDirectConnection(m_tgxlConn);

    // The Antenna Genius, at the address set in Radio Setup › Amp. Band recall
    // off: nothing goes to it but the operator's own antenna choice (it and
    // a TGXL keep their own band memories).
    m_ag = new AntennaGeniusModel(this);
    m_ag->setBandRecall(false);
    m_ag->setAutoReconnect(true);
    connect(m_ag, &AntennaGeniusModel::authCodeRequired, this, [this](quint64 attempt) {
        m_ag->setAuthCodeForAttempt(attempt, loadSecret(kAgAuthSecret), !secureStoreAvailable());
    });
    connect(m_ag, &AntennaGeniusModel::authCodeAccepted, this, [](const QString& code) { saveSecret(kAgAuthSecret, code); });
    connect(m_ag, &AntennaGeniusModel::connectionError, this, [this](const QString& msg) {
        m_agLinkError = msg;
        emit agChanged();
    });
    connect(m_ag, &AntennaGeniusModel::connected, this, [this] {
        m_agLinkError.clear();
        emit agChanged();
    });
    // What the AG says of itself, into the shared log once its info reply is in.
    connect(m_ag, &AntennaGeniusModel::deviceInfoChanged, this, [this] {
        const AgDeviceInfo& d = m_ag->connectedDevice();
        qCInfo(lcMobile).noquote() << "AG says: name=" + d.name << "serial=" + d.serial << "version=" + d.version
                                   << "ports=" + QString::number(d.radioPorts) << "antennas=" + QString::number(d.antennaPorts)
                                   << "mode=" + d.mode;
    });
    connect(m_ag, &AntennaGeniusModel::antennasChanged, this, [this] {
        QStringList names;
        for (const AgAntennaInfo& a : m_ag->antennas())
            names << QStringLiteral("%1 %2 tx=%3 rx=%4").arg(a.id).arg(a.name)
                         .arg(a.txBandMask, 4, 16, QLatin1Char('0')).arg(a.rxBandMask, 4, 16, QLatin1Char('0'));
        qCInfo(lcMobile).noquote() << "AG antennas:" << names.join(QStringLiteral("; "));
    });
    for (auto sig : {&AntennaGeniusModel::disconnected, &AntennaGeniusModel::antennasChanged,
                     &AntennaGeniusModel::bandsChanged, &AntennaGeniusModel::deviceInfoChanged}) {
        connect(m_ag, sig, this, [this] { emit agChanged(); });
    }
    connect(m_ag, &AntennaGeniusModel::portStatusChanged, this, [this](int port) {
        const AgPortStatus p = port == 1 ? m_ag->portA() : m_ag->portB();
        qCInfo(lcMobile).noquote() << QStringLiteral("AG port %1: band %2 rx %3 tx %4 %5%6%7")
            .arg(port == 1 ? "A" : "B", m_ag->bandName(p.band), m_ag->antennaName(p.rxAntenna), m_ag->antennaName(p.txAntenna),
                 p.autoMode ? QStringLiteral("auto") : QStringLiteral("manual"),
                 p.transmitting ? QStringLiteral(" tx") : QString(), p.inhibited ? QStringLiteral(" inhibited") : QString());
        emit agChanged();
    });

    // An SO2R TGXL (no 3way in its info reply): its active channel and both
    // sides, from its own status.
    connect(m_tgxlConn, &TgxlConnection::statusUpdated, this, [this](const QMap<QString, QString>& kvs) {
        bool changed = false;
        if (kvs.contains(QStringLiteral("serial")) && kvs.contains(QStringLiteral("version"))) {
            const bool so2r = kvs.value(QStringLiteral("3way")) != QLatin1String("1");
            if (so2r != m_tgxlSo2r) { m_tgxlSo2r = so2r; changed = true; }
        }
        if (kvs.contains(QStringLiteral("active"))) {
            const int a = kvs.value(QStringLiteral("active")).toInt();
            if (a != m_tgxlActive) { m_tgxlActive = a; changed = true; }
        }
        for (const char* k : {"freqA", "freqB", "bandA", "bandB", "antA", "antB", "modeA", "modeB", "flexA", "flexB", "bypassA", "bypassB"}) {
            const QString key = QString::fromLatin1(k);
            if (kvs.contains(key) && m_tgxlSides.value(key).toString() != kvs.value(key)) {
                m_tgxlSides[key] = kvs.value(key);
                changed = true;
            }
        }
        if (changed) emit tgxlChanged();
    });
    connect(m_tgxlConn, &TgxlConnection::stateUpdated, this, [this](const QMap<QString, QString>& kvs) {
        if (kvs.contains(QStringLiteral("active"))) {
            const int a = kvs.value(QStringLiteral("active")).toInt();
            if (a != m_tgxlActive) { m_tgxlActive = a; emit tgxlChanged(); }
        }
    });

    // An AUTH greeting asks for the device's code: the one kept in the
    // secure store, saved only once the device has accepted it.
    connect(m_pgxlConn, &PgxlConnection::authCodeRequired, this, [this](quint64 attempt) {
        m_pgxlConn->setAuthCodeForAttempt(attempt, loadSecret(kPgxlAuthSecret), !secureStoreAvailable());
    });
    connect(m_tgxlConn, &TgxlConnection::authCodeRequired, this, [this](quint64 attempt) {
        m_tgxlConn->setAuthCodeForAttempt(attempt, loadSecret(kTgxlAuthSecret), !secureStoreAvailable());
    });
    connect(m_pgxlConn, &PgxlConnection::authCodeAccepted, this, [](const QString& code) { saveSecret(kPgxlAuthSecret, code); });
    connect(m_tgxlConn, &TgxlConnection::authCodeAccepted, this, [](const QString& code) { saveSecret(kTgxlAuthSecret, code); });
    connect(m_pgxlConn, &PgxlConnection::connectionFailed, this, [this](const QString& reason) {
        m_pgxlLinkError = reason;
        emit pgxlChanged();
    });
    connect(m_tgxlConn, &TgxlConnection::connectionFailed, this, [this](const QString& reason) {
        m_tgxlLinkError = reason;
        emit tgxlChanged();
    });
    for (auto sig : {&PgxlConnection::connected, &PgxlConnection::disconnected}) {
        connect(m_pgxlConn, sig, this, [this] {
            if (m_pgxlConn->isConnected()) m_pgxlLinkError.clear();
            emit pgxlChanged();
        });
    }
    for (auto sig : {&TgxlConnection::connected, &TgxlConnection::disconnected}) {
        connect(m_tgxlConn, sig, this, [this] {
            if (m_tgxlConn->isConnected()) m_tgxlLinkError.clear();
            emit tgxlChanged();
        });
    }

    // What each device says of itself, into the log a tester shares: its info
    // reply (serial, version, 3way on a 1x3 TGXL) and its first status after
    // each connect, the keys its firmware uses (an SO2R TGXL's channel fields,
    // a PGXL's setup group without its remote code).
    {
        auto logFirst = [](const char* device, const QMap<QString, QString>& kvs, bool* logged) {
            if (*logged) return;
            QStringList fields;
            for (auto it = kvs.constBegin(); it != kvs.constEnd(); ++it) {
                if (it.key().contains(QLatin1String("auth"), Qt::CaseInsensitive)
                    || it.key() == QLatin1String("code"))
                    fields << it.key() + QStringLiteral("=<hidden>");
                else
                    fields << it.key() + QLatin1Char('=') + it.value();
            }
            qCInfo(lcMobile).noquote() << device << "says:" << fields.join(QLatin1Char(' '));
            // The info reply carries serial and version; the first status
            // with fields beyond those ends the logging until the next connect.
            if (!kvs.contains(QStringLiteral("serial"))) *logged = true;
        };
        auto tgxlLogged = std::make_shared<bool>(false);
        auto pgxlLogged = std::make_shared<bool>(false);
        connect(m_tgxlConn, &TgxlConnection::connected, this, [tgxlLogged] { *tgxlLogged = false; });
        connect(m_pgxlConn, &PgxlConnection::connected, this, [pgxlLogged] { *pgxlLogged = false; });
        connect(m_tgxlConn, &TgxlConnection::statusUpdated, this,
                [logFirst, tgxlLogged](const QMap<QString, QString>& kvs) { logFirst("TGXL", kvs, tgxlLogged.get()); });
        connect(m_pgxlConn, &PgxlConnection::statusUpdated, this,
                [logFirst, pgxlLogged](const QMap<QString, QString>& kvs) { logFirst("PGXL", kvs, pgxlLogged.get()); });
        connect(m_pgxlConn, &PgxlConnection::setupRead, this, [logFirst](const QMap<QString, QString>& kvs) {
            bool once = false;
            logFirst("PGXL setup", kvs, &once);
        });
    }

    // The meters a 4O3A device makes on the radio, into the log too: each
    // AMP meter's definition as the radio lists it (name, unit, range, whose
    // handle), the first values after a connect, and each transmission's
    // highest readings, so a tester's log shows whether a real PGXL's or
    // TGXL's meters flow over the radio and on what scale.
    {
        MeterModel& mm = rm.meterModel();
        connect(&mm, &MeterModel::meterDefinitionChanged, this, [&mm](int index) {
            const MeterDef* d = mm.meterDef(index);
            if (!d || d->source != QLatin1String("AMP")) return;
            qCInfo(lcMobile).noquote() << QStringLiteral("radio meter %1: AMP %2 (%3, %4..%5) of 0x%6")
                .arg(d->index).arg(d->name, d->unit).arg(d->low).arg(d->high)
                .arg(static_cast<quint32>(d->sourceIndex), 8, 16, QLatin1Char('0'));
        });
        struct Peak { bool firstAmp = false, firstTgxl = false; float fwd = 0, drv = 0, id = 0, temp = 0, tfwd = 0, tswr = 0; };
        auto peak = std::make_shared<Peak>();
        connect(&mm, &MeterModel::ampMetersChanged, this,
                [peak](float fwd, float swr, float temp, float drv, bool drvValid) {
            if (!peak->firstAmp) {
                peak->firstAmp = true;
                qCInfo(lcMobile).noquote() << QStringLiteral("radio meters, amplifier: forward %1 W, SWR %2, temp %3, drive %4")
                    .arg(fwd, 0, 'f', 1).arg(swr, 0, 'f', 2).arg(temp, 0, 'f', 1)
                    .arg(drvValid ? QString::number(drv, 'f', 1) + QStringLiteral(" W") : QStringLiteral("none"));
            }
            peak->fwd = std::max(peak->fwd, fwd);
            if (drvValid) peak->drv = std::max(peak->drv, drv);
            peak->temp = std::max(peak->temp, temp);
        });
        connect(&mm, &MeterModel::ampVitalsChanged, this,
                [peak](float id, bool idValid, bool, float, bool, bool) {
            if (idValid) peak->id = std::max(peak->id, id);
        });
        connect(&mm, &MeterModel::tgxlMetersChanged, this, [peak](float fwd, float swr) {
            if (!peak->firstTgxl) {
                peak->firstTgxl = true;
                qCInfo(lcMobile).noquote() << QStringLiteral("radio meters, tuner: forward %1 W, SWR %2")
                    .arg(fwd, 0, 'f', 1).arg(swr, 0, 'f', 2);
            }
            peak->tfwd = std::max(peak->tfwd, fwd);
            peak->tswr = std::max(peak->tswr, swr);
        });
        connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, [peak](bool on) {
            if (on) {
                *peak = Peak{true, true};
                return;
            }
            if (peak->fwd > 0 || peak->drv > 0 || peak->tfwd > 0)
                qCInfo(lcMobile).noquote() << QStringLiteral("radio meters over the transmission: amplifier forward %1 W, drive %2 W, current %3 A, temp %4; tuner forward %5 W, SWR %6")
                    .arg(peak->fwd, 0, 'f', 1).arg(peak->drv, 0, 'f', 1).arg(peak->id, 0, 'f', 1)
                    .arg(peak->temp, 0, 'f', 1).arg(peak->tfwd, 0, 'f', 1).arg(peak->tswr, 0, 'f', 2);
        });
        connect(this, &MobileEngine::connectionStateChanged, this, [this, peak] {
            if (m_connectionState == QLatin1String("connected")) *peak = Peak{};
        });
    }

    // The amplifier's own status: supply, temperatures, fan.
    connect(m_pgxlConn, &PgxlConnection::statusUpdated, this, [this](const QMap<QString, QString>& kvs) {
        if (kvs.contains(QStringLiteral("temp"))) {
            const QString tv = kvs.value(QStringLiteral("temp"));
            const int slash = tv.indexOf(QLatin1Char('/'));
            m_pgxlTemp = (slash >= 0 ? tv.left(slash) : tv).toDouble();
            if (slash >= 0) m_pgxlHlTemp = tv.mid(slash + 1).toDouble();
        }
        if (kvs.contains(QStringLiteral("hltemp"))) m_pgxlHlTemp = kvs.value(QStringLiteral("hltemp")).toDouble();
        else if (kvs.contains(QStringLiteral("tempb"))) m_pgxlHlTemp = kvs.value(QStringLiteral("tempb")).toDouble();
        if (kvs.contains(QStringLiteral("id"))) m_pgxlId = kvs.value(QStringLiteral("id")).toDouble();
        if (kvs.contains(QStringLiteral("vdd"))) m_pgxlVdd = kvs.value(QStringLiteral("vdd")).toDouble();
        if (kvs.contains(QStringLiteral("vac"))) m_pgxlVac = kvs.value(QStringLiteral("vac")).toInt();
        emit pgxlChanged();
    });
    connect(&rm.amplifier(), &AmpModel::directMetersChanged, this, [this](float watts, float swr) {
        m_pgxlDirectFwd = watts;
        m_pgxlDirectSwr = swr;
        emit pgxlChanged();
    });
    // The radio's relay of the amplifier's meters (over SmartLink too).
    connect(&rm.meterModel(), &MeterModel::ampMetersChanged, this,
            [this](float fwd, float swr, float, float drive, bool driveValid) {
        if (m_session.radioModel().meterModel().hasAmpPower()) {
            m_pgxlRelayFwd = fwd;
            m_pgxlRelaySwr = swr;
            m_pgxlRelayAt = QDateTime::currentMSecsSinceEpoch();
        }
        m_pgxlDrive = driveValid ? drive : -1.0;
        emit pgxlChanged();
    });
    connect(&rm.meterModel(), &MeterModel::ampVitalsChanged, this,
            [this](float id, bool idValid, bool idUpdated, float temp, bool tempValid, bool tempUpdated) {
        if (idValid && idUpdated) m_pgxlId = id;
        if (tempValid && tempUpdated) m_pgxlTemp = temp;
        emit pgxlChanged();
    });
    connect(&rm.meterModel(), &MeterModel::tgxlMetersChanged, this, [this](float, float) { emit tgxlChanged(); });
    for (auto sig : {&AmpModel::presenceChanged}) {
        connect(&rm.amplifier(), sig, this, [this](bool) { connect4o3a(); emit pgxlChanged(); emit meterSelectionChanged(); });
    }
    connect(&rm.amplifier(), &AmpModel::stateChanged, this, [this] { m_pgxlPending = false; emit pgxlChanged(); emit ratedPowerChanged(); });
    connect(&rm.amplifier(), &AmpModel::ampStateChanged, this, [this](const QString&) { emit pgxlChanged(); });
    connect(&rm.amplifier(), &AmpModel::alertChanged, this, [this](const QString&) { emit pgxlChanged(); });
    connect(&rm.amplifier(), &AmpModel::portsChanged, this, &MobileEngine::pgxlChanged);
    connect(&rm.amplifier(), &AmpModel::meffaChanged, this, [this](const QString&) { emit pgxlChanged(); });
    connect(&rm.amplifier(), &AmpModel::telemetryUpdated, this, [this](const QMap<QString, QString>&) { emit pgxlChanged(); });
    connect(&rm.tunerModel(), &TunerModel::presenceChanged, this, [this](bool) { connect4o3a(); emit tgxlChanged(); emit meterSelectionChanged(); });
    connect(&rm.tunerModel(), &TunerModel::stateChanged, this, [this] {
        if (!m_tgxlConn->isConnected()) connect4o3a();
        emit tgxlChanged();
    });
    connect(&rm.tunerModel(), &TunerModel::metersChanged, this, [this](float, float, float) { emit tgxlChanged(); });
    connect(&rm.tunerModel(), &TunerModel::alertChanged, this, [this](const QString&) { emit tgxlChanged(); });
    connect(&rm.transmitModel(), &TransmitModel::apdStateChanged, this, &MobileEngine::apdChanged);
    connect(&rm.transmitModel(), &TransmitModel::apdSamplerChanged, this, [this](const QString&) { emit apdChanged(); });
    // The links follow the radio's connection.
    connect(this, &MobileEngine::connectionStateChanged, this, [this] {
        if (m_connectionState == QLatin1String("connected")) connect4o3a();
        else if (m_connectionState == QLatin1String("idle")) disconnect4o3a();
    });
    // The direct links poll fast from the moment the radio keys (tune too), as
    // the desktop does, not one 250 ms receive poll later; the devices' own
    // ptt fields drop the rate again.
    connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, [this](bool on) {
        if (!on) return;
        if (m_tgxlConn) m_tgxlConn->setTransmitting(true);
        if (m_pgxlConn) m_pgxlConn->setTransmitting(true);
    });
    connect(&rm.tunerModel(), &TunerModel::stateChanged, this, [this, &rm] {
        if (rm.tunerModel().isTuning() && m_tgxlConn) m_tgxlConn->setTransmitting(true);
    });
    // The radio's own refusal of a relayed command, word for word: a Tune or
    // Operate that did nothing now says why.
    connect(&rm, &RadioModel::geniusCommandRefused, this, [this](const QString& device, const QString& message) {
        if (device == QLatin1String("amp")) {
            m_pgxlPending = false;
            m_pgxlNotice = tr("The radio refused it: %1").arg(message);
            emit pgxlChanged();
        } else {
            m_tgxlNotice = tr("The radio refused it: %1").arg(message);
            emit tgxlChanged();
        }
    });
    connect(&rm.tunerModel(), &TunerModel::relayedCommandRefused, this, [this](const QString&) {
        m_tgxlNotice = tr("The tuner takes Operate and Bypass only through the radio; it is reached by its address only.");
        emit tgxlChanged();
    });
}

// The direct links follow the devices the radio reports (or the addresses
// set in Radio Setup), as the desktop's presence wiring.
void MobileEngine::connect4o3a()
{
    if (!m_pgxlConn) return;
    RadioModel& rm = m_session.radioModel();
    const AppSettings& s = AppSettings::instance();
    const QString pgxlManual = s.value(kPgxlIpKey).toString().trimmed();
    const QString pgxlIp = pgxlManual.isEmpty() ? rm.amplifier().ip() : pgxlManual;
    const bool up = m_connectionState == QLatin1String("connected");
    if (up && (rm.amplifier().present() || !pgxlManual.isEmpty()) && !pgxlIp.isEmpty()) {
        if (!m_pgxlConn->isConnected() && !m_pgxlConn->isConnecting() && !m_pgxlConn->isAuthBlocked()) {
            m_pgxlConn->autoConnectToPgxl(pgxlIp, 9008);
        }
    } else if (m_pgxlConn->isConnected() || m_pgxlConn->isConnecting()) {
        m_pgxlConn->disconnect();
    }
    const QString tgxlManual = s.value(kTgxlIpKey).toString().trimmed();
    const QString tgxlIp = tgxlManual.isEmpty() ? rm.tunerModel().tgxlIp() : tgxlManual;
    if (up && (rm.tunerModel().isPresent() || !tgxlManual.isEmpty()) && !tgxlIp.isEmpty()) {
        if (!m_tgxlConn->isConnected() && !m_tgxlConn->isConnecting() && !m_tgxlConn->isAuthBlocked()) {
            m_tgxlConn->autoConnectToTgxl(tgxlIp, 9010);
        }
    } else if (m_tgxlConn->isConnected() || m_tgxlConn->isConnecting()) {
        m_tgxlConn->disconnect();
    }
    // The Antenna Genius: only at an address set here (the radio does not
    // report it).
    const QString agIp = s.value(kAgIpKey).toString().trimmed();
    if (up && !agIp.isEmpty()) {
        if (!m_ag->isConnected() && !m_ag->isConnecting() && !m_ag->isAuthBlocked()) {
            m_ag->connectToAddress(agIp, 9007);
        }
    } else if (m_ag->isConnected() || m_ag->isConnecting()) {
        m_ag->disconnectFromDevice();
    }
}

void MobileEngine::disconnect4o3a()
{
    if (!m_pgxlConn) return;
    m_pgxlConn->disconnect();
    m_tgxlConn->disconnect();
    m_ag->disconnectFromDevice();
    m_agNotice.clear();
    m_tgxlSo2r = false;
    m_tgxlActive = 0;
    m_tgxlSides.clear();
    emit agChanged();
    m_pgxlPending = false;
    m_pgxlNotice.clear();
    m_tgxlNotice.clear();
    emit pgxlChanged();
    emit tgxlChanged();
}

// The amplifier's forward power and SWR: the radio's relay while it is
// fresh, else the amplifier's own (the desktop applet's arbitration).
double MobileEngine::pgxlForwardW() const
{
    const bool fresh = QDateTime::currentMSecsSinceEpoch() - m_pgxlRelayAt < kRelayFreshMs;
    return fresh ? m_pgxlRelayFwd : (m_pgxlConn && m_pgxlConn->isConnected() ? m_pgxlDirectFwd : 0.0);
}

double MobileEngine::pgxlSwr() const
{
    const bool fresh = QDateTime::currentMSecsSinceEpoch() - m_pgxlRelayAt < kRelayFreshMs;
    return fresh ? m_pgxlRelaySwr : (m_pgxlConn && m_pgxlConn->isConnected() ? m_pgxlDirectSwr : 1.0);
}

bool MobileEngine::pgxlOperating() const
{
    const AmpModel& amp = m_session.radioModel().amplifier();
    return amp.present() && amp.operate();
}

QVariantMap MobileEngine::pgxl() const
{
    const AmpModel& amp = m_session.radioModel().amplifier();
    QVariantMap d;
    const bool direct = m_pgxlConn && m_pgxlConn->isConnected();
    d[QStringLiteral("present")] = amp.present() || direct;
    d[QStringLiteral("radio")] = amp.present();
    d[QStringLiteral("model")] = amp.modelName();
    d[QStringLiteral("ip")] = AppSettings::instance().value(kPgxlIpKey).toString().trimmed().isEmpty()
                                  ? amp.ip() : AppSettings::instance().value(kPgxlIpKey).toString().trimmed();
    d[QStringLiteral("manualIp")] = AppSettings::instance().value(kPgxlIpKey).toString().trimmed();
    d[QStringLiteral("radioIp")] = amp.ip();   // the device's address as the radio reports it
    d[QStringLiteral("direct")] = direct;
    d[QStringLiteral("connecting")] = m_pgxlConn && m_pgxlConn->isConnecting();
    d[QStringLiteral("authBlocked")] = m_pgxlConn && m_pgxlConn->isAuthBlocked();
    d[QStringLiteral("linkError")] = m_pgxlLinkError;
    d[QStringLiteral("control")] = settingOn(kPgxlControlKey);
    d[QStringLiteral("state")] = amp.stateText();
    // Read only directly (the radio does not report it): its own state word
    // says it, so Mode does not show Standby under an OPERATE chip.
    const QString st = amp.stateText();
    d[QStringLiteral("operate")] = amp.present()
        ? amp.operate()
        : (st == QLatin1String("IDLE") || st.startsWith(QLatin1String("TRANSMIT")));
    d[QStringLiteral("pending")] = m_pgxlPending;
    d[QStringLiteral("alert")] = amp.alert();
    d[QStringLiteral("notice")] = m_pgxlNotice;
    const double fwd = pgxlForwardW();
    const double swr = pgxlSwr();
    d[QStringLiteral("fwd")] = fwd;
    d[QStringLiteral("swr")] = swr;
    d[QStringLiteral("refl")] = reflectedWatts(fwd, swr);
    d[QStringLiteral("drive")] = m_pgxlDrive;
    d[QStringLiteral("amps")] = m_pgxlId;
    d[QStringLiteral("temp")] = m_pgxlTemp;
    d[QStringLiteral("hlTemp")] = m_pgxlHlTemp;
    d[QStringLiteral("volts")] = m_pgxlVdd;
    d[QStringLiteral("mains")] = m_pgxlVac;
    d[QStringLiteral("metersLive")] = direct || QDateTime::currentMSecsSinceEpoch() - m_pgxlRelayAt < kRelayFreshMs;
    d[QStringLiteral("fanMode")] = amp.fanMode();
    d[QStringLiteral("canWriteFan")] = direct && amp.canWriteSetup();
    const auto port = [](const AmpPortInfo& p) {
        return QVariantMap{{QStringLiteral("live"), p.live}, {QStringLiteral("band"), p.band},
                           {QStringLiteral("bias"), p.bias}, {QStringLiteral("source"), p.source},
                           {QStringLiteral("ptt"), p.ptt}};
    };
    d[QStringLiteral("portsKnown")] = amp.hasPortInfo();
    d[QStringLiteral("portA")] = port(amp.portA());
    d[QStringLiteral("portB")] = port(amp.portB());
    return d;
}

QVariantMap MobileEngine::ag() const
{
    QVariantMap d;
    if (!m_ag) return d;
    const QString ip = AppSettings::instance().value(kAgIpKey).toString().trimmed();
    const bool direct = m_ag->isConnected();
    d[QStringLiteral("present")] = direct || !ip.isEmpty();
    d[QStringLiteral("direct")] = direct;
    d[QStringLiteral("connecting")] = m_ag->isConnecting();
    d[QStringLiteral("authBlocked")] = m_ag->isAuthBlocked();
    d[QStringLiteral("linkError")] = m_agLinkError;
    d[QStringLiteral("ip")] = ip;
    d[QStringLiteral("manualIp")] = ip;
    d[QStringLiteral("control")] = settingOn(kAgControlKey);
    d[QStringLiteral("notice")] = m_agNotice;
    const AgDeviceInfo& dev = m_ag->connectedDevice();
    d[QStringLiteral("name")] = QString(dev.name).replace(QLatin1Char('_'), QLatin1Char(' '));
    d[QStringLiteral("version")] = dev.version;
    QVariantList ports;
    if (direct) {
        const int count = std::max(1, dev.radioPorts);
        for (int id = 1; id <= std::min(count, 2); ++id) {
            const AgPortStatus p = id == 1 ? m_ag->portA() : m_ag->portB();
            const int band = m_ag->effectiveBand(id);
            QVariantList choices;
            for (const AgAntennaInfo& a : m_ag->antennas()) {
                if (band > 0 && !m_ag->canRxOnBand(a.id, band) && !m_ag->canTxOnBand(a.id, band)) continue;
                choices.append(QVariantMap{{QStringLiteral("id"), a.id},
                                           {QStringLiteral("name"), QString(a.name).replace(QLatin1Char('_'), QLatin1Char(' '))},
                                           {QStringLiteral("tx"), band > 0 && m_ag->canTxOnBand(a.id, band)}});
            }
            ports.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("letter"), id == 1 ? QStringLiteral("A") : QStringLiteral("B")},
                {QStringLiteral("band"), band > 0 ? m_ag->bandName(band) : QString()},
                {QStringLiteral("rx"), p.rxAntenna},
                {QStringLiteral("txAnt"), p.txAntenna},
                {QStringLiteral("rxName"), QString(m_ag->antennaName(p.rxAntenna)).replace(QLatin1Char('_'), QLatin1Char(' '))},
                {QStringLiteral("txName"), QString(m_ag->antennaName(p.txAntenna)).replace(QLatin1Char('_'), QLatin1Char(' '))},
                {QStringLiteral("auto"), p.autoMode},
                {QStringLiteral("transmitting"), p.transmitting},
                {QStringLiteral("inhibited"), p.inhibited},
                {QStringLiteral("choices"), choices}});
        }
    }
    d[QStringLiteral("ports")] = ports;
    return d;
}

void MobileEngine::agSelectAntenna(int port, int antenna)
{
    if (!m_ag || port < 1 || port > 2 || antenna < 1) return;
    const AgPortStatus p = port == 1 ? m_ag->portA() : m_ag->portB();
    if (!m_connected || !settingOn(kAgControlKey)) {
        m_agNotice = tr("Turn on control in Radio Setup › Amp first.");
    } else if (!m_ag->isConnected()) {
        m_agNotice = tr("The Antenna Genius is not connected.");
    } else if (p.transmitting || radioTransmitting()) {
        m_agNotice = tr("Not while the radio transmits.");
    } else {
        m_agNotice.clear();
        qCInfo(lcMobile) << "AG port" << port << "antenna" << antenna;
        m_ag->selectAntenna(port, antenna);
        emit agChanged();
        return;
    }
    qCInfo(lcMobile) << "AG port" << port << "antenna" << antenna << "refused:" << m_agNotice;
    emit agChanged();
}

void MobileEngine::tgxlActivateChannel(int channel)
{
    if (channel < 1 || channel > 2 || !m_tgxlConn || !m_tgxlConn->isConnected() || !m_tgxlSo2r) return;
    if (!geniusCommandAllowed(true, true, channel == 1 ? "channel A" : "channel B")) return;
    m_tgxlConn->sendCommand(QStringLiteral("activate ch=%1").arg(channel));
}

QVariantMap MobileEngine::tgxl() const
{
    TunerModel& t = const_cast<MobileEngine*>(this)->m_session.radioModel().tunerModel();
    QVariantMap d;
    const bool direct = m_tgxlConn && m_tgxlConn->isConnected();
    d[QStringLiteral("present")] = t.isPresent();
    d[QStringLiteral("radio")] = !t.handle().isEmpty();
    d[QStringLiteral("ip")] = AppSettings::instance().value(kTgxlIpKey).toString().trimmed().isEmpty()
                                  ? t.tgxlIp() : AppSettings::instance().value(kTgxlIpKey).toString().trimmed();
    d[QStringLiteral("manualIp")] = AppSettings::instance().value(kTgxlIpKey).toString().trimmed();
    d[QStringLiteral("radioIp")] = t.tgxlIp();
    d[QStringLiteral("direct")] = direct;
    d[QStringLiteral("connecting")] = m_tgxlConn && m_tgxlConn->isConnecting();
    d[QStringLiteral("authBlocked")] = m_tgxlConn && m_tgxlConn->isAuthBlocked();
    d[QStringLiteral("linkError")] = m_tgxlLinkError;
    d[QStringLiteral("control")] = settingOn(kTgxlControlKey);
    d[QStringLiteral("operate")] = t.isOperate();
    d[QStringLiteral("bypass")] = t.isBypass();
    d[QStringLiteral("tuning")] = t.isTuning();
    d[QStringLiteral("relayC1")] = t.relayC1();
    d[QStringLiteral("relayL")] = t.relayL();
    d[QStringLiteral("relayC2")] = t.relayC2();
    d[QStringLiteral("antenna")] = t.antennaA() + 1;  // 1-3, 0 unknown
    d[QStringLiteral("antennaSwitch")] = t.hasAntennaSwitch();
    // SO2R: the active channel and both sides as the tuner reports them.
    d[QStringLiteral("so2r")] = direct && m_tgxlSo2r;
    d[QStringLiteral("active")] = m_tgxlActive;
    d[QStringLiteral("sides")] = m_tgxlSides;
    d[QStringLiteral("fwd")] = t.fwdPower();
    d[QStringLiteral("swr")] = t.swr();
    d[QStringLiteral("alert")] = t.alert();
    d[QStringLiteral("notice")] = m_tgxlNotice;
    return d;
}

bool MobileEngine::geniusCommandAllowed(bool tuner, bool notWhileTransmitting, const char* what)
{
    QString& notice = tuner ? m_tgxlNotice : m_pgxlNotice;
    if (!m_connected || !settingOn(tuner ? kTgxlControlKey : kPgxlControlKey)) {
        notice = tr("Turn on control in Radio Setup › Amp first.");
    } else if (notWhileTransmitting && radioTransmitting()) {
        notice = tr("Not while the radio transmits.");
    } else {
        notice.clear();
        qCInfo(lcMobile) << (tuner ? "TGXL" : "PGXL") << what;
        tuner ? emit tgxlChanged() : emit pgxlChanged();
        return true;
    }
    qCInfo(lcMobile) << (tuner ? "TGXL" : "PGXL") << what << "refused:" << notice;
    tuner ? emit tgxlChanged() : emit pgxlChanged();
    return false;
}

void MobileEngine::pgxlSetOperate(bool on)
{
    AmpModel& amp = m_session.radioModel().amplifier();
    if (!amp.present() || !geniusCommandAllowed(false, false, on ? "operate" : "standby")) return;
    m_pgxlPending = true;
    amp.setOperate(on);
    // The state comes back in the radio's status; a lost answer frees the button.
    QTimer::singleShot(5000, this, [this] {
        if (m_pgxlPending) {
            m_pgxlPending = false;
            emit pgxlChanged();
        }
    });
    emit pgxlChanged();
}

void MobileEngine::pgxlSetFanMode(const QString& mode)
{
    AmpModel& amp = m_session.radioModel().amplifier();
    if (mode != QLatin1String("STANDARD") && mode != QLatin1String("CONTEST") && mode != QLatin1String("BROADCAST")) return;
    if (!amp.canWriteSetup() || !geniusCommandAllowed(false, false, "fan mode")) return;
    amp.setFanMode(mode);
}

void MobileEngine::tgxlSetOperate(bool on)
{
    if (!geniusCommandAllowed(true, false, on ? "operate" : "standby")) return;
    m_session.radioModel().tunerModel().setOperate(on);
}

void MobileEngine::tgxlSetBypass(bool on)
{
    if (!geniusCommandAllowed(true, false, on ? "bypass" : "inline")) return;
    m_session.radioModel().tunerModel().setBypass(on);
}

// Tune, or stop a tune: the tuner keys the radio itself through its
// interlock cable and sets its amplifier's PTT as its utility says.
void MobileEngine::tgxlTune()
{
    TunerModel& t = m_session.radioModel().tunerModel();
    if (t.isTuning()) {
        if (geniusCommandAllowed(true, false, "stop tune")) t.abortTune();
        return;
    }
    if (geniusCommandAllowed(true, false, "autotune")) t.autoTune();
}

void MobileEngine::tgxlAdjustRelay(int relay, int direction)
{
    if (relay < 0 || relay > 2 || (direction != 1 && direction != -1) || !m_tgxlConn->isConnected()) return;
    if (!geniusCommandAllowed(true, true, "relay")) return;
    m_session.radioModel().tunerModel().adjustRelay(relay, direction);
}

void MobileEngine::tgxlSetAntenna(int antenna)
{
    TunerModel& t = m_session.radioModel().tunerModel();
    if (antenna < 1 || antenna > 3 || !t.hasAntennaSwitch() || !m_tgxlConn->isConnected()) return;
    if (!geniusCommandAllowed(true, true, "antenna")) return;
    t.setAntennaA(antenna);  // 1-3, as the TGXL numbers them
}

void MobileEngine::setGeniusSetting(const QString& device, const QString& key, const QVariant& value)
{
    const bool tuner = device == QLatin1String("tgxl");
    AppSettings& s = AppSettings::instance();
    if (device == QLatin1String("ag")) {
        if (key == QLatin1String("control")) {
            s.setValue(kAgControlKey, value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
            m_agNotice.clear();
        } else if (key == QLatin1String("address")) {
            s.setValue(kAgIpKey, value.toString().trimmed());
            m_ag->disconnectFromDevice();
            m_agLinkError.clear();
        } else if (key == QLatin1String("authCode")) {
            const QString code = value.toString().trimmed();
            if (code.isEmpty() || code.size() > 128 || code.contains(QLatin1Char('|'))) return;
            removeSecret(kAgAuthSecret);
            m_ag->disconnectFromDevice();
            m_ag->setAuthCode(code);
        } else {
            return;
        }
        s.save();
        connect4o3a();
        emit agChanged();
        return;
    }
    if (!tuner && device != QLatin1String("pgxl")) return;
    if (key == QLatin1String("control")) {
        s.setValue(tuner ? kTgxlControlKey : kPgxlControlKey, value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
        (tuner ? m_tgxlNotice : m_pgxlNotice).clear();
    } else if (key == QLatin1String("address")) {
        // Empty: the address the radio reports.
        s.setValue(tuner ? kTgxlIpKey : kPgxlIpKey, value.toString().trimmed());
        tuner ? m_tgxlConn->disconnect() : m_pgxlConn->disconnect();
    } else if (key == QLatin1String("authCode")) {
        // Tried at once; kept (secure store) only when the device accepts it.
        const QString code = value.toString().trimmed();
        if (code.isEmpty() || code.size() > 128 || code.contains(QLatin1Char('|'))) return;
        if (tuner) {
            removeSecret(kTgxlAuthSecret);
            m_tgxlConn->disconnect();
            m_tgxlConn->setAuthCode(code);
        } else {
            removeSecret(kPgxlAuthSecret);
            m_pgxlConn->disconnect();
            m_pgxlConn->setAuthCode(code);
        }
    } else {
        return;
    }
    s.save();
    connect4o3a();
    tuner ? emit tgxlChanged() : emit pgxlChanged();
}

// ---- APD, on the transmit model as the desktop uses it: "apd enable=",
// "apd sampler tx_ant= sample_port=", "apd reset". Offered only when the
// radio says it is configurable. ----
QVariantMap MobileEngine::apd() const
{
    const TransmitModel& tx = const_cast<MobileEngine*>(this)->m_session.radioModel().transmitModel();
    QVariantMap d;
    d[QStringLiteral("configurable")] = tx.apdConfigurable();
    d[QStringLiteral("enabled")] = tx.apdEnabled();
    d[QStringLiteral("equalizerActive")] = tx.apdEqualizerActive();
    QVariantList samplers;
    for (const QString& ant : {QStringLiteral("ANT1"), QStringLiteral("ANT2"), QStringLiteral("XVTA"), QStringLiteral("XVTB")}) {
        const TransmitModel::ApdSampler s = tx.apdSampler(ant);
        samplers.append(QVariantMap{{QStringLiteral("ant"), ant}, {QStringLiteral("selected"), s.selected},
                                    {QStringLiteral("available"), s.available}});
    }
    d[QStringLiteral("samplers")] = samplers;
    return d;
}

void MobileEngine::setApdEnabled(bool on)
{
    TransmitModel& tx = m_session.radioModel().transmitModel();
    if (!m_connected || !tx.apdConfigurable()) return;
    qCInfo(lcMobile) << "APD" << (on ? "on" : "off");
    tx.setApdEnabled(on);
}

void MobileEngine::setApdSampler(const QString& txAntenna, const QString& port)
{
    TransmitModel& tx = m_session.radioModel().transmitModel();
    if (!m_connected || !tx.apdConfigurable() || !tx.apdSampler(txAntenna).available.contains(port.toUpper())) return;
    qCInfo(lcMobile) << "APD sampler" << txAntenna << port;
    tx.setApdSamplerPort(txAntenna, port);
}

void MobileEngine::resetApdEqualizer()
{
    TransmitModel& tx = m_session.radioModel().transmitModel();
    if (!m_connected || !tx.apdConfigurable()) return;
    qCInfo(lcMobile) << "APD equalizer reset";
    tx.resetApdEqualizer();
}

void MobileEngine::injectRadioStatus(const QString& line)
{
    // "<object words> key=value ...": the words before the first key=value
    // name the object, as the radio's status lines.
    QStringList objectWords;
    QMap<QString, QString> kvs;
    for (const QString& token : line.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const int eq = token.indexOf(QLatin1Char('='));
        if (eq > 0) kvs.insert(token.left(eq), token.mid(eq + 1));
        else if (kvs.isEmpty()) objectWords << token;
        else kvs.insert(token, QString());
    }
    if (!objectWords.isEmpty()) {
        m_session.radioModel().handleStatusForTest(objectWords.join(QLatin1Char(' ')), kvs);
    }
}

} // namespace AetherSDR::mobile
