// A file to open on Android through Android's own picker (roadmap 129).
// Qt 6.12's QML FileDialog drops a file from a cloud provider: its
// setSelectedFiles refuses a URL QQmlFile::urlToLocalFileOrQrc gives no path
// for, and on Android that is any content: URL but the phone's own storage's
// (isLocalContentAuthority). So a file in Google Drive, OneDrive, Dropbox or
// Nextcloud could not be chosen: the shared ADIF file stayed "No file
// chosen" and an ADIF import read QUrl("") (the user's phone, beta 111).
// Here OksaActivity starts ACTION_OPEN_DOCUMENT (read, write and a lasting
// grant offered) and hands back the content: URL as it is; documentPicked
// carries it to the QML that asked, which then does what its FileDialog's
// onAccepted did. Elsewhere pickDocument returns false and QML opens its
// FileDialog as before.

#include "MobileEngine.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
// The purposes, by the number Java carries back.
const QStringList kPurposes = {QStringLiteral("adifImport"), QStringLiteral("sharedAdif"), QStringLiteral("appSetup"),
                               QStringLiteral("midiImport")};

#ifdef Q_OS_ANDROID
QPointer<MobileEngine> s_pickerEngine;
bool s_pickerBridge = false;

void documentPickedFromJava(JNIEnv* env, jclass, jint purpose, jstring uri)
{
    const char* chars = uri ? env->GetStringUTFChars(uri, nullptr) : nullptr;
    const QString text = chars ? QString::fromUtf8(chars) : QString();
    if (chars) {
        env->ReleaseStringUTFChars(uri, chars);
    }
    QPointer<MobileEngine> engine = s_pickerEngine;
    if (!engine) {
        return;
    }
    QMetaObject::invokeMethod(engine, [engine, purpose, text] {
        if (engine) {
            engine->documentPickedNative(int(purpose), text);
        }
    }, Qt::QueuedConnection);
}
#endif
} // namespace

void MobileEngine::setupDocumentPicker()
{
#ifdef Q_OS_ANDROID
    s_pickerEngine = this;
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"documentPicked", "(ILjava/lang/String;)V", reinterpret_cast<void*>(documentPickedFromJava)}};
    s_pickerBridge = env.registerNativeMethods("fi/oh6oksa/oksasdr/OksaActivity", methods, 1);
    if (!s_pickerBridge) {
        qCWarning(lcMobile) << "file picker: the bridge to Android did not register; QML's file dialog is used";
    }
#endif
}

bool MobileEngine::pickDocument(const QString& purpose)
{
#ifdef Q_OS_ANDROID
    const int index = kPurposes.indexOf(purpose);
    if (!s_pickerBridge || index < 0) {
        return false;
    }
    const bool started = QJniObject::callStaticMethod<jboolean>("fi/oh6oksa/oksasdr/OksaActivity", "pickDocument", "(I)Z",
                                                                jint(index));
    QJniEnvironment env;
    if (env.checkAndClearExceptions() || !started) {
        qCWarning(lcMobile).noquote() << "file picker:" << purpose << "could not start Android's picker; QML's file dialog is used";
        return false;
    }
    qCInfo(lcMobile).noquote() << "file picker:" << purpose << "opened";
    return true;
#else
    Q_UNUSED(purpose)
    return false;
#endif
}

void MobileEngine::documentPickedNative(int purpose, const QString& uri)
{
    const QString name = kPurposes.value(purpose);
    if (name.isEmpty()) {
        return;
    }
    if (uri.isEmpty()) {
        qCInfo(lcMobile).noquote() << "file picker:" << name << "cancelled";
        return;
    }
    // The provider (Drive, the phone's storage...) in the log, not the
    // document's id.
    qCInfo(lcMobile).noquote() << "file picker:" << name << "chose a file from" << QUrl(uri).host();
    emit documentPicked(name, QUrl(uri));
}

} // namespace AetherSDR::mobile
