<div align="center">

  <img src="https://github.com/user-attachments/assets/11102964-1235-43cf-9924-6c485556b22d" alt="TamaFi Banner" width="100%"/>

  <br/>
  <br/>

  <p align="center">
    <a href="https://github.com/cifertech/TamaFi"><img src="https://img.shields.io/static/v1?label=cifertech&message=TamaFi&color=7e57c2&logo=github"/></a>
    <a href="https://github.com/cifertech/TamaFi"><img src="https://img.shields.io/github/stars/cifertech/TamaFi?style=social"/></a>
    <a href="https://github.com/cifertech/TamaFi"><img src="https://img.shields.io/github/forks/cifertech/TamaFi?style=social"/></a>
    <img src="https://img.shields.io/badge/ESP32--S3-%20Virtual%20Pet-7e57c2"/>
    <img src="https://img.shields.io/badge/license-MIT-7e57c2"/>
  </p>

  <p align="center">
    <a href="https://twitter.com/techcifer"><img src="https://img.shields.io/badge/Twitter-7e57c2?logo=x&logoColor=white"/></a>
    <a href="https://www.instagram.com/cifertech/"><img src="https://img.shields.io/badge/Instagram-7e57c2?logo=instagram&logoColor=white"/></a>
    <a href="https://www.youtube.com/@techcifer"><img src="https://img.shields.io/badge/YouTube-7e57c2?logo=youtube&logoColor=white"/></a>
    <a href="https://cifertech.net/"><img src="https://img.shields.io/badge/Website-7e57c2?logo=googlechrome&logoColor=white"/></a>
  </p>

</div>

&nbsp;

## 📖 Explore the Full Story

Project write-up, rebuild notes, and the V2 design story live on the site:

