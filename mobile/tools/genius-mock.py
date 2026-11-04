#!/usr/bin/env python3
"""A stand-in for 4O3A's Power Genius XL (TCP 9008) and Tuner Genius XL
(TCP 9010), for testing the app's direct links without the devices.

    mobile/tools/genius-mock.py [--bind ADDR] [--pgxl-port 9008] [--tgxl-port 9010]
                                [--auth CODE] [--transmit] [--radio HOST]

Speaks the frames upstream's protocol tests replay from real devices
(tests/pgxl_direct_protocol_test.cpp, tests/tgxl_direct_protocol_test.cpp):
the V version line (with " AUTH" and the auth handshake under --auth),
C<seq>|cmd -> R<seq>|code|body, S0| state pushes and M| alerts. The PGXL
answers info, setup read, status, setup writes (fan mode) and the radio's
relayed operate=0/1 (STANDBY / IDLE, IDLE being OPERATE unkeyed); the TGXL
answers info and status, the radio's relayed operate set= / bypass set=, steps its relays (tune relay=1..3 move=D, 1 = C1), switches
its antenna (activate ant=N) and runs a 3 s autotune that ends with a new
solution and a "Tuned SWR" alert. --transmit reports the amplifier keyed at
1 kW. Every command received is printed, so a test can check what the app
sent. Nothing here reaches a real device.

--radio HOST registers the stand-ins with that FLEX radio as the real
devices do once paired (4O3A's "PGXL Amplifier to Radio API", March 2024):
a non-GUI client on the radio's TCP 4992, "amplifier create ip= port=
model= serial_num= ant=", "keepalive enable" and a ping every few seconds.
The radio then reports them to every client, SmartLink ones included, as
it would the real devices, and they go when this exits (the connection
closes). No interlock is created, so the radio's transmit is not gated on
the stand-ins; --radio-meters adds the PGXL's meters (FWD, RL, DRV, ID,
TEMP) on the radio, their values streamed to its VITA-49 port 4991. Every line the radio sends about them is
printed ("RADIO ->"). --radio-tune makes the TGXL's autotune key the
radio's tune carrier for its 3 s, as the real tuner does: that TRANSMITS,
at the radio's tune power, on its TX slice.
"""

import argparse
import socket
import threading
import time

LOCK = threading.Lock()
PGXL = {"state": "STANDBY", "fanmode": "STANDARD", "meffa": "STANDBY"}
TGXL = {"operate": 1, "tuning": 0, "relayC1": 44, "relayL": 12, "relayC2": 8, "antA": 0, "bypass": 0}
TGXL_CLIENTS = []
TGXL_TUNE_END = [0.0]   # when the running autotune ends
RADIO_SEND = {}   # device name -> send(cmd) on its radio connection (--radio)


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def pgxl_status(transmit):
    s = dict(PGXL)
    if transmit:
        s.update(state="TRANSMIT_A", fwd="60.0", swr="-26.0", id="38.5", vdd="51.9")
    else:
        # Idle: no forward power (0 dBm is 1 mW, shown as 0 W; 30 dBm read
        # as a steady 1 W).
        s.update(fwd="0.0", swr="-60.0", id="0.0", vdd="0.0" if s["state"] == "STANDBY" else "51.8")
    return ("state={state} bandA=20 bandB=0 bsrcA=FLEX bsrcB=FLEX flexA=FLEX-8600 flexB=FLEX-8600 vac=245 "
            "vdd={vdd} id={id} peakid={id} fwd={fwd} peakfwd={fwd} swr={swr} temp=31.4 cntfreq=0 cat1freq=0 "
            "cat2freq=0 hltemp=29.8 biasA=RADIO_AAB biasB=RADIO_AB fanmode={fanmode} meffa={meffa}").format(**s)


def tgxl_state():
    return ("state bypassA={bypass} bypassRxA=0 antA={antA} bypassB=0 bypassRxB=0 antB=0 state={operate} "
            "tuning={tuning} bypass={bypass} relayC1={relayC1} relayL={relayL} relayC2={relayC2}").format(**TGXL)


