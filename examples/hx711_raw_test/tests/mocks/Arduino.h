#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

typedef uint8_t byte;

namespace MockArduino {
    extern unsigned long currentMillis;
    extern unsigned long lastDelayMs;
    extern unsigned long delayCallCount;
    void reset();
}

inline unsigned long millis() {
    return MockArduino::currentMillis;
}

inline void delay(unsigned long ms) {
    MockArduino::lastDelayMs = ms;
    MockArduino::delayCallCount++;
}

class MockSerialPort {
public:
    unsigned long baudRate = 0;
    int beginCallCount = 0;
    std::string currentLine;
    std::vector<std::string> lines;

    void reset() {
        baudRate = 0;
        beginCallCount = 0;
        currentLine.clear();
        lines.clear();
    }

    void begin(unsigned long baud) {
        baudRate = baud;
        beginCallCount++;
    }

    void print(const char* s) {
        if (s) currentLine += s;
    }

    void print(long v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", v);
        currentLine += buf;
    }

    void print(unsigned long v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", v);
        currentLine += buf;
    }

    void println(const char* s) {
        print(s);
        lines.push_back(currentLine);
        currentLine.clear();
    }

    void println(long v) {
        print(v);
        lines.push_back(currentLine);
        currentLine.clear();
    }

    void println(unsigned long v) {
        print(v);
        lines.push_back(currentLine);
        currentLine.clear();
    }
};

extern MockSerialPort Serial;

#endif // MOCK_ARDUINO_H
