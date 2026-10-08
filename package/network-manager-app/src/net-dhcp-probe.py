#!/usr/bin/env python3
"""net-dhcp-probe.py - is there a DHCP server on this port?

One DHCPDISCOVER on one interface, then every DHCPOFFER that answers within
the timeout, one line per server:

    OFFER server=192.168.1.1 offered=192.168.1.57 router=192.168.1.1
    DONE servers=1

It never sends a DHCPREQUEST, so no address is taken from anyone. Needs root.
Works while NetworkManager's own DHCP client runs on the same port (plan
2.1). Standard library only.

The DISCOVER goes out as a whole Ethernet frame from IP 0.0.0.0, as a DHCP
client sends it, and the answers are read from the link (AF_PACKET), not
through the IP stack: the DHCP guard probes a port that already carries its
server address (192.168.10.1 in DHCP-server mode), and a DISCOVER sent from
that foreign address over a UDP socket went unanswered by a home router that
answers a proper one - the guard then let the port serve on a LAN that had a
DHCP server (rig 1, 2026-10-08). Reading the link also means no IP-layer
filter can hide an offer.

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


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF


def frame(mac, payload):
    """payload as a broadcast UDP 68 -> 67 from 0.0.0.0, in an Ethernet frame."""
    src, dst = b"\0" * 4, b"\xff" * 4
    udp_len = 8 + len(payload)
    pseudo = src + dst + struct.pack("!BBH", 0, 17, udp_len)
    udp = struct.pack("!HHHH", 68, 67, udp_len, 0) + payload
    udp = udp[:6] + struct.pack("!H", checksum(pseudo + udp) or 0xFFFF) + udp[8:]
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + udp_len, 0, 0, 64, 17, 0, src, dst)
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    return b"\xff" * 6 + mac + b"\x08\x00" + ip + udp


def bootp_of(raw):
    """The BOOTP payload of a UDP 67 -> 68 IPv4 frame, else None."""
    if len(raw) < 14 + 20 + 8 or raw[12:14] != b"\x08\x00":
        return None
    ip = raw[14:]
    ihl = (ip[0] & 15) * 4
    if ip[0] >> 4 != 4 or ip[9] != 17 or len(ip) < ihl + 8:
        return None
    sport, dport, length = struct.unpack("!HHH", ip[ihl:ihl + 6])
    if sport != 67 or dport != 68:
        return None
    return ip[ihl + 8:ihl + length]


def probe(iface, timeout):
    xid = random.getrandbits(32)
    mac = mac_of(iface)
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.ntohs(0x0800))
    s.bind((iface, 0))
    s.send(frame(mac, discover(xid, mac)))
    seen = {}
    end = time.monotonic() + timeout
    while True:
        left = end - time.monotonic()
        if left <= 0:
            break
        r, _, _ = select.select([s], [], [], left)
        if not r:
            break
        data = bootp_of(s.recv(4096))
        offer = parse_offer(data, xid) if data else None
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
    # the frame: from 0.0.0.0 to the broadcast address, valid checksums, and
    # the link reader takes the BOOTP payload back out of it
    mac_ = bytes.fromhex("2ccf674f66ca")
    f = frame(mac_, d)
    assert f[:6] == b"\xff" * 6 and f[6:12] == mac_ and f[12:14] == b"\x08\x00", "ethernet"
    assert f[26:30] == b"\0" * 4 and f[30:34] == b"\xff" * 4, "from 0.0.0.0 to broadcast"
    assert checksum(f[14:34]) == 0, "ip checksum"
    assert struct.unpack("!HH", f[34:38]) == (68, 67), "ports"
    pseudo = f[26:34] + struct.pack("!BBH", 0, 17, len(f) - 34)
    assert checksum(pseudo + f[34:]) == 0, "udp checksum"
    reply = bytearray(f)
    reply[34:38] = struct.pack("!HH", 67, 68)
    assert bootp_of(bytes(reply)) == d, "the payload back"
    assert bootp_of(f) is None, "our own discover is not a reply"
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
        print("ERROR needs root (a raw packet socket)", file=sys.stderr)
        return 2
    try:
        return probe(a.iface, a.timeout)
    except OSError as e:
        print("ERROR %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
