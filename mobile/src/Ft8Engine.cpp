#include "Ft8Engine.h"

#include "core/DecoderPcmAdapter.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

extern "C" {
#include <common/monitor.h>
#include <ft8/decode.h>
#include <ft8/encode.h>
#include <ft8/message.h>
}

namespace {

constexpr int kRate = 12000;           // decoder sample rate
constexpr int kMinScore = 10;          // candidate sync score threshold
constexpr int kMaxCandidates = 140;
constexpr int kLdpcIterations = 25;
constexpr int kMaxDecoded = 50;

// The callsign hash table ft8_lib's message packing uses for <hashed> calls.
// Its interface has no context pointer, so it is one table, shared and locked.
constexpr int kHashSize = 256;
struct HashEntry {
    char callsign[12];
    uint32_t hash;  // 8 MSBs age, 22 LSBs hash
};
HashEntry g_hash[kHashSize];
std::recursive_mutex g_hashMutex;

void hashAdd(const char* callsign, uint32_t hash)
{
    const uint16_t hash10 = (hash >> 12) & 0x3FFu;
    int i = (hash10 * 23) % kHashSize;
    while (g_hash[i].callsign[0] != '\0') {
        if ((g_hash[i].hash & 0x3FFFFFu) == hash && std::strcmp(g_hash[i].callsign, callsign) == 0) {
            g_hash[i].hash &= 0x3FFFFFu;
            return;
        }
        i = (i + 1) % kHashSize;
    }
    std::strncpy(g_hash[i].callsign, callsign, 11);
    g_hash[i].callsign[11] = '\0';
    g_hash[i].hash = hash;
}

bool hashLookup(ftx_callsign_hash_type_t type, uint32_t hash, char* callsign)
{
    const uint8_t shift = type == FTX_CALLSIGN_HASH_10_BITS ? 12 : (type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    const uint16_t hash10 = (hash >> (12 - shift)) & 0x3FFu;
    int i = (hash10 * 23) % kHashSize;
    while (g_hash[i].callsign[0] != '\0') {
        if (((g_hash[i].hash & 0x3FFFFFu) >> shift) == hash) {
            std::strcpy(callsign, g_hash[i].callsign);
            return true;
        }
        i = (i + 1) % kHashSize;
    }
    callsign[0] = '\0';
    return false;
}

void hashAge()
{
    for (auto& e : g_hash) {
        if (e.callsign[0] == '\0') {
            continue;
        }
        const uint8_t age = uint8_t(e.hash >> 24);
        if (age > 20) {
            e.callsign[0] = '\0';
            e.hash = 0;
        } else {
            e.hash = ((uint32_t(age) + 1u) << 24) | (e.hash & 0x3FFFFFu);
        }
    }
}

ftx_callsign_hash_interface_t g_hashIf = {hashLookup, hashAdd};

// GFSK synthesis, as ft8_lib's gen_ft8 demo (MIT).
void gfskPulse(int nSpsym, float bt, float* pulse)
{
    const float c = float(M_PI * std::sqrt(2.0 / std::log(2.0)));
    for (int i = 0; i < 3 * nSpsym; ++i) {
        const float t = i / float(nSpsym) - 1.5f;
        const float a1 = c * bt * (t + 0.5f);
        const float a2 = c * bt * (t - 0.5f);
        pulse[i] = (std::erf(a1) - std::erf(a2)) / 2;
    }
}

void synthGfsk(const uint8_t* symbols, int nSym, float f0, float bt, float symbolPeriod, int rate, float* out)
{
    const int nSpsym = int(0.5f + rate * symbolPeriod);
    const int nWave = nSym * nSpsym;
    const float hmod = 1.0f;
    const float dphi_peak = float(2 * M_PI * hmod / nSpsym);
    std::vector<float> dphi(size_t(nWave + 2 * nSpsym), float(2 * M_PI * f0 / rate));
    std::vector<float> pulse(size_t(3 * nSpsym));
    gfskPulse(nSpsym, bt, pulse.data());
    for (int i = 0; i < nSym; ++i) {
        const int ib = i * nSpsym;
        for (int j = 0; j < 3 * nSpsym; ++j) {
            dphi[size_t(j + ib)] += dphi_peak * symbols[i] * pulse[size_t(j)];
        }
    }
    // Dummy symbols at the ends, as WSJT-X.
    for (int j = 0; j < 2 * nSpsym; ++j) {
        dphi[size_t(j)] += dphi_peak * pulse[size_t(j + nSpsym)] * symbols[0];
        dphi[size_t(j + nSym * nSpsym)] += dphi_peak * pulse[size_t(j)] * symbols[nSym - 1];
    }
    float phi = 0;
    for (int k = 0; k < nWave; ++k) {
        out[k] = std::sin(phi);
        phi = std::fmod(phi + dphi[size_t(k + nSpsym)], float(2 * M_PI));
    }
    // Raised-cosine ramps over 1/8 symbol at both ends.
    const int nRamp = int(std::lround(nSpsym / 8.0f));
    for (int i = 0; i < nRamp; ++i) {
        const float env = (1 - std::cos(float(2 * M_PI * i / (2 * nRamp)))) / 2;
        out[i] *= env;
        out[nWave - 1 - i] *= env;
    }
}

// 31-tap low-pass for 24 -> 12 kHz (pass to ~4.5 kHz).
const std::vector<float>& decimTaps()
{
    static const std::vector<float> taps = [] {
        constexpr int n = 31;
        constexpr double fc = 5000.0 / 24000.0;
        std::vector<float> t(n);
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            const double m = i - (n - 1) / 2.0;
            const double sinc = m == 0 ? 2 * fc : std::sin(2 * M_PI * fc * m) / (M_PI * m);
            const double w = 0.54 - 0.46 * std::cos(2 * M_PI * i / (n - 1));
            t[size_t(i)] = float(sinc * w);
            sum += t[size_t(i)];
        }
        for (float& v : t) {
            v = float(v / sum);
        }
        return t;
    }();
    return taps;
}

}  // namespace

