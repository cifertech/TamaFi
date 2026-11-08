#include "event_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint8_t kCap = 28;
static LogEntry s_buf[kCap];
static uint8_t s_head = 0;
static uint8_t s_count = 0;
static uint32_t s_seq = 0;
static uint32_t s_lastAt = 0;
static char s_lastText[22];

void eventLogPush(LogKind kind, const char* text) {
  if (!text || !text[0]) return;
  const uint32_t now = millis();
  if (s_count && now - s_lastAt < 700 && strncmp(s_lastText, text, 21) == 0) return;

  LogEntry& e = s_buf[s_head];
  e.ms = now;
  e.kind = kind;
  uint8_t n = 0;
  while (text[n] && n < 21) {
    e.text[n] = text[n];
    n++;
  }
  e.text[n] = 0;
  strncpy(s_lastText, e.text, 21);
  s_lastText[21] = 0;
  s_lastAt = now;
  s_head = (uint8_t)((s_head + 1) % kCap);
  if (s_count < kCap) s_count++;
  s_seq++;
}

void eventLogPushf(LogKind kind, const char* fmt, ...) {
  char buf[22];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  eventLogPush(kind, buf);
}

uint8_t eventLogCount() { return s_count; }

uint32_t eventLogSeq() { return s_seq; }

const LogEntry* eventLogAt(uint8_t age) {
  if (age >= s_count) return nullptr;
  int i = (int)s_head - 1 - (int)age;
  while (i < 0) i += kCap;
  return &s_buf[i];
}

const char* eventLogKindName(LogKind kind) {
  switch (kind) {
    case LOG_HUNT: return "HUNT";
    case LOG_DISC: return "DISC";
    case LOG_REST: return "REST";
    case LOG_AIR: return "AIR ";
    case LOG_WIFI: return "WIFI";
    case LOG_BLE: return "BLE ";
    case LOG_IMU: return "IMU ";
    case LOG_MIC: return "MIC ";
    case LOG_SYS: return "SYS ";
    default: return "PET ";
  }
}

uint16_t eventLogKindColor(LogKind kind) {
  switch (kind) {
    case LOG_HUNT: return 0x067A;
    case LOG_DISC: return 0xE36A;
    case LOG_REST: return 0x54D6;
    case LOG_AIR: return 0xE36A;
    case LOG_WIFI: return 0x067A;
    case LOG_BLE: return 0xE36A;
    case LOG_IMU: return 0xE36A;
    case LOG_MIC: return 0x067A;
    case LOG_SYS: return 0x54D6;
    default: return 0xE36A;
  }
}
