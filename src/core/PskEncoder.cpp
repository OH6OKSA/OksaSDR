#include "PskEncoder.h"

#include "PskVaricode.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace AetherSDR {

QString PskEncoder::sendable(const QString& text)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QString out;
    out.reserve(t.size() + 8);
    for (const QChar ch : t) {
        if (ch == QLatin1Char('\n')) {
            out += QStringLiteral("\r\n");
        } else if (ch.unicode() > 0 && !PskVaricode::code(ch).isEmpty()) {
            out += ch;
        }
    }
    return out;
}

QByteArray PskEncoder::encode(const QString& text, const Params& p, QString* sent)
{
    const QString body = sendable(text);
    if (sent) {
        *sent = body;
    }
    // The symbols: reversals, each character's code and "00", then carrier.
    QString bits(p.preambleSymbols, QLatin1Char('0'));
    for (const QChar ch : body) {
        bits += PskVaricode::code(ch);
        bits += QStringLiteral("00");
    }
    bits += QString(p.postambleSymbols, QLatin1Char('1'));

    const double symbolSamples = p.sampleRate / p.baud;
    const double w = 2.0 * std::numbers::pi * p.carrierHz / p.sampleRate;
    std::vector<float> out;
    out.reserve(size_t((bits.size() + 1) * symbolSamples) + 16);
    long n = 0;  // the carrier's phase runs on through every symbol
    double clock = 0.0;
    auto put = [&](double envelope) {
        out.push_back(float(p.amplitude * envelope * std::sin(w * double(n))));
        ++n;
    };
    // Half a symbol rising from silence, half a symbol falling into it.
    const int half = int(std::lround(symbolSamples / 2.0));
    for (int i = 0; i < half; ++i) {
        put(std::sin(std::numbers::pi / 2.0 * double(i) / half));
    }
    double sign = 1.0;
    for (const QChar b : bits) {
        clock += symbolSamples;
        const long end = std::lround(clock) + half;
        const long start = long(out.size());
        const double len = double(end - start);
        for (long i = start; i < end; ++i) {
            const double t = double(i - start) / len;
            put(b == QLatin1Char('0') ? sign * std::cos(std::numbers::pi * t) : sign);
        }
        if (b == QLatin1Char('0')) {
            sign = -sign;
        }
    }
    for (int i = 0; i < half; ++i) {
        put(sign * std::cos(std::numbers::pi / 2.0 * double(i) / half));
    }
    return QByteArray(reinterpret_cast<const char*>(out.data()), int(out.size() * sizeof(float)));
}

} // namespace AetherSDR
