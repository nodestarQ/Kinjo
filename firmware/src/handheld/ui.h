// Screen, touch and buttons of the handheld (MSP2807 via TFT_eSPI, landscape 320 x 240).
// Same look as the web app: grey console, blue-grey title bar, lined paper strips with a
// colored name tab per sender (same colors as web/app/src/lib/kinjo/tabs.ts).
//
// Home: four tiles. Chat, Pay, Info (own name and key), Calibrate (touch).
// Contacts: everyone the web app put on this device. Tap one to open the chat with them.
//   Tap the right of the title bar to show only verified humans (World ID badge) or everyone.
// Chat: the last messages with that contact, newest at the bottom. WRITE A NOTE at the bottom.
// Note: lined paper to draw and write on. Toolbar at the right: color, ABC, clear, send.
//   SEND sends what is on the note: text, drawing or both (a NOTE message).
// Palette: opened from the color button, 8 pen colors to pick from.
// Keyboard: opened from ABC. The text goes onto the note, DONE returns to it.
// View: tap a message in the chat to see all of it full screen. The X closes it.
// "< Back" at the top left of every screen but Home goes back one screen, like the BACK button.
//
// Buttons (INPUT_PULLUP, pressed = LOW):
//   HOME   back to the home screen
//   POWER  hold to switch off (deep sleep), press to switch on again
//   BACK   back one screen
#pragma once

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <kinjo.h>

#include "node.h"
#include "storage.h"

namespace ui {

using namespace kinjo;

constexpr int PIN_HOME = 25;
constexpr int PIN_POWER = 32;  // an RTC pin, so it can wake the ESP32
constexpr int PIN_BACK = 33;
constexpr int PIN_BACKLIGHT = 4;  // optional: screen LED on this pin instead of 3V3 turns it dark when off
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t POWER_HOLD_MS = 800;

constexpr int W = 320, H = 240;
constexpr int HEADER_H = 20;
constexpr int TAB_H = 13;
constexpr int RULE = 12;  // line spacing of the paper
constexpr int THUMB_H = 72;
constexpr int STRIP_W = 272;  // own messages on the left, others on the right
constexpr int BACK_W = 60;    // "< Back" in the title bar
constexpr int WRITE_H = 30;   // WRITE A NOTE button of the chat

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
constexpr uint16_t ACCENT = rgb(0x3D, 0x6F, 0xD6);

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

// Touch calibration from the hardware test (rotation 1), until the Calibrate screen saves a new one.
static uint16_t cal_data[5] = {470, 3255, 371, 2826, 7};

static TFT_eSPI tft;
static DeviceState* state = nullptr;

enum class Screen { Home, Contacts, Chat, Note, Palette, Keyboard, View, Info, Pay };
static Screen screen = Screen::Home;
static uint8_t peer = 0;  // contact of the open chat
static uint8_t unread[MAX_CONTACTS];
static bool humans_only = false;  // contacts filter, kept in flash

// --- message history (all chats together, oldest dropped first) ---

constexpr int MAX_MSGS = 16;
constexpr int DRAWING_SLOTS = 4;

struct Msg {
  uint32_t peer;  // name hash of the other side
  char name[40];  // short name shown in the tab
  uint16_t color;
  bool outgoing;
  bool failed;
  bool human;  // sender has the World ID badge
  char text[MAX_TEXT + 1];
  int8_t drawing;  // slot in `drawings` (-1: none)
};
static Msg msgs[MAX_MSGS];
static uint8_t msg_count = 0;
static uint8_t drawings[DRAWING_SLOTS][MAX_PLAINTEXT];
static size_t drawing_len[DRAWING_SLOTS];
static uint8_t next_slot = 0;

// Home: 2 x 2 tiles.
constexpr int TILE_GAP = 10;
constexpr int TILE_W = (W - 3 * TILE_GAP) / 2;
constexpr int TILE_H = (H - HEADER_H - 3 * TILE_GAP) / 2;

// Contacts: one row per contact, a page at a time.
constexpr int ROW_H = 30;
constexpr int ROWS = (H - HEADER_H) / ROW_H;  // the last row turns into "more" when needed
static uint8_t contacts_page = 0;

// Toolbar at the right edge of the note.
constexpr int TOOL_W = 48;
constexpr int TOOL_X = W - TOOL_W;
constexpr int TOOL_COLOR_Y = HEADER_H;        // color button
constexpr int TOOL_ABC_Y = HEADER_H + 50;     // keyboard button
constexpr int TOOL_CLEAR_Y = HEADER_H + 100;  // clear button
constexpr int TOOL_SEND_Y = HEADER_H + 150;   // send button, to the bottom

// Keyboard: a text strip, then 5 rows of keys.
constexpr int KB_TEXT_H = 34;
constexpr int KB_TOP = HEADER_H + KB_TEXT_H;
constexpr int KB_ROWS = 5;
constexpr int KEY_H = (H - KB_TOP) / KB_ROWS;
constexpr int KEY_W = W / 10;
static const char* const KB_LETTERS[4] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?"};
static char kb_text[MAX_TEXT + 1];
static size_t kb_len = 0;
static bool kb_shift = false;
// Palette: 4 x 2 color fields.
constexpr int GAP = 8;
constexpr int CELL_W = (W - 5 * GAP) / 4;
constexpr int CELL_H = (H - HEADER_H - 3 * GAP) / 2;
static uint8_t pen_color = 0;
static bool touch_down = false;  // for taps: act once per touch

// Where the messages are on the chat screen, for taps.
struct Hit {
  int y0, y1;
  uint8_t msg;
};
static Hit hits[MAX_MSGS];
static uint8_t hit_count = 0;
constexpr int CLOSE_W = 44;  // X button of the view screen

// The note being written. It stays until it's sent or cleared, or the chat changes.
static uint8_t canvas[MAX_PLAINTEXT];
static DrawingEncoder* encoder = nullptr;
static int note_peer = -1;
static bool in_stroke = false;
static int last_x = 0, last_y = 0;
static uint32_t last_touch_ms = 0;

struct Button {
  int pin;
  bool down;
  uint32_t changed_ms;
  bool held;  // a long press already acted
};
static Button buttons[3] = {{PIN_HOME, false, 0, false}, {PIN_POWER, false, 0, false}, {PIN_BACK, false, 0, false}};

// --- helpers ---

static uint32_t name_hash(const char* name, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) h = (h ^ (uint8_t)name[i]) * 0x01000193;
  return h;
}

