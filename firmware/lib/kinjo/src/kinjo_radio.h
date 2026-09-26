// ESP-NOW and USB serial glue shared by the sketches. Arduino only, so it
// lives in a header that the laptop tests never include. Include it from one
// file per sketch (it defines static state).
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <string.h>

#include "kinjo.h"
#include "topology.h"

namespace kinjo {
namespace radio {

struct RxFrame {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[PACKET_MAX];
};

struct Stats {
  uint32_t rx = 0, rx_dropped = 0, tx = 0, tx_failed = 0;
};

static QueueHandle_t rx_queue = nullptr;
static Stats stats;
static const uint8_t* peers[3];
static size_t peer_count = 0;

inline bool is_zero_mac(const uint8_t* mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i]) return false;
  }
  return true;
}

// --- Serial frames (SPEC §12) ---

inline void write_frame(uint8_t type, const uint8_t* body, size_t len) {
  static uint8_t out[PACKET_MAX + 16];
  if (len > PACKET_MAX + 7) return;
  size_t n = serial_frame(type, body, len, out);
  Serial.write(out, n);
}

inline void log(const char* fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n > 0) write_frame(FRAME_LOG, (const uint8_t*)buf, n < (int)sizeof(buf) ? n : sizeof(buf) - 1);
}

// Reports a received packet as RADIO_RX. RSSI is 0: core 2.0.17 doesn't report it.
inline void report_rx(const RxFrame& f) {
  uint8_t body[7 + PACKET_MAX];
  memcpy(body, f.mac, 6);
  body[6] = 0;
  memcpy(body + 7, f.data, f.len);
  write_frame(FRAME_RADIO_RX, body, 7 + f.len);
}

// --- ESP-NOW ---

inline void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len <= 0 || len > (int)PACKET_MAX) return;
  RxFrame f;
  memcpy(f.mac, mac, 6);
  f.len = len;
  memcpy(f.data, data, len);
  if (xQueueSend(rx_queue, &f, 0) != pdTRUE) stats.rx_dropped++;
}

inline void on_sent(const uint8_t*, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) stats.tx_failed++;
}

// Starts USB serial, Wi-Fi on the fixed channel and ESP-NOW with the given peers.
inline bool begin(const char* role, std::initializer_list<const uint8_t*> peer_macs) {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // don't stall when no laptop is listening
#endif
  Serial.write((uint8_t)0);  // ends the ROM boot text so the first frame parses
  rx_queue = xQueueCreate(16, sizeof(RxFrame));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    log("%s: esp_now_init failed", role);
    return false;
  }
  esp_now_register_recv_cb(on_recv);
  esp_now_register_send_cb(on_sent);

  for (const uint8_t* mac : peer_macs) {
    if (is_zero_mac(mac)) {
      log("%s: a peer MAC in topology.h is still zero, skipped", role);
      continue;
    }
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = WIFI_CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    if (esp_now_add_peer(&p) == ESP_OK && peer_count < 3) peers[peer_count++] = mac;
  }
  return true;
}

inline void log_identity(const char* role) {
  log("%s up, mac %s, channel %u, %u peers", role, WiFi.macAddress().c_str(), WIFI_CHANNEL, (unsigned)peer_count);
}

inline void log_stats(const char* role) {
  log("%s stats rx=%lu rx_dropped=%lu tx=%lu tx_failed=%lu", role, (unsigned long)stats.rx,
      (unsigned long)stats.rx_dropped, (unsigned long)stats.tx, (unsigned long)stats.tx_failed);
}

inline bool poll(RxFrame& f) {
  if (xQueueReceive(rx_queue, &f, 0) != pdTRUE) return false;
  stats.rx++;
  return true;
}

// Sends to every peer except `except` (the MAC the packet came from). `except` may be nullptr.
inline void send_to_peers(const uint8_t* packet, size_t len, const uint8_t* except) {
  for (size_t i = 0; i < peer_count; i++) {
    if (except && memcmp(peers[i], except, 6) == 0) continue;
    if (esp_now_send(peers[i], packet, len) == ESP_OK) {
      stats.tx++;
    } else {
      stats.tx_failed++;
    }
  }
}

}  // namespace radio
}  // namespace kinjo
