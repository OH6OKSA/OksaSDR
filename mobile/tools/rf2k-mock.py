#!/usr/bin/env python3
"""A stand-in RF-KIT RF2K-S for testing the app's Amp tab without one.

    mobile/tools/rf2k-mock.py [--port 8080] [--bind 0.0.0.0] [--name NAME]
                              [--tx-cycle S | --keyed] [--forward W] [--swr X]
                              [--operate] [--error TEXT] [--status TEXT]
                              [--slow MS] [--drop-after N] [--not-rfkit]
                              [--log FILE]

Answers the RF-KIT REST API (OpenAPI 0.9.0, the shapes upstream's
RfkitProtocol parses and a real RF2K-S sends: {"antennas":[...]} for the
antenna list): GET /info, /power, /data, /tuner, /antennas, /antennas/active,
/operate-mode, /operational-interface. With --tx-cycle S the readings key
and unkey every S seconds (forward up to --forward W at --swr, temperature
and current following); --keyed transmits all the time (the manual's
picture), else it stays in receive.

Every request is printed as "METHOD /path"; a PUT or POST (a command that
would change the amplifier) is answered as the API does but printed as
"WRITE", and the summary at the end (Ctrl-C, or --log FILE written as it
goes) counts them: the app's read-only step must leave it at 0.

Failure cases for the app's link handling: --slow MS delays every answer,
--drop-after N closes the listening socket after N requests (the amplifier
switched off), --not-rfkit answers /info with a web page (a router at that
address).
"""
import argparse
import json
import math
import signal
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

START = time.monotonic()
COUNTS = {"GET": 0, "WRITE": 0}
LOCK = threading.Lock()


def keyed(args) -> float:
    """0..1: how far into a transmission the cycle is (0 = receive)."""
    if args.keyed:
        return 0.93
    if not args.tx_cycle:
        return 0.0
    t = (time.monotonic() - START) % (2 * args.tx_cycle)
    if t < args.tx_cycle:
        return 0.0
    # A voice-like envelope while keyed.
    return 0.55 + 0.45 * abs(math.sin(t * 2.1))


