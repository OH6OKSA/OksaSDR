// MIDI controllers (roadmap 54): Lynovation's CTR2-MIDI / CTR2-Dial and any
// other MIDI controller, over Bluetooth LE (paired in the app on an iPhone or
// iPad, in Audio MIDI Setup on a Mac) or USB, as the desktop's MIDI mapping:
// the engine's MidiControlManager (RtMidi on CoreMIDI) reads the controller,
// and MidiSettings keeps the bindings and imports a SmartSDR ".map" (the
// files Lynovation ships for SmartSDR for iOS / macOS) or the desktop's XML
// profile. The functions are the app's own, under the desktop's ids, so the
// same maps work. Transmitting from MIDI (PTT, MOX, TUNE, ATU, the CW
// paddles) stays off until the operator turns it on, and a transmission a
// controller keyed ends when the controller goes. Android: RtMidi has no
// backend there yet (its AMIDI one needs API 29), so the page says so.

#include "MobileEngine.h"

#include "MidiPairing.h"
#include "QsoLog.h"
#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#ifdef HAVE_MIDI
#include "core/MidiControlManager.h"
#include "core/MidiSettings.h"
#endif

#include <QDateTime>
#include <QLoggingCategory>
#include <QUrl>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

#if defined(HAVE_MIDI) && !defined(Q_OS_ANDROID)

namespace {
// Ports are listed again this often while a controller is open or waited
// for, so one switched on (or paired) later is opened by itself and one
// switched off is closed. Each listing makes a CoreMIDI client, so not
// when there is nothing to watch; Radio Setup › MIDI lists them itself.
constexpr int kMidiWatchMs = 4000;
// The VFO knob tunes from its own target while it turns: the radio's
// frequency echo lags the commands (the desktop's 250 ms).
constexpr int kMidiTuneIdleMs = 250;
// The last-message line is updated at most this often.
constexpr int kMidiActivityMs = 200;
// A turn of the VFO knob starts after this long without its messages; the
// first few messages and steps of each turn go to the log.
constexpr int kMidiVfoTurnGapMs = 1500;
constexpr int kMidiVfoLogPerTurn = 6;
const QString kMidiVfoParam = QStringLiteral("rx.tuneKnob");
const QString kMidiTransmitKey = QStringLiteral("MobileMidiTransmit");
} // namespace

