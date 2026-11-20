// Elecraft's KPA500 amplifier and KAT500 tuner (roadmap 12: Sergio Landoni
// LU7YS, a FLEX-6600 with both, reached through a Raspberry Pi Pico W that
// gives each device's serial port as raw TCP, one client a port). The core is
// upstream's (Kpa500Connection, Kat500Model, from the devices' own references:
// KPA500 Programmer's Reference Rev. A2, the KAT500 Serial Command
// Reference), over its raw TCP transport; this file puts them on Controls ›
// Amp, the meters and Radio Setup › Amp, as the KPA1500 (MobileEngineKpa.cpp).
//
// In use from the first build (the user, 2026-11-19: not read-only first),
// behind Control in Radio Setup › Amp as every amplifier here: with Control
// off nothing is sent that changes a device (its queries only); with it on,
// the KPA500's Operate / Standby, fault clear and fan minimum, its band
// following the radio's TX frequency (the KPA500 gets no band data from a
// Flex), and the KAT500's antenna, mode, bypass, fault clear and Tune. Each
// asked first on the page (the fan without a question, as the KPA1500's),
// none while the radio transmits, one at a time, done only when the device
// reads it back.
//
// The KAT500's Tune as upstream's KAT500 pre-tune (Kat500TxController): the
// KPA500 to STANDBY first (the carrier is never amplified), the KAT500 set to
// the TX frequency (its memory for it), its full search asked (T), then the
// radio's TUNE carrier at the TX tab's Tune power (the KAT500 tunes with 10 to
// 100 W, 20 W or more for the best match: its product page) until the tuner
// says the tune is over (FT) or reads idle again, 30 s at most; then its
// result read back. The radio's own ATU is bypassed first (two tuners in
// line do not tune), as upstream's pre-tune does; the confirmation says so.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/Kpa500Connection.h"
#include "models/Kat500Model.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>

#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kKpa500HostKey = QStringLiteral("MobileAmpKpa500Host");
const QString kKpa500OnKey = QStringLiteral("MobileAmpKpa500On");
const QString kKpa500ControlKey = QStringLiteral("MobileAmpKpa500Control");
const QString kKat500HostKey = QStringLiteral("MobileAmpKat500Host");
const QString kKat500OnKey = QStringLiteral("MobileAmpKat500On");
const QString kKat500ControlKey = QStringLiteral("MobileAmpKat500Control");
// A KPA500 command is done when the readback shows it, or not seen in this long.
constexpr int kKpa500CommandWaitMs = 3000;
// Its power: off it is silent and asked only every 5 s (Kpa500Connection's
// power check), so On is seen once a check after the wake and the boot gets
// ^ON1, and Off by ^ON0 or by the silence through two checks.
constexpr int kKpa500PowerWaitMs = 15000;
constexpr int kKat500TuneSeconds = 30;
// The KAT500's autotune power range (Elecraft: 10 to 100 W).
constexpr int kKat500TuneMinWatts = 10;
constexpr int kKat500TuneMaxWatts = 100;
constexpr double kKat500TuneStopWatts = 110.0;
constexpr int kKat500TuneKeyWaitMs = 3000;
constexpr int kKat500StandbyWaitMs = 5000;
// A step of the tune not answered in this long ends it.
constexpr int kKat500StepWaitMs = 6000;
// A gap in the tuner's answers this long before "stopped answering" (see
// updateKat500State).
constexpr int kKat500QuietMs = 3000;

bool settingOn(const QString& key)
{
    return AppSettings::instance().value(key).toString() == QLatin1String("1");
}

// "host:port": the bridge's port is the device's own (no default port: a
// raw TCP serial bridge has none of its own).
bool splitHostPort(const QString& spec, QString* host, quint16* port)
{
    const qsizetype colon = spec.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || spec.indexOf(QLatin1Char(':')) != colon) {
        return false;
    }
    bool ok = false;
    const uint p = spec.mid(colon + 1).toUInt(&ok);
    if (!ok || p == 0 || p > 65535) {
        return false;
    }
    *host = spec.left(colon).trimmed();
    *port = quint16(p);
    return !host->isEmpty();
}

// The KPA500's band (^BN 00-10) for a frequency, or -1 outside its bands.
int kpa500BandFor(double mhz)
{
    struct Range {
        double lo;
        double hi;
        int band;
    };
    static const Range ranges[] = {{1.8, 2.0, 0},     {3.5, 4.0, 1},     {5.25, 5.45, 2},   {7.0, 7.3, 3},
                                   {10.1, 10.15, 4},  {14.0, 14.35, 5},  {18.068, 18.168, 6},
                                   {21.0, 21.45, 7},  {24.89, 24.99, 8}, {28.0, 29.7, 9},   {50.0, 54.0, 10}};
    for (const Range& r : ranges) {
        if (mhz >= r.lo && mhz <= r.hi) {
            return r.band;
        }
    }
    return -1;
}

QString kat500ModeName(Kat500::Mode mode)
{
    switch (mode) {
    case Kat500::Mode::Auto:
        return QStringLiteral("AUTO");
    case Kat500::Mode::Manual:
        return QStringLiteral("MANUAL");
    case Kat500::Mode::Bypass:
        return QStringLiteral("BYPASS");
    }
    return {};
}
}  // namespace

