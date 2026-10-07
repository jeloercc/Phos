#include "phos_rain.h"
#include "phos_glyphs.h"
#include <math.h>

namespace {
constexpr int16_t W = 320;
constexpr int16_t H = 240;
uint8_t clampIndex(int value) {
  return static_cast<uint8_t>(value < 1 ? 1 : (value > 9 ? 9 : value));
}
}

uint32_t PhosRain::random32() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

void PhosRain::begin(uint32_t seed) {
  rng_ ^= seed;
  for (uint8_t x = 0; x < 40; ++x) {
    for (uint8_t drop = 0; drop < 2; ++drop) {
      drops_[x][drop].head = static_cast<float>(random32() % 24);
      drops_[x][drop].speed = 4.0f + static_cast<float>(random32() % 81) / 10.0f;
      drops_[x][drop].tail = 10 + random32() % 15;
      drops_[x][drop].phase = random32() & 0xFF;
      drops_[x][drop].active = drop == 0 || (random32() % 100) < 40;
    }
    for (uint8_t y = 0; y < 24; ++y) {
      glyphs_[x][y] = random32() % PHOS_GLYPH_COUNT;
      wake_[x][y] = 0;
    }
  }
}

void PhosRain::setMode(RainMode mode) { mode_ = mode; }

void PhosRain::update(float dt, const BodyState &body) {
  body_ = &body;
  const float speedScale = mode_ == RainMode::FAST ? 2.0f :
                           mode_ == RainMode::CALM ? 0.5f : 1.0f;
  for (uint8_t x = 0; x < 40; ++x) {
    for (uint8_t drop = 0; drop < 2; ++drop) {
      Drop &item = drops_[x][drop];
      if (!item.active) continue;
      const float oldHead = item.head;
      item.head += item.speed * speedScale * dt;
      if (item.head - item.tail > 24.0f) {
        item.head = -static_cast<float>(random32() % 8);
      }
      const int oldCell = static_cast<int>(oldHead);
      const int newCell = static_cast<int>(item.head);
      if (newCell != oldCell && newCell >= 0 && newCell < 24) {
        glyphs_[x][newCell] = random32() % PHOS_GLYPH_COUNT;
        wake_[x][newCell] = 18;
      }
    }
    for (uint8_t y = 0; y < 24; ++y) {
      if (wake_[x][y] > 0) --wake_[x][y];
      if ((random32() % 100) < 3) glyphs_[x][y] = random32() % PHOS_GLYPH_COUNT;
    }
  }
}

void PhosRain::draw(TFT_eSprite &sprite) {
  const float eyeX = body_ ? body_->x : 160.0f;
  const float eyeY = body_ ? body_->y : 120.0f;
  const bool moving = body_ && body_->moving;
  const int32_t rx = 83;
  for (uint8_t x = 0; x < 40; ++x) {
    for (uint8_t y = 0; y < 24; ++y) {
      int brightness = 0;
      for (uint8_t drop = 0; drop < 2; ++drop) {
        const Drop &item = drops_[x][drop];
        const int distance = static_cast<int>(item.head) - y;
        if (item.active && distance >= 0 && distance < item.tail) {
          const int value = distance == 0 ? 9 : 8 - (distance * 7 / item.tail);
          if (value > brightness) brightness = value;
        }
      }
      const int16_t px = x * 8 + 4;
      const int16_t py = y * 10 + 5;
      const int32_t dx = px - static_cast<int32_t>(eyeX);
      const int32_t dy = py - static_cast<int32_t>(eyeY);
      const int32_t distanceSq = (dx * dx * 100L) / (rx * rx) + dy * dy;
      if (distanceSq < 38L * 38L) brightness = 0;
      else if (distanceSq < 52L * 52L) brightness = min(brightness, 1);
      if (moving && distanceSq >= 50L * 50L && distanceSq < 56L * 56L) brightness = min(9, brightness + 1);
      if (mode_ == RainMode::CALM) brightness = min(brightness, 3);
      if (brightness > 0) {
        const PhosGlyph glyph = PHOS_GLYPHS[glyphs_[x][y]];
        const uint8_t bitmap[8] = {
            glyph.bits[0], glyph.bits[1], glyph.bits[2], glyph.bits[3],
            glyph.bits[4], glyph.bits[5], glyph.bits[6], glyph.bits[7]};
        sprite.drawBitmap(x * 8, y * 10, bitmap, 8, 8, brightness);
      }
    }
  }
}
