#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "phos_body.h"
#include "phos_eyes.h"
#include "phos_rain.h"

namespace {
TFT_eSPI tft;
TFT_eSprite sprite(&tft);
PhosBody body;
PhosEyes eyes;
PhosRain rain;
uint32_t lastFrame = 0, lastReport = 0, frames = 0, commands = 0;
uint32_t rainUs = 0, bodyUs = 0, eyesUs = 0, pushUs = 0;
char line[64];
uint8_t lineLength = 0;
bool lastBoot = HIGH;
bool galleryMode = false;
bool demoMode = false;
uint32_t demoStarted = 0;
uint8_t demoStage = 0;
const uint16_t PHOS_PALETTE[16] = {
    0x0000, 0x08C1, 0x1181, 0x2283, 0x3365, 0x4CC7, 0x6E2A, 0x872E,
    0xB794, 0xDFFA, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

bool paletteOk() {
  bool ok = true;
  for (uint8_t index = 0; index < 16; ++index) {
    const uint16_t actual = sprite.getPaletteColor(index);
    if (actual != PHOS_PALETTE[index]) {
      if (ok) Serial.println("PALETTE_FAIL");
      Serial.printf("PALETTE_MISMATCH index=%u expected=%04X actual=%04X\n",
                    index, PHOS_PALETTE[index], actual);
      ok = false;
    }
  }
  if (ok) Serial.println("PALETTE_OK");
  return ok;
}

void sendSnapshot() {
  const uint8_t *buffer = static_cast<const uint8_t *>(sprite.getPointer());
  Serial.println("SNAP_BEGIN");
  for (size_t index = 0; index < 320U * 240U / 2U; ++index) {
    Serial.printf("%02X", buffer[index]);
    if ((index & 31U) == 31U) Serial.println();
  }
  Serial.println();
  Serial.println("SNAP_END");
}

Mood moodFor(const char *state) {
  if (!strcmp(state, "SPEAKING")) return Mood::SPEAKING;
  if (!strcmp(state, "SLEEP")) return Mood::SLEEP;
  if (!strcmp(state, "THINKING")) return Mood::THINKING;
  if (!strcmp(state, "LISTENING")) return Mood::LISTENING;
  if (!strcmp(state, "ERROR")) return Mood::ERROR;
  if (!strcmp(state, "OFF")) return Mood::OFF;
  if (!strcmp(state, "BOOT")) return Mood::BOOT;
  if (!strcmp(state, "PET")) return Mood::PET;
  return Mood::IDLE;
}
bool setEmotionByName(const char *name, uint8_t intensity, uint32_t holdMs) {
  static const char *names[] = {"NEUTRAL", "HAPPY", "JOY", "LOVE", "SAD", "ANGRY",
                                "SURPRISED", "SCARED", "SKEPTICAL", "CONFUSED",
                                "CURIOUS", "FOCUSED", "BORED", "SHY", "PROUD",
                                "DETERMINED", "GLITCH"};
  for (uint8_t index = 0; index < 17; ++index) {
    if (!strcmp(name, names[index])) {
      eyes.setEmotion(static_cast<Emotion>(index), intensity, holdMs);
      return true;
    }
  }
  return false;
}
bool setGestureByName(const char *name) {
  static const char *names[] = {"WINK_L", "WINK_R", "CONFUSED", "LAUGH", "NOD",
                                "SHAKE", "ROLL", "STARTLE", "PEEK", "DIVE", "LOOP",
                                "SIGH"};
  for (uint8_t index = 0; index < 12; ++index) {
    if (!strcmp(name, names[index])) {
      eyes.gesture(static_cast<Gesture>(min<uint8_t>(index, 10)));
      return true;
    }
  }
  return false;
}
void applyState(const char *state) {
  galleryMode = false;
  demoMode = false;
  eyes.setMood(moodFor(state));
  if (!strcmp(state, "THINKING")) { body.setBehavior(Behavior::ORBIT); rain.setMode(RainMode::FAST); }
  else if (!strcmp(state, "SPEAKING") || !strcmp(state, "LISTENING")) {
    body.setBehavior(Behavior::CENTER);
    rain.setMode(!strcmp(state, "SPEAKING") ? RainMode::PULSE : RainMode::NORMAL);
  } else if (!strcmp(state, "SLEEP")) { body.setBehavior(Behavior::SINK); rain.setMode(RainMode::CALM); }
  else { body.setBehavior(Behavior::WANDER); rain.setMode(RainMode::NORMAL); }
  eyes.gesture(Gesture::STARTLE);
}
void error(const char *text) { Serial.print("ERR:"); Serial.println(text); }
void command(char *value) {
  for (char *p = value; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
  if (!strcmp(value, "PING")) Serial.println("OK:PONG");
  else if (!strcmp(value, "SNAP")) { sendSnapshot(); }
  else if (!strcmp(value, "PAL")) { paletteOk(); }
  else if (!strcmp(value, "DEMO")) { demoMode = true; galleryMode = false; demoStarted = millis(); demoStage = 0; Serial.println("OK:DEMO"); }
  else if (!strcmp(value, "GALLERY")) { galleryMode = true; demoMode = false; Serial.println("OK:GALLERY"); }
  else if (!strcmp(value, "IDLE") || !strcmp(value, "LISTENING") ||
           !strcmp(value, "THINKING") || !strcmp(value, "SPEAKING") || !strcmp(value, "SLEEP")) {
    applyState(value); Serial.print("OK:"); Serial.println(value);
  } else if (!strncmp(value, "STATE ", 6)) {
    const char *state = value + 6;
    if (!strcmp(state, "ALERT")) {
      eyes.gesture(Gesture::STARTLE);
      eyes.setEmotion(Emotion::GLITCH, 100, 400);
    }
    applyState(state); Serial.print("OK:"); Serial.println(state);
  } else if (!strncmp(value, "FEEL ", 5)) {
    char name[16]; int intensity; unsigned hold;
    if (sscanf(value + 5, "%15s %d %u", name, &intensity, &hold) == 3 &&
        intensity >= 0 && intensity <= 100 &&
        setEmotionByName(name, static_cast<uint8_t>(intensity), hold)) {
      Serial.print("OK:FEEL "); Serial.println(name);
    } else error("BAD_FEEL");
  } else if (!strncmp(value, "MOOD ", 5)) {
    applyState(value + 5); Serial.print("OK:"); Serial.println(value + 5);
  } else if (!strncmp(value, "GOTO ", 5)) {
    int x, y;
    if (sscanf(value + 5, "%d %d", &x, &y) == 2) { body.goTo(x, y); Serial.println("OK:GOTO"); }
    else error("BAD_GOTO");
  } else if (!strncmp(value, "LOOK ", 5)) {
    int x, y;
    if (sscanf(value + 5, "%d %d", &x, &y) == 2) { eyes.lookAt(x, y); body.lookAt(x, y); Serial.println("OK:LOOK"); }
    else error("BAD_LOOK");
  } else if (!strncmp(value, "BEHAVIOR ", 9)) {
    const char *name = value + 9;
    if (!strcmp(name, "WANDER")) body.setBehavior(Behavior::WANDER);
    else if (!strcmp(name, "ARRIVE")) body.setBehavior(Behavior::ARRIVE);
    else if (!strcmp(name, "ORBIT")) body.setBehavior(Behavior::ORBIT);
    else if (!strcmp(name, "CENTER")) body.setBehavior(Behavior::CENTER);
    else if (!strcmp(name, "SINK")) body.setBehavior(Behavior::SINK);
    else { error("BAD_BEHAVIOR"); return; }
    Serial.println("OK:BEHAVIOR");
  } else if (!strncmp(value, "GESTURE ", 8)) {
    if (setGestureByName(value + 8)) { Serial.print("OK:GESTURE "); Serial.println(value + 8); }
    else error("BAD_GESTURE");
  }
  else if (*value) error("UNKNOWN_COMMAND");
  ++commands;
}
void readSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') { line[lineLength] = '\0'; command(line); lineLength = 0; }
    else if (lineLength < sizeof(line) - 1) line[lineLength++] = c;
    else { lineLength = 0; error("COMMAND_TOO_LONG"); }
  }
}
void report(uint32_t now) {
  if (now - lastReport < 2000) return;
  Serial.printf("PERF FPS=%.1f rain_ms=%.2f body_ms=%.2f eyes_ms=%.2f push_ms=%.2f heap=%u largest=%u SPI=%u commands=%lu\n",
                frames * 1000.0f / (now - lastReport), rainUs / 2000.0f, bodyUs / 2000.0f,
                eyesUs / 2000.0f, pushUs / 2000.0f, ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), SPI_FREQUENCY / 1000000,
                static_cast<unsigned long>(commands));
  frames = rainUs = bodyUs = eyesUs = pushUs = 0; lastReport = now;
}
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  tft.init(); tft.setRotation(1); sprite.setColorDepth(4);
  if (!sprite.createSprite(320, 240)) { Serial.println("ERR:SPRITE_ALLOC"); while (true) delay(100); }
  sprite.createPalette(PHOS_PALETTE, 16);
  paletteOk();
  const uint32_t seed = static_cast<uint32_t>(ESP.getEfuseMac());
  body.begin(seed); eyes.begin(seed); rain.begin(seed);
  lastFrame = lastReport = millis();
}

