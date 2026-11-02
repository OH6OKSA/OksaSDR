#pragma once

#include <QByteArray>
#include <QString>

namespace AetherSDR {

// Amateur RTTY as audio tones (AFSK) for a transmitter: ITA2 Baudot with
// the US figures (the table RttyDecoder reads), 1 start bit, 5 data bits
// and 1.5 stop bits, continuous-phase FSK between the mark and the space
// tone. Which tone is the mark follows the sideband: on lower sideband the
// mark (the higher radio frequency) is the lower tone, on upper sideband
// the upper one; the caller gives the tones as they must sound.
class RttyEncoder {
public:
    struct Params {
        double markHz{2125.0};
        double spaceHz{2295.0};
        double baud{45.45};
        double sampleRate{24000.0};
        double amplitude{0.9};
        // Mark before the first character and after the last, and LTRS
        // ("diddles") after the lead so a receiver is in step and unshifted.
        double leadSeconds{0.25};
        double tailSeconds{0.15};
        int leadLtrs{2};
    };

    // Float mono samples at sampleRate. Lower case goes as upper case, a
    // line break as CR LF; what Baudot cannot carry is left out. sent, if
    // given, is the text as it goes out.
    static QByteArray encode(const QString& text, const Params& params, QString* sent = nullptr);
    // The text as Baudot can carry it (what encode sends).
    static QString sendable(const QString& text);
};

} // namespace AetherSDR