static uint16_t tab_color(const char* name, size_t len) {
  return TAB_COLORS[name_hash(name, len) % (sizeof(TAB_COLORS) / sizeof(TAB_COLORS[0]))];
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

static const Contact* chat_contact() {
  if (!state->contact_count) return nullptr;
  if (peer >= state->contact_count) peer = 0;
  return &state->contacts[peer];
}

static uint32_t contact_hash(const Contact& c) { return name_hash(c.name, c.name_len); }

// The badge belongs to the owner, so this device is verified if any device of the same owner is:
// "handheld.alice.kinjo.eth" is verified when e.g. "laptop.alice.kinjo.eth" has the flag.
static bool owner_verified() {
  const char* dot = (const char*)memchr(state->name, '.', state->name_len);
  if (!dot) return false;
  size_t suffix = state->name_len - (dot - state->name);  // ".alice.kinjo.eth"
  for (uint8_t i = 0; i < state->contact_count; i++) {
    const Contact& c = state->contacts[i];
    if ((c.flags & FLAG_VERIFIED) && !(c.flags & FLAG_REVOKED) && c.name_len > suffix && memcmp(c.name + c.name_len - suffix, dot, suffix) == 0) return true;
  }
  return false;
}

// Revoked contacts stay in flash but are never shown.
static bool visible(uint8_t i) {
  uint8_t f = state->contacts[i].flags;
  return !(f & FLAG_REVOKED) && (!humans_only || (f & FLAG_VERIFIED));
}

static Msg& new_msg(const Contact& other, const char* tab_name, size_t tab_len, bool outgoing) {
  if (msg_count == MAX_MSGS) {
    if (msgs[0].drawing >= 0) drawing_len[msgs[0].drawing] = 0;
    memmove(msgs, msgs + 1, sizeof(Msg) * (MAX_MSGS - 1));
    msg_count--;
  }
  Msg& m = msgs[msg_count++];
  memset(&m, 0, sizeof(m));
  m.peer = contact_hash(other);
  short_name(tab_name, tab_len, m.name, sizeof(m.name));
  m.color = tab_color(tab_name, tab_len);
  m.outgoing = outgoing;
  m.human = outgoing ? owner_verified() : (other.flags & FLAG_VERIFIED);
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

// The screen font is ASCII only: every other character (one UTF-8 sequence) shows as "?".
static void set_text(Msg& m, const char* text, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < len && n < sizeof(m.text) - 1; i++) {
    uint8_t c = text[i];
    if (c < 0x80) m.text[n++] = c;
    else if (c >= 0xC0) m.text[n++] = '?';  // start of a sequence; continuation bytes are skipped
  }
  m.text[n] = 0;
}

static void paper(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, PAPER);
  for (int ly = y + RULE; ly < y + h; ly += RULE) tft.drawFastHLine(x, ly, w, RULE_COLOR);
}

