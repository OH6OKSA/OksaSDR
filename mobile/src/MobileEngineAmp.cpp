// An RF-KIT RF2K-S amplifier over its own REST API (Sebastian Schu DG5CST
// asked for his; others for the FlexRadio amplifier and tuner, which come
// later). Step 1 was readings only, checked against his amplifier's own
// display (build 148). Step 2 adds Operate / Standby, the internal antenna
// and the error reset, as upstream's applet (#6596): off by default (the
// Control switch in Radio Setup › Amp), every command confirmed on the page
// first, one at a time, never while the radio transmits (the amplifier's
// relays must not switch with RF on them), and never latched: the page shows
// only what the amplifier reads back. With control off, RfkitConnection runs
// read-only, so only GET requests ever reach the amplifier.
//
// The amplifier is a device on the operator's network, not behind the
// radio: the phone reaches it directly (same network or a VPN), never over
// SmartLink. It is read while the app is on screen and connected to a
// radio: every half second in receive, every 250 ms (the desktop's rate)
// while the radio transmits, so the power readings follow the keying.
//
// The tuner: the API reads it but cannot start a tune (upstream's applet
// shows it read only; a community-modified server with a tune command is
// not the vendor's API). The RF2K-S tunes in STANDBY on a 4-39 W carrier
// when its own Tune is pressed (manual 06/25, 7.2-7.3), so owners open its
// screen over VNC while the radio sends a carrier, which on one phone
// stopped as the app left the screen (Jeff Stinger, build 149). ampTune
// asks for STANDBY, then keys the radio's TUNE and keeps it while the app
// is away, for 20 s at most, stopping once the amplifier reports a tune
// done, the amplifier in OPERATE, more than 39 W from the radio, or the
// link to either lost.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/RfkitConnection.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

#include <QGuiApplication>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kHostKey = QStringLiteral("MobileAmpRf2kHost");
const QString kOnKey = QStringLiteral("MobileAmpRf2kOn");
const QString kControlKey = QStringLiteral("MobileAmpRf2kControl");
constexpr int kPollMs = 500;
constexpr int kPollTxMs = 250;
constexpr int kTuneSeconds = 20;
constexpr int kTuneMinWatts = 4;
constexpr int kTuneMaxWatts = 39;
// The radio's forward power may read a little over the setting; past this
// the carrier is not one the amplifier tunes with.
constexpr double kTuneStopWatts = 45.0;
constexpr int kTuneStandbyWaitMs = 5000;
constexpr int kTuneKeyWaitMs = 3000;

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

QString antennaStateName(Rfkit::AntennaState state)
{
    switch (state) {
    case Rfkit::AntennaState::Active:
        return QStringLiteral("ACTIVE");
    case Rfkit::AntennaState::Available:
        return QStringLiteral("AVAILABLE");
    default:
        return QStringLiteral("DISABLED");
    }
}

bool controlOn()
{
    return AppSettings::instance().value(kControlKey).toString() == QLatin1String("1");
}
}  // namespace

