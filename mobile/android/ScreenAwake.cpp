#include "ScreenAwake.h"

#include "AndroidJni.h"
#include "AudioSession.h"

#include <QApplicationStateChangeEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegion>
#include <QTimer>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <atomic>

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

// The audio route while a radio is connected: Auto takes a connected
// Bluetooth headset's hands-free link (receive and microphone); Speaker and
// Bluetooth leave Android's own route, the receive and microphone streams
// opening on the devices the choice names (MobileEngine::
// applyAudioOutputDevice): taking the speaker as the communication device
// too switched Android's audio mode, and the route change ended the speaker
// stream just opened (beta 39). Whatever was set before is ended first.
void applyAudioRoute()
{
    const QJniObject context = androidContext();
    QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset", "(Landroid/content/Context;Z)Ljava/lang/String;",
                                       context.object(), jboolean(false));
    if (!s_connected) {
        return;
    }
    QString route;
    if (s_outputMode == QLatin1String("Speaker")) {
        route = QStringLiteral("the device's speaker and microphone (the streams open on them)");
    } else if (s_outputMode == QLatin1String("Bluetooth")) {
        route = QStringLiteral("Android's media route (a Bluetooth device's media link when connected)");
    } else {
        route = QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset",
            "(Landroid/content/Context;Z)Ljava/lang/String;", context.object(), jboolean(true)).toString();
        if (route.isEmpty()) {
            route = QStringLiteral("no Bluetooth headset (Android's own route)");
        }
    }
    qCInfo(lcAndroid).noquote() << "audio output" << s_outputMode << "->" << route;
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

} // namespace AetherSDR::mobile