void MobileEngine::setupMidi()
{
    m_midi = new MidiControlManager(this);
    registerMidiFunctions();
    MidiSettings::instance().load();
    for (const MidiBinding& b : MidiSettings::instance().loadBindings()) {
        if (m_midi->findParam(b.paramId)) {
            m_midi->addBinding(b);
        }
    }
    connect(m_midi, &MidiControlManager::paramActionTrace, this,
            [this](const QString& paramId, float value, quint64, quint64, quint64, const TxCoordinator::Request&) {
                midiAction(paramId, value);
            });
    connect(m_midi, &MidiControlManager::relativeAction, this, [this](const QString& paramId, int steps) {
        if (MidiControlManager::isVfoTuneKnob(paramId)) {
            if (m_midiVfoStepsLogged < kMidiVfoLogPerTurn) {
                ++m_midiVfoStepsLogged;
                qCInfo(lcMobile).noquote() << "MIDI: VFO knob"
                                           << (steps > 0 ? QStringLiteral("+%1").arg(steps) : QString::number(steps))
                                           << "step(s) of" << stepHz() << "Hz";
            }
            midiTune(steps);
        }
    });
    connect(m_midi, &MidiControlManager::midiActivity, this, [this](int channel, int type, int number, int value) {
        // The VFO knob's raw values, a few at the start of each turn: the
        // encoding a controller uses (64 +/- n, 1 / 127, 0 / 127 ...) is
        // read from them, and the steps above show how they were taken.
        for (const MidiBinding& b : m_midi->bindings()) {
            if (b.paramId != kMidiVfoParam || static_cast<int>(b.msgType) != type
                || (type != MidiBinding::PitchBend && b.number != number) || (b.channel >= 0 && b.channel != channel)) {
                continue;
            }
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_midiVfoLogMs > kMidiVfoTurnGapMs) {
                m_midiVfoRawLogged = 0;
                m_midiVfoStepsLogged = 0;
            }
            m_midiVfoLogMs = now;
            if (m_midiVfoRawLogged < kMidiVfoLogPerTurn) {
                ++m_midiVfoRawLogged;
                qCInfo(lcMobile).noquote() << "MIDI: VFO knob sent"
                                           << (type == MidiBinding::PitchBend ? QStringLiteral("pitch bend = %1").arg(value)
                                                                              : QStringLiteral("CC %1 = %2").arg(number).arg(value))
                                           << QStringLiteral("channel %1,").arg(channel + 1)
                                           << (b.relative ? "relative" : "not relative") + QString(b.inverted ? ", inverted" : "");
            }
            break;
        }
        QString what;
        switch (type) {
        case MidiBinding::CC: what = QStringLiteral("CC %1 = %2").arg(number).arg(value); break;
        case MidiBinding::NoteOn: what = value > 0 ? tr("Note %1 on").arg(number) : tr("Note %1 off").arg(number); break;
        case MidiBinding::NoteOff: what = tr("Note %1 off").arg(number); break;
        default: what = tr("Pitch bend = %1").arg(value); break;
        }
        m_midiActivityPending = tr("%1, channel %2").arg(what).arg(channel + 1);
        if (!m_midiActivityTimer.isActive()) {
            m_midiActivityTimer.start();
        }
    });
    m_midiActivityTimer.setSingleShot(true);
    m_midiActivityTimer.setInterval(kMidiActivityMs);
    connect(&m_midiActivityTimer, &QTimer::timeout, this, [this] {
        if (m_midiActivityPending != m_midiActivity) {
            m_midiActivity = m_midiActivityPending;
            emit midiActivityChanged();
        }
    });
    connect(m_midi, &MidiControlManager::learnCompleted, this, [this](const QString& paramId, const MidiBinding& b) {
        const MidiParam* p = m_midi->findParam(paramId);
        qCInfo(lcMobile).noquote() << "MIDI: learned" << b.sourceDisplayName() << "for" << (p ? p->displayName : paramId)
                                   << (b.relative ? "(relative)" : "");
        midiSaveBindings();
        emit midiChanged();
    });
    connect(m_midi, &MidiControlManager::portOpened, this, [this](const QString& name) {
        qCInfo(lcMobile).noquote() << "MIDI: opened" << name << "," << m_midi->bindings().size() << "bindings";
        for (const MidiBinding& b : m_midi->bindings()) {
            if (b.paramId == kMidiVfoParam) {
                qCInfo(lcMobile).noquote() << "MIDI: VFO knob on" << b.sourceDisplayName()
                                           << QString(b.relative ? "(relative" : "(not relative") + (b.inverted ? ", inverted)" : ")");
            }
        }
        MidiSettings::instance().setLastDevice(name);
        MidiSettings::instance().save();
        emit midiChanged();
    });
    connect(m_midi, &MidiControlManager::portClosed, this, [this] {
        midiEndTransmit(QStringLiteral("MIDI controller gone"));
        emit midiChanged();
    });
    connect(m_midi, &MidiControlManager::portError, this, [this](const QString& error) {
        qCWarning(lcMobile).noquote() << "MIDI:" << error;
        setLastError(tr("MIDI: %1").arg(error));
    });
    m_midiTuneIdle.setSingleShot(true);
    m_midiTuneIdle.setInterval(kMidiTuneIdleMs);
    connect(&m_midiTuneIdle, &QTimer::timeout, this, [this] { m_midiTuneTargetMhz = -1.0; });
    m_midiWatchTimer.setInterval(kMidiWatchMs);
    connect(&m_midiWatchTimer, &QTimer::timeout, this, [this] {
        if (m_midi->isOpen()
            || (MidiSettings::instance().autoConnect() && !MidiSettings::instance().lastDevice().isEmpty())) {
            midiWatch();
        }
    });
    m_midiWatchTimer.start();
    qCInfo(lcMobile).noquote() << "MIDI:" << m_midi->bindings().size() << "bindings kept, auto-connect"
                               << MidiSettings::instance().autoConnect() << "to"
                               << (MidiSettings::instance().lastDevice().isEmpty() ? QStringLiteral("(none yet)")
                                                                                   : MidiSettings::instance().lastDevice());
    midiWatch();
}

