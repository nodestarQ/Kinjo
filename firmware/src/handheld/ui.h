// Screen, touch and buttons of the handheld (MSP2807 via TFT_eSPI, landscape 320 x 240).
// Same look as the web app: grey console, blue-grey title bar, lined paper strips with a
// colored name tab per sender (same colors as web/app/src/lib/kinjo/tabs.ts).
//
// Messages screen: the last messages as strips, newest at the bottom. Drawings as previews.
// Note screen: lined paper to draw and write on. Toolbar at the right: color, ABC, clear, send.
// SEND sends what is on the note: text, drawing or both (a NOTE message).
// Palette screen: opened from the color button, 8 pen colors to pick from.
// View screen: tap a drawing in the chat to see it full screen. The X closes it.
// Keyboard screen: opened from the ABC button. The text goes onto the note, DONE returns to it.
//
// Buttons (INPUT_PULLUP, pressed = LOW):
//   MESSAGES  switch between the two screens
//   ROOM      next contact as recipient
//   SEND      messages screen: send "gm". Draw screen: send the drawing (same as the SEND button on screen).
#pragma once

#include <TFT_eSPI.h>
#include <kinjo.h>

#include "node.h"

namespace ui {

using namespace kinjo;

constexpr int PIN_MESSAGES = 25;
constexpr int PIN_ROOM = 32;
constexpr int PIN_SEND = 33;
constexpr uint32_t DEBOUNCE_MS = 40;

constexpr int W = 320, H = 240;
constexpr int HEADER_H = 20;
constexpr int TAB_H = 13;
constexpr int RULE = 12;  // line spacing of the paper
constexpr int THUMB_W = 96, THUMB_H = 72;
constexpr int STRIP_W = 272;  // own messages on the left, others on the right

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t CONSOLE = rgb(0xE9, 0xED, 0xF1);
constexpr uint16_t FRAME = rgb(0x8E, 0xA0, 0xB3);
constexpr uint16_t FRAME_DARK = rgb(0x5F, 0x72, 0x86);
constexpr uint16_t PAPER = rgb(0xFC, 0xFD, 0xFE);
constexpr uint16_t RULE_COLOR = rgb(0xD7, 0xE2, 0xEC);
constexpr uint16_t INK = rgb(0x26, 0x31, 0x3D);
constexpr uint16_t GREY_TAB = rgb(0x9A, 0xA5, 0xB1);
constexpr uint16_t FAIL = rgb(0xC0, 0x39, 0x2B);

// Same list and hash (FNV-1a) as web/app/src/lib/kinjo/tabs.ts, so a device has the same color everywhere.
constexpr uint16_t TAB_COLORS[] = {rgb(0x3D, 0x6F, 0xD6), rgb(0x2F, 0x9E, 0x5B), rgb(0xE0, 0x83, 0x2F),
                                   rgb(0xD4, 0x54, 0x8E), rgb(0x8A, 0x57, 0xD6), rgb(0x1F, 0x9A, 0xA8),
                                   rgb(0xC9, 0xA2, 0x27), rgb(0xD8, 0x45, 0x3B), rgb(0x5B, 0x8C, 0x2A),
                                   rgb(0x2B, 0x4F, 0xA0), rgb(0xB0, 0x56, 0x9E), rgb(0x7A, 0x5C, 0x3E)};

// Pen palette, SPEC §9. Index 0 is ink.
constexpr uint16_t PEN_COLORS[PALETTE_SIZE] = {INK,
                                               rgb(0xD8, 0x45, 0x3B),
                                               rgb(0xE0, 0x83, 0x2F),
                                               rgb(0xD9, 0xB9, 0x2B),
                                               rgb(0x2F, 0x9E, 0x5B),
                                               rgb(0x3D, 0x6F, 0xD6),
                                               rgb(0x8A, 0x57, 0xD6),
                                               rgb(0xD4, 0x54, 0x8E)};

// Touch calibration from the hardware test (rotation 1). See docs/hardware.md.
static uint16_t CAL_DATA[5] = {470, 3255, 371, 2826, 7};

static TFT_eSPI tft;
static DeviceState* state = nullptr;

enum class Screen { Messages, Draw, Palette, View, Keyboard };
static Screen screen = Screen::Messages;
static uint8_t recipient = 0;

// --- message history ---

constexpr int MAX_MSGS = 8;
constexpr int DRAWING_SLOTS = 4;

struct Msg {
  char name[40];  // short name shown in the tab
  uint16_t color;
  bool outgoing;
  bool failed;
  char text[96];
  int8_t drawing;  // slot in `drawings` (-1: none)
};
static Msg msgs[MAX_MSGS];
static uint8_t msg_count = 0;
static uint8_t drawings[DRAWING_SLOTS][MAX_PLAINTEXT];
static size_t drawing_len[DRAWING_SLOTS];
static uint8_t next_slot = 0;

// Toolbar at the right edge of the draw screen.
constexpr int TOOL_W = 48;
constexpr int TOOL_X = W - TOOL_W;
constexpr int TOOL_COLOR_Y = HEADER_H;        // color button
constexpr int TOOL_ABC_Y = HEADER_H + 50;     // keyboard button
constexpr int TOOL_CLEAR_Y = HEADER_H + 100;  // clear button
constexpr int TOOL_SEND_Y = HEADER_H + 150;   // send button, to the bottom

// Keyboard screen: a text strip, then 5 rows of keys.
constexpr int KB_TEXT_H = 34;
constexpr int KB_TOP = HEADER_H + KB_TEXT_H;
constexpr int KB_ROWS = 5;
constexpr int KEY_H = (H - KB_TOP) / KB_ROWS;
constexpr int KEY_W = W / 10;
static const char* const KB_LETTERS[4] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?"};
static char kb_text[MAX_TEXT + 1];
static size_t kb_len = 0;
static bool kb_shift = false;
constexpr uint16_t ACCENT = rgb(0x3D, 0x6F, 0xD6);
// Palette screen: 4 x 2 color fields.
constexpr int GAP = 8;
constexpr int CELL_W = (W - 5 * GAP) / 4;
constexpr int CELL_H = (H - HEADER_H - 3 * GAP) / 2;
static uint8_t pen_color = 0;
static bool touch_down = false;  // for taps: act once per touch

// Where the drawings are on the messages screen, for taps.
struct Hit {
  int y0, y1;
  uint8_t msg;
};
static Hit hits[MAX_MSGS];
static uint8_t hit_count = 0;
constexpr int CLOSE_W = 44;  // X button of the view screen

static uint8_t canvas[MAX_PLAINTEXT];
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

static uint16_t tab_color(const char* name, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) h = (h ^ (uint8_t)name[i]) * 0x01000193;
  return TAB_COLORS[h % (sizeof(TAB_COLORS) / sizeof(TAB_COLORS[0]))];
}

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

