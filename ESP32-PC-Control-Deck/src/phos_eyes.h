#pragma once

#include <TFT_eSPI.h>
#include "phos_body.h"

enum class Mood : uint8_t {
  IDLE, HAPPY, SAD, ANGRY, SURPRISED, SKEPTICAL, SLEEPY,
  THINKING, LISTENING, SPEAKING
};

enum class Gesture : uint8_t {
  WINK_L, WINK_R, CONFUSED, LAUGH, NOD, SHAKE, ROLL, STARTLE, PEEK, DIVE, LOOP
};

class PhosEyes {
 public:
  void begin(uint32_t seed);
  void update(float dt);
  void draw(TFT_eSprite &sprite, const BodyState &body);
  void setMood(Mood mood);
  void gesture(Gesture gesture);
  void lookAt(float x, float y);

 private:
  Mood mood_ = Mood::IDLE;
  Gesture gesture_ = Gesture::STARTLE;
  float gazeX_ = 160.0f;
  float gazeY_ = 120.0f;
  float targetGazeX_ = 160.0f;
  float targetGazeY_ = 120.0f;
  float gestureTime_ = 0.0f;
  uint32_t rng_ = 0xBEEFu;
  uint32_t random32();
  void drawEye(TFT_eSprite &sprite, int16_t x, int16_t y, int16_t height,
               bool happy, bool wink, bool left);
};
