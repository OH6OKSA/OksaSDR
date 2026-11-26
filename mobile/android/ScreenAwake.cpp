#include "ScreenAwake.h"

#include "AndroidJni.h"
#include "AudioSession.h"

#include <QApplicationStateChangeEvent>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMutex>
#include <QRegion>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <atomic>
#include <functional>
#include <memory>

Q_LOGGING_CATEGORY(lcAndroid, "aether.mobile.android")

namespace AetherSDR::mobile {

namespace {

std::atomic<int> s_framesDrawn{0};

// Qt's application state against Android's: a return from the background
// left Qt Suspended with the app on screen (beta 40 and 41: Active, then the
// Suspended of the earlier stop, after the app had stood still in the
// background), which kept the waterfall paused and key PTT refused. While
// Qt says Suspended, every second: the activity's own state (OksaActivity)
// and the frames drawn; on screen by either, Qt's state is set from it. The
// first second's reading goes to the log.
void watchApplicationState()
{
    auto* check = new QTimer(qApp);
    check->setInterval(1000);
    static int ticks = 0;
    QObject::connect(check, &QTimer::timeout, check, [check] {
        const int frames = s_framesDrawn.exchange(0);
        if (qApp->applicationState() != Qt::ApplicationSuspended) {
            check->stop();
            return;
        }
        const jint android = QJniObject::callStaticMethod<jint>("fi/oh6oksa/oksasdr/OksaActivity", "lifecycleState");
        if (++ticks == 1) {
            qCInfo(lcAndroid) << "Qt's state Suspended; the activity" << Qt::ApplicationState(android) << "," << frames
                              << "frames drawn in the last second";
        }
        Qt::ApplicationState state = Qt::ApplicationSuspended;
        if (android == Qt::ApplicationActive || android == Qt::ApplicationInactive) {
            state = Qt::ApplicationState(android);
        } else if (frames >= 10) {
            state = Qt::ApplicationActive;
        }
        if (state == Qt::ApplicationSuspended) {
            return;
        }
        check->stop();
        qCInfo(lcAndroid) << "app on screen while Qt's state was Suspended (the activity" << Qt::ApplicationState(android)
                          << "," << frames << "frames in 1 s): state set to" << state;
        QWindowSystemInterface::handleApplicationStateChanged<QWindowSystemInterface::SynchronousDelivery>(state);
    });
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, check, [check](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended) {
            ticks = 0;
            s_framesDrawn = 0;
            check->start();
        } else {
            check->stop();
        }
    });
}

// The render thread idle before Qt drops the window's surface: on Suspended,
// Qt's Android window (6.12) destroys the surface and then asks for an
// unexposed frame; a frame the render thread was drawing at that moment
// asked for a new surface and waited for it, holding the GUI thread, which
// waited for the render thread, until the app came back. The radio dropped
// the connection after 15 s without pings (beta 49, 22:26: in the middle of
// an FT8 QSO; the "GUI thread stalled (app not in front)" since beta 42).
// The state change event comes before the signal Qt's window acts on, so an
// unexpose here, delivered at once, lets the render thread finish its frame
// (it fails fast while the old surface is still marked created) and stop.
class SuspendGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationStateChange
            && static_cast<QApplicationStateChangeEvent*>(event)->applicationState() <= Qt::ApplicationHidden) {
            for (QWindow* window : QGuiApplication::topLevelWindows()) {
                if (window->isExposed()) {
                    QWindowSystemInterface::handleExposeEvent<QWindowSystemInterface::SynchronousDelivery>(window,
                                                                                                           QRegion());
                }
            }
        }
        return false;
    }
};

} // namespace

void androidFrameDrawn()
{
    s_framesDrawn.fetch_add(1, std::memory_order_relaxed);
}

void androidStartup()
{
    qApp->installEventFilter(new SuspendGuard(qApp));
    watchApplicationState();
    QJniObject::callStaticMethod<void>(kPlatformClass, "setMulticast", "(Landroid/content/Context;Z)V",
                                       androidContext().object(), jboolean(true));
    qCInfo(lcAndroid) << "multicast lock held for LAN discovery";
    const QString exit = QJniObject::callStaticObjectMethod(kPlatformClass, "lastExit",
        "(Landroid/content/Context;)Ljava/lang/String;", androidContext().object()).toString();
    if (!exit.isEmpty()) {
        for (const QString& line : exit.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            qCInfo(lcAndroid).noquote() << "previous run ended:" << line;
        }
    }
}