void loop() {
  readSerial();
  const uint32_t now = millis();
  const bool boot = digitalRead(0);
  if (lastBoot == HIGH && boot == LOW) applyState("THINKING");
  lastBoot = boot;
  const float dt = min(0.08f, (now - lastFrame) / 1000.0f);
  if (dt < 0.02f) return;
  lastFrame = now; sprite.fillSprite(0);
  if (galleryMode) {
    static const char *names[] = {"NEUTRAL", "HAPPY", "JOY", "LOVE", "SAD",
                                  "ANGRY", "SURPRISED", "SCARED", "SKEPTICAL",
                                  "CONFUSED", "CURIOUS", "FOCUSED", "BORED",
                                  "SHY", "PROUD", "DETERMINED", "GLITCH"};
    for (uint8_t index = 0; index < 17; ++index) {
      const int16_t x = 45 + (index % 4) * 76;
      const int16_t y = 42 + (index / 4) * 48;
      eyes.drawGalleryCell(sprite, x, y, static_cast<Emotion>(index));
      sprite.setTextColor(5, 0);
      sprite.drawString(names[index], x - 27, y + 19, 5);
    }
    sprite.pushSprite(0, 0);
    report(now);
    return;
  }
  if (demoMode && now - demoStarted > 4000) {
    demoStarted = now;
    static const char *states[] = {"IDLE", "LISTENING", "THINKING", "SPEAKING", "SLEEP"};
    static const char *emotions[] = {"HAPPY", "JOY", "LOVE", "SAD", "ANGRY",
                                     "SURPRISED", "SCARED", "SKEPTICAL", "CONFUSED",
                                     "CURIOUS", "FOCUSED", "BORED", "SHY", "PROUD",
                                     "DETERMINED", "GLITCH"};
    if (demoStage < 5) {
      applyState(states[demoStage]);
      demoMode = true;
      Serial.print("DEMO:STATE "); Serial.println(states[demoStage]);
    } else if (demoStage < 21) {
      setEmotionByName(emotions[demoStage - 5], 100, 3000);
      Serial.print("DEMO:FEEL "); Serial.println(emotions[demoStage - 5]);
    } else {
      static const char *gestures[] = {"WINK_L", "WINK_R", "CONFUSED", "LAUGH",
                                       "NOD", "SHAKE", "ROLL", "STARTLE", "PEEK",
                                       "DIVE", "LOOP"};
      const uint8_t gesture = demoStage - 21;
      if (gesture < 11) {
        setGestureByName(gestures[gesture]);
        Serial.print("DEMO:GESTURE "); Serial.println(gestures[gesture]);
      } else {
        demoMode = false;
      }
    }
    ++demoStage;
  }
  uint32_t started = micros(); body.update(dt); bodyUs += micros() - started;
  started = micros(); rain.update(dt, body.state()); rain.draw(sprite); rainUs += micros() - started;
  started = micros(); eyes.update(dt); eyes.draw(sprite, body.state()); eyesUs += micros() - started;
  started = micros(); sprite.pushSprite(0, 0); pushUs += micros() - started;
  ++frames; report(now);
}
