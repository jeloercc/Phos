#pragma once

#include <TFT_eSPI.h>
#include "phos_body.h"

enum class Mood : uint8_t {
  BOOT, IDLE, LISTENING, THINKING, SPEAKING, ERROR, OFF, SLEEP, PET,
  HAPPY, SAD, ANGRY, SURPRISED, SKEPTICAL, SLEEPY
};

enum class Emotion : uint8_t {
  NEUTRAL, HAPPY, JOY, LOVE, SAD, ANGRY, SURPRISED, SCARED, SKEPTICAL,
  CONFUSED, CURIOUS, FOCUSED, BORED, SHY, PROUD, DETERMINED, GLITCH
};

enum class Gesture : uint8_t {
  WINK_L, WINK_R, CONFUSED, LAUGH, NOD, SHAKE, ROLL, STARTLE, PEEK, DIVE, LOOP
};

class PhosEyes {
 public:
  void begin(uint32_t seed);
  void update(float dt);
  void draw(TFT_eSprite &sprite, const BodyState &body);
  void drawGalleryCell(TFT_eSprite &sprite, int16_t x, int16_t y, Emotion emotion);
  void setMood(Mood mood);
  void setEmotion(Emotion emotion, uint8_t intensity, uint32_t holdMs);
  void gesture(Gesture gesture);
  void lookAt(float x, float y);

 private:
  Mood mood_ = Mood::IDLE;
  Emotion emotion_ = Emotion::NEUTRAL;
  uint8_t emotionIntensity_ = 0;
  uint32_t emotionHoldMs_ = 0;
  Gesture gesture_ = Gesture::STARTLE;
  float gazeX_ = 160.0f;
  float gazeY_ = 120.0f;
  float targetGazeX_ = 160.0f;
  float targetGazeY_ = 120.0f;
  float gestureTime_ = 0.0f;
  uint32_t rng_ = 0xBEEFu;
  uint32_t random32();
  float emotionFactor(Emotion emotion) const;
  void drawEye(TFT_eSprite &sprite, int16_t x, int16_t y, int16_t height,
               bool happy, bool wink, bool left);
};
