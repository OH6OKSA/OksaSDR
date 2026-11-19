#!/usr/bin/env python3
"""Stand-ins for an Elecraft KPA500 and KAT500 behind a raw TCP serial bridge.

As Sergio Landoni LU7YS's Raspberry Pi Pico W gives them (OksaSDR roadmap 12):
each device's serial port on a TCP port of its own, one client at a time (a
second connection is closed at once). The protocols as the devices' own
references give them (KPA500 Programmer's Reference Rev. A2: ^CMD[data];
frames; KAT500 Serial Command Reference: CMD[data]; frames) and as
upstream's Kpa500Connection / Kat500Model read them: the stand-ins answer
every query those send, take every SET the app sends, and answer no SET
(the client reads back).

    elecraft-mock.py [--kpa500 2000] [--kat500 2001] [--host 0.0.0.0]
                     [--tune-ms 3000] [--nomatch-on N] [--keyed]

Commands on stdin (one a line), for tests:
    kpa fault NN | kpa clear | kpa tx WATTS SWR | kpa rx | kpa off | kpa on
    kat fault N | kat nomatch | kat match | kat swr X.XX
    status
"""

import argparse
import asyncio
import sys
import time


def log(*args):
    print(time.strftime("%H:%M:%S"), *args, flush=True)


class Kpa500:
    name = "kpa500"

    def __init__(self):
        self.operate = 0
        self.power = 1
        self.band = 5
        self.fwd = 0
        self.swr = 0      # 000 when not transmitting
        self.temp = 32
        self.volts = 602  # 60.2 V
        self.amps = 5     # 0.5 A
        self.fault = 0
        self.fan = 2
        self.speaker = 1

    def handle(self, frame: str):
        """frame without the terminating ';' ("^OS1", "^WS", "", "P")."""
        if frame == "":
            return ";"
        if frame == "P":
            self.power = 1
            log("kpa500: woken (P)")
            return None
        if not frame.startswith("^"):
            return None
        cmd, arg = frame[1:3], frame[3:]
        if frame.startswith("^RVM"):
            return "^RVM01.54;"
        if not self.power and cmd != "ON":
            return None  # off: silent
        if cmd == "OS":
            if arg == "":
                return "^OS%d;" % self.operate
            if arg in ("0", "1"):
                if arg == "1" and self.fault:
                    log("kpa500: OPERATE refused, fault %02d" % self.fault)
                else:
                    self.operate = int(arg)
                    log("kpa500:", "OPERATE" if self.operate else "STANDBY")
            return None
        if cmd == "WS":
            return "^WS%03d %03d;" % (self.fwd, self.swr)
        if cmd == "VI":
            return "^VI%03d %03d;" % (self.volts, self.amps)
        if cmd == "TM":
            return "^TM%03d;" % self.temp
        if cmd == "BN":
            if arg == "":
                return "^BN%02d;" % self.band
            if arg.isdigit() and 0 <= int(arg) <= 10:
                if self.fwd > 0:
                    log("kpa500: band change refused while RF is on")
                else:
                    self.band = int(arg)
                    log("kpa500: band %02d" % self.band)
            return None
        if cmd == "FL":
            if arg == "":
                return "^FL%02d;" % self.fault
            if arg == "C":
                log("kpa500: fault cleared")
                self.fault = 0
            return None
        if cmd == "FC":
            if arg == "":
                return "^FC%d;" % self.fan
            if arg.isdigit() and 0 <= int(arg) <= 6:
                self.fan = int(arg)
                log("kpa500: fan minimum %d" % self.fan)
            return None
        if cmd == "ON":
            if arg == "":
                return "^ON1;" if self.power else None
            if arg == "0":
                self.power = 0
                self.operate = 0
                log("kpa500: powered off")
            return None
        if cmd == "SP":
            if arg == "":
                return "^SP%d;" % self.speaker
            if arg in ("0", "1"):
                self.speaker = int(arg)
            return None
        if cmd == "SN":
            return "^SN01234;"
        log("kpa500: unknown", frame)
        return None

    def command(self, words):
        if words[:1] == ["fault"]:
            self.fault = int(words[1])
            self.operate = 0
        elif words[:1] == ["clear"]:
            self.fault = 0
        elif words[:1] == ["tx"]:
            self.fwd = int(words[1])
            self.swr = int(round(float(words[2]) * 10))
            self.amps = 150
        elif words[:1] == ["rx"]:
            self.fwd, self.swr, self.amps = 0, 0, 5
        elif words[:1] == ["off"]:
            self.power, self.operate = 0, 0
        elif words[:1] == ["on"]:
            self.power = 1
        log("kpa500:", vars(self))


