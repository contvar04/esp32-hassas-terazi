#include <Arduino.h>
#include <Wire.h>
#include "scan_core.h"

constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;
bool busReady = false;
unsigned scanNumber = 0;

struct SerialReporter {
    void found(uint8_t address) { Serial.printf("I2C_FOUND: 0x%02X\n", address); }
    void error(uint8_t address, uint8_t status) {
        Serial.printf("I2C_ERROR: address=0x%02X status=%u\n", address, status);
    }
};

void setup() {
    Serial.begin(115200);
    delay(500);
    busReady = Wire.begin(SDA_PIN, SCL_PIN, 100000);
    Wire.setTimeOut(50);
    Serial.println("I2C_DIAGNOSTIC: SDA=21 SCL=22 baud=115200; Wi-Fi OFF");
    if (!busReady) Serial.println("I2C_INIT_FAILED");
}

void loop() {
    if (busReady) {
        const bool sdaHigh = digitalRead(SDA_PIN) == HIGH;
        const bool sclHigh = digitalRead(SCL_PIN) == HIGH;
        Serial.printf("I2C_SCAN_BEGIN: n=%u SDA=%d SCL=%d\n", ++scanNumber, sdaHigh, sclHigh);
        SerialReporter reporter;
        const ScanResult result = scanI2c(Wire, reporter, sdaHigh, sclHigh);
        if (!result.completed) Serial.println("I2C_BUS_LOW: probing skipped; disconnect power before rewiring");
        Serial.printf("I2C_SCAN_END: completed=%d found=%u errors=%u\n", result.completed, result.found, result.errors);
    }
    delay(3000);
}
