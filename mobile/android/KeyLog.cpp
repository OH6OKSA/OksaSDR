// The key and push-to-talk broadcast log on Android (roadmap 119, Rob
// Metallo VA3MET's UNIWA F400): OksaActivity notes the keys the app does not
// use and the push-to-talk broadcasts handsets send; this hands its lines to
// the app's own log, so Log > Send to the developer carries them. Java calls
// the native only once it is registered here (an unregistered native would
// throw in dispatchKeyEvent); until then it writes to logcat.

#include "SideKeys.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr const char* kActivityClass = "fi/oh6oksa/oksasdr/OksaActivity";

// OksaActivity's native keyLog(String), on Android's UI thread.
void JNICALL keyLogFromJava(JNIEnv*, jclass, jstring line)
{
    qCInfo(lcMobile).noquote() << QJniObject(line).toString();
}
} // namespace

void startKeyLog()
{
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"keyLog", "(Ljava/lang/String;)V", reinterpret_cast<void*>(keyLogFromJava)}};
    if (!env.registerNativeMethods(kActivityClass, methods, 1)) {
        qCWarning(lcMobile) << "key log: the bridge to Android did not register; keys are not logged";
        return;
    }
    QJniObject::callStaticMethod<void>(kActivityClass, "setKeyLogOn", "(Z)V", jboolean(true));
}

} // namespace AetherSDR::mobile