class Kat500:
    name = "kat500"

    def __init__(self, tune_ms, nomatch_on=0):
        self.tune_ms = tune_ms
        self.nomatch_on = nomatch_on
        self.tunes = 0
        self.power = 1
        self.mode = "A"
        self.antenna = 1
        self.bypassed = "N"
        self.tuning = 0
        self.swr = 1.12
        self.bypass_swr = 2.80
        self.freq = 14074
        self.fault = 0
        self.ampi = 0
        self.attn = 0
        self.side = "T"
        self.c = 0x1A
        self.l = 0x0F
        self.enabled = {1: 1, 2: 1, 3: 0}
        self.nomatch = False
        self.writer = None
        self.tune_task = None

    def band(self):
        for lo, hi, b in ((1800, 2000, 0), (3500, 4000, 1), (5250, 5450, 2), (7000, 7300, 3), (10100, 10150, 4),
                          (14000, 14350, 5), (18068, 18168, 6), (21000, 21450, 7), (24890, 24990, 8),
                          (28000, 29700, 9), (50000, 54000, 10)):
            if lo <= self.freq <= hi:
                return b
        return 5

    async def tune(self):
        log("kat500: tuning (full search) for %d ms" % self.tune_ms)
        await asyncio.sleep(self.tune_ms / 1000)
        if not self.tuning:
            return
        self.tuning = 0
        if self.nomatch:
            self.fault = 1
            if self.nomatch_on:
                self.nomatch = False
            log("kat500: no match (FLT1)")
        else:
            self.swr = 1.05
            self.bypassed = "N"
            self.c, self.l = 0x22, 0x11
            log("kat500: tuned, SWR 1.05")
        if self.writer:
            self.writer.write(b"FT;")
            await self.writer.drain()

    def handle(self, frame: str):
        f = frame
        if f == "":
            return ";"
        if f == "I":
            return "KAT500;"
        if f == "RV":
            return "RV02.16;"
        if f == "SN":
            return "SN01234;"
        if f.startswith("PS"):
            if f == "PS":
                return "PS%d;" % self.power
            if f in ("PS0", "PS1"):
                self.power = int(f[2])
            return None
        if not self.power:
            return None
        if f == "TP":
            return "TP%d;" % self.tuning
        if f == "VFWD":
            return "VFWD%d;" % (800 if self.tuning else 0)
        if f == "VRFL":
            return "VRFL%d;" % (120 if self.tuning else 0)
        if f == "VSWR":
            return "VSWR%.2f;" % self.swr
        if f == "VSWRB":
            return "VSWRB%.2f;" % self.bypass_swr
        if f == "F":
            return "F%d;" % self.freq
        if f.startswith("F ") and f[2:].isdigit():
            self.freq = int(f[2:])
            log("kat500: frequency %d kHz (band %02d)" % (self.freq, self.band()))
            return None
        if f == "FC":
            return "FC%d;" % (self.freq - 2)
        if f == "FX":
            return "FX0;"
        if f == "FY":
            lo = self.freq // 100 * 100
            return "FY %d-%d;" % (lo, lo + 99)
        if f == "BN":
            return "BN%02d;" % self.band()
        if f.startswith("AE") and len(f) == 5:
            return "%s%d;" % (f, self.enabled.get(int(f[4]), 0))
        if f.startswith("AN"):
            if f == "AN":
                return "AN%d;" % self.antenna
            if f[2:] in ("1", "2", "3"):
                if self.enabled.get(int(f[2:]), 0):
                    self.antenna = int(f[2:])
                    log("kat500: ANT %d" % self.antenna)
                else:
                    log("kat500: ANT %s not enabled" % f[2:])
            return None
        if f.startswith("MD"):
            if f == "MD":
                return "MD%s;" % self.mode
            if f[2:] in ("A", "M", "B"):
                self.mode = f[2:]
                if self.mode == "B":
                    self.bypassed = "B"
                log("kat500: mode %s" % self.mode)
            return None
        if f == "AMPI":
            return "AMPI%d;" % self.ampi
        if f == "ATTN":
            return "ATTN%d;" % self.attn
        if f.startswith("BYP"):
            if f == "BYP":
                return "BYP%s;" % self.bypassed
            if f[3:] in ("B", "N"):
                self.bypassed = f[3:]
                log("kat500: bypass", "on" if self.bypassed == "B" else "off")
            return None
        if f.startswith("FLT"):
            if f == "FLT":
                return "FLT%d;" % self.fault
            if f == "FLTC":
                self.fault = 0
                log("kat500: fault cleared")
            return None
        if f == "C":
            return "C%02X;" % self.c
        if f == "L":
            return "L%02X;" % self.l
        if f == "SIDE":
            return "SIDE%s;" % self.side
        if f in ("T", "FTNS"):
            if self.mode == "B":
                log("kat500: tune refused in BYPASS mode")
                return None
            self.tuning = 1
            self.fault = 0
            self.tunes += 1
            if self.nomatch_on and self.tunes == self.nomatch_on:
                self.nomatch = True
                log("kat500: tune %d ends with no match" % self.tunes)
            self.tune_task = asyncio.get_event_loop().create_task(self.tune())
            return None
        if f == "CT":
            if self.tuning:
                log("kat500: tune cancelled (CT)")
            self.tuning = 0
            return None
        log("kat500: unknown", frame)
        return None

    def command(self, words):
        if words[:1] == ["fault"]:
            self.fault = int(words[1])
        elif words[:1] == ["nomatch"]:
            self.nomatch = True
        elif words[:1] == ["match"]:
            self.nomatch = False
        elif words[:1] == ["swr"]:
            self.swr = float(words[1])
        log("kat500:", {k: v for k, v in vars(self).items() if k not in ("writer", "tune_task")})


