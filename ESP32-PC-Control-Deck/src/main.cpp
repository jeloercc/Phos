#ifndef RUN_CANDIDATE
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <math.h>

namespace {
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
#ifdef PHOS_INLAND
constexpr int BACKLIGHT_PIN = 21;
constexpr int TOUCH_SCK_PIN = 25;
constexpr int TOUCH_MOSI_PIN = 32;
constexpr int TOUCH_MISO_PIN = 39;
constexpr int TOUCH_CS_PIN = 33;
constexpr int TOUCH_IRQ_PIN = 36;
#else
constexpr int BACKLIGHT_PIN = 27;
#endif
constexpr uint32_t STALE_AFTER_MS = 15000;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 300;
constexpr char PROTOCOL_PREFIX[] = "DD:";
constexpr size_t PROTOCOL_PREFIX_LEN = 3;
constexpr uint8_t PROTOCOL_VERSION = 3;

constexpr uint16_t C_BG = 0x0841;
constexpr uint16_t C_PANEL = 0x1082;
constexpr uint16_t C_PANEL_2 = 0x18C3;
constexpr uint16_t C_TEXT = 0xFFFF;
constexpr uint16_t C_MUTED = 0xAD55;
constexpr uint16_t C_ACCENT = 0x07FF;
constexpr uint16_t C_GOOD = 0x07E0;
constexpr uint16_t C_WARN = 0xFFE0;
constexpr uint16_t C_BAD = 0xF800;
constexpr uint16_t C_BAR_BG = 0x2945;
constexpr uint16_t MATRIX_BLACK = 0x0000;
constexpr uint16_t MATRIX_HEAD = 0x07E0;
constexpr uint16_t MATRIX_TRAIL_BRIGHT = 0x03E0;
constexpr uint16_t MATRIX_TRAIL_DIM = 0x01E0;
constexpr uint16_t MATRIX_ALERT = 0xFFFF;
constexpr int MATRIX_AREA_BOTTOM = 239;
constexpr uint8_t MATRIX_DROP_COUNT = 32;
constexpr char MATRIX_GLYPHS[] = "0123456789ABCDEF";

TFT_eSPI tft;
Preferences prefs;

class PartialSprite {
 public:
  explicit PartialSprite(TFT_eSPI *display) : canvas(display) {}

  void begin(int16_t width, int16_t height) {
    if (canvas.createSprite(width, height) == nullptr) {
      abort();
    }
  }

