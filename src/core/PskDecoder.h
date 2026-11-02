#pragma once

#include "DecoderPcmAdapter.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QThread>

#include <atomic>
#include <complex>
#include <memory>
#include <vector>

namespace AetherSDR {

// PSK31 / PSK63 (G3PLX's BPSK with Varicode) from a slice's receive audio,
// on a worker thread as RttyDecoder: the audio mixed down from the carrier
// frequency, low-pass filtered to 16 samples a symbol, matched to the
// signal's cosine-shaped symbols, the symbol clock kept on the amplitude
// peaks (an early / late gate), a phase reversal read as a zero, and "00"
// ending each Varicode character. AFC follows a drifting signal within
// kAfcRangeHz of the frequency set.
//
// Usage:
//   decoder.setCarrierHz(1000); decoder.setBaud(31.25); decoder.start();
//   connect(&audio, &DecoderAudioModel::pcmReady, &decoder, &PskDecoder::feedPcmBlock);
//   connect(&decoder, &PskDecoder::textDecoded, ...);
class PskDecoder : public QObject {
    Q_OBJECT

public:
    explicit PskDecoder(QObject* parent = nullptr);
    ~PskDecoder() override;

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // The frequency set (the tap) and the one AFC has followed the signal to.
    double carrierHz() const { return m_carrierHz; }
    double trackedHz() const { return m_trackedHz; }
    double baud() const { return m_baud; }
    bool afc() const { return m_afc; }

    static constexpr double kAfcRangeHz = 25.0;

public slots:
    void feedPcmBlock(const AetherSDR::DecoderPcmBlock& block);
    void resetInput();

    void setCarrierHz(double hz);
    void setBaud(double baud);
    void setAfc(bool on);

signals:
    // quality: 0.5 = noise, 1.0 = a clean signal (how close the symbols'
    // phase changes are to 0 and 180 degrees), as RttyDecoder's confidence.
    void textDecoded(const QString& text, float quality);
    // About twice a second: where the signal is (AFC) and how clean.
    void statsUpdated(double trackedHz, float quality, bool locked);

private:
    void appendMono(const QByteArray& mono, const PcmEpochLease& source, bool typed, bool discontinuity);
    void queueResetStats(quint64 generation);
    void decodeLoop();
    void resetDsp();

    static constexpr double kSampleRate = 24000.0;
    static constexpr int kRingCapacity = static_cast<int>(kSampleRate) * sizeof(float) * 4;  // 4 s mono
    static constexpr int kSamplesPerSymbol = 16;  // after decimation

    std::unique_ptr<QThread> m_workerThread;
    QMutex m_bufMutex;
    QByteArray m_ringBuf;
    PcmEpochLease m_source;  // protected together with the ring buffer
    bool m_typedSource = false;
    std::atomic<quint64> m_inputGeneration{0};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paramsChanged{false};
    std::atomic<double> m_carrierHz{1000.0};
    std::atomic<double> m_trackedHz{1000.0};
    std::atomic<double> m_baud{31.25};
    std::atomic<bool> m_afc{true};

    // Worker thread only.
    double m_freq{1000.0};       // the mixer's frequency (AFC moves it)
    double m_phase{0.0};         // the mixer's phase
    int m_decimation{48};
    int m_decimCount{0};
    std::vector<float> m_lowpass;                 // at 24 kHz, for decimation
    std::vector<std::complex<float>> m_mixed;     // its input, a ring
    int m_mixedPos{0};
    std::vector<float> m_rxFilter;                // at 16 samples a symbol
    std::vector<float> m_afcFilter;               // wider, for AFC (as long)
    std::vector<std::complex<float>> m_baseband;  // its input, a ring
    int m_basebandPos{0};
    double m_bitClock{0.0};
    double m_syncBuf[kSamplesPerSymbol]{};
    std::complex<double> m_prevSymbol{1.0, 0.0};
    unsigned m_shiftReg{0};
    std::complex<double> m_quality{0.0, 0.0};  // the phase change doubled, averaged
    std::complex<double> m_afcQuality{0.0, 0.0};  // the same, shorter, for AFC
    bool m_steady{false};        // AFC holding a signal it reads cleanly
};

} // namespace AetherSDR
