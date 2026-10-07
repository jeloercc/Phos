#include "phos_body.h"
#include <Arduino.h>
#include <math.h>

namespace {
constexpr float W = 320.0f;
constexpr float H = 240.0f;
float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}
}

uint32_t PhosBody::random32() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

void PhosBody::begin(uint32_t seed) {
  rng_ ^= seed;
  current_.x = W / 2.0f;
  current_.y = H / 2.0f;
  chooseWanderTarget();
}

void PhosBody::chooseWanderTarget() {
  targetX_ = 48.0f + static_cast<float>(random32() % 224);
  targetY_ = 42.0f + static_cast<float>(random32() % 156);
  pauseSeconds_ = 0.3f + static_cast<float>(random32() % 370) / 100.0f;
}

void PhosBody::setBehavior(Behavior behavior) {
  behavior_ = behavior;
  if (behavior == Behavior::CENTER) {
    targetX_ = W / 2.0f;
    targetY_ = H / 2.0f;
  } else if (behavior == Behavior::SINK) {
    targetX_ = W / 2.0f;
    targetY_ = H * 0.72f;
  } else if (behavior == Behavior::ORBIT) {
    orbitAngle_ = 0.0f;
  }
}

void PhosBody::goTo(float x, float y) {
  targetX_ = clampf(x, 22.0f, W - 22.0f);
  targetY_ = clampf(y, 22.0f, H - 22.0f);
  behavior_ = Behavior::ARRIVE;
}

void PhosBody::lookAt(float x, float y) {
  lookX_ = clampf(x, 0.0f, W);
  lookY_ = clampf(y, 0.0f, H);
}

void PhosBody::update(float dt) {
  dt = clampf(dt, 0.001f, 0.08f);
  float desiredX = 0.0f;
  float desiredY = 0.0f;
  if (behavior_ == Behavior::WANDER) {
    const float dx = targetX_ - current_.x;
    const float dy = targetY_ - current_.y;
    if (fabsf(dx) + fabsf(dy) < 12.0f) {
      pauseSeconds_ -= dt;
      if (pauseSeconds_ <= 0.0f) chooseWanderTarget();
    }
    desiredX = dx;
    desiredY = dy;
  } else if (behavior_ == Behavior::ORBIT) {
    orbitAngle_ += dt * 0.7f;
    targetX_ = current_.x + cosf(orbitAngle_) * orbitRadius_;
    targetY_ = current_.y + sinf(orbitAngle_) * orbitRadius_;
    desiredX = targetX_ - current_.x;
    desiredY = targetY_ - current_.y;
  } else {
    desiredX = targetX_ - current_.x;
    desiredY = targetY_ - current_.y;
  }
  const float length = sqrtf(desiredX * desiredX + desiredY * desiredY);
  if (length > 0.1f) {
    const float speed = behavior_ == Behavior::SINK ? 18.0f :
                        behavior_ == Behavior::CENTER ? 34.0f : 38.0f;
    desiredX = desiredX / length * speed;
    desiredY = desiredY / length * speed;
  }
  const float maxAccel = 130.0f * dt;
  current_.vx += clampf(desiredX - current_.vx, -maxAccel, maxAccel);
  current_.vy += clampf(desiredY - current_.vy, -maxAccel, maxAccel);
  const float speed = sqrtf(current_.vx * current_.vx + current_.vy * current_.vy);
  const float maxSpeed = behavior_ == Behavior::SINK ? 22.0f : 55.0f;
  if (speed > maxSpeed) {
    current_.vx = current_.vx / speed * maxSpeed;
    current_.vy = current_.vy / speed * maxSpeed;
  }
  current_.x += current_.vx * dt;
  current_.y += current_.vy * dt;
  if (current_.x < 22.0f || current_.x > W - 22.0f) current_.vx *= -0.6f;
  if (current_.y < 22.0f || current_.y > H - 22.0f) current_.vy *= -0.6f;
  current_.x = clampf(current_.x, 22.0f, W - 22.0f);
  current_.y = clampf(current_.y, 22.0f, H - 22.0f);
  current_.heading = atan2f(current_.vy, current_.vx);
  current_.moving = speed > 4.0f;
  current_.stretch = clampf(speed / maxSpeed, 0.0f, 1.0f);
  current_.turn = clampf((desiredX * current_.vy - desiredY * current_.vx) / 400.0f, -1.0f, 1.0f);
  const float bob = current_.moving ? 0.0f : sinf(millis() * 0.0050265f) * 2.0f;
  current_.leftEyeX = current_.x - 30.0f;
  current_.rightEyeX = current_.x + 30.0f;
  current_.leftEyeY = current_.y + bob + current_.turn * 4.0f;
  current_.rightEyeY = current_.y + bob - current_.turn * 4.0f;
  current_.leftEyeHalfHeight = 17.0f;
  current_.rightEyeHalfHeight = 17.0f;
}

const BodyState &PhosBody::state() const { return current_; }
Behavior PhosBody::behavior() const { return behavior_; }
