// Relay (XIAO ESP32-C3): forwards packets between the handheld and the radio
// bridge (SPEC §8). It has no keys and never decrypts. Every packet it
// forwards is reported over USB serial as RADIO_RX, which is the demo log
// showing it only sees ciphertext.

#include <kinjo.h>
#include <kinjo_radio.h>

using namespace kinjo;

static SeenCache seen;
static uint32_t last_stats_ms = 0;

void setup() {
  radio::begin("relay", {MAC_HANDHELD, MAC_BRIDGE});
  radio::log_identity("relay");
}

void loop() {
  radio::RxFrame f;
  while (radio::poll(f)) {
    Header h;
    const uint8_t* frag;
    size_t frag_len;
    if (!parse_packet(f.data, f.len, h, frag, frag_len)) continue;
    if (seen.check_and_add(h)) continue;
    radio::report_rx(f);
    if (forward_ttl(f.data, f.len)) radio::send_to_peers(f.data, f.len, f.mac);
  }

  if (millis() - last_stats_ms > 10000) {
    last_stats_ms = millis();
    radio::log_stats("relay");
  }
  delay(1);
}