namespace {

QString s_outputMode = QStringLiteral("Auto");
bool s_connected = false;
// Auto took a Bluetooth headset's hands-free link. Written where the route
// is asked for (the audio calls' thread), read on the GUI thread.
std::atomic<bool> s_headsetInUse{false};

// ---- Calls to Android's audio service, each waited for only a moment
// (roadmap 221). The service answers the app's calls one after another and
// can take 15 s over one while a Bluetooth headset with a music link goes:
// on the user's phone, switching the headset off with its media audio on
// stopped the whole app for 15.5 and 15.8 s (the second preview of beta
// 122), and the radio closed the connection meanwhile. The headset's keeper
// asks twice a second, on the GUI thread. So every such call runs on a
// thread of its own, and the GUI thread waits for it a moment at most: a
// question then answers what it last knew, a request goes on there, and
// while one is out no other question is asked (audioCallsOut): a slow audio
// service costs the sound for a while, not the app.
constexpr int kAskWaitMs = 150;      // a question: the keeper's tick must not stand
constexpr int kRouteWaitMs = 1500;   // a route change: about a second when all is well

std::atomic<int> s_audioCallsOut{0};
std::atomic<qint64> s_audioCallLateSinceMs{0};

QObject* audioCallsContext()
{
    static QObject* context = [] {
        auto* thread = new QThread;   // for the whole run
        thread->setObjectName(QStringLiteral("AndroidAudioCalls"));
        auto* object = new QObject;
        object->moveToThread(thread);
        thread->start();
        return object;
    }();
    return context;
}

// fn on the audio calls' thread, in the order asked. True when it finished
// within waitMs (0: not waited for); false when it is still out, in which
// case nothing it writes may be read here.
bool runAudioCall(std::function<void()> fn, int waitMs)
{
    auto done = std::make_shared<QSemaphore>(0);
    const qint64 askedMs = QDateTime::currentMSecsSinceEpoch();
    s_audioCallsOut.fetch_add(1);
    QMetaObject::invokeMethod(audioCallsContext(), [fn = std::move(fn), done, askedMs] {
        fn();
        s_audioCallsOut.fetch_sub(1);
        if (s_audioCallLateSinceMs.load() != 0 && s_audioCallsOut.load() == 0) {
            s_audioCallLateSinceMs = 0;
            qCWarning(lcAndroid).noquote()
                << QStringLiteral("Android's audio service answers again (a call took %1 ms)")
                       .arg(QDateTime::currentMSecsSinceEpoch() - askedMs);
        }
        done->release();
    }, Qt::QueuedConnection);
    if (waitMs <= 0) {
        return false;
    }
    if (done->tryAcquire(1, waitMs)) {
        return true;
    }
    qint64 none = 0;
    if (s_audioCallLateSinceMs.compare_exchange_strong(none, askedMs)) {
        qCWarning(lcAndroid).noquote()
            << QStringLiteral("Android's audio service has not answered in %1 ms: the app goes on without it until it does")
                   .arg(waitMs);
    }
    return false;
}

QString javaHeadsetState(const QJniObject& context)
{
    return QJniObject::callStaticObjectMethod(kPlatformClass, "headsetState",
        "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
}

QMutex s_headsetStateMutex;
QString s_headsetStateKnown;   // the last answer, for a log line while the service is slow

// The audio route while a radio is connected: Auto takes a connected
// Bluetooth headset's hands-free link (receive and microphone); Speaker and
// Bluetooth leave Android's own route, the receive and microphone streams
// opening on the devices the choice names (MobileEngine::
// applyAudioOutputDevice): taking the speaker as the communication device
// too switched Android's audio mode, and the route change ended the speaker
// stream just opened (beta 39). Whatever was set before is ended first.
// On the audio calls' thread (roadmap 221), with the choice as it stood when
// it was asked.
void applyAudioRoute()
{
    const QJniObject context = androidContext();
    const QString mode = s_outputMode;
    const bool connected = s_connected;
    const auto work = [context, mode, connected] {
        QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset", "(Landroid/content/Context;Z)Ljava/lang/String;",
                                           context.object(), jboolean(false));
        if (!connected) {
            return;
        }
        QString route;
        bool inUse = false;
        if (mode == QLatin1String("Speaker")) {
            route = QStringLiteral("the device's speaker and microphone (the streams open on them)");
        } else if (mode == QLatin1String("Bluetooth")) {
            route = QStringLiteral("Android's media route (a Bluetooth device's media link when connected)");
        } else {
            route = QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset",
                "(Landroid/content/Context;Z)Ljava/lang/String;", context.object(), jboolean(true)).toString();
            inUse = !route.isEmpty();
            if (route.isEmpty()) {
                route = QStringLiteral("no Bluetooth headset (Android's own route)");
            }
        }
        s_headsetInUse = inUse;
        qCInfo(lcAndroid).noquote() << "audio output" << mode << "->" << route;
        if (inUse) {
            const QString state = javaHeadsetState(context);
            {
                QMutexLocker lock(&s_headsetStateMutex);
                s_headsetStateKnown = state;
            }
            qCInfo(lcAndroid).noquote() << "headset:" << state;
            // Once a run: whether the route is asked through an AudioManager of
            // the app's own (roadmap 218; Platform.audioManager has the story).
            static bool said = false;
            if (!said) {
                said = true;
                const bool apart = QJniObject::callStaticMethod<jboolean>(kPlatformClass, "audioManagerApart",
                    "(Landroid/content/Context;)Z", context.object());
                qCInfo(lcAndroid) << "headset route asked through an AudioManager of the app's own:" << (apart ? "yes" : "no");
            }
        }
    };
    // Until the answer is in, the headset is not taken to be the route.
    s_headsetInUse = false;
    runAudioCall(work, kRouteWaitMs);
}

} // namespace

