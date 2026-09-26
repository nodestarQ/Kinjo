// Checks firmware/lib/kinjo against protocol/test-vectors. Run: make -C firmware/test

#include <stdio.h>
#include <string.h>

#include <string>
#include <utility>
#include <vector>

#include "kinjo.h"

using namespace kinjo;
using Bytes = std::vector<uint8_t>;
using Strokes = std::vector<std::vector<std::pair<int, int>>>;

struct HeaderCase {
  uint8_t version, type, flags, ttl;
  uint32_t message_id, source, destination;
  uint8_t frag_index, frag_count;
  const char* header;
  const char* ad;
};
struct FragmentCase {
  uint8_t type;
  uint32_t message_id, source, destination;
  uint8_t ttl;
  const char* body;
  std::vector<const char*> packets;
};
struct SealedCase {
  const char* label;
  const char* key;
  uint32_t source, destination, message_id;
  uint8_t ttl;
  const char* nonce;
  const char* plaintext;
  std::vector<const char*> packets;
};
struct DrawingCase {
  Strokes strokes;
  std::vector<int> colors;
  const char* plaintext;
  Strokes decoded;
  std::vector<int> decoded_colors;
};
struct NoteCase {
  const char* text;
  const char* plaintext;
  Strokes decoded;
  std::vector<int> decoded_colors;
};
struct IdentityCase {
  const char* pub;
  const char* name;
  const char* body;
  std::vector<const char*> packets;
};
struct CobsCase {
  const char* raw;
  const char* encoded;
};
struct SerialFrameCase {
  uint8_t frame_type;
  const char* body;
  const char* wire;
};

struct ProvisionCase {
  const char* label;
  const char* body;
};

#include "vectors.inc"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what)                                               \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      printf("FAIL %s:%d %s (%s)\n", __FILE__, __LINE__, what, #cond); \
    }                                                                   \
  } while (0)

static Bytes hex(const char* s) {
  Bytes out;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
    unsigned v;
    sscanf(s + i, "%2x", &v);
    out.push_back((uint8_t)v);
  }
  return out;
}

static bool eq(const uint8_t* a, size_t n, const Bytes& b) { return n == b.size() && (n == 0 || memcmp(a, b.data(), n) == 0); }

static void test_header() {
  for (const HeaderCase& c : HEADER_CASES) {
    Header h;
    h.version = c.version;
    h.type = c.type;
    h.flags = c.flags;
    h.ttl = c.ttl;
    h.message_id = c.message_id;
    h.source = c.source;
    h.destination = c.destination;
    h.frag_index = c.frag_index;
    h.frag_count = c.frag_count;
    uint8_t raw[HEADER_SIZE], ad[AD_SIZE];
    encode_header(h, raw);
    header_ad(h, ad);
    CHECK(eq(raw, HEADER_SIZE, hex(c.header)), "header bytes");
    CHECK(eq(ad, AD_SIZE, hex(c.ad)), "header ad");
    Header back;
    decode_header(raw, back);
    CHECK(back.message_id == c.message_id && back.destination == c.destination && back.ttl == c.ttl, "decode");
  }
}

static void test_fragments() {
  for (const FragmentCase& c : FRAGMENT_CASES) {
    Bytes body = hex(c.body);
    Header base;
    base.type = c.type;
    base.message_id = c.message_id;
    base.source = c.source;
    base.destination = c.destination;
    base.ttl = c.ttl;
    uint8_t count = frag_count_for(body.size());
    CHECK(count == c.packets.size(), "fragment count");
    for (uint8_t i = 0; i < count; i++) {
      uint8_t out[PACKET_MAX];
      size_t n = build_fragment(base, body.data(), body.size(), i, out);
      CHECK(eq(out, n, hex(c.packets[i])), "fragment bytes");
    }

    // Reassemble in reverse order.
    static Reassembler r;
    Header h;
    const uint8_t* got = nullptr;
    size_t got_len = 0;
    bool done = false;
    std::vector<Bytes> packets;
    for (const char* p : c.packets) packets.push_back(hex(p));
    for (size_t i = packets.size(); i-- > 0;) {
      done = r.push(packets[i].data(), packets[i].size(), 0, h, got, got_len);
    }
    CHECK(done && eq(got, got_len, body), "reassembly");
  }
  CHECK(frag_count_for(0) == 0 && frag_count_for(MAX_BODY + 1) == 0, "body limits");
}

