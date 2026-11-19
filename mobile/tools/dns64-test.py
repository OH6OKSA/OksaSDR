#!/usr/bin/env python3
"""A small DNS64 for tests (OksaSDR roadmap 106), with nat64-test.py.

Answers the IPv6-only test network's DNS (ipv6only-testnet.sh sends its port
53 here): an AAAA question gets the name's IPv4 addresses as PREFIX::a.b.c.d
(always, as dns64-synthall: the test network has no IPv6 of its own beyond
the NAT64), every other question goes to the upstream resolver unchanged.

    dns64-test.py [--listen fd64:64::1] [--port 5300] [--prefix 64:ff9b::]
                  [--upstream 127.0.0.53]

UDP only (the test network's resolver sends no EDNS0, so answers fit 512
bytes); runs as the user, no root.
"""

import argparse
import ipaddress
import socket
import struct
import sys


def skip_name(msg: bytes, pos: int) -> int:
    while True:
        length = msg[pos]
        if length & 0xC0 == 0xC0:
            return pos + 2
        if length == 0:
            return pos + 1
        pos += 1 + length


def ask(upstream, query: bytes) -> bytes:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(3)
        s.sendto(query, upstream)
        return s.recv(4096)


def synthesize(query: bytes, upstream, prefix: bytes):
    qid, flags, qd = struct.unpack("!HHH", query[:6])
    qend = skip_name(query, 12)
    question = query[12:qend + 4]
    # The same question for A.
    a_query = query[:4] + struct.pack("!HHHH", 1, 0, 0, 0) + query[12:qend] + struct.pack("!HH", 1, 1)
    reply = ask(upstream, a_query)
    rflags, _, ancount = struct.unpack("!HHH", reply[2:8])
    pos = skip_name(reply, 12) + 4
    answers = []
    for _ in range(ancount):
        pos = skip_name(reply, pos)
        rtype, rclass, ttl, rdlen = struct.unpack("!HHIH", reply[pos:pos + 10])
        rdata = reply[pos + 10:pos + 10 + rdlen]
        pos += 10 + rdlen
        if rtype == 1 and rdlen == 4:
            answers.append(struct.pack("!HHHIH", 0xC00C, 28, rclass, ttl, 16) + prefix + rdata)
    header = struct.pack("!HHHHHH", qid, 0x8000 | (flags & 0x0100) | 0x0080 | (rflags & 0x000F),
                         1, len(answers), 0, 0)
    return header + question + b"".join(answers), len(answers)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--listen", default="fd64:64::1")
    ap.add_argument("--port", type=int, default=5300)
    ap.add_argument("--prefix", default="64:ff9b::")
    ap.add_argument("--upstream", default="127.0.0.53")
    ap.add_argument("--no-a", action="store_true", help="answer A questions with no addresses, as iOS hides them")
    args = ap.parse_args()
    prefix = ipaddress.IPv6Address(args.prefix).packed[:12]
    upstream = (args.upstream, 53)
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    sock.bind((args.listen, args.port))
    print("dns64: on [%s]:%d, AAAA as %s/96, the rest to %s" % (args.listen, args.port, args.prefix, args.upstream), flush=True)
    while True:
        query, client = sock.recvfrom(4096)
        try:
            if len(query) < 17 or struct.unpack("!H", query[4:6])[0] != 1:
                continue
            qend = skip_name(query, 12)
            qtype = struct.unpack("!H", query[qend:qend + 2])[0]
            if qtype == 28:
                reply, count = synthesize(query, upstream, prefix)
                name = []
                p = 12
                while query[p]:
                    name.append(query[p + 1:p + 1 + query[p]].decode("ascii", "replace"))
                    p += 1 + query[p]
                print("dns64: AAAA %s: %d synthesized" % (".".join(name), count), flush=True)
            elif qtype == 1 and args.no_a:
                # NODATA: the question back, no answers.
                reply = struct.pack("!HHHHHH", struct.unpack("!H", query[:2])[0], 0x8180, 1, 0, 0, 0) + query[12:qend + 4]
            else:
                reply = ask(upstream, query)
            sock.sendto(reply, client)
        except (OSError, IndexError, struct.error) as error:
            print("dns64: no answer:", error, flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
