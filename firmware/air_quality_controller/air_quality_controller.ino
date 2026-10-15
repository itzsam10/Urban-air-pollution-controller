#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_SHT31.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// --- PIN DEFINITIONS ---
#define SD_CS_PIN 5
#define PMS_RX_PIN 16        // ESP32 RX2 pin connected to PMS Sensor TX pin
#define MOSFET_PIN 25        // PWM control pin for MOSFET module / Blower

// --- PWM CONFIGURATION ---
#define PWM_FREQ 5000        // 5 kHz PWM frequency
#define PWM_RESOLUTION 8     // 8-bit resolution (0 - 255 values)

// PWM Duty Cycle Values for 8-bit (0-255)
#define SPEED_0_PERCENT   0    // Blower OFF
#define SPEED_20_PERCENT  51   // 20% of 255
#define SPEED_40_PERCENT  102  // 40% of 255
#define SPEED_60_PERCENT  153  // 60% of 255
#define SPEED_100_PERCENT 255  // 100% of 255

// --- OBJECT INITIALIZATION ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
RTC_DS3231 rtc;
HardwareSerial pmsSerial(2);

// --- VARIABLES ---
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL = 2000;

// Structure to hold all particulate readings
struct SensorData {
  int pm1;
  int pm25;
  int pm10;
};

// Robust Parser to extract PM1.0, PM2.5, and PM10 together
SensorData readPMSData() {
  SensorData data = {-1, -1, -1};
  while (pmsSerial.available() >= 32) {
    if (pmsSerial.peek() == 0x42) {
      uint8_t buffer[32];
      pmsSerial.readBytes(buffer, 32);

      // Verify frame header (0x42 0x4D)
      if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
        // Verify Checksum
        uint16_t checksum = (buffer[30] << 8) | buffer[31];
        uint16_t calculatedChecksum = 0;
        for (int i = 0; i < 30; i++) {
          calculatedChecksum += buffer[i];
        }

        if (checksum == calculatedChecksum) {
          // Extract atmospheric environment data fields (Bytes 10 to 15)
          data.pm1  = (buffer[10] << 8) | buffer[11];
          data.pm25 = (buffer[12] << 8) | buffer[13];
          data.pm10 = (buffer[14] << 8) | buffer[15];
          return data;
        }
      }
    } else {
      pmsSerial.read(); // Discard non-header byte to realign stream
    }
  }
  return data; // Returns -1 if data is incomplete or corrupt
}

void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX_PIN, -1);

  // Initialize ESP32 PWM on MOSFET Pin
  ledcAttach(MOSFET_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(MOSFET_PIN, SPEED_40_PERCENT); // Start blower at default speed

  Wire.begin(21, 22);

  // LCD Init
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // SHT Sensor Init
  if (!sht31.begin(0x44)) {
    if (!sht31.begin(0x45)) {
      Serial.println("Warning: SHT sensor not found!");
    }
  }

  // RTC Init
  if (!rtc.begin()) {
    Serial.println("Warning: RTC module not found!");
  }

  // SD Card Init
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    lcd.setCursor(0, 1);
    lcd.print("SD Init Failed!");
    delay(2000);
  } else {
    File file = SD.open("/air_data.csv", FILE_WRITE);
    if (file) {
      if (file.size() == 0) {
        file.println("Date,Time,PM1.0(ug/m3),PM2.5(ug/m3),PM10(ug/m3),Temperature(C),Humidity(%),Blower_Speed(%)");
      }
      file.close();
    }
  }

  // Print Header to Serial Monitor as well
  Serial.println("Date,Time,PM1.0(ug/m3),PM2.5(ug/m3),PM10(ug/m3),Temperature(C),Humidity(%),Blower_Speed(%)");

  lcd.clear();
}

void loop() {
  if (millis() - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = millis();

    // 1. Read Sensors
    float temp = sht31.readTemperature();
    float hum = sht31.readHumidity();
    SensorData pms = readPMSData();

    if (isnan(temp)) temp = 0.0;
    if (isnan(hum)) hum = 0.0;
    if (pms.pm1 < 0) pms.pm1 = 0;
    if (pms.pm25 < 0) pms.pm25 = 0;
    if (pms.pm10 < 0) pms.pm10 = 0;

    // 2. MOSFET Blower Speed Logic (100% when PM2.5 < 500)
    int currentPwmValue = SPEED_0_PERCENT;
    int speedPercentage = 0;

    if (pms.pm25 >= 700) {
      currentPwmValue = SPEED_20_PERCENT;  // 20% Speed
      speedPercentage = 20;
    } else if (pms.pm25 >= 676 && pms.pm25 <= 699) {
      currentPwmValue = SPEED_40_PERCENT;  // 40% Speed
      speedPercentage = 40;
    } else if (pms.pm25 >= 500 && pms.pm25 <= 675) {
      currentPwmValue = SPEED_60_PERCENT;  // 60% Speed
      speedPercentage = 60;
    } else { // pms.pm25 < 500
      currentPwmValue = SPEED_100_PERCENT; // 100% Speed
      speedPercentage = 100;
    }

    // Apply speed to MOSFET
    ledcWrite(MOSFET_PIN, currentPwmValue);

    // 3. Format Date & Time
    DateTime now = rtc.now();
    char dateBuf[11];
    char timeBuf[9];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    // 4. Update LCD Display (Showing PM2.5 on line 0, Temp & Hum on line 1)
    lcd.setCursor(0, 0);
    lcd.print("PM2.5: ");
    lcd.print(pms.pm25);
    lcd.print(" ug/m3   ");

    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(temp, 1);
    lcd.print((char)223);
    lcd.print("C ");

    lcd.print("H:");
    lcd.print((int)hum);
    lcd.print("%   ");

    // 5. Construct CSV Log String
    String logDataEntry = String("'") + dateBuf + "','" + timeBuf + "'," +
                          pms.pm1 + "," + pms.pm25 + "," + pms.pm10 + "," +
                          String(temp, 1) + "," + (int)hum + "," + speedPercentage + "%";

    // 6. Save to SD Card using FILE_WRITE (automatically handles appends safely on ESP32)
    File dataFile = SD.open("/air_data.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println(logDataEntry);
      dataFile.close();
    } else {
      Serial.println("Error opening air_data.csv on SD Card");
    }

    // 7. Print to Serial Monitor
    Serial.println(logDataEntry);
  }
}

