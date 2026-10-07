#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_SHT31.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

/*
  ESP32 Urban Air Pollution Exposure Chamber Controller

  CONNECTION SUMMARY
  ------------------
  Shared I2C bus:
    GPIO 21 -> LCD SDA, SHT31 SDA, DS3231 SDA
    GPIO 22 -> LCD SCL, SHT31 SCL, DS3231 SCL

  PMS5003 UART:
    PMS TX  -> GPIO 16 (ESP32 RX2)
    PMS RX  -> not used
    PMS VCC -> regulated 5 V; PMS GND -> common ground

  MicroSD SPI:
    CS -> GPIO 5, SCK -> GPIO 18, MISO -> GPIO 19, MOSI -> GPIO 23

  External blower:
    GPIO 25 -> MOSFET control input
    Use a separate, fused supply matching the blower rating.
    Join the external-supply ground to ESP32 ground.
    Never power the blower directly from the ESP32.

  IMPORTANT
  ---------
  Many 5 V I2C LCD backpacks pull SDA/SCL up to 5 V. Protect the 3.3 V ESP32
  with an appropriate level shifter or 3.3 V pull-ups.
*/

enum class ExposureMode {
  PM25,
  PM10
};

// Change this single setting for the intended chamber configuration.
constexpr ExposureMode ACTIVE_MODE = ExposureMode::PM25;

// true: blower introduces particle-laden air; sensor failure stops the blower.
// false: blower is ventilation/exhaust; sensor failure commands full airflow.
constexpr bool BLOWER_SUPPLIES_POLLUTED_AIR = true;

constexpr uint8_t SD_CS_PIN = 5;
constexpr uint8_t PMS_RX_PIN = 16;
constexpr uint8_t MOSFET_PIN = 25;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

constexpr uint32_t PMS_BAUD_RATE = 9600;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t LOG_INTERVAL_MS = 2000;
constexpr uint32_t PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint8_t PWM_CHANNEL = 0;  // Used by Arduino-ESP32 2.x.

constexpr char LOG_PATH[] = "/air_data.csv";

// Prototype thresholds supplied with the original project firmware.
// These are configuration values, not universal exposure limits.
constexpr uint16_t BAND_60_MIN = 500;
constexpr uint16_t BAND_40_MIN = 676;
constexpr uint16_t BAND_20_MIN = 700;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_SHT31 sht31;
RTC_DS3231 rtc;
HardwareSerial pmsSerial(2);

bool shtReady = false;
bool rtcReady = false;
bool sdReady = false;
unsigned long lastLogTime = 0;

struct PmsData {
  uint16_t pm1 = 0;
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;
  bool valid = false;
};

uint8_t percentToDuty(uint8_t percentage) {
  return static_cast<uint8_t>((static_cast<uint16_t>(percentage) * 255U) / 100U);
}

void initializePwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(MOSFET_PIN, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(MOSFET_PIN, PWM_CHANNEL);
#endif
}

