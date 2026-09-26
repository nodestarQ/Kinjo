"""Shows the serial frames of a Kinjo board and can send test packets.

  python monitor.py /dev/cu.usbmodem1101              # print LOG and RADIO_RX frames
  python monitor.py /dev/cu.usbmodem1101 --send-test  # also send a sealed test packet every 2 s (bridge only)

Needs pyserial and cryptography (see requirements.txt).
"""

import argparse
import os
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "protocol" / "reference"))
import kinjo as k  # noqa: E402

# Test keys from protocol/test-vectors/keys.json. Never use on a real device.
HANDHELD_PRIV = bytes(range(1, 33))
LAPTOP_PRIV = bytes(range(33, 65))


def describe(packet: bytes) -> str:
    try:
        h, frag = k.parse_packet(packet)
    except k.ProtocolError as e:
        return f"invalid packet ({e}): {packet.hex()}"
    kind = {k.TYPE_SEALED: "SEALED", k.TYPE_IDENTITY: "IDENTITY"}.get(h.type, f"type 0x{h.type:02x}")
    return (f"{kind} id={h.message_id:08x} {h.source:08x} -> {h.destination:08x} ttl={h.ttl} "
            f"frag {h.frag_index + 1}/{h.frag_count} {len(frag)} B: {frag[:24].hex()}...")


def test_packets(counter: int) -> list[bytes]:
    """A sealed text from the laptop test key to the handheld test key."""
    lp, hp = k.public_key(LAPTOP_PRIV), k.public_key(HANDHELD_PRIV)
    key = k.derive_key(LAPTOP_PRIV, hp)
    pt = k.text_plaintext(f"test {counter}")
    return k.seal(key, k.node_id(lp), k.node_id(hp), int.from_bytes(os.urandom(4), "little"), pt, os.urandom(12))


def open_port(path: str) -> serial.Serial:
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = False  # keep the DevKitC from resetting on open
    s.rts = False
    s.open()
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--send-test", action="store_true")
    args = ap.parse_args()

    port = open_port(args.port)
    buf = bytearray()
    counter, next_send = 0, time.monotonic()
    print(f"listening on {args.port}, ctrl+c to stop")
    try:
        while True:
            if args.send_test and time.monotonic() >= next_send:
                counter += 1
                for p in test_packets(counter):
                    port.write(k.serial_frame(k.FRAME_RADIO_TX, p))
                    print(f"TX   {describe(p)}")
                next_send = time.monotonic() + 2

            buf += port.read(512)
            while b"\x00" in buf:
                raw, _, rest = bytes(buf).partition(b"\x00")
                buf = bytearray(rest)
                if not raw:
                    continue
                try:
                    ftype, body = k.parse_serial_frame(raw)
                except k.ProtocolError:
                    print(f"?    {raw!r}")  # boot messages from the ESP32 ROM land here
                    continue
                if ftype == k.FRAME_LOG:
                    print(f"LOG  {body.decode(errors='replace')}")
                elif ftype == k.FRAME_RADIO_RX and len(body) >= 7:
                    mac = ":".join(f"{b:02x}" for b in body[:6])
                    print(f"RX   from {mac} {describe(body[7:])}")
                else:
                    print(f"0x{ftype:02x} {body.hex()}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
