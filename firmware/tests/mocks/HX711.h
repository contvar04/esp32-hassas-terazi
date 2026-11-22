#ifndef MOCK_HX711_H
#define MOCK_HX711_H

#include "Arduino.h"
#include <deque>

class HX711 {
public:
    byte configuredDout = 0;
    byte configuredSck = 0;
    byte configuredGain = 0;
    int beginCallCount = 0;
    int isReadyCallCount = 0;
    int readCallCount = 0;
    int readAverageCallCount = 0;
    int tareCallCount = 0;
    int waitReadyTimeoutCallCount = 0;

    bool readyState = false;
    long nextReadValue = 0;
    std::deque<long> queuedSamples;

    void reset() {
        configuredDout = 0;
        configuredSck = 0;
        configuredGain = 0;
        beginCallCount = 0;
        isReadyCallCount = 0;
        readCallCount = 0;
        readAverageCallCount = 0;
        tareCallCount = 0;
        waitReadyTimeoutCallCount = 0;
        readyState = false;
        nextReadValue = 0;
        queuedSamples.clear();
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

    bool wait_ready_timeout(unsigned long = 1000, unsigned long = 0) {
        waitReadyTimeoutCallCount++;
        return readyState;
    }

    long read() {
        readCallCount++;
        if (!queuedSamples.empty()) {
            long val = queuedSamples.front();
            queuedSamples.pop_front();
            nextReadValue = val;
            return val;
        }
        return nextReadValue;
    }

    long read_average(byte = 10) {
        readAverageCallCount++;
        return nextReadValue;
    }

    void tare(byte = 10) {
        tareCallCount++;
    }
};

#endif // MOCK_HX711_H
