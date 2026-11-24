#!/usr/bin/env python3
"""A plain command client for a FlexRadio's TCP port, for protocol experiments.

    flexcmd.py HOST [--show REGEX] [--settle SECONDS] COMMAND [COMMAND ...]

Sends each COMMAND as C<n>|COMMAND and prints the replies (R<n>|code|body) and the status lines that match
REGEX. "wait N" among the commands pauses N seconds. It takes no GUI client slot, so it runs beside the app.
Serial numbers, radio and client ids and GPS fields are never printed.

What it found (the developer's FLEX-6600M, SmartSDR 4.3.16, 2026-11-24): outside FM the radio answers
"slice set N repeater_offset_dir=", "fm_tone_mode=" and "fm_tone_value=" with 50000061 and applies them all the
same; "fm_repeater_offset_freq=" and "tx_offset_freq=" answer 0.
"""
import re
import select
import socket
import sys
import time


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith('-'):
        sys.exit(__doc__)
    host = args.pop(0)
    show = None
    settle = 1.2
    while args and args[0].startswith('--'):
        if args[0] == '--show' and len(args) > 1:
            show = re.compile(args[1])
            args = args[2:]
        elif args[0] == '--settle' and len(args) > 1:
            settle = float(args[1])
            args = args[2:]
        else:
            sys.exit('unknown option ' + args[0])
    sock = socket.create_connection((host, 4992), timeout=6)
    buf = b''

    def redact(line):
        return re.sub(r'(serial|radio_id|chassis_serial|client_id|gps[a-z_]*)=\S+', r'\1=<redacted>', line)

    def pump(seconds):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            readable, _, _ = select.select([sock], [], [], 0.1)
            if not readable:
                continue
            data = sock.recv(65536)
            if not data:
                return
            buf += data
            while b'\n' in buf:
                raw, buf = buf.split(b'\n', 1)
                line = raw.decode('utf-8', 'replace').strip()
                if line.startswith('R') or (show and show.search(line)):
                    print(redact(line)[:420])

    pump(1.0)
    seq = 0
    for command in args:
        if command.startswith('wait '):
            pump(float(command[5:]))
            continue
        seq += 1
        print('>>> C%d|%s' % (seq, command))
        sock.sendall(('C%d|%s\n' % (seq, command)).encode())
        pump(settle)
    sock.close()


if __name__ == '__main__':
    main()
