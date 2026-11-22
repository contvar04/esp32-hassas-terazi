#include "mocks/Arduino.h"
#include "mocks/HX711.h"
#include "mocks/Wire.h"
#include "mocks/LiquidCrystal_PCF8574.h"
#include "mocks/Preferences.h"
#include "mocks/WiFi.h"
#include "mocks/WebServer.h"

namespace MockArduino {
    unsigned long currentMillis = 0;
    unsigned long lastDelayMs = 0;
    unsigned long delayCallCount = 0;
    uint8_t pinModes[64] = {0};
    uint8_t digitalStates[64] = {0};

    void reset() {
        currentMillis = 0;
        lastDelayMs = 0;
        delayCallCount = 0;
        for (int i = 0; i < 64; ++i) {
            pinModes[i] = 0;
            digitalStates[i] = HIGH;
        }
    }
}

namespace MockNvsStore {
    std::map<std::string, float> floatStore;
    bool failBegin = false;
    bool failPut = false;
    int beginCallCount = 0;
    int endCallCount = 0;
    int putFloatCallCount = 0;
    int getFloatCallCount = 0;

    void reset() {
        floatStore.clear();
        failBegin = false;
        failPut = false;
        beginCallCount = 0;
        endCallCount = 0;
        putFloatCallCount = 0;
        getFloatCallCount = 0;
    }
}

MockSerialPort Serial;
TwoWire Wire;
MockWiFiClass WiFi;

// Gerçek üretim firmware kodunu doğrudan dahil et
#include "../firmware.ino"

#include <iostream>
#include <string>
#include <cmath>

static int g_failures = 0;

