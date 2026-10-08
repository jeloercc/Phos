#pragma once

#include <TFT_eSPI.h>
#include "phos_body.h"

enum class RainMode : uint8_t { NORMAL, FAST, CALM, PULSE, RIPPLE };

class PhosRain {
 public:
  void begin(uint32_t seed);
  void update(float dt, const BodyState &body);
  void draw(TFT_eSprite &sprite);
  void setMode(RainMode mode);

 private:
  struct Drop {
    float head;
    float speed;
    uint8_t tail;
    uint8_t phase;
    bool active;
  };
  Drop drops_[40][2]{};
  uint8_t glyphs_[40][24]{};
  RainMode mode_ = RainMode::NORMAL;
  uint32_t rng_ = 0x12345678u;
  const BodyState *body_ = nullptr;
  uint32_t random32();
};
