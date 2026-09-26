"""Writes protocol/test-vectors/*.json. Run: python gen_vectors.py"""

import json
from pathlib import Path

import kinjo as k

OUT = Path(__file__).resolve().parent.parent / "test-vectors"

# Fixed keys, test only.
HANDHELD_PRIV = bytes(range(1, 33))
LAPTOP_PRIV = bytes(range(33, 65))
HANDHELD_NAME = "handheld.alice.kinjo.eth"
LAPTOP_NAME = "laptop.alice.kinjo.eth"


def hx(b: bytes) -> str:
    return b.hex()


def header_vectors():
    cases = [
        k.Header(type=k.TYPE_SEALED, message_id=0x12345678, source=0xA1B2C3D4,
                 destination=0x0A0B0C0D, frag_index=0, frag_count=1),
        k.Header(type=k.TYPE_IDENTITY, message_id=0xDEADBEEF, source=0x01020304,
                 destination=k.BROADCAST, frag_index=2, frag_count=3, ttl=1),
    ]
    return {"cases": [{
        "fields": {"version": h.version, "type": h.type, "flags": h.flags, "ttl": h.ttl,
                   "message_id": h.message_id, "source": h.source, "destination": h.destination,
                   "frag_index": h.frag_index, "frag_count": h.frag_count},
        "header": hx(h.encode()),
        "ad": hx(h.ad()),
    } for h in cases]}


def fragment_vectors():
    cases = []
    for n in (1, 232, 233, 600, k.MAX_BODY):
        body = bytes(i % 251 for i in range(n))
        cases.append({
            "type": k.TYPE_SEALED, "message_id": 0x0000CAFE, "source": 0x11111111,
            "destination": 0x22222222, "ttl": k.DEFAULT_TTL, "body": hx(body),
            "packets": [hx(p) for p in k.fragment(k.TYPE_SEALED, 0xCAFE, 0x11111111, 0x22222222, body)],
        })
    return {"cases": cases}


def key_vectors():
    hp, lp = k.public_key(HANDHELD_PRIV), k.public_key(LAPTOP_PRIV)
    key = k.derive_key(HANDHELD_PRIV, lp)
    assert key == k.derive_key(LAPTOP_PRIV, hp)
    return {
        "handheld": {"private": hx(HANDHELD_PRIV), "public": hx(hp), "node_id": k.node_id(hp),
                     "name": HANDHELD_NAME},
        "laptop": {"private": hx(LAPTOP_PRIV), "public": hx(lp), "node_id": k.node_id(lp),
                   "name": LAPTOP_NAME},
        "shared_secret": hx(k.shared_secret(HANDHELD_PRIV, lp)),
        "hkdf_info": hx(b"kinjo/0.1" + min(hp, lp) + max(hp, lp)),
        "key": hx(key),
    }


def sealed_vectors():
    hp, lp = k.public_key(HANDHELD_PRIV), k.public_key(LAPTOP_PRIV)
    key = k.derive_key(HANDHELD_PRIV, lp)
    src, dst = k.node_id(hp), k.node_id(lp)
    long_stroke = [(10 + (i * 3) % 300, 20 + (i * 7) % 200) for i in range(400)]
    plaintexts = [
        ("text", k.text_plaintext("gm Tokyo")),
        ("drawing_multi_fragment", k.encode_drawing([long_stroke])),
    ]
    cases = []
    for i, (label, pt) in enumerate(plaintexts):
        nonce = bytes([0xA0 + i]) * k.NONCE_SIZE
        mid = 0x1000 + i
        packets = k.seal(key, src, dst, mid, pt, nonce)
        cases.append({"label": label, "key": hx(key), "source": src, "destination": dst,
                      "message_id": mid, "ttl": k.DEFAULT_TTL, "nonce": hx(nonce),
                      "plaintext": hx(pt), "packets": [hx(p) for p in packets]})
    return {"cases": cases}


def drawing_vectors():
    cases = [
        [[(0, 0)]],
        [[(10, 10), (12, 11), (15, 13), (19, 12)], [(100, 100), (101, 99)]],
        [[(0, 0), (319, 239)]],                      # big jump, gets intermediate points
        [[(i % 320, (i * 2) % 240) for i in range(300)]],  # long stroke, gets split
    ]
    out = []
    for strokes in cases:
        enc = k.encode_drawing(strokes)
        out.append({"strokes": strokes, "plaintext": hx(enc),
                    "decoded": [[list(p) for p in s] for s in k.decode_drawing(enc)]})
    return {"cases": out}