static void check(bool condition, const char* testName, const std::string& detail) {
    if (!condition) {
        std::cerr << "[FAIL] " << testName << ": " << detail << std::endl;
        g_failures++;
    } else {
        std::cout << "[PASS] " << testName << ": " << detail << std::endl;
    }
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void resetTestEnv(unsigned long startMs = 1000, bool clearNvs = true) {
    MockArduino::reset();
    MockArduino::currentMillis = startMs;
    if (clearNvs) {
        MockNvsStore::reset();
    } else {
        MockNvsStore::failBegin = false;
        MockNvsStore::failPut = false;
        MockNvsStore::beginCallCount = 0;
        MockNvsStore::endCallCount = 0;
        MockNvsStore::putFloatCallCount = 0;
        MockNvsStore::getFloatCallCount = 0;
    }
    Serial.reset();
    Wire.reset();
    WiFi.reset();
    scale.reset();
    lcd.reset();
    server.reset();
    resetFirmwareStateForTest();
}

static void feedSamples(int count, long rawValue, unsigned long stepMs = 100) {
    for (int i = 0; i < count; ++i) {
        MockArduino::currentMillis += stepMs;
        scale.readyState = true;
        scale.nextReadValue = rawValue;
        loop();
    }
}

// 1. Setup kurulumu, bloklamama, 4 HTTP rotası ve PROGMEM send_P kontrolü
static void testSetupNonBlockingAndRoutes() {
    const char* name = "setup_non_blocking_and_routes";
    resetTestEnv(500);
    setup();

    check(Serial.beginCallCount == 1 && Serial.baudRate == 115200UL,
          name, "Serial.begin(115200) called once");
    check(scale.beginCallCount == 1 && scale.configuredDout == 16 && scale.configuredSck == 4 && scale.configuredGain == 128,
          name, "HX711 configured on DOUT=16, SCK=4, GAIN=128");
    check(scale.readCallCount == 0 && scale.readAverageCallCount == 0 && scale.tareCallCount == 0,
          name, "setup() never blocks on HX711 read/read_average/tare");
    check(MockArduino::pinModes[PIN_TARE_BUTTON] == INPUT_PULLUP,
          name, "Tare button GPIO27 configured as INPUT_PULLUP");
    check(Wire.beginCallCount >= 1 && Wire.sdaPin == 21 && Wire.sclPin == 22,
          name, "Wire.begin(21, 22) configured for I2C");
    check(lcd.beginCallCount >= 1 && lcd.cols == 20 && lcd.rows == 4,
          name, "20x4 LCD initialized with Wire");
    check(LCD_I2C_ADDR == 0x27 && lcd.i2cAddr == 0x27,
          name, "LCD uses hardware-scanned address 0x27");
    check(WiFi.modeCallCount == 1 && WiFi.currentMode == WIFI_AP && WiFi.softApCallCount == 1,
          name, "WiFi SoftAP started");
    check(server.beginCallCount == 1 && server.routes.size() == 4,
          name, "Exactly 4 HTTP routes registered");

    // GET / -> PROGMEM send_P ile tablet HTML döner
    MockHttpResponse rootRes = server.dispatchRequest(HTTP_GET, "/");
    check(rootRes.statusCode == 200 && rootRes.usedSendP && contains(rootRes.contentType, "text/html") &&
          rootRes.body.size() == TABLET_INDEX_HTML_LEN && contains(rootRes.body, "TeraziCore"),
          name, "GET / serves embedded tablet HTML from PROGMEM via send_P");

    // Sensör yokken bile GET /api/weight çalışır, ağırlık null döner
    MockHttpResponse snapRes = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(snapRes.statusCode == 200 && contains(snapRes.body, "\"grams\":null") &&
          contains(snapRes.body, "\"sensor_ok\":false") &&
          contains(snapRes.body, "\"seq\":0") &&
          contains(snapRes.body, "\"sample_age_ms\":null"),
          name, "GET /api/weight works without sensor and returns null grams/age with seq 0: " + snapRes.body);

    // Wi-Fi AP başlatma hatası (WiFi.mode veya WiFi.softAP false dönerse) görünür seri/LCD durumuna taşınmalı,
    // yanlış "Wi-Fi AP hazir, IP:" mesajı veya sahte IP gösterilmemeli, ancak donanım ölçümü ve HTTP rotaları çalışmayı sürdürmeli!
    resetTestEnv(500);
    WiFi.softApResult = false;
    setup();
    bool hasFakeApReadyMsg = false;
    bool hasApErrorMsg = false;
    for (const auto& l : Serial.lines) {
        if (contains(l, "Wi-Fi AP hazir")) hasFakeApReadyMsg = true;
        if (contains(l, "HATA") && contains(l, "Wi-Fi AP")) hasApErrorMsg = true;
    }
    check(!hasFakeApReadyMsg && hasApErrorMsg,
          name, "WiFi.softAP() == false logs visible AP error on Serial and never logs 'Wi-Fi AP hazir, IP: 192.168.4.1'");
    check(contains(lcd.rowText[3], "AP:HATA") && !contains(lcd.rowText[3], "192.168.4.1"),
          name, "WiFi.softAP() == false shows AP error on LCD row 3 instead of fake IP: " + lcd.rowText[3]);
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 1000L);
    check(WiFi.softApCallCount == 1 && g_state.sensorOk && g_state.hasZeroReference,
          name, "Hardware measurement continues normally without infinite AP retry loops when softAP fails");

    resetTestEnv(500);
    WiFi.modeResult = false;
    setup();
    bool hasModeApError = false;
    for (const auto& l : Serial.lines) {
        if (contains(l, "Wi-Fi AP hazir")) hasModeApError = false;
        if (contains(l, "HATA") && contains(l, "Wi-Fi AP")) hasModeApError = true;
    }
    check(hasModeApError && contains(lcd.rowText[3], "AP:HATA"),
          name, "WiFi.mode() == false is also reported as AP error on Serial and LCD");
}

