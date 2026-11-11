// An Elecraft KPA1500 amplifier (roadmap 61: Rich Yost N2RY asked to test
// his). Read over the amplifier's UDP server (Kpa1500Reader): only queries,
// and the amplifier's one TCP connection stays free. Its readings go to
// Controls › Amp (AmpControls with Engine.kpa), every meter's Amp readings
// and the logged power in OPERATE, as the RF2K-S's (MobileEngineAmp.cpp).
//
// Control, as the RF2K-S's: off by default (Control the amplifier in Radio
// Setup › Amp); with it on, the amplifier's TCP connection is opened
// (Kpa1500Connection without its own poll: the commands only) for
// Operate / Standby, the antenna (ANT1 / ANT2), the ATU mode (in line /
// bypass), the fault clear and Tune; each confirmed on the page first, one
// at a time, never while the radio transmits, and never latched: a command
// is done when the UDP readback shows it (the amplifier answers no SET).
// Not here: keying the amplifier over the network (^TX); it stays on its KEY
// IN cable from the radio, as Elecraft's Flex guide wires it.
//
// Tune: the KPA1500's full-search tune (^FT) "needs continuous exciter RF
// power to complete" (Programming Reference V3, ^FE); its manual: "Apply 10
// to 50 W of transmit drive", in STBY or OPER (the PA is bypassed while it
// tunes). So kpaTune starts the search, keys the radio's TUNE carrier at the
// TX tab's Tune power (10-50 W) and stops it once ^TP shows the search done,
// on a fault (no match), over 55 W from the radio, a lost link, Stop or after
// 20 s (then ^FE cancels the search).
//
// As the RF2K-S: a device on the operator's network (same network or a
// VPN), never over SmartLink; read while the app is on screen and connected
// to a radio, every half second, every 250 ms while the radio transmits.

#include "Kpa1500Reader.h"
#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/Kpa1500Connection.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kKpaHostKey = QStringLiteral("MobileAmpKpaHost");
const QString kKpaOnKey = QStringLiteral("MobileAmpKpaOn");
const QString kKpaControlKey = QStringLiteral("MobileAmpKpaControl");
// A command is done when the readback shows it, or not seen in this long.
constexpr int kKpaCommandWaitMs = 3000;
constexpr int kKpaTuneSeconds = 20;
// The manual's full-search tune drive: 10 to 50 W; past 55 W read from the
// radio the carrier is not one to tune with.
constexpr int kKpaTuneMinWatts = 10;
constexpr int kKpaTuneMaxWatts = 50;
constexpr double kKpaTuneStopWatts = 55.0;
constexpr int kKpaTuneKeyWaitMs = 3000;
// The search read done (^TP0): a no match shows as fault 92 in the same
// moment, maybe read after ^TP; "tuned" only once ^FL has been read again
// (it is asked every cycle while a tune runs, 250 ms keyed).
constexpr int kKpaTuneSettleMs = 700;
// A control link closed this soon after it came up was refused.
constexpr int kKpaLinkRefusedMs = 2000;

// "host" or "host:port"; the amplifier's port 1500 when not given.
bool splitKpaAddress(const QString& spec, QString* host, quint16* port)
{
    *host = spec;
    *port = Kpa1500::kDefaultPort;
    const qsizetype colon = spec.lastIndexOf(QLatin1Char(':'));
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

bool kpaControlOn()
{
    return AppSettings::instance().value(kKpaControlKey).toString() == QLatin1String("1");
}
}  // namespace

