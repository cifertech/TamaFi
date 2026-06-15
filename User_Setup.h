// TamaFi Configuration for ESP32-S3 ST7789 240x240
// Pin mapping from TamaFi schematic

// ===== DISPLAY DRIVER =====
#define ST7789_DRIVER      

// ===== DISPLAY SIZE =====
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ===== ESP32-S3 GPIO PINS =====
// From schematic:
//   SDA  -> IO35
//   SCL  -> IO36
//   CS   -> IO17
//   D/C  -> IO16
//   RST  -> RESET1 (hardware)
//   LEDC -> IO7 (controlled in code)

#define TFT_MOSI 35   // SDA
#define TFT_SCLK 36   // SCL
#define TFT_CS   17   // CS
#define TFT_DC   16   // D/C
#define TFT_RST  -1   // Hardware reset
// TFT_BL = 7 controlled via PWM in main code

#define USE_HSPI_PORT

// ===== COLOR ORDER =====
#define TFT_RGB_ORDER TFT_RGB

// ===== FONTS =====
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ===== SPI SPEED =====
#define SPI_FREQUENCY        27000000
#define SPI_READ_FREQUENCY   20000000
