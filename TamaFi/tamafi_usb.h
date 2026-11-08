#pragma once

#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "HWCDC.h"
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define TamafiHost Serial
#elif defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
#define TamafiHost USBSerial
#else
inline HWCDC& tamafiUsb() {
  static HWCDC usb;
  return usb;
}
#define TamafiHost tamafiUsb()
#endif
#else
#define TamafiHost Serial
#endif

inline void tamafiHostBegin() {
  Serial.begin(115200);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  TamafiHost.begin(115200);
  TamafiHost.setTxTimeoutMs(0);
#endif
}
