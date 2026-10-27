#include "MobileEngine.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <QCoreApplication>

#include "gui/ModeFilterPresets.h"
#include "core/SystemInfo.h"
#include "core/NetworkMtuPolicy.h"
#include "gui/VoiceModeGate.h"

#include "AudioSession.h"
#include "QsoLog.h"
#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#include <QtCore/qcoreapplication_platform.h>
#include <QCoreApplication>
#include <QJniObject>
#endif
#include <QSet>
#include <QRegularExpression>
#include <QTime>
#include <QUrl>
#include "Ft8Engine.h"
#include "ShareText.h"

#include "ScreenAwake.h"
#include "SecureStore.h"
#include "SideKeys.h"

#include "core/AppActivity.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DxClusterClient.h"
#include "core/DxSpot.h"
#include "core/SpotModeResolver.h"
#include "core/ClientComp.h"
#include "core/ClientEq.h"
#include "core/ClientFinalLimiter.h"
#include "core/ClientGate.h"
#include "core/RadioDiscovery.h"
#include "core/backends/IRadioBackend.h"
#include "core/backends/flex/PanadapterStream.h"
#include "core/discovery/RadioDiscoverySource.h"
#include "core/CtcssTones.h"
#include "core/MemoryRecallPolicy.h"
#include "core/CallsignInfo.h"
#include "core/CallsignLookupService.h"
#include "core/CtyDatParser.h"
#include "core/QrzLookupSettings.h"
#include "core/CwSidetoneGenerator.h"
#include "core/CwxLocalKeyer.h"
#include "core/IambicKeyer.h"
#include "models/CwRxModel.h"
#include "models/EqualizerModel.h"
#include "gui/MemoryCommands.h"
#include "models/CwxModel.h"
#include "models/DecoderAudioModel.h"
#include "models/MeterModel.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"
#include "models/SpotModel.h"
#include "models/TnfModel.h"
#include "models/TransmitModel.h"
#include "models/XvtrPolicy.h"
#include "models/BandSettings.h"
#include "models/BandDefs.h"

#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QQuickItem>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPointer>
#include <QHostAddress>
#include <QPermissions>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QThread>
#include <QDateTime>
#include <QMetaEnum>
#include <QNetworkInformation>
#include <QNetworkInterface>
#include <QTimer>
#ifdef Q_OS_ANDROID
#include <QMediaDevices>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <unistd.h>
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif

Q_LOGGING_CATEGORY(lcMobile, "aether.mobile")

namespace AetherSDR::mobile {

namespace {

MobileEngine* s_instance = nullptr;

constexpr double kS9Dbm = -73.0;
// The radio's remote_audio_tx VITA-49 port (FlexLib).
constexpr quint16 kRadioTxAudioPort = 4991;
const QString kLastAddressKey = QStringLiteral("MobileLastRadioAddress");
const QString kControlsVisibleKey = QStringLiteral("MobileControlsVisible");
const QString kTuneRowsShownKey = QStringLiteral("MobileTuneRowsShown");
const QString kRadioStepButtonsKey = QStringLiteral("MobileRadioStepButtons");
const QString kDeviceMicSelection = QStringLiteral("PC");
constexpr int kPanPixelsSettleMs = 400;
// The radio's FFT vertical encoding, the same in every orientation and on
// every device: the desktop's 700 rows, also the decoder's default. It used
// to follow the view's height (226 rows in iPad landscape, 406 in portrait);
// when the radio's y_pixels echo for a rotation was missed, the decoder kept
// the old height and the trace and 3D view drew flat until the next resend.
constexpr int kPanYPixels = 700;
// The pan's dBm scale the radio encodes against. Bins below the bottom are
// pinned there, and narrow FFT bins on a quiet band with RF gain 0 sit near
// -140 dBm, under the radio's usual -135 bottom: the trace and the 3D view
// then drew a flat line. The app scales the view itself, so ask for a scale
// wide enough to hold the noise and the strongest signals.
constexpr float kPanMinDbm = -165.0f;
constexpr float kPanMaxDbm = -20.0f;
const QString kSmartLinkTokenKey = QStringLiteral("smartlink_refresh_token");
// SmartLink UDP port when the radio reports none (FlexLib default).
constexpr quint16 kDefaultWanUdpPort = 4993;
constexpr int kWanReconnectMs = 5000;
// Fraction of the span kept between the slice and the edge while tuning.
constexpr double kPanFollowEdge = 0.1;
// Coalescing interval for pan moves during a drag.
constexpr int kPanMoveMs = 40;
// How long a dragged pan centre is shown before the radio's echo rules again.
constexpr int kPanPreviewHoldMs = 700;
constexpr double kDbPerSUnit = 6.0;

// The app's memory in MB: on iOS and macOS its physical footprint (what
// Xcode shows, and what the system's memory limit for the app counts),
// elsewhere the resident set; 0 where it cannot be read.
double processMemoryMB()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0.0;
    }
    return info.phys_footprint / (1024.0 * 1024.0);
#elif defined(Q_OS_LINUX)
    // /proc/self/statm: total and resident size in pages.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) {
        return 0.0;
    }
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.size() > 1 ? fields.at(1).toDouble() * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0) : 0.0;
#else
    return 0.0;
#endif
}

} // namespace

namespace {
// Keyboard diagnostics (Pentti's MacBook: no typing into any field): which
// item has focus, and whether Qt sees key or input-method events for it;
// summed up when a text field lets go. A few lines per field, not per key.
class KeyboardDiag : public QObject {
public:
    explicit KeyboardDiag(QObject* parent) : QObject(parent) {}
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        const QEvent::Type t = event->type();
        if ((t == QEvent::KeyPress || t == QEvent::InputMethod) && receiver == QGuiApplication::focusObject()) {
            if (t == QEvent::KeyPress) {
                ++keys;
                if (keys <= 3) {
                    // Not the key itself: what is typed may be a password.
                    const auto* k = static_cast<QKeyEvent*>(event);
                    qCInfo(lcMobile).noquote() << QStringLiteral("keyboard: a key (%1) reached %2")
                                                      .arg(k->text().isEmpty() ? QStringLiteral("no text") : QStringLiteral("with text"),
                                                           QString::fromLatin1(receiver->metaObject()->className()));
                }
            } else {
                ++inputMethod;
            }
        }
        return false;
    }
    int keys{0};
    int inputMethod{0};
};

// On a Mac an iPad app gets the keyboard as key presses only: the typed text
// never reached Qt's text responder (build 108, a tester's MacBook: no key or
// text event at the focused field, the GameController handler already off).
// There the text fields report no input method, so Qt's view sends each press
// with its text (from the Mac's layout: ä, ö, /) as a key event, the way the
// desktop types.
class MacTextKeys : public QObject {
public:
    explicit MacTextKeys(QObject* parent) : QObject(parent) {}
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (event->type() != QEvent::InputMethodQuery
            || !(receiver->inherits("QQuickTextInput") || receiver->inherits("QQuickTextEdit"))) {
            return false;
        }
        // Answered as QQuickItem::event does, ImEnabled excepted.
        const auto* item = static_cast<QQuickItem*>(receiver);
        auto* query = static_cast<QInputMethodQueryEvent*>(event);
        const uint queries = uint(query->queries());
        for (uint i = 0; i < 32; ++i) {
            const auto q = static_cast<Qt::InputMethodQuery>(int(queries & (1u << i)));
            if (q) {
                query->setValue(q, q == Qt::ImEnabled ? QVariant(false) : item->inputMethodQuery(q));
            }
        }
        query->accept();
        return true;
    }
};

// Puts text into a Qt text field as typing does: the old text selected and
// replaced key by key (validators and textEdited see a user's edit), then
// Return for a one-line field (accepted, editingFinished).
void typeIntoField(QQuickItem* field, const QString& text)
{
    const auto send = [field](int key, const QString& keyText) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(field, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, keyText);
        QCoreApplication::sendEvent(field, &release);
    };
    QMetaObject::invokeMethod(field, "selectAll");
    send(Qt::Key_Backspace, QString());
    for (const QChar ch : text) {
        send(ch.toUpper().unicode(), QString(ch));
    }
    if (field->inherits("QQuickTextInput")) {
        send(Qt::Key_Return, QString());
    }
}

// On a Mac a focused text field opens Apple's text box (no typed key reaches
// Qt there). Once per focus: the field lets go of focus afterwards, so the
// box does not come back when the window takes focus again.
void offerMacTextEntry(QObject* obj)
{
    static bool open = false;
    static QElapsedTimer openedAt;
    static QPointer<QObject> lastField;
    static QElapsedTimer closedAt;
    // A box that never showed must not block the rest for good.
    if (open && openedAt.elapsed() < 30000) {
        return;
    }
    auto* field = qobject_cast<QQuickItem*>(obj);
    if (!field || field->property("readOnly").toBool()) {
        return;
    }
    if (field == lastField && closedAt.isValid() && closedAt.elapsed() < 1500) {
        return;
    }
    open = true;
    openedAt.start();
    qCInfo(lcMobile).noquote() << "keyboard: key routing:" << macKeyRouting();
    QString title = field->property("placeholderText").toString();
    if (title.isEmpty()) {
        title = MobileEngine::tr("Text");
    }
    const bool secret = field->property("echoMode").toInt() != 0;  // TextInput.Normal is 0
    const QPointer<QQuickItem> guard(field);
    QTimer::singleShot(0, qApp, [guard, title, secret] {
        if (!guard) {
            open = false;
            return;
        }
        showMacTextEntry(title, MobileEngine::tr("Type here; Return puts the text in the field."),
                         guard->property("text").toString(), secret, MobileEngine::tr("OK"),
                         MobileEngine::tr("Cancel"), [guard](bool ok, const QString& typed) {
                             open = false;
                             lastField = guard.data();
                             closedAt.start();
                             qCInfo(lcMobile).noquote() << "keyboard: text box" << (ok ? "OK," : "cancelled,")
                                                        << typed.size() << "characters";
                             if (!guard) {
                                 return;
                             }
                             if (ok) {
                                 typeIntoField(guard, typed);
                             }
                             guard->setFocus(false);
                         });
    });
}
}  // namespace

MobileEngine::MobileEngine(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    m_bandPlan.loadPlans();
    if (!bandPlan().isEmpty()) {
        m_bandPlan.setActivePlan(bandPlan());
    }
    RadioModel& rm = m_session.radioModel();
    m_session.setSessionId(1);

    // AudioEngine reads AppSettings in its constructor; main() loads them first.
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName(QStringLiteral("AudioEngine"));
    m_audio = new AudioEngine;
    m_audio->moveToThread(m_audioThread);
    connect(m_audio, &AudioEngine::txClipRecorded, this, &MobileEngine::onVoiceClipRecorded);
    connect(m_audio, &AudioEngine::txClipFinished, this, &MobileEngine::onVoiceClipFinished);
    setupDxCluster();
    setupFreeDv();
    // Network changes in the log (Wi-Fi <-> mobile data, offline): a remote
    // session that drops can then be told apart from the network switching
    // under it.
    if (QNetworkInformation::loadDefaultBackend()) {
        QNetworkInformation* net = QNetworkInformation::instance();
        auto describe = [net] {
            return QStringLiteral("%1, %2%3")
                .arg(QString::fromLatin1(QMetaEnum::fromType<QNetworkInformation::TransportMedium>()
                                             .valueToKey(int(net->transportMedium()))),
                     QString::fromLatin1(QMetaEnum::fromType<QNetworkInformation::Reachability>()
                                             .valueToKey(int(net->reachability()))),
                     net->isMetered() ? QStringLiteral(", metered") : QString());
        };
        qCInfo(lcMobile).noquote() << "network:" << describe() << "(" << net->backendName() << ")";
        connect(net, &QNetworkInformation::transportMediumChanged, this,
                [describe] { qCInfo(lcMobile).noquote() << "network changed:" << describe(); });
        connect(net, &QNetworkInformation::reachabilityChanged, this,
                [describe] { qCInfo(lcMobile).noquote() << "network changed:" << describe(); });
    } else {
        qCInfo(lcMobile) << "network: no information backend on this platform";
    }
    // The final limiter (-1 dBFS) on unless chosen otherwise: the device
    // microphone's gain can boost up to +30 dB.
    if (!AppSettings::instance().contains(QStringLiteral("ClientFinalLimiterTxEnabled"))) {
        if (ClientFinalLimiter* limiter = m_audio->clientFinalLimiterTx()) {
            limiter->setEnabled(true);
            m_audio->saveClientFinalLimiterSettings();
            AppSettings::instance().save();
        }
    }
    m_txChainSaveTimer.setSingleShot(true);
    m_txChainSaveTimer.setInterval(1000);
    connect(&m_txChainSaveTimer, &QTimer::timeout, this, [this] {
        m_audio->saveClientGateSettings();
        m_audio->saveClientCompSettings();
        m_audio->saveClientEqSettings();
        AppSettings::instance().save();
    });
    // This device's microphone: the radio's MIC meter only sees its own
    // jacks, so the level is measured from what is sent (as AetherSDR's
    // P/CW applet), with VU ballistics: fast attack, ~20 dB/s decay.
    connect(m_audio, &AudioEngine::pcMicLevelChanged, this, [this](float peakDb, float avgDb) {
        if (!m_connected || m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection) {
            return;
        }
        constexpr double kDecayPerUpdate = 1.0;
        m_micLevelDbfs = avgDb > m_micLevelDbfs ? avgDb : std::max<double>(avgDb, m_micLevelDbfs - kDecayPerUpdate);
        m_micPeakDbfs = peakDb > m_micPeakDbfs ? peakDb
                                               : std::max(m_micLevelDbfs, m_micPeakDbfs - kDecayPerUpdate / 2);
        emit txMetersChanged();
    });
    // Receive audio: the chosen buffer, and a line in the log whenever the
    // playback buffer ran dry in the last 10 s (with what the link did), so
    // stutter can be told apart: late packets or too short a buffer.
    QTimer::singleShot(0, this, [this] { applyAudioBuffer(); });
    setAudioOutputMode(audioOutput());
    applyAudioOutputDevice();
#ifdef Q_OS_ANDROID
    m_headsetLinkTimer.setInterval(250);
    connect(&m_headsetLinkTimer, &QTimer::timeout, this, [this] {
        if (!m_connected || !m_audio || audioOutput() != QLatin1String("Auto")) {
            m_headsetLinkTimer.stop();
            return;
        }
        const qint64 linkUpMs = QJniObject::callStaticMethod<jlong>("fi/oh6oksa/oksasdr/Platform", "scoUpAt");
        const qint64 rxOpenedMs = m_audio->rxStartedAtMs();
        const bool timedOut = ++m_headsetLinkTicks >= 16;
        if (linkUpMs == 0 && !timedOut) {
            return;
        }
        m_headsetLinkTimer.stop();
        if (linkUpMs > 1 && rxOpenedMs > linkUpMs) {
            return;  // receive opened on the link
        }
        if (linkUpMs == 0 && !QJniObject::callStaticMethod<jboolean>("fi/oh6oksa/oksasdr/Platform", "headsetRoute",
                "(Landroid/content/Context;)Z", QNativeInterface::QAndroidApplication::context().object())) {
            return;  // no headset
        }
        if (linkUpMs == 1) {
            return;  // the link was up before
        }
        qCInfo(lcMobile).noquote() << (linkUpMs > 0
            ? QStringLiteral("headset hands-free link up %1 ms after receive opened: receive opened again on it")
                  .arg(linkUpMs - rxOpenedMs)
            : QStringLiteral("no hands-free link state from Android in 4 s with the headset as the route: receive opened again"));
        QMetaObject::invokeMethod(m_audio, [audio = m_audio] { audio->reopenRx(); }, Qt::QueuedConnection);
    });
#endif
    m_audioDiagTimer.setInterval(10000);
    connect(&m_audioDiagTimer, &QTimer::timeout, this, [this] {
        if (!m_connected || !m_audio) {
            return;
        }
        const quint64 underruns = m_audio->rxBufferUnderrunCount();
        RadioModel& model = m_session.radioModel();
        const int gapWindowMaxMs = model.takeAudioPacketGapWindowMaxMs();
        if (underruns > m_lastUnderruns && m_lastUnderruns > 0) {
            qCInfo(lcMobile).noquote()
                << QStringLiteral("audio underrun: %1 in 10 s, buffer %2 ms, queued %3 ms, "
                                  "packet gap max %4 ms (session %10 ms), jitter %5 ms, buffer mode %6, "
                                  "CPU %7 % (%8 % of one core), %9 frames/s")
                       .arg(underruns - m_lastUnderruns)
                       .arg(m_audio->rxBufferMs(), 0, 'f', 0)
                       .arg(m_audio->rxPlaybackQueuedMs())
                       .arg(gapWindowMaxMs)
                       .arg(model.audioPacketJitterMs())
                       .arg(audioBuffer())
                       .arg(qRound(m_cpuPercent))
                       .arg(qRound(m_cpuPercentOfCore))
                       .arg(m_uiFps)
                       .arg(model.audioPacketGapMaxMs());
        }
        m_lastUnderruns = underruns;
    });
    m_audioDiagTimer.start();
    // CPU (as the desktop's status bar) and frames drawn, every 2 s.
    m_perfClock.start();
    if (const auto cpu = SystemInfo::processCpuUsecs()) {
        m_lastCpuUsecs = *cpu;
    }
    m_perfTimer.setInterval(2000);
    connect(&m_perfTimer, &QTimer::timeout, this, [this] {
        const quint64 elapsedUs = static_cast<quint64>(m_perfClock.nsecsElapsed() / 1000);
        m_perfClock.restart();
        if (const auto cpu = SystemInfo::processCpuUsecs()) {
            m_cpuPercent = SystemInfo::processPercentOfCapacity(m_lastCpuUsecs, *cpu, elapsedUs, cpuCores());
            m_cpuPercentOfCore = elapsedUs > 0 && *cpu >= m_lastCpuUsecs
                ? 100.0 * double(*cpu - m_lastCpuUsecs) / double(elapsedUs) : 0.0;
            m_lastCpuUsecs = *cpu;
        }
        m_uiFps = elapsedUs > 0 ? qRound(m_framesThisPeriod.exchange(0) * 1e6 / double(elapsedUs)) : 0;
        m_memoryMB = processMemoryMB();
        sampleThreads(elapsedUs);
        emit performanceChanged();
        // Once a minute in the log while connected (30 samples of 2 s).
        static int samples = 0;
        if (m_connected && ++samples >= 30) {
            samples = 0;
            qCInfo(lcMobile).noquote()
                << QStringLiteral("performance: CPU %1 % of %2 cores (%3 % of one core), %4 frames/s, memory %5 MB%6")
                       .arg(qRound(m_cpuPercent)).arg(cpuCores())
                       .arg(qRound(m_cpuPercentOfCore)).arg(m_uiFps)
                       .arg(qRound(m_memoryMB))
                       .arg(m_busyThreads.isEmpty() ? QString()
                                                    : QStringLiteral("; busiest threads: ") + m_busyThreads.join(QStringLiteral(", ")));
        }
    });
    m_perfTimer.start();
    // QSO log and callsign lookup: the log file, cty.dat for the country of
    // any call, and QRZ.com with the account the Log tab stores (password in
    // the platform's secure store; the engine keeps it for the session).
    // The file is read in a worker: a logbook of tens of thousands of
    // contacts no longer holds the start (worked-before marks follow when
    // it is in).
    m_qsoLog = std::make_unique<QsoLog>(QsoLog::defaultPath(), false);
    {
        const QString path = m_qsoLog->path();
        QThread* reader = QThread::create([this, path] {
            QElapsedTimer timer;
            timer.start();
            QsoLog::Loaded loaded = QsoLog::read(path);
            const qint64 ms = timer.elapsed();
            QMetaObject::invokeMethod(this, [this, loaded = std::move(loaded), ms]() mutable {
                m_qsoLog->adopt(std::move(loaded));
                qCInfo(lcMobile) << "QSO log:" << m_qsoLog->count() << "contacts in" << m_qsoLog->path() << "read in"
                                 << ms << "ms";
                emit qsoLogChanged();
            }, Qt::QueuedConnection);
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
    }
    m_qsoLog->setMirrorPath(QsoLog::defaultMirrorPath());
#ifdef Q_OS_ANDROID
    // Android 9 and 10 write the shared Documents copy only with storage
    // permission; asked once, the copy is written when it is granted.
    if (!m_qsoLog->mirrorPath().isEmpty() && !m_qsoLog->mirrorOk() && QNativeInterface::QAndroidApplication::sdkVersion() < 30) {
        QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE"))
            .then(this, [this](QtAndroidPrivate::PermissionResult result) {
                if (result == QtAndroidPrivate::Authorized) {
                    m_qsoLog->setMirrorPath(m_qsoLog->mirrorPath());
                    emit qsoLogChanged();
                }
            });
    }
#endif
    setupDigi();
    setupOnlineLog();
    m_cty = std::make_unique<CtyDatParser>();
    m_cty->loadFromResource(QStringLiteral(":/cty.dat"));
    {
        CallsignLookupService& lookup = CallsignLookupService::instance();
        lookup.setCtyParser(m_cty.get());
        if (!QrzLookupSettings::username().isEmpty()) {
            lookup.savePassword(loadSecret(QStringLiteral("qrz_password")));
        }
        connect(&lookup, &CallsignLookupService::infoReady, this,
                [this](const CallsignInfo& info, bool) { applyCallInfo(info); });
        // A QSO still waiting for its lookup when the app closes is written
        // as it is, not lost.
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            for (const QString& call : m_pendingQsos.keys()) {
                completePendingQso(call, {});
            }
        });
        connect(&lookup, &CallsignLookupService::lookupFailed, this, [this](const QString& call, const QString& message) {
            completePendingQso(call, {});
            if (call.compare(m_lookupCall, Qt::CaseInsensitive) == 0) {
                m_callLookup = {{QStringLiteral("call"), call}, {QStringLiteral("error"), message}};
                emit callLookupChanged();
            }
        });
        // The sign-in check after Save: shown under the account fields.
        connect(&lookup, &CallsignLookupService::loginTestFinished, this, [this](bool ok, const QString& message) {
            m_qrzStatusKind = ok ? QStringLiteral("ok") : QStringLiteral("failed");
            m_qrzStatus = ok ? (message.isEmpty() ? tr("Signed in to QRZ.com.")
                                                  : tr("Signed in to QRZ.com: %1").arg(message))
                             : tr("QRZ.com sign-in failed: %1").arg(message);
            qCInfo(lcMobile) << "QRZ.com sign-in" << (ok ? "ok" : "failed:") << (ok ? QString() : message);
            emit qrzChanged();
        });
        // A saved account is checked at start too, so the Log tab says
        // whether it still signs in (it said nothing after a restart).
        const QString user = QrzLookupSettings::username();
        const QString pass = user.isEmpty() ? QString() : loadSecret(QStringLiteral("qrz_password"));
        if (!pass.isEmpty()) {
            m_qrzStatusKind = QStringLiteral("checking");
            m_qrzStatus = tr("Signing in to QRZ.com…");
            QTimer::singleShot(3000, this, [user, pass] { CallsignLookupService::instance().testLogin(user, pass); });
        }
    }

    // After a call, alarm or Siri: receive audio (and a microphone that was
    // open) could not reopen while iOS held the session (the RX sink's restart
    // failed), so start them again once the session is back.
    setAudioSessionReactivatedHandler([this] {
        if (!m_connected || !m_audio) {
            return;
        }
        qCInfo(lcMobile) << "audio session back: restarting receive audio";
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio] {
            audio->stopRxStream();
            audio->startRxStream();
        }, Qt::QueuedConnection);
        if (m_micCheckOpen || m_pttHeld) {
            stopDeviceMicrophoneCapture(true);
            startDeviceMicrophoneCapture();
        }
    });
    startStreamThread(m_audioThread);

    connect(&rm, &RadioModel::connectionStateChanged, this, &MobileEngine::onConnectionStateChanged);
    connect(&rm, &RadioModel::connectionError, this, [this](const QString& msg) {
        setLastError(msg);
        if (m_connectionState == QLatin1String("connecting")) {
            setConnectionState(QStringLiteral("idle"));
        }
    });
    connect(&rm, &RadioModel::backendRebuilt, this, [this] {
        stopTransmit(QStringLiteral("backend rebuilt"));
        wireRxAudio();
        wireTxPacketSink();
    });
    wireTransmit();
    // Losing the foreground (call, app switch, lock) must never leave the
    // transmitter keyed.
    // The 3D view on a weak GPU can hang the app (Android "isn't responding",
    // seen on a Unisoc T606 tablet) before the slow-frame check can act. A
    // mark is kept while the 3D view runs in front and cleared whenever the
    // app leaves it or quits; found at start, the app was killed with 3D on,
    // so it starts in 2D.
    if (spectrum3D() && AppSettings::instance().value(QStringLiteral("Mobile3DRunning"), 0).toInt() != 0) {
        qCWarning(lcMobile) << "3D view: the app stopped while it ran; starting with the 2D waterfall";
        setSpectrum3D(false);
        QTimer::singleShot(3000, this, [this] {
            setLastError(tr("The app stopped while the 3D view was on, so it starts with the 2D waterfall. "
                            "The 3D view can be turned on again in View."));
        });
    }
    update3DGuard();
    connect(this, &MobileEngine::displayLevelsChanged, this, &MobileEngine::update3DGuard);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [] {
        AppSettings::instance().setValue(QStringLiteral("Mobile3DRunning"), 0);
        AppSettings::instance().save();
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            stopTransmit(QStringLiteral("application inactive"));
        }
        update3DGuard();
        qCInfo(lcMobile) << "app state:" << state;
        // Nobody sees the display once the app is in the background: stop
        // feeding the spectrum, waterfall and 3D view (audio keeps playing).
        // iOS kills an app whose main thread is busy when it moves to the
        // background (scene-update watchdog, seen on testers' iPads), and it
        // saves power. Inactive is still on screen (a system prompt or Control
        // Center over it, an unfocused split-screen window): the display keeps
        // going there. A tester's iPhone Duo stayed inactive for minutes while
        // in use, and its display froze.
        const bool active = state == Qt::ApplicationActive;
        const bool onScreen = active || state == Qt::ApplicationInactive;
        // Back in front signed in but without the SmartLink server (it
        // drops in the background): reconnect, so the radio list is live.
        if (active && m_smartLink.isAuthenticated() && !m_smartLink.isConnected()) {
            qCInfo(lcMobile) << "SmartLink: reconnecting to the server (app in front)";
            m_smartLink.reconnect();
        }
        if (!active) {
            // stopTransmit released the paddles and PTT; GameController's
            // keys start from up when the app is back.
            m_sideDit = false;
            m_sideDah = false;
            m_spacePtt = false;
        }
        if (onScreen != m_displayActive) {
            m_displayActive = onScreen;
            qCInfo(lcMobile) << "display" << (onScreen ? "resumed" : "paused (app in the background)");
            markCwDecodedAway(!onScreen);
        }
    });
    connect(&rm, &RadioModel::sliceAdded, this, &MobileEngine::onSliceAdded);
    connect(&rm, &RadioModel::otherClientsChanged, this, [this](int, const QStringList&) { emit stationsChanged(); });
    // Transverter bands follow the radio's xvtr status (infoChanged) and the
    // connection.
    const auto refreshBands = [this] {
        const QStringList now = bands();
        if (now != m_lastBands) {
            m_lastBands = now;
            emit bandsChanged();
        }
    };
    connect(&rm, &RadioModel::infoChanged, this, refreshBands);
    connect(this, &MobileEngine::connectionStateChanged, this, refreshBands);
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int) { applyBeatCancel(); });
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int id) {
        // The split's TX slice closed (here, another client or the radio):
        // transmit goes back to the RX slice; either side gone ends split.
        if (id == m_splitTxId || id == m_splitRxId) {
            if (id == m_splitTxId) {
                if (SliceModel* rx = m_session.radioModel().slice(m_splitRxId)) {
                    rx->setTxSlice(true);
                }
            }
            qCInfo(lcMobile) << "split off: slice" << id << "closed";
            m_splitRxId = m_splitTxId = -1;
            m_splitPending = false;
        }
        selectActiveSlice();
        emit slicesChanged();
    });
    // A reconnect reuses the previous session's slice and pan objects
    // without sliceAdded/panadapterAdded; these are the signals it does send.
    connect(&rm, &RadioModel::slotOccupancyChanged, this, [this](int) {
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panadapterReclaimed, this, [this](PanadapterModel*) { selectActiveSlice(); });
    // The radio's reason when its interlock refuses transmit (no TX slice,
    // TX antenna, band inhibit...), not just "did not accept".
    connect(&rm, &RadioModel::interlockNotificationRequested, this,
            [this](const QString& message, const QString& key, const QString&) {
        qCWarning(lcMobile).noquote() << "transmit refused:" << message << "(" + key + ")";
        m_lastInterlockMs = QDateTime::currentMSecsSinceEpoch();
        setLastError(message);
    });
    // Which slice transmits changes what PTT/TUNE can do.
    connect(this, &MobileEngine::slicesChanged, this, &MobileEngine::transmitStateChanged);
    connect(&rm, &RadioModel::callsignChanged, this, &MobileEngine::callsignChanged);
    connect(&rm, &RadioModel::networkQualityChanged, this, &MobileEngine::networkChanged);
    connect(&rm, &RadioModel::sliceCreateFailed, this, [this](int limit, const QString&) {
        RadioModel& model = m_session.radioModel();
        // The first slice refused on a pan the radio restored from an earlier
        // session (seen with free slices, and another client working at once):
        // close that pan and start on a fresh one, once per connection.
        if (model.slices().isEmpty() && !m_sliceRecoveryTried && !model.panadapters().isEmpty()) {
            m_sliceRecoveryTried = true;
            m_sliceRecoveryPending = true;
            const QList<PanadapterModel*> pans = model.panadapters();
            qCWarning(lcMobile) << "slice refused on the restored pan"
                                << (pans.isEmpty() ? QString() : pans.first()->panId())
                                << ": recreating the panadapter";
            for (PanadapterModel* pan : pans) {
                model.removePanadapter(pan->panId());
            }
            QTimer::singleShot(500, this, [this] { m_session.radioModel().createPanadapter(); });
            return;
        }
        // With no slice at all the app cannot tune: say why and what helps.
        if (model.slices().isEmpty()) {
            setLastError(tr("The radio did not give this app a slice. Its %1 slices may all be "
                            "in use by other programs (SmartSDR, another station): close one "
                            "there, then try again.").arg(limit));
        } else {
            setLastError(tr("The radio allows %1 slices.").arg(limit));
        }
    });
    connect(&rm, &RadioModel::panadapterLimitReached, this, [this](int limit, const QString&) {
        setLastError(tr("The radio allows %1 panadapters.").arg(limit));
    });
    connect(&rm, &RadioModel::panadapterAdded, this, [this](PanadapterModel* pan) {
        sendPanDimensions(pan->panId());
        // The fresh pan of a slice recovery: put the slice on it.
        if (m_sliceRecoveryPending && m_session.radioModel().slices().isEmpty()) {
            m_sliceRecoveryPending = false;
            qCInfo(lcMobile) << "slice recovery: creating the slice on the new pan" << pan->panId();
            const QString panId = pan->panId();
            QTimer::singleShot(400, this, [this, panId] { m_session.radioModel().addSliceOnPan(panId); });
        }
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panadapterRemoved, this, [this](const QString&) {
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panDimensionsNeeded, this, [this](const QString& panId) { sendPanDimensions(panId, true); });
    m_panPreviewEndTimer.setSingleShot(true);
    m_panPreviewEndTimer.setInterval(kPanPreviewHoldMs + 20);
    // Back to the radio's centre (normally the same by now).
    connect(&m_panPreviewEndTimer, &QTimer::timeout, this, &MobileEngine::panGeometryChanged);
    m_panMoveTimer.setSingleShot(true);
    m_panMoveTimer.setInterval(kPanMoveMs);
    connect(&m_panMoveTimer, &QTimer::timeout, this, [this] {
        if (m_pan && m_pendingPanCenterMhz > 0.0) {
            m_session.radioModel().requestPanCenter(m_pan->panId(), m_pendingPanCenterMhz);
        }
    });
    m_panPixelsTimer.setSingleShot(true);
    m_panPixelsTimer.setInterval(kPanPixelsSettleMs);
    connect(&m_panPixelsTimer, &QTimer::timeout, this, [this] {
        if (m_pan) {
            sendPanDimensions(m_pan->panId());
        }
    });

    connect(&rm, &RadioModel::panFeedSpectrumReady, this,
            [this](quint32 streamId, const QVector<float>& bins, qint64, const SpectrumDecodeScale&) {
        if (m_pan && m_pan->panStreamId() == streamId) {
            ++m_fftFramesSeen;
            if (m_displayActive) {
                emit spectrumFrame(bins);
            }
        }
    });
    connect(&rm, &RadioModel::panFeedWaterfallRowReady, this,
            [this](quint32 streamId, const QVector<float>& bins, double lowMhz, double highMhz,
                   quint32, qint64, bool) {
        if (m_pan && m_pan->wfStreamId() == streamId) {
            ++m_wfRowsSeen;
            if (m_displayActive) {
                emit waterfallRow(bins, lowMhz, highMhz);
            }
        } else {
            m_otherWfStreamSeen = streamId;
        }
    });
    connect(&rm.meterModel(), &MeterModel::sLevelChanged, this, [this](int sliceIndex, float dbm) {
        if (m_slice && m_slice->sliceId() == sliceIndex) {
            m_sMeterDbm = dbm;
            emit sMeterChanged();
        }
    });

    // Opus for remote audio: "Auto" compresses only over WAN (SmartLink),
    // where uncompressed PCM would not fit a mobile link. An explicit choice
    // is kept.
    if (AppSettings::instance().value(QStringLiteral("AudioCompression")).toString().isEmpty()) {
        AppSettings::instance().setValue(QStringLiteral("AudioCompression"), QStringLiteral("Auto"));
        AppSettings::instance().save();
    }
    wireSmartLink();
    wireCw();
    if (sideKeysAvailable()) {
        const QPointer<MobileEngine> self = this;
        startSideKeys([self](SideKey key, bool pressed) {
            if (self) {
                self->onSideKey(static_cast<int>(key), pressed);
            }
        });
    }
    // A text field with focus gets the keyboard to itself (GameController's
    // keyboard handler off), with a short record of what reached it.
    {
        auto* diag = new KeyboardDiag(this);
        qApp->installEventFilter(diag);
        qCInfo(lcMobile) << "keyboard: iPad app on a Mac:" << (runningAsIosAppOnMac() ? "yes" : "no");
        if (runningAsIosAppOnMac()) {
            qApp->installEventFilter(new MacTextKeys(this));
            startMacKeyDiagnostics();
            qCInfo(lcMobile) << "keyboard: text fields take the key presses directly (no input method); "
                                "a focused field opens a text box";
        }
        auto textItem = std::make_shared<QPointer<QObject>>();
        auto seenAtFocus = std::make_shared<int>(0);
        connect(qGuiApp, &QGuiApplication::focusObjectChanged, this, [diag, textItem, seenAtFocus](QObject* obj) {
            if (*textItem && *textItem != obj) {
                qCInfo(lcMobile).noquote() << QStringLiteral("keyboard: %1 \"%2\" let go: %3 key and %4 input-method events reached it; GCKeyboard saw %5 keys")
                                                  .arg(QString::fromLatin1((*textItem)->metaObject()->className()), (*textItem)->objectName())
                                                  .arg(diag->keys)
                                                  .arg(diag->inputMethod)
                                                  .arg(sideKeysSeen() - *seenAtFocus);
                *textItem = nullptr;
            }
            const bool text = obj && (obj->inherits("QQuickTextInput") || obj->inherits("QQuickTextEdit"));
            setSideKeysSuspended(text);
            if (text && *textItem != obj) {
                *textItem = obj;
                diag->keys = 0;
                diag->inputMethod = 0;
                *seenAtFocus = sideKeysSeen();
                qCInfo(lcMobile).noquote() << "keyboard: text field focused:" << obj->metaObject()->className() << obj->objectName();
                if (macTextEntryAvailable()) {
                    offerMacTextEntry(obj);
                }
            }
        });
    }

    m_discovery = makeLocalRadioDiscoverySource({.local = true, .simulator = true});
    connect(m_discovery.get(), &RadioDiscoverySource::radioChanged, this,
            [this](const DiscoveredRadio& radio) { m_radios.upsert(radio); });
    connect(m_discovery.get(), &RadioDiscoverySource::radioLost, this,
            [this](const QString& family, const QString& serial) { m_radios.remove(family, serial); });
    m_discovery->start();
}

MobileEngine::~MobileEngine()
{
    s_instance = nullptr;
    // Their workers call into the audio engine, which goes below.
    m_cwxLocalKeyer.reset();
    m_iambicKeyer.reset();
    stopTransmit(QStringLiteral("shutdown"));
    if (radeActive()) {
        deactivateRade();
    }
    // The FT8 slice's handlers call back into the engine; the slices go
    // after the engine's own members.
    for (const QMetaObject::Connection& c : std::as_const(m_digiSliceConnections)) {
        disconnect(c);
    }
    m_digiSliceConnections.clear();
    setKeepScreenAwake(false);
    if (m_discovery) {
        m_discovery->stop();
    }
    m_session.radioModel().disconnectFromRadio();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] {
        audio->stopRxStream();
        delete audio;
    }, Qt::BlockingQueuedConnection);
    m_audio = nullptr;
    m_audioThread->quit();
    m_audioThread->wait();
}

