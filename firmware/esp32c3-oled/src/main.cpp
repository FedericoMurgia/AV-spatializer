#include <Arduino.h>
#include <FastLED.h>
#include <U8g2lib.h>
#include <Wire.h>

namespace {

// Matrix size.
constexpr uint8_t  MATRIX_WIDTH  = 8;
constexpr uint8_t  MATRIX_HEIGHT = 8;
constexpr uint16_t LED_COUNT     = MATRIX_WIDTH * MATRIX_HEIGHT;

// OLED — 01space 0.42" SSD1306, 72x40, I2C on GPIO5/GPIO6.
constexpr uint8_t OLED_SDA = 5;
constexpr uint8_t OLED_SCL = 6;
constexpr uint32_t SCREEN_INTERVAL_MS = 2000;

// Pins — ESP32-C3 Egg board.
constexpr uint8_t LED_PIN             = 7;
constexpr uint8_t JOYSTICK_X_PIN     = 0;
constexpr uint8_t JOYSTICK_Y_PIN     = 1;
constexpr uint8_t JOYSTICK_BUTTON_PIN = 8;
constexpr uint8_t POT_PIN            = 2;
constexpr uint8_t SLIDER1_PIN        = 3;
constexpr uint8_t SLIDER2_PIN        = 4;

// LED brightness.
constexpr uint8_t BRIGHTNESS = 40;

// Target ~50 fps.
constexpr uint32_t FRAME_INTERVAL_MS = 20;

// Ignore tiny joystick movement around the center.
constexpr int DEADZONE = 250;

// Minimum stable time before a button press is accepted.
constexpr uint32_t DEBOUNCE_MS = 50;

// Full 12-bit ADC range.
constexpr int ADC_MAX = 4095;

// Symmetric deflection range around center.
// Kept below physical ADC max so full deflection reliably clears the last position bin.
constexpr int AXIS_HALF_RANGE = 1500;

// Strobe frequency range controlled by s2 slider.
constexpr float STROBE_HZ_MIN = 1.0f;
constexpr float STROBE_HZ_MAX = 20.0f;

U8G2_SSD1306_72X40_ER_F_SW_I2C u8g2(U8G2_R2, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

CRGB leds[LED_COUNT];

int centerX = 2048, centerY = 2048;
int axisMinX = 0, axisMaxX = ADC_MAX;
int axisMinY = 0, axisMaxY = ADC_MAX;

constexpr float EMA_ALPHA     = 0.4f;
constexpr float EMA_ALPHA_POT = 0.8f;
float emaX   = 2048.0f;
float emaY   = 2048.0f;
float emaPot = 2048.0f;
float emaSl1 = 2048.0f;
float emaSl2 = 2048.0f;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastDebounceTime = 0;

// 0=fade  1=white  2=red  3=green  4=blue  5=strobe white  6=strobe fade  7=off
uint8_t colorMode = 0;
constexpr uint8_t COLOR_MODES = 8;

uint8_t fadeHue = 0;
bool displayWasEnabled = true;
int lastBx = -1, lastBy = -1;
bool currentStrobeOn = false;

uint16_t xyToIndex(uint8_t x, uint8_t y) {
  return static_cast<uint16_t>(y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x));
}

// All analog pins on ADC1 — no dummy read needed, just average for noise.
int smoothAnalogRead(uint8_t pin, uint8_t samples = 4) {
  long sum = 0;
  for (uint8_t i = 0; i < samples; i++) sum += analogRead(pin);
  return constrain(static_cast<int>(sum / samples), 0, ADC_MAX);
}

void calibrateJoystick() {
  delay(250);
  long sumX = 0, sumY = 0;
  constexpr uint8_t SAMPLES = 32;
  for (uint8_t i = 0; i < SAMPLES; i++) {
    sumX += analogRead(JOYSTICK_X_PIN);
    sumY += analogRead(JOYSTICK_Y_PIN);
    delay(2);
  }
  centerX = static_cast<int>(sumX / SAMPLES);
  centerY = static_cast<int>(sumY / SAMPLES);
  axisMinX = centerX - AXIS_HALF_RANGE;
  axisMaxX = centerX + AXIS_HALF_RANGE;
  axisMinY = centerY - AXIS_HALF_RANGE;
  axisMaxY = centerY + AXIS_HALF_RANGE;
  emaX = static_cast<float>(centerX);
  emaY = static_cast<float>(centerY);
}

uint8_t joystickToPosition(int rawValue, int centerValue,
                           int axisMin, int axisMax, uint8_t size) {
  const int delta = rawValue - centerValue;
  if (abs(delta) < DEADZONE) return size / 2;
  long position;
  if (delta < 0) {
    position = map(rawValue, centerValue - DEADZONE, axisMin, size / 2 - 1, 0);
  } else {
    position = map(rawValue, centerValue + DEADZONE, axisMax, size / 2, size - 1);
  }
  return static_cast<uint8_t>(constrain(position, 0, size - 1));
}

void updateButton() {
  const bool reading = digitalRead(JOYSTICK_BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounceTime = millis();
  lastButtonReading = reading;
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) colorMode = (colorMode + 1) % COLOR_MODES;
    }
  }
}

