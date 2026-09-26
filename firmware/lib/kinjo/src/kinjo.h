// Kinjo wire format v0.1 (protocol/SPEC.md). Plain C++11, no Arduino calls,
// so the same code runs on the ESP32 boards and in the laptop tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kinjo {

constexpr uint8_t VERSION = 0x01;

constexpr uint8_t TYPE_IDENTITY = 0x01;
constexpr uint8_t TYPE_SEALED = 0x10;

constexpr uint8_t KIND_TEXT = 0x01;
constexpr uint8_t KIND_DRAWING = 0x02;

constexpr uint32_t BROADCAST = 0xFFFFFFFF;
constexpr uint8_t DEFAULT_TTL = 4;

constexpr size_t HEADER_SIZE = 18;
constexpr size_t AD_SIZE = 16;
constexpr size_t FRAGMENT_MAX = 232;
constexpr size_t PACKET_MAX = HEADER_SIZE + FRAGMENT_MAX;  // 250, one ESP-NOW frame
constexpr size_t MAX_FRAGMENTS = 16;
constexpr size_t MAX_BODY = FRAGMENT_MAX * MAX_FRAGMENTS;

constexpr size_t KEY_SIZE = 32;
constexpr size_t NONCE_SIZE = 12;
constexpr size_t TAG_SIZE = 16;
constexpr size_t SEALED_OVERHEAD = NONCE_SIZE + TAG_SIZE;
constexpr size_t MAX_PLAINTEXT = MAX_BODY - SEALED_OVERHEAD;
constexpr size_t MAX_TEXT = 200;
constexpr size_t MAX_NAME = 64;
constexpr size_t IDENTITY_MAX = KEY_SIZE + MAX_NAME;

constexpr int CANVAS_W = 320;
constexpr int CANVAS_H = 240;

constexpr size_t SEEN_CACHE_SIZE = 64;
constexpr size_t REASSEMBLY_SLOTS = 4;
constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 5000;

constexpr uint8_t FRAME_RADIO_RX = 0x01;
constexpr uint8_t FRAME_RADIO_TX = 0x02;
constexpr uint8_t FRAME_LOG = 0x03;
constexpr uint8_t FRAME_PROVISION = 0x04;

// --- Header (SPEC §5) ---

struct Header {
  uint8_t version = VERSION;
  uint8_t type = 0;
  uint8_t flags = 0;
  uint8_t ttl = DEFAULT_TTL;
  uint32_t message_id = 0;
  uint32_t source = 0;
  uint32_t destination = 0;
  uint8_t frag_index = 0;
  uint8_t frag_count = 1;
};

void encode_header(const Header& h, uint8_t out[HEADER_SIZE]);
void decode_header(const uint8_t in[HEADER_SIZE], Header& h);
void header_ad(const Header& h, uint8_t out[AD_SIZE]);

// Checks version and fragment fields. On success `frag` points into `packet`.
bool parse_packet(const uint8_t* packet, size_t len, Header& h, const uint8_t*& frag, size_t& frag_len);

// --- Fragmentation (SPEC §7) ---

// 0 if body_len is 0 or larger than MAX_BODY.
uint8_t frag_count_for(size_t body_len);

// Writes fragment `index` of `body` into `out` (PACKET_MAX bytes). `base` gives
// everything but frag_index and frag_count. Returns the packet length, 0 on error.
size_t build_fragment(const Header& base, const uint8_t* body, size_t body_len, uint8_t index, uint8_t* out);

class Reassembler {
 public:
  // Returns true when a message is complete. `body` stays valid until the next push.
  bool push(const uint8_t* packet, size_t len, uint32_t now_ms, Header& h, const uint8_t*& body, size_t& body_len);

 private:
  struct Slot {
    bool used = false;
    uint32_t source = 0;
    uint32_t message_id = 0;
    uint32_t started_ms = 0;
    Header header;
    uint16_t received = 0;
    size_t last_len = 0;
    uint8_t data[MAX_BODY];
  };
  Slot slots_[REASSEMBLY_SLOTS];
};

// --- Forwarding (SPEC §8) ---

