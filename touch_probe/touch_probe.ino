/*
 * reTerminal Sticky raw-touch probe (no HID / no display writes)
 *
 * Target: Seeed reTerminal Sticky, ESP32-S3 + Goodix GT911.
 * Touch pins from Seeed's Sticky hardware overview:
 *   SDA 3, SCL 2, INT 21, RESET 41, power enable 42.
 *
 * Compile candidate:
 *   arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio' touch_probe
 *
 * Serial is UART0 on GPIO43/44 through Sticky's CH343P USB bridge.
 * CDCOnBoot=default is deliberate: Serial must NOT select ESP32 native USB.
 * This reports raw GT911 data only: no USB/BLE HID, display writes, or
 * user-data storage. Preserve the original flash before first upload.
 *
 * GT911 address selection uses INT state during reset. The sequence below is
 * a candidate based on community Sticky GT911 code; verify on this exact
 * device/revision before flashing. It tries 0x5D and 0x14 in turn.
 */
#include <Arduino.h>
#include <Wire.h>
#include "touch_data.h"

static constexpr int PIN_TOUCH_SCL = 2;
static constexpr int PIN_TOUCH_SDA = 3;
static constexpr int PIN_TOUCH_INT = 21;
static constexpr int PIN_TOUCH_RST = 41;
static constexpr int PIN_TOUCH_EN  = 42;
static constexpr uint8_t GT911_ADDR_CANDIDATES[] = {0x5D, 0x14};
static constexpr uint32_t POLL_INTERVAL_MS = 20;
static constexpr uint16_t REG_PRODUCT_ID = 0x8140;
static constexpr uint16_t REG_SENSOR_RESOLUTION = 0x8146; // runtime sensor resolution, not config RAM
static constexpr uint16_t REG_STATUS = 0x814E;
static constexpr uint16_t REG_POINT1 = 0x814F;

static uint8_t gtAddress = 0;
static uint32_t lastPollMs = 0;
static uint32_t sampleNumber = 0;
static int previousInt = HIGH;
static uint8_t previousCount = 0;
static uint32_t frames = 0;
static uint32_t errors = 0;
static uint32_t lastHeartbeatMs = 0;

