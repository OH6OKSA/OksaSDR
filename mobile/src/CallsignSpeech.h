#pragma once

#include <QString>
#include <QStringList>

namespace AetherSDR::mobile::CallsignSpeech {

// A callsign from what a speech recogniser heard (roadmap 79, logging by
// voice): "Victor Alpha Three Mike Whiskey", "victor alpha 3 mike whisky",
// "V A 3 M W", "VA3MW", "OH6OKSA stroke portable"... NATO and ITU words,
// plain letters, digits as numerals or words (niner, fife, tree, oh),
// slash / stroke / portable / mobile. A report after the call ("five nine",
// "59", "599") comes apart from it. Nothing is guessed: what does not read
// as a call gives an empty call.
struct Result {
    QString call;    // VA3MW, OH6OKSA/P; empty when none was heard
    QString report;  // 59 / 599 when one followed the call
    QString heard;   // what the recogniser gave, for the read-back
};

Result parse(const QString& text);
// The first alternative that reads as a call (recognisers give several).
Result parseBest(const QStringList& alternatives);
// The words a recogniser is told to expect (contextual strings).
QStringList vocabulary();

} // namespace AetherSDR::mobile::CallsignSpeech
