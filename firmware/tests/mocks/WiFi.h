#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#include "Arduino.h"

typedef enum {
    WIFI_OFF = 0,
    WIFI_STA = 1,
    WIFI_AP = 2,
    WIFI_AP_STA = 3
} wifi_mode_t;

class MockWiFiClass {
public:
    wifi_mode_t currentMode = WIFI_OFF;
    std::string apSsid;
    std::string apPass;
    int modeCallCount = 0;
    int softApCallCount = 0;
    bool modeResult = true;
    bool softApResult = true;

    void reset() {
        currentMode = WIFI_OFF;
        apSsid.clear();
        apPass.clear();
        modeCallCount = 0;
        softApCallCount = 0;
        modeResult = true;
        softApResult = true;
    }

    bool mode(wifi_mode_t m) {
        currentMode = m;
        modeCallCount++;
        return modeResult;
    }

    bool softAP(const char* ssid, const char* passphrase = nullptr) {
        apSsid = ssid ? ssid : "";
        apPass = passphrase ? passphrase : "";
        softApCallCount++;
        return softApResult;
    }

    IPAddress softAPIP() const {
        return IPAddress(192, 168, 4, 1);
    }
};

extern MockWiFiClass WiFi;

#endif // MOCK_WIFI_H
