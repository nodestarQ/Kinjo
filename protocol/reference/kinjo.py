"""Reference implementation of the Kinjo wire format v0.1 (protocol/SPEC.md).

Slow and simple on purpose. Firmware and laptop code are tested against the
vectors this module generates, not against this code directly.
"""

import struct
from dataclasses import dataclass

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

VERSION = 0x01

TYPE_IDENTITY = 0x01
TYPE_SEALED = 0x10

KIND_TEXT = 0x01
KIND_DRAWING = 0x02
KIND_NOTE = 0x03

BROADCAST = 0xFFFFFFFF
DEFAULT_TTL = 4

HEADER_SIZE = 18
FRAGMENT_MAX = 232
MAX_FRAGMENTS = 16
MAX_BODY = FRAGMENT_MAX * MAX_FRAGMENTS

NONCE_SIZE = 12
TAG_SIZE = 16
KEY_SIZE = 32
MAX_TEXT = 200
MAX_NAME = 64

CANVAS_W = 320
CANVAS_H = 240
COLOR_MARKER = 0x00
PALETTE = ["#26313d", "#d8453b", "#e0832f", "#d9b92b", "#2f9e5b", "#3d6fd6", "#8a57d6", "#d4548e"]

REASSEMBLY_SLOTS = 4
REASSEMBLY_TIMEOUT = 5.0
SEEN_CACHE_SIZE = 64

FRAME_RADIO_RX = 0x01
FRAME_RADIO_TX = 0x02
FRAME_LOG = 0x03
FRAME_PROVISION = 0x04
FRAME_PROVISION_REPLY = 0x05

CMD_INFO = 0x01
CMD_SET_NAME = 0x02
CMD_ADD_CONTACT = 0x03
CMD_CLEAR_CONTACTS = 0x04
CMD_WIPE = 0x05

STATUS_OK = 0x00
STATUS_MALFORMED = 0x01
STATUS_FULL = 0x02
STATUS_UNKNOWN_COMMAND = 0x03

FLAG_VERIFIED = 0x01
MAX_CONTACTS = 64
MAX_BLOCKS = 256

_HEADER = struct.Struct("<BBBBIIIBB")


class ProtocolError(ValueError):
    pass


# --- Header (SPEC §5) ---

@dataclass(frozen=True)
class Header:
    type: int
    message_id: int
    source: int
    destination: int
    frag_index: int
    frag_count: int
    ttl: int = DEFAULT_TTL
    flags: int = 0
    version: int = VERSION

    def encode(self) -> bytes:
        return _HEADER.pack(self.version, self.type, self.flags, self.ttl, self.message_id,
                            self.source, self.destination, self.frag_index, self.frag_count)

    @classmethod
    def decode(cls, data: bytes) -> "Header":
        if len(data) < HEADER_SIZE:
            raise ProtocolError("short header")
        v, t, fl, ttl, mid, src, dst, fi, fc = _HEADER.unpack_from(data)
        return cls(type=t, message_id=mid, source=src, destination=dst,
                   frag_index=fi, frag_count=fc, ttl=ttl, flags=fl, version=v)

    def ad(self) -> bytes:
        """AEAD associated data: header bytes 0-2, 4-15 and 17."""
        raw = self.encode()
        return raw[0:3] + raw[4:16] + raw[17:18]


def frag_count_for(body_len: int) -> int:
    if not 1 <= body_len <= MAX_BODY:
        raise ProtocolError(f"body length {body_len} out of range")
    return (body_len + FRAGMENT_MAX - 1) // FRAGMENT_MAX


# --- Fragmentation (SPEC §7) ---

def fragment(type_: int, message_id: int, source: int, destination: int, body: bytes,
             ttl: int = DEFAULT_TTL) -> list[bytes]:
    count = frag_count_for(len(body))
    packets = []
    for i in range(count):
        h = Header(type=type_, message_id=message_id, source=source, destination=destination,
                   frag_index=i, frag_count=count, ttl=ttl)
        packets.append(h.encode() + body[i * FRAGMENT_MAX:(i + 1) * FRAGMENT_MAX])
    return packets


