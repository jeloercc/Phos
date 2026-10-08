#pragma once

#include <stdint.h>

enum class Behavior : uint8_t { WANDER, ARRIVE, ORBIT, CENTER, SINK };

struct BodyState {
  float x;
  float y;
  float vx;
  float vy;
  float heading;
  float leftEyeX;
  float leftEyeY;
  float rightEyeX;
  float rightEyeY;
  float leftEyeHalfHeight;
  float rightEyeHalfHeight;
  float stretch;
  float turn;
  bool moving;
};

class PhosBody {
 public:
  void begin(uint32_t seed);
  void update(float dt);
  void setBehavior(Behavior behavior);
  void goTo(float x, float y);
  void lookAt(float x, float y);
  const BodyState &state() const;
  Behavior behavior() const;

 private:
  BodyState current_{};
  Behavior behavior_ = Behavior::WANDER;
  float targetX_ = 160.0f;
  float targetY_ = 120.0f;
  float lookX_ = 160.0f;
  float lookY_ = 120.0f;
  float orbitAngle_ = 0.0f;
  float orbitRadius_ = 40.0f;
  float pauseSeconds_ = 0.0f;
  uint32_t rng_ = 0xA5A5A5A5u;
  uint32_t random32();
  void chooseWanderTarget();
};
