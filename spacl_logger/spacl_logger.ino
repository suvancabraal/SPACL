#include <Wire.h>
#include <RTClib.h>
#include <LowPower.h>
#include <SPI.h>
#include <SD.h>
#include <BME280I2C.h>
#include <Adafruit_MCP9600.h>
#include <SDI12.h>

// ------------------------ User-toggles / easy edits ------------------------

const char* SD_FILENAME    = "SPACL##.txt";   // change your output file here
#define MINUTE 60UL
const unsigned long LOOP_PERIOD_S  = 1 * MINUTE; // Measurement Loop 1m
const int  STOP_AFTER_RECNUM       = 6;   
const bool FORCE_RTC_RESET = false;          // True: reset time, then False

// ------------------------ Pins & addresses ------------------------
#define SERIAL_BAUD 9600
#define BME_ADDRESS 0x76
#define MCP_ADDRESS 0x67
#define CHIP_SELECT 10
#define SDI12_DATA_PIN 5
#define SOIL_PIN A0
#define SIGNAL_PIN 7
#define WAKE_DELAY 0

// ------------------------ Globals ------------------------
File dataFile;
RTC_DS3231 rtc;
BME280I2C bme;
Adafruit_MCP9600 mcp;
SDI12 mySDI12(SDI12_DATA_PIN);
int RecNum = 1;

// ------------------------ Helpers ------------------------

inline unsigned long s2ms(unsigned long s){ return s * 1000UL; }

void SD_setup() {
  if (!SD.begin(CHIP_SELECT)) {
    Serial.println(F("SD Fail"));
    while (1);
  }
}

void RTC_setup() {
  if (!rtc.begin()) {
    Serial.println(F("RTC Fail"));
    while (1);
  }

  if (rtc.lostPower() || FORCE_RTC_RESET) {
    Serial.println(F("RTC Reset"));
    DateTime compiled(F(__DATE__), F(__TIME__));
    rtc.adjust(compiled + TimeSpan(0, 0, 0, 10));
    rtc.disable32K();
  }

}

// Low-power sleep in watchdog chunks (so we don't block long with delay)
void sleep_ms(unsigned long ms){
  while (ms >= 8000UL){ LowPower.powerDown(SLEEP_8S,   ADC_OFF, BOD_OFF); ms -= 8000UL; }
  if    (ms >= 4000UL){ LowPower.powerDown(SLEEP_4S,   ADC_OFF, BOD_OFF); ms -= 4000UL; }
  if    (ms >= 2000UL){ LowPower.powerDown(SLEEP_2S,   ADC_OFF, BOD_OFF); ms -= 2000UL; }
  if    (ms >= 1000UL){ LowPower.powerDown(SLEEP_1S,   ADC_OFF, BOD_OFF); ms -= 1000UL; }
  if    (ms >= 500UL) { LowPower.powerDown(SLEEP_500MS,ADC_OFF, BOD_OFF); ms -= 500UL;  }
  if    (ms >= 250UL) { LowPower.powerDown(SLEEP_250MS,ADC_OFF, BOD_OFF); ms -= 250UL;  }
  if    (ms >= 120UL) { LowPower.powerDown(SLEEP_120MS,ADC_OFF, BOD_OFF); ms -= 120UL;  }
  if    (ms >= 60UL)  { LowPower.powerDown(SLEEP_60MS, ADC_OFF, BOD_OFF); ms -= 60UL;   }
  if    (ms >= 30UL)  { LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_OFF); ms -= 30UL;   }
}

void BME_setup() {
  if (!bme.begin()) {
    Serial.println(F("BME Fail"));
    while (1);
  }
}

void MCP_setup() {
  if (!mcp.begin(MCP_ADDRESS)) {
    Serial.println(F("MCP Fail"));
    while (1);
  }
  mcp.setADCresolution(MCP9600_ADCRESOLUTION_18);
  mcp.setThermocoupleType(MCP9600_TYPE_T);
  mcp.setFilterCoefficient(3);
}

void read_bme280(float &temp, float &hum, float &pres) {
  temp = bme.temp();
  hum  = bme.hum();
  pres = bme.pres() / 100.0F;
}