static Msg& new_msg(const char* full_name, size_t name_len, bool outgoing) {
  if (msg_count == MAX_MSGS) {
    if (msgs[0].drawing >= 0) drawing_len[msgs[0].drawing] = 0;
    memmove(msgs, msgs + 1, sizeof(Msg) * (MAX_MSGS - 1));
    msg_count--;
  }
  Msg& m = msgs[msg_count++];
  memset(&m, 0, sizeof(m));
  short_name(full_name, name_len, m.name, sizeof(m.name));
  m.color = tab_color(full_name, name_len);
  m.outgoing = outgoing;
  m.drawing = -1;
  return m;
}

// Keeps a drawing (its items, without the kind byte) for previews. Stored as a DRAWING plaintext.
static void store_items(Msg& m, const uint8_t* items, size_t len) {
  if (!len) return;
  uint8_t slot = next_slot;
  next_slot = (next_slot + 1) % DRAWING_SLOTS;
  for (uint8_t i = 0; i < msg_count; i++) {
    if (msgs[i].drawing == (int8_t)slot) msgs[i].drawing = -1;  // older message loses its preview
  }
  drawings[slot][0] = KIND_DRAWING;
  memcpy(drawings[slot] + 1, items, len);
  drawing_len[slot] = len + 1;
  m.drawing = slot;
}

