#include "ShareText.h"

#include "AndroidJni.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>

namespace AetherSDR::mobile {

void copyText(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
}

// Android: the system share sheet (Gmail, Messages, …).
bool shareText(const QString& subject, const QString& text)
{
    return QJniObject::callStaticMethod<jboolean>(kPlatformClass, "share",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        androidContext().object(), QJniObject::fromString(subject).object<jstring>(),
        QJniObject::fromString(text).object<jstring>());
}

// Android: a mail app with the address, subject, body and the file; what
// happened goes to the app's log (the Log page's report).
DeveloperMail sendToDeveloper(const QString& address, const QString& subject, const QString& body,
                              const QString& path)
{
    const QString shown = QJniObject::callStaticObjectMethod(kPlatformClass, "sendToDeveloper",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        androidContext().object(), QJniObject::fromString(address).object<jstring>(),
        QJniObject::fromString(subject).object<jstring>(), QJniObject::fromString(body).object<jstring>(),
        QJniObject::fromString(path).object<jstring>()).toString();
    qInfo().noquote() << "Send to the developer:" << (shown.isEmpty() ? QStringLiteral("none: the Java call failed") : shown);
    if (shown.startsWith(QLatin1String("share"))) {
        copyText(address);  // a share target that is not a mail app: the address to paste
        return DeveloperMail::ShareSheet;
    }
    return shown == QLatin1String("mail") ? DeveloperMail::Mail : DeveloperMail::NotShown;
}

QString deviceModel()
{
    const QString maker = QJniObject::getStaticObjectField<jstring>("android/os/Build", "MANUFACTURER").toString();
    const QString model = QJniObject::getStaticObjectField<jstring>("android/os/Build", "MODEL").toString();
    return model.startsWith(maker, Qt::CaseInsensitive) ? model : maker + QLatin1Char(' ') + model;
}

// Android: the share sheet with the file through the app's FileProvider.
bool shareFile(const QString& subject, const QString& path, const QString& mimeType)
{
    return QJniObject::callStaticMethod<jboolean>(kPlatformClass, "shareFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        androidContext().object(), QJniObject::fromString(subject).object<jstring>(),
        QJniObject::fromString(path).object<jstring>(), QJniObject::fromString(mimeType).object<jstring>());
}

} // namespace AetherSDR::mobile
