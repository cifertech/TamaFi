#include "config.h"
#include "pet_feedback.h"
#include "mic.h"

#include <string.h>

#if TAMAFI_PET_LEDS_ENABLE
#include <Adafruit_NeoPixel.h>
#endif

#if TAMAFI_PET_SOUND_ENABLE
#include <driver/i2s.h>
#endif

#if TAMAFI_PET_LEDS_ENABLE
static Adafruit_NeoPixel s_leds(LED_COUNT, PIN_LED_DI, NEO_GRB + NEO_KHZ800);
#endif

#if TAMAFI_PET_SOUND_ENABLE
static const i2s_port_t kAmpPort = I2S_NUM_1;
static const uint32_t kSampleRate = 16000;
static const int16_t kVoiceAmp = 16800;
static const int16_t kSleepAmp = 7800;

enum ChirpKind : uint8_t { CHIRP_TONE = 0, CHIRP_SWEEP, CHIRP_WARB, CHIRP_CHAT, CHIRP_GAP };

struct Chirp {
  uint8_t kind;
  uint16_t f0;
  uint16_t f1;
  uint16_t ms;
};

static bool s_soundOk = false;
static bool s_playing = false;
static Chirp s_phrase[10];
static uint8_t s_phraseLen = 0;
static uint8_t s_phraseIdx = 0;
static uint8_t s_kind = CHIRP_GAP;
static uint16_t s_f0 = 0;
static uint16_t s_f1 = 0;
static uint32_t s_notePos = 0;
static uint32_t s_noteTotal = 0;
static uint32_t s_phase = 0;
static int16_t s_amp = kVoiceAmp;
static uint32_t s_speakUntil = 0;
static TamafiMood s_lastSpeakMood = TAMAFI_MOOD_COUNT;
static uint32_t s_lastSpeakAt = 0;
static uint8_t s_lastVar = 255;
#endif

static bool s_ledOk = false;
static bool s_ledsEnabled = true;
static bool s_soundEnabled = true;
static uint8_t s_ledBriIndex = 1;
static uint32_t s_ledUntil = 0;

static uint8_t ledBrightnessValue(uint8_t index) {
  if (index == 0) return 20;
  if (index == 2) return 180;
  return 90;
}

static void ledsHardOff() {
#if TAMAFI_PET_LEDS_ENABLE
  if (!s_ledOk) return;
  for (int pass = 0; pass < 3; ++pass) {
    for (int i = 0; i < LED_COUNT; ++i) s_leds.setPixelColor(i, 0);
    s_leds.show();
    delay(2);
  }
#endif
}

static uint32_t ledColorForMood(TamafiMood mood) {
#if TAMAFI_PET_LEDS_ENABLE
  switch (mood) {
    case TAMAFI_MOOD_HAPPY:
      return s_leds.Color(233, 196, 106);
    case TAMAFI_MOOD_EXCITED:
      return s_leds.Color(244, 162, 97);
    case TAMAFI_MOOD_THINK:
    case TAMAFI_MOOD_SURPRISED:
      return s_leds.Color(0, 107, 112);
    case TAMAFI_MOOD_SLEEP:
      return s_leds.Color(38, 70, 83);
    case TAMAFI_MOOD_SAD:
    case TAMAFI_MOOD_BORED:
      return s_leds.Color(0, 70, 75);
    case TAMAFI_MOOD_AIR_ATTACK:
      return s_leds.Color(231, 111, 81);
    case TAMAFI_MOOD_AIR_NOISE:
      return s_leds.Color(244, 162, 97);
    case TAMAFI_MOOD_AIR_OPEN:
      return s_leds.Color(233, 196, 106);
    default:
      return s_leds.Color(0, 107, 112);
  }
#else
  (void)mood;
  return 0;
#endif
}

#if TAMAFI_PET_SOUND_ENABLE
static void phraseClear() {
  s_phraseLen = 0;
  s_phraseIdx = 0;
}

static void phraseAdd(uint8_t kind, uint16_t f0, uint16_t f1, uint16_t ms) {
  if (s_phraseLen >= 10) return;
  s_phrase[s_phraseLen++] = {kind, f0, f1, ms};
}

static void startNote() {
  while (s_phraseIdx < s_phraseLen) {
    const Chirp& n = s_phrase[s_phraseIdx];
    s_kind = n.kind;
    s_f0 = n.f0;
    s_f1 = n.f1;
    s_noteTotal = ((uint32_t)n.ms * kSampleRate) / 1000;
    s_notePos = 0;
    s_phase = 0;
    if (s_noteTotal) return;
    s_phraseIdx++;
  }
  s_playing = false;
}