  TFT_eSprite canvas;
};

PartialSprite headerSprite(&tft);
PartialSprite metricBarSprite(&tft);
PartialSprite metricValueSprite(&tft);
PartialSprite infoBoxSprite(&tft);
PartialSprite matrixColumnSprite(&tft);
PartialSprite creatureCoreSprite(&tft);


enum MoodState : uint8_t { IDLE, THINKING, SPEAKING, SLEEP, ALERT };
MoodState mood = IDLE;
uint32_t alertStartedMs = 0;

struct MatrixDrop {
  int16_t x;
  float y;
  float speed;
  uint8_t trailLength;
};

MatrixDrop matrixDrops[MATRIX_DROP_COUNT];
uint32_t lastMatrixUpdateMs = 0;
uint32_t matrixFrame = 0;

struct Stats {
  float cpu = NAN;
  float ram = NAN;
  float gpu = NAN;
  float vram = NAN;
  float cpuTemp = NAN;
  float gpuTemp = NAN;
  float systemTemp = NAN;
  float diskTemp = NAN;
  float ramUsedGB = NAN;
  float ramTotalGB = NAN;
  float vramUsedGB = NAN;
  float vramTotalGB = NAN;
  float diskUsed = NAN;
  float diskReadMBs = NAN;
  float diskWriteMBs = NAN;
  float netDownMbps = NAN;
  float netUpMbps = NAN;
  uint32_t clockSecondsAtSync = 0;
  uint32_t clockSyncMs = 0;
  uint32_t lastUpdateMs = 0;
  bool clockValid = false;
  bool everUpdated = false;
} stats;

String macroLabels[6] = {
    "CHATGPT", "OLLAMA", "VOICE SERVER", "TERMINAL", "TASK MANAGER", "LOCK PC"};
String serialLine;
uint32_t lastTouchMs = 0;
bool fullRedrawRequested = true;
bool statsDirty = true;

#ifdef PHOS_INLAND
struct TouchCalibration {
  uint16_t rawLeft;
  uint16_t rawRight;
  uint16_t rawTop;
  uint16_t rawBottom;
  bool xUsesRawX;
  bool xInverted;
  bool yUsesRawX;
  bool yInverted;
};

TouchCalibration touchCalibration{};

SPIClass touchSPI(VSPI);

uint16_t touchReadAxis(uint8_t command) {
  digitalWrite(TOUCH_CS_PIN, LOW);
  touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  touchSPI.transfer(command);
  const uint16_t value = touchSPI.transfer16(0x0000) >> 3;
  touchSPI.endTransaction();
  digitalWrite(TOUCH_CS_PIN, HIGH);
  return value;
}

bool touchReadRaw(uint16_t &rawX, uint16_t &rawY) {
  if (digitalRead(TOUCH_IRQ_PIN) != LOW) return false;
  uint32_t sumX = 0;
  uint32_t sumY = 0;
  for (int sample = 0; sample < 3; ++sample) {
    sumX += touchReadAxis(0xD0);
    sumY += touchReadAxis(0x90);
  }
  rawX = static_cast<uint16_t>(sumX / 3);
  rawY = static_cast<uint16_t>(sumY / 3);
  return rawX > 100 && rawX < 4095 && rawY > 100 && rawY < 4095;
}

bool touchRead(uint16_t &x, uint16_t &y) {
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  if (!touchReadRaw(rawX, rawY)) return false;

  const uint16_t xRaw = touchCalibration.xUsesRawX ? rawX : rawY;
  const uint16_t yRaw = touchCalibration.yUsesRawX ? rawX : rawY;
  const int xValue = map(xRaw, touchCalibration.rawLeft,
                         touchCalibration.rawRight,
                         touchCalibration.xInverted ? SCREEN_W - 1 : 0,
                         touchCalibration.xInverted ? 0 : SCREEN_W - 1);
  const int yValue = map(yRaw, touchCalibration.rawTop,
                         touchCalibration.rawBottom,
                         touchCalibration.yInverted ? SCREEN_H - 1 : 0,
                         touchCalibration.yInverted ? 0 : SCREEN_H - 1);
  x = static_cast<uint16_t>(constrain(xValue, 0, SCREEN_W - 1));
  y = static_cast<uint16_t>(constrain(yValue, 0, SCREEN_H - 1));
  return true;
}
#endif

float clampPct(float value) {
  if (isnan(value)) return 0.0f;
  if (value < 0.0f) return 0.0f;
  if (value > 100.0f) return 100.0f;
  return value;
}

String fmtValue(float value, unsigned int decimals = 0) {
  if (isnan(value)) return "--";
  return String(value, decimals);
}

uint16_t usageColor(float value) {
  if (isnan(value)) return C_MUTED;
  if (value < 70.0f) return C_GOOD;
  if (value < 90.0f) return C_WARN;
  return C_BAD;
}

bool parseClockText(const char *text, uint32_t &secondsOfDay) {
  if (!text || text[0] < '0' || text[0] > '9' || text[1] < '0' ||
      text[1] > '9' || text[2] != ':' || text[3] < '0' || text[3] > '9' ||
      text[4] < '0' || text[4] > '9') {
    return false;
  }

  const int hour = (text[0] - '0') * 10 + (text[1] - '0');
  const int minute = (text[3] - '0') * 10 + (text[4] - '0');
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;

  secondsOfDay = static_cast<uint32_t>(hour * 3600 + minute * 60);
  return true;
}

String currentClockText() {
  if (!stats.clockValid) return "--:--";

  const uint32_t elapsedSeconds = (millis() - stats.clockSyncMs) / 1000UL;
  const uint32_t secondsOfDay =
      (stats.clockSecondsAtSync + elapsedSeconds) % 86400UL;
  const uint32_t hour = secondsOfDay / 3600UL;
  const uint32_t minute = (secondsOfDay % 3600UL) / 60UL;

  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
           static_cast<unsigned long>(hour),
           static_cast<unsigned long>(minute));
  return String(buffer);
}