void MobileEngine::setupKpa()
{
    m_kpa = new Kpa1500Reader(this);
    m_kpaSet = !AppSettings::instance().value(kKpaHostKey).toString().trimmed().isEmpty();
    connect(m_kpa, &Kpa1500Reader::answered, this, [this] {
        m_kpaState = QStringLiteral("connected");
        m_kpaError.clear();
        emit kpaChanged();
    });
    connect(m_kpa, &Kpa1500Reader::silent, this, [this](const QString& why) {
        if (!m_kpaWanted) {
            return;
        }
        m_kpaState = QStringLiteral("failed");
        m_kpaError = why;
        if (!m_kpaPending.isEmpty()) {
            kpaCommandDone(tr("The amplifier stopped answering before it showed the change: it may or may not have done it."), false);
        }
        if (!m_kpaTune.isEmpty()) {
            endKpaTune(tr("The amplifier stopped answering: the tune carrier stopped."), false);
        }
        emit kpaChanged();
    });
    connect(m_kpa, &Kpa1500Reader::statusUpdated, this, [this] {
        // A fault logged once as it comes and once as it clears.
        const int fault = m_kpa->status().faultCode.value_or(0);
        if (fault != m_kpaLastFault && m_kpa->answering()) {
            m_kpaLastFault = fault;
            if (fault != 0) {
                qCInfo(lcMobile).noquote() << "amp: KPA1500 fault"
                                           << QStringLiteral("%1").arg(fault, 2, 16, QLatin1Char('0')).toUpper()
                                           << Kpa1500::faultDescription(fault);
            } else {
                qCInfo(lcMobile) << "amp: KPA1500 fault cleared";
            }
        }
        checkKpaPending();
        checkKpaTune();
        emit kpaChanged();
    });

    // The control link: commands only, its own poll off (the UDP reader
    // reads); retried every 5 s while Control is on.
    m_kpaCtl = new Kpa1500Connection(this);
    m_kpaCtl->setPolling(false);
    m_kpaCtl->setAutoReconnect(true);
    connect(m_kpaCtl, &Kpa1500Connection::connected, this, [this] {
        m_kpaLinkUpClock.start();
        if (!m_kpaLinkClosedAtOnce) {
            m_kpaLinkState = QStringLiteral("connected");
            m_kpaLinkError.clear();
            qCInfo(lcMobile).noquote() << "amp: KPA1500 control link up at" << m_kpaCtl->description() << "(TCP)";
            emit kpaChanged();
        }
    });
    connect(m_kpaCtl, &Kpa1500Connection::disconnected, this, [this] {
        if (!m_kpaCtlWanted) {
            return;  // our own close
        }
        if (!m_kpaTune.isEmpty()) {
            endKpaTune(tr("The control link to the amplifier was lost: the tune carrier stopped."), false);
        }
        if (m_kpaLinkUpClock.isValid() && m_kpaLinkUpClock.elapsed() < kKpaLinkRefusedMs) {
            if (!m_kpaLinkClosedAtOnce) {
                qCInfo(lcMobile) << "amp: KPA1500 control link closed by the amplifier at once"
                                    " (another program may hold its one TCP connection); trying again every 5 s";
            }
            m_kpaLinkClosedAtOnce = true;
            m_kpaLinkState = QStringLiteral("failed");
            m_kpaLinkError = tr("the amplifier closed it at once");
        } else {
            m_kpaLinkClosedAtOnce = false;
            m_kpaLinkState = QStringLiteral("connecting");
            qCInfo(lcMobile) << "amp: KPA1500 control link lost";
        }
        emit kpaChanged();
    });
    m_kpaLinkCheck = new QTimer(this);
    m_kpaLinkCheck->setSingleShot(true);
    m_kpaLinkCheck->setInterval(kKpaLinkRefusedMs);
    // Up and still up after the refusal window: a link again.
    connect(m_kpaCtl, &Kpa1500Connection::connected, m_kpaLinkCheck, qOverload<>(&QTimer::start));
    connect(m_kpaLinkCheck, &QTimer::timeout, this, [this] {
        if (m_kpaLinkClosedAtOnce && m_kpaCtl->isConnected()) {
            m_kpaLinkClosedAtOnce = false;
            m_kpaLinkState = QStringLiteral("connected");
            m_kpaLinkError.clear();
            qCInfo(lcMobile).noquote() << "amp: KPA1500 control link up at" << m_kpaCtl->description() << "(TCP)";
            emit kpaChanged();
        }
    });
    connect(m_kpaCtl, &Kpa1500Connection::connectionFailed, this, [this](const QString& error) {
        if (!m_kpaCtlWanted) {
            return;
        }
        // A close right after it came up: the disconnect above says so.
        if (m_kpaLinkUpClock.isValid() && m_kpaLinkUpClock.elapsed() < kKpaLinkRefusedMs) {
            return;
        }
        if (m_kpaLinkState != QLatin1String("failed") || m_kpaLinkError != error) {
            qCInfo(lcMobile).noquote() << "amp: KPA1500 control link at" << m_kpaCtl->description() << "not reached:" << error;
        }
        m_kpaLinkState = QStringLiteral("failed");
        m_kpaLinkError = error;
        emit kpaChanged();
    });
    m_kpaPendingTimer = new QTimer(this);
    m_kpaPendingTimer->setSingleShot(true);
    m_kpaPendingTimer->setInterval(kKpaCommandWaitMs);
    connect(m_kpaPendingTimer, &QTimer::timeout, this, [this] {
        if (m_kpaPending.isEmpty()) {
            return;
        }
        qCInfo(lcMobile).noquote() << "amp: the KPA1500 has not shown" << m_kpaPending << "in" << kKpaCommandWaitMs / 1000 << "s";
        kpaCommandDone(m_kpaPending == QLatin1String("clear")
                           ? tr("The fault is still there: a temperature fault clears only as the amplifier cools; deal with the cause first.")
                           : tr("The amplifier has not shown the change in 3 s: it may not have taken it (a fault, or its front panel in a menu)."),
                       false);
    });
    m_kpaTuneTimer = new QTimer(this);
    m_kpaTuneTimer->setInterval(250);
    connect(m_kpaTuneTimer, &QTimer::timeout, this, &MobileEngine::checkKpaTune);

    connect(this, &MobileEngine::connectionStateChanged, this, [this] {
        if (!m_connected && !m_kpaTune.isEmpty()) {
            endKpaTune(tr("The radio was disconnected: no tune carrier."), false);
        }
    });
    connect(this, &MobileEngine::connectionStateChanged, this, &MobileEngine::updateKpaLink);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MobileEngine::updateKpaLink);
    connect(this, &MobileEngine::transmittingChanged, this, [this] { m_kpa->setFast(radioTransmitting()); });
    connect(this, &MobileEngine::tuneStateChanged, this, [this] { m_kpa->setFast(radioTransmitting()); });
}

