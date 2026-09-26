// Radio bridge (XIAO ESP32-C3): a modem for the laptop node (SPEC §12).
// Radio to laptop: every received packet goes out as RADIO_RX.
// Laptop to radio: every RADIO_TX frame is sent to the relay.
// The laptop does dedupe, reassembly and crypto.

#include <kinjo.h>
#include <kinjo_radio.h>

using namespace kinjo;

static FrameReader reader;
static uint32_t last_stats_ms = 0;

void setup() {
  radio::begin("bridge", {MAC_RELAY});
  radio::log_identity("bridge");
}

void loop() {
  radio::RxFrame f;
  while (radio::poll(f)) radio::report_rx(f);

  while (Serial.available()) {
    uint8_t type;
    const uint8_t* body;
    size_t len;
    if (!reader.push(Serial.read(), type, body, len) || type != FRAME_RADIO_TX) continue;
    Header h;
    const uint8_t* frag;
    size_t frag_len;
    if (parse_packet(body, len, h, frag, frag_len)) {
      radio::send_to_peers(body, len, nullptr);
    } else {
      radio::log("bridge: RADIO_TX with an invalid packet, dropped");
    }
  }

  if (millis() - last_stats_ms > 10000) {
    last_stats_ms = millis();
    radio::log_stats("bridge");
  }
  delay(1);
}
