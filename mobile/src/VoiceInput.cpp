#include "VoiceInput.h"

#include <QCoreApplication>
#include <QTimer>

namespace AetherSDR::mobile {

// The desktop build has no recogniser. For tests, OKSASDR_VOICE_FAKE stands
// in for one: its text is what it "hears", after 300 ms, whatever was
// recorded (the render tool's voicetext skips the recording altogether);
// with OKSASDR_VOICE_LIVE set too it listens itself, as Android's does.
bool voiceInputAvailable()
{
    return !qEnvironmentVariableIsEmpty("OKSASDR_VOICE_FAKE");
}

QString voiceInputUnavailableReason()
{
    return voiceInputAvailable() ? QString()
                                 : QCoreApplication::translate("VoiceInput", "No speech recognition in this build.");
}

void recognizeSpeech(const QByteArray&, int, const QStringList&,
                     std::function<void(const QStringList&, const QString&)> done)
{
    if (!voiceInputAvailable()) {
        done({}, voiceInputUnavailableReason());
        return;
    }
    const QString heard = qEnvironmentVariable("OKSASDR_VOICE_FAKE");
    QTimer::singleShot(300, qApp, [done, heard] { done({heard}, QString()); });
}

bool voiceInputListensItself()
{
    return !qEnvironmentVariableIsEmpty("OKSASDR_VOICE_LIVE");
}

namespace {
std::function<void(const QStringList&, const QString&)> g_listening;
}

void startListening(const QStringList&, std::function<void()> ready,
                    std::function<void(const QStringList&, const QString&)> done)
{
    if (!voiceInputAvailable()) {
        done({}, voiceInputUnavailableReason());
        return;
    }
    g_listening = std::move(done);
    QTimer::singleShot(300, qApp, [ready] {
        if (ready) {
            ready();
        }
    });
}

void stopListening()
{
    if (!g_listening) {
        return;
    }
    auto done = std::move(g_listening);
    g_listening = nullptr;
    const QString heard = qEnvironmentVariable("OKSASDR_VOICE_FAKE");
    QTimer::singleShot(300, qApp, [done, heard] { done({heard}, QString()); });
}

} // namespace AetherSDR::mobile
