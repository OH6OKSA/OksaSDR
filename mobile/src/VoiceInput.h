#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace AetherSDR::mobile {

// Speech to text on this device for logging by voice (roadmap 79): a short
// recording of the microphone the app already captures (the voice keyer's
// recorder), handed to the platform's recogniser in one piece. iOS / iPadOS
// and the iPad app on a Mac: the Speech framework, on device where the
// device offers it; Android 13 and later: SpeechRecognizer reading the
// recording (on device where offered); elsewhere none.
bool voiceInputAvailable();
// Why voice input is not offered here (empty when it is).
QString voiceInputUnavailableReason();
// The recogniser's alternatives for floatMono (32-bit float, one channel,
// at rate Hz), best first, or an error; done runs on the GUI thread.
void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList& vocabulary,
                     std::function<void(const QStringList& alternatives, const QString& error)> done);

} // namespace AetherSDR::mobile
