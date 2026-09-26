#include <string.h>

#include "kinjo.h"

namespace kinjo {

static void put32(uint8_t* p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void encode_header(const Header& h, uint8_t out[HEADER_SIZE]) {
  out[0] = h.version;
  out[1] = h.type;
  out[2] = h.flags;
  out[3] = h.ttl;
  put32(out + 4, h.message_id);
  put32(out + 8, h.source);
  put32(out + 12, h.destination);
  out[16] = h.frag_index;
  out[17] = h.frag_count;
}

void decode_header(const uint8_t in[HEADER_SIZE], Header& h) {
  h.version = in[0];
  h.type = in[1];
  h.flags = in[2];
  h.ttl = in[3];
  h.message_id = get32(in + 4);
  h.source = get32(in + 8);
  h.destination = get32(in + 12);
  h.frag_index = in[16];
  h.frag_count = in[17];
}

void header_ad(const Header& h, uint8_t out[AD_SIZE]) {
  uint8_t raw[HEADER_SIZE];
  encode_header(h, raw);
  memcpy(out, raw, 3);
  memcpy(out + 3, raw + 4, 12);
  out[15] = raw[17];
}

bool parse_packet(const uint8_t* packet, size_t len, Header& h, const uint8_t*& frag, size_t& frag_len) {
  if (len < HEADER_SIZE + 1 || len > PACKET_MAX) return false;
  decode_header(packet, h);
  if (h.version != VERSION) return false;
  if (h.frag_count < 1 || h.frag_count > MAX_FRAGMENTS || h.frag_index >= h.frag_count) return false;
  frag = packet + HEADER_SIZE;
  frag_len = len - HEADER_SIZE;
  return true;
}

uint8_t frag_count_for(size_t body_len) {
  if (body_len == 0 || body_len > MAX_BODY) return 0;
  return (body_len + FRAGMENT_MAX - 1) / FRAGMENT_MAX;
}

size_t build_fragment(const Header& base, const uint8_t* body, size_t body_len, uint8_t index, uint8_t* out) {
  uint8_t count = frag_count_for(body_len);
  if (count == 0 || index >= count) return 0;
  Header h = base;
  h.frag_index = index;
  h.frag_count = count;
  encode_header(h, out);
  size_t offset = (size_t)index * FRAGMENT_MAX;
  size_t n = body_len - offset < FRAGMENT_MAX ? body_len - offset : FRAGMENT_MAX;
  memcpy(out + HEADER_SIZE, body + offset, n);
  return HEADER_SIZE + n;
}

bool Reassembler::push(const uint8_t* packet, size_t len, uint32_t now_ms, Header& h, const uint8_t*& body,
                       size_t& body_len) {
  const uint8_t* frag;
  size_t frag_len;
  if (!parse_packet(packet, len, h, frag, frag_len)) return false;

  for (Slot& s : slots_) {
    if (s.used && now_ms - s.started_ms > REASSEMBLY_TIMEOUT_MS) s.used = false;
  }

  if (h.frag_count == 1) {
    body = frag;
    body_len = frag_len;
    return true;
  }

  bool last = h.frag_index == h.frag_count - 1;
  if (!last && frag_len != FRAGMENT_MAX) return false;

  Slot* slot = nullptr;
  for (Slot& s : slots_) {
    if (s.used && s.source == h.source && s.message_id == h.message_id) slot = &s;
  }
  if (slot && slot->header.frag_count != h.frag_count) {
    slot->used = false;
    return false;
  }
  if (!slot) {
    for (Slot& s : slots_) {
      if (!s.used) {
        slot = &s;
        break;
      }
    }
    if (!slot) {
      slot = &slots_[0];
      for (Slot& s : slots_) {
        if (now_ms - s.started_ms > now_ms - slot->started_ms) slot = &s;
      }
    }
    slot->used = true;
    slot->source = h.source;
    slot->message_id = h.message_id;
    slot->started_ms = now_ms;
    slot->header = h;
    slot->received = 0;
  }

  memcpy(slot->data + (size_t)h.frag_index * FRAGMENT_MAX, frag, frag_len);
  if (last) slot->last_len = frag_len;
  slot->received |= (uint16_t)(1u << h.frag_index);
  if (slot->received != (uint16_t)((1u << h.frag_count) - 1)) return false;

  slot->used = false;
  h = slot->header;
  body = slot->data;
  body_len = (size_t)(h.frag_count - 1) * FRAGMENT_MAX + slot->last_len;
  return true;
}

bool SeenCache::check_and_add(const Header& h) {
  for (size_t i = 0; i < count_; i++) {
    const Entry& e = entries_[i];
    if (e.source == h.source && e.message_id == h.message_id && e.frag_index == h.frag_index) return true;
  }
  entries_[next_] = {h.source, h.message_id, h.frag_index};
  next_ = (next_ + 1) % SEEN_CACHE_SIZE;
  if (count_ < SEEN_CACHE_SIZE) count_++;
  return false;
}

bool forward_ttl(uint8_t* packet, size_t len) {
  if (len < HEADER_SIZE || packet[3] <= 1) return false;
  packet[3]--;
  return true;
}

}  // namespace kinjo
