// A late remote_audio_rx packet is dropped, not measured against the newest
// packet's 4-bit count. Measured that way, a packet that arrived one place
// late read as a jump of 14 and packet-loss concealment filled 8 frames for
// it, then decoded it out of order: across a reordering path (a tester's
// iPad over a VPN and SmartLink) the audio came out choppy. Now the numbers a
// packet skips are concealed when it arrives, and a packet that fills one of
// them later is dropped; reordering is not counted as loss.
// Socket-free: IF-narrow (uncompressed float32 stereo) packets go straight to
// processDatagram(), and the frames published downstream are counted.

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

    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 0);
    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 1);
    report("in-order packets play as they are", frames == 2 * n);

    // 3 overtakes 2: 2's slot is concealed when 3 comes, and 2 is dropped.
    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 3);
    report("a skipped packet is concealed when the next one comes", frames == 4 * n);
    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 2);
    report("the late packet is dropped, no 8-frame concealment", frames == 4 * n);
    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 4);
    report("the next packet follows the newest, nothing concealed", frames == 5 * n);
    report("reordering is not counted as loss", stream.packetErrorCount() == 0);

    // A real loss: 5 and 6 never come.
    VitaSequenceLossLogTestAccess::feedAudio(stream, id, 7);
    report("a real gap is concealed", frames == 8 * n);
    for (int seq = 8; seq < 16; ++seq) {
        VitaSequenceLossLogTestAccess::feedAudio(stream, id, seq);
    }
    report("a real gap counts as loss once it is out of the window", stream.packetErrorCount() == 2);

    std::printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed ? 1 : 0;
}