// A verified human gets a small white check mark at the end of the tab.
constexpr int CHECK_W = 9;
static int tab_width(const char* name, bool human) {
  tft.setTextFont(1);
  return tft.textWidth(name) + 8 + (human ? CHECK_W : 0);
}

static void name_tab(int x, int y, const char* name, uint16_t color, bool human = false) {
  int w = tab_width(name, human);
  tft.fillRect(x, y, w, TAB_H, color);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawString(name, x + 4, y + 3);
  if (human) {
    int cx = x + w - CHECK_W - 1, cy = y + 7;
    tft.drawLine(cx, cy, cx + 2, cy + 3, TFT_WHITE);
    tft.drawLine(cx + 2, cy + 3, cx + 7, cy - 3, TFT_WHITE);
    tft.drawLine(cx, cy - 1, cx + 2, cy + 2, TFT_WHITE);
    tft.drawLine(cx + 2, cy + 2, cx + 7, cy - 4, TFT_WHITE);
  }
}

// Title bar with an optional "<" (back) at the left and a note at the right.
static void title_bar(const char* left, const char* right = "", bool back = true, uint16_t fill = FRAME) {
  tft.fillRect(0, 0, W, HEADER_H, fill);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, fill);
  tft.setTextDatum(TL_DATUM);
  int x = 6;
  if (back) {
    tft.fillRect(0, 0, BACK_W - 6, HEADER_H, FRAME_DARK);
    tft.setTextColor(TFT_WHITE, FRAME_DARK);
    tft.drawString("< Back", 6, 2);
    tft.setTextColor(TFT_WHITE, fill);
    x = BACK_W;
  }
  tft.drawString(left, x, 2);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(right, W - 6, 2);
  tft.setTextDatum(TL_DATUM);
}

static void centered(const char* text, int y, uint16_t color = FRAME_DARK, uint16_t bg = CONSOLE) {
  tft.setTextFont(2);
  tft.setTextColor(color, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, W / 2, y);
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

// --- home screen ---

static void tile_frame(int i, const char* label) {
  int x = TILE_GAP + (i % 2) * (TILE_W + TILE_GAP), y = HEADER_H + TILE_GAP + (i / 2) * (TILE_H + TILE_GAP);
  tft.fillRect(x, y, TILE_W, TILE_H, PAPER);
  tft.drawRect(x, y, TILE_W, TILE_H, FRAME_DARK);
  tft.drawRect(x + 1, y + 1, TILE_W - 2, TILE_H - 2, FRAME_DARK);
  tft.setTextFont(2);
  tft.setTextColor(INK, PAPER);
  tft.setTextDatum(BC_DATUM);
  tft.drawString(label, x + TILE_W / 2, y + TILE_H - 6);
  tft.setTextDatum(TL_DATUM);
}

static void tile_center(int i, int& cx, int& cy) {
  cx = TILE_GAP + (i % 2) * (TILE_W + TILE_GAP) + TILE_W / 2;
  cy = HEADER_H + TILE_GAP + (i / 2) * (TILE_H + TILE_GAP) + TILE_H / 2 - 10;
}

static void show_home() {
  tft.fillScreen(CONSOLE);
  char own[40] = "not set up yet";
  if (state->name_len) short_name(state->name, state->name_len, own, sizeof(own));
  title_bar(own, "kinjo", false);
  int cx, cy;

  tile_frame(0, "Chat");  // speech bubble
  tile_center(0, cx, cy);
  tft.fillRoundRect(cx - 26, cy - 18, 52, 32, 6, ACCENT);
  tft.fillTriangle(cx - 14, cy + 13, cx - 4, cy + 13, cx - 18, cy + 24, ACCENT);
  for (int d = -12; d <= 12; d += 12) tft.fillCircle(cx + d, cy - 2, 3, TFT_WHITE);
  uint16_t total = 0;
  for (uint8_t i = 0; i < state->contact_count; i++) total += visible(i) ? unread[i] : 0;
  if (total) {
    char n[6];
    snprintf(n, sizeof(n), "%u", total);
    tft.fillCircle(cx + 28, cy - 18, 10, FAIL);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, FAIL);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(n, cx + 28, cy - 18);
    tft.setTextDatum(TL_DATUM);
  }

  tile_frame(1, "Pay");  // coin with a diamond
  tile_center(1, cx, cy);
  tft.fillCircle(cx, cy, 24, rgb(0xC9, 0xA2, 0x27));
  tft.fillTriangle(cx, cy - 16, cx - 10, cy, cx + 10, cy, TFT_WHITE);
  tft.fillTriangle(cx, cy + 16, cx - 10, cy + 3, cx + 10, cy + 3, TFT_WHITE);

  tile_frame(2, "Info");  // "i" in a circle
  tile_center(2, cx, cy);
  tft.fillCircle(cx, cy, 24, rgb(0x2F, 0x9E, 0x5B));
  tft.fillCircle(cx, cy - 12, 4, TFT_WHITE);
  tft.fillRect(cx - 3, cy - 4, 7, 20, TFT_WHITE);

  tile_frame(3, "Calibrate");  // crosshair
  tile_center(3, cx, cy);
  tft.drawCircle(cx, cy, 20, rgb(0xD4, 0x54, 0x8E));
  tft.drawCircle(cx, cy, 19, rgb(0xD4, 0x54, 0x8E));
  tft.fillRect(cx - 26, cy - 1, 52, 3, rgb(0xD4, 0x54, 0x8E));
  tft.fillRect(cx - 1, cy - 26, 3, 52, rgb(0xD4, 0x54, 0x8E));
}

