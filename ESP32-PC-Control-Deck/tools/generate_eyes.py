cpp_code = r"""
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/base64.h>
#include "phos_glyphs.h"

namespace {
constexpr int16_t W = 320;
constexpr int16_t H = 240;
constexpr uint32_t BAUD = 115200;
constexpr uint8_t BOOT_PIN = 0;
constexpr uint8_t COLUMNS = 40;
constexpr uint32_t FRAME_MS = 33;
constexpr char HEX_GLYPHS[] = "0123456789ABCDEF";
const bool ENABLE_DRIFT = true;

TFT_eSPI tft;
TFT_eSprite frame(&tft);

inline float easeInOut(float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
uint8_t randomByte(uint8_t maxVal) { return static_cast<uint8_t>(random(maxVal)); }
uint32_t rng() { static uint32_t state = 0x7A31C9E1; state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
void printError(const char *msg) { Serial.print("ERR:"); Serial.println(msg); }

enum Emotion : uint8_t {
    NEUTRAL, HAPPY, JOY, LOVE, SAD, ANGRY, SURPRISED, SCARED, SKEPTICAL, CONFUSED,
    CURIOUS, FOCUSED, BORED, SHY, PROUD, DETERMINED, GLITCH, DISGUSTED, MISCHIEVOUS, SLEEPY
};
const char* EMOTION_NAMES[20] = {
    "NEUTRAL", "HAPPY", "JOY", "LOVE", "SAD", "ANGRY", "SURPRISED", "SCARED",
    "SKEPTICAL", "CONFUSED", "CURIOUS", "FOCUSED", "BORED", "SHY", "PROUD", 
    "DETERMINED", "GLITCH", "DISGUSTED", "MISCHIEVOUS", "SLEEPY"
};

struct EyeShape { float topInner, topOuter, bottom, smile, heightScale, yOffset; };
const EyeShape EMOTIONS[20][2] = {
    {{0,0,0,0,1,0}, {0,0,0,0,1,0}}, // NEUTRAL (0)
    {{0,0,0,0.65,1,0}, {0,0,0,0.65,1,0}}, // HAPPY (1)
    {{0,0,0,0.9,1,0}, {0,0,0,0.9,1,0}}, // JOY (2)
    {{0,0,0,0.75,1,0}, {0,0,0,0.75,1,0}}, // LOVE (3)
    {{0,0.4,0,0,1,4}, {0,0.4,0,0,1,4}}, // SAD (4) - Outer corners low, 4px lower
    {{0.4,0,0,0,0.7,0}, {0.4,0,0,0,0.7,0}}, // ANGRY (5) - Inner corners low, height 70%
    {{0,0,0,0,1,0}, {0,0,0,0,1,0}}, // SURPRISED (6) - Fully open
    {{0,0,0.45,0,1,0}, {0,0,0.45,0,1,0}}, // SCARED (7) - Flat bottom
    {{0.4,0.4,0,0,0.5,0}, {0.15,0.15,0,0,1,0}}, // SKEPTICAL (8) - One eye 50% height
    {{0.1,0.5,0,0,1,0}, {0.5,0.1,0,0,1,0}}, // CONFUSED (9) - Asymmetric anger/sad
    {{0,0,0,0,1,0}, {0,0,0,0,1,0}}, // CURIOUS (10) - Base open, dynamics added in drawEyes
    {{0.35,0.35,0.3,0,1,0}, {0.35,0.35,0.3,0,1,0}}, // FOCUSED (11) - Squint
    {{0.5,0.5,0.25,0,1,0}, {0.5,0.5,0.25,0,1,0}}, // BORED (12)
    {{0.5,0.15,0.4,0,1,0}, {0.15,0.5,0.4,0,1,0}}, // SHY (13)
    {{0.15,0.15,0.55,0,1,0}, {0.15,0.15,0.55,0,1,0}}, // PROUD (14)
    {{0.25,0.55,0.3,0,1,0}, {0.25,0.55,0.3,0,1,0}}, // DETERMINED (15)
    {{0.75,0.15,0,0,1,0}, {0.15,0.75,0,0,1,0}}, // GLITCH (16)
    {{0.35,0.15,0.4,0,1,0}, {0.35,0.15,0.4,0,1,0}}, // DISGUSTED (17)
    {{0.15,0.5,0,0.6,1,0}, {0.15,0.5,0,0.6,1,0}}, // MISCHIEVOUS (18)
    {{0.5,0.5,0.2,0,1,0}, {0.5,0.5,0.2,0,1,0}} // SLEEPY (19)
};

enum class State : uint8_t { BOOT, IDLE, LISTENING, THINKING, SPEAKING, ERROR, OFF, SLEEP, PET };
enum class Gesture : uint8_t { NONE, WINK_L, WINK_R, CONFUSED, LAUGH, NOD, SHAKE, ROLL, STARTLE, SIGH, PEEK, DIVE, LOOP };

State currentState = State::BOOT;
State previousState = State::BOOT;
uint32_t stateStartMs = 0;

Gesture currentGesture = Gesture::NONE;
uint32_t gestureStartMs = 0;

Emotion emotion = NEUTRAL;
Emotion previousEmotion = NEUTRAL;
enum Presence : uint8_t { CLOSE, DRIFT };
Presence presence = CLOSE;
Presence prevPresence = CLOSE;

uint32_t moodChangedAt = 0;
uint32_t presenceChangedAt = 0;
uint32_t lastInteractionAt = 0;
bool galleryMode = false;
bool debugMode = false;
float currentFps = 0.0f;

uint32_t lastFrameAt = 0;
uint32_t lastReportAt = 0;
uint32_t nextBlinkAt = 0;
uint32_t blinkStartedAt = 0;
uint32_t frameCount = 0;
uint32_t stageCodeUs = 0, stageEyesUs = 0, stagePushUs = 0, totalFrameUs = 0;
uint16_t fpsFrames = 0;
char serialBuffer[64];
uint8_t serialLength = 0;
bool lastBoot = HIGH;

struct CStream { float row, speed; uint8_t tail, glyph[24]; };
CStream streams[COLUMNS];

struct Gaze { int16_t x, y, targetX, targetY; uint32_t targetAt, nextTargetAt; };
Gaze gaze = {0, 0, 0, 0, 0, 0};

void setPresence(Presence next) {
  if (next == presence) return;
  prevPresence = presence; presence = next; presenceChangedAt = millis();
}

float moodEase(uint32_t now) { return easeInOut(min(1.0f, static_cast<float>(now - moodChangedAt) / 600.0f)); }
float presenceEase(uint32_t now) { return easeInOut(min(1.0f, static_cast<float>(now - presenceChangedAt) / 1000.0f)); }

void setEmotion(Emotion next) {
  if (next == emotion) return;
  previousEmotion = emotion; emotion = next; moodChangedAt = millis();
}

void setState(State next) {
  if (next == currentState) return;
  previousState = currentState; currentState = next; stateStartMs = millis();
}

void setGesture(Gesture next) {
  currentGesture = next; gestureStartMs = millis();
}

void handleCommand(const char *command) {
  if (strcmp(command, "PING") == 0) { Serial.println("OK:PONG");
  } else if (strcmp(command, "IDLE") == 0) { setState(State::IDLE); setEmotion(NEUTRAL); Serial.println("OK:IDLE");
  } else if (strcmp(command, "LISTENING") == 0) { setState(State::LISTENING); setEmotion(NEUTRAL); Serial.println("OK:LISTENING");
  } else if (strcmp(command, "THINKING") == 0) { setState(State::THINKING); setEmotion(CURIOUS); Serial.println("OK:THINKING");
  } else if (strcmp(command, "SPEAKING") == 0) { setState(State::SPEAKING); setEmotion(HAPPY); Serial.println("OK:SPEAKING");
  } else if (strcmp(command, "ERROR") == 0) { setState(State::ERROR); setEmotion(GLITCH); Serial.println("OK:ERROR");
  } else if (strcmp(command, "OFF") == 0) { setState(State::OFF); setEmotion(NEUTRAL); Serial.println("OK:OFF");
  } else if (strcmp(command, "SLEEP") == 0) { setState(State::SLEEP); setEmotion(SLEEPY); Serial.println("OK:SLEEP");
  } else if (strcmp(command, "PET") == 0) { setState(State::PET); setEmotion(LOVE); Serial.println("OK:PET");
  } else if (strcmp(command, "EXCITED") == 0) { setEmotion(JOY); Serial.println("OK:EXCITED");
  } else if (strcmp(command, "LOVING") == 0) { setEmotion(LOVE); Serial.println("OK:LOVING");
  } else if (strcmp(command, "GALLERY") == 0) { galleryMode = true; Serial.println("OK:GALLERY");
  } else if (strcmp(command, "CLOSE") == 0) { setPresence(CLOSE); stateStartMs = millis(); Serial.println("OK:CLOSE");
  } else if (strcmp(command, "DRIFT") == 0) { setPresence(DRIFT); Serial.println("OK:DRIFT");
  } else if (strcmp(command, "DEBUG ON") == 0) { debugMode = true; Serial.println("OK:DEBUG ON");
  } else if (strcmp(command, "DEBUG OFF") == 0) { debugMode = false; Serial.println("OK:DEBUG OFF");
  } else if (strncmp(command, "GESTURE:", 8) == 0) {
      const char* g = command + 8;
      if (strcmp(g, "WINK_L") == 0) setGesture(Gesture::WINK_L);
      else if (strcmp(g, "WINK_R") == 0) setGesture(Gesture::WINK_R);
      else if (strcmp(g, "CONFUSED") == 0) setGesture(Gesture::CONFUSED);
      else if (strcmp(g, "LAUGH") == 0) setGesture(Gesture::LAUGH);
      else if (strcmp(g, "NOD") == 0) setGesture(Gesture::NOD);
      else if (strcmp(g, "SHAKE") == 0) setGesture(Gesture::SHAKE);
      else if (strcmp(g, "ROLL") == 0) setGesture(Gesture::ROLL);
      else if (strcmp(g, "STARTLE") == 0) setGesture(Gesture::STARTLE);
      else if (strcmp(g, "SIGH") == 0) setGesture(Gesture::SIGH);
      else if (strcmp(g, "PEEK") == 0) setGesture(Gesture::PEEK);
      else if (strcmp(g, "DIVE") == 0) setGesture(Gesture::DIVE);
      else if (strcmp(g, "LOOP") == 0) setGesture(Gesture::LOOP);
      Serial.println("OK:GESTURE");
  } else if (strcmp(command, "SNAP") == 0) {
    Serial.println("SNAP_BEGIN");
    uint8_t *ptr = (uint8_t *)frame.getPointer();
    size_t len = 320 * 240 / 2, out_len = 0;
    unsigned char *base64_buf = (unsigned char *)malloc(len * 2);
    if (base64_buf) {
      mbedtls_base64_encode(base64_buf, len * 2, &out_len, ptr, len);
      Serial.write(base64_buf, out_len); Serial.println(); free(base64_buf);
    }
    Serial.println("SNAP_END");
  } else if (command[0] != '\0') { printError("UNKNOWN_COMMAND"); }
}

void readSerial() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\n' || character == '\r') {
      serialBuffer[serialLength] = '\0';
      handleCommand(serialBuffer);
      serialLength = 0;
    } else if (serialLength < sizeof(serialBuffer) - 1) {
      serialBuffer[serialLength++] = character;
    }
  }
}

void initializeStreams() {
  for (uint8_t column = 0; column < COLUMNS; ++column) {
    streams[column].row = static_cast<float>((column * 13 + randomByte(7)) % 24);
    streams[column].speed = 2.5f + static_cast<float>(randomByte(31)) / 10.0f;
    streams[column].tail = 16 + randomByte(9);
    for (uint8_t index = 0; index < 24; ++index) streams[column].glyph[index] = randomByte(16);
  }
}

void updateGazeAndPresence(uint32_t now) {
  if (currentState == State::IDLE && ENABLE_DRIFT && !galleryMode) {
      if (presence == CLOSE && (now - stateStartMs > 30000)) {
          setPresence(DRIFT);
      } else if (presence == DRIFT && (now - presenceChangedAt > 20000 + (rng() % 10000))) {
          setPresence(CLOSE);
          stateStartMs = now; // reset idle timer
      }
  } else {
      if (presence == DRIFT) setPresence(CLOSE);
  }

  if (now >= gaze.nextTargetAt) {
      if (currentState == State::THINKING) {
          gaze.targetX = (rng() % 2 == 0) ? -30 : 30;
          gaze.targetY = -20;
          gaze.nextTargetAt = now + 500 + rng() % 1000;
      } else if (presence == DRIFT) {
          gaze.targetX = static_cast<int16_t>((rng() % 164) - 82);
          gaze.targetY = static_cast<int16_t>((rng() % 61) - 30);
          gaze.nextTargetAt = now + 1000 + rng() % 2000;
      } else {
          gaze.targetX = static_cast<int16_t>((rng() % 81) - 40);
          gaze.targetY = static_cast<int16_t>((rng() % 21) - 10);
          gaze.nextTargetAt = now + 2000 + rng() % 3001;
      }
      gaze.targetAt = now;
  }
  
  if (emotion == SAD) { gaze.targetY = 20; }
  
  float t = easeInOut(min(1.0f, static_cast<float>(now - gaze.targetAt) / 400.0f));
  gaze.x = static_cast<int16_t>(lerp(static_cast<float>(gaze.x), static_cast<float>(gaze.targetX), t));
  gaze.y = static_cast<int16_t>(lerp(static_cast<float>(gaze.y), static_cast<float>(gaze.targetY), t));
}

uint8_t eyeClearance(int16_t x, int16_t y, int16_t cxL, int16_t cxR, int16_t cy, float scale) {
  float wHalf = 34.0f * scale;
  float hHalf = 52.0f * scale;
  
  float dxL = max(0.0f, abs(x - cxL) - wHalf);
  float dyL = max(0.0f, abs(y - cy) - hHalf);
  float distL = sqrt(dxL*dxL + dyL*dyL);
  
  float dxR = max(0.0f, abs(x - cxR) - wHalf);
  float dyR = max(0.0f, abs(y - cy) - hHalf);
  float distR = sqrt(dxR*dxR + dyR*dyR);
  
  float dist = min(distL, distR);
  if (dist < 4.0f) return 2;
  if (dist < 16.0f) return 1;
  return 0;
}

void drawCode(uint32_t now, int16_t cxL, int16_t cxR, int16_t cy, float scale) {
  const uint32_t started = micros();
  const float dt = static_cast<float>(now - lastFrameAt) / 1000.0f;
  const bool dim = (currentState == State::SLEEP || currentState == State::OFF);
  
  float speedMult = 1.0f;
  if (currentState == State::THINKING) speedMult = 2.0f;
  if (currentState == State::SLEEP) speedMult = 0.5f;
  
  float pulseBright = 1.0f;
  if (currentState == State::SPEAKING) {
      pulseBright = 0.5f + 0.5f * sinf(static_cast<float>(now) / 1000.0f * TWO_PI * 4.0f);
  }
  
  for (uint8_t column = 0; column < COLUMNS; ++column) {
    CStream &stream = streams[column];
    stream.row += stream.speed * speedMult * dt;
    if (stream.row - stream.tail > 24.0f) {
      stream.row = -static_cast<float>(8 + randomByte(18));
      stream.speed = 2.5f + static_cast<float>(randomByte(31)) / 10.0f;
      stream.tail = 16 + randomByte(9);
    }
    for (uint8_t row = 0; row < 24; ++row) {
      const int16_t x = column * 8;
      const int16_t y = row * 10;
      const int16_t distance = static_cast<int16_t>(stream.row - row);
      const bool forcedBand = abs(y + 5 - cy) < 20 &&
                              abs(x + 4 - cxL) > 60 && abs(x + 4 - cxR) > 60;
      if ((distance < 0 || distance >= stream.tail) && !forcedBand) continue;
      const uint8_t clearance = eyeClearance(x + 4, y + 5, cxL, cxR, cy, scale);
      if (clearance == 2) continue;
      
      uint8_t color = forcedBand && (distance < 0 || distance >= stream.tail)
                          ? 2
                          : distance == 0 ? 6 : static_cast<uint8_t>(5 - (distance * 4 / stream.tail));
      
      if (currentState == State::SPEAKING && distance == 0) {
          color = static_cast<uint8_t>(3 + pulseBright * 3);
      }
      
      if (currentState == State::SLEEP && color > 3) color = 3;
      if (currentState == State::OFF && color > 1) color = 1;
      
      if (clearance == 1) color = min<uint8_t>(color, 1);
      
      if (randomByte(100) < 3) {
          if (randomByte(100) < 60) stream.glyph[row] = 16 + randomByte(PHOS_GLYPH_COUNT);
          else stream.glyph[row] = randomByte(16);
      }
      
      uint8_t g = stream.glyph[row];
      if (g < 16) {
          frame.setTextColor(color);
          frame.drawChar(HEX_GLYPHS[g], x, y, 1);
      } else {
          g -= 16;
          if (g >= PHOS_GLYPH_COUNT) g = 0;
          const PhosGlyph glyph = PHOS_GLYPHS[g];
          const uint8_t bitmap[8] = { glyph.bits[0], glyph.bits[1], glyph.bits[2], glyph.bits[3], glyph.bits[4], glyph.bits[5], glyph.bits[6], glyph.bits[7] };
          frame.drawBitmap(x, y + 1, bitmap, 8, 8, color);
      }
    }
  }
  stageCodeUs = micros() - started;
}

void drawEye(int16_t cx, int16_t cy, float scale, EyeShape shape, bool isLeft, int16_t gazeX, int16_t gazeY, float breath) {
  int16_t w = static_cast<int16_t>(96 * scale);
  int16_t h = static_cast<int16_t>(150 * scale * breath * shape.heightScale);
  cy += static_cast<int16_t>(shape.yOffset * scale);
  
  if (h <= 0) return; // Fully closed
  
  int16_t r = min(w, h) / 2;

  frame.fillRoundRect(cx - w/2 - 4, cy - h/2 - 4, w + 8, h + 8, r + 4, 2);
  frame.fillRoundRect(cx - w/2, cy - h/2, w, h, r, 6);

  int16_t i7 = static_cast<int16_t>(12 * scale);
  int16_t i8 = static_cast<int16_t>(26 * scale);

  if (w > i7*2 && h > i7*2) frame.fillRoundRect(cx - w/2 + i7, cy - h/2 + i7, w - i7*2, h - i7*2, max<int16_t>(1, r - i7), 7);
  if (w > i8*2 && h > i8*2) frame.fillRoundRect(cx - w/2 + i8, cy - h/2 + i8, w - i8*2, h - i8*2, max<int16_t>(1, r - i8), 8);

  int16_t gx = static_cast<int16_t>(gazeX * scale * 0.25f);
  int16_t gy = static_cast<int16_t>(gazeY * scale * 0.25f);
  int16_t glintSize = max<int16_t>(2, static_cast<int16_t>(10 * scale));
  frame.fillRect(cx + gx - glintSize/2, cy + gy - glintSize/2 - (h/4), glintSize, glintSize, 9);

  int16_t yInner = static_cast<int16_t>(150 * scale * breath * shape.heightScale * shape.topInner);
  int16_t yOuter = static_cast<int16_t>(150 * scale * breath * shape.heightScale * shape.topOuter);
  int16_t ptYLeft = cy - h/2 + (isLeft ? yOuter : yInner);
  int16_t ptYRight = cy - h/2 + (isLeft ? yInner : yOuter);

  int16_t maskX1 = cx - w/2 - 10;
  int16_t maskX2 = cx + w/2 + 10;
  int16_t maskY_top = cy - h/2 - 10;

  frame.fillTriangle(maskX1, maskY_top, maskX2, maskY_top, maskX1, ptYLeft, 0);
  frame.fillTriangle(maskX2, maskY_top, maskX1, ptYLeft, maskX2, ptYRight, 0);

  int16_t btmH = static_cast<int16_t>(150 * scale * breath * shape.heightScale * shape.bottom);
  if (btmH > 0) frame.fillRect(maskX1, cy + h/2 - btmH, maskX2 - maskX1, btmH + 10, 0);

  if (shape.smile > 0.0f) {
    int16_t sh = static_cast<int16_t>((150 * scale * breath * shape.heightScale / 2.0f) * shape.smile);
    if (sh > 0) frame.fillEllipse(cx, cy + h/2, w/2 + 4, sh, 0);
  }
}

EyeShape lerpShape(EyeShape a, EyeShape b, float t) {
    return {
        a.topInner + (b.topInner - a.topInner) * t,
        a.topOuter + (b.topOuter - a.topOuter) * t,
        a.bottom + (b.bottom - a.bottom) * t,
        a.smile + (b.smile - a.smile) * t,
        a.heightScale + (b.heightScale - a.heightScale) * t,
        a.yOffset + (b.yOffset - a.yOffset) * t
    };
}

void drawEyes(uint32_t now) {
  const uint32_t started = micros();
  updateGazeAndPresence(now);
  
  float breath = 1.0f + 0.03f * sinf(static_cast<float>(now) / 2500.0f);
  
  if (now >= nextBlinkAt && currentState != State::BOOT) { blinkStartedAt = now; nextBlinkAt = now + 4000 + rng() % 4001; }
  const bool blinking = blinkStartedAt != 0 && now - blinkStartedAt < 250;

  float ease = moodEase(now);
  EyeShape prevL = EMOTIONS[previousEmotion][0], prevR = EMOTIONS[previousEmotion][1];
  EyeShape currL = EMOTIONS[emotion][0], currR = EMOTIONS[emotion][1];
  EyeShape L = lerpShape(prevL, currL, ease);
  EyeShape R = lerpShape(prevR, currR, ease);
  
  if (emotion == CURIOUS) {
      if (gaze.x < -10) { L.heightScale = 1.15f; R.heightScale = 1.0f; }
      else if (gaze.x > 10) { R.heightScale = 1.15f; L.heightScale = 1.0f; }
  }

  float pEase = presenceEase(now);
  float tScale = (presence == CLOSE) ? 1.0f : 0.35f;
  float pScale = (prevPresence == CLOSE) ? 1.0f : 0.35f;
  float scale = pScale + (tScale - pScale) * pEase;

  int16_t pairCx = W / 2 + gaze.x;
  int16_t pairCy = H / 2 + gaze.y;
  
  // Apply state animations
  if (currentState == State::BOOT) {
      uint32_t t = now - stateStartMs;
      if (t < 2000) { L.heightScale = 0; R.heightScale = 0; }
      else if (t < 2500) { L.heightScale = 0.1; R.heightScale = 0.1; } // horizontal line
      else if (t < 3500) { L.heightScale = easeInOut((t - 2500)/1000.0f); R.heightScale = L.heightScale; }
      // blink twice
      if (t > 4000 && t < 4150) { L.heightScale = 0; R.heightScale = 0; }
      if (t > 4300 && t < 4450) { L.heightScale = 0; R.heightScale = 0; }
  } else if (currentState == State::LISTENING) {
      scale *= 1.1f;
      pairCx = W / 2;
      pairCy = H / 2 + static_cast<int16_t>(sinf(now / 1000.0f * TWO_PI * 1.5f) * 10.0f); // 1.5Hz bob
  } else if (currentState == State::THINKING) {
      L.heightScale *= 0.85f; R.heightScale *= 0.85f;
  } else if (currentState == State::SPEAKING) {
      float pulse = 1.0f + 0.1f * sinf(now / 1000.0f * TWO_PI * 4.0f) + (rng()%10)/100.0f;
      L.heightScale *= pulse; R.heightScale *= pulse;
  } else if (currentState == State::ERROR) {
      L.topInner = 0.4f; L.topOuter = 0; R.topInner = 0; R.topOuter = 0.4f;
      pairCx += (rng() % 10) - 5;
      pairCy += (rng() % 10) - 5;
      if (now - stateStartMs > 3000) { setState(State::IDLE); setEmotion(NEUTRAL); }
  } else if (currentState == State::OFF) {
      uint32_t t = now - stateStartMs;
      if (t < 1000) { L.heightScale = 1.0f - (t/1000.0f); R.heightScale = 1.0f - (t/1000.0f); }
      else { L.heightScale = 0; R.heightScale = 0; }
  } else if (currentState == State::SLEEP) {
      L.topInner=0.55; L.topOuter=0.55; L.bottom=0;
      R.topInner=0.55; R.topOuter=0.55; R.bottom=0;
      pairCy += 20; // sinks
      if (now - blinkStartedAt < 500) { L.heightScale = 0; R.heightScale = 0; }
  } else if (currentState == State::PET) {
      L.smile = 0.8f; R.smile = 0.8f;
      pairCy -= static_cast<int16_t>(abs(sinf(now / 1000.0f * TWO_PI * 2.0f) * 15.0f));
      if (now - stateStartMs > 3000) { setState(previousState); setEmotion(previousEmotion); }
  }
  
  // Apply gestures
  if (currentGesture != Gesture::NONE) {
      uint32_t gt = now - gestureStartMs;
      if (currentGesture == Gesture::WINK_L && gt < 200) L.heightScale = 0;
      else if (currentGesture == Gesture::WINK_R && gt < 200) R.heightScale = 0;
      else if (currentGesture == Gesture::CONFUSED && gt < 1000) { L.topInner=0.4; R.topOuter=0.4; }
      else if (currentGesture == Gesture::LAUGH && gt < 1000) { L.smile=0.8; R.smile=0.8; pairCy -= abs(sinf(gt/100.0f)*10); }
      else if (currentGesture == Gesture::NOD && gt < 800) { pairCy += sinf(gt/800.0f * TWO_PI)*15; }
      else if (currentGesture == Gesture::SHAKE && gt < 800) { pairCx += sinf(gt/800.0f * TWO_PI * 2.0f)*15; }
      else if (currentGesture == Gesture::ROLL && gt < 1000) { pairCx += cosf(gt/1000.0f * TWO_PI)*20; pairCy += sinf(gt/1000.0f * TWO_PI)*20; }
      else if (currentGesture == Gesture::STARTLE && gt < 500) { scale *= 1.2f; L.heightScale=1.1f; R.heightScale=1.1f; pairCy -= 15; }
      else if (currentGesture == Gesture::SIGH && gt < 1500) { pairCy += sinf(gt/1500.0f * PI)*10; L.topInner=0.4; R.topInner=0.4; }
      else if (currentGesture == Gesture::PEEK && gt < 2200) {
          const float phase = min(1.0f, gt / 2200.0f);
          pairCx += static_cast<int16_t>((phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f) * 150.0f - 75.0f);
          L.heightScale *= 0.75f; R.heightScale *= 0.75f;
      }
      else if (currentGesture == Gesture::DIVE && gt < 2000) {
          const float phase = gt / 2000.0f;
          pairCy += static_cast<int16_t>((phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f) * 90.0f);
          L.heightScale *= 0.85f; R.heightScale *= 0.85f;
      }
      else if (currentGesture == Gesture::LOOP && gt < 2000) { pairCx += sinf(gt/2000.0f * TWO_PI)*30; pairCy += sinf(gt/1000.0f * TWO_PI)*15; }
      
      if (gt > 2200) currentGesture = Gesture::NONE;
  }

  if (blinking && currentState != State::ERROR && currentState != State::SLEEP) {
      L.heightScale = 0; R.heightScale = 0;
  }

  int16_t cxL = pairCx - static_cast<int16_t>(62 * scale);
  int16_t cxR = pairCx + static_cast<int16_t>(62 * scale);

  drawCode(now, cxL, cxR, pairCy, scale);

  drawEye(cxL, pairCy, scale, L, true, gaze.x, gaze.y, breath);
  drawEye(cxR, pairCy, scale, R, false, gaze.x, gaze.y, breath);
  
  if (debugMode) {
      frame.setTextColor(3);
      char dbg[64];
      sprintf(dbg, "ST:%d EMO:%s PR:%s FPS:%.1f", (int)currentState, EMOTION_NAMES[emotion], presence == CLOSE ? "C" : "D", currentFps);
      frame.setCursor(2, H - 10);
      frame.print(dbg);
  }
  
  stageEyesUs = micros() - started - stageCodeUs;
}

void drawGallery(uint32_t now) {
    const uint32_t started = micros();
    int cellW = W / 5;
    int cellH = H / 4;
    for (int i=0; i<20; ++i) {
        int cx = (i % 5) * cellW + cellW/2;
        int cy = (i / 5) * cellH + cellH/2 - 4;
        drawEye(cx - 10, cy, 0.20f, EMOTIONS[i][0], true, 0, 0, 1.0f);
        drawEye(cx + 10, cy, 0.20f, EMOTIONS[i][1], false, 0, 0, 1.0f);
        frame.setTextColor(6);
        int16_t tw = frame.textWidth(EMOTION_NAMES[i]);
        frame.setCursor(cx - tw/2, cy + 18);
        frame.print(EMOTION_NAMES[i]);
    }
    stageEyesUs = micros() - started;
    stageCodeUs = 0;
}

void report(uint32_t now) {
  if (now - lastReportAt < 2000) return;
  const uint32_t elapsed = now - lastReportAt;
  const float fps = static_cast<float>(fpsFrames) * 1000.0f / elapsed;
  currentFps = fps;
  Serial.printf("PERF FPS=%.1f frame_us=%lu code_us=%lu eyes_us=%lu push_us=%lu heap=%u largest=%u SPI=%u presence=%s\n",
                fps, static_cast<unsigned long>(totalFrameUs / max<uint16_t>(1, fpsFrames)),
                static_cast<unsigned long>(stageCodeUs), static_cast<unsigned long>(stageEyesUs),
                static_cast<unsigned long>(stagePushUs), ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                static_cast<unsigned>(SPI_FREQUENCY / 1000000), presence == CLOSE ? "CLOSE" : "DRIFT");
  lastReportAt = now; fpsFrames = 0; totalFrameUs = 0;
}
}

void setup() {
  Serial.begin(BAUD);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  tft.init(); tft.setRotation(1);
  frame.setColorDepth(4);
  if (frame.createSprite(W, H) == nullptr) { Serial.println("ERR: failed to create sprite"); while (true) delay(100); }
  const uint16_t colors[16] = { 0x0000, 0x1101, 0x21E1, 0x42E3, 0x6B65, 0x9E87, 0x66EA, 0x87AE, 0xB7D4, 0xE7F9, 0, 0, 0, 0, 0, 0 };
  frame.createPalette(colors, 16);
  frame.setTextFont(1); frame.setTextSize(1); frame.setTextWrap(false);
  initializeStreams();
  lastFrameAt = millis(); lastReportAt = lastFrameAt; lastInteractionAt = millis();
}

void loop() {
  readSerial();
  const uint32_t now = millis();
  const bool boot = digitalRead(BOOT_PIN);
  if (lastBoot == HIGH && boot == LOW) setEmotion(static_cast<Emotion>((emotion + 1) % 20));
  lastBoot = boot;

  if (now - lastFrameAt >= FRAME_MS) {
    const uint32_t started = micros();
    frame.fillSprite(0);
    if (galleryMode) drawGallery(now);
    else drawEyes(now);
    const uint32_t pushStarted = micros();
    frame.pushSprite(0, 0);
    stagePushUs = micros() - pushStarted;
    totalFrameUs += micros() - started;
    ++fpsFrames; ++frameCount; lastFrameAt = now; report(now);
  }
}
"""

generated = "// GENERATED FILE. Edit tools/generate_eyes.py, then run: python3 tools/generate_eyes.py\n" + cpp_code
with open("src/eyes.cpp", "w") as f:
    f.write(generated)
