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
answers info and status, the radio's relayed operate set= / bypass set=, steps its relays (tune relay=R move=D), switches
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
closes). No interlock and no meters are created, so the radio's transmit
is not gated on the stand-ins. Every line the radio sends about them is
printed ("RADIO ->").
"""

import argparse
import socket
import threading
import time

LOCK = threading.Lock()
PGXL = {"state": "STANDBY", "fanmode": "STANDARD", "meffa": "STANDBY"}
TGXL = {"operate": 1, "tuning": 0, "relayC1": 44, "relayL": 12, "relayC2": 8, "antA": 0, "bypass": 0}
TGXL_CLIENTS = []


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def pgxl_status(transmit):
    s = dict(PGXL)
    if transmit:
        s.update(state="TRANSMIT_A", fwd="60.0", swr="-26.0", id="38.5", vdd="51.9")
    else:
        s.update(fwd="30.0", swr="-60.0", id="0.0", vdd="0.0" if s["state"] == "STANDBY" else "51.8")
    return ("state={state} bandA=20 bandB=0 bsrcA=FLEX bsrcB=FLEX flexA=FLEX-8600 flexB=FLEX-8600 vac=245 "
            "vdd={vdd} id={id} peakid={id} fwd={fwd} peakfwd={fwd} swr={swr} temp=31.4 cntfreq=0 cat1freq=0 "
            "cat2freq=0 hltemp=29.8 biasA=RADIO_AAB biasB=RADIO_AB fanmode={fanmode} meffa={meffa}").format(**s)


def tgxl_state():
    return ("state bypassA={bypass} bypassRxA=0 antA={antA} bypassB=0 bypassRxB=0 antB=0 state={operate} "
            "tuning={tuning} bypass={bypass} relayC1={relayC1} relayL={relayL} relayC2={relayC2}").format(**TGXL)


def tgxl_status(seq):
    return ("S{seq}|status fwd=21.42 peak=21.42 max=62.43 swr=-60.0000 pttA=0 bandA=6 modeA=1 flexA=FLEX-8600 "
            "freqA=14161.500 bypassA={bypass} bypassRxA=0 antA={antA} pttB=0 bandB=0 modeB=0 flexB=FLEX-8600 "
            "freqB=0.000 bypassB=0 bypassRxB=0 antB=0 state={operate} active=1 tuning={tuning} bypass={bypass} ag=0 "
            "relayC1={relayC1} relayL={relayL} relayC2={relayC2}").format(seq=seq, **TGXL)


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


def handshake(conn, version, auth, ok_reply, auth_prefix, peer=None, radio=""):
    # The real devices ask a client from outside their network, never the
    # paired radio: with --radio, the radio's connection is not asked.
    if not auth or (radio and peer == radio):
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
    conn.sendall(f"R{seq}|5000002D|\n".encode())
    conn.close()
    return iter(())


def pgxl_handler(conn, peer, args):
    log("PGXL client", peer[0])
    for line in handshake(conn, "3.8.9", args.auth, "Authorized", "auth code=", peer[0], args.radio):
        if not line.startswith("C"):
            continue
        seq, _, cmd = line[1:].partition("|")
        if cmd != "status":
            log("PGXL <-", cmd)
        if cmd == "info":
            reply = "0|serial=10-200/24-0203  version=3.8.9 protocol=1.0 mains=240"
        elif cmd == "setup read":
            reply = "0|ledintens=141 txdelay=16 inactivity-timeout=0 nickname=PowerGeniusXL authcode="
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
        for line in handshake(conn, "1.2.17", args.auth, "auth OK", "auth ", peer[0], args.radio):
            if not line.startswith("C"):
                continue
            seq, _, cmd = line[1:].partition("|")
            if cmd != "status":
                log("TGXL <-", cmd)
            words = dict(kv.partition("=")[::2] for kv in cmd.split()[1:])
            if cmd == "info":
                conn.sendall(f"R{seq}|0|serial=TGXL-0001 version=1.2.17\n".encode())
            elif cmd == "status":
                with LOCK:
                    conn.sendall((tgxl_status(seq) + "\n").encode())
            elif cmd.startswith("tune "):
                name = {"0": "relayC1", "1": "relayL", "2": "relayC2"}.get(words.get("relay"))
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
                if ant in ("1", "2", "3"):
                    with LOCK:
                        TGXL["antA"] = int(ant) - 1
                        state = tgxl_state()
                    conn.sendall(f"R{seq}|0|\n".encode())
                    push_tgxl("S0|" + state)
                else:
                    conn.sendall(f"R{seq}|50000013|\n".encode())
            elif cmd == "autotune":
                conn.sendall(f"R{seq}|0|\n".encode())
                with LOCK:
                    if TGXL["tuning"]:
                        TGXL["tuning"] = 0
                        push_tgxl("S0|" + tgxl_state())
                        push_tgxl("M|Tune aborted")
                        continue
                    TGXL["tuning"] = 1
                    push_tgxl("S0|" + tgxl_state())

                def finish():
                    time.sleep(3)
                    with LOCK:
                        if not TGXL["tuning"]:
                            return
                        TGXL.update(tuning=0, relayC1=52, relayL=17, relayC2=6)
                        push_tgxl("S0|" + tgxl_state())
                    push_tgxl("M|Tuned SWR: 1.13:1")
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

    def register():
        send("sub amplifier all")
        send(create.format(ip=own))
        send("keepalive enable")
        threading.Thread(target=pinger, daemon=True).start()

    registered = False
    raw = open(args.radio_raw + "." + name, "a", buffering=1) if args.radio_raw else None
    for line in lines(sock):
        if raw:
            raw.write(time.strftime("%H:%M:%S ") + line + "\n")
        if line.startswith("H") and not registered:
            registered = True
            log("RADIO handle", line[1:])
            threading.Thread(target=register, daemon=True).start()
        elif line.startswith("R"):
            n, _, rest = line[1:].partition("|")
            cmd = pending.pop(int(n), None) if n.isdigit() else None
            if cmd is not None:
                log("RADIO ->", line, "(" + cmd.split(" ip=")[0] + ")")
            elif not rest.startswith("0|"):
                log("RADIO ->", line)
        elif line.startswith("V"):
            log("RADIO version", line[1:])
        elif any(w in line for w in ("amplifier", "tgxl", "interlock", "|M", "message")) and "|slice " not in line:
            log("RADIO ->", line[:400])
    log("RADIO link closed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--pgxl-port", type=int, default=9008)
    ap.add_argument("--tgxl-port", type=int, default=9010)
    ap.add_argument("--auth", default="", help="ask for this remote code (AUTH greeting)")
    ap.add_argument("--transmit", action="store_true", help="report the amplifier keyed at 1 kW")
    ap.add_argument("--radio", default="", help="register the stand-ins with this FLEX radio (TCP 4992)")
    ap.add_argument("--radio-tgxl", action="store_true", help="with --radio, register the TGXL too")
    ap.add_argument("--radio-raw", default="", help="with --radio, append every line the radio sends to this file")
    ap.add_argument("--advertise", default="", help="with --radio, the address the radio is told (default: ours toward it)")
    args = ap.parse_args()
    if args.radio:
        threading.Thread(target=radio_link, daemon=True, args=(args, "PGXL",
            "amplifier create ip={ip} port=%d model=PowerGeniusXL serial_num=MOCK-PGXL ant=ANT1:PORTA" % args.pgxl_port)).start()
        if args.radio_tgxl:
            threading.Thread(target=radio_link, daemon=True, args=(args, "TGXL",
                "amplifier create ip={ip} port=%d model=TunerGeniusXL serial_num=MOCK-TGXL ant=ANT1:PORTA" % args.tgxl_port)).start()
    threading.Thread(target=serve, args=(args.pgxl_port, args.bind, lambda c, p: pgxl_handler(c, p, args)), daemon=True).start()
    serve(args.tgxl_port, args.bind, lambda c, p: tgxl_handler(c, p, args))


if __name__ == "__main__":
    main()
