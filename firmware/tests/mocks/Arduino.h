#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

typedef uint8_t byte;
typedef bool boolean;

#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define FPSTR(s) (s)
#define F(s) (s)

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t INPUT = 0x01;
constexpr uint8_t OUTPUT = 0x02;
constexpr uint8_t INPUT_PULLUP = 0x05;

class String {
private:
    std::string _str;
public:
    String() : _str() {}
    String(const char* s) : _str(s ? s : "") {}
    String(const std::string& s) : _str(s) {}
    String(int v) : _str(std::to_string(v)) {}
    String(unsigned int v) : _str(std::to_string(v)) {}
    String(long v) : _str(std::to_string(v)) {}
    String(unsigned long v) : _str(std::to_string(v)) {}

    const char* c_str() const { return _str.c_str(); }
    size_t length() const { return _str.size(); }
    bool isEmpty() const { return _str.empty(); }
    const std::string& std_str() const { return _str; }

    bool operator==(const String& other) const { return _str == other._str; }
    bool operator==(const char* other) const { return _str == (other ? other : ""); }
    bool operator!=(const String& other) const { return _str != other._str; }
    bool operator!=(const char* other) const { return _str != (other ? other : ""); }

    String operator+(const String& other) const { return String(_str + other._str); }
    String operator+(const char* other) const { return String(_str + (other ? other : "")); }
    String& operator+=(const String& other) { _str += other._str; return *this; }
    String& operator+=(const char* other) { if (other) _str += other; return *this; }
    String& operator+=(char c) { _str.push_back(c); return *this; }
};

class IPAddress {
private:
    uint8_t _octets[4];
public:
    IPAddress(uint8_t a = 192, uint8_t b = 168, uint8_t c = 4, uint8_t d = 1) {
        _octets[0] = a; _octets[1] = b; _octets[2] = c; _octets[3] = d;
    }
    String toString() const {
        char buf[24];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", _octets[0], _octets[1], _octets[2], _octets[3]);
        return String(buf);
    }
};

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t ch) = 0;
    virtual size_t print(const char* s) {
        if (!s) return 0;
        size_t n = 0;
        while (*s) { write(static_cast<uint8_t>(*s++)); n++; }
        return n;
    }
    virtual size_t print(const String& s) { return print(s.c_str()); }
};

namespace MockArduino {
    extern unsigned long currentMillis;
    extern unsigned long lastDelayMs;
    extern unsigned long delayCallCount;
    extern uint8_t pinModes[64];
    extern uint8_t digitalStates[64];
    void reset();
}

inline unsigned long millis() {
    return MockArduino::currentMillis;
}

inline void delay(unsigned long ms) {
    MockArduino::lastDelayMs = ms;
    MockArduino::delayCallCount++;
}

inline void delayMicroseconds(unsigned int) {}

inline void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < 64) MockArduino::pinModes[pin] = mode;
}

inline int digitalRead(uint8_t pin) {
    if (pin < 64) return MockArduino::digitalStates[pin];
    return HIGH;
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

    void print(const char* text) {
        if (text) currentLine += text;
    }

    void print(const String& text) {
        currentLine += text.std_str();
    }

    void print(long value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", value);
        currentLine += buf;
    }

    void print(double value, int digits = 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", digits, value);
        currentLine += buf;
    }

    void println(const char* text) {
        if (text) currentLine += text;
        lines.push_back(currentLine);
        currentLine.clear();
    }

    void println(const String& text) {
        currentLine += text.std_str();
        lines.push_back(currentLine);
        currentLine.clear();
    }

    void println(const IPAddress& ip) {
        println(ip.toString());
    }

    void println(long value) {
        print(value);
        lines.push_back(currentLine);
        currentLine.clear();
    }
};

extern MockSerialPort Serial;

#endif // MOCK_ARDUINO_H
