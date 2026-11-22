#include <Arduino.h>
#include <HX711.h>

// Planlanan ESP32 (ESP-32S / WROOM 30 pin) baglanti pinleri
constexpr uint8_t PIN_HX711_DOUT = 16;
constexpr uint8_t PIN_HX711_SCK  = 4;

// Seri haberlesme ve zamanlama sabitleri
constexpr unsigned long SERIAL_BAUD = 115200UL;
constexpr unsigned long READY_TIMEOUT_MS = 1000UL;
constexpr unsigned long ERROR_REPORT_INTERVAL_MS = 2000UL;
constexpr unsigned long LOOP_POLL_DELAY_MS = 10UL;

HX711 scale;

static unsigned long lastReadyMs = 0;
static unsigned long lastErrorReportMs = 0;
static bool inTimeoutError = false;

void setup() {
    Serial.begin(SERIAL_BAUD);
    scale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);

    lastReadyMs = millis();
    lastErrorReportMs = 0;
    inTimeoutError = false;

    Serial.println("BILGI: HX711 ham ADC okuma testi basladi.");
}

void loop() {
    const unsigned long nowMs = millis();

    if (scale.is_ready()) {
        // Not: bogde/HX711 read() fonksiyonu iceride wait_ready() cagirir.
        // Yalnizca is_ready() true oldugunda tekil read() cagrilarak normal
        // akista bloklama onlenir; read_average() kesinlikle kullanilmaz.
        const long rawSample = scale.read();

        if (inTimeoutError) {
            Serial.println("BILGI: HX711 veri akisi normale dondu.");
            inTimeoutError = false;
        }

        lastReadyMs = nowMs;
        Serial.print("HAM_ADC: ");
        Serial.println(rawSample);
    } else if (nowMs - lastReadyMs >= READY_TIMEOUT_MS) {
        if (!inTimeoutError || (nowMs - lastErrorReportMs >= ERROR_REPORT_INTERVAL_MS)) {
            Serial.println("UYARI: HX711 hazir degil (DOUT=HIGH), baglantiyi kontrol edin.");
            lastErrorReportMs = nowMs;
            inTimeoutError = true;
        }
    }

    delay(LOOP_POLL_DELAY_MS);
}
