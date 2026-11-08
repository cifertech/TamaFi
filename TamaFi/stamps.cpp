#include "stamps.h"

#include "event_log.h"
#include "face.h"
#include "pet_feedback.h"
#include "ui_glcd.h"
#include "ui_icons.h"

#include <Preferences.h>
#include <math.h>
#include <string.h>

static const uint32_t kPlayMs = 4000;
static const uint32_t kSlideMs = 320;
static const char* kPrefs = "tamafi_mark";

static Preferences s_prefs;
static bool s_prefsOk = false;
static uint32_t s_bits = 0;
static uint8_t s_pending = 0xFF;
static uint8_t s_playing = 0xFF;
static uint32_t s_playAt = 0;
static bool s_sounded = false;

static const char* kTitle[STAMP_COUNT] = {
    "ONLINE", "HUNTER", "NAP TIME", "GROWN", "HOSTILE", "STATIC",
    "HEARD", "CROWDED", "OPEN AIR", "DAY ONE", "SURVIVOR"};

const char* stampsTitle(StampId id) {
  if (id >= STAMP_COUNT) return "?";
  return kTitle[id];
}

bool stampsHas(StampId id) {
  if (id >= STAMP_COUNT) return false;
  return (s_bits & (1UL << id)) != 0;
}

uint8_t stampsUnlockedCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < STAMP_COUNT; ++i) {
    if (s_bits & (1UL << i)) n++;
  }
  return n;
}

static void saveBits() {
  if (s_prefsOk) s_prefs.putULong("bits", s_bits);
}

static uint16_t mix565(uint16_t a, uint16_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const int r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
  const int r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
  return (uint16_t)(((r1 + (int)((r2 - r1) * t)) << 11) |
                    ((g1 + (int)((g2 - g1) * t)) << 5) |
                    (b1 + (int)((b2 - b1) * t)));
}

static const uint8_t kStampIcon[STAMP_COUNT] = {
    UI_ICON_WIFI, UI_ICON_SEARCH, UI_ICON_SLEEP, UI_ICON_DINO, UI_ICON_SKULL,
    UI_ICON_NOISE, UI_ICON_MIC, UI_ICON_CROWD, UI_ICON_UNLOCK, UI_ICON_SUN,
    UI_ICON_HOURGLASS};

void stampsDrawMark(TFT_eSPI& g, StampId id, int cx, int cy, int size, uint16_t color) {
  if (id >= STAMP_COUNT) return;
  int s = size;
  if (s < 16) s = 16;
  if (s > 32) s = 32;
  uiDrawIconCentered(g, kStampIcon[id], cx, cy, s, color);
}

static void drawFgChar(TFT_eSPI& g, int x, int y, char c, uint16_t fg, uint8_t scale) {
  if (c < 32 || c > 126) c = '?';
  if (scale < 1) scale = 1;
  const uint8_t* glyph = kGlcd5x7 + (c - 32) * 5;
  if (scale == GLCD_M) {
    for (int row = 0; row < 12; ++row) {
      const int srcRow = row * 7 / 12;
      for (int col = 0; col < 9; ++col) {
        const int srcCol = col * 5 / 8;
        const uint8_t bits = (srcCol < 5) ? glyph[srcCol] : 0;
        if (bits & (1 << srcRow)) g.drawPixel(x + col, y + row, fg);
      }
    }
    return;
  }
  for (int col = 0; col < 6; ++col) {
    const uint8_t bits = (col < 5) ? glyph[col] : 0;
    for (int row = 0; row < 8; ++row) {
      if (!(bits & (1 << row))) continue;
      if (scale == 1) g.drawPixel(x + col, y + row, fg);
      else g.fillRect(x + col * scale, y + row * scale, scale, scale, fg);
    }
  }
}

static void drawFgText(TFT_eSPI& g, int x, int y, const char* s, uint16_t fg, uint8_t scale, int tracking) {
  if (!s) return;
  while (*s) {
    drawFgChar(g, x, y, *s++, fg, scale);
    x += glcdCharW(scale) + tracking;
  }
}

static void fillPlate(TFT_eSPI& g, int x, int y, int w, int h, int cut, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  g.fillRect(x + cut, y, w - 2 * cut, h, c);
  g.fillRect(x, y + cut, w, h - 2 * cut, c);
}