static uint8_t pickVariant(TamafiMood mood, uint8_t n) {
  uint8_t v = (uint8_t)(esp_random() % n);
  if (n > 1 && mood == s_lastSpeakMood && v == s_lastVar) v = (uint8_t)((v + 1) % n);
  s_lastVar = v;
  return v;
}

static void detunePhrase() {
  const int detune = (int)(esp_random() % 71) - 35;
  for (uint8_t i = 0; i < s_phraseLen; ++i) {
    if (s_phrase[i].kind == CHIRP_GAP || s_phrase[i].f0 == 0) continue;
    int f0 = (int)s_phrase[i].f0 + detune;
    int f1 = (int)s_phrase[i].f1 + detune;
    if (f0 < 180) f0 = 180;
    if (f1 < 180) f1 = 180;
    if (f0 > 3200) f0 = 3200;
    if (f1 > 3200) f1 = 3200;
    s_phrase[i].f0 = (uint16_t)f0;
    s_phrase[i].f1 = (uint16_t)f1;
  }
}

static bool shouldSpeak(const char* label, TamafiMood mood) {
  (void)mood;
  if (!label) return false;
  if (strstr(label, "look") || strcmp(label, "rest-start") == 0) return false;
  if (strstr(label, "attack") || strstr(label, "noisy") || strstr(label, "preview")) return true;
  if (strstr(label, "evolve") || strstr(label, "petted") || strstr(label, "heard")) return true;
  if (strstr(label, "stamp")) return true;
  if (strstr(label, "hunt-start") || strstr(label, "discover-start")) return true;
  if (strstr(label, "-ok") || strstr(label, "fail") || strstr(label, "empty") ||
      strstr(label, "woke")) {
    return true;
  }
  return false;
}

