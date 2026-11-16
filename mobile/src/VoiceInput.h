#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace AetherSDR::mobile {

// Speech to text on this device for logging by voice (roadmaps 79 and 81):
// a short recording of the microphone the app already captures (the voice
// keyer's recorder), recognised in one piece on the device by Vosk held to
// the spelling alphabet (VoskSpeech): Android, iOS / iPadOS and the iPad app
// on a Mac. An iOS build without Vosk hands it to Apple's Speech framework;
// the desktop has Vosk from OKSASDR_VOSK_LIB / _MODEL. Android's own
// recogniser listening to the microphone itself (startListening) is kept
// for languages Vosk has no model for.
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