MobileEngine* MobileEngine::instance()
{
    return s_instance;
}

MobileEngine* MobileEngineForeign::create(QQmlEngine*, QJSEngine*)
{
    MobileEngine* engine = MobileEngine::instance();
    Q_ASSERT(engine);
    QJSEngine::setObjectOwnership(engine, QJSEngine::CppOwnership);
    return engine;
}

void MobileEngine::connectToRadio(int row)
{
    const DiscoveredRadio* radio = m_radios.radioAt(row);
    if (!radio || m_connectionState != QLatin1String("idle")) {
        return;
    }
    RadioInfo info;
    info.family = radio->family;
    info.serial = radio->serial;
    info.name = radio->name;
    info.model = radio->model;
    info.nickname = radio->nickname;
    info.version = radio->version;
    info.address = radio->address.isEmpty() ? QHostAddress(QHostAddress::LocalHost)
                                            : QHostAddress(radio->address);
    if (radio->port != 0) {
        info.port = radio->port;
    }
    connectWithInfo(info, radio->nickname.isEmpty() ? radio->model : radio->nickname);
}

void MobileEngine::connectToAddress(const QString& host)
{
    const QHostAddress address(host.trimmed());
    if (address.isNull()) {
        setLastError(tr("“%1” is not an IP address.").arg(host.trimmed()));
        return;
    }
    if (m_connectionState != QLatin1String("idle")) {
        return;
    }
    RadioInfo info;
    info.family = QStringLiteral("flex");
    info.address = address;
    AppSettings::instance().setValue(kLastAddressKey, address.toString());
    AppSettings::instance().save();
    emit lastAddressChanged();
    connectWithInfo(info, address.toString());
}

void MobileEngine::connectWithInfo(const RadioInfo& info, const QString& displayName)
{
    applyAudioCompression(info.address, false);
    m_wanActive = false;
    m_wanReconnectTimer.stop();
    m_radioName = displayName;
    setLastError({});
    setConnectionState(QStringLiteral("connecting"));
    m_session.radioModel().connectToRadio(info);
}

void MobileEngine::connectToDemoWhenListed()
{
    auto tryConnect = [this] {
        for (int row = 0; row < m_radios.rowCount(); ++row) {
            if (m_radios.radioAt(row)->transport == QLatin1String("sim")) {
                connectToRadio(row);
                return true;
            }
        }
        return false;
    };
    if (tryConnect()) {
        return;
    }
    auto pending = std::make_shared<QMetaObject::Connection>();
    *pending = connect(&m_radios, &RadioListModel::countChanged, this, [pending, tryConnect] {
        if (tryConnect()) {
            disconnect(*pending);
        }
    });
}

void MobileEngine::disconnectRadio()
{
    m_wanUserDisconnect = true;
    m_wanReconnectTimer.stop();
    stopTransmit(QStringLiteral("operator disconnect"));
    // RADE's unmute must reach the radio before the link goes, or the radio
    // keeps the slice muted for this app.
    stopRadeBeforeLeaving("disconnect");
    if (m_digiProcRestore) {  // FT8 had switched it off
        m_digiProcRestore = false;
        setProcEnabled(true);
    }
    m_session.radioModel().disconnectFromRadio();
}

// ---- SmartLink ---------------------------------------------------------------

void MobileEngine::wireSmartLink()
{
    connect(&m_smartLink, &SmartLinkClient::authenticated, this, [this] {
        // Auth0 may rotate the refresh token on every refresh; keep the
        // latest. Never an empty one over the kept token: that lost the
        // sign-in after every start (a refresh without rotation had none).
        if (!m_smartLink.refreshToken().isEmpty()) {
            saveSecret(kSmartLinkTokenKey, m_smartLink.refreshToken());
        }
        setSmartLinkState(QStringLiteral("signedIn"));
    });
    connect(&m_smartLink, &SmartLinkClient::authFailed, this, [this](const QString& error) {
        // The server refusing an expired id token is not a wrong password:
        // sign in again once from the kept token before giving it up.
        const QString kept = !m_smartLink.refreshToken().isEmpty() ? m_smartLink.refreshToken()
                                                                   : loadSecret(kSmartLinkTokenKey);
        if (error.contains(QLatin1String("registration invalid")) && !m_smartLinkRetried && !kept.isEmpty()) {
            m_smartLinkRetried = true;
            qCInfo(lcMobile) << "SmartLink refused the session: signing in again from the kept sign-in";
            setSmartLinkState(QStringLiteral("signingIn"));
            m_smartLink.loginWithRefreshToken(kept);
            return;
        }
        m_smartLinkRetried = false;
        qCInfo(lcMobile).noquote() << "SmartLink sign-in failed:" << error;
        removeSecret(kSmartLinkTokenKey);
        m_wanReconnectTimer.stop();
        if (m_connectionState == QLatin1String("connecting") && m_wanActive) {
            m_wanActive = false;
            setConnectionState(QStringLiteral("idle"));
        }
        setLastError(tr("SmartLink: %1").arg(error));
        setSmartLinkState(QStringLiteral("signedOut"));
    });
    connect(&m_smartLink, &SmartLinkClient::serverConnected, this, [this] {
        setSmartLinkState(QStringLiteral("signedIn"));
    });
    connect(&m_smartLink, &SmartLinkClient::serverDisconnected, this, [this] {
        if (m_smartLink.isAuthenticated()) {
            setSmartLinkState(QStringLiteral("offline"));
        }
    });
    connect(&m_smartLink, &SmartLinkClient::radioListReceived, this,
            [this](const QList<WanRadioInfo>& radios) {
        m_smartLinkRetried = false;   // the server took the session
        m_wanRadios = radios;
        emit smartLinkRadiosChanged();
    });
    connect(&m_smartLink, &SmartLinkClient::connectReady, this,
            [this](const QString& handle, const QString& serial) {
        if (!m_wanActive || serial != m_pendingWan.serial) {
            return;
        }
        qCInfo(lcMobile) << "SmartLink connect_ready for" << serial;
        applyAudioCompression(QHostAddress(), true);
        m_wan.connectToRadio(m_pendingWan.publicIp, static_cast<quint16>(m_pendingWan.publicTlsPort), handle);
    });
    // The mismatch fires before WanConnection::connected, so it is taken from
    // the connection itself; the connect stays paused until answered.
    connect(&m_wan, &WanConnection::certFingerprintMismatch, this, &MobileEngine::certificateChanged);
    connect(&m_wan, &WanConnection::errorOccurred, this, [this](const QString& message) {
        setLastError(tr("Remote connection: %1").arg(message));
        if (m_connectionState == QLatin1String("connecting")) {
            setConnectionState(QStringLiteral("idle"));
        }
    });
    connect(&m_wan, &WanConnection::connected, this, [this] {
        if (!m_wanActive) {
            return;
        }
        const quint16 udpPort = m_pendingWan.publicUdpPort > 0
            ? static_cast<quint16>(m_pendingWan.publicUdpPort) : kDefaultWanUdpPort;
        m_session.radioModel().connectViaWan(&m_wan, m_pendingWan.publicIp, udpPort);
    });

    // An unexpected drop of a remote session retries every few seconds until
    // it is back, the user disconnects, or sign-in is needed again.
    m_wanReconnectTimer.setSingleShot(true);
    m_wanReconnectTimer.setInterval(kWanReconnectMs);
    connect(&m_wanReconnectTimer, &QTimer::timeout, this, [this] {
        if (!m_wanActive || m_connected) {
            return;
        }
        if (!m_smartLink.isConnected()) {
            m_smartLink.reconnect();
            m_wanReconnectTimer.start();
            return;
        }
        startWanConnect();
    });

    const QString token = loadSecret(kSmartLinkTokenKey);
    // SmartLinkClient's own QtKeychain store is not built in here (it logs
    // "persistence: disabled"); the app keeps the sign-in itself.
    qCInfo(lcMobile).noquote() << QStringLiteral("SmartLink sign-in kept by the app in the %1: %2")
        .arg(secureStoreAvailable() ? QStringLiteral("platform secure store") : QStringLiteral("(no secure store)"),
             token.isEmpty() ? QStringLiteral("none saved") : QStringLiteral("saved"));
    if (!token.isEmpty()) {
        setSmartLinkState(QStringLiteral("signingIn"));
        m_smartLink.loginWithRefreshToken(token);
    }
}

void MobileEngine::setSmartLinkState(const QString& state)
{
    if (state != m_smartLinkState) {
        m_smartLinkState = state;
        qCInfo(lcMobile) << "SmartLink state" << state;
    }
    emit smartLinkChanged();
}

QString MobileEngine::smartLinkAccount() const
{
    const QString name = (m_smartLink.firstName() + QLatin1Char(' ') + m_smartLink.lastName()).trimmed();
    if (!m_smartLink.callsign().isEmpty()) {
        return name.isEmpty() ? m_smartLink.callsign()
                              : QStringLiteral("%1 (%2)").arg(m_smartLink.callsign(), name);
    }
    return name;
}

QString MobileEngine::smartLinkEmail() const
{
    // SmartLinkClient keeps the last e-mail Base64-encoded for pre-filling.
    const QByteArray stored = AppSettings::instance().value(QStringLiteral("SmartLinkEmail")).toByteArray();
    return QString::fromUtf8(QByteArray::fromBase64(stored));
}

QVariantList MobileEngine::smartLinkRadios() const
{
    QVariantList list;
    for (const WanRadioInfo& radio : m_wanRadios) {
        list.append(QVariantMap{
            {QStringLiteral("nickname"), radio.nickname},
            {QStringLiteral("model"), radio.model},
            {QStringLiteral("serial"), radio.serial},
            {QStringLiteral("status"), radio.status},
            {QStringLiteral("callsign"), radio.callsign},
            {QStringLiteral("reachable"), radio.publicTlsPort > 0 && !radio.publicIp.isEmpty()},
        });
    }
    return list;
}

bool MobileEngine::remoteSession() const
{
    return m_connected && m_session.radioModel().isWan();
}

void MobileEngine::signInSmartLink(const QString& email, const QString& password)
{
    if (email.trimmed().isEmpty() || password.isEmpty()) {
        setLastError(tr("Enter your SmartLink e-mail and password."));
        return;
    }
    setLastError({});
    setSmartLinkState(QStringLiteral("signingIn"));
    m_smartLink.login(email.trimmed(), password);
}

void MobileEngine::signOutSmartLink()
{
    m_wanReconnectTimer.stop();
    m_smartLink.logout();
    removeSecret(kSmartLinkTokenKey);
    m_wanRadios.clear();
    emit smartLinkRadiosChanged();
    setSmartLinkState(QStringLiteral("signedOut"));
}

void MobileEngine::connectSmartLinkRadio(int index)
{
    if (index < 0 || index >= m_wanRadios.size() || m_connectionState != QLatin1String("idle")) {
        return;
    }
    const WanRadioInfo& radio = m_wanRadios.at(index);
    if (radio.publicTlsPort <= 0 || radio.publicIp.isEmpty()) {
        setLastError(tr("%1 is not reachable through SmartLink right now.").arg(radio.nickname));
        return;
    }
    m_pendingWan = radio;
    m_wanActive = true;
    m_wanUserDisconnect = false;
    m_radioName = radio.nickname.isEmpty() ? radio.model : radio.nickname;
    setLastError({});
    setConnectionState(QStringLiteral("connecting"));
    // The SmartLink server connection may have dropped (after a disconnect,
    // in the background, on a network change): sign in again from the kept
    // sign-in first; the retry timer asks for the radio once it is back.
    if (!m_smartLink.isConnected()) {
        qCInfo(lcMobile) << "SmartLink server not connected: reconnecting before the radio";
        m_smartLink.reconnect();
        m_wanReconnectTimer.start();
        return;
    }
    startWanConnect();
}

void MobileEngine::startWanConnect()
{
    RadioModel& rm = m_session.radioModel();
    rm.ensureBackendFamily(QStringLiteral("flex"));
    PanadapterStream* ps = rm.panStream();
    if (!ps) {
        setLastError(tr("Remote connection: no radio stream available."));
        setConnectionState(QStringLiteral("idle"));
        return;
    }
    const QHostAddress address(m_pendingWan.publicIp);
    const quint16 udpPort = m_pendingWan.publicUdpPort > 0
        ? static_cast<quint16>(m_pendingWan.publicUdpPort) : kDefaultWanUdpPort;
    // Bind the stream's UDP socket first: its port is the hole-punch port
    // SmartLink hands the radio.
    if (ps->localPort() == 0) {
        QMetaObject::invokeMethod(ps, [ps, address, udpPort] { ps->startWan(address, udpPort); },
                                  Qt::BlockingQueuedConnection);
    }
    qCInfo(lcMobile) << "SmartLink connect request for" << m_pendingWan.serial;
    m_smartLink.requestConnect(m_pendingWan.serial, ps->localPort());
}

void MobileEngine::answerCertificate(bool accept)
{
    if (accept) {
        m_wan.acceptPresentedCert();
    } else {
        m_wan.rejectPresentedCert();
        m_wanActive = false;
        setConnectionState(QStringLiteral("idle"));
    }
}

QString MobileEngine::lastAddress() const
{
    return AppSettings::instance().value(kLastAddressKey).toString();
}

bool MobileEngine::controlsVisible() const
{
    return AppSettings::instance().value(kControlsVisibleKey, QStringLiteral("True")).toString()
        == QLatin1String("True");
}

void MobileEngine::setControlsVisible(bool visible)
{
    if (visible == controlsVisible()) {
        return;
    }
    AppSettings::instance().setValue(kControlsVisibleKey, visible ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit controlsVisibleChanged();
}

bool MobileEngine::tuneRowsShown() const
{
    return AppSettings::instance().value(kTuneRowsShownKey, QStringLiteral("True")).toString() == QLatin1String("True");
}

void MobileEngine::setTuneRowsShown(bool shown)
{
    if (shown == tuneRowsShown()) {
        return;
    }
    AppSettings::instance().setValue(kTuneRowsShownKey, shown ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit tuneRowsChanged();
}

bool MobileEngine::radioStepButtons() const
{
    return AppSettings::instance().value(kRadioStepButtonsKey, QStringLiteral("False")).toString() == QLatin1String("True");
}

void MobileEngine::setRadioStepButtons(bool radio)
{
    if (radio == radioStepButtons()) {
        return;
    }
    AppSettings::instance().setValue(kRadioStepButtonsKey, radio ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit tuneRowsChanged();
}

// ---- transmit -------------------------------------------------------------

void MobileEngine::wireTransmit()
{
    RadioModel& rm = m_session.radioModel();
    // Host-routed PCM has no continuous contract: stamp it at PTT admission.
    const TxCoordinator::Producer hostProducer = rm.registerTxProducer(m_audio);
    connect(&rm, &RadioModel::localTransmitEngaged, this, [this, hostProducer] {
        const TxCoordinator::Context context = m_session.radioModel().captureTxMedia(hostProducer);
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, context] {
            audio->setHostMicrophoneContext(context);
        }, Qt::QueuedConnection);
    });

    // The radio always forces Opus on remote_audio_tx (SmartSDR v1.4.0.0).
    connect(&rm, &RadioModel::remoteTxStreamReady, this, [this](quint32 streamId) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, streamId] {
            audio->setRemoteTxStreamId(streamId);
            audio->setOpusTxEnabled(true);
        }, Qt::QueuedConnection);
        if (m_micCheckOpen) {
            startDeviceMicrophoneCapture();   // a TX tab shown before the stream came
        }
        emit transmitStateChanged();
    });
    connect(&rm, &RadioModel::txAudioStreamReady, this, [this](quint32 streamId) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, streamId] {
            audio->setTxStreamId(streamId);
            audio->setOpusTxEnabled(true);
        }, Qt::QueuedConnection);
    });
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, [this] {
        defaultMicToDevice();
        if (m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection) {
            releasePtt(QStringLiteral("mic input left this device"));
        }
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::micInputListChanged, this, [this] {
        defaultMicToDevice();
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, &MobileEngine::transmittingChanged);
    connect(&rm.transmitModel(), &TransmitModel::rfPowerChanged, this, &MobileEngine::rfPowerChanged);
    connect(&rm.transmitModel(), &TransmitModel::txFilterCutoffChanged, this, &MobileEngine::txFilterChanged);
    connect(&rm, &RadioModel::globalProfilesChanged, this, &MobileEngine::profilesChanged);
    connect(&rm, &RadioModel::memoryChanged, this, &MobileEngine::memoriesChanged);
    connect(&rm, &RadioModel::memoryRemoved, this, &MobileEngine::memoriesChanged);
    connect(&rm, &RadioModel::memoriesCleared, this, &MobileEngine::memoriesChanged);
    connect(&rm.tnfModel(), &TnfModel::tnfChanged, this, &MobileEngine::tnfsChanged);
    connect(&rm.tnfModel(), &TnfModel::tnfRemoved, this, &MobileEngine::tnfsChanged);
    connect(&rm.tnfModel(), &TnfModel::globalEnabledChanged, this, &MobileEngine::tnfsChanged);
    connect(&rm.transmitModel(), &TransmitModel::profileListChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::micProfileListChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::stateChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, &MobileEngine::updateMicCheck);
    connect(&rm.transmitModel(), &TransmitModel::stateChanged, this, &MobileEngine::updateMicCheck);
    connect(&rm.equalizerModel(), &EqualizerModel::txStateChanged, this, &MobileEngine::eqChanged);
    connect(&rm.equalizerModel(), &EqualizerModel::rxStateChanged, this, &MobileEngine::eqChanged);
    connect(&rm.meterModel(), &MeterModel::micMetersChanged, this,
            [this](float micLevel, float, float micPeak, float compPeak) {
        // This device's microphone is metered from the sent audio instead.
        if (m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection) {
            m_micLevelDbfs = micLevel;
            m_micPeakDbfs = micPeak;
        }
        m_compressionDb = std::clamp(static_cast<double>(compPeak), 0.0, 25.0);
        emit txMetersChanged();
    });
    connect(&rm.meterModel(), &MeterModel::alcValueChanged, this,
            [this](float value, const QString& unit) {
        m_alcDbfs = value;
        m_alcAvailable = !unit.isEmpty();
        emit txMetersChanged();
    });
    connect(&rm.meterModel(), &MeterModel::hwTelemetryChanged, this,
            [this](float paTemp, float supplyVolts) {
        m_paTempC = paTemp;
        m_supplyVolts = supplyVolts;
        emit txMetersChanged();
    });
    connect(&rm, &RadioModel::radioTransmittingChanged, this, &MobileEngine::txMetersChanged);
    // Tune, ATU and radio-side TX audio state.
    TransmitModel& tm = rm.transmitModel();
    connect(&tm, &TransmitModel::tuneChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::tuneAvailabilityChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::tunePowerChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::hasTunerChanged, this, &MobileEngine::atuChanged);
    connect(&tm, &TransmitModel::hasTunerMemoriesChanged, this, &MobileEngine::atuChanged);
    connect(&tm, &TransmitModel::atuStateChanged, this, [this] {
        const TransmitModel& t = m_session.radioModel().transmitModel();
        const auto status = t.atuStatus();
        if (status == ATUStatus::Successful || status == ATUStatus::OK) {
            m_atuTunedFreqMhz = t.transmitFreq();
        } else if (status == ATUStatus::Bypass
                   || status == ATUStatus::ManualBypass) {
            m_atuTunedFreqMhz = -1.0;
        }
        emit atuChanged();
    });
    connect(&tm, &TransmitModel::micStateChanged, this, &MobileEngine::txAudioChanged);
    connect(&tm, &TransmitModel::phoneStateChanged, this, &MobileEngine::phoneChanged);
    DvkModel& dvk = rm.dvkModel();
    connect(&dvk, &DvkModel::recordingChanged, this, &MobileEngine::dvkChanged);
    connect(&dvk, &DvkModel::recordingsLoaded, this, &MobileEngine::dvkChanged);
    connect(&dvk, &DvkModel::licenseRefusedChanged, this, &MobileEngine::dvkChanged);
    connect(&rm, &RadioModel::licenseFeaturesChanged, this, &MobileEngine::dvkChanged);
    connect(&dvk, &DvkModel::commandFailed, this, [this](const QString& verb, int id, uint, const QString& message) {
        qCWarning(lcMobile).noquote() << QStringLiteral("DVK %1 slot %2 refused: %3").arg(verb).arg(id).arg(message);
        setLastError(tr("Voice keyer: %1").arg(message));
    });
    connect(&dvk, &DvkModel::statusChanged, this, [this](DvkModel::Status status, int id) {
        // The radio records the selected mic input, which is this device:
        // the microphone streams for as long as the radio records.
        if (status != DvkModel::Recording && m_dvkRecordingMic) {
            m_dvkRecordingMic = false;
            if (!m_pttHeld) {
                stopDeviceMicrophoneCapture();
            }
            const TxSendStats& st = *m_txSendStats;
            qCInfo(lcMobile).noquote() << QStringLiteral("DVK recorded slot %1: %2 mic packets sent, %3 stopped by the fence")
                                              .arg(m_dvkRecordSlot).arg(static_cast<int>(st.packets))
                                              .arg(static_cast<int>(st.fenced));
        }
        qCInfo(lcMobile) << "DVK" << dvkStateName(status) << "slot" << id;
        emit dvkChanged();
    });
    connect(&rm.meterModel(), &MeterModel::txMetersChanged, this,
            [this](float fwdPower, float swr, bool swrValid) {
        m_forwardPowerW = fwdPower;
        if (m_digiTransmitting) {
            m_digiTxPeakW = std::max(m_digiTxPeakW, double(fwdPower));
            if (swrValid) {
                m_digiTxPeakSwr = std::max(m_digiTxPeakSwr, double(swr));
            }
        }
        m_swr = swr;
        m_swrValid = swrValid;
        emit txMetersChanged();
    });
    connect(&rm, &RadioModel::capabilitiesChanged, this, &MobileEngine::applyTxAudioCapabilities);
    // Ask for microphone permission as soon as transmitting from this device
    // is possible, so the iOS prompt never interrupts a PTT press. Asking
    // does not open the microphone.
    connect(this, &MobileEngine::transmitStateChanged, this, [this] {
        if (transmitAvailable()) {
            ensureMicrophonePermission();
        }
    });
    wireTxPacketSink();
}

void MobileEngine::wireTxPacketSink()
{
    disconnect(m_txSinkConnection);
    PanadapterStream* ps = m_session.radioModel().panStream();
    if (!ps) {
        return;
    }
    // Every TX packet passes the coordinator's dispatch fence before it
    // reaches the radio; a stale or revoked context sends nothing.
    m_txSinkConnection = connect(m_audio, &AudioEngine::txPacketReady, ps,
        [ps, stats = m_txSendStats](const QByteArray& packet, const TxCoordinator::Context& context) {
            const qint64 now = TxCoordinator::monotonicMs();
            const TxCoordinator::Dispatch dispatch = context.beginDispatch(now);
            if (dispatch) {
                ps->sendToRadio(packet);
                const qint64 last = stats->lastMs.exchange(now);
                if (last > 0) {
                    const qint64 gap = now - last;
                    if (gap > stats->maxGapMs) {
                        stats->maxGapMs = gap;
                    }
                } else {
                    stats->firstMs = now;
                }
                ++stats->packets;
                stats->bytes += packet.size();
            } else {
                ++stats->fenced;
            }
        });
}

void MobileEngine::applyTxAudioCapabilities(bool connected, const RadioCapabilities& caps)
{
    m_caps = caps;
    const bool pcAudioRequired = caps.takesTxAudioOverSeam && caps.canTransmit && caps.hostModulates;
    const bool pcAudioEnabled = pcAudioRequired
        || AppSettings::instance().value(QStringLiteral("PcAudioEnabled"), QStringLiteral("True")).toString()
               == QLatin1String("True");
    RadioModel& rm = m_session.radioModel();
    if (!m_microphoneProducer.valid()) {
        m_microphoneProducer = rm.registerTxProducer(m_audio, true);
    }
    const TxCoordinator::Context microphone = connected
        ? rm.captureTxMedia(m_microphoneProducer) : TxCoordinator::Context{};
    AudioEngine* audio = m_audio;
    const QHostAddress address = rm.radioAddress();
    QMetaObject::invokeMethod(audio, [audio, connected, caps, pcAudioEnabled, address, microphone] {
        audio->setMicrophoneContext(microphone);
        audio->applyBackendAudioCapabilities(connected, caps, pcAudioEnabled, address);
    });
    if (connected) {
        rm.notePcAudioEnabled(pcAudioEnabled);
    }
    emit transmitStateChanged();
}

bool MobileEngine::ensureMicrophonePermission()
{
    QMicrophonePermission permission;
    auto request = [this, &permission] {
        qCInfo(lcMobile) << "asking for the microphone";
        qApp->requestPermission(permission, this, [this](const QPermission& result) {
            qCInfo(lcMobile) << "microphone" << (result.status() == Qt::PermissionStatus::Granted ? "allowed" : "refused");
            if (result.status() != Qt::PermissionStatus::Granted) {
                setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
            }
        });
    };
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Granted:
        return true;
    case Qt::PermissionStatus::Denied:
#ifdef Q_OS_ANDROID
        // Android cannot tell "never asked" from "refused" (a fresh install
        // reads Denied): ask. The system shows its dialog again unless the
        // user chose "Don't ask again"; then the refusal comes straight back.
        request();
#else
        setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
#endif
        return false;
    case Qt::PermissionStatus::Undetermined:
        if (!m_micPermissionRequested) {
            m_micPermissionRequested = true;
            request();
        }
        return false;
    }
    return false;
}

void MobileEngine::startDeviceMicrophoneCapture()
{
    // The microphone is open only while PTT is held.
    if (!ensureMicrophonePermission()) {
        return;
    }
    // Make sure the play-and-record session is active before the input opens
    // (it can be lost to another app or an interruption).
    configureAudioSession();
    const float gainDb = float(micGainDb());
    const QHostAddress address = m_session.radioModel().radioAddress();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, gainDb, address] {
        audio->setPcMicGainDb(gainDb);
        if (!audio->isTxStreaming() && audio->hasAnyTxStream()) {
            audio->startTxStream(address, kRadioTxAudioPort);
        }
        // For the Log page: why PTT might carry no audio.
        qCInfo(lcMobile).noquote()
            << QStringLiteral("mic capture: streaming=%1 remoteTx=0x%2 daxTx=0x%3 gain=%4")
                   .arg(audio->isTxStreaming() ? 1 : 0)
                   .arg(QString::number(audio->remoteTxStreamId(), 16))
                   .arg(QString::number(audio->txStreamId(), 16))
                   .arg(QStringLiteral("%1 dB").arg(gainDb, 0, 'f', 0));
    }, Qt::QueuedConnection);
}

void MobileEngine::stopDeviceMicrophoneCapture(bool force)
{
    if (m_micCheckOpen && !force) {
        return;   // a shown TX tab keeps metering it
    }
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] {
        if (audio->isTxStreaming()) {
            audio->stopTxStream();
        }
    }, Qt::QueuedConnection);
}

void MobileEngine::sampleThreads(quint64 elapsedUs)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // /proc/self/task/<tid>/stat: utime and stime (fields 14 and 15, clock
    // ticks) after the name in parentheses; the name also from "comm".
    static const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    QHash<int, quint64> now;
    QList<QPair<double, QString>> busy;
    const QDir tasks(QStringLiteral("/proc/self/task"));
    for (const QString& entry : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile stat(tasks.filePath(entry + QStringLiteral("/stat")));
        if (!stat.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray line = stat.readAll();
        const int open = line.indexOf('(');
        const int close = line.lastIndexOf(')');
        if (open < 0 || close < open) {
            continue;
        }
        const QList<QByteArray> fields = line.mid(close + 2).split(' ');
        if (fields.size() < 13) {
            continue;
        }
        // After ")": state is field 3, so utime (14) and stime (15) sit at 11 and 12.
        const quint64 ticks = fields.at(11).toULongLong() + fields.at(12).toULongLong();
        const int tid = entry.toInt();
        now.insert(tid, ticks);
        const auto before = m_threadTicks.constFind(tid);
        if (before == m_threadTicks.constEnd() || ticks < *before || elapsedUs == 0 || ticksPerSecond <= 0) {
            continue;
        }
        const double percent = 100.0 * double(ticks - *before) / double(ticksPerSecond) * 1e6 / double(elapsedUs);
        if (percent >= 1.0) {
            // The main thread carries the process name: call it what it is.
            QString name = tid == int(getpid()) ? QStringLiteral("UI") : QString::fromUtf8(line.mid(open + 1, close - open - 1));
            if (name == QLatin1String("QSGRenderThread")) {
                name = QStringLiteral("render");
            }
            busy.append({percent, name});
        }
    }
    m_threadTicks = now;
    std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    m_busyThreads.clear();
    for (int i = 0; i < busy.size() && i < 4; ++i) {
        m_busyThreads << QStringLiteral("%1 %2 %").arg(busy.at(i).second).arg(qRound(busy.at(i).first));
    }
#else
    Q_UNUSED(elapsedUs);
#endif
}

bool MobileEngine::micMeterLive() const
{
    if (!m_connected) {
        return false;
    }
    return m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection || radioTransmitting()
        || m_micCheckOpen;
}