static void buildDroidPhrase(TamafiMood mood) {
  phraseClear();
  const uint8_t v = pickVariant(mood, 3);
  switch (mood) {
    case TAMAFI_MOOD_HAPPY:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 1320, 1320, 18);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_TONE, 1760, 1760, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_SWEEP, 1200, 2480, 55);
        phraseAdd(CHIRP_GAP, 0, 0, 12);
        phraseAdd(CHIRP_TONE, 1680, 1680, 22);
      } else if (v == 1) {
        phraseAdd(CHIRP_CHAT, 1400, 2200, 70);
        phraseAdd(CHIRP_GAP, 0, 0, 12);
        phraseAdd(CHIRP_SWEEP, 1500, 2300, 45);
      } else {
        phraseAdd(CHIRP_SWEEP, 1100, 1900, 40);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_TONE, 2100, 2100, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_TONE, 1680, 1680, 28);
      }
      break;
    case TAMAFI_MOOD_EXCITED:
      if (v == 0) {
        phraseAdd(CHIRP_CHAT, 1600, 2600, 80);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_SWEEP, 1400, 2700, 42);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_TONE, 2200, 2200, 18);
      } else if (v == 1) {
        phraseAdd(CHIRP_TONE, 1800, 1800, 14);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_TONE, 2200, 2200, 14);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_SWEEP, 1600, 2600, 40);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_WARB, 1800, 2400, 50);
      } else {
        phraseAdd(CHIRP_SWEEP, 1000, 2400, 35);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_CHAT, 1700, 2500, 70);
      }
      break;
    case TAMAFI_MOOD_LOVE:
      phraseAdd(CHIRP_TONE, 1400, 1400, 20);
      phraseAdd(CHIRP_GAP, 0, 0, 12);
      phraseAdd(CHIRP_SWEEP, 1200, 2100, 60);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_TONE, 1880, 1880, 24);
      break;
    case TAMAFI_MOOD_THINK:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 1080, 1080, 18);
        phraseAdd(CHIRP_GAP, 0, 0, 28);
        phraseAdd(CHIRP_SWEEP, 900, 1680, 70);
      } else if (v == 1) {
        phraseAdd(CHIRP_SWEEP, 1200, 820, 45);
        phraseAdd(CHIRP_GAP, 0, 0, 16);
        phraseAdd(CHIRP_SWEEP, 900, 1560, 55);
      } else {
        phraseAdd(CHIRP_TONE, 980, 980, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 14);
        phraseAdd(CHIRP_TONE, 1280, 1280, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 22);
        phraseAdd(CHIRP_SWEEP, 1100, 1700, 50);
      }
      break;
    case TAMAFI_MOOD_SURPRISED:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 2400, 2400, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_SWEEP, 2000, 1100, 50);
      } else if (v == 1) {
        phraseAdd(CHIRP_SWEEP, 900, 2500, 38);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_TONE, 1800, 1800, 20);
      } else {
        phraseAdd(CHIRP_CHAT, 1600, 2400, 50);
        phraseAdd(CHIRP_GAP, 0, 0, 8);
        phraseAdd(CHIRP_TONE, 2200, 2200, 18);
      }
      break;
    case TAMAFI_MOOD_SLEEP:
      phraseAdd(CHIRP_TONE, 880, 880, 22);
      phraseAdd(CHIRP_GAP, 0, 0, 40);
      phraseAdd(CHIRP_SWEEP, 820, 480, 90);
      break;
    case TAMAFI_MOOD_SAD:
      phraseAdd(CHIRP_SWEEP, 1700, 900, 55);
      phraseAdd(CHIRP_GAP, 0, 0, 16);
      phraseAdd(CHIRP_TONE, 760, 760, 28);
      phraseAdd(CHIRP_GAP, 0, 0, 12);
      phraseAdd(CHIRP_TONE, 640, 640, 36);
      break;
    case TAMAFI_MOOD_BORED:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 720, 720, 24);
        phraseAdd(CHIRP_GAP, 0, 0, 36);
        phraseAdd(CHIRP_TONE, 640, 640, 40);
      } else if (v == 1) {
        phraseAdd(CHIRP_SWEEP, 900, 560, 70);
      } else {
        phraseAdd(CHIRP_TONE, 680, 680, 18);
        phraseAdd(CHIRP_GAP, 0, 0, 40);
        phraseAdd(CHIRP_SWEEP, 700, 500, 50);
      }
      break;
    case TAMAFI_MOOD_CONFUSED:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 1680, 1680, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 12);
        phraseAdd(CHIRP_TONE, 980, 980, 18);
        phraseAdd(CHIRP_GAP, 0, 0, 12);
        phraseAdd(CHIRP_SWEEP, 1200, 1900, 45);
      } else if (v == 1) {
        phraseAdd(CHIRP_SWEEP, 1600, 800, 40);
        phraseAdd(CHIRP_GAP, 0, 0, 12);
        phraseAdd(CHIRP_SWEEP, 900, 1700, 45);
      } else {
        phraseAdd(CHIRP_CHAT, 900, 1700, 70);
        phraseAdd(CHIRP_GAP, 0, 0, 10);
        phraseAdd(CHIRP_TONE, 1400, 1400, 20);
      }
      break;
    case TAMAFI_MOOD_SHY:
      if (v == 0) {
        phraseAdd(CHIRP_TONE, 980, 980, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 22);
        phraseAdd(CHIRP_SWEEP, 860, 1400, 50);
      } else if (v == 1) {
        phraseAdd(CHIRP_SWEEP, 1100, 780, 40);
        phraseAdd(CHIRP_GAP, 0, 0, 16);
        phraseAdd(CHIRP_TONE, 1200, 1200, 20);
      } else {
        phraseAdd(CHIRP_TONE, 880, 880, 14);
        phraseAdd(CHIRP_GAP, 0, 0, 18);
        phraseAdd(CHIRP_TONE, 1180, 1180, 16);
        phraseAdd(CHIRP_GAP, 0, 0, 14);
        phraseAdd(CHIRP_TONE, 1020, 1020, 22);
      }
      break;
    case TAMAFI_MOOD_SCARED:
      phraseAdd(CHIRP_TONE, 2100, 2100, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_TONE, 2100, 2100, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_SWEEP, 1800, 900, 45);
      break;
    case TAMAFI_MOOD_LAUGH:
      phraseAdd(CHIRP_TONE, 1500, 1500, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_TONE, 1800, 1800, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_TONE, 2100, 2100, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_CHAT, 1600, 2300, 55);
      break;
    case TAMAFI_MOOD_DIZZY:
      phraseAdd(CHIRP_WARB, 900, 1900, 90);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_CHAT, 800, 1600, 70);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_SWEEP, 1600, 700, 50);
      break;
    case TAMAFI_MOOD_LISTEN:
      phraseAdd(CHIRP_TONE, 1200, 1200, 16);
      phraseAdd(CHIRP_GAP, 0, 0, 18);
      phraseAdd(CHIRP_SWEEP, 1000, 1680, 55);
      break;
    case TAMAFI_MOOD_AIR_ATTACK:
      phraseAdd(CHIRP_CHAT, 1600, 2600, 70);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_TONE, 2400, 2400, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_SWEEP, 2000, 900, 40);
      phraseAdd(CHIRP_GAP, 0, 0, 8);
      phraseAdd(CHIRP_WARB, 1700, 2500, 55);
      break;
    case TAMAFI_MOOD_AIR_NOISE:
      phraseAdd(CHIRP_TONE, 1400, 1400, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_TONE, 1760, 1760, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_TONE, 2100, 2100, 14);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_SWEEP, 1200, 2200, 50);
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      phraseAdd(CHIRP_TONE, 900, 900, 20);
      phraseAdd(CHIRP_GAP, 0, 0, 22);
      phraseAdd(CHIRP_SWEEP, 800, 1500, 55);
      break;
    case TAMAFI_MOOD_ANGRY:
      phraseAdd(CHIRP_CHAT, 700, 1400, 60);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_TONE, 640, 640, 28);
      phraseAdd(CHIRP_GAP, 0, 0, 10);
      phraseAdd(CHIRP_SWEEP, 1100, 500, 45);
      break;
    default:
      phraseAdd(CHIRP_TONE, 1100, 1100, 16);
      phraseAdd(CHIRP_GAP, 0, 0, 12);
      phraseAdd(CHIRP_SWEEP, 900, 1600, 50);
      break;
  }
}