Ft8Engine::Ft8Engine(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<Ft8Engine::Decode>();
    m_pool.setMaxThreadCount(1);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &Ft8Engine::tick);
    m_timer->start();
}

Ft8Engine::~Ft8Engine()
{
    m_pool.waitForDone();
}

double Ft8Engine::slotSeconds(Protocol protocol)
{
    return protocol == Protocol::Ft4 ? FT4_SLOT_TIME : FT8_SLOT_TIME;
}

double Ft8Engine::slotSeconds() const
{
    return slotSeconds(m_protocol);
}

void Ft8Engine::setProtocol(Protocol protocol)
{
    if (m_protocol != protocol) {
        m_protocol = protocol;
        m_nextDecodeSlotMs = 0;
    }
}

void Ft8Engine::setRunning(bool on)
{
    if (m_running == on) {
        return;
    }
    m_running = on;
    m_nextDecodeSlotMs = 0;
    if (!on) {
        resetAudio();
    }
    emit runningChanged();
}

qint64 Ft8Engine::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

void Ft8Engine::resetAudio()
{
    m_buf.clear();
    m_decimHistory.clear();
    m_decimOdd = false;
    m_lateSinceMs = 0;
}

void Ft8Engine::feed(const AetherSDR::DecoderPcmBlock& block)
{
    if (!m_running || block.samples.isEmpty()) {
        return;
    }
    if (block.discontinuity) {
        resetAudio();
    }
    // Decimate by two through the low-pass.
    const std::vector<float>& taps = decimTaps();
    const size_t n = taps.size();
    if (m_decimHistory.size() != n) {
        m_decimHistory.assign(n, 0.0f);
    }
    std::vector<float> out;
    out.reserve(size_t(block.samples.size() / 2 + 1));
    for (float x : block.samples) {
        m_decimHistory.erase(m_decimHistory.begin());
        m_decimHistory.push_back(std::isfinite(x) ? x : 0.0f);
        m_decimOdd = !m_decimOdd;
        if (!m_decimOdd) {
            continue;
        }
        float acc = 0;
        for (size_t i = 0; i < n; ++i) {
            acc += taps[i] * m_decimHistory[i];
        }
        out.push_back(acc);
    }
    // On-air time of this block's first sample.
    const double blockStart = nowMs() / 1000.0 - m_rxLatencyMs / 1000.0 - double(out.size()) / kRate;
    const double bufEnd = m_bufT0 + double(m_buf.size()) / kRate;
    const double diff = blockStart - bufEnd;
    const qint64 now = nowMs();
    if (m_buf.empty() || diff < -2.0 || diff > 5.0) {
        m_buf.clear();
        m_bufT0 = blockStart;
        m_lateSinceMs = 0;
    } else if (diff > 1.0) {
        // More than a second late: a stall whose audio follows in a burst
        // (SmartLink, a busy device) or audio lost. Only when it stays late
        // for 2 s is it a gap, and silence in its place keeps the timing;
        // silence for a burst would push everything after it out of the
        // slot window (the buffer grew faster than the clock and the slots
        // after the first decoded nothing).
        if (m_lateSinceMs == 0) {
            m_lateSinceMs = now;
        } else if (now - m_lateSinceMs > 2000) {
            m_buf.insert(m_buf.end(), size_t(diff * kRate), 0.0f);
            m_lateSinceMs = 0;
        }
    } else if (diff < 0.0) {
        m_lateSinceMs = 0;
        // Audio cannot arrive early: an earlier arrival than the buffer
        // assumed means the buffer's clock was late (it had been set by a
        // delayed burst). Late arrivals (bursts after a stall, another
        // slice added) leave it, so they cannot drag the slot window.
        m_bufT0 += diff;
    } else {
        // Creep forward slowly (5 ms a second at most), for the clocks'
        // drift and a buffer once set by an unusually early block.
        m_bufT0 += std::min(diff, 0.005 * double(out.size()) / kRate);
        m_lateSinceMs = 0;
    }
    m_buf.insert(m_buf.end(), out.begin(), out.end());
    // Keep about two and a half FT8 slots.
    constexpr size_t kKeep = size_t(40 * kRate);
    if (m_buf.size() > kKeep) {
        const size_t drop = m_buf.size() - size_t(32 * kRate);
        m_buf.erase(m_buf.begin(), m_buf.begin() + qsizetype(drop));
        m_bufT0 += double(drop) / kRate;
    }
}