String memoryText(float usedGB, float totalGB) {
  if (isnan(usedGB) || isnan(totalGB) || totalGB <= 0.0f) return "MEM N/A";
  const unsigned int totalDecimals = totalGB >= 10.0f ? 0U : 1U;
  return String(usedGB, 1) + "/" + String(totalGB, totalDecimals) + "G";
}


void drawHeaderStatic(const String &title) {
  tft.fillRect(0, 0, SCREEN_W, 28, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.drawString(title, 8, 14, 2);
}

void drawHeaderDynamic() {
  const bool stale =
      stats.everUpdated && (millis() - stats.lastUpdateMs > STALE_AFTER_MS);
  uint16_t statusColor = C_MUTED;
  if (stats.everUpdated) statusColor = stale ? C_WARN : C_GOOD;

  headerSprite.canvas.fillSprite(C_PANEL);
  headerSprite.canvas.fillCircle(10, 14, 4, statusColor);
  headerSprite.canvas.setTextDatum(ML_DATUM);
  headerSprite.canvas.setTextColor(C_MUTED, C_PANEL);
  headerSprite.canvas.drawString("USB", 19, 14, 1);
  headerSprite.canvas.setTextDatum(MR_DATUM);
  headerSprite.canvas.setTextColor(stats.clockValid ? C_TEXT : C_WARN, C_PANEL);
  headerSprite.canvas.drawString(currentClockText(), 146, 14, 2);
  headerSprite.canvas.pushSprite(168, 0);
}

void drawMatrixColumn(const MatrixDrop &drop, int16_t top) {
  constexpr int16_t glyphHeight = 8;
  constexpr int16_t spriteHeight = 132;

  matrixColumnSprite.canvas.fillSprite(MATRIX_BLACK);
  for (uint8_t index = 0; index < drop.trailLength; ++index) {
    const int16_t y = static_cast<int16_t>(drop.y) -
                      index * glyphHeight - top;
    if (y < 0 || y + glyphHeight > spriteHeight ||
        top + y > MATRIX_AREA_BOTTOM) {
      continue;
    }

    uint16_t color = index == 0
                         ? MATRIX_HEAD
                         : (index == 1 ? MATRIX_TRAIL_BRIGHT : MATRIX_TRAIL_DIM);
    if (mood == SLEEP) {
      color = MATRIX_TRAIL_DIM;
    }
    matrixColumnSprite.canvas.setTextDatum(TL_DATUM);
    matrixColumnSprite.canvas.setTextColor(color, MATRIX_BLACK);
    const uint8_t glyphIndex =
        static_cast<uint8_t>((matrixFrame + index + drop.x) %
                             (sizeof(MATRIX_GLYPHS) - 1));
    char glyph[2] = {MATRIX_GLYPHS[glyphIndex], '\0'};
    matrixColumnSprite.canvas.drawString(glyph, 1, y, 1);
  }
  matrixColumnSprite.canvas.pushSprite(drop.x, top);
}

void initializeMatrixDrops() {
  for (uint8_t index = 0; index < MATRIX_DROP_COUNT; ++index) {
    MatrixDrop &drop = matrixDrops[index];
    drop.x = 2 + index * 10;
    drop.y = -static_cast<float>((index * 37) % 220);
    drop.speed = 0.04f + static_cast<float>((index * 7) % 10) * 0.01f;
    drop.trailLength = 6 + (index * 5) % 9;
  }
}

void updateMatrix(uint32_t now) {
  if (now - lastMatrixUpdateMs < 33) return;
  const float elapsed = min(120.0f, static_cast<float>(now - lastMatrixUpdateMs));
  lastMatrixUpdateMs = now;
  ++matrixFrame;
  const float speedMultiplier =
      mood == THINKING ? 4.0f : (mood == SLEEP ? 0.04f : 1.0f);

  for (MatrixDrop &drop : matrixDrops) {
    const int16_t oldTop = constrain(
        static_cast<int16_t>(drop.y) - drop.trailLength * 8, 0,
        MATRIX_AREA_BOTTOM - 132 + 1);
    bool wrapped = false;
    drop.y += drop.speed * speedMultiplier * elapsed;
    if (drop.y - drop.trailLength * 8 > MATRIX_AREA_BOTTOM) {
      drop.y = -static_cast<float>((drop.x * 3) % 160);
      wrapped = true;
    }
    const int16_t newTop = constrain(
        static_cast<int16_t>(drop.y) - drop.trailLength * 8, 0,
        MATRIX_AREA_BOTTOM - 132 + 1);
    if (wrapped) {
      tft.fillRect(drop.x, oldTop, 14, 132, MATRIX_BLACK);
    } else if (newTop > oldTop) {
      tft.fillRect(drop.x, oldTop, 14, newTop - oldTop, MATRIX_BLACK);
    }
    drawMatrixColumn(drop, newTop);
  }
}

uint16_t lerp565(uint16_t from, uint16_t to, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const uint8_t fromR = (from >> 11) & 0x1F;
  const uint8_t fromG = (from >> 5) & 0x3F;
  const uint8_t fromB = from & 0x1F;
  const uint8_t toR = (to >> 11) & 0x1F;
  const uint8_t toG = (to >> 5) & 0x3F;
  const uint8_t toB = to & 0x1F;
  const uint8_t red = fromR + static_cast<uint8_t>((toR - fromR) * amount);
  const uint8_t green =
      fromG + static_cast<uint8_t>((toG - fromG) * amount);
  const uint8_t blue = fromB + static_cast<uint8_t>((toB - fromB) * amount);
  return (static_cast<uint16_t>(red) << 11) |
         (static_cast<uint16_t>(green) << 5) | blue;
}

void drawCreatureCore(uint32_t now) {
  constexpr int16_t coreX = 110;
  constexpr int16_t coreY = 62;
  constexpr int16_t coreWidth = 100;
  constexpr int16_t coreHeight = 86;
  float pulseTime = now / 300.0f;
  float breath = (sin(pulseTime) + 1.0f) * 0.5f;
  if (mood == THINKING) {
    pulseTime = now / 85.0f;
    breath = (sin(pulseTime) + sin(now / 47.0f) * 0.35f + 1.35f) * 0.5f;
    breath = constrain(breath, 0.0f, 1.0f);
  } else if (mood == SPEAKING) {
    pulseTime = now / 72.0f;
    breath = (sin(pulseTime) + 1.0f) * 0.5f;
  } else if (mood == SLEEP) {
    breath = 0.12f;
  }
  const uint16_t coreColor =
      mood == ALERT ? MATRIX_ALERT : lerp565(MATRIX_TRAIL_DIM, MATRIX_HEAD, breath);

  creatureCoreSprite.canvas.fillSprite(MATRIX_BLACK);
  creatureCoreSprite.canvas.setTextDatum(MC_DATUM);
  creatureCoreSprite.canvas.setTextColor(coreColor, MATRIX_BLACK);

  const int16_t pulse =
      static_cast<int16_t>(breath * (mood == SPEAKING ? 8.0f : 3.0f));
  const int16_t centerX = coreWidth / 2;
  const int16_t centerY = coreHeight / 2 + pulse;
  creatureCoreSprite.canvas.drawString(mood == SPEAKING ? "< * * >" : "[  *  ]",
                                        centerX, centerY - 24, 2);
  creatureCoreSprite.canvas.drawString(mood == ALERT ? "< 0 1 >" : "< 0 1 >",
                                        centerX, centerY - 4, 2);
  creatureCoreSprite.canvas.drawString(mood == SPEAKING ? "[ *** ]" : "[  *  ]",
                                        centerX, centerY + 16, 2);
  creatureCoreSprite.canvas.drawCircle(centerX, centerY + 1,
                                       (mood == ALERT ? 31 : 27) + pulse,
                                       coreColor);
  creatureCoreSprite.canvas.pushSprite(coreX, coreY);
}


void handleTouch() {
  if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return;

  uint16_t x = 0;
  uint16_t y = 0;
#ifdef PHOS_INLAND
  if (!touchRead(x, y)) return;
#else
  if (!tft.getTouch(&x, &y)) return;
#endif
  lastTouchMs = millis();

  // Any touch on screen triggers ALERT
  mood = ALERT;
  alertStartedMs = millis();
}

float readFloat(JsonVariantConst value, float fallback = NAN) {
  if (value.isNull()) return fallback;
  return value.as<float>();
}

void syncClock(JsonDocument &doc) {
  if (!doc["clock_seconds"].isNull()) {
    const int32_t seconds = doc["clock_seconds"].as<int32_t>();
    if (seconds >= 0 && seconds < 86400) {
      stats.clockSecondsAtSync = static_cast<uint32_t>(seconds);
      stats.clockSyncMs = millis();
      stats.clockValid = true;
      return;
    }
  }

  const char *clockText = doc["clock"] | nullptr;
  uint32_t secondsOfDay = 0;
  if (parseClockText(clockText, secondsOfDay)) {
    stats.clockSecondsAtSync = secondsOfDay;
    stats.clockSyncMs = millis();
    stats.clockValid = true;
  }
}

void handleStats(JsonDocument &doc) {
  stats.cpu = readFloat(doc["cpu"]);
  stats.ram = readFloat(doc["ram"]);
  stats.gpu = readFloat(doc["gpu"]);
  stats.vram = readFloat(doc["vram"]);
  stats.cpuTemp = readFloat(doc["cpu_temp"]);
  stats.gpuTemp = readFloat(doc["gpu_temp"]);
  stats.systemTemp = readFloat(doc["system_temp"]);
  stats.diskTemp = readFloat(doc["disk_temp"]);
  stats.ramUsedGB = readFloat(doc["ram_used_gb"]);
  stats.ramTotalGB = readFloat(doc["ram_total_gb"]);
  stats.vramUsedGB = readFloat(doc["vram_used_gb"]);
  stats.vramTotalGB = readFloat(doc["vram_total_gb"]);
  stats.diskUsed = readFloat(doc["disk_used"]);
  stats.diskReadMBs = readFloat(doc["disk_read_mbs"]);
  stats.diskWriteMBs = readFloat(doc["disk_write_mbs"]);
  stats.netDownMbps = readFloat(doc["net_down_mbps"]);
  stats.netUpMbps = readFloat(doc["net_up_mbps"]);
  syncClock(doc);

  stats.lastUpdateMs = millis();
  stats.everUpdated = true;
  statsDirty = true;
}

void handleConfig(JsonDocument &doc) {
  JsonArrayConst macros = doc["macros"].as<JsonArrayConst>();
  bool labelsChanged = false;

  for (JsonObjectConst item : macros) {
    const int id = item["id"] | 0;
    const char *label = item["label"] | "";
    if (id < 1 || id > 6 || !label || label[0] == '\0') continue;

    String next(label);
    if (next.length() > 22) next = next.substring(0, 22);
    if (macroLabels[id - 1] != next) {
      macroLabels[id - 1] = next;
      labelsChanged = true;
    }
  }


}

MoodState parseMoodState(const char *state) {
  if (!state) return IDLE;
  String normalized(state);
  normalized.toUpperCase();
  if (normalized == "THINKING") return THINKING;
  if (normalized == "SPEAKING") return SPEAKING;
  if (normalized == "SLEEP") return SLEEP;
  if (normalized == "ALERT") return ALERT;
  return IDLE;
}







void drawCreatureFull() {
  tft.fillScreen(MATRIX_BLACK);
}

void renderFull() {
  drawCreatureFull();
  fullRedrawRequested = false;
}

void sendFrame(JsonDocument &doc) {
  Serial.print(PROTOCOL_PREFIX);
  serializeJson(doc, Serial);
  Serial.println();
}

void sendSimpleFrame(const char *type) {
  JsonDocument doc;
  doc["type"] = type;
  sendFrame(doc);
}

void sendMacro(uint8_t id) {
  JsonDocument doc;
  doc["type"] = "macro";
  doc["id"] = id;
  sendFrame(doc);
}
void processSerialLine(const String &line) {
  bool framed = false;
  String payload;
  if (line.startsWith(PROTOCOL_PREFIX)) {
    if (line.length() <= PROTOCOL_PREFIX_LEN) return;
    payload = line.substring(PROTOCOL_PREFIX_LEN);
    framed = true;
  } else if (line.startsWith("{")) {
    payload = line;
  } else {
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    if (framed) {
      JsonDocument errorDoc;
      errorDoc["type"] = "error";
      errorDoc["code"] = "invalid_json";
      errorDoc["detail"] = error.c_str();
      sendFrame(errorDoc);
    }
    return;
  }

  const char *type = doc["type"] | "";
  if (strcmp(type, "stats") == 0) {
    handleStats(doc);
  } else if (strcmp(type, "config") == 0) {
    handleConfig(doc);
  } else if (strcmp(type, "mood") == 0) {
    const char *state = doc["state"] | "IDLE";
    mood = parseMoodState(state);
    if (mood == ALERT) alertStartedMs = millis();
  } else if (strcmp(type, "ping") == 0) {
    sendSimpleFrame("pong");
  } else if (strcmp(type, "recalibrate") == 0) {
    prefs.remove("touchcal");
    sendSimpleFrame("rebooting");
    delay(100);
    ESP.restart();
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      serialLine.trim();
      processSerialLine(serialLine);
      serialLine = "";
    } else if (c != '\r') {
      if (serialLine.length() < 2048) {
        serialLine += c;
      } else {
        serialLine = "";
      }
    }
  }
}