static void set_text(Msg& m, const char* text, size_t len) {
  size_t n = len < sizeof(m.text) - 1 ? len : sizeof(m.text) - 1;
  memcpy(m.text, text, n);
  m.text[n] = 0;
}

static void paper(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, PAPER);
  for (int ly = y + RULE; ly < y + h; ly += RULE) tft.drawFastHLine(x, ly, w, RULE_COLOR);
}

static void name_tab(int x, int y, const char* name, uint16_t color) {
  tft.setTextFont(1);
  int w = tft.textWidth(name) + 8;
  tft.fillRect(x, y, w, TAB_H, color);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawString(name, x + 4, y + 3);
}

static void title_bar(const char* left) {
  tft.fillRect(0, 0, W, HEADER_H, FRAME);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, FRAME);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(left, 6, 2);
  char to[48] = "no contacts yet";
  if (const Contact* c = current_contact()) {
    char name[40];
    short_name(c->name, c->name_len, name, sizeof(name));
    snprintf(to, sizeof(to), "to %s", name);
  }
  tft.setTextDatum(TR_DATUM);
  tft.drawString(to, W - 6, 2);
  tft.setTextDatum(TL_DATUM);
}

// --- drawing previews ---

// The preview shows the part that was drawn in, scaled by one factor for both axes.
struct Bounds {
  int x0 = W, y0 = H, x1 = 0, y1 = 0;
};
static void bounds_point(void* ctx, bool, int x, int y, uint8_t) {
  Bounds& b = *(Bounds*)ctx;
  if (x < b.x0) b.x0 = x;
  if (y < b.y0) b.y0 = y;
  if (x > b.x1) b.x1 = x;
  if (y > b.y1) b.y1 = y;
}

struct Thumb {
  int x, y;    // top left of the preview on screen
  int ox, oy;  // top left of the drawn area on the canvas
  int num, den;  // scale = num / den
  int px, py;
};
static void thumb_point(void* ctx, bool new_stroke, int x, int y, uint8_t color) {
  Thumb& t = *(Thumb*)ctx;
  int sx = t.x + (x - t.ox) * t.num / t.den, sy = t.y + (y - t.oy) * t.num / t.den;
  if (!new_stroke) tft.drawLine(t.px, t.py, sx, sy, PEN_COLORS[color]);
  else tft.drawPixel(sx, sy, PEN_COLORS[color]);
  t.px = sx;
  t.py = sy;
}

// --- messages screen ---

// Size of a drawing preview: the drawn area, scaled by one factor to fit, never above 1:1.
struct Preview {
  Bounds b;
  int num = 1, den = 1;
  int w = 0, h = 0;
};
static Preview preview_of(const Msg& m) {
  Preview p;
  decode_drawing(drawings[m.drawing], drawing_len[m.drawing], bounds_point, &p.b);
  int bw = p.b.x1 - p.b.x0 + 1, bh = p.b.y1 - p.b.y0 + 1;
  int max_w = STRIP_W - 12, max_h = THUMB_H;
  if (bw > max_w || bh > max_h) {
    if (bw * max_h > bh * max_w) p.num = max_w, p.den = bw;
    else p.num = max_h, p.den = bh;
  }
  p.w = bw * p.num / p.den;
  p.h = bh * p.num / p.den;
  return p;
}

static int text_lines(const Msg& m) {
  tft.setTextFont(2);
  return tft.textWidth(m.text) > STRIP_W - 12 ? 2 : 1;
}

