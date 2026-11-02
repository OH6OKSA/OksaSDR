// PSK31 / PSK63: PskEncoder's audio through the actual PskDecoder worker.
// The Varicode table's rules (G3PLX: a code starts and ends with a one and
// holds no "00"); clean text both speeds; a signal off the set frequency,
// which AFC must find; white noise at an SNR in 2500 Hz, the character
// error rate within the limits below; and the wrong speed reading nothing.
#include "core/DecoderPcmAdapter.h"
#include "core/PskDecoder.h"
#include "core/PskEncoder.h"
#include "core/PskVaricode.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>

#include <QPair>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

using namespace AetherSDR;

namespace {
int failures = 0;
int checks = 0;
bool table = false;

void check(bool value, const QString& message)
{
    ++checks;
    if (!value) {
        ++failures;
    }
    if (!value || table) {
        std::printf("[%s] %s\n", value ? " OK " : "FAIL", qPrintable(message));
    }
}

void pumpUntil(const std::function<bool()>& done, int ms)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < ms) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QThread::msleep(2);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}

int editDistance(const QString& a, const QString& b)
{
    QVector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) {
        prev[j] = j;
    }
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

QString visible(QString s)
{
    return s.replace(QLatin1Char('\r'), QStringLiteral("\\r")).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
}

struct Run {
    double signalHz{1000.0};
    double decoderHz{1000.0};
    double baud{31.25};
    double decoderBaud{31.25};
    double snrDb{99.0};
    unsigned seed{1};
    bool afc{true};
    double neighborHz{0.0};  // another station's signal there, as strong
    double tailSeconds{0.0};  // silence (then noise) after the signal
    bool noSignal{false};    // only the noise (as loud as for snrDb)
};

// The app's squelch (MobileEngine::appendRttyGated, on by default): it
// opens on a character of quality 0.70 with up to three before it of 0.60,
// and closes on two below 0.60.
QString squelched(const QVector<QPair<QChar, float>>& chars)
{
    QString out;
    QString pending;
    QString held;
    bool open = false;
    for (const auto& [ch, q] : chars) {
        if (!open) {
            if (q >= 0.70f) {
                open = true;
                out += pending + ch;
                pending.clear();
            } else if (q >= 0.60f) {
                pending = (pending + ch).right(3);
            } else {
                pending.clear();
            }
        } else if (q < 0.60f) {
            if (!held.isEmpty()) {
                open = false;
                pending.clear();
                held.clear();
            } else {
                held = ch;
            }
        } else {
            out += held + ch;
            held.clear();
        }
    }
    return out;
}

// The text decoded through the app's squelch (CR left out, as the decoder
// leaves it), what was sent (likewise) and the decoder's last frequency.
QString decode(const QString& text, const Run& r, QString* sent, double* trackedHz, int lane)
{
    PskEncoder::Params p;
    p.carrierHz = r.signalHz;
    p.baud = r.baud;
    p.amplitude = 0.3;
    const QByteArray audio = PskEncoder::encode(text, p, sent);
    sent->remove(QLatin1Char('\r'));
    const auto* f = reinterpret_cast<const float*>(audio.constData());
    QVector<float> samples(f, f + audio.size() / int(sizeof(float)));
    double power = 0.0;
    for (float s : samples) {
        power += double(s) * s;
    }
    power /= samples.size();
    samples.resize(samples.size() + int(r.tailSeconds * 24000.0), 0.0f);
    if (r.noSignal) {
        samples.fill(0.0f);
    }
    if (r.neighborHz > 0.0) {
        PskEncoder::Params n = p;
        n.carrierHz = r.neighborHz;
        const QByteArray other = PskEncoder::encode(QStringLiteral("cq cq cq de DL1ABC DL1ABC DL1ABC pse k\n"
                                                                   "tnx fer qso es 73 de DL1ABC sk\n"), n);
        const auto* o = reinterpret_cast<const float*>(other.constData());
        for (int i = 0; i < std::min(int(samples.size()), int(other.size() / int(sizeof(float)))); ++i) {
            samples[i] += o[i];
        }
    }
    if (r.snrDb < 90.0) {
        const double sigma = std::sqrt(power * 12000.0 / (2500.0 * std::pow(10.0, r.snrDb / 10.0)));
        std::mt19937 rng(r.seed);
        std::normal_distribution<double> gauss(0.0, sigma);
        for (float& s : samples) {
            s = float(s + gauss(rng));
        }
    }
    PskDecoder decoder;
    decoder.setCarrierHz(r.decoderHz);
    decoder.setBaud(r.decoderBaud);
    decoder.setAfc(r.afc);
    QVector<QPair<QChar, float>> decoded;
    QObject::connect(&decoder, &PskDecoder::textDecoded, &decoder,
                     [&](const QString& t, float q) { decoded.append({t.at(0), q}); });
    DecoderPcmAdapter adapter;
    adapter.selectRoute(DecoderPcmAdapter::RouteLane::NativeSlice, lane);
    PcmProducer producer;
    producer.start(PcmPurpose::Slice, lane, {24000, PcmLayout::Mono});
    decoder.start();
    for (int offset = 0; offset < samples.size(); offset += 480) {
        const int frames = std::min(480, int(samples.size()) - offset);
        const std::optional<PcmFrame> frame = producer.produce(samples.mid(offset, frames));
        if (const std::optional<DecoderPcmBlock> block = adapter.accept(*frame)) {
            decoder.feedPcmBlock(*block);
        }
        if ((offset / 480) % 25 == 0) {
            pumpUntil([] { return false; }, 10);
        }
    }
    pumpUntil([] { return false; }, 400);
    *trackedHz = decoder.trackedHz();
    decoder.stop();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    return squelched(decoded);
}

