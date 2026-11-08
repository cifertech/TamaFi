#include "config.h"
#include "mpu.h"

#include <Wire.h>
#include <math.h>

static const uint8_t kAddrA = 0x68;
static const uint8_t kAddrB = 0x69;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static uint8_t s_addr = kAddrA;
static bool s_ok = false;
static uint32_t s_last = 0;

static float s_ax = 0, s_ay = 0, s_az = 1;
static float s_gx = 0, s_gy = 0, s_gz = 0;
static float s_restX = 0, s_restY = 0, s_restZ = 1;
static uint8_t s_calN = 0;
static bool s_calibrated = false;

static bool s_shake = false;
static bool s_tap = false;
static uint8_t s_fail = 0;

static const int kWin = 32;
static int16_t s_wAx[kWin];
static int16_t s_wAy[kWin];
static int16_t s_wAz[kWin];
static int16_t s_wG[kWin];
static uint8_t s_wHead = 0;
static uint8_t s_wCount = 0;
static uint8_t s_sinceClass = 0;
static TamafiGesture s_gesture = TAMAFI_GESTURE_NONE;
static uint32_t s_coolNudge = 0;
static uint32_t s_coolShake = 0;
static uint32_t s_coolTap = 0;
static uint32_t s_coolTilt = 0;
static uint32_t s_coolFlip = 0;

static bool write8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readN(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)s_addr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; ++i) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool probe(uint8_t addr) {
  s_addr = addr;
  uint8_t id = 0;
  if (!readN(0x75, &id, 1)) return false;
  return id == 0x68 || id == 0x70 || id == 0x71 || id == 0x73 || id == 0x98;
}

bool mpuInit() {
  pinMode(PIN_CAM_PWDN, OUTPUT);
  digitalWrite(PIN_CAM_PWDN, HIGH);
  pinMode(PIN_CAM_RESET, OUTPUT);
  digitalWrite(PIN_CAM_RESET, LOW);

  if (!Wire.begin((int)PIN_I2C_SDA, (int)PIN_I2C_SCL)) {
    Serial.println("MPU6050: Wire.begin failed");
    s_ok = false;
    return false;
  }
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  delay(40);

  s_ok = probe(kAddrA) || probe(kAddrB);
  if (!s_ok) {
    Serial.println("MPU6050: not found on GPIO8/9");
    return false;
  }
  write8(0x6B, 0x00);
  delay(10);
  write8(0x1A, 0x03);
  write8(0x1B, 0x00);
  write8(0x1C, 0x00);
  Serial.print("MPU6050: ok @ 0x");
  Serial.println(s_addr, HEX);
  s_last = millis();
  s_fail = 0;
  s_wHead = 0;
  s_wCount = 0;
  s_sinceClass = 0;
  s_gesture = TAMAFI_GESTURE_NONE;
  return true;
}

bool mpuOk() { return s_ok; }

static int16_t toI16(float v, float scale, int16_t lim) {
  int x = (int)lroundf(v * scale);
  if (x > lim) x = lim;
  if (x < -lim) x = -lim;
  return (int16_t)x;
}

