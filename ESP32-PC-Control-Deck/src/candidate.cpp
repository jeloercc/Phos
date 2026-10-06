#ifdef RUN_CANDIDATE
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "DigitalRainAnimation.hpp"

TFT_eSPI tft = TFT_eSPI();
DigitalRainAnimation<TFT_eSPI> matrix_effect = DigitalRainAnimation<TFT_eSPI>();

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // Setup digital rain
  // true: use custom font, we leave it false to use internal logic or true if Japanese
  matrix_effect.init(&tft);
}

void loop() {
  matrix_effect.loop();
}
#endif
