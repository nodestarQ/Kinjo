"""Run: python -m unittest -v"""

import json
import unittest
from pathlib import Path

from cryptography.exceptions import InvalidTag

import gen_vectors
import kinjo as k

VECTORS = Path(__file__).resolve().parent.parent / "test-vectors"


def load(name):
    return json.loads((VECTORS / name).read_text())


class VectorsUpToDate(unittest.TestCase):
    def test_files_match_generator(self):
        for name, fn in [("header.json", gen_vectors.header_vectors),
                         ("fragment.json", gen_vectors.fragment_vectors),
                         ("keys.json", gen_vectors.key_vectors),
                         ("sealed.json", gen_vectors.sealed_vectors),
                         ("drawing.json", gen_vectors.drawing_vectors),
                         ("identity.json", gen_vectors.identity_vectors),
                         ("cobs.json", gen_vectors.cobs_vectors),
                         ("provision.json", gen_vectors.provision_vectors)]:
            self.assertEqual(load(name), json.loads(json.dumps(fn())), name)


class HeaderTest(unittest.TestCase):
    def test_roundtrip_and_size(self):
        for c in load("header.json")["cases"]:
            raw = bytes.fromhex(c["header"])
            self.assertEqual(len(raw), k.HEADER_SIZE)
            self.assertEqual(k.Header.decode(raw).encode(), raw)
            self.assertEqual(len(bytes.fromhex(c["ad"])), 16)

    def test_ad_ignores_ttl_and_frag_index(self):
        a = k.Header(type=k.TYPE_SEALED, message_id=1, source=2, destination=3, frag_index=0, frag_count=4, ttl=4)
        b = k.Header(type=k.TYPE_SEALED, message_id=1, source=2, destination=3, frag_index=3, frag_count=4, ttl=1)
        self.assertEqual(a.ad(), b.ad())


class FragmentTest(unittest.TestCase):
    def test_reassembly_any_order(self):
        for c in load("fragment.json")["cases"]:
            packets = [bytes.fromhex(p) for p in c["packets"]]
            self.assertTrue(all(len(p) <= 250 for p in packets))
            r = k.Reassembler()
            result = None
            for p in reversed(packets):
                result = r.push(p, now=0.0) or result
            self.assertEqual(result[1].hex(), c["body"])

    def test_timeout_drops_partial(self):
        packets = k.fragment(k.TYPE_SEALED, 9, 1, 2, bytes(600))
        r = k.Reassembler()
        self.assertIsNone(r.push(packets[0], now=0.0))
        self.assertIsNone(r.push(packets[1], now=1.0))
        self.assertIsNone(r.push(packets[2], now=10.0))  # first two expired

    def test_body_limits(self):
        with self.assertRaises(k.ProtocolError):
            k.fragment(k.TYPE_SEALED, 1, 1, 2, b"")
        with self.assertRaises(k.ProtocolError):
            k.fragment(k.TYPE_SEALED, 1, 1, 2, bytes(k.MAX_BODY + 1))


class ForwardingTest(unittest.TestCase):
    def test_seen_cache_and_ttl(self):
        p = k.fragment(k.TYPE_SEALED, 5, 1, 2, b"x", ttl=2)[0]
        seen = k.SeenCache()
        h, _ = k.parse_packet(p)
        self.assertFalse(seen.check_and_add(h))
        self.assertTrue(seen.check_and_add(h))
        p1 = k.forward(p)
        self.assertEqual(p1[3], 1)
        self.assertIsNone(k.forward(p1))


class CryptoTest(unittest.TestCase):
    def setUp(self):
        self.keys = load("keys.json")

    def test_both_sides_derive_same_key(self):
        hp = bytes.fromhex(self.keys["handheld"]["public"])
        lp = bytes.fromhex(self.keys["laptop"]["public"])
        a = k.derive_key(bytes.fromhex(self.keys["handheld"]["private"]), lp)
        b = k.derive_key(bytes.fromhex(self.keys["laptop"]["private"]), hp)
        self.assertEqual(a, b)
        self.assertEqual(a.hex(), self.keys["key"])

    def test_node_id_is_first_four_bytes_on_the_wire(self):
        pub = bytes.fromhex(self.keys["handheld"]["public"])
        h = k.Header(type=k.TYPE_SEALED, message_id=0, source=k.node_id(pub), destination=0,
                     frag_index=0, frag_count=1)
        self.assertEqual(h.encode()[8:12], pub[:4])

    def test_sealed_vectors_open(self):
        for c in load("sealed.json")["cases"]:
            key = bytes.fromhex(c["key"])
            r = k.Reassembler()
            result = None
            for p in c["packets"]:
                result = r.push(bytes.fromhex(p), now=0.0) or result
            self.assertEqual(k.open_sealed(key, *result).hex(), c["plaintext"])

    def test_relay_can_lower_ttl(self):
        c = load("sealed.json")["cases"][0]
        p = k.forward(bytes.fromhex(c["packets"][0]))
        h, body = k.parse_packet(p)
        self.assertEqual(k.open_sealed(bytes.fromhex(c["key"]), h, body).hex(), c["plaintext"])

    def test_tampering_fails(self):
        c = load("sealed.json")["cases"][0]
        key = bytes.fromhex(c["key"])
        raw = bytes.fromhex(c["packets"][0])
        for offset in (1, 5, 9, 13, 17, 20, len(raw) - 1):  # type, id, src, dst, count, nonce, tag
            bad = bytearray(raw)
            bad[offset] ^= 0x01
            with self.assertRaises((InvalidTag, k.ProtocolError)):
                h, body = k.parse_packet(bytes(bad))
                k.open_sealed(key, h, body)


