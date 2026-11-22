#include "mocks/Arduino.h"
#include "mocks/HX711.h"

namespace MockArduino {
    unsigned long currentMillis = 0;
    unsigned long lastDelayMs = 0;
    unsigned long delayCallCount = 0;

    void reset() {
        currentMillis = 0;
        lastDelayMs = 0;
        delayCallCount = 0;
    }
}

MockSerialPort Serial;

// Include the actual production sketch under test
#include "../hx711_raw_test.ino"

#include <iostream>
#include <string>

static int g_failures = 0;

static void check(bool condition, const char* testName, const std::string& detail) {
    if (!condition) {
        std::cerr << "[FAIL] " << testName << ": " << detail << std::endl;
        g_failures++;
    } else {
        std::cout << "[PASS] " << testName << ": " << detail << std::endl;
    }
}

static void resetAll(unsigned long startMillis = 0) {
    MockArduino::reset();
    MockArduino::currentMillis = startMillis;
    Serial.reset();
    scale.reset();
}

static bool lineContains(const std::string& line, const std::string& needle) {
    return line.find(needle) != std::string::npos;
}

static void testSetupConfiguresSerialAndPins() {
    const char* name = "setup_configures_serial_and_hx711";
    resetAll(100);
    setup();

    check(Serial.beginCallCount == 1 && Serial.baudRate == 115200UL,
          name, "Serial.begin(115200) must be called once");
    check(scale.beginCallCount == 1 &&
          scale.configuredDout == 16 &&
          scale.configuredSck == 4 &&
          scale.configuredGain == 128,
          name, "scale.begin(16, 4, 128) must be configured");
}

static void testNotReadyNeverCallsRead() {
    const char* name = "not_ready_never_calls_read";
    resetAll(0);
    setup();
    Serial.lines.clear();

    scale.readyState = false;
    for (unsigned long t = 100; t <= 900; t += 200) {
        MockArduino::currentMillis = t;
        loop();
    }

    check(scale.readCallCount == 0 && scale.readAverageCallCount == 0,
          name, "read() or read_average() must NEVER be called while is_ready() is false");
    check(Serial.lines.empty(),
          name, "No timeout error should be emitted before READY_TIMEOUT_MS (1000 ms)");
}

static void testReadySingleSignedSampleOutput() {
    const char* name = "ready_single_signed_sample_output";
    resetAll(0);
    setup();
    Serial.lines.clear();

    const long testSamples[] = { 842150L, 0L, -1L, -125430L, -8388608L, 8388607L };
    const size_t sampleCount = sizeof(testSamples) / sizeof(testSamples[0]);

    for (size_t i = 0; i < sampleCount; ++i) {
        MockArduino::currentMillis = static_cast<unsigned long>((i + 1) * 100);
        scale.readyState = true;
        scale.nextReadValue = testSamples[i];
        loop();
    }

    check(scale.readCallCount == static_cast<int>(sampleCount),
          name, "read() must be called exactly once per ready sample");
    check(scale.readAverageCallCount == 0,
          name, "read_average() must never be called");
    check(Serial.lines.size() == sampleCount,
          name, "Each ready sample must produce exactly one serial output line");

    if (Serial.lines.size() == sampleCount) {
        for (size_t i = 0; i < sampleCount; ++i) {
            char expectedNum[32];
            snprintf(expectedNum, sizeof(expectedNum), "%ld", testSamples[i]);
            bool hasSignedValue = lineContains(Serial.lines[i], expectedNum);
            bool wronglyMarkedError = lineContains(Serial.lines[i], "HATA") ||
                                      lineContains(Serial.lines[i], "ERROR");
            check(hasSignedValue && !wronglyMarkedError,
                  name, std::string("Sample ") + expectedNum + " printed as signed value without false error: " + Serial.lines[i]);
        }
    }
}

static void testTimeoutErrorRateLimiting() {
    const char* name = "timeout_error_rate_limiting";
    resetAll(0);
    setup();
    Serial.lines.clear();

    scale.readyState = false;

    // At t = 1000 ms (exact timeout threshold), first error message must appear
    MockArduino::currentMillis = 1000;
    loop();
    check(Serial.lines.size() == 1,
          name, "First timeout error emitted at READY_TIMEOUT_MS (1000 ms)");

    // Repeated calls between 1010 ms and 2990 ms must NOT emit new error messages (rate-limited to 2000 ms)
    for (unsigned long t = 1010; t < 3000; t += 100) {
        MockArduino::currentMillis = t;
        loop();
    }
    check(Serial.lines.size() == 1,
          name, "Error output is rate-limited (still 1 message before 3000 ms)");

    // At t = 3000 ms (2000 ms after first error), second error message must appear
    MockArduino::currentMillis = 3000;
    loop();
    check(Serial.lines.size() == 2,
          name, "Second periodic error emitted at 3000 ms");

    check(scale.readCallCount == 0,
          name, "read() was never called during sustained not-ready timeout");
}

