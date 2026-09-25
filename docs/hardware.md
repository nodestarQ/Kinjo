# Kinjo hardware

No soldering needed.

## Parts

| Part | Qty | Used for |
|---|---|---|
| ESP32-DevKitC-VE | 1 | handheld |
| MSP2807 2.8" touch TFT (ILI9341) | 1 | handheld screen |
| Tactile switch | 3 | handheld buttons |
| Half-size breadboard, jumper wires | 1 | handheld wiring |
| Seeed XIAO ESP32-C3 | 2 | relay and laptop radio |
| USB power bank | 1 | powers the handheld |

## Handheld pins

| Screen pin | ESP32 |
|---|---|
| VCC | 5V |
| GND | GND |
| LED | 3V3 |
| SCK, T_CLK | 18 |
| SDI (MOSI), T_DIN | 23 |
| T_DO | 19 |
| CS | 26 |
| RESET | 22 |
| DC | 21 |
| T_CS | 27 |
| SDO (MISO), T_IRQ | not connected |

Buttons go between the pin and GND (`INPUT_PULLUP`): MESSAGES 25, ROOM 32, SEND 33.

Touch calibration (rotation 1): `{ 470, 3255, 371, 2826, 7 }`

## If something's wrong

- Black screen: LED isn't on 3V3.
- White screen: a data wire is in the wrong place.
- Upload hangs: something is plugged into the ESP32's RX pin.
