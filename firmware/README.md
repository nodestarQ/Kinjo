# Firmware

Arduino sketches for the three boards plus the shared protocol library.

| Folder | Board | Does |
|---|---|---|
| `src/relay` | XIAO ESP32-C3 | forwards packets, logs every one over USB |
| `src/bridge` | XIAO ESP32-C3 | modem for the laptop node (ESP-NOW to serial frames and back) |
| `src/handheld` | ESP32-DevKitC | key and contacts in flash, USB provisioning, sealed send and receive, screen UI (`ui.h`) |
| `lib/kinjo` | all | wire format, crypto, radio glue. `topology.h` holds the board MACs |
| `test` | laptop | library tests against `protocol/test-vectors` |
| `tools` | laptop | `monitor.py` shows a board's serial frames, `provision.py` sends USB provisioning commands |

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

## Screen library setup (handheld only)

TFT_eSPI **2.5.43** takes its pins from `User_Setup.h` in the library folder (`~/Documents/Arduino/libraries/TFT_eSPI/` on macOS, `~/Arduino/libraries/TFT_eSPI/` on Linux). Replace its content with:

```c
#define ILI9341_DRIVER
#define TFT_MISO 19   // not wired to the display; touch T_DO uses it
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   26
#define TFT_DC   21
#define TFT_RST  22
#define TOUCH_CS 27
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
```

## Handheld controls

| Button | Messages screen | Draw screen |
|---|---|---|
| MESSAGES (GPIO 25) | open the draw screen | back to messages |
| ROOM (GPIO 32) | next contact as recipient | next contact as recipient |
| SEND (GPIO 33) | send "gm" | send the drawing |

Draw with a stylus or fingernail. A received drawing opens on the draw screen. Senders with a World ID badge show in green.

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

## Test the handheld on a spare XIAO

Build with `-DKINJO_TEST_DIRECT` and the handheld talks to the bridge directly, no relay needed:

```sh
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --library firmware/lib/kinjo \
  --build-property "compiler.cpp.extra_flags=-DKINJO_TEST_DIRECT" --output-dir /tmp/hh firmware/src/handheld
arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32C3 -p <port> --input-dir /tmp/hh firmware/src/handheld
```

Then, with the venv from above:

```sh
cd firmware/tools
python provision.py <handheld port> info               # prints its public key
python provision.py <handheld port> set-name handheld.alice.kinjo.eth
python provision.py <handheld port> add-test-laptop    # the laptop test key as a contact
python provision.py <handheld port> send laptop.alice.kinjo.eth "gm Tokyo"
python monitor.py <bridge port> --as-laptop --peer <handheld public key> [--send-test]
```

Pass: the bridge monitor prints `opened: TEXT 'gm Tokyo'`. With `--send-test` the handheld logs `rx text from laptop.alice.kinjo.eth: test 1` and so on.