class SeenCache {
 public:
  // True if this fragment was seen before. Otherwise remembers it.
  bool check_and_add(const Header& h);

 private:
  struct Entry {
    uint32_t source, message_id;
    uint8_t frag_index;
  };
  Entry entries_[SEEN_CACHE_SIZE] = {};
  size_t count_ = 0;
  size_t next_ = 0;
};

// Lowers the TTL in place. False means drop.
bool forward_ttl(uint8_t* packet, size_t len);

// --- Identity and crypto (SPEC §3, §10) ---

void public_key(const uint8_t priv[KEY_SIZE], uint8_t pub[KEY_SIZE]);
uint32_t node_id(const uint8_t pub[KEY_SIZE]);
// False if the peer key gives an all-zero shared secret.
bool derive_key(const uint8_t priv[KEY_SIZE], const uint8_t peer_pub[KEY_SIZE], uint8_t key[KEY_SIZE]);

// Encrypts `plaintext` into `body` (plaintext_len + SEALED_OVERHEAD bytes).
// Fills `base` (type, frag_count, ids, ttl) for build_fragment. Returns body length, 0 on error.
size_t seal(const uint8_t key[KEY_SIZE], uint32_t source, uint32_t destination, uint32_t message_id,
            uint8_t ttl, const uint8_t nonce[NONCE_SIZE], const uint8_t* plaintext, size_t plaintext_len,
            uint8_t* body, Header& base);

// Decrypts a reassembled SEALED body into `plaintext` (body_len - SEALED_OVERHEAD bytes).
bool open_sealed(const uint8_t key[KEY_SIZE], const Header& h, const uint8_t* body, size_t body_len,
                 uint8_t* plaintext, size_t& plaintext_len);

// --- Payloads (SPEC §6, §9) ---

// Writes KIND_TEXT + text. Returns length, 0 if too long.
size_t text_plaintext(const char* text, size_t text_len, uint8_t* out);

size_t identity_body(const uint8_t pub[KEY_SIZE], const char* name, size_t name_len, uint8_t* out);
bool parse_identity(const uint8_t* body, size_t len, const uint8_t*& pub, const char*& name, size_t& name_len);

// Builds a DRAWING plaintext point by point. Adds intermediate points for big
// jumps and splits strokes longer than 255 points, like the reference.
class DrawingEncoder {
 public:
  DrawingEncoder(uint8_t* out, size_t capacity);
  bool begin_stroke(int x, int y);
  bool add_point(int x, int y);
  size_t length() const { return failed_ ? 0 : len_; }

 private:
  bool append_step(int x, int y);
  bool start(int x, int y);
  uint8_t* out_;
  size_t cap_;
  size_t len_ = 1;
  size_t count_pos_ = 0;
  bool in_stroke_ = false;
  bool failed_ = false;
  int x_ = 0, y_ = 0;
};

// Calls `point(ctx, new_stroke, x, y)` for each point. False on malformed data.
bool decode_drawing(const uint8_t* plaintext, size_t len, void (*point)(void* ctx, bool new_stroke, int x, int y),
                    void* ctx);

// --- Serial framing (SPEC §12) ---

// `out` needs len + len / 254 + 1 bytes. Returns encoded length.
size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out);
// Returns decoded length (SIZE_MAX on malformed input). `out` needs len bytes.
size_t cobs_decode(const uint8_t* in, size_t len, uint8_t* out);

// Collects serial bytes until 0x00 and decodes the frame.
class FrameReader {
 public:
  static constexpr size_t MAX_ENCODED = 1024;
  // True when a frame is ready. `type` and `body` stay valid until the next push.
  bool push(uint8_t byte, uint8_t& type, const uint8_t*& body, size_t& body_len);

 private:
  uint8_t buf_[MAX_ENCODED];
  uint8_t decoded_[MAX_ENCODED];
  size_t len_ = 0;
  bool overflow_ = false;
};

// Writes COBS(type + body) + 0x00 into `out`. Returns total length.
size_t serial_frame(uint8_t type, const uint8_t* body, size_t body_len, uint8_t* out);

}  // namespace kinjo
