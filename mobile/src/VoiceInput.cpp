#include "VoiceInput.h"

#include "VoskSpeech.h"

#include <QCoreApplication>
#include <QTimer>

namespace AetherSDR::mobile {

// The desktop build recognises with Vosk: the Linux AppImage carries a
// libvosk and the English model (roadmap 91), a development build finds
// them through OKSASDR_VOSK_LIB / _MODEL (roadmap 81, as on Android; the
// render tool's voicewav feeds it a recording). For tests without them,
// OKSASDR_VOICE_FAKE stands in: its text is what it "hears", after 300 ms,
// whatever was recorded (the render tool's voicetext skips the recording
// altogether); with OKSASDR_VOICE_LIVE set too it listens itself, as a
// platform recogniser does.
namespace {
bool fake()
{
    return !qEnvironmentVariableIsEmpty("OKSASDR_VOICE_FAKE");
}
} // namespace

bool voiceInputAvailable()
{
    return fake() || VoskSpeech::unavailableReason().isEmpty();
}

QString voiceInputUnavailableReason()
{
    return voiceInputAvailable() ? QString()
                                 : QCoreApplication::translate("VoiceInput", "No speech recognition in this build.");
}

void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList&,
                     std::function<void(const QStringList&, const QString&)> done)
{
    if (!voiceInputAvailable()) {
        done({}, voiceInputUnavailableReason());
        return;
    }
    if (!fake()) {
        VoskSpeech::recognizeClip(floatMono, rate, std::move(done));
        return;
    }
    const QString heard = qEnvironmentVariable("OKSASDR_VOICE_FAKE");
    QTimer::singleShot(300, qApp, [done, heard] { done({heard}, QString()); });
}

bool voiceInputListensItself()
{
    return fake() && !qEnvironmentVariableIsEmpty("OKSASDR_VOICE_LIVE");
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
