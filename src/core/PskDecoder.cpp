#include "PskDecoder.h"

#include "LogManager.h"
#include "PskVaricode.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AetherSDR {

PskDecoder::PskDecoder(QObject* parent)
    : QObject(parent)
{}

PskDecoder::~PskDecoder()
{
    stop();
}

void PskDecoder::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    m_paramsChanged = true;
    resetInput();
    auto* worker = QThread::create([this]() { decodeLoop(); });
    worker->setObjectName("PskDecoder");
    m_workerThread.reset(worker);
    worker->start();
    qCDebug(lcDsp) << "PskDecoder: started baud=" << m_baud.load() << "carrier=" << m_carrierHz.load();
}

void PskDecoder::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    resetInput();
    if (m_workerThread) {
        m_workerThread->wait();
        m_workerThread.reset();
    }
    qCDebug(lcDsp) << "PskDecoder: stopped";
}

void PskDecoder::setCarrierHz(double hz)
{
    hz = std::clamp(hz, 100.0, 3500.0);
    m_carrierHz = hz;
    m_trackedHz = hz;
    m_paramsChanged = true;
}

void PskDecoder::setBaud(double baud)
{
    if (m_baud.load() == baud) {
        return;
    }
    m_baud = baud;
    m_paramsChanged = true;
}

void PskDecoder::setAfc(bool on)
{
    m_afc = on;
}

void PskDecoder::feedPcmBlock(const DecoderPcmBlock& block)
{
    if (!m_running || !block.current() || block.samples.size() > PcmFrame::kMaxFrames) {
        return;
    }
    for (float sample : block.samples) {
        if (!std::isfinite(sample)) {
            resetInput();
            return;
        }
    }
    appendMono(QByteArray(reinterpret_cast<const char*>(block.samples.constData()),
                          block.samples.size() * sizeof(float)),
               block.source, true, block.discontinuity);
}

void PskDecoder::appendMono(const QByteArray& mono, const PcmEpochLease& source, bool typed, bool discontinuity)
{
    QMutexLocker lock(&m_bufMutex);
    if (!m_running || (typed && !source.current())) {
        return;
    }
    if (discontinuity || typed != m_typedSource || (typed && source.stream() != m_source.stream())
        || (typed && m_ringBuf.size() + mono.size() > kRingCapacity)) {
        ++m_inputGeneration;
        m_ringBuf.clear();
        queueResetStats(m_inputGeneration.load());
    }
    m_source = source;
    m_typedSource = typed;
    m_ringBuf.append(mono);
}

void PskDecoder::resetInput()
{
    QMutexLocker lock(&m_bufMutex);
    ++m_inputGeneration;
    m_ringBuf.clear();
    m_source = {};
    m_typedSource = false;
    queueResetStats(m_inputGeneration.load());
}

void PskDecoder::queueResetStats(quint64 generation)
{
    QMetaObject::invokeMethod(this, [this, generation] {
        if (m_inputGeneration.load() == generation) {
            emit statsUpdated(m_trackedHz.load(), 0.5f, false);
        }
    }, Qt::QueuedConnection);
}

