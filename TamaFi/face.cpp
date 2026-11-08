#include "config.h"
#include "face.h"
#include "ui_glcd.h"

#include <math.h>
#include <string.h>

static TFT_eSPI* s_tft = nullptr;
static TFT_eSprite* s_spr = nullptr;
static bool s_use_sprite = false;
static bool s_static_sprite = false;

#if TAMAFI_FACE_USE_FULL_SPRITE && TAMAFI_FACE_USE_STATIC_SPRITE
class TamafiStaticSprite : public TFT_eSprite {
 public:
  explicit TamafiStaticSprite(TFT_eSPI* tft) : TFT_eSprite(tft) {}

  bool attach16(uint16_t* buffer, int16_t w, int16_t h) {
    if (!buffer || w <= 0 || h <= 0) return false;
    m_buffer = buffer;
    m_w = w;
    m_h = h;
    _bpp = 16;
    uint8_t* bytes = (uint8_t*)buffer;
    _init_width = w;
    _init_height = h;
    _width = w;
    _height = h;
    _iwidth = _dwidth = _bitwidth = w;
    _iheight = _dheight = h;
    _vpX = 0;
    _vpY = 0;
    _vpW = w;
    _vpH = h;
    _xDatum = 0;
    _yDatum = 0;
    _xWidth = w;
    _yHeight = h;
    _vpDatum = false;
    _vpOoB = false;
    _sx = 0;
    _sy = 0;
    _sw = w;
    _sh = h;
    _scolor = TFT_BLACK;
    _img8 = bytes;
    _img8_1 = bytes;
    _img8_2 = bytes;
    _img = buffer;
    _img4 = bytes;
    _created = true;
    rotation = 0;
    setViewport(0, 0, _dwidth, _dheight);
    resetViewport();
    _vpOoB = false;
    setTextFont(1);
    setTextSize(1);
    setPivot(_iwidth / 2, _iheight / 2);
    fillSprite(TFT_BLACK);
    return true;
  }

  size_t write(uint8_t utf8) override {
    if (utf8 == '\r') return 1;
    if (utf8 == '\n') {
      cursor_y += 8;
      cursor_x = 0;
      return 1;
    }
    glcdDrawChar(*this, cursor_x, cursor_y, (char)utf8, textcolor, textbgcolor);
    cursor_x += 6;
    return 1;
  }