// Read while it is set up and on, the app on screen and connected to a
// radio; else not. The control link likewise, with Control on.
void MobileEngine::updateKpaLink()
{
    QString host;
    quint16 port = 0;
    const QString spec = AppSettings::instance().value(kKpaHostKey).toString().trimmed();
    const bool on = AppSettings::instance().value(kKpaOnKey).toString() == QLatin1String("1");
    const Qt::ApplicationState app = qApp->applicationState();
    const bool onScreen = app == Qt::ApplicationActive || app == Qt::ApplicationInactive;
    const bool want = on && splitKpaAddress(spec, &host, &port) && m_connected && onScreen;
    const bool wantCtl = want && kpaControlOn();
    const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
    if (want != m_kpaWanted || (want && target != m_kpaTarget)) {
        if (!m_kpaTune.isEmpty()) {
            endKpaTune(tr("The amplifier is no longer read: the tune carrier stopped."), false);
        }
        m_kpaWanted = false;
        m_kpa->stop();
        m_kpaWanted = want;
        m_kpaError.clear();
        m_kpaPending.clear();
        if (want) {
            m_kpaState = QStringLiteral("connecting");
            qCInfo(lcMobile).noquote() << "amp: reading the KPA1500 at" << target << "(UDP, queries only)";
            m_kpa->setFast(radioTransmitting());
            m_kpa->start(host, port);
        } else {
            m_kpaState = QStringLiteral("off");
            qCInfo(lcMobile) << "amp: KPA1500 not read";
        }
    }
    if (wantCtl != m_kpaCtlWanted || (wantCtl && target != m_kpaTarget)) {
        m_kpaCtlWanted = false;
        m_kpaCtl->disconnect();
        m_kpaCtlWanted = wantCtl;
        m_kpaLinkError.clear();
        m_kpaLinkClosedAtOnce = false;
        m_kpaLinkUpClock.invalidate();
        if (wantCtl) {
            m_kpaLinkState = QStringLiteral("connecting");
            qCInfo(lcMobile).noquote() << "amp: KPA1500 control link to" << target << "(TCP)";
            m_kpaCtl->connectNetwork(host, port);
        } else {
            m_kpaLinkState = QStringLiteral("off");
        }
    }
    m_kpaTarget = want ? target : QString();
    emit kpaChanged();
}