// Android, while a radio is connected: the window's keep-screen-on flag; the
// RadioService foreground service (notification, wake and Wi-Fi locks) so
// receive keeps running while another app is in front; and the audio route
// the operator chose (applyAudioRoute).
void setKeepScreenAwake(bool awake)
{
    static constexpr jint kFlagKeepScreenOn = 0x00000080;  // WindowManager.LayoutParams
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([awake] {
        const QJniObject window = androidContext().callMethod<jobject>("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            window.callMethod<void>(awake ? "addFlags" : "clearFlags", "(I)V", kFlagKeepScreenOn);
        }
    });
    if (awake) {
        const QString service = QJniObject::callStaticObjectMethod("fi/oh6oksa/oksasdr/RadioService", "start",
            "(Landroid/content/Context;)Ljava/lang/String;", androidContext().object()).toString();
        qCInfo(lcAndroid).noquote() << "foreground service:" << service;
        static bool asked = false;
        if (!asked) {
            asked = true;
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
                const QString result = QJniObject::callStaticObjectMethod(kPlatformClass,
                    "askBackgroundPermissions", "(Landroid/content/Context;)Ljava/lang/String;",
                    androidContext().object()).toString();
                qCInfo(lcAndroid).noquote() << "background:" << result;
            });
        }
    } else {
        QJniObject::callStaticMethod<void>("fi/oh6oksa/oksasdr/RadioService", "stop",
                                           "(Landroid/content/Context;)V", androidContext().object());
    }
    s_connected = awake;
    applyAudioRoute();
}

void setAudioOutputMode(const QString& mode)
{
    if (mode == s_outputMode) {
        return;
    }
    s_outputMode = mode;
    if (s_connected) {
        applyAudioRoute();
    }
}

bool androidAudioBusy()
{
    return s_audioCallsOut.load() > 0;
}

bool runAndroidAudioCall(std::function<void()> fn, int waitMs)
{
    return runAudioCall(std::move(fn), waitMs);
}

QString headsetState()
{
    if (!androidAudioBusy()) {
        const QJniObject context = androidContext();
        auto out = std::make_shared<QString>();
        if (runAudioCall([context, out] { *out = javaHeadsetState(context); }, kAskWaitMs)) {
            QMutexLocker lock(&s_headsetStateMutex);
            s_headsetStateKnown = *out;
            return *out;
        }
    }
    QMutexLocker lock(&s_headsetStateMutex);
    return s_headsetStateKnown + QStringLiteral(" [as last known: Android's audio service is slow]");
}