static uint32_t phraseDurationMs() {
  uint32_t ms = 0;
  for (uint8_t i = 0; i < s_phraseLen; ++i) ms += s_phrase[i].ms;
  return ms;
}

static void startDroidPhrase(TamafiMood mood) {
  if (!s_soundOk || !s_soundEnabled) return;
  buildDroidPhrase(mood);
  detunePhrase();
  s_amp = (mood == TAMAFI_MOOD_SLEEP) ? kSleepAmp : kVoiceAmp;
  s_playing = s_phraseLen > 0;
  s_phraseIdx = 0;
  startNote();
  const uint32_t hold = phraseDurationMs() + 850;
  s_speakUntil = millis() + hold;
  s_lastSpeakMood = mood;
  s_lastSpeakAt = millis();
  micHoldoff(hold);
}

static int16_t nextVoiceSample() {
  if (!s_playing) return 0;
  if (s_notePos >= s_noteTotal) {
    s_phraseIdx++;
    startNote();
    if (!s_playing) return 0;
  }

  const uint32_t pos = s_notePos++;
  if (s_kind == CHIRP_GAP || s_f0 == 0) return 0;

  uint16_t f = s_f0;
  if (s_kind == CHIRP_SWEEP && s_noteTotal) {
    uint32_t t = (pos << 8) / s_noteTotal;
    t = (t * t) >> 8;
    const int32_t df = (int32_t)s_f1 - (int32_t)s_f0;
    f = (uint16_t)((int32_t)s_f0 + ((df * (int32_t)t) >> 8));
    if ((pos / (kSampleRate / 55)) & 1) {
      f = (uint16_t)(f + (f / 28));
    }
  } else if (s_kind == CHIRP_WARB) {
    f = ((pos / (kSampleRate / 38)) & 1) ? s_f1 : s_f0;
  } else if (s_kind == CHIRP_CHAT) {
    const uint32_t hop = kSampleRate / 90;
    const uint32_t i = hop ? (pos / hop) : 0;
    const int32_t df = (int32_t)s_f1 - (int32_t)s_f0;
    f = (uint16_t)((int32_t)s_f0 + (df * (int32_t)(i % 5)) / 4);
  }
  if (f < 180) f = 180;

  s_phase += ((uint32_t)f << 16) / kSampleRate;
  const uint16_t p = (uint16_t)s_phase;

  int32_t amp = s_amp;
  const uint32_t att = 32;
  const uint32_t rel = 70;
  if (pos < att) amp = (amp * (int32_t)pos) / (int32_t)att;
  else if (s_noteTotal > rel && s_noteTotal - pos < rel) {
    amp = (amp * (int32_t)(s_noteTotal - pos)) / (int32_t)rel;
  }
  if (pos < 24) amp += 4000;

  if (p < 19000) return (int16_t)amp;
  return (int16_t)(-(amp / 3));
}
#endif