void drawPixelFromJoystick() {
  if (colorMode == 7) {
    if (displayWasEnabled) { FastLED.clear(true); displayWasEnabled = false; }
    return;
  }
  displayWasEnabled = true;

  const int rawX = smoothAnalogRead(JOYSTICK_X_PIN);
  const int rawY = smoothAnalogRead(JOYSTICK_Y_PIN);
  emaX = EMA_ALPHA * rawX + (1.0f - EMA_ALPHA) * emaX;
  emaY = EMA_ALPHA * rawY + (1.0f - EMA_ALPHA) * emaY;
  const int sX  = static_cast<int>(emaX);
  const int sY  = static_cast<int>(emaY);
  emaPot = EMA_ALPHA_POT * smoothAnalogRead(POT_PIN)    + (1.0f - EMA_ALPHA_POT) * emaPot;
  emaSl1 = EMA_ALPHA_POT * smoothAnalogRead(SLIDER1_PIN) + (1.0f - EMA_ALPHA_POT) * emaSl1;
  emaSl2 = EMA_ALPHA_POT * smoothAnalogRead(SLIDER2_PIN) + (1.0f - EMA_ALPHA_POT) * emaSl2;
  const int pot = static_cast<int>(emaPot);
  const int sl1 = static_cast<int>(emaSl1);
  const int sl2 = static_cast<int>(emaSl2);

  FastLED.setBrightness(map(pot, 0, 4095, 0, 255));
  FastLED.clear();

  const int blockSize = map(sl1, 0, 4095, 8, 1);
  // Odd sizes > 1 show as the next even size with a dimmed outer ring.
  const bool hasDimRing = (blockSize > 1 && blockSize % 2 == 1);
  const int displaySize = hasDimRing ? blockSize + 1 : blockSize;
  const uint8_t sizeX = MATRIX_WIDTH  + displaySize - 1;
  const uint8_t sizeY = MATRIX_HEIGHT + displaySize - 1;
  const uint8_t xPos  = joystickToPosition(sX, centerX, axisMinX, axisMaxX, sizeX);
  const uint8_t yBot  = joystickToPosition(sY, centerY, axisMinY, axisMaxY, sizeY);
  const int bx = (int)xPos - (displaySize - 1);
  const int by = (int)(MATRIX_HEIGHT - yBot) - 1;
  const uint8_t joyX = joystickToPosition(sX, centerX, axisMinX, axisMaxX, 9);
  const uint8_t joyY = joystickToPosition(sY, centerY, axisMinY, axisMaxY, 9);
  lastBx = (int)joyX - 4;
  lastBy = 4 - (int)joyY;

  static float    strobePhase    = 0.0f;
  static uint32_t lastStrobeTime = 0;
  const uint32_t  nowMs          = millis();
  const float     dt             = (lastStrobeTime > 0) ? (nowMs - lastStrobeTime) / 1000.0f
                                                        : FRAME_INTERVAL_MS / 1000.0f;
  lastStrobeTime = nowMs;
  const float strobeHz = STROBE_HZ_MIN + ((4095 - sl2) / 4095.0f) * (STROBE_HZ_MAX - STROBE_HZ_MIN);
  strobePhase += strobeHz * dt;
  if (strobePhase >= 1.0f) strobePhase -= 1.0f;
  const bool strobeOn = strobePhase < 0.5f;
  currentStrobeOn = strobeOn;

  if (colorMode == 0) fadeHue++;
  if (colorMode == 6) fadeHue++;

  CRGB color;
  switch (colorMode) {
    case 0: color = CHSV(fadeHue, 255, 255);                      break;
    case 1: color = CRGB::White;                                   break;
    case 2: color = CRGB::Red;                                     break;
    case 3: color = CRGB::Green;                                   break;
    case 4: color = CRGB::Blue;                                    break;
    case 5: color = strobeOn ? CRGB::White : CRGB::Black;         break;
    case 6: color = strobeOn ? CRGB(CHSV(fadeHue, 255, 255))
                             : CRGB::Black;                        break;
    default: color = CRGB::Black;                                  break;
  }

  Serial.printf("sx-%d-sy-%d-x-%d-y-%d-p-%d-s1-%d-s2-%d-m-%d\n",
                sX, sY, joyX, joyY, pot, sl1, sl2, colorMode);

  CRGB dimColor = color;
  dimColor.nscale8(50);  // ~20% brightness for the outer ring on odd sizes

  for (int dy = 0; dy < displaySize; dy++) {
    for (int dx = 0; dx < displaySize; dx++) {
      const int px = bx + dx;
      const int py = by + dy;
      if (px >= 0 && px < MATRIX_WIDTH && py >= 0 && py < MATRIX_HEIGHT) {
        const bool isOuter = hasDimRing &&
                             (dx == 0 || dx == displaySize - 1 ||
                              dy == 0 || dy == displaySize - 1);
        leds[xyToIndex(px, py)] = isOuter ? dimColor : color;
      }
    }
  }

  FastLED.show();
}

