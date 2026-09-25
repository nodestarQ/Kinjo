# Kinjo protocol spec

> **Status: draft.** "Proposed" values are starting points. Overview: `docs/design.md`.

---

## 0. Meta

- Spec version: `0.1` (TBD at kickoff)
- Rule: **any change to the wire format requires updating this file first**.

## 1. Scope

- Goals: see `docs/design.md`.
- Non-goals (say explicitly): anonymity, traffic-analysis resistance, forward secrecy, jamming resistance, production wallet security.

## 2. Terms

- **Owner:** ENS name holder, e.g. `alice.kinjo.eth`
- **Device / node:** a physical participant with its own keys, e.g. `handheld.alice.kinjo.eth`
- **Relay:** a node that forwards packets not addressed to it
- **Gateway:** a node with Internet that serves `GATEWAY_*` requests (chain reads) and submits `INTENT`s
- **Hop, TTL, transport, MTU, fragment:** define here

## 3. Node roles & capabilities

| Node | Roles | Keys | Can spend? |
|---|---|---|---|
| Handheld | endpoint, (relay) | signing + encryption | yes, via vault limits (§13) |
| Relay | relay | signing (identity only) | no |
| Laptop node | endpoint, gateway | signing + encryption (keys on laptop) | no (only submits intents) |

Questions:
- Capability names/bitfield? (proposed: `relay`, `endpoint`, `gateway`, `spend`)
- Does the relay need an ENS name at all? (nice-to-have: `node1.bob.kinjo.eth`)

## 4. Identifiers

- Node ID in packets: ENS name, device address or a **short ID**? (proposed: 4-byte short ID to save space + privacy)
- How is the short ID derived and mapped to ENS? (e.g. first 4 bytes of hash(device address) or ephemeral per session)
- Message ID: size and generation? (proposed: 4 bytes random or `source + sequence`)
- Collision handling?

## 5. Transport abstraction

- Interface: `send(bytes, next_hop)`, `on_receive(bytes, last_hop)`, `mtu()`
- ESP-NOW binding:
  - channel (proposed: fixed, 1/6/11; test at the venue)
  - unicast to next-hop MAC (proposed) vs broadcast for discovery (`HELLO`)
  - peer list / forced-topology allowlist format
  - MTU: 250 B
- BLE binding (stretch): GATT service/characteristic UUIDs, MTU negotiation
- Is `last_hop` a transport-level value or a header field? (header field makes non-MAC transports work)

## 6. Packet format