void read_mcp9600(float &adc_mv) {
  adc_mv = mcp.readADC() * 0.002; // V per LSB -> mV
}

float read_soil_moisture() {
  int raw = analogRead(SOIL_PIN);
  return (raw * 5.0) / 1023.0;
}

bool getResults(char i, int resultsExpected) {
  uint8_t resultsReceived = 0;
  String command = String(i) + "D0!";
  mySDI12.sendCommand(command, WAKE_DELAY);

  uint32_t start = millis();
  while (mySDI12.available() < 3 && (millis() - start) < 1500) {}

  mySDI12.read();                // discard echoed address
  if (mySDI12.peek() == '+') mySDI12.read();

  while (mySDI12.available()) {
    char c = mySDI12.peek();
    if (isdigit(c) || c == '.' || c == '-') {
      float result = mySDI12.parseFloat();
      dataFile.print(String(result, 2));
      dataFile.print(',');
      Serial.print(result, 2);
      Serial.print(' ');
      if (result != -9999) resultsReceived++;
    } else {
      mySDI12.read();
    }
    delay(10);
  }
  mySDI12.clearBuffer();
  return (resultsReceived == resultsExpected);
}

bool takeMeasurement(char i) {
  mySDI12.clearBuffer();
  String command = String(i) + "M!";
  mySDI12.sendCommand(command, WAKE_DELAY);
  delay(100);

  uint8_t waitTime = 1;   // seconds (device dependent)
  uint8_t numResults = 3; // expected
  unsigned long t0 = millis();
  while ((millis() - t0) < (1000UL * (waitTime + 1))) {}

  mySDI12.clearBuffer();
  return (numResults > 0) ? getResults(i, numResults) : true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) {}
  Serial.println(F("Setup"));

  Wire.begin();
  SD_setup();
  RTC_setup();
  BME_setup();
  MCP_setup();
  mySDI12.begin();

  pinMode(SOIL_PIN, INPUT);
  pinMode(SIGNAL_PIN, OUTPUT);
  digitalWrite(SIGNAL_PIN, LOW);

  Serial.println(F("------------------------"));
  Serial.flush();
}

void loop() {

  unsigned long t_start = millis();
  unsigned long elapsed = millis() - t_start;
  if (elapsed < s2ms(LOOP_PERIOD_S)) {
    delay(5);
    sleep_ms(s2ms(LOOP_PERIOD_S) - elapsed);
  }

  dataFile = SD.open(SD_FILENAME, FILE_WRITE);
  if (!dataFile) {
    Serial.println(F("SD Fail"));
    return;
  }

  DateTime time = rtc.now();

  char ts[] = "MM/DD/YY hh:mm:ss";
  dataFile.print(time.toString(ts));
  dataFile.print(',');
  Serial.println(ts);

  float temp, hum, pres;
  read_bme280(temp, hum, pres);
  dataFile.print(temp); dataFile.print(',');
  dataFile.print(hum);  dataFile.print(',');
  dataFile.print(pres); dataFile.print(',');
  Serial.print(F("BME: ")); Serial.print(temp); Serial.print('/'); Serial.print(hum);

  float adc_mv;
  read_mcp9600(adc_mv);
  dataFile.print(adc_mv);dataFile.print(',');
  Serial.print(F("\nMCP: ")); 
  Serial.println(adc_mv);

  float soil_v = read_soil_moisture();
  dataFile.print(soil_v); dataFile.print(',');
  Serial.print(F("Soil: ")); Serial.println(soil_v);

  Serial.print(F("WWC: "));
  for (byte i = 0; i < 1; i++) {
    char addr = '0' + i;
    takeMeasurement(addr);
    Serial.println();
  }

  RecNum++;
  dataFile.println();
  dataFile.close();

  Serial.println(F("------------------------"));
  Serial.flush();

  if (RecNum == STOP_AFTER_RECNUM) {
    Serial.println(F("End — signal to D7"));
    digitalWrite(SIGNAL_PIN, HIGH);
    delay(250);
    digitalWrite(SIGNAL_PIN, LOW);
    while (1);
  }

}
