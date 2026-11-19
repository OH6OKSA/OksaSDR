// Opt-in check of production UDP wiring, separate from default socket-free
// cadence/qualification/lifecycle coverage. No firmware peer or radio session.
#include "core/backends/flex/PanadapterStream.h"
#include "core/backends/flex/RadioConnection.h"
#include "TestSettingsProfile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QMetaObject>
#include <QNetworkDatagram>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>
#include <cstdio>

namespace AetherSDR {
class WanUdpKeepaliveSocketTestAccess {
public:
    static QTimer& timer(PanadapterStream& stream) { return *stream.m_wanRegisterTimer; }
    static bool confirmed(const PanadapterStream& stream) { return stream.m_wanRegistered; }
    static void timeout(PanadapterStream& stream)
    {
        QMetaObject::invokeMethod(stream.m_wanRegisterTimer, "timeout", Qt::DirectConnection);
    }
};
}
using namespace AetherSDR;

namespace {
int failures = 0;
void check(bool ok, const char* claim)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", claim);
    failures += !ok;
}
void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
}
int drain(QUdpSocket& sink, const QByteArray& expected)
{
    int count = 0;
    while (sink.hasPendingDatagrams()) {
        check(sink.receiveDatagram().data() == expected, "native UDP payload is padded udp_register");
        ++count;
    }
    return count;
}
}

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("wan-udp-keepalive-socket"));
    if (!profile.isValid()) {
        return 77;
    }
    QCoreApplication app(argc, argv);
    QUdpSocket outgoing;
    QUdpSocket incoming;
    if (!outgoing.bind(QHostAddress::LocalHost, 0) || !incoming.bind(QHostAddress::LocalHost, 0)) {
        std::fprintf(stderr, "SKIP: cannot bind own loopback sinks\n");
        return 77;
    }
    PanadapterStream stream;
    stream.init();
    if (!stream.startWan(QHostAddress::LocalHost, outgoing.localPort())) {
        std::fprintf(stderr, "SKIP: cannot bind production UDP socket\n");
        return 77;
    }
    const QByteArray expected("client udp_register handle=0x0000000A");
    stream.startWanUdpRegister(0xAu);
    pump(20);
    check(drain(outgoing, expected) >= 1, "production writer reaches the configured sink");

    QByteArray packet(32, '\0');
    auto* raw = reinterpret_cast<uchar*>(packet.data());
    qToBigEndian<quint32>(0x38D00008u, raw);
    qToBigEndian<quint32>(0x70000001u, raw + 4);
    qToBigEndian<quint32>(0x001C2Du, raw + 8);
    qToBigEndian<quint32>(0x534C8002u, raw + 12);
    qToBigEndian<quint16>(1, raw + 28);
    check(incoming.writeDatagram(packet, QHostAddress::LocalHost, stream.localPort()) == packet.size(),
          "own incoming socket delivers a Flex VITA fixture");
    pump(100);
    check(WanUdpKeepaliveSocketTestAccess::confirmed(stream)
          && WanUdpKeepaliveSocketTestAccess::timer(stream).interval() == 5000,
          "readyRead qualifies VITA from a different source endpoint");
    check(stream.categoryStats(PanadapterStream::CatMeter).packets == 1,
          "native receive wiring preserves meter dispatch");
    drain(outgoing, expected);
    pump(120);
    check(drain(outgoing, expected) == 0, "native confirmation ends rapid sends");

    // An IPv6-only network (OksaSDR roadmap 106): the TLS session reached the
    // radio's IPv4 address through the NAT64, so the UDP goes there too.
    const QHostAddress publicIp(QStringLiteral("203.0.113.7"));
    const QHostAddress nat64(QStringLiteral("64:ff9b::cb00:7107"));
    check(PanadapterStream::wanUdpTarget(publicIp, nat64) == nat64, "a NAT64 TLS peer takes the UDP along");
    check(PanadapterStream::wanUdpTarget(publicIp, publicIp) == publicIp, "an IPv4 TLS peer keeps the IPv4 UDP");
    check(PanadapterStream::wanUdpTarget(publicIp, QHostAddress(QStringLiteral("::ffff:203.0.113.7"))) == publicIp,
          "a v4-mapped TLS peer keeps the IPv4 UDP");
    check(PanadapterStream::wanUdpTarget(publicIp, QHostAddress()) == publicIp, "no TLS peer keeps the IPv4 UDP");
    check(PanadapterStream::wanUdpTarget(publicIp, QHostAddress(QStringLiteral("fe80::1"))) == publicIp,
          "a link-local TLS peer keeps the IPv4 UDP");
    QUdpSocket outgoing6;
    QUdpSocket incoming6;
    if (outgoing6.bind(QHostAddress::LocalHostIPv6, 0) && incoming6.bind(QHostAddress::LocalHostIPv6, 0)) {
        const auto metersBefore = stream.categoryStats(PanadapterStream::CatMeter).packets;
        check(stream.startWan(QHostAddress::LocalHostIPv6, outgoing6.localPort()), "an IPv6 radio address binds");
        stream.startWanUdpRegister(0xAu);
        pump(20);
        check(drain(outgoing6, expected) >= 1, "udp_register reaches the radio's IPv6 address");
        check(incoming6.writeDatagram(packet, QHostAddress::LocalHostIPv6, stream.localPort()) == packet.size(),
              "own IPv6 socket delivers a Flex VITA fixture");
        pump(100);
        check(WanUdpKeepaliveSocketTestAccess::confirmed(stream), "VITA over IPv6 confirms the registration");
        check(stream.categoryStats(PanadapterStream::CatMeter).packets > metersBefore, "VITA over IPv6 is dispatched");
        check(stream.startWan(QHostAddress::LocalHost, outgoing.localPort()), "back to IPv4 rebinds IPv4");
        stream.startWanUdpRegister(0xAu);
        pump(20);
        check(drain(outgoing, expected) >= 1, "udp_register reaches the IPv4 sink again");
        drain(outgoing6, expected);
        check(!outgoing6.hasPendingDatagrams(), "nothing more to the IPv6 sink");
    } else {
        std::printf("SKIP IPv6 loopback not available\n");
    }

    RadioConnection conn; // cold, never dials
    check(stream.start(&conn), "WAN to LAN binds normally");
    WanUdpKeepaliveSocketTestAccess::timeout(stream);
    pump(120);
    check(!WanUdpKeepaliveSocketTestAccess::timer(stream).isActive()
          && drain(outgoing, expected) == 0, "LAN start retires WAN registration");
    check(stream.rebindToEphemeralPort(&conn), "LAN collision rebind remains usable");
    stream.stop();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
