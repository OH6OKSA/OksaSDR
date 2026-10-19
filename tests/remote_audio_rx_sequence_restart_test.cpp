// remote_audio_rx has no registration in PanadapterStream, so RadioModel
// restarts the VITA-49 sequence tracking of every stream the radio reports
// removed (#6285), and of nothing else: a status for a stream still flowing
// must not hide a gap. The radio can give a re-created stream the same
// id; without the restart its first packet is measured against the old
// stream's last count and counted (and logged) as a sequence error.
// Socket-free: a backend with no transport, status lines fed to the model's
// router, and meter-class packets fed straight to processDatagram(). The
// restart is keyed by stream id alone, whatever the packet class.
// Loss is counted net of reordering (VitaSequenceTracker): a missing number
// counts once it falls more than 7 behind the newest packet, one error per
// lost packet, so each gap is followed by packets that take it out of the
// window; the same packets would show a skipped restart.

#include "TestSettingsProfile.h"
#include "core/backends/IRadioBackend.h"
#include "core/backends/flex/PanadapterStream.h"
#include "models/RadioModel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QMap>
#include <QString>
#include <QtEndian>

#include <cstdio>
#include <memory>

namespace AetherSDR {
class VitaSequenceLossLogTestAccess {
public:
    static void feedMeter(PanadapterStream& stream, quint32 streamId, int seq)
    {
        QByteArray packet(PanadapterStream::VITA49_HEADER_BYTES + 4, '\0');
        auto* raw = reinterpret_cast<uchar*>(packet.data());
        qToBigEndian<quint32>(0x18000000u | (quint32(seq & 0x0F) << 16), raw);
        qToBigEndian<quint32>(streamId, raw + 4);
        qToBigEndian<quint32>(PanadapterStream::PCC_METER, raw + 12);
        qToBigEndian<quint16>(1, raw + PanadapterStream::VITA49_HEADER_BYTES);
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

class Backend final : public IRadioBackend {
public:
    RadioCapabilities capabilities() const override { return {}; }
    bool ownsRxAudio() const override { return true; }
    void connectRadio(const RadioConnectRequest&) override { emit connected(); }
    void disconnectRadio() override { emit disconnected(); }
    bool isConnected() const override { return false; }
    void setSliceFrequency(int, double) override {}
    void setSliceMode(int, const QString&) override {}
    void setSliceFilter(int, int, int) override {}
    void setSliceAgc(int, const QString&, int) override {}
    void setPanCenter(const QString&, double, PanCenterIntent) override {}
    void setKeying(bool, const TxCoordinator::Operation&,
                   const TxCoordinator::Completion&) override {} // no TX transport
    void invokeExtension(const QString&, const QString&, quint64, const QVariant&) override {}
    std::unique_ptr<PanadapterStream> stream = std::make_unique<PanadapterStream>();
};

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("remote-audio-rx-seq-restart"));
    QCoreApplication app(argc, argv);

    RadioModel radio;
    auto backend = std::make_unique<Backend>();
    PanadapterStream* stream = backend->stream.get();  // no init(), no socket
    radio.setBackendForTest(std::move(backend), QStringLiteral("rtl"), stream);

    const quint32 id = 0x04000008u;
    const QString object = QStringLiteral("stream 0x04000008");
    for (int seq = 0; seq < 3; ++seq) {
        VitaSequenceLossLogTestAccess::feedMeter(*stream, id, seq);
    }
    report("in-order packets count no error", stream->packetErrorCount() == 0);

    // The radio reports the stream; it keeps flowing, so a gap across the
    // status line is a real one.
    radio.handleStatusForTest(object, {{QStringLiteral("type"), QStringLiteral("remote_audio_rx")},
                                       {QStringLiteral("compression"), QStringLiteral("none")}});
    VitaSequenceLossLogTestAccess::feedMeter(*stream, id, 5);  // 3..4 missing
    for (int seq = 6; seq <= 13; ++seq) {
        VitaSequenceLossLogTestAccess::feedMeter(*stream, id, seq);  // both out of the window by 12
    }
    report("adopted: a gap in a stream still flowing counts", stream->packetErrorCount() == 2);

    // The radio removes it; the next stream with this id starts over.
    radio.handleStatusForTest(object + QStringLiteral(" removed"), {});
    for (int seq = 0; seq <= 8; ++seq) {
        VitaSequenceLossLogTestAccess::feedMeter(*stream, id, seq);  // old instance ended at 13: 14 and 15 would read as lost
    }
    report("removed: a re-created stream's first packet counts no error",
           stream->packetErrorCount() == 2);

    VitaSequenceLossLogTestAccess::feedMeter(*stream, id, 12);  // 9..11 missing
    for (int seq : {13, 14, 15, 0, 1, 2, 3, 4}) {
        VitaSequenceLossLogTestAccess::feedMeter(*stream, id, seq);  // 11 is 9 behind at 4
    }
    report("a real gap still counts", stream->packetErrorCount() == 5);

    std::printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed ? 1 : 0;
}