  void drawPixel(int32_t x, int32_t y, uint32_t color) override {
    if (!m_buffer || x < 0 || y < 0 || x >= m_w || y >= m_h) return;
    m_buffer[y * m_w + x] = pack(color);
  }

  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) override {
    if (!m_buffer || y < 0 || y >= m_h || w <= 0) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (x + w > m_w) w = m_w - x;
    if (w <= 0) return;
    uint16_t c = pack(color);
    uint16_t* row = m_buffer + y * m_w + x;
    while (w--) *row++ = c;
  }

  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override {
    if (!m_buffer || x < 0 || x >= m_w || h <= 0) return;
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (y + h > m_h) h = m_h - y;
    if (h <= 0) return;
    uint16_t c = pack(color);
    uint16_t* p = m_buffer + y * m_w + x;
    while (h--) {
      *p = c;
      p += m_w;
    }
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) override {
    if (!m_buffer || w <= 0 || h <= 0) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x + w > m_w) w = m_w - x;
    if (y + h > m_h) h = m_h - y;
    if (w <= 0 || h <= 0) return;
    uint16_t c = pack(color);
    for (int32_t yy = 0; yy < h; ++yy) {
      uint16_t* row = m_buffer + (y + yy) * m_w + x;
      for (int32_t xx = 0; xx < w; ++xx) row[xx] = c;
    }
  }

  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) override {
    int32_t dx = abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    while (true) {
      drawPixel(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      int32_t e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  void fillSprite(uint32_t color) {
    fillRect(0, 0, m_w, m_h, color);
  }

 private:
  static uint16_t pack(uint32_t color) { return (uint16_t)((color >> 8) | (color << 8)); }

  uint16_t* m_buffer = nullptr;
  int16_t m_w = 0;
  int16_t m_h = 0;
};

static uint16_t s_spriteBuffer[TAMAFI_FACE_SIZE * TAMAFI_FACE_SIZE + 1];
#endif

struct FaceRect {
  int x;
  int y;
  int w;
  int h;
  bool valid;
};

struct EmoPose {
  float gazeX, gazeY;
  float eyeW, eyeH;
  float gap;
  float tilt;
  float brow;
  float browTilt;
  float mouth;
  float mouthOpen;
  float faceY;
  float bounce;
  float blush;
  float style;
  float dim;
};

static TamafiMood s_mood = TAMAFI_MOOD_IDLE;
static EmoPose s_live, s_target, s_vel;
static bool s_ready = false;

static float s_gazeX = 0, s_gazeY = 0, s_gazeVX = 0, s_gazeVY = 0;
static float s_gazeTX = 0, s_gazeTY = 0;
static uint32_t s_nextSaccade = 0;
static bool s_extLook = false;
static float s_extLookX = 0, s_extLookY = 0;

static float s_lidL = 1, s_lidR = 1;
static float s_lidVL = 0, s_lidVR = 0;
static float s_lidTL = 1, s_lidTR = 1;
static bool s_blinking = false;
static bool s_doubleBlink = false;
static uint32_t s_blinkStart = 0;
static uint32_t s_nextBlink = 0;

static float s_intro = 1;
static float s_scale = 1, s_scaleV = 0;
static uint32_t s_lastTick = 0;
static uint32_t s_lastFrame = 0;
static FaceRect s_lastBounds = {0, 0, 0, 0, false};

enum WakePhase : uint8_t { WAKE_SCAN = 0, WAKE_DECODE, WAKE_MORPH, WAKE_DONE };
static WakePhase s_wake = WAKE_DONE;
static uint32_t s_wakeAt = 0;

static const uint8_t kAsciiRows = 8;
static const uint8_t kAsciiCols = 25;
static const uint16_t kAsciiScanMs = 780;
static const uint16_t kAsciiDecodeMs = 560;
static const uint16_t kAsciiHoldMs = 200;
static const uint16_t kAsciiMorphMs = 820;

enum BeatPhase : uint8_t { BP_ANTIC = 0, BP_HIT, BP_HOLD, BP_SETTLE, BP_IDLE };
static BeatPhase s_beat = BP_IDLE;
static uint32_t s_beatAt = 0;
static float s_punch = 1, s_punchV = 0, s_punchT = 1;
static float s_shakeX = 0, s_shakeV = 0, s_shakeT = 0;
static uint8_t s_dizzyKind = 0;
static uint8_t s_dizzyLast = 255;
static const uint8_t kDizzyKinds = 4;
static bool s_plainEyes = false;
static char s_say[17] = {0};
static uint32_t s_sayAt = 0;
static uint32_t s_sayUntil = 0;

static const char* kMoodNames[TAMAFI_MOOD_COUNT] = {
    "idle",    "happy", "sad",     "angry",   "wow",    "love",
    "huh",     "bored", "scared",  "excited", "shy",    "laugh",
    "wink",    "dizzy", "cry",     "sleep",   "listen", "think",
    "speak",   "blink", "deauth",  "probe",   "open"};

static const uint16_t kWhite = 0xFFDE;
static const uint16_t kEye = 0xEF7D;
static const uint16_t kSheen = 0xFFFF;
static const uint16_t kHeart = 0xFCD3;
static const uint16_t kHeartDeep = 0xF00A;
static const uint16_t kBlush = 0xC9AA;
static const uint16_t kTear = 0x7DFF;
static const uint16_t kBrow = 0xD6BA;
static const uint16_t kLidLine = 0x4A69;
static const uint16_t kSpark = 0xEF7D;
static const uint16_t kAlert = 0xE36A;
static const uint16_t kWarnY = 0xF50C;

static const float kMinGap = 28.0f;
static const int kCorner = 10;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static float smootherstep(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static void spring(float& x, float& v, float t, float dt, float w, float z) {
  const float a = -w * w * (x - t) - 2.0f * z * w * v;
  v += a * dt;
  x += v * dt;
}

static uint16_t mix565(uint16_t a, uint16_t b, float t) {
  t = clampf(t, 0.0f, 1.0f);
  const int r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
  const int r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
  return (uint16_t)(((r1 + (int)((r2 - r1) * t)) << 11) |
                    ((g1 + (int)((g2 - g1) * t)) << 5) |
                    (b1 + (int)((b2 - b1) * t)));
}

template <typename Display>
static void drawSpeechBubble(Display& d, int faceCx) {
  if (!s_say[0] || millis() > s_sayUntil) return;
  if (s_mood == TAMAFI_MOOD_SLEEP) return;

  const uint32_t shown = (millis() - s_sayAt) / 38;
  char buf[17];
  uint8_t n = 0;
  while (s_say[n] && n < 16 && n < shown) {
    buf[n] = s_say[n];
    n++;
  }
  if (n == 0) {
    buf[0] = s_say[0];
    n = 1;
  }
  buf[n] = 0;

  const int tw = glcdTextW(buf, GLCD_L);
  const int padX = 6;
  const int padY = 4;
  const int bw = tw + padX * 2;
  const int bh = glcdCharH(GLCD_L) + padY * 2;
  int bx = faceCx - bw / 2;
  int by = TAMAFI_FACE_SIZE - bh - 10 + (int)(sinf(millis() / 180.0f) * 1.2f);
  if (bx < 3) bx = 3;
  if (bx + bw > TAMAFI_FACE_SIZE - 3) bx = TAMAFI_FACE_SIZE - 3 - bw;
  if (by < 168) by = 168;

  const uint16_t fill = 0x3186;
  const uint16_t edge = mix565(kEye, 0x0000, 0.25f);
  const int mx = faceCx;
  d.fillTriangle(mx - 6, by + 1, mx + 6, by + 1, mx, by - 8, fill);
  d.drawLine(mx - 6, by + 1, mx, by - 8, edge);
  d.drawLine(mx + 6, by + 1, mx, by - 8, edge);
  d.fillRoundRect(bx, by, bw, bh, 4, edge);
  d.fillRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 3, fill);
  glcdDrawText(d, bx + padX, by + padY, buf, kWhite, fill, GLCD_L);
}

static EmoPose poseFor(TamafiMood mood) {
  EmoPose p = {0, 0, 54, 54, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  switch (mood) {
    case TAMAFI_MOOD_HAPPY:
      p.eyeH = 24;
      p.mouth = 1;
      p.mouthOpen = 0.55f;
      p.bounce = 1;
      p.faceY = -3;
      break;
    case TAMAFI_MOOD_SAD:
      p.tilt = -0.32f;
      p.brow = 0.95f;
      p.browTilt = -0.8f;
      p.mouth = 3;
      p.mouthOpen = 0.3f;
      p.faceY = 6;
      p.dim = 0.08f;
      break;
    case TAMAFI_MOOD_ANGRY:
      p.eyeW = 50;
      p.eyeH = 42;
      p.tilt = 0.62f;
      p.brow = 1.1f;
      p.browTilt = 1.1f;
      p.mouth = 6;
      p.mouthOpen = 0.35f;
      break;
    case TAMAFI_MOOD_SURPRISED:
      p.eyeW = 60;
      p.eyeH = 60;
      p.gap = 28;
      p.brow = 0.7f;
      p.mouth = 4;
      p.mouthOpen = 0.9f;
      p.faceY = -5;
      break;
    case TAMAFI_MOOD_LOVE:
      p.style = 1;
      p.eyeW = 48;
      p.eyeH = 48;
      p.mouth = 1;
      p.mouthOpen = 0.4f;
      p.blush = 0.85f;
      p.bounce = 0.7f;
      break;
    case TAMAFI_MOOD_CONFUSED:
      p.brow = 0.95f;
      p.browTilt = 1.05f;
      p.tilt = 0.18f;
      p.mouth = 4;
      p.mouthOpen = 0.22f;
      break;
    case TAMAFI_MOOD_BORED:
      p.eyeH = 20;
      p.gazeX = 10;
      p.mouth = 6;
      p.mouthOpen = 0.12f;
      p.faceY = 5;
      p.dim = 0.12f;
      break;
    case TAMAFI_MOOD_SCARED:
      p.eyeW = 58;
      p.eyeH = 58;
      p.gap = 28;
      p.mouth = 4;
      p.mouthOpen = 0.55f;
      p.brow = 0.55f;
      p.browTilt = -0.35f;
      break;
    case TAMAFI_MOOD_EXCITED:
      p.eyeH = 32;
      p.mouth = 1;
      p.mouthOpen = 0.8f;
      p.bounce = 1.5f;
      p.faceY = -5;
      break;
    case TAMAFI_MOOD_SHY:
      p.eyeH = 38;
      p.gazeY = 9;
      p.gazeX = 7;
      p.blush = 1;
      p.mouth = 1;
      p.mouthOpen = 0.18f;
      p.faceY = 8;
      break;
    case TAMAFI_MOOD_LAUGH:
      p.eyeH = 20;
      p.mouth = 1;
      p.mouthOpen = 0.82f;
      p.bounce = 1.15f;
      p.blush = 0.28f;
      p.faceY = -4;
      break;
    case TAMAFI_MOOD_WINK:
      p.mouth = 1;
      p.mouthOpen = 0.38f;
      p.tilt = 0.14f;
      p.blush = 0.16f;
      break;
    case TAMAFI_MOOD_DIZZY:
      p.style = 3;
      p.mouth = 4;
      p.mouthOpen = 0.32f;
      p.tilt = 0.28f;
      p.dim = 0.04f;
      p.brow = 0.35f;
      switch (s_dizzyKind) {
        case 1:
          p.eyeW = 58;
          p.eyeH = 58;
          p.mouth = 4;
          p.mouthOpen = 0.45f;
          break;
        case 2:
          p.eyeW = 50;
          p.eyeH = 50;
          p.gap = 40;
          p.mouth = 2;
          p.mouthOpen = 0.55f;
          p.browTilt = 0.45f;
          break;
        case 3:
          p.eyeW = 52;
          p.eyeH = 46;
          p.mouth = 3;
          p.mouthOpen = 0.4f;
          p.blush = 0;
          p.brow = 0.45f;
          p.browTilt = -0.35f;
          break;
        default:
          p.eyeW = 56;
          p.eyeH = 56;
          break;
      }
      break;
    case TAMAFI_MOOD_CRY:
      p.eyeH = 46;
      p.tilt = -0.36f;
      p.brow = 1;
      p.browTilt = -0.85f;
      p.mouth = 3;
      p.mouthOpen = 0.5f;
      p.faceY = 8;
      p.dim = 0.1f;
      break;
    case TAMAFI_MOOD_SLEEP:
      p.eyeH = 8;
      p.faceY = 7;
      p.dim = 0.28f;
      break;
    case TAMAFI_MOOD_LISTEN:
      p.eyeW = 64;
      p.eyeH = 40;
      p.gap = 30;
      p.tilt = 0.22f;
      p.brow = 0.72f;
      p.browTilt = 0.9f;
      p.gazeX = 4;
      p.gazeY = -2;
      p.mouth = 6;
      p.mouthOpen = 0.1f;
      p.faceY = -2;
      break;
    case TAMAFI_MOOD_THINK:
      p.brow = 0.9f;
      p.browTilt = 0.7f;
      p.tilt = 0.1f;
      p.mouth = 6;
      p.mouthOpen = 0.1f;
      break;
    case TAMAFI_MOOD_SPEAK:
      p.mouth = 2;
      p.mouthOpen = 0.75f;
      p.bounce = 1.1f;
      break;
    case TAMAFI_MOOD_AIR_ATTACK:
      p.eyeW = 56;
      p.eyeH = 56;
      p.gap = 28;
      p.brow = 0.85f;
      p.browTilt = 0.45f;
      p.mouth = 6;
      p.mouthOpen = 0.18f;
      p.faceY = -2;
      break;
    case TAMAFI_MOOD_AIR_NOISE:
      p.eyeW = 60;
      p.eyeH = 28;
      p.gap = 30;
      p.brow = 0.75f;
      p.browTilt = 0.35f;
      p.mouth = 6;
      p.mouthOpen = 0.12f;
      p.faceY = -3;
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      p.eyeW = 50;
      p.eyeH = 28;
      p.brow = 0.7f;
      p.browTilt = -0.25f;
      p.mouth = 6;
      p.mouthOpen = 0.18f;
      p.faceY = 3;
      p.gazeX = 8;
      p.gazeY = 4;
      p.blush = 0.35f;
      break;
    default:
      break;
  }
  return p;
}

static void beatTimes(TamafiMood mood, uint16_t& antic, uint16_t& hit, uint16_t& hold,
                      uint16_t& settle) {
  antic = hit = hold = settle = 0;
  switch (mood) {
    case TAMAFI_MOOD_SURPRISED:
      antic = 80;
      hit = 240;
      hold = 300;
      settle = 520;
      break;
    case TAMAFI_MOOD_ANGRY:
      antic = 90;
      hit = 170;
      hold = 420;
      settle = 380;
      break;
    case TAMAFI_MOOD_LOVE:
      antic = 160;
      hit = 380;
      hold = 340;
      settle = 500;
      break;
    case TAMAFI_MOOD_HAPPY:
      antic = 70;
      hit = 220;
      hold = 200;
      settle = 360;
      break;
    case TAMAFI_MOOD_SCARED:
      antic = 50;
      hit = 180;
      hold = 380;
      settle = 420;
      break;
    case TAMAFI_MOOD_LAUGH:
      antic = 70;
      hit = 220;
      hold = 420;
      settle = 380;
      break;
    case TAMAFI_MOOD_WINK:
      antic = 40;
      hit = 90;
      hold = 220;
      settle = 180;
      break;
    case TAMAFI_MOOD_CRY:
      antic = 180;
      hit = 280;
      hold = 360;
      settle = 480;
      break;
    case TAMAFI_MOOD_EXCITED:
      antic = 60;
      hit = 200;
      hold = 220;
      settle = 320;
      break;
    case TAMAFI_MOOD_CONFUSED:
      antic = 90;
      hit = 180;
      hold = 220;
      settle = 360;
      break;
    case TAMAFI_MOOD_SHY:
      antic = 140;
      hit = 240;
      hold = 280;
      settle = 420;
      break;
    case TAMAFI_MOOD_SAD:
      antic = 200;
      hit = 260;
      hold = 300;
      settle = 460;
      break;
    case TAMAFI_MOOD_DIZZY:
      antic = 70;
      hit = 220;
      hold = 520;
      settle = 480;
      break;
    case TAMAFI_MOOD_LISTEN:
      antic = 50;
      hit = 180;
      hold = 220;
      settle = 280;
      break;
    case TAMAFI_MOOD_AIR_ATTACK:
      antic = 40;
      hit = 200;
      hold = 420;
      settle = 360;
      break;
    case TAMAFI_MOOD_AIR_NOISE:
      antic = 60;
      hit = 160;
      hold = 280;
      settle = 300;
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      antic = 120;
      hit = 200;
      hold = 260;
      settle = 340;
      break;
    default:
      break;
  }
}

static void startBeat() {
  uint16_t a, h, o, s;
  beatTimes(s_mood, a, h, o, s);
  s_beatAt = millis();
  s_beat = (a + h + o + s) ? BP_ANTIC : BP_IDLE;
  if (s_beat == BP_ANTIC && a == 0) s_beat = BP_HIT;
  s_punchT = 1;
  s_shakeT = 0;
}

static BeatPhase beatPhase(uint32_t now, float& localT) {
  uint16_t a, h, o, s;
  beatTimes(s_mood, a, h, o, s);
  const uint32_t e = now - s_beatAt;
  uint32_t acc = 0;
  const uint16_t durs[4] = {a, h, o, s};
  for (uint8_t i = 0; i < 4; ++i) {
    if (e < acc + durs[i]) {
      localT = durs[i] ? (e - acc) / (float)durs[i] : 1.0f;
      return (BeatPhase)i;
    }
    acc += durs[i];
  }
  localT = 1;
  return BP_IDLE;
}

static void applyBeat(EmoPose& t, float localT) {
  const float k = smootherstep(localT);
  s_punchT = 1;
  s_shakeT = 0;
  switch (s_mood) {
    case TAMAFI_MOOD_SURPRISED:
      s_gazeTX = 0;
      s_gazeTY = 0;
      if (s_beat == BP_ANTIC) {
        t.eyeW = 46;
        t.eyeH = 46;
        s_lidTL = s_lidTR = 0.7f;
        t.mouthOpen = 0.15f;
      } else if (s_beat == BP_HIT) {
        t.eyeW = 74;
        t.eyeH = 74;
        t.faceY = -14;
        t.brow = 1.05f;
        t.mouthOpen = 1;
        s_lidTL = s_lidTR = 1;
        s_punchT = 1.12f;
      } else if (s_beat == BP_HOLD) {
        t.eyeW = 68;
        t.eyeH = 68;
        t.faceY = -10;
        t.mouthOpen = 0.95f;
        s_punchT = 1.04f;
      }
      break;
    case TAMAFI_MOOD_ANGRY:
      if (s_beat == BP_ANTIC) {
        s_lidTL = s_lidTR = 0.62f;
        t.tilt = 0.2f;
      } else if (s_beat == BP_HIT) {
        s_lidTL = s_lidTR = 1;
        s_punchT = 1.08f;
        s_shakeT = sinf(millis() * 0.08f) * 7.0f;
      } else if (s_beat == BP_HOLD) {
        s_shakeT = sinf(millis() * 0.07f) * 5.5f;
        s_punchT = 1.02f;
      } else if (s_beat == BP_SETTLE) {
        s_shakeT = sinf(millis() * 0.05f) * 1.2f * (1.0f - k);
      }
      break;
    case TAMAFI_MOOD_LOVE:
      if (s_beat == BP_ANTIC) {
        t.style = 0;
        t.blush = 0.35f + 0.4f * k;
        t.eyeW = 52;
        t.eyeH = 52;
      } else if (s_beat == BP_HIT) {
        t.style = 1;
        t.blush = 1;
        t.eyeW = 56;
        t.eyeH = 56;
        t.faceY = -8;
        s_punchT = 1.1f;
      } else if (s_beat == BP_HOLD) {
        t.style = 1;
        t.blush = 0.95f;
        s_punchT = 1.03f;
      }
      break;
    case TAMAFI_MOOD_HAPPY:
      if (s_beat == BP_ANTIC) {
        s_lidTL = s_lidTR = 0.55f;
      } else if (s_beat == BP_HIT) {
        t.eyeH = 18;
        t.faceY = -10;
        t.mouthOpen = 0.8f;
        s_punchT = 1.07f;
        s_lidTL = s_lidTR = 1;
      } else if (s_beat == BP_HOLD) {
        t.faceY = -6;
        t.eyeH = 22;
      }
      break;
    case TAMAFI_MOOD_SCARED:
      if (s_beat == BP_ANTIC) {
        t.eyeW = 50;
        t.eyeH = 50;
      } else if (s_beat == BP_HIT) {
        t.eyeW = 66;
        t.eyeH = 66;
        t.faceY = -8;
        t.mouthOpen = 0.85f;
        s_punchT = 1.09f;
        s_shakeT = sinf(millis() * 0.09f) * 3.0f;
      } else if (s_beat == BP_HOLD) {
        s_shakeT = sinf(millis() * 0.11f) * 4.0f;
        t.eyeW = 62;
        t.eyeH = 62;
      }
      break;
    case TAMAFI_MOOD_LAUGH:
      if (s_beat == BP_ANTIC) {
        s_lidTL = s_lidTR = 0.52f;
        t.mouthOpen = 0.4f;
      } else if (s_beat == BP_HIT) {
        t.eyeH = 18;
        t.faceY = -10;
        t.mouthOpen = 0.9f;
        s_punchT = 1.07f;
        s_lidTL = s_lidTR = 0.82f;
      } else if (s_beat == BP_HOLD) {
        t.eyeH = 20;
        t.faceY = -6;
        t.mouthOpen = 0.82f;
        s_lidTL = s_lidTR = 0.72f;
      }
      break;
    case TAMAFI_MOOD_WINK:
      s_punchT = 1;
      if (s_beat == BP_ANTIC) {
        s_lidTL = 1.0f;
        s_lidTR = 1.0f;
        t.tilt = 0.04f;
        t.mouthOpen = 0.22f;
      } else if (s_beat == BP_HIT) {
        s_lidTL = 1.0f - 0.86f * k;
        s_lidTR = 1.0f;
        t.tilt = 0.14f * k;
        t.mouthOpen = 0.38f;
      } else if (s_beat == BP_HOLD) {
        s_lidTL = 0.14f;
        s_lidTR = 1.0f;
        t.tilt = 0.14f;
        t.mouthOpen = 0.38f;
      } else {
        s_lidTL = 0.14f + 0.86f * k;
        s_lidTR = 1.0f;
        t.tilt = 0.14f * (1.0f - k);
      }
      break;
    case TAMAFI_MOOD_CRY:
      if (s_beat == BP_ANTIC) {
        t.mouthOpen = 0.15f;
        t.faceY = 2;
      } else if (s_beat == BP_HIT) {
        t.mouthOpen = 0.75f;
        t.faceY = 10;
        s_punchT = 1.04f;
      }
      break;
    case TAMAFI_MOOD_EXCITED:
      if (s_beat == BP_HIT) {
        t.faceY = -14;
        t.eyeH = 26;
        t.mouthOpen = 1;
        s_punchT = 1.1f;
      } else if (s_beat == BP_HOLD) {
        t.faceY = -8;
      }
      break;
    case TAMAFI_MOOD_CONFUSED:
      if (s_beat == BP_ANTIC) {
        s_gazeTX = -8;
        s_gazeTY = 2;
      } else if (s_beat == BP_HIT) {
        s_gazeTX = 12;
        s_gazeTY = -6;
        t.browTilt = 1.2f;
        s_punchT = 1.04f;
      }
      break;
    case TAMAFI_MOOD_SHY:
      if (s_beat == BP_ANTIC) {
        s_gazeTX = 0;
        s_gazeTY = 0;
        t.blush = 0.2f;
      } else if (s_beat == BP_HIT) {
        s_gazeTX = 9;
        s_gazeTY = 11;
        t.blush = 1;
        t.faceY = 10;
        s_lidTL = s_lidTR = 0.72f;
      }
      break;
    case TAMAFI_MOOD_SAD:
      if (s_beat == BP_ANTIC) {
        s_gazeTY = 4;
        t.faceY = 1;
      } else if (s_beat == BP_HIT) {
        t.faceY = 9;
        t.mouthOpen = 0.45f;
      }
      break;
    case TAMAFI_MOOD_LISTEN:
      s_gazeTX = 4;
      s_gazeTY = -2;
      if (s_beat == BP_ANTIC) {
        t.eyeW = 50;
        t.eyeH = 44;
        t.tilt = 0.08f;
        s_lidTL = s_lidTR = 0.62f;
      } else if (s_beat == BP_HIT) {
        t.eyeW = 68;
        t.eyeH = 36;
        t.tilt = 0.26f;
        t.brow = 0.95f;
        t.browTilt = 1.05f;
        t.faceY = -4;
        s_lidTL = 1;
        s_lidTR = 0.92f;
        s_punchT = 1.03f;
      } else if (s_beat == BP_HOLD) {
        t.eyeW = 64;
        t.eyeH = 40;
        t.tilt = 0.22f;
        s_punchT = 1.01f;
      }
      break;
    case TAMAFI_MOOD_DIZZY:
      if (s_beat == BP_ANTIC) {
        t.eyeW = 40;
        t.eyeH = 40;
        t.mouthOpen = 0.12f;
        s_lidTL = s_lidTR = 0.42f;
        s_gazeTX = 0;
        s_gazeTY = 0;
      } else if (s_beat == BP_HIT) {
        t.eyeW = (s_dizzyKind == 1) ? 64.0f : 58.0f;
        t.eyeH = t.eyeW;
        t.faceY = -10;
        t.mouthOpen = 0.7f;
        t.tilt = 0.4f;
        s_lidTL = s_lidTR = 1;
        s_punchT = 1.14f;
        s_shakeT = sinf(millis() * 0.11f) * 9.0f;
      } else if (s_beat == BP_HOLD) {
        t.faceY = -4 + sinf(millis() * 0.03f) * 3.0f;
        t.mouthOpen = 0.45f;
        s_punchT = 1.05f;
        s_shakeT = sinf(millis() * 0.1f) * 6.5f;
        s_lidTL = s_lidTR = 1;
      } else if (s_beat == BP_SETTLE) {
        s_shakeT = sinf(millis() * 0.08f) * 3.0f * (1.0f - k);
        t.tilt = 0.28f;
      }
      break;
    case TAMAFI_MOOD_AIR_ATTACK:
      if (s_beat == BP_HIT) {
        t.eyeW = 60;
        t.eyeH = 60;
        s_punchT = 1.06f;
        s_shakeT = 4;
      }
      break;
    case TAMAFI_MOOD_AIR_NOISE:
      if (s_beat == BP_HIT) {
        t.eyeW = 64;
        t.eyeH = 24;
        s_punchT = 1.04f;
      }
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      if (s_beat == BP_HIT) {
        s_gazeTX = 10;
        s_gazeTY = 6;
        s_lidTL = s_lidTR = 0.7f;
        t.faceY = 6;
      }
      break;
    default:
      break;
  }
}

static uint32_t airGlitchBeat(uint32_t now) { return now / 88; }

static bool airGlitchHit(uint32_t now) { return (airGlitchBeat(now) % 8) >= 6; }

static float airProbeSweep(uint32_t now) {
  const float t = (now % 1600) / 1600.0f;
  return (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
}

static bool airProbePing(uint32_t now) {
  const uint32_t p = now % 1600;
  return p < 130 || (p > 800 && p < 930);
}

static uint16_t moodEyeColor(float dim) {
  uint16_t c = kEye;
  if (s_mood == TAMAFI_MOOD_LOVE) c = mix565(kEye, kHeart, 0.45f);
  else if (s_mood == TAMAFI_MOOD_ANGRY) c = mix565(kEye, 0xFEEE, 0.2f);
  else if (s_mood == TAMAFI_MOOD_SLEEP) c = mix565(kEye, 0x7BEF, 0.35f);
  else if (s_mood == TAMAFI_MOOD_SCARED) c = kWhite;
  else if (s_mood == TAMAFI_MOOD_AIR_ATTACK)
    c = mix565(kWhite, kAlert, airGlitchHit(millis()) ? 0.55f : 0.16f);
  else if (s_mood == TAMAFI_MOOD_AIR_NOISE)
    c = mix565(kEye, kWarnY, airProbePing(millis()) ? 0.45f : 0.18f);
  return mix565(c, 0x0000, dim);
}

static void scheduleBlink(uint32_t now) {
  if (s_mood == TAMAFI_MOOD_SLEEP || s_mood == TAMAFI_MOOD_WINK ||
      s_mood == TAMAFI_MOOD_DIZZY || s_mood == TAMAFI_MOOD_LAUGH ||
      s_mood == TAMAFI_MOOD_AIR_ATTACK || s_mood == TAMAFI_MOOD_AIR_NOISE) {
    s_nextBlink = now + 60000;
    return;
  }
  uint32_t minWait = 2200, span = 2600;
  if (s_mood == TAMAFI_MOOD_SURPRISED || s_mood == TAMAFI_MOOD_SCARED) minWait = 3400;
  if (s_mood == TAMAFI_MOOD_BORED) {
    minWait = 3000;
    span = 3600;
  }
  s_nextBlink = now + minWait + (esp_random() % span);
}

static void scheduleSaccade(uint32_t now) {
  uint32_t wait = 1100 + (esp_random() % 2000);
  switch (s_mood) {
    case TAMAFI_MOOD_CONFUSED:
    case TAMAFI_MOOD_SCARED:
      wait = 320 + (esp_random() % 380);
      break;
    case TAMAFI_MOOD_LISTEN:
      wait = 2400 + (esp_random() % 1600);
      break;
    case TAMAFI_MOOD_THINK:
      wait = 1700 + (esp_random() % 1600);
      break;
    case TAMAFI_MOOD_BORED:
      wait = 2600 + (esp_random() % 2200);
      break;
    case TAMAFI_MOOD_SLEEP:
    case TAMAFI_MOOD_LOVE:
    case TAMAFI_MOOD_DIZZY:
    case TAMAFI_MOOD_AIR_ATTACK:
    case TAMAFI_MOOD_AIR_NOISE:
      wait = 9000;
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      wait = 1400 + (esp_random() % 1200);
      break;
    default:
      break;
  }
  s_nextSaccade = now + wait;
}

static void pickGaze(uint32_t now) {
  switch (s_mood) {
    case TAMAFI_MOOD_SLEEP:
    case TAMAFI_MOOD_DIZZY:
      s_gazeTX = 0;
      s_gazeTY = 0;
      break;
    case TAMAFI_MOOD_LISTEN:
      s_gazeTX = 4 + ((int)(esp_random() % 3) - 1);
      s_gazeTY = -2 + ((int)(esp_random() % 3) - 1);
      break;
    case TAMAFI_MOOD_THINK:
      s_gazeTX = 7 + (int)(esp_random() % 5);
      s_gazeTY = -9 + (int)(esp_random() % 4);
      break;
    case TAMAFI_MOOD_BORED:
      s_gazeTX = (esp_random() & 1) ? 10.0f : -10.0f;
      s_gazeTY = 3;
      break;
    case TAMAFI_MOOD_SHY:
      s_gazeTX = 5 + (int)(esp_random() % 5);
      s_gazeTY = 8;
      break;
    case TAMAFI_MOOD_CONFUSED:
      s_gazeTX = ((int)(esp_random() % 19) - 9);
      s_gazeTY = ((int)(esp_random() % 13) - 6);
      break;
    case TAMAFI_MOOD_SCARED:
      s_gazeTX = ((int)(esp_random() % 15) - 7);
      s_gazeTY = ((int)(esp_random() % 9) - 6);
      break;
    case TAMAFI_MOOD_AIR_ATTACK:
      s_gazeTX = 0;
      s_gazeTY = 0;
      break;
    case TAMAFI_MOOD_AIR_NOISE:
      s_gazeTX = 0;
      s_gazeTY = -2;
      break;
    case TAMAFI_MOOD_AIR_OPEN:
      s_gazeTX = 6 + (int)(esp_random() % 6);
      s_gazeTY = 5 + (int)(esp_random() % 4);
      break;
    default: {
      static const int8_t spots[][2] = {
          {0, 0}, {-10, 0}, {10, 1}, {0, -8}, {8, -6}, {-8, 5},
          {5, 6}, {-5, -5}, {0, 3},  {7, 0},  {-8, -3}, {0, 0}};
      const int i = esp_random() % 12;
      s_gazeTX = (float)spots[i][0];
      s_gazeTY = (float)spots[i][1];
      break;
    }
  }
  scheduleSaccade(now);
}

template <typename Display>
static void fillOval(Display& d, int cx, int cy, int rx, int ry, uint16_t color) {
  if (rx < 1 || ry < 1) return;
  if (abs(rx - ry) <= 1) {
    d.fillCircle(cx, cy, rx, color);
    return;
  }
  d.fillEllipse(cx, cy, rx, ry, color);
}

static int cornerR(int w, int h) {
  int r = kCorner;
  const int cap = min(w, h) / 3;
  if (r > cap) r = cap;
  if (r < 2) r = 2;
  return r;
}

template <typename Display>
static void drawGlow(Display& d, int x, int y, int w, int h, int r, uint16_t eye, float open) {
  if (open < 0.42f || w < 4 || h < 8) return;
  const float fade = clampf((open - 0.42f) / 0.58f, 0.0f, 1.0f);
  const int cover = (int)lroundf((1.0f - open) * h);
  const int gy = y + cover;
  const int gh = h - cover;
  if (gh < 10) return;
  const uint16_t g3 = mix565(eye, 0x0000, 1.0f - 0.12f * fade);
  const uint16_t g2 = mix565(eye, 0x0000, 1.0f - 0.22f * fade);
  const uint16_t g1 = mix565(eye, 0x0000, 1.0f - 0.38f * fade);
  d.fillRoundRect(x - 7, gy - 3, w + 14, gh + 10, r + 4, g3);
  d.fillRoundRect(x - 4, gy - 1, w + 8, gh + 5, r + 2, g2);
  d.fillRoundRect(x - 2, gy, w + 4, gh + 2, r + 1, g1);
}

template <typename Display>
static void drawSquareEye(Display& d, int cx, int cy, int w, int h, uint16_t eye, float lookX,
                          float lookY) {
  if (w < 2 || h < 2) return;
  const int x = cx - w / 2;
  const int y = cy - h / 2;
  const int r = cornerR(w, h);
  if (h <= 6) {
    d.fillRoundRect(x, cy - 2, w, 4, 2, mix565(eye, 0x0000, 0.15f));
    return;
  }

  d.fillRoundRect(x, y, w, h, r, eye);

  if (h > 18) {
    const int hx = cx - w / 5 + (int)(lookX * 0.25f);
    const int hy = cy - h / 5 + (int)(lookY * 0.25f);
    const int hs = max(3, min(w, h) / 9);
    d.fillCircle(hx, hy, hs, kSheen);
  }
}

template <typename Display>
static void drawLid(Display& d, int cx, int cy, int w, int h, float open) {
  if (h <= 6) return;
  const float closed = 1.0f - clampf(open, 0.0f, 1.0f);
  int cover = (int)lroundf(closed * (h - 3));
  if (cover < 1) return;
  const int x = cx - w / 2;
  const int top = cy - h / 2;
  const int r = cornerR(w, h);
  d.fillRect(x - 10, top - 12, w + 20, cover + 12, TFT_BLACK);
  d.fillRoundRect(x - 1, top - r - 1, w + 2, cover + r + 1, r, TFT_BLACK);
  if (cover < h - 4) {
    d.fillRoundRect(x + 3, top + cover - 1, w - 6, 2, 1, kLidLine);
  }
}

template <typename Display>
static void drawHeart(Display& d, int cx, int cy, int s, uint16_t color, float lookX, float lookY) {
  const int r = max(6, s / 3);
  d.fillCircle(cx - r + 1, cy - r / 3, r + 2, mix565(color, 0x0000, 0.7f));
  d.fillCircle(cx + r - 1, cy - r / 3, r + 2, mix565(color, 0x0000, 0.7f));
  d.fillCircle(cx - r + 1, cy - r / 3, r, color);
  d.fillCircle(cx + r - 1, cy - r / 3, r, color);
  d.fillTriangle(cx - s / 2, cy + 1, cx + s / 2, cy + 1, cx, cy + s / 2 + 1, color);
  const int hx = cx - r / 2 + (int)(lookX * 0.2f);
  const int hy = cy - r / 2 + (int)(lookY * 0.2f);
  d.fillCircle(hx, hy, max(2, r / 4), kSheen);
}

template <typename Display>
static void drawOrbitSheen(Display& d, int cx, int cy, int w, int h, float a, uint16_t eye) {
  const int hs = max(4, min(w, h) / 7);
  const float radx = w * 0.20f;
  const float rady = h * 0.20f;
  const int gx = cx + (int)(cosf(a - 0.65f) * radx);
  const int gy = cy + (int)(sinf(a - 0.65f) * rady);
  d.fillCircle(gx, gy, max(2, hs - 1), mix565(kSheen, eye, 0.58f));
  d.fillCircle(cx + (int)(cosf(a) * radx), cy + (int)(sinf(a) * rady), hs, kSheen);
}

template <typename Display>
static void drawBrow(Display& d, int cx, int top, int w, float amount, float tilt, int side) {
  if (amount < 0.08f) return;
  const int bw = max(14, (int)(w * 0.68f));
  const int bh = max(3, (int)(3.4f * amount));
  const int by = top - 12 - bh + (int)(tilt * 7.0f * side);
  const int bx = cx - bw / 2 + (int)(tilt * 4.0f * side);
  d.fillRoundRect(bx, by, bw, bh, bh / 2, mix565(kBrow, 0x0000, 0.12f));
}

template <typename Display>
static void drawMouth(Display& d, const EmoPose& p, int cx, int below, uint16_t color) {
  if (p.mouth < 0.12f) return;
  const int y = below + 18;
  if (p.mouth < 1.5f) {
    const int rw = (int)(16 + 8 * p.mouthOpen);
    const int rh = (int)(5 + 3 * p.mouthOpen);
    fillOval(d, cx, y + 3, rw, rh, color);
    fillOval(d, cx, y - 2, rw + 2, rh + 1, TFT_BLACK);
    return;
  }
  if (p.mouth < 2.5f) {
    const int rw = 8 + (int)(9 * p.mouthOpen);
    const int rh = 3 + (int)(8 * p.mouthOpen);
    fillOval(d, cx, y, rw, rh, color);
    if (rh > 6) fillOval(d, cx, y + 1, max(2, rw - 5), max(2, rh - 5), TFT_BLACK);
    return;
  }
  if (p.mouth < 3.5f) {
    fillOval(d, cx, y - 3, 13, 5, color);
    fillOval(d, cx, y + 2, 15, 6, TFT_BLACK);
    return;
  }
  if (p.mouth < 4.5f) {
    const int r = 5 + (int)(7 * p.mouthOpen);
    d.fillCircle(cx, y, r, color);
    d.fillCircle(cx, y + 1, max(2, r - 4), TFT_BLACK);
    return;
  }
  d.fillRoundRect(cx - 14, y - 1, 28, 4, 2, mix565(color, 0x0000, 0.08f));
}

template <typename Display>
static void drawTear(Display& d, int cx, int cy, float t, float phase) {
  const float drop = 0.5f + 0.5f * sinf(t * 2.1f + phase);
  const int y = cy + 4 + (int)(drop * 12.0f);
  const uint16_t c = mix565(kTear, 0x0000, 0.15f);
  d.fillCircle(cx, y, 3, c);
  d.fillTriangle(cx - 3, y, cx + 3, y, cx, y - 7, c);
  d.fillCircle(cx - 1, y - 1, 1, kSheen);
}

template <typename Display>
static void drawBlush(Display& d, int cx, int cy, float amt) {
  if (amt < 0.15f) return;
  for (int i = 3; i >= 0; --i) {
    const float k = 0.55f + i * 0.12f;
    fillOval(d, cx, cy, 14 + i * 2, 7 + i, mix565(kBlush, 0x0000, k - amt * 0.2f));
  }
}

template <typename Display>
static void drawSweat(Display& d, int x, int y, float t) {
  const int oy = (int)(sinf(t * 3.0f) * 2.0f);
  d.fillCircle(x, y + oy, 4, mix565(kTear, 0x0000, 0.1f));
  d.fillTriangle(x - 4, y + oy, x + 4, y + oy, x, y - 8 + oy, mix565(kTear, 0x0000, 0.1f));
  d.fillCircle(x - 1, y - 1 + oy, 1, kSheen);
}

template <typename Display>
static void drawSpark(Display& d, int x, int y, int s, float a) {
  const uint16_t c = mix565(kSpark, 0x0000, 0.15f + 0.25f * (0.5f - 0.5f * a));
  d.drawFastHLine(x - s, y, s * 2 + 1, c);
  d.drawFastVLine(x, y - s, s * 2 + 1, c);
  if (s > 4) {
    d.drawLine(x - s / 2, y - s / 2, x + s / 2, y + s / 2, c);
    d.drawLine(x - s / 2, y + s / 2, x + s / 2, y - s / 2, c);
  }
}

template <typename Display>
static void drawListenArcs(Display& d, int x, int y, float t, uint16_t c) {
  if (x > TAMAFI_FACE_SIZE - 28) x = TAMAFI_FACE_SIZE - 28;
  if (x < 18) x = 18;
  if (y < 18) y = 18;
  if (y > TAMAFI_FACE_SIZE - 18) y = TAMAFI_FACE_SIZE - 18;
  const float pulse = 0.5f + 0.5f * sinf(t * 3.6f);
  for (int i = 0; i < 2; ++i) {
    const float vis = pulse - i * 0.28f;
    if (vis < 0.18f) continue;
    const uint16_t col = mix565(c, 0x0000, 0.22f + i * 0.14f);
    const int r = 7 + i * 6;
    for (int a = -40; a <= 40; a += 10) {
      const float rad = a * 0.017453f;
      const int px = x + (int)(cosf(rad) * r);
      const int py = y + (int)(sinf(rad) * r);
      if (px < 2 || px > TAMAFI_FACE_SIZE - 3 || py < 2 || py > TAMAFI_FACE_SIZE - 3) continue;
      d.fillCircle(px, py, 1, col);
    }
  }
}

template <typename Display>
static void drawQuestion(Display& d, int x, int y, uint16_t c) {
  d.fillCircle(x, y - 7, 5, c);
  d.fillCircle(x, y - 7, 2, TFT_BLACK);
  d.fillRoundRect(x + 2, y - 6, 3, 7, 1, c);
  d.fillCircle(x, y + 6, 2, c);
}

template <typename Display>
static void drawWifiMark(Display& d, int x, int y, uint16_t c, bool broken, float t) {
  if (x < 20) x = 20;
  if (x > TAMAFI_FACE_SIZE - 20) x = TAMAFI_FACE_SIZE - 20;
  if (y < 18) y = 18;
  const float flicker = broken ? (0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 22.0f))) : 1.0f;
  const uint16_t col = mix565(c, 0x0000, 1.0f - flicker);
  d.fillCircle(x, y + 16, 2, col);
  for (int i = 0; i < 3; ++i) {
    if (broken && i == 1 && ((millis() / 90) & 1)) continue;
    const int r = 6 + i * 6;
    for (int a = 200; a <= 340; a += 10) {
      const float rad = a * 0.017453f;
      const int px = x + (int)(cosf(rad) * r);
      const int py = y + 16 + (int)(sinf(rad) * r);
      if (px < 2 || px > TAMAFI_FACE_SIZE - 3 || py < 2 || py > TAMAFI_FACE_SIZE - 3) continue;
      d.fillCircle(px, py, 1, col);
    }
  }
  if (broken) {
    d.drawLine(x - 15, y + 22, x + 16, y - 2, kAlert);
    d.drawLine(x - 14, y + 22, x + 17, y - 2, kAlert);
  }
}

template <typename Display>
static void drawRadarSweep(Display& d, int x, int y, uint32_t now, uint16_t c) {
  if (x < 28) x = 28;
  if (x > TAMAFI_FACE_SIZE - 28) x = TAMAFI_FACE_SIZE - 28;
  if (y < 16) y = 16;
  const int cy = y + 18;
  d.fillCircle(x, cy, 2, c);
  for (int i = 0; i < 3; ++i) {
    const int r = 8 + i * 8;
    const uint16_t col = mix565(c, 0x0000, 0.28f + i * 0.16f);
    for (int a = 200; a <= 340; a += 10) {
      const float rad = a * 0.017453f;
      const int px = x + (int)(cosf(rad) * r);
      const int py = cy + (int)(sinf(rad) * r);
      if (px < 2 || px > TAMAFI_FACE_SIZE - 3 || py < 2 || py > TAMAFI_FACE_SIZE - 3) continue;
      d.fillCircle(px, py, 1, col);
    }
  }

  const float u = airProbeSweep(now);
  const float ang = (200.0f + u * 140.0f) * 0.017453f;
  const int rr = 24;
  const int tx = x + (int)(cosf(ang) * rr);
  const int ty = cy + (int)(sinf(ang) * rr);
  d.drawLine(x, cy, tx, ty, mix565(kWarnY, c, 0.25f));
  d.fillCircle(tx, ty, 2, kWarnY);

  if (airProbePing(now)) {
    const uint32_t p = now % 1600;
    const float k = (p < 130) ? (p / 130.0f) : ((p - 800) / 130.0f);
    const int pr = 10 + (int)(k * 16.0f);
    const uint16_t ping = mix565(kWarnY, 0x0000, k * 0.7f);
    for (int a = 200; a <= 340; a += 8) {
      const float rad = a * 0.017453f;
      const int px = x + (int)(cosf(rad) * pr);
      const int py = cy + (int)(sinf(rad) * pr);
      if (px < 2 || px > TAMAFI_FACE_SIZE - 3 || py < 2 || py > TAMAFI_FACE_SIZE - 3) continue;
      d.fillCircle(px, py, 1, ping);
    }
  }
}

template <typename Display>
static void drawOpenLock(Display& d, int x, int y, uint16_t c) {
  if (x < 16) x = 16;
  if (x > TAMAFI_FACE_SIZE - 16) x = TAMAFI_FACE_SIZE - 16;
  if (y < 22) y = 22;
  d.fillRoundRect(x - 8, y, 16, 13, 2, c);
  d.fillRoundRect(x - 4, y + 4, 8, 6, 1, TFT_BLACK);
  d.drawLine(x + 5, y, x + 5, y - 9, c);
  d.drawLine(x + 6, y, x + 6, y - 9, c);
  d.drawLine(x + 5, y - 9, x - 3, y - 12, c);
  d.drawLine(x + 6, y - 9, x - 2, y - 12, c);
  d.drawLine(x - 3, y - 12, x - 8, y - 7, c);
}

#if TAMAFI_FACE_USE_FULL_SPRITE && TAMAFI_FACE_USE_STATIC_SPRITE
static void shiftSpriteRows(int y0, int h, int dx) {
  const int W = TAMAFI_FACE_SIZE;
  if (h <= 0 || dx == 0) return;
  if (y0 < 0) {
    h += y0;
    y0 = 0;
  }
  if (y0 + h > W) h = W - y0;
  if (h <= 0) return;
  if (dx > W - 1) dx = W - 1;
  if (dx < -(W - 1)) dx = -(W - 1);
  for (int y = 0; y < h; ++y) {
    uint16_t* row = s_spriteBuffer + (y0 + y) * W;
    if (dx > 0) {
      memmove(row + dx, row, (size_t)(W - dx) * sizeof(uint16_t));
      for (int x = 0; x < dx; ++x) row[x] = 0;
    } else {
      const int n = -dx;
      memmove(row, row + n, (size_t)(W - n) * sizeof(uint16_t));
      for (int x = W - n; x < W; ++x) row[x] = 0;
    }
  }
}

static void applyDeauthGlitch(uint32_t now) {
  const int W = TAMAFI_FACE_SIZE;
  const uint16_t hair = (uint16_t)((0x39C7 >> 8) | (0x39C7 << 8));
  static const int kHairY[2] = {86, 154};
  for (int i = 0; i < 2; ++i) {
    uint16_t* row = s_spriteBuffer + kHairY[i] * W;
    for (int x = 24; x < W - 24; ++x) {
      if (row[x]) row[x] = hair;
    }
  }

  if (!airGlitchHit(now)) return;

  const uint32_t beat = airGlitchBeat(now);
  static const int8_t kShift[8] = {10, -14, 8, -11, 12, -8, 6, -13};
  static const int kBandY[3] = {72, 108, 148};
  static const int kBandH[3] = {10, 12, 8};
  const uint16_t red = (uint16_t)((kAlert >> 8) | (kAlert << 8));
  for (int i = 0; i < 3; ++i) {
    const int dx = kShift[(beat + (uint32_t)i * 3) & 7];
    shiftSpriteRows(kBandY[i], kBandH[i], dx);
    uint16_t* edge = s_spriteBuffer + kBandY[i] * W;
    for (int x = 16; x < W - 16; ++x) edge[x] = red;
  }
}
#endif

template <typename Display>
static void drawLetterZ(Display& d, int x, int y, int s, uint16_t c) {
  if (s < 5) s = 5;
  const int t = max(1, s / 5);
  d.fillRect(x, y, s, t, c);
  d.fillRect(x, y + s - t, s, t, c);
  d.drawLine(x + s - 1, y + t, x, y + s - t, c);
  d.drawLine(x + s - 2, y + t, x + 1, y + s - t, c);
}

template <typename Display>
static void drawZzz(Display& d, int x, int y, float t) {
  for (int i = 0; i < 3; ++i) {
    const float u = fmodf(t * 0.32f + i * 0.33f, 1.0f);
    const int s = 12 - (int)(u * 6.0f);
    const int px = x + (int)(u * 24.0f) - s / 2;
    const int py = y - (int)(u * 30.0f) - s / 2;
    if (px < 1 || py < 1 || px + s > TAMAFI_FACE_SIZE - 1 || py + s > TAMAFI_FACE_SIZE - 1) continue;
    drawLetterZ(d, px, py, s, mix565(kEye, 0x0000, u * 0.85f));
  }
}

static FaceRect faceBounds(const EmoPose& p, float scale) {
  const float lookX = clampf(p.gazeX, -22.0f, 22.0f);
  const float lookY = clampf(p.gazeY, -16.0f, 16.0f);
  const int cx = TAMAFI_FACE_SIZE / 2;
  const int cy = TAMAFI_FACE_SIZE / 2 - 4 + (int)lroundf(p.faceY);
  float w = p.eyeW * scale;
  float h = p.eyeH * scale;
  if (h < 4) h = 4;
  const float gap = (p.gap < kMinGap ? kMinGap : p.gap) * scale;
  const float split = (w + gap) * 0.5f;
  int lx = cx - (int)lroundf(split - lookX) + (int)lroundf(s_shakeX);
  int rx = cx + (int)lroundf(split + lookX) + (int)lroundf(s_shakeX);
  const int ey = cy + (int)lroundf(lookY);
  const int ly = ey + (int)lroundf(-p.tilt * 5.0f);
  const int ry = ey + (int)lroundf(p.tilt * 5.0f);
  const int iw = (int)w;
  const int ih = (int)h;
  const int margin = 42;

  int x0 = min(lx, rx) - iw / 2 - margin;
  int x1 = max(lx, rx) + iw / 2 + margin;
  int y0 = min(ly, ry) - ih / 2 - margin;
  int y1 = max(ly, ry) + ih / 2 + 62;

  if (s_mood == TAMAFI_MOOD_LOVE || s_mood == TAMAFI_MOOD_SLEEP) y0 = min(y0, 0);
  if (s_mood == TAMAFI_MOOD_LISTEN || s_mood == TAMAFI_MOOD_CONFUSED) x1 = TAMAFI_FACE_SIZE;
  if (s_mood == TAMAFI_MOOD_SCARED || s_mood == TAMAFI_MOOD_SHY) x1 = TAMAFI_FACE_SIZE;
  if (s_mood == TAMAFI_MOOD_AIR_ATTACK || s_mood == TAMAFI_MOOD_AIR_NOISE ||
      s_mood == TAMAFI_MOOD_AIR_OPEN) {
    x0 = 0;
    x1 = TAMAFI_FACE_SIZE;
    y0 = 0;
  }
  if (s_mood == TAMAFI_MOOD_CRY || s_mood == TAMAFI_MOOD_SAD) y1 += 18;
  if (s_mood == TAMAFI_MOOD_EXCITED || s_mood == TAMAFI_MOOD_LOVE) {
    x0 -= 20;
    x1 += 20;
    y0 -= 20;
  }

  x0 = max(0, x0);
  y0 = max(0, y0);
  x1 = min(TAMAFI_FACE_SIZE, x1);
  y1 = min(TAMAFI_FACE_SIZE, y1);
  return {x0, y0, max(1, x1 - x0), max(1, y1 - y0), true};
}

static void clearRect(const FaceRect& r) {
  if (!r.valid || !s_tft) return;
  s_tft->fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
}

static FaceRect expandRect(FaceRect r, int pad) {
  if (!r.valid) return r;
  const int x0 = max(0, r.x - pad);
  const int y0 = max(0, r.y - pad);
  const int x1 = min(TAMAFI_FACE_SIZE, r.x + r.w + pad);
  const int y1 = min(TAMAFI_FACE_SIZE, r.y + r.h + pad);
  return {x0, y0, max(1, x1 - x0), max(1, y1 - y0), true};
}

static FaceRect unionRect(const FaceRect& a, const FaceRect& b) {
  if (!a.valid) return b;
  if (!b.valid) return a;
  const int x0 = min(a.x, b.x);
  const int y0 = min(a.y, b.y);
  const int x1 = max(a.x + a.w, b.x + b.w);
  const int y1 = max(a.y + a.h, b.y + b.h);
  return {x0, y0, max(1, x1 - x0), max(1, y1 - y0), true};
}

template <typename Display>
static void drawFace(Display& d, const EmoPose& p, float lidL, float lidR, float intro, float scale,
                     bool clearScreen) {
  if (clearScreen) d.fillScreen(TFT_BLACK);

  const float t = millis() / 1000.0f;
  const uint16_t eye = moodEyeColor(p.dim);
  const int cx = TAMAFI_FACE_SIZE / 2;
  const int cy = TAMAFI_FACE_SIZE / 2 - 4 + (int)lroundf(p.faceY);

  float w = p.eyeW * scale;
  float h = p.eyeH * scale;
  if (h < 4) h = 4;

  const float gap = (p.gap < kMinGap ? kMinGap : p.gap) * scale;
  const float split = (w + gap) * 0.5f;
  const float lookX = clampf(p.gazeX, -22.0f, 22.0f);
  const float lookY = clampf(p.gazeY, -16.0f, 16.0f);

  int lx = cx - (int)lroundf(split - lookX) + (int)lroundf(s_shakeX);
  int rx = cx + (int)lroundf(split + lookX) + (int)lroundf(s_shakeX);
  const int ey = cy + (int)lroundf(lookY);
  const int ly = ey + (int)lroundf(-p.tilt * 5.0f);
  const int ry = ey + (int)lroundf(p.tilt * 5.0f);

  const int minDist = (int)lroundf(w + kMinGap * scale);
  if ((rx - lx) < minDist) {
    const int mid = (lx + rx) / 2;
    lx = mid - minDist / 2;
    rx = mid + minDist / 2;
  }

  const int iw = (int)w, ih = (int)h;
  const int half = iw / 2 + 8;
  const int pad = 10;
  if (lx - half < pad) {
    const int d = pad - (lx - half);
    lx += d;
    rx += d;
  }
  if (rx + half > TAMAFI_FACE_SIZE - pad) {
    const int d = (rx + half) - (TAMAFI_FACE_SIZE - pad);
    lx -= d;
    rx -= d;
  }
  const int r = cornerR(iw, ih);
  if (!s_plainEyes) {
    if (!(s_mood == TAMAFI_MOOD_WINK && lidL < 0.55f))
      drawGlow(d, lx - iw / 2, ly - ih / 2, iw, ih, r, eye, lidL);
    drawGlow(d, rx - iw / 2, ry - ih / 2, iw, ih, r, eye, lidR);
    drawBlush(d, lx, ly + ih / 2 + 6, p.blush);
    drawBlush(d, rx, ry + ih / 2 + 6, p.blush);
  }

  if (s_mood == TAMAFI_MOOD_DIZZY) {
    if (s_dizzyKind == 2) {
      lx += 4;
      rx -= 4;
    }
    d.fillRoundRect(lx - iw / 2, ly - ih / 2, iw, ih, r, eye);
    d.fillRoundRect(rx - iw / 2, ry - ih / 2, iw, ih, r, eye);
    if (s_dizzyKind == 1) {
      const float a = t * 3.4f;
      drawOrbitSheen(d, lx, ly, iw, ih, a, eye);
      drawOrbitSheen(d, lx, ly, iw, ih, a + 3.1416f, eye);
      drawOrbitSheen(d, rx, ry, iw, ih, -a, eye);
      drawOrbitSheen(d, rx, ry, iw, ih, -a + 3.1416f, eye);
    } else if (s_dizzyKind == 2) {
      drawOrbitSheen(d, lx + 5, ly, iw, ih, t * 3.8f, eye);
      drawOrbitSheen(d, rx - 5, ry, iw, ih, -t * 3.8f + 1.2f, eye);
    } else if (s_dizzyKind == 3) {
      drawOrbitSheen(d, lx, ly, iw, ih, t * 4.2f, eye);
      drawOrbitSheen(d, rx, ry, iw, ih, -1.35f, eye);
    } else {
      drawOrbitSheen(d, lx, ly, iw, ih, t * 4.6f, eye);
      drawOrbitSheen(d, rx, ry, iw, ih, -t * 4.6f, eye);
    }
    drawLid(d, lx, ly, iw, ih, lidL);
    drawLid(d, rx, ry, iw, ih, lidR);
  } else if (s_mood == TAMAFI_MOOD_LOVE && p.style > 0.5f) {
    drawHeart(d, lx, ly, iw, mix565(kHeart, kHeartDeep, 0.25f), lookX, lookY);
    drawHeart(d, rx, ry, iw, mix565(kHeart, kHeartDeep, 0.25f), lookX, lookY);
  } else {
    drawSquareEye(d, lx, ly, iw, ih, eye, lookX, lookY);
    drawSquareEye(d, rx, ry, iw, ih, eye, lookX, lookY);
    drawLid(d, lx, ly, iw, ih, lidL);
    drawLid(d, rx, ry, iw, ih, lidR);
  }

  if (!s_plainEyes) {
    const int top = ey - ih / 2;
    drawBrow(d, lx, top, iw, p.brow, p.browTilt, -1);
    drawBrow(d, rx, top, iw, p.brow, p.browTilt, 1);
  }

  const int below = ey + ih / 2;
  if (!s_plainEyes) drawMouth(d, p, cx, below, eye);

  if (s_plainEyes) return;

  if (s_mood == TAMAFI_MOOD_SAD || s_mood == TAMAFI_MOOD_CRY) {
    drawTear(d, lx, below + 1, t, 0);
    if (s_mood == TAMAFI_MOOD_CRY) drawTear(d, rx, below + 1, t, 1.15f);
  }
  if (s_mood == TAMAFI_MOOD_SHY || s_mood == TAMAFI_MOOD_SCARED) {
    drawSweat(d, rx + iw / 2 + 8, ry - ih / 2 - 2, t);
  }
  if (s_mood == TAMAFI_MOOD_LISTEN) {
    drawListenArcs(d, rx + iw / 2 + 8, ry, t, eye);
  }
  if (s_mood == TAMAFI_MOOD_CONFUSED) {
    drawQuestion(d, rx + iw / 2 + 14, ry - ih / 2 - 10, mix565(eye, 0x0000, 0.08f));
  }
  if (s_mood == TAMAFI_MOOD_SLEEP) {
    drawZzz(d, rx + 16, ry - 16, t);
  }
  if (s_mood == TAMAFI_MOOD_AIR_NOISE) {
    drawRadarSweep(d, cx, 10, millis(), mix565(kWarnY, eye, 0.2f));
  }
  if (s_mood == TAMAFI_MOOD_AIR_OPEN) {
    drawWifiMark(d, cx, 16, mix565(eye, kWarnY, 0.35f), false, t);
    drawOpenLock(d, rx + iw / 2 + 16, ry - 8, mix565(kWarnY, eye, 0.2f));
  }
  if (s_mood == TAMAFI_MOOD_EXCITED || s_mood == TAMAFI_MOOD_LOVE) {
    const float pulse = 0.5f + 0.5f * sinf(t * 5.5f);
    drawSpark(d, lx - 20, ly - 16, 5 + (int)(2 * pulse), pulse);
    drawSpark(d, rx + 20, ry - 20, 4 + (int)(2 * (1.0f - pulse)), 1.0f - pulse);
    if (s_mood == TAMAFI_MOOD_LOVE) {
      const float u = 0.5f + 0.5f * sinf(t * 2.2f);
      drawHeart(d, cx, 34 - (int)(u * 6.0f), 14, mix565(kHeart, 0x0000, 0.2f + 0.3f * (1.0f - u)), 0, 0);
    }
  }
  drawSpeechBubble(d, cx);
}

static float cellWave(int col, int row) {
  const float cl = 6.2f;
  const float cr = 18.8f;
  const float cy = 3.6f;
  const float dlx = (float)col - cl, dly = (float)row - cy;
  const float drx = (float)col - cr, dry = (float)row - cy;
  const float dl = sqrtf(dlx * dlx + dly * dly);
  const float dr = sqrtf(drx * drx + dry * dry);
  return clampf(fminf(dl, dr) / 7.4f, 0.0f, 1.0f);
}

static void asciiLayout(int& ox, int& oy, int& cw, int& ch) {
  cw = glcdCharW(GLCD_S);
  ch = glcdCharH(GLCD_S);
  ox = (TAMAFI_FACE_SIZE - kAsciiCols * cw) / 2;
  oy = (TAMAFI_FACE_SIZE / 2 - 4) - (kAsciiRows * ch) / 2;
}

static bool inRoundRect(int px, int py, int x, int y, int w, int h, int r) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  const int lx = px - x;
  const int ly = py - y;
  int cx = 0, cy = 0;
  bool corner = false;
  if (lx < r && ly < r) {
    cx = r;
    cy = r;
    corner = true;
  } else if (lx >= w - r && ly < r) {
    cx = w - r;
    cy = r;
    corner = true;
  } else if (lx < r && ly >= h - r) {
    cx = r;
    cy = h - r;
    corner = true;
  } else if (lx >= w - r && ly >= h - r) {
    cx = w - r;
    cy = h - r;
    corner = true;
  }
  if (!corner) return true;
  const int dx = lx - cx, dy = ly - cy;
  return dx * dx + dy * dy <= r * r;
}

static bool inCircle(int px, int py, int cx, int cy, int rad) {
  const int dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= rad * rad;
}

static char asciiCell(int col, int row) {
  int ox, oy, cw, ch;
  asciiLayout(ox, oy, cw, ch);
  int hits = 0, glow = 0, sheen = 0;
  const int x0 = ox + col * cw;
  const int y0 = oy + row * ch;
  const int cx = TAMAFI_FACE_SIZE / 2;
  const int cy = TAMAFI_FACE_SIZE / 2 - 4;
  const int w = 54, h = 54, gap = 30, rr = 10;
  const int split = (w + gap) / 2;
  const int lx = cx - split;
  const int rx = cx + split;
  const int hs = 6;
  for (int sy = 1; sy <= 7; sy += 3) {
    for (int sx = 1; sx <= 5; sx += 2) {
      const int px = x0 + sx;
      const int py = y0 + sy;
      const bool left = inRoundRect(px, py, lx - w / 2, cy - h / 2, w, h, rr);
      const bool right = inRoundRect(px, py, rx - w / 2, cy - h / 2, w, h, rr);
      if (left || right) hits++;
      if (inRoundRect(px, py, lx - w / 2 - 6, cy - h / 2 - 3, w + 12, h + 8, rr + 4) ||
          inRoundRect(px, py, rx - w / 2 - 6, cy - h / 2 - 3, w + 12, h + 8, rr + 4))
        glow++;
      if (inCircle(px, py, lx - w / 5, cy - h / 5, hs) || inCircle(px, py, rx - w / 5, cy - h / 5, hs))
        sheen++;
    }
  }
  if (sheen >= 2 && hits) return '*';
  if (hits >= 6) return '#';
  if (hits >= 3) return '%';
  if (glow >= 4) return ':';
  if (glow >= 1) return '.';
  return ' ';
}

static uint16_t asciiColor(char c) {
  if (c == '*') return kSheen;
  if (c == '#') return kEye;
  if (c == '%') return mix565(kEye, 0x0000, 0.18f);
  if (c == ':') return mix565(kEye, 0x0000, 0.55f);
  if (c == '.') return mix565(kEye, 0x0000, 0.78f);
  return kWhite;
}

template <typename Display>
static void drawAsciiBoot(Display& d, uint32_t now) {
  int ox, oy, cw, ch;
  asciiLayout(ox, oy, cw, ch);
  const float t = (float)(now - s_wakeAt);
  const char* hexSet = "0123456789ABCDEF";
  char buf[2];
  buf[1] = 0;
  int beamY = -1;
  float beamBest = 99.0f;

  for (int r = 0; r < kAsciiRows; r++) {
    const int y = oy + r * ch;
    for (int c = 0; c < kAsciiCols; c++) {
      const char glyph = asciiCell(c, r);
      if (glyph == ' ') continue;
      const float wave = cellWave(c, r);
      char g = 0;
      uint16_t col = asciiColor(glyph);
      bool flash = false;

      if (s_wake == WAKE_SCAN) {
        const float u = t / (float)kAsciiScanMs - wave * 0.78f;
        if (u < 0.0f) continue;
        if (u < 0.16f) {
          g = hexSet[(now / 22 + r * 17 + c * 13) & 15];
          col = mix565(kWhite, 0x0000, 0.28f + u * 2.0f);
        } else {
          g = hexSet[(uint8_t)glyph & 15];
          col = mix565(kWhite, asciiColor(glyph), 0.35f);
        }
        const float dist = fabsf(u - 0.08f);
        if (dist < beamBest) {
          beamBest = dist;
          beamY = y + ch - 1;
        }
      } else if (s_wake == WAKE_DECODE) {
        const float u = t / (float)kAsciiDecodeMs - wave * 0.82f;
        if (u < 0.0f) {
          g = hexSet[(uint8_t)glyph & 15];
          col = mix565(kWhite, asciiColor(glyph), 0.35f);
        } else if (u < 0.14f) {
          g = (u < 0.07f) ? hexSet[(now / 18 + c) & 15] : glyph;
          col = kSheen;
          flash = true;
        } else {
          g = glyph;
          col = asciiColor(glyph);
        }
      } else {
        const float u = t / (float)kAsciiMorphMs - wave * 0.88f;
        if (u < 0.0f) {
          g = glyph;
          col = asciiColor(glyph);
        } else if (u < 0.18f) {
          g = (u < 0.08f) ? '*' : glyph;
          col = mix565(kSheen, asciiColor(glyph), clampf(u / 0.18f, 0.0f, 1.0f));
          flash = true;
        } else {
          continue;
        }
      }

      buf[0] = g;
      glcdDrawText(d, ox + c * cw, y, buf, col, TFT_BLACK, GLCD_S);
      if (flash) {
        d.drawRect(ox + c * cw - 1, y - 1, cw + 1, ch + 1, mix565(kSheen, 0x0000, 0.55f));
      }
    }
  }

  if (s_wake == WAKE_SCAN && beamY >= 0 && beamY < TAMAFI_FACE_SIZE) {
    d.drawFastHLine(ox, beamY, kAsciiCols * cw, mix565(kEye, 0x0000, 0.55f));
  }
}

static void present(const EmoPose& p, float lidL, float lidR, float intro, float scale, bool push) {
  const uint32_t now = millis();
  const bool asciiOnly = (s_wake == WAKE_SCAN || s_wake == WAKE_DECODE);

  if (s_use_sprite && s_spr) {
    if (asciiOnly) {
      s_spr->fillScreen(TFT_BLACK);
      drawAsciiBoot(*s_spr, now);
    } else {
      drawFace(*s_spr, p, lidL, lidR, intro, scale, true);
      if (s_wake == WAKE_MORPH) drawAsciiBoot(*s_spr, now);
    }
#if TAMAFI_FACE_USE_FULL_SPRITE && TAMAFI_FACE_USE_STATIC_SPRITE
    if (s_static_sprite && s_mood == TAMAFI_MOOD_AIR_ATTACK && s_wake >= WAKE_DONE)
      applyDeauthGlitch(millis());
#endif
    if (!push) return;
#if TAMAFI_FACE_USE_FULL_SPRITE && TAMAFI_FACE_USE_STATIC_SPRITE
    if (s_static_sprite && s_tft) {
      bool oldSwapBytes = s_tft->getSwapBytes();
      s_tft->setSwapBytes(false);
      s_tft->pushImage(0, 0, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE, s_spriteBuffer);
      s_tft->setSwapBytes(oldSwapBytes);
    } else
#endif
    {
      s_spr->pushSprite(0, 0);
    }
  } else {
    s_tft->startWrite();
    if (asciiOnly) {
      s_tft->fillScreen(TFT_BLACK);
      drawAsciiBoot(*s_tft, now);
      s_lastBounds = {0, 0, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE, true};
    } else {
      FaceRect bounds = faceBounds(p, scale);
      if (s_wake == WAKE_MORPH) {
        s_tft->fillScreen(TFT_BLACK);
        drawFace(*s_tft, p, lidL, lidR, intro, scale, false);
        drawAsciiBoot(*s_tft, now);
        s_lastBounds = {0, 0, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE, true};
      } else {
        clearRect(expandRect(unionRect(s_lastBounds, bounds), 14));
        drawFace(*s_tft, p, lidL, lidR, intro, scale, false);
        s_lastBounds = bounds;
      }
    }
    s_tft->endWrite();
  }
}

static float blinkCurve(float t) {
  if (t >= 1.0f) return 1.0f;
  if (t < 0.28f) {
    const float u = t / 0.28f;
    return 1.0f - u * u * u;
  }
  if (t < 0.38f) return 0.06f;
  const float u = (t - 0.38f) / 0.62f;
  const float e = 1.0f - (1.0f - u) * (1.0f - u);
  return 0.06f + 0.94f * e;
}

void faceInit(TFT_eSPI& tft) {
  s_tft = &tft;
#if TAMAFI_FACE_USE_FULL_SPRITE
#if TAMAFI_FACE_USE_STATIC_SPRITE
  static TamafiStaticSprite staticSprite(&tft);
  s_spr = &staticSprite;
  s_use_sprite = staticSprite.attach16(s_spriteBuffer, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE);
  s_static_sprite = s_use_sprite;
  if (s_use_sprite) {
    Serial.println("Face: static 16-bit sprite buffer ready");
  } else {
    Serial.println("Face: static sprite buffer failed");
  }
#else
  if (!s_spr) s_spr = new TFT_eSprite(&tft);
  if (s_spr) {
    s_spr->setColorDepth(TAMAFI_FACE_SPRITE_BPP);
    Serial.printf("Face: allocating %u-bit sprite\n", (unsigned)TAMAFI_FACE_SPRITE_BPP);
    s_use_sprite = s_spr->createSprite(TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE) != nullptr;
    if (s_use_sprite) {
      Serial.printf("Face: %u-bit sprite buffer ready\n", (unsigned)TAMAFI_FACE_SPRITE_BPP);
    } else {
      Serial.println("Face: sprite buffer failed");
    }
  }
#endif
#else
  s_spr = nullptr;
  s_use_sprite = false;
  s_static_sprite = false;
#endif
  tft.fillScreen(TFT_BLACK);
  s_lastBounds = {0, 0, 0, 0, false};
  memset(&s_vel, 0, sizeof(s_vel));
  s_intro = 1;
  s_scale = 1;
  s_scaleV = 0;
  s_lastTick = millis();
  s_ready = true;
  faceSetMood(TAMAFI_MOOD_IDLE);
  s_live = s_target;
  s_live.dim = 0.32f;
  s_live.faceY = 5;
  s_lidL = s_lidR = 0.05f;
  s_lidVL = s_lidVR = 0;
  s_lidTL = s_lidTR = 0.05f;
  s_wake = WAKE_SCAN;
  s_wakeAt = millis();
  s_nextSaccade = millis() + 4000;
  s_nextBlink = millis() + 8000;
}

void faceSetMood(TamafiMood mood) {
  if (mood >= TAMAFI_MOOD_COUNT) mood = TAMAFI_MOOD_IDLE;
  s_wake = WAKE_DONE;
  s_mood = mood;
  if (mood == TAMAFI_MOOD_DIZZY) {
    uint8_t next = (uint8_t)(esp_random() % kDizzyKinds);
    if (next == s_dizzyLast) next = (next + 1) % kDizzyKinds;
    s_dizzyKind = next;
    s_dizzyLast = next;
    s_live.style = 3;
    s_vel.style = 0;
  }
  s_target = poseFor(mood);
  s_blinking = false;
  s_doubleBlink = false;
  s_lidTL = s_lidTR = 1.0f;
  if (mood == TAMAFI_MOOD_SLEEP) s_lidTL = s_lidTR = 0.14f;
  else if (mood == TAMAFI_MOOD_BLINK) s_lidTL = s_lidTR = 0.08f;
  else if (mood == TAMAFI_MOOD_WINK) {
    s_lidTL = 1.0f;
    s_lidTR = 1.0f;
  } else if (mood == TAMAFI_MOOD_BORED) s_lidTL = s_lidTR = 0.58f;
  else if (mood == TAMAFI_MOOD_LAUGH) s_lidTL = s_lidTR = 0.72f;
  else if (mood == TAMAFI_MOOD_AIR_OPEN) s_lidTL = s_lidTR = 0.68f;
  else if (mood == TAMAFI_MOOD_AIR_NOISE) s_lidTL = s_lidTR = 0.88f;
  s_say[0] = 0;
  s_sayUntil = 0;
  startBeat();
  uint16_t a, h, o, se;
  beatTimes(s_mood, a, h, o, se);
  pickGaze(millis());
  scheduleBlink(millis() + (uint32_t)a + h + o + se + 450);
}

void faceSay(const char* text, uint16_t holdMs) {
  if (!text || !text[0]) {
    s_say[0] = 0;
    s_sayUntil = 0;
    return;
  }
  uint8_t n = 0;
  while (text[n] && n < 16) {
    s_say[n] = text[n];
    n++;
  }
  s_say[n] = 0;
  s_sayAt = millis();
  s_sayUntil = s_sayAt + (holdMs < 900 ? 1400 : holdMs);
}

void faceClearSay() {
  s_say[0] = 0;
  s_sayUntil = 0;
}

void faceSetLook(float x, float y) {
  s_extLook = true;
  s_extLookX = clampf(x, -22.0f, 22.0f);
  s_extLookY = clampf(y, -16.0f, 16.0f);
}

void faceClearLook() { s_extLook = false; }

void faceHoldForHatch() {
  s_wake = WAKE_DONE;
  s_beat = BP_IDLE;
  s_mood = TAMAFI_MOOD_IDLE;
  s_target = poseFor(TAMAFI_MOOD_IDLE);
  s_live = s_target;
  s_live.dim = 0;
  s_punch = 1;
  s_punchT = 1;
  s_shakeX = 0;
  s_lidL = s_lidR = 0.10f;
  s_lidTL = s_lidTR = 0.10f;
  s_lidVL = s_lidVR = 0;
  s_blinking = false;
  s_say[0] = 0;
  s_sayUntil = 0;
}

void faceDrawForHatch(float scale, float lids, float faceY, float gazeX) {
  if (!s_spr) return;
  EmoPose p = poseFor(TAMAFI_MOOD_IDLE);
  p.faceY = faceY;
  p.gazeX = clampf(gazeX, -22.0f, 22.0f);
  p.gazeY = 0;
  p.dim = 0;
  p.gap = 22;
  const float L = clampf(lids, 0.04f, 1.0f);
  if (scale < 0.12f) scale = 0.12f;
  s_plainEyes = true;
  drawFace(*s_spr, p, L, L, 1.0f, scale, false);
  s_plainEyes = false;
}

void faceBeginLife() {
  s_wake = WAKE_DONE;
  s_lidL = s_lidR = 1.0f;
  s_lidTL = s_lidTR = 1.0f;
  s_lidVL = s_lidVR = 0;
  s_blinking = false;
  faceSetMood(TAMAFI_MOOD_HAPPY);
}

bool faceBusy() { return s_wake < WAKE_DONE || s_beat < BP_IDLE; }

TamafiMood faceMood() { return s_mood; }

const char* faceMoodName(TamafiMood mood) {
  if (mood >= TAMAFI_MOOD_COUNT) return "?";
  return kMoodNames[mood];
}

TamafiMood faceMoodFromName(const char* name) {
  if (!name || !name[0]) return TAMAFI_MOOD_COUNT;
  char buf[16];
  uint8_t n = 0;
  while (name[n] && name[n] != ' ' && n < sizeof(buf) - 1) {
    char c = name[n];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    buf[n++] = c;
  }
  buf[n] = 0;
  if (!buf[0]) return TAMAFI_MOOD_COUNT;
  if (!strcmp(buf, "surprised") || !strcmp(buf, "wow")) return TAMAFI_MOOD_SURPRISED;
  if (!strcmp(buf, "confused") || !strcmp(buf, "huh")) return TAMAFI_MOOD_CONFUSED;
  if (!strcmp(buf, "deauth") || !strcmp(buf, "attack") || !strcmp(buf, "d")) return TAMAFI_MOOD_AIR_ATTACK;
  if (!strcmp(buf, "probe") || !strcmp(buf, "noise") || !strcmp(buf, "p")) return TAMAFI_MOOD_AIR_NOISE;
  if (!strcmp(buf, "open") || !strcmp(buf, "o")) return TAMAFI_MOOD_AIR_OPEN;
  if (!strcmp(buf, "l")) return TAMAFI_MOOD_LAUGH;
  if (!strcmp(buf, "w")) return TAMAFI_MOOD_WINK;
  for (uint8_t i = 0; i < TAMAFI_MOOD_COUNT; ++i) {
    if (!strcmp(buf, kMoodNames[i])) return (TamafiMood)i;
  }
  return TAMAFI_MOOD_COUNT;
}

TFT_eSprite* faceSprite() { return s_use_sprite ? s_spr : nullptr; }

void facePresentSprite() {
  if (!s_use_sprite || !s_spr || !s_tft) return;
#if TAMAFI_FACE_USE_FULL_SPRITE && TAMAFI_FACE_USE_STATIC_SPRITE
  if (s_static_sprite) {
    bool oldSwapBytes = s_tft->getSwapBytes();
    s_tft->setSwapBytes(false);
    s_tft->pushImage(0, 0, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE, s_spriteBuffer);
    s_tft->setSwapBytes(oldSwapBytes);
    return;
  }
#endif
  s_spr->pushSprite(0, 0);
}

bool faceTick(bool push) {
  if (!s_tft || !s_ready) return false;
  const uint32_t now = millis();
  if (now - s_lastFrame < 15) return false;

  float dt = (now - s_lastTick) / 1000.0f;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.05f) dt = 0.05f;
  s_lastTick = now;
  s_lastFrame = now;

  float localT = 1.0f;
  s_beat = (s_wake < WAKE_DONE) ? BP_IDLE : beatPhase(now, localT);

  const bool followLook =
      s_extLook && s_wake >= WAKE_DONE && s_beat >= BP_IDLE &&
      (s_mood == TAMAFI_MOOD_IDLE || s_mood == TAMAFI_MOOD_THINK ||
       s_mood == TAMAFI_MOOD_BORED || s_mood == TAMAFI_MOOD_SHY ||
       s_mood == TAMAFI_MOOD_SAD || s_mood == TAMAFI_MOOD_HAPPY ||
       s_mood == TAMAFI_MOOD_EXCITED || s_mood == TAMAFI_MOOD_CONFUSED ||
       s_mood == TAMAFI_MOOD_LISTEN);
  if (followLook) {
    s_gazeTX = s_extLookX;
    s_gazeTY = s_extLookY;
  } else if (s_wake >= WAKE_DONE && s_beat >= BP_IDLE && now >= s_nextSaccade) {
    pickGaze(now);
  }
  const float lookW = s_extLook ? 22.0f : 16.0f;
  spring(s_gazeX, s_gazeVX, s_gazeTX, dt, lookW, 0.72f);
  spring(s_gazeY, s_gazeVY, s_gazeTY, dt, lookW, 0.72f);

  EmoPose target = s_target;
  target.gazeX = s_gazeX;
  target.gazeY = s_gazeY;

  const float tsec = now / 1000.0f;
  const float breath = sinf(tsec * 1.15f);
  if (s_beat < BP_IDLE) {
    applyBeat(target, localT);
  } else switch (s_mood) {
    case TAMAFI_MOOD_SPEAK:
      target.mouthOpen = 0.22f + 0.72f * fabsf(sinf(tsec * 12.5f));
      target.faceY = sinf(tsec * 8.0f) * 2.4f;
      break;
    case TAMAFI_MOOD_HAPPY:
      target.faceY = -3 + sinf(tsec * 5.2f) * 2.6f;
      break;
    case TAMAFI_MOOD_LAUGH:
      target.faceY = -4 + sinf(tsec * 5.0f) * 2.8f;
      break;
    case TAMAFI_MOOD_EXCITED:
      target.faceY = -5 + sinf(tsec * 9.5f) * 4.8f;
      break;
    case TAMAFI_MOOD_SLEEP:
      target.faceY = 7 + breath * 2.4f;
      break;
    case TAMAFI_MOOD_LISTEN:
      target.eyeW = 64 + sinf(tsec * 4.4f) * 2.5f;
      target.eyeH = 40 + sinf(tsec * 4.4f) * 1.6f;
      target.tilt = 0.22f + sinf(tsec * 2.0f) * 0.05f;
      target.gazeX = 4 + sinf(tsec * 1.7f) * 2.0f;
      target.brow = 0.7f + 0.1f * (0.5f + 0.5f * sinf(tsec * 2.4f));
      break;
    case TAMAFI_MOOD_SCARED:
      target.gazeX += sinf(tsec * 22.0f) * 1.2f;
      target.faceY = sinf(tsec * 21.0f) * 1.1f;
      break;
    case TAMAFI_MOOD_AIR_ATTACK: {
      const bool hit = airGlitchHit(now);
      target.gazeX = 0;
      target.gazeY = 0;
      target.eyeW = 56;
      target.eyeH = 56;
      target.mouthOpen = 0.18f;
      target.faceY = -2;
      if (hit) {
        const int dir = (airGlitchBeat(now) & 1) ? 5 : -5;
        s_shakeT = (float)dir;
        target.dim = 0.22f;
        target.gazeX = (float)dir * 0.4f;
      } else {
        s_shakeT = 0;
        target.dim = 0;
      }
      break;
    }
    case TAMAFI_MOOD_AIR_NOISE: {
      const float u = airProbeSweep(now);
      const bool ping = airProbePing(now);
      target.gazeX = -13.0f + u * 26.0f;
      target.gazeY = -2.0f - sinf(u * 3.1416f) * 2.0f;
      target.tilt = -0.14f + u * 0.28f;
      target.faceY = -3;
      target.eyeW = ping ? 66.0f : 60.0f;
      target.eyeH = ping ? 22.0f : 28.0f;
      s_gazeTX = target.gazeX;
      s_gazeTY = target.gazeY;
      break;
    }
    case TAMAFI_MOOD_AIR_OPEN:
      target.gazeX = 8 + sinf(tsec * 1.5f) * 2.2f;
      target.gazeY = 5 + sinf(tsec * 1.2f) * 1.4f;
      target.faceY = 3 + breath * 0.9f;
      target.blush = 0.32f + 0.18f * (0.5f + 0.5f * sinf(tsec * 2.1f));
      break;
    case TAMAFI_MOOD_ANGRY:
      target.faceY = sinf(tsec * 13.0f) * 0.9f;
      break;
    case TAMAFI_MOOD_LOVE:
      target.faceY = sinf(tsec * 3.4f) * 2.4f;
      break;
    case TAMAFI_MOOD_DIZZY: {
      const float spin = (s_dizzyKind == 1) ? 2.0f : (s_dizzyKind == 3) ? 3.2f : 4.2f;
      const float amp = (s_dizzyKind == 2) ? 8.0f : 6.5f;
      target.gazeX = sinf(tsec * spin) * amp;
      target.gazeY = cosf(tsec * spin * 0.84f) * 4.5f;
      target.tilt = 0.2f + sinf(tsec * 4.6f) * 0.14f;
      target.faceY = sinf(tsec * 5.8f) * 2.4f;
      target.mouthOpen = 0.3f + 0.1f * (0.5f + 0.5f * sinf(tsec * 5.2f));
      s_shakeT = sinf(tsec * 13.0f) * ((s_dizzyKind == 1) ? 1.4f : 2.4f);
      break;
    }
    case TAMAFI_MOOD_CRY:
      target.faceY = 8 + sinf(tsec * 6.5f) * 1.1f;
      break;
    case TAMAFI_MOOD_SURPRISED:
      target.eyeW = 60 + sinf(tsec * 6.5f) * 1.1f;
      target.eyeH = 60 + sinf(tsec * 6.5f) * 1.1f;
      break;
    default:
      target.faceY += breath * 1.15f;
      break;
  }

  if (s_wake < WAKE_DONE) {
    const uint32_t e = now - s_wakeAt;
    s_gazeTX = 0;
    s_gazeTY = 0;
    target.gazeX = 0;
    target.gazeY = 0;
    s_blinking = false;
    if (s_wake == WAKE_SCAN) {
      s_lidTL = s_lidTR = 0.05f;
      target.dim = 0.22f;
      s_punchT = 1;
      if (e >= kAsciiScanMs) {
        s_wake = WAKE_DECODE;
        s_wakeAt = now;
      }
    } else if (s_wake == WAKE_DECODE) {
      s_lidTL = s_lidTR = 0.08f;
      target.dim = 0.1f;
      s_punchT = 1;
      if (e >= (uint32_t)kAsciiDecodeMs + kAsciiHoldMs) {
        s_wake = WAKE_MORPH;
        s_wakeAt = now;
      }
    } else {
      const float tL = smootherstep(e / (float)kAsciiMorphMs);
      s_lidTL = 0.10f + 0.90f * clampf(tL, 0.0f, 1.0f);
      s_lidTR = 0.10f + 0.90f * clampf(tL, 0.0f, 1.0f);
      target.dim = 0.10f * (1.0f - tL);
      target.faceY = 3.0f * (1.0f - tL);
      s_punchT = 0.93f + 0.09f * tL;
      if (e >= kAsciiMorphMs) {
        s_lidTL = s_lidTR = 1.0f;
        s_punchT = 1;
        s_wake = WAKE_DONE;
        scheduleBlink(now + 1400);
      }
    }
  }

  float wPose = (s_mood == TAMAFI_MOOD_SLEEP || s_mood == TAMAFI_MOOD_BORED) ? 7.5f : 10.5f;
  if (s_beat == BP_HIT && s_mood != TAMAFI_MOOD_WINK) wPose = 15.0f;
  else if (s_beat == BP_ANTIC && s_mood != TAMAFI_MOOD_WINK) wPose = 13.0f;
  const float zPose = (s_beat == BP_HIT && s_mood != TAMAFI_MOOD_WINK) ? 0.66f : 0.82f;
  spring(s_live.gazeX, s_vel.gazeX, target.gazeX, dt, wPose, zPose);
  spring(s_live.gazeY, s_vel.gazeY, target.gazeY, dt, wPose, zPose);
  spring(s_live.eyeW, s_vel.eyeW, target.eyeW, dt, wPose, zPose);
  spring(s_live.eyeH, s_vel.eyeH, target.eyeH, dt, wPose, zPose);
  spring(s_live.gap, s_vel.gap, target.gap, dt, wPose, zPose);
  spring(s_live.tilt, s_vel.tilt, target.tilt, dt, 9.0f, 0.85f);
  spring(s_live.brow, s_vel.brow, target.brow, dt, 10.0f, 0.85f);
  spring(s_live.browTilt, s_vel.browTilt, target.browTilt, dt, 10.0f, 0.85f);
  spring(s_live.mouth, s_vel.mouth, target.mouth, dt, 9.0f, 0.88f);
  spring(s_live.mouthOpen, s_vel.mouthOpen, target.mouthOpen, dt, 13.0f, 0.75f);
  spring(s_live.faceY, s_vel.faceY, target.faceY, dt, 9.5f, 0.78f);
  spring(s_live.blush, s_vel.blush, target.blush, dt, 7.0f, 0.9f);
  spring(s_live.style, s_vel.style, target.style, dt, 8.0f, 0.9f);
  spring(s_live.dim, s_vel.dim, target.dim, dt, 6.0f, 0.9f);
  spring(s_punch, s_punchV, s_punchT, dt, 14.0f, 0.68f);
  spring(s_shakeX, s_shakeV, s_shakeT, dt, 20.0f, 0.55f);

  if (s_wake < WAKE_DONE) {

  } else if (s_beat < BP_IDLE && s_mood != TAMAFI_MOOD_SLEEP) {

  } else if (s_mood == TAMAFI_MOOD_WINK) {
    s_lidTL = 1.0f;
    s_lidTR = 1.0f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_SLEEP) {
    s_lidTL = s_lidTR = 0.12f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_BORED) {
    s_lidTL = s_lidTR = 0.55f;
  } else if (s_mood == TAMAFI_MOOD_LAUGH) {
    s_lidTL = s_lidTR = 0.72f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_DIZZY && s_dizzyKind == 3) {
    s_lidTL = 0.62f + 0.12f * sinf(tsec * 3.4f);
    s_lidTR = 1.0f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_BLINK) {
    s_lidTL = s_lidTR = 0.08f;
  } else if (s_mood == TAMAFI_MOOD_AIR_ATTACK) {
    s_lidTL = s_lidTR = 1.0f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_AIR_NOISE) {
    s_lidTL = s_lidTR = airProbePing(now) ? 1.0f : 0.88f;
    s_blinking = false;
  } else if (s_mood == TAMAFI_MOOD_AIR_OPEN) {
    s_lidTL = 0.66f + sinf(tsec * 2.4f) * 0.05f;
    s_lidTR = 0.70f + sinf(tsec * 2.1f) * 0.04f;
    s_blinking = false;
  } else {
    if (!s_blinking && now >= s_nextBlink) {
      s_blinking = true;
      s_blinkStart = now;
      s_doubleBlink = (esp_random() % 6) == 0;
    }
    if (s_blinking) {
      const float pL = (now - s_blinkStart) / 190.0f;
      const float pR = (now - (s_blinkStart + 22)) / 190.0f;
      if (pL >= 1.0f && pR >= 1.0f) {
        s_lidTL = s_lidTR = 1.0f;
        s_blinking = false;
        if (s_doubleBlink) {
          s_doubleBlink = false;
          s_blinking = true;
          s_blinkStart = now + 50;
          s_nextBlink = now + 450;
        } else {
          scheduleBlink(now);
        }
      } else {
        s_lidTL = pL < 0 ? 1.0f : blinkCurve(pL);
        s_lidTR = pR < 0 ? 1.0f : blinkCurve(pR);
      }
    } else {
      s_lidTL = s_lidTR = 1.0f;
    }
  }

  const float lidW = (s_wake == WAKE_MORPH) ? 8.5f : 22.0f;
  const float lidZ = (s_wake == WAKE_MORPH) ? 0.92f : 0.78f;
  if (s_mood == TAMAFI_MOOD_WINK) {
    s_lidL = s_lidTL;
    s_lidR = s_lidTR;
    s_lidVL = s_lidVR = 0;
    s_punch = 1;
    s_punchV = 0;
  } else {
    spring(s_lidL, s_lidVL, s_lidTL, dt, lidW, lidZ);
    spring(s_lidR, s_lidVR, s_lidTR, dt, lidW, lidZ);
  }

  present(s_live, s_lidL, s_lidR, s_intro, s_scale * s_punch, push);
  return true;
}
