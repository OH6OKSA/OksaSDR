#!/usr/bin/env bash
# An IPv6-only test network on this Linux machine, as an iPhone meets one on
# mobile data (OksaSDR roadmap 106): a network namespace with IPv6 only, a
# NAT64 (nat64-test.py on the TUN device made here) and a DNS64
# (dns64-test.py), both run by the user, so the app inside reaches IPv4 hosts
# only through 64:ff9b::/96, as through a carrier's NAT64. No packages.
#
#   sudo mobile/tools/ipv6only-testnet.sh up     # make it
#   sudo mobile/tools/ipv6only-testnet.sh down   # remove it, settings back
#   sudo mobile/tools/ipv6only-testnet.sh status
#
# After up, as the user: mobile/tools/nat64-test.py and
# mobile/tools/dns64-test.py, and programs inside the namespace through its
# tmux server (no sudo):
#   tmux -L v6only new-window -d 'COMMAND > LOG 2>&1'
#
# Changes while up: IPv4 and IPv6 forwarding on, an nft table ip oksa_nat64
# (masquerade of the NAT64 pool out of $WAN), an nft table ip6 oksa_dns64
# (the namespace's DNS, port 53 on v6h, to dns64-test.py's port 5300), ufw
# route rules between v6h, nat64 and $WAN and port 5300 in on v6h,
# /etc/netns/v6only/resolv.conf. down takes each back (forwarding to what it
# was).
set -euo pipefail

NS=v6only
WAN=${WAN:-$(ip -4 route show default | awk '{print $5; exit}')}
OWNER=${SUDO_USER:-$(id -un)}
POOL=192.168.255.0/24
PREFIX=64:ff9b::/96
STATE=/run/oksa-v6only.state
DNSPORT=5300

[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }

ufw_rules() { # add|delete
    local op=$1
    local d=()
    [ "$op" = delete ] && d=(delete)
    ufw route "${d[@]}" allow in on v6h out on nat64 >/dev/null
    ufw route "${d[@]}" allow in on nat64 out on v6h >/dev/null
    ufw route "${d[@]}" allow in on nat64 out on "$WAN" >/dev/null
    ufw "${d[@]}" allow in on v6h to any port "$DNSPORT" proto udp >/dev/null
}

case "${1:-}" in
up)
    if ip netns list | grep -qw "$NS"; then
        echo "already up (down first)" >&2
        exit 1
    fi
    [ -n "$WAN" ] || { echo "no default IPv4 route: set WAN=" >&2; exit 1; }
    printf 'ip_forward=%s\nv6_forwarding=%s\nwan=%s\n' \
        "$(cat /proc/sys/net/ipv4/ip_forward)" "$(cat /proc/sys/net/ipv6/conf/all/forwarding)" "$WAN" > "$STATE"

    ip netns add "$NS"
    ip link add v6h type veth peer name v6n
    ip link set v6n netns "$NS"
    ip link set v6h mtu 1520 up
    ip -6 addr add fd64:64::1/64 dev v6h nodad
    ip netns exec "$NS" ip link set lo up
    ip netns exec "$NS" ip link set v6n mtu 1520 up
    ip netns exec "$NS" ip -6 addr add fd64:64::2/64 dev v6n nodad
    ip netns exec "$NS" ip -6 route add default via fd64:64::1

    ip tuntap add dev nat64 mode tun user "$OWNER"
    ip link set nat64 mtu 1520 up
    ip addr add 192.168.255.1/24 dev nat64
    ip -6 route add "$PREFIX" dev nat64

    sysctl -q -w net.ipv4.ip_forward=1 net.ipv6.conf.all.forwarding=1
    nft add table ip oksa_nat64
    nft add chain ip oksa_nat64 post '{ type nat hook postrouting priority srcnat; }'
    nft add rule ip oksa_nat64 post ip saddr "$POOL" oifname "$WAN" masquerade
    nft add table ip6 oksa_dns64
    nft add chain ip6 oksa_dns64 pre '{ type nat hook prerouting priority dstnat; }'
    nft add rule ip6 oksa_dns64 pre iifname v6h udp dport 53 redirect to :"$DNSPORT"
    ufw_rules add

    mkdir -p /etc/netns/"$NS"
    echo 'nameserver fd64:64::1' > /etc/netns/"$NS"/resolv.conf

    ip netns exec "$NS" sudo -H -u "$OWNER" env XDG_RUNTIME_DIR=/run/user/"$(id -u "$OWNER")" \
        tmux -L v6only new-session -d -s v6only
    echo "up: namespace $NS (fd64:64::2, IPv6 only), NAT64 $PREFIX on tun nat64 -> $WAN, DNS fd64:64::1:53 -> :$DNSPORT"
    echo "next, as $OWNER: mobile/tools/nat64-test.py and mobile/tools/dns64-test.py"
    ;;
down)
    wan=$WAN; v4=0; v6=0
    if [ -f "$STATE" ]; then
        # shellcheck disable=SC1090
        . "$STATE"
        v4=$ip_forward; v6=$v6_forwarding; WAN=$wan
    fi
    sudo -u "$OWNER" tmux -L v6only kill-server 2>/dev/null || true
    ufw_rules delete || true
    nft delete table ip oksa_nat64 2>/dev/null || true
    nft delete table ip6 oksa_dns64 2>/dev/null || true
    ip link del nat64 2>/dev/null || true
    ip link del v6h 2>/dev/null || true
    ip netns del "$NS" 2>/dev/null || true
    rm -rf /etc/netns/"$NS"
    sysctl -q -w net.ipv4.ip_forward="$v4" net.ipv6.conf.all.forwarding="$v6"
    rm -f "$STATE"
    echo "down: forwarding back to IPv4 $v4 / IPv6 $v6"
    ;;
status)
    ip netns list | grep -w "$NS" || echo "no namespace"
    ip -br link show nat64 2>/dev/null || echo "no tun"
    nft list table ip oksa_nat64 2>/dev/null | grep masquerade || echo "no masquerade"
    nft list table ip6 oksa_dns64 2>/dev/null | grep redirect || echo "no DNS redirect"
    ufw status | grep -E 'v6h|nat64' || echo "no ufw rules"
    ip netns exec "$NS" ip -br addr 2>/dev/null || true
    ;;
*)
    echo "usage: sudo $0 up|down|status" >&2
    exit 2
    ;;
esac