void MobileEngine::holdMicCheck(QObject* holder, bool on)
{
    if (!holder) {
        return;
    }
    if (on && !m_micCheckHolders.contains(holder)) {
        m_micCheckHolders.append(holder);
        connect(holder, &QObject::destroyed, this, [this, holder] {
            m_micCheckHolders.removeAll(holder);
            updateMicCheck();
        });
    } else if (!on) {
        m_micCheckHolders.removeAll(holder);
        disconnect(holder, &QObject::destroyed, this, nullptr);
    }
    updateMicCheck();
}

void MobileEngine::updateMicCheck()
{
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    const bool want = !m_micCheckHolders.isEmpty() && m_connected && tm.micSelection() == kDeviceMicSelection
        && !tm.voxEnable();
    if (want == m_micCheckOpen) {
        return;
    }
    if (want) {
        if (!ensureMicrophonePermission()) {
            return;   // asked; the next time the tab shows it opens
        }
        m_micLevelDbfs = -100.0;
        m_micPeakDbfs = -100.0;
        startDeviceMicrophoneCapture();
        m_micCheckOpen = true;
        qCInfo(lcMobile) << "mic check: microphone open for the Mic meter";
    } else {
        m_micCheckOpen = false;
        if (!m_pttHeld && !m_dvkRecordingMic && m_voiceState == QLatin1String("idle")) {
            stopDeviceMicrophoneCapture();
        }
        qCInfo(lcMobile) << "mic check: microphone closed";
    }
    emit txMetersChanged();
}

namespace {
// The app's EQ layout on the client TX EQ: a low cut, a bass shelf and a
// presence peak (the radio's own 8-band TX EQ is in the Audio tab).
constexpr float kBassHz = 250.0f;
constexpr float kPresenceHz = 2000.0f;
bool isAppEqLayout(const ClientEq& eq)
{
    return eq.activeBandCount() >= 3 && eq.band(0).type == ClientEq::FilterType::HighPass
        && eq.band(1).type == ClientEq::FilterType::LowShelf && eq.band(2).type == ClientEq::FilterType::Peak;
}
void ensureAppEqLayout(ClientEq& eq)
{
    if (isAppEqLayout(eq)) {
        return;
    }
    ClientEq::BandParams lowCut;
    lowCut.type = ClientEq::FilterType::HighPass;
    lowCut.freqHz = 100.0f;
    lowCut.enabled = false;
    lowCut.slopeDbPerOct = 12;
    ClientEq::BandParams bass;
    bass.type = ClientEq::FilterType::LowShelf;
    bass.freqHz = kBassHz;
    ClientEq::BandParams presence;
    presence.type = ClientEq::FilterType::Peak;
    presence.freqHz = kPresenceHz;
    presence.q = 1.0f;
    eq.setBand(0, lowCut);
    eq.setBand(1, bass);
    eq.setBand(2, presence);
    eq.setActiveBandCount(3);
}
} // namespace

QVariantMap MobileEngine::txChain() const
{
    ClientGate* gate = m_audio->clientGateTx();
    ClientComp* comp = m_audio->clientCompTx();
    ClientEq* eq = m_audio->clientEqTx();
    if (!gate || !comp || !eq) {
        return {};
    }
    const bool ours = isAppEqLayout(*eq);
    return {
        {QStringLiteral("gateOn"), gate->isEnabled()},
        {QStringLiteral("gateThreshold"), gate->thresholdDb()},
        {QStringLiteral("eqOn"), eq->isEnabled()},
        {QStringLiteral("lowCut"), ours && eq->band(0).enabled ? qRound(eq->band(0).freqHz) : 0},
        {QStringLiteral("bass"), ours ? eq->band(1).gainDb : 0.0f},
        {QStringLiteral("presence"), ours ? eq->band(2).gainDb : 0.0f},
        {QStringLiteral("compOn"), comp->isEnabled()},
        {QStringLiteral("compThreshold"), comp->thresholdDb()},
        {QStringLiteral("compRatio"), comp->ratio()},
        {QStringLiteral("compMakeup"), comp->makeupDb()},
    };
}

void MobileEngine::setTxChain(const QString& key, const QVariant& value)
{
    ClientGate* gate = m_audio->clientGateTx();
    ClientComp* comp = m_audio->clientCompTx();
    ClientEq* eq = m_audio->clientEqTx();
    if (!gate || !comp || !eq) {
        return;
    }
    const float v = value.toFloat();
    if (key == QLatin1String("gateOn")) {
        gate->setEnabled(value.toBool());
    } else if (key == QLatin1String("gateThreshold")) {
        gate->setThresholdDb(std::clamp(v, -80.0f, 0.0f));
    } else if (key == QLatin1String("compOn")) {
        comp->setEnabled(value.toBool());
    } else if (key == QLatin1String("compThreshold")) {
        comp->setThresholdDb(std::clamp(v, -60.0f, 0.0f));
    } else if (key == QLatin1String("compRatio")) {
        comp->setRatio(std::clamp(v, 1.0f, 20.0f));
    } else if (key == QLatin1String("compMakeup")) {
        comp->setMakeupDb(std::clamp(v, -12.0f, 24.0f));
    } else if (key == QLatin1String("eqOn")) {
        ensureAppEqLayout(*eq);
        eq->setEnabled(value.toBool());
    } else if (key == QLatin1String("lowCut") || key == QLatin1String("bass") || key == QLatin1String("presence")) {
        ensureAppEqLayout(*eq);
        const int index = key == QLatin1String("lowCut") ? 0 : key == QLatin1String("bass") ? 1 : 2;
        ClientEq::BandParams band = eq->band(index);
        if (index == 0) {
            band.enabled = v > 0.0f;
            if (band.enabled) {
                band.freqHz = std::clamp(v, 50.0f, 400.0f);
            }
        } else {
            band.gainDb = std::clamp(v, -12.0f, 12.0f);
        }
        eq->setBand(index, band);
    } else {
        return;
    }
    m_txChainSaveTimer.start();
    emit txChainChanged();
}

double MobileEngine::compReductionDb() const
{
    ClientComp* comp = m_audio->clientCompTx();
    return comp && comp->isEnabled() ? std::min(0.0f, comp->gainReductionDb()) : 0.0;
}

bool MobileEngine::transmitAvailable() const
{
    return m_connected && m_caps.canTransmit
        && m_session.radioModel().transmitModel().micSelection() == kDeviceMicSelection;
}

bool MobileEngine::canUseDeviceMicrophone() const
{
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    return m_connected && m_caps.canTransmit && tm.micSelection() != kDeviceMicSelection
        && tm.micInputList().contains(kDeviceMicSelection);
}

QString MobileEngine::transmitUnavailableReason() const
{
    if (!m_connected) {
        return tr("Not connected.");
    }
    if (!m_caps.canTransmit) {
        return tr("This radio is receive-only.");
    }
    // No slice transmits (e.g. the TX slice was closed): the radio's
    // interlock refuses PTT and TUNE.
    const QList<SliceModel*> slices = m_session.radioModel().slices();
    if (!slices.isEmpty()
        && std::none_of(slices.cbegin(), slices.cend(), [](SliceModel* s) { return s->isTxSlice(); })) {
        return tr("No slice transmits: press and hold a slice and choose Make TX slice.");
    }
    const QString mic = m_session.radioModel().transmitModel().micSelection();
    if (mic != kDeviceMicSelection) {
        return tr("The radio takes transmit audio from its %1 input. Switch it to this device's microphone to transmit from here.")
            .arg(mic);
    }
    return {};
}

bool MobileEngine::transmitting() const
{
    return m_session.radioModel().transmitModel().isTransmitting();
}

int MobileEngine::rfPower() const
{
    return m_session.radioModel().transmitModel().rfPower();
}

void MobileEngine::setRfPower(int power)
{
    if (m_connected && m_caps.canTransmit) {
        m_session.radioModel().transmitModel().setRfPower(std::clamp(power, 0, 100));
    }
}

bool MobileEngine::radioTransmitting() const
{
    const RadioModel& rm = m_session.radioModel();
    return rm.isRadioTransmitting() || rm.transmitModel().isTransmitting() || rm.transmitModel().isTuning();
}

bool MobileEngine::hasPaTemp() const
{
    return m_session.radioModel().meterModel().hasPaTemp();
}

bool MobileEngine::hasSupplyVolts() const
{
    return m_session.radioModel().meterModel().hasSupplyVoltage();
}

QStringList MobileEngine::meterKinds() const
{
    return {QStringLiteral("S"), QStringLiteral("Power"), QStringLiteral("SWR"), QStringLiteral("ALC"),
            QStringLiteral("Mic"), QStringLiteral("Comp"), QStringLiteral("PA temp"), QStringLiteral("Volts")};
}

QStringList MobileEngine::meterKindsFor(const QString& side, bool tx) const
{
    QStringList kinds = meterKinds();
    if (!tx) {
        // Read only while transmitting.
        for (const char* txOnly : {"Power", "SWR", "ALC", "Comp"}) {
            kinds.removeAll(QLatin1String(txOnly));
        }
    } else {
        // The S meter has nothing to read while transmitting (the user); a
        // transmit choice of S falls back to the side's default.
        kinds.removeAll(QStringLiteral("S"));
    }
    if (side.startsWith(QLatin1String("right"))) {
        kinds.removeAll(QStringLiteral("Mic"));
    }
    return kinds;
}

QString MobileEngine::meterStyle(const QString& side) const
{
    const bool right = side.startsWith(QLatin1String("right"));
    // Stored 0 bar, 1 analog, 2 two bars, 3 two analog faces. Until chosen: the analog setting
    // (per side, else the older shared one); a left meter whose second
    // transmit bar was chosen (builds 107-112 stacked it while
    // transmitting) keeps two bars.
    const bool analog = displayInt(right ? "MobileMeterAnalogRight" : "MobileMeterAnalogLeft",
                                   displayInt("MobileMeterAnalog", 0)) != 0;
    const int before = analog ? 1
                     : (!right && AppSettings::instance().contains(QStringLiteral("MobileMeterLeft2Tx"))) ? 2 : 0;
    switch (displayInt(right ? "MobileMeterStyleRight" : "MobileMeterStyleLeft", before)) {
    case 1:
        return QStringLiteral("analog");
    case 2:
        return QStringLiteral("two");
    case 3:
        return QStringLiteral("analog2");
    default:
        return QStringLiteral("bar");
    }
}

void MobileEngine::setMeterStyle(const QString& side, const QString& style)
{
    const int v = style == QLatin1String("analog") ? 1 : style == QLatin1String("two") ? 2
                : style == QLatin1String("analog2") ? 3 : 0;
    setDisplayInt(side.startsWith(QLatin1String("right")) ? "MobileMeterStyleRight" : "MobileMeterStyleLeft", v);
}

QString MobileEngine::meterChoice(const char* key, const char* fallback) const
{
    const QString k = QLatin1String(key);
    const QString side = k.contains(QLatin1String("Left")) ? QStringLiteral("left") : QStringLiteral("right");
    const QString kind = AppSettings::instance().value(k, QLatin1String(fallback)).toString();
    return meterKindsFor(side, k.endsWith(QLatin1String("Tx"))).contains(kind) ? kind : QString::fromLatin1(fallback);
}

QString MobileEngine::meterLeft2() const
{
    return meterChoice("MobileMeterLeft2", meterRight() == QLatin1String("Volts") ? "PA temp" : "Volts");
}

QString MobileEngine::meterRight2() const
{
    return meterChoice("MobileMeterRight2", meterRight() == QLatin1String("Volts") ? "PA temp" : "Volts");
}

void MobileEngine::setMeter(const QString& side, bool tx, const QString& kind)
{
    if (!tx && side == QLatin1String("left")) {
        setMeterLeft(kind);
        return;
    }
    if (!tx && side == QLatin1String("right")) {
        setMeterRight(kind);
        return;
    }
    const QString base = side == QLatin1String("left")     ? QStringLiteral("MobileMeterLeft")
                         : side == QLatin1String("left2")  ? QStringLiteral("MobileMeterLeft2")
                         : side == QLatin1String("right2") ? QStringLiteral("MobileMeterRight2")
                                                           : QStringLiteral("MobileMeterRight");
    const QString key = tx ? base + QStringLiteral("Tx") : base;
    if (meterKindsFor(side, tx).contains(kind) && AppSettings::instance().value(key).toString() != kind) {
        AppSettings::instance().setValue(key, kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

QString MobileEngine::meterLeft() const
{
    return meterChoice("MobileMeterLeft", "S");
}

void MobileEngine::setMeterLeft(const QString& kind)
{
    if (meterKindsFor(QStringLiteral("left"), false).contains(kind) && kind != meterLeft()) {
        AppSettings::instance().setValue(QStringLiteral("MobileMeterLeft"), kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

QString MobileEngine::meterRight() const
{
    return meterChoice("MobileMeterRight", "Volts");
}

void MobileEngine::setMeterRight(const QString& kind)
{
    if (meterKindsFor(QStringLiteral("right"), false).contains(kind) && kind != meterRight()) {
        AppSettings::instance().setValue(QStringLiteral("MobileMeterRight"), kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

bool MobileEngine::tuning() const
{
    return m_session.radioModel().transmitModel().isTuning();
}

bool MobileEngine::tuneAvailable() const
{
    return m_connected && m_caps.canTransmit && m_session.radioModel().transmitModel().tuneAvailable();
}

int MobileEngine::tunePower() const
{
    return m_session.radioModel().transmitModel().tunePower();
}

void MobileEngine::setTunePower(int power)
{
    if (m_connected && m_caps.canTransmit) {
        m_session.radioModel().transmitModel().setTunePower(std::clamp(power, 0, 100));
    }
}

bool MobileEngine::hasTuner() const
{
    return m_connected && m_session.radioModel().transmitModel().hasTuner();
}

bool MobileEngine::hasTunerMemories() const
{
    return m_connected && m_session.radioModel().transmitModel().hasTunerMemories();
}

QString MobileEngine::atuStatusText() const
{
    using S = ATUStatus;
    switch (m_session.radioModel().transmitModel().atuStatus()) {
    case S::InProgress: return tr("Tuning…");
    case S::Successful:
    case S::OK: return tr("Tuned");
    case S::Bypass:
    case S::ManualBypass: return tr("Bypass");
    case S::FailBypass:
    case S::Fail: return tr("Tune failed");
    case S::Aborted: return tr("Aborted");
    case S::None:
    case S::NotStarted: return {};
    }
    return {};
}

bool MobileEngine::atuTuned() const
{
    const auto status = m_session.radioModel().transmitModel().atuStatus();
    return status == ATUStatus::Successful || status == ATUStatus::OK;
}

bool MobileEngine::atuMemories() const
{
    return m_session.radioModel().transmitModel().memoriesEnabled();
}

void MobileEngine::setAtuMemories(bool on)
{
    if (hasTunerMemories()) {
        m_session.radioModel().transmitModel().setAtuMemories(on);
    }
}

QString MobileEngine::dvkStateName(int status)
{
    switch (status) {
    case DvkModel::Recording: return QStringLiteral("recording");
    case DvkModel::Preview: return QStringLiteral("preview");
    case DvkModel::Playback: return QStringLiteral("playback");
    case DvkModel::Disabled: return QStringLiteral("unavailable");
    default: return QStringLiteral("idle");
    }
}

bool MobileEngine::radioFeatureLicensed(const QString& feature) const
{
    // SmartSDR+ features: the radio's `license feature` status says which it
    // has. Until it has said, assume present (older firmware never says).
    const RadioModel& rm = m_session.radioModel();
    for (const QString& name : {feature.toLower(), feature.toUpper()}) {
        if (rm.licenseFeatureSeen(name)) {
            return rm.licenseFeatureEnabled(name);
        }
    }
    return true;
}

bool MobileEngine::dvkOnRadio() const
{
    // The radio's DVK needs SmartSDR+; without it the app keeps its own.
    const RadioModel& rm = m_session.radioModel();
    return m_connected && !rm.dvkLicenseRefused() && radioFeatureLicensed(QString(DvkModel::kLicenseFeature));
}

namespace {
QString voiceClipPath(int id)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/voice/F%1.f32").arg(id);
}
QString voiceNameKey(int id) { return QStringLiteral("MobileVoiceName%1").arg(id); }
QString voiceRateKey(int id) { return QStringLiteral("MobileVoiceRate%1").arg(id); }
constexpr int kVoiceClipMaxMs = 15000;
} // namespace

QVariantList MobileEngine::dvkSlots() const
{
    QVariantList list;
    if (dvkOnRadio()) {
        // RadioModel has no const DvkModel accessor; only const DvkModel calls follow.
        const DvkModel& dvk = const_cast<RadioModel&>(m_session.radioModel()).dvkModel();
        for (int id = 1; id <= DvkModel::kSlotCount; ++id) {
            QString name;
            int ms = 0;
            for (const DvkRecording& r : dvk.recordings()) {
                if (r.id == id) {
                    name = r.name;
                    ms = r.durationMs;
                }
            }
            list.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), name.isEmpty() ? DvkModel::defaultName(id) : name},
                {QStringLiteral("seconds"), ms / 1000.0},
            });
        }
        return list;
    }
    // The app's own clips, on this device.
    for (int id = 1; id <= DvkModel::kSlotCount; ++id) {
        const int rate = AppSettings::instance().value(voiceRateKey(id), 0).toInt();
        const qint64 bytes = QFileInfo(voiceClipPath(id)).size();
        const QString name = AppSettings::instance().value(voiceNameKey(id)).toString();
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name.isEmpty() ? tr("Voice %1").arg(id) : name},
            {QStringLiteral("seconds"), rate > 0 ? bytes / static_cast<double>(sizeof(float)) / rate : 0.0},
        });
    }
    return list;
}

QString MobileEngine::dvkState() const
{
    if (!m_connected) {
        return QStringLiteral("unavailable");
    }
    if (!dvkOnRadio()) {
        return m_voiceState;
    }
    return dvkStateName(const_cast<RadioModel&>(m_session.radioModel()).dvkModel().status());
}

int MobileEngine::dvkActiveId() const
{
    if (!dvkOnRadio()) {
        return m_voiceActiveId;
    }
    return const_cast<RadioModel&>(m_session.radioModel()).dvkModel().activeId();
}

void MobileEngine::dvkPlay(int id)
{
    if (!dvkOnRadio()) {
        if (m_voiceState == QLatin1String("playback")) {
            QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
            return;
        }
        if (m_voiceState != QLatin1String("idle")) {
            return;
        }
        QFile file(voiceClipPath(id));
        const int rate = AppSettings::instance().value(voiceRateKey(id), 0).toInt();
        if (rate <= 0 || !file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray clip = file.readAll();
        if (!transmitAvailable()) {
            setLastError(transmitUnavailableReason());
            return;
        }
        // The clip plays in the microphone's place for one PTT press.
        setPtt(true);
        if (!m_pttHeld) {
            return;
        }
        m_voiceState = QStringLiteral("playback");
        m_voiceActiveId = id;
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, clip, rate] { audio->playTxClip(clip, rate); }, Qt::QueuedConnection);
        qCInfo(lcMobile) << "voice macro play F" << id << clip.size() / sizeof(float) / double(rate) << "s";
        emit dvkChanged();
        return;
    }
    DvkModel& dvk = m_session.radioModel().dvkModel();
    if (dvk.status() == DvkModel::Playback) {
        dvk.playbackStop();
        return;
    }
    if (!transmitAvailable()) {
        setLastError(transmitUnavailableReason());
        return;
    }
    if (!dvk.canStartOperation()) {
        return;
    }
    qCInfo(lcMobile) << "DVK play slot" << id;
    dvk.playbackStart(id);
}

void MobileEngine::dvkRecord(int id)
{
    if (!dvkOnRadio()) {
        AudioEngine* audio = m_audio;
        if (m_voiceState == QLatin1String("recording")) {
            QMetaObject::invokeMethod(audio, &AudioEngine::stopTxClipRecording, Qt::QueuedConnection);
            return;
        }
        if (m_voiceState != QLatin1String("idle") || m_pttHeld) {
            return;
        }
        // Recording opens the microphone into a clip; nothing is sent.
        m_voiceState = QStringLiteral("recording");
        m_voiceActiveId = id;
        startDeviceMicrophoneCapture();
        QMetaObject::invokeMethod(audio, [audio] { audio->startTxClipRecording(kVoiceClipMaxMs); },
                                  Qt::QueuedConnection);
        qCInfo(lcMobile) << "voice macro record F" << id;
        emit dvkChanged();
        return;
    }
    DvkModel& dvk = m_session.radioModel().dvkModel();
    if (dvk.status() == DvkModel::Recording) {
        dvk.recStop();
        return;
    }
    if (!dvk.canStartOperation()) {
        return;
    }
    // Recording does not transmit; the device microphone feeds the radio's
    // PC mic input meanwhile.
    m_dvkRecordingMic = true;
    m_dvkRecordSlot = id;
    m_txSendStats->reset();
    startDeviceMicrophoneCapture();
    qCInfo(lcMobile) << "DVK record slot" << id;
    dvk.recStart(id);
}

void MobileEngine::onVoiceClipRecorded(const QByteArray& clip, int rate)
{
    const int id = m_voiceActiveId;
    if (!m_pttHeld) {
        stopDeviceMicrophoneCapture();
    }
    m_voiceState = QStringLiteral("idle");
    if (id >= 1 && id <= DvkModel::kSlotCount && !clip.isEmpty() && rate > 0) {
        QDir().mkpath(QFileInfo(voiceClipPath(id)).absolutePath());
        QFile file(voiceClipPath(id));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(clip);
            AppSettings::instance().setValue(voiceRateKey(id), rate);
            AppSettings::instance().save();
            qCInfo(lcMobile) << "voice macro saved F" << id << clip.size() / sizeof(float) / double(rate) << "s at"
                             << rate << "Hz";
        } else {
            setLastError(tr("The voice macro could not be saved."));
        }
    }
    emit dvkChanged();
}

void MobileEngine::onVoiceClipFinished()
{
    if (m_digiTransmitting) {
        onDigiTxFinished();
        return;
    }
    if (m_voiceState == QLatin1String("playback")) {
        m_voiceState = QStringLiteral("idle");
        releasePtt(QStringLiteral("voice macro done"));
        emit dvkChanged();
    }
}

void MobileEngine::dvkPreview(int id)
{
    if (!dvkOnRadio()) {
        // The app's own clip, played on this device only (nothing is sent).
        if (m_voiceState == QLatin1String("preview")) {
            stopVoicePreview();
            return;
        }
        if (m_voiceState != QLatin1String("idle")) {
            return;
        }
        QFile file(voiceClipPath(id));
        const int rate = AppSettings::instance().value(voiceRateKey(id), 0).toInt();
        if (rate <= 0 || !file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray clip = file.readAll();
        const auto* in = reinterpret_cast<const float*>(clip.constData());
        const qsizetype inFrames = clip.size() / qsizetype(sizeof(float));
        if (inFrames < 2) {
            return;
        }
        // To the receive output's 24 kHz stereo (linear interpolation is
        // plenty for a voice check).
        constexpr int kOutRate = 24000;
        const qsizetype outFrames = inFrames * kOutRate / rate;
        m_voicePreviewPcm.resize(outFrames * 2 * qsizetype(sizeof(float)));
        auto* out = reinterpret_cast<float*>(m_voicePreviewPcm.data());
        for (qsizetype i = 0; i < outFrames; ++i) {
            const double t = double(i) * rate / kOutRate;
            const qsizetype k = std::min<qsizetype>(qsizetype(t), inFrames - 2);
            const float v = in[k] + float(t - k) * (in[k + 1] - in[k]);
            out[2 * i] = v;
            out[2 * i + 1] = v;
        }
        m_voicePreviewPos = 0;
        m_voiceState = QStringLiteral("preview");
        m_voiceActiveId = id;
        m_voicePreviewClock.start();
        if (!m_voicePreviewTimer.isActive()) {
            m_voicePreviewTimer.setInterval(20);
            connect(&m_voicePreviewTimer, &QTimer::timeout, this, &MobileEngine::feedVoicePreview,
                    Qt::UniqueConnection);
            m_voicePreviewTimer.start();
        }
        feedVoicePreview();
        qCInfo(lcMobile) << "voice macro listen F" << id << outFrames / double(kOutRate) << "s";
        emit dvkChanged();
        return;
    }
    DvkModel& dvk = m_session.radioModel().dvkModel();
    if (dvk.status() == DvkModel::Preview) {
        dvk.previewStop();
        return;
    }
    if (dvk.canStartOperation()) {
        dvk.previewStart(id);
    }
}

void MobileEngine::feedVoicePreview()
{
    // Kept ~80 ms ahead of real time: the output trims anything much longer.
    constexpr qsizetype kFrameBytes = 2 * qsizetype(sizeof(float));
    const qsizetype due = (m_voicePreviewClock.elapsed() + 80) * 24 * kFrameBytes;
    const qsizetype end = std::min(due, m_voicePreviewPcm.size());
    if (end > m_voicePreviewPos) {
        const QByteArray chunk = m_voicePreviewPcm.mid(m_voicePreviewPos, end - m_voicePreviewPos);
        m_voicePreviewPos = end;
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, chunk] { audio->feedDecodedSpeech(chunk); }, Qt::QueuedConnection);
    }
    if (m_voicePreviewPos >= m_voicePreviewPcm.size() && due >= m_voicePreviewPcm.size() + 100 * 24 * kFrameBytes) {
        stopVoicePreview();
    }
}

void MobileEngine::stopVoicePreview()
{
    m_voicePreviewTimer.stop();
    m_voicePreviewPcm.clear();
    m_voicePreviewPos = 0;
    if (m_voiceState == QLatin1String("preview")) {
        m_voiceState = QStringLiteral("idle");
        emit dvkChanged();
    }
}

void MobileEngine::dvkStop()
{
    if (!dvkOnRadio()) {
        if (m_voiceState == QLatin1String("preview")) {
            stopVoicePreview();
            return;
        }
        if (m_voiceState == QLatin1String("recording")) {
            QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClipRecording, Qt::QueuedConnection);
        } else if (m_voiceState == QLatin1String("playback")) {
            QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
        }
        return;
    }
    DvkModel& dvk = m_session.radioModel().dvkModel();
    switch (dvk.status()) {
    case DvkModel::Recording: dvk.recStop(); break;
    case DvkModel::Preview: dvk.previewStop(); break;
    case DvkModel::Playback: dvk.playbackStop(); break;
    default: break;
    }
}

void MobileEngine::dvkClear(int id)
{
    if (!dvkOnRadio()) {
        QFile::remove(voiceClipPath(id));
        AppSettings::instance().remove(voiceRateKey(id));
        AppSettings::instance().save();
        emit dvkChanged();
        return;
    }
    m_session.radioModel().dvkModel().clear(id);
}

void MobileEngine::dvkRename(int id, const QString& name)
{
    if (!dvkOnRadio()) {
        AppSettings::instance().setValue(voiceNameKey(id), name.trimmed().left(40));
        AppSettings::instance().save();
        emit dvkChanged();
        return;
    }
    m_session.radioModel().dvkModel().setName(id, DvkModel::sanitizeName(name));
}

QVariantMap MobileEngine::phone() const
{
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    return {
        {QStringLiteral("voxOn"), tm.voxEnable()},
        {QStringLiteral("voxLevel"), tm.voxLevel()},
        {QStringLiteral("voxDelay"), tm.voxDelay()},
        {QStringLiteral("dexpOn"), tm.dexpOn()},
        {QStringLiteral("dexpLevel"), tm.dexpLevel()},
        {QStringLiteral("amCarrier"), tm.amCarrierLevel()},
    };
}

void MobileEngine::setPhone(const QString& key, int value)
{
    if (!m_connected) {
        return;
    }
    TransmitModel& tm = m_session.radioModel().transmitModel();
    const int level = std::clamp(value, 0, 100);
    if (key == QLatin1String("voxOn")) tm.setVoxEnable(value != 0);
    else if (key == QLatin1String("voxLevel")) tm.setVoxLevel(level);
    else if (key == QLatin1String("voxDelay")) tm.setVoxDelay(level);
    else if (key == QLatin1String("dexpOn")) tm.setDexp(value != 0);
    else if (key == QLatin1String("dexpLevel")) tm.setDexpLevel(level);
    else if (key == QLatin1String("amCarrier")) tm.setAmCarrierLevel(level);
}

void MobileEngine::toggleMox()
{
    // Same keying path and refusals as PTT; only the release is the next tap.
    if (transmitting()) {
        setPtt(false, QStringLiteral("MOX off"));
    } else {
        qCInfo(lcMobile) << "MOX on";
        setPtt(true);
    }
}

bool MobileEngine::procEnabled() const
{
    return m_session.radioModel().transmitModel().speechProcessorEnable();
}

void MobileEngine::setProcEnabled(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSpeechProcessorEnable(on);
    }
}

int MobileEngine::procLevel() const
{
    return m_session.radioModel().transmitModel().speechProcessorLevel();
}

void MobileEngine::setProcLevel(int level)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSpeechProcessorLevel(std::clamp(level, 0, procLevelMax()));
    }
}

int MobileEngine::procLevelMax() const
{
    return m_session.radioModel().transmitModel().speechProcessorLevelMaximum();
}

bool MobileEngine::monitor() const
{
    return m_session.radioModel().transmitModel().sbMonitor();
}

void MobileEngine::setMonitor(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSbMonitor(on);
    }
}

bool MobileEngine::dax() const
{
    return m_session.radioModel().transmitModel().daxOn();
}

void MobileEngine::setDax(bool on)
{
    if (m_connected) {
        qCInfo(lcMobile) << "TX: DAX" << (on ? "on" : "off") << "from the TX tab";
        m_session.radioModel().transmitModel().setDax(on);
    }
}

int MobileEngine::monitorGain() const
{
    return m_session.radioModel().transmitModel().monGainSb();
}

void MobileEngine::setMonitorGain(int gain)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setMonGainSb(std::clamp(gain, 0, 100));
    }
}

int MobileEngine::radioMicLevel() const
{
    return m_session.radioModel().transmitModel().micLevel();
}

void MobileEngine::setRadioMicLevel(int level)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setMicLevel(std::clamp(level, 0, 100));
    }
}

void MobileEngine::toggleTune()
{
    RadioModel& rm = m_session.radioModel();
    if (rm.transmitModel().isTuning()) {
        stopTransmit(QStringLiteral("tune button"));
        return;
    }
    if (!tuneAvailable()) {
        qCInfo(lcMobile) << "tune press ignored: tune not available";
        return;
    }
    m_tuneInput = rm.localTxController()->capture(TxController::Activity::Tune);
    if (!m_tuneInput.start()) {
        m_tuneInput.stop();
        qCWarning(lcMobile) << "tune start refused by the TX controller";
        setLastError(tr("The radio did not accept the tune request."));
        return;
    }
    qCInfo(lcMobile) << "tune keyed";
}

void MobileEngine::atuPressed()
{
    if (!hasTuner() || !m_caps.canTransmit) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    m_atuInput = rm.localTxController()->capture(TxController::Activity::Atu);
    // Already tuned on this transmit frequency: a press bypasses the ATU.
    const bool sameFreq = m_atuTunedFreqMhz > 0.0
        && std::abs(rm.transmitModel().transmitFreq() - m_atuTunedFreqMhz) < 1e-6;
    if (atuTuned() && sameFreq) {
        if (!m_atuInput.bypassAtu()) {
            setLastError(tr("The radio did not accept the ATU bypass."));
        }
        qCInfo(lcMobile) << "ATU bypass requested";
        return;
    }
    if (!m_atuInput.start()) {
        m_atuInput.stop();
        setLastError(tr("The radio did not accept the ATU tune request."));
        return;
    }
    qCInfo(lcMobile) << "ATU tune cycle started";
}

void MobileEngine::stopTransmit(const QString& reason)
{
    if (m_digiTransmitting || m_digiState != QLatin1String("idle")) {
        digiHalt();
    }
    releasePtt(reason);
    setCwKey(false, reason);
    releasePaddles();
    if (m_cwxInput.valid()) {
        m_cwxInput.stop();
        m_cwxInput = {};
    }
    if (m_session.radioModel().transmitModel().isTuning() && m_tuneInput.valid()) {
        m_tuneInput.stop();
        qCInfo(lcMobile).noquote() << "tune stopped:" << reason;
    }
    if (m_atuInput.valid()
        && m_session.radioModel().transmitModel().atuStatus() == ATUStatus::InProgress) {
        m_atuInput.stop();
        qCInfo(lcMobile).noquote() << "ATU cycle stopped:" << reason;
    }
}

double MobileEngine::micGainDb() const
{
    AppSettings& settings = AppSettings::instance();
    const QVariant db = settings.value(QStringLiteral("MobileMicGainDb"));
    if (db.isValid()) {
        return std::clamp(db.toDouble(), -20.0, 30.0);
    }
    // Before build 78 the app kept an attenuation-only trim in percent.
    const int percent = settings.value(QStringLiteral("PcMicGain"), 100).toInt();
    return percent >= 100 ? 0.0 : std::max(-20.0, 20.0 * std::log10(std::max(1, percent) / 100.0));
}

void MobileEngine::setMicGainDb(double db)
{
    db = std::clamp(std::round(db), -20.0, 30.0);
    if (std::abs(db - micGainDb()) < 0.01) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobileMicGainDb"), db);
    AppSettings::instance().save();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, db] { audio->setPcMicGainDb(float(db)); }, Qt::QueuedConnection);
    emit micGainChanged();
}