static void test_reassembly_timeout() {
  static uint8_t body[600] = {};
  Header base;
  base.type = TYPE_SEALED;
  base.message_id = 9;
  base.source = 1;
  base.destination = 2;
  uint8_t p[3][PACKET_MAX];
  size_t n[3];
  for (uint8_t i = 0; i < 3; i++) n[i] = build_fragment(base, body, sizeof(body), i, p[i]);
  static Reassembler r;
  Header h;
  const uint8_t* got;
  size_t got_len;
  CHECK(!r.push(p[0], n[0], 0, h, got, got_len), "partial 1");
  CHECK(!r.push(p[1], n[1], 1000, h, got, got_len), "partial 2");
  CHECK(!r.push(p[2], n[2], 10000, h, got, got_len), "expired partial is dropped");
}

static void test_forwarding() {
  uint8_t body[1] = {'x'};
  Header base;
  base.type = TYPE_SEALED;
  base.message_id = 5;
  base.source = 1;
  base.destination = 2;
  base.ttl = 2;
  uint8_t p[PACKET_MAX];
  size_t n = build_fragment(base, body, 1, 0, p);
  Header h;
  const uint8_t* frag;
  size_t frag_len;
  CHECK(parse_packet(p, n, h, frag, frag_len), "parse");
  SeenCache seen;
  CHECK(!seen.check_and_add(h), "first time not seen");
  CHECK(seen.check_and_add(h), "second time seen");
  CHECK(forward_ttl(p, n) && p[3] == 1, "ttl lowered");
  CHECK(!forward_ttl(p, n), "ttl 1 dropped");

  uint8_t bad[PACKET_MAX];
  memcpy(bad, p, n);
  bad[0] = 2;
  CHECK(!parse_packet(bad, n, h, frag, frag_len), "wrong version rejected");
  CHECK(!parse_packet(p, HEADER_SIZE, h, frag, frag_len), "empty fragment rejected");
}

static void test_crypto() {
  Bytes hpriv = hex(HANDHELD_PRIV), lpriv = hex(LAPTOP_PRIV);
  Bytes hpub = hex(HANDHELD_PUB), lpub = hex(LAPTOP_PUB);
  uint8_t pub[KEY_SIZE], k1[KEY_SIZE], k2[KEY_SIZE];
  public_key(hpriv.data(), pub);
  CHECK(eq(pub, KEY_SIZE, hpub), "handheld public key");
  public_key(lpriv.data(), pub);
  CHECK(eq(pub, KEY_SIZE, lpub), "laptop public key");
  CHECK(node_id(hpub.data()) == HANDHELD_NODE_ID && node_id(lpub.data()) == LAPTOP_NODE_ID, "node ids");
  CHECK(derive_key(hpriv.data(), lpub.data(), k1), "derive handheld side");
  CHECK(derive_key(lpriv.data(), hpub.data(), k2), "derive laptop side");
  CHECK(eq(k1, KEY_SIZE, hex(SHARED_KEY)) && eq(k2, KEY_SIZE, hex(SHARED_KEY)), "shared key");
  uint8_t zero_pub[KEY_SIZE] = {};
  CHECK(!derive_key(hpriv.data(), zero_pub, k1), "all-zero secret rejected");
}