void MobileEngine::registerMidiFunctions()
{
    using P = MidiParamType;
    const auto reg = [this](const char* id, const QString& name, const QString& category, P type, float lo, float hi,
                            std::function<void(float)> action, bool tx = false) {
        m_midi->registerParam({QString::fromLatin1(id), name, category, type, lo, hi, {}, {}});
        m_midiActions.insert(QString::fromLatin1(id), std::move(action));
        if (tx) {
            m_midiTxFunctions.insert(QString::fromLatin1(id));
        }
    };
    const QString tuning = tr("Tuning");
    const QString bandMode = tr("Band and mode");
    const QString receive = tr("Receive");
    const QString transmit = tr("Transmit");

    // ---- Tuning ----
    // The knob itself comes through relativeAction (midiTune).
    reg("rx.tuneKnob", tr("VFO knob"), tuning, P::Slider, 0, 127, [](float) {});
    const auto stepTo = [this](int direction) {
        QList<int> steps = stepList();
        std::sort(steps.begin(), steps.end());
        if (steps.isEmpty()) {
            return;
        }
        const int now = stepHz();
        int next = now;
        if (direction > 0) {
            const auto it = std::upper_bound(steps.cbegin(), steps.cend(), now);
            next = it != steps.cend() ? *it : steps.last();
        } else {
            const auto it = std::lower_bound(steps.cbegin(), steps.cend(), now);
            next = it != steps.cbegin() ? *(it - 1) : steps.first();
        }
        if (next != now) {
            setStepHz(next);
        }
    };
    reg("rx.stepUp", tr("Step larger"), tuning, P::Trigger, 0, 1, [stepTo](float) { stepTo(1); });
    reg("rx.stepDown", tr("Step smaller"), tuning, P::Trigger, 0, 1, [stepTo](float) { stepTo(-1); });
    const auto megahertz = [this](double by) {
        if (m_slice) {
            tuneTo(m_slice->frequency() + by);
        }
    };
    reg("global.tuneUp1mhz", tr("Up 1 MHz"), tuning, P::Trigger, 0, 1, [megahertz](float) { megahertz(1.0); });
    reg("global.tuneDown1mhz", tr("Down 1 MHz"), tuning, P::Trigger, 0, 1, [megahertz](float) { megahertz(-1.0); });
    reg("global.panZoomIn", tr("Zoom in"), tuning, P::Trigger, 0, 1, [this](float) { zoom(0.5); });
    reg("global.panZoomOut", tr("Zoom out"), tuning, P::Trigger, 0, 1, [this](float) { zoom(2.0); });
    reg("global.splitToggle", tr("Split"), tuning, P::Trigger, 0, 1, [this](float) { toggleSplit(); });

    // ---- Band and mode ----
    const auto bandStep = [this](int direction) {
        QStringList list = bands();
        list.removeAll(QStringLiteral("WWV"));
        list.removeAll(QStringLiteral("GEN"));
        if (list.isEmpty() || !m_slice) {
            return;
        }
        const int at = list.indexOf(QsoLog::bandOf(m_slice->frequency()));
        const int next = at < 0 ? 0 : (at + direction + list.size()) % list.size();
        selectBand(list.at(next));
    };
    reg("global.bandUp", tr("Band up"), bandMode, P::Trigger, 0, 1, [bandStep](float) { bandStep(1); });
    reg("global.bandDown", tr("Band down"), bandMode, P::Trigger, 0, 1, [bandStep](float) { bandStep(-1); });
    static const char* const kBandIds[][2] = {{"global.band160m", "160m"}, {"global.band80m", "80m"},
                                              {"global.band60m", "60m"},   {"global.band40m", "40m"},
                                              {"global.band30m", "30m"},   {"global.band20m", "20m"},
                                              {"global.band17m", "17m"},   {"global.band15m", "15m"},
                                              {"global.band12m", "12m"},   {"global.band10m", "10m"},
                                              {"global.band6m", "6m"}};
    for (const auto& b : kBandIds) {
        const QString band = QString::fromLatin1(b[1]);
        reg(b[0], tr("Band %1").arg(band), bandMode, P::Trigger, 0, 1, [this, band](float) { selectBand(band); });
    }
    const auto modeStep = [this](int direction) {
        const QStringList list = modes();
        if (list.isEmpty()) {
            return;
        }
        const int at = list.indexOf(mode());
        chooseMode(list.at(at < 0 ? 0 : (at + direction + list.size()) % list.size()));
    };
    reg("global.modeUp", tr("Mode next"), bandMode, P::Trigger, 0, 1, [modeStep](float) { modeStep(1); });
    reg("global.modeDown", tr("Mode previous"), bandMode, P::Trigger, 0, 1, [modeStep](float) { modeStep(-1); });
    for (const char* m : {"USB", "LSB", "CW", "AM", "SAM", "FM", "DIGU", "DIGL", "RTTY"}) {
        const QString name = QString::fromLatin1(m);
        reg(QByteArray("global.mode").append(m).constData(), tr("Mode %1").arg(name), bandMode, P::Trigger, 0, 1,
            [this, name](float) {
                if (modes().contains(name)) {
                    chooseMode(name);
                }
            });
    }
    const auto filterStep = [this](int direction) {
        QList<int> widths = filterPresets();
        std::sort(widths.begin(), widths.end());
        if (widths.isEmpty()) {
            return;
        }
        const int now = filterWidth();
        if (direction > 0) {
            const auto it = std::upper_bound(widths.cbegin(), widths.cend(), now);
            setFilterWidth(it != widths.cend() ? *it : widths.last());
        } else {
            const auto it = std::lower_bound(widths.cbegin(), widths.cend(), now);
            setFilterWidth(it != widths.cbegin() ? *(it - 1) : widths.first());
        }
    };
    reg("global.filterWiden", tr("Filter wider"), bandMode, P::Trigger, 0, 1, [filterStep](float) { filterStep(1); });
    reg("global.filterNarrow", tr("Filter narrower"), bandMode, P::Trigger, 0, 1,
        [filterStep](float) { filterStep(-1); });

    // ---- Receive ----
    reg("rx.afGain", tr("Slice volume"), receive, P::Slider, 0, 100,
        [this](float v) { setSliceAudio(activeSliceId(), QStringLiteral("gain"), v); });
    reg("rx.mute", tr("Slice mute"), receive, P::Toggle, 0, 1, [this](float) {
        if (m_slice) {
            setSliceAudio(activeSliceId(), QStringLiteral("mute"), m_slice->audioMute() ? 0.0 : 1.0);
        }
    });
    // SmartSDR's main mute (a CTR2 map's "mainmute"): this app's mute is the
    // slice's, as the speaker icon's.
    reg("global.masterMute", tr("Mute"), receive, P::Toggle, 0, 1, [this](float) { setMuted(!muted()); });
    reg("rx.agcThreshold", tr("AGC-T"), receive, P::Slider, 0, 100, [this](float v) { setAgcThreshold(qRound(v)); });
    reg("rx.nbEnable", tr("NB"), receive, P::Toggle, 0, 1, [this](float) { setNb(!nb()); });
    reg("rx.nrEnable", tr("NR"), receive, P::Toggle, 0, 1, [this](float) { setNr(!nr()); });
    reg("rx.anfEnable", tr("ANF"), receive, P::Toggle, 0, 1, [this](float) { setAnf(!anf()); });

    // ---- Transmit ----
    // A setting, not a transmission: allowed with transmitting from MIDI off.
    reg("tx.rfPower", tr("RF power"), transmit, P::Slider, 0, 100, [this](float v) { setRfPower(qRound(v)); });
    reg("cw.speed", tr("CW speed"), transmit, P::Slider, 5, 60, [this](float v) { setCwSpeed(qRound(v)); });
    reg("tx.mox", tr("MOX"), transmit, P::Toggle, 0, 1, [this](float) {
        if (!midiTransmitRefused()) {
            m_midiKeyed = true;
            toggleMox();
        }
    }, true);
    reg("tx.tune", tr("TUNE"), transmit, P::Toggle, 0, 1, [this](float) {
        if (!midiTransmitRefused()) {
            m_midiKeyed = true;
            toggleTune();
        }
    }, true);
    reg("tx.atuStart", tr("ATU"), transmit, P::Trigger, 0, 1, [this](float) {
        if (!midiTransmitRefused()) {
            atuPressed();
        }
    }, true);
    // Held: down while the button is held, as the PTT button.
    reg("cw.ptt", tr("PTT (hold)"), transmit, P::Gate, 0, 1, [this](float v) {
        if (v > 0.5f) {
            if (!midiTransmitRefused()) {
                m_midiKeyed = true;
                setPtt(true, QStringLiteral("MIDI"));
            }
        } else {
            setPtt(false, QStringLiteral("MIDI released"));
        }
    }, true);
    // The CW paddles into the app's keyer, as the on-screen paddles.
    const auto paddles = [this](bool dit, bool dah) {
        if ((dit || dah) && midiTransmitRefused()) {
            return;
        }
        m_midiDit = dit;
        m_midiDah = dah;
        m_midiKeyed = m_midiKeyed || dit || dah;
        setCwPaddleState(dit, dah);
    };
    reg("cwdit", tr("CW paddle dit"), transmit, P::Gate, 0, 1,
        [this, paddles](float v) { paddles(v > 0.5f, m_midiDah); }, true);
    reg("cwdah", tr("CW paddle dah"), transmit, P::Gate, 0, 1,
        [this, paddles](float v) { paddles(m_midiDit, v > 0.5f); }, true);
    reg("cwkey", tr("CW straight key"), transmit, P::Gate, 0, 1, [this](float v) {
        if (v > 0.5f) {
            if (!midiTransmitRefused()) {
                m_midiKeyed = true;
                setCwKey(true, QStringLiteral("MIDI"));
            }
        } else {
            setCwKey(false, QStringLiteral("MIDI released"));
        }
    }, true);
}

