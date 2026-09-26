# Kinjo protocol spec

Version **0.1**. Overview: [docs/design.md](../docs/design.md).

Any change to the wire format updates this file first. All multi-byte integers are **little-endian**.

## 1. Scope

v0.1 covers text and drawings between endpoints over a forced 2-hop ESP-NOW route, E2E encrypted, with identities from ENS.

Not in v0.1: rooms, time beacon, ACKs, gateway requests, intents. Non-goals: anonymity, traffic-analysis resistance, forward secrecy, jamming resistance.

## 2. Nodes

| Node | Hardware | Keys | Does |
|---|---|---|---|
| Handheld | ESP32-DevKitC | X25519 | sends and receives, doesn't forward |
| Relay | XIAO ESP32-C3 | none | forwards, can't decrypt |
| Radio bridge | XIAO ESP32-C3 | none | passes packets between ESP-NOW and the laptop over USB serial |
| Laptop node | laptop | X25519 | endpoint, ENS sync, desktop UI |

The laptop node is an endpoint. The radio bridge under it is just a modem.

## 3. Identity

- Each endpoint has a static **X25519 key pair**. The private key never leaves the device.
- **Node ID** = first 4 bytes of the X25519 public key. Used as `source` and `destination`.
- `0xFFFFFFFF` = broadcast.
- The ENS subname of a device (e.g. `handheld.alice.kinjo.eth`) publishes the public key. See §10.

## 4. Transport: ESP-NOW

| Setting | Value |
|---|---|
| Wi-Fi channel | 1 (change at the venue if busy, same on all nodes) |
| Addressing | unicast to peer MACs, never broadcast |
| ESP-NOW encryption | off (Kinjo encrypts end to end) |
| Max frame | 250 B |

Each node has a compile-time peer list (`firmware/lib/kinjo/topology.h`). This forces the demo route:

| Node | Peers |
|---|---|
| Handheld | relay |
| Relay | handheld, radio bridge |
| Radio bridge | relay |

## 5. Packet

Every ESP-NOW frame is one packet: an 18-byte header followed by a fragment of the message body.

| Offset | Field | Size | In AD | Notes |
|---|---|---|---|---|
| 0 | version | 1 | yes | `0x01` |
| 1 | type | 1 | yes | §6 |
| 2 | flags | 1 | yes | reserved, `0x00` |
| 3 | ttl | 1 | no | lowered by each forwarder |
| 4 | message_id | 4 | yes | random per message |
| 8 | source | 4 | yes | node ID |
| 12 | destination | 4 | yes | node ID or broadcast |
| 16 | frag_index | 1 | no | 0-based |
| 17 | frag_count | 1 | yes | 1 to 16 |
| 18 | fragment | 1 to 232 | | slice of the message body |

**AD** (AEAD associated data) = bytes 0 to 2, 4 to 15 and 17, concatenated in that order (16 bytes). A relay can change `ttl` without breaking decryption. Changing anything else makes decryption fail.

## 6. Message types

| Type | Name | Body | Encrypted |
|---|---|---|---|
| `0x01` | IDENTITY | X25519 public key (32 B) + ENS name (UTF-8, max 64 B) | no |
| `0x10` | SEALED | `nonce (12) + ciphertext + tag (16)` | yes |

SEALED plaintext starts with a **kind** byte, so relays can't tell text from drawings:

| Kind | Name | Rest of plaintext |
|---|---|---|
| `0x01` | TEXT | UTF-8, max 200 B. The handheld renders ASCII only |
| `0x02` | DRAWING | strokes, §9 |

## 7. Fragmentation

- Build the full body first (for SEALED: encrypt, then prepend nonce and append tag). Then split it into chunks of at most 232 B.
- All fragments share `message_id` and `frag_count`.
- Relays forward fragments without reassembling.
- The destination keeps up to **4** messages in reassembly. A message not complete after **5 s** is dropped. When all 4 slots are busy the oldest is dropped.
- No retransmission in v0.1. ESP-NOW unicast already retries at the MAC layer.

## 8. Forwarding

On every received frame:

1. Drop if `version != 0x01` or the frame is shorter than 19 B.
2. Drop if `(source, message_id, frag_index)` is in the **seen cache** (64 entries, oldest evicted). Otherwise add it.
3. If `destination` is me or broadcast: hand it to reassembly.
4. If I forward (relay only) and `destination` isn't me: lower `ttl`. If `ttl > 0`, send to every peer except the one it came from.

Default `ttl` when sending: **4**.

The same seen cache stops replays at the destination for as long as the entry lives. Longer replay protection is out of scope for v0.1.

## 9. Drawing

- Canvas 320 x 240, origin top left, one color, one pen width.
- Plaintext after the kind byte is a list of strokes until the end:

```text
stroke = point_count (u8, 1 to 255)
         x0 (u16) y0 (u16)
         (point_count - 1) x [dx (i8) dy (i8)]
```

- If a step is larger than 127 in either axis, the encoder adds points in between. A stroke longer than 255 points continues in a new stroke that starts at its last point.
- Max plaintext: 16 fragments x 232 B minus 29 B overhead.

## 10. Crypto