double errorRate(const QString& want, const QString& got)
{
    return want.isEmpty() ? 0.0 : 100.0 * editDistance(want, got) / want.size();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    table = argc > 1 && QByteArray(argv[1]) == "--table";
    int lane = 20;

    // Varicode: every ASCII character has a code by the rules, and reads back.
    int bad = 0;
    for (int c = 1; c < 128; ++c) {
        const QString code = PskVaricode::code(QChar(c));
        unsigned bits = 0;
        for (const QChar b : code) {
            bits = (bits << 1) | unsigned(b == QLatin1Char('1'));
        }
        if (code.isEmpty() || !code.startsWith(QLatin1Char('1')) || !code.endsWith(QLatin1Char('1'))
            || code.contains(QStringLiteral("00")) || code.size() > 10 || PskVaricode::character(bits) != QChar(c)) {
            ++bad;
        }
    }
    check(bad == 0, QStringLiteral("Varicode: %1 of 127 characters break the rules or do not read back").arg(bad));
    check(PskVaricode::code(QLatin1Char('e')) == QStringLiteral("11") && PskVaricode::code(QLatin1Char(' ')) == QStringLiteral("1")
              && PskVaricode::code(QLatin1Char('t')) == QStringLiteral("101"),
          QStringLiteral("Varicode: e 11, space 1, t 101 (G3PLX's table)"));
    check(PskEncoder::sendable(QStringLiteral("cq de OH6OKSA\nÄ")) == QStringLiteral("cq de OH6OKSA\r\n"),
          QStringLiteral("sendable: case kept, a line break as CR LF, beyond ASCII left out"));

    const QString text = QStringLiteral("CQ CQ CQ de OH6OKSA OH6OKSA OH6OKSA pse k\n"
                                        "OH2XYZ de OH6OKSA tnx fer call, ur rst 599 599 name Niko qth Konnevesi KP32EQ\n"
                                        "btu OH2XYZ de OH6OKSA k\n");
    // Clean, both speeds, on the frequency and off it within AFC's reach.
    for (double baud : {31.25, 62.5}) {
        for (double off : {0.0, 6.0, -6.0, 12.0, -12.0}) {
            Run r;
            r.baud = r.decoderBaud = baud;
            r.signalHz = 1000.0 + off;
            QString sent;
            double tracked = 0.0;
            const QString got = decode(text, r, &sent, &tracked, lane++);
            // The first characters may go while AFC pulls in.
            const double cer = errorRate(sent, got);
            check(cer <= (off == 0.0 ? 0.0 : 2.0) && std::abs(tracked - r.signalHz) < 1.0,
                  QStringLiteral("PSK%1 signal %2 Hz off: %3 % errors, AFC at %4 Hz (read \"%5\")")
                      .arg(baud == 31.25 ? 31 : 63).arg(off).arg(cer, 0, 'f', 1).arg(tracked, 0, 'f', 1)
                      .arg(visible(got.left(60))));
        }
    }
    // AFC off: a signal 3 Hz off still reads (the symbols turn 35 degrees).
    {
        Run r;
        r.signalHz = 1003.0;
        r.afc = false;
        QString sent;
        double tracked = 0.0;
        const QString got = decode(text, r, &sent, &tracked, lane++);
        check(errorRate(sent, got) <= 1.0 && tracked == 1000.0,
              QStringLiteral("PSK31 AFC off, 3 Hz off: %1 % errors, stays at %2 Hz").arg(errorRate(sent, got), 0, 'f', 1).arg(tracked));
    }
    // Noise: SNR in 2500 Hz -> the most errors allowed (%), three draws.
    struct Row {
        double baud;
        double snr;
        double maxCer;
    };
    const Row rows[] = {
        {31.25, -6.0, 0.5},
        {31.25, -10.0, 3.0},
        {31.25, -12.0, 22.0},
        {62.5, -6.0, 1.5},
        {62.5, -8.0, 5.0},
    };
    for (const Row& row : rows) {
        double sum = 0.0;
        for (unsigned seed = 1; seed <= 3; ++seed) {
            Run r;
            r.baud = r.decoderBaud = row.baud;
            r.snrDb = row.snr;
            r.seed = seed;
            r.signalHz = 1004.0;  // a tap a few hertz off
            QString sent;
            double tracked = 0.0;
            const QString got = decode(text, r, &sent, &tracked, lane++);
            sum += errorRate(sent, got);
        }
        const double mean = sum / 3.0;
        check(mean <= row.maxCer, QStringLiteral("PSK%1 SNR %2 dB: %3 % errors (limit %4 %)")
                                      .arg(row.baud == 31.25 ? 31 : 63).arg(row.snr).arg(mean, 0, 'f', 1).arg(row.maxCer));
    }
    // AFC pulls in a signal 20 Hz off the tap (its range is 25 Hz).
    {
        Run r;
        r.signalHz = 1020.0;
        QString sent;
        double tracked = 0.0;
        const QString got = decode(text, r, &sent, &tracked, lane++);
        check(errorRate(sent, got) <= 2.0 && std::abs(tracked - 1020.0) < 1.0,
              QStringLiteral("PSK31 20 Hz off the tap: %1 % errors, AFC at %2 Hz").arg(errorRate(sent, got), 0, 'f', 1).arg(tracked, 0, 'f', 1));
    }
    // A station 100 Hz away, as strong, at -6 dB: only ours is read, and AFC
    // stays on it.
    for (double neighbor : {1100.0, 900.0}) {
        Run r;
        r.neighborHz = neighbor;
        r.snrDb = -6.0;
        QString sent;
        double tracked = 0.0;
        const QString got = decode(text, r, &sent, &tracked, lane++);
        check(errorRate(sent, got) <= 1.0 && !got.contains(QStringLiteral("DL1ABC")) && std::abs(tracked - 1000.0) < 2.0,
              QStringLiteral("PSK31 with a station at %1 Hz: %2 % errors, AFC at %3 Hz").arg(neighbor).arg(errorRate(sent, got), 0, 'f', 1).arg(tracked, 0, 'f', 1));
    }
    // AFC holds where the signal was once it ends (two seconds of noise
    // after the text): it wandered off in noise alone.
    {
        Run r;
        r.signalHz = 1008.0;
        r.snrDb = -6.0;
        r.tailSeconds = 4.0;
        QString sent;
        double tracked = 0.0;
        decode(text, r, &sent, &tracked, lane++);
        check(std::abs(tracked - 1008.0) < 1.5, QStringLiteral("AFC after the signal: %1 Hz (the signal was at 1008)").arg(tracked, 0, 'f', 1));
    }
    // Noise alone (as loud as a signal at -6 dB would have): the squelch
    // lets (next to) nothing through.
    for (unsigned seed = 1; seed <= 3; ++seed) {
        Run r;
        r.noSignal = true;
        r.snrDb = -6.0;
        r.seed = seed;
        QString sent;
        double tracked = 0.0;
        const QString got = decode(text, r, &sent, &tracked, lane++);
        check(got.size() <= 3, QStringLiteral("noise alone: %1 characters through the squelch (\"%2\")").arg(got.size()).arg(visible(got)));
    }
    // The wrong speed reads (next to) nothing of the text.
    {
        Run r;
        r.baud = 62.5;
        r.decoderBaud = 31.25;
        QString sent;
        double tracked = 0.0;
        const QString got = decode(text, r, &sent, &tracked, lane++);
        check(!got.contains(QStringLiteral("OH6OKSA")), QStringLiteral("PSK63 read as PSK31 is not the text (read \"%1\")").arg(visible(got.left(60))));
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