// --- contacts screen ---

// Contact indices shown with the current filter.
static uint8_t shown[MAX_CONTACTS];
static uint8_t shown_count = 0;
static void filter_contacts() {
  shown_count = 0;
  for (uint8_t i = 0; i < state->contact_count; i++) {
    if (visible(i)) shown[shown_count++] = i;
  }
}

constexpr int FILTER_W = 130;  // tap area of the filter at the right of the title bar

static void show_contacts() {
  tft.fillScreen(CONSOLE);
  filter_contacts();
  char filter[32];
  uint8_t hidden = state->contact_count - shown_count;
  if (humans_only) snprintf(filter, sizeof(filter), "humans only (%u hidden)", hidden);
  else snprintf(filter, sizeof(filter), "all (%u)", shown_count);
  title_bar("Contacts", filter);
  if (!shown_count) {
    centered(state->contact_count ? "No verified humans yet." : "No contacts yet.", H / 2 - 10);
    centered(state->contact_count ? "Tap the top right to show all." : "Add them in the Kinjo web app.", H / 2 + 10);
    return;
  }
  bool paged = shown_count > ROWS;
  int per_page = paged ? ROWS - 1 : ROWS;
  if (contacts_page * per_page >= shown_count) contacts_page = 0;
  for (int r = 0; r < per_page; r++) {
    int s_i = contacts_page * per_page + r;
    if (s_i >= shown_count) break;
    uint8_t i = shown[s_i];
    const Contact& c = state->contacts[i];
    int y = HEADER_H + r * ROW_H;
    tft.fillRect(0, y, W, ROW_H - 2, PAPER);
    uint16_t color = tab_color(c.name, c.name_len);
    tft.fillRect(0, y, 8, ROW_H - 2, color);
    char name[40];
    short_name(c.name, c.name_len, name, sizeof(name));
    tft.setTextFont(2);
    tft.setTextColor(INK, PAPER);
    tft.drawString(name, 16, y + 6);
    int x = W - 10;
    if (unread[i]) {
      char n[6];
      snprintf(n, sizeof(n), "%u", unread[i]);
      tft.fillCircle(x - 8, y + ROW_H / 2 - 1, 9, FAIL);
      tft.setTextColor(TFT_WHITE, FAIL);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(n, x - 8, y + ROW_H / 2 - 1);
      tft.setTextDatum(TL_DATUM);
      x -= 24;
    }
    if (c.flags & FLAG_VERIFIED) name_tab(x - tab_width("human", true), y + 8, "human", ACCENT, true);
  }
  if (paged) {
    int y = HEADER_H + (ROWS - 1) * ROW_H;
    tft.fillRect(0, y, W, ROW_H - 2, FRAME);
    char more[24];
    int pages = (shown_count + per_page - 1) / per_page;
    snprintf(more, sizeof(more), "more (%d/%d)", contacts_page + 1, pages);
    centered(more, y + ROW_H / 2 - 1, TFT_WHITE, FRAME);
  }
}

// --- chat screen ---

constexpr int STRIP_LINES = 2;  // text lines in a chat strip. The full view shows all of it.