// 2. NVS başlangıç doğrulaması (0, NaN, Inf kalibrasyonsuz; negatif ve pozitif K geçerli)
static void testNvsStartupValidationAndNegativeK() {
    const char* name = "nvs_startup_validation_and_negative_k";

    // Geçersiz K = 0.0f -> calibrated: false
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 0.0f;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 10000L);
    MockHttpResponse rZero = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(rZero.body, "\"calibrated\":false") && contains(rZero.body, "\"grams\":null"),
          name, "Zero scale factor in NVS treated as uncalibrated: " + rZero.body);

    // Geçersiz K = NAN -> calibrated: false
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = NAN;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 10000L);
    MockHttpResponse rNan = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(rNan.body, "\"calibrated\":false") && contains(rNan.body, "\"grams\":null"),
          name, "NaN scale factor in NVS treated as uncalibrated: " + rNan.body);

    // Geçersiz K = INFINITY -> calibrated: false
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = INFINITY;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 10000L);
    MockHttpResponse rInf = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(rInf.body, "\"calibrated\":false") && contains(rInf.body, "\"grams\":null"),
          name, "Infinity scale factor in NVS treated as uncalibrated: " + rInf.body);

    // Geçerli negatif K = -400.0f (sayım/gram) -> calibrated: true ve işaret korunur
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = -400.0f;
    setup();
    // Açılış boş platform sıfırlaması: R0 = 100000
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 100000L);
    // Ters yönlü sensörde +250 g yük altında ham değer azalır: R = 0 -> (0 - 100000) / -400 = +250.0 g
    feedSamples(FILTER_WINDOW_SIZE, 0L);
    MockHttpResponse rNegK = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(rNegK.body, "\"calibrated\":true") && contains(rNegK.body, "\"grams\":250.00"),
          name, "Negative K (-400 counts/g) preserved and computes +250.00 g at R=0: " + rNegK.body);
}

