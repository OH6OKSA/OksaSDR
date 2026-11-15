// Logging by voice, step 1 (roadmap 79, Michael Walker): hold the voice
// button, spell the call, let go. The microphone the app already uses is
// recorded as for a voice macro (nothing is sent), the radio's audio held
// silent on this device meanwhile so the speaker does not talk over it (not
// the slice's mute: that is the radio's, every station's); the recording goes
// to the device's own recogniser, and what it heard is read as a call by
// CallsignSpeech. On Android the recogniser listens to the microphone itself
// instead (VoiceInput.h), the app's capture closed meanwhile. The call is filled in for the operator to check and Log:
// nothing is logged by voice alone.

#include "MobileEngine.h"

#include "CallsignSpeech.h"
#include "VoiceInput.h"

#include "core/AudioEngine.h"

#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr int kSpeechMaxMs = 8000;
}

bool MobileEngine::voiceInputOffered() const
{
    return voiceInputAvailable();
}

// This device's receive playback held silent (AudioEngine's duck, as while
// its microphone is keyed); let go only when nothing keys it.
void MobileEngine::duckReceiveForSpeech(bool on)
{
    AudioEngine* audio = m_audio;
    const bool duck = on || m_pttHeld;
    QMetaObject::invokeMethod(audio, [audio, duck] { audio->setRxDucked(duck); }, Qt::QueuedConnection);
}

void MobileEngine::setSpeechState(const QString& state, const QString& status)
{
    m_speechState = state;
    m_speechStatus = status;
    emit voiceInputChanged();
}

void MobileEngine::voiceInputStart()
{
    if (m_speechState != QLatin1String("idle")) {
        return;
    }
    QString why;
    if (!voiceInputAvailable()) {
        why = voiceInputUnavailableReason();
    } else if (!m_connected || !m_audio) {
        why = tr("Connect to the radio first: the voice button listens through the same microphone.");
    } else if (m_pttHeld || radioTransmitting()) {
        why = tr("Not while transmitting.");
    } else if (m_voiceState != QLatin1String("idle")) {
        why = tr("The voice keyer is busy.");
    }
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "voice log: not listening -" << why;
        setSpeechState(QStringLiteral("idle"), why);
        return;
    }
    duckReceiveForSpeech(true);
    m_speechTimer.start(kSpeechMaxMs + 200);
    qCInfo(lcMobile) << "voice log: listening";
    if (voiceInputListensItself()) {
        // The recogniser takes the microphone (Android): a Mic meter's
        // capture closed until it answers; "Listening" once it is ready.
        if (m_micCheckOpen) {
            stopDeviceMicrophoneCapture(true);
        }
        setSpeechState(QStringLiteral("listening"), tr("Starting…"));
        startListening(
            CallsignSpeech::vocabulary(),
            [this] {
                if (m_speechState == QLatin1String("listening")) {
                    setSpeechState(QStringLiteral("listening"), tr("Listening… spell the call, then let go."));
                }
            },
            [this](const QStringList& alternatives, const QString& error) {
                m_speechTimer.stop();
                duckReceiveForSpeech(false);
                if (m_micCheckOpen) {
                    startDeviceMicrophoneCapture();
                }
                voiceAnswered(alternatives, error);
            });
        return;
    }
    startDeviceMicrophoneCapture();
    m_speechRecording = true;
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] { audio->startTxClipRecording(kSpeechMaxMs); }, Qt::QueuedConnection);
    setSpeechState(QStringLiteral("listening"), tr("Listening… spell the call, then let go."));
}

void MobileEngine::voiceInputStop()
{
    if (m_speechState != QLatin1String("listening")) {
        return;
    }
    m_speechTimer.stop();
    if (voiceInputListensItself()) {
        stopListening();
    } else {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, &AudioEngine::stopTxClipRecording, Qt::QueuedConnection);
    }
    setSpeechState(QStringLiteral("recognizing"), tr("Recognising…"));
}

// The recording made for the voice button (onVoiceClipRecorded hands it
// here): to the recogniser.
void MobileEngine::recognizeVoiceClip(const QByteArray& clip, int rate)
{
    duckReceiveForSpeech(false);
    qCInfo(lcMobile) << "voice log: recorded" << clip.size() / sizeof(float) / double(std::max(1, rate)) << "s";
    setSpeechState(QStringLiteral("recognizing"), tr("Recognising…"));
    recognizeSpeech(clip, rate, CallsignSpeech::vocabulary(), [this](const QStringList& alternatives, const QString& error) {
        voiceAnswered(alternatives, error);
    });
}

// The recogniser's answer, from either way of listening.
void MobileEngine::voiceAnswered(const QStringList& alternatives, const QString& error)
{
    if (!error.isEmpty()) {
        qCInfo(lcMobile).noquote() << "voice log: the recogniser said:" << error;
        setSpeechState(QStringLiteral("idle"), error);
        return;
    }
    voiceInputText(alternatives);
}

// What was heard, read as a call (also the render tool's voicetext).
void MobileEngine::voiceInputText(const QStringList& alternatives)
{
    const CallsignSpeech::Result r = CallsignSpeech::parseBest(alternatives);
    if (r.call.isEmpty()) {
        qCInfo(lcMobile).noquote() << "voice log: no call in" << alternatives.size() << "alternative(s)";
        setSpeechState(QStringLiteral("idle"), r.heard.isEmpty() ? tr("Nothing was heard.")
                                                                 : tr("Not a callsign: \"%1\"").arg(r.heard));
        return;
    }
    qCInfo(lcMobile).noquote() << "voice log: heard" << r.call << (r.report.isEmpty() ? QString() : r.report);
    setSpeechState(QStringLiteral("idle"), r.report.isEmpty() ? tr("Heard %1 (\"%2\")").arg(r.call, r.heard)
                                                              : tr("Heard %1, %2 (\"%3\")").arg(r.call, r.report, r.heard));
    emit voiceHeard(r.call, r.report);
}

} // namespace AetherSDR::mobile