def tgxl_status(seq):
    # While it tunes the tuner reads the radio's carrier: fwd in dBm (41.76 is
    # 15 W, the test radio's tune power), swr as return loss falling from
    # -9.5 dB (SWR 2.0) toward the match as the search goes on.
    t = dict(TGXL)
    if TGXL["tuning"]:
        left = max(0.0, TGXL_TUNE_END[0] - time.time())
        t.update(fwd="41.76", swr="%.4f" % -(9.5 + (3.0 - left) * 5.0), ptt=1)
    else:
        t.update(fwd="21.42", swr="-60.0000", ptt=0)
    return ("S{seq}|status fwd={fwd} peak={fwd} max=62.43 swr={swr} pttA={ptt} bandA=6 modeA=1 flexA=FLEX-8600 "
            "freqA=14161.500 bypassA={bypass} bypassRxA=0 antA={antA} pttB=0 bandB=0 modeB=0 flexB=FLEX-8600 "
            "freqB=0.000 bypassB=0 bypassRxB=0 antB=0 state={operate} active=1 tuning={tuning} bypass={bypass} ag=0 "
            "relayC1={relayC1} relayL={relayL} relayC2={relayC2}").format(seq=seq, **t)


def push_tgxl(line):
    for c in list(TGXL_CLIENTS):
        try:
            c.sendall((line + "\n").encode())
        except OSError:
            pass


def serve(port, bind, handler):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((bind, port))
    srv.listen(4)
    log("listening on", bind, port)
    while True:
        conn, peer = srv.accept()
        threading.Thread(target=handler, args=(conn, peer), daemon=True).start()


def lines(conn):
    buf = b""
    while True:
        data = conn.recv(4096)
        if not data:
            return
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            yield line.decode(errors="replace").strip()


def handshake(conn, version, auth, ok_reply, auth_prefix, peer=None, radio="", ask_lan=False):
    # The real devices ask a client from outside their network, never the
    # paired radio: with --radio, the radio's connection is not asked, nor
    # one from the mock's own /24 (its LAN; a VPN client is asked).
    lan = conn.getsockname()[0].rsplit(".", 1)[0] + "."
    if not auth or (radio and peer == radio) or (not ask_lan and (peer or "").startswith(lan)):
        conn.sendall(f"V{version}\n".encode())
        return lines(conn)
    conn.sendall(f"V{version} AUTH\n".encode())
    it = lines(conn)
    first = next(it, "")
    seq, _, cmd = first[1:].partition("|")
    log("auth:", "<code>" if auth in cmd else cmd, "from", peer)
    if cmd == auth_prefix + auth:
        conn.sendall(f"R{seq}|0|{ok_reply}\n".encode())
        return it
    # The TGXL refuses with code 0 and "Unauthorized" (4O3A's protocol); the
    # PGXL's refusal is not documented, an error code stands in.
    conn.sendall((f"R{seq}|0|Unauthorized\n" if ok_reply == "auth OK" else f"R{seq}|5000002D|\n").encode())
    conn.close()
    return iter(())


def pgxl_handler(conn, peer, args):
    log("PGXL client", peer[0])
    for line in handshake(conn, "3.8.9", args.auth, "Authorized", "auth code=", peer[0], args.radio, args.ask_lan):
        if not line.startswith("C"):
            continue
        seq, _, cmd = line[1:].partition("|")
        if cmd != "status":
            log("PGXL <-", cmd)
        if cmd == "info":
            reply = "0|serial=10-200/24-0203  version=3.8.9 protocol=1.0 mains=240"
        elif cmd == "setup read":
            # A device with a remote code reports it here (the app then leaves
            # authcode= out of its setup writes, as 4O3A firmware 3.9.8 wants).
            reply = "0|ledintens=141 txdelay=16 inactivity-timeout=0 nickname=PowerGeniusXL authcode=" + args.auth
        elif cmd == "status":
            with LOCK:
                reply = "0|" + pgxl_status(args.transmit)
        elif cmd in ("operate=0", "operate=1"):
            # What the radio relays from a client's "amplifier set <h>
            # operate=" (seen from a FLEX-6600M, v1.4.0.0 protocol).
            with LOCK:
                PGXL["state"] = "IDLE" if cmd.endswith("1") else "STANDBY"
                push = f"S0|state={PGXL['state']} fanmode={PGXL['fanmode']} meffa={PGXL['meffa']}"
            conn.sendall(f"R{seq}|0|\n".encode())
            conn.sendall((push + "\n").encode())
            continue
        elif cmd.startswith("setup "):
            with LOCK:
                for kv in cmd.split()[1:]:
                    k, _, v = kv.partition("=")
                    if k == "fanmode" and v in ("STANDARD", "CONTEST", "BROADCAST"):
                        PGXL["fanmode"] = v
                    if k == "meffa" and v in ("AUTO", "OFF"):
                        PGXL["meffa"] = "STANDBY" if v == "AUTO" else "OFF"
                push = f"S0|state={PGXL['state']} fanmode={PGXL['fanmode']} meffa={PGXL['meffa']}"
            reply = "0|"
            conn.sendall(f"R{seq}|{reply}\n".encode())
            conn.sendall((push + "\n").encode())
            continue
        else:
            reply = "50000015|"
        conn.sendall(f"R{seq}|{reply}\n".encode())


