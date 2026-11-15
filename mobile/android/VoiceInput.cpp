#include "VoiceInput.h"

#include "AndroidJni.h"

#include <QJniEnvironment>
#include <QPointer>
#include <QDebug>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

namespace AetherSDR::mobile {

namespace {

// The recogniser takes 16 kHz 16-bit PCM: the recording (the device's
// capture rate, float) averaged down, or linearly resampled.
QByteArray toPcm16k(const QByteArray& floatMono, int rate)
{
    const auto* in = reinterpret_cast<const float*>(floatMono.constData());
    const qsizetype n = floatMono.size() / qsizetype(sizeof(float));
    const double step = double(rate) / 16000.0;
    const qsizetype out = qsizetype(double(n) / step);
    QByteArray pcm(out * qsizetype(sizeof(qint16)), Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(pcm.data());
    for (qsizetype i = 0; i < out; ++i) {
        // The mean over the input samples this output sample covers.
        const qsizetype a = qsizetype(double(i) * step);
        const qsizetype b = std::min(n, std::max(a + 1, qsizetype(double(i + 1) * step)));
        double sum = 0.0;
        for (qsizetype k = a; k < b; ++k) {
            sum += in[k];
        }
        const double v = sum / double(b - a);
        dst[i] = qint16(std::clamp(std::isfinite(v) ? v * 32767.0 : 0.0, -32768.0, 32767.0));
    }
    return pcm;
}

} // namespace

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

void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList& vocabulary,
                     std::function<void(const QStringList&, const QString&)> done)
{
    if (floatMono.isEmpty() || rate <= 0) {
        done({}, QStringLiteral("Nothing was recorded."));
        return;
    }
    const QByteArray pcm = toPcm16k(floatMono, rate);
    QJniEnvironment env;
    jbyteArray bytes = env->NewByteArray(jsize(pcm.size()));
    env->SetByteArrayRegion(bytes, 0, jsize(pcm.size()), reinterpret_cast<const jbyte*>(pcm.constData()));
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray words = env->NewObjectArray(jsize(vocabulary.size()), stringClass, nullptr);
    for (qsizetype i = 0; i < vocabulary.size(); ++i) {
        const QJniObject w = QJniObject::fromString(vocabulary.at(i));
        env->SetObjectArrayElement(words, jsize(i), w.object<jstring>());
    }
    const QJniObject why = QJniObject::callStaticObjectMethod(
        kPlatformClass, "startSpeech", "(Landroid/content/Context;[BI[Ljava/lang/String;)Ljava/lang/String;",
        androidContext().object(), bytes, jint(16000), words);
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(words);
    env->DeleteLocalRef(stringClass);
    if (why.isValid() && !why.toString().isEmpty()) {
        done({}, why.toString());
        return;
    }
    // Polled every 200 ms, 20 s at most.
    auto* timer = new QTimer;
    auto left = std::make_shared<int>(100);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, left, done] {
        const QString answer = QJniObject::callStaticObjectMethod(kPlatformClass, "speechPoll", "()Ljava/lang/String;").toString();
        if (answer.isEmpty() && --*left > 0) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        if (answer.startsWith(QLatin1String("ok"))) {
            // "ok on device" / "ok network", then the alternatives.
            const qsizetype eol = answer.indexOf(QLatin1Char('\n'));
            qInfo().noquote() << "voice log: Android recogniser:" << answer.left(eol).mid(3);
            done(eol < 0 ? QStringList() : answer.mid(eol + 1).split(QLatin1Char('\n'), Qt::SkipEmptyParts), QString());
        } else if (answer.startsWith(QLatin1String("error\n"))) {
            done({}, answer.mid(6));
        } else {
            done({}, QStringLiteral("The recogniser did not answer."));
        }
    });
    timer->start(200);
}

} // namespace AetherSDR::mobile
