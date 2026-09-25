#include <Arduino.h>
#include <FastLED.h>
#include <NimBLEDevice.h>

// ── BLE MIDI ──────────────────────────────────────────────────────────────────
// BLE MIDI service/characteristic UUIDs (spec-defined).
#define BLEMIDI_SERVICE_UUID        "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLEMIDI_CHAR_UUID           "7772E5DB-3868-4112-A1A9-F2669D106BF3"

static NimBLECharacteristic* bleMidiChar = nullptr;
static bool                  bleConnected = false;

class MidiServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*)    override { bleConnected = true; }
  void onDisconnect(NimBLEServer*) override {
    bleConnected = false;
    NimBLEDevice::getAdvertising()->start();
  }
};

// Send a single BLE MIDI CC message (one notify per CC — universally compatible).
static void bleMidiSendCC(uint8_t channel, uint8_t cc, uint8_t value) {
  if (!bleConnected || !bleMidiChar) return;
  const uint16_t t      = millis() & 0x1FFF;
  const uint8_t  header = 0x80 | ((t >> 7) & 0x3F);
  const uint8_t  ts     = 0x80 | (t & 0x7F);
  const uint8_t  status = 0xB0 | ((channel - 1) & 0x0F);
  const uint8_t  pkt[]  = { header, ts, status, cc, value };
  bleMidiChar->setValue(pkt, sizeof(pkt));
  bleMidiChar->notify();
}

static void bleMidiSendCCs(uint8_t channel,
                            uint8_t x, uint8_t y,
                            uint8_t p, uint8_t s1, uint8_t s2) {
  bleMidiSendCC(channel, 1, x);
  bleMidiSendCC(channel, 2, y);
  bleMidiSendCC(channel, 3, p);
  bleMidiSendCC(channel, 4, s1);
  bleMidiSendCC(channel, 5, s2);
}

static void bleMidiInit() {
  NimBLEDevice::init("AV Controller");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power

  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new MidiServerCB());

  NimBLEService* svc = srv->createService(BLEMIDI_SERVICE_UUID);
  bleMidiChar = svc->createCharacteristic(
    BLEMIDI_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
  );
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLEMIDI_SERVICE_UUID);
  adv->setScanResponse(true);   // puts the 128-bit UUID in scan response (needed for iOS)
  adv->setMinInterval(0x20);    // 20ms — fast advertising for quick discovery
  adv->setMaxInterval(0x40);
  adv->start();

  Serial.println("BLE MIDI advertising as 'AV Controller'");
}
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Matrix size.
constexpr uint8_t  MATRIX_WIDTH  = 8;
constexpr uint8_t  MATRIX_HEIGHT = 8;
constexpr uint16_t LED_COUNT     = MATRIX_WIDTH * MATRIX_HEIGHT;

// Pins.
constexpr uint8_t LED_PIN              = 0;
constexpr uint8_t JOYSTICK_X_PIN       = 34;
constexpr uint8_t JOYSTICK_Y_PIN       = 35;
constexpr uint8_t JOYSTICK_BUTTON_PIN  = 25;
constexpr uint8_t POT_PIN              = 27;
constexpr uint8_t SLIDER1_PIN          = 26;
constexpr uint8_t SLIDER2_PIN          = 33;

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

// Symmetric deflection range around center — set at boot from the calibrated
// center so both directions feel identical from the first frame.
constexpr int AXIS_HALF_RANGE = 1500;

CRGB leds[LED_COUNT];

// Calibrated center values.
int centerX = 2048;
int centerY = 2048;

// Observed ADC extremes — set symmetrically from center in calibrateJoystick().
int axisMinX = 0, axisMaxX = ADC_MAX;
int axisMinY = 0, axisMaxY = ADC_MAX;

// Exponential moving average state — smooths ADC noise between frames.
// Higher alpha = faster response but less smoothing.
constexpr float EMA_ALPHA        = 0.4f;
constexpr float EMA_ALPHA_ANALOG = 0.3f;
float emaX   = 2048.0f;
float emaY   = 2048.0f;
float emaPot = 2048.0f;
float emaSl1 = 2048.0f;
float emaSl2 = 2048.0f;

// Button debounce state.
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastDebounceTime = 0;

// Colour cycle:
//   0=fade(1px)  1=white(2×2)  2=red(2×2)  3=green(2×2)  4=blue(2×2)
//   5=strobe white(2×2)  6=strobe fade(2×2)  7=off
uint8_t colorMode = 0;
constexpr uint8_t COLOR_MODES = 8;