void MobileEngine::setupAmp()
{
    m_amp = new RfkitConnection(this);
    m_amp->setReadOnly(!controlOn());
    m_amp->setPollInterval(kPollMs);
    m_ampSet = !AppSettings::instance().value(kHostKey).toString().trimmed().isEmpty();
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
        if (!m_ampPending.isEmpty()) {
            ampCommandDone(tr("The link was lost before the amplifier answered: it may or may not have done it."), false);
        }
        if (!m_ampTune.isEmpty()) {
            endAmpTune(tr("The link to the amplifier was lost: the tune carrier stopped."), false);
        }
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
        if (!m_ampPending.isEmpty()) {
            ampCommandDone(tr("The link was lost before the amplifier answered: it may or may not have done it."), false);
        }
        if (!m_ampTune.isEmpty()) {
            endAmpTune(tr("The link to the amplifier was lost: the tune carrier stopped."), false);
        }
        emit ampChanged();
    });
    connect(m_amp, &RfkitConnection::statusUpdated, this, [this](const Rfkit::Status& status) {
        m_ampStatus = status;
        emit ampChanged();
        checkAmpTune();
    });
    connect(m_amp, &RfkitConnection::commandFinished, this,
            [this](const QString& path, bool accepted, bool answered, const QString& reason) {
        if (m_ampPending.isEmpty()) {
            return;  // from a session this page no longer waits on
        }
        if (accepted) {
            qCInfo(lcMobile).noquote() << "amp: the RF2K-S accepted" << m_ampPending << "(" + path + ")";
            // Operate and the antenna show in the readback; a reset may
            // change nothing visible, so it says so.
            ampCommandDone(path == QLatin1String(Rfkit::kPathErrorReset)
                               ? tr("The amplifier accepted the reset.") : QString(), true);
        } else if (answered) {
            qCInfo(lcMobile).noquote() << "amp: the RF2K-S did not take" << m_ampPending << "(" + path + "):" << reason;
            ampCommandDone(tr("The amplifier did not take it: %1").arg(reason), false);
        } else {
            // Lost on the way back, or on the way there: whether it acted
            // is unknown until it is read again.
            qCInfo(lcMobile).noquote() << "amp: no answer from the RF2K-S to" << m_ampPending << "(" + path + "):" << reason;
            ampCommandDone(tr("No answer from the amplifier (%1): it may or may not have done it. The tab shows its state once it is read again.").arg(reason), false);
        }
        // The tune's STANDBY refused or unanswered: no carrier.
        if (!accepted && m_ampTune == QLatin1String("standby")) {
            endAmpTune(m_ampResult + QLatin1Char(' ') + tr("No tune carrier was sent."), false);
        }
    });
    m_ampTuneTimer = new QTimer(this);
    m_ampTuneTimer->setInterval(250);
    connect(m_ampTuneTimer, &QTimer::timeout, this, &MobileEngine::checkAmpTune);
    connect(this, &MobileEngine::connectionStateChanged, this, [this] {
        if (!m_connected && !m_ampTune.isEmpty()) {
            endAmpTune(tr("The radio was disconnected: no tune carrier."), false);
        }
    });
    connect(this, &MobileEngine::connectionStateChanged, this, &MobileEngine::updateAmpLink);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MobileEngine::updateAmpLink);
    connect(this, &MobileEngine::txMetersChanged, this, &MobileEngine::updateAmpPollRate);
    connect(this, &MobileEngine::tuneStateChanged, this, &MobileEngine::updateAmpPollRate);
    connect(this, &MobileEngine::transmittingChanged, this, &MobileEngine::updateAmpPollRate);
}