void MobileEngine::defaultMicToDevice()
{
    // On a phone or tablet the only microphone is the device itself, so each
    // connection starts with the radio's mic input on PC. Once per
    // connection: a later change made elsewhere is respected.
    if (!m_micDefaultEnabled || m_micDefaulted || !canUseDeviceMicrophone()) {
        return;
    }
    m_micDefaulted = true;
    qCInfo(lcMobile) << "mic input" << m_session.radioModel().transmitModel().micSelection()
                     << "-> PC (this device)";
    useDeviceMicrophone();
}

void MobileEngine::useDeviceMicrophone()
{
    if (canUseDeviceMicrophone()) {
        m_session.radioModel().transmitModel().setMicSelection(kDeviceMicSelection);
    }
}

void MobileEngine::setPtt(bool down, const QString& reason)
{
    if (!down) {
        releasePtt(reason.isEmpty() ? QStringLiteral("operator release") : reason);
        return;
    }
    if (m_pttHeld) {
        return;
    }
    if (!transmitAvailable()) {
        qCInfo(lcMobile) << "PTT press ignored:" << transmitUnavailableReason();
        return;
    }
    // Voice PTT on a TX slice in DIGU / DIGL: the radio refuses it ("You
    // cannot transmit voice in DIGU/DIGL mode"), and the app said only that
    // the request was not accepted (an Aurora tester). Said plainly, and
    // the microphone stays shut. RADE and FT8 / FT4 transmit there on
    // purpose.
    if (!radeActive() && !m_digiKeying) {
        for (SliceModel* s : m_session.radioModel().slices()) {
            if (s && s->isTxSlice() && (s->mode() == QLatin1String("DIGU") || s->mode() == QLatin1String("DIGL"))) {
                const QString why = tr("The TX slice is in %1: PTT is for voice. Switch it to USB / LSB to talk; FT8 / FT4 run from the FTx tab, RADE from the FreeDV tab.")
                                        .arg(s->mode());
                qCInfo(lcMobile).noquote() << "PTT press ignored:" << why;
                setLastError(why);
                return;
            }
        }
    }
    // The microphone opens first, so its audio is flowing when the radio
    // keys: opened after keying, the first ~0.6 s went out as silence or the
    // capture's start-up noise.
    m_txSendStats->reset();
    // The device mic's level starts over (it is metered only while sent),
    // unless a shown TX tab was metering it already.
    if (!m_micCheckOpen) {
        m_micLevelDbfs = -100.0;
        m_micPeakDbfs = -100.0;
    }
    // With the radio's DAX transmit on, the radio takes its transmit audio
    // from the DAX stream and ignores this device's (no modulation, nothing
    // in the monitor). It is left on by digital-mode software on a PC; with
    // this device's microphone the operator means it to be heard.
    TransmitModel& tm = m_session.radioModel().transmitModel();
    qCInfo(lcMobile).noquote() << QStringLiteral("PTT: mic input %1, DAX transmit %2")
                                      .arg(tm.micSelection(), tm.daxOn() ? QStringLiteral("on") : QStringLiteral("off"));
    if (tm.micSelection() == kDeviceMicSelection && tm.daxOn() && !radeActive()) {
        qCWarning(lcMobile) << "DAX transmit was on: turned off so this device's microphone is heard";
        tm.setDax(false);
    }
    startDeviceMicrophoneCapture();
    m_pttInput = m_session.radioModel().localTxController()->capture(TxController::Activity::Mox);
    m_pttHeld = true;
    if (!m_pttInput.start()) {
        m_pttHeld = false;
        m_pttInput.stop();
        stopDeviceMicrophoneCapture();
        qCWarning(lcMobile) << "PTT start refused by the TX controller";
        // The radio's own reason, when it gave one just now, stays shown.
        if (QDateTime::currentMSecsSinceEpoch() - m_lastInterlockMs > 1000) {
            setLastError(tr("The radio did not accept the transmit request."));
        }
        return;
    }
    qCInfo(lcMobile) << "PTT keyed";
}

void MobileEngine::releasePtt(const QString& reason)
{
    if (!m_pttHeld) {
        return;
    }
    m_pttHeld = false;
    if (m_pttLatched) {
        m_pttLatched = false;
        m_pttToggleLimit.stop();
        emit pttToggleChanged();
    }
    m_pttInput.stop();
    stopDeviceMicrophoneCapture();
    // A voice macro ends with its PTT press.
    if (m_voiceState == QLatin1String("playback")) {
        m_voiceState = QStringLiteral("idle");
        QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxClip, Qt::QueuedConnection);
        emit dvkChanged();
    }
    qCInfo(lcMobile).noquote() << "PTT released:" << reason;
    // What left for the radio during this press: a steady rate and short
    // gaps mean stutter heard on the air comes from the network (VPN) or the
    // radio, long gaps that the phone sent unevenly.
    const TxSendStats& st = *m_txSendStats;
    const int packets = st.packets;
    const qint64 spanMs = packets > 1 ? st.lastMs - st.firstMs : 0;
    // The microphone's real rate: a 10 ms Opus packet each, so 100/s is real
    // time, and the mic must deliver its nominal rate for that.
    const qint64 micSpanMs = m_audio->txMicLastMs() - m_audio->txMicFirstMs();
    const int micRate = micSpanMs > 200 ? qRound(m_audio->txMicFramesDelivered() * 1000.0 / micSpanMs) : 0;
    qCInfo(lcMobile).noquote()
        << QStringLiteral("TX audio: %1 packets in %2 s (%3/s, 100/s is real time), %4 bytes each, send gap max %5 ms, "
                          "%6 stopped by the fence; mic delivered %7 Hz of nominal %8 Hz")
               .arg(packets)
               .arg(spanMs / 1000.0, 0, 'f', 1)
               .arg(spanMs > 0 ? qRound(packets * 1000.0 / spanMs) : 0)
               .arg(packets > 0 ? qRound(static_cast<double>(st.bytes) / packets) : 0)
               .arg(static_cast<qint64>(st.maxGapMs))
               .arg(static_cast<int>(st.fenced))
               .arg(micRate)
               .arg(m_audio->txInputRate());
}

// ---- tuning, bands, filters ------------------------------------------------

void MobileEngine::tuneTo(double mhz)
{
    if (!m_slice || mhz <= 0.0) {
        return;
    }
    if (radeActive() && BandSettings::bandForFrequency(mhz) != BandSettings::bandForFrequency(m_slice->frequency())) {
        stopRadeBeforeLeaving("move to another band");
    }
    if (!m_pan || m_pan->bandwidthMhz() <= 0.0) {
        m_slice->tuneAndRecenter(mhz);
        return;
    }
    m_slice->setFrequency(mhz);
    // Near or past an edge the view follows, keeping the slice
    // kPanFollowEdge of a span inside it, so dragging keeps going up or down
    // the band instead of stopping at the edge of the display.
    const double bandwidth = m_pan->bandwidthMhz();
    const double offset = mhz - panCenterMhz();
    const double limit = bandwidth * (0.5 - kPanFollowEdge);
    if (std::abs(offset) > limit) {
        panTo(offset > 0.0 ? mhz - limit : mhz + limit);
    }
}

void MobileEngine::panTo(double centerMhz)
{
    if (!m_pan || centerMhz <= 0.0) {
        return;
    }
    m_pendingPanCenterMhz = centerMhz;
    if (!m_panMoveTimer.isActive()) {
        m_panMoveTimer.start();
    }
    m_panPreviewMhz = centerMhz;
    m_panPreviewUntilMs = QDateTime::currentMSecsSinceEpoch() + kPanPreviewHoldMs;
    m_panPreviewEndTimer.start();
    emit panGeometryChanged();
}

namespace {
// The radio's transverters (Radio Setup > XVTR in SmartSDR or the desktop),
// in their configured order, as the band policy takes them.
QVector<XvtrPolicy::Transverter> transvertersOf(const RadioModel& rm)
{
    QVector<XvtrPolicy::Transverter> list;
    for (const RadioModel::XvtrInfo& x : rm.xvtrList()) {
        list.append({x.index, x.order, x.name, x.rfFreq, x.ifFreq, x.isValid});
    }
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return (a.order < 0 ? a.index : a.order) < (b.order < 0 ? b.index : b.order);
    });
    return list;
}
} // namespace

QVariantList MobileEngine::transverters() const
{
    QVariantList out;
    if (!m_connected) {
        return out;
    }
    const RadioModel& rm = m_session.radioModel();
    for (const RadioModel::XvtrInfo& x : rm.xvtrList()) {
        const XvtrPolicy::MaxPowerRange range = XvtrPolicy::maxPowerRangeFor(x.ifFreq, rm.model());
        out.append(QVariantMap{{QStringLiteral("index"), x.index},
                               {QStringLiteral("name"), x.name},
                               {QStringLiteral("rfMhz"), x.rfFreq},
                               {QStringLiteral("ifMhz"), x.ifFreq},
                               {QStringLiteral("loMhz"), x.rfFreq - x.ifFreq},
                               {QStringLiteral("loErrorMhz"), x.loError},
                               {QStringLiteral("rxGain"), x.rxGain},
                               {QStringLiteral("maxPowerDbm"), x.maxPower},
                               {QStringLiteral("maxPowerMin"), range.minimumDbm},
                               {QStringLiteral("maxPowerMax"), range.maximumDbm},
                               {QStringLiteral("rxOnly"), x.rxOnly},
                               {QStringLiteral("valid"), x.isValid}});
    }
    return out;
}

void MobileEngine::addTransverter()
{
    if (m_connected) {
        m_session.radioModel().sendCommand(QStringLiteral("xvtr create"));
    }
}

void MobileEngine::setTransverter(int index, const QString& key, const QVariant& value)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    const auto it = rm.xvtrList().constFind(index);
    if (it == rm.xvtrList().cend()) {
        return;
    }
    QString v;
    if (key == QLatin1String("name")) {
        v = value.toString().trimmed().remove(QLatin1Char(' '));
        if (v.isEmpty()) {
            return;
        }
    } else if (key == QLatin1String("rx_only")) {
        v = value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    } else if (key == QLatin1String("max_power")) {
        // Within what the radio allows at this IF (the desktop's clamp).
        v = QString::number(XvtrPolicy::clampMaxPowerDbm(value.toDouble(), it->ifFreq, rm.model()), 'f', 1);
    } else if (key == QLatin1String("rf_freq") || key == QLatin1String("if_freq") || key == QLatin1String("lo_error")
               || key == QLatin1String("rx_gain")) {
        bool ok = false;
        const double d = value.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (!ok) {
            setLastError(tr("Not a number: %1").arg(value.toString()));
            return;
        }
        v = QString::number(d, 'f', key == QLatin1String("lo_error") ? 6 : (key == QLatin1String("rx_gain") ? 1 : 6));
    } else {
        return;
    }
    rm.sendCommand(QStringLiteral("xvtr set %1 %2=%3").arg(index).arg(key, v));
}

void MobileEngine::removeTransverter(int index)
{
    if (m_connected) {
        m_session.radioModel().sendCommand(QStringLiteral("xvtr remove %1").arg(index));
    }
}

bool MobileEngine::fmMode() const
{
    if (!m_slice) {
        return false;
    }
    const QString mode = m_slice->mode();
    return mode == QLatin1String("FM") || mode == QLatin1String("NFM") || mode == QLatin1String("DFM");
}

QString MobileEngine::repeaterDir() const
{
    const QString dir = m_slice ? m_slice->repeaterOffsetDir().toLower() : QString();
    return (dir == QLatin1String("down") || dir == QLatin1String("up")) ? dir : QStringLiteral("simplex");
}

void MobileEngine::setRepeaterDir(const QString& dir)
{
    if (m_slice && (dir == QLatin1String("simplex") || dir == QLatin1String("down") || dir == QLatin1String("up"))) {
        m_slice->setRepeaterOffsetDir(dir);
        // Direction and magnitude are only labels to the radio:
        // tx_offset_freq is what moves the transmitter.
        m_slice->setTxOffsetFreq(SliceModel::txOffsetForDirection(dir, m_slice->fmRepeaterOffsetFreq()));
    }
}

double MobileEngine::repeaterOffsetMhz() const
{
    return m_slice ? m_slice->fmRepeaterOffsetFreq() : 0.0;
}

void MobileEngine::setRepeaterOffsetMhz(double mhz)
{
    if (m_slice && std::isfinite(mhz) && mhz >= 0.0 && mhz <= 100.0) {
        m_slice->setFmRepeaterOffsetFreq(mhz);
        m_slice->setTxOffsetFreq(SliceModel::txOffsetForDirection(m_slice->repeaterOffsetDir(), mhz));
    }
}

double MobileEngine::txOffsetMhz() const
{
    return m_slice ? m_slice->txOffsetFreq() : 0.0;
}

bool MobileEngine::ctcssOn() const
{
    return m_slice && m_slice->fmToneMode().toLower() == QLatin1String("ctcss_tx");
}

void MobileEngine::setCtcssOn(bool on)
{
    if (m_slice) {
        m_slice->setFmToneMode(on ? QStringLiteral("ctcss_tx") : QStringLiteral("off"));
    }
}

QString MobileEngine::ctcssTone() const
{
    bool ok = false;
    const double hz = m_slice ? m_slice->fmToneValue().toDouble(&ok) : 0.0;
    return ok && hz > 0.0 ? QString::number(hz, 'f', 1) : QString();
}

void MobileEngine::setCtcssTone(const QString& tone)
{
    if (m_slice && ctcssTones().contains(tone)) {
        m_slice->setFmToneValue(tone);
    }
}

QStringList MobileEngine::ctcssTones() const
{
    QStringList list;
    for (const CtcssTone& tone : kCtcssTones) {
        list << QString::number(tone.frequency, 'f', 1);
    }
    return list;
}

QString MobileEngine::activeBand() const
{
    if (!m_connected || !m_slice) {
        return {};
    }
    const double mhz = m_slice->frequency();
    // Transverters first, as the desktop: their band is what they tune
    // through the radio's IF range (to 54 MHz), moved by their offset.
    for (const XvtrPolicy::Transverter& x : transvertersOf(m_session.radioModel())) {
        const double offset = x.rfFreqMhz - x.ifFreqMhz;
        if (x.isValid && !x.name.isEmpty() && mhz >= offset && mhz <= offset + 54.0) {
            return x.name;
        }
    }
    for (const double wwv : {2.5, 5.0, 10.0, 15.0, 20.0, 25.0}) {
        if (std::abs(mhz - wwv) < 0.005) {
            return QStringLiteral("WWV");
        }
    }
    const QString band = BandSettings::bandForFrequency(mhz);
    return bands().contains(band) ? band : QStringLiteral("GEN");
}

double MobileEngine::frequencyFromEntry(const QString& entry) const
{
    QString text = entry.trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value) || value <= 0.0) {
        return 0.0;
    }
    // A transverter tunes its RF band through the radio's IF range (to
    // 54 MHz), moved by its offset (RF minus IF).
    const QVector<XvtrPolicy::Transverter> xvtrs =
        m_connected ? transvertersOf(m_session.radioModel()) : QVector<XvtrPolicy::Transverter>{};
    const auto onTransverter = [&xvtrs](double mhz) {
        for (const XvtrPolicy::Transverter& x : xvtrs) {
            const double offset = x.rfFreqMhz - x.ifFreqMhz;
            if (x.isValid && mhz >= offset && mhz <= offset + 54.0) {
                return true;
            }
        }
        return false;
    };
    const auto tunable = [&onTransverter](double mhz) { return mhz <= 54.0 || onTransverter(mhz); };
    const double asMhz = value;
    const double asKhz = value / 1000.0;
    if (text.contains(QLatin1Char('.'))) {
        if (tunable(asMhz)) {
            return asMhz;
        }
        if (tunable(asKhz)) {
            return asKhz;
        }
    } else if (onTransverter(asMhz)) {
        return asMhz;
    }
    return value < 100.0 ? asMhz : asKhz;
}

QStringList MobileEngine::bands() const
{
    QStringList list{QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("60m"), QStringLiteral("40m"),
                     QStringLiteral("30m"), QStringLiteral("20m"), QStringLiteral("17m"), QStringLiteral("15m"),
                     QStringLiteral("12m"), QStringLiteral("10m"), QStringLiteral("6m")};
    // Transverter bands between HF and the utility stacks, as the desktop's
    // band panel: the radio tunes the RF frequency and does the IF itself.
    if (m_connected) {
        for (const XvtrPolicy::Transverter& x : transvertersOf(m_session.radioModel())) {
            if (x.isValid && !x.name.isEmpty() && !list.contains(x.name)) {
                list << x.name;
            }
        }
    }
    // The radio's WWV and general-coverage stacks, as the desktop's band panel.
    list << QStringLiteral("WWV") << QStringLiteral("GEN");
    return list;
}

bool MobileEngine::bandsAvailable() const
{
    return m_connected && m_session.radioModel().usesFlexCommandPlane();
}

void MobileEngine::selectBand(const QString& band)
{
    RadioModel& rm = m_session.radioModel();
    if (!bandsAvailable() || !m_pan) {
        return;
    }
    const auto result = XvtrPolicy::resolveBandStackKey(band, transvertersOf(rm), rm.capabilities());
    if (!result.isSupported()) {
        setLastError(result.unsupportedReason);
        return;
    }
    stopRadeBeforeLeaving("band change");
    armModeAgc();
    rm.requestPanBand(m_pan->panId(), result.key);
    // A band the radio remembers muted (RADE's mute kept by earlier builds
    // when the band changed under it: a tester heard nothing on 40 m until
    // the app was reinstalled): say where the sound went.
    QTimer::singleShot(2000, this, [this] {
        if (m_slice && m_slice->audioMute() && !radeActive() && m_slice->sliceId() != m_splitTxId) {
            setLastError(tr("This slice is muted on this band: tap the speaker on the info panel to hear it."));
        }
    });
}

// RADE mutes the slice and puts it in DIGU / DIGL; the radio keeps a band's
// slice settings when the slice leaves the band, so RADE goes off first and
// the band is remembered as it was before RADE.
void MobileEngine::stopRadeBeforeLeaving(const char* why)
{
    if (!radeActive()) {
        return;
    }
    qCInfo(lcMobile) << "RADE: off before the" << why << "(its mute and mode stay out of the band's memory)";
    deactivateRade();
    emit radeChanged();
}

void MobileEngine::tuneBySteps(int steps)
{
    if (!m_slice || steps == 0) {
        return;
    }
    const qint64 step = stepHz();
    const qint64 hz = std::llround(m_slice->frequency() * 1e6);
    // Land on the grid first, then move: 14.074 37 +1 step(100) -> 14.074 40.
    qint64 base = (hz / step) * step;
    if (steps < 0 && base != hz) {
        base += step;
    }
    tuneTo(static_cast<double>(base + steps * step) / 1e6);
}

void MobileEngine::tuneByHz(int hz)
{
    if (hz == 0) {
        return;
    }
    const qint64 step = std::abs(static_cast<qint64>(hz));
    qint64 freq = std::llround(frequencyMhz() * 1e6);
    // Steps less than 0.5 s apart (a held button) go on from the last target,
    // not from a frequency the radio has not confirmed yet (unless the
    // slice has gone elsewhere meanwhile: a drag, a spot).
    if (m_stepClock.isValid() && m_stepClock.elapsed() < 500 && m_stepTargetHz > 0
        && std::abs(m_stepTargetHz - freq) <= 4 * step) {
        freq = m_stepTargetHz;
    }
    // Floor/ceil to the grid; on the grid, take a full step.
    qint64 target = hz > 0 ? (freq / step + 1) * step
                           : ((freq + step - 1) / step - 1) * step;
    m_stepTargetHz = target;
    m_stepClock.start();
    tuneTo(static_cast<double>(target) / 1e6);
}

void MobileEngine::tuneToSnapped(double mhz)
{
    const qint64 step = stepHz();
    tuneTo(static_cast<double>(std::llround(mhz * 1e6 / step) * step) / 1e6);
}

void MobileEngine::zoom(double factor)
{
    if (factor <= 0.0) {
        return;
    }
    if (!m_pan || m_pan->bandwidthMhz() <= 0.0) {
        setLastError(tr("Zoom is not available: no panadapter is bound yet."));
        qCWarning(lcMobile) << "zoom refused: no bound pan";
        return;
    }
    double bandwidth = m_pan->bandwidthMhz() * factor;
    if (m_pan->bandwidthLimitsKnown() && m_pan->minBandwidthMhz() < m_pan->maxBandwidthMhz()) {
        bandwidth = std::clamp(bandwidth, m_pan->minBandwidthMhz(), m_pan->maxBandwidthMhz());
    }
    // Centre on the slice so the zoom closes in on what is being heard.
    const double center = m_slice ? m_slice->frequency() : m_pan->centerMhz();
    const bool sent = m_session.radioModel().requestPanCenter(m_pan->panId(), center, bandwidth);
    qCInfo(lcMobile) << "zoom pan" << m_pan->panId() << "bandwidth" << m_pan->bandwidthMhz()
                     << "->" << bandwidth << "sent" << sent;
    if (!sent && qFuzzyCompare(bandwidth, m_pan->bandwidthMhz())) {
        setLastError(tr("Already at the radio's span limit."));
    }
}

namespace {
constexpr int kRitXitLimitHz = 9999;
}

bool MobileEngine::ritOn() const { return m_slice && m_slice->ritOn(); }
int MobileEngine::ritHz() const { return m_slice ? m_slice->ritFreq() : 0; }
bool MobileEngine::xitOn() const { return m_slice && m_slice->xitOn(); }
int MobileEngine::xitHz() const { return m_slice ? m_slice->xitFreq() : 0; }

void MobileEngine::setRitOn(bool on)
{
    if (m_slice) {
        m_slice->setRit(on, m_slice->ritFreq());
    }
}

void MobileEngine::setRitHz(int hz)
{
    if (m_slice) {
        m_slice->setRit(m_slice->ritOn(), std::clamp(hz, -kRitXitLimitHz, kRitXitLimitHz));
    }
}

void MobileEngine::setXitOn(bool on)
{
    if (m_slice) {
        m_slice->setXit(on, m_slice->xitFreq());
    }
}

void MobileEngine::setXitHz(int hz)
{
    if (m_slice) {
        m_slice->setXit(m_slice->xitOn(), std::clamp(hz, -kRitXitLimitHz, kRitXitLimitHz));
    }
}

bool MobileEngine::squelchAvailable() const
{
    static const QStringList kNoSquelch{QStringLiteral("DIGU"), QStringLiteral("DIGL"), QStringLiteral("NT"),
                                        QStringLiteral("RTTY"), QStringLiteral("CW"), QStringLiteral("CWL")};
    return m_slice && !kNoSquelch.contains(m_slice->mode());
}

bool MobileEngine::squelchOn() const { return m_slice && m_slice->receiveSquelchOn(); }
int MobileEngine::squelchLevel() const { return m_slice ? m_slice->receiveSquelchLevel() : 0; }

void MobileEngine::setSquelchOn(bool on)
{
    if (m_slice && squelchAvailable()) {
        m_slice->setManualSquelch(on, m_slice->receiveSquelchLevel());
    }
}

void MobileEngine::setSquelchLevel(int level)
{
    if (m_slice && squelchAvailable()) {
        m_slice->setManualSquelch(m_slice->receiveSquelchOn(), std::clamp(level, 0, 100));
    }
}

QString MobileEngine::rxAntenna() const { return m_slice ? m_slice->rxAntenna() : QString(); }
QString MobileEngine::txAntenna() const { return m_slice ? m_slice->txAntenna() : QString(); }

QStringList MobileEngine::rxAntennas() const
{
    if (!m_slice) {
        return {};
    }
    return m_slice->rxAntennaList().isEmpty() ? m_session.radioModel().antennaList() : m_slice->rxAntennaList();
}

QStringList MobileEngine::txAntennas() const
{
    if (!m_slice) {
        return {};
    }
    // The radio's TX list; never offer a made-up port.
    QStringList list = m_slice->txAntennaList();
    if (!m_slice->txAntenna().isEmpty() && !list.contains(m_slice->txAntenna())) {
        list.prepend(m_slice->txAntenna());
    }
    return list;
}

void MobileEngine::setRxAntenna(const QString& antenna)
{
    if (m_slice && rxAntennas().contains(antenna) && antenna != m_slice->rxAntenna()) {
        m_slice->setRxAntenna(antenna);
    }
}

void MobileEngine::setTxAntenna(const QString& antenna)
{
    if (m_slice && txAntennas().contains(antenna) && antenna != m_slice->txAntenna()) {
        m_slice->setTxAntenna(antenna);
    }
}

QVariantList MobileEngine::slices() const
{
    QVariantList list;
    for (SliceModel* s : m_session.radioModel().slices()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), s->sliceId()},
            {QStringLiteral("letter"), s->letter()},
            {QStringLiteral("frequency"), s->frequency()},
            {QStringLiteral("mode"), s->mode()},
            {QStringLiteral("active"), m_slice == s},
            {QStringLiteral("tx"), s->isTxSlice()},
            {QStringLiteral("panId"), s->panId()},
            {QStringLiteral("filterLow"), s->filterLow()},
            {QStringLiteral("filterHigh"), s->filterHigh()},
            {QStringLiteral("gain"), static_cast<double>(s->audioGain())},
            {QStringLiteral("pan"), s->audioPan()},
            {QStringLiteral("mute"), s->audioMute()},
        });
    }
    return list;
}

void MobileEngine::setSliceAudio(int sliceId, const QString& what, double value)
{
    // Per-slice receive audio, as the desktop's RX applet: AF gain (0-100),
    // pan (0 left, 50 centre, 100 right) and mute.
    SliceModel* s = m_session.radioModel().slice(sliceId);
    if (!s) {
        return;
    }
    if (what == QLatin1String("gain")) {
        s->setAudioGain(static_cast<float>(std::clamp(value, 0.0, 100.0)));
    } else if (what == QLatin1String("pan")) {
        s->setAudioPan(std::clamp(qRound(value), 0, 100));
    } else if (what == QLatin1String("mute")) {
        s->setAudioMute(value != 0.0);
    }
}

int MobileEngine::activeSliceId() const
{
    return m_slice ? m_slice->sliceId() : -1;
}

bool MobileEngine::canAddSlice() const
{
    const RadioModel& rm = m_session.radioModel();
    return m_connected && rm.slices().size() < rm.maxSlices();
}

bool MobileEngine::canAddPanadapter() const
{
    const RadioModel& rm = m_session.radioModel();
    return m_connected && rm.usesFlexCommandPlane() && rm.panadapters().size() < rm.maxPanadapters();
}

void MobileEngine::selectSlice(int sliceId)
{
    RadioModel& rm = m_session.radioModel();
    SliceModel* s = rm.slice(sliceId);
    if (!s) {
        return;
    }
    rm.setActivePanId(s->panId());
    if (!s->isActive()) {
        s->setActive(true);
    }
    bindSlice(s);
    bindPan(rm.panadapter(s->panId()));
}

void MobileEngine::addSlice()
{
    if (!canAddSlice()) {
        return;
    }
    // With no slice there is no active pan yet: use the one the radio gave
    // this client (its restored layout), as the retry after a refused slice.
    RadioModel& rm = m_session.radioModel();
    PanadapterModel* pan = m_pan;
    if (!pan && !rm.panadapters().isEmpty()) {
        pan = rm.panadapters().first();
    }
    if (!pan) {
        return;
    }
    setLastError(QString());
    rm.addSliceOnPan(pan->panId());
}

bool MobileEngine::splitActive() const
{
    return m_splitPending || (m_splitTxId >= 0 && m_session.radioModel().slice(m_splitTxId));
}

bool MobileEngine::splitAvailable() const
{
    return splitActive() || (m_slice && canAddSlice() && m_caps.canTransmit);
}

void MobileEngine::toggleSplitUp(int khz)
{
    m_splitUpKhz = splitActive() ? 0 : std::clamp(khz, 0, 100);
    toggleSplit();
    m_splitUpKhz = 0;
}

QVariantMap MobileEngine::panDisplay() const
{
    if (!m_pan) {
        return {};
    }
    return {
        {QStringLiteral("average"), std::max(0, m_pan->average())},
        {QStringLiteral("fps"), m_pan->fps() > 0 ? m_pan->fps() : 25},
        {QStringLiteral("wfRate"), m_pan->waterfallLineDuration() > 0 ? m_pan->waterfallLineDuration() : 50},
        {QStringLiteral("bandZoom"), m_pan->bandZoomOn()},
    };
}

