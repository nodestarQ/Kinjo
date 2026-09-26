// Forced demo route (SPEC §4): handheld <-> relay <-> radio bridge.
// Fill in the MACs each board prints at boot ("mac xx:xx:xx:xx:xx:xx").
// A MAC left at all zeros is skipped. The board logs a warning.
#pragma once

#include <stdint.h>

namespace kinjo {

constexpr uint8_t WIFI_CHANNEL = 1;

constexpr uint8_t MAC_HANDHELD[6] = {0x88, 0xF1, 0x55, 0x03, 0x2D, 0xE4};
constexpr uint8_t MAC_RELAY[6] = {0xAC, 0x27, 0x6E, 0x80, 0x26, 0x6C};
constexpr uint8_t MAC_BRIDGE[6] = {0x1C, 0xDB, 0xD4, 0xEE, 0x4A, 0xF4};

}  // namespace kinjo
