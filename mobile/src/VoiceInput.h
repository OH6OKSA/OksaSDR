#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace AetherSDR::mobile {

// Speech to text on this device for logging by voice (roadmap 79). iOS /
// iPadOS and the iPad app on a Mac: a short recording of the microphone the
// app already captures (the voice keyer's recorder), handed to the Speech
// framework in one piece, on device where the device offers it. Android 13
// and later: the device's default recogniser listens to the microphone
// itself while the button is held (the app records nothing then). Elsewhere
// none.
bool voiceInputAvailable();
// Why voice input is not offered here (empty when it is).
QString voiceInputUnavailableReason();
// The recogniser's alternatives for floatMono (32-bit float, one channel,
// at rate Hz), best first, or an error; done runs on the GUI thread.
void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList& vocabulary,
                     std::function<void(const QStringList& alternatives, const QString& error)> done);

// Whether the recogniser takes the microphone itself (Android): then
// startListening when the button goes down (ready runs once it listens),
// stopListening when it is let go; done runs once, on the GUI thread, also
// when the recogniser ends by itself after a pause.
bool voiceInputListensItself();
void startListening(const QStringList& vocabulary, std::function<void()> ready,
                    std::function<void(const QStringList& alternatives, const QString& error)> done);
void stopListening();

} // namespace AetherSDR::mobile