Header fields to define (size, byte order and whether they're covered by AEAD associated data):

| Field | Size (proposed) | In AD? | Notes |
|---|---|---|---|
| version | 1 B | yes | |
| type | 1 B | yes | §7 |
| flags | 1 B | yes | encrypted? ack-requested? |
| message_id | 4 B | yes | |
| source | 4 B | yes | short ID |
| destination | 4 B | yes | short ID or broadcast/room |
| last_hop | 4 B | **no** | rewritten per hop |
| ttl | 1 B | **no** | decremented by relays |
| sequence | 4 B | yes | replay protection |
| frag_index / frag_count | 1 B + 1 B | count yes | §8 |
| nonce | 12 B | - | only on encrypted messages |
| payload / ciphertext | var | - | |
| auth_tag | 16 B | - | |

Questions: byte order (proposed: little-endian); fixed header vs TLV; header CRC needed (ESP-NOW already has a frame check)?

## 7. Message types

| Code | Type | Encrypted | Direction | Payload § |
|---|---|---|---|---|
| TBD | HELLO | no | broadcast | discovery / neighbor list |
| TBD | IDENTITY | no (signed) | any | device cert §11 |
| TBD | MESSAGE (text) | yes | endpoint → endpoint | §12 |
| TBD | DRAWING | yes | endpoint → endpoint | §12 |
| TBD | ROOM_MESSAGE | yes | endpoint → room | §12 (group key model TBD) |
| TBD | ACK | yes/no? | reverse | §15 |
| TBD | KEY_UPDATE | signed | any | rotation |
| TBD | TIME_BEACON | signed | gateway → all | §11 |
| TBD | GATEWAY_REQUEST / RESPONSE | yes (to gateway) | §14 | |
| TBD | INTENT / TX_RECEIPT | yes (to each gateway / back to device) | §14 | |

Decide at kickoff: code values and which types are MVP vs stretch.

## 8. Fragmentation

- Encrypt first, then fragment (proposed).
- Max fragments per message? Max message size?
- Reassembly buffer: count, timeout, eviction.
- Retransmit missing fragments: ACK bitmap or resend everything?
- Per-transport MTU → fragment size computed from `transport.mtu()`.

## 9. Forwarding

- Dedupe cache: key (`source + message_id + frag_index`?), size, expiry.
- Default TTL (proposed: 4).
- Forward rule: unicast to all allowed neighbors except `last_hop`?
- Forced topology config: where it lives (compile-time table vs serial command).
- Does the handheld also forward? (proposed: yes, same code, off by default)

## 10. Crypto

- Libraries: ESP32 (mbedTLS / libsodium; trezor-crypto / micro-ecc), laptop (libsodium / noble / ethers|viem). **Decide now.**
- Key agreement: static X25519 per device pair → key derivation (HKDF? with what info/salt?)
- AEAD: ChaCha20-Poly1305 (12-byte nonce: random or counter-based?)
- Associated data = which header fields (§6)
- Replay: per-source sequence window
- Room/group key model: shared room key vs fan-out (**decision needed**)
- Key storage on ESP32: NVS? flash encryption? (hackathon: plain NVS, disclosed)
- **Test vectors**: one shared file used by both firmware and laptop tests (§17)

## 11. Identity & ENS

- ENS hierarchy and record keys (`xyz.kinjo.*`): list the final keys
- Device certificate (EIP-712) struct: fields, domain, size limit (must fit in one frame?)
- Provisioning flow: laptop → device over USB serial (what's sent, in which format)
- `IDENTITY` exchange: when (on HELLO? on first message?), caching, eviction
- Verification on-device: recover signer, compare with cached owner address
- Time: `TIME_BEACON` format and trust; behavior when there's no time source
- Revocation: how it propagates; what a node does with a revoked peer

## 12. Payload formats

- **Text:** UTF-8, max length? Emoji support on the TFT (font)?
- **Drawing:** stroke encoding
  - canvas size + coordinate system (proposed 320×240 or a normalized grid)
  - `STROKE_START x,y` + delta list (varint/zigzag?) + `STROKE_END`
  - pen width/color/erase? max strokes per message?
- **Room message:** room ID format, membership

## 13. Wallet / intents

- Define the `Intent` EIP-712 struct here; `KinjoVault` checks it on-chain.
- Decide: nonce scheme (random + bitmap), deadline default, spend-limit period.
- `target` = cached owner address of the chosen ENS name (never resolved by the gateway); MVP `data` empty (ETH transfers); optional note sent E2E as `MESSAGE` with the tx hash.
- Stealth payments (stretch): `target` derived from `xyz.kinjo.stealth-meta-address`.
- **Test vector:** the same digest on ESP32 and ethers/viem.

## 14. Gateway protocol

- `GATEWAY_REQUEST` / `RESPONSE`: request kinds (ENS resolve, balance, time), encoding (CBOR? fixed structs?)
- `GATEWAY_REQUEST` kinds for chain reads: vault balance, device allowance, tx status by hash
- `INTENT` → `TX_RECEIPT`: payload fields for each (receipt: nonce, tx hash, status, block, gateway ID, gateway signature)
- Multiple gateways: send to every known gateway (the demo has one, but don't hard-code that); simulate before submitting; resend timeout N; idempotency via nonce
- Timeouts and error codes (e.g. expired, nonce used, not authorized, over limit)

## 15. Reliability

- ACKs end-to-end or hop-by-hop? (ESP-NOW unicast already gives hop-by-hop MAC ACK)
- End-to-end ACK for messages: yes/no; retry count and backoff
- UI states: sending / delivered / failed

## 16. Serial bridge (laptop ↔ XIAO)

- Framing: COBS or SLIP + length + CRC16 (**decide**)
- Frame types: `RADIO_RX` (bytes + last_hop MAC + RSSI), `RADIO_TX` (bytes + next_hop), `CONFIG` (channel, peers), `LOG`
- Baud / USB CDC settings
- Is the relay's debug log the same framing? (useful for the "relay only sees ciphertext" demo)

## 17. Test vectors & conformance

Shared `protocol/test-vectors/` used by firmware and laptop tests:
- [ ] header encode/decode
- [ ] fragmentation/reassembly
- [ ] X25519 shared secret + key derivation
- [ ] ChaCha20-Poly1305 encrypt with fixed key/nonce/AD
- [ ] EIP-712 device-cert digest + signature
- [ ] EIP-712 intent digest + signature
- [ ] drawing encode/decode

## 18. Limits & constants

Table to fill: MTU, max message size, max fragments, dedupe cache size, reassembly timeout, default TTL, max text length, max strokes.

## 19. Open decisions (kickoff agenda)

1. Short IDs vs full addresses in the header
2. Room/group key model
3. Nonce scheme (random vs counter)
4. Serial framing (COBS vs SLIP)
5. Payload encoding (fixed structs vs CBOR)
6. MVP message types vs stretch
7. Libraries on both sides (crypto, TFT, Ethereum)
8. Who owns which section (below)

## 20. Section owners

| Section | Owner |
|---|---|
| 5, 6, 7, 8, 9, 15 (transport, packets, forwarding) | senior |
| 10, 13 (crypto, intents) | senior |
| 11 (identity & ENS) | senior + B2 |
| 12 (payloads, drawing) | B1 |
| 14, 16 (gateway, serial bridge) | B2 |
| 17 (test vectors) | everyone for their sections |