void Ft8Engine::tick()
{
    const qint64 now = nowMs();
    const qint64 slotMs = qint64(slotSeconds() * 1000);
    const qint64 currentSlot = now / slotMs * slotMs;
    if (currentSlot != m_lastSlotMs) {
        m_lastSlotMs = currentSlot;
        emit slotStarted(currentSlot);
    }
    if (!m_running) {
        return;
    }
    if (m_nextDecodeSlotMs == 0 || m_nextDecodeSlotMs < currentSlot - slotMs) {
        m_nextDecodeSlotMs = currentSlot;
    }
    // A slot decodes once its window (all of it but the last second) has
    // been heard: FT8 0 - 14 s, FT4 0 - 6.5 s.
    const qint64 windowMs = slotMs - 1000;
    if (now < m_nextDecodeSlotMs + windowMs + m_rxLatencyMs + 50) {
        return;
    }
    const qint64 slot = m_nextDecodeSlotMs;
    m_nextDecodeSlotMs += slotMs;
    startDecode(slot);
}

void Ft8Engine::startDecode(qint64 slotMs)
{
    if (m_decoding.load()) {
        return;
    }
    const double windowS = slotSeconds() - 1.0;
    const int count = int(windowS * kRate);
    const qint64 first = qint64(std::llround((slotMs / 1000.0 - m_bufT0) * kRate));
    std::vector<float> samples(size_t(count), 0.0f);
    int covered = 0;
    for (int i = 0; i < count; ++i) {
        const qint64 at = first + i;
        if (at >= 0 && at < qint64(m_buf.size())) {
            samples[size_t(i)] = m_buf[size_t(at)];
            ++covered;
        }
    }
    float peak = 0.0f;
    for (float v : samples) {
        peak = std::max(peak, std::abs(v));
    }
    qDebug().noquote() << QStringLiteral("ft8: slot %1 covered %2% peak %3 dBFS buffer %4 s")
                             .arg(slotMs % 100000)
                             .arg(100 * covered / std::max(1, count))
                             .arg(peak > 0 ? 20 * std::log10(peak) : -999.0, 0, 'f', 1)
                             .arg(double(m_buf.size()) / kRate, 0, 'f', 1);
    if (qEnvironmentVariableIsSet("FT8_DUMP_DIR")) {
        QFile f(QStringLiteral("%1/slot-%2.f32").arg(qEnvironmentVariable("FT8_DUMP_DIR")).arg(slotMs));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reinterpret_cast<const char*>(samples.data()), qint64(samples.size() * sizeof(float)));
        }
    }
    if (covered < count / 2) {
        return;
    }
    m_decoding = true;
    m_pool.start([this, samples = std::move(samples), slotMs] {
        const QVector<Decode> decodes = decodeSamples(samples, slotMs);
        QMetaObject::invokeMethod(this, [this, decodes, slotMs] {
            m_decoding = false;
            emit decoded(decodes, slotMs);
        }, Qt::QueuedConnection);
    });
}