// 3. Boş referans -> bilinen yük 2 adımlı kalibrasyon, dara, negatif net ağırlık ve filtre izolasyonu
static void testTareAndCalibrationFlowWithFilterIsolation() {
    const char* name = "tare_and_calibration_flow_with_filter_isolation";
    resetTestEnv(1000, true);
    setup();

    // Açılışta boş platform sıfırlaması (3 örnek, R0 = -50000)
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, -50000L);
    MockHttpResponse snapAfterBoot = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(snapAfterBoot.body, "\"sensor_ok\":true") &&
          contains(snapAfterBoot.body, "\"calibrated\":false") &&
          contains(snapAfterBoot.body, "\"seq\":3"),
          name, "Startup zero finishes after 3 samples with negative raw offset -50000");

    // Yüksüz (sıfır fark) kalibrasyon denemesi reddedilmeli (R = -50000 == R0)
    MockHttpResponse zeroDiffCal = server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":1000}");
    check(zeroDiffCal.statusCode >= 400 && zeroDiffCal.statusCode < 500 && contains(zeroDiffCal.body, "\"ok\":false"),
          name, "Zero raw difference calibration rejected with 4xx: " + zeroDiffCal.body);

    // Geçersiz JSON / NaN / negatif / kapasite üstü (>5000g) / aşırı büyük gövde reddedilmeli
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":0}").statusCode == 400,
          name, "known_g=0 rejected with 400");
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":-10}").statusCode == 400,
          name, "known_g=-10 rejected with 400");
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":5001}").statusCode == 400,
          name, "known_g=5001 (>5000g capacity) rejected with 400");
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":\"1000\"}").statusCode == 400,
          name, "String known_g rejected with 400");
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", "bozuk_json").statusCode == 400,
          name, "Malformed JSON rejected with 400");
    std::string hugeBody(300, ' ');
    check(server.dispatchRequest(HTTP_POST, "/api/calibrate", hugeBody).statusCode == 413,
          name, "Oversized body (>256 bytes) rejected with 413");

    // Şimdi platforma 1000 g bilinen yük konur; ters yönlü sensör örneği: R = -450000 (fark = -400000, K = -400 sayım/g)
    // Önce tek bir örnek gelir, ardından önce RFC 8259 JSON sayı sözdizimi ve float underflow redleri sınanır:
    feedSamples(1, -450000L);
    uint32_t opIdBeforeInvalid = g_state.opId;
    const char* nonRfcJsonBodies[] = {
        "{\"known_g\":01}",
        "{\"known_g\":1.}",
        "{\"known_g\":0x1p0}",
        "{\"known_g\":.5}",
        "{\"known_g\":+10}",
        "{\"known_g\":1e}",
        "{\"known_g\":1e+}",
        "{\"known_g\":1e-50}"
    };
    for (const char* badBody : nonRfcJsonBodies) {
        MockHttpResponse badRes = server.dispatchRequest(HTTP_POST, "/api/calibrate", badBody);
        check(badRes.statusCode == 400 && !g_state.busy && g_state.opId == opIdBeforeInvalid && g_state.opTargetKnownGrams == 0.0f,
              name, std::string("Invalid JSON number or float underflow rejected with 400 without starting op: ") + badBody + " -> " + badRes.body);
    }

    // Geçerli RFC 8259 JSON sayıları (ör. 1.5, 1e3 ve JSON whitespace) kabul edilmelidir:
    MockHttpResponse calAccept = server.dispatchRequest(HTTP_POST, "/api/calibrate", " \t\r\n{ \"known_g\" : 1e3 }\n ");
    check(calAccept.statusCode == 202 && contains(calAccept.body, "\"status\":\"accepted\"") &&
          contains(calAccept.body, "\"busy\":true"),
          name, "POST /api/calibrate accepts valid scientific JSON number 1e3 with whitespace and returns 202 Accepted: " + calAccept.body);

    // Kalibrasyon örnekleri tamamlanmadan GET /api/weight busy:true ve grams:null döner
    feedSamples(1, -450000L);
    MockHttpResponse calBusy = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(calBusy.body, "\"busy\":true") && contains(calBusy.body, "\"grams\":null"),
          name, "While collecting calibration samples, snapshot is busy and grams is null");

    // 3. taze örnek geldiğinde kalibrasyon tamamlanır; eski filtre örnekleri (-50000) yeni yükle (-450000) KARIŞMAZ!
    feedSamples(2, -450000L);
    MockHttpResponse calDone = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(calDone.body, "\"busy\":false") &&
          contains(calDone.body, "\"calibrated\":true") &&
          contains(calDone.body, "\"op_status\":\"completed\"") &&
          contains(calDone.body, "\"grams\":1000.00"),
          name, "Calibration completes with exact 1000.00 g (no old filter contamination) and negative K saved: " + calDone.body);
    check(MockNvsStore::putFloatCallCount == 1 && std::fabs(MockNvsStore::floatStore[NVS_KEY_SCALE] - (-400.0f)) < 0.01f,
          name, "NVS written exactly once with K = -400.0 counts/g");

    // Şimdi 1000 g yük varken POST /api/tare ile dara alınır (flash yazması olmamalı!)
    int nvsWritesBeforeTare = MockNvsStore::putFloatCallCount;
    MockHttpResponse tareAccept = server.dispatchRequest(HTTP_POST, "/api/tare");
    check(tareAccept.statusCode == 202 && contains(tareAccept.body, "\"operation\":\"tare\""),
          name, "POST /api/tare returns 202 Accepted");
    feedSamples(TARE_SAMPLE_COUNT, -450000L);
    MockHttpResponse afterTare = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(afterTare.body, "\"grams\":0.00") && contains(afterTare.body, "\"op_status\":\"completed\""),
          name, "After tare at -450000, net weight is 0.00 g: " + afterTare.body);
    check(MockNvsStore::putFloatCallCount == nvsWritesBeforeTare,
          name, "Tare never writes to NVS flash");

    // Yük kaldırıldığında (R = -50000'e döndüğünde) net ağırlık -1000.00 g (negatif net) olmalıdır!
    feedSamples(FILTER_WINDOW_SIZE, -50000L);
    MockHttpResponse negNet = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(negNet.body, "\"grams\":-1000.00"),
          name, "Removing tared 1000g load yields negative net weight -1000.00 g: " + negNet.body);
}

