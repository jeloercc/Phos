#include "phos_eyes.h"
#include <math.h>

uint32_t PhosEyes::random32() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

void PhosEyes::begin(uint32_t seed) {
  rng_ ^= seed;
  targetGazeX_ = gazeX_;
  targetGazeY_ = gazeY_;
}

void PhosEyes::setMood(Mood mood) { mood_ = mood; }
void PhosEyes::setEmotion(Emotion emotion, uint8_t intensity, uint32_t holdMs) {
  emotion_ = emotion;
  emotionIntensity_ = min<uint8_t>(100, intensity);
  emotionHoldMs_ = holdMs;
}
void PhosEyes::gesture(Gesture gesture) {
  gesture_ = gesture;
  gestureTime_ = 0.0f;
}
void PhosEyes::lookAt(float x, float y) {
  targetGazeX_ = x;
  targetGazeY_ = y;
}

void PhosEyes::update(float dt) {
  gestureTime_ += dt;
  if (emotionHoldMs_ > 0) {
    const uint32_t elapsed = static_cast<uint32_t>(dt * 1000.0f);
    emotionHoldMs_ = elapsed >= emotionHoldMs_ ? 0 : emotionHoldMs_ - elapsed;
  } else if (emotionIntensity_ > 0) {
    const float fade = min(1.0f, dt / 1.5f);
    emotionIntensity_ = static_cast<uint8_t>(emotionIntensity_ * (1.0f - fade));
  }
  const float spring = 1.0f - expf(-dt / 0.12f);
  gazeX_ += (targetGazeX_ - gazeX_) * spring;
  gazeY_ += (targetGazeY_ - gazeY_) * spring;
  if (mood_ == Mood::THINKING && gestureTime_ > 1.0f) {
    targetGazeX_ = 120.0f + static_cast<float>(random32() % 81);
    targetGazeY_ = 95.0f + static_cast<float>(random32() % 51);
    gestureTime_ = 0.0f;
  }
}

float PhosEyes::emotionFactor(Emotion emotion) const {
    return emotion == Emotion::JOY || emotion == Emotion::SURPRISED ? 1.10f :
           emotion == Emotion::SCARED || emotion == Emotion::SHY ? 0.90f :
           emotion == Emotion::ANGRY ? 0.70f :
           emotion == Emotion::SKEPTICAL || emotion == Emotion::FOCUSED ? 0.75f :
           1.0f;
}

void PhosEyes::drawEye(TFT_eSprite &sprite, int16_t x, int16_t y, int16_t height,
                       bool happy, bool wink, bool left) {
  constexpr int16_t width = 26;
  const int16_t radius = min<int16_t>(width, height) / 2;
  sprite.fillRoundRect(x - width / 2 - 3, y - height / 2 - 3, width + 6, height + 6,
                       radius + 3, 2);
  sprite.fillRoundRect(x - width / 2, y - height / 2, width, height, radius, 6);
  if (height > 10 && !wink) {
    sprite.fillRoundRect(x - width / 2 + 3, y - height / 2 + 3, width - 6, height - 6,
                         max<int16_t>(1, radius - 3), 7);
    sprite.fillRoundRect(x - width / 2 + 7, y - height / 2 + 7, width - 14, height - 14,
                         max<int16_t>(1, radius - 7), 8);
    sprite.fillRect(x + (left ? -5 : 2), y - height / 2 + 8, 3, 3, 9);
  }
  if (happy) sprite.fillEllipse(x, y + height / 4, width / 2, max<int16_t>(2, height / 3), 0);
  if (mood_ == Mood::SAD || mood_ == Mood::ANGRY) {
    sprite.fillTriangle(x - width / 2, y - height / 2, x + width / 2, y - height / 2,
                        x + (mood_ == Mood::ANGRY ? -2 : 4), y - height / 2 + 6, 0);
  }
}

void PhosEyes::draw(TFT_eSprite &sprite, const BodyState &body) {
  const float size = mood_ == Mood::SURPRISED ? 1.2f : 1.0f;
  float height = 34.0f * size;
  if (mood_ == Mood::SLEEPY || mood_ == Mood::SLEEP) height *= 0.55f;
  if (mood_ == Mood::ANGRY) height *= 0.70f;
  if (mood_ == Mood::SKEPTICAL) height *= 0.75f;
  const float emotionSize = emotionFactor(emotion_);
  height *= 1.0f + (emotionSize - 1.0f) * emotionIntensity_ / 100.0f;
  if (emotion_ == Emotion::CONFUSED) height *= 0.9f;
  if (emotion_ == Emotion::SCARED) height *= 0.9f;
  if (mood_ == Mood::SPEAKING) height *= 1.0f + sinf(millis() * 0.025f) * 0.12f;
  if (gesture_ == Gesture::STARTLE && gestureTime_ < 0.3f) height *= 0.8f + gestureTime_ * 1.16f;
  const bool blink = gesture_ == Gesture::WINK_L || gesture_ == Gesture::WINK_R;
  const bool happy = mood_ == Mood::HAPPY || mood_ == Mood::SPEAKING ||
                     mood_ == Mood::PET || emotion_ == Emotion::HAPPY ||
                     emotion_ == Emotion::JOY || emotion_ == Emotion::LOVE ||
                     emotion_ == Emotion::PROUD;
  drawEye(sprite, static_cast<int16_t>(body.leftEyeX), static_cast<int16_t>(body.leftEyeY),
          static_cast<int16_t>(blink && gesture_ == Gesture::WINK_L ? 3 : height), happy,
          blink && gesture_ == Gesture::WINK_L, true);
  drawEye(sprite, static_cast<int16_t>(body.rightEyeX), static_cast<int16_t>(body.rightEyeY),
          static_cast<int16_t>(blink && gesture_ == Gesture::WINK_R ? 3 : height), happy,
          blink && gesture_ == Gesture::WINK_R, false);
}