void drawCentered(const char* str, uint8_t y) {
  u8g2.drawStr((72 - u8g2.getStrWidth(str)) / 2, y, str);
}

void drawIdleScreen(uint8_t page) {
  u8g2.clearBuffer();
  if (page == 0) {
    u8g2.setFont(u8g2_font_10x20_tf);
    drawCentered("AV", 20);
    u8g2.setFont(u8g2_font_6x12_tf);
    drawCentered("controller", 38);
  } else {
    u8g2.setFont(u8g2_font_6x12_tf);
    drawCentered("Studio", 10);
    drawCentered("Federico", 24);
    drawCentered("Murgia", 37);
  }
  u8g2.sendBuffer();
}

void updateScreen(int pot, int sl1, int sl2, bool strobeMode, bool strobeOn) {
  static uint32_t lastSwitch   = 0;
  static uint32_t lastActivity = 0;
  static uint32_t lastDraw     = 0;
  static uint8_t  page         = 0;
  static int      lastPot      = -1;
  static int      lastSl1      = -1;
  static int      lastSl2      = -1;
  static int      prevBx       = -99;
  static int      prevBy       = -99;
  static int      drawnBx      = -99;
  static int      drawnBy      = -99;
  constexpr int       THRESHOLD       = 40;
  constexpr uint32_t ACTIVITY_MS = 2000;
  constexpr uint32_t MIN_DRAW_MS = 200;

  const uint32_t now = millis();

  // During strobe: draw "STROBE" once on entry, then freeze completely.
  static bool strobeLabelShown = false;
  if (strobeMode) {
    if (!strobeLabelShown) {
      strobeLabelShown = true;
      lastSl2 = sl2; lastPot = pot; lastSl1 = sl1;  // sync so exit shows fresh values
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x12_tf);
      drawCentered("mode", 16);
      u8g2.setFont(u8g2_font_10x20_tf);
      drawCentered("STROBE", 38);
      u8g2.sendBuffer();
    }
    return;  // absolutely no further screen updates while in strobe
  }
  strobeLabelShown = false;  // reset so next strobe entry redraws

  // Normal (non-strobe) mode — check for significant changes.
  const char* label = nullptr;
  char valStr[16];

  if (abs(pot - lastPot) > THRESHOLD) {
    label = "Bright";
    snprintf(valStr, sizeof(valStr), "%d%%", map(pot, 0, 4095, 0, 100));
    lastPot = pot; lastActivity = now;
  } else if (abs(sl1 - lastSl1) > THRESHOLD) {
    label = "Size";
    snprintf(valStr, sizeof(valStr), "%d", map(sl1, 0, 4095, 8, 1));
    lastSl1 = sl1; lastActivity = now;
  } else if (abs(sl2 - lastSl2) > THRESHOLD) {
    label = "Strobe";
    snprintf(valStr, sizeof(valStr), "%.0fHz",
             STROBE_HZ_MIN + ((4095 - sl2) / 4095.0f) * (STROBE_HZ_MAX - STROBE_HZ_MIN));
    lastSl2 = sl2; lastActivity = now;
  } else if (lastBx != prevBx || lastBy != prevBy) {
    prevBx = lastBx; prevBy = lastBy;
    lastActivity = now;
    // don't draw yet — wait for joystick to settle
  } else if ((lastBx != drawnBx || lastBy != drawnBy) &&
             now - lastActivity > 150) {
    label = "X  Y";
    snprintf(valStr, sizeof(valStr), "%+d %+d", lastBx, lastBy);
    drawnBx = lastBx; drawnBy = lastBy;
  }

  if (label && (now - lastDraw >= MIN_DRAW_MS)) {
    lastDraw = now;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    drawCentered(label, 16);
    u8g2.setFont(u8g2_font_10x20_tf);
    drawCentered(valStr, 38);
    u8g2.sendBuffer();
    return;
  }

  // No recent activity — show idle alternating text.
  if (now - lastActivity < ACTIVITY_MS) return;

  if (now - lastSwitch >= SCREEN_INTERVAL_MS) {
    lastSwitch = now;
    lastDraw = now;
    page = 1 - page;
    drawIdleScreen(page);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr((72 - u8g2.getStrWidth("AV")) / 2, 20, "AV");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr((72 - u8g2.getStrWidth("controller")) / 2, 38, "controller");
  u8g2.sendBuffer();

  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // sets all ADC1 pins at once on C3

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  calibrateJoystick();

  Serial.println("ready");
}

void loop() {
  static uint32_t lastFrame = 0;
  updateButton();
  const uint32_t now = millis();
  if (now - lastFrame >= FRAME_INTERVAL_MS) {
    lastFrame = now;
    drawPixelFromJoystick();
    const bool strobeMode = (colorMode == 5 || colorMode == 6);
    updateScreen(static_cast<int>(emaPot), static_cast<int>(emaSl1), static_cast<int>(emaSl2),
                 strobeMode, currentStrobeOn);
  }
}