void MobileEngine::setupElecraft()
{
    m_kpa500Set = !AppSettings::instance().value(kKpa500HostKey).toString().trimmed().isEmpty();
    m_kat500Set = !AppSettings::instance().value(kKat500HostKey).toString().trimmed().isEmpty();

    // ---- KPA500 ----
    m_kpa500 = new Kpa500Connection(this);
    m_kpa500->setAutoReconnect(true);
    m_kpa500->setFollowTx(false);
    connect(m_kpa500, &Kpa500Connection::connected, this, [this] {
        qCInfo(lcMobile).noquote() << "amp: KPA500 link up at" << m_kpa500Target << "(TCP); waiting for its answer";
        emit kpa500Changed();
    });
    connect(m_kpa500, &Kpa500Connection::respondingChanged, this, [this](bool responding) {
        if (!m_kpa500Wanted) {
            return;
        }
        if (responding) {
            m_kpa500State = QStringLiteral("connected");
            m_kpa500Error.clear();
            qCInfo(lcMobile) << "amp: the KPA500 answers";
        } else {
            m_kpa500State = QStringLiteral("connecting");
            m_kpa500Error = tr("the amplifier does not answer (off, or its bridge not connected to it)");
            // After Off the silence is what is expected; the wait decides it
            // (kKpa500PowerWaitMs). After On the wake is still under way.
            if (!m_kpa500Pending.isEmpty() && !m_kpa500Pending.startsWith(QLatin1String("power "))) {
                kpa500CommandDone(tr("The amplifier stopped answering before it showed the change: it may or may not have done it."), false);
            }
            qCInfo(lcMobile) << "amp: the KPA500 stopped answering";
        }
        emit kpa500Changed();
        emit meterSelectionChanged();
    });
    connect(m_kpa500, &Kpa500Connection::disconnected, this, [this] {
        if (!m_kpa500Wanted) {
            return;  // our own close
        }
        m_kpa500State = QStringLiteral("connecting");
        if (!m_kpa500Pending.isEmpty()) {
            kpa500CommandDone(tr("The link to the amplifier was lost before it showed the change."), false);
        }
        qCInfo(lcMobile) << "amp: KPA500 link lost; trying again every 5 s";
        emit kpa500Changed();
    });
    connect(m_kpa500, &Kpa500Connection::connectionFailed, this, [this](const QString& error) {
        if (!m_kpa500Wanted) {
            return;
        }
        if (m_kpa500State != QLatin1String("failed") || m_kpa500Error != error) {
            qCInfo(lcMobile).noquote() << "amp: KPA500 at" << m_kpa500Target << "not reached:" << error;
        }
        m_kpa500State = QStringLiteral("failed");
        m_kpa500Error = error;
        emit kpa500Changed();
    });
    connect(m_kpa500, &Kpa500Connection::statusUpdated, this, [this] {
        const int fault = m_kpa500->lastStatus().faultCode.value_or(0);
        if (fault != m_kpa500LastFault && m_kpa500->isResponding()) {
            m_kpa500LastFault = fault;
            if (fault != 0) {
                qCInfo(lcMobile).noquote() << "amp: KPA500 fault" << QStringLiteral("%1").arg(fault, 2, 10, QLatin1Char('0'));
            } else {
                qCInfo(lcMobile) << "amp: KPA500 fault cleared";
            }
        }
        checkKpa500Pending();
        checkKat500Tune();
        emit kpa500Changed();
    });
    connect(m_kpa500, &Kpa500Connection::bandTrackingChanged, this, [this](const QString& text) {
        qCInfo(lcMobile).noquote() << "amp: KPA500 band:" << text;
        emit kpa500Changed();
    });
    m_kpa500PendingTimer = new QTimer(this);
    m_kpa500PendingTimer->setSingleShot(true);
    m_kpa500PendingTimer->setInterval(kKpa500CommandWaitMs);
    connect(m_kpa500PendingTimer, &QTimer::timeout, this, [this] {
        if (m_kpa500Pending.isEmpty()) {
            return;
        }
        const bool answersOn = m_kpa500->isResponding() && m_kpa500->lastStatus().powerOn == true;
        if (m_kpa500Pending == QLatin1String("power off") && !answersOn) {
            qCInfo(lcMobile) << "amp: the KPA500 is silent after power off, as it is when off";
            kpa500CommandDone(tr("The amplifier is silent after Off, as the KPA500 is when it is off."), true);
            return;
        }
        qCInfo(lcMobile).noquote() << "amp: the KPA500 has not shown" << m_kpa500Pending << "in"
                                   << m_kpa500PendingTimer->interval() / 1000 << "s";
        kpa500CommandDone(m_kpa500Pending == QLatin1String("clear")
                              ? tr("The fault is still there: deal with its cause first (a temperature fault clears only as the amplifier cools).")
                          : m_kpa500Pending == QLatin1String("power on")
                              ? tr("The amplifier has not answered in 15 s: the wake needs its rear power switch on and the bridge connected to it.")
                          : m_kpa500Pending == QLatin1String("power off")
                              ? tr("The amplifier still answers as on: it did not take Off.")
                              : tr("The amplifier has not shown the change in 3 s: it may not have taken it (a fault, or its front panel in a menu)."),
                          false);
    });
    // The TX band for the KPA500's band following, read twice a second
    // while it is followed (a slice tuned to another band, a split, XIT).
    m_elecraftTxTimer = new QTimer(this);
    m_elecraftTxTimer->setInterval(500);
    connect(m_elecraftTxTimer, &QTimer::timeout, this, &MobileEngine::updateKpa500TxBand);

    // ---- KAT500 ----
    m_kat500 = new Kat500Model(this);
    m_kat500->setAutoReconnect(true);
    connect(m_kat500, &Kat500Model::connected, this, [this] {
        qCInfo(lcMobile).noquote() << "amp: KAT500 link up at" << m_kat500Target << "(TCP); waiting for its answer";
        updateKat500State();
    });
    connect(m_kat500, &Kat500Model::disconnected, this, [this] {
        if (!m_kat500Wanted) {
            return;
        }
        if (!m_kat500Tune.isEmpty()) {
            endKat500Tune(tr("The link to the tuner was lost: the tune carrier stopped."), false);
        }
        if (!m_kat500Pending.isEmpty()) {
            m_kat500Pending.clear();
            m_kat500Result = tr("The link to the tuner was lost before it showed the change.");
            m_kat500ResultOk = false;
        }
        qCInfo(lcMobile) << "amp: KAT500 link lost; trying again every 5 s";
        updateKat500State();
    });
    connect(m_kat500, &Kat500Model::connectionFailed, this, [this](const QString& error) {
        if (!m_kat500Wanted) {
            return;
        }
        if (m_kat500State != QLatin1String("failed") || m_kat500Error != error) {
            qCInfo(lcMobile).noquote() << "amp: KAT500 at" << m_kat500Target << "not reached:" << error;
        }
        m_kat500State = QStringLiteral("failed");
        m_kat500Error = error;
        emit kat500Changed();
    });
    connect(m_kat500, &Kat500Model::statusUpdated, this, [this] {
        const int fault = m_kat500->status().faultCode.value_or(0);
        if (fault != m_kat500LastFault && m_kat500->isResponding()) {
            m_kat500LastFault = fault;
            qCInfo(lcMobile).noquote() << "amp: KAT500" << (fault != 0 ? Kat500::currentFaultName(fault) : QStringLiteral("fault cleared"));
        }
        updateKat500State();
        checkKat500Tune();
        emit kat500Changed();
    });
    // Emitted every 100 ms: only a change of state is passed on.
    connect(m_kat500, &Kat500Model::availabilityChanged, this, &MobileEngine::updateKat500State);
    connect(m_kat500, &Kat500Model::operationFinished, this, [this](const QString& message) {
        if (!m_kat500Tune.isEmpty() && m_kat500TuneOwnsOp) {
            m_kat500TuneOwnsOp = false;
            kat500TuneStepDone(true, message);
            return;
        }
        if (m_kat500Pending.isEmpty()) {
            return;
        }
        qCInfo(lcMobile).noquote() << "amp: the KAT500 shows" << m_kat500Pending;
        m_kat500Pending.clear();
        m_kat500Result.clear();
        m_kat500ResultOk = true;
        emit kat500Changed();
    });
    connect(m_kat500, &Kat500Model::operationFailed, this, [this](const QString& reason) {
        if (!m_kat500Tune.isEmpty() && m_kat500TuneOwnsOp) {
            m_kat500TuneOwnsOp = false;
            kat500TuneStepDone(false, reason);
            return;
        }
        if (m_kat500Pending.isEmpty()) {
            return;
        }
        qCInfo(lcMobile).noquote() << "amp: the KAT500 did not show" << m_kat500Pending << "-" << reason;
        m_kat500Pending.clear();
        m_kat500Result = tr("The tuner did not show the change: %1").arg(reason);
        m_kat500ResultOk = false;
        emit kat500Changed();
    });
    // The tuner's own "tune over" (FT;).
    connect(m_kat500, &Kat500Model::telemetryReceived, this, [this](const QString& command) {
        if (command == QLatin1String("FT") && m_kat500Tune == QLatin1String("carrier")) {
            qCInfo(lcMobile).noquote() << "amp: the KAT500 says its tune is over (FT) at"
                                       << QString::number(m_kat500TuneClock.elapsed() / 1000.0, 'f', 1) << "s";
            m_kat500TuneEndSeen = true;
            checkKat500Tune();
        }
    });
    m_kat500TuneTimer = new QTimer(this);
    m_kat500TuneTimer->setInterval(250);
    connect(m_kat500TuneTimer, &QTimer::timeout, this, &MobileEngine::checkKat500Tune);

    connect(this, &MobileEngine::connectionStateChanged, this, [this] {
        if (!m_connected && !m_kat500Tune.isEmpty()) {
            endKat500Tune(tr("The radio was disconnected: no tune carrier."), false);
        }
    });
    connect(this, &MobileEngine::connectionStateChanged, this, &MobileEngine::updateElecraftLinks);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MobileEngine::updateElecraftLinks);
    connect(this, &MobileEngine::transmittingChanged, this, &MobileEngine::updateKpa500TxBand);
    connect(this, &MobileEngine::tuneStateChanged, this, &MobileEngine::updateKpa500TxBand);
}

