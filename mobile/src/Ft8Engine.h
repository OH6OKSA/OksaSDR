#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <vector>

namespace AetherSDR {
struct DecoderPcmBlock;
}

// FT8 and FT4 on this device (ft8_lib, third_party/ft8_lib): decodes the
// receiver's audio slot by slot and makes the transmit audio for a message.
// Slots follow UTC from the device clock. Lives on the main thread; each
// slot's decode runs on a worker of its own.
class Ft8Engine : public QObject {
    Q_OBJECT

public:
    enum class Protocol { Ft8, Ft4 };

    struct Decode {
        qint64 slotMs = 0;   // the slot's start, ms since the epoch (UTC)
        int snr = 0;         // dB in 2500 Hz, as WSJT-X
        float dt = 0.0f;     // s from the nominal start
        float freq = 0.0f;   // audio Hz
        QString text;
    };

    // A message split into its parts (calls without < >).
    struct Parsed {
        bool cq = false;
        QString to;     // empty for CQ
        QString de;     // the sender
        QString extra;  // grid, report, R-report, RR73, RRR or 73
    };

    explicit Ft8Engine(QObject* parent = nullptr);
    ~Ft8Engine() override;

    Protocol protocol() const { return m_protocol; }
    void setProtocol(Protocol protocol);
    double slotSeconds() const;
    static double slotSeconds(Protocol protocol);

    bool running() const { return m_running; }
    void setRunning(bool on);

    // Receive audio, 24 kHz mono (the decoder tap). Its on-air time is the
    // arrival time less the receive latency.
    void feed(const AetherSDR::DecoderPcmBlock& block);
    void resetAudio();
    int rxLatencyMs() const { return m_rxLatencyMs; }
    void setRxLatencyMs(int ms) { m_rxLatencyMs = ms; }

    // Transmit audio for a message at audio frequency hz: 24 kHz mono float,
    // starting with the message's first tone. Empty (and error set) when the
    // text does not encode.
    QByteArray encode(const QString& text, double audioHz, QString* error = nullptr) const;
    static constexpr int kTxSampleRate = 24000;

    // Decodes one slot of 12 kHz audio (also used by the tests).
    QVector<Decode> decodeSamples(const std::vector<float>& samples12k, qint64 slotMs) const;

    // The time FT8 / FT4 run on: the device clock plus the correction a time
    // server gave (NtpClock), as an app cannot set the device's own clock.
    static qint64 nowMs();
    static void setClockCorrectionMs(qint64 ms);
    static qint64 clockCorrectionMs();
    static Parsed parse(const QString& text);
    // A call the standard messages carry in full (K1ABC, OH2XO); others
    // (OH6OKSA, PJ4/K1ABC) need <hashed> forms and type 4 messages.
    static bool isStandardCall(const QString& call);
    static bool isGrid(const QString& s);
    static bool isReport(const QString& s);  // -12, +05, R-12, R+05

signals:
    void decoded(const QVector<Ft8Engine::Decode>& decodes, qint64 slotMs);
    void slotStarted(qint64 slotMs);
    void runningChanged();

private:
    void tick();
    void startDecode(qint64 slotMs);

    Protocol m_protocol{Protocol::Ft8};
    bool m_running{false};
    int m_rxLatencyMs{250};

    // Receive audio at 12 kHz and the on-air time of its first sample (s).
    std::vector<float> m_buf;
    double m_bufT0{0.0};
    qint64 m_lateSinceMs{0};  // when the stream began running more than 1 s late
    std::vector<float> m_decimHistory;  // half-band FIR state (24 -> 12 kHz)
    bool m_decimOdd{false};

    qint64 m_nextDecodeSlotMs{0};
    qint64 m_lastSlotMs{0};
    std::atomic<bool> m_decoding{false};
    QThreadPool m_pool;
    class QTimer* m_timer{nullptr};
};

Q_DECLARE_METATYPE(Ft8Engine::Decode)