// Strips fit their content, like chat bubbles. At least as wide as the name tab.
static void strip_size(const Msg& m, int& w, int& h) {
  tft.setTextFont(1);
  int tab_w = tft.textWidth(m.name) + 8;
  w = 0;
  h = 8;
  if (m.text[0]) {
    tft.setTextFont(2);
    int lines = text_lines(m);
    w = lines == 1 ? tft.textWidth(m.text) + 12 : STRIP_W;
    h += lines * 16;
  }
  if (m.drawing >= 0) {
    Preview p = preview_of(m);
    if (p.w + 12 > w) w = p.w + 12;
    h += p.h + (m.text[0] ? 4 : 0);
  }
  if (w < tab_w) w = tab_w;
  if (w > STRIP_W) w = STRIP_W;
}

static int strip_height(const Msg& m) {
  int w, h;
  strip_size(m, w, h);
  return h;
}

static void draw_text(const Msg& m, int x, int y, int w) {
  tft.setTextFont(2);
  tft.setTextColor(m.failed ? FAIL : INK, PAPER);
  // Up to two lines, cut at a space where possible.
  size_t len = strlen(m.text), start = 0;
  for (int line = 0; line < 2 && start < len; line++) {
    size_t end = len;
    char buf[96];
    for (;;) {
      memcpy(buf, m.text + start, end - start);
      buf[end - start] = 0;
      if (tft.textWidth(buf) <= w || end <= start + 1) break;
      size_t cut = end - 1;
      while (cut > start && m.text[cut] != ' ') cut--;
      end = cut > start ? cut : end - 1;
    }
    tft.drawString(buf, x, y + line * 16);
    start = end;
    while (start < len && m.text[start] == ' ') start++;
  }
}

static void draw_msg(const Msg& m, int y) {
  uint16_t color = m.failed ? GREY_TAB : m.color;
  int sw, sh;
  strip_size(m, sw, sh);
  int x = m.outgoing ? 4 : W - 4 - sw;  // own on the left, others on the right
  tft.setTextFont(1);
  int tab_w = tft.textWidth(m.name) + 8;
  name_tab(m.outgoing ? x : x + sw - tab_w, y, m.name, color);
  int sy = y + TAB_H;
  tft.fillRect(x, sy, sw, sh, PAPER);  // plain: only the draw screen is ruled
  tft.drawRect(x, sy, sw, sh, color);
  tft.drawRect(x + 1, sy + 1, sw - 2, sh - 2, color);
  int ty = sy + 4;
  if (m.text[0]) {
    draw_text(m, x + 6, ty, sw - 12);
    ty += text_lines(m) * 16 + 4;
  }
  if (m.drawing >= 0) {
    Preview p = preview_of(m);
    Thumb t = {x + 6, ty, p.b.x0, p.b.y0, p.num, p.den, 0, 0};
    // Ruled like the draw screen, at the same scale.
    for (int ly = RULE - (p.b.y0 % RULE); ly * p.num / p.den < p.h; ly += RULE) {
      tft.drawFastHLine(t.x, t.y + ly * p.num / p.den, p.w, RULE_COLOR);
    }
    decode_drawing(drawings[m.drawing], drawing_len[m.drawing], thumb_point, &t);
  }
}

