#ifndef MOCK_HX711_H
#define MOCK_HX711_H

#include "Arduino.h"
#include <vector>

class HX711 {
public:
    byte configuredDout = 0xFF;
    byte configuredSck = 0xFF;
    byte configuredGain = 0;
    int beginCallCount = 0;

    bool readyState = false;
    int isReadyCallCount = 0;

    long nextReadValue = 0;
    int readCallCount = 0;
    int readAverageCallCount = 0;
    int waitReadyCallCount = 0;
    int waitReadyTimeoutCallCount = 0;

    void reset() {
        configuredDout = 0xFF;
        configuredSck = 0xFF;
        configuredGain = 0;
        beginCallCount = 0;
        readyState = false;
        isReadyCallCount = 0;
        nextReadValue = 0;
        readCallCount = 0;
        readAverageCallCount = 0;
        waitReadyCallCount = 0;
        waitReadyTimeoutCallCount = 0;
    }

    void begin(byte dout, byte pd_sck, byte gain = 128) {
        configuredDout = dout;
        configuredSck = pd_sck;
        configuredGain = gain;
        beginCallCount++;
    }

    bool is_ready() {
        isReadyCallCount++;
        return readyState;
    }

    long read() {
        readCallCount++;
        return nextReadValue;
    }

    long read_average(byte times = 10) {
        (void)times;
        readAverageCallCount++;
        return nextReadValue;
    }

    void wait_ready(unsigned long delay_ms = 0) {
        (void)delay_ms;
        waitReadyCallCount++;
    }

    bool wait_ready_timeout(unsigned long timeout = 1000, unsigned long delay_ms = 0) {
        (void)timeout;
        (void)delay_ms;
        waitReadyTimeoutCallCount++;
        return readyState;
    }
};

#endif // MOCK_HX711_H