// 4. NVS yazma başarısızlığında önceki geçerli kalibrasyon korunur ve sahte başarı verilmez
static void testNvsWriteFailurePreservesPreviousCalibration() {
    const char* name = "nvs_write_failure_preserves_previous_calibration";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 200.0f; // Mevcut geçerli K = 200 sayım/g
    setup();

    // Boş sıfırlama: R0 = 0
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L);
    // 500 g yük: R = 100000 -> 100000 / 200 = 500.00 g
    feedSamples(FILTER_WINDOW_SIZE, 100000L);
    MockHttpResponse beforeFail = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(beforeFail.body, "\"grams\":500.00"),
          name, "Initial calibrated weight is 500.00 g with K=200");

    // NVS yazma hatasını simüle et ve 1000 g olarak yeniden kalibre etmeyi dene (yeni K=100 olacaktı)
    MockNvsStore::failPut = true;
    MockHttpResponse calReq = server.dispatchRequest(HTTP_POST, "/api/calibrate", "{\"known_g\":1000}");
    check(calReq.statusCode == 202, name, "Calibration request accepted for sampling");
    feedSamples(CALIBRATE_SAMPLE_COUNT, 100000L);

    MockHttpResponse afterFail = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(afterFail.body, "\"op_status\":\"failed\"") &&
          !contains(afterFail.body, "\" error\":null") &&
          contains(afterFail.body, "\"calibrated\":true") &&
          contains(afterFail.body, "\"grams\":500.00"),
          name, "NVS write failure reports op_status=failed and preserves previous K=200 (500.00 g): " + afterFail.body);
}

// 5. Fiziksel dara butonu debounce ve kenar tetikleme (basılı tutarken tekrar tetiklememe)
static void testPhysicalButtonDebounceAndEdgeTrigger() {
    const char* name = "physical_button_debounce_and_edge_trigger";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L);
    feedSamples(FILTER_WINDOW_SIZE, 25000L); // 250.00 g

    // 20 ms'lik kısa parazit (< 50 ms debounce) dara başlatmamalı
    MockArduino::digitalStates[PIN_TARE_BUTTON] = LOW;
    MockArduino::currentMillis += 20;
    loop();
    MockArduino::digitalStates[PIN_TARE_BUTTON] = HIGH;
    MockArduino::currentMillis += 20;
    loop();
    MockHttpResponse afterGlitch = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(afterGlitch.body, "\"grams\":250.00") && contains(afterGlitch.body, "\"busy\":false"),
          name, "Short 20ms button glitch ignored by debounce");

    const uint32_t priorOp = g_state.opId;
    MockArduino::digitalStates[PIN_TARE_BUTTON] = LOW;
    loop();
    MockArduino::currentMillis += 60;
    loop();
    feedSamples(16, 35000L);
    check(g_state.opId == priorOp && g_state.rawZeroOffset == 0.0,
          name, "Holding the button does not sample or start tare");

    MockArduino::digitalStates[PIN_TARE_BUTTON] = HIGH;
    loop();
    MockArduino::currentMillis += 60;
    loop();
    feedSamples(4, 35000L, 50);
    check(g_state.rawZeroOffset == 0.0 && g_state.opCollectedSamples == 0,
          name, "Transient samples in the first 200 ms after stable release are discarded");

    feedSamples(4, 25000L);
    check(g_state.busy && g_state.rawZeroOffset == 0.0,
          name, "Tare waits for more than three settled samples");
    feedSamples(1, 31000L);
    feedSamples(1, 21000L);
    feedSamples(5, 25000L);
    check(g_state.opId == priorOp + 1 && !g_state.busy && g_state.rawZeroOffset == 25000.0,
          name, "Physical tare runs once and trims a high and a low outlier");
    feedSamples(FILTER_WINDOW_SIZE, 0L);
    const auto afterUnload = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(afterUnload.body, "\"grams\":-250.00"),
          name, "Unloading a tared load still produces a signed negative measurement");
}