bool petFeedbackInit() {
#if TAMAFI_PET_LEDS_ENABLE
  pinMode(PIN_LED_DI, OUTPUT);
  digitalWrite(PIN_LED_DI, LOW);
  delay(3);
  s_leds.begin();
  s_leds.setBrightness(ledBrightnessValue(s_ledBriIndex));
  s_ledOk = true;
  ledsHardOff();
  Serial.println("Pet feedback: LEDs ready");
#else
  s_ledOk = false;
#endif

#if TAMAFI_PET_SOUND_ENABLE
  if (ESP.getFreeHeap() < TAMAFI_PET_SOUND_MIN_FREE_HEAP) {
    Serial.printf("Pet feedback: sound skipped, heap=%u\n", ESP.getFreeHeap());
    return true;
  }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 3;
  cfg.dma_buf_len = 128;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_AMP_BCLK;
  pins.ws_io_num = PIN_AMP_LRCLK;
  pins.data_out_num = PIN_AMP_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kAmpPort, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("Pet feedback: sound install failed");
    return true;
  }
  if (i2s_set_pin(kAmpPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kAmpPort);
    Serial.println("Pet feedback: sound pins failed");
    return true;
  }
  i2s_zero_dma_buffer(kAmpPort);
  s_soundOk = true;
  Serial.println("Pet feedback: MAX98357A ready");
#endif

  return true;
}

void petFeedbackUpdate() {
  const uint32_t now = millis();

#if TAMAFI_PET_LEDS_ENABLE
  if (s_ledOk && s_ledUntil && now > s_ledUntil) {
    s_ledUntil = 0;
    ledsHardOff();
  }
#endif

#if TAMAFI_PET_SOUND_ENABLE
  if (!s_soundOk || !s_playing) return;

  int16_t samples[256];
  for (int pass = 0; pass < 2 && s_playing; ++pass) {
    bool any = false;
    for (int i = 0; i < 256; ++i) {
      samples[i] = nextVoiceSample();
      if (samples[i]) any = true;
    }
    if (!s_playing && !any) {
      i2s_zero_dma_buffer(kAmpPort);
      break;
    }
    size_t written = 0;
    i2s_write(kAmpPort, samples, sizeof(samples), &written, 0);
  }
#endif
}

void petFeedbackEvent(const char* label, TamafiMood mood) {
  (void)label;
  if (mood == TAMAFI_MOOD_IDLE || mood == TAMAFI_MOOD_BLINK) {
    ledsHardOff();
    return;
  }

#if TAMAFI_PET_LEDS_ENABLE
  if (s_ledOk && s_ledsEnabled) {
    const uint32_t color = ledColorForMood(mood);
    for (int i = 0; i < LED_COUNT; ++i) s_leds.setPixelColor(i, color);
    s_leds.show();
    s_ledUntil = millis() + (mood == TAMAFI_MOOD_AIR_ATTACK ? 1800 : 900);
  }
#endif

#if TAMAFI_PET_SOUND_ENABLE
  if (shouldSpeak(label, mood)) startDroidPhrase(mood);
#endif
}

bool petFeedbackSpeaking() {
#if TAMAFI_PET_SOUND_ENABLE
  return s_playing || millis() < s_speakUntil;
#else
  return false;
#endif
}

void petFeedbackSetSoundEnabled(bool enabled) {
  s_soundEnabled = enabled;
#if TAMAFI_PET_SOUND_ENABLE
  if (!enabled) {
    s_playing = false;
    s_speakUntil = 0;
    if (s_soundOk) i2s_zero_dma_buffer(kAmpPort);
  }
#endif
}

void petFeedbackSetLedsEnabled(bool enabled) {
  s_ledsEnabled = enabled;
  if (!enabled) ledsHardOff();
}

void petFeedbackSetLedBrightness(uint8_t index) {
  if (index > 2) index = 2;
  s_ledBriIndex = index;
#if TAMAFI_PET_LEDS_ENABLE
  if (s_ledOk) s_leds.setBrightness(ledBrightnessValue(s_ledBriIndex));
#endif
}

bool petFeedbackSoundEnabled() { return s_soundEnabled; }
bool petFeedbackLedsEnabled() { return s_ledsEnabled; }
uint8_t petFeedbackLedBrightness() { return s_ledBriIndex; }