static void test_sealed() {
  for (const SealedCase& c : SEALED_CASES) {
    Bytes key = hex(c.key), nonce = hex(c.nonce), pt = hex(c.plaintext);
    static uint8_t body[MAX_BODY];
    Header base;
    size_t body_len = seal(key.data(), c.source, c.destination, c.message_id, c.ttl, nonce.data(), pt.data(),
                           pt.size(), body, base);
    CHECK(body_len == pt.size() + SEALED_OVERHEAD, c.label);
    CHECK(base.frag_count == c.packets.size(), c.label);
    for (uint8_t i = 0; i < base.frag_count; i++) {
      uint8_t out[PACKET_MAX];
      size_t n = build_fragment(base, body, body_len, i, out);
      CHECK(eq(out, n, hex(c.packets[i])), c.label);
    }

    // Receive side, with the TTL lowered as if a relay forwarded it.
    static Reassembler r;
    Header h;
    const uint8_t* got = nullptr;
    size_t got_len = 0;
    bool done = false;
    std::vector<Bytes> packets;  // kept alive: a single-fragment body points into its packet
    for (const char* s : c.packets) packets.push_back(hex(s));
    for (Bytes& p : packets) {
      forward_ttl(p.data(), p.size());
      done = r.push(p.data(), p.size(), 0, h, got, got_len);
    }
    static uint8_t plain[MAX_BODY];
    size_t plain_len = 0;
    CHECK(done && open_sealed(key.data(), h, got, got_len, plain, plain_len), c.label);
    CHECK(eq(plain, plain_len, pt), c.label);
  }

  // Flip one bit in type, id, source, destination, count, nonce and tag.
  const SealedCase& c = SEALED_CASES[0];
  Bytes key = hex(c.key), raw = hex(c.packets[0]);
  for (size_t offset : {size_t(1), size_t(5), size_t(9), size_t(13), size_t(17), size_t(20), raw.size() - 1}) {
    Bytes bad = raw;
    bad[offset] ^= 0x01;
    Header h;
    const uint8_t* frag;
    size_t frag_len;
    uint8_t plain[MAX_BODY];
    size_t plain_len;
    bool opened = parse_packet(bad.data(), bad.size(), h, frag, frag_len) &&
                  open_sealed(key.data(), h, frag, frag_len, plain, plain_len);
    CHECK(!opened, "tampered packet rejected");
  }
}

struct Collected {
  Strokes strokes;
  std::vector<int> colors;
};

static void collect(void* ctx, bool new_stroke, int x, int y, uint8_t color) {
  Collected* c = (Collected*)ctx;
  if (new_stroke) {
    c->strokes.emplace_back();
    c->colors.push_back(color);
  }
  c->strokes.back().emplace_back(x, y);
}

static void test_drawing() {
  for (const DrawingCase& c : DRAWING_CASES) {
    uint8_t out[MAX_PLAINTEXT];
    DrawingEncoder enc(out, sizeof(out));
    for (size_t s = 0; s < c.strokes.size(); s++) {
      const auto& stroke = c.strokes[s];
      enc.set_color(c.colors[s]);
      enc.begin_stroke(stroke[0].first, stroke[0].second);
      for (size_t i = 1; i < stroke.size(); i++) enc.add_point(stroke[i].first, stroke[i].second);
    }
    CHECK(eq(out, enc.length(), hex(c.plaintext)), "drawing encode");
    Collected back;
    CHECK(decode_drawing(out, enc.length(), collect, &back) && back.strokes == c.decoded, "drawing decode");
    CHECK(back.colors == c.decoded_colors, "drawing colors");
  }
  uint8_t out[16];
  DrawingEncoder enc(out, sizeof(out));
  CHECK(!enc.begin_stroke(CANVAS_W, 0) && enc.length() == 0, "off canvas rejected");
  uint8_t bad[] = {KIND_DRAWING, COLOR_MARKER, PALETTE_SIZE};
  Collected ignored;
  CHECK(!decode_drawing(bad, sizeof(bad), collect, &ignored), "color outside the palette rejected");
}

