// RttyEncoder's transmit audio through the actual RttyDecoder worker: what
// goes out is what is read, as the decoder is set for lower sideband (the
// RTTY mode, DIGL: the mark the lower tone) and for upper sideband (DIGU:
// the mark the upper tone), including figures after spaces and line breaks.
#include "core/DecoderPcmAdapter.h"
#include "core/RttyDecoder.h"
#include "core/RttyEncoder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>

#include <cstdio>
#include <functional>

using namespace AetherSDR;

namespace {
int failures = 0;
int checks = 0;
void check(bool value, const QString& message)
{
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
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

QString visible(QString s)
{
    return s.replace(QLatin1Char('\r'), QStringLiteral("\\r")).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
}

// Encodes text with the tones as the sideband makes them sound and decodes
// it with the decoder set as the app sets it for that sideband.
QString roundTrip(const QString& text, bool usb, QString* sent)
{
    RttyEncoder::Params p;
    p.markHz = usb ? 2295.0 : 2125.0;
    p.spaceHz = usb ? 2125.0 : 2295.0;
    const QByteArray audio = RttyEncoder::encode(text, p, sent);
    const auto* f = reinterpret_cast<const float*>(audio.constData());
    const int n = int(audio.size() / int(sizeof(float)));

    RttyDecoder decoder;
    decoder.setMarkFreqHz(usb ? 2295 : 2125);
    decoder.setShiftHz(170);
    decoder.setBaudRate(45.45f);
    decoder.setReversePolarity(!usb);  // the space above the mark on LSB
    QString decoded;
    QObject::connect(&decoder, &RttyDecoder::textDecoded, &decoder,
                     [&](const QString& t, float) { decoded += t; });
    DecoderPcmAdapter adapter;
    adapter.selectRoute(DecoderPcmAdapter::RouteLane::NativeSlice, 5);
    PcmProducer producer;
    producer.start(PcmPurpose::Slice, 5, {24000, PcmLayout::Mono});
    decoder.start();
    // Some noise-free idle mark after it, as the radio's audio continues.
    QVector<float> samples(f, f + n);
    for (int i = 0; i < 24000 / 2; ++i) {
        samples.append(0.0f);
    }
    for (int offset = 0; offset < samples.size(); offset += 480) {
        const int frames = std::min(480, int(samples.size()) - offset);
        const std::optional<PcmFrame> frame = producer.produce(samples.mid(offset, frames));
        if (const std::optional<DecoderPcmBlock> block = adapter.accept(*frame)) {
            decoder.feedPcmBlock(*block);
        }
        // Real time is not needed, but the worker's buffer is finite.
        if ((offset / 480) % 50 == 0) {
            pumpUntil([] { return false; }, 1);
        }
    }
    pumpUntil([&] { return decoded.size() >= sent->size(); }, 8000);
    decoder.stop();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    return decoded;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    check(RttyEncoder::sendable(QStringLiteral("cq de oh6oksa @=+\n")) == QStringLiteral("CQ DE OH6OKSA \r\n"),
          QStringLiteral("sendable: upper case, a line break as CR LF, @ = + left out"));

    const QStringList texts = {
        QStringLiteral("CQ CQ CQ DE OH6OKSA OH6OKSA OH6OKSA PSE K\n"),
        QStringLiteral("OH2XX DE OH6OKSA UR RST 599 599 NR 001 001 BTU"),
        QStringLiteral("A1B2 C3 4D 5 E6 F?"),
        QStringLiteral("TNX FER QSO, 73 / SK: (OH6OKSA)"),
    };
    for (bool usb : {false, true}) {
        for (const QString& text : texts) {
            QString sent;
            const QString decoded = roundTrip(text, usb, &sent);
            check(decoded == sent, QStringLiteral("%1: sent \"%2\", read \"%3\"")
                                       .arg(usb ? QStringLiteral("USB") : QStringLiteral("LSB"), visible(sent), visible(decoded)));
        }
    }
    // The wrong sideband reads something else (the polarity matters).
    {
        RttyEncoder::Params p;
        p.markHz = 2295.0;  // sent for USB
        p.spaceHz = 2125.0;
        QString sent;
        const QByteArray audio = RttyEncoder::encode(QStringLiteral("RYRYRY THE QUICK BROWN FOX"), p, &sent);
        Q_UNUSED(audio);
        QString lsbSent;
        // Decoded as LSB (mark 2125): roundTrip with usb=false re-encodes,
        // so check the polarity by encoding USB tones and decoding as LSB.
        RttyDecoder decoder;
        decoder.setMarkFreqHz(2125);
        decoder.setShiftHz(170);
        decoder.setBaudRate(45.45f);
        decoder.setReversePolarity(true);
        QString decoded;
        QObject::connect(&decoder, &RttyDecoder::textDecoded, &decoder, [&](const QString& t, float) { decoded += t; });
        DecoderPcmAdapter adapter;
        adapter.selectRoute(DecoderPcmAdapter::RouteLane::NativeSlice, 6);
        PcmProducer producer;
        producer.start(PcmPurpose::Slice, 6, {24000, PcmLayout::Mono});
        decoder.start();
        const auto* f = reinterpret_cast<const float*>(audio.constData());
        QVector<float> samples(f, f + audio.size() / int(sizeof(float)));
        for (int offset = 0; offset < samples.size(); offset += 480) {
            const std::optional<PcmFrame> frame = producer.produce(samples.mid(offset, std::min(480, int(samples.size()) - offset)));
            if (const std::optional<DecoderPcmBlock> block = adapter.accept(*frame)) {
                decoder.feedPcmBlock(*block);
            }
        }
        pumpUntil([] { return false; }, 1500);
        decoder.stop();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        check(!decoded.contains(QStringLiteral("QUICK BROWN")),
              QStringLiteral("upper-sideband tones read as lower sideband are not the text (read \"%1\")").arg(visible(decoded)));
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
