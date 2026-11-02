#include "RttyEncoder.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace AetherSDR {

namespace {

// As RttyDecoder's tables (ITA2 letters, US figures); 0 = none.
constexpr char kLtrs[32] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',
    '\r', 'D',  'R', 'J', 'N', 'F', 'C', 'K',
     'T', 'Z',  'L', 'W', 'H', 'Y', 'P', 'Q',
     'O', 'B',  'G', '\0', 'M', 'X', 'V', '\0'
};
constexpr char kFigs[32] = {
    '\0', '3', '\n', '-', ' ', '\0', '8', '7',
    '\r', '$',  '4','\'', ',', '!', ':', '(',
     '5', '"',  ')', '2', '#', '6', '0', '1',
     '9', '?',  '&', '\0', '.', '/', ';', '\0'
};
constexpr int kLtrsCode = 0x1F;
constexpr int kFigsCode = 0x1B;

// The code for c, and whether it is a figure; -1 when Baudot has none.
// Space, CR and LF are in both shifts.
int codeFor(char c, bool* figure)
{
    for (int i = 1; i < 32; ++i) {
        if (kLtrs[i] == c) {
            *figure = false;
            return i;
        }
    }
    for (int i = 1; i < 32; ++i) {
        if (kFigs[i] == c) {
            *figure = true;
            return i;
        }
    }
    return -1;
}

bool inBothShifts(char c)
{
    return c == ' ' || c == '\r' || c == '\n';
}

} // namespace

QString RttyEncoder::sendable(const QString& text)
{
    QString t = text.toUpper();
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QString out;
    out.reserve(t.size() + 8);
    for (const QChar qc : t) {
        const char c = qc.toLatin1();
        bool figure = false;
        if (c == '\n') {
            out += QStringLiteral("\r\n");
        } else if (c != '\0' && codeFor(c, &figure) >= 0) {
            out += QLatin1Char(c);
        }
    }
    return out;
}

QByteArray RttyEncoder::encode(const QString& text, const Params& p, QString* sent)
{
    const QString body = sendable(text);
    if (sent) {
        *sent = body;
    }
    std::vector<float> out;
    const double samplesPerBit = p.sampleRate / p.baud;
    double phase = 0.0;
    double clock = 0.0;  // where the current bit ends, in samples (no drift)
    const auto tone = [&](bool mark, double bits) {
        clock += bits * samplesPerBit;
        const double step = 2.0 * std::numbers::pi * (mark ? p.markHz : p.spaceHz) / p.sampleRate;
        while (double(out.size()) < clock) {
            out.push_back(float(p.amplitude * std::sin(phase)));
            phase = std::fmod(phase + step, 2.0 * std::numbers::pi);
        }
    };
    const auto send = [&](int code) {
        tone(false, 1.0);  // start
        for (int b = 0; b < 5; ++b) {
            tone(((code >> b) & 1) != 0, 1.0);
        }
        tone(true, 1.5);  // stop
    };

    tone(true, p.leadSeconds * p.baud);
    for (int i = 0; i < p.leadLtrs; ++i) {
        send(kLtrsCode);
    }
    // After a space sent in figures a receiver may be in either shift
    // (unshift on space, or not): the next character's shift goes out
    // again, so both print it right.
    bool figs = false;
    bool unsure = false;
    for (const QChar qc : body) {
        const char c = qc.toLatin1();
        bool figure = false;
        const int code = codeFor(c, &figure);
        if (code < 0) {
            continue;
        }
        if (inBothShifts(c)) {
            send(code);
            if (figs) {
                unsure = true;
            }
            continue;
        }
        if (unsure || figure != figs) {
            send(figure ? kFigsCode : kLtrsCode);
            figs = figure;
            unsure = false;
        }
        send(code);
    }
    send(kLtrsCode);
    tone(true, p.tailSeconds * p.baud);

    // 5 ms rises and falls: no key clicks at the ends.
    const int ramp = std::min<int>(int(0.005 * p.sampleRate), int(out.size() / 2));
    for (int i = 0; i < ramp; ++i) {
        const float g = float(0.5 - 0.5 * std::cos(std::numbers::pi * i / ramp));
        out[size_t(i)] *= g;
        out[out.size() - 1 - size_t(i)] *= g;
    }
    return QByteArray(reinterpret_cast<const char*>(out.data()), qsizetype(out.size() * sizeof(float)));
}

} // namespace AetherSDR