def parse_packet(packet: bytes) -> tuple[Header, bytes]:
    if len(packet) < HEADER_SIZE + 1:
        raise ProtocolError("packet too short")
    h = Header.decode(packet)
    if h.version != VERSION:
        raise ProtocolError("unknown version")
    if not 1 <= h.frag_count <= MAX_FRAGMENTS or h.frag_index >= h.frag_count:
        raise ProtocolError("bad fragment fields")
    return h, packet[HEADER_SIZE:]


class Reassembler:
    """Collects fragments per (source, message_id). `now` is seconds."""

    def __init__(self):
        self._slots: dict[tuple[int, int], dict] = {}

    def push(self, packet: bytes, now: float) -> tuple[Header, bytes] | None:
        h, frag = parse_packet(packet)
        self._expire(now)
        if h.frag_count == 1:
            return h, frag
        key = (h.source, h.message_id)
        slot = self._slots.get(key)
        if slot is None:
            if len(self._slots) >= REASSEMBLY_SLOTS:
                oldest = min(self._slots, key=lambda k: self._slots[k]["started"])
                del self._slots[oldest]
            slot = {"started": now, "header": h, "parts": {}}
            self._slots[key] = slot
        elif slot["header"].frag_count != h.frag_count:
            del self._slots[key]
            return None
        slot["parts"][h.frag_index] = frag
        if len(slot["parts"]) < h.frag_count:
            return None
        del self._slots[key]
        body = b"".join(slot["parts"][i] for i in range(h.frag_count))
        return slot["header"], body

    def _expire(self, now: float):
        for k in [k for k, s in self._slots.items() if now - s["started"] > REASSEMBLY_TIMEOUT]:
            del self._slots[k]


# --- Forwarding (SPEC §8) ---

class SeenCache:
    def __init__(self, size: int = SEEN_CACHE_SIZE):
        self._size = size
        self._items: list[tuple[int, int, int]] = []

    def check_and_add(self, h: Header) -> bool:
        """True if the fragment was already seen."""
        key = (h.source, h.message_id, h.frag_index)
        if key in self._items:
            return True
        self._items.append(key)
        if len(self._items) > self._size:
            self._items.pop(0)
        return False


def forward(packet: bytes) -> bytes | None:
    """Lower TTL. None means drop."""
    ttl = packet[3] - 1
    if ttl <= 0:
        return None
    return packet[:3] + bytes([ttl]) + packet[4:]


# --- Identity and crypto (SPEC §3, §10) ---

def public_key(private: bytes) -> bytes:
    return X25519PrivateKey.from_private_bytes(private).public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def node_id(pub: bytes) -> int:
    """First 4 bytes of the public key. Little-endian, so the header carries them unchanged."""
    return int.from_bytes(pub[:4], "little")


def shared_secret(private: bytes, peer_pub: bytes) -> bytes:
    return X25519PrivateKey.from_private_bytes(private).exchange(
        X25519PublicKey.from_public_bytes(peer_pub))


def derive_key(private: bytes, peer_pub: bytes) -> bytes:
    lo, hi = sorted([public_key(private), peer_pub])
    return HKDF(algorithm=hashes.SHA512(), length=KEY_SIZE, salt=None,
                info=b"kinjo/0.1" + lo + hi).derive(shared_secret(private, peer_pub))


def seal(key: bytes, source: int, destination: int, message_id: int, plaintext: bytes,
         nonce: bytes, ttl: int = DEFAULT_TTL) -> list[bytes]:
    """Encrypt a SEALED message and split it into packets."""
    if len(nonce) != NONCE_SIZE:
        raise ProtocolError("nonce must be 12 bytes")
    body_len = NONCE_SIZE + len(plaintext) + TAG_SIZE
    h = Header(type=TYPE_SEALED, message_id=message_id, source=source, destination=destination,
               frag_index=0, frag_count=frag_count_for(body_len), ttl=ttl)
    body = nonce + ChaCha20Poly1305(key).encrypt(nonce, plaintext, h.ad())
    return fragment(TYPE_SEALED, message_id, source, destination, body, ttl)


def open_sealed(key: bytes, header: Header, body: bytes) -> bytes:
    """Decrypt a reassembled SEALED body. Raises on any tampering."""
    if header.type != TYPE_SEALED or len(body) < NONCE_SIZE + TAG_SIZE + 1:
        raise ProtocolError("not a sealed body")
    nonce, ct = body[:NONCE_SIZE], body[NONCE_SIZE:]
    return ChaCha20Poly1305(key).decrypt(nonce, ct, header.ad())


