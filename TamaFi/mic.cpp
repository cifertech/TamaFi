#include "config.h"
#include "mic.h"

#include <driver/i2s.h>
#include <math.h>

static const i2s_port_t kPort = I2S_NUM_0;
static const int kChunk = 128;

static bool s_ok = false;
static float s_level = 0;
static float s_dc = 0;
static float s_noise = 0.02f;
static float s_peak = 0;
static float s_valley = 0;
static bool s_calibrated = false;
static uint8_t s_calN = 0;
static uint8_t s_warmup = 0;
static float s_calSum = 0;
static bool s_hearing = false;
static uint32_t s_loudSince = 0;
static uint32_t s_quietSince = 0;
static uint32_t s_hearAt = 0;
static uint32_t s_holdoffUntil = 0;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool micInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("MIC: I2S install failed");
    return false;
  }
  if (i2s_set_pin(kPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kPort);
    Serial.println("MIC: I2S pins failed");
    return false;
  }
  i2s_zero_dma_buffer(kPort);
  s_ok = true;
  Serial.println("MIC: INMP441 on GPIO40/41/42");
  return true;
}

bool micOk() { return s_ok; }

void micUpdate() {
  if (!s_ok) return;

  int32_t raw[kChunk];
  size_t bytes = 0;
  if (i2s_read(kPort, raw, sizeof(raw), &bytes, 0) != ESP_OK || bytes < 4) return;

  const int n = (int)(bytes / sizeof(int32_t));
  if (s_warmup < 14) {
    for (int i = 0; i < n; ++i) s_dc += (((float)(raw[i] >> 14)) - s_dc) * 0.08f;
    s_warmup++;
    return;
  }

  double acc = 0;
  for (int i = 0; i < n; ++i) {
    const float s = (float)(raw[i] >> 14);
    s_dc += (s - s_dc) * 0.002f;
    const float x = s - s_dc;
    acc += (double)x * (double)x;
  }
  float rms = sqrtf((float)(acc / n)) / 32768.0f;
  if (rms > 1.2f) rms = 1.2f;
  s_level += (rms - s_level) * 0.4f;

  if (s_level > s_peak) s_peak += (s_level - s_peak) * 0.55f;
  else s_peak += (s_level - s_peak) * 0.08f;
  if (s_level < s_valley) s_valley += (s_level - s_valley) * 0.55f;
  else s_valley += (s_level - s_valley) * 0.08f;
  const float mod = s_peak - s_valley;

  if (!s_calibrated) {
    s_calSum += s_level;
    if (++s_calN >= 40) {
      s_noise = clampf(s_calSum / s_calN, 0.006f, 0.18f);
      s_calibrated = true;
      Serial.print("MIC: noise ");
      Serial.println(s_noise, 4);
    }
    return;
  }

  s_noise += (s_level - s_noise) * (s_hearing ? 0.008f : 0.018f);
  s_noise = clampf(s_noise, 0.006f, 0.2f);

  const float on = s_noise + 0.016f;
  const float off = s_noise + 0.007f;
  const uint32_t now = millis();

  if (now < s_holdoffUntil) {
    s_hearing = false;
    s_loudSince = 0;
    s_quietSince = now;
    s_hearAt = 0;
    return;
  }

  if (s_level > on && mod > 0.007f) {
    if (!s_loudSince) s_loudSince = now;
    s_quietSince = 0;
    if (!s_hearing && now - s_loudSince > 55) {
      s_hearing = true;
      s_hearAt = now;
    }
  } else {
    s_loudSince = 0;
    if (!s_quietSince) s_quietSince = now;
    if (s_hearing && now - s_quietSince > 550) s_hearing = false;
  }

  if (s_hearing && now - s_hearAt > 2800) {
    s_hearing = false;
    s_noise = clampf(s_level, 0.006f, 0.2f);
  }
}

float micLevel() { return s_level; }

bool micHearing() { return s_calibrated && s_hearing && millis() >= s_holdoffUntil; }

void micHoldoff(uint32_t extraMs) {
  const uint32_t until = millis() + extraMs;
  if (until > s_holdoffUntil) s_holdoffUntil = until;
  s_hearing = false;
  s_loudSince = 0;
  s_quietSince = millis();
  s_hearAt = 0;
}

bool micHeldOff() { return millis() < s_holdoffUntil; }
