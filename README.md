<div align="center">

  <img src="https://user-images.githubusercontent.com/62047147/195847997-97553030-3b79-4643-9f2c-1f04bba6b989.png" alt="logo" width="100" height="auto" />
  
  <h1> TamaFi </h1>
  <p> WiFi-fed, fully autonomous virtual pet powered by ESP32-S3 </p>


<!-- Badges -->
<a href="https://github.com/cifertech/TamaFi" title="Go to GitHub repo"><img src="https://img.shields.io/static/v1?label=cifertech&message=TamaFi&color=white&logo=github" alt="cifertech - TamaFi"></a>
<a href="https://github.com/cifertech/TamaFi"><img src="https://img.shields.io/github/stars/cifertech/TamaFi?style=social" alt="stars - TamaFi"></a>
<a href="https://github.com/cifertech/TamaFi"><img src="https://img.shields.io/github/forks/cifertech/TamaFi?style=social" alt="forks - TamaFi"></a>

   
<h4>
    <a href="https://twitter.com/techcifer">TWITTER</a>
  <span> · </span>
    <a href="https://www.instagram.com/cifertech/">INSTAGRAM</a>
  <span> · </span>
    <a href="https://www.youtube.com/@techcifer">YOUTUBE</a>
  <span> · </span>
    <a href="https://cifertech.net/">WEBSITE</a>
  </h4>
</div>

<br />


<div>&nbsp;</div>


## 📖 Explore the Full Documentation

Ready to dive deeper into TamaFi's details? Discover the full story, in-depth tutorials, and all the exciting features in our comprehensive [documentation](https://cifertech.net/tamafi-reviving-the-classic-virtual-pet-with-a-tech-twist/). Click the link and explore further!
  
<div>&nbsp;</div>

<!-- About the Project -->
## :star2: About the Project
TamaFi is a modern, WiFi-aware virtual pet.  
V2 rebuilds the original project with **new hardware**, **smarter logic**, and a **clean, compact PCB** that actually feels like a finished device.

The pet lives on an ESP32-S3, eats nearby WiFi, evolves over time, and makes its own decisions based on the environment around it.

---

## ✨ Highlights (V2)

- ⚙ **ESP32-S3 based** – more performance, native USB, better room for future features  
- 🖥 **TFT ST7789 240×240** – full-color UI, custom sprites, status bars, menus  
- 🧠 **Autonomous behavior engine** – the pet hunts, explores, rests, and reacts on its own  
- 📶 **WiFi-fed** – nearby networks affect hunger, happiness, and health  
- 🎛 **Full menu system** – Pet Status, Environment, System Info, Controls, Settings, Diagnostics  
- 💾 **Persistent state** – age, stats, stage, settings stored in flash (Preferences)  
- 🌈 **4× WS2812-2020 NeoPixels** – mood & activity feedback (happy/sad/wifi/rest patterns)  
- 🔊 **Retro sound engine** – non-blocking chiptune-style beeps and sequences  
- 🔋 **Battery-ready** – TP4056 charger + MOSFET-controlled TFT backlight  
- 🖱 **6 soft tactile switches** – three on each side for navigation and shortcuts  
- 🛠 **DisplayKit-ready UI workflow** – UI graphics can be designed/exported via DisplayKit

> ⚠️ Some advanced / sensitive behaviors are intentionally simplified or left as placeholders in the public firmware.  
> This project is for learning, tinkering, and fun — **not** for breaking things.

---

## 🧩 How TamaFi V2 Behaves

TamaFi isn’t just a sprite animation loop. It runs a small decision engine that constantly evaluates:

- Pet stats: `hunger`, `happiness`, `health`
- Traits: curiosity, activity, stress
- Environment: number of nearby networks, open/hidden networks, average RSSI
- Time: age (minutes → hours → days)

From that, it decides whether to:

- 🍖 **Hunt** – use WiFi scan results to “feed” and adjust hunger/happiness/health  
- 👁 **Explore** – react to open/hidden networks and network diversity  
- 😴 **Rest** – enter a sleep cycle with special egg animation and recovery stats  
- 😐 **Idle** – chill on the home screen with mood-based idle animations  

### Mood system

The pet mood changes based on stats + environment:

- `HUNGRY` – low hunger  
- `HAPPY` – good stats + decent WiFi  
- `CURIOUS` – hidden/open networks nearby  
- `BORED` – no WiFi for too long  
- `SICK` – low health or bad conditions  
- `EXCITED` / `CALM` – good conditions & variety

Mood affects:

- Idle animation speed  
- LED patterns  
- How often it wants to hunt / explore / rest  

### Evolution

Age is tracked in **minutes, hours, and days**:

- `BABY` → `TEEN` → `ADULT` → `ELDER`

Evolution depends on:

