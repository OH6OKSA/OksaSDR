#include "VoiceInput.h"

#include "AndroidJni.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QLoggingCategory>
#include <QTimer>

#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

QString voiceInputUnavailableReason()
{
    const QJniObject why = QJniObject::callStaticObjectMethod(kPlatformClass, "speechUnavailable",
                                                              "(Landroid/content/Context;)Ljava/lang/String;",
                                                              androidContext().object());
    return why.isValid() ? why.toString() : QStringLiteral("No speech recognition.");
}

bool voiceInputAvailable()
{
    return voiceInputUnavailableReason().isEmpty();
}

// Android's recogniser listens to the microphone itself (startListening):
// a recording handed to it is not read by every recogniser.
bool voiceInputListensItself()
{
    return true;
}

void recognizeSpeech(const QByteArray&, int, const QStringList&,
                     std::function<void(const QStringList&, const QString&)> done)
{
    done({}, QCoreApplication::translate("VoiceInput", "Android's recogniser listens to the microphone itself."));
}

void startListening(const QStringList& vocabulary, std::function<void()> ready,
                    std::function<void(const QStringList&, const QString&)> done)
{
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray words = env->NewObjectArray(jsize(vocabulary.size()), stringClass, nullptr);
    for (qsizetype i = 0; i < vocabulary.size(); ++i) {
        const QJniObject w = QJniObject::fromString(vocabulary.at(i));
        env->SetObjectArrayElement(words, jsize(i), w.object<jstring>());
    }
    const QJniObject why = QJniObject::callStaticObjectMethod(
        kPlatformClass, "startListening", "(Landroid/content/Context;[Ljava/lang/String;)Ljava/lang/String;",
        androidContext().object(), words);
    env->DeleteLocalRef(words);
    env->DeleteLocalRef(stringClass);
    if (why.isValid() && !why.toString().isEmpty()) {
        done({}, why.toString());
        return;
    }
    // Polled every 100 ms: ready once, then the answer; 30 s at most (8 s
    // of listening and the recogniser's own time).
    auto* timer = new QTimer;
    auto left = std::make_shared<int>(300);
    auto readySent = std::make_shared<bool>(false);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, left, readySent, ready, done] {
        if (!*readySent && QJniObject::callStaticMethod<jboolean>(kPlatformClass, "speechReady", "()Z")) {
            *readySent = true;
            if (ready) {
                ready();
            }
        }
        const QString answer = QJniObject::callStaticObjectMethod(kPlatformClass, "speechPoll", "()Ljava/lang/String;").toString();
        if (answer.isEmpty() && --*left > 0) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        const QString notes = QJniObject::callStaticObjectMethod(kPlatformClass, "speechNotes", "()Ljava/lang/String;").toString();
        qCInfo(lcMobile).noquote() << "voice log: Android recogniser:" << (notes.isEmpty() ? QStringLiteral("not started") : notes);
        if (answer.startsWith(QLatin1String("ok\n"))) {
            done(answer.mid(3).split(QLatin1Char('\n'), Qt::SkipEmptyParts), QString());
        } else if (answer.startsWith(QLatin1String("error\n"))) {
            done({}, answer.mid(6));
        } else {
            stopListening();
            done({}, QCoreApplication::translate("VoiceInput", "The recogniser did not answer."));
        }
    });
    timer->start(100);
}

void stopListening()
{
    QJniObject::callStaticMethod<void>(kPlatformClass, "stopListening", "()V");
}

} // namespace AetherSDR::mobile
