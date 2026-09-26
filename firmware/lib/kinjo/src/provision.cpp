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

// Stored contact with this name, nullptr if none.
static Contact* contact_named(DeviceState& s, const char* name, uint8_t name_len) {
  for (uint8_t i = 0; i < s.contact_count; i++) {
    Contact& c = s.contacts[i];
    if (c.name_len == name_len && memcmp(c.name, name, name_len) == 0) return &c;
  }
  return nullptr;
}

// Adds the contact or replaces the one with the same name. False if the list is full.
static bool put_contact(DeviceState& s, uint8_t flags, const uint8_t* pub, const char* name, uint8_t name_len) {
  Contact* slot = contact_named(s, name, name_len);
  if (!slot) {
    if (s.contact_count >= MAX_CONTACTS) return false;
    slot = &s.contacts[s.contact_count++];
  }
  slot->flags = flags & FLAG_VERIFIED;  // other bits are 0 on the wire, FLAG_REVOKED is local
  memcpy(slot->pub, pub, KEY_SIZE);
  slot->name_len = name_len;
  memcpy(slot->name, name, name_len);
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
      if (!put_contact(s, body[1], body + 2, name, name_len)) {
        reply[1] = STATUS_FULL;
        return 2;
      }
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

// Everything after the first dot, empty if there is none.
static const char* parent(const char* name, size_t len, size_t& parent_len) {
  const char* dot = (const char*)memchr(name, '.', len);
  parent_len = dot ? len - (dot + 1 - name) : 0;
  return dot ? dot + 1 : name + len;
}

static bool same(const char* a, size_t a_len, const char* b, size_t b_len) {
  return a_len == b_len && memcmp(a, b, a_len) == 0;
}

bool accepts_update(const char* own, size_t own_len, const char* sender, size_t sender_len, const char* target,
                    size_t target_len) {
  size_t own_parent_len, sender_parent_len;
  const char* own_parent = parent(own, own_len, own_parent_len);
  const char* sender_parent = parent(sender, sender_len, sender_parent_len);
  return own_parent_len > 0 && !same(sender, sender_len, own, own_len) &&
         same(sender_parent, sender_parent_len, own_parent, own_parent_len) &&
         !same(target, target_len, own, own_len);
}

UpdateResult apply_contact_update(DeviceState& s, const Contact& from, const uint8_t* pt, size_t len,
                                  bool& changed) {
  changed = false;
  const char* name;
  uint8_t name_len;
  size_t end;
  if (len < 3 + KEY_SIZE || pt[0] != KIND_CONTACT_UPDATE ||
      !read_name(pt, len, 3 + KEY_SIZE, name, name_len, end) || end != len || name_len == 0) {
    return UPDATE_MALFORMED;
  }
  uint8_t op = pt[1], flags = pt[2];
  const uint8_t* pub = pt + 3;
  if (op == OP_REVOKE) {
    for (size_t i = 0; i < KEY_SIZE; i++) {
      if (pub[i]) return UPDATE_MALFORMED;
    }
    if (flags) return UPDATE_MALFORMED;
  } else if (op != OP_SET) {
    return UPDATE_MALFORMED;
  }
  if (!accepts_update(s.name, s.name_len, from.name, from.name_len, name, name_len)) return UPDATE_NOT_TRUSTED;

  if (op == OP_SET) {
    if (!put_contact(s, flags, pub, name, name_len)) return UPDATE_FULL;
    changed = true;
    return UPDATE_APPLIED;
  }
  Contact* c = contact_named(s, name, name_len);
  if (c && !(c->flags & FLAG_REVOKED)) {
    c->flags |= FLAG_REVOKED;
    changed = true;
  }
  return UPDATE_APPLIED;
}

}  // namespace kinjo
