#!/usr/bin/env python3
"""A FlexRadio's discovery announcements for a test of "On this network".

    discovery-announce.py RADIO_IP [SECONDS] [MODEL] [VERSION]

Once a second to this machine's UDP 4992, where the app's discovery listens: the radio then shows on the Connect
page as found on the network, and its card connects to RADIO_IP (the real radio, reached over a VPN or a route
where its own broadcasts do not arrive). When the announcements end, the app drops the radio after five seconds.
The serial number is made up.
"""
import socket
import sys
import time

if len(sys.argv) < 2:
    sys.exit(__doc__)
ip = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 5
model = sys.argv[3] if len(sys.argv) > 3 else 'FLEX-6600M'
version = sys.argv[4] if len(sys.argv) > 4 else '4.3.16.0'
text = ('discovery_protocol_version=3.1.0.2 model=%s serial=0000-TEST-0001 version=%s nickname= callsign= '
        'ip=%s port=4992 status=Available inuse=0 mf_enable=1 licensed_clients=2 available_clients=1 '
        'max_panadapters=4 available_panadapters=4 max_slices=4 available_slices=4' % (model, version, ip)).encode()
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
end = time.time() + seconds
count = 0
while time.time() < end:
    sock.sendto(text, ('127.0.0.1', 4992))
    count += 1
    time.sleep(1.0)
print('announced %d times' % count)