def identity_vectors():
    hp = k.public_key(HANDHELD_PRIV)
    body = k.identity_body(hp, HANDHELD_NAME)
    return {"public": hx(hp), "name": HANDHELD_NAME, "body": hx(body),
            "packets": [hx(p) for p in k.fragment(k.TYPE_IDENTITY, 0x77, k.node_id(hp), k.BROADCAST, body)]}


def cobs_vectors():
    raw = [b"", b"\x00", b"\x00\x00", b"\x11\x22\x00\x33", b"\x11\x22\x33\x44",
           bytes(range(1, 255)), bytes(range(1, 256)), bytes([0]) + bytes(range(1, 256))]
    cases = [{"raw": hx(r), "encoded": hx(k.cobs_encode(r))} for r in raw]
    # Same data, other valid encoding (no trailing 0x01 after a full block). Decode only.
    decode_only = [{"raw": hx(bytes(range(1, 255))), "encoded": hx(b"\xff" + bytes(range(1, 255)))}]
    frame = {"frame_type": k.FRAME_LOG, "body": hx(b"relay up"),
             "wire": hx(k.serial_frame(k.FRAME_LOG, b"relay up"))}
    return {"cases": cases, "decode_only": decode_only, "serial_frame": frame}


def provision_vectors():
    hp = k.public_key(HANDHELD_PRIV)
    lp = k.public_key(LAPTOP_PRIV)
    requests = [
        ("info", k.CMD_INFO, {}),
        ("set_name", k.CMD_SET_NAME, {"name": HANDHELD_NAME}),
        ("clear_name", k.CMD_SET_NAME, {"name": ""}),
        ("add_contact_verified", k.CMD_ADD_CONTACT, {"flags": k.FLAG_VERIFIED, "pub": lp, "name": LAPTOP_NAME}),
        ("clear_contacts", k.CMD_CLEAR_CONTACTS, {}),
        ("wipe", k.CMD_WIPE, {}),
    ]
    req_out = []
    for label, cmd, args in requests:
        body = k.provision_request(cmd, **args)
        req_out.append({"label": label, "command": cmd,
                        "args": {a: (v.hex() if isinstance(v, bytes) else v) for a, v in args.items()},
                        "body": hx(body), "wire": hx(k.serial_frame(k.FRAME_PROVISION, body))})
    info_new = k.info_data(hp, 0, 0, "")
    info_named = k.info_data(hp, 1, 3, HANDHELD_NAME)
    replies = [
        ("info_new_device", k.CMD_INFO, k.STATUS_OK, info_new),
        ("info_provisioned", k.CMD_INFO, k.STATUS_OK, info_named),
        ("set_name_ok", k.CMD_SET_NAME, k.STATUS_OK, b""),
        ("add_contact_full", k.CMD_ADD_CONTACT, k.STATUS_FULL, b""),
        ("unknown_command", 0x7F, k.STATUS_UNKNOWN_COMMAND, b""),
    ]
    rep_out = [{"label": label, "command": cmd, "status": st, "data": hx(data),
                "body": hx(k.provision_reply(cmd, st, data)),
                "wire": hx(k.serial_frame(k.FRAME_PROVISION_REPLY, k.provision_reply(cmd, st, data)))}
               for label, cmd, st, data in replies]
    infos = [{"data": hx(d), "pub": hx(hp), "version": 1, "contacts": c, "blocks": b, "name": n}
             for d, c, b, n in ((info_new, 0, 0, ""), (info_named, 1, 3, HANDHELD_NAME))]
    malformed = [
        ("empty", b""),
        ("name_too_long", bytes([k.CMD_SET_NAME, 65]) + b"a" * 65),
        ("name_length_past_end", bytes([k.CMD_SET_NAME, 10]) + b"abc"),
        ("contact_short_key", bytes([k.CMD_ADD_CONTACT, 0]) + bytes(10)),
        ("trailing_bytes", bytes([k.CMD_INFO, 0x00])),
    ]
    return {"requests": req_out, "replies": rep_out, "infos": infos,
            "malformed": [{"label": l, "body": hx(b)} for l, b in malformed],
            "unknown_command": hx(bytes([0x7F]))}


def main():
    OUT.mkdir(exist_ok=True)
    files = {
        "header.json": header_vectors(),
        "fragment.json": fragment_vectors(),
        "keys.json": key_vectors(),
        "sealed.json": sealed_vectors(),
        "drawing.json": drawing_vectors(),
        "identity.json": identity_vectors(),
        "cobs.json": cobs_vectors(),
        "provision.json": provision_vectors(),
    }
    for name, data in files.items():
        (OUT / name).write_text(json.dumps(data, indent=2) + "\n")
        print("wrote", OUT / name)


if __name__ == "__main__":
    main()