static TamafiGesture classifyWindow() {
  if (s_wCount < kWin) return TAMAFI_GESTURE_NONE;

  double gSum = 0, gSq = 0;
  float gPeak = 0;
  float jPeak = 0;
  float prevMag = 1;
  int zcr = 0;
  int hot = 0;
  int stillTail = 0;
  double txSum = 0, tySum = 0, azSum = 0;
  float prevAx = 0;

  for (int i = 0; i < kWin; ++i) {
    const int idx = (s_wHead + i) % kWin;
    const float ax = s_wAx[idx] / 1000.0f;
    const float ay = s_wAy[idx] / 1000.0f;
    const float az = s_wAz[idx] / 1000.0f;
    const float g = s_wG[idx] / 10.0f;
    gSum += g;
    gSq += (double)g * (double)g;
    if (g > gPeak) gPeak = g;
    if (g > 50.0f) hot++;
    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (i > 0) {
      const float jerk = fabsf(mag - prevMag);
      if (jerk > jPeak) jPeak = jerk;
      if ((ax - s_restX) * (prevAx - s_restX) < 0 && fabsf(ax - prevAx) > 0.12f) zcr++;
    }
    prevMag = mag;
    prevAx = ax;
    txSum += (ay - s_restY);
    tySum += (ax - s_restX);
    azSum += az;
  }
  for (int i = kWin - 10; i < kWin; ++i) {
    const int idx = (s_wHead + i) % kWin;
    if (s_wG[idx] / 10.0f < 38.0f) stillTail++;
  }

  const float gyroMean = (float)(gSum / kWin);
  const float gyroRms = sqrtf((float)(gSq / kWin));
  const float tiltX = clampf((float)(txSum / kWin) * 4.0f, -1.5f, 1.5f);
  const float tiltY = clampf((float)(tySum / kWin) * 4.0f, -1.5f, 1.5f);
  const float tiltMag = sqrtf(tiltX * tiltX + tiltY * tiltY);
  float tiltVar = 0;
  for (int i = 0; i < kWin; ++i) {
    const int idx = (s_wHead + i) % kWin;
    const float tx = clampf((s_wAy[idx] / 1000.0f - s_restY) * 4.0f, -1.5f, 1.5f);
    const float ty = clampf((s_wAx[idx] / 1000.0f - s_restX) * 4.0f, -1.5f, 1.5f);
    const float d = sqrtf(tx * tx + ty * ty) - tiltMag;
    tiltVar += d * d;
  }
  tiltVar /= kWin;
  const float azMean = (float)(azSum / kWin);

  if (azMean * s_restZ < -0.35f && gyroMean < 55.0f && stillTail >= 7) return TAMAFI_GESTURE_FLIP;
  if (jPeak > 0.50f && gyroRms < 100.0f && hot < 10 && zcr <= 2) return TAMAFI_GESTURE_TAP;
  if (gyroRms > 130.0f && zcr >= 3 && hot >= 12) return TAMAFI_GESTURE_SHAKE;
  if (gPeak > 55.0f && gyroRms < 130.0f && zcr <= 2 && hot >= 3 && hot < 16 && jPeak < 0.70f) {
    return TAMAFI_GESTURE_NUDGE;
  }
  if (tiltMag > 0.52f && tiltVar < 0.04f && gyroRms < 48.0f && stillTail >= 8) {
    return TAMAFI_GESTURE_TILT_HOLD;
  }
  return TAMAFI_GESTURE_NONE;
}

