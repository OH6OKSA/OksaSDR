#!/usr/bin/env python3
"""A stand-in Elecraft KPA1500 for testing the app's Amp tab without one.

    mobile/tools/kpa1500-mock.py [--port 1500] [--bind 0.0.0.0]
                                 [--tx-cycle S | --keyed] [--forward W] [--swr X]
                                 [--standby] [--fault HH] [--off] [--atu bypass]
                                 [--antenna N] [--band BB] [--firmware NN.NN]
                                 [--silent-after S] [--tune-seconds S] [--no-match] [--no-ft-reply]
                                 [--log FILE]

Answers the KPA1500 Programming Reference V3's command set as the amplifier
does, on its UDP server and its TCP server on the same port (the reference:
"a single TCP client and any number of UDP clients"; over UDP one command a
packet, at most one answer; it never sends by itself). Every message is
"^CMD[arg];"; a bare one is a query, answered with the command and its
value: ^PWF (W out), ^PWR (W reflected), ^SW (SWR in tenths), ^PWI (W in),
^VI (PA volts in tenths and amps, "^VI513 061;"), ^TM (°C), ^BN (band
number), ^FL (two hex digits), ^OS (1 OPERATE, 0 STANDBY), ^ON (main
supplies), ^AN (antenna), ^AM (I / B), ^AI (1 / 0), ^TP (tuning), ^RV
(firmware). With --tx-cycle S it keys and unkeys every S seconds (forward
up to --forward W at --swr, the drive, current and temperature following);
--keyed transmits all the time. --off answers only ^ON0; and ^RV, as a
sleeping amplifier's microcontroller does; --silent-after S stops answering
after S seconds (the amplifier switched off, a VPN down).

Every query is printed as "UDP ^CMD;" or "TCP ^CMD;"; a command that would
change the amplifier (a SET, or ^FT / ^FE / ^FLC) is printed as "WRITE"
and counted in the summary at the end (Ctrl-C, or --log FILE written as it
goes): with the app's Control off it must stay at 0. The stand-in acts on
them as the amplifier does: ^OS0 / ^OS1 (OPERATE clears a fault but the
temperature one), ^AN1 / ^AN2, ^AMI / ^AMB, ^AI0 / ^AI1, ^FLC (clears the
fault but a temperature one), ^FT (a full-search tune: ^TP1 for
--tune-seconds, 3, then done and in line, or with --no-match fault 92 in
STANDBY), ^FE (cancels it). A tune started over TCP ends with the
amplifier's "^FT;" on that link, done or cancelled, as the reference says
("When the tune completes or is cancelled, the KPA1500 responds with
^FT;"); --no-ft-reply leaves it out (only ^TP tells). A short search
(--tune-seconds 0.3) is over between two of the app's ^TP readings.
"""

import argparse
import math
import select
import signal
import socket
import sys
import time