// Strobe: 15 Hz → period ~67 ms, on for first half.
// Strobe frequency range controlled by s2 slider.
constexpr float STROBE_HZ_MIN = 1.0f;
constexpr float STROBE_HZ_MAX = 20.0f;

// Hue for fading mode — incremented every frame (~5 s per full cycle at 50 fps).
uint8_t fadeHue = 0;

// Track whether display was active last frame to know when to blank.
bool displayWasEnabled = true;

// Convert matrix coordinates to LED index.
// All rows go right-to-left (non-serpentine wiring).
uint16_t xyToIndex(uint8_t x, uint8_t y) {
  return static_cast<uint16_t>(y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x));
}

// Average several ADC readings at startup to find the true resting center.
void calibrateJoystick() {
  delay(250);

  long sumX = 0;
  long sumY = 0;
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

// Average several ADC reads and clamp to the safe window to avoid
// ESP32 saturation crosstalk near the ADC rails.
// A dummy read + settling delay is required after channel switches.
int smoothAnalogRead(uint8_t pin, uint8_t samples = 4) {
  analogRead(pin);           // discard — lets the ADC mux settle after channel switch
  delayMicroseconds(100);
  long sum = 0;
  for (uint8_t i = 0; i < samples; i++) sum += analogRead(pin);
  return constrain(static_cast<int>(sum / samples), 0, ADC_MAX);
}

// Map a joystick axis to a matrix position [0, size-1].
// Each direction is mapped independently from the observed ADC extreme to the
// deadzone edge, so both sides always reach position 0 / size-1 regardless of
// how asymmetric the joystick's physical range is.
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

// Cycle colour mode on each button press, with debounce.
void updateButton() {
  const bool reading = digitalRead(JOYSTICK_BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  lastButtonReading = reading;

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        colorMode = (colorMode + 1) % COLOR_MODES;
      }
    }
  }
}