QVector<Ft8Engine::Decode> Ft8Engine::decodeSamples(const std::vector<float>& samples12k, qint64 slotMs) const
{
    const ftx_protocol_t protocol = m_protocol == Protocol::Ft4 ? FTX_PROTOCOL_FT4 : FTX_PROTOCOL_FT8;
    monitor_config_t cfg{};
    cfg.f_min = 200;
    cfg.f_max = 3000;
    cfg.sample_rate = kRate;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;
    cfg.protocol = protocol;
    monitor_t mon;
    monitor_init(&mon, &cfg);
    for (size_t pos = 0; pos + size_t(mon.block_size) <= samples12k.size(); pos += size_t(mon.block_size)) {
        monitor_process(&mon, samples12k.data() + pos);
    }
    const ftx_waterfall_t* wf = &mon.wf;

    // Noise floor: the 30th percentile of the waterfall's power, per bin.
    double noiseLin = 1e-12;
    {
        const int total = wf->num_blocks * wf->block_stride;
        std::vector<uint8_t> sample;
        sample.reserve(size_t(total / 5 + 1));
        for (int i = 0; i < total; i += 5) {
            sample.push_back(wf->mag[i]);
        }
        if (!sample.empty()) {
            auto mid = sample.begin() + qsizetype(sample.size() * 3 / 10);
            std::nth_element(sample.begin(), mid, sample.end());
            noiseLin = std::pow(10.0, WF_ELEM_MAG(*mid) / 10.0);
        }
    }
    // Tone-to-floor ratio -> SNR in 2500 Hz: measured with test signals of
    // known SNR in white noise through this same path (FT8 3.125 Hz bins,
    // FT4 10.4 Hz; the window and the bin's share of a symbol included).
    const double bwDb = protocol == FTX_PROTOCOL_FT4 ? 24.6 : 30.3;

    ftx_candidate_t candidates[kMaxCandidates];
    const int numCandidates = ftx_find_candidates(wf, kMaxCandidates, candidates, kMinScore);
    QVector<Decode> result;
    std::vector<ftx_message_t> seen;
    std::lock_guard<std::recursive_mutex> lock(g_hashMutex);
    for (int c = 0; c < numCandidates && int(seen.size()) < kMaxDecoded; ++c) {
        const ftx_candidate_t* cand = &candidates[c];
        ftx_message_t message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(wf, cand, kLdpcIterations, &message, &status)) {
            continue;
        }
        const bool dup = std::any_of(seen.begin(), seen.end(), [&](const ftx_message_t& m) {
            return m.hash == message.hash && std::memcmp(m.payload, message.payload, sizeof(m.payload)) == 0;
        });
        if (dup) {
            continue;
        }
        seen.push_back(message);
        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        if (ftx_message_decode(&message, &g_hashIf, text, &offsets) != FTX_MESSAGE_RC_OK) {
            continue;
        }
        // SNR: the message's tones against the noise floor, in 2500 Hz.
        uint8_t tones[FT4_NN > FT8_NN ? FT4_NN : FT8_NN];
        int nn = FT8_NN;
        if (protocol == FTX_PROTOCOL_FT4) {
            ft4_encode(message.payload, tones);
            nn = FT4_NN;
        } else {
            ft8_encode(message.payload, tones);
        }
        double sigSum = 0;
        int sigCount = 0;
        for (int k = 0; k < nn; ++k) {
            const int block = cand->time_offset + k;
            const int bin = cand->freq_offset + tones[k];
            if (block < 0 || block >= wf->num_blocks || bin < 0 || bin >= wf->num_bins) {
                continue;
            }
            const int idx = block * wf->block_stride + (cand->time_sub * wf->freq_osr + cand->freq_sub) * wf->num_bins + bin;
            sigSum += std::pow(10.0, WF_ELEM_MAG(wf->mag[idx]) / 10.0);
            ++sigCount;
        }
        const double ratio = sigCount > 0 ? sigSum / sigCount / noiseLin - 1.0 : 0.0;
        const int snr = std::clamp(int(std::lround(10.0 * std::log10(std::max(ratio, 1e-3)) - bwDb)), -30, 30);

        Decode d;
        d.slotMs = slotMs;
        d.snr = snr;
        d.freq = float((mon.min_bin + cand->freq_offset + float(cand->freq_sub) / wf->freq_osr) / mon.symbol_period);
        d.dt = float((cand->time_offset + float(cand->time_sub) / wf->time_osr) * mon.symbol_period) - 0.5f;
        d.text = QString::fromLatin1(text).simplified();
        result.append(d);
    }
    hashAge();
    monitor_free(&mon);
    std::sort(result.begin(), result.end(), [](const Decode& a, const Decode& b) { return a.freq < b.freq; });
    return result;
}