bool headsetInUse()
{
    return s_connected && s_outputMode == QLatin1String("Auto") && s_headsetInUse.load();
}

QString reassertHeadsetRoute()
{
    if (!headsetInUse() || androidAudioBusy()) {
        return {};
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<QString>();
    if (!runAudioCall([context, out] {
            *out = QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset",
                "(Landroid/content/Context;Z)Ljava/lang/String;", context.object(), jboolean(true)).toString();
        }, kRouteWaitMs)) {
        return {};
    }
    return *out;
}

bool headsetRouteOk()
{
    if (!headsetInUse()) {
        return false;
    }
    // The call link's own state first: kept by the app itself (the watch in
    // Platform), no question to Android's audio service.
    if (QJniObject::callStaticMethod<jlong>(kPlatformClass, "scoUpAt") != 0) {
        return true;
    }
    // Down, or an LE Audio headset, which has no such link: Android's word
    // for the communication device.
    static std::atomic<bool> known{false};
    if (androidAudioBusy()) {
        return known.load();
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<bool>(false);
    if (runAudioCall([context, out] {
            *out = QJniObject::callStaticMethod<jboolean>(kPlatformClass, "headsetRouteOk",
                "(Landroid/content/Context;)Z", context.object());
        }, kAskWaitMs)) {
        known = *out;
    }
    return known.load();
}

bool headsetIsAndroidsRoute()
{
    if (androidAudioBusy()) {
        return false;
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<bool>(false);
    return runAudioCall([context, out] {
               *out = QJniObject::callStaticMethod<jboolean>(kPlatformClass, "headsetRoute",
                   "(Landroid/content/Context;)Z", context.object());
           }, kAskWaitMs)
        && *out;
}

bool headsetListed()
{
    static std::atomic<bool> known{true};
    if (androidAudioBusy()) {
        return known.load();
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<bool>(false);
    if (runAudioCall([context, out] {
            *out = QJniObject::callStaticMethod<jboolean>(kPlatformClass, "headsetListed",
                "(Landroid/content/Context;)Z", context.object());
        }, kAskWaitMs)) {
        known = *out;
    }
    return known.load();
}

int androidAudioMode()
{
    static std::atomic<int> known{0};
    if (androidAudioBusy()) {
        return known.load();
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<int>(0);
    if (runAudioCall([context, out] {
            *out = QJniObject::callStaticMethod<jint>(kPlatformClass, "audioMode", "(Landroid/content/Context;)I",
                                                      context.object());
        }, kAskWaitMs)) {
        known = *out;
    }
    return known.load();
}

void holdHeadsetMode()
{
    if (!headsetInUse() || androidAudioBusy()) {
        return;
    }
    const QJniObject context = androidContext();
    runAudioCall([context] {
        QJniObject::callStaticMethod<void>(kPlatformClass, "holdHeadsetMode", "(Landroid/content/Context;)V",
                                           context.object());
    }, kRouteWaitMs);
}

void dropHeadsetRoute()
{
    if (!headsetInUse()) {
        return;
    }
    const QJniObject context = androidContext();
    runAudioCall([context] {
        QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset", "(Landroid/content/Context;Z)Ljava/lang/String;",
                                           context.object(), jboolean(false));
    }, kRouteWaitMs);
}

void reapplyAudioRoute()
{
    applyAudioRoute();
}

int bluetoothOutputId(bool music)
{
    if (androidAudioBusy()) {
        return -1;
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<int>(-1);
    if (!runAudioCall([context, out, music] {
            *out = QJniObject::callStaticMethod<jint>(kPlatformClass, "bluetoothOutputId",
                "(Landroid/content/Context;Z)I", context.object(), jboolean(music));
        }, kAskWaitMs)) {
        return -1;
    }
    return *out;
}

QString androidPlaybackDevices()
{
    if (androidAudioBusy()) {
        return {};
    }
    const QJniObject context = androidContext();
    auto out = std::make_shared<QString>();
    if (!runAudioCall([context, out] {
            *out = QJniObject::callStaticObjectMethod(kPlatformClass, "playbackDevices",
                "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
        }, kAskWaitMs)) {
        return {};
    }
    return *out;
}

// The foreground service keeps the app running through a reconnect.
void setBackgroundTimeHeld(bool)
{
}

} // namespace AetherSDR::mobile