static void show_messages() {
  tft.fillScreen(CONSOLE);
  title_bar("ROOM kinjo.eth");
  if (!msg_count) {
    tft.setTextFont(2);
    tft.setTextColor(FRAME_DARK, CONSOLE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(state->contact_count ? "Nobody has said anything yet." : "Set me up in the Kinjo web app.", W / 2, H / 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  // Newest at the bottom, as many as fit.
  int y = H - 2;
  hit_count = 0;
  for (int i = msg_count - 1; i >= 0; i--) {
    int h = TAB_H + strip_height(msgs[i]);
    if (y - h < HEADER_H + 2) break;
    y -= h;
    draw_msg(msgs[i], y);
    if (msgs[i].drawing >= 0) hits[hit_count++] = {y, y + h, (uint8_t)i};
    y -= 4;
  }
}

// --- draw screen ---

static void reset_canvas() {
  static uint8_t encoder_mem[sizeof(DrawingEncoder)];
  encoder = new (encoder_mem) DrawingEncoder(canvas, sizeof(canvas));
  encoder->set_color(pen_color);
  in_stroke = false;
}

static void pen(int x0, int y0, int x1, int y1, uint16_t c) {
  tft.drawLine(x0, y0, x1, y1, c);
  tft.drawLine(x0 + 1, y0, x1 + 1, y1, c);
  tft.drawLine(x0, y0 + 1, x1, y1 + 1, c);
}

static void tool_button(int y, int h, uint16_t fill, const char* label, uint16_t text) {
  tft.fillRect(TOOL_X, y, TOOL_W, h, fill);
  tft.drawRect(TOOL_X, y, TOOL_W, h, FRAME_DARK);
  tft.setTextFont(2);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, TOOL_X + TOOL_W / 2, y + h / 2);
  tft.setTextDatum(TL_DATUM);
}

static void draw_toolbar() {
  tft.fillRect(TOOL_X, TOOL_COLOR_Y, TOOL_W, 50, CONSOLE);
  tft.fillRect(TOOL_X + 8, TOOL_COLOR_Y + 8, TOOL_W - 16, 34, PEN_COLORS[pen_color]);  // current color
  tft.drawRect(TOOL_X + 8, TOOL_COLOR_Y + 8, TOOL_W - 16, 34, FRAME_DARK);
  tft.drawRect(TOOL_X, TOOL_COLOR_Y, TOOL_W, 50, FRAME_DARK);
  tool_button(TOOL_ABC_Y, 50, TFT_WHITE, "ABC", INK);
  tool_button(TOOL_CLEAR_Y, 50, TFT_WHITE, "CLR", INK);
  tool_button(TOOL_SEND_Y, H - TOOL_SEND_Y, ACCENT, "SEND", TFT_WHITE);
}

// Redraws what is already in the canvas, after the palette screen covered it.
static int redraw_x, redraw_y;
static void redraw_point(void*, bool new_stroke, int x, int y, uint8_t color) {
  if (new_stroke) tft.fillCircle(x, y, 1, PEN_COLORS[color]);
  else pen(redraw_x, redraw_y, x, y, PEN_COLORS[color]);
  redraw_x = x;
  redraw_y = y;
}

static void show_canvas(bool keep) {
  paper(0, HEADER_H, TOOL_X, H - HEADER_H);
  draw_toolbar();
  title_bar("NOTE");
  char own[40];
  short_name(state->name, state->name_len, own, sizeof(own));
  if (state->name_len) name_tab(0, HEADER_H, own, tab_color(state->name, state->name_len));
  if (kb_len) {  // the text written with the keyboard sits at the top of the note
    tft.setTextFont(2);
    tft.setTextColor(INK, PAPER);
    const char* shown = kb_text;
    while (*shown && tft.textWidth(shown) > TOOL_X - 8) shown++;
    tft.drawString(shown, 4, HEADER_H + TAB_H + 2);
  }
  if (keep && encoder) {
    decode_drawing(canvas, encoder->length(), redraw_point, nullptr);
    encoder->set_color(pen_color);
  } else {
    reset_canvas();
  }
}

static uint8_t viewing = 0;  // message shown on the view screen

static void show_view() {
  const Msg& m = msgs[viewing];
  paper(0, HEADER_H, W, H - HEADER_H);
  tft.fillRect(0, 0, W, HEADER_H, m.color);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, m.color);
  tft.drawString(m.name, 6, 2);
  tft.fillRect(W - CLOSE_W, 0, CLOSE_W, HEADER_H + 10, FRAME_DARK);  // X button
  tft.setTextColor(TFT_WHITE, FRAME_DARK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("X", W - CLOSE_W / 2, (HEADER_H + 10) / 2);
  tft.setTextDatum(TL_DATUM);
  if (m.text[0]) {
    tft.setTextColor(INK, PAPER);
    draw_text(m, 6, HEADER_H + 14, W - 12);
  }
  if (m.drawing >= 0) decode_drawing(drawings[m.drawing], drawing_len[m.drawing], redraw_point, nullptr);
}

// --- keyboard screen ---

static void kb_draw_text() {
  uint16_t own = tab_color(state->name, state->name_len);
  tft.fillRect(0, HEADER_H, W, KB_TEXT_H, CONSOLE);
  tft.fillRect(4, HEADER_H + 4, W - 8, KB_TEXT_H - 8, PAPER);
  tft.drawRect(4, HEADER_H + 4, W - 8, KB_TEXT_H - 8, own);
  tft.drawRect(5, HEADER_H + 5, W - 10, KB_TEXT_H - 10, own);
  tft.setTextFont(2);
  tft.setTextColor(INK, PAPER);
  // Show the end of the text if it's longer than the strip, with a cursor.
  const char* shown = kb_text;
  while (*shown && tft.textWidth(shown) > W - 24) shown++;
  tft.drawString(shown, 10, HEADER_H + 9);
  int cx = 10 + tft.textWidth(shown);
  tft.fillRect(cx + 1, HEADER_H + 9, 2, 16, own);
}

static void kb_key(int col, int row, int span, const char* label, uint16_t fill, uint16_t text) {
  int x = col * KEY_W, y = KB_TOP + row * KEY_H;
  tft.fillRect(x + 1, y + 1, span * KEY_W - 2, KEY_H - 2, fill);
  tft.drawRect(x + 1, y + 1, span * KEY_W - 2, KEY_H - 2, FRAME);
  tft.setTextFont(2);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + span * KEY_W / 2, y + KEY_H / 2);
  tft.setTextDatum(TL_DATUM);
}

static void kb_draw_keys() {
  tft.fillRect(0, KB_TOP, W, H - KB_TOP, CONSOLE);
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 10; col++) {
      char label[2] = {KB_LETTERS[row][col], 0};
      if (kb_shift && label[0] >= 'a' && label[0] <= 'z') label[0] -= 32;
      kb_key(col, row, 1, label, TFT_WHITE, INK);
    }
  }
  // Bottom row: shift (2), space (4), del (2), send (2)
  kb_key(0, 4, 2, "SHIFT", kb_shift ? FRAME_DARK : TFT_WHITE, kb_shift ? TFT_WHITE : INK);
  kb_key(2, 4, 4, "space", TFT_WHITE, INK);
  kb_key(6, 4, 2, "DEL", TFT_WHITE, INK);
  kb_key(8, 4, 2, "DONE", ACCENT, TFT_WHITE);
}

