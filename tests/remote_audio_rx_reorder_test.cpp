// remote_audio_rx packets are put back in order before decoding (upstream
// #6992, #6051). Decoded as they came, a packet one place late read as a
// jump of 14 and packet-loss concealment filled 8 frames for it; across a
// reordering path (a tester's iPad over a VPN and SmartLink) the audio came
// out choppy. Now a packet after a gap waits up to 30 ms for the missing one,
// the late one plays in its place, and only a gap still open after 30 ms is
// concealed; a packet later than that is dropped. Reordering is not counted
// as loss.
// Socket-free: IF-narrow (uncompressed float32 stereo) packets go straight to
// processDatagram(), the reorder clock is the test's, and the frames
// published downstream are counted.

#include "TestSettingsProfile.h"
#include "core/PcmFrame.h"
#include "core/backends/flex/PanadapterStream.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QtEndian>

#include <cstdio>
#include <cstring>

namespace AetherSDR {
class VitaSequenceLossLogTestAccess {
public:
    static constexpr int kFrames = 32;

    static void feedAudio(PanadapterStream& stream, quint32 streamId, int seq)
    {
        const int payloadBytes = kFrames * 2 * 4;
        const int totalBytes = PanadapterStream::VITA49_HEADER_BYTES + payloadBytes;
        QByteArray packet(totalBytes, '\0');
        auto* raw = reinterpret_cast<uchar*>(packet.data());
        qToBigEndian<quint32>(0x18000000u | (quint32(seq & 0x0F) << 16) | quint32(totalBytes / 4), raw);
        qToBigEndian<quint32>(streamId, raw + 4);
        qToBigEndian<quint32>(PanadapterStream::PCC_IF_NARROW, raw + 12);
        const float sample = 0.25f;
        quint32 bits = 0;
        std::memcpy(&bits, &sample, 4);
        for (int i = 0; i < kFrames * 2; ++i) {
            qToBigEndian<quint32>(bits, raw + PanadapterStream::VITA49_HEADER_BYTES + i * 4);
        }
        stream.processDatagram(packet);
    }

    static void setNow(PanadapterStream& stream, qint64 ms) { stream.m_rxReorderNowForTest = ms; }
};
} // namespace AetherSDR

using namespace AetherSDR;

namespace {

int g_failed = 0;

void report(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok) {
        ++g_failed;
    }
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("remote-audio-rx-reorder"));
    QCoreApplication app(argc, argv);

    PanadapterStream stream;  // no init(), no socket
    qsizetype frames = 0;
    QObject::connect(&stream, &PanadapterStream::pcmFrameReady, &app,
                     [&frames](const PcmFrame& frame) { frames += frame.frameCount(); });

    constexpr int n = VitaSequenceLossLogTestAccess::kFrames;
    const quint32 id = 0x04000008u;
    using T = VitaSequenceLossLogTestAccess;
    T::setNow(stream, 1000);

    T::feedAudio(stream, id, 0);
    T::feedAudio(stream, id, 1);
    report("in-order packets play as they are", frames == 2 * n);

    // 3 overtakes 2: 3 waits for 2, then both play in order, nothing concealed.
    T::feedAudio(stream, id, 3);
    report("a packet after a gap waits for the missing one", frames == 2 * n);
    T::setNow(stream, 1010);
    T::feedAudio(stream, id, 2);
    report("the late packet plays in its place, then the one that waited", frames == 4 * n);
    T::feedAudio(stream, id, 4);
    report("the next packet plays at once", frames == 5 * n);
    report("reordering is not counted as loss", stream.packetErrorCount() == 0);

    // A real loss: 5 and 6 never come. 7 waits 30 ms, then the gap is concealed.
    T::feedAudio(stream, id, 7);
    report("a packet after a real gap waits", frames == 5 * n);
    T::setNow(stream, 1029);
    T::feedAudio(stream, id, 8);
    report("still waiting before 30 ms", frames == 5 * n);
    T::setNow(stream, 1041);
    T::feedAudio(stream, id, 9);
    report("after 30 ms the gap is concealed and 7, 8, 9 play", frames == 10 * n);
    T::feedAudio(stream, id, 5);
    report("a packet later than 30 ms is dropped", frames == 10 * n);
    for (int seq = 10; seq < 16; ++seq) {
        T::feedAudio(stream, id, seq);
    }
    report("the stream plays on in order", frames == 16 * n);
    // 5 came (too late to play), 6 never: the network lost one packet.
    report("a real gap counts as loss once it is out of the window", stream.packetErrorCount() == 1);

    std::printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed ? 1 : 0;
}