void setupTouchCalibration() {
#ifdef PHOS_INLAND
  prefs.begin("deskdeck", false);
  const size_t length = prefs.getBytesLength("touchcal");
  pinMode(TOUCH_CS_PIN, OUTPUT);
  pinMode(TOUCH_IRQ_PIN, INPUT_PULLUP);
  touchSPI.begin(TOUCH_SCK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  digitalWrite(TOUCH_CS_PIN, HIGH);

  if (length == sizeof(touchCalibration)) {
    prefs.getBytes("touchcal", &touchCalibration, sizeof(touchCalibration));
    return;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("FIRST START", SCREEN_W / 2, 28, 4);
  tft.drawString("Touch calibration", SCREEN_W / 2, 66, 2);
  tft.drawString("Touch each marked corner", SCREEN_W / 2, 92, 2);
  delay(1200);

  const int points[4][2] = {{15, 15}, {SCREEN_W - 15, 15},
                             {SCREEN_W - 15, SCREEN_H - 15},
                             {15, SCREEN_H - 15}};
  uint16_t rawX[4] = {0};
  uint16_t rawY[4] = {0};
  for (int point = 0; point < 4; ++point) {
    tft.fillScreen(TFT_BLACK);
    tft.drawCircle(points[point][0], points[point][1], 6, TFT_MAGENTA);
    tft.drawString("Touch the marker", SCREEN_W / 2, SCREEN_H / 2, 2);
    while (digitalRead(TOUCH_IRQ_PIN) != LOW) delay(10);
    while (!touchReadRaw(rawX[point], rawY[point])) delay(10);
    while (digitalRead(TOUCH_IRQ_PIN) == LOW) delay(10);
  }

  const uint16_t horizontalX =
      abs(static_cast<int>(rawX[1]) - static_cast<int>(rawX[0]));
  const uint16_t horizontalY =
      abs(static_cast<int>(rawY[1]) - static_cast<int>(rawY[0]));
  touchCalibration.xUsesRawX = horizontalX >= horizontalY;
  touchCalibration.yUsesRawX = !touchCalibration.xUsesRawX;
  const uint16_t xFirst =
      touchCalibration.xUsesRawX ? rawX[0] : rawY[0];
  const uint16_t xSecond =
      touchCalibration.xUsesRawX ? rawX[1] : rawY[1];
  const uint16_t yFirst =
      touchCalibration.yUsesRawX ? rawX[0] : rawY[0];
  const uint16_t yLast =
      touchCalibration.yUsesRawX ? rawX[3] : rawY[3];
  touchCalibration.rawLeft = min(xFirst, xSecond);
  touchCalibration.rawRight = max(xFirst, xSecond);
  touchCalibration.rawTop = min(yFirst, yLast);
  touchCalibration.rawBottom = max(yFirst, yLast);
  touchCalibration.xInverted = xFirst > xSecond;
  touchCalibration.yInverted = yFirst > yLast;
  prefs.putBytes("touchcal", &touchCalibration, sizeof(touchCalibration));
  return;
#else
  uint16_t calData[5] = {0};
  prefs.begin("deskdeck", false);
  const size_t length = prefs.getBytesLength("touchcal");

  if (length == sizeof(calData)) {
    prefs.getBytes("touchcal", calData, sizeof(calData));
    tft.setTouch(calData);
    return;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("FIRST START", SCREEN_W / 2, 28, 4);
  tft.drawString("Touch calibration", SCREEN_W / 2, 66, 2);
  tft.drawString("Use the stylus and touch", SCREEN_W / 2, 92, 2);
  tft.drawString("the marked corners.", SCREEN_W / 2, 114, 2);
  delay(1200);

  tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
  tft.setTouch(calData);
  prefs.putBytes("touchcal", calData, sizeof(calData));
#endif
}
}  // namespace


void setup() {
  Serial.begin(SERIAL_BAUD);
  serialLine.reserve(2048);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
    matrixColumnSprite.begin(14, 132);
  creatureCoreSprite.begin(100, 86);
  initializeMatrixDrops();
  setupTouchCalibration();
  renderFull();

  JsonDocument hello;
  hello["type"] = "hello";
  hello["device"] = "ESP32 PC Control Deck";
  hello["firmware"] = "0.3.3";
  hello["protocol"] = PROTOCOL_VERSION;
  sendFrame(hello);
}


void loop() {
  pollSerial();
  handleTouch();

  if (fullRedrawRequested) {
    renderFull();
  }

  const uint32_t now = millis();
  if (mood == ALERT && now - alertStartedMs >= 2000) {
    mood = IDLE;
  }
  
  updateMatrix(now);
  drawCreatureCore(now);

  static uint32_t frameCount = 0;
  static uint32_t lastFpsTime = 0;
  frameCount++;
  if (now - lastFpsTime >= 1000) {
    Serial.printf("FPS: %lu, Free Heap: %u, Max Block: %u\n", 
                  (unsigned long)frameCount, 
                  ESP.getFreeHeap(), 
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    frameCount = 0;
    lastFpsTime = now;
  }

  static uint32_t lastYield = 0;
  if (now - lastYield >= 1) {
    lastYield = now;
    yield();
  }
}
#endif