static void show_keyboard() {
  title_bar("WRITE");
  kb_draw_text();
  kb_draw_keys();
}

static void show_palette() {
  tft.fillScreen(CONSOLE);
  title_bar("PICK A COLOR");
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    int x = GAP + (i % 4) * (CELL_W + GAP), y = HEADER_H + GAP + (i / 4) * (CELL_H + GAP);
    tft.fillRect(x, y, CELL_W, CELL_H, PEN_COLORS[i]);
    tft.drawRect(x, y, CELL_W, CELL_H, FRAME_DARK);
    if (i == pen_color) {
      tft.drawRect(x + 4, y + 4, CELL_W - 8, CELL_H - 8, TFT_WHITE);
      tft.drawRect(x + 5, y + 5, CELL_W - 10, CELL_H - 10, TFT_WHITE);
    }
  }
}

static void switch_to(Screen s) {
  Screen from = screen;
  screen = s;
  if (s == Screen::Messages) show_messages();
  else if (s == Screen::Palette) show_palette();
  else if (s == Screen::View) show_view();
  else if (s == Screen::Keyboard) show_keyboard();
  else show_canvas(from == Screen::Palette);  // coming back from the palette keeps the drawing
}

// --- public ---

inline void begin(DeviceState& s) {
  state = &s;
  for (Button& b : buttons) pinMode(b.pin, INPUT_PULLUP);
  tft.init();
  tft.setRotation(1);
  tft.setTouch(CAL_DATA);
  show_messages();
}

