# Firmware

Arduino sketches for the three boards plus the shared protocol library.

| Folder | Board | Does |
|---|---|---|
| `src/relay` | XIAO ESP32-C3 | forwards packets, logs every one over USB |
| `src/bridge` | XIAO ESP32-C3 | modem for the laptop node (ESP-NOW to serial frames and back) |
| `src/handheld` | ESP32-DevKitC | not written yet |
| `lib/kinjo` | all | wire format, crypto, radio glue. `topology.h` holds the board MACs |
| `test` | laptop | library tests against `protocol/test-vectors` |
| `tools` | laptop | `monitor.py` shows a board's serial frames |

Toolchain: esp32 core **2.0.17**. Board wiring: [docs/hardware.md](../docs/hardware.md).

## Build and flash

Arduino IDE: link the library once, then open a sketch folder.

```sh
ln -s "$PWD/firmware/lib/kinjo" ~/Documents/Arduino/libraries/kinjo   # macOS
```

arduino-cli, from the repo root:

```sh
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --library firmware/lib/kinjo firmware/src/relay
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C3 -p <port> firmware/src/relay
```

Same for `bridge`. The handheld uses `esp32:esp32:esp32wrover`.

If the first upload to a XIAO fails with `No serial data received`: unplug it, hold **B**, plug it in, release **B**, upload again.

## Set the MACs

1. Flash each board once. It logs `... up, mac xx:xx:xx:xx:xx:xx` at boot (see monitor below).
2. Put the three MACs into `lib/kinjo/src/topology.h`.
3. Flash all boards again. A board with a zero MAC in `topology.h` logs a warning and skips that peer.

## Check relay and bridge without the handheld

```sh
python3 -m venv .venv && .venv/bin/pip install -r firmware/tools/requirements.txt
.venv/bin/python firmware/tools/monitor.py <relay port>                # terminal 1
.venv/bin/python firmware/tools/monitor.py <bridge port> --send-test   # terminal 2
```

Pass: terminal 2 prints `TX SEALED ...` every 2 s and terminal 1 prints `RX from <bridge mac> SEALED ...` with the same message ID. The relay only ever shows ciphertext. It reports `tx=0` while the handheld MAC is still zero (nothing to forward to). Opening a port resets the XIAO, so each run starts with its boot log.
