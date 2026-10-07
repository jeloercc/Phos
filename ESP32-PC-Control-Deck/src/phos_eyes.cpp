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
  const float spring = 1.0f - expf(-dt / 0.12f);
  gazeX_ += (targetGazeX_ - gazeX_) * spring;
  gazeY_ += (targetGazeY_ - gazeY_) * spring;
  if (mood_ == Mood::THINKING && gestureTime_ > 1.0f) {
    targetGazeX_ = 120.0f + static_cast<float>(random32() % 81);
    targetGazeY_ = 95.0f + static_cast<float>(random32() % 51);
    gestureTime_ = 0.0f;
  }
}

void PhosEyes::drawEye(TFT_eSprite &sprite, int16_t x, int16_t y, int16_t height,
                       bool happy, bool wink, bool left) {
  constexpr int16_t width = 26;
  const int16_t radius = min<int16_t>(width, height) / 2;
  sprite.fillRoundRect(x - width / 2 - 7, y - height / 2 - 7, width + 14, height + 14, radius + 7, 2);
  sprite.fillRoundRect(x - width / 2 - 4, y - height / 2 - 4, width + 8, height + 8, radius + 4, 3);
  sprite.fillRoundRect(x - width / 2, y - height / 2, width, height, radius, 8);
  if (height > 10 && !wink) {
    sprite.fillRoundRect(x - width / 2 + 5, y - height / 2 + 5, width - 10, height - 10,
                         max<int16_t>(1, radius - 5), 9);
    sprite.fillRect(x + (left ? -5 : 2), y - height / 2 + 7, 3, 3, 9);
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
  if (mood_ == Mood::SLEEPY) height *= 0.55f;
  if (mood_ == Mood::ANGRY) height *= 0.70f;
  if (mood_ == Mood::SKEPTICAL) height *= 0.75f;
  if (mood_ == Mood::SPEAKING) height *= 1.0f + sinf(millis() * 0.025f) * 0.12f;
  if (gesture_ == Gesture::STARTLE && gestureTime_ < 0.3f) height *= 0.8f + gestureTime_ * 1.16f;
  const bool blink = gesture_ == Gesture::WINK_L || gesture_ == Gesture::WINK_R;
  const bool happy = mood_ == Mood::HAPPY || mood_ == Mood::SPEAKING;
  drawEye(sprite, static_cast<int16_t>(body.leftEyeX), static_cast<int16_t>(body.leftEyeY),
          static_cast<int16_t>(blink && gesture_ == Gesture::WINK_L ? 3 : height), happy,
          blink && gesture_ == Gesture::WINK_L, true);
  drawEye(sprite, static_cast<int16_t>(body.rightEyeX), static_cast<int16_t>(body.rightEyeY),
          static_cast<int16_t>(blink && gesture_ == Gesture::WINK_R ? 3 : height), happy,
          blink && gesture_ == Gesture::WINK_R, false);
}