QUERIES = {"PWF", "PWR", "SW", "PWI", "VI", "TM", "BN", "FL", "OS", "ON", "AN", "AM", "AI", "TP", "RV", "TQ"}
THREE = {"BRP", "BRX", "PWD", "PWF", "PWI", "PWR", "RVM", "STA", "STB", "STN", "STS", "VMH"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=1500)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--tx-cycle", type=float, default=0.0)
    ap.add_argument("--keyed", action="store_true")
    ap.add_argument("--forward", type=float, default=1200.0)
    ap.add_argument("--swr", type=float, default=1.3)
    ap.add_argument("--standby", action="store_true")
    ap.add_argument("--fault", default="00")
    ap.add_argument("--off", action="store_true")
    ap.add_argument("--atu", choices=["inline", "bypass"], default="inline")
    ap.add_argument("--antenna", type=int, default=1)
    ap.add_argument("--band", type=int, default=5)
    ap.add_argument("--firmware", default="03.08")
    ap.add_argument("--silent-after", type=float, default=0.0)
    ap.add_argument("--tune-seconds", type=float, default=3.0)
    ap.add_argument("--no-ft-reply", action="store_true")
    ap.add_argument("--no-match", action="store_true")
    ap.add_argument("--log")
    args = ap.parse_args()

    # A kill (the screenshot tool's) ends it as Ctrl-C does: the summary.
    def stop(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)

    log = open(args.log, "a", buffering=1) if args.log else None
    counts = {"queries": 0, "writes": 0}
    start = time.monotonic()
    # What a SET changes, as the amplifier keeps it.
    st = {"operate": not args.standby and args.fault == "00", "antenna": args.antenna,
          "atu": "I" if args.atu == "inline" else "B", "fault": args.fault.upper(), "tune_end": 0.0,
          "ft_owed": False}

    def tune_poll():
        # A running search ends after --tune-seconds: in line and stored, or
        # no match (fault 92, STANDBY).
        if st["tune_end"] and time.monotonic() >= st["tune_end"]:
            st["tune_end"] = 0.0
            if args.no_match:
                st["fault"], st["operate"] = "92", False
                say("MOCK tune: no match (fault 92)")
            else:
                st["atu"] = "I"
                say("MOCK tune: done, in line")

    def say(line):
        print(line, flush=True)
        if log:
            log.write(line + "\n")

    def keyed_now():
        if not st["operate"] or args.off:
            return False
        if args.keyed:
            return True
        if args.tx_cycle > 0:
            return int((time.monotonic() - start) / args.tx_cycle) % 2 == 1
        return False

    def value(cmd):
        k = keyed_now()
        fwd = args.forward * (0.97 + 0.03 * math.sin(time.monotonic() * 3)) if k else 0.0
        refl = fwd * ((args.swr - 1) / (args.swr + 1)) ** 2
        drive = fwd / 20.0 if k else 0.0
        amps = int(round(fwd / 50.0 / 0.55)) if k else 2
        volts = 498 if k else 532
        temp = 48 if k else 38
        if cmd == "PWF":
            return "%04d" % round(fwd)
        if cmd == "PWR":
            return "%04d" % round(refl)
        if cmd == "SW":
            return "%03d" % round((args.swr if k else 1.0) * 10)
        if cmd == "PWI":
            return "%04d" % round(drive)
        if cmd == "VI":
            return "%03d %03d" % (volts, amps)
        if cmd == "TM":
            return "%03d" % temp
        if cmd == "BN":
            return "%02d" % args.band
        if cmd == "FL":
            return st["fault"]
        if cmd == "OS":
            return "1" if st["operate"] else "0"
        if cmd == "ON":
            return "0" if args.off else "1"
        if cmd == "AN":
            return str(st["antenna"])
        if cmd == "AM":
            return st["atu"]
        if cmd == "AI":
            return "1" if st["atu"] == "I" else "0"
        if cmd == "TP":
            tune_poll()
            return "1" if st["tune_end"] else "0"
        if cmd == "RV":
            return args.firmware
        if cmd == "TQ":
            return "0"
        return None

    def act(cmd, arg):
        if cmd == "OS" and arg in ("0", "1"):
            st["operate"] = arg == "1"
            if st["operate"] and st["fault"] != "40":
                st["fault"] = "00"
        elif cmd == "AN" and arg in ("1", "2"):
            st["antenna"] = int(arg)
        elif cmd == "AM" and arg in ("I", "B"):
            st["atu"] = arg
        elif cmd == "AI" and arg in ("0", "1"):
            st["atu"] = "I" if arg == "1" else "B"
        elif cmd == "FL" and arg == "C":
            if st["fault"] != "40":
                st["fault"] = "00"
        elif cmd == "FT":
            st["tune_end"] = time.monotonic() + args.tune_seconds
        elif cmd == "FE":
            st["tune_end"] = 0.0
        say("MOCK state: %s %s ANT%d ATU %s fault %s%s" % (
            via_name(), "OPERATE" if st["operate"] else "STANDBY", st["antenna"], st["atu"], st["fault"],
            " tuning" if st["tune_end"] else ""))

    def via_name():
        return "now"

    def answer(frame, via):
        """The answer to one ^...; frame, or None."""
        body = frame[1:-1]
        head = body[:3].upper()
        n = 3 if head in THREE else 2
        cmd, arg = body[:n].upper(), body[n:]
        if arg or cmd in ("FT", "FE", "RX", "TX"):
            counts["writes"] += 1
            say("%s WRITE ^%s%s;" % (via, cmd, arg))
            act(cmd, arg)
            if cmd in ("FT", "FE") and via == "TCP" and not args.no_ft_reply:
                st["ft_owed"] = st["ft_owed"] or cmd == "FT"
            return None
        counts["queries"] += 1
        say("%s ^%s;" % (via, cmd))
        if args.silent_after and time.monotonic() - start > args.silent_after:
            return None
        if args.off and cmd not in ("ON", "RV"):
            return None
        if cmd not in QUERIES:
            return None
        v = value(cmd)
        return None if v is None else ("^%s%s;" % (cmd, v)).encode("ascii")

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind((args.bind, args.port))
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind((args.bind, args.port))
    tcp.listen(1)
    client = None
    buf = b""
    say("KPA1500 stand-in on %s:%d (UDP and TCP)" % (args.bind, args.port))
    try:
        while True:
            socks = [udp, tcp] + ([client] if client else [])
            ready, _, _ = select.select(socks, [], [], 0.05)
            # A search over (done, no match or cancelled): its ^FT; on the
            # TCP link that started it.
            tune_poll()
            if st["ft_owed"] and not st["tune_end"]:
                st["ft_owed"] = False
                if client:
                    client.sendall(b"^FT;")
                    say("TCP ^FT; sent (the tune is over)")
            for s in ready:
                if s is udp:
                    data, peer = udp.recvfrom(512)
                    text = data.decode("latin-1")
                    i = text.find("^")
                    j = text.find(";", i + 1)
                    if i >= 0 and j > i:
                        out = answer(text[i:j + 1], "UDP")
                        if out:
                            udp.sendto(out, peer)
                elif s is tcp:
                    conn, peer = tcp.accept()
                    if client:
                        say("TCP second client %s refused (one TCP client)" % (peer,))
                        conn.close()
                    else:
                        client, buf = conn, b""
                        say("TCP client %s" % (peer,))
                else:
                    data = client.recv(512)
                    if not data:
                        say("TCP client gone")
                        client.close()
                        client = None
                        continue
                    buf += data
                    while b";" in buf:
                        frame, buf = buf.split(b";", 1)
                        i = frame.find(b"^")
                        if i >= 0:
                            out = answer(frame[i:].decode("latin-1") + ";", "TCP")
                            if out:
                                client.sendall(out)
    except KeyboardInterrupt:
        pass
    finally:
        say("summary: %d queries, %d writes" % (counts["queries"], counts["writes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
