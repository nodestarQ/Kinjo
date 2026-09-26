#include <string.h>

#include "kinjo.h"

namespace kinjo {

void reset_state(DeviceState& s, const uint8_t new_priv[KEY_SIZE]) {
  memset(&s, 0, sizeof(s));
  memcpy(s.priv, new_priv, KEY_SIZE);
  public_key(s.priv, s.pub);
}

// Reads a length-prefixed name at `body[offset]`. Returns false if it runs past `len` or is too long.
static bool read_name(const uint8_t* body, size_t len, size_t offset, const char*& name, uint8_t& name_len,
                      size_t& end) {
  if (offset >= len) return false;
  name_len = body[offset];
  end = offset + 1 + name_len;
  if (name_len > MAX_NAME || end > len) return false;
  name = (const char*)body + offset + 1;
  return true;
}

static size_t write_info(const DeviceState& s, uint8_t* out) {
  memcpy(out, s.pub, KEY_SIZE);
  out[KEY_SIZE] = VERSION;
  out[KEY_SIZE + 1] = s.contact_count;
  out[KEY_SIZE + 2] = s.block_count & 0xFF;
  out[KEY_SIZE + 3] = s.block_count >> 8;
  out[KEY_SIZE + 4] = s.name_len;
  memcpy(out + KEY_SIZE + 5, s.name, s.name_len);
  return KEY_SIZE + 5 + s.name_len;
}

size_t handle_provision(DeviceState& s, const uint8_t* body, size_t len, void (*new_priv)(uint8_t out[KEY_SIZE]),
                        uint8_t* reply, bool& changed) {
  changed = false;
  uint8_t cmd = len ? body[0] : 0;
  reply[0] = cmd;
  reply[1] = STATUS_MALFORMED;
  if (len == 0) return 2;

  const char* name;
  uint8_t name_len;
  size_t end;
  switch (cmd) {
    case CMD_INFO:
      if (len != 1) return 2;
      reply[1] = STATUS_OK;
      return 2 + write_info(s, reply + 2);

    case CMD_SET_NAME:
      if (!read_name(body, len, 1, name, name_len, end) || end != len) return 2;
      memcpy(s.name, name, name_len);
      s.name_len = name_len;
      changed = true;
      reply[1] = STATUS_OK;
      return 2;

    case CMD_ADD_CONTACT: {
      if (len < 2 + KEY_SIZE || !read_name(body, len, 2 + KEY_SIZE, name, name_len, end) || end != len) return 2;
      Contact* slot = nullptr;
      for (uint8_t i = 0; i < s.contact_count; i++) {
        Contact& c = s.contacts[i];
        if (c.name_len == name_len && memcmp(c.name, name, name_len) == 0) slot = &c;
      }
      if (!slot) {
        if (s.contact_count >= MAX_CONTACTS) {
          reply[1] = STATUS_FULL;
          return 2;
        }
        slot = &s.contacts[s.contact_count++];
      }
      slot->flags = body[1];
      memcpy(slot->pub, body + 2, KEY_SIZE);
      slot->name_len = name_len;
      memcpy(slot->name, name, name_len);
      changed = true;
      reply[1] = STATUS_OK;
      return 2;
    }

    case CMD_CLEAR_CONTACTS:
      if (len != 1) return 2;
      s.contact_count = 0;
      memset(s.contacts, 0, sizeof(s.contacts));
      changed = true;
      reply[1] = STATUS_OK;
      return 2;

    case CMD_WIPE: {
      if (len != 1) return 2;
      uint8_t priv[KEY_SIZE];
      new_priv(priv);
      reset_state(s, priv);
      memset(priv, 0, sizeof(priv));
      changed = true;
      reply[1] = STATUS_OK;
      return 2 + write_info(s, reply + 2);
    }

    default:
      reply[1] = STATUS_UNKNOWN_COMMAND;
      return 2;
  }
}

const Contact* find_contact(const DeviceState& s, uint32_t id) {
  for (uint8_t i = 0; i < s.contact_count; i++) {
    if (node_id(s.contacts[i].pub) == id) return &s.contacts[i];
  }
  return nullptr;
}

}  // namespace kinjo