async def serve(device, host, port):
    busy = {"writer": None}

    async def client(reader, writer):
        peer = writer.get_extra_info("peername")
        if busy["writer"] is not None:
            log(device.name + ": second client", peer, "closed (one at a time)")
            writer.close()
            return
        busy["writer"] = writer
        if hasattr(device, "writer"):
            device.writer = writer
        log(device.name + ": client", peer)
        buf = b""
        try:
            while True:
                data = await reader.read(256)
                if not data:
                    break
                buf += data
                while True:
                    # The KPA500's bootloader wake is a lone P with no ';'.
                    if device.name == "kpa500" and buf[:1] == b"P":
                        device.handle("P")
                        buf = buf[1:]
                        continue
                    i = buf.find(b";")
                    if i < 0:
                        break
                    frame, buf = buf[:i].decode(errors="replace").strip(), buf[i + 1:]
                    reply = device.handle(frame)
                    if reply:
                        writer.write(reply.encode())
                await writer.drain()
        except (ConnectionResetError, BrokenPipeError):
            pass
        finally:
            log(device.name + ": client gone")
            busy["writer"] = None
            if hasattr(device, "writer"):
                device.writer = None
            writer.close()

    server = await asyncio.start_server(client, host, port)
    log(device.name + ": on %s:%d" % (host, port))
    return server


async def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--kpa500", type=int, default=2000)
    ap.add_argument("--kat500", type=int, default=2001)
    ap.add_argument("--tune-ms", type=int, default=3000)
    ap.add_argument("--nomatch-on", type=int, default=0, help="the Nth tune (1-based) finds no match")
    ap.add_argument("--keyed", action="store_true", help="readings as while transmitting (pictures)")
    args = ap.parse_args()
    kpa = Kpa500()
    kat = Kat500(args.tune_ms, args.nomatch_on)
    if args.keyed:
        kpa.operate, kpa.fwd, kpa.swr, kpa.amps, kpa.volts, kpa.temp = 1, 480, 12, 152, 571, 46
        kat.swr = 1.15
    servers = [await serve(kpa, args.host, args.kpa500), await serve(kat, args.host, args.kat500)]
    loop = asyncio.get_event_loop()
    reader = asyncio.StreamReader()
    try:
        await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
    except (ValueError, OSError):
        reader = None
    while True:
        if reader is None:
            await asyncio.sleep(3600)
            continue
        line = await reader.readline()
        if not line:
            await asyncio.sleep(3600)
            continue
        words = line.decode().split()
        if words[:1] == ["kpa"]:
            kpa.command(words[1:])
        elif words[:1] == ["kat"]:
            kat.command(words[1:])
        elif words[:1] == ["status"]:
            kpa.command([])
            kat.command([])
    for s in servers:
        s.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