void MobileEngine::setPanDisplay(const QString& key, int value)
{
    if (!m_pan || !m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    const QString id = m_pan->panId();
    const int fps = m_pan->fps() > 0 ? m_pan->fps() : 25;
    const int rate = m_pan->waterfallLineDuration() > 0 ? m_pan->waterfallLineDuration() : 50;
    if (key == QLatin1String("average")) {
        rm.requestPanAverage(id, std::clamp(value, 0, 100));
    } else if (key == QLatin1String("fps")) {
        // The radio accepts fps and line_duration (code 0) but echoes neither
        // in its status (FLEX-6600M, 4.2.20): hold the accepted value here,
        // or the slider would spring back to the old one.
        const int v = std::clamp(value, 5, 60);
        if (rm.requestPanDisplayRates(id, v, rate)) {
            m_pan->setDisplayRates(v, 0);
        }
    } else if (key == QLatin1String("wfRate")) {
        const int v = std::clamp(value, 1, 100);
        if (rm.requestPanDisplayRates(id, fps, v)) {
            m_pan->setDisplayRates(0, v);
        }
    } else if (key == QLatin1String("bandZoom")) {
        // FlexLib's per-pan band zoom: the radio fits the pan to the band
        // its centre is in, and does nothing outside every band (a pan
        // dragged from 40 m down to 6.94 MHz: Band did nothing). Back to a
        // band first: the slice's, else the nearest one.
        if (value && BandSettings::bandForFrequency(m_pan->centerMhz()) == QLatin1String("GEN")) {
            const double center = m_pan->centerMhz();
            double target = 0.0;
            if (m_slice && BandSettings::bandForFrequency(m_slice->frequency()) != QLatin1String("GEN")) {
                target = m_slice->frequency();
            } else {
                double nearest = std::numeric_limits<double>::max();
                for (const BandDef& band : kBands) {
                    const double distance = center < band.lowMhz ? band.lowMhz - center : center - band.highMhz;
                    if (distance < nearest) {
                        nearest = distance;
                        target = (band.lowMhz + band.highMhz) / 2.0;
                    }
                }
            }
            if (target > 0.0) {
                rm.requestPanCenter(id, target);
                qCInfo(lcMobile) << "band zoom: pan centre" << center << "is outside the bands, back to" << target;
            }
        }
        rm.sendCommand(QStringLiteral("display pan set %1 band_zoom=%2").arg(id).arg(value ? 1 : 0));
    }
    qCInfo(lcMobile) << "pan display" << key << value;
}

void MobileEngine::toggleSplit()
{
    RadioModel& rm = m_session.radioModel();
    if (splitActive()) {
        SliceModel* rx = rm.slice(m_splitRxId);
        SliceModel* tx = rm.slice(m_splitTxId);
        stopTransmit(QStringLiteral("split off"));
        if (rx) {
            rx->setTxSlice(true);
        }
        m_splitPending = false;
        m_splitRxId = m_splitTxId = -1;
        if (tx && rm.slices().size() > 1) {
            rm.removeSlice(tx->sliceId());
        }
        if (rx) {
            selectSlice(rx->sliceId());
        }
        qCInfo(lcMobile) << "split off";
        emit slicesChanged();
        return;
    }
    if (!m_slice || !m_pan || !canAddSlice()) {
        setLastError(tr("No free slice for the transmit side of a split."));
        return;
    }
    const QString mode = m_slice->mode();
    const bool cw = mode == QLatin1String("CW") || mode == QLatin1String("CWL") || mode == QLatin1String("CWU");
    const double txMhz = m_slice->frequency() + (m_splitUpKhz > 0 ? m_splitUpKhz / 1000.0 : (cw ? 0.001 : 0.005));
    m_splitRxId = m_slice->sliceId();
    m_splitTxId = -1;
    m_splitPending = true;
    setLastError(QString());
    rm.addSliceOnPan(m_pan->panId(), txMhz);
    emit slicesChanged();
}

void MobileEngine::removeSlice(int sliceId)
{
    RadioModel& rm = m_session.radioModel();
    if (rm.slices().size() <= 1) {
        setLastError(tr("The last slice cannot be closed."));
        return;
    }
    if (SliceModel* s = rm.slice(sliceId); s && s->isTxSlice()) {
        stopTransmit(QStringLiteral("TX slice closed"));
        // Hand transmit to the slice that stays (the active one if it is not
        // the one going): otherwise no slice transmits and PTT/TUNE stop.
        SliceModel* heir = (m_slice && m_slice != s) ? m_slice : nullptr;
        for (SliceModel* o : rm.slices()) {
            if (!heir && o != s) {
                heir = o;
            }
        }
        if (heir) {
            qCInfo(lcMobile) << "TX slice" << s->letter() << "closed: transmit moves to" << heir->letter();
            heir->setTxSlice(true);
        }
    }
    rm.removeSlice(sliceId);
}

void MobileEngine::makeTxSlice(int sliceId)
{
    if (SliceModel* s = m_session.radioModel().slice(sliceId); s && !s->isTxSlice()) {
        stopTransmit(QStringLiteral("TX slice changed"));
        s->setTxSlice(true);
    }
}

int MobileEngine::panCount() const
{
    return m_connected ? static_cast<int>(m_session.radioModel().panadapters().size()) : 0;
}

void MobileEngine::closeOtherPanadapters()
{
    RadioModel& rm = m_session.radioModel();
    if (!m_pan) {
        return;
    }
    const QString keep = m_pan->panId();
    QStringList closing;
    for (PanadapterModel* pan : rm.panadapters()) {
        if (pan && pan->panId() != keep) {
            closing << pan->panId();
        }
    }
    for (SliceModel* s : rm.slices()) {
        if (s->isTxSlice() && closing.contains(s->panId())) {
            stopTransmit(QStringLiteral("TX slice's panadapter closed"));
        }
    }
    qCInfo(lcMobile) << "closing other panadapters" << closing << "keeping" << keep;
    for (const QString& id : std::as_const(closing)) {
        rm.removePanadapter(id);
    }
}

void MobileEngine::addPanadapter()
{
    if (canAddPanadapter()) {
        m_session.radioModel().createPanadapter();
    }
}

// ---- CW ----------------------------------------------------------------------

namespace {
constexpr int kCwDecodedMaxChars = 2000;
const QString kCwDecoderKey = QStringLiteral("MobileCwDecoder");
}

void MobileEngine::wireCw()
{
    RadioModel& rm = m_session.radioModel();
    // Local sidetone follows the key the radio actually sends, and the
    // radio's sidetone, pitch and CW monitor gain.
    connect(&rm, &RadioModel::cwKeyDownChanged, this, [this](bool down) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, down] { audio->setCwKeyDown(down); }, Qt::QueuedConnection);
    });
    auto syncSidetone = [this] {
        const TransmitModel& tm = m_session.radioModel().transmitModel();
        const bool on = tm.cwSidetone();
        const float pitch = static_cast<float>(tm.cwPitch());
        const float volume = static_cast<float>(tm.monGainCw()) / 100.0f;
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, on, pitch, volume] {
            if (CwSidetoneGenerator* sidetone = audio->cwSidetone()) {
                sidetone->setEnabled(on);
                sidetone->setPitchHz(pitch);
                sidetone->setVolume(volume);
            }
        }, Qt::QueuedConnection);
        emit cwChanged();
    };
    connect(&rm.transmitModel(), &TransmitModel::phoneStateChanged, this, syncSidetone);
    connect(&rm.transmitModel(), &TransmitModel::stateChanged, this, &MobileEngine::cwChanged);

    // CWX (memories, CW text, Live) keys in the radio, which tells nothing of
    // the elements: a local keyer plays the same text at the same speed into
    // the sidetone, on its own thread against steady_clock (the desktop's
    // CwxLocalKeyer). Feedback only; it may drift a little from the radio.
    m_cwxLocalKeyer = std::make_unique<CwxLocalKeyer>();
    m_cwxLocalKeyer->setOnKeyDownChange([this](bool down, std::chrono::steady_clock::time_point when) {
        if (m_audio) {
            m_audio->setCwKeyDown(down, when);
        }
        if (down) {
            m_cwxSidetoneDowns.fetch_add(1, std::memory_order_relaxed);
            m_cwxSidetoneDownAt = when;
        } else if (m_cwxSidetoneDownAt.time_since_epoch().count() != 0) {
            m_cwxSidetoneKeyedMs.fetch_add(
                std::chrono::duration_cast<std::chrono::milliseconds>(when - m_cwxSidetoneDownAt).count(),
                std::memory_order_relaxed);
        }
    });
    connect(&rm.cwxModel(), &CwxModel::transmissionRequested, this, [this](const QString& text, int wpm) {
        if (m_audio) {
            m_audio->noteCwOverSpeed(wpm);
        }
        m_cwxSidetoneDowns.store(0, std::memory_order_relaxed);
        m_cwxSidetoneKeyedMs.store(0, std::memory_order_relaxed);
        qCInfo(lcMobile) << "CWX sidetone:" << text.size() << "characters at" << wpm << "WPM";
        if (m_cwxLocalKeyer) {
            m_cwxLocalKeyer->start(text, wpm);
        }
    });
    // Also emitted by the disconnect at shutdown, after the keyer is gone.
    connect(&rm.cwxModel(), &CwxModel::transmissionCancelled, this, [this] {
        if (m_cwxLocalKeyer) {
            m_cwxLocalKeyer->stop();
        }
    });

    // The radio refusing CW (e.g. another client sending CW/CWX) must not
    // leave the key silently dead: say why, at most every few seconds.
    // A FLEX-6600 once refused every key-down this way with no client
    // transmitting, until the radio was restarted; say so then.
    connect(&rm, &RadioModel::cwKeyRefused, this, [this](const QString& message) {
        static QElapsedTimer shown;
        if (!shown.isValid() || shown.elapsed() > 5000) {
            shown.start();
            const quint32 tx = m_session.radioModel().txClientHandle();
            const bool nobodyTransmitting = tx == 0 || tx == m_session.radioModel().ourClientHandle();
            qCWarning(lcMobile).noquote() << "CW refused by the radio:" << message
                << (nobodyTransmitting ? "(no client transmitting)" : "(another client transmitting)");
            setLastError(nobodyTransmitting && message.contains(QLatin1String("another client"))
                ? tr("The radio did not key CW: %1. No client is transmitting, so restarting the radio "
                     "should clear this.").arg(message)
                : tr("The radio did not key CW: %1").arg(message));
        }
    });
    // The radio's transmit state in the log, and whose it is.
    connect(&rm, &RadioModel::radioTransmittingChanged, this, [this](bool on) {
        static int last = -1;   // the signal also repeats an unchanged state
        if (int(on) == last) {
            return;
        }
        last = int(on);
        RadioModel& model = m_session.radioModel();
        qCInfo(lcMobile).noquote() << "radio transmitting:" << (on ? "yes" : "no")
            << (on ? (model.txClientHandle() == model.ourClientHandle() ? "(this app)" : "(another client)") : "");
    });

    // Paddles: the elements are timed here (worker thread, steady_clock),
    // as the desktop's local iambic keyer; each edge goes to the radio over
    // NetCW at its scheduled instant and to the sidetone in the same step.
    m_iambicKeyer = std::make_unique<IambicKeyer>();
    m_iambicKeyer->setOnKeyDownChange([this](bool down, std::chrono::steady_clock::time_point when) {
        if (m_audio) {
            m_audio->setCwKeyDown(down, when);
        }
        m_paddleKeyDown.store(down, std::memory_order_relaxed);
        if (down) {
            m_paddleElements.fetch_add(1, std::memory_order_relaxed);
        }
        QMetaObject::invokeMethod(this, &MobileEngine::cwKeyDownChanged, Qt::QueuedConnection);
    });
    m_iambicKeyer->setOnRoutedKeyDownChange([this](bool down, std::chrono::steady_clock::time_point when,
                                                   const TxCoordinator::Request& input) {
        m_session.radioModel().queueProducerCwKeyEdge(input, down, QStringLiteral("cw:paddles"), 0, 0, when);
    });
    connect(&rm.transmitModel(), &TransmitModel::phoneStateChanged, this, &MobileEngine::syncIambicKeyer);

    m_cwDecoder = std::make_unique<CwRxModel>();
    m_cwAudio = std::make_unique<DecoderAudioModel>(rm, DecoderAudioModel::Consumer::Cw);
    connect(m_cwAudio.get(), &DecoderAudioModel::nativePcmReady, m_cwDecoder.get(), &CwRxModel::feed);
    connect(m_cwAudio.get(), &DecoderAudioModel::pcmReady, m_cwDecoder.get(), &CwRxModel::feedFixed24);
    connect(m_cwAudio.get(), &DecoderAudioModel::sourceReset, m_cwDecoder.get(), &CwRxModel::reset);
    connect(m_cwDecoder.get(), &CwRxModel::textDecoded, this, [this](const QString& text, float cost) {
        m_cwCostBins[std::clamp(int(cost * 10.0f), 0, 9)] += int(text.size());
        // As the desktop's Sens slider: uncertain letters (noise) stay out.
        if (cost >= 1.0f - cwDecoderSensitivity() / 100.0f * 0.9f) {
            m_cwDropped += int(text.size());
            return;
        }
        m_cwShown += int(text.size());
        appendCwDecoded(text);
        emit cwDecoderChanged();
    });
    connect(m_cwDecoder.get(), &CwRxModel::statsUpdated, this, [this](float pitch, float speed) {
        m_cwDecoderStats = QStringLiteral("%1 WPM  %2 Hz").arg(qRound(speed)).arg(qRound(pitch));
        emit cwDecoderChanged();
    });
    connect(this, &MobileEngine::modeChanged, this, &MobileEngine::updateCwDecoder);
}

int MobileEngine::cwDecoderSensitivity() const
{
    // 70 by default (the desktop's 30 let most band noise through here:
    // measured, noise letters sit at cost 0.5-0.8, a clean signal's below 0.1).
    return std::clamp(AppSettings::instance().value(QStringLiteral("MobileCwDecoderSensitivity"), QStringLiteral("70")).toInt(), 0, 100);
}

void MobileEngine::setCwDecoderSensitivity(int sens)
{
    sens = std::clamp(sens, 0, 100);
    if (sens != cwDecoderSensitivity()) {
        AppSettings::instance().setValue(QStringLiteral("MobileCwDecoderSensitivity"), QString::number(sens));
        AppSettings::instance().save();
        emit cwDecoderChanged();
    }
}

// The decoder keeps listening with the app in the background (a tester left
// it on CW decode): marks in its text show what came while away. Back with
// nothing new, the away mark goes again.
void MobileEngine::markCwDecodedAway(bool away)
{
    const QString now = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (away) {
        if (!m_cwDecoder || !m_cwDecoder->isRunning()) {
            m_cwAwayMark.clear();
            return;
        }
        m_cwAwayMark = QStringLiteral("\n[away %1]\n").arg(now);
        m_cwStripBeforeAway = m_cwStripStart + m_cwStrip.size();
        appendCwDecoded(m_cwAwayMark);
        m_cwStripAfterAway = m_cwStripStart + m_cwStrip.size();
    } else if (!m_cwAwayMark.isEmpty()) {
        if (m_cwDecodedText.endsWith(m_cwAwayMark)) {
            m_cwDecodedText.chop(m_cwAwayMark.size());
            // The strip takes the mark back too, when nothing came after it.
            if (m_cwStripStart + m_cwStrip.size() == m_cwStripAfterAway && m_cwStripBeforeAway >= m_cwStripStart) {
                m_cwStrip.truncate(int(m_cwStripBeforeAway - m_cwStripStart));
            }
        } else {
            appendCwDecoded(QStringLiteral("\n[back %1]\n").arg(now));
        }
        m_cwAwayMark.clear();
    } else {
        return;
    }
    emit cwDecoderChanged();
}

// Decoded (and sent) CW: the text as received, and the strip over the
// waterfall: whitespace runs as one space, each character at a fixed place
// in the whole stream (m_cwStripStart counts the ones dropped from its
// front), so the strip's lines fill left to right, top then bottom, as
// writing does, and do not shift when old text is dropped.
void MobileEngine::appendCwDecoded(const QString& text)
{
    m_cwDecodedText += text;
    if (m_cwDecodedText.size() > kCwDecodedMaxChars) {
        m_cwDecodedText = m_cwDecodedText.right(kCwDecodedMaxChars);
    }
    for (const QChar c : text) {
        if (c.isSpace()) {
            if (!m_cwStrip.isEmpty() && !m_cwStrip.endsWith(QLatin1Char(' '))) {
                m_cwStrip += QLatin1Char(' ');
            }
        } else {
            m_cwStrip += c;
        }
    }
    constexpr int kStripMaxChars = 600;
    if (m_cwStrip.size() > kStripMaxChars) {
        const int drop = int(m_cwStrip.size()) - kStripMaxChars;
        m_cwStrip.remove(0, drop);
        m_cwStripStart += drop;
    }
}

void MobileEngine::updateCwDecoder()
{
    syncDigiAudio();
    if (!m_cwDecoder || !m_cwAudio) {
        return;
    }
    const bool run = m_connected && cwDecoderEnabled() && cwMode() && m_slice;
    m_cwAudio->setSlice(run ? m_slice.data() : nullptr);
    m_cwAudio->setEnabled(run);
    if (run && !m_cwDecoder->isRunning()) {
        // Hand-sent and keyer CW, not noise read as 60 WPM elements.
        m_cwDecoder->setSpeedRange(8, 45);
        m_cwDecoder->start();
    } else if (!run && m_cwDecoder->isRunning()) {
        m_cwDecoder->stop();
    }
}

bool MobileEngine::cwMode() const
{
    return m_slice && (m_slice->mode() == QLatin1String("CW") || m_slice->mode() == QLatin1String("CWL"));
}

bool MobileEngine::cwKeyAvailable() const
{
    return m_connected && m_caps.canTransmit;
}

bool MobileEngine::cwxAvailable() const
{
    return cwKeyAvailable() && m_session.radioModel().hasRadioSideCwKeyer();
}

int MobileEngine::cwSpeed() const { return m_session.radioModel().transmitModel().cwSpeed(); }
int MobileEngine::cwPitch() const { return m_session.radioModel().transmitModel().cwPitch(); }
bool MobileEngine::cwBreakIn() const { return m_session.radioModel().transmitModel().cwBreakIn(); }

void MobileEngine::setCwSpeed(int wpm)
{
    if (!m_connected) {
        return;
    }
    wpm = std::clamp(wpm, 5, 100);
    m_session.radioModel().transmitModel().setCwSpeed(wpm);
    m_session.radioModel().cwxModel().setSpeed(wpm);
}

void MobileEngine::setCwPitch(int hz)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setCwPitch(std::clamp(hz, 100, 6000));
    }
}

void MobileEngine::setCwBreakIn(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setCwBreakIn(on);
    }
}

namespace {
// Twelve, as the radio's CWX macros (F1-F12 on a keyboard).
constexpr int kCwMemoryCount = 12;
QString cwMemoryKey(int index)
{
    return QStringLiteral("MobileCwMemory%1").arg(index + 1);
}
const char* const kCwMemoryDefaults[kCwMemoryCount] = {
    "CQ CQ CQ DE {CALL} {CALL} K", "{CALL}", "5NN TU", "TU 73", "QRZ?", "AGN?",
    "QRL?", "R R TU 73", "", "", "", "",
};
} // namespace

QStringList MobileEngine::cwMemories() const
{
    QStringList list;
    for (int i = 0; i < kCwMemoryCount; ++i) {
        list << AppSettings::instance().value(cwMemoryKey(i), QString::fromLatin1(kCwMemoryDefaults[i])).toString();
    }
    return list;
}

void MobileEngine::setCwMemory(int index, const QString& text)
{
    if (index < 0 || index >= kCwMemoryCount) {
        return;
    }
    AppSettings::instance().setValue(cwMemoryKey(index), text.trimmed().toUpper());
    AppSettings::instance().save();
    emit cwMemoriesChanged();
}

void MobileEngine::sendCwMemory(int index)
{
    const QStringList list = cwMemories();
    if (index < 0 || index >= list.size() || list.at(index).isEmpty()) {
        return;
    }
    QString text = list.at(index);
    const QString call = m_session.radioModel().callsign().trimmed().toUpper();
    if (text.contains(QLatin1String("{CALL}")) && call.isEmpty()) {
        setLastError(tr("The radio has no callsign for {CALL}. Set it in Radio Setup (Setup › Radio › Callsign)."));
        return;
    }
    text.replace(QLatin1String("{CALL}"), call);
    if (text.contains(QLatin1String("{HIS}")) && m_workedCall.isEmpty()) {
        setLastError(tr("No call for {HIS}: pick a heard call on the CW tab or type it in the Log tab's Call field."));
        return;
    }
    text.replace(QLatin1String("{HIS}"), m_workedCall);
    sendCwx(text);
}

int MobileEngine::txFilterLow() const
{
    return m_session.radioModel().transmitModel().txFilterLow();
}

int MobileEngine::txFilterHigh() const
{
    return m_session.radioModel().transmitModel().txFilterHigh();
}

void MobileEngine::setTxFilterLow(int hz)
{
    if (m_connected && hz != txFilterLow()) {
        m_session.radioModel().transmitModel().setTxFilterLow(hz);
    }
}

void MobileEngine::setTxFilterHigh(int hz)
{
    if (m_connected && hz != txFilterHigh()) {
        m_session.radioModel().transmitModel().setTxFilterHigh(hz);
    }
}

QStringList MobileEngine::eqBandLabels() const
{
    QStringList labels;
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        labels << EqualizerModel::bandLabel(static_cast<EqualizerModel::Band>(b));
    }
    return labels;
}

bool MobileEngine::eqTxEnabled() const
{
    return m_session.radioModel().equalizerModel().txEnabled();
}

bool MobileEngine::eqRxEnabled() const
{
    return m_session.radioModel().equalizerModel().rxEnabled();
}

QVariantList MobileEngine::eqTxBands() const
{
    QVariantList list;
    const EqualizerModel& eq = m_session.radioModel().equalizerModel();
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        list << eq.txBand(static_cast<EqualizerModel::Band>(b));
    }
    return list;
}

QVariantList MobileEngine::eqRxBands() const
{
    QVariantList list;
    const EqualizerModel& eq = m_session.radioModel().equalizerModel();
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        list << eq.rxBand(static_cast<EqualizerModel::Band>(b));
    }
    return list;
}

void MobileEngine::setEqEnabled(bool tx, bool on)
{
    if (!m_connected) {
        return;
    }
    EqualizerModel& eq = m_session.radioModel().equalizerModel();
    tx ? eq.setTxEnabled(on) : eq.setRxEnabled(on);
}

void MobileEngine::setEqBand(bool tx, int band, int dB)
{
    if (!m_connected || band < 0 || band >= EqualizerModel::BandCount) {
        return;
    }
    const auto b = static_cast<EqualizerModel::Band>(band);
    dB = std::clamp(dB, -10, 10);
    EqualizerModel& eq = m_session.radioModel().equalizerModel();
    if ((tx ? eq.txBand(b) : eq.rxBand(b)) != dB) {
        tx ? eq.setTxBand(b, dB) : eq.setRxBand(b, dB);
    }
}

void MobileEngine::resetEq(bool tx)
{
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        setEqBand(tx, b, 0);
    }
}

QStringList MobileEngine::globalProfiles() const { return m_session.radioModel().globalProfiles(); }
QString MobileEngine::activeGlobalProfile() const { return m_session.radioModel().activeGlobalProfile(); }
QStringList MobileEngine::txProfiles() const { return m_session.radioModel().transmitModel().profileList(); }
QString MobileEngine::activeTxProfile() const { return m_session.radioModel().transmitModel().activeProfile(); }
QStringList MobileEngine::micProfiles() const { return m_session.radioModel().transmitModel().micProfileList(); }
QString MobileEngine::activeMicProfile() const { return m_session.radioModel().transmitModel().activeMicProfile(); }

namespace {
QString quotedProfileName(const QString& name)
{
    QString n = name.trimmed();
    n.remove(QLatin1Char('"'));
    return QLatin1Char('"') + n + QLatin1Char('"');
}
} // namespace

void MobileEngine::loadProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()) {
        return;
    }
    // A profile changes transmit settings (or the whole radio layout); never
    // load one under an active transmission.
    stopTransmit(QStringLiteral("profile load"));
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "load" << type << "profile" << name;
    if (type == QLatin1String("global")) {
        rm.loadGlobalProfile(name);
    } else if (type == QLatin1String("transmit")) {
        rm.transmitModel().loadProfile(name);
    } else if (type == QLatin1String("mic")) {
        rm.transmitModel().loadMicProfile(name);
    }
}

void MobileEngine::saveProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "save" << type << "profile" << name;
    if (type == QLatin1String("global")) {
        rm.sendCommand(QStringLiteral("profile global save ") + quotedProfileName(name));
    } else if (type == QLatin1String("transmit") || type == QLatin1String("mic")) {
        const QStringList& existing = type == QLatin1String("mic") ? micProfiles() : txProfiles();
        if (existing.contains(name.trimmed())) {
            setLastError(tr("A %1 profile named “%2” exists. The radio cannot overwrite it: "
                            "delete it first, then save again.").arg(type, name.trimmed()));
            return;
        }
        rm.sendCommand(QStringLiteral("profile %1 create ").arg(type) + quotedProfileName(name));
    }
}

void MobileEngine::deleteProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()
        || (type != QLatin1String("global") && type != QLatin1String("transmit") && type != QLatin1String("mic"))) {
        return;
    }
    qCInfo(lcMobile) << "delete" << type << "profile" << name;
    m_session.radioModel().sendCommand(QStringLiteral("profile %1 delete ").arg(type) + quotedProfileName(name));
}

int MobileEngine::agcThreshold() const
{
    return m_slice ? m_slice->agcThreshold() : 0;
}

void MobileEngine::setAgcThreshold(int value)
{
    value = std::clamp(value, 0, 100);
    if (m_slice && m_slice->agcThreshold() != value) {
        m_slice->setAgcThreshold(value);
    }
}

int MobileEngine::rfGain() const
{
    return m_pan ? m_pan->rfGain() : 0;
}

QString MobileEngine::panScaleInfo() const
{
    if (!m_pan) {
        return QStringLiteral("no pan");
    }
    return QStringLiteral("ypixels %1, scale %2..%3 dBm")
        .arg(m_pan->fftYPixels())
        .arg(m_pan->minDbm(), 0, 'f', 0)
        .arg(m_pan->maxDbm(), 0, 'f', 0);
}

int MobileEngine::rfGainLow() const
{
    return m_pan && m_pan->hasRfGainRange() ? m_pan->rfGainLow() : -8;
}

int MobileEngine::rfGainHigh() const
{
    return m_pan && m_pan->hasRfGainRange() ? m_pan->rfGainHigh() : 32;
}

int MobileEngine::rfGainStep() const
{
    return m_pan && m_pan->hasRfGainRange() && m_pan->rfGainStep() > 0 ? m_pan->rfGainStep() : 8;
}

void MobileEngine::setRfGain(int gain)
{
    if (!m_pan || !m_connected) {
        return;
    }
    gain = std::clamp(gain, rfGainLow(), rfGainHigh());
    if (gain != m_pan->rfGain()) {
        m_session.radioModel().setPanRfGainFor(m_pan->panId(), gain);
    }
}

QVariantList MobileEngine::recentQsos() const
{
    QVariantList out;
    const QList<QVariantMap>& records = m_qsoLog->recent();
    for (qsizetype i = records.size() - 1; i >= 0; --i) {
        out << records.at(i);
    }
    return out;
}

int MobileEngine::qsoCount() const
{
    return static_cast<int>(m_qsoLog->count());
}

QString MobileEngine::qrzUser() const
{
    return QrzLookupSettings::username();
}

QString MobileEngine::qrzStatus() const
{
    return m_qrzStatus;
}

QString MobileEngine::qrzStatusKind() const
{
    return m_qrzStatusKind;
}

QString MobileEngine::myGrid() const
{
    const QString gps = m_session.radioModel().gpsGrid();
    if (!gps.isEmpty()) {
        return gps;
    }
    return AppSettings::instance().value(QStringLiteral("MobileMyGrid")).toString();
}

void MobileEngine::setMyGrid(const QString& grid)
{
    AppSettings::instance().setValue(QStringLiteral("MobileMyGrid"), grid.trimmed().toUpper());
    AppSettings::instance().save();
    emit qrzChanged();
}

void MobileEngine::setQrzAccount(const QString& user, const QString& password)
{
    CallsignLookupService& lookup = CallsignLookupService::instance();
    QrzLookupSettings::setUsername(user.trimmed());
    if (user.trimmed().isEmpty()) {
        removeSecret(QStringLiteral("qrz_password"));
        lookup.savePassword(QString());
    } else {
        if (!password.isEmpty()) {
            saveSecret(QStringLiteral("qrz_password"), password);
        }
        lookup.savePassword(password.isEmpty() ? loadSecret(QStringLiteral("qrz_password")) : password);
    }
    lookup.reloadConfiguration();
    qCInfo(lcMobile) << "QRZ.com account" << (user.trimmed().isEmpty() ? "removed" : "set");
    m_qrzStatus.clear();
    m_qrzStatusKind.clear();
    const QString pass = password.isEmpty() ? loadSecret(QStringLiteral("qrz_password")) : password;
    if (!user.trimmed().isEmpty()) {
        if (pass.isEmpty()) {
            m_qrzStatusKind = QStringLiteral("failed");
            m_qrzStatus = tr("Type the QRZ.com password too.");
        } else {
            m_qrzStatusKind = QStringLiteral("checking");
            m_qrzStatus = tr("Signing in to QRZ.com…");
            lookup.testLogin(user.trimmed(), pass);
        }
    }
    emit qrzChanged();
    if (!m_lookupCall.isEmpty()) {
        lookup.lookup(m_lookupCall, true);
    }
}

void MobileEngine::lookupCallsign(const QString& call)
{
    const QString c = call.trimmed().toUpper();
    m_lookupCall = c;
    m_callLookup.clear();
    emit callLookupChanged();
    if (c.size() < 3) {
        return;
    }
    CallsignLookupService& lookup = CallsignLookupService::instance();
    if (!m_session.radioModel().callsign().isEmpty()) {
        lookup.setOwnCallsign(m_session.radioModel().callsign());
    }
    lookup.lookup(c);
}

void MobileEngine::applyCallInfo(const CallsignInfo& info)
{
    const QVariantMap m = callInfoMap(info);
    completePendingQso(info.call, m);
    if (info.call.compare(m_lookupCall, Qt::CaseInsensitive) != 0) {
        return;
    }
    m_callLookup = m;
    emit callLookupChanged();
}

QVariantMap MobileEngine::callInfoMap(const CallsignInfo& info) const
{
    QVariantMap m{{QStringLiteral("call"), info.call}, {QStringLiteral("name"), info.prefixOnly ? QString() : info.displayName()},
                  {QStringLiteral("qth"), info.prefixOnly ? QString() : info.displayLocation()},
                  {QStringLiteral("country"), info.country}, {QStringLiteral("grid"), info.grid},
                  {QStringLiteral("prefixOnly"), info.prefixOnly}, {QStringLiteral("lotw"), info.lotw},
                  {QStringLiteral("eqsl"), info.eqsl}, {QStringLiteral("mailQsl"), info.mailQsl},
                  {QStringLiteral("continent"), info.continent}, {QStringLiteral("cqZone"), info.cqZone}};
    if (info.distanceKm >= 0.0) {
        m.insert(QStringLiteral("distanceKm"), qRound(info.distanceKm));
        m.insert(QStringLiteral("bearing"), qRound(info.bearingDeg));
    }
    // Country, continent and CQ zone from cty.dat where the lookup left
    // them out (QRZ.com gives the country only).
    if (info.prefixOnly || info.country.isEmpty() || info.continent.isEmpty() || info.cqZone <= 0) {
        if (const DxccEntity* e = m_cty->entityForCallsign(info.call)) {
            if (info.country.isEmpty()) {
                m.insert(QStringLiteral("country"), e->name);
            }
            if (info.continent.isEmpty()) {
                m.insert(QStringLiteral("continent"), e->continent);
            }
            if (info.cqZone <= 0) {
                m.insert(QStringLiteral("cqZone"), e->cqZone);
            }
        }
    }
    return m;
}

void MobileEngine::update3DGuard()
{
    const int running = spectrum3D() && qApp->applicationState() == Qt::ApplicationActive ? 1 : 0;
    AppSettings& settings = AppSettings::instance();
    if (settings.value(QStringLiteral("Mobile3DRunning"), 0).toInt() != running) {
        settings.setValue(QStringLiteral("Mobile3DRunning"), running);
        settings.save();
    }
}

void MobileEngine::disable3DTooSlow()
{
    if (!spectrum3D()) {
        return;
    }
    setSpectrum3D(false);
    setLastError(tr("The 3D view was too slow on this device and was switched off; "
                    "the 2D waterfall is shown. It can be turned on again in View."));
}

QString MobileEngine::qsoLogLocation() const
{
#if defined(Q_OS_IOS)
    return tr("Files › On My iPhone / iPad › OksaSDR › oksasdr-log.adi");
#elif defined(Q_OS_ANDROID)
    return m_qsoLog->mirrorOk() ? tr("Files › Documents › OksaSDR › oksasdr-log.adi")
                                : tr("In the app only; Share the log gives a copy.");
#else
    return QDir::toNativeSeparators(m_qsoLog->path());
#endif
}