static bool readRegisters(uint16_t reg, uint8_t *out, size_t length) {
  Wire.beginTransmission(gtAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  const size_t received = Wire.requestFrom(static_cast<int>(gtAddress), static_cast<int>(length));
  if (received != length) {
    while (Wire.available()) (void)Wire.read();
    return false;
  }
  for (size_t i = 0; i < length; ++i) out[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

static bool writeRegister(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(gtAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool probeAddress(uint8_t address) {
  gtAddress = address;
  uint8_t product[4] = {};
  if (!readRegisters(REG_PRODUCT_ID, product, sizeof(product))) return false;
  Serial.printf("GT911 probe addr=0x%02X product='%c%c%c%c' bytes=%02X %02X %02X %02X\n",
                gtAddress,
                product[0] >= 32 && product[0] <= 126 ? product[0] : '.',
                product[1] >= 32 && product[1] <= 126 ? product[1] : '.',
                product[2] >= 32 && product[2] <= 126 ? product[2] : '.',
                product[3] >= 32 && product[3] <= 126 ? product[3] : '.',
                product[0], product[1], product[2], product[3]);
  return sticky::isGT911(product);
}

static void resetTouchController(uint8_t address) {
  // INT selects the GT911's I2C address at the reset rising edge.
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, address == 0x5D ? LOW : HIGH);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(10);
  // Complete the post-reset INT synchronization before normal I2C polling.
  digitalWrite(PIN_TOUCH_INT, LOW);
  delay(50);
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(10);
}

static bool beginTouch() {
  pinMode(PIN_TOUCH_EN, OUTPUT);
  digitalWrite(PIN_TOUCH_EN, HIGH);
  delay(300);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 100000);
  for (uint8_t address : GT911_ADDR_CANDIDATES) {
    resetTouchController(address);
    if (probeAddress(address)) return true;
  }
  gtAddress = 0;
  return false;
}

static void pollTouch() {
  const uint32_t now = millis();
  if (now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;
  ++sampleNumber;

  const int intLevel = digitalRead(PIN_TOUCH_INT);
  if (intLevel != previousInt) {
    Serial.printf("INT t=%lu level=%d\n", static_cast<unsigned long>(now), intLevel);
    previousInt = intLevel;
  }

  uint8_t status = 0;
  if (!readRegisters(REG_STATUS, &status, 1)) {
    ++errors;
    if ((sampleNumber % 50) == 0) Serial.printf("I2C read error t=%lu\n", static_cast<unsigned long>(now));
    return;
  }

  const uint8_t count = status & 0x0F;
  const bool ready = (status & 0x80) != 0;
  if (!ready) return;
  if (count > 5) {
    Serial.printf("Unexpected GT911 touch count=%u status=0x%02X\n", count, status);
    (void)writeRegister(REG_STATUS, 0);
    return;
  }

  // Each contact record is 8 bytes: track ID, X, Y, and size.
  uint8_t points[5 * 8] = {};
  if (count && !readRegisters(REG_POINT1, points, static_cast<size_t>(count) * 8)) {
    Serial.printf("Point data read error t=%lu count=%u\n", static_cast<unsigned long>(now), count);
    return;
  }
  const char *event = count == 0 ? "UP" : previousCount == 0 ? "DOWN" : "MOVE";
  ++frames;
  Serial.printf("TOUCH %s t=%lu n=%lu status=0x%02X count=%u",
                event, static_cast<unsigned long>(now), static_cast<unsigned long>(sampleNumber), status, count);
  for (uint8_t i = 0; i < count; ++i) {
    const auto p = sticky::decodePoint(&points[i * 8]);
    Serial.printf(" [id=%u x=%u y=%u size=%u]", p.id, p.x, p.y, p.size);
  }
  Serial.println();
  previousCount = count;

  // Acknowledge ready only after reading the full point record(s).
  if (!writeRegister(REG_STATUS, 0)) Serial.println("GT911 status clear failed");
}

void setup() {
  // Seeed peripheral demo: keep board supply latched on during testing.
  pinMode(45, OUTPUT);
  digitalWrite(45, HIGH);
  pinMode(46, OUTPUT);
  digitalWrite(46, HIGH);
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) delay(10);
  Serial.println("Sticky raw-touch probe; HID disabled; e-paper untouched");
  Serial.printf("Touch pins SDA=%d SCL=%d INT=%d RST=%d EN=%d\n",
                PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT, PIN_TOUCH_RST, PIN_TOUCH_EN);
  if (!beginTouch()) {
    Serial.println("GT911 not detected at candidate addresses 0x5D/0x14; stopping polls.");
    return;
  }
  Serial.printf("GT911 found at 0x%02X\n", gtAddress);
  uint8_t resolution[4] = {};
  if (readRegisters(REG_SENSOR_RESOLUTION, resolution, sizeof(resolution))) {
    const uint16_t maxX = static_cast<uint16_t>(resolution[0] | (resolution[1] << 8));
    const uint16_t maxY = static_cast<uint16_t>(resolution[2] | (resolution[3] << 8));
    Serial.printf("GT911 resolution registers sensorX=%u sensorY=%u raw=%02X %02X %02X %02X\n",
                  maxX, maxY, resolution[0], resolution[1], resolution[2], resolution[3]);
  } else {
    Serial.println("GT911 resolution register read failed");
  }
}

void loop() {
  if (gtAddress) pollTouch();
  if (millis() - lastHeartbeatMs >= 2000) {
    lastHeartbeatMs = millis();
    Serial.printf("ALIVE t=%lu addr=0x%02X frames=%lu read_errors=%lu count=%u INT=%d\n",
                  static_cast<unsigned long>(millis()), gtAddress,
                  static_cast<unsigned long>(frames), static_cast<unsigned long>(errors),
                  previousCount, digitalRead(PIN_TOUCH_INT));
  }
  delay(1);
}
