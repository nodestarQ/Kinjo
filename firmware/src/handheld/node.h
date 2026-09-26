// Messaging for the handheld: seals messages to contacts, receives and opens
// messages addressed to this device (SPEC §6 to §8).
#pragma once

#include <esp_system.h>
#include <kinjo.h>
#include <kinjo_radio.h>

namespace node {

using namespace kinjo;

// Called for every message that opens. `plaintext` starts with the kind byte.
typedef void (*MessageHandler)(const Contact& from, const uint8_t* plaintext, size_t len);

static DeviceState* state = nullptr;
static MessageHandler on_message = nullptr;
static SeenCache seen;
static Reassembler reassembler;
static uint8_t body_buf[MAX_BODY];
static uint8_t plain_buf[MAX_BODY];

inline uint32_t my_id() { return node_id(state->pub); }

// Seals `plaintext` to `to` and sends every fragment to the peers. False if the key or size is bad.
inline bool send(const Contact& to, const uint8_t* plaintext, size_t len) {
  uint8_t key[KEY_SIZE], nonce[NONCE_SIZE];
  if (!derive_key(state->priv, to.pub, key)) return false;
  esp_fill_random(nonce, sizeof(nonce));
  Header base;
  size_t body_len = seal(key, my_id(), node_id(to.pub), esp_random(), DEFAULT_TTL, nonce, plaintext, len, body_buf, base);
  memset(key, 0, sizeof(key));
  if (body_len == 0) return false;
  for (uint8_t i = 0; i < base.frag_count; i++) {
    uint8_t packet[PACKET_MAX];
    size_t n = build_fragment(base, body_buf, body_len, i, packet);
    radio::send_to_peers(packet, n, nullptr);
    delay(4);  // give ESP-NOW time to send before the next fragment
  }
  return true;
}

inline bool send_text(const Contact& to, const char* text, size_t len) {
  uint8_t pt[MAX_TEXT + 1];
  size_t n = text_plaintext(text, len, pt);
  return n && send(to, pt, n);
}

// Handles one received radio frame.
inline void receive(const radio::RxFrame& f, uint32_t now_ms) {
  Header h;
  const uint8_t* frag;
  size_t frag_len;
  if (!parse_packet(f.data, f.len, h, frag, frag_len) || seen.check_and_add(h)) return;
  if (h.destination != my_id() || h.type != TYPE_SEALED) return;

  const uint8_t* body;
  size_t body_len;
  if (!reassembler.push(f.data, f.len, now_ms, h, body, body_len)) return;

  const Contact* from = find_contact(*state, h.source);
  if (!from) {
    radio::log("handheld: message from unknown node %08lx dropped", (unsigned long)h.source);
    return;
  }
  uint8_t key[KEY_SIZE];
  size_t plain_len = 0;
  bool ok = derive_key(state->priv, from->pub, key) && open_sealed(key, h, body, body_len, plain_buf, plain_len);
  memset(key, 0, sizeof(key));
  if (!ok) {
    radio::log("handheld: message from %.*s failed to open", from->name_len, from->name);
    return;
  }
  if (on_message) on_message(*from, plain_buf, plain_len);
}

}  // namespace node
