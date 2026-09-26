# Protocol reference

Python version of the wire format in [SPEC.md](../SPEC.md). It generates the shared test vectors in [test-vectors/](../test-vectors/). Firmware and laptop tests must produce the same bytes.

```sh
cd protocol/reference
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python gen_vectors.py      # rewrites test-vectors/*.json
.venv/bin/python -m unittest -v
```

| Vector file | Covers |
|---|---|
| `header.json` | header bytes and AEAD associated data |
| `fragment.json` | splitting bodies of 1, 232, 233, 600 and 3712 bytes |
| `keys.json` | two fixed key pairs, node IDs, X25519 secret, HKDF key |
| `sealed.json` | encrypted text (1 packet) and drawing (several packets) |
| `drawing.json` | stroke encoding, big jumps, long strokes |
| `identity.json` | IDENTITY body and packet |
| `cobs.json` | COBS and serial frames |
| `provision.json` | USB provisioning commands, replies, info and malformed requests |

The keys in the vectors are public test keys. Never use them on a real device.