// Read while set up and on, the app on screen and connected to a radio, as
// the KPA1500. The bridge takes one client a port: off screen it is free for
// another program (AetherSDR on a PC).
void MobileEngine::updateElecraftLinks()
{
    const Qt::ApplicationState app = qApp->applicationState();
    const bool onScreen = app == Qt::ApplicationActive || app == Qt::ApplicationInactive;

    {
        QString host;
        quint16 port = 0;
        const QString spec = AppSettings::instance().value(kKpa500HostKey).toString().trimmed();
        const bool want = settingOn(kKpa500OnKey) && splitHostPort(spec, &host, &port) && m_connected && onScreen;
        const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
        if (want != m_kpa500Wanted || (want && target != m_kpa500Target)) {
            m_kpa500Wanted = false;
            m_kpa500->disconnect();
            m_kpa500Pending.clear();
            m_kpa500PendingTimer->stop();
            m_kpa500Wanted = want;
            m_kpa500Error.clear();
            m_kpa500LastFault = 0;
            m_kpa500TxBand = -2;
            if (want) {
                m_kpa500State = QStringLiteral("connecting");
                m_kpa500Target = target;
                qCInfo(lcMobile).noquote() << "amp: reading the KPA500 at" << target << "(TCP)"
                                           << (settingOn(kKpa500ControlKey) ? "with control" : "read only");
                m_kpa500->setFollowTx(settingOn(kKpa500ControlKey));
                m_kpa500->connectToHost(host, port);
            } else {
                m_kpa500State = QStringLiteral("off");
                m_kpa500Target.clear();
                qCInfo(lcMobile) << "amp: KPA500 not read";
            }
        }
        // The band follows the TX frequency only with Control on.
        m_kpa500->setFollowTx(want && settingOn(kKpa500ControlKey));
        if (want && settingOn(kKpa500ControlKey)) {
            m_elecraftTxTimer->start();
            updateKpa500TxBand();
        } else {
            m_elecraftTxTimer->stop();
        }
    }
    {
        QString host;
        quint16 port = 0;
        const QString spec = AppSettings::instance().value(kKat500HostKey).toString().trimmed();
        const bool want = settingOn(kKat500OnKey) && splitHostPort(spec, &host, &port) && m_connected && onScreen;
        const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
        if (want != m_kat500Wanted || (want && target != m_kat500Target)) {
            if (!m_kat500Tune.isEmpty()) {
                endKat500Tune(tr("The tuner is no longer read: the tune carrier stopped."), false);
            }
            m_kat500Wanted = false;
            m_kat500->disconnectDevice();
            m_kat500Pending.clear();
            m_kat500Wanted = want;
            m_kat500Error.clear();
            m_kat500LastFault = 0;
            if (want) {
                m_kat500State = QStringLiteral("connecting");
                m_kat500Target = target;
                qCInfo(lcMobile).noquote() << "amp: reading the KAT500 at" << target << "(TCP)"
                                           << (settingOn(kKat500ControlKey) ? "with control" : "read only");
                m_kat500->connectToHost(host, port);
            } else {
                m_kat500State = QStringLiteral("off");
                m_kat500Target.clear();
                qCInfo(lcMobile) << "amp: KAT500 not read";
            }
        }
    }
    emit kpa500Changed();
    emit kat500Changed();
}

// The radio's TX frequency in MHz: its transmit frequency, else the TX
// slice's; 0 when not known.
double MobileEngine::elecraftTxMhz() const
{
    const RadioModel& rm = m_session.radioModel();
    const double tx = rm.transmitModel().transmitFreq();
    if (std::isfinite(tx) && tx > 0.1) {
        return tx;
    }
    for (SliceModel* s : const_cast<RadioModel&>(rm).slices()) {
        if (s && s->isTxSlice()) {
            return s->frequency();
        }
    }
    return 0.0;
}

void MobileEngine::updateKpa500TxBand()
{
    if (!m_kpa500 || !m_kpa500Wanted || !settingOn(kKpa500ControlKey)) {
        return;
    }
    const int band = kpa500BandFor(elecraftTxMhz());
    const bool transmitting = radioTransmitting();
    if (band == m_kpa500TxBand && transmitting == m_kpa500TxTransmitting) {
        return;
    }
    m_kpa500TxBand = band;
    m_kpa500TxTransmitting = transmitting;
    m_kpa500->setTxContext(band, QStringLiteral("frequency"), transmitting);
}

void MobileEngine::setKpa500Address(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitHostPort(spec, &host, &port)) {
        setLastError(tr("KPA500 address: use host:port, the bridge's port for the amplifier, e.g. 192.168.1.194:2000."));
        return;
    }
    AppSettings::instance().setValue(kKpa500HostKey, spec);
    AppSettings::instance().setValue(kKpa500OnKey, spec.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    if (spec.isEmpty()) {
        AppSettings::instance().setValue(kKpa500ControlKey, QStringLiteral("0"));
    }
    AppSettings::instance().save();
    m_kpa500Result.clear();
    const bool setChanged = m_kpa500Set != !spec.isEmpty();
    m_kpa500Set = !spec.isEmpty();
    updateElecraftLinks();
    if (setChanged) {
        emit meterSelectionChanged();
    }
}