// The file is read here (an Android content:// URL is read best on this
// thread); parsing, the duplicate check and the write run in a worker, so
// a logbook of tens of thousands of contacts does not hold the display
// (an import of 20 000 froze it, and iOS closed the app). Records are
// taken one at a time: only the new contacts' entries stay in memory.
// adifImported() brings the result.
void MobileEngine::importAdif(const QUrl& url)
{
    if (m_importing) {
        emit adifImported({{QStringLiteral("error"), tr("An import is already running.")}});
        return;
    }
    if (!m_qsoLog->isLoaded()) {
        emit adifImported({{QStringLiteral("error"), tr("The log is still being read; try again in a moment.")}});
        return;
    }
    // Android hands a content:// URL, which QFile reads as it is; iOS and
    // the desktop a file URL.
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcMobile) << "ADIF import: cannot open" << url << file.errorString();
        emit adifImported({{QStringLiteral("error"), tr("The file could not be opened.")}});
        return;
    }
    QByteArray data = file.readAll();
    QSet<QString> have;
    have.reserve(m_qsoLog->count());
    for (const QsoEntry& e : m_qsoLog->entries()) {
        have.insert(QsoLog::dedupeKey(e));
    }
    m_importing = true;
    const QString path = m_qsoLog->path();
    const QString mirror = m_qsoLog->mirrorPath();
    QThread* worker = QThread::create([this, data = std::move(data), have = std::move(have), path, mirror]() mutable {
        QElapsedTimer timer;
        timer.start();
        int found = 0;
        int duplicates = 0;
        QString adif;
        QList<QsoEntry> entries;
        QList<QVariantMap> newest;
        QsoLog::parseAdifEach(data, [&](QVariantMap&& r) {
            ++found;
            if (r.value(QStringLiteral("CALL")).toString().trimmed().isEmpty()) {
                return;
            }
            if (!r.contains(QStringLiteral("BAND")) && r.contains(QStringLiteral("FREQ"))) {
                r.insert(QStringLiteral("BAND"), QsoLog::bandOf(r.value(QStringLiteral("FREQ")).toDouble()));
            }
            const QsoEntry e = QsoLog::entryOf(r);
            const QString k = QsoLog::dedupeKey(e);
            if (have.contains(k)) {
                ++duplicates;
                return;
            }
            have.insert(k);
            adif += QsoLog::adifRecord(r) + QLatin1Char('\n');
            entries.append(e);
            newest.append(std::move(r));
            if (newest.size() > 2 * QsoLog::kRecent) {
                newest.remove(0, newest.size() - QsoLog::kRecent);
            }
        });
        data.clear();
        bool mirrorOk = false;
        const bool written = QsoLog::writeText(path, mirror, adif, &mirrorOk);
        const qint64 ms = timer.elapsed();
        QMetaObject::invokeMethod(this, [this, entries = std::move(entries), newest = std::move(newest), found, duplicates,
                                         written, mirrorOk, ms] {
            m_importing = false;
            if (found == 0) {
                emit adifImported({{QStringLiteral("error"), tr("No contacts found in the file (is it ADIF?).")}});
                return;
            }
            if (!written) {
                emit adifImported({{QStringLiteral("error"), tr("The log file could not be written.")},
                                   {QStringLiteral("imported"), 0},
                                   {QStringLiteral("duplicates"), duplicates}});
                return;
            }
            m_qsoLog->addWritten(entries, newest, mirrorOk);
            qCInfo(lcMobile) << "ADIF import:" << entries.size() << "imported," << duplicates << "already in the log, in"
                             << ms << "ms";
            emit qsoLogChanged();
            emit adifImported({{QStringLiteral("imported"), int(entries.size())}, {QStringLiteral("duplicates"), duplicates}});
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool MobileEngine::shareQsoLog()
{
    if (m_qsoLog->count() == 0 || !QFile::exists(m_qsoLog->path())) {
        setLastError(tr("No contacts in the log yet."));
        return false;
    }
    // A dated copy in the app's files folder (Android shares only from there).
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("share"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("OksaSDR-log-%1.adi")
                                                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmm"))));
    QFile::remove(path);
    if (!QFile::copy(m_qsoLog->path(), path)) {
        setLastError(tr("The log could not be prepared for sharing."));
        return false;
    }
    qCInfo(lcMobile) << "share QSO log:" << m_qsoLog->count() << "contacts";
    return shareFile(tr("OksaSDR log (ADIF)"), path, QStringLiteral("text/plain"));
}

QVariantMap MobileEngine::qsoDefaults() const
{
    const double mhz = m_slice ? m_slice->frequency() : 0.0;
    const QString mode = m_slice ? m_slice->mode() : QString();
    const QString group = QsoLog::modeGroup(QsoLog::adifMode(mode).first);
    return {{QStringLiteral("freq"), mhz}, {QStringLiteral("band"), QsoLog::bandOf(mhz)},
            // No report until the slice's mode is known (59 voice, 599 CW and data).
            {QStringLiteral("mode"), mode},
            {QStringLiteral("rst"), mode.isEmpty() ? QString() : group == QLatin1String("PHONE") ? QStringLiteral("59") : QStringLiteral("599")},
            {QStringLiteral("power"), m_connected ? rfPower() : 0},
            {QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"))},
            {QStringLiteral("myCall"), m_session.radioModel().callsign()}, {QStringLiteral("myGrid"), myGrid()}};
}

QVariantMap MobileEngine::workedInfo(const QString& call) const
{
    const QVariantMap d = qsoDefaults();
    return {{QStringLiteral("worked"), m_qsoLog->workedCount(call)},
            {QStringLiteral("dupe"), m_qsoLog->isDupe(call, d.value(QStringLiteral("band")).toString(),
                                                      QsoLog::adifMode(d.value(QStringLiteral("mode")).toString()).first)}};
}

bool MobileEngine::logQso(const QVariantMap& fields)
{
    const QString call = fields.value(QStringLiteral("call")).toString().trimmed().toUpper();
    if (call.size() < 3) {
        return false;
    }
    QVariantMap d = qsoDefaults();
    // FT8 / FT4 log the frequency of the slice they ran on.
    if (fields.value(QStringLiteral("freqMhz")).toDouble() > 0.0) {
        const double mhz = fields.value(QStringLiteral("freqMhz")).toDouble();
        d.insert(QStringLiteral("freq"), mhz);
        d.insert(QStringLiteral("band"), QsoLog::bandOf(mhz));
    }
    // FT8 / FT4's time: the device clock, corrected by a time server when
    // FT8 / FT4 have run (Ft8Engine::nowMs).
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(Ft8Engine::nowMs()).toUTC();
    QString timeOn = fields.value(QStringLiteral("timeOn")).toString().remove(QLatin1Char(':'));
    if (timeOn.size() != 4) {
        timeOn = now.toString(QStringLiteral("HHmm"));
    }
    auto mode = QsoLog::adifMode(d.value(QStringLiteral("mode")).toString());
    // FT8 / FT4 from the Digi tab name their ADIF mode themselves.
    if (fields.contains(QStringLiteral("adifMode"))) {
        mode = {fields.value(QStringLiteral("adifMode")).toString(), fields.value(QStringLiteral("adifSubmode")).toString()};
    }
    QVariantMap r{{QStringLiteral("CALL"), call},
                  {QStringLiteral("QSO_DATE"), now.toString(QStringLiteral("yyyyMMdd"))},
                  {QStringLiteral("TIME_ON"), timeOn},
                  {QStringLiteral("TIME_OFF"), now.toString(QStringLiteral("HHmm"))},
                  {QStringLiteral("FREQ"), QString::number(d.value(QStringLiteral("freq")).toDouble(), 'f', 6)},
                  {QStringLiteral("BAND"), d.value(QStringLiteral("band"))},
                  {QStringLiteral("MODE"), mode.first},
                  {QStringLiteral("SUBMODE"), mode.second},
                  {QStringLiteral("RST_SENT"), fields.value(QStringLiteral("rstSent"))},
                  {QStringLiteral("RST_RCVD"), fields.value(QStringLiteral("rstRcvd"))},
                  {QStringLiteral("TX_PWR"), d.value(QStringLiteral("power")).toInt() > 0 ? d.value(QStringLiteral("power")).toString() : QString()},
                  {QStringLiteral("COMMENT"), fields.value(QStringLiteral("comment"))},
                  {QStringLiteral("SIG"), fields.value(QStringLiteral("sig"))},
                  {QStringLiteral("SIG_INFO"), fields.value(QStringLiteral("sigInfo")).toString().toUpper()},
                  {QStringLiteral("MY_SIG"), fields.value(QStringLiteral("mySig"))},
                  {QStringLiteral("MY_SIG_INFO"), fields.value(QStringLiteral("mySigInfo")).toString().toUpper()},
                  {QStringLiteral("STATION_CALLSIGN"), d.value(QStringLiteral("myCall"))},
                  {QStringLiteral("MY_GRIDSQUARE"), d.value(QStringLiteral("myGrid"))}};
    if (fields.contains(QStringLiteral("grid"))) {
        r.insert(QStringLiteral("GRIDSQUARE"), fields.value(QStringLiteral("grid")));
    }
    // Name, QTH and the rest from the lookup, however the call got in (typed,
    // a heard CW call, a spot, FT8 / FT4 by itself): the Log tab's card when
    // it is for this call, the lookup cache, or a QRZ.com lookup the QSO
    // waits for (at most 8 s); without an account, cty.dat's country.
    if (m_callLookup.value(QStringLiteral("call")).toString().compare(call, Qt::CaseInsensitive) == 0
        && !m_callLookup.contains(QStringLiteral("error"))) {
        addLookupFields(r, m_callLookup);
        return writeQso(r);
    }
    CallsignLookupService& lookup = CallsignLookupService::instance();
    const CallsignInfo cached = lookup.cachedEntry(call);
    if (cached.isValid() && !cached.prefixOnly) {
        addLookupFields(r, callInfoMap(cached));
        return writeQso(r);
    }
    if (QrzLookupSettings::username().isEmpty()) {
        addLookupFields(r, ctyFields(call));
        return writeQso(r);
    }
    m_pendingQsos.insert(call, r);
    qCInfo(lcMobile) << "QSO with" << call << "waits for its lookup";
    lookup.lookup(call);
    QTimer::singleShot(8000, this, [this, call] { completePendingQso(call, {}); });
    return true;
}

QVariantMap MobileEngine::ctyFields(const QString& call) const
{
    QVariantMap m;
    if (const DxccEntity* e = m_cty->entityForCallsign(call)) {
        m.insert(QStringLiteral("country"), e->name);
        m.insert(QStringLiteral("continent"), e->continent);
        m.insert(QStringLiteral("cqZone"), e->cqZone);
    }
    return m;
}

// The lookup's fields into an ADIF record; an empty one leaves what the
// record has (an FT8 grid stays when QRZ.com has none).
void MobileEngine::addLookupFields(QVariantMap& r, const QVariantMap& info) const
{
    const auto put = [&r, &info](const char* adif, const char* key) {
        const QString v = info.value(QLatin1String(key)).toString();
        if (!v.isEmpty()) {
            r.insert(QLatin1String(adif), v);
        }
    };
    put("NAME", "name");
    put("QTH", "qth");
    put("GRIDSQUARE", "grid");
    put("COUNTRY", "country");
    put("CONT", "continent");
    const int cqz = info.value(QStringLiteral("cqZone")).toInt();
    if (cqz > 0) {
        r.insert(QStringLiteral("CQZ"), cqz);
    }
}

// A QSO that waited for its lookup: written with what came (or cty.dat's
// country when the lookup failed or took too long).
void MobileEngine::completePendingQso(const QString& call, const QVariantMap& info)
{
    const QString key = call.trimmed().toUpper();
    if (!m_pendingQsos.contains(key)) {
        return;
    }
    QVariantMap r = m_pendingQsos.take(key);
    const bool found = !info.isEmpty() && !info.value(QStringLiteral("prefixOnly")).toBool();
    addLookupFields(r, found ? info : ctyFields(key));
    qCInfo(lcMobile) << "QSO with" << key << (found ? "completed from its lookup" : "written without a lookup");
    writeQso(r);
}

bool MobileEngine::writeQso(const QVariantMap& r)
{
    if (m_qsoLogDryRun) {  // a test: the record in the log, not in the logbook
        qCInfo(lcMobile).noquote() << "logged (dry run):" << QsoLog::adifRecord(r);
        return true;
    }
    if (!m_qsoLog->append(r)) {
        setLastError(tr("The QSO could not be written to the log file."));
        return false;
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("logged %1 %2 %3 %4 %5")
                                      .arg(r.value(QStringLiteral("CALL")).toString(), r.value(QStringLiteral("BAND")).toString(),
                                           r.value(QStringLiteral("MODE")).toString(), r.value(QStringLiteral("TIME_ON")).toString(),
                                           r.value(QStringLiteral("NAME")).toString().isEmpty() ? QStringLiteral("(no name)")
                                                                                                : QStringLiteral("with name and QTH"));
    onlineLogQueue(QsoLog::adifRecord(r));
    emit qsoLogChanged();
    return true;
}

QVariantList MobileEngine::memories() const
{
    QList<MemoryEntry> list = m_session.radioModel().memories().values();
    std::sort(list.begin(), list.end(), [](const MemoryEntry& a, const MemoryEntry& b) {
        return a.freq < b.freq;
    });
    QVariantList out;
    for (const MemoryEntry& m : std::as_const(list)) {
        const QString dir = m.offsetDir.toLower();
        const bool ctcss = m.toneMode.toLower() == QLatin1String("ctcss_tx");
        out << QVariantMap{{QStringLiteral("index"), m.index}, {QStringLiteral("name"), m.name},
                           {QStringLiteral("freq"), m.freq}, {QStringLiteral("mode"), m.mode},
                           {QStringLiteral("group"), m.group},
                           // FM repeater: direction, shift (MHz) and CTCSS tone.
                           {QStringLiteral("dir"), dir == QLatin1String("up") || dir == QLatin1String("down")
                                                       ? dir : QStringLiteral("simplex")},
                           {QStringLiteral("offset"), std::abs(m.repeaterOffset)},
                           {QStringLiteral("ctcss"), ctcss},
                           {QStringLiteral("tone"), m.toneValue > 0.0 ? QString::number(m.toneValue, 'f', 1) : QString()}};
    }
    return out;
}

void MobileEngine::recallMemory(int index)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "recall memory" << index;
    if (rm.usesLocalMemoryBank()) {
        rm.recallCachedMemory(index);
        return;
    }
    rm.sendCommand(QStringLiteral("memory apply %1").arg(index));
    // As the desktop: the radio's memory apply sets the repeater direction and
    // shift but not tx_offset_freq, which moves the transmitter, so a
    // repeater memory transmitted on the receive frequency.
    const auto it = rm.memories().constFind(index);
    if (m_slice && it != rm.memories().constEnd()) {
        const QString retune = buildMemoryRecallRetuneCommand(m_slice->sliceId(), it.value());
        if (!retune.isEmpty()) {
            rm.sendCommand(retune);
        }
        const QString fixup = buildMemoryRecallSliceFixupCommand(m_slice->sliceId(), it.value());
        if (!fixup.isEmpty()) {
            rm.sendCommand(fixup);
        }
    }
}

void MobileEngine::saveMemory(const QString& name)
{
    if (!m_connected || !m_slice) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    if (!rm.memoriesWritable()) {
        setLastError(tr("This radio's memories cannot be written."));
        return;
    }
    createMemoryFromSlice(&rm, m_slice, name.trimmed(), this, [this](int code, const QString& body, int index) {
        if (code != 0) {
            setLastError(tr("The radio did not save the memory: %1").arg(body));
        } else {
            qCInfo(lcMobile) << "memory saved as" << index;
        }
        emit memoriesChanged();
    });
}

void MobileEngine::removeMemory(int index)
{
    if (m_connected) {
        qCInfo(lcMobile) << "remove memory" << index;
        m_session.radioModel().sendCommand(QStringLiteral("memory remove %1").arg(index));
    }
}

void MobileEngine::editMemory(int index, const QString& name, double freqMhz, const QString& mode,
                              const QString& dir, double offsetMhz, int ctcss, const QString& tone)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    const auto it = rm.memories().constFind(index);
    if (it == rm.memories().constEnd() || !rm.memoriesWritable() || freqMhz <= 0.0) {
        return;
    }
    MemoryEntry e = it.value();
    e.name = name.trimmed();
    e.freq = freqMhz;
    if (!mode.isEmpty()) {
        e.mode = mode;
    }
    if (dir == QLatin1String("simplex") || dir == QLatin1String("down") || dir == QLatin1String("up")) {
        e.offsetDir = dir;
    }
    if (offsetMhz >= 0.0 && offsetMhz <= 100.0) {
        e.repeaterOffset = offsetMhz;
    }
    if (ctcss >= 0) {
        e.toneMode = ctcss ? QStringLiteral("ctcss_tx") : QStringLiteral("off");
    }
    if (!tone.isEmpty() && ctcssTones().contains(tone)) {
        e.toneValue = tone.toDouble();
    }
    const MemoryUpdateData update = buildMemoryUpdateData(e);
    qCInfo(lcMobile) << "edit memory" << index << e.freq << e.mode;
    rm.sendCmdPublic(QStringLiteral("memory set %1 %2").arg(index).arg(update.commandSuffix),
                     [this, &rm, index, kvs = update.kvs](int code, const QString& body) {
                         if (code == 0) {
                             rm.handleMemoryStatus(index, kvs);
                         } else {
                             setLastError(tr("The radio did not change the memory: %1").arg(body));
                         }
                         emit memoriesChanged();
                     });
}

bool MobileEngine::memoriesWritable() const
{
    return m_connected && m_session.radioModel().memoriesWritable();
}

QVariantList MobileEngine::tnfs() const
{
    QVariantList out;
    if (!m_connected) {
        return out;
    }
    for (const TnfEntry& t : m_session.radioModel().tnfModel().tnfs()) {
        out << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("freq"), t.freqMhz},
                           {QStringLiteral("width"), t.widthHz}, {QStringLiteral("depth"), t.depthDb},
                           {QStringLiteral("permanent"), t.permanent}};
    }
    return out;
}

bool MobileEngine::tnfEnabled() const
{
    return m_session.radioModel().tnfModel().globalEnabled();
}

void MobileEngine::setTnfEnabled(bool on)
{
    if (tnfAvailable() && on != tnfEnabled()) {
        m_session.radioModel().tnfModel().requestGlobalTnfEnabled(on);
    }
}

bool MobileEngine::tnfAvailable() const
{
    return m_connected && m_session.radioModel().backendCapabilities().maxNotchFilters > 0;
}

bool MobileEngine::canAddTnf() const
{
    return tnfAvailable()
        && m_session.radioModel().tnfModel().tnfs().size() < m_session.radioModel().backendCapabilities().maxNotchFilters;
}

bool MobileEngine::tnfHasDepth() const
{
    return tnfAvailable() && m_session.radioModel().backendCapabilities().notchHasDepth;
}

void MobileEngine::addTnf(double freqMhz)
{
    if (!canAddTnf() || !m_slice) {
        return;
    }
    if (freqMhz <= 0.0) {
        freqMhz = m_slice->frequency() + (m_slice->filterLow() + m_slice->filterHigh()) / 2.0 / 1e6;
    }
    qCInfo(lcMobile) << "add TNF at" << freqMhz;
    m_session.radioModel().tnfModel().createTnf(freqMhz);
}

void MobileEngine::setTnf(int id, const QString& key, const QVariant& value)
{
    if (!tnfAvailable()) {
        return;
    }
    TnfModel& tm = m_session.radioModel().tnfModel();
    if (!tm.tnf(id)) {
        return;
    }
    const RadioCapabilities caps = m_session.radioModel().backendCapabilities();
    if (key == QLatin1String("freq")) {
        tm.setTnfFreq(id, value.toDouble());
    } else if (key == QLatin1String("width")) {
        const int lo = caps.notchMinWidthHz > 0 ? int(caps.notchMinWidthHz) : 10;
        const int hi = caps.notchMaxWidthHz > 0 ? int(caps.notchMaxWidthHz) : 6000;
        tm.setTnfWidth(id, std::clamp(value.toInt(), lo, hi));
    } else if (key == QLatin1String("depth") && caps.notchHasDepth) {
        tm.setTnfDepth(id, std::clamp(value.toInt(), 1, 3));
    } else if (key == QLatin1String("permanent")) {
        tm.setTnfPermanent(id, value.toBool());
    }
}

void MobileEngine::removeTnf(int id)
{
    if (tnfAvailable() && m_session.radioModel().tnfModel().tnf(id)) {
        qCInfo(lcMobile) << "remove TNF" << id;
        m_session.radioModel().tnfModel().requestRemoveTnf(id);
    }
}

void MobileEngine::setupDxCluster()
{
    m_dx = new DxClusterClient(this);
    m_dx->initialize();
    connect(this, &MobileEngine::dxClusterChanged, this, &MobileEngine::dxClusterStatusChanged);
    connect(m_dx, &DxClusterClient::connected, this, [this] {
        m_dxState = QStringLiteral("connected");
        m_dxError.clear();
        qCInfo(lcMobile) << "DX cluster connected to" << m_dx->host() << m_dx->port();
        emit dxClusterChanged();
    });
    connect(m_dx, &DxClusterClient::disconnected, this, [this] {
        // It reconnects by itself until disconnect() is called.
        if (m_dxState != QLatin1String("off")) {
            m_dxState = QStringLiteral("connecting");
            emit dxClusterChanged();
        }
    });
    connect(m_dx, &DxClusterClient::connectionError, this, [this](const QString& error) {
        m_dxError = error;
        qCInfo(lcMobile) << "DX cluster:" << error;
        emit dxClusterChanged();
    });
    connect(m_dx, &DxClusterClient::spotReceived, this, [this](const DxSpot& spot) {
        ++m_dxSpotCount;
        m_dxLastSpotUtc = QDateTime::currentDateTimeUtc();
        emit dxClusterStatusChanged();
        RadioModel& rm = m_session.radioModel();
        if (!m_connected || spot.dxCall.trimmed().isEmpty() || spot.freqMhz <= 0.0) {
            return;
        }
        AppSettings& settings = AppSettings::instance();
        int lifetime = spot.lifetimeSec > 0 ? spot.lifetimeSec
                                            : settings.value(QStringLiteral("DxClusterSpotLifetimeSec"), 0).toInt();
        if (lifetime <= 0) {
            lifetime = settings.value(QStringLiteral("DxClusterSpotLifetime"), 30).toInt() * 60;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const auto seen = m_dxSeen.constFind(spot.dxCall);
        if (seen != m_dxSeen.constEnd() && std::abs(seen->first - spot.freqMhz) < 0.001
            && now - seen->second <= qint64(lifetime) * 1000) {
            return;
        }
        m_dxSeen.insert(spot.dxCall, {spot.freqMhz, now});
        QString color = spot.color.isEmpty()
            ? settings.value(QStringLiteral("DxClusterSpotColor"), QStringLiteral("#D2B48C")).toString()
            : spot.color;
        if (color.length() == 7) {
            color = QStringLiteral("#FF") + color.mid(1);
        }
        const QString freq = QString::number(spot.freqMhz, 'f', 6);
        // trigger_action=none: a tap on the spot never retunes or changes
        // the mode of another client's slice (the desktop's #1846).
        QString cmd = QStringLiteral("spot add callsign=%1 rx_freq=%2 tx_freq=%2 source=DXCluster "
                                     "spotter_callsign=%3 trigger_action=none lifetime_seconds=%4")
                          .arg(QString(spot.dxCall).replace(QLatin1Char(' '), QChar(0x7f)), freq,
                               spot.spotterCall)
                          .arg(lifetime);
        if (!spot.comment.isEmpty()) {
            cmd += QStringLiteral(" comment=") + QString(spot.comment).replace(QLatin1Char(' '), QChar(0x7f));
        }
        cmd += QStringLiteral(" color=") + color;
        m_dxQueue.append(cmd);
        Q_UNUSED(rm);
    });
    m_dxSendTimer.setInterval(1000);
    connect(&m_dxSendTimer, &QTimer::timeout, this, [this] {
        if (m_dxQueue.isEmpty() || !m_connected) {
            return;
        }
        const int n = std::min<int>(m_dxQueue.size(),
                                    std::max(1, AppSettings::instance().value(QStringLiteral("RbnRateLimit"), 10).toInt()));
        for (int i = 0; i < n; ++i) {
            m_session.radioModel().sendCommand(m_dxQueue.at(i));
        }
        m_dxQueue.remove(0, n);
    });
    m_dxSendTimer.start();

    m_spotsNotifyTimer.setSingleShot(true);
    m_spotsNotifyTimer.setInterval(500);
    connect(&m_spotsNotifyTimer, &QTimer::timeout, this, &MobileEngine::spotsChanged);
    // With an age limit, spots leave the display as they age.
    m_spotsAgeTimer.setInterval(30000);
    connect(&m_spotsAgeTimer, &QTimer::timeout, this, [this] {
        if (spotMaxAgeMin() > 0 && showSpots()) {
            emit spotsChanged();
        }
    });
    m_spotsAgeTimer.start();
    SpotModel& spots = m_session.radioModel().spotModel();
    auto spotsMoved = [this] {
        if (!m_spotsNotifyTimer.isActive()) {
            m_spotsNotifyTimer.start();
        }
    };
    connect(&spots, &SpotModel::spotAdded, this, spotsMoved);
    connect(&spots, &SpotModel::spotUpdated, this, spotsMoved);
    connect(&spots, &SpotModel::spotRemoved, this, spotsMoved);
    connect(&spots, &SpotModel::spotsCleared, this, spotsMoved);
    connect(&spots, &SpotModel::spotsRefreshed, this, spotsMoved);
    // The radio's callsign is the cluster login when none is set here.
    connect(&m_session.radioModel(), &RadioModel::infoChanged, this, &MobileEngine::dxClusterChanged);
}

void MobileEngine::spotTest(const QString& action)
{
    RadioModel& rm = m_session.radioModel();
    if (!m_connected || !m_slice) {
        return;
    }
    if (action == QLatin1String("add")) {
        const double f = m_slice->frequency();
        const struct { const char* call; double offsetKhz; const char* comment; } tests[] = {
            {"OH0TEST", 2.0, "test spot"}, {"OH0TST2", 2.3, "close by"}, {"OH0TST3", -3.0, "CW 599"}};
        for (const auto& t : tests) {
            const QString freq = QString::number(f + t.offsetKhz / 1000.0, 'f', 6);
            rm.sendCommand(QStringLiteral("spot add callsign=%1 rx_freq=%2 tx_freq=%2 source=OksaSDR-test "
                                          "trigger_action=none lifetime_seconds=300 color=#FF3FB6FF comment=%3")
                               .arg(QLatin1String(t.call), freq,
                                    QString::fromLatin1(t.comment).replace(QLatin1Char(' '), QChar(0x7f))));
        }
    } else if (action == QLatin1String("clear")) {
        for (const SpotData& s : rm.spotModel().spots()) {
            if (s.source == QLatin1String("OksaSDR-test") || s.callsign.startsWith(QLatin1String("OH0TEST"))) {
                rm.sendCommand(QStringLiteral("spot remove %1").arg(s.index));
            }
        }
    }
}

QString MobileEngine::cwxSidetoneStats() const
{
    return QStringLiteral("%1 key-downs, %2 ms keyed")
        .arg(m_cwxSidetoneDowns.load(std::memory_order_relaxed))
        .arg(m_cwxSidetoneKeyedMs.load(std::memory_order_relaxed));
}

void MobileEngine::devCommand(const QString& command)
{
    m_session.radioModel().sendCmdPublic(command, [command](int code, const QString& body) {
        qInfo().noquote() << QStringLiteral("radio-commands: \"%1\" -> 0x%2 %3").arg(command, QString::number(code, 16), body);
    });
}

QString MobileEngine::dxCallsign() const
{
    const QString own = AppSettings::instance().value(QStringLiteral("DxClusterCallsign")).toString().trimmed();
    return own.isEmpty() ? m_session.radioModel().callsign().trimmed() : own;
}

QVariantList MobileEngine::spots() const
{
    QVariantList out;
    if (!m_connected || !showSpots()) {
        return out;
    }
    const QString modes = spotModes();
    const qint64 maxAgeMs = qint64(spotMaxAgeMin()) * 60 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const SpotData& s : m_session.radioModel().spotModel().spots()) {
        if (maxAgeMs > 0 && s.addedMs > 0 && now - s.addedMs > maxAgeMs) {
            continue;
        }
        if (modes != QLatin1String("all")) {
            const QString mode = SpotModeResolver::resolveSpotRadioMode(s.mode, s.comment, s.rxFreqMhz);
            const bool cw = mode.startsWith(QLatin1String("CW"));
            const bool digital = mode.startsWith(QLatin1String("DIG")) || mode == QLatin1String("RTTY")
                || mode == QLatin1String("FDV");
            const bool phone = !mode.isEmpty() && !cw && !digital;
            if ((modes == QLatin1String("cw") && !cw) || (modes == QLatin1String("digital") && !digital)
                || (modes == QLatin1String("phone") && !phone)) {
                continue;
            }
        }
        out << QVariantMap{{QStringLiteral("index"), s.index}, {QStringLiteral("call"), s.callsign},
                           {QStringLiteral("freq"), s.rxFreqMhz}, {QStringLiteral("mode"), s.mode},
                           {QStringLiteral("comment"), s.comment}, {QStringLiteral("spotter"), s.spotterCallsign},
                           {QStringLiteral("color"), s.color}, {QStringLiteral("source"), s.source}};
    }
    return out;
}

bool MobileEngine::showSpots() const
{
    return AppSettings::instance().value(QStringLiteral("MobileShowSpots"), QStringLiteral("True")).toString()
        == QLatin1String("True");
}

void MobileEngine::setShowSpots(bool on)
{
    if (on != showSpots()) {
        AppSettings::instance().setValue(QStringLiteral("MobileShowSpots"), on ? QStringLiteral("True") : QStringLiteral("False"));
        AppSettings::instance().save();
        emit spotsChanged();
    }
}

QString MobileEngine::spotModes() const
{
    const QString m = AppSettings::instance().value(QStringLiteral("MobileSpotModes"), QStringLiteral("all")).toString();
    return (m == QLatin1String("cw") || m == QLatin1String("phone") || m == QLatin1String("digital")) ? m
                                                                                                      : QStringLiteral("all");
}

void MobileEngine::setSpotModes(const QString& modes)
{
    if (modes != spotModes()) {
        AppSettings::instance().setValue(QStringLiteral("MobileSpotModes"), modes);
        AppSettings::instance().save();
        emit spotsChanged();
    }
}

int MobileEngine::spotMaxAgeMin() const
{
    return std::clamp(AppSettings::instance().value(QStringLiteral("MobileSpotMaxAgeMin"), 0).toInt(), 0, 240);
}

void MobileEngine::setSpotMaxAgeMin(int minutes)
{
    minutes = std::clamp(minutes, 0, 240);
    if (minutes != spotMaxAgeMin()) {
        AppSettings::instance().setValue(QStringLiteral("MobileSpotMaxAgeMin"), minutes);
        AppSettings::instance().save();
        emit spotsChanged();
    }
}

QVariantMap MobileEngine::dxCluster() const
{
    const AppSettings& settings = AppSettings::instance();
    const QString own = settings.value(QStringLiteral("DxClusterCallsign")).toString().trimmed();
    return {
        {QStringLiteral("host"), settings.value(QStringLiteral("DxClusterHost"), QStringLiteral("dxc.nc7j.com")).toString()},
        {QStringLiteral("port"), settings.value(QStringLiteral("DxClusterPort"), 7300).toInt()},
        {QStringLiteral("callsign"), dxCallsign()},
        {QStringLiteral("callsignFromRadio"), own.isEmpty()},
        {QStringLiteral("autoConnect"),
         settings.value(QStringLiteral("DxClusterAutoConnect"), QStringLiteral("False")).toString() == QLatin1String("True")},
        {QStringLiteral("state"), m_dxState},
        {QStringLiteral("error"), m_dxError},
    };
}

QString MobileEngine::dxClusterStatus() const
{
    const QVariantMap cfg = dxCluster();
    const QString server = QStringLiteral("%1:%2").arg(cfg.value(QStringLiteral("host")).toString())
                               .arg(cfg.value(QStringLiteral("port")).toInt());
    if (m_dxState == QLatin1String("connected")) {
        const QString spots = m_dxSpotCount > 0
            ? tr("%1 spots so far, the latest at %2 UTC").arg(m_dxSpotCount).arg(m_dxLastSpotUtc.toString(QStringLiteral("HH:mm")))
            : tr("no spots yet");
        return tr("Connected to %1 as %2; %3.").arg(server, cfg.value(QStringLiteral("callsign")).toString(), spots);
    }
    if (m_dxState == QLatin1String("connecting")) {
        return m_dxError.isEmpty() ? tr("Connecting to %1…").arg(server)
                                   : tr("Connecting to %1 again (%2)").arg(server, m_dxError);
    }
    return m_dxError.isEmpty() ? tr("Not connected.") : tr("Not connected: %1").arg(m_dxError);
}

QString MobileEngine::dxClusterStatusKind() const
{
    if (m_dxState == QLatin1String("connected")) {
        return QStringLiteral("ok");
    }
    return m_dxState == QLatin1String("off") && !m_dxError.isEmpty() ? QStringLiteral("failed") : QString();
}

void MobileEngine::setDxCluster(const QString& key, const QVariant& value)
{
    AppSettings& settings = AppSettings::instance();
    if (key == QLatin1String("host")) {
        settings.setValue(QStringLiteral("DxClusterHost"), value.toString().trimmed());
    } else if (key == QLatin1String("port")) {
        settings.setValue(QStringLiteral("DxClusterPort"), std::clamp(value.toInt(), 1, 65535));
    } else if (key == QLatin1String("callsign")) {
        settings.setValue(QStringLiteral("DxClusterCallsign"), value.toString().trimmed().toUpper());
    } else if (key == QLatin1String("autoConnect")) {
        settings.setValue(QStringLiteral("DxClusterAutoConnect"), value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
    } else {
        return;
    }
    settings.save();
    emit dxClusterChanged();
}

void MobileEngine::connectDxCluster()
{
    const QVariantMap cfg = dxCluster();
    const QString call = cfg.value(QStringLiteral("callsign")).toString();
    if (!m_connected || call.isEmpty()) {
        if (call.isEmpty()) {
            setLastError(tr("The DX cluster needs a callsign: set it in Radio Setup or under Spots."));
        }
        return;
    }
    m_dxWanted = true;
    m_dxState = QStringLiteral("connecting");
    m_dxError.clear();
    m_dxSpotCount = 0;
    qCInfo(lcMobile) << "DX cluster connect" << cfg.value(QStringLiteral("host")).toString()
                     << cfg.value(QStringLiteral("port")).toInt();
    m_dx->connectToCluster(cfg.value(QStringLiteral("host")).toString(),
                           quint16(cfg.value(QStringLiteral("port")).toInt()), call);
    emit dxClusterChanged();
}

void MobileEngine::disconnectDxCluster()
{
    m_dxWanted = false;
    m_dxState = QStringLiteral("off");
    m_dxQueue.clear();
    m_dx->disconnect();
    emit dxClusterChanged();
}

void MobileEngine::tuneToSpot(int index)
{
    RadioModel& rm = m_session.radioModel();
    const auto it = rm.spotModel().spots().constFind(index);
    if (!m_connected || !m_slice || it == rm.spotModel().spots().constEnd()) {
        return;
    }
    const SpotData spot = it.value();
    if (m_pan && index >= 0) {
        rm.sendCommand(QStringLiteral("spot trigger %1 pan=%2").arg(index).arg(m_pan->panId()));
    }
    if (AppSettings::instance().value(QStringLiteral("SpotAutoSwitchMode"), QStringLiteral("True")).toString()
        == QLatin1String("True")) {
        const QString mode = SpotModeResolver::resolveSpotRadioMode(spot.mode, spot.comment, spot.rxFreqMhz);
        if (!mode.isEmpty() && mode != m_slice->mode() && m_slice->modeList().contains(mode)) {
            m_slice->setMode(mode);
        }
    }
    qCInfo(lcMobile) << "tune to spot" << spot.callsign << spot.rxFreqMhz;
    if (m_lastSpotCall != spot.callsign) {
        m_lastSpotCall = spot.callsign;
        emit lastSpotCallChanged();
    }
    tuneTo(spot.rxFreqMhz);
}

QString MobileEngine::buttonStyle() const
{
    const QString style = AppSettings::instance().value(QStringLiteral("MobileButtonStyle"), QStringLiteral("lit")).toString();
    return (style == QLatin1String("light") || style == QLatin1String("classic")) ? style : QStringLiteral("lit");
}

void MobileEngine::setButtonStyle(const QString& style)
{
    if (style != buttonStyle()) {
        AppSettings::instance().setValue(QStringLiteral("MobileButtonStyle"), style);
        AppSettings::instance().save();
        emit buttonStyleChanged();
    }
}

namespace {
constexpr int kPttToggleLimitS = 180;
}

bool MobileEngine::pttToggle() const
{
    return AppSettings::instance().value(QStringLiteral("MobilePttToggle"), QStringLiteral("False")).toString()
        == QLatin1String("True");
}

void MobileEngine::setPttToggle(bool on)
{
    if (on == pttToggle()) {
        return;
    }
    // Switching back to hold-to-talk never leaves a transmission latched.
    if (!on && m_pttLatched) {
        releasePtt(QStringLiteral("PTT mode changed to hold"));
    }
    AppSettings::instance().setValue(QStringLiteral("MobilePttToggle"), on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit pttToggleChanged();
}

void MobileEngine::togglePtt()
{
    if (m_pttHeld) {
        releasePtt(QStringLiteral("toggled off"));
        return;
    }
    setPtt(true);
    if (!m_pttHeld) {
        return;
    }
    m_pttLatched = true;
    if (!m_pttToggleLimit.isActive()) {
        m_pttToggleLimit.setSingleShot(true);
        m_pttToggleLimit.setInterval(kPttToggleLimitS * 1000);
        // A member function: Qt::UniqueConnection refuses a lambda, and the
        // limit was never connected (a tester's log warned at every tap).
        connect(&m_pttToggleLimit, &QTimer::timeout, this, &MobileEngine::pttToggleLimitReached, Qt::UniqueConnection);
    }
    m_pttToggleLimit.start();
    emit pttToggleChanged();
}

void MobileEngine::pttToggleLimitReached()
{
    if (m_pttLatched) {
        releasePtt(QStringLiteral("toggle time limit"));
        setLastError(tr("Transmit stopped after %1 minutes (PTT in tap on/off mode).").arg(kPttToggleLimitS / 60));
    }
}

bool MobileEngine::locked() const
{
    return m_slice && m_slice->isLocked();
}

void MobileEngine::setLocked(bool on)
{
    if (m_slice && m_slice->isLocked() != on) {
        m_slice->setLocked(on);
    }
}

QStringList MobileEngine::pinnedSections() const
{
    return AppSettings::instance().value(QStringLiteral("MobilePinnedSections"), QStringLiteral("DSP"))
        .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
}

void MobileEngine::setPinnedSections(const QStringList& sections)
{
    if (sections == pinnedSections()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobilePinnedSections"), sections.join(QLatin1Char(',')));
    AppSettings::instance().save();
    emit displayLevelsChanged();
}

int MobileEngine::displayInt(const char* key, int fallback) const
{
    bool ok = false;
    const int v = AppSettings::instance().value(QLatin1String(key), QString::number(fallback)).toString().toInt(&ok);
    return ok ? v : fallback;
}

void MobileEngine::setDisplayInt(const char* key, int value)
{
    if (displayInt(key, value + 1) == value) {
        return;
    }
    AppSettings::instance().setValue(QLatin1String(key), QString::number(value));
    AppSettings::instance().save();
    emit displayLevelsChanged();
}

bool MobileEngine::cwDecoderEnabled() const
{
    return AppSettings::instance().value(kCwDecoderKey, QStringLiteral("False")).toString() == QLatin1String("True");
}

void MobileEngine::setCwDecoderEnabled(bool on)
{
    AppSettings::instance().setValue(kCwDecoderKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    updateCwDecoder();
    emit cwDecoderChanged();
}

QStringList MobileEngine::cwCalls() const
{
    // What was received, not sent ([TX] lines are this station's own).
    QStringList words;
    for (const QString& line : m_cwDecodedText.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("[TX]"))) {
            words += line.toUpper().split(QRegularExpression(QStringLiteral("[^A-Z0-9/]+")), Qt::SkipEmptyParts);
        }
    }
    // A prefix, a digit, a suffix ending in a letter, a portable part either
    // side; 5NN, 599, TU and the like do not match. A decoding error rarely
    // makes the same call twice, and a call after DE is meant as one.
    static const QRegularExpression call(
        QStringLiteral("^([A-Z0-9]{1,3}/)?[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z](/[A-Z0-9]{1,4})?$"));
    const QString my = m_session.radioModel().callsign().trimmed().toUpper();
    QHash<QString, int> seen;
    QHash<QString, int> last;
    QSet<QString> afterDe;
    for (int i = 0; i < words.size(); ++i) {
        const QString& w = words.at(i);
        if (w.size() < 3 || w.size() > 12 || w == my || !call.match(w).hasMatch()
            || (m_cty && !m_cty->entityForCallsign(w))) {
            continue;
        }
        ++seen[w];
        last[w] = i;
        if (i > 0 && words.at(i - 1) == QLatin1String("DE")) {
            afterDe.insert(w);
        }
    }
    QStringList out;
    for (auto it = last.cbegin(); it != last.cend(); ++it) {
        if (seen.value(it.key()) >= 2 || afterDe.contains(it.key())) {
            out << it.key();
        }
    }
    std::sort(out.begin(), out.end(), [&last](const QString& a, const QString& b) { return last.value(a) > last.value(b); });
    return out.mid(0, 3);
}

void MobileEngine::clearCwDecoded()
{
    m_cwDecodedText.clear();
    m_cwStrip.clear();
    m_cwStripStart = 0;
    emit cwDecoderChanged();
}

void MobileEngine::setCwKey(bool down, const QString& reason)
{
    if (!down) {
        if (m_cwKeyHeld) {
            m_cwKeyHeld = false;
            m_cwKeyInput.stop();
            qCInfo(lcMobile).noquote() << "CW key up:" << (reason.isEmpty() ? QStringLiteral("released") : reason);
            emit cwChanged();
            emit cwKeyDownChanged();
        }
        return;
    }
    if (m_cwKeyHeld || !cwKeyAvailable()) {
        return;
    }
    m_cwKeyInput = m_session.radioModel().localTxController()->capture(TxController::Activity::CwKey);
    m_cwKeyHeld = true;
    qCInfo(lcMobile) << "CW key down";
    if (!m_cwKeyInput.start()) {
        m_cwKeyHeld = false;
        m_cwKeyInput.stop();
        setLastError(tr("The radio did not accept the CW key."));
        return;
    }
    noteBreakInOff();
    emit cwChanged();
    emit cwKeyDownChanged();
}

bool MobileEngine::cwPaddles() const
{
    return AppSettings::instance().value(QStringLiteral("MobileCwPaddles"), QStringLiteral("False")).toString()
        == QLatin1String("True");
}

void MobileEngine::setCwPaddles(bool on)
{
    if (on == cwPaddles()) {
        return;
    }
    releasePaddles();
    setCwKey(false, QStringLiteral("keying changed"));
    AppSettings::instance().setValue(QStringLiteral("MobileCwPaddles"), on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit cwChanged();
}

QString MobileEngine::cwKeyboardPaddles() const
{
    // Ctrl by default: what USB paddle adapters send, on any layout ([ and ]
    // need AltGr or Option on Finnish and German ones).
    const QString keys = AppSettings::instance().value(QStringLiteral("MobileCwKeyboardPaddles"), QStringLiteral("ctrl")).toString();
    return (keys == QLatin1String("brackets") || keys == QLatin1String("alt") || keys == QLatin1String("off"))
        ? keys : QStringLiteral("ctrl");
}

void MobileEngine::setCwKeyboardPaddles(const QString& keys)
{
    if (keys != cwKeyboardPaddles()) {
        releasePaddles();
        AppSettings::instance().setValue(QStringLiteral("MobileCwKeyboardPaddles"), keys);
        AppSettings::instance().save();
        emit cwChanged();
    }
}

int MobileEngine::cwIambicMode() const { return m_session.radioModel().transmitModel().cwIambicMode(); }

void MobileEngine::setCwIambicMode(int mode)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setCwIambicMode(mode ? 1 : 0);
    }
}

bool MobileEngine::cwSwapPaddles() const { return m_session.radioModel().transmitModel().cwSwapPaddles(); }

void MobileEngine::setCwSwapPaddles(bool on)
{
    if (m_connected) {
        releasePaddles();
        m_session.radioModel().transmitModel().setCwSwapPaddles(on);
    }
}

bool MobileEngine::nativeKeyboardSides() const
{
    return sideKeysAvailable();
}

void MobileEngine::setKeyboardOnPage(bool on)
{
    if (on == m_keyboardOnPage) {
        return;
    }
    m_keyboardOnPage = on;
    if (!on && m_spacePtt) {
        m_spacePtt = false;
        setPtt(false, QStringLiteral("page lost keyboard focus"));
    }
    emit keyboardOnPageChanged();
}

void MobileEngine::onSideKey(int keyValue, bool pressed)
{
    const auto key = static_cast<SideKey>(keyValue);
    if (key == SideKey::Space) {
        // Hold-to-talk, as OperatePage does elsewhere: only while the page
        // has the keyboard (no dialog or text entry) and the app is in front.
        if (m_spaceLogged < 6) {
            ++m_spaceLogged;
            qCInfo(lcMobile) << "Space" << (pressed ? "down" : "up") << "(GameController)"
                             << (m_keyboardOnPage ? "" : "- page has no keyboard focus");
        }
        if (pressed) {
            if (m_spacePtt || !m_keyboardOnPage || QGuiApplication::applicationState() != Qt::ApplicationActive) {
                return;
            }
            if (pttToggle() && !cwMode()) {
                togglePtt();
                return;
            }
            m_spacePtt = true;
            setPtt(true);
        } else if (m_spacePtt) {
            m_spacePtt = false;
            setPtt(false, QStringLiteral("space released"));
        }
        return;
    }
    const bool control = key == SideKey::LeftControl || key == SideKey::RightControl;
    if (cwKeyboardPaddles() != (control ? QLatin1String("ctrl") : QLatin1String("alt"))) {
        return;
    }
    const bool left = key == SideKey::LeftControl || key == SideKey::LeftOption;
    bool& held = left ? m_sideDit : m_sideDah;
    if (held == pressed) {
        return;
    }
    if (pressed) {
        if (!cwMode() || !cwPaddles() || QGuiApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }
        // Typing in a text field (Option gives special characters) keys
        // nothing.
        if (QObject* focus = QGuiApplication::focusObject()) {
            QInputMethodQueryEvent query(Qt::ImEnabled);
            QCoreApplication::sendEvent(focus, &query);
            if (query.value(Qt::ImEnabled).toBool()) {
                return;
            }
        }
    }
    held = pressed;
    if (pressed && m_sideKeysLogged < 6) {
        ++m_sideKeysLogged;
        qCInfo(lcMobile).noquote() << "CW keyboard paddles:" << (left ? "dit from left" : "dah from right")
                                   << (control ? "Control" : "Option") << "(GameController)";
    }
    setCwPaddleState(m_sideDit, m_sideDah);
}

void MobileEngine::logKeyboardPaddles(const QString& what)
{
    qCInfo(lcMobile).noquote() << "CW keyboard paddles:" << what.left(200);
}

void MobileEngine::noteBreakInOff()
{
    if (m_session.radioModel().transmitModel().cwBreakIn()) {
        return;
    }
    static QElapsedTimer shown;
    if (!shown.isValid() || shown.elapsed() > 10000) {
        shown.start();
        qCInfo(lcMobile) << "CW keyed with break-in off";
        setLastError(tr("Break-in is off, so the key does not put the radio on air. Turn on Break-in in the CW tab."));
    }
}

void MobileEngine::syncIambicKeyer()
{
    if (!m_iambicKeyer) {
        return;
    }
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    m_iambicKeyer->setMode(tm.cwIambicMode() == 0 ? IambicKeyer::Mode::IambicA : IambicKeyer::Mode::IambicB);
    m_iambicKeyer->setWpm(tm.cwSpeed());
    m_iambicKeyer->setSwapPaddles(tm.cwSwapPaddles());
}

void MobileEngine::setCwPaddleState(bool dit, bool dah)
{
    if (!m_iambicKeyer) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    const bool held = dit || dah;
    // A new press needs a radio that can key and no TUNE carrier; a release
    // always goes through, so nothing is left keyed.
    if (held && !m_cwPaddleHeld
        && (!cwKeyAvailable() || !rm.transmitModel().admitsCwKeyEdge(true))) {
        return;
    }
    if (held && !m_cwPaddleHeld) {
        m_cwPaddleInput = rm.localTxController()->capture(TxController::Activity::CwKey).request();
        noteBreakInOff();
    }
    m_cwPaddleHeld = held;
    rm.setCwPaddleHeld(held);
    if (!m_iambicKeyer->isRunning()) {
        syncIambicKeyer();
        m_iambicKeyer->start();
    }
    m_iambicKeyer->setPaddleState(dit, dah, m_cwPaddleInput);
}

void MobileEngine::releasePaddles()
{
    if (m_iambicKeyer && m_cwPaddleHeld) {
        m_cwPaddleHeld = false;
        m_session.radioModel().setCwPaddleHeld(false);
        m_iambicKeyer->setPaddleState(false, false, m_cwPaddleInput);
    }
}

void MobileEngine::sendCwx(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !cwxAvailable()) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    if (!rm.cwxModel().canSend()) {
        setLastError(tr("CW text cannot be sent right now."));
        return;
    }
    m_cwxInput = rm.localTxController()->capture(TxController::Activity::Cwx);
    if (!m_cwxInput.send(trimmed)) {
        setLastError(tr("The radio did not accept the CW text."));
        return;
    }
    qCInfo(lcMobile) << "CWX sent" << trimmed.size() << "characters";
    m_cwxLiveInDecoder = false;
    // What we send shows in the decoder's text too, marked, so the exchange
    // reads both ways (the receiver hears nothing of it while transmitting).
    appendCwDecoded(QStringLiteral("\n[TX] %1\n").arg(trimmed.toUpper()));
    emit cwDecoderChanged();
}

void MobileEngine::sendCwxChar(const QString& ch)
{
    // Live mode (the desktop's CWX live typing): each character to the
    // radio's keyer as it is typed.
    if (ch.isEmpty() || !cwxAvailable()) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    if (!m_cwxInput.valid() || !m_cwxInput.active()) {
        if (!rm.cwxModel().canSend()) {
            setLastError(tr("CW text cannot be sent right now."));
            return;
        }
        m_cwxInput = rm.localTxController()->capture(TxController::Activity::Cwx);
    }
    if (!m_cwxInput.sendChar(ch.toUpper())) {
        return;
    }
    // Typed text joins the decoder's text under one [TX] mark per burst.
    if (!m_cwxLiveInDecoder) {
        appendCwDecoded(QStringLiteral("\n[TX] "));
        m_cwxLiveInDecoder = true;
    }
    appendCwDecoded(ch.toUpper());
    emit cwDecoderChanged();
}

void MobileEngine::stopCwx()
{
    m_cwxLiveInDecoder = false;
    if (m_cwxInput.valid()) {
        m_cwxInput.stop();
        m_cwxInput = {};
    }
    m_session.radioModel().cwxModel().clearBuffer();
}

int MobileEngine::stepHz() const
{
    return m_slice && m_slice->stepHz() > 0 ? m_slice->stepHz() : 100;
}

QVariantList MobileEngine::stations() const
{
    const RadioModel& rm = m_session.radioModel();
    const quint32 ours = rm.ourClientHandle();
    QVariantList list;
    const auto& infos = rm.clientInfoMap();
    for (auto it = infos.cbegin(); it != infos.cend(); ++it) {
        list.append(QVariantMap{
            {QStringLiteral("handle"), static_cast<int>(it.key())},
            {QStringLiteral("station"), it->station.isEmpty() ? QStringLiteral("(unnamed)") : it->station},
            {QStringLiteral("program"), it->program},
            {QStringLiteral("ours"), it.key() == ours},
            // The radio's local_ptt: this station holds the PTT right.
            {QStringLiteral("localPtt"), it->localPtt},
        });
    }
    return list;
}

void MobileEngine::disconnectStation(int handle)
{
    RadioModel& rm = m_session.radioModel();
    const quint32 h = static_cast<quint32>(handle);
    if (h == 0 || h == rm.ourClientHandle()) {
        return;
    }
    const QString name = rm.clientInfoMap().value(h).station;
    qCInfo(lcMobile).noquote() << QStringLiteral("disconnecting station %1 (0x%2)").arg(name).arg(h, 0, 16);
    if (!rm.disconnectClient(h)) {
        setLastError(tr("The radio did not take the disconnect for %1.").arg(name));
    }
}

QList<int> MobileEngine::stepList() const
{
    // The radio's list for the slice's mode; the desktop's ladder until it
    // has sent one.
    if (m_slice && !m_slice->stepList().isEmpty()) {
        const QVector<int> list = m_slice->stepList();
        return QList<int>(list.cbegin(), list.cend());
    }
    return {1, 10, 50, 100, 500, 1000, 2000, 3000};
}

void MobileEngine::setStepHz(int hz)
{
    if (!m_slice || hz <= 0 || hz == m_slice->stepHz()) {
        return;
    }
    // As MainWindow::applyOperatorTuningStep: the radio owns the slice's
    // step; backends without a command plane keep it on the client.
    RadioModel& rm = m_session.radioModel();
    if (!rm.applyClientOwnedSliceStep(m_slice->sliceId(), hz)) {
        // The radio's answer goes to the log: a step that does not take shows why.
        const QString cmd = QStringLiteral("slice set %1 step=%2").arg(m_slice->sliceId()).arg(hz);
        rm.sendCmdPublic(cmd, [cmd](int code, const QString& body) {
            if (code != 0) {
                qCWarning(lcMobile).noquote() << QStringLiteral("tuning step refused: %1 -> 0x%2 %3")
                                                     .arg(cmd).arg(code, 8, 16, QLatin1Char('0')).arg(body);
            }
        });
    }
    // The radio takes the step (answer 0) but sends no slice status for it,
    // so the app shows it at once, as the desktop's STEP control does.
    m_slice->applyRecalledStepHz(hz);
    qCInfo(lcMobile) << "tuning step" << hz << "Hz";
}

bool MobileEngine::nr() const
{
    return m_slice && m_slice->nrOn();
}

void MobileEngine::setNr(bool on)
{
    if (m_slice) {
        m_slice->setNr(on);
    }
}

bool MobileEngine::nb() const
{
    return m_slice && m_slice->nbOn();
}

void MobileEngine::setNb(bool on)
{
    if (m_slice) {
        m_slice->setNb(on);
    }
}

bool MobileEngine::anf() const
{
    return m_slice && m_slice->anfOn();
}

void MobileEngine::setAnf(bool on)
{
    if (m_slice) {
        m_slice->setAnf(on);
    }
}

QString MobileEngine::agcMode() const
{
    return m_slice ? m_slice->agcMode() : QString();
}

void MobileEngine::setAgcMode(const QString& mode)
{
    if (m_slice && mode != m_slice->agcMode()) {
        m_slice->setAgcMode(mode);
        AppSettings::instance().setValue(QStringLiteral("MobileAgc_") + agcModeClass(m_slice->mode()), mode);
        AppSettings::instance().save();
    }
}

QString MobileEngine::agcModeClass(const QString& mode)
{
    static const QStringList data{QStringLiteral("DIGU"), QStringLiteral("DIGL"), QStringLiteral("RTTY"),
                                  QStringLiteral("DFM"), QStringLiteral("FDVU"), QStringLiteral("FDVL")};
    if (mode.startsWith(QLatin1String("CW"))) {
        return QStringLiteral("cw");
    }
    return data.contains(mode) ? QStringLiteral("data") : QStringLiteral("voice");
}

void MobileEngine::applyModeAgc()
{
    if (!m_slice || QDateTime::currentMSecsSinceEpoch() > m_modeAgcUntilMs) {
        return;
    }
    const QString kind = agcModeClass(m_slice->mode());
    const QString agc = AppSettings::instance()
                            .value(QStringLiteral("MobileAgc_") + kind,
                                   kind == QLatin1String("voice") ? QStringLiteral("slow") : QStringLiteral("fast"))
                            .toString();
    if (agcModes().contains(agc) && agc != m_slice->agcMode()) {
        qCInfo(lcMobile) << "AGC" << agc << "for" << m_slice->mode();
        m_slice->setAgcMode(agc);
    }
}

QStringList MobileEngine::agcModes() const
{
    // SmartSDR slice agc_mode values.
    return {QStringLiteral("off"), QStringLiteral("slow"), QStringLiteral("med"), QStringLiteral("fast")};
}

int MobileEngine::filterLow() const
{
    return m_slice ? m_slice->filterLow() : 0;
}

int MobileEngine::filterHigh() const
{
    return m_slice ? m_slice->filterHigh() : 0;
}

namespace {
bool voiceMode(const QString& m)
{
    return m == QLatin1String("USB") || m == QLatin1String("LSB") || m == QLatin1String("AM")
        || m == QLatin1String("SAM") || m == QLatin1String("DSB") || ModeFilters::isFmMode(m)
        || m == QLatin1String("DFM");
}
} // namespace

bool MobileEngine::beatCancelMode() const
{
    // Voice only, never CW or data: BC would notch the signal itself. The
    // receive audio mixes every audible slice, so every one that is not
    // muted must be in a voice mode, not just the active one.
    if (!m_slice || !voiceMode(m_slice->mode())) {
        return false;
    }
    for (SliceModel* s : m_session.radioModel().slices()) {
        if (!s->audioMute() && !voiceMode(s->mode())) {
            return false;
        }
    }
    return true;
}

bool MobileEngine::beatCancelSetting() const
{
    return AppSettings::instance().value(QStringLiteral("MobileBeatCancel"), QStringLiteral("0")).toString()
        == QLatin1String("1");
}

void MobileEngine::applyBeatCancel()
{
    if (m_audio) {
        m_audio->setBeatCancelEnabled(beatCancelSetting() && beatCancelMode());
    }
}

QStringList MobileEngine::dspKeys() const
{
    if (!m_slice) {
        return {};
    }
    // Visibility rules of VfoWidget's DSP grid.
    const QString m = m_slice->mode();
    const bool fm = ModeFilters::isFmMode(m) || m == QLatin1String("DFM");
    const bool cw = isCwMode(m);
    const RadioModel& rm = m_session.radioModel();
    QStringList keys{QStringLiteral("NR"), QStringLiteral("NB"), QStringLiteral("ANF")};
    if (cw) {
        keys << QStringLiteral("APF");
    }
    if (rm.hasLmsNoiseFilters()) {
        keys << QStringLiteral("NRL") << QStringLiteral("ANFL") << QStringLiteral("ANFT");
    }
    if (rm.hasExtendedDspFilters() && !fm) {
        keys << QStringLiteral("NRS");
        if (!cw) {
            keys << QStringLiteral("RNN");
        }
        keys << QStringLiteral("NRF");
    }
    // Wideband noise blanker: the panadapter's, for every slice on it.
    if (m_pan) {
        keys << QStringLiteral("WNB");
    }
    // Client-side: beat cancel (this app's audio, not the radio): offered
    // always, dimmed and idle while a slice that is heard is in CW or data.
    keys << QStringLiteral("BC");
    return keys;
}

bool MobileEngine::dspOn(const QString& key) const
{
    if (!m_slice) {
        return false;
    }
    if (key == QLatin1String("NR")) return m_slice->nrOn();
    if (key == QLatin1String("NB")) return m_slice->nbOn();
    if (key == QLatin1String("ANF")) return m_slice->anfOn();
    if (key == QLatin1String("APF")) return m_slice->apfOn();
    if (key == QLatin1String("NRL")) return m_slice->nrlOn();
    if (key == QLatin1String("ANFL")) return m_slice->anflOn();
    if (key == QLatin1String("ANFT")) return m_slice->anftOn();
    if (key == QLatin1String("NRS")) return m_slice->nrsOn();
    if (key == QLatin1String("RNN")) return m_slice->rnnOn();
    if (key == QLatin1String("NRF")) return m_slice->nrfOn();
    if (key == QLatin1String("WNB")) return m_pan && m_pan->wnbActive();
    if (key == QLatin1String("BC")) return beatCancelSetting() && beatCancelMode();
    return false;
}

bool MobileEngine::dspHasLevel(const QString& key) const
{
    static const QStringList kWithLevel{QStringLiteral("NR"),  QStringLiteral("NB"),   QStringLiteral("ANF"),
                                        QStringLiteral("APF"), QStringLiteral("NRL"),  QStringLiteral("ANFL"),
                                        QStringLiteral("NRS"), QStringLiteral("NRF"),  QStringLiteral("WNB")};
    return kWithLevel.contains(key);
}

int MobileEngine::dspLevel(const QString& key) const
{
    if (key == QLatin1String("WNB")) return m_pan ? m_pan->wnbLevel() : 0;
    if (!m_slice) {
        return 0;
    }
    if (key == QLatin1String("NR")) return m_slice->nrLevel();
    if (key == QLatin1String("NB")) return m_slice->nbLevel();
    if (key == QLatin1String("ANF")) return m_slice->anfLevel();
    if (key == QLatin1String("APF")) return m_slice->apfLevel();
    if (key == QLatin1String("NRL")) return m_slice->nrlLevel();
    if (key == QLatin1String("ANFL")) return m_slice->anflLevel();
    if (key == QLatin1String("NRS")) return m_slice->nrsLevel();
    if (key == QLatin1String("NRF")) return m_slice->nrfLevel();
    return 0;
}

void MobileEngine::setDspLevel(const QString& key, int level)
{
    level = std::clamp(level, 0, 100);
    if (key == QLatin1String("WNB")) {
        if (m_pan) {
            m_session.radioModel().setPanWnbLevel(level);
        }
        return;
    }
    if (!m_slice) {
        return;
    }
    if (key == QLatin1String("NR")) m_slice->setNrLevel(level);
    else if (key == QLatin1String("NB")) m_slice->setNbLevel(level);
    else if (key == QLatin1String("ANF")) m_slice->setAnfLevel(level);
    else if (key == QLatin1String("APF")) m_slice->setApfLevel(level);
    else if (key == QLatin1String("NRL")) m_slice->setNrlLevel(level);
    else if (key == QLatin1String("ANFL")) m_slice->setAnflLevel(level);
    else if (key == QLatin1String("NRS")) m_slice->setNrsLevel(level);
    else if (key == QLatin1String("NRF")) m_slice->setNrfLevel(level);
}

void MobileEngine::setDsp(const QString& key, bool on)
{
    if (!m_slice) {
        return;
    }
    if (key == QLatin1String("NR")) m_slice->setNr(on);
    else if (key == QLatin1String("NB")) m_slice->setNb(on);
    else if (key == QLatin1String("ANF")) m_slice->setAnf(on);
    else if (key == QLatin1String("APF")) m_slice->setApf(on);
    else if (key == QLatin1String("NRL")) m_slice->setNrl(on);
    else if (key == QLatin1String("ANFL")) m_slice->setAnfl(on);
    else if (key == QLatin1String("ANFT")) m_slice->setAnft(on);
    else if (key == QLatin1String("NRS")) m_slice->setNrs(on);
    else if (key == QLatin1String("RNN")) m_slice->setRnn(on);
    else if (key == QLatin1String("NRF")) m_slice->setNrf(on);
    else if (key == QLatin1String("WNB")) m_session.radioModel().setPanWnb(on);
    else if (key == QLatin1String("BC")) {
        AppSettings::instance().setValue(QStringLiteral("MobileBeatCancel"), on ? QStringLiteral("1") : QStringLiteral("0"));
        AppSettings::instance().save();
        applyBeatCancel();
        emit dspChanged();
    }
}

QList<int> MobileEngine::filterPresets() const
{
    // The desktop's per-mode ladders (VFO filter grid): 8 widths a mode.
    const QVector<int>& widths = ModeFilters::widthsForMode(mode());
    return QList<int>(widths.cbegin(), widths.cend());
}

int MobileEngine::activeFilterPreset() const
{
    // The passband's labelled width, then the nearest ladder entry: the
    // radio's own defaults differ slightly from the presets.
    if (!m_slice) {
        return 0;
    }
    const int width = ModeFilters::widthForEdges(mode(), filterLow(), filterHigh());
    int best = 0;
    int bestDiff = 0;
    for (const int preset : filterPresets()) {
        const int diff = std::abs(width - preset);
        if (diff <= std::max(60, preset / 20) && (best == 0 || diff < bestDiff)) {
            best = preset;
            bestDiff = diff;
        }
    }
    return best;
}

int MobileEngine::filterWidth() const
{
    return m_slice ? ModeFilters::widthForEdges(mode(), filterLow(), filterHigh()) : 0;
}

void MobileEngine::setFilterEdges(int lowHz, int highHz)
{
    if (!m_slice) {
        return;
    }
    lowHz = std::clamp(lowHz, -12000, 12000);
    highHz = std::clamp(highHz, -12000, 12000);
    if (highHz - lowHz < 50 || (lowHz == m_slice->filterLow() && highHz == m_slice->filterHigh())) {
        return;
    }
    m_slice->setFilterWidth(lowHz, highHz);
}

void MobileEngine::setFilterWidth(int widthHz)
{
    if (!m_slice || widthHz <= 0) {
        return;
    }
    // Edge placement per mode, as the desktop VFO places a ladder width.
    ModeFilters::SliceContext ctx;
    ctx.diguOffset = m_slice->diguOffset();
    ctx.diglOffset = m_slice->diglOffset();
    // Under RADE the widths centre on its signal (1500 Hz from the carrier),
    // not the radio's DIGU / DIGL offset (2210 Hz on a tester's radio: a
    // 1.5K pick cut RADE's 750-2250 Hz in half).
    if (radeActive()) {
        ctx.diguOffset = 1500;
        ctx.diglOffset = 1500;
    }
    ctx.rttyShift = m_slice->rttyShift();
    const ModeFilters::Edges e = ModeFilters::edgesForWidth(m_slice->mode(), widthHz, ctx);
    m_slice->setFilterWidth(e.lo, e.hi);
}

void MobileEngine::onConnectionStateChanged(bool connected)
{
    if (!connected && radeActive()) {
        deactivateRade();
        emit radeChanged();
    }
    m_connected = connected;
    m_micDefaulted = false;
    m_netSampleMs = 0;
    m_sliceRecoveryTried = false;
    m_sliceRecoveryPending = false;
    // A new session: the radio has none of our pan sizes yet.
    m_sentPanId.clear();
    m_sentPanWidth = 0;
    emit networkChanged();
    emit memoriesChanged();
    emit tnfsChanged();
    if (!connected) {
        stopVoicePreview();
        releasePaddles();
        if (m_iambicKeyer) {
            m_iambicKeyer->stop();
        }
        // The cluster feeds the radio; without one it waits (and comes back
        // with the radio when it was on).
        if (m_dx && m_dxState != QLatin1String("off")) {
            m_dx->disconnect();
            m_dxState = QStringLiteral("off");
            m_dxQueue.clear();
            emit dxClusterChanged();
        }
    }
    if (connected) {
        defaultMicToDevice();
        // Display diagnostics for the Log page (e.g. SmartLink without a
        // waterfall): what arrived in the first seconds, and on which streams.
        m_fftFramesSeen = 0;
        m_wfRowsSeen = 0;
        m_otherWfStreamSeen = 0;
        QTimer::singleShot(6000, this, [this] {
            if (!m_connected) {
                return;
            }
            qCInfo(lcMobile).noquote()
                << QStringLiteral("display check: %1 pans=%10 pan=%2 fft=0x%3 wf=0x%4 frames=%5 wfRows=%6 otherWf=0x%7 px=%8x%9")
                       .arg(m_session.radioModel().isWan() ? QStringLiteral("WAN") : QStringLiteral("LAN"))
                       .arg(m_pan ? m_pan->panId() : QStringLiteral("-"))
                       .arg(QString::number(m_pan ? m_pan->panStreamId() : 0, 16))
                       .arg(QString::number(m_pan ? m_pan->wfStreamId() : 0, 16))
                       .arg(m_fftFramesSeen).arg(m_wfRowsSeen)
                       .arg(QString::number(m_otherWfStreamSeen, 16))
                       .arg(m_panWidth).arg(m_panHeight)
                       .arg(m_session.radioModel().panadapters().size());
        });
        wireRxAudio();
        wireTxPacketSink();
        startAudio();
        setKeepScreenAwake(true);
        watchHeadsetLink();
        setConnectionState(QStringLiteral("connected"));
        selectActiveSlice();
        // A slice RADE left muted when the app last went away.
        QTimer::singleShot(3000, this, [this] { healRadeMute(); });
        // The DX cluster, when it was on or is set to connect by itself
        // (after the radio's status has brought its callsign).
        QTimer::singleShot(3000, this, [this] {
            if (m_connected && m_dxState == QLatin1String("off")
                && (m_dxWanted || dxCluster().value(QStringLiteral("autoConnect")).toBool())
                && !dxCallsign().isEmpty()) {
                connectDxCluster();
            }
        });
        emit transmitStateChanged();
        emit rfPowerChanged();
        emit tuneStateChanged();
        emit txFilterChanged();
        emit eqChanged();
        emit profilesChanged();
        emit atuChanged();
        emit txAudioChanged();
        emit cwChanged();
        updateCwDecoder();
        updateMicCheck();
        return;
    }
    stopTransmit(QStringLiteral("radio disconnected"));
    updateCwDecoder();
    if (m_wanActive && !m_wanUserDisconnect) {
        qCInfo(lcMobile) << "remote session dropped; reconnecting";
        m_wanReconnectTimer.start();
    } else {
        m_wanActive = false;
    }
    m_micCheckOpen = false;
    stopDeviceMicrophoneCapture(true);
    setKeepScreenAwake(false);
    stopAudio();
    bindSlice(nullptr);
    bindPan(nullptr);
    setConnectionState(QStringLiteral("idle"));
}

void MobileEngine::startAudio()
{
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] { audio->startRxStream(); }, Qt::QueuedConnection);
}

void MobileEngine::stopAudio()
{
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] { audio->stopRxStream(); }, Qt::QueuedConnection);
}

