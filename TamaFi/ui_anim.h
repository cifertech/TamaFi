#pragma once

// ===== Egg / Hatch animations =====
#define EGG_IDLE_DELAY      350   // ms between egg idle frames
#define HATCH_DELAY         300   // ms between hatch frames

// ===== Pet idle animation =====
#define IDLE_BASE_DELAY     200   // default idle speed
#define IDLE_FAST_DELAY     120   // excited
#define IDLE_SLOW_DELAY     280   // bored / sick

// ===== Hunger effect overlay =====
#define HUNGER_EFFECT_DELAY 100   // ms per hunger frame
#define HUNGER_FRAME_COUNT  4

// ===== Rest / sleep animation =====
#define REST_ENTER_DELAY    400   // ms per "going to sleep" frame
#define REST_WAKE_DELAY     400   // ms per "waking up" frame
#define REST_BREATHE_MS     400   // breathing cycle

#define REST_MIN_DURATION   5000  // ms
#define REST_MAX_DURATION   15000  // ms

// ===== Death animation =====
#define DEAD_DELAY          300   // ms per dead frame
#define DEAD_FRAME_COUNT    3

// ===== Menu highlight animation =====
#define MENU_ANIM_INTERVAL  16    // ~60fps
#define MENU_ANIM_STEP      3     // pixels per tick

// ===== Hatch page hint pulse =====
#define PRESS_HINT_PULSE_MS 600
