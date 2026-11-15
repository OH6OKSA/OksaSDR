#include "VoiceInput.h"

#include "AndroidJni.h"
#include "VoskSpeech.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QLoggingCategory>
#include <QTimer>

#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

// Logging by voice on Android (roadmap 81): the app's own recording read by
// Vosk, held to the alphabet's words. The phone's own recognisers could not
// take the English alphabet on the developer's phone (betas 85-91: plain
// English words over the network, nothing or error 11 on the device).
QString voiceInputUnavailableReason()
{
    const QString why = VoskSpeech::unavailableReason();
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "voice log: no voice entry -" << why;
    }
    return why.isEmpty() ? QString() : QCoreApplication::translate("VoiceInput", "No speech recognition in this build.");
}

bool voiceInputAvailable()
{
    return VoskSpeech::unavailableReason().isEmpty();
}

void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList&,
                     std::function<void(const QStringList&, const QString&)> done)
{
    VoskSpeech::recognizeClip(floatMono, rate, std::move(done));
}

// The phone's own recogniser listening to the microphone itself (betas
// 87-91, Platform.startListening): unused while Vosk reads the recording,
// kept for a spelling language Vosk has no model for (Finnish, roadmap 81).
bool voiceInputListensItself()
{
    return false;
}

void startListening(const QStringList& vocabulary, std::function<void()> ready,
                    std::function<void(const QStringList&, const QString&)> done)
{
    // The words go as hints to the on-device recogniser only (Platform.java).
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
        // A line each part (the log keeps 300 characters of a line).
        for (const QString& part : notes.isEmpty() ? QStringList{QStringLiteral("not started")}
                                                   : notes.split(QStringLiteral("; "))) {
            qCInfo(lcMobile).noquote() << "voice log: Android recogniser:" << part;
        }
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