# --- Payloads (SPEC §6, §9) ---

def text_plaintext(text: str) -> bytes:
    data = text.encode("utf-8")
    if len(data) > MAX_TEXT:
        raise ProtocolError("text too long")
    return bytes([KIND_TEXT]) + data


def identity_body(pub: bytes, name: str) -> bytes:
    n = name.encode("utf-8")
    if len(pub) != 32 or len(n) > MAX_NAME:
        raise ProtocolError("bad identity")
    return pub + n


def parse_identity(body: bytes) -> tuple[bytes, str]:
    if not 32 <= len(body) <= 32 + MAX_NAME:
        raise ProtocolError("bad identity")
    return body[:32], body[32:].decode("utf-8")


def _steps(a: tuple[int, int], b: tuple[int, int]) -> list[tuple[int, int]]:
    """Points after `a` up to `b`, each step at most 127 on both axes."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    n = max(1, -(-max(abs(dx), abs(dy)) // 127))
    return [(a[0] + dx * i // n, a[1] + dy * i // n) for i in range(1, n + 1)]


def encode_drawing(strokes: list[list[tuple[int, int]]], colors: list[int] | None = None) -> bytes:
    """Strokes are lists of (x, y), `colors` the palette index per stroke (default 0).
    Big jumps get intermediate points, long strokes get split."""
    out = bytearray([KIND_DRAWING])
    current = 0
    for n, stroke in enumerate(strokes):
        if not stroke:
            continue
        color = colors[n] if colors else 0
        if not 0 <= color < len(PALETTE):
            raise ProtocolError(f"color {color} not in the palette")
        if color != current:
            out += bytes([COLOR_MARKER, color])
            current = color
        for x, y in stroke:
            if not (0 <= x < CANVAS_W and 0 <= y < CANVAS_H):
                raise ProtocolError(f"point {(x, y)} off canvas")
        points = [stroke[0]]
        for p in stroke[1:]:
            points += _steps(points[-1], p)
        while points:
            chunk, points = points[:255], points[254:] if len(points) > 255 else []
            out += bytes([len(chunk)]) + struct.pack("<HH", *chunk[0])
            for prev, cur in zip(chunk, chunk[1:]):
                out += struct.pack("<bb", cur[0] - prev[0], cur[1] - prev[1])
    return bytes(out)


def decode_drawing_colored(plaintext: bytes) -> list[tuple[int, list[tuple[int, int]]]]:
    """[(palette index, stroke), ...]"""
    if not plaintext or plaintext[0] != KIND_DRAWING:
        raise ProtocolError("not a drawing")
    return _decode_items(plaintext, 1)


def _decode_items(plaintext: bytes, start: int) -> list[tuple[int, list[tuple[int, int]]]]:
    strokes, i, color = [], start, 0
    while i < len(plaintext):
        count = plaintext[i]
        if count == COLOR_MARKER:
            if i + 1 >= len(plaintext) or plaintext[i + 1] >= len(PALETTE):
                raise ProtocolError("bad color")
            color = plaintext[i + 1]
            i += 2
            continue
        if i + 5 + 2 * (count - 1) > len(plaintext):
            raise ProtocolError("bad stroke")
        x, y = struct.unpack_from("<HH", plaintext, i + 1)
        i += 5
        stroke = [(x, y)]
        for _ in range(count - 1):
            dx, dy = struct.unpack_from("<bb", plaintext, i)
            i += 2
            x, y = x + dx, y + dy
            stroke.append((x, y))
        strokes.append((color, stroke))
    return strokes


def note_plaintext(text: str, strokes: list[list[tuple[int, int]]], colors: list[int] | None = None) -> bytes:
    """Text and drawing in one message. The drawing part is a drawing without its kind byte."""
    t = text.encode("utf-8")
    if len(t) > MAX_TEXT:
        raise ProtocolError("text too long")
    return bytes([KIND_NOTE, len(t)]) + t + encode_drawing(strokes, colors)[1:]


def decode_note(plaintext: bytes) -> tuple[str, list[tuple[int, list[tuple[int, int]]]]]:
    if len(plaintext) < 2 or plaintext[0] != KIND_NOTE:
        raise ProtocolError("not a note")
    n = plaintext[1]
    if n > MAX_TEXT or 2 + n > len(plaintext):
        raise ProtocolError("bad note text")
    return plaintext[2:2 + n].decode("utf-8"), _decode_items(plaintext, 2 + n)


def decode_drawing(plaintext: bytes) -> list[list[tuple[int, int]]]:
    """Strokes without their colors."""
    return [stroke for _, stroke in decode_drawing_colored(plaintext)]


# --- Serial framing (SPEC §12) ---

def cobs_encode(data: bytes) -> bytes:
    out = bytearray()
    block = bytearray()
    for b in data:
        if b == 0:
            out += bytes([len(block) + 1]) + block
            block.clear()
        else:
            block.append(b)
            if len(block) == 254:
                out += b"\xff" + block
                block.clear()
    out += bytes([len(block) + 1]) + block
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        block = data[i + 1:i + code]
        if len(block) != code - 1 or 0 in block:
            raise ProtocolError("bad COBS")
        out += block
        i += code
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def serial_frame(frame_type: int, body: bytes) -> bytes:
    return cobs_encode(bytes([frame_type]) + body) + b"\x00"


def parse_serial_frame(frame: bytes) -> tuple[int, bytes]:
    """`frame` without the trailing 0x00."""
    data = cobs_decode(frame)
    if not data:
        raise ProtocolError("empty frame")
    return data[0], data[1:]


# --- Provisioning (SPEC §13) ---

def encode_name(name: str) -> bytes:
    n = name.encode("utf-8")
    if len(n) > MAX_NAME:
        raise ProtocolError("name too long")
    return bytes([len(n)]) + n


def decode_name(data: bytes, offset: int) -> tuple[str, int]:
    if offset >= len(data):
        raise ProtocolError("missing name")
    n = data[offset]
    end = offset + 1 + n
    if n > MAX_NAME or end > len(data):
        raise ProtocolError("bad name")
    return data[offset + 1:end].decode("utf-8"), end


def provision_request(cmd: int, name: str = "", pub: bytes = b"", flags: int = 0) -> bytes:
    if cmd in (CMD_INFO, CMD_CLEAR_CONTACTS, CMD_WIPE):
        return bytes([cmd])
    if cmd == CMD_SET_NAME:
        return bytes([cmd]) + encode_name(name)
    if cmd == CMD_ADD_CONTACT:
        if len(pub) != KEY_SIZE:
            raise ProtocolError("bad key")
        return bytes([cmd, flags]) + pub + encode_name(name)
    raise ProtocolError("unknown command")


def parse_provision_request(body: bytes) -> tuple[int, dict]:
    """Strict: trailing bytes are malformed. Unknown commands raise with cmd set."""
    if not body:
        raise ProtocolError("empty request")
    cmd = body[0]
    if cmd in (CMD_INFO, CMD_CLEAR_CONTACTS, CMD_WIPE):
        end, args = 1, {}
    elif cmd == CMD_SET_NAME:
        name, end = decode_name(body, 1)
        args = {"name": name}
    elif cmd == CMD_ADD_CONTACT:
        if len(body) < 2 + KEY_SIZE:
            raise ProtocolError("short contact")
        name, end = decode_name(body, 2 + KEY_SIZE)
        args = {"flags": body[1], "pub": body[2:2 + KEY_SIZE], "name": name}
    else:
        raise ProtocolError("unknown command")
    if end != len(body):
        raise ProtocolError("trailing bytes")
    return cmd, args


def info_data(pub: bytes, contacts: int, blocks: int, name: str) -> bytes:
    return pub + bytes([VERSION, contacts]) + struct.pack("<H", blocks) + encode_name(name)


def parse_info(data: bytes) -> dict:
    if len(data) < KEY_SIZE + 4:
        raise ProtocolError("short info")
    name, end = decode_name(data, KEY_SIZE + 4)
    if end != len(data):
        raise ProtocolError("trailing bytes")
    return {"pub": data[:KEY_SIZE], "version": data[KEY_SIZE], "contacts": data[KEY_SIZE + 1],
            "blocks": struct.unpack_from("<H", data, KEY_SIZE + 2)[0], "name": name}


def provision_reply(cmd: int, status: int, data: bytes = b"") -> bytes:
    return bytes([cmd, status]) + data


def parse_provision_reply(body: bytes) -> tuple[int, int, bytes]:
    if len(body) < 2:
        raise ProtocolError("short reply")
    return body[0], body[1], body[2:]
