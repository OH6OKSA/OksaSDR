#!/usr/bin/env python3
"""A small NAT64 for tests (OksaSDR roadmap 106): IPv6 to the radio's IPv4.

The IPv6 side is an IPv6-only network (ipv6only-testnet.sh's namespace); its
packets to PREFIX::a.b.c.d come in on the TUN device, leave as IPv4 to
a.b.c.d from a pool address (one per IPv6 client, as tayga), and the kernel's
NAT44 (masquerade) takes them out to the internet; the answers come back the
same way. TCP, UDP and ping (ICMP echo); not fragments or ICMP errors. That is
what a carrier's NAT64 does for a SmartLink session: the radio sees the TLS and
the UDP from one IPv4 address.

    nat64-test.py [--tun nat64] [--prefix 64:ff9b::] [--pool 192.168.255.0/24]

Runs as the user who owns the TUN device (ip tuntap add ... user NAME); no root.
"""

import argparse
import fcntl
import ipaddress
import os
import struct
import sys
import time

TUNSETIFF = 0x400454CA
IFF_TUN = 0x0001
IFF_NO_PI = 0x1000


def checksum(data: bytes) -> int:
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def with_checksum(segment: bytes, offset: int, pseudo: bytes, udp: bool) -> bytes:
    seg = bytearray(segment)
    seg[offset:offset + 2] = b"\0\0"
    value = checksum(pseudo + bytes(seg))
    if udp and value == 0:
        value = 0xFFFF
    seg[offset:offset + 2] = struct.pack("!H", value)
    return bytes(seg)


