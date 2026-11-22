#ifndef MOCK_LIQUIDCRYSTAL_PCF8574_H
#define MOCK_LIQUIDCRYSTAL_PCF8574_H

#include "Arduino.h"
#include "Wire.h"

class LiquidCrystal_PCF8574 : public Print {
public:
    uint8_t i2cAddr = 0;
    uint8_t cols = 0;
    uint8_t rows = 0;
    uint8_t backlight = 0;
    uint8_t cursorCol = 0;
    uint8_t cursorRow = 0;
    bool connected = true;

    int beginCallCount = 0;
    int clearCallCount = 0;
    int setBacklightCallCount = 0;
    int printCallCount = 0;
    std::string rowText[4];

    LiquidCrystal_PCF8574(uint8_t addr = 0x27) : i2cAddr(addr) {
        reset();
    }

    void reset() {
        cols = 0;
        rows = 0;
        backlight = 0;
        cursorCol = 0;
        cursorRow = 0;
        connected = true;
        beginCallCount = 0;
        clearCallCount = 0;
        setBacklightCallCount = 0;
        printCallCount = 0;
        for (int i = 0; i < 4; ++i) rowText[i] = std::string(20, ' ');
    }

    void begin(uint8_t c, uint8_t r, TwoWire& = Wire) {
        cols = c;
        rows = r;
        beginCallCount++;
    }

    bool isConnected() {
        return connected;
    }

    void setBacklight(uint8_t b) {
        backlight = b;
        setBacklightCallCount++;
    }

    void clear() {
        clearCallCount++;
        for (int i = 0; i < 4; ++i) rowText[i] = std::string(20, ' ');
    }

    void setCursor(uint8_t col, uint8_t row) {
        cursorCol = col;
        cursorRow = row;
    }

    virtual size_t write(uint8_t ch) override {
        if (cursorRow < 4 && cursorCol < 20) {
            if (rowText[cursorRow].size() < 20) rowText[cursorRow].resize(20, ' ');
            rowText[cursorRow][cursorCol] = static_cast<char>(ch);
            cursorCol++;
        }
        return 1;
    }

    virtual size_t print(const char* s) override {
        printCallCount++;
        return Print::print(s);
    }
};

#endif // MOCK_LIQUIDCRYSTAL_PCF8574_H