QByteArray Ft8Engine::encode(const QString& text, double audioHz, QString* error) const
{
    // <CALL> marks a call sent hashed. A hashed "to" call, or CQ by a call
    // the standard message cannot carry, is a type 4 message (the other call
    // in full, 58 bits); everything else goes through ft8_lib's own choice,
    // which hashes a nonstandard call by itself (K1ABC <OH6OKSA> -12).
    QStringList tokens = text.trimmed().toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool toHashed = tokens.size() >= 2 && tokens[0].startsWith(QLatin1Char('<'));
    for (QString& t : tokens) {
        t.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    }
    const bool cqNonstd = tokens.size() == 2 && tokens[0] == QLatin1String("CQ") && !isStandardCall(tokens[1]);
    ftx_message_t message;
    ftx_message_rc_t rc;
    {
        std::lock_guard<std::recursive_mutex> lock(g_hashMutex);
        if (cqNonstd || toHashed) {
            const QByteArray to = tokens.value(0).toLatin1();
            const QByteArray de = tokens.value(1).toLatin1();
            const QByteArray extra = tokens.value(2).toLatin1();
            rc = ftx_message_encode_nonstd(&message, &g_hashIf, to.constData(), de.constData(), extra.constData());
        } else {
            rc = ftx_message_encode(&message, &g_hashIf, tokens.join(QLatin1Char(' ')).toLatin1().constData());
        }
    }
    if (rc != FTX_MESSAGE_RC_OK) {
        if (error) {
            *error = tr("This message cannot be sent in FT8/FT4 (code %1).").arg(int(rc));
        }
        return {};
    }
    const bool ft4 = m_protocol == Protocol::Ft4;
    const int nn = ft4 ? FT4_NN : FT8_NN;
    const float symbolPeriod = ft4 ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    const float bt = ft4 ? 1.0f : 2.0f;
    uint8_t tones[FT4_NN > FT8_NN ? FT4_NN : FT8_NN];
    if (ft4) {
        ft4_encode(message.payload, tones);
    } else {
        ft8_encode(message.payload, tones);
    }
    const int samples = int(0.5f + nn * symbolPeriod * kTxSampleRate);
    QByteArray out(samples * int(sizeof(float)), Qt::Uninitialized);
    auto* f = reinterpret_cast<float*>(out.data());
    synthGfsk(tones, nn, float(audioHz), bt, symbolPeriod, kTxSampleRate, f);
    for (int i = 0; i < samples; ++i) {
        f[i] *= 0.5f;
    }
    return out;
}

bool Ft8Engine::isStandardCall(const QString& call)
{
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9]?[A-Z0-9][0-9][A-Z]{1,3}(/[PR])?$"));
    return re.match(call).hasMatch() && call.section(QLatin1Char('/'), 0, 0).size() <= 6;
}

bool Ft8Engine::isGrid(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[A-R]{2}[0-9]{2}([A-X]{2})?$"));
    return re.match(s).hasMatch() && s != QLatin1String("RR73");
}

bool Ft8Engine::isReport(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^R?[+-][0-9]{2}$"));
    return re.match(s).hasMatch();
}

Ft8Engine::Parsed Ft8Engine::parse(const QString& text)
{
    Parsed p;
    QStringList t = text.simplified().toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& s : t) {
        s.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    }
    if (t.isEmpty()) {
        return p;
    }
    if (t[0] == QLatin1String("CQ")) {
        p.cq = true;
        if (t.size() >= 4) {
            p.de = t[2];
            p.extra = t[3];
        } else if (t.size() == 3) {
            if (isGrid(t[2])) {
                p.de = t[1];
                p.extra = t[2];
            } else {
                p.de = t[2];
            }
        } else if (t.size() == 2) {
            p.de = t[1];
        }
        return p;
    }
    p.to = t.value(0);
    p.de = t.value(1);
    p.extra = t.value(2);
    return p;
}