class Nat64:
    def __init__(self, prefix: ipaddress.IPv6Address, pool: ipaddress.IPv4Network, trace=None):
        self.trace = trace.packed if trace else None
        self.prefix = prefix.packed[:12]
        self.pool = list(pool.hosts())[1:]   # .1 is the host's end of the TUN
        self.to4 = {}
        self.to6 = {}
        self.ident = 0
        self.counts = {"6to4": 0, "4to6": 0, "dropped": 0}
        self.flows = set()

    def client4(self, src6: bytes):
        v4 = self.to4.get(src6)
        if v4 is None:
            if len(self.to4) >= len(self.pool):
                return None
            v4 = self.pool[len(self.to4)].packed
            self.to4[src6] = v4
            self.to6[v4] = src6
            print("nat64: %s is %s on the IPv4 side" % (ipaddress.IPv6Address(src6), ipaddress.IPv4Address(v4)), flush=True)
        return v4

    def show(self, way: str, proto: int, seg: bytes):
        if proto != 6 or len(seg) < 20:
            print("trace %s proto %d len %d" % (way, proto, len(seg)), flush=True)
            return
        sport, dport, seq, ack, off, flags = struct.unpack("!HHIIBB", seg[:14])
        names = "".join(n for b, n in ((0x02, "S"), (0x10, "."), (0x08, "P"), (0x01, "F"), (0x04, "R")) if flags & b)
        print("trace %.3f %s %d>%d %s seq %d ack %d data %d" % (time.monotonic() % 1000, way, sport, dport, names,
                                                             seq, ack, len(seg) - (off >> 4) * 4), flush=True)

    def drop(self, why: str):
        self.counts["dropped"] += 1
        if self.counts["dropped"] <= 20:
            print("nat64: dropped", why, flush=True)
        return None

    def six_to_four(self, pkt: bytes):
        if len(pkt) < 40:
            return self.drop("a short IPv6 packet")
        tclass = ((pkt[0] & 0x0F) << 4) | (pkt[1] >> 4)
        plen, nh, hlim = struct.unpack("!HBB", pkt[4:8])
        src, dst = pkt[8:24], pkt[24:40]
        if dst[:12] != self.prefix:
            return self.drop("IPv6 to %s (not the NAT64 prefix)" % ipaddress.IPv6Address(dst))
        payload = pkt[40:40 + plen]
        src4, dst4 = self.client4(src), dst[12:16]
        if src4 is None:
            return self.drop("the pool is full")
        if nh == 58:
            if len(payload) < 8 or payload[0] not in (128, 129):
                return self.drop("ICMPv6 type %d" % (payload[0] if payload else -1))
            icmp = bytes([8 if payload[0] == 128 else 0, 0]) + b"\0\0" + payload[4:]
            seg = with_checksum(icmp, 2, b"", False)
            proto = 1
        elif nh in (6, 17):
            pseudo = src4 + dst4 + struct.pack("!BBH", 0, nh, len(payload))
            seg = with_checksum(payload, 16 if nh == 6 else 6, pseudo, nh == 17)
            proto = nh
        else:
            return self.drop("IPv6 next header %d" % nh)
        flow = (proto, dst4, struct.unpack("!H", payload[2:4])[0] if proto != 1 else 0)
        if flow not in self.flows:
            self.flows.add(flow)
            print("nat64: new flow %s to %s port %d" % ({1: "ping", 6: "TCP", 17: "UDP"}[proto],
                                                       ipaddress.IPv4Address(dst4), flow[2]), flush=True)
        if self.trace == dst4:
            self.show("out", proto, seg)
        self.ident = (self.ident + 1) & 0xFFFF
        header = struct.pack("!BBHHHBBH4s4s", 0x45, tclass, 20 + len(seg), self.ident, 0,
                             hlim, proto, 0, src4, dst4)
        header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
        self.counts["6to4"] += 1
        return header + seg

    def four_to_six(self, pkt: bytes):
        if len(pkt) < 20:
            return self.drop("a short IPv4 packet")
        ihl = (pkt[0] & 0x0F) * 4
        tos = pkt[1]
        total, _, frag, ttl, proto = struct.unpack("!HHHBB", pkt[2:10])
        if frag & 0x3FFF:
            return self.drop("an IPv4 fragment")
        src, dst = pkt[12:16], pkt[16:20]
        dst6 = self.to6.get(dst)
        if dst6 is None:
            return self.drop("IPv4 to %s (no client)" % ipaddress.IPv4Address(dst))
        src6 = self.prefix + src
        seg = pkt[ihl:total]
        if proto == 1:
            if len(seg) < 8 or seg[0] not in (0, 8):
                return self.drop("ICMP type %d" % (seg[0] if seg else -1))
            icmp6 = bytes([129 if seg[0] == 0 else 128, 0]) + b"\0\0" + seg[4:]
            pseudo = src6 + dst6 + struct.pack("!I", len(icmp6)) + b"\0\0\0" + bytes([58])
            seg6 = with_checksum(icmp6, 2, pseudo, False)
            nh = 58
        elif proto in (6, 17):
            pseudo = src6 + dst6 + struct.pack("!I", len(seg)) + b"\0\0\0" + bytes([proto])
            seg6 = with_checksum(seg, 16 if proto == 6 else 6, pseudo, proto == 17)
            nh = proto
        else:
            return self.drop("IPv4 protocol %d" % proto)
        if self.trace == src:
            self.show("in ", proto, seg)
        header = struct.pack("!IHBB16s16s", (6 << 28) | (tos << 20), len(seg6), nh, ttl, src6, dst6)
        self.counts["4to6"] += 1
        return header + seg6


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tun", default="nat64")
    ap.add_argument("--prefix", default="64:ff9b::")
    ap.add_argument("--pool", default="192.168.255.0/24")
    ap.add_argument("--trace", help="print every TCP segment to and from this IPv4 address")
    args = ap.parse_args()
    nat = Nat64(ipaddress.IPv6Address(args.prefix), ipaddress.IPv4Network(args.pool),
                ipaddress.IPv4Address(args.trace) if args.trace else None)
    fd = os.open("/dev/net/tun", os.O_RDWR)
    fcntl.ioctl(fd, TUNSETIFF, struct.pack("16sH", args.tun.encode(), IFF_TUN | IFF_NO_PI))
    print("nat64: on %s, %s/96 to IPv4 from %s" % (args.tun, args.prefix, args.pool), flush=True)
    last = time.monotonic()
    while True:
        pkt = os.read(fd, 65535)
        version = pkt[0] >> 4 if pkt else 0
        out = nat.six_to_four(pkt) if version == 6 else nat.four_to_six(pkt) if version == 4 else None
        if out:
            os.write(fd, out)
        now = time.monotonic()
        if now - last >= 10:
            last = now
            print("nat64: packets %(6to4)d out, %(4to6)d in, %(dropped)d dropped" % nat.counts, flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