void MobileEngine::setKpa500On(bool on)
{
    AppSettings::instance().setValue(kKpa500OnKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    updateElecraftLinks();
}

void MobileEngine::setKpa500Control(bool on)
{
    AppSettings::instance().setValue(kKpa500ControlKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    m_kpa500Result.clear();
    m_kpa500TxBand = -2;
    qCInfo(lcMobile) << "amp: KPA500 control" << (on ? "on: commands confirmed on the Amp tab, band follows TX" : "off: read only");
    updateElecraftLinks();
}

bool MobileEngine::kpa500CommandAllowed(const char* what, bool whileTransmitting)
{
    QString why;
    const Kpa500::Status& s = m_kpa500->lastStatus();
    if (!settingOn(kKpa500ControlKey)) {
        why = tr("Control is off (Radio Setup › Amp).");
    } else if (!m_kpa500->isResponding()) {
        why = tr("The amplifier is not answering.");
    } else if (s.powerOn && !*s.powerOn) {
        why = tr("The amplifier is off.");
    } else if (!whileTransmitting && (radioTransmitting() || s.forwardPowerW.value_or(0.0f) > 0.0f)) {
        why = tr("Not while transmitting: the amplifier must not switch with RF on it.");
    } else if (!m_kpa500Pending.isEmpty() || !m_kat500Tune.isEmpty()) {
        why = tr("Still waiting for the amplifier.");
    }
    if (why.isEmpty()) {
        return true;
    }
    qCInfo(lcMobile).noquote() << "amp: not sending" << what << "to the KPA500 -" << why;
    m_kpa500Result = why;
    m_kpa500ResultOk = false;
    emit kpa500Changed();
    return false;
}

void MobileEngine::kpa500SendPending(const QString& pending)
{
    m_kpa500Pending = pending;
    m_kpa500Result.clear();
    m_kpa500PendingTimer->start(pending.startsWith(QLatin1String("power ")) ? kKpa500PowerWaitMs : kKpa500CommandWaitMs);
    emit kpa500Changed();
}

void MobileEngine::checkKpa500Pending()
{
    if (m_kpa500Pending.isEmpty() || !m_kpa500->isResponding()) {
        return;
    }
    const Kpa500::Status& s = m_kpa500->lastStatus();
    bool done = false;
    if (m_kpa500Pending == QLatin1String("operate") || m_kpa500Pending == QLatin1String("standby")) {
        done = s.operate && *s.operate == (m_kpa500Pending == QLatin1String("operate"));
    } else if (m_kpa500Pending == QLatin1String("clear")) {
        done = s.faultCode && *s.faultCode == 0;
    } else if (m_kpa500Pending.startsWith(QLatin1String("fan minimum "))) {
        done = s.fanMinSpeed && QStringLiteral("fan minimum %1").arg(*s.fanMinSpeed) == m_kpa500Pending;
    } else if (m_kpa500Pending.startsWith(QLatin1String("power "))) {
        done = s.powerOn && *s.powerOn == (m_kpa500Pending == QLatin1String("power on"));
    }
    if (done) {
        qCInfo(lcMobile).noquote() << "amp: the KPA500 shows" << m_kpa500Pending;
        kpa500CommandDone(m_kpa500Pending == QLatin1String("clear") ? tr("The amplifier's fault is cleared.") : QString(), true);
    }
}

void MobileEngine::kpa500CommandDone(const QString& result, bool ok)
{
    m_kpa500Pending.clear();
    m_kpa500PendingTimer->stop();
    m_kpa500Result = result;
    m_kpa500ResultOk = ok;
    emit kpa500Changed();
}

void MobileEngine::kpa500SetOperate(bool operate)
{
    if (!kpa500CommandAllowed(operate ? "OPERATE" : "STANDBY")) {
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA500 for" << (operate ? "OPERATE" : "STANDBY");
    m_kpa500->setOperate(operate);
    kpa500SendPending(operate ? QStringLiteral("operate") : QStringLiteral("standby"));
}

void MobileEngine::kpa500ClearFault()
{
    if (!kpa500CommandAllowed("the fault clear")) {
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA500 to clear its fault";
    m_kpa500->clearFault();
    kpa500SendPending(QStringLiteral("clear"));
}

void MobileEngine::kpa500SetFanMinimum(int speed)
{
    if (!kpa500CommandAllowed("the fan minimum", true)) {
        return;
    }
    if (speed < 0 || speed > 6) {
        m_kpa500Result = tr("The fan's minimum speed is 0 to 6.");
        m_kpa500ResultOk = false;
        emit kpa500Changed();
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA500 for fan minimum speed" << speed;
    m_kpa500->setFanSpeed(speed);
    kpa500SendPending(QStringLiteral("fan minimum %1").arg(speed));
}

// The power as its own power button: Off (^ON0;) while it answers as on, On
// (the bootloader's wake, a lone P) also while it is silent, as it is off.
// Not while transmitting, as the desktop's Kpa500Connection::setPower.
void MobileEngine::kpa500SetPower(bool on)
{
    const char* what = on ? "power on" : "power off";
    const Kpa500::Status& s = m_kpa500->lastStatus();
    const bool answersOn = m_kpa500->isResponding() && s.powerOn == true;
    QString why;
    if (!settingOn(kKpa500ControlKey)) {
        why = tr("Control is off (Radio Setup › Amp).");
    } else if (!m_kpa500Wanted || !m_kpa500->isConnected()) {
        why = tr("The link to the amplifier's bridge is not up.");
    } else if (!on && !answersOn) {
        why = tr("The amplifier is not answering as on.");
    } else if (radioTransmitting() || (m_kpa500->isResponding() && s.forwardPowerW.value_or(0.0f) > 0.0f)) {
        why = tr("Not while transmitting: the amplifier must not switch with RF on it.");
    } else if (!m_kpa500Pending.isEmpty() || !m_kat500Tune.isEmpty()) {
        why = tr("Still waiting for the amplifier.");
    }
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "amp: not sending" << what << "to the KPA500 -" << why;
        m_kpa500Result = why;
        m_kpa500ResultOk = false;
        emit kpa500Changed();
        return;
    }
    if (on && answersOn) {
        return;
    }
    // The connection refuses power while it thinks the radio transmits; its
    // TX state follows the radio's every 500 ms, so bring it up to date.
    updateKpa500TxBand();
    qCInfo(lcMobile) << "amp: asking the KPA500 for" << what;
    m_kpa500->setPower(on);
    kpa500SendPending(QString::fromLatin1(what));
}

bool MobileEngine::kpa500Operating() const
{
    const Kpa500::Status& s = m_kpa500->lastStatus();
    return m_kpa500->isResponding() && s.operate && *s.operate && s.powerOn.value_or(true);
}

double MobileEngine::kpa500ForwardW() const
{
    const Kpa500::Status& s = m_kpa500->lastStatus();
    return m_kpa500->isResponding() && s.forwardPowerW ? *s.forwardPowerW : 0.0;
}

QVariantMap MobileEngine::kpa500() const
{
    const QString spec = AppSettings::instance().value(kKpa500HostKey).toString().trimmed();
    QVariantMap m{{QStringLiteral("set"), !spec.isEmpty()},
                  {QStringLiteral("model"), QStringLiteral("KPA500")},
                  {QStringLiteral("device"), QStringLiteral("Elecraft KPA500")},
                  {QStringLiteral("address"), spec},
                  {QStringLiteral("on"), settingOn(kKpa500OnKey)},
                  {QStringLiteral("control"), settingOn(kKpa500ControlKey)},
                  {QStringLiteral("pending"), m_kpa500Pending},
                  {QStringLiteral("result"), m_kpa500Result},
                  {QStringLiteral("resultOk"), m_kpa500ResultOk},
                  {QStringLiteral("state"), m_kpa500State},
                  {QStringLiteral("error"), m_kpa500Error},
                  {QStringLiteral("target"), m_kpa500Target},
                  // The KAT500's tune holds the amplifier (STANDBY for it).
                  {QStringLiteral("busy"), !m_kat500Tune.isEmpty()},
                  {QStringLiteral("bandTracking"), m_kpa500Wanted ? m_kpa500->bandTrackingText() : QString()},
                  // The bridge's TCP link up: off, the amplifier is silent on it.
                  {QStringLiteral("linkUp"), m_kpa500Wanted && m_kpa500->isConnected()}};
    if (!m_kpa500->isResponding()) {
        return m;
    }
    const Kpa500::Status& s = m_kpa500->lastStatus();
    if (s.powerOn) {
        m.insert(QStringLiteral("power"), *s.powerOn);
    }
    // A reading not in yet is left out (the page shows "–"), never 0.
    if (s.forwardPowerW) {
        m.insert(QStringLiteral("fwd"), *s.forwardPowerW);
    }
    if (s.swr) {
        m.insert(QStringLiteral("swr"), *s.swr);
        // The KPA500 reports forward power and SWR; reflected from them.
        if (s.forwardPowerW && *s.forwardPowerW > 0.0f && *s.swr >= 1.0f) {
            const double g = (double(*s.swr) - 1.0) / (double(*s.swr) + 1.0);
            m.insert(QStringLiteral("refl"), double(*s.forwardPowerW) * g * g);
        }
    }
    if (s.paTemperatureC) {
        m.insert(QStringLiteral("temp"), *s.paTemperatureC);
    }
    if (s.paVoltageV) {
        m.insert(QStringLiteral("volts"), *s.paVoltageV);
    }
    if (s.paCurrentA) {
        m.insert(QStringLiteral("amps"), *s.paCurrentA);
    }
    if (s.band) {
        m.insert(QStringLiteral("band"), Kpa500::bandName(*s.band));
    }
    if (s.powerOn && !*s.powerOn) {
        m.insert(QStringLiteral("operate"), QStringLiteral("POWER OFF"));
    } else if (s.operate) {
        m.insert(QStringLiteral("operate"), *s.operate ? QStringLiteral("OPERATE") : QStringLiteral("STANDBY"));
    }
    if (s.faultCode && *s.faultCode != 0) {
        m.insert(QStringLiteral("fault"), QStringLiteral("%1").arg(*s.faultCode, 2, 10, QLatin1Char('0')));
    }
    if (s.firmwareVersion) {
        m.insert(QStringLiteral("firmware"), *s.firmwareVersion);
    }
    if (s.fanMinSpeed) {
        m.insert(QStringLiteral("fanMin"), *s.fanMinSpeed);
    }
    return m;
}

// ---- KAT500 ----

void MobileEngine::updateKat500State()
{
    if (!m_kat500Wanted) {
        return;
    }
    QString state = m_kat500State;
    if (m_kat500->isConnected() && m_kat500->isDetected() && m_kat500->isResponding()) {
        state = QStringLiteral("connected");
        m_kat500Error.clear();
        m_kat500Quiet.invalidate();
    } else if (m_kat500->isConnected() || m_kat500->isConnecting()) {
        if (state == QLatin1String("connected")) {
            // Off (PS0), the tuner is asked only every 5 s, and a slow bridge
            // (a Pico W on Wi-Fi) can put its answer just past the model's 5 s
            // freshness: 3 s more before it counts as stopped (Sergio's log
            // of build 194 flipped between the two every few seconds).
            if (m_kat500->isConnected() && m_kat500->isDetected()) {
                if (!m_kat500Quiet.isValid()) {
                    m_kat500Quiet.start();
                }
                if (m_kat500Quiet.elapsed() < kKat500QuietMs) {
                    return;
                }
            }
            m_kat500Quiet.invalidate();
            m_kat500Error = tr("the tuner does not answer (its bridge not connected to it, or the tuner unplugged)");
            qCInfo(lcMobile) << "amp: the KAT500 stopped answering";
        }
        state = QStringLiteral("connecting");
    }
    if (state != m_kat500State) {
        if (state == QLatin1String("connected")) {
            qCInfo(lcMobile).noquote() << "amp: the KAT500 answers" << m_kat500->status().firmware;
        }
        m_kat500State = state;
        emit kat500Changed();
        emit meterSelectionChanged();
    }
}

void MobileEngine::setKat500Address(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitHostPort(spec, &host, &port)) {
        setLastError(tr("KAT500 address: use host:port, the bridge's port for the tuner, e.g. 192.168.1.194:2001."));
        return;
    }
    AppSettings::instance().setValue(kKat500HostKey, spec);
    AppSettings::instance().setValue(kKat500OnKey, spec.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    if (spec.isEmpty()) {
        AppSettings::instance().setValue(kKat500ControlKey, QStringLiteral("0"));
    }
    AppSettings::instance().save();
    m_kat500Result.clear();
    const bool setChanged = m_kat500Set != !spec.isEmpty();
    m_kat500Set = !spec.isEmpty();
    updateElecraftLinks();
    if (setChanged) {
        emit meterSelectionChanged();
    }
}

void MobileEngine::setKat500On(bool on)
{
    AppSettings::instance().setValue(kKat500OnKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    updateElecraftLinks();
}

void MobileEngine::setKat500Control(bool on)
{
    AppSettings::instance().setValue(kKat500ControlKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    m_kat500Result.clear();
    if (!on && !m_kat500Tune.isEmpty()) {
        endKat500Tune(tr("Control was turned off: the tune carrier stopped."), false);
    }
    qCInfo(lcMobile) << "amp: KAT500 control" << (on ? "on: commands confirmed on the Amp tab" : "off: read only");
    emit kat500Changed();
}

bool MobileEngine::kat500CommandAllowed(const char* what, bool offOk)
{
    QString why;
    if (!settingOn(kKat500ControlKey)) {
        why = tr("Control is off (Radio Setup › Amp).");
    } else if (m_kat500State != QLatin1String("connected")) {
        why = tr("The tuner is not answering.");
    } else if (!m_kat500->status().powerOn) {
        why = tr("The tuner's power is not read yet.");
    } else if (!offOk && !*m_kat500->status().powerOn) {
        why = tr("The tuner is off: Power › On turns it on.");
    } else if (radioTransmitting()) {
        why = tr("Not while transmitting: the tuner must not switch with RF on it.");
    } else if (!m_kat500Pending.isEmpty() || !m_kat500Tune.isEmpty() || m_kat500->operationBusy()) {
        why = tr("Still waiting for the tuner.");
    } else if (!offOk) {
        why = m_kat500->unavailableReason(true);
    }
    if (why.isEmpty()) {
        return true;
    }
    qCInfo(lcMobile).noquote() << "amp: not sending" << what << "to the KAT500 -" << why;
    m_kat500Result = why;
    m_kat500ResultOk = false;
    emit kat500Changed();
    return false;
}

void MobileEngine::kat500Sent(bool accepted, const QString& pending)
{
    if (!accepted) {
        // Refused before it went (the model's own reason when it has one).
        const QString why = m_kat500->unavailableReason(true);
        m_kat500Result = why.isEmpty() ? tr("The tuner cannot take that now.") : why;
        m_kat500ResultOk = false;
        qCInfo(lcMobile).noquote() << "amp: the KAT500 did not take" << pending << "-" << m_kat500Result;
        emit kat500Changed();
        return;
    }
    qCInfo(lcMobile).noquote() << "amp: asking the KAT500 for" << pending;
    m_kat500Pending = pending;
    m_kat500Result.clear();
    emit kat500Changed();
}

void MobileEngine::kat500SelectAntenna(int antenna)
{
    if (!kat500CommandAllowed("an antenna")) {
        return;
    }
    if (antenna < 1 || antenna > 3) {
        return;
    }
    // One the tuner has not enabled on this band (its own setting) is not asked.
    const Kat500::Status& st = m_kat500->status();
    if (st.band && m_kat500->configuration().value(Kat500::SettingKey{Kat500::Setting::EnabledAntenna, *st.band, antenna}, -1.0) == 0.0) {
        m_kat500Result = tr("ANT %1 is not enabled on this band in the tuner.").arg(antenna);
        m_kat500ResultOk = false;
        qCInfo(lcMobile).noquote() << "amp: not sending ANT" << antenna << "to the KAT500 - not enabled on this band";
        emit kat500Changed();
        return;
    }
    kat500Sent(m_kat500->setAntenna(antenna), QStringLiteral("ANT %1").arg(antenna));
}

void MobileEngine::kat500SetMode(const QString& mode)
{
    if (!kat500CommandAllowed("a mode")) {
        return;
    }
    const Kat500::Mode m = mode == QLatin1String("manual") ? Kat500::Mode::Manual
                         : mode == QLatin1String("bypass") ? Kat500::Mode::Bypass : Kat500::Mode::Auto;
    kat500Sent(m_kat500->setMode(m), QStringLiteral("mode %1").arg(kat500ModeName(m)));
}

void MobileEngine::kat500SetBypass(bool bypassed)
{
    if (!kat500CommandAllowed("bypass")) {
        return;
    }
    kat500Sent(m_kat500->setBypassed(bypassed), bypassed ? QStringLiteral("bypass on") : QStringLiteral("bypass off"));
}

void MobileEngine::kat500ClearFault()
{
    if (!kat500CommandAllowed("the fault clear")) {
        return;
    }
    kat500Sent(m_kat500->clearFault(), QStringLiteral("the fault cleared"));
}

void MobileEngine::kat500SetPower(bool on)
{
    // Off, the tuner answers only its identity and power; its readings and
    // every other command come back once it is on. The tuner itself
    // refuses a power change while transmitting, as this does before it.
    if (!kat500CommandAllowed(on ? "power on" : "power off", true)) {
        return;
    }
    if (m_kat500->status().powerOn == on) {
        return;
    }
    const QString pending = on ? QStringLiteral("power on") : QStringLiteral("power off");
    if (!m_kat500->setPower(on)) {
        // Refused before it went: an earlier answer still awaited (the
        // model's reasons all assume the tuner on, so not those).
        m_kat500Result = tr("The tuner cannot take that now (an earlier answer still awaited): try again in a moment.");
        m_kat500ResultOk = false;
        qCInfo(lcMobile).noquote() << "amp: the KAT500 did not take" << pending << "- an earlier answer awaited";
        emit kat500Changed();
        return;
    }
    kat500Sent(true, pending);
}

// ---- KAT500 Tune ----

void MobileEngine::kat500Tune()
{
    if (!kat500CommandAllowed("the tune")) {
        return;
    }
    const int watts = tunePower();
    const Kat500::Status& st = m_kat500->status();
    const int khz = int(std::llround(elecraftTxMhz() * 1000.0));
    QString why;
    if (!m_ampTune.isEmpty() || !m_kpaTune.isEmpty()) {
        why = tr("Another amplifier's tune is running.");
    } else if (!st.mode || *st.mode == Kat500::Mode::Bypass) {
        why = tr("The tuner is in BYPASS mode: choose Auto or Manual first.");
    } else if (st.faultCode.value_or(0) != 0) {
        why = tr("The tuner shows a fault: clear it first.");
    } else if (!Kat500::isSupportedFrequency(khz)) {
        why = tr("The KAT500 tunes from 1.8 to 54 MHz.");
    } else if (!tuneAvailable()) {
        why = tr("The radio cannot send a tune carrier now.");
    } else if (watts < kKat500TuneMinWatts || watts > kKat500TuneMaxWatts) {
        why = tr("Tune power is %1 W: the KAT500 tunes with 10 to 100 W (20 W or more matches best). Set Tune power on the TX tab.").arg(watts);
    } else if (txLocked()) {
        why = tr("Transmit is locked (SB): hold PTT, KEY, TUNE or CQ for a second to arm it.");
    }
    // The carrier is never amplified: a KPA500 in OPERATE goes to STANDBY
    // first, which needs its Control.
    const bool kpaOperate = m_kpa500Wanted && m_kpa500->isResponding() && m_kpa500->lastStatus().operate.value_or(false);
    if (why.isEmpty() && kpaOperate && !settingOn(kKpa500ControlKey)) {
        why = tr("The KPA500 is in OPERATE and its Control is off: put it in STANDBY first.");
    }
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "amp: no KAT500 tune -" << why;
        m_kat500Result = why;
        m_kat500ResultOk = false;
        emit kat500Changed();
        return;
    }
    m_kat500Result.clear();
    m_kat500TuneKhz = khz;
    m_kat500TuneWasOperate = false;
    m_kat500TuneKeyed = false;
    m_kat500TuneEndSeen = false;
    m_kat500TuneSawTuning = false;
    m_kat500TuneOwnsOp = false;
    m_kat500TuneLeftShown = -1;
    m_kat500TuneClock.start();
    m_kat500TuneStepClock.start();
    m_kat500TuneTimer->start();
    if (kpaOperate) {
        m_kat500TuneWasOperate = true;
        m_kat500Tune = QStringLiteral("standby");
        qCInfo(lcMobile) << "amp: KAT500 tune: asking the KPA500 for STANDBY first";
        m_kpa500->setOperate(false);
        emit kat500Changed();
        emit kpa500Changed();
        return;
    }
    kat500TuneAtu();
}

// The radio's own ATU in line (upstream's Kat500TxController::internalBypassed).
bool MobileEngine::radioAtuInLine() const
{
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    const ATUStatus st = tm.atuStatus();
    return tm.hasTuner() && st != ATUStatus::Bypass && st != ATUStatus::ManualBypass && st != ATUStatus::FailBypass;
}

void MobileEngine::kat500TuneAtu()
{
    if (!radioAtuInLine()) {
        kat500TuneRecall();
        return;
    }
    m_kat500Tune = QStringLiteral("atu");
    m_kat500TuneStepClock.start();
    qCInfo(lcMobile) << "amp: KAT500 tune: bypassing the radio's own ATU first";
    m_atuInput = m_session.radioModel().localTxController()->capture(TxController::Activity::Atu);
    if (!m_atuInput.bypassAtu()) {
        endKat500Tune(tr("The radio did not accept its ATU bypass: no tune carrier was sent."), false);
        return;
    }
    emit kat500Changed();
}

void MobileEngine::kat500TuneRecall()
{
    m_kat500Tune = QStringLiteral("frequency");
    m_kat500TuneStepClock.start();
    qCInfo(lcMobile) << "amp: KAT500 tune: the tuner to" << m_kat500TuneKhz << "kHz";
    m_kat500TuneOwnsOp = true;
    if (!m_kat500->recallFrequency(m_kat500TuneKhz)) {
        m_kat500TuneOwnsOp = false;
        endKat500Tune(tr("The tuner did not take the frequency."), false);
        return;
    }
    emit kat500Changed();
    emit kpa500Changed();
}

// The tuner finished (or failed) the step the tune asked of it.
void MobileEngine::kat500TuneStepDone(bool ok, const QString& message)
{
    if (m_kat500Tune == QLatin1String("frequency")) {
        if (!ok) {
            endKat500Tune(tr("The tuner did not take the frequency: %1").arg(message), false);
            return;
        }
        // The search first, then the carrier it waits for.
        m_kat500Tune = QStringLiteral("search");
        m_kat500TuneStepClock.start();
        qCInfo(lcMobile) << "amp: KAT500 tune: asking for a full search";
        m_kat500TuneOwnsOp = true;
        if (!m_kat500->startTune(true)) {
            m_kat500TuneOwnsOp = false;
            endKat500Tune(tr("The tuner did not take the tune request."), false);
            return;
        }
        emit kat500Changed();
    } else if (m_kat500Tune == QLatin1String("search")) {
        if (!ok || m_kat500->status().tuning != true) {
            endKat500Tune(ok ? tr("The tuner did not start its search.") : tr("The tuner did not start its search: %1").arg(message), false);
            return;
        }
        startKat500TuneCarrier();
    } else if (m_kat500Tune == QLatin1String("result")) {
        const Kat500::Status& s = m_kat500->status();
        if (!ok) {
            endKat500Tune(tr("The tune is over, but its result was not read: %1").arg(message), false);
            return;
        }
        if (s.faultCode.value_or(0) != 0) {
            endKat500Tune(tr("The tuner: %1.").arg(Kat500::currentFaultName(*s.faultCode)), false);
            return;
        }
        QString result = s.swr ? tr("The tuner tuned (SWR %1).").arg(*s.swr, 0, 'f', 2) : tr("The tuner tuned.");
        if (s.bypassed.value_or(false)) {
            result += QLatin1Char(' ') + tr("The antenna needs no match here: the tuner bypasses itself.");
        }
        if (m_kat500TuneWasOperate) {
            result += QLatin1Char(' ') + tr("Put the KPA500 in OPERATE when you are ready.");
        }
        endKat500Tune(result, true);
    }
}

void MobileEngine::startKat500TuneCarrier()
{
    if (radioTransmitting() || !tuneAvailable()) {
        endKat500Tune(tr("The radio cannot send a tune carrier now."), false);
        return;
    }
    RadioModel& rm = m_session.radioModel();
    m_tuneInput = rm.localTxController()->capture(TxController::Activity::Tune);
    if (!m_tuneInput.start()) {
        m_tuneInput.stop();
        endKat500Tune(tr("The radio did not accept the tune request."), false);
        return;
    }
    m_kat500Tune = QStringLiteral("carrier");
    m_kat500TuneStepClock.start();
    m_kat500TuneKeyed = false;
    qCInfo(lcMobile).noquote() << "amp: KAT500 tune carrier keyed at" << tunePower() << "W for up to" << kKat500TuneSeconds << "s";
    emit kat500Changed();
}

// Every 250 ms and with every reading while a tune runs.
void MobileEngine::checkKat500Tune()
{
    if (m_kat500Tune.isEmpty()) {
        return;
    }
    if (m_kat500Tune == QLatin1String("standby")) {
        const Kpa500::Status& k = m_kpa500->lastStatus();
        if (k.operate && !*k.operate) {
            qCInfo(lcMobile) << "amp: KAT500 tune: the KPA500 is in STANDBY";
            kat500TuneAtu();
        } else if (m_kat500TuneStepClock.elapsed() > kKat500StandbyWaitMs) {
            endKat500Tune(tr("The KPA500 did not go to STANDBY: no tune carrier was sent."), false);
        }
        return;
    }
    if (m_kat500Tune == QLatin1String("atu")) {
        if (!radioAtuInLine()) {
            qCInfo(lcMobile) << "amp: KAT500 tune: the radio's ATU is bypassed";
            kat500TuneRecall();
        } else if (m_kat500TuneStepClock.elapsed() > kKat500StandbyWaitMs) {
            endKat500Tune(tr("The radio's own ATU did not go to bypass: no tune carrier was sent."), false);
        }
        return;
    }
    if (m_kat500Tune == QLatin1String("frequency") || m_kat500Tune == QLatin1String("search")
        || m_kat500Tune == QLatin1String("result")) {
        if (m_kat500TuneStepClock.elapsed() > kKat500StepWaitMs) {
            endKat500Tune(tr("The tuner did not answer the tune in time."), false);
        }
        return;
    }
    // The carrier.
    const qint64 ms = m_kat500TuneClock.elapsed();
    const bool tuning = m_session.radioModel().transmitModel().isTuning();
    if (tuning) {
        m_kat500TuneKeyed = true;
    }
    const Kat500::Status& s = m_kat500->status();
    if (s.tuning == true) {
        if (!m_kat500TuneSawTuning) {
            qCInfo(lcMobile) << "amp: the KAT500 is tuning";
        }
        m_kat500TuneSawTuning = true;
    }
    if (m_kat500TuneKeyed && !tuning) {
        endKat500Tune(tr("The tune carrier stopped."), false);
        return;
    }
    if (!m_kat500TuneKeyed && m_kat500TuneStepClock.elapsed() > kKat500TuneKeyWaitMs) {
        endKat500Tune(tr("The radio did not start the tune carrier."), false);
        return;
    }
    if (m_kat500TuneKeyed && m_forwardPowerW > kKat500TuneStopWatts) {
        endKat500Tune(tr("The radio sent %1 W, more than the tuner tunes with (10 to 100 W): the tune carrier stopped.")
                          .arg(qRound(m_forwardPowerW)), false);
        return;
    }
    // Never amplified.
    if (m_kpa500Wanted && m_kpa500->isResponding() && m_kpa500->lastStatus().operate.value_or(false)) {
        endKat500Tune(tr("The KPA500 went to OPERATE: the tune carrier stopped."), false);
        return;
    }
    if (s.faultCode.value_or(0) != 0) {
        endKat500Tune(tr("The tuner: %1.").arg(Kat500::currentFaultName(*s.faultCode)), false);
        return;
    }
    // Done: its FT;, or idle again after it was seen tuning.
    if (m_kat500TuneKeyed && (m_kat500TuneEndSeen || (m_kat500TuneSawTuning && s.tuning == false))) {
        if (m_tuneInput.valid() && m_session.radioModel().transmitModel().isTuning()) {
            m_tuneInput.stop();
            qCInfo(lcMobile) << "tune stopped: the KAT500's tune is over";
        }
        m_kat500Tune = QStringLiteral("result");
        m_kat500TuneStepClock.start();
        m_kat500TuneOwnsOp = true;
        if (!m_kat500->readMatchStatus()) {
            m_kat500TuneOwnsOp = false;
            endKat500Tune(tr("The tune is over, but its result could not be read."), false);
            return;
        }
        emit kat500Changed();
        return;
    }
    if (ms >= kKat500TuneSeconds * 1000) {
        endKat500Tune(m_kat500TuneSawTuning ? tr("The tuner was still tuning after 30 s: the tune stopped.")
                                            : tr("No tune seen in 30 s: the tune stopped."), false);
        return;
    }
    const int left = int((kKat500TuneSeconds * 1000 - ms + 999) / 1000);
    if (left != m_kat500TuneLeftShown) {
        m_kat500TuneLeftShown = left;
        emit kat500Changed();
    }
}

void MobileEngine::endKat500Tune(const QString& result, bool ok)
{
    const QString phase = std::exchange(m_kat500Tune, QString());
    m_kat500TuneTimer->stop();
    m_kat500TuneLeftShown = -1;
    if (m_tuneInput.valid() && m_session.radioModel().transmitModel().isTuning()) {
        m_tuneInput.stop();
        qCInfo(lcMobile) << "tune stopped: the KAT500's tune ended";
    }
    // A search not seen over is cancelled (CT), after the carrier is off.
    if (!ok && (phase == QLatin1String("search") || phase == QLatin1String("carrier")) && m_kat500->isConnected()) {
        m_kat500TuneOwnsOp = false;
        if (m_kat500->operationBusy()) {
            m_kat500->cancelTuneAfterPendingOperation();
        } else if (!m_kat500->cancelTune()) {
            m_kat500->cancelTuneAfterPendingOperation();
        }
    } else if (m_kat500TuneOwnsOp) {
        m_kat500TuneOwnsOp = false;
        m_kat500->cancelOperation();
    }
    m_kat500Result = result;
    m_kat500ResultOk = ok;
    qCInfo(lcMobile).noquote() << "amp: KAT500 tune ended:" << result;
    emit kat500Changed();
    emit kpa500Changed();
}

void MobileEngine::kat500TuneStop()
{
    if (!m_kat500Tune.isEmpty()) {
        endKat500Tune(tr("Stopped."), false);
    }
}

QVariantMap MobileEngine::kat500() const
{
    const QString spec = AppSettings::instance().value(kKat500HostKey).toString().trimmed();
    QVariantMap m{{QStringLiteral("set"), !spec.isEmpty()},
                  {QStringLiteral("model"), QStringLiteral("KAT500")},
                  {QStringLiteral("device"), QStringLiteral("Elecraft KAT500")},
                  {QStringLiteral("address"), spec},
                  {QStringLiteral("on"), settingOn(kKat500OnKey)},
                  {QStringLiteral("control"), settingOn(kKat500ControlKey)},
                  {QStringLiteral("pending"), m_kat500Pending},
                  {QStringLiteral("result"), m_kat500Result},
                  {QStringLiteral("resultOk"), m_kat500ResultOk},
                  {QStringLiteral("state"), m_kat500State},
                  {QStringLiteral("error"), m_kat500Error},
                  {QStringLiteral("target"), m_kat500Target},
                  {QStringLiteral("tune"), m_kat500Tune},
                  {QStringLiteral("tuneLeft"), m_kat500Tune == QLatin1String("carrier")
                       ? std::max<qint64>(0, (kKat500TuneSeconds * 1000 - m_kat500TuneClock.elapsed() + 999) / 1000) : kKat500TuneSeconds},
                  {QStringLiteral("tunePower"), tunePower()},
                  {QStringLiteral("busy"), m_kat500->operationBusy()}};
    if (m_kat500State != QLatin1String("connected")) {
        return m;
    }
    const Kat500::Status& s = m_kat500->status();
    if (!s.firmware.isEmpty()) {
        m.insert(QStringLiteral("firmware"), s.firmware);
    }
    if (s.powerOn) {
        m.insert(QStringLiteral("power"), *s.powerOn);
    }
    // Off (PS0) it answers only its identity and power: what it read before
    // it was turned off is not shown (nor on the meters) as if it were now.
    if (s.powerOn == false) {
        return m;
    }
    if (s.mode) {
        m.insert(QStringLiteral("mode"), kat500ModeName(*s.mode));
    }
    if (s.antenna) {
        m.insert(QStringLiteral("antenna"), *s.antenna);
    }
    if (s.bypassed) {
        m.insert(QStringLiteral("bypassed"), *s.bypassed);
    }
    if (s.tuning) {
        m.insert(QStringLiteral("tuning"), *s.tuning);
    }
    if (s.swr) {
        m.insert(QStringLiteral("swr"), *s.swr);
    }
    if (s.bypassSwr) {
        m.insert(QStringLiteral("bypassSwr"), *s.bypassSwr);
    }
    if (s.band) {
        m.insert(QStringLiteral("band"), Kat500::bandName(*s.band));
    }
    if (s.frequencyKhz && *s.frequencyKhz > 0) {
        m.insert(QStringLiteral("freq"), *s.frequencyKhz);
    }
    if (s.ampKeyInterrupted) {
        m.insert(QStringLiteral("ampKey"), *s.ampKeyInterrupted);
    }
    if (s.faultCode && *s.faultCode != 0) {
        m.insert(QStringLiteral("fault"), Kat500::currentFaultName(*s.faultCode));
    }
    // The antennas enabled on this band (the tuner's own setting), once read.
    if (s.band) {
        QVariantList enabled;
        for (int a = 1; a <= 3; ++a) {
            const double v = m_kat500->configuration().value(Kat500::SettingKey{Kat500::Setting::EnabledAntenna, *s.band, a}, -1.0);
            enabled << (v < 0 ? QVariant() : QVariant(v != 0.0));
        }
        m.insert(QStringLiteral("antennas"), enabled);
    }
    return m;
}

bool MobileEngine::kat500Answering() const
{
    return m_kat500State == QLatin1String("connected");
}

}  // namespace AetherSDR::mobile