// Lays out `text` in lines of at most `w` pixels, cut at spaces where possible. The last allowed
// line ends in "..." when text is left over. Draws only if `draw`. Returns the number of lines.
static int layout_text(const char* text, int x, int y, int w, int max_lines, bool draw) {
  tft.setTextFont(2);
  size_t len = strlen(text), start = 0;
  int line = 0;
  char buf[MAX_TEXT + 4];
  for (; line < max_lines && start < len; line++) {
    size_t end = len;
    for (;;) {
      memcpy(buf, text + start, end - start);
      buf[end - start] = 0;
      if (tft.textWidth(buf) <= w || end <= start + 1) break;
      size_t cut = end - 1;
      while (cut > start && text[cut] != ' ') cut--;
      end = cut > start ? cut : end - 1;
    }
    if (line == max_lines - 1 && end < len) {  // last line and more to come: cut and add "..."
      size_t n = end - start;
      for (;;) {
        memcpy(buf, text + start, n);
        strcpy(buf + n, "...");
        if (tft.textWidth(buf) <= w || n == 0) break;
        n--;
      }
    }
    if (draw) tft.drawString(buf, x, y + line * 16);
    start = end;
    while (start < len && text[start] == ' ') start++;
  }
  return line;
}

static int text_lines(const Msg& m) { return layout_text(m.text, 0, 0, STRIP_W - 12, STRIP_LINES, false); }

