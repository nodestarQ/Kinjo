// Handheld (ESP32-DevKitC): keeps its key and contacts in flash, is set up over
// USB by the web app (SPEC §13) and sends and receives sealed messages.
//
// Build with -DKINJO_TEST_DIRECT to run this on a spare XIAO that talks to the
// radio bridge directly (no relay), for testing without the real handheld.

#include <kinjo.h>
#include <kinjo_radio.h>

#include "node.h"
#include "storage.h"

using namespace kinjo;

static DeviceState state;
static FrameReader reader;
static uint32_t last_stats_ms = 0;

static void on_message(const Contact& from, const uint8_t* pt, size_t len) {
  if (pt[0] == KIND_TEXT) {
    radio::log("rx text from %.*s: %.*s", from.name_len, from.name, (int)(len - 1), (const char*)pt + 1);
  } else if (pt[0] == KIND_DRAWING) {
    radio::log("rx drawing from %.*s, %u bytes", from.name_len, from.name, (unsigned)len);
  }
}

static void log_identity() {
  radio::log("handheld %.*s, node %08lx, %u contacts, mac %s", state.name_len, state.name,
             (unsigned long)node_id(state.pub), state.contact_count, WiFi.macAddress().c_str());
}

// SEND_TEXT (SPEC §12): contact name + text. For testing without the UI.
static void handle_send_text(const uint8_t* body, size_t len) {
  if (len < 1 || 1 + (size_t)body[0] > len) return;
  const char* name = (const char*)body + 1;
  uint8_t name_len = body[0];
  for (uint8_t i = 0; i < state.contact_count; i++) {
    const Contact& c = state.contacts[i];
    if (c.name_len == name_len && memcmp(c.name, name, name_len) == 0) {
      bool ok = node::send_text(c, (const char*)body + 1 + name_len, len - 1 - name_len);
      radio::log(ok ? "sent text to %.*s" : "sending to %.*s failed", name_len, name);
      return;
    }
  }
  radio::log("no contact named %.*s", name_len, name);
}

static void handle_serial() {
  while (Serial.available()) {
    uint8_t type;
    const uint8_t* body;
    size_t len;
    if (!reader.push(Serial.read(), type, body, len)) continue;
    if (type == FRAME_PROVISION) {
      uint8_t reply[PROVISION_REPLY_MAX];
      bool changed;
      size_t n = handle_provision(state, body, len, storage::random_key, reply, changed);
      if (changed) storage::save(state);
      radio::write_frame(FRAME_PROVISION_REPLY, reply, n);
    } else if (type == FRAME_SEND_TEXT) {
      handle_send_text(body, len);
    }
  }
}

void setup() {
#ifdef KINJO_TEST_DIRECT
  radio::begin("handheld", {MAC_BRIDGE});
#else
  radio::begin("handheld", {MAC_RELAY});
#endif
  bool first_boot = storage::load(state);
  node::state = &state;
  node::on_message = on_message;
  if (first_boot) radio::log("handheld: first boot, new key pair made");
  log_identity();
}

void loop() {
  handle_serial();

  radio::RxFrame f;
  while (radio::poll(f)) node::receive(f, millis());

  if (millis() - last_stats_ms > 10000) {
    last_stats_ms = millis();
    radio::log_stats("handheld");
  }
  delay(1);
}