void MobileEngine::wireRxAudio()
{
    for (const QMetaObject::Connection& c : std::as_const(m_rxAudioConnections)) {
        disconnect(c);
    }
    m_rxAudioConnections.clear();

    RadioModel& rm = m_session.radioModel();
    IRadioBackend* backend = rm.backend();
    if (!backend) {
        return;
    }
    // Exactly one RX source per backend: seam audio for backends that own it
    // (including the demo, whose PanadapterStream carries a different scene),
    // otherwise the Flex stream's decoded PCM. Both would sum at the sink.
    if (backend->ownsRxAudio()) {
        m_rxAudioConnections.append(connect(&rm, &RadioModel::backendAudioFrameReady,
                                            m_audio, &AudioEngine::feedPcmFrame));
    } else if (PanadapterStream* ps = rm.panStream()) {
        m_rxAudioConnections.append(connect(ps, &PanadapterStream::pcmFrameReady,
                                            m_audio, &AudioEngine::feedPcmFrame));
    }
}

void MobileEngine::onSliceAdded(SliceModel* slice)
{
    // FT8 / FT4 follow the TX slice.
    connect(slice, &SliceModel::txSliceChanged, this, &MobileEngine::updateDigiSlice);
    // Any audible slice's mode or mute decides whether BC may run.
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::applyBeatCancel);
    connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::applyBeatCancel);
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::dspChanged);
    connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::dspChanged);
    // The split's TX slice: same mode as the RX slice, muted (the RX slice
    // is what you listen to), transmits, and gets focus to tune the offset.
    if (m_splitPending && slice->sliceId() != m_splitRxId) {
        m_splitPending = false;
        m_splitTxId = slice->sliceId();
        if (SliceModel* rx = m_session.radioModel().slice(m_splitRxId); rx && slice->mode() != rx->mode()) {
            slice->setMode(rx->mode());
        }
        slice->setAudioMute(true);
        slice->setTxSlice(true);
        qCInfo(lcMobile) << "split on: RX slice" << m_splitRxId << "TX slice" << m_splitTxId
                         << "at" << slice->frequency();
        QMetaObject::invokeMethod(this, [this, id = m_splitTxId] { selectSlice(id); }, Qt::QueuedConnection);
    }
    connect(slice, &SliceModel::activeChanged, this, [this](bool) { selectActiveSlice(); });
    for (auto signal : {&SliceModel::frequencyChanged}) {
        connect(slice, signal, this, &MobileEngine::slicesChanged);
    }
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::txSliceChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::letterChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::audioGainChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::audioPanChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::slicesChanged);
    emit slicesChanged();
    selectActiveSlice();
}