// Strips fit their content, like chat bubbles. At least as wide as the name tab.
static void strip_size(const Msg& m, int& w, int& h) {
  int tab_w = tab_width(m.name, m.human);
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

static void draw_text(const Msg& m, int x, int y, int w, int max_lines) {
  tft.setTextFont(2);
  tft.setTextColor(m.failed ? FAIL : INK, PAPER);
  layout_text(m.text, x, y, w, max_lines, true);
}

static void draw_msg(const Msg& m, int y) {
  uint16_t color = m.failed ? GREY_TAB : m.color;
  int sw, sh;
  strip_size(m, sw, sh);
  int x = m.outgoing ? 4 : W - 4 - sw;  // own on the left, others on the right
  int tab_w = tab_width(m.name, m.human);
  name_tab(m.outgoing ? x : x + sw - tab_w, y, m.name, color, m.human && !m.failed);
  int sy = y + TAB_H;
  tft.fillRect(x, sy, sw, sh, PAPER);  // plain: only the note is ruled
  tft.drawRect(x, sy, sw, sh, color);
  tft.drawRect(x + 1, sy + 1, sw - 2, sh - 2, color);
  int ty = sy + 4;
  if (m.text[0]) {
    draw_text(m, x + 6, ty, sw - 12, STRIP_LINES);
    ty += text_lines(m) * 16 + 4;
  }
  if (m.drawing >= 0) {
    Preview p = preview_of(m);
    Thumb t = {x + 6, ty, p.b.x0, p.b.y0, p.num, p.den, 0, 0};
    // Ruled like the note, at the same scale.
    for (int ly = RULE - (p.b.y0 % RULE); ly * p.num / p.den < p.h; ly += RULE) {
      tft.drawFastHLine(t.x, t.y + ly * p.num / p.den, p.w, RULE_COLOR);
    }
    decode_drawing(drawings[m.drawing], drawing_len[m.drawing], thumb_point, &t);
  }
}

static void show_chat() {
  tft.fillScreen(CONSOLE);
  const Contact* c = chat_contact();
  if (!c) return;
  unread[peer] = 0;
  char name[40];
  short_name(c->name, c->name_len, name, sizeof(name));
  title_bar(name, c->flags & FLAG_VERIFIED ? "human" : "", true, tab_color(c->name, c->name_len));

  // WRITE A NOTE, full width at the bottom.
  tft.fillRect(0, H - WRITE_H, W, WRITE_H, ACCENT);
  centered("WRITE A NOTE", H - WRITE_H / 2, TFT_WHITE, ACCENT);

  // Newest at the bottom, as many as fit.
  uint32_t h_peer = contact_hash(*c);
  int y = H - WRITE_H - 4;
  hit_count = 0;
  bool any = false;
  for (int i = msg_count - 1; i >= 0; i--) {
    if (msgs[i].peer != h_peer) continue;
    any = true;
    int h = TAB_H + strip_height(msgs[i]);
    if (y - h < HEADER_H + 2) break;
    y -= h;
    draw_msg(msgs[i], y);
    hits[hit_count++] = {y, y + h, (uint8_t)i};
    y -= 4;
  }
  if (!any) centered("Nothing here yet. Say hi!", (H - WRITE_H + HEADER_H) / 2);
}

// --- note ---

static void reset_note() {
  static uint8_t encoder_mem[sizeof(DrawingEncoder)];
  encoder = new (encoder_mem) DrawingEncoder(canvas, sizeof(canvas));
  encoder->set_color(pen_color);
  in_stroke = false;
  kb_len = 0;
  kb_text[0] = 0;
  note_peer = peer;
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

// Redraws what is already on the note, after another screen covered it.
static int redraw_x, redraw_y;
static void redraw_point(void*, bool new_stroke, int x, int y, uint8_t color) {
  if (new_stroke) tft.fillCircle(x, y, 1, PEN_COLORS[color]);
  else pen(redraw_x, redraw_y, x, y, PEN_COLORS[color]);
  redraw_x = x;
  redraw_y = y;
}

static void show_note() {
  if (!encoder || note_peer != peer) reset_note();  // a note belongs to one chat
  paper(0, HEADER_H, TOOL_X, H - HEADER_H);
  draw_toolbar();
  char title[48] = "NOTE";
  if (const Contact* c = chat_contact()) {
    char name[40];
    short_name(c->name, c->name_len, name, sizeof(name));
    snprintf(title, sizeof(title), "NOTE to %s", name);
  }
  title_bar(title);
  char own[40];
  short_name(state->name, state->name_len, own, sizeof(own));
  if (state->name_len) name_tab(0, HEADER_H, own, tab_color(state->name, state->name_len), owner_verified());
  if (kb_len) {  // the text written with the keyboard sits at the top of the note
    tft.setTextFont(2);
    tft.setTextColor(INK, PAPER);
    const char* shown = kb_text;
    while (*shown && tft.textWidth(shown) > TOOL_X - 8) shown++;
    tft.drawString(shown, 4, HEADER_H + TAB_H + 2);
  }
  decode_drawing(canvas, encoder->length(), redraw_point, nullptr);
  encoder->set_color(pen_color);
  in_stroke = false;
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
    draw_text(m, 6, HEADER_H + 14, W - 12, (H - HEADER_H - 14) / 16);
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
  // Bottom row: shift (2), space (4), del (2), done (2)
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

// --- info, pay, calibrate ---

static void info_row(int& y, const char* label, const char* value) {
  tft.setTextFont(1);
  tft.setTextColor(FRAME_DARK, CONSOLE);
  tft.drawString(label, 8, y);
  tft.setTextFont(2);
  tft.setTextColor(INK, CONSOLE);
  tft.drawString(value, 8, y + 9);
  y += 30;
}

static void show_info() {
  tft.fillScreen(CONSOLE);
  title_bar("Info");
  int y = HEADER_H + 6;
  char buf[72];
  if (state->name_len) {
    memcpy(buf, state->name, state->name_len);
    buf[state->name_len] = 0;
  } else {
    strcpy(buf, "not registered yet");
  }
  info_row(y, "ENS NAME", buf);
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)node_id(state->pub));
  info_row(y, "NODE ID", buf);
  // Public key in two lines of 32 hex digits.
  tft.setTextFont(1);
  tft.setTextColor(FRAME_DARK, CONSOLE);
  tft.drawString("PUBLIC KEY (xyz.kinjo.encryption-key)", 8, y);
  tft.setTextFont(2);
  tft.setTextColor(INK, CONSOLE);
  for (int line = 0; line < 2; line++) {
    for (int i = 0; i < 16; i++) snprintf(buf + i * 2, 3, "%02x", state->pub[line * 16 + i]);
    tft.drawString(buf, 8, y + 9 + line * 16);
  }
  y += 46;
  snprintf(buf, sizeof(buf), "%u", state->contact_count);
  info_row(y, "CONTACTS", buf);
  info_row(y, "WORLD ID", owner_verified() ? "verified human" : "not verified (optional, in the web app)");
  info_row(y, "RADIO MAC", WiFi.macAddress().c_str());
}

static void show_pay() {
  tft.fillScreen(CONSOLE);
  title_bar("Pay");
  centered("Payments over the mesh", H / 2 - 20, INK);
  centered("are coming soon.", H / 2, INK);
  centered("Signed here, sent on once a node is online.", H / 2 + 28);
}

// Runs TFT_eSPI's four-corner calibration and keeps the result in flash.
static void calibrate() {
  tft.fillScreen(CONSOLE);
  title_bar("Calibrate", "", false);
  centered("Lift your finger, then touch", H / 2 - 10, INK);
  centered("each arrow as it shows up.", H / 2 + 10, INK);
  while (tft.getTouchRawZ() > 100) delay(10);  // the tap that opened this screen
  delay(1500);
  tft.fillScreen(TFT_BLACK);
  tft.calibrateTouch(cal_data, TFT_MAGENTA, TFT_BLACK, 15);
  tft.setTouch(cal_data);
  storage::save_touch(cal_data);
  while (tft.getTouchRawZ() > 100) delay(10);
  touch_down = false;
}

static void switch_to(Screen s) {
  screen = s;
  switch (s) {
    case Screen::Home: show_home(); break;
    case Screen::Contacts: show_contacts(); break;
    case Screen::Chat: show_chat(); break;
    case Screen::Note: show_note(); break;
    case Screen::Palette: show_palette(); break;
    case Screen::Keyboard: show_keyboard(); break;
    case Screen::View: show_view(); break;
    case Screen::Info: show_info(); break;
    case Screen::Pay: show_pay(); break;
  }
}

static void back() {
  switch (screen) {
    case Screen::Home: break;
    case Screen::Chat: switch_to(Screen::Contacts); break;
    case Screen::Note: switch_to(Screen::Chat); break;
    case Screen::Palette:
    case Screen::Keyboard: switch_to(Screen::Note); break;
    case Screen::View: switch_to(Screen::Chat); break;
    default: switch_to(Screen::Home); break;
  }
}

// --- power ---

static void backlight(bool on) {
  gpio_hold_dis((gpio_num_t)PIN_BACKLIGHT);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, on ? HIGH : LOW);
}