static void test_notes() {
  for (const NoteCase& c : NOTE_CASES) {
    Bytes pt = hex(c.plaintext);
    const char* text;
    size_t text_len;
    const uint8_t* items;
    size_t items_len;
    CHECK(parse_note(pt.data(), pt.size(), text, text_len, items, items_len) && std::string(text, text_len) == c.text,
          "note text");
    Collected back;
    CHECK(decode_drawing_items(items, items_len, collect, &back) && back.strokes == c.decoded &&
              back.colors == c.decoded_colors,
          "note drawing");

    // Build it again from the text and the drawing part, as the handheld does.
    Bytes drawing = {KIND_DRAWING};
    drawing.insert(drawing.end(), items, items + items_len);
    uint8_t out[MAX_PLAINTEXT];
    size_t n = note_plaintext(c.text, strlen(c.text), drawing.data(), drawing.size(), out);
    CHECK(eq(out, n, pt), "note encode");
  }
  uint8_t bad[] = {KIND_NOTE, 5, 'a', 'b'};
  const char* t;
  size_t tl, il;
  const uint8_t* it;
  CHECK(!parse_note(bad, sizeof(bad), t, tl, it, il), "short note rejected");
}

static void test_payloads() {
  Bytes pub = hex(IDENTITY.pub);
  uint8_t body[IDENTITY_MAX];
  size_t n = identity_body(pub.data(), IDENTITY.name, strlen(IDENTITY.name), body);
  CHECK(eq(body, n, hex(IDENTITY.body)), "identity body");
  Header base;
  base.type = TYPE_IDENTITY;
  base.message_id = 0x77;
  base.source = node_id(pub.data());
  base.destination = BROADCAST;
  uint8_t p[PACKET_MAX];
  size_t pn = build_fragment(base, body, n, 0, p);
  CHECK(eq(p, pn, hex(IDENTITY.packets[0])), "identity packet");
  const uint8_t* got_pub;
  const char* name;
  size_t name_len;
  CHECK(parse_identity(body, n, got_pub, name, name_len) && std::string(name, name_len) == IDENTITY.name,
        "identity parse");

  uint8_t text[MAX_TEXT + 2];
  std::string long_text(MAX_TEXT + 1, 'a');
  CHECK(text_plaintext(long_text.data(), long_text.size(), text) == 0, "text limit");
  CHECK(text_plaintext("gm Tokyo", 8, text) == 9 && text[0] == KIND_TEXT, "text");
}

static void test_cobs() {
  for (const CobsCase& c : COBS_CASES) {
    Bytes raw = hex(c.raw), enc = hex(c.encoded);
    uint8_t out[600];
    size_t n = cobs_encode(raw.data(), raw.size(), out);
    CHECK(eq(out, n, enc), "cobs encode");
  }
  std::vector<CobsCase> all = COBS_CASES;
  all.insert(all.end(), COBS_DECODE_ONLY.begin(), COBS_DECODE_ONLY.end());
  for (const CobsCase& c : all) {
    Bytes raw = hex(c.raw), enc = hex(c.encoded);
    uint8_t out[600];
    size_t n = cobs_decode(enc.data(), enc.size(), out);
    CHECK(n != SIZE_MAX && eq(out, n, raw), "cobs decode");
  }
  uint8_t trunc[] = {0x05, 0x11, 0x22}, out[8];
  CHECK(cobs_decode(trunc, sizeof(trunc), out) == SIZE_MAX, "truncated cobs rejected");

  Bytes body = hex(SERIAL_FRAME.body), wire = hex(SERIAL_FRAME.wire);
  uint8_t frame[64];
  size_t fn = serial_frame(SERIAL_FRAME.frame_type, body.data(), body.size(), frame);
  CHECK(eq(frame, fn, wire), "serial frame");

  static FrameReader reader;
  uint8_t type = 0;
  const uint8_t* got = nullptr;
  size_t got_len = 0;
  bool done = false;
  reader.push(0, type, got, got_len);  // stray delimiter is ignored
  for (uint8_t b : wire) done = reader.push(b, type, got, got_len);
  CHECK(done && type == SERIAL_FRAME.frame_type && eq(got, got_len, body), "frame reader");
}

static const ProvisionCase& provision(const std::vector<ProvisionCase>& cases, const char* label) {
  for (const ProvisionCase& c : cases) {
    if (strcmp(c.label, label) == 0) return c;
  }
  printf("missing vector %s\n", label);
  return cases[0];
}