def documents(args, state):
    k = keyed(args)
    fwd = round(args.forward * k)
    swr = args.swr if fwd >= 1 else 1.0
    refl = round(fwd * ((swr - 1) / (swr + 1)) ** 2)
    temp = 32.0 + 14.0 * k + (time.monotonic() - START) / 600.0
    return {
        "/info": {"device": "RF2K-S", "software_version": {"GUI": 200, "controller": 267},
                  "custom_device_name": args.name},
        "/power": {"temperature": {"value": round(temp, 1), "unit": "°C"},
                   "voltage": {"value": round(53.2 - 1.6 * k, 1), "unit": "V"},
                   "current": {"value": round(0.4 + 24.0 * k, 1), "unit": "A"},
                   "forward": {"value": fwd, "max_value": 1500, "unit": "W"},
                   "reflected": {"value": refl, "max_value": 300, "unit": "W"},
                   "swr": {"value": round(swr, 2), "max_value": 10, "unit": ""}},
        "/data": {"band": {"value": 20, "unit": "m"}, "frequency": {"value": 14074, "unit": "kHz"},
                  "status": args.status},
        "/tuner": {"mode": "AUTO", "setup": "CL", "L": {"value": 120, "unit": "nH"},
                   "C": {"value": 47, "unit": "pF"}},
        "/antennas": {"antennas": [
            {"type": "INTERNAL", "number": 1, "state": "ACTIVE" if state["antenna"] == 1 else "AVAILABLE"},
            {"type": "INTERNAL", "number": 2, "state": "ACTIVE" if state["antenna"] == 2 else "AVAILABLE"},
            {"type": "INTERNAL", "number": 3, "state": "DISABLED"},
            {"type": "INTERNAL", "number": 4, "state": "DISABLED"},
            {"type": "EXTERNAL", "state": "AVAILABLE"}]},
        "/antennas/active": {"type": "INTERNAL", "number": state["antenna"]},
        "/operate-mode": {"operate_mode": "OPERATE" if state["operate"] else "STANDBY"},
        "/operational-interface": {"operational_interface": "CAT", **({"error": args.error} if args.error else {})},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--name", default="Shack")
    ap.add_argument("--tx-cycle", type=float, default=0.0, help="key / unkey every S seconds")
    ap.add_argument("--keyed", action="store_true", help="transmitting all the time")
    ap.add_argument("--forward", type=int, default=750)
    ap.add_argument("--swr", type=float, default=1.3)
    ap.add_argument("--operate", action="store_true")
    ap.add_argument("--error", default="")
    ap.add_argument("--status", default="OK")
    ap.add_argument("--slow", type=int, default=0, help="ms before each answer")
    ap.add_argument("--drop-after", type=int, default=0)
    ap.add_argument("--not-rfkit", action="store_true")
    ap.add_argument("--log", help="append every request here")
    args = ap.parse_args()
    state = {"operate": args.operate, "antenna": 1}
    log = open(args.log, "a", buffering=1) if args.log else None
    server_box = {}

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):  # our own line per request instead
            pass

        def answer(self, status, body, ctype="application/json"):
            if args.slow:
                time.sleep(args.slow / 1000.0)
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def note(self, kind):
            with LOCK:
                COUNTS[kind] += 1
                total = COUNTS["GET"] + COUNTS["WRITE"]
            line = f"{time.strftime('%H:%M:%S')} {'WRITE ' if kind == 'WRITE' else ''}{self.command} {self.path}"
            print(line, flush=True)
            if log:
                log.write(line + "\n")
            if args.drop_after and total >= args.drop_after and "srv" in server_box:
                print("-- dropping: the amplifier goes away", flush=True)
                threading.Thread(target=server_box["srv"].shutdown, daemon=True).start()

        def do_GET(self):
            self.note("GET")
            if args.not_rfkit and self.path == "/info":
                self.answer(200, b"<html><body>Router login</body></html>", "text/html")
                return
            doc = documents(args, state).get(self.path)
            if doc is None:
                self.answer(404, {"status": "error", "reason": "no such endpoint"})
            else:
                self.answer(200, doc)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            return json.loads(self.rfile.read(n) or b"{}") if n else {}

        def do_PUT(self):
            self.note("WRITE")
            req = self.body()
            if self.path == "/operate-mode":
                state["operate"] = req.get("operate_mode") == "OPERATE"
                self.answer(200, documents(args, state)["/operate-mode"])
            elif self.path == "/antennas/active":
                if req.get("type") == "INTERNAL" and req.get("number") in (1, 2):
                    state["antenna"] = req["number"]
                    self.answer(200, documents(args, state)["/antennas/active"])
                else:
                    self.answer(409, {"status": "error", "reason": "antenna not available"})
            else:
                self.answer(404, {"status": "error", "reason": "no such endpoint"})

        def do_POST(self):
            self.note("WRITE")
            self.body()
            self.answer(200, {} if self.path == "/error/reset" else {"status": "error"})

    srv = ThreadingHTTPServer((args.bind, args.port), Handler)
    server_box["srv"] = srv
    print(f"RF2K-S stand-in on {args.bind}:{args.port}"
          f"{' (keying every %gs)' % args.tx_cycle if args.tx_cycle else ''}", flush=True)
    def stop(*_):  # a kill as Ctrl-C: the summary still prints
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    print(f"-- {COUNTS['GET']} reads, {COUNTS['WRITE']} writes", flush=True)
    if log:
        log.write(f"-- {COUNTS['GET']} reads, {COUNTS['WRITE']} writes\n")
    return 0 if COUNTS["WRITE"] == 0 else 3


if __name__ == "__main__":
    sys.exit(main())