def tgxl_handler(conn, peer, args):
    log("TGXL client", peer[0])
    TGXL_CLIENTS.append(conn)
    try:
        for line in handshake(conn, "1.2.17", args.auth, "auth OK", "auth ", peer[0], args.radio, args.ask_lan):
            if not line.startswith("C"):
                continue
            seq, _, cmd = line[1:].partition("|")
            if cmd != "status":
                log("TGXL <-", cmd)
            words = dict(kv.partition("=")[::2] for kv in cmd.split()[1:])
            if cmd == "info":
                # As 4O3A's protocol description: "info ..." with 3way=1 on the
                # three-antenna model (absent on the SO2R one).
                conn.sendall(f"R{seq}|0|info serial=TGXL-0001 version=1.2.17 nickname=Tuner_Genius_XL"
                             f"{' 3way=1' if args.tgxl_3way else ''}\n".encode())
            elif cmd == "ifconf read":
                conn.sendall(f"R{seq}|0|ifconf dhcp=1 ip={conn.getsockname()[0]} netmask=255.255.255.0 gateway=0.0.0.0\n".encode())
            elif cmd == "status":
                with LOCK:
                    conn.sendall((tgxl_status(seq) + "\n").encode())
            elif cmd.startswith("tune "):
                # 4O3A numbers the relays 1 = C1, 2 = L, 3 = C2.
                name = {"1": "relayC1", "2": "relayL", "3": "relayC2"}.get(words.get("relay"))
                if name and words.get("move") in ("1", "-1"):
                    with LOCK:
                        TGXL[name] = max(0, min(255, TGXL[name] + int(words["move"])))
                        state = tgxl_state()
                    conn.sendall(f"R{seq}|0|\n".encode())
                    push_tgxl("S0|" + state)
                else:
                    conn.sendall(f"R{seq}|50000013|\n".encode())
            elif cmd in ("operate set=0", "operate set=1", "bypass set=0", "bypass set=1"):
                # What the radio relays from a client's "tgxl set handle=<h>
                # mode=" / "bypass=" (seen from a FLEX-6600M).
                with LOCK:
                    TGXL["operate" if cmd.startswith("operate") else "bypass"] = int(cmd[-1])
                    state = tgxl_state()
                conn.sendall(f"R{seq}|0|\n".encode())
                push_tgxl("S0|" + state)
            elif cmd.startswith("activate "):
                ant = words.get("ant", "")
                if args.tgxl_3way and ant in ("1", "2", "3"):
                    with LOCK:
                        TGXL["antA"] = int(ant) - 1
                        state = tgxl_state()
                    conn.sendall(f"R{seq}|0|\n".encode())
                    push_tgxl("S0|" + state)
                else:
                    conn.sendall(f"R{seq}|50000013|\n".encode())
            elif cmd == "autotune":
                conn.sendall(f"R{seq}|0|\n".encode())
                key = RADIO_SEND.get("TGXL") if args.radio_tune else None
                with LOCK:
                    if TGXL["tuning"]:
                        TGXL["tuning"] = 0
                        push_tgxl("S0|" + tgxl_state())
                        push_tgxl("M|Tune aborted")
                        if key:
                            key("transmit tune 0")
                        continue
                    TGXL["tuning"] = 1
                    TGXL_TUNE_END[0] = time.time() + 3
                    push_tgxl("S0|" + tgxl_state())
                # The real tuner keys the radio's tune carrier (at the radio's
                # tune power) while it searches: --radio-tune does the same.
                if key:
                    key("transmit tune 1")

                def finish():
                    try:
                        time.sleep(3)
                        with LOCK:
                            if not TGXL["tuning"]:
                                return
                            TGXL.update(tuning=0, relayC1=52, relayL=17, relayC2=6)
                            push_tgxl("S0|" + tgxl_state())
                        push_tgxl("M|Tuned SWR: 1.13:1")
                    finally:
                        if key:
                            key("transmit tune 0")
                threading.Thread(target=finish, daemon=True).start()
            else:
                conn.sendall(f"R{seq}|50000015|\n".encode())
    finally:
        TGXL_CLIENTS.remove(conn)