// 6. Saturasyon, sensör kopması/toparlanması, seq artışı ve 32-bit millis() taşması
static void testSaturationTimeoutRecoveryAndMillisWrap() {
    const char* name = "saturation_timeout_recovery_and_millis_wrap";
    const unsigned long nearWrapMs = 0xFFFFFFFFUL - 300UL;
    resetTestEnv(nearWrapMs, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    setup();

    // millis() 32-bit taşma sınırını geçerken açılış sıfırlaması (3 örnek, R0 = 0)
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L, 150);
    // +100 g ölçüm (R = 10000)
    feedSamples(FILTER_WINDOW_SIZE, 10000L, 100);
    MockHttpResponse validSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(validSnap.body, "\"grams\":100.00") && contains(validSnap.body, "\"seq\":6"),
          name, "Valid measurement across millis() 32-bit wrap-around with seq=6: " + validSnap.body);

    // Yeni örnek gelmeden 400 ms geçtiğinde seq artmaz, sample_age_ms 400 olur
    scale.readyState = false;
    MockArduino::currentMillis += 400;
    loop();
    MockHttpResponse agedSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(agedSnap.body, "\"seq\":6") && contains(agedSnap.body, "\"sample_age_ms\":400") &&
          contains(agedSnap.body, "\"sensor_ok\":true"),
          name, "seq does not increment without new sample; sample_age_ms tracks 400 ms: " + agedSnap.body);

    // 1000 ms (toplam) sensör hazır olmadığında zaman aşımı -> sensor_ok:false, grams:null
    MockArduino::currentMillis += 600;
    loop();
    MockHttpResponse timeoutSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(timeoutSnap.body, "\"sensor_ok\":false") &&
          contains(timeoutSnap.body, "\"grams\":null") &&
          contains(timeoutSnap.body, "\"sample_age_ms\":1000"),
          name, "Sensor timeout at 1000ms sets sensor_ok=false and grams=null: " + timeoutSnap.body);

    // Sensör geri geldiğinde İLK taze örnekte (seq=7) eski filtre örnekleri (10000L -> 100g) yeni örnekle (20000L -> 200g) KARIŞMAMALI!
    feedSamples(1, 20000L, 100);
    MockHttpResponse firstRecoveredSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(firstRecoveredSnap.body, "\"sensor_ok\":true") &&
          contains(firstRecoveredSnap.body, "\"grams\":200.00") &&
          contains(firstRecoveredSnap.body, "\"seq\":7"),
          name, "First sample after timeout recovery reflects only new epoch (200.00 g, no 133.33 g old filter mix): " + firstRecoveredSnap.body);

    // Uzun boşluk sonrası (>= 1000 ms) yeni numune ilk loop'ta hazır geldiğinde de eski filtre karışmamalı:
    MockArduino::currentMillis += 1200;
    scale.readyState = true;
    scale.nextReadValue = 30000L; // 300.00 g
    loop();
    MockHttpResponse gapFirstSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(gapFirstSnap.body, "\"sensor_ok\":true") &&
          contains(gapFirstSnap.body, "\"grams\":300.00") &&
          contains(gapFirstSnap.body, "\"seq\":8"),
          name, "First sample arriving immediately after >=1000ms stale gap clears old filter and reports 300.00 g: " + gapFirstSnap.body);

    // Daralırken (tare busy iken) sensör zaman aşımı olursa işlem op_status:failed olmalı:
    MockHttpResponse tareStart = server.dispatchRequest(HTTP_POST, "/api/tare");
    check(tareStart.statusCode == 202, name, "Tare started before timeout test");
    feedSamples(1, 30000L, 100);
    scale.readyState = false;
    MockArduino::currentMillis += 1000;
    loop();
    MockHttpResponse tareTimeoutSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(tareTimeoutSnap.body, "\"sensor_ok\":false") &&
          contains(tareTimeoutSnap.body, "\"busy\":false") &&
          contains(tareTimeoutSnap.body, "\"operation\":\"tare\"") &&
          contains(tareTimeoutSnap.body, "\"op_status\":\"failed\""),
          name, "Timeout during active tare fails the operation cleanly: " + tareTimeoutSnap.body);

    // Sensör tekrar geldiğinde 20000L ile devam eder
    feedSamples(FILTER_WINDOW_SIZE, 20000L, 100);
    MockHttpResponse recoveredSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(recoveredSnap.body, "\"sensor_ok\":true") &&
          contains(recoveredSnap.body, "\"grams\":200.00") &&
          contains(recoveredSnap.body, "\"seq\":12"),
          name, "Sensor automatically recovers and resumes valid output: " + recoveredSnap.body);

    // ADC saturasyon ray değeri (8388607L veya -8388608L) hata sayılır, seq artmaz, grams null olur
    MockArduino::currentMillis += 100;
    scale.readyState = true;
    scale.nextReadValue = HX711_RAW_SAT_MAX;
    loop();
    MockHttpResponse satSnap = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(satSnap.body, "\"sensor_ok\":false") &&
          contains(satSnap.body, "\"grams\":null") &&
          contains(satSnap.body, "\"seq\":12"),
          name, "ADC saturation value (8388607) flagged as error without incrementing seq: " + satSnap.body);
}