// The filters for the speed: a low-pass at 24 kHz before taking every
// decimation-th sample (16 a symbol), flat to twice the baud rate and down
// by 8 times it, where the decimated band folds; the receive filter (below);
// and a wider one for AFC, which must hear both tones of the reversals (the
// carrier plus and minus half the baud rate) wherever the signal is within
// its range.
void PskDecoder::resetDsp()
{
    const double baud = m_baud.load();
    m_decimation = std::max(1, int(std::lround(kSampleRate / (kSamplesPerSymbol * baud))));
    const double transition = 6.0 * baud;
    int taps = int(std::lround(5.5 * kSampleRate / transition)) | 1;
    const double cutoff = 5.0 * baud / kSampleRate;  // cycles a sample
    m_lowpass.assign(size_t(taps), 0.0f);
    double sum = 0.0;
    for (int k = 0; k < taps; ++k) {
        const double m = k - (taps - 1) / 2.0;
        const double sinc = m == 0.0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * m) / (std::numbers::pi * m);
        const double blackman = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * k / (taps - 1))
                                + 0.08 * std::cos(4.0 * std::numbers::pi * k / (taps - 1));
        m_lowpass[size_t(k)] = float(sinc * blackman);
        sum += sinc * blackman;
    }
    for (float& c : m_lowpass) {
        c = float(c / sum);
    }
    // Each sample written twice, a buffer apart, so a window is contiguous.
    m_mixed.assign(size_t(2 * taps), {0.0f, 0.0f});
    m_mixedPos = 0;

    // The receive filter at 16 samples a symbol: a low-pass to 0.625 times
    // the baud rate, four symbols long. The symbol's own shape (a cosine
    // squared two symbols long, the matched filter) halved each reversal's
    // symbol where it is read, and Varicode is mostly reversals: in
    // tests/psk_roundtrip_test's noise this one read PSK31 at -10 dB SNR with
    // 1.6 % errors where the matched filter made 3.9 % (before the AFC took
    // its own wider filter).
    constexpr int rxTaps = 4 * kSamplesPerSymbol;
    constexpr double rxCutoff = 0.625 / kSamplesPerSymbol;  // cycles a sample
    m_rxFilter.assign(size_t(rxTaps), 0.0f);
    sum = 0.0;
    for (int k = 0; k < rxTaps; ++k) {
        const double m = k - (rxTaps - 1) / 2.0;
        const double sinc = m == 0.0 ? 2.0 * rxCutoff
                                     : std::sin(2.0 * std::numbers::pi * rxCutoff * m) / (std::numbers::pi * m);
        const double blackman = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * k / (rxTaps - 1))
                                + 0.08 * std::cos(4.0 * std::numbers::pi * k / (rxTaps - 1));
        m_rxFilter[size_t(k)] = float(sinc * blackman);
        sum += sinc * blackman;
    }
    for (float& c : m_rxFilter) {
        c = float(c / sum);
    }
    constexpr double afcCutoff = 1.5 / kSamplesPerSymbol;
    m_afcFilter.assign(size_t(rxTaps), 0.0f);
    sum = 0.0;
    for (int k = 0; k < rxTaps; ++k) {
        const double m = k - (rxTaps - 1) / 2.0;
        const double sinc = m == 0.0 ? 2.0 * afcCutoff : std::sin(2.0 * std::numbers::pi * afcCutoff * m) / (std::numbers::pi * m);
        const double blackman = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * k / (rxTaps - 1))
                                + 0.08 * std::cos(4.0 * std::numbers::pi * k / (rxTaps - 1));
        m_afcFilter[size_t(k)] = float(sinc * blackman);
        sum += sinc * blackman;
    }
    for (float& c : m_afcFilter) {
        c = float(c / sum);
    }
    m_baseband.assign(size_t(2 * rxTaps), {0.0f, 0.0f});
    m_basebandPos = 0;

    m_freq = m_carrierHz.load();
    m_trackedHz = m_freq;
    m_decimCount = 0;
    m_bitClock = 0.0;
    std::fill(std::begin(m_syncBuf), std::end(m_syncBuf), 0.0);
    m_prevSymbol = {1.0, 0.0};
    m_shiftReg = 0;
    m_quality = {0.0, 0.0};
    m_afcQuality = {0.0, 0.0};
    m_steady = false;
}