void MobileEngine::midiAction(const QString& paramId, float value)
{
    const auto it = m_midiActions.constFind(paramId);
    if (it != m_midiActions.cend() && *it) {
        (*it)(value);
    }
}

void MobileEngine::midiTune(int steps)
{
    if (!m_slice || steps == 0) {
        return;
    }
    const int step = stepHz();
    if (step <= 0) {
        return;
    }
    // From the knob's own target while it turns: the radio's echo of the
    // last command would pull the next step back (the desktop's way).
    if (m_midiTuneTargetMhz < 0.0
        || (!m_midiTuneIdle.isActive() && std::abs(m_midiTuneTargetMhz - m_slice->frequency()) > 0.001)) {
        const qint64 hz = std::llround(m_slice->frequency() * 1e6);
        m_midiTuneTargetMhz = static_cast<double>(((hz + step / 2) / step) * step) / 1e6;
    }
    m_midiTuneTargetMhz += steps * step / 1e6;
    m_midiTuneIdle.start();
    tuneTo(m_midiTuneTargetMhz);
}

void MobileEngine::midiWatch()
{
    // ALSA's "Midi Through" (Linux) is a loopback, never a controller.
    QStringList ports = m_midi->availablePorts();
    ports.erase(std::remove_if(ports.begin(), ports.end(),
                               [](const QString& p) { return p.startsWith(QLatin1String("Midi Through")); }),
                ports.end());
    if (ports != m_midiPorts) {
        qCInfo(lcMobile).noquote() << "MIDI ports:" << (ports.isEmpty() ? QStringLiteral("none") : ports.join(QStringLiteral(", ")));
        m_midiPorts = ports;
        emit midiChanged();
    }
    // A Bluetooth controller switched off leaves its port open but gone from
    // the list: closed, so what it keyed ends.
    if (m_midi->isOpen() && !ports.contains(m_midi->currentPortName())) {
        qCInfo(lcMobile).noquote() << "MIDI:" << m_midi->currentPortName() << "gone";
        m_midi->closePort();
    }
    const QString last = MidiSettings::instance().lastDevice();
    if (!m_midi->isOpen() && MidiSettings::instance().autoConnect() && !last.isEmpty()
        && std::any_of(ports.cbegin(), ports.cend(), [&last](const QString& p) { return p == last; })) {
        m_midi->openPortByName(last);
    }
}

