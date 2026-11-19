#!/bin/sh
# Runs a program as on an IPv6-only network (OksaSDR roadmap 106): started
# inside ipv6only-testnet.sh's namespace (through its tmux server), with host
# names from the test network's DNS64 (resolv.conf) instead of
# systemd-resolved's socket, which answers from the host's own network.
# No root: a user and mount namespace of its own for the nsswitch.conf.
#
#   tmux -L v6only new-window -d "cd ~/AetherSDR && mobile/tools/v6only-run.sh COMMAND ARGS > LOG 2>&1"
#
# With nat64-test.py and dns64-test.py --no-a running (--no-a: no IPv4
# addresses, as iOS hides them on an IPv6-only network). The dev build takes
# the radio's TLS through the NAT64 with OKSASDR_TEST_NAT64=64:ff9b::.
set -eu
conf=${XDG_RUNTIME_DIR:-/tmp}/v6only-nsswitch.conf
sed -E 's/^hosts:.*/hosts: files dns/' /etc/nsswitch.conf > "$conf"
exec unshare --map-root-user --mount sh -c 'mount --bind "$0" /etc/nsswitch.conf && exec "$@"' "$conf" "$@"
