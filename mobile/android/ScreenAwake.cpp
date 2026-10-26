#include "ScreenAwake.h"

#include "AndroidJni.h"
#include "AudioSession.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAndroid, "aether.mobile.android")

namespace AetherSDR::mobile {

void androidStartup()
{
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
// Bluetooth headset's hands-free link (receive and microphone), Speaker the
// built-in speaker even with Bluetooth connected, Bluetooth leaves Android's
// own media route (a Bluetooth device's media link, the device microphone).
// Whatever was set before is ended first.
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
        route = QJniObject::callStaticObjectMethod(kPlatformClass, "useSpeaker",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
        if (route.isEmpty()) {
            route = QStringLiteral("device speaker not available, Android's own route");
        }
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