- Time alive  
- Average of hunger / happiness / health  
- Environment quality

Reaching a new stage triggers special SFX + animations.

---

## 🖥 UI & Menu System

All rendering goes through a 240×240 framebuffer (`TFT_eSPI` + `TFT_eSprite`) to avoid flicker.

### Home Screen

- Pet sprite (idle / hunt / rest / special states)  
- Background image  
- Stats bars: Hunger, Happiness, Health  
- Mood + Stage text  
- Activity label in the top bar (`Idle`, `Hunting…`, `Resting…`, etc.)  
- Overlays for special effects (e.g. hunger effect)

### Menus

Navigation is done using the left 3 buttons (UP / OK / DOWN).

**Main Menu:**

1. **Pet Status** – stats, mood, age, stage, short description  
2. **Environment** – WiFi network counts, hidden/open, “signal mood”  
3. **System Info** – firmware version, uptime, battery (if implemented), etc.  
4. **Controls** – brightness, LED level, sound on/off, NeoPixel on/off  
5. **Settings** – auto-sleep, auto-save interval, soft reset options  
6. **Diagnostics** – debug info, test modes (non-destructive)  
7. **Back to Home**

Changes in Controls/Settings are saved to NVS via `Preferences`, so they persist across reboots.

---

## 🔌 Hardware Overview

> This is a summary of the V2 hardware stack.  
> Check the `PCB/` and `Schematic/` directories for full details.

### Core

- **MCU:** ESP32-S3 module  
- **Display:** 1.3–1.54" TFT ST7789 @ 240×240  
- **Buttons:** 6× soft tactile switches  
- **LEDs:** 4× WS2812-2020 addressable RGB  
- **Buzzer:** driven with PWM (LEDc) for retro SFX

### Power

- **Charger:** TP4056 (single-cell Li-ion/Li-Po)  
- **Backlight control:** MOSFET on TFT LED pin, driven by PWM for brightness levels  
- **Power input:** USB-C (on PCB)  

### Connectivity / Dev

- ESP32-S3 native USB for firmware upload  
- Optional **CP2102 USB-to-TTL** on the board for easier flashing / serial during development

---

## 🧰 Firmware

The firmware is written for **Arduino IDE** using:

- `TFT_eSPI`  
- `Adafruit_NeoPixel`  
- `WiFi`  
- `Preferences`  

### Core modules (conceptual)

- `main.ino` – hardware init, timers, logic tick, button handling  
- `ui.cpp / ui.h` – rendering, menus, bar drawing, layout  
- `ui_anim.h` – sprite frame tables for idle, egg, hunt, etc.  
- `sound` – non-blocking retro sound sequencer using LEDc  
- `state` – pet stats, traits, persistence with `Preferences`

> The code is structured around **non-blocking updates**:  
> no `delay()` in the main logic, so animations, WiFi, sound, and UI can all coexist smoothly.

---

## 🧪 Status

> V2 is still under active development. Expect rough edges.

| Area            | Status       | Notes                                      |
|-----------------|-------------|--------------------------------------------|
| Core stats      | ✅ Stable    | Hunger, happiness, health, age             |
| Mood system     | ✅ Stable    | Driven by stats + WiFi environment         |
| Evolution       | ✅ Stable    | Multi-stage, environment-aware             |
| WiFi “feeding”  | ⚠ In progress | Logic works; tuning values ongoing       |
| Rest system     | ✅ Stable    | Enter/exit animations + stat recovery      |
| Hunt animation  | ✅ Stable    | Uses dedicated attack frames               |
| Menus & UI      | ⚠ In progress | Layout & styling still evolving          |
| Sound engine    | ✅ Stable    | Non-blocking retro SFX                     |
| NeoPixel effects| ⚠ In progress | Mood + activity patterns, still tweaking |
| Desktop tooling | ⚠ Planned   | Companion webapp / deeper integration      |

---

<!-- License -->
## :warning: License

Distributed under the MIT License. See LICENSE.txt for more information.

<div>&nbsp;</div>

<!-- Contact -->
## :handshake: Contact

▶ Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)

CiferTech - [@twitter](https://twitter.com/techcifer) - CiferTech@gmali.com

Project Link: [https://github.com/cifertech/TamaFi](https://github.com/cifertech/TamaFi)

<div>&nbsp;</div>

<!-- Acknowledgments -->
## :gem: Acknowledgements 

**The libraries and projects listed below are used in this software:**
 - [Mecha-Stone Golem](https://darkpixel-kronovi.itch.io/mecha-golem-free)
 - [Fantasy Wooden GUI](https://kanekizlf.itch.io/fantasy-wooden-gui-free)
 - [MyPixelWorld](https://scarloxy.itch.io/mpwsp01)
 - [Transparent Sprites](https://github.com/VolosR/SpritesTuT)