static void testAutomaticRecoveryAfterTimeout() {
    const char* name = "automatic_recovery_after_timeout";
    resetAll(0);
    setup();
    Serial.lines.clear();

    // Enter timeout state at t = 1500 ms
    scale.readyState = false;
    MockArduino::currentMillis = 1500;
    loop();
    check(Serial.lines.size() == 1,
          name, "Timeout error logged while sensor is unresponsive");

    // Sensor becomes ready at t = 1600 ms with signed negative value -450
    Serial.lines.clear();
    scale.readyState = true;
    scale.nextReadValue = -450L;
    MockArduino::currentMillis = 1600;
    loop();

    check(scale.readCallCount == 1,
          name, "read() called once upon recovery");
    check(Serial.lines.size() == 2,
          name, "Recovery emits 1 recovery status line + 1 raw ADC sample line");
    if (Serial.lines.size() == 2) {
        check(lineContains(Serial.lines[1], "-450"),
              name, "Recovered sample line contains signed reading -450: " + Serial.lines[1]);
    }

    // Next normal sample at t = 1700 ms should only emit the sample line, not another recovery line
    Serial.lines.clear();
    scale.readyState = true;
    scale.nextReadValue = -448L;
    MockArduino::currentMillis = 1700;
    loop();
    check(Serial.lines.size() == 1 && lineContains(Serial.lines[0], "-448"),
          name, "Subsequent ready sample emits only the raw reading line");

    // If sensor stops responding again, timeout is measured from last ready (1700 ms)
    Serial.lines.clear();
    scale.readyState = false;
    MockArduino::currentMillis = 2600; // 900 ms since 1700 -> not timed out yet
    loop();
    check(Serial.lines.empty(),
          name, "No premature error 900 ms after last valid sample");

    MockArduino::currentMillis = 2700; // 1000 ms since 1700 -> timeout triggers again
    loop();
    check(Serial.lines.size() == 1,
          name, "Timeout error triggers cleanly again if sensor disconnects a second time");
}

static void testMillisWrapAroundSafety() {
    const char* name = "millis_wrap_around_safety";
    const unsigned long startMs = 0xFFFFFFFFUL - 400UL; // 400 ms before 32-bit wrap
    resetAll(startMs);
    setup();
    Serial.lines.clear();

    scale.readyState = false;

    // 500 ms elapsed (100 ms past wrap-around): not timed out yet (< 1000 ms)
    MockArduino::currentMillis = startMs + 500UL;
    loop();
    check(Serial.lines.empty(),
          name, "No false timeout across millis() wrap-around at +500 ms");

    // 1000 ms elapsed across wrap-around: timeout must trigger
    MockArduino::currentMillis = startMs + 1000UL;
    loop();
    check(Serial.lines.size() == 1,
          name, "Timeout triggers accurately across millis() wrap-around at +1000 ms");

    // Rate limiting across wrap-around: +2500 ms (1500 ms after first error) -> still 1 message
    MockArduino::currentMillis = startMs + 2500UL;
    loop();
    check(Serial.lines.size() == 1,
          name, "Error rate limit holds across wrap-around");

    // +3000 ms (2000 ms after first error) -> second error message
    MockArduino::currentMillis = startMs + 3000UL;
    loop();
    check(Serial.lines.size() == 2,
          name, "Second error message triggers at +3000 ms after wrap-around");
}

int main() {
    std::cout << "=== HX711 Raw Test Host Behavioral Suite ===" << std::endl;
    testSetupConfiguresSerialAndPins();
    testNotReadyNeverCallsRead();
    testReadySingleSignedSampleOutput();
    testTimeoutErrorRateLimiting();
    testAutomaticRecoveryAfterTimeout();
    testMillisWrapAroundSafety();

    if (g_failures > 0) {
        std::cerr << "RESULT: FAILED (" << g_failures << " failed checks)" << std::endl;
        return 1;
    }
    std::cout << "RESULT: PASSED (all behavioral checks succeeded)" << std::endl;
    return 0;
}