void MobileEngine::midiRefresh()
{
    midiWatch();
}

void MobileEngine::midiSaveBindings()
{
    MidiSettings::instance().saveBindings(m_midi->bindings());
}

bool MobileEngine::midiTransmitRefused()
{
    if (AppSettings::instance().value(kMidiTransmitKey, false).toBool()) {
        return false;
    }
    setLastError(tr("Transmitting from the MIDI controller is off: turn it on in Radio Setup › MIDI."));
    return true;
}

void MobileEngine::midiEndTransmit(const QString& why)
{
    m_midiDit = false;
    m_midiDah = false;
    if (!m_midiKeyed) {
        return;
    }
    m_midiKeyed = false;
    setCwPaddleState(false, false);
    setCwKey(false, why);
    if (transmitting()) {
        stopTransmit(why);
    }
}

QVariantMap MobileEngine::midi() const
{
    QVariantMap m{{QStringLiteral("supported"), m_midi != nullptr}};
    if (!m_midi) {
        return m;
    }
    QVariantList functions;
    for (const MidiParam& p : m_midi->params()) {
        QString source;
        for (const MidiBinding& b : m_midi->bindings()) {
            if (b.paramId == p.id) {
                source = b.sourceDisplayName();
            }
        }
        functions << QVariantMap{{QStringLiteral("id"), p.id},
                                 {QStringLiteral("name"), p.displayName},
                                 {QStringLiteral("category"), p.category},
                                 {QStringLiteral("source"), source},
                                 {QStringLiteral("tx"), m_midiTxFunctions.contains(p.id)}};
    }
    m.insert(QStringLiteral("ports"), m_midiPorts);
    m.insert(QStringLiteral("port"), m_midi->isOpen() ? m_midi->currentPortName() : QString());
    m.insert(QStringLiteral("lastDevice"), MidiSettings::instance().lastDevice());
    m.insert(QStringLiteral("autoConnect"), MidiSettings::instance().autoConnect());
    m.insert(QStringLiteral("transmit"), AppSettings::instance().value(kMidiTransmitKey, false).toBool());
    m.insert(QStringLiteral("learning"), m_midi->isLearning() ? m_midi->learningParamId() : QString());
    m.insert(QStringLiteral("pairing"), bluetoothMidiPairingAvailable());
    m.insert(QStringLiteral("onMac"), runningOnMac());
    m.insert(QStringLiteral("functions"), functions);
    m.insert(QStringLiteral("bound"), static_cast<int>(m_midi->bindings().size()));
    m.insert(QStringLiteral("import"), m_midiImport);
    return m;
}