// 7. LCD yokluğunda sistemin çalışmaya devam etmesi ve aralıklı titreşimsiz (clear'sız) güncelleme
static void testLcdAbsenceAndFlickerFreeUpdate() {
    const char* name = "lcd_absence_and_flicker_free_update";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    lcd.connected = false; // LCD takılı değil!
    setup();

    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L, 100);
    feedSamples(FILTER_WINDOW_SIZE, 50000L, 100);
    MockHttpResponse snapNoLcd = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(snapNoLcd.body, "\"grams\":500.00") && lcd.printCallCount == 0,
          name, "Missing LCD does not block measurement or HTTP API and skips I2C print calls");

    // LCD sonradan bağlandığında otomatik algılanır ve loop içinde clear() çağrılmadan güncellenir
    lcd.connected = true;
    int clearsBeforeLoop = lcd.clearCallCount;
    MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS + 10;
    scale.readyState = false;
    loop();
    int printsAfterFirstUpdate = lcd.printCallCount;
    check(printsAfterFirstUpdate > 0 && lcd.clearCallCount == clearsBeforeLoop,
          name, "Connected LCD updates rows without calling lcd.clear() in loop");

    // Veri değişmediyse aynı satırlar tekrar I2C üzerinden basılmaz
    MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS + 10;
    loop();
    check(lcd.printCallCount == printsAfterFirstUpdate,
          name, "Unchanged LCD rows are not redundantly reprinted");
}

static void testWholeGramLcdDisplay() {
    const char* name = "whole_gram_lcd_display";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L, 100);
    feedSamples(FILTER_WINDOW_SIZE, 26150L, 100);
    MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS;
    loop();
    check(contains(lcd.rowText[1], "262 g"), name, "LCD rounds 261.50 g up to 262 g: " + lcd.rowText[1]);
    const MockHttpResponse snapshot = server.dispatchRequest(HTTP_GET, "/api/weight");
    check(contains(snapshot.body, "\"grams\":261.50"), name, "HTTP measurement retains fractional grams");
    feedSamples(FILTER_WINDOW_SIZE, -20L, 100);
    MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS;
    loop();
    check(!contains(lcd.rowText[1], "-") && contains(lcd.rowText[1], "0 g"),
          name, "LCD shows 0 g without a negative sign near zero");
    check(contains(lcd.rowText[2], "0.000kg") && contains(lcd.rowText[2], "0.00N") && !contains(lcd.rowText[2], "-"),
          name, "LCD shows unsigned zero in kg and N: " + lcd.rowText[2]);
    feedSamples(FILTER_WINDOW_SIZE, -21600L, 100);
    MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS;
    loop();
    check(contains(lcd.rowText[1], "-216 g") && contains(lcd.rowText[2], "-0.216kg") && contains(lcd.rowText[2], "-2.12N"),
          name, "LCD preserves the sign of nonzero negative net weight: " + lcd.rowText[1] + " / " + lcd.rowText[2]);
    check(contains(server.dispatchRequest(HTTP_GET, "/api/weight").body, "\"grams\":-216.00"),
          name, "Signed net measurement is preserved for the API");
}

