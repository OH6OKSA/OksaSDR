#pragma once

#include <QString>
#include <QStringList>

namespace AetherSDR::mobile::CallsignSpeech {

// A callsign from what a speech recogniser heard (roadmap 79, logging by
// voice): "Victor Alpha Three Mike Whiskey", "victor alpha 3 mike whisky",
// "V A 3 M W", "VA3MW", "OH6OKSA stroke portable"... NATO and ITU words,
// the older and DX ones (Sugar, King, Kilowatt, Japan...), plain letters, digits as numerals or words (niner, fife, tree, oh),
// slash / stroke / portable / mobile. A report after the call ("five nine",
// "59", "599") comes apart from it. Nothing is guessed: what does not read
// as a call gives an empty call.
struct Result {
    QString call;    // VA3MW, OH6OKSA/P; empty when none was heard
    QString report;  // 59 / 599 when one followed the call
    QString heard;   // what the recogniser gave, for the read-back
    QString cut;     // no call: this one, cut by a word not understood
};

Result parse(const QString& text);
// The first alternative that reads as a call (recognisers give several);
// with none, the first that has a cut call, else the first.
Result parseBest(const QStringList& alternatives);
// The words a recogniser is told to expect (contextual strings).
QStringList vocabulary();
// The words a recogniser held to a grammar may hear (Vosk, roadmap 81), in
// lower case, a phrase for the two-word forms: what parse() reads as part of
// a call or its report, less the plain letters, the ITU's 1927 words and a
// second spelling of a word.
QStringList recognizerWords();

} // namespace AetherSDR::mobile::CallsignSpeech
