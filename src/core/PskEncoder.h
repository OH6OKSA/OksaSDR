#pragma once

#include <QByteArray>
#include <QString>

namespace AetherSDR {

// PSK31 / PSK63 (G3PLX's BPSK) as audio for a transmitter: Varicode
// characters each followed by "00", a zero sent as a phase reversal and a
// one as no change, the carrier's amplitude following a cosine through
// zero at each reversal (so the signal is no wider than it must be). A run
// of reversals before the text lets a receiver find the symbol clock, and
// a steady carrier after it ends the transmission.
class PskEncoder {
public:
    struct Params {
        double carrierHz{1000.0};
        double baud{31.25};  // 62.5 for PSK63
        double sampleRate{24000.0};
        double amplitude{0.9};
        int preambleSymbols{32};
        int postambleSymbols{32};
    };

    // Float mono samples at sampleRate. A line break goes as CR LF; what
    // Varicode cannot carry (beyond ASCII) is left out. sent, if given, is
    // the text as it goes out.
    static QByteArray encode(const QString& text, const Params& params, QString* sent = nullptr);
    // The text as Varicode can carry it (what encode sends).
    static QString sendable(const QString& text);
};

} // namespace AetherSDR