void PskDecoder::decodeLoop()
{
    constexpr int kChunkSamples = 240;  // 10 ms
    constexpr int kChunkBytes = kChunkSamples * static_cast<int>(sizeof(float));
    quint64 workerGeneration = m_inputGeneration.load();
    int statsTick = 0;
    // The squared baseband's rotation from one decimated sample to the next
    // (the modulation squared away): twice the signal's offset from the
    // mixer, and how coherent it is (1 for a clean carrier, near 0 noise).
    // Two averages: over ~2 symbols to find a signal fast, over ~16 to hold
    // it steady in noise once it reads cleanly.
    std::complex<double> rotation{0.0, 0.0};
    double power2 = 0.0;
    std::complex<double> slowRotation{0.0, 0.0};
    double slowPower2 = 0.0;
    std::complex<double> prevSquared{0.0, 0.0};

    while (m_running) {
        if (m_paramsChanged.exchange(false)) {
            resetDsp();
            rotation = slowRotation = {};
            power2 = slowPower2 = 0.0;
        }
        QByteArray chunk;
        quint64 generation = 0;
        PcmEpochLease source;
        bool typed = false;
        {
            QMutexLocker lock(&m_bufMutex);
            if (m_ringBuf.size() < kChunkBytes) {
                lock.unlock();
                QThread::msleep(10);
                continue;
            }
            chunk = m_ringBuf.left(kChunkBytes);
            m_ringBuf.remove(0, kChunkBytes);
            generation = m_inputGeneration.load();
            source = m_source;
            typed = m_typedSource;
        }
        if (workerGeneration != generation) {
            workerGeneration = generation;
            resetDsp();
            rotation = slowRotation = {};
            power2 = slowPower2 = 0.0;
            statsTick = 0;
        }
        const auto current = [this, generation, source, typed] {
            return m_running && m_inputGeneration.load() == generation && (!typed || source.current());
        };
        if (!current()) {
            continue;
        }

        const double decimatedRate = kSampleRate / m_decimation;
        const int taps = int(m_lowpass.size());
        const int rxTaps = int(m_rxFilter.size());
        const auto* samples = reinterpret_cast<const float*>(chunk.constData());
        for (int i = 0; i < kChunkSamples; ++i) {
            // Down from the carrier.
            const double x = samples[i];
            m_phase += 2.0 * std::numbers::pi * m_freq / kSampleRate;
            if (m_phase > 2.0 * std::numbers::pi) {
                m_phase -= 2.0 * std::numbers::pi;
            }
            const std::complex<float> mixed(float(x * std::cos(m_phase)), float(-x * std::sin(m_phase)));
            m_mixed[size_t(m_mixedPos)] = mixed;
            m_mixed[size_t(m_mixedPos + taps)] = mixed;
            m_mixedPos = (m_mixedPos + 1) % taps;
            if (++m_decimCount < m_decimation) {
                continue;
            }
            m_decimCount = 0;

            // Low-pass at the decimated sample: the newest taps samples.
            std::complex<float> acc{0.0f, 0.0f};
            const std::complex<float>* window = &m_mixed[size_t(m_mixedPos)];
            for (int k = 0; k < taps; ++k) {
                acc += window[k] * m_lowpass[size_t(taps - 1 - k)];
            }
            m_baseband[size_t(m_basebandPos)] = acc;
            m_baseband[size_t(m_basebandPos + rxTaps)] = acc;
            m_basebandPos = (m_basebandPos + 1) % rxTaps;
            std::complex<float> mf{0.0f, 0.0f};
            std::complex<float> wide{0.0f, 0.0f};
            const std::complex<float>* bwin = &m_baseband[size_t(m_basebandPos)];
            for (int k = 0; k < rxTaps; ++k) {
                mf += bwin[k] * m_rxFilter[size_t(k)];
                wide += bwin[k] * m_afcFilter[size_t(k)];
            }
            const std::complex<double> z(mf.real(), mf.imag());

            // AFC: the squared signal's rotation (the wider filter's), over
            // ~2 symbols to find a signal and ~16 to hold it.
            const std::complex<double> w(wide.real(), wide.imag());
            const std::complex<double> squared = w * w;
            constexpr double kRotAlpha = 1.0 / (2.0 * kSamplesPerSymbol);
            constexpr double kSlowAlpha = 1.0 / (16.0 * kSamplesPerSymbol);
            const std::complex<double> turn = squared * std::conj(prevSquared);
            rotation += kRotAlpha * (turn - rotation);
            power2 += kRotAlpha * (std::norm(squared) - power2);
            slowRotation += kSlowAlpha * (turn - slowRotation);
            slowPower2 += kSlowAlpha * (std::norm(squared) - slowPower2);
            prevSquared = squared;

            // Symbol clock: the amplitude averaged at each of the 16
            // positions; the early half against the late half pulls the
            // clock so the symbol is read at the peak.
            const double mag = std::abs(z);
            const int idx = std::clamp(int(m_bitClock), 0, kSamplesPerSymbol - 1);
            m_syncBuf[idx] = 0.8 * m_syncBuf[idx] + 0.2 * mag;
            double early = 0.0;
            double total = 0.0;
            for (int k = 0; k < kSamplesPerSymbol / 2; ++k) {
                early += m_syncBuf[k] - m_syncBuf[k + kSamplesPerSymbol / 2];
                total += m_syncBuf[k] + m_syncBuf[k + kSamplesPerSymbol / 2];
            }
            if (total > 0.0) {
                m_bitClock -= std::clamp(early / total / 5.0, -0.5, 0.5);
            }
            m_bitClock += 1.0;
            if (m_bitClock < 0.0) {
                m_bitClock += kSamplesPerSymbol;
            }
            if (m_bitClock < kSamplesPerSymbol) {
                continue;
            }
            m_bitClock -= kSamplesPerSymbol;

            // A symbol: no phase change a one, a reversal a zero.
            const std::complex<double> d = z * std::conj(m_prevSymbol);
            m_prevSymbol = z;
            const int bit = d.real() > 0.0 ? 1 : 0;
            // Quality: how alike the phase changes are once doubled (0 and
            // 180 degrees both become 0), whatever a frequency offset adds
            // to each: 1 for a clean signal, near 0 for noise.
            const double angle = std::arg(d);
            // For the squelch over ~30 symbols (shorter, noise alone now and
            // then rose to a signal's quality and opened it); for AFC over
            // ~20, which follows a weak signal better.
            const std::complex<double> doubled = std::polar(1.0, 2.0 * angle);
            m_quality += 0.03 * (doubled - m_quality);
            m_afcQuality += 0.05 * (doubled - m_afcQuality);
            const double quality = std::abs(m_quality);
            const double afcQuality = std::abs(m_afcQuality);

            // Finding the signal, 15 % of the fast average's offset each
            // symbol (in well before the end of the reversals before the
            // text); reading it cleanly, 1 % of the slow one's. Each gain
            // times its average's length stays well under one, or the loop
            // swings. Steady from a quality of 0.6 until it falls under 0.3
            // (a weak signal's quality wavers around the middle).
            m_steady = afcQuality > 0.6 || (m_steady && afcQuality > 0.3);
            const bool steady = m_steady;
            const std::complex<double>& rot = steady ? slowRotation : rotation;
            const double pow2 = steady ? slowPower2 : power2;
            const double coherence = pow2 > 1e-20 ? std::abs(rot) / pow2 : 0.0;
            if (m_afc.load() && coherence > (steady ? 0.1 : 0.3)) {
                const double offset = std::arg(rot) * decimatedRate / (4.0 * std::numbers::pi);
                const double set = m_carrierHz.load();
                m_freq = std::clamp(m_freq + (steady ? 0.01 : 0.15) * offset, set - kAfcRangeHz, set + kAfcRangeHz);
                m_trackedHz = m_freq;
            }

            m_shiftReg = ((m_shiftReg << 1) | unsigned(bit)) & 0xFFFFF;
            if ((m_shiftReg & 3u) == 0) {
                const unsigned code = m_shiftReg >> 2;
                m_shiftReg = 0;
                // Text and line breaks; CR (before LF) and the other control
                // characters are left out.
                const QChar ch = PskVaricode::character(code);
                if (ch == QLatin1Char('\n') || (ch.unicode() >= 32 && ch.unicode() < 127)) {
                    const float conf = float(0.5 + 0.5 * std::clamp(quality, 0.0, 1.0));
                    const QString text(ch);
                    QMetaObject::invokeMethod(this, [this, current, text, conf] {
                        if (current()) {
                            emit textDecoded(text, conf);
                        }
                    }, Qt::QueuedConnection);
                }
            }
        }

        statsTick += kChunkSamples;
        if (statsTick >= 12000) {
            statsTick = 0;
            const double tracked = m_trackedHz.load();
            const double q = std::abs(m_quality);
            const float quality = float(0.5 + 0.5 * std::clamp(q, 0.0, 1.0));
            const bool locked = q > 0.4;
            QMetaObject::invokeMethod(this, [this, current, tracked, quality, locked] {
                if (current()) {
                    emit statsUpdated(tracked, quality, locked);
                }
            }, Qt::QueuedConnection);
        }
    }
}

} // namespace AetherSDR