def radio_link(args, name, create):
    # One connection a device: the radio keeps one amplifier a client
    # connection (a second create overwrites the first).
    def log(*a):
        print(time.strftime("%H:%M:%S"), *[str(x).replace("RADIO", "RADIO " + name, 1) for x in a], flush=True)

    sock = socket.create_connection((args.radio, 4992), timeout=10)
    sock.settimeout(None)
    own = args.advertise or sock.getsockname()[0]
    log("RADIO connected", args.radio, "as", own)

    seq = [0]
    pending = {}

    def send(cmd):
        with LOCK:
            seq[0] += 1
            n = seq[0]
            pending[n] = cmd
        log("RADIO <-", cmd)
        sock.sendall(f"C{n}|{cmd}\n".encode())

    RADIO_SEND[name] = send

    def pinger():
        while True:
            time.sleep(5)
            try:
                with LOCK:
                    seq[0] += 1
                    n = seq[0]
                sock.sendall(f"C{n}|ping\n".encode())
            except OSError:
                return

    # The PGXL's five meters, as 4O3A's amplifier-to-radio API lists them;
    # their values go to the radio's VITA-49 port as the FLEX metering
    # protocol has them (PCC 0x8002, meter id and value per 32-bit word).
    meters = {}          # name -> (meter id, stream id)
    meter_specs = [("FWD", "30.0", "63.01", "DBM"), ("RL", "34.0", "60.0", "DB"),
                   ("DRV", "10.0", "50.00", "DBM"), ("ID", "0.0", "70.0", "AMPS"),
                   ("TEMP", "0.0", "100.0", "TEMP_C")]
    keyed = [False]      # the radio transmitting (its interlock status)
    my_handle = [0]      # this connection's client handle (the H line)

    def meter_values():
        operate = PGXL["state"] != "STANDBY"
        if keyed[0] and operate:
            return {"FWD": 54.77, "RL": 25.0, "DRV": 41.76, "ID": 12.0, "TEMP": 33.0}  # 300 W from 15 W
        return {"FWD": 0.0, "RL": 34.0, "DRV": 41.76 if keyed[0] else 10.0, "ID": 0.0, "TEMP": 31.0}

    def scale(unit, v):
        if unit in ("DB", "DBM"):
            return int(round(v * 128)) & 0xFFFF
        if unit == "AMPS":
            return int(round(v * 256)) & 0xFFFF
        if unit in ("TEMPC", "TEMP_C"):
            return int(round(v * 64)) & 0xFFFF
        return int(v) & 0xFFFF

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("", 0))

    def meter_sender():
        import struct
        count = 0
        while True:
            time.sleep(0.1)
            if not meters:
                continue
            vals = meter_values()
            by_stream = {}
            for nm, unit in ((m[0], m[3]) for m in meter_specs):
                if nm in meters:
                    mid, sid = meters[nm]
                    by_stream.setdefault(sid, []).append((mid << 16) | scale(unit, vals[nm]))
            for sid, words in by_stream.items():
                size = 4 + len(words)
                hdr = (0x3 << 28) | (1 << 27) | ((count & 0xF) << 16) | size
                pkt = struct.pack(">IIII", hdr, sid, 0x001C2D, (0x534C << 16) | 0x8002)
                pkt += b"".join(struct.pack(">I", w) for w in words)
                try:
                    udp.sendto(pkt, (args.radio, 4991))
                except OSError:
                    pass
            count += 1

    def register():
        send("sub amplifier all")
        send(create.format(ip=own))
        send("keepalive enable")
        if name == "PGXL" and args.radio_meters:
            # The radio takes a client's VITA-49 from the UDP port it named.
            send(f"client udpport {udp.getsockname()[1]}")
            for nm, lo, hi, unit in meter_specs:
                send(f"meter create name={nm} type=AMP min={lo} max={hi} units={unit}")
            threading.Thread(target=meter_sender, daemon=True).start()
        threading.Thread(target=pinger, daemon=True).start()

    registered = False
    raw = open(args.radio_raw + "." + name, "a", buffering=1) if args.radio_raw else None
    for line in lines(sock):
        if raw:
            raw.write(time.strftime("%H:%M:%S ") + line + "\n")
        if line.startswith("H") and not registered:
            registered = True
            log("RADIO handle", line[1:])
            try:
                my_handle[0] = int(line[1:], 16)
            except ValueError:
                pass
            threading.Thread(target=register, daemon=True).start()
        elif line.startswith("R"):
            n, _, rest = line[1:].partition("|")
            cmd = pending.pop(int(n), None) if n.isdigit() else None
            if cmd is not None and cmd.startswith("meter create") and rest.startswith("0|"):
                # R<seq>|0|<meter number>,<stream id>
                # The FLEX-6600M (protocol 1.4.0.0) answers the number alone,
                # and lists the meter with num=<this client's handle>: that
                # handle is the stream id then.
                try:
                    mid, _, sid = rest[2:].partition(",")
                    meters[cmd.split("name=")[1].split()[0]] = (int(mid), int(sid, 16) if sid else my_handle[0])
                except ValueError:
                    pass
            if cmd is not None:
                log("RADIO ->", line, "(" + cmd.split(" ip=")[0] + ")")
            elif not rest.startswith("0|"):
                log("RADIO ->", line)
        elif line.startswith("V"):
            log("RADIO version", line[1:])
        elif any(w in line for w in ("amplifier", "tgxl", "interlock", "|M", "message")) and "|slice " not in line:
            if "interlock" in line and " state=" in line:
                keyed[0] = " state=TRANSMITTING" in line
            log("RADIO ->", line[:400])
    log("RADIO link closed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--pgxl-port", type=int, default=9008)
    ap.add_argument("--tgxl-port", type=int, default=9010)
    ap.add_argument("--auth", default="", help="ask for this remote code (AUTH greeting)")
    ap.add_argument("--ask-lan", action="store_true", help="with --auth, ask the mock's own LAN too (a test from this PC)")
    ap.add_argument("--transmit", action="store_true", help="report the amplifier keyed at 1 kW")
    ap.add_argument("--radio", default="", help="register the stand-ins with this FLEX radio (TCP 4992)")
    ap.add_argument("--radio-tgxl", action="store_true", help="with --radio, register the TGXL too")
    ap.add_argument("--radio-raw", default="", help="with --radio, append every line the radio sends to this file")
    ap.add_argument("--radio-meters", action="store_true",
                    help="with --radio, the PGXL creates its five meters on the radio and streams their values (FWD, RL, DRV, ID, TEMP)")
    ap.add_argument("--radio-tune", action="store_true",
                    help="with --radio-tgxl, key the radio's tune carrier for the 3 s autotune, as the real tuner (transmits!)")
    ap.add_argument("--tgxl-3way", action="store_true", help="the TGXL is the 3-antenna (1x3) model: info says 3way=1")
    ap.add_argument("--advertise", default="", help="with --radio, the address the radio is told (default: ours toward it)")
    args = ap.parse_args()
    if args.radio:
        threading.Thread(target=radio_link, daemon=True, args=(args, "PGXL",
            "amplifier create ip={ip} port=%d model=PowerGeniusXL serial_num=MOCK-PGXL ant=ANT1:PORTA" % args.pgxl_port)).start()
        if args.radio_tgxl:
            threading.Thread(target=radio_link, daemon=True, args=(args, "TGXL",
                "amplifier create ip={ip} port=%d model=TunerGeniusXL serial_num=MOCK-TGXL ant=ANT1" % args.tgxl_port)).start()
    threading.Thread(target=serve, args=(args.pgxl_port, args.bind, lambda c, p: pgxl_handler(c, p, args)), daemon=True).start()
    serve(args.tgxl_port, args.bind, lambda c, p: tgxl_handler(c, p, args))


if __name__ == "__main__":
    main()
