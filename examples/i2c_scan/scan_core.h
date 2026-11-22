#pragma once
#include <stdint.h>
struct ScanResult { unsigned found; unsigned errors; bool completed; };
template<class Bus, class Reporter>
ScanResult scanI2c(Bus& bus, Reporter& reporter, bool sdaHigh, bool sclHigh) {
    ScanResult result = {0, 0, false};
    if (!sdaHigh || !sclHigh) return result;
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        bus.beginTransmission(address);
        const uint8_t status = bus.endTransmission();
        if (status == 0) {
            ++result.found;
            reporter.found(address);
        } else if (status != 2) {
            ++result.errors;
            reporter.error(address, status);
        }
    }
    result.completed = true;
    return result;
}
