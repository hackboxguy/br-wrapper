#!/usr/bin/env python3
"""net-dhcp-probe.py - is there a DHCP server on this port?

One DHCPDISCOVER on one interface, then every DHCPOFFER that answers within
the timeout, one line per server:

    OFFER server=192.168.1.1 offered=192.168.1.57 router=192.168.1.1
    DONE servers=1

It never sends a DHCPREQUEST, so no address is taken from anyone. Needs root
(UDP port 68, SO_BINDTODEVICE); works while NetworkManager's own DHCP client
runs on the same port (plan 2.1). Standard library only.

    net-dhcp-probe.py --iface eth0 [--timeout 3]
    net-dhcp-probe.py --self-test        packet code only, no network
"""
import argparse
import os
import random
import select
import socket
import struct
import sys
import time

MAGIC = b"\x63\x82\x53\x63"


def mac_of(iface):
    with open(f"/sys/class/net/{iface}/address") as f:
        return bytes(int(x, 16) for x in f.read().strip().split(":"))


def discover(xid, mac):
    """A BOOTP request with the broadcast flag, DHCP message type DISCOVER."""
    pkt = struct.pack("!BBBBIHH4s4s4s4s16s64s128s",
                      1, 1, 6, 0, xid, 0, 0x8000,
                      b"\0" * 4, b"\0" * 4, b"\0" * 4, b"\0" * 4,
                      mac.ljust(16, b"\0"), b"\0" * 64, b"\0" * 128)
    opts = MAGIC
    opts += bytes([53, 1, 1])                     # DHCPDISCOVER
    opts += bytes([55, 4, 1, 3, 6, 54])           # ask for mask, router, DNS, server id
    opts += bytes([255])
    return (pkt + opts).ljust(300, b"\0")


def parse_offer(data, xid):
    """(server, offered, router) of a DHCPOFFER for xid, else None."""
    if len(data) < 240 or data[0] != 2 or data[236:240] != MAGIC:
        return None
    if struct.unpack("!I", data[4:8])[0] != xid:
        return None
    offered = socket.inet_ntoa(data[16:20])
    opts, i = {}, 240
    while i < len(data):
        code = data[i]
        if code == 255:
            break
        if code == 0:
            i += 1
            continue
        if i + 1 >= len(data):
            break
        n = data[i + 1]
        opts[code] = data[i + 2:i + 2 + n]
        i += 2 + n
    if opts.get(53) != b"\x02":                   # DHCPOFFER only
        return None
    server = socket.inet_ntoa(opts[54][:4]) if len(opts.get(54, b"")) >= 4 else socket.inet_ntoa(data[20:24])
    router = socket.inet_ntoa(opts[3][:4]) if len(opts.get(3, b"")) >= 4 else ""
    return server, offered, router


def probe(iface, timeout):
    xid = random.getrandbits(32)
    mac = mac_of(iface)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, iface.encode() + b"\0")
    s.bind(("", 68))
    s.sendto(discover(xid, mac), ("255.255.255.255", 67))
    seen = {}
    end = time.monotonic() + timeout
    while True:
        left = end - time.monotonic()
        if left <= 0:
            break
        r, _, _ = select.select([s], [], [], left)
        if not r:
            break
        data, _ = s.recvfrom(4096)
        offer = parse_offer(data, xid)
        if offer and offer[0] not in seen:
            seen[offer[0]] = offer
            print("OFFER server=%s offered=%s%s" % (offer[0], offer[1], " router=" + offer[2] if offer[2] else ""),
                  flush=True)
    print("DONE servers=%d" % len(seen), flush=True)
    return 0


def self_test():
    xid = 0x12345678
    mac = bytes.fromhex("2ccf674f66ca")
    d = discover(xid, mac)
    assert d[0] == 1 and d[236:240] == MAGIC and bytes([53, 1, 1]) in d and d[28:34] == mac, "discover"
    # an offer as a server sends it: yiaddr, server id, router
    head = bytearray(d[:240])
    head[0] = 2
    head[16:20] = socket.inet_aton("192.168.1.57")
    opts = bytes([53, 1, 2, 54, 4]) + socket.inet_aton("192.168.1.1") + bytes([3, 4]) \
        + socket.inet_aton("192.168.1.254") + bytes([0, 255])
    offer = bytes(head) + opts
    assert parse_offer(offer, xid) == ("192.168.1.1", "192.168.1.57", "192.168.1.254"), parse_offer(offer, xid)
    assert parse_offer(offer, xid + 1) is None, "other xid"
    ack = bytes(head) + bytes([53, 1, 5, 54, 4]) + socket.inet_aton("192.168.1.1") + bytes([255])
    assert parse_offer(ack, xid) is None, "an ACK is not an offer"
    noroute = bytes(head) + bytes([53, 1, 2, 54, 4]) + socket.inet_aton("10.0.0.1") + bytes([255])
    assert parse_offer(noroute, xid) == ("10.0.0.1", "192.168.1.57", ""), "no router option"
    assert parse_offer(b"\0" * 10, xid) is None, "short"
    print("self-test: PASS")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--iface")
    ap.add_argument("--timeout", type=float, default=4.5)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.iface:
        ap.error("--iface is required")
    if os.geteuid() != 0:
        print("ERROR needs root (UDP port 68)", file=sys.stderr)
        return 2
    try:
        return probe(a.iface, a.timeout)
    except OSError as e:
        print("ERROR %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
