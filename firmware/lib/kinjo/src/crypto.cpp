#include <string.h>

#include "kinjo.h"
#include "monocypher-ed25519.h"
#include "monocypher.h"

namespace kinjo {

static const char INFO_PREFIX[] = "kinjo/0.1";
static constexpr size_t INFO_PREFIX_LEN = sizeof(INFO_PREFIX) - 1;

void public_key(const uint8_t priv[KEY_SIZE], uint8_t pub[KEY_SIZE]) { crypto_x25519_public_key(pub, priv); }

uint32_t node_id(const uint8_t pub[KEY_SIZE]) {
  return (uint32_t)pub[0] | (uint32_t)pub[1] << 8 | (uint32_t)pub[2] << 16 | (uint32_t)pub[3] << 24;
}

bool derive_key(const uint8_t priv[KEY_SIZE], const uint8_t peer_pub[KEY_SIZE], uint8_t key[KEY_SIZE]) {
  uint8_t own[KEY_SIZE], shared[KEY_SIZE];
  public_key(priv, own);
  crypto_x25519(shared, priv, peer_pub);

  uint8_t acc = 0;
  for (uint8_t b : shared) acc |= b;
  if (acc == 0) return false;

  bool own_first = memcmp(own, peer_pub, KEY_SIZE) < 0;
  uint8_t info[INFO_PREFIX_LEN + 2 * KEY_SIZE];
  memcpy(info, INFO_PREFIX, INFO_PREFIX_LEN);
  memcpy(info + INFO_PREFIX_LEN, own_first ? own : peer_pub, KEY_SIZE);
  memcpy(info + INFO_PREFIX_LEN + KEY_SIZE, own_first ? peer_pub : own, KEY_SIZE);

  crypto_sha512_hkdf(key, KEY_SIZE, shared, KEY_SIZE, nullptr, 0, info, sizeof(info));
  crypto_wipe(shared, sizeof(shared));
  return true;
}

size_t seal(const uint8_t key[KEY_SIZE], uint32_t source, uint32_t destination, uint32_t message_id,
            uint8_t ttl, const uint8_t nonce[NONCE_SIZE], const uint8_t* plaintext, size_t plaintext_len,
            uint8_t* body, Header& base) {
  size_t body_len = plaintext_len + SEALED_OVERHEAD;
  uint8_t count = frag_count_for(body_len);
  if (plaintext_len == 0 || count == 0) return 0;

  base = Header();
  base.type = TYPE_SEALED;
  base.ttl = ttl;
  base.message_id = message_id;
  base.source = source;
  base.destination = destination;
  base.frag_count = count;

  uint8_t ad[AD_SIZE];
  header_ad(base, ad);

  crypto_aead_ctx ctx;
  crypto_aead_init_ietf(&ctx, key, nonce);
  memcpy(body, nonce, NONCE_SIZE);
  crypto_aead_write(&ctx, body + NONCE_SIZE, body + NONCE_SIZE + plaintext_len, ad, AD_SIZE, plaintext,
                    plaintext_len);
  crypto_wipe(&ctx, sizeof(ctx));
  return body_len;
}

bool open_sealed(const uint8_t key[KEY_SIZE], const Header& h, const uint8_t* body, size_t body_len,
                 uint8_t* plaintext, size_t& plaintext_len) {
  if (h.type != TYPE_SEALED || body_len < SEALED_OVERHEAD + 1) return false;
  size_t ct_len = body_len - SEALED_OVERHEAD;

  uint8_t ad[AD_SIZE];
  header_ad(h, ad);

  crypto_aead_ctx ctx;
  crypto_aead_init_ietf(&ctx, key, body);
  int rc = crypto_aead_read(&ctx, plaintext, body + NONCE_SIZE + ct_len, ad, AD_SIZE, body + NONCE_SIZE, ct_len);
  crypto_wipe(&ctx, sizeof(ctx));
  if (rc != 0) return false;
  plaintext_len = ct_len;
  return true;
}

}  // namespace kinjo