void MobileEngine::midiOpen(const QString& port)
{
    if (m_midi->openPortByName(port)) {
        MidiSettings::instance().setAutoConnect(true);
        MidiSettings::instance().save();
    }
    emit midiChanged();
}

void MobileEngine::midiClose()
{
    // Not opened again by itself until chosen again.
    MidiSettings::instance().setAutoConnect(false);
    MidiSettings::instance().save();
    m_midi->closePort();
    qCInfo(lcMobile) << "MIDI: closed by the operator";
    emit midiChanged();
}

void MobileEngine::midiLearn(const QString& paramId)
{
    if (!m_midi->findParam(paramId)) {
        return;
    }
    m_midi->startLearn(paramId);
    qCInfo(lcMobile).noquote() << "MIDI: learn" << m_midi->findParam(paramId)->displayName;
    emit midiChanged();
}

void MobileEngine::midiCancelLearn()
{
    m_midi->cancelLearn();
    emit midiChanged();
}

void MobileEngine::midiUnbind(const QString& paramId)
{
    m_midi->removeBinding(paramId);
    midiSaveBindings();
    qCInfo(lcMobile).noquote() << "MIDI: unbound" << paramId;
    emit midiChanged();
}

void MobileEngine::midiClearAll()
{
    m_midi->clearBindings();
    midiSaveBindings();
    qCInfo(lcMobile) << "MIDI: every binding cleared";
    emit midiChanged();
}

