// Screen, touch and buttons of the handheld (MSP2807 via TFT_eSPI, landscape 320 x 240).
//
// Messages screen: header with own name and recipient, the last messages below.
// Draw screen: full-screen canvas. Received drawings open here too.
//
// Buttons (INPUT_PULLUP, pressed = LOW):
//   MESSAGES  switch between the two screens
//   ROOM      next contact as recipient
//   SEND      draw screen: send the drawing. Messages screen: send "gm".
#pragma once

#include <TFT_eSPI.h>
#include <kinjo.h>

#include "node.h"

namespace ui {

using namespace kinjo;

constexpr int PIN_MESSAGES = 25;
constexpr int PIN_ROOM = 32;
constexpr int PIN_SEND = 33;
constexpr int HEADER_H = 20;
constexpr int LINE_H = 18;
constexpr int MAX_LINES = (240 - HEADER_H) / LINE_H;
constexpr uint32_t DEBOUNCE_MS = 40;

// Touch calibration from the hardware test (rotation 1). See docs/hardware.md.
static uint16_t CAL_DATA[5] = {470, 3255, 371, 2826, 7};

static TFT_eSPI tft;
static DeviceState* state = nullptr;

enum class Screen { Messages, Draw };
static Screen screen = Screen::Messages;
static uint8_t recipient = 0;

struct Line {
  char text[48];
  uint16_t color;
};
static Line lines[MAX_LINES];
static uint8_t line_count = 0;

static uint8_t drawing[MAX_PLAINTEXT];
static DrawingEncoder* encoder = nullptr;
static bool in_stroke = false;
static int last_x = 0, last_y = 0;
static uint32_t last_touch_ms = 0;

struct Button {
  int pin;
  bool down;
  uint32_t changed_ms;
};
static Button buttons[3] = {{PIN_MESSAGES, false, 0}, {PIN_ROOM, false, 0}, {PIN_SEND, false, 0}};

// --- helpers ---

// "handheld.alice.kinjo.eth" -> "handheld.alice"
static void short_name(const char* name, size_t len, char* out, size_t cap) {
  size_t n = len;
  const char* suffix = ".kinjo.eth";
  size_t sl = strlen(suffix);
  if (len > sl && memcmp(name + len - sl, suffix, sl) == 0) n = len - sl;
  if (n >= cap) n = cap - 1;
  memcpy(out, name, n);
  out[n] = 0;
}

static const Contact* current_contact() {
  if (!state->contact_count) return nullptr;
  if (recipient >= state->contact_count) recipient = 0;
  return &state->contacts[recipient];
}

static void add_line(uint16_t color, const char* fmt, ...) {
  if (line_count == MAX_LINES) {
    memmove(lines, lines + 1, sizeof(Line) * (MAX_LINES - 1));
    line_count--;
  }
  va_list args;
  va_start(args, fmt);
  vsnprintf(lines[line_count].text, sizeof(lines[0].text), fmt, args);
  va_end(args);
  lines[line_count].color = color;
  line_count++;
}

static void draw_header(const char* title) {
  tft.fillRect(0, 0, 320, HEADER_H, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  char own[40], to[40] = "no contacts";
  short_name(state->name, state->name_len, own, sizeof(own));
  if (const Contact* c = current_contact()) short_name(c->name, c->name_len, to, sizeof(to));
  tft.setCursor(4, 2);
  tft.printf("%s  %s", title, state->name_len ? own : "not set up");
  tft.setTextDatum(TR_DATUM);
  char right[48];
  snprintf(right, sizeof(right), "to %s", to);
  tft.drawString(right, 316, 2);
  tft.setTextDatum(TL_DATUM);
}

static void show_messages() {
  tft.fillScreen(TFT_BLACK);
  draw_header("MSG");
  for (uint8_t i = 0; i < line_count; i++) {
    tft.setTextColor(lines[i].color, TFT_BLACK);
    tft.setCursor(4, HEADER_H + 2 + i * LINE_H);
    tft.print(lines[i].text);
  }
  if (!line_count) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(4, HEADER_H + 4);
    tft.print(state->contact_count ? "No messages yet." : "Set me up in the Kinjo web app.");
  }
}

static void reset_canvas() {
  static uint8_t encoder_mem[sizeof(DrawingEncoder)];
  encoder = new (encoder_mem) DrawingEncoder(drawing, sizeof(drawing));
  in_stroke = false;
}

static void show_canvas() {
  tft.fillScreen(TFT_WHITE);
  draw_header("DRAW");
  reset_canvas();
}

static void switch_to(Screen s) {
  screen = s;
  if (s == Screen::Messages) show_messages();
  else show_canvas();
}

// --- drawing received ---

static int draw_prev_x, draw_prev_y;
static void draw_point(void*, bool new_stroke, int x, int y) {
  if (new_stroke) tft.fillCircle(x, y, 1, TFT_BLACK);
  else tft.drawLine(draw_prev_x, draw_prev_y, x, y, TFT_BLACK);
  draw_prev_x = x;
  draw_prev_y = y;
}

// --- public ---

inline void begin(DeviceState& s) {
  state = &s;
  for (Button& b : buttons) pinMode(b.pin, INPUT_PULLUP);
  tft.init();
  tft.setRotation(1);
  tft.setTouch(CAL_DATA);
  tft.setTextFont(2);
  show_messages();
}

/** Call after provisioning changed the state (name, contacts). */
inline void refresh() {
  if (screen == Screen::Messages) show_messages();
  else draw_header("DRAW");
}

inline void on_message(const Contact& from, const uint8_t* pt, size_t len) {
  char name[40];
  short_name(from.name, from.name_len, name, sizeof(name));
  uint16_t color = (from.flags & FLAG_VERIFIED) ? TFT_GREEN : TFT_WHITE;
  if (pt[0] == KIND_TEXT) {
    add_line(color, "%s: %.*s", name, (int)(len - 1), (const char*)pt + 1);
    if (screen == Screen::Messages) show_messages();
  } else if (pt[0] == KIND_DRAWING) {
    add_line(color, "%s: [drawing]", name);
    switch_to(Screen::Draw);  // show it on the canvas, keep drawing on top of it if you like
    char title[48];
    snprintf(title, sizeof(title), "from %s", name);
    draw_header(title);
    decode_drawing(pt, len, draw_point, nullptr);
  }
}

static void send_drawing() {
  const Contact* to = current_contact();
  size_t len = encoder->length();
  if (!to || len <= 1) return;
  char name[40];
  short_name(to->name, to->name_len, name, sizeof(name));
  bool ok = node::send(*to, drawing, len);
  add_line(ok ? TFT_CYAN : TFT_RED, ok ? "you -> %s: [drawing]" : "sending to %s failed", name);
  switch_to(Screen::Messages);
}

static void send_gm() {
  const Contact* to = current_contact();
  if (!to) return;
  char name[40];
  short_name(to->name, to->name_len, name, sizeof(name));
  bool ok = node::send_text(*to, "gm", 2);
  add_line(ok ? TFT_CYAN : TFT_RED, ok ? "you -> %s: gm" : "sending to %s failed", name);
  show_messages();
}

static void on_press(int pin) {
  if (pin == PIN_MESSAGES) {
    switch_to(screen == Screen::Messages ? Screen::Draw : Screen::Messages);
  } else if (pin == PIN_ROOM) {
    if (state->contact_count) recipient = (recipient + 1) % state->contact_count;
    draw_header(screen == Screen::Messages ? "MSG" : "DRAW");
  } else if (pin == PIN_SEND) {
    if (screen == Screen::Draw) send_drawing();
    else send_gm();
  }
}

static void poll_buttons(uint32_t now) {
  for (Button& b : buttons) {
    bool down = digitalRead(b.pin) == LOW;
    if (down == b.down || now - b.changed_ms < DEBOUNCE_MS) continue;
    b.down = down;
    b.changed_ms = now;
    if (down) on_press(b.pin);
  }
}

static void poll_touch(uint32_t now) {
  if (screen != Screen::Draw) return;
  uint16_t x, y;
  if (!tft.getTouch(&x, &y) || y < HEADER_H) {
    if (in_stroke && now - last_touch_ms > 60) in_stroke = false;  // pen lifted
    return;
  }
  last_touch_ms = now;
  if (!in_stroke) {
    if (!encoder->begin_stroke(x, y)) return;  // canvas full
    in_stroke = true;
    tft.fillCircle(x, y, 1, TFT_BLACK);
  } else {
    int dx = x - last_x, dy = y - last_y;
    if (dx * dx + dy * dy < 9) return;  // ignore jitter under 3 px
    if (!encoder->add_point(x, y)) return;
    tft.drawLine(last_x, last_y, x, y, TFT_BLACK);
  }
  last_x = x;
  last_y = y;
}

inline void loop(uint32_t now) {
  poll_buttons(now);
  poll_touch(now);
}

}  // namespace ui
