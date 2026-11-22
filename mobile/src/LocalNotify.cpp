#include "LocalNotify.h"

// iOS and the Mac: ios/LocalNotify.mm.
#if !defined(Q_OS_IOS)

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace AetherSDR::mobile {

void requestLocalNotifications()
{
}

void postLocalNotification(const QString& title, const QString& body)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("fi/oh6oksa/oksasdr/Platform", "postNotification",
                                       "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
                                       QNativeInterface::QAndroidApplication::context().object(),
                                       QJniObject::fromString(title).object<jstring>(),
                                       QJniObject::fromString(body).object<jstring>());
    QJniEnvironment env;
    env.checkAndClearExceptions();
#else
    Q_UNUSED(title)
    Q_UNUSED(body)
#endif
}

} // namespace AetherSDR::mobile

#endif
