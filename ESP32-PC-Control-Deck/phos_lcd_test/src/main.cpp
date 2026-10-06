#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {
constexpr uint8_t BACKLIGHT_PIN = 21;
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 240;

TFT_eSPI tft;
}

void setup() {
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("PHOS", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
}

void loop() {
  delay(1000);
}

// TODO(Phase 3): XPT2046 uses dedicated GPIO 25/32/39/33/36 lines.
// The LCD and microSD consume the two hardware SPI buses, so touch will
// likely need a software SPI implementation rather than a shared HW bus.
