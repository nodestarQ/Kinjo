"""Sends one USB provisioning command to a handheld (SPEC §13) and prints the reply.

  python provision.py PORT info
  python provision.py PORT set-name handheld.alice.kinjo.eth
  python provision.py PORT add-contact laptop.alice.kinjo.eth <public key hex> [--verified]
  python provision.py PORT add-test-laptop          # the laptop test key from keys.json
  python provision.py PORT clear-contacts
  python provision.py PORT wipe
  python provision.py PORT send laptop.alice.kinjo.eth "gm Tokyo"   # SEND_TEXT, for testing
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "protocol" / "reference"))
import kinjo as k  # noqa: E402
from monitor import LAPTOP_PRIV, open_port  # noqa: E402

FRAME_SEND_TEXT = 0x06
STATUS = {0: "OK", 1: "malformed", 2: "full", 3: "unknown command"}


def read_frames(port, timeout: float):
    buf, end = bytearray(), time.monotonic() + timeout
    while time.monotonic() < end:
        buf += port.read(256)
        while b"\x00" in buf:
            raw, _, rest = bytes(buf).partition(b"\x00")
            buf = bytearray(rest)
            try:
                yield k.parse_serial_frame(raw)
            except k.ProtocolError:
                pass  # boot text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("command", choices=["info", "set-name", "add-contact", "add-test-laptop", "clear-contacts",
                                        "wipe", "send"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--verified", action="store_true")
    a = ap.parse_args()

    if a.command == "send":
        name, text = a.args
        body = k.encode_name(name) + text.encode()
        frame = k.serial_frame(FRAME_SEND_TEXT, body)
    else:
        if a.command == "info":
            req = k.provision_request(k.CMD_INFO)
        elif a.command == "set-name":
            req = k.provision_request(k.CMD_SET_NAME, name=a.args[0] if a.args else "")
        elif a.command == "add-contact":
            req = k.provision_request(k.CMD_ADD_CONTACT, name=a.args[0], pub=bytes.fromhex(a.args[1].removeprefix("0x")),
                                      flags=k.FLAG_VERIFIED if a.verified else 0)
        elif a.command == "add-test-laptop":
            req = k.provision_request(k.CMD_ADD_CONTACT, name="laptop.alice.kinjo.eth",
                                      pub=k.public_key(LAPTOP_PRIV), flags=k.FLAG_VERIFIED)
        elif a.command == "clear-contacts":
            req = k.provision_request(k.CMD_CLEAR_CONTACTS)
        else:
            req = k.provision_request(k.CMD_WIPE)
        frame = k.serial_frame(k.FRAME_PROVISION, req)

    port = open_port(a.port)
    time.sleep(1.5)  # opening the port resets a XIAO, let it boot
    port.reset_input_buffer()
    port.write(frame)
    for ftype, body in read_frames(port, 2.0):
        if ftype == k.FRAME_LOG:
            print("LOG ", body.decode(errors="replace"))
        elif ftype == k.FRAME_PROVISION_REPLY:
            cmd, status, data = k.parse_provision_reply(body)
            print(f"reply to 0x{cmd:02x}: {STATUS.get(status, status)}")
            if data:
                info = k.parse_info(data)
                print(f"  public key {info['pub'].hex()}")
                print(f"  node id    {k.node_id(info['pub']):08x}")
                print(f"  name       {info['name'] or '(none)'}")
                print(f"  contacts   {info['contacts']}, blocks {info['blocks']}")
            if a.command != "send":
                break


if __name__ == "__main__":
    main()