- [TamaFi V2: I Rebuilt My WiFi-Aware Pocket Pet](https://cifertech.net/tamafi-v2-i-rebuilt-my-wifi-aware-pocket-pet/)
- [Original TamaFi (V1)](https://cifertech.net/tamafi-reviving-the-classic-virtual-pet-with-a-tech-twist/)

<div>&nbsp;</div>

## :star2: About the Project

**TamaFi** is an open-source pocket pet on a custom **ESP32-S3** board. It tracks stats, evolves through life stages, and reads the Wi-Fi and Bluetooth activity around it as its sense of the room.

This is a full V2 rebuild. The original used sprite-sheet faces and simple “Wi-Fi count → hunger” logic. V2 draws the face from code every frame, runs a real decision engine, and ships with a Windows companion app: **TamaFi Lab**.

It runs fully standalone. No app required. Lab is optional.

> [!NOTE]
> In the current public firmware, radio sensing is **read-only**. No SSIDs or MAC addresses are stored, and nothing is transmitted for attacks. This project is for learning, tinkering, and fun.

> [!IMPORTANT]
> This PCB has **no working PSRAM**. The face owns **one** static 240×240 16-bit framebuffer. A second full-screen buffer will fail. That is a board limit, not a firmware bug.



<div align="center">
  <img src="https://github.com/user-attachments/assets/742623f6-e413-45d8-a381-5f590e7163bc" alt="" width="48%"/>
  <img src="https://github.com/user-attachments/assets/b5c4ab4a-f494-4e2a-869a-9489e05949fd" alt="" width="48%"/>
  <br/>
 
</div>

<div>&nbsp;</div>

## :dart: Features

### 👁 Face

- **Code-drawn face** - built every frame. No sprite sheet, no stored bitmaps
- **Hatch** - first boot cracks the egg, then the face comes alive
- **Later boots** - short ASCII intro, then the eyes open
- **Mood stages** - anticipation → expression → hold → rest, with spring motion
- **Radio moods** - deauth glitch, probe alert, open-network wary look

### 🧠 Pet Brain

- **Six stats** - hunger, energy, happiness, health, curiosity, comfort
- **Life stages** - baby → teen → adult → elder (age in minutes, stored in NVS)
- **Activities** - hunt, discover, rest, or idle. Weighted odds, no fixed loop
- **Hunting** - uses nearby Wi-Fi / BLE activity to feed and adjust mood
- **Discovering** - looks for open / interesting radio conditions
- **Resting** - timed sleep face while stats recover

### 📡 The Air

- **Five states** - calm, busy, open, noisy, hostile
- **Dual radio** - Wi-Fi and BLE take turns to stay inside the memory budget
- **Hostile** - heavy deauth / disassoc activity. Sticks around, drops comfort, glitch face
- **Privacy** - read-only in this build. No SSIDs, no MACs stored

### 🎮 Controls & Body

- **Priority order** - motion → microphone → pet activity (only one owns the face)
- **MPU6050** - tilt to look, shake for dizzy, tap / nudge gestures
- **INMP441 mic** - listens for nearby speech
- **Speaker** - MAX98357A + real speaker. Sound auto-disables if heap gets low
- **WS2812 LEDs** - four addressable LEDs for mood / status feedback
- **Buttons** - UP / OK / DOWN for menus · RIGHT1 pet · RIGHT2 env · RIGHT3 log

### 📋 Menus & Progress

- **Pet menu** - stage, activity, live stats
- **Environment** - air state, Wi-Fi / BLE counts, deauth / probe activity
- **Event log** - rolling history of hunts, rests, radio events, mic / motion
- **Stamps** - 11 achievements (first hunt, nap, grown, hostile, heard, day, week, …)

### 💻 TamaFi Lab

- **Live mirror** - face, stats, radio readings, log, and settings over USB
- **Firmware flash** - upload from the desktop app
- **Handshake** - Lab sends `PING` · device replies `PONG TAMAFI`
- **Windows build** - packaged exe under `TamaFi Lab/`. No Python required to run

<div>&nbsp;</div>

## 🔧 Hardware Overview

- **MCU** - ESP32-S3-WROOM-1 (16 MB flash, native USB, Wi-Fi + BLE)
- **Display** - ST7789 240×240 over SPI
- **Motion** - MPU6050 (I2C)
- **Audio in** - INMP441 MEMS microphone (I2S)
- **Audio out** - MAX98357A → speaker
- **LEDs** - 4× WS2812
- **Buttons** - 6 soft tactile switches + ESP32 reset / boot
- **Power** - USB-C · TP4056 Li-ion charge · LF33 3.3 V · SI2302 backlight MOSFET
- **Camera** - OV2640 FPC footprint. **Unused** on this revision (connector pinout reversed; several lines share button GPIOs)

<img width="3240" height="1350" alt="tamafi slide carousel" src="https://github.com/user-attachments/assets/ba402331-dd55-42dd-a9b2-3f751a0b921e" />

<div>&nbsp;</div>

## ⚡ Flashing the Firmware

Sketch folder: [`TamaFi/`](TamaFi/)

1. Install **Arduino IDE** + Boards Manager package **esp32** by Espressif (**2.0.x**)
2. Tools → Board → **ESP32S3 Dev Module**
3. Critical settings:
   - **USB CDC On Boot:** Enabled
   - **PSRAM:** Disabled
   - **USB Mode:** Hardware CDC and JTAG
4. Libraries: [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (Bodmer), [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)
5. Pins live in [`TamaFi/tamafi_pins.h`](TamaFi/tamafi_pins.h) · TFT setup is already loaded by [`TamaFi/config.h`](TamaFi/config.h)
6. Open `TamaFi/Tamafi.ino` → Upload

> Close **TamaFi Lab** and the Serial Monitor before uploading. Only one program can hold the COM port.

Arduino compiles every file inside the sketch folder. Keep experiments outside `TamaFi/` or they get built in by accident.

### Quick checks if something fails

- **No serial / Lab can’t see it** → CDC On Boot left disabled
- **Crash / blank / reboot loop** → PSRAM enabled, or a second 240×240 buffer
- **Buttons dead after “camera” experiments** → camera DVP lines share GPIOs with RIGHT1 / OK / DOWN
- **Upload fails mid-way** → Lab or another serial tool still holding the port

<div>&nbsp;</div>

## 🖥 TamaFi Lab

Windows USB companion. Mirrors the pet live and can flash firmware.

1. Plug in TamaFi
2. Close Arduino Serial Monitor / any other COM tool
3. Run the Lab build from [`TamaFi Lab/`](TamaFi%20Lab/)

Keep the whole Lab folder together if you use a packaged build. The exe needs its `_internal` data next to it.

Handshake over USB CDC at **115200**: `PING` → `PONG TAMAFI`.

<div>&nbsp;</div>

## :warning: License

Distributed under the MIT License. See [LICENSE](LICENSE) for more information.



## Support & Contributions

- 💬 Found a bug or have a feature request? Open an [Issue](https://github.com/cifertech/TamaFi/issues)
- ⭐ Like the project? Star the repo!
- 🛠 Want to contribute? Fork it and submit a pull request



## Contact

- Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)
- CiferTech - [@X](https://twitter.com/techcifer) - CiferTech@gmail.com
- Project Link: [https://github.com/cifertech/TamaFi](https://github.com/cifertech/TamaFi)



## Acknowledgements

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (Bodmer)
- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)
- [Fennec](https://github.com/cifertech/Fennec): same CiferTech ESP32-S3 house style