void MobileEngine::midiImport(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const MidiImportResult r = MidiSettings::instance().importProfile(
        path, [this](const QString& id) { return m_midi->findParam(id) != nullptr; });
    if (!r.ok()) {
        m_midiImport = {{QStringLiteral("error"), r.errors.join(QStringLiteral(" "))}};
        qCWarning(lcMobile).noquote() << "MIDI import failed:" << r.errors.join(QStringLiteral(" "));
        emit midiChanged();
        return;
    }
    m_midi->clearBindings();
    for (const MidiBinding& b : MidiSettings::instance().loadProfile(r.profileName)) {
        if (m_midi->findParam(b.paramId)) {
            m_midi->addBinding(b);
        }
    }
    midiSaveBindings();
    QStringList skipped = r.skippedUnknownParam;
    skipped << r.skippedBadType;
    m_midiImport = {{QStringLiteral("name"), r.profileName},
                    {QStringLiteral("imported"), r.importedCount},
                    {QStringLiteral("skipped"), skipped},
                    {QStringLiteral("duplicates"), r.duplicates}};
    qCInfo(lcMobile).noquote() << "MIDI import" << r.profileName << ":" << r.importedCount << "bindings, skipped"
                               << (skipped.isEmpty() ? QStringLiteral("none") : skipped.join(QStringLiteral("; ")));
    emit midiChanged();
}

void MobileEngine::midiPair()
{
    if (!showBluetoothMidiPairing()) {
        setLastError(runningOnMac() ? tr("On a Mac, pair the Bluetooth MIDI controller in Audio MIDI Setup.")
                                    : tr("Bluetooth MIDI pairing is not available here."));
    }
}

void MobileEngine::setMidiAutoConnect(bool on)
{
    MidiSettings::instance().setAutoConnect(on);
    MidiSettings::instance().save();
    emit midiChanged();
    if (on) {
        midiWatch();
    }
}

void MobileEngine::setMidiTransmit(bool on)
{
    AppSettings::instance().setValue(kMidiTransmitKey, on);
    AppSettings::instance().save();
    qCInfo(lcMobile) << "MIDI: transmitting from the controller" << (on ? "allowed" : "off");
    if (!on) {
        midiEndTransmit(QStringLiteral("MIDI transmit turned off"));
    }
    emit midiChanged();
}

#else

// Android (no RtMidi backend yet) and builds without MIDI.
void MobileEngine::setupMidi() {}
void MobileEngine::registerMidiFunctions() {}
void MobileEngine::midiAction(const QString&, float) {}
void MobileEngine::midiTune(int) {}
void MobileEngine::midiWatch() {}
void MobileEngine::midiRefresh() {}
void MobileEngine::midiSaveBindings() {}
bool MobileEngine::midiTransmitRefused() { return true; }
void MobileEngine::midiEndTransmit(const QString&) {}
QVariantMap MobileEngine::midi() const { return {{QStringLiteral("supported"), false}}; }
void MobileEngine::midiOpen(const QString&) {}
void MobileEngine::midiClose() {}
void MobileEngine::midiLearn(const QString&) {}
void MobileEngine::midiCancelLearn() {}
void MobileEngine::midiUnbind(const QString&) {}
void MobileEngine::midiClearAll() {}
void MobileEngine::midiImport(const QUrl&) {}
void MobileEngine::midiPair() {}
void MobileEngine::setMidiAutoConnect(bool) {}
void MobileEngine::setMidiTransmit(bool) {}

#endif

} // namespace AetherSDR::mobile
