#include "hatch.h"

#include "event_log.h"
#include "face.h"
#include "hatch_egg.h"
#include "pet_feedback.h"
#include "pet_state.h"

#include <math.h>

enum HatchPhase : uint8_t {
  HATCH_STILL = 0,
  HATCH_SHAKE,
  HATCH_CRACK,
  HATCH_PEEK,
  HATCH_SPLIT,
  HATCH_EMERGE,
  HATCH_SETTLE,
  HATCH_DONE
};

static const uint16_t kDur[] = {700, 1400, 1100, 1100, 1000, 860, 560};
static const uint16_t kShell = 0xFFDE;

static HatchPhase s_phase = HATCH_DONE;
static uint32_t s_at = 0;
static uint32_t s_last = 0;
static uint8_t s_fx = 0;

static float clampf(float v, float a, float b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

static float easeOut(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t);
}

static float easeIn(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return t * t;
}

static float phaseU() {
  return clampf((float)(millis() - s_at) / (float)kDur[s_phase], 0.0f, 1.0f);
}

static void eggKnock(float u, float at, float dur, float amp, int swings, int dir, float* x, float* y) {
  const float t = (u - at) / dur;
  if (t < 0.0f || t >= 1.0f) return;
  const float env = (1.0f - t) * (1.0f - t);
  const float w = sinf((t + 0.06f) * 3.1415926f * (float)swings);
  *x += w * env * amp * (float)dir;
  *y += fabsf(w) * env * amp * 0.18f;
  if (t < 0.14f) *y -= (1.0f - t / 0.14f) * (amp * 0.12f);
}

static void eggShake(float u, int* ox, int* oy) {
  float x = 0, y = 0;
  eggKnock(u, 0.05f, 0.14f, 10.0f, 3, 1, &x, &y);
  eggKnock(u, 0.30f, 0.16f, 14.0f, 4, -1, &x, &y);
  eggKnock(u, 0.55f, 0.28f, 18.0f, 7, 1, &x, &y);
  *ox = (int)lroundf(x);
  *oy = (int)lroundf(y);
}

static int crackCenterY() {
  return hatchEggY() + hatchCrackMid(hatchEggW() / 2);
}

static void finish(bool alive) {
  s_phase = HATCH_DONE;
  petStateMarkHatched();
  if (alive) {
    faceBeginLife();
    petFeedbackEvent("hatch", TAMAFI_MOOD_HAPPY);
    eventLogPush(LOG_SYS, "hatched");
  }
}

void hatchStart() {
  s_phase = HATCH_DONE;
  if (petStateHatched()) return;
  if (!faceSprite()) {
    petStateMarkHatched();
    return;
  }
  s_phase = HATCH_STILL;
  s_at = millis();
  s_last = 0;
  s_fx = 0;
  faceHoldForHatch();
  Serial.println("hatch: first boot");
}

bool hatchBusy() { return s_phase < HATCH_DONE; }

void hatchSkip() {
  if (s_phase >= HATCH_DONE) return;
  finish(true);
}

bool hatchTick() {
  if (s_phase >= HATCH_DONE) return false;
  const uint32_t now = millis();
  if (now - s_last < 33) return false;
  s_last = now;

  if (now - s_at >= kDur[s_phase]) {
    s_phase = (HatchPhase)(s_phase + 1);
    s_at = now;
    if (s_phase >= HATCH_DONE) {
      finish(true);
      return true;
    }
  }

  const float u = phaseU();
  int shakeX = 0;
  int shakeY = 0;
  int split = 0;
  int open = 0;
  float crackLen = 0;
  float scale = 0.28f;
  float lids = 0.12f;
  float gaze = 0;
  bool showFace = false;

  switch (s_phase) {
    case HATCH_STILL:
      if (u > 0.82f) {
        const float t = (u - 0.82f) / 0.18f;
        shakeX = (int)lroundf(sinf(t * 18.0f) * (1.0f - t) * 3.0f);
      }
      break;
    case HATCH_SHAKE:
      eggShake(u, &shakeX, &shakeY);
      if (!(s_fx & 1) && u > 0.06f) {
        s_fx |= 1;
        petFeedbackEvent("hatch", TAMAFI_MOOD_SCARED);
      }
      break;
    case HATCH_CRACK:
      crackLen = easeOut(u);
      open = 0;
      if (u > 0.72f) open = (int)lroundf(easeOut((u - 0.72f) / 0.28f) * 8.0f);
      shakeX = (int)lroundf(sinf(now / 38.0f) * (1.0f - u) * 2.0f);
      if (!(s_fx & 2) && u > 0.18f) {
        s_fx |= 2;
        petFeedbackEvent("hatch", TAMAFI_MOOD_SURPRISED);
      }
      break;
    case HATCH_PEEK:
      showFace = true;
      crackLen = 1;
      open = 12 + (int)lroundf(easeOut(u) * 4.0f);
      lids = 0.22f + 0.70f * easeOut(u);
      gaze = sinf(u * 5.5f) * 3.0f;
      scale = 0.22f + 0.08f * u;
      shakeX = (int)lroundf(sinf(now / 160.0f) * 1.0f);
      break;
    case HATCH_SPLIT:
      showFace = true;
      crackLen = 1;
      open = 16;
      split = (int)lroundf(easeOut(u) * 36.0f);
      lids = 0.95f;
      scale = 0.36f + 0.28f * easeOut(u);
      gaze = (1.0f - u) * 3.0f;
      if (!(s_fx & 4) && u > 0.18f) {
        s_fx |= 4;
        petFeedbackEvent("hatch", TAMAFI_MOOD_EXCITED);
      }
      break;
    case HATCH_EMERGE:
      showFace = true;
      crackLen = 1;
      open = 16;
      split = 36 + (int)lroundf(easeIn(u) * 140.0f);
      lids = 1;
      scale = 0.64f + 0.36f * easeOut(u);
      break;
    case HATCH_SETTLE:
      showFace = true;
      lids = 1;
      scale = 1;
      if (u > 0.28f && u < 0.52f) lids = 0.08f;
      break;
    default:
      break;
  }

  const int midX = hatchEggW() / 2;
  const int natural = hatchCrackBot(midX) - hatchCrackTop(midX);
  const int opened = (int)lroundf((float)natural * ((float)open / 16.0f));
  if (s_phase < HATCH_SETTLE && showFace) {
    const int hole = (opened > 6 ? opened : 6) + 2 * split;
    if (hole < 22 && split == 0) {
      const float fit = (natural - 8) / 54.0f;
      if (scale > fit) scale = (fit > 0.18f ? fit : 0.18f);
    } else if (hole >= 22) {
      const float fit = (hole - 8) / 54.0f;
      if (scale > fit) scale = fit;
    }
  }

  float faceY = (float)(crackCenterY() - (TAMAFI_FACE_SIZE / 2 - 4));
  if (s_phase == HATCH_EMERGE) faceY *= (1.0f - easeOut(u));
  else if (s_phase >= HATCH_SETTLE) faceY = 0;

  TFT_eSprite* spr = faceSprite();
  if (!spr) {
    finish(true);
    return true;
  }

  spr->fillRect(0, 0, TAMAFI_FACE_SIZE, TAMAFI_FACE_SIZE, TFT_BLACK);
  if (showFace) faceDrawForHatch(scale, lids, faceY, gaze);
  if (s_phase < HATCH_SETTLE) {
    hatchBlitShell(*spr, shakeX, shakeY, split, open, crackLen, kShell);
  }
  facePresentSprite();
  return true;
}