static void fixed_priv(uint8_t out[KEY_SIZE]) {
  Bytes k = hex(HANDHELD_PRIV);
  memcpy(out, k.data(), KEY_SIZE);
}

static void test_provision() {
  static DeviceState s;
  Bytes laptop_priv = hex(LAPTOP_PRIV);
  reset_state(s, laptop_priv.data());  // some other key, WIPE must replace it
  uint8_t reply[PROVISION_REPLY_MAX];
  bool changed;

  // WIPE with the handheld test key gives the "new device" info.
  Bytes wipe = hex(provision(PROVISION_REQUESTS, "wipe").body);
  size_t n = handle_provision(s, wipe.data(), wipe.size(), fixed_priv, reply, changed);
  Bytes expect_new = hex(provision(PROVISION_REPLIES, "info_new_device").body);
  expect_new[0] = CMD_WIPE;
  CHECK(changed && eq(reply, n, expect_new), "wipe gives new-device info");

  Bytes info = hex(provision(PROVISION_REQUESTS, "info").body);
  n = handle_provision(s, info.data(), info.size(), fixed_priv, reply, changed);
  CHECK(!changed && eq(reply, n, hex(provision(PROVISION_REPLIES, "info_new_device").body)), "info new device");

  Bytes set_name = hex(provision(PROVISION_REQUESTS, "set_name").body);
  n = handle_provision(s, set_name.data(), set_name.size(), fixed_priv, reply, changed);
  CHECK(changed && eq(reply, n, hex(provision(PROVISION_REPLIES, "set_name_ok").body)), "set name");

  Bytes add = hex(provision(PROVISION_REQUESTS, "add_contact_verified").body);
  handle_provision(s, add.data(), add.size(), fixed_priv, reply, changed);
  handle_provision(s, add.data(), add.size(), fixed_priv, reply, changed);  // same name replaces
  CHECK(s.contact_count == 1 && s.contacts[0].flags == FLAG_VERIFIED, "add contact replaces by name");
  CHECK(find_contact(s, LAPTOP_NODE_ID) == &s.contacts[0] && !find_contact(s, HANDHELD_NODE_ID), "find contact");

  s.block_count = 3;  // the provisioned vector has 1 contact and 3 blocks
  n = handle_provision(s, info.data(), info.size(), fixed_priv, reply, changed);
  CHECK(eq(reply, n, hex(provision(PROVISION_REPLIES, "info_provisioned").body)), "info provisioned");

  Bytes clear = hex(provision(PROVISION_REQUESTS, "clear_contacts").body);
  handle_provision(s, clear.data(), clear.size(), fixed_priv, reply, changed);
  CHECK(changed && s.contact_count == 0, "clear contacts");

  s.contact_count = MAX_CONTACTS;
  n = handle_provision(s, add.data(), add.size(), fixed_priv, reply, changed);
  CHECK(!changed && eq(reply, n, hex(provision(PROVISION_REPLIES, "add_contact_full").body)), "contacts full");

  uint8_t unknown[] = {0x7F};
  n = handle_provision(s, unknown, 1, fixed_priv, reply, changed);
  CHECK(eq(reply, n, hex(provision(PROVISION_REPLIES, "unknown_command").body)), "unknown command");

  for (const ProvisionCase& m : PROVISION_MALFORMED) {
    Bytes b = hex(m.body);
    n = handle_provision(s, b.data(), b.size(), fixed_priv, reply, changed);
    CHECK(n == 2 && reply[1] == STATUS_MALFORMED && !changed, m.label);
  }

  n = handle_provision(s, wipe.data(), wipe.size(), fixed_priv, reply, changed);
  CHECK(s.contact_count == 0 && s.name_len == 0 && s.block_count == 0, "wipe clears everything");
}

int main() {
  test_header();
  test_fragments();
  test_reassembly_timeout();
  test_forwarding();
  test_crypto();
  test_sealed();
  test_drawing();
  test_notes();
  test_payloads();
  test_cobs();
  test_provision();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
