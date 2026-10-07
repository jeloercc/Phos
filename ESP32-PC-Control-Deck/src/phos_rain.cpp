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
      }
    }
    for (uint8_t y = 0; y < 24; ++y) {
      if ((random32() % 100) < 3) glyphs_[x][y] = random32() % PHOS_GLYPH_COUNT;
    }
  }
}

void PhosRain::draw(TFT_eSprite &sprite) {
  const float leftX = body_ ? body_->leftEyeX : 130.0f;
  const float leftY = body_ ? body_->leftEyeY : 120.0f;
  const float rightX = body_ ? body_->rightEyeX : 190.0f;
  const float rightY = body_ ? body_->rightEyeY : 120.0f;
  const float leftRadius = body_ ? body_->leftEyeHalfHeight : 17.0f;
  const float rightRadius = body_ ? body_->rightEyeHalfHeight : 17.0f;
  for (uint8_t x = 0; x < 40; ++x) {
    for (uint8_t y = 0; y < 24; ++y) {
      int brightness = 0;
      for (uint8_t drop = 0; drop < 2; ++drop) {
        const Drop &item = drops_[x][drop];
        const int distance = static_cast<int>(item.head) - y;
        if (item.active && distance >= 0 && distance < item.tail) {
          const int value = distance == 0 ? 6 : 5 - (distance * 4 / item.tail);
          if (value > brightness) brightness = value;
        }
      }
      const int16_t px = x * 8 + 4;
      const int16_t py = y * 10 + 5;
      const float leftDistance = sqrtf((px - leftX) * (px - leftX) +
                                       (py - leftY) * (py - leftY));
      const float rightDistance = sqrtf((px - rightX) * (px - rightX) +
                                        (py - rightY) * (py - rightY));
      const float distance = min(leftDistance - leftRadius,
                                 rightDistance - rightRadius);
      if (distance < 10.0f) {
        brightness = 0;
      } else if (distance < 34.0f) {
        brightness = brightness > 0
                         ? max(1, static_cast<int>(brightness * (distance - 10.0f) / 24.0f))
                         : 0;
      }
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