void MobileEngine::selectActiveSlice()
{
    const QList<SliceModel*> slices = m_session.radioModel().slices();
    SliceModel* chosen = nullptr;
    for (SliceModel* s : slices) {
        if (s->isActive()) {
            chosen = s;
            break;
        }
    }
    // With no active slice, stay on the current one rather than jumping.
    if (!chosen && m_slice && slices.contains(m_slice.data())) {
        chosen = m_slice;
    }
    if (!chosen && !slices.isEmpty()) {
        chosen = slices.first();
    }
    if (chosen != m_slice) {
        bindSlice(chosen);
    }
    PanadapterModel* pan = chosen ? m_session.radioModel().panadapter(chosen->panId()) : nullptr;
    qCDebug(lcMobile) << "selectActiveSlice slices" << slices.size() << "chosen"
                      << (chosen ? chosen->sliceId() : -1) << "pan" << (chosen ? chosen->panId() : QString())
                      << "found" << (pan != nullptr) << "pans" << m_session.radioModel().panadapters().size();
    if (pan != m_pan) {
        bindPan(pan);
    }
}

void MobileEngine::bindSlice(SliceModel* slice)
{
    for (const QMetaObject::Connection& c : std::as_const(m_sliceConnections)) {
        disconnect(c);
    }
    m_sliceConnections.clear();
    m_slice = slice;
    applyBeatCancel();
    emit fmChanged();
    if (slice) {
        m_sliceConnections.append(connect(slice, &SliceModel::frequencyChanged, this, &MobileEngine::frequencyChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::modeChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::fmToneModeChanged, this, &MobileEngine::fmChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::fmToneValueChanged, this, &MobileEngine::fmChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::repeaterOffsetDirChanged, this, &MobileEngine::fmChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::fmRepeaterOffsetFreqChanged, this, &MobileEngine::fmChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::lockedChanged, this, &MobileEngine::lockedChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::tuneBlockedByLock, this, &MobileEngine::tuneBlocked));
        // The preset set changes with the mode.
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::filterChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeListChanged, this, &MobileEngine::modesChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioGainChanged, this, &MobileEngine::afGainChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::mutedChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::filterChanged, this, &MobileEngine::filterChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nrChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nbChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::anfChanged, this, &MobileEngine::dspChanged));
        for (auto sig : {&SliceModel::nrlChanged, &SliceModel::nrsChanged, &SliceModel::rnnChanged,
                         &SliceModel::nrfChanged, &SliceModel::anflChanged, &SliceModel::anftChanged,
                         &SliceModel::apfChanged}) {
            m_sliceConnections.append(connect(slice, sig, this, &MobileEngine::dspChanged));
        }
        for (auto sig : {&SliceModel::nrLevelChanged, &SliceModel::nbLevelChanged, &SliceModel::anfLevelChanged,
                         &SliceModel::apfLevelChanged, &SliceModel::nrlLevelChanged, &SliceModel::nrsLevelChanged,
                         &SliceModel::nrfLevelChanged, &SliceModel::anflLevelChanged}) {
            m_sliceConnections.append(connect(slice, sig, this, &MobileEngine::dspChanged));
        }
        // The DSP set changes with the mode (APF in CW, none of the 8000s in FM).
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::dspChanged));
        // Beat cancel only on voice: it would notch CW and data tones.
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::applyBeatCancel));
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::applyModeAgc));
        // A band change brings the band's own AGC from the radio after ours.
        m_sliceConnections.append(connect(slice, &SliceModel::agcModeChanged, this, &MobileEngine::applyModeAgc));
        m_sliceConnections.append(connect(slice, &SliceModel::agcModeChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::agcThresholdChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::stepChanged, this, &MobileEngine::stepChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::ritChanged, this, &MobileEngine::ritXitChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::xitChanged, this, &MobileEngine::ritXitChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::squelchChanged, this, &MobileEngine::squelchChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::rxAntennaChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::txAntennaChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::rxAntennaListChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::txAntennaListChanged, this, &MobileEngine::antennaChanged));
    }
    emit frequencyChanged();
    emit lockedChanged();
    emit modeChanged();
    emit modesChanged();
    emit afGainChanged();
    emit mutedChanged();
    emit filterChanged();
    emit dspChanged();
    emit stepChanged();
    emit ritXitChanged();
    emit squelchChanged();
    emit antennaChanged();
    emit slicesChanged();
}

void MobileEngine::bindPan(PanadapterModel* pan)
{
    for (const QMetaObject::Connection& c : std::as_const(m_panConnections)) {
        disconnect(c);
    }
    m_panConnections.clear();
    m_pan = pan;
    if (pan) {
        m_panConnections.append(connect(pan, &PanadapterModel::infoChanged, this, &MobileEngine::panGeometryChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::rfGainChanged, this, &MobileEngine::rfGainChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::wnbChanged, this, &MobileEngine::dspChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::averageChanged, this, &MobileEngine::panDisplayChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::fpsReported, this, &MobileEngine::panDisplayChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::waterfallLineDurationChanged, this,
                                        &MobileEngine::panDisplayChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::bandZoomChanged, this, &MobileEngine::panDisplayChanged));
        // A band change restores the band's stored dBm scale: put ours back.
        m_panConnections.append(connect(pan, &PanadapterModel::levelChanged, this, [this, id = pan->panId()] {
            QTimer::singleShot(300, this, [this, id] { ensurePanDbmRange(id); });
        }));
        m_panConnections.append(connect(pan, &PanadapterModel::rfGainInfoChanged, this, &MobileEngine::rfGainChanged));
        sendPanDimensions(pan->panId());
    }
    emit panGeometryChanged();
    emit rfGainChanged();
}

void MobileEngine::setPanPixels(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    // The radio takes only the width (the height encoding is fixed).
    const bool widthChanged = width != m_panWidth;
    m_panWidth = width;
    m_panHeight = height;
    if (!widthChanged) {
        return;
    }
    // Each new width restarts the radio's FFT stream at the new geometry, so
    // only a size that has held for a moment is sent; layout settling and
    // rotation produce bursts of intermediate sizes.
    m_panPixelsTimer.start();
}

void MobileEngine::sendPanDimensions(const QString& panId, bool force)
{
    RadioModel& rm = m_session.radioModel();
    if (panId.isEmpty() || m_panWidth <= 0 || m_panHeight <= 0 || !rm.panadapter(panId)) {
        return;
    }
    // The radio's pan and waterfall both report the reset in one go, which
    // asked twice: the same size within half a second is sent once.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool same = panId == m_sentPanId && m_panWidth == m_sentPanWidth;
    if (same && (!force || nowMs - m_sentPanSizeMs < 500)) {
        ensurePanDbmRange(panId);
        return;
    }
    m_sentPanId = panId;
    m_sentPanWidth = m_panWidth;
    m_sentPanSizeMs = nowMs;
    // Self-shaping backends take the width through the seam; the
    // xpixels/ypixels text is the Flex command-plane contract.
    // ypixels sets only the FFT's vertical resolution (the view scales the
    // dBm values itself), so it stays fixed at kPanYPixels.
    const int yPixels = kPanYPixels;
    if (!rm.requestLocalPanPixelWidth(panId, m_panWidth) && rm.hasCommandPlane()) {
        rm.sendCommand(QStringLiteral("display pan set %1 xpixels=%2 ypixels=%3")
                           .arg(panId).arg(m_panWidth).arg(yPixels));
    }
    ensurePanDbmRange(panId);
}

void MobileEngine::ensurePanDbmRange(const QString& panId)
{
    // The radio keeps a dBm scale per band and restores it on a band change
    // (160 m came back at -112..-17 dBm, pinning 96 % of the bins flat), so
    // the app's wide scale goes back whenever the pan reports another one.
    // At most every 2 s, so a client that insists cannot start a tug of war.
    RadioModel& rm = m_session.radioModel();
    const PanadapterModel* pan = rm.panadapter(panId);
    if (!pan || !rm.hasCommandPlane()
        || (std::abs(pan->minDbm() - kPanMinDbm) <= 0.5f && std::abs(pan->maxDbm() - kPanMaxDbm) <= 0.5f)) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastDbmRangeSendMs < 2000) {
        // Too soon after the last write: look again once the 2 s have passed.
        if (!m_dbmRangeRetryPending) {
            m_dbmRangeRetryPending = true;
            QTimer::singleShot(2050 - (now - m_lastDbmRangeSendMs), this, [this, panId] {
                m_dbmRangeRetryPending = false;
                ensurePanDbmRange(panId);
            });
        }
        return;
    }
    m_lastDbmRangeSendMs = now;
    qCInfo(lcMobile).noquote() << QStringLiteral("pan dBm scale %1..%2 -> %3..%4")
                                      .arg(pan->minDbm(), 0, 'f', 0).arg(pan->maxDbm(), 0, 'f', 0)
                                      .arg(kPanMinDbm, 0, 'f', 0).arg(kPanMaxDbm, 0, 'f', 0);
    rm.sendCommand(QStringLiteral("display pan set %1 min_dbm=%2 max_dbm=%3")
                       .arg(panId)
                       .arg(static_cast<double>(kPanMinDbm), 0, 'f', 2)
                       .arg(static_cast<double>(kPanMaxDbm), 0, 'f', 2));
}

double MobileEngine::frequencyMhz() const
{
    return m_slice ? m_slice->frequency() : 0.0;
}

void MobileEngine::setFrequencyMhz(double mhz)
{
    if (m_slice && mhz > 0.0) {
        m_slice->setFrequency(mhz);
    }
}

QString MobileEngine::mode() const
{
    return m_slice ? m_slice->mode() : QString();
}

void MobileEngine::setMode(const QString& mode)
{
    if (m_slice && mode != m_slice->mode()) {
        // Out of DIGU / DIGL RADE has nothing to decode; off first, which
        // also unmutes the slice.
        if (mode != QLatin1String("DIGU") && mode != QLatin1String("DIGL")) {
            stopRadeBeforeLeaving("mode change");
        }
        if (mode == m_slice->mode()) {
            return;  // RADE's own restore already set it
        }
        armModeAgc();
        m_slice->setMode(mode);
    }
}

QStringList MobileEngine::modes() const
{
    if (!m_slice) {
        return {};
    }
    const QStringList preferred{QStringLiteral("LSB"), QStringLiteral("USB"), QStringLiteral("CW"),
                                QStringLiteral("AM"), QStringLiteral("FM"), QStringLiteral("DIGU")};
    const QStringList available = m_slice->modeList();
    if (available.isEmpty()) {
        return preferred;
    }
    // The common modes first, then whatever else the radio offers in its own
    // order (DIGL, SAM, NFM, RTTY, and waveform modes such as FreeDV's FDVU/FDVL).
    QStringList result;
    for (const QString& m : preferred) {
        if (available.contains(m)) {
            result.append(m);
        }
    }
    for (const QString& m : available) {
        if (!result.contains(m)) {
            result.append(m);
        }
    }
    return result;
}

double MobileEngine::afGain() const
{
    return m_slice ? m_slice->audioGain() : 0.0;
}

void MobileEngine::setAfGain(double gain)
{
    if (m_slice) {
        m_slice->setAudioGain(static_cast<float>(std::clamp(gain, 0.0, 100.0)));
    }
}

bool MobileEngine::muted() const
{
    return m_slice && m_slice->audioMute();
}

void MobileEngine::setMuted(bool muted)
{
    if (m_slice) {
        m_slice->setAudioMute(muted);
    }
}

QString MobileEngine::sMeterText() const
{
    if (m_sMeterDbm <= kS9Dbm) {
        const int s = std::max(0, static_cast<int>(std::lround(9.0 + (m_sMeterDbm - kS9Dbm) / kDbPerSUnit)));
        return QStringLiteral("S%1  %2 dBm").arg(s).arg(m_sMeterDbm, 0, 'f', 0);
    }
    return QStringLiteral("S9+%1  %2 dBm").arg(std::lround(m_sMeterDbm - kS9Dbm)).arg(m_sMeterDbm, 0, 'f', 0);
}

double MobileEngine::panCenterMhz() const
{
    if (!m_pan) {
        return 0.0;
    }
    // While a drag moves the view, show where it is going, not the radio's
    // last echo, so the waterfall and markers follow the finger at once
    // (as SpectrumWidget's pan-drag preview does).
    if (m_panPreviewUntilMs > QDateTime::currentMSecsSinceEpoch()) {
        return m_panPreviewMhz;
    }
    return m_pan->centerMhz();
}

double MobileEngine::panBandwidthMhz() const
{
    return m_pan ? m_pan->bandwidthMhz() : 0.0;
}

void MobileEngine::setConnectionState(const QString& state)
{
    if (state != m_connectionState) {
        m_connectionState = state;
        qCInfo(lcMobile) << "connection state" << state;
        emit connectionStateChanged();
    }
}

void MobileEngine::setLastError(const QString& error)
{
    // Every notice is announced, even a repeat of the last one: the display
    // shows each as a popup (lastErrorChanged), so a second refusal must
    // show again.
    m_lastError = error;
    emit lastErrorChanged();
}

QString MobileEngine::appVersion() const
{
    return QStringLiteral("%1 (%2)").arg(QCoreApplication::applicationVersion(),
                                         QStringLiteral(AETHER_MOBILE_BUILD_NUMBER));
}

QString MobileEngine::manualUrl() const
{
    return QStringLiteral("https://github.com/OH6OKSA/OksaSDR/blob/HEAD/docs/mobile/MANUAL.md");
}

int MobileEngine::cpuCores() const
{
    return std::max(1, QThread::idealThreadCount());
}

QString MobileEngine::networkQuality() const
{
    return m_connected ? m_session.radioModel().networkQuality() : QStringLiteral("Off");
}

int MobileEngine::networkRttMs() const
{
    return m_connected ? m_session.radioModel().lastPingRtt() : 0;
}

QVariantMap MobileEngine::networkDetails()
{
    const RadioModel& rm = m_session.radioModel();
    QVariantMap d;
    d[QStringLiteral("quality")] = networkQuality();
    d[QStringLiteral("rttMs")] = networkRttMs();
    d[QStringLiteral("path")] = remoteSession() ? QStringLiteral("SmartLink (internet)") : QStringLiteral("Local network");
    d[QStringLiteral("radio")] = rm.targetRadioIp();
    d[QStringLiteral("lossPercent")] = rm.packetLossPercent();
    d[QStringLiteral("lossWindowS")] = rm.packetLossWindowSeconds();
    d[QStringLiteral("lossDrops")] = rm.packetLossWindowDrops();
    d[QStringLiteral("lossPackets")] = rm.packetLossWindowPackets();
    d[QStringLiteral("audioGapMs")] = rm.audioPacketGapMs();
    d[QStringLiteral("audioGapMaxMs")] = rm.audioPacketGapMaxMs();
    d[QStringLiteral("jitterMs")] = rm.audioPacketJitterMs();
    d[QStringLiteral("sessionDrops")] = rm.packetDropCount();
    d[QStringLiteral("sessionPackets")] = rm.packetTotalCount();
    QVariantList streams;
    if (rm.hasStreamCategoryStats()) {
        const std::pair<const char*, PanadapterStream::StreamCategory> cats[] = {
            {"Audio", PanadapterStream::CatAudio}, {"FFT", PanadapterStream::CatFFT},
            {"Waterfall", PanadapterStream::CatWaterfall}, {"Meters", PanadapterStream::CatMeter},
        };
        for (const auto& [name, cat] : cats) {
            const PanadapterStream::CategoryStats st = rm.categoryStats(cat);
            streams << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(name)},
                                   {QStringLiteral("packets"), st.packets},
                                   {QStringLiteral("errors"), st.errors}};
        }
    }
    d[QStringLiteral("streams")] = streams;
    // Rates since the previous call (the dialog polls once a second).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 rx = rm.rxBytes();
    const qint64 tx = rm.txBytes();
    double rxKbps = 0.0;
    double txKbps = 0.0;
    if (m_netSampleMs > 0 && now > m_netSampleMs && rx >= m_netRxBytes && tx >= m_netTxBytes) {
        const double s = (now - m_netSampleMs) / 1000.0;
        rxKbps = (rx - m_netRxBytes) * 8.0 / 1000.0 / s;
        txKbps = (tx - m_netTxBytes) * 8.0 / 1000.0 / s;
    }
    m_netRxBytes = rx;
    m_netTxBytes = tx;
    m_netSampleMs = now;
    d[QStringLiteral("rxKbps")] = rxKbps;
    d[QStringLiteral("txKbps")] = txKbps;
    d[QStringLiteral("rxMB")] = rx / 1e6;
    d[QStringLiteral("txMB")] = tx / 1e6;
    return d;
}

QVariantMap MobileEngine::radioSetup() const
{
    const RadioModel& rm = m_session.radioModel();
    const TransmitModel& tx = rm.transmitModel();
    QVariantMap d;
    d[QStringLiteral("callsign")] = callsign();
    d[QStringLiteral("nickname")] = rm.nickname();
    d[QStringLiteral("model")] = rm.model();
    d[QStringLiteral("serial")] = m_redactIdentity ? QStringLiteral("(hidden)")
                                : rm.serial().isEmpty() ? rm.chassisSerial() : rm.serial();
    d[QStringLiteral("version")] = rm.version();
    d[QStringLiteral("region")] = rm.region();
    d[QStringLiteral("ip")] = rm.targetRadioIp();
    d[QStringLiteral("remoteOn")] = rm.remoteOnEnabled();
    d[QStringLiteral("muteLocalWhenRemote")] = rm.muteLocalWhenRemote();
    d[QStringLiteral("maxPower")] = tx.maxPowerLevel();
    d[QStringLiteral("txDelay")] = tx.txDelay();
    d[QStringLiteral("interlockTimeoutS")] = tx.interlockTimeout() / 1000;
    d[QStringLiteral("tuneMode")] = tx.tuneMode();
    d[QStringLiteral("cwDelay")] = tx.cwDelay();
    d[QStringLiteral("iambic")] = tx.cwIambic();
    d[QStringLiteral("iambicMode")] = tx.cwIambicMode();
    d[QStringLiteral("swapPaddles")] = tx.cwSwapPaddles();
    d[QStringLiteral("micBias")] = tx.micBias();
    d[QStringLiteral("micBoost")] = tx.micBoost();
    d[QStringLiteral("lineoutGain")] = rm.lineoutGain();
    d[QStringLiteral("lineoutMute")] = rm.lineoutMute();
    d[QStringLiteral("headphoneGain")] = rm.headphoneGain();
    d[QStringLiteral("headphoneMute")] = rm.headphoneMute();
    d[QStringLiteral("networkMtu")] = rm.networkMtuSetting();
    return d;
}

void MobileEngine::setRadioSetup(const QString& key, const QVariant& value)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    TransmitModel& tx = rm.transmitModel();
    const bool flex = rm.usesFlexCommandPlane();
    qCInfo(lcMobile) << "radio setup:" << key << "=" << value;
    if (key == QLatin1String("callsign")) {
        setCallsign(value.toString());
    } else if (key == QLatin1String("nickname") && flex) {
        const QString name = value.toString().trimmed();
        if (!name.isEmpty()) {
            rm.sendCommand(QStringLiteral("radio name ") + name);
        }
    } else if (key == QLatin1String("remoteOn")) {
        rm.setRemoteOnEnabled(value.toBool());
    } else if (key == QLatin1String("muteLocalWhenRemote") && flex) {
        rm.setMuteLocalWhenRemote(value.toBool());
    } else if (key == QLatin1String("maxPower") && flex) {
        rm.sendCommand(QStringLiteral("transmit set max_power_level=%1").arg(std::clamp(value.toInt(), 0, 100)));
    } else if (key == QLatin1String("txDelay") && flex) {
        rm.sendCommand(QStringLiteral("interlock set tx_delay=%1").arg(std::max(0, value.toInt())));
    } else if (key == QLatin1String("interlockTimeoutS") && flex) {
        // The radio takes the timeout in milliseconds (FlexLib Radio.cs).
        rm.sendCommand(QStringLiteral("interlock set timeout=%1").arg(std::max(0, value.toInt()) * 1000));
    } else if (key == QLatin1String("tuneMode")) {
        tx.setTuneMode(value.toString());
    } else if (key == QLatin1String("cwDelay")) {
        tx.setCwDelay(std::clamp(value.toInt(), 0, 2000));
    } else if (key == QLatin1String("iambic")) {
        tx.setCwIambic(value.toBool());
    } else if (key == QLatin1String("iambicMode")) {
        tx.setCwIambicMode(value.toInt() ? 1 : 0);
    } else if (key == QLatin1String("swapPaddles")) {
        tx.setCwSwapPaddles(value.toBool());
    } else if (key == QLatin1String("micBias")) {
        tx.setMicBias(value.toBool());
    } else if (key == QLatin1String("micBoost")) {
        tx.setMicBoost(value.toBool());
    } else if (key == QLatin1String("lineoutGain")) {
        rm.setLineoutGain(std::clamp(value.toInt(), 0, 100));
    } else if (key == QLatin1String("lineoutMute")) {
        rm.setLineoutMute(value.toBool());
    } else if (key == QLatin1String("headphoneGain")) {
        rm.setHeadphoneGain(std::clamp(value.toInt(), 0, 100));
    } else if (key == QLatin1String("headphoneMute")) {
        rm.setHeadphoneMute(value.toBool());
    } else if (key == QLatin1String("networkMtu")) {
        // The app's own setting (VITA-49 packet size the radio sends us),
        // as the desktop's Network MTU: saved, then sent (capped over a
        // tailnet by the core).
        const int mtu = std::clamp(value.toInt(), kMinNetworkMtu, kMaxNetworkMtu);
        AppSettings::instance().setValue(QStringLiteral("NetworkMtu"), QString::number(mtu));
        AppSettings::instance().save();
        if (flex) {
            rm.sendCommand(QStringLiteral("client set enforce_network_mtu=1 network_mtu=%1")
                               .arg(rm.networkMtuParam(mtu)));
        }
    }
}

bool MobileEngine::setupSuggested() const
{
    const RadioModel& rm = m_session.radioModel();
    if (!m_connected || !rm.usesFlexCommandPlane() || !callsign().isEmpty()) {
        return false;
    }
    const QString key = QStringLiteral("MobileSetupOffered_%1").arg(rm.serial());
    return AppSettings::instance().value(key, QStringLiteral("0")).toString() != QLatin1String("1");
}

void MobileEngine::dismissSetupSuggestion()
{
    const QString key = QStringLiteral("MobileSetupOffered_%1").arg(m_session.radioModel().serial());
    AppSettings::instance().setValue(key, QStringLiteral("1"));
    AppSettings::instance().save();
}

namespace {
const QString kMobileAudioCompressionKey = QStringLiteral("MobileAudioCompression");

// The radio is reached through a VPN: no local network holds its address,
// and a tunnel interface (iOS utun/ipsec, wg, tun, ppp) carries IPv4. A
// routed home network without a tunnel is not a VPN.
bool radioThroughVpn(const QHostAddress& radio)
{
    if (radio.protocol() != QAbstractSocket::IPv4Protocol || radio.isLoopback()) {
        return false;
    }
    bool tunnel = false;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }
        const QString name = iface.name();
        const bool tunnelName = name.startsWith(QLatin1String("utun")) || name.startsWith(QLatin1String("ipsec"))
            || name.startsWith(QLatin1String("wg")) || name.startsWith(QLatin1String("tun"))
            || name.startsWith(QLatin1String("ppp"));
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            if (tunnelName) {
                tunnel = true;
            } else if (entry.prefixLength() > 0 && radio.isInSubnet(entry.ip(), entry.prefixLength())) {
                return false;   // on a local network
            }
        }
    }
    return tunnel;
}
} // namespace

QString MobileEngine::audioBuffer() const
{
    const QString v = AppSettings::instance().value(QStringLiteral("MobileAudioBuffer"), QStringLiteral("Normal")).toString();
    return (v == QLatin1String("Low") || v == QLatin1String("Smooth")) ? v : QStringLiteral("Normal");
}

void MobileEngine::setAudioBuffer(const QString& mode)
{
    if (mode == audioBuffer()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobileAudioBuffer"), mode);
    AppSettings::instance().save();
    applyAudioBuffer();
    emit audioCompressionChanged();
}

QString MobileEngine::audioOutput() const
{
    const QString v = AppSettings::instance().value(QStringLiteral("MobileAudioOutput"), QStringLiteral("Auto")).toString();
    return (v == QLatin1String("Speaker") || v == QLatin1String("Bluetooth")) ? v : QStringLiteral("Auto");
}

void MobileEngine::setAudioOutput(const QString& mode)
{
    if (mode == audioOutput()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobileAudioOutput"), mode);
    AppSettings::instance().save();
    setAudioOutputMode(audioOutput());
    applyAudioOutputDevice();
    watchHeadsetLink();
    qCInfo(lcMobile).noquote() << "audio output:" << audioOutput();
    emit audioCompressionChanged();
}

// Android: Qt opens the receive and microphone streams each on one device
// (an AAudio stream with that device's id), and Android's routing (the
// communication device, the speakerphone) does not move them afterwards:
// with Bluetooth on, Speaker kept playing in the Bluetooth device and the
// microphone was the headset's in every choice (beta 37, the user's phone;
// a Bluetooth microphone also turns the link to hands-free, so Bluetooth
// media lost its sound). Speaker opens the receive stream on the built-in
// speaker; Auto and Bluetooth on Qt's default output (a connected Bluetooth
// device first). The microphone: Auto Qt's default input (a Bluetooth
// headset's first), Speaker and Bluetooth the device's own. Running streams
// restart on the new device.
void MobileEngine::applyAudioOutputDevice()
{
#ifdef Q_OS_ANDROID
    if (!m_audio) {
        return;
    }
    const auto named = [](const QList<QAudioDevice>& devices, QLatin1String description) {
        for (const QAudioDevice& candidate : devices) {
            if (candidate.description() == description) {
                return candidate;
            }
        }
        return QAudioDevice{};
    };
    const QString mode = audioOutput();
    const QAudioDevice output = mode == QLatin1String("Speaker")
        ? named(QMediaDevices::audioOutputs(), QLatin1String("Built in speaker")) : QAudioDevice{};
    const QAudioDevice input = mode == QLatin1String("Auto")
        ? QAudioDevice{} : named(QMediaDevices::audioInputs(), QLatin1String("Built in microphone"));
    if (m_outputDeviceApplied && output.id() == m_outputDeviceId && input.id() == m_inputDeviceId) {
        return;
    }
    m_outputDeviceApplied = true;
    m_outputDeviceId = output.id();
    m_inputDeviceId = input.id();
    const auto name = [](const QAudioDevice& device) {
        return device.isNull() ? QStringLiteral("the system default") : device.description();
    };
    qCInfo(lcMobile).noquote() << "audio devices: output" << name(output) << "/ microphone" << name(input);
    // The microphone first: opening it can change Android's route, which
    // ends a receive stream opened before it.
    // Receive opens again if the change left it stopped (an open refused
    // while Android changed the route, beta 39: it stayed silent).
    QMetaObject::invokeMethod(m_audio, [audio = m_audio, output, input, connected = m_connected] {
        audio->setInputDevice(input, AudioDeviceSelection::Fallback);
        audio->setOutputDevice(output, AudioDeviceSelection::Fallback);
        if (connected) {
            audio->restartRxSoon();
        }
    }, Qt::QueuedConnection);
#endif
}

// Android, Auto with a Bluetooth headset: Android brings the headset's
// hands-free link up a moment after the route is asked for, and a receive
// stream opened before it stays where it opened. From Speaker the route
// change ended the stream and it reopened on the headset; from Bluetooth
// media it stayed on the headset's music link, which the headset shuts while
// hands-free is up: no sound in the headset until the next change (beta 41).
// Every quarter second for 4 s: when the link came up after receive opened,
// receive opens again; with no word from Android in 4 s but the headset as
// the route, once anyway.
void MobileEngine::watchHeadsetLink()
{
#ifdef Q_OS_ANDROID
    if (!m_connected || audioOutput() != QLatin1String("Auto")) {
        m_headsetLinkTimer.stop();
        return;
    }
    m_headsetLinkTicks = 0;
    m_headsetLinkTimer.start();
#endif
}

void MobileEngine::applyAudioBuffer()
{
    const QString mode = audioBuffer();
    const int capMs = mode == QLatin1String("Low") ? 100 : mode == QLatin1String("Smooth") ? 400 : 200;
    const int prebufferMs = mode == QLatin1String("Low") ? 0 : mode == QLatin1String("Smooth") ? 150 : 60;
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, capMs, prebufferMs] {
        audio->setRxBufferCapMs(capMs);
        audio->setReceivePresentationDelays(prebufferMs, 0);
    }, Qt::QueuedConnection);
    qCInfo(lcMobile).noquote() << QStringLiteral("receive audio buffer: %1 (%2 ms, prebuffer %3 ms)")
                                      .arg(mode).arg(capMs).arg(prebufferMs);
}

QString MobileEngine::audioCompression() const
{
    const QString v = AppSettings::instance().value(kMobileAudioCompressionKey, QStringLiteral("Auto")).toString();
    return (v == QLatin1String("Opus") || v == QLatin1String("None")) ? v : QStringLiteral("Auto");
}

void MobileEngine::setAudioCompression(const QString& mode)
{
    if (mode == audioCompression()) {
        return;
    }
    AppSettings::instance().setValue(kMobileAudioCompressionKey, mode);
    AppSettings::instance().save();
    emit audioCompressionChanged();
}

void MobileEngine::applyAudioCompression(const QHostAddress& radio, bool wan)
{
    const QString choice = audioCompression();
    QString core = choice;   // the core's AudioCompression: Auto, Opus or None
    QString why;
    if (choice == QLatin1String("Auto")) {
        if (wan) {
            why = QStringLiteral("SmartLink");
        } else if (radioThroughVpn(radio)) {
            core = QStringLiteral("Opus");
            why = QStringLiteral("VPN");
        }
    }
    AppSettings::instance().setValue(QStringLiteral("AudioCompression"), core);
    const bool opus = core == QLatin1String("Opus") || (core == QLatin1String("Auto") && wan);
    m_audioCodingInUse = opus ? (why.isEmpty() ? QStringLiteral("Opus") : QStringLiteral("Opus (%1)").arg(why))
                              : QStringLiteral("Uncompressed");
    qCInfo(lcMobile).noquote() << "receive audio:" << m_audioCodingInUse << "(setting" << choice + ")";
    emit audioCompressionChanged();
}

namespace {
const QString kBandPlanKey = QStringLiteral("MobileBandPlan");
}

QString MobileEngine::bandPlan() const
{
    // Unset: IARU Region 1 (the app's first users are in Europe); "Off"
    // hides the strip.
    const QString v = AppSettings::instance().value(kBandPlanKey, QStringLiteral("IARU Region 1")).toString();
    return v == QLatin1String("Off") ? QString() : v;
}

void MobileEngine::setBandPlan(const QString& name)
{
    const QString stored = name.isEmpty() ? QStringLiteral("Off") : name;
    AppSettings::instance().setValue(kBandPlanKey, stored);
    AppSettings::instance().save();
    if (!name.isEmpty()) {
        m_bandPlan.setActivePlan(name);
    }
    emit bandPlanChanged();
}

QStringList MobileEngine::bandPlans() const
{
    return m_bandPlan.availablePlans();
}

QVector<BandPlanManager::Segment> MobileEngine::bandPlanSegments() const
{
    if (bandPlan().isEmpty() || m_bandPlan.activePlanName() != bandPlan()) {
        return {};
    }
    return m_bandPlan.segments();
}

QString MobileEngine::callsign() const
{
    return m_session.radioModel().callsign().trimmed().toUpper();
}

void MobileEngine::setCallsign(const QString& callsign)
{
    const QString call = callsign.trimmed().toUpper();
    RadioModel& rm = m_session.radioModel();
    // Empty is "no change", never "erase" (as the desktop's station field).
    if (call.isEmpty() || call == rm.callsign() || !m_connected) {
        return;
    }
    if (rm.usesFlexCommandPlane()) {
        rm.sendCommand(QStringLiteral("radio callsign ") + call);
    }
    rm.setStationCallsign(call);
    qCInfo(lcMobile) << "callsign set to" << call;
    emit callsignChanged();
}

} // namespace AetherSDR::mobile