static void testSerialMeasurementDiagnostics() {
    const char* name = "serial_measurement_diagnostics";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    setup();
    feedSamples(1, 1000L);
    check(Serial.lines.back() == "TERAZI ms=1100 raw=1000 net_g=NA",
          name, "A fresh raw sample is visible during startup, without fake grams");
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT - 1, 1000L);
    feedSamples(FILTER_WINDOW_SIZE, 950L);
    check(contains(Serial.lines.back(), "raw=950 net_g=-0.500"),
          name, "Serial exposes the signed net value before display rounding");
    const auto lineCount = Serial.lines.size();
    scale.readyState = false;
    loop();
    check(Serial.lines.size() == lineCount, name, "No fresh sample means no duplicate diagnostic row");
}

static void testSharedGramHysteresis() {
    const char* name = "shared_gram_hysteresis";
    resetTestEnv(1000, true);
    MockNvsStore::floatStore[NVS_KEY_SCALE] = 100.0f;
    setup();
    feedSamples(STARTUP_ZERO_SAMPLE_COUNT, 0L);
    const auto show = [&](long raw, const char* lcdText, const char* apiText) {
        feedSamples(FILTER_WINDOW_SIZE, raw);
        MockArduino::currentMillis += LCD_UPDATE_INTERVAL_MS;
        loop();
        const auto snap = server.dispatchRequest(HTTP_GET, "/api/weight");
        check(contains(lcd.rowText[1], lcdText) && contains(snap.body, apiText), name, lcd.rowText[1] + snap.body);
    };
    show(6768L, "68 g", "\"display_grams\":68");
    show(6736L, "68 g", "\"display_grams\":68");
    check(contains(lcd.rowText[2], "0.068kg") && contains(lcd.rowText[2], "0.67N"),
          name, "LCD kg and N derive from stable 68 g: " + lcd.rowText[2]);
    check(contains(server.dispatchRequest(HTTP_GET, "/api/weight").body, "\"grams\":67.36"), name, "Raw net API precision is intact");
    show(6729L, "67 g", "\"display_grams\":67");
    show(6768L, "67 g", "\"display_grams\":67");
    show(6771L, "68 g", "\"display_grams\":68");
    show(-6800L, "-68 g", "\"display_grams\":-68");
    show(0L, "0 g", "\"display_grams\":0");
    check(server.dispatchRequest(HTTP_POST, "/api/tare").statusCode == 202, name, "Tare starts");
    check(contains(server.dispatchRequest(HTTP_GET, "/api/weight").body, "\"display_grams\":null"), name, "No stale display weight during tare");
}

int main() {
    std::cout << "=== Hassas Terazi Firmware Host Behavioral Suite ===" << std::endl;
    testSetupNonBlockingAndRoutes();
    testNvsStartupValidationAndNegativeK();
    testTareAndCalibrationFlowWithFilterIsolation();
    testNvsWriteFailurePreservesPreviousCalibration();
    testPhysicalButtonDebounceAndEdgeTrigger();
    testSaturationTimeoutRecoveryAndMillisWrap();
    testLcdAbsenceAndFlickerFreeUpdate();
    testWholeGramLcdDisplay();
    testSerialMeasurementDiagnostics();
    testSharedGramHysteresis();

    if (g_failures > 0) {
        std::cerr << "RESULT: FAILED (" << g_failures << " failed checks)" << std::endl;
        return 1;
    }
    std::cout << "RESULT: PASSED (all firmware behavioral checks succeeded)" << std::endl;
    return 0;
}
