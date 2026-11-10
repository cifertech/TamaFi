# TamaFi V2 - TFT Display Setup Guide


The TamaFi V2 hardware uses **different pins** than V1, and requires specific TFT_eSPI library configuration. Without proper setup, the device will crash with a `Guru Meditation Error` during display initialization.
The ESP32-S3 in TamaFi V2 uses GPIO pins **35 and 36** for the display, which are on the **HSPI bus**. The TFT_eSPI library must be configured to use this bus.

---

## Step-by-Step Setup Instructions

### Step 1: Install Required Libraries

Open **Arduino IDE** and install these libraries via **Tools → Manage Libraries**:

1. **TFT_eSPI** (version 2.5.43 or newer)
2. **Adafruit NeoPixel**
3. **Preferences** (should be built-in with ESP32 core)

### Step 2: Locate TFT_eSPI Library Folder

Find your Arduino libraries folder:

- **Windows**: `C:\Users\[YourUsername]\Documents\Arduino\libraries\TFT_eSPI\`
- **Mac**: `~/Documents/Arduino/libraries/TFT_eSPI/`
- **Linux**: `~/Arduino/libraries/TFT_eSPI/`

### Step 3: Create Custom Setup File

1. Navigate to the `User_Setups` folder inside TFT_eSPI: `TFT_eSPI/User_Setups/`
2. Create a new file called `Setup_TamaFi_V2.h`
3. Add the following configuration:

```cpp
// TamaFi V2 Display Configuration
// Setup file for TFT_eSPI library

#define USER_SETUP_INFO "Setup_TamaFi_V2"

// ST7789 240x240 display driver
#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_RGB

// Display dimensions
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// Pin configuration for TamaFi V2 hardware
#define TFT_MOSI 35  // SDA
#define TFT_SCLK 36  // SCL
#define TFT_CS   17  // Chip Select
#define TFT_DC   16  // Data/Command
#define TFT_RST  -1  // Reset (not used)

// CRITICAL: Use HSPI bus for GPIO 35/36
#define USE_HSPI_PORT

// Font support
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// SPI speed settings
#define SPI_FREQUENCY  27000000      // 27MHz - stable speed
#define SPI_READ_FREQUENCY 20000000  // 20MHz for read operations
```

4. **Save the file**

### Step 4: Edit User_Setup_Select.h

1. Go back to the TFT_eSPI root folder
2. Open `User_Setup_Select.h` in a text editor
3. Find the line (around line 22):
   ```cpp
   #include <User_Setup.h>           // Default setup is root library folder
   ```
4. **Comment it out** by adding `//` at the start:
   ```cpp
   //#include <User_Setup.h>           // Default setup is root library folder
   ```
5. Scroll down and add your custom setup (add this line anywhere after the commented-out default):
   ```cpp
   #include <User_Setups/Setup_TamaFi_V2.h>
   ```
6. **Make sure all other setup includes are commented out** (they should have `//` in front)
7. **Save the file**

### Step 5: Clear Arduino Cache (Important!)

The Arduino IDE caches compiled libraries. After changing TFT_eSPI configuration, you **must** clear the cache:

#### Windows:
1. Close Arduino IDE
2. Navigate to: `C:\Users\[YourUsername]\AppData\Local\arduino\sketches\`
3. Delete all folders inside (these are cached builds)
4. Reopen Arduino IDE

#### Mac:
1. Close Arduino IDE
2. Navigate to: `~/Library/Arduino15/sketches/`
3. Delete all folders inside
4. Reopen Arduino IDE

#### Linux:
1. Close Arduino IDE
2. Navigate to: `~/.arduino15/sketches/`
3. Delete all folders inside
4. Reopen Arduino IDE

### Step 6: Upload TamaFi Code

1. Open the `TamaFi.ino` sketch
2. Select **Board**: "ESP32S3 Dev Module"
3. Configure board settings:
   - USB CDC On Boot: Disabled
   - Upload Speed: 115200
   - Flash Size: 16MB
4. Select your COM port
5. Click **Upload**

---

## Troubleshooting

### Issue: Still getting Guru Meditation Error

**Solution**: 
1. Verify Step 4 - make sure the default `User_Setup.h` is commented out
2. Verify Step 5 - cache must be cleared
3. Check that your `User_Setup_Select.h` includes ONLY `Setup_TamaFi_V2.h`

### Issue: Display shows nothing (black screen)

**Check**:
1. Backlight pin - TFT_eSPI doesn't control it automatically
2. The TamaFi code already handles this, but if testing separately:
   ```cpp
   pinMode(7, OUTPUT);
   digitalWrite(7, HIGH);  // Turn on backlight
   ```

### Issue: Wrong colors or garbled display

**Solution**: The SPI speed might be too high. Edit `Setup_TamaFi_V2.h`:
```cpp
#define SPI_FREQUENCY  20000000  // Reduce from 27MHz to 20MHz
```

### Issue: Display is rotated wrong

**Solution**: Change rotation in code:
```cpp
tft.setRotation(0);  // 0, 1, 2, or 3
```

### Issue: Compile errors about multiple setups

**Solution**: Make sure you commented out ALL other includes in `User_Setup_Select.h`. Only one setup file should be active.

---

## Why USE_HSPI_PORT is Required

The ESP32-S3 has multiple SPI buses:
- **SPI2 (FSPI)** - Default bus, typically uses GPIOs 11-14
- **SPI3 (HSPI)** - Secondary bus, can use various GPIOs including 35/36

The TamaFi V2 hardware routes the display to **GPIO 35 and 36**, which are only accessible via the **HSPI bus**. Without `USE_HSPI_PORT`, TFT_eSPI tries to use the default FSPI bus, which causes a NULL pointer crash.

---

## Hardware Pin Reference

| Signal | GPIO | Notes |
|--------|------|-------|
| TFT_MOSI | 35 | SPI Data Out |
| TFT_SCLK | 36 | SPI Clock |
| TFT_CS | 17 | Chip Select |
| TFT_DC | 16 | Data/Command |
| TFT_RST | - | Not connected |
| Backlight | 7 | Controlled by code |


---

## Why This Method is Better

Creating a custom setup file in `User_Setups/` instead of editing `User_Setup.h` directly has several advantages:

1. **Won't be overwritten** when TFT_eSPI library updates
2. **Multiple configurations** - you can have different setups for different projects
3. **Easy to share** - just share your `Setup_TamaFi_V2.h` file
4. **Cleaner** - keeps your custom config separate from library defaults

---

## Additional Notes

- Always clear Arduino cache after modifying TFT_eSPI configuration
- If you're still having issues, check that GPIO 35/36 aren't being used by PSRAM or other peripherals

---

## Quick Verification

To verify your setup is correct, add this to your `setup()` function:

```cpp
Serial.begin(115200);
Serial.println(USER_SETUP_INFO);
Serial.print("MOSI: "); Serial.println(TFT_MOSI);
Serial.print("SCLK: "); Serial.println(TFT_SCLK);
```

You should see:
```
Setup_TamaFi_V2
MOSI: 35
SCLK: 36
```

---

## Credits

- Original TamaFi project by [CiferTech](https://github.com/cifertech/TamaFi)
- V2 hardware configuration guide by [MJD19994](https://github.com/MJD19994)

---

## Questions or Issues?

If you encounter problems not covered in this guide, please [open an issue](https://github.com/MJD19994/TamaFi/issues) with:
1. Your error message (full serial output)
2. Arduino IDE version
3. ESP32 board core version
4. TFT_eSPI library version
5. What steps you've already tried

