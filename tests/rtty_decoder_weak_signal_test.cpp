// RttyDecoder on weak signals: RttyEncoder's audio with white noise at a
// signal-to-noise ratio in 2500 Hz (as digital-mode SNRs are given), on
// both sidebands, at the exact speed and at a transmitter's clock 1 % fast
// or slow, through the actual worker. The character error rate must stay
// within the limits below, which the bit clock's phase decides: it samples
// mid-bit, so an edge belongs halfway between two samples.
#include "core/DecoderPcmAdapter.h"
#include "core/RttyDecoder.h"
#include "core/RttyEncoder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

using namespace AetherSDR;

namespace {
int failures = 0;

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

const QString kText = QStringLiteral(
    "CQ CQ CQ DE OH6OKSA OH6OKSA OH6OKSA PSE K\n"
    "OH2XYZ DE OH6OKSA GM UR RST 599 599 NAME NIKO NIKO QTH KONNEVESI KP32EQ\n"
    "RIG FLEX 6600 PWR 100W ANT DIPOLE WX 3C CLOUDY BTU OH2XYZ DE OH6OKSA K\n"
    "TNX FER QSO 73 ES GL OH2XYZ DE OH6OKSA SK\n");

// The character error rate (%) of the text sent with noise at snrDb in
// 2500 Hz, the transmitter's speed baudScale times 45.45.
double errorRate(bool usb, double snrDb, double baudScale, unsigned seed, int lane)
{
    RttyEncoder::Params p;
    p.markHz = usb ? 2295.0 : 2125.0;
    p.spaceHz = usb ? 2125.0 : 2295.0;
    p.baud = 45.45 * baudScale;
    p.amplitude = 0.3;
    QString sent;
    const QByteArray audio = RttyEncoder::encode(kText, p, &sent);
    const auto* f = reinterpret_cast<const float*>(audio.constData());
    QVector<float> samples(f, f + audio.size() / int(sizeof(float)));
    for (int i = 0; i < 24000 / 2; ++i) {
        samples.append(0.0f);
    }
    // The signal's power over its keyed part; the noise's in 2500 Hz of the
    // 12 kHz band.
    double power = 0.0;
    for (float s : samples) {
        power += double(s) * s;
    }
    power /= samples.size();
    const double sigma = std::sqrt(power * 12000.0 / (2500.0 * std::pow(10.0, snrDb / 10.0)));
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, sigma);
    for (float& s : samples) {
        s = float(s + gauss(rng));
    }

    RttyDecoder decoder;
    decoder.setMarkFreqHz(usb ? 2295 : 2125);
    decoder.setShiftHz(170);
    decoder.setBaudRate(45.45f);
    decoder.setReversePolarity(!usb);
    QString decoded;
    QObject::connect(&decoder, &RttyDecoder::textDecoded, &decoder,
                     [&](const QString& t, float) { decoded += t; });
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
        // Faster than real time, but within the worker's 4 s buffer.
        if ((offset / 480) % 25 == 0) {
            pumpUntil([] { return false; }, 4);
        }
    }
    pumpUntil([] { return false; }, 400);
    decoder.stop();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    // Line ends as the decoder gives them; the comparison is on the text.
    QString want = sent;
    want.remove(QLatin1Char('\r'));
    decoded.remove(QLatin1Char('\r'));
    return 100.0 * editDistance(want, decoded) / want.size();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const bool table = argc > 1 && QByteArray(argv[1]) == "--table";

    // SNR in 2500 Hz -> the most errors allowed (%), the mean of three
    // noise draws on each sideband at each speed. The noise after the text
    // decodes as a character or two at any SNR. With the clock pulled
    // toward the bit edges (before 2026-11-02) -3 dB gave 1.3-4.0 % and
    // -6 dB 14.8-21.7 %; sampling mid-bit, 0.1-0.6 % and 8.1-12.6 %.
    struct Row {
        double snr;
        double maxAtSpeed;
        double maxOffSpeed;
    };
    const Row rows[] = {
        {6.0, 1.0, 1.0},
        {0.0, 1.5, 1.5},
        {-3.0, 1.2, 1.2},
        {-6.0, 13.0, 14.0},
    };
    int lane = 10;
    for (const Row& row : rows) {
        for (double scale : {1.0, 0.99, 1.01}) {
            for (bool usb : {false, true}) {
                double sum = 0.0;
                for (unsigned seed = 1; seed <= 3; ++seed) {
                    sum += errorRate(usb, row.snr, scale, seed, lane++);
                }
                const double mean = sum / 3.0;
                const double limit = scale == 1.0 ? row.maxAtSpeed : row.maxOffSpeed;
                const bool ok = table || mean <= limit;
                std::printf("[%s] %s SNR %+5.1f dB speed x%.2f: %5.1f %% errors (limit %.1f %%)\n",
                            ok ? " OK " : "FAIL", usb ? "USB" : "LSB", row.snr, scale, mean, limit);
                if (!ok) {
                    ++failures;
                }
            }
        }
    }
    std::printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures == 0 ? 0 : 1;
}