static void drawPlateEdge(TFT_eSPI& g, int x, int y, int w, int h, int cut, uint16_t c) {
  g.drawFastHLine(x + cut, y, w - 2 * cut, c);
  g.drawFastHLine(x + cut, y + h - 1, w - 2 * cut, c);
  g.drawFastVLine(x, y + cut, h - 2 * cut, c);
  g.drawFastVLine(x + w - 1, y + cut, h - 2 * cut, c);
  g.drawLine(x, y + cut, x + cut, y, c);
  g.drawLine(x + w - 1, y + cut, x + w - 1 - cut, y, c);
  g.drawLine(x, y + h - 1 - cut, x + cut, y + h - 1, c);
  g.drawLine(x + w - 1, y + h - 1 - cut, x + w - 1 - cut, y + h - 1, c);
}

static void drawStar(TFT_eSPI& g, int x, int y, uint16_t c) {
  g.drawFastHLine(x - 2, y, 5, c);
  g.drawFastVLine(x, y - 2, 5, c);
  g.drawPixel(x - 1, y - 1, c);
  g.drawPixel(x + 1, y - 1, c);
  g.drawPixel(x - 1, y + 1, c);
  g.drawPixel(x + 1, y + 1, c);
}

static void drawCoin(TFT_eSPI& g, int cx, int cy, int rad, StampId id, uint32_t now) {
  const uint16_t gold = 0xE60D;
  const uint16_t goldHi = 0xF716;
  const uint16_t goldMd = 0xF50C;
  const uint16_t goldLo = 0xB366;
  const uint16_t well = 0x2A2A;

  g.fillCircle(cx + 1, cy + 2, rad, 0x0841);
  g.fillCircle(cx, cy, rad, goldLo);
  g.fillCircle(cx, cy, rad - 1, gold);
  g.fillCircle(cx, cy, rad - 2, goldMd);
  g.fillCircle(cx, cy, rad - 5, 0x2A2A);
  g.fillCircle(cx, cy, rad - 6, well);
  g.drawCircle(cx, cy, rad - 1, goldHi);
  g.drawCircle(cx, cy, rad - 5, goldMd);
  g.drawCircle(cx, cy, rad, goldLo);

  const int hx = cx - rad / 3;
  const int hy = cy - rad / 3;
  const int hr = rad > 10 ? rad / 5 : 2;
  g.fillCircle(hx, hy, hr, goldHi);
  g.fillCircle(hx - 1, hy - 1, hr > 2 ? hr / 2 : 1, 0xFFFF);

  stampsDrawMark(g, id, cx, cy, 28, 0xFFEE);

  const float a = now / 260.0f;
  const int px = cx + (int)(cosf(a) * (rad - 1));
  const int py = cy + (int)(sinf(a) * (rad - 1));
  g.fillRect(px, py, 2, 2, 0xFFFF);
}

static float toastSlide(uint32_t dt) {
  if (dt >= kPlayMs) return 0.0f;
  if (dt < kSlideMs) {
    float t = dt / (float)kSlideMs - 1.0f;
    const float c = 1.70158f;
    return 1.0f + t * t * ((c + 1.0f) * t + c);
  }
  if (dt > kPlayMs - kSlideMs) {
    const float u = (dt - (kPlayMs - kSlideMs)) / (float)kSlideMs;
    return 1.0f - u * u * u;
  }
  return 1.0f;
}

void stampsInit() {
  s_prefsOk = s_prefs.begin(kPrefs, false);
  if (s_prefsOk) s_bits = s_prefs.getULong("bits", 0);
  s_pending = 0xFF;
  s_playing = 0xFF;
}

void stampsNote(StampId id) {
  if (id >= STAMP_COUNT) return;
  const uint32_t bit = 1UL << id;
  if (s_bits & bit) return;
  s_bits |= bit;
  saveBits();
  eventLogPush(LOG_PET, stampsTitle(id));
  if (s_pending == 0xFF && s_playing == 0xFF) s_pending = id;
}

bool stampsBusy() { return s_playing != 0xFF; }

bool stampsWantsOverlay() { return s_pending != 0xFF || s_playing != 0xFF; }

bool stampsDismiss() {
  if (s_playing == 0xFF) return false;
  s_playing = 0xFF;
  s_playAt = 0;
  s_sounded = false;
  return true;
}