// Deep sleep until POWER is pressed again. Waking up restarts the handheld (setup() runs again).
static void power_off() {
  tft.fillScreen(CONSOLE);
  centered("Bye", H / 2, INK);
  while (digitalRead(PIN_POWER) == LOW) delay(10);  // a held button would wake it right away
  delay(300);
  tft.writecommand(0x28);  // display off
  tft.writecommand(0x10);  // display sleep
  backlight(false);
  gpio_hold_en((gpio_num_t)PIN_BACKLIGHT);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)PIN_POWER);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_POWER);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_POWER, 0);
  esp_deep_sleep_start();
}

// --- public ---

inline void begin(DeviceState& s) {
  state = &s;
  backlight(true);
  for (Button& b : buttons) {
    pinMode(b.pin, INPUT_PULLUP);
    // Woken up by POWER: it's still held, don't count that as a press to switch off.
    b.down = b.held = digitalRead(b.pin) == LOW;
  }
  storage::load_touch(cal_data);
  humans_only = storage::load_humans_only();
  tft.init();
  tft.setRotation(1);
  tft.setTouch(cal_data);
  show_home();
}

/** Call after provisioning changed the state (name, contacts). */
inline void refresh() {
  memset(unread, 0, sizeof(unread));
  if (screen == Screen::Chat || screen == Screen::Note || screen == Screen::View) {
    screen = Screen::Contacts;  // the open chat's contact may be gone
  }
  switch_to(screen);
}

