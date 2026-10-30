#include "q43_capture.h"

// Teensy: Serial is USB; Serial1 is an independent hardware UART.
// Teensy 4.0/4.1: Q43 TX -> pin 0 (RX1), common GND.
// Leave Q43 RX disconnected for this receive-only capture.
// Check the pinout for your exact model if using an older Teensy.

uint32_t gnss_rx_bytes = 0;

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && (uint32_t)(millis() - start) < 3000U) {
    // Allow the USB monitor to open, but also run without a monitor.
  }
  Serial1.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("Q43 capture: Serial1 RX=0, TX=1, baud=115200");
}

void loop() {
  gnss_rx_bytes += captureQ43(Serial1, Serial);

  static uint32_t lastBlink = 0;
  static bool ledOn = false;
  uint32_t now = millis();
  if ((uint32_t)(now - lastBlink) >= 500U) {
    lastBlink = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
  }

  static uint32_t lastReport = 0;
  if ((uint32_t)(now - lastReport) >= 2000U) {
    lastReport = now;
    if (Serial) {
      Serial.print("\r\n[Teensy alive; GNSS received bytes: ");
      Serial.print(gnss_rx_bytes);
      Serial.println("]");
    }
  }
}