class PayloadTest(unittest.TestCase):
    def test_drawing_steps_fit_i8_and_keep_endpoints(self):
        for c in load("drawing.json")["cases"]:
            decoded = [[tuple(p) for p in s] for s in c["decoded"]]
            self.assertEqual(k.decode_drawing(bytes.fromhex(c["plaintext"])), decoded)
            self.assertEqual(decoded[0][0], tuple(c["strokes"][0][0]))
            self.assertEqual(decoded[-1][-1], tuple(c["strokes"][-1][-1]))

    def test_drawing_colors(self):
        for c in load("drawing.json")["cases"]:
            got = k.decode_drawing_colored(bytes.fromhex(c["plaintext"]))
            self.assertEqual([color for color, _ in got], c["decoded_colors"])
        with self.assertRaises(k.ProtocolError):
            k.encode_drawing([[(1, 1)]], [8])
        with self.assertRaises(k.ProtocolError):
            k.decode_drawing_colored(bytes([k.KIND_DRAWING, 0x00, 9]))

    def test_notes(self):
        for c in load("drawing.json")["notes"]:
            text, colored = k.decode_note(bytes.fromhex(c["plaintext"]))
            self.assertEqual(text, c["text"])
            self.assertEqual([[list(p) for p in s] for _, s in colored], c["decoded"])
        with self.assertRaises(k.ProtocolError):
            k.decode_note(bytes([k.KIND_NOTE, 5]) + b"ab")

    def test_drawing_rejects_off_canvas(self):
        with self.assertRaises(k.ProtocolError):
            k.encode_drawing([[(320, 0)]])

    def test_identity(self):
        v = load("identity.json")
        self.assertEqual(k.parse_identity(bytes.fromhex(v["body"])), (bytes.fromhex(v["public"]), v["name"]))

    def test_text_limit(self):
        with self.assertRaises(k.ProtocolError):
            k.text_plaintext("a" * 201)


class CobsTest(unittest.TestCase):
    def test_roundtrip_no_zeros(self):
        v = load("cobs.json")
        for c in v["cases"] + v["decode_only"]:
            enc = bytes.fromhex(c["encoded"])
            self.assertNotIn(0, enc)
            self.assertEqual(k.cobs_decode(enc).hex(), c["raw"])

    def test_serial_frame(self):
        f = load("cobs.json")["serial_frame"]
        wire = bytes.fromhex(f["wire"])
        self.assertEqual(wire[-1], 0)
        self.assertEqual(k.parse_serial_frame(wire[:-1]), (f["frame_type"], bytes.fromhex(f["body"])))

    def test_rejects_truncated(self):
        with self.assertRaises(k.ProtocolError):
            k.cobs_decode(b"\x05\x11\x22")


class ProvisionTest(unittest.TestCase):
    def setUp(self):
        self.v = load("provision.json")

    def test_requests_roundtrip(self):
        for r in self.v["requests"]:
            cmd, args = k.parse_provision_request(bytes.fromhex(r["body"]))
            self.assertEqual(cmd, r["command"])
            got = {a: (v.hex() if isinstance(v, bytes) else v) for a, v in args.items()}
            self.assertEqual(got, r["args"], r["label"])
            wire = bytes.fromhex(r["wire"])
            self.assertEqual(k.parse_serial_frame(wire[:-1]), (k.FRAME_PROVISION, bytes.fromhex(r["body"])))

    def test_replies_and_info(self):
        for r in self.v["replies"]:
            self.assertEqual(k.parse_provision_reply(bytes.fromhex(r["body"])),
                             (r["command"], r["status"], bytes.fromhex(r["data"])))
        for i in self.v["infos"]:
            info = k.parse_info(bytes.fromhex(i["data"]))
            self.assertEqual((info["pub"].hex(), info["version"], info["contacts"], info["blocks"], info["name"]),
                             (i["pub"], i["version"], i["contacts"], i["blocks"], i["name"]))

    def test_malformed_rejected(self):
        for m in self.v["malformed"]:
            with self.assertRaises(k.ProtocolError, msg=m["label"]):
                k.parse_provision_request(bytes.fromhex(m["body"]))
        with self.assertRaises(k.ProtocolError):
            k.parse_provision_request(bytes.fromhex(self.v["unknown_command"]))


if __name__ == "__main__":
    unittest.main()
