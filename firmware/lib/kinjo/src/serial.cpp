#include <string.h>

#include "kinjo.h"

namespace kinjo {

namespace {

struct CobsWriter {
  uint8_t* out;
  size_t code_pos = 0, n = 1;
  uint8_t code = 1;

  explicit CobsWriter(uint8_t* o) : out(o) {}

  void put(uint8_t b) {
    if (b != 0) {
      out[n++] = b;
      if (++code != 0xFF) return;
    }
    out[code_pos] = code;
    code_pos = n++;
    code = 1;
  }

  size_t finish() {
    out[code_pos] = code;
    return n;
  }
};

}  // namespace

size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out) {
  CobsWriter w(out);
  for (size_t i = 0; i < len; i++) w.put(in[i]);
  return w.finish();
}

size_t cobs_decode(const uint8_t* in, size_t len, uint8_t* out) {
  size_t i = 0, n = 0;
  while (i < len) {
    uint8_t code = in[i];
    if (code == 0 || i + code > len) return SIZE_MAX;
    for (size_t k = 1; k < code; k++) {
      if (in[i + k] == 0) return SIZE_MAX;
      out[n++] = in[i + k];
    }
    i += code;
    if (code != 0xFF && i < len) out[n++] = 0;
  }
  return n;
}

size_t serial_frame(uint8_t type, const uint8_t* body, size_t body_len, uint8_t* out) {
  CobsWriter w(out);
  w.put(type);
  for (size_t i = 0; i < body_len; i++) w.put(body[i]);
  size_t n = w.finish();
  out[n] = 0;
  return n + 1;
}

bool FrameReader::push(uint8_t byte, uint8_t& type, const uint8_t*& body, size_t& body_len) {
  if (byte != 0) {
    if (len_ < MAX_ENCODED) {
      buf_[len_++] = byte;
    } else {
      overflow_ = true;
    }
    return false;
  }
  size_t len = len_;
  bool overflow = overflow_;
  len_ = 0;
  overflow_ = false;
  if (len == 0 || overflow) return false;
  size_t n = cobs_decode(buf_, len, decoded_);
  if (n == SIZE_MAX || n == 0) return false;
  type = decoded_[0];
  body = decoded_ + 1;
  body_len = n - 1;
  return true;
}

}  // namespace kinjo