void writeBlowerDuty(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MOSFET_PIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

void setBlowerPercentage(uint8_t percentage) {
  writeBlowerDuty(percentToDuty(percentage));
}

uint8_t faultBlowerPercentage() {
  return BLOWER_SUPPLIES_POLLUTED_AIR ? 0 : 100;
}

const char* modeLabel() {
  return ACTIVE_MODE == ExposureMode::PM25 ? "PM2.5" : "PM10";
}

uint16_t selectedPm(const PmsData& data) {
  return ACTIVE_MODE == ExposureMode::PM25 ? data.pm25 : data.pm10;
}

uint8_t blowerPercentageFor(uint16_t selectedReading) {
  if (selectedReading >= BAND_20_MIN) {
    return 20;
  }
  if (selectedReading >= BAND_40_MIN) {
    return 40;
  }
  if (selectedReading >= BAND_60_MIN) {
    return 60;
  }
  return 100;
}

PmsData readPmsData() {
  PmsData data;

  while (pmsSerial.available() >= 32) {
    if (pmsSerial.read() != 0x42) {
      continue;
    }

    if (pmsSerial.peek() != 0x4D) {
      continue;
    }

    uint8_t frame[32] = {0};
    frame[0] = 0x42;
    const size_t received = pmsSerial.readBytes(frame + 1, 31);
    if (received != 31 || frame[1] != 0x4D) {
      continue;
    }

    const uint16_t frameLength = (static_cast<uint16_t>(frame[2]) << 8) | frame[3];
    if (frameLength != 28) {
      continue;
    }

    uint16_t calculatedChecksum = 0;
    for (uint8_t i = 0; i < 30; ++i) {
      calculatedChecksum += frame[i];
    }

    const uint16_t receivedChecksum =
        (static_cast<uint16_t>(frame[30]) << 8) | frame[31];

    if (calculatedChecksum != receivedChecksum) {
      continue;
    }

    // Atmospheric-environment mass concentrations from the PMS frame.
    data.pm1 = (static_cast<uint16_t>(frame[10]) << 8) | frame[11];
    data.pm25 = (static_cast<uint16_t>(frame[12]) << 8) | frame[13];
    data.pm10 = (static_cast<uint16_t>(frame[14]) << 8) | frame[15];
    data.valid = true;
    return data;
  }

  return data;
}

void printPaddedLcdLine(uint8_t row, const char* text) {
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16.16s", text);
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void updateLcd(const PmsData& pms, float temperature, float humidity) {
  char line1[24];
  char line2[24];

  if (pms.valid) {
    snprintf(line1, sizeof(line1), "%s:%u ug/m3", modeLabel(), selectedPm(pms));
  } else {
    snprintf(line1, sizeof(line1), "%s SENSOR ERR", modeLabel());
  }

  if (!isnan(temperature) && !isnan(humidity)) {
    snprintf(line2, sizeof(line2), "T:%.1fC H:%.0f%%", temperature, humidity);
  } else {
    snprintf(line2, sizeof(line2), "T/RH SENSOR ERR");
  }

  printPaddedLcdLine(0, line1);
  printPaddedLcdLine(1, line2);
}

void writeCsvHeader(Print& output) {
  output.println(
      "Date,Time,Exposure_Mode,PM1.0_ug_m3,PM2.5_ug_m3,PM10_ug_m3,"
      "Temperature_C,Humidity_pct,PMS_Valid,Blower_Speed_pct");
}

void writeCsvRow(Print& output,
                 const PmsData& pms,
                 float temperature,
                 float humidity,
                 uint8_t blowerPercentage) {
  if (rtcReady) {
    const DateTime now = rtc.now();
    char dateBuffer[11];
    char timeBuffer[9];
    snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d",
             now.year(), now.month(), now.day());
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
             now.hour(), now.minute(), now.second());
    output.print(dateBuffer);
    output.print(',');
    output.print(timeBuffer);
  } else {
    output.print("NA,NA");
  }

  output.print(',');
  output.print(modeLabel());
  output.print(',');

  if (pms.valid) {
    output.print(pms.pm1);
    output.print(',');
    output.print(pms.pm25);
    output.print(',');
    output.print(pms.pm10);
  } else {
    output.print("NA,NA,NA");
  }

  output.print(',');
  if (isnan(temperature)) {
    output.print("NA");
  } else {
    output.print(temperature, 1);
  }

  output.print(',');
  if (isnan(humidity)) {
    output.print("NA");
  } else {
    output.print(humidity, 1);
  }

  output.print(',');
  output.print(pms.valid ? 1 : 0);
  output.print(',');
  output.println(blowerPercentage);
}

void initializeSdCard() {
  sdReady = SD.begin(SD_CS_PIN);
  if (!sdReady) {
    Serial.println("ERROR: microSD initialization failed");
    return;
  }

  if (!SD.exists(LOG_PATH)) {
    File file = SD.open(LOG_PATH, FILE_WRITE);
    if (!file) {
      sdReady = false;
      Serial.println("ERROR: could not create CSV log");
      return;
    }
    writeCsvHeader(file);
    file.close();
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  pmsSerial.begin(PMS_BAUD_RATE, SERIAL_8N1, PMS_RX_PIN, -1);
  pmsSerial.setTimeout(100);

  initializePwm();
  setBlowerPercentage(faultBlowerPercentage());

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.backlight();
  printPaddedLcdLine(0, "Initializing...");
  printPaddedLcdLine(1, modeLabel());

  shtReady = sht31.begin(0x44) || sht31.begin(0x45);
  if (!shtReady) {
    Serial.println("WARNING: SHT31 not detected");
  }

  rtcReady = rtc.begin();
  if (!rtcReady) {
    Serial.println("WARNING: DS3231 RTC not detected");
  } else if (rtc.lostPower()) {
    Serial.println("WARNING: RTC lost power; set the clock before data collection");
  }

  initializeSdCard();
  writeCsvHeader(Serial);
  delay(500);
  lcd.clear();
}

void loop() {
  const unsigned long nowMs = millis();
  if (nowMs - lastLogTime < LOG_INTERVAL_MS) {
    return;
  }
  lastLogTime = nowMs;

  const PmsData pms = readPmsData();
  const float temperature = shtReady ? sht31.readTemperature() : NAN;
  const float humidity = shtReady ? sht31.readHumidity() : NAN;

  const uint8_t blowerPercentage =
      pms.valid ? blowerPercentageFor(selectedPm(pms)) : faultBlowerPercentage();
  setBlowerPercentage(blowerPercentage);

  updateLcd(pms, temperature, humidity);
  writeCsvRow(Serial, pms, temperature, humidity, blowerPercentage);

  if (sdReady) {
    File file = SD.open(LOG_PATH, FILE_APPEND);
    if (file) {
      writeCsvRow(file, pms, temperature, humidity, blowerPercentage);
      file.close();
    } else {
      Serial.println("ERROR: could not append to CSV log");
    }
  }
}


