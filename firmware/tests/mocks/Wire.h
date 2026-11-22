#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include "Arduino.h"

class TwoWire {
public:
    int sdaPin = -1;
    int sclPin = -1;
    int beginCallCount = 0;
    uint8_t lastAddress = 0;
    uint8_t transmissionResult = 0; // 0 = ACK (cihaz bağlı), !=0 = NACK

    void reset() {
        sdaPin = -1;
        sclPin = -1;
        beginCallCount = 0;
        lastAddress = 0;
        transmissionResult = 0;
    }

    bool begin(int sda = -1, int scl = -1, uint32_t = 0) {
        if (sda >= 0) sdaPin = sda;
        if (scl >= 0) sclPin = scl;
        beginCallCount++;
        return true;
    }

    void beginTransmission(uint8_t address) {
        lastAddress = address;
    }

    uint8_t endTransmission(bool = true) {
        return transmissionResult;
    }
};

extern TwoWire Wire;

#endif // MOCK_WIRE_H
