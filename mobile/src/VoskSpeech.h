#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace AetherSDR::mobile::VoskSpeech {

// Offline speech recognition by Vosk (roadmap 81: alphacephei's Kaldi-based
// recogniser, Apache 2.0) for logging by voice, as the platforms' own
// recognisers cannot be held to the spelling alphabet (Android's wrote
// plain English, Apple's cut calls at a word it misheard). The recognition
// is held to a grammar of the words CallsignSpeech reads, so a word can only
// come out as one of them, whatever the speaker's accent. libvosk is opened
// at run time in the APK (the release's arm64 build) and, on the desktop,
// from OKSASDR_VOSK_LIB; on iOS it is linked in (AETHER_IOS_VOSK: Alphacephei's
// iOS build). The English model comes in the APK's assets (copied out to the
// app's data once), in the app bundle (iOS) or, on the desktop, from
// OKSASDR_VOSK_MODEL.

// Why it cannot run here (empty when it can); cheap, loads nothing big.
QString unavailableReason();

// The alternatives, best first, for pcm16k (16-bit mono at 16 kHz), held to
// words (the rest leave). Blocking (the model is loaded on the first call):
// run it off the GUI thread. On failure the list is empty and error says why.
QStringList recognize(const QByteArray& pcm16k, const QStringList& words, QString* error);

// The voice button's recording (32-bit float mono at rate Hz) recognised off
// the GUI thread, held to CallsignSpeech's words; done runs on the GUI thread.
void recognizeClip(const QByteArray& floatMono, int rate,
                   std::function<void(const QStringList& alternatives, const QString& error)> done);

// 32-bit float mono at rate Hz to 16-bit at 16 kHz (averaged down, or
// linearly resampled up).
QByteArray toPcm16k(const QByteArray& floatMono, int rate);

} // namespace AetherSDR::mobile::VoskSpeech