// Faster while the radio transmits, so forward power and SWR follow the
// keying; the connection lowers the rate itself if the amplifier is slower.
void MobileEngine::updateAmpPollRate()
{
    const bool fast = radioTransmitting();
    if (fast == m_ampFastPoll) {
        return;
    }
    m_ampFastPoll = fast;
    m_amp->setPollInterval(fast ? kPollTxMs : kPollMs);
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
    // Kept while the tune carrier runs with the app away: the amplifier's
    // readback is what stops it once tuned.
    const bool onScreen = app == Qt::ApplicationActive || app == Qt::ApplicationInactive || !m_ampTune.isEmpty();
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
    m_ampPending.clear();
    // The last result stays: back from the amplifier's own screen, the tab
    // still says how the tune ended.
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
    m_ampResult.clear();
    // A new address is meant to be read; forgetting it turns it off, and
    // control with it (a new amplifier starts read only).
    AppSettings::instance().setValue(kOnKey, spec.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    if (spec.isEmpty()) {
        AppSettings::instance().setValue(kControlKey, QStringLiteral("0"));
        m_amp->setReadOnly(true);
    }
    AppSettings::instance().save();
    // The meters offer the amplifier's readings only while one is set up:
    // known before ampChanged, which makes the meter chooser ask again.
    const bool setChanged = m_ampSet != !spec.isEmpty();
    m_ampSet = !spec.isEmpty();
    updateAmpLink();
    emit ampChanged();
    if (setChanged) {
        emit meterSelectionChanged();
    }
}

void MobileEngine::setAmpControl(bool on)
{
    AppSettings::instance().setValue(kControlKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    m_amp->setReadOnly(!on);
    m_ampResult.clear();
    qCInfo(lcMobile) << "amp: control" << (on ? "on: commands confirmed on the Amp tab" : "off: read only");
    emit ampChanged();
}

// One command at a time, only with control on, a live link and the radio
// in receive. A refusal says why on the Amp tab.
bool MobileEngine::ampCommandAllowed(const char* what)
{
    QString why;
    if (!controlOn()) {
        why = tr("Control is off (Radio Setup › Amp).");
    } else if (!m_amp->isConnected() || m_ampState != QLatin1String("connected")) {
        why = tr("Not connected to the amplifier.");
    } else if (radioTransmitting()) {
        why = tr("Not while transmitting: the amplifier's relays must not switch with RF on them.");
    } else if (!m_ampPending.isEmpty()) {
        why = tr("Still waiting for the amplifier's answer.");
    }
    if (why.isEmpty()) {
        return true;
    }
    qCInfo(lcMobile).noquote() << "amp: not sending" << what << "-" << why;
    m_ampResult = why;
    m_ampResultOk = false;
    emit ampChanged();
    return false;
}

void MobileEngine::ampCommandDone(const QString& result, bool ok)
{
    m_ampPending.clear();
    m_ampResult = result;
    m_ampResultOk = ok;
    emit ampChanged();
}

void MobileEngine::ampSetOperate(bool operate)
{
    if (!ampCommandAllowed(operate ? "OPERATE" : "STANDBY")) {
        return;
    }
    m_ampPending = operate ? QStringLiteral("operate") : QStringLiteral("standby");
    m_ampResult.clear();
    qCInfo(lcMobile) << "amp: asking the RF2K-S for" << (operate ? "OPERATE" : "STANDBY");
    m_amp->setOperate(operate);
    emit ampChanged();
}

void MobileEngine::ampSelectAntenna(int number)
{
    const QString what = QStringLiteral("antenna %1").arg(number);
    if (!ampCommandAllowed(what.toLatin1().constData())) {
        return;
    }
    // Only an internal antenna the amplifier lists as set up (upstream's
    // range check runs again in the connection); the external one is shown,
    // not switched (nobody has seen what the amplifier does with it).
    bool available = false;
    if (m_ampStatus.antennas) {
        for (const Rfkit::AntennaWithState& a : *m_ampStatus.antennas) {
            if (a.antenna.type == Rfkit::AntennaType::Internal && a.antenna.number == number) {
                available = a.state == Rfkit::AntennaState::Available;
            }
        }
    }
    if (number < Rfkit::kMinInternalAntenna || number > Rfkit::kMaxInternalAntenna || !available) {
        qCInfo(lcMobile).noquote() << "amp: not sending" << what << "- not an available antenna";
        m_ampResult = tr("Antenna %1 is not available on the amplifier.").arg(number);
        m_ampResultOk = false;
        emit ampChanged();
        return;
    }
    m_ampPending = what;
    m_ampResult.clear();
    qCInfo(lcMobile).noquote() << "amp: asking the RF2K-S for" << what;
    m_amp->selectAntenna(Rfkit::Antenna{Rfkit::AntennaType::Internal, number});
    emit ampChanged();
}

void MobileEngine::ampResetError()
{
    if (!ampCommandAllowed("the error reset")) {
        return;
    }
    m_ampPending = QStringLiteral("reset");
    m_ampResult.clear();
    qCInfo(lcMobile) << "amp: asking the RF2K-S to reset its error";
    m_amp->resetError();
    emit ampChanged();
}

void MobileEngine::ampTune()
{
    if (!m_ampTune.isEmpty() || !ampCommandAllowed("the tune carrier")) {
        return;
    }
    const int watts = tunePower();
    QString why;
    if (!m_ampStatus.operate) {
        why = tr("The amplifier's mode is not read yet.");
    } else if (!tuneAvailable()) {
        why = tr("The radio cannot send a tune carrier now.");
    } else if (watts < kTuneMinWatts || watts > kTuneMaxWatts) {
        why = tr("Tune power is %1 W: the RF2K-S tunes with 4 to 39 W (10 W or more is best). Set Tune power on the TX tab.").arg(watts);
    } else if (txLocked()) {
        why = tr("Transmit is locked (SB): hold PTT, KEY, TUNE or CQ for a second to arm it.");
    }
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "amp: no tune carrier -" << why;
        m_ampResult = why;
        m_ampResultOk = false;
        emit ampChanged();
        return;
    }
    m_ampResult.clear();
    if (*m_ampStatus.operate == Rfkit::OperateMode::Operate) {
        // Tuned in STANDBY only: the carrier must not be amplified.
        m_ampTune = QStringLiteral("standby");
        m_ampPending = QStringLiteral("standby");
        qCInfo(lcMobile) << "amp: tune: asking the RF2K-S for STANDBY first";
        m_amp->setOperate(false);
        m_ampTuneClock.start();
        m_ampTuneTimer->start();
        updateAmpLink();
        emit ampChanged();
        return;
    }
    startAmpTuneCarrier();
}

void MobileEngine::startAmpTuneCarrier()
{
    if (radioTransmitting() || !tuneAvailable()) {
        endAmpTune(tr("The radio cannot send a tune carrier now."), false);
        return;
    }
    RadioModel& rm = m_session.radioModel();
    m_tuneInput = rm.localTxController()->capture(TxController::Activity::Tune);
    if (!m_tuneInput.start()) {
        m_tuneInput.stop();
        endAmpTune(tr("The radio did not accept the tune request."), false);
        return;
    }
    m_ampTune = QStringLiteral("carrier");
    m_ampTuneSawTuning = false;
    m_ampTuneKeyed = false;
    m_ampTuneClock.start();
    m_ampTuneTimer->start();
    qCInfo(lcMobile).noquote() << "amp: tune carrier keyed at" << tunePower() << "W for up to" << kTuneSeconds << "s";
    updateAmpLink();
    emit ampChanged();
}

// Every 250 ms and with every reading while a tune runs.
void MobileEngine::checkAmpTune()
{
    if (m_ampTune == QLatin1String("standby")) {
        if (m_ampPending.isEmpty() && m_ampStatus.operate && *m_ampStatus.operate == Rfkit::OperateMode::Standby) {
            qCInfo(lcMobile) << "amp: tune: the RF2K-S is in STANDBY";
            startAmpTuneCarrier();
        } else if (m_ampTuneClock.elapsed() > kTuneStandbyWaitMs) {
            endAmpTune(tr("The amplifier did not go to STANDBY: no tune carrier was sent."), false);
        }
        return;
    }
    if (m_ampTune != QLatin1String("carrier")) {
        return;
    }
    const qint64 ms = m_ampTuneClock.elapsed();
    const bool tuning = m_session.radioModel().transmitModel().isTuning();
    if (tuning) {
        m_ampTuneKeyed = true;
    }
    if (m_ampStatus.operate && *m_ampStatus.operate == Rfkit::OperateMode::Operate) {
        endAmpTune(tr("The amplifier went to OPERATE: the tune carrier stopped."), false);
        return;
    }
    if (m_ampTuneKeyed && !tuning) {
        endAmpTune(tr("The tune carrier stopped."), false);
        return;
    }
    if (!m_ampTuneKeyed && ms > kTuneKeyWaitMs) {
        endAmpTune(tr("The radio did not start the tune carrier."), false);
        return;
    }
    if (m_ampTuneKeyed && m_forwardPowerW > kTuneStopWatts) {
        endAmpTune(tr("The radio sent %1 W, more than the amplifier tunes with (4 to 39 W): the tune carrier stopped.")
                       .arg(qRound(m_forwardPowerW)), false);
        return;
    }
    if (m_ampStatus.tuner) {
        if (m_ampStatus.tuner->mode == Rfkit::TunerMode::AutoTuning) {
            if (!m_ampTuneSawTuning) {
                qCInfo(lcMobile) << "amp: tune: the RF2K-S is tuning";
            }
            m_ampTuneSawTuning = true;
        } else if (m_ampTuneSawTuning) {
            // Read with the carrier still on: the SWR it tuned to, when the
            // amplifier shows the carrier.
            const bool swrRead = m_ampStatus.power && m_ampStatus.power->forward.value >= 1.0;
            endAmpTune(swrRead ? tr("The amplifier tuned (SWR %1). Put it in OPERATE when you are ready.")
                                     .arg(m_ampStatus.power->swr.value, 0, 'f', 2)
                               : tr("The amplifier tuned. Put it in OPERATE when you are ready."), true);
            return;
        }
    }
    if (ms >= kTuneSeconds * 1000) {
        endAmpTune(m_ampTuneSawTuning ? tr("The amplifier was still tuning after 20 s: the tune carrier stopped.")
                                      : tr("No tune seen in 20 s: the tune carrier stopped."), false);
        return;
    }
    // The seconds left, once a second.
    const int left = int((kTuneSeconds * 1000 - ms + 999) / 1000);
    if (left != m_ampTuneLeftShown) {
        m_ampTuneLeftShown = left;
        emit ampChanged();
    }
}

void MobileEngine::endAmpTune(const QString& result, bool ok)
{
    const bool carrier = m_ampTune == QLatin1String("carrier");
    // Cleared first: the carrier's stop below is this session's own.
    m_ampTune.clear();
    m_ampTuneTimer->stop();
    m_ampTuneLeftShown = -1;
    if (carrier && m_tuneInput.valid() && m_session.radioModel().transmitModel().isTuning()) {
        m_tuneInput.stop();
        qCInfo(lcMobile) << "tune stopped: the amplifier's tune ended";
    }
    m_ampResult = result;
    m_ampResultOk = ok;
    qCInfo(lcMobile).noquote() << "amp: tune ended:" << result;
    updateAmpLink();
    emit ampChanged();
}

void MobileEngine::ampTuneStop()
{
    if (!m_ampTune.isEmpty()) {
        endAmpTune(tr("Stopped."), true);
    }
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
                  {QStringLiteral("control"), controlOn()},
                  {QStringLiteral("pending"), m_ampPending},
                  {QStringLiteral("result"), m_ampResult},
                  {QStringLiteral("resultOk"), m_ampResultOk},
                  {QStringLiteral("state"), m_ampState},
                  {QStringLiteral("error"), m_ampError},
                  {QStringLiteral("target"), m_ampTarget},
                  {QStringLiteral("tune"), m_ampTune},
                  {QStringLiteral("tuneLeft"), m_ampTune == QLatin1String("carrier")
                       ? std::max<qint64>(0, (kTuneSeconds * 1000 - m_ampTuneClock.elapsed() + 999) / 1000) : kTuneSeconds},
                  {QStringLiteral("tunePower"), tunePower()}};
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
        QVariantList list;
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
            const bool internal = a.antenna.type == Rfkit::AntennaType::Internal;
            list << QVariantMap{{QStringLiteral("label"), label},
                                {QStringLiteral("type"), internal ? QStringLiteral("INTERNAL") : QStringLiteral("EXTERNAL")},
                                {QStringLiteral("number"), internal ? a.antenna.number : a.externalPort},
                                {QStringLiteral("state"), antennaStateName(a.state)}};
        }
        m.insert(QStringLiteral("antennas"), available.join(QStringLiteral(", ")));
        m.insert(QStringLiteral("antennaList"), list);
    }
    return m;
}

}  // namespace AetherSDR::mobile