void mpuUpdate() {
  if (!s_ok) return;
  const uint32_t now = millis();
  if (now - s_last < 20) return;
  s_last = now;

  uint8_t raw[14];
  if (!readN(0x3B, raw, 14)) {
    if (++s_fail > 20) {
      s_ok = false;
      Serial.println("MPU6050: dropped");
    }
    return;
  }
  s_fail = 0;

  const int16_t rax = (int16_t)((raw[0] << 8) | raw[1]);
  const int16_t ray = (int16_t)((raw[2] << 8) | raw[3]);
  const int16_t raz = (int16_t)((raw[4] << 8) | raw[5]);
  const int16_t rgx = (int16_t)((raw[8] << 8) | raw[9]);
  const int16_t rgy = (int16_t)((raw[10] << 8) | raw[11]);
  const int16_t rgz = (int16_t)((raw[12] << 8) | raw[13]);

  const float ax = rax / 16384.0f;
  const float ay = ray / 16384.0f;
  const float az = raz / 16384.0f;
  const float gx = rgx / 131.0f;
  const float gy = rgy / 131.0f;
  const float gz = rgz / 131.0f;

  s_ax += (ax - s_ax) * 0.25f;
  s_ay += (ay - s_ay) * 0.25f;
  s_az += (az - s_az) * 0.25f;
  s_gx += (gx - s_gx) * 0.35f;
  s_gy += (gy - s_gy) * 0.35f;
  s_gz += (gz - s_gz) * 0.35f;

  if (!s_calibrated) {
    s_restX += ax;
    s_restY += ay;
    s_restZ += az;
    s_calN++;
    if (s_calN >= 40) {
      s_restX /= s_calN;
      s_restY /= s_calN;
      s_restZ /= s_calN;
      s_calibrated = true;
      Serial.println("MPU6050: calibrated");
    }
    return;
  }

  const float gyr = sqrtf(gx * gx + gy * gy + gz * gz);
  s_wAx[s_wHead] = toI16(ax, 1000.0f, 32000);
  s_wAy[s_wHead] = toI16(ay, 1000.0f, 32000);
  s_wAz[s_wHead] = toI16(az, 1000.0f, 32000);
  s_wG[s_wHead] = toI16(gyr, 10.0f, 20000);
  s_wHead = (uint8_t)((s_wHead + 1) % kWin);
  if (s_wCount < kWin) s_wCount++;
  if (++s_sinceClass < 4) return;
  s_sinceClass = 0;

  const TamafiGesture g = classifyWindow();
  if (g == TAMAFI_GESTURE_NONE || s_gesture != TAMAFI_GESTURE_NONE) return;
  uint32_t* cool = nullptr;
  uint32_t wait = 800;
  if (g == TAMAFI_GESTURE_SHAKE) {
    cool = &s_coolShake;
    wait = 1600;
  } else if (g == TAMAFI_GESTURE_NUDGE) {
    cool = &s_coolNudge;
    wait = 900;
  } else if (g == TAMAFI_GESTURE_TAP) {
    cool = &s_coolTap;
    wait = 700;
  } else if (g == TAMAFI_GESTURE_TILT_HOLD) {
    cool = &s_coolTilt;
    wait = 2200;
  } else if (g == TAMAFI_GESTURE_FLIP) {
    cool = &s_coolFlip;
    wait = 1800;
  }
  if (!cool || now < *cool) return;
  s_gesture = g;
  *cool = now + wait;
  if (g == TAMAFI_GESTURE_SHAKE) s_shake = true;
  if (g == TAMAFI_GESTURE_TAP) s_tap = true;
  Serial.printf("MPU: %s\n", mpuGestureName(g));
}

float mpuTiltX() {
  if (!s_calibrated) return 0;
  return clampf((s_ay - s_restY) * 4.0f, -1.0f, 1.0f);
}

float mpuTiltY() {
  if (!s_calibrated) return 0;
  return clampf((s_ax - s_restX) * 4.0f, -1.0f, 1.0f);
}

TamafiGesture mpuConsumeGesture() {
  const TamafiGesture g = s_gesture;
  s_gesture = TAMAFI_GESTURE_NONE;
  if (g == TAMAFI_GESTURE_SHAKE) s_shake = false;
  if (g == TAMAFI_GESTURE_TAP) s_tap = false;
  return g;
}

const char* mpuGestureName(TamafiGesture gesture) {
  switch (gesture) {
    case TAMAFI_GESTURE_NUDGE:
      return "nudge";
    case TAMAFI_GESTURE_SHAKE:
      return "shake";
    case TAMAFI_GESTURE_TAP:
      return "tap";
    case TAMAFI_GESTURE_TILT_HOLD:
      return "tilt-hold";
    case TAMAFI_GESTURE_FLIP:
      return "flip";
    default:
      return "none";
  }
}

bool mpuConsumeShake() {
  if (!s_shake && s_gesture != TAMAFI_GESTURE_SHAKE) return false;
  s_shake = false;
  if (s_gesture == TAMAFI_GESTURE_SHAKE) s_gesture = TAMAFI_GESTURE_NONE;
  return true;
}

bool mpuConsumeTap() {
  if (!s_tap) return false;
  s_tap = false;
  return true;
}

bool mpuUpsideDown() {
  if (!s_calibrated) return false;
  return (s_az * s_restZ) < -0.35f;
}