| Part | Choice |
|---|---|
| Key agreement | X25519 |
| Key derivation | HKDF-SHA512, salt empty, `info = "kinjo/0.1" + lower_pubkey + higher_pubkey` (keys sorted bytewise), 32-byte output |
| AEAD | ChaCha20-Poly1305 (RFC 8439), 12-byte random nonce, AD from §5 |
| Randomness | `esp_random()` (true RNG while the radio is on) |
| ESP32 library | Monocypher 4 (`crypto_x25519`, `crypto_aead_init_ietf`, `crypto_sha512_hkdf`) |
| Laptop libraries | Python `cryptography`, JS `@noble/curves` + `@noble/ciphers` + `@noble/hashes` |

Both sides derive the same key for a pair, so one key covers both directions. Random nonces are fine at our message volume.

Keys sit in plain flash (NVS) on the ESP32. Static keys mean no forward secrecy.

## 11. ENS

Names:

```text
kinjo.eth
  alice.kinjo.eth             owner, holds the wallet
    handheld.alice.kinjo.eth  device
    laptop.alice.kinjo.eth    device
```

Text records on a device subname:

| Key | Value |
|---|---|
| `xyz.kinjo.encryption-key` | X25519 public key, `0x`-prefixed hex |
| `xyz.kinjo.kind` | `device` |
| `xyz.kinjo.protocol` | `kinjo/0.1` |

Trust rule on the laptop node: a SEALED message is **verified as `<name>`** when it decrypts with the key currently cached from that name's `xyz.kinjo.encryption-key`. Static X25519 authenticates the sender implicitly.

**Revocation:** the owner clears `xyz.kinjo.encryption-key`. At its next ENS sync the laptop marks the device revoked and rejects its messages. Relays still forward them because they are blind.

Offline devices never resolve ENS. The laptop resolves names and pushes identities to the handheld over USB (§12).

Registration flow: [docs/onboarding.md](../docs/onboarding.md).

## 12. Serial bridge (laptop to radio bridge or handheld)

- USB CDC. Baud setting ignored, use 115200.
- Frames are **COBS**-encoded and end with `0x00`.
- Decoded frame = `frame_type (1) + body`.
- Decoders must accept both COBS forms for data ending in a full 254-byte block (with or without a trailing `0x01`).

| Type | Name | Direction | Body |
|---|---|---|---|
| `0x01` | RADIO_RX | device to laptop | from MAC (6) + RSSI (i8, 0 = unknown) + packet |
| `0x02` | RADIO_TX | laptop to device | packet (sent to all peers) |
| `0x03` | LOG | device to laptop | UTF-8 text |
| `0x04` | PROVISION | laptop to handheld | §13 |
| `0x05` | PROVISION_REPLY | handheld to laptop | §13 |

The relay uses the same framing to report every frame it forwards as RADIO_RX. That is the log that shows it only sees ciphertext.

## 13. Provisioning (web app to handheld over USB)

Flow: [docs/onboarding.md](../docs/onboarding.md). The web app sends one PROVISION frame and waits for one PROVISION_REPLY before sending the next.

```text
PROVISION        = command (1) + arguments
PROVISION_REPLY  = command (1) + status (1) + data
name             = length (u8, 0 to 64) + UTF-8 bytes, ENS-normalized by the web app
```

| Command | Name | Arguments | Reply data (status OK) |
|---|---|---|---|
| `0x01` | INFO | none | info |
| `0x02` | SET_NAME | name (length 0 clears it) | none |
| `0x03` | ADD_CONTACT | flags (1) + public key (32) + name | none |
| `0x04` | CLEAR_CONTACTS | none | none |
| `0x05` | WIPE | none | info (with the new key) |

```text
info = public key (32) + protocol version (1, 0x01) + contacts (u8) + blocks (u16)
       + own name
```

- Contact flags: bit 0 = verified human (owner has `xyz.kinjo.verified-human`, see onboarding). Other bits 0.
- ADD_CONTACT with a name that's already stored replaces that contact.
- WIPE makes a new X25519 key pair and clears the name, contacts and block list. The old private key is overwritten.
- The block list is managed on the device only. Nothing here reads or changes it except WIPE.

| Status | Meaning |
|---|---|
| `0x00` | OK |
| `0x01` | malformed arguments |
| `0x02` | full (64 contacts) |
| `0x03` | unknown command |

## 14. Limits

| Constant | Value |
|---|---|
| Header | 18 B |
| Fragment payload | 232 B |
| Max fragments | 16 |
| Max body | 3712 B |
| SEALED overhead | 28 B + 1 B kind |
| Default TTL | 4 |
| Seen cache | 64 entries |
| Reassembly slots / timeout | 4 / 5 s |
| Max text | 200 B |
| Max ENS name | 64 B |
| Contacts / blocks on the handheld | 64 / 256 |

## 15. Test vectors

`protocol/test-vectors/` holds JSON files made by the reference in `protocol/reference/`. Firmware and laptop tests must match them:

- header encode and decode
- fragmentation and reassembly
- X25519 + HKDF key for a fixed key pair
- SEALED body for a fixed key, nonce and plaintext
- drawing encode
- COBS frames
- provisioning commands and replies