inline void on_message(const Contact& from, const uint8_t* pt, size_t len) {
  if (pt[0] != KIND_TEXT && pt[0] != KIND_DRAWING && pt[0] != KIND_NOTE) return;  // newer kinds aren't chat messages
  Msg& m = new_msg(from, from.name, from.name_len, false);
  if (pt[0] == KIND_TEXT) {
    set_text(m, (const char*)pt + 1, len - 1);
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
  uint8_t i = &from - state->contacts;
  bool open = screen == Screen::Chat && i == peer;
  if (!open && i < MAX_CONTACTS && unread[i] < 99) unread[i]++;
  if (screen == Screen::Chat || screen == Screen::Contacts || screen == Screen::Home) switch_to(screen);
}

// `drawing` is a DRAWING plaintext (or null).
static void add_own(const Contact& to, const char* text, size_t text_len, bool failed, const uint8_t* drawing,
                    size_t drawing_len_) {
  Msg& m = new_msg(to, state->name, state->name_len, true);
  m.failed = failed;
  if (text) set_text(m, text, text_len);
  if (drawing && drawing_len_ > 1) store_items(m, drawing + 1, drawing_len_ - 1);
}

static uint8_t note_buf[MAX_PLAINTEXT];

/** Sends the note: only text, only the drawing, or both as one NOTE. Then back to the chat. */
static void send_note() {
  const Contact* to = chat_contact();
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
  if (ok) add_own(*to, has_text ? kb_text : nullptr, kb_len, false, has_drawing ? canvas : nullptr, dlen);
  else add_own(*to, "sending failed", 14, true, nullptr, 0);
  reset_note();
  switch_to(Screen::Chat);
}

static void on_press(int pin) {
  if (pin == PIN_HOME) switch_to(Screen::Home);
  else if (pin == PIN_BACK) back();
}

static void poll_buttons(uint32_t now) {
  for (Button& b : buttons) {
    bool down = digitalRead(b.pin) == LOW;
    if (down && b.pin == PIN_POWER && !b.held && b.down && now - b.changed_ms >= POWER_HOLD_MS) {
      b.held = true;
      power_off();
    }
    if (down == b.down || now - b.changed_ms < DEBOUNCE_MS) continue;
    b.down = down;
    b.changed_ms = now;
    if (!down) b.held = false;
    else if (b.pin != PIN_POWER) on_press(b.pin);
  }
}

static void on_tap_home(int x, int y) {
  if (y < HEADER_H + TILE_GAP) return;
  int col = x < W / 2 ? 0 : 1, row = y < HEADER_H + TILE_GAP + TILE_H + TILE_GAP / 2 ? 0 : 1;
  int tile = row * 2 + col;
  if (tile == 0) switch_to(Screen::Contacts);
  else if (tile == 1) switch_to(Screen::Pay);
  else if (tile == 2) switch_to(Screen::Info);
  else {
    calibrate();
    switch_to(Screen::Home);
  }
}

static void on_tap_contacts(int x, int y) {
  if (y < HEADER_H) {
    if (x >= W - FILTER_W) {  // filter: all <-> verified humans only
      humans_only = !humans_only;
      storage::save_humans_only(humans_only);
      contacts_page = 0;
      show_contacts();
    }
    return;
  }
  if (!shown_count) return;
  int r = (y - HEADER_H) / ROW_H;
  bool paged = shown_count > ROWS;
  int per_page = paged ? ROWS - 1 : ROWS;
  if (paged && r == ROWS - 1) {
    contacts_page++;
    show_contacts();
    return;
  }
  int s_i = contacts_page * per_page + r;
  if (r >= per_page || s_i >= shown_count) return;
  peer = shown[s_i];
  switch_to(Screen::Chat);
}

static void on_tap_keyboard(int x, int y) {
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
    switch_to(Screen::Note);  // back to the note, keeping text and drawing
  }
}

// Taps act once per touch. Strokes on the note need the pen held down.
static void on_tap(int x, int y) {
  if (screen == Screen::View) {
    if (x >= W - CLOSE_W && y < HEADER_H + 10) switch_to(Screen::Chat);
    return;
  }
  if (screen != Screen::Home && y < HEADER_H && x < BACK_W) {
    back();
    return;
  }
  switch (screen) {
    case Screen::Home: on_tap_home(x, y); break;
    case Screen::Contacts: on_tap_contacts(x, y); break;
    case Screen::Chat:
      if (y >= H - WRITE_H) {
        switch_to(Screen::Note);
        return;
      }
      for (uint8_t i = 0; i < hit_count; i++) {
        if (y >= hits[i].y0 && y < hits[i].y1) {
          viewing = hits[i].msg;
          switch_to(Screen::View);
          return;
        }
      }
      break;
    case Screen::Keyboard: on_tap_keyboard(x, y); break;
    case Screen::Palette:
      for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
        int cx = GAP + (i % 4) * (CELL_W + GAP), cy = HEADER_H + GAP + (i / 4) * (CELL_H + GAP);
        if (x >= cx && x < cx + CELL_W && y >= cy && y < cy + CELL_H) {
          pen_color = i;
          switch_to(Screen::Note);
          return;
        }
      }
      break;
    case Screen::Note:  // toolbar
      if (y < HEADER_H) break;
      if (y < TOOL_ABC_Y) switch_to(Screen::Palette);
      else if (y < TOOL_CLEAR_Y) switch_to(Screen::Keyboard);
      else if (y < TOOL_SEND_Y) {
        reset_note();
        show_note();
      } else {
        send_note();
      }
      break;
    default: break;
  }
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

  if (screen != Screen::Note || x >= TOOL_X || y < HEADER_H) {
    in_stroke = false;
    if (new_touch) on_tap(x, y);
    return;
  }
  if (y < HEADER_H + TAB_H) return;
  if (!in_stroke) {
    if (!encoder->begin_stroke(x, y)) return;  // note full
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