/** Call after provisioning changed the state (name, contacts). */
inline void refresh() { switch_to(screen); }

inline void on_message(const Contact& from, const uint8_t* pt, size_t len) {
  Msg& m = new_msg(from.name, from.name_len, false);
  if (pt[0] == KIND_TEXT) {
    size_t n = len - 1 < sizeof(m.text) - 1 ? len - 1 : sizeof(m.text) - 1;
    memcpy(m.text, pt + 1, n);
  } else if (pt[0] == KIND_DRAWING) {
    store_items(m, pt + 1, len - 1);
  } else if (pt[0] == KIND_NOTE) {
    const char* text;
    size_t text_len, items_len;
    const uint8_t* items;
    if (!parse_note(pt, len, text, text_len, items, items_len)) return;
    set_text(m, text, text_len);
    store_items(m, items, items_len);
  }
  if (screen == Screen::Messages) show_messages();
}

// `drawing` is a DRAWING plaintext (or null).
static void add_own(const char* text, size_t text_len, bool failed, const uint8_t* drawing, size_t drawing_len_) {
  Msg& m = new_msg(state->name, state->name_len, true);
  m.failed = failed;
  if (text) set_text(m, text, text_len);
  if (drawing && drawing_len_ > 1) store_items(m, drawing + 1, drawing_len_ - 1);
}

static uint8_t note_buf[MAX_PLAINTEXT];

/** Sends the note: only text, only the drawing, or both as one NOTE. Then back to the chat. */
static void send_note() {
  const Contact* to = current_contact();
  size_t dlen = encoder->length();
  bool has_drawing = dlen > 1, has_text = kb_len > 0;
  if (!to || (!has_drawing && !has_text)) return;
  bool ok;
  if (has_drawing && has_text) {
    size_t n = note_plaintext(kb_text, kb_len, canvas, dlen, note_buf);
    ok = n && node::send(*to, note_buf, n);
  } else if (has_drawing) {
    ok = node::send(*to, canvas, dlen);
  } else {
    ok = node::send_text(*to, kb_text, kb_len);
  }
  if (ok) add_own(has_text ? kb_text : nullptr, kb_len, false, has_drawing ? canvas : nullptr, dlen);
  else add_own("sending failed", 14, true, nullptr, 0);
  kb_len = 0;
  kb_text[0] = 0;
  switch_to(Screen::Messages);
}

static void send_gm() {
  const Contact* to = current_contact();
  if (!to) return;
  bool ok = node::send_text(*to, "gm", 2);
  add_own(ok ? "gm" : "sending failed", ok ? 2 : 14, !ok, nullptr, 0);
  show_messages();
}