// Draw the pixel(s) that track the joystick position.
void drawPixelFromJoystick() {
  // Mode 7 = off.
  if (colorMode == 7) {
    if (displayWasEnabled) {
      FastLED.clear(true);
      displayWasEnabled = false;
    }
    return;
  }
  displayWasEnabled = true;

  const int rawX = smoothAnalogRead(JOYSTICK_X_PIN);
  const int rawY = smoothAnalogRead(JOYSTICK_Y_PIN);

  // Apply EMA to further smooth inter-frame noise.
  emaX = EMA_ALPHA * rawX + (1.0f - EMA_ALPHA) * emaX;
  emaY = EMA_ALPHA * rawY + (1.0f - EMA_ALPHA) * emaY;
  const int sX  = static_cast<int>(emaX);
  const int sY  = static_cast<int>(emaY);
  emaPot = EMA_ALPHA_ANALOG * smoothAnalogRead(POT_PIN)     + (1.0f - EMA_ALPHA_ANALOG) * emaPot;
  emaSl1 = EMA_ALPHA_ANALOG * smoothAnalogRead(SLIDER1_PIN) + (1.0f - EMA_ALPHA_ANALOG) * emaSl1;
  emaSl2 = EMA_ALPHA_ANALOG * smoothAnalogRead(SLIDER2_PIN) + (1.0f - EMA_ALPHA_ANALOG) * emaSl2;
  const int pot = static_cast<int>(emaPot);
  const int sl1 = ADC_MAX - static_cast<int>(emaSl1);
  const int sl2 = ADC_MAX - static_cast<int>(emaSl2);



  FastLED.clear();

  // Block size from s1: 1×1 to 8×8.
  // Odd sizes > 1 display as the next even size with a dimmed outer ring.
  const int blockSize = 1 + (sl1 * 7 / 4095);
  const bool hasDimRing = (blockSize > 1 && blockSize % 2 == 1);
  const int displaySize = hasDimRing ? blockSize + 1 : blockSize;

  // Brightness: start from pot, then scale down by mode and block size.
  // Fade (0) and white (1) are brighter colours so need extra reduction.
  int brightness;
  if      (colorMode == 3 || colorMode == 4) brightness = map(pot, 0, 4095, 0, 70);
  else if (colorMode == 0 || colorMode == 1) brightness = map(pot, 0, 4095, 0, 22);
  else                                       brightness = map(pot, 0, 4095, 0, 45);
  if      (displaySize >= 7) brightness /= 4;   // 8px → 25%
  else if (displaySize >= 3) brightness /= 2;   // 4–6px → 50%
  FastLED.setBrightness(brightness);

  // Joystick maps to the top-left corner of the block so it can slide fully to each edge.
  const uint8_t sizeX = MATRIX_WIDTH  + displaySize - 1;
  const uint8_t sizeY = MATRIX_HEIGHT + displaySize - 1;
  const uint8_t xPos  = joystickToPosition(sX, centerX, axisMinX, axisMaxX, sizeX);
  const uint8_t yBot  = joystickToPosition(sY, centerY, axisMinY, axisMaxY, sizeY);
  const int bx = (int)xPos - (displaySize - 1);
  const int by = (int)(MATRIX_HEIGHT - yBot) - 1;

  // Strobe phase accumulator — runs in all modes so switching doesn't reset it.
  static float strobePhase = 0.0f;
  const float strobeHz = STROBE_HZ_MIN + (sl2 / 4095.0f) * (STROBE_HZ_MAX - STROBE_HZ_MIN);
  strobePhase += strobeHz * (FRAME_INTERVAL_MS / 1000.0f);
  if (strobePhase >= 1.0f) strobePhase -= 1.0f;
  const bool strobeOn = strobePhase < 0.5f;

  if (colorMode == 0) fadeHue++;
  if (colorMode == 6) fadeHue++;

  CRGB color;
  switch (colorMode) {
    case 0: color = CHSV(fadeHue, 255, 255);                         break;
    case 1: color = CRGB::White;                                      break;
    case 2: color = CRGB::Red;                                        break;
    case 3: color = CRGB::Green;                                      break;
    case 4: color = CRGB::Blue;                                       break;
    case 5: color = strobeOn ? CRGB::White : CRGB::Black;            break;
    case 6: color = strobeOn ? CRGB(CHSV(fadeHue, 255, 255))
                             : CRGB::Black;                           break;
    default: color = CRGB::Black;                                     break;
  }

  const uint8_t joyX = joystickToPosition(sX, centerX, axisMinX, axisMaxX, 9);
  const uint8_t joyY = joystickToPosition(sY, centerY, axisMinY, axisMaxY, 9);

  // BLE MIDI — send CCs on the channel matching the active mode (1-8).
  // Rate-limited to 20 Hz so BLE isn't overwhelmed.
  static uint32_t lastMidi = 0;
  static uint8_t  prevX = 255, prevY = 255, prevP = 255, prevS1 = 255, prevS2 = 255;
  static uint8_t  prevCh = 0;
  const uint32_t nowMidi = millis();
  if (nowMidi - lastMidi >= 100) {
    lastMidi = nowMidi;
    const uint8_t ch  = colorMode + 1;
    const uint8_t mx  = map(joyX, 0, 8,    0, 127);
    const uint8_t my  = map(joyY, 0, 8,    0, 127);
    const uint8_t mp  = map(pot,  0, 4095, 0, 127);
    const uint8_t ms1 = map(sl1,  0, 4095, 0, 127);
    const uint8_t ms2 = map(sl2,  0, 4095, 0, 127);
    const bool chChanged = (ch != prevCh);
    if (chChanged || mx  != prevX)  { bleMidiSendCC(ch, 1, mx);  prevX  = mx; }
    if (chChanged || my  != prevY)  { bleMidiSendCC(ch, 2, my);  prevY  = my; }
    if (chChanged || abs((int)mp  - (int)prevP)  > 1) { bleMidiSendCC(ch, 3, mp);  prevP  = mp; }
    if (chChanged || abs((int)ms1 - (int)prevS1) > 1) { bleMidiSendCC(ch, 4, ms1); prevS1 = ms1; }
    if (chChanged || abs((int)ms2 - (int)prevS2) > 1) { bleMidiSendCC(ch, 5, ms2); prevS2 = ms2; }
    prevCh = ch;
  }

  CRGB dimColor = color;
  dimColor.nscale8(50);

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

}  // namespace

void setup() {
  Serial.begin(115200);

  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(JOYSTICK_X_PIN, ADC_11db);
  analogSetPinAttenuation(JOYSTICK_Y_PIN, ADC_11db);
  analogSetPinAttenuation(POT_PIN,        ADC_11db);
  analogSetPinAttenuation(SLIDER1_PIN,    ADC_11db);
  analogSetPinAttenuation(SLIDER2_PIN,    ADC_11db);

  bleMidiInit();

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 50);  // ~200mA real cap — leaves headroom for BLE radio bursts
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
  }
}
