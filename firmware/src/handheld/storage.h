// Keeps the DeviceState in flash (NVS). Only the used part of the contact and
// block lists is written, so a fresh device takes a few hundred bytes.
#pragma once

#include <Preferences.h>
#include <esp_system.h>
#include <kinjo.h>

namespace storage {

static Preferences prefs;

struct Core {
  uint8_t priv[kinjo::KEY_SIZE];
  uint8_t name_len;
  char name[kinjo::MAX_NAME];
};

inline void random_key(uint8_t out[kinjo::KEY_SIZE]) { esp_fill_random(out, kinjo::KEY_SIZE); }

inline void save(const kinjo::DeviceState& s) {
  Core core;
  memcpy(core.priv, s.priv, sizeof(core.priv));
  core.name_len = s.name_len;
  memcpy(core.name, s.name, sizeof(core.name));
  prefs.putBytes("core", &core, sizeof(core));
  prefs.putUChar("ncontacts", s.contact_count);
  prefs.putBytes("contacts", s.contacts, s.contact_count * sizeof(kinjo::Contact));
  prefs.putUShort("nblocks", s.block_count);
  prefs.putBytes("blocks", s.blocks, s.block_count * sizeof(s.blocks[0]));
  memset(&core, 0, sizeof(core));
}

// Loads the state. On first boot makes a new key pair and saves it. Returns true on first boot.
inline bool load(kinjo::DeviceState& s) {
  prefs.begin("kinjo", false);
  Core core;
  if (prefs.getBytes("core", &core, sizeof(core)) != sizeof(core)) {
    uint8_t priv[kinjo::KEY_SIZE];
    random_key(priv);
    kinjo::reset_state(s, priv);
    memset(priv, 0, sizeof(priv));
    save(s);
    return true;
  }
  kinjo::reset_state(s, core.priv);
  s.name_len = core.name_len <= kinjo::MAX_NAME ? core.name_len : 0;
  memcpy(s.name, core.name, s.name_len);
  memset(&core, 0, sizeof(core));

  s.contact_count = prefs.getUChar("ncontacts", 0);
  if (s.contact_count > kinjo::MAX_CONTACTS) s.contact_count = 0;
  prefs.getBytes("contacts", s.contacts, s.contact_count * sizeof(kinjo::Contact));
  s.block_count = prefs.getUShort("nblocks", 0);
  if (s.block_count > kinjo::MAX_BLOCKS) s.block_count = 0;
  prefs.getBytes("blocks", s.blocks, s.block_count * sizeof(s.blocks[0]));
  return false;
}

}  // namespace storage