void stampsTick(bool canShow) {
  if (!canShow) return;
  if (s_playing == 0xFF) {
    if (s_pending == 0xFF) return;
    s_playing = s_pending;
    s_pending = 0xFF;
    s_playAt = millis();
    s_sounded = false;
  }

  TFT_eSprite* spr = faceSprite();
  if (!spr) return;
  TFT_eSPI& g = *spr;
  const uint32_t now = millis();
  const uint32_t dt = now - s_playAt;
  if (dt >= kPlayMs) {
    s_playing = 0xFF;
    s_sounded = false;
    return;
  }

  if (!s_sounded && dt > 40) {
    petFeedbackEvent("stamp", TAMAFI_MOOD_EXCITED);
    s_sounded = true;
  }

  const float slide = toastSlide(dt);
  const int barW = 226;
  const int barH = 64;
  const int cut = 3;
  const int x = 7;
  const int restY = TAMAFI_FACE_SIZE - 5 - barH;
  const int y = (int)(restY + (1.0f - slide) * (barH + 12));
  if (y >= TAMAFI_FACE_SIZE) return;

  const StampId id = (StampId)s_playing;
  const uint16_t gold = 0xE60D;
  const uint16_t goldHi = 0xF716;
  const uint16_t goldLo = 0xB366;
  const uint16_t cream = 0xE36A;
  const uint16_t caption = 0xE36A;

  fillPlate(g, x + 3, y + 4, barW, barH, cut, 0x0000);
  fillPlate(g, x + 2, y + 3, barW, barH, cut, 0x0841);
  fillPlate(g, x, y, barW, barH, cut, 0x1925);
  g.fillRect(x + cut, y + 1, barW - 2 * cut, barH / 3, 0x2A2A);
  g.fillRect(x + cut, y + 1, barW - 2 * cut, 2, 0x034E);
  g.fillRect(x + cut, y + barH - 4, barW - 2 * cut, 3, 0x0841);
  drawPlateEdge(g, x, y, barW, barH, cut, goldLo);
  drawPlateEdge(g, x + 1, y + 1, barW - 2, barH - 2, cut > 1 ? cut - 1 : 1, 0x2A2A);

  g.fillRect(x + 4, y + 8, 3, barH - 16, goldLo);
  g.fillRect(x + 4, y + 8, 2, barH - 16, gold);
  g.fillRect(x + 4, y + 8, 1, barH - 16, goldHi);

  if (dt > kSlideMs && dt < kSlideMs + 1100) {
    const float u = (dt - kSlideMs) / 1100.0f;
    const int sx = x + 4 + (int)(u * (barW - 18));
    const int sw = 14;
    const int x0 = sx < x + 6 ? x + 6 : sx;
    const int x1 = sx + sw;
    const int xRight = x + barW - 6;
    if (x0 < xRight && x1 > x + 6) {
      const int w0 = (x1 > xRight ? xRight : x1) - x0;
      g.fillRect(x0, y + 3, w0 > 4 ? 4 : w0, barH - 6, 0x3186);
      if (w0 > 6) g.fillRect(x0 + 4, y + 3, 3, barH - 6, 0x4A69);
    }
  }

  const int cx = x + 36;
  const int cy = y + barH / 2;
  int rad = 21;
  if (dt < kSlideMs) {
    rad = 16 + (int)(5.0f * (dt / (float)kSlideMs));
  } else if (dt < kSlideMs + 180) {
    const float u = (dt - kSlideMs) / 180.0f;
    rad = 21 + (int)(3.0f * (1.0f - u) * sinf(u * 3.1416f));
  }
  if (rad < 14) rad = 14;
  drawCoin(g, cx, cy, rad, id, now);

  const int tx = x + 70;
  const char* title = stampsTitle(id);
  const int tw = glcdTextW(title, GLCD_L);
  drawStar(g, tx - 8, y + 15, gold);
  drawFgText(g, tx, y + 11, "ACHIEVEMENT UNLOCKED", caption, GLCD_S, 1);
  g.fillRect(tx, y + 23, tw, 1, goldLo);
  g.fillRect(tx, y + 24, tw, 1, mix565(gold, 0x0000, 0.45f));
  drawFgText(g, tx, y + 30, title, cream, GLCD_L, 0);

  if (dt > kSlideMs && dt < kSlideMs + 900) {
    const float a0 = now / 180.0f;
    for (int i = 0; i < 3; ++i) {
      const float a = a0 + i * 2.094f;
      const int px = cx + (int)(cosf(a) * (rad + 4));
      const int py = cy + (int)(sinf(a) * (rad + 3));
      if (py > y + 6 && py < y + barH - 6) drawStar(g, px, py, (i & 1) ? goldHi : gold);
    }
  }
}
