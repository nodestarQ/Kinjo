#include <string.h>

#include "kinjo.h"

namespace kinjo {

size_t text_plaintext(const char* text, size_t text_len, uint8_t* out) {
  if (text_len > MAX_TEXT) return 0;
  out[0] = KIND_TEXT;
  memcpy(out + 1, text, text_len);
  return text_len + 1;
}

size_t identity_body(const uint8_t pub[KEY_SIZE], const char* name, size_t name_len, uint8_t* out) {
  if (name_len > MAX_NAME) return 0;
  memcpy(out, pub, KEY_SIZE);
  memcpy(out + KEY_SIZE, name, name_len);
  return KEY_SIZE + name_len;
}

bool parse_identity(const uint8_t* body, size_t len, const uint8_t*& pub, const char*& name, size_t& name_len) {
  if (len < KEY_SIZE || len > IDENTITY_MAX) return false;
  pub = body;
  name = (const char*)body + KEY_SIZE;
  name_len = len - KEY_SIZE;
  return true;
}

// Rounds toward minus infinity, like Python's //.
static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static int iabs(int v) { return v < 0 ? -v : v; }

DrawingEncoder::DrawingEncoder(uint8_t* out, size_t capacity) : out_(out), cap_(capacity) {
  if (cap_ < 1) {
    failed_ = true;
    return;
  }
  out_[0] = KIND_DRAWING;
}

bool DrawingEncoder::start(int x, int y) {
  if (len_ + 5 > cap_) return false;
  count_pos_ = len_;
  out_[len_++] = 1;
  out_[len_++] = x & 0xFF;
  out_[len_++] = x >> 8;
  out_[len_++] = y & 0xFF;
  out_[len_++] = y >> 8;
  x_ = x;
  y_ = y;
  return true;
}

bool DrawingEncoder::append_step(int x, int y) {
  if (out_[count_pos_] == 255 && !start(x_, y_)) return false;
  if (len_ + 2 > cap_) return false;
  out_[len_++] = (uint8_t)(int8_t)(x - x_);
  out_[len_++] = (uint8_t)(int8_t)(y - y_);
  out_[count_pos_]++;
  x_ = x;
  y_ = y;
  return true;
}

bool DrawingEncoder::begin_stroke(int x, int y) {
  if (failed_) return false;
  if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H || !start(x, y)) {
    failed_ = true;
    return false;
  }
  in_stroke_ = true;
  return true;
}

bool DrawingEncoder::add_point(int x, int y) {
  if (failed_ || !in_stroke_ || x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) {
    failed_ = true;
    return false;
  }
  int ax = x_, ay = y_;
  int dx = x - ax, dy = y - ay;
  int big = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  int n = (big + 126) / 127;
  if (n < 1) n = 1;
  for (int i = 1; i <= n; i++) {
    if (!append_step(ax + floor_div(dx * i, n), ay + floor_div(dy * i, n))) {
      failed_ = true;
      return false;
    }
  }
  return true;
}

bool decode_drawing(const uint8_t* p, size_t len, void (*point)(void* ctx, bool new_stroke, int x, int y), void* ctx) {
  if (len < 1 || p[0] != KIND_DRAWING) return false;
  size_t i = 1;
  while (i < len) {
    size_t count = p[i];
    if (count == 0 || i + 5 + 2 * (count - 1) > len) return false;
    int x = p[i + 1] | p[i + 2] << 8;
    int y = p[i + 3] | p[i + 4] << 8;
    i += 5;
    point(ctx, true, x, y);
    for (size_t k = 1; k < count; k++) {
      x += (int8_t)p[i];
      y += (int8_t)p[i + 1];
      i += 2;
      point(ctx, false, x, y);
    }
  }
  return true;
}

}  // namespace kinjo