void MobileEngine::setKpaAddress(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitKpaAddress(spec, &host, &port)) {
        setLastError(tr("KPA1500 address: use host or host:port, e.g. 192.168.1.70:1500."));
        return;
    }
    AppSettings::instance().setValue(kKpaHostKey, spec);
    m_kpaResult.clear();
    // A new address is meant to be read; forgetting it turns it off, and
    // control with it (a new amplifier starts read only).
    AppSettings::instance().setValue(kKpaOnKey, spec.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    if (spec.isEmpty()) {
        AppSettings::instance().setValue(kKpaControlKey, QStringLiteral("0"));
    }
    AppSettings::instance().save();
    // The meters offer the amplifier's readings only while one is set up.
    const bool setChanged = m_kpaSet != !spec.isEmpty();
    m_kpaSet = !spec.isEmpty();
    updateKpaLink();
    emit kpaChanged();
    if (setChanged) {
        emit meterSelectionChanged();
    }
}

void MobileEngine::setKpaOn(bool on)
{
    AppSettings::instance().setValue(kKpaOnKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    updateKpaLink();
    emit kpaChanged();
}

void MobileEngine::setKpaControl(bool on)
{
    AppSettings::instance().setValue(kKpaControlKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    m_kpaResult.clear();
    if (!on && !m_kpaTune.isEmpty()) {
        endKpaTune(tr("Control was turned off: the tune carrier stopped."), false);
    }
    qCInfo(lcMobile) << "amp: KPA1500 control" << (on ? "on: commands confirmed on the Amp tab" : "off: read only");
    updateKpaLink();
    emit kpaChanged();
}

// One command at a time, only with control on, both links up and the radio
// in receive. A refusal says why on the Amp tab.
bool MobileEngine::kpaCommandAllowed(const char* what)
{
    QString why;
    if (!kpaControlOn()) {
        why = tr("Control is off (Radio Setup › Amp).");
    } else if (!m_kpa->answering()) {
        why = tr("The amplifier is not answering.");
    } else if (!m_kpaCtl->isConnected() || m_kpaLinkState != QLatin1String("connected")) {
        why = tr("The control link is not connected.");
    } else if (radioTransmitting()) {
        why = tr("Not while transmitting: the amplifier must not switch with RF on it.");
    } else if (!m_kpaPending.isEmpty() || !m_kpaTune.isEmpty()) {
        why = tr("Still waiting for the amplifier.");
    }
    if (why.isEmpty()) {
        return true;
    }
    qCInfo(lcMobile).noquote() << "amp: not sending" << what << "to the KPA1500 -" << why;
    m_kpaResult = why;
    m_kpaResultOk = false;
    emit kpaChanged();
    return false;
}

void MobileEngine::kpaSendPending(const QString& pending, const QString& readback)
{
    m_kpaPending = pending;
    m_kpaResult.clear();
    m_kpaPendingTimer->start();
    // Read back at once, not at its turn in the cycle.
    m_kpa->queryNow(readback);
    emit kpaChanged();
}

// A command is done when the readback shows it.
void MobileEngine::checkKpaPending()
{
    if (m_kpaPending.isEmpty() || !m_kpa->answering()) {
        return;
    }
    const Kpa1500::Status& s = m_kpa->status();
    bool done = false;
    if (m_kpaPending == QLatin1String("operate") || m_kpaPending == QLatin1String("standby")) {
        done = s.operate && *s.operate == (m_kpaPending == QLatin1String("operate"));
    } else if (m_kpaPending.startsWith(QLatin1String("ANT"))) {
        done = s.antenna && QStringLiteral("ANT%1").arg(*s.antenna) == m_kpaPending;
    } else if (m_kpaPending == QLatin1String("atu in") || m_kpaPending == QLatin1String("atu bypass")) {
        const Kpa1500::AtuMode want = m_kpaPending == QLatin1String("atu in") ? Kpa1500::AtuMode::Inline : Kpa1500::AtuMode::Bypass;
        done = s.atuMode && *s.atuMode == want;
    } else if (m_kpaPending == QLatin1String("clear")) {
        done = s.faultCode && *s.faultCode == 0;
    }
    if (done) {
        qCInfo(lcMobile).noquote() << "amp: the KPA1500 shows" << m_kpaPending;
        kpaCommandDone(m_kpaPending == QLatin1String("clear") ? tr("The amplifier's fault is cleared.") : QString(), true);
    }
}

void MobileEngine::kpaCommandDone(const QString& result, bool ok)
{
    m_kpaPending.clear();
    m_kpaPendingTimer->stop();
    m_kpaResult = result;
    m_kpaResultOk = ok;
    emit kpaChanged();
}

void MobileEngine::kpaSetOperate(bool operate)
{
    if (!kpaCommandAllowed(operate ? "OPERATE" : "STANDBY")) {
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA1500 for" << (operate ? "OPERATE" : "STANDBY");
    m_kpaCtl->setOperate(operate);
    kpaSendPending(operate ? QStringLiteral("operate") : QStringLiteral("standby"), QStringLiteral("OS"));
}

void MobileEngine::kpaSelectAntenna(int number)
{
    const QString what = QStringLiteral("ANT%1").arg(number);
    if (!kpaCommandAllowed(what.toLatin1().constData())) {
        return;
    }
    // Its two connectors only; numbers 3-32 behind an external switch are
    // shown, not switched (as upstream's applet).
    if (number < Kpa1500::kMinAntenna || number > Kpa1500::kMaxAntenna) {
        m_kpaResult = tr("ANT%1 is not one of the amplifier's connectors.").arg(number);
        m_kpaResultOk = false;
        emit kpaChanged();
        return;
    }
    qCInfo(lcMobile).noquote() << "amp: asking the KPA1500 for" << what;
    m_kpaCtl->selectAntenna(number);
    kpaSendPending(what, QStringLiteral("AN"));
}

void MobileEngine::kpaSetAtu(bool inLine)
{
    if (!kpaCommandAllowed(inLine ? "ATU in line" : "ATU bypass")) {
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA1500 for its ATU" << (inLine ? "in line" : "bypassed");
    m_kpaCtl->setAtuMode(inLine);
    kpaSendPending(inLine ? QStringLiteral("atu in") : QStringLiteral("atu bypass"), QStringLiteral("AM"));
}

void MobileEngine::kpaClearFault()
{
    if (!kpaCommandAllowed("the fault clear")) {
        return;
    }
    qCInfo(lcMobile) << "amp: asking the KPA1500 to clear its fault";
    m_kpaCtl->clearFault();
    kpaSendPending(QStringLiteral("clear"), QStringLiteral("FL"));
}

void MobileEngine::kpaTune()
{
    if (!kpaCommandAllowed("the tune")) {
        return;
    }
    const int watts = tunePower();
    QString why;
    if (!m_ampTune.isEmpty()) {
        why = tr("The RF2K-S's tune is running.");
    } else if (m_kpa->status().faultCode.value_or(0) != 0) {
        why = tr("The amplifier shows a fault: clear it first.");
    } else if (!tuneAvailable()) {
        why = tr("The radio cannot send a tune carrier now.");
    } else if (watts < kKpaTuneMinWatts || watts > kKpaTuneMaxWatts) {
        why = tr("Tune power is %1 W: the KPA1500 tunes with 10 to 50 W (over 20 W tunes more exactly). Set Tune power on the TX tab.").arg(watts);
    } else if (txLocked()) {
        why = tr("Transmit is locked (SB): hold PTT, KEY, TUNE or CQ for a second to arm it.");
    }
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "amp: no KPA1500 tune -" << why;
        m_kpaResult = why;
        m_kpaResultOk = false;
        emit kpaChanged();
        return;
    }
    // The search first, then the carrier it waits for.
    qCInfo(lcMobile) << "amp: asking the KPA1500 for a full-search tune";
    m_kpaCtl->startTune();
    RadioModel& rm = m_session.radioModel();
    m_tuneInput = rm.localTxController()->capture(TxController::Activity::Tune);
    if (!m_tuneInput.start()) {
        m_tuneInput.stop();
        m_kpaCtl->cancelTune();
        m_kpaResult = tr("The radio did not accept the tune request.");
        m_kpaResultOk = false;
        emit kpaChanged();
        return;
    }
    m_kpaTune = QStringLiteral("carrier");
    m_kpaTuneSawTuning = false;
    m_kpaTuneKeyed = false;
    m_kpaTuneDoneMs = 0;
    m_kpaTuneLeftShown = -1;
    m_kpaResult.clear();
    m_kpaTuneClock.start();
    m_kpaTuneTimer->start();
    m_kpa->setExtraQueries({QStringLiteral("TP"), QStringLiteral("FL")});
    m_kpa->queryNow(QStringLiteral("TP"));
    qCInfo(lcMobile).noquote() << "amp: KPA1500 tune carrier keyed at" << watts << "W for up to" << kKpaTuneSeconds << "s";
    emit kpaChanged();
}

// Every 250 ms and with every reading while a tune runs.
void MobileEngine::checkKpaTune()
{
    if (m_kpaTune.isEmpty()) {
        return;
    }
    const qint64 ms = m_kpaTuneClock.elapsed();
    const bool tuning = m_session.radioModel().transmitModel().isTuning();
    if (tuning) {
        m_kpaTuneKeyed = true;
    }
    const Kpa1500::Status& s = m_kpa->status();
    if (m_kpaTuneKeyed && !tuning) {
        endKpaTune(tr("The tune carrier stopped."), false);
        return;
    }
    if (!m_kpaTuneKeyed && ms > kKpaTuneKeyWaitMs) {
        endKpaTune(tr("The radio did not start the tune carrier."), false);
        return;
    }
    if (m_kpaTuneKeyed && m_forwardPowerW > kKpaTuneStopWatts) {
        endKpaTune(tr("The radio sent %1 W, more than the amplifier tunes with (10 to 50 W): the tune carrier stopped.")
                       .arg(qRound(m_forwardPowerW)), false);
        return;
    }
    // A fault while tuning: 92 is the ATU's "no match".
    if (s.faultCode && *s.faultCode != 0) {
        const QString text = Kpa1500::faultDescription(*s.faultCode);
        endKpaTune(*s.faultCode == 0x92 ? tr("The amplifier's ATU found no match: check the band and antenna.")
                                        : tr("The amplifier faulted: %1.").arg(text.isEmpty() ? QString::number(*s.faultCode, 16) : text),
                   false);
        return;
    }
    if (s.tuning) {
        if (*s.tuning) {
            if (!m_kpaTuneSawTuning) {
                qCInfo(lcMobile) << "amp: the KPA1500 is tuning";
            }
            m_kpaTuneSawTuning = true;
        } else if (m_kpaTuneSawTuning && m_kpaTuneKeyed && m_kpaTuneDoneMs == 0) {
            m_kpaTuneDoneMs = ms;
            m_kpa->queryNow(QStringLiteral("FL"));
        } else if (m_kpaTuneDoneMs > 0 && ms - m_kpaTuneDoneMs >= kKpaTuneSettleMs) {
            // No fault read since: tuned. Read with the carrier still on:
            // the SWR it tuned to.
            const bool swrRead = s.swr && s.forwardWatts && *s.forwardWatts >= 1.0f;
            endKpaTune(swrRead ? tr("The amplifier tuned (SWR %1).").arg(double(*s.swr), 0, 'f', 2)
                               : tr("The amplifier tuned."), true);
            return;
        }
    }
    if (ms >= kKpaTuneSeconds * 1000) {
        endKpaTune(m_kpaTuneSawTuning ? tr("The amplifier was still tuning after 20 s: the tune stopped.")
                                      : tr("No tune seen in 20 s: the tune stopped."), false);
        return;
    }
    const int left = int((kKpaTuneSeconds * 1000 - ms + 999) / 1000);
    if (left != m_kpaTuneLeftShown) {
        m_kpaTuneLeftShown = left;
        emit kpaChanged();
    }
}

void MobileEngine::endKpaTune(const QString& result, bool ok, int cancel)
{
    // Cleared first: the carrier's stop below is this session's own.
    m_kpaTune.clear();
    m_kpaTuneTimer->stop();
    m_kpaTuneLeftShown = -1;
    m_kpa->setExtraQueries({});
    if (m_tuneInput.valid() && m_session.radioModel().transmitModel().isTuning()) {
        m_tuneInput.stop();
        qCInfo(lcMobile) << "tune stopped: the KPA1500's tune ended";
    }
    // A search not seen done is cancelled (it waits for RF otherwise). The
    // carrier is off first: the reference wants no RF at its end.
    if ((cancel < 0 ? !ok : cancel != 0) && m_kpaCtl->isConnected()) {
        m_kpaCtl->cancelTune();
    }
    m_kpaResult = result;
    m_kpaResultOk = ok;
    qCInfo(lcMobile).noquote() << "amp: KPA1500 tune ended:" << result;
    emit kpaChanged();
}

void MobileEngine::kpaTuneStop()
{
    if (!m_kpaTune.isEmpty()) {
        endKpaTune(tr("Stopped."), true, 1);
    }
}

bool MobileEngine::kpaOperating() const
{
    const Kpa1500::Status& s = m_kpa->status();
    return m_kpa->answering() && s.operate && *s.operate && s.powerOn.value_or(true);
}

double MobileEngine::kpaForwardW() const
{
    const Kpa1500::Status& s = m_kpa->status();
    return m_kpa->answering() && s.forwardWatts ? *s.forwardWatts : 0.0;
}

QVariantMap MobileEngine::kpa() const
{
    const QString spec = AppSettings::instance().value(kKpaHostKey).toString().trimmed();
    QVariantMap m{{QStringLiteral("set"), !spec.isEmpty()},
                  {QStringLiteral("model"), QStringLiteral("KPA1500")},
                  {QStringLiteral("device"), QStringLiteral("Elecraft KPA1500")},
                  {QStringLiteral("address"), spec},
                  {QStringLiteral("on"), AppSettings::instance().value(kKpaOnKey).toString() == QLatin1String("1")},
                  {QStringLiteral("control"), kpaControlOn()},
                  {QStringLiteral("link"), m_kpaLinkState},
                  {QStringLiteral("linkError"), m_kpaLinkError},
                  {QStringLiteral("pending"), m_kpaPending},
                  {QStringLiteral("result"), m_kpaResult},
                  {QStringLiteral("resultOk"), m_kpaResultOk},
                  {QStringLiteral("state"), m_kpaState},
                  {QStringLiteral("error"), m_kpaError},
                  {QStringLiteral("target"), m_kpaTarget},
                  {QStringLiteral("tune"), m_kpaTune},
                  {QStringLiteral("tuneLeft"), !m_kpaTune.isEmpty()
                       ? std::max<qint64>(0, (kKpaTuneSeconds * 1000 - m_kpaTuneClock.elapsed() + 999) / 1000) : kKpaTuneSeconds},
                  {QStringLiteral("tunePower"), tunePower()}};
    if (!m_kpa->answering()) {
        return m;
    }
    const Kpa1500::Status& s = m_kpa->status();
    // A reading not in yet is left out (the page shows "–"), never 0.
    if (s.forwardWatts) {
        m.insert(QStringLiteral("fwd"), *s.forwardWatts);
    }
    if (s.reflectedWatts) {
        m.insert(QStringLiteral("refl"), *s.reflectedWatts);
    }
    if (s.swr) {
        m.insert(QStringLiteral("swr"), *s.swr);
    }
    if (s.inputWatts) {
        m.insert(QStringLiteral("drive"), *s.inputWatts);
    }
    if (s.tempC) {
        m.insert(QStringLiteral("temp"), *s.tempC);
    }
    if (s.paVolts) {
        m.insert(QStringLiteral("volts"), *s.paVolts);
    }
    if (s.paAmps) {
        m.insert(QStringLiteral("amps"), *s.paAmps);
    }
    if (s.band) {
        const QString band = Kpa1500::bandName(*s.band);
        if (!band.isEmpty()) {
            m.insert(QStringLiteral("band"), band + QStringLiteral(" m"));
        }
    }
    if (s.antenna) {
        m.insert(QStringLiteral("antenna"), QStringLiteral("ANT%1").arg(*s.antenna));
    }
    if (s.tuning) {
        m.insert(QStringLiteral("tuning"), *s.tuning);
    }
    // The ATU: its mode (in line or bypassed for this band and antenna), and
    // bypassed for now when a tune found the antenna good enough without it.
    if (s.atuMode && *s.atuMode != Kpa1500::AtuMode::Unknown) {
        const bool inLine = *s.atuMode == Kpa1500::AtuMode::Inline;
        m.insert(QStringLiteral("atu"), inLine ? QStringLiteral("INLINE") : QStringLiteral("BYPASS"));
        QString tuner = inLine ? tr("In line") : tr("Bypassed");
        if (inLine && s.atuInline && !*s.atuInline) {
            tuner += tr(" (setting bypassed)");
        }
        if (s.tuning && *s.tuning) {
            tuner = tr("Tuning…");
        }
        m.insert(QStringLiteral("tuner"), tuner);
    }
    if (s.powerOn && !*s.powerOn) {
        m.insert(QStringLiteral("operate"), QStringLiteral("POWER OFF"));
    } else if (s.operate) {
        m.insert(QStringLiteral("operate"), *s.operate ? QStringLiteral("OPERATE") : QStringLiteral("STANDBY"));
    }
    if (s.faultCode && *s.faultCode != 0) {
        const QString code = QStringLiteral("%1").arg(*s.faultCode, 2, 16, QLatin1Char('0')).toUpper();
        const QString text = Kpa1500::faultDescription(*s.faultCode);
        m.insert(QStringLiteral("fault"), text.isEmpty() ? code : QStringLiteral("%1 (%2)").arg(text, code));
    }
    if (s.firmware) {
        m.insert(QStringLiteral("firmware"), *s.firmware);
    }
    return m;
}

}  // namespace AetherSDR::mobile