static void on_press(int pin) {
  if (pin == PIN_MESSAGES) {
    switch_to(screen == Screen::Messages ? Screen::Draw : Screen::Messages);  // also leaves view and palette
  } else if (pin == PIN_ROOM) {
    if (state->contact_count) recipient = (recipient + 1) % state->contact_count;
    if (screen == Screen::Messages) title_bar("ROOM kinjo.eth");
    else if (screen == Screen::Draw) title_bar("NOTE");
  } else if (pin == PIN_SEND) {
    if (screen == Screen::Draw || screen == Screen::Keyboard) send_note();
    else if (screen == Screen::Messages) send_gm();
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

// Taps on the toolbar and the palette act once per touch. Strokes need the pen held down.
static void on_tap(int x, int y) {
  if (screen == Screen::Messages) {
    for (uint8_t i = 0; i < hit_count; i++) {
      if (y >= hits[i].y0 && y < hits[i].y1 && msgs[hits[i].msg].drawing >= 0) {
        viewing = hits[i].msg;
        switch_to(Screen::View);
        return;
      }
    }
    return;
  }
  if (screen == Screen::Keyboard) {
    if (y < KB_TOP) return;
    int row = (y - KB_TOP) / KEY_H, col = x / KEY_W;
    if (row < 4 && col < 10) {
      char c = KB_LETTERS[row][col];
      if (kb_shift && c >= 'a' && c <= 'z') c -= 32;
      if (kb_len < MAX_TEXT) kb_text[kb_len++] = c;
      kb_text[kb_len] = 0;
      if (kb_shift) {
        kb_shift = false;
        kb_draw_keys();
      }
      kb_draw_text();
    } else if (row == 4 && col < 2) {
      kb_shift = !kb_shift;
      kb_draw_keys();
    } else if (row == 4 && col < 6) {
      if (kb_len < MAX_TEXT) kb_text[kb_len++] = ' ';
      kb_text[kb_len] = 0;
      kb_draw_text();
    } else if (row == 4 && col < 8) {
      if (kb_len) kb_text[--kb_len] = 0;
      kb_draw_text();
    } else if (row == 4) {
      switch_to(Screen::Draw);  // back to the note, keeping text and drawing
    }
    return;
  }
  if (screen == Screen::View) {
    if (x >= W - CLOSE_W && y < HEADER_H + 10) switch_to(Screen::Messages);
    return;
  }
  if (screen == Screen::Palette) {
    for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
      int cx = GAP + (i % 4) * (CELL_W + GAP), cy = HEADER_H + GAP + (i / 4) * (CELL_H + GAP);
      if (x >= cx && x < cx + CELL_W && y >= cy && y < cy + CELL_H) {
        pen_color = i;
        switch_to(Screen::Draw);
        return;
      }
    }
    return;
  }
  if (y < TOOL_ABC_Y) switch_to(Screen::Palette);
  else if (y < TOOL_CLEAR_Y) switch_to(Screen::Keyboard);
  else if (y < TOOL_SEND_Y) {
    kb_len = 0;
    kb_text[0] = 0;
    show_canvas(false);
  }
  else send_note();
}

static void poll_touch(uint32_t now) {
  uint16_t x, y;
  bool touched = tft.getTouch(&x, &y);
  if (!touched) {
    if (in_stroke && now - last_touch_ms > 60) in_stroke = false;  // pen lifted
    if (now - last_touch_ms > 60) touch_down = false;
    return;
  }
  last_touch_ms = now;
  bool new_touch = !touch_down;
  touch_down = true;

  if (screen != Screen::Draw) {
    if (new_touch) on_tap(x, y);
    return;
  }
  if (x >= TOOL_X) {
    in_stroke = false;
    if (new_touch && y >= HEADER_H) on_tap(x, y);
    return;
  }
  if (y < HEADER_H + TAB_H) return;
  if (!in_stroke) {
    if (!encoder->begin_stroke(x, y)) return;  // canvas full
    in_stroke = true;
    tft.fillCircle(x, y, 1, PEN_COLORS[pen_color]);
  } else {
    int dx = x - last_x, dy = y - last_y;
    if (dx * dx + dy * dy < 9) return;  // ignore jitter under 3 px
    if (!encoder->add_point(x, y)) return;
    pen(last_x, last_y, x, y, PEN_COLORS[pen_color]);
  }
  last_x = x;
  last_y = y;
}

inline void loop(uint32_t now) {
  poll_buttons(now);
  poll_touch(now);
}

}  // namespace ui
